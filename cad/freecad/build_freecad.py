"""
build_freecad.py -- the gear assembly as a FreeCAD document, from the STLs that
export_parts.py writes (every part already in its assembled position).

Two steps, so the heavy one cannot hang the FreeCAD GUI:

  1. convert (headless):   "C:\\Program Files\\FreeCAD 1.1\\bin\\freecadcmd.exe" build_freecad.py
       each STL -> a refined solid (coplanar facets merged) -> _build/<name>.brep
  2. assemble (in the FreeCAD GUI, Macro > Execute, or exec(open(...).read())):
       loads the BREPs into one document, one group per sub-assembly, each part
       coloured and labelled, saves star_tracker_gears.FCStd and a STEP beside it.

The parts are meshes turned into solids, so curved faces are faceted (as in
the STLs). Fine for looking, measuring and clearance checks; to change a part,
change star_tracker_gears.scad and rerun export_parts.py and this.
"""
import json
import os

import FreeCAD as App
import Part

HERE = os.path.dirname(os.path.abspath(__file__)) if '__file__' in globals() else \
    r'C:\Users\mikem\claude\star-tracker-gears\cad\freecad'
BUILD = os.path.join(HERE, '_build')
PARTS = json.load(open(os.path.join(BUILD, 'parts.json')))
OUT = os.path.join(HERE, 'star_tracker_gears.FCStd')


def convert():
    import Mesh
    for p in PARTS:
        m = Mesh.Mesh(os.path.join(BUILD, p['name'] + '.stl'))
        s = Part.Shape()
        s.makeShapeFromMesh(m.Topology, 0.05, False)     # loose triangles
        s.sewShape()                                       # -> one shell per separate body
        shells = s.Shells or [Part.Shell(s.Faces)]
        solids = [Part.Solid(sh).removeSplitter() for sh in shells]   # merge coplanar triangles into faces
        sol = solids[0] if len(solids) == 1 else Part.makeCompound(solids)
        sol.exportBrep(os.path.join(BUILD, p['name'] + '.brep'))
        print('%-11s %6d facets -> %5d faces  vol %9.1f mm3  valid %s'
              % (p['name'], m.CountFacets, len(sol.Faces), sol.Volume, sol.isValid()), flush=True)


def assemble():
    import FreeCADGui as Gui
    name = 'star_tracker_gears'
    if name in App.listDocuments():
        App.closeDocument(name)
    doc = App.newDocument(name)
    groups = {}
    for p in PARTS:
        g = groups.get(p['group'])
        if g is None:
            g = groups[p['group']] = doc.addObject('App::DocumentObjectGroup', p['group'].replace(' ', '_'))
            g.Label = p['group']
        sh = Part.Shape()
        sh.importBrep(os.path.join(BUILD, p['name'] + '.brep'))
        o = doc.addObject('Part::Feature', p['name'])
        o.Shape = sh
        o.Label = p['label']
        g.addObject(o)
        c = p['color'].lstrip('#')
        rgb = tuple(int(c[i:i + 2], 16) / 255.0 for i in (0, 2, 4))
        vo = o.ViewObject
        vo.ShapeAppearance = [App.Material(DiffuseColor=rgb)]
        vo.LineColor = (0.15, 0.15, 0.15)
    doc.recompute()
    doc.saveAs(OUT)
    Part.export([o for o in doc.Objects if o.TypeId == 'Part::Feature'], OUT.replace('.FCStd', '.step'))
    Gui.activeDocument().activeView().viewIsometric()
    Gui.SendMsgToActiveView('ViewFit')
    print('saved', OUT)


if App.GuiUp:
    assemble()
else:
    convert()
