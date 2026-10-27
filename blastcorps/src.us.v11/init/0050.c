#include "common.h"
#include <ultra64.h>

extern u32 bb;
extern u32 bk;
extern u8 *inbuf;
extern u8 *slide;
extern s32 inptr;
extern s32 outcnt;

/*
 * This whole file is Blast Corps' INFLATE (DEFLATE decompression, RFC 1951)
 * implementation, almost certainly adapted from the classic public-domain
 * inflate.c (Mark Adler, used by zlib/gzip/info-zip throughout the 1990s).
 * Confirmed definitively: inflate_fixed()'s fixed Huffman code-length table
 * initialization (144/256/280/288 boundaries with lengths 8/9/7/8, and 30
 * distance codes all length 5) matches RFC 1951 and the reference source
 * exactly, right down to the table addresses lining up with the expected
 * byte sizes (cplens: 31 entries * 2 bytes = 62, rounded to 64; cpdist: 30
 * entries * 2 bytes = 60, exact fit). huft_build's NEEDBITS/DUMPBITS-style
 * `register` bit-buffer convention (see inflate_stored in this same file,
 * and inflate_block in init/0E30.c, which matches exactly) matches the
 * reference source's own `register ulg b; register unsigned k;`
 * declarations verbatim.
 *
 * RFC 1951 fixed Huffman tables (copy lengths/dists + their extra-bit
 * counts), inside the opaque init/3A40 binary blob. Confirmed via
 * inflate_dynamic (init/0E30.c) that these need real symbol_addrs entries
 * rather than raw-address macros: a raw numeric-literal-cast-to-pointer
 * compiles address formation as `ori` (bit construction), while the real
 * target uses `addiu` (the standard %lo(symbol) relocation form) - same
 * tell as mask_bits below.
 */
extern u16 cplens[];
extern u8 cplext[];
extern u16 cpdist[];
extern u8 cpdext[];

/*
 * mask_bits: the reference source's `static ush mask_bits[] = {0x0000,
 * 0x0001,0x0003,0x0007,...}` precomputed (1<<n)-1 table, used throughout
 * inflate_codes/huft_build to mask off exactly N bits from the bit buffer.
 * Unlike cplens/cplext/cpdist/cpdext above, this one's addressed via a real
 * symbol_addrs entry rather than a raw-address macro: the target computes
 * a full base pointer (lui+addiu forming 0x80222810) before indexing, which
 * only happens for a genuine symbol reference, not a numeric-literal cast.
 */
extern u16 mask_bits[];

typedef struct Huft {
    u8 e;
    u8 b;
    u8 pad[2];
    union {
        u16 n;
        struct Huft *t;
    } v;
} Huft;

extern s32 gInflateHuftArena;
extern s32 hufts;

#define BMAX 16
#define N_MAX 288

/* huft_build - builds a Huffman decode table from a list of code lengths.
 * An exact match for the reference source's huft_build(), except that each
 * new sub-table is bump-allocated from a fixed arena (gInflateHuftArena +
 * hufts*sizeof(Huft)) instead of malloc'd, and the reference's
 * defensive error returns inside the main loop are absent.
 *
 * Matched by porting hd_code's copy (hd_huft_build in hd_code/53220.c).
 * What fixed the old near-miss: it returns `int`, not s32. s32 is `long`
 * in this ultra64.h, and with it IDO computes the `&&` into $s6 and adds a
 * `move v0,s6`. The arena address is written `hufts * 8 + gInflateHuftArena`
 * (that operand order), and `p = c + 1, xp = x + 2;` is a comma expression;
 * as separate statements the pair's instruction order was wrong.
 */
