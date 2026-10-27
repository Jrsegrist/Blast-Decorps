/* Texture slots for mods: PNG and the N64 texture packings (see tex_codec.h).
 * Every step mirrors tools_port/bcfiles texture.py / png.py (the same
 * rounding, the same greedy back-references), so both give the same bytes. */
#include "tex_codec.h"
#include "host_util.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* inflate.c: raw deflate */
long inflate_raw_used(const uint8_t *src, size_t srclen, uint8_t *dst, size_t dstmax, size_t *used);


/* ------------------------------------------------------------------ checksums */
static uint32_t g_crc[256];
static uint32_t crc32_of(const uint8_t *p, size_t n, uint32_t c) {
    size_t i;
    if (g_crc[1] == 0) {
        uint32_t k, v;
        int j;
        for (k = 0; k < 256; k++) {
            v = k;
            for (j = 0; j < 8; j++) v = v & 1 ? 0xEDB88320u ^ (v >> 1) : v >> 1;
            g_crc[k] = v;
        }
    }
    c = ~c;
    for (i = 0; i < n; i++) c = g_crc[(c ^ p[i]) & 0xFF] ^ (c >> 8);
    return ~c;
}
static uint32_t adler32_of(const uint8_t *p, size_t n) {
    uint32_t a = 1, b = 0;
    size_t i;
    for (i = 0; i < n; i++) {
        a = (a + p[i]) % 65521u;
        b = (b + a) % 65521u;
    }
    return b << 16 | a;
}

/* ------------------------------------------------------------------ PNG */
static int paeth(int a, int b, int c) {
    const int pa = abs(b - c), pb = abs(a - c), pc = abs(a + b - 2 * c);
    return pa <= pb && pa <= pc ? a : (pb <= pc ? b : c);
}

int tc_png_decode(const uint8_t *png, size_t n, int any, uint32_t *w, uint32_t *h, uint8_t **rgba, char *err,
                  size_t errlen) {
    static const uint8_t sig[8] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};
    Buf idat = {0};
    uint8_t pal[256][4];
    unsigned npal = 0, depth = 0, ctype = 0, inter = 0, chans, bitsPx, bpp;
    uint32_t W = 0, H = 0, y, x;
    size_t p = 8, stride, rawn, used = 0;
    uint8_t *raw = NULL, *prev = NULL, *line = NULL, *out = NULL;
    int seenHdr = 0, i;
    long got;
    *rgba = NULL;
    memset(pal, 0, sizeof pal);
    for (i = 0; i < 256; i++) pal[i][3] = 255;
    if (n < 8 || memcmp(png, sig, 8) != 0) {
        seterr(err, errlen, "not a PNG");
        return -1;
    }
    while (p + 8 <= n) {
        const uint32_t len = get_be32(png + p);
        const uint8_t *kind = png + p + 4, *data = png + p + 8;
        if (len > n - p - 8) {
            seterr(err, errlen, "a PNG chunk runs past the file");
            goto fail;
        }
        if (!memcmp(kind, "IHDR", 4) && len >= 13) {
            W = get_be32(data), H = get_be32(data + 4), depth = data[8], ctype = data[9], inter = data[12];
            seenHdr = 1;
            if (!any && (depth != 8 || (ctype != 2 && ctype != 6) || inter)) {
                seterr(err, errlen, "unsupported PNG (need 8-bit RGB/RGBA, no interlace)");
                goto fail;
            }
        } else if (!memcmp(kind, "PLTE", 4)) {
            for (npal = 0; npal < 256 && 3 * npal + 3 <= len; npal++)
                pal[npal][0] = data[3 * npal], pal[npal][1] = data[3 * npal + 1], pal[npal][2] = data[3 * npal + 2];
        } else if (!memcmp(kind, "tRNS", 4) && ctype == 3) {
            for (i = 0; i < 256 && (uint32_t) i < len; i++) pal[i][3] = data[i];
        } else if (!memcmp(kind, "IDAT", 4)) {
            if (buf_add(&idat, data, len)) goto oom;
        }
        p += 12 + (size_t) len;
    }
    if (!seenHdr || W == 0 || H == 0 || W > 8192 || H > 8192) {
        seterr(err, errlen, "a PNG without a usable header");
        goto fail;
    }
    if (inter) {
        seterr(err, errlen, "interlaced PNGs aren't read (save it without interlacing)");
        goto fail;
    }
    switch (ctype) {
        case 0: chans = 1; break;
        case 2: chans = 3; break;
        case 3: chans = 1; break;
        case 4: chans = 2; break;
        case 6: chans = 4; break;
        default:
            seterr(err, errlen, "PNG colour type %u not known", ctype);
            goto fail;
    }
    if (!(depth == 8 || depth == 16 || ((ctype == 0 || ctype == 3) && (depth == 1 || depth == 2 || depth == 4))) ||
        (ctype == 3 && depth == 16)) {
        seterr(err, errlen, "PNG bit depth %u with colour type %u not known", depth, ctype);
        goto fail;
    }
    bitsPx = chans * depth;
    bpp = bitsPx >= 8 ? bitsPx / 8 : 1;
    stride = ((size_t) W * bitsPx + 7) / 8;
    rawn = (size_t) H * (stride + 1);
    if (idat.n < 6 || (idat.p[0] & 0x0F) != 8 || ((idat.p[0] << 8) | idat.p[1]) % 31 != 0 || (idat.p[1] & 0x20)) {
        seterr(err, errlen, "the PNG's image data isn't zlib data");
        goto fail;
    }
    raw = (uint8_t *) malloc(rawn + 1);
    prev = (uint8_t *) calloc(stride + 1, 1);
    line = (uint8_t *) malloc(stride + 1);
    out = (uint8_t *) malloc((size_t) W * H * 4 + 1);
    if (raw == NULL || prev == NULL || line == NULL || out == NULL) goto oom;
    got = inflate_raw_used(idat.p + 2, idat.n - 2, raw, rawn, &used);
    if (got < 0 || (size_t) got < rawn) {
        seterr(err, errlen, "the PNG's image data doesn't inflate to its size");
        goto fail;
    }
    if (2 + used + 4 <= idat.n && get_be32(idat.p + 2 + used) != adler32_of(raw, (size_t) got)) {
        seterr(err, errlen, "the PNG's image data is damaged (Adler-32)");
        goto fail;
    }
    for (y = 0; y < H; y++) {
        const uint8_t *q = raw + (size_t) y * (stride + 1);
        const int ft = q[0];
        size_t k;
        memcpy(line, q + 1, stride);
        for (k = 0; k < stride; k++) {
            const int a = k >= bpp ? line[k - bpp] : 0, b = prev[k], c = k >= bpp ? prev[k - bpp] : 0;
            if (ft == 1) line[k] = (uint8_t) (line[k] + a);
            else if (ft == 2) line[k] = (uint8_t) (line[k] + b);
            else if (ft == 3) line[k] = (uint8_t) (line[k] + ((a + b) >> 1));
            else if (ft == 4) line[k] = (uint8_t) (line[k] + paeth(a, b, c));
        }
        memcpy(prev, line, stride);
        for (x = 0; x < W; x++) {
            uint8_t *o = out + ((size_t) y * W + x) * 4;
            unsigned v[4] = {0, 0, 0, 255}, c;
            for (c = 0; c < chans; c++) {
                if (depth == 8) v[c] = line[(size_t) x * chans + c];
                else if (depth == 16) v[c] = line[((size_t) x * chans + c) * 2];
                else {
                    const size_t bit = (size_t) x * depth;
                    v[c] = (line[bit / 8] >> (8 - depth - bit % 8)) & ((1u << depth) - 1);
                }
            }
            if (ctype == 3) {
                const unsigned ix = v[0];
                if (ix < 256) o[0] = pal[ix][0], o[1] = pal[ix][1], o[2] = pal[ix][2], o[3] = pal[ix][3];
                else o[0] = o[1] = o[2] = 0, o[3] = 255;
                continue;
            }
            if ((ctype == 0 || ctype == 4) && depth < 8) v[0] = v[0] * 255 / ((1u << depth) - 1);
            if (ctype == 0) o[0] = o[1] = o[2] = (uint8_t) v[0], o[3] = 255;
            else if (ctype == 4) o[0] = o[1] = o[2] = (uint8_t) v[0], o[3] = (uint8_t) v[1];
            else o[0] = (uint8_t) v[0], o[1] = (uint8_t) v[1], o[2] = (uint8_t) v[2], o[3] = (uint8_t) (chans == 4 ? v[3] : 255);
        }
    }
    free(idat.p), free(raw), free(prev), free(line);
    *w = W, *h = H, *rgba = out;
    return 0;
