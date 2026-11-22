// Host tests for the pure parts of lib/hal. Run:  pio test -e native
#include <unity.h>
#include <math.h>
#include "CivilTime.h"
#include "EncoderMap.h"
#include "Nmea.h"
#include <stdio.h>

void test_civil_epoch_and_known_dates(void) {
  TEST_ASSERT_EQUAL_INT64(0, toUnix({1970, 1, 1, 0, 0, 0}));
  TEST_ASSERT_EQUAL_INT64(951782400, toUnix({2000, 2, 29, 0, 0, 0}));     // leap day
  TEST_ASSERT_EQUAL_INT64(1795219200, toUnix({2026, 11, 21, 0, 0, 0}));
  TEST_ASSERT_EQUAL_INT64(4102444800, toUnix({2100, 1, 1, 0, 0, 0}));     // 2100 not leap
}

void test_civil_round_trip(void) {
  // every 37 h 13 min 7 s for ~80 years: crosses every month end, leap day, year end
  for (int64_t t = 946684800; t < 3471292800; t += 133987) {
    Civil c = fromUnix(t);
    TEST_ASSERT_EQUAL_INT64(t, toUnix(c));
    TEST_ASSERT_TRUE(c.month >= 1 && c.month <= 12 && c.day >= 1 && c.day <= 31);
  }
  Civil c = fromUnix(1795219200 + 23 * 3600 + 59 * 60 + 59);
  TEST_ASSERT_EQUAL(2026, c.year); TEST_ASSERT_EQUAL(11, c.month); TEST_ASSERT_EQUAL(21, c.day);
  TEST_ASSERT_EQUAL(23, c.hour);   TEST_ASSERT_EQUAL(59, c.minute); TEST_ASSERT_EQUAL(59, c.second);
}

// a fake eccentric magnet: raw = start + true_counts + 25*sin(angle)
static uint16_t fakeRaw(double deg) {
  double r = 1029 + deg * 4096.0 / 360.0 + 25.0 * sin(deg * M_PI / 180.0);
  long v = lround(r);
  return (uint16_t)(((v % 4096) + 4096) % 4096);
}

static EncoderMap sweep(double from, double to, double step) {
  EncoderMap m;
  m.magic = EncoderMap::MAGIC;
  m.first_units = (int32_t)lround(from * 13107200.0 / 360.0);
  m.step_units = (int32_t)lround(step * 13107200.0 / 360.0);
  for (double d = from; d <= to + 1e-9; d += step) m.add(fakeRaw(d));
  return m;
}

void test_map_corrects_eccentricity(void) {
  EncoderMap m = sweep(-2, 182, 1);
  TEST_ASSERT_TRUE(m.valid());
  double worst_map = 0, worst_line = 0;
  for (double d = -1.5; d < 181.5; d += 0.37) {
    int64_t u;
    TEST_ASSERT_TRUE(m.toUnits(fakeRaw(d), &u));
    double got = u * 360.0 / 13107200.0;
    worst_map = fmax(worst_map, fabs(got - d));
    double line = (fakeRaw(d) - 1029) * 360.0 / 4096.0;
    worst_line = fmax(worst_line, fabs(line - d));
  }
  TEST_ASSERT_TRUE(worst_map < 0.1);       // ~1 count
  TEST_ASSERT_TRUE(worst_line > 1.5);      // the straight line is badly off
}

void test_map_unwraps_across_4095(void) {
  // start near the top of the range so the sweep crosses 4095 -> 0
  EncoderMap m;
  m.magic = EncoderMap::MAGIC;
  m.first_units = 0;
  m.step_units = 36409;
  for (int k = 0; k < 40; k++) m.add((uint16_t)((4000 + k * 11) % 4096));
  int64_t u;
  TEST_ASSERT_TRUE(m.toUnits(4000, &u));  TEST_ASSERT_EQUAL_INT64(0, u);
  TEST_ASSERT_TRUE(m.toUnits(4095 % 4096, &u));
  TEST_ASSERT_TRUE(m.toUnits(10, &u));     // past the wrap
  TEST_ASSERT_TRUE(u > 9 * 36409 && u < 10 * 36409);
  TEST_ASSERT_FALSE(m.toUnits(1000, &u));  // outside the mapped arc
}