int huft_build(s32 *b, u32 n, u32 s, u16 *d, u8 *e, Huft **t, s32 *m) {
    u32 a;
    u32 c[BMAX + 1];
    u32 f;
    s32 g;
    s32 h;
    register u32 i;
    register u32 j;
    register s32 k;
    s32 l;
    register u32 *p;
    register Huft *q;
    Huft r;
    Huft *u[BMAX];
    u32 v[N_MAX];
    register s32 w;
    u32 x[BMAX + 1];
    u32 *xp;
    s32 y;
    u32 z;

    bzero(c, sizeof(c));
    p = (u32 *) b;
    i = n;
    do {
        c[*p]++;
        p++;
    } while (--i);
    if (c[0] == n) {
        *t = NULL;
        *m = 0;
        return 0;
    }

    l = *m;
    for (j = 1; j <= BMAX; j++) {
        if (c[j]) {
            break;
        }
    }
    k = j;
    if ((u32) l < j) {
        l = j;
    }
    for (i = BMAX; i; i--) {
        if (c[i]) {
            break;
        }
    }
    g = i;
    if ((u32) l > i) {
        l = i;
    }
    *m = l;

    for (y = 1 << j; j < (u32) i; j++, y <<= 1) {
        y -= c[j];
    }
    y -= c[i];
    c[i] += y;

    x[1] = j = 0;
    p = c + 1, xp = x + 2;
    while (--i) {
        *xp++ = (j += *p++);
    }

    p = (u32 *) b;
    i = 0;
    do {
        if ((j = *p++) != 0) {
            v[x[j]++] = i;
        }
    } while (++i < n);

    x[0] = i = 0;
    p = v;
    h = -1;
    w = -l;
    u[0] = NULL;
    q = NULL;
    z = 0;

    for (; k <= g; k++) {
        a = c[k];
        while (a--) {
            while (k > w + l) {
                h++;
                w += l;

                z = (z = g - w) > (u32) l ? l : z;
                if ((f = 1 << (j = k - w)) > a + 1) {
                    f -= a + 1;
                    xp = c + k;
                    while (++j < z) {
                        if ((f <<= 1) <= *++xp) {
                            break;
                        }
                        f -= *xp;
                    }
                }
                z = 1 << j;

                q = (Huft *) (hufts * 8 + gInflateHuftArena);
                hufts += z + 1;
                *t = q + 1;
                *(t = (Huft **) &(q->v.t)) = NULL;
                u[h] = ++q;

                if (h) {
                    x[h] = i;
                    r.b = (u8) l;
                    r.e = (u8) (16 + j);
                    r.v.t = q;
                    j = i >> (w - l);
                    u[h - 1][j] = r;
                }
            }

            r.b = (u8) (k - w);
            if (p >= v + n) {
                r.e = 99;
            } else if (*p < s) {
                r.e = (u8) (*p < 256 ? 16 : 15);
                r.v.n = (u16) (*p);
                p++;
            } else {
                r.e = e[*p - s];
                r.v.n = d[*p++ - s];
            }

            f = 1 << (k - w);
            for (j = i >> w; j < z; j += f) {
                q[j] = r;
            }

            for (j = 1 << (k - 1); i & j; j >>= 1) {
                i ^= j;
            }
            i ^= j;

            while ((i & ((1 << w) - 1)) != x[h]) {
                h--;
                w -= l;
            }
        }
    }

    return y != 0 && g != 1;
}

/* inflate_codes - decodes Huffman-coded literals/lengths (via `tl`) and
 * distances (via `td`) and copies literal bytes / back-references straight
 * into the output buffer (slide). The reference's inflate_codes()
 * minus its WSIZE sliding window (no flush_output, no `& (WSIZE-1)`) and
 * minus the defensive `if (e == 99) return 1;` checks.
 *
 * Matched by porting hd_code's copy (hd_inflate_codes in hd_code/53220.c).
 * What fixed the old one-missing-`move` near-miss: `register u32 k;`
 * declared before `register u32 b;`, the copy loop written as the
 * reference's `n -= (e = n); do { ... } while (--e);` (reusing `e`, no extra
 * counter), and each DUMPBITS(t->b) as `b >>= t->b; k -= t->b;`.
 */