oom:
    seterr(err, errlen, "out of memory");
fail:
    free(idat.p), free(raw), free(prev), free(line), free(out);
    return -1;
}

static int chunk(Buf *b, const char *kind, const uint8_t *d, size_t n) {
    uint8_t q[8];
    uint32_t c;
    put_be32(q, (uint32_t) n);
    memcpy(q + 4, kind, 4);
    if (buf_add(b, q, 8) || buf_add(b, d, n)) return -1;
    c = crc32_of(q + 4, 4, 0);
    c = crc32_of(d, n, c);
    put_be32(q, c);
    return buf_add(b, q, 4);
}

uint8_t *tc_png_encode(const uint8_t *rgba, uint32_t w, uint32_t h, size_t *n) {
    return tc_png_encode_px(rgba, w, h, 4, n);
}

uint8_t *tc_png_encode_px(const uint8_t *px, uint32_t w, uint32_t h, int channels, size_t *n) {
    static const uint8_t sig[8] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};
    Buf out = {0}, z = {0};
    uint8_t hdr[13], *rows;
    const size_t stride = (size_t) w * (size_t) channels, rn = (size_t) h * (stride + 1);
    size_t y, at;
    rows = (uint8_t *) malloc(rn + 1);
    if (rows == NULL) return NULL;
    for (y = 0; y < h; y++) {
        rows[y * (stride + 1)] = 0;
        memcpy(rows + y * (stride + 1) + 1, px + y * stride, stride);
    }
    put_be32(hdr, w), put_be32(hdr + 4, h);
    hdr[8] = 8, hdr[9] = channels == 3 ? 2 : 6, hdr[10] = hdr[11] = hdr[12] = 0;   /* (colour type RGB / RGBA) */
    {
        static const uint8_t zh[2] = {0x78, 0x01};
        uint8_t ad[4];
        if (buf_add(&z, zh, 2)) goto oom;
        for (at = 0; at < rn;) {
            const size_t len = rn - at > 65535 ? 65535 : rn - at;
            uint8_t bh[5];
            bh[0] = (uint8_t) (at + len >= rn ? 1 : 0);
            bh[1] = (uint8_t) len, bh[2] = (uint8_t) (len >> 8), bh[3] = (uint8_t) ~len, bh[4] = (uint8_t) (~len >> 8);
            if (buf_add(&z, bh, 5) || buf_add(&z, rows + at, len)) goto oom;
            at += len;
        }
        put_be32(ad, adler32_of(rows, rn));
        if (buf_add(&z, ad, 4)) goto oom;
    }
    if (buf_add(&out, sig, 8) || chunk(&out, "IHDR", hdr, 13) || chunk(&out, "IDAT", z.p, z.n) ||
        chunk(&out, "IEND", NULL, 0))
        goto oom;
    free(rows), free(z.p);
    *n = out.n;
    return out.p;
