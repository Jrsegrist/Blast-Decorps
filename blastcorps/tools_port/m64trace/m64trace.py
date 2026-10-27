#!/usr/bin/env python3
"""Minimal mupen64plus front end (ctypes over libmupen64plus.so.2) that logs
register state at breakpoints without stopping (the debugger's update callback
logs and resumes), so thousands of hits per run are cheap. Video glide64mk2
(the frame loop needs the RDP), rsp-hle, no audio; cached interpreter
(EMUMODE=0/1/2 overrides), 8 MB. Input: input-sdl with controller 1 plugged
and no device (no input), or, when the spec defines INPUT, the scripted input
plugin m64input.so (built from m64input.c next to this file on first use).

usage: m64trace.py ROM SPEC.py OUT.txt     (M64VERBOSE=1 for the core's messages,
       M64SAVEDIR=dir for the EEPROM/pak/SRAM files instead of the user's save dir,
       M64PAK=1 for a Controller Pak in controller 1 with scripted INPUT)
SPEC.py defines:
  BPS = {addr: "label" | ("label", [gprs], [fprs])}   exec breakpoints (may be empty)
  VIS = 60*120                      stop after this many VI interrupts (60 per second)
  GPRS = [...], FPRS = [12, ...]    default registers to log (FPRs as single bits + value)
  MEM  = [(addr, size), ...]        memory logged at every hit (size 1/2/4)
  REGMEM = {addr: [("a0", off, size)]}  memory at reg+off (only KSEG0 RAM is read)
  ONHIT = f(pc, gprs, read)         extra text per hit (None: don't log this hit)
  DUMPS = {addr: (start, length)}   RAM dump to OUT.dumpN at every hit
  MAXHITS = {addr: n}, DEFMAX       disable a breakpoint after n hits
  DEDUPE = True                     write a line only for new (breakpoint, values) tuples
  INPUT = f(vi, read, ctl) -> (buttons, stick_x, stick_y)
                                    called once per VI; the result is controller 1's
                                    state (N64 button word, s8 stick) until the next
                                    call. ctl.log(text) writes "#in vi=N text" to OUT;
                                    ctl.save(path) / ctl.load(path) queue a save state
                                    save / load (taken at the next VI); ctl.stop() ends
                                    the run; ctl.write(addr, size, value) pokes RAM;
                                    ctl.polls = the game's controller reads so far;
                                    ctl.hits = {bp addr: hits}; ctl.vars = a dict the
                                    script may keep state in.
  LOADSTATE = "path"              load this save state at the first VI (the INPUT
                                    script keeps running from there)
  WATCH = [(addr, size), ...]       logged as "#w vi=N ..." whenever one changes
  WRITES = [(lo, hi), ...]          memory write breakpoints (RDRAM, [lo, hi)); each
  ONWRITE = f(pc, addr, gprs, read) write calls ONWRITE (before the store): its text is
                                    logged as "W vi=N pc=PC | text" (None: not logged)
Output: one line per (new) hit, then "#count LABEL N | values" lines.
NM test ROM addresses come from build_nm/hd_code.rom.us.v11.elf (text at 0x804xxxxx).
Summaries: summarise.py OUT.txt; RAM dump diffs: dumpcmp.py A B [n lo hi].
"""
import ctypes as C, sys, os, struct, time, runpy, subprocess

ROM, SPECF, OUTF = sys.argv[1], sys.argv[2], sys.argv[3]
spec = runpy.run_path(SPECF)
BPS = {int(a): l for a, l in spec.get("BPS", {}).items()}
VIS = spec.get("VIS", 60 * 120)
GPRN = ["r0", "at", "v0", "v1", "a0", "a1", "a2", "a3", "t0", "t1", "t2", "t3", "t4", "t5", "t6", "t7",
        "s0", "s1", "s2", "s3", "s4", "s5", "s6", "s7", "t8", "t9", "k0", "k1", "gp", "sp", "fp", "ra"]
