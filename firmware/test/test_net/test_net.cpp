// The web API (lib/net) against the FSM simulator. Run:  pio test -e native
#include <unity.h>
#include <string.h>
#include <string>
#include "../test_fsm_sim/Sim.h"
#include "Api.h"

using namespace net;

static const int64_t T0 = 1795305600;            // 2026-11-22 00:00:00Z
static const int64_t H = 3600;

struct FakeHost : Host {
  World &w;
  std::string sched = "{\"version\":1}";
  bool accept = true;
  std::string reports;
  explicit FakeHost(World &x) : w(x) {}
  int64_t utc() override { return w.utc(); }
  void status(JsonObject o) override {
    o["pos_deg"] = w.step_deg;
    o["encoder_deg"] = w.true_deg;
    o["battery_v"] = w.battery_v;
    o["rtc_utc"] = w.utc();
  }
  bool scheduleText(std::string *out) override { *out = sched; return true; }
  void scheduleInfo(JsonObject o) override { o["valid"] = true; o["source"] = "/schedule.json"; }
  bool uploadSchedule(const char *json, size_t len, app::Messages *err, app::Messages *warn) override {
    if (!accept) { err->add("defaults.stop.event: unknown event 'dusk_astro'"); return false; }
    warn->add("2026-12-01: twilight fallback");
    sched.assign(json, len);
    return true;
  }
  int nights(int n, std::string *nd) override {
    *nd = reports;
    int c = 0; for (char ch : reports) c += ch == '\n';
    (void)n; return c;
  }
  bool setTime(int64_t t) override { return w.setUtc(t); }
};

struct Rig {
  Sim s;
  LogRing log;
  FakeHost host{s.w};
  Api api{host, log};
  Response call(const char *method, const char *path, const char *body = "", const char *query = "") {
    api.attach(s.fsm.get());
    Request r; r.method = method; r.path = path; r.body = body; r.body_len = strlen(body); r.query = query;
    return api.handle(r);
  }
  JsonDocument json(const Response &r) { JsonDocument d; deserializeJson(d, r.body); return d; }
  void toConfig(int64_t at) {
    s.w.t_ms = T0 * 1000; s.boot(Wake::PowerOn);
    s.run(at - 60);
    s.button_at_ms = at * 1000;
    s.run(at + 2);
  }
};

void test_status_carries_the_contract(void) {
  Rig g;
  g.toConfig(T0 + H);
  Response r = g.call("GET", "/api/status");
  TEST_ASSERT_EQUAL(200, r.code);
  JsonDocument d = g.json(r);
  TEST_ASSERT_EQUAL_STRING("CONFIG_AP", d["state"]);
  TEST_ASSERT_EQUAL_STRING("ap", d["radio_mode"]);
  TEST_ASSERT_EQUAL_STRING("none", d["fault_text"]);
  TEST_ASSERT_TRUE(d["night"].isNull());
  TEST_ASSERT_TRUE(d["deadline_remaining_s"].as<int>() > 590);
  TEST_ASSERT_TRUE(d["battery_v"].is<double>());                   // the host's fields are merged in
  TEST_ASSERT_TRUE(d["log_next"].is<unsigned>());
}

void test_status_during_a_night_reports_tracking(void) {
  Rig g;
  NightPlan n; n.valid = true; n.start_utc = T0 + 3 * H; n.stop_utc = T0 + 9 * H; n.start_deg = 5;
  g.s.w.nights.push_back(n);
  g.s.w.t_ms = T0 * 1000; g.s.boot(Wake::PowerOn);
  g.s.run(T0 + 4 * H);
  JsonDocument d = g.json(g.call("GET", "/api/status"));
  TEST_ASSERT_EQUAL_STRING("TRACK", d["state"]);
  TEST_ASSERT_FALSE(d["night"]["test"].as<bool>());
  TEST_ASSERT_INT64_WITHIN(2, H, d["night"]["tracking_elapsed_s"].as<int64_t>());
  TEST_ASSERT_INT64_WITHIN(2, 5 * H, d["night"]["tracking_remaining_s"].as<int64_t>());
  TEST_ASSERT_DOUBLE_WITHIN(0.01, 5 + 3600 * SID_DEG_S, d["night"]["expected_deg"].as<double>());
}

