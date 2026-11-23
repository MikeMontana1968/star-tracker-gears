// =====================================================================
//  EspPlatform -- app::Platform on the Rev C board: the night FSM's
//  interfaces implemented over lib/motion + lib/hal.
//
//    clock    DS3231, read every 10 s and carried on millis() between
//    motion   StepEngine + StepTimer + Tmc2209: 1/8 to slew, 1/32 to track;
//             service() runs the jam / slip guards and the driver-reset
//             watchdog (any of them stops the move; the FSM sees it)
//    sensors  AS5600 through the NVS encoder map; battery divider
//    gps      NEO-6M on UART1 (RX 18 / TX 5, 9600), power on GPIO 17
//    camera   src/EspCamera (GoPro HERO6 over gpControl, in a core-0 task)
//    store    Persist in NVS; night reports appended to LittleFS
//             /nights.ndjson; the schedule on LittleFS, atomic writes,
//             /schedule.good.json kept as the fallback
//    system   deep sleep with the DS3231 alarm -- or, with "stay awake"
//             set (the bench default), wait in place and restart at the
//             wake time, which the FSM cannot tell from a wake
// =====================================================================
#pragma once
#include <Arduino.h>
#include "Platform.h"
#include "Schedule.h"
#include "StepEngine.h"
#include "Tmc2209.h"
#include "As5600.h"
#include "Ds3231.h"
#include "EncoderMap.h"
#include "Nmea.h"
#include "Oled.h"
#include "StepTimer.h"

