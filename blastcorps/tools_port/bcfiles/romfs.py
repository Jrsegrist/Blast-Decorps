"""The ROM as a list of files (docs/systems/files.md section 1).

The files from the title logo to the front end's code are found by walking
the ROM the way it is laid out: gzip members one after another (zero
padding between files), the three LZSS front-end pictures, the scenes, the
packed-object table and the buildings it lists, the static segment, the
code.  Names come from the gzip FNAME fields, level numbers from the names
(lvl_load_file's order), vehicle types from lvl_load_model's table.  So a
ROM from the shiftable build (files moved) reads like the original; the
constants below are the original US offsets (1.0 and 1.1 share every asset
offset), which the walk reproduces exactly on an unmodified ROM.  The
texture and sound area before the title logo is taken at its original
offsets.
"""
import struct

from . import gz, lzss
from .binio import FormatError
from .texture import TEX_TABLE, read_table

ROM_SIZE = 0x800000
OBJ_TABLE = 0x6EC4C0
OBJ_ENTRIES = 0x200
STATIC_SEGMENT = 0x787F40
HD_CODE = 0x787FD0

# level number -> ROM start (romLagpRaw .. romLevel59Raw); the next level's start (or
# romWorldTexturesRaw for 59) ends it.  Levels 0 and 1 are swapped in ROM order.
LEVEL_ROM = [0x4ACC10, 0x4A5660, 0x4B8960, 0x4BFD60, 0x4C3AC0, 0x4D5F90, 0x4E2F70, 0x4E4E80, 0x4E7C00,
             0x4E8F70, 0x4F5C10, 0x500520, 0x507E80, 0x511340, 0x523080, 0x52CD00, 0x532700, 0x53E9B0,
             0x54A820, 0x552DE0, 0x555000, 0x560E90, 0x5652D0, 0x56F3F0, 0x5721E0, 0x5736E0, 0x57A2C0,
             0x580B60, 0x588CE0, 0x58BE80, 0x597B80, 0x59B7D0, 0x5A5840, 0x5B0B10, 0x5B5A30, 0x5B8BB0,
             0x5C4C80, 0x5CA9C0, 0x5CCF50, 0x5D1060, 0x5DC830, 0x5E6EE0, 0x5EC800, 0x5F3A80, 0x6014B0,
             0x60A710, 0x613AA0, 0x61DD70, 0x621AF0, 0x6269E0, 0x630C30, 0x635700, 0x63CA10, 0x641F30,
             0x644810, 0x646080, 0x647550, 0x654FC0, 0x660950, 0x665F80]
LEVELS_END = 0x66C900

# lvl_load_model's ROM ranges: (start, end, type numbers)
MODEL_ROM = [
    (0x48FE90, 0x4903C0, [0x98]),            # scientist (academy marker)
    (0x4903C0, 0x490AC0, [0x96]),            # commpoint
    (0x490AC0, 0x491E00, [3]),               # buggy = Skyfall
    (0x491E00, 0x4929D0, [0]),               # driver
    (0x4929D0, 0x494390, [1]),               # sideswipe
    (0x494390, 0x496AD0, [2]),               # magoo = Thunderfist
    (0x496AD0, 0x497AF0, [4]),               # bulldozer = Ramdozer
    (0x497AF0, 0x4989E0, [5]),               # truck = Backlash
    (0x4989E0, 0x499690, [0xFE]),            # chopper
    (0x499690, 0x49AD20, [0xFF]),            # cmo = the carrier
    (0x49AD20, 0x49B630, [6]),               # crane
    (0x49B630, 0x49BCE0, [7]),               # train
    (0x49BCE0, 0x49C480, [8]),               # hotrod = American Dream
    (0x49C480, 0x49E8E0, [9]),               # jetpack = J-Bomb
    (0x49E8E0, 0x49F7A0, [10]),              # bike = Ballista
    (0x49F7A0, 0x49FF70, [11, 17, 18]),      # barge
    (0x49FF70, 0x4A0720, [13]),              # police
    (0x4A0720, 0x4A1000, [14]),              # ateam
    (0x4A1000, 0x4A1690, [15]),              # starski
    (0x4A1690, 0x4A4120, [16]),              # minimagoo = Cyclone
    (0x4A4120, 0x4A5660, [0xFD]),            # shuttle
]
SCENE_ROM = [(0x6E8980, 0x6EA850), (0x6EA850, 0x6EAB90), (0x6EAB90, 0x6EC4B4)]

