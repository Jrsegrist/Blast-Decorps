"""bcfiles command line (python3 -m bcfiles, with tools_port on PYTHONPATH, or
python3 tools_port/bcfiles).

  list ROM                      the ROM's files and their kinds
  dump ROM FILE [--json]        a level, model, scene or building as text (or JSON)
  extract ROM OUTDIR            every file decompressed, levels/models/buildings also as JSON
  build ROM JSONDIR OUT.z64     a ROM with the edited JSON files of an extract put back
  gltf ROM FILE OUTDIR          glTF 2.0 (+ PNG textures) of a model, building or level
  roundtrip ROM                 parse and write every level/model/scene/building, compare
  mod-check ROM MOD.bcmod       a level mod (bc.exe's editor): its hashes and changes against the ROM
  mod-apply ROM MOD.bcmod OUTDIR   the edited level files (level_NN.bin, inflated)
  mod-export ROM MOD.bcmod OUT.z64 [--shift-dir D]   a ROM with the mod's levels in it (for emulators)
  geo-rebuild ROM OUTDIR [--levels a,b-c] [--keep-rare]   levels with their derived sections regenerated
  geo-gltf ROM level:N OUTDIR [--scale S]        a level's triangles as glTF for editing (Blender)
  geo-import ROM level:N EDITED.gltf OUT.bcmod   the edited glTF back as a geometry mod (bcmod/2)
  tex-import ROM ID IMAGE.png OUT.bcmod [--into MOD]   an image into texture slot ID (bcmod/2)

FILE: level:N | level:NAME | model:NAME | scene:NAME | object:N (object-table index)
| object:NAME | a hex ROM offset (0x4A5660).
"""
import json
import os
import struct
import sys

from . import gz, lzss, rombuild
from .level import Level
from .model import PackedObject, VehicleModel
from .romfs import Rom

PARSERS = {"level": Level, "model": VehicleModel, "scene": VehicleModel, "object": PackedObject}


def usage():
    sys.stderr.write(__doc__)
    sys.exit(1)


def find(rom, spec):
    kind, _, key = spec.partition(":")
    files = rom.files()
    if not key:
        try:
            off = int(spec, 16)
        except ValueError:
            raise SystemExit("bad FILE %r" % spec)
        for f in files:
            if f.start == off:
                return f
        raise SystemExit("no file starts at %06X" % off)
    if kind == "level" and key.isdigit():
        return rom.level(int(key))
    if kind == "object" and key.isdigit():
        return rom.object(int(key))
    for f in files:
        if f.kind == kind and f.name == key:
            if kind == "level":
                return rom.level(f.info["level"])
            return f
    raise SystemExit("no %s named %r" % (kind, key))


def parse_file(f):
    cls = PARSERS[f.kind]
    return cls.parse(f.data, f.split)


# ------------------------------------------------------------------- list

def cmd_list(args):
    rom = Rom.load(args[0])
    print("%s (%s), compressor for edits: %s" % (args[0], rom.version, gz.which_compressor()))
    print("%-7s %-8s %-8s %-28s %s" % ("kind", "start", "end", "name", "info"))
    for f in rom.files():
        info = []
        if f.kind == "level":
            info.append("level %d" % f.info["level"])
        if f.kind == "model":
            info.append("types " + ",".join("0x%X" % t for t in f.info["types"]))
        if f.kind == "object":
            idx = f.info.get("indexes", [])
            info.append("table %s" % (",".join(map(str, idx[:6])) + ("..." if len(idx) > 6 else "")))
        if f.members:
            info.append("gzip x%d -> %d bytes" % (len(f.members), len(f.data)))
        if f.kind == "lzss":
            info.append("lzss %d-bit" % f.info["bits"])
        print("%-7s %06X   %06X   %-28s %s" % (f.kind, f.start, f.end, f.name[:28], "; ".join(info)))


# ------------------------------------------------------------------- dump

def _fmt_rec(r):
    if hasattr(r, "to_json"):
        return " ".join("%s=%s" % kv for kv in r.to_json().items())
    return str(r)


