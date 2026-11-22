// =====================================================================
//  Star tracker firmware -- slices 1 + 3: motion and the hardware layer,
//  driven from a serial bench console. The FSM (slice 2) replaces the
//  console as the thing that decides what happens.
//
//  Serial 115200, one command per line:
//   motion   s            status
//            g <deg>      go to <deg>, arriving forward (1/8 step)
//            t [rate]     pre-roll, switch to 1/32, track at rate x sidereal
//            r            rewind to 0
//            x            stop now
//            z            re-seed the position from the encoder (idle)
//            v <hz>       slew cruise rate, pulses/s
//            e            hold the motor energised while idle (toggle)
//            c            calibrate the encoder map: full forward sweep of the
//                         travel envelope in 1 deg steps (~11 min), saved to NVS
//            C            clear the encoder map (seeding falls back to a line)
//   any move is stopped if the encoder stops following (jam guard)
//   hardware i            driver / encoder / clock / battery detail
//            T <unix>     set the DS3231 (UTC seconds)
//            A <s>        arm the RTC alarm <s> seconds ahead (watch WAKE)
//            S <s>        deep sleep, RTC alarm wakes in <s> seconds
//            k / n        camera / GPS power switch (toggle)
// =====================================================================

#include <Arduino.h>
#include <Wire.h>
#include <Preferences.h>
#include "config.h"
#include "StepEngine.h"
#include "StepTimer.h"
#include "Tmc2209.h"
#include "As5600.h"
#include "Ds3231.h"
#include "EncoderMap.h"
#include "Board.h"
#include "Oled.h"

using namespace motion;

static StepEngine *eng;
static Tmc2209 tmc;
static As5600 enc;
static Ds3231 rtc;
static Oled oled;
static EncoderMap encmap;
static Preferences prefs;

static constexpr int64_t UNITS_PER_COUNT = USTEPS_PER_REV / 4096;   // 3200
static uint32_t I2C_HZ = 400000;
static bool uart_ok = false;
static bool pending_track = false;
static uint32_t pending_rate = 1;
static bool hold_idle = false, cam_on = false, gps_on = false;
static uint32_t idle_since = 0;
static char event_msg[22] = "boot";

// calibration sweep state
static bool cal_active = false;
static int64_t cal_next = 0;
static EncoderMap cal_map;

static void note(const char *fmt, ...) {
  va_list ap; va_start(ap, fmt);
  vsnprintf(event_msg, sizeof event_msg, fmt, ap);
  va_end(ap);
}

// ---- encoder -> position --------------------------------------------------
static bool encToUnits(int raw, int64_t *u) {
  if (raw < 0) return false;
  if (encmap.valid() && encmap.toUnits((uint16_t)raw, u)) return true;
  // straight line from the start count, unwrapped into -90 .. +270 deg so
  // the whole -2 .. 182 envelope (and some overrun either side) decodes
  // unambiguously; a +/-180 window turned +208 deg into -152.
  int d = raw - ENC_RAW_START;
  while (d >= 3072) d -= 4096;
  while (d < -1024) d += 4096;
  *u = (int64_t)d * UNITS_PER_COUNT;
  return true;
}

static void seedFromEncoder() {
  int64_t u;
  if (!encToUnits(enc.rawAveraged(8), &u)) { Serial.println("encoder not answering: position left as is"); return; }
  MotionLock l;
  eng->setPosition(u);
}

static void loadMap() {
  prefs.begin("enc", true);
  size_t n = prefs.getBytes("map", &encmap, sizeof encmap);
  prefs.end();
  if (n != sizeof encmap || !encmap.valid()) encmap = EncoderMap();
}

static void saveMap(const EncoderMap &m) {
  prefs.begin("enc", false);
  prefs.putBytes("map", &m, sizeof m);
  prefs.end();
  encmap = m;
}

