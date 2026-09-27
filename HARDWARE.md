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
| GPS, powered only for the nightly fix | 0.22 W | **<0.1 Wh** |
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

### Control board (see [hardware/PCB.md](hardware/PCB.md))

Everything in this table lives on the `star_tracker_ctrl` PCB. It is
**entirely through-hole**, and every module sits in a socket rather than being
soldered down.

| Qty | Ref | Item | ~USD | Notes |
|---|---|---|---|---|
| 1 | — | PCB, 127 x 76.2 mm, 2-layer, 1.6 mm | 12 | qty 5 from a cheap fab |
| 1 | — | ESP32 devkit, 38-pin, 0.96" OLED, CH340 | 10 | **have** — verify row spacing |
| 1 | J20/J21 | 1x19 female header, 2.54 mm | 2 | the ESP32 socket |
| 1 | — | TMC2209 StepStick, UART mode | 7 | BTT / Fysetc / Watterott |
| 1 | J30/J31 | 1x08 female header, 2.54 mm | 1 | the driver socket |
| 1 | — | DS3231 RTC module (ZS-042) + CR2032 | 4 | ±2 ppm. **Not** DS1307 |
| 1 | J6 | 1x06 female header | 1 | RTC socket |
| 1 | — | AS5600 encoder module | 4 | remote, on the encoder bracket |
| 1 | — | Diametric magnet 6 x 2.5 mm | 2 | must be **diametric** |
| 1 | — | **GY-NEO6MV2 GPS module** (NEO-6M) + antenna | 8 | time, date and position. **PPS is not on its 4-pin header** — solder a lead at the PPS LED |

### Power stage (on the PCB)

12 V in, protected, to a 5 V 3 A switching rail with a firmware-switched
camera output.

| Qty | Ref | Item | ~USD | Notes |
|---|---|---|---|---|
| 1 | U2 | **LM2596T-5.0**, TO-220-5 | 2 | 150 kHz, 3 A, fixed 5 V |
| 1 | — | TO-220 clip-on heatsink | 1 | **fit it** — see PCB.md §5 |
| 1 | L1 | 33 µH radial power inductor, >=3 A | 2 | 12 mm body, 5 mm pitch |
| 1 | D3 | 1N5822, 3 A 40 V Schottky, DO-201AD | 0.5 | catch diode |
| 1 | C3 | 470 µF 25 V low-ESR | 1 | buck input |
| 1 | C5 | 220 µF 25 V low-ESR | 1 | buck output |
| 1 | C1 | 470 µF 25 V | 1 | 12 V bulk |
| 1 | C11 | 100 µF 25 V low-ESR | 1 | **VMOT, at the driver socket** |
| 2 | Q1 Q2 | IRF4905 P-MOSFET, TO-220 | 2 | reverse-polarity + camera switch |
| 1 | Q3 | 2N3904 NPN, TO-92 | 0.2 | gate driver for Q2 |
| 1 | Q4 | 2N3906 PNP, TO-92 | 0.2 | GPS 5 V high-side switch |
| 1 | Q5 | 2N3904 NPN, TO-92 | 0.2 | drives Q4 from GPIO 2 |
| 1 | D1 | 1N4744A 15 V zener, DO-41 | 0.2 | Q1 gate clamp |
| 1 | D2 | P6KE20CA TVS, DO-15 | 0.5 | input transient clamp; bidirectional |
| 1 | F1 | 5x20 mm fuse holder + 3 A fuse | 2 | PCB clips |
| 3 | LED1-3 | 3 mm LED (grn, grn, **red**) | 0.5 | 5 V / camera / status. LED3 runs off a 3.3 V GPIO, so red + 330 R — blue would barely light |
| 1 | SW1 | 6 mm tactile switch | 0.3 | config + wake |

### Connectors and passives (on the PCB)

| Qty | Ref | Item | ~USD | Notes |
|---|---|---|---|---|
| 2 | J1 J3 | 2-pos screw terminal, 5 mm | 1 | 12 V in, camera 5 V out |
| 1 | J4 | 4-pos screw terminal, 5 mm | 1 | motor |
| 1 | J2 | USB-A receptacle, THT horizontal | 1.5 | camera; D+/D- shorted |
| 1 | — | 1x40 male header strip, 2.54 mm | 1 | cut for J5/J7-J11/JP3-JP5 |
| 5 | — | 2.54 mm jumper shunts | 0.5 | MS1, MS2, PDN_ALT |
| 9 | C2.. | 100 nF ceramic, 2.5 mm pitch | 1 | decoupling |
| 2 | C7 C9 | 100 µF 16 V / 10 V | 1 | camera rail, 3V3 |
| 19 | R1.. | 1/4 W resistors, see PCB.md | 1.5 | 2x 1% for the battery divider |
| 4 | H1-H4 | M3 standoff + screw | 2 | |

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

The transistor switch on the GPS 5 V feed is there for a reason: left
powered the NEO-6M draws 30-45 mA around the clock, roughly 5 Wh a day doing
nothing at all. Switched on only for the nightly fix, it costs under 0.1 Wh.

**Roughly $205 without solar, $255 with** (the PCB and its discrete
power stage replace the perfboard, the buck module and the load-switch module).

---

## 5. Pin map

> **Superseded — see [FIRMWARE.md §4](FIRMWARE.md#4-pin-map--as-built).**
> The table below predates the OLED devkit and is missing the DS3231 interrupt
> line, without which the RTC alarm cannot wake the ESP32 from deep sleep.
> FIRMWARE.md §4 is the authoritative map. Kept here for the wiring rules only.

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
- [ ] Confirm the HERO6 keeps its AP rejoinable after the ESP32 disconnects
      mid-record — if not, the dawn stop has to become a load-switch cut, which
      loses the last file
- [ ] Cable routing for 180° of sweep — slack loop vs slip ring
- [ ] Decide battery vs battery+solar

Firmware architecture, state machine, schedule format and build order:
**[FIRMWARE.md](FIRMWARE.md)**.