def cmd_dump(args):
    as_json = "--json" in args
    full = "--all" in args
    args = [a for a in args if not a.startswith("--")]
    rom = Rom.load(args[0])
    f = find(rom, args[1])
    p = parse_file(f)
    if as_json:
        json.dump(p.to_json(), sys.stdout, indent=1)
        print()
        return
    print("%s %s: ROM %06X-%06X, %d bytes inflated (%s)" % (f.kind, f.name, f.start, f.end, len(f.data),
                                                         " + ".join(str(len(m.data)) for m in f.members)))
    for m in f.members:
        print("  gzip member %s mtime %d at %06X" % (m.name, m.mtime, m.start))
    for n in p.notes:
        print("  note:", n)
    if getattr(p, "raw", None) is not None:
        print("  (kept as raw bytes)")
        return
    if isinstance(p, Level):
        print("header:", " ".join("%s=%s" % kv for kv in p.header.items()))
        print("vertices: %d" % len(p.vertices))
        bulk = {"guard_tris", "ground_grid", "coll_grid_a", "coll_grid_b", "vis_grid", "vis_records_0",
                "vis_records_1", "vis_records_2", "vis_records_3", "vis_groups_0", "vis_groups_1", "vis_groups_2",
                "vis_groups_3", "part_groups", "switch_groups", "gate_groups"}
        summ = p.summary()
        for s, sec in p.sections.items():
            v = sec.value
            print("[0x%02X %s] %s" % (s, sec.name, summ.get(sec.name)))
            if sec.name in bulk and not full:
                continue
            if isinstance(v, list):
                for i, r in enumerate(v):
                    print("   %3d %s" % (i, _fmt_rec(r) if not isinstance(r, dict) else
                                         json.dumps({k: (x.to_json() if hasattr(x, "to_json") else x) for k, x in r.items()})))
            elif v is not None and hasattr(v, "to_json"):
                print("   ", _fmt_rec(v))
            elif isinstance(v, dict):
                print("   ", json.dumps(json.loads(json.dumps(v, default=lambda o: o.to_json())))[:2000])
        print("display lists: %d bytes; entry points %s" % (len(p.dl), " ".join("0x%02X=+%X" % kv for kv in p.dl_ptrs.items())))
    else:
        if isinstance(p, PackedObject):
            print("header:", " ".join("%s=%s" % kv for kv in p.fields.items()))
            print("vertices: %d" % len(p.vertices))
        for k, v in p.summary().items():
            print("  %-14s %s" % (k, v))
        if isinstance(p, VehicleModel):
            print("  dl entries:", " ".join("%s=+%X" % kv for kv in p.dl_entries().items()))
        tex = set()
        dl = p.dl_bytes() or b""
        for q in range(0, len(dl) - 7, 8):
            if dl[q] == 0xFD:
                tex.add(int.from_bytes(dl[q + 4:q + 8], "big"))
        print("  textures (table ids): %s" % " ".join(str(t) for t in sorted(tex)))
        if full:
            from . import gbi
            for q in range(0, len(dl) - 7, 8):
                w0, w1 = int.from_bytes(dl[q:q + 4], "big"), int.from_bytes(dl[q + 4:q + 8], "big")
                print("    +%05X %s" % (q, gbi.describe(w0, w1)))


# ---------------------------------------------------------------- extract

def file_dir(f):
    if f.kind == "level":
        return os.path.join("levels", "%02d_%s" % (f.info["level"], f.name))
    if f.kind == "object":
        return os.path.join("objects", "%03d_%s" % (f.info["indexes"][0], f.name))
    return os.path.join(f.kind + "s", f.name)


