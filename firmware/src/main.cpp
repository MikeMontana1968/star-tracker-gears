// =====================================================================
//  Star tracker firmware -- the night FSM (lib/app) on the Rev C board
//  through EspPlatform, with a small serial console. The motion/hardware
//  bench console is the other build: pio run -e esp32-bench.
//
//  Serial 115200, one command per line:
//    s          status
//    n          the next night the schedule resolves (no action)
//    U <json>   upload a schedule (one line, <= 8 KB): validated, then
//               written atomically; every error is listed if it is refused
//    T <unix>   set the DS3231 (UTC seconds)
//    r [n]      the last n night reports (default 5)
//    g          GPS / NMEA state
//    B          toggle stay-awake (bench default ON: the board waits on USB
//               instead of deep sleeping -- turn it OFF for deployment)
//    F          clear a fault, then restart
//    Z          restart
// =====================================================================

#include <Arduino.h>
#include <Wire.h>
#include <LittleFS.h>
#include <Preferences.h>
#include "config.h"
#include "Fsm.h"
#include "Schedule.h"
#include "StepEngine.h"
#include "StepTimer.h"
#include "Board.h"
#include "EspPlatform.h"

using namespace motion;

// survives a software restart (not a power cut): how the stay-awake bench
// mode tells the next boot what "woke" it
// schedule validation + ArduinoJson need more than the default 8 KB loop stack
SET_LOOP_TASK_STACK_SIZE(16 * 1024);

RTC_NOINIT_ATTR static uint32_t g_soft_wake;
static constexpr uint32_t SOFT_ALARM = 0xA1A2A3A4, SOFT_BUTTON = 0xB1B2B3B4;

static Tmc2209 tmc;
static As5600 as5600;
static Ds3231 ds3231;
static Oled oled;
static EncoderMap encmap;
static StepEngine *eng;

static esp::Clock clk(ds3231);
static esp::Motion *mot;
static esp::Sensors sens(as5600, encmap);
static esp::Gps gps;
static esp::Camera cam;
static esp::Store store;
static esp::System sys_(ds3231, oled, clk);
static app::ScheduleLimits limits;
static esp::ScheduleStore sstore(limits);
static app::Platform *plat;
static app::Fsm *fsm;

static void loadMap() {
  Preferences p;
  p.begin("enc", true);
  size_t n = p.getBytes("map", &encmap, sizeof encmap);
  p.end();
  if (n != sizeof encmap || !encmap.valid()) encmap = EncoderMap();
}

static void fmtUtc(int64_t t, char *buf, size_t n) {
  Civil c = fromUnix(t);
  snprintf(buf, n, "%02d-%02d %02d:%02dZ", c.month, c.day, c.hour, c.minute);
}

static void status() {
  char a[24], b[24], now[24];
  fmtUtc(clk.utc(), now, sizeof now);
  Serial.printf("state %s%s  fault %s  utc %s%s  %s\n", app::stateName(fsm->state()),
                sys_.asleep() ? " (asleep)" : "", app::faultName(fsm->fault()), now,
                clk.lostPower() ? " (RTC LOST POWER)" : "", esp::System::stayAwake() ? "STAY-AWAKE" : "deep-sleep");
  if (fsm->nightActive()) {
    const app::NightReport &r = fsm->night();
    fmtUtc(r.plan.start_utc, a, sizeof a); fmtUtc(r.plan.stop_utc, b, sizeof b);
    Serial.printf("night %s .. %s from %.2f deg  camera %d gps %d  max div %.3f  warnings %u  resumes %u\n",
                  a, b, r.plan.start_deg, (int)r.camera, (int)r.gps, r.max_div_deg, r.warnings, r.resumes);
  }
  double e = NAN; bool eok = sens.encoderDeg(&e);
  Serial.printf("pos %.4f deg  enc %s%.4f (%s)  1/%u  guard '%s'  vbat %.2f V  schedule %s\n",
                mot->positionDeg(), eok ? "" : "--- ", e, sens.mapValid() ? "map" : "line",
                mot->uartOk() ? tmc.microsteps() : 8, mot->lastGuard(), sens.batteryV(), sstore.source());
  if (sys_.asleep()) {
    fmtUtc(sys_.wakeAt(), a, sizeof a);
    Serial.printf("asleep: wake %s\n", sys_.wakeAt() ? a : "on the button only");
  }
}

