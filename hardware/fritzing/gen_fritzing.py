#!/usr/bin/env python3
"""
Fritzing wiring project for the star tracker's external electronics.

    python gen_fritzing.py            # writes star_tracker_wiring.fzz
    & 'C:\\Program Files\\Fritzing\\Fritzing.exe' -svg <folder>   # exports views

Like the PCB, this is generated, not hand-drawn. Every part in the sketch is a
custom part carried inside the .fzz, so every pin position is known exactly
and the wires land on it:

  - the controller board is drawn from star_tracker_ctrl.kicad_pcb itself:
    its silkscreen, pads, tracks, vias and the connector positions all come
    from the layout, so the illustration follows the board when it changes
  - battery, fuse, NEMA 17, AS5600, GY-NEO6MV2 and the GoPro are drawn here

Breadboard view carries the realistic wiring (bezier wires, Fritzing colours).
Schematic view carries the same connections as a clean system diagram: each
external part is placed so its pins sit level with the board pins they meet,
so every trace is one straight line.

Units: part SVGs are authored in mm. Fritzing's scene is 90 px per inch.
"""
import math
import os
import sys
import zipfile
from xml.sax.saxutils import escape

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.dirname(HERE))
import kicadlib as K  # noqa: E402

PCB = os.path.join(HERE, '..', 'star_tracker_ctrl', 'star_tracker_ctrl.kicad_pcb')
OUT = os.path.join(HERE, 'star_tracker_wiring.fzz')
MM = 90 / 25.4          # scene px per mm
SCH_P = 2.54            # schematic pin pitch, mm (0.1 in grid)

# wire colours (Fritzing's own palette names in the comments)
RED, BLACK, BLUE, YELLOW, GREEN = '#cc1414', '#404040', '#418dd9', '#ffe24d', '#47cc79'
ORANGE, PURPLE, WHITE, GREY = '#ef6100', '#ab58a2', '#ffffff', '#999999'


def f(v):
    return ('%.3f' % v).rstrip('0').rstrip('.')


# --------------------------------------------------------------------------
# the board, read from KiCad
# --------------------------------------------------------------------------
def rot(x, y, a):
    """KiCad footprint rotation: +a is counter-clockwise on screen (y down)."""
    r = math.radians(a)
    return x * math.cos(r) + y * math.sin(r), -x * math.sin(r) + y * math.cos(r)


def load_board():
    t = K.parse(open(PCB, encoding='utf8').read())
    fps, pads, silk = {}, {}, []
    for fp in K.kids(t, 'footprint'):
        ref = next((p[2] for p in K.kids(fp, 'property') if p[1] == 'Reference'), None)
        at = K.kid(fp, 'at')
        ox, oy, oa = float(at[1]), float(at[2]), float(at[3]) if len(at) > 3 else 0.0

        def P(x, y):
            dx, dy = rot(float(x), float(y), oa)
            return ox + dx, oy + dy
        fps[ref] = (ox, oy, oa, fp[1])
        for pd in K.kids(fp, 'pad'):
            a = K.kid(pd, 'at'); sz = K.kid(pd, 'size'); dr = K.kid(pd, 'drill')
            x, y = P(a[1], a[2])
            pads.setdefault(ref, []).append(dict(num=pd[1], x=x, y=y, shape=pd[3],
                                                 w=float(sz[1]), h=float(sz[2]),
                                                 drill=float(dr[1]) if dr and len(dr) > 1 and dr[1] != 'oval' else 0.8))
        for g in fp:
            if not isinstance(g, list) or g[0] not in ('fp_line', 'fp_circle', 'fp_rect', 'fp_arc', 'fp_poly'):
                continue
            ly = K.kid(g, 'layer')
            if not ly or ly[1] != 'F.SilkS':
                continue
            if g[0] == 'fp_line':
                s, e = K.kid(g, 'start'), K.kid(g, 'end')
                silk.append(('L', P(s[1], s[2]), P(e[1], e[2])))
            elif g[0] == 'fp_rect':
                s, e = K.kid(g, 'start'), K.kid(g, 'end')
                c = [P(s[1], s[2]), P(e[1], s[2]), P(e[1], e[2]), P(s[1], e[2])]
                for i in range(4):
                    silk.append(('L', c[i], c[(i + 1) % 4]))
            elif g[0] == 'fp_circle':
                c, e = K.kid(g, 'center'), K.kid(g, 'end')
                r = math.hypot(float(e[1]) - float(c[1]), float(e[2]) - float(c[2]))
                silk.append(('C', P(c[1], c[2]), r))
            elif g[0] == 'fp_arc':
                s, m, e = K.kid(g, 'start'), K.kid(g, 'mid'), K.kid(g, 'end')
                silk.append(('A', P(s[1], s[2]), P(m[1], m[2]), P(e[1], e[2])))
            elif g[0] == 'fp_poly':
                pts = [P(q[1], q[2]) for q in K.kid(g, 'pts')[1:]]
                for i in range(len(pts)):
                    silk.append(('L', pts[i], pts[(i + 1) % len(pts)]))
    texts = []
    for g in K.kids(t, 'gr_text'):
        ly = K.kid(g, 'layer')
        if not ly or ly[1] != 'F.SilkS':
            continue
        at = K.kid(g, 'at'); eff = K.kid(g, 'effects'); fnt = K.kid(eff, 'font')
        size = float(K.kid(fnt, 'size')[1])
        just = K.kid(eff, 'justify')
        texts.append(dict(s=g[1], x=float(at[1]), y=float(at[2]), a=float(at[3]) if len(at) > 3 else 0.0,
                          size=size, just=just[1] if just else 'center'))
    segs = [(float(K.kid(s, 'start')[1]), float(K.kid(s, 'start')[2]), float(K.kid(s, 'end')[1]),
             float(K.kid(s, 'end')[2]), float(K.kid(s, 'width')[1]))
            for s in K.kids(t, 'segment') if K.kid(s, 'layer')[1] == 'F.Cu']
    vias = [(float(K.kid(v, 'at')[1]), float(K.kid(v, 'at')[2])) for v in K.kids(t, 'via')]
    return fps, pads, silk, texts, segs, vias


