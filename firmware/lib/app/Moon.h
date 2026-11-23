// =====================================================================
//  Moon -- where the moon is, and when it is up (moon nights, 2026-11-22).
//  Meeus "Astronomical Algorithms" ch. 47, truncated to the largest terms
//  (~22 in longitude, 10 in latitude, 13 in distance): a few arcminutes,
//  so rise/set to ~2 min, which is far inside what a night needs.
//  Topocentric: the moon's ~1 deg horizontal parallax is applied, and
//  rise/set is the upper limb on the horizon with standard refraction
//  (topocentric centre altitude -0.833 deg), as almanacs define it.
//
//  Checked against Skyfield/DE421 in test_schedule (test_moon_*).
//  Pure C++, double; no allocation.
// =====================================================================
#pragma once
#include <stdint.h>

namespace moon {

constexpr double RISE_ALT = -0.833;   // topocentric centre altitude at rise/set

struct Pos {
  double ra_deg = 0, dec_deg = 0;     // geocentric, of date
  double dist_km = 0;
  double alt_deg = 0, az_deg = 0;     // topocentric, az from north through east
  double illum = 0;                   // illuminated fraction 0..1
  bool   waxing = false;
};

// the moon seen from lat/lon (east-positive) at a UTC instant
Pos at(int64_t utc, double lat, double lon);

// The longest stretch inside [from, to] with the moon above RISE_ALT.
// false if it is never up in the window. Boundaries are good to ~30 s
// (sampled every 20 min, then bisected), so a pass shorter than ~20 min
// can be missed -- far shorter than any night worth running.
bool upWindow(double lat, double lon, int64_t from, int64_t to, int64_t *rise, int64_t *set);

}  // namespace moon
