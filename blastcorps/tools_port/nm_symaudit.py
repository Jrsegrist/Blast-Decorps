#!/usr/bin/env python3
"""nm_symaudit: check that no data symbol moved between build/ and build_nm/.

The NON_MATCHING link relocates only the hd_code and front-end text.  Every data, .bss and
absolute symbol must keep its original address, otherwise the hand asm (which
uses raw addresses) and a C rewrite (which uses the symbol) would see two
different homes for one variable.  This compares the symbol tables of both
builds' ELFs and lists every non-text symbol whose address differs.

    python3 tools_port/nm_symaudit.py [--ref build] [--new build_nm]

Exit status 1 if anything moved.  runchecks.py runs it after -b/-B builds.
"""
import argparse
import os
import re
import sys

from elftools.elf.elffile import ELFFile
from elftools.elf.sections import SymbolTableSection


def load(path):
    syms, dup = {}, set()
    with open(path, "rb") as f:
        e = ELFFile(f)
        secs = list(e.iter_sections())
        text = [(s["sh_addr"], s["sh_addr"] + s["sh_size"]) for s in secs
                if s["sh_flags"] & 4 and s["sh_flags"] & 2]
        for st in secs:
            if not isinstance(st, SymbolTableSection):
                continue
            for sy in st.iter_symbols():
                n = sy.name
                if not n or sy["st_info"]["type"] in ("STT_SECTION", "STT_FILE") \
                        or sy["st_shndx"] == "SHN_UNDEF":
                    continue
                if n in syms and syms[n][0] != sy["st_value"]:
                    dup.add(n)          # same-named locals in several files: ambiguous
                sec = "ABS" if sy["st_shndx"] == "SHN_ABS" else secs[sy["st_shndx"]].name
                syms[n] = (sy["st_value"], sec)
    return syms, text, dup


def audit(ref_dir, new_dir, version="us.v11"):
    """-> list of (elf, name, ref_addr, ref_sec, new_addr, new_sec)"""
    moved = []
    elfs = ("hd_code", "init", "hd_front_end")
    loaded = {elf: (load(os.path.join(ref_dir, "%s.%s.elf" % (elf, version))),
                    load(os.path.join(new_dir, "%s.%s.elf" % (elf, version)))) for elf in elfs}
    # code moves on purpose, including one segment's absolute symbols for the
    # other's functions (the front end calls hd_code's NM addresses)
    rtext = [t for (_, rt, _), _ in loaded.values() for t in rt]
    for elf in elfs:
        (r, _, rdup), (n, _, ndup) = loaded[elf]
        for name, (v, sec) in sorted(r.items()):
            if name in rdup or name in ndup or name not in n:
                continue
            # code (moves on purpose); _binary_*_end labels at the end of the text
            if any(lo <= v < hi for lo, hi in rtext) or \
                    (name.startswith("_binary_") and any(v == hi for _, hi in rtext)):
                continue
            v2, sec2 = n[name]
            if v2 != v:
                moved.append((elf, name, v, sec, v2, sec2))
    return moved


def pinned_tables(ref_dir, new_dir, version="us.v11"):
    """The NON_MATCHING C definitions of the tables inside the original .text
    bins (`.nm_pin_<ADDR>_<SIZE>` sections, nm_ldscript.py --pin-object) must
    hold exactly the bytes the matching build has at ADDR..ADDR+SIZE.
    -> (list of (elf, section, addr, size), list of problem strings)"""
    seen, bad = [], []
    for elf in ("hd_code", "hd_front_end"):
        with open(os.path.join(ref_dir, "%s.%s.elf" % (elf, version)), "rb") as f:
            ref = [(s["sh_addr"], s.data()) for s in ELFFile(f).iter_sections()
                   if s["sh_flags"] & 2 and s["sh_type"] != "SHT_NOBITS" and s["sh_size"]]
        with open(os.path.join(new_dir, "%s.%s.elf" % (elf, version)), "rb") as f:
            for s in ELFFile(f).iter_sections():
                m = re.match(r"^\.nm_pin_([0-9A-F]{8})_([0-9A-F]+)$", s.name)
                if not m:
                    continue
                addr, size = int(m.group(1), 16), int(m.group(2), 16)
                seen.append((elf, s.name, addr, size))
                if s["sh_addr"] != addr or s["sh_size"] < size:
                    bad.append("%s %s: at 0x%08X, 0x%X bytes" % (elf, s.name, s["sh_addr"], s["sh_size"]))
                    continue
                want = None
                for a, d in ref:
                    if a <= addr and addr + size <= a + len(d):
                        want = d[addr - a:addr - a + size]
                if want is None:
                    bad.append("%s %s: no matching-build section covers it" % (elf, s.name))
                    continue
                got = s.data()[:size]
                if got != want:
                    diffs = [i for i in range(size) if got[i] != want[i]]
                    bad.append("%s %s: %d byte(s) differ from %s, first at 0x%08X (%02X, want %02X)"
                               % (elf, s.name, len(diffs), ref_dir, addr + diffs[0], got[diffs[0]],
                                  want[diffs[0]]))
    return seen, bad


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--ref", default="build")
    ap.add_argument("--new", default="build_nm")
    ap.add_argument("--version", default="us.v11")
    o = ap.parse_args()
    moved = audit(o.ref, o.new, o.version)
    pins, bad = pinned_tables(o.ref, o.new, o.version)
    rc = 0
    if not moved:
        print("nm_symaudit: OK, no data symbol moved between %s and %s" % (o.ref, o.new))
    else:
        print("nm_symaudit: %d data symbol(s) moved between %s and %s:" % (len(moved), o.ref, o.new))
        for elf, name, v, sec, v2, sec2 in moved:
            print("  %-8s %-28s 0x%08X (%s) -> 0x%08X (%s)" % (elf, name, v, sec, v2, sec2))
        rc = 1
    if bad:
        print("nm_symaudit: %d pinned table(s) differ from the original bin bytes:" % len(bad))
        for b in bad:
            print("  " + b)
        rc = 1
    elif pins:
        print("nm_symaudit: OK, %d pinned table(s) (0x%X bytes) equal the original bin bytes"
              % (len(pins), sum(p[3] for p in pins)))
    return rc


if __name__ == "__main__":
    sys.exit(main())
