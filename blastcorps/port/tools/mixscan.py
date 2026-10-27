#!/usr/bin/env python3
"""Mixed-width accesses in the native build (byte-order exception class a).

Scans the i686 assembly gcc produced for the game C (port/build/.../*.s)
for direct accesses to absolute N64 data (`_D_xxxxxxxx+off`, KSEG0 literals)
and reports addresses that the program reads or writes at more than one
width where the accesses overlap: on the big-endian N64 a byte read of a
word's first byte sees its most significant byte, on x86 the least.  Copies
of the same bytes at one width are harmless, so the report is a review list,
not a verdict.  Indexed accesses (`_D_x(,%eax,4)`) only give the base and
are listed separately as context.

usage: mixscan.py ADDRS S_FILE...              (ADDRS = port/build/addrs.txt)
       mixscan.py --widths OUT ADDRS S_FILE...  the reads of the data images at
           fixed addresses ('hd|fe OFFSET WIDTH [idx]'), for port/tools/swaptab.py:
           code paths the attract-demo trace never ran still type their data
"""
import collections
import re
import sys

MEM = re.compile(r"(?<![\w$])(_D_[0-9A-F]{8})(?:\.\d+)?([+-]\d+)?(\([^)]*\))?|(?<![\w$.])(-\d{10})(\([^)]*\))?")
SKIP = {"leal", "lea", "call", "jmp", "pushl" , "nop", "prefetcht0"}


def width(m, ops):
    if m in SKIP or m.startswith(("j", "set", "rep", "cmov")) and not m.startswith("setb"):
        return None
    if m.startswith(("movz", "movs")) and len(m) == 6 and m not in ("movsbl", "movswl") and m[4] in "bw":
        return {"b": 1, "w": 2}[m[4]]
    if m in ("movsbl", "movsbw", "movswl", "movzbl", "movzbw", "movzwl"):
        return {"b": 1, "w": 2}[m[4]]
    if m in ("movdqa", "movdqu", "movaps", "movups", "movapd", "movupd"):
        return 16
    if m in ("movq", "movlps", "movhps", "movlpd", "movhpd"):
        return 8
    if m == "movd":
        return 4
    if m.endswith("ss") or m in ("cvtss2sd", "cvttss2si", "cvtss2si", "comiss", "ucomiss"):
        return 4
    if m.endswith("sd") or m in ("cvtsd2ss", "cvttsd2si", "cvtsd2si", "comisd", "ucomisd"):
        return 8
    if m.startswith("cvtsi2"):
        return 8 if m.endswith("q") else 4
    if m in ("flds", "fsts", "fstps", "fadds", "fmuls", "fsubs", "fdivs", "fcomps", "fcoms"):
        return 4
    if m in ("fldl", "fstl", "fstpl", "faddl", "fmull", "fsubl", "fdivl", "fildll", "fistpll", "fildq", "fistpq"):
        return 8
    if m in ("fildl", "fistl", "fistpl", "fisttpl"):
        return 4
    if m in ("fildw", "fists", "fistps", "fildS"):
        return 2
    if m[-1] in "bwlq" and m not in ("cltd", "cwtl"):
        return {"b": 1, "w": 2, "l": 4, "q": 8}[m[-1]]
    return None


IMAGES = {"hd": (0x802E8BD0, 0x8030F660), "fe": (0x80208040, 0x80210E90)}


def main():
    widths_out = None
    if sys.argv[1] == "--widths":
        widths_out = sys.argv[2]
        del sys.argv[1:3]
    addrs = {}
    for line in open(sys.argv[1]):
        p = line.split()
        if len(p) >= 2:
            addrs[p[0]] = (int(p[1], 16), int(p[2]) if len(p) > 2 else 0)
    direct = collections.defaultdict(set)      # addr -> {(width, kind)}
    where = collections.defaultdict(set)       # (addr, width) -> {file:func}
    indexed = collections.defaultdict(set)
    for path in sys.argv[2:]:
        fn = path.rsplit("/", 1)[-1]
        func = "?"
        for line in open(path):
            if line and not line[0].isspace() and line.rstrip().endswith(":") and not line.startswith(("L", ".L")):
                func = line.rstrip()[:-1].lstrip("_")
                continue
            s = line.strip()
            if not s or s[0] in ".#":
                continue
            parts = s.split(None, 1)
            m = parts[0]
            ops = parts[1] if len(parts) > 1 else ""
            for mm in MEM.finditer(ops):
                if mm.group(1):
                    name = mm.group(1)[1:]
                    if name not in addrs:
                        continue
                    a = addrs[name][0] + int(mm.group(2) or 0)
                    idx = mm.group(3)
                else:
                    a = int(mm.group(4)) & 0xFFFFFFFF
                    if not (0x80000000 <= a < 0x80800000):
                        continue
                    idx = mm.group(5)
                w = width(m, ops)
                if w is None or w == 16:
                    continue
                # destination operand is the last one in AT&T syntax
                kind = "W" if ops.rstrip().endswith(mm.group(0)) and not m.startswith(("cmp", "test")) else "R"
                if idx:
                    # an element access: only when the scale is the width
                    # (`_D_x(,%eax,4)` with movl) is it an array of that width
                    sc = re.search(r",\s*(\d)\)$", idx)
                    if (sc and int(sc.group(1)) == w) or (not sc and w == 1):
                        indexed[a].add(w)
                    continue
                direct[a].add((w, kind))
                where[(a, w)].add("%s:%s" % (fn.replace(".pinned.s", "").replace(".s", ""), func))
    if widths_out:
        with open(widths_out, "w") as f:
            f.write("# region offset width [idx]: fixed-address accesses of the native game code to the data\n"
                    "# images (port/tools/mixscan.py --widths over the i686 assembly)\n")
            for region, (lo, hi) in IMAGES.items():
                for a in sorted(set(direct) | set(indexed)):
                    if not (lo <= a < hi):
                        continue
                    for w in sorted(set(w for w, _ in direct.get(a, ()))):
                        f.write("%s %X %d\n" % (region, a - lo, w))
                    for w in sorted(indexed.get(a, ())):
                        f.write("%s %X %d idx\n" % (region, a - lo, w))
        return
    # overlap analysis per byte
    cover = collections.defaultdict(set)
    for a, ws in direct.items():
        for w, _ in ws:
            for k in range(w):
                cover[a + k].add((a, w))
    groups = {}
    for b, accs in cover.items():
        if len(accs) > 1:
            key = min(x[0] for x in accs)
            groups.setdefault(key, set()).update(accs)
    rev = {}
    for n, (a, _) in addrs.items():
        rev.setdefault(a, n)
    order = sorted(rev)
    import bisect

    def symname(a):
        i = bisect.bisect_right(order, a) - 1
        if i < 0:
            return "?"
        base = order[i]
        return rev[base] + ("+0x%X" % (a - base) if a != base else "")

    merged = []
    for key in sorted(groups):
        if merged and key < max(a + w for a, w in merged[-1]):
            merged[-1] |= groups[key]
        else:
            merged.append(set(groups[key]))
    print("# %d groups of overlapping accesses at different widths (direct)" % len(merged))
    for g in merged:
        items = sorted(g)
        print("%s:" % symname(items[0][0]))
        for a, w in items:
            kinds = "".join(sorted(k for ww, k in direct[a] if ww == w))
            print("   %08X w%d %s  %s" % (a, w, kinds, " ".join(sorted(where[(a, w)])[:6])))


if __name__ == "__main__":
    main()