# LZSS files of the original layout: (start, end, bits, what)
LZSS_FILES = [(0x350950, 0x3539A0, 13, "music bank .ctl"), (0x3A1920, 0x3A48C0, 13, "sfx bank .ctl"),
              (0x6AD3F0, 0x6BF2F0, 13, "front-end picture 1"), (0x6BF2F0, 0x6D3D30, 13, "front-end picture 2"),
              (0x6D3D30, 0x6E8980, 13, "front-end picture 3"), (STATIC_SEGMENT, 0x788000, 10, "static segment")]
RAW_FILES = [(0x0, 0x40, "cartridge header"), (0x40, 0x1000, "IPL3"), (0x1000, TEX_TABLE, "init (boot code)"),
             (TEX_TABLE, 0xCCE0, "texture table"), (0x3539A0, 0x3A1920, "music samples .tbl"),
             (0x3A48C0, 0x44F5C0, "sfx samples .tbl"), (0x44F5C0, 0x487050, "sequence file + tunes"),
             (OBJ_TABLE, OBJ_TABLE + 0x800, "packed-object table")]


def byteswap_to_z64(data):
    """.z64 (big-endian), .v64 (16-bit swapped) or .n64 (32-bit swapped) -> .z64"""
    b = bytes(data[:4])
    if b == b"\x80\x37\x12\x40":
        return bytes(data)
    out = bytearray(data)
    if b == b"\x37\x80\x40\x12":
        out[0::2], out[1::2] = data[1::2], data[0::2]
        return bytes(out)
    if b == b"\x40\x12\x37\x80":
        for i in range(0, len(out) - 3, 4):
            out[i:i + 4] = data[i:i + 4][::-1]
        return bytes(out)
    raise FormatError("not an N64 ROM image")


class RomFile:
    """One file of the ROM.  kind: level, model, scene, object, image, demos,
    lzss, code, raw.  members: the gzip members (if gzip)."""

    def __init__(self, kind, start, end, name, members=None, **info):
        self.kind, self.start, self.end, self.name = kind, start, end, name
        self.members = members or []
        self.info = info

    @property
    def data(self):
        """the inflated bytes, members back to back (as the game puts them in RAM)"""
        if self.members:
            return b"".join(m.data for m in self.members)
        return self.info.get("data", b"")

    @property
    def split(self):
        """length of the first member (the data/display-list boundary of a pair)"""
        return len(self.members[0].data) if self.members else 0

    def __repr__(self):
        return "RomFile(%s %06X-%06X %s)" % (self.kind, self.start, self.end, self.name)




LEVEL_NAMES = {"chimp": 0, "lagp": 1, "valley": 2, "fact": 3, "dip": 4, "beetle": 5, "bonus1": 6, "bonus2": 7,
               "bonus3": 8}
LEVEL_NAMES.update(("level%d" % n, n) for n in range(9, 60))
MODEL_NAMES = ["scientist", "commpoint", "buggy", "driver", "sideswipe", "magoo", "bulldozer", "truck", "chopper",
               "cmo", "crane", "train", "hotrod", "jetpack", "bike", "barge", "police", "ateam", "starski",
               "minimagoo", "shuttle"]
MODEL_TYPES = dict(zip(MODEL_NAMES, [t for _, _, t in MODEL_ROM]))
SCENE_NAMES = ("usa_star", "ninlogo", "reflectlogo")
DEMO_NAMES = ("attract",)


