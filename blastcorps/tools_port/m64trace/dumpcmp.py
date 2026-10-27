#!/usr/bin/env python3
"""Compare two RAM dumps (m64trace DUMPS) word by word; print differing ranges with the nearest
symbol. Word pairs that are a code pointer in the base build and in the NM build are skipped.
usage: dumpcmp.py A B [maxruns [lo hi]]"""
import sys, re, bisect, glob, os

a, b = sys.argv[1], sys.argv[2]
maxl = int(sys.argv[3]) if len(sys.argv) > 3 else 80
repo = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..")
syms = {}
for f in glob.glob(repo + "/undefined_syms*.txt"):
    for line in open(f):
        m = re.match(r"\s*(\w+)\s*=\s*(0x[0-9A-Fa-f]+)", line)
        if m:
            v = int(m.group(2), 16)
            if 0x80000000 <= v < 0x80800000:
                syms.setdefault(v, m.group(1))
addrs = sorted(syms)


def name(x):
    i = bisect.bisect_right(addrs, x) - 1
    if i < 0:
        return "?"
    return "%s+0x%x" % (syms[addrs[i]], x - addrs[i])


def load(p):
    d = {}
    hdr = ""
    for line in open(p):
        if line.startswith("#"):
            hdr = line.strip()
            continue
        k, v = line.split()
        d[int(k, 16)] = int(v, 16)
    return hdr, d


ha, da = load(a)
hb, db = load(b)
print(ha)
print(hb)
lo = int(sys.argv[4], 16) if len(sys.argv) > 4 else 0
hi = int(sys.argv[5], 16) if len(sys.argv) > 5 else 0xFFFFFFFF


def codeptr(x, y):
    return 0x80244000 <= x < 0x802E9000 and (0x80400000 <= y < 0x80600000 or 0x80800000 <= y < 0x80A00000)


diffs = [k for k in sorted(da) if k in db and da[k] != db[k] and lo <= k < hi and not codeptr(da[k], db[k])]
print("differing words:", len(diffs))
# group into runs
runs = []
for k in diffs:
    if runs and k - runs[-1][1] <= 8:
        runs[-1][1] = k
    else:
        runs.append([k, k])
for i, (s, e) in enumerate(runs[:maxl]):
    words = " ".join("%08x/%08x" % (da[x], db[x]) for x in range(s, e + 4, 4) if da[x] != db[x])[:160]
    print("%08x-%08x %-28s %s" % (s, e + 3, name(s), words))
if len(runs) > maxl:
    print("... %d more runs" % (len(runs) - maxl))
