#include "common.h"
#include <ultra64.h>
#include "game/game.h"

/* FILE-WIDE FINDING: this file's functions save $ra via the 64-bit `sd`/`ld`
 * doubleword form, not the normal 32-bit `sw`/`lw` pair - the same signature
 * documented in full in hd_code/77E20.c's top-of-file note and the project
 * skill file, confirming (with no exceptions found) that this file is part
 * of the same large hand-written-assembly module. Any pragma below without
 * a more specific comment follows this convention; a few have their own
 * more specific non-ABI explanation where one was already worked out. */

/* .data at 0x80305C10 (ROM 0xC1450-0xC1490): the 0-terminated list of effect
 * definitions (in the 7D9D0 tables inside .text) that get a heap texture
 * block (fx_get_texture), then fx_alloc_texture_caches's two debug strings. */
u8 * N64P gFxCachedDefs[] = {
    N64_DPTR(gFxExhaustSmoke), N64_DPTR(gFxSparks), N64_DPTR(gFxDefNodeHit), N64_DPTR(gFxSkyfallBoost),
    N64_DPTR(gFxDefJetFlame), N64_DPTR(gFxDefBlast), N64_DPTR(gFxDefKind20), N64_DPTR(gFxDefKind21), NULL,
};
char gFxTexCacheCountMsg[] = "Num texture caches=";
char gFxTexCacheCountFmt[] = "%d\n";

#ifdef NON_MATCHING
/* Shared declarations for the NON_MATCHING (port) rewrites below. */

/* One of 16 records at gEffects (stride 0x3C). */
typedef struct {
    /* 0x00 */ u8 * N64P def;     /* points at a definition whose u16 at +0xE is a count */
    /* 0x04 */ u8 pad4[0x2E];
    /* 0x32 */ u8 last;     /* def's count - 1 */
    /* 0x33 */ u8 active;
    /* 0x34 */ u8 pad34[8];
} Rec3C; /* size 0x3C */

/* A 0x1010-byte block carved from the heap by fx_alloc_texture_caches. Only the
 * trailing words are touched here. */
typedef struct {
    /* 0x0000 */ u8 data[0x1000];
    /* 0x1000 */ s32 inUse;
    /* 0x1004 */ u16 age;
    /* 0x1006 */ u8 pad1006[0xA];
} HeapBlock; /* size 0x1010 */

extern Rec3C gEffects[16];
extern u8 gFxTileSlots[16];
extern HeapBlock * N64P gFxTexCaches;  /* first block */
extern HeapBlock * N64P gFxTexCachesEnd;  /* end of the blocks */
extern s16 gFxTexCachesFree;         /* number of blocks (asm reads it with lh) */
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
extern u8 gTexDecodeQueue[];
extern u8 * N64P gTexDecodeQueueTail;
extern u8 * N64P gTexDecodeQueueHead;

/* Empties the texture decode queue (each frame) (0x802A5720).
 * Resets the two cursors gTexDecodeQueueTail / gTexDecodeQueueHead to the start of the
 * buffer gTexDecodeQueue. */
