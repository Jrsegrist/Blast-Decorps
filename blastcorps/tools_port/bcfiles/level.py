"""Level files (docs/systems/levels.md): parse into typed sections, write back.

A level is two gzip members inflated back to back: the data (header,
segment-8 vertices, the sections) and the display-list block, which starts
exactly where the first member ends (header word 0x78).  The header is a
chain of s32 offsets; each section runs from its offset to the next one in
the chain.  Section order (every US level):

  0xC8 vertices | 0x20 crates | 0x24 guard tris | 0x28 comm points |
  0x2C texture animations | 0x30 ground grid | 0x34 RDUs | 0x38 TNT |
  0x3C push blocks | 0x40 camera zones | 0x44 chopper zones |
  0x48 visibility grid | 0x4C bounds | 0x50 vehicle spawns | 0x54 carrier |
  0x58 panel events | 0x5C objects | 0x60 alpha zones | 0x64 switch groups |
  0x68 gate groups | 0x6C collision grid A | 0x70 collision grid B |
  0x74 moving-part groups | 0xA0-0xAC visibility records (4 lists) |
  0xB0-0xBC visibility groups (4 lists) | 0xC0 (padding) | 0x78 display lists

Inside the display-list block the words 0x7C, 0x80, 0x88, 0x8C, 0x90-0x9C
and 0x84 (its end) point at lists; they are kept relative to the block.
The visibility records and groups hold absolute file offsets into the
block; they are kept relative to it too and fixed up when it moves.

Bytes a section holds beyond its records are kept as its `tail`; padding
that only aligns the next section is recomputed.  A section whose bytes
don't parse is kept whole as raw bytes, so every file round-trips.
"""
from .binio import (FormatError, Record, Tri9, Vtx, be16, be32, hexs, pack_records, unhex)
import struct

# ---------------------------------------------------------------- records


class Crate(Record):           # 0x20 crate_spawn_all (BoxSpawn)
    FIELDS = [("x", "h"), ("y", "h"), ("z", "h"), ("type", "h")]


class GuardTri(Record):        # 0x24 ground_scan_guard_tris
    FIELDS = Tri9.FIELDS + [("w", "H")]


class CommPoint(Record):       # 0x28 commpt_setup
    FIELDS = [("x", "h"), ("y", "h"), ("z", "h"), ("w", "H")]


class GroundTri(Record):       # 0x30 floor triangles (ground_scan_grid)
    FIELDS = Tri9.FIELDS + [("surface", "B"), ("flag", "B")]


class Rdu(Record):             # 0x34 rdu_init
    FIELDS = [("x", "h"), ("y", "h"), ("z", "h")]


class Tnt(Record):             # 0x38 tnt_spawn_all
    FIELDS = [("x", "h"), ("y", "h"), ("z", "h"), ("type", "B"), ("timer", "B"), ("unk8", "h"), ("unkA", "h")]


class Block(Record):           # 0x3C block_spawn_all: push blocks
    FIELDS = [("x", "h"), ("y", "h"), ("z", "h"), ("model", "h")]


class CollTri(Record):         # 0x6C/0x70 grids, 0x3C slot triangles (0x16 bytes)
    FIELDS = Tri9.FIELDS + [("heading", "H"), ("side", "B"), ("b15", "B")]


class HeightZone(Record):      # 0x40 / 0x44
    FIELDS = [("minx", "h"), ("minz", "h"), ("maxx", "h"), ("maxz", "h"), ("height", "h")]


class Bounds(Record):          # 0x4C
    FIELDS = [("minx", "h"), ("minz", "h"), ("maxx", "h"), ("maxz", "h")]


class VehSpawn(Record):        # 0x50 lvl_spawn_vehicles (bytewise BE)
    FIELDS = [("type", "B"), ("x", "h"), ("y", "h"), ("z", "h"), ("heading", "h")]


class Carrier(Record):         # 0x54 lvl_spawn_carrier
    FIELDS = [("speed", "B"), ("x", "h"), ("z", "h"), ("heading", "h"), ("limit", "h")]


class PanelEvent(Record):      # 0x58 dig_coach_update (CarrierPanelEvent)
    FIELDS = [("z", "h"), ("a", "B"), ("b", "B"), ("c", "B"), ("d", "B"), ("done", "B"), ("pad", "B")]


