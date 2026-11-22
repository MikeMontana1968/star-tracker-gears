// =====================================================================
//  Tmc2209 -- single-wire UART driver (PDN_UART: ESP TX GPIO33 via R7
//  1k, RX GPIO14 direct; JP3 fitted). Every frame we send echoes back on
//  RX first, then the reply; writes are confirmed by IFCNT incrementing.
//
//  What it sets (FIRMWARE.md s2):
//    GCONF  pdn_disable=1 (UART owns PDN), mstep_reg_select=1 (MRES from
//           CHOPCONF, not MS1/MS2 -- those now only set the address),
//           I_scale_analog=1 (VREF still sets full scale: 0.50 V ~350 mA)
//    IHOLD_IRUN  run 31/32 of VREF, hold 16/32
//    CHOPCONF.MRES  1/32 while tracking, 1/8 while slewing
//
//  A driver power cycle (VM lost, ESP32 still up) resets all of that to
//  1/8 from the pins. health() catches it via GSTAT.reset; the caller
//  must treat the position as suspect if it happened while not idle.
// =====================================================================
#pragma once
#ifdef ARDUINO
#include <Arduino.h>

class Tmc2209 {
 public:
  bool begin(HardwareSerial &port, int pin_rx, int pin_tx, uint8_t addr = 0);
  bool alive();                       // IOIN version == 0x21
  bool configure();                   // GCONF + IHOLD_IRUN + MRES (current shadow)
  bool setMicrosteps(uint16_t usteps);  // 8, 16, 32 ... ; verified by read-back
  uint16_t microsteps() const { return usteps_; }

  // Returns false if the chip stopped answering. Sets *was_reset if GSTAT
  // shows a reset since the last call (and re-applies configure()).
  bool health(bool *was_reset);
  // what the last reset flag looked like, for the log
  uint32_t lastGstat() const { return last_gstat_; }
  uint32_t lastGconf() const { return last_gconf_; }
  uint32_t lastIfcnt() const { return last_ifcnt_; }
  uint32_t spurious() const { return spurious_; }   // GSTAT.reset seen with config intact

  bool read(uint8_t reg, uint32_t *val);
  bool write(uint8_t reg, uint32_t val);   // confirmed via IFCNT

  struct Status {
    bool ok;
    uint32_t drv_status, gstat, ioin;
    uint16_t mscnt;
    uint8_t  cs_actual;
    bool ot, otpw, s2g, s2vs, ol, stealth, standstill;
  };
  Status status();

 private:
  bool xfer(const uint8_t *tx, int ntx, uint8_t *rx, int nrx);
  HardwareSerial *s_ = nullptr;
  uint8_t addr_ = 0;
  uint16_t usteps_ = 8;
  uint32_t chopconf_ = 0x10000053;   // reset default, MRES patched in
  uint32_t last_gstat_ = 0, last_gconf_ = 0, last_ifcnt_ = 0, spurious_ = 0;
};
#endif
