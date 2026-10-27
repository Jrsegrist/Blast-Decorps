"""Model files: vehicle/marker models and front-end scenes (0x4C-byte header,
lvl_load_model / fe_scene_load) and packed objects (buildings, 0x50-byte
header, lvl_load_object).  docs/systems/files.md sections 4.2 and 4.4.

Both are two gzip members inflated back to back; the second member is the
display-list area.  Headers are s32 offsets from the file start; each
section runs to the next offset of its chain.  Display lists address the
vertices through a segment (6 for models: the vertex section; 9 for
objects: the file + 0x50), so they don't depend on where the vertices sit.
"""
import struct

from .binio import FormatError, Record, Tri9, Vtx, be16, be32, hexs, pack_records, unhex


class _Sectioned:
    """Shared machinery: a chain of header slots, per-slot codecs with
    alignment, typed values or raw bytes, tails."""
    CHAIN = []
    END = None
    ALIGN = {}
    CODECS = {}
    NAMES = {}
    INTERIOR = []          # header slots inside the last section (kept relative to it)
    HEADER_SIZE = 0
    FIRST = 0              # where the first section starts (after the header / vertices)

    def __init__(self):
        self.fields = {}
        self.sections = {}
        self.interior = {}
        self.trailer = None      # None: zero padding to 16 bytes
        self.raw = None
        self.raw_split = 0
        self.notes = []
        self.end_overhang = 0

    def _parse_chain(self, b, hdr):
        offs = [hdr[s] for s in self.CHAIN] + [hdr[self.END]]
        if offs != sorted(offs) or offs[0] < self.HEADER_SIZE:
            raise FormatError("header offsets are not one ascending chain")
        # the end word may point past the file (ninlogo, reflectlogo: by 8 bytes)
        self.end_overhang = max(0, offs[-1] - len(b))
        if self.end_overhang:
            if offs[-2] > len(b):
                raise FormatError("sections past the end of the file")
            offs[-1] = hdr[self.END] = len(b)
            self.notes.append("end word 0x%02X points 0x%X bytes past the file" % (self.END, self.end_overhang))
        for i, s in enumerate(self.CHAIN):
            lo, hi = offs[i], offs[i + 1]
            nxt = self.CHAIN[i + 1] if i + 1 < len(self.CHAIN) else None
            nalign = self.ALIGN.get(nxt, 1) if nxt is not None else 1
            codec = self.CODECS.get(s)
            if codec is None:
                self.sections[s] = [None, b"", b[lo:hi]]
                continue
            try:
                val, end = codec[0](self, b, lo, hi)
                if end > hi:
                    raise FormatError("parsed past the end")
                rest = b[end:hi]
                if len(rest) == (-end) % nalign and not any(rest):
                    rest = b""
                self.sections[s] = [val, rest, None]
            except (FormatError, struct.error, IndexError) as e:
                self.sections[s] = [None, b"", b[lo:hi]]
                self.notes.append("%s kept raw: %s" % (self.NAMES.get(s, "0x%X" % s), e))
        last = self.CHAIN[-1]
        self.interior = {s: hdr[s] - hdr[last] for s in self.INTERIOR}
        # after the end word: zero padding of the display-list member to 16 bytes (None),
        # or kept as it is
        tr = b[hdr[self.END]:]
        self.trailer = None if (len(tr) == (-hdr[self.END]) % 16 and not any(tr)) else tr

    def _write_chain(self, body, hdr):
        for s in self.CHAIN:
            a = self.ALIGN.get(s, 1)
            if len(body) % a:
                body += bytes(-len(body) % a)
            hdr[s] = len(body)
            val, tail, raw = self.sections[s]
            if raw is not None:
                body += raw
            else:
                body += self.CODECS[s][1](self, val, len(body)) + tail
        hdr[self.END] = len(body) + getattr(self, "end_overhang", 0)
        for s, rel in self.interior.items():
            hdr[s] = hdr[self.CHAIN[-1]] + rel
        body += self.trailer if self.trailer is not None else bytes(-len(body) % 16)
        return body

    def _sections_json(self):
        out = {}
        for s in self.CHAIN:
            val, tail, raw = self.sections[s]
            d = {"slot": "0x%02X" % s}
            if raw is not None:
                d["raw"] = hexs(raw)
            else:
                d["value"] = self.CODECS[s][2](val)
                if tail:
                    d["tail"] = hexs(tail)
            out[self.NAMES[s]] = d
        return out

    def _sections_from_json(self, j):
        by = {v: k for k, v in self.NAMES.items()}
        for name, d in j.items():
            s = by[name]
            if "raw" in d:
                self.sections[s] = [None, b"", unhex(d["raw"])]
            else:
                self.sections[s] = [self.CODECS[s][3](d["value"]), unhex(d.get("tail", "")), None]

    def unexplained(self):
        """bytes kept raw: opaque file, raw sections, tails, odd trailers"""
        if self.raw is not None:
            return len(self.raw)
        n = len(getattr(self, "vtx_tail", b"")) + (len(self.trailer) if self.trailer is not None else 0)
        for s, (val, tail, raw) in self.sections.items():
            n += len(raw) if raw is not None else len(tail)
            if isinstance(val, dict) and s == 0x10 and isinstance(self, VehicleModel):
                n += len(bytes.fromhex(val.get("table_tail", ""))) + sum(len(bytes.fromhex(b["tail"])) for b in val["blocks"])
        return n

    def section(self, name):
        by = {v: k for k, v in self.NAMES.items()}
        return self.sections[by[name]][0]

    def dl_bytes(self):
        return self.sections[self.CHAIN[-1]][0]


