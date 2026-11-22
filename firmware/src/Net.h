// =====================================================================
//  Net -- the radio and the web UI on the board (FIRMWARE.md s7, slice 5).
//
//    Web       softAP "StarTracker-XXXX" (WPA2, AP_PASS), 192.168.4.1, a
//              captive-portal DNS so a phone opens the page by itself, and
//              the synchronous WebServer feeding net::Api. "/" is the UI,
//              gzipped into flash at build time (tools/embed_ui.py from
//              web/index.html) -- not LittleFS, because uploadfs would wipe
//              the schedule and the night log.
//    EspHost   net::Host over the board: the hardware half of /api/status,
//              schedule text + upload, the night log, set time.
//
//  Radio modes come only from the FSM (ISystem::radio). STA is the GoPro
//  link and lands with slice 6; until then STA leaves the radio off.
//  The HTTP server runs in loop(), on core 1 beside the step ISR: a slow
//  request can delay the FSM tick, never a step (s2).
// =====================================================================
#pragma once
#include <Arduino.h>
#include "Api.h"
#include "EspPlatform.h"

namespace esp {

class EspHost : public net::Host {
 public:
  EspHost(Clock &c, Motion &m, Sensors &s, As5600 &a, Ds3231 &r, Gps &g, Store &st, ScheduleStore &ss)
      : clk_(c), mot_(m), sens_(s), as_(a), rtc_(r), gps_(g), store_(st), sstore_(ss) {}
  int64_t utc() override { return clk_.utc(); }
  void status(JsonObject o) override;
  bool scheduleText(std::string *out) override;
  void scheduleInfo(JsonObject o) override;
  bool uploadSchedule(const char *json, size_t len, app::Messages *err, app::Messages *warn) override;
  int  nights(int n, std::string *ndjson) override;
  bool setTime(int64_t utc) override { return clk_.setUtc(utc); }
 private:
  Clock &clk_; Motion &mot_; Sensors &sens_; As5600 &as_; Ds3231 &rtc_; Gps &gps_;
  Store &store_; ScheduleStore &sstore_;
};

namespace web {
void begin(net::Api &api);
void radio(app::Radio r);      // ISystem::radio lands here
void service();                // every loop
bool apOn();
const char *ssid();
int  clients();
// one request through the same Api, for the serial console's `H`
net::Response call(const char *method, const char *path_and_query, const char *body);
}  // namespace web

}  // namespace esp
