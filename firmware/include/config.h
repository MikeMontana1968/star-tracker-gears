// =====================================================================
//  config.h -- pins, measured constants, limits. One place (FIRMWARE.md s8).
//  Every number here was measured on the built hardware unless marked.
// =====================================================================
#pragma once

// ---- pins (FIRMWARE.md s4, board Rev C) ---------------------------------
constexpr int PIN_STEP = 25;
constexpr int PIN_DIR  = 26;
constexpr int PIN_EN   = 27;          // active LOW
constexpr int PIN_SDA  = 21;
constexpr int PIN_SCL  = 22;
constexpr int PIN_TMC_TX = 33;        // -> R7 1k -> PDN_UART (JP3 fitted: both pdn pins)
constexpr int PIN_TMC_RX = 14;        // PDN_UART direct

// ---- direction (spin_dir + first_spin, 2026-11-21) ----------------------
// DIR LOW turns the motor CW seen from above the plate; 4 single meshes
// keep the sense, so the output turns CW from the camera end = tracking.
constexpr int DIR_FORWARD_LEVEL = 0;  // LOW

// ---- drive -----------------------------------------------------------------
// Boot/slew: 1/8 microstep (first_spin: 5.01 deg per 46161 pulses vs 5.07
// predicted), one pulse = 4 canonical 1/32 units. Tracking switches the
// driver to 1/32 over the UART (pulse = 1 unit); without the UART it stays
// at 1/8 from MS1/MS2. The position counter never changes meaning.
constexpr int PULSE_UNITS = 4;
constexpr unsigned TICK_HZ = 20000;

// Slew: 3200 pulses/s (2 motor rev/s) with a 1 s ramp is what first_spin
// ran clean. FIRMWARE.md hopes for 6.7 kHz; raise with `v` on the bench
// and only adopt a figure that survives a full rewind.
constexpr unsigned SLEW_START_HZ = 400;
constexpr unsigned SLEW_MAX_HZ   = 3200;
constexpr unsigned SLEW_ACCEL    = 3200;
constexpr unsigned TRACK_CATCHUP_HZ = 1600;

// Backlash: first_spin's reversed leg came up 4 encoder counts short
// (= 12,800 units, 0.35 deg at the output). Overshoot past it.
// TODO: set from a repeat_test run (FIRMWARE.md s9).
constexpr long BACKLASH_UNITS = 16000;

// ---- travel envelope, output degrees from the start position -------------
// PLACEHOLDER: the cable arc is not measured yet (FIRMWARE.md s9). 12 h of
// tracking is 180.5 deg; -2 deg leaves room for the rewind's overshoot.
constexpr double TRAVEL_MIN_DEG = -2.0;
constexpr double TRAVEL_MAX_DEG = 182.0;

// ---- encoder (AS5600 on the output shaft, 1:1) ----------------------------
// Raw at the start position (calibrated 2026-11-21, guide step 59). Raw
// counts UP as the output turns forward. One count = 3200 units.
constexpr int AS5600_ADDR    = 0x36;
// Jam guard: per 1 deg of commanded output travel the encoder must move at
// least 3 counts (0.26 deg). Lash (0.35) + non-linearity (~3%) leave >= 6.
// Added after a calibration rewind ground on against over-tight nuts.
constexpr double JAM_WINDOW_DEG = 1.0;
constexpr int    JAM_MIN_COUNTS = 3;
// Slip guard: max drift of (encoder - position) within one move. The line
// fit is off by up to ~3 deg across the envelope; the map by ~0.1 deg; the
// lash adds 0.35 at a reversal. Added after teeth skipped 25.8 deg unseen.
constexpr double SLIP_DEG_LINE = 4.0;
constexpr double SLIP_DEG_MAP  = 2.0;
constexpr int ENC_RAW_START  = 1029;

// ---- radio / web UI (FIRMWARE.md s7, slice 5) -------------------------------
// softAP "StarTracker-XXXX" (last MAC bytes). WPA2 needs >= 8 characters.
// This repo is public: change AP_PASS before the tracker goes outdoors.
#define AP_PASS "stjarnspar"
constexpr int AP_CHANNEL = 6;
