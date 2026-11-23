#include "CamProfile.h"
#include <ArduinoJson.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace cam {

bool parseMac(const char *s, uint8_t mac[6]) {
  if (!s) return false;
  for (int i = 0; i < 6; i++) {
    if (!isxdigit((unsigned char)s[0]) || !isxdigit((unsigned char)s[1])) return false;
    char h[3] = {s[0], s[1], 0};
    mac[i] = (uint8_t)strtol(h, nullptr, 16);
    s += 2;
    if (i < 5) { if (*s != ':' && *s != '-') return false; s++; }
  }
  return *s == 0;
}

static bool copyStr(JsonVariantConst v, char *dst, size_t cap, const char *path, bool required,
                    app::Messages *err, bool must_be_path = false) {
  if (v.isNull()) {
    if (required) err->add("%s: missing", path);
    return !required;
  }
  if (!v.is<const char *>()) { err->add("%s: must be a string", path); return false; }
  const char *s = v.as<const char *>();
  size_t n = strlen(s);
  if (required && n == 0) { err->add("%s: must not be empty", path); return false; }
  if (n >= cap) { err->add("%s: longer than %u characters", path, (unsigned)(cap - 1)); return false; }
  if (must_be_path && s[0] != '/') { err->add("%s: must start with '/'", path); return false; }
  memcpy(dst, s, n + 1);
  return true;
}

static int intOr(JsonVariantConst v, int dflt, const char *path, int lo, int hi, app::Messages *err) {
  if (v.isNull()) return dflt;
  if (!v.is<int>()) { err->add("%s: must be a whole number", path); return dflt; }
  int x = v.as<int>();
  if (x < lo || x > hi) { err->add("%s: %d is outside %d..%d", path, x, lo, hi); return dflt; }
  return x;
}

static void parseSettings(JsonVariantConst sv, const char *where, Setting *out, int max, int *n,
                          app::Messages *err) {
  *n = 0;
  if (sv.isNull()) return;
  if (!sv.is<JsonArrayConst>()) { err->add("%s: must be an array", where); return; }
  int i = 0;
  for (JsonObjectConst s : sv.as<JsonArrayConst>()) {
    char path[64];
    if (i >= max) { err->add("%s: more than %d", where, max); break; }
    Setting &t = out[i];
    t = Setting();
    snprintf(path, sizeof path, "%s[%d].name", where, i);
    copyStr(s["name"], t.name, sizeof t.name, path, false, err);
    snprintf(path, sizeof path, "%s[%d].id", where, i);
    if (!s["id"].is<int>()) err->add("%s: missing or not a whole number", path);
    else t.id = (uint16_t)intOr(s["id"], 0, path, 1, 999, err);
    snprintf(path, sizeof path, "%s[%d].value", where, i);
    if (!s["value"].is<int>()) err->add("%s: missing or not a whole number", path);
    else t.value = intOr(s["value"], 0, path, 0, 100000, err);
    i++;
  }
  *n = i;
}

const Preset *Profile::preset(const char *name) const {
  for (int i = 0; i < n_presets; i++) if (!strcmp(presets[i].name, name)) return &presets[i];
  return nullptr;
}

