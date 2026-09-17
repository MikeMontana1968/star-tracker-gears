# Printing guide

What to print, in what order, and with what settings. Every file named here is
generated into `out/` by `.\build.ps1` — see [README.md](README.md) for the
design and [DESIGN_NOTES.md](DESIGN_NOTES.md) for why it is the way it is.

All-printed build, module-2 final stage. **2048:1**, `USTEPS_PER_REV = 13,107,200`
at 1/32 microstepping.

**Every STL here is cut at `bore = 5.2` nominal.** On this printer that lands
near 4.9-5.0 mm actual, which suits a 20d common nail (4.88 mm) better than it
suits true 5.00 mm ground stock. See *Shafts* below.

## The gear train — 5 pieces from 4 files

| File | Qty | What |
|---|---|---|
| `pinion.stl` | 1 | 15T motor pinion, **D-shaft bore 5.45** for the NEMA17's 5.00 mm ground shaft |
| `stage.stl` | **2** | 120T + 15T compound, 8:1 — shafts 1 and 2 |
| `final_stage_gear.stl` | 1 | 120T module-1 wheel + **24T module-2** pinion, one piece — shaft 3 |
| `final_wheel_96T_m2_PRINTED.stl` | 1 | 96T module-2 output wheel, 196 mm OD, 4:1. **Ø22 bore, 6 bolts at r=18** — it locates on the hub register, not on its own bore |

Note `stage.stl` is **2**, not 3 — the third compound is `final_stage_gear.stl`,
which carries the coarse module-2 pinion.

## Baseplate — cut, not printed

121.5 x 174 mm, too big for either bed and wanting stiffness rather than
strength, so it is a cut part.

**6061 aluminium, 4.3 mm as ordered** (nearest stock to the 4.0 mm designed).
157 cm2, ~182 g.

The extra 0.3 mm changes nothing dimensional: **every gear height is referenced
from the plate's top face**, so thickness lives entirely below the datum. It
buys 24% more bending stiffness (t^3) for 13 g. Two fastener consequences:
the tower flange grip becomes 8.3 mm (M3x12 or 14 + nut), and the motor bolts
want **M3x10, not M3x8**, into the NEMA17's ~4.5 mm tapped holes.

**Watch for one thing on the DFM review:** the M3 holes are Ø3.4, which is
0.79 x material thickness. The usual laser rule is minimum hole diameter >= 1 x
thickness. This was already true at 4.0 mm (0.85x) and is slightly worse at
4.3. If the quote priced without comment it passed their check; if it comes
back flagged, this is why, and the fix is to open those four motor holes and
three flange holes to Ø4.5 and use washers. 3 mm works if the wedge
sits close to the output shaft; below 3 mm the bearing-tower flange will dimple
it.

**Send `out/docs/baseplate_cut.dxf`** — SendCutSend wants DXF. It carries the
outline as **4 ARC + 4 LINE** (the hull of circles decomposes exactly into arcs
joined by their common tangents) and every hole as a true **CIRCLE**: 16 of
them, 24 entities total.

Do **not** export DXF from OpenSCAD. Its writer emits the whole profile as
loose line segments — **708** of them for this part, with every hole a faceted
polygon and not one CIRCLE. Services reject or mis-cut that.

`baseplate_cut.svg` is the same geometry (17 closed contours) if a vendor
prefers SVG. R12 DXF has no reliable units record, so **state mm on upload**;
the file measures 121.50 x 174.00 mm, which is the giveaway if anything reads
it as inches.

**DFM note:** the tower flange bolt circle is Ø35, not Ø28. At Ø28 the web
between the plate's Ø22.4 spigot bore and each M3 was only **1.10 mm** — under
the min-feature rule for 4 mm aluminium. Ø35 gives 4.60 mm, and the tower
flange grew to Ø44 to suit, which still clears shaft 1's wheel by 2.5 mm.
`os_flange_bc` and `bp_tower_bc` must stay equal.

The outline is a hull of lobes at the five shaft centres, not a slab under the
whole gear footprint: **the wheels overhang the plate on purpose**, so the plate
only carries the shafts and the motor. That is what takes it from ~740 g to
170 g.

