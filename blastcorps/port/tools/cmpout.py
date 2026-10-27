#!/usr/bin/env python3
"""Compare spike.exe outputs with the reference: per test, cases equal/different.
usage: cmpout.py REF OUT...   (exit 1 if the first OUT differs from REF)"""
import collections
import sys


def load(p):
    d = collections.OrderedDict()
    for line in open(p):
        line = line.strip()
        if line[:1] == "T" and " " in line:
            t, rest = line.split(" ", 1)
            key, _, val = rest.partition(" ")
            d[(t, key)] = val
    return d


ref = load(sys.argv[1])
rc = 0
for n, path in enumerate(sys.argv[2:]):
    out = load(path)
    per = collections.OrderedDict()
    examples = {}
    for k, v in ref.items():
        t = k[0]
        ok = out.get(k) == v
        s = per.setdefault(t, [0, 0])
        s[0 if ok else 1] += 1
        if not ok and t not in examples:
            examples[t] = "%s %s: ref %s, got %s" % (t, k[1], v, out.get(k))
    print("%s:" % path)
    for t, (good, bad) in per.items():
        print("  %s: %d/%d match%s" % (t, good, good + bad, ("   e.g. " + examples[t]) if bad else ""))
    if n == 0 and any(b for _, b in per.values()):
        rc = 1
sys.exit(rc)
