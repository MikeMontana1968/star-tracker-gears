"""Connected components of one net's copper, from the zone fills saved in the
.kicad_pcb: fill polygons + tracks per layer, joined across layers by vias and
through-hole pads. Prints every component and the pads it holds, so an island
(pads that touch each other but not the main plane) stands out.

    python gndislands.py board.kicad_pcb [NET]
"""
import math, re, sys
from shapely.geometry import Polygon, Point, LineString, box
from shapely.affinity import rotate as srot, translate as strans
from shapely.ops import unary_union

src = open(sys.argv[1], encoding='utf-8').read()
NET = sys.argv[2] if len(sys.argv) > 2 else 'GND'
TAG = '(net "%s")' % NET

def blocks(s, head):
    pat = re.compile(r'\(' + head + r'[\s"]')
    i = 0
    while True:
        m = pat.search(s, i)
        if not m:
            return
        i = m.start(); d = 0; j = i
        while True:
            c = s[j]
            if c == '(':
                d += 1
            elif c == ')':
                d -= 1
                if d == 0:
                    break
            elif c == '"':
                j = s.index('"', j + 1)
            j += 1
        yield s[i:j + 1]
        i = j + 1

# ---- copper per layer ----------------------------------------------------
copper = {'F.Cu': [], 'B.Cu': []}
for z in blocks(src, 'zone'):
    if TAG not in z[:400]:
        continue
    for fp in blocks(z, 'filled_polygon'):
        layer = re.search(r'\(layer "([^"]+)"\)', fp).group(1)
        pts = [(float(a), float(b)) for a, b in re.findall(r'\(xy ([-\d.]+) ([-\d.]+)\)', fp)]
        copper[layer].append(Polygon(pts).buffer(0))
for t in blocks(src, 'segment'):
    if TAG in t:
        s = re.search(r'\(start ([-\d.]+) ([-\d.]+)\)', t); e = re.search(r'\(end ([-\d.]+) ([-\d.]+)\)', t)
        w = float(re.search(r'\(width ([-\d.]+)\)', t).group(1))
        layer = re.search(r'\(layer "([^"]+)"\)', t).group(1)
        copper[layer].append(LineString([(float(s.group(1)), float(s.group(2))),
                                         (float(e.group(1)), float(e.group(2)))]).buffer(w/2, cap_style=1))
links = []      # (shape, label) present on BOTH layers: vias and through-hole pads
for v in blocks(src, 'via'):
    if TAG in v:
        m = re.search(r'\(at ([-\d.]+) ([-\d.]+)\)', v); sz = float(re.search(r'\(size ([-\d.]+)', v).group(1))
        links.append((Point(float(m.group(1)), float(m.group(2))).buffer(sz/2), 'via'))

pads = []
for fp in blocks(src, 'footprint'):
    ref = re.search(r'\(property "Reference" "([^"]+)"', fp).group(1)
    at = re.search(r'\(at ([-\d.]+) ([-\d.]+)(?: ([-\d.]+))?\)', fp)
    fx, fy, fr = float(at.group(1)), float(at.group(2)), float(at.group(3) or 0)
    for p in blocks(fp, 'pad'):
        if TAG not in p:
            continue
        num = re.match(r'\(pad "([^"]*)"', p).group(1)
        shape = re.match(r'\(pad "[^"]*" \S+ (\S+)', p).group(1)
        m = re.search(r'\(at ([-\d.]+) ([-\d.]+)(?: ([-\d.]+))?\)', p)
        px, py, pr = float(m.group(1)), float(m.group(2)), float(m.group(3) or 0)
        sx, sy = [float(v) for v in re.search(r'\(size ([-\d.]+) ([-\d.]+)\)', p).groups()]
        g = Point(0, 0).buffer(0.5).buffer(0) if False else None
        if shape in ('circle',):
            g = Point(0, 0).buffer(sx/2)
        elif shape in ('oval', 'roundrect'):
            r = min(sx, sy)/2
            g = box(-sx/2 + r, -sy/2 + r, sx/2 - r, sy/2 - r).buffer(r) if shape == 'oval' else box(-sx/2, -sy/2, sx/2, sy/2)
        else:
            g = box(-sx/2, -sy/2, sx/2, sy/2)
        g = srot(g, -pr, origin=(0, 0))
        a = math.radians(-fr)
        cx = fx + px*math.cos(a) - py*math.sin(a); cy = fy + px*math.sin(a) + py*math.cos(a)
        g = strans(srot(g, -fr, origin=(0, 0)), cx, cy)
        th = ' thru_hole ' in p
        pads.append(('%s.%s' % (ref, num), g, th))
        if th:
            links.append((g, '%s.%s' % (ref, num)))
        else:
            layer = 'F.Cu' if '"F.Cu"' in p else 'B.Cu'
            copper[layer].append(g)

# ---- components: union per layer, then join through links ---------------
parts = []          # (layer, polygon)
for layer, gs in copper.items():
    u = unary_union(gs)
    for poly in getattr(u, 'geoms', [u]):
        parts.append((layer, poly))
for g, lab in links:                     # pads/vias exist on both layers: add as copper too
    parts.append(('F.Cu', g)); parts.append(('B.Cu', g))
n = len(parts)
parent = list(range(n))
def find(a):
    while parent[a] != a:
        parent[a] = parent[parent[a]]; a = parent[a]
    return a
def join(a, b):
    parent[find(a)] = find(b)
# same-layer overlap
for i in range(n):
    for j in range(i + 1, n):
        if parts[i][0] == parts[j][0] and parts[i][1].intersects(parts[j][1]):
            join(i, j)
# a link's F and B copies are the same metal
k = len(parts) - 2*len(links)
for t in range(len(links)):
    join(k + 2*t, k + 2*t + 1)

comp = {}
for name, g, th in pads:
    hits = {find(i) for i, (layer, poly) in enumerate(parts) if poly.intersects(g)}
    for h in hits:
        comp.setdefault(h, set()).add(name)
    if not hits:
        comp.setdefault('none:' + name, set()).add(name)
groups = {}
for name, g, th in pads:
    roots = frozenset(r for r, members in comp.items() if name in members)
    groups.setdefault(roots, []).append(name)
print('%s net: %d pads, %d connected piece(s)' % (NET, len(pads), len(groups)))
for i, (roots, members) in enumerate(sorted(groups.items(), key=lambda kv: -len(kv[1]))):
    print('  piece %d: %d pads: %s' % (i + 1, len(members), ' '.join(sorted(members))))
