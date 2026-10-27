#!/usr/bin/env python3
"""Access-width tracer (stage 3 "load"): runs the original ROM in mupen64plus
(cached interpreter, debugger on) through boot, front end and the attract
demos and records, for every CPU load/store into
  - the hd_code .data/.rodata image (0x802E8BD0-0x8030F660),
  - the front-end .data/.rodata image (0x80208040-0x80210E90, while resident),
  - every asset loaded through hd_rom_load (decompressed, at its dest) and
    every other osPiStartDma (raw, at its dram address),
the access width (1/2/4/8, U = lwl/lwr/swl/swr) and whether it was a read or
a write, plus whether the first access to each byte was a read (only those
bytes need their ROM value in host order).

Memory breakpoints (physical ranges, flags READ|WRITE) report through the
debugger's update callback; the width comes from decoding the instruction at
pc.  A breakpoint whose hits stop producing new (address, width, kind) facts
is disabled for a while (hot tables), and re-armed periodically and at every
new load.

usage: load_m64widths.py ROM OUT VIS
OUT lines:
  L vi rom dest size kind ra         a load (kind gz = hd_rom_load, dma = osPiStartDma)
  A region off width kind count pc   region = 'hd' | 'fe' | 'r<ROMHEX>' (asset by ROM start)
  F region off                       first access to that byte was a read
"""
import ctypes as C, sys, os, time, collections

ROM, OUTF, VIS = sys.argv[1], sys.argv[2], int(sys.argv[3])
core = C.CDLL("/usr/lib/x86_64-linux-gnu/libmupen64plus.so.2")
PLUG = "/usr/lib/x86_64-linux-gnu/mupen64plus/"
DEBUGCB = C.CFUNCTYPE(None, C.c_void_p, C.c_int, C.c_char_p)
STATECB = C.CFUNCTYPE(None, C.c_void_p, C.c_int, C.c_int)
INITCB = C.CFUNCTYPE(None)
UPDCB = C.CFUNCTYPE(None, C.c_uint)
VICB = C.CFUNCTYPE(None)


def dbgmsg(ctx, level, msg):
    if level <= 1:
        sys.stderr.write("[%d] %s\n" % (level, msg.decode(errors="replace")))


dbgmsg_c = DEBUGCB(dbgmsg)
statecb_c = STATECB(lambda c, p, v: None)
core.CoreStartup.argtypes = [C.c_int, C.c_char_p, C.c_char_p, C.c_void_p, DEBUGCB, C.c_void_p, STATECB]
cfgdir = os.path.expanduser("~/trace_m64cfg_load")
os.makedirs(cfgdir, exist_ok=True)
assert core.CoreStartup(0x020001, cfgdir.encode(), b"/usr/share/games/mupen64plus", None, dbgmsg_c, None,
                        statecb_c) == 0
core.ConfigOpenSection.argtypes = [C.c_char_p, C.POINTER(C.c_void_p)]
core.ConfigSetParameter.argtypes = [C.c_void_p, C.c_char_p, C.c_int, C.c_void_p]


def setp(sec, name, typ, val):
    h = C.c_void_p()
    assert core.ConfigOpenSection(sec.encode(), C.byref(h)) == 0
    v = C.c_int(val)
    assert core.ConfigSetParameter(h, name.encode(), typ, C.byref(v)) == 0


setp("Core", "EnableDebugger", 3, 1)
setp("Core", "R4300Emulator", 1, int(os.environ.get("EMUMODE", "1")))
setp("Core", "DisableExtraMem", 3, 0)
setp("Core", "OnScreenDisplay", 3, 0)
# W_SAVEDIR=dir: EEPROM / pak files there (emptied first: a blank EEPROM, as
# compare.py's runs have) instead of the user's mupen64plus save directory,
# where a previous run's save changes the menus (W_INPUT runs need this)
if os.environ.get("W_SAVEDIR"):
    _sd = os.environ["W_SAVEDIR"]
    os.makedirs(_sd, exist_ok=True)
    for _f in os.listdir(_sd):
        os.remove(os.path.join(_sd, _f))
    _h = C.c_void_p()
    core.ConfigOpenSection(b"Core", C.byref(_h))
    core.ConfigSetParameter(_h, b"SaveSRAMPath", 4, C.c_char_p((_sd + "/").encode()))
