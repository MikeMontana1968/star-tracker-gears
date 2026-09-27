# Firmware — implementation plan and status

Companion to **[HARDWARE.md](HARDWARE.md)**. This file is the resumable one: it
carries the architecture, the decisions already made, the pin map as built, and
the slice-by-slice build order. **HARDWARE.md §5 is superseded by §4 here** —
that table predates the OLED board and the DS3231 interrupt line.

**Status: nothing written yet beyond the timing core.** Slice 0 is next.

---

## 0. Where this stands

| Slice | What | Blocked on | State |
|---|---|---|---|
| 0 | Board bring-up: verify the actual pinout, PlatformIO env | — | **next** |
| 1 | `motion/` — step engine, position counter, both rate profiles | slice 0 | not started |
| 2 | `app/fsm` + fake camera + host-side simulator | — | not started |
| 3 | `hal/` — DS3231, AS5600, TMC2209 UART, battery, OLED, deep sleep | modules on hand | not started |
| 4 | Schedule parse/validate + solar solver (host-tested) | — | not started |
| 5 | Web server, status JSON, static UI, config AP | slice 2 | not started |
| 6 | Real GoPro driver + `discover`; answers the 3 hardware unknowns | HERO6 | not started |

Slices 2 and 4 need no hardware at all and are unit-testable on the laptop.
Start slice 0 and slice 2 in parallel if you want to move fast.

`firmware/sidereal_drive/sidereal_drive.ino` stays where it is as the reference
artefact. Its accumulator migrates into `lib/motion/`; the sketch is not the
build target.

---

## 1. Decisions taken (and why) — 2026-09-17

| # | Decision | Rationale |
|---|---|---|
| D1 | **Position-servo step engine, not deadline-based** | Jitter is free, lost steps are not. Servoing to a computed position makes the drive immune to WiFi stalls and gives free mid-night resume. §2 |
| D2 | **Tracking is the mission; the camera is best-effort** | Bench repeatability runs need tracking with no camera at all, and other camera bodies are coming. Camera failure logs and continues. §3 |
| D3 | **Rewind for v1**, forward-only noted as the better end state | Forward-only removes every direction reversal but needs the slip ring. A slack cable loop is the v1 reality and the cable must not bind. |
| D4 | **DS3231 INT on GPIO 33**, wired-OR with the config button | Was missing from the pin map entirely. Deep sleep cannot be woken by an alarm that is not connected to anything. §4 |
| D5 | **On-device solar computation**, with absolute-UTC override | Makes the 3-day schedule an override on indefinite operation, which is what justifies the solar panel. §5 |
| D6 | **Camera setting IDs live in a data file**, never in code | The HERO6 gpControl tree is unmeasured. A `discover` command dumps it; firmware reads a profile. Unblocks all firmware work now. §6 |
| D7 | **Status by 1 Hz polling of one JSON struct** | A dropped SSE stream looks identical to "nothing is happening" — the worst failure mode to make invisible on an unattended device. §7 |
| D8 | **AP up during TRACK**, config flag, default on while debugging | ~100 mW, ~1.2 Wh of a 43 Wh night. The GoPro does not need the radio once the shutter is running. |
| D9 | **PlatformIO, not the Arduino IDE** | Host-side unit tests (`platform = native`) are the only way to validate a 3-night schedule without waiting 3 nights. §8 |

### Deferred to v2 — GPS + compass

A GPS module solves time authority and site coordinates in one part, and a
magnetometer helps the polar alignment that the README says dominates everything.

> ### REMINDER, as requested: wire the GPS PPS output
>
> The 1 pulse-per-second edge is what turns a GPS from "knows the time to a
> second" into a disciplined reference. **Reserve GPIO 36 (VP)** for it — input
> only, interrupt-capable, ADC1, and nothing else here wants it. Reserve
> **GPIO 13 / 14** for the GPS UART. A compass (QMC5883L `0x0D` or HMC5883L
> `0x1E`) drops onto the existing I²C bus with no address conflict.
>
> Note what PPS is and is not for: it disciplines the **wall clock**, and it may
> legitimately discipline the step accumulator's *rate* between nights. It must
> never correct step *phase* mid-track — see the rule in §5.

