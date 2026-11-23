#include "Api.h"
#include "CivilTime.h"
#include "Moon.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace net {

using namespace app;


const char *Api::radioName(Radio r) { return r == Radio::Ap ? "ap" : r == Radio::Sta ? "sta" : "off"; }
static const char *opName(Op o) { return o == Op::Ok ? "ok" : o == Op::Fail ? "fail" : "not_tried"; }
static const char *policyName(CamPolicy c) {
  return c == CamPolicy::None ? "none" : c == CamPolicy::Required ? "required" : "best_effort";
}

Response Api::error(int code, const char *msg) {
  Response r;
  r.code = code;
  JsonDocument d;
  d["ok"] = false;
  d["error"] = msg;
  serializeJson(d, r.body);
  return r;
}

long Api::queryInt(const char *q, const char *key, long dflt) {
  size_t k = strlen(key);
  for (const char *p = q; p && *p;) {
    if (!strncmp(p, key, k) && p[k] == '=') return strtol(p + k + 1, nullptr, 10);
    p = strchr(p, '&');
    if (p) p++;
  }
  return dflt;
}

Response Api::handle(const Request &r) {
  if (!fsm_) return error(503, "starting");
  fsm_->touch();                                     // someone is looking
  std::string path = r.path;
  bool get = !strcmp(r.method, "GET"), post = !strcmp(r.method, "POST"), put = !strcmp(r.method, "PUT");
  if (path == "/api/status")      return get ? status() : error(405, "GET");
  if (path == "/api/command")     return post ? command(r) : error(405, "POST");
  if (path == "/api/schedule") {
    if (get) return getSchedule();
    if (put || post) return putSchedule(r);
    if (!strcmp(r.method, "DELETE")) {
      bool ok = host_.deleteSchedule();
      if (!ok) return error(500, "delete failed");
      Response out;
      out.body = "{\"ok\":true,\"msg\":\"schedule deleted: nothing is planned\"}";
      return out;
    }
    return error(405, "GET, PUT or DELETE");
  }
  if (path == "/api/schedule/check")  return post || put ? checkSchedule(r) : error(405, "POST");
  if (path == "/api/schedule/nights") return get ? scheduleNights(r) : error(405, "GET");
  if (path == "/api/log")         return get ? logSince(r) : error(405, "GET");
  if (path == "/api/night/last")  return get ? nights(r, true) : error(405, "GET");
  if (path == "/api/night/list")  return get ? nights(r, false) : error(405, "GET");
  if (path == "/api/time")        return post ? setTime(r) : error(405, "POST");
  if (path == "/api/camera/profile") {
    if (get) {
      std::string j;
      if (!host_.cameraProfile(&j)) return error(404, "no camera profile on the device");
      Response out; out.body = j; return out;
    }
    if (put || post) {
      static Messages err;
      err = Messages();
      bool ok = host_.putCameraProfile(r.body, r.body_len, &err);
      JsonDocument d;
      d["ok"] = ok;
      JsonArray e = d["errors"].to<JsonArray>();
      for (int i = 0; i < err.n; i++) e.add(err.msg[i]);
      Response out; out.code = ok ? 200 : 422;
      serializeJson(d, out.body);
      return out;
    }
    if (!strcmp(r.method, "DELETE")) {
      if (!host_.deleteCameraProfile()) return error(409, "camera busy or no profile");
      Response out; out.body = "{\"ok\":true,\"msg\":\"camera profile deleted: camera steps now fail at once\"}"; return out;
    }
    return error(405, "GET, PUT or DELETE");
  }
  if (path == "/api/camera/dump") {
    if (!get) return error(405, "GET");
    JsonDocument d;
    host_.cameraFiles(d.to<JsonObject>());
    Response out; serializeJson(d, out.body); return out;
  }
  return error(404, "no such endpoint");
}

