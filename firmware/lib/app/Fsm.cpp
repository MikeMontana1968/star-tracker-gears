#include "Fsm.h"
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

namespace app {

static const double SIDEREAL_DEG_PER_S = 360.0 / 86164.0905;

const char *stateName(State s) {
  static const char *n[] = {"BOOT", "HOME", "IDLE", "CONFIG_AP", "PREFLIGHT", "TIME_SYNC", "ORIENT",
                            "CAM_POWER", "CAM_WAKE", "CAM_CONFIG", "CAM_START", "WAIT_START", "TRACK",
                            "CAM_STOP", "SAFE_STOP", "REWIND", "FAULT"};
  return n[(int)s];
}

const char *faultName(Fault f) {
  static const char *n[] = {"none", "encoder at boot", "encoder lost", "divergence",
                            "orient failed", "rewind failed", "home failed"};
  return n[(int)f];
}

static bool activeState(State s) {
  switch (s) {
    case State::Preflight: case State::TimeSync: case State::Orient:
    case State::CamPower: case State::CamWake: case State::CamConfig: case State::CamStart:
    case State::WaitStart: case State::Track:
      return true;
    default:
      return false;
  }
}

Fsm::Fsm(Platform &p, const FsmConfig &c) : p_(p), c_(c) {}

// ---- plumbing --------------------------------------------------------------
void Fsm::log(const char *fmt, ...) {
  char buf[128];
  int n = snprintf(buf, sizeof buf, "[%s] ", stateName(state_));
  va_list ap; va_start(ap, fmt);
  vsnprintf(buf + n, sizeof buf - n, fmt, ap);
  va_end(ap);
  p_.sys.log(buf);
}

void Fsm::setReason(const char *why) {
  strncpy(night_.reason, why, sizeof night_.reason - 1);
  night_.reason[sizeof night_.reason - 1] = 0;
}

void Fsm::enter(State s, uint32_t deadline_ms) {
  state_ = s;
  entered_ms_ = p_.clock.ms();
  deadline_ms_ = deadline_ms;
  cam_started_ = false;
  setRadio(radioFor(s));
  persist();
}

// The one radio table (FIRMWARE.md s3: "the state table declares what each
// state requires"). AP and STA never together: the softAP would hop to the
// GoPro's channel and drop every phone.
Radio Fsm::radioFor(State s) const {
  if (s == State::ConfigAp) return Radio::Ap;
  bool cam_seq = s >= State::CamPower && s <= State::CamStart;
  bool cam_stop = (s == State::CamStop || s == State::SafeStop) && night_.camera == Op::Ok;
  if (cam_seq || cam_stop) return Radio::Sta;                     // talking to the GoPro
  bool winding_down = s == State::CamStop || s == State::SafeStop || s == State::Rewind;
  if (night_active_ && night_.plan.test && (activeState(s) || winding_down)) return Radio::Ap;  // bench: watch it
  if (c_.ap_during_track && (s == State::WaitStart || s == State::Track)) return Radio::Ap;
  return Radio::Off;
}

void Fsm::setRadio(Radio r) {
  if (r == radio_) return;
  radio_ = r;
  p_.sys.radio(r);
}

uint32_t Fsm::deadlineLeftMs() const {
  if (!deadline_ms_) return 0;
  uint64_t base = state_ == State::ConfigAp && touch_ms_ > entered_ms_ ? touch_ms_ : entered_ms_;
  uint64_t el = p_.clock.ms() - base;
  return el >= deadline_ms_ ? 1 : (uint32_t)(deadline_ms_ - el);
}

void Fsm::touch() {
  if (state_ == State::ConfigAp) touch_ms_ = p_.clock.ms();
}

void Fsm::persist() {
  Persist r;
  r.magic = Persist::MAGIC;
  r.state = (uint8_t)state_;
  r.fault = (uint8_t)fault_;
  r.night_active = night_active_;
  r.night = night_;
  r.pos_deg = p_.motion.positionDeg();
  r.last_done_stop_utc = last_done_stop_utc_;
  r.boot_count = boot_count_;
  p_.store.save(r);
}

uint32_t Fsm::inState() const { return (uint32_t)(p_.clock.ms() - entered_ms_); }
bool Fsm::timedOut() const {
  if (!deadline_ms_) return false;
  uint64_t base = state_ == State::ConfigAp && touch_ms_ > entered_ms_ ? touch_ms_ : entered_ms_;
  return p_.clock.ms() - base >= deadline_ms_;
}

void Fsm::sleep(int64_t wake_utc) {
  p_.motion.stop();
  p_.motion.enable(false);
  p_.camera.power(false);
  p_.gps.power(false);
  setRadio(Radio::Off);
  persist();
  asleep_ = true;
  log("sleep, wake %s %lld", wake_utc ? "at" : "on button only", (long long)wake_utc);
  p_.sys.sleepUntil(wake_utc);
}

double Fsm::expectedDeg(int64_t utc) const {
  double dt = (double)(utc - night_.plan.start_utc);
  return night_.plan.start_deg + (dt > 0 ? dt : 0) * SIDEREAL_DEG_PER_S;
}

void Fsm::clearFault() {
  fault_ = Fault::None;
  persist();
}

// ---- boot ----------------------------------------------------------------
void Fsm::begin(Wake why) {
  Persist r;
  bool have = p_.store.load(&r) && r.magic == Persist::MAGIC;
  if (have) {
    fault_ = (Fault)r.fault;
    night_active_ = r.night_active;
    night_ = r.night;
    last_done_stop_utc_ = r.last_done_stop_utc;
    boot_count_ = r.boot_count;
  }
  boot_count_++;
  State prior = have ? (State)r.state : State::Boot;
  state_ = State::Boot;
  entered_ms_ = p_.clock.ms();

  double enc;
  if (!p_.sensors.encoderDeg(&enc)) { toFault(Fault::EncoderAtBoot, "encoder not answering at boot"); return; }
  p_.motion.setPosition(enc);
  if (have && !activeState(prior) && prior != State::Fault && fabs(enc - r.pos_deg) > c_.div_warn_deg) {
    // the single most informative diagnostic this machine has (FIRMWARE.md s3)
    log("position moved while asleep: saved %.3f, encoder %.3f deg", r.pos_deg, enc);
    if (night_active_) night_.warnings++;
  }

  // the button opens CONFIG_AP -- except mid-night, where the night resumes
  // (TRACK brings the AP up itself when ap_during_track)
  bool mid_night = night_active_ && (activeState(prior) || prior == State::CamStop ||
                                     prior == State::SafeStop || prior == State::Rewind);
  if (why == Wake::Button && !mid_night) { enter(State::ConfigAp, c_.config_ap_ms); return; }
  if (fault_ != Fault::None) { toFault(fault_, "fault persists across boot"); return; }

  int64_t now = p_.clock.utc();
  if (night_active_ && activeState(prior)) {
    night_.resumes++;
    if (now >= night_.plan.stop_utc) {
      log("rebooted after the night ended: stop the camera and rewind");
      enter(State::CamStop, c_.cam_stop_ms);
      return;
    }
    resumed_ = true;
    log("resuming mid-night (was %s), boot %u", stateName(prior), boot_count_);
    if (prior == State::Preflight || prior == State::TimeSync) { enter(State::Preflight, c_.preflight_ms); return; }
    p_.motion.enable(true);
    if (!p_.motion.moveTo(expectedDeg(now + 60))) { toFault(Fault::OrientFailed, "resume move refused"); return; }
    enter(State::Orient, c_.orient_ms);
    return;
  }
  if (night_active_ && (prior == State::CamStop || prior == State::SafeStop || prior == State::Rewind)) {
    log("rebooted during %s: finish the rewind", stateName(prior));
    p_.motion.enable(true);
    p_.motion.moveTo(c_.park_deg);
    enter(State::Rewind, c_.rewind_ms);
    return;
  }
  enter(State::Home, c_.home_ms);
}

// ---- night bookkeeping ------------------------------------------------------
void Fsm::startNight(const NightPlan &plan) {
  night_ = NightReport();
  night_.plan = plan;
  night_active_ = true;
  resumed_ = false;
  pending_fault_ = Fault::None;
  log("night %lld..%lld from %.2f deg", (long long)plan.start_utc, (long long)plan.stop_utc, plan.start_deg);
  enter(State::Preflight, c_.preflight_ms);
}

void Fsm::skipNight(const char *why) {
  night_.outcome = Outcome::Skipped;
  setReason(why);
  log("skip night: %s", why);
  if (fabs(p_.motion.positionDeg() - c_.park_deg) > c_.home_tol_deg) {   // moved already: park first
    p_.motion.enable(true);
    p_.motion.moveTo(c_.park_deg);
    enter(State::Rewind, c_.rewind_ms);
    return;
  }
  finishNight();
}

void Fsm::finishNight() {
  p_.store.report(night_);
  bool test = night_.plan.test;
  if (!test) last_done_stop_utc_ = night_.plan.stop_utc;   // a test never uses up a real night
  night_active_ = false;
  if (test) { touch_ms_ = 0; enter(State::ConfigAp, c_.config_ap_ms); }   // back to the human
  else enter(State::Idle);
}

void Fsm::toFault(Fault f, const char *why) {
  p_.motion.stop();
  fault_ = f;
  log("FAULT: %s", why);
  if (night_active_) {
    night_.outcome = Outcome::Faulted;
    setReason(why);
    p_.store.report(night_);
    if (!night_.plan.test) last_done_stop_utc_ = night_.plan.stop_utc;
    night_active_ = false;
  }
  enter(State::Fault);
}

// Halt: the motor stops now, whatever it was doing; a night in progress is
// written up as stopped and the FSM waits in CONFIG_AP for the human.
void Fsm::endNightNow(const char *why) {
  p_.motion.stop();
  p_.camera.cancel();
  p_.gps.power(false);
  log("HALT: %s", why);
  if (night_active_) {
    night_.outcome = Outcome::SafeStopped;
    if (!night_.track_stop_utc && night_.track_start_utc) night_.track_stop_utc = p_.clock.utc();
    setReason(why);
    p_.store.report(night_);
    if (!night_.plan.test) last_done_stop_utc_ = night_.plan.stop_utc;
    night_active_ = false;
  }
  p_.camera.power(false);
  touch_ms_ = 0;
  move_pending_ = false;
  enter(State::ConfigAp, c_.config_ap_ms);
}

CmdResult Fsm::command(Cmd c, double arg) {
  if (asleep_) return {false, "asleep"};
  bool cfg = state_ == State::ConfigAp;
  switch (c) {
    case Cmd::Halt:
      endNightNow("halted from the web UI");
      return {true, "halted"};

    case Cmd::EndNight:
      if (!night_active_ || !activeState(state_)) return {false, "no night in progress"};
      p_.gps.power(false);
      if (state_ >= State::CamPower && state_ <= State::CamStart) { p_.camera.cancel(); p_.camera.power(false); }
      safeStop("ended from the web UI");
      return {true, "ending the night: rewinding"};

    case Cmd::Goto: case Cmd::Jog: case Cmd::Home: {
      if (!cfg) return {false, "only in CONFIG_AP"};
      if (p_.motion.moving()) return {false, "already moving"};
      double to = c == Cmd::Home ? c_.park_deg : c == Cmd::Goto ? arg : p_.motion.positionDeg() + arg;
      if (!(to >= c_.travel_min_deg && to <= c_.travel_max_deg)) return {false, "outside the travel envelope"};
      p_.motion.enable(true);
      if (!p_.motion.moveTo(to)) return {false, "move refused"};
      move_pending_ = true; move_failed_ = false;
      touch();
      log("manual move to %.3f deg", to);
      return {true, "moving"};
    }

    case Cmd::ClearFault:
      if (!cfg) return {false, "only in CONFIG_AP"};
      clearFault();
      log("fault cleared from the web UI");
      return {true, "fault cleared"};

    case Cmd::TestTrack: {
      if (!cfg) return {false, "only in CONFIG_AP"};
      if (fault_ != Fault::None) return {false, "clear the fault first"};
      if (p_.motion.moving()) return {false, "still moving"};
      if (!(arg >= 1 && arg <= 12 * 60)) return {false, "minutes must be 1..720"};
      int64_t now = p_.clock.utc();
      NightPlan pl;
      pl.valid = true; pl.test = true;
      pl.start_utc = now + 20;
      pl.stop_utc = pl.start_utc + (int64_t)(arg * 60);
      pl.start_deg = c_.park_deg;
      pl.cam = CamPolicy::None;         // s3: straight repeatability of the drive
      pl.gps = GpsPolicy::None;
      strncpy(pl.camera, "test_track", sizeof pl.camera - 1);
      move_pending_ = false;
      startNight(pl);
      return {true, "test track starting"};
    }

    case Cmd::SleepNow:
      if (!cfg) return {false, "only in CONFIG_AP"};
      if (p_.motion.moving()) return {false, "still moving"};
      if (fault_ != Fault::None) { toFault(fault_, "fault still set after config"); return {true, "fault: sleeping until the button"}; }
      enter(State::Home, c_.home_ms);
      return {true, "leaving config: home, then sleep"};
  }
  return {false, "unknown command"};
}

// CONFIG_AP: the human is in charge, but a scheduled night still wins.
void Fsm::doConfig() {
  if (move_pending_ && !p_.motion.moving()) {
    move_pending_ = false;
    move_failed_ = !p_.motion.arrived();
    log(move_failed_ ? "manual move stopped short (guard: look before moving again)" : "manual move arrived, %.3f deg",
        p_.motion.positionDeg());
  }
  uint64_t ms = p_.clock.ms();
  if (fault_ == Fault::None && !p_.motion.moving() && ms - last_due_ms_ >= 10000) {
    last_due_ms_ = ms;
    int64_t now = p_.clock.utc();
    int64_t after = now > last_done_stop_utc_ ? now : last_done_stop_utc_;
    NightPlan plan = p_.schedule.next(after);
    if (plan.valid && plan.start_utc - c_.lead_s <= now) {
      log("a scheduled night is due: leaving config");
      enter(State::Home, c_.home_ms);
      return;
    }
  }
  if (timedOut() && !p_.motion.moving()) {
    if (fault_ != Fault::None) toFault(fault_, "fault still set after config");
    else enter(State::Home, c_.home_ms);
  }
}

void Fsm::safeStop(const char *why, Fault f) {
  p_.motion.stop();                                  // the motor FIRST
  night_.track_stop_utc = p_.clock.utc();
  night_.outcome = f == Fault::None ? Outcome::SafeStopped : Outcome::Faulted;
  setReason(why);
  pending_fault_ = f;
  log("SAFE_STOP: %s", why);
  enter(State::SafeStop, night_.camera == Op::Ok ? c_.safe_cam_ms : 1);
}

void Fsm::camFail(const char *why) {
  p_.camera.cancel();
  p_.camera.power(false);
  night_.camera = Op::Fail;
  night_.warnings++;
  log("camera: %s", why);
  if (night_.plan.cam == CamPolicy::Required) { skipNight("camera required, and it failed"); return; }
  enter(State::WaitStart);
}

void Fsm::afterOrient() {
  bool cam_done = night_.camera != Op::Pending;           // a resume keeps what it had
  if (night_.plan.cam == CamPolicy::None || cam_done) { enter(State::WaitStart); return; }
  cam_seq_ms_ = p_.clock.ms();
  p_.camera.power(true);
  enter(State::CamPower);
}

// ---- states ------------------------------------------------------------------
void Fsm::doIdle() {
  p_.motion.enable(false);
  int64_t now = p_.clock.utc();
  int64_t after = now > last_done_stop_utc_ ? now : last_done_stop_utc_;
  NightPlan plan = p_.schedule.next(after);
  if (!plan.valid) { log("nothing scheduled"); sleep(now + c_.idle_recheck_s); return; }
  int64_t wake = plan.start_utc - c_.lead_s;
  if (wake > now + 5) { sleep(wake); return; }
  startNight(plan);
}

void Fsm::doTrack() {
  uint64_t ms = p_.clock.ms();
  if (ms - last_check_ms_ < 1000) return;
  last_check_ms_ = ms;
  int64_t now = p_.clock.utc();

  if (now >= night_.plan.stop_utc) {
    p_.motion.stop();
    night_.track_stop_utc = now;
    log("stop time reached, %.3f deg", p_.motion.positionDeg());
    enter(State::CamStop, night_.camera == Op::Ok ? c_.cam_stop_ms : 1);
    return;
  }
  if (p_.motion.limitHit()) { safeStop("travel envelope reached"); return; }
  if (!p_.motion.tracking()) { safeStop("motion stopped (jam/slip guard)", Fault::Divergence); return; }

  double enc;
  if (!p_.sensors.encoderDeg(&enc)) {
    if (++enc_fail_ >= c_.enc_fail_samples) { safeStop("encoder lost", Fault::EncoderLost); return; }
  } else {
    enc_fail_ = 0;
    double diff = enc - p_.motion.positionDeg();
    if (!div_armed_) { div_offset_ = diff; div_armed_ = true; }
    double div = fabs(diff - div_offset_);
    if (div > night_.max_div_deg) night_.max_div_deg = div;
    if (div > c_.div_fault_deg) { safeStop("encoder divergence past 2 deg", Fault::Divergence); return; }
    if (div > c_.div_warn_deg && !div_warned_) { div_warned_ = true; night_.warnings++; log("divergence %.2f deg", div); }
  }

  double v = p_.sensors.batteryV();
  if (v < night_.min_battery_v) night_.min_battery_v = v;
  if (v < c_.batt_min_run_v) {
    if (++batt_low_ >= c_.batt_low_samples) { safeStop("battery low"); return; }
  } else {
    batt_low_ = 0;
  }
}

void Fsm::tick() {
  if (asleep_) return;
  int64_t now = p_.clock.utc();
  switch (state_) {
    case State::Boot:
      break;

    case State::Home: {
      if (!cam_started_) {                                 // first tick in HOME: decide
        cam_started_ = true;
        if (fabs(p_.motion.positionDeg() - c_.park_deg) <= c_.home_tol_deg) { enter(State::Idle); break; }
        p_.motion.enable(true);
        if (!p_.motion.moveTo(c_.park_deg)) { toFault(Fault::HomeFailed, "home move refused"); break; }
        break;
      }
      if (!p_.motion.moving()) {
        if (p_.motion.arrived()) enter(State::Idle);
        else toFault(Fault::HomeFailed, "home move stopped short");
      } else if (timedOut()) {
        toFault(Fault::HomeFailed, "home timed out");
      }
      break;
    }

    case State::Idle:
      doIdle();
      break;

    case State::ConfigAp:
      doConfig();
      break;

    case State::Preflight: {
      const NightPlan &pl = night_.plan;
      double end_deg = pl.start_deg + (double)(pl.stop_utc - pl.start_utc) * SIDEREAL_DEG_PER_S;
      double enc, v = p_.sensors.batteryV();
      night_.min_battery_v = v;
      if (pl.stop_utc <= pl.start_utc) { skipNight("stop not after start"); break; }
      if (pl.stop_utc - pl.start_utc > (int64_t)(c_.max_night_h * 3600)) { skipNight("night longer than 16 h"); break; }
      if (now >= pl.stop_utc) { skipNight("woke after the night ended"); break; }
      if (pl.start_deg < c_.travel_min_deg || end_deg > c_.travel_max_deg) {
        skipNight("night would leave the travel envelope"); break;
      }
      if (v < c_.batt_min_start_v) { skipNight("battery too low to start"); break; }
      if (!p_.sensors.encoderDeg(&enc)) { toFault(Fault::EncoderLost, "encoder not answering at preflight"); break; }
      if (pl.gps == GpsPolicy::None) {
        if (p_.clock.lostPower()) { skipNight("RTC lost power and GPS is off"); break; }
        night_.gps = Op::Pending;
        p_.motion.enable(true);
        if (!p_.motion.moveTo(now < pl.start_utc ? pl.start_deg : expectedDeg(now + 30))) {
          toFault(Fault::OrientFailed, "orient move refused"); break;
        }
        enter(State::Orient, c_.orient_ms);
        break;
      }
      p_.gps.power(true);
      enter(State::TimeSync, c_.sync_ms);
      break;
    }

    case State::TimeSync: {
      int64_t gps_utc; double lat, lon;
      bool got = p_.gps.fix(&gps_utc, &lat, &lon);
      if (got) {
        night_.rtc_drift_s = p_.clock.utc() - gps_utc;
        p_.clock.setUtc(gps_utc);
        night_.gps = Op::Ok;
        log("GPS fix after %u ms, RTC was %+lld s off", inState(), (long long)night_.rtc_drift_s);
        // move it and the times follow (FIRMWARE.md s3): tonight from the fix
        p_.schedule.setSite(lat, lon);
        NightPlan np = p_.schedule.refresh(night_.plan);
        double end = np.start_deg + (double)(np.stop_utc - np.start_utc) * SIDEREAL_DEG_PER_S;
        if (np.valid && np.stop_utc > np.start_utc && np.start_deg >= c_.travel_min_deg && end <= c_.travel_max_deg) {
          if (np.start_utc != night_.plan.start_utc || np.stop_utc != night_.plan.stop_utc)
            log("site from GPS: night now %lld..%lld", (long long)np.start_utc, (long long)np.stop_utc);
          night_.plan = np;
        }
      } else if (timedOut()) {
        night_.gps = Op::Fail;
        night_.warnings++;
        log("no GPS fix in %u s: keeping the RTC", c_.sync_ms / 1000);
      } else {
        break;
      }
      p_.gps.power(false);
      if (p_.clock.lostPower()) { skipNight("RTC lost power and no GPS fix"); break; }
      now = p_.clock.utc();
      p_.motion.enable(true);
      if (!p_.motion.moveTo(now < night_.plan.start_utc ? night_.plan.start_deg : expectedDeg(now + 30))) {
        toFault(Fault::OrientFailed, "orient move refused"); break;
      }
      enter(State::Orient, c_.orient_ms);
      break;
    }

    case State::Orient:
      if (!p_.motion.moving()) {
        if (p_.motion.arrived()) afterOrient();
        else toFault(Fault::OrientFailed, "orient move stopped short (jam/slip guard)");
      } else if (timedOut()) {
        toFault(Fault::OrientFailed, "orient timed out");
      }
      break;

    case State::CamPower: case State::CamWake: case State::CamConfig: case State::CamStart: {
      if (p_.clock.ms() - cam_seq_ms_ >= c_.cam_total_ms) { camFail("sequence timed out"); break; }
      if (state_ == State::CamPower) {
        if (inState() >= c_.cam_power_ms) enter(State::CamWake);
        break;
      }
      CamStep step = state_ == State::CamWake ? CamStep::Wake : state_ == State::CamConfig ? CamStep::Config : CamStep::Start;
      if (!cam_started_) { p_.camera.begin(step); cam_started_ = true; break; }
      Op r = p_.camera.poll();
      if (r == Op::Pending) break;
      if (r == Op::Fail) {
        camFail(step == CamStep::Wake ? "wake failed" : step == CamStep::Config ? "config failed" : "start failed");
        break;
      }
      if (state_ == State::CamWake) enter(State::CamConfig);
      else if (state_ == State::CamConfig) enter(State::CamStart);
      else { night_.camera = Op::Ok; log("camera recording"); enter(State::WaitStart); }
      break;
    }

    case State::WaitStart:
      if (now >= night_.plan.start_utc) {
        if (!p_.motion.startTracking(night_.plan.start_deg, night_.plan.start_utc)) {
          safeStop("tracking refused"); break;
        }
        if (!night_.track_start_utc) night_.track_start_utc = now;
        div_armed_ = false; div_warned_ = false; batt_low_ = 0; enc_fail_ = 0;
        last_check_ms_ = p_.clock.ms();
        log("tracking from %.3f deg%s", p_.motion.positionDeg(), resumed_ ? " (resumed)" : "");
        enter(State::Track);
      }
      break;

    case State::Track:
      doTrack();
      break;

    case State::CamStop: case State::SafeStop: {
      bool safe = state_ == State::SafeStop;
      if (night_.camera == Op::Ok) {
        if (!cam_started_) { p_.camera.begin(CamStep::Stop); cam_started_ = true; break; }
        Op r = p_.camera.poll();
        if (r == Op::Pending && !timedOut()) break;
        if (r != Op::Ok) { night_.warnings++; log("camera stop %s", r == Op::Fail ? "failed" : "timed out"); p_.camera.cancel(); }
      }
      p_.camera.power(false);
      if (safe && pending_fault_ != Fault::None) { toFault(pending_fault_, night_.reason); break; }
      p_.motion.enable(true);
      if (!p_.motion.moveTo(c_.park_deg)) { toFault(Fault::RewindFailed, "rewind refused"); break; }
      enter(State::Rewind, c_.rewind_ms);
      break;
    }

    case State::Rewind:
      if (!p_.motion.moving()) {
        if (p_.motion.arrived()) { log("parked"); finishNight(); }
        else toFault(Fault::RewindFailed, "rewind stopped short (jam/slip guard)");
      } else if (timedOut()) {
        toFault(Fault::RewindFailed, "rewind timed out");
      }
      break;

    case State::Fault:
      p_.motion.enable(false);
      sleep(0);                                    // button only: a human must look
      break;
  }
}

}  // namespace app
