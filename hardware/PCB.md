# star_tracker_ctrl — controller PCB, Rev C

A 150 × 100 mm (5.9 × 3.9 in) two-layer carrier board for the sidereal
tracker: 12 V in, protected and fused, a 5 V 3 A switching rail, a
firmware-switched camera output, and sockets for the ESP32, the TMC2209 and
the DS3231.

Architecture, state machine and pin map: **[../FIRMWARE.md](../FIRMWARE.md)**.
Project-level BOM: **[../HARDWARE.md §4](../HARDWARE.md)**.

---

## 0. Read this before you order

Rev C is drawn for the module in hand, the **30-pin ideaspark ESP32 + 0.96"
OLED** (silkscreen `ESP32 OLED-0.96 V3.0`, DOIT DevKit V1 layout). These were
measured or checked on that module:

- [x] **Row spacing 27.94 mm (1.1 in), pitch 2.54 mm, 15 pins per side** —
      measured. [`design.py`](design.py) `ESP32_ROW_SPACING` / `ESP32_PINS_PER_SIDE`.
- [x] **Pin order** read off the module's silkscreen; the map is FIRMWARE.md §4.
- [x] **Pin check, 2026-09-27:** the OLED answers at `0x3C` on GPIO 21/22 (it
      shares the sensor bus, GPIO 4 stays free for WAKE), and there is **no
      PSRAM** — WROOM-class, GPIO 16/17 free.
- [ ] Using a different ESP32 board? Redo all three: measure the rows, read
      the pin order, run `firmware/pincheck`. A Heltec-style board with the
      OLED on GPIO 4/15 needs a different map.
- [ ] Check the DS3231 module's pin order is `32K SQW SCL SDA VCC GND`
      (ZS-042). Other orders exist; the socket is labelled.
- [ ] Look at your GPS module for a **PPS pad**. The common GY-NEO6MV2 brings
      out only `VCC RX TX GND` and drives an on-board PPS LED instead, so
      you will be soldering a flying lead. See §6.

Everything is through-hole, and every module is socketed. There are no SMD
parts and no DIP ICs — the only IC is the LM2596 in a TO-220.

---

## 1. What is on it

| Block | Parts | Notes |
|---|---|---|
| Input protection | F1, Q1, D1, D2, C1 | 3 A fuse, P-FET reverse polarity, TVS |
| 5 V 3 A buck | U2, L1, D3, C3, C5 | LM2596T-5.0, 150 kHz |
| Camera switch | Q2, Q3, R3–R5, J2, J3 | high-side P-FET, USB-A + screw terminal |
| ESP32 socket | J20, J21 | 2 × 1×15, 27.94 mm apart (30-pin module) |
| TMC2209 socket | J30, J31 | 2 × 1×8, 15.24 mm apart (StepStick standard) |
| Motor | J4 | 4-pos screw terminal |
| RTC | J6 | DS3231 module socket |
| Sensors | J7, J8, J10 | AS5600 (remote), opto home, I²C expansion |
| GPS | J9, Q4, Q5 | GY-NEO6MV2 on a switched 5 V feed, plus PPS |
| v2 | J11 | spare IO: GPIO 39, 35 (input-only) |
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

## 5. GPS

```
       GPIO17 ──R18 10k──┬── Q5 base          +5V ──┬── R16 10k ──┬── Q4 base
                     R19 100k                       │             │
                         │                          └── Q4 emitter│
                        GND     Q5 collector ──R17 1k─────────────┘
                                Q5 emitter ── GND
                                Q4 collector ── GPS_VCC ── C17 ── J9.2
```

GPIO high turns Q5 on, which pulls Q4's base down through R17 and switches
5 V onto the module. R16 holds Q4 off by default and R19 does the same for
Q5, so **the GPS is off while GPIO 17 floats at boot**. Q4 is a 2N3906: at
45 mA it drops about 0.2 V, and the module's own LDO makes 3.3 V from what
is left. Its TX output is 3.3 V logic, safe straight into the ESP32.

**Why switch it at all:** the 3.3 V and 5 V rails both stay up through deep
sleep, so an unswitched GPS would pull 30–45 mA around the clock — roughly
5 Wh a day, for a part that has a job for about ninety seconds each evening.

### J9 pinout