def cmd_extract(args):
    rom = Rom.load(args[0])
    out = args[1]
    os.makedirs(out, exist_ok=True)
    index = {"format": "bcfiles.extract/1", "version": rom.version, "files": []}
    n = 0
    for f in rom.files():
        ent = {"kind": f.kind, "name": f.name, "start": "0x%06X" % f.start, "end": "0x%06X" % f.end}
        if f.kind in PARSERS:
            d = file_dir(f)
            if f.kind == "level":
                f = rom.level(f.info["level"])
            os.makedirs(os.path.join(out, d), exist_ok=True)
            with open(os.path.join(out, d, "data.bin"), "wb") as fh:
                fh.write(f.data)
            p = parse_file(f)
            j = p.to_json()
            j["rom"] = {"start": "0x%06X" % f.start, "kind": f.kind, "name": f.name,
                        "members": [m.meta_json() for m in f.members]}
            with open(os.path.join(out, d, f.kind + ".json"), "w") as fh:
                json.dump(j, fh, indent=0)
            ent["dir"] = d
        elif f.members:
            d = {"image": "images", "demos": "demos", "code": "code"}.get(f.kind, f.kind + "s")
            os.makedirs(os.path.join(out, d), exist_ok=True)
            names = []
            for i, m in enumerate(f.members):
                nm = "%06X_%s%s.bin" % (f.start, f.name, "" if len(f.members) == 1 else "_%d" % i)
                with open(os.path.join(out, d, nm), "wb") as fh:
                    fh.write(m.data)
                names.append(os.path.join(d, nm))
            ent["files"] = names
        elif f.kind == "lzss":
            data, end = lzss.decompress(rom.data, f.info["bits"], f.start)
            nm = os.path.join("lzss", "%06X.bin" % f.start)
            os.makedirs(os.path.join(out, "lzss"), exist_ok=True)
            with open(os.path.join(out, nm), "wb") as fh:
                fh.write(data)
            ent["files"] = [nm]
            ent["lzss_end"] = "0x%06X" % end
        index["files"].append(ent)
        n += 1
    with open(os.path.join(out, "index.json"), "w") as fh:
        json.dump(index, fh, indent=1)
    print("extract: %d files into %s (index.json)" % (n, out))


# ------------------------------------------------------------------ build

def cmd_build(args):
    shift_dir = None
    if "--shift-dir" in args:
        i = args.index("--shift-dir")
        shift_dir = args[i + 1]
        del args[i:i + 2]
    rom = Rom.load(args[0])
    src, out = args[1], args[2]
    patches = []
    for f in rom.files():
        if f.kind not in PARSERS:
            continue
        if f.kind == "level":
            f = rom.level(f.info["level"])
        path = os.path.join(src, file_dir(f), f.kind + ".json")
        if not os.path.exists(path):
            continue
        with open(path) as fh:
            j = json.load(fh)
        p = PARSERS[f.kind].from_json(j)
        data, split = p.write()
        changed = data != f.data or split != f.split
        if not changed:
            continue
        blob, how = rombuild.pack_to_fit(f, data, split)
        pt = rombuild.Patch(f, blob, True, how)
        print("build: %s %s changed: %d -> %d bytes inflated, %d -> %d compressed (%s; slot %d): %s"
              % (f.kind, f.name, len(f.data), len(data), f.end - f.start, len(blob), how, pt.slot,
                 "in place" if pt.fits else "does NOT fit"))
        patches.append(pt)
    image, rest = rombuild.apply_in_place(rom.data, patches)
    if rest:
        d = shift_dir or (out + ".shift")
        fit = [p for p in patches if p.fits]
        cfg = rombuild.shift_config(rest, d, in_place=fit)
        if fit:
            print("build: in the config too, written in place by the shiftable build: %s"
                  % ", ".join("%s %s" % (p.file.kind, p.file.name) for p in fit))
        print("build: %d file(s) grew past their slot; for the shiftable build (docs/shiftable.md):" % len(rest))
        print("  make VERSION=us.v11 SHIFT=1 BASEROM=<this ROM> SHIFT_PADS=%s" % os.path.abspath(cfg))
        print("  (%s not written)" % out)
        sys.exit(3)
    with open(out, "wb") as fh:
        fh.write(image)
    print("build: %s written (%d file(s) replaced in place, compressor: %s)" % (out, len(patches), gz.which_compressor()))


# ------------------------------------------------------------------- gltf

def cmd_gltf(args):
    from . import gltf
    scale = 0.01
    if "--scale" in args:
        i = args.index("--scale")
        scale = float(args[i + 1])
        del args[i:i + 2]
    no_obj = "--no-objects" in args
    args = [a for a in args if a != "--no-objects"]
    rom = Rom.load(args[0])
    f = find(rom, args[1])
    p = parse_file(f)
    name = f.name
    if f.kind == "level":
        path, st = gltf.export_level(rom, p, args[2], "level%02d_%s" % (f.info["level"], name), not no_obj, scale)
    elif f.kind == "object":
        path, st = gltf.export_object(rom, p, args[2], name, scale)
    else:
        path, st = gltf.export_model(rom, p, args[2], name, scale)
    print("gltf: %s (%s)" % (path, ", ".join("%s %s" % kv for kv in st.items())))


# -------------------------------------------------------------- roundtrip