// ---- microstep mode: 1/8 to slew, 1/32 to track (idle only) -------------
static bool useMicrosteps(uint16_t usteps) {
  if (!uart_ok) return usteps == 8;            // standalone: MS1/MS2 fix 1/8
  if (tmc.microsteps() == usteps) return true;
  bool ok = tmc.setMicrosteps(usteps);
  if (ok) { MotionLock l; ok = eng->setPulseUnits(32 / usteps); }
  if (!ok) Serial.printf("could not set 1/%u microstepping\n", usteps);
  return ok;
}

// ---- helpers -------------------------------------------------------------
static const char *modeName(Mode m) {
  return m == Mode::Track ? "TRACK" : m == Mode::Slew ? "SLEW" : "IDLE";
}
static const char *stopName(Stop s) {
  switch (s) {
    case Stop::Arrived: return "arrived";
    case Stop::Limit: return "LIMIT";
    case Stop::Commanded: return "stopped";
    default: return "-";
  }
}
static void driver(bool on) { digitalWrite(PIN_EN, on ? LOW : HIGH); }

static void fmtTime(char *buf, size_t n) {
  int64_t t;
  if (!rtc.now(&t)) { snprintf(buf, n, "RTC ---"); return; }
  Civil c = fromUnix(t);
  snprintf(buf, n, "%04d-%02d-%02d %02d:%02d:%02dZ%s", c.year, c.month, c.day,
           c.hour, c.minute, c.second, rtc.lostPower() ? "?" : "");
}

static void status() {
  int64_t pos, tgt; Mode m; Stop st; uint32_t hz; uint64_t n; bool loaded;
  { MotionLock l; pos = eng->position(); tgt = eng->target(); m = eng->mode();
    st = eng->lastStop(); hz = eng->slewHz(); n = eng->pulses(); loaded = eng->forwardLoaded(); }
  int raw = enc.raw();
  int64_t e;
  Serial.printf("%-5s 1/%-2u pos %9.4f  target %9.4f  ", modeName(m), uart_ok ? tmc.microsteps() : 8,
                unitsToDeg(pos), unitsToDeg(tgt));
  if (encToUnits(raw, &e))
    Serial.printf("enc %9.4f (raw %4d, %s, enc-pos %+6.1f')  ", unitsToDeg(e), raw,
                  encmap.valid() ? "map" : "line", unitsToDeg(e - pos) * 60.0);
  else
    Serial.print("enc ---  ");
  if (m == Mode::Slew) Serial.printf("%u p/s  ", hz);
  Serial.printf("pulses %llu  lash %s  last %s\n", (unsigned long long)n,
                loaded ? "taken up" : "unknown", stopName(st));
}

static void info() {
  Tmc2209::Status ts = uart_ok ? tmc.status() : Tmc2209::Status{};
  if (uart_ok && ts.ok)
    Serial.printf("driver: 1/%u  CS %u/31  %s  %s  ot %d otpw %d short %d/%d open %d  MSCNT %u  GSTAT 0x%lx\n",
                  tmc.microsteps(), ts.cs_actual, ts.stealth ? "StealthChop" : "SpreadCycle",
                  ts.standstill ? "standstill" : "moving", ts.ot, ts.otpw, ts.s2g, ts.s2vs, ts.ol,
                  ts.mscnt, (unsigned long)ts.gstat);
  else
    Serial.println("driver: UART not answering (standalone 1/8 from MS1/MS2)");
  As5600::Reading r = enc.read();
  if (r.ok)
    Serial.printf("encoder: raw %u  AGC %u/128  magnitude %u  magnet %s%s%s  map %s (%d pts)\n",
                  r.raw, r.agc, r.magnitude, r.md ? "detected" : "MISSING", r.ml ? " too-weak" : "",
                  r.mh ? " too-strong" : "", encmap.valid() ? "loaded" : "none", encmap.n);
  else
    Serial.println("encoder: not answering");
  char tb[32]; fmtTime(tb, sizeof tb);
  float tc = NAN; rtc.temperature(&tc);
  Serial.printf("clock: %s  %.2f C  %s  alarm flag %d  WAKE line %s\n", tb, tc,
                rtc.lostPower() ? "LOST POWER: set it (T)" : "running", rtc.alarmFired(),
                board::wakeLow() ? "LOW" : "high");
  Serial.printf("power: vbat %.2f V  camera %s  gps %s\n", board::vbatMillivolts() / 1000.0,
                cam_on ? "ON" : "off", gps_on ? "ON" : "off");
}

