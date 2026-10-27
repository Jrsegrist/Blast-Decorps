"""Textures: the texture table's seven packings and N64 texel formats.

Table (ROM 0x4CE0, 4096 entries): {u32 offset from 0x4CE0; u16 packed size;
u16 packing}.  An entry's DMA runs to the next entry's offset; `size` bytes
are decoded (tex_decode, src.us.v11/hd_code/60F60.c).

Packings 1-6 read big-endian s16 tokens.  Bit 15 set: copy (tok & 0x1F)
units from earlier output, one at a time; the distance is
(tok & 0x7FFF) >> 5 bytes for 16-bit units (1, 3, 6) and
(tok & 0x7FE0) >> 4 bytes for 32-bit units (2, 4, 5).  Otherwise a literal:

  0  stored: size >> 3 doublewords (a trailing size & 7 is dropped)
  1  one u16: ((tok & 0xFFC0) << 1) | (tok & 0x3F)          RGBA16 5551
  2  one u32: 4-bit R, G, B to the top of bytes 0-2, 3-bit A to byte 3
  3  two bytes, each << 1                                     (IA8 pairs)
  4  two u16 via a 128-entry u16 palette: (pal[b >> 1] << 1) | (b & 1)
  5  one u32 via a palette of 5-5-5 colours, 4-bit alpha
  6  two bytes xxAAABBB -> AAA0BBB0                           (I4 pairs)

The texel format and size are not stored with the texture; the display
list that uses it says (G_SETTIMG / G_SETTILE / G_SETTILESIZE).
"""
import struct

TEX_TABLE = 0x4CE0
TEX_ENTRIES = 0x1000

UNIT = {1: 2, 2: 4, 3: 2, 4: 4, 5: 4, 6: 2}


def _backref(out, tok, wide):
    n = tok & 0x1F
    if wide:
        dist = (tok & 0x7FE0) >> 4
        unit = 4
    else:
        dist = (tok & 0x7FFF) >> 5
        unit = 2
    src = len(out) - dist
    for _ in range(n):
        out += out[src:src + unit] if src + unit <= len(out) else bytes(
            out[src + k] if src + k < len(out) else 0 for k in range(unit))
        src += unit


def decode(packed, ptype, palette=None):
    """Decode one texture's packed bytes (exactly `size` of them)."""
    if ptype == 0:
        return bytes(packed[:len(packed) & ~7])
    if ptype not in UNIT:
        return b""
    wide = UNIT[ptype] == 4
    out = bytearray()
    pal = palette or bytes(0x100)
    for (tok,) in struct.iter_unpack(">H", bytes(packed[:len(packed) & ~1])):
        if tok & 0x8000:
            _backref(out, tok, wide)
            continue
        if ptype == 1:
            out += struct.pack(">H", ((tok & 0xFFC0) << 1) | (tok & 0x3F))
        elif ptype == 2:
            v = ((tok & 0x7800) << 17) | ((tok & 0x0780) << 13) | ((tok & 0x78) << 9) | ((tok & 7) << 5)
            out += struct.pack(">I", v)
        elif ptype == 3:
            out += bytes((((tok >> 8) << 1) & 0xFF, ((tok & 0xFF) << 1) & 0xFF))
        elif ptype == 4:
            b0, b1 = tok >> 8, tok & 0xFF
            p0 = struct.unpack_from(">H", pal, b0 & 0xFE)[0]
            p1 = struct.unpack_from(">H", pal, b1 & 0xFE)[0]
            out += struct.pack(">HH", ((p0 << 1) | (b0 & 1)) & 0xFFFF, ((p1 << 1) | (b1 & 1)) & 0xFFFF)
        elif ptype == 5:
            i = (tok >> 4) << 1
            p = struct.unpack_from(">H", pal, i)[0] if i + 2 <= len(pal) else 0
            v = ((p & 0x7C00) << 17) | ((p & 0x03E0) << 14) | ((p & 0x1F) << 11) | ((tok & 0xF) << 4)
            out += struct.pack(">I", v)
        elif ptype == 6:
            def f(b):
                return ((b & 0x38) << 2) | ((b & 7) << 1)
            out += bytes((f(tok >> 8), f(tok & 0xFF)))
    return bytes(out)


def encode_stored(texels):
    """Packing 0 for raw texels (padded to a multiple of 8)."""
    t = bytes(texels)
    return t + bytes(-len(t) % 8)


class TextureError(Exception):
    pass


