// Schedule + solar solver tests. Run:  pio test -e native
#include <unity.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <string>
#include "Solar.h"
#include "Moon.h"
#include "Schedule.h"
#include "CivilTime.h"

using namespace app;

// Reference crossings from Skyfield 1.54 / JPL DE421 (scratchpad
// ephem/twilight_ref.py): lat, lon, tz_h, y, m, d, alt, rising, utc (0 = none)
struct Ref { double lat, lon; int tz, y, m, d; double alt; int rising; long long utc; };
static const Ref REFS[] = {
  {34.6000, -120.4000, -8, 2026, 11, 22, -0.833, 0, 1795395189},
  {34.6000, -120.4000, -8, 2026, 11, 22, -0.833, 1, 1795444987},
  {34.6000, -120.4000, -8, 2026, 11, 22, -6.000, 0, 1795396819},
  {34.6000, -120.4000, -8, 2026, 11, 22, -6.000, 1, 1795443354},
  {34.6000, -120.4000, -8, 2026, 11, 22, -12.000, 0, 1795398664},
  {34.6000, -120.4000, -8, 2026, 11, 22, -12.000, 1, 1795441506},
  {34.6000, -120.4000, -8, 2026, 11, 22, -18.000, 0, 1795400472},
  {34.6000, -120.4000, -8, 2026, 11, 22, -18.000, 1, 1795439696},
  {34.6000, -120.4000, -8, 2026, 6, 21, -0.833, 0, 1782098267},
  {34.6000, -120.4000, -8, 2026, 6, 21, -0.833, 1, 1782132566},
  {34.6000, -120.4000, -8, 2026, 6, 21, -6.000, 0, 1782100039},
  {34.6000, -120.4000, -8, 2026, 6, 21, -6.000, 1, 1782130794},
  {34.6000, -120.4000, -8, 2026, 6, 21, -12.000, 0, 1782102226},
  {34.6000, -120.4000, -8, 2026, 6, 21, -12.000, 1, 1782128607},
  {34.6000, -120.4000, -8, 2026, 6, 21, -18.000, 0, 1782104635},
  {34.6000, -120.4000, -8, 2026, 6, 21, -18.000, 1, 1782126198},
  {34.6000, -120.4000, -8, 2026, 3, 20, -0.833, 0, 1774059205},
  {34.6000, -120.4000, -8, 2026, 3, 20, -0.833, 1, 1774101817},
  {34.6000, -120.4000, -8, 2026, 3, 20, -6.000, 0, 1774060714},
  {34.6000, -120.4000, -8, 2026, 3, 20, -6.000, 1, 1774100310},
  {34.6000, -120.4000, -8, 2026, 3, 20, -12.000, 0, 1774062476},
  {34.6000, -120.4000, -8, 2026, 3, 20, -12.000, 1, 1774098550},
  {34.6000, -120.4000, -8, 2026, 3, 20, -18.000, 0, 1774064260},
  {34.6000, -120.4000, -8, 2026, 3, 20, -18.000, 1, 1774096768},
  {34.6000, -120.4000, -8, 2026, 12, 21, -0.833, 0, 1797900895},
  {34.6000, -120.4000, -8, 2026, 12, 21, -0.833, 1, 1797951913},
  {34.6000, -120.4000, -8, 2026, 12, 21, -6.000, 0, 1797902577},
  {34.6000, -120.4000, -8, 2026, 12, 21, -6.000, 1, 1797950232},
  {34.6000, -120.4000, -8, 2026, 12, 21, -12.000, 0, 1797904469},
  {34.6000, -120.4000, -8, 2026, 12, 21, -12.000, 1, 1797948339},
  {34.6000, -120.4000, -8, 2026, 12, 21, -18.000, 0, 1797906314},
  {34.6000, -120.4000, -8, 2026, 12, 21, -18.000, 1, 1797946495},
  {51.4779, 0.0000, 0, 2026, 6, 21, -0.833, 0, 1782073252},
  {51.4779, 0.0000, 0, 2026, 6, 21, -0.833, 1, 1782099779},
  {51.4779, 0.0000, 0, 2026, 6, 21, -6.000, 0, 1782076115},
  {51.4779, 0.0000, 0, 2026, 6, 21, -6.000, 1, 1782096917},
  {51.4779, 0.0000, 0, 2026, 6, 21, -12.000, 0, 1782080581},
  {51.4779, 0.0000, 0, 2026, 6, 21, -12.000, 1, 1782092451},
  {51.4779, 0.0000, 0, 2026, 6, 21, -18.000, 0, 0},
  {51.4779, 0.0000, 0, 2026, 6, 21, -18.000, 1, 0},
  {51.4779, 0.0000, 0, 2026, 12, 21, -0.833, 0, 1797868382},
  {51.4779, 0.0000, 0, 2026, 12, 21, -0.833, 1, 1797926615},
  {51.4779, 0.0000, 0, 2026, 12, 21, -6.000, 0, 1797870800},
  {51.4779, 0.0000, 0, 2026, 12, 21, -6.000, 1, 1797924197},
  {51.4779, 0.0000, 0, 2026, 12, 21, -12.000, 0, 1797873390},
  {51.4779, 0.0000, 0, 2026, 12, 21, -12.000, 1, 1797921607},
  {51.4779, 0.0000, 0, 2026, 12, 21, -18.000, 0, 1797875838},
  {51.4779, 0.0000, 0, 2026, 12, 21, -18.000, 1, 1797919159},
  {51.4779, 0.0000, 0, 2026, 3, 20, -0.833, 0, 1774030379},
  {51.4779, 0.0000, 0, 2026, 3, 20, -0.833, 1, 1774072837},
  {51.4779, 0.0000, 0, 2026, 3, 20, -6.000, 0, 1774032379},
  {51.4779, 0.0000, 0, 2026, 3, 20, -6.000, 1, 1774070841},
  {51.4779, 0.0000, 0, 2026, 3, 20, -12.000, 0, 1774034745},
  {51.4779, 0.0000, 0, 2026, 3, 20, -12.000, 1, 1774068480},
  {51.4779, 0.0000, 0, 2026, 3, 20, -18.000, 0, 1774037209},
  {51.4779, 0.0000, 0, 2026, 3, 20, -18.000, 1, 1774066020},
  {-33.8600, 151.2100, 10, 2026, 6, 21, -0.833, 0, 1782024830},
  {-33.8600, 151.2100, 10, 2026, 6, 21, -0.833, 1, 1782075609},
  {-33.8600, 151.2100, 10, 2026, 6, 21, -6.000, 0, 1782026494},
  {-33.8600, 151.2100, 10, 2026, 6, 21, -6.000, 1, 1782073946},
  {-33.8600, 151.2100, 10, 2026, 6, 21, -12.000, 0, 1782028368},
  {-33.8600, 151.2100, 10, 2026, 6, 21, -12.000, 1, 1782072071},
  {-33.8600, 151.2100, 10, 2026, 6, 21, -18.000, 0, 1782030197},
  {-33.8600, 151.2100, 10, 2026, 6, 21, -18.000, 1, 1782070242},
  {-33.8600, 151.2100, 10, 2026, 12, 21, -0.833, 0, 1797843922},
  {-33.8600, 151.2100, 10, 2026, 12, 21, -0.833, 1, 1797878469},
  {-33.8600, 151.2100, 10, 2026, 12, 21, -6.000, 0, 1797845672},
  {-33.8600, 151.2100, 10, 2026, 12, 21, -6.000, 1, 1797876719},
  {-33.8600, 151.2100, 10, 2026, 12, 21, -12.000, 0, 1797847826},
  {-33.8600, 151.2100, 10, 2026, 12, 21, -12.000, 1, 1797874565},
  {-33.8600, 151.2100, 10, 2026, 12, 21, -18.000, 0, 1797850185},
  {-33.8600, 151.2100, 10, 2026, 12, 21, -18.000, 1, 1797872206},
  {69.6500, 18.9600, 1, 2026, 6, 21, -0.833, 0, 0},
  {69.6500, 18.9600, 1, 2026, 6, 21, -0.833, 1, 0},
  {69.6500, 18.9600, 1, 2026, 6, 21, -6.000, 0, 0},
  {69.6500, 18.9600, 1, 2026, 6, 21, -6.000, 1, 0},
  {69.6500, 18.9600, 1, 2026, 6, 21, -12.000, 0, 0},
  {69.6500, 18.9600, 1, 2026, 6, 21, -12.000, 1, 0},
  {69.6500, 18.9600, 1, 2026, 6, 21, -18.000, 0, 0},
  {69.6500, 18.9600, 1, 2026, 6, 21, -18.000, 1, 0},
  {69.6500, 18.9600, 1, 2026, 12, 21, -0.833, 0, 0},
  {69.6500, 18.9600, 1, 2026, 12, 21, -0.833, 1, 0},
  {69.6500, 18.9600, 1, 2026, 12, 21, -6.000, 0, 1797857588},
  {69.6500, 18.9600, 1, 2026, 12, 21, -6.000, 1, 1797928305},
  {69.6500, 18.9600, 1, 2026, 12, 21, -12.000, 0, 1797863861},
  {69.6500, 18.9600, 1, 2026, 12, 21, -12.000, 1, 1797922032},
  {69.6500, 18.9600, 1, 2026, 12, 21, -18.000, 0, 1797868564},
  {69.6500, 18.9600, 1, 2026, 12, 21, -18.000, 1, 1797917329},
  {0.0000, 0.0000, 0, 2026, 3, 20, -0.833, 0, 1774030241},
  {0.0000, 0.0000, 0, 2026, 3, 20, -0.833, 1, 1774073033},
  {0.0000, 0.0000, 0, 2026, 3, 20, -6.000, 0, 1774031481},
  {0.0000, 0.0000, 0, 2026, 3, 20, -6.000, 1, 1774071794},
  {0.0000, 0.0000, 0, 2026, 3, 20, -12.000, 0, 1774032920},
  {0.0000, 0.0000, 0, 2026, 3, 20, -12.000, 1, 1774070354},
  {0.0000, 0.0000, 0, 2026, 3, 20, -18.000, 0, 1774034360},
  {0.0000, 0.0000, 0, 2026, 3, 20, -18.000, 1, 1774068914},
};

