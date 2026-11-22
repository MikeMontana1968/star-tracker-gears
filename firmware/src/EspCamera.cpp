#include "EspCamera.h"
#include <WiFi.h>
#include <WiFiUdp.h>
#include <HTTPClient.h>
#include <LittleFS.h>
#include "Board.h"
#include "Net.h"

namespace esp {

static const char *PROFILE_PATH = "/camera_profile.json";
static const char *MASK = "********";

// ---- the GoPro's Io on the ESP32 ---------------------------------------------------
class EspCamIo : public cam::Io {
 public:
  explicit EspCamIo(EspCamera &c) : cam_(c) {}
  uint64_t ms() override { return millis(); }
  void delay(uint32_t ms) override { vTaskDelay(pdMS_TO_TICKS(ms)); }
  bool joined() override { return (WiFi.getMode() & WIFI_STA) && WiFi.status() == WL_CONNECTED; }
  int get(const char *path, std::string *body, uint32_t timeout_ms) override {
    if (!joined()) return -1;
    WiFiClient c;
    HTTPClient h;
    h.setConnectTimeout(timeout_ms);
    h.setTimeout(timeout_ms);
    if (!h.begin(c, "10.5.5.9", 80, path)) return -1;
    int code = h.GET();
    if (code > 0) { String s = h.getString(); body->assign(s.c_str(), s.length()); }
    h.end();
    return code > 0 ? code : -1;
  }
  long save(const char *path, const char *file, uint32_t timeout_ms) override {
    if (!joined()) return -1;
    WiFiClient c;
    HTTPClient h;
    h.setConnectTimeout(timeout_ms);
    h.setTimeout(timeout_ms);
    if (!h.begin(c, "10.5.5.9", 80, path)) return -1;
    int code = h.GET();
    if (code != 200) { h.end(); return -1; }
    File f = LittleFS.open(file, "w");
    if (!f) { h.end(); return -1; }
    int n = h.writeToStream(&f);                  // handles chunked bodies
    f.close();
    h.end();
    return n < 0 ? -1 : n;
  }
  bool wol(const uint8_t mac[6]) override {
    uint8_t pkt[102];
    memset(pkt, 0xFF, 6);
    for (int i = 0; i < 16; i++) memcpy(pkt + 6 + i * 6, mac, 6);
    WiFiUDP u;
    bool ok = true;
    // unicast to the camera and broadcast on its subnet, port 9
    const IPAddress dst[] = {IPAddress(10, 5, 5, 9), IPAddress(10, 5, 5, 255)};
    for (const IPAddress &a : dst) {
      ok &= u.beginPacket(a, 9) && u.write(pkt, sizeof pkt) == sizeof pkt && u.endPacket();
    }
    return ok;
  }
  void rail(bool on) override { board::camera(on); }
  bool cancelled() override { return cam_.cancel_; }
 private:
  EspCamera &cam_;
};

static EspCamIo *s_io = nullptr;

// ---- profile -----------------------------------------------------------------------
static bool readAll(const char *path, String *out) {
  File f = LittleFS.open(path, "r");
  if (!f) return false;
  *out = f.readString();
  f.close();
  return true;
}

void EspCamera::applyProfile() {
  web::setStaCredentials(prof_.valid ? prof_.ssid : "", prof_.valid ? prof_.pass : "");
}

void EspCamera::begin() {
  String s;
  if (readAll(PROFILE_PATH, &s)) {
    static app::Messages err;
    err = app::Messages();
    if (!cam::parseProfile(s.c_str(), s.length(), &prof_, &err))
      Serial.printf("camera profile: INVALID (%s)\n", err.n ? err.msg[0] : "?");
    else
      Serial.printf("camera profile: %s, ssid '%s', %d settings%s\n", prof_.model, prof_.ssid, prof_.n_settings,
                    prof_.has_mac ? "" : ", no MAC (no wake-on-LAN)");
  } else {
    Serial.println("camera profile: none (upload one; camera steps fail until then)");
  }
  applyProfile();
  s_io = new EspCamIo(*this);
  gp_ = new cam::GoPro(*s_io, prof_);
  xTaskCreatePinnedToCore(taskMain, "camera", 8192, this, 1, &task_, 0);   // core 0, with WiFi
}

bool EspCamera::profileJson(std::string *out) {
  String s;
  if (!readAll(PROFILE_PATH, &s)) return false;
  JsonDocument d;
  if (deserializeJson(d, s)) { out->assign(s.c_str(), s.length()); return true; }
  if (d["wifi"]["password"].is<const char *>()) d["wifi"]["password"] = MASK;
  serializeJson(d, *out);
  return true;
}

bool EspCamera::putProfile(const char *json, size_t len, app::Messages *err) {
  if (busy_) { err->add("the camera is busy: try again after the current step"); return false; }
  // a masked password means "keep the stored one"
  JsonDocument d;
  if (deserializeJson(d, json, len)) { err->add("profile: not JSON"); return false; }
  if (d["wifi"]["password"].is<const char *>() && !strcmp(d["wifi"]["password"], MASK)) {
    if (!prof_.valid || !prof_.pass[0]) { err->add("wifi.password: no stored password to keep"); return false; }
    d["wifi"]["password"] = (const char *)prof_.pass;
  }
  std::string full;
  serializeJson(d, full);
  static cam::Profile trial;
  if (!cam::parseProfile(full.c_str(), full.size(), &trial, err)) return false;
  File f = LittleFS.open("/camera_profile.tmp", "w");
  if (!f) { err->add("filesystem: cannot write"); return false; }
  size_t w = f.write((const uint8_t *)full.data(), full.size());
  f.close();
  if (w != full.size()) { err->add("filesystem: short write"); LittleFS.remove("/camera_profile.tmp"); return false; }
  LittleFS.remove(PROFILE_PATH);
  if (!LittleFS.rename("/camera_profile.tmp", PROFILE_PATH)) { err->add("filesystem: rename failed"); return false; }
  prof_ = trial;
  applyProfile();
  Serial.printf("camera profile stored: %s, ssid '%s'\n", prof_.model, prof_.ssid);
  return true;
}

bool EspCamera::removeProfile() {
  if (busy_) return false;
  LittleFS.remove(PROFILE_PATH);
  prof_ = cam::Profile();
  applyProfile();
  Serial.println("camera profile deleted");
  return true;
}

// ---- steps -------------------------------------------------------------------------
void EspCamera::power(bool on) { powered_ = on; board::camera(on); }

void EspCamera::begin(app::CamStep s) {
  static const char *const names[] = {"wake", "config", "start", "stop", "discover"};
  strncpy(last_step_, names[(int)s], sizeof last_step_ - 1);
  if (!prof_.valid) { result_ = (uint8_t)app::Op::Fail; return; }
  cancel_ = false;
  step_ = (uint8_t)s;
  result_ = (uint8_t)app::Op::Pending;
  seq_++;
  xTaskNotifyGive(task_);
}

void EspCamera::taskMain(void *arg) { static_cast<EspCamera *>(arg)->run(); }

void EspCamera::run() {
  for (;;) {
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    uint32_t seq = seq_;
    app::CamStep s = (app::CamStep)step_;
    busy_ = true;
    app::Op r = app::Op::Fail;
    long bytes = 0;
    switch (s) {
      case app::CamStep::Wake:     r = gp_->wake(); break;
      case app::CamStep::Config:   r = gp_->configure(); break;
      case app::CamStep::Start:    r = gp_->start(); break;
      case app::CamStep::Stop:     r = gp_->stop(); break;
      case app::CamStep::Discover:
        r = gp_->wake();
        if (r == app::Op::Ok) r = gp_->discover(&bytes);
        break;
    }
    busy_ = false;
    Serial.printf("camera %s: %s%s%s\n", last_step_, r == app::Op::Ok ? "ok" : "FAILED",
                  r == app::Op::Ok ? "" : " -- ", r == app::Op::Ok ? "" : gp_->state().error);
    if (seq == seq_) result_ = (uint8_t)r;       // a newer begin() owns the result now
  }
}

// ---- reporting ---------------------------------------------------------------------
void EspCamera::status(JsonObject o) {
  const cam::State &s = gp_ ? gp_->state() : cam::State();
  o["profile"] = prof_.valid ? (const char *)prof_.model : "none";
  o["powered"] = powered_;
  o["joined"] = s_io ? s_io->joined() : false;
  o["busy"] = busy_;
  o["last_step"] = (const char *)last_step_;
  o["reachable"] = s.reachable;
  o["recording"] = s.recording;
  if (s.battery_pct >= 0) o["battery_pct"] = s.battery_pct;
  if (s.sd_free_kb >= 0) o["sd_free_mb"] = s.sd_free_kb / 1024;
  o["wakes_by_wol"] = s.wakes_by_wol;
  o["wakes_by_rail"] = s.wakes_by_rail;
  o["last_error"] = (const char *)s.error;
}

void EspCamera::files(JsonObject o) {
  static const char *const names[] = {"/camera_info.json", "/camera_status.json", "/camera_dump.json"};
  JsonArray a = o["files"].to<JsonArray>();
  for (const char *n : names) {
    File f = LittleFS.open(n, "r");
    if (!f) continue;
    JsonObject e = a.add<JsonObject>();
    e["name"] = n + 1;
    e["bytes"] = (uint32_t)f.size();
    e["url"] = String("/camera/") + (n + 8);      // "/camera_dump.json" -> "/camera/dump.json"
    f.close();
  }
  String info;
  if (readAll("/camera_info.json", &info)) {
    JsonDocument d;
    if (!deserializeJson(d, info)) {
      o["model"] = d["info"]["model_name"] | "?";
      o["firmware"] = d["info"]["firmware_version"] | "?";
      o["ap_mac"] = d["info"]["ap_mac"] | "";
      o["serial"] = d["info"]["serial_number"] | "";
    }
  }
}

}  // namespace esp
