#!/usr/bin/env python3
"""Derive NON_MATCHING linker scripts from the splat-generated ones.

Usage: nm_ldscript.py IN.ld OUT.ld --build-dir build_nm
                      [--relocate SECTION=VRAM] [--pin-data SECTION]
                      [--extra-vram ADDR --extra-glob GLOB [--extra-name .nm_extra_fe]]
       nm_ldscript.py undefined_syms.X.txt OUT.txt --provide [--resolve-elf OTHER.elf]

What it does (see tools_port/README.md for the why):
  * rewrites every `build/...` object path to `<build-dir>/...`;
  * --relocate .hd_code=0x80800000 moves a code section to a new VRAM, so a
    NON_MATCHING rewrite that is bigger than the original asm can never run
    into the data segment that follows it (which must stay at its original
    address: most data symbols are absolute `D_xxxxxxxx = 0x...` definitions);
  * --pin-data .hd_code_data pins every `<seg>_<ROMOFF>_bin = .;` label in
    that section back to its original address (`. = ROMOFF - SEG_ROM_START`),
    so a C file whose .rodata changed size cannot shift the binary blobs
    after it.  A file whose .rodata *grew* past the next blob makes ld fail
    with "cannot move location counter backwards", which is what we want;
  * --extra-vram/--extra-glob add catch-all output sections before
    /DISCARD/ for .rodata/.data/.bss that a rewritten file now has but the
    matching layout never placed (e.g. float constants or a jump table in
    one of the hand-written-asm files);
  * --provide turns the *function* assignments of an undefined_*.txt symbol
    file (func_* names, aliases `a = b;`, and everything in undefined_funcs*)
    into `PROVIDE(name = ...);`.  In the matching link those absolute
    assignments override object definitions (harmless, the addresses agree).
    In the relocated link they would send every call of a function listed
    there to its *original* address, silently bypassing the rewrite.  Data
    assignments stay hard, so every data symbol keeps its original address
    even when a C file also defines it (tools_port/nm_symaudit.py checks).
  * --resolve-elf OTHER.elf (with --provide) gives every assignment whose
    name is a code symbol of OTHER.elf (defined in one of its executable
    sections) that symbol's address there.  The front end's symbol files name
    hd_code's functions at their original addresses; resolved against the
    NON_MATCHING hd_code ELF, the NON_MATCHING front end calls hd_code's
    relocated (and rewritten) code directly, which matters for rewrites with
    a non-ABI convention (their C version is not at, and does not take the
    registers of, the original address).
  * --extra-name names the catch-all section (default .nm_extra), so two
    segments' catch-alls stay apart (the front end uses .nm_extra_fe).
  * --pin-object ADDR:SIZE=OBJ (repeatable) links OBJ's .data, alone, in an
    output section `.nm_pin_<ADDR>_<SIZE>` at ADDR.  For the data tables
    the matching build keeps inside its .text bins (68810, 690C0, 7D9D0,
    800E0, 8E910): the NON_MATCHING build defines them in C and puts them
    back at their original addresses (free there, as the text moved), so the
    absolute D_ symbols, the bins' pointer words, the harness and the
    front end all see them where they were.  SIZE is the table's size in
    the ROM; nm_symaudit.py compares those bytes with the matching build.
"""
import argparse
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from names import orig  # noqa: E402  (renamed symbols are classified by their address names)

SKIP_NAME =re.compile(r"^(\.|L[0-9A-F]{8}|jtbl_|D_|_asmpp_|_binary_|.*_(START|END|VRAM|bin|SIZE)$)")


