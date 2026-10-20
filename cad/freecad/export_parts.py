#!/usr/bin/env python3
"""
export_parts.py -- one STL per component of the gear assembly, each already in
its assembled position, for the FreeCAD model (build_freecad.py).

    python cad/freecad/export_parts.py

It copies star_tracker_gears.scad without its `part ==` menu, appends a
`comp ==` menu that places every component the way assembly() and os_stack()
do (os_stack is built at the origin; here it is moved to shaft 4), and runs
OpenSCAD once per component, in parallel. The .scad stays the single source of
truth: change it, rerun this, rebuild the FreeCAD file.

Writes cad/freecad/_build/<comp>.stl and parts.json (name, label, colour, group).
"""
import concurrent.futures as cf
import json
import os
import re
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.normpath(os.path.join(HERE, '..', '..'))
SCAD = os.path.join(REPO, 'star_tracker_gears.scad')
BUILD = os.path.join(HERE, '_build')
OPENSCAD = r'C:\Program Files\OpenSCAD\openscad.exe'

# name, label, colour, group, OpenSCAD body (s4 = translate to shaft 4)
S4 = 'translate([shaftpos(4)[0], shaftpos(4)[1], 0])'
COMPS = [
    ('baseplate', 'Baseplate 6061, 4.3 mm', '#c8c8c0', 'Frame', 'baseplate();'),
    ('motor', 'NEMA 17 pancake (envelope)', '#3a3d42', 'Frame',
     # 42.3 square x 20 deep under the plate, square to it (the plate's bolts sit
     # at 45 deg, i.e. at the corners of the 31 mm square), 22 mm pilot boss in its hole
     'translate([shaftpos(0)[0], shaftpos(0)[1], -bp_t]) { translate([-21.15,-21.15,-20]) cube([42.3,42.3,20]);'
     ' translate([0,0,-0.01]) cylinder(d=22, h=2, $fn=64); }'),
] + [
    ('shaft%d' % i, 'Shaft %d, 5 mm' % i, '#9aa0a6', 'Shafts',
     'translate([shaftpos(%d)[0], shaftpos(%d)[1], 0]) translate([0,0,-4]) cylinder(d=5, h=gear_top(%d)+7, $fn=32);' % (i, i, i))
    for i in range(4)
] + [
    ('spacer%d' % i, 'Spacer, shaft %d' % i, '#4a4a4a', 'Spacers',
     'translate([shaftpos(%d)[0], shaftpos(%d)[1], 0]) spacer(sp_h(%d));' % (i, i, i))
    for i in range(1, 4)       # shaft 0 is the motor's own shaft: the pinion sits on it, no spacer
] + [
    ('pinion', 'Motor pinion, 15T m1', '#8d8d8d', 'Gear train',
     'translate([shaftpos(0)[0], shaftpos(0)[1], sp_clr]) motor_pinion();'),
    ('stage1', 'Stage gear 1, 120T/15T m1', '#7fb2e5', 'Gear train',
     'translate([shaftpos(1)[0], shaftpos(1)[1], sp_clr]) stage_gear();'),
    ('stage2', 'Stage gear 2, 120T/15T m1', '#3d85c6', 'Gear train',
     'translate([shaftpos(2)[0], shaftpos(2)[1], sp_clr + sp_step()]) stage_gear();'),
    ('stage3', 'Final stage gear, 120T m1 / 24T m2', '#1c4587', 'Gear train',
     'translate([shaftpos(3)[0], shaftpos(3)[1], sp_clr + 2*sp_step()]) final_stage_gear();'),
    ('wheel', 'Output wheel, 96T m2', '#d97b3f', 'Gear train',
     S4 + ' translate([0,0,os_zw()]) output_wheel(zwf, mod_f);'),
    # the output stack, as os_stack() draws it, moved to shaft 4
    ('outshaft', 'Output shaft, 5 mm ground, 55 mm', '#9aa0a6', 'Output stack',
     S4 + ' translate([0,0,-33]) cylinder(d=os_shaft_d, h=os_shaft_len, $fn=48);'),
    ('bearing_lo', '625ZZ bearing, lower', '#5a5f66', 'Output stack',
     S4 + ' translate([0,0,-21]) difference() { cylinder(d=os_brg_od, h=os_brg_w, $fn=64); translate([0,0,-1]) cylinder(d=os_shaft_d, h=os_brg_w+2, $fn=48); }'),
    ('bearing_hi', '625ZZ bearing, upper', '#5a5f66', 'Output stack',
     S4 + ' translate([0,0,9]) difference() { cylinder(d=os_brg_od, h=os_brg_w, $fn=64); translate([0,0,-1]) cylinder(d=os_shaft_d, h=os_brg_w+2, $fn=48); }'),
    ('tower', 'Output bearing tower', '#7fb2e5', 'Output stack', S4 + ' output_bearing_tower();'),
    ('washer', 'Hub washer', '#e0c060', 'Output stack', S4 + ' translate([0,0,14]) hub_washer();'),
    ('hub_lo', 'Output hub, lower', '#3d85c6', 'Output stack', S4 + ' output_hub_lower();'),
    ('hub_hi', 'Output hub, upper', '#2f6fa8', 'Output stack', S4 + ' output_hub_upper();'),     # carries its own height, as in os_stack()
    ('magcap', 'Magnet cap', '#7fb2e5', 'Encoder', S4 + ' magnet_cap();'),
    ('magnet', 'Diametric magnet 6 x 2.5', '#a48fd0', 'Encoder',
     S4 + ' translate([0,0,-36.9]) cylinder(d=os_mag_d, h=os_mag_h, $fn=48);'),
    ('bracket', 'AS5600 bracket', '#aeb6c2', 'Encoder', S4 + ' as5600_bracket();'),
    ('as5600', 'AS5600 breakout board', '#2f7d4f', 'Encoder',
     S4 + ' translate([-10,-7.5,-41.6]) cube([20,15,1.6]); ' + S4 + ' translate([-3,-3,-40]) cube([6,6,1.0]);'),
]


