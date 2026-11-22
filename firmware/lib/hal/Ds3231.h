// =====================================================================
//  Ds3231 -- wall clock and wake alarm (FIRMWARE.md s5: the clock that
//  survives deep sleep; never used to discipline step phase).
//  Kept in UTC. Alarm 1 matches date+h+m+s and pulls INT (GPIO 4, wired-OR
//  with the button, R12 pull-up) low; ext0 wakes on that.
// =====================================================================
#pragma once
#ifdef ARDUINO
#include <Arduino.h>
#include <Wire.h>
#include "CivilTime.h"

class Ds3231 {
 public:
  static constexpr uint8_t ADDR = 0x68;
  explicit Ds3231(TwoWire &w = Wire) : w_(w) {}

  bool present();
  bool now(int64_t *unix_utc);          // false on bus error
  bool set(int64_t unix_utc);           // also clears the oscillator-stop flag
  bool lostPower();                     // OSF: time is not to be trusted
  bool temperature(float *c);

  // Alarm 1 at an absolute UTC time; INT goes low when it fires.
  bool setAlarm(int64_t unix_utc);
  bool alarmFired();                    // A1F
  bool clearAlarm();                    // clear A1F (releases INT)
  bool disableAlarm();

 private:
  bool rd(uint8_t reg, uint8_t *buf, int n);
  bool wr(uint8_t reg, const uint8_t *buf, int n);
  TwoWire &w_;
};
#endif
