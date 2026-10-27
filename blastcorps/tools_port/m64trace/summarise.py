#!/usr/bin/env python3
"""Summarise a trace log: per label, per register, distinct values (weighted by #count lines).
usage: trace_sum.py LOG [label ...]"""
import sys, re, collections

log = sys.argv[1]
want = set(sys.argv[2:])
stats = collections.defaultdict(lambda: collections.defaultdict(collections.Counter))
levels = collections.defaultdict(collections.Counter)
for line in open(log):
    if not line.startswith("#count"):
        continue
    head, *rest = line.rstrip("\n").split(" | ")
    _, lab, cnt = head.split()
    if want and lab not in want:
        continue
    cnt = int(cnt)
    body = " ".join(rest)
    lv = re.search(r"802e8bdc:(\w+)", body)
    lvl = lv.group(1) if lv else "?"
    levels[lab][lvl] += cnt
    for k, v in re.findall(r"(\[?[\w+]+\]?)=([0-9a-f-]+(?:\([^)]*\))?)", body):
        stats[lab][k][v] += cnt
    for k, v in re.findall(r"(80[0-9a-f]{6}):([0-9a-f]+)", body):
        stats[lab]["m" + k][v] += cnt
for lab in stats:
    print("==", lab, "levels:", dict(levels[lab]))
    for k, c in stats[lab].items():
        items = c.most_common(8)
        more = "" if len(c) <= 8 else " ... (%d distinct)" % len(c)
        print("   %-10s %s%s" % (k, ", ".join("%s x%d" % (v, n) for v, n in items), more))
