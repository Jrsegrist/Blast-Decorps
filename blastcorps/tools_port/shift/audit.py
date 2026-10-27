#!/usr/bin/env python3
"""What pins addresses in the matching build, and what the shiftable build
does about each (docs/shiftable.md "Audit").

    python3 tools_port/shift/audit.py --baserom ROM [--out build_shift/audit] [--details]

Counts, then (--details) every item with file:line:
  1. absolute symbol assignments (undefined_*.txt): into a moving unit / a ROM
     offset / fixed (hardware, low RAM, pins);
  2. raw references (no relocation in the linked matching ELF): pointer words in
     the binary blobs and in C data, hand-asm lui/lo pairs building RAM
     addresses, ROM offsets in code, the texture table base as an immediate;
  3. raw address constants in the C sources;
  4. linker-script pins (section VMAs, .bss sections at fixed addresses);
  5. ROM tables of offsets (texture table, object table) and sizes taken as
     "next file - this file";
  6. the boot: the header CRCs, init's DMA of hd_code and its front-end slot;
  7. what the port uses.
"""
import argparse
import collections
import glob
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.dirname(HERE))
import shiftbuild as SB  # noqa: E402
import shiftlib as SL  # noqa: E402
import romlayout as RL  # noqa: E402


def asm_lines():
    """VRAM -> (file, line) for every instruction in asm/ (splat comments: /* ROM VRAM WORD */)."""
    out = {}
    for f in glob.glob("asm/**/*.s", recursive=True):
        for i, line in enumerate(open(f, errors="replace"), 1):
            m = re.match(r"/\* [0-9A-F]+ ([0-9A-F]{8}) [0-9A-F]{8} \*/", line)
            if m:
                out[int(m.group(1), 16)] = (f, i)
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--baserom", required=True)
    ap.add_argument("--ref", default="build")
    ap.add_argument("--out", default="build_shift/audit")
    ap.add_argument("--details", action="store_true")
    args = ap.parse_args()
    a = argparse.Namespace(ref=args.ref, out=args.out)
    segs = {s: SB.Segment(s) for s in SB.SEGS}
    SB.reference(a, segs)
    base = open(args.baserom, "rb").read()
    rom_units = RL.rom_units(base)
    for u in rom_units:
        u.data = base[u.start:u.start + u.size]
    sym_over, site_over, lowbits, glue = SB.load_overrides(os.path.join(HERE, "overrides.txt"))
    sites = SB.scan(segs, rom_units, site_over)
    lines = asm_lines()
    rep = []

    def say(s=""):
        rep.append(s)
        print(s)

    # 1. symbol files
    say("1. Absolute symbol assignments (undefined_*.txt)")
    for seg in SB.SEGS:
        s = segs[seg]
        own = SL.Mapper(SB.ref_units(s), SB.ref_units(s))
        others = {o: SL.Mapper(SB.ref_units(segs[o]), SB.ref_units(segs[o])) for o in SB.SEGS if o != seg}
        c = collections.Counter()
        for fn in s.symfiles:
            for name, val, prov in SL.parse_symfile(fn):
                if isinstance(val, str):
                    c["alias"] += 1
                elif val >= 0x80000000 and own.covers(val):
                    c["in a unit of %s" % seg] += 1
                elif val >= 0x80000000 and any(m.covers(val) for m in others.values()):
                    c["in another segment"] += 1
                elif 0x1000 <= val <= 0x800000:
                    c["ROM offset"] += 1
                else:
                    c["fixed (hardware, low RAM, pins, segment addresses)"] += 1
        say("   %-13s %s" % (seg, ", ".join("%d %s" % (n, k) for k, n in sorted(c.items(), key=lambda x: -x[1]))))
    say("   shiftable build: unit-relative (D_x = __shu_hd_N + off) or mapped through the other segment / the ROM layout;")
    say("   fixed ones stay.")
    say()

    # 2. raw references
    say("2. Raw references (no relocation in the matching link)")
    kinds = collections.Counter()
    by_unit = collections.defaultdict(list)
    units_desc = {}
    for seg in SB.SEGS:
        for u in segs[seg].ref["units"]:
            units_desc[u.label] = u.desc
    for st in sites:
        if st.kind == "hir":        # a relocated lui (only checked): its raw low halves are counted
            continue
        desc = units_desc.get(st.unit, st.unit)
        what = ("bin word" if st.note == "bin" else "C data word") if st.kind == "w32" else \
               ("ROM offset in code" if st.space == "rom" else "hand-asm address (lui/lo)")
        if st.kind == "imm16":
            what = "texture table base (16-bit immediate)"
        kinds[(st.seg, what)] += 1
        by_unit[(st.seg, what, desc)].append(st)
    for (seg, what), n in sorted(kinds.items()):
        say("   %-13s %-40s %5d" % (seg, what, n))
    say("   %d in all; shiftable build: patched through the address map after the link (shiftbuild.py "
        "apply_sites), checked by shiftcheck.py" % sum(kinds.values()))
    if args.details:
        for (seg, what, desc), lst in sorted(by_unit.items()):
            src = re.sub(r"^build/|\.o\(.*$", "", desc)
            say("     %s %s %s: %d" % (seg, what, src, len(lst)))
            for st in lst[:400]:
                fl = lines.get(st.addr)
                at = "%s:%d" % fl if fl else "%s+0x%X" % (src, st.addr - [u for u in segs[seg].ref["units"]
                                                                           if u.label == st.unit][0].start)
                say("        %08X %-5s %08X  %s" % (st.addr, st.kind, st.value, at))
    say()

    # 3. C sources
    say("3. Raw address constants in the C sources (outside comments)")
    # addresses in the segments' part of RAM, and ROM offsets that start a ROM unit
    pat = re.compile(r"\b0x([0-9A-Fa-f]{8})\b")
    rom_starts = set(u.start for u in rom_units if u.start >= 0x1000)

    def interesting(v):
        return 0x801E7000 <= v <= 0x80400000 or v in rom_starts
    cnt = collections.Counter()
    for f in sorted(glob.glob("src.%s/**/*.c" % SL.VERSION, recursive=True)):
        txt = open(f, errors="replace").read()
        txt = re.sub(r"/\*.*?\*/", lambda m: "\n" * m.group(0).count("\n"), txt, flags=re.S)
        stack = []      # for each open #if: is this branch NON_MATCHING/PORT_HOST-only?
        for i, line in enumerate(txt.split("\n"), 1):
            line = line.split("//")[0]
            s = line.strip()
            if re.match(r"#\s*if(n?def)?\b", s):
                nm = bool(re.match(r"#\s*ifdef\s+(NON_MATCHING|PORT_HOST)\b|#\s*if\s+defined\(?(NON_MATCHING|PORT_HOST)", s))
                stack.append([nm, bool(re.match(r"#\s*ifndef\s+(NON_MATCHING|PORT_HOST)\b", s))])
                continue
            if re.match(r"#\s*else\b", s) and stack:
                stack[-1] = [stack[-1][1], stack[-1][0]]
                continue
            if re.match(r"#\s*endif\b", s) and stack:
                stack.pop()
                continue
            only_nm = any(x[0] for x in stack)
            for m in pat.finditer(line):
                if not interesting(int(m.group(1), 16)):
                    continue
                cnt["NON_MATCHING / port only" if only_nm else "matching build"] += 1
                if args.details and not only_nm:
                    say("     %s:%d  %s" % (f, i, s[:110]))
    say("   %s" % ", ".join("%d in the %s" % (n, k) for k, n in sorted(cnt.items())))
    say("   the matching build's are pins (the front end's base 0x801E7000 and its 0x37D00-byte window,")
    say("   the load buffer 0x8021ED00, the end of RAM 0x80400000) or debug prints; kept")
    say()

    # 4. linker scripts
    say("4. Linker-script pins")
    n = 0
    for seg in SB.SEGS:
        s = segs[seg]
        for os_ in s.outsecs:
            say("   %-13s %-20s VMA 0x%08X" % (seg, os_.name, os_.vram))
            n += 1
        for addr, obj in s.bss_objs:
            n += 1
        say("   %-13s %d .bss sections at fixed addresses (%s)" % (seg, len(s.bss_objs), s.cfg["bss_ld"] and s.cfg["bss_ld"] % SL.VERSION))
    say("   shiftable build: only the text VMAs stay (init 0x8021ED00: the header's entry point; hd_code "
        "0x802447C0, --hd-vram; front end 0x801E7000); data follows text, .bss follows data")
    say()

    # 5. ROM
    say("5. ROM: %d units (the %s ROM walked by tools_port/romsplit.py)" % (len(rom_units), SL.VERSION))
    say("   texture table 0x4CE0: %d entries (offset from the table, unpacked size, type); object table 0x6EC4C0: %d "
        "offsets" % (RL.TEX_ENTRIES, RL.OBJ_ENTRIES))
    say("   sizes are 'next file - this file' everywhere (lvl_load_file, lvl_load_model, textures, objects)")
    say()

    # 6. boot
    say("6. Boot: IPL3 checks CIC-6102 CRCs over ROM 0x1000..0x101000 (recomputed); init DMAs hd_code's two "
        "gzip members (adjacent) and stores the front end's ROM range at 0x803FFFF8/C")
    say()
    say("7. Port: the port loads the official ROM's files at their original offsets and runs the matching / "
        "NON_MATCHING code at the original addresses (port/inputs); a shifted ROM is not for the port")
    os.makedirs(args.out, exist_ok=True)
    open(os.path.join(args.out, "audit.txt"), "w").write("\n".join(rep) + "\n")


if __name__ == "__main__":
    main()
