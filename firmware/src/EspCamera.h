// =====================================================================
//  EspCamera -- app::ICamera for the HERO6 (slice 6): lib/cam's GoPro
//  sequences run in a FreeRTOS task on core 0, because HTTPClient and the
//  WiFi join block. The FSM only ever begin()s a step and poll()s it.
//
//    profile   /camera_profile.json on LittleFS (validated by cam::parseProfile);
//              its ssid/password are handed to the radio for STA mode
//    wake      the radio is already in STA (the FSM's table says so in CAM_*);
//              GoPro::wake waits for the join, then WoL, then the rail cycle
//    discover  /camera_info.json, /camera_status.json, /camera_dump.json
//
//  Without a valid profile every step fails at once (the old stub's
//  behaviour), so a best_effort night still tracks.
// =====================================================================
#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>
#include <string>
#include "Platform.h"
#include "CamProfile.h"
#include "GoPro.h"

namespace esp {

class EspCamera : public app::ICamera {
 public:
  void begin();                                   // load the profile, start the task
  void power(bool on) override;
  void begin(app::CamStep s) override;
  app::Op poll() override { return (app::Op)result_; }
  void cancel() override { cancel_ = true; }

  bool profileValid() const { return prof_.valid; }
  bool profileJson(std::string *out);             // stored JSON, password masked
  bool putProfile(const char *json, size_t len, app::Messages *err);
  bool removeProfile();
  void status(JsonObject o);                      // for /api/status
  void files(JsonObject o);                       // what discover saved

  // the task side (public for the Io adapter)
  volatile bool cancel_ = false;

 private:
  static void taskMain(void *arg);
  void run();
  void applyProfile();
  cam::Profile prof_;
  cam::GoPro *gp_ = nullptr;
  TaskHandle_t task_ = nullptr;
  volatile uint8_t result_ = (uint8_t)app::Op::Fail;
  volatile uint8_t step_ = 0;
  volatile uint32_t seq_ = 0;
  volatile bool busy_ = false;
  bool powered_ = false;
  char last_step_[12] = "none";
};

}  // namespace esp
