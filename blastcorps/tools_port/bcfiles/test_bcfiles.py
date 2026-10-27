"""bcfiles tests.

    python3 tools_port/bcfiles/test_bcfiles.py              synthetic data only (no ROM; CI)
    python3 tools_port/bcfiles/test_bcfiles.py --rom ROM    also every file of the player's ROM:
        parse/write/JSON round trip of every level, model, scene and building,
        recompression, LZSS, the texture table, an unchanged rebuild == the ROM

Nothing here contains or writes ROM data: the --rom tests read the ROM given
and compare in memory.
"""
import base64
import gzip
import hashlib
import json
import os
import random
import struct
import sys
import tempfile
import unittest
import zlib

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path[:] = [p for p in sys.path if os.path.abspath(p or ".") != HERE]
sys.path.insert(0, os.path.dirname(HERE))

from bcfiles import deflate, gz, lzss, png, rombuild, texture  # noqa: E402
import gzpack  # noqa: E402
from bcfiles.binio import Vtx  # noqa: E402
from bcfiles import level as L  # noqa: E402
from bcfiles import model as M  # noqa: E402

ROM_PATH = None


# ------------------------------------------------------------------ helpers

def synthetic_level(nobj=3):
    lv = L.Level()
    lv.header = {k: 0 for k in L.HEADER_U16}
    lv.header.update(vis_cols=2, vis_rows=1, ground_cols=1, ground_rows=2, coll_cols=2, coll_rows=1,
                     gravity=-4, dl_profile=1)
    lv.vertices = [Vtx(i, 2 * i, -i, 0, 32 * i, 0, 255, 128, 0, 255) for i in range(6)]
    t9 = [10, 0, 10, 20, 0, 10, 10, 0, 30]
    sec = {}
    sec[0x20] = [L.Crate(1, 2, 3, 0)]
    sec[0x24] = [L.GuardTri(*(t9 + [7]))]
    sec[0x28] = [L.CommPoint(5, 6, 7, 1)]
    sec[0x2C] = [{"head": L.TexAnimHead(9, 3, 0, 1, 0, 4, 0), "ids": [11, 12]}]
    sec[0x30] = [[L.GroundTri(*(t9 + [1, 0]))], []]
    sec[0x34] = [L.Rdu(1, 1, 1), L.Rdu(2, 2, 2)]
    sec[0x38] = [L.Tnt(1, 2, 3, 0, 5, 0, 0)]
    sec[0x3C] = {"blocks": [L.Block(1, 2, 3, 0)],
                 "slots": [{"x": 4, "y": 5, "z": 6, "model": 0, "target": 1, "tris": [L.CollTri(*(t9 + [0x400, 1, 0]))]}]}
    sec[0x40] = [L.HeightZone(0, 0, 100, 100, 3000)]
    sec[0x44] = []
    sec[0x48] = [[0, 10], [5, 20]]
    sec[0x4C] = L.Bounds(-100, -100, 100, 100)
    sec[0x50] = [L.VehSpawn(0, 10, 0, 10, 0), L.VehSpawn(1, 10, 0, 10, 0x400)]
    sec[0x54] = L.Carrier(3, 0, -500, 0, 2000)
    sec[0x58] = [L.PanelEvent(100, 1, 2, 3, 4, 0, 0)]
    sec[0x5C] = [L.ObjPlacement(100 * i, 0, 50, i, 0, 2, 0, 0) for i in range(nobj)]
    sec[0x60] = {"default": 255, "zones": [{"x": 1, "y": 2, "z": 3, "radius": 50, "mode": 1, "vehicles": [0, 9],
                                            "object": 1, "alpha": 0}]}
    sec[0x64] = [{"tag": 1, "kinds": [0], "tris": [L.SwitchTri(*(t9 + [0x800]))]}]
    sec[0x68] = [{"owner": 7, "kind": 0, "areas": [L.GateArea(0, 0, 10, 0, 0, 10)], "word": 5,
                  "tris": [L.GateTri(*(t9 + [0, 3]))], "ids": [3]},
                 {"owner": 7, "kind": 1, "states": [0x0102], "tris": [], "ids": []}]
    sec[0x6C] = [[L.CollTri(*(t9 + [0x400, 1, 0]))], []]
    sec[0x70] = [[], [L.CollTri(*(t9 + [0, 0, 0]))]]
    sec[0x74] = {"nmtx": 0x40, "groups": [{"head": L.PartGroupHead(*([-1] + [0] * 9 + [1] + [0] * 9 + [1])),
                                           "items": [L.PartItem(*([0] * 9 + [0, 0] + [0x40, 1, 2, 3] * 3))]}]}
    # display lists: two lists; vis records point into them (block-relative)
    dl = struct.pack(">IIII", 0x04100000 | 0x20, 0x08000000, 0xBF000000, 0x0000000A) + struct.pack(">II", 0xB8000000, 0)
    dl += struct.pack(">II", 0x06000000, 0) + struct.pack(">II", 0xB8000000, 0)
    sec[0xA0] = [L.VisRecord(0, 0, 0x10, 1, 0)]
    sec[0xA4] = []
    sec[0xA8] = []
    sec[0xAC] = [L.VisRecord(0, 8, 0x10, 2, 1)]
    sec[0xB0] = [{"start": 0, "alt": 8, "end": 0x10, "id": 2, "cells": [0, 1]}]
    sec[0xB4] = []
    sec[0xB8] = []
    sec[0xBC] = []
    sec[0xC0] = None
    for s, v in sec.items():
        lv.sections[s] = L.Section(s, v)
    lv.dl = dl
    lv.dl_ptrs = {0x7C: 0x18, 0x80: 0x28, 0x88: 0, 0x8C: 0x18, 0x90: 0x18, 0x94: 0x18, 0x98: 0x18, 0x9C: 0x18}
    return lv


def synthetic_object():
    o = M.PackedObject()
    o.fields = {f[0]: 0 for f in M.OBJ_HEADER}
    o.fields.update(num_topple_keys=2, radius=50, toughness=3, people=1)
    o.vertices = [Vtx(i, i, i, 0, 0, 0, 1, 2, 3, 4) for i in range(4)]
    t9 = [1, 2, 3, 4, 5, 6, 7, 8, 9]
    vals = {0x1C: [M.Corner(1, 2, 3)] * 4, 0x20: [M.ObjBounds(-5, -5, 5, 5)], 0x24: [M.ObjTri(*(t9 + [1]))],
            0x28: [{"id": 1, "b5": 0, "b6": 0, "b7": 0, "w8": 0, "hC": 0, "hE": 0, "ids": [7]}],
            0x2C: [M.ObjPoint(1, 2, 3, 0)], 0x30: [M.Debris(*([0] * 11 + [3, 4] + [0] * 8))], 0x34: [],
            0x38: {"entries": [[1, 2, 3]], "groups": [{"words": [1, 2, 3, 4], "entries": [[5, 6, 7]]}]},
            0x3C: [1, 2], 0x40: [3], 0x44: [1, 2, 3], 0x48: [M.Tri25(*(t9 + [b"\0" * 7]))], 0x4C: None,
            0x10: struct.pack(">IIII", 0x04300040, 0x09000000, 0xB8000000, 0)}
    for s, v in vals.items():
        o.sections[s] = [v, b"", None]
    o.interior = {0x18: 8}
    return o


def synthetic_model():
    m = M.VehicleModel()
    vals = {0x00: {"lead": [], "runs": [{"h": [1, 2, 3], "words": [4, 5]}]},
            0x04: {"lead": [1, 0], "runs": [{"h": [1, 2, 3, 4, 5], "words": [6]}]},
            0x08: {"words": [1], "rows": [list(range(10))]}, 0x0C: None,
            0x10: {"table": [0, 1, 1], "table_tail": "", "blocks": [
                {"head": "01020300", "groups": [{"a": 1, "b": 2, "keys": [list(range(10))]}], "tail": ""},
                {"head": "", "groups": [], "tail": ""}]},
            0x14: [Vtx(1, 2, 3, 0, 0, 0, 9, 9, 9, 9)] * 3, 0x18: [0x40, 0x800, 0x10000, 0],
            0x1C: struct.pack(">IIII", 0x04200020, 0x06000000, 0xB8000000, 0)}
    for s, v in vals.items():
        m.sections[s] = [v, b"", None]
    m.interior = {s: 0 for s in M.VehicleModel.INTERIOR}
    return m


