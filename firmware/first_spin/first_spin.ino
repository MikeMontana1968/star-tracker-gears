// =====================================================================
//  first_spin.ino -- guide step 56, with the encoder watching.
//
//  Runs the motor 15 s one way, pauses, 15 s the other way, then disables
//  the driver. While it runs it reads the AS5600 on the output shaft, so the
//  serial monitor (115200) answers, without anyone timing a slow dial:
//
//    - does the motor turn, and does the whole train carry it to the output?
//    - which DIR level turns the platform clockwise seen from the camera end
//      (the AS5600 counts UP clockwise seen from above: DIR pin to GND)?
//    - what microstep mode is the TMC2209 in? Standalone it reads MS1/MS2
//      (JP4/JP5): measured output travel against the travel expected at 1/8,
//      1/16, 1/32 and 1/64 says which.
//
//  Motor at 120 rpm at 1/8 (faster or slower at other modes, see below):
//  the output turns ~21 deg/min, about 5 deg in a 15 s run. Ramps up and
//  down so a low current limit doesn't stall it.
//  Pins as FIRMWARE.md s4: STEP 25, DIR 26, EN 27 (active low).
// =====================================================================

#include <Arduino.h>
#include <Wire.h>

static const int PIN_STEP = 25, PIN_DIR = 26, PIN_EN = 27;
static const int SDA_PIN = 21, SCL_PIN = 22;
static const uint8_t AS5600 = 0x36;

static const double RATIO     = 2048.0;      // motor turns per output turn
static const double FULL_STEP = 200.0;
static const uint32_t RUN_MS  = 15000;
static const uint32_t STEP_US = 312;         // 3200 steps/s: 120 rpm at 1/8
static const uint32_t RAMP_MS = 600;

static int rawAngle() {
  Wire.beginTransmission(AS5600);
  Wire.write(0x0C);
  if (Wire.endTransmission(false) != 0) return -1;
  if (Wire.requestFrom(AS5600, (uint8_t)2) != 2) return -1;
  int hi = Wire.read(), lo = Wire.read();
  return ((hi & 0x0F) << 8) | lo;
}

// Runs one leg; returns the output's travel in encoder counts (unwrapped).
static long runLeg(bool dirHigh, uint32_t &steps) {
  digitalWrite(PIN_DIR, dirHigh ? HIGH : LOW);
  delay(5);
  int last = rawAngle();
  long travel = 0;
  steps = 0;
  uint32_t t0 = millis(), lastRead = t0, lastPrint = t0;
  uint32_t next = micros();
  while (millis() - t0 < RUN_MS) {
    uint32_t el = millis() - t0;
    // trapezoid: ramp the step period in and out
    float k = 1.0f;
    if (el < RAMP_MS) k = el / (float)RAMP_MS;
    else if (RUN_MS - el < RAMP_MS) k = (RUN_MS - el) / (float)RAMP_MS;
    k = max(k, 0.05f);
    uint32_t period = (uint32_t)(STEP_US / k);
    if ((int32_t)(micros() - next) >= 0) {
      digitalWrite(PIN_STEP, HIGH);
      delayMicroseconds(3);
      digitalWrite(PIN_STEP, LOW);
      steps++;
      next += period;
    }
    if (millis() - lastRead >= 20) {            // 50 Hz: the output can't wrap between reads
      lastRead = millis();
      int r = rawAngle();
      if (r >= 0 && last >= 0) {
        int d = r - last;
        if (d > 2048) d -= 4096;
        if (d < -2048) d += 4096;
        travel += d;
      }
      if (r >= 0) last = r;
      if (millis() - lastPrint >= 1000) {
        lastPrint = millis();
        Serial.printf("   t %4.1f s  raw %4d  travel %+5ld counts (%+6.2f deg)\n",
                      el / 1000.0, r, travel, travel * 360.0 / 4096.0);
      }
    }
  }
  return travel;
}

static void report(const char *name, uint32_t steps, long travel) {
  double deg = travel * 360.0 / 4096.0;
  Serial.printf("\n%s: %lu steps, output moved %+ld counts = %+.2f deg\n", name, (unsigned long)steps, travel, deg);
  Serial.println("   expected at   1/8      1/16     1/32     1/64");
  Serial.print  ("              ");
  const int modes[] = {8, 16, 32, 64};
  int best = 0; double bestErr = 1e9;
  for (int i = 0; i < 4; i++) {
    double e = steps / (FULL_STEP * modes[i] * RATIO) * 360.0;
    Serial.printf("%7.2f  ", e);
    double err = fabs(fabs(deg) - e) / e;
    if (err < bestErr) { bestErr = err; best = modes[i]; }
  }
  Serial.println("deg");
  if (fabs(deg) < 0.2) {
    Serial.println("   -> output did NOT move. Motor buzzing? raise VREF a little. Silent? check J4 wiring / 12 V");
    return;
  }
  Serial.printf("   -> matches 1/%d microstepping (%.0f%% off)%s\n", best, bestErr * 100,
                bestErr > 0.15 ? "  -- poor match: slipping or missed steps?" : "");
  Serial.printf("   -> turned %s seen from the camera end\n", travel > 0 ? "CLOCKWISE" : "COUNTER-CLOCKWISE");
}

void setup() {
  Serial.begin(115200);
  delay(1200);
  pinMode(PIN_STEP, OUTPUT);
  pinMode(PIN_DIR, OUTPUT);
  pinMode(PIN_EN, OUTPUT);
  digitalWrite(PIN_EN, HIGH);                    // disabled until we start
  Wire.begin(SDA_PIN, SCL_PIN, 400000);
  esp_log_level_set("i2c.master", ESP_LOG_NONE);

  Serial.println("\n\n=== star tracker :: first spin (step 56) ===");
  if (rawAngle() < 0) { Serial.println("AS5600 not answering -- stopping"); return; }

  Serial.println("starting in 3 s ...");
  delay(3000);
  digitalWrite(PIN_EN, LOW);
  delay(50);

  uint32_t sA, sB;
  Serial.println("\n-- leg A: DIR HIGH --");
  long a = runLeg(true, sA);
  delay(1500);
  Serial.println("\n-- leg B: DIR LOW --");
  long b = runLeg(false, sB);
  digitalWrite(PIN_EN, HIGH);                    // coils off: no heat, no hum

  report("leg A (DIR HIGH)", sA, a);
  report("leg B (DIR LOW) ", sB, b);
  Serial.printf("\nnet %+ld counts (backlash/slip shows here: ~0 is good)\n", a + b);
  Serial.println("Tracking needs CLOCKWISE seen from the camera end (guide step 56),");
  Serial.printf("so the firmware's tracking DIR is: %s\n",
                a > 0 ? "HIGH" : (b > 0 ? "LOW" : "unknown (no motion)"));
  Serial.println("driver disabled. done.");
}

void loop() { delay(1000); }
