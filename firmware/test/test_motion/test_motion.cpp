// Host tests for lib/motion. Run:  pio test -e native
#include <unity.h>
#include "StepEngine.h"

using namespace motion;

static Config cfg() {
  Config c;
  c.min_units = degToUnits(-2.0);
  c.max_units = degToUnits(182.0);
  return c;
}

// drive the engine like the ISR does; returns ticks used
static uint64_t now_us = 0;
static uint64_t run(StepEngine &e, uint64_t max_ticks, uint32_t *peak_hz = nullptr,
                    int *dir_changes = nullptr, bool *last_fwd = nullptr) {
  const uint64_t dt = 1000000ULL / e.config().tick_hz;
  uint64_t t = 0; int changes = 0; bool have = false, prev = true;
  uint32_t peak = 0;
  for (; t < max_ticks && e.mode() != Mode::Idle; t++) {
    now_us += dt;
    Pulse p = e.tick(now_us);
    if (p.step) {
      if (have && p.forward != prev) changes++;
      prev = p.forward; have = true;
    }
    if (e.slewHz() > peak) peak = e.slewHz();
  }
  if (peak_hz) *peak_hz = peak;
  if (dir_changes) *dir_changes = changes;
  if (last_fwd) *last_fwd = prev;
  return t;
}

void test_sidereal_exact(void) {
  TEST_ASSERT_EQUAL_INT64(USTEPS_PER_REV, StepEngine::siderealUnits(SIDEREAL_US));
  TEST_ASSERT_EQUAL_INT64(2 * USTEPS_PER_REV, StepEngine::siderealUnits(2 * SIDEREAL_US));
  TEST_ASSERT_EQUAL_INT64(USTEPS_PER_REV / 2, StepEngine::siderealUnits(SIDEREAL_US / 2));
  // 12 h: 180.5 deg of output, the travel the envelope is sized for
  double deg12 = unitsToDeg(StepEngine::siderealUnits(12ULL * 3600 * 1000000));
  TEST_ASSERT_DOUBLE_WITHIN(0.01, 180.49, deg12);
  // rate multiplier is exact too
  TEST_ASSERT_EQUAL_INT64(USTEPS_PER_REV, StepEngine::siderealUnits(SIDEREAL_US / 4, 4));
}

void test_forward_move_arrives_exactly(void) {
  StepEngine e(cfg());
  e.setPosition(0);
  TEST_ASSERT_TRUE(e.moveToForward(degToUnits(10.0) & ~3LL));
  int changes; bool fwd; uint32_t peak;
  run(e, 100000000ULL, &peak, &changes, &fwd);
  TEST_ASSERT_EQUAL(Mode::Idle, e.mode());
  TEST_ASSERT_EQUAL(Stop::Arrived, e.lastStop());
  TEST_ASSERT_EQUAL_INT64(degToUnits(10.0) & ~3LL, e.position());
  TEST_ASSERT_TRUE(fwd);
  TEST_ASSERT_TRUE(e.forwardLoaded());
  TEST_ASSERT_TRUE(peak <= e.config().slew_max_hz);
  TEST_ASSERT_TRUE(peak >= e.config().slew_max_hz - 50);    // long move reaches cruise
}

void test_boot_move_prerolls_for_backlash(void) {
  // after setPosition the lash state is unknown: even a short forward move
  // must back off and come forward so it arrives with the lash taken up
  StepEngine e(cfg());
  e.setPosition(100000);
  TEST_ASSERT_TRUE(e.moveToForward(100400));
  int changes; bool fwd;
  run(e, 10000000ULL, nullptr, &changes, &fwd);
  TEST_ASSERT_EQUAL_INT64(100400, e.position());
  TEST_ASSERT_EQUAL(1, changes);
  TEST_ASSERT_TRUE(fwd);
}

void test_reverse_move_overshoots_and_arrives_forward(void) {
  StepEngine e(cfg());
  e.setPosition(0);
  e.moveToForward(degToUnits(20.0) & ~3LL);
  run(e, 1000000000ULL);
  uint64_t p0 = e.pulses();
  TEST_ASSERT_TRUE(e.moveToForward(degToUnits(5.0) & ~3LL));     // rewind
  int changes; bool fwd;
  run(e, 1000000000ULL, nullptr, &changes, &fwd);
  TEST_ASSERT_EQUAL_INT64(degToUnits(5.0) & ~3LL, e.position());
  TEST_ASSERT_EQUAL(1, changes);             // reverse past it, then forward once
  TEST_ASSERT_TRUE(fwd);
  // pulses = 15 deg back + 2 x backlash, all in pulse units
  int64_t expect = ((degToUnits(20.0) & ~3LL) - (degToUnits(5.0) & ~3LL) + 2 * e.config().backlash_units) / 4;
  TEST_ASSERT_EQUAL_INT64(expect, (int64_t)(e.pulses() - p0));
}

void test_envelope_refuses(void) {
  StepEngine e(cfg());
  e.setPosition(0);
  TEST_ASSERT_FALSE(e.moveToForward(degToUnits(183.0)));
  TEST_ASSERT_FALSE(e.moveToForward(degToUnits(-3.0)));
  // inside the envelope but the overshoot point would leave it
  TEST_ASSERT_FALSE(e.moveToForward(degToUnits(-1.9)));
  TEST_ASSERT_EQUAL(Mode::Idle, e.mode());
}

void test_tracking_needs_forward_arrival(void) {
  StepEngine e(cfg());
  e.setPosition(0);
  TEST_ASSERT_FALSE(e.startTracking(now_us));        // lash unknown
  e.moveToForward(0);
  run(e, 100000000ULL);
  TEST_ASSERT_TRUE(e.startTracking(now_us));
}

