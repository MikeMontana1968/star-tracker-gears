"""Fill copper zones headlessly (KiCad's own python: it has pcbnew)."""
import os
import sys

import pcbnew

path = sys.argv[1] if len(sys.argv) > 1 else os.path.join(
    os.path.dirname(os.path.abspath(__file__)),
    "star_tracker_ctrl", "star_tracker_ctrl.kicad_pcb")

board = pcbnew.LoadBoard(path)
zones = board.Zones()
pcbnew.ZONE_FILLER(board).Fill(zones)
board.Save(path)
print("filled %d zone(s) in %s" % (len(zones), os.path.basename(path)))