setp("Video-General", "Fullscreen", 3, 0)
setp("Video-General", "ScreenWidth", 1, 320)
setp("Video-General", "ScreenHeight", 1, 240)
for i in (1, 2, 3, 4):
    setp("Input-SDL-Control%d" % i, "plugged", 3, 1 if i == 1 else 0)
    setp("Input-SDL-Control%d" % i, "mode", 1, 0)
    setp("Input-SDL-Control%d" % i, "device", 1, -1)
data = open(ROM, "rb").read()
buf = C.create_string_buffer(data, len(data))
core.CoreDoCommand.argtypes = [C.c_int, C.c_int, C.c_void_p]
assert core.CoreDoCommand(1, len(data), buf) == 0
plugs = []
# W_INPUT=file (bc_headless --input format): controller 1 read by read, through
# the scripted input plugin (tools_port/m64trace/m64input.c), set at each
# osContStartReadData; W_POKE=R:ADDR:SIZE:VAL,... (hex ADDR/VAL): RAM writes at
# controller read R (e.g. compare.py level's globe poke: 925:80364AF8:1:L)
W_INPUT = os.environ.get("W_INPUT")
W_POKES = [(int(r), int(a, 16), int(s), int(v, 16)) for r, a, s, v in
           (p.split(":") for p in os.environ.get("W_POKE", "").split(",") if p)]
_inplug = None
if W_INPUT:
    import hashlib, subprocess
    _src = os.path.join(os.path.dirname(os.path.abspath(__file__)), "../../tools_port/m64trace/m64input.c")
    _so = os.path.expanduser("~/.cache/m64input-%s.so" % hashlib.md5(open(_src, "rb").read()).hexdigest()[:8])
    if not os.path.exists(_so):
        os.makedirs(os.path.dirname(_so), exist_ok=True)
        subprocess.check_call(["gcc", "-shared", "-fPIC", "-O2", "-o", _so, _src])
    _inplug = _so
for typ, name in ((2, PLUG + "mupen64plus-video-glide64mk2.so"),
                  (4, _inplug or PLUG + "mupen64plus-input-sdl.so"),
                  (1, PLUG + "mupen64plus-rsp-hle.so")):
    lib = C.CDLL(name)
    lib.PluginStartup.argtypes = [C.c_void_p, C.c_void_p, DEBUGCB]
    assert lib.PluginStartup(C.c_void_p(core._handle), None, dbgmsg_c) == 0
    assert core.CoreAttachPlugin(typ, C.c_void_p(lib._handle)) == 0
    plugs.append(lib)
    if typ == 4 and _inplug:
        _keys = C.c_uint32.in_dll(lib, "m64input_keys")


def _load_input(path):
    rows, idx = [], 0
    for line in open(path):
        q = line.split("#")[0].strip()
        if not q:
            continue
        parts = q.replace(",", " ").split()
        if parts[0].startswith("@"):
            idx = int(parts[0][1:], 0)
            parts = parts[1:]
        if not parts:
            continue
        v = [int(p, 0) for p in parts] + [0, 0]
        rows.append((idx, v[0] & 0xFFFF, v[1], v[2]))
        idx += 1
    return rows


_rows = _load_input(W_INPUT) if W_INPUT else []
_reads = [0]


def _pad_at(n):
    cur = (0, 0, 0)
    for idx, b, x, y in _rows:
        if idx > n:
            break
        cur = (b, x, y)
    b, x, y = cur
    return ((b >> 8) | ((b & 0xFF) << 8)) | ((x & 0xFF) << 16) | ((y & 0xFF) << 24)
core.DebugMemRead32.restype = C.c_uint32
core.DebugMemRead32.argtypes = [C.c_uint32]
core.DebugMemRead8.restype = C.c_uint8
core.DebugMemRead8.argtypes = [C.c_uint32]
core.DebugSetRunState.argtypes = [C.c_int]
core.DebugGetCPUDataPtr.restype = C.c_void_p
core.DebugGetCPUDataPtr.argtypes = [C.c_int]


class BKP(C.Structure):
    _fields_ = [("address", C.c_uint), ("endaddr", C.c_uint), ("flags", C.c_uint)]


core.DebugBreakpointCommand.argtypes = [C.c_int, C.c_uint, C.POINTER(BKP)]
core.DebugBreakpointTriggeredBy.argtypes = [C.POINTER(C.c_uint32), C.POINTER(C.c_uint32)]
ADD, REPLACE, REMOVE_ADDR, REMOVE_IDX, ENABLE, DISABLE = 2, 3, 4, 5, 6, 7

