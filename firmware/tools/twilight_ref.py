"""Reference twilight times from Skyfield (DE421) for the firmware's solar
solver tests. Prints C initialisers: {lat, lon, y, m, d, alt, rising, utc}
-- the UTC time the sun's centre crosses `alt` (geometric, no refraction
except for sunrise/sunset's -0.833 convention) on the evening of local date
y-m-d (setting) or the following morning (rising). 0 = no crossing."""
import sys
from datetime import datetime, timedelta, timezone
from skyfield.api import load, wgs84
from skyfield import almanac

ts = load.timescale()
eph = load(sys.argv[1])
sun, earth = eph['sun'], eph['earth']

cases = [
    # lat, lon, tz_hours (for "local evening"), dates
    (34.6, -120.4, -8, ["2026-11-22", "2026-06-21", "2026-03-20", "2026-12-21"]),   # central coast CA
    (51.4779, 0.0, 0, ["2026-06-21", "2026-12-21", "2026-03-20"]),                 # Greenwich
    (-33.86, 151.21, 10, ["2026-06-21", "2026-12-21"]),                            # Sydney
    (69.65, 18.96, 1, ["2026-06-21", "2026-12-21"]),                               # Tromso
    (0.0, 0.0, 0, ["2026-03-20"]),
]
alts = [-0.833, -6.0, -12.0, -18.0]

def crossings(lat, lon, t0, t1, alt):
    loc = earth + wgs84.latlon(lat, lon)
    def above(t):
        a, _, _ = loc.at(t).observe(sun).apparent().altaz()
        return a.degrees > alt
    above.step_days = 1 / 48
    t, y = almanac.find_discrete(t0, t1, above)
    return [(ti, yi) for ti, yi in zip(t, y)]

for lat, lon, tz, dates in cases:
    for ds in dates:
        y, m, d = map(int, ds.split("-"))
        # local noon of the date -> next local noon: evening setting, then morning rising
        noon = datetime(y, m, d, 12, tzinfo=timezone.utc) - timedelta(hours=tz)
        t0 = ts.from_datetime(noon)
        t1 = ts.from_datetime(noon + timedelta(hours=24))
        for alt in alts:
            ev = crossings(lat, lon, t0, t1, alt)
            sets = [t for t, up in ev if not up]
            rises = [t for t, up in ev if up]
            s = int(round(sets[0].utc_datetime().timestamp())) if sets else 0
            r = int(round(rises[0].utc_datetime().timestamp())) if rises else 0
            print("  {%.4f, %.4f, %d, %d, %d, %d, %.3f, 0, %d}," % (lat, lon, tz, y, m, d, alt, s))
            print("  {%.4f, %.4f, %d, %d, %d, %d, %.3f, 1, %d}," % (lat, lon, tz, y, m, d, alt, r))

