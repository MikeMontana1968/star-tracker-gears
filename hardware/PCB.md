# star_tracker_ctrl — controller PCB

A 127 × 84 mm (5.0 × 3.3 in) two-layer carrier board for the sidereal
tracker: 12 V in, protected and fused, a 5 V 3 A switching rail, a
firmware-switched camera output, and sockets for the ESP32, the TMC2209 and
the DS3231.

Architecture, state machine and pin map: **[../FIRMWARE.md](../FIRMWARE.md)**.
Project-level BOM: **[../HARDWARE.md §4](../HARDWARE.md)**.

---

## 0. Read this before you order

Three of these will cost you a board if you skip them.

- [ ] **Measure your ESP32 module's header row spacing.** The board is drawn
      for **25.4 mm (1.0 in)**, the usual 38-pin DevKitC/DevKit-V1 clone. Some
      "wide" 38-pin boards are 27.94 mm. One number in
      [`design.py`](design.py) (`ESP32_ROW_SPACING`) and a regenerate fixes it.
- [ ] **Confirm your module's pin order** against the silkscreen legend beside
      each row. The board assumes the standard 38-pin map printed in
      FIRMWARE.md §4. A Heltec-style board with the OLED on GPIO 4/15/16 will
      *not* work with this layout — see FIRMWARE.md §4 for what changes.
- [ ] **Confirm it is a WROOM-32, not a WROVER.** WROVER's PSRAM occupies
      GPIO 16/17, which this board uses for the TMC2209 UART.
- [ ] Check the DS3231 module's pin order is `32K SQW SCL SDA VCC GND`
      (ZS-042). Other orders exist; the socket is labelled.

Everything is through-hole, and every module is socketed. There are no SMD
parts and no DIP ICs — the only IC is the LM2596 in a TO-220.

---

## 1. What is on it

| Block | Parts | Notes |
|---|---|---|
| Input protection | F1, Q1, D1, D2, C1 | 3 A fuse, P-FET reverse polarity, TVS |
| 5 V 3 A buck | U2, L1, D3, C3, C5 | LM2596T-5.0, 150 kHz |
| Camera switch | Q2, Q3, R3–R5, J2, J3 | high-side P-FET, USB-A + screw terminal |
| ESP32 socket | J20, J21 | 2 × 1×19, 25.4 mm apart |
| TMC2209 socket | J30, J31 | 2 × 1×8, 15.24 mm apart (StepStick standard) |
| Motor | J4 | 4-pos screw terminal |
| RTC | J6 | DS3231 module socket |
| Sensors | J7, J8, J10 | AS5600 (remote), opto home, I²C expansion |
| v2 | J9, J11 | GPS (with PPS), spare IO |
| Jumpers | JP3, JP4, JP5, J5 | PDN alt, MS1, MS2, DIAG |

---

## 2. Power input

```
  J1 +12V ── F1 3A ──┬── Q1 IRF4905 ──┬── +12V rail
                     │   (D→in, S→out)│
                    GND        R1 10k ┴ GND,  D1 15V zener G–S clamp
                                      │
                               D2 P6KE20CA ── GND
                               C1 470µF ──── GND
```

**Q1's orientation is the part people get wrong.** For reverse-polarity
protection with a P-channel MOSFET, the **drain** goes to the supply and the
**source** to the load — not the other way round. The body diode runs
drain→source, so wired this way it is forward-biased in normal use (the
channel carries the current anyway, at ~20 mΩ) and **reverse-biased when the
supply is backwards**, which is the whole point. Wire it source-to-supply and
the body diode happily passes −12 V straight into the board.

D1 clamps V<sub>GS</sub> if a transient gets past the TVS. D2 is
**bidirectional** (P6KE20CA, not the unidirectional …A) so there is no
polarity to get wrong at assembly.

## 3. 5 V 3 A buck — LM2596T-5.0

Pin order confirmed against TI SNVS124G Table 6-1:
**1 = V<sub>IN</sub>, 2 = Output, 3 = GND, 4 = Feedback, 5 = ON/OFF.**

