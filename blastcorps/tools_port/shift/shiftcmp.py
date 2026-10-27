#!/usr/bin/env python3
"""Compare the RAM of a shifted ROM with the RAM of the unshifted one at the
same moment, object by object (docs/shiftable.md "shift test").

    shiftcmp.py --ref-rom A.z64 --new-rom B.z64 --ref-elfs DIR_A --new-elfs DIR_B
                [--noise A2.bin,A3.bin] A.bin B.bin

A.z64 / B.z64 are shiftbuild.py outputs (with their .map.txt unit maps),
DIR_A / DIR_B their shift/ directories (the linked ELFs); A.bin / B.bin RDRAM
images taken at the same game frame (spec_shiftrun.py).

hd_code's data and .bss units (always resident) are compared at their own
address in each image, the front end's when it is resident in both
(frontEndPresent); the rest of RAM (exception vectors, frame buffers, heap,
the front end's area, the init area that becomes the load buffer, the top)
at the same address.  A word that differs still counts as equal when:

  * translated: it is a pointer, and B's word is the shifted address of A's
    (a pointer into a moved unit, its physical address, or a ROM offset into
    a moved ROM unit);
  * leftover: each image holds its own build's copy of something loaded
    there earlier and never overwritten (the compressed hd_code init DMAs to
    0x80000400, the front end's code in its area, the exception vector code);
  * timing: it differs between A and a run of the same ROM with other CPU
    timing (--noise: thread contexts, message queues, audio buffers, timer
    values).

Prints each region with real differences (the first few of each) and a
summary; exit status 1 if anything really differs.
"""
import argparse
import array
import bisect
import os
import re
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

FIXED_REGIONS = [(0x80000000, 0x80000400, "exception vectors"), (0x80000400, 0x80025C00, "frame buffer 0"),
                 (0x80025C00, 0x8004B400, "frame buffer 1"), (0x8004B400, 0x801E7000, "heap"),
                 (0x801E7000, 0x8021ED00, "front end area (heap in levels)"),
                 (0x8021ED00, 0x802447C0, "init area (the load buffer)"), (0x802447C0, 0x80400000, "above hd_code")]


def load_map(path):
    ram, rom = {}, []
    for line in open(path):
        p = line.split()
        if not p or p[0] == "map":      # (map: the port's ROM map runs, romlayout.py)
            continue
        if p[0] == "rom":
            # rom TYPE OLD -> NEW +SIZE NAME
            i = p.index("->")
            old = None if p[i - 1] == "new" else int(p[i - 1], 16)
            rom.append((old, int(p[i + 1], 16), int(p[i + 2][1:], 16), " ".join(p[i + 3:])))
        else:
            seg, lab = p[0], p[1]
            old, osz = p[2].split("+")
            new, nsz = p[4].split("+")
            ram[lab] = (seg, int(old, 16), int(osz, 16), int(new, 16), int(nsz, 16), " ".join(p[5:]))
    return ram, rom


class Translator:
    """REF value -> NEW value for pointers into units (RAM) and ROM offsets."""

    def __init__(self, ram_ref, ram_new, rom_ref, rom_new):
        self.items = []
        for lab, r in ram_ref.items():
            n = ram_new.get(lab)
            if n is None or r[4] == 0:
                continue
            self.items.append((r[3], r[3] + r[4], n[3], n[4] == r[4]))
        self.items.sort()
        self.starts = [x[0] for x in self.items]
        self.rom = []
        rn = {x[0]: x for x in rom_new if x[0] is not None}
        for x in rom_ref:
            if x[0] is None:
                continue
            y = rn.get(x[0])
            if y:
                self.rom.append((x[1], x[1] + x[2], y[1]))
        self.rom.sort()
        self.rstarts = [x[0] for x in self.rom]
        # the self-relative ROM tables (texture table, object table): entries are offsets from the table
        self.tables = []
        b = {x[0]: x for x in rom_new}
        for x in rom_ref:
            if x[0] in (0x4CE0, 0x6EC4C0) and x[0] in b:
                self.tables.append((x[1], b[x[0]][1]))

    def ram(self, v):
        i = bisect.bisect_right(self.starts, v) - 1
        if i >= 0 and v <= self.items[i][1]:
            s, e, ns, same = self.items[i]
            return ns + (v - s)
        return None

    def romoff(self, v):
        i = bisect.bisect_right(self.rstarts, v) - 1
        if i >= 0 and v < self.rom[i][1]:
            return self.rom[i][2] + v - self.rom[i][0]
        return None

    def same(self, a, b):
        if a == b:
            return True
        t = self.ram(a)
        if t is not None and (t == b or t == (b & 0x7FFFFFFF) | 0x80000000):
            return True
        if 0x00001000 <= a < 0x00800000:
            t = self.romoff(a)
            if t == b:
                return True
        if a < 0x00800000:              # the physical address of a moved object (DMA, VI)
            t = self.ram(a | 0x80000000)
            if t is not None and (t & 0x1FFFFFFF) == b:
                return True
            for ta, tb in self.tables:  # an entry of a copy of the texture or object table
                t = self.romoff(ta + a)
                if t is not None and t == tb + b:
                    return True
        return False

    def deltas(self):
        if not hasattr(self, "_deltas"):
            d = set((ns - s) & 0xFFFFFFFF for s, e, ns, same in self.items if ns != s)
            d |= set((n - o) & 0xFFFFFFFF for o, e, n in self.rom if n != o)
            self._deltas = d
        return self._deltas

    def derived(self, a, b):
        """b - a is how far some unit moved, and a is no plain pointer: an
        address packed with other bits, or an offset between a moved and a
        fixed object (stack words, audio sequence state, scaled offsets)."""
        if 0x80000000 <= a < 0x80800000:
            return False
        return ((b - a) & 0xFFFFFFFF) in self.deltas()


