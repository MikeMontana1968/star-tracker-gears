// =====================================================================
//  driver_hold.ino -- keeps the TMC2209 enabled and stepping slowly
//  (50 steps/s) forever, so the driver can be probed with a meter:
//  VM ~12 V, VIO 3.3 V, EN ~0 V, VREF (pot wiper) ~0.5 V for ~350 mA.
//  Enabled, the motor shaft should resist being turned by hand.
//  Pins as FIRMWARE.md s4: STEP 25, DIR 26, EN 27 (active low).
// =====================================================================

#include <Arduino.h>

static const int PIN_STEP = 25, PIN_DIR = 26, PIN_EN = 27;

void setup() {
  Serial.begin(115200);
  pinMode(PIN_STEP, OUTPUT);
  pinMode(PIN_DIR, OUTPUT);
  pinMode(PIN_EN, OUTPUT);
  digitalWrite(PIN_DIR, HIGH);
  digitalWrite(PIN_EN, LOW);                     // enabled, and it stays that way
  delay(1000);
  Serial.println("\n=== driver hold: EN low, 50 steps/s, until power-off ===");
}

void loop() {
  static uint32_t n = 0;
  digitalWrite(PIN_STEP, HIGH);
  delayMicroseconds(3);
  digitalWrite(PIN_STEP, LOW);
  delay(20);
  if (++n % 250 == 0) Serial.printf("steps %lu  (EN=%d DIR=%d)\n", (unsigned long)n,
                                    digitalRead(PIN_EN), digitalRead(PIN_DIR));
}
