// =====================================================================
//  pincheck.ino -- Slice 0, step 1.  Answers the one question the board
//  photo cannot: which GPIOs does the on-board OLED actually use?
//
//  Sources disagree for the ideaspark ESP32 + 0.96" OLED. One says the
//  panel is on GPIO 21/22 (the ESP32 I2C defaults), another says 4/15/16.
//  It decides whether WAKE can live on GPIO 4, so guess and you scrap a
//  board.
//
//  Flash it, open the serial monitor at 115200, read the verdict.
//  Nothing else need be connected -- no RTC, no encoder, no driver.
// =====================================================================

#include <Arduino.h>
#include <Wire.h>

// The two candidate buses, plus the third pin some boards use for RESET.
struct Bus { const char *name; int sda, scl; };
static const Bus BUSES[] = {
  { "GPIO21 (SDA) / GPIO22 (SCL)  <- ESP32 default", 21, 22 },
  { "GPIO4  (SDA) / GPIO15 (SCL)  <- Heltec style",   4, 15 },
  { "GPIO5  (SDA) / GPIO4  (SCL)",                    5,  4 },
};

static const char *whoIs(uint8_t a) {
  switch (a) {
    case 0x3C: case 0x3D: return "SSD1306 OLED";
    case 0x36:            return "AS5600 encoder";
    case 0x68:            return "DS3231 RTC";
    case 0x57:            return "DS3231 EEPROM (AT24C32)";
    case 0x0D: case 0x1E: return "magnetometer";
    default:              return "unknown";
  }
}

static int scan(const Bus &b) {
  Wire.end();
  if (!Wire.begin(b.sda, b.scl, 100000)) {
    Serial.printf("  %-44s  bus would not start\n", b.name);
    return 0;
  }
  delay(50);
  int found = 0;
  for (uint8_t a = 1; a < 127; a++) {
    Wire.beginTransmission(a);
    if (Wire.endTransmission() == 0) {
      if (!found) Serial.printf("  %s\n", b.name);
      Serial.printf("      0x%02X  %s\n", a, whoIs(a));
      found++;
    }
  }
  if (!found) Serial.printf("  %-44s  nothing\n", b.name);
  return found;
}

void setup() {
  Serial.begin(115200);
  delay(1500);
  Serial.println("\n\n=== star tracker :: pin check ===");
  Serial.printf("chip %s  rev %d  %d core(s)  flash %u MB\n",
                ESP.getChipModel(), ESP.getChipRevision(), ESP.getChipCores(),
                (unsigned)(ESP.getFlashChipSize() / (1024 * 1024)));
  Serial.printf("PSRAM: %s   <- must be NONE; WROVER's PSRAM sits on GPIO16/17\n",
                ESP.getPsramSize() ? "PRESENT (this is a WROVER!)" : "none");

  Serial.println("\nI2C scan:");
  int hits = 0;
  for (const Bus &b : BUSES) hits += scan(b);

  Serial.println("\n--- what this means -------------------------------");
  if (!hits) {
    Serial.println("No devices anywhere. If the OLED is lit, it is not I2C;");
    Serial.println("if it is dark, the panel may be SPI or on other pins.");
  }
  Serial.println("If 0x3C answered on 21/22  -> OLED shares the sensor bus.");
  Serial.println("   WAKE stays on GPIO4, nothing else changes.");
  Serial.println("If 0x3C answered on 4/15   -> those pins are spoken for.");
  Serial.println("   WAKE must move; report which bus answered.");
}

void loop() {
  delay(10000);
}