class Leftovers:
    """RAM ranges that may still hold an image loaded earlier: (lo, ref bytes, new bytes)."""

    def __init__(self):
        self.items = []

    def add(self, lo, a, b):
        self.items.append((lo, a, b))

    def explains(self, addr, wa, wb):
        """Every byte that differs is, in each image, that build's byte of a
        loaded image (a word can be half rewritten since: a u16 field over old
        code)."""
        ba, bb = struct.pack(">I", wa), struct.pack(">I", wb)
        for k in range(4):
            if ba[k] == bb[k]:
                continue
            ok = False
            for lo, a, b in self.items:
                o = addr + k - lo
                if 0 <= o < len(a) and o < len(b) and a[o] == ba[k] and b[o] == bb[k]:
                    ok = True
                    break
            if not ok:
                return False
        return True


KINDS = ("eq", "trans", "derived", "left", "stale", "timing", "async", "diff")

# RAM other threads own: their stacks and OSThread records, message queues, the
# audio heap, the scheduler (gScheduler, which holds its thread), libultra's
# managers (ul_*.c .bss), the audio manager (22EE0) and sched.c (2C560).  What
# they hold at a given game frame depends on where the other threads happen to
# be, not on the game.
ASYNC_SYMBOLS = re.compile(r"^g\w*(Stack|Thread|Queue|MesgBuf|Mesgs?)$|^gAudioHeapBuf$|^gRspTasks$|^gScheduler$")
ASYNC_UNITS = re.compile(r"/ul_\w+\.c\.o\(\.bss\)|/22EE0\.c\.o\(\.bss\)|/2C560\.c\.o\(\.bss\)")


def async_ranges(elf_path):
    """[(lo, hi)] of the ASYNC_SYMBOLS of an ELF (each up to the next symbol)."""
    from shiftlib import Elf
    e = Elf(elf_path)
    pts = sorted(set((r["value"], n) for n, r in e.symlist
                     if 0x80000000 <= r["value"] < 0x80800000 and not n.startswith(("__sh", "$", ".", "_binary"))))
    out = []
    for i, (v, n) in enumerate(pts):
        if ASYNC_SYMBOLS.match(n):
            nxt = next((w for w, _ in pts[i + 1:] if w > v), v + 4)
            out.append((v, nxt))
    return out


def unwritten(starts, oa, ob, a, b):
    """Every byte of the word that differs still holds, in each run, its value
    at the level's start (a level can write part of a word over the front
    end's leftover data: a u16 or three bytes)."""
    ba, bb = struct.pack(">I", a), struct.pack(">I", b)
    for k in range(4):
        if ba[k] != bb[k] and (starts[0][oa + k] != ba[k] or starts[1][ob + k] != bb[k]):
            return False
    return True


