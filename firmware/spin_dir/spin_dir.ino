// =====================================================================
//  spin_dir.ino -- spin the motor one way on command, to see which DIR
//  level turns which way. Serial 115200:
//     H  -> DIR HIGH for 8 s      L  -> DIR LOW for 8 s
//  Ramps up to 1600 steps/s (1/8 step = 1 motor rev/s), then disables.
//  Pins as FIRMWARE.md s4: STEP 25, DIR 26, EN 27 (active low).
// =====================================================================

#include <Arduino.h>

static const int PIN_STEP = 25, PIN_DIR = 26, PIN_EN = 27;

static void spin(int dirLevel) {
  digitalWrite(PIN_DIR, dirLevel);
  digitalWrite(PIN_EN, LOW);
  delay(50);
  Serial.printf("spinning DIR %s for 8 s ...\n", dirLevel ? "HIGH" : "LOW");
  uint32_t t0 = millis();
  while (millis() - t0 < 8000) {
    uint32_t t = millis() - t0;
    float rate = 200 + 1400.0f * min(1.0f, t / 1000.0f);   // 1 s ramp
    digitalWrite(PIN_STEP, HIGH); delayMicroseconds(3);
    digitalWrite(PIN_STEP, LOW);
    delayMicroseconds((uint32_t)(1e6f / rate) - 3);
  }
  digitalWrite(PIN_EN, HIGH);
  Serial.println("stopped, driver disabled.");
}

void setup() {
  Serial.begin(115200);
  pinMode(PIN_STEP, OUTPUT); pinMode(PIN_DIR, OUTPUT); pinMode(PIN_EN, OUTPUT);
  digitalWrite(PIN_EN, HIGH);
  delay(800);
  Serial.println("\n=== spin_dir: send H or L ===");
}

void loop() {
  if (!Serial.available()) return;
  char c = toupper(Serial.read());
  if (c == 'H') spin(HIGH);
  else if (c == 'L') spin(LOW);
}