bool parseProfile(const char *json, size_t len, Profile *p, app::Messages *err) {
  *p = Profile();
  if (len > 6000) { err->add("profile: over 6000 bytes"); return false; }
  JsonDocument d;
  DeserializationError e = deserializeJson(d, json, len);
  if (e) { err->add("profile: not JSON (%s)", e.c_str()); return false; }
  JsonObjectConst o = d.as<JsonObjectConst>();
  if (o.isNull()) { err->add("profile: must be an object"); return false; }
  int before = err->n + err->dropped;

  copyStr(o["model"], p->model, sizeof p->model, "model", false, err);
  JsonObjectConst w = o["wifi"];
  if (w.isNull()) err->add("wifi: missing (ssid + password from the camera's Connections menu)");
  else {
    copyStr(w["ssid"], p->ssid, sizeof p->ssid, "wifi.ssid", true, err);
    copyStr(w["password"], p->pass, sizeof p->pass, "wifi.password", true, err);
    if (p->pass[0] && strlen(p->pass) < 8) err->add("wifi.password: WPA2 needs at least 8 characters");
  }
  if (!o["mac"].isNull()) {
    const char *m = o["mac"] | "";
    if (parseMac(m, p->mac)) p->has_mac = true;
    else err->add("mac: '%s' is not AA:BB:CC:DD:EE:FF", m);
  }

  JsonObjectConst c = o["commands"];
  if (c.isNull()) err->add("commands: missing");
  else {
    copyStr(c["mode"], p->cmd_mode, sizeof p->cmd_mode, "commands.mode", false, err, true);
    copyStr(c["shutter_on"], p->cmd_shutter_on, sizeof p->cmd_shutter_on, "commands.shutter_on", true, err, true);
    copyStr(c["shutter_off"], p->cmd_shutter_off, sizeof p->cmd_shutter_off, "commands.shutter_off", true, err, true);
    copyStr(c["sleep"], p->cmd_sleep, sizeof p->cmd_sleep, "commands.sleep", false, err, true);
  }

  JsonObjectConst vm = o["verify_mode"];
  if (!vm.isNull()) {
    p->mode_key = intOr(vm["mode_key"], -1, "verify_mode.mode_key", 0, 999, err);
    p->mode = intOr(vm["mode"], -1, "verify_mode.mode", 0, 99, err);
    p->submode_key = intOr(vm["submode_key"], -1, "verify_mode.submode_key", 0, 999, err);
    p->submode = intOr(vm["submode"], -1, "verify_mode.submode", 0, 99, err);
  }

  parseSettings(o["settings"], "settings", p->settings, Profile::MAX_SETTINGS, &p->n_settings, err);

  JsonVariantConst pv = o["presets"];
  if (!pv.isNull()) {
    if (!pv.is<JsonObjectConst>()) err->add("presets: must be an object of name: [settings]");
    else {
      for (JsonPairConst kv : pv.as<JsonObjectConst>()) {
        if (p->n_presets >= Profile::MAX_PRESETS) { err->add("presets: more than %d", Profile::MAX_PRESETS); break; }
        Preset &q = p->presets[p->n_presets];
        const char *name = kv.key().c_str();
        char path[48];
        snprintf(path, sizeof path, "presets.%.30s", name);
        if (!name[0] || strlen(name) >= sizeof q.name) { err->add("%s: name must be 1-%d characters", path, (int)sizeof q.name - 1); continue; }
        if (p->preset(name)) { err->add("%s: listed twice", path); continue; }
        strcpy(q.name, name);
        parseSettings(kv.value(), path, q.settings, Preset::MAX_SETTINGS, &q.n_settings, err);
        p->n_presets++;
      }
    }
  }

  JsonObjectConst k = o["status_keys"];
  if (!k.isNull()) {
    p->key_recording = intOr(k["recording"], p->key_recording, "status_keys.recording", 1, 999, err);
    p->key_battery = intOr(k["battery_pct"], p->key_battery, "status_keys.battery_pct", 1, 999, err);
    p->key_sd_kb = intOr(k["sd_free_kb"], p->key_sd_kb, "status_keys.sd_free_kb", 1, 999, err);
  }
  if (!o["sleep_after_stop"].isNull()) p->sleep_after_stop = o["sleep_after_stop"] | true;

  JsonObjectConst t = o["timing"];
  if (!t.isNull()) {
    p->join_ms = (uint32_t)intOr(t["join_s"], 20, "timing.join_s", 3, 120, err) * 1000;
    p->wake_ms = (uint32_t)intOr(t["wake_s"], 45, "timing.wake_s", 5, 140, err) * 1000;
    p->stop_ms = (uint32_t)intOr(t["stop_s"], 45, "timing.stop_s", 5, 120, err) * 1000;
    p->http_timeout_ms = (uint32_t)intOr(t["http_timeout_ms"], 2500, "timing.http_timeout_ms", 300, 10000, err);
  }

  p->valid = err->n + err->dropped == before;
  return p->valid;
}

}  // namespace cam
