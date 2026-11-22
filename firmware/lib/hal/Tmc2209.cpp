#ifdef ARDUINO
#include "Tmc2209.h"

namespace {
enum : uint8_t { GCONF = 0x00, GSTAT = 0x01, IFCNT = 0x02, IOIN = 0x06,
                 IHOLD_IRUN = 0x10, MSCNT = 0x6A, CHOPCONF = 0x6C, DRV_STATUS = 0x6F };

uint8_t crc8(const uint8_t *d, int n) {
  uint8_t crc = 0;
  for (int i = 0; i < n; i++) {
    uint8_t b = d[i];
    for (int j = 0; j < 8; j++) {
      if ((crc >> 7) ^ (b & 1)) crc = (crc << 1) ^ 0x07; else crc <<= 1;
      b >>= 1;
    }
  }
  return crc;
}

uint8_t mresCode(uint16_t usteps) {   // 256 -> 0 ... 1 (full) -> 8
  uint8_t code = 0;
  for (uint16_t u = 256; u > usteps && code < 8; u >>= 1) code++;
  return code;
}
}  // namespace

bool Tmc2209::begin(HardwareSerial &port, int pin_rx, int pin_tx, uint8_t addr) {
  s_ = &port;
  addr_ = addr;
  s_->begin(115200, SERIAL_8N1, pin_rx, pin_tx);
  delay(5);
  uint32_t c;
  if (read(CHOPCONF, &c)) chopconf_ = c;
  return alive();
}

bool Tmc2209::xfer(const uint8_t *tx, int ntx, uint8_t *rx, int nrx) {
  while (s_->available()) s_->read();
  s_->write(tx, ntx);
  s_->flush();
  int want = ntx + nrx, n = 0;           // our echo, then the reply
  uint8_t buf[16];
  uint32_t t0 = millis();
  while (n < want && millis() - t0 < 20)
    if (s_->available()) buf[n++] = s_->read();
  if (n < want) return false;
  for (int i = 0; i < nrx; i++) rx[i] = buf[ntx + i];
  return true;
}

bool Tmc2209::read(uint8_t reg, uint32_t *val) {
  for (int attempt = 0; attempt < 3; attempt++) {
    uint8_t q[4] = {0x05, addr_, reg, 0};
    q[3] = crc8(q, 3);
    uint8_t r[8];
    if (!xfer(q, 4, r, 8)) continue;
    if (r[0] != 0x05 || r[1] != 0xFF || r[2] != reg || crc8(r, 7) != r[7]) continue;
    *val = ((uint32_t)r[3] << 24) | ((uint32_t)r[4] << 16) | ((uint32_t)r[5] << 8) | r[6];
    return true;
  }
  return false;
}

bool Tmc2209::write(uint8_t reg, uint32_t val) {
  uint32_t before, after;
  if (!read(IFCNT, &before)) return false;
  for (int attempt = 0; attempt < 3; attempt++) {
    uint8_t w[8] = {0x05, addr_, (uint8_t)(reg | 0x80),
                    (uint8_t)(val >> 24), (uint8_t)(val >> 16), (uint8_t)(val >> 8), (uint8_t)val, 0};
    w[7] = crc8(w, 7);
    xfer(w, 8, nullptr, 0);              // only our echo comes back
    delayMicroseconds(200);
    if (read(IFCNT, &after) && ((after - before) & 0xFF) != 0) return true;
  }
  return false;
}

bool Tmc2209::alive() {
  uint32_t v;
  return read(IOIN, &v) && (v >> 24) == 0x21;
}

bool Tmc2209::configure() {
  bool ok = write(GCONF, (1u << 0) | (1u << 6) | (1u << 7) | (1u << 8));  // 0x1C1
  ok &= write(IHOLD_IRUN, (16u << 0) | (31u << 8) | (8u << 16));
  chopconf_ = (chopconf_ & ~(0xFu << 24)) | ((uint32_t)mresCode(usteps_) << 24);
  ok &= write(CHOPCONF, chopconf_);
  uint32_t g;
  ok &= read(GSTAT, &g);
  if (ok && (g & 1)) write(GSTAT, 1);    // clear the power-on reset flag
  return ok;
}

bool Tmc2209::setMicrosteps(uint16_t usteps) {
  uint32_t want = (chopconf_ & ~(0xFu << 24)) | ((uint32_t)mresCode(usteps) << 24);
  if (!write(CHOPCONF, want)) return false;
  uint32_t back;
  if (!read(CHOPCONF, &back) || ((back >> 24) & 0xF) != mresCode(usteps)) return false;
  chopconf_ = back;
  usteps_ = usteps;
  return true;
}

bool Tmc2209::health(bool *was_reset) {
  *was_reset = false;
  uint32_t g, gc, ifc = 0;
  if (!read(GSTAT, &g)) return false;
  if (!(g & 1)) return true;
  // GSTAT says reset. Confirm against GCONF before acting: a real power-on
  // reset puts GCONF back to its OTP default (0x101), losing pdn_disable and
  // mstep_reg_select; a corrupted reply that slipped past the CRC does not.
  if (!read(GCONF, &gc)) return false;
  read(IFCNT, &ifc);
  bool lost = (gc & ((1u << 6) | (1u << 7))) != ((1u << 6) | (1u << 7));
  last_gstat_ = g; last_gconf_ = gc; last_ifcnt_ = ifc;
  if (lost) {
    *was_reset = true;
    configure();
  } else {
    write(GSTAT, 1);                     // flag set but config intact: clear it
    spurious_++;
  }
  return true;
}

Tmc2209::Status Tmc2209::status() {
  Status st = {};
  uint32_t m = 0;
  st.ok = read(DRV_STATUS, &st.drv_status) && read(GSTAT, &st.gstat) &&
          read(IOIN, &st.ioin) && read(MSCNT, &m);
  uint32_t d = st.drv_status;
  st.mscnt = (uint16_t)m;
  st.otpw = d & 1; st.ot = (d >> 1) & 1;
  st.s2g = (d >> 2) & 3; st.s2vs = (d >> 4) & 3; st.ol = (d >> 6) & 3;
  st.cs_actual = (d >> 16) & 31; st.stealth = (d >> 30) & 1; st.standstill = (d >> 31) & 1;
  return st;
}
#endif