// ---- GET /api/status: the contract (FIRMWARE.md s7) -------------------------
Response Api::status() {
  JsonDocument d;
  const Fsm &f = *fsm_;
  int64_t now = host_.utc();
  d["state"] = stateName(f.state());
  d["state_s"] = f.stateMs() / 1000;
  uint32_t left = f.deadlineLeftMs();
  if (left) d["deadline_remaining_s"] = (left + 999) / 1000; else d["deadline_remaining_s"] = nullptr;
  d["fault_code"] = (int)f.fault();
  d["fault_text"] = faultName(f.fault());
  d["move_failed"] = f.moveFailed();
  d["radio_mode"] = radioName(f.radio());
  if (f.nightActive()) {
    const NightReport &n = f.night();
    JsonObject o = d["night"].to<JsonObject>();
    o["test"] = n.plan.test;
    o["start_utc"] = n.plan.start_utc;
    o["stop_utc"] = n.plan.stop_utc;
    o["start_deg"] = n.plan.start_deg;
    o["camera_policy"] = policyName(n.plan.cam);
    o["camera"] = opName(n.camera);
    o["gps"] = opName(n.gps);
    o["max_div_deg"] = n.max_div_deg;
    o["min_battery_v"] = n.min_battery_v;
    o["warnings"] = n.warnings;
    o["resumes"] = n.resumes;
    o["rtc_drift_s"] = n.rtc_drift_s;
    if (f.state() == State::Track) {
      int64_t el = now - n.plan.start_utc;
      o["tracking_elapsed_s"] = el > 0 ? el : 0;
      o["tracking_remaining_s"] = n.plan.stop_utc > now ? n.plan.stop_utc - now : 0;
      o["expected_deg"] = n.plan.start_deg + (el > 0 ? el : 0) * rateDegPerS(n.plan.rate);
    }
  } else {
    d["night"] = nullptr;
  }
  d["coils"] = f.coils();
  static const char *const dn[] = {"none", "running", "ok", "failed"};
  d["camera_discover"] = dn[f.discoverState() & 3];
  const DiagState &g = f.diagState();
  if (g.kind != DiagKind::None) {
    JsonObject o = d["diag"].to<JsonObject>();
    o["kind"] = diagName(g.kind);
    o["running"] = g.running;
    o["ok"] = g.ok;
    o["msg"] = (const char *)g.msg;
    o["base_deg"] = g.base;
    o["out_deg"] = g.out;
    o["cycles"] = g.cycles;
    o["overshoot_deg"] = g.overshoot;
    if (g.slew_hz) o["slew_hz"] = g.slew_hz;
    o["done"] = g.n_ret;
    JsonArray r = o["return_err_arcsec"].to<JsonArray>();
    for (int i = 0; i < g.n_ret; i++) r.add(round(g.ret_err_deg[i] * 36000) / 10);
    JsonArray a = o["out_err_deg"].to<JsonArray>();
    for (int i = 0; i < g.n_out; i++) a.add(round(g.out_err_deg[i] * 10000) / 10000);
    if (!g.running && g.ok) {
      o["return_mean_arcsec"] = round(g.ret_mean_as * 10) / 10;
      o["return_sd_arcsec"] = round(g.ret_sd_as * 10) / 10;
      o["return_max_arcsec"] = round(g.ret_max_as * 10) / 10;
      o["out_mean_err_deg"] = round(g.out_mean_deg * 10000) / 10000;
    }
  } else {
    d["diag"] = nullptr;
  }
  d["log_next"] = log_.next();
  host_.status(d.as<JsonObject>());
  Response r;
  serializeJson(d, r.body);
  return r;
}

