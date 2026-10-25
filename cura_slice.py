#!/usr/bin/env python3
"""
cura_slice.py -- slice every printable STL in out/ with CuraEngine, using the
printer, quality, material and nozzle that the Cura GUI has active, and write
out/gcode/<name>.gcode.

    python cura_slice.py                       # all of out/*.stl
    python cura_slice.py stage pinion          # names containing these
    python cura_slice.py --set infill_sparse_density=15 gopro
    python cura_slice.py --machine "Creality Ender-3 / Ender-3 v2"
    python cura_slice.py --dump                # print the resolved settings

Why this is more than one CuraEngine call: CuraEngine only reads the plain
`default_value` of each setting. The GUI builds a stack -- user, quality
changes, intent, quality, material, nozzle variant, machine changes, machine
definition, fdmprinter -- and evaluates the Python formulas in it (wall
count from wall thickness, speeds from speed_print, and so on). This script
rebuilds that stack from %APPDATA%\\cura\\<ver> and the Cura install, evaluates
the formulas, and hands CuraEngine every value that differs from fdmprinter's
defaults. Change a setting in the Cura GUI (as a custom profile or just in
the panel) and the next run here picks it up.

Single-extruder machines only.
"""
import argparse
import ast
import configparser
import datetime
import glob
import json
import math
import os
import re
import subprocess
import sys
import urllib.parse
import xml.etree.ElementTree as ET

HERE = os.path.dirname(os.path.abspath(__file__))
OUT_STL = os.path.join(HERE, 'out')
OUT_GCODE = os.path.join(OUT_STL, 'gcode')

# Not printed, or not on this printer.
SKIP = ('baseplate_',)

# Per-part fixes. The .scad draws most parts in their print orientation; the
# lower tower half is drawn as assembled, plate-facing face UP (PRINTING.md:
# "plate-facing face down").
FLIP = {'output_tower_LOWER.stl'}
PER_PART = {
    # 'gopro_base_adapter.stl': {'infill_sparse_density': 15},
}

# XmlMaterialProfile's key map (the parts that matter for FDM on one extruder)
MATERIAL_KEYS = {
    'print temperature': 'default_material_print_temperature',
    'heated bed temperature': 'default_material_bed_temperature',
    'standby temperature': 'material_standby_temperature',
    'print cooling': 'cool_fan_speed',
    'retraction amount': 'retraction_amount',
    'retraction speed': 'retraction_speed',
    'adhesion tendency': 'material_adhesion_tendency',
    'surface energy': 'material_surface_energy',
    'build volume temperature': 'build_volume_temperature',
    'shrinkage percentage': 'material_shrinkage_percentage',
}


# ---- finding Cura -------------------------------------------------------
def find_cura():
    hits = sorted(glob.glob(r'C:\Program Files\UltiMaker Cura *\CuraEngine.exe'))
    if not hits:
        raise SystemExit('CuraEngine.exe not found under C:\\Program Files\\UltiMaker Cura *')
    eng = hits[-1]
    root = os.path.dirname(eng)
    ver = re.search(r'Cura (\d+\.\d+)', root).group(1)
    cfg = os.path.join(os.environ['APPDATA'], 'cura', ver)
    return eng, os.path.join(root, 'share', 'cura', 'resources'), cfg


def index_containers(res, cfg):
    """container id -> file. The user's config dir wins over the install."""
    idx = {}
    suffixes = ('.inst.cfg', '.def.json', '.xml.fdm_material', '.global.cfg', '.extruder.cfg')
    for base in (res, cfg):
        for p in glob.glob(os.path.join(base, '**', '*'), recursive=True):
            n = os.path.basename(p)
            for s in suffixes:
                if n.endswith(s):
                    idx[urllib.parse.unquote_plus(n[:-len(s)])] = p
    return idx


def read_cfg(p):
    c = configparser.ConfigParser(interpolation=None, strict=False)
    c.optionxform = str
    c.read(p, encoding='utf-8')
    return c


# ---- definitions --------------------------------------------------------
def flatten(tree, out):
    for k, v in tree.items():
        if v.get('type') != 'category':
            out[k] = {p: x for p, x in v.items() if p != 'children'}
        if 'children' in v:
            flatten(v['children'], out)


