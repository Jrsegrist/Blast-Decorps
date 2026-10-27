"""glTF for geometry editing: a level's triangles out (for Blender), and an
edited copy back in as a geometry edit (geometry.Geometry.apply_edit).

The export ("bcfiles geo-gltf") writes one glTF 2.0 file with four meshes:

  render    every drawn triangle of the level (the display-list block), three
            vertices of its own each, grouped into primitives by the run of
            triangles that share their state (same piece, no command between
            them); material name "r<piece>.<run>", textured as gltf.py does
  ground    the floor triangles (the ground grid); material "g.s<surface>.f<flag>"
  walls_a   the walls of collision grid A; material "wall_a"
  walls_b   the walls of collision grid B; material "wall_b"

Every vertex carries where it came from, so a round trip through Blender
(which keeps UV maps on face corners) can say what changed:

  TEXCOORD_0  the texture coordinates (render)
  TEXCOORD_1  (x, z) of the corner in the unedited level (map units)
  TEXCOORD_2  (y of that corner, the triangle's id) - render ids count the
              block's triangles, ground and wall ids their lists
  TEXCOORD_3  walls: (heading, side * 256 + fail byte)
  COLOR_0     the vertex colour (render)

A material's "extras" hold what an added triangle needs: "after" (the id of
the run's last triangle: added triangles go after it, in its state) and the
texture's size and tile values (to turn UVs back into s, t).

The import reads the edited file (.gltf + .bin, or .glb): a triangle whose
three corners carry the same id and that id's three original corners is the
original triangle (corners that moved become moves); any other triangle is
added (render: into the run of its material; ground: with its material's
surface and flag; walls: TEXCOORD_3 or the heading of its normal); ids that
are gone are removed.  Positions are rounded to whole map units.
"""
import base64
import json
import os
import struct

from . import geometry, gltf as G

FLOAT, UBYTE, USHORT, UINT = 5126, 5121, 5123, 5125


class ImportError_(Exception):
    pass


# ------------------------------------------------------------------ export

def export(rom, lv, outdir, name, scale=0.01):
    geo = geometry.Geometry.from_level(lv)
    b = G.Builder(outdir, name, rom, scale)
    b.g["asset"]["extras"] = {"bcfiles_geometry": 1, "scale": scale}
    vbuf = b"".join(v.pack() for v in lv.vertices)
    w = G.walk_linear(vbuf, 8, lv.dl)
    rt = geo.render_tris()
    if len(w.tris) != len(rt):
        raise geometry.GeometryError("the walk found %d triangles, the pieces %d" % (len(w.tris), len(rt)))
    # runs: (piece, the run of triangles in it) -> [triangle ids]
    runs = {}
    order = []
    tid = 0
    for pi, pc in enumerate(geo.pieces):
        run = 0
        last_tri = False
        for o in pc.ops:
            if o[0] in ('t', 'tri'):
                key = (pi, run)
                if key not in runs:
                    runs[key] = []
                    order.append(key)
                runs[key].append(tid)
                tid += 1
                last_tri = True
            elif last_tri:
                run += 1
                last_tri = False
    prims = []
    for key in order:
        ids = runs[key]
        a0, b0, c0, tkey, geom, tscale = w.tris[ids[0]]
        tex = w.textures.get(tkey) if tkey is not None else None
        pos, uv0, uv1, uv2, col, idx = [], [], [], [], [], []
        for t in ids:
            a, bb, c, tk, gm, sc = w.tris[t]
            for v in (a, bb, c):
                idx.append(len(pos))
                pos.append((v[0] * scale, v[1] * scale, v[2] * scale))
                uv0.append(G._uv(v, tex, sc) if tex is not None else (0.0, 0.0))
                uv1.append((float(v[0]), float(v[2])))
                uv2.append((float(v[1]), float(t)))
                col.append(tuple(v[6:10]))
        mat = _material(b, tex, "r%d.%d" % key, {"after": ids[-1], "tex": _tex_extras(tex, tscale)})
        prims.append(_prim(b, pos, idx, mat, uv0=uv0 if tex else None, uv1=uv1, uv2=uv2, col=col))
    _mesh_node(b, "render", prims)
    # ground
    by = {}
    for i, t in enumerate(geo.ground):
        by.setdefault((t[9], t[10]), []).append(i)
    prims = []
    for (sf, fl), ids in sorted(by.items()):
        pos, uv1, uv2, idx = [], [], [], []
        for i in ids:
            t = geo.ground[i]
            for k in range(3):
                x, y, z = t[3 * k:3 * k + 3]
                idx.append(len(pos))
                pos.append((x * scale, y * scale, z * scale))
                uv1.append((float(x), float(z)))
                uv2.append((float(y), float(i)))
        mat = _material(b, None, "g.s%d.f%d" % (sf, fl), {"surface": sf, "flag": fl}, color=(0.2, 0.8, 0.2, 1))
        prims.append(_prim(b, pos, idx, mat, uv1=uv1, uv2=uv2))
    _mesh_node(b, "ground", prims)
    for slot, nm in ((0x6C, "walls_a"), (0x70, "walls_b")):
        pos, uv1, uv2, uv3, idx = [], [], [], [], []
        for i, t in enumerate(geo.walls[slot]):
            for k in range(3):
                x, y, z = t[3 * k:3 * k + 3]
                idx.append(len(pos))
                pos.append((x * scale, y * scale, z * scale))
                uv1.append((float(x), float(z)))
                uv2.append((float(y), float(i)))
                uv3.append((float(t[9]), float(t[10] * 256 + t[11])))
        if pos:
            mat = _material(b, None, "wall_" + nm[-1], {}, color=(0.9, 0.2, 0.2, 1))
            _mesh_node(b, nm, [_prim(b, pos, idx, mat, uv1=uv1, uv2=uv2, uv3=uv3)])
    path = b.save()
    return path, {"render": len(rt), "runs": len(order), "ground": len(geo.ground),
                  "walls_a": len(geo.walls[0x6C]), "walls_b": len(geo.walls[0x70])}