// ---- POST /api/command --------------------------------------------------------
Response Api::command(const Request &r) {
  JsonDocument in;
  if (deserializeJson(in, r.body, r.body_len)) return error(400, "body is not JSON");
  const char *c = in["cmd"] | "";
  double deg = in["deg"] | NAN;
  double minutes = in["minutes"] | NAN;
  CmdResult res;
  if (!strcmp(c, "halt"))             res = fsm_->command(Cmd::Halt);
  else if (!strcmp(c, "end_night"))   res = fsm_->command(Cmd::EndNight);
  else if (!strcmp(c, "goto"))        res = fsm_->command(Cmd::Goto, deg);
  else if (!strcmp(c, "jog"))         res = fsm_->command(Cmd::Jog, deg);
  else if (!strcmp(c, "home"))        res = fsm_->command(Cmd::Home);
  else if (!strcmp(c, "clear_fault")) res = fsm_->command(Cmd::ClearFault);
  else if (!strcmp(c, "test_track") && (in["camera"] | false)) {
    fsm_->testPreset(in["preset"] | "");
    res = fsm_->command(Cmd::TestTrackCamera, minutes);
  }
  else if (!strcmp(c, "test_track"))  res = fsm_->command((in["camera"] | false) ? Cmd::TestTrackCamera : Cmd::TestTrack, minutes);
  else if (!strcmp(c, "discover"))    res = fsm_->command(Cmd::Discover);
  else if (!strcmp(c, "sleep_now"))   res = fsm_->command(Cmd::SleepNow);
  else if (!strcmp(c, "coils"))       res = fsm_->command(Cmd::Coils, (in["on"] | false) ? 1 : 0);
  else if (!strcmp(c, "diag")) {
    const char *k = in["kind"] | "";
    DiagKind dk = !strcmp(k, "wiggle") ? DiagKind::Wiggle : !strcmp(k, "sweep") ? DiagKind::Sweep
                : !strcmp(k, "repeat") ? DiagKind::Repeat : DiagKind::None;
    if (dk == DiagKind::None) return error(400, "kind: wiggle, sweep or repeat");
    res = fsm_->diag(dk, deg, in["cycles"] | 1, in["overshoot_deg"] | NAN, in["slew_hz"] | 0u);
  }
  else if (!strcmp(c, "extend"))      res = {true, "config timeout restarted"};   // the touch did it
  else return error(400, "unknown cmd");
  Response out;
  out.code = res.ok ? 200 : 409;
  JsonDocument d;
  d["ok"] = res.ok;
  d[res.ok ? "msg" : "error"] = res.msg;
  d["state"] = stateName(fsm_->state());
  serializeJson(d, out.body);
  return out;
}

// ---- schedule -------------------------------------------------------------------
Response Api::getSchedule() {
  JsonDocument d;
  host_.scheduleInfo(d.to<JsonObject>());
  std::string text;
  if (host_.scheduleText(&text)) d["text"] = text; else d["text"] = nullptr;
  Response r;
  serializeJson(d, r.body);
  return r;
}

Response Api::putSchedule(const Request &r) {
  static Messages err, warn;                       // 1.5 KB each: off the stack
  err = Messages(); warn = Messages();
  bool ok = host_.uploadSchedule(r.body, r.body_len, &err, &warn);
  JsonDocument d;
  d["ok"] = ok;
  JsonArray e = d["errors"].to<JsonArray>();
  for (int i = 0; i < err.n; i++) e.add(err.msg[i]);
  if (err.dropped) d["errors_dropped"] = err.dropped;
  JsonArray w = d["warnings"].to<JsonArray>();
  for (int i = 0; i < warn.n; i++) w.add(warn.msg[i]);
  if (ok) host_.scheduleInfo(d["schedule"].to<JsonObject>());
  Response out;
  out.code = ok ? 200 : 422;
  serializeJson(d, out.body);
  return out;
}

// the nights a schedule resolves, from tonight: what the tracker WILL do
void Api::preview(Schedule &s, int n, JsonArray out) {
  int64_t now = host_.utc();
  int32_t tz = s.doc().tz_offset_min;
  int32_t today = (int32_t)((now + tz * 60LL) / 86400 - ((now + tz * 60LL) < 0 ? 1 : 0));
  int shown = 0;
  for (int32_t day = today - 1; day < today + 400 && shown < n; day++) {
    NightPlan p; const char *why = nullptr;
    bool ok = s.resolve(day, &p, &why);
    if (ok && p.stop_utc <= now) continue;            // already over
    if (!ok && day < today) continue;
    Civil c = fromUnix((int64_t)day * 86400);
    char date[12];
    snprintf(date, sizeof date, "%04d-%02d-%02d", c.year, c.month, c.day);
    JsonObject o = out.add<JsonObject>();
    o["date"] = date;
    if (!ok) { o["skipped"] = why ? why : "not scheduled"; }
    else {
      o["start_utc"] = p.start_utc;
      o["stop_utc"] = p.stop_utc;
      o["hours"] = round((p.stop_utc - p.start_utc) / 36.0) / 100;
      o["start_deg"] = p.start_deg;
      o["preset"] = (const char *)p.camera;
      o["end_deg"] = round((p.start_deg + (p.stop_utc - p.start_utc) * rateDegPerS(p.rate)) * 100) / 100;
      o["rate"] = p.rate == TrackRate::Lunar ? "lunar" : "sidereal";
      if (p.moon) {
        // for aiming at setup: where the moon is when tracking starts and ends
        moon::Pos a = moon::at(p.start_utc, s.doc().lat, s.doc().lon);
        moon::Pos b = moon::at(p.stop_utc, s.doc().lat, s.doc().lon);
        JsonObject m = o["moon"].to<JsonObject>();
        m["az_start"] = round(a.az_deg); m["alt_start"] = round(a.alt_deg);
        m["az_stop"] = round(b.az_deg);  m["alt_stop"] = round(b.alt_deg);
        m["illum_pct"] = round(a.illum * 100);
        m["waxing"] = a.waxing;
      }
      o["fallback"] = p.fallback;
      o["camera"] = (const char *)p.camera;
      o["camera_policy"] = policyName(p.cam);
    }
    shown++;
  }
}

