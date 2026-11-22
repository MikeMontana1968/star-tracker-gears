// The GoPro sequences against a simulated HERO6. Run:  pio test -e native
#include <unity.h>
#include <stdio.h>
#include <string.h>
#include <string>
#include <map>
#include "CamProfile.h"
#include "GoPro.h"

using namespace cam;
using app::Op;

// ---- a HERO6 as the protocol sees it --------------------------------------------
struct FakeHero : Io {
  uint64_t t = 0;
  bool powered = true;
  bool awake = true;
  bool ap_in_sleep = true;        // the AP stays up while asleep (WoL works through it)
  bool wol_works = true;
  bool wakes_on_power = false;    // rail on -> the camera boots by itself
  bool drop_ap_recording = false; // the open question in FIRMWARE.md s6
  int  clamp_id = -1, clamp_to = 0;   // a setting the camera silently changes
  int  refuse_id = -1;
  bool recording = false;
  int  mode = 0, submode = 0;
  std::map<int, int> settings;
  uint64_t wake_at = 0, rail_on_at = 0;
  int  wols = 0, rails = 0, sleeps = 0;
  uint64_t cancel_at = 0;

  bool apUp() { return powered && (awake || ap_in_sleep) && !(drop_ap_recording && recording); }
  void tick() {
    if (wake_at && t >= wake_at) { awake = true; wake_at = 0; }
  }
  uint64_t ms() override { return t; }
  void delay(uint32_t d) override { t += d; tick(); }
  bool joined() override { tick(); return apUp(); }
  int get(const char *path, std::string *body, uint32_t) override {
    t += 50; tick();
    if (!apUp() || !awake) { t += 2000; return -1; }        // nobody answers: a timeout
    std::string p = path;
    if (p == "/gp/gpControl/status") {
      char b[512];
      std::string sets;
      for (auto &kv : settings) { char e[24]; snprintf(e, sizeof e, ",\"%d\":%d", kv.first, kv.second); sets += e; }
      snprintf(b, sizeof b, "{\"status\":{\"8\":%d,\"70\":87,\"54\":31000000,\"43\":%d,\"44\":%d},\"settings\":{\"0\":0%s}}",
               recording ? 1 : 0, mode, submode, sets.c_str());
      *body = b;
      return 200;
    }
    if (p == "/gp/gpControl/info") { *body = "{\"info\":{\"model_name\":\"HERO6 Black\",\"firmware_version\":\"HD6.01.02.01.00\"}}"; return 200; }
    if (p.rfind("/gp/gpControl/command/sub_mode?mode=", 0) == 0) {
      sscanf(path, "/gp/gpControl/command/sub_mode?mode=%d&sub_mode=%d", &mode, &submode);
      return 200;
    }
    int id, val;
    if (sscanf(path, "/gp/gpControl/setting/%d/%d", &id, &val) == 2) {
      if (id == refuse_id) return 403;
      settings[id] = id == clamp_id ? clamp_to : val;
      return 200;
    }
    if (p == "/gp/gpControl/command/shutter?p=1") { recording = true; return 200; }
    if (p == "/gp/gpControl/command/shutter?p=0") { recording = false; return 200; }
    if (p == "/gp/gpControl/command/system/sleep") { awake = false; sleeps++; return 200; }
    return 404;
  }
  long save(const char *path, const char *, uint32_t) override {
    std::string b;
    if (std::string(path) == "/gp/gpControl") { t += 400; return apUp() && awake ? 64000 : -1; }
    return get(path, &b, 0) == 200 ? (long)b.size() + 100 : -1;
  }
  bool wol(const uint8_t *) override {
    wols++;
    if (powered && apUp() && !awake && wol_works && !wake_at) wake_at = t + 3000;
    return true;
  }
  void rail(bool on) override {
    rails++;
    if (!on) { powered = false; awake = false; recording = false; wake_at = 0; }
    else { powered = true; if (wakes_on_power) wake_at = t + 4000; }
  }
  bool cancelled() override { return cancel_at && t >= cancel_at; }
};