static void display() {
  int64_t pos; Mode m; bool loaded;
  { MotionLock l; pos = eng->position(); m = eng->mode(); loaded = eng->forwardLoaded(); }
  char l0[22], l1[22], l2[22], l3[22], l4[22], l5[22], l6[22];
  snprintf(l0, sizeof l0, "%-5s %s 1/%u", cal_active ? "CAL" : modeName(m),
           m == Mode::Track ? "x" : "", uart_ok ? tmc.microsteps() : 8);
  if (m == Mode::Track) snprintf(l0 + strlen(l0), sizeof l0 - strlen(l0), " x%u", pending_rate);
  snprintf(l1, sizeof l1, "pos %9.4f deg", unitsToDeg(pos));
  As5600::Reading r = enc.read();
  int64_t e;
  if (r.ok && encToUnits(r.raw, &e)) {
    snprintf(l2, sizeof l2, "enc %8.3f AGC %u", unitsToDeg(e), r.agc);
    snprintf(l3, sizeof l3, "err %+6.1f' %s", unitsToDeg(e - pos) * 60.0, loaded ? "lash ok" : "lash ?");
  } else {
    snprintf(l2, sizeof l2, "enc ---"); l3[0] = 0;
  }
  fmtTime(l4, sizeof l4);
  float tc = NAN; rtc.temperature(&tc);
  snprintf(l5, sizeof l5, "%.2fV  %.1fC %s%s", board::vbatMillivolts() / 1000.0, tc,
           cam_on ? "CAM " : "", gps_on ? "GPS" : "");
  snprintf(l6, sizeof l6, "drv %s", uart_ok ? "uart ok" : "standalone");
  const char *lines[8] = {l0, l1, l2, l3, l4, l5, l6, event_msg};
  oled.lines(lines);
}

// ---- calibration sweep: 1 deg forward steps, averaged read at each --------
static void calStart() {
  cal_map = EncoderMap();
  cal_map.magic = EncoderMap::MAGIC;
  cal_map.first_units = (int32_t)degToUnits(TRAVEL_MIN_DEG + 0.5);   // room for the pre-roll
  cal_map.step_units = (int32_t)degToUnits(1.0);
  cal_next = cal_map.first_units;
  useMicrosteps(8);
  driver(true); delay(5);
  bool ok; { MotionLock l; ok = eng->moveToForward(cal_next); }
  cal_active = ok;
  Serial.println(ok ? "calibrating: forward sweep in 1 deg steps (x aborts)" : "calibration REFUSED");
  note(ok ? "calibrating" : "cal refused");
}

static void calStep() {                       // called when idle during a sweep
  Stop st; { MotionLock l; st = eng->lastStop(); }
  if (st != Stop::Arrived) { cal_active = false; Serial.println("calibration aborted"); note("cal aborted"); return; }
  delay(150);                                 // let the train settle
  int raw = enc.rawAveraged(16);
  if (raw < 0 || !cal_map.add((uint16_t)raw)) { cal_active = false; Serial.println("calibration failed"); return; }
  Serial.printf("  cal %3d  %7.2f deg  raw %4d\n", cal_map.n - 1, unitsToDeg(cal_next), raw);
  cal_next += cal_map.step_units;
  if (cal_next > degToUnits(TRAVEL_MAX_DEG) || cal_map.n >= EncoderMap::MAX_POINTS) {
    cal_active = false;
    saveMap(cal_map);
    Serial.printf("calibration done: %d points saved. Rewind with r.\n", cal_map.n);
    note("cal saved %d pts", cal_map.n);
    return;
  }
  bool ok; { MotionLock l; ok = eng->moveToForward(cal_next); }
  if (!ok) { cal_active = false; Serial.println("calibration move refused"); }
}

