// =====================================================================
//  Fsm -- the night state machine (FIRMWARE.md s3). Pure C++ over
//  app::Platform; host-tested in test/test_fsm_sim.
//
//    BOOT -> HOME -> IDLE (sleep until start - lead)
//    IDLE -> PREFLIGHT -> TIME_SYNC -> ORIENT -> CAM_* -> WAIT_START
//         -> TRACK -> CAM_STOP -> REWIND -> report -> IDLE
//    TRACK -- battery / limit --> SAFE_STOP -> REWIND
//    TRACK -- divergence / jam --> SAFE_STOP -> FAULT (no blind rewind)
//    button wake -> CONFIG_AP (radio lands in slice 5) -> HOME
//
//  Rules (s3): every state has a deadline that leads somewhere safe and is
//  checked here, never inside a call; SAFE_STOP stops the motor before it
//  touches the camera; FAULT only for damage or impossibility -- camera
//  trouble, low battery and a bad night are skipped nights; persist on
//  transition, not on tick. The camera is best-effort unless the night
//  says `required` (D2).
// =====================================================================
#pragma once
#include <stdint.h>
#include "Platform.h"

namespace app {

enum class State : uint8_t {
  Boot, Home, Idle, ConfigAp, Preflight, TimeSync, Orient,
  CamPower, CamWake, CamConfig, CamStart, WaitStart, Track,
  CamStop, SafeStop, Rewind, Fault
};

enum class Fault : uint8_t {
  None, EncoderAtBoot, EncoderLost, Divergence, OrientFailed, RewindFailed, HomeFailed
};

const char *stateName(State s);
const char *faultName(Fault f);

struct FsmConfig {
  double   park_deg = 0;             // where the output rests by day (start position)
  double   travel_min_deg = -2, travel_max_deg = 182;
  int64_t  lead_s = 600;             // wake this long before the night's start
  double   max_night_h = 16;

  uint32_t preflight_ms = 10000;
  uint32_t sync_ms = 120000;         // GPS fix
  uint32_t orient_ms = 6 * 60000;
  uint32_t cam_power_ms = 3000;      // rail settle before waking the camera
  uint32_t cam_total_ms = 150000;    // the whole CAM_POWER..CAM_START sequence
  uint32_t cam_stop_ms = 30000;
  uint32_t safe_cam_ms = 15000;      // SAFE_STOP's camera stop: short, it may be what hung
  uint32_t rewind_ms = 12 * 60000;   // a 180 deg rewind measured 9.7 min
  uint32_t home_ms = 12 * 60000;
  uint32_t config_ap_ms = 10 * 60000;

  double   batt_min_start_v = 11.4;  // PREFLIGHT: skip the night below this
  double   batt_min_run_v = 10.8;    // TRACK: SAFE_STOP below this...
  int      batt_low_samples = 5;     // ...for this many 1 Hz samples in a row
  double   div_warn_deg = 0.5;       // FIRMWARE.md s3 thresholds
  double   div_fault_deg = 2.0;
  int      enc_fail_samples = 5;
  double   home_tol_deg = 0.25;
  int64_t  idle_recheck_s = 6 * 3600;  // nothing scheduled: look again later
};

class Fsm {
 public:
  Fsm(Platform &p, const FsmConfig &c = FsmConfig());
  void begin(Wake why);     // after every (re)boot, including deep-sleep wakes
  void tick();              // as often as convenient; checks run at <= 1 Hz where it matters

  State state() const { return state_; }
  Fault fault() const { return fault_; }
  bool  asleep() const { return asleep_; }
  bool  nightActive() const { return night_active_; }
  const NightReport &night() const { return night_; }
  void  clearFault();       // the config UI's "I fixed it" (slice 5)

 private:
  void enter(State s, uint32_t deadline_ms = 0);
  void persist();
  void sleep(int64_t wake_utc);
  void log(const char *fmt, ...);
  bool timedOut() const;
  uint32_t inState() const;

  void startNight(const NightPlan &plan);
  void skipNight(const char *why);
  void finishNight();
  void safeStop(const char *why, Fault f = Fault::None);
  void camFail(const char *why);
  void toFault(Fault f, const char *why);
  void afterOrient();
  double expectedDeg(int64_t utc) const;
  void setReason(const char *why);

  void doIdle();
  void doTrack();

  Platform &p_;
  FsmConfig c_;
  State   state_ = State::Boot;
  Fault   fault_ = Fault::None;
  uint64_t entered_ms_ = 0, cam_seq_ms_ = 0, last_check_ms_ = 0;
  uint32_t deadline_ms_ = 0;
  bool    asleep_ = false;
  bool    resumed_ = false;
  bool    cam_started_ = false;   // this state's one-shot action has run (camera step, home move)
  bool    night_active_ = false;
  NightReport night_;
  int64_t last_done_stop_utc_ = 0;
  uint32_t boot_count_ = 0;

  // TRACK monitors
  double  div_offset_ = 0;
  bool    div_armed_ = false, div_warned_ = false;
  int     batt_low_ = 0, enc_fail_ = 0;
  Fault   pending_fault_ = Fault::None;   // SAFE_STOP -> FAULT instead of REWIND
};

}  // namespace app
