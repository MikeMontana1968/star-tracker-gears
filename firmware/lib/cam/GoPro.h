// =====================================================================
//  GoPro -- the HERO5/6-family gpControl sequences (FIRMWARE.md s6) over a
//  small Io interface, so the logic is host-tested against a simulated
//  camera (test/test_cam) and the ESP32 supplies WiFi + HTTP + UDP + the
//  rail switch (src/EspCamera.cpp, run in a task on core 0: these calls
//  BLOCK for up to http_timeout_ms each, which the FSM never may).
//
//    wake()       wait for the STA join; status answers -> awake. Else
//                 wake-on-LAN x3 (UDP 9, unicast + broadcast) and poll; still
//                 nothing -> cycle the camera rail (GPIO 32) and try once more
//    configure()  mode command, every profile setting, then READ BACK the
//                 status/settings and fail on any mismatch, with which one
//    start()      shutter on, until the recording key reads 1
//    stop()       shutter off, until it reads 0; then sleep (profile)
//    discover()   /gp/gpControl/info, /status and the whole /gp/gpControl
//                 settings tree, each saved through Io::save()
//
//  Every loop checks Io::cancelled() and its own deadline; nothing waits
//  forever. Camera status is kept in state() for /api/status.
// =====================================================================
#pragma once
#include <stdint.h>
#include <string>
#include "CamProfile.h"
#include "Platform.h"

namespace cam {

struct Io {
  virtual uint64_t ms() = 0;
  virtual void     delay(uint32_t ms) = 0;
  virtual bool     joined() = 0;                       // STA associated, IP up
  // GET http://10.5.5.9<path>: HTTP status, or -1 when nothing answered
  virtual int      get(const char *path, std::string *body, uint32_t timeout_ms) = 0;
  // GET and stream the body into a file; bytes written, or -1
  virtual long     save(const char *path, const char *file, uint32_t timeout_ms) = 0;
  virtual bool     wol(const uint8_t mac[6]) = 0;
  virtual void     rail(bool on) = 0;
  virtual bool     cancelled() { return false; }
  virtual ~Io() {}
};

struct State {
  bool     reachable = false;     // the last status call answered
  bool     recording = false;
  int      battery_pct = -1;
  long     sd_free_kb = -1;
  int      mode = -1, submode = -1;
  uint32_t wakes_by_wol = 0, wakes_by_rail = 0;
  char     error[80] = {};
};

class GoPro {
 public:
  GoPro(Io &io, const Profile &p) : io_(io), p_(p) {}
  app::Op wake();
  app::Op configure();
  app::Op start();
  app::Op stop();
  app::Op discover(long *bytes);
  const State &state() const { return st_; }
  bool status();                 // one status read into state(); false if no answer

 private:
  bool waitJoin(uint64_t until);
  bool waitAwake(uint64_t until);
  bool waitRecording(bool want, uint32_t within_ms);
  bool cmd(const char *path);
  app::Op fail(const char *fmt, ...);
  Io &io_;
  const Profile &p_;
  State st_;
  std::string last_;             // the last status body (settings read-back)
};

}  // namespace cam
