"""glTF 2.0 export of models, buildings and a level's static geometry.

The display lists are walked the way the RSP would draw them (gbi.Walker);
every G_TRI1 becomes a triangle with the vertex positions, the texture
coordinates (Vtx s, t in 10.5, scaled by G_TEXTURE and the tile's shift,
divided by the tile size) and the vertex colours (or normals when
G_LIGHTING is on).  Triangles are grouped by texture (texture-table id plus
the tile's format, size and wrap modes); each texture is decoded from the
ROM (texture.decode + texture.to_rgba8) and written as a PNG.

Not modelled: the vehicles' part matrices (G_MTX, segment 7, built at run
time: parts are exported where their vertices are), the combiner and blend
modes (textured materials use alpha MASK), fog, texture animations.
"""
import json
import os
import struct

from . import gbi, png, texture

SIZ_BITS = {0: 4, 1: 8, 2: 16, 3: 32}


class TextureCache:
    def __init__(self, rom, outdir):
        self.rom = rom
        self.outdir = outdir
        self.done = {}

    def png_for(self, t):
        """t: the walker's texture dict -> (relative png path, has alpha) or None"""
        key = (t["id"], t["fmt"], t["siz"], t["width"], t["height"], t["tlut"])
        if key in self.done:
            return self.done[key]
        res = None
        tid = t["id"]
        if self.rom is not None and tid is not None and tid < texture.TEX_ENTRIES:
            ent = self.rom.tex_table[tid]
            pal = None
            if t["tlut"] is not None and t["tlut"] < texture.TEX_ENTRIES:
                pal = self.rom.texture(t["tlut"])
            texels = self.rom.texture(tid)
            w, h = t["width"], t["height"]
            bits = SIZ_BITS[t["siz"]]
            if t.get("mips", 1) > 1 and bits != 32:
                # (a mipmapped slot holds its odd rows with their words swapped: texture.mip_texels)
                texels = texture.unswap_rows(texels, w, h, bits)
            if w > 0 and h > 0 and w * h <= 1 << 16:
                rgba = texture.to_rgba8(texels, t["fmt"], bits, w, h, pal)
                name = "tex_%04d_%d%d_%dx%d.png" % (tid, t["fmt"], bits, w, h)
                os.makedirs(os.path.join(self.outdir, "textures"), exist_ok=True)
                png.write_rgba(os.path.join(self.outdir, "textures", name), w, h, rgba)
                alpha = any(rgba[i] != 255 for i in range(3, len(rgba), 4))
                res = ("textures/" + name, alpha, ent.type)
        self.done[key] = res
        return res


def _uv(v, tex, scale):
    s, t = v[4], v[5]
    ss, ts = scale
    fs = (ss / 65536.0 if ss else 1.0)
    ft = (ts / 65536.0 if ts else 1.0)
    u = s * fs / 32.0
    w = t * ft / 32.0

    def shift(x, sh):
        if 0 < sh <= 10:
            return x / (1 << sh)
        if sh > 10:
            return x * (1 << (16 - sh))
        return x
    u = shift(u, tex["shifts"]) - tex["uls"] / 4.0
    w = shift(w, tex["shiftt"]) - tex["ult"] / 4.0
    return u / max(tex["width"], 1), w / max(tex["height"], 1)


def _wrap(cm):
    if cm & 2:
        return 33071      # CLAMP_TO_EDGE
    if cm & 1:
        return 33648      # MIRRORED_REPEAT
    return 10497          # REPEAT


