// Night-FSM scenarios on the host simulator. Run:  pio test -e native
// Every night here sleeps, wakes by alarm and reboots, so resume paths run
// on every test, not just the brownout one.
#include <unity.h>
#include <string.h>
#include "Sim.h"
#include "Schedule.h"

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

void test_a_moon_night_tracks_at_the_lunar_rate(void) {
  Sim s;
  NightPlan n = night(T0 + 3 * H, T0 + 13 * H, 0.0);
  n.moon = true; n.rate = TrackRate::Lunar;
  s.w.nights.push_back(n);
  start(s);
  s.run(T0 + 8 * H);                                                // 5 h into the night
  TEST_ASSERT_EQUAL((int)TrackRate::Lunar, (int)s.w.last_rate);
  TEST_ASSERT_DOUBLE_WITHIN(0.05, 5 * 3600 * 360.0 / LUNAR_DAY_S, s.w.true_deg);   // 72.5, not 75.2
  s.run(T0 + 15 * H);
  TEST_ASSERT_EQUAL(1, (int)s.w.reports.size());
  TEST_ASSERT_EQUAL(Outcome::Completed, s.w.reports[0].outcome);
  TEST_ASSERT_TRUE(s.w.reports[0].max_div_deg < 0.01);              // the divergence check uses the same rate
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

// ---- with the real schedule (slice 4) ---------------------------------------
static const char *SCHED =
    "{\"version\":1,\"site\":{\"lat\":34.6,\"lon\":-120.4,\"tz_offset_min\":-480},"
    "\"defaults\":{\"start\":{\"event\":\"dusk_astronomical\",\"offset_min\":10},"
    "\"stop\":{\"event\":\"dawn_astronomical\",\"offset_min\":-10},\"start_angle_deg\":0.0,"
    "\"camera\":\"nightlapse_30s_iso800\",\"camera_policy\":\"best_effort\"},"
    "\"nights\":[{\"date\":\"2026-11-22\"},{\"date\":\"2026-11-23\",\"skip\":true}],"
    "\"repeat_after_list\":true}";

void test_real_schedule_three_dusk_to_dawn_nights(void) {
  static ScheduleDoc doc; Messages err;
  ScheduleLimits lim;
  TEST_ASSERT_TRUE(parseSchedule(SCHED, strlen(SCHED), lim, T0, &doc, &err));
  Schedule sched(doc, lim);
  Sim s;
  s.w.ext = &sched;
  start(s);
  s.run(T0 + 5 * 24 * H);
  // local evenings 22nd, (23rd skipped), 24th, 25th; each dusk is ~01:30Z the next
  // UTC day, so the 26th's starts after the run ends (27th 00:00Z)
  TEST_ASSERT_EQUAL(3, (int)s.w.reports.size());
  int32_t d22; parseDate("2026-11-22", &d22);
  TEST_ASSERT_EQUAL_INT32(d22, s.w.reports[0].plan.day);
  TEST_ASSERT_EQUAL_INT32(d22 + 2, s.w.reports[1].plan.day);
  TEST_ASSERT_EQUAL_INT32(d22 + 3, s.w.reports[2].plan.day);
  for (auto &r : s.w.reports) {
    TEST_ASSERT_EQUAL(Outcome::Completed, r.outcome);
    TEST_ASSERT_INT64_WITHIN(2, r.plan.start_utc, r.track_start_utc);
    TEST_ASSERT_TRUE(r.plan.stop_utc - r.plan.start_utc > 10 * H);   // a long November night
  }
  // Skyfield: astronomical dusk 2026-11-22 at 1795400472; start = dusk + 10 min
  TEST_ASSERT_INT64_WITHIN(60, 1795400472 + 600, s.w.reports[0].track_start_utc);
}

void test_gps_fix_moves_tonights_times(void) {
  static ScheduleDoc doc; Messages err;
  ScheduleLimits lim;
  TEST_ASSERT_TRUE(parseSchedule(SCHED, strlen(SCHED), lim, T0, &doc, &err));
  Schedule sched(doc, lim);
  Sim s;
  s.cfg.lead_s = 1800;            // wake early enough that an earlier start is still ahead
  s.w.ext = &sched;
  s.w.gps_lon = -118.4;           // the tracker was carried 2 deg east of the schedule's site
  start(s);
  s.run(T0 + 40 * H);                                  // the night ends ~12:00Z on the 23rd
  TEST_ASSERT_EQUAL(1, (int)s.w.reports.size());
  const NightReport &r = s.w.reports[0];
  TEST_ASSERT_EQUAL(Op::Ok, r.gps);
  // 4 min earlier per degree east: the night started ~8 min before the site's dusk
  TEST_ASSERT_INT64_WITHIN(60, 1795400472 + 600 - 480, r.track_start_utc);
  TEST_ASSERT_EQUAL(Outcome::Completed, r.outcome);
}

// ---- slice 5: CONFIG_AP, the radio table, web commands ---------------------
// boot with nothing scheduled, then press the button at `at`
static void toConfig(Sim &s, int64_t at) {
  start(s);
  s.run(at - 60);
  s.button_at_ms = at * 1000;
  s.run(at + 2);
}

// run to `until`, a client touching every `every_s` seconds
static void runTouching(Sim &s, int64_t until, int every_s = 30) {
  while (s.w.trueUtc() < until) {
    int64_t nxt = s.w.trueUtc() + every_s;
    s.run(nxt < until ? nxt : until);
    if (s.fsm && !s.w.sleeping) s.fsm->touch();
  }
}

void test_config_ap_brings_the_ap_up_and_activity_keeps_it(void) {
  Sim s;
  toConfig(s, T0 + 1 * H);
  TEST_ASSERT_EQUAL(State::ConfigAp, s.fsm->state());
  TEST_ASSERT_EQUAL(Radio::Ap, s.w.radio_now);
  runTouching(s, T0 + 2 * H);                         // an hour of a phone polling
  TEST_ASSERT_EQUAL(State::ConfigAp, s.fsm->state());
  TEST_ASSERT_TRUE(s.fsm->deadlineLeftMs() > 9 * 60000);
  s.run(T0 + 2 * H + 11 * 60);                        // the phone goes away: 10 min later, out
  TEST_ASSERT_TRUE(s.w.sleeping);
  TEST_ASSERT_EQUAL(Radio::Off, s.w.radio_now);
}

void test_config_ap_gives_way_to_a_scheduled_night(void) {
  Sim s;
  NightPlan n = night(T0 + 3 * H, T0 + 9 * H);
  s.w.nights.push_back(n);
  start(s);
  s.run(T0 + 1 * H);
  s.button_at_ms = (T0 + 1 * H) * 1000;               // wake for config before the night
  runTouching(s, T0 + 10 * H);                        // ...and a phone left connected all night
  TEST_ASSERT_EQUAL(1, (int)s.w.reports.size());
  const NightReport &r = s.w.reports[0];
  TEST_ASSERT_EQUAL(Outcome::Completed, r.outcome);
  TEST_ASSERT_INT64_WITHIN(2, n.start_utc, r.track_start_utc);
}

void test_radio_table_over_a_night(void) {
  for (int ap = 0; ap < 2; ap++) {
    Sim s;
    s.cfg.ap_during_track = ap;
    s.w.nights.push_back(night(T0 + 3 * H, T0 + 9 * H));
    start(s);
    s.run(T0 + 2 * H);
    TEST_ASSERT_EQUAL(0, (int)s.w.radios.size());     // HOME/IDLE never touch the radio
    s.run(T0 + 3 * H - 5);                             // camera done, waiting for the start
    TEST_ASSERT_EQUAL(Radio::Sta, s.w.radios[0]);      // the camera sequence: STA
    TEST_ASSERT_EQUAL(Radio::Sta, s.w.radio_now);      // recording: stay joined (D8 does not apply)
    s.run(T0 + 5 * H);
    TEST_ASSERT_EQUAL(Radio::Sta, s.w.radio_now);
    s.run(T0 + 10 * H);
    TEST_ASSERT_EQUAL(Radio::Off, s.w.radio_now);
    // STA from the wake to the stop, never dropped -> off for the rewind
    TEST_ASSERT_EQUAL(2, (int)s.w.radios.size());
    TEST_ASSERT_EQUAL(Radio::Sta, s.w.radios[0]);
    TEST_ASSERT_EQUAL(Radio::Off, s.w.radios[1]);
  }
}

void test_radio_table_without_a_camera(void) {
  // the camera failed: nothing to stay joined to, so D8 decides the tracking radio
  for (int ap = 0; ap < 2; ap++) {
    Sim s;
    s.cfg.ap_during_track = ap;
    s.w.cam.fail_step = (int)CamStep::Wake;
    s.w.nights.push_back(night(T0 + 3 * H, T0 + 9 * H));
    start(s);
    s.run(T0 + 5 * H);
    TEST_ASSERT_EQUAL(ap ? Radio::Ap : Radio::Off, s.w.radio_now);
    s.run(T0 + 10 * H);
    TEST_ASSERT_EQUAL(Radio::Off, s.w.radio_now);
  }
}

void test_config_commands_move_inside_the_envelope_only(void) {
  Sim s;
  toConfig(s, T0 + 1 * H);
  TEST_ASSERT_TRUE(s.fsm->command(Cmd::Goto, 10).ok);
  TEST_ASSERT_FALSE(s.fsm->command(Cmd::Goto, 20).ok);        // already moving
  s.run(T0 + 1 * H + 60);
  TEST_ASSERT_DOUBLE_WITHIN(1e-6, 10.0, s.w.step_deg);
  TEST_ASSERT_TRUE(s.fsm->command(Cmd::Jog, -3).ok);
  s.run(T0 + 1 * H + 120);
  TEST_ASSERT_DOUBLE_WITHIN(1e-6, 7.0, s.w.step_deg);
  TEST_ASSERT_FALSE(s.fsm->command(Cmd::Goto, 190).ok);       // past the envelope
  TEST_ASSERT_FALSE(s.fsm->command(Cmd::Jog, -9.5).ok);       // 7 - 9.5 = -2.5 < -2
  TEST_ASSERT_FALSE(s.fsm->command(Cmd::Goto, NAN).ok);
  TEST_ASSERT_FALSE(s.fsm->command(Cmd::EndNight).ok);        // no night
  TEST_ASSERT_TRUE(s.fsm->command(Cmd::Home).ok);
  s.run(T0 + 1 * H + 180);
  TEST_ASSERT_DOUBLE_WITHIN(1e-6, 0.0, s.w.step_deg);
  TEST_ASSERT_FALSE(s.fsm->moveFailed());
  TEST_ASSERT_TRUE(s.fsm->command(Cmd::SleepNow).ok);
  s.run(T0 + 1 * H + 190);
  TEST_ASSERT_TRUE(s.w.sleeping);
}

void test_config_commands_are_refused_outside_config(void) {
  Sim s;
  s.w.nights.push_back(night(T0 + 3 * H, T0 + 9 * H));
  start(s);
  s.run(T0 + 4 * H);
  TEST_ASSERT_EQUAL(State::Track, s.fsm->state());
  TEST_ASSERT_FALSE(s.fsm->command(Cmd::Goto, 10).ok);
  TEST_ASSERT_FALSE(s.fsm->command(Cmd::TestTrack, 10).ok);
  TEST_ASSERT_FALSE(s.fsm->command(Cmd::ClearFault).ok);
  TEST_ASSERT_FALSE(s.fsm->command(Cmd::SleepNow).ok);
  TEST_ASSERT_EQUAL(State::Track, s.fsm->state());
}

void test_test_track_runs_with_the_ap_up_and_leaves_tonight_alone(void) {
  Sim s;
  NightPlan n = night(T0 + 6 * H, T0 + 10 * H);
  s.w.nights.push_back(n);
  start(s);
  s.run(T0 + 1 * H);
  s.button_at_ms = (T0 + 1 * H) * 1000;
  s.run(T0 + 1 * H + 5);
  TEST_ASSERT_FALSE(s.fsm->command(Cmd::TestTrack, 0).ok);
  TEST_ASSERT_TRUE(s.fsm->command(Cmd::TestTrack, 30).ok);
  size_t r0 = s.w.radios.size();
  s.run(T0 + 1 * H + 15 * 60);
  TEST_ASSERT_EQUAL(State::Track, s.fsm->state());
  s.run(T0 + 1 * H + 40 * 60);                        // 30 min + rewind
  TEST_ASSERT_EQUAL(1, (int)s.w.reports.size());
  const NightReport &t = s.w.reports[0];
  TEST_ASSERT_TRUE(t.plan.test);
  TEST_ASSERT_EQUAL(Outcome::Completed, t.outcome);
  TEST_ASSERT_INT64_WITHIN(2, 30 * 60, t.track_stop_utc - t.track_start_utc);
  TEST_ASSERT_EQUAL(State::ConfigAp, s.fsm->state());  // back to the human
  for (size_t i = r0; i < s.w.radios.size(); i++) TEST_ASSERT_EQUAL(Radio::Ap, s.w.radios[i]);
  TEST_ASSERT_EQUAL(Radio::Ap, s.w.radio_now);
  TEST_ASSERT_EQUAL(0, (int)s.w.cam.done.size());     // no camera, no GPS
  TEST_ASSERT_FALSE(s.w.gps_on);
  TEST_ASSERT_DOUBLE_WITHIN(0.01, 0.0, s.w.true_deg);
  s.run(T0 + 11 * H);                                 // the real night still happens
  TEST_ASSERT_EQUAL(2, (int)s.w.reports.size());
  TEST_ASSERT_FALSE(s.w.reports[1].plan.test);
  TEST_ASSERT_EQUAL(Outcome::Completed, s.w.reports[1].outcome);
}

void test_end_night_rewinds_and_halt_stops_dead(void) {
  {
    Sim s;
    s.w.nights.push_back(night(T0 + 3 * H, T0 + 9 * H));
    start(s);
    s.run(T0 + 4 * H);
    TEST_ASSERT_TRUE(s.fsm->command(Cmd::EndNight).ok);
    s.run(T0 + 5 * H);
    const NightReport &r = s.w.reports.at(0);
    TEST_ASSERT_EQUAL(Outcome::SafeStopped, r.outcome);
    TEST_ASSERT_EQUAL_STRING("ended from the web UI", r.reason);
    TEST_ASSERT_DOUBLE_WITHIN(0.01, 0.0, s.w.true_deg);   // rewound
    TEST_ASSERT_TRUE(s.w.sleeping);
    TEST_ASSERT_EQUAL(Fault::None, s.fsm->fault());
  }
  {
    Sim s;
    s.w.nights.push_back(night(T0 + 3 * H, T0 + 9 * H));
    start(s);
    s.run(T0 + 4 * H);
    double at = s.w.step_deg;
    TEST_ASSERT_TRUE(s.fsm->command(Cmd::Halt).ok);
    s.run(T0 + 4 * H + 60);
    TEST_ASSERT_EQUAL(State::ConfigAp, s.fsm->state());
    TEST_ASSERT_DOUBLE_WITHIN(1e-9, at, s.w.step_deg);      // not one step after the halt
    TEST_ASSERT_EQUAL(Outcome::SafeStopped, s.w.reports.at(0).outcome);
    TEST_ASSERT_FALSE(s.w.cam.powered);
    TEST_ASSERT_TRUE(s.fsm->command(Cmd::Home).ok);          // the human brings it home
    s.run(T0 + 4 * H + 600);
    TEST_ASSERT_DOUBLE_WITHIN(0.01, 0.0, s.w.true_deg);
  }
}

// ---- diagnostics ----------------------------------------------------------------
void test_home_at_park_says_so(void) {
  Sim s;
  toConfig(s, T0 + H);
  CmdResult r = s.fsm->command(Cmd::Home);
  TEST_ASSERT_TRUE(r.ok);
  TEST_ASSERT_EQUAL_STRING("already at park: nothing to move", r.msg);
}

void test_diag_repeat_measures_the_lash(void) {
  Sim s;
  s.w.lash_deg = 0.3;                     // sim overshoot 0: every return is a plain reverse arrival
  toConfig(s, T0 + H);
  TEST_ASSERT_FALSE(s.fsm->diag(DiagKind::Repeat, 0, 3).ok);       // degrees must be > 0
  TEST_ASSERT_FALSE(s.fsm->diag(DiagKind::Repeat, 5, 21).ok);      // cycles 1..20
  TEST_ASSERT_FALSE(s.fsm->diag(DiagKind::Repeat, 190, 1).ok);     // envelope
  TEST_ASSERT_TRUE(s.fsm->diag(DiagKind::Repeat, 5, 3).ok);
  TEST_ASSERT_FALSE(s.fsm->command(Cmd::Goto, 3).ok);              // busy
  s.run(T0 + H + 300);
  const DiagState &d = s.fsm->diagState();
  TEST_ASSERT_FALSE(d.running);
  TEST_ASSERT_TRUE(d.ok);
  TEST_ASSERT_EQUAL(3, d.n_ret);
  TEST_ASSERT_EQUAL(3, d.n_out);
  TEST_ASSERT_DOUBLE_WITHIN(1, 1080, d.ret_mean_as);               // the whole lash: 0.3 deg = 1080"
  TEST_ASSERT_DOUBLE_WITHIN(1, 0, d.ret_sd_as);
  TEST_ASSERT_DOUBLE_WITHIN(1e-3, 0, d.out_mean_deg);              // forward-loaded reference: out is exact
  TEST_ASSERT_DOUBLE_WITHIN(1e-6, 0.0, s.w.step_deg);              // ends at park
  TEST_ASSERT_EQUAL(State::ConfigAp, s.fsm->state());
  // an overshoot past the lash takes it all up: returns land on the reference
  TEST_ASSERT_FALSE(s.fsm->diag(DiagKind::Repeat, 5, 3, 2.0).ok);  // 0..1.5
  TEST_ASSERT_TRUE(s.fsm->diag(DiagKind::Repeat, 5, 3, 0.5).ok);
  s.run(T0 + H + 600);
  TEST_ASSERT_TRUE(s.fsm->diagState().ok);
  TEST_ASSERT_DOUBLE_WITHIN(1, 0, s.fsm->diagState().ret_mean_as);
  TEST_ASSERT_DOUBLE_WITHIN(1e-9, 0, s.w.overshoot_deg);           // restored afterwards
  // ...and one short of it leaves the difference
  TEST_ASSERT_TRUE(s.fsm->diag(DiagKind::Repeat, 5, 2, 0.2).ok);
  s.run(T0 + H + 900);
  TEST_ASSERT_DOUBLE_WITHIN(1, 360, s.fsm->diagState().ret_mean_as);
  // a slew-speed override runs slower and is put back afterwards
  TEST_ASSERT_FALSE(s.fsm->diag(DiagKind::Sweep, 5, 1, NAN, 100).ok);
  TEST_ASSERT_TRUE(s.fsm->diag(DiagKind::Sweep, 5, 1, NAN, 1600).ok);
  TEST_ASSERT_EQUAL_UINT32(1600, s.w.slew_hz);
  s.run(T0 + H + 1200);
  TEST_ASSERT_TRUE(s.fsm->diagState().ok);
  TEST_ASSERT_EQUAL_UINT32(3200, s.w.slew_hz);
}

void test_diag_wiggle_and_sweep(void) {
  Sim s;
  toConfig(s, T0 + H);
  TEST_ASSERT_TRUE(s.fsm->command(Cmd::Goto, 7).ok);
  s.run(T0 + H + 60);
  TEST_ASSERT_TRUE(s.fsm->diag(DiagKind::Wiggle).ok);
  s.run(T0 + H + 90);
  TEST_ASSERT_TRUE(s.fsm->diagState().ok);
  TEST_ASSERT_DOUBLE_WITHIN(1e-6, 7.0, s.w.step_deg);              // wiggle comes back to where it was
  TEST_ASSERT_TRUE(s.fsm->diag(DiagKind::Sweep, 20).ok);
  s.run(T0 + H + 300);
  const DiagState &d = s.fsm->diagState();
  TEST_ASSERT_TRUE(d.ok);
  TEST_ASSERT_DOUBLE_WITHIN(1e-6, 0, d.out_err_deg[0]);
  TEST_ASSERT_DOUBLE_WITHIN(1e-6, 0.0, s.w.step_deg);              // sweep ends at park
}

void test_diag_halt_and_coils(void) {
  Sim s;
  toConfig(s, T0 + H);
  TEST_ASSERT_TRUE(s.fsm->command(Cmd::Coils, 0).ok);
  TEST_ASSERT_FALSE(s.w.enabled);
  TEST_ASSERT_FALSE(s.fsm->coils());
  TEST_ASSERT_TRUE(s.fsm->command(Cmd::Coils, 1).ok);
  TEST_ASSERT_TRUE(s.w.enabled);
  // a halt mid-diagnostic stops it dead
  TEST_ASSERT_TRUE(s.fsm->diag(DiagKind::Repeat, 10, 5).ok);
  s.run(T0 + H + 20);
  double at = s.w.step_deg;
  TEST_ASSERT_TRUE(s.fsm->command(Cmd::Halt).ok);
  s.run(T0 + H + 40);
  TEST_ASSERT_FALSE(s.fsm->diagState().running);
  TEST_ASSERT_FALSE(s.fsm->diagState().ok);
  TEST_ASSERT_DOUBLE_WITHIN(1e-9, at, s.w.step_deg);
  // refused outside CONFIG_AP
  Sim t;
  t.w.nights.push_back(night(T0 + 3 * H, T0 + 9 * H));
  start(t);
  t.run(T0 + 4 * H);
  TEST_ASSERT_FALSE(t.fsm->diag(DiagKind::Wiggle).ok);
  TEST_ASSERT_FALSE(t.fsm->command(Cmd::Coils, 0).ok);
}

// ---- slice 6: the camera from CONFIG_AP -----------------------------------------
void test_discover_swaps_the_radio_and_back(void) {
  Sim s;
  toConfig(s, T0 + H);
  size_t r0 = s.w.radios.size();
  TEST_ASSERT_TRUE(s.fsm->command(Cmd::Discover).ok);
  TEST_ASSERT_EQUAL(Radio::Sta, s.w.radio_now);
  TEST_ASSERT_TRUE(s.w.cam.powered);
  TEST_ASSERT_FALSE(s.fsm->command(Cmd::Discover).ok);       // one at a time
  TEST_ASSERT_FALSE(s.fsm->command(Cmd::TestTrack, 5).ok);
  s.run(T0 + H + 30);
  TEST_ASSERT_EQUAL(2, s.fsm->discoverState());
  TEST_ASSERT_EQUAL(Radio::Ap, s.w.radio_now);                // the phone gets its AP back
  TEST_ASSERT_FALSE(s.w.cam.powered);
  TEST_ASSERT_EQUAL(2, (int)(s.w.radios.size() - r0));        // STA, then AP: nothing else
  // a camera that never answers: cut off at the sequence deadline, AP restored
  Sim t;
  t.w.cam.hang_step = (int)CamStep::Discover;
  toConfig(t, T0 + H);
  TEST_ASSERT_TRUE(t.fsm->command(Cmd::Discover).ok);
  t.run(T0 + H + 200);
  TEST_ASSERT_EQUAL(3, t.fsm->discoverState());
  TEST_ASSERT_EQUAL(Radio::Ap, t.w.radio_now);
  TEST_ASSERT_EQUAL(State::ConfigAp, t.fsm->state());
}

void test_test_track_with_the_camera(void) {
  Sim s;
  toConfig(s, T0 + H);
  TEST_ASSERT_TRUE(s.fsm->command(Cmd::TestTrackCamera, 10).ok);
  s.run(T0 + H + 30 * 60);
  TEST_ASSERT_EQUAL(1, (int)s.w.reports.size());
  const NightReport &r = s.w.reports[0];
  TEST_ASSERT_TRUE(r.plan.test);
  TEST_ASSERT_EQUAL(Outcome::Completed, r.outcome);
  TEST_ASSERT_EQUAL(Op::Ok, r.camera);                        // woke, configured, recorded, stopped
  TEST_ASSERT_EQUAL(4, (int)s.w.cam.done.size());
  TEST_ASSERT_INT64_WITHIN(2, 600, r.track_stop_utc - r.track_start_utc);
  TEST_ASSERT_FALSE(s.w.cam.powered);
}

void setUp(void) {}
void tearDown(void) {}

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_one_good_night);
  RUN_TEST(test_a_moon_night_tracks_at_the_lunar_rate);
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
  RUN_TEST(test_real_schedule_three_dusk_to_dawn_nights);
  RUN_TEST(test_gps_fix_moves_tonights_times);
  RUN_TEST(test_config_ap_brings_the_ap_up_and_activity_keeps_it);
  RUN_TEST(test_config_ap_gives_way_to_a_scheduled_night);
  RUN_TEST(test_radio_table_over_a_night);
  RUN_TEST(test_radio_table_without_a_camera);
  RUN_TEST(test_config_commands_move_inside_the_envelope_only);
  RUN_TEST(test_config_commands_are_refused_outside_config);
  RUN_TEST(test_test_track_runs_with_the_ap_up_and_leaves_tonight_alone);
  RUN_TEST(test_end_night_rewinds_and_halt_stops_dead);
  RUN_TEST(test_home_at_park_says_so);
  RUN_TEST(test_diag_repeat_measures_the_lash);
  RUN_TEST(test_diag_wiggle_and_sweep);
  RUN_TEST(test_diag_halt_and_coils);
  RUN_TEST(test_discover_swaps_the_radio_and_back);
  RUN_TEST(test_test_track_with_the_camera);
  return UNITY_END();
}
