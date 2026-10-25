#!/usr/bin/env python3
"""
Laser-etch SVGs for an aluminium sidereal dial on the final gear, in arc
segments small enough for the laser bed (a half ring would not fit).

The output wheel turns once per SIDEREAL day (86164.09 s), so 360 deg reads
24 sidereal hours of hour angle: 1 h = 15 deg = 59 m 50.17 s civil, 1 min =
0.25 deg. Graduations: every minute, longer every 5, longer again every 15
(one 15-minute step = exactly one tooth of the 96T), hour lines + numerals.

The band is 1" wide by default, its outer edge at r 92.5: 1.0 inside the
wheel's 93.5 root and 1.5 inside the closest the 24T pinion's tips come (r 94
from the wheel centre; the pinion is 7 wide against the wheel's 5 and stands
0.5 proud of the top face). The wheel is only solid from r 79.87 out -- inside
that the six lightening holes open -- so the inner part of a 1" band bridges
them. The epoxy lands on the 12.6 mm solid rim and the spokes.

Seams fall at hh:37.5, half-way between the :35 and :40 ticks, so no numeral
or major tick is ever cut in two.

Default direction: numerals increase COUNTER-CLOCKWISE seen from above (the
camera end). The platform turns clockwise from there (guide step 56), so the
numbers come up in order past a fixed pointer. --cw flips it.

    python make_dial_arcs.py                 # quarters and sixths
    python make_dial_arcs.py --cw --width 20

Writes out/docs/dial_arcs/: one SVG per segment, plus a preview of each set
laid on the wheel. In each segment SVG: black fill = etch (text is already
paths, no fonts needed), red hairline = the piece outline.
"""
import argparse
import math
import os

from matplotlib.font_manager import FontProperties
from matplotlib.path import Path
from matplotlib.textpath import TextPath

OUT = 'out/docs/dial_arcs'

# ---- model, kept in step with star_tracker_gears.scad / make_dial_pdf.py ---
ZWF, MOD_F = 96, 2.0
R_ROOT = MOD_F * ZWF / 2.0 - MOD_F * 1.25        # 93.5
R_TIP = MOD_F * ZWF / 2.0 + MOD_F                # 98
LH_R, LH_D, LH_N = 56.35, 46.97, 6               # lightening holes, as make_dial_pdf derives them
BC_R, BC_D, BC_N = 18.0, 3.2, 6
HUB_D, BORE = 44.0, 22.0
PINION_CD, PINION_TIP = 120.0, 26.0              # 24T m2: centre distance, tip radius

FONT = FontProperties(family='DejaVu Sans', weight='bold')
MARGIN = 2.0                                     # mm round each piece in the SVG

# tick: (every n minutes, length mm, width mm) -- the first match wins
TICKS = [(60, 8.0, 0.80), (15, 5.5, 0.45), (5, 4.0, 0.30), (1, 2.2, 0.15)]


def f(v):
    return ('%.3f' % v).rstrip('0').rstrip('.')


class Piece:
    """One segment, drawn in wheel coordinates (mm, y up, centre at 0,0)."""

    def __init__(self, a):
        self.a = a
        self.etch = []         # SVG path 'd' strings, wheel coords
        self.text = []         # (TextPath, x, y, rot_deg) -- placed later

    def ang(self, h, hm):
        """Wheel angle of hour h in a piece centred on hour hm (mid at the top)."""
        s = 1.0 if not self.a.cw else -1.0
        return 90.0 + s * (h - hm) * 15.0

    def radial_bar(self, phi, r0, r1, w):
        a = math.radians(phi)
        ux, uy = math.cos(a), math.sin(a)
        nx, ny = -uy * w / 2, ux * w / 2
        p = [(r0 * ux + nx, r0 * uy + ny), (r1 * ux + nx, r1 * uy + ny),
             (r1 * ux - nx, r1 * uy - ny), (r0 * ux - nx, r0 * uy - ny)]
        self.etch.append('M' + ' L'.join('%s,%s' % (f(x), f(y)) for x, y in p) + ' Z')

    def ring(self, r, w, phi0, phi1):
        """A thin annular strip r +- w/2 between two angles."""
        lo, hi = min(phi0, phi1), max(phi0, phi1)
        ro, ri = r + w / 2, r - w / 2
        self.etch.append(arc_band_d(ro, ri, lo, hi))

    def label(self, s, size, r, phi):
        tp = TextPath((0, 0), s, size=size, prop=FONT)
        bb = tp.get_extents()
        self.text.append((tp, bb, r, phi))