def roundtrip(rom, verbose=False):
    """(results per kind, [failures]) for every level, model, scene and building"""
    res = {}
    bad = []
    files = [rom.level(n) for n in range(60)] + [f for f in rom.files() if f.kind in ("model", "scene", "object")]
    for f in files:
        cls = PARSERS[f.kind]
        p = cls.parse(f.data, f.split)
        d, s = p.write()
        ok_bytes = d == f.data and s == f.split
        j = json.loads(json.dumps(p.to_json()))
        p2 = cls.from_json(j)
        d2, s2 = p2.write()
        ok_json = (d2, s2) == (d, s) and json.loads(json.dumps(p2.to_json())) == j
        # recompression: the members rebuilt from the inflated data
        blob = b"".join(m.header() + gz.compress(m.data) + m.trailer() for m in f.members)
        orig = b"".join(m.build() for m in f.members)
        r = res.setdefault(f.kind, {"files": 0, "bytes_equal": 0, "json_equal": 0, "typed": 0, "fully_typed": 0,
                                    "raw_bytes": 0, "recompress_equal": 0})
        r["files"] += 1
        r["bytes_equal"] += ok_bytes
        r["json_equal"] += ok_json
        r["typed"] += getattr(p, "raw", None) is None
        r["fully_typed"] += p.unexplained() == 0
        r["raw_bytes"] += p.unexplained()
        r["recompress_equal"] += blob == orig
        if not (ok_bytes and ok_json):
            bad.append("%s %s: bytes %s json %s" % (f.kind, f.name, ok_bytes, ok_json))
        if verbose and p.notes:
            print("  %s %s: %s" % (f.kind, f.name, "; ".join(p.notes)))
    return res, bad


def cmd_roundtrip(args):
    rom = Rom.load(args[0])
    res, bad = roundtrip(rom, verbose=True)
    print("roundtrip %s (%s; compressor %s):" % (args[0], rom.version, gz.which_compressor()))
    for k, r in res.items():
        print("  %-7s %s" % (k, " ".join("%s=%d" % kv for kv in r.items())))
    for b in bad:
        print("  FAIL", b)
    print("roundtrip: %s" % ("OK" if not bad else "FAILED"))
    sys.exit(1 if bad else 0)


# -------------------------------------------------------------------- mods

def _load_mod(rom, path):
    from . import mod
    try:
        m = mod.load(path)
        return m, mod.apply_mod(rom, m)
    except (mod.ModError, ValueError) as e:
        raise SystemExit("%s: %s" % (path, e))


def cmd_mod_check(args):
    from . import mod
    rom = Rom.load(args[0])
    m, out = _load_mod(rom, args[1])
    print("%s: \"%s\" (%s)" % (args[1], m.get("name", ""), m.get("description", "")))
    for ml in m["levels"]:
        data, split = out[ml["level"]]
        f = rom.level(ml["level"])
        print("  level %d (%s): %d change%s, %d -> %d bytes inflated, edited SHA-1 %s%s" % (
            ml["level"], f.name, len(ml["changes"]), "" if len(ml["changes"]) == 1 else "s", len(f.data), len(data),
            mod.sha1(data), " (as the mod says)" if ml.get("edited_sha1") else ""))
        for c in ml["changes"]:
            print("    " + mod.describe(c))
        g = ml.get("geometry")
        if g:
            print("    geometry: %d moved corner(s), removed %s, added %s, %d G_SETTIMG change(s)" % (
                len(g.get("move", [])), {k: len(v) for k, v in g.get("remove", {}).items()},
                {k: len(v) for k, v in g.get("add", {}).items()}, len(g.get("settimg", []))))
    try:
        for p in mod.texture_plans(rom, m):
            if p.kind == "fit":
                print("  texture %d: %d packed bytes at ROM 0x%06X (fits in place)" % (p.index, p.size, p.rom))
            elif p.kind == "grow":
                print("  texture %d: %d packed bytes at ROM 0x%06X, more than its room %d: it grows (a ROM export "
                      "takes the shift path)" % (p.index, p.size, p.rom, p.room))
            else:
                print("  texture %d: a new slot, %d bytes stored (a ROM export puts it in the ROM's free space)"
                      % (p.index, p.size))
    except mod.ModError as e:
        raise SystemExit("%s: %s" % (args[1], e))
    print("mod-check: OK")


