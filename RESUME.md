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
## Rev D layout notes (from building Rev C)

Collected while assembling the first Rev C board; apply together at the next
regenerate.

1. **F1 fuse clips are turned 90 degrees.** The footprint is
   `Fuseholder_Clip-5x20mm_Littelfuse_111_Inline`: its two legs per clip run
   along the fuse axis, but the clips in hand have their legs across it. Swap to
   a `..._Lateral_...` footprint (legs 5.00 mm apart across the fuse); pick it by
   the clip-to-clip spacing of the actual clips (Littelfuse 111 Lateral 18.80,
   Bel FC-203-22 17.80, Littelfuse 521 17.00, Schurter OG 15.00). Rev C build:
   hand-hacked.
2. **C11, C12, R7 and R9 crowd J4.** Too close to the motor screw terminal to get
   the motor leads in around them. Move the TMCX region in `place.py` (x 112-148,
   y 56-80) away from J4 at (118, 48) -- keep C11 at the driver's VM pin, which is
   its whole job, and give J4 clear access from the board edge.
3. **Label J2 on the silkscreen as camera power, not USB data.** It reads like a
   second USB port for the ESP32; it is the switched camera 5 V (`+5V_CAM`), with
   D+/D- tied together (charging-port signal) and no data. Something like
   `CAMERA 5V OUT - power only` beside J2 (and `CAM 5V` already marks J3), plus
   `PROGRAM VIA THE ESP32'S OWN USB` near the module's USB end. Add to `SILK` in
   `place.py`.
4. **Label SW1 on the silkscreen** -- `CONFIG / WAKE`. It reads as a reset
   button; reset is the ESP32 module's own EN button. SW1 wakes the tracker from
   deep sleep, raises the setup Wi-Fi, and steps the OLED pages.
5. **Room for clip-on heatsinks on Q1, Q2 and U2** (all TO-220). Keep the space
   around each tab clear of other parts, and keep the heatsinks from touching
   each other: the tabs are live -- Q1's at +12 V (drain), Q2's at +5V_CAM
   (drain), U2's at GND. By calculation Q1 and Q2 dissipate under 0.2 W and run
   cool bare, but Q2 sees only -5 V of gate drive (see the Test 4 note), so the
   space is cheap insurance.
   **Design target (no heatsinks bought yet):** a generic TO-220 clip-on or
   push-on heatsink, fins on the tab side. Reserve a keep-out per device of
   **22 mm wide x 16 mm deep behind the tab x 25 mm tall**, plus **2 mm air**
   between neighbouring keep-outs and to any tall part (electrolytics, L1). Draw
   it on F.Fab/F.Courtyard so `layout.check()` enforces it, and print the outline
   on the silkscreen. Then buy any clip-on TO-220 heatsink that fits inside it
   (roughly 20 x 13 mm footprint is the common size).
6. **A jumper table on the silkscreen**, beside JP3-JP5 and J5:

   | Jumper | Fit | What it does |
   |---|---|---|
   | JP4 MS1 | to GND | TMC2209 UART address bit 0 |
   | JP5 MS2 | to GND | address bit 1 -- both to GND = address 0, which the firmware expects |
   | JP3 PDN ALT | open | bridge only if your TMC2209 module puts UART on pin 5 |
   | J5 DIAG | wire | flying lead from the module's DIAG pad (StallGuard) |

   Silkscreen is small text: about 1.0 mm, four lines, in the clear area right
   of the jumpers. Also note there that the V<sub>REF</sub> pot is unused (UART sets
   current).
7. **Name the bottom-edge connectors**, above each one (pin names are already
   printed beside the pins):

   | Ref | Label |
   |---|---|
   | J7 | `AS5600 ENCODER` |
   | J6 | `DS3231 RTC - module plugs in` |
   | J8 | `HOME SENSOR (optional)` |
   | J10 | `I2C EXPANSION` |
   | J9 | `GPS - cable on 2-5, PPS lead on 1` |

   Place them in the fan-out channel above the strip; silk over copper there is
   cosmetic.
8. **Print each small transistor's part number beside its reference**:
   `Q3 2N3904`, `Q4 2N3906`, `Q5 2N3904`. The 2N3904 (NPN) and 2N3906 (PNP) look
   identical in TO-92 and swapping them fails quietly; the board should say
   which goes where. (Q1/Q2 IRF4905 too, while at it.)
9. **Separate the `Q4` reference from the `GPS PWR` group label** -- they print
   on top of each other. Move `GPS PWR` (a `SILK` entry in `place.py`, at
   90, 68) clear of the Q4/Q5/R17-R19 group, and check the DRC's silk-overlap
   list for any others touching a reference designator.
10. **Label J3 like J2**: `CAMERA 5V OUT - power only`, with `+5V` / `GND` at
    its pins (pin 1, the square pad, is +5V_CAM). J3 is the screw-terminal twin
    of the USB-A J2, both switched by Q2; the label keeps anyone from treating it
    as a supply input.
11. **Turn J1 so its wire openings face the board edge.** All three screw
    terminals (J1, J3, J4) use the MaiXu footprint at 0 deg, whose wire entry is
    the footprint's +y side (the line printed just past the holes). That suits
    J3 on the bottom edge, but J1 sits at the TOP edge (y 8), so its openings
    face into the board and the 12 V lead has to come in over the parts. Rotate
    J1 180 deg in `place.py` (pin 1, +12V_IN, then sits on the right: update
    the `+12V` / `GND` silk labels and the guide's J1 drawings to match). J4's
    openings face C11/C12 (note 2); check them against wherever TMCX moves.
12. **Put the revision and a contact on the silkscreen.** Today `design.REV`
    reaches only KiCad's title block, so nothing on the board itself says which
    revision it is. Add one line of board text built FROM `design.REV`, so it
    can never go stale, plus the owner's address:
    `STAR TRACKER CTRL  REV D  -  MikeMontana@Gmail.com` (or two lines if
    space is tight), size about 1.2 mm, in a clear area away from pads. Check
    the DRC silk-overlap list after regenerating.

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
