# RESUME — where this stopped and what unblocks it

Last touched 2026-09-27.

## The one thing blocking everything

**The controller PCB cannot be regenerated until the ESP32's OLED pins are
known.** `hardware/gen_board.py` refuses to run and says so:

```
ESP32_LEFT has 19 entries but ESP32_PINS_PER_SIDE is 15. The pin map has
not been updated for this module -- fill it in from the board's silkscreen
before generating.
```

That block is deliberate. The board committed to git is drawn for a 38-pin
DevKitC at 25.4 mm row spacing; the module actually in hand is a **30-pin
ideaspark ESP32 + 0.96" OLED**. **Do not fabricate the committed board.**

### Next action: run the pin check

Flash **[`firmware/pincheck/pincheck.ino`](firmware/pincheck/pincheck.ino)**.
Arduino IDE, board "DOIT ESP32 DEVKIT V1", serial monitor at 115200. Nothing
else needs to be connected — no RTC, no encoder, no driver.

It scans the three candidate I²C buses and names whatever answers. Two
outcomes:

| Result | Consequence |
|---|---|
| `0x3C` on **GPIO 21/22** | OLED shares the sensor bus. `WAKE` stays on GPIO 4. Regenerate as planned. |
| `0x3C` on **GPIO 4/15** | Those pins are spoken for. `WAKE` must move to the upper row and eat one row-crossing. |

It also prints PSRAM, which confirms WROOM vs WROVER electrically — WROVER's
PSRAM sits on GPIO 16/17, which the board uses for the TMC2209 UART.

## Module facts, measured and confirmed

| | |
|---|---|
| Board | ideaspark ESP32 + 0.96" OLED, silkscreen `ESP32 OLED-0.96 V3.0` |
| Module | ESP-WROOM-32 (per the can; confirm with pincheck) |
| Pins | **15 per side, 30 total** |
| Pitch | **2.54 mm** — 14 gaps measured end-to-end as 35.56 mm |
| Row spacing | **27.94 mm** (1.1 in) — inside 27.27 + outside 28.90 → 28.09 measured |
| USB | at the **bottom**; EN button bottom-left, BOOT bottom-right |

### Pin layout — read off the silkscreen and confirmed by the user

Standard DOIT DevKit V1 layout. Note **VIN is on the left column and 3V3 on
the right**, opposite ends, unlike the 38-pin board.

| # | LEFT (J2) top→bottom | RIGHT (J1) top→bottom |
|---|---|---|
| 1 | EN | D23 |
| 2 | VP (GPIO36) | D22 |
| 3 | VN (GPIO39) | TX0 (GPIO1) |
| 4 | D34 | RX0 (GPIO3) |
| 5 | D35 | D21 |
| 6 | D32 | D19 |
| 7 | D33 | D18 |
| 8 | D25 | D5 |
| 9 | D26 | TX2 (GPIO17) |
| 10 | D27 | RX2 (GPIO16) |
| 11 | D14 | D4 |
| 12 | D12 | D2 |
| 13 | D13 | D15 |
| 14 | GND | GND |
| 15 | VIN | 3V3 |

## After the scan — the regenerate

Roughly ten minutes, all parameterised:

1. Fill `ESP32_LEFT` / `ESP32_RIGHT` in `hardware/design.py` with the 15-entry
   map. The guard checks the length and that no signal lands on two pins.
2. **Re-derive which signals sit on which row.** This is not cosmetic. On a
   through-hole board the two pin rows are walls — nothing can cross them on
   either layer — so every signal bound for the bottom connector strip must be
   on the **lower** row, and everything serving the driver, camera and power
   input on the **upper** row. See PCB.md §7.
3. Update the ESP32 body outline in `place.py` (`MODULE_OUTLINES`) — the 30-pin
   board is shorter: 35.56 mm of pins rather than 45.72.
4. `python gen_board.py`, then `fill_zones.py`, then DRC.

A proposed map that avoids GPIO 2 entirely (it needs a pull-up for `WAKE`, and
GPIO 2 must be low to enter download mode, which would break USB flashing):

- **Upper row:** TMC_DIAG=36, VBAT_SENSE=34, CAM_EN=32, ESP_TX=33, STEP=25,
  MOT_DIR=26, TMC_EN=27, TMC_UART=14, LED_ST=13, spares 39/35, VIN=+5V
- **Lower row:** PPS=23, SCL=22, SDA=21, HOME=19, GPS_TX_ESP_RX=18, ESP_TX_GPS_RX=5,
  GPS_EN=17, WAKE=4, 3V3=+3V3; leave 16/15 free, TX0/RX0 free

25 GPIOs available against 17 needed, so it fits either way the scan lands.

## Mechanical Rev B -- done 2026-09-27

Writing the assembly guide turned up eight conflicts in the Rev A CAD. All are
fixed in the `.scad` and the docs:

| # | Rev A problem | Rev B |
|---|---|---|
| 1 | Tower cone sat inside the plate; the flange could not seat | Halves clamp the plate: flange above, Ø41 disc below, dowels across |
| 2 | Output shaft ran through the 1/4"-20 nut | Shaft cut to 55 mm, z -33 to 22 |
| 3 | M3x10 motor screws bottom out | M3x8 flat head |
| 4 | Motor screw heads and one M5 sit under the S1 wheel (2 mm) | Countersink them by hand; the plate is already cut |
| 5 | Lower hub rubbed the tower rim and the bearing's outer race | Printed washer on the inner race, hub 1 mm shorter |
| 6 | AS5600 bracket had no axial stop | Bracket stops on the disc; gap set by the parts |
| 7 | Grub holes in gears that turn on fixed nails | Removed from `stage` and `final_stage_gear` |
| 8 | 1/4"-20 nut pocket 2.2 mm deep | Upper hub 10 mm thick, 5.9 mm pocket; wheel screws are M3x20 |

## Still open, unrelated to the blocker

- **Is the OLED on the rotating camera platform or the fixed enclosure?** The
  camera sweeps ~180° a night and the enclosure does not, so "display opposite
  the lens" only holds at one point in the sweep if the panel is fixed.
  `oled_on_while_tracking` currently defaults to on.
- **Solder the GPS PPS flying lead.** The GY-NEO6MV2's 4-pin header does not
  bring TIMEPULSE out; tap it at the on-board PPS LED.
- The three HERO6 unknowns in HARDWARE.md §6 are still unmeasured.

## Where things are

| | |
|---|---|
| `README.md` | project overview, written as a resume doc |
| `FIRMWARE.md` | firmware architecture, state machine, pin map, build order |
| `HARDWARE.md` | electronics architecture and BOM |
| `hardware/PCB.md` | board design notes, orientation rules, assembly order |
| `hardware/*.py` | the generator: design → place → layout → route → gen_board |

The KiCad project is **generated, not hand-drawn**. Edit `design.py` and
`place.py`; anything done in the GUI is lost on the next run.