| Pin | Signal | Note |
|---|---|---|
| 1 | **PPS** | flying lead — see below |
| 2 | VCC | switched 5 V |
| 3 | RXD | net `ESP_TX_GPS_RX`: module RX ← ESP32 TX (GPIO 5) |
| 4 | TXD | net `GPS_TX_ESP_RX`: module TX → ESP32 RX (GPIO 18) |
| 5 | GND | |

Pins 2–5 are deliberately in the module's own `VCC RX TX GND` order, so its
4-wire cable plugs straight in. **Pin 1 is separate because the GY-NEO6MV2
does not break PPS out** — the NEO-6M's TIMEPULSE drives the on-board PPS LED,
so tap it there. Check your board first; some revisions do have a PPS pad.

PPS is worth wiring, but keep its value in proportion: it sets the RTC to
sub-second accuracy and *measures* your crystal's real ppm error. It does not
meaningfully improve tracking — 20 ppm over a 12 h night is 3.6 arcsec at the
output, and polar alignment beats that by a thousand.

## 6. Module orientation — read this before fitting anything

**Every module on this board sits in a socket.** The ESP32 (J20/J21), the
TMC2209 (J30/J31) and the DS3231 (J6) are all female headers — nothing is
soldered down, and any of them can be pulled and replaced.

That makes orientation the thing that bites, because a socket will happily
accept a module fitted the wrong way round.

### The rule

The ESP32 module viewed from the top with its USB at the bottom has the left
column (`EN … VIN`) down the left edge and the right column (`IO23 … 3V3`)
down the right. Laid on its side there are exactly **two** legal placements:

| Rotation | Left column | Pin 1 | USB |
|---|---|---|---|
| 90° CW | **TOP row** | RIGHT | LEFT |
| 90° CCW | BOTTOM row | LEFT | RIGHT |

**Anything else is a reflection and cannot be built.** Revision A of this board
had the left column on top with pin 1 at the *left*, which is neither — the
transform from module coordinates to board coordinates had determinant −1. The
module would have had to be flipped over to fit, and every single pin was on
the wrong signal.

This board uses the first form: both sockets at rotation 270, pin 1 (`EN`
on the upper row, `IO23` on the lower) at the right-hand end, **USB pointing
left**.

### What is printed on the board

- a dashed **body outline** for the ESP32 and the TMC2209, so you can see the
  module's extent and which way it lies before fitting it
- a large **`USB <<<`** at the left-hand end of the ESP32 outline
- a bold **`1`** beside pin 1 of all four sockets
- the **signal name beside every pin** of both modules
- the orientation stated in words inside the outline, where it is covered once
  the module is in and visible exactly when you need it

If your module's pin 1 does not land next to the `1`, stop — do not force it.

## 7. Grounding and the two routing constraints

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

This is also why the board grew from the 127 × 76.2 mm that "3 × 5 inches"
would give, first to 127 × 84 and then to **150 × 100**. Each time the limit
was the same: nets with nowhere to fan out between the lower header row and
the bottom connector strip, and corridors too narrow at the module's ends.
The current size gives ~12 mm corridors and a 32 mm deep fan-out channel,
which is what finally made routing comfortable rather than marginal.

## 8. Jumpers

| Ref | Default | What it does |
|---|---|---|
| JP4 | **GND** | MS1 — TMC2209 UART address bit 0 |
| JP5 | **GND** | MS2 — UART address bit 1 |
| JP3 | open | bridges PDN_UART to the alternate StepStick pin, if your module puts UART on position 5 instead of 4 |
| J5 | wire | DIAG — no vendor brings StallGuard out on the 16-pin header, so run a flying lead from the module's DIAG pad to this header |

Both address jumpers to GND gives UART address 0, which is what the firmware
assumes. **The TMC2209's V<sub>REF</sub> pot is unused** — run current is set
over UART with IRUN/IHOLD.

## 9. Building it

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

## 10. Known state

**Rev C**, generated 2026-09-27 for the 30-pin ideaspark module, fully routed,
and checked with KiCad 10's own tools:

| Check | Result |
|---|---|
| ERC | **0 violations** |
| DRC — unconnected | **0** |
| DRC — clearance / shorts / hole-to-hole | **0** |
| DRC — silkscreen overlap | 68 + 6 over-copper, cosmetic |
| Parts | 71 placed + 4 mounting holes |
| Nets | 45 |
| Routing | 234 track runs, 82 vias, GND poured both sides; complete on pass 12 of 30 |