---

## 2. Motion: one ISR, one position counter, two rate profiles

Four layers. The boundary that matters is between the top two:

```
  app/     state machine, schedule, policy          core 0, soft realtime
  net/     web server, GoPro client, WiFi mode      core 0, blocking allowed
  ----------------------------------------------------------------
  motion/  position counter, rate profiles, ISR     core 1, hard realtime
  hal/     TMC2209, AS5600, DS3231, OLED, ADC       either
```

**The step generator must be unable to lose a step no matter what the WiFi stack
does.** `WiFi.begin()`, DNS and `HTTPClient` block for tens to hundreds of ms
inside the Arduino stack. At 152 steps/s a 200 ms stall is 30 dropped steps —
3 arcsec of permanent output error that nothing downstream can detect.

### The inversion

Do not ask "is a step due?". Ask "where should I be?":

```
target_usteps(t) = (elapsed_us * USTEPS_PER_REV) / SIDEREAL_US   // 64-bit, exact
while (pos_usteps < target_usteps) emit_one_step();
```

Same Bresenham arithmetic as the existing sketch, different subject. Block it for
200 ms and it emits 30 pulses back to back and is exactly where it should be.

Consequences worth naming:

- **Self-healing against any core-0 stall.** No critical sections around WiFi.
- **Tracking and slew become one mechanism** with two `target()` functions. One
  position counter, one place that touches DIR, no handoff seam.
- **Mid-night resume is free.** Reboot at 02:00, read the encoder, recompute
  `target` from `now - start_time`, catch up in one slew. You lose frames, not
  the night. This is impossible with a deadline-based stepper.

Implementation: a GPTimer ISR at a fixed **20 kHz tick pinned to core 1**,
emitting at most N pulses per tick. 50 µs of jitter, which is 0.0008 arcsec of
sidereal motion. Irrelevant.

### Two rate profiles

| | Tracking | Slew (orient / rewind) |
|---|---|---|
| Rate | 152.13 usteps/s | ~6.7 kHz pulses |
| MRES | 1/32 | **1/8** (set over UART) |
| Target fn | Bresenham vs. sidereal clock | trapezoid ramp to a position |
| Duration | 12 h | ~4.1 min for 180.5° |

180.5° of output is 1024 motor revolutions. At 1/32 that is 6.55 M pulses and
would need 27 kHz. At 1/8 it is 1.64 M pulses at 6.7 kHz — and the TMC2209's
MicroPlyer interpolates to 1/256 internally regardless, so nothing is lost.

> **Trap: `pos_usteps` must stay in one canonical unit.** Keep it in 1/32 units
> always; a 1/8 pulse advances it by 4. Do not let the counter's meaning change
> with MRES. That works on the bench and loses a revolution at 3 a.m.

### Backlash discipline lives in exactly one function

```
moveToForward(int64_t target_usteps)   // the ONLY thing that writes DIR
```

If the move would arrive travelling in reverse, overshoot past the target by
`BACKLASH_USTEPS` and come back forward. Nothing else may set direction.

| Quantity | Value |
|---|---|
| Output backlash (final mesh, 0.35 mm / 96 mm) | 3.6 mrad ≈ **750 arcsec** = 0.208° |
| usteps per output degree | 36,409 |
| `BACKLASH_USTEPS` | ~7,600 → **use 10,000** for margin |
| Overshoot cost at slew | 2,500 pulses ≈ 0.37 s |

750 arcsec is the largest correctable error in the machine and it is entirely a
software discipline problem. With D3 (rewind), every single night crosses it.

### Travel envelope is a hard limit, enforced in firmware

12 h of tracking is **180.5°** of output rotation. With a slack cable loop rather
than a slip ring, the cable sets a physical arc. The firmware must refuse a
schedule that would run past it rather than discover it at 4 a.m. by pulling the
USB-C out of the camera.

