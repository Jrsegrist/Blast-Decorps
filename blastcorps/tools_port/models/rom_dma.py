"""Cartridge DMA for whole-chain checks (eqcheck --model).

    --conv 'osPiStartDma: in a0=a0,a1=a1,a2=a2,a3=a3,stack0=stack0,stack1=stack1,stack2=stack2 ; out v0=ret'
    --model osPiStartDma=rom_dma.py:dma --model osRecvMesg=rom_dma.py:noop

`dma` copies nbytes of the cartridge image (baserom.us.v11.z64, big-endian)
from ROM offset devAddr to vAddr, the way the PI manager would before posting
its message, and returns 0; `noop` stands in for the blocking osRecvMesg.
Both builds get the same bytes, so level/model data loaded through
lvl_load_model, lvl_load_object, tex_load_table, LOAD_TABLE_ENTRY ... is real.
"""
import os

_ROM = None


def _rom():
    global _ROM
    if _ROM is None:
        here = os.path.dirname(os.path.abspath(__file__))
        cands = [
            os.path.join(here, "..", "..", "baserom.us.v11.z64"),
            os.path.join(here, "..", "..", "..", "baserom.us.v11.z64"),
            os.path.expanduser("~/blastcorps/blastcorps/baserom.us.v11.z64"),
            os.path.expanduser("~/blastcorps/baserom.us.v11.z64"),
        ]
        for p in cands:
            if os.path.exists(p):
                with open(p, "rb") as f:
                    _ROM = f.read()
                break
        if _ROM is None:
            raise RuntimeError("rom_dma: baserom.us.v11.z64 not found")
    return _ROM


def dma(args, mem):
    mb, pri, direction, dev, vaddr, n, mq = args[:7]
    rom = _rom()
    dev &= 0x0FFFFFFF
    n &= 0xFFFFFFFF
    data = rom[dev:dev + n]
    if len(data) < n:
        data = data + bytes(n - len(data))
    mem.write(vaddr & 0xFFFFFFFF, data)
    return 0


def noop(args, mem):
    return 0