GPRS = spec.get("GPRS", ["v0", "v1", "a0", "a1", "a2", "a3", "t0", "t1", "t2", "t3", "t4", "t5", "t6", "t7",
                         "s0", "s1", "s2", "s3", "s4", "s5", "s6", "s7", "t8", "t9", "gp", "sp", "fp", "ra"])
FPRS = spec.get("FPRS", [12, 14, 20, 22, 24, 26])
MEM = spec.get("MEM", [(0x802E8BD8, 4), (0x802E8BEC, 4), (0x80364456, 1), (0x80358060, 4)])
REGMEM = spec.get("REGMEM", {})
MAXHITS = spec.get("MAXHITS", {})
DEFMAX = spec.get("DEFMAX", 400)
ONHIT = spec.get("ONHIT")
DEDUPE = spec.get("DEDUPE", True)
DUMPS = {int(a): v for a, v in spec.get("DUMPS", {}).items()}
INPUT = spec.get("INPUT")
LOADSTATE = spec.get("LOADSTATE")
WATCH = spec.get("WATCH", [])
WRITES = spec.get("WRITES", [])
ONWRITE = spec.get("ONWRITE")

core = C.CDLL("/usr/lib/x86_64-linux-gnu/libmupen64plus.so.2")
PLUG = "/usr/lib/x86_64-linux-gnu/mupen64plus/"
HERE = os.path.dirname(os.path.abspath(__file__))

DEBUGCB = C.CFUNCTYPE(None, C.c_void_p, C.c_int, C.c_char_p)
STATECB = C.CFUNCTYPE(None, C.c_void_p, C.c_int, C.c_int)
INITCB = C.CFUNCTYPE(None)
UPDCB = C.CFUNCTYPE(None, C.c_uint)
VICB = C.CFUNCTYPE(None)

quiet = os.environ.get("M64VERBOSE") is None


def dbgmsg(ctx, level, msg):
    if not quiet or level <= 2:
        sys.stderr.write("[%d] %s\n" % (level, msg.decode(errors="replace")))


def statecb(ctx, p, v):
    pass


dbgmsg_c = DEBUGCB(dbgmsg)
statecb_c = STATECB(statecb)

core.CoreStartup.argtypes = [C.c_int, C.c_char_p, C.c_char_p, C.c_void_p, DEBUGCB, C.c_void_p, STATECB]
cfgdir = os.path.expanduser("~/trace_m64cfg_py")
os.makedirs(cfgdir, exist_ok=True)
r = core.CoreStartup(0x020001, cfgdir.encode(), b"/usr/share/games/mupen64plus", None, dbgmsg_c, None, statecb_c)
assert r == 0, r

core.ConfigOpenSection.argtypes = [C.c_char_p, C.POINTER(C.c_void_p)]
core.ConfigSetParameter.argtypes = [C.c_void_p, C.c_char_p, C.c_int, C.c_void_p]


def setp(sec, name, typ, val):
    h = C.c_void_p()
    assert core.ConfigOpenSection(sec.encode(), C.byref(h)) == 0
    if typ in (1, 3):
        v = C.c_int(val)
    else:
        v = C.c_char_p(val.encode())
    r = core.ConfigSetParameter(h, name.encode(), typ, C.byref(v) if typ in (1, 3) else v)
    assert r == 0, (sec, name, r)


setp("Core", "EnableDebugger", 3, 1)
setp("Core", "R4300Emulator", 1, int(os.environ.get("EMUMODE", "1")))
setp("Core", "DisableExtraMem", 3, 0)
setp("Core", "OnScreenDisplay", 3, 0)
if os.environ.get("M64SAVEDIR"):  # EEPROM / Controller Pak / SRAM files (default: the user's save dir)
    os.makedirs(os.environ["M64SAVEDIR"], exist_ok=True)
    setp("Core", "SaveSRAMPath", 4, os.environ["M64SAVEDIR"])  # M64TYPE_STRING