class Builder:
    def __init__(self, outdir, name, rom=None, scale=0.01):
        self.outdir = outdir
        self.name = name
        self.scale = scale
        self.tex = TextureCache(rom, outdir)
        self.bin = bytearray()
        self.g = {"asset": {"version": "2.0", "generator": "bcfiles (Blast Corps decomp tools)"},
                  "scene": 0, "scenes": [{"nodes": []}], "nodes": [], "meshes": [], "accessors": [],
                  "bufferViews": [], "materials": [], "textures": [], "images": [], "samplers": []}
        self.mat_index = {}
        self.stats = {"triangles": 0, "textures": 0, "untextured_tris": 0}

    def _view(self, data, target=None):
        while len(self.bin) % 4:
            self.bin.append(0)
        v = {"buffer": 0, "byteOffset": len(self.bin), "byteLength": len(data)}
        if target:
            v["target"] = target
        self.bin += data
        self.g["bufferViews"].append(v)
        return len(self.g["bufferViews"]) - 1

    def _acc(self, data, ctype, count, typ, mn=None, mx=None, normalized=False, target=34962):
        a = {"bufferView": self._view(data, target), "componentType": ctype, "count": count, "type": typ}
        if mn is not None:
            a["min"], a["max"] = mn, mx
        if normalized:
            a["normalized"] = True
        self.g["accessors"].append(a)
        return len(self.g["accessors"]) - 1

    def material(self, tex):
        key = None if tex is None else (tex["id"], tex["fmt"], tex["siz"], tex["width"], tex["height"], tex["cms"],
                                        tex["cmt"], tex["tlut"])
        if key in self.mat_index:
            return self.mat_index[key]
        m = {"name": "untextured", "pbrMetallicRoughness": {"metallicFactor": 0.0, "roughnessFactor": 1.0},
             "doubleSided": True}
        if tex is not None:
            p = self.tex.png_for(tex)
            m["name"] = "tex%04d" % tex["id"]
            if p is not None:
                self.g["images"].append({"uri": p[0]})
                self.g["samplers"].append({"magFilter": 9729, "minFilter": 9729, "wrapS": _wrap(tex["cms"]),
                                           "wrapT": _wrap(tex["cmt"])})
                self.g["textures"].append({"source": len(self.g["images"]) - 1,
                                           "sampler": len(self.g["samplers"]) - 1})
                m["pbrMetallicRoughness"]["baseColorTexture"] = {"index": len(self.g["textures"]) - 1}
                if p[1]:
                    m["alphaMode"] = "MASK"
                    m["alphaCutoff"] = 0.5
                self.stats["textures"] += 1
        self.g["materials"].append(m)
        self.mat_index[key] = len(self.g["materials"]) - 1
        return self.mat_index[key]

    def mesh(self, walker, name):
        """one mesh from a walker's triangles; returns its index (None if empty)"""
        groups = {}
        for (a, b, c, tkey, geom, scale) in walker.tris:
            groups.setdefault(tkey, []).append((a, b, c, geom, scale))
        prims = []
        for tkey, tris in groups.items():
            tex = walker.textures.get(tkey) if tkey is not None else None
            pos, uvs, cols, nrm, idx = [], [], [], [], []
            seen = {}
            lit_any = any(g & gbi.G_LIGHTING for (_, _, _, g, _) in tris)
            for (a, b, c, geom, scale) in tris:
                lit = geom & gbi.G_LIGHTING
                for v in (a, b, c):
                    uv = _uv(v, tex, scale) if tex is not None else (0.0, 0.0)
                    k = (v, uv, lit)
                    if k not in seen:
                        seen[k] = len(pos)
                        pos.append((v[0] * self.scale, v[1] * self.scale, v[2] * self.scale))
                        uvs.append(uv)
                        if lit:
                            n = [x - 256 if x > 127 else x for x in v[6:9]]
                            ln = (n[0] ** 2 + n[1] ** 2 + n[2] ** 2) ** 0.5 or 1.0
                            nrm.append((n[0] / ln, n[1] / ln, n[2] / ln))
                            cols.append((255, 255, 255, v[9]))
                        else:
                            nrm.append((0.0, 1.0, 0.0))
                            cols.append(v[6:10])
                    idx.append(seen[k])
            self.stats["triangles"] += len(tris)
            if tex is None:
                self.stats["untextured_tris"] += len(tris)
            mn = [min(p[i] for p in pos) for i in range(3)]
            mx = [max(p[i] for p in pos) for i in range(3)]
            attrs = {"POSITION": self._acc(b"".join(struct.pack("<3f", *p) for p in pos), 5126, len(pos), "VEC3", mn, mx),
                     "COLOR_0": self._acc(b"".join(struct.pack("<4B", *c) for c in cols), 5121, len(pos), "VEC4",
                                          normalized=True)}
            if tex is not None:
                attrs["TEXCOORD_0"] = self._acc(b"".join(struct.pack("<2f", *u) for u in uvs), 5126, len(pos), "VEC2")
            if lit_any:
                attrs["NORMAL"] = self._acc(b"".join(struct.pack("<3f", *n) for n in nrm), 5126, len(pos), "VEC3")
            ind = self._acc(struct.pack("<%dI" % len(idx), *idx), 5125, len(idx), "SCALAR", target=34963)
            prims.append({"attributes": attrs, "indices": ind, "material": self.material(tex), "mode": 4})
        if not prims:
            return None
        self.g["meshes"].append({"name": name, "primitives": prims})
        return len(self.g["meshes"]) - 1

    def node(self, mesh, name, translation=None, parent=None):
        n = {"name": name}
        if mesh is not None:
            n["mesh"] = mesh
        if translation:
            n["translation"] = [t * self.scale for t in translation]
        self.g["nodes"].append(n)
        i = len(self.g["nodes"]) - 1
        if parent is None:
            self.g["scenes"][0]["nodes"].append(i)
        else:
            self.g["nodes"][parent].setdefault("children", []).append(i)
        return i

    def save(self):
        os.makedirs(self.outdir, exist_ok=True)
        for k in ("textures", "images", "samplers"):
            if not self.g[k]:
                del self.g[k]
        binname = self.name + ".bin"
        self.g["buffers"] = [{"uri": binname, "byteLength": len(self.bin)}]
        with open(os.path.join(self.outdir, binname), "wb") as f:
            f.write(self.bin)
        path = os.path.join(self.outdir, self.name + ".gltf")
        with open(path, "w") as f:
            json.dump(self.g, f, indent=1)
        return path