s32 inflate_codes(Huft *tl, Huft *td, u32 bl, u32 bd) {
    register u32 e;
    u32 n;
    u32 d;
    u32 w;
    Huft *t;
    u32 ml;
    u32 md;
    register u32 k;
    register u32 b;

    b = bb;
    k = bk;
    w = outcnt;

    ml = mask_bits[bl];
    md = mask_bits[bd];

    for (;;) {
        while (k < bl) {
            b |= (u32) inbuf[inptr++] << k;
            k += 8;
        }
        t = tl + (b & ml);
        e = t->e;
        if (e > 16) {
            do {
                b >>= t->b;
                k -= t->b;
                e -= 16;
                while (k < e) {
                    b |= (u32) inbuf[inptr++] << k;
                    k += 8;
                }
                t = t->v.t + (b & mask_bits[e]);
                e = t->e;
            } while (e > 16);
        }
        b >>= t->b;
        k -= t->b;
        if (e == 16) {
            slide[w++] = (u8) t->v.n;
            continue;
        }
        if (e == 15) {
            break;
        }

        while (k < e) {
            b |= (u32) inbuf[inptr++] << k;
            k += 8;
        }
        n = t->v.n + (b & mask_bits[e]);
        k -= e;
        b >>= e;

        while (k < bd) {
            b |= (u32) inbuf[inptr++] << k;
            k += 8;
        }
        t = td + (b & md);
        e = t->e;
        if (e > 16) {
            do {
                b >>= t->b;
                k -= t->b;
                e -= 16;
                while (k < e) {
                    b |= (u32) inbuf[inptr++] << k;
                    k += 8;
                }
                t = t->v.t + (b & mask_bits[e]);
                e = t->e;
            } while (e > 16);
        }
        b >>= t->b;
        k -= t->b;

        while (k < e) {
            b |= (u32) inbuf[inptr++] << k;
            k += 8;
        }
        d = (w - t->v.n) - (b & mask_bits[e]);
        b >>= e;
        k -= e;

        do {
            n -= (e = n);
            do {
                slide[w++] = slide[d++];
            } while (--e);
        } while (n);
    }

    outcnt = w;
    bb = b;
    bk = k;
    return 0;
}

s32 inflate_stored(void) {
    u32 n;
    u32 w;
    register u32 k;
    register u32 b;

    k = bk;
    b = bb;
    w = outcnt;

    n = k & 7;
    b >>= n;
    k -= n;

    while (k < 16) {
        b |= (u32) inbuf[inptr++] << k;
        k += 8;
    }
    n = b & 0xffff;
    b >>= 16;
    k -= 16;

    while (k < 16) {
        b |= (u32) inbuf[inptr++] << k;
        k += 8;
    }
    b >>= 16;
    k -= 16;

    while (n--) {
        while (k < 8) {
            b |= (u32) inbuf[inptr++] << k;
            k += 8;
        }
        slide[w++] = (u8) b;
        b >>= 8;
        k -= 8;
    }

    outcnt = w;
    bb = b;
    bk = k;
    return 0;
}

extern int huft_build(s32 *b, u32 n, u32 s, u16 *d, u8 *e, Huft **t, s32 *m);
extern s32 inflate_codes(Huft *tl, Huft *td, u32 bl, u32 bd);

s32 inflate_fixed(void) {
    s32 i;
    Huft *tl;
    Huft *td;
    s32 bl;
    s32 bd;
    s32 l[288];

    for (i = 0; i < 144; i++) {
        l[i] = 8;
    }
    for (; i < 256; i++) {
        l[i] = 9;
    }
    for (; i < 280; i++) {
        l[i] = 7;
    }
    for (; i < 288; i++) {
        l[i] = 8;
    }
    bl = 7;
    huft_build(l, 288, 257, cplens, cplext, &tl, &bl);

    for (i = 0; i < 30; i++) {
        l[i] = 5;
    }
    bd = 5;
    huft_build(l, 30, 0, cpdist, cpdext, &td, &bd);

    inflate_codes(tl, td, bl, bd);
    return 0;
}
