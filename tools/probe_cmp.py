#!/usr/bin/env python3
# usage: probe_cmp.py probe.o ASMDIR func [func...]
# Compares each probe function's instruction words with the target .s in
# ASMDIR, masking relocated fields (hi/lo immediates where the probe has a
# reloc, jal targets). FULL=a:b prints a side-by-side of instructions a..b.
import subprocess, sys, re, os
obj, asmdir, funcs = sys.argv[1], sys.argv[2], sys.argv[3:]
dis = subprocess.run(['mips-linux-gnu-objdump', '-d', '-z', '-r', obj], capture_output=True, text=True).stdout
probe, cur = {}, None
for l in dis.splitlines():
    m = re.match(r'^[0-9a-f]+ <(\w+)>:', l)
    if m:
        cur = m.group(1); probe[cur] = []; continue
    m = re.match(r'^\s+([0-9a-f]+):\s+([0-9a-f]{8})\s+(.*)', l)
    if m and cur:
        probe[cur].append([int(m.group(2), 16), False, m.group(3)]); continue
    if 'R_MIPS' in l and cur and probe[cur]:
        probe[cur][-1][1] = True
full = os.environ.get('FULL')
for f in funcs:
    tgt = []
    for l in open(os.path.join(asmdir, f + '.s')):
        m = re.match(r'^/\* [0-9A-F]+ [0-9A-F]+ ([0-9A-F]{8}) \*/\s+(.*)', l)
        if m:
            tgt.append((int(m.group(1), 16), m.group(2)))
    p = probe.get(f, [])
    while p and p[-1][0] == 0 and len(p) > len(tgt):
        p.pop()
    bad = 0
    for i in range(max(len(p), len(tgt))):
        a = p[i] if i < len(p) else None
        b = tgt[i] if i < len(tgt) else None
        ok = a and b
        if ok:
            mask = 0xFFFFFFFF
            if a[1]:
                mask = 0xFC000000 if (a[0] >> 26) in (2, 3) else 0xFFFF0000
            ok = (a[0] & mask) == (b[0] & mask)
        if not ok:
            bad += 1
        show = (int(full.split(':')[0]) <= i <= int(full.split(':')[1])) if full else (not ok and bad <= 12)
        if show:
            print('%s %3d  %-34s | %s' % (' ' if ok else '*', i, a[2] if a else '-', b[1] if b else '-'))
    print('%s: %s (%d/%d insns, %d differ)' % (f, 'MATCH' if bad == 0 else 'diff', len(p), len(tgt), bad))