void test_solar_matches_skyfield(void) {
  int n = 0, none_ok = 0; long long worst = 0; const Ref *wr = nullptr;
  for (const Ref &r : REFS) {
    int64_t t;
    bool ok = solar::eventUtc(r.lat, r.lon, r.tz * 60, r.y, r.m, r.d, r.alt, r.rising, &t);
    if (r.utc == 0) {
      char msg[96]; snprintf(msg, sizeof msg, "expected no crossing: %.2f %d-%d-%d alt %.1f rising %d", r.lat, r.y, r.m, r.d, r.alt, r.rising);
      TEST_ASSERT_FALSE_MESSAGE(ok, msg);
      none_ok++;
      continue;
    }
    char msg[96]; snprintf(msg, sizeof msg, "missing crossing: %.2f %d-%d-%d alt %.1f rising %d", r.lat, r.y, r.m, r.d, r.alt, r.rising);
    TEST_ASSERT_TRUE_MESSAGE(ok, msg);
    long long err = llabs((long long)t - r.utc);
    // within 2 min everywhere; the high-latitude grazing cases are the hard ones
    long long tol = fabs(r.lat) > 60 ? 600 : 120;
    snprintf(msg, sizeof msg, "%.2f %d-%d-%d alt %.1f rising %d: off by %lld s", r.lat, r.y, r.m, r.d, r.alt, r.rising, err);
    TEST_ASSERT_TRUE_MESSAGE(err <= tol, msg);
    if (fabs(r.lat) < 60 && err > worst) { worst = err; wr = &r; }
    n++;
  }
  printf("solar: %d crossings checked, %d no-crossing cases, worst mid-latitude error %lld s (%.2f %d-%d-%d alt %.1f)\n",
         n, none_ok, worst, wr ? wr->lat : 0, wr ? wr->y : 0, wr ? wr->m : 0, wr ? wr->d : 0, wr ? wr->alt : 0);
}

