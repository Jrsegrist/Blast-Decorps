#!/usr/bin/env python3
"""Little-endian copies of a few SDK sources (the port's `le/` files).

The same edits port/Makefile makes with sed (see the comments there), for the
MSVC build (port/CMakeLists.txt), which runs on Windows without sed:

  lesrc.py controller  IN OUT   PRinternal/controller.h: __OSInodeUnit's (bank,
                                page) and __OSPackId's (banks, version) byte pairs
                                trade places (pif.c delivers host-order u16s)
  lesrc.py pfsalloc    IN OUT   pfsallocatefile.c: the new directory entry's status
                                and reserved bytes get the N64's stack leftovers
  lesrc.py xldtob      IN OUT   libc xldtob.c: the double's half-word indices
  lesrc.py xprintf     IN OUT   ul_xprintf.c: a double's sign half-word is the last
  lesrc.py gudouble    IN OUT   gu sinf.c/cosf.c: `du` constants {hi, lo} -> {lo, hi}

Each edit is checked, as the Makefile's grep checks do.
"""
import re
import sys


def swap_pair(lines, first, second, marker):
    for i in range(len(lines) - 1):
        if marker in lines[i] and first in lines[i] and second in lines[i + 1]:
            lines[i] = lines[i].replace(first, second, 1)
            lines[i + 1] = lines[i + 1].replace(second, first, 1)


def main():
    kind, inp, outp = sys.argv[1:4]
    text = open(inp, newline="").read()
    lines = text.split("\n")
    if kind == "controller":
        swap_pair(lines, "u8 bank;", "u8 page;", "/* 0x0 */ u8 bank;")
        swap_pair(lines, "u8 banks;", "u8 version;", "u8 banks;")
        out = "\n".join(lines)
        ok = re.search(r"u8 page;.*\n.*u8 bank;", out) and re.search(r"u8 version;.*\n.*u8 banks;", out)
    elif kind == "pfsalloc":
        lines = [re.sub(r"^( *)dir\.data_sum = 0;",
                        r"\1dir.data_sum = 0;\n\1dir.status = 2; dir.reserved = 0; /* port: the N64 stack leftovers */",
                        l) for l in lines]
        out = "\n".join(lines)
        ok = "dir.reserved = 0" in out
    elif kind == "xldtob":
        for a, b in (("0", "3"), ("1", "2"), ("2", "1"), ("3", "0")):
            lines = [("#define _D%s %s" % (a, b)) + l[len("#define _D0 0"):] if l.startswith("#define _D%s %s" % (a, a))
                     else l for l in lines]
        out = "\n".join(lines)
        ok = re.search(r"^#define _D0 3", out, re.M) and re.search(r"^#define _D3 0", out, re.M)
    elif kind == "xprintf":
        old, new = "(((unsigned short *)&(x))[0] & 0x8000)", "(((unsigned short *)&(x))[3] & 0x8000)"
        lines = [l.replace(old, new, 1) for l in lines]
        out = "\n".join(lines)
        ok = "short *)&(x))[3]" in out
    elif kind == "gudouble":
        pat = re.compile(r"\{(0x[0-9a-fA-F]{8}),\s*(0x[0-9a-fA-F]{8})\}")
        lines = [pat.sub(r"{\2, \1}", l, count=1) for l in lines]
        out = "\n".join(lines)
        ok = "{0x00000000, 0x3ff00000}" in out
    else:
        raise SystemExit("lesrc: unknown kind " + kind)
    if not ok:
        raise SystemExit("lesrc: %s: the edit did not apply to %s" % (kind, inp))
    with open(outp, "w", newline="") as f:
        f.write(out)


if __name__ == "__main__":
    main()
