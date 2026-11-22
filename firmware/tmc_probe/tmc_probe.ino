// =====================================================================
//  tmc_probe.ino -- READ-ONLY look inside the TMC2209 over its
//  single-wire UART (PDN_UART: ESP TX GPIO33 via R7 1k, RX GPIO14).
//  Nothing is written to the driver. For each UART address 0-3 it tries
//  IOIN (version 0x21 = alive), then reports:
//    IOIN      pin levels the chip sees (EN, MS1, MS2, STEP, DIR, PDN)
//              -- with EN/STEP/DIR driven high, then low
//    GSTAT     reset / drv_err / undervoltage
//    DRV_STATUS faults (short, open load, overtemp), CS_ACTUAL, standstill
//    MSCNT     microstep counter, before and after 256 STEP pulses
//  Ends with the driver disabled (EN high).
// =====================================================================

#include <Arduino.h>

static const int PIN_STEP = 25, PIN_DIR = 26, PIN_EN = 27;
static const int PIN_UTX = 33, PIN_URX = 14;
HardwareSerial TMC(2);

static uint8_t crc8(const uint8_t *d, int n) {
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

static bool dbg = true;
// returns true and fills *val on a good reply
static bool rd(uint8_t addr, uint8_t reg, uint32_t *val) {
  uint8_t q[4] = {0x05, addr, reg, 0};
  q[3] = crc8(q, 3);
  while (TMC.available()) TMC.read();
  TMC.write(q, 4);
  TMC.flush();
  uint8_t buf[16]; int n = 0;
  uint32_t t0 = millis();
  while (millis() - t0 < 30 && n < 12) if (TMC.available()) buf[n++] = TMC.read();
  // single wire: our own 4 bytes echo back first, then the 8-byte reply
  if (dbg) {
    Serial.printf("   addr %d reg 0x%02X: %d bytes back:", addr, reg, n);
    for (int i = 0; i < n; i++) Serial.printf(" %02X", buf[i]);
    Serial.println();
  }
  int off = (n >= 12) ? 4 : (n == 8 ? 0 : -1);
  if (off < 0) return false;
  uint8_t *r = buf + off;
  if (r[0] != 0x05 || r[1] != 0xFF || r[2] != reg || crc8(r, 7) != r[7]) return false;
  *val = ((uint32_t)r[3] << 24) | ((uint32_t)r[4] << 16) | ((uint32_t)r[5] << 8) | r[6];
  return true;
}

static void pins(int en, int step, int dir) {
  digitalWrite(PIN_EN, en); digitalWrite(PIN_STEP, step); digitalWrite(PIN_DIR, dir);
  delay(5);
}

static void showIoin(uint32_t v, const char *tag) {
  Serial.printf("   IOIN %-22s ENN=%lu MS1=%lu MS2=%lu STEP=%lu DIR=%lu PDN=%lu  ver=0x%02lX\n", tag,
                v & 1, (v >> 2) & 1, (v >> 3) & 1, (v >> 7) & 1, (v >> 9) & 1, (v >> 6) & 1, v >> 24);
}

void setup() {
  Serial.begin(115200);
  pinMode(PIN_STEP, OUTPUT); pinMode(PIN_DIR, OUTPUT); pinMode(PIN_EN, OUTPUT);
  pins(HIGH, LOW, LOW);
  TMC.begin(115200, SERIAL_8N1, PIN_URX, PIN_UTX);
  delay(1200);
  Serial.println("\n=== TMC2209 probe (read-only) ===");

  int addr = -1; uint32_t v;
  for (int a = 0; a < 4 && addr < 0; a++)
    for (int tries = 0; tries < 3; tries++)
      if (rd(a, 0x06, &v) && (v >> 24) == 0x21) { addr = a; break; }
  if (addr < 0) {
    Serial.println("no UART reply at addresses 0-3 (module may route UART to the other pin: JP3)");
    Serial.printf("   raw IOIN attempt addr0 ok=%d val=0x%08lX\n", rd(0, 0x06, &v), v);
    pins(HIGH, LOW, LOW);
    return;
  }
  Serial.printf("driver answers at UART address %d\n", addr);
  dbg = false;

  pins(HIGH, HIGH, HIGH); if (rd(addr, 0x06, &v)) showIoin(v, "(EN/STEP/DIR all HIGH)");
  pins(LOW, LOW, LOW);    if (rd(addr, 0x06, &v)) showIoin(v, "(EN/STEP/DIR all LOW)");

  if (rd(addr, 0x01, &v)) Serial.printf("   GSTAT  0x%lX  reset=%lu drv_err=%lu uv_cp=%lu\n", v, v & 1, (v >> 1) & 1, (v >> 2) & 1);
  if (rd(addr, 0x00, &v)) Serial.printf("   GCONF  0x%08lX  (pdn_disable=%lu mstep_reg_select=%lu en_spreadcycle=%lu)\n", v, (v >> 6) & 1, (v >> 7) & 1, (v >> 2) & 1);
  if (rd(addr, 0x6C, &v)) Serial.printf("   CHOPCONF 0x%08lX  MRES=%lu toff=%lu\n", v, (v >> 24) & 15, v & 15);

  delay(200);                                    // enabled (EN low) now
  uint32_t m0 = 0, m1 = 0;
  bool ok0 = rd(addr, 0x6A, &m0);
  // 100 steps: not a multiple of any microstep cycle, so MSCNT can't wrap back
  // to where it started (256 steps at 1/8 is exactly 8 cycles and reads 0)
  for (int i = 0; i < 100; i++) { digitalWrite(PIN_STEP, HIGH); delayMicroseconds(5); digitalWrite(PIN_STEP, LOW); delayMicroseconds(2000); }
  bool ok1 = rd(addr, 0x6A, &m1);
  Serial.printf("   MSCNT  before %lu (ok=%d)  after 100 steps %lu (ok=%d)  -> moved %ld (1/8 step = 32 counts, so expect 128)\n",
                m0, ok0, m1, ok1, (long)((m1 - m0) & 1023));

  for (int k = 0; k < 3; k++) {
    for (int i = 0; i < 200; i++) { digitalWrite(PIN_STEP, HIGH); delayMicroseconds(5); digitalWrite(PIN_STEP, LOW); delayMicroseconds(1000); }
    if (rd(addr, 0x6F, &v))
      Serial.printf("   DRV_STATUS 0x%08lX  ot=%lu otpw=%lu s2ga=%lu s2gb=%lu s2vsa=%lu s2vsb=%lu ola=%lu olb=%lu CS_ACTUAL=%lu stealth=%lu stst=%lu\n",
                    v, (v >> 1) & 1, v & 1, (v >> 2) & 1, (v >> 3) & 1, (v >> 4) & 1, (v >> 5) & 1,
                    (v >> 6) & 1, (v >> 7) & 1, (v >> 16) & 31, (v >> 30) & 1, (v >> 31) & 1);
  }
  pins(HIGH, LOW, LOW);
  Serial.println("driver disabled. done.");
}

void loop() { delay(1000); }
