#!/usr/bin/env python3
"""The texture codec's twin test: port/src/load/tex_codec.c (bc_cameratest --texcodec DIR) against
tools_port/bcfiles png.py / texture.py on synthetic images (nothing from a ROM).

    python3 port/tools/texcodec_cross.py BC_CAMERATEST.EXE [WORKDIR]

PNGs come from png.py's writer (zlib level 9, filter 0) and from a writer here that gives every row another
filter (sub, up, average, Paeth) at several zlib levels; each is decoded by both, then packed into a slot
(every texel format x packings 0, 1, 2, 3, 6, with and without slot texels after the image, also as mipmapped
slots whose levels of detail are made from the image) by both.
Exit 0: every decode and every packed slot byte for byte the same (or refused by both)."""
import os
import random
import struct
import subprocess
import sys
import tempfile
import zlib

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "..", "tools_port"))
from bcfiles import png, texture  # noqa: E402


def chunk(kind, data):
    c = struct.pack(">I", len(data)) + kind + data
    return c + struct.pack(">I", zlib.crc32(kind + data) & 0xFFFFFFFF)


def png_filtered(w, h, rgba, alpha, level, seed):
    """RGBA or RGB rows, each with a filter chosen at random"""
    bpp = 4 if alpha else 3
    rnd = random.Random(seed)
    rows = []
    stride = w * bpp
    prev = bytes(stride)
    for y in range(h):
        line = bytearray()
        for x in range(w):
            p = rgba[4 * (y * w + x):4 * (y * w + x) + 4]
            line += p if alpha else p[:3]
        ft = rnd.randrange(5)
        out = bytearray([ft])
        for i in range(stride):
            a = line[i - bpp] if i >= bpp else 0
            b = prev[i]
            c = prev[i - bpp] if i >= bpp else 0
            if ft == 0:
                pr = 0
            elif ft == 1:
                pr = a
            elif ft == 2:
                pr = b
            elif ft == 3:
                pr = (a + b) >> 1
            else:
                pa, pb, pc = abs(b - c), abs(a - c), abs(a + b - 2 * c)
                pr = a if pa <= pb and pa <= pc else (b if pb <= pc else c)
            out.append((line[i] - pr) & 255)
        rows.append(bytes(out))
        prev = bytes(line)
    raw = b"".join(rows)
    return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 6 if alpha else 2, 0, 0, 0))
            + chunk(b"IDAT", zlib.compress(raw, level)) + chunk(b"IEND", b""))


def image(w, h, kind, seed):
    rnd = random.Random(seed)
    out = bytearray()
    for y in range(h):
        for x in range(w):
            if kind == 0:
                out += bytes(rnd.randrange(256) for _ in range(4))
            elif kind == 1:
                out += bytes(((x * 255) // max(1, w - 1), (y * 255) // max(1, h - 1), (x * y) & 255,
                              255 if (x ^ y) & 2 else 60))
            else:
                on = ((x // 8) + (y // 8)) & 1
                out += bytes((250, 200, 20, 255) if on else (30, 30, 30, 255))
    return bytes(out)


def main():
    exe = sys.argv[1]
    work = sys.argv[2] if len(sys.argv) > 2 else tempfile.mkdtemp(prefix="texcodec_")
    os.makedirs(work, exist_ok=True)
    fmts = [(0, 16), (0, 32), (3, 4), (3, 8), (3, 16), (4, 4), (4, 8)]
    cases = []
    n = 0
    for (w, h) in ((32, 32), (16, 64), (8, 8), (64, 32)):
        for kind in range(3):
            rgba = image(w, h, kind, w * 7 + h + kind)
            pngs = [png.encode_rgba(w, h, rgba)]
            for lvl in (1, 6, 9):
                pngs.append(png_filtered(w, h, rgba, True, lvl, n + lvl))
            pngs.append(png_filtered(w, h, rgba, False, 9, n + 77))
            for pi, data in enumerate(pngs):
                name = "p%04d" % n
                n += 1
                with open(os.path.join(work, name + ".png"), "wb") as f:
                    f.write(data)
                fmt, siz = fmts[(n + pi) % len(fmts)]
                # (and mipmapped: RGBA16 in 4 levels as the level lists draw them, the format's own in 3)
                for mf, ms, mips in ((fmt, siz, 0), (0, 16, 4), (fmt, siz, 3)):
                    for ptype in (0, 1, 2, 3, 6):
                        for with_orig in (0, 1):
                            cname = "%s_%d_%d_%d_%d_%d" % (name, mf, ms, ptype, with_orig, mips)
                            orig = "-"
                            if with_orig:
                                if mips:
                                    tn = texture.mip_layout(w, h, ms, mips)[1]
                                else:
                                    tn = len(texture.from_rgba8(rgba, mf, ms, w, h))
                                o = bytes((i * 13 + n) & 255 for i in range(tn + 40))
                                orig = cname + ".orig"
                                with open(os.path.join(work, orig), "wb") as f:
                                    f.write(o)
                            cases.append((cname, name + ".png", mf, ms, ptype, orig, mips))
    with open(os.path.join(work, "cases.txt"), "w") as f:
        for c in cases:
            f.write("%s %s %d %d %d %s %d\n" % c)
    win = work
    if work.startswith("/mnt/"):
        win = subprocess.run(["wslpath", "-w", work], capture_output=True, text=True).stdout.strip()
    r = subprocess.run([exe, "--texcodec", win], capture_output=True, text=True)
    print(r.stdout.strip())
    same = diff = 0
    for (cname, pngname, fmt, siz, ptype, orig, mips) in cases:
        with open(os.path.join(work, pngname), "rb") as f:
            data = f.read()
        try:
            w, h, rgba = png.decode_rgba(data)
            o = None
            if orig != "-":
                with open(os.path.join(work, orig), "rb") as f:
                    o = f.read()
            try:
                want = ("OK", texture.encode_slot(rgba, w, h, fmt, siz, ptype, o, mips))
            except texture.TextureError:
                want = ("ERR", None)
        except ValueError:
            want, rgba = ("ERR", None), None
        with open(os.path.join(work, cname + ".c.txt")) as f:
            got = f.read().split()[0]
        ok = got == want[0]
        if ok and got == "OK":
            with open(os.path.join(work, cname + ".c.bin"), "rb") as f:
                ok = f.read() == want[1]
            with open(os.path.join(work, cname + ".c.rgba"), "rb") as f:
                ok = ok and f.read() == rgba
        if ok:
            same += 1
        else:
            diff += 1
            if diff <= 5:
                print("DIFF %s: Python %s, C %s" % (cname, want[0], got))
    print("texcodec cross: %d cases the same, %d different (%s)" % (same, diff, work))
    return 0 if diff == 0 and same > 0 else 1


if __name__ == "__main__":
    sys.exit(main())