# ------------------------------------------------------------- synthetic

class TestGzip(unittest.TestCase):
    def test_member_roundtrip(self):
        data = bytes(range(256)) * 40
        m = gz.Member(data, "x.raw", 855417165)
        blob = m.build(gz.compress_zlib(data))
        r = gz.read_member(b"\0" + blob + b"tail", 0)        # one pad byte is skipped, as kiunzip does
        self.assertEqual(r.data, data)
        self.assertEqual(r.name, "x.raw")
        self.assertEqual(r.mtime, 855417165)
        self.assertEqual(r.pad, 1)
        self.assertEqual(r.end, 1 + len(blob))
        self.assertTrue(r.trailer_ok)
        self.assertEqual(r.header(), blob[:len(r.header())])

    def test_two_members(self):
        a, b = b"first" * 100, b"second" * 50
        blob = gz.Member(a, "a.raw").build(gz.compress_zlib(a)) + gz.Member(b, "a_dl.raw").build(gz.compress_zlib(b))
        ms = gz.read_members(blob, 0, 2)
        self.assertEqual([m.data for m in ms], [a, b])

    def test_bad_method(self):
        blob = bytearray(gz.Member(b"x", "n").build(gz.compress_zlib(b"x")))
        blob[2] = 7
        with self.assertRaises(Exception):
            gz.read_member(bytes(blob), 0)


def deflate_samples():
    """Synthetic inputs of several shapes for the deflate tests."""
    rnd = random.Random(7)
    words = [b"blast", b"corps", b"truck", b"crate", b"bomb", b"carrier", b"route", b"tnt", b" ", b"\n"]
    text = b"".join(rnd.choice(words) for _ in range(9000))
    runs = b"".join(bytes(rnd.randrange(256) for _ in range(3)) + b"\0" * rnd.randrange(150, 250) for _ in range(300))
    mixed = bytearray()
    while len(mixed) < 150000:          # past the 64 KB window: slides, several blocks
        k = rnd.randrange(4)
        if k == 0:
            mixed += bytes(rnd.randrange(256) for _ in range(rnd.randrange(50, 2000)))
        elif k == 1:
            mixed += text[rnd.randrange(20000):][:rnd.randrange(100, 5000)]
        elif k == 2:
            mixed += bytes([rnd.randrange(256)]) * rnd.randrange(3, 600)
        else:
            mixed += bytes(rnd.randrange(4) for _ in range(rnd.randrange(100, 3000)))
    return {
        "empty": b"",
        "one": b"x",
        "short": b"hello hello hello, said the crate",
        "random": bytes(rnd.randrange(256) for _ in range(3000)),
        "text": text,
        "lowentropy": bytes(rnd.randrange(4) for _ in range(20000)),
        "runs": runs,
        "mixed": bytes(mixed),
    }


# sha1 of deflate_raw(sample) at level 6: pins the exact output (the encoder
# exists to be byte-exact; the --rom test checks it against the ROM itself)
DEFLATE_GOLDEN = {
    "empty": "688934845f22049cb14668832efa33d45013b6b9",
    "one": "b8cd821899f42e9036271f730be064ae124fc6b6",
    "short": "cf441fed13ddc6768bc3fe36b6ca761e67832091",
    "random": "ccf64313f59de43334a11d0337fee43cb1265544",
    "text": "61fba1a27e56cd9035086f2105993528f285941f",
    "lowentropy": "b01b43a5635206ba13de8a90ab6982ba0b9f5dd1",
    "runs": "79d939a8770715fefb7fe38e2a4f4ec170f80980",
    "mixed": "1d9f57a55440d1e101cf2386c0d6dcf11d9487b6",
}