def load_definition(did, idx):
    """{key: merged properties} for a definition and everything it inherits."""
    d = json.load(open(idx[did], encoding='utf-8'))
    props = load_definition(d['inherits'], idx) if 'inherits' in d else {}
    flat = {}
    flatten(d.get('settings', {}), flat)
    for k, v in flat.items():
        props.setdefault(k, {}).update(v)
    for k, v in d.get('overrides', {}).items():
        props.setdefault(k, {}).update(v)
    return props


# ---- instance containers, as {key: raw string} --------------------------
def load_inst(cid, idx, machine_def):
    if not cid or cid.startswith('empty'):
        return {}
    p = idx.get(cid)
    if p is None:
        print('  (container %r not found, skipped)' % cid)
        return {}
    if p.endswith('.xml.fdm_material'):
        return load_material(p)
    c = read_cfg(p)
    return dict(c['values']) if c.has_section('values') else {}


def load_material(p):
    ns = {'m': 'http://www.ultimaker.com/material'}
    r = ET.parse(p).getroot()
    out = {}
    dia = r.find('m:properties/m:diameter', ns)
    if dia is not None:
        out['material_diameter'] = dia.text.strip()
    for s in r.findall('m:settings/m:setting', ns):          # top level only, not the per-machine blocks
        k = MATERIAL_KEYS.get(s.get('key'))
        if k:
            out[k] = s.text.strip()
    return out


# ---- evaluation ---------------------------------------------------------
class Stack:
    """Ordered layers, top wins. A layer is {key: raw} or the definition."""

    def __init__(self, layers, definition, ext_def):
        self.layers, self.defn, self.ext_def = layers, definition, ext_def
        self.cache, self.busy, self.errors = {}, set(), []

    def type_of(self, k):
        return self.defn.get(k, {}).get('type', 'str')

    def raw(self, k):
        for L in self.layers:
            if k in L:
                return L[k], True
        for d in (self.ext_def, self.defn):
            pr = d.get(k)
            if pr and ('value' in pr or 'default_value' in pr):
                if 'value' in pr:
                    v = pr['value']
                    return ('=' + v if isinstance(v, str) else v), False
                return pr['default_value'], False
        raise KeyError(k)

    def get(self, k):
        if k in self.cache:
            return self.cache[k]
        if k in self.busy:
            raise RecursionError(k)
        self.busy.add(k)
        try:
            v, from_inst = self.raw(k)
            if isinstance(v, str) and v.startswith('='):
                try:
                    v = self.evaluate(v[1:])
                except Exception as e:                    # keep going; fall back to the plain default
                    self.errors.append('%s: %s' % (k, e))
                    v = self.defn.get(k, {}).get('default_value')
            elif isinstance(v, str) and from_inst:
                v = self.parse(k, v)
            v = self.cast(k, v)
        finally:
            self.busy.discard(k)
        self.cache[k] = v
        return v

    def parse(self, k, s):
        t = self.type_of(k)
        if t in ('float', 'int'):
            return float(s)
        if t == 'bool':
            return s.strip().lower() in ('true', '1', 'yes', 'on')
        if t in ('polygon', 'polygons', '[int]') or s.strip().startswith('['):
            try:
                return ast.literal_eval(s)
            except Exception:
                return s
        return s

    def cast(self, k, v):
        t = self.type_of(k)
        try:
            if t == 'float':
                return float(v)
            if t == 'int':
                return int(round(float(v)))
            if t == 'bool':
                return bool(v)
            if t in ('extruder', 'optional_extruder'):
                return str(int(v))
        except (TypeError, ValueError):
            pass
        return v

    def evaluate(self, expr):
        names = {n.id for n in ast.walk(ast.parse(expr, mode='eval')) if isinstance(n, ast.Name)}
        g = dict(FUNCS)
        g.update({
            'math': math,
            'extruderValue': lambda i, k: self.get(k),
            'extruderValues': lambda k: [self.get(k)],
            'resolveOrValue': lambda k: self.get(k),
            'valueFromContainer': lambda i, k=None: self.get(k if k is not None else i),
            'extruderValueFromContainer': lambda e, k, i=None: self.get(k),
            'anyExtruderWithMaterial': lambda k: 0,
            'anyExtruderNrWithOrDefault': lambda k: 0,
            'defaultExtruderPosition': lambda: '0',
        })
        for n in names:
            if n not in g and (n in self.defn or n in self.ext_def):
                g[n] = self.get(n)
        return eval(compile(expr, '<setting>', 'eval'), g)


