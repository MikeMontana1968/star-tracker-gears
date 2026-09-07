#!/usr/bin/env python3
"""
Generate a 1:1 template for the star-tracker gearbox baseplate.

Draws the real plate outline and every hole, so it works as a drilling
template on stock you cut yourself, or as a check print against the SVG you
send to a cutting service.

Print with page scaling OFF ("Actual size" / 100%), then measure BOTH scale
bars with a rule before you drill anything.

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

PLATE_T     = 4.0      # bp_t
SHAFT_HOLE  = 4.9      # bp_shaft_d, light press for a 20d nail
MOTOR_BOSS  = 23.0     # bp_motor_boss, NEMA17 pilot registers here
MOTOR_BC    = 31.0     # bp_motor_bc, square pattern
TOWER_D     = 22.4     # bp_tower_d, bearing tower spigot
TOWER_BC    = 28.0     # bp_tower_bc, 3 x M3
MOUNT_BC    = 50.0     # bp_mount_bc, wedge interface on the OUTPUT axis
MOUNT_D     = 5.5      # bp_mount_d, M5
MOUNT_N     = 4
M3          = 3.4

SHAFTS = [("S0", 0.0, 0.0, "motor"), ("S1", 67.5, 0.0, ""),
          ("S2", 67.5, 67.5, ""), ("S3", 0.0, 67.5, ""),
          ("S4", 0.0, -52.5, "output")]

# outline is the convex hull of a lobe at each shaft (bp_lobe)
LOBES = [((0.0, 0.0), 26.0), ((67.5, 0.0), 20.0), ((67.5, 67.5), 20.0),
         ((0.0, 67.5), 20.0), ((0.0, -52.5), 34.0)]

# gear tip circles, drawn faintly so you can see what overhangs the plate
TIPS = [(0.0, 0.0, MODULE*ZP/2 + MODULE), (67.5, 0.0, MODULE*ZW/2 + MODULE),
        (67.5, 67.5, MODULE*ZW/2 + MODULE), (0.0, 67.5, MODULE*ZW/2 + MODULE),
        (0.0, -52.5, MOD_F*ZWF/2 + MOD_F)]

PW, PH = letter
DRAW = (12*mm, 52*mm, (215.9-12)*mm, 272*mm)

MX0, MX1 = -40.0, 94.0
MY0, MY1 = -93.0, 94.0
ORIGIN_X = (DRAW[0] + DRAW[2]) / 2 - ((MX0 + MX1) / 2) * mm
ORIGIN_Y = (DRAW[1] + DRAW[3]) / 2 - ((MY0 + MY1) / 2) * mm


def X(v):
    return ORIGIN_X + v * mm


def Y(v):
    return ORIGIN_Y + v * mm


def hull_of_lobes(lobes, n=360):
    pts = []
    for (cx, cy), r in lobes:
        for k in range(n):
            a = 2 * math.pi * k / n
            pts.append((cx + r * math.cos(a), cy + r * math.sin(a)))
    pts = sorted(set(pts))

    def half(seq):
        st = []
        for q in seq:
            while len(st) > 1:
                (ax, ay), (bx, by) = st[-2], st[-1]
                if (bx - ax) * (q[1] - ay) - (by - ay) * (q[0] - ax) <= 0:
                    st.pop()
                else:
                    break
            st.append(q)
        return st

    return half(pts)[:-1] + half(pts[::-1])[:-1]


def main():
    c = canvas.Canvas(OUT, pagesize=letter)
    c.setTitle("Star tracker gearbox - baseplate template (1:1)")

    # ---- faint gear tip circles, clipped ---------------------------------
    c.saveState()
    p = c.beginPath()
    p.rect(DRAW[0], DRAW[1], DRAW[2]-DRAW[0], DRAW[3]-DRAW[1])
    c.clipPath(p, stroke=0, fill=0)
    c.setStrokeColorRGB(0.80, 0.80, 0.80)
    c.setLineWidth(0.4)
    c.setDash(3, 3)
    for cx, cy, r in TIPS:
        c.circle(X(cx), Y(cy), r * mm, stroke=1, fill=0)
    c.setDash()
    c.restoreState()

    # ---- plate outline ---------------------------------------------------
    hull = hull_of_lobes(LOBES)
    c.setStrokeColorRGB(0, 0, 0)
    c.setLineWidth(1.1)
    path = c.beginPath()
    path.moveTo(X(hull[0][0]), Y(hull[0][1]))
    for x, y in hull[1:]:
        path.lineTo(X(x), Y(y))
    path.close()
    c.drawPath(path, stroke=1, fill=0)

    # ---- centre-distance chain -------------------------------------------
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
        if abs(b[1]-a[1]) < 0.01:
            c.drawCentredString(X((a[0]+b[0])/2), Y(a[1]) + 2.0*mm, f"{d:g}")
        else:
            t = 0.35
            c.drawString(X(a[0] + t*(b[0]-a[0])) + 3.5*mm,
                         Y(a[1] + t*(b[1]-a[1])) - 1.0*mm, f"{d:g}")
    c.setFillColorRGB(0, 0, 0)

    # ---- holes -----------------------------------------------------------
    def hole(x, y, d, lw=0.8):
        c.setLineWidth(lw)
        c.circle(X(x), Y(y), (d/2) * mm, stroke=1, fill=0)

    # shaft 0: motor boss + the 31 mm square pattern
    hole(0, 0, MOTOR_BOSS)
    for a in (45, 135, 225, 315):
        r = MOTOR_BC / math.sqrt(2)
        hole(r*math.cos(math.radians(a)), r*math.sin(math.radians(a)), M3)
    # shafts 1-3: nails
    for _, x, y, _n in SHAFTS[1:4]:
        hole(x, y, SHAFT_HOLE)
    # shaft 4: tower spigot, flange bolts, wedge interface
    hole(0, -52.5, TOWER_D)
    for i in range(3):
        a = math.radians(i*120 + 60)
        hole((TOWER_BC/2)*math.cos(a), -52.5 + (TOWER_BC/2)*math.sin(a), M3)
    for i in range(MOUNT_N):
        a = math.radians(i*360/MOUNT_N + 45)
        hole((MOUNT_BC/2)*math.cos(a), -52.5 + (MOUNT_BC/2)*math.sin(a),
             MOUNT_D, lw=1.0)

    # ---- shaft crosshairs and labels -------------------------------------
    for label, x, y, note in SHAFTS:
        cx, cy = X(x), Y(y)
        c.setLineWidth(0.5)
        c.setStrokeColorRGB(0, 0, 0)
        arm = 9 * mm
        c.line(cx - arm, cy, cx + arm, cy)
        c.line(cx, cy - arm, cx, cy + arm)
        c.setFillColorRGB(0, 0, 0)
        c.circle(cx, cy, 0.35*mm, stroke=0, fill=1)
        c.setFont("Helvetica-Bold", 8)
        c.drawString(cx + 3.4*mm, cy + 4.2*mm,
                     label + (f"  ({note})" if note else ""))
        c.setFont("Helvetica", 7)
        c.setFillColorRGB(0.3, 0.3, 0.3)
        c.drawString(cx + 3.4*mm, cy + 1.4*mm, f"{x:g}, {y:g}")
        c.setFillColorRGB(0, 0, 0)

    c.setFont("Helvetica-Oblique", 6.5)
    c.drawRightString(X(0) - 3.4*mm, Y(0) - 6.5*mm, "datum 0,0")

    # ---- scale bars, one per axis ----------------------------------------
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
    c.drawString(bx + 104*mm, by + 3.0*mm, "100 mm - CHECK BOTH BARS")

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
                 "Sidereal drive gearbox - baseplate, 1:1")
    c.setFont("Helvetica", 7.5)
    lines = [
        "SCALE 1:1 on US Letter. Print at 100% / Actual size - no page scaling, no fit-to-page.",
        f"Plate 121.5 x 174 x {PLATE_T:g} mm, 6061 aluminium. 157 cm2, ~170 g at {PLATE_T:g} mm.",
        f"S1-S3 dia {SHAFT_HOLE:g} press fit for 20d nails. S0 dia {MOTOR_BOSS:g} NEMA17 boss + 4 x M3 on {MOTOR_BC:g} square.",
        f"S4 dia {TOWER_D:g} tower spigot + 3 x M3 on dia {TOWER_BC:g}. Wedge mount {MOUNT_N} x M5 on dia {MOUNT_BC:g}.",
        "Wedge bolts are centred on the OUTPUT axis, not the plate centroid - shortest load path.",
        "Faint dashed circles are gear tip circles - the wheels overhang the plate by design.",
    ]
    yy = ty + th - 11.0*mm
    for ln in lines:
        c.drawString(tx + 3*mm, yy, ln)
        yy -= 3.2*mm

    c.showPage()
    c.save()
    print(f"wrote {OUT}")
    print(f"  plate outline {max(p[0] for p in hull)-min(p[0] for p in hull):.1f}"
          f" x {max(p[1] for p in hull)-min(p[1] for p in hull):.1f} mm")
    for label, x, y, _ in SHAFTS:
        print(f"  {label}  model {x:7.2f},{y:7.2f}  ->  page "
              f"{X(x)/mm:7.2f},{Y(y)/mm:7.2f} mm")


if __name__ == "__main__":
    main()
