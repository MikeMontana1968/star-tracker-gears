#!/usr/bin/env python3
"""
Baseplate geometry — the single source for the PDF template and the DXF.

Kept in step with the bp_* parameters in star_tracker_gears.scad. The outline
is the convex hull of a lobe at each shaft centre, which decomposes exactly
into circular arcs joined by their common external tangents. That matters for
the DXF: a cutting service gets true ARC and CIRCLE entities instead of a few
hundred loose line segments.
"""
import math

# ---- parameters, mirroring star_tracker_gears.scad -----------------------
PLATE_T    = 4.3      # bp_t -- as ordered; nearest stock to 4.0
SHAFT_HOLE = 4.9      # bp_shaft_d, light press for a 20d nail
MOTOR_BOSS = 23.0     # bp_motor_boss, NEMA17 pilot registers here
MOTOR_BC   = 31.0     # bp_motor_bc, square pattern
TOWER_D    = 22.4     # bp_tower_d, bearing tower spigot
TOWER_BC   = 35.0     # bp_tower_bc, 3 x M3 -- Ø28 left only a 1.10 mm web
MOUNT_BC   = 50.0     # bp_mount_bc, wedge interface on the OUTPUT axis
MOUNT_D    = 5.5      # bp_mount_d, M5
MOUNT_N    = 4
M3         = 3.4

SHAFTS = [("S0", 0.0, 0.0, "motor"), ("S1", 67.5, 0.0, ""),
          ("S2", 67.5, 67.5, ""), ("S3", 0.0, 67.5, ""),
          ("S4", 0.0, -52.5, "output")]

LOBES = [((0.0, 0.0), 26.0), ((67.5, 0.0), 20.0), ((67.5, 67.5), 20.0),
         ((0.0, 67.5), 20.0), ((0.0, -52.5), 34.0)]


def holes():
    """Every cut circle as (cx, cy, diameter, tag)."""
    out = []
    out.append((0.0, 0.0, MOTOR_BOSS, "NEMA17 pilot boss"))
    r = MOTOR_BC / math.sqrt(2)
    for a in (45, 135, 225, 315):
        out.append((r*math.cos(math.radians(a)), r*math.sin(math.radians(a)),
                    M3, "motor M3"))
    for _lbl, x, y, _n in SHAFTS[1:4]:
        out.append((x, y, SHAFT_HOLE, "nail shaft"))
    sx, sy = 0.0, -52.5
    out.append((sx, sy, TOWER_D, "tower spigot"))
    for i in range(3):
        a = math.radians(i*120 + 60)
        out.append((sx + (TOWER_BC/2)*math.cos(a),
                    sy + (TOWER_BC/2)*math.sin(a), M3, "tower flange M3"))
    for i in range(MOUNT_N):
        a = math.radians(i*360/MOUNT_N + 45)
        out.append((sx + (MOUNT_BC/2)*math.cos(a),
                    sy + (MOUNT_BC/2)*math.sin(a), MOUNT_D, "wedge M5"))
    return out


# ---- hull -----------------------------------------------------------------
def _hull_points(pts):
    pts = sorted(set(pts))

    def half(seq):
        st = []
        for q in seq:
            while len(st) > 1:
                (ax, ay), (bx, by) = st[-2], st[-1]
                if (bx-ax)*(q[1]-ay) - (by-ay)*(q[0]-ax) <= 0:
                    st.pop()
                else:
                    break
            st.append(q)
        return st

    return half(pts)[:-1] + half(pts[::-1])[:-1]


def hull_polygon(lobes=None, n=720):
    """Dense closed polygon of the outline — used by the PDF template."""
    lobes = lobes or LOBES
    pts = []
    for (cx, cy), r in lobes:
        for k in range(n):
            a = 2*math.pi*k/n
            pts.append((cx + r*math.cos(a), cy + r*math.sin(a)))
    return _hull_points(pts)


def hull_lobe_order(lobes=None, n=720):
    """Which lobes reach the hull, in counter-clockwise order."""
    lobes = lobes or LOBES
    tagged = {}
    pts = []
    for i, ((cx, cy), r) in enumerate(lobes):
        for k in range(n):
            a = 2*math.pi*k/n
            p = (cx + r*math.cos(a), cy + r*math.sin(a))
            pts.append(p)
            tagged[p] = i
    hull = _hull_points(pts)
    area = sum(hull[i][0]*hull[(i+1) % len(hull)][1] -
               hull[(i+1) % len(hull)][0]*hull[i][1]
               for i in range(len(hull))) / 2
    if area < 0:                      # force counter-clockwise
        hull = hull[::-1]
    seq = []
    for p in hull:
        i = tagged[p]
        if not seq or seq[-1] != i:
            seq.append(i)
    if len(seq) > 1 and seq[0] == seq[-1]:
        seq.pop()
    return seq


def outer_tangent(c1, r1, c2, r2):
    """
    External tangent for a counter-clockwise traversal from circle 1 to 2.

    The tangent's outward unit normal n satisfies (c2-c1).n = r1-r2; of the two
    solutions, CCW traversal wants the one to the right of the travel
    direction, hence the minus branch.
    """
    dx, dy = c2[0]-c1[0], c2[1]-c1[1]
    L = math.hypot(dx, dy)
    a = math.atan2(dy, dx)
    k = (r1 - r2) / L
    k = max(-1.0, min(1.0, k))
    psi = a - math.acos(k)
    n = (math.cos(psi), math.sin(psi))
    return psi, (c1[0] + r1*n[0], c1[1] + r1*n[1]), \
                (c2[0] + r2*n[0], c2[1] + r2*n[1])


def outline_entities(lobes=None):
    """
    Exact outline as an ordered list of ('arc', c, r, a0, a1) and
    ('line', p0, p1), counter-clockwise and closed.
    """
    lobes = lobes or LOBES
    order = hull_lobe_order(lobes)
    m = len(order)
    tang = []
    for k in range(m):
        i, j = order[k], order[(k+1) % m]
        psi, p1, p2 = outer_tangent(lobes[i][0], lobes[i][1],
                                    lobes[j][0], lobes[j][1])
        tang.append((psi, p1, p2))

    ents = []
    for k in range(m):
        i = order[k]
        psi_in = tang[(k-1) % m][0]     # arrives on circle i at this angle
        psi_out = tang[k][0]            # leaves circle i at this angle
        c, r = lobes[i]
        ents.append(('arc', c, r, math.degrees(psi_in) % 360,
                     math.degrees(psi_out) % 360))
        ents.append(('line', tang[k][1], tang[k][2]))
    return ents


def bbox(lobes=None):
    poly = hull_polygon(lobes)
    xs = [p[0] for p in poly]
    ys = [p[1] for p in poly]
    return min(xs), min(ys), max(xs), max(ys)


if __name__ == "__main__":
    order = hull_lobe_order()
    print("lobes on the hull (CCW):", order)
    off = [i for i in range(len(LOBES)) if i not in order]
    if off:
        print("lobes swallowed by the hull:", off,
              "-> their radius does not set the outline")
    x0, y0, x1, y1 = bbox()
    print(f"bbox {x1-x0:.1f} x {y1-y0:.1f} mm")
    print(f"holes: {len(holes())}")
