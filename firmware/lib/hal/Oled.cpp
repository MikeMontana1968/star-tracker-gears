#ifdef ARDUINO
#include "Oled.h"
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

static Adafruit_SSD1306 s_disp(128, 64, &Wire, -1);

bool Oled::begin() {
  ok_ = s_disp.begin(SSD1306_SWITCHCAPVCC, 0x3C, /*reset=*/false, /*periphBegin=*/false);
  if (ok_) {
    s_disp.clearDisplay();
    s_disp.setTextColor(SSD1306_WHITE);
    s_disp.setTextSize(1);
    s_disp.display();
  }
  return ok_;
}

void Oled::lines(const char *const text[8]) {
  if (!ok_) return;
  s_disp.clearDisplay();
  for (int i = 0; i < 8; i++) {
    if (!text[i]) continue;
    s_disp.setCursor(0, i * 8);
    s_disp.print(text[i]);
  }
  s_disp.display();
}

void Oled::on()  { if (ok_) s_disp.ssd1306_command(SSD1306_DISPLAYON); }
void Oled::off() { if (ok_) s_disp.ssd1306_command(SSD1306_DISPLAYOFF); }
#endif
