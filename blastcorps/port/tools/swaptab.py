#!/usr/bin/env python3
"""Build-time byte-order table for the RDRAM image (stage 3, "load").

The hd_code and front-end .data/.rodata images are inflated from the ROM in
big-endian byte order.  Bytes that come from C objects (game or libultra
.data/.rodata) need nothing: the port links those objects and copies their
native initialisers over the image (or reads its own copy of the constants).
The rest comes from splat `bin` pieces; those bytes are swapped in place, by
element width, before the game runs:

  1. the C declarations' leaves (typemap.py: DWARF of every game file), and
  2. for bytes no multi-byte declaration covers, the widths the original code
     really reads them at (port/data/image_widths.txt, from the mupen64plus
     access-width tracer over boot, front end and the attract demos).

Writes OUT.c (the swap runs, see port/src/load/port_load.h) and prints the
coverage of the bin bytes plus the conflicts (a byte read at a width that
disagrees with its declaration, or at two widths: the exception list).

  3. port/data/image_overrides.txt: ranges where both are wrong for the
     native code ('ADDR SIZE w4' forces 4-byte words, 'be' keeps the bytes
     big-endian, 'vtx' lays out F3D vertices for the renderer; the reason is
     in each line's comment).

  4. --static FILE ADDRS: fixed-address and element accesses of the native
     game code (port/tools/mixscan.py --widths), for bytes 1-2 leave untyped.

usage: swaptab.py TYPEMAP WIDTHS OUT.c [REPORT] [--static FILE ADDRS] [--maps HD.map FE.map]
       (from the project root, which has build_nm/*.map, unless --maps names them)
"""
import os
import collections
import re
import sys

IMAGES = {
    "hd": (0x802E8BD0, 0x8030F660, "build_nm/hd_code.us.v11.map", ".hd_code_data"),
    "fe": (0x80208040, 0x80210E90, "build_nm/hd_front_end.us.v11.map", ".hd_front_end_data"),
}
CONTRIB = re.compile(r"^ \.(data|rodata|late_rodata)\s+0x([0-9a-f]+)\s+0x([0-9a-f]+)\s+(\S+)")


def bin_ranges(mapfile, lo, hi):
    """[(start, end, object)] of the image's pieces that come from splat bins."""
    out = []
    for line in open(mapfile):
        m = CONTRIB.match(line)
        if not m:
            continue
        a, n, obj = int(m.group(2), 16), int(m.group(3), 16), m.group(4)
        if n and lo <= a < hi and obj.endswith(".bin.o"):
            out.append((a, min(a + n, hi), obj))
    return out


def load_typemap(path):
    """[(addr, name, size, defined, [(off, width, isfloat)])]"""
    syms = []
    for line in open(path):
        p = line.split()
        if len(p) < 4:
            continue
        lv = []
        for t in p[4:]:
            o, w = t.split(":")
            lv.append((int(o, 16), int(w.rstrip("f")), w.endswith("f")))
        syms.append((int(p[0], 16), p[1], int(p[2]), p[3] == "D", lv))
    return syms


def load_widths(path):
    """region -> {offset: set(widths read)} (multi-byte reads only) and region -> set(touched offsets)."""
    reads = collections.defaultdict(lambda: collections.defaultdict(set))
    touched = collections.defaultdict(set)
    try:
        f = open(path)
    except FileNotFoundError:
        return reads, touched
    for line in f:
        p = line.split()
        if not p or p[0].startswith("#"):
            continue
        region, off, w = p[0], int(p[1], 16), p[2]
        if w in ("1", "2", "4", "8"):
            n = int(w)
            for k in range(n):
                touched[region].add(off + k)
            if n > 1:
                reads[region][off].add(n)
        else:
            reads[region][off].add(w)
    return reads, touched


def load_static(path):
    direct = collections.defaultdict(lambda: collections.defaultdict(set))
    indexed = collections.defaultdict(lambda: collections.defaultdict(set))
    if path and os.path.exists(path):
        for line in open(path):
            p = line.split()
            if not p or p[0].startswith("#"):
                continue
            (indexed if len(p) > 3 else direct)[p[0]][int(p[1], 16)].add(int(p[2]))
    return direct, indexed


