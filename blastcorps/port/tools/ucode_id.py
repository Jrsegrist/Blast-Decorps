#!/usr/bin/env python3
"""Identify the RSP microcodes the game ships, by hash (stage-0 spike).

Reads the microcode bytes from the matching build's ELFs (build/, i.e. the
ROM's own bytes; nothing is written to the repo) and prints, per microcode:
size, CRC32/MD5/SHA-1 of text and data, the ID strings in the data, and the
keys public HLE implementations use to recognise a microcode:
  * glide64 key: 32-bit sum of the first 3072 bytes of the ucode text,
    taken as big-endian words (Glide64's uc_crc)
  * rsp-hle audio key: ucode data words at +0x00, +0x28, +0x30
  * rsp-hle task key: 16-bit-sum of the text bytes, min(size, 0xF80) / 2 bytes
usage: ucode_id.py [build-dir]   (from the project root)
"""
import hashlib
import re
import struct
import sys
import zlib

from elftools.elf.elffile import ELFFile

BUILD = sys.argv[1] if len(sys.argv) > 1 else "build"
secs = []
for name in ("hd_code", "hd_front_end"):
    with open("%s/%s.us.v11.elf" % (BUILD, name), "rb") as f:
        for s in ELFFile(f).iter_sections():
            if s["sh_type"] == "SHT_PROGBITS" and s["sh_addr"]:
                secs.append((s["sh_addr"], s.data()))


def rd(a, n):
    for base, data in secs:
        if base <= a and a + n <= base + len(data):
            return data[a - base:a - base + n]
    raise SystemExit("0x%08X+0x%X not in the ELFs" % (a, n))


# (name, text addr, text size, data addr, data size); sizes from the ROM layout:
# A0C30 bin = gfx text | rspboot | audio text | gfx (dram) text, back to back
UCODES = [
    ("rspboot", 0x802E6820, 0xD0, None, 0),
    ("hd gfx (slots 1-3, gF3dText)", 0x802E53F0, 0x802E6820 - 0x802E53F0, 0x8030E390, 0x800),
    ("hd audio (gAspMainText)", 0x802E68F0, 0x802E77B0 - 0x802E68F0, 0x8030EB90, 0x8030EE60 - 0x8030EB90),
    ("hd gfx to-DRAM task (gCullUcodeText, 5FD50 cull_rsp_test)", 0x802E77B0, 0x802E8BD0 - 0x802E77B0, 0x8030EE60, 0x800),
    ("front-end gfx (slot 0, gL3dText)", 0x80207090, 0x80208040 - 0x80207090, 0x80210690, 0x800),
]


def h(b):
    return "crc32 %08X md5 %s sha1 %s" % (zlib.crc32(b), hashlib.md5(b).hexdigest(), hashlib.sha1(b).hexdigest())


def strings(b):
    return [m.group(0).decode() for m in re.finditer(rb"[\x20-\x7e]{8,}", b)]


blobs = {}
for name, ta, tn, da, dn in UCODES:
    text = rd(ta, tn)
    data = rd(da, dn) if da else b""
    blobs[name] = (text, data)
    print("== %s" % name)
    print("   text 0x%08X size 0x%X  %s" % (ta, tn, h(text)))
    t1000 = text[:0x1000]
    print("   text[0:0x1000]  crc32 %08X" % zlib.crc32(t1000))
    if da:
        print("   data 0x%08X size 0x%X  %s" % (da, dn, h(data)))
        for s in strings(data):
            print("   data string: %r" % s)
        g64 = sum(struct.unpack(">768I", text[:3072])) & 0xFFFFFFFF if len(text) >= 3072 else None
        print("   glide64 uc_crc (sum of text words 0..3071): %s" % ("%08X" % g64 if g64 is not None else "-"))
        w = lambda o: struct.unpack(">I", data[o:o + 4])[0] if len(data) >= o + 4 else 0
        print("   data words +0 %08X +0x28 %08X +0x30 %08X" % (w(0), w(0x28), w(0x30)))
    n = min(len(text), 0xF80) >> 1
    print("   rsp-hle sum_bytes(text, 0x%X) = 0x%X" % (n, sum(text[:n])))

# how the two Fast3D copies differ
a, b = blobs["hd gfx (slots 1-3, gF3dText)"], blobs["front-end gfx (slot 0, gL3dText)"]
for what, x, y in (("text", a[0], b[0]), ("data", a[1], b[1])):
    n = min(len(x), len(y))
    diff = [i for i in range(n) if x[i] != y[i]]
    print("hd gfx vs front-end gfx %s: sizes 0x%X/0x%X, %d of 0x%X common bytes differ%s" %
          (what, len(x), len(y), len(diff), n, (", first at 0x%X, last at 0x%X" % (diff[0], diff[-1])) if diff else ""))
a2 = blobs["hd gfx to-DRAM task (gCullUcodeText, 5FD50 cull_rsp_test)"]
for what, x, y in (("text", a[0], a2[0]), ("data", a[1], a2[1])):
    n = min(len(x), len(y))
    diff = [i for i in range(n) if x[i] != y[i]]
    print("hd gfx vs to-DRAM gfx %s: sizes 0x%X/0x%X, %d of 0x%X common bytes differ%s" %
          (what, len(x), len(y), len(diff), n, (", first at 0x%X, last at 0x%X" % (diff[0], diff[-1])) if diff else ""))