void test_map_extrapolates_just_past_its_ends(void) {
  EncoderMap m = sweep(-1.5, 181.5, 1);            // as calibrated on the board
  int64_t u;
  TEST_ASSERT_TRUE(m.toUnits(fakeRaw(-2.2), &u));   // a rewind overshoot below the sweep
  TEST_ASSERT_DOUBLE_WITHIN(0.15, -2.2, u * 360.0 / 13107200.0);
  TEST_ASSERT_TRUE(m.toUnits(fakeRaw(183.0), &u));
  TEST_ASSERT_DOUBLE_WITHIN(0.15, 183.0, u * 360.0 / 13107200.0);
  TEST_ASSERT_FALSE(m.toUnits(fakeRaw(-9), &u));    // far outside: no guess
  TEST_ASSERT_FALSE(m.toUnits(fakeRaw(190), &u));
}

void test_map_invalid_until_filled(void) {
  EncoderMap m;
  int64_t u;
  TEST_ASSERT_FALSE(m.toUnits(1029, &u));
}

static void feed(Nmea &g, const char *s) { while (*s) g.feed(*s++); g.feed('\r'); g.feed('\n'); }

void test_nmea_rmc_gga(void) {
  Nmea g;
  // the classic NMEA reference sentences (real checksums)
  feed(g, "$GPRMC,123519,A,4807.038,N,01131.000,E,022.4,084.4,230394,003.1,W*6A");
  feed(g, "$GPGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,*47");
  const Nmea::Fix &f = g.fix();
  TEST_ASSERT_TRUE(f.rmc_ok);
  TEST_ASSERT_EQUAL_INT64(toUnix({1994, 3, 23, 12, 35, 19}), f.utc);
  TEST_ASSERT_DOUBLE_WITHIN(1e-6, 48 + 7.038 / 60, f.lat);
  TEST_ASSERT_DOUBLE_WITHIN(1e-6, 11 + 31.0 / 60, f.lon);
  TEST_ASSERT_EQUAL(1, f.quality);
  TEST_ASSERT_EQUAL(8, f.sats);
  TEST_ASSERT_DOUBLE_WITHIN(1e-9, 0.9, f.hdop);
  TEST_ASSERT_TRUE(g.usable());
}

void test_nmea_rejects_bad_checksum_and_void_fix(void) {
  Nmea g;
  feed(g, "$GPRMC,123519,A,4807.038,N,01131.000,E,022.4,084.4,230394,003.1,W*6B");   // one off
  TEST_ASSERT_EQUAL(1, (int)g.fix().bad_checksum);
  TEST_ASSERT_FALSE(g.fix().rmc_ok);
  // status V (no fix) from a GPS still searching; checksum computed for it
  const char *body = "GPRMC,031500.00,V,,,,,,,221126,,,N";
  uint8_t sum = 0; for (const char *p = body; *p; p++) sum ^= (uint8_t)*p;
  char line[96]; snprintf(line, sizeof line, "$%s*%02X", body, sum);
  feed(g, line);
  TEST_ASSERT_FALSE(g.fix().rmc_ok);
  TEST_ASSERT_FALSE(g.usable());
  // west / south hemispheres, GN talker
  const char *b2 = "GNRMC,031500.00,A,3436.000,N,12024.000,W,0.0,0.0,221126,,,A";
  sum = 0; for (const char *p = b2; *p; p++) sum ^= (uint8_t)*p;
  snprintf(line, sizeof line, "$%s*%02X", b2, sum);
  feed(g, line);
  TEST_ASSERT_TRUE(g.fix().rmc_ok);
  TEST_ASSERT_DOUBLE_WITHIN(1e-6, 34.6, g.fix().lat);
  TEST_ASSERT_DOUBLE_WITHIN(1e-6, -120.4, g.fix().lon);
  TEST_ASSERT_EQUAL_INT64(toUnix({2026, 11, 22, 3, 15, 0}), g.fix().utc);
  TEST_ASSERT_FALSE(g.usable());                     // no GGA yet: quality unknown
}

void setUp(void) {}
void tearDown(void) {}

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_civil_epoch_and_known_dates);
  RUN_TEST(test_civil_round_trip);
  RUN_TEST(test_map_corrects_eccentricity);
  RUN_TEST(test_map_unwraps_across_4095);
  RUN_TEST(test_map_extrapolates_just_past_its_ends);
  RUN_TEST(test_map_invalid_until_filled);
  RUN_TEST(test_nmea_rmc_gga);
  RUN_TEST(test_nmea_rejects_bad_checksum_and_void_fix);
  return UNITY_END();
}