def arc_path(s, m, e):
    """SVG arc through three points."""
    (x1, y1), (x2, y2), (x3, y3) = s, m, e
    d = 2 * (x1 * (y2 - y3) + x2 * (y3 - y1) + x3 * (y1 - y2))
    if abs(d) < 1e-9:
        return 'M%s %sL%s %s' % (f(x1), f(y1), f(x3), f(y3))
    ux = ((x1**2 + y1**2) * (y2 - y3) + (x2**2 + y2**2) * (y3 - y1) + (x3**2 + y3**2) * (y1 - y2)) / d
    uy = ((x1**2 + y1**2) * (x3 - x2) + (x2**2 + y2**2) * (x1 - x3) + (x3**2 + y3**2) * (x2 - x1)) / d
    r = math.hypot(x1 - ux, y1 - uy)
    cross = (x2 - x1) * (y3 - y1) - (y2 - y1) * (x3 - x1)
    sweep = 1 if cross > 0 else 0
    a1, a3 = math.atan2(y1 - uy, x1 - ux), math.atan2(y3 - uy, x3 - ux)
    a2 = math.atan2(y2 - uy, x2 - ux)
    span = (a3 - a1) % (2 * math.pi) if sweep else (a1 - a3) % (2 * math.pi)
    large = 1 if span > math.pi else 0
    return 'M%s %sA%s %s 0 %d %d %s %s' % (f(x1), f(y1), f(r), f(r), large, sweep, f(x3), f(y3))