// ---- console -------------------------------------------------------------
static void command(String line) {
  line.trim();
  if (line.isEmpty()) return;
  char c = line[0];
  String arg = line.substring(1); arg.trim();
  bool ok = true;
  switch (c) {
    case 's': break;
    case 'g': case 'r': {
      double deg = c == 'r' ? 0.0 : arg.toDouble();
      ok = useMicrosteps(8);
      driver(true); delay(5);
      if (ok) { MotionLock l; ok = eng->moveToForward(degToUnits(deg)); }
      Serial.printf("goto %.4f deg: %s\n", deg, ok ? "moving" : "REFUSED (busy, or outside the envelope)");
      break;
    }
    case 't': {
      pending_rate = arg.length() ? (uint32_t)arg.toInt() : 1;
      if (pending_rate == 0) pending_rate = 1;
      int64_t here; { MotionLock l; here = eng->position(); }
      ok = useMicrosteps(8);
      driver(true); delay(5);
      if (ok) { MotionLock l; ok = eng->moveToForward(here); }     // pre-roll takes the lash up
      pending_track = ok;
      Serial.printf("track x%u: %s\n", pending_rate, ok ? "pre-roll, then tracking" : "REFUSED (busy?)");
      break;
    }
    case 'x': { MotionLock l; eng->stop(); } pending_track = false; cal_active = false;
              Serial.println("stop"); note("stopped"); break;
    case 'z': {
      Mode m; { MotionLock l; m = eng->mode(); }
      if (m != Mode::Idle) { Serial.println("busy"); break; }
      seedFromEncoder(); Serial.println("position re-seeded from the encoder");
      break;
    }
    case 'v': {
      uint32_t hz = (uint32_t)arg.toInt();
      { MotionLock l; eng->setSlewMax(hz); }
      Serial.printf("slew cruise %u pulses/s\n", eng->config().slew_max_hz);
      break;
    }
    case 'e': hold_idle = !hold_idle; Serial.printf("hold while idle: %s\n", hold_idle ? "on" : "off"); break;
    case 'c': calStart(); break;
    case 'C': {
      saveMap(EncoderMap());                  // invalid map: seeding falls back to the line
      Serial.println("encoder map cleared");
      return;
    }
    case 'i': info(); return;
    case 'T': {
      int64_t t = atoll(arg.c_str());
      ok = t > 1700000000 && rtc.set(t);
      char tb[32]; fmtTime(tb, sizeof tb);
      Serial.printf("clock set: %s (%s)\n", tb, ok ? "ok" : "FAILED");
      return;
    }
    case 'A': case 'S': {
      int s = arg.toInt(); if (s < 2) s = 10;
      int64_t t;
      ok = rtc.now(&t) && rtc.setAlarm(t + s);
      Serial.printf("alarm in %d s: %s\n", s, ok ? "armed" : "FAILED");
      if (c == 'S' && ok) {
        Mode m; { MotionLock l; m = eng->mode(); }
        if (m != Mode::Idle) { Serial.println("not sleeping while moving"); return; }
        Serial.println("deep sleep now");
        oled.off();
        board::deepSleep();
      }
      return;
    }
    case 'b': {                                 // I2C scan at both bus speeds
      for (uint32_t hz : {400000u, 100000u}) {
        Wire.setClock(hz);
        Serial.printf("I2C @ %u kHz:", (unsigned)(hz / 1000));
        for (uint8_t a = 1; a < 127; a++) {
          Wire.beginTransmission(a);
          if (Wire.endTransmission() == 0) Serial.printf(" 0x%02X", a);
        }
        int64_t t; Serial.printf("   DS3231 read: %s\n", rtc.now(&t) ? "ok" : "FAIL");
      }
      Wire.setClock(I2C_HZ);
      return;
    }
    case 'k': cam_on = !cam_on; board::camera(cam_on); Serial.printf("camera power %s\n", cam_on ? "ON" : "off"); return;
    case 'n': gps_on = !gps_on; board::gps(gps_on); Serial.printf("GPS power %s\n", gps_on ? "ON" : "off"); return;
    default:
      Serial.println("motion: s g<deg> t[rate] r x z v<hz> e c | hw: i T<unix> A<s> S<s> k n");
      return;
  }
  status();
}