WIDTH = {0x20: 1, 0x24: 1, 0x28: 1, 0x21: 2, 0x25: 2, 0x29: 2, 0x23: 4, 0x27: 4, 0x2B: 4, 0x31: 4, 0x39: 4,
         0x35: 8, 0x37: 8, 0x3D: 8, 0x3F: 8, 0x22: "U", 0x26: "U", 0x2A: "U", 0x2E: "U", 0x1A: "U8",
         0x1B: "U8", 0x2C: "U8", 0x2D: "U8"}
F_EXEC, F_RW = 1 | 8, 1 | 2 | 4

HD = (0x802E8BD0, 0x8030F660)
FE = (0x80208040, 0x80210E90)
FE_FLAG = 0x80370C50
STAGE = 0x8021ED00
# MODE=base: the original ROM (hand asm widths); MODE=nm: the NON_MATCHING
# test ROM (build_nm/blastcorps.nm.us.v11.z64): the C rewrites' own widths,
# which is what the native port reads with.  Data addresses are the same.
# Code addresses come from the ELFs and maps of the traced build (run from
# the project root): build/ for the original, build_nm/*.rom.* for the NM ROM.
import bisect
import re
# renamed symbols (tools_port/names.txt) are classified by the address names they had
sys.path.insert(0, os.path.join(os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))),
                                "tools_port"))
from names import orig  # noqa: E402
from elftools.elf.elffile import ELFFile
from elftools.elf.sections import SymbolTableSection
BASE = os.environ.get("MODE", "nm") == "base"
_ELFS = (["build/hd_code.us.v11.elf", "build/hd_front_end.us.v11.elf"] if BASE
         else ["build_nm/hd_code.rom.us.v11.elf", "build_nm/hd_front_end.rom.us.v11.elf"])
CODE, _text, _allsyms = [], {}, {}
for _p in _ELFS:
    with open(_p, "rb") as _f:
        _e = ELFFile(_f)
        for _s in _e.iter_sections():
            if _s["sh_flags"] & 4 and _s["sh_size"]:          # executable: code ranges
                CODE.append((_s["sh_addr"], _s["sh_addr"] + _s["sh_size"]))
                _text[_s["sh_addr"]] = _s.data()
        for _s in _e.iter_sections():
            if isinstance(_s, SymbolTableSection):
                for _y in _s.iter_symbols():
                    if _y.name and _y["st_shndx"] != "SHN_UNDEF":
                        _allsyms.setdefault(_y.name, _y["st_value"])
# the hd_code ELF also maps its pinned .text tables low (NM_PIN_HD_CODE); only real code counts
CODE = [r for r in CODE if r[1] - r[0] > 0x1000]


def sym(n):
    return _allsyms[n] & 0xFFFFFFFF


def first_jr_ra(a):
    """address of the first `jr ra` at or after a"""
    for base, data in _text.items():
        if base <= a < base + len(data):
            for k in range(a - base, len(data) - 3, 4):
                if data[k:k + 4] == b"\x03\xe0\x00\x08":
                    return base + k
    raise SystemExit("no jr ra after %08X" % a)


def text_range(mapfile, obj):
    pat = re.compile(r"^ \.text\s+0x([0-9a-f]+)\s+0x([0-9a-f]+)\s+\S*/%s\.c\.o$" % re.escape(obj))
    for line in open(mapfile):
        m = pat.match(line)
        if m:
            return int(m.group(1), 16), int(m.group(1), 16) + int(m.group(2), 16)
    raise SystemExit("%s not in %s" % (obj, mapfile))


_MAP = "build/hd_code.us.v11.map" if BASE else "build_nm/hd_code.rom.us.v11.map"
LOADER = sym("hd_rom_load")
LOADER_RET = first_jr_ra(LOADER)
PIDMA = sym("osPiStartDma")
AUDIO = text_range(_MAP, "22EE0")        # audio.c: sample DMA (.tbl pieces, read by the RSP only)
PK_ENTRY = sym("kiunzip_entry")          # inflate one member
PK_RET = first_jr_ra(PK_ENTRY)
PK_PTR = not BASE                        # hand asm: a1 = dst value; C rewrite: a1 = &dst
# only game code counts: the decompressors' window reads and the copy loops
# are byte-order neutral (and init's boot copy of the image isn't game code)
TRANSPARENT = [text_range(_MAP, "17A70"), text_range(_MAP, "53220"), text_range(_MAP, "7F8B0")]
for _n, _sz in (("memcpy", 0xA0), ("bcopy", 0x310), ("bzero", 0xA0)):
    TRANSPARENT.append((sym(_n), sym(_n) + _sz))


