// =====================================================================
//  Sim -- a fake world for the night FSM (FIRMWARE.md s8: "the simulator
//  is the thing to push hardest on"). One object fakes every Platform
//  interface over a simulated true time; Sim::run() advances it in 1 s
//  steps, and turns every sleepUntil() into a real reboot: the FSM object
//  is destroyed and rebuilt from the persisted record, as on the ESP32.
//
//  The physical model keeps two angles apart on purpose:
//    step_deg -- the step counter (what the firmware believes)
//    true_deg -- the output shaft (what the encoder sees)
//  A jam freezes true_deg while step_deg keeps going.
// =====================================================================
#pragma once
#include <memory>
#include <string>
#include <vector>
#include <stdio.h>
#include <math.h>
#include "Fsm.h"

using namespace app;

static const double SID_DEG_S = 360.0 / 86164.0905;

struct World : IClock, IMotion, ISensors, IGps, IStore, ISchedule, ISystem {
  // ---- time: true UTC in ms; the RTC may be off by rtc_offset_s ------------
  int64_t t_ms = 0;              // true time
  int64_t boot_ms = 0;           // true time of the last boot
  int64_t rtc_offset_s = 0;      // RTC minus true
  bool    rtc_lost = false;

  int64_t  trueUtc() const { return t_ms / 1000; }
  int64_t  utc() override { return trueUtc() + rtc_offset_s; }
  uint64_t ms() override { return (uint64_t)(t_ms - boot_ms); }
  bool     setUtc(int64_t t) override { rtc_offset_s = t - trueUtc(); rtc_lost = false; return true; }
  bool     lostPower() override { return rtc_lost; }

  // ---- motion -------------------------------------------------------------
  enum class M { Idle, Slew, Track } mode = M::Idle;
  double  step_deg = 0, true_deg = 0;
  double  slew_target = 0;
  double  track_origin = 0; int64_t track_origin_true_ms = 0;
  double  track_dps = SID_DEG_S; TrackRate last_rate = TrackRate::Sidereal;
  bool    arrived_ = false, limit_ = false, enabled = false;
  double  slew_dps = 0.3516;     // 3200 pulses/s at 1/8
  double  catchup_dps = 0.044;   // 1600 pulses/s at 1/32
  double  min_deg = -2, max_deg = 182;
  int64_t jam_at_ms = -1;        // from this true time the output stops following
  double  max_true_deg_seen = -1e9;
  double  in_deg = 0;             // the gear train's input side (steps, minus any jam)
  double  lash_deg = 0;           // backlash at the output
  double  overshoot_deg = 0;      // the engine's forward-arrival overshoot (0: plain slews)
  double  final_target = 0; bool has_final = false;
  void   setOvershootDeg(double d) override { overshoot_deg = d; }
  double overshootDeg() override { return overshoot_deg; }
  uint32_t slew_hz = 3200;
  void     setSlewHz(uint32_t hz) override { slew_dps = 0.3516 * hz / 3200.0; slew_hz = hz; }
  uint32_t slewHz() override { return slew_hz; }

  bool setPosition(double d) override { if (mode != M::Idle) return false; step_deg = d; return true; }
  bool moveTo(double d) override {
    if (mode != M::Idle || d < min_deg || d > max_deg) return false;
    mode = M::Slew; arrived_ = false; moves++;
    if (d < step_deg && overshoot_deg > 0) { slew_target = d - overshoot_deg; final_target = d; has_final = true; }
    else { slew_target = d; has_final = false; }
    return true;
  }
  bool startTracking(double origin, int64_t origin_utc, TrackRate r = TrackRate::Sidereal) override {
    if (mode != M::Idle) return false;
    mode = M::Track; track_origin = origin; track_dps = rateDegPerS(r); last_rate = r;
    track_origin_true_ms = t_ms - (utc() - origin_utc) * 1000;   // device clock -> true time
    limit_ = false; tracks++; return true;
  }
  void   stop() override { if (mode == M::Slew) arrived_ = false; mode = M::Idle; }
  bool   moving() override { return mode != M::Idle; }
  bool   tracking() override { return mode == M::Track; }
  bool   arrived() override { return arrived_; }
  bool   limitHit() override { return limit_; }
  double positionDeg() override { return step_deg; }
  void   enable(bool on) override { enabled = on; }

