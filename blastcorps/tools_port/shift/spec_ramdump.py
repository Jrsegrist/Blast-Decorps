"""m64trace.py spec for the shift test (docs/shiftable.md): no input (the
title screen, then the attract demos), RDRAM dumped at chosen VIs.

    SHIFT_DUMP_VIS=600,1800 SHIFT_DUMP_PREFIX=out/run \
        python3 tools_port/m64trace/m64trace.py ROM tools_port/shift/spec_ramdump.py out/run.txt

writes out/run.vi600.bin, ... (8 MB of RDRAM each, big-endian, as the N64 sees it)
and logs the game mode / level at every change.
"""
import ctypes as C
import os
import sys

VIS_LIST = [int(x) for x in os.environ.get("SHIFT_DUMP_VIS", "600").split(",")]
PREFIX = os.environ.get("SHIFT_DUMP_PREFIX", "ramdump")
VIS = max(VIS_LIST) + 1
BPS = {}
MEM = []
DEDUPE = True

_core = C.CDLL("/usr/lib/x86_64-linux-gnu/libmupen64plus.so.2")
_core.DebugMemGetPointer.restype = C.c_void_p
_core.DebugMemGetPointer.argtypes = [C.c_int]


def _dump(path):
    p = _core.DebugMemGetPointer(1)          # M64P_DBG_PTR_RDRAM
    raw = C.string_at(p, 0x800000)
    # mupen64plus keeps RDRAM as host-endian 32-bit words
    import array
    a = array.array("I", raw)
    if sys.byteorder == "little":
        a.byteswap()
    with open(path, "wb") as f:
        f.write(a.tobytes())


def INPUT(vi, read, ctl):
    if vi in VIS_LIST:
        _dump("%s.vi%d.bin" % (PREFIX, vi))
        ctl.log("dumped RDRAM")
    return 0, 0, 0
