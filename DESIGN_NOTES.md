# Star Tracker Gear Drive — Design Notes

Working notes for a printed gear train that rotates a camera at one revolution
per sidereal day, driven by a stepper on an ESP32.

`README.md` is the build sheet — what to print and what to set.
This file is the *reasoning*: why these numbers, and what was rejected.

Status: design settled at **2048:1** with a module-2 final stage. Accuracy
targets are deliberately loose; first light intended on a 50 mm lens.

The 4608:1 module-1 train described in §2 was the first design. It was replaced
after §3 showed the final *pinion* radius, not the wheel, sets short-exposure
trailing — see §3a. The history is kept because the reasoning still applies.

---

## 1. Tools surveyed

Before writing a custom generator, the existing options:

**Browser, no install**

| Tool | Notes |
|---|---|
| [STLGears](https://www.stlgears.com/generators/3dprint) | Fast one-offs. Spur/helical/bevel/rack, direct STL, no account. |
| [Gear Generator Pro](https://geargenerator.pro/) | Widest coverage — spur, helical, bevel, worm, rack, sprockets. Live 3D preview. |
| [EasyGear Maker](https://easygear.ddns.net/en/) | Best of the web tools if the mesh has to *work*: true involute, exposes profile shift and backlash, exports STEP. |
| [The Engineer Online](https://www.theengineer.online/) | DXF/SVG/STEP/STL including internal gears. Good when you want the 2D profile. |
| [geargenerator.com](https://geargenerator.com/) | Animates a train — useful as a ratio calculator, weak on export. |

**Parametric**

| Tool | Notes |
|---|---|
| [chrisspen/gears](https://github.com/chrisspen/gears) | OpenSCAD port of Dr. Kirsch's `getriebe.scad`. Spur/ring/bevel/worm/rack **plus prebuilt assemblies** with correct centre distances. The serious choice. |
| [playfultechnology/openscad-gears](https://github.com/playfultechnology/openscad-gears) | Lighter-weight alternative. |
| Fusion 360 Spur Gear add-in | Utilities → Add-Ins → Scripts. Gives real B-rep, so you can cut hubs and keyways into it. |
| FreeCAD FCGear workbench | Via Addon Manager. Full involute/bevel/worm/cycloid, parametric. |

**Why a custom file anyway:** this train needs *compound* gears (wheel + next
pinion on one hub) with mount features, printed 3× identically. Writing ~200
lines of self-contained OpenSCAD was less work than fighting a library's
assembly conventions, and it version-controls as text.

---

## 2. Ratio selection

Sidereal day = **86164.0905 s**.

Working backward from motor speed:

| Motor speed | Reduction needed |
|---|---|
| 1 RPM | 1436 : 1 |
| 3 RPM | 4308 : 1 |
| 10 RPM | 14361 : 1 |

Below ~1 RPM the motor's own 50-cycle/rev torque ripple starts to show.
Above ~5 RPM you buy extra gear stages you don't need. **~3 RPM is the sweet spot.**

Chose **4608 : 1 = 8 × 8 × 8 × 9** because it factors into one repeated part:

```
NEMA17 --15T--> [120T|15T] --> [120T|15T] --> [120T|15T] --> 135T --> camera
         8:1          8:1            8:1            9:1
```

Five printed parts, two distinct designs. At 200 steps/rev, 1/16 microstepping:

| | |
|---|---|
| microsteps / sidereal day | 14,745,600 |
| step interval | 5843.377 µs |
| step rate | 171.13 steps/s |
| motor speed | 3.209 RPM |
| output resolution | 0.088 arcsec/µstep |

Module 1.0, 20° PA. Centre distances **67.5 / 67.5 / 67.5 / 75.0 mm**.
ODs: 15T = 17 mm, 120T = 122 mm, 135T = 137 mm. All fit an Ender 3 bed.

**Don't chase "nice" numbers.** The software accumulator (§4) makes any ratio
exact, so gears get chosen for mechanical convenience, not numerology.

### Rejected: single-stage worm

60:1 or 90:1 in one stage is tempting, but it puts the motor at ~0.04 RPM —
deep into torque-ripple territory. Worms belong in the *final* stage, not as
the whole reduction. See §6.

---

## 3a. The correction that reshaped the design

The table in §3 tracks the **wheels**. It misses the pinions, and the pinions
are what matter.

For any mesh both gears contribute equally to output error per unit
eccentricity — the pinion's smaller radius exactly cancels its higher
reduction. But the periods differ, and trailing depends on how far the error
moves *during* an exposure:

```
excursion over exposure t  =  (e / r_pinion) x 2*pi*t / 86164
```

Only the final pinion's pitch radius appears. With the original 15T module-1
pinion (r = 7.5 mm) a 0.1 mm eccentricity gives ~60 arcsec over a 300 s sub —
nine times worse than the 135T wheel the §3 table fingered.

**Final design: 24T pinion / 96T wheel at module 2.0, 4:1.** r_pinion goes
7.5 -> 24 mm, and that 60 arcsec becomes ~19. Total ratio 8x8x8x4 = **2048:1**,
`USTEPS_PER_REV = 13,107,200` at 1/32 microstepping.

The same change makes the wheel laser-cuttable if you ever want it in metal:
module-1 tooth tips are 0.69 mm, module-2 tips are 1.43 mm.

A single defective tooth is a smaller problem than it first appears. Trailing
comes from error *changing* during an exposure, so a sub taken wholly inside one
tooth's engagement sees a constant offset that stacking removes. Only subs
straddling the bad tooth's entry or exit are damaged — about 6% of them.

## 3. Error budget — the useful part

Each shaft's eccentricity is divided by the reduction **downstream** of it:

| Shaft | rev/day | period | 0.1 mm eccentricity → output error |
|---|---|---|---|
| 0 (motor pinion) | 4608 | 18.7 s | ~0.001″ |
| 1 | 576 | 2.5 min | ~0.6″ |
| 2 | 72 | 20 min | ~4.6″ |
| 3 | 9 | 2.7 h | ~37″ |
| 4 (output, 135T) | 1 | 23.9 h | ~305″ |

**Only the last stage matters.** Stages 1–3 can be as sloppy as the printer
likes. This is why the output wheel is the biggest one — angular error scales
as 1/R, so print it as large as the bed allows.

The 305″ on the output sounds fatal but its *period* is a whole day, so within
a 300 s exposure you only see about **6.7 arcsec** of it:

```
excursion ≈ A × 2π × t_exposure / 86164
          = 305 × 2π × 300 / 86164 ≈ 6.7″
```

- 50 mm lens on APS-C ≈ 16 arcsec/px → invisible.
- 200 mm lens ≈ 4 arcsec/px → ~1.7 px of trailing, borderline.

**Start on a 50 mm.**

### What actually ruins subs

**Polar alignment.** Misalignment θ drifts at θ·ω where ω = 7.292e-5 rad/s:

```
1° = 3600″ → 3600 × 7.292e-5 = 0.263 ″/s = ~16 arcsec/min
```

That swamps every mechanical error above. Getting to 0.25° with a sight tube
down the RA axis buys more than any amount of gear precision. **Do this first.**

---

## 4. Why there is no speed feedback loop

A stepper is *synchronous* — it advances exactly one microstep per pulse. The
ESP32 crystal (±20 ppm) is the timebase, and over a 300 s exposure that is
±6 ms ≈ 0.000025° at the output. Four orders of magnitude better than the
mechanics. **There is nothing for a speed loop to correct.**

What feedback *is* worth having: **stall / skip detection.** A TMC2209 gives it
free over UART via StallGuard. An AS5600 on an intermediate shaft also works
(12-bit = 0.088°, ÷9 through the final stage = ~35″ at the output — coarse for
correction, fine for "is it still turning?").

### The one thing that must be exact

The step interval 5843.377… µs is **not** an integer. Rounding to 5843 µs loses
0.377 µs × 171 steps/s = 65 µs/s = **5.6 s/day ≈ 23 arcsec of drift.** Visible.

So never round — carry the remainder, Bresenham style:

```c
const uint64_t SIDEREAL_US    = 86164090500ULL;      // µs
const uint64_t USTEPS_PER_REV = 200*16*4608;         // 14,745,600

acc += SIDEREAL_US;
uint64_t dt = acc / USTEPS_PER_REV;   // alternates 5843 / 5844
acc -= dt * USTEPS_PER_REV;           // remainder carries forward
next_us += dt;
```

Long-term rate is exact for **any** gear ratio. Change `USTEPS_PER_REV` and
nothing else. Use `esp_timer_get_time()` (int64 µs, monotonic, never wraps),
not `micros()`.

Run the motor at **low current (300–400 mA)** — it needs almost no torque
through 4608:1, and low current means less vibration and heat.

---

## 5. The generator

`star_tracker_gears.scad`, self-contained, no libraries.

**Involute construction.** Flank angle at radius R:

```
θ(R) = ψ_pitch + inv(α) − inv(acos(r_base/R))
```

with `inv(a) = tan(a) − a`, and `ψ_pitch = 90/z` degrees minus the backlash
allowance. Below the base circle the flank extends radially (conservative —
a real trochoid root is more generous). Root fillets come from a morphological
closing, `offset(r=-f) offset(r=+f)`, which rounds concave corners only.

**Helix done right.** Twist is computed *per gear* from a shared helix angle β:

```
twist_deg = h · tan(β) / r_pitch · 180/π
```

Applying the same twist *degrees* to a 15T and a 120T — which most generators
do — produces gears that will not mesh, because `r` differs. Cutting in the
transverse plane also keeps centre distance exactly `m(z₁+z₂)/2`.

**Mount features** (§ README) — `mount_type` / `out_mount`:
`round`, `dshaft`, `hex`, `bearing`, `thread`. Plus an M3 grub screw and a
parametric bolt circle. Lightening holes compute their inner radius from
whatever the bolt circle and bearing boss need.

### Two constraints found the hard way

1. **A bearing pocket does not fit the compound `stage` gear.** A 625ZZ is
   16 mm across; the 15T pinion's root circle is 12.5 mm. The pocket eats the
   pinion. Bearings go in the **frame**, plain shaft through the gear — which
   is what a real gearbox does. Hence `mount_type` and `out_mount` are separate.

2. **A D-hole is the circle *minus* the flat side**, not the circle plus a slot.
   The first attempt cut an oversized slot outward, sliced teeth off, and
   produced 5 disconnected bodies. Caught by checking `Volumes:` in the render
   output — `2` means one solid, anything more means the part fell apart.

### Verification

All parts render manifold (`Simple: yes`, `Volumes: 2`) in every `mount_type`.
Console echo confirms `ratio = 4608 : 1` and
`centre distances = [67.5, 67.5, 67.5, 75]`.

```powershell
& "C:\Program Files\OpenSCAD\openscad.exe" -o out\stage.stl -D 'part="stage"' star_tracker_gears.scad
```

**Using the GUI:** F5 = fast preview, F6 = full render (needed before STL
export), F7 = export. `$t` is 0 under F5, so nothing moves — for motion use
**View → Animate**, then FPS 10 / Steps 100. Scrubbing the `Time` field by hand
is usually more informative than watching it spin.

The 135T wheel takes ~2 min to F6. That is `offset()` doing fillets on 135
teeth, not a hang. Set `fillet = 0` and `flank_pts = 8` while experimenting.

---

## 6. Upgrade path — same firmware

Replace stages 3 and 4 with a worm:

```
NEMA17 --15T--> [120T|15T] --> [120T|15T] --> 1-start worm --> 72T wheel
         8:1          8:1                            72:1
```

8 × 8 × 72 = 4608 : 1, so `USTEPS_PER_REV` changes to 200 × 32 × 4608.
A worm final stage has no backlash reversal, is nearly self-locking, and a
*bought brass* worm wheel removes the single largest error source in the design
(§3). This is what a real EQ mount does.

---

## 7. The lazier alternative

A **barn-door tracker** — hinged plate pushed by a threaded rod on a long lever
— gets ~1000:1 mechanical advantage for free, makes printed-part precision
almost irrelevant, and is a weekend build. It only tracks for about an hour
before tangent error accumulates, and needs an isosceles or curved-rod
correction beyond that.

If "get a feel" is genuinely the goal, that is the better *first* project.
The gearbox is the better second one.

---

## 8. Order of work

1. Print `test_pair`. Tune `backlash` until it turns freely with barely
   perceptible slop. Everything downstream depends on this one number.
2. Build the polar alignment **before** the gearbox.
3. Open-loop drive, 50 mm lens, one 5-minute sub. **Measure the trailing.**
4. Only then decide what to fix. Adding an encoder before you have data is
   solving an imaginary problem.

## Print settings (Ender 3 / V3 SE)

- Module 1.0 at 0.4 mm nozzle → ~1.57 mm tooth thickness at the pitch line,
  about 4 extrusion widths. Fine at these loads. **Do not go below module 1.0.**
- `backlash = 0.25` mm total per mesh is the PLA starting point. Binds → 0.35.
  Rattles → 0.15.
- 0.12–0.16 mm layers, **4 perimeters**, ≥40% infill. Perimeters carry the
  teeth, so more perimeters beats more infill.
- Flat on the bed, no supports. Lightening holes are already in the wheels.
- **PLA, not PETG** — PETG creeps under sustained load.
- Shafts: 5 mm. M5 bolts work. 625ZZ (5×16×5) on the last two shafts, which
  are the ones that matter.


---

## 9. Assembly constraints found during layout

Three of these only surfaced when the layout was drawn to scale. All are now
enforced in `star_tracker_gears.scad` or documented on the drilling template.

**Every mesh needs its own Z band — the stack cannot be flattened.** For any
mesh, `pinion_tip + wheel_tip = a + 2m`: the tip circles always overlap by two
modules. So a pinion collides with *any* wheel on an adjacent shaft, mating or
not. Four meshes, four bands, irreducible. What can shrink is band height —
3 mm faces and a 1 mm gap take the stack from 24.5 mm to 16 mm.

**The assembly is a pancake, not a staircase.** Drawn folded and at true scale
it is 226 mm across and 24.5 mm tall. The three 122 mm wheels overlap almost
completely in plan; only the Z staircase separates them.

**Spacer OD is the tightest fit in the box.** A spacer on shaft 2 shares a Z
band with shaft 1's wheel: `67.5 - 61 - 5.0 = 1.5 mm` at Ø10, and zero at Ø13.
Never fit a stock 5 mm shaft collar (12-16 mm OD) at shafts 2 or 3.

**The output wheel sits over shafts 0 and 1.** They must stay below 19 mm, and
cannot reach a top plate. Harmless — their errors divide by 2048 and 256.

**Consecutive wheels were originally coplanar** with zero axial gap, and the
lowest gear sat flat on the baseplate. Both fixed with `z_gap` and `sp_clr`.
`fw_pinion` is 2 mm wider than `fw_wheel` precisely so the axial gap can exist
while the pinion still covers the full wheel face.

## 10. Printer calibration

Measured, not assumed: **a printed post must run ~0.50 mm under a printed
hole's nominal diameter** for a snug rotating fit on this machine — hole shrink
plus post growth combined. High side of typical (0.30-0.40), which hints flow
runs a few percent hot.

Consequences: gears carry a 5.2 bore and run on **20d common nails (4.88 mm)**,
which fit the as-printed hole better than true 5.00 mm ground stock would. The
motor pinion is the exception — it goes on the NEMA17's ground 5.00 mm shaft
and needs ~5.45, with the D-flat scaled to match at `2.0 + (bore - 5.0)/2`.

These constants live at the top of `build.ps1`.
