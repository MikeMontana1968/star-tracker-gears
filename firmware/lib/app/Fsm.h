// =====================================================================
//  Fsm -- the night state machine (FIRMWARE.md s3). Pure C++ over
//  app::Platform; host-tested in test/test_fsm_sim.
//
//    BOOT -> HOME -> IDLE (sleep until start - lead)
//    IDLE -> PREFLIGHT -> TIME_SYNC -> ORIENT -> CAM_* -> WAIT_START
//         -> TRACK -> CAM_STOP -> REWIND -> report -> IDLE
//    TRACK -- battery / limit --> SAFE_STOP -> REWIND
//    TRACK -- divergence / jam --> SAFE_STOP -> FAULT (no blind rewind)
//    button wake -> CONFIG_AP (softAP + web UI, s7) -> HOME
//
//  Radio mode is a state attribute (s3): radioFor() is the one table, and
//  enter() is the one caller of ISystem::radio(). CONFIG_AP and a bench
//  test_track run the AP; WAIT_START/TRACK run it when ap_during_track (D8);
//  the camera states want STA (slice 6). The web UI drives the FSM only
//  through command() and touch().
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
#include <math.h>
#include <string.h>
#include "Platform.h"

namespace app {

enum class State : uint8_t {
  Boot, Home, Idle, ConfigAp, Preflight, TimeSync, Orient,
  CamPower, CamWake, CamConfig, CamStart, WaitStart, Track,
  CamStop, SafeStop, Rewind, Fault
};

// what the web UI can ask for (FIRMWARE.md s7: POST /api/command)
enum class Cmd : uint8_t {
  Halt,        // any state: motor off NOW, night ended, CONFIG_AP
  EndNight,    // a night in progress: SAFE_STOP -> camera stop -> rewind
  Goto, Jog,   // CONFIG_AP: move to / by arg deg (inside the envelope)
  Home,        // CONFIG_AP: move to the park position
  ClearFault,  // CONFIG_AP: the human says it is fixed
  TestTrack,   // CONFIG_AP: track arg minutes from park, no camera/GPS, AP up throughout
  SleepNow,    // CONFIG_AP: leave now (HOME -> IDLE -> sleep)
  TestTrackCamera, // CONFIG_AP: as TestTrack, but the camera runs (best_effort): wake, config, record, stop
  Discover,    // CONFIG_AP: join the camera, dump its settings tree (the AP drops meanwhile)
  Coils        // CONFIG_AP: arg 1 = driver enabled (holding), 0 = released
};
struct CmdResult { bool ok; const char *msg; };

// CONFIG_AP motor diagnostics: a list of moves, the encoder read after each
// (settled), and the numbers that come out of it. Waypoints are
//   base, then (base + out, base) x cycles
// Wiggle: base = here, out = +-2, 1 cycle.  Sweep: base = park, 1 cycle.
// Repeat (FIRMWARE.md s3 repeat_test): base = park, N cycles -> the return
// error at park in arcsec (mean, sigma, max) -- the number that sets
// BACKLASH_UNITS. Errors are relative to the first reading at base, so the
// encoder's absolute offset (lash state at boot) drops out.
enum class DiagKind : uint8_t { None, Wiggle, Sweep, Repeat };
struct DiagState {
  static constexpr int MAX = 20;
  DiagKind kind = DiagKind::None;
  bool   running = false, ok = false;
  double base = 0, out = 0;
  int    cycles = 0, wp = 0;           // wp: -1 = priming (base - 1, then forward to base), 0 = base reading
  double overshoot = 0, saved_overshoot = 0;   // the overshoot used / the one to restore
  bool   override_os = false;
  uint32_t slew_hz = 0, saved_slew_hz = 0;      // 0: not overridden
  double ref_enc = 0;                  // encoder at base before the first cycle
  int    n_ret = 0, n_out = 0;
  double ret_err_deg[MAX] = {};        // encoder at base minus ref, per cycle
  double out_err_deg[MAX] = {};        // (encoder out - ref) - out, per cycle
  double ret_mean_as = 0, ret_sd_as = 0, ret_max_as = 0, out_mean_deg = 0;
  char   msg[64] = {};
};
const char *diagName(DiagKind k);

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
  uint32_t orient_ms = 20 * 60000;   // a resume may slew most of the travel: 180 deg at 2000/s = 15.5 min
  uint32_t cam_power_ms = 3000;      // rail settle before waking the camera
  uint32_t cam_total_ms = 150000;    // the whole CAM_POWER..CAM_START sequence
  uint32_t cam_stop_ms = 90000;      // rejoin (20 s) + the last exposure (up to 30 s) + margin
  uint32_t safe_cam_ms = 60000;      // SAFE_STOP's camera stop: shorter, but it must outlast a 30 s exposure
  uint32_t rewind_ms = 20 * 60000;   // 180 deg: 9.7 min at 3200/s, ~15.5 at 2000/s
  uint32_t home_ms = 20 * 60000;
  uint32_t config_ap_ms = 10 * 60000;   // CONFIG_AP with no client activity
  bool     ap_during_track = true;      // D8: on while debugging (~1.2 Wh a night)

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

  // the camera preset for the next TestTrackCamera ("" = the shared settings only)
  void testPreset(const char *name) { strncpy(test_preset_, name ? name : "", sizeof test_preset_ - 1); test_preset_[sizeof test_preset_ - 1] = 0; }
  State state() const { return state_; }
  Fault fault() const { return fault_; }
  bool  asleep() const { return asleep_; }
  bool  nightActive() const { return night_active_; }
  const NightReport &night() const { return night_; }
  void  clearFault();       // the config UI's "I fixed it"
  CmdResult command(Cmd c, double arg = 0);
  // overshoot_deg: the forward-arrival overshoot for this run only (NaN: keep).
  // 0 makes every return a plain reverse arrival, so the return error IS the lash.
  // slew_hz: top slew speed for this run only (0: keep).
  CmdResult diag(DiagKind k, double deg = 0, int cycles = 1, double overshoot_deg = NAN, uint32_t slew_hz = 0);
  const DiagState &diagState() const { return diag_; }
  bool  coils() const { return coils_; }
  // camera discover: 0 none yet, 1 running, 2 ok, 3 failed
  uint8_t discoverState() const { return discover_; }
  void  touch();            // a web client is looking: CONFIG_AP's timeout restarts
  Radio radio() const { return radio_; }
  uint32_t stateMs() const { return inState(); }
  uint32_t deadlineLeftMs() const;   // 0: no deadline
  bool  moveFailed() const { return move_failed_; }   // CONFIG_AP: the last manual move stopped short

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

  Radio radioFor(State s) const;
  void setRadio(Radio r);
  void endNightNow(const char *why);
  void doIdle();
  void doConfig();
  void doDiag();
  void diagEnd(bool ok, const char *msg);
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
  Radio   radio_ = Radio::Off;
  uint64_t touch_ms_ = 0, last_due_ms_ = 0;
  bool    move_pending_ = false, move_failed_ = false;
  bool    coils_ = false;
  uint8_t discover_ = 0;
  DiagState diag_;
  uint64_t diag_settle_ms_ = 0;

  // TRACK monitors
  double  div_offset_ = 0;
  bool    div_armed_ = false, div_warned_ = false;
  int     batt_low_ = 0, enc_fail_ = 0;
  char    test_preset_[24] = "";
  Fault   pending_fault_ = Fault::None;   // SAFE_STOP -> FAULT instead of REWIND
};

}  // namespace app