def main():
    args = sys.argv[1:]
    static_path = addrs_path = None
    if "--maps" in args:
        # the NM link maps from elsewhere (the MSVC build's inputs folder)
        i = args.index("--maps")
        for region, path in (("hd", args[i + 1]), ("fe", args[i + 2])):
            lo, hi, _, sec = IMAGES[region]
            IMAGES[region] = (lo, hi, path, sec)
        del args[i:i + 3]
    if "--static" in args:
        i = args.index("--static")
        static_path, addrs_path = args[i + 1], args[i + 2]
        del args[i:i + 3]
    tm_path, widths_path, out_path = args[0:3]
    report = open(args[3], "w") if len(args) > 3 else None
    syms = load_typemap(tm_path)
    reads, touched = load_widths(widths_path)
    static_direct, static_indexed = load_static(static_path)
    starts = sorted(set([a for a, *_ in syms] +
                        ([int(l.split()[1], 16) for l in open(addrs_path) if len(l.split()) > 1] if addrs_path else [])))

    def next_sym(a):
        import bisect
        i = bisect.bisect_right(starts, a)
        return starts[i] if i < len(starts) else a + 0x10
    overrides = []
    ovp = os.path.join(os.path.dirname(widths_path), "image_overrides.txt")
    if os.path.exists(ovp):
        for line in open(ovp):
            p = line.split("#")[0].split()
            if len(p) >= 3:
                # optional STRIDE COUNT: the same range in COUNT records
                stride, count = (int(p[3], 16), int(p[4], 16)) if len(p) >= 5 else (0, 1)
                for k in range(count):
                    overrides.append((int(p[0], 16) + k * stride, int(p[1], 16), p[2]))
    runs_by = {}
    summary = []
    for region, (lo, hi, mapfile, _) in IMAGES.items():
        bins = bin_ranges(mapfile, lo, hi)
        inbin = bytearray(hi - lo)
        for a, b, _ in bins:
            for k in range(a, b):
                inbin[k - lo] = 1
        width = {}            # leaf start address -> width
        cover = bytearray(hi - lo)   # 0 none, 1 declared byte/opaque, 2 decl leaf, 3 trace leaf
        owner = {}
        conflicts = []
        for a, name, size, defined, lv in syms:
            if not (lo <= a < hi) or defined:
                continue
            for k in range(a, min(a + size, hi)):
                if cover[k - lo] == 0:
                    cover[k - lo] = 1
            for o, w, _ in lv:
                s = a + o
                if not (lo <= s and s + w <= hi) or not inbin[s - lo]:
                    continue
                clash = [k for k in range(s, s + w) if cover[k - lo] == 2 and owner.get(k) != (s, w)]
                if clash:
                    conflicts.append("%s: %08X width %d (%s) overlaps a declared leaf at %08X" %
                                     (region, s, w, name, clash[0]))
                    continue
                width[s] = w
                for k in range(s, s + w):
                    cover[k - lo] = 2
                    owner[k] = (s, w)
        def add_widths(src, code, what):
            """leaves from observed accesses where nothing stronger types the bytes"""
            for off, ws in sorted(src.items()):
                s = lo + off
                if not (lo <= s < hi) or not inbin[off]:
                    continue
                nums = sorted(w for w in ws if isinstance(w, int))
                odd = [w for w in ws if not isinstance(w, int)]
                if odd:
                    conflicts.append("%s: %08X %s unaligned (%s)" % (region, s, what, ",".join(odd)))
                if len(nums) > 1:
                    conflicts.append("%s: %08X %s at widths %s" % (region, s, what, "/".join(map(str, nums))))
                if not nums:
                    continue
                w = nums[-1]
                if code == 3 and s + w <= hi and not s % w:
                    # the NON_MATCHING code reads one wider field where the
                    # declarations (a per-file struct view) have narrower ones:
                    # the trace wins (struct copies are the exception, listed
                    # in image_overrides.txt)
                    inner = [p for p in list(width) if s <= p < s + w]
                    if inner and all(p + width[p] <= s + w and width[p] < w for p in inner) and \
                            all(cover[k - lo] in (0, 1, 2) for k in range(s, s + w)) and \
                            not any(p < s < p + width[p] for p in range(s - 7, s) if p in width):
                        conflicts.append("%s: %08X declared %s, read at %d: read width used" %
                                         (region, s, "/".join(str(width[p]) for p in sorted(inner)), w))
                        for p in inner:
                            del width[p]
                        width[s] = w
                        for k in range(s, s + w):
                            cover[k - lo] = 3
                        continue
                if s in width:
                    if width[s] != w:
                        conflicts.append("%s: %08X width %d, %s at %d" % (region, s, width[s], what, w))
                    continue
                if any(cover[k - lo] >= 2 for k in range(s, min(s + w, hi))):
                    if code == 3:
                        conflicts.append("%s: %08X %s at width %d inside another leaf" % (region, s, what, w))
                    continue
                if s + w > hi or s % w:
                    conflicts.append("%s: %08X %s at width %d, misaligned" % (region, s, what, w))
                    continue
                width[s] = w
                for k in range(s, s + w):
                    cover[k - lo] = code

        # the tracer's read widths, where no declaration types the bytes
        add_widths(reads[region], 3, "read")
        # then the native code's fixed-address accesses (paths the trace didn't run)
        add_widths(static_direct[region], 5, "accessed")
        # and its element accesses (scale == width): the array runs to the next symbol
        for off, ws in sorted(static_indexed[region].items()):
            w = max(ws)
            s = lo + off
            if w < 2 or s % w or not (lo <= s < hi):
                continue
            nxt = next_sym(s)
            for e in range(s, min(nxt, hi) - w + 1, w):
                if not all(inbin[k - lo] and cover[k - lo] < 2 for k in range(e, e + w)):
                    break
                width[e] = w
                for k in range(e, e + w):
                    cover[k - lo] = 6
        # overrides
        for s0, n, act in overrides:
            if not (lo <= s0 < hi):
                continue
            for s in [x for x in width if s0 <= x < s0 + n]:
                del width[s]
            for k in range(s0, s0 + n):
                cover[k - lo] = 4 if act == "be" else 2
            if act.startswith("w"):
                w = int(act[1:])
                for s in range(s0, s0 + n, w):
                    width[s] = w
            elif act == "vtx":
                # F3D Vtx records {s16 x, y, z, flag, s, t; u8 r, g, b, a}:
                # six halfwords, the colour bytes stay as they are
                for r in range(s0, s0 + n, 16):
                    for h in range(0, 12, 2):
                        width[r + h] = 2
        # runs: consecutive leaves of one width
        runs = []
        for s in sorted(width):
            w = width[s]
            if runs and runs[-1][2] == w and runs[-1][0] + runs[-1][1] * w == s:
                runs[-1][1] += 1
            else:
                runs.append([s, 1, w])
        runs_by[region] = runs
        # coverage of the bin bytes
        tot = sum(inbin)
        c = collections.Counter()
        for k in range(hi - lo):
            if not inbin[k]:
                continue
            if cover[k] == 4:
                c["be"] += 1
            elif cover[k] == 2:
                c["decl"] += 1
            elif cover[k] == 3:
                c["trace"] += 1
            elif cover[k] == 5:
                c["static"] += 1
            elif cover[k] == 6:
                c["staticidx"] += 1
            elif k in touched[region]:
                c["bytes"] += 1        # read, but only ever as bytes
            elif cover[k] == 1:
                c["declbyte"] += 1     # declared u8[]/opaque, never seen read
            else:
                c["none"] += 1
        summary.append((region, hi - lo, tot, c, len(runs), len(conflicts)))
        if report:
            report.write("# %s conflicts\n" % region)
            report.write("".join(x + "\n" for x in conflicts))
            # the untyped bin bytes by symbol (what's still unswapped), largest first
            names = {}
            for a, name, size, defined, lv in syms:
                names[a] = (name, "decl")
            for a in starts:
                names.setdefault(a, ("D_%08X" % a, "undeclared"))
            spans = []
            cur = None
            for k in range(hi - lo):
                a = lo + k
                if a in names or cur is None:
                    if cur and cur[2]:
                        spans.append(cur)
                    cur = [a, names.get(a, ("?", "?")), 0]
                if inbin[k] and cover[k] in (0, 1) and k not in touched[region]:
                    cur[2] += 1
            if cur and cur[2]:
                spans.append(cur)
            report.write("# %s untyped bin bytes by symbol (bytes, start, name, declared?)\n" % region)
            for a, (name, d), n in sorted(spans, key=lambda x: -x[2]):
                report.write("%6X %08X %s %s\n" % (n, a, name, d))
    with open(out_path, "w") as f:
        f.write("/* generated by port/tools/swaptab.py: byte-order runs for the RDRAM image */\n")
        f.write('#include "load/port_load.h"\n')
        for region, runs in runs_by.items():
            f.write("const PortSwapRun port_swap_%s[] = {\n" % region)
            for s, n, w in runs:
                f.write("    { 0x%08Xu, %du, %d },\n" % (s, n, w))
            f.write("    { 0, 0, 0 }\n};\n")
    for region, size, tot, c, nruns, ncon in summary:
        print("%s image 0x%X bytes, 0x%X from bins (the rest is C: native)" % (region, size, tot))
        for k, label in (("decl", "swapped by C declaration"), ("trace", "swapped by traced read width"),
                         ("static", "swapped by native fixed-address access"),
                         ("staticidx", "swapped by native array access"),
                         ("be", "kept big-endian (override)"),
                         ("bytes", "traced, read as bytes only (no swap)"),
                         ("declbyte", "declared byte/opaque, not seen read"),
                         ("none", "undeclared, not seen read")):
            print("  %-40s 0x%05X (%5.1f%% of bins)" % (label, c[k], 100.0 * c[k] / max(tot, 1)))
        print("  %d runs, %d conflicts" % (nruns, ncon))


if __name__ == "__main__":
    main()