# -------------------------------------------------------------- codecs

def _c_records(cls):
    return (lambda m, b, lo, hi: cls.unpack_array(b, lo, hi),
            lambda m, v, pos: pack_records(v),
            lambda v: [r.to_list() for r in v],
            lambda j: [cls.from_list(r) for r in j])


def _c_dl():
    def parse(m, b, lo, hi):
        n = (hi - lo) & ~7
        return b[lo:lo + n], lo + n
    return (parse, lambda m, v, pos: v,
            lambda v: ["%08X %08X" % struct.unpack_from(">II", v, q) for q in range(0, len(v), 8)],
            lambda j: b"".join(bytes.fromhex(c.replace(" ", "")) for c in j))


def _c_words():
    def parse(m, b, lo, hi):
        n = (hi - lo) // 4
        return list(struct.unpack_from(">%dI" % n, b, lo)), lo + 4 * n
    return (parse, lambda m, v, pos: struct.pack(">%dI" % len(v), *v), list, list)


def _c_halves(signed=True):
    code = "h" if signed else "H"

    def parse(m, b, lo, hi):
        n = (hi - lo) // 2
        return list(struct.unpack_from(">%d%s" % (n, code), b, lo)), lo + 2 * n
    return (parse, lambda m, v, pos: struct.pack(">%d%s" % (len(v), code), *v), list, list)


def _c_bytes():
    return (lambda m, b, lo, hi: (list(b[lo:hi]), hi), lambda m, v, pos: bytes(v), list, list)


def _c_empty():
    """no records known: everything is the tail (alignment padding recomputed)"""
    return (lambda m, b, lo, hi: (None, lo), lambda m, v, pos: b"", (lambda v: v), (lambda j: j))


# vehicle model point/part lists (veh_points_transform, veh_register_parts): runs of
# {s16 x ns, the last = n; n x s32} to the section end
def _c_runs(ns, lead=0):
    def parse(m, b, lo, hi):
        p = lo
        out = {"lead": list(struct.unpack_from(">%dh" % lead, b, p)) if lead else [], "runs": []}
        p += 2 * lead
        while p + 2 * ns <= hi:
            hs = list(struct.unpack_from(">%dh" % ns, b, p))
            n = hs[-1] & 0xFFFF
            if p + 2 * ns + 4 * n > hi:
                break
            words = list(struct.unpack_from(">%di" % n, b, p + 2 * ns))
            out["runs"].append({"h": hs[:-1], "words": words})
            p += 2 * ns + 4 * n
        return out, p

    def write(m, v, pos):
        o = bytearray(struct.pack(">%dh" % lead, *v["lead"]) if lead else b"")
        for r in v["runs"]:
            o += struct.pack(">%dh" % ns, *(r["h"] + [len(r["words"])]))
            o += struct.pack(">%di" % len(r["words"]), *r["words"])
        return bytes(o)
    return parse, write, (lambda v: v), (lambda j: j)


