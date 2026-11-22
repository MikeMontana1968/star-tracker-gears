#ifdef ARDUINO
#include "As5600.h"

bool As5600::reg(uint8_t r, uint8_t *buf, int n) {
  w_.beginTransmission(ADDR);
  w_.write(r);
  if (w_.endTransmission(false) != 0) return false;
  if (w_.requestFrom((int)ADDR, n) != n) return false;
  for (int i = 0; i < n; i++) buf[i] = w_.read();
  return true;
}

int As5600::raw() {
  uint8_t b[2];
  if (!reg(0x0C, b, 2)) return -1;
  return ((b[0] << 8) | b[1]) & 0x0FFF;
}

int As5600::rawAveraged(int n) {
  // average as offsets from the first read, so a 4095/0 straddle is fine
  int first = raw();
  if (first < 0) return -1;
  long sum = 0;
  for (int i = 1; i < n; i++) {
    int r = raw();
    if (r < 0) return -1;
    int d = r - first;
    if (d > 2048) d -= 4096;
    if (d < -2048) d += 4096;
    sum += d;
  }
  int v = first + (int)((sum + (sum >= 0 ? n / 2 : -n / 2)) / n);
  return (v + 4096) % 4096;
}

As5600::Reading As5600::read() {
  Reading r = {};
  uint8_t st, agc, b[2], m[2];
  r.ok = reg(0x0B, &st, 1) && reg(0x1A, &agc, 1) && reg(0x0C, b, 2) && reg(0x1B, m, 2);
  if (!r.ok) return r;
  r.md = st & 0x20; r.ml = st & 0x10; r.mh = st & 0x08;
  r.agc = agc;
  r.raw = ((b[0] << 8) | b[1]) & 0x0FFF;
  r.magnitude = ((m[0] << 8) | m[1]) & 0x0FFF;
  return r;
}
#endif
