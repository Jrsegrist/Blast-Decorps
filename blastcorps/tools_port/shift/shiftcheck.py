#!/usr/bin/env python3
"""Static check of a shifted build against an unshifted one (docs/shiftable.md).

    shiftcheck.py --ref-dir W/unshifted --new-dir W/shifted --ref-rom A.z64 --new-rom B.z64

Both directories are shiftbuild.py --out directories (shift/<seg>.elf, the
final, patched ELFs linked with -q, so they keep their relocations); the
ROMs come with their .map.txt and .sites.txt.  Checks:

 1. units: every RAM unit that kept its size holds the same bytes in both
    builds, except words with a relocation (the linker moved them) or a
    patched raw reference (.sites.txt);
 2. symbols: every symbol of the unshifted ELFs is, in the shifted ELF, at the
    shifted address of its old address (units: old -> new);
 3. relocations: every R_MIPS_32 / R_MIPS_26 target and every patched raw
    reference points at the shifted address of what it pointed at before;
 4. stale references: no word in the shifted image (data words, and lui/lo
    pairs in code) that has no relocation and isn't a patched site still holds
    an old address of something that moved;
 5. ROM: every ROM unit holds the same bytes (the texture and object tables:
    the same entries, moved), and the header CRCs are right;
 6. the ROM map for the Windows port: every unit's original start, through the
    map, is where the build put it (and no map only when nothing moved).
Exit status 1 on any failure.
"""
import argparse
import bisect
import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.dirname(HERE))
import romlayout as RL  # noqa: E402
import relocscan  # noqa: E402
from shiftlib import Elf  # noqa: E402
from shiftcmp import load_map  # noqa: E402

SEGS = ["init", "hd_code", "hd_front_end"]
TEXT = {"init": ".init", "hd_code": ".hd_code", "hd_front_end": ".hd_front_end"}


