#!/usr/bin/env python3
"""
Generate a 1:1 24-hour sidereal dial face for the final gear (96 T module 2).

The output shaft turns exactly once per sidereal day -- 2048:1 from a 200-step
motor at 1/32 is 13,107,200 usteps, and that is 86164.0905 s. So one turn of
this wheel is one full revolution of the sky, and the dial reads hour angle.
A division marked 01:00 is one SIDEREAL hour = 59 m 50.17 s of civil time; it
is NOT a wall clock and will gain 3 m 56 s a day against one.

96 teeth over 24 h means one tooth is 3.75 deg = 897.54 s = 14 m 57.5 s, which
is exactly a quarter hour. So the 15-minute graduations land one per tooth --
handy when you clock the dial to the wheel.

Hole geometry is NOT the .scad defaults. build.ps1 builds this wheel with
    -D bore=22 -D bc_holes=6 -D bc_r=18
so the centre is a 22 mm register bore (no hub boss survives it) and there are
six M3 clamp bolts on r 18. The six lightening holes follow from those via
lightening_2d(): web_r() = max(hub_d/2, bc_r + bc_d), ri = web_r + 5,
ro = root - 7, and the holes are (ro-ri)*0.78 across on radius (ri+ro)/2.
Those reach r 79.87, so the only continuous ring of material on the face is
r 79.87 .. 93.5 -- every tick and numeral lives in that 13.6 mm band.

Two pages, because the direction depends on how the stepper is wired:
    page 1  HH increases CLOCKWISE seen from above
    page 2  HH increases COUNTER-CLOCKWISE
Print the matching one with page scaling OFF, then measure the 100 mm bar
before you trust a single graduation.

    python make_dial_pdf.py
    python make_dial_pdf.py --bolts 6 --bolt-r 18 --bore 22
"""
import argparse
import math
import os

from reportlab.pdfgen import canvas
from reportlab.lib.pagesizes import letter
from reportlab.lib.units import mm

OUT = "out/docs/final_wheel_24h_dial.pdf"

# ---- model, kept in step with star_tracker_gears.scad --------------------
ZWF, MOD_F, CLEAR_ = 96, 2.0, 0.25
HUB_D = 12.0
HUB_CLAMP_D = 44.0          # os_hub_od -- the clamp hubs that sandwich the wheel
SIDEREAL_S = 86164.0905     # one revolution of the output shaft

R_PITCH = MOD_F * ZWF / 2.0                      # 96.0
R_TIP = R_PITCH + MOD_F                          # 98.0
R_ROOT = R_PITCH - MOD_F * (1.0 + CLEAR_)        # 93.5
R_DIAL = R_ROOT - 0.5                            # keep the paper out of the mesh

PAGE_W, PAGE_H = letter
CX = PAGE_W / 2.0
CY = PAGE_H - (10 * mm) - (R_TIP * mm)


def lightening(bc_n, bc_r, bc_d):
    """Mirror of lightening_2d() in the .scad, for z >= 60."""
    web_r = max(HUB_D / 2.0, (bc_r + bc_d) if bc_n > 0 else 0.0)
    ri = web_r + 5.0
    ro = R_ROOT - 7.0
    if ro <= ri + 6.0:
        return None
    return {"r": (ri + ro) / 2.0, "d": (ro - ri) * 0.78, "n": 6, "phase": 30.0}


def polar(r, deg):
    a = math.radians(deg)
    return CX + r * mm * math.cos(a), CY + r * mm * math.sin(a)


def hour_angle(h, clockwise):
    """00:00 at the top."""
    return (90.0 - h * 15.0) if clockwise else (90.0 + h * 15.0)