| | |
|---|---|
| Topology | fixed 5 V, 150 kHz, 3 A |
| Input cap | C3 470 µF 25 V **low-ESR** |
| Inductor | L1 33 µH, ≥3 A, 12 mm radial |
| Catch diode | D3 1N5822, 3 A 40 V Schottky, cathode on the SW node |
| Output cap | C5 220 µF 25 V **low-ESR** |
| ON/OFF | tied to GND = always on (open is also on) |
| Feedback | straight to the +5 V rail — this is the fixed-output part |

**Low-ESR is not optional** on C3 and C5. A general-purpose electrolytic will
work at room temperature and then make the loop unstable on a cold night.

### Fit the heatsink

At 3 A out the LM2596 dissipates about 1.7 W. A bare vertical TO-220 is
roughly 60 °C/W to still air, so that is a 100 °C rise — too hot. The real
load is much lower (the camera is ~0.5 A, ~1.5 A while it charges its own
battery, plus ~0.15 A for the ESP32), which is 0.4–0.6 W and fine bare. **Fit
a clip-on heatsink anyway** — it costs a dollar and the failure mode is a
thermal shutdown at 2 a.m.

If you would rather have 94 % and no inductor, a Traco **TSR 3-2450** (SIP-3,
3 A, 6.5–36 V in) is a drop-in *concept* replacement, but it needs a footprint
this project does not ship — its pin pitch is not verified here. The Recom
R-78B5.0-2.0 has a stock KiCad footprint but is only 2 A, which is under spec.

## 4. Camera 5 V switch

```
  +5V ──┬── R3 10k ──┬── Q2 gate       GPIO32 ── R4 10k ── Q3 base
        │            │                                  R5 100k ── GND
        └── Q2 source│
                     └── Q3 collector ── (Q3 emitter → GND)
  Q2 drain ── +5V_CAM ── C7 100µF ── J2 USB-A / J3 screw terminal
```

GPIO high → Q3 on → gate pulled to ground → V<sub>GS</sub> = −5 V → Q2 on.
R3 holds the gate at the source so the camera is **off by default**, including
while the ESP32 is unpowered or its GPIO is floating; R5 does the same for the
base. IRF4905 at V<sub>GS</sub> = −5 V is roughly 30 mΩ, so ~0.1 V and 0.15 W
at 1.5 A.

J2's D+ and D− are shorted together — the "dedicated charging port"
convention, which is what tells a GoPro it may draw full current.

## 5. Grounding and the two routing constraints

GND is a **pour on both layers**, so return paths are short and the sensitive
things (AS5600 I²C, the step pulse) sit over solid copper.

Two constraints the layout is built around, both worth knowing before you move
anything:

**Through-hole pin rows cannot be crossed.** At 2.54 mm pitch with ~1.7 mm
pads the gap between adjacent pads is 0.84 mm, and a 0.3 mm track with 0.3 mm
clearance needs 0.9 mm. Worse, a through-hole pad blocks *both* layers, so
there is no "go under it on the back" escape. The ESP32's two header rows are
therefore walls across the middle of the board.

**So the GPIO map is chosen to suit the geometry.** Everything whose
destination is the bottom connector strip lives on the ESP32's **lower** row;
everything serving the driver, the camera and the power input lives on the
**upper** row. That is why WAKE is on GPIO 4 rather than 33, and PPS on
GPIO 23 rather than 36 — see the revision note in FIRMWARE.md §4. Corridors at
x ≈ 35–42 mm and x ≈ 91–99 mm carry what is left, mostly the power rails, and
a horizontal channel below the lower row fans the bottom-strip signals out.

This is also why the board is 84 mm tall rather than the 76.2 mm that "3 × 5
inches" would give. At 76.2 the placement fitted comfortably but five nets
could not be routed — there was no room to fan out between the lower header
row and the bottom connector strip. 8 mm of extra height fixed it. If you need
exactly 3 × 5, the way to get there is to move parts off the bottom strip, not
to squeeze the channel.

## 6. Jumpers

| Ref | Default | What it does |
|---|---|---|
| JP4 | **GND** | MS1 — TMC2209 UART address bit 0 |
| JP5 | **GND** | MS2 — UART address bit 1 |
| JP3 | open | bridges PDN_UART to the alternate StepStick pin, if your module puts UART on position 5 instead of 4 |
| J5 | wire | DIAG — no vendor brings StallGuard out on the 16-pin header, so run a flying lead from the module's DIAG pad to this header |

