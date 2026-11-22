#include "StepEngine.h"

namespace motion {

StepEngine::StepEngine(const Config &c) : c_(c) {
  setSlewMax(c_.slew_max_hz);
  dv256_ = ((uint64_t)c_.slew_accel << 8) / c_.tick_hz;
  if (dv256_ == 0) dv256_ = 1;
}

void StepEngine::setSlewMax(uint32_t hz) {
  if (hz < c_.slew_start_hz) hz = c_.slew_start_hz;
  if (hz > c_.tick_hz / 2) hz = c_.tick_hz / 2;     // never >1 pulse per tick
  c_.slew_max_hz = hz;
}

// exact: q*U + (r*U)/S, with r < S so r*U < 1.2e18 -- no 64-bit overflow
// for any elapsed time (elapsed*rate_x must fit in 64 bits: 5800 years at 1x)
int64_t StepEngine::siderealUnits(uint64_t elapsed_us, uint32_t rate_x) {
  uint64_t e = elapsed_us * rate_x;
  uint64_t q = e / SIDEREAL_US, r = e % SIDEREAL_US;
  return (int64_t)(q * (uint64_t)USTEPS_PER_REV + (r * (uint64_t)USTEPS_PER_REV) / SIDEREAL_US);
}

bool StepEngine::setPulseUnits(int32_t units) {
  if (mode_ != Mode::Idle || units < 1 || units > 32) return false;
  c_.pulse_units = units;
  return true;
}

bool StepEngine::setPosition(int64_t units) {
  if (mode_ != Mode::Idle) return false;
  pos_ = units;
  fwd_loaded_ = false;          // gear lash state unknown
  fwd_run_ = 0;
  return true;
}

bool StepEngine::moveToForward(int64_t target) {
  if (mode_ != Mode::Idle) return false;
  if (target < c_.min_units || target > c_.max_units) return false;
  bool overshoot = target < pos_ || (!fwd_loaded_ && target - pos_ < c_.backlash_units);
  if (overshoot) {
    int64_t o = target - c_.backlash_units;
    if (o < c_.min_units) return false;
    seg_[0] = o; seg_[1] = target; seg_n_ = 2;
  } else {
    seg_[0] = target; seg_n_ = 1;
  }
  seg_i_ = 0;
  beginSegment();
  mode_ = Mode::Slew;
  last_stop_ = Stop::None;
  return true;
}

bool StepEngine::startTracking(uint64_t now_us, uint32_t rate_x) {
  if (mode_ != Mode::Idle || !fwd_loaded_ || rate_x == 0) return false;
  t0_us_ = now_us;
  track_origin_ = track_target_ = pos_;
  rate_x_ = rate_x;
  min_gap_ticks_ = c_.tick_hz / (c_.track_catchup_hz ? c_.track_catchup_hz : 1);
  if (min_gap_ticks_ == 0) min_gap_ticks_ = 1;
  ticks_since_pulse_ = min_gap_ticks_;
  mode_ = Mode::Track;
  last_stop_ = Stop::None;
  return true;
}

void StepEngine::stop() {
  if (mode_ == Mode::Idle) return;
  mode_ = Mode::Idle;
  last_stop_ = Stop::Commanded;
  v256_ = 0;
}

void StepEngine::beginSegment() {
  v256_ = (uint64_t)c_.slew_start_hz << 8;
  phase_ = (uint64_t)c_.tick_hz << 8;        // first pulse on the first tick
}

Pulse StepEngine::emit(bool forward) {
  pos_ += forward ? c_.pulse_units : -c_.pulse_units;
  pulses_++;
  if (forward) {
    fwd_run_ += c_.pulse_units;
    if (fwd_run_ >= c_.backlash_units) fwd_loaded_ = true;
  } else {
    fwd_run_ = 0;
    fwd_loaded_ = false;
  }
  return {true, forward};
}

Pulse StepEngine::tick(uint64_t now_us) {
  const Pulse none = {false, true};

  if (mode_ == Mode::Slew) {
    int64_t d = seg_[seg_i_] - pos_;
    bool fwd = d > 0;
    uint64_t rem = (uint64_t)(fwd ? d : -d) / (uint64_t)c_.pulse_units;
    if (rem == 0) {
      if (seg_i_ + 1 < seg_n_) {                // overshoot done: turn round
        seg_i_++;
        beginSegment();
        return none;                            // one quiet tick across the DIR change
      }
      mode_ = Mode::Idle;
      last_stop_ = Stop::Arrived;
      fwd_loaded_ = true;                       // every plan ends with a forward run >= lash
      v256_ = 0;
      return none;
    }
    // trapezoid: brake once the stopping distance reaches what is left
    uint64_t s256 = (uint64_t)c_.slew_start_hz << 8;
    uint64_t m256 = (uint64_t)c_.slew_max_hz << 8;
    uint64_t brake = (v256_ * v256_ - s256 * s256) / (2ULL * c_.slew_accel * 65536ULL);
    if (brake >= rem)
      v256_ = (v256_ > s256 + dv256_) ? v256_ - dv256_ : s256;
    else if (v256_ < m256)
      v256_ = (v256_ + dv256_ < m256) ? v256_ + dv256_ : m256;
    phase_ += v256_;
    uint64_t tick256 = (uint64_t)c_.tick_hz << 8;
    if (phase_ >= tick256) {
      phase_ -= tick256;
      return emit(fwd);
    }
    return none;
  }

  if (mode_ == Mode::Track) {
    uint64_t el = now_us > t0_us_ ? now_us - t0_us_ : 0;
    track_target_ = track_origin_ + siderealUnits(el, rate_x_);
    if (track_target_ > c_.max_units) track_target_ = c_.max_units;
    if (ticks_since_pulse_ < min_gap_ticks_) ticks_since_pulse_++;
    if (track_target_ - pos_ >= c_.pulse_units) {
      if (pos_ + c_.pulse_units > c_.max_units) {     // envelope edge
        mode_ = Mode::Idle;
        last_stop_ = Stop::Limit;
        return none;
      }
      if (ticks_since_pulse_ >= min_gap_ticks_) {
        ticks_since_pulse_ = 0;
        return emit(true);
      }
    } else if (track_target_ >= c_.max_units && pos_ + c_.pulse_units > c_.max_units) {
      mode_ = Mode::Idle;
      last_stop_ = Stop::Limit;
    }
    return none;
  }

  return none;
}

}  // namespace motion
