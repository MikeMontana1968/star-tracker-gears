// =====================================================================
//  Schedule -- the night schedule (FIRMWARE.md s5, D5): intent, not just
//  timestamps. Parsed from JSON, validated in four layers, resolved night
//  by night against the solar solver. Pure C++ (ArduinoJson is header-only),
//  host-tested in test_schedule.
//
//  {
//    "version": 1,
//    "site": { "lat": 34.6, "lon": -120.4, "tz_offset_min": -480 },
//    "defaults": {
//      "start": { "event": "dusk_astronomical", "offset_min": 10 },
//      "stop":  { "event": "dawn_astronomical", "offset_min": -10 },
//      "start_angle_deg": 0.0,
//      "camera": "nightlapse_30s_iso800",
//      "camera_policy": "best_effort",          none | best_effort | required
//      "gps_policy": "best_effort"              none | best_effort   (optional)
//    },
//    "nights": [                                strictly ascending local dates
//      { "date": "2026-09-18" },
//      { "date": "2026-09-19", "start": { "utc": "2026-09-20T04:15:00Z" } },
//      { "date": "2026-09-20", "skip": true }
//    ],
//    "repeat_after_list": true                  after the last listed date, every night
//  }
//
//  "target": "moon" (in defaults or on a night; default "sky"): the night runs
//  only while the moon is up INSIDE the start..stop window -- so with the
//  astronomical dusk/dawn defaults it is "the moon in the dark" -- and tracks
//  at the lunar rate. A night with the moon up < 30 min of it is skipped.
//
//  Events: sunset, dusk_civil, dusk_nautical, dusk_astronomical (evening of
//  the date); sunrise, dawn_civil, dawn_nautical, dawn_astronomical (the
//  following morning). A twilight with no solution falls back
//  astronomical -> nautical -> civil, then the night is skipped with a
//  reason. "date" is the LOCAL date of the evening (UTC + tz_offset_min).
//
//  Validation (all at upload, and again at every boot):
//    1 parse     size cap before parsing; JSON syntax
//    2 schema    types, ranges (lat, lon, tz, offsets +-180 min), known
//                event / policy / camera names
//    3 semantic  real dates, ascending, stop > start, < 16 h, no overlaps
//    4 envelope  start_angle + duration x sidereal <= TRAVEL_MAX_DEG, for
//                every listed night and for the defaults over the next year
//  Every error is collected (so a phone can show the whole list), with a
//  path: "nights[2].start: unknown event 'dusk_astro'".
// =====================================================================
#pragma once
#include <stddef.h>
#include <stdint.h>
#include "Platform.h"

namespace app {

enum class Event : uint8_t {
  Utc, Sunset, DuskCivil, DuskNautical, DuskAstro, Sunrise, DawnCivil, DawnNautical, DawnAstro
};

struct TimeSpec {
  Event   ev = Event::DuskAstro;
  int16_t offset_min = 0;
  int64_t utc = 0;               // Event::Utc only
};

struct NightEntry {
  int32_t   day = 0;             // local date, days since 1970
  bool      skip = false;
  bool      has_start = false, has_stop = false, has_angle = false, has_camera = false, has_policy = false;
  TimeSpec  start, stop;
  double    angle = 0;
  char      camera[24] = {};
  CamPolicy policy = CamPolicy::BestEffort;
  bool      has_target = false, moon = false;   // "target": "sky" | "moon"
};

struct ScheduleDoc {
  static constexpr int MAX_NIGHTS = 64;
  int       version = 0;
  double    lat = 0, lon = 0;
  int       tz_offset_min = 0;
  TimeSpec  def_start, def_stop;
  double    def_angle = 0;
  char      def_camera[24] = {};
  CamPolicy def_policy = CamPolicy::BestEffort;
  GpsPolicy gps = GpsPolicy::BestEffort;
  bool      def_moon = false;          // defaults.target == "moon"
  bool      repeat = false;
  int       n = 0;
  NightEntry nights[MAX_NIGHTS];
};

struct ScheduleLimits {
  double  travel_min_deg = -2, travel_max_deg = 182;
  double  max_night_h = 16;
  size_t  max_bytes = 8192;
  const char *const *cameras = nullptr;   // known camera profile names (null: any)
  int     n_cameras = 0;
};

struct Messages {
  static constexpr int MAX = 16;
  int  n = 0;
  int  dropped = 0;
  char msg[MAX][96];
  void add(const char *fmt, ...);
  bool empty() const { return n == 0 && dropped == 0; }
};

// Parse + validate. now_utc anchors the defaults' one-year envelope check.
// Returns true when there are no errors (warnings do not fail it).
bool parseSchedule(const char *json, size_t len, const ScheduleLimits &lim, int64_t now_utc,
                   ScheduleDoc *out, Messages *errors, Messages *warnings = nullptr);

class Schedule : public ISchedule {
 public:
  // Keeps a REFERENCE: the doc (~6 KB) must outlive the Schedule. On the
  // ESP32 it is a static, never a stack copy (the loop task has 8 KB).
  Schedule(const ScheduleDoc &doc, const ScheduleLimits &lim) : d_(doc), lim_(lim), lat_(doc.lat), lon_(doc.lon) {}
  NightPlan next(int64_t after_utc) override;
  void      setSite(double lat, double lon) override { lat_ = lat; lon_ = lon; }
  void      reload() { lat_ = d_.lat; lon_ = d_.lon; }   // after the doc it refers to changed
  NightPlan refresh(const NightPlan &p) override;

  // One local date -> a plan. false with *why when that date has no night.
  // clip_moon = false gives a moon night's unclipped dark window (a superset of the
  // clipped one): the year-long envelope scan uses it, ~400 moon solves cost seconds.
  bool resolve(int32_t day, NightPlan *out, const char **why, bool clip_moon = true) const;
  const char *lastSkip() const { return last_skip_; }
  const ScheduleDoc &doc() const { return d_; }

 private:
  const NightEntry *entry(int32_t day) const;
  bool timeFor(const TimeSpec &t, int32_t day, int64_t *utc, uint8_t *fell) const;
  const ScheduleDoc &d_;
  ScheduleLimits lim_;
  double lat_, lon_;
  mutable const char *last_skip_ = nullptr;
};

// "YYYY-MM-DD" -> days since 1970; "YYYY-MM-DDTHH:MM:SSZ" -> unix. false if not real.
bool parseDate(const char *s, int32_t *day);
bool parseUtc(const char *s, int64_t *utc);

}  // namespace app