def scad_source():
    src = open(SCAD, encoding='utf8').read()
    a = src.index('if      (part == "train")')
    b = src.index('test_pair(zp, zw, $t*360);') + len('test_pair(zp, zw, $t*360);')
    menu = '\n'.join('%sif (comp == "%s") { %s }' % ('' if i == 0 else 'else ', c[0], c[4]) for i, c in enumerate(COMPS))
    return src[:a] + '// (part menu removed by cad/freecad/export_parts.py)\ncomp = "";\n' + menu + '\n' + src[b:]


def export(c):
    out = os.path.join(BUILD, c[0] + '.stl')
    r = subprocess.run([OPENSCAD, '-o', out, '--export-format', 'binstl', '-D', 'comp="%s"' % c[0],
                        os.path.join(BUILD, 'assembly_parts.scad')], capture_output=True, text=True)
    ok = r.returncode == 0 and os.path.exists(out) and os.path.getsize(out) > 84
    return c[0], ok, '' if ok else (r.stderr[-400:])


def main():
    os.makedirs(BUILD, exist_ok=True)
    with open(os.path.join(BUILD, 'assembly_parts.scad'), 'w', encoding='utf8') as fh:
        fh.write(scad_source())
    only = set(sys.argv[1:])                     # e.g. python export_parts.py motor redoes one part
    with cf.ThreadPoolExecutor(max_workers=6) as ex:
        res = list(ex.map(export, [c for c in COMPS if not only or c[0] in only]))
    for name, ok, err in res:
        print('%-11s %s %s' % (name, 'ok' if ok else 'FAILED', err))
    json.dump([dict(name=c[0], label=c[1], color=c[2], group=c[3]) for c in COMPS],
              open(os.path.join(BUILD, 'parts.json'), 'w'), indent=1)
    if not all(ok for _, ok, _ in res):
        raise SystemExit('some components failed')


if __name__ == '__main__':
    main()