void test_commands_map_onto_the_fsm_and_refusals_are_409(void) {
  Rig g;
  g.toConfig(T0 + H);
  Response r = g.call("POST", "/api/command", "{\"cmd\":\"goto\",\"deg\":12.5}");
  TEST_ASSERT_EQUAL(200, r.code);
  r = g.call("POST", "/api/command", "{\"cmd\":\"goto\",\"deg\":20}");
  TEST_ASSERT_EQUAL(409, r.code);                                     // already moving
  TEST_ASSERT_EQUAL_STRING("already moving", g.json(r)["error"]);
  g.s.run(T0 + H + 60);
  TEST_ASSERT_DOUBLE_WITHIN(1e-6, 12.5, g.s.w.step_deg);
  TEST_ASSERT_EQUAL(409, g.call("POST", "/api/command", "{\"cmd\":\"goto\"}").code);   // no deg: NaN refused
  TEST_ASSERT_EQUAL(409, g.call("POST", "/api/command", "{\"cmd\":\"jog\",\"deg\":200}").code);
  TEST_ASSERT_EQUAL(400, g.call("POST", "/api/command", "{\"cmd\":\"warp\"}").code);
  TEST_ASSERT_EQUAL(400, g.call("POST", "/api/command", "not json").code);
  TEST_ASSERT_EQUAL(405, g.call("GET", "/api/command").code);
  TEST_ASSERT_EQUAL(404, g.call("GET", "/api/nope").code);
}

void test_test_track_over_the_api(void) {
  Rig g;
  g.toConfig(T0 + H);
  Response r = g.call("POST", "/api/command", "{\"cmd\":\"test_track\",\"minutes\":5}");
  TEST_ASSERT_EQUAL(200, r.code);
  g.s.run(T0 + H + 120);
  JsonDocument d = g.json(g.call("GET", "/api/status"));
  TEST_ASSERT_EQUAL_STRING("TRACK", d["state"]);
  TEST_ASSERT_TRUE(d["night"]["test"].as<bool>());
  TEST_ASSERT_EQUAL_STRING("ap", d["radio_mode"]);
  TEST_ASSERT_EQUAL_STRING("none", d["night"]["camera_policy"]);
  r = g.call("POST", "/api/command", "{\"cmd\":\"end_night\"}");
  TEST_ASSERT_EQUAL(200, r.code);
  g.s.run(T0 + H + 300);
  TEST_ASSERT_EQUAL(State::ConfigAp, g.s.fsm->state());
  TEST_ASSERT_EQUAL(Outcome::SafeStopped, g.s.w.reports.back().outcome);
}

void test_polling_keeps_config_alive(void) {
  Rig g;
  g.toConfig(T0 + H);
  for (int i = 0; i < 40; i++) {                                      // 40 min of 1/min polls
    g.s.run(T0 + H + 2 + (i + 1) * 60);
    TEST_ASSERT_EQUAL(200, g.call("GET", "/api/status").code);
  }
  TEST_ASSERT_EQUAL(State::ConfigAp, g.s.fsm->state());
  g.s.run(T0 + H + 2 + 41 * 60 + 11 * 60);
  TEST_ASSERT_TRUE(g.s.w.sleeping);
}

