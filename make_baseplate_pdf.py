#!/usr/bin/env python3
"""
Generate a 1:1 drilling template for the star-tracker gearbox baseplate.

Print with page scaling OFF ("Actual size" / 100%), then check the 100 mm
scale bar with a rule before you drill anything.

    python make_baseplate_pdf.py
"""
import math
from reportlab.pdfgen import canvas
from reportlab.lib.pagesizes import letter
from reportlab.lib.units import mm

OUT = "out/docs/baseplate_template.pdf"

# ---- model, kept in step with star_tracker_gears.scad --------------------
MODULE, ZP, ZW, ZWF, MOD_F, ZPF = 1.0, 15, 120, 96, 2.0, 24
CD_M1 = MODULE * (ZP + ZW) / 2           # 67.5
CD_M2 = MOD_F * (ZPF + ZWF) / 2          # 120.0
SHAFT_D = 5.0                            # drill size
SPACER_OD = 10.0

SHAFTS = [                               # (label, x, y, note)
    ("S0", 0.0,    0.0,   "motor"),
    ("S1", 67.5,   0.0,   ""),
    ("S2", 67.5,   67.5,  ""),
    ("S3", 0.0,    67.5,  ""),
    ("S4", 0.0,   -52.5,  "output"),
]
# tip radius of the largest gear on each shaft, for context circles
TIPS = [(0.0, 0.0, MODULE*ZP/2 + MODULE),
        (67.5, 0.0, MODULE*ZW/2 + MODULE),
        (67.5, 67.5, MODULE*ZW/2 + MODULE),
        (0.0, 67.5, MODULE*ZW/2 + MODULE),
        (0.0, -52.5, MOD_F*ZWF/2 + MOD_F)]

PW, PH = letter                          # 215.9 x 279.4 mm
DRAW = (12*mm, 52*mm, (215.9-12)*mm, 272*mm)   # x0,y0,x1,y1 of drawing area

# place model origin so the shaft pattern centres in the drawing area
MX0, MX1 = -25.0, 92.5
MY0, MY1 = -80.0, 92.5
ORIGIN_X = (DRAW[0] + DRAW[2]) / 2 - ((MX0 + MX1) / 2) * mm
ORIGIN_Y = (DRAW[1] + DRAW[3]) / 2 - ((MY0 + MY1) / 2) * mm


def X(x_mm):
    return ORIGIN_X + x_mm * mm


def Y(y_mm):
    return ORIGIN_Y + y_mm * mm


