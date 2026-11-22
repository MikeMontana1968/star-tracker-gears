// Night-FSM scenarios on the host simulator. Run:  pio test -e native
// Every night here sleeps, wakes by alarm and reboots, so resume paths run
// on every test, not just the brownout one.
#include <unity.h>
#include "Sim.h"

static const int64_t T0 = 1795305600;            // 2026-11-22 00:00:00Z
static const int64_t H = 3600;

static NightPlan night(int64_t start, int64_t stop, double deg = 5.0,
                       CamPolicy cam = CamPolicy::BestEffort, GpsPolicy gps = GpsPolicy::BestEffort) {
  NightPlan n; n.valid = true; n.start_utc = start; n.stop_utc = stop;
  n.start_deg = deg; n.cam = cam; n.gps = gps;
  return n;
}

static void start(Sim &s, int64_t at = T0) {
  s.w.t_ms = at * 1000;
  s.boot(Wake::PowerOn);
}

static double expected(const NightPlan &n, int64_t utc) { return n.start_deg + (utc - n.start_utc) * SID_DEG_S; }

// ------------------------------------------------------------------------
void test_one_good_night(void) {
  Sim s;
  NightPlan n = night(T0 + 3 * H, T0 + 13 * H);
  s.w.nights.push_back(n);
  start(s);
  s.run(T0 + 2 * H);
  TEST_ASSERT_TRUE(s.w.sleeping);                                   // asleep until start - lead
  TEST_ASSERT_EQUAL_INT64(n.start_utc - 600, s.w.wake_utc);
  s.run(T0 + 8 * H);                                                // mid-track
  TEST_ASSERT_EQUAL(State::Track, s.fsm->state());
  TEST_ASSERT_DOUBLE_WITHIN(0.01, expected(n, T0 + 8 * H), s.w.step_deg);
  s.run(T0 + 14 * H);
  TEST_ASSERT_EQUAL(1, (int)s.w.reports.size());
  const NightReport &r = s.w.reports[0];
  TEST_ASSERT_EQUAL(Outcome::Completed, r.outcome);
  TEST_ASSERT_EQUAL(Op::Ok, r.camera);
  TEST_ASSERT_EQUAL(Op::Ok, r.gps);
  TEST_ASSERT_INT64_WITHIN(2, n.start_utc, r.track_start_utc);
  TEST_ASSERT_INT64_WITHIN(2, n.stop_utc, r.track_stop_utc);
  TEST_ASSERT_TRUE(r.max_div_deg < 0.01);
  TEST_ASSERT_EQUAL(0, r.warnings);
  TEST_ASSERT_DOUBLE_WITHIN(0.01, 0.0, s.w.true_deg);              // rewound and parked
  TEST_ASSERT_TRUE(s.w.sleeping);
  TEST_ASSERT_FALSE(s.w.cam.powered);
  TEST_ASSERT_FALSE(s.w.gps_on);
}

void test_three_nights_in_a_row(void) {
  Sim s;
  for (int d = 0; d < 3; d++) s.w.nights.push_back(night(T0 + d * 24 * H + 3 * H, T0 + d * 24 * H + 13 * H));
  start(s);
  s.run(T0 + 4 * 24 * H);
  TEST_ASSERT_EQUAL(3, (int)s.w.reports.size());
  for (auto &r : s.w.reports) TEST_ASSERT_EQUAL(Outcome::Completed, r.outcome);
  TEST_ASSERT_TRUE(s.boots >= 4);
  TEST_ASSERT_TRUE(s.w.sleeping);
  TEST_ASSERT_TRUE(s.w.wake_utc > T0 + 3 * 24 * H);                // nothing left: periodic recheck
}

void test_camera_fails_at_every_stage_night_still_tracks(void) {
  // D2: the camera is best-effort -- each failure mode must still give a full night
  for (int mode = 0; mode < 4; mode++) {
    Sim s;
    NightPlan n = night(T0 + 3 * H, T0 + 9 * H);
    s.w.nights.push_back(n);
    if (mode < 3) s.w.cam.fail_step = mode;          // Wake, Config, Start
    else s.w.cam.hang_step = (int)CamStep::Config;   // never answers
    start(s);
    s.run(T0 + 10 * H);
    TEST_ASSERT_EQUAL(1, (int)s.w.reports.size());
    const NightReport &r = s.w.reports[0];
    TEST_ASSERT_EQUAL_MESSAGE(Outcome::Completed, r.outcome, "camera failure must not cost the night");
    TEST_ASSERT_EQUAL(Op::Fail, r.camera);
    TEST_ASSERT_TRUE(r.warnings >= 1);
    TEST_ASSERT_INT64_WITHIN(2, n.start_utc, r.track_start_utc);
    TEST_ASSERT_FALSE(s.w.cam.powered);
  }
}