void test_schedule_upload_200_or_422(void) {
  Rig g;
  g.toConfig(T0 + H);
  Response r = g.call("PUT", "/api/schedule", "{\"version\":1,\"nights\":[]}");
  TEST_ASSERT_EQUAL(200, r.code);
  JsonDocument d = g.json(r);
  TEST_ASSERT_TRUE(d["ok"].as<bool>());
  TEST_ASSERT_EQUAL(1, (int)d["warnings"].size());
  TEST_ASSERT_EQUAL_STRING("/schedule.json", d["schedule"]["source"]);
  d = g.json(g.call("GET", "/api/schedule"));
  TEST_ASSERT_EQUAL_STRING("{\"version\":1,\"nights\":[]}", d["text"]);
  g.host.accept = false;
  r = g.call("PUT", "/api/schedule", "{}");
  TEST_ASSERT_EQUAL(422, r.code);
  d = g.json(r);
  TEST_ASSERT_FALSE(d["ok"].as<bool>());
  TEST_ASSERT_EQUAL_STRING("defaults.stop.event: unknown event 'dusk_astro'", d["errors"][0]);
}

void test_log_since_and_ring_overflow(void) {
  Rig g;
  g.toConfig(T0 + H);
  for (int i = 0; i < 10; i++) { char m[16]; snprintf(m, sizeof m, "line %d", i); g.log.add(T0 + i, m); }
  JsonDocument d = g.json(g.call("GET", "/api/log", "", "since=0"));
  TEST_ASSERT_EQUAL(10, (int)d["lines"].size());
  TEST_ASSERT_EQUAL(11, d["next"].as<int>());
  TEST_ASSERT_EQUAL(0, d["lost"].as<int>());
  d = g.json(g.call("GET", "/api/log", "", "since=8"));
  TEST_ASSERT_EQUAL(3, (int)d["lines"].size());
  TEST_ASSERT_EQUAL_STRING("line 7", d["lines"][0]["msg"]);
  d = g.json(g.call("GET", "/api/log", "", "since=11"));
  TEST_ASSERT_EQUAL(0, (int)d["lines"].size());
  for (int i = 0; i < 100; i++) g.log.add(T0, "x");                   // 110 lines through a 64 ring
  d = g.json(g.call("GET", "/api/log", "", "since=11"));
  TEST_ASSERT_EQUAL(LogRing::N, (int)d["lines"].size());
  TEST_ASSERT_EQUAL(111 - LogRing::N - 11, d["lost"].as<int>());
}

void test_night_reports_as_json(void) {
  Rig g;
  g.toConfig(T0 + H);
  TEST_ASSERT_EQUAL(404, g.call("GET", "/api/night/last").code);
  g.host.reports = "{\"day\":1,\"outcome\":\"completed\"}\n{\"day\":2,\"outcome\":\"skipped\"}\n";
  JsonDocument d = g.json(g.call("GET", "/api/night/list", "", "n=10"));
  TEST_ASSERT_EQUAL(2, (int)d.size());
  TEST_ASSERT_EQUAL_STRING("skipped", d[1]["outcome"]);
  d = g.json(g.call("GET", "/api/night/last"));
  TEST_ASSERT_EQUAL(2, d["day"].as<int>());
}

void test_set_time(void) {
  Rig g;
  g.toConfig(T0 + H);
  g.s.w.rtc_offset_s = 7;
  int64_t truth = g.s.w.trueUtc();
  char body[48]; snprintf(body, sizeof body, "{\"utc\":%lld}", (long long)truth);
  Response r = g.call("POST", "/api/time", body);
  TEST_ASSERT_EQUAL(200, r.code);
  TEST_ASSERT_EQUAL(7, g.json(r)["was_off_s"].as<int>());
  TEST_ASSERT_EQUAL_INT64(0, g.s.w.rtc_offset_s);
  TEST_ASSERT_EQUAL(400, g.call("POST", "/api/time", "{\"utc\":12}").code);
}

void setUp(void) {}
void tearDown(void) {}

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_status_carries_the_contract);
  RUN_TEST(test_status_during_a_night_reports_tracking);
  RUN_TEST(test_commands_map_onto_the_fsm_and_refusals_are_409);
  RUN_TEST(test_test_track_over_the_api);
  RUN_TEST(test_polling_keeps_config_alive);
  RUN_TEST(test_schedule_upload_200_or_422);
  RUN_TEST(test_log_since_and_ring_overflow);
  RUN_TEST(test_night_reports_as_json);
  RUN_TEST(test_set_time);
  return UNITY_END();
}
