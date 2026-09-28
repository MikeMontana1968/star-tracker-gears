"""A small two-layer maze router (A* with via cost) for this board.

Not clever: grid-based Lee/A*, nets routed longest-first, traces become
obstacles for later nets.  Good enough for a 150 kHz THT controller board,
and every result is checked by KiCad's own DRC afterwards.
"""
import heapq
import random

import kicadlib as K
import layout as LAY

GRID = 0.25
F, B = 0, 1
VIA_COST = 12
MAX_EXPANSIONS = 200000          # in grid steps
VIA_R = 0.4            # 0.8 mm via
VIA_KEEP = 0.4 + 0.3 + 0.2   # via radius + clearance + hole slop
# Drill-to-drill: the fab's minimum hole spacing applies whatever the nets,
# but the copper checks pass a via against its OWN net's pads. A GND via once
# landed 0.19 mm from a GND pad's hole (min 0.25). VIA_HOLE is the via drill
# radius; a pad's hole is taken as its copper radius, conservatively.
VIA_HOLE = 0.2
HOLE_GAP = 0.25 + 0.1         # fab minimum + grid slop
TURN_COST = 2


class Router:
    def __init__(self, parts, board_w, board_h, nets_power, clearance=0.3):
        self.w = int(board_w / GRID) + 1
        self.h = int(board_h / GRID) + 1
        self.clear = clearance
        self.power = set(nets_power)
        # occ[layer] = bytearray of net ids; 0 = free, 255 = hard block
        self.occ = [bytearray(self.w * self.h) for _ in range(2)]
        self.nid = {}
        self.next_id = 1
        self.pad_cells = {}          # net -> list of (layer, cell) groups per pad
        self.tracks = []
        self.laid = {}   # net -> cells carrying track (not pads)
        self.vias = []
        self.holes = bytearray(self.w * self.h)   # 1 = no via centre here
        self.stranded = {}   # net -> pad cell groups route_net could not reach
        self._load(parts)

    # ---------------- grid helpers ----------------
    def idx(self, x, y):
        return y * self.w + x

    def to_grid(self, mm):
        return int(round(mm / GRID))

    def in_bounds(self, x, y):
        return 0 <= x < self.w and 0 <= y < self.h

    def id_of(self, name):
        if name == "#":
            return 255
        i = self.nid.get(name)
        if i is None:
            i = self.next_id
            self.next_id += 1
            if i >= 255:
                raise RuntimeError("too many nets for a byte grid")
            self.nid[name] = i
        return i

    def half(self, net):
        return 0.5 if net in self.power else 0.2

    # ---------------- obstacles ----------------
    def _load(self, parts):
        self.pads = []
        for p in parts:
            if not p["fp"]:
                continue
            fp = K.load_footprint(p["fp"])
            fx, fy, rot = p["pcb"]
            for pad in K.kids(fp, "pad"):
                num = str(pad[1])
                at = K.kid(pad, "at")
                size = K.kid(pad, "size")
                if at is None or size is None:
                    continue
                lx, ly = float(at[1]), float(at[2])
                sw, sh = float(size[1]), float(size[2])
                rx, ry = K.rotate(lx, ly, rot)
                net = p["pins"].get(num, "-")
                if net == "-":
                    net = None
                self.pads.append(dict(net=net, x=fx + rx, y=fy + ry,
                                      r=max(sw, sh) / 2.0, ref=p["ref"],
                                      num=num))

        # board margin
        for lay in (F, B):
            o = self.occ[lay]
            mg = int(1.0 / GRID)
            for y in range(self.h):
                for x in range(self.w):
                    if x < mg or y < mg or x >= self.w - mg or y >= self.h - mg:
                        o[self.idx(x, y)] = 255

        # pads occupy both layers (through-hole)
        for pad in self.pads:
            self._stamp_pad(pad)
            self._stamp_hole(pad["x"], pad["y"], pad["r"])

    def _stamp_hole(self, cx, cy, hole_r):
        """mark where a via centre may not go: hole_r + via hole + gap"""
        r = hole_r + VIA_HOLE + HOLE_GAP
        for y in range(self.to_grid(cy - r), self.to_grid(cy + r) + 1):
            for x in range(self.to_grid(cx - r), self.to_grid(cx + r) + 1):
                if self.in_bounds(x, y) and (x * GRID - cx) ** 2 + (y * GRID - cy) ** 2 <= r * r:
                    self.holes[self.idx(x, y)] = 1

    def _stamp_disc(self, lay, cx, cy, r, name, both=False):
        layers = (F, B) if both else (lay,)
        x0, x1 = self.to_grid(cx - r), self.to_grid(cx + r)
        y0, y1 = self.to_grid(cy - r), self.to_grid(cy + r)
        for y in range(y0, y1 + 1):
            for x in range(x0, x1 + 1):
                if not self.in_bounds(x, y):
                    continue
                dx, dy = x * GRID - cx, y * GRID - cy
                if dx * dx + dy * dy <= r * r:
                    nid = self.id_of(name)
                    for L in layers:
                        if self.occ[L][self.idx(x, y)] == 0:
                            self.occ[L][self.idx(x, y)] = nid

    def _stamp_pad(self, pad):
        cx, cy, r = pad["x"], pad["y"], pad["r"]
        name = pad["net"] or "#"
        rr = r + 0.05
        x0, x1 = self.to_grid(cx - rr), self.to_grid(cx + rr)
        y0, y1 = self.to_grid(cy - rr), self.to_grid(cy + rr)
        cells = []
        for y in range(y0, y1 + 1):
            for x in range(x0, x1 + 1):
                if not self.in_bounds(x, y):
                    continue
                dx, dy = x * GRID - cx, y * GRID - cy
                if dx * dx + dy * dy <= rr * rr:
                    for lay in (F, B):
                        self.occ[lay][self.idx(x, y)] = self.id_of(name)
                    cells.append((x, y))
        if not cells:
            x, y = self.to_grid(cx), self.to_grid(cy)
            cells = [(x, y)]
            for lay in (F, B):
                self.occ[lay][self.idx(x, y)] = self.id_of(name)
        pad["cells"] = cells

    def _free(self, lay, x, y, net):
        v = self.occ[lay][self.idx(x, y)]
        return v == 0 or v == self.id_of(net)

    def _clear_ok(self, lay, x, y, net):
        return self._clear_r(lay, x, y, net, self.clear + self.half(net) + 0.18)

    def _clear_r(self, lay, x, y, net, radius):
        """cell free, and nothing foreign within `radius` mm.

        Row-at-a-time using bytes.count(), which runs in C -- this is the
        router's hot loop and a per-cell Python scan is ~20x slower.
        """
        rad = int(round(radius / GRID))
        if x - rad < 0 or y - rad < 0 or x + rad >= self.w or y + rad >= self.h:
            return False
        nid = self.id_of(net)
        occ = self.occ[lay]
        span = 2 * rad + 1
        base = (y - rad) * self.w + (x - rad)
        for _ in range(span):
            seg = occ[base:base + span]
            z = seg.count(0)
            if z != span and z + seg.count(nid) != span:
                return False
            base += self.w
        return True

    # ---------------- routing ----------------
    def route_net(self, net):
        groups = [p["cells"] for p in self.pads if p["net"] == net]
        if len(groups) < 2:
            return True
        # Seed from track already laid for this net, so a second stitching
        # pass lets a straggler reach copper placed after its first attempt.
        laid = self.laid.get(net, set())
        connected = set(laid)
        remaining = []
        for g in groups:
            if any((F, x, y) in laid or (B, x, y) in laid for x, y in g):
                for x, y in g:
                    connected.add((F, x, y))
                    connected.add((B, x, y))
            else:
                remaining.append(g)
        if not connected:
            g0 = remaining.pop(0)
            for x, y in g0:
                connected.add((F, x, y))
                connected.add((B, x, y))
        if not remaining:
            return True
        ok = True
        while remaining:
            # nearest remaining pad to the connected set
            def d(g):
                gx = sum(c[0] for c in g) / len(g)
                gy = sum(c[1] for c in g) / len(g)
                return min(abs(gx - c[1]) + abs(gy - c[2]) for c in connected)
            remaining.sort(key=d)
            target = remaining.pop(0)
            path = self._astar(connected, target, net)
            if path is None:
                ok = False
                self.stranded.setdefault(net, []).append(target)
                continue
            self._commit(path, net)
            for lay, x, y in path:
                connected.add((lay, x, y))
            for x, y in target:
                connected.add((F, x, y))
                connected.add((B, x, y))
        return ok

    def rescue(self, net, group):
        """Connect one pad to the nearest other pad of its net, now, before
        anything else is laid -- a short escape reserved ahead of the signals."""
        others = [p["cells"] for p in self.pads if p["net"] == net and p["cells"] is not group
                  and set(p["cells"]) != set(group)]
        gx = sum(c[0] for c in group) / len(group); gy = sum(c[1] for c in group) / len(group)
        for o in sorted(others, key=lambda o: abs(o[0][0] - gx) + abs(o[0][1] - gy))[:4]:
            src = set((L, x, y) for x, y in o for L in (F, B))
            path = self._astar(src, group, net)
            if path:
                self._commit(path, net)
                return True
        return False

    def _astar(self, sources, target, net):
        tgt = set(target)
        tx = sum(c[0] for c in target) / len(target)
        ty = sum(c[1] for c in target) / len(target)

        def hcost(x, y):
            return abs(x - tx) + abs(y - ty)

        pq = []
        best = {}
        for lay, x, y in sources:
            st = (lay, x, y)
            best[st] = 0
            heapq.heappush(pq, (hcost(x, y), 0, st, None, None))
        came = {}
        goal = None
        budget = MAX_EXPANSIONS
        while pq:
            budget -= 1
            if budget <= 0:          # unroutable: bail instead of sweeping
                return None
            _f, g, st, parent, pdir = heapq.heappop(pq)
            if st in came and came[st][1] <= g:
                continue
            came[st] = (parent, g)
            lay, x, y = st
            if (x, y) in tgt:
                goal = st
                break
            for dx, dy, dl in ((1, 0, 0), (-1, 0, 0), (0, 1, 0), (0, -1, 0),
                               (0, 0, 1)):
                if dl:
                    nl, nx, ny = 1 - lay, x, y
                    step = VIA_COST
                    if self.holes[self.idx(nx, ny)]:
                        continue
                    if not (self._clear_r(F, nx, ny, net, VIA_KEEP) and
                            self._clear_r(B, nx, ny, net, VIA_KEEP)):
                        continue
                else:
                    nl, nx, ny = lay, x + dx, y + dy
                    step = 1 + (TURN_COST if pdir and pdir != (dx, dy) else 0)
                    if not self.in_bounds(nx, ny):
                        continue
                    if (nx, ny) not in tgt and not self._clear_ok(nl, nx, ny, net):
                        continue
                ns = (nl, nx, ny)
                ng = g + step
                if ns in best and best[ns] <= ng:
                    continue
                best[ns] = ng
                heapq.heappush(pq, (ng + hcost(nx, ny), ng, ns, st,
                                    (dx, dy) if not dl else pdir))
        if goal is None:
            return None
        path = []
        cur = goal
        while cur is not None:
            path.append(cur)
            cur = came[cur][0]
        path.reverse()
        return path

    def _commit(self, path, net):
        # a track is as wide as it is: stamp its whole body, plus grid slop,
        # so later nets keep a true edge-to-edge clearance
        rad = self.half(net) + 0.18
        cells = self.laid.setdefault(net, set())
        for lay, x, y in path:
            self._stamp_disc(lay, x * GRID, y * GRID, rad, net)
            cells.add((lay, x, y))
        # split into straight runs per layer, emit segments and vias
        run = [path[0]]
        for cur in path[1:]:
            if cur[0] != run[-1][0]:
                self._emit(run, net)
                vx, vy = run[-1][1] * GRID, run[-1][2] * GRID
                self._stamp_disc(None, vx, vy, VIA_R + 0.18, net, both=True)
                self._stamp_hole(vx, vy, VIA_HOLE)
                self.vias.append((vx, vy, net))
                run = [cur]
            else:
                run.append(cur)
        self._emit(run, net)

    def _emit(self, run, net):
        if len(run) < 2:
            return
        lay = run[0][0]
        pts = [(run[0][1], run[0][2])]
        for i in range(1, len(run)):
            dx = run[i][1] - run[i - 1][1]
            dy = run[i][2] - run[i - 1][2]
            if i + 1 < len(run):
                ndx = run[i + 1][1] - run[i][1]
                ndy = run[i + 1][2] - run[i][2]
                if (dx, dy) == (ndx, ndy):
                    continue
            pts.append((run[i][1], run[i][2]))
        self.tracks.append((net, "F.Cu" if lay == F else "B.Cu",
                            [(x * GRID, y * GRID) for x, y in pts],
                            1.0 if net in self.power else 0.4))