READ = sym("osContStartReadData")
EXECS = (LOADER, LOADER_RET, PIDMA, PK_ENTRY, PK_RET) + ((READ,) if W_INPUT or W_POKES else ())
core.DebugMemWrite8.argtypes = [C.c_uint32, C.c_uint8]
core.DebugMemWrite16.argtypes = [C.c_uint32, C.c_uint16]
core.DebugMemWrite32.argtypes = [C.c_uint32, C.c_uint32]

# pc -> function name
_fsyms = {}
for _p in _ELFS:
    with open(_p, "rb") as _f:
        for _s in ELFFile(_f).iter_sections():
            if isinstance(_s, SymbolTableSection):
                for _y in _s.iter_symbols():
                    if (_y["st_info"]["type"] in ("STT_FUNC", "STT_NOTYPE") and _y.name
                            and not orig(_y.name).startswith((".", "$", "L8", "jtbl", "D_", "_"))
                            and any(a <= _y["st_value"] < b for a, b in CODE)):
                        if _y["st_info"]["type"] == "STT_FUNC" or _y["st_value"] not in _fsyms:
                            _fsyms[_y["st_value"]] = _y.name
_faddr = sorted(_fsyms)
_fcache = {}


def funcof(pc):
    """the function's name; '~name' for the byte-order neutral copy and
    decompression loops (consumers of the trace drop those)"""
    r = _fcache.get(pc)
    if r is None:
        i = bisect.bisect_right(_faddr, pc) - 1
        r = _fsyms[_faddr[i]] if i >= 0 else "?"
        if any(a <= pc < b for a, b in TRANSPARENT):
            r = "~" + r
        _fcache[pc] = r
    return r


def counted(pc):
    return any(a <= pc < b for a, b in CODE) and not any(a <= pc < b for a, b in TRANSPARENT)

loadlines = []


class _Out:
    def write(self, s):
        loadlines.append(s)

    def flush(self):
        pass


out = _Out()
st = {"vi": 0, "started": False}
regs_p = None
facts = collections.Counter()         # (region, off, width, kind) -> count
factpc = {}
first = {}                           # (region, off) -> 'R' | 'W'
bps = {}                             # bp index -> dict(region, base, lo, hi, quiet, enabled)
pending = []
nhits = [0]
nmis = [0]
loads = []


def gpr(i):
    return C.cast(regs_p, C.POINTER(C.c_int64))[i] & 0xFFFFFFFF


def add_bp(region, lo, hi, base):
    b = BKP(lo & 0x1FFFFFFF, (hi - 1) & 0x1FFFFFFF, F_RW)
    idx = core.DebugBreakpointCommand(ADD, 0, C.byref(b))
    if idx < 0:
        return None
    bps[idx] = {"region": region, "lo": lo, "hi": hi, "base": base, "quiet": 0, "enabled": True}
    return idx


# Breakpoints are never removed (the core compacts its array on removal, which
# renumbers every later breakpoint): asset breakpoints come from a fixed pool
# of slots that are retargeted with REPLACE.
POOLS = {"load": [], "dma": []}   # slot indices, oldest use first: decompressed
                                  # loads (levels, models...) never lose their slots
                                  # to the stream of small texture DMAs


def free_slot(idx):
    b = bps[idx]
    if b["enabled"]:
        core.DebugBreakpointCommand(DISABLE, idx, None)
    b.update(region="-", lo=0, hi=0, base=0, enabled=False, quiet=0)


def rearm(pred=lambda b: True):
    for idx, b in bps.items():
        if not b["enabled"] and b["region"] != "-" and pred(b):
            core.DebugBreakpointCommand(ENABLE, idx, None)
            b["enabled"] = True
            b["quiet"] = 0