Both address jumpers to GND gives UART address 0, which is what the firmware
assumes. **The TMC2209's V<sub>REF</sub> pot is unused** — run current is set
over UART with IRUN/IHOLD.

## 7. Building it

```powershell
cd hardware
python gen_board.py                       # schematic + board + BOM
& 'C:\Program Files\KiCad\10.0\bin\python.exe' fill_zones.py
```

The KiCad project is **generated, not hand-drawn**. Edit the source, not the
`.kicad_pcb`:

| File | Holds |
|---|---|
| `design.py` | the netlist, every value, the ESP32/TMC pin maps |
| `place.py` | the floorplan: fixed positions and packing regions |
| `layout.py` | courtyard extents, the region packer, the collision check |
| `route.py` | the two-layer maze router |
| `gen_board.py` | emits `.kicad_sch`, `.kicad_pcb`, `.kicad_pro`, `BOM.csv` |
| `kicadlib.py` | S-expression reader/writer and library loader |

Regenerating overwrites the project, so any edit you make in KiCad's GUI is
lost on the next run. For one-off tweaks that is fine — open the project and
edit. For anything you want to keep, change `design.py` or `place.py`.

Checks that run every time:

- `layout.check()` fails the build on any courtyard overlap or off-board part
- `kicad-cli sch erc` — currently clean
- `kicad-cli pcb drc` — see §8

## 8. Known state

Generated, fully routed, and checked with KiCad 10.0.6's own tools:

| Check | Result |
|---|---|
| ERC | **0 violations** |
| DRC — unconnected | **0** |
| DRC — clearance / shorts / hole | **0** |
| DRC — thermal relief | **0** |
| DRC — silkscreen overlap | 19 + 6 over-copper, cosmetic |
| Parts | 61 placed + 4 mounting holes |
| Nets | 42 |
| Routing | ~215 track runs, ~76 vias, GND poured both sides |

The silkscreen warnings are reference designators colliding with each other
and with the module pin legends on a dense board. Nothing about fabrication
or assembly depends on them.

**The router is a greedy maze router with randomised restarts**, not a
commercial autorouter. It reached a complete route on pass 5 of 30 with a
fixed seed, so `gen_board.py` reproduces the same board every time. If you
change the floorplan and a net comes out unrouted, the message names it —
raise `passes` in `route.py`, or open the board and route that one by hand.

## 9. Fabrication

2 layers, 1.6 mm, 1 oz copper, HASL — no controlled impedance, no fine pitch,
no blind vias. Minimum track 0.4 mm signal / 1.0 mm power, minimum clearance
0.3 mm, smallest drill 0.4 mm (vias). Any cheap fab will build this at their
default rules.

```powershell
$cli = 'C:\Program Files\KiCad\10.0\bin\kicad-cli.exe'
& $cli pcb export gerbers --output gerbers star_tracker_ctrl\star_tracker_ctrl.kicad_pcb
& $cli pcb export drill   --output gerbers star_tracker_ctrl\star_tracker_ctrl.kicad_pcb
```

## 10. Assembly order

Lowest parts first, and test each rail before fitting the next stage.

1. Resistors, diodes, small ceramics.
2. Q1, D2, F1, C1 — then **apply 12 V and check the rail at Q1's source**,
   and check that reversing the supply gives 0 V and nothing gets warm.
3. U2 (with its heatsink), L1, D3, C3, C5 — **check 5 V** before going on.
4. Q2, Q3, C7, the USB socket — pull GPIO 32's pad high by hand and confirm
   the camera rail switches.
5. Electrolytics, LEDs, connectors, sockets last.
6. **Fit the modules only after all three rails read correctly.**

The 5 V rail feeds the ESP32's on-board regulator through the module's `5V`
pin, and everything at 3.3 V comes back out of the module's `3V3` pin. Added
3.3 V load is only ~20 mA (AS5600, DS3231, TMC2209 V<sub>IO</sub>, pull-ups),
which the module's AMS1117 handles without complaint.