void setup() {
  board::begin();                               // EN high, loads off
  Serial.begin(115200);
  Wire.begin(PIN_SDA, PIN_SCL);
  Wire.setClock(I2C_HZ);
  delay(300);

  bool alarm = rtc.alarmFired();
  board::Wake why = board::wakeCause(alarm);
  if (alarm) rtc.clearAlarm();

  Config c;
  c.tick_hz = TICK_HZ;
  c.pulse_units = PULSE_UNITS;
  c.slew_start_hz = SLEW_START_HZ;
  c.slew_max_hz = SLEW_MAX_HZ;
  c.slew_accel = SLEW_ACCEL;
  c.track_catchup_hz = TRACK_CATCHUP_HZ;
  c.backlash_units = BACKLASH_UNITS;
  c.min_units = degToUnits(TRAVEL_MIN_DEG);
  c.max_units = degToUnits(TRAVEL_MAX_DEG);
  eng = new StepEngine(c);

  uart_ok = tmc.begin(Serial2, PIN_TMC_RX, PIN_TMC_TX) && tmc.configure() && tmc.setMicrosteps(8);
  loadMap();
  seedFromEncoder();
  StepTimer::begin(eng, PIN_STEP, PIN_DIR, DIR_FORWARD_LEVEL);   // ISR on this core (1)
  oled.begin();

  Serial.println("\n=== star tracker :: slices 1+3 ===");
  Serial.printf("wake: %s\n", why == board::Wake::Alarm ? "RTC alarm" : why == board::Wake::Button ? "button" : "power-on/reset");
  Serial.printf("envelope %.1f .. %.1f deg, slew %u p/s, backlash %.3f deg\n",
                TRAVEL_MIN_DEG, TRAVEL_MAX_DEG, SLEW_MAX_HZ, unitsToDeg(BACKLASH_UNITS));
  note(why == board::Wake::Alarm ? "woke: alarm" : why == board::Wake::Button ? "woke: button" : "power on");
  info();
  status();
}