class ObjPlacement(Record):    # 0x5C lvl_spawn_objects
    FIELDS = [("x", "H"), ("y", "H"), ("z", "H"), ("type", "H"), ("path", "B"), ("people", "B"),
              ("motion", "H"), ("tag", "H")]


class SwitchTri(Record):       # 0x64 (0x14 bytes)
    FIELDS = Tri9.FIELDS + [("heading", "H")]


class GateTri(Record):         # 0x68 (0x15 bytes)
    FIELDS = Tri9.FIELDS + [("heading", "H"), ("id", "B")]


class GateArea(Record):        # 0x68 kind 0: a 2-D triangle
    FIELDS = [("x0", "h"), ("z0", "h"), ("x1", "h"), ("z1", "h"), ("x2", "h"), ("z2", "h")]


class PartGroupHead(Record):   # 0x74 group header (0x2C bytes)
    FIELDS = [("next", "h")] + [("u%X" % (2 + 2 * i), "h") for i in range(9)] + \
             [("mtx%d" % i, "H") for i in range(10)] + [("count", "i")]


class PartItem(Record):        # 0x74 item (0x44 bytes)
    FIELDS = [("i%d" % i, "h") for i in range(9)] + [("b12", "B"), ("b13", "B")] + \
             sum([[("mtx%d" % k, "i"), ("bx%d" % k, "i"), ("by%d" % k, "i"), ("bz%d" % k, "i")] for k in range(3)], [])


class VisRecord(Record):       # 0xA0.. (offsets relative to the display-list block)
    FIELDS = [("start", "i"), ("alt", "i"), ("end", "i"), ("id", "i"), ("cell", "i")]


class TexAnimHead(Record):     # 0x2C (lvl_resolve_tex_ids, lvl_step_tex_anims, lvl_find_tex_anim)
    FIELDS = [("id", "I"), ("frames", "B"), ("frame", "B"), ("fade", "B"), ("alpha", "B"),
              ("period", "H"), ("timer", "H")]


HEADER_U16 = ["vis_cols", "vis_rows", "vis_cell_w", "vis_cell_d", "ground_cols", "ground_rows",
              "ground_cell_w", "ground_cell_d", "coll_cols", "coll_rows", "coll_cell_w", "coll_cell_d"]

# chain order and the alignment each section needs (fields read natively)
CHAIN = [0x20, 0x24, 0x28, 0x2C, 0x30, 0x34, 0x38, 0x3C, 0x40, 0x44, 0x48, 0x4C, 0x50, 0x54, 0x58, 0x5C, 0x60,
         0x64, 0x68, 0x6C, 0x70, 0x74, 0xA0, 0xA4, 0xA8, 0xAC, 0xB0, 0xB4, 0xB8, 0xBC, 0xC0, 0x78]
ALIGN = {0x20: 2, 0x24: 2, 0x28: 2, 0x2C: 4, 0x30: 2, 0x34: 2, 0x38: 2, 0x3C: 2, 0x40: 2, 0x44: 2, 0x48: 2,
         0x4C: 2, 0x50: 1, 0x54: 1, 0x58: 2, 0x5C: 2, 0x60: 1, 0x64: 1, 0x68: 1, 0x6C: 1, 0x70: 1, 0x74: 4,
         0xA0: 4, 0xA4: 4, 0xA8: 4, 0xAC: 4, 0xB0: 4, 0xB4: 4, 0xB8: 4, 0xBC: 4, 0xC0: 1, 0x78: 16}
DL_SLOTS = [0x7C, 0x80, 0x88, 0x8C, 0x90, 0x94, 0x98, 0x9C]

SECTION_NAMES = {0x20: "crates", 0x24: "guard_tris", 0x28: "comm_points", 0x2C: "tex_anims", 0x30: "ground_grid",
                 0x34: "rdus", 0x38: "tnt", 0x3C: "blocks", 0x40: "cam_zones", 0x44: "chopper_zones",
                 0x48: "vis_grid", 0x4C: "bounds", 0x50: "spawns", 0x54: "carrier", 0x58: "panel_events",
                 0x5C: "objects", 0x60: "alpha_zones", 0x64: "switch_groups", 0x68: "gate_groups",
                 0x6C: "coll_grid_a", 0x70: "coll_grid_b", 0x74: "part_groups", 0xA0: "vis_records_0",
                 0xA4: "vis_records_1", 0xA8: "vis_records_2", 0xAC: "vis_records_3", 0xB0: "vis_groups_0",
                 0xB4: "vis_groups_1", 0xB8: "vis_groups_2", 0xBC: "vis_groups_3", 0xC0: "pre_dl"}