def arc_band_d(ro, ri, lo, hi):
    def pt(r, d):
        return f(r * math.cos(math.radians(d))), f(r * math.sin(math.radians(d)))
    big = 1 if hi - lo > 180 else 0
    (x0, y0), (x1, y1) = pt(ro, lo), pt(ro, hi)
    (x2, y2), (x3, y3) = pt(ri, hi), pt(ri, lo)
    # drawn in wheel coords (y up) under the outer flip; sweep 1 = increasing angle there
    return ('M%s,%s A%s,%s 0 %d 1 %s,%s L%s,%s A%s,%s 0 %d 0 %s,%s Z'
            % (x0, y0, f(ro), f(ro), big, x1, y1, x2, y2, f(ri), f(ri), big, x3, y3))


def textpath_d(tp):
    out = []
    for seg, code in tp.iter_segments(curves=True, simplify=False):
        v = seg.reshape(-1, 2)
        if code == Path.MOVETO:
            out.append('M%s,%s' % (f(v[0][0]), f(v[0][1])))
        elif code == Path.LINETO:
            out.append('L%s,%s' % (f(v[0][0]), f(v[0][1])))
        elif code == Path.CURVE3:
            out.append('Q%s,%s %s,%s' % (f(v[0][0]), f(v[0][1]), f(v[1][0]), f(v[1][1])))
        elif code == Path.CURVE4:
            out.append('C%s,%s %s,%s %s,%s' % tuple(f(c) for c in v.flatten()))
        elif code == Path.CLOSEPOLY:
            out.append('Z')
    return ' '.join(out)


def build_piece(a, k, n):
    """Segment k of n. Seams at hh:37.5 so nothing important is cut."""
    span = 24.0 / n
    h0 = k * span - 0.375
    h1 = h0 + span
    hm = (h0 + h1) / 2.0
    p = Piece(a)
    ro, ri = a.r_out, a.r_out - a.width
    edge = ro - 0.6                       # keep the burn off the very edge

    m0 = math.ceil(h0 * 60 - 1e-9)
    m1 = math.floor(h1 * 60 + 1e-9)
    for m in range(m0, m1 + 1):
        if not (h0 * 60 < m < h1 * 60):
            continue
        L, w = next((L, w) for n_, L, w in TICKS if m % n_ == 0)
        p.radial_bar(p.ang(m / 60.0, hm), edge - L, edge, w)

    # hour numerals, tangential, upright at the top of the dial
    hs = a.hour_size
    r_hour = edge - TICKS[0][1] - 1.6 - hs / 2
    for h in range(math.ceil(h0), math.floor(h1) + 1):
        if h0 < h < h1:
            p.label('%02d' % (h % 24), hs, r_hour, p.ang(h, hm))
    # :15 :30 :45 under their ticks
    ms = 2.4
    r_min = edge - TICKS[1][1] - 0.9 - ms / 2
    for q in range(math.ceil(h0 * 4), math.floor(h1 * 4) + 1):
        if q % 4 and h0 < q / 4 < h1:
            p.label('%02d' % (q % 4 * 15), ms, r_min, p.ang(q / 4.0, hm))

    # inner rule, and the legend in the middle of the piece
    phiA, phiB = p.ang(h0, hm), p.ang(h1, hm)
    p.ring(ri + 1.6, 0.35, phiA, phiB)
    legend_r = (ri + 1.6 + r_hour - hs / 2) / 2
    if r_hour - hs / 2 - (ri + 1.6) > 4.5:
        p.label('SIDEREAL  %02d–%02d h' % (round(hm - span / 2 + 0.375) % 24,
                                               round(hm + span / 2 + 0.375) % 24),
                2.6, legend_r, 90.0)
    p.outline = arc_band_d(ro, ri, min(phiA, phiB), max(phiA, phiB))
    p.span = (min(phiA, phiB), max(phiA, phiB))
    p.hm = hm
    return p