// ---- schedule -----------------------------------------------------------------
static const int64_t T0 = 1795305600;            // 2026-11-22 00:00Z
static ScheduleLimits LIM;

static std::string doc(const char *site, const char *defaults, const char *nights, bool repeat) {
  char buf[4096];
  snprintf(buf, sizeof buf,
           "{\"version\":1,\"site\":%s,\"defaults\":%s,\"nights\":[%s],\"repeat_after_list\":%s}",
           site, defaults, nights, repeat ? "true" : "false");
  return buf;
}
static const char *SITE = "{\"lat\":34.6,\"lon\":-120.4,\"tz_offset_min\":-480}";
static const char *DEFS =
    "{\"start\":{\"event\":\"dusk_astronomical\",\"offset_min\":10},"
    "\"stop\":{\"event\":\"dawn_astronomical\",\"offset_min\":-10},"
    "\"start_angle_deg\":0.0,\"camera\":\"nightlapse_30s_iso800\",\"camera_policy\":\"best_effort\"}";

static bool parse(const std::string &j, ScheduleDoc *d, Messages *e, Messages *w = nullptr) {
  return parseSchedule(j.c_str(), j.size(), LIM, T0, d, e, w);
}
static bool has(const Messages &m, const char *needle) {
  for (int i = 0; i < m.n; i++) if (strstr(m.msg[i], needle)) return true;
  return false;
}
static void dump(const Messages &m) { for (int i = 0; i < m.n; i++) printf("    %s\n", m.msg[i]); }
static int32_t D(const char *s) { int32_t d; parseDate(s, &d); return d; }

