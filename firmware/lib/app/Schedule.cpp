#include "Schedule.h"
#include <ArduinoJson.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "CivilTime.h"
#include "Solar.h"
#include "Moon.h"

namespace app {


void Messages::add(const char *fmt, ...) {
  if (n >= MAX) { dropped++; return; }
  va_list ap; va_start(ap, fmt);
  vsnprintf(msg[n++], sizeof msg[0], fmt, ap);
  va_end(ap);
}

// ---- dates -------------------------------------------------------------------
static int daysIn(int y, int m) {
  static const int d[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  bool leap = (y % 4 == 0 && y % 100 != 0) || y % 400 == 0;
  return m == 2 && leap ? 29 : d[m - 1];
}

bool parseDate(const char *s, int32_t *day) {
  int y, m, d; char tail;
  if (!s || strlen(s) != 10 || sscanf(s, "%4d-%2d-%2d%c", &y, &m, &d, &tail) != 3) return false;
  if (y < 2020 || y > 2100 || m < 1 || m > 12 || d < 1 || d > daysIn(y, m)) return false;
  *day = (int32_t)daysFromCivil(y, m, d);
  return true;
}

bool parseUtc(const char *s, int64_t *utc) {
  int y, m, d, hh, mm, ss; char z, tail;
  if (!s || strlen(s) != 20 ||
      sscanf(s, "%4d-%2d-%2dT%2d:%2d:%2d%c%c", &y, &m, &d, &hh, &mm, &ss, &z, &tail) != 7 || z != 'Z')
    return false;
  if (y < 2020 || y > 2100 || m < 1 || m > 12 || d < 1 || d > daysIn(y, m) || hh > 23 || mm > 59 || ss > 59)
    return false;
  *utc = toUnix({y, m, d, hh, mm, ss});
  return true;
}

static void fmtDay(int32_t day, char *buf, size_t n) {
  Civil c = fromUnix((int64_t)day * 86400);
  snprintf(buf, n, "%04d-%02d-%02d", c.year, c.month, c.day);
}

// ---- events -----------------------------------------------------------------
struct EvName { const char *name; Event ev; };
static const EvName EVENTS[] = {
  {"sunset", Event::Sunset}, {"dusk_civil", Event::DuskCivil}, {"dusk_nautical", Event::DuskNautical},
  {"dusk_astronomical", Event::DuskAstro}, {"sunrise", Event::Sunrise}, {"dawn_civil", Event::DawnCivil},
  {"dawn_nautical", Event::DawnNautical}, {"dawn_astronomical", Event::DawnAstro},
};
static bool isEvening(Event e) { return e >= Event::Sunset && e <= Event::DuskAstro; }
static bool isMorning(Event e) { return e >= Event::Sunrise; }

static double altFor(Event e) {
  switch (e) {
    case Event::Sunset: case Event::Sunrise: return solar::SUNSET;
    case Event::DuskCivil: case Event::DawnCivil: return solar::CIVIL;
    case Event::DuskNautical: case Event::DawnNautical: return solar::NAUTICAL;
    default: return solar::ASTRONOMICAL;
  }
}
static Event fallbackOf(Event e) {           // astro -> nautical -> civil -> (none)
  switch (e) {
    case Event::DuskAstro: return Event::DuskNautical;
    case Event::DuskNautical: return Event::DuskCivil;
    case Event::DawnAstro: return Event::DawnNautical;
    case Event::DawnNautical: return Event::DawnCivil;
    default: return Event::Utc;               // no further fallback
  }
}

static bool parsePolicy(const char *s, CamPolicy *p) {
  if (!s) return false;
  if (!strcmp(s, "none")) { *p = CamPolicy::None; return true; }
  if (!strcmp(s, "best_effort")) { *p = CamPolicy::BestEffort; return true; }
  if (!strcmp(s, "required")) { *p = CamPolicy::Required; return true; }
  return false;
}

static bool parseTime(JsonVariantConst v, const char *path, bool evening, Messages *err, TimeSpec *out) {
  if (!v.is<JsonObjectConst>()) { err->add("%s: must be an object", path); return false; }
  JsonObjectConst o = v.as<JsonObjectConst>();
  bool ok = true;
  if (!o["utc"].isNull()) {
    const char *s = o["utc"].as<const char *>();
    if (!parseUtc(s, &out->utc)) { err->add("%s.utc: not a UTC time like 2026-09-20T04:15:00Z", path); ok = false; }
    out->ev = Event::Utc;
    if (!o["event"].isNull()) { err->add("%s: give either event or utc, not both", path); ok = false; }
    return ok;
  }
  const char *name = o["event"].as<const char *>();
  if (!name) { err->add("%s: needs \"event\" or \"utc\"", path); return false; }
  bool found = false;
  for (const EvName &e : EVENTS) if (!strcmp(name, e.name)) { out->ev = e.ev; found = true; }
  if (!found) { err->add("%s.event: unknown event '%s'", path, name); return false; }
  if (evening && !isEvening(out->ev)) { err->add("%s.event: '%s' is a morning event; start needs sunset/dusk_*", path, name); ok = false; }
  if (!evening && !isMorning(out->ev)) { err->add("%s.event: '%s' is an evening event; stop needs sunrise/dawn_*", path, name); ok = false; }
  if (!o["offset_min"].isNull()) {
    if (!o["offset_min"].is<int>()) { err->add("%s.offset_min: must be a whole number", path); return false; }
    int off = o["offset_min"].as<int>();
    if (off < -180 || off > 180) { err->add("%s.offset_min: %d is outside +-180", path, off); ok = false; }
    out->offset_min = (int16_t)off;
  }
  return ok;
}

static bool parseTarget(JsonVariantConst v, const char *path, Messages *err, bool *moon) {
  const char *s = v.as<const char *>();
  if (s && !strcmp(s, "sky")) { *moon = false; return true; }
  if (s && !strcmp(s, "moon")) { *moon = true; return true; }
  err->add("%s: must be sky or moon", path);
  return false;
}

static bool knownCamera(const ScheduleLimits &lim, const char *name) {
  if (!lim.cameras) return true;
  for (int i = 0; i < lim.n_cameras; i++) if (!strcmp(lim.cameras[i], name)) return true;
  return false;
}

static bool parseCamera(JsonVariantConst v, const char *path, const ScheduleLimits &lim, Messages *err, char *out) {
  const char *s = v.as<const char *>();
  if (!s || !*s) { err->add("%s: must be a non-empty string", path); return false; }
  if (strlen(s) >= 24) { err->add("%s: name longer than 23 characters", path); return false; }
  if (!knownCamera(lim, s)) { err->add("%s: unknown camera profile '%s'", path, s); return false; }
  strcpy(out, s);
  return true;
}

// ---- parse + validate ---------------------------------------------------------
bool parseSchedule(const char *json, size_t len, const ScheduleLimits &lim, int64_t now_utc,
                   ScheduleDoc *d, Messages *err, Messages *warn) {
  static Messages scratch;                     // 1.5 KB: off the (ESP32) stack
  if (!warn) { scratch = Messages(); warn = &scratch; }
  *d = ScheduleDoc();

  // ---- layer 1: parse ----
  if (len > lim.max_bytes) { err->add("schedule is %u bytes; the limit is %u", (unsigned)len, (unsigned)lim.max_bytes); return false; }
  JsonDocument doc;
  DeserializationError je = deserializeJson(doc, json, len);
  if (je) { err->add("JSON: %s", je.c_str()); return false; }
  if (!doc.is<JsonObjectConst>()) { err->add("top level must be an object"); return false; }

  // ---- layer 2: schema and range ----
  if (doc["version"].as<int>() != 1) err->add("version: must be 1");
  JsonObjectConst site = doc["site"].as<JsonObjectConst>();
  if (site.isNull()) {
    err->add("site: missing");
  } else {
    if (!site["lat"].is<double>()) err->add("site.lat: missing or not a number");
    else if (fabs(d->lat = site["lat"].as<double>()) > 90) err->add("site.lat: %.4f is outside -90..90", d->lat);
    if (!site["lon"].is<double>()) err->add("site.lon: missing or not a number");
    else if (fabs(d->lon = site["lon"].as<double>()) > 180) err->add("site.lon: %.4f is outside -180..180", d->lon);
    if (!site["tz_offset_min"].is<int>()) err->add("site.tz_offset_min: missing or not a whole number");
    else if (abs(d->tz_offset_min = site["tz_offset_min"].as<int>()) > 840) err->add("site.tz_offset_min: %d is outside +-840", d->tz_offset_min);
  }
  d->version = doc["version"].as<int>();

  JsonObjectConst def = doc["defaults"].as<JsonObjectConst>();
  if (def.isNull()) {
    err->add("defaults: missing");
  } else {
    parseTime(def["start"], "defaults.start", true, err, &d->def_start);
    parseTime(def["stop"], "defaults.stop", false, err, &d->def_stop);
    if (d->def_start.ev == Event::Utc || d->def_stop.ev == Event::Utc)
      err->add("defaults: start/stop must be events -- a fixed utc belongs on a night");
    if (!def["start_angle_deg"].is<double>()) err->add("defaults.start_angle_deg: missing or not a number");
    else d->def_angle = def["start_angle_deg"].as<double>();
    parseCamera(def["camera"], "defaults.camera", lim, err, d->def_camera);
    if (!parsePolicy(def["camera_policy"].as<const char *>(), &d->def_policy))
      err->add("defaults.camera_policy: must be none, best_effort or required");
    if (!def["target"].isNull()) parseTarget(def["target"], "defaults.target", err, &d->def_moon);
    if (!def["gps_policy"].isNull()) {
      const char *g = def["gps_policy"].as<const char *>();
      if (g && !strcmp(g, "none")) d->gps = GpsPolicy::None;
      else if (g && !strcmp(g, "best_effort")) d->gps = GpsPolicy::BestEffort;
      else err->add("defaults.gps_policy: must be none or best_effort");
    }
  }

  if (!doc["repeat_after_list"].isNull()) {
    if (!doc["repeat_after_list"].is<bool>()) err->add("repeat_after_list: must be true or false");
    d->repeat = doc["repeat_after_list"].as<bool>();
  }

  JsonArrayConst nights = doc["nights"].as<JsonArrayConst>();
  if (!doc["nights"].isNull() && nights.isNull()) err->add("nights: must be an array");
  if (nights.size() > (size_t)ScheduleDoc::MAX_NIGHTS)
    err->add("nights: %u entries; the limit is %d", (unsigned)nights.size(), ScheduleDoc::MAX_NIGHTS);
  int i = 0;
  for (JsonVariantConst v : nights) {
    if (i >= ScheduleDoc::MAX_NIGHTS) break;
    char path[32];
    snprintf(path, sizeof path, "nights[%d]", i);
    NightEntry &e = d->nights[d->n];
    if (!v.is<JsonObjectConst>()) { err->add("%s: must be an object", path); i++; continue; }
    JsonObjectConst o = v.as<JsonObjectConst>();
    char p2[48];
    // layer 3 starts here for the date: real, and strictly ascending
    if (!parseDate(o["date"].as<const char *>(), &e.day)) { err->add("%s.date: not a real date like 2026-09-18", path); i++; continue; }
    if (d->n > 0 && e.day <= d->nights[d->n - 1].day) err->add("%s.date: dates must be strictly ascending", path);
    e.skip = o["skip"].as<bool>();
    if (!o["start"].isNull()) { snprintf(p2, sizeof p2, "%s.start", path); e.has_start = parseTime(o["start"], p2, true, err, &e.start); }
    if (!o["stop"].isNull())  { snprintf(p2, sizeof p2, "%s.stop", path);  e.has_stop = parseTime(o["stop"], p2, false, err, &e.stop); }
    if (!o["start_angle_deg"].isNull()) {
      if (!o["start_angle_deg"].is<double>()) err->add("%s.start_angle_deg: not a number", path);
      else { e.has_angle = true; e.angle = o["start_angle_deg"].as<double>(); }
    }
    if (!o["camera"].isNull()) { snprintf(p2, sizeof p2, "%s.camera", path); e.has_camera = parseCamera(o["camera"], p2, lim, err, e.camera); }
    if (!o["target"].isNull()) { snprintf(p2, sizeof p2, "%s.target", path); e.has_target = parseTarget(o["target"], p2, err, &e.moon); }
    if (!o["camera_policy"].isNull()) {
      if (!parsePolicy(o["camera_policy"].as<const char *>(), &e.policy)) err->add("%s.camera_policy: must be none, best_effort or required", path);
      else e.has_policy = true;
    }
    d->n++;
    i++;
  }
  if (!err->empty()) return false;            // layers 3-4 need a sound document

  // ---- layers 3 + 4: resolve every listed night ----
  Schedule s(*d, lim);
  int64_t prev_stop = 0; int prev_i = -1;
  for (int k = 0; k < d->n; k++) {
    const NightEntry &e = d->nights[k];
    if (e.skip) continue;
    char day[12]; fmtDay(e.day, day, sizeof day);
    NightPlan p; const char *why;
    if (!s.resolve(e.day, &p, &why)) {
      if (!strcmp(why, "stop is not after start")) err->add("nights[%d] (%s): stop is not after start", k, day);
      else warn->add("nights[%d] (%s): %s -- it will be skipped", k, day, why);    // polar summer
      continue;
    }
    if (p.fallback) warn->add("nights[%d] (%s): no astronomical darkness, falls back %d level%s", k, day, p.fallback, p.fallback > 1 ? "s" : "");
    if (p.stop_utc <= p.start_utc) { err->add("nights[%d] (%s): stop is not after start", k, day); continue; }
    double h = (p.stop_utc - p.start_utc) / 3600.0;
    if (h > lim.max_night_h) err->add("nights[%d] (%s): %.1f h is longer than %.0f h", k, day, h, lim.max_night_h);
    if (prev_i >= 0 && p.start_utc < prev_stop) err->add("nights[%d] (%s): starts before nights[%d] ends", k, day, prev_i);
    prev_stop = p.stop_utc; prev_i = k;
    double end = p.start_deg + (p.stop_utc - p.start_utc) * rateDegPerS(p.rate);
    if (p.start_deg < lim.travel_min_deg)
      err->add("nights[%d] (%s): start angle %.1f is below the envelope (%.1f)", k, day, p.start_deg, lim.travel_min_deg);
    else if (end > lim.travel_max_deg)
      err->add("nights[%d] (%s): would track to %.1f deg, past the envelope (%.1f)", k, day, end, lim.travel_max_deg);
  }

  // ---- layer 4 for the defaults: the next year of default nights ----
  if (d->repeat) {
    int32_t today = (int32_t)((now_utc + d->tz_offset_min * 60) / 86400);
    int32_t from = d->n ? (d->nights[d->n - 1].day + 1 > today ? d->nights[d->n - 1].day + 1 : today) : today;
    double worst_end = -1e9; int32_t worst_day = 0, long_day = 0; double longest_h = 0;
    for (int32_t day = from; day < from + 366; day++) {
      NightPlan p; const char *why;
      if (!s.resolve(day, &p, &why, false)) continue;       // moon nights: the dark window bounds them
      double end = p.start_deg + (p.stop_utc - p.start_utc) * rateDegPerS(p.rate);
      if (end > worst_end) { worst_end = end; worst_day = day; }
      double h = (p.stop_utc - p.start_utc) / 3600.0;
      if (h > longest_h) { longest_h = h; long_day = day; }
    }
    if (longest_h > lim.max_night_h) {
      char ld[12]; fmtDay(long_day, ld, sizeof ld);
      err->add("defaults: the night of %s would last %.1f h, longer than %.0f h", ld, longest_h, lim.max_night_h);
    }
    char day[12]; fmtDay(worst_day, day, sizeof day);
    if (d->def_angle < lim.travel_min_deg)
      err->add("defaults.start_angle_deg: %.1f is below the envelope (%.1f)", d->def_angle, lim.travel_min_deg);
    else if (worst_end > lim.travel_max_deg)
      err->add("defaults: the night of %s would track to %.1f deg, past the envelope (%.1f)", day, worst_end, lim.travel_max_deg);
  }
  return err->empty();
}

// ---- resolve -------------------------------------------------------------------
const NightEntry *Schedule::entry(int32_t day) const {
  for (int i = 0; i < d_.n; i++) if (d_.nights[i].day == day) return &d_.nights[i];
  return nullptr;
}

bool Schedule::timeFor(const TimeSpec &t, int32_t day, int64_t *utc, uint8_t *fell) const {
  if (t.ev == Event::Utc) { *utc = t.utc; return true; }
  Civil c = fromUnix((int64_t)day * 86400);
  for (Event e = t.ev; e != Event::Utc; e = fallbackOf(e)) {
    int64_t at;
    if (solar::eventUtc(lat_, lon_, d_.tz_offset_min, c.year, c.month, c.day, altFor(e), isMorning(e), &at)) {
      *utc = at + (int64_t)t.offset_min * 60;
      return true;
    }
    (*fell)++;
  }
  return false;
}

bool Schedule::resolve(int32_t day, NightPlan *out, const char **why, bool clip_moon) const {
  const NightEntry *e = entry(day);
  if (e && e->skip) { *why = "skipped by the schedule"; return false; }
  if (!e) {
    bool after_list = d_.n == 0 || day > d_.nights[d_.n - 1].day;
    if (!d_.repeat || !after_list) { *why = "not scheduled"; return false; }
  }
  NightPlan p;
  p.day = day;
  p.gps = d_.gps;
  p.start_deg = e && e->has_angle ? e->angle : d_.def_angle;
  p.cam = e && e->has_policy ? e->policy : d_.def_policy;
  strcpy(p.camera, e && e->has_camera ? e->camera : d_.def_camera);
  uint8_t fell_a = 0, fell_b = 0;
  if (!timeFor(e && e->has_start ? e->start : d_.def_start, day, &p.start_utc, &fell_a) ||
      !timeFor(e && e->has_stop ? e->stop : d_.def_stop, day, &p.stop_utc, &fell_b)) {
    *why = "no darkness: the sun does not reach civil twilight";
    return false;
  }
  p.fallback = fell_a > fell_b ? fell_a : fell_b;
  p.valid = p.stop_utc > p.start_utc;
  if (!p.valid) { *why = "stop is not after start"; return false; }
  p.moon = e && e->has_target ? e->moon : d_.def_moon;
  if (p.moon) {
    p.rate = TrackRate::Lunar;
    int64_t a, b;
    if (clip_moon) {
      if (!moon::upWindow(lat_, lon_, p.start_utc, p.stop_utc, &a, &b)) { *why = "the moon is not up in the window"; return false; }
      if (b - a < 30 * 60) { *why = "the moon is up under 30 min of the window"; return false; }
      p.start_utc = a; p.stop_utc = b;
    }
  }
  *out = p;
  return true;
}

NightPlan Schedule::next(int64_t after_utc) {
  int32_t day = (int32_t)((after_utc + d_.tz_offset_min * 60) / 86400) - 1;
  int32_t last = d_.n ? d_.nights[d_.n - 1].day : day;
  for (int k = 0; k < 400; k++, day++) {
    if (!d_.repeat && day > last) break;
    NightPlan p; const char *why;
    if (!resolve(day, &p, &why)) { if (entry(day) || d_.repeat) last_skip_ = why; continue; }
    if (p.stop_utc > after_utc) return p;
  }
  return NightPlan();
}

NightPlan Schedule::refresh(const NightPlan &p) {
  NightPlan q; const char *why;
  return resolve(p.day, &q, &why) ? q : p;
}

}  // namespace app