class TestDeflate(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.samples = deflate_samples()
        cls.out = {k: deflate.deflate_raw(v) for k, v in cls.samples.items()}

    def test_roundtrip(self):
        for k, v in self.samples.items():
            self.assertEqual(zlib.decompress(self.out[k], -15), v, k)

    def test_block_types(self):
        # first block's BTYPE: stored for incompressible data, static for a
        # short string, dynamic for text
        btype = {k: (self.out[k][0] >> 1) & 3 for k in ("random", "short", "text")}
        self.assertEqual(btype, {"random": 0, "short": 1, "text": 2})

    def test_golden(self):
        got = {k: hashlib.sha1(v).hexdigest() for k, v in self.out.items()}
        self.assertEqual(got, DEFLATE_GOLDEN)

    def test_levels(self):
        s = self.samples["mixed"][:40000]
        for level in (4, 5, 7, 8, 9):
            self.assertEqual(zlib.decompress(deflate.deflate_raw(s, level), -15), s, level)
        with self.assertRaises(ValueError):
            deflate.deflate_raw(s, 3)

    def test_gzpack(self):
        data = self.samples["text"]
        blob = gzpack.gzip_member(data, "hd_code_text.raw", 855502464)
        self.assertEqual(blob[:10], b"\x1f\x8b\x08\x08" + struct.pack("<I", 855502464) + b"\x00\x03")
        self.assertEqual(blob[10:27], b"hd_code_text.raw\0")
        self.assertEqual(blob[27:-8], self.out["text"])
        self.assertEqual(gzip.decompress(blob), data)
        m = gz.read_member(blob, 0)
        self.assertEqual((m.name, m.mtime, m.data), ("hd_code_text.raw", 855502464, data))
        self.assertTrue(m.trailer_ok)
        self.assertEqual(m.build(gz.compress(data)), blob)


class TestLzss(unittest.TestCase):
    def test_roundtrip(self):
        rnd = random.Random(1)
        samples = [b"", b"a", b"abcabcabcabcabc" * 20, bytes(rnd.randrange(4) for _ in range(5000)),
                   bytes(rnd.randrange(256) for _ in range(700)), b"\0" * 3000]
        for bits in (10, 12, 13):
            for s in samples:
                c = lzss.compress(s, bits)
                d, end = lzss.decompress(c, bits)
                self.assertEqual(d, s, "bits %d len %d" % (bits, len(s)))
                self.assertEqual(end, len(c))
                self.assertEqual(end % 2, 0)

    def test_hand_stream(self):
        # 1 'A', 1 'B', then a copy of 3+1 bytes from window position 1 (bits = 10), then the end
        bits = []

        def put(v, n):
            bits.extend((v >> (n - 1 - i)) & 1 for i in range(n))
        put(1, 1); put(0x41, 8); put(1, 1); put(0x42, 8)
        put(0, 1); put(1, 10); put(1, 6)
        put(0, 1); put(0, 10)
        bits += [0] * (-len(bits) % 16)
        data = bytes(int("".join(map(str, bits[i:i + 8])), 2) for i in range(0, len(bits), 8))
        out, end = lzss.decompress(data, 10)
        self.assertEqual(out, b"ABABAB")


class TestTexture(unittest.TestCase):
    def test_type1_literal_and_copy(self):
        toks = [0x7FFF, 0x0001, 0x8000 | (4 << 5) | 2]       # two texels, then copy 2 halfwords from 4 bytes back
        out = texture.decode(struct.pack(">3H", *toks), 1)
        self.assertEqual(struct.unpack(">4H", out), (0xFFBF, 0x0001, 0xFFBF, 0x0001))

    def test_type0_drops_tail(self):
        self.assertEqual(texture.decode(bytes(range(13)), 0), bytes(range(8)))

    def test_types(self):
        self.assertEqual(texture.decode(struct.pack(">H", 0x7FFF), 2), struct.pack(">I", 0xF0F0F0E0))
        self.assertEqual(texture.decode(struct.pack(">H", 0x7F01), 3), bytes((0xFE, 0x02)))
        self.assertEqual(texture.decode(struct.pack(">H", 0x3F3F), 6), bytes((0xEE, 0xEE)))
        pal = struct.pack(">128H", *range(128))
        self.assertEqual(texture.decode(struct.pack(">H", 0x0405), 4, pal), struct.pack(">HH", 4, 5))
        self.assertEqual(texture.decode(struct.pack(">H", 0x001F), 5, struct.pack(">2H", 0, 0x7FFF)),
                         struct.pack(">I", 0xF8F8F8F0))

    def test_rgba(self):
        rgba = texture.to_rgba8(struct.pack(">H", 0xF801), 0, 16, 1, 1)
        self.assertEqual(rgba, bytes((255, 0, 0, 255)))
        self.assertEqual(texture.to_rgba8(bytes((0xF0,)), 4, 4, 2, 1), bytes((255,) * 4 + (0,) * 4))


class TestPng(unittest.TestCase):
    def test_roundtrip(self):
        rgba = bytes(range(4 * 6 * 5)) * 1
        w, h, back = png.decode_rgba(png.encode_rgba(6, 5, rgba))
        self.assertEqual((w, h, back), (6, 5, rgba))


class TestCrc(unittest.TestCase):
    def test_fix(self):
        rom = bytearray(0x101000)
        for i in range(0x1000, 0x101000, 4):
            struct.pack_into(">I", rom, i, (i * 2654435761) & 0xFFFFFFFF)
        rombuild.fix_crc(rom)
        self.assertEqual(struct.unpack_from(">II", rom, 0x10), rombuild.cic6102_crc(rom))
        self.assertNotEqual(rombuild.cic6102_crc(rom), (0, 0))


class TestLevel(unittest.TestCase):
    def test_write_parse_write(self):
        lv = synthetic_level()
        data, split = lv.write()
        p = L.Level.parse(data, split)
        self.assertEqual(p.notes, [])
        self.assertEqual(p.write(), (data, split))
        self.assertEqual(json.loads(json.dumps(p.to_json())), json.loads(json.dumps(lv.to_json())))
        self.assertEqual(L.Level.from_json(json.loads(json.dumps(p.to_json()))).write(), (data, split))
        self.assertEqual(p["objects"][1].x, 100)
        self.assertEqual(p["coll_grid_b"][1][0].x0, 10)

    def test_grow_keeps_references(self):
        small = synthetic_level(3)
        big = synthetic_level(10)
        d1, s1 = small.write()
        d2, s2 = big.write()
        self.assertGreater(s2, s1)
        p = L.Level.parse(d2, s2)
        self.assertEqual(len(p["objects"]), 10)
        # visibility records stay relative to the display-list block, which moved
        self.assertEqual(p["vis_records_3"][0].alt, 8)
        hdr = struct.unpack_from(">I", d2, 0x78)[0]
        rec = struct.unpack_from(">I", d2, struct.unpack_from(">I", d2, 0xAC)[0] + 4)[0]
        self.assertEqual(rec, hdr + 8)
        self.assertEqual(hdr % 16, 0)
        # every later section moved by the same amount
        self.assertEqual(p.write(), (d2, s2))

    def test_odd_bytes_kept(self):
        # bytes the format doesn't explain after a section's records are kept as its tail
        lv = synthetic_level()
        lv.sections[0x54].tail = b"\x5A\x00\x00"
        lv.sections[0x20].tail = b"\x01\x02"
        data, split = lv.write()
        p = L.Level.parse(data, split)
        self.assertEqual(p.notes, [])
        self.assertTrue(p.sections[0x54].tail.startswith(b"\x5A"))
        self.assertEqual(p.sections[0x20].tail, b"\x01\x02")
        self.assertEqual(p.write(), (data, split))

    def test_opaque_fallback(self):
        p = L.Level.parse(b"\1" * 0x100)
        self.assertIsNotNone(p.raw)
        self.assertEqual(p.write(), (b"\1" * 0x100, 0x100))


class TestMod(unittest.TestCase):
    """level mods (.bcmod, mod.py): the same rules as bc.exe's (port/src/load/level_mod.c)"""

    def setUp(self):
        from bcfiles import mod
        self.mod = mod
        self.data, self.split = synthetic_level().write()

    def level_mod(self, changes, edited=True):
        lv = L.Level.parse(self.data, self.split)
        for c in changes:
            self.mod.apply_change(lv, c)
        out = lv.write()[0] if edited else None
        return self.mod.make("t", "", [(5, self.data, changes, out)])

    def test_set_add_delete(self):
        m = self.mod
        changes = [{"op": "set", "kind": "object", "index": 1, "fields": {"x": 1880, "z": 1881}},
                   {"op": "add", "kind": "vehicle", "index": 2, "copy_of": 1, "fields": {"x": 400}},
                   {"op": "delete", "kind": "rdu", "index": 0}]
        j = m.parse(json.loads(m.dumps(self.level_mod(changes))))
        data, split = m.apply_level(j["levels"][0], self.data, self.split)
        p = L.Level.parse(data, split)
        self.assertEqual(p.notes, [])
        self.assertEqual((p["objects"][1].x, p["objects"][1].z), (1880, 1881))
        self.assertEqual([(v.type, v.x) for v in p["spawns"]], [(0, 10), (1, 10), (1, 400)])
        self.assertEqual(len(p["rdus"]), 1)
        # the odd-sized spawn moved everything after it; the block stays on 16 bytes
        self.assertEqual(split % 16, 0)
        self.assertEqual(p.dl, L.Level.parse(self.data, self.split).dl)

    def test_undo_gives_the_original(self):
        changes = [{"op": "add", "kind": "crate", "index": 0, "copy_of": 0, "fields": {"y": 99}},
                   {"op": "delete", "kind": "crate", "index": 0}]
        j = self.level_mod(changes)
        data, split = self.mod.apply_level(j["levels"][0], self.data, self.split)
        self.assertEqual((data, split), (self.data, self.split))
        self.assertEqual(j["levels"][0]["edited_sha1"], j["levels"][0]["original_sha1"])

    def test_slot_moves_its_triangles(self):
        changes = [{"op": "set", "kind": "slot", "index": 0, "fields": {"x": 54}}]
        data, split = self.mod.apply_level(self.level_mod(changes)["levels"][0], self.data, self.split)
        s = L.Level.parse(data, split)["blocks"]["slots"][0]
        self.assertEqual((s["x"], s["tris"][0].x0, s["tris"][0].x1, s["tris"][0].y0), (54, 60, 70, 0))

    def test_refused(self):
        m = self.mod
        j = self.level_mod([{"op": "set", "kind": "rdu", "index": 0, "fields": {"x": 5}}])
        other = bytearray(self.data)
        other[0xC8] ^= 1
        with self.assertRaises(m.ModError):         # another ROM's file
            m.apply_level(j["levels"][0], bytes(other), self.split)
        j["levels"][0]["edited_sha1"] = "0" * 40
        with self.assertRaises(m.ModError):         # not the maker's result
            m.apply_level(j["levels"][0], self.data, self.split)
        bad = [{"op": "add", "kind": "carrier", "index": 1, "copy_of": 0},
               {"op": "delete", "kind": "carrier", "index": 0},
               {"op": "set", "kind": "object", "index": 0, "fields": {"x": -1}},
               {"op": "set", "kind": "vehicle", "index": 0, "fields": {"heading": 4096}},
               {"op": "add", "kind": "commpt", "index": 0, "copy_of": 9},
               {"op": "delete", "kind": "tnt", "index": 5}]
        for c in bad:
            lv = L.Level.parse(self.data, self.split)
            with self.assertRaises(m.ModError, msg=str(c)):
                m.apply_change(lv, c)
            self.assertEqual(lv.write(), (self.data, self.split), "a refused change leaves the level as it was")
        lv = L.Level.parse(self.data, self.split)
        m.apply_change(lv, {"op": "add", "kind": "commpt", "index": 0, "copy_of": 0})
        with self.assertRaises(m.ModError):         # the game holds 2
            m.apply_change(lv, {"op": "add", "kind": "commpt", "index": 0, "copy_of": 0})
        lv = L.Level.parse(self.data, self.split)
        m.apply_change(lv, {"op": "delete", "kind": "vehicle", "index": 0})
        with self.assertRaises(m.ModError):         # no driver
            m.check_rules(lv)
        for t in ({"format": "x", "levels": []}, {"format": "bcmod/1", "levels": [{"level": 60}]},
                  {"format": "bcmod/1", "levels": [{"level": 1, "original_sha1": "z" * 40, "changes": []}]},
                  {"format": "bcmod/1", "levels": [{"level": 1, "original_sha1": "a" * 40,
                                                    "changes": [{"op": "add", "kind": "rdu", "index": 0}]}]}):
            with self.assertRaises(m.ModError):
                m.parse(t)


def geometry_level():
    """synthetic_level() with a small terrain: a 200 x 100 quad at y 10 (two
    drawn and two floor triangles over a 2 x 1 ground grid of 100-unit cells),
    a sub-list with Rare's G_CULLDL prologue called from fixed list 0, and one
    wall at x = 150"""
    lv = synthetic_level()
    lv.header.update(vis_cols=2, vis_rows=1, vis_cell_w=100, vis_cell_d=100, ground_cols=2, ground_rows=1,
                     ground_cell_w=100, ground_cell_d=100, coll_cols=2, coll_rows=1, coll_cell_w=100, coll_cell_d=100)
    quad = [(0, 10, 0), (200, 10, 0), (200, 10, 100), (0, 10, 100)]
    box = [(x, 10, z) for z in (0, 100) for x in (0, 200) for _ in (0, 1)]     # Rare's corner order, flat in y
    lv.vertices = [Vtx(x, y, z, 0, 0, 0, 0, 0, 0, 0) for (x, y, z) in box] + \
                  [Vtx(x, y, z, 0, x * 4, z * 4, 200, 100, 50, 255) for (x, y, z) in quad]
    c = []
    c += [(0xE7000000, 0)]                                        # 0x00 prefix (0x88..0x8C)
    sub = 8
    c += [(0x04700080, 0x08000000), (0xBE000000, 0x00000140),     # the box and G_CULLDL
          (0xFD100000, 7), (0x04300040, 0x08000080),              # a texture id, the quad's 4 vertices
          (0xBF000000, 0x00000A14), (0xBF000000, 0x0000141E), (0xB8000000, 0)]
    fixed = 8 * len(c)
    c += [(0x06000000, sub), (0xB8000000, 0)] + [(0xB8000000, 0)] * 3
    lv.dl = b"".join(struct.pack(">II", *x) for x in c)
    lv.dl_ptrs = {0x7C: fixed, 0x80: fixed, 0x88: 0, 0x8C: 8, 0x90: fixed, 0x94: fixed + 16, 0x98: fixed + 24,
                  0x9C: fixed + 32}
    for s in (0xA0, 0xA4, 0xA8, 0xAC):
        lv.sections[s].value = []
    for s in (0xB0, 0xB4, 0xB8, 0xBC):
        lv.sections[s].value = []
    t1 = [0, 10, 0, 200, 10, 0, 200, 10, 100]
    t2 = [0, 10, 0, 200, 10, 100, 0, 10, 100]
    lv.sections[0x30].value = [[L.GroundTri(*(t1 + [1, 0])), L.GroundTri(*(t2 + [1, 0]))]] * 2
    wall = [150, 10, 0, 150, 60, 0, 150, 60, 100]
    # (in cell 1 only: the half-open rule's answer, see test_full_rebuild_matches_the_rules)
    lv.sections[0x6C].value = [[], [L.CollTri(*(wall + [0x400, 1, 0]))]]
    lv.sections[0x70].value = [[], []]
    lv.sections[0x48].value = [[0, 10], [0, 10]]
    lv.sections[0x4C].value = L.Bounds(-50, -50, 250, 150)
    lv.sections[0x74].value = {"nmtx": 0, "groups": []}
    lv.sections[0x64].value = []
    lv.sections[0x68].value = []
    return lv


class TestGeometry(unittest.TestCase):
    """geometry.py: the generators and edits, on synthetic triangles"""

    def setUp(self):
        from bcfiles import geometry
        self.G = geometry
        self.data, self.split = geometry_level().write()

    def level(self):
        return L.Level.parse(self.data, self.split)

    def test_keep_rare_gives_the_file_back(self):
        lv = self.level()
        g = self.G.Geometry.from_level(lv)
        self.assertEqual(g.stats()["frozen"], 0)
        out = self.level()
        g.write_into(out, base=lv)
        self.assertEqual(out.write(), (self.data, self.split))

    def test_full_rebuild_matches_the_rules(self):
        lv = self.level()
        out = self.level()
        self.G.Geometry.from_level(lv).write_into(out)
        self.assertEqual(self.G.level_draw_streams(lv), self.G.level_draw_streams(out))
        # both floor triangles meet both cells; the wall at x 150 only touches cell 0's grown
        # rectangle [-50, 150] at its high edge (half-open: not in it)
        self.assertEqual([len(c) for c in out["ground_grid"]], [2, 2])
        self.assertEqual([len(c) for c in out["coll_grid_a"]], [0, 1])
        self.assertEqual(out["vis_grid"], [[0, 10], [0, 10]])
        self.assertEqual(out["bounds"], L.Bounds(-50, -50, 250, 150))

    def test_wall_ties_kept(self):
        # a file that lists the wall in cell 0 too (as Rare did for 11 such walls): the rebuild keeps that
        # for the unchanged triangle, the rule applies again once it moves
        lv = self.level()
        lv.sections[0x6C].value = [list(lv.sections[0x6C].value[1])] * 2
        data, split = lv.write()
        src = L.Level.parse(data, split)
        g = self.G.Geometry.from_level(src)
        self.assertEqual(sum(len(v) for v in g.wall_ties[0x6C].values()), 1)
        out = L.Level.parse(data, split)
        g.write_into(out)
        self.assertEqual([len(c) for c in out["coll_grid_a"]], [1, 1])
        g = self.G.Geometry.from_level(src)
        g.apply_edit({"move": [[150, 10, 0, 151, 10, 0], [150, 60, 0, 151, 60, 0], [150, 60, 100, 151, 60, 100]]})
        out = L.Level.parse(data, split)
        g.write_into(out)
        self.assertEqual([len(c) for c in out["coll_grid_a"]], [0, 1])

    def test_cell_rules(self):
        G = self.G
        big = [-1000, 0, -1000, 3000, 0, -1000, -1000, 0, 3000]        # covers cell 0 and 1 strictly
        self.assertEqual(G.tri_cells(big, 100, 100, 2, 1), [])
        corner = [0, 0, 0, 3000, 0, 0, 0, 0, 3000]                     # a corner on cell 0's corner
        self.assertEqual(G.tri_cells(corner, 100, 100, 2, 1), [0, 1])
        edge = [100, 0, 10, 100, 50, 10, 100, 50, 90]                  # vertical, on x = 100
        self.assertEqual(G.tri_cells(edge, 100, 100, 2, 1), [0, 1])
        self.assertEqual(G.tri_cells(edge, 100, 100, 2, 1, 0, True), [1])
        far = [260, 0, 10, 260, 50, 10, 260, 50, 90]                   # 60 past cell 1: out of reach
        self.assertEqual(G.tri_cells(far, 100, 100, 2, 1, 50, True), [])
        seg = [0, 0, 300, 300, 0, 0, 300, 0, 0]                        # a segment, off the cells' corner
        self.assertFalse(G.tri_meets_rect(seg, 0, 0, 100, 100))
        self.assertEqual(G.wall_heading([150, 10, 0, 150, 60, 0, 150, 60, 100]) in (0x400, 0xC00), True)

    def test_move_raises_and_regenerates(self):
        lv = self.level()
        g = self.G.Geometry.from_level(lv)
        g.apply_edit({"move": [[200, 10, 100, 200, 300, 100]]}, lv)
        out = self.level()
        g.write_into(out, base=lv)
        data, split = out.write()
        self.assertEqual(len(data), len(self.data))          # moved vertices are written over the old ones
        p = L.Level.parse(data, split)
        tris = [t[2] for t in self.G.Geometry.from_level(p).render_tris()]
        self.assertIn((200, 300, 100), [(v[0], v[1], v[2]) for t in tris for v in t])
        self.assertEqual(p["vis_grid"][1], [0, 300])                     # the moved corner is in cell 1 only
        self.assertEqual(p["vis_grid"][0], [0, 10])
        self.assertEqual(p["ground_grid"][0][0].y1, 10)
        self.assertIn(300, [t._v[k] for t in p["ground_grid"][1] for k in (1, 4, 7)])
        # the culling box follows the vertices
        st = self.G.Geometry.from_level(p)
        sub = [pc for pc in st.pieces if pc.kind == "sub"][0]
        self.assertEqual(sub.ops[0][0], "cull")
        self.assertEqual(max(v[1] for v in sub.ops[0][6]), 300)

    def test_remove_add_and_batches(self):
        lv = self.level()
        g = self.G.Geometry.from_level(lv)
        new = [{"after": 1, "v": [[0, 10, 100 + 10 * k, 0, 0, 9, 9, 9, 255], [200, 10, 100 + 10 * k, 0, 0, 9, 9, 9, 255],
                                  [100, 20, 110 + 10 * k, 0, 0, 9, 9, 9, 255]]} for k in range(8)]
        g.apply_edit({"remove": {"render": [0], "ground": [0]}, "add": {"render": new, "ground": [
            {"v": [0, 10, 100, 200, 10, 100, 100, 20, 190], "surface": 2}]}}, lv)
        out = self.level()
        g.write_into(out, base=lv)
        data, split = out.write()
        p = L.Level.parse(data, split)
        s = self.G.draw_stream(p, p.dl_ptrs[0x90])
        tris = [x for x in s if x[0] == "T"]
        self.assertEqual(len(tris), 1 + 8)
        # every G_VTX loads at most 16 and the triangles' vertices were loaded
        for q in range(0, len(p.dl), 8):
            w0 = struct.unpack_from(">I", p.dl, q)[0]
            if w0 >> 24 == 0x04:
                self.assertLessEqual(((w0 >> 20) & 0xF) + 1, 16)
        self.assertTrue(all(None not in t[2] for t in tris))
        self.assertEqual([t._v[9] for c in p["ground_grid"] for t in c].count(2), 2)
        self.assertEqual(p["bounds"].maxz, 190 + 50)

    def test_refused_edits(self):
        G = self.G
        lv = self.level()
        lv.sections[0x64].value = [{"tag": 1, "kinds": [0], "tris": [L.SwitchTri(*([0, 10, 0, 200, 10, 0, 9, 9, 9] + [0]))]}]
        for ed in ({"move": [[0, 10, 0, 1, 2, 3]]},                       # a switch triangle's corner
                   {"move": [[1, 2, 3, 4, 5, 70000]]},
                   {"move": [[200, 10, 0, 1, 1, 1], [200, 10, 0, 2, 2, 2]]},
                   {"remove": {"render": [99]}},
                   {"add": {"render": [{"after": 0, "v": [[0] * 9] * 2}]}},
                   {"add": {"ground": [{"v": [0] * 8}]}}):
            with self.assertRaises(G.GeometryError, msg=str(ed)):
                G.Geometry.from_level(lv).apply_edit(ed, lv)

    def test_geometry_mod(self):
        from bcfiles import mod
        ed = {"move": [[200, 10, 100, 200, 40, 100]]}
        m = mod.make("hill", "", [(3, self.data, [], None, ed)])
        self.assertEqual(m["format"], "bcmod/2")
        j = mod.parse(json.loads(mod.dumps(m)))
        data, split = mod.apply_level(j["levels"][0], self.data, self.split)
        self.assertNotEqual(data, self.data)
        self.assertEqual(L.Level.parse(data, split)["vis_grid"][1], [0, 40])
        # an empty edit is the original; v1 still loads; v1 can't carry geometry
        m0 = mod.make("none", "", [(3, self.data, [], None, {"move": []})])
        self.assertEqual(mod.apply_level(mod.parse(json.loads(mod.dumps(m0)))["levels"][0], self.data, self.split),
                         (self.data, self.split))
        self.assertEqual(mod.make("v1", "", [(3, self.data, [], None)])["format"], "bcmod/1")
        with self.assertRaises(mod.ModError):
            mod.parse({"format": "bcmod/1", "levels": [{"level": 3, "original_sha1": "a" * 40, "changes": [],
                                                       "geometry": {}}]})

    def _corners(self, ed):
        return [tuple(v[:3]) for a in ed["add"]["render"] for v in a["v"]]

    def test_subdivide_neighbours_and_seams(self):
        # drawn triangle 0 split: its neighbour 1 splits along the shared diagonal, both floor twins the same,
        # and the wall's foot (150, 10, 0), a corner the level has on the split edge (0,0)-(200,0), is taken in
        lv = self.level()
        g = self.G.Geometry.from_level(lv)
        ed = self.G.subdivide_edit(g, [0], 1, lv=lv)
        self.assertEqual(ed["remove"]["render"], [0, 1])
        self.assertEqual(ed["remove"]["ground"], [0, 1])
        self.assertEqual(len(ed["add"]["render"]), 4 + 1 + 2)
        self.assertEqual(len(ed["add"]["ground"]), 4 + 1 + 2)
        self.assertNotIn("move", ed)
        self.assertIn((150, 10, 0), self._corners(ed))
        self.assertIn((100, 10, 50), self._corners(ed))          # the shared diagonal's midpoint
        self.assertEqual({a["after"] for a in ed["add"]["render"]}, {0, 1})
        # texture coordinates split with the corners (x * 4, z * 4 at the quad's corners)
        for a in ed["add"]["render"]:
            for v in a["v"]:
                self.assertEqual((v[3], v[4]), (v[0] * 4, v[2] * 4))
        out = self.level()
        g2 = self.G.Geometry.from_level(lv)
        g2.apply_edit(ed, lv)
        g2.write_into(out, base=lv)
        self.assertEqual(len(self.G.Geometry.from_level(L.Level.parse(*out.write())).render_tris()), 7)

    def test_subdivide_keeps_an_edge_a_neighbour_cant_split(self):
        # a switch triangle on corner (0, 10, 100) protects it: triangle 1 and its floor twin can't split, so
        # their shared edge with triangle 0 stays whole and triangle 0 splits three ways (+ the wall-foot seam)
        lv = self.level()
        lv.sections[0x64].value = [{"tag": 1, "kinds": [0], "tris": [L.SwitchTri(*([0, 10, 100, 0, 60, 100, 0, 60, 150]
                                                                                    + [0]))]}]
        g = self.G.Geometry.from_level(lv)
        ed = self.G.subdivide_edit(g, [0], 1, lv=lv)
        self.assertEqual(ed["remove"]["render"], [0])
        self.assertEqual(ed["remove"]["ground"], [0])
        self.assertEqual(len(ed["add"]["render"]), 3 + 1)
        self.assertNotIn((100, 10, 50), self._corners(ed))
        with self.assertRaises(self.G.GeometryError):
            self.G.subdivide_edit(self.G.Geometry.from_level(lv), [1], 1, lv=lv)

    def test_subdivide_twice_and_raise(self):
        lv = self.level()
        g = self.G.Geometry.from_level(lv)
        ed = self.G.subdivide_edit(g, [0, 1], 2, height=lambda x, z: 20, lv=lv)
        # every corner of the pieces is raised: the level's own (moves, the wall's foot too) and the new ones
        self.assertEqual(sorted(tuple(m[:3]) for m in ed["move"]),
                         [(0, 10, 0), (0, 10, 100), (150, 10, 0), (200, 10, 0), (200, 10, 100)])
        self.assertTrue(all(m[4] == m[1] + 20 and (m[3], m[5]) == (m[0], m[2]) for m in ed["move"]))
        self.assertTrue(all(c[1] == 30 for c in self._corners(ed)))
        self.assertEqual(len(ed["add"]["render"]), len(ed["add"]["ground"]))
        self.assertGreaterEqual(len(ed["add"]["render"]), 32)
        # the wall split where its foot is: its triangles keep heading, side and fail
        self.assertEqual({(w["heading"], w["side"], w["fail"]) for w in ed["add"].get("walls_a", [])} or {(0x400, 1, 0)},
                         {(0x400, 1, 0)})

    def test_gltf_round_trip(self):
        from bcfiles import geogltf
        lv = self.level()
        with tempfile.TemporaryDirectory() as d:
            path, st = geogltf.export(None, lv, d, "geo")
            self.assertEqual((st["render"], st["ground"], st["walls_a"]), (2, 2, 1))
            self.assertEqual(geogltf.import_edit(path, lv), {})
            # move a corner in the file (as Blender would write it back): POSITION of every vertex at (200, 10, 100)
            j = json.load(open(path))
            binp = os.path.join(d, j["buffers"][0]["uri"])
            buf = bytearray(open(binp, "rb").read())
            for m in j["meshes"]:
                for pr in m["primitives"]:
                    a = j["accessors"][pr["attributes"]["POSITION"]]
                    off = j["bufferViews"][a["bufferView"]]["byteOffset"]
                    for k in range(a["count"]):
                        x, y, z = struct.unpack_from("<3f", buf, off + 12 * k)
                        if (round(x * 100), round(y * 100), round(z * 100)) == (200, 10, 100):
                            struct.pack_into("<3f", buf, off + 12 * k, x, 0.5, z)
            open(binp, "wb").write(buf)
            ed = geogltf.import_edit(path, lv)
            self.assertEqual(ed, {"move": [[200, 10, 100, 200, 50, 100]]})


class TestTexturePack(unittest.TestCase):
    def test_packings_round_trip(self):
        w, h = 16, 8
        rgba = bytes((x * 16, y * 32, (x * y) & 255, 255 if (x + y) % 3 else 0)[k] for y in range(h) for x in range(w)
                     for k in range(4))
        for fmt, siz, ptype in ((0, 16, 1), (0, 32, 2), (3, 8, 3), (4, 4, 6), (3, 16, 0), (4, 8, 3)):
            tex = texture.from_rgba8(rgba, fmt, siz, w, h)
            packed = texture.encode_slot(rgba, w, h, fmt, siz, ptype, len(tex))
            self.assertEqual(texture.decode(packed, ptype)[:len(tex)], texture.quantize(tex, ptype), (fmt, siz, ptype))
        flat = bytes([10, 20, 30, 255]) * (w * h)
        self.assertLess(len(texture.encode_slot(flat, w, h, 0, 16, 1)), 16)     # one literal and back-references
        with self.assertRaises(texture.TextureError):
            texture.encode_slot(rgba, w, h, 2, 8, 1)                           # CI: no

    def test_mipmaps(self):
        # the level lists' layout: 32 x 32 RGBA16 in 4 levels at TMEM 0, 256, 320, 336 (1360 texels)
        lay, total = texture.mip_layout(32, 32, 16, 4)
        self.assertEqual(total, 2720)
        self.assertEqual([o for o, _, _, _ in lay], [0, 2048, 2560, 2688])
        self.assertEqual(texture.mip_layout(16, 64, 16, 4)[1], 2752)          # 2 x 8 rows padded to 8 bytes
        # odd rows' 32-bit words swapped, level 1 the 2 x 2 means
        img = b"".join(bytes((i * 32, 0, 0, 255)) for i in range(8))
        t = texture.mip_texels(img, 4, 2, 0, 16, 2)
        r = [struct.unpack_from(">H", t, k)[0] >> 11 for k in range(0, len(t), 2)]
        self.assertEqual(r, [0, 4, 8, 12, 24, 28, 16, 20, 10, 18, 0, 0])
        # a slot: the image, its levels (each the means of the one before), the rest of the slot kept
        w, h = 16, 16
        rgba = bytes((x * 16, y * 16, 128, 255)[k] for y in range(h) for x in range(w) for k in range(4))
        orig = bytes(range(256)) * 3
        packed = texture.encode_slot(rgba, w, h, 0, 16, 1, orig, 3)
        back = texture.decode(packed, 1)
        n = texture.mip_layout(w, h, 16, 3)[1]
        self.assertEqual(back[n:len(orig)], texture.quantize(orig, 1)[n:])
        lv0 = texture.unswap_rows(back[:512], w, h, 16)
        self.assertEqual(lv0, texture.quantize(texture.from_rgba8(rgba, 0, 16, w, h), 1))
        down, dw, dh = texture.mip_down(rgba, w, h)
        self.assertEqual(texture.unswap_rows(back[512:640], dw, dh, 16),
                         texture.quantize(texture.from_rgba8(down, 0, 16, dw, dh), 1))
        with self.assertRaises(texture.TextureError):
            texture.encode_slot(rgba, w, h, 0, 32, 2, None, 4)                # 32-bit levels: not done
        with self.assertRaises(texture.TextureError):
            texture.encode_slot(rgba, w, h, 0, 16, 1, orig[:600], 4)          # more than the slot holds

    def test_palette_packings(self):
        # a synthetic palette: entry i = (i, 2i, 31 - i) in 5 bits
        pal = b"".join(struct.pack(">H", (i & 31) << 10 | ((2 * i) & 31) << 5 | (31 - (i & 31))) for i in range(128))
        w, h = 16, 8
        # every pixel a palette colour (entries 0..63), alpha alternating
        cols = [((i & 31) << 3, ((2 * i) & 31) << 3, (31 - (i & 31)) << 3) for i in range(64)]
        rgba = b"".join(bytes(cols[(x * 7 + y * 3) % 64]) + bytes((255 if (x + y) % 2 else 0,))
                        for y in range(h) for x in range(w))
        for ptype, siz in ((4, 16), (5, 32)):
            tex, toks = texture.palette_texels(rgba, w, h, ptype, pal)
            packed = texture.encode_slot(rgba, w, h, 0, siz, ptype, None, palette=pal)
            back = texture.decode(packed, ptype, pal)
            self.assertEqual(back[:len(tex)], tex)
            # exact colours come back exactly (RGBA16: 5 bits; RGBA32: 5 bits per colour, 4-bit alpha)
            want = bytes(cols[0]) if siz == 32 else bytes(c | c >> 5 for c in cols[0])   # (RGBA16 shown 5 -> 8 bits)
            self.assertEqual(texture.to_rgba8(back, 0, siz, w, h)[0:3], want)
            # literals: packing 4's pair bytes stay below 0x80 for the first texel
            self.assertTrue(all(t < 0x8000 for t in toks))
        with self.assertRaises(texture.TextureError):
            texture.encode_slot(rgba, w, h, 0, 16, 4)                         # no palette
        with self.assertRaises(texture.TextureError):
            texture.encode_slot(rgba, w, h, 3, 8, 4, None, palette=pal)        # packing 4 is RGBA16
        with self.assertRaises(texture.TextureError):
            texture.encode_slot(rgba, w, h, 0, 32, 5, bytes(4 * w * h + 4), palette=pal)   # the slot is bigger


class TestModels(unittest.TestCase):
    def test_object(self):
        o = synthetic_object()
        data, split = o.write()
        p = M.PackedObject.parse(data, split)
        self.assertEqual(p.notes, [])
        self.assertEqual(p.write(), (data, split))
        self.assertEqual(M.PackedObject.from_json(json.loads(json.dumps(p.to_json()))).write(), (data, split))
        self.assertEqual(split % 16, 0)

    def test_model(self):
        m = synthetic_model()
        data, split = m.write()
        p = M.VehicleModel.parse(data, split)
        self.assertEqual(p.notes, [])
        self.assertEqual(p.write(), (data, split))
        self.assertEqual(M.VehicleModel.from_json(json.loads(json.dumps(p.to_json()))).write(), (data, split))
        self.assertEqual(len(p.vertices()), 3)

    def test_gltf(self):
        from bcfiles import gltf
        o = synthetic_object()
        o.sections[0x10][0] = struct.pack(">IIII", 0x04300040, 0x09000000, 0xBF000000, 0x00000A14) + \
            struct.pack(">II", 0xB8000000, 0)
        with tempfile.TemporaryDirectory() as d:
            path, st = gltf.export_object(None, o, d, "t")
            with open(path) as fh:
                g = json.load(fh)
            self.assertEqual(st["triangles"], 1)
            self.assertEqual(g["accessors"][g["meshes"][0]["primitives"][0]["indices"]]["count"], 3)
            self.assertEqual(os.path.getsize(os.path.join(d, "t.bin")), g["buffers"][0]["byteLength"])


def _romlayout():
    """tools_port/shift/romlayout.py (its shiftlib needs pyelftools); None if it doesn't import"""
    p = os.path.join(os.path.dirname(HERE), "shift")
    if p not in sys.path:
        sys.path.insert(0, p)
    try:
        import romlayout
    except ImportError:
        return None
    return romlayout


class TestShiftTextures(unittest.TestCase):
    """mod-export's shift path (exit 3): the texture slots and the levels that fit go into the shiftable build's
    config as in-place "patch" entries, written over their units before they are placed (synthetic ROM)"""
    T = 0x4CE0

    def setUp(self):
        self.RL = _romlayout()
        if self.RL is None:
            self.skipTest("tools_port/shift doesn't import (pyelftools)")
        RL = self.RL
        # a table of 4096 entries: textures 0, 1, 2 (rooms 0x20, 0x40, 0x38), the rest empty at the end of them,
        # then an alignment gap (zeros) and a unit the build makes from the code
        tab = bytearray(0x8000)
        offs = [0xCCE0, 0xCD00, 0xCD40] + [0xCD78] * 4093
        for i, o in enumerate(offs):
            struct.pack_into(">IHH", tab, 8 * i, o - self.T, [0x18, 0x40, 0x30][i] if i < 3 else 0, 1 if i < 3 else 0)
        base = bytearray(0x800000)
        base[0:0x4CE0] = bytes((i * 7) & 255 for i in range(0x4CE0))
        base[self.T:self.T + 0x8000] = tab
        base[0xCCE0:0xCD78] = bytes((i * 13 + 1) & 255 for i in range(0x98))
        base[0xCD80:0xCE00] = b"\x5A" * 0x80
        self.base = bytes(base)
        self.units = []
        for start, size, typ, name in ((0, 0x4CE0, "bin", "head"), (self.T, 0x8000, "bin", "textable"),
                                       (0xCCE0, 0x20, "bin", "t0"), (0xCD00, 0x40, "bin", "t1"),
                                       (0xCD40, 0x38, "bin", "t2"), (0xCD78, 0x8, "", ""),
                                       (0xCD80, 0x800000 - 0xCD80, "bin", "hd_code_text")):
            u = RL.RomUnit(start, typ, name)
            u.size = size
            u.data = self.base[start:start + size]
            self.units.append(u)
        RL.mark_fills(self.units, self.base)
        self.assertTrue(self.units[5].fill)
        self.assertTrue(self.units[6].dyn)

    def build(self, pads=()):
        RL = self.RL
        seq, end = RL.place(self.units, pads)
        m = RL.RomMapper(self.units)
        tu = m.unit_of(self.T)
        tu.data = RL.rewrite_tex_table(tu.data, m, tu.new_start)
        return RL.write_rom(seq, end)

    def entry(self, image, i):
        off, size, typ = struct.unpack_from(">IHH", image, self.T + 8 * i)
        return self.T + off, size

    def config(self, tex, in_place=()):
        with tempfile.TemporaryDirectory() as d:
            path = rombuild.shift_config([], d, in_place=in_place, textures=tex)
            with open(path) as fh:
                cfg = json.load(fh)
            return cfg, self.RL.load_patches(cfg["rom"])

    def test_config_is_the_in_place_write(self):
        tp = rombuild.TexPatch(1, 0xCD00, 0x40, b"\x11" * 0x25)
        cfg, patches = self.config([tp])
        self.assertEqual(cfg["rom"]["replace"], [])
        self.assertEqual([(p[0], len(p[1]), p[3]) for p in patches], [(0xCD00, 0x40, 1), (self.T + 12, 2, 1)])
        self.RL.apply_patches(self.units, patches, self.base)
        image = self.build()
        inplace = bytearray(self.base)
        rombuild.apply_textures(inplace, [tp])
        self.assertEqual(image[:0x10000], inplace[:0x10000])           # nothing moved: the in-place ROM's bytes
        self.assertEqual(self.entry(image, 1), (0xCD00, 0x25))

    def test_slot_moves_with_its_unit(self):
        RL = self.RL
        tp = rombuild.TexPatch(1, 0xCD00, 0x40, bytes(range(1, 0x31)))
        RL.apply_patches(self.units, self.config([tp])[1], self.base)
        pad = RL.RomUnit(None, "pad", "pad")
        pad.data, pad.align = b"\xEE" * 16, 16
        image = self.build([(0xCD00, pad)])
        at, size = self.entry(image, 1)
        self.assertEqual((at, size), (0xCD10, 0x30))                    # the table follows the slot, size kept
        self.assertEqual(image[at:at + 0x40], tp.slot_bytes())
        self.assertEqual(self.entry(image, 0), (0xCCE0, 0x18))
        self.assertEqual(self.entry(image, 2), (0xCD50, 0x30))
        self.assertEqual(image[0xCD50:0xCD88], self.base[0xCD40:0xCD78])
        runs = RL.map_runs(self.units)
        self.assertTrue(RL.map_needed(runs))
        self.assertEqual(runs[0][:3], (0, 0xCD00, 0))
        self.assertEqual(runs[1][:3], (0xCD00, 0x800000, 0xCD10))       # the port's map: the slot's original offset

    def test_level_that_fits_spans_units(self):
        class F:
            start, kind, name = 0xCCE0, "level", "x"

        class P:
            file, changed, slot = F, True, 0x60
            blob = b"\x77" * 0x50
        cfg, patches = self.config([], in_place=[P])
        self.assertEqual([(p[0], len(p[1])) for p in patches], [(0xCCE0, 0x60)])
        self.RL.apply_patches(self.units, patches, self.base)
        image = self.build()
        self.assertEqual(image[0xCCE0:0xCD40], P.blob + bytes(0x10))
        self.assertEqual(image[0xCD40:0xCD78], self.base[0xCD40:0xCD78])

    def test_refused(self):
        RL = self.RL
        with self.assertRaises(ValueError):
            rombuild.TexPatch(1, 0xCD00, 0x40, bytes(0x41))             # bigger than the slot's room
        bad = [[(0xCD00, bytes(0x30), "t", 1)],                           # not the slot's room (made for another ROM)
               [(0xCD04, bytes(0x40), "t", 1)],                           # not the slot's place
               [(0xCD90, b"\1", "code", None)],                          # in a unit made from the code
               [(0xCD70, b"\1" * 16, "gap", None)],                      # non-zero bytes in an alignment gap
               [(0x7FFFFF, b"\1\1", "end", None)]]                        # past the original ROM
        for p in bad:
            with self.assertRaises(SystemExit, msg=p[0][2]):
                RL.apply_patches(self.units, p, self.base)
        RL.apply_patches(self.units, [(0xCD70, bytes(16), "gap", None)], self.base)   # zeros over a gap: fine
        self.units[4].data = b"\1" * 5                                   # a replaced unit
        with self.assertRaises(SystemExit):
            RL.apply_patches(self.units, [(0xCD40, bytes(0x38), "t", 2)], self.base)

    def test_grown_slot(self):
        """a slot past its room: its texture's file replaced by the packed bytes rounded up to 8, the textures after
        it moved, the table following; its own DMA (to the next entry) reads it whole; the port's map says so"""
        from bcfiles import mod
        RL = self.RL
        g = mod.TexPlan("grow", 1, 0xCD00, 0x40, bytes(range(1, 0x46)), 1)
        self.assertEqual(g.grown_room(), 0x48)
        with tempfile.TemporaryDirectory() as d:
            path = rombuild.shift_config([], d, grown=[g])
            with open(path) as fh:
                cfg = json.load(fh)
            rep = cfg["rom"]["replace"]
            self.assertEqual([r["at"] for r in rep], ["0x00CD00"])
            with open(rep[0]["path"], "rb") as fh:
                self.units[3].data = fh.read()
            patches = RL.load_patches(cfg["rom"])
        self.assertEqual([(p[0], p[1], p[3]) for p in patches], [(self.T + 12, b"\x00\x45", 1)])
        RL.apply_patches(self.units, patches, self.base)
        image = self.build()
        self.assertEqual(self.entry(image, 1), (0xCD00, 0x45))
        self.assertEqual(self.entry(image, 2), (0xCD50, 0x30))            # (t2 keeps its 16-byte alignment)
        self.assertEqual(image[0xCD00:0xCD48], g.packed + bytes(3))
        self.assertEqual(image[0xCD50:0xCD88], self.base[0xCD40:0xCD78])
        runs = RL.map_runs(self.units)
        self.assertEqual(runs[0], (0, 0xCD40, 0, 0xCD48))                 # t1's data ends at 0xCD48: read whole
        self.assertEqual(runs[1][:3], (0xCD40, 0x800000, 0xCD50))

    def test_new_slots(self):
        """new slots: in place in the ROM's free tail (contiguous, in id order: an entry's DMA is its bytes), their
        entries and the next one's offset pointed there; on the shift path an added file the table points into"""
        from bcfiles import mod
        RL = self.RL
        a = mod.TexPlan("new", 10, None, 0, bytes([0x21]) * 0x18, 0)
        b = mod.TexPlan("new", 11, None, 0, bytes([0x42]) * 0x10, 0)
        c = mod.TexPlan("new", 20, None, 0, bytes([0x63]) * 0x08, 0)
        data = bytearray(self.base)
        data[0x7FF000:0x800000] = b"\xFF" * 0x1000
        at = rombuild.apply_new_slots(data, [c, a, b])
        self.assertEqual(at, 0x7FF000)
        self.assertEqual(self.entry(data, 10), (0x7FF000, 0x18))
        self.assertEqual(self.entry(data, 11), (0x7FF018, 0x10))
        self.assertEqual(self.entry(data, 12)[0], 0x7FF028)              # (11's DMA ends there)
        self.assertEqual(self.entry(data, 20), (0x7FF028, 0x08))
        self.assertEqual(self.entry(data, 21)[0], 0x7FF030)
        self.assertEqual(self.entry(data, 9), (0xCD78, 0))                # (the empty entries around: as they were)
        self.assertEqual(data[0x7FF000:0x7FF030], a.packed + b.packed + c.packed)
        full = bytearray(self.base)
        full[0x7FFFF0:0x800000] = b"\xFF" * 16
        self.assertIsNone(rombuild.apply_new_slots(full, [a]))           # (no room: the shift path)
        with tempfile.TemporaryDirectory() as d:
            with open(rombuild.shift_config([], d, new=[a, b, c])) as fh:
                cfg = json.load(fh)
            add = cfg["rom"]["add"]
            self.assertEqual([x["name"] for x in add], ["texnew"])
            with open(add[0]["path"], "rb") as fh:
                blob = fh.read()
        self.assertEqual(blob, a.packed + b.packed + c.packed)
        self.assertEqual(cfg["rom"]["texnew"], [{"id": 10, "at": 0, "size": 0x18}, {"id": 11, "at": 0x18, "size": 0x10},
                                               {"id": 20, "at": 0x28, "size": 0x08}])
        u = RL.RomUnit(None, "bin", "texnew")
        u.data, u.align = blob, 16
        seq, end = RL.place(self.units, adds=[(0xCD40, u)])                # (before t2 here: the trailer in a ROM)
        m = RL.RomMapper(self.units)
        tu = m.unit_of(self.T)
        tu.data = RL.apply_texnew(RL.rewrite_tex_table(tu.data, m, tu.new_start), self.base, u.new_start, tu.new_start,
                                  cfg["rom"]["texnew"])
        image = RL.write_rom(seq, end)
        self.assertEqual(self.entry(image, 10), (u.new_start, 0x18))
        self.assertEqual(self.entry(image, 11), (u.new_start + 0x18, 0x10))
        self.assertEqual(self.entry(image, 12)[0], u.new_start + 0x28)
        self.assertEqual(self.entry(image, 20), (u.new_start + 0x28, 0x08))
        self.assertEqual(image[u.new_start:u.new_start + 0x30], blob)
        self.assertEqual(self.entry(image, 2), (u.new_start + 0x30 + (-(u.new_start + 0x30) % 16), 0x30))
        with self.assertRaises(SystemExit):                                # 3: the entry before is a texture's end
            RL.apply_texnew(tu.data, self.base, u.new_start, tu.new_start, [{"id": 3, "at": 0, "size": 8}])

    def test_new_slot_needs_empty_neighbours(self):
        from bcfiles import mod, texture as tx

        class R:
            tex_table = [tx.TexEntry(0, 0x9000, 0x20, 1, 0x20)] + [tx.TexEntry(i, 0x9020, 0, 0, 0) for i in range(1, 8)]
        png64 = base64.b64encode(png.encode_rgba(1, 1, bytes(4))).decode()
        t = {"id": 1, "new": True, "fmt": 0, "siz": 16, "png": png64, "slot_sha1": mod.sha1(b"")}
        with self.assertRaises(mod.ModError) as c:
            mod.texture_plans(R, {"textures": [t]})                        # 0 is a texture: its DMA would run on
        self.assertIn("empty ones before and after", str(c.exception))
        t["id"] = 2
        p = mod.texture_plans(R, {"textures": [t]})[0]
        self.assertEqual((p.kind, p.index, p.size, p.packed), ("new", 2, 8, bytes(8)))


# ------------------------------------------------------------------ ROM

@unittest.skipUnless(ROM_PATH or "--rom" in sys.argv, "no --rom")
class TestRom(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        from bcfiles import Rom
        cls.rom = Rom.load(ROM_PATH)

    def test_roundtrip_every_file(self):
        from bcfiles.cli import roundtrip
        res, bad = roundtrip(self.rom)
        print("\n  %s: %s" % (self.rom.version, "; ".join("%s %s" % (k, " ".join("%s=%d" % kv for kv in r.items()))
                                                         for k, r in res.items())))
        self.assertEqual(bad, [])
        self.assertEqual(res["level"]["files"], 60)
        self.assertEqual(res["model"]["files"], 21)
        self.assertEqual(res["scene"]["files"], 3)
        for r in res.values():
            self.assertEqual(r["bytes_equal"], r["files"])
            self.assertEqual(r["typed"], r["files"])

    def test_lzss_files(self):
        n = 0
        for f in self.rom.files():
            if f.kind != "lzss":
                continue
            data, end = self.rom.lzss_file(f)
            self.assertGreater(len(data), 0)
            c = lzss.compress(data, f.info["bits"])
            self.assertEqual(lzss.decompress(c, f.info["bits"])[0], data, f.name)
            n += 1
        self.assertEqual(n, 6)

    def test_layout_walk(self):
        """the walk finds every file where the code's constants say it is"""
        from bcfiles import romfs
        for n, s in enumerate(romfs.LEVEL_ROM):
            self.assertEqual(self.rom.level(n).start, s)
            nxt = romfs.LEVELS_END if n == 59 else min(x for x in romfs.LEVEL_ROM if x > s)
            self.assertEqual(self.rom.level(n).info["slot_end"], nxt)
        for i, (s, e, types) in enumerate(romfs.MODEL_ROM):
            self.assertEqual((self.rom.model(i).start, self.rom.model(i).info["slot_end"]), (s, e))
            self.assertEqual(self.rom.model(i).info["types"], types)
        self.assertEqual(self.rom.obj_table_rom, romfs.OBJ_TABLE)
        self.assertEqual(self.rom.static_rom, romfs.STATIC_SEGMENT)
        self.assertEqual(self.rom.code_rom, romfs.HD_CODE)
        scenes = [f for f in self.rom.files() if f.kind == "scene"]
        self.assertEqual([(f.start, f.info["slot_end"]) for f in scenes][:2], romfs.SCENE_ROM[:2])
        lz = [(f.start, f.info["bits"]) for f in self.rom.files() if f.kind == "lzss"]
        self.assertEqual(lz, [(s, b) for s, e, b, w in romfs.LZSS_FILES])

    def test_texture_table(self):
        t = self.rom.tex_table
        self.assertEqual(len(t), 4096)
        n = 0
        for e in t:
            if e.type <= 6 and e.size and e.dma_len >= e.size:
                texture.decode(self.rom.texture_packed(e.index), e.type)
                n += 1
        self.assertGreater(n, 3000)

    def test_unchanged_build(self):
        from bcfiles.cli import PARSERS
        patches = []
        for f in [self.rom.level(n) for n in range(60)] + [x for x in self.rom.files() if x.kind in PARSERS and x.kind != "level"]:
            p = PARSERS[f.kind].parse(f.data, f.split)
            data, split = p.write()
            blob = rombuild.build_members(f, data, split)
            self.assertEqual(blob, self.rom.data[f.start:f.end], f.name)
            patches.append(rombuild.Patch(f, blob, data != f.data))
        image, rest = rombuild.apply_in_place(self.rom.data, patches)
        self.assertEqual(rest, [])
        self.assertEqual(image, self.rom.data)

    def test_edit_in_place(self):
        f = self.rom.level(1)
        lv = L.Level.parse(f.data, f.split)
        lv["objects"][0].x += 256
        data, split = lv.write()
        self.assertEqual(split, f.split)
        blob, how = rombuild.pack_to_fit(f, data, split)
        pt = rombuild.Patch(f, blob, True, how)
        print("\n  level 1, object 0 moved: %d compressed bytes (%s), slot %d" % (len(blob), how, pt.slot))
        self.assertTrue(pt.fits)
        image, rest = rombuild.apply_in_place(self.rom.data, [pt])
        from bcfiles import Rom
        r2 = Rom(image)
        lv2 = L.Level.parse(r2.level(1).data, r2.level(1).split)
        self.assertEqual(lv2["objects"][0].x, lv["objects"][0].x)
        self.assertEqual(struct.unpack_from(">II", image, 0x10), rombuild.cic6102_crc(image))


if __name__ == "__main__":
    if "--rom" in sys.argv:
        i = sys.argv.index("--rom")
        ROM_PATH = sys.argv[i + 1]
        del sys.argv[i:i + 2]
        TestRom.__unittest_skip__ = False
    unittest.main(verbosity=1)