`TRAVEL_MIN_DEG` / `TRAVEL_MAX_DEG` are **compile-time constants**, not schedule
fields. A phone must not be able to widen them. Measure the real arc once the
cable is routed and set them then.

> **Encoder wrap:** the AS5600 is 12-bit over one revolution and the output turns
> <1 rev per night. Orient the magnet so the 180.5° working arc does not cross
> the 0/4095 boundary — put the start near raw 90° so the sweep ends near 271°.
> Then no wrap handling is needed anywhere.

---

## 3. State machine

The camera path is a **best-effort side branch**, not a gate (D2).

```
                        +--------------------------------------+
  power on              |                                      |
    |                   |                                      |
    v                   |                                      |
  BOOT --> HOME --> IDLE (deep sleep; GPIO33 wake armed)        |
           |         |  ^                        radio: OFF    |
    encoder bad      |  +---------- button --> CONFIG_AP -------+
           |         |                         radio: AP
           v         |  RTC alarm, start - lead               timeout 10 min
         FAULT       v
           ^     PREFLIGHT   battery, encoder, schedule, envelope check
           |         |       radio: OFF            deadline 10 s
           |         v
           |      ORIENT     moveToForward(start_angle)
           |         |       radio: OFF            deadline 6 min
           |         v
           |    +- CAM_SEQUENCE ------------------------------+
           |    |  CAM_POWER -> CAM_WAKE -> CAM_CONFIG -> START|
           |    |  radio: STA(gopro)      deadline 150 s total |
           |    |                                              |
           |    |  any failure, or camera_policy == none:      |
           |    |  log, set camera.result, DROP THROUGH -------+--+
           |    +--------------------+-------------------------+  |
           |                         v                            |
           |                      TRACK  <--------------------------+
           |                         |    accumulator running
           |                         |    radio: OFF or AP (D8)
           |                         |
           |   stall / encoder divergence / battery low / envelope
           |                         +----------------> SAFE_STOP
           |                         v stop_time reached        |
           |                      CAM_STOP   best-effort, 30 s  |
           |                         |       radio: STA(gopro)  |
           |                         v                          v
           |                      REWIND <----------------------+
           |                         |  moveToForward(start), deadline 8 min
           |                         v
           +---- mechanical ----  write night report, re-arm alarm --> IDLE
```

### `camera_policy` — the D2 knob

| Value | Behaviour |
|---|---|
| `none` | CAM_SEQUENCE skipped entirely. **Bench repeatability runs.** |
| `best_effort` | **Default.** Failure is logged, the night tracks anyway. |
| `required` | Failure means the night is abandoned, back to IDLE. |

A schedule night may override the global default. `required` exists for the
future case where the frames are the whole point and 9 Wh of tracking without
them is waste; it is not the default now.

### Rules baked into the FSM

- **Every state has a wall-clock deadline** whose expiry leads somewhere safe.
  The deadline is checked in the FSM tick, never inside a network call. A
  `WiFi.begin()` against a camera that never woke sits there forever otherwise.
- **`SAFE_STOP` is not `CAM_STOP`.** The abort path stops the motor *first*, then
  deals with the camera best-effort with short timeouts — whatever triggered the
  abort may be the same thing hanging the network.
- **FAULT is only for damage or impossibility**: encoder not responding at boot,
  commanded/encoder divergence past threshold, travel envelope violation. Camera
  trouble, low battery and a bad schedule are all *skipped nights*, not faults.
- **Persist on transition, not on tick.** `{state, pos_usteps, night_index,
  last_completed_utc, fault_code}` to NVS. A dozen writes a night — nothing for
  flash wear.
- **Persist position even though the encoder is absolute.** A disagreement
  between "where I thought I was" and "where the magnet says I am" is the single
  most informative diagnostic this machine can produce: it means the train
  slipped or a coupling let go.

### Divergence thresholds

| | Value |
|---|---|
| AS5600 resolution | 0.088° |
| Backlash | 0.208° |
| **Warn** (log, keep tracking) | 0.5° |
| **Fault** (SAFE_STOP) | 2.0° |

