#include "Moon.h"
#include <math.h>

namespace moon {

static const double D2R = M_PI / 180.0, R2D = 180.0 / M_PI;

static double norm360(double a) { a = fmod(a, 360.0); return a < 0 ? a + 360.0 : a; }
static double sind(double a) { return sin(a * D2R); }
static double cosd(double a) { return cos(a * D2R); }

// Meeus 47.A / 47.B, largest terms. Units: 1e-6 deg (l, b), 1e-3 km (r).
struct TermLR { int8_t d, m, mp, f; int32_t l, r; };
static const TermLR LR[] = {
  {0, 0, 1, 0, 6288774, -20905355}, {2, 0, -1, 0, 1274027, -3699111}, {2, 0, 0, 0, 658314, -2955968},
  {0, 0, 2, 0, 213618, -569925},    {0, 1, 0, 0, -185116, 48888},     {0, 0, 0, 2, -114332, -3149},
  {2, 0, -2, 0, 58793, 246158},     {2, -1, -1, 0, 57066, -152138},   {2, 0, 1, 0, 53322, -170733},
  {2, -1, 0, 0, 45758, -204586},    {0, 1, -1, 0, -40923, -129620},   {1, 0, 0, 0, -34720, 108743},
  {0, 1, 1, 0, -30383, 104755},     {2, 0, 0, -2, 15327, 10321},      {0, 0, 1, 2, -12528, 0},
  {0, 0, 1, -2, 10980, 79661},      {4, 0, -1, 0, 10675, -34782},     {0, 0, 3, 0, 10034, -23210},
  {4, 0, -2, 0, 8548, -21636},      {2, 1, -1, 0, -7888, 24208},      {2, 1, 0, 0, -6766, 30824},
  {1, 0, -1, 0, -5163, -8379},
};
struct TermB { int8_t d, m, mp, f; int32_t b; };
static const TermB B[] = {
  {0, 0, 0, 1, 5128122}, {0, 0, 1, 1, 280602}, {0, 0, 1, -1, 277693}, {2, 0, 0, -1, 173237},
  {2, 0, -1, 1, 55413},  {2, 0, -1, -1, 46271}, {2, 0, 0, 1, 32573},  {0, 0, 2, 1, 17198},
  {2, 0, 1, -1, 9266},   {0, 0, 2, -1, 8822},
};

Pos at(int64_t utc, double lat, double lon) {
  double jd = utc / 86400.0 + 2440587.5;
  double dj = jd - 2451545.0, T = dj / 36525.0;
  double Lp = norm360(218.3164477 + 481267.88123421 * T);
  double D  = norm360(297.8501921 + 445267.1114034 * T);
  double M  = norm360(357.5291092 + 35999.0502909 * T);
  double Mp = norm360(134.9633964 + 477198.8675055 * T);
  double F  = norm360(93.2720950 + 483202.0175233 * T);
  double E  = 1 - 0.002516 * T;
  double sl = 0, sr = 0, sb = 0;
  for (const TermLR &t : LR) {
    double arg = t.d * D + t.m * M + t.mp * Mp + t.f * F;
    double e = t.m == 0 ? 1 : (t.m == 1 || t.m == -1 ? E : E * E);
    sl += t.l * e * sind(arg);
    sr += t.r * e * cosd(arg);
  }
  for (const TermB &t : B) {
    double arg = t.d * D + t.m * M + t.mp * Mp + t.f * F;
    double e = t.m == 0 ? 1 : (t.m == 1 || t.m == -1 ? E : E * E);
    sb += t.b * e * sind(arg);
  }
  double lam = Lp + sl / 1e6, bet = sb / 1e6;
  Pos p;
  p.dist_km = 385000.56 + sr / 1000.0;
  double eps = 23.439291 - 0.0130042 * T;
  double ra = atan2(sind(lam) * cosd(eps) - tan(bet * D2R) * sind(eps), cosd(lam)) * R2D;
  double dec = asin(sind(bet) * cosd(eps) + cosd(bet) * sind(eps) * sind(lam)) * R2D;
  p.ra_deg = norm360(ra); p.dec_deg = dec;

  // horizon coordinates
  double gmst = norm360(280.46061837 + 360.98564736629 * dj + 0.000387933 * T * T);
  double H = norm360(gmst + lon - p.ra_deg);
  double alt = asin(sind(lat) * sind(dec) + cosd(lat) * cosd(dec) * cosd(H)) * R2D;
  double az = atan2(sind(H), cosd(H) * sind(lat) - tan(dec * D2R) * cosd(lat)) * R2D + 180.0;
  double par = asin(6378.14 / p.dist_km) * R2D;          // horizontal parallax
  p.alt_deg = alt - par * cosd(alt);
  p.az_deg = norm360(az);

  // phase from the sun's ecliptic longitude (Meeus 25, low precision)
  double L0 = 280.46646 + 36000.76983 * T;
  double Ms = 357.52911 + 35999.05029 * T;
  double C = (1.914602 - 0.004817 * T) * sind(Ms) + 0.019993 * sind(2 * Ms) + 0.000289 * sind(3 * Ms);
  double lsun = norm360(L0 + C);
  double elong = acos(cosd(bet) * cosd(lam - lsun)) * R2D;
  p.illum = (1 - cosd(elong)) / 2;
  p.waxing = norm360(lam - lsun) < 180.0;
  return p;
}

static double above(int64_t t, double lat, double lon) { return at(t, lat, lon).alt_deg - RISE_ALT; }

static int64_t crossing(int64_t a, int64_t b, double lat, double lon) {
  bool up_a = above(a, lat, lon) > 0;
  while (b - a > 30) {
    int64_t m = a + (b - a) / 2;
    if ((above(m, lat, lon) > 0) == up_a) a = m; else b = m;
  }
  return b;
}

bool upWindow(double lat, double lon, int64_t from, int64_t to, int64_t *rise, int64_t *set) {
  const int64_t STEP = 1200;
  int64_t best_a = 0, best_b = 0, seg_a = 0;
  bool in = false, have = false;
  int64_t prev = from;
  bool prev_up = above(from, lat, lon) > 0;
  if (prev_up) { in = true; seg_a = from; }
  for (int64_t t = from + STEP;; t += STEP) {
    if (t > to) t = to;
    bool up = above(t, lat, lon) > 0;
    if (up != prev_up) {
      int64_t x = crossing(prev, t, lat, lon);
      if (up) { in = true; seg_a = x; }
      else if (in) {
        in = false;
        if (!have || x - seg_a > best_b - best_a) { best_a = seg_a; best_b = x; have = true; }
      }
    }
    prev = t; prev_up = up;
    if (t >= to) break;
  }
  if (in && (!have || to - seg_a > best_b - best_a)) { best_a = seg_a; best_b = to; have = true; }
  if (!have) return false;
  *rise = best_a; *set = best_b;
  return true;
}

}  // namespace moon
