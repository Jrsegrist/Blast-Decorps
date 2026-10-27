#!/usr/bin/env python3
"""bc.exe's icon (original artwork, drawn here so the repo holds no binary):
a rounded tile with hazard stripes along the top and bottom and "BC" in the
middle.  Writes a Windows .ico with PNG images at 16..256 px.

    make_icon.py OUT.ico
"""
import struct
import sys
import zlib

# 5x7 glyphs
FONT = {
    "B": ["1111.", "1...1", "1...1", "1111.", "1...1", "1...1", "1111."],
    "C": [".1111", "1....", "1....", "1....", "1....", "1....", ".1111"],
}

BG = (40, 42, 48)
YELLOW = (250, 196, 20)
BLACK = (20, 20, 20)
LETTER = (255, 120, 20)
OUTLINE = (12, 12, 14)


def sample(x, y):
    """colour (r, g, b, a) at (x, y) in the unit square"""
    r = 0.16  # corner radius
    cx = min(max(x, r), 1 - r)
    cy = min(max(y, r), 1 - r)
    if (x - cx) ** 2 + (y - cy) ** 2 > r * r:
        return (0, 0, 0, 0)
    # hazard bands: top and bottom fifth, diagonal stripes
    if y < 0.2 or y > 0.8:
        stripe = int((x + y) / 0.125) % 2
        c = YELLOW if stripe == 0 else BLACK
        return c + (255,)
    # letters "BC" in the middle band (y 0.27..0.73), 5x7 cells each
    cell = 0.46 / 7
    top = 0.27
    left = 0.5 - (11 * cell) / 2
    gy = int((y - top) / cell)
    if 0 <= gy < 7:
        for i, ch in enumerate("BC"):
            gx = int((x - left - i * 6 * cell) / cell)
            if 0 <= gx < 5 and (x - left - i * 6 * cell) >= 0 and FONT[ch][gy][gx] == "1":
                return LETTER + (255,)
    return BG + (255,)


def render(size, ss=4):
    rows = []
    for py in range(size):
        row = bytearray()
        for px in range(size):
            acc = [0, 0, 0, 0]
            for sy in range(ss):
                for sx in range(ss):
                    c = sample((px + (sx + 0.5) / ss) / size, (py + (sy + 0.5) / ss) / size)
                    a = c[3]
                    acc[0] += c[0] * a
                    acc[1] += c[1] * a
                    acc[2] += c[2] * a
                    acc[3] += a
            n = ss * ss
            a = acc[3]
            if a == 0:
                row += bytes(4)
            else:
                row += bytes([acc[0] // a, acc[1] // a, acc[2] // a, a // n])
        rows.append(bytes(row))
    return rows


def png(size, rows):
    def chunk(t, d):
        return struct.pack(">I", len(d)) + t + d + struct.pack(">I", zlib.crc32(t + d) & 0xFFFFFFFF)
    raw = b"".join(b"\x00" + r for r in rows)
    return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", size, size, 8, 6, 0, 0, 0)) +
            chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b""))


def main():
    sizes = [16, 24, 32, 48, 64, 128, 256]
    images = [png(s, render(s, 4 if s <= 64 else 2)) for s in sizes]
    out = struct.pack("<HHH", 0, 1, len(sizes))
    off = 6 + 16 * len(sizes)
    for s, data in zip(sizes, images):
        out += struct.pack("<BBBBHHII", s % 256, s % 256, 0, 0, 1, 32, len(data), off)
        off += len(data)
    out += b"".join(images)
    with open(sys.argv[1], "wb") as f:
        f.write(out)


if __name__ == "__main__":
    main()