Holes, all in the SVG and on the 1:1 template:

| Where | What |
|---|---|
| S0 | dia 23 NEMA17 pilot boss + 4 x M3 on a 31 mm square |
| S1-S3 | dia 4.9, light press for 20d nails driven up from underneath |
| S4 | dia 22.4 tower spigot + 3 x M3 on dia 28 |
| S4 | **4 x M5 on dia 50 — the wedge/tripod interface** |

The wedge bolts are centred on the **output axis**, not the plate centroid. The
camera moment is reacted where it is generated, which is the shortest load path
and the reason the plate can be this thin.

`out/docs/baseplate_template.pdf` is the same thing at 1:1 on US Letter with
both scale bars — use it to check the SVG before ordering, or as a drilling
template on stock you cut yourself.

**One consequence:** shaft 0 has no usable spacer. Its hole is dia 23 for the
motor boss, so the 2 mm spacer would fall through. The motor pinion's height is
set by its grub screw on the NEMA17 shaft instead. Spacers 1-4 are unaffected.

## Output shaft and AS5600 encoder

The output is the one shaft that rotates: the encoder magnet has to sit on the
axis and turn with the wheel, and the camera load wants real bearings anyway.

| File | Qty | What |
|---|---|---|
| `output_tower_LOWER.stl` | 1 | tube + taper, lower 625ZZ pocket, spigot the sensor bracket registers on |
| `output_tower_UPPER.stl` | 1 | flange onto the baseplate, upper 625ZZ pocket |
| `output_tower_DOWELS.stl` | 1 | three Ø2.9 x 7.6 alignment dowels |
| `output_hub_lower.stl` | 1 | clamps the shaft, Ø22 register the wheel locates on |
| `output_hub_upper.stl` | 1 | camera platform, 1/4"-20 with a captive nut pocket |
| `magnet_cap.stl` | 1 | presses on the shaft end, 1.5 mm floor under the magnet |
| `as5600_bracket.stl` | 1 | slides up over the tower spigot, PCB screws to its underside |

Also needed: a **5 x 65 mm ground shaft** (not a nail — this one runs in
bearings and carries the encoder), 2 x 625ZZ, a **diametrically** magnetised
6 x 2.5 magnet, and an AS5600 breakout.

**Verified geometry:** tower Ø38 x 35 mm, hubs Ø44, cap Ø9 x 9, bracket
Ø30 x 27. Bearing centres 30 mm apart. **Magnet face to sensor die = 2.0 mm**,
inside the AS5600's 0.5-3.0 mm window.

### The tower is split, because one piece will not print

It fails in **both** orientations, for different reasons:

- **Bottom-up:** the first layer is a 2.75 mm wide ring — only **166 mm2** of
  bed contact under a 35 mm tall part. It lifts.
- **Top-down:** the flange's top face becomes an 8 mm wide 90 degree ledge.

Split at the taper/flange junction (z = 0, where the taper meets the max
radius). **Both halves then print flange-face-down on ~1080 mm2** — 6.5x the
bed contact — and neither has an overhang: the lower half's cone shrinks going
up, the upper half steps inward going up. No supports on either.

Three Ø3.2 sockets straddle the joint, 4 mm into each half, on a 16 mm circle
clear of the bore and the M3s. **Both mating faces want to be on the bed** —
that is the whole point of the split — so an integral male pin would have to
print as an overhang on whichever half carried it. Hence separate dowels.
`output_tower_DOWELS.stl` gives three; a 3 mm rod or filament offcut works
just as well. Glue with the dowels dry-fitted first, then bolt down.

The three M3 flange holes pass through **both** halves, so the mounting screws
also key the joint.

### Print orientation matters on three of these
- **Tower halves — flange face down**, both of them.
- **Magnet cap — magnet pocket down.** Small part: print three or four at once
  or the layers never cool.
- **Bracket — floor down.** Everything above it is open upward.

Both hubs print flat-face-down with no overhangs.

### Concentricity comes from the parts, not from you

The AS5600 needs the magnet centred over the die within ~0.25 mm. The bracket
therefore registers on the **same tower boss that holds the lower bearing**, so
concentricity is built in rather than dialled in. Do not substitute slotted
holes and eyeballing.