# ------------------------------------------------------- section codecs
# parse(level, buf, lo, hi) -> (value, end of what was parsed)
# write(level, value, pos) -> bytes   (pos: where the section starts, for offsets)

def _arr(cls):
    def parse(lv, b, lo, hi):
        return cls.unpack_array(b, lo, hi)

    def write(lv, v, pos):
        return pack_records(v)

    def tojs(v):
        return [r.to_json() for r in v]

    def fromjs(j):
        return [cls.from_json(d) for d in j]
    return parse, write, tojs, fromjs


def _one(cls):
    def parse(lv, b, lo, hi):
        if hi - lo < cls.SIZE:
            return None, lo
        return cls.unpack(b, lo), lo + cls.SIZE

    def write(lv, v, pos):
        return v.pack() if v is not None else b""
    return parse, write, (lambda v: v.to_json() if v is not None else None), \
        (lambda j: cls.from_json(j) if j is not None else None)


def _p_texanims(lv, b, lo, hi):
    out = []
    p = lo
    while p + 12 <= hi:
        h = TexAnimHead.unpack(b, p)
        if h.frames == 0:
            break
        n = h.frames - 1
        if p + 12 + 4 * n > hi:
            break
        ids = list(struct.unpack_from(">%dI" % n, b, p + 12))
        out.append({"head": h, "ids": ids})
        p += 12 + 4 * n
    return out, p


def _w_texanims(lv, v, pos):
    return b"".join(a["head"].pack() + struct.pack(">%dI" % len(a["ids"]), *a["ids"]) for a in v)


def _grid(tri_cls, ncells_fn):
    def parse(lv, b, lo, hi):
        n = ncells_fn(lv)
        cells = []
        p = lo
        for _ in range(n):
            if p + 4 > hi:
                raise FormatError("grid cell header past the section end")
            e = lo + be32(b, p)
            if e < p + 4 or e > hi or (e - p - 4) % tri_cls.SIZE:
                raise FormatError("grid cell end 0x%X" % e)
            cells.append(tri_cls.unpack_array(b, p + 4, e)[0])
            p = e
        return cells, p

    def write(lv, v, pos):
        out = bytearray()
        for cell in v:
            body = pack_records(cell)
            out += struct.pack(">I", len(out) + 4 + len(body)) + body
        return bytes(out)

    def tojs(v):
        return [[list(t._v) for t in cell] for cell in v]

    def fromjs(j):
        return [[tri_cls(*t) for t in cell] for cell in j]
    return parse, write, tojs, fromjs


def _p_blocks(lv, b, lo, hi):
    if hi - lo < 2:
        return None, lo
    n = be16(b, lo)
    p = lo + 2
    blocks = []
    for _ in range(n):
        blocks.append(Block.unpack(b, p))
        p += 8
    if p + 2 > hi:
        raise FormatError("push blocks: no slot count")
    m = be16(b, p)
    p += 2
    slots = []
    for _ in range(m):
        x, y, z, model, nt, target = struct.unpack_from(">hhhBBh", b, p)
        p += 10
        tris = [CollTri.unpack(b, p + 0x16 * i) for i in range(nt)]
        p += 0x16 * nt
        slots.append({"x": x, "y": y, "z": z, "model": model, "target": target, "tris": tris})
    if p > hi:
        raise FormatError("push blocks run past the section")
    return {"blocks": blocks, "slots": slots}, p


def _w_blocks(lv, v, pos):
    if v is None:
        return b""
    out = bytearray(struct.pack(">H", len(v["blocks"])) + pack_records(v["blocks"]))
    out += struct.pack(">H", len(v["slots"]))
    for s in v["slots"]:
        out += struct.pack(">hhhBBh", s["x"], s["y"], s["z"], s["model"], len(s["tris"]), s["target"])
        out += pack_records(s["tris"])
    return bytes(out)