def draw_dial(c, clockwise, geo):
    lh = geo["lh"]
    r_free = lh["r"] + lh["d"] / 2.0      # outermost reach of the lightening holes

    r_label = r_free + 3.0
    r_tick_h = r_label + 3.3
    r_tick_15 = r_tick_h + 2.6
    r_tick_5 = r_tick_h + 4.8

    # ---- reference: tooth tips ------------------------------------------
    c.setDash(2, 2)
    c.setLineWidth(0.25)
    c.setStrokeColorRGB(0.65, 0.65, 0.65)
    c.circle(CX, CY, R_TIP * mm)
    c.setDash()

    # ---- cut outlines ----------------------------------------------------
    c.setStrokeColorRGB(0, 0, 0)
    c.setLineWidth(0.5)
    c.circle(CX, CY, R_DIAL * mm)
    c.circle(CX, CY, (geo["bore"] / 2.0) * mm)

    c.setLineWidth(0.4)
    for i in range(lh["n"]):
        x, y = polar(lh["r"], lh["phase"] + i * 360.0 / lh["n"])
        c.circle(x, y, (lh["d"] / 2.0) * mm)
    for i in range(geo["bc_n"]):
        x, y = polar(geo["bc_r"], i * 360.0 / geo["bc_n"])
        c.circle(x, y, (geo["bc_d"] / 2.0) * mm)

    # The clamp hubs land on this face and cover everything inside r 22,
    # bolts included. Trim here instead and the bolt holes never get cut.
    c.setDash(3, 2)
    c.setLineWidth(0.35)
    c.setStrokeColorRGB(0.45, 0.45, 0.45)
    c.circle(CX, CY, (HUB_CLAMP_D / 2.0) * mm)
    c.setDash()
    c.setStrokeColorRGB(0, 0, 0)

    # ---- graduations: hour / 15 min / 5 min ------------------------------
    for k in range(24 * 12):
        a = hour_angle(k * 5.0 / 60.0, clockwise)
        if k % 12 == 0:
            r_in, lw = r_tick_h, 0.7
        elif k % 3 == 0:
            r_in, lw = r_tick_15, 0.45     # one per tooth
        else:
            r_in, lw = r_tick_5, 0.2
        c.setLineWidth(lw)
        x0, y0 = polar(R_DIAL, a)
        x1, y1 = polar(r_in, a)
        c.line(x0, y0, x1, y1)

    # ---- HH:MM labels ----------------------------------------------------
    fs = 3.4
    c.setFillColorRGB(0, 0, 0)
    c.setFont("Helvetica-Bold", fs * mm)
    for h in range(24):
        a = hour_angle(h, clockwise)
        x, y = polar(r_label, a)
        c.saveState()
        c.translate(x, y)
        # tangential, so each label is upright as it passes a fixed index
        c.rotate(a - 90.0)
        c.drawCentredString(0, -fs * mm * 0.36, "%02d:00" % h)
        c.restoreState()

    # ---- zero index ------------------------------------------------------
    p = c.beginPath()
    p.moveTo(*polar(R_DIAL - 0.6, 90.0))
    p.lineTo(*polar(R_DIAL - 4.2, 88.0))
    p.lineTo(*polar(R_DIAL - 4.2, 92.0))
    p.close()
    c.drawPath(p, fill=1, stroke=0)

    c.setFont("Helvetica-Bold", 2.6 * mm)
    c.drawCentredString(CX, CY + 25.5 * mm, "SIDEREAL")
    c.setFont("Helvetica", 2.0 * mm)
    c.drawCentredString(CX, CY - 26.5 * mm, "CW" if clockwise else "CCW")