def cmd_mod_apply(args):
    rom = Rom.load(args[0])
    _, out = _load_mod(rom, args[1])
    os.makedirs(args[2], exist_ok=True)
    for n, (data, split) in sorted(out.items()):
        path = os.path.join(args[2], "level_%02d.bin" % n)
        with open(path, "wb") as fh:
            fh.write(data)
        print("mod-apply: %s (%d bytes, split 0x%X)" % (path, len(data), split))


def cmd_mod_export(args):
    shift_dir = None
    if "--shift-dir" in args:
        i = args.index("--shift-dir")
        shift_dir = args[i + 1]
        del args[i:i + 2]
    rom = Rom.load(args[0])
    m, out = _load_mod(rom, args[1])
    from . import mod as modm
    try:
        plans = modm.texture_plans(rom, m)
    except modm.ModError as e:
        raise SystemExit("%s: %s" % (args[1], e))
    tex = [rombuild.TexPatch(p.index, p.rom, p.room, p.packed) for p in plans if p.kind == "fit"]
    grown = [p for p in plans if p.kind == "grow"]
    news = [p for p in plans if p.kind == "new"]
    orig_data = rom.data
    if tex:
        # texture slots in place (their table entries' packed size too): the code and the tables keep every offset
        data = bytearray(rom.data)
        rombuild.apply_textures(data, tex)
        for t in tex:
            print("mod-export: texture %d: %d -> %d packed bytes (room %d), in place"
                  % (t.index, rom.tex_table[t.index].size, t.size, t.room))
        rom.data = bytes(data)
    for g in grown:
        print("mod-export: texture %d: %d -> %d packed bytes, more than its room %d: it grows (the shiftable build "
              "moves the textures after it)" % (g.index, rom.tex_table[g.index].size, g.size, g.room))
    patches = []
    for n, (data, split) in sorted(out.items()):
        f = rom.level(n)
        if data == f.data:
            continue
        blob, how = rombuild.pack_to_fit(f, data, split)
        pt = rombuild.Patch(f, blob, True, how)
        print("mod-export: level %d %s: %d -> %d bytes inflated, %d -> %d compressed (%s; slot %d): %s"
              % (n, f.name, len(f.data), len(data), f.end - f.start, len(blob), how, pt.slot,
                 "in place" if pt.fits else "does NOT fit"))
        patches.append(pt)
    # new slots: in the ROM's free space at its end when nothing needs the shift path (the shiftable build starts
    # from BASEROM and lays the end out again: there they are an added file)
    news_in_place = None
    if news and not grown and all(p.fits for p in patches):
        data = bytearray(rom.data)
        news_in_place = rombuild.apply_new_slots(data, news)
        if news_in_place is not None:
            rom.data = bytes(data)
            for p in news:
                print("mod-export: new texture slot %d: %d bytes (stored), in the ROM's free space from 0x%06X"
                      % (p.index, p.size, news_in_place))
        else:
            print("mod-export: the new texture slots (%d bytes) don't fit the ROM's free space (0x%X bytes): on the "
                  "shift path" % (sum(p.size for p in news), rombuild.free_tail(orig_data)[1] -
                                  rombuild.free_tail(orig_data)[0]))
    image, rest = rombuild.apply_in_place(rom.data, patches)
    shift_news = news if news and news_in_place is None else []
    if rest or grown or shift_news:
        d = shift_dir or (args[2] + ".shift")
        # the shiftable build starts from the ROM given (BASEROM): everything the in-place write would have put in
        # it goes into the config too - the levels that fit and the texture slots, as "patch" entries; grown slots
        # replace their textures' files, new slots are an added file the table points into
        fit = [p for p in patches if p.fits]
        cfg = rombuild.shift_config(rest, d, in_place=fit, textures=tex, grown=grown, new=shift_news)
        if fit or tex:
            print("mod-export: in the config too, written in place by the shiftable build: %s" % ", ".join(
                ["level %s" % p.file.name for p in fit] + ["texture %d" % t.index for t in tex]))
        if grown or shift_news:
            print("mod-export: in the config: %s" % ", ".join(
                ["texture %d grown to %d bytes" % (g.index, g.grown_room()) for g in grown] +
                ["new texture slot %d" % p.index for p in shift_news]))
        print("mod-export: %d level(s) grew past their slot, %d texture slot(s) grew, %d new slot(s) on the shift "
              "path; for the shiftable build (docs/shiftable.md):" % (len(rest), len(grown), len(shift_news)))
        print("  make VERSION=us.v11 SHIFT=1 BASEROM=<this ROM> SHIFT_PADS=%s" % os.path.abspath(cfg))
        print("  (%s not written; the shiftable build's ROM runs in an emulator and in bc.exe / bc_headless, or use "
              "the mod)" % args[2])
        sys.exit(3)
    with open(args[2], "wb") as fh:
        fh.write(image)
    print("mod-export: %s written (%d level(s) in place%s; bc.exe needs --no-rom-check for it, or use the mod)"
          % (args[2], len(patches), ", %d new texture slot(s)" % len(news) if news else ""))