def code_symbols(path):
    """name -> address of the code symbols of an ELF: symbols defined in an
    executable section (not data labels inside text bins, local labels or
    segment markers)."""
    from elftools.elf.elffile import ELFFile
    from elftools.elf.sections import SymbolTableSection
    out = {}
    with open(path, "rb") as f:
        e = ELFFile(f)
        secs = list(e.iter_sections())
        for st in secs:
            if not isinstance(st, SymbolTableSection):
                continue
            for sy in st.iter_symbols():
                shndx = sy["st_shndx"]
                if not sy.name or not isinstance(shndx, int) or SKIP_NAME.match(orig(sy.name)):
                    continue
                if sy["st_info"]["type"] in ("STT_SECTION", "STT_FILE", "STT_OBJECT"):
                    continue
                if not secs[shndx]["sh_flags"] & 4:         # SHF_EXECINSTR
                    continue
                if sy["st_info"]["bind"] == "STB_GLOBAL" or sy.name not in out:
                    out[sy.name] = sy["st_value"]
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("inp")
    ap.add_argument("out")
    ap.add_argument("--build-dir", default="build_nm")
    ap.add_argument("--relocate", action="append", default=[])
    ap.add_argument("--pin-data", action="append", default=[])
    ap.add_argument("--extra-vram")
    ap.add_argument("--extra-glob", action="append", default=[])
    ap.add_argument("--extra-name", default=".nm_extra")
    ap.add_argument("--pin-object", action="append", default=[],
                    help="ADDR:SIZE=OBJ: link OBJ(.data) at ADDR (relative OBJ paths are under --build-dir)")
    ap.add_argument("--provide", action="store_true",
                    help="symbol-assignment file: wrap every assignment in PROVIDE()")
    ap.add_argument("--resolve-elf", help="(--provide) take code symbols' addresses from this ELF")
    args = ap.parse_args()

    text = open(args.inp).read()
    text = re.sub(r"(?<![\w/])build/", args.build_dir + "/", text)

    if args.provide:
        # Functions: `name = value;` -> `PROVIDE(name = value);` so an object's
        # own definition (a function that moved with the relocated text) wins
        # over the absolute one.  Data: kept as a hard assignment, exactly as in
        # the matching link.  Several data symbols are pinned here although a C
        # file also defines them (e.g. hd.c's u64 gPrevGameMode, gGameMode, gNextGameMode sit outside
        # its modelled .bss); a PROVIDE would let the C definition win and move
        # the variable, while the hand asm still uses the raw original address.
        funcs_file = "undefined_funcs" in os.path.basename(args.inp)
        code = code_symbols(args.resolve_elf) if args.resolve_elf else {}

        def wrap(m):
            name, val = m.group(2), m.group(3).strip()
            if name in code:
                val = "0x%08X" % code[name]
            is_func = funcs_file or name in code or orig(name).startswith("func_") or \
                re.match(r"^[A-Za-z_.$][\w.$]*$", val) is not None   # alias of another symbol
            if not is_func:
                return "%s%s = %s;" % (m.group(1), name, val)
            return "%sPROVIDE(%s = %s);" % (m.group(1), name, val)
        text = re.sub(r"^(\s*)([A-Za-z_.$][\w.$]*)\s*=\s*([^;]+);", wrap, text, flags=re.M)
        open(args.out, "w").write(text)
        return

    for r in args.relocate:
        sec, vram = r.split("=")
        pat = re.compile(r"^(\s*)" + re.escape(sec) + r" 0x[0-9A-Fa-f]+ :", re.M)
        text, n = pat.subn(lambda m: "%s%s %s :" % (m.group(1), sec, vram), text)
        if n != 1:
            sys.exit("nm_ldscript: section %s not found exactly once" % sec)

    pins = []
    for p in args.pin_object:
        m = re.match(r"^(0x[0-9A-Fa-f]+):(0x[0-9A-Fa-f]+|\d+)=(.+)$", p)
        if not m:
            sys.exit("nm_ldscript: bad --pin-object %r (want ADDR:SIZE=OBJ)" % p)
        addr, size, obj = int(m.group(1), 16), int(m.group(2), 0), m.group(3)
        if not obj.startswith("/") and not obj.startswith(args.build_dir + "/"):
            obj = args.build_dir + "/" + obj
        pins.append((addr, size, obj))
    pins.sort()
    for (a1, s1, _), (a2, _, o2) in zip(pins, pins[1:]):
        if a1 + s1 > a2:
            sys.exit("nm_ldscript: --pin-object ranges overlap at %s" % o2)
    pins_done = not pins

    def emit_pins(out):
        out.append("    /* NON_MATCHING: C definitions of the tables inside the original .text bins,")
        out.append("       at their original addresses (nm_ldscript.py --pin-object) */")
        # LMA: the pinned data segment's VMA-LMA offset, so the sections after
        # these (the catch-all) keep the LMA they had without them
        dsec = args.pin_data[0] if args.pin_data else None
        for addr, size, obj in pins:
            sec = ".nm_pin_%08X_%X" % (addr, size)
            at = " AT(0x%08X - ADDR(%s) + LOADADDR(%s))" % (addr, dsec, dsec) if dsec else ""
            out.append("    %s 0x%08X :%s SUBALIGN(16)" % (sec, addr, at))
            out.append("    {")
            out.append("        %s(.data);" % obj)
            out.append("    }")
            out.append('    ASSERT(SIZEOF(%s) >= 0x%X, "%s: %s(.data) is smaller than the table")'
                       % (sec, size, sec, obj))
        out.append("")

    lines = text.split("\n")
    out = []
    cur_sec = None
    cur_rom = None
    last_rompos = None
    for line in lines:
        m = re.match(r"\s*__romPos = (0x[0-9A-Fa-f]+);", line)
        if m:
            last_rompos = int(m.group(1), 16)
        m = re.match(r"\s*(\.\w+) 0x[0-9A-Fa-f]+ : AT", line)
        if m:
            cur_sec = m.group(1)
            cur_rom = last_rompos
        m = re.match(r"(\s*)\w+_([0-9A-F]{5,})_bin = \.;", line)
        if m and cur_sec in args.pin_data and cur_rom is not None:
            off = int(m.group(2), 16) - cur_rom
            out.append("%s. = 0x%X;" % (m.group(1), off))
        if not pins_done and re.match(r"\s*/DISCARD/", line):
            emit_pins(out)
            pins_done = True
        if args.extra_vram and re.match(r"\s*/DISCARD/", line):
            globs = args.extra_glob
            sel = lambda secs: " ".join("%s(%s)" % (g, secs) for g in globs)
            xn = args.extra_name
            out.append("    /* NON_MATCHING: sections the matching layout never placed */")
            out.append("    %s %s : SUBALIGN(16)" % (xn, args.extra_vram))
            out.append("    {")
            out.append("        %s_START = .;" % xn[1:])
            out.append("        %s;" % sel(".text"))
            out.append("        %s;" % sel(".rodata .rodata.* .late_rodata"))
            out.append("        %s;" % sel(".data .data.* .sdata"))
            out.append("        %s_END = .;" % xn[1:])
            out.append("    }")
            out.append("    %s_bss (NOLOAD) : SUBALIGN(16)" % xn)
            out.append("    {")
            out.append("        %s;" % sel(".bss .sbss COMMON .scommon"))
            out.append("    }")
            out.append("")
        out.append(line)
    if not pins_done:
        sys.exit("nm_ldscript: no /DISCARD/ section to put the --pin-object sections before")

    open(args.out, "w").write("\n".join(out))


if __name__ == "__main__":
    main()
