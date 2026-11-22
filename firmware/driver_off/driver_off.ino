// driver_off.ino -- parks the TMC2209: EN high (disabled), STEP/DIR low.
// Flash this to stop any motor test. Pins: STEP 25, DIR 26, EN 27.
#include <Arduino.h>

void setup() {
  pinMode(27, OUTPUT); digitalWrite(27, HIGH);   // driver disabled
  pinMode(25, OUTPUT); digitalWrite(25, LOW);
  pinMode(26, OUTPUT); digitalWrite(26, LOW);
  Serial.begin(115200);
  delay(500);
  Serial.println("\n=== driver off: EN high, motor unpowered ===");
}

void loop() { delay(1000); }