# --------------------------------------------------------------- geometry

def cmd_geo_rebuild(args):
    """every level (or --levels a,b-c) with its derived sections regenerated
    from its own triangles (geometry.py): level_NN.bin (inflated, the ROM's byte
    order) and a summary of how close the result is to Rare's"""
    from . import geometry
    levels = list(range(60))
    if "--levels" in args:
        i = args.index("--levels")
        levels = _level_list(args[i + 1])
        del args[i:i + 2]
    keep = "--keep-rare" in args
    args = [a for a in args if a != "--keep-rare"]
    rom = Rom.load(args[0])
    os.makedirs(args[1], exist_ok=True)
    for n in levels:
        f = rom.level(n)
        lv = Level.parse(f.data, f.split)
        g = geometry.Geometry.from_level(lv)
        out = Level.parse(f.data, f.split)
        g.write_into(out, base=lv if keep else None)
        data, split = out.write()
        with open(os.path.join(args[1], "level_%02d.bin" % n), "wb") as fh:
            fh.write(data)
        print("geo-rebuild: level %02d %-10s %6d -> %6d bytes%s" % (n, f.name, len(f.data), len(data),
                                                                   " (the ROM's bytes)" if data == f.data else ""))


def cmd_geo_gltf(args):
    """a level's triangles as glTF for editing (geogltf.py: ids and original
    positions ride along in TEXCOORD_1/2)"""
    from . import geogltf
    scale = 0.01
    if "--scale" in args:
        i = args.index("--scale")
        scale = float(args[i + 1])
        del args[i:i + 2]
    rom = Rom.load(args[0])
    f = find(rom, args[1])
    if f.kind != "level":
        raise SystemExit("geo-gltf: a level, please (level:N)")
    lv = Level.parse(f.data, f.split)
    path, st = geogltf.export(rom, lv, args[2], "level%02d_%s_geo" % (f.info["level"], f.name), scale)
    print("geo-gltf: %s (%s)" % (path, ", ".join("%s %s" % kv for kv in st.items())))


def cmd_geo_import(args):
    """an edited geo-gltf export -> a bcmod/2 with the geometry edit (checked by applying it)"""
    from . import geogltf, mod
    name = None
    if "--name" in args:
        i = args.index("--name")
        name = args[i + 1]
        del args[i:i + 2]
    rom = Rom.load(args[0])
    f = find(rom, args[1])
    lv = Level.parse(f.data, f.split)
    try:
        edit = geogltf.import_edit(args[2], lv)
    except geogltf.ImportError_ as e:
        raise SystemExit("geo-import: %s" % e)
    n = f.info["level"]
    m = mod.make(name or os.path.basename(args[2]), "geometry from %s" % os.path.basename(args[2]),
                 [(n, f.data, [], None, edit)])
    m["format"] = "bcmod/2"
    try:
        data, split = mod.apply_level(mod.parse(json.loads(mod.dumps(m)))["levels"][0], f.data, f.split)
    except mod.ModError as e:
        raise SystemExit("geo-import: the edit doesn't apply: %s" % e)
    m["levels"][0]["edited_sha1"] = mod.sha1(data)
    with open(args[3], "w") as fh:
        fh.write(mod.dumps(m))
    print("geo-import: %s: %d moved corner(s), removed %s, added %s; level %d %d -> %d bytes" % (
        args[3], len(edit.get("move", [])), {k: len(v) for k, v in edit.get("remove", {}).items()},
        {k: len(v) for k, v in edit.get("add", {}).items()}, n, len(f.data), len(data)))