  void advance(int64_t dt_ms) {
    double dt = dt_ms / 1000.0, before = step_deg;
    if (mode == M::Slew) {
      double d = slew_target - step_deg, s = slew_dps * dt;
      if (fabs(d) <= s) {
        step_deg = slew_target;
        if (has_final) { slew_target = final_target; has_final = false; }   // the forward approach
        else { mode = M::Idle; arrived_ = true; }
      }
      else step_deg += d > 0 ? s : -s;
    } else if (mode == M::Track) {
      double want = track_origin + (t_ms + dt_ms - track_origin_true_ms) / 1000.0 * track_dps;
      if (want > max_deg) want = max_deg;
      double d = want - step_deg, s = catchup_dps * dt;
      step_deg += d <= s ? (d > 0 ? d : 0) : s;
      if (step_deg >= max_deg - 1e-9) { mode = M::Idle; limit_ = true; }
    }
    bool jammed = jam_at_ms >= 0 && t_ms >= jam_at_ms;
    double dlt = jammed ? 0 : step_deg - before;
    in_deg += dlt;
    if (lash_deg <= 0) true_deg += dlt;                // tests may set true_deg directly
    else if (dlt != 0) {                                // the output follows through a dead band
      if (in_deg - true_deg > lash_deg / 2) true_deg = in_deg - lash_deg / 2;
      else if (true_deg - in_deg > lash_deg / 2) true_deg = in_deg + lash_deg / 2;
    }
    if (true_deg > max_true_deg_seen) max_true_deg_seen = true_deg;
  }

  // ---- sensors --------------------------------------------------------------
  bool    encoder_ok = true;
  double  battery_v = 12.6;
  int64_t battery_drop_at_ms = -1; double battery_drop_to = 10.0;
  bool   encoderDeg(double *d) override { if (!encoder_ok) return false; *d = true_deg; return true; }
  double batteryV() override { return battery_drop_at_ms >= 0 && t_ms >= battery_drop_at_ms ? battery_drop_to : battery_v; }

  // ---- GPS: a fix arrives gps_fix_s after power-on, if gps_ok -------------
  bool gps_ok = true, gps_on = false; int64_t gps_on_ms = 0; int gps_fix_s = 35;
  double gps_lat = 34.6, gps_lon = -120.4;
  void power(bool on) override { if (on && !gps_on) gps_on_ms = t_ms; gps_on = on; }
  bool fix(int64_t *u, double *lat, double *lon) override {
    if (!gps_on || !gps_ok || t_ms - gps_on_ms < gps_fix_s * 1000) return false;
    *u = trueUtc(); *lat = gps_lat; *lon = gps_lon; return true;
  }

  // ---- camera: each step takes cam_step_s; one step can fail or hang -------
  struct Cam : ICamera {
    bool powered = false; int64_t *t; int step_s = 4;
    int fail_step = -1, hang_step = -1;        // CamStep as int
    int cur = -1; int64_t began = 0;
    std::vector<int> done;                     // steps that completed Ok
    void power(bool on) override { powered = on; }
    void begin(CamStep s) override { cur = (int)s; began = *t; }
    Op poll() override {
      if (cur < 0) return Op::Fail;
      if (!powered) return Op::Fail;
      if (cur == hang_step) return Op::Pending;
      if (*t - began < step_s * 1000) return Op::Pending;
      if (cur == fail_step) { cur = -1; return Op::Fail; }
      done.push_back(cur); cur = -1; return Op::Ok;
    }
    void cancel() override { cur = -1; }
  } cam;