void test_camera_required_and_failing_skips_the_night(void) {
  Sim s;
  s.w.nights.push_back(night(T0 + 3 * H, T0 + 9 * H, 5.0, CamPolicy::Required));
  s.w.cam.fail_step = (int)CamStep::Wake;
  start(s);
  s.run(T0 + 10 * H);
  TEST_ASSERT_EQUAL(1, (int)s.w.reports.size());
  TEST_ASSERT_EQUAL(Outcome::Skipped, s.w.reports[0].outcome);
  TEST_ASSERT_EQUAL_INT64(0, s.w.reports[0].track_start_utc);
  TEST_ASSERT_EQUAL(0, s.w.tracks);
  TEST_ASSERT_DOUBLE_WITHIN(0.01, 0.0, s.w.true_deg);              // oriented, then parked again
}

void test_camera_none_never_touches_the_camera(void) {
  Sim s;
  s.w.nights.push_back(night(T0 + 3 * H, T0 + 5 * H, 5.0, CamPolicy::None));
  start(s);
  s.run(T0 + 6 * H);
  TEST_ASSERT_EQUAL(Outcome::Completed, s.w.reports[0].outcome);
  TEST_ASSERT_EQUAL(Op::Pending, s.w.reports[0].camera);
  TEST_ASSERT_EQUAL(0, (int)s.w.cam.done.size());
}

void test_brownout_mid_track_resumes_on_the_sky(void) {
  Sim s;
  NightPlan n = night(T0 + 3 * H, T0 + 13 * H);
  s.w.nights.push_back(n);
  s.brownout_at_ms = (T0 + 7 * H) * 1000;                          // 02:00-ish into the night
  start(s);
  s.run(T0 + 7 * H + 10 * 60);
  TEST_ASSERT_EQUAL(State::Track, s.fsm->state());
  TEST_ASSERT_DOUBLE_WITHIN(0.02, expected(n, T0 + 7 * H + 600), s.w.step_deg);
  s.run(T0 + 14 * H);
  const NightReport &r = s.w.reports.back();
  TEST_ASSERT_EQUAL(Outcome::Completed, r.outcome);
  TEST_ASSERT_EQUAL(1, r.resumes);
  TEST_ASSERT_EQUAL(Op::Ok, r.camera);                              // kept from before the reset
  TEST_ASSERT_INT64_WITHIN(2, n.start_utc, r.track_start_utc);      // the night's start, not the resume's
  TEST_ASSERT_DOUBLE_WITHIN(0.01, 0.0, s.w.true_deg);
}

void test_brownout_during_rewind_finishes_the_rewind(void) {
  Sim s;
  NightPlan n = night(T0 + 3 * H, T0 + 9 * H);
  s.w.nights.push_back(n);
  s.brownout_at_ms = (n.stop_utc + 120) * 1000;                     // 2 min into the rewind
  start(s);
  s.run(T0 + 10 * H);
  TEST_ASSERT_EQUAL(1, (int)s.w.reports.size());
  TEST_ASSERT_EQUAL(Outcome::Completed, s.w.reports[0].outcome);
  TEST_ASSERT_DOUBLE_WITHIN(0.01, 0.0, s.w.true_deg);
}

void test_envelope_violation_is_refused_without_moving(void) {
  Sim s;
  s.w.nights.push_back(night(T0 + 3 * H, T0 + 13 * H, 100.0));     // 100 + 150 deg > 182
  start(s);
  s.run(T0 + 14 * H);
  TEST_ASSERT_EQUAL(1, (int)s.w.reports.size());
  TEST_ASSERT_EQUAL(Outcome::Skipped, s.w.reports[0].outcome);
  TEST_ASSERT_EQUAL_STRING("night would leave the travel envelope", s.w.reports[0].reason);
  TEST_ASSERT_EQUAL(0, s.w.moves);
  TEST_ASSERT_TRUE(s.w.max_true_deg_seen < 0.01);
}