def _tex_extras(tex, tscale):
    if tex is None:
        return None
    return {"width": tex["width"], "height": tex["height"], "shifts": tex["shifts"], "shiftt": tex["shiftt"],
            "uls": tex["uls"], "ult": tex["ult"], "sscale": tscale[0], "tscale": tscale[1]}


def _material(b, tex, name, extras, color=None):
    i = b.material(tex)
    m = dict(b.g["materials"][i])
    m["name"] = name
    m["extras"] = extras
    if color is not None:
        m["pbrMetallicRoughness"] = dict(m["pbrMetallicRoughness"], baseColorFactor=list(color))
    b.g["materials"].append(m)
    return len(b.g["materials"]) - 1


def _prim(b, pos, idx, mat, uv0=None, uv1=None, uv2=None, uv3=None, col=None):
    mn = [min(p[i] for p in pos) for i in range(3)]
    mx = [max(p[i] for p in pos) for i in range(3)]
    attrs = {"POSITION": b._acc(b"".join(struct.pack("<3f", *p) for p in pos), FLOAT, len(pos), "VEC3", mn, mx)}
    for nm, uv in (("TEXCOORD_0", uv0), ("TEXCOORD_1", uv1), ("TEXCOORD_2", uv2), ("TEXCOORD_3", uv3)):
        if uv is not None:
            attrs[nm] = b._acc(b"".join(struct.pack("<2f", *u) for u in uv), FLOAT, len(uv), "VEC2")
    if col is not None:
        attrs["COLOR_0"] = b._acc(b"".join(struct.pack("<4B", *c) for c in col), UBYTE, len(col), "VEC4",
                                  normalized=True)
    ind = b._acc(struct.pack("<%dI" % len(idx), *idx), UINT, len(idx), "SCALAR", target=34963)
    return {"attributes": attrs, "indices": ind, "material": mat, "mode": 4}


def _mesh_node(b, name, prims):
    if not prims:
        return
    b.g["meshes"].append({"name": name, "primitives": prims})
    b.node(len(b.g["meshes"]) - 1, name)


# ------------------------------------------------------------------ import