Firmware setup aid: read the AS5600's `AGC` and `MAGNITUDE` registers over I2C.
They tell you "too far / too close" as a number, so the air gap is measured
rather than guessed.

## Shaft spacers

`SPACERS_shaft_0-4.stl` — five spacers, left to right = shafts 0,1,2,3,4:

| Shaft | Height | Sets |
|---|---|---|
| 0 | 2.0 mm | motor pinion off the plate |
| 1 | 2.0 mm | 120T wheel off the plate |
| 2 | 8.5 mm | one stage up |
| 3 | 15.0 mm | two stages up |
| 4 | 21.5 mm | output wheel |

Heights are computed as `sp_clr + (i-1) x (fw_wheel + z_gap)`, so if you switch
to the slim 3 mm-face stack they recompute to 2, 2, 6, 10, 14 automatically.

**The spacer OD is the tightest fit in the whole gearbox.** A spacer on shaft 2
shares a Z band with shaft 1's 120T wheel, 67.5 mm away with a 61 mm tip radius:

```
shaft only (r 2.5)   67.5 - 61 - 2.5 = 4.0 mm clear
10 mm spacer (r 5)   67.5 - 61 - 5.0 = 1.5 mm clear
13 mm spacer         67.5 - 61 - 6.5 = 0.0 mm  <-- hard wall
```

Same story for shaft 3 against shaft 2's wheel. Shafts 0, 1 and 4 are
unconstrained — their spacers never share a Z band with a neighbouring wheel.
**Never fit a stock 5 mm shaft collar (12-16 mm OD) at shafts 2 or 3.**

Printed on end so the end faces are first/last layers and come out flat; the
bore is chamfered both ends so first-layer squish cannot cock them over.

Note the 2 mm `sp_clr` under the lowest gear — earlier layouts had the shaft-1
wheel sitting at z = 0, i.e. rubbing on the baseplate.

## Shafts

Five shafts, 15–32 mm of working length each (see the spacer heights above,
plus the gear stack). They are FIXED — the gears turn on them.

**20d bright common nails, 0.192 in = 4.88 mm.** Closest standard nail to 5 mm,
and a better match to the as-printed 5.2 bore than true 5.00 mm stock would be.

Shafts 1-4 only. **Shaft 0 is the motor's own 5.00 mm ground shaft**, so
`pinion.stl` is cut differently from the rest:

| File | Bore | Flat |
|---|---|---|
| `pinion_bore5.30.stl` | 5.30 | 2.150 |
| **`pinion.stl`** | **5.45** | **2.225** |
| `pinion_bore5.60.stl` | 5.60 | 2.300 |

All three are `mount_type = "dshaft"` — the hole is a circle clipped by a flat,
matching the NEMA17 D-cut, so the pinion cannot slip however the grub screw
behaves. The flat tracks the bore at `2.0 + (bore - 5.0)/2`, because a printed
flat needs the same radial compensation the bore gets.

5.45 is the estimate from the 0.50 mm offset the fit gauge measured. Print all
three next to `stage.stl` — you need that gear anyway, and batching also fixes
the minimum-layer-time problem that makes a lone pinion droop. Try each on the
motor shaft and keep the tightest one that slides on without force.

| Nail | Diameter | Verdict |
|---|---|---|
| 16d common | 0.162 in / 4.11 mm | ~0.8 mm of slop — this is why the first test mount felt loose |
| **20d common** | **0.192 in / 4.88 mm** | **use this** |
| 30d common | 0.207 in / 5.26 mm | too fat for a 5.2 nominal bore that printed under |

Specify **common** and **bright**. Box nails and sinkers run a gauge thinner for
the same penny size; galvanised nails have a lumpy zinc coating and are useless
as a bearing surface.

- **Roll-test every nail** on glass or a saw table and keep the five straightest.
  A bent shaft is eccentricity, and on shaft 3 that is the dominant tracking
  error — see the error budget in [README.md](README.md).
- Cut the point off and square the end. A 20d is 4 in long; you need ~30 mm.
- Drive them **up from underneath** so the head (~8.9 mm, well under the 13 mm
  limit) seats against the underside of the baseplate. Positive location, and
  the head sits below everything so it fouls nothing.

