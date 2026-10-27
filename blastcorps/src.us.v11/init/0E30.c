#include "common.h"
#include <ultra64.h>

extern s32 inptr;
extern s32 outcnt;
extern s32 bb;
extern u32 bk;
extern u32 hufts;
extern u8 *inbuf;

typedef struct Huft {
    u8 e;
    u8 b;
    u8 pad[2];
    union {
        u16 n;
        struct Huft *t;
    } v;
} Huft;

extern u16 cplens[];
extern u8 cplext[];
extern u16 cpdist[];
extern u8 cpdext[];

extern u16 mask_bits[];
extern u8 border[];  /* border[] - RFC1951 bit-length code transmission order */
extern s32 lbits;   /* lbits - default lit/length table lookup bits */
extern s32 dbits;   /* dbits - default distance table lookup bits */

extern int huft_build(s32 *b, u32 n, u32 s, u16 *d, u8 *e, Huft **t, s32 *m);
extern s32 inflate_codes(Huft *tl, Huft *td, u32 bl, u32 bd);

/* inflate_dynamic - decompresses a type-2 (dynamic Huffman codes) block:
 * reads the bit-length code lengths (in `border`/border order), builds a
 * 7-bit table for them, decodes the literal/length and distance code lengths
 * (with the 16/17/18 repeat escapes), then builds the real tables and hands
 * off to inflate_codes. The reference's inflate_dynamic() minus every
 * defensive error return; `ll` is 286+30 (not the PKZIP_BUG_WORKAROUND
 * 288+32), the only size that gives the right frame.
 *
 * Matched by porting hd_code's copy (hd_inflate_dynamic in hd_code/53220.c).
 * What fixed the old "ll 8 bytes off" near-miss: `register u32 k, b` are
 * declared before `ll`, not after it. That moves ll from sp+0x40 to the
 * target's sp+0x48.
 */
#define LL_SIZE (286 + 30)

s32 inflate_dynamic(void) {
    s32 i;
    u32 j;
    u32 l;
    u32 m;
    u32 n;
    Huft *tl;
    Huft *td;
    s32 bl;
    s32 bd;
    u32 nb;
    u32 nl;
    u32 nd;
    register u32 k;
    register u32 b;
    s32 ll[LL_SIZE];

    k = bk;
    b = bb;

    while (k < 5) {
        b |= (u32) inbuf[inptr++] << k;
        k += 8;
    }
    nl = 257 + (b & 0x1F);
    b >>= 5;
    k -= 5;

    while (k < 5) {
        b |= (u32) inbuf[inptr++] << k;
        k += 8;
    }
    nd = 1 + (b & 0x1F);
    b >>= 5;
    k -= 5;

    while (k < 4) {
        b |= (u32) inbuf[inptr++] << k;
        k += 8;
    }
    nb = 4 + (b & 0xF);
    b >>= 4;
    k -= 4;

    for (j = 0; j < nb; j++) {
        while (k < 3) {
            b |= (u32) inbuf[inptr++] << k;
            k += 8;
        }
        ll[border[j]] = b & 7;
        b >>= 3;
        k -= 3;
    }
    for (; j < 19; j++) {
        ll[border[j]] = 0;
    }

    bl = 7;
    huft_build(ll, 19, 19, NULL, NULL, &tl, &bl);

    n = nl + nd;
    m = mask_bits[bl];
    i = l = 0;
    while ((u32) i < n) {
        while (k < (u32) bl) {
            b |= (u32) inbuf[inptr++] << k;
            k += 8;
        }
        td = tl + (b & m);
        j = td->b;
        b >>= j;
        k -= j;
        j = td->v.n;
        if (j < 16) {
            ll[i++] = l = j;
        } else if (j == 16) {
            while (k < 2) {
                b |= (u32) inbuf[inptr++] << k;
                k += 8;
            }
            j = 3 + (b & 3);
            b >>= 2;
            k -= 2;
            while (j--) {
                ll[i++] = l;
            }
        } else if (j == 17) {
            while (k < 3) {
                b |= (u32) inbuf[inptr++] << k;
                k += 8;
            }
            j = 3 + (b & 7);
            b >>= 3;
            k -= 3;
            while (j--) {
                ll[i++] = 0;
            }
            l = 0;
        } else {
            while (k < 7) {
                b |= (u32) inbuf[inptr++] << k;
                k += 8;
            }
            j = 11 + (b & 0x7F);
            b >>= 7;
            k -= 7;
            while (j--) {
                ll[i++] = 0;
            }
            l = 0;
        }
    }

    bb = b;
    bk = k;

    bl = lbits;
    huft_build(ll, nl, 257, cplens, cplext, &tl, &bl);
    bd = dbits;
    huft_build(ll + nl, nd, 0, cpdist, cpdext, &td, &bd);
    inflate_codes(tl, td, bl, bd);
    return 0;
}

extern s32 inflate_stored(void);
extern s32 inflate_fixed(void);

/*
 * inflate_block: reads a 3-bit block header (1-bit "last block" flag + 2-bit
 * block type) from the same bit-buffer/bit-count globals used throughout
 * this file's INFLATE implementation, then dispatches to the handler for
 * that block type. Confirmed an exact structural match (including the
 * t==2/t==0/t==1 dispatch order and the "return 2" bad-type fallthrough)
 * against the classic public-domain inflate.c's own inflate_block(): this is
 * byte-for-byte the same function, just compiled by IDO instead of a PC
 * compiler. inflate_dynamic and inflate_stored identified here via their
 * call sites (inflate_stored's TODO comment in init/0050.c predates this
 * identification); inflate_fixed already identified/named separately.
 */
s32 inflate_block(s32 *arg0) {
    s32 type;
    register u32 k;
    register u32 b;

    k = bk;
    b = bb;

    while (k == 0) {
        b |= (u32) inbuf[inptr++] << k;
        k += 8;
    }
    *arg0 = b & 1;
    b = b >> 1;
    k -= 1;

    while (k < 2) {
        b |= (u32) inbuf[inptr++] << k;
        k += 8;
    }
    type = b & 3;
    b = b >> 2;
    k -= 2;

    bb = b;
    bk = k;

    if (type == 2) {
        return inflate_dynamic();
    }
    if (type == 0) {
        return inflate_stored();
    }
    if (type == 1) {
        return inflate_fixed();
    }
    return 2;
}

s32 inflate(void) {
    s32 sp24;
    s32 sp20;
    u32 sp1C;

    outcnt = 0;
    bk = 0;
    bb = 0;
    sp1C = 0;
    for (;;) {
        hufts = 0;
        sp20 = inflate_block(&sp24);
        if (sp20 != 0) {
            return sp20;
        }
        if (hufts > sp1C) {
            sp1C = hufts;
        }
        if (sp24 != 0) {
            break;
        }
    }
    if (bk >= 8) {
        do {
            bk -= 8;
            inptr -= 1;
        } while (bk >= 8);
    }
    inptr += 8;
    return 0;
}
