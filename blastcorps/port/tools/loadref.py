#!/usr/bin/env python3
"""Reference outputs for port/src/load/loadcheck_main.c: the same cases run
through the ORIGINAL MIPS code (build/, the matching ELFs) in unicorn on
big-endian memory, with the assets taken straight from the ROM.  Prints the
same lines as loadcheck.exe.

usage: loadref.py ROM [TESTS] [build-dir]     (run from the project root)
"""
import re
import struct
import sys
import zlib

from elftools.elf.elffile import ELFFile
from elftools.elf.sections import SymbolTableSection
from unicorn import Uc, UcError, UC_ARCH_MIPS, UC_MODE_MIPS64, UC_MODE_BIG_ENDIAN
from unicorn import mips_const as M

ROMP = sys.argv[1]
TESTS = sys.argv[2] if len(sys.argv) > 2 else "5678A"
BUILD = sys.argv[3] if len(sys.argv) > 3 else "build"
rom = open(ROMP, "rb").read()
RAM = 0x00800000
SENTINEL = 0x80000010
STACK = 0x807FF000


def sext(v):
    v &= 0xFFFFFFFF
    return v | 0xFFFFFFFF00000000 if v & 0x80000000 else v


uc = Uc(UC_ARCH_MIPS, UC_MODE_MIPS64 | UC_MODE_BIG_ENDIAN)
# (the VR5432, a MIPS IV core: unicorn's R4000 model raises a reserved-instruction
# exception for the 64-bit FPU conversions the VR4300 has, cvt.s.l and the like,
# which the collision builders of TB use; MIPS III code runs the same on both)
uc.ctl_set_cpu_model(M.UC_CPU_MIPS64_VR5432 if "B" in TESTS else M.UC_CPU_MIPS64_R4000)
uc.mem_map(0, RAM)
# the FPU on with 64-bit registers (Status CU1 | FR), as the N64 runs (0x34000000): the
# collision builders and queries (TB) use it, cvt.s.l among others
#   (and CU3: QEMU's MIPS IV models allow the 64-bit FPU forms only with CU1 and CU3 set)
uc.reg_write(M.UC_MIPS_REG_CP0_STATUS, uc.reg_read(M.UC_MIPS_REG_CP0_STATUS) | 0xA4000000)
syms = {}
for name in ("hd_code", "hd_front_end"):
    with open("%s/%s.us.v11.elf" % (BUILD, name), "rb") as f:
        elf = ELFFile(f)
        for s in elf.iter_sections():
            if s["sh_flags"] & 2 and s["sh_size"] and s["sh_type"] != "SHT_NOBITS":
                uc.mem_write(s["sh_addr"] & 0x1FFFFFFF, s.data())
            if isinstance(s, SymbolTableSection):
                for sy in s.iter_symbols():
                    if sy.name and sy.name not in syms:
                        syms[sy.name] = sy["st_value"]
LAST_BLOCKS = []
if __import__("os").environ.get("LOADREF_DEBUG"):
    from unicorn import UC_HOOK_BLOCK

    from unicorn import UC_HOOK_CODE

    def _blk(uc_, addr, size, data):
        LAST_BLOCKS.append(addr & 0xFFFFFFFF)
        del LAST_BLOCKS[:-8]
    uc.hook_add(UC_HOOK_CODE if __import__("os").environ.get("LOADREF_DEBUG") == "code" else UC_HOOK_BLOCK, _blk)
REG = {n: getattr(M, "UC_MIPS_REG_" + n.upper()) for n in
       ("a0", "a1", "a2", "a3", "v0", "t0", "t9", "s1", "gp", "sp", "ra", "pc")}


def call(func, stack_args=(), **regs):
    for r, v in regs.items():
        uc.reg_write(REG[r], sext(v))
    for i, v in enumerate(stack_args):      # o32: arguments 5.. at sp + 0x10
        uc.mem_write((STACK + 0x10 + 4 * i) & 0x1FFFFFFF, struct.pack(">I", v & 0xFFFFFFFF))
    uc.reg_write(REG["sp"], sext(STACK))
    uc.reg_write(REG["ra"], sext(SENTINEL))
    try:
        uc.emu_start(sext(syms[func]), sext(SENTINEL), count=20000000)
    except UcError:
        sys.stderr.write("loadref: %s stopped at pc %08X (last blocks %s)\n" % (
            func, uc.reg_read(REG["pc"]) & 0xFFFFFFFF, " ".join("%08X" % b for b in LAST_BLOCKS)))
        raise
    return uc.reg_read(REG["v0"]) & 0xFFFFFFFF


