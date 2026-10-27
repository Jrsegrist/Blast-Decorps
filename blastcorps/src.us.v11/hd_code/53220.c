#include "common.h"
#include <ultra64.h>
#ifndef NON_MATCHING /* legacy declarations */
/* Matching build: IDO compiled the matched code here against older
 * declarations of these, which the file keeps; the NON_MATCHING build
 * uses game/game.h's. */
#define LEGACY_gYoshiHeapItems
#endif /* legacy declarations */
#include "game/game.h"
/* Views: this file reads these shared variables (game/variables.h) as other types. */
#define gYoshiSprites ((Font *) gYoshiSprites)
#define players ((PlayerInfo *) players)
#ifdef NON_MATCHING
#define gYoshiHeapItems (*(YoshiItem * N64P *) &gYoshiHeapItems)
#endif
/* end of views */

/*
 * Two parts: the academy marker-found yoshi window builder (window 0x15) (academy_build_window
 * and helpers), then a second copy of the INFLATE decompressor that init/
 * carries (init/0050.c, init/0E30.c), compiled from the same source:
 *   hd_huft_build huft_build, hd_inflate_codes inflate_codes,
 *   hd_inflate_stored inflate_stored, hd_inflate_fixed inflate_fixed,
 *   hd_inflate_dynamic inflate_dynamic, hd_inflate_block inflate_block,
 *   hd_inflate inflate.
 */

/* (players[]: PlayerInfo, game/types.h) */

/* (The window, YW_ACADEMY_MARKER, and its items built on the heap: YoshiWindow,
 * YoshiItem, game/types.h.)  An alias of YoshiWindow with an s16 sel: with the u16
 * of game/types.h, the NON_MATCHING build's academy_build_window comes out 4 bytes
 * shorter (the matching build is the same either way). */
typedef struct {
    /* 0x00 */ u8 pad0[0x10];
    /* 0x10 */ u16 itemCount;
    /* 0x12 */ u8 pad12[6];
    /* 0x18 */ s16 sel;
    /* 0x1A */ u8 pad1A[2];
} YoshiWindowS16;

typedef struct {
    u8 * N64P text;
    u16 * N64P jtext;
} MenuText;

/* 0x30 bytes */
typedef struct {
    /* 0x00 */ u8 unk0[4];
    /* 0x04 */ u8 unk4;
    /* 0x05 */ u8 unk5;
    /* 0x06 */ u8 ids[0x26];
    /* 0x2C */ u8 unk2C;
    /* 0x2D */ u8 unk2D;
    /* 0x2E */ u8 unk2E[2];
} Font;

extern u8 gAcademyPage;
extern u8 gAcademyMarkerLevels[];
extern MenuText gAcademyPageText[][20];
extern u8 gAcademyPageSelectable[];
#ifndef NON_MATCHING
extern YoshiItem *gYoshiHeapItems;
#endif



u8 academy_count_left(void);

/* Builds the yoshi window shown when the academy marker is touched (window 0x15): the page hinting at the
 * next unfound marker, or the all-found page, with the count of markers left (0x802979E0).
 * Build the yoshi window that lists a group's levels, numbering the
 * "N left" text with how many levels remain */