setp("Video-General", "Fullscreen", 3, 0)
setp("Video-General", "ScreenWidth", 1, 320)
setp("Video-General", "ScreenHeight", 1, 240)
for i in (1, 2, 3, 4):
    setp("Input-SDL-Control%d" % i, "plugged", 3, 1 if i == 1 else 0)
    setp("Input-SDL-Control%d" % i, "mode", 1, 0)
    setp("Input-SDL-Control%d" % i, "device", 1, -1)

# ROM
data = open(ROM, "rb").read()
buf = C.create_string_buffer(data, len(data))
core.CoreDoCommand.argtypes = [C.c_int, C.c_int, C.c_void_p]
assert core.CoreDoCommand(1, len(data), buf) == 0  # ROM_OPEN


def input_plugin():
    """Path of the scripted input plugin, (re)built from m64input.c if needed."""
    import hashlib
    src = os.path.join(HERE, "m64input.c")
    # one build per source version (worktrees with different sources share ~/.cache)
    tag = hashlib.md5(open(src, "rb").read()).hexdigest()[:8]
    so = os.path.join(os.path.expanduser("~/.cache"), "m64input-%s.so" % tag)
    if not os.path.exists(so):
        os.makedirs(os.path.dirname(so), exist_ok=True)
        subprocess.check_call(["gcc", "-shared", "-fPIC", "-O2", "-o", so, src])
    os.environ["M64INPUT_SO"] = so  # (specs read its poll count: ctypes.CDLL(so) is the same handle)
    return so


# M64RSP / M64AUDIO: other RSP / audio plugins (e.g. port/tools/audio's rsp_tap.so,
# LLE audio and visibility tests, and ai_dump.so); M64CFG="Section:Name:type:value;..."
# config values set before the plugins start (type 1 int, 3 bool, 4 string)
for item in filter(None, os.environ.get("M64CFG", "").split(";")):
    sec, name, typ, val = item.split(":", 3)
    try:
        setp(sec, name, int(typ), val if int(typ) == 4 else int(val))
    except AssertionError:
        sys.stderr.write("m64trace: can't set %s\n" % item)
inlib = None
plugs = []
_plugins = [(2, PLUG + "mupen64plus-video-glide64mk2.so"),
            (4, input_plugin() if INPUT else PLUG + "mupen64plus-input-sdl.so"),
            (1, os.environ.get("M64RSP") or PLUG + "mupen64plus-rsp-hle.so")]
if os.environ.get("M64AUDIO"):
    _plugins.insert(1, (3, os.environ["M64AUDIO"]))
for typ, name in _plugins:
    lib = C.CDLL(name)
    lib.PluginStartup.argtypes = [C.c_void_p, C.c_void_p, DEBUGCB]
    if typ == 4 and INPUT and os.environ.get("M64PAK"):  # Controller Pak in controller 1
        C.c_int.in_dll(lib, "m64input_plugin").value = 2
    r = lib.PluginStartup(C.c_void_p(core._handle), None, dbgmsg_c)
    assert r == 0, (name, r)
    r = core.CoreAttachPlugin(typ, C.c_void_p(lib._handle))
    assert r == 0, (name, r)
    plugs.append(lib)
    if typ == 4 and INPUT:
        inlib = lib

core.DebugGetCPUDataPtr.restype = C.c_void_p
core.DebugGetCPUDataPtr.argtypes = [C.c_int]
core.DebugMemRead32.restype = C.c_uint32
core.DebugMemRead32.argtypes = [C.c_uint32]
core.DebugMemRead16.restype = C.c_uint16
core.DebugMemRead16.argtypes = [C.c_uint32]
core.DebugMemRead8.restype = C.c_uint8
core.DebugMemRead8.argtypes = [C.c_uint32]
core.DebugMemWrite32.argtypes = [C.c_uint32, C.c_uint32]
core.DebugMemWrite16.argtypes = [C.c_uint32, C.c_uint16]
core.DebugMemWrite8.argtypes = [C.c_uint32, C.c_uint8]
core.DebugSetRunState.argtypes = [C.c_int]


class BKP(C.Structure):
    _fields_ = [("address", C.c_uint), ("endaddr", C.c_uint), ("flags", C.c_uint)]