namespace esp {

class Clock : public app::IClock {
 public:
  explicit Clock(Ds3231 &rtc) : rtc_(rtc) {}
  void begin();
  int64_t  utc() override;
  int64_t  utcMs();
  uint64_t ms() override { return millis(); }
  bool     setUtc(int64_t t) override;
  bool     lostPower() override { return lost_; }
 private:
  void refresh();
  Ds3231 &rtc_;
  int64_t base_utc_ = 0; uint32_t base_ms_ = 0, last_read_ = 0;
  bool lost_ = true, ok_ = false;
};

class Motion : public app::IMotion {
 public:
  Motion(motion::StepEngine &e, Tmc2209 &t, Clock &c) : eng_(e), tmc_(t), clock_(c) {}
  void begin(bool uart_ok) { uart_ok_ = uart_ok; }
  bool   setPosition(double deg) override;
  bool   moveTo(double deg) override;
  bool   startTracking(double origin_deg, int64_t origin_utc, app::TrackRate rate = app::TrackRate::Sidereal) override;
  void   stop() override;
  bool   moving() override;
  bool   tracking() override;
  bool   arrived() override;
  bool   limitHit() override;
  double positionDeg() override;
  void   enable(bool on) override { enabled_ = on; drive(); }
  void   setOvershootDeg(double d) override;
  double overshootDeg() override;
  void     setSlewHz(uint32_t hz) override { motion::MotionLock l; eng_.setSlewMax(hz); slew_hz_ = hz; }
  uint32_t slewHz() override { return slew_hz_; }
  uint32_t slew_hz_ = 0;                          // set by main from SLEW_MAX_HZ
  // call every loop: guards, watchdog, EN
  void   service(class Sensors &s);
  const char *lastGuard() const { return guard_; }
  int64_t servoError();                           // target - position, 1/32 units (s7)
  bool   uartOk() const { return uart_ok_; }
 private:
  bool useMicrosteps(uint16_t u);
  void drive();
  motion::StepEngine &eng_; Tmc2209 &tmc_; Clock &clock_;
  bool uart_ok_ = false, enabled_ = false;
  // guards
  bool jam_armed_ = false; int64_t jam_pos0_ = 0, slip_off0_ = 0; int jam_raw0_ = 0;
  bool slip_map_ = true;                           // the slip baseline's reading came from the map
  uint32_t last_health_ = 0;
  const char *guard_ = "";
};

class Sensors : public app::ISensors {
 public:
  Sensors(As5600 &a, EncoderMap &m) : enc_(a), map_(m) {}
  bool   encoderDeg(double *deg) override;
  double batteryV() override;
  // map (incl. its short extrapolation), else the straight line; *from_map says which
  bool   encoderUnits(int raw, int64_t *units, bool *from_map = nullptr);
  int    raw() { return enc_.raw(); }
  bool   mapValid() const { return map_.valid(); }
 private:
  As5600 &enc_; EncoderMap &map_;
  uint32_t last_v_ms_ = 0; double v_ = 0;
};

class Gps : public app::IGps {
 public:
  void begin();
  void power(bool on) override;
  bool fix(int64_t *utc, double *lat, double *lon) override;
  void service();                                  // drain the UART
  const Nmea::Fix &nmea() const { return nmea_.fix(); }
  bool on() const { return on_; }
  void echo(uint32_t ms) { echo_until_ = millis() + ms; echo_ = ms > 0; }   // copy raw NMEA to the console
  uint32_t bytes() const { return bytes_; }
 private:
  Nmea nmea_;
  bool on_ = false;
  bool echo_ = false; uint32_t echo_until_ = 0, bytes_ = 0;
  uint32_t last_rmc_ms_ = 0; uint32_t seen_rmc_ = 0;
};

// the camera is src/EspCamera.h (slice 6)

class Store : public app::IStore {
 public:
  bool load(app::Persist *p) override;
  void save(const app::Persist &p) override;
  void report(const app::NightReport &r) override;
  void printReports(Print &out, int last_n);
  int  tail(int last_n, String *out);              // the last n report lines
  void (*on_completed)() = nullptr;                // e.g. promote the schedule to "good"
 private:
  app::Persist last_{};
  bool have_last_ = false;
};

class System : public app::ISystem {
 public:
  System(Ds3231 &rtc, Oled &oled, Clock &clock) : rtc_(rtc), oled_(oled), clock_(clock) {}
  void sleepUntil(int64_t wake_utc) override;
  void log(const char *msg) override;
  void radio(app::Radio r) override { if (on_radio) on_radio(r); }
  void (*on_radio)(app::Radio) = nullptr;          // src/Net.cpp
  void (*on_log)(int64_t utc, const char *msg) = nullptr;   // the web log ring
  // stay-awake bench mode: the loop calls this while "asleep"
  void serviceSleep();
  bool asleep() const { return asleep_; }
  int64_t wakeAt() const { return wake_; }
  static bool stayAwake();
  static void setStayAwake(bool on);
  const char *lastLog() const { return last_; }
 private:
  Ds3231 &rtc_; Oled &oled_; Clock &clock_;
  bool asleep_ = false; int64_t wake_ = 0;
  char last_[96] = "";
};

// The schedule on LittleFS. load() validates (layers 1-4) at every boot,
// falling back to schedule.good.json. upload() validates before it writes.
class ScheduleStore {
 public:
  explicit ScheduleStore(const app::ScheduleLimits &lim) : lim_(lim) {}
  bool load(int64_t now_utc, Print &log);          // false: no valid schedule
  bool upload(const char *json, size_t len, int64_t now_utc, app::Messages *err, app::Messages *warn);
  void markGood();                                 // after a night completes on it
  // what the FSM holds for its whole life: forwards to the schedule in use
  // NOW, so an upload or a delete takes effect without a restart
  app::ISchedule &schedule() { return live_; }
  bool valid() const { return valid_; }
  const char *source() const { return source_; }
  bool text(String *out);                          // the schedule file in use
  app::Schedule *check(const char *json, size_t len, int64_t now_utc, app::Messages *err, app::Messages *warn);
  app::Schedule *current() { return valid_ ? &sched_ : nullptr; }
  bool remove();                                   // delete both files: nothing planned
  uint32_t generation() const { return gen_; }     // bumps on every upload / delete
 private:
  struct Live : app::ISchedule {
    ScheduleStore *s;
    explicit Live(ScheduleStore *x) : s(x) {}
    app::NightPlan next(int64_t a) override { return s->valid_ ? s->sched_.next(a) : app::NightPlan(); }
    void setSite(double la, double lo) override { if (s->valid_) s->sched_.setSite(la, lo); }
    app::NightPlan refresh(const app::NightPlan &p) override { return s->valid_ ? s->sched_.refresh(p) : p; }
  } live_{this};
  app::ScheduleLimits lim_;
  static app::ScheduleDoc doc_;                    // ~6 KB: static, never on the stack
  static app::ScheduleDoc trial_;                  // validation scratch (upload, check)
  app::Schedule trial_sched_{trial_, lim_};
  app::Schedule sched_{doc_, lim_};
  bool valid_ = false;
  const char *source_ = "none";
  uint32_t gen_ = 0;
};

}  // namespace esp