void academy_build_window(u8 level) {
    YoshiWindowS16 *win = (YoshiWindowS16 *) (gYoshiWindows + 0x24C); /* YW_ACADEMY_MARKER (0x15 * 0x1C) */
    YoshiItem *item;
    Font *font;
    u8 left;
    u8 done;
    s32 i;
    s32 j;
    s32 nsel;
    s32 n;
    u8 stop;
    u8 *text;
    u16 *jtext;

    left = academy_count_left();
    done = 0;
    if (left == 1 && !academy_marker_found(level)) {
        left--;
    }
    if (left == 0) {
        gAcademyPage = 6;
    } else {
        stop = 0;
        for (i = gAcademyMarkerIndex; i < gAcademyMarkerIndex + 6 && !stop;) {
            if (!academy_marker_found(gAcademyMarkerLevels[i % 6]) && gAcademyMarkerLevels[i % 6] != level) {
                stop = 1;
            } else {
                i++;
            }
        }
        gAcademyPage = i % 6;
    }
    i = 0;
    gYoshiHeapItems = (YoshiItem *) gHeapPtr;
    gHeapPtr = (u8 *) gHeapPtr + 0x24C;
    nsel = 0;
    n = 0;
    stop = 0;
    for (; i < 20 && !stop; i++) {
        text = gAcademyPageText[gAcademyPage][i].text;
        jtext = gAcademyPageText[gAcademyPage][i].jtext;
        item = &gYoshiHeapItems[i];
        if (text != NULL) {
            n++;
            item->flags = 0x1020;
            if (gAcademyPageSelectable[gAcademyPage * 5 + nsel] == i) {
                item->flags |= 1;
                nsel++;
            }
            item->y = i << 4;
            item->w = 0x10;
            item->h = 0x10;
            item->text = text;
            item->jtext = jtext;
            item->sprite = 0;
            item->sound = 0;
            item->color = 7;
            item->selColor = 7;
            item->spriteBase = 0;
        } else {
            stop = 1;
        }
    }

    item = &gYoshiHeapItems[n];
    item->text = NULL;
    item->jtext = 0;
    item->flags = 0x400;
    item->x = -0x20;
    item->y = 0x26;
    item->sprite = 0x18;
    item->sound = item->spriteBase = 0;
    font = &gYoshiSprites[item->sprite];
    item->spriteBase = spr_load((u16 *) font->ids, 0, font->unk4, font->unk2C, font->unk2D | 4, 1.0f);
    win->itemCount = n + 1;
    win->sel = gAcademyPageSelectable[gAcademyPage * 5];
    if (!academy_marker_found(level)) {
        left--;
    }
    for (i = 0; i < win->itemCount && !done; i++) {
        item = &gYoshiHeapItems[i];
        for (j = 0; j < font_strlen(item->text) && !done; j++) {
            if (item->text[j] >= '0' && item->text[j] < '6') {
                item->text[j] = left + '0';
                done = 1;
            }
        }
    }
}

/* Puts window 0x15's cursor on the current academy page's first selectable item (0x80297ECC). */
void academy_reset_window_cursor(void) {
    *(s16 *) (gYoshiWindows + 0x264) = gAcademyPageSelectable[gAcademyPage * 5]; /* YW_ACADEMY_MARKER's sel */
}

/* Whether the level's academy marker is saved as found (a players[playerNumber].academy bit) (0x80297EF8).
 * Is level completed in its academy group? */
u8 academy_marker_found(u8 level) {
    u8 idx;
    u8 found;

    found = academy_find_level(level, &idx);
    return (found && (players[playerNumber].academy & (1 << idx))) ? 1 : 0;
}

/* The number of academy markers not yet found (of 6), from the saved bits (0x80297F74).
 * Number of academy groups not yet completed */
u8 academy_count_left(void) {
    s32 i;
    s32 n = 6;

    for (i = 0; i < 6; i++) {
        if (players[playerNumber].academy & (1 << i)) {
            n--;
        }
    }
    return n;
}

typedef struct Huft {
    u8 e;
    u8 b;
    u8 pad[2];
    union {
        u16 n;
        struct Huft * N64P t;
    } v;
} Huft;

extern u32 hd_bb;  /* bb */
extern u32 hd_bk;  /* bk */
extern u32 hd_hufts;  /* hufts */

extern u8 hd_border[];  /* border */
extern u16 hd_cplens[]; /* cplens */
extern u8 hd_cplext[];  /* cplext */
extern u16 hd_cpdist[]; /* cpdist */
extern u8 hd_cpdext[];  /* cpdext */
extern u16 hd_mask_bits[]; /* mask_bits */
extern s32 hd_lbits;   /* lbits */
extern s32 hd_dbits;   /* dbits */

int hd_huft_build(s32 *b, u32 n, u32 s, u16 *d, u8 *e, Huft * N64P *t, s32 *m);
s32 hd_inflate_codes(Huft *tl, Huft *td, u32 bl, u32 bd);
s32 hd_inflate_dynamic(void);
s32 hd_inflate_stored(void);
s32 hd_inflate_fixed(void);

/* huft_build: build a Huffman decode table from code lengths b[0..n-1]
 * (classic inflate.c, bump-allocated from the gHdInflateHuftArena arena). It returns
 * `int`, not s32 (long): with s32, IDO puts the `&&` result in $s6 and
 * adds a move to $v0. */
#define BMAX 16
#define N_MAX 288

/* huft_build from gzip's inflate.c: a Huffman decode table from code lengths, bump-allocated from the huft
 * arena; nonzero for an incomplete code (0x80297FE0). */
