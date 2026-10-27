#!/usr/bin/env python3
"""Reduce access-width traces (port/tools/m64widths.py output) to what the
byte-order layer consumes.

  widths.py image OUT TRACE...    the reads of the hd/front-end data images:
                                  'region offset width' (port/data/image_widths.txt)
  widths.py assets TRACE...       per loaded asset (by ROM start): load sites,
                                  sizes, and a summary of the read/write widths
  widths.py facts OUT TRACE...    per asset, every offset read and its width(s):
                                  the oracle of `make -C port loadcheck` (T9)

The traces hold no ROM bytes, only which offsets the original code reads at
which width (and how often).
"""
import collections
import os
import sys


# copy loops: they move bytes at whatever width suits them, which says
# nothing about the data's own layout (the copy is byte-order neutral)
COPIERS = {"util_copy64",   # 23C20: copy in 8-byte units
           "tex_decode",   # 60F60: packed texture -> scratch copy (u64, u16)
           "tex_decode_type0",   # 60F60: type-0 (stored) texture copy
           "stats_copy_result",   # 409D0: 16-byte record copy
           "obj_queue_effect",   # 77E20: 0x38-byte debris record copy (words)
           "veh_deserialize_state",   # 679E0: vehicle state restore (byte copy)
           "veh_serialize_state",   # 679E0: vehicle state save (byte copy)
           "veh_snapshot",   # vehicle block byte loop
           "memcpy", "bcopy", "bzero", "alCopy"}

# Level-time code: in a level the front end's memory (0x801E7000-0x8021ED00)
# is heap (decoded textures, models, collision records), so what these read
# there says nothing about the front end's data image.  (Before this filter,
# the texture decoder's u16 accesses swapped e.g. the pak file name strings
# gPakGameName and character set gPakCharset as halfwords.)
LEVEL_HEAP_USERS = {"tex_decode_type1", "tex_decode_type2",   # 60F60: texture decode
                    "anim_init_model", "anim_update_channel", "anim_apply_linear_key",   # 56040: models
                    "veh_add_record", "tex_resolve_dl_impl", "lvl_load_model",   # 5CB60: level/object load
                    "mtx_to_n64", "mtx_mul"}  # 679E0: vehicle blocks


# WIDTHS_NO_DECODERS=1 (compare.py's strict facts, port/Makefile strict-data):
# drop the texture decoders' reads.  The tracer charges an access to the last
# load at that address, and the decoders work in heap that earlier assets
# (e.g. the packed-object table) occupied; the strict comparison keeps
# textures lenient anyway.
SKIP = {"tex_decode_type1", "tex_decode_type2", "tex_decode"} if os.environ.get("WIDTHS_NO_DECODERS") else set()


def parse(paths, keep_copiers=False):
    acc = collections.defaultdict(collections.Counter)    # (region, off) -> Counter((width, kind))
    loads = []
    for p in paths:
        for line in open(p):
            f = line.split()
            if not f:
                continue
            if f[0] == "A":
                func = f[6] if len(f) > 6 else "?"
                if (func in COPIERS or func.startswith("~")) and not keep_copiers:
                    continue
                if f[1] == "fe" and func in LEVEL_HEAP_USERS:
                    continue
                if func in SKIP:
                    continue
                off = int(f[2], 16)
                if f[3] == "8" and off % 8 == 4:
                    continue      # the debugger reports a doubleword access twice (each half)
                acc[(f[1], off)][(f[3], f[4])] += int(f[5])
            elif f[0] == "L":
                loads.append((int(f[1]), int(f[2], 16), int(f[3], 16), int(f[4], 16), f[5], int(f[6], 16)))
    return acc, loads


def cmd_image(out, paths):
    acc, _ = parse(paths)
    rows = []
    for (region, off), c in acc.items():
        if region not in ("hd", "fe"):
            continue
        for (w, kind), n in c.items():
            if kind == "R":
                rows.append((region, off, w))
    rows.sort(key=lambda r: (r[0], r[1], r[2]))
    with open(out, "w") as f:
        f.write("# region offset width: reads of the hd_code / front-end data images by the original code\n")
        f.write("# (port/tools/m64widths.py over boot, front end and the attract demos; reduced by widths.py)\n")
        for r in rows:
            f.write("%s %X %s\n" % r)
    print("%d image read facts" % len(rows))


def cmd_facts(out, paths):
    """The oracle for loadcheck's T9: per traced asset (ROM start, decompressed
    size, how it arrived), every offset the code read and at which width
    (one width per offset; offsets read at several widths are listed as such)."""
    acc, loads = parse(paths)
    size = {}
    how = {}
    for vi, rom, dest, sz, kind, ra in loads:
        if kind in ("gz", "pk"):
            size[rom] = max(size.get(rom, 0), sz)
            how[rom] = kind
        elif kind == "dma" and rom not in how:
            size[rom] = max(size.get(rom, 0), sz)
            how[rom] = "dma"
    rows = collections.defaultdict(lambda: collections.defaultdict(set))
    for (region, off), c in acc.items():
        if region[0] != "r":
            continue
        rom = int(region[1:], 16)
        for (w, kind), n in c.items():
            if kind == "R" and w in ("1", "2", "4", "8"):
                rows[rom][off].add(int(w))
    with open(out, "w") as f:
        f.write("# ROM SIZE HOW then OFF W[,W...] lines (widths.py facts; local, not committed)\n")
        for rom in sorted(rows):
            if rom not in size:
                continue
            f.write("@ %06X %X %s\n" % (rom, size[rom], how[rom]))
            for off in sorted(rows[rom]):
                if off < size[rom]:
                    f.write("%X %s\n" % (off, ",".join(str(w) for w in sorted(rows[rom][off]))))
    print("%d assets" % len(rows))


def cmd_assets(paths):
    acc, loads = parse(paths)
    by = collections.defaultdict(list)
    for vi, rom, dest, size, kind, ra in loads:
        by[rom].append((vi, dest, size, kind, ra))
    per = collections.defaultdict(lambda: collections.Counter())
    for (region, off), c in acc.items():
        if region[0] != "r":
            continue
        for (w, kind), n in c.items():
            per[int(region[1:], 16)][(w, kind)] += 1
    for rom in sorted(by):
        ls = by[rom]
        sites = collections.Counter("%08X/%s" % (l[4], l[3]) for l in ls)
        sizes = sorted(set(l[2] for l in ls))
        wc = per.get(rom, {})
        print("%06X loads=%d sizes=%s sites=%s widths=%s" % (
            rom, len(ls), ",".join("%X" % s for s in sizes[:4]), ",".join(sites),
            " ".join("%s%s:%d" % (w, k, n) for (w, k), n in sorted(wc.items()))))


if __name__ == "__main__":
    if sys.argv[1] == "image":
        cmd_image(sys.argv[2], sys.argv[3:])
    elif sys.argv[1] == "assets":
        cmd_assets(sys.argv[2:])
    elif sys.argv[1] == "facts":
        cmd_facts(sys.argv[2], sys.argv[3:])
    else:
        sys.exit(__doc__)