void test_resolves_a_night_from_twilight(void) {
  static ScheduleDoc d; Messages e;
  bool ok = parse(doc(SITE, DEFS, "{\"date\":\"2026-11-22\"}", false), &d, &e);
  dump(e);
  TEST_ASSERT_TRUE(ok);
  Schedule s(d, LIM);
  NightPlan p = s.next(T0);
  TEST_ASSERT_TRUE(p.valid);
  TEST_ASSERT_EQUAL_INT32(D("2026-11-22"), p.day);
  // Skyfield: astronomical dusk 1795400472, dawn 1795439696 at this site/date
  TEST_ASSERT_INT64_WITHIN(60, 1795400472 + 600, p.start_utc);
  TEST_ASSERT_INT64_WITHIN(60, 1795439696 - 600, p.stop_utc);
  TEST_ASSERT_EQUAL_STRING("nightlapse_30s_iso800", p.camera);
  TEST_ASSERT_EQUAL(CamPolicy::BestEffort, p.cam);
  TEST_ASSERT_EQUAL(0, p.fallback);
  TEST_ASSERT_FALSE(s.next(p.stop_utc).valid);          // no repeat: that was the only night
}

void test_overrides_skip_and_repeat(void) {
  static ScheduleDoc d; Messages e;
  std::string j = doc(SITE, DEFS,
      "{\"date\":\"2026-11-22\",\"camera_policy\":\"none\",\"start_angle_deg\":3.5},"
      "{\"date\":\"2026-11-23\",\"start\":{\"utc\":\"2026-11-24T04:15:00Z\"}},"
      "{\"date\":\"2026-11-24\",\"skip\":true}", true);
  TEST_ASSERT_TRUE(parse(j, &d, &e));
  Schedule s(d, LIM);
  NightPlan a = s.next(T0);
  TEST_ASSERT_EQUAL_INT32(D("2026-11-22"), a.day);
  TEST_ASSERT_EQUAL(CamPolicy::None, a.cam);
  TEST_ASSERT_DOUBLE_WITHIN(1e-9, 3.5, a.start_deg);
  NightPlan b = s.next(a.stop_utc);
  TEST_ASSERT_EQUAL_INT32(D("2026-11-23"), b.day);
  int64_t want; parseUtc("2026-11-24T04:15:00Z", &want);
  TEST_ASSERT_EQUAL_INT64(want, b.start_utc);
  TEST_ASSERT_DOUBLE_WITHIN(1e-9, 0.0, b.start_deg);           // defaults where not overridden
  NightPlan c = s.next(b.stop_utc);
  TEST_ASSERT_EQUAL_INT32(D("2026-11-25"), c.day);              // the 24th is skipped
  NightPlan e2 = s.next(c.stop_utc);
  TEST_ASSERT_EQUAL_INT32(D("2026-11-26"), e2.day);             // repeat_after_list keeps going
  // a night already in progress is still "next"
  NightPlan mid = s.next(c.start_utc + 3600);
  TEST_ASSERT_EQUAL_INT32(c.day, mid.day);
}