def rd(a, n): return bytes(uc.mem_read(a & 0x1FFFFFFF, n))
def r8(a): return rd(a, 1)[0]
def r16(a): return struct.unpack(">H", rd(a, 2))[0]
def r32(a): return struct.unpack(">I", rd(a, 4))[0]
def wr(a, b): uc.mem_write(a & 0x1FFFFFFF, bytes(b))
def w8(a, v): wr(a, [v & 0xFF])
def w16(a, v): wr(a, struct.pack(">H", v & 0xFFFF))
def w32(a, v): wr(a, struct.pack(">I", v & 0xFFFFFFFF))


class Rng:
    def __init__(self, s): self.s = s

    def __call__(self):
        self.s = (self.s * 1103515245 + 12345) & 0xFFFFFFFF
        return self.s >> 8


HEAP, PARAM = 0x80100000, 0x801F0000
out = []

if "5" in TESTS:
    for flag in (0, 1):
        for i in range(19):
            w8(0x802E8BF8, flag)
            out.append("T5 %d.%02d %08X" % (flag, i, r32(0x802E8BF8 + 4 * i)))

if "6" in TESTS:
    for i in range(0x200):
        w16(PARAM + i * 2, i * 0x2B7 + 0x55)
    TAB = 0x4CE0
    for tid in range(0, 0xFFF, 7):
        romo, size, typ = struct.unpack(">IHH", rom[TAB + tid * 8:TAB + tid * 8 + 8])
        nxt = struct.unpack(">I", rom[TAB + tid * 8 + 8:TAB + tid * 8 + 12])[0]
        if nxt <= romo or nxt - romo > 0x8000 or typ > 6:
            continue
        buf = 0x80180000
        wr(buf, rom[TAB + romo:TAB + nxt])
        req = 0x803C4B58
        w32(req, buf); w32(req + 4, size); w32(req + 8, typ)
        w32(req + 12, PARAM if typ in (4, 5) else 0)
        n = call("tex_decode", a0=req)
        out.append("T6 %03X %d %05X %08X" % (tid, typ, n, zlib.crc32(rd(buf, n)) & 0xFFFFFFFF))


def level_starts():
    """[(start, end)] of the 60 levels, from lvl_load_file's switch (00000.c)"""
    src = open("src.us.v11/hd_code/00000.c").read()
    body = src[src.index("void lvl_load_file(s32 level"):]
    body = body[:body.index("\n}\n")]
    sys.path.insert(0, "tools_port")
    from names import addr_of      # the ROM symbols are named (romLagpRaw): their address names
    res = {}
    for m in re.finditer(r"case (\d+):\s*start = (\w+);\s*\*size = (\w+) - \2;", body):
        res[int(m.group(1))] = (addr_of(m.group(2)), addr_of(m.group(3)))
    return res


def level_blob(lvl):
    st, en = LEVELS[lvl]
    data = rom[st:en]
    blob = b""
    for _ in range(2):     # hd_rom_load(..., 12, 10, 1): two gzip members
        d = zlib.decompressobj(31)
        blob += d.decompress(data)
        data = d.unused_data
    return blob


if "7" in TESTS or "8" in TESTS or "A" in TESTS or "B" in TESTS:
    LEVELS = level_starts()