def _align16(p):
    return (p + 15) & ~15


class Rom:
    def __init__(self, data):
        self.data = byteswap_to_z64(data)
        if len(self.data) < ROM_SIZE:
            raise FormatError("ROM too small (%X bytes)" % len(self.data))
        self.game_code = self.data[0x3B:0x3F].decode("latin-1")
        self.revision = self.data[0x3F]
        if self.game_code != "NBCE":
            raise FormatError("not a US Blast Corps ROM (game code %r); this library knows NBCE 1.0/1.1"
                              % self.game_code)
        self.version = "us.v1%d" % self.revision
        self._files = None
        self._tex = None
        self._objtab = None
        self._scan()

    @classmethod
    def load(cls, path):
        with open(path, "rb") as f:
            return cls(f.read())

    # ----------------------------------------------------------- the walk
    def _skip_pad(self, p):
        """past the zeros between two files (alignment, or the rest of a slot a
        smaller file was written into; LZSS streams and gzip members start
        with a non-zero byte)"""
        q = p
        while q < len(self.data) and self.data[q] == 0 and q - p < 0x10000:
            q += 1
        return q

    def _scan(self):
        d = self.data
        k = d.find(b"titlelogo_usa.raw\0")
        if k < 10 or d[k - 10:k - 8] != b"\x1f\x8b":
            raise FormatError("can't find the title logo's gzip member (not a Blast Corps ROM layout this library knows)")
        p = k - 10
        members = []
        while True:
            q = self._skip_pad(p)
            if d[q:q + 2] != b"\x1f\x8b":
                break
            m = gz.read_member(d, q)
            members.append(m)
            p = m.end
        chain = []                      # RomFiles in ROM order
        i = 0
        while i < len(members):
            m = members[i]
            name = (m.name or "%06X" % m.start).replace(".raw", "")
            if i + 1 < len(members) and (members[i + 1].name or "") == name + "_dl.raw":
                pair = [m, members[i + 1]]
                i += 2
                if name in LEVEL_NAMES:
                    f = RomFile("level", m.start, pair[1].end, name, pair, level=LEVEL_NAMES[name])
                elif name in MODEL_TYPES:
                    f = RomFile("model", m.start, pair[1].end, name, pair, types=MODEL_TYPES[name])
                else:
                    f = RomFile("pair", m.start, pair[1].end, name, pair)
            else:
                i += 1
                f = RomFile("demos" if name in DEMO_NAMES else "image", m.start, m.end, name, [m])
            chain.append(f)
        # the three LZSS front-end pictures (13-bit offsets)
        for n in range(3):
            q = self._skip_pad(p)
            _, end = lzss.decompress(d, 13, q)
            chain.append(RomFile("lzss", q, end, "front-end picture %d" % (n + 1), bits=13))
            p = end
        # the scenes (pairs)
        for _ in SCENE_NAMES:
            q = self._skip_pad(p)
            a = gz.read_member(d, q)
            b = gz.read_member(d, a.end)
            chain.append(RomFile("scene", q, b.end, a.name.replace(".raw", ""), [a, b]))
            p = b.end
        self.obj_table_rom = _align16(p)
        tab = struct.unpack_from(">%dI" % OBJ_ENTRIES, d, self.obj_table_rom)
        if tab[0] != 0x800 or d[self.obj_table_rom + 0x800:self.obj_table_rom + 0x802] != b"\x1f\x8b":
            raise FormatError("no packed-object table after the scenes (at %06X)" % self.obj_table_rom)
        self._objtab = list(tab)
        for f, nxt in zip(chain, chain[1:] + [None]):
            f.info["slot_end"] = nxt.start if nxt is not None else self.obj_table_rom
        self.chain = chain
        self.levels = {}
        self.models = {}
        for f in chain:
            if f.kind == "level":
                self.levels[f.info["level"]] = f
            elif f.kind == "model":
                self.models[f.name] = f
        if sorted(self.levels) != list(range(60)) or len(self.models) != len(MODEL_NAMES):
            raise FormatError("found %d levels and %d models (want 60 and %d)" % (len(self.levels), len(self.models),
                                                                               len(MODEL_NAMES)))
        # buildings: the table's distinct offsets; the largest is the end of the last one
        offs = sorted(set(tab))
        self.obj_end = self.obj_table_rom + offs[-1]
        self.obj_starts = [self.obj_table_rom + o for o in offs[:-1]]
        self.static_rom = _align16(self.obj_end)
        _, send = lzss.decompress(d, 10, self.static_rom)
        self.static_end = send
        self.code_rom = _align16(send)
        if d[self.code_rom:self.code_rom + 2] != b"\x1f\x8b":
            raise FormatError("no hd_code member after the static segment (at %06X)" % self.code_rom)

    # ----------------------------------------------------------- tables
    @property
    def tex_table(self):
        if self._tex is None:
            self._tex = read_table(self.data)
        return self._tex

    @property
    def obj_table(self):
        return self._objtab

    def texture_packed(self, idx):
        e = self.tex_table[idx]
        return self.data[e.rom:e.rom + e.size]

    def texture(self, idx, palette=None):
        from .texture import decode
        e = self.tex_table[idx]
        return decode(self.texture_packed(idx), e.type, palette)

    # ----------------------------------------------------------- files
    def pair(self, start, kind, name_hint=None, **info):
        a = gz.read_member(self.data, start)
        b = gz.read_member(self.data, a.end)
        return RomFile(kind, start, b.end, (a.name or name_hint or "%06X" % start).replace(".raw", ""), [a, b], **info)

    def level(self, n):
        return self.levels[n]

    def model(self, i):
        """model i in lvl_load_model's ROM order (MODEL_NAMES)"""
        return self.models[MODEL_NAMES[i]]

    def object(self, index):
        """packed object (building) `index` of the object table"""
        s = self.obj_table_rom + self.obj_table[index]
        if s >= self.obj_end:
            raise KeyError("object-table entry %d is unused (points at the end)" % index)
        f = self.pair(s, "object", index=index)
        later = [x for x in self.obj_starts if x > s]
        f.info["slot_end"] = later[0] if later else self.obj_end
        return f

    def object_indexes(self):
        """distinct object-table entries that start a file -> [indexes using it]"""
        by = {}
        for i, off in enumerate(self.obj_table):
            if self.obj_table_rom + off < self.obj_end:
                by.setdefault(off, []).append(i)
        return by

    def code_members(self):
        """hd_code text + data, then the front end's text + data (two pairs)"""
        out = []
        p = self.code_rom
        for name in ("hd_code", "hd_front_end"):
            a = gz.read_member(self.data, p)
            b = gz.read_member(self.data, a.end)
            out.append(RomFile("code", p, b.end, name, [a, b]))
            p = _align16(b.end)
        return out

    def files(self):
        """every file, in ROM order"""
        if self._files is not None:
            return self._files
        fl = []
        for s, e, what in RAW_FILES:
            if s == OBJ_TABLE:
                s, e = self.obj_table_rom, self.obj_table_rom + 0x800
            fl.append(RomFile("raw", s, e, what))
        fl.append(RomFile("textures", 0xCCE0, 0x350950, "textures and palettes (%d table entries)" % len(self.tex_table)))
        for s, e, bits, what in LZSS_FILES[:2]:
            fl.append(RomFile("lzss", s, e, what, bits=bits))
        fl += self.chain
        for off, idxs in sorted(self.object_indexes().items()):
            f = self.object(idxs[0])
            f.info["indexes"] = idxs
            fl.append(f)
        fl.append(RomFile("lzss", self.static_rom, self.static_end, "static segment", bits=10))
        fl += self.code_members()
        fl.sort(key=lambda f: (f.start, f.end))
        self._files = fl
        return fl

    def lzss_file(self, f):
        return lzss.decompress(self.data, f.info["bits"], f.start)