def _load(path):
    if path.endswith(".glb"):
        data = open(path, "rb").read()
        if data[:4] != b"glTF":
            raise ImportError_("not a .glb")
        p = 12
        j, binc = None, b""
        while p < len(data):
            n, kind = struct.unpack_from("<II", data, p)
            chunk = data[p + 8:p + 8 + n]
            if kind == 0x4E4F534A:
                j = json.loads(chunk.decode("utf-8"))
            elif kind == 0x004E4942:
                binc = chunk
            p += 8 + n
        bufs = [binc]
    else:
        with open(path, "rb") as f:
            j = json.load(f)
        bufs = []
        for bf in j.get("buffers", []):
            uri = bf.get("uri", "")
            if uri.startswith("data:"):
                bufs.append(base64.b64decode(uri.split(",", 1)[1]))
            else:
                with open(os.path.join(os.path.dirname(path), uri), "rb") as f:
                    bufs.append(f.read())
    return j, bufs


def _accessor(j, bufs, i):
    a = j["accessors"][i]
    bv = j["bufferViews"][a["bufferView"]]
    buf = bufs[bv.get("buffer", 0)]
    ncomp = {"SCALAR": 1, "VEC2": 2, "VEC3": 3, "VEC4": 4}[a["type"]]
    ct = a["componentType"]
    fmt, size = {FLOAT: ("f", 4), UBYTE: ("B", 1), USHORT: ("H", 2), UINT: ("I", 4), 5120: ("b", 1),
                 5122: ("h", 2)}[ct]
    stride = bv.get("byteStride") or ncomp * size
    base = bv.get("byteOffset", 0) + a.get("byteOffset", 0)
    out = []
    for k in range(a["count"]):
        v = struct.unpack_from("<%d%s" % (ncomp, fmt), buf, base + k * stride)
        if a.get("normalized") and ct != FLOAT:
            v = tuple(x / {UBYTE: 255.0, USHORT: 65535.0}.get(ct, 1.0) for x in v)
        out.append(v)
    return out


def _triangles(j, bufs, prim):
    at = prim["attributes"]
    pos = _accessor(j, bufs, at["POSITION"])
    get = {k: (_accessor(j, bufs, at[k]) if k in at else None) for k in
           ("TEXCOORD_0", "TEXCOORD_1", "TEXCOORD_2", "TEXCOORD_3", "COLOR_0")}
    if "indices" in prim:
        idx = [v[0] for v in _accessor(j, bufs, prim["indices"])]
    else:
        idx = list(range(len(pos)))
    if prim.get("mode", 4) != 4:
        raise ImportError_("only triangle lists are read (primitive mode %d)" % prim.get("mode", 4))
    tris = []
    for q in range(0, len(idx) - 2, 3):
        tris.append([{"pos": pos[i], **{k: (v[i] if v is not None else None) for k, v in get.items()}}
                     for i in idx[q:q + 3]])
    return tris