void test_battery_low_mid_track_safe_stops_and_rewinds(void) {
  Sim s;
  s.w.nights.push_back(night(T0 + 3 * H, T0 + 13 * H));
  s.w.battery_drop_at_ms = (T0 + 6 * H) * 1000;
  start(s);
  s.run(T0 + 14 * H);
  const NightReport &r = s.w.reports.back();
  TEST_ASSERT_EQUAL(Outcome::SafeStopped, r.outcome);
  TEST_ASSERT_EQUAL_STRING("battery low", r.reason);
  TEST_ASSERT_INT64_WITHIN(10, T0 + 6 * H, r.track_stop_utc);       // within the 5-sample debounce
  TEST_ASSERT_TRUE(r.min_battery_v < 10.5);
  // the camera got its stop command, then lost power
  TEST_ASSERT_EQUAL((int)CamStep::Stop, s.w.cam.done.back());
  TEST_ASSERT_FALSE(s.w.cam.powered);
  TEST_ASSERT_DOUBLE_WITHIN(0.01, 0.0, s.w.true_deg);
  TEST_ASSERT_EQUAL(Fault::None, s.fsm->fault());
}

void test_jam_mid_track_faults_without_a_blind_rewind(void) {
  Sim s;
  s.w.nights.push_back(night(T0 + 3 * H, T0 + 13 * H));
  s.w.jam_at_ms = (T0 + 5 * H) * 1000;
  start(s);
  s.run(T0 + 14 * H);
  const NightReport &r = s.w.reports.back();
  TEST_ASSERT_EQUAL(Outcome::Faulted, r.outcome);
  TEST_ASSERT_EQUAL(Fault::Divergence, s.fsm->fault());
  TEST_ASSERT_TRUE(r.max_div_deg > 2.0 && r.max_div_deg < 2.2);   // caught at the threshold, not later
  TEST_ASSERT_TRUE(s.w.sleeping);
  TEST_ASSERT_EQUAL_INT64(0, s.w.wake_utc);                          // button only
  TEST_ASSERT_FALSE(s.w.enabled);
  // the next night is not attempted while the fault stands
  s.w.nights.push_back(night(T0 + 27 * H, T0 + 30 * H));
  s.run(T0 + 31 * H);
  TEST_ASSERT_EQUAL(1, (int)s.w.reports.size());
}

void test_button_wake_from_fault_goes_to_config_then_back_to_fault(void) {
  Sim s;
  s.w.nights.push_back(night(T0 + 3 * H, T0 + 13 * H));
  s.w.jam_at_ms = (T0 + 5 * H) * 1000;
  start(s);
  s.run(T0 + 14 * H);
  s.button_at_ms = (T0 + 15 * H) * 1000;
  s.run(T0 + 15 * H + 60);
  TEST_ASSERT_EQUAL(State::ConfigAp, s.fsm->state());
  s.run(T0 + 16 * H);                                                 // config times out
  TEST_ASSERT_EQUAL(State::Fault, s.fsm->state());
}

void test_no_gps_fix_keeps_the_rtc_and_tracks(void) {
  Sim s;
  s.w.nights.push_back(night(T0 + 3 * H, T0 + 9 * H));
  s.w.gps_ok = false;
  s.w.rtc_offset_s = 4;
  start(s);
  s.run(T0 + 10 * H);
  const NightReport &r = s.w.reports.back();
  TEST_ASSERT_EQUAL(Outcome::Completed, r.outcome);
  TEST_ASSERT_EQUAL(Op::Fail, r.gps);
  TEST_ASSERT_TRUE(r.warnings >= 1);
  TEST_ASSERT_EQUAL_INT64(4, s.w.rtc_offset_s);                      // untouched
}

void test_gps_corrects_a_drifted_rtc(void) {
  Sim s;
  NightPlan n = night(T0 + 3 * H, T0 + 9 * H);
  s.w.nights.push_back(n);
  s.w.rtc_offset_s = 7;                                               // RTC 7 s fast
  start(s);
  s.run(T0 + 10 * H);
  const NightReport &r = s.w.reports.back();
  TEST_ASSERT_EQUAL(Op::Ok, r.gps);
  TEST_ASSERT_EQUAL_INT64(7, r.rtc_drift_s);
  TEST_ASSERT_EQUAL_INT64(0, s.w.rtc_offset_s);
  TEST_ASSERT_INT64_WITHIN(2, n.start_utc, r.track_start_utc);
}