# --------------------------------------------------------------------------
# a part: breadboard art + connectors; schematic and pcb are derived
# --------------------------------------------------------------------------
class Part:
    def __init__(self, key, title, label, w, h, art, desc=''):
        self.key, self.title, self.label, self.w, self.h, self.art, self.desc = key, title, label, w, h, art, desc
        self.conns = []         # (name, description, bb x, bb y, schematic side)
        self.sch_left, self.sch_right = [], []

    def conn(self, name, desc, x, y, side, slot=None):
        cid = 'connector%d' % len(self.conns)
        self.conns.append(dict(id=cid, name=name, desc=desc, x=x, y=y, side=side))
        (self.sch_left if side == 'L' else self.sch_right).append((cid, name, slot))
        return cid

    @property
    def module_id(self):
        return 'startracker_%s_v1' % self.key

    # -- files -------------------------------------------------------------
    def fn(self, view):
        return 'startracker_%s_%s.svg' % (self.key, view)

    def fzp(self):
        c = []
        for k in self.conns:
            c.append('''  <connector id="%(id)s" name="%(name)s" type="female">
   <description>%(desc)s</description>
   <views>
    <breadboardView><p layer="breadboard" svgId="%(id)spin"/></breadboardView>
    <schematicView><p layer="schematic" svgId="%(id)spin" terminalId="%(id)sterminal"/></schematicView>
    <pcbView><p layer="copper0" svgId="%(id)spin"/><p layer="copper1" svgId="%(id)spin"/></pcbView>
   </views>
  </connector>''' % dict(id=k['id'], name=escape(k['name']), desc=escape(k['desc'])))
        return '''<?xml version="1.0" encoding="UTF-8"?>
<module fritzingVersion="1.0.8" moduleId="%s">
 <version>1</version>
 <author>star-tracker-gears/hardware/fritzing/gen_fritzing.py</author>
 <title>%s</title>
 <label>%s</label>
 <description>%s</description>
 <tags><tag>star tracker</tag></tags>
 <properties><property name="family">star tracker</property><property name="variant">%s</property></properties>
 <views>
  <iconView><layers image="icon/%s"><layer layerId="icon"/></layers></iconView>
  <breadboardView><layers image="breadboard/%s"><layer layerId="breadboard"/></layers></breadboardView>
  <schematicView><layers image="schematic/%s"><layer layerId="schematic"/></layers></schematicView>
  <pcbView><layers image="pcb/%s"><layer layerId="copper0"/><layer layerId="silkscreen"/><layer layerId="copper1"/></layers></pcbView>
 </views>
 <connectors>
%s
 </connectors>
</module>
''' % (self.module_id, escape(self.title), self.label, escape(self.desc), self.key,
       self.fn('icon'), self.fn('breadboard'), self.fn('schematic'), self.fn('pcb'), '\n'.join(c))

    def bb_svg(self, layer='breadboard'):
        pins = ''.join('<rect id="%spin" x="%s" y="%s" width="1.2" height="1.2" fill="none" stroke="none"/>'
                       % (k['id'], f(k['x'] - 0.6), f(k['y'] - 0.6)) for k in self.conns)
        return ('<?xml version="1.0" encoding="UTF-8"?>\n<svg xmlns="http://www.w3.org/2000/svg" '
                'width="%smm" height="%smm" viewBox="0 0 %s %s"><g id="%s">%s%s</g></svg>\n'
                % (f(self.w), f(self.h), f(self.w), f(self.h), layer, self.art, pins))

    # schematic: a box with pins on two sides, 0.1 in grid
    def sch_geom(self):
        nl = max([s for _, _, s in self.sch_left if s is not None] + [len(self.sch_left) - 1, 0]) + 1
        nr = max([s for _, _, s in self.sch_right if s is not None] + [len(self.sch_right) - 1, 0]) + 1
        rows = max(nl, nr, 2)
        longest = max([len(n) for _, n, _ in self.sch_left + self.sch_right] + [6])
        bw = max(6 * SCH_P, math.ceil((longest * 1.6 * (2 if self.sch_left and self.sch_right else 1) + 4) / SCH_P) * SCH_P)
        bw = max(bw, math.ceil((len(self.title) * 1.7 + 4) / SCH_P) * SCH_P) if len(self.title) < 34 else bw
        pl = SCH_P * 2
        W = bw + (pl if self.sch_left else 0) + (pl if self.sch_right else 0)
        H = (rows + 2) * SCH_P
        x0 = pl if self.sch_left else 0
        return dict(rows=rows, bw=bw, pl=pl, W=W, H=H, x0=x0)

    def sch_terminal(self, cid):
        g = self.sch_geom()
        for side, lst in (('L', self.sch_left), ('R', self.sch_right)):
            for i, (c, n, slot) in enumerate(lst):
                if c == cid:
                    row = slot if slot is not None else i
                    y = (row + 1.5) * SCH_P
                    return (0.0 if side == 'L' else g['W']), y
        raise KeyError(cid)

    def sch_svg(self):
        g = self.sch_geom()
        x0, bw, pl, W, H = g['x0'], g['bw'], g['pl'], g['W'], g['H']
        s = ['<rect x="%s" y="%s" width="%s" height="%s" fill="#FFFFFF" stroke="#000000" stroke-width="0.25"/>'
             % (f(x0), f(SCH_P * 0.5), f(bw), f(H - SCH_P))]
        s.append('<text x="%s" y="%s" font-family="Droid Sans, Noto Sans, Arial, sans-serif" font-size="2.2" text-anchor="middle" fill="#000000">%s</text>'
                 % (f(x0 + bw / 2), f(SCH_P * 0.5 - 0.8), escape(self.title)))
        for side, lst in (('L', self.sch_left), ('R', self.sch_right)):
            for i, (c, n, slot) in enumerate(lst):
                row = slot if slot is not None else i
                y = (row + 1.5) * SCH_P
                if side == 'L':
                    xa, xb, tx, anc = 0, pl, pl + 0.8, 'start'
                else:
                    xa, xb, tx, anc = W, W - pl, W - pl - 0.8, 'end'
                s.append('<line id="%spin" x1="%s" y1="%s" x2="%s" y2="%s" stroke="#555555" stroke-width="0.25"/>'
                         % (c, f(xa), f(y), f(xb), f(y)))
                s.append('<rect id="%sterminal" x="%s" y="%s" width="0.01" height="0.01" fill="none"/>' % (c, f(xa - 0.005), f(y - 0.005)))
                s.append('<text x="%s" y="%s" font-family="Droid Sans, Noto Sans, Arial, sans-serif" font-size="1.7" text-anchor="%s" fill="#555555">%s</text>'
                         % (f(tx), f(y + 0.6), anc, escape(n)))
        return ('<?xml version="1.0" encoding="UTF-8"?>\n<svg xmlns="http://www.w3.org/2000/svg" width="%smm" height="%smm" '
                'viewBox="0 0 %s %s"><g id="schematic">%s</g></svg>\n' % (f(W), f(H), f(W), f(H), ''.join(s)))

    def pcb_svg(self):
        n = len(self.conns)
        W, H = max(n * 2.54, 2.54) + 2, 4.54
        pads = ''.join('<circle id="%spin" cx="%s" cy="2.27" r="0.8" fill="none" stroke="#F7BD13" stroke-width="0.5"/>'
                       % (k['id'], f(1 + 1.27 + i * 2.54)) for i, k in enumerate(self.conns))
        return ('<?xml version="1.0" encoding="UTF-8"?>\n<svg xmlns="http://www.w3.org/2000/svg" width="%smm" height="%smm" '
                'viewBox="0 0 %s %s"><g id="silkscreen"><rect x="0.2" y="0.2" width="%s" height="%s" fill="none" '
                'stroke="#000000" stroke-width="0.2"/></g><g id="copper1"><g id="copper0">%s</g></g></svg>\n'
                % (f(W), f(H), f(W), f(H), f(W - 0.4), f(H - 0.4), pads))


# --------------------------------------------------------------------------
# the parts
# --------------------------------------------------------------------------
def text(x, y, s, size=2.2, fill='#ffffff', anchor='middle', weight='bold', rot=0):
    tr = ' transform="rotate(%s %s %s)"' % (f(rot), f(x), f(y)) if rot else ''
    return ('<text x="%s" y="%s" font-family="Droid Sans, Noto Sans, Arial, sans-serif" font-size="%s" font-weight="%s" fill="%s" text-anchor="%s"%s>%s</text>'
            % (f(x), f(y), f(size), weight, fill, anchor, tr, escape(s)))


