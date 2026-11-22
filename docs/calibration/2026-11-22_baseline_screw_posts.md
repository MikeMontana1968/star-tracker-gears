# Gear-train calibration — baseline, 2026-11-22

**Build:** shafts 1–3 on **#12 screw posts** (5.4 mm) through printed **6.0 mm gear bores**,
re-shimmed with spacers, wide washers and jam nuts. Plate holes 5.4 mm. TMC2209 at
VREF 0.50 V (~350 mA), 1/8 microstep for slews, `BACKLASH_UNITS` = 16000 (0.44° overshoot),
slew 3200 pulses/s with a 3200 /s² ramp. No camera load. Encoder: AS5600 through the
NVS map (184 points, −1.5..181.5°; 1 count = 0.088° = 317″).

**Compare against:** the 696ZZ + 6 mm shoulder-bolt build (parts due 2026-12-10). Run
`firmware/tools/Run-Calibration.ps1` (suite `all`) on the new build and put the numbers
beside these. The suite reproduces the moves below; today's runs were ad hoc, so where a
row says *partial*, only part of it was measured today.

## Results

All errors are encoder minus step counter, relative to a forward-loaded reference reading.
Return error = where the output lands back at the base, against where it started.

### Lash at park (repeat, 5° out and back, 4 cycles)

| Overshoot | Return errors | Out error | Reading |
|---|---|---|---|
| 0 (plain reverse arrival) | −400, 0, 0, +400″ | −0.06° | lash below one count (< 0.09°) |
| 0.44° (firmware default) | 0, 0, 0, 0″ | −0.083° | exact |
| 0.7° | 0, 0, 0, 0″ | −0.083° | exact |
| 1.0° | 0, 0, 0, 0″ | −0.083° | exact |

### Lash against angle (wiggle: 2° forward and back at the default overshoot)

| Position | 44° | 45.5° | 47° | 48.5° | 90° / 135° / 178° |
|---|---|---|---|---|---|
| Return error | 0″ | +360″ | +1108″ | +1080″ | *not measured* |

Return error > 0 means the 0.44° overshoot did not take all the lash up: lash at 47–48.5°
is ~0.75° (0.44 + 0.31). A plain 1° reversal at 47° **jammed** (counter −1.0°, encoder
−0.15°), and for ~12 min afterwards every move from that spot stopped within 3 s in both
directions; a long reverse slew later passed it. Eccentric gear centres (bore play) are
the suspect.

### Steps lost against slew speed (60° sweep out and home)

| Slew | Return error | Out error |
|---|---|---|
| 3200 /s, run 1 | +2000″ (+0.56°) | +0.20° |
| 3200 /s, run 2 | −400″ (−0.11°) | −0.46° |
| 2000 /s | 0″ | −0.04° |
| 1200 /s | 0″ | −0.44° |

Returns are clean at ≤ 2000 /s; at 3200 they scatter by ±0.5°. The out errors
scatter too (+0.20 / −0.46 / −0.04 / −0.44°) even at low speed — the encoder reading
at 60° after a forward approach is not as repeatable as at park; suspect shaft wobble
moving the magnet. **Firmware slew lowered to 2000 /s** (config.h) on this result.

### Whole travel (180° sweep at 3200 /s)

Out 179.910° for 180.000° (−0.09°). Return **+2946″ (+0.82°)** — the rewind came home
short; afterwards the encoder read 1.5° at a counter of 0. Out ≈ 10.9 min including the
prime, back 8.6 min.

### Other observations

- After a reboot with the coils released at 48.5°, the encoder read 45.95° (−2.5°), with
  the AS5600 magnitude changed 2102 → 2133. Either the train relaxed or the magnet sits
  differently with the coils off; worth repeating on the new build.
- Any one of these errors would be re-seeded away at the next boot (the counter starts
  from the encoder), but a rewind that loses more than 2° trips the slip guard: a
  REWIND fault ends the deployment.

## What to compare

1. Lash against angle — the bearing build should flatten the 44→48.5° rise.
2. Return error at 3200 /s over 60° and 180° — lost steps on rewinds.
3. Repeatability (σ across identical runs), not just the mean.
4. Whether a plain reversal ever jams.
