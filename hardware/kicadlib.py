"""Minimal KiCad S-expression reader/writer + symbol & footprint loader.

Enough to build a schematic and a board from a netlist description.
Targets KiCad 10 file formats (sch 20250610, pcb 20250513).
"""
import math
import os
import re
import uuid as _uuid

KICAD_SHARE = r"C:\Program Files\KiCad\10.0\share\kicad"
SYMBOL_DIR = os.path.join(KICAD_SHARE, "symbols")
FOOTPRINT_DIR = os.path.join(KICAD_SHARE, "footprints")


# --------------------------------------------------------------------------
# S-expression parse / write
# --------------------------------------------------------------------------
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


def _quote(s):
    s = s.replace("\\", "\\\\").replace('"', '\\"').replace("\n", "\\n")
    return '"%s"' % s


def write(node, indent=0):
    """Serialise a nested list back to S-expression text."""
    pad = "\t" * indent
    if isinstance(node, Atom):
        return pad + str(node)
    if isinstance(node, str):
        return pad + _quote(node)
    if not node:
        return pad + "()"

    head = node[0]
    head_txt = str(head) if isinstance(head, Atom) else _quote(head)
    rest = node[1:]

    # keep simple all-atom forms on one line
    if all(not isinstance(c, list) for c in rest):
        parts = [head_txt] + [
            (str(c) if isinstance(c, Atom) else _quote(c)) for c in rest
        ]
        return pad + "(" + " ".join(parts) + ")"

    out = [pad + "(" + head_txt]
    inline = []
    for c in rest:
        if not isinstance(c, list):
            inline.append(str(c) if isinstance(c, Atom) else _quote(c))
        else:
            break
    if inline:
        out[0] += " " + " ".join(inline)
    for c in rest[len(inline):]:
        out.append(write(c, indent + 1))
    out.append(pad + ")")
    return "\n".join(out)


def uid():
    return str(_uuid.uuid4())


def A(x):
    """Coerce a number to a KiCad atom with tidy formatting."""
    if isinstance(x, Atom):
        return x
    if isinstance(x, float):
        s = ("%.6f" % x).rstrip("0").rstrip(".")
        return Atom(s if s not in ("", "-0") else "0")
    return Atom(str(x))


# --------------------------------------------------------------------------
# helpers to poke at parsed trees
# --------------------------------------------------------------------------
def kids(node, name):
    return [c for c in node if isinstance(c, list) and c and str(c[0]) == name]


def kid(node, name):
    k = kids(node, name)
    return k[0] if k else None


def prop(node, name):
    for p in kids(node, "property"):
        if len(p) > 1 and p[1] == name:
            return p
    return None


def set_prop(node, name, value):
    p = prop(node, name)
    if p is not None and len(p) > 2:
        p[2] = value


# --------------------------------------------------------------------------
# symbol library
# --------------------------------------------------------------------------
_sym_cache = {}


def _load_symlib(lib):
    if lib not in _sym_cache:
        path = os.path.join(SYMBOL_DIR, lib + ".kicad_sym")
        with open(path, encoding="utf-8") as f:
            root = parse(f.read())
        table = {}
        for s in kids(root, "symbol"):
            table[s[1]] = s
        _sym_cache[lib] = table
    return _sym_cache[lib]


def _deepcopy(n):
    if isinstance(n, list):
        return [_deepcopy(c) for c in n]
    return n


def load_symbol(lib_id):
    """Return a flattened symbol tree named '<lib>:<name>', with `extends` resolved."""
    lib, name = lib_id.split(":", 1)
    table = _load_symlib(lib)
    if name not in table:
        raise KeyError("symbol %s not found" % lib_id)
    sym = _deepcopy(table[name])

    ext = kid(sym, "extends")
    if ext is not None:
        parent_name = ext[1]
        parent = _deepcopy(table[parent_name])
        # child's own properties win; everything else comes from the parent
        child_props = {p[1]: p for p in kids(sym, "property")}
        merged = [Atom("symbol"), name]
        for c in parent[2:]:
            if isinstance(c, list) and str(c[0]) == "property" and c[1] in child_props:
                merged.append(child_props.pop(c[1]))
            elif isinstance(c, list) and str(c[0]) == "symbol":
                # rename unit sub-symbols  PARENT_0_1 -> CHILD_0_1
                unit = _deepcopy(c)
                unit[1] = str(unit[1]).replace(parent_name, name, 1)
                merged.append(unit)
            else:
                merged.append(_deepcopy(c))
        for p in child_props.values():
            merged.append(p)
        sym = merged

    sym[1] = lib_id
    return sym


def symbol_pins(lib_id):
    """[(number, name, x, y, rot)] in library coordinates (Y up)."""
    sym = load_symbol(lib_id)
    out = []
    for unit in kids(sym, "symbol"):
        for p in kids(unit, "pin"):
            at = kid(p, "at")
            num = kid(p, "number")
            nm = kid(p, "name")
            if at is None or num is None:
                continue
            rot = float(at[3]) if len(at) > 3 else 0.0
            out.append((str(num[1]), str(nm[1]) if nm else "",
                        float(at[1]), float(at[2]), rot))
    return out


# --------------------------------------------------------------------------
# footprint library
# --------------------------------------------------------------------------
_fp_cache = {}


def load_footprint(lib_id):
    """Return a parsed footprint tree for '<lib>:<name>'."""
    if lib_id in _fp_cache:
        return _deepcopy(_fp_cache[lib_id])
    lib, name = lib_id.split(":", 1)
    path = os.path.join(FOOTPRINT_DIR, lib + ".pretty", name + ".kicad_mod")
    with open(path, encoding="utf-8") as f:
        fp = parse(f.read())
    _fp_cache[lib_id] = fp
    return _deepcopy(fp)


def rotate(x, y, deg):
    """KiCad point rotation (Y down, positive angle = counter-clockwise on screen)."""
    a = math.radians(deg)
    c, s = math.cos(a), math.sin(a)
    return x * c + y * s, -x * s + y * c


def footprint_pads(fp):
    """[(number, local_x, local_y)] for every pad in a footprint tree."""
    out = []
    for p in kids(fp, "pad"):
        at = kid(p, "at")
        if at is None:
            continue
        out.append((str(p[1]), float(at[1]), float(at[2])))
    return out
