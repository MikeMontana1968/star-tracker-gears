#!/usr/bin/env python3
"""
Baseplate marking file for the Ray5 laser engraver (LightBurn / LaserGRBL).

Geometry comes from baseplate_geom.py, so it stays in step with the DXF and
the drilling template. Units are millimetres, 1 SVG unit = 1 mm, drawn the way
the plate looks from the TOP (+Y up on the plate = up on the page); S0 is at the
origin of the plate frame.

Layers, by stroke/fill colour (map each to a LightBurn layer):
  black  FILL    labels (text is converted to paths -- no font needed)  engrave
  black  STROKE  rotation arrows                                        engrave
  blue   STROKE  motor mount outline (NEMA17 42.3 mm square)            score
  green  STROKE  hole circles, true size                                 score
  red    STROKE  plate outline                                          alignment
  magenta STROKE crosshair at the centre of the plate's bounding box    alignment

Turn the red/green layers off ("Output: no") if you only want the marks and are
registering the plate by hand; leave them on to engrave onto a jig sheet first.

Rotation arrows: gear meshes reverse the direction at every stage, so the arrows
alternate. S0 (motor) is drawn clockwise as the reference; flip all of them if
the motor is wired the other way.

    python make_baseplate_ray5_svg.py
"""
import math
import baseplate_geom as G
from fontTools.ttLib import TTFont
from fontTools.pens.svgPathPen import SVGPathPen
from fontTools.pens.transformPen import TransformPen

OUT = "out/docs/baseplate_ray5.svg"
FONT = r"C:\Windows\Fonts\arialbd.ttf"
MARGIN = 6.0
NEMA17 = 42.3                    # motor face, square
CHAMFER = 4.0                    # NEMA17 corner relief

_font = TTFont(FONT)
_gs = _font.getGlyphSet()
_cmap = _font.getBestCmap()
_upm = _font["head"].unitsPerEm
_cap = _font["OS/2"].sCapHeight


def text_path(s, x, y, cap_mm, anchor="middle"):
    """Text as a filled SVG path. (x, y) is the baseline anchor, y up on the plate."""
    k = cap_mm / _cap
    width = sum(_gs[_cmap[ord(c)]].width for c in s) * k
    x0 = x - (width / 2 if anchor == "middle" else 0)
    d = []
    pen_x = 0
    for c in s:
        g = _gs[_cmap[ord(c)]]
        pen = SVGPathPen(_gs, ntos=lambda v: f"{v:.3f}")
        # font units -> mm, flip Y for the page (page y = -plate y)
        tp = TransformPen(pen, (k, 0, 0, -k, x0 + pen_x * k, -y))
        g.draw(tp)
        d.append(pen.getCommands())
        pen_x += g.width
    return " ".join(d)


def P(x, y):
    """Plate (x, y up) -> page coordinates."""
    return x, -y


def circle(cx, cy, r):
    return f'<circle cx="{cx:.3f}" cy="{-cy:.3f}" r="{r:.3f}"/>'


def arc_arrow(cx, cy, r, cw, a_start=40, sweep=270, head=3.2):
    """Circular arrow about (cx, cy). Angles are plate-frame degrees, CCW positive."""
    sgn = -1 if cw else 1
    a0 = math.radians(a_start)
    a1 = math.radians(a_start + sgn * sweep)
    # stop the shaft short of the tip so the head sits on the end of the arc
    back = (head * 0.9) / r
    a_end = a1 - sgn * back
    p0 = (cx + r * math.cos(a0), cy + r * math.sin(a0))
    p1 = (cx + r * math.cos(a_end), cy + r * math.sin(a_end))
    large = 1 if sweep - math.degrees(back) > 180 else 0
    # page y is flipped, so a plate-CCW arc is SVG sweep-flag 0
    sweep_flag = 1 if cw else 0
    out = (f'<path d="M {p0[0]:.3f} {-p0[1]:.3f} A {r:.3f} {r:.3f} 0 {large} '
           f'{sweep_flag} {p1[0]:.3f} {-p1[1]:.3f}"/>')
    # head: triangle at the tip, pointing along the tangent
    tip = (cx + r * math.cos(a1), cy + r * math.sin(a1))
    t = (-math.sin(a1) * sgn, math.cos(a1) * sgn)      # direction of travel
    n = (math.cos(a1), math.sin(a1))                   # radial
    w = head * 0.55
    b = (tip[0] - t[0] * head, tip[1] - t[1] * head)
    tri = [tip, (b[0] + n[0] * w, b[1] + n[1] * w), (b[0] - n[0] * w, b[1] - n[1] * w)]
    pts = " ".join(f"{x:.3f},{-y:.3f}" for x, y in tri)
    out += f'<polygon points="{pts}" fill="#000" stroke="none"/>'
    return out


def motor_outline(cx, cy):
    h = NEMA17 / 2
    c = CHAMFER
    pts = [(h - c, h), (-h + c, h), (-h, h - c), (-h, -h + c), (-h + c, -h),
           (h - c, -h), (h, -h + c), (h, h - c)]
    return " ".join(f"{cx + x:.3f},{-(cy + y):.3f}" for x, y in pts)