static const char *PROFILE =
  "{\"model\":\"HERO6\",\"wifi\":{\"ssid\":\"GP-test\",\"password\":\"goprohero\"},"
  "\"mac\":\"D6:32:60:AA:BB:CC\","
  "\"commands\":{\"mode\":\"/gp/gpControl/command/sub_mode?mode=2&sub_mode=2\","
  "\"shutter_on\":\"/gp/gpControl/command/shutter?p=1\",\"shutter_off\":\"/gp/gpControl/command/shutter?p=0\","
  "\"sleep\":\"/gp/gpControl/command/system/sleep\"},"
  "\"verify_mode\":{\"mode_key\":43,\"mode\":2,\"submode_key\":44,\"submode\":2},"
  "\"settings\":[{\"name\":\"exposure\",\"id\":31,\"value\":6},{\"name\":\"interval\",\"id\":32,\"value\":0},"
  "{\"name\":\"iso\",\"id\":24,\"value\":0}],"
  "\"status_keys\":{\"recording\":8,\"battery_pct\":70,\"sd_free_kb\":54}}";

static Profile prof() {
  Profile p; app::Messages e;
  TEST_ASSERT_TRUE_MESSAGE(parseProfile(PROFILE, strlen(PROFILE), &p, &e), e.n ? e.msg[0] : "?");
  return p;
}

// ------------------------------------------------------------------------------
void test_profile_parses_and_rejects(void) {
  Profile p = prof();
  TEST_ASSERT_EQUAL_STRING("GP-test", p.ssid);
  TEST_ASSERT_TRUE(p.has_mac);
  TEST_ASSERT_EQUAL_HEX8(0xCC, p.mac[5]);
  TEST_ASSERT_EQUAL(3, p.n_settings);
  TEST_ASSERT_EQUAL(31, p.settings[0].id);
  TEST_ASSERT_EQUAL(45000u, p.wake_ms);

  const char *bad = "{\"wifi\":{\"ssid\":\"x\",\"password\":\"short\"},\"mac\":\"nope\","
                    "\"commands\":{\"shutter_on\":\"gp/no-slash\"},\"settings\":[{\"name\":\"a\"}]}";
  Profile q; app::Messages e;
  TEST_ASSERT_FALSE(parseProfile(bad, strlen(bad), &q, &e));
  std::string all;
  for (int i = 0; i < e.n; i++) { all += e.msg[i]; all += "\n"; }
  TEST_ASSERT_TRUE(all.find("wifi.password") != std::string::npos);
  TEST_ASSERT_TRUE(all.find("mac:") != std::string::npos);
  TEST_ASSERT_TRUE(all.find("commands.shutter_on: must start with '/'") != std::string::npos);
  TEST_ASSERT_TRUE(all.find("commands.shutter_off: missing") != std::string::npos);
  TEST_ASSERT_TRUE(all.find("settings[0].id") != std::string::npos);
  TEST_ASSERT_FALSE(q.valid);
}

void test_wake_already_awake(void) {
  Profile p = prof(); FakeHero h; GoPro g(h, p);
  TEST_ASSERT_EQUAL(Op::Ok, g.wake());
  TEST_ASSERT_EQUAL(0, h.wols);
  TEST_ASSERT_EQUAL(87, g.state().battery_pct);
}

void test_wake_by_wol(void) {
  Profile p = prof(); FakeHero h; h.awake = false; GoPro g(h, p);
  TEST_ASSERT_EQUAL(Op::Ok, g.wake());
  TEST_ASSERT_EQUAL(3, h.wols);
  TEST_ASSERT_EQUAL(0, h.rails);
  TEST_ASSERT_EQUAL(1u, g.state().wakes_by_wol);
  TEST_ASSERT_TRUE(h.t < 15000);
}

void test_wake_falls_back_to_the_rail(void) {
  Profile p = prof(); FakeHero h; h.awake = false; h.wol_works = false; h.wakes_on_power = true; GoPro g(h, p);
  TEST_ASSERT_EQUAL(Op::Ok, g.wake());
  TEST_ASSERT_EQUAL(2, h.rails);                       // off, on
  TEST_ASSERT_EQUAL(1u, g.state().wakes_by_rail);
  TEST_ASSERT_TRUE(h.t <= p.wake_ms);
}