### Bench commands that serve D2

- `POST /api/command {test_track, duration_min}` — enter TRACK immediately with
  `camera_policy: none`. Straight repeatability of the drive.
- `POST /api/command {repeat_test, cycles, track_min}` — N × (orient → short
  track → rewind → read encoder at the start angle). Reports mean and σ of the
  **return error in arcsec**. This measures backlash discipline, train
  repeatability and slip in one number, and it is the number that tells you
  whether `BACKLASH_USTEPS` is right.

### Radio mode is a state attribute, never set ad hoc

`WIFI_AP_STA` exists but the AP and STA **must share a channel**. When the STA
joins the GoPro's AP, the softAP silently hops to the GoPro's channel and every
connected phone drops. Do not treat AP+STA as a capability. One function
transitions `radio_mode`; the state table declares what each state requires.

---

## 4. Pin map — as built

**Board: ESP32 devkit with integrated 0.96" SSD1306 OLED, CH340, micro-USB.**

> ### Slice 0, step 1: verify this board's actual pinout before wiring anything
>
> These OLED boards come in two families and the difference is destructive here:
>
> | Family | OLED pins | Collides with |
> |---|---|---|
> | **Generic 38-pin clone** (assumed below) | SDA 21 / SCL 22 | nothing — `0x3C`, `0x68`, `0x36` coexist |
> | **Heltec-style** | SDA 4 / SCL 15 / RST 16 | config button (4), TMC UART RX (16), strapping (15) |
>
> Also check: some of these boards put the onboard **LED on GPIO 25**, which is
> our STEP line, and some carry a WROVER module whose **PSRAM occupies GPIO
> 16/17**, which is our TMC2209 UART.
>
> **Procedure:** flash a bare I²C scanner on SDA 21 / SCL 22 and see whether
> `0x3C` answers. Then blink GPIO 25 and GPIO 2 and watch which onboard LED
> responds. Then check for PSRAM. Ten minutes, and it decides the whole loom.
>
> **If it is Heltec-style:** move the config button to GPIO 13, move the TMC UART
> off 16 (use 18/19), and accept GPIO 15 as an I²C line — idle-high with pull-ups
> satisfies the strapping requirement, so it is safe, but the boot log is lost.

| GPIO | To | Notes |
|---|---|---|
| 25 | TMC2209 **STEP** | upper row |
| 26 | TMC2209 **DIR** | upper row; written only by `moveToForward()` |
| 27 | TMC2209 **EN** | upper row; active low |
| 39 | TMC2209 **DIAG** | upper row; StallGuard. Input-only, 10k pull-down |
| 32 | camera 5 V load switch | upper row; drives Q3 -> Q2 gate |
| 34 | battery sense | upper row; input only, divider, **no internal pull-up** |
| 17 | TMC2209 **PDN_UART** | lower row, via 1 kΩ (ESP TX) |
| 16 | TMC2209 UART RX | lower row, straight onto PDN_UART |
| 21 | **I²C SDA** | lower row. DS3231 `0x68`, AS5600 `0x36`, OLED `0x3C` |
| 22 | **I²C SCL** | lower row; run the bus at 400 kHz |
| **4** | **DS3231 INT + config button** | lower row; wired-OR, 4.7 kΩ pull-up. RTC-capable |
| 19 | opto home flag | lower row; 10 kΩ pull-up |
| 15 | status LED | lower row; strapping pin, but the LED is high-Z at boot |
| **23** | *GPS PPS (v2)* | lower row |
| **5** | *GPS TX, ESP -> GPS RX (v2)* | lower row; strapping, idles high as UART TX |
| **18** | *GPS RX (v2)* | lower row |
| 33, 14, 13, 36 | spare header J11 | upper row; 36 is input-only |
| 35, 12, 0, 2, 1, 3 | unused | 12/0/2 strapping, 1/3 = USB serial |
| — | *compass (v2)* | I²C, `0x0D` / `0x1E` — no conflict |