def text_svg(tp, bb, r, phi, fill):
    """A TextPath centred on (r, phi), turned tangentially, in the y-flipped frame."""
    cx, cy = (bb.x0 + bb.x1) / 2, (bb.y0 + bb.y1) / 2
    a = math.radians(phi)
    x, y = r * math.cos(a), r * math.sin(a)
    # Final frame flips y, so the text is drawn in wheel coords: rotate by phi-90
    # and translate. The overall scale(1,-1) happens on the outer group.
    return ('<path fill="%s" transform="translate(%s,%s) rotate(%s) translate(%s,%s)" d="%s"/>'
            % (fill, f(x), f(y), f(phi - 90.0), f(-cx), f(-cy), textpath_d(tp)))


def bbox(p):
    lo, hi = p.span
    angs = [lo, hi] + [d for d in range(-360, 721, 90) if lo < d < hi]
    rs = (p.a.r_out, p.a.r_out - p.a.width)
    xs = [r * math.cos(math.radians(d)) for r in rs for d in angs]
    ys = [r * math.sin(math.radians(d)) for r in rs for d in angs]
    return min(xs), min(ys), max(xs), max(ys)


def write_piece(p, path, title):
    x0, y0, x1, y1 = bbox(p)
    W, H = x1 - x0 + 2 * MARGIN, y1 - y0 + 2 * MARGIN
    g = ['<g transform="translate(%s,%s) scale(1,-1)">' % (f(MARGIN - x0), f(MARGIN + y1))]
    g.append('<g id="etch" fill="#000">')
    g += ['<path d="%s"/>' % d for d in p.etch]
    g += [text_svg(tp, bb, r, phi, '#000') for tp, bb, r, phi in p.text]
    g.append('</g>')
    g.append('<path id="outline" fill="none" stroke="#ff0000" stroke-width="0.1" d="%s"/>' % p.outline)
    g.append('</g>')
    svg = ('<?xml version="1.0" encoding="UTF-8"?>\n'
           '<svg xmlns="http://www.w3.org/2000/svg" width="%smm" height="%smm" viewBox="0 0 %s %s">\n'
           '<title>%s</title>\n%s\n</svg>\n' % (f(W), f(H), f(W), f(H), title, '\n'.join(g)))
    with open(path, 'w', encoding='utf-8') as fh:
        fh.write(svg)
    return W, H


