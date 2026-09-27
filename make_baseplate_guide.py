#!/usr/bin/env python3
"""
Baseplate assembly guide — what sits on which shaft, and with what hardware.

Deliberately NOT to scale: the 96T wheel is 196 mm across and the whole gear
footprint is 226 x 279 mm, which will not fit a Letter page at 1:1. Use
baseplate_template.pdf for drilling; this one is for the bench.

    python make_baseplate_guide.py
"""
import math
from reportlab.pdfgen import canvas
from reportlab.lib.pagesizes import letter
from reportlab.lib.units import mm
import baseplate_geom as G

OUT = "out/docs/baseplate_assembly_guide.pdf"

# ---- what sits on each shaft ---------------------------------------------
# (n, shaft, x, y, spacer_mm, gear_outlines[(dia, label)], parts, fasteners)
STACK = [
    (1, "S0  motor", 0.0, 0.0, None,
     [(42.0, "NEMA17 body, under plate"), (17.0, "15T pinion")],
     "NEMA17 below plate; pinion.stl above",
     "4 x M3x8 flat head, countersunk"),
    (2, "S1", 67.5, 0.0, 2.0,
     [(122.0, "120T wheel"), (17.0, "15T pinion")],
     "spacer, then stage.stl (120T + 15T)",
     "20d nail shaft, head under the plate"),
    (3, "S2", 67.5, 67.5, 8.5,
     [(122.0, "120T wheel"), (17.0, "15T pinion")],
     "spacer, then stage.stl (120T + 15T)",
     "20d nail shaft, head under the plate"),
    (4, "S3", 0.0, 67.5, 15.0,
     [(122.0, "120T wheel"), (52.0, "24T module-2 pinion")],
     "spacer, then final_stage_gear.stl",
     "20d nail shaft, head under the plate"),
    (5, "S4  output", 0.0, -52.5, None,
     [(196.0, "96T module-2 wheel"), (44.0, "hubs / tower flange")],
     "tower halves clamp the plate, 2x625ZZ, shaft, washer, hubs, 96T wheel",
     "3 x M3x14 into the disc; 6 x M3x20 hub clamp"),
]

PW, PH = letter
DRAW = (12*mm, 92*mm, 204*mm, 268*mm)
SCALE = 0.62

MX0, MX1 = -98.0, 128.5
MY0, MY1 = -150.5, 128.5
OX = (DRAW[0]+DRAW[2])/2 - SCALE*((MX0+MX1)/2)*mm
OY = (DRAW[1]+DRAW[3])/2 - SCALE*((MY0+MY1)/2)*mm


def X(v):
    return OX + SCALE*v*mm


def Y(v):
    return OY + SCALE*v*mm


def R(v):
    return SCALE*v*mm