def _js_blocks(v):
    if v is None:
        return None
    return {"blocks": [r.to_json() for r in v["blocks"]],
            "slots": [dict((k, s[k]) for k in ("x", "y", "z", "model", "target")) | {"tris": [list(t._v) for t in s["tris"]]}
                      for s in v["slots"]]}


def _fjs_blocks(j):
    if j is None:
        return None
    return {"blocks": [Block.from_json(d) for d in j["blocks"]],
            "slots": [dict((k, s[k]) for k in ("x", "y", "z", "model", "target")) | {"tris": [CollTri(*t) for t in s["tris"]]}
                      for s in j["slots"]]}


def _p_visgrid(lv, b, lo, hi):
    n = lv.header["vis_cols"] * lv.header["vis_rows"]
    if lo + 4 * n > hi:
        raise FormatError("visibility grid shorter than cols * rows")
    return [list(struct.unpack_from(">hh", b, lo + 4 * i)) for i in range(n)], lo + 4 * n


def _w_visgrid(lv, v, pos):
    return b"".join(struct.pack(">hh", *c) for c in v)


def _p_alpha(lv, b, lo, hi):
    if lo >= hi:
        return None, lo
    d = {"default": b[lo], "zones": []}
    p = lo + 1
    while p < hi:
        if p + 10 > hi:
            raise FormatError("alpha zone record past the end")
        x, y, z, r, mode, n = struct.unpack_from(">HHHHBB", b, p)
        p += 10
        ids = list(b[p:p + n])
        p += n
        if p + 2 > hi:
            raise FormatError("alpha zone record past the end")
        d["zones"].append({"x": x, "y": y, "z": z, "radius": r, "mode": mode, "vehicles": ids,
                           "object": b[p], "alpha": b[p + 1]})
        p += 2
    return d, p


def _w_alpha(lv, v, pos):
    if v is None:
        return b""
    out = bytearray([v["default"]])
    for z in v["zones"]:
        out += struct.pack(">HHHHBB", z["x"], z["y"], z["z"], z["radius"], z["mode"], len(z["vehicles"]))
        out += bytes(z["vehicles"]) + bytes((z["object"], z["alpha"]))
    return bytes(out)


def _p_switch(lv, b, lo, hi):
    out = []
    p = lo
    while p < hi:
        tag, nk = b[p], b[p + 1]
        kinds = list(b[p + 2:p + 2 + nk])
        p += 2 + nk
        nt = b[p]
        p += 1
        tris = [SwitchTri.unpack(b, p + 0x14 * i) for i in range(nt)]
        p += 0x14 * nt
        if p > hi:
            raise FormatError("switch group past the end")
        out.append({"tag": tag, "kinds": kinds, "tris": tris})
    return out, p


def _w_switch(lv, v, pos):
    out = bytearray()
    for g in v:
        out += bytes((g["tag"], len(g["kinds"]))) + bytes(g["kinds"]) + bytes((len(g["tris"]),)) + pack_records(g["tris"])
    return bytes(out)


def _js_tris_groups(v):
    return [dict((k, g[k]) for k in g if k != "tris") | {"tris": [list(t._v) for t in g["tris"]]} for g in v]


def _fjs_tris_groups(cls):
    def f(j):
        return [dict((k, g[k]) for k in g if k != "tris") | {"tris": [cls(*t) for t in g["tris"]]} for g in j]
    return f


def _p_gates(lv, b, lo, hi):
    out = []
    p = lo
    while p < hi:
        owner, kind, n = b[p], b[p + 1], b[p + 2]
        p += 3
        if n == 0:
            raise FormatError("gate group with no areas (the game's do-while would run 2^32 times)")
        g = {"owner": owner, "kind": kind}
        if kind == 0:
            g["areas"] = [GateArea.unpack(b, p + 12 * i) for i in range(n)]
            p += 12 * n
            g["word"] = be32(b, p)
            p += 4
        else:
            g["states"] = [be16(b, p + 2 * i) for i in range(n)]
            p += 2 * n
        m = b[p]
        p += 1
        g["tris"] = [GateTri.unpack(b, p + 0x15 * i) for i in range(m)]
        p += 0x15 * m
        k = b[p]
        g["ids"] = list(b[p + 1:p + 1 + k])
        p += 1 + k
        if p > hi:
            raise FormatError("gate group past the end")
        out.append(g)
    return out, p


