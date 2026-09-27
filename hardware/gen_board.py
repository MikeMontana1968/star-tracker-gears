"""Generate the KiCad project (schematic + board + BOM) from design.py."""
import json
import os
import sys

import kicadlib as K
from kicadlib import A, Atom, uid
import design as D
import layout as LAY
import place as PL

OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "star_tracker_ctrl")
PROJ = "star_tracker_ctrl"
GRID = 1.27


def snap(v):
    return round(v / GRID) * GRID


# --------------------------------------------------------------------------
# nets
# --------------------------------------------------------------------------
def build_nets():
    names = set()
    for p in D.PARTS:
        for n in p["pins"].values():
            if n != "-":
                names.add(n)
    ordered = ["GND"] + sorted(n for n in names if n != "GND")
    return {n: i + 1 for i, n in enumerate(ordered)}


NETS = build_nets()


# ==========================================================================
# SCHEMATIC
# ==========================================================================
def pin_sheet_pos(sym_x, sym_y, lib_x, lib_y):
    """Library coords are Y-up; the sheet is Y-down."""
    return sym_x + lib_x, sym_y - lib_y


OUTWARD = {0: (-1, 0), 180: (1, 0), 90: (0, 1), 270: (0, -1)}
LABEL_ANGLE = {(-1, 0): 180, (1, 0): 0, (0, 1): 270, (0, -1): 90}
LABEL_JUST = {(-1, 0): "right", (1, 0): "left", (0, 1): "right", (0, -1): "left"}


def gen_schematic():
    root = [Atom("kicad_sch"),
            [Atom("version"), A(20250610)],
            [Atom("generator"), "eeschema"],
            [Atom("generator_version"), "9.99"]]
    root_uuid = uid()
    root += [[Atom("uuid"), root_uuid],
             [Atom("paper"), "A1"],
             [Atom("title_block"),
              [Atom("title"), D.TITLE],
              [Atom("date"), "2026-09-27"],
              [Atom("rev"), D.REV],
              [Atom("company"), D.COMPANY]]]

    # --- lib_symbols ---
    used = []
    for p in D.PARTS:
        if p["sym"] not in used:
            used.append(p["sym"])
    lib = [Atom("lib_symbols")]
    for lid in used:
        lib.append(K.load_symbol(lid))
    root.append(lib)

    STUB = 2.54
    for p in D.PARTS:
        sx, sy = snap(p["sch"][0]), snap(p["sch"][1])
        sym = [Atom("symbol"),
               [Atom("lib_id"), p["sym"]],
               [Atom("at"), A(sx), A(sy), A(0)],
               [Atom("unit"), A(1)],
               [Atom("exclude_from_sim"), Atom("no")],
               [Atom("in_bom"), Atom("yes")],
               [Atom("on_board"), Atom("yes")],
               [Atom("dnp"), Atom("yes" if p["dnp"] else "no")],
               [Atom("uuid"), uid()]]

        def fld(name, val, dy, hide):
            f = [Atom("property"), name, val,
                 [Atom("at"), A(sx), A(sy + dy), A(0)]]
            if hide:
                f.append([Atom("hide"), Atom("yes")])
            f.append([Atom("effects"),
                      [Atom("font"), [Atom("size"), A(1.27), A(1.27)]],
                      [Atom("justify"), Atom("left")]])
            return f

        sym.append(fld("Reference", p["ref"], -12.7, False))
        sym.append(fld("Value", p["value"] + ("  [DNP]" if p["dnp"] else ""),
                       -10.16, False))
        sym.append(fld("Footprint", p["fp"] or "", 0, True))
        sym.append(fld("Datasheet", "", 0, True))
        sym.append(fld("Description", p["note"], 0, True))

        pins = K.symbol_pins(p["sym"])
        for num, _name, lx, ly, rot in pins:
            sym.append([Atom("pin"), num, [Atom("uuid"), uid()]])
        sym.append([Atom("instances"),
                    [Atom("project"), PROJ,
                     [Atom("path"), "/" + root_uuid,
                      [Atom("reference"), p["ref"]],
                      [Atom("unit"), A(1)]]]])
        root.append(sym)

        # stub wires + labels (or a no-connect marker)
        for num, _name, lx, ly, rot in pins:
            px, py = pin_sheet_pos(sx, sy, lx, ly)
            net = p["pins"].get(num, "-")
            if net == "-":
                root.append([Atom("no_connect"),
                             [Atom("at"), A(px), A(py)],
                             [Atom("uuid"), uid()]])
                continue
            dx, dy = OUTWARD.get(int(rot) % 360, (-1, 0))
            ex, ey = px + dx * STUB, py + dy * STUB
            root.append([Atom("wire"),
                         [Atom("pts"),
                          [Atom("xy"), A(px), A(py)],
                          [Atom("xy"), A(ex), A(ey)]],
                         [Atom("stroke"), [Atom("width"), A(0)],
                          [Atom("type"), Atom("default")]],
                         [Atom("uuid"), uid()]])
            root.append([Atom("label"), net,
                         [Atom("at"), A(ex), A(ey),
                          A(LABEL_ANGLE[(dx, dy)])],
                         [Atom("effects"),
                          [Atom("font"), [Atom("size"), A(1.27), A(1.27)]],
                          [Atom("justify"), Atom(LABEL_JUST[(dx, dy)]),
                           Atom("bottom")]],
                         [Atom("uuid"), uid()]])

    root.append([Atom("sheet_instances"),
                 [Atom("path"), "/", [Atom("page"), "1"]]])
    root.append([Atom("embedded_fonts"), Atom("no")])
    return K.write(root) + "\n", root_uuid