void test_high_latitude_falls_back_then_skips(void) {
  static ScheduleDoc d; Messages e, w;
  // Greenwich at midsummer: no astronomical darkness -> nautical (1 level)
  std::string j = doc("{\"lat\":51.4779,\"lon\":0.0,\"tz_offset_min\":0}", DEFS, "{\"date\":\"2026-06-21\"}", false);
  TEST_ASSERT_TRUE(parse(j, &d, &e, &w));
  TEST_ASSERT_TRUE(has(w, "falls back 1 level"));
  Schedule s(d, LIM);
  NightPlan p = s.next(1781913600);                             // 2026-06-20 00:00Z
  TEST_ASSERT_TRUE(p.valid);
  TEST_ASSERT_EQUAL(1, p.fallback);
  // Skyfield nautical dusk/dawn at Greenwich 2026-06-21
  int64_t dusk = 0, dawn = 0;
  for (const Ref &r : REFS)
    if (r.lat > 51 && r.lat < 52 && r.m == 6 && r.alt == -12.0) { if (r.rising) dawn = r.utc; else dusk = r.utc; }
  TEST_ASSERT_INT64_WITHIN(60, dusk + 600, p.start_utc);
  TEST_ASSERT_INT64_WITHIN(60, dawn - 600, p.stop_utc);

  // Tromso at midsummer: not even civil twilight -> skipped, with a warning not an error.
  // (repeat_after_list is refused there: its polar-winter nights run ~19 h.)
  static ScheduleDoc d2; Messages e2, w2;
  std::string j2 = doc("{\"lat\":69.65,\"lon\":18.96,\"tz_offset_min\":60}", DEFS,
                       "{\"date\":\"2026-06-21\"},{\"date\":\"2026-08-20\"}", false);
  bool ok2 = parse(j2, &d2, &e2, &w2);
  dump(e2);
  TEST_ASSERT_TRUE(ok2);
  TEST_ASSERT_TRUE(has(w2, "nights[0] (2026-06-21): no darkness"));
  Schedule s2(d2, LIM);
  NightPlan q = s2.next(1781913600);
  TEST_ASSERT_EQUAL_INT32(D("2026-08-20"), q.day);              // midsummer passed over
  TEST_ASSERT_EQUAL(2, q.fallback);                             // only civil twilight in August
  static ScheduleDoc d3; Messages e3;
  TEST_ASSERT_FALSE(parse(doc("{\"lat\":69.65,\"lon\":18.96,\"tz_offset_min\":60}", DEFS, "", true), &d3, &e3));
  TEST_ASSERT_TRUE(has(e3, "defaults: the night of"));
}

void test_validation_collects_every_error(void) {
  static ScheduleDoc d; Messages e;
  std::string j = doc("{\"lat\":95,\"lon\":-120.4,\"tz_offset_min\":-480}",
      "{\"start\":{\"event\":\"dawn_civil\",\"offset_min\":200},"
      "\"stop\":{\"event\":\"dusk_astro\"},"
      "\"start_angle_deg\":0.0,\"camera\":\"nope\",\"camera_policy\":\"maybe\"}",
      "{\"date\":\"2026-02-30\"},{\"date\":\"2026-11-23\"},{\"date\":\"2026-11-22\"}", false);
  static const char *const CAMS[] = {"nightlapse_30s_iso800"};
  ScheduleLimits lim = LIM; lim.cameras = CAMS; lim.n_cameras = 1;
  TEST_ASSERT_FALSE(parseSchedule(j.c_str(), j.size(), lim, T0, &d, &e));
  dump(e);
  TEST_ASSERT_TRUE(has(e, "site.lat: 95"));
  TEST_ASSERT_TRUE(has(e, "defaults.start.event: 'dawn_civil' is a morning event"));
  TEST_ASSERT_TRUE(has(e, "defaults.start.offset_min: 200"));
  TEST_ASSERT_TRUE(has(e, "defaults.stop.event: unknown event 'dusk_astro'"));
  TEST_ASSERT_TRUE(has(e, "defaults.camera: unknown camera profile 'nope'"));
  TEST_ASSERT_TRUE(has(e, "defaults.camera_policy"));
  TEST_ASSERT_TRUE(has(e, "nights[0].date: not a real date"));
  TEST_ASSERT_TRUE(has(e, "nights[2].date: dates must be strictly ascending"));
  TEST_ASSERT_TRUE(e.n >= 8);
}