int hd_huft_build(s32 *b, u32 n, u32 s, u16 *d, u8 *e, Huft * N64P *t, s32 *m) {
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

#ifdef PORT_HOST
    /* (the port: every table entry is a copy of r, whose padding (and the other half of v when only v.n is set)
     * would otherwise be the host stack's leftovers, which differ from run to run; on the N64 they are the MIPS
     * stack's.  Nothing reads them.) */
    bzero(&r, sizeof(r));
#endif
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

                q = (Huft *) (hd_hufts * 8 + gHdInflateHuftArena);
                hd_hufts += z + 1;
                *t = q + 1;
                *(t = (Huft * N64P *) &(q->v.t)) = NULL;
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

/* inflate_codes from gzip's inflate.c: decodes literal/length and distance codes into the output until the
 * end of the block (0x8029867C). */
s32 hd_inflate_codes(Huft *tl, Huft *td, u32 bl, u32 bd) {
    register u32 e;
    u32 n;
    u32 d;
    u32 w;
    Huft *t;
    u32 ml;
    u32 md;
    register u32 k;
    register u32 b;

    b = hd_bb;
    k = hd_bk;
    w = hd_outcnt;

    ml = hd_mask_bits[bl];
    md = hd_mask_bits[bd];

    for (;;) {
        while (k < bl) {
            b |= (u32) hd_inbuf[hd_inptr++] << k;
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
                    b |= (u32) hd_inbuf[hd_inptr++] << k;
                    k += 8;
                }
                t = t->v.t + (b & hd_mask_bits[e]);
                e = t->e;
            } while (e > 16);
        }
        b >>= t->b;
        k -= t->b;
        if (e == 16) {
            hd_slide[w++] = (u8) t->v.n;
            continue;
        }
        if (e == 15) {
            break;
        }

        while (k < e) {
            b |= (u32) hd_inbuf[hd_inptr++] << k;
            k += 8;
        }
        n = t->v.n + (b & hd_mask_bits[e]);
        k -= e;
        b >>= e;

        while (k < bd) {
            b |= (u32) hd_inbuf[hd_inptr++] << k;
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
                    b |= (u32) hd_inbuf[hd_inptr++] << k;
                    k += 8;
                }
                t = t->v.t + (b & hd_mask_bits[e]);
                e = t->e;
            } while (e > 16);
        }
        b >>= t->b;
        k -= t->b;

        while (k < e) {
            b |= (u32) hd_inbuf[hd_inptr++] << k;
            k += 8;
        }
        d = (w - t->v.n) - (b & hd_mask_bits[e]);
        b >>= e;
        k -= e;

        do {
            n -= (e = n);
            do {
                hd_slide[w++] = hd_slide[d++];
            } while (--e);
        } while (n);
    }

    hd_outcnt = w;
    hd_bb = b;
    hd_bk = k;
    return 0;
}

/* inflate_stored from gzip's inflate.c: copies a stored block (the length's complement is not checked)
 * (0x80298A84). */
s32 hd_inflate_stored(void) {
    u32 n;
    u32 w;
    register u32 k;
    register u32 b;

    k = hd_bk;
    b = hd_bb;
    w = hd_outcnt;

    n = k & 7;
    b >>= n;
    k -= n;

    while (k < 16) {
        b |= (u32) hd_inbuf[hd_inptr++] << k;
        k += 8;
    }
    n = b & 0xffff;
    b >>= 16;
    k -= 16;

    while (k < 16) {
        b |= (u32) hd_inbuf[hd_inptr++] << k;
        k += 8;
    }
    b >>= 16;
    k -= 16;

    while (n--) {
        while (k < 8) {
            b |= (u32) hd_inbuf[hd_inptr++] << k;
            k += 8;
        }
        hd_slide[w++] = (u8) b;
        b >>= 8;
        k -= 8;
    }

    hd_outcnt = w;
    hd_bb = b;
    hd_bk = k;
    return 0;
}

/* inflate_fixed from gzip's inflate.c: builds the fixed literal/length and distance tables and decodes the
 * block (0x80298C18). */
s32 hd_inflate_fixed(void) {
    s32 i;
    Huft * N64P tl;
    Huft * N64P td;
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
    hd_huft_build(l, 288, 257, hd_cplens, hd_cplext, &tl, &bl);

    for (i = 0; i < 30; i++) {
        l[i] = 5;
    }
    bd = 5;
    hd_huft_build(l, 30, 0, hd_cpdist, hd_cpdext, &td, &bd);

    hd_inflate_codes(tl, td, bl, bd);
    return 0;
}