# ==========================================================================
# BOARD
# ==========================================================================
LAYERS = [
    (0, "F.Cu", "signal", None), (2, "B.Cu", "signal", None),
    (9, "F.Adhes", "user", "F.Adhesive"), (11, "B.Adhes", "user", "B.Adhesive"),
    (13, "F.Paste", "user", None), (15, "B.Paste", "user", None),
    (5, "F.SilkS", "user", "F.Silkscreen"), (7, "B.SilkS", "user", "B.Silkscreen"),
    (1, "F.Mask", "user", None), (3, "B.Mask", "user", None),
    (17, "Dwgs.User", "user", "User.Drawings"),
    (19, "Cmts.User", "user", "User.Comments"),
    (21, "Eco1.User", "user", "User.Eco1"), (23, "Eco2.User", "user", "User.Eco2"),
    (25, "Edge.Cuts", "user", None), (27, "Margin", "user", None),
    (31, "F.CrtYd", "user", "F.Courtyard"), (29, "B.CrtYd", "user", "B.Courtyard"),
    (35, "F.Fab", "user", None), (33, "B.Fab", "user", None),
]


def pad_abs(p):
    """{pad number: [(x, y)]} in board coordinates."""
    fp = K.load_footprint(p["fp"])
    fx, fy, rot = p["pcb"]
    out = {}
    for num, lx, ly in K.footprint_pads(fp):
        rx, ry = K.rotate(lx, ly, rot)
        out.setdefault(num, []).append((fx + rx, fy + ry))
    return out


def gr_line(x1, y1, x2, y2, layer, width=0.1):
    return [Atom("gr_line"),
            [Atom("start"), A(x1), A(y1)],
            [Atom("end"), A(x2), A(y2)],
            [Atom("stroke"), [Atom("width"), A(width)],
             [Atom("type"), Atom("solid")]],
            [Atom("layer"), layer],
            [Atom("uuid"), uid()]]