The silkscreen warnings are reference designators colliding with each other
and with the module pin legends on a dense board. Nothing about fabrication
or assembly depends on them.

**The router is a greedy maze router with randomised restarts**, not a
commercial autorouter. With a fixed seed, `gen_board.py` reproduces the same
board every time. If you change the floorplan and a net comes out unrouted,
the message names it — raise `passes` in `route.py`, or open the board and
route that one by hand.

**Always run the DRC after regenerating**, and read the unconnected items: a
GND pad the pour cannot reach passes the router and ERC and only shows there.

### Getting the ground pour to reach every pad

Worth knowing if you move things. A pad can be fenced in by signal tracks on
both layers — and through-hole pads block both — leaving the pour unable to
reach it. What fixed that here, in order of how much it mattered:

1. **Tight thermals.** `thermal_gap` 0.3 mm and `thermal_bridge_width` 0.4 mm,
   with `min_resolved_spokes` set to 1. Going from the 0.5 mm defaults took
   isolated pads from 8 to 1. Thermal relief is kept rather than using a solid
   pour connection, because soldering a through-hole ground pin into solid
   1 oz copper is genuinely unpleasant.
2. **`GND_RESCUE` in `design.py`**: pads the router connects to their nearest
   GND pad *before* any signal is routed. Rev C names the four GND end pins of
   the bottom connector strip (J6.6, J7.5, J8.1, J8.4) — the lower-row signals
   fan out between them and fenced a different two in on every attempt until
   all four were reserved. The list comes from the DRC, which knows the pour;
   the router does not (reserving every pad the router *itself* could not
   stitch, eleven of them, left six signals unroutable).
3. **An explicit GND stitch**, routed last at signal width, twice, so a
   straggler can connect to track laid after its own first attempt.
4. **Ground on end pins.** Interior pins on a 2.54 mm connector are
   unreachable by the pour — the gap between adjacent pads is 0.84 mm and the
   pour needs about 1.25 mm — so ground belongs on an end pin.

### Drill spacing

The router keeps every via centre clear of every pad and via hole by the
fab's 0.25 mm hole-to-hole minimum, whatever the nets. Its copper checks alone
let a GND via land 0.19 mm from a GND pad's hole, which is legal copper and an
illegal drill.
## 11. Fabrication

2 layers, 1.6 mm, 1 oz copper, HASL — no controlled impedance, no fine pitch,
no blind vias. Minimum track 0.4 mm signal / 1.0 mm power, minimum clearance
0.3 mm, smallest drill 0.4 mm (vias). Any cheap fab will build this at their
default rules.

```powershell
$cli = 'C:\Program Files\KiCad\10.0\bin\kicad-cli.exe'
& $cli pcb export gerbers --output gerbers star_tracker_ctrl\star_tracker_ctrl.kicad_pcb
& $cli pcb export drill   --output gerbers star_tracker_ctrl\star_tracker_ctrl.kicad_pcb
```

## 12. Assembly order

Lowest parts first, and test each rail before fitting the next stage.

1. Resistors, diodes, small ceramics.
2. Q1, D2, F1, C1 — then **apply 12 V and check the rail at Q1's source**,
   and check that reversing the supply gives 0 V and nothing gets warm.
3. U2 (with its heatsink), L1, D3, C3, C5 — **check 5 V** before going on.
4. Q2, Q3, C7, the USB socket — pull GPIO 32's pad high by hand and confirm
   the camera rail switches.
5. Q4, Q5 and their four resistors — drive the socket's GPS_EN pin (GPIO 17) high by hand and
   confirm ~4.8 V appears on J9 pin 2, and 0 V when it is low.
6. Electrolytics, LEDs, connectors, sockets last.
7. **Fit the modules only after all rails read correctly.**

The 5 V rail feeds the ESP32's on-board regulator through the module's `5V`
pin, and everything at 3.3 V comes back out of the module's `3V3` pin. Added
3.3 V load is only ~20 mA (AS5600, DS3231, TMC2209 V<sub>IO</sub>, pull-ups),
which the module's AMS1117 handles without complaint.
