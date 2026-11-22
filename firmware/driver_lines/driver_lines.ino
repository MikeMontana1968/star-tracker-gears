// =====================================================================
//  driver_lines.ino -- slow toggle of the TMC2209's three control lines
//  so each can be followed with a meter at the EMPTY driver socket:
//  every 2 s, EN, STEP and DIR all flip between 0 V and 3.3 V together.
//  A line that sits at 0 V (or 3.3 V) through the flips is broken between
//  the ESP32 and the socket, or its GPIO is damaged.
//  Pins as FIRMWARE.md s4: STEP 25, DIR 26, EN 27.
// =====================================================================

#include <Arduino.h>

static const int PINS[] = {27, 25, 26};             // EN, STEP, DIR
static const char *NAMES[] = {"EN (GPIO27)", "STEP (GPIO25)", "DIR (GPIO26)"};

void setup() {
  Serial.begin(115200);
  for (int p : PINS) pinMode(p, OUTPUT);
  delay(1000);
  Serial.println("\n=== driver lines: EN/STEP/DIR flip together every 2 s ===");
}

void loop() {
  static bool level = false;
  level = !level;
  for (int p : PINS) digitalWrite(p, level);
  Serial.printf("all three now %s\n", level ? "HIGH (3.3 V)" : "LOW (0 V)");
  for (int i = 0; i < 3; i++)
    Serial.printf("   %-14s reads back %d\n", NAMES[i], digitalRead(PINS[i]));
  delay(2000);
}
