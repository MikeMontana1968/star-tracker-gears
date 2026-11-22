// =====================================================================
//  As5600 -- raw I2C driver (FIRMWARE.md s8: by hand, because AGC and
//  MAGNITUDE are what make the magnet gap measurable). Read-only: we
//  never burn ZPOS/MPOS -- the zero lives in config/EncoderMap instead.
// =====================================================================
#pragma once
#ifdef ARDUINO
#include <Arduino.h>
#include <Wire.h>

class As5600 {
 public:
  static constexpr uint8_t ADDR = 0x36;
  struct Reading {
    bool ok;
    uint16_t raw;        // RAW ANGLE 0..4095
    uint8_t  agc;        // 0..128 at 3.3 V; mid-range = good gap
    uint16_t magnitude;
    bool md, ml, mh;     // magnet detected / too weak / too strong
  };
  explicit As5600(TwoWire &w = Wire) : w_(w) {}
  int raw();             // -1 on bus error
  int rawAveraged(int n);// mean of n reads, -1 on error
  Reading read();

 private:
  bool reg(uint8_t r, uint8_t *buf, int n);
  TwoWire &w_;
};
#endif