# ------------------------------------------------------------- walkers

def _walker_for(vbuf, seg, dlbuf):
    def resolve(addr):
        if addr >> 24 != seg:
            return None
        return vbuf, addr & 0xFFFFFF
    w = gbi.Walker(dlbuf, resolve)
    return w


def walk_linear(vbuf, seg, dlbuf):
    """all triangles of a display-list area, walked once front to back"""
    w = _walker_for(vbuf, seg, dlbuf)
    stop = len(dlbuf)
    pc = 0
    while pc + 8 <= stop:
        w0 = struct.unpack_from(">I", dlbuf, pc)[0]
        op = w0 >> 24
        if op in (0x06, 0xB8, 0xBE):
            pc += 8
            continue
        # run a single command
        w.max_steps = 1
        w.run(pc)
        pc += 8
    return w


def export_level(rom, lv, outdir, name, objects=True, scale=0.01):
    b = Builder(outdir, name, rom, scale)
    vbuf = b"".join(v.pack() for v in lv.vertices)
    w = walk_linear(vbuf, 8, lv.dl)
    m = b.mesh(w, name + "_static")
    root = b.node(m, name)
    nobj = 0
    if objects and rom is not None:
        cache = {}
        from .model import PackedObject
        for i, pl in enumerate(lv["objects"] or []):
            if pl.type not in cache:
                try:
                    f = rom.object(pl.type)
                    obj = PackedObject.parse(f.data, f.split)
                    cache[pl.type] = object_mesh(b, obj, "obj%03d_%s" % (pl.type, f.name))
                except Exception:
                    cache[pl.type] = None
            if cache[pl.type] is not None:
                b.node(cache[pl.type], "place%02d_obj%03d" % (i, pl.type), (pl.x, pl.y, pl.z), parent=root)
                nobj += 1
    b.stats["objects"] = nobj
    return b.save(), b.stats


def object_mesh(b, obj, name):
    vbuf = b"".join(v.pack() for v in obj.vertices)
    w = walk_linear(vbuf, 9, obj.dl_bytes())
    return b.mesh(w, name)


def export_object(rom, obj, outdir, name, scale=0.01):
    b = Builder(outdir, name, rom, scale)
    b.node(object_mesh(b, obj, name), name)
    return b.save(), b.stats


def export_model(rom, vm, outdir, name, scale=0.01):
    b = Builder(outdir, name, rom, scale)
    vbuf = b"".join(v.pack() for v in vm.vertices())
    w = walk_linear(vbuf, 6, vm.dl_bytes())
    b.node(b.mesh(w, name), name)
    return b.save(), b.stats