def _w_gates(lv, v, pos):
    out = bytearray()
    for g in v:
        if g["kind"] == 0:
            out += bytes((g["owner"], 0, len(g["areas"]))) + pack_records(g["areas"]) + struct.pack(">I", g["word"])
        else:
            out += bytes((g["owner"], g["kind"], len(g["states"]))) + b"".join(struct.pack(">H", s) for s in g["states"])
        out += bytes((len(g["tris"]),)) + pack_records(g["tris"]) + bytes((len(g["ids"]),)) + bytes(g["ids"])
    return bytes(out)


def _js_gates(v):
    out = []
    for g in v:
        d = {"owner": g["owner"], "kind": g["kind"]}
        if g["kind"] == 0:
            d["areas"] = [a.to_json() for a in g["areas"]]
            d["word"] = g["word"]
        else:
            d["states"] = g["states"]
        d["tris"] = [list(t._v) for t in g["tris"]]
        d["ids"] = g["ids"]
        out.append(d)
    return out


def _fjs_gates(j):
    out = []
    for d in j:
        g = {"owner": d["owner"], "kind": d["kind"]}
        if d["kind"] == 0:
            g["areas"] = [GateArea.from_json(a) for a in d["areas"]]
            g["word"] = d["word"]
        else:
            g["states"] = d["states"]
        g["tris"] = [GateTri(*t) for t in d["tris"]]
        g["ids"] = d["ids"]
        out.append(g)
    return out


def _p_parts(lv, b, lo, hi):
    if hi - lo < 4:
        return None, lo
    nmtx = struct.unpack_from(">i", b, lo)[0]
    p = lo + 4
    groups = []
    if nmtx != 0:
        while True:
            h = PartGroupHead.unpack(b, p)
            p += PartGroupHead.SIZE
            items = []
            for _ in range(h.count):
                items.append(PartItem.unpack(b, p))
                p += PartItem.SIZE
            if p > hi:
                raise FormatError("part group past the end")
            groups.append({"head": h, "items": items})
            if h.next == -1:
                break
    return {"nmtx": nmtx, "groups": groups}, p


def _w_parts(lv, v, pos):
    if v is None:
        return b""
    out = bytearray(struct.pack(">i", v["nmtx"]))
    for g in v["groups"]:
        h = g["head"]
        h.count = len(g["items"])
        out += h.pack() + pack_records(g["items"])
    return bytes(out)


def _js_parts(v):
    if v is None:
        return None
    return {"nmtx": v["nmtx"], "groups": [{"head": g["head"].to_json(), "items": [it.to_json() for it in g["items"]]}
                                          for g in v["groups"]]}


def _fjs_parts(j):
    if j is None:
        return None
    return {"nmtx": j["nmtx"], "groups": [{"head": PartGroupHead.from_json(g["head"]),
                                           "items": [PartItem.from_json(i) for i in g["items"]]} for g in j["groups"]]}


def _p_visrecs(lv, b, lo, hi):
    out = []
    p = lo
    while p + 0x14 <= hi:
        r = VisRecord.unpack(b, p)
        r.start -= lv._dl_base
        r.alt -= lv._dl_base
        r.end -= lv._dl_base
        out.append(r)
        p += 0x14
    return out, p


def _w_visrecs(lv, v, pos):
    out = bytearray()
    for r in v:
        out += struct.pack(">iiiii", r.start + lv._dl_base, r.alt + lv._dl_base, r.end + lv._dl_base, r.id, r.cell)
    return bytes(out)


def _p_visgroups(lv, b, lo, hi):
    out = []
    p = lo
    while p + 0x14 <= hi:
        s, a, e, i, n = struct.unpack_from(">iiiii", b, p)
        if n < 0 or p + 0x14 + 4 * n > hi:
            raise FormatError("visibility group past the end")
        cells = list(struct.unpack_from(">%di" % n, b, p + 0x14))
        out.append({"start": s - lv._dl_base, "alt": a - lv._dl_base, "end": e - lv._dl_base, "id": i, "cells": cells})
        p += 0x14 + 4 * n
    return out, p


