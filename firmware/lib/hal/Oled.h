// =====================================================================
//  Oled -- the module's 0.96" SSD1306 (128x64, 0x3C, shares the sensor
//  I2C bus). Eight 21-character text lines; the app decides what goes on
//  them. Update at 1-2 Hz: a full frame is ~26 ms of bus time at 400 kHz
//  (that blocks I2C, never the step ISR). off() before deep sleep -- the
//  panel draws even with static content (FIRMWARE.md s4).
// =====================================================================
#pragma once
#ifdef ARDUINO
#include <Arduino.h>

class Oled {
 public:
  bool begin();
  void lines(const char *const text[8]);   // nullptr entries are blank
  void on();
  void off();
  bool ok() const { return ok_; }
 private:
  bool ok_ = false;
};
#endif
