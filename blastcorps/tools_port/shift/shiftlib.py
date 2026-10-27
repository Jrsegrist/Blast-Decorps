"""Shared pieces of the shiftable build (tools_port/shift, docs/shiftable.md).

The model
---------
A *unit* is a rigid piece of the RAM image: one input section of the splat
link (an object's .text/.data/.rodata/.bss, or a binary blob), or one stretch
of the unmodelled .bss between two objects' .bss sections.  Nothing inside a
unit moves relative to the rest of that unit; units move relative to each
other when something is inserted, grows or shrinks.

Every RAM unit gets two linker-script labels, __shu_<seg>_<n> (start) and
__she_<seg>_<n> (end), with <n> its index in the segment, so a reference link
of the matching objects and a shifted link of changed objects name the same
unit the same way.  An original address maps to the shifted image through the
unit that contains it (Mapper).  ROM units work the same way (romlayout.py).
"""
import bisect
import os
import re
import struct
import sys

from elftools.elf.elffile import ELFFile
from elftools.elf.relocation import RelocationSection
from elftools.elf.sections import SymbolTableSection

VERSION = "us.v11"

# The three RAM segments: splat linker scripts, output sections, the matching
# build's .bss linker script, and where each segment's .bss (as the game uses
# it, not as splat models it) ends.  See docs/shiftable.md "Memory map".
SEGMENTS = {
    "init": dict(ld="init.%s.ld", bss_ld=None, text=".init", data=None,
                 # gInflateHuftArena (0x802229E0) .. __osHwIntTable (0x80224D00) + its 0x40 bytes
                 bss_start=0x802229E0, bss_end=0x80224D40),
    "hd_code": dict(ld="hd_code.%s.ld", bss_ld="hd_code_bss.%s.ld", text=".hd_code", data=".hd_code_data",
                    # the data ends at 0x8030F660; gStaticSegmentBuf (0x803FF600) holds the 0xC0-byte static
                    # segment (romStaticSegment..End), the last thing the game keeps there
                    bss_start=None, bss_end=0x803FF6C0),
    "hd_front_end": dict(ld="hd_front_end.%s.ld", bss_ld="hd_front_end_bss.%s.ld", text=".hd_front_end",
                         data=".hd_front_end_data",
                         # ul_pfsisplug's .bss (0x8021ABF0..0x8021AC30) is the last thing it uses
                         bss_start=None, bss_end=0x8021AC30),
}
SEGTAG = {"init": "init", "hd_code": "hd", "hd_front_end": "fe"}

# Fixed points of the RAM map that are not part of any segment's image ("pins").
# A shifted build must keep every segment inside its window.
PINS = {
    "fe_vram": 0x801E7000,        # hd_load_front_end inflates the front end here (46C20.c), init's inflate window
    "init_vram": 0x8021ED00,      # the ROM header's entry point; after boot, the ROM-load staging buffer
    "hd_vram": 0x802447C0,        # init inflates hd_code here (overridable: --hd-vram)
    "fe_rom_slot": 0x803FFFF8,    # init stores the front end's ROM start/end at 0x803FFFF8/C
    "ram_end": 0x80400000,
}


def die(msg):
    sys.exit("shift: " + msg)


def be32(b, off=0):
    return struct.unpack_from(">I", b, off)[0]


# --------------------------------------------------------------------------- ELF


