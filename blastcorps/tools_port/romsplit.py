#!/usr/bin/env python3
"""The ROM's top-level layout, read from the ROM itself.

Two views of one walk over a US Blast Corps ROM (1.0 or 1.1):

  segments(rom, version)   the matching build's top-level split, as splat
                           segments: the header, IPL3, init, the texture
                           table, the texture/sound area, then one bin per
                           gzip member (each running to the next member, so
                           the zeros, the front-end pictures, the object table
                           and the static segment ride along with the member
                           before them), the four code members exactly (their
                           padding a segment of its own) and the 0xFF trailer.
                           `romsplit.py splat` writes these as a splat yaml
                           (the top-level Makefile's extract runs it).
  units(rom, version)      the shiftable build's ROM units
                           (tools_port/shift/romlayout.py): the same, plus
                           every texture-table slot (its packed bytes; the
                           zeros before the next slot a unit of its own), the
                           front-end pictures, the zeros from the last scene's
                           end and from the last building's end, the object
                           table and the static segment.  (hd_code_data's unit
                           keeps its padding; the front end's is a unit.)

and code_layout(), the (start, end) of init and of the four code members
(tools_port/vermap.py).

How the walk finds things: init and the texture table are where the boot code
DMAs them from (0x1000, 0x4CE0); each texture slot is a table entry (offset
from the table, packed size, packing); the files from the title logo on are
found the way romfs.py (bcfiles) walks them: gzip members one after another,
the LZSS front-end pictures, the scenes, the packed-object table and the
buildings it lists, the static segment and the four code members.  Names are
the ones the ROM carries: a gzip member is its FNAME and MTIME
("titlelogo_usa.raw.855417225"; ".DUPE" on a second member with the same
both), a code member is "<FNAME without .raw>.<version>", a unit is its FNAME
without ".raw".  Nothing of the ROM's contents is written out, only offsets
and those names.

    python3 tools_port/romsplit.py splat --version us.v11 --rom ../baserom.us.v11.z64 -o build/blastcorps.us.v11.yaml
    python3 tools_port/romsplit.py units --rom ../baserom.us.v11.z64      (the unit list, for reading)
    python3 tools_port/romsplit.py layout --rom ../baserom.us.v11.z64     (init and the code members)
"""
import argparse
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from bcfiles import gz, romfs, texture  # noqa: E402

ROM_SIZE = 0x800000
INIT = 0x1000                   # IPL3 hands over to init at ROM 0x1000
TEX_TABLE = texture.TEX_TABLE   # init ends where the texture table starts
TEX_AREA = TEX_TABLE + 8 * texture.TEX_ENTRIES
CODE = ("hd_code_text", "hd_code_data", "hd_front_end_text", "hd_front_end_data")
PALETTE_SIZES = {0x100: "lut256", 0x80: "lut128"}   # stored (packing 0) slots of one palette


class Walk:
    """The files of one ROM, in ROM order (offsets only)."""

    def __init__(self, data, version=None):
        self.rom = romfs.Rom(data)
        self.data = self.rom.data
        self.version = version or self.rom.version
        if self.version != self.rom.version:
            sys.exit("romsplit: the ROM is %s, not %s" % (self.rom.version, self.version))
        d = self.data
        r = self.rom
        # gzip members before the front-end pictures (title, models, levels, textures, demos)
        self.chain = [m for f in r.chain if f.kind not in ("lzss", "scene") for m in f.members]
        self.pictures = [f for f in r.chain if f.kind == "lzss"]
        self.scenes = [m for f in r.chain if f.kind == "scene" for m in f.members]
        # the buildings: members one after another from the end of the object table to the last one's end
        self.buildings = []
        p = r.obj_table_rom + 4 * romfs.OBJ_ENTRIES
        while p < r.obj_end:
            m = gz.read_member(d, r._skip_pad(p))
            self.buildings.append(m)
            p = m.end
        if p != r.obj_end:
            sys.exit("romsplit: the buildings end at %06X, the object table says %06X" % (p, r.obj_end))
        self.obj_table = r.obj_table_rom
        self.static = r.static_rom
        self.code = [m for f in r.code_members() for m in f.members]
        if [m.name for m in self.code] != [n + ".raw" for n in CODE]:
            sys.exit("romsplit: the code members are %s" % [m.name for m in self.code])
        end = self.code[-1].end
        self.trailer = max(end, len(d[:ROM_SIZE].rstrip(b"\xff")))
        if any(d[end:self.trailer]):
            sys.exit("romsplit: non-zero bytes between the front end (%06X) and the trailer (%06X)" % (end, self.trailer))

    def members(self):
        """every gzip member before the code, in ROM order"""
        return self.chain + self.scenes + self.buildings


def _named(members, version):
    """splat names of the members: FNAME.MTIME, .DUPE on a repeat"""
    seen = set()
    out = []
    for m in members:
        n = "%s.%d" % (m.name, m.mtime)
        if n in seen:
            n += ".DUPE"
        seen.add(n)
        out.append((m.start, n))
    return out


def segments(data, version=None):
    """[(start, type, name or None)] of the top-level split, ending with (0x800000, None, None)"""
    w = Walk(data, version)
    v = w.version
    segs = [(0, "header", "header"), (0x40, "bin", "boot"), (INIT, "bin", "init." + v),
            (TEX_TABLE, "bin", None), (TEX_AREA, "bin", None)]
    segs += [(s, "bin", n) for s, n in _named(w.members(), v)]
    for i, m in enumerate(w.code):
        segs.append((m.start, "bin", "%s.%s" % (CODE[i], v)))
        nxt = w.code[i + 1].start if i + 1 < len(w.code) else w.trailer
        if m.end < nxt:
            segs.append((m.end, "bin", None))           # zero padding
    segs += [(w.trailer, "bin", "trailer"), (ROM_SIZE, None, None)]
    return segs