def gen_board(routes, vias=()):
    b = [Atom("kicad_pcb"),
         [Atom("version"), A(20250513)],
         [Atom("generator"), "pcbnew"],
         [Atom("generator_version"), "9.99"],
         [Atom("general"), [Atom("thickness"), A(1.6)],
          [Atom("legacy_teardrops"), Atom("no")]],
         [Atom("paper"), "A4"],
         [Atom("title_block"),
          [Atom("title"), D.TITLE],
          [Atom("date"), "2026-09-27"],
          [Atom("rev"), D.REV],
          [Atom("company"), D.COMPANY]]]

    lay = [Atom("layers")]
    for n, nm, ty, usr in LAYERS:
        e = [A(n), nm, Atom(ty)]
        if usr:
            e.append(usr)
        lay.append(e)
    b.append(lay)

    b.append([Atom("setup"),
              [Atom("pad_to_mask_clearance"), A(0)],
              [Atom("allow_soldermask_bridges_in_footprints"), Atom("no")],
              [Atom("tenting"), Atom("front"), Atom("back")]])

    b.append([Atom("net"), A(0), ""])
    for name, idx in sorted(NETS.items(), key=lambda kv: kv[1]):
        b.append([Atom("net"), A(idx), name])

    # ---- footprints ----
    for p in D.PARTS:
        if not p["fp"]:
            continue
        fp = K.load_footprint(p["fp"])
        fx, fy, rot = p["pcb"]
        out = [Atom("footprint"), p["fp"],
               [Atom("layer"), "F.Cu"],
               [Atom("uuid"), uid()],
               [Atom("at"), A(fx), A(fy), A(rot)]]
        for c in fp[1:]:
            if not isinstance(c, list):
                continue
            h = str(c[0])
            if h in ("version", "generator", "generator_version", "layer", "at"):
                continue
            if h == "property":
                nm = c[1] if len(c) > 1 else ""
                if nm == "Reference":
                    c[2] = p["ref"]
                    if p["ref"].startswith("H"):
                        c.append([Atom("hide"), Atom("yes")])
                    fo = K.kid(K.kid(c, "effects") or [], "font")
                    if fo is not None:
                        sz = K.kid(fo, "size")
                        if sz is not None:
                            sz[1], sz[2] = A(0.8), A(0.8)
                elif nm == "Value":
                    c[2] = p["value"]
                elif nm in ("Footprint", "Datasheet", "Description"):
                    continue
                out.append(c)
                continue
            if h == "pad":
                net = p["pins"].get(str(c[1]), "-")
                # KiCad stores the footprint rotation inside each pad's angle
                at = K.kid(c, "at")
                if at is not None and rot:
                    ang = float(at[3]) if len(at) > 3 else 0.0
                    ang = (ang + rot) % 360
                    if len(at) > 3:
                        at[3] = A(ang)
                    else:
                        at.append(A(ang))
                if net != "-" and net in NETS:
                    c.append([Atom("net"), A(NETS[net]), net])
                c.append([Atom("uuid"), uid()])
                out.append(c)
                continue
            out.append(c)
        # add Footprint/Datasheet/Description properties back, tidily
        for nm, val in (("Footprint", p["fp"]), ("Datasheet", ""),
                        ("Description", p["note"])):
            out.append([Atom("property"), nm, val,
                        [Atom("at"), A(0), A(0), A(0)],
                        [Atom("layer"), "F.Fab"],
                        [Atom("hide"), Atom("yes")],
                        [Atom("uuid"), uid()],
                        [Atom("effects"),
                         [Atom("font"), [Atom("size"), A(1), A(1)],
                          [Atom("thickness"), A(0.15)]]]])
        b.append(out)

    # ---- board outline ----
    W, H = D.BOARD_W, D.BOARD_H
    for x1, y1, x2, y2 in [(0, 0, W, 0), (W, 0, W, H), (W, H, 0, H), (0, H, 0, 0)]:
        b.append(gr_line(x1, y1, x2, y2, "Edge.Cuts", 0.1))

    # ---- silkscreen notes ----
    for x, y, txt, size in PL.SILK:
        b.append([Atom("gr_text"), txt,
                  [Atom("at"), A(x), A(y), A(0)],
                  [Atom("layer"), "F.SilkS"],
                  [Atom("uuid"), uid()],
                  [Atom("effects"),
                   [Atom("font"), [Atom("size"), A(size), A(size)],
                    [Atom("thickness"), A(size * 0.15)]],
                   [Atom("justify"), Atom("left")]]])

    # module pin-name legends, so the sockets can be verified by eye
    def legend(x0, y0, rot, rows, side):
        """side = -1 puts the names before pin 1's row, +1 after it."""
        for i, (nm, _net) in enumerate(rows):
            if rot in (90, 270):                # pins run along X
                step = 2.54 if rot == 90 else -2.54
                x, y = x0 + i * step, y0 + side * 2.3
                ang, just = 90, ("right" if side > 0 else "left")
            else:                               # pins run along +Y
                x, y = x0 + side * 2.3, y0 + i * 2.54
                ang, just = 0, ("left" if side > 0 else "right")
            b.append([Atom("gr_text"), nm,
                      [Atom("at"), A(x), A(y), A(ang)],
                      [Atom("layer"), "F.SilkS"],
                      [Atom("uuid"), uid()],
                      [Atom("effects"),
                       [Atom("font"), [Atom("size"), A(0.8), A(0.8)],
                        [Atom("thickness"), A(0.12)]],
                       [Atom("justify"), Atom(just)]]])

    # per-pin labels on the small connectors, derived from actual pad
    # positions so they cannot drift from the netlist
    for p in D.PARTS:
        if not p["fp"] or not p.get("labels"):
            continue
        pads = pad_abs(p)
        xs = [v[0][0] for v in pads.values()]
        ys = [v[0][1] for v in pads.values()]
        horiz = (max(xs) - min(xs)) >= (max(ys) - min(ys))
        for i, txt in enumerate(p["labels"]):
            pos = pads.get(str(i + 1))
            if not pos:
                continue
            px, py = pos[0]
            if horiz:
                tx, ty, ang, just = px, py - 2.4, 90, "left"
            else:
                tx, ty, ang, just = px - 2.4, py, 0, "right"
            b.append([Atom("gr_text"), txt,
                      [Atom("at"), A(tx), A(ty), A(ang)],
                      [Atom("layer"), "F.SilkS"],
                      [Atom("uuid"), uid()],
                      [Atom("effects"),
                       [Atom("font"), [Atom("size"), A(0.8), A(0.8)],
                        [Atom("thickness"), A(0.12)]],
                       [Atom("justify"), Atom(just)]]])

    for x0, y0, x1, y1 in getattr(PL, "MODULE_OUTLINES", []):
        b.append([Atom("gr_rect"),
                  [Atom("start"), A(x0), A(y0)],
                  [Atom("end"), A(x1), A(y1)],
                  [Atom("stroke"), [Atom("width"), A(0.15)],
                   [Atom("type"), Atom("dash")]],
                  [Atom("fill"), Atom("no")],
                  [Atom("layer"), "F.SilkS"],
                  [Atom("uuid"), uid()]])

    # a "1" beside pin 1 of each socket -- the unambiguous orientation mark
    for ref, dx, dy in (("J20", 3.4, 0), ("J21", 3.4, 0),
                        ("J30", 0, -3.4), ("J31", 0, -3.4)):
        p = next((q for q in D.PARTS if q["ref"] == ref), None)
        if p is None:
            continue
        px, py = pad_abs(p)["1"][0]
        b.append([Atom("gr_text"), "1",
                  [Atom("at"), A(px + dx), A(py + dy), A(0)],
                  [Atom("layer"), "F.SilkS"],
                  [Atom("uuid"), uid()],
                  [Atom("effects"),
                   [Atom("font"), [Atom("size"), A(1.4), A(1.4)],
                    [Atom("thickness"), A(0.25)]]]])

    legend(PL.ESP_PIN1_X, PL.ESP_Y, PL.ESP_ROT, D.ESP32_LEFT, -1)
    legend(PL.ESP_PIN1_X, PL.ESP_Y + D.ESP32_ROW_SPACING, PL.ESP_ROT,
           D.ESP32_RIGHT, +1)
    legend(PL.TMC_X, PL.TMC_Y, PL.TMC_ROT, D.TMC_LEFT, -1)
    legend(PL.TMC_X + D.TMC_ROW_SPACING, PL.TMC_Y, PL.TMC_ROT, D.TMC_RIGHT, +1)

    # ---- tracks ----
    for net, layer, pts, width in routes:
        n = NETS[net]
        for i in range(len(pts) - 1):
            (x1, y1), (x2, y2) = pts[i], pts[i + 1]
            if (x1, y1) == (x2, y2):
                continue
            b.append([Atom("segment"),
                      [Atom("start"), A(x1), A(y1)],
                      [Atom("end"), A(x2), A(y2)],
                      [Atom("width"), A(width)],
                      [Atom("layer"), layer],
                      [Atom("net"), A(n)],
                      [Atom("uuid"), uid()]])

    for vx, vy, vnet in vias:
        b.append([Atom("via"),
                  [Atom("at"), A(vx), A(vy)],
                  [Atom("size"), A(D.VIA_D)],
                  [Atom("drill"), A(D.VIA_DRILL)],
                  [Atom("layers"), "F.Cu", "B.Cu"],
                  [Atom("net"), A(NETS[vnet])],
                  [Atom("uuid"), uid()]])

    # ---- GND pour, both sides ----
    m = 0.4
    poly = [Atom("pts")]
    for x, y in [(m, m), (W - m, m), (W - m, H - m), (m, H - m)]:
        poly.append([Atom("xy"), A(x), A(y)])
    b.append([Atom("zone"),
              [Atom("net"), A(NETS["GND"])],
              [Atom("net_name"), "GND"],
              [Atom("layers"), "F.Cu", "B.Cu"],
              [Atom("uuid"), uid()],
              [Atom("name"), "GND"],
              [Atom("hatch"), Atom("edge"), A(0.508)],
              [Atom("priority"), A(0)],
              [Atom("connect_pads"), [Atom("clearance"), A(0.3)]],
              [Atom("min_thickness"), A(0.2)],
              [Atom("filled_areas_thickness"), Atom("no")],
              [Atom("fill"), [Atom("thermal_gap"), A(0.3)],
               [Atom("thermal_bridge_width"), A(0.4)]],
              [Atom("polygon"), poly]])

    b.append([Atom("embedded_fonts"), Atom("no")])
    return K.write(b) + "\n"


