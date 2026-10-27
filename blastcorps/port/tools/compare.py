#!/usr/bin/env python3
"""Frame-by-frame comparison of bc_headless.exe with the emulator.

The emulator (mupen64plus through tools_port/m64trace, spec port/tools/cmp_spec.py)
runs a ROM (default: the NON_MATCHING test ROM, which runs the same C as the
exe) and logs, per frame, the point where the frame-ending gfx task starts
(where bc_headless dumps), when each frame's RDP work finishes, and every
osGetTime/osGetCount result with its caller.  From that log the exe gets the
emulator's clock (--clock, --frame-done, --boot-count), runs the same frames
and dumps RDRAM at the same frame numbers; the dumps are then compared word by
word after normalising byte order (see diff below).

usage (run from port/; `make -C port compare DEMO=n` drives it):
  compare.py emu    EMUDIR [--rom ROM] [--vis N] [--dump SPEC] [--stop F] [--lle]
                    [--input FILE] [--poke F:ADDR:SIZE:VAL,..]
                    (--lle: audio tasks and the visibility test on an LLE RSP, see lle_env;
                    --input: controller 1 per read, bc_headless's --input format; --poke:
                    RAM writes at frame sends, as bc_headless --poke: pass both to native too)
  compare.py inject EMUDIR                    -> EMUDIR/{clock,framedone,boot}.txt
  compare.py native EMUDIR NATDIR [--frames N] [--dump SPEC] [--exe EXE] [--rom ROM]
  compare.py diff   EMUDIR NATDIR [--from F] [--to F] [--detail N] [--all]
  compare.py frames EMUDIR NATDIR             per-frame timeline (vi, mode, level) side by side
  compare.py demos  EMUDIR                    frame ranges of the attract demos in the log
  compare.py hex    EMUDIR NATDIR FRAME ADDR [LEN]   a region side by side
  compare.py calls  EMUDIR NATDIR             calls of chosen functions per frame (emu --calls F,G
                                              and the exe's --calls F,G): where execution forks
  compare.py run    --demo N [--cache DIR] [--kind nm|base]   the lot (make -C port compare)
  compare.py verify [--frames N] [--cache DIR]   quick regression check (make -C port verify)

Dump SPEC: "every:N", "a-b", "f1,f2" joined with '+'.

Byte order.  The emulator dumps are N64 big-endian; the exe's are host order
with every multi-byte field swapped by the width the code reads it at.  A word
matches when some layout of it agrees: one u32 (or f32/pointer), two u16, u16
+ two bytes, four bytes, or (8-aligned pairs) one u64/f64.  Code pointers match
when both name the same function (emulator: the ROM's ELFs; exe: --syms).
Regions that legitimately differ are listed in tools/compare_ignore.txt.
"""
import array, bisect, collections, os, re, subprocess, sys

HERE = os.path.dirname(os.path.abspath(__file__))
PORT = os.path.dirname(HERE)
ROOT = os.path.dirname(PORT)  # blastcorps/
SIZE = 0x400000  # the game only uses the first 4 MB
BASE = 0x80000000
NATIVE_PERIOD = 781250
# renamed symbols (tools_port/names.txt) count as the address names they had
sys.path.insert(0, os.path.join(ROOT, "tools_port"))
from names import orig, game_functions  # noqa: E402


def die(msg):
    sys.stderr.write("compare: %s\n" % msg)
    sys.exit(1)


def opt(args, name, default=None, conv=str):
    if name in args:
        i = args.index(name)
        v = args[i + 1]
        del args[i:i + 2]
        return conv(v)
    return default


def flag(args, name):
    if name in args:
        args.remove(name)
        return True
    return False


# ---------------------------------------------------------------- symbols

class Syms:
    """sorted (addr, name, size) from ELF symtabs / nm output"""

    def __init__(self):
        self.items = []

    def add(self, addr, name, size=0):
        self.items.append((addr, name, size))

    def done(self):
        self.items.sort()
        self.addrs = [a for a, _, _ in self.items]
        return self

    def find(self, addr):
        i = bisect.bisect_right(self.addrs, addr) - 1
        if i < 0:
            return None
        return self.items[i]

    def name(self, addr):
        it = self.find(addr)
        if it is None:
            return "%08X" % addr
        a, n, _ = it
        return n if a == addr else "%s+0x%X" % (n, addr - a)


def elf_syms(paths, want):
    from elftools.elf.elffile import ELFFile
    s = Syms()
    for p in paths:
        with open(p, "rb") as f:
            for sec in ELFFile(f).iter_sections():
                if sec.name != ".symtab":
                    continue
                for sym in sec.iter_symbols():
                    t = sym["st_info"]["type"]
                    if not sym.name or sym.name.startswith(("$", ".")) or t not in want:
                        continue
                    s.add(sym["st_value"], sym.name, sym["st_size"])
    return s.done()


def nm_syms(path):
    s = Syms()
    for line in open(path):
        p = line.split()
        # (`nm -n` of the i686 exe: 8 digits; of the x86_64 one: 16).  Only
        # i686 C names carry a leading '_'; x86_64 names are kept as they are
        # (SDK names like __amMain, __scMain)
        if len(p) == 3 and p[1] in "Tt" and len(p[0]) in (8, 16):
            name = p[2][1:] if len(p[0]) == 8 and p[2].startswith("_") else p[2]
            s.add(int(p[0], 16), name.split(".")[0])
    return s.done()


def rom_elfs(rom_kind):
    """the ELFs giving the ROM's addresses, relative to ROOT (as elfs.txt stores them)"""
    if rom_kind == "nm":
        return ["build_nm/hd_code.rom.us.v11.elf", "build_nm/hd_front_end.rom.us.v11.elf"]
    return ["build/hd_code.us.v11.elf", "build/hd_front_end.us.v11.elf"]


def cached_elfs(emudir):
    """a cached emulator run's ELFs (elfs.txt), in THIS checkout.  The cache is
    shared by worktrees, so elfs.txt names them relative to ROOT; runs recorded
    before that hold absolute paths into the worktree that recorded them (gone
    once it is removed), mapped here by their build_nm/... or build/... tail.
    That is sound because the folder is keyed by the ROM's hash: an NM run
    (build_nm) is used only when its rom.sha1 is this checkout's NM ROM's."""
    elfs = []
    for p in open(os.path.join(emudir, "elfs.txt")).read().split():
        if os.path.isabs(p):
            m = re.search(r"(?:^|/)(build(?:_nm)?/[^/]+\.elf)$", p)
            if not m:
                die("%s/elfs.txt: %s is not a build/ or build_nm/ ELF" % (emudir, p))
            p = m.group(1)
        elfs.append(os.path.join(ROOT, p))
    sp = os.path.join(emudir, "rom.sha1")
    if elfs and "/build_nm/" in elfs[0] and os.path.exists(sp):
        rom = default_rom("nm")
        have = rom_sha1(rom) if os.path.exists(rom) else "missing"
        want = open(sp).read().strip()
        if have != want:
            die("%s was recorded from the NM ROM %s, but this checkout's %s is %s: rebuild build_nm "
                "(make VERSION=us.v11 NON_MATCHING=1 nmrom at %s) "
                "so its ELFs describe that ROM" % (emudir, want[:8], rom, have[:8], ROOT))
    for p in elfs:
        if not os.path.exists(p):
            die("%s (named by %s/elfs.txt) is missing: build it (make VERSION=us.v11%s at %s)"
                % (p, emudir, " NON_MATCHING=1 nmrom" if "/build_nm/" in p else "", ROOT))
    return elfs


def data_syms():
    """data/bss names: the NON_MATCHING ELFs (pinned at the N64 addresses) + gensyms' list"""
    s = elf_syms([os.path.join(ROOT, "build_nm/hd_code.us.v11.elf"),
                  os.path.join(ROOT, "build_nm/hd_front_end.us.v11.elf")],
                 ("STT_OBJECT", "STT_NOTYPE"))
    have = set(a for a, _, _ in s.items)
    p = os.path.join(PORT, "build/gen/addrs.txt")
    if os.path.exists(p):
        for line in open(p):
            f = line.split()
            if len(f) >= 2:
                a = int(f[1], 16)
                if a not in have:
                    s.add(a, f[0], int(f[2]) if len(f) > 2 else 0)
    s.items = [it for it in s.items if BASE <= it[0] < BASE + 0x800000]
    return s.done()


# ---------------------------------------------------------------- emulator log

def parse_emu(emudir):
    ev = {"F": [], "R": [], "T": [], "C": [], "A": [], "U": [], "Q": [], "B": None, "V": [], "M": []}
    wrap = [0, 0]  # added, previous: the 32-bit count register, unwrapped in log (= time) order

    def unwrap(c):
        if c + wrap[0] < wrap[1] - (1 << 31):
            wrap[0] += 1 << 32
        wrap[1] = c + wrap[0]
        return wrap[1]

    for seq, line in enumerate(open(os.path.join(emudir, "emu.txt"))):
        if line.startswith("#in "):
            m = re.match(r"#in vi=\d+ V (\d+) count=(\d+)", line)
            if m:
                ev["V"].append((int(m.group(1)), unwrap(int(m.group(2)))))
            continue
        if line.startswith("#"):
            continue
        i = line.find("| ")
        if i < 0:
            continue
        m = re.match(r"\S+ vi=(\d+) ", line)
        vi = int(m.group(1)) if m else 0
        for seg in line[i + 2:].split(" || "):
            parse_seg(ev, seg, vi, seq, unwrap)
    return ev


def parse_seg(ev, seg, vi, seq, unwrap):
    if True:
        body = seg.split()
        kind = body[0]
        kv = dict(x.split("=", 1) for x in body[1:] if "=" in x)
        if kind == "F":
            kv["n"] = int(body[1])
            ev["F"].append(kv)
        elif kind == "R":
            kv["seq"] = seq
            ev["R"].append(kv)
        elif kind == "P":
            ev.setdefault("P", []).append(kv)
        elif kind == "K":
            ev.setdefault("K", []).append((kv["f"], int(kv["ra"], 16), int(kv["frame"]), kv.get("a", "")))
        elif kind in ("T", "C", "A", "U", "Q"):
            # (visibility tests are keyed by frame, not by retrace)
            key = int(kv["frame"]) if kind == "U" and "frame" in kv else vi
            if "name" in kv:   # (a PORT_GVI site with its own key, cmp_spec.py GVI_SITES)
                ev.setdefault("names" + kind, {})[len(ev[kind])] = kv["name"]
            ev[kind].append((int(kv["ra"], 16), int(kv["th"]), int(kv["v"], 16), key))
            ev.setdefault("seq" + kind, []).append(seq)
        elif kind == "M":
            ev["M"].append((int(kv["ra"], 16), int(kv["th"]), unwrap(int(kv["c"])), kv["f"], int(kv.get("q", "0"), 16),
                            int(kv.get("gvi", "-1"), 16)))
            ev.setdefault("seqM", []).append(seq)
        elif kind == "B":
            kv["count"] = str(unwrap(int(kv["count"])))
            ev["B"] = kv


