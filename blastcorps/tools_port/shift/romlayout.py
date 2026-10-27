"""The ROM as units, laid out again.

The units come from the original ROM itself (rom_units: tools_port/romsplit.py
walks it): the header, IPL3, init, the texture table, each texture slot and
the zeros after it, the sound area, each compressed file, the front-end
pictures, the object table (lvl_load_object's table at 0x6EC4C0: 0x200
offsets from there), the static segment (romStaticSegment: hd_mode_setup
DMAs 0xC0 bytes from it), the code, the padding and the trailer.  The second
copies of cohut1 and cohut1_dl (object-table entry 241) are units of their
own, named like the first.  Units are placed in order, each at the next
address with its original alignment (the lowest set bit of its original start,
at most 16), so inserting, growing or dropping a unit moves the ones after it
and nothing else.

Dynamic units take built bytes (init, hd_code's and the front end's
compressed text and data); the two self-relative tables are rewritten
(the texture table at 0x4CE0, offsets from itself, 8-byte entries; the object
table at 0x6EC4C0, offsets from itself, words); the trailer (0xFF) fills to
the end; the header CRCs are recomputed (CIC-6102).
"""
import os
import struct
import sys

from shiftlib import VERSION, die

TEX_TABLE = 0x4CE0
TEX_ENTRIES = 0x1000
OBJ_TABLE = 0x6EC4C0
OBJ_ENTRIES = 0x200
DYNAMIC = {"init.us.v11": "init", "hd_code_text": "hd_code_text", "hd_code_data": "hd_code_data",
           "hd_front_end_text": "hd_front_end_text", "hd_front_end_data": "hd_front_end_data"}
ROM_LIMIT = 0xFFB000            # hd_boot reads a debug command line from ROM 0xFFB000
# second gzip members: inflated straight after the first one (init's boot for
# hd_code, hd_load_front_end for the front end), so no gap may separate them
STREAM_NEXT = ("hd_code_data", "hd_front_end_data")


class RomUnit:
    __slots__ = ("start", "size", "type", "name", "align", "data", "new_start", "dyn", "fill")

    def __init__(self, start, typ, name):
        self.start, self.type, self.name = start, typ, name
        self.size = None
        self.align = 16 if start is None else 1
        while self.align < 16 and not start & self.align:
            self.align <<= 1
        if name in STREAM_NEXT:
            self.align = 1
        self.data = None
        self.new_start = None
        self.dyn = DYNAMIC.get(name)
        self.fill = False       # alignment padding: no bytes of its own, the next unit's alignment makes it

    @property
    def label(self):
        return "rom_%06X" % self.start if self.start is not None else "rom_new_%s" % self.name


def rom_units(base, version=VERSION):
    """[RomUnit] in ROM order, with sizes, of the original ROM `base` (its bytes; romsplit.units).  Types:
    "header", "bin" (named: a file; unnamed: padding), "blast" (a texture slot's packed bytes), "rzip" (a gzip
    member), "" (the zeros between texture slots, the sound area)."""
    tp = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    if tp not in sys.path:
        sys.path.insert(0, tp)
    import romsplit
    us = romsplit.units(base, version)
    out = []
    for (a, typ, name), (b, _, _) in zip(us, us[1:]):
        u = RomUnit(a, typ, name)
        u.size = b - a
        out.append(u)
    if us[-1][0] != 0x800000:
        die("the ROM's units don't end at 0x800000")
    return out


def mark_fills(units, base):
    """Gaps that only align the next unit (all zero, shorter than its
    alignment) become fills, so they grow or shrink with what's before them."""
    for u, nxt in zip(units, units[1:]):
        if u.type in ("", "bin") and (u.type == "" or not u.name) and u.size < nxt.align \
                and not any(base[u.start:u.start + u.size]):
            u.fill = True


class RomMapper:
    def __init__(self, units):
        self.units = [u for u in units if u.start is not None]
        self.starts = [u.start for u in self.units]

    def unit_of(self, rom):
        import bisect
        i = bisect.bisect_right(self.starts, rom) - 1
        if i < 0:
            return None
        u = self.units[i]
        if rom < u.start + u.size or (rom == u.start + u.size and i == len(self.units) - 1):
            return u
        return None

    def map(self, rom, prefer_end=False):
        u = self.unit_of(rom)
        if prefer_end:
            import bisect
            j = bisect.bisect_left(self.starts, rom) - 1
            if j >= 0 and self.units[j].start + self.units[j].size == rom:
                p = self.units[j]
                return p.new_start + len(p.data)
        if u is None:
            die("ROM offset %06X is in no unit" % rom)
        if u.new_start is None:
            die("ROM offset %06X is in %s (%06X), which this build drops" % (rom, u.name, u.start))
        return u.new_start + rom - u.start

    def by_name(self, name):
        for u in self.units:
            if u.name == name:
                return u
        return None