def code_layout(data, version=None):
    """{"init" | "hd_code_text" | ...: (start, end)}: each one up to the next segment"""
    segs = segments(data, version)
    v = segs[2][2].split(".", 1)[1]
    out = {}
    for k, (s, t, n) in enumerate(segs):
        if n and n.endswith("." + v):
            out[n[:-len(v) - 1]] = (s, segs[k + 1][0])
    return out


def _tex_units(d, end):
    """the texture slots from TEX_AREA up to `end`: (start, type, name), a gap ("", "") after a slot whose
    packed bytes stop short of the next one.  Where several entries share an offset, the slot is the last
    non-empty one's (packed size, packing): the ones before it have a DMA length of 0 (the next entry is at
    the same offset), whatever size they say."""
    slots = {}
    for e in texture.read_table(d):
        if e.size:
            slots[e.rom] = [e.size, e.type]
    out = []
    starts = sorted(slots)
    if starts[0] != TEX_AREA:
        sys.exit("romsplit: the first texture is at %06X, not %06X" % (starts[0], TEX_AREA))
    for k, s in enumerate(starts):
        size, ptype = slots[s]
        if ptype == 0 and size in PALETTE_SIZES:
            out.append((s, "bin", "%06X.%s" % (s, PALETTE_SIZES[size])))
        else:
            out.append((s, "blast", "%06X.blast%d" % (s, ptype)))
        nxt = starts[k + 1] if k + 1 < len(starts) else end
        if s + size < nxt:
            out.append((s + size, "", ""))
        elif s + size > nxt:
            sys.exit("romsplit: texture slot %06X+%X runs into the next one (%06X)" % (s, size, nxt))
    return out


def units(data, version=None):
    """[(start, type, name)] of the shiftable build's ROM units, ending with (0x800000, "", "")"""
    w = Walk(data, version)
    v = w.version
    d = w.data
    out = [(0, "header", "header"), (0x40, "bin", "boot"), (INIT, "bin", "init." + v), (TEX_TABLE, "bin", "")]
    out += _tex_units(d, w.chain[0].start)

    def rzip(m):
        out.append((m.start, "rzip", m.name.replace(".raw", "")))

    def gap(s, e):
        if s < e:
            out.append((s, "bin", "%06X.bin" % s))
    for m in w.chain:
        rzip(m)
    gap(w.pictures[0].start, w.scenes[0].start)          # the three LZSS front-end pictures
    for m in w.scenes:
        rzip(m)
    gap(w.scenes[-1].end, w.obj_table)
    out.append((w.obj_table, "bin", "objtable"))
    for m in w.buildings:
        rzip(m)
    gap(w.buildings[-1].end, w.static)
    out.append((w.static, "bin", "static_segment"))
    for m in w.code:
        rzip(m)                     # hd_code_data runs to the front end (its padding with it)
    if w.code[-1].end < w.trailer:
        out.append((w.code[-1].end, "bin", ""))          # the front end's padding: an alignment fill
    out += [(w.trailer, "bin", "trailer"), (ROM_SIZE, "", "")]
    return out


def splat_yaml(data, version, base_path="..", rom_path=None):
    """the top-level splat yaml (text)"""
    segs = segments(data, version)
    v = segs[2][2].split(".", 1)[1]
    lines = ["# generated by tools_port/romsplit.py from the ROM: do not edit (make extract writes it)",
             "options:",
             "  base_path: %s" % base_path,
             "  basename: blastcorps.%s" % v,
             "  compiler: IDO",
             "  create_detected_syms: yes",
             "  find_file_boundaries: yes",
             "  target_path: %s" % (rom_path or "baserom.%s.z64" % v)]
    if v != "us.v11":
        # us.v11 splits into asm/ and assets/; another version into its own subdirectories
        lines += ["  asm_path: asm/%s" % v, "  asset_path: assets/%s" % v]
    lines.append("segments:")
    for s, t, n in segs:
        if t == "header" or n == "boot":
            lines += ["  - name: %s" % n, "    type: %s" % t, "    start: 0x%08X" % s]
        elif t is None:
            lines.append("  - [0x%X]" % s)
        elif n is None:
            lines.append("  - [0x%X, %s]" % (s, t))
        else:
            lines.append("  - [0x%X, %s, %s]" % (s, t, n))
    return "\n".join(lines) + "\n"


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("cmd", choices=["splat", "units", "layout"])
    ap.add_argument("--rom", required=True)
    ap.add_argument("--version", help="us.v11 or us.v10 (default: the ROM's)")
    ap.add_argument("--base-path", default="..", help="splat's base_path, from the yaml's directory (default ..)")
    ap.add_argument("-o", "--output")
    a = ap.parse_args()
    data = open(a.rom, "rb").read()
    if a.cmd == "splat":
        text = splat_yaml(data, a.version, a.base_path)
        if a.output:
            os.makedirs(os.path.dirname(os.path.abspath(a.output)), exist_ok=True)
            with open(a.output, "w") as f:
                f.write(text)
        else:
            sys.stdout.write(text)
    elif a.cmd == "units":
        us = units(data, a.version)
        for (s, t, n), (e, _, _) in zip(us, us[1:]):
            print("%06X %06X %-6s %s" % (s, e - s, t, n))
    else:
        for n, (s, e) in code_layout(data, a.version).items():
            print("%-18s %06X-%06X" % (n, s, e))


if __name__ == "__main__":
    main()