def controller():
    fps, pads, silk, texts, segs, vias = load_board()
    W, H = 150.0, 100.0
    a = ['<rect x="0" y="0" width="150" height="100" rx="1.5" fill="#1f5c2e" stroke="#123a1c" stroke-width="0.4"/>']
    a.append('<g stroke="#2f7a40" stroke-linecap="round" fill="none">%s</g>' % ''.join(
        '<line x1="%s" y1="%s" x2="%s" y2="%s" stroke-width="%s"/>' % (f(x1), f(y1), f(x2), f(y2), f(w))
        for x1, y1, x2, y2, w in segs))
    a.append(''.join('<circle cx="%s" cy="%s" r="0.45" fill="#c9a227"/>' % (f(x), f(y)) for x, y in vias))
    sl = []
    for g in silk:
        if g[0] == 'L':
            sl.append('<line x1="%s" y1="%s" x2="%s" y2="%s"/>' % (f(g[1][0]), f(g[1][1]), f(g[2][0]), f(g[2][1])))
        elif g[0] == 'C':
            sl.append('<circle cx="%s" cy="%s" r="%s"/>' % (f(g[1][0]), f(g[1][1]), f(g[2])))
        else:
            sl.append('<path d="%s"/>' % arc_path(g[1], g[2], g[3]))
    a.append('<g stroke="#f2f2f2" stroke-width="0.15" fill="none">%s</g>' % ''.join(sl))
    for t in texts:
        anc = {'left': 'start', 'right': 'end'}.get(t['just'], 'middle')
        a.append(text(t['x'], t['y'] + t['size'] * 0.35, t['s'], size=t['size'] * 0.95, fill='#f2f2f2',
                      anchor=anc, weight='normal', rot=-t['a']))
    for ref, pl in pads.items():
        for p in pl:
            if p['num'] == '' or ref.startswith('H'):
                a.append('<circle cx="%s" cy="%s" r="1.6" fill="#ffffff" stroke="#c9a227" stroke-width="1.2"/>' % (f(p['x']), f(p['y'])))
                continue
            r = min(p['w'], p['h']) / 2
            if p['shape'] in ('rect', 'roundrect'):
                a.append('<rect x="%s" y="%s" width="%s" height="%s" fill="#c9a227"/>' % (f(p['x'] - r), f(p['y'] - r), f(2 * r), f(2 * r)))
            else:
                a.append('<circle cx="%s" cy="%s" r="%s" fill="#c9a227"/>' % (f(p['x']), f(p['y']), f(r)))
            a.append('<circle cx="%s" cy="%s" r="%s" fill="#0d2413"/>' % (f(p['x']), f(p['y']), f(min(p['drill'] / 2, r * 0.7))))

    def P(ref, num):
        return next(p for p in pads[ref] if p['num'] == num)

    # ---- things that stand on the board, drawn over the copper ----------
    # screw terminals: blue blocks with a screw over each pad
    for ref, n in (('J1', 2), ('J3', 2), ('J4', 4)):
        p1, pn = P(ref, '1'), P(ref, str(n))
        a.append('<rect x="%s" y="%s" width="%s" height="8" rx="0.6" fill="#2c6fb8" stroke="#1b4a7d" stroke-width="0.3"/>'
                 % (f(p1['x'] - 2.6), f(p1['y'] - 4.4), f(pn['x'] - p1['x'] + 5.2)))
        for i in range(1, n + 1):
            q = P(ref, str(i))
            a.append('<circle cx="%s" cy="%s" r="1.7" fill="#c8ccd0" stroke="#6b7075" stroke-width="0.25"/>'
                     '<line x1="%s" y1="%s" x2="%s" y2="%s" stroke="#6b7075" stroke-width="0.35"/>'
                     % (f(q['x']), f(q['y'] - 1.2), f(q['x'] - 1.2), f(q['y'] - 1.2), f(q['x'] + 1.2), f(q['y'] - 1.2)))
            a.append('<rect x="%s" y="%s" width="2.6" height="1.6" fill="#0f2c4d"/>' % (f(q['x'] - 1.3), f(q['y'] + 1.4)))
    # pin headers the harnesses plug onto (black shroud, gold pins)
    for ref, n in (('J7', 5), ('J8', 4), ('J9', 5), ('J10', 4), ('J11', 6), ('J5', 2)):
        ps = [P(ref, str(i)) for i in range(1, n + 1)]
        xs = [q['x'] for q in ps]; ys = [q['y'] for q in ps]
        a.append('<rect x="%s" y="%s" width="%s" height="%s" fill="#1a1a1a"/>'
                 % (f(min(xs) - 1.27), f(min(ys) - 1.27), f(max(xs) - min(xs) + 2.54), f(max(ys) - min(ys) + 2.54)))
        for q in ps:
            a.append('<rect x="%s" y="%s" width="0.64" height="0.64" fill="#e3c35a"/>' % (f(q['x'] - 0.32), f(q['y'] - 0.32)))
    # USB-A receptacle at J2, mouth to the board edge
    j2 = P('J2', '1'); j2b = P('J2', '4')
    ux = (j2['x'] + j2b['x']) / 2
    a.append('<rect x="%s" y="-1.5" width="14" height="14" rx="0.5" fill="#b9bec4" stroke="#6b7075" stroke-width="0.3"/>'
             '<rect x="%s" y="-1.5" width="10" height="3" fill="#2a2a2a"/>' % (f(ux - 7), f(ux - 5)))
    # the modules, plugged in
    j20 = pads['J20']; j21 = pads['J21']
    ex0 = min(p['x'] for p in j20) - 3; ex1 = max(p['x'] for p in j20) + 3
    ey0 = j20[0]['y'] - 3; ey1 = j21[0]['y'] + 3
    a.append('<rect x="%s" y="%s" width="%s" height="%s" rx="1" fill="#1b1b1b" stroke="#000" stroke-width="0.3"/>'
             % (f(ex0), f(ey0), f(ex1 - ex0), f(ey1 - ey0)))
    a.append('<rect x="%s" y="%s" width="18" height="16" fill="#9aa3ab" stroke="#6b7075" stroke-width="0.3"/>'
             % (f(ex1 - 24), f(ey0 + 4.7)))
    a.append('<rect x="%s" y="%s" width="20" height="11" fill="#0b2e4f"/>' % (f(ex0 + 8), f(ey0 + 7)))
    a.append(text(ex0 + 18, ey0 + 13.6, 'OLED', 2.2, '#7fd0ff'))
    a.append(text(ex1 - 15, ey0 + 13.6, 'ESP32', 2.2, '#2a2a2a'))
    a.append('<rect x="%s" y="%s" width="8" height="5" fill="#b9bec4"/>' % (f(ex0 - 2), f((ey0 + ey1) / 2 - 2.5)))
    a.append(text(ex0 + 1, ey1 - 1.2, 'USB <<<', 1.8, '#e0e0e0', 'start'))
    t0 = pads['J30'][0]; t1 = pads['J31'][-1]
    a.append('<rect x="%s" y="%s" width="%s" height="%s" rx="0.6" fill="#6a2c91" stroke="#3f1a57" stroke-width="0.3"/>'
             % (f(t0['x'] - 2), f(t0['y'] - 2), f(t1['x'] - t0['x'] + 4), f(t1['y'] - t0['y'] + 4)))
    a.append('<rect x="%s" y="%s" width="9" height="9" fill="#7d8388" stroke="#4d5256" stroke-width="0.3"/>'
             % (f((t0['x'] + t1['x']) / 2 - 4.5), f((t0['y'] + t1['y']) / 2 - 4.5)))
    a.append(text((t0['x'] + t1['x']) / 2, t1['y'] + 0.6, 'TMC2209', 1.9))
    # labels for the connectors the harnesses use
    for ref, dx, dy, s in (('J1', 2.5, 7.8, 'J1 12V IN'), ('J4', 7.5, -5.6, 'J4 MOTOR'), ('J7', 5, -3.6, 'J7 AS5600'),
                           ('J9', 5, -3.6, 'J9 GPS'), ('J2', 3, 16, 'J2 CAMERA')):
        q = P(ref, '1')
        a.append('<rect x="%s" y="%s" width="%s" height="3.4" rx="0.6" fill="#ffffff" opacity="0.92"/>'
                 % (f(q['x'] + dx - len(s) * 0.9), f(q['y'] + dy - 2.6), f(len(s) * 1.8)))
        a.append(text(q['x'] + dx, q['y'] + dy, s, 2.2, '#123a1c'))

    p = Part('controller', 'Star tracker controller (Rev B)', 'U', W, H, ''.join(a),
             'star_tracker_ctrl, 150 x 100 mm, drawn from the KiCad layout. Modules shown fitted.')
    names = {'J1': ['+12V', 'GND'], 'J4': ['A1', 'A2', 'B1', 'B2'], 'J7': ['GND', '3V3', 'SDA', 'SCL', 'DIR'],
             'J9': ['PPS', 'VCC', 'RXD', 'TXD', 'GND']}
    # schematic rows, chosen so each external symbol fits beside its group
    # without touching the next: J1 at 0 and 3 (the GND line passes under
    # the fuse), J7 from 7 (the AS5600 carries two unused pins below), J9
    # from 17; on the right J4 from 0 and the camera at 8
    rows = {'J1': [0, 3], 'J7': [7, 8, 9, 10, 11], 'J9': [17, 18, 19, 20, 21], 'J4': [0, 1, 2, 3]}
    for ref in ('J1', 'J7', 'J9', 'J4'):
        for i, n in enumerate(names[ref]):
            q = P(ref, str(i + 1))
            p.conn('%s.%d %s' % (ref, i + 1, n), '%s pin %d, %s' % (ref, i + 1, n), q['x'], q['y'],
                   'R' if ref == 'J4' else 'L', rows[ref][i])
    p.conn('J2 USB-A', 'camera 5 V, switched', ux, 0.0, 'R', 8)
    return p


