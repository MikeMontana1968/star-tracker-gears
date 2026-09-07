// =====================================================================
//  sidereal_drive.ino  --  ESP32 sidereal-rate stepper clock
//
//  The whole point: the step interval is 5843.377... us, which is NOT an
//  integer. Rounding it to 5843 us loses 0.377 us per step * 171 steps/s
//  = 65 us/s = 5.6 s per day = 23 arcsec of drift. Visible.
//
//  So we never round. We carry the remainder in a 64-bit accumulator,
//  Bresenham style, and the long-term rate is EXACT -- limited only by
//  the crystal (~20 ppm, i.e. ~0.3 arcsec/day. Irrelevant).
// =====================================================================

#include <Arduino.h>

// ---- pins (TMC2209 / A4988 / DRV8825 all work) ----------------------
const int PIN_STEP = 25;
const int PIN_DIR  = 26;
const int PIN_EN   = 27;   // active LOW on most carriers

// ---- the two numbers that define everything -------------------------
// Sidereal day = 86164.0905 s, in microseconds.
const uint64_t SIDEREAL_US = 86164090500ULL;

// Microsteps per revolution of the OUTPUT shaft.
// Pick ONE of these to match the final stage you actually built.

// (A) all-printed train: 8*8*8*9 = 4608:1, module 1.0 throughout
//     -> 5843.377 us, 171.13 steps/s, motor 3.209 rpm, 0.088 arcsec/ustep
// const uint64_t USTEPS_PER_REV = 200ULL * 16ULL * 4608ULL;  // 14,745,600

// (B) laser final stage: 8*8*8*4 = 2048:1, module 2.0 last stage
//     1/32 microstepping keeps the motor smooth at its lower speed
//     -> 6573.80 us, 152.13 steps/s, motor 1.426 rpm, 0.099 arcsec/ustep
const uint64_t USTEPS_PER_REV = 200ULL * 32ULL * 2048ULL;    // 13,107,200

// This is the ONLY line that changes between the two builds. That is the
// entire point of the accumulator below -- the ratio never has to be tidy.

uint64_t acc     = 0;   // remainder carry, units of 1/USTEPS_PER_REV us
int64_t  next_us = 0;

void setup() {
  Serial.begin(115200);
  pinMode(PIN_STEP, OUTPUT);
  pinMode(PIN_DIR,  OUTPUT);
  pinMode(PIN_EN,   OUTPUT);

  digitalWrite(PIN_DIR, HIGH);    // set for your hemisphere / gear parity
  digitalWrite(PIN_EN,  LOW);     // enable

  next_us = esp_timer_get_time(); // 64-bit monotonic us. Never wraps.
}

void loop() {
  if (esp_timer_get_time() >= next_us) {
    digitalWrite(PIN_STEP, HIGH);
    delayMicroseconds(3);           // most drivers need >1 us
    digitalWrite(PIN_STEP, LOW);

    // exact rational advance: dt alternates 5843 / 5844, mean 5843.377
    acc += SIDEREAL_US;
    uint64_t dt = acc / USTEPS_PER_REV;   // (A) 5843/5844   (B) 6573/6574
    acc -= dt * USTEPS_PER_REV;
    next_us += (int64_t)dt;
  }

  // Everything else -- StallGuard polling, encoder sanity check, buttons,
  // slew mode -- goes here. At 171 steps/s you have ~5.8 ms of slack per
  // step, which is an eternity.
}

// ---------------------------------------------------------------------
//  NOTES
//
//  * Run the motor at LOW current (300-400 mA). It needs almost no torque
//    through a 4608:1 train, and low current means less vibration and heat.
//
//  * TMC2209 in StealthChop at 1/16 or 1/32 is dramatically smoother than
//    an A4988. Worth the extra few dollars.
//
//  * Feedback: do NOT close a loop on speed -- the crystal is already
//    better than your mechanics by four orders of magnitude. Use
//    StallGuard (TMC2209, over UART) or an AS5600 on an intermediate
//    shaft purely to answer "is the train still turning?"
//
//  * Want a different gear ratio? Change only USTEPS_PER_REV. Any ratio
//    works -- that is the whole point of the accumulator.
//
//  * Obsessive tier: discipline next_us against a GPS PPS edge. But
//    polar-alignment error will still dominate by 1000x.
// ---------------------------------------------------------------------
