"""Minimal PNG writer/reader (RGBA8, standard library only)."""
import struct
import zlib


def _chunk(kind, data):
    c = struct.pack(">I", len(data)) + kind + data
    return c + struct.pack(">I", zlib.crc32(kind + data) & 0xFFFFFFFF)


def encode_rgba(width, height, rgba):
    rows = bytearray()
    stride = width * 4
    for y in range(height):
        rows.append(0)
        rows += rgba[y * stride:(y + 1) * stride]
    return (b"\x89PNG\r\n\x1a\n" + _chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0))
            + _chunk(b"IDAT", zlib.compress(bytes(rows), 9)) + _chunk(b"IEND", b""))


def write_rgba(path, width, height, rgba):
    with open(path, "wb") as f:
        f.write(encode_rgba(width, height, rgba))


def decode_rgba(png):
    """Decode an 8-bit RGBA or RGB, non-interlaced PNG -> (w, h, rgba bytes)."""
    if png[:8] != b"\x89PNG\r\n\x1a\n":
        raise ValueError("not a PNG")
    p = 8
    idat = b""
    w = h = ctype = 0
    while p < len(png):
        n = struct.unpack_from(">I", png, p)[0]
        kind = png[p + 4:p + 8]
        data = png[p + 8:p + 8 + n]
        if kind == b"IHDR":
            w, h, depth, ctype, _, _, inter = struct.unpack(">IIBBBBB", data)
            if depth != 8 or ctype not in (2, 6) or inter:
                raise ValueError("unsupported PNG (need 8-bit RGB/RGBA, no interlace)")
        elif kind == b"IDAT":
            idat += data
        p += 12 + n
    bpp = 4 if ctype == 6 else 3
    raw = zlib.decompress(idat)
    stride = w * bpp
    out = bytearray()
    prev = bytearray(stride)
    q = 0
    for _ in range(h):
        ft = raw[q]
        line = bytearray(raw[q + 1:q + 1 + stride])
        q += 1 + stride
        for i in range(stride):
            a = line[i - bpp] if i >= bpp else 0
            b = prev[i]
            c = prev[i - bpp] if i >= bpp else 0
            if ft == 1:
                line[i] = (line[i] + a) & 255
            elif ft == 2:
                line[i] = (line[i] + b) & 255
            elif ft == 3:
                line[i] = (line[i] + ((a + b) >> 1)) & 255
            elif ft == 4:
                pa, pb, pc = abs(b - c), abs(a - c), abs(a + b - 2 * c)
                pr = a if pa <= pb and pa <= pc else (b if pb <= pc else c)
                line[i] = (line[i] + pr) & 255
        prev = line
        if bpp == 4:
            out += line
        else:
            for i in range(0, stride, 3):
                out += line[i:i + 3] + b"\xff"
    return w, h, bytes(out)