def _w_visgroups(lv, v, pos):
    out = bytearray()
    for g in v:
        out += struct.pack(">iiiii", g["start"] + lv._dl_base, g["alt"] + lv._dl_base, g["end"] + lv._dl_base, g["id"],
                           len(g["cells"]))
        out += struct.pack(">%di" % len(g["cells"]), *g["cells"])
    return bytes(out)


def _p_none(lv, b, lo, hi):
    return None, lo


def _ident(v):
    return v


def _js_texanims(v):
    return [a["head"].to_json() | {"ids": a["ids"]} for a in v]


def _fjs_texanims(j):
    return [{"head": TexAnimHead.from_json(a), "ids": a["ids"]} for a in j]


def _tri_list(cls):
    p, w, _, _ = _arr(cls)
    return p, w, (lambda v: [list(t._v) for t in v]), (lambda j: [cls(*t) for t in j])


CODECS = {
    0x20: _arr(Crate),
    0x24: _tri_list(GuardTri),
    0x28: _arr(CommPoint),
    0x2C: (_p_texanims, _w_texanims, _js_texanims, _fjs_texanims),
    0x30: _grid(GroundTri, lambda lv: lv.header["ground_cols"] * lv.header["ground_rows"]),
    0x34: _arr(Rdu),
    0x38: _arr(Tnt),
    0x3C: (_p_blocks, _w_blocks, _js_blocks, _fjs_blocks),
    0x40: _arr(HeightZone),
    0x44: _arr(HeightZone),
    0x48: (_p_visgrid, _w_visgrid, _ident, _ident),
    0x4C: _one(Bounds),
    0x50: _arr(VehSpawn),
    0x54: _one(Carrier),
    0x58: _arr(PanelEvent),
    0x5C: _arr(ObjPlacement),
    0x60: (_p_alpha, _w_alpha, _ident, _ident),
    0x64: (_p_switch, _w_switch, _js_tris_groups, _fjs_tris_groups(SwitchTri)),
    0x68: (_p_gates, _w_gates, _js_gates, _fjs_gates),
    0x6C: _grid(CollTri, lambda lv: lv.header["coll_cols"] * lv.header["coll_rows"]),
    0x70: _grid(CollTri, lambda lv: lv.header["coll_cols"] * lv.header["coll_rows"]),
    0x74: (_p_parts, _w_parts, _js_parts, _fjs_parts),
    0xC0: (_p_none, lambda lv, v, pos: b"", _ident, _ident),
}
for _s in (0xA0, 0xA4, 0xA8, 0xAC):
    CODECS[_s] = (_p_visrecs, _w_visrecs, lambda v: [r.to_json() for r in v], lambda j: [VisRecord.from_json(d) for d in j])
for _s in (0xB0, 0xB4, 0xB8, 0xBC):
    CODECS[_s] = (_p_visgroups, _w_visgroups, _ident, _ident)


class Section:
    __slots__ = ("slot", "value", "tail", "raw")

    def __init__(self, slot, value=None, tail=b"", raw=None):
        self.slot, self.value, self.tail, self.raw = slot, value, tail, raw

    @property
    def name(self):
        return SECTION_NAMES[self.slot]


