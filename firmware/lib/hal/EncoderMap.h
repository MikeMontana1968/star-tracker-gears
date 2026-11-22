// =====================================================================
//  EncoderMap -- AS5600 raw count -> output position, from one measured
//  forward sweep. Pure C++ (host-tested).
//
//  Why: the magnet is a few tenths of a mm off-axis, so raw counts are
//  ~3% locally non-linear (slice 1 bench: 84 arcmin over 25 deg, and
//  repeatable). A straight line from ENC_RAW_START is good to ~1.5 deg;
//  interpolating in a 1-degree table taken from the step counter is good
//  to about one count (0.09 deg) -- what the encoder can resolve anyway.
//
//  The sweep is taken travelling FORWARD, so the map is exact for a
//  forward-loaded drive; after a reverse move the reading lags by the
//  backlash (~0.35 deg).
// =====================================================================
#pragma once
#include <stdint.h>

struct EncoderMap {
  static constexpr int MAX_POINTS = 200;
  static constexpr uint32_t MAGIC = 0x454D4131;   // "EMA1"

  uint32_t magic = 0;
  int32_t  first_units = 0;     // position of point 0, canonical units
  int32_t  step_units = 0;      // spacing between points
  int16_t  n = 0;
  uint16_t raw[MAX_POINTS] = {};   // raw count at each point, unwrapped (may exceed 4095)

  bool valid() const { return magic == MAGIC && n >= 2 && step_units > 0; }

  // Add the next point of a forward sweep. raw is the 12-bit reading;
  // it is unwrapped against the previous point.
  bool add(uint16_t raw12) {
    if (n >= MAX_POINTS) return false;
    uint32_t r = raw12;
    if (n > 0) {
      uint32_t prev = raw[n - 1];
      while (r + 2048 < prev) r += 4096;        // crossed 4095 -> 0 going forward
    }
    raw[n++] = (uint16_t)r;
    return true;
  }

  // Position (canonical units) for a 12-bit reading, or false if it lies
  // outside the mapped range. Linear interpolation between points.
  bool toUnits(uint16_t raw12, int64_t *units) const {
    if (!valid()) return false;
    for (int wrap = 0; wrap < 2; wrap++) {
      uint32_t r = raw12 + 4096u * wrap;
      if (r < raw[0] || r > raw[n - 1]) continue;
      for (int k = 0; k + 1 < n; k++) {
        if (r >= raw[k] && r <= raw[k + 1]) {
          int32_t span = raw[k + 1] - raw[k];
          int64_t base = first_units + (int64_t)k * step_units;
          *units = span > 0 ? base + (int64_t)(r - raw[k]) * step_units / span : base;
          return true;
        }
      }
    }
    // just past either end (the envelope reaches -2, the sweep -1.5; a
    // rewind overshoots below its target): extrapolate on the map's mean
    // slope rather than fall back to the straight line, which is anchored on
    // a constant and can sit degrees away -- the slip guard reads that jump
    // as a slip (2026-11-22, diag priming to -1.44 deg)
    int64_t total_u = (int64_t)(n - 1) * step_units;
    int32_t total_r = (int32_t)raw[n - 1] - (int32_t)raw[0];
    if (total_r <= 0) return false;
    for (int wrap = -1; wrap < 2; wrap++) {
      int32_t r = (int32_t)raw12 + 4096 * wrap;
      int32_t below = (int32_t)raw[0] - r, above = r - (int32_t)raw[n - 1];
      if (below > 0 && below <= EXTRAP_COUNTS) { *units = first_units - (int64_t)below * total_u / total_r; return true; }
      if (above > 0 && above <= EXTRAP_COUNTS) { *units = first_units + total_u + (int64_t)above * total_u / total_r; return true; }
    }
    return false;
  }
  static constexpr int32_t EXTRAP_COUNTS = 64;   // ~5.6 deg of output
};
