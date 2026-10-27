#!/usr/bin/env python3
"""The N64 data symbols of the 64-bit build as a COFF object (port/README.md, "64-bit").

Writes a COFF x86_64 object that defines each given data symbol as an absolute
symbol (IMAGE_SYM_ABSOLUTE) at its N64 address.  It replaces the 32-bit build's
GNU ld script (abs_syms.ld): an object file links the same way with GNU ld,
lld-link and link.exe, and needs no linker-script support.

Code refers to these symbols RIP-relative (REL32), so the exe is linked at a
fixed base below RDRAM and within 2 GB of it (image base 0x40000000, no
relocations, no dynamic base); RDRAM itself is reserved at 0x80000000 at run
time (rdram.c).  (A section of the image at 0x80000000 would need the image to
span 0x7FFE0000, where Windows maps KUSER_SHARED_DATA in every process.)

usage: rdramobj.py [--base HEX] OUT.o SYMS      SYMS: 'name addr' lines (hex address)

--base IMAGE_BASE (MSVC's link.exe): each value is written as the address minus
the image base, modulo 2^32: link.exe resolves REL32 fixups against an absolute
symbol as image base + value (it takes the value for an RVA), so the code then
reaches the N64 address; ADDR64/ADDR32 fixups take the value as it is, and
coffpin.py --base adds the image base to their addends.
"""
import struct
import sys

RDRAM_BASE = 0x80000000
RDRAM_SIZE = 0x800000
MACHINE_AMD64 = 0x8664


def write(out, syms, machine=MACHINE_AMD64, base=0):
    strtab = bytearray(b"\0\0\0\0")
    symtab = bytearray()
    n = 0
    for name, addr in sorted(syms, key=lambda s: (s[1], s[0])):
        # N64 addresses, and the ROM offsets the game names as symbols (D_00xxxxxx)
        if not 0 <= addr < 1 << 32:
            raise SystemExit("rdramobj: %s = 0x%X is not a 32-bit value" % (name, addr))
        b = name.encode()
        if len(b) <= 8:
            nm = b.ljust(8, b"\0")
        else:
            nm = b"\0\0\0\0" + struct.pack("<I", len(strtab))
            strtab += b + b"\0"
        # value, section -1 (absolute), type 0, class external, no aux
        symtab += nm + struct.pack("<IhHBB", (addr - base) & 0xFFFFFFFF, -1, 0, 2, 0)
        n += 1
    struct.pack_into("<I", strtab, 0, len(strtab))
    data = bytearray(struct.pack("<HHIIIHH", machine, 0, 0, 20, n, 0, 0))
    data += symtab + strtab
    open(out, "wb").write(data)


def main():
    args = sys.argv[1:]
    base = 0
    if args[:1] == ["--base"]:
        base = int(args[1], 16)
        args = args[2:]
    out, syms_path = args[0:2]
    syms, seen = [], set()
    for line in open(syms_path):
        p = line.split()
        if len(p) >= 2 and p[0] not in seen:
            seen.add(p[0])
            syms.append((p[0], int(p[1], 16)))
    write(out, syms, base=base)


if __name__ == "__main__":
    main()
