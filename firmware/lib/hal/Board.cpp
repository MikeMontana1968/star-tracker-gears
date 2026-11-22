#ifdef ARDUINO
#include "Board.h"
#include <esp_sleep.h>
#include <driver/rtc_io.h>

namespace board {

void begin() {
  rtc_gpio_hold_dis((gpio_num_t)PIN_TMC_EN);     // released after a deep-sleep wake
  pinMode(PIN_TMC_EN, OUTPUT); digitalWrite(PIN_TMC_EN, HIGH);
  pinMode(PIN_CAM_EN, OUTPUT); digitalWrite(PIN_CAM_EN, LOW);
  pinMode(PIN_GPS_EN, OUTPUT); digitalWrite(PIN_GPS_EN, LOW);
  pinMode(PIN_LED, OUTPUT);    digitalWrite(PIN_LED, LOW);
  pinMode(PIN_WAKE, INPUT);                     // R12 is the pull-up
  analogSetPinAttenuation(PIN_VBAT, ADC_11db);
}

void camera(bool on) { digitalWrite(PIN_CAM_EN, on ? HIGH : LOW); }
void gps(bool on)    { digitalWrite(PIN_GPS_EN, on ? HIGH : LOW); }
void led(bool on)    { digitalWrite(PIN_LED, on ? HIGH : LOW); }
bool wakeLow()       { return digitalRead(PIN_WAKE) == LOW; }

int vbatMillivolts() {
  uint32_t sum = 0;
  for (int i = 0; i < 16; i++) sum += analogReadMilliVolts(PIN_VBAT);
  return (int)((sum / 16) * 118 / 18);
}

Wake wakeCause(bool alarm_flag_set) {
  if (esp_sleep_get_wakeup_cause() != ESP_SLEEP_WAKEUP_EXT0) return Wake::PowerOn;
  return alarm_flag_set ? Wake::Alarm : Wake::Button;   // FIRMWARE.md s4: the flag decides
}

void deepSleep() {
  digitalWrite(PIN_TMC_EN, HIGH);
  rtc_gpio_hold_en((gpio_num_t)PIN_TMC_EN);
  camera(false);
  gps(false);
  led(false);
  esp_sleep_enable_ext0_wakeup((gpio_num_t)PIN_WAKE, 0);
  Serial.flush();
  esp_deep_sleep_start();
}

}  // namespace board
#endif