def build():
    x0, y0, x1, y1 = G.bbox()
    # include the motor square, which overhangs the plate at its corners
    h = NEMA17 / 2
    x0, y0 = min(x0, -h), min(y0, -h)
    x1, y1 = max(x1, h), max(y1, h)
    vb_x, vb_y = x0 - MARGIN, -(y1 + MARGIN)
    vb_w, vb_h = (x1 - x0) + 2 * MARGIN, (y1 - y0) + 2 * MARGIN

    s = []
    s.append('<?xml version="1.0" encoding="UTF-8"?>')
    s.append(f'<svg xmlns="http://www.w3.org/2000/svg" width="{vb_w:.2f}mm" '
             f'height="{vb_h:.2f}mm" viewBox="{vb_x:.2f} {vb_y:.2f} {vb_w:.2f} {vb_h:.2f}">')
    s.append('<title>Star tracker baseplate - Ray5 marking file</title>')

    # --- red: plate outline --------------------------------------------------
    poly = G.hull_polygon(n=180)
    pts = " ".join(f"{x:.3f},{-y:.3f}" for x, y in poly)
    s.append(f'<g id="plate_outline" fill="none" stroke="#ff0000" stroke-width="0.2">'
             f'<polygon points="{pts}"/></g>')

    # --- green: holes ----------------------------------------------------------
    s.append('<g id="holes" fill="none" stroke="#00a000" stroke-width="0.2">')
    for cx, cy, d, _tag in G.holes():
        s.append(circle(cx, cy, d / 2))
    s.append('</g>')

    # --- blue: motor mount outline --------------------------------------------
    s.append(f'<g id="motor_mount" fill="none" stroke="#0000ff" stroke-width="0.3">'
             f'<polygon points="{motor_outline(0, 0)}"/></g>')

    # --- magenta: crosshair at the centre of the plate's bounding box ---------
    bx0, by0, bx1, by1 = G.bbox()
    ccx, ccy = (bx0 + bx1) / 2, (by0 + by1) / 2
    arm = 6.0
    s.append(f'<g id="crosshair" fill="none" stroke="#ff00ff" stroke-width="0.2">'
             f'<path d="M {ccx-arm:.3f} {-ccy:.3f} L {ccx+arm:.3f} {-ccy:.3f} '
             f'M {ccx:.3f} {-(ccy-arm):.3f} L {ccx:.3f} {-(ccy+arm):.3f}"/>'
             f'{circle(ccx, ccy, 2.0)}</g>')

    # --- black: arrows ------------------------------------------------------
    # (label, ring radius) -- rings sit between each shaft hole and the next feature
    rings = {"S0": 16.0, "S1": 9.5, "S2": 9.5, "S3": 9.5, "S4": 13.6}
    s.append('<g id="rotation_arrows" fill="none" stroke="#000000" '
             'stroke-width="0.5" stroke-linecap="round">')
    for i, (lbl, x, y, _n) in enumerate(G.SHAFTS):
        cw = (i % 2 == 0)                    # S0 CW, then alternate per mesh
        # open the gap of each ring toward its label so they don't collide
        s.append(arc_arrow(x, y, rings[lbl], cw, a_start=(60 if i != 4 else 120)))
    s.append('</g>')

    # --- black: labels -------------------------------------------------------
    s.append('<g id="labels" fill="#000000" stroke="none">')
    label_pos = {            # (x, y baseline, cap height) in the plate frame
        "S0": (0.0, -3.0, 5.0),
        "S1": (67.5, -19.5, 5.0),
        "S2": (67.5, 67.5 + 12.5, 5.0),
        "S3": (0.0, 67.5 + 12.5, 5.0),
        "S4": (0.0, -52.5 - 2.5, 5.0),
    }
    for lbl, (x, y, cap) in label_pos.items():
        if lbl in ("S0", "S4"):           # keep these out of the shaft hole
            continue
        s.append(f'<path d="{text_path(lbl, x, y, cap)}"/>')
    # sub-labels
    # S0 / S4 sit to the right of their shafts: outside the hole, ring and mount outline
    for lbl, sub, lx, ly in (("S0", "MOTOR", NEMA17 / 2 + 3.5, 21.0),
                             ("S4", "OUTPUT", 23.0, -52.5)):
        s.append(f'<path d="{text_path(lbl, lx, ly - 1.0, 5.0, anchor="start")}"/>')
        s.append(f'<path d="{text_path(sub, lx, ly - 5.5, 2.2, anchor="start")}"/>')
    # motor mount label, on the outline's lower edge, outside the plate-hole ring
    s.append(f'<path d="{text_path("MOTOR MOUNT (NEMA17)", 0, -NEMA17/2 - 3.6, 2.6)}"/>')
    s.append('</g>')

    # --- rotation key ----------------------------------------------------------
    s.append('<g id="key" fill="#000000" stroke="none">')
    for n, line in enumerate(["ARROWS: ROTATION", "VIEWED FROM TOP", "S0 = CW REFERENCE", "EACH MESH REVERSES"]):
        s.append(f'<path d="{text_path(line, 40.0, 38.0 - n * 3.6, 1.9)}"/>')
    s.append('</g>')

    s.append('</svg>')
    return "\n".join(s)


if __name__ == "__main__":
    import os
    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    with open(OUT, "w", encoding="utf-8") as f:
        f.write(build())
    print("wrote", OUT)