> ### Why these moved (D4 revised, 2026-09-27)
>
> Laying out the board changed the map. On a through-hole board the ESP32's
> two header rows are **walls**: 2.54 mm pitch with ~1.7 mm pads leaves a
> 0.84 mm gap, and a 0.3 mm track needs 0.9 mm to pass legally — so no signal
> can cross a pin row *on either layer*, because through-hole pads block both.
> Everything crossing has to go the long way round the module's ends.
>
> The fix is to make nothing need to cross: every signal whose destination is
> the bottom connector strip now lives on the **lower** row, and everything
> serving the driver, the camera and the power input lives on the **upper**
> row. Four assignments changed as a result:
>
> | Signal | Was | Now | Why |
> |---|---|---|---|
> | DS3231 INT + button (`WAKE`) | GPIO 33 | **GPIO 4** | both are RTC-capable, so `ext0` deep-sleep wake is unaffected; GPIO 4 is on the lower row, next to the RTC socket |
> | GPS PPS | GPIO 36 | **GPIO 23** | GPIO 36 is input-only and now sits on the spare header instead |
> | GPS UART | 13 / 14 | **5 / 18** | lower row, beside the GPS connector |
> | Home flag | GPIO 35 | **GPIO 19** | lower row, beside its connector |
>
> Nothing about the firmware changes except the pin constants, and the PPS
> reservation still stands — it just lives on a different pin.

Avoid GPIO 6–11 (flash) and 0 / 12 / 15 (strapping).

### The GPIO 4 wired-OR — why one pin takes both wake sources

ESP32 `ext0` wakes on **one** pin. `ext1` offers `ALL_LOW` or `ANY_HIGH`, and two
independent active-low sources fit neither. The clean answer needs no extra parts:

**The DS3231 INT is open-drain and a button to ground is effectively open-drain.
Tie both to GPIO 4 with a single 4.7 kΩ pull-up.** Either one pulls the line low
and wakes the ESP32 through `ext0`.

Disambiguate in software on wake:

```
read DS3231 status register
  alarm flag set   -> it was the alarm; clear the flag
  flag clear       -> it was the button
```

The second input pin this section originally called for has been dropped: a
two-terminal switch cannot drive two nets, and the software already tells the
two sources apart from the alarm flag. One pin, one pull-up, zero extra parts.

### The OLED

An asset — it means you can see what the machine is doing without a phone. Two
rules:

- **Display OFF during TRACK.** It is a blue-white light source next to a lens
  doing wide-field astrophotography. This is not a power optimisation, it is an
  image-quality requirement. Wake it on button press for 30 s.
- **Issue the SSD1306 display-off command before deep sleep.** The panel draws
  even when the content is static.

Cost when on: ~0.05 W, ~0.6 Wh over a night. Update at 1–2 Hz, not faster — a
full 1024-byte frame at 400 kHz is ~26 ms of bus time. That blocks the I²C task
but **not** the step ISR, which never touches I²C.

Suggested pages, cycled by the button: `STATE + countdown` / `encoder angle + AGC
+ magnitude` / `battery + RTC` / `last night result`.

### Wiring rules that kill drivers if ignored

100 µF low-ESR across VMOT right at the TMC2209; common ground between ESP32 and
driver; never unplug the motor with power applied; TMC2209 VDD from **3.3 V**,
not 5 V.

---

## 5. Time, schedule, and validation

### Two clocks, never mixed

| Clock | Job | Why |
|---|---|---|
| **DS3231** | wall clock, alarms, schedule | ±2 ppm, battery-backed, survives deep sleep |
| **`esp_timer`** | step phase within one night | 64-bit µs, monotonic, ±20 ppm crystal |

The ESP32's own RTC in deep sleep runs off a ~150 kHz RC oscillator at roughly
±5% — ±40 minutes across a winter day. Useless for "wake at dusk". The DS3231 is
not optional.