def draw_notes(c, clockwise, geo):
    lh = geo["lh"]
    r_free = lh["r"] + lh["d"] / 2.0
    per_tooth = SIDEREAL_S / ZWF

    y = 70 * mm
    c.setFillColorRGB(0, 0, 0)
    c.setFont("Helvetica-Bold", 3.2 * mm)
    c.drawString(14 * mm, y, "24 h SIDEREAL DIAL - final gear, %d T module %0.1f" % (ZWF, MOD_F))
    y -= 5.0 * mm
    c.setFont("Helvetica", 2.5 * mm)
    for ln in [
        "Direction: HH increases %s viewed from above (printed side up)."
        % ("CLOCKWISE" if clockwise else "COUNTER-CLOCKWISE"),
        "One revolution = one sidereal day = %0.1f s. 01:00 = one sidereal hour = 59 m 50.17 s"
        % SIDEREAL_S,
        "of civil time - this reads hour angle, not wall clock.",
        "1 tooth = %0.2f deg = %0.1f s = %d m %04.1f s. %d teeth over 24 h, so every 15-minute"
        % (360.0 / ZWF, per_tooth, int(per_tooth // 60), per_tooth % 60, ZWF),
        "tick is exactly one tooth pitch - use them to clock the dial.",
        "",
        "Cut outs (build.ps1: bore=%g, bc_holes=%d, bc_r=%g): outer %0.1f dia (0.5 inside the"
        % (geo["bore"], geo["bc_n"], geo["bc_r"], R_DIAL * 2),
        "%0.1f root), %0.0f dia register bore, %d x %0.1f dia bolts on r %0.0f, %d x %0.3f dia"
        % (R_ROOT * 2, geo["bore"], geo["bc_n"], geo["bc_d"], geo["bc_r"], lh["n"], lh["d"]),
        "lightening holes on r %0.2f at %0.0f deg + i*%0.0f deg. Those reach r %0.2f, so all"
        % (lh["r"], lh["phase"], 360.0 / lh["n"], r_free),
        "graduation sits in the solid band r %0.2f to %0.1f. Outer dashed = tooth tips (%0.0f"
        % (r_free, R_ROOT, R_TIP * 2),
        "dia); inner dashed %0.0f dia = clamp-hub footprint - trim there and skip the bolts."
        % HUB_CLAMP_D,
        "",
        "PRINT AT 100% (page scaling off) - verify with the bar below.",
    ]:
        c.drawString(14 * mm, y, ln)
        y -= 3.4 * mm

    bx, by = 14 * mm, 14 * mm
    c.setLineWidth(0.6)
    c.line(bx, by, bx + 100 * mm, by)
    for i in range(11):
        x = bx + i * 10 * mm
        c.line(x, by, x, by + (3.0 if i % 5 == 0 else 1.8) * mm)
    c.setFont("Helvetica", 2.4 * mm)
    c.drawString(bx - 0.5 * mm, by + 4.5 * mm, "0")
    c.drawString(bx + 96 * mm, by + 4.5 * mm, "100 mm")
    c.drawString(bx + 108 * mm, by, "measure this: must be exactly 100 mm")


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--bore", type=float, default=22.0,
                    help="central register bore, mm (build.ps1 uses 22)")
    ap.add_argument("--bolts", type=int, default=6,
                    help="clamp bolt count (build.ps1 $OutBolts)")
    ap.add_argument("--bolt-r", type=float, default=18.0,
                    help="clamp bolt circle radius, mm (build.ps1 $OutBoltR)")
    ap.add_argument("--bolt-d", type=float, default=3.2, help="M3 clearance, mm")
    ap.add_argument("-o", "--out", default=OUT)
    a = ap.parse_args()

    lh = lightening(a.bolts, a.bolt_r, a.bolt_d)
    if lh is None:
        raise SystemExit("no lightening holes for these parameters - dial band undefined")
    geo = {"bore": a.bore, "bc_n": a.bolts, "bc_r": a.bolt_r,
           "bc_d": a.bolt_d, "lh": lh}

    d = os.path.dirname(a.out)
    if d:
        os.makedirs(d, exist_ok=True)

    c = canvas.Canvas(a.out, pagesize=letter)
    c.setTitle("24 h sidereal dial - final gear %d T module %0.1f" % (ZWF, MOD_F))
    for clockwise in (True, False):
        draw_dial(c, clockwise, geo)
        draw_notes(c, clockwise, geo)
        c.showPage()
    c.save()

    r_free = lh["r"] + lh["d"] / 2.0
    print(f"wrote {a.out}")
    print(f"  dial {R_DIAL*2:.1f} dia, graduation band r {r_free:.2f}..{R_ROOT:.1f}"
          f" ({R_ROOT - r_free:.2f} mm wide)")
    print(f"  {lh['n']} lightening holes {lh['d']:.3f} dia on r {lh['r']:.2f}")
    print(f"  1 tooth = {360.0/ZWF:.2f} deg = {SIDEREAL_S/ZWF:.2f} s")


if __name__ == "__main__":
    main()