def texture_usage(rom, tid, levels=False):
    """{(fmt, siz, width, height)} the levels' display lists use texture tid with
    (levels: (fmt, siz, width, height, levels of detail))"""
    from . import gltf
    use = set()
    for n in range(60):
        f = rom.level(n)
        lv = Level.parse(f.data, f.split)
        vbuf = b"".join(v.pack() for v in lv.vertices)
        w = gltf.walk_linear(vbuf, 8, lv.dl)
        for t in w.textures.values():
            if t["id"] == tid:
                u = (t["fmt"], {0: 4, 1: 8, 2: 16, 3: 32}[t["siz"]], t["width"], t["height"])
                use.add(u + (t.get("mips", 1),) if levels else u)
    return use


def palette_usage(rom, tid):
    """[(what, palette entry, width, height)] the palette packings' (4, 5) users of texture tid: the
    billboard effect definitions (hd_code gFxDefsByKind: EffectDef {s16 palette, u8 cols, rows, ..., u16 w, h,
    frames} and its texture ids) and the medal sprites (front end gMedalPalettes, gMedalSprites: 2 ids drawn with
    4 palettes, 32 x 32 tiles).  US 1.1 (the tables' addresses); [] for another ROM"""
    return palette_usage_cached(rom, tid)


def cmd_tex_import(args):
    """an image into a texture-table slot: a bcmod/2 with the slot (a new mod, or
    added to an existing one with --into MOD); the image is packed the slot's way"""
    import base64
    from . import mod, png, texture
    into = None
    if "--into" in args:
        i = args.index("--into")
        into = args[i + 1]
        del args[i:i + 2]
    palette = None
    if "--palette" in args:
        i = args.index("--palette")
        palette = int(args[i + 1], 0)
        del args[i:i + 2]
    rom = Rom.load(args[0])
    tid = int(args[1], 0)
    data = open(args[2], "rb").read()
    w, h, rgba = png.decode_rgba(data)
    e = rom.tex_table[tid]
    if e.type in (4, 5):
        # a palette packing (effect frames, medal sprites): the picture takes the colours of the slot's palette,
        # which stays as it is - every one of the game's palettes is shared by many textures
        users = palette_usage(rom, tid)
        pals = sorted(set(u[1] for u in users))
        if palette is None:
            if not pals:
                raise SystemExit("tex-import: texture %d (packing %d) is drawn with a palette this tool doesn't know: "
                                 "give it with --palette ID" % (tid, e.type))
            palette = pals[0]
        if len(pals) > 1:
            print("tex-import: warning: texture %d is drawn with %d palettes (%s: %s); the picture is matched to "
                  "palette %d, the others show its indexes in their own colours"
                  % (tid, len(pals), ", ".join(sorted(set(u[0] for u in users))), pals, palette))
        sharing = sum(1 for p in range(texture.TEX_ENTRIES) if p != tid and any(
            u[1] == palette for u in palette_usage_cached(rom, p))) if users else 0
        pe = rom.tex_table[palette]
        pal = rom.data[pe.rom:pe.rom + min(pe.dma_len, 0x100)]
        fmt, siz = (0, 16) if e.type == 4 else (0, 32)
        orig = rom.texture(tid, pal + bytes(0x100 - len(pal)))
        try:
            packed = texture.encode_slot(rgba, w, h, fmt, siz, e.type, orig, palette=pal)
        except texture.TextureError as ex:
            raise SystemExit("tex-import: %s" % ex)
        print("tex-import: texture %d (packing %d, %s%d %dx%d, palette %d of %d colours, kept: %s): %d -> %d packed "
              "bytes, room %d: %s" % (tid, e.type, "RGBA", siz, w, h, palette, len(pal) // 2,
                                      "shared by %d other textures" % sharing if sharing else "its users unknown",
                                      e.size, len(packed), e.dma_len,
                                      "fits in place" if len(packed) <= e.dma_len else "does NOT fit"))
        if len(packed) > e.dma_len:
            sys.exit(3)
        ent = {"id": tid, "fmt": fmt, "siz": siz, "palette": palette, "png": base64.b64encode(data).decode("ascii"),
               "slot_sha1": mod.sha1(rom.texture_packed(tid))}
        _tex_import_write(args, into, tid, ent)
        return
    use = texture_usage(rom, tid, levels=True)
    if not use:
        raise SystemExit("tex-import: no level list uses texture %d (its texel format is unknown)" % tid)
    fits = sorted(u for u in use if u[2] == w and u[3] == h)
    if not fits:
        raise SystemExit("tex-import: texture %d is used as %s; the image is %dx%d"
                         % (tid, sorted(set(u[:4] for u in use)), w, h))
    others = sorted(set(u[:4] for u in use if u[2:4] != (w, h)))
    if others:
        print("tex-import: warning: texture %d is also drawn as %s: those lists will show the picture's texels "
              "at their size" % (tid, others))
    fmt, siz = fits[-1][:2]
    mips = max(u[4] for u in fits if u[:2] == (fmt, siz))
    if siz == 32:
        mips = 1
    try:
        packed = texture.encode_slot(rgba, w, h, fmt, siz, e.type, rom.texture(tid), mips)
    except texture.TextureError as ex:
        raise SystemExit("tex-import: %s" % ex)
    print("tex-import: texture %d (%s %d-bit %dx%d, packing %d%s): %d -> %d packed bytes, room %d: %s" % (
        tid, texture.G_IM_FMT.get(fmt), siz, w, h, e.type,
        ", %d levels of detail made from the image" % mips if mips > 1 else "", e.size, len(packed), e.dma_len,
        "fits in place" if len(packed) <= e.dma_len else "does NOT fit"))
    if len(packed) > e.dma_len:
        sys.exit(3)
    ent = {"id": tid, "fmt": fmt, "siz": siz, "png": base64.b64encode(data).decode("ascii"),
           "slot_sha1": mod.sha1(rom.texture_packed(tid))}
    if mips > 1:
        ent["mips"] = mips
    _tex_import_write(args, into, tid, ent)