oom:
    free(rows), free(z.p), free(out.p);
    return NULL;
}

static const char kB64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

char *tc_base64_encode(const uint8_t *p, size_t n) {
    char *s = (char *) malloc((n + 2) / 3 * 4 + 1), *o = s;
    size_t i;
    if (s == NULL) return NULL;
    for (i = 0; i + 2 < n; i += 3) {
        const uint32_t v = (uint32_t) p[i] << 16 | (uint32_t) p[i + 1] << 8 | p[i + 2];
        *o++ = kB64[v >> 18], *o++ = kB64[(v >> 12) & 63], *o++ = kB64[(v >> 6) & 63], *o++ = kB64[v & 63];
    }
    if (n - i == 1) {
        const uint32_t v = (uint32_t) p[i] << 16;
        *o++ = kB64[v >> 18], *o++ = kB64[(v >> 12) & 63], *o++ = '=', *o++ = '=';
    } else if (n - i == 2) {
        const uint32_t v = (uint32_t) p[i] << 16 | (uint32_t) p[i + 1] << 8;
        *o++ = kB64[v >> 18], *o++ = kB64[(v >> 12) & 63], *o++ = kB64[(v >> 6) & 63], *o++ = '=';
    }
    *o = 0;
    return s;
}

int tc_base64_decode(const char *s, uint8_t **out, size_t *n) {
    const size_t len = strlen(s);
    uint8_t *o = (uint8_t *) malloc(len / 4 * 3 + 4);
    uint32_t v = 0;
    int k = 0, pad = 0;
    size_t i, m = 0;
    if (o == NULL) return -1;
    for (i = 0; i < len; i++) {
        const char c = s[i];
        const char *q;
        if (c == '\n' || c == '\r' || c == ' ' || c == '\t') continue;
        if (c == '=') {
            pad++;
            continue;
        }
        q = strchr(kB64, c);
        if (q == NULL || c == 0 || pad) {
            free(o);
            return -1;
        }
        v = v << 6 | (uint32_t) (q - kB64);
        if (++k == 4) {
            o[m++] = (uint8_t) (v >> 16), o[m++] = (uint8_t) (v >> 8), o[m++] = (uint8_t) v;
            v = 0, k = 0;
        }
    }
    if (k == 2) o[m++] = (uint8_t) (v >> 4);
    else if (k == 3) o[m++] = (uint8_t) (v >> 10), o[m++] = (uint8_t) (v >> 2);
    else if (k == 1) {
        free(o);
        return -1;
    }
    *out = o, *n = m;
    return 0;
}

/* ------------------------------------------------------------------ the packings */
static int unit_of(int ptype) {
    return ptype == 1 || ptype == 3 || ptype == 6 ? 2 : ptype == 2 || ptype == 4 || ptype == 5 ? 4 : 0;
}

/* Python's out[a:b] (negative indexes count from the end) appended to out */
static int py_slice_add(Buf *b, long a, long e) {
    const long n = (long) b->n;
    uint8_t tmp[8];
    if (a < 0) a += n;
    if (a < 0) a = 0;
    if (a > n) a = n;
    if (e < 0) e += n;
    if (e < 0) e = 0;
    if (e > n) e = n;
    if (e <= a) return 0;
    memcpy(tmp, b->p + a, (size_t) (e - a));
    return buf_add(b, tmp, (size_t) (e - a));
}

static int backref(Buf *b, unsigned tok, int wide) {
    const unsigned n = tok & 0x1F;
    const long dist = wide ? (long) ((tok & 0x7FE0) >> 4) : (long) ((tok & 0x7FFF) >> 5);
    const int unit = wide ? 4 : 2;
    long src = (long) b->n - dist;
    unsigned i;
    for (i = 0; i < n; i++) {
        if (src + unit <= (long) b->n) {
            if (py_slice_add(b, src, src + unit)) return -1;
        } else {
            uint8_t u[4];
            int k;
            for (k = 0; k < unit; k++) {
                const long at = src + k;
                u[k] = at < (long) b->n ? (at >= 0 ? b->p[at] : (at + (long) b->n >= 0 ? b->p[at + (long) b->n] : 0)) : 0;
            }
            if (buf_add(b, u, (size_t) unit)) return -1;
        }
        src += unit;
    }
    return 0;
}

