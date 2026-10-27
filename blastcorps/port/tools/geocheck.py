#!/usr/bin/env python3
"""loadcheck TD: the port's geometry engine (load/level_geo.c) against
tools_port/bcfiles (geometry.py): each level's full rebuild must be the same
file as `bcfiles geo-rebuild` wrote, and the keep-Rare rebuild the ROM's file.

usage: geocheck.py LOADCHECK_OUTPUT REBUILT_DIR      (exit 1 on a difference)
"""
import hashlib
import os
import sys

lines = [l.split() for l in open(sys.argv[1]) if l.startswith("TD ")]
full = keep = n = 0
bad = []
for p in lines:
    if len(p) != 8 or p[2] != "full":
        bad.append(" ".join(p))
        continue
    lvl = int(p[1])
    n += 1
    path = os.path.join(sys.argv[2], "level_%02d.bin" % lvl)
    py = hashlib.sha1(open(path, "rb").read()).hexdigest() if os.path.exists(path) else None
    if p[3] == py:
        full += 1
    else:
        bad.append("level %02d: C full rebuild %s, bcfiles %s" % (lvl, p[3], py))
    if p[5] == p[7]:
        keep += 1
    else:
        bad.append("level %02d: C keep rebuild %s is not the ROM's file %s" % (lvl, p[5], p[7]))
print("TD: %d/%d C rebuilds equal bcfiles geo-rebuild's; keep-Rare %d/%d the ROM's file" % (full, n, keep, n))
for b in bad[:10]:
    print("  " + b)
sys.exit(1 if bad or n != 60 else 0)
