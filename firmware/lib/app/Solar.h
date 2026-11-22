// =====================================================================
//  Solar -- when the sun crosses an altitude (FIRMWARE.md s5, D5).
//  NOAA solar-position algorithm (declination + equation of time from
//  Meeus' low-precision series), iterated so declination and the equation
//  of time are taken at the event, not at noon. ~40 lines of double.
//
//  Checked against Skyfield/DE421 (test_schedule): within ~1 min at
//  mid-latitudes. Where the sun never reaches the altitude (astronomical
//  twilight in a high-latitude summer, midnight sun) it says so rather
//  than returning a garbage time -- the schedule falls back from there.
// =====================================================================
#pragma once
#include <stdint.h>

namespace solar {

// standard altitudes of the sun's centre, degrees
constexpr double SUNSET = -0.833;      // includes refraction + semi-diameter
constexpr double CIVIL = -6.0, NAUTICAL = -12.0, ASTRONOMICAL = -18.0;

// UTC time the sun's centre crosses alt_deg going DOWN on the evening of
// local date y-m-d (rising = false), or going UP on the following morning
// (rising = true). "Local" is UTC + tz_offset_min. lon is east-positive.
// Returns false if the sun does not cross alt_deg in that half-night.
bool eventUtc(double lat, double lon, int tz_offset_min, int y, int m, int d,
              double alt_deg, bool rising, int64_t *out_utc);

// declination and equation of time (minutes) at a UTC instant; exposed for tests
void sunAt(int64_t utc, double *decl_deg, double *eot_min);

}  // namespace solar