# {s16 k; s16 n; n x s32; k x ten s16} (decks, veh_build_tris)
def _c_klist():
    def parse(m, b, lo, hi):
        if hi - lo < 4:
            return None, lo
        k, n = struct.unpack_from(">hh", b, lo)
        p = lo + 4
        if k < 0 or n < 0 or p + 4 * n + 20 * k > hi:
            raise FormatError("list past the end")
        words = list(struct.unpack_from(">%di" % n, b, p))
        p += 4 * n
        rows = [list(struct.unpack_from(">10h", b, p + 20 * i)) for i in range(k)]
        return {"words": words, "rows": rows}, p + 20 * k

    def write(m, v, pos):
        if v is None:
            return b""
        o = struct.pack(">hh", len(v["rows"]), len(v["words"])) + struct.pack(">%di" % len(v["words"]), *v["words"])
        return o + b"".join(struct.pack(">10h", *r) for r in v["rows"])
    return parse, write, (lambda v: v), (lambda j: j)


# animation block (anim_init_model, anim_update_channel): s16 offsets (count =
# the first / 2), each to {u8 k; k + 1 bytes; pad to a word} then groups
# {s32; s32; k x ten s16} up to the next animation
def _c_anims():
    def parse(m, b, lo, hi):
        if hi - lo < 2:
            return None, lo
        n = be16(b, lo) // 2
        if n == 0 or lo + 2 * n > hi:
            raise FormatError("animation table")
        offs = list(struct.unpack_from(">%dH" % n, b, lo))
        starts = sorted(set(offs))
        if starts[0] < 2 * n:
            raise FormatError("animation offset inside the table")
        table_tail = b[lo + 2 * n:lo + starts[0]]
        blocks = []
        for i, a in enumerate(starts):
            s = lo + a
            e = lo + starts[i + 1] if i + 1 < len(starts) else hi
            if s + 4 > e:
                # an empty animation (the table's unused entries point at the section end)
                blocks.append({"head": b[s:e].hex(), "groups": [], "tail": ""})
                continue
            k = b[s]
            hl = (k + 2 + 3) & ~3
            if s + hl > e:
                raise FormatError("animation header past the end")
            head = b[s:s + hl]
            p = s + hl
            groups = []
            gl = 8 + 20 * k
            while p + gl <= e:
                a0, a1 = struct.unpack_from(">ii", b, p)
                groups.append({"a": a0, "b": a1, "keys": [list(struct.unpack_from(">10h", b, p + 8 + 20 * j))
                                                          for j in range(k)]})
                p += gl
            blocks.append({"head": head.hex(), "groups": groups, "tail": b[p:e].hex()})
        return {"table": [starts.index(o) for o in offs], "table_tail": table_tail.hex(), "blocks": blocks}, hi

    def write(m, v, pos):
        if v is None:
            return b""
        n = len(v["table"])
        bodies = []
        for blk in v["blocks"]:
            o = bytearray(bytes.fromhex(blk["head"]))
            for g in blk["groups"]:
                o += struct.pack(">ii", g["a"], g["b"]) + b"".join(struct.pack(">10h", *r) for r in g["keys"])
            o += bytes.fromhex(blk["tail"])
            bodies.append(bytes(o))
        tt = bytes.fromhex(v["table_tail"])
        starts = []
        a = 2 * n + len(tt)
        for body in bodies:
            starts.append(a)
            a += len(body)
        return struct.pack(">%dH" % n, *[starts[i] for i in v["table"]]) + tt + b"".join(bodies)
    return parse, write, (lambda v: v), (lambda j: j)


# ------------------------------------------------------------ vehicle models

