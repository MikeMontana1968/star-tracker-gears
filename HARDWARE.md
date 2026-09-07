# Hardware — scheduled star tracker

Scope beyond the gearbox: a standalone device that wakes at dusk, configures and
starts a GoPro HERO6 night lapse, tracks at sidereal rate until dawn, stops the
camera, and rewinds ready for the next night.

---

## 1. Decisions, and why

### Controller: ESP32, not a Pi

| | ESP32 | Pi Zero W | Pi 4 |
|---|---|---|---|
| Idle power | ~0.3 W | ~0.9 W | ~3 W |
| Boot | instant | ~30 s | ~30 s |
| Survives a flat battery mid-write | yes | **SD corruption risk** | same risk |
| Step timing | hardware timers, no OS jitter | userspace, fine in practice | same |
| Web UI effort | moderate (ESPAsyncWebServer) | trivial (Flask) | trivial |
| RTC | needs DS3231 | needs DS3231 | needs DS3231 |

The deciding factor is not speed, it is **unattended robustness**. This device
sits outside for days and will eventually have its battery go flat mid-operation.
An ESP32 comes back up in a second with its LittleFS schedule intact; a Pi with a
half-written SD card may not come back at all. The extra 0.6 W also costs ~7 Wh a
night, which is real against a 43 Wh budget.

A Pi Zero 2 W with a **read-only root filesystem** is a legitimate alternative if
you would rather write Python — that closes the corruption gap. It does not close
the power or boot-time gap. A Pi 4 is the wrong shape for a battery device.

### Camera control: WiFi, not Bluetooth

