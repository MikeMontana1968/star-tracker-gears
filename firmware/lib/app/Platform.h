// =====================================================================
//  Platform -- everything the night FSM touches, as small interfaces
//  (FIRMWARE.md s3). The ESP32 implements them over lib/motion + lib/hal;
//  the host simulator (test/test_fsm_sim) fakes them with a clock it can
//  run at any speed. The FSM itself never includes an Arduino header.
//
//  Two rules the interfaces encode:
//   * Nothing blocks. Camera and GPS operations are started, then polled;
//     every wait is a deadline the FSM checks on its own tick.
//   * Deep sleep is a reboot. sleepUntil() does not return on the device;
//     the next boot rebuilds the FSM from the persisted record. The
//     simulator does exactly the same, so every night exercises resume.
// =====================================================================
#pragma once
#include <stdint.h>

namespace app {

enum class CamPolicy : uint8_t { None, BestEffort, Required };
enum class GpsPolicy : uint8_t { None, BestEffort };
enum class Op : uint8_t { Pending, Ok, Fail };
enum class Wake : uint8_t { PowerOn, Alarm, Button };

// One night as the schedule resolves it (slice 4 computes these from
// twilight; tests hand them in directly).
struct NightPlan {
  bool      valid = false;
  int64_t   start_utc = 0, stop_utc = 0;   // tracking window
  double    start_deg = 0;                 // output angle at start_utc
  CamPolicy cam = CamPolicy::BestEffort;
  GpsPolicy gps = GpsPolicy::BestEffort;
  int32_t   day = 0;                       // the night's local date, days since 1970 (its key)
  uint8_t   fallback = 0;                  // twilight levels fallen back (astro -> nautical = 1)
  char      camera[24] = {};               // camera profile name
};

struct IClock {
  virtual int64_t  utc() = 0;              // DS3231, seconds
  virtual uint64_t ms() = 0;               // monotonic since boot
  virtual bool     setUtc(int64_t t) = 0;  // GPS sync
  virtual bool     lostPower() = 0;        // RTC oscillator stopped: time untrusted
  virtual ~IClock() {}
};

struct IMotion {
  virtual bool   setPosition(double deg) = 0;               // seed (idle)
  virtual bool   moveTo(double deg) = 0;                     // moveToForward, 1/8
  virtual bool   startTracking(double origin_deg, int64_t origin_utc) = 0;  // 1/32
  virtual void   stop() = 0;
  virtual bool   moving() = 0;                               // slewing or tracking
  virtual bool   tracking() = 0;
  virtual bool   arrived() = 0;                              // last move ended on target
  virtual bool   limitHit() = 0;                             // tracking reached the envelope
  virtual double positionDeg() = 0;                          // step counter
  virtual void   enable(bool on) = 0;                        // driver EN
  virtual ~IMotion() {}
};

struct ISensors {
  virtual bool   encoderDeg(double *deg) = 0;    // false: not answering / no magnet
  virtual double batteryV() = 0;
  virtual ~ISensors() {}
};

struct IGps {
  virtual void power(bool on) = 0;
  // true once there is a usable 3-D fix; fills what it knows
  virtual bool fix(int64_t *utc, double *lat, double *lon) = 0;
  virtual ~IGps() {}
};

enum class CamStep : uint8_t { Wake, Config, Start, Stop };
struct ICamera {
  virtual void power(bool on) = 0;           // GPIO 32 load switch
  virtual void begin(CamStep s) = 0;         // start one step...
  virtual Op   poll() = 0;                   // ...and poll it; never blocks
  virtual void cancel() = 0;                 // abandon the step in flight
  virtual ~ICamera() {}
};

enum class Outcome : uint8_t { Completed, Skipped, SafeStopped, Faulted };

// one record per night, written on the way back to IDLE (FIRMWARE.md s7)
struct NightReport {
  NightPlan plan;
  Outcome   outcome = Outcome::Completed;
  char      reason[48] = {};
  int64_t   track_start_utc = 0, track_stop_utc = 0;
  double    max_div_deg = 0;      // encoder vs step counter during TRACK
  double    min_battery_v = 99;
  Op        camera = Op::Pending; // Ok, Fail, or Pending = not attempted
  Op        gps = Op::Pending;
  int64_t   rtc_drift_s = 0;      // RTC minus GPS at sync
  uint16_t  warnings = 0;
  uint16_t  resumes = 0;          // reboots survived mid-night
};

// ---- persisted on every transition (NVS on the device) -----------------
struct Persist {
  uint32_t    magic = 0;
  uint8_t     state = 0;              // app::State
  uint8_t     fault = 0;              // app::Fault
  bool        night_active = false;   // `night` is a night in progress
  NightReport night;                  // its plan and report so far (resume needs both)
  double      pos_deg = 0;            // where the step counter was
  int64_t     last_done_stop_utc = 0; // stop time of the last finished/skipped night
  uint32_t    boot_count = 0;
  static constexpr uint32_t MAGIC = 0x46534D32;   // "FSM2"
};
struct IStore {
  virtual bool load(Persist *p) = 0;
  virtual void save(const Persist &p) = 0;
  virtual void report(const NightReport &r) = 0;
  virtual ~IStore() {}
};

struct ISchedule {
  // the next night whose stop is after `after_utc` (invalid: nothing planned)
  virtual NightPlan next(int64_t after_utc) = 0;
  // GPS position for the twilight sums (overrides the schedule's site), and
  // re-resolve a night already chosen with it. Defaults: no-ops.
  virtual void setSite(double lat, double lon) { (void)lat; (void)lon; }
  virtual NightPlan refresh(const NightPlan &p) { return p; }
  virtual ~ISchedule() {}
};

struct ISystem {
  // Arm the RTC alarm (0 = none: button only) and deep sleep. Never returns
  // on the device. The simulator records it and reboots the FSM.
  virtual void sleepUntil(int64_t wake_utc) = 0;
  virtual void log(const char *msg) = 0;
  virtual ~ISystem() {}
};

struct Platform {
  IClock &clock; IMotion &motion; ISensors &sensors; IGps &gps;
  ICamera &camera; IStore &store; ISchedule &schedule; ISystem &sys;
};

}  // namespace app
