// =====================================================================
//  Nmea -- just enough NMEA 0183 for TIME_SYNC (FIRMWARE.md s3): RMC for
//  UTC date/time, validity and position; GGA for fix quality, satellites
//  and HDOP. Checksums enforced. Pure C++ (host-tested), fed a byte at a
//  time from the NEO-6M's UART (9600 8N1).
// =====================================================================
#pragma once
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "CivilTime.h"

class Nmea {
 public:
  struct Fix {
    bool    rmc_ok = false;      // last RMC had status A
    int64_t utc = 0;             // from the last good RMC (whole seconds)
    double  lat = 0, lon = 0;    // degrees, north/east positive
    int     quality = 0;         // GGA fix quality (0 = none)
    int     sats = 0;
    double  hdop = 99;
    uint32_t rmc_count = 0, gga_count = 0, bad_checksum = 0;
  };

  // feed one byte; returns true when it completed a sentence that updated the RMC time
  bool feed(char c) {
    if (c == '$') { n_ = 0; buf_[n_++] = c; return false; }
    if (n_ == 0) return false;
    if (c == '\r' || c == '\n') { buf_[n_] = 0; bool r = sentence(); n_ = 0; return r; }
    if (n_ < (int)sizeof buf_ - 1) buf_[n_++] = c; else n_ = 0;
    return false;
  }
  const Fix &fix() const { return f_; }
  // a fix worth setting the clock from (FIRMWARE.md: 3-D-ish fix, sane HDOP)
  bool usable() const { return f_.rmc_ok && f_.quality >= 1 && f_.sats >= 4 && f_.hdop <= 5.0; }

 private:
  static int hex(char c) { return c >= '0' && c <= '9' ? c - '0' : c >= 'A' && c <= 'F' ? c - 'A' + 10 : c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1; }

  // split buf_ in place into fields after verifying the checksum
  bool sentence() {
    char *star = strchr(buf_, '*');
    if (!star || star[1] == 0 || star[2] == 0) return false;
    uint8_t sum = 0;
    for (char *p = buf_ + 1; p < star; p++) sum ^= (uint8_t)*p;
    int h = hex(star[1]), l = hex(star[2]);
    if (h < 0 || l < 0 || sum != (uint8_t)(h * 16 + l)) { f_.bad_checksum++; return false; }
    *star = 0;
    char *fld[20]; int nf = 0;
    for (char *p = buf_; nf < 20;) {
      fld[nf++] = p;
      char *comma = strchr(p, ',');
      if (!comma) break;
      *comma = 0; p = comma + 1;
    }
    if (strlen(fld[0]) != 6) return false;
    const char *type = fld[0] + 3;                 // $GPRMC / $GNRMC ...
    if (!strcmp(type, "RMC") && nf >= 10) return rmc(fld);
    if (!strcmp(type, "GGA") && nf >= 9) gga(fld);
    return false;
  }

  static double angle(const char *v, const char *hemi, int deg_digits) {
    if (!*v) return 0;
    char d[4] = {0}; memcpy(d, v, deg_digits);
    double a = atof(d) + atof(v + deg_digits) / 60.0;
    return (*hemi == 'S' || *hemi == 'W') ? -a : a;
  }

  bool rmc(char **f) {
    f_.rmc_count++;
    f_.rmc_ok = f[2][0] == 'A';
    if (!f_.rmc_ok || strlen(f[1]) < 6 || strlen(f[9]) != 6) return false;
    int hh = (f[1][0] - '0') * 10 + f[1][1] - '0', mm = (f[1][2] - '0') * 10 + f[1][3] - '0';
    int ss = (f[1][4] - '0') * 10 + f[1][5] - '0';
    int dd = (f[9][0] - '0') * 10 + f[9][1] - '0', mo = (f[9][2] - '0') * 10 + f[9][3] - '0';
    int yy = (f[9][4] - '0') * 10 + f[9][5] - '0';
    f_.utc = toUnix({yy < 80 ? 2000 + yy : 1900 + yy, mo, dd, hh, mm, ss});
    f_.lat = angle(f[3], f[4], 2);
    f_.lon = angle(f[5], f[6], 3);
    return true;
  }

  void gga(char **f) {
    f_.gga_count++;
    f_.quality = atoi(f[6]);
    f_.sats = atoi(f[7]);
    f_.hdop = *f[8] ? atof(f[8]) : 99;
  }

  char buf_[100];
  int  n_ = 0;
  Fix  f_;
};