int tc_decode(const uint8_t *packed, size_t n, int ptype, const uint8_t *pal, size_t paln, uint8_t **out,
              size_t *outn) {
    static const uint8_t zero[0x100] = {0};
    Buf b = {0};
    size_t i;
    const int wide = unit_of(ptype) == 4;
    if (pal == NULL) pal = zero, paln = sizeof zero;
    if (ptype == 0) {
        const size_t m = n & ~(size_t) 7;
        uint8_t *p = (uint8_t *) malloc(m + 1);
        if (p == NULL) return -1;
        memcpy(p, packed, m);
        *out = p, *outn = m;
        return 0;
    }
    if (unit_of(ptype) == 0) {
        *out = (uint8_t *) calloc(1, 1), *outn = 0;
        return *out ? 0 : -1;
    }
    for (i = 0; i + 2 <= (n & ~(size_t) 1); i += 2) {
        const unsigned tok = (unsigned) packed[i] << 8 | packed[i + 1];
        uint8_t u[4];
        if (tok & 0x8000) {
            if (backref(&b, tok, wide)) goto oom;
            continue;
        }
        switch (ptype) {
            case 1: {
                const unsigned v = ((tok & 0xFFC0) << 1) | (tok & 0x3F);
                u[0] = (uint8_t) (v >> 8), u[1] = (uint8_t) v;
                if (buf_add(&b, u, 2)) goto oom;
                break;
            }
            case 2: {
                const uint32_t v = ((tok & 0x7800u) << 17) | ((tok & 0x0780u) << 13) | ((tok & 0x78u) << 9) | ((tok & 7u) << 5);
                put_be32(u, v);
                if (buf_add(&b, u, 4)) goto oom;
                break;
            }
            case 3:
                u[0] = (uint8_t) (((tok >> 8) << 1) & 0xFF), u[1] = (uint8_t) (((tok & 0xFF) << 1) & 0xFF);
                if (buf_add(&b, u, 2)) goto oom;
                break;
            case 4: {
                const unsigned b0 = tok >> 8, b1 = tok & 0xFF;
                const unsigned p0 = (b0 & 0xFE) + 2 <= paln ? (unsigned) pal[b0 & 0xFE] << 8 | pal[(b0 & 0xFE) + 1] : 0;
                const unsigned p1 = (b1 & 0xFE) + 2 <= paln ? (unsigned) pal[b1 & 0xFE] << 8 | pal[(b1 & 0xFE) + 1] : 0;
                const unsigned v0 = ((p0 << 1) | (b0 & 1)) & 0xFFFF, v1 = ((p1 << 1) | (b1 & 1)) & 0xFFFF;
                u[0] = (uint8_t) (v0 >> 8), u[1] = (uint8_t) v0, u[2] = (uint8_t) (v1 >> 8), u[3] = (uint8_t) v1;
                if (buf_add(&b, u, 4)) goto oom;
                break;
            }
            case 5: {
                const unsigned ix = (tok >> 4) << 1;
                const uint32_t pv = ix + 2 <= paln ? (uint32_t) pal[ix] << 8 | pal[ix + 1] : 0;
                const uint32_t v = ((pv & 0x7C00u) << 17) | ((pv & 0x03E0u) << 14) | ((pv & 0x1Fu) << 11) | ((tok & 0xFu) << 4);
                put_be32(u, v);
                if (buf_add(&b, u, 4)) goto oom;
                break;
            }
            case 6: {
                const unsigned b0 = tok >> 8, b1 = tok & 0xFF;
                u[0] = (uint8_t) (((b0 & 0x38) << 2) | ((b0 & 7) << 1));
                u[1] = (uint8_t) (((b1 & 0x38) << 2) | ((b1 & 7) << 1));
                if (buf_add(&b, u, 2)) goto oom;
                break;
            }
        }
    }
    *out = b.p ? b.p : (uint8_t *) calloc(1, 1), *outn = b.n;
    return *out ? 0 : -1;
oom:
    free(b.p);
    return -1;
}

void tc_quantize(uint8_t *t, size_t n, int ptype) {
    size_t i;
    if (ptype == 1) {
        for (i = 0; i + 1 < n; i += 2) t[i + 1] &= 0xBF;
    } else if (ptype == 2) {
        for (i = 0; i + 3 < n; i += 4) t[i] &= 0xF0, t[i + 1] &= 0xF0, t[i + 2] &= 0xF0, t[i + 3] &= 0xE0;
    } else if (ptype == 3) {
        for (i = 0; i < n; i++) t[i] &= 0xFE;
    } else if (ptype == 6) {
        for (i = 0; i < n; i++) t[i] &= 0xEE;
    }
}

static unsigned literal(const uint8_t *u, int ptype) {
    switch (ptype) {
        case 1: {
            const unsigned v = (unsigned) u[0] << 8 | u[1];
            return ((v & 0xFF80) >> 1) | (v & 0x3F);
        }
        case 2: return (unsigned) ((u[0] >> 4) << 11 | (u[1] >> 4) << 7 | (u[2] >> 4) << 3 | (u[3] >> 5));
        case 3: return (unsigned) ((u[0] >> 1) << 8 | (u[1] >> 1));
        default: {
            const unsigned a = (unsigned) ((u[0] >> 5) << 3 | ((u[0] >> 1) & 7)), b = (unsigned) ((u[1] >> 5) << 3 | ((u[1] >> 1) & 7));
            return a << 8 | b;
        }
    }
}

static int pack_impl(const uint8_t *t, size_t n, int ptype, const uint16_t *toks, uint8_t **out, size_t *outn,
                     char *err, size_t errlen);

int tc_pack(const uint8_t *t, size_t n, int ptype, uint8_t **out, size_t *outn, char *err, size_t errlen) {
    if (ptype == 4 || ptype == 5) {
        *out = NULL;
        seterr(err, errlen, "packing %d needs the slot's palette (tc_palette_texels)", ptype);
        return -1;
    }
    return pack_impl(t, n, ptype, NULL, out, outn, err, errlen);
}

int tc_pack_tokens(const uint8_t *t, size_t n, int ptype, const uint16_t *toks, uint8_t **out, size_t *outn, char *err,
                   size_t errlen) {
    return pack_impl(t, n, ptype, toks, out, outn, err, errlen);
}