> **Rule: never discipline the step accumulator to the DS3231.** A 1-second
> resolution reference correcting a 20 ppm crystal injects far more error than it
> removes. Wall clock and step phase are separate concerns; coupling them is how
> you get a drift bug you cannot reproduce. (A GPS PPS edge *can* legitimately
> discipline the rate in v2 — 1 ppm in a few minutes of averaging — but even then
> it corrects **rate between nights**, never **phase mid-track**.)

For scale: 20 ppm over a 12 h run is 0.86 s = **3.6 arcsec** at the output,
accumulated across the whole night. Three orders of magnitude below polar
alignment error. Leave it alone.

### Schedule format — intent, not just timestamps (D5)

```json
{
  "version": 1,
  "site": { "lat": 0.0000, "lon": 0.0000, "tz_offset_min": -480 },
  "defaults": {
    "start": { "event": "dusk_astronomical", "offset_min": 10 },
    "stop":  { "event": "dawn_astronomical", "offset_min": -10 },
    "start_angle_deg": -90.0,
    "camera": "nightlapse_30s_iso800",
    "camera_policy": "best_effort"
  },
  "nights": [
    { "date": "2026-09-18" },
    { "date": "2026-09-19", "start": { "utc": "2026-09-20T04:15:00Z" } },
    { "date": "2026-09-20", "skip": true }
  ],
  "repeat_after_list": true
}
```

`site` is a placeholder — put your own latitude and longitude in before the
first night, because the twilight times are computed from them.

The NOAA solar position algorithm is ~40 lines of `double` and resolves twilight
to well under a minute — ample against the lead time. `repeat_after_list` is what
keeps the device running past the three explicit nights, which is what makes the
solar panel worth having. An explicit `utc` overrides the computed event.

### Validation — four layers, and the fourth is the one that matters

1. **Parse.** ArduinoJson, fixed document size. Reject oversized input before it
   touches the filesystem.
2. **Schema and range.** lat ∈ [−90, 90], offsets ±180 min, known camera profile
   name, known `camera_policy`.
3. **Semantic.** stop > start; duration < 16 h; nights ascending and
   non-overlapping; date resolves to a real date.
4. **Mechanical envelope.**
   `start_angle + duration × sidereal_rate ≤ TRAVEL_MAX_DEG`.
   A schedule that fails this is hardware-damaging. Refuse it at upload with a
   readable reason.

Validation runs **at upload** — returning the full error list so you fix it on the
phone — **and again at every boot**, because half-written flash is exactly the
failure mode the ESP32 was chosen to survive.

**High-latitude edge case:** astronomical twilight has *no solution* above ~49°
in summer. The solver must return "no such event" rather than a garbage number.
Policy: fall back to nautical, then civil, then skip the night with a logged
reason.

### Storage

LittleFS, written atomically — `schedule.json.tmp` → fsync → rename. Keep
`schedule.good.json` as the last version that both validated *and* survived a
full night, and fall back to it on load failure. A device that comes up after a
brownout with a corrupt schedule and no fallback does nothing until you drive out
to it.

---

## 6. Camera layer (D6)

**Never hardcode a gpControl setting ID.** `camera_profile.json` on LittleFS maps
logical names to whatever this specific HERO6 firmware uses:

```json
{
  "model": "HERO6", "fw": "<from status>",
  "mac": "AA:BB:CC:DD:EE:FF",
  "commands": {
    "shutter_on":  "/gp/gpControl/command/shutter?p=1",
    "shutter_off": "/gp/gpControl/command/shutter?p=0",
    "sleep":       "/gp/gpControl/command/system/sleep",
    "mode_nightlapse": "/gp/gpControl/command/mode?p=0&sub_mode=4"
  },
  "settings": {
    "nightlapse_shutter":  { "id": 31, "values": { "30s": 8 } },
    "nightlapse_interval": { "id": 32, "values": { "continuous": 0 } },
    "iso_max":             { "id": 24, "values": { "800": 3 } }
  },
  "status_keys": { "recording": 8, "battery": 2, "sd_remaining": 54 }
}
```

