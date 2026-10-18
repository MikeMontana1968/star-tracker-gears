#!/usr/bin/env python3
"""
kicad_board.py -- read a .kicad_pcb into plain Python: footprints, absolute pad
positions, front silkscreen, F.Cu tracks, vias, board text. No KiCad install
needed; the file is S-expressions.

    import kicad_board as KB
    fps, pads, silk, texts, segs, vias = KB.load_board('board.kicad_pcb')
    json_text = KB.board_json('board.kicad_pcb')   # for window.BOARD in the guide

Rotation convention (checked against KiCad 10 layouts): a footprint at angle a
maps a pad offset (x, y) to (x cos a + y sin a, -x sin a + y cos a), y down.
A connector at 90 deg runs pin 1 -> N toward +x; at -90 deg toward -x.
"""
import json
import math
import re


class Atom(str):
    """A bare (unquoted) token: numbers, keywords, yes/no."""
    __slots__ = ()


_TOKEN = re.compile(r'"(?:[^"\\]|\\.)*"|[()]|[^\s()]+')


def parse(text):
    """Parse S-expressions into nested lists. Returns the first top-level form."""
    stack = [[]]
    for m in _TOKEN.finditer(text):
        t = m.group(0)
        if t == "(":
            new = []
            stack[-1].append(new)
            stack.append(new)
        elif t == ")":
            stack.pop()
        elif t[0] == '"':
            body = t[1:-1]
            body = body.replace("\\n", "\n").replace('\\"', '"').replace("\\\\", "\\")
            stack[-1].append(body)
        else:
            stack[-1].append(Atom(t))
    return stack[0][0]


def kids(node, name):
    return [c for c in node if isinstance(c, list) and c and str(c[0]) == name]


def kid(node, name):
    k = kids(node, name)
    return k[0] if k else None

def rot(x, y, a):
    """KiCad footprint rotation: +a is counter-clockwise on screen (y down)."""
    r = math.radians(a)
    return x * math.cos(r) + y * math.sin(r), -x * math.sin(r) + y * math.cos(r)


def load_board(pcb_path):
    t = parse(open(pcb_path, encoding='utf8').read())
    fps, pads, silk = {}, {}, []
    for fp in kids(t, 'footprint'):
        ref = next((p[2] for p in kids(fp, 'property') if p[1] == 'Reference'), None)
        at = kid(fp, 'at')
        ox, oy, oa = float(at[1]), float(at[2]), float(at[3]) if len(at) > 3 else 0.0

        def P(x, y):
            dx, dy = rot(float(x), float(y), oa)
            return ox + dx, oy + dy
        fps[ref] = (ox, oy, oa, fp[1])
        for pd in kids(fp, 'pad'):
            a = kid(pd, 'at'); sz = kid(pd, 'size'); dr = kid(pd, 'drill'); nt = kid(pd, 'net')
            x, y = P(a[1], a[2])
            pads.setdefault(ref, []).append(dict(num=pd[1], x=x, y=y, shape=pd[3], net=str(nt[-1]) if nt else '',
                                                 w=float(sz[1]), h=float(sz[2]),
                                                 drill=float(dr[1]) if dr and len(dr) > 1 and dr[1] != 'oval' else 0.8))
        for g in fp:
            if not isinstance(g, list) or g[0] not in ('fp_line', 'fp_circle', 'fp_rect', 'fp_arc', 'fp_poly'):
                continue
            ly = kid(g, 'layer')
            if not ly or ly[1] != 'F.SilkS':
                continue
            if g[0] == 'fp_line':
                s, e = kid(g, 'start'), kid(g, 'end')
                silk.append(('L', P(s[1], s[2]), P(e[1], e[2])))
            elif g[0] == 'fp_rect':
                s, e = kid(g, 'start'), kid(g, 'end')
                c = [P(s[1], s[2]), P(e[1], s[2]), P(e[1], e[2]), P(s[1], e[2])]
                for i in range(4):
                    silk.append(('L', c[i], c[(i + 1) % 4]))
            elif g[0] == 'fp_circle':
                c, e = kid(g, 'center'), kid(g, 'end')
                r = math.hypot(float(e[1]) - float(c[1]), float(e[2]) - float(c[2]))
                silk.append(('C', P(c[1], c[2]), r))
            elif g[0] == 'fp_arc':
                s, m, e = kid(g, 'start'), kid(g, 'mid'), kid(g, 'end')
                silk.append(('A', P(s[1], s[2]), P(m[1], m[2]), P(e[1], e[2])))
            elif g[0] == 'fp_poly':
                pts = [P(q[1], q[2]) for q in kid(g, 'pts')[1:]]
                for i in range(len(pts)):
                    silk.append(('L', pts[i], pts[(i + 1) % len(pts)]))
    texts = []
    for g in kids(t, 'gr_text'):
        ly = kid(g, 'layer')
        if not ly or ly[1] != 'F.SilkS':
            continue
        at = kid(g, 'at'); eff = kid(g, 'effects'); fnt = kid(eff, 'font')
        size = float(kid(fnt, 'size')[1])
        just = kid(eff, 'justify')
        texts.append(dict(s=g[1], x=float(at[1]), y=float(at[2]), a=float(at[3]) if len(at) > 3 else 0.0,
                          size=size, just=just[1] if just else 'center'))
    segs = [(float(kid(s, 'start')[1]), float(kid(s, 'start')[2]), float(kid(s, 'end')[1]),
             float(kid(s, 'end')[2]), float(kid(s, 'width')[1]))
            for s in kids(t, 'segment') if kid(s, 'layer')[1] == 'F.Cu']
    vias = [(float(kid(v, 'at')[1]), float(kid(v, 'at')[2])) for v in kids(t, 'via')]
    return fps, pads, silk, texts, segs, vias