FUNCS = {n: getattr(__builtins__, n) if not isinstance(__builtins__, dict) else __builtins__[n]
         for n in ('abs', 'all', 'any', 'bool', 'enumerate', 'float', 'int', 'len', 'list', 'map',
                   'max', 'min', 'range', 'round', 'set', 'sorted', 'str', 'sum', 'tuple', 'zip', 'filter')}


def build_stack(cfg, idx, machine):
    gpath = idx.get(machine)
    if gpath is None or not gpath.endswith('.global.cfg'):
        raise SystemExit('machine %r not found in %s\\machine_instances' % (machine, cfg))
    g = read_cfg(gpath)
    gc = [g['containers'][str(i)] for i in range(len(g['containers']))]
    machine_def = gc[-1]
    # the extruder stack whose metadata names this machine
    ext = None
    for p in glob.glob(os.path.join(cfg, 'extruders', '*.extruder.cfg')):
        e = read_cfg(p)
        if e['metadata'].get('machine') == machine and e['metadata'].get('position', '0') == '0':
            ext = e
    if ext is None:
        raise SystemExit('no extruder stack for %r' % machine)
    ec = [ext['containers'][str(i)] for i in range(len(ext['containers']))]

    names = {'global': gc, 'extruder': ec}
    layers = [load_inst(c, idx, machine_def) for c in ec[:-1]] + \
             [load_inst(c, idx, machine_def) for c in gc[:-1]]
    defn = load_definition(machine_def, idx)
    ext_def = {k: v for k, v in load_definition(ec[-1], idx).items()
               if 'value' in v or 'default_value' in v}
    # fdmextruder re-declares a few fdmprinter settings with its own defaults; only
    # the extruder definition's own overrides should shadow the machine's chain
    own = json.load(open(idx[ec[-1]], encoding='utf-8')).get('overrides', {})
    ext_def = {k: v for k, v in ext_def.items() if k in own or k not in defn}
    return Stack(layers, defn, ext_def), names, machine_def


# ---- start/end g-code, as Cura's StartSliceJob prepares it ----------------
def expand_gcode(text, st):
    def rep(m):
        try:
            v = st.get(m.group(1))
        except KeyError:
            return m.group(0)
        if isinstance(v, float) and v.is_integer():
            v = int(v)
        return str(v)
    return re.sub(r'\{(\w+)(?:,\s?\w+)?\}', rep, text)