class Stats:
    def __init__(self):
        self.rows = []

    def compare(self, ref, new, ra, na, n, tr, label, noise=None, left=None, starts=None, is_async=None, show=6):
        """starts: (REF, NEW) dumps at the level's start: a fixed-address word
        that neither run wrote since then is stale (left over from before).
        is_async(REF address): memory other threads own (ASYNC_*)."""
        c = dict((k, 0) for k in KINDS)
        firsts = []
        ws = struct.Struct(">I")
        for off in range(0, n & ~3, 4):
            oa, ob = (ra & 0x7FFFFF) + off, (na & 0x7FFFFF) + off
            a = ws.unpack_from(ref, oa)[0]
            b = ws.unpack_from(new, ob)[0]
            if a == b:
                c["eq"] += 1
            elif is_async is not None and is_async(ra + off):
                c["async"] += 1
            elif noise is not None and noise[oa >> 2]:
                c["timing"] += 1
            elif tr.same(a, b):
                c["trans"] += 1
            elif left is not None and left.explains(ra + off, a, b):
                c["left"] += 1
            elif starts is not None and unwritten(starts, oa, ob, a, b):
                c["stale"] += 1
            elif tr.derived(a, b):
                c["derived"] += 1
            else:
                c["diff"] += 1
                if len(firsts) < show:
                    firsts.append("%08X/%08X: %08X vs %08X" % (ra + off, na + off, a, b))
        self.rows.append((label, n, c, firsts))


def elf_image(path, names):
    """The bytes of these sections of an ELF, back to back, and the first one's address."""
    from shiftlib import Elf
    e = Elf(path)
    out = b""
    lo = None
    for nm in names:
        s = e.sections.get(nm)
        if s is None or s["data"] is None:
            continue
        if lo is None:
            lo = s["addr"]
        out += s["data"]
    return lo, out, e