def main():
    c = canvas.Canvas(OUT, pagesize=letter)
    c.setTitle("Sidereal drive gearbox - baseplate assembly guide")

    # ---- title -----------------------------------------------------------
    c.setFont("Helvetica-Bold", 13)
    c.drawString(12*mm, 272*mm, "Baseplate assembly guide")
    c.setFont("Helvetica-Bold", 8.5)
    c.setFillColorRGB(0.65, 0.15, 0.10)
    c.drawRightString(204*mm, 272.6*mm, "NOT TO SCALE - drill from baseplate_template.pdf")
    c.setFillColorRGB(0, 0, 0)

    # ---- gear / motor outlines, dashed -----------------------------------
    c.saveState()
    p = c.beginPath()
    p.rect(DRAW[0], DRAW[1], DRAW[2]-DRAW[0], DRAW[3]-DRAW[1])
    c.clipPath(p, stroke=0, fill=0)
    for _n, _s, x, y, _sp, outlines, _pt, _f in STACK:
        for dia, _lbl in outlines:
            c.setStrokeColorRGB(0.62, 0.62, 0.62)
            c.setLineWidth(0.5)
            c.setDash(3, 2)
            if abs(dia - 42.0) < 0.01:          # NEMA17 body is square
                c.rect(X(x)-R(21), Y(y)-R(21), R(42), R(42), stroke=1, fill=0)
            else:
                c.circle(X(x), Y(y), R(dia/2), stroke=1, fill=0)
            c.setDash()
    c.restoreState()

    # ---- the plate itself, solid -----------------------------------------
    hull = G.hull_polygon(n=1440)
    c.setStrokeColorRGB(0, 0, 0)
    c.setLineWidth(1.4)
    path = c.beginPath()
    path.moveTo(X(hull[0][0]), Y(hull[0][1]))
    for x, y in hull[1:]:
        path.lineTo(X(x), Y(y))
    path.close()
    c.drawPath(path, stroke=1, fill=0)

    c.setLineWidth(0.7)
    for cx, cy, d, _tag in G.holes():
        c.circle(X(cx), Y(cy), R(d/2), stroke=1, fill=0)

    # ---- numbered callouts -----------------------------------------------
    for n, _s, x, y, _sp, _o, _pt, _f in STACK:
        c.setFillColorRGB(0.10, 0.10, 0.10)
        c.circle(X(x), Y(y), 3.6*mm, stroke=0, fill=1)
        c.setFillColorRGB(1, 1, 1)
        c.setFont("Helvetica-Bold", 9)
        c.drawCentredString(X(x), Y(y) - 1.25*mm, str(n))
    c.setFillColorRGB(0, 0, 0)

    # wedge interface callout, offset so it does not sit on the tower bore
    c.setFillColorRGB(0.10, 0.10, 0.10)
    c.circle(X(0) + R(25)*0.707, Y(-52.5) - R(25)*0.707, 3.6*mm, stroke=0, fill=1)
    c.setFillColorRGB(1, 1, 1)
    c.setFont("Helvetica-Bold", 9)
    c.drawCentredString(X(0) + R(25)*0.707, Y(-52.5) - R(25)*0.707 - 1.25*mm, "6")
    c.setFillColorRGB(0, 0, 0)

    c.setFont("Helvetica-Oblique", 7)
    c.setFillColorRGB(0.45, 0.45, 0.45)
    c.drawString(12*mm, 88*mm,
                 "Dashed = parts that sit above the plate. They overhang it by design.")
    c.setFillColorRGB(0, 0, 0)

    # ---- table -----------------------------------------------------------
    ty = 78*mm
    cols = [12, 19, 38, 53, 133]        # mm from left
    edges = cols[1:] + [204]            # each column ends where the next starts
    c.setFont("Helvetica-Bold", 7.5)
    for cx, h in zip(cols, ["#", "Shaft", "Spacer", "Sits on it (bottom to top)",
                            "Fasteners"]):
        c.drawString(cx*mm, ty, h)
    c.setLineWidth(0.6)
    c.line(12*mm, ty - 1.6*mm, 204*mm, ty - 1.6*mm)

    rows = [(str(n), s, ("-" if sp is None else f"{sp:g} mm"), pt, f)
            for n, s, _x, _y, sp, _o, pt, f in STACK]
    rows.append(("6", "wedge", "-", "tripod / wedge adapter, below plate",
                 "4 x M5 on dia 50"))

    overflow = []
    y = ty - 6.2*mm
    c.setFont("Helvetica", 7)
    for r in rows:
        for cx, edge, txt in zip(cols, edges, r):
            w = c.stringWidth(txt, "Helvetica", 7)
            if w > (edge - cx - 2)*mm:
                overflow.append((r[0], txt, w/mm, edge-cx-2))
            c.drawString(cx*mm, y, txt)
        y -= 5.0*mm

    # ---- notes -----------------------------------------------------------
    y -= 1.5*mm
    c.setLineWidth(0.4)
    c.setStrokeColorRGB(0.6, 0.6, 0.6)
    c.line(12*mm, y + 2.5*mm, 204*mm, y + 2.5*mm)
    c.setStrokeColorRGB(0, 0, 0)
    c.setFont("Helvetica-Bold", 7.5)
    c.drawString(12*mm, y - 1*mm, "Notes")
    c.setFont("Helvetica", 7)
    notes = [
        "Only THREE of the five printed spacers get used - S1 2.0, S2 8.5, S3 15.0. S0's would fall through the dia 23 motor bore "
        "(pinion height is set by its grub screw); S4's job is done by the bearing tower.",
        "Nails go in from UNDERNEATH, head against the plate's underside. Cut the point off and square the end.",
        "Trim the NEMA17 shaft to ~10 mm above the plate. At full 24 mm length it fouls the 96T output wheel.",
        "Countersink the 4 motor holes: socket-cap heads hit the S1 wheel 2.0 mm above the plate. "
        "M3x8 flat head leaves 3.7 mm of thread; M3x10 bottoms out in the motor.",
        "The M5 wedge hole nearest S1 is also under the S1 wheel: countersink it or leave that bolt out.",
        "Nothing over dia 13 mm may sit inline at S1-S3 - a stock 5 mm shaft collar will hit the neighbouring wheel.",
        "Rewind then approach the start angle FORWARDS; the train has ~750 arcsec of backlash at the output.",
    ]
    y -= 5.0*mm
    for n in notes:
        # wrap to the page width
        words, line = n.split(), ""
        for w in words:
            t = (line + " " + w).strip()
            if c.stringWidth(t, "Helvetica", 7) > 190*mm:
                c.drawString(12*mm, y, line)
                y -= 3.3*mm
                line = w
            else:
                line = t
        c.drawString(12*mm, y, line)
        y -= 4.2*mm

    c.showPage()
    c.save()
    print(f"wrote {OUT}")
    print(f"  scale {SCALE:g}  (content {MX1-MX0:.1f} x {MY1-MY0:.1f} mm)")
    print(f"  {len(STACK)} numbered stations, {len(G.holes())} holes drawn")
    print(f"  lowest note baseline {y/mm:.1f} mm from page bottom "
          f"{'OK' if y/mm > 6 else 'OVERFLOW'}")
    if overflow:
        print("  TABLE OVERFLOW:")
        for rid, txt, w, avail in overflow:
            print(f"    row {rid}: {w:.0f} mm into {avail:.0f} mm  \"{txt}\"")
    else:
        print("  table columns: no overflow")


if __name__ == "__main__":
    main()
