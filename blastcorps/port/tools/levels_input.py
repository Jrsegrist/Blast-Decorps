#!/usr/bin/env python3
"""levels_input.py OUT: the controller input `compare.py level` / `verify-levels` play
(bc_headless --input format: "@READ BUTTONS X Y", a line holds until the next).

- reads 400-985: START / A alternately every 45 reads: title, new game (player
  A), the opening sequence, then A on the globe at read 985 selects the level
  the run poked (compare.py LEVEL_POKE_FRAME);
- reads 1030-1210: A taps every 45 reads (mission intro pages, bonus-level
  information, sequences);
- from read 1220: a driving pattern, PHASE reads per step: accelerate, turn
  both ways, brake / reverse, coast to a stop, Z (get out when stopped, the
  vehicle's action otherwise), walk, A on foot, Z (get back in), accelerate
  with the stick down, accelerate + Z, C-up (camera), reverse turning."""
import sys

A, B, Z, START, CU = 0x8000, 0x4000, 0x2000, 0x1000, 0x0008
PHASE = 35


def build():
    lines = ["@0 0 0 0"]
    r, btn = 400, START
    while r <= 985:
        lines += ["@%d 0x%x 0 0" % (r, btn), "@%d 0 0 0" % (r + 6)]
        btn = A if btn == START else START
        r += 45
    r = 1030
    while r < 1210:
        lines += ["@%d 0x%x 0 0" % (r, A), "@%d 0 0 0" % (r + 6)]
        r += 45
    pat = [(A, 0, 0), (A, 80, 0), (A, -80, 0), (B, 0, 0), (B, 60, 0), (A, 0, 80), (0, 0, 0), (Z, 0, 0),
           (0, 0, 0), (0, 70, 0), (A, 0, 0), (0, -70, 0), (Z, 0, 0), (0, 0, 0), (A, 0, -80), (A | Z, 0, 0),
           (CU, 0, 0), (B, -60, 0)]
    r = 1220
    for b, x, y in pat:
        lines.append("@%d 0x%x %d %d" % (r, b, x, y))
        if b & Z:   # a tap: Z released after 4 reads
            lines.append("@%d 0x%x %d %d" % (r + 4, b & ~Z, x, y))
        r += PHASE
    lines.append("@%d 0 0 0" % r)
    return lines


if __name__ == "__main__":
    open(sys.argv[1], "w").write("\n".join(build()) + "\n")