class Elf:
    """Sections, symbols and (for ld -q output) relocations of an ELF."""

    def __init__(self, path, relocs=False):
        self.path = path
        with open(path, "rb") as f:
            e = ELFFile(f)
            secs = list(e.iter_sections())
            self.sections = {}
            for s in secs:
                if s["sh_flags"] & 2 and s["sh_size"]:   # SHF_ALLOC
                    data = None if s["sh_type"] == "SHT_NOBITS" else s.data()
                    self.sections[s.name] = dict(addr=s["sh_addr"], size=s["sh_size"], offset=s["sh_offset"],
                                                 data=data, flags=s["sh_flags"])
            self.syms = {}
            self.symlist = []
            for st in secs:
                if not isinstance(st, SymbolTableSection):
                    continue
                for sy in st.iter_symbols():
                    if not sy.name:
                        continue
                    shndx = sy["st_shndx"]
                    sec = secs[shndx].name if isinstance(shndx, int) else shndx
                    rec = dict(value=sy["st_value"], size=sy["st_size"], sec=sec,
                               type=sy["st_info"]["type"], bind=sy["st_info"]["bind"])
                    self.symlist.append((sy.name, rec))
                    if sy.name not in self.syms or rec["bind"] == "STB_GLOBAL":
                        self.syms[sy.name] = rec
            self.relocs = {}
            if relocs:
                for s in secs:
                    if not isinstance(s, RelocationSection):
                        continue
                    symtab = secs[s["sh_link"]]
                    for r in s.iter_relocations():
                        sym = symtab.get_symbol(r["r_info_sym"])
                        # executables: r_offset is the virtual address
                        self.relocs.setdefault(r["r_offset"], []).append((r["r_info_type"], sym.name, sym["st_value"]))

    def value(self, name):
        s = self.syms.get(name)
        return None if s is None else s["value"]

    def read(self, addr, n):
        for s in self.sections.values():
            if s["data"] is not None and s["addr"] <= addr and addr + n <= s["addr"] + s["size"]:
                o = addr - s["addr"]
                return s["data"][o:o + n]
        return None

    def file_offset(self, addr):
        for s in self.sections.values():
            if s["data"] is not None and s["addr"] <= addr < s["addr"] + s["size"]:
                return s["offset"] + addr - s["addr"]
        return None

    def section_of(self, addr):
        for n, s in self.sections.items():
            if s["addr"] <= addr < s["addr"] + s["size"]:
                return n
        return None


# ------------------------------------------------------------- splat ld scripts


class OutSec:
    def __init__(self, name, vram):
        self.name = name
        self.vram = vram
        self.entries = []       # ("obj", path, secname) | ("label", name)


def parse_splat_ld(path):
    """The output sections of a splat linker script, in order."""
    out = []
    cur = None
    for line in open(path):
        m = re.match(r"\s*(\.\w+) (0x[0-9A-Fa-f]+) : AT\(", line)
        if m:
            cur = OutSec(m.group(1), int(m.group(2), 16))
            out.append(cur)
            continue
        if cur is None:
            continue
        if re.match(r"\s*}", line):
            cur = None
            continue
        m = re.match(r"\s*(\S+\.o)\((\.\w+)\);", line)
        if m:
            cur.entries.append(("obj", m.group(1), m.group(2)))
            continue
        m = re.match(r"\s*([A-Za-z_]\w*) = \.;", line)
        if m:
            cur.entries.append(("label", m.group(1)))
    return out


def parse_bss_ld(path):
    """[(addr, objpath)] of a matching .bss linker script."""
    out = []
    if path is None or not os.path.exists(path):
        return out
    for line in open(path):
        m = re.match(r"\s*\.\w+ (0x[0-9A-Fa-f]+) \(NOLOAD\) : \{ (\S+\.o)\(\.bss\); \}", line)
        if m:
            out.append((int(m.group(1), 16), m.group(2)))
    return sorted(out)


def parse_symfile(path):
    """[(name, value-or-alias-string, is_provide)] of an undefined_*.txt file."""
    out = []
    if not os.path.exists(path):
        return out
    for line in open(path):
        line = re.sub(r"/\*.*?\*/", "", line)
        m = re.match(r"\s*(PROVIDE\()?\s*([A-Za-z_.$][\w.$]*)\s*=\s*([^;)]+)\)?\s*;", line)
        if not m:
            continue
        v = m.group(3).strip()
        try:
            v = int(v, 0)
        except ValueError:
            pass
        out.append((m.group(2), v, bool(m.group(1))))
    return out


def symfiles(seg):
    names = ["undefined_syms_auto", "undefined_funcs_auto"]
    if seg == "hd_front_end":
        names.append("undefined_syms_hd_code_auto")
    names.append("undefined_syms")
    return ["%s.%s.%s.txt" % (n, seg, VERSION) for n in names]


# ------------------------------------------------------------------------ units


