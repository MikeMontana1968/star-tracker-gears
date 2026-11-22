#ifdef ARDUINO
#include "Ds3231.h"

namespace {
enum : uint8_t { R_SECONDS = 0x00, R_ALARM1 = 0x07, R_CONTROL = 0x0E, R_STATUS = 0x0F, R_TEMP = 0x11 };
uint8_t bcd(int v) { return (uint8_t)(((v / 10) << 4) | (v % 10)); }
int unbcd(uint8_t b) { return (b >> 4) * 10 + (b & 0x0F); }
}  // namespace

bool Ds3231::rd(uint8_t reg, uint8_t *buf, int n) {
  w_.beginTransmission(ADDR);
  w_.write(reg);
  if (w_.endTransmission(false) != 0) return false;
  if (w_.requestFrom((int)ADDR, n) != n) return false;
  for (int i = 0; i < n; i++) buf[i] = w_.read();
  return true;
}

bool Ds3231::wr(uint8_t reg, const uint8_t *buf, int n) {
  w_.beginTransmission(ADDR);
  w_.write(reg);
  for (int i = 0; i < n; i++) w_.write(buf[i]);
  return w_.endTransmission() == 0;
}

bool Ds3231::present() {
  uint8_t b;
  return rd(R_STATUS, &b, 1);
}

bool Ds3231::now(int64_t *t) {
  uint8_t b[7];
  if (!rd(R_SECONDS, b, 7)) return false;
  Civil c;
  c.second = unbcd(b[0] & 0x7F);
  c.minute = unbcd(b[1] & 0x7F);
  c.hour = (b[2] & 0x40) ? (unbcd(b[2] & 0x1F) % 12 + ((b[2] & 0x20) ? 12 : 0))   // 12 h mode
                         : unbcd(b[2] & 0x3F);
  c.day = unbcd(b[4] & 0x3F);
  c.month = unbcd(b[5] & 0x1F);
  c.year = 2000 + unbcd(b[6]) + ((b[5] & 0x80) ? 100 : 0);
  *t = toUnix(c);
  return true;
}

bool Ds3231::set(int64_t t) {
  Civil c = fromUnix(t);
  int64_t days = daysFromCivil(c.year, c.month, c.day);
  int dow = (int)(((days % 7) + 7 + 4) % 7) + 1;       // 1970-01-01 was a Thursday; 1 = Sunday
  uint8_t b[7] = {bcd(c.second), bcd(c.minute), bcd(c.hour), (uint8_t)dow,
                  bcd(c.day), (uint8_t)(bcd(c.month) | (c.year >= 2100 ? 0x80 : 0)),
                  bcd(c.year % 100)};
  if (!wr(R_SECONDS, b, 7)) return false;
  uint8_t st;
  if (!rd(R_STATUS, &st, 1)) return false;
  st &= ~0x80;                                         // clear OSF
  st &= ~0x08;                                         // 32 kHz output off (power)
  return wr(R_STATUS, &st, 1);
}

bool Ds3231::lostPower() {
  uint8_t st;
  return !rd(R_STATUS, &st, 1) || (st & 0x80);
}

bool Ds3231::temperature(float *c) {
  uint8_t b[2];
  if (!rd(R_TEMP, b, 2)) return false;
  *c = (int8_t)b[0] + (b[1] >> 6) * 0.25f;
  return true;
}

bool Ds3231::setAlarm(int64_t t) {
  Civil c = fromUnix(t);
  // A1M1..A1M4 = 0, DY/DT = 0: match seconds, minutes, hours AND date
  uint8_t a[4] = {bcd(c.second), bcd(c.minute), bcd(c.hour), bcd(c.day)};
  if (!wr(R_ALARM1, a, 4)) return false;
  if (!clearAlarm()) return false;
  uint8_t ctl;
  if (!rd(R_CONTROL, &ctl, 1)) return false;
  ctl |= 0x04 | 0x01;                                  // INTCN, A1IE
  ctl &= ~0x80;                                        // EOSC low = oscillator runs on battery
  return wr(R_CONTROL, &ctl, 1);
}

bool Ds3231::alarmFired() {
  uint8_t st;
  return rd(R_STATUS, &st, 1) && (st & 0x01);
}

bool Ds3231::clearAlarm() {
  uint8_t st;
  if (!rd(R_STATUS, &st, 1)) return false;
  st &= ~0x03;                                         // A1F, A2F
  return wr(R_STATUS, &st, 1);
}

bool Ds3231::disableAlarm() {
  uint8_t ctl;
  if (!rd(R_CONTROL, &ctl, 1)) return false;
  ctl &= ~0x03;
  return wr(R_CONTROL, &ctl, 1) && clearAlarm();
}
#endif