def engine_settings(st, base_defaults, overrides):
    keys = sorted(set(st.defn) | set(st.ext_def))
    vals = {}
    for k in keys:
        if st.defn.get(k, {}).get('type') == 'category':
            continue
        try:
            vals[k] = st.get(k)
        except KeyError:
            pass
    vals.update(overrides)

    start, end = vals.get('machine_start_gcode', ''), vals.get('machine_end_gcode', '')
    bed = ['material_bed_temperature', 'material_bed_temperature_layer_0']
    hot = ['material_print_temperature', 'material_print_temperature_layer_0',
           'default_material_print_temperature', 'material_initial_print_temperature',
           'material_final_print_temperature', 'material_standby_temperature', 'print_temperature']
    vals['material_bed_temp_prepend'] = not re.search(r'\{(%s)(,\s?\w+)?\}' % '|'.join(bed), start)
    vals['material_print_temp_prepend'] = not re.search(r'\{(%s)(,\s?\w+)?\}' % '|'.join(hot), start)
    vals['machine_start_gcode'] = expand_gcode(start, st)
    vals['machine_end_gcode'] = expand_gcode(end, st)

    def fmt(v):
        return str(v)
    for k, v in base_defaults.items():                # anything the stack could not resolve
        vals.setdefault(k, v)
    return {k: fmt(v) for k, v in vals.items() if v is not None}


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('names', nargs='*', help='only STLs whose name contains one of these')
    ap.add_argument('--machine', help='Cura machine name (default: the active one in cura.cfg)')
    ap.add_argument('--set', action='append', default=[], metavar='KEY=VALUE',
                    help='override a setting for this run, e.g. infill_sparse_density=15')
    ap.add_argument('--dump', action='store_true', help='print the resolved settings and stop')
    a = ap.parse_args()

    engine, res, cfg = find_cura()
    idx = index_containers(res, cfg)
    machine = a.machine
    if not machine:
        cc = read_cfg(os.path.join(cfg, 'cura.cfg'))
        machine = cc['cura'].get('active_machine') if cc.has_section('cura') else None
        if not machine:
            raise SystemExit('no active machine in cura.cfg; pass --machine')
    st, names, mdef = build_stack(cfg, idx, machine)

    fdm = load_definition('fdmprinter', idx)
    base_defaults = {k: v['default_value'] for k, v in fdm.items() if 'default_value' in v}

    if a.dump:
        for k in sorted(st.defn):
            try:
                print('%-50s %r' % (k, st.get(k)))
            except KeyError:
                pass
        return

    stls = [p for p in sorted(glob.glob(os.path.join(OUT_STL, '*.stl')))
            if not os.path.basename(p).startswith(SKIP)
            and (not a.names or any(n.lower() in os.path.basename(p).lower() for n in a.names))]
    if not stls:
        raise SystemExit('no STLs matched')
    os.makedirs(OUT_GCODE, exist_ok=True)

    cli_over = {}
    for s in a.set:
        k, v = s.split('=', 1)
        cli_over[k.strip()] = st.parse(k.strip(), v.strip())

    print('machine  %s  (%s)' % (machine, mdef))
    print('stack    %s' % ' / '.join(c for c in names['extruder'][1:-1] if not c.startswith('empty')))
    q = {k: st.get(k) for k in ('layer_height', 'wall_line_count', 'infill_sparse_density',
                               'material_print_temperature', 'material_bed_temperature',
                               'retraction_amount', 'speed_print', 'adaptive_layer_height_enabled')}
    print('         ' + '  '.join('%s=%s' % kv for kv in q.items()))
    if st.errors:
        print('  %d formula(s) fell back to their default, e.g. %s' % (len(st.errors), st.errors[0]))

    fdm_json = idx['fdmprinter']
    env = dict(os.environ, CURA_ENGINE_SEARCH_PATH=os.pathsep.join(
        [os.path.join(res, 'definitions'), os.path.join(res, 'extruders')]))
    fails = 0
    for p in stls:
        name = os.path.basename(p)
        over = dict(cli_over)
        over.update(PER_PART.get(name, {}))
        sets = engine_settings(st, base_defaults, over)
        sets['center_object'] = 'True'
        sets['mesh_rotation_matrix'] = ('[[1,0,0],[0,-1,0],[0,0,-1]]' if name in FLIP
                                        else '[[1,0,0],[0,1,0],[0,0,1]]')
        mesh_keys = ()      # read while -l loads the mesh, so they live in the profile too
        # The resolved profile goes in as a definition that inherits fdmprinter, loaded
        # for the scene AND for extruder 0: CuraEngine's extruder has no defaults of its
        # own, and -s for ~150 settings twice over would pass Windows' command-line limit.
        prof = os.path.join(OUT_GCODE, '_profile.def.json')
        # Flat: CuraEngine only reads default_value from LEAF settings, so a parent
        # like min_wall_line_width would be dropped if left in fdmprinter's tree.
        doc = {'version': 2, 'name': 'cura_slice profile (%s)' % machine, 'metadata': {},
               'settings': {'all': {'label': 'all', 'type': 'category', 'children': {
                   k: {'type': 'str', 'default_value': v} for k, v in sets.items() if k not in mesh_keys}}}}
        json.dump(doc, open(prof, 'w', encoding='utf-8'))
        args = [engine, 'slice', '-j', prof, '-e0', '-j', prof]
        out = os.path.join(OUT_GCODE, os.path.splitext(name)[0] + '.gcode')
        args += ['-l', p]
        for k in mesh_keys:
            if k in sets:
                args += ['-s', '%s=%s' % (k, sets[k])]
        args += ['-o', out]
        r = subprocess.run(args, capture_output=True, text=True, env=env)
        ok = r.returncode == 0 and os.path.exists(out) and os.path.getsize(out) > 1000
        if ok:
            stamp(out, machine, names, st, over, name in FLIP, r.stdout + r.stderr)
            t, fil = summary(out)
            print('  %-34s ok   %s  %s' % (name, t, fil))
        else:
            fails += 1
            print('  %-34s FAILED (exit %s)\n%s' % (name, r.returncode, (r.stderr or r.stdout)[-800:]))
    os.remove(os.path.join(OUT_GCODE, '_profile.def.json'))
    if fails:
        raise SystemExit('%d STL(s) failed to slice' % fails)