  // ---- store -------------------------------------------------------------------
  bool have = false; Persist rec; std::vector<NightReport> reports;
  bool load(Persist *p) override { if (!have) return false; *p = rec; return true; }
  void save(const Persist &p) override { rec = p; have = true; }
  void report(const NightReport &r) override { reports.push_back(r); }

  // ---- schedule ------------------------------------------------------------------
  std::vector<NightPlan> nights;
  ISchedule *ext = nullptr;      // a real Schedule, when a test wants one
  NightPlan next(int64_t after) override {
    if (ext) return ext->next(after);
    for (auto &n : nights) if (n.stop_utc > after) return n;
    return NightPlan();
  }
  void setSite(double lat, double lon) override { if (ext) ext->setSite(lat, lon); }
  NightPlan refresh(const NightPlan &p) override { return ext ? ext->refresh(p) : p; }

  // ---- system ------------------------------------------------------------------
  bool sleeping = false; int64_t wake_utc = 0; int sleeps = 0;
  bool verbose = false; std::vector<std::string> logs;
  void sleepUntil(int64_t w) override { sleeping = true; wake_utc = w; sleeps++; }
  Radio radio_now = Radio::Off; std::vector<Radio> radios;
  void radio(Radio r) override { radio_now = r; radios.push_back(r); }
  void log(const char *m) override {
    char buf[200];
    snprintf(buf, sizeof buf, "%lld  %s", (long long)trueUtc(), m);
    logs.push_back(buf);
    if (verbose) printf("%s\n", buf);
  }
  int moves = 0, tracks = 0;

  World() { cam.t = &t_ms; }
};

// camera power goes to the camera object, GPS power to the GPS
struct CamProxy : ICamera {
  World &w; explicit CamProxy(World &x) : w(x) {}
  void power(bool on) override { w.cam.power(on); }
  void begin(CamStep s) override { w.cam.begin(s); }
  Op   poll() override { return w.cam.poll(); }
  void cancel() override { w.cam.cancel(); }
};

struct Sim {
  World w;
  CamProxy camp{w};
  FsmConfig cfg;
  Platform plat{w, w, w, w, camp, w, w, w};
  std::unique_ptr<Fsm> fsm;
  int boots = 0;
  int64_t brownout_at_ms = -1;
  int64_t button_at_ms = -1;

  void boot(Wake why) {
    w.boot_ms = w.t_ms;
    w.sleeping = false;
    w.mode = World::M::Idle;      // a reset stops the driver; the counter is re-seeded
    w.enabled = false;
    w.radio_now = Radio::Off;    // a reboot drops the radio too
    boots++;
    fsm.reset(new Fsm(plat, cfg));
    fsm->begin(why);
  }

  // run until true UTC `end`; sleeps become reboots at the alarm (or the button)
  void run(int64_t end_utc) {
    int64_t end_ms = end_utc * 1000;
    while (w.t_ms < end_ms) {
      if (w.sleeping) {
        int64_t alarm_ms = w.wake_utc ? (w.wake_utc - w.rtc_offset_s) * 1000 : -1;
        int64_t next = -1; Wake why = Wake::Alarm;
        if (alarm_ms >= 0) next = alarm_ms;
        if (button_at_ms >= 0 && button_at_ms >= w.t_ms && (next < 0 || button_at_ms < next)) { next = button_at_ms; why = Wake::Button; }
        if (next < 0 || next > end_ms) { w.t_ms = end_ms; break; }
        if (next < w.t_ms) next = w.t_ms;
        w.t_ms = next;
        if (why == Wake::Button) button_at_ms = -1;
        boot(why);
        continue;
      }
      if (brownout_at_ms >= 0 && w.t_ms >= brownout_at_ms) { brownout_at_ms = -1; boot(Wake::PowerOn); continue; }
      fsm->tick();
      w.advance(1000);
      w.t_ms += 1000;
    }
  }
};