void test_rtc_lost_power_without_gps_skips(void) {
  Sim s;
  s.w.nights.push_back(night(T0 + 3 * H, T0 + 9 * H, 5.0, CamPolicy::BestEffort, GpsPolicy::None));
  s.w.rtc_lost = true;
  start(s);
  s.run(T0 + 10 * H);
  TEST_ASSERT_EQUAL(Outcome::Skipped, s.w.reports.back().outcome);
  TEST_ASSERT_EQUAL(0, s.w.tracks);
}

void test_power_on_mid_night_starts_late_on_the_sky(void) {
  Sim s;
  NightPlan n = night(T0 + 3 * H, T0 + 13 * H);
  s.w.nights.push_back(n);
  start(s, T0 + 6 * H);                                               // plugged in three hours late
  s.run(T0 + 7 * H);
  TEST_ASSERT_EQUAL(State::Track, s.fsm->state());
  TEST_ASSERT_DOUBLE_WITHIN(0.02, expected(n, T0 + 7 * H), s.w.step_deg);
  s.run(T0 + 14 * H);
  TEST_ASSERT_EQUAL(Outcome::Completed, s.w.reports.back().outcome);
}

void test_night_across_year_end(void) {
  Sim s;
  int64_t nye = 1830211200;                                           // 2027-12-31 00:00Z
  NightPlan n = night(nye + 20 * H, nye + 30 * H);                    // ends 2028-01-01 06:00Z
  s.w.nights.push_back(n);
  start(s, nye);
  s.run(nye + 31 * H);
  TEST_ASSERT_EQUAL(Outcome::Completed, s.w.reports.back().outcome);
  TEST_ASSERT_INT64_WITHIN(2, n.stop_utc, s.w.reports.back().track_stop_utc);
}

void test_encoder_dead_at_boot_is_a_fault(void) {
  Sim s;
  s.w.encoder_ok = false;
  start(s);
  s.run(T0 + 60);
  TEST_ASSERT_EQUAL(State::Fault, s.fsm->state());
  TEST_ASSERT_EQUAL(Fault::EncoderAtBoot, s.fsm->fault());
}

void test_position_moved_while_asleep_is_logged(void) {
  Sim s;
  s.w.nights.push_back(night(T0 + 3 * H, T0 + 5 * H));
  start(s);
  s.run(T0 + 1 * H);                                  // asleep, parked at 0
  s.w.true_deg = 1.5;                                 // someone turns the camera by hand
  s.run(T0 + 6 * H);
  bool found = false;
  for (auto &l : s.w.logs) if (l.find("position moved while asleep") != std::string::npos) found = true;
  TEST_ASSERT_TRUE(found);
  TEST_ASSERT_EQUAL(Outcome::Completed, s.w.reports.back().outcome);
}

void setUp(void) {}
void tearDown(void) {}

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_one_good_night);
  RUN_TEST(test_three_nights_in_a_row);
  RUN_TEST(test_camera_fails_at_every_stage_night_still_tracks);
  RUN_TEST(test_camera_required_and_failing_skips_the_night);
  RUN_TEST(test_camera_none_never_touches_the_camera);
  RUN_TEST(test_brownout_mid_track_resumes_on_the_sky);
  RUN_TEST(test_brownout_during_rewind_finishes_the_rewind);
  RUN_TEST(test_envelope_violation_is_refused_without_moving);
  RUN_TEST(test_battery_low_mid_track_safe_stops_and_rewinds);
  RUN_TEST(test_jam_mid_track_faults_without_a_blind_rewind);
  RUN_TEST(test_button_wake_from_fault_goes_to_config_then_back_to_fault);
  RUN_TEST(test_no_gps_fix_keeps_the_rtc_and_tracks);
  RUN_TEST(test_gps_corrects_a_drifted_rtc);
  RUN_TEST(test_rtc_lost_power_without_gps_skips);
  RUN_TEST(test_power_on_mid_night_starts_late_on_the_sky);
  RUN_TEST(test_night_across_year_end);
  RUN_TEST(test_encoder_dead_at_boot_is_a_fault);
  RUN_TEST(test_position_moved_while_asleep_is_logged);
  return UNITY_END();
}