if "7" in TESTS:
    for lvl in range(60):
        blob = level_blob(lvl)
        wr(HEAP, blob)
        try:
            call("lvl_read_alpha_zones", t0=HEAP)
        except UcError:
            out.append("T7 %02d.size CRASH" % lvl)
            continue
        end = r32(0x803BDFD4)
        out.append("T7 %02d.size %05X first %02X n %u" % (lvl, len(blob), r8(0x80364A6E), (end - 0x803BDFD8) // 0x24))
        e = 0x803BDFD8
        while e < end and e < 0x803BDFD8 + 0x24 * 400:
            n = r8(e + 0x13)
            out.append("T7 %02d.%04X %08X %08X %08X %08X %02X %02X %02X %02X %02X %s" % (
                lvl, (e - 0x803BDFD8) // 0x24, r32(e), r32(e + 4), r32(e + 8), r32(e + 0xC), r8(e + 0x10),
                r8(e + 0x11), r8(e + 0x12), r8(e + 0x13), r8(e + 0x14), rd(e + 0x15, min(n, 0xF)).hex().upper()))
            e += 0x24

if "8" in TESTS:
    rnd = Rng(8)
    for lvl in range(60):
        wr(HEAP, level_blob(lvl))
        for i in range(40):
            a = ((rnd() % 0x400) << 5) | (rnd() & 31)
            w32(0x803643E0, a)
            a = ((rnd() % 0x400) << 5) | (rnd() & 31)
            w32(0x803643E8, a)
            w16(0x80364450, rnd() & 0xFFFF)
            w16(0x8036444E, 0x1234)
            w8(0x80364411, 0x55)
            try:
                call("cam_apply_height_zone", a0=HEAP)
            except UcError:
                out.append("T8 %02d.%02d CRASH" % (lvl, i))
                continue
            out.append("T8 %02d.%02d %04X %02X" % (lvl, i, r16(0x8036444E), r8(0x80364411)))

if "A" in TESTS:
    # the panel events (0x58) through dig_coach_update as the carrier's z sweeps
    # the s16 range (loadcheck_main.c ta: the same globals, the same steps)
    for lvl in range(60):
        wr(HEAP, level_blob(lvl))
        lo, hi = HEAP + r32(HEAP + 0x58), HEAP + r32(HEAP + 0x5C)
        n = (hi - lo) // 8
        out.append("TA %02d n %u" % (lvl, n))
        if n == 0 or n > 64:
            continue
        w32(0x803BE6FC, lo)          # gCarrierPanelEvents
        w32(0x803BE700, hi)          # gCarrierPanelEventsEnd
        wr(0x80364A90, bytes(8))     # gGameMode
        w8(0x803643DB, 1)            # gHaveCarrier
        w8(0x803643D6, 0)            # gCarrierHit
        w8(0x8036CB2F, 0)            # gDigHit
        w8(0x8036CB28, 0)            # gDigSampleTail
        w8(0x8036CB29, 0)            # gDigSampleHead
        fired = [None] * n
        try:
            for k in range(-0x8000, 0x8000, 0x10):
                w32(0x803EF6E4, k << 5)  # gCarrierZ
                call("dig_coach_update", a0=0)
                for i in range(n):
                    if fired[i] is None and r8(lo + 8 * i + 6):
                        fired[i] = k
        except UcError:
            out.append("TA %02d CRASH" % lvl)
            continue
        for i in range(n):
            args = " ".join(str(r8(lo + 8 * i + j)) for j in range(2, 6))
            if fired[i] is None:
                out.append("TA %02d.%02u never (%s)" % (lvl, i, args))
            else:
                out.append("TA %02d.%02u fires at z %d (%s)" % (lvl, i, fired[i], args))

if "B" in TESTS:
    # the ground and wall queries over the ROM's level and the rebuilt one
    # (loadcheck_main.c tb: the same set-up, points and outputs; the rebuilt
    # files from BC_LC_LEVELS_DIR)
    import os
    ldir = os.environ.get("BC_LC_LEVELS_DIR")
    rebuilt = {}
    if ldir:
        for lvl in range(60):
            p = os.path.join(ldir, "level_%02d.bin" % lvl)
            if os.path.exists(p):
                rebuilt[lvl] = open(p, "rb").read()
    out.append("TB rebuilt %d" % len(rebuilt))
    HEAPPTR, TB_HEAP = 0x80358070, 0x80180000
    A = dict(objects=0x803F4030, objectsEnd=0x803F7654, dyn=0x803F9330, dynEnd=0x803FB8B0, sw=0x803BD308,
             swEnd=0x803BD30C, coll=0x803B9890, collEnd=0x803BD300, levelno=0x802E8BDC, groundHit=0x803EBC08,
             groundTriId=0x803F932C, arcStart=0x803A7410, arcEnd=0x803A7412, collHit=0x803A7424,
             arcCollapsed=0x803A742F, fail=0x803BE738, hitSpeed=0x803F77FC)

    def tb_setup(lvl, blob):
        wr(HEAP, blob)
        w32(HEAPPTR, TB_HEAP)
        w32(A["levelno"], lvl)
        call("lvl_read_header", t0=HEAP)
        call("lvl_build_ground_grid", t0=HEAP)
        call("lvl_build_coll_grid_a", t0=HEAP, v0=0, t9=0, s1=0)
        call("lvl_build_coll_grid_b", t0=HEAP, v0=0, t9=0, s1=0)
        call("lvl_build_part_tris", t0=HEAP)
        w32(A["objectsEnd"], A["objects"])
        w32(A["dynEnd"], A["dyn"])
        w32(A["sw"], 0)
        w32(A["swEnd"], 0)
        w32(A["collEnd"], A["coll"])
        w16(A["hitSpeed"], 0)

    def s32(v):
        v &= 0xFFFFFFFF
        return v - (1 << 32) if v & 0x80000000 else v

    def tb_ground(x, z, y):
        w32(A["groundHit"], 0)
        w8(A["groundTriId"], 0)
        h = call("coll_ground_height", a0=x, a1=z, a2=y)
        t = r32(A["groundHit"])
        tri = (r8(t + 0x12) << 8 | r8(t + 0x13)) if t else 0xFFFF0000
        return ((h * 2654435761) ^ tri ^ (r8(A["groundTriId"]) << 24)) & 0xFFFFFFFF

    def tb_probe(x, y, z, r):
        w8(A["fail"], 0)
        call("coll_begin_probe", a0=x, a1=y, a2=z)
        call("coll_probe_tris", stack_args=(0xBE, 0), a0=x, a1=y, a2=z, a3=r)
        return (r16(A["arcStart"]) | r16(A["arcEnd"]) << 12 | r8(A["collHit"]) << 24 | r8(A["arcCollapsed"]) << 25 |
                (r8(A["fail"]) != 0) << 26) & 0xFFFFFFFF

    NG = NP = 600
    RADII = (0x100, 0x400, 0xC00, 0x1800)
    for lvl in range(60):
        if lvl not in rebuilt:
            continue
        blobs = (level_blob(lvl), rebuilt[lvl])
        res = [None, None]
        for pas in (0, 1):
            tag = "rebuilt" if pas else "rom"
            try:
                tb_setup(lvl, blobs[pas])
                h = struct.unpack(">12H", rd(HEAP, 24))
                gw, gd = h[4] * h[6], h[5] * h[7]
                cw, cd = min(h[8] * h[10], gw), min(h[9] * h[11], gd)
                rnd = Rng(0xB0 + lvl)
                pts = []
                for i in range(NG):
                    k = rnd() % 4
                    x = (rnd() % gw) * 32
                    x += rnd() % 32
                    z = (rnd() % gd) * 32
                    z += rnd() % 32
                    y = 0 if k == 0 else (rnd() % 0x10000) if k == 1 else (rnd() % 0x40000) if k == 2 else 0x7FFFF
                    pts.append((x, z, y))
                prb = []
                for i in range(NP):
                    x = (rnd() % cw) * 32
                    x += rnd() % 32
                    z = (rnd() % cd) * 32
                    z += rnd() % 32
                    prb.append((x, z, RADII[rnd() % 4]))
                r = [tb_ground(x, z, y) for (x, z, y) in pts]
                for k, (x, z, rad) in enumerate(prb):
                    gh = s32(call("coll_ground_height", a0=x, a1=z, a2=0x40000))
                    if gh >= 0x40000:     # no ground there: at 0 (far up, the original's dadd traps)
                        gh = 0
                    try:
                        r.append(tb_probe(x, gh + rad // 2, z, rad))
                    except UcError:
                        sys.stderr.write("TB %02d.%s probe %d (%d, %d, %d, r %d) trapped\n" % (lvl, tag, k, x, gh + rad // 2, z, rad))
                        r.append(0xFFFFFFFF)
            except UcError as e:
                sys.stderr.write("TB %02d.%s: %s at pc %08X\n" % (lvl, tag, e, uc.reg_read(REG["pc"]) & 0xFFFFFFFF))
                out.append("TB %02d.%s CRASH" % (lvl, tag))
                break
            res[pas] = r
            cg = zlib.crc32(struct.pack("<%dI" % NG, *r[:NG])) & 0xFFFFFFFF
            cp = zlib.crc32(struct.pack("<%dI" % NP, *r[NG:])) & 0xFFFFFFFF
            out.append("TB %02d.%s ground %08X probe %08X" % (lvl, tag, cg, cp))
        if res[0] is not None and res[1] is not None:
            dg = sum(1 for a, b in zip(res[0][:NG], res[1][:NG]) if a != b)
            dp = sum(1 for a, b in zip(res[0][NG:], res[1][NG:]) if a != b)
            out.append("TB %02d.diff ground %d/%d probe %d/%d" % (lvl, dg, NG, dp, NP))

print("\n".join(out))