static void nextNight() {
  app::NightPlan p = sstore.schedule().next(clk.utc());
  if (!p.valid) { Serial.println("no night ahead in the schedule"); return; }
  char a[24], b[24];
  fmtUtc(p.start_utc, a, sizeof a); fmtUtc(p.stop_utc, b, sizeof b);
  Serial.printf("next night: %s .. %s (%.1f h), from %.2f deg to %.2f deg, camera '%s' policy %d, fallback %u\n",
                a, b, (p.stop_utc - p.start_utc) / 3600.0, p.start_deg,
                p.start_deg + (p.stop_utc - p.start_utc) * 360.0 / 86164.0905, p.camera, (int)p.cam, p.fallback);
}

static void command(String &line) {
  line.trim();
  if (line.isEmpty()) return;
  char c = line[0];
  String arg = line.substring(1); arg.trim();
  switch (c) {
    case 's': status(); break;
    case 'n': nextNight(); break;
    case 'U': {
      static app::Messages err, warn;             // 1.5 KB each: never on the loop stack
      err = app::Messages(); warn = app::Messages();
      bool ok = sstore.upload(arg.c_str(), arg.length(), clk.utc(), &err, &warn);
      Serial.printf("schedule upload: %s\n", ok ? "ACCEPTED" : "REFUSED");
      for (int i = 0; i < err.n; i++) Serial.printf("  error: %s\n", err.msg[i]);
      if (err.dropped) Serial.printf("  (+%d more errors)\n", err.dropped);
      for (int i = 0; i < warn.n; i++) Serial.printf("  warning: %s\n", warn.msg[i]);
      if (ok) { nextNight(); Serial.println("(takes effect at the next IDLE; Z restarts now)"); }
      break;
    }
    case 'T': {
      int64_t t = atoll(arg.c_str());
      bool ok = t > 1700000000 && clk.setUtc(t);
      Serial.printf("clock %s\n", ok ? "set" : "NOT set");
      break;
    }
    case 'r': store.printReports(Serial, arg.length() ? arg.toInt() : 5); break;
    case 'g': {
      const Nmea::Fix &f = gps.nmea();
      Serial.printf("gps %s  rmc %lu gga %lu bad %lu  status %s  q %d sats %d hdop %.1f  %.5f %.5f\n",
                    gps.on() ? "ON" : "off", (unsigned long)f.rmc_count, (unsigned long)f.gga_count,
                    (unsigned long)f.bad_checksum, f.rmc_ok ? "A" : "V", f.quality, f.sats, f.hdop, f.lat, f.lon);
      break;
    }
    case 'B': {
      bool on = !esp::System::stayAwake();
      esp::System::setStayAwake(on);
      Serial.printf("stay-awake %s%s\n", on ? "ON (bench)" : "OFF (deep sleep: for deployment)",
                    on ? "" : " -- the USB serial goes away while it sleeps");
      break;
    }
    case 'F': fsm->clearFault(); Serial.println("fault cleared; restarting"); Serial.flush(); delay(50); ESP.restart(); break;
    case 'Z': Serial.flush(); delay(50); ESP.restart(); break;
    default: Serial.println("commands: s n U<json> T<unix> r[n] g B F Z"); break;
  }
}

