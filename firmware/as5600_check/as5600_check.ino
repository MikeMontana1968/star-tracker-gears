// =====================================================================
//  as5600_check.ino -- guide steps 58 (magnet gap) and 59 (encoder zero).
//
//  Reads the AS5600 five times a second and shows, on the OLED and on
//  the serial monitor (115200):
//
//    step 58  STATUS bits MD/ML/MH, AGC and MAGNITUDE -> a gap verdict
//    step 59  RAW ANGLE, its distance from 1024, and where the 180.5 deg
//             night's sweep would end -- it must not cross 4095 -> 0
//
//  Only the encoder (J7) need be connected. The motor driver, RTC and
//  camera can be absent. Nothing is written to the AS5600 -- no ZPOS,
//  no burn -- so it is safe to run as often as you like.
//
//  Driver by hand, ~30 lines of raw I2C (FIRMWARE.md s8): the libraries
//  mostly skip AGC and MAGNITUDE, which are the two registers that make
//  the gap a reading instead of a guess.
// =====================================================================

#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

static const int     SDA_PIN = 21, SCL_PIN = 22;   // FIRMWARE.md s4: shared bus
static const uint8_t AS5600  = 0x36;
static const uint8_t OLED    = 0x3C;

// AS5600 registers (datasheet table 21)
enum : uint8_t {
  R_ZPOS = 0x01, R_MPOS = 0x03, R_MANG = 0x05, R_CONF = 0x07,
  R_STATUS = 0x0B, R_RAW = 0x0C, R_ANGLE = 0x0E, R_AGC = 0x1A, R_MAG = 0x1B,
};
static const uint8_t ST_MD = 0x20, ST_ML = 0x10, ST_MH = 0x08;

// Step 59 targets. 180.5 deg of output = one night = 2054 counts.
static const int ZERO_TARGET = 1024;   // raw at the start angle (90 deg)
static const int ZERO_TOL    = 12;     // +-12 counts = +-1 deg is plenty
static const int SWEEP       = 2054;

// AGC runs 0..128 when the chip is powered as a 3.3 V part and 0..255 as
// a 5 V part; breakouts differ in how they tie VDD3V3. Assume 128 and
// widen to 255 the first time a reading proves otherwise.
static int agcMax = 128;

Adafruit_SSD1306 oled(128, 64, &Wire, -1);
static bool haveOled = false;

// ---- raw I2C ---------------------------------------------------------
static bool rd(uint8_t reg, uint8_t *buf, size_t n) {
  Wire.beginTransmission(AS5600);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom(AS5600, (uint8_t)n) != n) return false;
  for (size_t i = 0; i < n; i++) buf[i] = Wire.read();
  return true;
}
static int rd8(uint8_t reg)  { uint8_t b;    return rd(reg, &b, 1) ? b : -1; }
static int rd12(uint8_t reg) { uint8_t b[2]; return rd(reg, b, 2) ? ((b[0] & 0x0F) << 8) | b[1] : -1; }

// ---- verdicts --------------------------------------------------------
struct Gap { const char *word; const char *advice; };
static Gap gapVerdict(int st, int agc);   // stops the IDE hoisting its own prototype above Gap

static Gap gapVerdict(int st, int agc) {
  if (!(st & ST_MD)) return { "NONE",   "no magnet seen: check the cap is on" };
  if (st & ST_MH)    return { "STRONG", "too close: move the magnet AWAY" };
  if (st & ST_ML)    return { "WEAK",   "too far: move the magnet CLOSER" };
  // Low AGC = strong field. The middle of the range leaves the most room
  // for temperature and the shaft's end float either way.
  int pct = agc * 100 / agcMax;
  if (pct < 20)      return { "CLOSE",  "works, but on the strong side: away a little" };
  if (pct > 80)      return { "FAR",    "works, but on the weak side: closer a little" };
  return               { "GOOD",   "gap is good -- step 58 done" };
}

// ---- noise: spread of the last 10 raw readings -----------------------
static int hist[10], nHist = 0, iHist = 0;
static int spread(int raw) {
  hist[iHist] = raw; iHist = (iHist + 1) % 10; if (nHist < 10) nHist++;
  int lo = 4096, hi = -1;
  for (int i = 0; i < nHist; i++) { lo = min(lo, hist[i]); hi = max(hi, hist[i]); }
  return hi - lo;   // a turning shaft reads large; this is for a still one
}