def import_edit(path, lv):
    """the edited glTF -> a geometry edit (dict) against the level it was exported from"""
    j, bufs = _load(path)
    scale = (j.get("asset", {}).get("extras") or {}).get("scale")
    if not scale:
        raise ImportError_("%s: no bcfiles geometry export (asset.extras.scale missing)" % path)
    geo = geometry.Geometry.from_level(lv)
    rt = geo.render_tris()
    orig = {"render": [[(v[0], v[1], v[2]) for v in t[2]] for t in rt],
            "ground": [[tuple(t[3 * k:3 * k + 3]) for k in range(3)] for t in geo.ground],
            "walls_a": [[tuple(t[3 * k:3 * k + 3]) for k in range(3)] for t in geo.walls[0x6C]],
            "walls_b": [[tuple(t[3 * k:3 * k + 3]) for k in range(3)] for t in geo.walls[0x70]]}
    # (Blender may rename meshes "render.001" and so on)
    meshes = {}
    for node in j.get("nodes", []):
        if "mesh" in node:
            nm = j["meshes"][node["mesh"]].get("name") or node.get("name", "")
            base = nm.split(".")[0]
            meshes.setdefault(base, []).append(j["meshes"][node["mesh"]])
    moves = {}
    seen = {k: set() for k in orig}
    adds = {"render": [], "ground": [], "walls_a": [], "walls_b": []}

    def rnd(x):
        return int(round(x / scale))

    def note_move(a, bpos):
        if a != bpos:
            if a in moves and moves[a] != bpos:
                raise ImportError_("the corner at (%d, %d, %d) moved to two places: (%d, %d, %d) and (%d, %d, %d) "
                                   "(split vertices?)" % (a + moves[a] + bpos))
            moves[a] = bpos
    for kind in ("render", "ground", "walls_a", "walls_b"):
        for mesh in meshes.get(kind, []):
            for prim in mesh["primitives"]:
                mat = j["materials"][prim["material"]] if "material" in prim else {}
                ex = mat.get("extras") or {}
                for tri in _triangles(j, bufs, prim):
                    newp = [(rnd(c["pos"][0]), rnd(c["pos"][1]), rnd(c["pos"][2])) for c in tri]
                    ident = None
                    if all(c["TEXCOORD_1"] is not None and c["TEXCOORD_2"] is not None for c in tri):
                        ids = set(int(round(c["TEXCOORD_2"][1])) for c in tri)
                        if len(ids) == 1:
                            t = ids.pop()
                            op = [(int(round(c["TEXCOORD_1"][0])), int(round(c["TEXCOORD_2"][0])),
                                   int(round(c["TEXCOORD_1"][1]))) for c in tri]
                            if 0 <= t < len(orig[kind]) and sorted(op) == sorted(orig[kind][t]) and t not in seen[kind]:
                                ident = t
                                for a, bp in zip(op, newp):
                                    note_move(a, bp)
                    if ident is not None:
                        seen[kind].add(ident)
                        continue
                    adds[kind].append((tri, newp, ex))
    edit = {"move": [list(a) + list(b) for a, b in sorted(moves.items())], "remove": {}, "add": {}}
    for kind in orig:
        gone = sorted(set(range(len(orig[kind]))) - seen[kind])
        if gone:
            edit["remove"][kind] = gone
    # added triangles: in their final coordinates
    for tri, newp, ex in adds["render"]:
        if "after" not in ex:
            raise ImportError_("an added drawn triangle has no material of the export (its \"after\")")
        tx = ex.get("tex")
        vs = []
        for c, p in zip(tri, newp):
            s, t = _st(c["TEXCOORD_0"], tx)
            colr = c["COLOR_0"] or (1.0, 1.0, 1.0, 1.0)
            vs.append(list(p) + [s, t] + [max(0, min(255, int(round(x * 255)))) for x in colr[:4]])
        edit["add"].setdefault("render", []).append({"after": ex["after"], "v": vs, "flag": 0})
    for tri, newp, ex in adds["ground"]:
        edit["add"].setdefault("ground", []).append({"v": [q for p in newp for q in p], "surface": ex.get("surface", 1),
                                                     "flag": ex.get("flag", 0)})
    for kind in ("walls_a", "walls_b"):
        for tri, newp, ex in adds[kind]:
            a = {"v": [q for p in newp for q in p]}
            if tri[0]["TEXCOORD_3"] is not None:
                h, sf = tri[0]["TEXCOORD_3"]
                a["heading"], a["side"], a["fail"] = int(round(h)) & 0xFFF, int(round(sf)) >> 8, int(round(sf)) & 255
            edit["add"].setdefault(kind, []).append(a)
    for k in ("remove", "add"):
        if not edit[k]:
            del edit[k]
    if not edit["move"]:
        del edit["move"]
    return edit


def _st(uv, tx):
    """glTF UV -> Vtx s, t (10.5) for the run's texture (gltf._uv inverted)"""
    if uv is None or tx is None:
        return 0, 0
    out = []
    for val, size, sh, ul, sc in ((uv[0], tx["width"], tx["shifts"], tx["uls"], tx["sscale"]),
                                  (uv[1], tx["height"], tx["shiftt"], tx["ult"], tx["tscale"])):
        x = val * max(size, 1) + ul / 4.0
        if 0 < sh <= 10:
            x *= (1 << sh)
        elif sh > 10:
            x /= (1 << (16 - sh))
        f = sc / 65536.0 if sc else 1.0
        out.append(max(-32768, min(32767, int(round(x * 32.0 / f)))))
    return out[0], out[1]