def _arc_pts(s, m, e, n=10):
    """Points along the circular arc start -> mid -> end."""
    (x1, y1), (x2, y2), (x3, y3) = s, m, e
    d = 2 * (x1 * (y2 - y3) + x2 * (y3 - y1) + x3 * (y1 - y2))
    if abs(d) < 1e-9:
        return [s, m, e]
    ux = ((x1 * x1 + y1 * y1) * (y2 - y3) + (x2 * x2 + y2 * y2) * (y3 - y1) + (x3 * x3 + y3 * y3) * (y1 - y2)) / d
    uy = ((x1 * x1 + y1 * y1) * (x3 - x2) + (x2 * x2 + y2 * y2) * (x1 - x3) + (x3 * x3 + y3 * y3) * (x2 - x1)) / d
    r = math.hypot(x1 - ux, y1 - uy)
    a1, a2, a3 = (math.atan2(p[1] - uy, p[0] - ux) for p in (s, m, e))
    # sweep from a1 to a3 the way that passes a2
    sw = (a3 - a1) % (2 * math.pi)
    if (a2 - a1) % (2 * math.pi) > sw:
        sw -= 2 * math.pi
    return [(ux + r * math.cos(a1 + sw * i / n), uy + r * math.sin(a1 + sw * i / n)) for i in range(n + 1)]


def _silk_by_ref(t):
    """Front silkscreen of each footprint, absolute: {ref: [polyline | circle]},
    a polyline as a flat [x, y, x, y, ...] list, a circle as {"c": [cx, cy, r]}."""
    out = {}
    for fp in kids(t, 'footprint'):
        ref = next((p[2] for p in kids(fp, 'property') if p[1] == 'Reference'), None)
        at = kid(fp, 'at')
        ox, oy, oa = float(at[1]), float(at[2]), float(at[3]) if len(at) > 3 else 0.0

        def P(x, y):
            dx, dy = rot(float(x), float(y), oa)
            return ox + dx, oy + dy
        items = []
        for g in fp:
            if not isinstance(g, list) or g[0] not in ('fp_line', 'fp_circle', 'fp_rect', 'fp_arc', 'fp_poly'):
                continue
            ly = kid(g, 'layer')
            if not ly or ly[1] != 'F.SilkS':
                continue
            pts = None
            if g[0] == 'fp_line':
                s, e = kid(g, 'start'), kid(g, 'end'); pts = [P(s[1], s[2]), P(e[1], e[2])]
            elif g[0] == 'fp_rect':
                s, e = kid(g, 'start'), kid(g, 'end')
                pts = [P(s[1], s[2]), P(e[1], s[2]), P(e[1], e[2]), P(s[1], e[2]), P(s[1], s[2])]
            elif g[0] == 'fp_poly':
                pts = [P(q[1], q[2]) for q in kid(g, 'pts')[1:]]; pts.append(pts[0])
            elif g[0] == 'fp_arc':
                s, m, e = kid(g, 'start'), kid(g, 'mid'), kid(g, 'end')
                pts = _arc_pts(P(s[1], s[2]), P(m[1], m[2]), P(e[1], e[2]))
            elif g[0] == 'fp_circle':
                c, e = kid(g, 'center'), kid(g, 'end')
                cx, cy = P(c[1], c[2])
                items.append({'c': [round(cx, 2), round(cy, 2), round(math.hypot(float(e[1]) - float(c[1]), float(e[2]) - float(c[2])), 2)]})
            if pts:
                items.append([round(v, 2) for p in pts for v in p])
        out[ref] = items
    return out


def board_json(pcb_path):
    """Every footprint's ref, library name, value, angle, absolute pads
    [x, y, number, shape ('s' square / 'c' round), w, h, net] and front silkscreen
    ('s', see _silk_by_ref) -- what figs_pcb.js draws the isometric board from
    and figs_beginner.js draws the top-down part maps from."""
    t = parse(open(pcb_path, encoding='utf8').read())
    vals = {}
    for fp in kids(t, 'footprint'):
        props = {p[1]: p[2] for p in kids(fp, 'property')}
        vals[props.get('Reference')] = props.get('Value', '')
    fps, pads, _, _, _, _ = load_board(pcb_path)
    silk = _silk_by_ref(t)
    out = []
    for ref, (x, y, a, name) in fps.items():
        out.append(dict(r=ref, f=name.split(':')[-1], v=vals.get(ref, ''), a=a,
                        p=[[round(q['x'], 2), round(q['y'], 2), q['num'],
                            's' if q['shape'] in ('rect', 'roundrect') else 'c', round(q['w'], 2), round(q['h'], 2), q['net']]
                           for q in pads.get(ref, [])],
                        s=silk.get(ref, [])))
    return json.dumps(out, separators=(',', ':'))


def board_info_json(pcb_path):
    """The board's outline box (Edge.Cuts extent) and its free silkscreen text:
    {"edge": [x0, y0, x1, y1], "text": [{s, x, y, a, size, just}]}. -> window.BOARD_INFO"""
    t = parse(open(pcb_path, encoding='utf8').read())
    xs, ys = [], []
    for g in t:
        if not isinstance(g, list) or not g or str(g[0]) not in ('gr_line', 'gr_rect', 'gr_arc', 'gr_circle'):
            continue
        ly = kid(g, 'layer')
        if not ly or ly[1] != 'Edge.Cuts':
            continue
        for k in ('start', 'end', 'mid', 'center'):
            p = kid(g, k)
            if p:
                xs.append(float(p[1])); ys.append(float(p[2]))
    _, _, _, texts, _, _ = load_board(pcb_path)
    edge = [min(xs), min(ys), max(xs), max(ys)] if xs else None
    return json.dumps(dict(edge=edge, text=texts), separators=(',', ':'))