#define LL_SIZE (286 + 30)

/* inflate_dynamic from gzip's inflate.c: reads the dynamic code lengths, builds the tables and decodes the
 * block (0x80298DC0). */
s32 hd_inflate_dynamic(void) {
    s32 i;
    u32 j;
    u32 l;
    u32 m;
    u32 n;
    Huft * N64P tl;
    Huft * N64P td;
    s32 bl;
    s32 bd;
    u32 nb;
    u32 nl;
    u32 nd;
    register u32 k;
    register u32 b;
    s32 ll[LL_SIZE];

    k = hd_bk;
    b = hd_bb;

    while (k < 5) {
        b |= (u32) hd_inbuf[hd_inptr++] << k;
        k += 8;
    }
    nl = 257 + (b & 0x1F);
    b >>= 5;
    k -= 5;

    while (k < 5) {
        b |= (u32) hd_inbuf[hd_inptr++] << k;
        k += 8;
    }
    nd = 1 + (b & 0x1F);
    b >>= 5;
    k -= 5;

    while (k < 4) {
        b |= (u32) hd_inbuf[hd_inptr++] << k;
        k += 8;
    }
    nb = 4 + (b & 0xF);
    b >>= 4;
    k -= 4;

    for (j = 0; j < nb; j++) {
        while (k < 3) {
            b |= (u32) hd_inbuf[hd_inptr++] << k;
            k += 8;
        }
        ll[hd_border[j]] = b & 7;
        b >>= 3;
        k -= 3;
    }
    for (; j < 19; j++) {
        ll[hd_border[j]] = 0;
    }

    bl = 7;
    hd_huft_build(ll, 19, 19, NULL, NULL, &tl, &bl);

    n = nl + nd;
    m = hd_mask_bits[bl];
    i = l = 0;
    while ((u32) i < n) {
        while (k < (u32) bl) {
            b |= (u32) hd_inbuf[hd_inptr++] << k;
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
                b |= (u32) hd_inbuf[hd_inptr++] << k;
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
                b |= (u32) hd_inbuf[hd_inptr++] << k;
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
                b |= (u32) hd_inbuf[hd_inptr++] << k;
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

    hd_bb = b;
    hd_bk = k;

    bl = hd_lbits;
    hd_huft_build(ll, nl, 257, hd_cplens, hd_cplext, &tl, &bl);
    bd = hd_dbits;
    hd_huft_build(ll + nl, nd, 0, hd_cpdist, hd_cpdext, &td, &bd);
    hd_inflate_codes(tl, td, bl, bd);
    return 0;
}

s32 hd_inflate_block(s32 *arg0) {
    s32 type;
    register u32 k;
    register u32 b;

    k = hd_bk;
    b = hd_bb;

    while (k == 0) {
        b |= (u32) hd_inbuf[hd_inptr++] << k;
        k += 8;
    }
    *arg0 = b & 1;
    b = b >> 1;
    k -= 1;

    while (k < 2) {
        b |= (u32) hd_inbuf[hd_inptr++] << k;
        k += 8;
    }
    type = b & 3;
    b = b >> 2;
    k -= 2;

    hd_bb = b;
    hd_bk = k;

    if (type == 2) {
        return hd_inflate_dynamic();
    }
    if (type == 0) {
        return hd_inflate_stored();
    }
    if (type == 1) {
        return hd_inflate_fixed();
    }
    return 2;
}

/* inflate from gzip's inflate.c: inflates blocks until the last one, gives back the unused lookahead bytes,
 * then skips the 8-byte gzip trailer (0x802994F8). */
s32 hd_inflate(void) {
    s32 sp24;
    s32 sp20;
    u32 sp1C;

    hd_outcnt = 0;
    hd_bk = 0;
    hd_bb = 0;
    sp1C = 0;
    for (;;) {
        hd_hufts = 0;
        sp20 = hd_inflate_block(&sp24);
        if (sp20 != 0) {
            return sp20;
        }
        if (hd_hufts > sp1C) {
            sp1C = hd_hufts;
        }
        if (sp24 != 0) {
            break;
        }
    }
    if (hd_bk >= 8) {
        do {
            hd_bk -= 8;
            hd_inptr -= 1;
        } while (hd_bk >= 8);
    }
    hd_inptr += 8;
    return 0;
}
