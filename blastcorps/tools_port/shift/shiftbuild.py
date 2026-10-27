#!/usr/bin/env python3
"""Build a shiftable Blast Corps ROM (docs/shiftable.md).

    python3 tools_port/shift/shiftbuild.py [--ref build] [--objs build_shift]
        [--out build_shift/rom] [--pads FILE.json] [--baserom ROM] -o OUT.z64

Every address the game has baked in is either a pin (a fixed point of the
memory map or the ROM, kept) or follows the unit it points into.  Steps:

 1. reference link: the matching objects (--ref, normally build/) with
    generated linker scripts (ldgen.py: one labelled unit per input section,
    .bss modelled) and the original absolute symbol files.  Its text and data
    must equal the matching ELFs byte for byte; the units' addresses there are
    the original addresses.
 2. raw references (relocscan.py): everything in the reference image that
    holds an address without a relocation: pointer words in the binary blobs
    and in C data, hand-asm lui/lo pairs, ROM offsets; plus overrides.txt.
 3. shifted links of --objs (normally build_shift/: the same sources, maybe
    edited) with the pads of --pads, the symbol files rewritten relative to the
    units (D_80301234 = __shu_hd_57 + 0x14), cross-segment names resolved
    against the other segments' shifted ELFs; then every raw reference patched
    through the address map.
 4. the ROM (romlayout.py): every ROM file a unit (found by walking --baserom,
    tools_port/romsplit.py); hd_code and the front end
    compressed again (tools_port/gzpack.py); the texture and object tables and
    every ROM offset rewritten; header CRCs (CIC-6102).

With no pads and unchanged objects the output is the original ROM, byte for
byte.  Reports: OUT.map.txt (units: old -> new), OUT.sites.txt (every raw
reference and what it became).
"""
import argparse
import json
import os
import struct
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.dirname(HERE))

import shiftlib as SL  # noqa: E402
from shiftlib import SEGMENTS, SEGTAG, VERSION, Elf, Mapper, die  # noqa: E402
import ldgen  # noqa: E402
import relocscan  # noqa: E402
import romlayout as RL  # noqa: E402

LD = "mips-linux-gnu-ld"
OBJCOPY = "mips-linux-gnu-objcopy"
AS = "mips-linux-gnu-as"
SEGS = ["init", "hd_code", "hd_front_end"]
TEXTSEC = {s: SEGMENTS[s]["text"] for s in SEGS}
ROM_ADDS = []           # RomUnits added to the ROM (their rom_NAME symbols go into every symbol file)
# gzip member names / time stamps of the four compressed images (Makefile GZ_TIMES)
GZ = {"hd_code_text": (".hd_code", "hd_code_text.raw", 855919890),
      "hd_code_data": (".hd_code_data", "hd_code_data.raw", 855919891),
      "hd_front_end_text": (".hd_front_end", "hd_front_end_text.raw", 855919891),
      "hd_front_end_data": (".hd_front_end_data", "hd_front_end_data.raw", 855919891)}


def run(cmd, **kw):
    r = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, **kw)
    if r.returncode:
        sys.stderr.write(r.stdout)
        die("command failed: %s" % " ".join(cmd))
    return r.stdout


def link(ld_path, sym_paths, out_elf):
    cmd = [LD, "-q", "-T", ld_path]
    for s in sym_paths:
        cmd += ["-T", s]
    cmd += ["--no-check-sections", "--no-warn-mismatch", "-Map", out_elf[:-4] + ".map", "-o", out_elf]
    run(cmd)
    return Elf(out_elf, relocs=True)


class Segment:
    def __init__(self, seg):
        self.seg = seg
        self.cfg = SEGMENTS[seg]
        self.tag = SEGTAG[seg]
        self.outsecs = SL.parse_splat_ld(self.cfg["ld"] % VERSION)
        self.bss_objs = SL.parse_bss_ld(self.cfg["bss_ld"] % VERSION if self.cfg["bss_ld"] else None)
        self.symfiles = SL.symfiles(seg)
        self.ref = None         # reference: dict(elf, units, bss)
        self.elf = None         # current shifted ELF
        self.units = None
        self.keep_pads = None   # overrides.txt lowbits: {unit index: bytes of space before it}


