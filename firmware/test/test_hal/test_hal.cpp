// Host tests for the pure parts of lib/hal. Run:  pio test -e native
#include <unity.h>
#include <math.h>
#include "CivilTime.h"
#include "EncoderMap.h"

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

void test_map_invalid_until_filled(void) {
  EncoderMap m;
  int64_t u;
  TEST_ASSERT_FALSE(m.toUnits(1029, &u));
}

void setUp(void) {}
void tearDown(void) {}

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_civil_epoch_and_known_dates);
  RUN_TEST(test_civil_round_trip);
  RUN_TEST(test_map_corrects_eccentricity);
  RUN_TEST(test_map_unwraps_across_4095);
  RUN_TEST(test_map_invalid_until_filled);
  return UNITY_END();
}