def _span(parts, net):
    """Manhattan extent of a net's pads -- a cheap difficulty proxy."""
    xs, ys = [], []
    for p in parts:
        if not p["fp"]:
            continue
        for num, n in p["pins"].items():
            if n == net:
                xs.append(p["pcb"][0])
                ys.append(p["pcb"][1])
    if len(xs) < 2:
        return 0.0
    return (max(xs) - min(xs)) + (max(ys) - min(ys))


def route_all(parts, board_w, board_h, power_nets, skip=("GND",), passes=30,
              seed=12345, rescue=()):
    """Greedy route with randomised multi-start.

    Which nets fail depends almost entirely on the order they are routed in,
    so rather than hand-tune one order, try several: failures from a pass are
    promoted to the front of the next, and the remainder is shuffled.
    Keeps the best result seen.

    rescue: ("J6", "6") pad ids that get a short escape to their nearest
    same-net pad BEFORE any signal is routed. For GND pads the pour cannot
    reach because signal tracks fence them in on both layers -- which only
    KiCad's DRC can tell (it knows the pour), so they are named in design.py
    from a DRC run rather than guessed here. Rescuing every pad the router
    itself cannot stitch (11 on Rev C) fenced six signals; rescuing the two
    KiCad names does not.
    """
    counts = {}
    for p in parts:
        for n in p["pins"].values():
            if n != "-":
                counts[n] = counts.get(n, 0) + 1
    # GND is poured on both layers; the stitch tracks below only rescue pads
    # the pour misses, at signal width, since the pour carries the current.
    stitch = [n for n in skip if counts.get(n, 0) > 1]
    nets = [n for n in counts if n not in skip and counts[n] > 1]
    spans = {n: _span(parts, n) for n in nets}
    rng = random.Random(seed)

    priority = []
    best = None
    for it in range(passes):
        rest = [n for n in nets if n not in priority]
        if it == 0:
            rest.sort(key=lambda n: -spans[n])
        else:
            rng.shuffle(rest)
        order = priority + rest
        # power rails first: wide tracks that want direct paths
        order.sort(key=lambda n: n not in power_nets)

        r = Router(parts, board_w, board_h,
                   [n for n in power_nets if n not in stitch])
        rescued = []
        for ref, num in rescue:
            pad = next((p for p in r.pads if p["ref"] == ref and p["num"] == num), None)
            if pad and r.rescue(pad["net"], pad["cells"]):
                rescued.append("%s.%s" % (ref, num))
        failed = [n for n in order if not r.route_net(n)]
        # GND last, at signal width. Never fatal. Twice, so a straggler can
        # reach track laid after its own first attempt (route_net seeds from
        # copper already laid for the net).
        for _ in range(2):
            for n in stitch:
                r.route_net(n)
        if best is None or len(failed) < len(best[2]):
            best = (r.tracks, r.vias, failed)
            print("    pass %d: %d unrouted%s%s" % (it + 1, len(failed),
                  (" (" + ", ".join(failed) + ")") if failed else "",
                  ("; rescued first: " + ", ".join(rescued)) if rescue else ""))
        if not failed:
            break
        priority = failed + [n for n in priority if n not in failed]
    return best