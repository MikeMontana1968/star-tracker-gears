# RESUME — where this stands and what is next

Last touched 2026-10-01.

> **PCB FROZEN — Rev C Gerbers were sent to the fab house (2026-10-01).**
> Do NOT modify `hardware/star_tracker_ctrl/`, `design.py`, `gen_board.py`,
> `place.py`, `route.py` or re-export Gerbers. Work around it in the build
> instead. Known workaround: L1 is a Murata `19R333C` (6.0 mm lead pitch) on
> the 5.0 mm `Fastron_11P` footprint; squeeze the leads 1 mm. The footprint
> name is cosmetic. Any real board change means a new revision (Rev D).
> Mouser order files: `hardware/mouser_passives.csv`, `mouser_semiconductors.csv`.
> Backorder substitution: Panasonic EEU-FR1E221 (C5) and EEU-FR1E101 (C11) were
> backordered at Mouser, and so was the proposed Nichicon `UHE1E221MPD`
> (`hardware/mouser_backorder_replacements.csv` is NOT ordered). Bought on
> **Amazon** instead (2026-10-01): Innfeeltech 220 uF / 25 V / 105 C aluminum
> radial electrolytic, 50-pack, general-purpose (ESR not stated). Use for C5
> and C11. **C11 is therefore 220 uF, not 100 uF** (a fine upsize, more VMOT
> bulk). If the driver resets or steps noisily, swap C11 for a low-ESR part
> first. Bend leads out to the 5 mm pads. `mouser_passives.csv` still lists the
> original Panasonic parts.

## Controller board Rev C — sent to fab

The blocker is gone. The pin check ran on the module in hand and the board
was redrawn for it:

```
chip ESP32-D0WD-V3  rev 300  2 core(s)  flash 4 MB
PSRAM: none
I2C scan:
  GPIO21 (SDA) / GPIO22 (SCL)  <- ESP32 default
      0x3C  SSD1306 OLED
  GPIO4  (SDA) / GPIO15 (SCL)  nothing
  GPIO5  (SDA) / GPIO4  (SCL)  nothing
```

- The OLED shares the sensor I²C bus on 21/22; `WAKE` stays on GPIO 4.
- No PSRAM (WROOM-class), so GPIO 16/17 are free.
- `hardware/design.py` carries the 30-pin map (FIRMWARE.md §4 is the table),
  `REV = "C"`. The GPS UART nets are named by direction
  (`ESP_TX_GPS_RX`, `GPS_TX_ESP_RX`), fixing Rev B's TX-to-TX wiring.
- Generated, routed, poured: **ERC 0, DRC 0 unconnected / 0 clearance / 0
  hole-to-hole**; only cosmetic silkscreen overlaps. PCB.md §10.
- Router changes this round: drill-to-drill keepout for vias, a two-pass GND
  stitch, and `GND_RESCUE` (named pads escaped before any signal).

### Next

1. Look over `hardware/board_top.png` and `hardware/schematic.pdf`; export
   Gerbers (PCB.md §11) and order.
2. Firmware slice 1 onward (FIRMWARE.md §0). The pin constants are §4.
3. Build per the assembly guide, `docs/assembly_guide/stjarnspar.pdf`.
## Module facts, measured and confirmed

| | |
|---|---|
| Board | ideaspark ESP32 + 0.96" OLED, silkscreen `ESP32 OLED-0.96 V3.0` |
| Module | ESP32-D0WD-V3 rev 3, 4 MB flash, no PSRAM (pin check 2026-09-27) |
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

## Still open

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
