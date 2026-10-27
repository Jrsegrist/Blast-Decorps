#!/usr/bin/env python3
"""Signed-overflow census of the game code (make -C port overflow-census).

MSVC has no -fwrapv, so a signed 32-bit add or multiply that overflows at run
time is undefined behaviour there (the N64 wraps; gcc and clang get -fwrapv).
The census runs a clang x86_64 bc_headless built with
-fno-wrapv -fsanitize=signed-integer-overflow and tools/ubsan_minimal.c over
the attract demos and every level (data/levels_input.txt, as compare.py
level), and lists every operation that overflowed, with its source line.
Each must be made explicit in the source (PORT_WRAP_MUL / PORT_WRAP_ADD,
include/game/port.h); exit status 1 if any is left.

usage: overflow_census.py EXE ROM INPUT OUTDIR [--attract N] [--levels a,b-c] [--frames N]
"""
import os
import subprocess
import sys


def opt(args, name, default):
    if name in args:
        i = args.index(name)
        v = args[i + 1]
        del args[i:i + 2]
        return v
    return default


def wpath(p):
    return subprocess.run(["wslpath", "-w", p], capture_output=True, text=True).stdout.strip()


def levels(spec):
    out = []
    for part in spec.split(","):
        a, _, b = part.partition("-")
        out += list(range(int(a), int(b or a) + 1))
    return out


def main():
    args = sys.argv[1:]
    attract = int(opt(args, "--attract", "4000"))
    lv = levels(opt(args, "--levels", "0-59"))
    frames = int(opt(args, "--frames", "1900"))
    exe, rom, inp, out = [os.path.abspath(a) if i != 1 else a for i, a in enumerate(args[:4])]
    os.makedirs(out, exist_ok=True)
    runs = [("attract", ["--frames", str(attract)])]
    for L in lv:
        runs.append(("level%02d" % L, ["--frames", str(frames), "--input", wpath(inp), "--poke",
                                       "930:80364AF8:1:%X" % L]))
    sites = {}
    for name, extra in runs:
        d = os.path.join(out, name)
        os.makedirs(d, exist_ok=True)
        f = os.path.join(d, "ubsan_sites.txt")
        if os.path.exists(f):
            os.remove(f)
        with open(os.path.join(d, "run.log"), "w") as log:
            r = subprocess.call([exe, rom, "--no-msgbox", "--crash-dir", wpath(d)] + extra, stdout=log,
                                stderr=subprocess.STDOUT, cwd=d)
        n = 0
        if os.path.exists(f):
            for line in open(f):
                p = line.split()
                if len(p) == 2:
                    sites.setdefault(p[0], (p[1], name))
                    n += 1
        print("%s: exit %d, %d sites" % (name, r, n), flush=True)
    pcs = sorted(sites)
    where = []
    if pcs:
        r = subprocess.run(["x86_64-w64-mingw32-addr2line", "-f", "-e", exe] + ["0x" + p for p in pcs],
                           capture_output=True, text=True).stdout.split("\n")
        where = [(r[2 * i], r[2 * i + 1]) for i in range(len(pcs))]
    for p, (fn, line) in zip(pcs, where):
        kind, first = sites[p]
        print("overflow: %s %s in %s at %s (first in %s)" % (p, kind, fn, line.split("/src.us.v11/")[-1], first))
    print("overflow-census: %d sites" % len(pcs))
    return 1 if pcs else 0


if __name__ == "__main__":
    sys.exit(main())