class Map:
    """old address -> new address over the units of both maps (all segments)."""

    def __init__(self, ram_ref, ram_new):
        self.items = []
        for lab, r in ram_ref.items():
            n = ram_new.get(lab)
            if n is None or r[4] == 0:
                continue
            self.items.append((r[3], r[3] + r[4], n[3], n[4], lab, r[0]))
        self.items.sort()
        self.starts = [x[0] for x in self.items]
        firsts = {}
        for it in self.items:
            if it[5] not in firsts:
                firsts[it[5]] = it
        self.sec = {}
        for seg, it in firsts.items():
            self.sec[it[0]] = None   # filled by caller (section starts)

    def unit(self, a):
        i = bisect.bisect_right(self.starts, a) - 1
        if i >= 0 and a < self.items[i][1]:
            return self.items[i]
        return None

    def moved(self, a):
        u = self.unit(a)
        return u is not None and u[2] != u[0]

    def map_end(self, a):
        """a as the end of the unit that ends there (None if none does)."""
        i = bisect.bisect_left(self.starts, a) - 1
        if i >= 0 and self.items[i][1] == a:
            return self.items[i][2] + self.items[i][3]
        return None

    def ok(self, a, b):
        """b is a fine new value for the old address a: the unit mapping, or (on a
        boundary) the end of the unit before / the start of its section."""
        return b in (self.map(a), self.map_unit(a), self.map_end(a))

    def map_unit(self, a):
        u = self.unit(a)
        return a if u is None else u[2] + a - u[0]

    def map(self, a):
        if a in self.sec and self.sec[a] is not None:
            return self.sec[a]
        u = self.unit(a)
        if u is None:
            i = bisect.bisect_right(self.starts, a) - 1
            if i >= 0 and a == self.items[i][1]:
                return self.items[i][2] + self.items[i][3]
            return a
        return u[2] + a - u[0]


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--ref-dir", required=True)
    ap.add_argument("--new-dir", required=True)
    ap.add_argument("--ref-rom", required=True)
    ap.add_argument("--new-rom", required=True)
    ap.add_argument("-v", action="store_true")
    args = ap.parse_args()
    ram_ref, rom_ref = load_map(args.ref_rom + ".map.txt")
    ram_new, rom_new = load_map(args.new_rom + ".map.txt")
    M = Map(ram_ref, ram_new)
    elf_ref = {s: Elf(os.path.join(args.ref_dir, "shift", "%s.elf" % s), relocs=True) for s in SEGS}
    elf_new = {s: Elf(os.path.join(args.new_dir, "shift", "%s.elf" % s), relocs=True) for s in SEGS}
    for s in SEGS:
        M.sec[elf_ref[s].sections[TEXT[s]]["addr"]] = elf_new[s].sections[TEXT[s]]["addr"]
    sites = {}
    for line in open(args.new_rom + ".sites.txt"):
        p = line.split()
        if len(p) >= 7:
            # seg OLD -> NEW kind OLDVAL -> NEWVAL unit note
            sites[(p[0], int(p[3], 16))] = (p[4], int(p[5], 16), int(p[7], 16))
    fails = []

    def fail(msg):
        fails.append(msg)
        if args.v or len(fails) <= 30:
            print("FAIL " + msg)

    # 1 + 3: units
    nunits = nwords = nrel = nsite = 0
    for lab, r in ram_ref.items():
        seg = r[0]
        n = ram_new.get(lab)
        if n is None or r[4] != n[4] or "(.bss)" in r[5] or r[5].startswith("bss@"):
            continue
        a = elf_ref[seg].read(r[3], r[4])
        b = elf_new[seg].read(n[3], n[4])
        if a is None or b is None:
            continue
        nunits += 1
        for off in range(0, r[4] & ~3, 4):
            wa = struct.unpack_from(">I", a, off)[0]
            wb = struct.unpack_from(">I", b, off)[0]
            nwords += 1
            na = n[3] + off
            rel = elf_new[seg].relocs.get(na)
            st = sites.get((seg, na))
            if st:
                nsite += 1
                kind, ov, nv = st
                if not M.ok(ov, nv) and kind not in ("imm16", "hir") and not (ov < 0x01000000):
                    fail("%s %08X: raw reference %s %08X now %08X, the map says %08X" % (seg, na, kind, ov, nv, M.map(ov)))
                continue
            if rel:
                nrel += 1
                typ = rel[0][0]
                if typ == 2:       # R_MIPS_32
                    if not M.ok(wa, wb) and not (wa < 0x80000000):
                        fail("%s %08X: R_MIPS_32 %08X -> %08X, the map says %08X (%s)" % (seg, na, wa, wb, M.map(wa), rel[0][1]))
                elif typ == 4:     # R_MIPS_26
                    ta = ((r[3] + off + 4) & 0xF0000000) | ((wa & 0x3FFFFFF) << 2)
                    tb = ((na + 4) & 0xF0000000) | ((wb & 0x3FFFFFF) << 2)
                    if not M.ok(ta, tb):
                        fail("%s %08X: jump to %08X -> %08X, the map says %08X (%s)" % (seg, na, ta, tb, M.map(ta), rel[0][1]))
                continue
            if wa != wb:
                fail("%s %08X (%s+%X): %08X -> %08X without a relocation or a site" % (seg, na, lab, off, wa, wb))
    print("units: %d units, %d words compared, %d relocated, %d patched raw references" % (nunits, nwords, nrel, nsite))

    # 2: symbols
    nsym = 0
    for s in SEGS:
        for name, rec in elf_ref[s].syms.items():
            if name.startswith(("__sh", "shift_pad", "$", ".L")) or rec["type"] in ("STT_SECTION", "STT_FILE"):
                continue
            nv = elf_new[s].value(name)
            if nv is None:
                continue
            v = rec["value"]
            if not (0x80000000 <= v < 0x80800000):
                continue
            nsym += 1
            # a symbol on a unit boundary may name the end of the unit before it (a bin's _end)
            if not M.ok(v, nv):
                fail("%s symbol %s: %08X -> %08X, the map says %08X" % (s, name, v, nv, M.map(v)))
    print("symbols: %d checked" % nsym)

    # 4: stale references in the shifted image
    nstale = 0
    for s in SEGS:
        e = elf_new[s]
        for lab, n in ram_new.items():
            if n[0] != s or n[4] == 0 or "(.bss)" in n[5] or n[5].startswith("bss@"):
                continue
            data = e.read(n[3], n[4])
            if data is None:
                continue
            is_text = n[5].endswith("(.text)") and not n[5].endswith(".bin.o(.data)")

            def stale(v):
                return M.moved(v) and M.map(v) != v

            if is_text and not n[5].endswith(".bin.o(.data)"):
                words = struct.unpack(">%dI" % (n[4] // 4), data[:n[4] & ~3])
                funcs = []
                for lui, kind, value, uses in relocscan.scan_text(words, n[3], e.relocs, funcs, stale):
                    if (s, lui) in sites:
                        continue
                    nstale += 1
                    fail("%s %08X: lui/lo still builds %08X, an old address of something that moved" % (s, lui, value))
            else:
                for a, w in relocscan.scan_words(data, n[3], e.relocs, stale):
                    if (s, a) in sites:
                        continue
                    nstale += 1
                    fail("%s %08X (%s): word %08X is an old address of something that moved (now %08X)"
                         % (s, a, lab, w, M.map(w)))
    print("stale references: %d" % nstale)

    # 5: ROM
    ra = open(args.ref_rom, "rb").read()
    rb = open(args.new_rom, "rb").read()
    newpos = {x[0]: x for x in rom_new if x[0] is not None}
    rommap = sorted((x[1], x[1] + x[2], newpos[x[0]][1]) for x in rom_ref if x[0] is not None and x[0] in newpos)
    rstarts = [x[0] for x in rommap]

    def rmap(v):
        i = bisect.bisect_right(rstarts, v) - 1
        if i >= 0 and v <= rommap[i][1]:
            return rommap[i][2] + v - rommap[i][0]
        return None

    for (seg, na), (kind, ov, nv) in sites.items():
        if ov < 0x01000000 and kind != "hir" and rmap(ov) != nv:
            fail("%s %08X: ROM offset %06X now %06X, the ROM map says %s" % (seg, na, ov, nv, rmap(ov)))
    nrom = 0
    for x in rom_ref:
        if x[0] is None or x[2] == 0:
            continue
        y = newpos.get(x[0])
        if y is None:
            continue
        if x[3] in ("init.us.v11", "hd_code_text", "hd_code_data", "hd_front_end_text", "hd_front_end_data", "header",
                    "trailer"):
            continue
        A = ra[x[1]:x[1] + x[2]]
        B = rb[y[1]:y[1] + y[2]]
        nrom += 1
        if x[0] == RL.TEX_TABLE:
            for i in range(RL.TEX_ENTRIES):
                oa, la, ta = struct.unpack_from(">IHH", A, 8 * i)
                ob, lb, tb = struct.unpack_from(">IHH", B, 8 * i)
                if (la, ta) != (lb, tb) or rmap(x[1] + oa) != y[1] + ob:
                    fail("texture table entry %d: %X -> %X" % (i, x[1] + oa, y[1] + ob))
            continue
        if x[0] == RL.OBJ_TABLE:
            for i in range(RL.OBJ_ENTRIES):
                oa = struct.unpack_from(">I", A, 4 * i)[0]
                ob = struct.unpack_from(">I", B, 4 * i)[0]
                if rmap(x[1] + oa) != y[1] + ob:
                    fail("object table entry %d: %X -> %X" % (i, x[1] + oa, y[1] + ob))
            continue
        if A != B:
            fail("ROM unit %06X %s: bytes differ" % (x[0], x[3]))
    # 6. the port's ROM map (romlayout.py; port/src/rdram.c reads it): every original unit's start, and the end of
    #    its data, through the map are where this build put them; no map only if nothing moved
    nmap = 0
    if rb[-16:-8] == RL.MAP_FOOTER:
        import zlib
        at, crc = struct.unpack_from(">II", rb, len(rb) - 8)
        cnt, flags = struct.unpack_from(">II", rb, at + 12)
        blob = rb[at:at + 32 + 16 * cnt]
        if rb[at:at + 8] != RL.MAP_MAGIC or zlib.crc32(blob) & 0xFFFFFFFF != crc:
            fail("ROM map: bad header or CRC")
        runs = [struct.unpack_from(">IIII", rb, at + 32 + 16 * i) for i in range(cnt)]

        def phys(v):
            for vs, ve, ps, pe in runs:
                if vs <= v < ve:
                    return ps + v - vs
            return v
        for x in rom_ref:
            y = newpos.get(x[0])
            if x[0] is None or y is None or y[2] == 0 or x[3] == "trailer":
                continue
            nmap += 1
            if phys(x[1]) != y[1]:
                fail("ROM map: unit %06X %s maps to %06X, the build put it at %06X" % (x[1], x[3], phys(x[1]), y[1]))
        print("ROM map: %d runs, %d units checked, flags %X" % (cnt, nmap, flags))
    elif any(y[1] != y[0] for y in rom_new if y[0] is not None and y[2]):
        fail("ROM map: files moved, but the ROM has no map")
    from nm_rom import cic6102_crc
    if struct.unpack(">II", rb[0x10:0x18]) != cic6102_crc(rb):
        fail("header CRCs")
    print("ROM: %d units compared, CRC %s" % (nrom, "ok" if struct.unpack(">II", rb[0x10:0x18]) == cic6102_crc(rb) else "BAD"))
    if fails:
        print("shiftcheck: %d failures" % len(fails))
        sys.exit(1)
    print("shiftcheck: OK")


if __name__ == "__main__":
    main()
