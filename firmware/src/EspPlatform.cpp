#include "EspPlatform.h"
#include <LittleFS.h>
#include <Preferences.h>
#include <esp_timer.h>
#include "config.h"
#include "Board.h"
#include "StepTimer.h"

using namespace motion;

namespace esp {

static constexpr int64_t UNITS_PER_COUNT = USTEPS_PER_REV / 4096;   // 3200

// ---- clock -------------------------------------------------------------------
void Clock::begin() { refresh(); }

void Clock::refresh() {
  int64_t t;
  if (rtc_.now(&t)) {
    base_utc_ = t; base_ms_ = millis(); ok_ = true;
    lost_ = rtc_.lostPower();
  }
  last_read_ = millis();
}

int64_t Clock::utcMs() {
  if (millis() - last_read_ > 10000) refresh();
  return base_utc_ * 1000 + (int64_t)(uint32_t)(millis() - base_ms_);
}
int64_t Clock::utc() { return utcMs() / 1000; }

bool Clock::setUtc(int64_t t) {
  bool ok = rtc_.set(t);
  refresh();
  return ok;
}

// ---- motion --------------------------------------------------------------------
static void driverPin(bool on) { digitalWrite(PIN_EN, on ? LOW : HIGH); }

bool Motion::useMicrosteps(uint16_t u) {
  if (!uart_ok_) return u == 8;
  if (tmc_.microsteps() == u) return true;
  if (!tmc_.setMicrosteps(u)) return false;
  MotionLock l;
  return eng_.setPulseUnits(32 / u);
}

bool Motion::setPosition(double deg) {
  MotionLock l;
  return eng_.setPosition(degToUnits(deg));
}

bool Motion::moveTo(double deg) {
  if (moving()) return false;
  if (!useMicrosteps(8)) return false;
  driverPin(true); delay(5);
  bool ok; { MotionLock l; ok = eng_.moveToForward(degToUnits(deg)); }
  jam_armed_ = false;
  if (!ok) drive();
  return ok;
}

bool Motion::startTracking(double origin_deg, int64_t origin_utc, app::TrackRate rate) {
  if (moving()) return false;
  if (!useMicrosteps(32)) return false;
  driverPin(true); delay(5);
  uint64_t now_us = engineNowUs();
  int64_t behind_us = (clock_.utcMs() - origin_utc * 1000) * 1000;
  if (behind_us < 0) behind_us = 0;
  if ((uint64_t)behind_us > now_us) return false;       // > 115 days: not a night
  bool ok; { MotionLock l; ok = eng_.startTrackingAt(degToUnits(origin_deg), now_us - behind_us, now_us,
                                                   rate == app::TrackRate::Lunar ? motion::LUNAR_US :
                                                   rate == app::TrackRate::Solar ? motion::SOLAR_US : motion::SIDEREAL_US); }
  jam_armed_ = false;
  return ok;
}

void Motion::stop() { MotionLock l; eng_.stop(); }
bool Motion::moving() { MotionLock l; return eng_.mode() != Mode::Idle; }
bool Motion::tracking() { MotionLock l; return eng_.mode() == Mode::Track; }
bool Motion::arrived() { MotionLock l; return eng_.mode() == Mode::Idle && eng_.lastStop() == Stop::Arrived; }
bool Motion::limitHit() { MotionLock l; return eng_.lastStop() == Stop::Limit; }
double Motion::positionDeg() { MotionLock l; return unitsToDeg(eng_.position()); }
void Motion::setOvershootDeg(double d) { MotionLock l; eng_.setBacklash(degToUnits(d)); }
double Motion::overshootDeg() { MotionLock l; return unitsToDeg(eng_.backlash()); }
int64_t Motion::servoError() { MotionLock l; return eng_.mode() == motion::Mode::Track ? eng_.target() - eng_.position() : 0; }
void Motion::drive() { driverPin(enabled_ || moving()); }

void Motion::service(Sensors &s) {
  // ---- driver watchdog: a TMC reset reverts MRES to 1/8 (pulses mis-sized)
  if (uart_ok_ && millis() - last_health_ > 1000) {
    last_health_ = millis();
    bool was_reset;
    if (tmc_.health(&was_reset) && was_reset) {
      guard_ = "driver reset";
      Serial.println("GUARD driver reset: re-seeding the counter from the encoder");
      if (moving()) { stop(); }
      int64_t u;
      if (s.encoderUnits(s.raw(), &u)) { MotionLock l; eng_.setPosition(u); }
      { MotionLock l; eng_.setPulseUnits(32 / tmc_.microsteps()); }
    }
  }
  // ---- jam + slip guards (as the bench console; FIRMWARE.md slice 3)
  if (!moving()) { jam_armed_ = false; drive(); return; }
  driverPin(true);
  int64_t pos; { MotionLock l; pos = eng_.position(); }
  int r = s.raw();
  int64_t eu;
  if (r < 0) return;
  if (!jam_armed_) {
    jam_pos0_ = pos; jam_raw0_ = r; jam_armed_ = true;
    if (s.encoderUnits(r, &eu, &slip_map_)) slip_off0_ = eu - pos;
    return;
  }
  bool fm = false;
  bool eok = s.encoderUnits(r, &eu, &fm);
  // crossing between the straight line (outside the calibrated arc) and the map
  // is a step in the reading, not a slip: re-arm the baseline there
  if (eok && fm != slip_map_) { slip_map_ = fm; slip_off0_ = eu - pos; }
  // the threshold follows where THIS reading came from (2026-11-22: a home from
  // -64 deg tripped the map's 2 deg on line readings that are good to ~3%)
  if (eok && llabs((eu - pos) - slip_off0_) > degToUnits(fm ? SLIP_DEG_MAP : SLIP_DEG_LINE)) {
    Serial.printf("GUARD slip: pos %.3f deg, encoder-minus-counter drifted %.3f deg this move\n", unitsToDeg(pos), unitsToDeg((eu - pos) - slip_off0_));
    stop(); guard_ = "slip"; jam_armed_ = false; return;
  }
  if (llabs(pos - jam_pos0_) >= degToUnits(JAM_WINDOW_DEG)) {
    int d = r - jam_raw0_;
    if (d > 2048) d -= 4096;
    if (d < -2048) d += 4096;
    if (abs(d) < JAM_MIN_COUNTS) {
      Serial.printf("GUARD jam: pos %.3f deg, counter moved %.3f deg, encoder %d counts (raw %d -> %d)\n",
                    unitsToDeg(pos), unitsToDeg(pos - jam_pos0_), d, jam_raw0_, r);
      stop(); guard_ = "jam"; jam_armed_ = false; return;
    }
    jam_pos0_ = pos; jam_raw0_ = r;
  }
}

// ---- sensors -------------------------------------------------------------------
bool Sensors::encoderUnits(int raw, int64_t *u, bool *from_map) {
  if (from_map) *from_map = false;
  if (raw < 0) return false;
  if (map_.valid() && map_.toUnits((uint16_t)raw, u)) { if (from_map) *from_map = true; return true; }
  int d = raw - ENC_RAW_START;                 // line, unwrapped into -90..+270 deg
  while (d >= 3072) d -= 4096;
  while (d < -1024) d += 4096;
  *u = (int64_t)d * UNITS_PER_COUNT;
  return true;
}

bool Sensors::encoderDeg(double *deg) {
  As5600::Reading r = enc_.read();
  if (!r.ok || !r.md) return false;
  int64_t u;
  if (!encoderUnits(r.raw, &u)) return false;
  *deg = unitsToDeg(u);
  return true;
}

double Sensors::batteryV() {
  if (!last_v_ms_ || millis() - last_v_ms_ > 1000) { v_ = board::vbatMillivolts() / 1000.0; last_v_ms_ = millis(); }
  return v_;
}

// ---- GPS ------------------------------------------------------------------------
void Gps::begin() { Serial1.begin(9600, SERIAL_8N1, 18, 5); }
void Gps::power(bool on) { board::gps(on); on_ = on; if (on) { seen_rmc_ = nmea_.fix().rmc_count; } }
void Gps::service() {
  if (echo_ && (int32_t)(millis() - echo_until_) >= 0) { echo_ = false; Serial.println("\n[gps echo off]"); }
  while (Serial1.available()) {
    char c = (char)Serial1.read();
    bytes_++;
    if (echo_) Serial.write(c);
    if (nmea_.feed(c)) last_rmc_ms_ = millis();
  }
}
bool Gps::fix(int64_t *utc, double *lat, double *lon) {
  service();
  // only sentences since this power-on, and the clock carried on from the RMC's arrival
  if (!on_ || nmea_.fix().rmc_count == seen_rmc_ || !nmea_.usable()) return false;
  *utc = nmea_.fix().utc + (int64_t)((millis() - last_rmc_ms_) / 1000);
  *lat = nmea_.fix().lat; *lon = nmea_.fix().lon;
  return true;
}


// ---- store -----------------------------------------------------------------------
bool Store::load(app::Persist *p) {
  Preferences pr;
  pr.begin("fsm", true);
  size_t n = pr.getBytes("rec", p, sizeof *p);
  pr.end();
  return n == sizeof *p && p->magic == app::Persist::MAGIC;
}

void Store::save(const app::Persist &p) {
  if (have_last_ && !memcmp(&p, &last_, sizeof p)) return;   // flash wear: only real changes
  Preferences pr;
  pr.begin("fsm", false);
  pr.putBytes("rec", &p, sizeof p);
  pr.end();
  last_ = p; have_last_ = true;
}

static const char *outcomeName(app::Outcome o) {
  switch (o) {
    case app::Outcome::Completed: return "completed";
    case app::Outcome::Skipped: return "skipped";
    case app::Outcome::SafeStopped: return "safe_stopped";
    default: return "faulted";
  }
}
static const char *opName(app::Op o) { return o == app::Op::Ok ? "ok" : o == app::Op::Fail ? "fail" : "not_tried"; }

void Store::report(const app::NightReport &r) {
  char line[480];
  snprintf(line, sizeof line,
           "{\"day\":%ld,\"plan_start\":%lld,\"plan_stop\":%lld,\"start_deg\":%.3f,\"outcome\":\"%s\","
           "\"reason\":\"%s\",\"track_start\":%lld,\"track_stop\":%lld,\"max_div_deg\":%.3f,"
           "\"min_battery_v\":%.2f,\"camera\":\"%s\",\"gps\":\"%s\",\"rtc_drift_s\":%lld,"
           "\"warnings\":%u,\"resumes\":%u,\"fallback\":%u,\"test\":%s}",
           (long)r.plan.day, (long long)r.plan.start_utc, (long long)r.plan.stop_utc, r.plan.start_deg,
           outcomeName(r.outcome), r.reason, (long long)r.track_start_utc, (long long)r.track_stop_utc,
           r.max_div_deg, r.min_battery_v, opName(r.camera), opName(r.gps), (long long)r.rtc_drift_s,
           r.warnings, r.resumes, r.plan.fallback, r.plan.test ? "true" : "false");
  File f = LittleFS.open("/nights.ndjson", "a");
  if (f) {
    f.println(line);
    size_t sz = f.size();
    f.close();
    if (sz > 32768) { LittleFS.remove("/nights.old"); LittleFS.rename("/nights.ndjson", "/nights.old"); }
  }
  Serial.printf("NIGHT REPORT %s\n", line);
  if (r.outcome == app::Outcome::Completed && on_completed) on_completed();
}

int Store::tail(int last_n, String *out) {
  *out = "";
  File f = LittleFS.open("/nights.ndjson", "r");
  if (!f) return 0;
  int total = 0;
  while (f.available()) if (f.read() == '\n') total++;
  f.seek(0);
  int line = 0, got = 0;
  while (f.available()) {
    String s = f.readStringUntil('\n');
    if (++line > total - last_n) { *out += s; *out += '\n'; got++; }
  }
  f.close();
  return got;
}

void Store::printReports(Print &out, int last_n) {
  File f = LittleFS.open("/nights.ndjson", "r");
  if (!f) { out.println("(no night reports yet)"); return; }
  // two passes: count, then print the tail
  int total = 0;
  while (f.available()) if (f.read() == '\n') total++;
  f.seek(0);
  int line = 0;
  String s;
  while (f.available()) {
    s = f.readStringUntil('\n');
    if (++line > total - last_n) out.println(s);
  }
  f.close();
}

// ---- system ----------------------------------------------------------------------
static int8_t s_awake = -1;                    // cached: read NVS once
bool System::stayAwake() {
  if (s_awake < 0) {
    Preferences pr;
    bool v = true;                              // bench default: stay on USB
    if (pr.begin("sys", false)) { v = pr.getBool("awake", true); pr.end(); }
    s_awake = v;
  }
  return s_awake;
}
void System::setStayAwake(bool on) {
  Preferences pr; pr.begin("sys", false); pr.putBool("awake", on); pr.end();
  s_awake = on;
}

void System::log(const char *msg) {
  Serial.printf("%lld %s\n", (long long)clock_.utc(), msg);
  strncpy(last_, msg, sizeof last_ - 1);
  if (on_log) on_log(clock_.utc(), msg);
}

void System::sleepUntil(int64_t wake) {
  wake_ = wake;
  if (wake) rtc_.setAlarm(wake); else rtc_.disableAlarm();
  if (stayAwake()) {
    asleep_ = true;
    Serial.printf("(stay-awake: holding until %lld or the button, then restarting)\n", (long long)wake);
    return;
  }
  Serial.println("deep sleep");
  oled_.off();
  board::deepSleep();
}

void System::serviceSleep() {
  if (!asleep_) return;
  bool alarm = wake_ && clock_.utc() >= wake_;
  if (alarm || board::wakeLow()) {
    Serial.printf("stay-awake wake (%s): restarting\n", alarm ? "alarm time" : "button/INT");
    Serial.flush();
    delay(50);
    ESP.restart();
  }
}

// ---- schedule store -----------------------------------------------------------------
app::ScheduleDoc ScheduleStore::doc_;
app::ScheduleDoc ScheduleStore::trial_;

static bool readFile(const char *path, String *out) {
  File f = LittleFS.open(path, "r");
  if (!f) return false;
  *out = f.readString();
  f.close();
  return true;
}

bool ScheduleStore::load(int64_t now_utc, Print &log) {
  const char *paths[] = {"/schedule.json", "/schedule.good.json"};
  for (const char *p : paths) {
    String s;
    if (!readFile(p, &s)) continue;
    static app::Messages err, warn;
    err = app::Messages(); warn = app::Messages();
    if (app::parseSchedule(s.c_str(), s.length(), lim_, now_utc, &doc_, &err, &warn)) {
      sched_.reload();
      valid_ = true;
      source_ = p;
      log.printf("schedule: %s (%d nights listed%s)\n", p, doc_.n, doc_.repeat ? ", repeating" : "");
      for (int i = 0; i < warn.n; i++) log.printf("  warning: %s\n", warn.msg[i]);
      return true;
    }
    log.printf("schedule: %s REJECTED at boot:\n", p);
    for (int i = 0; i < err.n; i++) log.printf("  %s\n", err.msg[i]);
  }
  valid_ = false;
  source_ = "none";
  log.println("schedule: none loaded (upload one with U)");
  return false;
}

bool ScheduleStore::upload(const char *json, size_t len, int64_t now_utc, app::Messages *err, app::Messages *warn) {
  app::ScheduleDoc &trial = trial_;               // validate into the scratch doc first
  if (!app::parseSchedule(json, len, lim_, now_utc, &trial, err, warn)) return false;
  // atomic: write tmp, close (flushes), then rename over the live file
  File f = LittleFS.open("/schedule.tmp", "w");
  if (!f) { err->add("filesystem: cannot write"); return false; }
  size_t w = f.write((const uint8_t *)json, len);
  f.close();
  if (w != len) { err->add("filesystem: short write"); LittleFS.remove("/schedule.tmp"); return false; }
  LittleFS.remove("/schedule.json");
  if (!LittleFS.rename("/schedule.tmp", "/schedule.json")) { err->add("filesystem: rename failed"); return false; }
  doc_ = trial;
  sched_.reload();
  valid_ = true;
  source_ = "/schedule.json";
  gen_++;
  return true;
}

app::Schedule *ScheduleStore::check(const char *json, size_t len, int64_t now_utc, app::Messages *err, app::Messages *warn) {
  if (!app::parseSchedule(json, len, lim_, now_utc, &trial_, err, warn)) return nullptr;
  trial_sched_.reload();
  return &trial_sched_;
}

bool ScheduleStore::remove() {
  LittleFS.remove("/schedule.json");
  LittleFS.remove("/schedule.good.json");
  valid_ = false;
  source_ = "none";
  gen_++;
  return !LittleFS.exists("/schedule.json") && !LittleFS.exists("/schedule.good.json");
}

bool ScheduleStore::text(String *out) {
  if (!valid_) return false;
  return readFile(source_, out);
}

void ScheduleStore::markGood() {
  String s;
  if (!readFile("/schedule.json", &s)) return;
  String g;
  if (readFile("/schedule.good.json", &g) && g == s) return;
  File f = LittleFS.open("/schedule.good.tmp", "w");
  if (!f) return;
  f.print(s);
  f.close();
  LittleFS.remove("/schedule.good.json");
  LittleFS.rename("/schedule.good.tmp", "/schedule.good.json");
}

}  // namespace esp