core.DebugBreakpointCommand.argtypes = [C.c_int, C.c_uint, C.POINTER(BKP)]
core.DebugBreakpointTriggeredBy.argtypes = [C.POINTER(C.c_uint32), C.POINTER(C.c_uint32)]

out = open(OUTF, "w")
state = {"vi": 0, "started": False, "hits": {}, "bpidx": {}}
regs_p = None
fpr_p = None


def rdmem(a, sz):
    a &= 0xFFFFFFFF
    if sz == 1:
        return core.DebugMemRead8(a)
    if sz == 2:
        return core.DebugMemRead16(a)
    if sz == 8:
        return (core.DebugMemRead32(a) << 32) | core.DebugMemRead32(a + 4)
    return core.DebugMemRead32(a)


def gpr(i):
    return C.cast(regs_p, C.POINTER(C.c_int64))[i] & 0xFFFFFFFF


def fpr(i):
    p = C.cast(fpr_p, C.POINTER(C.POINTER(C.c_uint32)))[i]
    return p[0]


def upd(pc):
    global regs_p, fpr_p
    if regs_p is None:
        regs_p = core.DebugGetCPUDataPtr(2)
        fpr_p = core.DebugGetCPUDataPtr(7)
    if not state["started"]:
        state["started"] = True
        for a in BPS:
            b = BKP(a, a, 1 | 8)
            state["bpidx"][a] = core.DebugBreakpointCommand(2, 0, C.byref(b))
        for lo, hi in WRITES:
            b = BKP(lo & 0x1FFFFFFF, (hi - 1) & 0x1FFFFFFF, 1 | 4)
            core.DebugBreakpointCommand(2, 0, C.byref(b))
    elif WRITES and pc not in BPS:
        # a memory write breakpoint: ONWRITE(pc, address, gprs, read) -> text or None
        fl, ad = C.c_uint32(), C.c_uint32()
        core.DebugBreakpointTriggeredBy(C.byref(fl), C.byref(ad))
        if fl.value & 4 and ONWRITE:
            g = {nm: gpr(i) for i, nm in enumerate(GPRN)}
            try:
                t = ONWRITE(pc, ad.value | 0x80000000, g, rdmem)
            except Exception as e:
                t = "onwrite-err %r" % e
            if t is not None:
                out.write("W vi=%d pc=%x | %s\n" % (state["vi"], pc, t))
    elif pc in BPS:
        n = state["hits"].get(pc, 0) + 1
        state["hits"][pc] = n
        lab = BPS[pc]
        gl, fl = GPRS, FPRS
        if isinstance(lab, tuple):
            lab, gl, fl = lab[0], lab[1], (lab[2] if len(lab) > 2 else FPRS)
        parts = []
        parts.append(" ".join("%x:%x" % (a, rdmem(a, s)) for a, s in MEM))
        g = {nm: gpr(i) for i, nm in enumerate(GPRN)}
        parts.append(" ".join("%s=%x" % (nm, g[nm]) for nm in gl))
        if fl:
            parts.append(" ".join("f%d=%08x(%g)" % (i, fpr(i), struct.unpack(">f", struct.pack(">I", fpr(i)))[0]) for i in fl))
        for rn, off, sz in REGMEM.get(pc, []) if isinstance(REGMEM, dict) else []:
            ad = (g[rn] + off) & 0xFFFFFFFF
            parts.append("[%s+%x]=%s" % (rn, off, ("%x" % rdmem(ad, sz)) if 0x80000000 <= ad < 0x80800000 else "-"))
        skip = False
        if ONHIT:
            try:
                extra = ONHIT(pc, g, rdmem)
                if extra is None:
                    skip = True
                else:
                    parts.append(extra)
            except Exception as e:
                parts.append("onhit-err %r" % e)
        if pc in DUMPS:
            st, ln = DUMPS[pc]
            with open("%s.dump%d" % (OUTF, n), "w") as df:
                df.write("# %s vi=%d level=%x demo=%x\n" % (lab, state["vi"], rdmem(0x802E8BDC, 4), rdmem(0x802E8BEC, 4)))
                for a in range(st, st + ln, 4):
                    df.write("%08x %08x\n" % (a, rdmem(a, 4)))
        body = " | ".join(p for p in parts if p)
        key = (pc, body)
        seen = state.setdefault("seen", {})
        if skip:
            pass
        elif not DEDUPE or key not in seen:
            seen[key] = 1
            out.write("%s vi=%d n=%d | %s\n" % (lab, state["vi"], n, body))
        else:
            seen[key] += 1
        if n >= MAXHITS.get(pc, DEFMAX):
            core.DebugBreakpointCommand(7, state["bpidx"][pc], None)  # DISABLE
    core.DebugSetRunState(2)
    core.DebugStep()