static void display() {
  char l[8][22] = {};
  char t[24];
  fmtUtc(clk.utc(), t, sizeof t);
  snprintf(l[0], 22, "%s%s", app::stateName(fsm->state()), sys_.asleep() ? " zzz" : "");
  snprintf(l[1], 22, "%s", t);
  if (fsm->nightActive()) {
    char a[24], b[24];
    fmtUtc(fsm->night().plan.start_utc, a, sizeof a); fmtUtc(fsm->night().plan.stop_utc, b, sizeof b);
    snprintf(l[2], 22, "%.11s-%.5s", a + 6, b + 6);
  } else if (sys_.asleep() && sys_.wakeAt()) {
    char a[24]; fmtUtc(sys_.wakeAt(), a, sizeof a);
    snprintf(l[2], 22, "wake %s", a);
  }
  double e = NAN; sens.encoderDeg(&e);
  snprintf(l[3], 22, "pos %8.3f", mot->positionDeg());
  snprintf(l[4], 22, "enc %8.3f", e);
  snprintf(l[5], 22, "%.2fV %s", sens.batteryV(), fsm->fault() != app::Fault::None ? "FAULT" : "");
  snprintf(l[6], 22, "%s", esp::System::stayAwake() ? "STAY-AWAKE (bench)" : "");
  snprintf(l[7], 22, "%.21s", sys_.lastLog());
  const char *lines[8] = {l[0], l[1], l[2], l[3], l[4], l[5], l[6], l[7]};
  oled.lines(lines);
}

void setup() {
  board::begin();                               // EN high, loads off
  Serial.begin(115200);
  Wire.begin(PIN_SDA, PIN_SCL);
  Wire.setClock(400000);
  delay(200);
  Serial.println("\n=== star tracker :: night FSM ===");

  // what woke us: DS3231 flag, ext0, or the stay-awake soft wake
  bool alarm = ds3231.alarmFired();
  board::Wake bw = board::wakeCause(alarm);
  app::Wake why = bw == board::Wake::Alarm ? app::Wake::Alarm : bw == board::Wake::Button ? app::Wake::Button : app::Wake::PowerOn;
  if (g_soft_wake == SOFT_ALARM) why = app::Wake::Alarm;
  if (g_soft_wake == SOFT_BUTTON) why = app::Wake::Button;
  g_soft_wake = 0;
  if (alarm) ds3231.clearAlarm();

  if (!LittleFS.begin(true)) Serial.println("LittleFS: mount FAILED (formatted?)");
  clk.begin();

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
  mot = new esp::Motion(*eng, tmc, clk);

  bool uart_ok = tmc.begin(Serial2, PIN_TMC_RX, PIN_TMC_TX) && tmc.configure() && tmc.setMicrosteps(8);
  mot->begin(uart_ok);
  loadMap();
  StepTimer::begin(eng, PIN_STEP, PIN_DIR, DIR_FORWARD_LEVEL);
  gps.begin();
  oled.begin();

  limits.travel_min_deg = TRAVEL_MIN_DEG;
  limits.travel_max_deg = TRAVEL_MAX_DEG;
  sstore.load(clk.utc(), Serial);
  store.on_completed = [] { sstore.markGood(); };

  app::FsmConfig fc;
  fc.travel_min_deg = TRAVEL_MIN_DEG;
  fc.travel_max_deg = TRAVEL_MAX_DEG;
  plat = new app::Platform{clk, *mot, sens, gps, cam, store, sstore.schedule(), sys_};
  fsm = new app::Fsm(*plat, fc);

  Serial.printf("wake: %s   driver uart %s   encoder map %s   %s\n",
                why == app::Wake::Alarm ? "alarm" : why == app::Wake::Button ? "button" : "power-on/reset",
                uart_ok ? "ok" : "MISSING", encmap.valid() ? "loaded" : "none",
                esp::System::stayAwake() ? "STAY-AWAKE (bench)" : "deep sleep");
  fsm->begin(why);
  status();
}

void loop() {
  static String buf;
  while (Serial.available()) {
    char ch = (char)Serial.read();
    if (ch == '\n' || ch == '\r') { command(buf); buf = ""; }
    else if (buf.length() < 8300) buf += ch;
  }

  gps.service();
  mot->service(sens);
  if (sys_.asleep()) {
    if (board::wakeLow() && !(ds3231.alarmFired())) g_soft_wake = SOFT_BUTTON;
    else g_soft_wake = SOFT_ALARM;
    sys_.serviceSleep();
  } else {
    fsm->tick();
  }

  static uint32_t last_disp = 0;
  if (millis() - last_disp > 500) { last_disp = millis(); display(); }
  delay(2);
}