def place(units, pads=(), drops=(), adds=()):
    """Assign new_start to every unit (data must be set).  pads/adds:
    [(before_start, RomUnit)] inserted before the unit that starts at
    before_start; drops: unit starts to leave out."""
    seq = []
    ins = {}
    for before, u in list(pads) + list(adds):
        ins.setdefault(before, []).append(u)
    for u in units:
        for x in ins.get(u.start, []):
            seq.append(x)
        if u.start in drops:
            u.new_start = None
            continue
        seq.append(u)
    pos = 0
    for u in seq:
        if u.fill:
            u.new_start = pos
            continue
        a = u.align
        if pos % a:
            pos += a - pos % a
        u.new_start = pos
        if u.name == "trailer":
            continue
        pos += len(u.data)
    return seq, pos


def write_rom(seq, end, fill_trailer=0xFF):
    size = 0x800000
    while size < end:
        size += 0x100000
    if size > ROM_LIMIT:
        die("ROM too big (%X bytes): hd_boot reads its debug command line at %X" % (end, ROM_LIMIT))
    rom = bytearray(b"\xFF" * size) if fill_trailer == 0xFF else bytearray(size)
    for u in seq:
        if u.name == "trailer" or u.fill:
            continue
        rom[u.new_start:u.new_start + len(u.data)] = u.data
    # zero the alignment gaps (between units) up to the trailer
    prev_end = 0
    for u in seq:
        if u.fill:
            continue
        if u.new_start > prev_end:
            rom[prev_end:u.new_start] = bytes(u.new_start - prev_end)
        prev_end = u.new_start + len(u.data)
    return rom


def tex_slot(base, i):
    """(ROM offset, room) of texture-table entry i in the original ROM (room: up to the next entry's offset, the
    game's DMA length)"""
    off = struct.unpack_from(">I", base, TEX_TABLE + 8 * i)[0]
    nxt = struct.unpack_from(">I", base, TEX_TABLE + 8 * (i + 1))[0] if i + 1 < TEX_ENTRIES else off
    return TEX_TABLE + off, nxt - off


def load_patches(rcfg):
    """the "patch" entries of a pads file's "rom" part: [(offset, bytes, what, texture id or None)]
    ({"at": "0x...", "path": file or "hex": "...", "what": text, "texture": id})"""
    out = []
    for p in rcfg.get("patch", []):
        if p.get("path"):
            with open(p["path"], "rb") as f:
                b = f.read()
        else:
            b = bytes.fromhex(p["hex"])
        out.append((int(p["at"], 16), b, p.get("what", p["at"]), p.get("texture")))
    return out


def apply_patches(units, patches, base=None):
    """In-place writes ("rom": {"patch": [...]}, bcfiles mod-export / build for the files that fit and the texture
    slots): [(original offset, bytes, what, texture id or None)].  The bytes go over the units' own bytes at their
    original offsets and no unit changes size, so each patch moves with its unit; a patch may span units (a level
    file's two members and the zeros to its slot's end) but not touch the code's units, a replaced or emptied one,
    or put anything but zeros in an alignment gap.  A texture slot's patch must be exactly the slot's room in this
    ROM (base): a slot can't grow - bytes after a texture would be read as part of it (docs/shiftable.md)."""
    import bisect
    starts = [u.start for u in units]
    for at, b, what, tex in patches:
        end = at + len(b)
        if tex is not None and base is not None and at != TEX_TABLE + 8 * tex + 4:
            rom, room = tex_slot(base, tex)
            if (at, len(b)) != (rom, room):
                die("rom.patch %s: texture %d is at %06X with room %X in this ROM, the patch is %06X+%X (made for "
                    "another ROM?)" % (what, tex, rom, room, at, len(b)))
        if not b or at < 0 or end > 0x800000:
            die("rom.patch %s: %06X+%X is outside the original ROM" % (what, at, len(b)))
        i = bisect.bisect_right(starts, at) - 1
        pos = at
        while pos < end:
            u = units[i]
            n = min(end, u.start + u.size) - pos
            part = b[pos - at:pos - at + n]
            if u.dyn:
                die("rom.patch %s: %06X is in %s, which the build makes from the code" % (what, pos, u.name))
            if u.fill:
                if any(part):
                    die("rom.patch %s: non-zero bytes in the alignment gap at %06X" % (what, pos))
            elif len(u.data) != u.size:
                die("rom.patch %s: %06X is in a unit this build replaces or empties (%06X)" % (what, pos, u.start))
            else:
                d = bytearray(u.data)
                d[pos - u.start:pos - u.start + n] = part
                u.data = bytes(d)
            pos += n
            i += 1