def battery():
    a = ('<rect x="0" y="4" width="120" height="58" rx="3" fill="#2b2f33" stroke="#111" stroke-width="0.5"/>'
         '<rect x="6" y="14" width="108" height="38" rx="2" fill="#3c8a3f"/>'
         + text(60, 30, 'LiFePO4  12.8 V  6 Ah', 5.2) + text(60, 40, 'with BMS', 3.4, '#e8f5e9', weight='normal')
         + '<rect x="12" y="0" width="12" height="6" rx="1" fill="#c62828"/><rect x="96" y="0" width="12" height="6" rx="1" fill="#222"/>'
         + text(18, 12, '+', 5, '#ff8a80') + text(102, 12, '-', 6))
    p = Part('battery', 'Battery 12 V LiFePO4', 'BT', 120, 62, a, 'LiFePO4 12.8 V nominal, 14.6 V full')
    p.conn('+', 'positive', 18, 2, 'R', 0)
    p.conn('-', 'negative', 102, 2, 'R', 3)
    return p


def fuse():
    a = ('<rect x="6" y="2" width="30" height="10" rx="5" fill="#1d1d1d"/><rect x="14" y="4.5" width="14" height="5" rx="1" fill="#e9c46a" opacity="0.9"/>'
         + text(21, 8.3, '3 A', 3, '#1d1d1d') + '<line x1="0" y1="7" x2="6" y2="7" stroke="#c62828" stroke-width="1.6"/>'
         '<line x1="36" y1="7" x2="42" y2="7" stroke="#c62828" stroke-width="1.6"/>')
    p = Part('fuse', 'Inline fuse holder, 3 A', 'F', 42, 14, a, 'blade or 5x20 inline holder, 3 A')
    p.conn('in', 'battery side', 0.6, 7, 'L', 0)
    p.conn('out', 'board side', 41.4, 7, 'R', 0)
    return p