**Those IDs are placeholders.** The `discover` command joins the camera,
`GET /gp/gpControl`, dumps the whole settings tree to LittleFS and serves it at
`GET /api/camera/dump`. Read it on the laptop, fill in the profile, done — that
is HARDWARE.md open item #1 turned into a firmware feature you run once.

A `Camera` interface with three implementations:

| Impl | Use |
|---|---|
| `GoProHero6` | the real thing, over `gpControl` |
| `FakeCamera` | canned status — everything above the interface is testable today |
| `NoCamera` | `camera_policy: none`; bench runs, and the seam where other bodies land (D2) |

The other two hardware unknowns become **config, not code**:

- **Standby draw** → boolean `camera_rail_off_during_day`. The GPIO 32 load
  switch already exists to make this a runtime choice. Measure later, flip it.
- **Does WoL work** → `CAM_WAKE` sends the magic packet 3×, then power-cycles the
  rail via GPIO 32 and retries. If WoL turns out unreliable, the rail cycle *is*
  the wake mechanism and the state machine does not change shape.

> **New unknown to verify early:** does the HERO6 keep its AP up and rejoinable
> after the ESP32 disconnects mid-record? If it drops WiFi while recording, the
> dawn stop cannot be issued and the fallback is cutting the load switch — which
> loses the last file. Cheap to test, and it changes the design.

---

## 7. Status and API (D7)

```
GET  /api/status          the struct, ~1 Hz polling
GET  /api/schedule        current, plus validation state
PUT  /api/schedule        upload; 200 + normalized, or 422 + error list
POST /api/command         {jog, goto, home, stop, test_track, repeat_test,
                           test_camera, discover, sleep_now}
GET  /api/log?since=N     ring buffer, NDJSON
GET  /api/night/last      last night report;  /api/night/list for the last 10
GET  /api/camera/dump     raw gpControl tree
POST /api/time            set DS3231 from the browser's clock
```

**Polling, not WebSocket, not SSE.** 1 Hz GET of ~400 bytes. A phone that locks
its screen, a backgrounded tab or a client walking out of range all recover with
zero connection state.

**The page is static.** `index.html` gzipped on LittleFS, far-future cache
headers, all dynamics from `/api/status`. No templating, no string building in
C++, and the UI can be developed against a mock JSON file in a browser with no
ESP32 attached.

### The status struct is the contract

```
state, state_entered_s, deadline_remaining_s, fault_code, fault_text,
pos_usteps, pos_deg, encoder_deg, encoder_err_deg, encoder_agc, encoder_magnitude,
tracking_elapsed_s, tracking_remaining_s, target_minus_actual_usteps,
rtc_utc, rtc_temp_c, rtc_lost_power,
battery_v, battery_pct,
camera: { policy, powered, awake, recording, battery_pct, sd_free_mb, last_error },
schedule: { valid, nights_loaded, next_start_utc, next_stop_utc, source },
radio: { mode, ssid, rssi, clients },
uptime_s, boot_count, free_heap, littlefs_free
```

Three fields earn their place specifically:

- **`encoder_agc` / `encoder_magnitude`** — the AS5600 reports "too far / too
  close" as a number. Put them on the front page of the UI *and* on the OLED
  during assembly. It converts an eyeballed air gap into a reading.
- **`target_minus_actual_usteps`** — the servo error from §2. Steady state
  oscillates 0–1. Anything large means something is starving the step task, and
  there is no other way to see it.
- **`camera.policy`** — so a bench run that is deliberately camera-less is never
  mistaken for a camera that failed.

### The night report is the real product

You are asleep while this runs. On entering IDLE, write one JSON record: planned
vs. actual start/stop, max encoder divergence, min battery voltage, camera
battery at start and end, frame count if status exposes it, `camera.result`, and
every warning. When the tracker does something odd on Tuesday you want Tuesday's
file, not a guess.

---

## 8. Build setup — PlatformIO (D9)

