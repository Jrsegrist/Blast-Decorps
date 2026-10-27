#!/usr/bin/env python3
"""Two bc_headless builds, same runs, per-frame traces compared (make -C port clang-check).

Runs exe A and exe B (e.g. the clang x86_64 check build and the MSVC build)
side by side over the attract demos (--attract N frames) and every level
(data/levels_input.txt and the globe poke, as compare.py level: --levels,
--frames), on the free-running virtual clock (no emulator), each with
--trace; the traces (retrace, time, mode, level, frames in mode, game
retrace counter per frame) must be identical and both runs must exit 0.

usage: tracecmp.py EXE_A EXE_B ROM INPUT OUTDIR [--attract N] [--levels a,b-c] [--frames N] [--jobs N]
  ROM: a Windows path (the exes are Windows programs)
exit status 1 if any run differs or fails.
"""
import concurrent.futures
import filecmp
import os
import shutil
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
    if spec:
        for part in spec.split(","):
            a, _, b = part.partition("-")
            out += list(range(int(a), int(b or a) + 1))
    return out


def run(exe, rom, d, extra):
    """one run in folder d: exit code, trace path"""
    if os.path.isdir(d):
        shutil.rmtree(d)
    os.makedirs(d)
    tr = os.path.join(d, "trace.txt")
    with open(os.path.join(d, "out.txt"), "w") as out, open(os.path.join(d, "err.txt"), "w") as err:
        try:
            r = subprocess.call([exe, rom, "--no-msgbox", "--trace", wpath(tr), "--crash-dir", wpath(d)] + extra,
                                stdout=out, stderr=err, cwd=d, timeout=1800)
        except subprocess.TimeoutExpired:
            r = "timeout"
    for f in os.listdir(d):      # no crash dumps left behind (the .txt report stays)
        if f.endswith(".dmp"):
            os.remove(os.path.join(d, f))
    return r, tr


def main():
    args = sys.argv[1:]
    attract = int(opt(args, "--attract", "4000"))
    lv = levels(opt(args, "--levels", "0-59"))
    frames = int(opt(args, "--frames", "1900"))
    jobs = int(opt(args, "--jobs", "2"))
    if len(args) != 5:
        sys.exit(__doc__)
    exe_a, exe_b, rom, inp, out = args
    exe_a, exe_b, out = os.path.abspath(exe_a), os.path.abspath(exe_b), os.path.abspath(out)
    runs = []
    if attract:
        runs.append(("attract", ["--frames", str(attract)]))
    for L in lv:
        runs.append(("level%02d" % L, ["--frames", str(frames), "--input", wpath(os.path.abspath(inp)),
                                       "--poke", "930:80364AF8:1:%X" % L]))
    print("tracecmp: A = %s\n          B = %s" % (exe_a, exe_b), flush=True)
    bad = 0
    # each job runs one exe; a run's A and B go side by side
    with concurrent.futures.ThreadPoolExecutor(max_workers=max(2, jobs)) as pool:
        futs = [(name, pool.submit(run, exe_a, rom, os.path.join(out, name, "a"), extra),
                 pool.submit(run, exe_b, rom, os.path.join(out, name, "b"), extra)) for name, extra in runs]
        for name, fa, fb in futs:
            (ra, ta), (rb, tb) = fa.result(), fb.result()
            n = sum(1 for _ in open(ta)) if os.path.exists(ta) else 0
            same = ra == 0 and rb == 0 and os.path.exists(ta) and os.path.exists(tb) and filecmp.cmp(ta, tb, False)
            if same:
                print("%s: identical (%d frames)" % (name, n), flush=True)
                continue
            bad += 1
            where = ""
            if os.path.exists(ta) and os.path.exists(tb):
                la, lb = open(ta).read().splitlines(), open(tb).read().splitlines()
                for i in range(max(len(la), len(lb))):
                    a = la[i] if i < len(la) else "<end>"
                    b = lb[i] if i < len(lb) else "<end>"
                    if a != b:
                        where = "; first difference at line %d: A %s / B %s" % (i + 1, a, b)
                        break
            print("%s: DIFFERENT (exit %s / %s)%s" % (name, ra, rb, where), flush=True)
    print("tracecmp: %d of %d runs identical" % (len(runs) - bad, len(runs)))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