def write_preview(pieces, n, path, a):
    """The set laid on the wheel at true size, rotated to its real position."""
    R = R_TIP + 4
    s = ['<?xml version="1.0" encoding="UTF-8"?>',
         '<svg xmlns="http://www.w3.org/2000/svg" width="%smm" height="%smm" viewBox="%s %s %s %s">'
         % (f(2 * R), f(2 * R), f(-R), f(-R), f(2 * R), f(2 * R)),
         '<rect x="%s" y="%s" width="%s" height="%s" fill="#fff"/>' % (f(-R), f(-R), f(2 * R), f(2 * R)),
         '<g transform="scale(1,-1)">',
         '<circle r="%s" fill="#e9c9a8" stroke="#b0703a" stroke-width="0.3"/>' % f(R_ROOT),
         '<circle r="%s" fill="none" stroke="#b0703a" stroke-width="0.3" stroke-dasharray="1.5 1"/>' % f(R_TIP)]
    for i in range(LH_N):
        d = math.radians(30 + i * 60)
        s.append('<circle cx="%s" cy="%s" r="%s" fill="#fff" stroke="#b0703a" stroke-width="0.3"/>'
                 % (f(LH_R * math.cos(d)), f(LH_R * math.sin(d)), f(LH_D / 2)))
    s.append('<circle r="%s" fill="#9fb9d6" stroke="#2f6fa8" stroke-width="0.3"/>' % f(HUB_D / 2))
    # the 24T pinion, at an arbitrary bearing (only the radial clearance matters)
    s.append('<circle cx="%s" cy="0" r="%s" fill="#c9d3e8" stroke="#1c4587" stroke-width="0.3"/>'
             % (f(PINION_CD), f(PINION_TIP)))
    for k, p in enumerate(pieces):
        # piece k is drawn with its middle at the top; rotate it to where hm lives
        rot = p.hm * 15.0 * (-1 if a.cw else 1)
        s.append('<g transform="rotate(%s)">' % f(rot))
        s.append('<path d="%s" fill="#d8dadc" fill-opacity="0.92" stroke="#c00" stroke-width="0.25"/>' % p.outline)
        s.append('<g fill="#111">' + ''.join('<path d="%s"/>' % d for d in p.etch)
                 + ''.join(text_svg(tp, bb, r, phi, '#111') for tp, bb, r, phi in p.text) + '</g>')
        s.append('</g>')
    s.append('</g>')
    s.append('<text x="%s" y="%s" font-family="sans-serif" font-size="4" fill="#555">%d segments, '
             'numerals %s seen from above; grey = aluminium, tan = wheel, blue = hub, 24T pinion right</text>'
             % (f(-R + 3), f(R - 3), n, 'CW' if a.cw else 'CCW'))
    s.append('</svg>')
    with open(path, 'w', encoding='utf-8') as fh:
        fh.write('\n'.join(s) + '\n')


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--width', type=float, default=25.4, help='band width, mm (1" default)')
    ap.add_argument('--r-out', type=float, default=92.5, help='outer radius, mm (wheel root is 93.5)')
    ap.add_argument('--hour-size', type=float, default=6.0, help='hour numeral height, mm')
    ap.add_argument('--cw', action='store_true', help='numerals increase clockwise seen from above')
    ap.add_argument('--sets', default='4,6', help='segment counts to write, e.g. 4,6 or 8')
    ap.add_argument('-o', '--out', default=OUT)
    a = ap.parse_args()
    if a.r_out > R_ROOT - 0.5:
        raise SystemExit('--r-out %.1f runs into the teeth (root %.1f)' % (a.r_out, R_ROOT))
    os.makedirs(a.out, exist_ok=True)

    for n in (int(x) for x in a.sets.split(',')):
        name = {4: 'quarter', 6: 'sixth'}.get(n, '1of%d' % n)
        pieces = [build_piece(a, k, n) for k in range(n)]
        for k, p in enumerate(pieces):
            lo = round(k * 24.0 / n) % 24
            hi = round((k + 1) * 24.0 / n) % 24
            fn = os.path.join(a.out, 'dial_%s_%d_of_%d_%02d-%02dh.svg' % (name, k + 1, n, lo, hi))
            W, H = write_piece(p, fn, 'Sidereal dial %d of %d (%02d-%02d h)' % (k + 1, n, lo, hi))
        write_preview(pieces, n, os.path.join(a.out, 'preview_%s.svg' % name), a)
        print('%-8s x%d  each %.1f x %.1f mm (incl. %g mm margin)  -> %s'
              % (name, n, W, H, MARGIN, a.out))
    print('band r %.1f..%.1f (%.1f mm); pinion tips clear by %.1f mm; solid wheel rim from r %.2f'
          % (a.r_out - a.width, a.r_out, a.width, PINION_CD - PINION_TIP - a.r_out, LH_R + LH_D / 2))


if __name__ == '__main__':
    main()