def motor():
    a = ('<rect x="0" y="0" width="42" height="42" rx="4" fill="#2a2a2a" stroke="#000" stroke-width="0.4"/>'
         '<rect x="3" y="3" width="36" height="36" rx="2" fill="#3d3d3d"/>'
         '<circle cx="21" cy="21" r="11" fill="#9aa3ab" stroke="#6b7075" stroke-width="0.4"/><circle cx="21" cy="21" r="2.5" fill="#dfe3e6"/>'
         + ''.join('<circle cx="%s" cy="%s" r="1.6" fill="#111"/>' % (f(x), f(y)) for x, y in ((5.5, 5.5), (36.5, 5.5), (5.5, 36.5), (36.5, 36.5)))
         + text(21, 40.5, 'NEMA 17 pancake', 2.4)
         + '<rect x="-6" y="14" width="6" height="14" fill="#e8e8e8" stroke="#999" stroke-width="0.3"/>')
    cols = (BLACK, GREEN, RED, BLUE)
    tails = ''
    for i, c in enumerate(cols):
        y = 15.5 + i * 3.6
        tails += '<line x1="-6" y1="%s" x2="-14" y2="%s" stroke="%s" stroke-width="1.3"/>' % (f(y), f(y), c)
    p = Part('motor', 'Stepper, NEMA 17 pancake', 'M', 42 + 14, 42, '<g transform="translate(14 0)">' + a + tails + '</g>',
             'bipolar, 0.7 A. Lead colours vary: find the pairs with a meter.')
    for i, n in enumerate(('A1', 'A2', 'B1', 'B2')):
        p.conn(n, 'coil %s' % n[0], 0.6, 15.5 + i * 3.6, 'L', i)
    return p


def as5600():
    a = ('<rect x="0" y="0" width="23" height="23" rx="1.5" fill="#6b3fa0" stroke="#43246b" stroke-width="0.4"/>'
         '<circle cx="3" cy="19.5" r="1.4" fill="#ffffff"/><circle cx="20" cy="19.5" r="1.4" fill="#ffffff"/>'
         '<rect x="8.5" y="11" width="6" height="6" fill="#111"/>' + text(11.5, 21.5, 'AS5600', 2)
         + '<rect x="1.8" y="1.2" width="19.4" height="3.4" fill="#1a1a1a"/>')
    pins = ('VCC', 'OUT', 'GND', 'DIR', 'SCL', 'SDA', 'GPO')
    for i, n in enumerate(pins):
        x = 3.0 + i * 2.54
        a += '<rect x="%s" y="2.58" width="0.64" height="0.64" fill="#e3c35a"/>' % f(x - 0.32) + text(x, 7.4, n, 1.2, '#ffffff', weight='normal', rot=-90)
    p = Part('as5600', 'AS5600 breakout', 'U', 23, 23, a, 'generic 7-pin AS5600 magnetic encoder board; on the bracket')
    order = {'GND': 0, 'VCC': 1, 'SDA': 2, 'SCL': 3, 'DIR': 4}
    for i, n in enumerate(pins):
        side, slot = ('R', order[n]) if n in order else ('R', {'OUT': 5, 'GPO': 6}[n])
        p.conn(n, n, 3.0 + i * 2.54, 2.9, side, slot)
    return p


def gps():
    a = ('<rect x="0" y="0" width="25" height="36" rx="1.5" fill="#1f5fa8" stroke="#123d6d" stroke-width="0.4"/>'
         '<rect x="5" y="9" width="15" height="15" fill="#c9ced3" stroke="#8c9399" stroke-width="0.3"/>'
         + text(12.5, 17.5, 'NEO-6M', 2.2, '#2a2a2a') + text(12.5, 32, 'GY-NEO6MV2', 2)
         + '<circle cx="21" cy="28" r="1.2" fill="#ffd54f"/>' + text(21, 31, 'PPS LED', 1.2, '#ffffff', weight='normal')
         + '<rect x="7.2" y="1.2" width="10.6" height="3.4" fill="#1a1a1a"/>')
    for i, n in enumerate(('VCC', 'RX', 'TX', 'GND')):
        x = 8.69 + i * 2.54
        a += '<rect x="%s" y="2.58" width="0.64" height="0.64" fill="#e3c35a"/>' % f(x - 0.32) + text(x, 7.4, n, 1.2, '#ffffff', weight='normal', rot=-90)
    a += '<circle cx="23.2" cy="28" r="0.7" fill="#e3c35a"/>'   # the PPS tap, flying lead lands here
    p = Part('gps', 'GPS, GY-NEO6MV2', 'U', 25, 36, a, 'u-blox NEO-6M. PPS is not on the header: tap it at the PPS LED.')
    p.conn('PPS', 'flying lead from the PPS LED', 23.2, 28, 'R', 0)
    for i, n in enumerate(('VCC', 'RX', 'TX', 'GND')):
        p.conn(n, n, 8.69 + i * 2.54, 2.9, 'R', i + 1)
    return p


def gopro():
    a = ('<rect x="0" y="0" width="62" height="45" rx="4" fill="#1b1b1b" stroke="#000" stroke-width="0.4"/>'
         '<rect x="4" y="4" width="54" height="37" rx="2" fill="#262626"/>'
         '<circle cx="18" cy="20" r="11" fill="#111" stroke="#555" stroke-width="0.8"/><circle cx="18" cy="20" r="6.5" fill="#1c2a3a"/>'
         '<circle cx="15.5" cy="17.5" r="1.6" fill="#8fb3d9" opacity="0.7"/>'
         '<rect x="36" y="9" width="17" height="11" rx="1" fill="#0e0e0e" stroke="#444" stroke-width="0.3"/>'
         + text(44.5, 16, 'HERO6', 2.6, '#8fb3d9') + text(45, 34, 'GoPro', 3.4, '#dddddd')
         + '<rect x="-1.5" y="30" width="3" height="7" rx="1" fill="#555"/>' + text(4, 44, 'USB-C', 1.8, '#bbbbbb', 'start', 'normal'))
    p = Part('gopro', 'GoPro HERO6', 'CAM', 62, 45, a, 'controlled over WiFi gpControl; powered from J2')
    p.conn('USB-C', 'USB-C power in', 0, 33.5, 'L', 0)
    return p


