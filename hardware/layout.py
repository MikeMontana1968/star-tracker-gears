"""Courtyard extents, an auto-packer for the regions, and a collision check."""
import kicadlib as K
import place as PL

_bb = {}


def raw_bbox(fp_id):
    """Courtyard extent in footprint-local coords, as (x0, y0, x1, y1)."""
    if fp_id in _bb:
        return _bb[fp_id]
    fp = K.load_footprint(fp_id)
    xs, ys = [], []

    def add(x, y):
        xs.append(x)
        ys.append(y)

    for name in ("fp_line", "fp_rect", "fp_poly", "fp_arc", "fp_circle"):
        for e in K.kids(fp, name):
            lay = K.kid(e, "layer")
            if not lay or "CrtYd" not in str(lay[1]):
                continue
            if name == "fp_circle":
                c, en = K.kid(e, "center"), K.kid(e, "end")
                if c and en:
                    cx, cy = float(c[1]), float(c[2])
                    r = ((float(en[1]) - cx) ** 2 + (float(en[2]) - cy) ** 2) ** 0.5
                    add(cx - r, cy - r)
                    add(cx + r, cy + r)
                continue
            for key in ("start", "end", "center", "mid"):
                k = K.kid(e, key)
                if k:
                    add(float(k[1]), float(k[2]))
            pts = K.kid(e, "pts")
            if pts:
                for p in K.kids(pts, "xy"):
                    add(float(p[1]), float(p[2]))
    if not xs:
        for _n, x, y in K.footprint_pads(fp):
            add(x - 1.2, y - 1.2)
            add(x + 1.2, y + 1.2)
    _bb[fp_id] = (min(xs), min(ys), max(xs), max(ys))
    return _bb[fp_id]


def bbox(fp_id, rot):
    """Courtyard extent after rotation, as offsets from the part origin."""
    x0, y0, x1, y1 = raw_bbox(fp_id)
    pts = [K.rotate(x, y, rot) for x, y in
           ((x0, y0), (x1, y0), (x1, y1), (x0, y1))]
    return (min(p[0] for p in pts), min(p[1] for p in pts),
            max(p[0] for p in pts), max(p[1] for p in pts))


def autoplace(parts):
    """Apply FIXED positions, then shelf-pack each region. Returns warnings."""
    by_ref = {p["ref"]: p for p in parts}
    warn = []

    for ref, pos in PL.FIXED.items():
        if ref in by_ref:
            by_ref[ref]["pcb"] = pos
        else:
            warn.append("FIXED names unknown ref %s" % ref)

    placed = set(PL.FIXED)
    for name, x0, y0, x1, y1, refs in PL.REGIONS:
        def _hw(entry):
            ref, prot = entry if isinstance(entry, tuple) else (entry, 0)
            if ref not in by_ref:
                return (0.0, 0.0)
            b = bbox(by_ref[ref]["fp"], prot)
            return (b[2] - b[0], b[3] - b[1])

        # First-fit decreasing packing.  A tall narrow region packs into
        # vertical columns (widest first); a wide short one into horizontal
        # shelves (tallest first).  Using the wrong one wastes ~40% of a region.
        use_cols = (y1 - y0) > (x1 - x0)
        refs = sorted(refs, key=lambda e: _hw(e)[0 if use_cols else 1],
                      reverse=True)
        cur, line, thick = (y0, x0, 0.0) if use_cols else (x0, y0, 0.0)
        for entry in refs:
            ref, prot = entry if isinstance(entry, tuple) else (entry, 0)
            if ref not in by_ref:
                warn.append("region %s names unknown ref %s" % (name, ref))
                continue
            p = by_ref[ref]
            bx0, by0, bx1, by1 = bbox(p["fp"], prot)
            w, h = bx1 - bx0, by1 - by0
            adv, cross = (h, w) if use_cols else (w, h)
            lim = y1 if use_cols else x1
            start_at = y0 if use_cols else x0
            if cur + adv > lim + 1e-9 and cur > start_at:
                line += thick + PL.GAP
                cur, thick = start_at, 0.0
            px, py = (line, cur) if use_cols else (cur, line)
            if px + w > x1 + 1e-9 or py + h > y1 + 1e-9:
                warn.append("region %s overflowed at %s" % (name, ref))
            p["pcb"] = (px - bx0, py - by0, prot)
            cur += adv + PL.GAP
            thick = max(thick, cross)
            placed.add(ref)

    for p in parts:
        if p["fp"] and p["ref"] not in placed:
            warn.append("unplaced: %s" % p["ref"])
    return warn


def check(parts, board_w, board_h, clear=0.2):
    """Report overlapping courtyards and parts hanging off the board."""
    boxes = []
    for p in parts:
        if not p["fp"]:
            continue
        fx, fy, rot = p["pcb"]
        bx0, by0, bx1, by1 = bbox(p["fp"], rot)
        boxes.append((p["ref"], fx + bx0, fy + by0, fx + bx1, fy + by1))

    bad = []
    for i in range(len(boxes)):
        r1, a0, b0, a1, b1 = boxes[i]
        if a0 < -0.1 or b0 < -0.1 or a1 > board_w + 0.1 or b1 > board_h + 0.1:
            bad.append("OFF-BOARD %-5s x[%.1f,%.1f] y[%.1f,%.1f]"
                       % (r1, a0, a1, b0, b1))
        for j in range(i + 1, len(boxes)):
            r2, c0, d0, c1, d1 = boxes[j]
            ox = min(a1, c1) - max(a0, c0)
            oy = min(b1, d1) - max(b0, d0)
            if ox > -clear and oy > -clear:
                bad.append("OVERLAP  %-5s %-5s  by %.2f x %.2f mm"
                           % (r1, r2, ox, oy))
    return bad
