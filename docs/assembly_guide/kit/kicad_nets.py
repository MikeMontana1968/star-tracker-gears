"""Draw the nets of a label-connected KiCad schematic.

A schematic whose pins end in a short stub plus a net label (no drawn wires
between parts) exports as a scatter of isolated symbols: the connections exist
only as label names. This reads every label / global_label / wire-stub end from
the .kicad_sch, groups the anchor points by net name, and returns an SVG <g>
(sheet mm = exported SVG user units) joining each net's points with orthogonal
Z-routes along a minimum spanning tree. Signal nets get one colour each and
solid lines; rails with many points (GND, +5V ...) are drawn thin, dashed and
faint so they show membership without burying the signals.

    overlay(path_to_kicad_sch)  ->  '<g class="nets">...</g>'
Insert it *before* the exported drawing so symbols and text stay on top.
"""
import re
import zlib
from collections import defaultdict

PALETTE = ['#d62728', '#1f77b4', '#2ca02c', '#ff7f0e', '#9467bd', '#8c564b', '#e377c2',
           '#17becf', '#bcbd22', '#0b6e4f', '#b5179e', '#c77800', '#3a86ff', '#e63946']
RAIL_MIN = 7           # nets with at least this many points are "rails"


def _nets(text):
    d = defaultdict(list)
    for m in re.finditer(r'\((?:global_)?label\s+"([^"]+)"\s*\(at\s+([-\d.]+)\s+([-\d.]+)', text):
        d[m.group(1)].append((float(m.group(2)), float(m.group(3))))
    return d


def _mst(pts):
    inside, edges = [pts[0]], []
    rest = pts[1:]
    while rest:
        best = min(((abs(a[0] - b[0]) + abs(a[1] - b[1]), a, b) for a in inside for b in rest))
        edges.append((best[1], best[2]))
        inside.append(best[2]); rest.remove(best[2])
    return edges


def _route(a, b, bend):
    (x1, y1), (x2, y2) = a, b
    if abs(x1 - x2) < 0.01 or abs(y1 - y2) < 0.01:
        return 'M%.2f %.2fL%.2f %.2f' % (x1, y1, x2, y2)
    mx = x1 + (x2 - x1) * bend
    return 'M%.2f %.2fL%.2f %.2fL%.2f %.2fL%.2f %.2f' % (x1, y1, mx, y1, mx, y2, x2, y2)


def overlay(sch_path):
    with open(sch_path, encoding='utf8') as fh:
        nets = _nets(fh.read())
    sig = [n for n, p in nets.items() if len(p) < RAIL_MIN]
    colour = {n: PALETTE[i % len(PALETTE)] for i, n in enumerate(sorted(sig))}
    out = ['<g class="nets" fill="none" stroke-linecap="round" stroke-linejoin="round">']
    for n, pts in sorted(nets.items(), key=lambda kv: -len(kv[1])):     # rails first, under signals
        pts = sorted(set(pts))
        if len(pts) < 2:
            continue
        rail = n not in colour
        d = ''.join(_route(a, b, 0.5 if rail else 0.35 + 0.3 * ((zlib.crc32(n.encode()) % 7) / 6.0)) for a, b in _mst(pts))
        if rail:
            out.append('<path d="%s" stroke="#7a7a7a" stroke-width="0.18" stroke-dasharray="1.2 0.9" opacity="0.55"/>' % d)
        else:
            out.append('<path d="%s" stroke="%s" stroke-width="0.32"/>' % (d, colour[n]))
    out.append('</g>')
    return ''.join(out)