class VehicleModel(_Sectioned):
    """Vehicle / marker models and front-end scenes (0x4C-byte header)."""
    HEADER_SIZE = 0x4C
    CHAIN = [0x00, 0x04, 0x08, 0x0C, 0x10, 0x14, 0x18, 0x1C]
    END = 0x20
    INTERIOR = [0x24, 0x28, 0x2C, 0x30, 0x34, 0x38, 0x3C, 0x40, 0x44, 0x48]
    ALIGN = {0x00: 4, 0x04: 4, 0x08: 4, 0x0C: 4, 0x10: 4, 0x14: 8, 0x18: 4, 0x1C: 8}
    NAMES = {0x00: "points", 0x04: "parts", 0x08: "decks", 0x0C: "tri_lists", 0x10: "anims", 0x14: "vertices",
             0x18: "matrix_block", 0x1C: "display_lists"}
    CODECS = {0x00: _c_runs(4), 0x04: _c_runs(6, lead=2), 0x08: _c_klist(), 0x0C: _c_klist(), 0x10: _c_anims(),
              0x14: _c_records(Vtx), 0x18: _c_words(), 0x1C: _c_dl()}
    DL_NAMES = {0x24: "main", 0x28: "dl_28", 0x2C: "dl_2C", 0x30: "dl_30", 0x34: "dl_34", 0x38: "dl_38",
                0x3C: "dl_3C", 0x40: "dl_40", 0x44: "dl_44", 0x48: "dl_48"}

    @classmethod
    def parse(cls, data, split=None):
        m = cls()
        b = bytes(data)
        try:
            if len(b) < 0x4C:
                raise FormatError("shorter than the header")
            hdr = {s: be32(b, s) for s in range(0, 0x4C, 4)}
            if split is not None and split != hdr[0x1C]:
                raise FormatError("first member ends at 0x%X, not at the display lists (0x%X)" % (split, hdr[0x1C]))
            m._parse_chain(b, hdr)
        except (FormatError, struct.error, IndexError) as e:
            m = cls()
            m.raw, m.raw_split = b, (split if split is not None else len(b))
            m.notes.append("opaque: %s" % e)
        return m

    def write(self):
        if self.raw is not None:
            return self.raw, self.raw_split
        hdr = {}
        body = self._write_chain(bytearray(0x4C), hdr)
        body[0:0x4C] = struct.pack(">19I", *[hdr[s] for s in range(0, 0x4C, 4)])
        return bytes(body), hdr[0x1C]

    def to_json(self):
        if self.raw is not None:
            return {"format": "bcfiles.model/1", "raw": hexs(self.raw), "raw_split": self.raw_split, "notes": self.notes}
        return {"format": "bcfiles.model/1", "vertex_fields": [f[0] for f in Vtx.FIELDS],
                "sections": self._sections_json(),
                "dl_entries": {self.DL_NAMES[s]: v for s, v in self.interior.items()},
                "end_overhang": self.end_overhang,
                "trailer": None if self.trailer is None else hexs(self.trailer), "notes": self.notes}

    @classmethod
    def from_json(cls, j):
        m = cls()
        m.notes = list(j.get("notes", []))
        if "raw" in j:
            m.raw, m.raw_split = unhex(j["raw"]), j["raw_split"]
            return m
        m._sections_from_json(j["sections"])
        inv = {v: k for k, v in cls.DL_NAMES.items()}
        m.interior = {inv[k]: v for k, v in j["dl_entries"].items()}
        m.end_overhang = j.get("end_overhang", 0)
        m.trailer = None if j.get("trailer") is None else unhex(j["trailer"])
        return m

    def vertices(self):
        return self.sections[0x14][0] or []

    def dl_entries(self):
        """{name: offset in the display-list area}"""
        return {self.DL_NAMES[s]: v for s, v in sorted(self.interior.items())}

    def summary(self):
        if self.raw is not None:
            return {"opaque": True}
        pts = self.section("points")
        prt = self.section("parts")
        return {"vertices": len(self.vertices()), "dl_bytes": len(self.dl_bytes() or b""),
                "point_runs": len(pts["runs"]) if pts else "raw", "part_runs": len(prt["runs"]) if prt else "raw",
                "anims": len(self.section("anims")["blocks"]) if self.section("anims") else 0,
                "dl_entries": len(self.interior)}


# ------------------------------------------------------------ packed objects

class ObjTri(Record):       # 0x24: floor triangles (nine s16 + a u16 not traced here)
    FIELDS = Tri9.FIELDS + [("w", "H")]


class ObjPoint(Record):     # 0x2C: 8-byte part points (obj_spawn_random_effects)
    FIELDS = [("x", "h"), ("y", "h"), ("z", "h"), ("w", "H")]


class Corner(Record):
    FIELDS = [("x", "h"), ("y", "h"), ("z", "h")]


class ObjBounds(Record):
    FIELDS = [("x0", "h"), ("z0", "h"), ("x1", "h"), ("z1", "h")]


class Debris(Record):       # 0x30/0x34: 0x38-byte records (obj_run_effect_queue reads the copy)
    FIELDS = [("x", "i"), ("y", "i"), ("z", "i")] + [("w%X" % (4 * i), "i") for i in range(3, 10)] + \
             [("w28", "i"), ("delay", "H"), ("radius", "H")] + [("b%X" % (0x30 + i), "B") for i in range(8)]