# --------------------------------------------------------------------------
# the sketch
# --------------------------------------------------------------------------
class Sketch:
    def __init__(self):
        self.idx = 5000
        self.inst = []        # (part, model index, bb xy mm, sch xy mm, pcb xy mm)
        self.links = {}       # (model idx, connector id) -> list of (other idx, other conn, view)
        self.wires = []

    def nid(self):
        self.idx += 1
        return self.idx

    def place(self, part, bb, sch, pcbxy):
        mi = self.nid()
        self.inst.append(dict(part=part, mi=mi, bb=bb, sch=sch, pcb=pcbxy))
        return mi

    def find(self, mi):
        return next(i for i in self.inst if i['mi'] == mi)

    def pin_bb(self, mi, cname):
        i = self.find(mi); c = next(k for k in i['part'].conns if k['name'] == cname)
        return (i['bb'][0] + c['x']) * MM, (i['bb'][1] + c['y']) * MM, c['id']

    def pin_sch(self, mi, cname):
        i = self.find(mi); c = next(k for k in i['part'].conns if k['name'] == cname)
        tx, ty = i['part'].sch_terminal(c['id'])
        return (i['sch'][0] + tx) * MM, (i['sch'][1] + ty) * MM, c['id']

    def link(self, a, ac, b, bc, view):
        self.links.setdefault((a, ac, view), []).append((b, bc))

    def wire(self, a, an, b, bn, color, sag=0.0, mils=22.2222):
        """one breadboard wire (curved) and one schematic trace, same two pins"""
        x0, y0, ac = self.pin_bb(a, an)
        x1, y1, bc = self.pin_bb(b, bn)
        wb = self.nid()
        dx, dy = x1 - x0, y1 - y0
        self.wires.append(dict(kind='bb', mi=wb, x=x0, y=y0, dx=dx, dy=dy, color=color, mils=mils,
                               cp=((dx * 0.3, dy * 0.3 + sag * MM), (dx * 0.7, dy * 0.7 + sag * MM)),
                               a=(a, ac), b=(b, bc)))
        self.link(a, ac, wb, 'connector0', 'bb'); self.link(b, bc, wb, 'connector1', 'bb')
        sx0, sy0, _ = self.pin_sch(a, an)
        sx1, sy1, _ = self.pin_sch(b, bn)
        ws = self.nid()
        self.wires.append(dict(kind='sch', mi=ws, x=sx0, y=sy0, dx=sx1 - sx0, dy=sy1 - sy0, a=(a, ac), b=(b, bc)))
        self.link(a, ac, ws, 'connector0', 'sch'); self.link(b, bc, ws, 'connector1', 'sch')

    # ---- XML ---------------------------------------------------------------
    def conn_xml(self, mi, view, layer, wlayer):
        out = []
        for (m, c, v), lst in sorted(self.links.items()):
            if m != mi or v != view:
                continue
            cs = ''.join('<connect connectorId="%s" modelIndex="%d" layer="%s"/>' % (oc, om, wlayer) for om, oc in lst)
            out.append('<connector connectorId="%s" layer="%s"><geometry x="0" y="0"/><connects>%s</connects></connector>' % (c, layer, cs))
        return '<connectors>%s</connectors>' % ''.join(out) if out else ''

    def fz(self):
        parts = []
        for n, i in enumerate(self.inst):
            p = i['part']
            parts.append('''<instance moduleIdRef="%s" modelIndex="%d" path="%s">
  <title>%s%d</title>
  <views>
   <breadboardView layer="breadboard"><geometry z="%s" x="%s" y="%s"/>%s</breadboardView>
   <schematicView layer="schematic"><geometry z="%s" x="%s" y="%s"/>%s</schematicView>
   <pcbView layer="copper0"><geometry z="%s" x="%s" y="%s"/></pcbView>
  </views>
 </instance>''' % (p.module_id, i['mi'], p.module_id + '.fzp', p.label, n + 1,
                   f(2.5 + n * 0.001), f(i['bb'][0] * MM), f(i['bb'][1] * MM), self.conn_xml(i['mi'], 'bb', 'breadboard', 'breadboardWire'),
                   f(2.5 + n * 0.001), f(i['sch'][0] * MM), f(i['sch'][1] * MM), self.conn_xml(i['mi'], 'sch', 'schematic', 'schematicTrace'),
                   f(1.5 + n * 0.001), f(i['pcb'][0] * MM), f(i['pcb'][1] * MM)))
        wires = []
        for n, w in enumerate(self.wires):
            (am, ac), (bm, bc) = w['a'], w['b']
            if w['kind'] == 'bb':
                view, layer, plyr, flags = 'breadboardView', 'breadboardWire', 'breadboardbreadboard', 64
                extras = ('<wireExtras mils="%s" color="%s" opacity="1" banded="0"><bezier><cp0 x="%s" y="%s"/><cp1 x="%s" y="%s"/></bezier></wireExtras>'
                          % (f(w['mils']), w['color'], f(w['cp'][0][0]), f(w['cp'][0][1]), f(w['cp'][1][0]), f(w['cp'][1][1])))
                z = 3.5 + n * 0.0001
            else:
                view, layer, plyr, flags = 'schematicView', 'schematicTrace', 'schematic', 128
                extras = '<wireExtras mils="9.7222" color="#404040" opacity="1" banded="0"/>'
                z = 5.5 + n * 0.0001
            wires.append('''<instance moduleIdRef="WireModuleID" modelIndex="%d" path="wire.fzp">
  <title>Wire%d</title>
  <views>
   <%s layer="%s">
    <geometry z="%s" x="%s" y="%s" x1="0" y1="0" x2="%s" y2="%s" wireFlags="%d"/>
    %s
    <connectors>
     <connector connectorId="connector0" layer="%s"><geometry x="0" y="0"/><connects><connect connectorId="%s" modelIndex="%d" layer="%s"/></connects></connector>
     <connector connectorId="connector1" layer="%s"><geometry x="0" y="0"/><connects><connect connectorId="%s" modelIndex="%d" layer="%s"/></connects></connector>
    </connectors>
   </%s>
  </views>
 </instance>''' % (w['mi'], n + 1, view, layer, f(z), f(w['x']), f(w['y']), f(w['dx']), f(w['dy']), flags, extras,
                   layer, ac, am, plyr, layer, bc, bm, plyr, view))
        return '''<?xml version="1.0" encoding="UTF-8"?>
<module fritzingVersion="1.0.8">
 <views>
  <view name="breadboardView" backgroundColor="#ffffff" gridSize="0.1in" showGrid="0" alignToGrid="0" viewFromBelow="0"/>
  <view name="schematicView" backgroundColor="#ffffff" gridSize="0.1in" showGrid="0" alignToGrid="0" viewFromBelow="0"/>
  <view name="pcbView" backgroundColor="#333333" gridSize="0.05in" showGrid="0" alignToGrid="0" viewFromBelow="0"/>
 </views>
 <title>star_tracker_wiring</title>
 <instances>
%s
%s
 </instances>
</module>
''' % ('\n'.join(parts), '\n'.join(wires))


