#!/usr/bin/env python3
"""Writes port/src/live/tex_vectors.h: tools_port/bcfiles texture.py encode_slot on synthetic images (an LCG's
noise, a gradient, a checkerboard; every texel format x packings 0, 1, 2, 3, 6; with and without slot texels
after the image; mipmapped slots, their levels of detail made from the image), the SHA-1 of each packed result.  bc_cameratest (terrain_test.cpp) builds the same images
and checks tex_codec.c against them.

    python3 port/tools/texcodec_vectors.py [OUT]"""
import hashlib
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "..", "tools_port"))
from bcfiles import texture  # noqa: E402


def lcg(seed):
    s = seed & 0xFFFFFFFF
    while True:
        s = (s * 1103515245 + 12345) & 0xFFFFFFFF
        yield (s >> 16) & 0xFF


def image(w, h, pattern, seed):
    g = lcg(seed)
    out = bytearray()
    for y in range(h):
        for x in range(w):
            if pattern == 0:
                out += bytes((next(g), next(g), next(g), next(g)))
            elif pattern == 1:
                out += bytes(((x * 255) // max(1, w - 1), (y * 255) // max(1, h - 1), ((x + y) * 7) & 255,
                              255 if (x + y) & 1 else 100))
            else:
                on = ((x // 4) + (y // 4)) & 1
                out += bytes((250, 200, 20, 255) if on else (30, 30, 30, 0))
    return bytes(out)


def palette(n, seed):
    """a synthetic palette entry: n u16 colours (0RRRRRGGGGGBBBBB, big-endian) from the LCG"""
    g = lcg(seed)
    out = bytearray()
    for _ in range(n):
        v = ((next(g) << 8) | next(g)) & 0x7FFF
        out += bytes((v >> 8, v & 0xFF))
    return bytes(out)


def row(fmt, siz, ptype, w, h, pattern, seed, with_orig, mips, pal_n=0, pal_seed=0):
    rgba = image(w, h, pattern, seed)
    orig = None
    pal = palette(pal_n, pal_seed) if pal_n else None
    if with_orig:
        if mips > 1:
            n = texture.mip_layout(w, h, siz, mips)[1]
        else:
            n = len(texture.from_rgba8(rgba, fmt, siz, w, h))
        # (a palette packing's slot is the picture alone: with_orig 2 gives it 64 bytes too many, refused)
        extra = 64 if not pal_n or with_orig == 2 else 0
        orig = bytes((i * 37 + seed) & 0xFF for i in range(n + extra))
    try:
        sha = hashlib.sha1(texture.encode_slot(rgba, w, h, fmt, siz, ptype, orig, mips, pal)).hexdigest()
    except texture.TextureError:
        sha = "error"
    return '    {%d, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d, "%s"}' % (fmt, siz, ptype, w, h, pattern, seed, with_orig,
                                                                    mips, pal_n, pal_seed, sha)


def rows():
    out = []
    for fmt, siz in [(0, 16), (0, 32), (3, 4), (3, 8), (3, 16), (4, 4), (4, 8)]:
        for ptype in (0, 1, 2, 3, 6):
            for (w, h) in ((16, 16), (32, 8)):
                for pattern in (0, 1, 2):
                    seed = fmt * 100 + siz * 10 + ptype + pattern
                    for with_orig in (0, 1):
                        out.append(row(fmt, siz, ptype, w, h, pattern, seed, with_orig, 0))
    # mipmapped slots (levels of detail made from the image, the odd rows' words swapped): the level lists' own
    # shapes (32 x 32, 64 x 16, 16 x 64, 128 x 8, 16 x 32, 32 x 16 RGBA16 in 4 levels), other formats, odd sizes,
    # levels down to 1 x 1, and the refusal (32-bit texels)
    for (w, h) in ((32, 32), (64, 16), (16, 64), (128, 8), (16, 32), (32, 16)):
        for ptype in (0, 1, 3):
            for pattern in (0, 1, 2):
                for with_orig in (0, 1):
                    out.append(row(0, 16, ptype, w, h, pattern, 1000 + w + h + ptype + pattern, with_orig, 4))
    for fmt, siz in [(0, 16), (0, 32), (3, 4), (3, 8), (3, 16), (4, 4), (4, 8)]:
        for (w, h, mips) in ((16, 16, 2), (8, 8, 4), (5, 3, 3), (8, 4, 6)):
            for with_orig in (0, 1):
                out.append(row(fmt, siz, 0, w, h, 1, 2000 + fmt * 10 + siz + w + mips, with_orig, mips))
                out.append(row(fmt, siz, 6, w, h, 0, 2100 + fmt * 10 + siz + w + mips, with_orig, mips))
    # the palette packings: the slot's palette kept, every texel its nearest colour (4: RGBA16 pairs, 64 colours;
    # 5: RGBA32, 128 colours, 4-bit alpha); palettes shorter than that, a slot of another size, the wrong format
    for ptype, siz in ((4, 16), (5, 32)):
        for (w, h) in ((32, 32), (16, 16), (64, 32), (8, 2)):
            for pattern in (0, 1, 2):
                for with_orig in (0, 1):
                    s = 3000 + ptype * 100 + w + h + pattern
                    out.append(row(0, siz, ptype, w, h, pattern, s, with_orig, 0, 64 if ptype == 4 else 128, s + 7))
        out.append(row(0, siz, ptype, 32, 32, 1, 3900 + ptype, 1, 0, 16, 77))       # a 16-colour palette
        out.append(row(0, siz, ptype, 32, 32, 2, 3910 + ptype, 2, 0, 64, 78))       # the slot holds more: refused
        out.append(row(3, 8, ptype, 32, 32, 2, 3920 + ptype, 0, 0, 64, 79))         # not RGBA: refused
        out.append(row(0, siz, ptype, 32, 32, 0, 3930 + ptype, 0, 0, 0, 0))         # no palette: refused
    out.append(row(0, 16, 4, 5, 3, 1, 3940, 0, 0, 64, 80))                          # an odd number of texels: refused
    return out


def main():
    path = sys.argv[1] if len(sys.argv) > 1 else os.path.join(HERE, "..", "src", "live", "tex_vectors.h")
    text = ("/* tex_codec.c's test vectors (bc_cameratest, terrain_test.cpp): tools_port/bcfiles texture.py's encode_slot\n"
            " * on synthetic images (port/tools/texcodec_vectors.py writes this file): fmt, siz, packing, w, h, pattern, seed,\n"
            " * with the slot's texels after them, levels of detail (mips), a synthetic palette's colours and seed (the\n"
            " * palette packings 4, 5), SHA-1 of the packed bytes (\"error\": refused). */\n"
            "static const TexVector kTexVectors[] = {\n" + ",\n".join(rows()) + "\n};\n")
    with open(path, "w", newline="\n") as f:
        f.write(text)
    print("%s: %d cases" % (path, text.count("{") - 1))


if __name__ == "__main__":
    main()