void test_tracking_follows_sidereal_and_heals_stalls(void) {
  StepEngine e(cfg());
  e.setPosition(0);
  e.moveToForward(0);
  run(e, 100000000ULL);
  uint64_t t0 = now_us;
  TEST_ASSERT_TRUE(e.startTracking(t0));
  const uint64_t dt = 50;   // 20 kHz
  int64_t worst = 0;
  // 10 minutes, with a 200 ms stall (WiFi.begin) at 5 minutes
  for (uint64_t t = dt; t <= 600ULL * 1000000; t += dt) {
    if (t > 300ULL * 1000000 && t < 300ULL * 1000000 + 200000) continue;   // ISR starved
    now_us = t0 + t;
    e.tick(now_us);
    if (t > 301ULL * 1000000 || t < 300ULL * 1000000) {
      int64_t err = StepEngine::siderealUnits(t) - e.position();
      if (err > worst) worst = err;
      TEST_ASSERT_TRUE(err >= 0);
    }
  }
  // never more than one pulse behind outside the stall's catch-up second
  TEST_ASSERT_TRUE(worst < e.config().pulse_units);
  TEST_ASSERT_EQUAL(Mode::Track, e.mode());
}

void test_tracking_catchup_is_rate_capped(void) {
  StepEngine e(cfg());
  e.setPosition(0);
  e.moveToForward(0);
  run(e, 100000000ULL);
  uint64_t t0 = now_us;
  e.startTracking(t0);
  // resume-style jump: 60 s with no ticks, then count pulses in the next 100 ms
  now_us = t0 + 60ULL * 1000000;
  uint64_t p0 = e.pulses();
  for (int i = 0; i < 2000; i++) { now_us += 50; e.tick(now_us); }
  uint64_t n = e.pulses() - p0;
  // integer tick gap: within 10% of the cap, never far above it
  TEST_ASSERT_TRUE(n <= e.config().track_catchup_hz / 10 * 11 / 10);
  TEST_ASSERT_TRUE(n >= e.config().track_catchup_hz / 10 * 9 / 10);
}

void test_tracking_stops_at_envelope(void) {
  Config c = cfg();
  c.max_units = degToUnits(1.0);
  StepEngine e(c);
  e.setPosition(0);
  e.moveToForward(0);
  run(e, 100000000ULL);
  e.startTracking(now_us, 1000);       // 1000x: 1 deg in ~0.24 s
  run(e, 20000ULL * 10);
  TEST_ASSERT_EQUAL(Mode::Idle, e.mode());
  TEST_ASSERT_EQUAL(Stop::Limit, e.lastStop());
  TEST_ASSERT_TRUE(e.position() <= c.max_units);
}

void test_canonical_units_with_1_32(void) {
  // the same move at 1/32 takes 4x the pulses and lands on the same unit
  Config c = cfg();
  c.pulse_units = 1;
  StepEngine e(c);
  e.setPosition(0);
  e.moveToForward(0);
  run(e, 100000000ULL);
  uint64_t p0 = e.pulses();
  e.moveToForward(40000);
  run(e, 100000000ULL);
  TEST_ASSERT_EQUAL_INT64(40000, e.position());
  TEST_ASSERT_EQUAL_UINT64(40000, e.pulses() - p0);
}

void test_stop_is_immediate(void) {
  StepEngine e(cfg());
  e.setPosition(0);
  e.moveToForward(degToUnits(30.0));
  for (int i = 0; i < 1000; i++) { now_us += 50; e.tick(now_us); }
  e.stop();
  int64_t p = e.position();
  for (int i = 0; i < 1000; i++) { now_us += 50; TEST_ASSERT_FALSE(e.tick(now_us).step); }
  TEST_ASSERT_EQUAL_INT64(p, e.position());
  TEST_ASSERT_EQUAL(Stop::Commanded, e.lastStop());
}

void test_tracking_from_past_origin_catches_up(void) {
  // resume: the night started 5 min ago at 0; we are at 0.5 deg after a slew
  StepEngine e(cfg());
  e.setPosition(degToUnits(0.5));
  e.moveToForward(degToUnits(0.5) & ~3LL);
  run(e, 100000000ULL);
  uint64_t t0 = now_us - 300ULL * 1000000;
  TEST_ASSERT_TRUE(e.startTrackingAt(0, t0, now_us));
  // 5 min of sidereal = 1.2534 deg, so the engine is ~0.75 deg behind; at the
  // 1600 p/s cap (6400 units/s) that is ~4.3 s of catch-up
  for (int i = 0; i < 20000 * 10; i++) { now_us += 50; e.tick(now_us); }
  int64_t want = StepEngine::siderealUnits(now_us - t0);
  TEST_ASSERT_TRUE(want - e.position() >= 0 && want - e.position() < 4);
  TEST_ASSERT_FALSE(e.startTrackingAt(0, now_us + 1, now_us));   // origin in the future
}

void setUp(void) {}
void tearDown(void) {}

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_sidereal_exact);
  RUN_TEST(test_forward_move_arrives_exactly);
  RUN_TEST(test_boot_move_prerolls_for_backlash);
  RUN_TEST(test_reverse_move_overshoots_and_arrives_forward);
  RUN_TEST(test_envelope_refuses);
  RUN_TEST(test_tracking_needs_forward_arrival);
  RUN_TEST(test_tracking_follows_sidereal_and_heals_stalls);
  RUN_TEST(test_tracking_catchup_is_rate_capped);
  RUN_TEST(test_tracking_stops_at_envelope);
  RUN_TEST(test_canonical_units_with_1_32);
  RUN_TEST(test_stop_is_immediate);
  RUN_TEST(test_tracking_from_past_origin_catches_up);
  return UNITY_END();
}
