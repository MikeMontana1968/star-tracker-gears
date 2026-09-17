#!/usr/bin/env python3
"""
Write the baseplate as a DXF for a cutting service (SendCutSend etc.).

Why not just export DXF from OpenSCAD: its DXF writer emits the whole profile
as loose LINE segments — 708 of them for this part, with every hole a faceted
polygon and not a single CIRCLE. Services reject or mis-cut that.

This writes R12 DXF with exact geometry instead:
  * every hole as a true CIRCLE entity
  * the outline as circular ARCs joined by their exact common tangents

Units are millimetres. R12 has no reliable units record, so **tell the vendor
mm on upload** — the file self-checks to 121.5 x 174.0 mm, which is the sanity
check if anything ever reads it as inches.

    python make_baseplate_dxf.py
"""
import math
import baseplate_geom as G

OUT = "out/docs/baseplate_cut.dxf"


def _g(code, value):
    return f"{code}\n{value}\n"


def _num(v):
    return f"{v:.6f}"


def dxf_document(entities):
    s = ""
    s += _g(0, "SECTION") + _g(2, "HEADER")
    s += _g(9, "$INSUNITS") + _g(70, 4)          # 4 = millimetres
    s += _g(9, "$MEASUREMENT") + _g(70, 1)       # 1 = metric
    s += _g(0, "ENDSEC")
    s += _g(0, "SECTION") + _g(2, "ENTITIES")
    s += entities
    s += _g(0, "ENDSEC")
    s += _g(0, "EOF")
    return s


def e_line(p0, p1, layer="0"):
    return (_g(0, "LINE") + _g(8, layer)
            + _g(10, _num(p0[0])) + _g(20, _num(p0[1])) + _g(30, "0.0")
            + _g(11, _num(p1[0])) + _g(21, _num(p1[1])) + _g(31, "0.0"))


def e_arc(c, r, a0, a1, layer="0"):
    return (_g(0, "ARC") + _g(8, layer)
            + _g(10, _num(c[0])) + _g(20, _num(c[1])) + _g(30, "0.0")
            + _g(40, _num(r)) + _g(50, _num(a0)) + _g(51, _num(a1)))


def e_circle(c, r, layer="0"):
    return (_g(0, "CIRCLE") + _g(8, layer)
            + _g(10, _num(c[0])) + _g(20, _num(c[1])) + _g(30, "0.0")
            + _g(40, _num(r)))


def build():
    body = ""
    ents = G.outline_entities()
    for e in ents:
        if e[0] == 'arc':
            _, c, r, a0, a1 = e
            body += e_arc(c, r, a0, a1)
        else:
            _, p0, p1 = e
            body += e_line(p0, p1)
    for cx, cy, d, _tag in G.holes():
        body += e_circle((cx, cy), d/2.0)
    return dxf_document(body), ents


# ---- verification ---------------------------------------------------------
def arc_point(c, r, adeg):
    a = math.radians(adeg)
    return (c[0] + r*math.cos(a), c[1] + r*math.sin(a))


def verify(ents):
    """Walk the outline and confirm it is continuous and closed."""
    pts = []
    for e in ents:
        if e[0] == 'arc':
            _, c, r, a0, a1 = e
            pts.append((arc_point(c, r, a0), arc_point(c, r, a1)))
        else:
            pts.append((e[1], e[2]))
    worst = 0.0
    for i in range(len(pts)):
        end = pts[i][1]
        nxt = pts[(i+1) % len(pts)][0]
        worst = max(worst, math.hypot(end[0]-nxt[0], end[1]-nxt[1]))
    return worst


def edge_margin():
    """Smallest gap between any hole edge and the plate outline."""
    poly = G.hull_polygon(n=1440)
    worst = (1e9, None)
    for cx, cy, d, tag in G.holes():
        best = 1e9
        for i in range(len(poly)):
            ax, ay = poly[i]
            bx, by = poly[(i+1) % len(poly)]
            vx, vy = bx-ax, by-ay
            L2 = vx*vx + vy*vy
            t = 0.0 if L2 == 0 else max(0.0, min(1.0, ((cx-ax)*vx + (cy-ay)*vy)/L2))
            px, py = ax + t*vx, ay + t*vy
            best = min(best, math.hypot(cx-px, cy-py) - d/2.0)
        if best < worst[0]:
            worst = (best, tag)
    return worst


def hole_spacing():
    hs = G.holes()
    worst = (1e9, None, None)
    for i in range(len(hs)):
        for j in range(i+1, len(hs)):
            xi, yi, di, ti = hs[i]
            xj, yj, dj, tj = hs[j]
            gap = math.hypot(xi-xj, yi-yj) - di/2 - dj/2
            if gap < worst[0]:
                worst = (gap, ti, tj)
    return worst


def main():
    doc, ents = build()
    with open(OUT, "w", encoding="ascii", newline="\r\n") as f:
        f.write(doc)

    n_arc = sum(1 for e in ents if e[0] == 'arc')
    n_line = sum(1 for e in ents if e[0] == 'line')
    x0, y0, x1, y1 = G.bbox()
    gap = verify(ents)
    marg, marg_tag = edge_margin()
    sp, ta, tb = hole_spacing()

    print(f"wrote {OUT}")
    print(f"  outline    {n_arc} ARC + {n_line} LINE   (OpenSCAD would emit 708 LINE)")
    print(f"  holes      {len(G.holes())} CIRCLE")
    print(f"  extents    {x1-x0:.2f} x {y1-y0:.2f} mm")
    print(f"  continuity worst joint gap {gap*1000:.4f} um  "
          f"{'OK' if gap < 1e-6 else 'BROKEN'}")
    print(f"  edge margin {marg:.2f} mm  (tightest: {marg_tag})")
    print(f"  hole gap    {sp:.2f} mm  (tightest: {ta} <-> {tb})")


if __name__ == "__main__":
    main()