static int pack_impl(const uint8_t *t, size_t n, int ptype, const uint16_t *toks, uint8_t **out, size_t *outn,
                     char *err, size_t errlen) {
    Buf o = {0};
    const int us = unit_of(ptype);
    size_t nu, i, k;
    long *prevp = NULL;
    uint32_t *key = NULL;
    /* the units' values -> the last unit index with that value (a hash of chains) */
    size_t hcap = 1;
    long *head = NULL;
    *out = NULL;
    if (ptype == 0) {
        const size_t m = (n + 7) & ~(size_t) 7;
        uint8_t *p = (uint8_t *) calloc(m + 1, 1);
        if (p == NULL) return -1;
        memcpy(p, t, n);
        *out = p, *outn = m;
        return 0;
    }
    if (ptype < 1 || ptype > 6 || ((ptype == 4 || ptype == 5) && toks == NULL)) {
        seterr(err, errlen, "packing %d is not encoded here", ptype);
        return -1;
    }
    if (n % (size_t) us) {
        seterr(err, errlen, "%u bytes of texels is not whole %d-byte units", (unsigned) n, us);
        return -1;
    }
    nu = n / (size_t) us;
    while (hcap < nu * 2 + 16) hcap *= 2;
    head = (long *) malloc(sizeof(long) * hcap);
    prevp = (long *) malloc(sizeof(long) * (nu + 1));
    key = (uint32_t *) malloc(sizeof(uint32_t) * (nu + 1));
    if (head == NULL || prevp == NULL || key == NULL) goto oom;
    for (i = 0; i < hcap; i++) head[i] = -1;
    for (i = 0; i < nu; i++) {
        uint32_t v = 0;
        for (k = 0; k < (size_t) us; k++) v = v << 8 | t[i * (size_t) us + k];
        key[i] = v;
    }
#define SLOT(v) ((size_t) (((v) * 2654435761u) >> 7) & (hcap - 1))
    /* (a chain per hash slot; units of other values in a slot are skipped) */
    i = 0;
    while (i < nu) {
        size_t bestLen = 0, bestD = 0;
        long j;
        for (j = head[SLOT(key[i])]; j >= 0; j = prevp[j]) {
            const size_t d = i - (size_t) j;
            if (key[j] != key[i]) continue;
            if (d > 511) break;
            k = 0;
            while (k < 31 && i + k < nu && key[(size_t) j + k] == key[i + k]) k++;
            if (k > bestLen) {
                bestLen = k, bestD = d;
                if (k == 31) break;
            }
        }
        if (bestLen >= 2) {
            const unsigned db = (unsigned) (bestD * (size_t) us);
            const unsigned tok = 0x8000u | (us == 2 ? db << 5 : db << 4) | (unsigned) bestLen;
            uint8_t q[2];
            size_t m;
            q[0] = (uint8_t) (tok >> 8), q[1] = (uint8_t) tok;
            if (buf_add(&o, q, 2)) goto oom;
            for (m = i; m < i + bestLen; m++) {
                const size_t s = SLOT(key[m]);
                prevp[m] = head[s], head[s] = (long) m;
            }
            i += bestLen;
        } else {
            const unsigned tok = toks ? toks[i] : literal(t + i * (size_t) us, ptype);
            uint8_t q[2];
            const size_t s = SLOT(key[i]);
            q[0] = (uint8_t) (tok >> 8), q[1] = (uint8_t) tok;
            if (buf_add(&o, q, 2)) goto oom;
            prevp[i] = head[s], head[s] = (long) i;
            i++;
        }
    }
#undef SLOT
    free(head), free(prevp), free(key);
    *out = o.p ? o.p : (uint8_t *) calloc(1, 1), *outn = o.n;
    return *out ? 0 : -1;
oom:
    free(head), free(prevp), free(key), free(o.p);
    seterr(err, errlen, "out of memory");
    return -1;
}

/* ------------------------------------------------------------------ texels */
static const char *const kFmt[5] = {"RGBA", "YUV", "CI", "IA", "I"};
const char *tc_fmt_name(int fmt) {
    return fmt >= 0 && fmt < 5 ? kFmt[fmt] : "?";
}

static unsigned lum(const uint8_t *c) {
    return ((unsigned) c[0] * 299 + (unsigned) c[1] * 587 + (unsigned) c[2] * 114 + 500) / 1000;
}

int tc_from_rgba8(const uint8_t *rgba, uint32_t w, uint32_t h, int fmt, int siz, uint8_t **out, size_t *outn,
                  char *err, size_t errlen) {
    const size_t n = (size_t) w * h;
    size_t i, m;
    uint8_t *o;
    *out = NULL;
    if (fmt == 0 && siz == 16) m = 2 * n;
    else if (fmt == 0 && siz == 32) m = 4 * n;
    else if (fmt == 3 && siz == 16) m = 2 * n;
    else if ((fmt == 3 || fmt == 4) && siz == 8) m = n;
    else if ((fmt == 3 || fmt == 4) && siz == 4) m = (n + 1) / 2;
    else {
        seterr(err, errlen, "texel format %s %d-bit is not encoded here", tc_fmt_name(fmt), siz);
        return -1;
    }
    o = (uint8_t *) malloc(m + 1);
    if (o == NULL) return -1;
    for (i = 0; i < n; i++) {
        const uint8_t *c = rgba + 4 * i;
        if (fmt == 0 && siz == 16) {
            const unsigned v = (unsigned) (c[0] >> 3) << 11 | (unsigned) (c[1] >> 3) << 6 | (unsigned) (c[2] >> 3) << 1 |
                               (c[3] >= 128 ? 1u : 0u);
            o[2 * i] = (uint8_t) (v >> 8), o[2 * i + 1] = (uint8_t) v;
        } else if (fmt == 0) {
            memcpy(o + 4 * i, c, 4);
        } else if (fmt == 3 && siz == 16) {
            o[2 * i] = (uint8_t) lum(c), o[2 * i + 1] = c[3];
        } else if (fmt == 3 && siz == 8) {
            o[i] = (uint8_t) ((lum(c) >> 4) << 4 | (unsigned) (c[3] >> 4));
        } else if (fmt == 4 && siz == 8) {
            o[i] = (uint8_t) lum(c);
        } else {
            const unsigned nib = fmt == 3 ? ((lum(c) >> 5) << 1) | (c[3] >= 128 ? 1u : 0u) : lum(c) >> 4;
            if (i % 2 == 0) o[i / 2] = (uint8_t) (nib << 4);
            else o[i / 2] |= (uint8_t) nib;
        }
    }
    *out = o, *outn = m;
    return 0;
}