void tex_reset_decode_queue(void) {
    gTexDecodeQueueTail = gTexDecodeQueue;
    gTexDecodeQueueHead = gTexDecodeQueue;
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/60F60/tex_reset_decode_queue.s")
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
/* Appends a decode request {dest, size, type, param} (0x802A5764).
 * Appends the four words (a, b, c, d) at the cursor gTexDecodeQueueTail and advances
 * it by 0x10. The asm takes them in s1, s2, s3 and fp (conventions.txt) and
 * saves v0/v1; its asm caller tex_queue_load_impl keeps f12 and f14 live across
 * the call (a mixed N64 build would need a thunk, the native port doesn't). */
void tex_queue_decode(s32 a, s32 b, s32 c, s32 d) {
    s32 *p = (s32 *) gTexDecodeQueueTail;

    p[0] = a;
    p[1] = b;
    p[2] = c;
    p[3] = d;
    gTexDecodeQueueTail = (u8 *) (p + 4);
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/60F60/tex_queue_decode.s")
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING

/* Decodes the next queued texture (after its DMA) (0x802A57AC).
 * Processes the next 16-byte record at the cursor gTexDecodeQueueHead with
 * tex_decode and advances the cursor by 0x10. The asm reuses the cursor
 * value it passed (tex_decode preserves a0) rather than rereading it, and
 * leaves tex_decode's v0 in v0 (the C caller declares it void). */
void tex_decode_next(void) {
    u8 *rec = gTexDecodeQueueHead;

    tex_decode(rec);
    gTexDecodeQueueHead = rec + 0x10;
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/60F60/tex_decode_next.s")
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
/* A 16-byte decode request (the records tex_decode_next walks). */
typedef struct {
    /* 0x0 */ u8 * N64P data;  /* packed on entry, decoded in place */
    /* 0x4 */ u32 size;  /* packed size in bytes (even) */
    /* 0x8 */ s32 type;  /* decoder: 0..6, anything else = none */
    /* 0xC */ u8 * N64P param; /* u16 table for types 4 and 5 */
} DecodeReq;

extern u64 gTexDecodeScratch[]; /* scratch copy of the packed data */
#ifdef PORT_HOST
/* Windows port, byte order (port/src/load/port_textures.c): the packed
 * tokens arrive big-endian (in host order before decoding), the decoded
 * texels leave as big-endian bytes, as the renderer expects them. */
void port_texture_input(u8 *data, u32 size, s32 type);
void port_texture_output(u8 *dst, u32 len, s32 type);
#endif

/* The decoders (non-ABI in the asm: src a0, size a1, dst a3, table t4;
 * they return the advanced dst in a3, see conventions.txt). */
u8 *tex_decode_type6(s16 *src, s32 len, u8 *dst);
u8 *tex_decode_type3(s16 *src, s32 len, u8 *dst);
u8 *tex_decode_type1(s16 *src, s32 len, u8 *dst);
u8 *tex_decode_type2(s16 *src, s32 len, u8 *dst);
u8 *tex_decode_type4(s16 *src, s32 len, u8 *dst, u8 *table);
u8 *tex_decode_type5(s16 *src, s32 len, u8 *dst, u8 *table);
u8 *tex_decode_type0(u64 *src, u32 len, u8 *dst);
/* The u16 tables of types 4 and 5 are texture-table entries of their own,
 * loaded as stored (type 0) data, so they stay big-endian in the port:
 * read them so (found by port/tools/compare.py: type-5 colours came out
 * with their channels swapped). */
#ifdef PORT_HOST
#define TBL_U16(t, off) ((u16) ((((u8 *) (t))[(off)] << 8) | ((u8 *) (t))[(off) + 1]))
#else
#define TBL_U16(t, off) (*(u16 *) ((u8 *) (t) + (off)))
#endif

/* Decodes one request in place, by packing type 0..6 (0x802A57DC).
 * Decodes a request in place: copies `size` packed bytes to gTexDecodeScratch
 * (8-byte units, then halfwords; the asm loops on `!= 0`, so size must be
 * even), runs the decoder for `type` from there back into `data`, and
 * returns the number of bytes written (0 for an unknown type).
 * Register note: the asm saves a0-a3, t0 and t4; asm caller tex_decode_next
 * reuses a0 and v1 after the call, tex_resolve_dl_impl/tex_get_impl keep f12 and
 * f14 (a mixed N64 build would need a thunk, the native port doesn't). */
s32 tex_decode(u8 *rec) {
    DecodeReq *req = (DecodeReq *) rec;
    u32 len = req->size;
    u32 n = (len >> 3) << 3;
    u64 *s8 = (u64 *) req->data;
    u64 *d8 = gTexDecodeScratch;
    u16 *s2;
    u16 *d2;
    u8 *dst;
    u8 *end;

#ifdef PORT_HOST
    port_texture_input(req->data, req->size, req->type);
#endif
    len -= n;
    for (; n != 0; n -= 8) {
        *d8++ = *s8++;
    }
    s2 = (u16 *) s8;
    d2 = (u16 *) d8;
    for (; len != 0; len -= 2) {
        *d2++ = *s2++;
    }

    dst = req->data;
    switch (req->type) {
        case 0:
            end = tex_decode_type0(gTexDecodeScratch, req->size, dst);
            break;
        case 1:
            end = tex_decode_type1((s16 *) gTexDecodeScratch, req->size, dst);
            break;
        case 2:
            end = tex_decode_type2((s16 *) gTexDecodeScratch, req->size, dst);
            break;
        case 4:
            end = tex_decode_type4((s16 *) gTexDecodeScratch, req->size, dst, req->param);
            break;
        case 5:
            end = tex_decode_type5((s16 *) gTexDecodeScratch, req->size, dst, req->param);
            break;
        case 3:
            end = tex_decode_type3((s16 *) gTexDecodeScratch, req->size, dst);
            break;
        case 6:
            end = tex_decode_type6((s16 *) gTexDecodeScratch, req->size, dst);
            break;
        default:
            end = dst;
            break;
    }
#ifdef PORT_HOST
    port_texture_output(dst, end - dst, req->type);
#endif
    return end - dst;
}

/* The decoders read s16 tokens until `len` bytes are consumed (the asm loops
 * on `len != 0`, so len must be even). A token with bit 15 clear is a literal
 * that expands to 2 or 4 output bytes; one with bit 15 set is a
 * back-reference: copy (tok & 0x1F) output units, one at a time and
 * overlap-safe, starting the given number of bytes back from dst. Each
 * returns the advanced dst (a3 in the asm). The asm saves every register it
 * uses except a3; its only caller is tex_decode. */

/* Back-reference of halfword units, offset (tok & 0x7FFF) >> 5 bytes. */
#define DECODE_BACKREF16(dst, tok)                                \
    {                                                             \
        s32 _n = (tok) & 0x1F;                                    \
        u16 *_from = (u16 *) ((dst) - (((tok) & 0x7FFF) >> 5));   \
                                                                  \
        while (_n != 0) {                                         \
            *(u16 *) (dst) = *_from++;                            \
            (dst) += 2;                                           \
            _n--;                                                 \
        }                                                         \
    }

/* Back-reference of word units, offset (tok & 0x7FE0) >> 4 bytes. */
#define DECODE_BACKREF32(dst, tok)                                \
    {                                                             \
        s32 _n = (tok) & 0x1F;                                    \
        u32 *_from = (u32 *) ((dst) - (((tok) & 0x7FE0) >> 4));   \
                                                                  \
        while (_n != 0) {                                         \
            *(u32 *) (dst) = *_from++;                            \
            (dst) += 4;                                           \
            _n--;                                                 \
        }                                                         \
    }
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/60F60/tex_decode.s")
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
/* Packing type 6 (0x802A5958).
 * Literal: two bytes, each 0bxxaaabbb -> 0baaa0bbb0 (bits 3-5 to 5-7,
 * bits 0-2 to 1-3). */
u8 *tex_decode_type6(s16 *src, s32 len, u8 *dst) {
    while (len != 0) {
        s32 tok = *src++;

        if (tok >= 0) {
            s32 hi = (u32) tok >> 8;
            s32 lo = tok & 0xFF;

            dst[0] = ((hi & 0x38) << 2) | ((hi & 7) << 1);
            dst[1] = ((lo & 0x38) << 2) | ((lo & 7) << 1);
            dst += 2;
            len -= 2;
        } else {
            len -= 2;
            DECODE_BACKREF16(dst, tok);
        }
    }
    return dst;
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/60F60/tex_decode_type6.s")
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
/* Packing type 3 (0x802A5A2C).
 * Literal: two bytes, each shifted left by one. */
u8 *tex_decode_type3(s16 *src, s32 len, u8 *dst) {
    while (len != 0) {
        s32 tok = *src++;

        if (tok >= 0) {
            dst[0] = ((u32) tok >> 8) << 1;
            dst[1] = (tok & 0xFF) << 1;
            dst += 2;
            len -= 2;
        } else {
            len -= 2;
            DECODE_BACKREF16(dst, tok);
        }
    }
    return dst;
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/60F60/tex_decode_type3.s")
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
/* Packing type 1 (0x802A5AE0).
 * Literal: one halfword, bits 6-15 shifted up by one (bit 6 becomes 0). */
u8 *tex_decode_type1(s16 *src, s32 len, u8 *dst) {
    while (len != 0) {
        s32 tok = *src++;

        if (tok >= 0) {
            *(u16 *) dst = ((tok & 0xFFC0) << 1) | (tok & 0x3F);
            dst += 2;
            len -= 2;
        } else {
            len -= 2;
            DECODE_BACKREF16(dst, tok);
        }
    }
    return dst;
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/60F60/tex_decode_type1.s")
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
/* Packing type 2 (0x802A5B90).
 * Literal: one word of four 4-bit fields (bits 11-14, 7-10, 3-6, 0-2)
 * expanded to the top of each byte. */
u8 *tex_decode_type2(s16 *src, s32 len, u8 *dst) {
    while (len != 0) {
        s32 tok = *src++;

        if (tok >= 0) {
            *(u32 *) dst = ((tok & 0x7800) << 17) | ((tok & 0x780) << 13) | ((tok & 0x78) << 9) |
                           ((tok & 7) << 5);
            dst += 4;
            len -= 2;
        } else {
            len -= 2;
            DECODE_BACKREF32(dst, tok);
        }
    }
    return dst;
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/60F60/tex_decode_type2.s")
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
/* Packing type 4 (0x802A5C5C).
 * Literal: two halfwords; each byte b of the token gives
 * (table[b >> 1] << 1) | (b & 1), table being u16 entries. */
u8 *tex_decode_type4(s16 *src, s32 len, u8 *dst, u8 *table) {
    while (len != 0) {
        s32 tok = *src++;

        if (tok >= 0) {
            s32 hi = (u32) tok >> 8;

            ((u16 *) dst)[0] = (TBL_U16(table, hi & 0xFE) << 1) | (hi & 1);
            ((u16 *) dst)[1] = (TBL_U16(table, tok & 0xFE) << 1) | (tok & 1);
            dst += 4;
            len -= 2;
        } else {
            len -= 2;
            DECODE_BACKREF32(dst, tok);
        }
    }
    return dst;
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/60F60/tex_decode_type4.s")
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
/* Packing type 5 (0x802A5D34).
 * Literal: one word from the 5-5-5 colour table[tok >> 4] (u16 entries), its
 * channels moved to the top of bytes 0-2, with (tok & 0xF) << 4 in byte 3. */
u8 *tex_decode_type5(s16 *src, s32 len, u8 *dst, u8 *table) {
    while (len != 0) {
        s32 tok = *src++;

        if (tok >= 0) {
            s32 c = TBL_U16(table, ((u32) tok >> 4) << 1);

            *(u32 *) dst = ((c & 0x7C00) << 17) | ((c & 0x3E0) << 14) | ((c & 0x1F) << 11) |
                           ((tok & 0xF) << 4);
            dst += 4;
            len -= 2;
        } else {
            len -= 2;
            DECODE_BACKREF32(dst, tok);
        }
    }
    return dst;
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/60F60/tex_decode_type5.s")
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
/* Packing type 0: a stored copy (0x802A5E10).
 * Type 0, stored: copies len >> 3 doublewords (a trailing len & 7 is
 * dropped). */
u8 *tex_decode_type0(u64 *src, u32 len, u8 *dst) {
    u32 n = len >> 3;

    while (n != 0) {
        *(u64 *) dst = *src++;
        dst += 8;
        n--;
    }
    return dst;
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/60F60/tex_decode_type0.s")
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
/* Every active effect jumps to its last frame (0x802A5E60).
 * For each active record of gEffects, sets `last` to the definition's
 * count (u16 at +0xE) minus 1, truncated to a byte.
 * Asm callers rely on preserved: carrier_check_crash keeps a0, a2, a3, t6, t7,
 * f12, f14; commpt_burst keeps t1. */
void fx_finish_all(void) {
    s32 i;

    for (i = 0; i < 16; i++) {
        if (gEffects[i].active != 0) {
            gEffects[i].last = *(u16 *) (gEffects[i].def + 0xE) - 1;
        }
    }
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/60F60/fx_finish_all.s")
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
/* The number of active effect records (0x802A5ED0).
 * Returns the number of active records of gEffects (t0 in the asm).
 * Asm callers (skyfall_update_anims, ramdozer_update_anims, backlash_update, amdream_update_effects,
 * ballista_update_actions, police_update_effects, ateam_update_effects, starski_update_effects) keep a0-a3,
 * t6, t7, f12 and f14 live across the call (a mixed N64 build would need a
 * thunk, the native port doesn't). */
s32 fx_count_active(void) {
    s32 i;
    s32 n = 0;

    for (i = 0; i < 16; i++) {
        if (gEffects[i].active != 0) {
            n++;
        }
    }
    return n;
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/60F60/fx_count_active.s")
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
/* Initlevel: no texture caches, all 16 records off (0x802A5F30).
 * Clears the heap-block range and count, deactivates all 16 records of
 * gEffects and clears the 16 bytes of gFxTileSlots.
 * Asm callers rely on preserved: initlevel keeps t0, f12, f14. */
void fx_reset(void) {
    s32 i;

    gFxTexCaches = NULL;
    gFxTexCachesEnd = NULL;
    gFxTexCachesFree = 0;
    for (i = 0; i < 16; i++) {
        gEffects[i].active = 0;
        gFxTileSlots[i] = 0;
    }
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/60F60/fx_reset.s")
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
extern u8 gFxHeapEndKind20[]; /* heap end when (gLevelKind & 0x20) */
extern u8 gFxHeapEnd[]; /* heap end otherwise */

/* The effects' 0x1010-byte texture caches ("Num texture caches=") (0x802A5FA8).
 * Carves as many 0x1010-byte HeapBlocks as fit between the heap pointer and
 * the heap end, records the range in gFxTexCaches / gFxTexCachesEnd and the count
 * in gFxTexCachesFree, advances the heap pointer past them, prints two debug
 * messages and clears each block's inUse word. */
void fx_alloc_texture_caches(void) {
    u8 *start = gHeapPtr;
    s32 span = ((gLevelKind & LEVEL_KIND_BUILDINGS) ? gFxHeapEndKind20 : gFxHeapEnd) - start;
    u32 n;
    HeapBlock *b;
    HeapBlock *end;

    if (span < 0) {
        span = 0;
    }
    n = (u32) span / sizeof(HeapBlock);
    gFxTexCaches = (HeapBlock *) start;
    gFxTexCachesFree = n;
    debug_printf(gFxTexCacheCountMsg);
    debug_printf(gFxTexCacheCountFmt, n);
    end = (HeapBlock *) (start + n * sizeof(HeapBlock));
    gFxTexCachesEnd = end;
    gHeapPtr = (u8 *) end;
    for (b = (HeapBlock *) start; b != end; b++) {
        b->inUse = 0;
#ifdef PORT_HOST
        {
            /* trailer bytes 0x1007-0x100F: never written (stale heap) */
            void port_garbage(const void *p, u32 len);

            port_garbage(&b->pad1006[1], 9);
        }
#endif
    }
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/60F60/fx_alloc_texture_caches.s")
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING

extern u8 gFxSpawnTag;

/* Sets up one of the 16 effect records for an EffectDef (0x802A6274).
 * Sets up a gEffects record for definition `def` (s16 id at +0, width and
 * height bytes at +2, +3). Stores io->a3 in gFxSpawnTag, takes the first
 * inactive record (byte 0x33 == 0; none of 16 -> return 0), and gives it
 * def[2] * def[3] free slots of the 16-byte map gFxTileSlots (their indices go
 * to rec+0x37.., then each is marked 1; running out returns 0 with the
 * indices written so far). Then: active = 1, +0 = def, +4 = data, +0x34 =
 * type, +0x32 = 0, +0x35 = b35, +0x3B = gFxSpawnTag. Type != 1: words
 * +8/+0xC/+0x10 = x/y/z, +0x20/+0x24/+0x28 = io->t6/w24/w28, +0x14/+0x18/
 * +0x1C = io->s1/w18/w1C, +0x2C = w2C, +0x36 = 0. Type 1 with z != 0: the
 * position comes from record (x, y) of veh_find_point (three words << 11),
 * type becomes 0, the other words 0 and +0x2C = 0xFC180000. Type 1 with
 * z == 0: bytes +0x30/+0x31 = x/y. Finally io->t6 = (s16) def[0] and, when
 * that isn't -1, io->s1 = the record's buffer (gFxTileSlots - 0x1000 +
 * index * 0x100) and tex_queue_palette(id, buffer). Returns 1 (0 on failure).
 * The asm takes its inputs in t0-t5, t6, t7, s0-s5 and a3 and returns in
 * t0 (conventions.txt); a3, t6 and s1 are also outputs (several asm callers
 * read them), and so are f12/f14, left as tex_queue_palette's libultra calls
 * leave them, which C can't express. It restores v0, v1, a0-a2, s6, s7. Asm
 * callers keep a0-a2 and t7 live (a mixed N64 build would need a thunk; the
 * native port won't). */
s32 fx_spawn(Io802A6274 *io, u8 *def, s32 data, s32 type, s32 x, s32 y, s32 z, s32 w24, s32 w28,
                  s32 w18, s32 w1C, s32 w2C, s32 b35) {
    u8 *rec = (u8 *) gEffects;
    u8 *slot;
    s32 i;
    s32 k;
    s32 n;

    gFxSpawnTag = io->a3;
    for (i = 0;; i++) {
        if (i == 16) {
            return 0;
        }
        if (rec[0x33] == 0) {
            break;
        }
        rec += 0x3C;
    }
    PORT_ISPAWN(PORT_ITAG_BILLBOARD, i);

    n = def[2] * def[3];
    slot = rec + 0x37;
    for (k = 0; n != 0; k++) {
        if (k == 16) {
            return 0;
        }
        io->a3 = gFxTileSlots[k];
        if (io->a3 == 0) {
            *slot++ = k;
            n--;
        }
    }
    n = def[2] * def[3];
    slot = rec + 0x37;
    while (n != 0) {
        n--;
        gFxTileSlots[*slot++] = 1;
    }

    rec[0x33] = 1;
    *(u8 * N64P *) (rec + 0x00) = def;
    *(s32 *) (rec + 0x04) = data;
    rec[0x34] = type;
    rec[0x32] = 0;
    rec[0x35] = b35;
    rec[0x3B] = gFxSpawnTag;
    if (type != 1) {
        *(s32 *) (rec + 0x08) = x;
        *(s32 *) (rec + 0x0C) = y;
        *(s32 *) (rec + 0x10) = z;
        *(s32 *) (rec + 0x20) = io->t6;
        *(s32 *) (rec + 0x24) = w24;
        *(s32 *) (rec + 0x28) = w28;
        *(s32 *) (rec + 0x14) = io->s1;
        *(s32 *) (rec + 0x18) = w18;
        *(s32 *) (rec + 0x1C) = w1C;
        *(s32 *) (rec + 0x2C) = w2C;
        rec[0x36] = 0;
    } else if (z != 0) {
        u8 *src;

        veh_find_point(x, y, &src);
        rec[0x34] = 0;
        *(s32 *) (rec + 0x08) = ((s32 *) src)[0] << 11;
        *(s32 *) (rec + 0x0C) = ((s32 *) src)[1] << 11;
        *(s32 *) (rec + 0x10) = ((s32 *) src)[2] << 11;
        *(s32 *) (rec + 0x20) = 0;
        *(s32 *) (rec + 0x24) = 0;
        *(s32 *) (rec + 0x28) = 0;
        *(s32 *) (rec + 0x14) = 0;
        *(s32 *) (rec + 0x18) = 0;
        *(s32 *) (rec + 0x1C) = 0;
        *(s32 *) (rec + 0x2C) = 0xFC180000;
        rec[0x36] = 0;
    } else {
        rec[0x30] = x;
        rec[0x31] = y;
    }

    io->t6 = *(s16 *) def;
    if (io->t6 != -1) {
        io->s1 = (s32) (gFxTileSlots - 0x1000 + i * 0x100);
        tex_queue_palette(io->t6, (void *) N64_IPTR(io->s1));
    }
    return 1;
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/60F60/fx_spawn.s")
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
extern Gfx * N64P gFxDlCursorA;
extern Gfx * N64P gFxDlCursorB;
extern s32 gFxMtx0[]; /* 16.16 matrices (0x40 bytes each), frame 0 / 1 */
extern s32 gFxMtx1[];
extern Vtx gFxVtx0[]; /* quad vertices, frame 0 / 1 */
extern Vtx gFxVtx1[];
extern u8 gFxTexBuf0[];  /* fallback texture buffers, frame 0 / 1 */
extern u8 gFxTexBuf1[];
extern u8 gFxLoadParams[];  /* 0x100-byte load parameter per record */
void fx_age_texture_caches(void);
u8 *fx_get_texture(u8 *rec, u8 *idx, u8 *base, u16 **list, u8 *param);
void fx_build_matrix(s32 *m, u8 *rec);
void fx_fill_quad(s32 w, Vtx *v, u8 *col, s32 x, s32 y, s32 h);
void fx_begin_dls(void);
void fx_draw_quad(u8 *obj, Vtx *vtx, void *timg, s32 fmtsiz, s32 width, s32 height, s32 zbuf);
Gfx * N64P *fx_get_dl_cursor(u8 *obj);

/* Draws and advances the effects, ends finished ones, ages caches (0x802A64A4).
 * Draws the 16 gEffects billboard records into the display lists of this
 * frame's buffer set (gBufferIndex picks lists, matrices, vertices and texture
 * buffers): resets both list cursors (gFxDlCursorA/B, then fx_begin_dls).
 * For each active record: its matrix (fx_build_matrix, matrices advance by
 * 0x40 per drawn record), then def[3] rows of def[2] quads of size w x h
 * (def u16 +0xA, +0xC) centred on the origin, x from (cols * w) >> 1 down by
 * w, y from -((rows * h) >> 1) up by h: each quad's vertices (fx_fill_quad,
 * colour from def), its texture (fx_get_texture with the record's slot bytes
 * at +0x37, the frame's id list at def + 0x10 + 2 * cols * rows * frame and
 * the record's 0x100-byte parameter) and the textured quad (fx_draw_quad,
 * format def u16 +4, z-buffer byte +0x35); then a G_POPMTX in the record's
 * list (fx_get_dl_cursor), and the frame (+0x32) advances, or at def u16 +0xE
 * the record ends (inactive, its map slots in gFxTileSlots freed). Finally
 * both lists get a G_ENDDL (cursors not advanced) and the heap blocks age
 * (fx_age_texture_caches). The asm saves s0-s7, gp, fp; the survey's inputs (a1, s1,
 * s2) are only saved. The `sub` stepping x traps on overflow (game range). */
void fx_draw_all(void) {
    s32 *mtx;
    Vtx *vtx;
    u8 *texBase;
    u8 *rec = (u8 *) gEffects;
    u8 *param = gFxLoadParams;
    s32 n;

    if (gBufferIndex != 0) {
        gFxDlCursorA = gFxDlA1;
        gFxDlCursorB = gFxDlB1;
        mtx = gFxMtx1;
        vtx = gFxVtx1;
        texBase = gFxTexBuf1;
    } else {
        gFxDlCursorA = gFxDlA0;
        gFxDlCursorB = gFxDlB0;
        mtx = gFxMtx0;
        vtx = gFxVtx0;
        texBase = gFxTexBuf0;
    }
    fx_begin_dls();
    for (n = 16; n != 0; n--, rec += 0x3C, param += 0x100) {
        u8 *def;
        u8 *idx;
        u16 *list;
        u32 cols;
        u32 rows;
        s32 w;
        s32 h;
        s32 fmtsiz;
        s32 zbuf;
        s32 x0;
        s32 y;
        s32 frame;
        Gfx * N64P *cur;
        Gfx *g;

        if (rec[0x33] == 0) {
            continue;
        }
        PORT_ITAG(*fx_get_dl_cursor(rec), PORT_ITAG_BILLBOARD, 16 - n);
        fx_build_matrix(mtx, rec);
        def = *(u8 * N64P *) rec;
        zbuf = rec[0x35];
        idx = rec + 0x37;
        cols = def[2];
        rows = def[3];
        h = *(u16 *) (def + 0xC);
        fmtsiz = *(u16 *) (def + 4);
        w = *(u16 *) (def + 0xA);
        list = (u16 *) (def + ((cols * rows * rec[0x32]) << 1) + 0x10);
        x0 = (s32) (cols * w) >> 1;
        y = -((s32) (rows * h) >> 1);
        while (rows != 0) {
            s32 x = x0;
            u32 c;

            rows--;
            for (c = cols; c != 0;) {
                u8 *buf;

                c--;
                fx_fill_quad(w, vtx, def, x, y, h);
                buf = fx_get_texture(rec, idx, texBase, &list, param);
                idx++;
                fx_draw_quad(rec, vtx, buf, fmtsiz, w, h, zbuf);
                vtx += 4;
                x -= w;
            }
            y += h;
        }
        mtx += 0x10;
        cur = fx_get_dl_cursor(rec);
        g = *cur;
        g->words.w0 = 0xBD000000;
        g->words.w1 = 0;
        *cur = g + 1;
        def = *(u8 * N64P *) rec;
        frame = rec[0x32] + 1;
        if (frame != *(u16 *) (def + 0xE)) {
            rec[0x32] = frame;
        } else {
            u8 *p = rec + 0x37;
            u32 k = def[2] * def[3];

            rec[0x33] = 0;
            while (k != 0) {
                k--;
                gFxTileSlots[*p++] = 0;
            }
        }
    }
    PORT_ITAG(gFxDlCursorA, PORT_ITAG_NONE, 0);
    PORT_ITAG(gFxDlCursorB, PORT_ITAG_NONE, 0);
    gFxDlCursorA->words.w0 = 0xB8000000;
    gFxDlCursorA->words.w1 = 0;
    gFxDlCursorB->words.w0 = 0xB8000000;
    gFxDlCursorB->words.w1 = 0;
    fx_age_texture_caches();
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/60F60/fx_draw_all.s")
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
/* Frees texture caches unused for 61 frames (0x802A6748).
 * Ages every in-use HeapBlock by one; a block whose age reaches 0x3D (61)
 * is freed (inUse = 0). Adds the number freed to gFxTexCachesFree. */
void fx_age_texture_caches(void) {
    HeapBlock *b;
    s32 freed = 0;

    for (b = gFxTexCaches; b != gFxTexCachesEnd; b++) {
        s32 age;

        if (b->inUse == 0) {
            continue;
        }
        age = b->age + 1; /* u16 + 1, compared as a full int */
        b->age = age;
        if (age >= 0x3D) {
            freed++;
            b->inUse = 0;
        }
    }
    gFxTexCachesFree += freed;
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/60F60/fx_age_texture_caches.s")
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
/* gFxCachedDefs (top of this file): 0-terminated list of definitions that get a heap block */

/* A tile's texture buffer (cache or fallback); loads the next id (0x802A67C4).
 * Texture buffer for record `rec` (its def word at +0 and byte +0x32): if a
 * heap block (gFxTexCaches .. gFxTexCachesEnd, walked with `!=`) is in use for that
 * def and byte, resets its age and returns it (nothing loaded). Otherwise, if
 * blocks remain (gFxTexCachesFree != 0) and the def is in the list gFxCachedDefs, takes
 * one (gFxTexCachesFree--, first block with inUse == 0, unbounded), tags it with
 * the def and byte, age 0; failing that the buffer is base + (*idx << 12).
 * Either way the next u16 id from *list (advanced by 2) is loaded into the
 * buffer with tex_queue_load_impl(id, buffer, param). Returns the buffer.
 * Register convention: asm takes rec, idx, base, list, param in t4, a0, t3,
 * t9, fp and returns the buffer in s1 and the advanced list in t9
 * (conventions.txt); it restores v0-a3 and leaves t6 = the id loaded. Its asm
 * caller fx_draw_all keeps a0, a1, t0, t1, t3, t4, t5, t7, t8 live. */
u8 *fx_get_texture(u8 *rec, u8 *idx, u8 *base, u16 **list, u8 *param) {
    HeapBlock *b = gFxTexCaches;
    HeapBlock *end = gFxTexCachesEnd;
    s32 key = rec[0x32];
    u32 def = *(u32 *) rec;
    u8 * N64P *l;
    u8 *buf;

    for (; b != end; b++) {
        if (b->inUse == 0 || def != (u32) b->inUse || key != b->pad1006[0]) {
            continue;
        }
        b->age = 0;
        return (u8 *) b;
    }
    if (gFxTexCachesFree == 0) {
        goto fallback;
    }
    for (l = gFxCachedDefs;; l++) {
        if ((u32) *l == def) {
            break;
        }
        if (*l == NULL) {
            goto fallback;
        }
    }
    gFxTexCachesFree--;
    for (b = gFxTexCaches; b->inUse != 0; b++) {
    }
    b->inUse = def;
    b->pad1006[0] = key;
    b->age = 0;
    buf = (u8 *) b;
    goto load;
fallback:
    buf = base + (*idx << 12);
load:
    tex_queue_load_impl(**list, buf, param);
    (*list)++;
    return buf;
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/60F60/fx_get_texture.s")
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
extern s32 gFxScratchMtx[]; /* scratch 16.16 matrix */

/* Scale, motion and bounce, face the camera, translate (0x802A68D4).
 * Billboard matrix of record rec into the 16.16 matrix m: emits a matrix
 * load of m (gSPMatrix word 0x01040040, physical address) into the record's
 * display list (fx_get_dl_cursor), sets m to the uniform scale rec+4
 * (mtx_scale), then the position: type 1 (+0x34) records use the words of
 * veh_find_point(+0x30, +0x31)'s record; others move by velocity (+0x14..+0x1C)
 * times the step byte +0x36 plus +0x20..+0x28 (stored back into +8..+0x10),
 * and once y (+0xC) drops to the floor +0x2C the y speed +0x24 becomes
 * |+0x24 + +0x18 * step| * 3/4 (v - (v >> 2)) and the step restarts (it is
 * stored back + 1); the position is >> 11. The pitch is the arcsine of
 * (player y - y) / distance (math_dist3 on the player position >> 11,
 * logical shifts; 64-bit division of dy << 27 by distance << 11, distance 0
 * taken as 1), >> 4 and mirrored (0xFFF - angle) unless the quotient is
 * negative: m *= rotation about x by that (mtx_rotate_x, mtx_mul),
 * m *= rotation about y by gCamYaw, m *= translation (position << 11),
 * then m is converted to the Mtx layout (mtx_to_n64).
 * Register convention: m in t0, rec in t4; the asm saves every integer
 * register (asm caller fx_draw_all keeps a1, t0, t1, t3, t4, t5 live). The
 * f12/f14 the survey lists as read by fx_draw_all are dead there. The asm's
 * ddiv traps on a zero divisor (distance << 11 wrapping to 0; game range). */
void fx_build_matrix(s32 *m, u8 *rec) {
    Gfx * N64P *cur = fx_get_dl_cursor(rec);
    Gfx *g = *cur;
    s32 scale;
    s32 x;
    s32 y;
    s32 z;
    s32 dist;
    s32 py;
    s64 q;
    s32 angle;

    g->words.w1 = (u32) m & 0x1FFFFFFF;
    g->words.w0 = 0x01040040;
    *cur = g + 1;
    scale = *(s32 *) (rec + 4);
    mtx_scale(scale, scale, scale, m);
    if (rec[0x34] == 1) {
        u8 *src;

        veh_find_point(rec[0x30], rec[0x31], &src);
        x = ((s32 *) src)[0];
        y = ((s32 *) src)[1];
        z = ((s32 *) src)[2];
    } else {
        s32 step = rec[0x36];
        s32 y0 = *(s32 *) (rec + 0xC);

        if (*(s32 *) (rec + 0x2C) >= y0) {
            s32 v = *(s32 *) (rec + 0x24) + *(s32 *) (rec + 0x18) * step;

            if (v < 0) {
                v = -v;
            }
            *(s32 *) (rec + 0x24) = v - (v >> 2);
            step = 0;
        }
        rec[0x36] = step + 1;
        x = *(s32 *) (rec + 0x08) + *(s32 *) (rec + 0x14) * step + *(s32 *) (rec + 0x20);
        *(s32 *) (rec + 0x08) = x;
        x >>= 11;
        y = y0 + *(s32 *) (rec + 0x18) * step + *(s32 *) (rec + 0x24);
        *(s32 *) (rec + 0x0C) = y;
        y >>= 11;
        z = *(s32 *) (rec + 0x10) + *(s32 *) (rec + 0x1C) * step + *(s32 *) (rec + 0x28);
        *(s32 *) (rec + 0x10) = z;
        z >>= 11;
    }
    py = (u32) gCamEyeY >> 11;
    dist = math_dist3((u32) gCamEyeX >> 11, py, (u32) gCamEyeZ >> 11, x, y, z);
    if (dist == 0) {
        dist = 1;
    }
    q = ((s64) (py - y) << 27) / (s32) ((u32) dist << 11);
    angle = math_asin((q >= 0) ? (s32) q : -(s32) q) >> 4;
    if (q >= 0) {
        angle = 0xFFF - angle;
    }
    mtx_rotate_x(angle, gFxScratchMtx);
    mtx_mul(gFxScratchMtx, m);
    mtx_rotate_y(((u16) gCamYaw), gFxScratchMtx);
    mtx_mul(gFxScratchMtx, m);
    mtx_translate(x << 11, y << 11, z << 11, gFxScratchMtx);
    mtx_mul(gFxScratchMtx, m);
    mtx_to_n64((u16 *) m);
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/60F60/fx_build_matrix.s")
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
/* One tile's 4 vertices in the definition's colour (0x802A6C10).
 * Fills a 4-vertex quad: corners (x, y), (x - w, y), (x, y + h),
 * (x - w, y + h) at z = 0, texture coordinates (0 or w << 5, 0 or h << 5),
 * and every vertex's colour from bytes 6-9 of `col`. The asm takes
 * w, v, col, x, y, h in v1, t1, t7, s2, s3, s5 (conventions.txt) and saves
 * everything it uses; asm caller fx_draw_all keeps a0-a3, t0, t1, t3-t5,
 * t7-t9, f12 and f14 live across the call (a mixed N64 build would need a
 * thunk, the native port doesn't). */
void fx_fill_quad(s32 w, Vtx *v, u8 *col, s32 x, s32 y, s32 h) {
    s32 x1 = x - w;
    s32 y1 = y + h;
    s32 s = w << 5;
    s32 t = h << 5;
    s32 i;

    v[0].v.ob[0] = x;
    v[0].v.ob[1] = y;
    v[0].v.ob[2] = 0;
    v[0].v.flag = 0;
    v[0].v.tc[0] = 0;
    v[0].v.tc[1] = 0;
    v[1].v.ob[0] = x1;
    v[1].v.ob[1] = y;
    v[1].v.ob[2] = 0;
    v[1].v.flag = 0;
    v[1].v.tc[0] = s;
    v[1].v.tc[1] = 0;
    v[2].v.ob[0] = x;
    v[2].v.ob[1] = y1;
    v[2].v.ob[2] = 0;
    v[2].v.flag = 0;
    v[2].v.tc[0] = 0;
    v[2].v.tc[1] = t;
    v[3].v.ob[0] = x1;
    v[3].v.ob[1] = y1;
    v[3].v.ob[2] = 0;
    v[3].v.flag = 0;
    v[3].v.tc[0] = s;
    v[3].v.tc[1] = t;
    for (i = 0; i < 4; i++) {
        v[i].v.cn[0] = col[6];
        v[i].v.cn[1] = col[7];
        v[i].v.cn[2] = col[8];
        v[i].v.cn[3] = col[9];
    }
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/60F60/fx_fill_quad.s")
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
extern Gfx * N64P gFxDlCursorA;
extern Gfx * N64P gFxDlCursorB;

/* The render-state prologue of both effect lists (0x802A6D34).
 * Runs gfx_begin_textured on the display-list cursors gFxDlCursorA and gFxDlCursorB,
 * in that order, storing the advanced cursors back.
 * Asm callers rely on preserved: fx_draw_all keeps a0-a3, t0, t1, t3-t5,
 * f12, f14 (the asm saves v0, v1, a0-a3, t0, t1, t3-t9 around the calls). */
void fx_begin_dls(void) {
    gFxDlCursorA = gfx_begin_textured(gFxDlCursorA);
    gFxDlCursorB = gfx_begin_textured(gFxDlCursorB);
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/60F60/fx_begin_dls.s")
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
Gfx * N64P *fx_get_dl_cursor(u8 *obj);

/* A textured tile into the record's list (0x802A6DE8).
 * Draws a textured quad into obj's display-list cursor (fx_get_dl_cursor picks
 * gFxDlCursorA or gFxDlCursorB): *cursor = gfx_draw_textured_quad(*cursor, vtx, timg,
 * fmtsiz, width, height, zbuf). The asm takes obj, vtx, timg, fmtsiz, width,
 * height, zbuf in t4, t1, s1, t8, v1, s5, gp (conventions.txt), saves every
 * register it uses except s0 (obj copy) and t2, and passes fmtsiz to
 * gfx_draw_textured_quad as the full register (the C prototype's s16 narrows it).
 * Asm caller fx_draw_all keeps a0-a3, t0, t1, t3-t5, t7-t9, f12, f14 live
 * (a mixed N64 build would need a thunk; the native port won't). */
void fx_draw_quad(u8 *obj, Vtx *vtx, void *timg, s32 fmtsiz, s32 width, s32 height, s32 zbuf) {
    Gfx *gfx = gfx_draw_textured_quad(*fx_get_dl_cursor(obj), vtx, timg, fmtsiz, width, height, zbuf);

    *fx_get_dl_cursor(obj) = gfx;
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/60F60/fx_draw_quad.s")
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
/* The list cursor of the record (list B if byte 0x3B) (0x802A6EB8).
 * Picks a display-list cursor: &gFxDlCursorB if byte 0x3B of `obj` is set,
 * else &gFxDlCursorA (obj in t4, result in t2 in the asm). Asm callers keep
 * registers live across the call: fx_draw_all a0-a3, t0, t1, t3-t5,
 * f12, f14; fx_build_matrix a0-a3, t0, t4, f12, f14; fx_draw_quad t1, t8
 * (a mixed N64 build would need a thunk, the native port doesn't). */
Gfx * N64P *fx_get_dl_cursor(u8 *obj) {
    if (obj[0x3B] != 0) {
        return &gFxDlCursorB;
    }
    return &gFxDlCursorA;
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/60F60/fx_get_dl_cursor.s")
#endif