Response Api::checkSchedule(const Request &r) {
  static Messages err, warn;
  err = Messages(); warn = Messages();
  Schedule *s = host_.checkSchedule(r.body, r.body_len, &err, &warn);
  JsonDocument d;
  d["ok"] = s != nullptr;
  JsonArray e = d["errors"].to<JsonArray>();
  for (int i = 0; i < err.n; i++) e.add(err.msg[i]);
  if (err.dropped) d["errors_dropped"] = err.dropped;
  JsonArray w = d["warnings"].to<JsonArray>();
  for (int i = 0; i < warn.n; i++) w.add(warn.msg[i]);
  if (s) preview(*s, (int)queryInt(r.query, "n", 14), d["nights"].to<JsonArray>());
  Response out;
  out.code = s ? 200 : 422;
  serializeJson(d, out.body);
  return out;
}

Response Api::scheduleNights(const Request &r) {
  Schedule *s = host_.currentSchedule();
  if (!s) return error(404, "no valid schedule on the device");
  JsonDocument d;
  long n = queryInt(r.query, "n", 14);
  if (n < 1) n = 1;
  if (n > 60) n = 60;
  preview(*s, (int)n, d["nights"].to<JsonArray>());
  Response out;
  serializeJson(d, out.body);
  return out;
}

// ---- GET /api/log?since=N ---------------------------------------------------------
Response Api::logSince(const Request &r) {
  JsonDocument d;
  long since = queryInt(r.query, "since", 0);
  JsonArray a = d["lines"].to<JsonArray>();
  uint32_t lost = log_.since(since > 0 ? (uint32_t)since : 0, [&](const LogRing::Entry &e) {
    JsonObject o = a.add<JsonObject>();
    o["seq"] = e.seq;
    o["utc"] = e.utc;
    o["msg"] = (const char *)e.msg;
  });
  d["next"] = log_.next();
  d["lost"] = lost;
  Response out;
  serializeJson(d, out.body);
  return out;
}

// ---- night reports: the NDJSON tail as a JSON array ------------------------------
Response Api::nights(const Request &r, bool last_only) {
  long n = last_only ? 1 : queryInt(r.query, "n", 10);
  if (n < 1) n = 1;
  if (n > 50) n = 50;
  std::string nd;
  int got = host_.nights((int)n, &nd);
  if (last_only && got == 0) return error(404, "no night reports yet");
  Response out;
  out.body = last_only ? "" : "[";
  bool first = true;
  size_t p = 0;
  while (p < nd.size()) {
    size_t e = nd.find('\n', p);
    if (e == std::string::npos) e = nd.size();
    std::string line = nd.substr(p, e - p);
    while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
    if (!line.empty()) {
      if (last_only) out.body = line;                 // the last one wins
      else { if (!first) out.body += ","; out.body += line; first = false; }
    }
    p = e + 1;
  }
  if (!last_only) out.body += "]";
  return out;
}

// ---- POST /api/time {"utc": s} ------------------------------------------------------
Response Api::setTime(const Request &r) {
  JsonDocument in;
  if (deserializeJson(in, r.body, r.body_len)) return error(400, "body is not JSON");
  int64_t t = in["utc"] | (int64_t)0;
  if (t < 1700000000LL || t > 4102444800LL) return error(400, "utc out of range");
  int64_t before = host_.utc();
  if (!host_.setTime(t)) return error(500, "RTC write failed");
  JsonDocument d;
  d["ok"] = true;
  d["was_off_s"] = before - t;
  Response out;
  serializeJson(d, out.body);
  return out;
}

}  // namespace net