# what each packing's literals can hold: texels are rounded to that first
def quantize(texels, ptype):
    t = bytearray(texels)
    if ptype == 1:                       # u16 with bit 6 clear
        for i in range(0, len(t) - 1, 2):
            t[i + 1] &= 0xBF
    elif ptype == 2:                     # 4-bit R, G, B, 3-bit A
        for i in range(0, len(t) - 3, 4):
            t[i] &= 0xF0
            t[i + 1] &= 0xF0
            t[i + 2] &= 0xF0
            t[i + 3] &= 0xE0
    elif ptype == 3:                     # even bytes
        for i in range(len(t)):
            t[i] &= 0xFE
    elif ptype == 6:                     # AAA0BBB0
        for i in range(len(t)):
            t[i] &= 0xEE
    return bytes(t)


def _literal(unit, ptype):
    if ptype == 1:
        v = (unit[0] << 8) | unit[1]
        return ((v & 0xFF80) >> 1) | (v & 0x3F)
    if ptype == 2:
        r, g, b, a = unit
        return ((r >> 4) << 11) | ((g >> 4) << 7) | ((b >> 4) << 3) | (a >> 5)
    if ptype == 3:
        return ((unit[0] >> 1) << 8) | (unit[1] >> 1)
    if ptype == 6:
        def f(b):
            return ((b >> 5) << 3) | ((b >> 1) & 7)
        return (f(unit[0]) << 8) | f(unit[1])
    raise TextureError("packing %d is not encoded here (palette packings 4 and 5)" % ptype)


def pack(texels, ptype, tokens=None):
    """texels (already representable: quantize) -> the packing's token stream,
    literals and back-references (greedy, the longest match in the window).
    tokens: the palette packings' literal of every unit (palette_texels)"""
    if ptype == 0:
        return encode_stored(texels)
    if ptype in (4, 5) and tokens is None:
        raise TextureError("packing %d needs the slot's palette (palette_texels)" % ptype)
    if ptype not in (1, 2, 3, 4, 5, 6):
        raise TextureError("packing %d is not known" % ptype)
    us = UNIT[ptype]
    t = bytes(texels)
    if len(t) % us:
        raise TextureError("%d bytes of texels is not whole %d-byte units" % (len(t), us))
    units = [t[i:i + us] for i in range(0, len(t), us)]
    maxd = 511 if us == 4 else 511       # units back (the token's 10-bit byte distance)
    out = bytearray()
    i = 0
    n = len(units)
    index = {}
    while i < n:
        best_len, best_d = 0, 0
        for j in reversed(index.get(units[i], [])):
            d = i - j
            if d > maxd:
                break
            k = 0
            while k < 31 and i + k < n and units[j + k] == units[i + k]:
                k += 1
            if k > best_len:
                best_len, best_d = k, d
                if k == 31:
                    break
        if best_len >= 2:
            db = best_d * us
            tok = 0x8000 | ((db << 5) if us == 2 else (db << 4)) | best_len
            out += struct.pack(">H", tok)
            for q in range(i, i + best_len):
                index.setdefault(units[q], []).append(q)
            i += best_len
        else:
            out += struct.pack(">H", tokens[i] if tokens is not None else _literal(units[i], ptype))
            index.setdefault(units[i], []).append(i)
            i += 1
    return bytes(out)