void tc_to_rgba8(const uint8_t *t, size_t n, int fmt, int siz, uint32_t w, uint32_t h, const uint8_t *tlut,
                 size_t tlutn, uint8_t *out) {
    const size_t np = (size_t) w * h;
    size_t i;
    for (i = 0; i < np; i++) {
        uint32_t v;
        uint8_t c[4];
        if (siz == 4) v = (i >> 1) < n ? (i & 1 ? t[i >> 1] & 0xF : t[i >> 1] >> 4) : 0;
        else if (siz == 8) v = i < n ? t[i] : 0;
        else if (siz == 16) v = 2 * i + 1 < n ? (uint32_t) t[2 * i] << 8 | t[2 * i + 1] : 0;
        else v = 4 * i + 3 < n ? get_be32(t + 4 * i) : 0;
        if (fmt == 0 && siz == 16) {
            const unsigned r = (v >> 11) & 31, g = (v >> 6) & 31, b = (v >> 1) & 31;
            c[0] = (uint8_t) (r << 3 | r >> 2), c[1] = (uint8_t) (g << 3 | g >> 2), c[2] = (uint8_t) (b << 3 | b >> 2);
            c[3] = v & 1 ? 255 : 0;
        } else if (fmt == 0 && siz == 32) {
            c[0] = (uint8_t) (v >> 24), c[1] = (uint8_t) (v >> 16), c[2] = (uint8_t) (v >> 8), c[3] = (uint8_t) v;
        } else if (fmt == 3 && siz == 4) {
            const uint8_t iv = (uint8_t) ((v >> 1) * 255 / 7);
            c[0] = c[1] = c[2] = iv, c[3] = v & 1 ? 255 : 0;
        } else if (fmt == 3 && siz == 8) {
            c[0] = c[1] = c[2] = (uint8_t) ((v >> 4) * 17), c[3] = (uint8_t) ((v & 15) * 17);
        } else if (fmt == 3 && siz == 16) {
            c[0] = c[1] = c[2] = (uint8_t) (v >> 8), c[3] = (uint8_t) v;
        } else if (fmt == 4 && siz == 4) {
            c[0] = c[1] = c[2] = c[3] = (uint8_t) (v * 17);
        } else if (fmt == 4 && siz == 8) {
            c[0] = c[1] = c[2] = c[3] = (uint8_t) v;
        } else if (fmt == 2 && tlut != NULL) {
            const unsigned e = 2 * v + 2 <= tlutn ? (unsigned) tlut[2 * v] << 8 | tlut[2 * v + 1] : 0;
            const unsigned r = (e >> 11) & 31, g = (e >> 6) & 31, b = (e >> 1) & 31;
            c[0] = (uint8_t) (r << 3 | r >> 2), c[1] = (uint8_t) (g << 3 | g >> 2), c[2] = (uint8_t) (b << 3 | b >> 2);
            c[3] = e & 1 ? 255 : 0;
        } else {
            c[0] = c[1] = c[2] = (uint8_t) v, c[3] = 255;
        }
        memcpy(out + 4 * i, c, 4);
    }
}

/* ------------------------------------------------------------------ levels of detail (texture.py mip_*) */
size_t tc_mip_layout(uint32_t w, uint32_t h, int siz, int levels, TcMipLevel *out) {
    size_t off = 0;
    int k;
    for (k = 0; k < levels; k++) {
        const uint32_t lw = w >> k ? w >> k : 1, lh = h >> k ? h >> k : 1;
        const uint32_t line = (lw * (uint32_t) siz + 63) / 64 * 8;
        if (out) out[k].offset = (uint32_t) off, out[k].line = line, out[k].w = lw, out[k].h = lh;
        off += (size_t) line * lh;
    }
    return off;
}

int tc_mip_down(const uint8_t *rgba, uint32_t w, uint32_t h, uint8_t **out, uint32_t *ow, uint32_t *oh) {
    const uint32_t nw = w >> 1 ? w >> 1 : 1, nh = h >> 1 ? h >> 1 : 1;
    uint8_t *o = (uint8_t *) malloc((size_t) nw * nh * 4 + 1);
    uint32_t x, y;
    if (o == NULL) return -1;
    for (y = 0; y < nh; y++)
        for (x = 0; x < nw; x++) {
            const uint32_t y1 = 2 * y + 1 < h ? 2 * y + 1 : 2 * y, x1 = 2 * x + 1 < w ? 2 * x + 1 : 2 * x;
            const unsigned n = (y1 - 2 * y + 1) * (x1 - 2 * x + 1);
            int c;
            for (c = 0; c < 4; c++) {
                unsigned s = 0;
                uint32_t yy, xx;
                for (yy = 2 * y; yy <= y1; yy++)
                    for (xx = 2 * x; xx <= x1; xx++) s += rgba[4 * ((size_t) yy * w + xx) + (size_t) c];
                o[4 * ((size_t) y * nw + x) + (size_t) c] = (uint8_t) ((s + n / 2) / n);
            }
        }
    *out = o, *ow = nw, *oh = nh;
    return 0;
}

