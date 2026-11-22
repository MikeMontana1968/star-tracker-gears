#ifdef ARDUINO
#include "StepTimer.h"
#include <esp_timer.h>

namespace motion {

portMUX_TYPE StepTimer::mux = portMUX_INITIALIZER_UNLOCKED;

static StepEngine *s_eng = nullptr;
static int s_step = -1, s_dir = -1, s_fwd_level = LOW;
static int s_dir_now = -1;
static hw_timer_t *s_timer = nullptr;

static void IRAM_ATTR onTick() {
  uint64_t now = (uint64_t)esp_timer_get_time();
  portENTER_CRITICAL_ISR(&StepTimer::mux);
  Pulse p = s_eng->tick(now);
  portEXIT_CRITICAL_ISR(&StepTimer::mux);
  if (!p.step) return;
  int level = p.forward ? s_fwd_level : !s_fwd_level;
  if (level != s_dir_now) {                 // DIR set-up time >> 20 ns here
    digitalWrite(s_dir, level);
    s_dir_now = level;
  }
  digitalWrite(s_step, HIGH);               // TMC2209 needs >100 ns high
  digitalWrite(s_step, HIGH);
  digitalWrite(s_step, LOW);
}

void StepTimer::begin(StepEngine *engine, int pin_step, int pin_dir, int dir_forward_level) {
  s_eng = engine;
  s_step = pin_step; s_dir = pin_dir; s_fwd_level = dir_forward_level;
  pinMode(s_step, OUTPUT); digitalWrite(s_step, LOW);
  pinMode(s_dir, OUTPUT);  digitalWrite(s_dir, s_fwd_level);
  s_dir_now = s_fwd_level;
  uint32_t period_us = 1000000UL / engine->config().tick_hz;
#if ESP_ARDUINO_VERSION_MAJOR >= 3
  s_timer = timerBegin(1000000);                       // 1 MHz
  timerAttachInterrupt(s_timer, &onTick);
  timerAlarm(s_timer, period_us, true, 0);
#else
  s_timer = timerBegin(0, 80, true);                   // 80 MHz / 80 = 1 MHz
  timerAttachInterrupt(s_timer, &onTick, false);       // level (edge unsupported)
  timerAlarmWrite(s_timer, period_us, true);
  timerAlarmEnable(s_timer);
#endif
}

}  // namespace motion
#endif