def rewrite_tex_table(data, mapper, table_new):
    out = bytearray(data)
    for i in range(TEX_ENTRIES):
        off, n, t = struct.unpack_from(">IHH", data, 8 * i)
        rom = TEX_TABLE + off
        new = mapper.map(rom)
        struct.pack_into(">I", out, 8 * i, new - table_new)
    return bytes(out)


def tex_entry_empty(base, i):
    """entry i has no bytes in the original ROM (size 0, its DMA length 0) and isn't the last"""
    if not 0 <= i < TEX_ENTRIES - 1:
        return False
    off, size, _ = struct.unpack_from(">IHH", base, TEX_TABLE + 8 * i)
    return size == 0 and struct.unpack_from(">I", base, TEX_TABLE + 8 * (i + 1))[0] == off


def apply_texnew(data, base, unit_start, table_new, entries):
    """new texture slots ("rom": {"texnew": [{"id", "at", "size"}]}, bcfiles mod-export): their bytes are an added
    file (unit_start: its place) at "at" in it; entry id points there (packing 0, the size), and the entry after it
    ends it (its offset: the bytes' end; a new slot there sets its own).  The entries before, at and after must be
    empty in the original ROM (the entry before a texture's ends its DMA).  data: the rewritten table"""
    out = bytearray(data)
    ids = set(int(e["id"]) for e in entries)
    for e in sorted(entries, key=lambda e: int(e["id"])):
        i, at, size = int(e["id"]), int(e["at"]), int(e["size"])
        if not all(tex_entry_empty(base, k) for k in (i - 1, i, i + 1)):
            die("rom.texnew: texture %d isn't an empty table entry with empty ones before and after it" % i)
        off = unit_start + at - table_new
        struct.pack_into(">IHH", out, 8 * i, off, size, 0)
        if i + 1 not in ids:
            struct.pack_into(">I", out, 8 * (i + 1), off + size)
    return bytes(out)


def rewrite_obj_table(data, mapper, table_new):
    out = bytearray(data)
    for i in range(OBJ_ENTRIES):
        off = struct.unpack_from(">I", data, 4 * i)[0]
        rom = OBJ_TABLE + off
        # the last entry is the end of the last building, where the zeros after it
        # (a unit of their own) start: it stays the end of the building
        u = mapper.unit_of(rom)
        new = mapper.map(rom, prefer_end=u is not None and u.type in ("", "bin"))
        struct.pack_into(">I", out, 4 * i, new - table_new)
    return bytes(out)


# The ROM map for the Windows port (port/src/rdram.c rom_map_*): the port runs its own code, whose ROM
# offsets are the original ones, so it finds the files of a shifted ROM through this table.  Runs of
# units that moved by the same amount: (original start, original end, new start, new end of the data).
# Written only when something moved or changed size (an unchanged build stays the original ROM): a
# header in the trailer after the last unit, and a footer in the ROM's last 16 bytes (nothing reads the
# trailer; the header CRCs cover 0x1000..0x101000 only).
MAP_MAGIC = b"BCSHMAP1"
MAP_FOOTER = b"BCSHIFT!"
MAP_RAM_MOVED = 1               # flags: code or data moved in RAM (the port's code isn't the ROM's then)


def map_runs(units):
    runs = []
    for u in units:
        if u.start is None or u.new_start is None:
            continue
        vend = u.start + u.size
        if u.fill or u.name == "trailer":
            if runs and runs[-1][1] == u.start:
                runs[-1][1] = vend
            continue
        d = u.new_start - u.start
        pend = u.new_start + len(u.data)
        if runs and runs[-1][4] == d and runs[-1][1] == u.start:
            runs[-1][1], runs[-1][3] = vend, pend
        else:
            runs.append([u.start, vend, u.new_start, pend, d])
    return [tuple(r[:4]) for r in runs]


def map_needed(runs):
    """everything at its original offset (one run, nothing moved): no map (a file that changed size inside
    its alignment reads as an in-place edit)"""
    return len(runs) != 1 or runs[0][:3] != (0, 0x800000, 0)


def map_blob(runs, flags):
    import zlib
    body = b"".join(struct.pack(">IIII", *r) for r in runs)
    head = MAP_MAGIC + struct.pack(">IIII", 1, len(runs), flags, zlib.crc32(body) & 0xFFFFFFFF) + bytes(8)
    return head + body


def put_map(rom, at, blob):
    import zlib
    rom[at:at + len(blob)] = blob
    struct.pack_into(">8sII", rom, len(rom) - 16, MAP_FOOTER, at, zlib.crc32(blob) & 0xFFFFFFFF)


def fix_crc(rom):
    from nm_rom import cic6102_crc      # tools_port/nm_rom.py
    struct.pack_into(">II", rom, 0x10, *cic6102_crc(bytes(rom)))
