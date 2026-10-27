#!/usr/bin/env python3
"""The shift test (docs/shiftable.md):  make VERSION=us.v11 SHIFT=1 shift-test

  1. shiftbuild.py without pads: the ROM must be the original ROM (SHA-1).
  2. shiftbuild.py with tools_port/shift/pads_test.json: a dummy function at
     the start of each code segment's text, data objects at the start of the
     data, .bss pads, bytes inserted in the middle of the ROM's file area and a
     new ROM file; everything after them moves.
  3. shiftcheck.py: no raw reference left pointing where things used to be.
  4. both ROMs in mupen64plus (tools_port/m64trace, spec_shiftrun.py), no
     input, RDRAM dumped at the same game frames of the same attract-demo
     levels (spec_shiftrun.py has the details); shiftcmp.py compares them
     object by object.

    shifttest.py all --baserom ROM [--frames 1:200,1:800,2:400] [--work DIR]
    shifttest.py run ROM HD_ELF PREFIX --frames 1:200
"""
import argparse
import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.dirname(HERE))
from shiftlib import Elf, die  # noqa: E402

M64TRACE = os.path.join(os.path.dirname(HERE), "m64trace", "m64trace.py")
SPEC = os.path.join(HERE, "spec_shiftrun.py")
SEED_CALLERS = ["util_seed_rand", "people_init", "snd_random_id"]
TIME_CALLERS = ["hd_game_frame"]
FRAME_FUNC = "cont_update"      # once per game frame in every mode (the pad read)


def func_ranges(elf):
    """name -> (start, end) for the code symbols of an ELF (end: the symbol's
    size, else the next code symbol)."""
    text = [(r["value"], n, r["size"]) for n, r in elf.symlist
            if r["sec"] == ".hd_code" and r["type"] in ("STT_FUNC", "STT_NOTYPE")
            and not n.startswith((".", "__sh", "_binary", "hd_code_", "jtbl_", "L8"))]
    text.sort()
    out = {}
    for i, (a, n, sz) in enumerate(text):
        nxt = next((b for b, _, _ in text[i + 1:] if b > a), a + 4)
        out.setdefault(n, (a, a + sz if sz else nxt))
    return out


def jr_ra(elf, start, end):
    data = elf.read(start, end - start)
    for i in range(0, len(data), 4):
        if data[i:i + 4] == b"\x03\xe0\x00\x08":
            return start + i
    die("no jr ra in %08X..%08X" % (start, end))


def run(rom, hd_elf, prefix, frames, timeout=900):
    elf = Elf(hd_elf)
    fr = func_ranges(elf)
    env = dict(os.environ)
    env["SHIFT_COUNT_RET"] = "%x" % jr_ra(elf, *fr["osGetCount"])
    env["SHIFT_TIME_RET"] = "%x" % jr_ra(elf, *fr["osGetTime"])
    env["SHIFT_COUNT_CALLERS"] = ",".join("%x-%x" % fr[n] for n in SEED_CALLERS)
    env["SHIFT_TIME_CALLERS"] = ",".join("%x-%x" % fr[n] for n in TIME_CALLERS)
    env["SHIFT_FRAME_BP"] = "%x" % fr[FRAME_FUNC][0]
    env["SHIFT_LEVEL_BP"] = "%x" % fr["hd_setup_level"][0]
    env["SHIFT_DUMP_AT"] = ",".join(frames)
    env["SHIFT_DUMP_PREFIX"] = prefix
    # the front end's code, dead at a level start (the level's heap goes
    # there); the levels read some of it before writing it (spec_shiftrun.py).
    # SHIFT_FILL_PARTS=text,data / none for experiments (zeroing the whole
    # window stops the game: its .bss is still in use, probably the pak queues)
    fe =Elf(os.path.join(os.path.dirname(hd_elf), "hd_front_end.elf")).sections
    fill = {"text": fe[".hd_front_end"], "data": fe[".hd_front_end_data"], "bss": fe[".hd_front_end_bss"]}
    parts = [p for p in os.environ.get("SHIFT_FILL_PARTS", "text").split(",") if p]   # "" or "none": no fill
    parts = [p for p in parts if p != "none"]
    env["SHIFT_FILL"] = ",".join("%x-%x" % (fill[p]["addr"], fill[p]["addr"] + fill[p]["size"]) for p in parts)
    # SDL's offscreen driver gives glide64mk2 a GL context without a window;
    # with "dummy" the video plugin can't start and the game stops at the
    # first level (it waits for the RDP)
    env.setdefault("SDL_VIDEODRIVER", "offscreen")
    env["M64SAVEDIR"] = prefix + ".saves"
    os.makedirs(os.path.dirname(os.path.abspath(prefix)), exist_ok=True)
    names = ["%s.L%sF%s.bin" % ((prefix,) + tuple(f.split(":"))) for f in frames]
    for n in names:                 # never compare an earlier run's dumps
        if os.path.exists(n):
            os.remove(n)
    with open(prefix + ".log", "w") as log:
        try:
            r = subprocess.run([sys.executable, M64TRACE, rom, SPEC, prefix + ".txt"], env=env,
                               stdout=log, stderr=subprocess.STDOUT, timeout=timeout)
        except subprocess.TimeoutExpired:
            die("%s: emulator run timed out (see %s.log)" % (rom, prefix))
    missing = [f for f, n in zip(frames, names) if not os.path.exists(n)]
    if missing:
        die("%s: no RAM dump at level:frame %s (rc %d; see %s.log)" % (rom, missing, r.returncode, prefix))
    return names


