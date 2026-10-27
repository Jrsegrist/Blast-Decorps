"""m64trace.py spec for the shift test (docs/shiftable.md); shifttest.py runs it.

No input: the title screen, then the attract demos (each one a level played
from a recording).  Two things make a run reproducible across ROMs whose code
and files sit at different addresses (the CPU count depends on how long the
boot inflates hd_code and how long DMAs take, which differ when the
compressed files differ; it even varies a little between two runs of the same
ROM):

  * the game's own uses of the CPU count get synthetic values: osGetCount
    called from util_seed_rand / people_init (the random seeds) and
    snd_random_id, and osGetTime called from hd_game_frame (gFrameDrawTime:
    the screen-shake and part-jitter seed) return N * step for their N-th
    call.  libultra's own uses (the VI manager, timers) see the real count.
  * RDRAM is dumped at game frames counted from a level's start (entries of
    cont_update, the once-a-frame pad read, after the N-th hd_setup_level),
    not at VIs: how many VIs the boot and the loads take differs.

Environment (hex addresses in the ROM being run):
  SHIFT_COUNT_RET  osGetCount's `jr ra`     SHIFT_COUNT_CALLERS lo-hi,lo-hi,...
  SHIFT_TIME_RET   osGetTime's `jr ra`      SHIFT_TIME_CALLERS  lo-hi,...
  SHIFT_FRAME_BP   cont_update              SHIFT_LEVEL_BP      hd_setup_level
  SHIFT_DUMP_AT    LEVEL:FRAME,...  (e.g. 1:300,2:300: frame 300 of the first and second level)
  SHIFT_DUMP_PREFIX out/run                 [SHIFT_VIS] [SHIFT_STEP]
  SHIFT_FILL       lo-hi,...  zeroed at every level start (hd_setup_level's entry)
writes out/run.L1F300.bin, ... (8 MB RDRAM images, big-endian) and stops after
the last one.

SHIFT_FILL: the levels put their vehicles' save-buffer pairs (gJbombMtxA/B, ...)
on the heap, where the front end's code was, and copy them before filling
them (veh_sync_buffers): the game then reads the dead front-end code as data.
A shifted ROM's front-end code differs (it calls hd_code at other addresses),
so the run would differ for that reason alone; shifttest.py zeroes the front
end's code (0x801E7000 to the end of its .text) at every level start, in
every run.
"""
import array
import ctypes as C
import os
import sys


def _ranges(name):
    out = []
    for r in filter(None, os.environ.get(name, "").split(",")):
        lo, hi = r.split("-")
        out.append((int(lo, 16), int(hi, 16)))
    return out


COUNT_RET = int(os.environ["SHIFT_COUNT_RET"], 16)
TIME_RET = int(os.environ["SHIFT_TIME_RET"], 16)
COUNT_CALLERS = _ranges("SHIFT_COUNT_CALLERS")
TIME_CALLERS = _ranges("SHIFT_TIME_CALLERS")
FRAME_BP = int(os.environ["SHIFT_FRAME_BP"], 16)
LEVEL_BP = int(os.environ["SHIFT_LEVEL_BP"], 16)
AT = sorted(tuple(int(y) for y in x.split(":")) for x in os.environ.get("SHIFT_DUMP_AT", "1:100").split(","))
PREFIX = os.environ.get("SHIFT_DUMP_PREFIX", "shiftrun")
FILL = _ranges("SHIFT_FILL")
STEP = int(os.environ.get("SHIFT_STEP", "40000"))
VIS = int(os.environ.get("SHIFT_VIS", "40000"))
BPS = {COUNT_RET: ("count", [], []), TIME_RET: ("time", [], []), FRAME_BP: ("frame", [], []),
       LEVEL_BP: ("level", [], [])}
MEM = []
GPRS = []
FPRS = []
DEDUPE = True
DEFMAX = 1 << 30
# debugging: SHIFT_WATCH=lo-hi logs the code that writes there ("W vi=.. pc=.." lines in the .txt)
WRITES = _ranges("SHIFT_WATCH")
if WRITES:
    _seen_w = {}

    def ONWRITE(pc, addr, g, read):
        k = (pc, addr & ~0xFFF)
        if k in _seen_w:
            return None
        _seen_w[k] = 1
        return "addr=%08x level=%d frame=%d" % (addr, _state["level"], _state["frame"])

_core = C.CDLL("/usr/lib/x86_64-linux-gnu/libmupen64plus.so.2")
_core.DebugMemGetPointer.restype = C.c_void_p
_core.DebugMemGetPointer.argtypes = [C.c_int]
_core.DebugGetCPUDataPtr.restype = C.c_void_p
_core.DebugGetCPUDataPtr.argtypes = [C.c_int]
_core.CoreDoCommand.argtypes = [C.c_int, C.c_int, C.c_void_p]
_state = {"count": 0, "time": 0, "frame": 0, "level": 0, "total": 0}


def _dump(path):
    raw = C.string_at(_core.DebugMemGetPointer(1), 0x800000)   # M64P_DBG_PTR_RDRAM, host-endian words
    a = array.array("I", raw)
    if sys.byteorder == "little":
        a.byteswap()
    with open(path, "wb") as f:
        f.write(a.tobytes())


def _s64(v):
    v &= 0xFFFFFFFF
    return v - (1 << 32) if v & 0x80000000 else v


def _from(ra, ranges):
    return any(lo <= ra < hi for lo, hi in ranges)


def ONHIT(pc, g, read):
    if not _state.get("unlimited"):
        _state["unlimited"] = True
        off = C.c_int(0)
        _core.CoreDoCommand(17, 5, C.byref(off))   # M64CMD_CORE_STATE_SET M64CORE_SPEED_LIMITER = 0
    if pc == COUNT_RET or pc == TIME_RET:
        if not _from(g["ra"], COUNT_CALLERS if pc == COUNT_RET else TIME_CALLERS):
            return None
        kind = "count" if pc == COUNT_RET else "time"
        _state[kind] += 1
        regs = C.cast(_core.DebugGetCPUDataPtr(2), C.POINTER(C.c_int64))   # M64P_CPU_REG_REG
        v = _state[kind] * STEP
        if kind == "count":
            regs[2] = _s64(v)                       # v0
        else:
            regs[2] = _s64(v >> 32)                 # u64 in v0:v1
            regs[3] = _s64(v)
        return None
    if pc == LEVEL_BP:
        _state["level"] += 1
        _state["frame"] = 0
        for lo, hi in FILL:      # the dead front-end code some levels read as data (see SHIFT_FILL)
            C.memset(C.c_void_p(_core.DebugMemGetPointer(1) + (lo & 0x7FFFFF)), 0, hi - lo)
        sys.stderr.write("shiftrun: level start %d (game frame %d)\n" % (_state["level"], _state["total"]))
        sys.stderr.flush()
        if (_state["level"], 0) in AT:      # frame 0: the entry of hd_setup_level
            _dump("%s.L%dF0.bin" % (PREFIX, _state["level"]))
        return None
    _state["frame"] += 1
    _state["total"] += 1
    key = (_state["level"], _state["frame"])
    if key in AT:
        _dump("%s.L%dF%d.bin" % (PREFIX, key[0], key[1]))
        sys.stderr.write("shiftrun: level %d frame %d dumped (seed calls %d, time calls %d)\n"
                         % (key[0], key[1], _state["count"], _state["time"]))
        sys.stderr.flush()
        if key == AT[-1]:
            _core.CoreDoCommand(6, 0, None)   # M64CMD_STOP
    return None