def main():
    S = Sketch()
    ctrl, bat, fu, mot, enc, gp, cam = controller(), battery(), fuse(), motor(), as5600(), gps(), gopro()

    # ---- schematic placement: every external pin level with its board pin
    cx, cy = 0.0, 0.0
    cg = ctrl.sch_geom()

    def level(part, pname, bname, left):
        """x,y for part so that part's pin pname sits level with board pin bname"""
        bc = next(k for k in ctrl.conns if k['name'] == bname)
        pc = next(k for k in part.conns if k['name'] == pname)
        _, by = ctrl.sch_terminal(bc['id']); _, py = part.sch_terminal(pc['id'])
        pg = part.sch_geom()
        x = cx - 12 * SCH_P - pg['W'] if left else cx + cg['W'] + 12 * SCH_P
        return x, cy + by - py

    s_fuse = level(fu, 'out', 'J1.1 +12V', True)
    s_bat = (s_fuse[0] - 10 * SCH_P - bat.sch_geom()['W'],
             cy + ctrl.sch_terminal(ctrl.conns[0]['id'])[1] - bat.sch_terminal(bat.conns[0]['id'])[1])
    s_enc = level(enc, 'GND', 'J7.1 GND', True)
    s_gps = level(gp, 'PPS', 'J9.1 PPS', True)
    s_mot = level(mot, 'A1', 'J4.1 A1', False)
    s_cam = level(cam, 'USB-C', 'J2 USB-A', False)

    # ---- breadboard placement (mm), board at the origin
    mi_ctrl = S.place(ctrl, (0, 0), (cx, cy), (0, 0))
    mi_bat = S.place(bat, (-205, 22), s_bat, (-80, 0))
    mi_fu = S.place(fu, (-66, -16), s_fuse, (-40, 0))
    mi_mot = S.place(mot, (178, 22), s_mot, (170, 0))
    mi_enc = S.place(enc, (-18, 128), s_enc, (0, 120))
    mi_gps = S.place(gp, (66, 128), s_gps, (40, 120))
    mi_cam = S.place(cam, (96, -92), s_cam, (100, -60))

    # ---- the harnesses
    S.wire(mi_bat, '+', mi_fu, 'in', RED, sag=-14)
    S.wire(mi_fu, 'out', mi_ctrl, 'J1.1 +12V', RED, sag=10)
    S.wire(mi_bat, '-', mi_ctrl, 'J1.2 GND', BLACK, sag=6)
    for n, c in zip(('A1', 'A2', 'B1', 'B2'), (BLACK, GREEN, RED, BLUE)):
        S.wire(mi_ctrl, 'J4.%d %s' % ('A1 A2 B1 B2'.split().index(n) + 1, n), mi_mot, n, c, sag=6)
    for bn, pn, c in (('J7.1 GND', 'GND', BLACK), ('J7.2 3V3', 'VCC', RED), ('J7.3 SDA', 'SDA', BLUE),
                      ('J7.4 SCL', 'SCL', YELLOW), ('J7.5 DIR', 'DIR', GREY)):
        S.wire(mi_ctrl, bn, mi_enc, pn, c, sag=8)
    for bn, pn, c in (('J9.1 PPS', 'PPS', ORANGE), ('J9.2 VCC', 'VCC', RED), ('J9.3 RXD', 'RX', GREEN),
                      ('J9.4 TXD', 'TX', PURPLE), ('J9.5 GND', 'GND', BLACK)):
        S.wire(mi_ctrl, bn, mi_gps, pn, c, sag=8)
    S.wire(mi_ctrl, 'J2 USB-A', mi_cam, 'USB-C', '#303030', sag=-30, mils=60)

    parts = [ctrl, bat, fu, mot, enc, gp, cam]
    with zipfile.ZipFile(OUT, 'w', zipfile.ZIP_DEFLATED) as z:
        z.writestr('star_tracker_wiring.fz', S.fz())
        for p in parts:
            z.writestr('part.%s.fzp' % p.module_id, p.fzp())
            z.writestr('svg.breadboard.%s' % p.fn('breadboard'), p.bb_svg())
            z.writestr('svg.icon.%s' % p.fn('icon'), p.bb_svg('icon'))
            z.writestr('svg.schematic.%s' % p.fn('schematic'), p.sch_svg())
            z.writestr('svg.pcb.%s' % p.fn('pcb'), p.pcb_svg())
    print('wrote %s  (%d parts, %d wires)' % (OUT, len(parts), len(S.wires)))


if __name__ == '__main__':
    main()
