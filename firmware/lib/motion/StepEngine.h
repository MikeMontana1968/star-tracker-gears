// =====================================================================
//  StepEngine -- position-servo step generator (FIRMWARE.md s2, D1).
//
//  Pure C++: no Arduino, no FreeRTOS. tick(now_us) is called at a fixed
//  rate (20 kHz on the ESP32 timer, any rate in the host tests) and says
//  whether to emit a STEP pulse and in which direction. Everything that
//  decides motion lives here, so all of it is host-testable.
//
//  Rules this class enforces (FIRMWARE.md s2):
//   * One canonical position unit: 1/32 microstep of the MOTOR, always.
//     A pulse advances it by pulse_units (4 at 1/8, 1 at 1/32). The
//     counter's meaning never changes with MRES.
//   * Tracking asks "where should I be?" -- target(t) is exact 64-bit
//     rational arithmetic on elapsed time, so a stall of any length is
//     caught up and lost time never becomes lost steps.
//   * moveToForward() is the ONLY way to request a move, and every move
//     ARRIVES travelling forward (the tracking direction), overshooting
//     by backlash_units when it has to approach from the reverse side.
//   * The travel envelope is a hard limit: moves outside it are refused,
//     tracking stops at its edge.
//
//  Threading: commands and tick() must not run concurrently. The ESP32
//  glue (StepTimer) wraps both in one spinlock.
// =====================================================================
#pragma once
#include <stdint.h>

namespace motion {

// Output shaft: 2048:1 gear train x 200 full steps x 32 (canonical units).
constexpr int64_t  USTEPS_PER_REV = 200LL * 32LL * 2048LL;   // 13,107,200
constexpr uint64_t SIDEREAL_US    = 86164090500ULL;          // 86164.0905 s

inline double unitsToDeg(int64_t u) { return (double)u * 360.0 / (double)USTEPS_PER_REV; }
inline int64_t degToUnits(double d) {
  double u = d * (double)USTEPS_PER_REV / 360.0;
  return (int64_t)(u < 0 ? u - 0.5 : u + 0.5);
}

struct Config {
  uint32_t tick_hz          = 20000;  // rate tick() is called at
  int32_t  pulse_units      = 4;      // canonical units per STEP pulse (1/8 = 4)
  uint32_t slew_start_hz    = 400;    // pulse rate a slew starts and ends at
  uint32_t slew_max_hz      = 3200;   // cruise pulse rate
  uint32_t slew_accel       = 3200;   // pulses/s^2
  uint32_t track_catchup_hz = 1600;   // pulse-rate cap while tracking catches up
  int64_t  backlash_units   = 16000;  // overshoot for a forward arrival
  int64_t  min_units        = 0;      // travel envelope, canonical units
  int64_t  max_units        = 0;
};

enum class Mode : uint8_t { Idle, Track, Slew };
enum class Stop : uint8_t { None, Arrived, Limit, Commanded };

struct Pulse {
  bool step;      // emit one STEP pulse this tick
  bool forward;   // direction for it (valid when step)
};

class StepEngine {
 public:
  explicit StepEngine(const Config &c = Config());

  // ---- commands (caller serialises against tick) --------------------
  // Seed the counter, e.g. from the encoder at boot. Idle only. The
  // backlash state becomes unknown, so the next move pre-rolls.
  bool setPosition(int64_t units);
  // Move so as to arrive at target travelling forward. False if the move
  // (including its overshoot point) leaves the envelope, or not Idle.
  bool moveToForward(int64_t target);
  // Track from the current position at rate_x times sidereal. Only from
  // Idle and only after a forward arrival (backlash taken up).
  bool startTracking(uint64_t now_us, uint32_t rate_x = 1);
  // Track the line target = origin + sidereal(now - t0_us). t0 may be in the
  // past (resume after a reboot, late start): the engine catches up at the
  // capped rate. Same preconditions as startTracking.
  bool startTrackingAt(int64_t origin_units, uint64_t t0_us, uint64_t now_us);
  void stop();                                // immediate, -> Idle
  void setSlewMax(uint32_t hz);
  // Units per pulse after the driver's MRES changed (1/8 -> 4, 1/32 -> 1).
  // Idle only: the counter keeps its meaning, only the pulse size changes.
  bool setPulseUnits(int32_t units);

  // ---- the ISR body ----------------------------------------------------
  Pulse tick(uint64_t now_us);

  // ---- status ------------------------------------------------------------
  int64_t  position()    const { return pos_; }
  int64_t  target()      const {
    return mode_ == Mode::Slew ? seg_[seg_i_] : mode_ == Mode::Track ? track_target_ : pos_;
  }
  Mode     mode()        const { return mode_; }
  Stop     lastStop()    const { return last_stop_; }
  bool     forwardLoaded() const { return fwd_loaded_; }
  uint32_t slewHz()      const { return (uint32_t)(v256_ >> 8); }
  uint64_t pulses()      const { return pulses_; }
  const Config &config() const { return c_; }

  // Exact sidereal target: units travelled after elapsed_us at rate_x.
  static int64_t siderealUnits(uint64_t elapsed_us, uint32_t rate_x = 1);

 private:
  Pulse emit(bool forward);
  void  beginSegment();

  Config   c_;
  Mode     mode_ = Mode::Idle;
  Stop     last_stop_ = Stop::None;
  int64_t  pos_ = 0;
  bool     fwd_loaded_ = false;   // last pulse was forward AND covered backlash
  int64_t  fwd_run_ = 0;          // units moved forward since the last reverse pulse
  uint64_t pulses_ = 0;

  // slew: up to two segments (overshoot point, then the real target)
  int64_t  seg_[2] = {0, 0};
  int      seg_n_ = 0, seg_i_ = 0;
  uint64_t v256_ = 0;             // current pulse rate x 256
  uint64_t phase_ = 0;            // accumulates v256_ per tick
  uint64_t dv256_ = 0;            // rate change per tick x 256

  // track
  uint64_t t0_us_ = 0;
  int64_t  track_origin_ = 0, track_target_ = 0;
  uint32_t rate_x_ = 1;
  uint32_t min_gap_ticks_ = 1, ticks_since_pulse_ = 0;
};

}  // namespace motion