void loop() {
  static String buf;
  while (Serial.available()) {
    char ch = (char)Serial.read();
    if (ch == '\n' || ch == '\r') { command(buf); buf = ""; }
    else if (buf.length() < 40) buf += ch;
  }

  Mode m; Stop st;
  { MotionLock l; m = eng->mode(); st = eng->lastStop(); }

  if (pending_track && m == Mode::Idle) {
    pending_track = false;
    if (st == Stop::Arrived && useMicrosteps(32)) {
      bool ok; { MotionLock l; ok = eng->startTracking(engineNowUs(), pending_rate); }
      Serial.printf("pre-roll done, tracking x%u at 1/%u: %s\n", pending_rate, uart_ok ? 32 : 8, ok ? "started" : "REFUSED");
      note(ok ? "tracking x%u" : "track refused", pending_rate);
      m = ok ? Mode::Track : m;
    }
  }
  if (cal_active && m == Mode::Idle) calStep();

  // driver watchdog: a TMC reset (VM dropout) silently reverts MRES to 1/8
  static uint32_t last_health = 0;
  if (uart_ok && millis() - last_health > 1000) {
    last_health = millis();
    bool was_reset;
    static uint32_t seen_spurious = 0;
    if (!tmc.health(&was_reset)) {
      Serial.printf("[%lu ms] driver UART read failed\n", (unsigned long)millis());
    } else if (tmc.spurious() != seen_spurious) {
      seen_spurious = tmc.spurious();
      Serial.printf("[%lu ms] GSTAT.reset seen but config intact (GSTAT 0x%lx GCONF 0x%lx IFCNT %lu): ignored, #%lu\n",
                    (unsigned long)millis(), (unsigned long)tmc.lastGstat(), (unsigned long)tmc.lastGconf(),
                    (unsigned long)tmc.lastIfcnt(), (unsigned long)seen_spurious);
    } else if (was_reset) {
      Serial.printf("[%lu ms] DRIVER RESET confirmed (GSTAT 0x%lx GCONF 0x%lx IFCNT %lu): config re-applied\n",
                    (unsigned long)millis(), (unsigned long)tmc.lastGstat(), (unsigned long)tmc.lastGconf(),
                    (unsigned long)tmc.lastIfcnt());
      note("driver reset");
      if (m != Mode::Idle) {                   // pulses were counted at the wrong size
        { MotionLock l; eng->stop(); }
        pending_track = false; cal_active = false;
        seedFromEncoder();
        Serial.println("motion stopped, position re-seeded from the encoder");
      }
    }
  }

  // jam guard: for every JAM_WINDOW of commanded travel the encoder must
  // show at least JAM_MIN_COUNTS of motion, or the output is not following
  // (seized bearing, tightened nut, cable caught). Window and threshold
  // leave room for the 0.35 deg lash after a reversal and the ~3% local
  // non-linearity. Runs in every moving mode, map or not.
  // Slip guard: the encoder-minus-position offset may not drift more than
  // SLIP_DEG over one move. Catches teeth skipping, where the output still
  // turns -- just less than commanded -- and the per-degree check passes.
  static bool jam_armed = false;
  static int64_t jam_pos0 = 0, slip_off0 = 0;
  static int jam_raw0 = 0;
  if (m == Mode::Idle) {
    jam_armed = false;
  } else {
    int64_t pos; { MotionLock l; pos = eng->position(); }
    int64_t eu;
    if (!jam_armed) {
      jam_pos0 = pos; jam_raw0 = enc.raw(); jam_armed = jam_raw0 >= 0;
      if (jam_armed && encToUnits(jam_raw0, &eu)) slip_off0 = eu - pos;
    } else if (encToUnits(enc.raw(), &eu) &&
               llabs((eu - pos) - slip_off0) > degToUnits(encmap.valid() ? SLIP_DEG_MAP : SLIP_DEG_LINE)) {
      { MotionLock l; eng->stop(); }
      pending_track = false; cal_active = false;
      Serial.printf("SLIP: encoder and position drifted %.2f deg apart during this move. Motion stopped. "
                    "Gears skipping? Free/fix, then z to re-seed.\n", unitsToDeg((eu - pos) - slip_off0));
      note("SLIP - stopped");
      m = Mode::Idle;
      jam_armed = false;
    } else if (llabs(pos - jam_pos0) >= degToUnits(JAM_WINDOW_DEG)) {
      int r = enc.raw();
      if (r >= 0) {
        int d = r - jam_raw0;
        if (d > 2048) d -= 4096;
        if (d < -2048) d += 4096;
        if (abs(d) < JAM_MIN_COUNTS) {
          { MotionLock l; eng->stop(); }
          pending_track = false; cal_active = false;
          Serial.printf("JAM: commanded %.2f deg, encoder moved %d counts. Motion stopped. "
                        "Free the output, then z to re-seed.\n", unitsToDeg(pos - jam_pos0), d);
          note("JAM - stopped");
          m = Mode::Idle;
        }
        jam_pos0 = pos; jam_raw0 = r;
      }
    }
  }

  if (m != Mode::Idle || cal_active) { driver(true); idle_since = millis(); }
  else if (!hold_idle && millis() - idle_since > 2000) driver(false);

  static uint32_t last = 0, last_disp = 0;
  static Mode last_mode = Mode::Idle;
  if (m != last_mode) { if (!cal_active) status(); last_mode = m; }
  else if (m != Mode::Idle && !cal_active && millis() - last > 2000) { last = millis(); status(); }
  if (millis() - last_disp > 500) { last_disp = millis(); display(); }
  delay(5);
}