# ------------------------------------------------------------------- step 1


def reference(args, segs):
    refdir = os.path.join(args.out, "ref")
    os.makedirs(refdir, exist_ok=True)
    for seg in SEGS:
        s = segs[seg]
        match_elf = Elf(os.path.join(args.ref, "%s.%s.elf" % (seg, VERSION)))
        data_sec = s.cfg["data"] or s.cfg["text"]
        data_end = match_elf.sections[data_sec]["addr"] + match_elf.sections[data_sec]["size"]
        bss = SL.bss_model(seg, match_elf, s.bss_objs, data_end)
        text, units = ldgen.gen_segment_ld(seg, s.outsecs, bss, args.ref,
                                           catch_glob="%s/src.%s/%s/*.o" % (args.ref, VERSION, seg))
        ldp = os.path.join(refdir, "%s.ld" % seg)
        open(ldp, "w").write(text)
        elf = link(ldp, s.symfiles, os.path.join(refdir, "%s.elf" % seg))
        for name in (s.cfg["text"], s.cfg["data"]):
            if name is None:
                continue
            a, b = elf.sections[name], match_elf.sections[name]
            if a["addr"] != b["addr"] or a["data"] != b["data"]:
                die("reference link of %s: %s differs from the matching build (%08X+%X vs %08X+%X)"
                    % (seg, name, a["addr"], a["size"], b["addr"], b["size"]))
        for kind in ("text", "data", "bss"):
            lo, hi = elf.value("__shx_%s_%s" % (s.tag, kind)), elf.value("__shx_%s_%s_end" % (s.tag, kind))
            if lo is not None and hi != lo:
                die("%s: %X bytes of %s the matching layout doesn't place" % (seg, hi - lo, kind))
        for u in units:
            u.start = elf.value(u.label)
            u.size = elf.value(u.end_label) - u.start
        for it in bss:
            if it[0] == "obj":
                u = [x for x in units if x.desc == "%s(.bss)" % it[1]][0]
                if u.start != it[2]:
                    die("reference link of %s: %s at %08X, not %08X" % (seg, u.desc, u.start, it[2]))
        s.ref = dict(elf=elf, units=units, bss=bss)
        print("reference %-13s %4d units  %08X..%08X  (text and data == the matching build)"
              % (seg, len(units), units[0].start, units[-1].start + units[-1].size))


def ref_units(s):
    return {u.label: (u.start, u.size) for u in s.ref["units"]}


# ------------------------------------------------------------------- step 2


def load_overrides(path):
    syms, sites, lowbits, glue = {}, {}, [], {}
    for line in open(path):
        line = line.split("#", 1)[0].strip()
        if not line:
            continue
        p = line.split()
        if p[0] == "sym" and len(p) >= 5 and p[3] == "=":
            syms[(p[1], p[2])] = " ".join(p[4:])
        elif p[0] == "site" and len(p) >= 4:
            sites[(p[1], int(p[2], 16))] = " ".join(p[3:])
        elif p[0] == "lowbits" and len(p) == 4:
            lowbits.append((p[1], p[2], int(p[3])))
        elif p[0] == "glue" and len(p) == 3:
            glue.setdefault(p[1], []).append(p[2])
        else:
            die("overrides.txt: bad line %r" % line)
    return syms, sites, lowbits, glue


def keep_low_units(segs, lowbits):
    """overrides.txt lowbits -> {seg: {unit index: (original start, mask)}}."""
    out = {}
    for seg, name, bits in lowbits:
        s = segs[seg]
        v = s.ref["elf"].value(name)
        if v is None:
            die("overrides.txt lowbits: %s has no symbol %s" % (seg, name))
        m = Mapper(ref_units(s), ref_units(s))
        lab, off = m.unit_of(v)
        u = [x for x in s.ref["units"] if x.label == lab][0]
        mask = (1 << bits) - 1
        old = out.setdefault(seg, {}).get(u.idx)
        out[seg][u.idx] = (u.start, mask | (old[1] if old else 0))
    return out