```
firmware/
  platformio.ini
  src/main.cpp
  include/config.h            pins, travel limits, thresholds — one place
  lib/
    motion/    StepEngine, RateProfile, moveToForward
    hal/       Ds3231, As5600, Tmc2209, Battery, Oled, LoadSwitch
    app/       Fsm, Schedule, Solar, Status, NightLog
    net/       WebApi, RadioMode, Camera + GoProHero6 / FakeCamera / NoCamera
  data/        index.html.gz, schedule.json, camera_profile.json
  test/
    test_solar/  test_schedule/  test_fsm_sim/
  sidereal_drive/sidereal_drive.ino    reference artefact, not a build target
```

```ini
[env:esp32-oled]
platform      = espressif32
board         = esp32dev
framework     = arduino
monitor_speed = 115200
upload_speed  = 460800            ; CH340 — drop to 115200 if uploads fail
board_build.filesystem = littlefs
build_flags   = -DCORE_DEBUG_LEVEL=3
lib_deps =
  bblanchon/ArduinoJson @ ^7.1.0
  teemuatlut/TMCStepper @ ^0.7.3
  adafruit/Adafruit SSD1306 @ ^2.5.9
  adafruit/Adafruit GFX Library @ ^1.11.9
  mathieucarbou/ESPAsyncWebServer @ ^3.1.0

; Host-side tests. This is the env that makes a 3-night schedule
; validate in seconds instead of three days.
[env:native]
platform       = native
test_framework = unity
build_flags    = -DSIM_BUILD -std=gnu++17
```

Write the **AS5600 driver by hand** (~30 lines of raw I²C). The libraries mostly
omit `AGC` and `MAGNITUDE`, which are the two registers that make the magnet gap
measurable rather than guessed.

Filesystem upload is `pio run -t uploadfs` and is a **separate step** from the
firmware upload — easy to forget after editing `data/`.

CH340 notes: Windows needs the CH340 driver; set `upload_port = COMx` if
auto-detect picks the wrong port; if the auto-reset circuit misbehaves, hold BOOT
during the "Connecting..." dots.

### The simulator is the thing to push hardest on

`test_fsm_sim` runs the FSM on the host against a fake clock at 1000×, a
`FakeCamera` and a fake encoder. You cannot debug a 3-day schedule in real time,
and every bug in this device manifests between midnight and dawn when you are not
watching. Cases that belong in it from day one:

- camera fails at every stage → night still tracks (D2)
- brownout at 02:00 → resume mid-track at the right position
- schedule that violates the travel envelope → refused
- astronomical twilight with no solution → falls back, then skips with a reason
- stop_time crosses a DST boundary / a month end / a year end
- battery drops below threshold mid-track → SAFE_STOP, camera stopped, rewound

---

## 9. Open items

**Firmware**

- [ ] **Slice 0, step 1: verify the OLED board pinout** (§4) before any wiring
- [ ] Confirm the module is WROOM-32, not WROVER (GPIO 16/17 = TMC UART)
- [ ] Measure the real cable arc, set `TRAVEL_MIN_DEG` / `TRAVEL_MAX_DEG`
- [ ] Set `BACKLASH_USTEPS` from a `repeat_test` run, not from the 0.35 mm estimate
- [ ] Choose the AS5600 magnet orientation so the working arc clears the 0/4095 wrap

**Hardware** — carried from HARDWARE.md §6

- [ ] Dump `/gp/gpControl` from the actual HERO6 → fills `camera_profile.json`
- [ ] Measure HERO6 standby draw → sets `camera_rail_off_during_day`
- [ ] Confirm WoL wakes the camera reliably
- [ ] **New:** does the HERO6 keep its AP rejoinable after the ESP32 disconnects
      mid-record? (§6)
- [ ] Cable routing for 180.5° of sweep — slack loop (v1) vs slip ring
- [ ] Battery vs battery + solar

**v2**

- [ ] GPS module — **wire PPS to GPIO 36**, UART to 13/14 (§1)
- [ ] Compass on the existing I²C bus for polar alignment assist
- [ ] Slip ring → forward-only operation, no direction reversal ever (D3)
