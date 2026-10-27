"""Putting edited files back into a ROM.

In place: a file whose new compressed bytes fit its slot (up to the next
file's start: levels and models are padded to 16 bytes, buildings are
packed back to back) is written over the old bytes, the rest of the slot
zeroed.  Every loader takes the length as "next file's start minus this
one's" and inflates exactly two members, so trailing zeros are never read
as data.  The ROM keeps every offset, so the code, the tables and the port
(which loads the original offsets) need no change.

A file that doesn't fit needs the shiftable build (docs/shiftable.md):
`shift_config()` writes the compressed file and a pads JSON entry
("rom": {"replace": ...}) for `make SHIFT=1 SHIFT_PADS=...`, which moves
everything after it and rewrites the tables and the code's ROM offsets.
The config also carries what the in-place write would have put in the ROM
next to it (the files that fit, a mod's texture slots: TexPatch) as
"patch" entries, the same bytes at the same original offsets.
"""
import json
import os
import struct

from . import gz

CIC6102_SEED = 0xF8CA4DDC


def _rol(v, n):
    n &= 31
    return ((v << n) | (v >> (32 - n))) & 0xFFFFFFFF if n else v


def cic6102_crc(rom):
    """The two header checksums for a CIC-6102 cartridge (over ROM 0x1000-0x101000)."""
    t1 = t2 = t3 = t4 = t5 = t6 = CIC6102_SEED
    words = struct.unpack_from(">%dI" % (0x100000 // 4), rom, 0x1000)
    for d in words:
        if (t6 + d) & 0xFFFFFFFF < t6:
            t4 = (t4 + 1) & 0xFFFFFFFF
        t6 = (t6 + d) & 0xFFFFFFFF
        t3 ^= d
        r = _rol(d, d & 0x1F)
        t5 = (t5 + r) & 0xFFFFFFFF
        if t2 > d:
            t2 ^= r
        else:
            t2 ^= t6 ^ d
        t1 = (t1 + (t5 ^ d)) & 0xFFFFFFFF
    return (t6 ^ t4 ^ t3) & 0xFFFFFFFF, (t5 ^ t2 ^ t1) & 0xFFFFFFFF


def fix_crc(rom):
    struct.pack_into(">II", rom, 0x10, *cic6102_crc(rom))


def build_members(romfile, data, split, level=6, all_members=False):
    """gzip bytes for a pair with new inflated data (split = first member's
    length); unchanged members keep their original deflate stream (unless
    all_members: then every member is compressed again at `level`)."""
    out = b""
    parts = [data[:split], data[split:]] if len(romfile.members) == 2 else [data]
    for m, part in zip(romfile.members, parts):
        nm = gz.Member(part, m.name, m.mtime, m.xfl, m.os, m.flags, m.extra, m.comment)
        if part == m.data and not all_members:
            out += nm.header() + m.deflate + nm.trailer()
        else:
            out += nm.build(gz.compress(part, level))
    return out


def pack_to_fit(romfile, data, split):
    """The gzip bytes for a changed file, trying to stay inside its slot:
    level 6 (what the ROM used; byte-identical for unchanged data), then
    level 9 for the changed member, then level 9 for both.  Returns
    (blob, how)."""
    slot = romfile.info.get("slot_end", romfile.end) - romfile.start
    best = None
    for how, kw in (("level 6", {}), ("level 9", {"level": 9}), ("level 9, both members", {"level": 9, "all_members": True})):
        blob = build_members(romfile, data, split, **kw)
        if len(blob) <= slot:
            return blob, how
        if best is None or len(blob) < len(best[0]):
            best = (blob, how)
    return best


class Patch:
    """one replaced file: its RomFile, the new gzip bytes, and how it was placed"""

    def __init__(self, romfile, blob, changed, how="level 6"):
        self.file, self.blob, self.changed, self.how = romfile, blob, changed, how
        self.slot = romfile.info.get("slot_end", romfile.end) - romfile.start
        self.fits = len(blob) <= self.slot


def apply_in_place(rom_bytes, patches):
    """Write the patches that fit into a copy of the ROM; returns (rom, [patches that don't fit])."""
    out = bytearray(rom_bytes)
    rest = []
    for p in patches:
        if not p.changed:
            continue
        if not p.fits:
            rest.append(p)
            continue
        s = p.file.start
        out[s:s + p.slot] = p.blob + bytes(p.slot - len(p.blob))
    fix_crc(out)
    return bytes(out), rest


TEX_TABLE = 0x4CE0


class TexPatch:
    """one texture-table slot written in place (bcfiles mod-export): the slot's
    room (its DMA length: up to the next entry's offset) gets the packed bytes
    and zeros after them, the table entry's u16 at +4 the packed size"""

    def __init__(self, index, rom, room, packed):
        if len(packed) > room:
            raise ValueError("texture %d: %d packed bytes, room %d" % (index, len(packed), room))
        self.index, self.rom, self.room, self.packed = index, rom, room, bytes(packed)

    @property
    def size(self):
        return len(self.packed)

    def slot_bytes(self):
        return self.packed + bytes(self.room - len(self.packed))

    def ranges(self):
        """[(ROM offset, bytes)]: what the in-place write puts in the ROM"""
        return [(self.rom, self.slot_bytes()), (TEX_TABLE + 8 * self.index + 4, struct.pack(">H", self.size))]


def apply_textures(data, tex):
    """the texture slots (TexPatch) into data (a bytearray), in place: the code and the tables keep every offset"""
    for t in tex:
        for at, b in t.ranges():
            data[at:at + len(b)] = b


# The retail ROM's trailer (0xFF to the end): the shift build's last unit, where nothing is read.  New texture slots
# go there in place, the last 16 bytes kept (a shifted ROM's map footer is there)
TRAILER = 0x7F9BE0
ROM_END = 0x800000


def free_tail(data):
    """(start, end): the 0xFF bytes at the end of the original 8 MB (16-aligned start, the last 16 bytes left out)"""
    k = min(len(data), ROM_END)
    while k > 0 and data[k - 1] == 0xFF:
        k -= 1
    k = (k + 15) & ~15
    return k, ROM_END - 16


def new_slots_layout(news, base):
    """[(TexPlan, offset)]: new slots one after the other from base in id order (bc.exe puts them so past the ROM's
    end: an entry's DMA - to the next entry's offset - is its packed bytes exactly), and the end"""
    out = []
    at = base
    for p in sorted(news, key=lambda p: p.index):
        out.append((p, at))
        at += p.size
    return out, at


def new_slot_entries(layout, end):
    """[(table offset, bytes)]: each new slot's entry (offset from the table, its size, packing 0) and the next
    entry's offset (the end of its bytes: the empty entry after it ends its DMA; a new slot there writes its own)"""
    out = []
    for i, (p, at) in enumerate(layout):
        out.append((TEX_TABLE + 8 * p.index, struct.pack(">IHH", at - TEX_TABLE, p.size, 0)))
        nxt = at + p.size
        if i + 1 >= len(layout) or layout[i + 1][0].index != p.index + 1:
            out.append((TEX_TABLE + 8 * (p.index + 1), struct.pack(">I", nxt - TEX_TABLE)))
    return out


def apply_new_slots(data, news):
    """new texture slots in place: in the ROM's free tail, their table entries pointed there.  -> the offset they
    start at; None if they don't fit there (the shift path then: shift_config's "add")"""
    start, end = free_tail(data)
    layout, at = new_slots_layout(news, start)
    if at > end:
        return None
    for p, off in layout:
        data[off:off + p.size] = p.packed
    for off, b in new_slot_entries(layout, at):
        data[off:off + len(b)] = b
    return start


def in_place_ranges(patch):
    """[(ROM offset, bytes)] apply_in_place writes for a patch that fits: the file and zeros to its slot's end"""
    return [(patch.file.start, patch.blob + bytes(patch.slot - len(patch.blob)))]


def shift_config(patches, outdir, assets_units=None, in_place=(), textures=(), grown=(), new=()):
    """For patches that don't fit: write each compressed file and a pads JSON
    for the shiftable build.  The whole pair goes into the unit at the file's
    start; the unit of its second member (every gzip member is a unit of the
    shift build, tools_port/romsplit.py) is emptied, so nothing can come
    between the two members.

    in_place (patches that fit) and textures (TexPatch) go in as "patch"
    entries: the same bytes at the same original offsets as the in-place
    write (a file and the zeros to its slot's end; a texture slot's room and
    its table entry's size), over units that keep their size, so they move
    with them wherever the shift build puts them.

    grown (mod.TexPlan "grow": a texture slot past its room): its texture's
    ROM file replaced by the packed bytes rounded up to 8 (bc.exe's grown
    room), the files after it moving and the table following, and its entry's
    size as a "patch".  new (TexPlan "new": slots in empty table entries):
    their bytes one after the other (in id order, as bc.exe lays them out)
    as an added file "texnew" before the trailer, and "texnew" entries for
    the shift build to point the table there (romlayout.apply_texnew)."""
    os.makedirs(outdir, exist_ok=True)
    rep = []
    for p in patches:
        path = os.path.join(outdir, "%06X_%s.gz" % (p.file.start, p.file.name))
        with open(path, "wb") as f:
            f.write(p.blob)
        e = {"at": "0x%06X" % p.file.start, "path": os.path.abspath(path)}
        if len(p.file.members) == 2:
            e["empty"] = ["0x%06X" % (p.file.members[1].start - p.file.members[1].pad)]
        rep.append(e)
    cfg = {"rom": {"replace": rep}}
    pat = []
    for p in in_place:
        if not p.changed:
            continue
        for at, b in in_place_ranges(p):
            path = os.path.join(outdir, "%06X_%s.in_place.bin" % (at, p.file.name))
            with open(path, "wb") as f:
                f.write(b)
            pat.append({"at": "0x%06X" % at, "path": os.path.abspath(path), "what": "%s %s (fits in place)"
                        % (p.file.kind, p.file.name)})
    for t in textures:
        path = os.path.join(outdir, "texture_%04d.bin" % t.index)
        with open(path, "wb") as f:
            f.write(t.slot_bytes())
        # (the size field: 2 bytes of the texture table, whose offsets the shift build rewrites around it)
        pat.append({"at": "0x%06X" % t.rom, "path": os.path.abspath(path), "texture": t.index,
                    "what": "texture %d: %d packed bytes, room %d" % (t.index, t.size, t.room)})
        pat.append({"at": "0x%06X" % (TEX_TABLE + 8 * t.index + 4), "hex": "%04X" % t.size, "texture": t.index,
                    "what": "texture %d's table entry: packed size" % t.index})
    for g in grown:
        path = os.path.join(outdir, "texture_%04d.grown.bin" % g.index)
        with open(path, "wb") as f:
            f.write(g.packed + bytes(g.grown_room() - g.size))
        rep.append({"at": "0x%06X" % g.rom, "path": os.path.abspath(path),
                    "what": "texture %d grown: %d packed bytes, its room was %d" % (g.index, g.size, g.room)})
        pat.append({"at": "0x%06X" % (TEX_TABLE + 8 * g.index + 4), "hex": "%04X" % g.size, "texture": g.index,
                    "what": "texture %d's table entry: packed size" % g.index})
    if new:
        layout, end = new_slots_layout(new, 0)
        path = os.path.join(outdir, "texnew.bin")
        with open(path, "wb") as f:
            f.write(b"".join(p.packed for p, _ in layout))
        cfg["rom"]["add"] = [{"name": "texnew", "path": os.path.abspath(path), "before": "0x%06X" % TRAILER}]
        cfg["rom"]["texnew"] = [{"id": p.index, "at": at, "size": p.size} for p, at in layout]
    if pat:
        cfg["rom"]["patch"] = pat
    path = os.path.join(outdir, "shift_pads.json")
    with open(path, "w") as f:
        json.dump(cfg, f, indent=1)
    return path
