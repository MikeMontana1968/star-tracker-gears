// =====================================================================
//  Board -- the Rev C controller's simple I/O: load switches, battery
//  divider, status LED, wake line, deep sleep.
//
//    CAM_EN  GPIO32 -> Q3 -> Q2 (P-FET): HIGH = camera 5 V on. R5 pull-down.
//    GPS_EN  GPIO17 -> Q5 -> Q4 (PNP):   HIGH = GPS on.        R19 pull-down.
//    VBAT    GPIO34, R14 100k / R15 18k: Vbat = Vpin * 118/18.
//    WAKE    GPIO4, DS3231 INT + SW1 wired-OR, R12 4.7k pull-up, active LOW.
//    TMC_EN  GPIO27, active LOW: held HIGH through deep sleep (RTC hold),
//            or a floating EN could energise the motor all day.
// =====================================================================
#pragma once
#ifdef ARDUINO
#include <Arduino.h>

namespace board {

constexpr int PIN_CAM_EN = 32, PIN_GPS_EN = 17, PIN_VBAT = 34, PIN_LED = 13, PIN_WAKE = 4;
constexpr int PIN_TMC_EN = 27;

void begin();                         // all loads off, LED off
void camera(bool on);
void gps(bool on);
void led(bool on);
bool wakeLow();                       // WAKE line asserted (alarm or button)
int  vbatMillivolts();                // averaged, calibrated ADC

enum class Wake { PowerOn, Alarm, Button, Other };
Wake wakeCause(bool alarm_flag_set);  // call once at boot, after reading the DS3231

// Everything off, TMC EN held high, wake on WAKE low. Does not return.
[[noreturn]] void deepSleep();

}  // namespace board
#endif