class Tri25(Record):        # 0x48: 0x19-byte triangles (unaligned BE s16)
    FIELDS = Tri9.FIELDS + [("rest", "7s")]


def _c_objtex():
    def parse(m, b, lo, hi):
        out = []
        p = lo
        while p + 0x10 <= hi:
            n = b[p + 4]
            if n == 0 or p + 0x10 + 4 * (n - 1) > hi:
                break
            idv, cnt, b5, b6, b7, w8, hc, he = struct.unpack_from(">IBBBBIhh", b, p)
            ids = list(struct.unpack_from(">%dI" % (n - 1), b, p + 0x10))
            out.append({"id": idv, "b5": b5, "b6": b6, "b7": b7, "w8": w8, "hC": hc, "hE": he, "ids": ids})
            p += 0x10 + 4 * (n - 1)
        return out, p

    def write(m, v, pos):
        return b"".join(struct.pack(">IBBBBIhh", r["id"], len(r["ids"]) + 1, r["b5"], r["b6"], r["b7"], r["w8"],
                                    r["hC"], r["hE"]) + struct.pack(">%dI" % len(r["ids"]), *r["ids"]) for r in v)
    return parse, write, (lambda v: v), (lambda j: j)


# obj_draw_all's part lists: s32 n, n x {s16; u8; u8}; then groups {s32 x 4;
# s32 m; m x {s16; u8; u8}} to the end (the last group may stop after its four words)
def _c_objparts():
    def entries(b, p, hi):
        n = struct.unpack_from(">i", b, p)[0]
        if n < 0 or p + 4 + 4 * n > hi:
            raise FormatError("part entries past the end")
        return [list(struct.unpack_from(">hBB", b, p + 4 + 4 * i)) for i in range(n)], p + 4 + 4 * n

    def parse(m, b, lo, hi):
        if hi - lo < 4:
            return None, lo
        first, p = entries(b, lo, hi)
        groups = []
        while p + 16 <= hi:
            words = list(struct.unpack_from(">4i", b, p))
            p += 16
            if p + 4 <= hi:
                ents, p = entries(b, p, hi)
                groups.append({"words": words, "entries": ents})
            else:
                groups.append({"words": words})
        return {"entries": first, "groups": groups}, p

    def write(m, v, pos):
        if v is None:
            return b""

        def ents(e):
            return struct.pack(">i", len(e)) + b"".join(struct.pack(">hBB", *x) for x in e)
        o = bytearray(ents(v["entries"]))
        for g in v["groups"]:
            o += struct.pack(">4i", *g["words"])
            if "entries" in g:
                o += ents(g["entries"])
        return bytes(o)
    return parse, write, (lambda v: v), (lambda j: j)


# ObjectModel's first 0x10 bytes (object.h; 0x02 is what LevelObject.radius is made from, << 5;
# 0x0E is overwritten with the placement's motion type at load)
OBJ_HEADER = [("num_topple_keys", "H"), ("radius", "H"), ("toughness", "B"), ("debris_scale", "B"), ("people", "B"),
              ("parts_topple", "B"), ("value", "i"), ("padC", "B"), ("immune_part", "B"), ("motion", "H")]


