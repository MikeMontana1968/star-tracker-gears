#!/usr/bin/env python3
"""1 cm calibration grid for the Ray5 Mini (bed 130 x 140 mm, as reported by the
controller's $I). 1 SVG unit = 1 mm, origin at the top-left corner.

Layers by stroke colour: black heavy centre axes + C marks, red bed border, blue 1 cm grid, green corner-to-corner X,
magenta rings (1 cm steps) from the bed centre, clipped to the bed.
    python make_ray5_grid_svg.py"""
import os
from make_baseplate_ray5_svg import text_path   # text -> filled paths, no font needed

W, H = 130, 140
STEP = 10
RING_STEP = 10                                  # radius grows 1 cm per ring
OUT = "out/docs/ray5_grid_130x140.svg"


def clip(x0, y0, x1, y1):
    """Liang-Barsky clip of a segment to the bed rectangle."""
    t0, t1 = 0.0, 1.0
    dx, dy = x1 - x0, y1 - y0
    for p, q in ((-dx, x0), (dx, W - x0), (-dy, y0), (dy, H - y0)):
        if p == 0:
            if q < 0:
                return None
        else:
            t = q / p
            if p < 0:
                t0 = max(t0, t)
            else:
                t1 = min(t1, t)
    if t0 >= t1:
        return None
    return (x0 + t0 * dx, y0 + t0 * dy, x0 + t1 * dx, y0 + t1 * dy)


def ring_arcs(r):
    """Exact arcs of the ring (centre CX,CY) that lie on the bed, as an SVG path.
    Real geometry rather than a clipPath: LightBurn ignores clip-path and would
    import the full circle."""
    import math
    angs = []
    for x in (0, W):                                   # vertical bed edges
        d = (x - CX) / r
        if abs(d) <= 1:
            for sg in (1, -1):
                angs.append(math.atan2(sg * math.sqrt(1 - d * d) * r, x - CX))
    for y in (0, H):                                   # horizontal bed edges
        d = (y - CY) / r
        if abs(d) <= 1:
            for sg in (1, -1):
                angs.append(math.atan2(y - CY, sg * math.sqrt(1 - d * d) * r))
    inside = lambda a: 0 <= CX + r * math.cos(a) <= W and 0 <= CY + r * math.sin(a) <= H
    if not angs:
        return f"M {CX + r:.3f} {CY:.3f} A {r} {r} 0 1 1 {CX - r:.3f} {CY:.3f} A {r} {r} 0 1 1 {CX + r:.3f} {CY:.3f} "
    angs = sorted(a % (2 * math.pi) for a in angs)
    out = ""
    for i, a0 in enumerate(angs):
        a1 = angs[(i + 1) % len(angs)]
        if a1 <= a0:
            a1 += 2 * math.pi
        if inside((a0 + a1) / 2):
            large = 1 if a1 - a0 > math.pi else 0
            out += (f"M {CX + r * math.cos(a0):.3f} {CY + r * math.sin(a0):.3f} "
                    f"A {r} {r} 0 {large} 1 {CX + r * math.cos(a1):.3f} {CY + r * math.sin(a1):.3f} ")
    return out


def path(segs):
    return "".join(f"M {a:.3f} {b:.3f} L {c:.3f} {d:.3f} " for a, b, c, d in segs)


CX, CY = W / 2, H / 2
# grid lines every 1 cm counted out from the bed centre (not from the edges)
xs = [CX + k * STEP for k in range(-int(CX // STEP), int(CX // STEP) + 1)]
ys = [CY + k * STEP for k in range(-int(CY // STEP), int(CY // STEP) + 1)]
grid = [(x, 0, x, H) for x in xs if 0 < x < W] + [(0, y, W, y) for y in ys if 0 < y < H]
diag = [(0, 0, W, H), (W, 0, 0, H)]            # corner-to-corner "X"
CX, CY = W / 2, H / 2
corner = (CX ** 2 + CY ** 2) ** 0.5            # farthest bed point from the centre
radii = []
r = RING_STEP
while r < corner:                              # rings that still touch the bed
    radii.append(r)
    r += RING_STEP
# heavy centre axes, each broken around a "C" marker near the bed border
C_OFF, C_CAP, C_GAP = 6.0, 4.0, 4.0
axes = [(CX, 0, CX, C_OFF - C_GAP), (CX, C_OFF + C_GAP, CX, H),
        (0, CY, C_OFF - C_GAP, CY), (C_OFF + C_GAP, CY, W, CY)]
c_marks = [text_path("C", CX, -(C_OFF + C_CAP / 2), C_CAP),     # on the vertical axis, top
           text_path("C", C_OFF, -(CY + C_CAP / 2), C_CAP)]     # on the horizontal axis, left

s = [f'<?xml version="1.0" encoding="UTF-8"?>',
     f'<svg xmlns="http://www.w3.org/2000/svg" width="{W}mm" height="{H}mm" viewBox="0 0 {W} {H}">',
     '<title>Ray5 Mini 1 cm calibration grid</title>',
     f'<rect id="bed_border" x="0" y="0" width="{W}" height="{H}" fill="none" stroke="#ff0000" stroke-width="0.2"/>',
     f'<path id="grid_1cm" fill="none" stroke="#0000ff" stroke-width="0.2" d="{path(grid)}"/>',
     f'<path id="diagonals" fill="none" stroke="#00a000" stroke-width="0.2" d="{path(diag)}"/>',
     f'<path id="centre_axes" fill="none" stroke="#000000" stroke-width="0.8" d="{path(axes)}"/>',
     '<g id="rings_1cm" fill="none" stroke="#ff00ff" stroke-width="0.2">']
s += [f'<path d="{ring_arcs(r)}"/>' for r in radii]
s += ['</g>', '<g id="centre_marks" fill="#000000" stroke="none">']
s += [f'<path d="{d}"/>' for d in c_marks]
s += ['</g>', '</svg>']
os.makedirs(os.path.dirname(OUT), exist_ok=True)
open(OUT, "w", encoding="utf-8").write("\n".join(s))
print("wrote", OUT, f"grid {len(grid)} diag {len(diag)} rings {radii}")