void test_parse_layer_rejects_junk_and_oversize(void) {
  static ScheduleDoc d;
  Messages e1;
  const char *junk = "{\"version\":1, \"site\": {";
  TEST_ASSERT_FALSE(parseSchedule(junk, strlen(junk), LIM, T0, &d, &e1));
  TEST_ASSERT_TRUE(has(e1, "JSON:"));
  Messages e2;
  std::string big(9000, ' ');
  TEST_ASSERT_FALSE(parseSchedule(big.c_str(), big.size(), LIM, T0, &d, &e2));
  TEST_ASSERT_TRUE(has(e2, "the limit is 8192"));
}

void test_semantic_overlap_and_length(void) {
  static ScheduleDoc d; Messages e;
  std::string j = doc(SITE, DEFS,
      "{\"date\":\"2026-11-22\",\"stop\":{\"utc\":\"2026-11-24T05:00:00Z\"}},"     // runs into the next night
      "{\"date\":\"2026-11-23\"},"
      "{\"date\":\"2026-11-25\",\"start\":{\"utc\":\"2026-11-26T03:00:00Z\"},\"stop\":{\"utc\":\"2026-11-26T02:00:00Z\"}}",
      false);
  TEST_ASSERT_FALSE(parse(j, &d, &e));
  dump(e);
  TEST_ASSERT_TRUE(has(e, "nights[0] (2026-11-22):") && has(e, "longer than 16 h"));
  TEST_ASSERT_TRUE(has(e, "nights[1] (2026-11-23): starts before nights[0] ends"));
  TEST_ASSERT_TRUE(has(e, "nights[2] (2026-11-25): stop is not after start"));
}

void test_envelope_layer(void) {
  // a listed night starting at 100 deg runs ~158 deg past it
  static ScheduleDoc d; Messages e;
  TEST_ASSERT_FALSE(parse(doc(SITE, DEFS, "{\"date\":\"2026-11-22\",\"start_angle_deg\":100}", false), &d, &e));
  dump(e);
  TEST_ASSERT_TRUE(has(e, "would track to") && has(e, "past the envelope"));
  // defaults at 60 deg with repeat: fine tonight, but the solstice nights are too long
  static ScheduleDoc d2; Messages e2;
  std::string defs60 = DEFS; defs60.replace(defs60.find("0.0"), 3, "60.0");
  TEST_ASSERT_FALSE(parse(doc(SITE, defs60.c_str(), "", true), &d2, &e2));
  dump(e2);
  TEST_ASSERT_TRUE(has(e2, "defaults: the night of 2026-12-"));
  // at 0 deg the longest default night of the year fits
  static ScheduleDoc d3; Messages e3;
  TEST_ASSERT_TRUE(parse(doc(SITE, DEFS, "", true), &d3, &e3));
}

void test_gps_site_moves_the_times(void) {
  static ScheduleDoc d; Messages e;
  TEST_ASSERT_TRUE(parse(doc(SITE, DEFS, "{\"date\":\"2026-11-22\"}", false), &d, &e));
  Schedule s(d, LIM);
  NightPlan a = s.next(T0);
  s.setSite(34.6, -118.4);                                      // 2 deg further east
  NightPlan b = s.refresh(a);
  TEST_ASSERT_EQUAL_INT32(a.day, b.day);
  TEST_ASSERT_INT64_WITHIN(30, a.start_utc - 480, b.start_utc); // 4 min per degree of longitude
  TEST_ASSERT_INT64_WITHIN(30, a.stop_utc - 480, b.stop_utc);
}