def main():
    c = canvas.Canvas(OUT, pagesize=letter)
    c.setTitle("Star tracker gearbox — baseplate drilling template (1:1)")
    c.setAuthor("star-tracker-gears")

    # ---- context: gear tip circles, clipped to the drawing area ---------
    c.saveState()
    p = c.beginPath()
    p.rect(DRAW[0], DRAW[1], DRAW[2]-DRAW[0], DRAW[3]-DRAW[1])
    c.clipPath(p, stroke=0, fill=0)
    c.setStrokeColorRGB(0.72, 0.72, 0.72)
    c.setLineWidth(0.4)
    c.setDash(3, 3)
    for cx, cy, r in TIPS:
        c.circle(X(cx), Y(cy), r * mm, stroke=1, fill=0)
    c.setDash()
    c.restoreState()

    # ---- centre-distance chain ------------------------------------------
    c.setStrokeColorRGB(0.55, 0.55, 0.55)
    c.setLineWidth(0.5)
    c.setDash(1, 2)
    chain = [(0, 0), (67.5, 0), (67.5, 67.5), (0, 67.5), (0, -52.5)]
    for a, b in zip(chain, chain[1:]):
        c.line(X(a[0]), Y(a[1]), X(b[0]), Y(b[1]))
    c.setDash()

    c.setFont("Helvetica", 7)
    c.setFillColorRGB(0.35, 0.35, 0.35)
    for a, b in zip(chain, chain[1:]):
        d = math.hypot(b[0]-a[0], b[1]-a[1])
        horizontal = abs(b[1]-a[1]) < 0.01
        if horizontal:
            mx, my = (a[0]+b[0])/2, (a[1]+b[1])/2
            c.drawCentredString(X(mx), Y(my) + 2.0*mm, f"{d:g}")
        else:
            # 35% along, not the midpoint: S3-S4 runs straight through S0
            t = 0.35
            mx = a[0] + t*(b[0]-a[0])
            my = a[1] + t*(b[1]-a[1])
            c.drawString(X(mx) + 3.5*mm, Y(my) - 1.0*mm, f"{d:g}")

    # ---- shaft holes -----------------------------------------------------
    for label, x, y, note in SHAFTS:
        cx, cy = X(x), Y(y)
        # spacer envelope, so you can see it clear the neighbouring wheel
        c.setStrokeColorRGB(0.80, 0.55, 0.25)
        c.setLineWidth(0.4)
        c.setDash(2, 2)
        c.circle(cx, cy, (SPACER_OD/2) * mm, stroke=1, fill=0)
        c.setDash()

        c.setStrokeColorRGB(0, 0, 0)
        c.setLineWidth(0.8)
        c.circle(cx, cy, (SHAFT_D/2) * mm, stroke=1, fill=0)

        arm = 9 * mm
        c.setLineWidth(0.5)
        c.line(cx - arm, cy, cx + arm, cy)
        c.line(cx, cy - arm, cx, cy + arm)
        c.setFillColorRGB(0, 0, 0)
        c.circle(cx, cy, 0.35 * mm, stroke=0, fill=1)

        c.setFont("Helvetica-Bold", 8)
        txt = f"{label}"
        if note:
            txt += f"  ({note})"
        c.drawString(cx + 3.2*mm, cy + 3.6*mm, txt)
        c.setFont("Helvetica", 7)
        c.setFillColorRGB(0.3, 0.3, 0.3)
        c.drawString(cx + 3.2*mm, cy + 0.9*mm, f"{x:g}, {y:g}")
        c.setFillColorRGB(0, 0, 0)

    # ---- datum -----------------------------------------------------------
    c.setStrokeColorRGB(0, 0, 0)
    c.setLineWidth(0.4)
    c.setFont("Helvetica-Oblique", 6.5)
    c.drawRightString(X(0) - 3.2*mm, Y(0) - 6.5*mm, "datum 0,0")

    # ---- scale bars, one per axis ---------------------------------------
    # Horizontal
    bx, by = DRAW[0] + 4*mm, DRAW[1] - 13*mm
    c.setStrokeColorRGB(0, 0, 0)
    c.setLineWidth(0.9)
    c.line(bx, by, bx + 100*mm, by)
    for t in (0, 50, 100):
        c.line(bx + t*mm, by, bx + t*mm, by + 2.6*mm)
    c.setFont("Helvetica-Bold", 7.5)
    for t in (0, 50, 100):
        c.drawCentredString(bx + t*mm, by + 3.8*mm, str(t))
    c.setFont("Helvetica-Bold", 8)
    c.drawString(bx + 104*mm, by + 3.0*mm, "100 mm — CHECK BOTH BARS")

    # Vertical, to catch a printer that scales the axes differently
    vx, vy = DRAW[0] + 3*mm, DRAW[1] + 6*mm
    c.setLineWidth(0.9)
    c.line(vx, vy, vx, vy + 100*mm)
    for t in (0, 50, 100):
        c.line(vx, vy + t*mm, vx + 2.6*mm, vy + t*mm)
    c.setFont("Helvetica-Bold", 7.5)
    for t in (0, 50, 100):
        c.drawString(vx + 3.4*mm, vy + t*mm - 0.9*mm, str(t))

    # ---- title block -----------------------------------------------------
    tx, ty, tw, th = DRAW[0], 10*mm, DRAW[2]-DRAW[0], 29*mm
    c.setLineWidth(0.6)
    c.rect(tx, ty, tw, th, stroke=1, fill=0)
    c.setFont("Helvetica-Bold", 10)
    c.drawString(tx + 3*mm, ty + th - 8*mm,
                 "Sidereal drive gearbox — baseplate drilling template")
    c.setFont("Helvetica", 7.5)
    lines = [
        f"SCALE 1:1 on US Letter. Print at 100% / Actual size — no page scaling, no fit-to-page.",
        f"5 holes Ø{SHAFT_D:g} mm. Centre distances {CD_M1:g} mm (stages 1-3) and {CD_M2:g} mm (final). Total 2048:1.",
        f"Spacer envelope Ø{SPACER_OD:g} shown dashed — hard max Ø13 before it fouls the neighbouring wheel at S2 and S3.",
        f"Shafts S0 and S1 sit under the 196 mm output wheel: keep them below 19 mm tall. No top plate on those two.",
        f"Dashed grey = gear tip circles for context (clipped). Baseplate itself is approx 246 x 299 mm — larger than this page.",
    ]
    yy = ty + th - 13*mm
    for ln in lines:
        c.drawString(tx + 3*mm, yy, ln)
        yy -= 3.6*mm

    c.showPage()
    c.save()
    print(f"wrote {OUT}")
    print(f"  page      {PW/mm:.1f} x {PH/mm:.1f} mm (US Letter portrait)")
    print(f"  origin at {ORIGIN_X/mm:.2f}, {ORIGIN_Y/mm:.2f} mm from page corner")
    for label, x, y, _ in SHAFTS:
        print(f"  {label}  model {x:7.2f},{y:7.2f}  ->  page "
              f"{X(x)/mm:7.2f},{Y(y)/mm:7.2f} mm")


if __name__ == "__main__":
    main()
