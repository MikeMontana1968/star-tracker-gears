// =====================================================================
//  StepTimer -- ESP32 glue for StepEngine: a hardware-timer ISR at
//  cfg.tick_hz that calls engine.tick() and drives STEP/DIR, plus the
//  spinlock that commands from loop()/core 0 must hold (MotionLock).
//
//  begin() must be called from the core the ISR should live on. The
//  Arduino setup()/loop() task runs on core 1 and WiFi on core 0, which
//  is the split FIRMWARE.md s2 asks for.
//
//  The ISR is not flagged IRAM-safe on purpose: during flash writes it is
//  simply deferred, and the position servo catches the lost time up.
// =====================================================================
#pragma once
#ifdef ARDUINO
#include <Arduino.h>
#include "StepEngine.h"

namespace motion {

// The engine's timebase: esp_timer (which restarts at 0 every boot) plus a
// fixed epoch, so a night origin hours before this boot -- a resume, a late
// start -- is still a positive time. 1e13 us is ~115 days of headroom.
constexpr uint64_t TIMER_EPOCH_US = 10000000000000ULL;
uint64_t engineNowUs();

class StepTimer {
 public:
  // dir_forward_level: the DIR pin level that turns the output in the
  // tracking direction (measured 2026-11-21: LOW).
  static void begin(StepEngine *engine, int pin_step, int pin_dir, int dir_forward_level);
  static portMUX_TYPE mux;
};

// RAII lock for commands and status reads from outside the ISR.
struct MotionLock {
  MotionLock()  { portENTER_CRITICAL(&StepTimer::mux); }
  ~MotionLock() { portEXIT_CRITICAL(&StepTimer::mux); }
};

}  // namespace motion
#endif