void setUp(void) {}
void tearDown(void) {}

// ---- moon: Skyfield 1.54 / DE421 (scratchpad moon_ref.py), 2026-11-20..12-20 ----
struct MoonSite { const char *name; double lat, lon; long long rise[6], set[6]; };
static const MoonSite MOON_SITES[] = {
  {"lompoc", 34.6, -120.4, {1795212690, 1795300884, 1795389373, 1795478299, 1795567804, 1795657941},
                           {1795169864, 1795260158, 1795350698, 1795441485, 1795532408, 1795623204}},
  {"oslo", 59.91, 10.75,   {1795179495, 1795265963, 1795352512, 1795439235, 1795526363, 1795614550},
                           {1795137377, 1795229338, 1795321691, 1795414531, 1795507729, 1795600526}},
  {"sydney", -33.87, 151.21, {1795146986, 1795237204, 1795327657, 1795418367, 1795509258, 1795600107},
                           {1795190224, 1795278384, 1795366804, 1795455623, 1795544984, 1795634971}},
};

void test_moon_rise_set_match_skyfield(void) {
  long worst = 0;
  for (const MoonSite &s : MOON_SITES)
    for (int i = 0; i < 6; i++) {
      int64_t a, b;
      // a window around the rise: down before it, so the pass starts at the rise
      TEST_ASSERT_TRUE_MESSAGE(moon::upWindow(s.lat, s.lon, s.rise[i] - 3 * 3600, s.rise[i] + 3 * 3600, &a, &b), s.name);
      long e = (long)llabs(a - s.rise[i]);
      if (e > worst) worst = e;
      TEST_ASSERT_TRUE_MESSAGE(e <= 180, s.name);
      TEST_ASSERT_TRUE_MESSAGE(moon::upWindow(s.lat, s.lon, s.set[i] - 3 * 3600, s.set[i] + 3 * 3600, &a, &b), s.name);
      e = (long)llabs(b - s.set[i]);
      if (e > worst) worst = e;
      TEST_ASSERT_TRUE_MESSAGE(e <= 180, s.name);
    }
  printf("moon rise/set: worst %ld s off Skyfield\n", worst);
}

void test_moon_position_and_phase(void) {
  // lompoc: utc, alt, az (topocentric, no refraction), illuminated fraction
  struct P { long long t; double alt, az, illum; } P_[] = {
    {1795400000, 33.823, 89.364, 0.9649}, {1795450000, -22.589, 319.815, 0.985}, {1796000000, -41.85, 356.321, 0.6349}};
  for (const P &p : P_) {
    moon::Pos m = moon::at(p.t, 34.6, -120.4);
    TEST_ASSERT_FLOAT_WITHIN(0.3, p.alt, m.alt_deg);
    TEST_ASSERT_FLOAT_WITHIN(0.4, p.az, m.az_deg);
    TEST_ASSERT_FLOAT_WITHIN(0.01, p.illum, m.illum);
  }
  int64_t a, b;
  TEST_ASSERT_FALSE(moon::upWindow(34.6, -120.4, 1795389373 - 6 * 3600, 1795389373 - 3600, &a, &b));   // down all window
}