void test_wake_gives_up_inside_its_deadline(void) {
  Profile p = prof(); FakeHero h; h.awake = false; h.wol_works = false; GoPro g(h, p);
  TEST_ASSERT_EQUAL(Op::Fail, g.wake());
  TEST_ASSERT_TRUE(h.t <= p.wake_ms + 5000);
  TEST_ASSERT_EQUAL_STRING("joined the camera AP, but it never answered", g.state().error);
  FakeHero n; n.awake = false; n.ap_in_sleep = false; n.wol_works = false; GoPro g2(n, p);   // asleep, AP off
  TEST_ASSERT_EQUAL(Op::Fail, g2.wake());
  TEST_ASSERT_EQUAL_STRING("could not join the camera AP 'GP-test'", g2.state().error);
}

void test_configure_applies_and_reads_back(void) {
  Profile p = prof(); FakeHero h; GoPro g(h, p);
  TEST_ASSERT_EQUAL(Op::Ok, g.configure());
  TEST_ASSERT_EQUAL(2, h.mode); TEST_ASSERT_EQUAL(2, h.submode);
  TEST_ASSERT_EQUAL(6, h.settings[31]);
  FakeHero c; c.clamp_id = 31; c.clamp_to = 4; GoPro g2(c, p);   // camera keeps 15 s
  TEST_ASSERT_EQUAL(Op::Fail, g2.configure());
  TEST_ASSERT_EQUAL_STRING("setting exposure (31) reads 4, wanted 6", g2.state().error);
  FakeHero r; r.refuse_id = 24; GoPro g3(r, p);
  TEST_ASSERT_EQUAL(Op::Fail, g3.configure());
  TEST_ASSERT_EQUAL_STRING("setting iso (24=0) refused", g3.state().error);
}

void test_start_and_stop(void) {
  Profile p = prof(); FakeHero h; GoPro g(h, p);
  TEST_ASSERT_EQUAL(Op::Ok, g.start());
  TEST_ASSERT_TRUE(h.recording);
  TEST_ASSERT_TRUE(g.state().recording);
  h.t += 8ULL * 3600 * 1000;                           // a night later
  TEST_ASSERT_EQUAL(Op::Ok, g.stop());
  TEST_ASSERT_FALSE(h.recording);
  TEST_ASSERT_EQUAL(1, h.sleeps);
}

void test_stop_when_the_ap_vanished_mid_record(void) {
  Profile p = prof(); FakeHero h; h.drop_ap_recording = true; GoPro g(h, p);
  TEST_ASSERT_EQUAL(Op::Fail, g.start());              // can't even confirm: the AP went with the shutter
  TEST_ASSERT_TRUE(h.recording);
  TEST_ASSERT_EQUAL(Op::Fail, g.stop());
  TEST_ASSERT_EQUAL_STRING("could not rejoin the camera AP to stop it", g.state().error);
}

void test_cancel_stops_a_wake(void) {
  Profile p = prof(); FakeHero h; h.awake = false; h.wol_works = false; h.cancel_at = 4000; GoPro g(h, p);
  TEST_ASSERT_EQUAL(Op::Fail, g.wake());
  TEST_ASSERT_TRUE(h.t < 9000);
  TEST_ASSERT_EQUAL(0, h.rails);                       // never got as far as the rail
}

void test_discover_saves_three_files(void) {
  Profile p = prof(); FakeHero h; GoPro g(h, p);
  long n = 0;
  TEST_ASSERT_EQUAL(Op::Ok, g.discover(&n));
  TEST_ASSERT_TRUE(n > 64000);
  FakeHero a; a.awake = false; a.ap_in_sleep = false; GoPro g2(a, p);
  TEST_ASSERT_EQUAL(Op::Fail, g2.discover(&n));
}

void setUp(void) {}
void tearDown(void) {}

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_profile_parses_and_rejects);
  RUN_TEST(test_wake_already_awake);
  RUN_TEST(test_wake_by_wol);
  RUN_TEST(test_wake_falls_back_to_the_rail);
  RUN_TEST(test_wake_gives_up_inside_its_deadline);
  RUN_TEST(test_configure_applies_and_reads_back);
  RUN_TEST(test_start_and_stop);
  RUN_TEST(test_stop_when_the_ap_vanished_mid_record);
  RUN_TEST(test_cancel_stops_a_wake);
  RUN_TEST(test_discover_saves_three_files);
  return UNITY_END();
}