def palette_texels(rgba, width, height, ptype, palette):
    """a picture in a palette packing's slot, the slot's own palette kept
    (every palette of the game is shared by many textures): each texel the
    nearest palette colour (squared distance over R, G, B as the palette
    entries expand them; the lowest index on a tie) -> (texels as the game
    decodes them, the literal token of every 4-byte unit).
      4: RGBA16 texels, a pair per token: byte (index << 1) | alpha bit; the
         first texel of a pair can only take indexes 0..63 (a literal's top
         bit is clear), the palette holds 64 (its 0x80 bytes)
      5: RGBA32 texels, one per token: (index << 4) | 4-bit alpha
    palette: the palette entry's bytes (u16 0RRRRRGGGGGBBBBB, big-endian)"""
    n = width * height
    if len(rgba) < 4 * n:
        raise TextureError("the image has %d pixels, %dx%d expected" % (len(rgba) // 4, width, height))
    if ptype not in (4, 5):
        raise TextureError("packing %d has no palette" % ptype)
    if ptype == 4 and n % 2:
        raise TextureError("packing 4 holds texels in pairs: %dx%d is an odd number" % (width, height))
    count = min(len(palette) // 2, 64 if ptype == 4 else 128)
    if count < 1:
        raise TextureError("the palette is empty")
    pal = [struct.unpack_from(">H", palette, 2 * i)[0] for i in range(count)]
    rgb = [(((p >> 10) & 31) << 3, ((p >> 5) & 31) << 3, (p & 31) << 3) for p in pal]
    best = {}

    def nearest(c):
        k = (c[0], c[1], c[2])
        if k not in best:
            bi, bd = 0, None
            for i, q in enumerate(rgb):
                d = (q[0] - c[0]) ** 2 + (q[1] - c[1]) ** 2 + (q[2] - c[2]) ** 2
                if bd is None or d < bd:
                    bi, bd = i, d
            best[k] = bi
        return best[k]
    out = bytearray()
    toks = []
    if ptype == 4:
        for i in range(0, n, 2):
            bs = []
            for q in (i, i + 1):
                c = rgba[4 * q:4 * q + 4]
                ix, a = nearest(c), 1 if c[3] >= 128 else 0
                bs.append((ix << 1) | a)
                out += struct.pack(">H", ((pal[ix] << 1) | a) & 0xFFFF)
            toks.append(bs[0] << 8 | bs[1])
    else:
        for q in range(n):
            c = rgba[4 * q:4 * q + 4]
            ix, a = nearest(c), c[3] >> 4
            p = pal[ix]
            out += struct.pack(">I", ((p & 0x7C00) << 17) | ((p & 0x03E0) << 14) | ((p & 0x1F) << 11) | (a << 4))
            toks.append((ix << 4) | a)
    return bytes(out), toks


def from_rgba8(rgba, fmt, siz, width, height):
    """RGBA8 rows -> N64 texels (fmt 0 RGBA, 3 IA, 4 I; siz in bits).  CI needs
    the palette of the slot and is not done here."""
    n = width * height
    if len(rgba) < 4 * n:
        raise TextureError("the image has %d pixels, %dx%d expected" % (len(rgba) // 4, width, height))
    px = [tuple(rgba[4 * i:4 * i + 4]) for i in range(n)]

    def lum(c):
        return (c[0] * 299 + c[1] * 587 + c[2] * 114 + 500) // 1000
    if fmt == 0 and siz == 16:
        return b"".join(struct.pack(">H", ((c[0] >> 3) << 11) | ((c[1] >> 3) << 6) | ((c[2] >> 3) << 1) |
                                    (1 if c[3] >= 128 else 0)) for c in px)
    if fmt == 0 and siz == 32:
        return b"".join(bytes(c) for c in px)
    if fmt == 3 and siz == 16:
        return b"".join(bytes((lum(c), c[3])) for c in px)
    if fmt == 3 and siz == 8:
        return bytes(((lum(c) >> 4) << 4) | (c[3] >> 4) for c in px)
    if fmt == 4 and siz == 8:
        return bytes(lum(c) for c in px)
    if fmt in (3, 4) and siz == 4:
        def nib(c):
            return ((lum(c) >> 5) << 1) | (1 if c[3] >= 128 else 0) if fmt == 3 else lum(c) >> 4
        out = bytearray()
        for i in range(0, n, 2):
            out.append((nib(px[i]) << 4) | (nib(px[i + 1]) if i + 1 < n else 0))
        return bytes(out)
    raise TextureError("texel format %s %d-bit is not encoded here" % (G_IM_FMT.get(fmt, fmt), siz))


def mip_layout(width, height, siz, levels):
    """the levels of detail of a mipmapped slot as the level lists lay them out
    in TMEM (one G_LOADBLOCK with dxt 0, tiles 0..levels-1): level k is
    (width >> k) x (height >> k) (at least 1), its rows padded to 8 bytes, the
    levels back to back -> ([(offset, line bytes, w, h)], total bytes)"""
    out, off = [], 0
    for k in range(levels):
        w, h = max(1, width >> k), max(1, height >> k)
        line = (w * siz + 63) // 64 * 8
        out.append((off, line, w, h))
        off += line * h
    return out, off


def mip_down(rgba, width, height):
    """the next level of detail: every texel the rounded mean of the 2 x 2 (or
    fewer, at an edge of one) texels it covers, each channel -> (rgba, w, h)"""
    w, h = max(1, width >> 1), max(1, height >> 1)
    out = bytearray(4 * w * h)
    for y in range(h):
        ys = [q for q in (2 * y, 2 * y + 1) if q < height]
        for x in range(w):
            xs = [q for q in (2 * x, 2 * x + 1) if q < width]
            n = len(xs) * len(ys)
            for c in range(4):
                s = sum(rgba[4 * (yy * width + xx) + c] for yy in ys for xx in xs)
                out[4 * (y * w + x) + c] = (s + n // 2) // n
    return bytes(out), w, h


def mip_texels(rgba, width, height, fmt, siz, levels, original=None):
    """the picture and its smaller levels in a mipmapped slot's layout
    (mip_layout): each level from the one before (mip_down), converted to the
    slot's texels, every odd row's 32-bit words swapped (a G_LOADBLOCK with dxt 0
    leaves the row interleave TMEM reads to the data: the slots hold it so); the
    bytes the layout doesn't cover (row padding) from `original` (else 0)"""
    if siz == 32:
        raise TextureError("mipmaps of 32-bit texels are not done (no level list draws one)")
    if not 1 <= levels <= 12:
        raise TextureError("%d levels of detail: 1..12 expected" % levels)
    lay, total = mip_layout(width, height, siz, levels)
    buf = bytearray(original[:total]) if original is not None else bytearray()
    buf += bytes(total - len(buf))
    img, w, h = bytes(rgba), width, height
    for k, (off, line, lw, lh) in enumerate(lay):
        if k:
            img, w, h = mip_down(img, w, h)
        for y in range(h):
            r = bytearray(buf[off + y * line:off + (y + 1) * line])
            src = from_rgba8(img[4 * w * y:4 * w * (y + 1)], fmt, siz, w, 1)   # (a row starts on a byte)
            r[:len(src)] = src
            if y & 1:
                for q in range(0, line - 7, 8):
                    r[q:q + 4], r[q + 4:q + 8] = r[q + 4:q + 8], r[q:q + 4]
            buf[off + y * line:off + (y + 1) * line] = r
    return bytes(buf)


def unswap_rows(texels, width, height, siz):
    """level 0 of a mipmapped slot as plain rows (its odd rows' 32-bit words swapped back), the rest as it is"""
    line = (width * siz + 63) // 64 * 8
    t = bytearray(texels)
    for y in range(1, height, 2):
        o = y * line
        if o + line > len(t):
            break
        for q in range(o, o + line - 7, 8):
            t[q:q + 4], t[q + 4:q + 8] = t[q + 4:q + 8], t[q:q + 4]
    return bytes(t)


def encode_slot(rgba, width, height, fmt, siz, ptype, original=None, mips=0, palette=None):
    """an image for a texture-table slot: its texel format (the lists that use
    it say: fmt, siz) and its packing; -> the packed bytes (they decode to the
    image, rounded to what the packing holds).  original: the slot's decoded
    texels; a slot that holds more than the image (other tiles after it) keeps
    the rest, and the result decodes to as many bytes as before.  mips: the
    number of levels of detail the slot's lists draw it with (mip_layout; 0 or
    1: none): the smaller levels are made from the image (mip_texels) instead of
    keeping the slot's own.  palette: the palette packings' (4, 5) palette
    entry bytes: the picture takes the slot's palette's colours (palette_texels);
    such a slot is one picture (RGBA16 for 4, RGBA32 for 5), nothing after it"""
    if isinstance(original, int):
        original = bytes(original)
    if ptype in (4, 5):
        if palette is None:
            raise TextureError("packing %d: the slot's palette is needed (its effect's or sprite's palette entry)"
                               % ptype)
        want = (0, 16) if ptype == 4 else (0, 32)
        if (fmt, siz) != want:
            raise TextureError("packing %d decodes to RGBA%d texels, not %s %d-bit" % (ptype, want[1],
                                                                                     G_IM_FMT.get(fmt, fmt), siz))
        tex, toks = palette_texels(rgba, width, height, ptype, palette)
        if original is not None and len(original) != len(tex):
            raise TextureError("%dx%d is %d bytes of texels, the slot holds %d: use its size" % (
                width, height, len(tex), len(original)))
        packed = pack(tex, ptype, toks)
        if decode(packed, ptype, bytes(palette) + bytes(max(0, 0x100 - len(palette))))[:len(tex)] != tex:
            raise TextureError("internal: the packed image doesn't decode back")
        if len(packed) > 0xFFFF:
            raise TextureError("%d packed bytes: the table's size field holds at most 65535" % len(packed))
        return packed
    if mips and mips > 1:
        tex = mip_texels(rgba, width, height, fmt, siz, mips, original)
    else:
        tex = from_rgba8(rgba, fmt, siz, width, height)
    if original is not None:
        if len(tex) > len(original):
            raise TextureError("%dx%d %s%d%s is %d bytes of texels, the slot holds %d: use the size its lists use"
                               % (width, height, G_IM_FMT.get(fmt, fmt), siz,
                                  " with %d levels of detail" % mips if mips and mips > 1 else "", len(tex),
                                  len(original)))
        tex += bytes(original[len(tex):])
    q = quantize(tex, ptype)
    packed = pack(q, ptype)
    if decode(packed, ptype)[:len(q)] != q:
        raise TextureError("internal: the packed image doesn't decode back")
    if len(packed) > 0xFFFF:
        raise TextureError("%d packed bytes: the table's size field holds at most 65535" % len(packed))
    return packed


class TexEntry:
    __slots__ = ("index", "offset", "size", "type", "dma_len")

    def __init__(self, index, offset, size, ptype, dma_len):
        self.index, self.offset, self.size, self.type, self.dma_len = index, offset, size, ptype, dma_len

    @property
    def rom(self):
        return TEX_TABLE + self.offset


def read_table(rom):
    ents = []
    raw = [struct.unpack_from(">IHH", rom, TEX_TABLE + 8 * i) for i in range(TEX_ENTRIES)]
    for i, (off, size, t) in enumerate(raw):
        nxt = raw[i + 1][0] if i + 1 < TEX_ENTRIES else off + size
        ents.append(TexEntry(i, off, size, t, nxt - off))
    return ents


# --------------------------------------------------------------- texel formats

G_IM_FMT = {0: "RGBA", 1: "YUV", 2: "CI", 3: "IA", 4: "I"}
G_IM_SIZ = {0: 4, 1: 8, 2: 16, 3: 32}


def to_rgba8(texels, fmt, siz, width, height, tlut=None):
    """N64 texels -> RGBA8 rows (bytes, width*height*4).  fmt: 0 RGBA, 2 CI,
    3 IA, 4 I; siz: bits per texel (4, 8, 16, 32)."""
    n = width * height
    out = bytearray(n * 4)
    t = bytes(texels)

    def px(i):
        if siz == 4:
            b = t[i >> 1] if (i >> 1) < len(t) else 0
            return (b >> 4) if not i & 1 else (b & 0xF)
        if siz == 8:
            return t[i] if i < len(t) else 0
        if siz == 16:
            return (t[2 * i] << 8 | t[2 * i + 1]) if 2 * i + 1 < len(t) else 0
        return struct.unpack_from(">I", t, 4 * i)[0] if 4 * i + 3 < len(t) else 0

    for i in range(n):
        v = px(i)
        if fmt == 0 and siz == 16:
            r, g, b, a = (v >> 11) & 31, (v >> 6) & 31, (v >> 1) & 31, v & 1
            c = ((r << 3) | (r >> 2), (g << 3) | (g >> 2), (b << 3) | (b >> 2), 255 if a else 0)
        elif fmt == 0 and siz == 32:
            c = (v >> 24, (v >> 16) & 255, (v >> 8) & 255, v & 255)
        elif fmt == 3 and siz == 4:
            iv = (v >> 1) * 255 // 7
            c = (iv, iv, iv, 255 if v & 1 else 0)
        elif fmt == 3 and siz == 8:
            iv, a = (v >> 4) * 17, (v & 15) * 17
            c = (iv, iv, iv, a)
        elif fmt == 3 and siz == 16:
            c = (v >> 8, v >> 8, v >> 8, v & 255)
        elif fmt == 4 and siz == 4:
            c = (v * 17,) * 4
        elif fmt == 4 and siz == 8:
            c = (v,) * 4
        elif fmt == 2 and tlut is not None:
            e = struct.unpack_from(">H", tlut, 2 * v)[0] if 2 * v + 2 <= len(tlut) else 0
            r, g, b, a = (e >> 11) & 31, (e >> 6) & 31, (e >> 1) & 31, e & 1
            c = ((r << 3) | (r >> 2), (g << 3) | (g >> 2), (b << 3) | (b >> 2), 255 if a else 0)
        else:
            iv = v & 255
            c = (iv, iv, iv, 255)
        out[4 * i:4 * i + 4] = bytes(c)
    return bytes(out)