def put_defaults(tree, vals):
    """Write resolved values into fdmprinter's own tree as default_value; return the
    ones the tree has no place for."""
    left = dict(vals)
    def walk(t):
        for k, v in t.items():
            if k in left:
                v['default_value'] = left.pop(k)
            if 'children' in v:
                walk(v['children'])
    walk(tree)
    return left


def summary(path):
    t = fil = ''
    with open(path, encoding='utf-8', errors='replace') as fh:
        for line in fh:
            if line.startswith(';TIME:'):
                s = int(float(line[6:]))
                t = '%dh%02dm' % (s // 3600, s % 3600 // 60)
            elif line.startswith(';Filament used:'):
                m = re.search(r'([\d.]+)m', line)
                fil = '%.2f m' % float(m.group(1)) if m else line.strip()
            if t and fil:
                break
    return t, fil


def stamp(path, machine, names, st, over, flipped, log):
    """Our provenance lines, plus the header values the Cura GUI would have filled
    in: run from the command line, CuraEngine leaves ;TIME:6666, 0 m of filament
    and inverted MIN/MAX placeholders, and prints the real numbers to its log."""
    head = [';Sliced by cura_slice.py %s' % datetime.datetime.now().strftime('%Y-%m-%d %H:%M'),
            ';Machine: %s' % machine,
            ';Profile: %s' % ' / '.join(c for c in names['extruder'][1:-1] if not c.startswith('empty'))]
    if over:
        head.append(';Overrides: %s' % ', '.join('%s=%s' % kv for kv in over.items()))
    if flipped:
        head.append(';Flipped upside down for printing (PRINTING.md orientation)')
    body = open(path, encoding='utf-8', errors='replace').read()

    t = re.search(r'Print time \(s\): (\d+)', log)
    fil = re.search(r';Filament used: ([\d.]+m)', log)
    if t:
        body = re.sub(r'(?m)^;TIME:\d+', ';TIME:' + t.group(1), body, count=1)
    if fil:
        body = re.sub(r'(?m)^;Filament used: .*', ';Filament used: ' + fil.group(1), body, count=1)
    # extents of the extruding moves, from layer 0 on (not the purge line or park move)
    lo, hi = [1e9] * 3, [-1e9] * 3
    pos = [0.0, 0.0, 0.0]
    started = False
    for line in body.splitlines():
        if line.startswith(';LAYER:0'):
            started = True
        if not started or not line.startswith(('G0', 'G1')):
            continue
        for i, ax in enumerate('XYZ'):
            m = re.search(ax + r'(-?[\d.]+)', line)
            if m:
                pos[i] = float(m.group(1))
        if line.startswith('G1') and ' E' in line:
            for i in range(3):
                lo[i], hi[i] = min(lo[i], pos[i]), max(hi[i], pos[i])
    if hi[0] > lo[0]:
        for ax, v in zip(('MINX', 'MINY', 'MINZ', 'MAXX', 'MAXY', 'MAXZ'), lo + hi):
            body = re.sub(r'(?m)^;%s:.*' % ax, ';%s:%.3f' % (ax, v), body, count=1)
    with open(path, 'w', encoding='utf-8', newline='\n') as fh:
        fh.write('\n'.join(head) + '\n' + body)


if __name__ == '__main__':
    main()