def emu_timing(ev):
    v = ev["V"]
    if len(v) < 2 or ev["B"] is None:
        die("emulator log has no V/B lines")
    period = (v[-1][1] - v[0][1]) / float(v[-1][0] - v[0][0])
    boot = int(ev["B"]["count"])
    c1 = v[0][1] - (v[0][0] - 1) * period  # count at emulator VI 1
    delta = int(round((c1 - boot) / period * NATIVE_PERIOD))
    r1 = int(round((boot + delta) / float(NATIVE_PERIOD)))
    # The emulator's first VI after hd_code starts is its VI 1 (its VI
    # interrupts start with the VI setup): no native retrace before it.
    delta = min(delta, NATIVE_PERIOD - 1000)
    native_boot = r1 * NATIVE_PERIOD - delta
    ev["to_native"] = lambda c: int(r1 * NATIVE_PERIOD + (c - c1) * NATIVE_PERIOD / period)
    ev["r1"], ev["c1"] = r1, int(c1)
    return period, native_boot, r1 - 1  # native retrace = emulator VI + offset


def cmd_inject(args):
    emudir = args[0]
    ev = parse_emu(emudir)
    period, boot, off = emu_timing(ev)
    fs = elf_syms(cached_elfs(emudir), ("STT_FUNC", "STT_NOTYPE"))
    fs.items = [it for it in fs.items if it[0] >= 0x80000000]
    fs.done()
    with open(os.path.join(emudir, "clock.txt"), "w") as f:
        # the emulator's clock: count = C1 + (native time - R1 * 781250) * PERIOD / 781250
        f.write("M %d %d %.6f\n" % (ev["r1"], ev["c1"], period))
        for kind in "TCAUQ":
            names = ev.get("names" + kind, {})
            for i, (ra, th, v, vi) in enumerate(ev[kind]):
                it = fs.find(ra)
                name = names.get(i) or (it[1] if it else "?")
                f.write("%s %s %d %x\n" % (kind, name, vi if kind == "U" else vi + off, v))
    # The scheduler's retrace handler (__scHandleRetrace) reads osGetTime once per
    # retrace: where an RDP-done handler ran before the retrace handler of
    # its own retrace (the DP interrupt's message reached the scheduler
    # before the VI manager's retrace message, which comes ~2000 counts
    # after the VI, or later when the CPU is busy), the exe gets the RDP done
    # just before that retrace, so the scheduler sees the same order (sched.c
    # gSchedSwapFrame = frameCount + 1 depends on it)
    retr = {}
    for s, (ra, th, v, vi) in zip(ev.get("seqT", []), ev["T"]):
        it = fs.find(ra)
        if it and it[1] == "__scHandleRetrace":
            retr.setdefault(vi, s)
    nflip = 0
    with open(os.path.join(emudir, "framedone.txt"), "w") as f:
        for n, r in enumerate(ev["R"], 1):
            fr = min(int(r["d"]) / period, 0.999999)
            vi = int(r["vi"])
            if vi in retr and r["seq"] < retr[vi]:
                f.write("@%d %d.999990\n" % (n, vi + off - 1))
                nflip += 1
            else:
                f.write("@%d %d.%06d\n" % (n, vi + off, int(fr * 1000000)))
    # the frame tasks' RSP parts (the RSP is free for audio/cull tasks from then)
    with open(os.path.join(emudir, "framesp.txt"), "w") as f:
        n = 0
        for p in ev.get("P", []):
            if p.get("y") == "1":
                continue
            n += 1
            fr = min(int(p["d"]) / period, 0.999999)
            f.write("@%d %d.%06d\n" % (n, int(p["vi"]) + off, int(fr * 1000000)))
    nsync = 0
    with open(os.path.join(emudir, "sync.txt"), "w") as f:
        kinds = {"osSendMesg": "s", "osRecvMesg": "r", "osJamMesg": "j", "osStartThread": "t", "enter": "e"}
        pts = [(s, c, th, ra, kinds[fn], q, gv) for s, (ra, th, c, fn, q, gv) in zip(ev.get("seqM", []), ev["M"])]
        # osGetTime / osGetCount calls are switch points too (the game reads
        # the clock between message calls): their value is the count itself
        # (main thread only: the scheduler's and the audio thread's clock reads
        # follow RSP timing the platform doesn't model, see compare_ignore.txt)
        for s, (ra, th, v, vi) in zip(ev.get("seqT", []), ev["T"]):
            if th == 3:
                pts.append((s, v, th, ra, "g", 0, -1))
        for s, (ra, th, v, vi) in zip(ev.get("seqC", []), ev["C"]):
            if th != 3:
                continue
            est = ev["c1"] + (vi - 1) * period
            k = round((est - v) / float(1 << 32))
            pts.append((s, v + k * (1 << 32), th, ra, "c", 0, -1))
        pts.sort(key=lambda p: p[0])  # log order = execution order
        # Not the scheduler (5) and the audio thread (4): they wake on
        # interrupts and take little CPU, and their call sequences follow the
        # RSP's task order, which the platform doesn't model; a switch point
        # matched one call off makes a high-priority thread hold the CPU.
        # (Tried: switch points for the audio thread's task send to sc->cmdQ
        # and the scheduler's retrace-handler clock read, for the 4 dumps of
        # 1449 where cmdQ.validCount differs; they didn't change it: the
        # native audio timer still fires a little before the game thread's
        # frame send there.)
        pts = [p for p in pts if p[2] not in (4, 5)]
        # game functions that no longer have a func_ name (tools_port/names.txt):
        # the exe's switch points only count game functions' calls
        for n in game_functions():
            f.write("G %s\n" % n)
        for s, c, th, ra, kind, q, gv in pts:
            it = fs.find(ra)
            if it and re.match(r"^func_[0-9A-F]{8}$", orig(it[1])):
                f.write("S %d %s %s %X %d %d\n" % (th, it[1], kind, q, ev["to_native"](c), gv))
                nsync += 1
    with open(os.path.join(emudir, "boot.txt"), "w") as f:
        f.write("%d %d %.3f\n" % (boot, off, period))
    print("inject: %d frames, %d osGetTime, %d osGetCount values, %d sync points; VI period %.1f counts, "
          "boot count %d, native retrace = emulator VI + %d; %d RDP-done handlers before their retrace's"
          % (len(ev["F"]), len(ev["T"]), len(ev["C"]), nsync, period, boot, off, nflip))


# ---------------------------------------------------------------- runs

def wpath(p):
    return subprocess.check_output(["wslpath", "-w", os.path.abspath(p)]).decode().strip()


def default_rom(kind):
    if kind == "nm":
        return os.path.join(ROOT, "build_nm/blastcorps.nm.us.v11.z64")
    for p in (os.path.join(ROOT, "../baserom.us.v11.z64"), os.path.expanduser("~/blastcorps/baserom.us.v11.z64")):
        if os.path.exists(p):
            return p
    die("no base ROM")


def lle_env(emudir):
    """emu --lle: the RSP tasks whose results the game reads run on an LLE RSP
    (cxd4 from ~/thirdparty/ref, a CC0 test oracle loaded at run time) through
    port/tools/audio's rsp_tap.so: every audio task (checked against the
    port's interpreter as well) and cull_rsp_test's visibility test (ucode
    gCullUcodeText; mupen64plus's HLE RSP never runs it, so its answer would
    always be "visible").  Graphics go to the HLE RSP as before.  ai_dump.so
    records the emulator's AI stream to EMUDIR/emu.wav (+ ai_log.txt)."""
    tools = os.path.join(PORT, "build/audio_tools")
    r = subprocess.call(["make", "-s", "-C", os.path.join(HERE, "audio"), "B=" + tools,
                         tools + "/rsp_tap.so", tools + "/ai_dump.so"])
    if r != 0:
        die("can't build port/tools/audio")
    hle = "/usr/lib/x86_64-linux-gnu/mupen64plus/mupen64plus-rsp-hle.so"
    return {"M64RSP": tools + "/rsp_tap.so", "M64AUDIO": tools + "/ai_dump.so", "TAP_HLE": hle,
            "TAP_LLE_UCODE": "2E77B0", "TAP_OUT": os.path.join(emudir, "tap.txt"),
            "AI_DUMP": os.path.join(emudir, "emu.wav"), "AI_DUMP_LOG": os.path.join(emudir, "ai_log.txt"),
            "SDL_AUDIODRIVER": "dummy",
            # cxd4 runs the tasks it gets itself (graphics never reach it)
            "M64CFG": "rsp-cxd4:DisplayListToGraphicsPlugin:3:0;rsp-cxd4:AudioListToAudioPlugin:3:0"}


def cmd_emu(args):
    emudir = args.pop(0)
    kind = opt(args, "--kind", "nm")
    rom = opt(args, "--rom", None) or default_rom(kind)
    os.makedirs(emudir, exist_ok=True)
    # (stored relative to ROOT: other worktrees reuse the run, cached_elfs)
    with open(os.path.join(emudir, "elfs.txt"), "w") as f:
        f.write("\n".join(rom_elfs(kind)) + "\n")
    elfs = [os.path.join(ROOT, p) for p in rom_elfs(kind)]
    for f in os.listdir(emudir):
        if f.startswith("frame_"):
            os.remove(os.path.join(emudir, f))
    # a blank EEPROM (no saves), as the exe has without --eeprom
    savedir = os.path.join(emudir, "save")
    if os.path.isdir(savedir):
        for f in os.listdir(savedir):
            os.remove(os.path.join(savedir, f))
    env = dict(os.environ, M64SAVEDIR=savedir, CMP_DIR=emudir, CMP_ELFS=",".join(elfs), CMP_VIS=str(opt(args, "--vis", 36000)),
               CMP_DUMP=opt(args, "--dump", "every:10"), CMP_STOP=str(opt(args, "--stop", 0)),
               CMP_CALLS=opt(args, "--calls", ""), CMP_POKE=opt(args, "--poke", ""),
               CMP_SHOTS=opt(args, "--shots", ""),
               CMP_INPUT=os.path.abspath(opt(args, "--input", "")) if "--input" in args else "")
    if flag(args, "--lle"):
        env.update(lle_env(emudir))
    py = sys.executable
    # CMP_TRACER: a variant of the tracer
    tracer = os.environ.get("CMP_TRACER") or os.path.join(ROOT, "tools_port/m64trace/m64trace.py")
    with open(os.path.join(emudir, "emu.log"), "w") as log:
        r = subprocess.call([py, tracer, rom, os.path.join(HERE, "cmp_spec.py"), os.path.join(emudir, "emu.txt")],
                            env=env, stdout=log, stderr=subprocess.STDOUT, cwd=ROOT)
    if r != 0:
        die("emulator run failed (%s)" % os.path.join(emudir, "emu.log"))
    with open(os.path.join(emudir, "rom.sha1"), "w") as f:
        f.write(rom_sha1(rom) + "\n")
    with open(os.path.join(emudir, "spec.sha1"), "w") as f:
        f.write(spec_sha1() + "\n")
    cmd_inject([emudir])


