#!/usr/bin/env python3
"""link.exe's map file as `nm -n` output (the MSVC build's EXE.syms).

The exes read EXE.syms with --syms (os_time.c: names for --clock, --sync,
--calls) and tools/compare.py names native code addresses with it; both
expect `nm -n` lines.  This writes, sorted by address:

  ADDRESS T name     every function (map flag 'f'), public or static
  ADDRESS D name     every other symbol in the image
  ADDRESS T etext    the end of the code section (compare.py: pointers past it
                     are data)

ADDRESS has 16 hex digits (x86_64).  MSVC's decorated names (`?x@...`) are kept.

usage: mapsyms.py EXE.map OUT.syms
"""
import re
import sys

# " 0001:00000000       hd_main_thread              0000000040001000 f   00000.obj"
SYM = re.compile(r"^\s*([0-9a-fA-F]{4}):([0-9a-fA-F]{8})\s+(\S+)\s+([0-9a-fA-F]{16})\s+(f\s+)?(i\s+)?(.*)$")
# " 0001:00000000 000c1f08H .text$mn                CODE"
SEC = re.compile(r"^\s*([0-9a-fA-F]{4}):([0-9a-fA-F]{8})\s+([0-9a-fA-F]{8})H\s+(\S+)\s+(\S+)\s*$")


def main():
    mapf, out = sys.argv[1:3]
    syms, text_seg = [], None
    sections = {}    # code segment -> end offset
    seg_rva = {}     # segment -> its address (a symbol's address minus its offset)
    for line in open(mapf, errors="replace"):
        m = SEC.match(line)
        if m:
            seg, off, size, cls = int(m.group(1), 16), int(m.group(2), 16), int(m.group(3), 16), m.group(5)
            if cls == "CODE":
                text_seg = seg
                sections[seg] = max(sections.get(seg, 0), off + size)
            continue
        m = SYM.match(line)
        if not m:
            continue
        seg, off, name, addr = int(m.group(1), 16), int(m.group(2), 16), m.group(3), int(m.group(4), 16)
        if seg == 0:
            continue     # absolute symbols (the N64 data, at their addresses: not the exe's)
        seg_rva.setdefault(seg, addr - off)
        syms.append((addr, "T" if m.group(5) else "D", name))
    # the end of the code section
    if text_seg in seg_rva and text_seg in sections:
        syms.append((seg_rva[text_seg] + sections[text_seg], "T", "etext"))
    syms.sort()
    with open(out, "w") as f:
        for a, t, n in syms:
            f.write("%016x %s %s\n" % (a, t, n))


if __name__ == "__main__":
    main()