def asset_bp(region, lo, hi, pool="load"):
    # free asset slots this range overlaps (the heap was reused)
    for idx in POOLS["load"] + POOLS["dma"]:
        b = bps[idx]
        if b["region"] != "-" and b["lo"] < hi and lo < b["hi"]:
            free_slot(idx)
    P = POOLS[pool]
    for a in range(lo, hi, 0x10000):   # split big assets: per-range quiet counting
        frees = [i for i in P if bps[i]["region"] == "-"]
        idx = frees[0] if frees else P[0]
        P.remove(idx)
        P.append(idx)
        e = min(hi, a + 0x10000)
        bk = BKP(a & 0x1FFFFFFF, (e - 1) & 0x1FFFFFFF, F_RW)
        core.DebugBreakpointCommand(REPLACE, idx, C.byref(bk))
        bps[idx].update(region=region, lo=a, hi=e, base=lo, quiet=0, enabled=True)


QUIET = int(os.environ.get("QUIET", "4000"))
CAP = int(os.environ.get("CAP", "150"))     # asset breakpoints per DMA call site (streamed textures)
per_ra = collections.Counter()
pk = {}
pk_stack = []


def on_mem(pc):
    fl = C.c_uint32()
    ad = C.c_uint32()
    core.DebugBreakpointTriggeredBy(C.byref(fl), C.byref(ad))
    a = ad.value | 0x80000000
    nhits[0] += 1
    if not any(lo <= pc < hi for lo, hi in CODE):
        return
    insn = core.DebugMemRead32(pc)
    w = WIDTH.get(insn >> 26)
    if w is None:
        insn = core.DebugMemRead32(pc + 4)
        w = WIDTH.get(insn >> 26, "?")
    kind = "W" if fl.value & 4 else "R"
    # the core reports the aligned word; the byte address comes from the
    # instruction (base register + offset; the callback runs before the load
    # writes its target register)
    if w != "?":
        ea = (gpr((insn >> 21) & 31) + ((insn & 0xFFFF) ^ 0x8000) - 0x8000) & 0xFFFFFFFF
        if (ea & 0x1FFFFFFC) == (ad.value & 0x1FFFFFFC):
            a = ea | 0x80000000
        else:
            nmis[0] += 1
    hit = None
    for idx, b in bps.items():
        if b["lo"] <= a < b["hi"]:
            hit = idx
            break
    if hit is None:
        return
    b = bps[hit]
    region = b["region"]
    if region == "fe" and core.DebugMemRead8(FE_FLAG) == 0:
        return
    off = a - b["base"]
    key = (region, off, w, kind, funcof(pc))
    new = key not in facts
    facts[key] += 1
    if new:
        factpc[key] = pc
        b["quiet"] = 0
    else:
        b["quiet"] += 1
        if b["quiet"] > QUIET and b["enabled"]:
            core.DebugBreakpointCommand(DISABLE, hit, None)
            b["enabled"] = False
    n = w if isinstance(w, int) else 4
    for k in range(n):
        fk = (region, off + k)
        if fk not in first:
            first[fk] = kind


def on_exec(pc):
    global regs_p
    if pc == READ:
        n = _reads[0]
        _reads[0] += 1
        if _inplug:
            _keys.value = _pad_at(n)
        for r, a, s, v in W_POKES:
            if r == n:
                {1: core.DebugMemWrite8, 2: core.DebugMemWrite16, 4: core.DebugMemWrite32}[s](a, v)
                out.write("P %d read %d %08X=%X\n" % (st["vi"], n, a, v))
        return
    if pc == PK_ENTRY:       # hand asm: a1 = dst (value, returned advanced); C: a1 = &dst
        if PK_PTR:
            ptr = gpr(5) | 0x80000000
            pk_stack.append((ptr, core.DebugMemRead32(ptr)))
        else:
            pk_stack.append((None, gpr(5)))
    elif pc == PK_RET and pk_stack:
        ptr, start = pk_stack.pop()
        end = core.DebugMemRead32(ptr) if ptr else gpr(5)
        if pk.get("rom") is not None:
            if pk["first"] is None:
                pk["first"] = start
            out.write("L %d %06X %08X %X pk %08X\n" % (st["vi"], pk["rom"], pk["first"], end - pk["first"], pk["ra"]))
            asset_bp("r%06X" % pk["rom"], pk["first"], end)
    elif pc == LOADER:
        pending.append((gpr(4), gpr(5), gpr(6), gpr(31)))
    elif pc == LOADER_RET and pending:
        dev, dest, sizep, ra = pending.pop()
        size = core.DebugMemRead32(sizep)
        loads.append((st["vi"], dev, dest, size, "gz", ra))
        out.write("L %d %06X %08X %X gz %08X\n" % (st["vi"], dev, dest, size, ra))
        if dest == 0x801E7000:
            rearm(lambda b: b["region"] == "fe")
        else:
            asset_bp("r%06X" % dev, dest, dest + size)
        rearm(lambda b: b["region"] == "hd")
    elif pc == PIDMA:
        ra = gpr(31)
        if LOADER <= ra < LOADER_RET or AUDIO[0] <= ra < AUDIO[1]:
            return
        sp = gpr(29) | 0x80000000
        dev = gpr(7)
        dram = core.DebugMemRead32(sp + 0x10) | 0x80000000
        size = core.DebugMemRead32(sp + 0x14)
        out.write("L %d %06X %08X %X dma %08X\n" % (st["vi"], dev, dram, size, ra))
        if dram == STAGE:          # packed block: registered once it is inflated (PK_RET)
            pk.update(rom=dev, first=None, ra=ra)
            return
        per_ra[ra] += 1
        if 0x80000000 <= dram < 0x80800000 and size and per_ra[ra] <= CAP:
            asset_bp("r%06X" % dev, dram, dram + size, "dma" if size <= 0x4000 else "load")