def native_exe():
    """the bc_headless under test: CMP_EXE (the make targets pass the MSVC
    build's), else the MSVC build's in MSVC_DIR (default
    %LOCALAPPDATA%/blastcorps-msvc, as port/msvc.mk), Release"""
    e = os.environ.get("CMP_EXE")
    if e:
        return os.path.abspath(e)
    d = os.environ.get("MSVC_DIR")
    if not d:
        r = subprocess.run(["cmd.exe", "/c", "echo %LOCALAPPDATA%"], capture_output=True, text=True, cwd="/mnt/c")
        la = r.stdout.strip()
        if not la or "%" in la:
            die("no CMP_EXE and no MSVC_DIR: which bc_headless.exe? (make -C port msvc builds it)")
        d = subprocess.run(["wslpath", la], capture_output=True, text=True).stdout.strip() + "/blastcorps-msvc"
    return os.path.join(d, "Release", "bc_headless.exe")


def native_tag():
    """the native run folders get a suffix naming the build ("...-nativemsvc";
    the clang check's exe: "...-native64"), so runs of different builds against
    the same emulator run are kept apart; CMP_TAG overrides it.  A short hash
    of the exe's path follows ("...-nativemsvc-1a2b3c"): two worktrees (each
    with its own MSVC_DIR) testing the same NM ROM never share a native folder."""
    import hashlib
    tag = os.environ.get("CMP_TAG") or ("64" if native_exe().find("headless64") >= 0 else "msvc")
    return tag + "-" + hashlib.sha1(native_exe().encode()).hexdigest()[:6]


# ---------------------------------------------------------------- the cache
#
# The cache (CMP_CACHE, default ~/cmp_cache) is shared by every worktree.  An
# emulator run's folder is named by its run AND the short hash of the ROM it
# ran ("verify-nm-1500-990290a8", "level-nm-07-990290a8"), and its native
# counterparts by that plus the exe ("...-nativemsvc-1a2b3c"), so worktrees
# with different NM ROMs never write into each other's folders; worktrees with
# the same ROM share the (expensive) emulator run.  Every run holds a lock on
# its emulator folder (FOLDER.lock, flock) from the staleness check to the
# last comparison, so a second user of the same folder waits instead of
# rewriting it mid-check.  A folder is reused only when its rom.sha1 (the full
# hash) matches.  Nothing in a folder names the worktree that recorded it
# (elfs.txt is relative to ROOT, cached_elfs).  Folders from before this scheme (no hash in the name) are
# never touched; tools/cmp_cache_clean.py lists (and with --delete removes)
# folders no ROM given to it still needs.

def rom_tag(rom):
    """the short ROM hash in cache folder names"""
    return rom_sha1(rom)[:8]


class CacheLock:
    """an exclusive flock on EMUDIR.lock for the duration of a run"""

    def __init__(self, emudir):
        self.path = emudir.rstrip("/") + ".lock"

    def __enter__(self):
        import fcntl
        os.makedirs(os.path.dirname(self.path) or ".", exist_ok=True)
        self.f = open(self.path, "a+")
        try:
            fcntl.flock(self.f, fcntl.LOCK_EX | fcntl.LOCK_NB)
        except OSError:
            print("cache: %s is in use by another run, waiting ..." % self.path)
            sys.stdout.flush()
            fcntl.flock(self.f, fcntl.LOCK_EX)
        self.f.seek(0)
        self.f.truncate()
        self.f.write("pid %d cwd %s\n" % (os.getpid(), os.getcwd()))
        self.f.flush()
        return self

    def __exit__(self, *exc):
        import fcntl
        fcntl.flock(self.f, fcntl.LOCK_UN)
        self.f.close()
        return False


def cmd_native(args):
    emudir, natdir = args[0], args[1]
    args = args[2:]
    exe = opt(args, "--exe", native_exe())
    rom = opt(args, "--rom", None) or default_rom("base")
    frames = opt(args, "--frames", None)
    dump = opt(args, "--dump", "every:10")
    extra = args
    os.makedirs(natdir, exist_ok=True)
    for f in os.listdir(natdir):
        if f.startswith("frame_") or f.startswith("bc-crash-"):
            os.remove(os.path.join(natdir, f))
    boot, off, _ = open(os.path.join(emudir, "boot.txt")).read().split()
    every, lst = parse_dump(dump)
    cmd = [exe, wpath(rom), "--boot-count", boot, "--clock", wpath(os.path.join(emudir, "clock.txt")),
           "--syms", wpath(exe[:-4] + ".syms"), "--frame-done", wpath(os.path.join(emudir, "framedone.txt")),
           "--trace", wpath(os.path.join(natdir, "trace.txt")), "--dump-dir", wpath(natdir)]
    # never a dialog on the desktop (errors, crashes: run.log and bc-crash-*.txt/.dmp in natdir)
    cmd += ["--load-log", "--no-msgbox", "--crash-dir", wpath(natdir)]
    if os.path.exists(os.path.join(emudir, "framesp.txt")):
        cmd += ["--frame-sp", wpath(os.path.join(emudir, "framesp.txt"))]
    if not flag(extra, "--no-sync"):
        cmd += ["--sync", wpath(os.path.join(emudir, "sync.txt"))]
    if every:
        cmd += ["--dump-every", str(every)]
    if lst:
        cmd += ["--dump", ",".join(str(x) for x in sorted(lst))]
    if frames:
        cmd += ["--frames", str(frames)]
    cmd += extra
    with open(os.path.join(natdir, "run.log"), "w") as log:
        r = subprocess.call(cmd, stdout=log, stderr=subprocess.STDOUT, cwd=natdir)
    print("native: exit %d (%s)" % (r, os.path.join(natdir, "run.log")))
    for f in sorted(os.listdir(natdir)):
        if f.startswith("bc-crash-"):
            print("native: CRASH report %s" % os.path.join(natdir, f))
    return r


def parse_dump(s):
    every, frames = 0, set()
    for part in s.split("+"):
        if part.startswith("every:"):
            every = int(part[6:])
        elif "-" in part:
            a, b = part.split("-")
            frames.update(range(int(a), int(b) + 1))
        elif part:
            frames.update(int(x) for x in part.split(","))
    return every, frames


# ---------------------------------------------------------------- comparison