def sh(cmd):
    print("+ " + " ".join(cmd), flush=True)
    r = subprocess.run(cmd)
    if r.returncode:
        die("failed: %s" % " ".join(cmd))


def sha1(path):
    import hashlib
    return hashlib.sha1(open(path, "rb").read()).hexdigest()


def rom_sha1(version):
    """The ROM's SHA-1 from the top-level roms.sha1 (`HASH  build/blastcorps.VERSION.z64`)."""
    path = os.path.join(HERE, "..", "..", "..", "roms.sha1")
    for line in open(path):
        f = line.split()
        if len(f) == 2 and f[1] == "build/blastcorps.%s.z64" % version:
            return f[0]
    die("%s has no line for %s" % (path, version))


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    a = sub.add_parser("all")
    a.add_argument("--baserom", required=True)
    a.add_argument("--ref", default="build")
    a.add_argument("--objs", default="build_shift")
    a.add_argument("--work", default="build_shift/test")
    a.add_argument("--pads", default=os.path.join(HERE, "pads_test.json"))
    a.add_argument("--frames", default="1:200,1:800," + ",".join("%d:400" % lv for lv in range(2, 10)),
                   help="LEVEL:FRAME,...: game frames after a level start (the attract demos play 9 levels)")
    a.add_argument("--noise-cpo", default="1,3,4", help="CountPerOp values of the timing-noise runs")
    a.add_argument("--sha1", help="the original ROM's SHA-1 (default: ../roms.sha1's us.v11 line)")
    a.add_argument("--no-emu", action="store_true")
    a.add_argument("--reuse", action="store_true", help="reuse the unshifted and noise runs' dumps if present "
                                                        "(the unshifted ROM is the original either way)")
    r = sub.add_parser("run")
    r.add_argument("rom")
    r.add_argument("hd_elf")
    r.add_argument("prefix")
    r.add_argument("--frames", default="1:200")
    args = ap.parse_args()

    if args.cmd == "run":
        print("\n".join(run(args.rom, args.hd_elf, args.prefix, args.frames.split(","))))
        return

    W = args.work
    os.makedirs(W, exist_ok=True)
    py = sys.executable
    build = os.path.join(HERE, "shiftbuild.py")
    flat = os.path.join(W, "blastcorps.unshifted.us.v11.z64")
    shifted = os.path.join(W, "blastcorps.shifted.us.v11.z64")
    common = ["--baserom", args.baserom, "--ref", args.ref, "--objs", args.objs]
    sh([py, build] + common + ["--out", os.path.join(W, "unshifted"), "-o", flat])
    want = args.sha1.lower() if args.sha1 else rom_sha1("us.v11")
    got = sha1(flat)
    print("unshifted ROM %s  %s" % (got, "== the original ROM" if got == want else "!= %s" % want), flush=True)
    if got != want:
        die("the unshifted shift build is not the original ROM")
    sh([py, build] + common + ["--out", os.path.join(W, "shifted"), "--pads", args.pads, "-o", shifted])
    sh([py, os.path.join(HERE, "shiftcheck.py"), "--ref-dir", os.path.join(W, "unshifted"),
        "--new-dir", os.path.join(W, "shifted"), "--ref-rom", flat, "--new-rom", shifted])
    if args.no_emu:
        return
    frames = args.frames.split(",")
    levels = sorted(set(f.split(":")[0] for f in frames))
    at = frames + ["%s:0" % lv for lv in levels if "%s:0" % lv not in frames]
    E = os.path.join(W, "emu")
    hd0 = os.path.join(W, "unshifted", "shift", "hd_code.elf")
    hd1 = os.path.join(W, "shifted", "shift", "hd_code.elf")
    print("emulator: unshifted ROM, %d timing-noise runs of it (CountPerOp %s), shifted ROM; RDRAM at %s"
          % (len(args.noise_cpo.split(",")), args.noise_cpo, ",".join(at)), flush=True)
    def run_or_reuse(rom, hd_elf, prefix):
        names = ["%s.L%sF%s.bin" % ((prefix,) + tuple(f.split(":"))) for f in at]
        if args.reuse and all(os.path.exists(n) for n in names):
            print("reusing %s.*.bin" % prefix, flush=True)
            return names
        return run(rom, hd_elf, prefix, at)

    d0 = run_or_reuse(flat, hd0, os.path.join(E, "unshifted"))
    noise = []
    for cpo in args.noise_cpo.split(","):
        os.environ["M64CFG"] = "Core:CountPerOp:1:%s" % cpo
        noise.append(run_or_reuse(flat, hd0, os.path.join(E, "noise_cpo%s" % cpo)))
    os.environ.pop("M64CFG", None)
    d1 = run(shifted, hd1, os.path.join(E, "shifted"), at)
    bad = 0
    for i, f in enumerate(frames):
        lv = f.split(":")[0]
        k0 = at.index("%s:0" % lv)
        print("== level %s, frame %s" % tuple(f.split(":")), flush=True)
        r = subprocess.run([py, os.path.join(HERE, "shiftcmp.py"), "--ref-rom", flat, "--new-rom", shifted,
                            "--ref-elfs", os.path.join(W, "unshifted", "shift"),
                            "--new-elfs", os.path.join(W, "shifted", "shift"),
                            "--noise", ",".join(n[i] for n in noise), "--starts", "%s,%s" % (d0[k0], d1[k0]),
                            d0[i], d1[i]])
        bad |= r.returncode
    if bad:
        die("the shifted ROM's RAM differs from the unshifted ROM's")
    print("shift test OK")


if __name__ == "__main__":
    main()
