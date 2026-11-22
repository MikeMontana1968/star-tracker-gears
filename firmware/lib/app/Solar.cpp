#include "Solar.h"
#include <math.h>
#include "CivilTime.h"

namespace solar {

static const double DEG = M_PI / 180.0;

void sunAt(int64_t utc, double *decl_deg, double *eot_min) {
  double jd = (double)utc / 86400.0 + 2440587.5;
  double jc = (jd - 2451545.0) / 36525.0;
  double L0 = fmod(280.46646 + jc * (36000.76983 + jc * 0.0003032), 360.0);
  double M = 357.52911 + jc * (35999.05029 - 0.0001537 * jc);
  double e = 0.016708634 - jc * (0.000042037 + 0.0000001267 * jc);
  double C = sin(M * DEG) * (1.914602 - jc * (0.004817 + 0.000014 * jc)) +
             sin(2 * M * DEG) * (0.019993 - 0.000101 * jc) + sin(3 * M * DEG) * 0.000289;
  double omega = 125.04 - 1934.136 * jc;
  double lambda = L0 + C - 0.00569 - 0.00478 * sin(omega * DEG);
  double eps0 = 23.0 + (26.0 + (21.448 - jc * (46.815 + jc * (0.00059 - jc * 0.001813))) / 60.0) / 60.0;
  double eps = eps0 + 0.00256 * cos(omega * DEG);
  *decl_deg = asin(sin(eps * DEG) * sin(lambda * DEG)) / DEG;
  double y = tan(eps * DEG / 2);
  y *= y;
  double eot = y * sin(2 * L0 * DEG) - 2 * e * sin(M * DEG) +
               4 * e * y * sin(M * DEG) * cos(2 * L0 * DEG) -
               0.5 * y * y * sin(4 * L0 * DEG) - 1.25 * e * e * sin(2 * M * DEG);
  *eot_min = 4.0 * eot / DEG;
}

// transit (solar noon) nearest to `near` UTC, using the EoT at `at`
static int64_t transitNear(double lon, int64_t near, double eot_min) {
  int64_t day0 = (near >= 0 ? near / 86400 : (near - 86399) / 86400) * 86400;
  double t = (double)day0 + (720.0 - 4.0 * lon - eot_min) * 60.0;
  while (t - (double)near > 43200) t -= 86400;
  while ((double)near - t > 43200) t += 86400;
  return (int64_t)llround(t);
}

bool eventUtc(double lat, double lon, int tz_offset_min, int y, int m, int d,
              double alt_deg, bool rising, int64_t *out_utc) {
  // local noon of the evening's date, or of the next day for the morning
  int64_t local_noon = daysFromCivil(y, m, d) * 86400 + 43200 - (int64_t)tz_offset_min * 60;
  if (rising) local_noon += 86400;
  int64_t t = local_noon + (rising ? -6 : 6) * 3600;      // first guess
  for (int it = 0; it < 5; it++) {
    double dec, eot;
    sunAt(t, &dec, &eot);
    double cosH = (sin(alt_deg * DEG) - sin(lat * DEG) * sin(dec * DEG)) / (cos(lat * DEG) * cos(dec * DEG));
    if (cosH > 1.0 || cosH < -1.0) {
      if (it == 0) {                                      // retry once from noon before giving up
        sunAt(local_noon, &dec, &eot);
        cosH = (sin(alt_deg * DEG) - sin(lat * DEG) * sin(dec * DEG)) / (cos(lat * DEG) * cos(dec * DEG));
        if (cosH > 1.0 || cosH < -1.0) return false;
      } else {
        return false;
      }
    }
    double H = acos(cosH) / DEG;                           // degrees of hour angle
    int64_t transit = transitNear(lon, local_noon, eot);
    int64_t next = transit + (int64_t)llround((rising ? -H : H) * 240.0);
    if (llabs(next - t) < 2) { t = next; break; }
    t = next;
  }
  *out_utc = t;
  return true;
}

}  // namespace solar