def find_rom_copy(rom, buf, n=64):
    """Where in rom the first n bytes of buf come from (None if nowhere, or ambiguous)."""
    probe = bytes(buf[:n])
    if not any(probe):
        return None
    i = rom.find(probe)
    if i < 0 or rom.find(probe, i + 1) >= 0:
        return None
    return i


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--ref-rom", required=True)
    ap.add_argument("--new-rom", required=True)
    ap.add_argument("--ref-elfs", required=True, help="the reference build's shift/ directory")
    ap.add_argument("--new-elfs", required=True)
    ap.add_argument("--noise", help="dumps of the reference ROM at the same frame from runs with other CPU timing "
                                    "(comma-separated): words that differ between any of them and the reference "
                                    "are timing-dependent")
    ap.add_argument("--starts", help="REF,NEW: dumps at the start of the level (frame 0): fixed-address words "
                                     "neither run wrote since are stale")
    ap.add_argument("--text", action="store_true", help="compare the code units too (relocated instructions differ)")
    ap.add_argument("-v", action="store_true", help="list every region")
    ap.add_argument("ref_dump")
    ap.add_argument("new_dump")
    args = ap.parse_args()
    ram_ref, rom_ref = load_map(args.ref_rom + ".map.txt")
    ram_new, rom_new = load_map(args.new_rom + ".map.txt")
    ref = open(args.ref_dump, "rb").read()
    new = open(args.new_dump, "rb").read()
    tr = Translator(ram_ref, ram_new, rom_ref, rom_new)
    starts = tuple(open(p, "rb").read() for p in args.starts.split(",")) if args.starts else None

    noise = None
    if args.noise:
        r = array.array("I", ref)
        noise = bytearray(len(r))
        for p in args.noise.split(","):
            x = array.array("I", open(p, "rb").read())
            for i in range(len(r)):
                if x[i] != r[i]:
                    noise[i] = 1

    # leftovers: the boot DMA of the compressed hd_code, the front end's image, the vector code,
    # the load buffer's compressed file
    left = Leftovers()
    roms = (open(args.ref_rom, "rb").read(), open(args.new_rom, "rb").read())
    spans = []
    for rm in (rom_ref, rom_new):
        st = {x[3]: x for x in rm}
        spans.append((st["hd_code_text"][1], st["hd_front_end_text"][1]))
    left.add(0x80000400, roms[0][spans[0][0]:spans[0][1]], roms[1][spans[1][0]:spans[1][1]])
    fe = [elf_image(os.path.join(d, "hd_front_end.elf"), [".hd_front_end", ".hd_front_end_data"])
          for d in (args.ref_elfs, args.new_elfs)]
    left.add(fe[0][0], fe[0][1], fe[1][1])
    hd = [elf_image(os.path.join(d, "hd_code.elf"), [".hd_code"]) for d in (args.ref_elfs, args.new_elfs)]
    vec = []
    for lo, img, e in hd:
        a = e.value("__osExceptionPreamble")
        stub = img[a - lo:a - lo + 0x10]
        vec.append(b"".join(stub + bytes(0x70) for _ in range(4)))
    left.add(0x80000000, vec[0], vec[1])
    LOADBUF, LOADEND = 0x8021ED00, 0x802447C0
    ia = find_rom_copy(roms[0], ref[LOADBUF & 0x7FFFFF:])
    ib = find_rom_copy(roms[1], new[LOADBUF & 0x7FFFFF:])
    loadbuf = "?"
    if ia is not None and ib is not None and tr.romoff(ia) == ib:
        n = LOADEND - LOADBUF
        left.add(LOADBUF, roms[0][ia:ia + n], roms[1][ib:ib + n])
        loadbuf = "ROM %06X / %06X" % (ia, ib)

    fe_resident = False
    fep = [e.value("frontEndPresent") for _, _, e in hd]
    if None not in fep:
        fe_resident = bool(ref[fep[0] & 0x7FFFFF] and new[fep[1] & 0x7FFFFF])
    ar = async_ranges(os.path.join(args.ref_elfs, "hd_code.elf"))
    ar_lo = [x[0] for x in ar]

    def in_async_symbol(addr):
        i = bisect.bisect_right(ar_lo, addr) - 1
        return i >= 0 and addr < ar[i][1]

    # the front end's message queues stay in use in the levels (the pak thread:
    # gPakRequestQueue, gPakReplyMesgs), over the level's heap: at the ref
    # address in one run, at the moved address in the other
    fe_ar = (async_ranges(os.path.join(args.ref_elfs, "hd_front_end.elf")) +
             async_ranges(os.path.join(args.new_elfs, "hd_front_end.elf")))

    def in_fe_async(addr):          # only asked for words that differ
        return any(lo <= addr < hi for lo, hi in fe_ar)

    S = Stats()
    for lab, r in sorted(ram_ref.items(), key=lambda kv: kv[1][3]):
        seg = r[0]
        n = ram_new.get(lab)
        if n is None:
            continue
        if seg == "init" or (seg == "hd_front_end" and not fe_resident):
            continue
        if r[5].endswith("(.text)") and not args.text:
            continue        # code: the shifted ELF's bytes by construction (shiftcheck.py checks them)
        if r[4] != n[4]:
            c = dict((k, 0) for k in KINDS)
            c["diff"] = 1
            S.rows.append((lab + " (size changed)", 0, c, []))
            continue
        whole = bool(ASYNC_UNITS.search(r[5]))
        S.compare(ref, new, r[3], n[3], r[4], tr, "%s %s %s" % (seg, lab, r[5]), noise, None,
                  is_async=(lambda a: True) if whole else in_async_symbol)
    segs = {}
    for lab, r in ram_ref.items():
        n = ram_new[lab]
        if r[0] == "hd_code" or (r[0] == "hd_front_end" and fe_resident):
            lo, hi = segs.get(r[0], (1 << 32, 0))
            segs[r[0]] = (min(lo, r[3], n[3]), max(hi, r[3] + r[4], n[3] + n[4]))
    holes = sorted(segs.values())
    for rlo, rhi, what in FIXED_REGIONS:
        pos = rlo
        for lo, hi in holes + [(rhi, rhi)]:
            lo, hi = min(max(lo, rlo), rhi), max(min(hi, rhi), rlo)
            if lo > pos:
                S.compare(ref, new, pos, pos, lo - pos, tr, "fixed %08X..%08X %s" % (pos, lo, what), noise, left,
                          starts, is_async=in_fe_async if rlo == 0x801E7000 and not fe_resident else None)
            pos = max(pos, hi)
    tot = dict((k, 0) for k in KINDS)
    nbytes = 0
    for label, n, c, firsts in S.rows:
        nbytes += n
        for k in tot:
            tot[k] += c[k]
        if c["diff"] or args.v:
            print("%-58s %6X bytes: %4d differ (%d translated, %d derived, %d leftover, %d stale, %d timing, "
                  "%d other threads')  %s"
                  % (label[:58], n, c["diff"], c["trans"], c["derived"], c["left"], c["stale"], c["timing"],
                     c["async"], "; ".join(firsts)))
    print("compared %X bytes of RAM (front end %s, load buffer %s): %d words equal; %d translated pointers, "
          "%d derived from moved addresses, %d leftovers, %d stale, %d timing-dependent, %d in other threads' "
          "memory; %d differ"
          % (nbytes, "resident" if fe_resident else "not resident", loadbuf, tot["eq"], tot["trans"],
             tot["derived"], tot["left"], tot["stale"], tot["timing"], tot["async"], tot["diff"]))
    sys.exit(1 if tot["diff"] else 0)


if __name__ == "__main__":
    main()
