#include "Api.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace net {

using namespace app;

static const double SID_DEG_S = 360.0 / 86164.0905;

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
  if (path == "/api/schedule")    return get ? getSchedule() : put || post ? putSchedule(r) : error(405, "GET or PUT");
  if (path == "/api/log")         return get ? logSince(r) : error(405, "GET");
  if (path == "/api/night/last")  return get ? nights(r, true) : error(405, "GET");
  if (path == "/api/night/list")  return get ? nights(r, false) : error(405, "GET");
  if (path == "/api/time")        return post ? setTime(r) : error(405, "POST");
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
      o["expected_deg"] = n.plan.start_deg + (el > 0 ? el : 0) * SID_DEG_S;
    }
  } else {
    d["night"] = nullptr;
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
  else if (!strcmp(c, "test_track"))  res = fsm_->command(Cmd::TestTrack, minutes);
  else if (!strcmp(c, "sleep_now"))   res = fsm_->command(Cmd::SleepNow);
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