class PackedObject(_Sectioned):
    """A building or other placed object (0x50-byte header, vertices from 0x50)."""
    HEADER_SIZE = 0x50
    CHAIN = [0x1C, 0x20, 0x24, 0x28, 0x2C, 0x30, 0x34, 0x38, 0x3C, 0x40, 0x44, 0x48, 0x4C, 0x10]
    END = 0x14
    INTERIOR = [0x18]
    ALIGN = {0x1C: 2, 0x20: 2, 0x24: 2, 0x28: 4, 0x2C: 2, 0x30: 4, 0x34: 4, 0x38: 4, 0x3C: 2, 0x40: 2, 0x44: 1,
             0x48: 1, 0x4C: 1, 0x10: 16}
    # names after ObjectModel (include/game/object.h) where it names the slot
    NAMES = {0x1C: "marker_points", 0x20: "footprint_box", 0x24: "floor_tris", 0x28: "tex_anims",
             0x2C: "part_points", 0x30: "break_effects", 0x34: "land_effects", 0x38: "part_dl_recs",
             0x3C: "topple_keys", 0x40: "part_heights", 0x44: "support_recs", 0x48: "tris_48", 0x4C: "unk_4c",
             0x10: "display_lists"}
    CODECS = {0x1C: _c_records(Corner), 0x20: _c_records(ObjBounds), 0x24: _c_records(ObjTri), 0x28: _c_objtex(),
              0x2C: _c_records(ObjPoint), 0x30: _c_records(Debris), 0x34: _c_records(Debris),
              0x38: _c_objparts(), 0x3C: _c_halves(), 0x40: _c_halves(), 0x44: _c_bytes(),
              0x48: _c_records(Tri25), 0x4C: _c_empty(), 0x10: _c_dl()}

    def __init__(self):
        super().__init__()
        self.vertices = []
        self.vtx_tail = b""

    @classmethod
    def parse(cls, data, split=None):
        m = cls()
        b = bytes(data)
        try:
            if len(b) < 0x50:
                raise FormatError("shorter than the header")
            hdr = {s: be32(b, s) for s in range(0x10, 0x50, 4)}
            if split is not None and split != hdr[0x10]:
                raise FormatError("first member ends at 0x%X, not at the display lists (0x%X)" % (split, hdr[0x10]))
            vals = struct.unpack_from(">HHBBBBiBBH", b, 0)
            m.fields = dict(zip([f[0] for f in OBJ_HEADER], vals))
            if hdr[0x1C] < 0x50:
                raise FormatError("vertex end before the header end")
            nv = (hdr[0x1C] - 0x50) // 16
            m.vertices = [Vtx.unpack(b, 0x50 + 16 * i) for i in range(nv)]
            m.vtx_tail = b[0x50 + 16 * nv:hdr[0x1C]]
            m._parse_chain(b, hdr)
        except (FormatError, struct.error, IndexError) as e:
            m = cls()
            m.raw, m.raw_split = b, (split if split is not None else len(b))
            m.notes.append("opaque: %s" % e)
        return m

    def write(self):
        if self.raw is not None:
            return self.raw, self.raw_split
        hdr = {}
        body = bytearray(0x50) + b"".join(v.pack() for v in self.vertices) + self.vtx_tail
        body = self._write_chain(body, hdr)
        body[0:0x10] = struct.pack(">HHBBBBiBBH", *[self.fields[f[0]] for f in OBJ_HEADER])
        body[0x10:0x50] = struct.pack(">16I", *[hdr[s] for s in range(0x10, 0x50, 4)])
        return bytes(body), hdr[0x10]

    def to_json(self):
        if self.raw is not None:
            return {"format": "bcfiles.object/1", "raw": hexs(self.raw), "raw_split": self.raw_split, "notes": self.notes}
        return {"format": "bcfiles.object/1", "header": dict(self.fields), "vertex_fields": [f[0] for f in Vtx.FIELDS],
                "vertices": [list(v._v) for v in self.vertices], "vertices_tail": hexs(self.vtx_tail),
                "sections": self._sections_json(), "dl_entries": {"0x18": self.interior.get(0x18)},
                "trailer": None if self.trailer is None else hexs(self.trailer), "notes": self.notes}

    @classmethod
    def from_json(cls, j):
        m = cls()
        m.notes = list(j.get("notes", []))
        if "raw" in j:
            m.raw, m.raw_split = unhex(j["raw"]), j["raw_split"]
            return m
        m.fields = dict(j["header"])
        m.vertices = [Vtx(*v) for v in j["vertices"]]
        m.vtx_tail = unhex(j.get("vertices_tail", ""))
        m._sections_from_json(j["sections"])
        m.interior = {0x18: j["dl_entries"]["0x18"]}
        m.trailer = None if j.get("trailer") is None else unhex(j["trailer"])
        return m

    def summary(self):
        if self.raw is not None:
            return {"opaque": True}
        out = {"vertices": len(self.vertices), "dl_bytes": len(self.dl_bytes() or b"")}
        for s in self.CHAIN[:-1]:
            val, tail, raw = self.sections[s]
            n = self.NAMES[s]
            if raw is not None:
                out[n] = "raw %d" % len(raw)
            elif isinstance(val, list):
                out[n] = len(val)
            elif isinstance(val, dict):
                out[n] = "%d entries, %d groups" % (len(val["entries"]), len(val["groups"]))
        return out


def parse_model(data, split=None):
    return VehicleModel.parse(data, split)


def parse_object(data, split=None):
    return PackedObject.parse(data, split)