class Level:
    """A parsed level.  header: dict of the named header fields; vertices:
    [Vtx] (segment 8); sections: {slot: Section}; dl: the display-list block
    (bytes); dl_ptrs: {slot: offset in dl}; members: gzip metadata."""

    def __init__(self):
        self.header = {}
        self.vertices = []
        self.vtx_tail = b""
        self.sections = {}
        self.dl = b""
        self.dl_ptrs = {}
        self.dl_trailer = None   # None: zero padding to 16 bytes after the lists (as in every level)
        self.unk_c4 = 0
        self.raw = None          # whole file, when the header chain itself doesn't parse
        self.raw_split = 0
        self.notes = []
        self._dl_base = 0

    def __getitem__(self, name):
        for s in self.sections.values():
            if s.name == name:
                return s.value
        raise KeyError(name)

    # ------------------------------------------------------------- parse
    @classmethod
    def parse(cls, data, split=None):
        """data: the inflated level (both members back to back); split: the
        first member's length (default: header word 0x78)."""
        lv = cls()
        b = bytes(data)
        try:
            lv._parse(b, split)
        except (FormatError, struct.error, IndexError) as e:
            lv = cls()
            lv.raw = b
            lv.raw_split = split if split is not None else len(b)
            lv.notes.append("opaque: %s" % e)
        return lv

    def _parse(self, b, split):
        if len(b) < 0xC8:
            raise FormatError("shorter than the 0xC8-byte header")
        u = struct.unpack_from(">12H", b, 0)
        self.header = dict(zip(HEADER_U16, u))
        self.header["gravity"], self.header["dl_profile"] = struct.unpack_from(">ii", b, 0x18)
        hdr = {s: be32(b, s) for s in range(0x20, 0xC8, 4)}
        self.unk_c4 = hdr[0xC4]
        offs = [hdr[s] for s in CHAIN]
        if offs != sorted(offs) or offs[0] < 0xC8 or hdr[0x84] < hdr[0x78] or hdr[0x84] > len(b):
            raise FormatError("header offsets are not one ascending chain")
        if split is not None and split != hdr[0x78]:
            raise FormatError("first member ends at 0x%X, not at the display-list block (0x%X)" % (split, hdr[0x78]))
        base = self._dl_base = hdr[0x78]
        # vertices
        nv = (hdr[0x20] - 0xC8) // 16
        self.vertices = [Vtx.unpack(b, 0xC8 + 16 * i) for i in range(nv)]
        self.vtx_tail = b[0xC8 + 16 * nv:hdr[0x20]]
        for i, s in enumerate(CHAIN[:-1]):
            lo, hi = hdr[s], hdr[CHAIN[i + 1]]
            parse = CODECS[s][0]
            nxt_align = ALIGN[CHAIN[i + 1]]
            try:
                val, end = parse(self, b, lo, hi)
                if end > hi:
                    raise FormatError("parsed past the end")
                rest = b[end:hi]
                pad = (-end) % nxt_align
                if len(rest) == pad and not any(rest):
                    rest = b""
                self.sections[s] = Section(s, val, rest)
            except (FormatError, struct.error, IndexError) as e:
                self.sections[s] = Section(s, None, b"", b[lo:hi])
                self.notes.append("%s kept raw: %s" % (SECTION_NAMES[s], e))
        self.dl = b[base:hdr[0x84]]
        self.dl_ptrs = {s: hdr[s] - base for s in DL_SLOTS}
        # the display-list member is zero-padded to 16 bytes past 0x84 (29 of the 60 levels
        # need padding); anything else after 0x84 is kept as it is
        tr = b[hdr[0x84]:]
        self.dl_trailer = None if (len(tr) == (-len(b[base:hdr[0x84]])) % 16 and not any(tr)) else tr

    # ------------------------------------------------------------- write
    def write(self):
        """-> (inflated bytes, length of the first member)"""
        if self.raw is not None:
            return self.raw, self.raw_split
        return self._layout()

    def _layout(self):
        # the visibility records hold the block's offset, but no size depends on
        # it: lay out once to find it, then again with it
        for attempt in range(2):
            body = bytearray(0xC8)
            hdr = {}
            body += b"".join(v.pack() for v in self.vertices) + self.vtx_tail
            for i, s in enumerate(CHAIN[:-1]):
                a = ALIGN[s]
                if len(body) % a:
                    body += bytes(-len(body) % a)
                hdr[s] = len(body)
                sec = self.sections[s]
                if sec.raw is not None:
                    body += sec.raw
                else:
                    body += CODECS[s][1](self, sec.value, len(body)) + sec.tail
            if len(body) % 16:
                body += bytes(-len(body) % 16)
            base = len(body)
            if attempt == 0:
                self._dl_base = base
                continue
        hdr[0x78] = base
        for s, rel in self.dl_ptrs.items():
            hdr[s] = base + rel
        hdr[0x84] = base + len(self.dl)
        h = struct.pack(">12H", *[self.header[k] for k in HEADER_U16])
        h += struct.pack(">ii", self.header["gravity"], self.header["dl_profile"])
        for s in range(0x20, 0xC8, 4):
            h += struct.pack(">I", self.unk_c4 if s == 0xC4 else hdr[s])
        body[0:0xC8] = h
        tr = self.dl_trailer if self.dl_trailer is not None else bytes(-len(self.dl) % 16)
        return bytes(body) + self.dl + tr, base

    # ------------------------------------------------------------- JSON
    def to_json(self):
        if self.raw is not None:
            return {"format": "bcfiles.level/1", "raw": hexs(self.raw), "raw_split": self.raw_split, "notes": self.notes}
        j = {"format": "bcfiles.level/1", "header": dict(self.header), "unk_c4": self.unk_c4,
             "vertex_fields": [f[0] for f in Vtx.FIELDS],
             "vertices": [list(v._v) for v in self.vertices], "vertices_tail": hexs(self.vtx_tail), "sections": {}}
        for s in CHAIN[:-1]:
            sec = self.sections[s]
            d = {"slot": "0x%02X" % s}
            if sec.raw is not None:
                d["raw"] = hexs(sec.raw)
            else:
                d["value"] = CODECS[s][2](sec.value)
                if sec.tail:
                    d["tail"] = hexs(sec.tail)
            j["sections"][sec.name] = d
        j["display_lists"] = {"pointers": {"0x%02X" % s: v for s, v in self.dl_ptrs.items()},
                              "commands": ["%08X %08X" % struct.unpack_from(">II", self.dl, q)
                                           for q in range(0, len(self.dl) - 7, 8)],
                              "tail": hexs(self.dl[len(self.dl) & ~7:])}
        if self.dl_trailer is not None:
            j["display_lists"]["trailer"] = hexs(self.dl_trailer)
        j["notes"] = self.notes
        return j

    @classmethod
    def from_json(cls, j):
        lv = cls()
        lv.notes = list(j.get("notes", []))
        if "raw" in j:
            lv.raw = unhex(j["raw"])
            lv.raw_split = j["raw_split"]
            return lv
        lv.header = dict(j["header"])
        lv.unk_c4 = j["unk_c4"]
        lv.vertices = [Vtx(*v) for v in j["vertices"]]
        lv.vtx_tail = unhex(j.get("vertices_tail", ""))
        by_name = {v: k for k, v in SECTION_NAMES.items()}
        for name, d in j["sections"].items():
            s = by_name[name]
            if "raw" in d:
                lv.sections[s] = Section(s, None, b"", unhex(d["raw"]))
            else:
                lv.sections[s] = Section(s, CODECS[s][3](d["value"]), unhex(d.get("tail", "")))
        dl = j["display_lists"]
        lv.dl_ptrs = {int(k, 16): v for k, v in dl["pointers"].items()}
        lv.dl = b"".join(bytes.fromhex(c.replace(" ", "")) for c in dl["commands"]) + unhex(dl.get("tail", ""))
        lv.dl_trailer = unhex(dl["trailer"]) if "trailer" in dl else None
        return lv

    def unexplained(self):
        """bytes kept raw: opaque file, raw sections, tails, odd trailers"""
        if self.raw is not None:
            return len(self.raw)
        n = len(self.vtx_tail) + (len(self.dl_trailer) if self.dl_trailer is not None else 0)
        for s in self.sections.values():
            n += len(s.raw) if s.raw is not None else len(s.tail)
        return n

    # ------------------------------------------------------------- summary
    def summary(self):
        if self.raw is not None:
            return {"opaque": True}
        out = {"vertices": len(self.vertices), "dl_bytes": len(self.dl)}
        for s in CHAIN[:-1]:
            sec = self.sections[s]
            v = sec.value
            if sec.raw is not None:
                out[sec.name] = "raw %d bytes" % len(sec.raw)
            elif isinstance(v, list):
                if v and isinstance(v[0], list) and s in (0x30, 0x6C, 0x70):
                    out[sec.name] = "%d cells, %d tris" % (len(v), sum(len(c) for c in v))
                else:
                    out[sec.name] = len(v)
            elif isinstance(v, dict):
                if s == 0x3C:
                    out[sec.name] = "%d blocks, %d slots" % (len(v["blocks"]), len(v["slots"]))
                elif s == 0x60:
                    out[sec.name] = len(v["zones"])
                elif s == 0x74:
                    out[sec.name] = "%d groups, %d items" % (len(v["groups"]), sum(len(g["items"]) for g in v["groups"]))
            elif v is not None:
                out[sec.name] = 1
            else:
                out[sec.name] = 0
            if sec.tail:
                out[sec.name + ".tail"] = len(sec.tail)
        return out


def parse(data, split=None):
    return Level.parse(data, split)