int tc_mip_texels(const uint8_t *rgba, uint32_t w, uint32_t h, int fmt, int siz, int levels, const uint8_t *orig,
                  size_t orign, uint8_t **out, size_t *outn, char *err, size_t errlen) {
    TcMipLevel lay[12];
    size_t total, i;
    uint8_t *buf, *img = NULL;
    uint32_t cw = w, ch = h;
    int k;
    *out = NULL;
    if (siz == 32) {
        seterr(err, errlen, "mipmaps of 32-bit texels are not done (no level list draws one)");
        return -1;
    }
    if (levels < 1 || levels > 12) {
        seterr(err, errlen, "%d levels of detail: 1..12 expected", levels);
        return -1;
    }
    total = tc_mip_layout(w, h, siz, levels, lay);
    buf = (uint8_t *) calloc(total + 1, 1);
    if (buf == NULL) return -1;
    if (orig != NULL) memcpy(buf, orig, orign < total ? orign : total);
    for (k = 0; k < levels; k++) {
        uint32_t y;
        if (k) {
            uint8_t *nimg = NULL;
            if (tc_mip_down(img ? img : rgba, cw, ch, &nimg, &cw, &ch)) goto oom;
            free(img);
            img = nimg;
        }
        for (y = 0; y < ch; y++) {
            uint8_t *row = buf + lay[k].offset + (size_t) y * lay[k].line, *src = NULL;
            size_t sn = 0;
            if (tc_from_rgba8((img ? img : rgba) + (size_t) 4 * cw * y, cw, 1, fmt, siz, &src, &sn, err, errlen)) {
                free(img), free(buf);
                return -1;
            }
            memcpy(row, src, sn < lay[k].line ? sn : lay[k].line);
            free(src);
            if (y & 1)
                for (i = 0; i + 8 <= lay[k].line; i += 8) {
                    uint8_t t[4];
                    memcpy(t, row + i, 4), memcpy(row + i, row + i + 4, 4), memcpy(row + i + 4, t, 4);
                }
        }
    }
    free(img);
    *out = buf, *outn = total;
    return 0;
oom:
    free(img), free(buf);
    return -1;
}

int tc_encode_slot(const uint8_t *rgba, uint32_t w, uint32_t h, int fmt, int siz, int ptype, const uint8_t *orig,
                   size_t orign, uint8_t **packed, size_t *packedn, char *err, size_t errlen) {
    return tc_encode_slot_mips(rgba, w, h, fmt, siz, ptype, orig, orign, 0, packed, packedn, err, errlen);
}

int tc_encode_slot_mips(const uint8_t *rgba, uint32_t w, uint32_t h, int fmt, int siz, int ptype, const uint8_t *orig,
                        size_t orign, int mips, uint8_t **packed, size_t *packedn, char *err, size_t errlen) {
    uint8_t *tex = NULL, *pk = NULL, *back = NULL;
    size_t tn = 0, pn = 0, bn = 0;
    *packed = NULL;
    if (mips > 1) {
        if (tc_mip_texels(rgba, w, h, fmt, siz, mips, orig, orign, &tex, &tn, err, errlen)) return -1;
    } else if (tc_from_rgba8(rgba, w, h, fmt, siz, &tex, &tn, err, errlen)) {
        return -1;
    }
    if (orig != NULL) {
        uint8_t *q;
        if (tn > orign) {
            if (mips > 1)
                seterr(err, errlen, "%ux%u %s%d with %d levels of detail is %u bytes of texels, the slot holds %u: use "
                       "the size its lists use", (unsigned) w, (unsigned) h, tc_fmt_name(fmt), siz, mips, (unsigned) tn,
                       (unsigned) orign);
            else
                seterr(err, errlen, "%ux%u %s%d is %u bytes of texels, the slot holds %u: use the size its lists use",
                       (unsigned) w, (unsigned) h, tc_fmt_name(fmt), siz, (unsigned) tn, (unsigned) orign);
            free(tex);
            return -1;
        }
        q = (uint8_t *) realloc(tex, orign + 1);
        if (q == NULL) {
            free(tex);
            return -1;
        }
        tex = q;
        memcpy(tex + tn, orig + tn, orign - tn);
        tn = orign;
    }
    tc_quantize(tex, tn, ptype);
    if (tc_pack(tex, tn, ptype, &pk, &pn, err, errlen)) {
        free(tex);
        return -1;
    }
    if (tc_decode(pk, pn, ptype, NULL, 0, &back, &bn) || bn < tn || memcmp(back, tex, tn) != 0) {
        seterr(err, errlen, "internal: the packed image doesn't decode back");
        free(tex), free(pk), free(back);
        return -1;
    }
    free(tex), free(back);
    if (pn > 0xFFFF) {
        seterr(err, errlen, "%u packed bytes: the table's size field holds at most 65535", (unsigned) pn);
        free(pk);
        return -1;
    }
    *packed = pk, *packedn = pn;
    return 0;
}