class Ctl:
    """Handle passed to the spec's INPUT function."""

    def __init__(self):
        self.vars = {}
        self.hits = state["hits"]
        self.polls = 0

    def log(self, text):
        out.write("#in vi=%d %s\n" % (state["vi"], text))
        out.flush()

    def save(self, path):
        self.log("save state %s" % path)
        core.CoreDoCommand(11, 1, C.c_char_p(path.encode()))  # STATE_SAVE, m64p format

    def load(self, path):
        self.log("load state %s" % path)
        core.CoreDoCommand(10, 0, C.c_char_p(path.encode()))  # STATE_LOAD

    def stop(self):
        self.log("stop")
        core.CoreDoCommand(6, 0, None)  # STOP

    def write(self, addr, size, value):
        {1: core.DebugMemWrite8, 2: core.DebugMemWrite16, 4: core.DebugMemWrite32}[size](addr, value)


ctl = Ctl()
if inlib is not None:
    keys = C.c_uint32.in_dll(inlib, "m64input_keys")
    polls = C.c_uint32.in_dll(inlib, "m64input_polls")


def n64_to_plugin(buttons, x, y):
    """N64 button word + stick -> mupen64plus BUTTONS word (see m64input.c)."""
    b = buttons & 0xFFFF
    return ((b >> 8) | ((b & 0xFF) << 8)) | ((x & 0xFF) << 16) | ((y & 0xFF) << 24)


def vi():
    state["vi"] += 1
    v = state["vi"]
    if v == 1 and LOADSTATE:
        ctl.load(LOADSTATE)
    if WATCH:
        cur = tuple(rdmem(a, s) for a, s in WATCH)
        if cur != state.get("watch"):
            state["watch"] = cur
            out.write("#w vi=%d %s\n" % (v, " ".join("%x:%x" % (a, c) for (a, _), c in zip(WATCH, cur))))
    if INPUT:
        ctl.polls = polls.value
        try:
            b, x, y = INPUT(v, rdmem, ctl)
        except Exception as e:
            ctl.log("input-err %r" % e)
            b, x, y = 0, 0, 0
        keys.value = n64_to_plugin(b, x, y)
    if v % 600 == 0:
        out.flush()
        sys.stderr.write("vi %d hits %s\n" % (v, sum(state["hits"].values())))
    if v >= VIS:
        core.CoreDoCommand(6, 0, None)  # STOP


def init():
    pass


init_c, upd_c, vi_c = INITCB(init), UPDCB(upd), VICB(vi)
core.DebugSetCallbacks.argtypes = [INITCB, UPDCB, VICB]
core.DebugSetCallbacks(init_c, upd_c, vi_c)
t0 = time.time()
r = core.CoreDoCommand(5, 0, None)  # EXECUTE (blocks)
out.write("# done vi=%d hits=%s secs=%.0f\n" % (state["vi"], {hex(k): v for k, v in state["hits"].items()}, time.time() - t0))
for (pc, body), c in sorted(state.get("seen", {}).items(), key=lambda kv: (kv[0][0], -kv[1])):
    lab = BPS[pc][0] if isinstance(BPS[pc], tuple) else BPS[pc]
    out.write("#count %s %d | %s\n" % (lab, c, body))
out.close()
core.CoreDoCommand(2, 0, None)
print("done", state["vi"], {hex(k): v for k, v in state["hits"].items()})