def load_ignore(dsyms, audio=False):
    """tools/compare_ignore.txt: `LO HI reason` (hex, [LO, HI)) or `SYMBOL [SIZE] reason`;
    plus the .data/.bss of the libultra objects the platform layer replaces"""
    mask = bytearray(SIZE // 4)
    reasons = []

    def ign(lo, hi, why):
        lo, hi = max(lo, BASE), min(hi, BASE + SIZE)
        if hi <= lo:
            return
        a, b = (lo - BASE) // 4, (hi - BASE + 3) // 4
        mask[a:b] = b"\x01" * (b - a)
        reasons.append((lo, hi, why))

    byname = {}
    for a, n, sz in dsyms.items:
        byname.setdefault(n, (a, sz))
    small = []  # re-applied after the pinned tables are unmasked

    def ign_small(lo, hi, why):
        ign(lo, hi, why)
        if hi - lo < 0x1000:
            small.append((lo, hi, why))

    section = ""
    for line in open(os.environ.get("CMP_IGNORE") or os.path.join(HERE, "compare_ignore.txt")):
        if line.startswith("# ---"):
            section = line[5:].strip()
        if audio and section == "audio":
            continue   # (diff --audio: the audio subsystem is compared)
        line = line.split("#")[0].strip()
        if not line:
            continue
        f = line.split(None, 2)
        if re.match(r"^[0-9A-Fa-f]{8}$", f[0]):
            ign_small(int(f[0], 16), int(f[1], 16), f[2] if len(f) > 2 else "")
        else:
            if f[0] not in byname:
                die("compare_ignore.txt: unknown symbol %s" % f[0])
            a, sz = byname[f[0]]
            if len(f) > 1 and re.match(r"^(0x)?[0-9A-Fa-f]+$", f[1]):
                sz = int(f[1], 16)
                why = f[2] if len(f) > 2 else ""
            else:
                why = " ".join(f[1:])
            if sz == 0:
                die("compare_ignore.txt: %s needs a size" % f[0])
            ign_small(a, a + sz, why)
    # libultra os/io objects replaced by the platform layer: their data and bss
    native_ul = set()
    mk = open(os.path.join(PORT, "Makefile")).read()
    m = re.search(r"^HL_UL :=((?:.*\\\n)*.*)$", mk, re.M)
    if m:
        native_ul = set(m.group(1).replace("\\\n", " ").split())
    native_ul |= {"ul_xprintf", "ul_xldtob", "ul_crc"}
    for mp in ("build_nm/hd_code.us.v11.map", "build_nm/hd_front_end.us.v11.map"):
        for line in open(os.path.join(ROOT, mp)):
            m = re.match(r"^ \.(data|bss|rodata|sdata|sbss)\s+0x([0-9a-f]+)\s+0x([0-9a-f]+)\s+\S*/(ul_\w+)\.c\.o", line)
            if m and m.group(4) not in native_ul:
                lo = int(m.group(2), 16)
                if BASE <= lo < BASE + SIZE:
                    ign(lo, lo + int(m.group(3), 16), "libultra %s (platform layer)" % m.group(4))
    # the data tables pinned inside the hd_code text (NM_PIN_HD_CODE) are compared
    from elftools.elf.elffile import ELFFile
    with open(os.path.join(ROOT, "build_nm/hd_code.us.v11.elf"), "rb") as f:
        elf = ELFFile(f)
        pins = [(s["sh_addr"], s["sh_addr"] + s["sh_size"]) for s in elf.iter_sections()
                if s.name.startswith(".nm_pin_")]
        for sec in elf.iter_sections():
            if sec.name != ".symtab":
                continue
            for sym in sec.iter_symbols():
                a, sz = sym["st_value"], sym["st_size"]
                if sz and sym["st_info"]["type"] == "STT_OBJECT" and any(lo <= a < hi for lo, hi in pins):
                    i, j = (a - BASE) // 4, (a + sz - BASE) // 4
                    mask[i:j] = bytes(j - i)
    for lo, hi, why in small:
        a, b = (lo - BASE) // 4, (hi - BASE + 3) // 4
        mask[a:b] = b"\x01" * (b - a)
    return mask, reasons


def word_ok(n, e):
    """native 4 bytes vs emulator big-endian 4 bytes: some layout of u16/u8 agrees (u32 checked before)"""
    if n == e:
        return True
    h0 = n[0] == e[1] and n[1] == e[0]
    b0 = n[0] == e[0] and n[1] == e[1]
    h1 = n[2] == e[3] and n[3] == e[2]
    b1 = n[2] == e[2] and n[3] == e[3]
    return (h0 or b0) and (h1 or b1)


def covered(nat, e, o):
    """Every byte of the word at o lies in some unit that agrees: a byte, a u16
    at an even address, or a u32 at an even (not necessarily 4-aligned)
    address -- the game has u32 fields at 2 mod 4 in byte-copied records
    (the demos' vehicle snapshots)."""
    ok = [False] * 4
    for start, size in ((o - 2, 4), (o, 4), (o + 2, 4), (o, 2), (o + 2, 2)):
        if start < 0 or start + size > len(e):
            continue
        if nat[start:start + size] == e[start:start + size][::-1]:
            for b in range(max(start, o), min(start + size, o + 4)):
                ok[b - o] = True
    for b in range(4):
        if nat[o + b] == e[o + b]:
            ok[b] = True
    return all(ok)


class Cmp:
    def __init__(self, emudir, natdir, audio=False):
        self.emudir, self.natdir = emudir, natdir
        self.layout = None   # strict mode (Layout), set by diff --strict
        self.stale = {}      # rule_ignored: words judged stale, while both sides keep them
        self.garb = bytearray(SIZE)   # port_garbage bytes (until a load covers them)
        self.garb_n = 0
        self.dsyms = data_syms()
        self.mask, self.reasons = load_ignore(self.dsyms, audio)
        elfs = cached_elfs(emudir)
        self.efun = elf_syms(elfs, ("STT_FUNC", "STT_NOTYPE", "STT_OBJECT"))
        tails = [a for a, n, _ in self.efun.items if n == "__osThreadTail"]
        self.thread_tail = tails[0] if tails else -1
        # NM test ROM: code relinked at 0x80400000+ (hd_code, then the front end)
        self.nm_text_lo, self.nm_text_hi = (0x80400000, 0x80800000) if "build_nm" in elfs[0] else (0, 0)
        self.orig_fun = {}
        if self.nm_text_hi:
            for a, n, _ in elf_syms([os.path.join(ROOT, "build/hd_code.us.v11.elf"),
                                     os.path.join(ROOT, "build/hd_front_end.us.v11.elf")],
                                    ("STT_FUNC", "STT_NOTYPE", "STT_OBJECT")).items:
                if 0x801E7000 <= a < 0x80208040 or 0x802447C0 <= a < 0x802E8BD0:
                    self.orig_fun.setdefault(n, a)
        sp = native_exe()[:-4] + ".syms"
        self.nfun = nm_syms(sp) if os.path.exists(sp) else Syms().done()
        # the exe's image (pointers into it are its own code/data): i686 at
        # 0x400000, x86_64 at 0x40000000 (port64.mk)
        self.exe_lo, self.exe_hi = 0x00401000, 0x01000000
        if self.nfun.items and self.nfun.items[0][0] >= 0x40000000:
            self.exe_lo, self.exe_hi = 0x40001000, 0x48000000
        # end of the exe's .text: after it, pointers are data (rdata, data, bss)
        self.text_end = 0
        if os.path.exists(sp):
            for line in open(sp):
                p = line.split()
                if len(p) == 3 and p[2] in ("etext", "_etext", "__etext"):
                    self.text_end = int(p[0], 16)
        if not self.text_end and self.nfun.items:
            self.text_end = self.nfun.items[-1][0] + 0x1000
        # the exe's loads (--load-log): which asset an address came from
        self.loads = []
        self.loads_at = {}  # dump frame -> number of loads before it
        lp = os.path.join(natdir, "run.log")
        if os.path.exists(lp):
            for line in open(lp, errors="replace"):
                m = re.match(r"load: (\w+)\s+rom ([0-9A-F]+) -> ([0-9A-F]+) len ([0-9A-F]+): (.*)", line)
                if m:
                    self.loads.append((int(m.group(3), 16), int(m.group(4), 16), int(m.group(2), 16),
                                       m.group(5).strip(), m.group(1)))
                # bytes the game never writes nor reads (port_garbage)
                m = re.match(r"load: garbage ([0-9A-F]+) len ([0-9A-F]+)", line)
                if m:
                    self.loads.append((int(m.group(1), 16), int(m.group(2), 16), 0, "garbage", "garbage"))
                m = re.match(r"dump: .*frame_(\d+)\.bin", line)
                if m:
                    self.loads_at[int(m.group(1))] = len(self.loads)

    def asset(self, a, frame=None):
        n = self.loads_at.get(frame, len(self.loads))
        for dst, ln, rom, kind, how in reversed(self.loads[:n]):
            if dst <= a < dst + ln:
                return "%s rom %06X+0x%X" % (kind, rom, a - dst)
        return ""

    def codeptr(self, ev, nv):
        """A pointer into the exe image where the emulator has an N64 address:
        a function pointer (both name the same function), or a pointer to
        the native copy of constant data (string literals of C data tables:
        the exe keeps them in its own .rdata).  Also: an empty thread queue
        (libultra's __osThreadTail sentinel, the platform's NULL), and an NM
        ROM code address where the exe holds the original one (pointers into
        the front end's text that came with the data image)."""
        if nv == 0 and ev == self.thread_tail:
            return True
        if self.nm_text_lo <= ev < self.nm_text_hi and (0x801E7000 <= nv < 0x80208040 or 0x802447C0 <= nv < 0x802E8BD0):
            # jump tables and text pointers in the data images: NM addresses
            # don't map back exactly (rewritten functions change size); the
            # native code never jumps through them
            return True
        if not (0x80000000 <= ev < 0x80800000 and self.exe_lo <= nv < self.exe_hi):
            return False
        a = self.efun.find(ev)
        b = self.nfun.find(nv)
        if a is not None and b is not None and a[0] == ev and b[0] == nv:
            return a[1] == b[1]
        if (self.nm_text_lo <= ev < self.nm_text_hi and a is not None and a[0] != ev and b is not None
                and b[0] == nv and nv < self.text_end):
            # a function the NM ELF has no symbol for (IDO emits none for
            # static functions, e.g. libaudio's __CSPVoiceHandler) against the
            # start of a native function: a function pointer both ways
            return True
        return not (b is not None and b[0] <= nv < self.text_end)

    def rule_ignored(self, a, e, nat=None):
        """Conditional ignores (state-dependent, so not in compare_ignore.txt)."""
        # 60F60.c's 0x1010-byte texture HeapBlocks (gFxTexCaches .. gFxTexCachesEnd,
        # carved from heap that held other data before): the trailer's only
        # fields are the s32 at 0x1000, the u16 age at 0x1004 and the byte key
        # at 0x1006; bytes 0x1007-0x100F are never written, so they keep
        # whatever the memory held before, in that data's own byte order
        o = a - BASE
        if nat is not None:
            # sound handles (sndPlaySfx's out parameter, e.g. gJbombThrustSound or in
            # heap objects): which slot of the sound player's state array
            # (0x40-byte entries from 0x80399FF0) a sound gets, and when it
            # ends (the handle goes NULL), follow the audio thread's timing,
            # which isn't modelled (compare_ignore.txt)
            ev = int.from_bytes(e[o:o + 4], "big")
            nv = int.from_bytes(nat[o:o + 4], "little")

            def snd(v):
                return 0x80399FF0 <= v < 0x8039AFF0 and (v - 0x80399FF0) % 0x40 == 0
            if (snd(ev) or ev == 0) and (snd(nv) or nv == 0) and (ev or nv):
                return True
        # 34430.c mb_render_view: the RDP renders a 120x90 RGBA16 view into
        # the buffer gMbBuffer points at (its depth image: gDepthBufferPhys's);
        # nothing renders natively, and the CPU doesn't read the pixels
        for ptr in (0x8036D170, 0x80358058):
            p = int.from_bytes(e[ptr - BASE:ptr - BASE + 4], "big") | 0x80000000
            if p <= a < p + 120 * 90 * 2:
                return True
        # 34430.c mb_init passes the heap pointer gHeapPtr's value,
        # not its address, to the round-up helper align_up (an original
        # bug, kept): it "rounds" the stale s32 just past that buffer (the
        # emulator's text Vtx x/y pair there gets y += 15; natively the same
        # bytes are a host-order word, already a multiple of 8).  Nothing reads it.
        p = int.from_bytes(e[0x8036D170 - BASE:0x8036D174 - BASE], "big") | 0x80000000
        if a == p + 0x5460:
            return True
        lo = int.from_bytes(e[0x803EB788 - BASE:0x803EB78C - BASE], "big")
        hi = int.from_bytes(e[0x803EB78C - BASE:0x803EB790 - BASE], "big")
        if nat is not None and self.stale.get(o) == (nat[o:o + 4], e[o:o + 4]):
            return True   # (a trailer of an earlier level's blocks, unchanged since)
        if lo <= a < hi and nat is not None:
            r = (a - lo) % 0x1010
            # a free block (inUse at 0x1000 is 0 on both sides: fx_alloc_texture_caches
            # clears it, the age and key at 0x1004 are only written once the
            # block is used): its trailer is all stale bytes
            u = o - r + 0x1000
            free = r == 0x1004 and e[u:u + 4] == bytes(4) and nat[u:u + 4] == bytes(4)
            if r in (0x1008, 0x100C) or free or (
                    r == 0x1004 and nat[o:o + 2] == e[o:o + 2][::-1] and nat[o + 2] == e[o + 2]):
                self.stale[o] = (nat[o:o + 4], e[o:o + 4])
                return True
        return False

    def frame(self, n):
        e = open(os.path.join(self.emudir, "frame_%07d.bin" % n), "rb").read()[:SIZE]
        with open(os.path.join(self.natdir, "frame_%07d.bin" % n), "rb") as f:
            nat = f.read(SIZE)
        sw = array.array("I", nat)
        sw.byteswap()
        n32 = sw.tobytes()
        diffs = []
        mask = self.mask
        # the garbage bytes logged before this dump: they stay stale until a
        # load lands on them (game code writing over them later goes
        # unnoticed: a known blind spot of this rule)
        upto = self.loads_at.get(n, len(self.loads))
        if upto < self.garb_n:
            self.garb, self.garb_n = bytearray(SIZE), 0
        for dst, ln, rom, kind, how in self.loads[self.garb_n:upto]:
            lo, hi = max(dst - BASE, 0), min(dst + ln - BASE, SIZE)
            if hi > lo:
                self.garb[lo:hi] = (b"\x01" if how == "garbage" else b"\x00") * (hi - lo)
        self.garb_n = upto
        garb = self.garb
        P = 4096
        for p in range(0, SIZE, P):
            if n32[p:p + P] == e[p:p + P]:
                continue
            for o in range(p, p + P, 4):
                if n32[o:o + 4] == e[o:o + 4] or mask[o >> 2]:
                    continue
                nb, eb = nat[o:o + 4], e[o:o + 4]
                if word_ok(nb, eb):
                    continue
                q = o & ~7  # u64/f64
                if nat[q:q + 8] == e[q:q + 8][::-1]:
                    continue
                if covered(nat, e, o):
                    continue
                ev = int.from_bytes(eb, "big")
                nv = int.from_bytes(nb, "little")
                if self.codeptr(ev, nv) or self.codeptr(ev, int.from_bytes(nb, "big")):
                    continue
                if self.rule_ignored(BASE + o, e, nat):
                    continue
                if any(garb[o:o + 4]):
                    # compare with the garbage bytes blanked on both sides
                    g = garb[o - 4:o + 8] if o >= 4 else bytes(4) + garb[o:o + 8]
                    nm = bytearray(nat[o - 4:o + 8] if o >= 4 else bytes(4) + nat[o:o + 8])
                    em = bytearray(e[o - 4:o + 8] if o >= 4 else bytes(4) + e[o:o + 8])
                    for k in range(12):
                        if g[k]:
                            nm[k] = em[k] = 0
                    if word_ok(nm[4:8], em[4:8]) or covered(nm, em, 4):
                        continue
                diffs.append(o)
        self.strict_only, self.typed = [], 0
        if self.layout is not None:
            # words the lenient rules accept but the typed layout doesn't
            self.layout.advance(n)
            sw, self.typed = self.layout.check(nat, e)
            have = set(diffs)
            for o in sw:
                if o in have or mask[o >> 2]:
                    continue
                ev = int.from_bytes(e[o:o + 4], "big")
                nv = int.from_bytes(nat[o:o + 4], "little")
                if self.codeptr(ev, nv) or self.rule_ignored(BASE + o, e, nat):
                    continue
                self.strict_only.append(o)
        return diffs, nat, e

    def runs(self, diffs, gap=16):
        out = []
        for o in diffs:
            if out and o - out[-1][1] <= gap:
                out[-1][1] = o + 4
                out[-1][2] += 1
            else:
                out.append([o, o + 4, 1])
        return out

    def describe(self, n, diffs, nat, e, detail, label=None):
        rs = self.runs(diffs)
        if label is None:
            print("frame %d: %d words differ in %d runs" % (n, len(diffs), len(rs)))
        for lo, hi, cnt in rs[:detail]:
            a = BASE + lo
            it = self.dsyms.find(a)
            nm = self.dsyms.name(a)
            sz = ""
            if it and it[2]:
                sz = " (%s size 0x%X)" % (it[1], it[2])
            ast = self.asset(a, n)
            if ast:
                sz += " [%s]" % ast
            samples = []
            for o in diffs:
                if lo <= o < hi and len(samples) < 3:
                    samples.append("+%X emu %s nat %s" % (o - lo, e[o:o + 4].hex(), nat[o:o + 4].hex()))
            # (both columns are memory bytes; a native u32 shows byte-reversed)
            print("  %08X-%08X %-28s %4d words%s | %s" % (a, BASE + hi, nm, cnt, sz, "; ".join(samples)))
        if len(rs) > detail:
            print("  ... %d more runs" % (len(rs) - detail))


class Layout:
    """Strict mode: the width each RDRAM byte is read at, from sources
    independent of the swaps under test --
      - the C declarations (typemap.py with TYPEMAP_ALL=1: every symbol,
        .bss included; multi-byte leaves only, since `extern u8 D_x[]` says
        nothing about how other files cast it),
      - the original code's traced reads of the data images
        (data/image_widths.txt) and of every traced asset (widths.py facts:
        per ROM asset, offset -> width), placed where the exe's load log
        (--load-log) put each asset, in load order up to the dump.
    A unit (start, width) expects the native bytes to be the emulator's
    reversed (bytes: equal).  Bytes no unit covers, or several do, stay
    lenient.  Texture decodes produce big-endian texels: bytes."""

    ONES = {w: b"\x01" * w for w in (1, 2, 4, 8)}
    FE = (0x801E7000, 0x8021ED00)   # the front end's text, data and bss
    # assets whose traced widths are polluted (the tracer charges an access to
    # the last load at the address; heap reused by other code afterwards)
    UNTYPED_ROM = {0x6EC4C0: "packed-object table (u32 offsets, read by lvl_load_object only): its 0x800 "
                             "bytes of heap are reused by the collision/level loaders in the trace"}
    UNTYPED = [("gTexDecodeScratch", "60F60.c texture decode scratch: declared u64 (the copy loop's unit), "
                              "holds the packed s16 tokens"),
               ("gMtxWork", "62740.c accumulated Mtx: declared u16[], used as Mtx words (PORT_HALF)"),
               ("gMtxChainTemp", "62740.c product Mtx: declared u16[], used as Mtx words (PORT_HALF)"),
               ("gGroundTriSwap", "62740.c record swap temp: declared s32[] (the copy unit), holds s16 records")]

    def __init__(self, cmp, typemap, facts):
        self.cmp = cmp
        self.start = bytearray(SIZE)   # width of the unit starting here
        self.cover = bytearray(SIZE)   # how many units cover the byte (saturating)
        self.n_loads = 0
        units = {1: set(), 2: set(), 4: set(), 8: set()}
        tsize = {}
        for line in open(typemap):
            p = line.split()
            if len(p) < 4:
                continue
            a = int(p[0], 16)
            tsize[p[1]] = (a, int(p[2]))
            for leaf in p[4:]:
                o, w = leaf.split(":")
                units[int(w.rstrip("f"))].add(a + int(o, 16) - BASE)
        img = {"hd": 0x802E8BD0, "fe": 0x80208040}
        seen = collections.defaultdict(set)
        for line in open(os.path.join(PORT, "data/image_widths.txt")):
            if line.startswith("#"):
                continue
            r, o, w = line.split()
            seen[img[r] + int(o, 16) - BASE].add(int(w))
        for a, ws in seen.items():
            if len(ws) == 1:
                units[ws.pop()].add(a)
        for w, l in units.items():
            self._add(sorted(l), w)
        # the hand decisions for the image swap (data/image_overrides.txt)
        for line in open(os.path.join(PORT, "data/image_overrides.txt")):
            p = line.split("#")[0].split()
            if len(p) < 3:
                continue
            # optional STRIDE COUNT: the same range in COUNT records (swaptab.py)
            stride, count = (int(p[3], 16), int(p[4], 16)) if len(p) >= 5 else (0, 1)
            for k in range(count):
                lo, n = int(p[0], 16) - BASE + k * stride, int(p[1], 16)
                self._clear(lo, lo + n)
                if p[2] == "be" or re.match(r"^w[248]$", p[2]):
                    w = 1 if p[2] == "be" else int(p[2][1:])
                    self._add(range(lo, lo + n, w), w)
            # (other actions, e.g. `vtx`: RSP-only data in the renderer's
            # layout, no CPU reader: lenient)
        # C objects' .data/.rodata: the exe links its own copies (pinned ones
        # are copied over the image in their declared host layout, the rest is
        # never read at the N64 address), so the original code's traced widths
        # (struct copies read words over byte fields) don't apply there
        self.excl = []
        for mp in ("build_nm/hd_code.us.v11.map", "build_nm/hd_front_end.us.v11.map"):
            for line in open(os.path.join(ROOT, mp)):
                m = re.match(r"^ \.(data|rodata|late_rodata)\s+0x([0-9a-f]+)\s+0x([0-9a-f]+)\s+(\S+)", line)
                if m and not m.group(4).endswith(".bin.o"):
                    lo = int(m.group(2), 16) - BASE
                    if 0 <= lo < SIZE and int(m.group(3), 16):
                        self.excl.append((lo, min(lo + int(m.group(3), 16), SIZE)))
        for lo, hi in self.excl:
            self._clear(lo, hi)
        # declared types that aren't the readers' layout
        for sym, why in self.UNTYPED:
            if sym in tsize:
                a, sz = tsize[sym]
                self._clear(a - BASE, a - BASE + sz)
        self.base_start, self.base_cover = bytes(self.start), bytes(self.cover)
        # asset facts: ROM -> (size, how, {width: offsets}); offsets read at
        # several widths stay lenient
        self.facts = {}
        cur = None
        if facts and os.path.exists(facts):
            for line in open(facts):
                p = line.split()
                if not p or p[0] == "#":
                    continue
                if p[0] == "@":
                    cur = {1: [], 2: [], 4: [], 8: []}
                    self.facts[int(p[1], 16)] = (int(p[2], 16), p[3], cur)
                elif cur is not None:
                    # (an offset read at several widths gets all of them: the
                    # overlapping units leave its bytes lenient)
                    for w in p[1].split(","):
                        cur[int(w)].append(int(p[0], 16))
            # a narrower read inside a wider one is a cast of that field
            # (IDO reads `(u16) word` as lhu at +2, `(u8) word` as lbu at +3),
            # which host order gets right: the wider unit stands
            for sz, how, d in self.facts.values():
                s4 = set(d[4])
                s2 = set(d[2])
                d[2] = [o for o in d[2] if o in s4 or (o - 2) not in s4]
                d[1] = [o for o in d[1] if not any((o - k) in s4 for k in (1, 2, 3)) and (o - 1) not in s2]

    def _add(self, offs, w, base=0, limit=SIZE):
        st, cv = self.start, self.cover
        for o in offs:
            o += base
            if 0 <= o and o + w <= limit:
                st[o] = w
                for k in range(o, o + w):
                    if cv[k] < 255:
                        cv[k] += 1

    def _clear(self, lo, hi):
        self.start[lo:hi] = bytes(hi - lo)
        self.cover[lo:hi] = bytes(hi - lo)

    def advance(self, frame):
        """apply the exe's loads made before FRAME's dump"""
        n = self.cmp.loads_at.get(frame, len(self.cmp.loads))
        if n < self.n_loads:
            self.start[:], self.cover[:] = self.base_start, self.base_cover
            self.n_loads = 0
        for dst, ln, rom, kind, how in self.cmp.loads[self.n_loads:n]:
            lo, hi = max(dst - BASE, 0), min(dst + ln - BASE, SIZE)
            if hi <= lo:
                continue
            if kind.startswith("level"):
                # a new level resets the heap: earlier assets' layouts are
                # stale, and so is the front end's (its memory is heap now)
                self.start[:], self.cover[:] = self.base_start, self.base_cover
                self._clear(self.FE[0] - BASE, self.FE[1] - BASE)
            elif kind.startswith("front end"):
                a, b = self.FE[0] - BASE, self.FE[1] - BASE
                self.start[a:b], self.cover[a:b] = self.base_start[a:b], self.base_cover[a:b]
            if how == "decode" or "(as is)" in kind:
                # decoded texels, and assets the load layer keeps as they are
                # (pictures, e.g. the 320x240 one at ROM 6BF2F0: the original
                # code's traced u16 pixel reads don't make them host order):
                # big-endian bytes
                self.start[lo:hi] = b"\x01" * (hi - lo)
                self.cover[lo:hi] = b"\x01" * (hi - lo)
                continue
            # whatever was typed there before is gone; textures stay big-endian
            # until 60F60.c decodes them (port_texture_input swaps the tokens
            # in place then), so they stay lenient
            self._clear(lo, hi)
            f = self.facts.get(rom) if "texture" not in kind and rom not in self.UNTYPED_ROM else None
            if f is not None:
                for w, offs in f[2].items():
                    self._add(offs, w, lo, hi)
                if kind.startswith(("front end", "image")):
                    for a, b in self.excl:
                        if a < hi and b > lo:
                            self._clear(max(a, lo), min(b, hi))
        self.n_loads = n

    def check(self, nat, e):
        """(byte offsets of the words that differ under the typed layout,
        bytes the layout types)"""
        st, cv = self.start, self.cover
        bad = set()
        # bytes: runs of 1-byte units compared as slices
        for m in re.finditer(b"\x01+", st):
            a, b = m.span()
            if nat[a:b] != e[a:b]:
                for s in range(a, b):
                    if nat[s] != e[s] and cv[s] == 1:
                        bad.add(s & ~3)
        for w in (2, 4, 8):
            ones = self.ONES[w]
            for m in re.finditer(re.escape(bytes([w])), st):
                s = m.start()
                if nat[s:s + w] != e[s:s + w][::-1] and cv[s:s + w] == ones:
                    bad.add(s & ~3)
                    bad.add((s + w - 1) & ~3)
        typed = SIZE - cv.count(0)
        return sorted(bad), typed


def frames_in(d):
    out = set()
    for f in os.listdir(d):
        m = re.match(r"frame_(\d{7})\.bin$", f)
        if m and int(m.group(1)) != 9999999:
            out.add(int(m.group(1)))
    return out


def cmd_diff(args):
    emudir, natdir = args[0], args[1]
    args = args[2:]
    lo = opt(args, "--from", 0, int)
    hi = opt(args, "--to", 1 << 30, int)
    detail = opt(args, "--detail", 30, int)
    show_all = flag(args, "--all")
    brief = flag(args, "--brief")   # (with --all: no lines for matching frames)
    strict = flag(args, "--strict")
    # --audio: also compare the audio subsystem (compare_ignore.txt's audio section);
    # meaningful with an emu --lle run (the emulator's real audio frame sizes)
    audio = flag(args, "--audio")
    typemap = opt(args, "--typemap", os.path.join(PORT, "build/gen/typemap_all.txt"))
    facts = opt(args, "--facts", os.path.join(PORT, "build/gen/facts.txt"))
    c = Cmp(emudir, natdir, audio)
    if strict:
        if not os.path.exists(typemap):
            die("strict mode needs %s (make -C port strict-data)" % typemap)
        c.layout = Layout(c, typemap, facts)
    common = sorted(x for x in frames_in(emudir) & frames_in(natdir) if lo <= x <= hi)
    if not common:
        die("no common dumps")
    first = None
    matched = 0
    strict_bad = 0
    for n in common:
        diffs, nat, e = c.frame(n)
        if strict:
            if c.strict_only:
                strict_bad += 1
                print("frame %d: strict: %d words match only leniently (0x%X bytes typed)" % (
                    n, len(c.strict_only), c.typed))
                c.describe(n, c.strict_only, nat, e, detail, "  strict")
            elif show_all and not brief:
                print("frame %d: strict: no extra differences (0x%X bytes typed)" % (n, c.typed))
        if not diffs:
            matched += 1
            if show_all and not brief:
                print("frame %d: match" % n)
            continue
        if first is None:
            first = n
            c.describe(n, diffs, nat, e, detail)
            if not show_all:
                break
        else:
            rs = c.runs(diffs)
            print("frame %d: %d words differ in %d runs, first %s" % (n, len(diffs), len(rs),
                                                                       c.dsyms.name(BASE + rs[0][0])))
    compared = len(common) if show_all or first is None else common.index(first) + 1
    print("diff: %d of %d compared frames match%s" % (
        matched, compared, "; first difference at frame %d" % first if first is not None else ""))
    if strict:
        print("strict: %d frames with words that match only leniently" % strict_bad)
    return matched, compared, first, strict_bad


def native_trace(natdir):
    out = {}
    p = os.path.join(natdir, "trace.txt")
    if not os.path.exists(p):
        return out
    for line in open(p):
        m = re.match(r"(\d+) vi=(\d+) t=\d+ mode=([0-9A-F]+) lvl=(-?\d+) mf=(\d+) gvi=(\d+)", line)
        if m:
            out[int(m.group(1))] = (int(m.group(2)), int(m.group(3), 16), int(m.group(4)), int(m.group(5)),
                                    int(m.group(6)))
    return out


def cmd_frames(args):
    emudir, natdir = args[0], args[1]
    ev = parse_emu(emudir)
    _, _, off = emu_timing(ev)
    nat = native_trace(natdir)
    same_vi = same_state = 0
    shown = 0
    limit = int(args[2]) if len(args) > 2 else 20
    for f in ev["F"]:
        n = f["n"]
        if n not in nat:
            continue
        evi = int(f["vi"]) + off
        es = (int(f["mode"], 16), int(f["lvl"], 16), int(f["mf"], 16), int(f["gvi"], 16))
        nv = nat[n]
        ns = (nv[1], nv[2], nv[3], nv[4])
        if evi == nv[0]:
            same_vi += 1
        if es == ns:
            same_state += 1
        if (evi != nv[0] or es != ns) and shown < limit:
            shown += 1
            print("frame %d: emu vi %d mode %X lvl %d mf %d gvi %d | native vi %d mode %X lvl %d mf %d gvi %d"
                  % ((n, evi) + es + (nv[0],) + ns))
    both = len([f for f in ev["F"] if f["n"] in nat])
    print("frames: %d in both; same submission retrace %d, same (mode, level, frames-in-mode, game VI) %d"
          % (both, same_vi, same_state))
    return both, same_vi, same_state


def cmd_hex(args):
    """hex EMUDIR NATDIR FRAME ADDR [LEN]: emulator bytes | native bytes | native as LE words"""
    emudir, natdir, n, a = args[0], args[1], int(args[2]), int(args[3], 16)
    ln = int(args[4], 16) if len(args) > 4 else 0x100
    e = open(os.path.join(emudir, "frame_%07d.bin" % n), "rb").read()
    nat = open(os.path.join(natdir, "frame_%07d.bin" % n), "rb").read()
    ds = data_syms()
    for o in range(a - BASE, a - BASE + ln, 16):
        eb, nb = e[o:o + 16], nat[o:o + 16]
        mark = "  " if eb == nb else "!="
        nw = " ".join("%08x" % int.from_bytes(nb[i:i + 4], "little") for i in range(0, 16, 4))
        print("%08X %s %s | %s | %s  %s" % (BASE + o, mark, eb.hex(), nb.hex(), nw, ds.name(BASE + o)))


def cmd_calls(args):
    """calls EMUDIR NATDIR: the calls logged on both sides (compare.py emu --calls F,G and the
    exe's --calls F,G), frame by frame: the first frame where the sequences differ"""
    """(--args: also compare the arguments, a0-a3 / the first four stack words;
    --nargs F=N,G=M: how many of them F and G take (default 4; an int-sized
    argument each); --all: list every differing frame, not just the first)"""
    emudir, natdir = args[0], args[1]
    with_args = flag(args, "--args")
    nargs = dict((x.split("=")[0], int(x.split("=")[1])) for x in opt(args, "--nargs", "").split(",") if x)
    show_all = flag(args, "--all")
    ev = parse_emu(emudir)
    fs = elf_syms(cached_elfs(emudir), ("STT_FUNC",))
    emu = {}
    for f, ra, fr, a in ev.get("K", []):
        it = fs.find(ra)
        emu.setdefault(fr, []).append((f, it[1] if it else "?", a))
    nat = {}
    for line in open(os.path.join(natdir, "run.log"), errors="replace"):
        m = re.match(r"call: (\S+) from (\S+) frame (\d+)(?: args (\S+))?", line)
        if m:
            nat.setdefault(int(m.group(3)), []).append((m.group(1), m.group(2), m.group(4) or ""))

    def key(x):
        if not with_args:
            return x[0]
        return (x[0], tuple(x[2].split(",")[:nargs.get(x[0], 4)]))

    last = max(list(emu) + list(nat) + [0])
    bad = 0
    for fr in range(0, last + 1):
        a, b = emu.get(fr, []), nat.get(fr, [])
        # (callers are shown, not compared: gcc inlines differently)
        if [key(x) for x in a] != [key(y) for y in b]:
            bad += 1
            print("frame %d: emulator %d calls, exe %d" % (fr + 1, len(a), len(b)))
            for i in range(max(len(a), len(b))):
                x = a[i] if i < len(a) else ("-", "", "")
                y = b[i] if i < len(b) else ("-", "", "")
                print("  %s %-14s from %-14s %-36s | %-14s from %-14s %s" % (
                    "  " if key(x) == key(y) else "!=", x[0], x[1], x[2], y[0], y[1], y[2]))
            if not show_all:
                return
    print("calls: %d of %d frames the same" % (last + 1 - bad, last + 1))


def demo_ranges(ev):
    """{demo: (first frame, last frame)} of the attract demos' first showing (from the
    frame the demo number is set to the next demo's; the attract cycle repeats)"""
    out = {}
    cur = None
    for f in ev["F"]:
        d = int(f["demo"], 16)
        if d == 0xFFFFFFFF or int(f["mode"], 16) not in (2, 1 << 48):
            continue
        if d != cur:
            if d in out:
                break
            cur = d
            out[d] = (f["n"], f["n"])
        out[d] = (out[d][0], f["n"])
    return out


def cmd_demos(args):
    ev = parse_emu(args[0])
    cur = None
    for f in ev["F"]:
        key = (f["lvl"], f["demo"], f["mode"])
        if key != cur:
            cur = key
            print("frame %6d vi %6s: mode %s level %s demo %s" % (f["n"], f["vi"], f["mode"], f["lvl"], f["demo"]))
    for d, (lo, hi) in sorted(demo_ranges(ev).items()):
        print("demo %d: frames %d-%d" % (d, lo, hi))


def cmd_run(args):
    """run --demo N [--cache DIR] [--kind nm|base] [--vis V] [--every K]: the lot, with the
    emulator side cached (re-run when cmp_spec.py or the ROM is newer)"""
    demo = opt(args, "--demo", 0, int)
    cache = os.path.expanduser(opt(args, "--cache", "~/cmp_cache"))
    kind = opt(args, "--kind", "nm")
    # the attract demos take about 3300 VIs each (demo 0 from VI ~630)
    vis = opt(args, "--vis", min(32000, 3700 * (demo + 1) + 600), int)
    every = opt(args, "--every", 10, int)
    rom = default_rom(kind)
    emudir = os.path.join(cache, "attract-%s-demo%d-%s" % (kind, demo, rom_tag(rom)))
    natdir = emudir + "-native" + native_tag()
    with CacheLock(emudir):
        run_demo(demo, kind, vis, every, emudir, natdir, rom)


def run_demo(demo, kind, vis, every, emudir, natdir, rom):
    if emu_stale(emudir, rom):
        print("compare: emulator run (%s ROM, %d VIs, dumps every %d frames) into %s ..." % (kind, vis, every, emudir))
        cmd_emu([emudir, "--kind", kind, "--lle", "--vis", str(vis), "--dump", "every:%d" % every])
    else:
        cmd_inject([emudir])
    ranges = demo_ranges(parse_emu(emudir))
    if demo not in ranges:
        die("demo %d not in the emulator run (demos %s); raise --vis" % (demo, sorted(ranges)))
    lo, hi = ranges[demo]
    r = cmd_native([emudir, natdir, "--frames", str(hi + 1), "--dump", "every:%d" % every])
    cmd_frames([emudir, natdir, "5"])
    cmd_diff([emudir, natdir, "--from", str(lo), "--to", str(hi), "--all", "--detail", "20"])
    print("demo %d: frames %d-%d%s" % (demo, lo, hi, "" if r == 0 else " (the exe stopped early: exit %d)" % r))


def emu_stale(emudir, rom):
    """the cached emulator run predates the ROM, the spec or the tracer"""
    log = os.path.join(emudir, "emu.txt")
    if not os.path.exists(log) or not os.path.exists(os.path.join(emudir, "boot.txt")):
        return True
    t = os.path.getmtime(log)
    # the ROM by content (make nmrom rewrites it every time)
    sp = os.path.join(emudir, "rom.sha1")
    if os.path.exists(sp):
        if open(sp).read().strip() != rom_sha1(rom):
            return True
    elif t < os.path.getmtime(rom):
        return True
    # the spec and the tracer by content when the run recorded them (worktrees
    # sharing the folder have their own checkouts, with their own mtimes)
    ss = os.path.join(emudir, "spec.sha1")
    if os.path.exists(ss):
        return open(ss).read().strip() != spec_sha1()
    return any(t < os.path.getmtime(p) for p in spec_files())


def spec_files():
    return (os.path.join(HERE, "cmp_spec.py"), os.path.join(ROOT, "tools_port/m64trace/m64trace.py"))


def spec_sha1():
    import hashlib
    h = hashlib.sha1()
    for p in spec_files():
        h.update(open(p, "rb").read())
    return h.hexdigest()


_rom_sha1 = {}


def rom_sha1(rom):
    import hashlib
    key = (os.path.abspath(rom), os.path.getmtime(rom), os.path.getsize(rom))
    if key not in _rom_sha1:
        _rom_sha1[key] = hashlib.sha1(open(rom, "rb").read()).hexdigest()
    return _rom_sha1[key]


def run_log_facts(natdir):
    """summary lines of the exe's run.log worth reporting"""
    out = {}
    for line in open(os.path.join(natdir, "run.log"), errors="replace"):
        m = re.match(r"cull: (\d+) tests, (\d+) not visible(?:; (\d+) answers from --clock, the model disagrees "
                     r"with (\d+))?", line)
        if m:
            out["cull_tests"], out["cull_hidden"] = int(m.group(1)), int(m.group(2))
            if m.group(3):
                out["cull_model_disagree"] = int(m.group(4))
        m = re.match(r"sync: thread (\d+): (\d+) of (\d+) points used .*, (\d+) mismatches", line)
        if m and m.group(1) == "3":
            out["sync_main_used"], out["sync_main_points"] = int(m.group(2)), int(m.group(3))
            out["sync_main_mismatches"] = int(m.group(4))
        m = re.match(r"audio: (\d+) tasks", line)
        if m:
            out["audio_tasks"] = int(m.group(1))
    return out


def verify_run(emudir, natdir, rom, frames, every, no_strict):
    """cmd_verify's runs and comparisons (under the cache folder's lock)"""
    if emu_stale(emudir, rom):
        print("verify: emulator run (NM ROM, LLE audio + visibility tests, %d frames, dumps every %d) into %s ..."
              % (frames, every, emudir))
        sys.stdout.flush()
        cmd_emu([emudir, "--kind", "nm", "--lle", "--vis", "40000", "--stop", str(frames),
                 "--dump", "every:%d" % every])
    else:
        print("verify: cached emulator run %s" % emudir)
        cmd_inject([emudir])
    sys.stdout.flush()
    r = cmd_native([emudir, natdir, "--frames", str(frames), "--dump", "every:%d" % every])
    res = {"exe_exit": r}
    res["frames"], res["timeline_retrace"], res["timeline_state"] = cmd_frames([emudir, natdir, "5"])
    typemap = os.path.join(PORT, "build/gen/typemap_all.txt")
    strict = os.path.exists(typemap) and not no_strict
    dargs = [emudir, natdir, "--all", "--brief", "--detail", "6"] + (["--strict"] if strict else [])
    matched, compared, first, strict_bad = cmd_diff(dargs)
    res["dumps"], res["dumps_match"] = compared, matched
    if strict:
        res["dumps_strict_ok"] = compared - strict_bad
    res.update(run_log_facts(natdir))
    tap = os.path.join(emudir, "tap.txt")
    if os.path.exists(tap):
        for line in open(tap):
            m = re.match(r"tap: audio tasks (\d+), identical (\d+), different (\d+).*?(\d+) other tasks on the LLE", line)
            if m:
                res["emu_audio_tasks"], res["emu_audio_lle_identical"] = int(m.group(1)), int(m.group(2))
                res["emu_cull_tasks_lle"] = int(m.group(4))
                res["emu_audio_lle_different"] = int(m.group(3))
    return res, matched, compared, strict


def cmd_verify(args):
    """verify [--cache DIR] [--frames N] [--every K] [--expect FILE] [--no-strict]: the quick
    regression check behind `make -C port verify`.  The emulator side (NM test ROM,
    LLE audio and visibility tests, the first N frames: boot, logos, front end,
    attract demo 0) is cached in DIR/verify-nm-N-ROMHASH and redone only when the ROM,
    cmp_spec.py or the tracer change; bc_headless follows it; reported: the
    per-frame timeline, the RAM dumps (lenient, and strict where `make -C port
    strict-data` has built the layouts), the visibility tests (emulator LLE vs the
    exe's model), the emulator's audio tasks (LLE vs the port's interpreter).
    Fails when a number is worse than in --expect (default data/verify_expect.txt)."""
    cache = os.path.expanduser(opt(args, "--cache", "~/cmp_cache"))
    frames = opt(args, "--frames", 1500, int)
    every = opt(args, "--every", 10, int)
    expect = opt(args, "--expect", os.path.join(PORT, "data/verify_expect.txt"))
    no_strict = flag(args, "--no-strict")
    rom = default_rom("nm")
    emudir = os.path.join(cache, "verify-nm-%d-%s" % (frames, rom_tag(rom)))
    natdir = emudir + "-native" + native_tag()
    with CacheLock(emudir):
        res, matched, compared, strict = verify_run(emudir, natdir, rom, frames, every, no_strict)
    print("verify: results")
    for k in sorted(res):
        print("  %-24s %s" % (k, res[k]))
    # expectations: "key min" (at least), "key =value", "key <=value"
    bad = []
    if os.path.exists(expect):
        section = None   # "frames N" lines start the expectations for N frames
        for line in open(expect):
            p = line.split("#")[0].split()
            if len(p) != 2:
                continue
            k, v = p
            if k == "frames":
                section = int(v)
                continue
            if section is not None and section != frames:
                continue
            have = res.get(k)
            if have is None:
                bad.append("%s missing" % k)
            elif v.startswith("<="):
                if have > int(v[2:]):
                    bad.append("%s = %s (expected at most %s)" % (k, have, v[2:]))
            elif v.startswith("="):
                if have != int(v[1:]):
                    bad.append("%s = %s (expected %s)" % (k, have, v[1:]))
            elif have < int(v):
                bad.append("%s = %s (expected at least %s)" % (k, have, v))
    if bad:
        print("verify: FAILED: " + "; ".join(bad))
        sys.exit(1)
    print("verify: OK (%d frames: timeline %d/%d, dumps %d/%d match%s)" % (
        frames, res["timeline_state"], res["frames"], matched, compared,
        ", strict %d/%d" % (res["dumps_strict_ok"], compared) if strict else ""))


# ---------------------------------------------------------------- levels

# How a run reaches level L (both sides, same input): data/levels_input.txt
# drives the menus (START/A from read 400: new game, player A) to the globe
# and presses A at read 985; at frame 930, before the globe first opens, the
# player's "last level" byte (players[0].pad0[8], 00000.c case 0x4000) is
# set to L, so the globe opens centred on L and A selects it.  The game then
# loads L its own way (intro page / sequence / bonus info, A taps), and the
# input drives the vehicle from read 1220.
LEVEL_POKE_FRAME = 930
LEVEL_SLOT0_LAST = 0x80364AF8


def level_names():
    """level number -> name (the globe table's strings, hd_front_end 11530.c)"""
    p = os.path.join(PORT, "data/levels.txt")
    out = {}
    if os.path.exists(p):
        for line in open(p):
            f = line.split("#")[0].split(None, 1)
            if f and f[0].isdigit():
                out[int(f[0])] = f[1].strip() if len(f) > 1 else ""
    return out


def run_level(level, cache, frames, every, input_path, kind="nm", shots="", force=False, keep=False,
              strict=False):
    """one level: the emulator side (cached by ROM, spec, input and parameters), then
    bc_headless, timeline + dump comparison; returns a dict of results"""
    rom = default_rom(kind)
    emudir, natdir = level_dirs(cache, kind, level, rom)
    with CacheLock(emudir):
        return run_level_locked(level, frames, every, input_path, kind, shots, force, keep, strict,
                                rom, emudir, natdir)


def level_dirs(cache, kind, level, rom):
    """a level's emulator and native folders in the cache"""
    emudir = os.path.join(cache, "level-%s-%02d-%s" % (kind, level, rom_tag(rom)))
    return emudir, emudir + "-native" + native_tag()


def run_level_locked(level, frames, every, input_path, kind, shots, force, keep, strict, rom, emudir, natdir):
    poke = "%d:%08X:1:%X" % (LEVEL_POKE_FRAME, LEVEL_SLOT0_LAST, level)
    # dumps once the level is under way (from frame 1000), every 20 frames up to
    # 1100 whatever EVERY is: the level loads around 1013-1172, and the
    # comparator's stale-trailer rule (60F60.c HeapBlocks) learns from the
    # frames where the blocks still cover the earlier level's memory
    frs = sorted(set(range(1000, min(1100, frames + 1), 20)) | set(range(1000, frames + 1, every)))
    dump = "+".join(str(f) for f in frs)
    params = "level=%d frames=%d poke=%s input=%s shots=%s\n" % (level, frames, poke, rom_sha1(input_path), shots)
    pfile = os.path.join(emudir, "params.txt")
    # (a cached run with more dumps will do: the frames this run compares must be there)
    want = set(frs)
    stale = force or emu_stale(emudir, rom) or not os.path.exists(pfile) or open(pfile).read() != params \
        or not want <= frames_in(emudir)
    if stale:
        print("level %d: emulator run (%s ROM, %d frames) into %s ..." % (level, kind, frames, emudir))
        sys.stdout.flush()
        if os.path.exists(pfile):
            os.remove(pfile)
        cmd_emu([emudir, "--kind", kind, "--lle", "--vis", str(frames * 4), "--stop", str(frames),
                 "--dump", dump.replace("+", ","), "--input", input_path, "--poke", poke]
                + (["--shots", shots] if shots else []))
        open(pfile, "w").write(params)
    else:
        cmd_inject([emudir])
    sys.stdout.flush()
    r = cmd_native([emudir, natdir, "--frames", str(frames), "--dump", dump.replace("+", ","),
                    "--input", wpath(input_path), "--poke", poke])
    res = {"level": level, "exe_exit": r}
    res["frames"], res["timeline_retrace"], res["timeline_state"] = cmd_frames([emudir, natdir, "3"])
    # the level's own frames: from the first frame in a level mode with level L
    ev = parse_emu(emudir)
    nat = native_trace(natdir)
    lvl_frames = [f["n"] for f in ev["F"] if int(f["lvl"], 16) == level and int(f["mode"], 16) in (4, 0x100)]
    res["level_from"] = lvl_frames[0] if lvl_frames else 0
    res["level_frames"] = len(lvl_frames)
    res["emu_modes"] = " ".join(sorted(set("%X" % int(f["mode"], 16) for f in ev["F"]
                                           if f["n"] >= LEVEL_POKE_FRAME and int(f["lvl"], 16) == level)))
    res["native_frames"] = len(nat)
    typemap = os.path.join(PORT, "build/gen/typemap_all.txt")
    strict = strict and os.path.exists(typemap)
    if frames_in(emudir) & frames_in(natdir):
        dargs = [emudir, natdir, "--all", "--brief", "--detail", "6"] + (["--strict"] if strict else [])
        matched, compared, first, strict_bad = cmd_diff(dargs)
        res["dumps"], res["dumps_match"], res["first_diff"] = compared, matched, first or 0
        if strict:
            res["dumps_strict_ok"] = compared - strict_bad
    else:
        res["dumps"] = res["dumps_match"] = 0
    res.update(run_log_facts(natdir))
    if not keep:
        # keep the logs, drop the native dumps (the emulator's stay: they are the cache)
        for f in os.listdir(natdir):
            if f.startswith("frame_"):
                os.remove(os.path.join(natdir, f))
    return res


def cmd_level(args):
    """level --level L [--cache DIR] [--frames N] [--every K] [--input FILE] [--kind nm|base]
    [--shots F,..] [--force] [--keep]: reach level L in the emulator and in bc_headless with
    the same input (see LEVEL_POKE_FRAME) and compare them"""
    level = opt(args, "--level", 0, int)
    cache = os.path.expanduser(opt(args, "--cache", "~/cmp_cache"))
    frames = opt(args, "--frames", 1900, int)
    every = opt(args, "--every", 20, int)
    inp = os.path.abspath(opt(args, "--input", os.path.join(PORT, "data/levels_input.txt")))
    kind = opt(args, "--kind", "nm")
    shots = opt(args, "--shots", "")
    # (--strict: also the typed comparison; in a level, heap data lands on
    # declared arrays like the frame buffers gFrameBuffers, so it reports noise)
    res = run_level(level, cache, frames, every, inp, kind, shots, flag(args, "--force"), flag(args, "--keep"),
                    flag(args, "--strict"))
    print("level: results")
    for k in sorted(res):
        print("  %-24s %s" % (k, res[k]))
    return res


def _level_job(a):
    """one level of verify-levels, its output in the log next to its native
    folder (CACHE/level-nm-LL-ROMHASH-nativeTAG.log)"""
    lv, cache, frames, every, inp = a
    log = open(level_dirs(cache, "nm", lv, default_rom("nm"))[1] + ".log", "w")
    sys.stdout.flush()
    saved = os.dup(1)
    os.dup2(log.fileno(), 1)
    try:
        return run_level(lv, cache, frames, every, inp)
    except SystemExit as e:
        return {"level": lv, "exe_exit": -1, "frames": 0, "timeline_retrace": 0, "timeline_state": 0,
                "level_frames": 0, "level_from": 0, "dumps": 0, "dumps_match": 0, "error": str(e)}
    finally:
        sys.stdout.flush()
        os.dup2(saved, 1)
        os.close(saved)
        log.close()


def cmd_verify_levels(args):
    """verify-levels [--cache DIR] [--levels a,b-c] [--frames N] [--every K] [--expect FILE]
    [--jobs J]: every level (data/levels.txt) through run_level (J at a time, each one's
    output in DIR/level-nm-LL-ROMHASH-nativeTAG.log); fails when a level's numbers are worse than in --expect
    (default data/verify_levels_expect.txt: `L key VALUE` lines, VALUE as in
    verify_expect.txt; `* key VALUE` applies to every level).  Prints a table."""
    cache = os.path.expanduser(opt(args, "--cache", "~/cmp_cache"))
    frames = opt(args, "--frames", 1900, int)
    every = opt(args, "--every", 100, int)
    expect = opt(args, "--expect", os.path.join(PORT, "data/verify_levels_expect.txt"))
    inp = os.path.abspath(opt(args, "--input", os.path.join(PORT, "data/levels_input.txt")))
    names = level_names()
    sel = opt(args, "--levels", None)
    if sel:
        levels = []
        for part in sel.split(","):
            a, _, b = part.partition("-")
            levels += list(range(int(a), int(b or a) + 1))
    else:
        levels = sorted(names) or list(range(60))
    jobs = opt(args, "--jobs", 1, int)
    exp = {}
    if os.path.exists(expect):
        for line in open(expect):
            p = line.split("#")[0].split()
            if len(p) == 3:
                exp.setdefault(p[0], []).append((p[1], p[2]))
    table, bad = [], []
    os.makedirs(cache, exist_ok=True)
    print("verify-levels: %d levels, %d at a time (each level's log: %s.log)"
          % (len(levels), jobs, level_dirs(cache, "nm", 0, default_rom("nm"))[1].replace("-00-", "-LL-")))
    sys.stdout.flush()
    if jobs > 1:
        import concurrent.futures
        with concurrent.futures.ProcessPoolExecutor(jobs) as pool:
            results = list(pool.map(_level_job, [(lv, cache, frames, every, inp) for lv in levels]))
    else:
        results = [_level_job((lv, cache, frames, every, inp)) for lv in levels]
    for lv, res in zip(levels, results):
        table.append(res)
        for k, v in exp.get("*", []) + exp.get(str(lv), []):
            have = res.get(k)
            if have is None:
                bad.append("level %d: %s missing" % (lv, k))
            elif v.startswith("<="):
                if have > int(v[2:]):
                    bad.append("level %d: %s = %s (expected at most %s)" % (lv, k, have, v[2:]))
            elif v.startswith("="):
                if have != int(v[1:]):
                    bad.append("level %d: %s = %s (expected %s)" % (lv, k, have, v[1:]))
            elif have < int(v):
                bad.append("level %d: %s = %s (expected at least %s)" % (lv, k, have, v))
    print("verify-levels: %d levels, %d frames each" % (len(table), frames))
    print("  lvl name                  exit timeline  in-level  dumps    strict first")
    for r in table:
        print("  %3d %-20s %5d %4d/%-4d %4d@%-4d %3d/%-3d %3s/%-3s %s" % (
            r["level"], names.get(r["level"], "")[:20], r["exe_exit"], r["timeline_state"], r["frames"],
            r["level_frames"], r["level_from"], r["dumps_match"], r["dumps"], r.get("dumps_strict_ok", "-"),
            r["dumps"], r.get("first_diff") or "-"))
    if bad:
        print("verify-levels: FAILED: " + "; ".join(bad))
        sys.exit(1)
    print("verify-levels: OK")


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        sys.exit(1)
    cmd, args = sys.argv[1], sys.argv[2:]
    {"emu": cmd_emu, "inject": cmd_inject, "native": cmd_native, "diff": cmd_diff, "frames": cmd_frames,
     "demos": cmd_demos, "hex": cmd_hex, "run": cmd_run, "calls": cmd_calls, "verify": cmd_verify,
     "level": cmd_level, "verify-levels": cmd_verify_levels}[cmd](args)


if __name__ == "__main__":
    main()