/* ------------------------------------------------------------------ the palette packings (texture.py palette_texels) */
int tc_palette_texels(const uint8_t *rgba, uint32_t w, uint32_t h, int ptype, const uint8_t *pal, size_t paln,
                      uint8_t **texels, size_t *tn, uint16_t **toks, size_t *nt, char *err, size_t errlen) {
    const size_t n = (size_t) w * h;
    size_t count, i;
    unsigned r[128], g[128], b[128], pv[128];
    uint8_t *o = NULL;
    uint16_t *tk = NULL;
    /* the nearest entry of every colour seen (a small cache over 24-bit colours) */
    uint32_t cacheKey[256];
    int cacheVal[256];
    *texels = NULL, *toks = NULL;
    if (ptype != 4 && ptype != 5) {
        seterr(err, errlen, "packing %d has no palette", ptype);
        return -1;
    }
    if (ptype == 4 && n % 2) {
        seterr(err, errlen, "packing 4 holds texels in pairs: %ux%u is an odd number", (unsigned) w, (unsigned) h);
        return -1;
    }
    count = paln / 2;
    if (count > (ptype == 4 ? 64u : 128u)) count = ptype == 4 ? 64u : 128u;
    if (count < 1) {
        seterr(err, errlen, "the palette is empty");
        return -1;
    }
    for (i = 0; i < count; i++) {
        const unsigned p = (unsigned) pal[2 * i] << 8 | pal[2 * i + 1];
        pv[i] = p, r[i] = ((p >> 10) & 31) << 3, g[i] = ((p >> 5) & 31) << 3, b[i] = (p & 31) << 3;
    }
    for (i = 0; i < 256; i++) cacheKey[i] = 0xFFFFFFFFu, cacheVal[i] = 0;
    o = (uint8_t *) malloc(n * (ptype == 4 ? 2 : 4) + 1);
    tk = (uint16_t *) malloc(sizeof(uint16_t) * (n + 1));
    if (o == NULL || tk == NULL) {
        free(o), free(tk);
        return -1;
    }
#define NEAREST(c, out)                                                                                          \
    do {                                                                                                         \
        const uint32_t k_ = (uint32_t) (c)[0] << 16 | (uint32_t) (c)[1] << 8 | (c)[2];                         \
        const unsigned s_ = (k_ * 2654435761u) >> 24;                                                            \
        if (cacheKey[s_] == k_) {                                                                                \
            (out) = cacheVal[s_];                                                                                \
        } else {                                                                                                 \
            size_t q_;                                                                                           \
            long bd_ = -1;                                                                                       \
            int bi_ = 0;                                                                                         \
            for (q_ = 0; q_ < count; q_++) {                                                                     \
                const long dr = (long) r[q_] - (c)[0], dg = (long) g[q_] - (c)[1], db = (long) b[q_] - (c)[2];   \
                const long d_ = dr * dr + dg * dg + db * db;                                                     \
                if (bd_ < 0 || d_ < bd_) bd_ = d_, bi_ = (int) q_;                                               \
            }                                                                                                    \
            cacheKey[s_] = k_, cacheVal[s_] = bi_, (out) = bi_;                                                  \
        }                                                                                                        \
    } while (0)
    if (ptype == 4) {
        for (i = 0; i < n; i += 2) {
            unsigned bs[2];
            int k;
            for (k = 0; k < 2; k++) {
                const uint8_t *c = rgba + 4 * (i + (size_t) k);
                int ix;
                unsigned a, v;
                NEAREST(c, ix);
                a = c[3] >= 128 ? 1u : 0u;
                bs[k] = (unsigned) ix << 1 | a;
                v = ((pv[ix] << 1) | a) & 0xFFFF;
                o[2 * (i + (size_t) k)] = (uint8_t) (v >> 8), o[2 * (i + (size_t) k) + 1] = (uint8_t) v;
            }
            tk[i / 2] = (uint16_t) (bs[0] << 8 | bs[1]);
        }
        *tn = 2 * n, *nt = n / 2;
    } else {
        for (i = 0; i < n; i++) {
            const uint8_t *c = rgba + 4 * i;
            int ix;
            unsigned a;
            uint32_t v;
            NEAREST(c, ix);
            a = (unsigned) c[3] >> 4;
            v = ((pv[ix] & 0x7C00u) << 17) | ((pv[ix] & 0x03E0u) << 14) | ((pv[ix] & 0x1Fu) << 11) | (a << 4);
            put_be32(o + 4 * i, v);
            tk[i] = (uint16_t) ((unsigned) ix << 4 | a);
        }
        *tn = 4 * n, *nt = n;
    }
#undef NEAREST
    *texels = o, *toks = tk;
    return 0;
}

int tc_encode_slot_pal(const uint8_t *rgba, uint32_t w, uint32_t h, int fmt, int siz, int ptype, size_t orign,
                       const uint8_t *pal, size_t paln, uint8_t **packed, size_t *packedn, char *err, size_t errlen) {
    uint8_t *tex = NULL, *pk = NULL, *back = NULL, pal256[0x100];
    uint16_t *toks = NULL;
    size_t tn = 0, nt = 0, pn = 0, bn = 0;
    *packed = NULL;
    if (fmt != 0 || siz != (ptype == 4 ? 16 : 32)) {
        seterr(err, errlen, "packing %d decodes to RGBA%d texels, not %s %d-bit", ptype, ptype == 4 ? 16 : 32,
               tc_fmt_name(fmt), siz);
        return -1;
    }
    if (tc_palette_texels(rgba, w, h, ptype, pal, paln, &tex, &tn, &toks, &nt, err, errlen)) return -1;
    if (orign && orign != tn) {
        seterr(err, errlen, "%ux%u is %u bytes of texels, the slot holds %u: use its size", (unsigned) w, (unsigned) h,
               (unsigned) tn, (unsigned) orign);
        free(tex), free(toks);
        return -1;
    }
    if (tc_pack_tokens(tex, tn, ptype, toks, &pk, &pn, err, errlen)) {
        free(tex), free(toks);
        return -1;
    }
    memset(pal256, 0, sizeof pal256);
    memcpy(pal256, pal, paln < sizeof pal256 ? paln : sizeof pal256);
    if (tc_decode(pk, pn, ptype, pal256, sizeof pal256, &back, &bn) || bn < tn || memcmp(back, tex, tn) != 0) {
        seterr(err, errlen, "internal: the packed image doesn't decode back");
        free(tex), free(toks), free(pk), free(back);
        return -1;
    }
    free(tex), free(toks), free(back);
    if (pn > 0xFFFF) {
        seterr(err, errlen, "%u packed bytes: the table's size field holds at most 65535", (unsigned) pn);
        free(pk);
        return -1;
    }
    *packed = pk, *packedn = pn;
    return 0;
}

int tc_entry(const uint8_t *rom, size_t romsize, int i, TcEntry *e) {
    const uint8_t *p;
    if (i < 0 || i >= TC_ENTRIES || romsize < TC_TABLE + 8u * TC_ENTRIES) return -1;
    p = rom + TC_TABLE + 8 * (unsigned) i;
    e->offset = get_be32(p);
    e->size = (unsigned) (p[4] << 8 | p[5]);
    e->type = (unsigned) (p[6] << 8 | p[7]);
    e->dmaLen = i + 1 < TC_ENTRIES ? get_be32(p + 8) - e->offset : e->size;
    return 0;
}
