# Star tracker project notes

- **Controller PCB Rev C is frozen: the Gerbers were sent to the fab house (2026-10-01).**
  Do not edit `hardware/star_tracker_ctrl/*`, the board generators (`design.py`,
  `gen_board.py`, `place.py`, `route.py`, `fill_zones.py`) or re-export Gerbers.
  Parts that don't match a footprint get a build-time workaround, not a board change.
  A real board change means starting Rev D, only if the user asks.
- Read `RESUME.md` first.