def scan(segs, rom_units, site_over):
    """All raw references of the reference image -> [Site]."""
    ram_maps = {seg: Mapper(ref_units(segs[seg]), ref_units(segs[seg])) for seg in SEGS}
    rom_starts = set(u.start for u in rom_units if u.start >= 0x1000)

    def ram_target(v):
        return 0x80000000 <= v < 0x80800000 and any(m.covers(v) for m in ram_maps.values())

    def rom_target(v):
        return v in rom_starts

    sites = []
    for seg in SEGS:
        s = segs[seg]
        elf = s.ref["elf"]
        relocd = elf.relocs
        funcs = sorted(r["value"] for n, r in elf.symlist
                       if r["type"] == "STT_FUNC" or (r["sec"] == TEXTSEC[seg] and r["type"] == "STT_NOTYPE"
                                                     and not n.startswith((".", "__sh", "_binary"))))
        for u in s.ref["units"]:
            if u.size == 0 or u.osec.endswith("_bss"):
                continue
            data = elf.read(u.start, u.size)
            is_bin = u.desc.endswith(".bin.o(.data)")
            if u.osec == TEXTSEC[seg] and not is_bin:
                words = struct.unpack(">%dI" % (u.size // 4), data[:u.size & ~3])
                for lui, kind, value, uses in relocscan.scan_text(words, u.start, relocd, funcs,
                                                                   lambda v: ram_target(v) or rom_target(v)):
                    space = "ram" if (uses[0][2] if uses else value) >= 0x80000000 else "rom"
                    sites.append(relocscan.Site(seg, lui, kind, value, space, u.label,
                                                " ".join("%08X" % x[0] for x in uses)))
                    for ua, uk, uv in uses:
                        sites.append(relocscan.Site(seg, ua, uk, uv, space, u.label, "lui %08X" % lui))
                # the texture table's ROM base as a 16-bit immediate
                for k, w in enumerate(words):
                    if w >> 26 in (0x09, 0x0D) and (w >> 21) & 31 == 0 and w & 0xFFFF == RL.TEX_TABLE:
                        sites.append(relocscan.Site(seg, u.start + 4 * k, "imm16", RL.TEX_TABLE, "rom", u.label, ""))
            else:
                for a, w in relocscan.scan_words(data, u.start, relocd, ram_target):
                    sites.append(relocscan.Site(seg, a, "w32", w, "ram", u.label, "bin" if is_bin else "data"))
    # overrides: "ignore" drops a site
    out = []
    for st in sites:
        o = site_over.get((st.seg, st.addr))
        if o == "ignore":
            continue
        out.append(st)
    return out


# ------------------------------------------------------------------- step 3


def make_pads(cfg, outdir):
    """pads.json "ram" -> {seg: {(kind, ordinal): (label, obj, size)}} (assembling pad objects)."""
    res = {}
    os.makedirs(outdir, exist_ok=True)
    for seg, kinds in (cfg.get("ram") or {}).items():
        tag = SEGTAG[seg]
        d = {}
        for kind, lst in kinds.items():
            for ordinal, size in lst:
                if size % 16:
                    die("pad sizes must be multiples of 16 (%s %s %d)" % (seg, kind, size))
                label = "__shpad_%s_%s_%d" % (tag, kind, ordinal if ordinal >= 0 else 9999)
                obj = None
                if kind in ("text", "data"):
                    src = os.path.join(outdir, "%s.s" % label)
                    obj = os.path.join(outdir, "%s.o" % label)
                    with open(src, "w") as f:
                        f.write(".set noreorder\n.set noat\n")
                        if kind == "text":
                            # a dummy function, then break instructions: code that lands here by
                            # mistake traps instead of falling through into the moved code
                            f.write(".section .text\n.globl shift_pad_%s_text_%d\n" % (tag, max(ordinal, 0)))
                            f.write("shift_pad_%s_text_%d:\n    jr $ra\n    nop\n" % (tag, max(ordinal, 0)))
                            f.write("    .fill %d, 4, 0x0000000D\n" % ((size - 8) // 4))
                        else:
                            f.write(".section .data\n.globl shift_pad_%s_data_%d\n" % (tag, max(ordinal, 0)))
                            f.write("shift_pad_%s_data_%d:\n    .fill %d, 4, 0x5AFEC0DE\n" % (tag, max(ordinal, 0), size // 4))
                    run([AS, "-EB", "-march=vr4300", "-mabi=32", "-o", obj, src])
                d[(kind, ordinal)] = (label, obj, size)
        res[seg] = d
    return res


def is_func_name(name, filename):
    from names import orig
    return "undefined_funcs" in filename or orig(name).startswith("func_")


def resolve_expr(expr, segs, rom):
    """overrides.txt expressions: SEG:NAME (a symbol of that segment's current
    shifted ELF), rom:NAME / rom_end:NAME (a ROM unit's new start/end), a
    number, or a sum of those (A + B)."""
    if "+" in expr:
        return sum(resolve_expr(x.strip(), segs, rom) for x in expr.split("+"))
    if ":" in expr:
        kind, name = expr.split(":", 1)
        if kind in SEGS:
            v = segs[kind].elf.value(name) if segs[kind].elf else None
            if v is None:
                v = segs[kind].ref["elf"].value(name)
            if v is None:
                die("override: %s has no symbol %s" % (kind, name))
            return v
        if kind in ("rom", "rom_end"):
            u = rom.by_name(name)
            if u is None:
                die("override: no ROM unit %s" % name)
            return u.new_start + (len(u.data) if kind == "rom_end" else 0)
    return int(expr, 0)


def gen_symfile(seg, segs, maps, rom, sym_over, path):
    """The segment's symbol files, relative to the units."""
    s = segs[seg]
    own = Mapper(ref_units(s), ref_units(s))
    vram = s.ref["units"][0].start
    lines = ["/* generated by tools_port/shift/shiftbuild.py from %s */" % " ".join(s.symfiles)]
    stats = {"unit": 0, "other": 0, "rom": 0, "fixed": 0, "alias": 0, "override": 0}
    for fn in s.symfiles:
        lines.append("/* %s */" % fn)
        for name, val, prov in SL.parse_symfile(fn):
            if (seg, name) in sym_over:
                expr = "0x%08X" % resolve_expr(sym_over[(seg, name)], segs, rom)
                stats["override"] += 1
            elif isinstance(val, str):
                expr = val
                stats["alias"] += 1
            elif val >= 0x80000000:
                lab, off = own.unit_of(val)
                if lab is not None:
                    expr = "ADDR(%s)" % TEXTSEC[seg] if val == vram else "%s + 0x%X" % (lab, off)
                    stats["unit"] += 1
                else:
                    expr = None
                    for o in SEGS:
                        if o == seg or not maps[o].covers(val):
                            continue
                        other = segs[o].elf
                        v = other.value(name) if other is not None else None
                        expr = "0x%08X" % (v if v is not None else maps[o].map(val))
                        stats["other"] += 1
                        break
                    if expr is None:
                        expr = "0x%08X" % val
                        stats["fixed"] += 1
            elif 0x1000 <= val <= 0x800000:
                # every such value in the symbol files is a ROM offset (romChimpRaw, romBootBannerEnd, ...);
                # an *_end / *End one is the end of the file before it
                expr = "0x%08X" % rom.map(val, prefer_end=name.endswith(("_end", "End", "_END")))
                stats["rom"] += 1
            else:
                expr = "0x%08X" % val
                stats["fixed"] += 1
            if prov or is_func_name(name, fn):
                lines.append("PROVIDE(%s = %s);" % (name, expr))
            else:
                lines.append("%s = %s;" % (name, expr))
    lines.append("/* files added to the ROM (--pads rom.add) */")
    for u in ROM_ADDS:
        if u.new_start is not None:
            lines.append("rom_%s = 0x%08X;" % (u.name, u.new_start))
            lines.append("rom_%s_end = 0x%08X;" % (u.name, u.new_start + len(u.data)))
    open(path, "w").write("\n".join(lines) + "\n")
    return stats


def shifted_link(seg, segs, args, pads, maps, rom, sym_over, tag=""):
    s = segs[seg]
    d = os.path.join(args.out, "shift")
    os.makedirs(d, exist_ok=True)
    vram = None
    if seg == "hd_code" and args.hd_vram:
        vram = args.hd_vram
    want = args.keep_low.get(seg, {})          # {unit index: (original start, mask)}
    keep = s.keep_pads if s.keep_pads is not None else dict((i, 0) for i in want)
    for attempt in range(8):
        text, units = ldgen.gen_segment_ld(seg, s.outsecs, s.ref["bss"], args.objs, pads.get(seg), vram=vram,
                                           catch_glob="%s/src.%s/%s/*.o" % (args.objs, VERSION, seg),
                                           keep_low=keep, glued=args.glue.get(seg, ()))
        ldp = os.path.join(d, "%s.ld" % seg)
        open(ldp, "w").write(text)
        symp = os.path.join(d, "%s.syms.txt" % seg)
        gen_symfile(seg, segs, maps, rom, sym_over, symp)
        elf = link(ldp, [symp], os.path.join(d, "%s%s.elf" % (seg, tag)))
        for u in units:
            u.start = elf.value(u.label)
            u.size = elf.value(u.end_label) - u.start
        # lowbits: grow the space before the first unit that lost its low address bits, link again
        bad = [(i, ((orig - units[i].start) & mask)) for i, (orig, mask) in sorted(want.items())
               if (units[i].start - orig) & mask]
        if not bad:
            break
        keep[bad[0][0]] += bad[0][1]
    else:
        die("%s: can't keep the low address bits of units %s" % (seg, sorted(want)))
    s.keep_pads = keep
    s.elf = elf
    s.units = units
    newu = {u.label: (u.start, u.size) for u in units}
    sec0 = [(s.ref["units"][0].start, elf.sections[TEXTSEC[seg]]["addr"])]
    maps[seg] = Mapper(ref_units(s), newu, sec0)
    return elf


def target_value(st, segs, maps, rom, site_over):
    o = site_over.get((st.seg, st.addr))
    if o and o not in ("end",):
        return resolve_expr(o, segs, rom)
    prefer_end = o == "end"
    if st.space == "rom":
        return rom.map(st.value, prefer_end)
    for seg in [st.seg] + [x for x in SEGS if x != st.seg]:
        if maps[seg].covers(st.value):
            return maps[seg].map(st.value, prefer_end)
    return st.value


def apply_sites(seg, segs, sites, maps, rom, site_over, report):
    """Patch the raw references of seg's shifted ELF (in the file)."""
    s = segs[seg]
    path = s.elf.path
    buf = bytearray(open(path, "rb").read())
    m = maps[seg]
    luis = {}
    n = 0
    for st in sites:
        if st.seg != seg:
            continue
        new_addr = m.map(st.addr)
        lab, _ = m.unit_of(st.addr)
        rs, rn = m.ref[lab]
        ns, nn = m.new[lab]
        if rn != nn:
            die("%s: raw reference at %08X is in %s, which changed size; can't patch it" % (seg, st.addr, lab))
        off = s.elf.file_offset(new_addr)
        w = SL.be32(buf, off)
        if st.kind == "hir":            # the linker's %hi: check its raw low halves against it (below)
            luis[st.addr] = ("hi", w & 0xFFFF)
            report.append("%s %08X -> %08X %-5s %08X -> %08X %s %s" % (seg, st.addr, new_addr, st.kind, st.value,
                                                                      (w & 0xFFFF) << 16, st.unit, st.note))
            continue
        t = target_value(st, segs, maps, rom, site_over)
        if st.kind == "w32":
            nw = t
        elif st.kind == "lo":
            nw = (w & 0xFFFF0000) | SL.lo16(t)
        elif st.kind == "loo":
            nw = (w & 0xFFFF0000) | (t & 0xFFFF)
        elif st.kind == "hi":
            nw = (w & 0xFFFF0000) | SL.hi16(t)
            luis[st.addr] = ("hi", nw & 0xFFFF)
        elif st.kind == "hio":
            nw = (w & 0xFFFF0000) | (t >> 16)
            luis[st.addr] = ("hio", nw & 0xFFFF)
        elif st.kind == "imm16":
            if t > 0x7FFF:
                die("%s %08X: ROM offset %X doesn't fit the 16-bit immediate" % (seg, st.addr, t))
            nw = (w & 0xFFFF0000) | t
        else:
            die("bad site kind %s" % st.kind)
        struct.pack_into(">I", buf, off, nw)
        if nw != w:
            n += 1
        report.append("%s %08X -> %08X %-5s %08X -> %08X %s %s" % (seg, st.addr, new_addr, st.kind, st.value, t,
                                                                  st.unit, st.note))
    # every lo half must agree with its lui
    for st in sites:
        if st.seg != seg or st.kind not in ("lo", "loo") or not st.note.startswith("lui "):
            continue
        lui = int(st.note[4:], 16)
        if lui not in luis:
            continue
        hk, hv = luis[lui]
        t = target_value(st, segs, maps, rom, site_over)
        want = SL.hi16(t) if st.kind == "lo" else t >> 16
        if want != hv:
            die("%s: the hand asm at %08X shares one lui (%08X) between targets that are now in different "
                "64 KB pages (%08X); it can't move this far without an edit" % (seg, st.addr, lui, t))
    open(path, "wb").write(buf)
    s.elf = Elf(path, relocs=True)
    return n


# ------------------------------------------------------------------- step 4


def compress(elf, name, outdir, gzpack):
    sec, raw_name, ts = GZ[name]
    binp = os.path.join(outdir, "%s.bin" % name)
    gzp = os.path.join(outdir, "%s.gz" % name)
    run([OBJCOPY, "-O", "binary", "--only-section", sec, elf.path, binp])
    run([sys.executable, gzpack, binp, gzp, "--name", raw_name, "--time", str(ts), "--level", "6"])
    return open(gzp, "rb").read()


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--ref", default="build", help="matching build dir (objects and ELFs)")
    ap.add_argument("--objs", default="build_shift", help="objects to link (same files as build/)")
    ap.add_argument("--out", default="build_shift/rom", help="work dir")
    ap.add_argument("--baserom", default="../baserom.%s.z64" % VERSION)
    ap.add_argument("--overrides", default=os.path.join(HERE, "overrides.txt"))
    ap.add_argument("--gzpack", default=os.path.join(os.path.dirname(HERE), "gzpack.py"))
    ap.add_argument("--pads", help="JSON pad configuration (tools_port/shift/pads_test.json)")
    ap.add_argument("--hd-vram", help="hd_code's VRAM (default: 0x802447C0)")
    ap.add_argument("--stage", default="all", choices=["ref", "scan", "all"])
    ap.add_argument("-o", "--output", default="build_shift/blastcorps.shift.%s.z64" % VERSION)
    args = ap.parse_args()

    segs = {s: Segment(s) for s in SEGS}
    reference(args, segs)
    if args.stage == "ref":
        return
    base = open(args.baserom, "rb").read()
    rom_units = RL.rom_units(base)
    for u in rom_units:
        u.data = base[u.start:u.start + u.size]
    RL.mark_fills(rom_units, base)
    sym_over, site_over, lowbits, args.glue = load_overrides(args.overrides)
    args.keep_low = keep_low_units(segs, lowbits)
    sites = scan(segs, rom_units, site_over)
    if args.stage == "scan":
        for st in sites:
            print(st)
        return

    cfg = json.load(open(args.pads)) if args.pads else {}
    if args.hd_vram is None and cfg.get("hd_vram"):
        args.hd_vram = cfg["hd_vram"]
    pads = make_pads(cfg, os.path.join(args.out, "pads"))
    maps = {seg: Mapper(ref_units(segs[seg]), ref_units(segs[seg]), [(segs[seg].ref["units"][0].start,) * 2])
            for seg in SEGS}

    # ROM pads / added / dropped files
    rcfg = cfg.get("rom") or {}
    rom_pads, rom_adds = [], []
    for before, size in rcfg.get("pads", []):
        u = RL.RomUnit(None, "pad", "pad_%X" % int(before, 16))
        u.data = b"\xEE" * size
        u.align = 16
        rom_pads.append((int(before, 16), u))
    for a in rcfg.get("add", []):
        # a new ROM file: the bytes of "path", or "size" bytes of a test pattern; the
        # symbols rom_NAME / rom_NAME_end in every segment give its place
        u = RL.RomUnit(None, "bin", a["name"])
        if a.get("path"):
            u.data = open(a["path"], "rb").read()
        else:
            u.data = bytes((i * 7 + 3) & 0xFF for i in range(a["size"]))
        u.align = 16
        rom_adds.append((int(a["before"], 16), u))
    ROM_ADDS[:] = [u for _, u in rom_adds]
    drops = set(int(x, 16) for x in rcfg.get("drop", []))
    # replaced files ("rom": {"replace": [{"at": ..., "path": ..., "empty": [...]}]}, written by
    # bcfiles build for an edited file that outgrew its place): the unit at "at" takes the
    # file's bytes; the units in "empty" (a gzip pair's second member, now inside the
    # file) become empty and unaligned, so nothing comes between the two members
    for r in rcfg.get("replace", []):
        at = int(r["at"], 16)
        u = next((x for x in rom_units if x.start == at), None)
        if u is None or u.dyn:
            die("rom.replace: no replaceable ROM unit starts at %06X" % at)
        u.data = open(r["path"], "rb").read()
        for e in r.get("empty", []):
            v = next((x for x in rom_units if x.start == int(e, 16)), None)
            if v is None:
                die("rom.replace: no ROM unit starts at %s" % e)
            v.data = b""
            v.align = 1
    # in-place writes ("rom": {"patch": [{"at": ..., "path": ... or "hex": ..., "texture": id}]}, written by bcfiles
    # mod-export / build next to a replaced file: the files that fit and a mod's texture slots, the bytes the
    # in-place write gives): over units that keep their size, before they are placed
    RL.apply_patches(rom_units, RL.load_patches(rcfg), base)

    rom = RL.RomMapper(rom_units)
    # 3a: init's size (it doesn't depend on any symbol value) -> the ROM files' new places
    shifted_link("init", segs, args, pads, maps, rom, sym_over, ".size")
    init_unit = rom.by_name("init.us.v11")
    init_size = segs["init"].elf.sections[".init"]["size"]
    init_unit.data = bytes(init_size)
    seq, end = RL.place(rom_units, rom_pads, drops, rom_adds)
    # 3b: hd_code, the front end, hd_code again (now with the front end's addresses)
    shifted_link("hd_code", segs, args, pads, maps, rom, sym_over, ".1")
    hd1 = {u.label: (u.start, u.size) for u in segs["hd_code"].units}
    shifted_link("hd_front_end", segs, args, pads, maps, rom, sym_over)
    shifted_link("hd_code", segs, args, pads, maps, rom, sym_over)
    if {u.label: (u.start, u.size) for u in segs["hd_code"].units} != hd1:
        die("hd_code's layout changed between its two links")
    shifted_link("hd_front_end", segs, args, pads, maps, rom, sym_over)
    report = []
    npatch = {}
    for seg in ("hd_code", "hd_front_end"):
        npatch[seg] = apply_sites(seg, segs, sites, maps, rom, site_over, report)
    # 4: compress, final ROM layout, init
    gzdir = os.path.join(args.out, "gz")
    os.makedirs(gzdir, exist_ok=True)
    for name in GZ:
        seg = "hd_code" if name.startswith("hd_code") else "hd_front_end"
        rom.by_name(name).data = compress(segs[seg].elf, name, gzdir, args.gzpack)
    seq, end = RL.place(rom_units, rom_pads, drops, rom_adds)
    shifted_link("init", segs, args, pads, maps, rom, sym_over)
    npatch["init"] = apply_sites("init", segs, sites, maps, rom, site_over, report)
    ie = segs["init"].elf
    if ie.sections[".init"]["size"] != init_size:
        die("init's size changed between its links")
    init_unit.data = ie.sections[".init"]["data"]
    # the self-relative tables
    tex = rom.unit_of(RL.TEX_TABLE)
    tex.data = RL.rewrite_tex_table(tex.data, rom, tex.new_start)
    # new texture slots (bcfiles mod-export: "rom": {"texnew": [...]}, their bytes the added file "texnew")
    if rcfg.get("texnew"):
        nu = next((u for u in ROM_ADDS if u.name == "texnew"), None)
        if nu is None or nu.new_start is None:
            die("rom.texnew: no added file \"texnew\" holds the new slots' bytes")
        tex.data = RL.apply_texnew(tex.data, base, nu.new_start, tex.new_start, rcfg["texnew"])
    obj = rom.unit_of(RL.OBJ_TABLE)
    obj.data = RL.rewrite_obj_table(obj.data, rom, obj.new_start)
    image = RL.write_rom(seq, end)
    # the ROM map for the Windows port (romlayout.py): only when a file moved or changed size
    map_runs = RL.map_runs(rom_units)
    if not RL.map_needed(map_runs):
        map_runs = None
    else:
        ram_moved = any((u.start, u.size) != (segs[seg].ref["units"][u.idx].start, segs[seg].ref["units"][u.idx].size)
                        for seg in SEGS for u in segs[seg].units)
        blob = RL.map_blob(map_runs, RL.MAP_RAM_MOVED if ram_moved else 0)
        at = (end + 15) & ~15
        image = RL.write_rom(seq, at + len(blob) + 16)
        RL.put_map(image, at, blob)
    RL.fix_crc(image)
    open(args.output, "wb").write(image)

    # pins and reports
    hd = segs["hd_code"].elf
    hd_end = hd.value("__sh_hd_end")
    fe_end = segs["hd_front_end"].elf.value("__sh_fe_end")
    init_end = ie.value("__sh_init_end")
    checks = [("front end inside 0x801E7000..0x8021ED00", segs["hd_front_end"].units[0].start >= SL.PINS["fe_vram"]
               and fe_end <= SL.PINS["init_vram"]),
              ("init (with .bss) below hd_code", init_end <= hd.sections[".hd_code"]["addr"]),
              ("hd_code (with .bss) below 0x803FFFF8", hd_end <= SL.PINS["fe_rom_slot"])]
    for what, ok in checks:
        if not ok:
            die("memory map: %s fails (init end %08X, hd_code end %08X, front end end %08X)"
                % (what, init_end, hd_end, fe_end))
    staging = hd.sections[".hd_code"]["addr"] - SL.PINS["init_vram"]
    big = max(len(u.data) for u in seq if u.type == "rzip" and not u.dyn)
    if big > staging:
        die("the largest compressed file (%X bytes) doesn't fit the load buffer below hd_code (%X)" % (big, staging))
    with open(args.output + ".map.txt", "w") as f:
        for seg in SEGS:
            s = segs[seg]
            for u in s.units:
                r = s.ref["units"][u.idx]
                f.write("%-14s %-10s %08X+%-6X -> %08X+%-6X %s\n" % (seg, u.label, r.start, r.size, u.start, u.size, u.desc))
        for u in seq:
            if u.name == "trailer":
                f.write("rom %-10s %6s -> %06X +%X %s\n" % (u.type, "%06X" % u.start, u.new_start,
                                                          len(image) - u.new_start, u.name))
                continue
            f.write("rom %-10s %6s -> %06X +%X %s\n" % (u.type or "-", "%06X" % u.start if u.start is not None else "new",
                                                      u.new_start, 0 if u.fill else len(u.data), u.name or "-"))
        for r in map_runs or ():
            f.write("map %06X..%06X -> %06X..%06X\n" % r)
    open(args.output + ".sites.txt", "w").write("\n".join(report) + "\n")
    print("shift ROM %s: %X bytes; patched %s raw references (%d sites); RAM ends: init %08X hd_code %08X "
          "front end %08X; load buffer %X (largest file %X); %s"
          % (args.output, len(image), npatch, len(sites), init_end, hd_end, fe_end, staging, big,
             "ROM map for the port: %d runs" % len(map_runs) if map_runs else "no file moved (no ROM map)"))


if __name__ == "__main__":
    main()