_PAL_USERS = {}


def palette_usage_cached(rom, tid):
    """palette_usage for every texture of the effect / medal tables, computed once"""
    if id(rom) not in _PAL_USERS:
        by = {}
        if rom.version == "us.v11":
            code, fe = rom.code_members()
            hd, fd = code.data, fe.data
            hd0, fe0 = 0x802447C0, 0x801E7000
            for k in range(28):
                d = struct.unpack_from(">I", hd, 0x802C3FFC + 4 * k - hd0)[0] - hd0
                pid, cols, rows = struct.unpack_from(">hBB", hd, d)
                w, h, frames = struct.unpack_from(">HHH", hd, d + 0xA)
                for t in set(struct.unpack_from(">%dH" % (cols * rows * frames), hd, d + 0x10)):
                    if pid >= 0:
                        by.setdefault(t, []).append(("effect %d" % k, pid, w, h))
            pals = struct.unpack_from(">4H", fd, 0x802082D8 - fe0)
            for t in struct.unpack_from(">2H", fd, 0x802082E4 - fe0):
                by.setdefault(t, []).extend(("medal sprite", p, 32, 32) for p in pals)
        _PAL_USERS[id(rom)] = by
    return _PAL_USERS[id(rom)].get(tid, [])


def _tex_import_write(args, into, tid, ent):
    from . import mod
    import os as _os
    if into and _os.path.exists(into):
        m = mod.load(into)
        m["format"] = "bcmod/2"
        m["textures"] = [t for t in m.get("textures", []) if t["id"] != tid] + [ent]
    else:
        m = mod.make(os.path.basename(args[2]), "texture %d from %s" % (tid, os.path.basename(args[2])), [], [ent])
    out = into or args[3]
    with open(out, "w") as fh:
        fh.write(mod.dumps(m))
    print("tex-import: %s written" % out)


def _level_list(s):
    out = []
    for part in s.split(","):
        a, _, b = part.partition("-")
        out += list(range(int(a), int(b or a) + 1))
    return out


def main(argv=None):
    argv = list(sys.argv[1:] if argv is None else argv)
    if not argv:
        usage()
    cmd, args = argv[0], argv[1:]
    table = {"list": (cmd_list, 1), "dump": (cmd_dump, 2), "extract": (cmd_extract, 2), "build": (cmd_build, 3),
             "gltf": (cmd_gltf, 3), "roundtrip": (cmd_roundtrip, 1), "mod-check": (cmd_mod_check, 2),
             "mod-apply": (cmd_mod_apply, 3), "mod-export": (cmd_mod_export, 3), "geo-rebuild": (cmd_geo_rebuild, 2),
             "geo-gltf": (cmd_geo_gltf, 3), "geo-import": (cmd_geo_import, 4), "tex-import": (cmd_tex_import, 3)}
    if cmd not in table or len([a for a in args if not a.startswith("--")]) < table[cmd][1]:
        usage()
    table[cmd][0](args)
