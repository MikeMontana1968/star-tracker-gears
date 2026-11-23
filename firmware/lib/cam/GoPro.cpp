#include "GoPro.h"
#include <ArduinoJson.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

namespace cam {

using app::Op;

Op GoPro::fail(const char *fmt, ...) {
  va_list ap; va_start(ap, fmt);
  vsnprintf(st_.error, sizeof st_.error, fmt, ap);
  va_end(ap);
  return Op::Fail;
}

bool GoPro::cmd(const char *path) {
  if (!path || !path[0]) return true;           // not configured: nothing to send
  std::string b;
  int code = io_.get(path, &b, p_.http_timeout_ms);
  return code == 200;
}

// one /gp/gpControl/status read; keeps the body for the settings read-back
bool GoPro::status() {
  std::string b;
  int code = io_.get("/gp/gpControl/status", &b, p_.http_timeout_ms);
  st_.reachable = code == 200;
  if (!st_.reachable) return false;
  JsonDocument d;
  if (deserializeJson(d, b)) { st_.reachable = false; return false; }
  // A sleeping HERO5 keeps its AP up and answers this with 200 and
  // {"state":{"shutdown_pending":true}} -- no status block. That is asleep, not awake
  // (found 2026-11-22: counting it as awake skipped the wake-on-LAN).
  st_.asleep = !d["status"].is<JsonObjectConst>() || (d["state"]["shutdown_pending"] | false);
  if (st_.asleep) return false;
  JsonObjectConst s = d["status"];
  char k[8];
  snprintf(k, sizeof k, "%d", p_.key_recording); st_.recording = (s[k] | 0) != 0;
  snprintf(k, sizeof k, "%d", p_.key_battery);   st_.battery_pct = s[k] | -1;
  snprintf(k, sizeof k, "%d", p_.key_sd_kb);     st_.sd_free_kb = s[k] | -1L;
  if (p_.mode_key >= 0)    { snprintf(k, sizeof k, "%d", p_.mode_key);    st_.mode = s[k] | -1; }
  if (p_.submode_key >= 0) { snprintf(k, sizeof k, "%d", p_.submode_key); st_.submode = s[k] | -1; }
  last_.swap(b);
  return true;
}

bool GoPro::waitJoin(uint64_t until) {
  while (!io_.joined()) {
    if (io_.cancelled() || io_.ms() >= until) return false;
    io_.delay(250);
  }
  return true;
}

// awake = two full status reads in a row, 1 s apart: a dozing HERO5 can answer one
// full status and then fall back to shutdown_pending (seen 2026-11-22)
bool GoPro::awakeNow() {
  if (!io_.joined() || !status()) return false;
  io_.delay(1000);
  return io_.joined() && status();
}

bool GoPro::waitAwake(uint64_t until) {
  while (io_.ms() < until && !io_.cancelled()) {
    if (awakeNow()) return true;
    io_.delay(1000);
  }
  return false;
}

Op GoPro::wake() {
  st_.error[0] = 0;
  if (!p_.valid) return fail("no camera profile");
  uint64_t t0 = io_.ms(), until = t0 + p_.wake_ms;
  for (int attempt = 0; attempt < 2; attempt++) {
    if (attempt == 1) {                          // second try: cycle the rail first
      if (io_.cancelled()) return fail("cancelled");
      io_.rail(false); io_.delay(2000); io_.rail(true); io_.delay(3000);
    }
    uint64_t join_until = io_.ms() + p_.join_ms;
    if (join_until > until) join_until = until;
    if (waitJoin(join_until)) {
      // wake-on-LAN first, always: harmless to an awake camera, and the status
      // alone cannot be trusted to say a HERO5 is awake
      bool sent = false;
      if (p_.has_mac)
        for (int i = 0; i < 3; i++) { io_.wol(p_.mac); io_.delay(300); sent = true; }
      if (!sent && awakeNow()) return Op::Ok;    // no MAC: only the status to go on
      uint64_t poll_until = io_.ms() + 15000;
      if (poll_until > until) poll_until = until;
      if (waitAwake(poll_until)) {
        if (attempt == 0) st_.wakes_by_wol++; else st_.wakes_by_rail++;
        return Op::Ok;
      }
    }
    if (io_.cancelled()) return fail("cancelled");
    if (io_.ms() >= until) break;
  }
  return fail(io_.joined() ? "joined the camera AP, but it never answered" : "could not join the camera AP '%s'", p_.ssid);
}

Op GoPro::apply(const Setting *set, int n) {
  for (int i = 0; i < n; i++) {
    const Setting &s = set[i];
    char path[48];
    snprintf(path, sizeof path, "/gp/gpControl/setting/%u/%ld", (unsigned)s.id, (long)s.value);
    if (!cmd(path)) return fail("setting %s (%u=%ld) refused", s.name, (unsigned)s.id, (long)s.value);
    if (io_.cancelled()) return fail("cancelled");
  }
  return Op::Ok;
}

Op GoPro::readBack(const Setting *set, int n, const Preset *ov) {
  JsonDocument d;
  deserializeJson(d, last_);
  JsonObjectConst got_all = d["settings"];
  for (int i = 0; i < n; i++) {
    const Setting &s = set[i];
    bool overridden = false;                    // the preset set this id last: it wins
    for (int j = 0; ov && j < ov->n_settings; j++) if (ov->settings[j].id == s.id) overridden = true;
    if (overridden) continue;
    char k[8]; snprintf(k, sizeof k, "%u", (unsigned)s.id);
    long got = got_all[k] | -1L;
    if (got != s.value) return fail("setting %s (%u) reads %ld, wanted %ld", s.name, (unsigned)s.id, got, (long)s.value);
  }
  return Op::Ok;
}

Op GoPro::configure(const char *preset) {
  st_.error[0] = 0;
  const Preset *pre = nullptr;
  if (preset && preset[0] && p_.n_presets > 0) {
    pre = p_.preset(preset);
    if (!pre) return fail("no camera preset '%s' in the profile", preset);
  }
  if (!cmd(p_.cmd_mode)) return fail("mode command refused");
  io_.delay(500);
  if (apply(p_.settings, p_.n_settings) != Op::Ok) return Op::Fail;
  if (pre && apply(pre->settings, pre->n_settings) != Op::Ok) return Op::Fail;
  io_.delay(500);
  if (!status()) return fail("no status after configuring");
  if (p_.mode >= 0 && st_.mode != p_.mode) return fail("mode is %d, wanted %d", st_.mode, p_.mode);
  if (p_.submode >= 0 && st_.submode != p_.submode) return fail("sub-mode is %d, wanted %d", st_.submode, p_.submode);
  // read every setting back from the same status body
  if (readBack(p_.settings, p_.n_settings, pre) != Op::Ok) return Op::Fail;
  if (pre && readBack(pre->settings, pre->n_settings) != Op::Ok) return Op::Fail;
  if (st_.sd_free_kb >= 0 && st_.sd_free_kb < 50 * 1024) return fail("SD card nearly full (%ld KB)", st_.sd_free_kb);
  return Op::Ok;
}

bool GoPro::waitRecording(bool want, uint32_t within_ms) {
  uint64_t until = io_.ms() + within_ms;
  while (io_.ms() < until && !io_.cancelled()) {
    if (status() && st_.recording == want) return true;
    io_.delay(1000);
  }
  return false;
}

Op GoPro::start() {
  st_.error[0] = 0;
  if (!cmd(p_.cmd_shutter_on)) return fail("shutter on refused");
  if (!waitRecording(true, 10000)) return fail("shutter on, but it is not recording");
  return Op::Ok;
}

Op GoPro::stop() {
  st_.error[0] = 0;
  // the camera AP may need a moment to take us back after hours away
  if (!waitJoin(io_.ms() + p_.join_ms)) return fail("could not rejoin the camera AP to stop it");
  bool sent = false;
  for (int i = 0; i < 3 && !sent; i++) { sent = cmd(p_.cmd_shutter_off); if (!sent) io_.delay(1000); }
  if (!sent) return fail("shutter off not answered");
  if (!waitRecording(false, p_.stop_ms)) return fail("shutter off, but it is still recording");
  if (p_.sleep_after_stop) cmd(p_.cmd_sleep);    // best effort; the rail goes off next anyway
  return Op::Ok;
}

Op GoPro::discover(long *bytes) {
  st_.error[0] = 0;
  *bytes = 0;
  static const char *const paths[][2] = {
    {"/gp/gpControl/info", "/camera_info.json"},
    {"/gp/gpControl/status", "/camera_status.json"},
    {"/gp/gpControl", "/camera_dump.json"},
  };
  for (auto &pf : paths) {
    long n = io_.save(pf[0], pf[1], p_.http_timeout_ms * 4);
    if (n < 0) return fail("%s did not answer", pf[0]);
    *bytes += n;
  }
  status();
  return Op::Ok;
}

}  // namespace cam