**Upgrade path:** shaft 3 carries the final pinion and sets short-exposure
trailing. If measurement says it is the limit, replace that one with ground
stock — a 5 mm drill blank, or the shank of a cheap 5 mm HSS drill bit, both
ground to h8. That will need the bore opened, either by reaming or by
re-rendering `final_stage_gear.stl` at a larger `bore`.

## Calibration — print these FIRST

Three gauges, all in `out/_scratch/`, all sub-15-minute prints. Each exists
because a dimension on this printer cannot be predicted, only measured.

| File | Why |
|---|---|
| `BOREGAUGE_hole_diameters.stl` | Stepped holes 5.0–5.6. Push a real 5 mm shaft through (a 5 mm drill shank is a precise gauge pin); the smallest hole that turns freely is what `bore` should be. **Needed for the motor pinion** — a NEMA 17 shaft is ground 5.00 mm and will not enter a bore that printed at 4.9. Push the actual motor shaft through the gauge and re-render `pinion.stl` at that bore. The other four gears run on 4.88 mm nails and are fine as cut. |
| `FITGAUGE_post_diameters.stl` | Stepped posts. Already run → **4.70** on this printer. Re-run with `-D fg_d0=` if you change filament. |
| `TESTPLATE_15T_30T.stl` | Backlash jig, posts at 22.5 mm centres, cut for the measured 4.70. |
| `TEST_wheel_30T.stl` | The 30T mate for the test plate. Pairs with `pinion.stl`. |
| `BEARINGGAUGE_pockets.stl` | **Print before the bearing tower.** Eight stepped pockets, 16.0 to 16.7. Printed holes come out undersize, so a nominal 16.0 pocket will not take a 16.0 bearing. Push a real 625ZZ into each; use the smallest that seats with firm thumb pressure — not one you have to hammer, and not one it drops into. Then set `os_brg_fit` (or `$BrgFit` in `build.ps1`) to that value minus 16.0. |

## Laser option

`final_wheel_96T_m2_laser.svg` — the same 96T wheel as a flat profile for
SendCutSend, cut at `backlash = 0.35`, 60 points per flank. Only needed if you
decide to have the final wheel cut from .063″ aluminium instead of printing it.
Ignore it otherwise.

## `out/docs/`

| File | What |
|---|---|
| `assembly.png` | Assembled 3/4 view — the whole gearbox on its baseplate |
| `assembly_exploded.png` | Same, every gear lifted 45 mm so the spacers and shafts are visible |
| `assembly_top.png` | Plan view, matches the baseplate template |
| `baseplate_template.pdf` | **1:1 drilling template**, US Letter portrait |
| `baseplate_assembly_guide.pdf` | bench reference: what sits on each shaft, spacer heights, fasteners. **Not to scale** |

The PDF must be printed at **100% / Actual size** — no page scaling, no
fit-to-page. It carries a 100 mm scale bar on each axis; measure both with a
rule before you drill. Regenerate it with `.\build.ps1 -Only baseplate_template.pdf`.

Render the assembly views with `.\build.ps1 -Group docs`.

## `out/_scratch/`

Calibration gauges, the backlash test jig, and superseded exports. Nothing in
there is part of the final build, and it is gitignored — but the gauges and the
test plate are re-runnable tools, not rubbish. Rebuild them with
`.\build.ps1 -Group calibration`.

## Print notes

Accuracy problem, not a strength problem — see the settings table in
[README.md](README.md). The three that matter most:

- **Walls = 4.** At module 1 a tooth is ~1.57 mm thick, so 4 walls makes it
  100 % perimeter. This decides your flank quality.
- **Z Seam = Random**, at least for pinions. A stacked seam puts a ridge on one
  tooth, which is the once-per-revolution drag spot found on the first test pair.
- **Don't print a pinion alone** — 17 mm layers give no cooling time. Batch them.

For the 196 mm final wheel: brim it, and consider `-D lighten=false`. Flatness
matters more than weight on that part, and the default lightening leaves ~4 mm
webs at this diameter.
