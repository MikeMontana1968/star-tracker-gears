// =====================================================================
//  CamProfile -- everything camera-specific, as DATA (FIRMWARE.md s6, D6):
//  "never hardcode a gpControl setting ID". Lives on LittleFS as
//  /camera_profile.json; the example in firmware/web/camera_profile.example.json
//  carries the HERO5/6 values the community documents, UNVERIFIED on this
//  camera until `discover` has dumped its real settings tree.
//
//  {
//    "model": "HERO6",
//    "wifi": { "ssid": "GP12345678", "password": "..." },   from the camera's
//    "mac": "AA:BB:CC:DD:EE:FF",                            Connections menu
//    "commands": {
//      "mode":        "/gp/gpControl/command/sub_mode?mode=2&sub_mode=2",
//      "shutter_on":  "/gp/gpControl/command/shutter?p=1",
//      "shutter_off": "/gp/gpControl/command/shutter?p=0",
//      "sleep":       "/gp/gpControl/command/system/sleep"
//    },
//    "verify_mode": { "mode_key": 43, "mode": 2, "submode_key": 44, "submode": 2 },
//    "settings": [ { "name": "protune", "id": 34, "value": 1 }, ... ],   always applied
//    "presets": {                                        optional; a schedule night
//      "astro_30s":   [ { "name": "shutter", "id": 31, "value": 6 }, ... ],   picks one
//      "moon_20s_1m": [ ... ]                            with "camera": "<name>"
//    },
//    "status_keys": { "recording": 8, "battery_pct": 70, "sd_free_kb": 54 },
//    "sleep_after_stop": true,
//    "timing": { "join_s": 20, "wake_s": 45, "http_timeout_ms": 2500 }
//  }
//
//  Pure C++ (ArduinoJson), host-tested in test/test_cam.
// =====================================================================
#pragma once
#include <stddef.h>
#include <stdint.h>
#include "Schedule.h"     // app::Messages

namespace cam {

struct Setting {
  char     name[24] = {};
  uint16_t id = 0;
  int32_t  value = 0;
};

// a named set of settings a schedule night picks with "camera": "<name>"
struct Preset {
  static constexpr int MAX_SETTINGS = 10;
  char    name[24] = {};
  Setting settings[MAX_SETTINGS];
  int     n_settings = 0;
};

struct Profile {
  static constexpr int MAX_SETTINGS = 16;
  static constexpr int MAX_PRESETS = 6;
  bool     valid = false;
  char     model[16] = {};
  char     ssid[33] = {};
  char     pass[65] = {};
  uint8_t  mac[6] = {};
  bool     has_mac = false;
  char     cmd_mode[112] = {};
  char     cmd_shutter_on[80] = {};
  char     cmd_shutter_off[80] = {};
  char     cmd_sleep[80] = {};
  int      mode_key = -1, mode = -1, submode_key = -1, submode = -1;   // -1: not verified
  Setting  settings[MAX_SETTINGS];      // always applied (mode-wide: protune, LEDs, auto-off...)
  int      n_settings = 0;
  Preset   presets[MAX_PRESETS];       // then the night's preset on top
  int      n_presets = 0;
  const Preset *preset(const char *name) const;
  int      key_recording = 10, key_battery = 70, key_sd_kb = 54;
  bool     sleep_after_stop = true;
  uint32_t join_ms = 20000, wake_ms = 45000, http_timeout_ms = 2500;
  uint32_t stop_ms = 45000;   // shutter off -> not recording: a night lapse finishes its exposure first
};

// Validates and fills *p. Every problem goes to *err with a path; false if any.
bool parseProfile(const char *json, size_t len, Profile *p, app::Messages *err);

// "AA:BB:CC:DD:EE:FF" (or with '-') -> 6 bytes
bool parseMac(const char *s, uint8_t mac[6]);

}  // namespace cam