def upd(pc):
    global regs_p
    if not st["started"]:
        st["started"] = True
        regs_p = core.DebugGetCPUDataPtr(2)
        for a in EXECS:
            b = BKP(a, a, F_EXEC)
            core.DebugBreakpointCommand(ADD, 0, C.byref(b))
        for a in range(HD[0], HD[1], 0x1000):
            add_bp("hd", a, min(HD[1], a + 0x1000), HD[0])
        for a in range(FE[0], FE[1], 0x1000):
            add_bp("fe", a, min(FE[1], a + 0x1000), FE[0])
        # EXTRA="name:lo:hi,...": more fixed regions (e.g. .bss state blocks)
        for spec in filter(None, os.environ.get("EXTRA", "").split(",")):
            nm_, lo_, hi_ = spec.split(":")
            add_bp(nm_, int(lo_, 16), int(hi_, 16), int(lo_, 16))
        for k in range(60):
            idx = add_bp("-", 0x807FF000, 0x807FF004, 0)
            free_slot(idx)
            POOLS["load" if k < 30 else "dma"].append(idx)
    else:
        fl = C.c_uint32()
        ad = C.c_uint32()
        if pc in EXECS:
            core.DebugBreakpointTriggeredBy(C.byref(fl), C.byref(ad))
            if fl.value & 8:
                on_exec(pc)
            else:
                on_mem(pc)
        else:
            on_mem(pc)
    core.DebugSetRunState(2)
    core.DebugStep()


def vi():
    st["vi"] += 1
    v = st["vi"]
    if v % 1800 == 0:
        rearm()
    if v % 600 == 0:
        sys.stderr.write("vi %d hits %d facts %d bps %d (enabled %d) secs %.0f\n" % (
            v, nhits[0], len(facts), len(bps), sum(b["enabled"] for b in bps.values()), time.time() - t0))
        sys.stderr.flush()
    if v % 3000 == 0:
        snapshot("partial")
    if v >= VIS:
        core.CoreDoCommand(6, 0, None)


def snapshot(tag):
    with open(OUTF + ".tmp", "w") as f:
        f.write("".join(loadlines))
        for (region, off, w, kind, fn), n in sorted(facts.items(),
                                                    key=lambda kv: (kv[0][0], kv[0][1], str(kv[0][2]), kv[0][3], kv[0][4])):
            f.write("A %s %X %s %s %d %s\n" % (region, off, w, kind, n, fn))
        for (region, off), k in sorted(first.items()):
            if k == "R":
                f.write("F %s %X\n" % (region, off))
        f.write("# %s vi=%d hits=%d ea-mismatch=%d secs=%.0f\n" % (tag, st["vi"], nhits[0], nmis[0], time.time() - t0))
    os.replace(OUTF + ".tmp", OUTF)


init_c, upd_c, vi_c = INITCB(lambda: None), UPDCB(upd), VICB(vi)
core.DebugSetCallbacks.argtypes = [INITCB, UPDCB, VICB]
core.DebugSetCallbacks(init_c, upd_c, vi_c)
t0 = time.time()
core.CoreDoCommand(5, 0, None)
snapshot("done")
core.CoreDoCommand(2, 0, None)
print("done vi", st["vi"], "hits", nhits[0], "facts", len(facts))