Your earlier Pi-Zero/HERO5 Bluetooth failure was not a Pi problem — it was
expected. **The documented GoPro BLE API (OpenGoPro) starts at HERO9.** HERO5 and
HERO6 are controlled over WiFi through the `gpControl` HTTP API, which is well
documented by the community
([KonradIT/goprowifihack](https://github.com/KonradIT/goprowifihack/tree/master/HERO6)).

- Camera is the access point; controller joins it. Base URL `http://10.5.5.9/gp/gpControl`
- Wake from standby with a **Wake-on-LAN magic packet** to the camera's WiFi MAC,
  UDP port 9 (documented for HERO6)
- `GET /gp/gpControl/status` returns the full state as JSON
- `GET /gp/gpControl/setting/<id>/<value>` for exposure, interval, ISO limit
- `GET /gp/gpControl/command/shutter?p=1` / `p=0` start and stop
- `GET /gp/gpControl/command/system/sleep` standby

**Do not hardcode setting IDs from a blog post.** `GET /gp/gpControl` returns the
camera's own settings tree — dump it from your actual HERO6 and read the night
lapse mode and shutter IDs out of it. Firmware ships a `discover` command to do
exactly that.

**Caveat to verify on your camera:** `system/sleep` is standby, not off. The
camera keeps its WiFi radio listening so WoL can reach it, and that draws from
the internal battery. Budget for it, or cut the USB rail during the day and let
the internal battery hold standby — the load switch in the BOM lets firmware
choose. Measure it before trusting either.

### Phone interface: captive web UI, not an app

The ESP32 raises its own AP on a button press, serves a single page, and takes a
JSON schedule. No app to install, no pairing, works from any phone or laptop, and
sidesteps BLE entirely. **One radio means one mode at a time** — the controller
is either its own AP (configuring) or a station on the GoPro's AP (commanding the
camera). Never both. That constraint is what makes the state machine simple.

### Position: absolute encoder, so "home" needs no search

An **AS5600** on the output shaft reports absolute angle over I²C at power-up —
12-bit, 0.088° (5.3 arcmin). No homing sweep, no index hunt, and the same sensor
gives you skip detection during tracking.

This requires the output shaft to **rotate in bearings** rather than being a fixed
nail with the wheel turning on it, so the magnet can sit on the shaft end. That is
a change worth making regardless: the output carries the camera and deserves real
bearings.

Cheaper alternative: an opto-interrupter and a printed flag on the wheel rim,
~±0.06° repeatable, but it needs a homing sweep at power-up and gives no
continuous feedback. Listed as optional in the BOM.

---

## 2. Operating cycle

```
  IDLE (deep sleep, RTC alarm armed)
    |
    v  alarm at astronomical dusk - lead time
  ORIENT      read AS5600, slew to the start angle, approach FORWARD
    |
    v
  WAKE CAM    WoL magic packet, join GoPro AP, poll /status until ready
    |
    v
  CONFIGURE   set night lapse mode, shutter, ISO limit, interval
    |
    v
  RECORD      shutter?p=1, then DROP WIFI and start stepping
    |
    v  ...tracking at 6573.80 us/microstep...
    v  alarm at sunrise
  STOP CAM    rejoin AP, shutter?p=0, system/sleep, drop WiFi
    |
    v
  REWIND      slew back, overshoot, approach start FORWARD
    |
    v
  IDLE
```

Two rules that are not obvious:

**Always approach the start angle from the forward (tracking) direction.**
Backlash referred to the output is dominated by the final mesh:
`0.35 mm / 96 mm = 3.6 mrad ≈ 750 arcsec`. Rewinding reverses the gear train, so
if you stop on the reverse stroke that lost motion is taken up during your first
minutes of tracking — as drift. Overshoot and come forward.

**Keep the mesh loaded.** With a perfectly balanced camera the tracking torque
is near zero and the teeth can rattle across the backlash. Deliberately bias the
counterweight so the load always pulls the same way.

---

## 3. Power

Measured against a 12-hour night:

| Load | Power | Per night |
|---|---|---|
| GoPro HERO6, night lapse over USB-C | 2.5 W | 30.0 Wh |
| Stepper + TMC2209 tracking | 0.75 W | 9.0 Wh |
| ESP32 awake, radio off | 0.3 W | 3.6 Wh |
| RTC + encoder | 0.05 W | 0.6 Wh |
| Rewind slew | 3 W for ~4 min | 0.2 Wh |
| **Total** | | **~43 Wh** |

**The camera is 69% of the budget.** Any power optimisation that is not about the
GoPro is noise.

Your 3-day schedule therefore needs ~130 Wh plus daytime standby. That is a
12 V 12 Ah LiFePO4 — about 1.8 kg. **A 12 V 6 Ah pack plus a 20 W solar panel is
lighter, cheaper, and makes the device indefinitely autonomous** — 43 Wh is about
four hours of decent sun. For a device whose entire job is to sit outside all day
doing nothing, solar is the obvious fit.

---

## 4. Bill of materials

Prices are rough and vary; treat as sizing, not quotes.

### Control

| Qty | Item | ~USD | Notes |
|---|---|---|---|
| 1 | ESP32-WROOM-32 devkit | 8 | 30-pin, USB-serial onboard |
| 1 | DS3231 RTC module + CR2032 | 4 | ±2 ppm. Do **not** use DS1307 |
| 1 | AS5600 encoder module | 4 | I²C 0x36 |
| 1 | Diametric magnet 6 × 2.5 mm | 2 | must be **diametric**, not axial |
| 1 | TMC2209 StepStick (BTT / Fysetc) | 7 | UART mode |
| 1 | 100 µF 25 V low-ESR electrolytic | 1 | **across VMOT at the driver** |
| 1 | High-side load switch or P-MOSFET module, 5 V 3 A | 3 | lets firmware cut camera power |
| 1 | Perfboard + headers | 6 | or a StepStick carrier |
| 1 | Momentary button + 10 k | 1 | enter config AP mode |

### Motion

| Qty | Item | ~USD | Notes |
|---|---|---|---|
| 1 | NEMA 17 pancake, ~13 N·cm, 0.7 A | 14 | 250× torque margin; run at 300–400 mA |
| 2 | 625ZZ bearing 5 × 16 × 5 | 3 | output shaft only |
| 1 | 5 mm ground shaft, 100 mm | 4 | output shaft, carries encoder magnet |
| 2 | 5 mm shaft collar | 4 | output shaft only — **not** at shafts 2 or 3 |
| 1 | 20d bright common nails, small box | 6 | 4.88 mm, shafts 1–4 |
| 1 | M3 hardware assortment | 8 | grub screws, caps, washers |

### Power

| Qty | Item | ~USD | Notes |
|---|---|---|---|
| 1 | 12 V 6 Ah LiFePO4 with BMS | 45 | ~1.8 nights unaided |
| 1 | 5 V 3 A buck converter | 6 | camera rail; **not** a linear regulator |
| 1 | 20 W solar panel + PWM/MPPT controller | 45 | *recommended* — makes it indefinite |
| 1 | Inline fuse holder + 3 A fuse | 3 | |
| 1 | XT60 or 5.5/2.1 mm barrel pair | 4 | |

### Camera

| Qty | Item | ~USD | Notes |
|---|---|---|---|
| 1 | GoPro HERO6 | — | have |
| 1 | Short right-angle USB-C cable | 8 | |
| 1 | 6-wire capsule slip ring | 12 | *optional* — a slack loop covers 180° |
| 1 | GoPro frame + ¼"-20 adapter | 8 | |
| 1 | Counterweight hardware | 6 | bias it to keep the mesh loaded |

### Enclosure

| Qty | Item | ~USD | Notes |
|---|---|---|---|
| 1 | IP54 project box | 15 | |
| 3 | Cable glands | 4 | |

**Roughly $180 without solar, $230 with.**

---

## 5. Pin map

| ESP32 | To | Note |
|---|---|---|
| GPIO 25 | TMC2209 STEP | LEDC for slew, timer ISR for tracking |
| GPIO 26 | TMC2209 DIR | |
| GPIO 27 | TMC2209 EN | active low |
| GPIO 17 | TMC2209 PDN_UART | via 1 kΩ for single-wire |
| GPIO 16 | TMC2209 UART RX | |
| GPIO 21 / 22 | I²C SDA / SCL | DS3231 @0x68, AS5600 @0x36 — no conflict |
| GPIO 4 | config button | to GND, internal pull-up |
| GPIO 34 | battery sense | ADC via divider; input-only pin |
| GPIO 35 | opto home flag | *optional*; input-only pin |
| GPIO 32 | camera 5 V load switch | |
| GPIO 2 | status LED | onboard |

Avoid GPIO 6–11 (flash) and 0/12/15 (strapping).

**Wiring rules that kill drivers if ignored:** 100 µF low-ESR across VMOT right
at the TMC2209; common ground between ESP32 and driver; never unplug the motor
with power applied; TMC2209 VDD from **3.3 V**, not 5 V.

---

## 6. Open items

- [ ] Dump `/gp/gpControl` from the actual HERO6 and pin down night-lapse mode
      and shutter setting IDs
- [ ] Measure HERO6 standby draw with WiFi listening — decides whether the day
      rail stays on
- [ ] Confirm WoL wakes your camera reliably from cold-ish standby
- [ ] Redesign the output shaft: two 625ZZ bearings, rotating shaft, encoder
      magnet at the lower end, camera platform above
- [ ] Cable routing for 180° of sweep — slack loop vs slip ring
- [ ] Decide battery vs battery+solar