class Unit:
    __slots__ = ("seg", "idx", "kind", "desc", "osec", "start", "size", "align")

    def __init__(self, seg, idx, kind, desc, osec, start=None, size=None, align=16):
        self.seg, self.idx, self.kind, self.desc, self.osec = seg, idx, kind, desc, osec
        self.start, self.size, self.align = start, size, align

    @property
    def label(self):
        return "__shu_%s_%d" % (SEGTAG[self.seg], self.idx)

    @property
    def end_label(self):
        return "__she_%s_%d" % (SEGTAG[self.seg], self.idx)

    def __repr__(self):
        return "<%s %s %s %s>" % (self.label, self.kind, self.desc,
                                  "" if self.start is None else "%08X+%X" % (self.start, self.size))


def lowbit_align(addr, cap=64):
    a = 1
    while a < cap and not addr & a:
        a <<= 1
    return a


def bss_model(seg, ref_elf, bss_objs, data_end):
    """The .bss of a segment as units: the objects' own .bss sections (at their
    matching addresses) and the unmodelled stretches between them."""
    cfg = SEGMENTS[seg]
    start = cfg["bss_start"] or data_end
    end = cfg["bss_end"]
    items = []
    pos = start
    for addr, obj in bss_objs:
        sec = [s for n, s in ref_elf.sections.items() if s["addr"] == addr and s["data"] is None]
        if not sec:
            die("%s: no .bss section at %08X in %s" % (seg, addr, ref_elf.path))
        if addr < pos:
            die("%s: .bss of %s at %08X overlaps the previous one" % (seg, obj, addr))
        if addr > pos:
            items.append(("gap", pos, addr - pos))
        items.append(("obj", obj, addr, sec[0]["size"]))
        pos = addr + sec[0]["size"]
    if end > pos:
        items.append(("gap", pos, end - pos))
    return items


# ----------------------------------------------------------------------- mapper


class Mapper:
    """Original address -> shifted address, through the units.

    ref_units / new_units: {label: (start, size)} for one address space.
    sec_starts: [(orig_start, new_start)] for output-section starts, which win
    over the unit they begin (a reference to a section's start means the
    section, e.g. hd_code's VRAM, even when a pad now sits before its first
    unit)."""

    def __init__(self, ref_units, new_units, sec_starts=()):
        self.items = sorted((s, s + n, lab) for lab, (s, n) in ref_units.items() if n > 0)
        self.starts = [x[0] for x in self.items]
        self.new = new_units
        self.ref = ref_units
        self.sec_starts = dict(sec_starts)
        for (a0, a1, l0), (b0, b1, l1) in zip(self.items, self.items[1:]):
            if b0 < a1:
                die("units overlap: %s %08X..%08X and %s %08X" % (l0, a0, a1, l1, b0))

    def unit_of(self, addr, prefer_end=False):
        """(label, offset) of the unit containing addr; prefer_end: an address
        equal to a unit's end belongs to that unit (a one-past-the-end pointer)."""
        i = bisect.bisect_right(self.starts, addr) - 1
        if prefer_end:
            j = bisect.bisect_left(self.starts, addr) - 1
            if j >= 0 and self.items[j][1] == addr:
                return self.items[j][2], addr - self.items[j][0]
        if i >= 0 and addr < self.items[i][1]:
            return self.items[i][2], addr - self.items[i][0]
        if i >= 0 and addr == self.items[i][1]:     # end of the last unit of a run
            return self.items[i][2], addr - self.items[i][0]
        return None, None

    def covers(self, addr):
        return self.unit_of(addr)[0] is not None

    def map(self, addr, prefer_end=False):
        if addr in self.sec_starts and not prefer_end:
            return self.sec_starts[addr]
        lab, off = self.unit_of(addr, prefer_end)
        if lab is None:
            return addr
        ns, nn = self.new[lab]
        rs, rn = self.ref[lab]
        if nn != rn and off != 0 and off != rn:
            raise ValueError("address %08X is inside %s, which changed size (%X -> %X)" % (addr, lab, rn, nn))
        if off == rn:
            return ns + nn
        return ns + off


def units_from_elf(elf, prefix):
    """{label: (start, size)} of the __shu_/__she_ labels in a linked ELF."""
    out = {}
    for name, rec in elf.syms.items():
        if name.startswith("__shu_" + prefix + "_"):
            end = elf.value("__she_" + name[6:])
            out[name] = (rec["value"], end - rec["value"])
    return out


def hi16(v):
    return ((v + 0x8000) >> 16) & 0xFFFF


def lo16(v):
    return v & 0xFFFF