# ==========================================================================
# project file
# ==========================================================================
def gen_pro(root_uuid):
    nc_power = {
        "name": "Power", "clearance": D.CLEARANCE, "track_width": D.TRACK_POWER,
        "via_diameter": 1.2, "via_drill": 0.6, "uvia_diameter": 0.3,
        "uvia_drill": 0.1, "diff_pair_width": 0.2, "diff_pair_gap": 0.25,
        "line_style": 0, "pcb_color": "rgba(0, 0, 0, 0.000)",
        "schematic_color": "rgba(0, 0, 0, 0.000)", "wire_width": 6,
        "bus_width": 12, "priority": 1,
    }
    nc_default = dict(nc_power, name="Default", track_width=D.TRACK_SIG,
                      via_diameter=D.VIA_D, via_drill=D.VIA_DRILL, priority=2)
    return json.dumps({
        "board": {
            "design_settings": {
                "defaults": {
                    "board_outline_line_width": 0.1,
                    "copper_line_width": 0.2,
                    "silk_line_width": 0.15,
                    "silk_text_size_h": 1.0, "silk_text_size_v": 1.0,
                    "silk_text_thickness": 0.15,
                },
                "rules": {
                    "min_clearance": D.CLEARANCE,
                    "min_copper_edge_clearance": 0.3,
                    "min_track_width": 0.25,
                    "min_through_hole_diameter": 0.3,
                    "min_via_annular_width": 0.13,
                    "min_via_diameter": 0.5,
                    # one thermal spoke is an electrical connection; the
                    # default of 2 is a mechanical-robustness preference and
                    # costs isolated pads on a dense hand-soldered board
                    "min_resolved_spokes": 1,
                },
                "track_widths": [0.0, D.TRACK_SIG, 0.8, D.TRACK_POWER, 2.0],
                "via_dimensions": [{"diameter": 0.0, "drill": 0.0},
                                   {"diameter": D.VIA_D, "drill": D.VIA_DRILL},
                                   {"diameter": 1.2, "drill": 0.6}],
            }
        },
        "meta": {"filename": PROJ + ".kicad_pro", "version": 3},
        "net_settings": {
            "classes": [nc_default, nc_power],
            "meta": {"version": 4},
            "netclass_patterns": [{"netclass": "Power", "pattern": p}
                                  for p in D.POWER_NETS],
        },
        "pcbnew": {"page_layout_descr_file": ""},
        "schematic": {"legacy_lib_dir": "", "legacy_lib_list": []},
        "sheets": [[root_uuid, ""]],
        "text_variables": {},
        "libraries": {"pinned_footprint_libs": [], "pinned_symbol_libs": []},
    }, indent=2)