void test_moon_nights_clip_to_the_moon_in_the_dark(void) {
  // Lompoc, astronomical dusk..dawn, target moon. Expected windows from Skyfield
  // (scratchpad moon_nights_ref.py): dark window intersected with the moon being up.
  const char *j =
    "{\"version\":1,\"site\":{\"lat\":34.6,\"lon\":-120.4,\"tz_offset_min\":-480},"
    "\"defaults\":{\"start\":{\"event\":\"dusk_astronomical\"},\"stop\":{\"event\":\"dawn_astronomical\"},"
    "\"start_angle_deg\":0,\"camera\":\"timelapse_20s_1m\",\"camera_policy\":\"best_effort\",\"target\":\"moon\"},"
    "\"nights\":[{\"date\":\"2026-11-22\"},{\"date\":\"2026-11-28\"},{\"date\":\"2026-12-06\"},"
    "{\"date\":\"2026-12-13\"},{\"date\":\"2026-12-14\",\"target\":\"sky\"}],\"repeat_after_list\":false}";
  static ScheduleDoc d; Messages err, warn;
  TEST_ASSERT_TRUE_MESSAGE(parseSchedule(j, strlen(j), ScheduleLimits(), 1795300000, &d, &err, &warn), err.n ? err.msg[0] : "");
  TEST_ASSERT_EQUAL(1, warn.n);                                    // 12-06: no moon -> skipped, warned
  TEST_ASSERT_TRUE(strstr(warn.msg[0], "2026-12-06") && strstr(warn.msg[0], "moon is not up"));
  Schedule s(d, ScheduleLimits());
  struct W { const char *date; long long a, b; } W_[] = {
    {"2026-11-22", 1795400472, 1795439696},      // full moon: the whole dark window
    {"2026-11-28", 1795930353, 1795958383},      // rises in the dark
    {"2026-12-13", 1797214916, 1797224811},      // sets in the dark
  };
  for (const W &w : W_) {
    int32_t day; parseDate(w.date, &day);
    NightPlan p; const char *why = "";
    TEST_ASSERT_TRUE_MESSAGE(s.resolve(day, &p, &why), why);
    TEST_ASSERT_TRUE(p.moon);
    TEST_ASSERT_EQUAL((int)TrackRate::Lunar, (int)p.rate);
    TEST_ASSERT_INT64_WITHIN(180, w.a, p.start_utc);
    TEST_ASSERT_INT64_WITHIN(180, w.b, p.stop_utc);
  }
  int32_t day; parseDate("2026-12-06", &day);
  NightPlan p; const char *why = "";
  TEST_ASSERT_FALSE(s.resolve(day, &p, &why));
  TEST_ASSERT_EQUAL_STRING("the moon is not up in the window", why);
  parseDate("2026-12-14", &day);                    // a sky night in a moon schedule: unclipped, sidereal
  TEST_ASSERT_TRUE(s.resolve(day, &p, &why));
  TEST_ASSERT_FALSE(p.moon);
  TEST_ASSERT_EQUAL((int)TrackRate::Sidereal, (int)p.rate);
  TEST_ASSERT_INT64_WITHIN(180, 1797301336, p.start_utc);
  // the lunar rate: a 10.9 h full-moon night ends at ~158 deg, not the ~164 sidereal would give
  parseDate("2026-11-22", &day);
  s.resolve(day, &p, &why);
  double end = p.start_deg + (p.stop_utc - p.start_utc) * rateDegPerS(p.rate);
  TEST_ASSERT_FLOAT_WITHIN(0.5, 157.9, end);
  // a bad target is named
  const char *bad = "{\"version\":1,\"site\":{\"lat\":34.6,\"lon\":-120.4,\"tz_offset_min\":-480},"
    "\"defaults\":{\"start\":{\"event\":\"dusk_astronomical\"},\"stop\":{\"event\":\"dawn_astronomical\"},"
    "\"start_angle_deg\":0,\"camera\":\"x\",\"camera_policy\":\"none\",\"target\":\"sun\"}}";
  Messages e2;
  TEST_ASSERT_FALSE(parseSchedule(bad, strlen(bad), ScheduleLimits(), 1795300000, &d, &e2));
  TEST_ASSERT_EQUAL_STRING("defaults.target: must be sky or moon", e2.msg[0]);
}

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_moon_nights_clip_to_the_moon_in_the_dark);
  RUN_TEST(test_moon_rise_set_match_skyfield);
  RUN_TEST(test_moon_position_and_phase);
  RUN_TEST(test_solar_matches_skyfield);
  RUN_TEST(test_resolves_a_night_from_twilight);
  RUN_TEST(test_overrides_skip_and_repeat);
  RUN_TEST(test_high_latitude_falls_back_then_skips);
  RUN_TEST(test_validation_collects_every_error);
  RUN_TEST(test_parse_layer_rejects_junk_and_oversize);
  RUN_TEST(test_semantic_overlap_and_length);
  RUN_TEST(test_envelope_layer);
  RUN_TEST(test_gps_site_moves_the_times);
  return UNITY_END();
}
