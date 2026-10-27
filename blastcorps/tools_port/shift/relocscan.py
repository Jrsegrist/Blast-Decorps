"""Raw address references: everything in the reference image that holds an
address but has no relocation, so the linker can't move it.

  * w32  a data word (binary blobs, C data with raw casts) holding a RAM address
  * hi / lo, hio / loo
         a hand-asm lui and its low halves (lo: addiu / load / store offset,
         signed; loo: ori, unsigned) that build an address
  * hir  a relocated lui (%hi(sym)) whose register raw low halves also use
         for other objects: the lo sites are patched, the lui is left to the
         linker and checked
  * imm16
         a 16-bit immediate that is a ROM offset (the texture table's 0x4CE0)

Targets are RAM addresses inside a unit of any segment, or ROM offsets that
start a ROM unit (romlayout).  scan() returns Site records; the shifted build
patches each one through the address map (apply.py).  A target equal to a
segment's VRAM (section start) means the section; one equal to the end of a
unit is taken as the start of the next one unless overrides.txt says "end".
"""
import struct
from collections import namedtuple

Site = namedtuple("Site", "seg addr kind value space unit note")
# space: "ram" | "rom"

LOADSTORE = {0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27, 0x28, 0x29, 0x2A, 0x2B, 0x2E,
             0x31, 0x35, 0x37, 0x39, 0x3D, 0x3F, 0x1A, 0x1B, 0x2C, 0x2D}


def s16(x):
    return x - 0x10000 if x & 0x8000 else x


def dest_gpr(w):
    op = w >> 26
    rt = (w >> 16) & 31
    if op == 0:
        funct = w & 63
        if funct in (0x08, 0x0C, 0x0D, 0x0F, 0x11, 0x13, 0x18, 0x19, 0x1A, 0x1B, 0x1C, 0x1D, 0x1E, 0x1F,
                     0x30, 0x31, 0x32, 0x33, 0x34, 0x36):
            return None
        return (w >> 11) & 31
    if op == 1:
        return 31 if rt in (0x10, 0x11, 0x12, 0x13) else None
    if op == 3:
        return 31
    if op in (2, 4, 5, 6, 7, 0x14, 0x15, 0x16, 0x17, 0x2F):
        return None
    if op in (0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F, 0x18, 0x19,
              0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27, 0x1A, 0x1B, 0x37, 0x30, 0x34):
        return rt
    if op == 0x11 or op == 0x10:
        rs = (w >> 21) & 31
        return rt if rs in (0, 1, 2) else None
    return None


def srcs(w):
    op = w >> 26
    rs, rt = (w >> 21) & 31, (w >> 16) & 31
    if op == 0:
        return {rs, rt}
    if op in (0x28, 0x29, 0x2A, 0x2B, 0x2E, 0x3F, 0x2C, 0x2D, 4, 5, 0x14, 0x15):
        return {rs, rt}
    return {rs}


def scan_text(words, base, relocd, funcs, is_target):
    """lui/lo pairs without relocations in an instruction stream.
    -> [(lui_addr, kind_lui, value, [(use_addr, kind, value)])]"""
    out = []
    state = {}          # reg -> [lui_addr, hi, uses, ori?]
    fstarts = set(funcs)
    for k, w in enumerate(words):
        a = base + 4 * k
        if a in fstarts:
            state = {}
        op = w >> 26
        rs, rt = (w >> 21) & 31, (w >> 16) & 31
        handled = False
        if op == 0x0F:
            # a raw lui, or a relocated one: hand asm reuses a %hi(sym) for
            # other objects in the same 64 KB page with raw offsets
            # (veh_copy_slot0: `sb $v0, -0x2C10($at)` after %hi(gSlotAirborne1))
            rec = [a, (w & 0xFFFF) << 16, [], None, a in relocd]
            state[rt] = rec
            out.append(rec)
            continue
        if op in (0x08, 0x09, 0x18, 0x19, 0x0D) and rs in state and a not in relocd:
            rec = state[rs]
            lo = (w & 0xFFFF) if op == 0x0D else s16(w & 0xFFFF)
            rec[2].append((a, "loo" if op == 0x0D else "lo", (rec[1] + lo) & 0xFFFFFFFF))
            if op == 0x0D:
                rec[3] = True
            state.pop(rt, None)
            handled = True
        elif op in LOADSTORE and rs in state and a not in relocd:
            rec = state[rs]
            rec[2].append((a, "lo", (rec[1] + s16(w & 0xFFFF)) & 0xFFFFFFFF))
            d = dest_gpr(w)
            if d is not None and d != rs:
                state.pop(d, None)
            elif d == rs:
                state.pop(rs, None)
            handled = True
        elif op == 0 and (w & 63) in (0x21, 0x20, 0x25, 0x2D, 0x23):   # addu/add/or/daddu/subu: derived base
            rd = (w >> 11) & 31
            src = [r for r in (rs, rt) if r in state and r != 0]
            if len(src) == 1 and rd != 0:
                state[rd] = state[src[0]]
                handled = True
        if not handled:
            d = dest_gpr(w)
            if d is not None:
                state.pop(d, None)
    res = []
    for lui, hi, uses, ori, rel in out:
        good = [u for u in uses if is_target(u[2])]
        if rel:
            # hir: the linker sets the lui; the raw low halves are patched
            # and must agree with it (apply_sites checks)
            if good:
                res.append((lui, "hir", hi, good))
        elif good:
            res.append((lui, "hio" if ori else "hi", good[0][2], good))
        elif not uses and is_target(hi):
            res.append((lui, "hi", hi, []))       # lone lui: needs an override
    return res


def scan_words(data, base, relocd, is_target):
    out = []
    for i in range(0, len(data) & ~3, 4):
        a = base + i
        if a & 3 or a in relocd:
            continue
        w = struct.unpack_from(">I", data, i)[0]
        if is_target(w):
            out.append((a, w))
    return out
