// =====================================================================
//  Api -- the HTTP API of FIRMWARE.md s7 as a pure function of a request:
//  routing, JSON in and out, and the mapping onto Fsm::command(). The
//  device's WebServer (src/Net.cpp) is a thin adapter over handle(); the
//  host tests (test/test_net) drive the same code against the simulator,
//  and the serial console's `H` command drives it on the board.
//
//    GET  /api/status          the status struct, ~1 Hz polling
//    GET  /api/schedule        the stored schedule text + validation state
//    PUT  /api/schedule        upload: 200 + warnings, or 422 + the error list
//    POST /api/command         {"cmd": halt | end_night | goto | jog | home |
//                               clear_fault | test_track | sleep_now | extend,
//                               "deg": n, "minutes": n}
//    GET  /api/log?since=N     log lines since N: {next, lost, lines}
//    GET  /api/night/last      the last night report
//    GET  /api/night/list?n=10 the last n reports, newest last
//    POST /api/time            {"utc": seconds} sets the DS3231
//
//  Every request counts as activity: CONFIG_AP stays up while a page polls.
//  Refused commands are 409 with the FSM's reason, never a silent 200.
// =====================================================================
#pragma once
#include <stdint.h>
#include <stddef.h>
#include <string>
#include <ArduinoJson.h>
#include "Fsm.h"
#include "Schedule.h"
#include "LogRing.h"

namespace net {

struct Request {
  const char *method = "GET";
  const char *path = "/";
  const char *query = "";           // without the '?'
  const char *body = "";
  size_t body_len = 0;
};

struct Response {
  int code = 200;
  std::string body;
  const char *type = "application/json";
};

// what only the device knows
struct Host {
  virtual int64_t utc() = 0;                          // the device clock, seconds
  // hardware fields of the status struct: position, encoder, RTC, GPS,
  // battery, schedule, radio, memory (keys as in FIRMWARE.md s7)
  virtual void status(JsonObject o) = 0;
  virtual bool scheduleText(std::string *out) = 0;
  virtual void scheduleInfo(JsonObject o) = 0;        // valid, source, next night
  virtual bool uploadSchedule(const char *json, size_t len, app::Messages *err, app::Messages *warn) = 0;
  // the last `n` night reports, one JSON object per line, oldest first
  virtual int  nights(int n, std::string *ndjson) = 0;
  virtual bool setTime(int64_t utc) = 0;
  virtual ~Host() {}
};

class Api {
 public:
  Api(Host &h, LogRing &log) : host_(h), log_(log) {}
  void attach(app::Fsm *f) { fsm_ = f; }            // the FSM is rebuilt on every boot
  Response handle(const Request &r);

  static const char *radioName(app::Radio r);

 private:
  Response status();
  Response command(const Request &r);
  Response getSchedule();
  Response putSchedule(const Request &r);
  Response logSince(const Request &r);
  Response nights(const Request &r, bool last_only);
  Response setTime(const Request &r);
  static Response error(int code, const char *msg);
  static long queryInt(const char *q, const char *key, long dflt);

  Host &host_;
  LogRing &log_;
  app::Fsm *fsm_ = nullptr;
};

}  // namespace net