void setup() {
  Serial.begin(115200);
  delay(1200);
  Serial.println("\n\n=== star tracker :: AS5600 check (steps 58, 59) ===");

  Wire.begin(SDA_PIN, SCL_PIN, 400000);
  esp_log_level_set("i2c.master", ESP_LOG_NONE);   // the core logs 4 lines per NACK; we report it ourselves

  haveOled = oled.begin(SSD1306_SWITCHCAPVCC, OLED);
  if (haveOled) {
    oled.setTextColor(SSD1306_WHITE);
    oled.clearDisplay(); oled.setCursor(0, 0);
    oled.print("AS5600 check"); oled.display();
  } else {
    Serial.println("(OLED did not answer at 0x3C -- serial only)");
  }

  Wire.beginTransmission(AS5600);
  if (Wire.endTransmission() != 0) {
    Serial.println("AS5600 did not answer at 0x36.");
    Serial.println("  Check J7: 3V3, GND, SDA, SCL, and DIR to GND.");
  } else {
    int zpos = rd12(R_ZPOS), mpos = rd12(R_MPOS), mang = rd12(R_MANG), conf = rd12(R_CONF);
    Serial.printf("AS5600 found.  ZPOS %d  MPOS %d  MANG %d  CONF 0x%03X\n", zpos, mpos, mang, conf);
    if (zpos || mpos || mang)
      Serial.println("  (start/stop positions are programmed -- harmless, this sketch reads RAW ANGLE)");
  }
  Serial.println("\n   st   AGC   MAG   RAW    deg   zero  end   noise  gap");
}

void loop() {
  int st  = rd8(R_STATUS);
  int agc = rd8(R_AGC);
  int mag = rd12(R_MAG);
  int raw = rd12(R_RAW);

  if (st < 0 || agc < 0 || mag < 0 || raw < 0) {
    Serial.println("-- no reply from AS5600 (0x36) --");
    if (haveOled) {
      oled.clearDisplay(); oled.setTextSize(1); oled.setCursor(0, 0);
      oled.println("AS5600 not found");
      oled.println("");
      oled.println("check J7 wiring:");
      oled.println("3V3 GND SDA SCL");
      oled.println("DIR to GND");
      oled.display();
    }
    delay(500);
    return;
  }

  if (agc > agcMax) agcMax = 255;   // a 5 V-mode part; see agcMax above

  Gap g      = gapVerdict(st, agc);
  int delta  = raw - ZERO_TARGET;
  int end    = raw + SWEEP;
  bool seam  = end > 4095;
  bool zeroOk = abs(delta) <= ZERO_TOL;
  int noise  = spread(raw);
  float deg  = raw * 360.0f / 4096.0f;

  Serial.printf("  %c%c%c  %3d/%-3d %4d  %4d  %5.1f  %+5d  %4d%s  %3d   %s\n",
                (st & ST_MD) ? 'D' : '-', (st & ST_ML) ? 'L' : '-', (st & ST_MH) ? 'H' : '-',
                agc, agcMax, mag, raw, deg, delta, end, seam ? "!" : " ", noise, g.word);

  // Repeat the advice now and then, not every line.
  static uint32_t lastAdvice = 0;
  if (millis() - lastAdvice > 3000) {
    lastAdvice = millis();
    Serial.printf("     gap : %s\n", g.advice);
    if (st & ST_MD) {
      if (seam)        Serial.println("     zero: the night's sweep would cross 4095->0 -- turn the magnet cap toward 1024");
      else if (zeroOk) Serial.println("     zero: within 1 deg of 1024 -- step 59 done");
      else             Serial.printf ("     zero: turn the magnet cap until 'zero' reads 0 (now %+d)\n", delta);
    }
  }

  if (haveOled) {
    oled.clearDisplay();
    oled.setTextSize(1);
    oled.setCursor(0, 0);
    oled.printf("GAP %s", g.word);
    // AGC bar: low is strong, high is weak; the box marks 20-80 %.
    int x0 = 70, w = 56;
    oled.drawRect(x0, 0, w, 7, SSD1306_WHITE);
    oled.fillRect(x0, 1, min(w, agc * w / agcMax), 5, SSD1306_WHITE);
    oled.drawFastVLine(x0 + w / 5, 7, 2, SSD1306_WHITE);
    oled.drawFastVLine(x0 + 4 * w / 5, 7, 2, SSD1306_WHITE);

    oled.setCursor(0, 11);
    oled.printf("AGC %d/%d  MAG %d", agc, agcMax, mag);
    oled.setCursor(0, 21);
    oled.printf("MD%d ML%d MH%d  noise %d", !!(st & ST_MD), !!(st & ST_ML), !!(st & ST_MH), noise);

    oled.drawFastHLine(0, 31, 128, SSD1306_WHITE);
    oled.setCursor(0, 35);
    oled.printf("RAW %4d  %5.1f deg", raw, deg);
    oled.setCursor(0, 45);
    oled.setTextSize(2);
    if (!(st & ST_MD))  oled.print("--");
    else if (zeroOk)    oled.print("ZERO OK");
    else                oled.printf("%+d", delta);
    oled.setTextSize(1);
    oled.setCursor(92, 56);
    oled.print(seam ? "SEAM!" : "");
    oled.display();
  }

  delay(200);
}