# ==========================================================================
def gen_bom():
    rows = {}
    for p in D.PARTS:
        if p["ref"].startswith("H") or not p["fp"]:
            continue
        key = (p["value"], p["fp"], p["dnp"])
        rows.setdefault(key, []).append(p["ref"])
    out = ["Qty,Refs,Value,Footprint,Fitted,Note"]
    for (val, fp, dnp), refs in sorted(rows.items(), key=lambda kv: kv[1][0]):
        note = next((x["note"] for x in D.PARTS if x["ref"] == refs[0]), "")
        out.append('%d,"%s","%s","%s",%s,"%s"' %
                   (len(refs), " ".join(sorted(refs)), val, fp.split(":")[-1],
                    "NO" if dnp else "yes", note.replace('"', "'")))
    return "\n".join(out) + "\n"


def main():
    warn = LAY.autoplace(D.PARTS)
    bad = LAY.check(D.PARTS, D.BOARD_W, D.BOARD_H)
    for m in warn + bad:
        print("  !", m)
    if bad:
        raise SystemExit("placement has collisions - fix place.py")
    import route as RT
    routes, vias, failed = RT.route_all(D.PARTS, D.BOARD_W, D.BOARD_H,
                                        D.POWER_NETS)
    if failed:
        print("  ! router could not finish:", ", ".join(failed))
    os.makedirs(OUT, exist_ok=True)
    sch, root_uuid = gen_schematic()
    open(os.path.join(OUT, PROJ + ".kicad_sch"), "w", encoding="utf-8").write(sch)
    open(os.path.join(OUT, PROJ + ".kicad_pcb"), "w", encoding="utf-8").write(
        gen_board(routes, vias))
    open(os.path.join(OUT, PROJ + ".kicad_pro"), "w", encoding="utf-8").write(
        gen_pro(root_uuid))
    open(os.path.join(OUT, "BOM.csv"), "w", encoding="utf-8").write(gen_bom())
    print("wrote %s | %d parts, %d nets, %d track runs, %d vias"
          % (OUT, len(D.PARTS), len(NETS), len(routes), len(vias)))


if __name__ == "__main__":
    main()
