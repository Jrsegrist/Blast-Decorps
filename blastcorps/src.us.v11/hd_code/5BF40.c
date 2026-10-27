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
#ifdef NON_MATCHING
/* Shared declarations for the NON_MATCHING (port) rewrites below. */
extern u32 * N64P gTexTable;         /* ROM offsets (from 0x4CE0) of the entries, 8 bytes apart */
extern u8 * N64P gTexCacheEnd;
extern u8 gTexCache[];
/* Entry n of the gTexTable table spans ROM [0x4CE0 + w[2n], 0x4CE0 + w[2n + 2]). */
#define TABLE_ROM_BASE 0x4CE0

/* Entry n of gTexTable (8 bytes): ROM offset, then the unpacked size and the
 * decoder type as u16s (the next entry's offset ends it). */
typedef struct {
    /* 0x0 */ u32 rom;
    /* 0x4 */ u16 size;
    /* 0x6 */ u16 type;
} TexTableEntry;

/* The 16-byte decode request handed to tex_decode (60F60.c's DecodeReq). */
typedef struct {
    /* 0x0 */ u8 * N64P data;
    /* 0x4 */ u32 size;
    /* 0x8 */ s32 type;
    /* 0xC */ u8 * N64P param;
} DecodeReq;
extern DecodeReq gTexDecodeReq;

/* gTexCache..gTexCacheEnd: the loaded-texture cache, 8-byte {id, physical
 * address} pairs; gTexCacheEnd is the first free pair. */

/* Loads table entry `id` at the heap pointer gHeapPtr (into `buf`):
 * invalidates 0x1000 bytes there, DMAs the packed entry in and waits, decodes
 * it in place through gTexDecodeReq (tex_decode; the caller sets .param where
 * the asm does) and advances the heap by the decoded size. */
#define LOAD_TABLE_ENTRY(id, buf)                                                                 \
    {                                                                                             \
        TexTableEntry *e_ = (TexTableEntry *) ((u8 *) gTexTable + (id) * 8);                     \
        s32 n_;                                                                                   \
                                                                                                  \
        (buf) = gHeapPtr;                                                                       \
        osInvalDCache(buf, 0x1000);                                                               \
        gTexDecodeReq.size = e_->size;                                                               \
        gTexDecodeReq.type = e_->type;                                                               \
        gTexDecodeReq.data = (buf);                                                                  \
        osPiStartDma(&gDmaIoMesg, OS_MESG_PRI_NORMAL, OS_READ, TABLE_ROM_BASE + e_->rom, buf,     \
                     ((TexTableEntry *) ((u8 *) e_ + 8))->rom - e_->rom, &textureDmaMessageQ);            \
        osRecvMesg(&textureDmaMessageQ, NULL, OS_MESG_BLOCK);                                             \
        n_ = tex_decode((u8 *) &gTexDecodeReq);                                                   \
        gHeapPtr += n_;                                                                         \
    }
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
/* The 0x8000-byte ROM texture table to the heap, once (0x802A0700).
 * Once (until gTexTableLoaded is set): takes 0x8000 heap bytes for the table
 * gTexTable, DMAs ROM 0x4CE0..+0x8000 into it and waits, then points
 * gTexCacheEnd at gTexCache.
 * The asm saves and restores every register; initlevel keeps t0, t6, t9
 * across it. (Its survey outputs f12/f14 are what the libultra calls leave.) */
void tex_load_table(void) {
    u8 *buf;

    if (gTexTableLoaded == 0) {
        buf = gHeapPtr;
        gTexTable = (u32 *) buf;
        gHeapPtr = buf + 0x8000;
        osInvalDCache(buf, 0x8000);
        osPiStartDma(&gDmaIoMesg, OS_MESG_PRI_NORMAL, OS_READ, TABLE_ROM_BASE, buf, 0x8000, &textureDmaMessageQ);
        osRecvMesg(&textureDmaMessageQ, NULL, OS_MESG_BLOCK);
        gTexCacheEnd = gTexCache;
        gTexTableLoaded = 1;
    }
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/5BF40/tex_load_table.s")
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING

/* C entry of tex_resolve_dl_impl (0x802A08B4).
 * C-callable entry: tex_resolve_dl_impl(dl, end) (dl/end moved to s0/s1, which it
 * saves). The asm passes its caller's s4 through as tex_resolve_dl_impl's in/out
 * s4 and doesn't restore s2-s6 (tex_resolve_dl_impl's outputs/scratch); nothing
 * calls it directly (no jal to it in the ROM), so `r` is just a local here. */
void tex_resolve_dl(u32 *dl, u32 *end) {
    Unk802A08E4Regs r;

    tex_resolve_dl_impl(dl, end, &r);
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/5BF40/tex_resolve_dl.s")
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
/* A display list's SETTIMG texture ids to loaded addresses (0x802A08E4).
 * Walks the display list [dl, end) (8-byte commands) and, for each
 * G_SETTIMG (0xFD), replaces its address word (a table id) with the physical
 * address of that texture: from the cache pairs if the id is there, else it
 * appends a pair, loads the entry at the heap (LOAD_TABLE_ENTRY, .param left
 * as it is) and caches its physical address (heap - 0x80000000). Stores the
 * new cache end in gTexCacheEnd.
 * Register convention: dl in s0, end in s1; outputs s2, s3, s4 through `r`
 * (conventions.txt). The asm saves every other register except s0 (= end
 * after), s5, s6 (scratch); its add/sub trap. Asm callers keep a1-a3, t1-t4
 * live; they also rely on f12/f14 surviving the libultra calls. */
void tex_resolve_dl_impl(u32 *dl, u32 *end, Unk802A08E4Regs *r) {
    u8 *cur = gTexCacheEnd;
    u8 *p;
    u8 *buf;
    u32 id;

    while (dl != end) {
        dl += 2;
        if (((dl[-2] & 0xFF000000) >> 24) != 0xFD) {
            continue;
        }
        id = dl[-1];
        for (p = gTexCache; p != cur; p += 8) {
            if (*(u32 *) p == id) {
                break;
            }
        }
        r->s4 = p;
        if (p == cur) {
            *(u32 *) cur = id;
            LOAD_TABLE_ENTRY(id, buf);
            ((u32 *) cur)[1] = (u32) buf - 0x80000000;
            cur += 8;
        }
        dl[-1] = ((u32 *) p)[1];
    }
    gTexCacheEnd = cur;
    r->s2 = &gTexCacheEnd;
    r->s3 = cur;
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/5BF40/tex_resolve_dl_impl.s")
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
void tex_load_to_heap_impl(s32 id, u8 *param);

/* C entry of tex_load_to_heap_impl (0x802A0B00).
 * C-callable entry: tex_load_to_heap_impl(id, param) (moved to t6/fp). The asm
 * returns its caller's s0 in v0 (a leftover); C callers declare it void.
 * The whole id register is used (2E490.c/2D810.c declare u16). */
void tex_load_to_heap(s32 id, u8 *param) {
    tex_load_to_heap_impl(id, param);
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/5BF40/tex_load_to_heap.s")
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
/* Loads and decodes a texture at gHeapPtr, not cached (0x802A0B34).
 * Loads table entry `id` at the heap (LOAD_TABLE_ENTRY with .param = param),
 * uncached. Register convention: id in t6, param in fp (conventions.txt); the
 * asm saves every register (it relies on the libultra calls keeping f12/f14,
 * which C needn't). */
void tex_load_to_heap_impl(s32 id, u8 *param) {
    u8 *buf;

    gTexDecodeReq.param = param;
    LOAD_TABLE_ENTRY(id, buf);
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/5BF40/tex_load_to_heap_impl.s")
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING

/* C entry of tex_get_impl (0x802A0CC8).
 * C-callable entry: returns tex_get_impl(id, param), the texture's physical
 * address (the asm moves id/param to t6/fp and returns s0 in v0). The whole
 * id register is used (callers declare s16 or s32). */
u32 tex_get(s32 id, u8 *param) {
    return tex_get_impl(id, param);
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/5BF40/tex_get.s")
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
/* A texture's physical address: cached, else loaded at the heap (0x802A0CFC).
 * Physical address of texture `id`: from the cache pairs gTexCache.. if
 * present, else appends a pair, loads the entry at the heap
 * (LOAD_TABLE_ENTRY with .param = param) and caches heap - 0x80000000.
 * Register convention: id in t6, param in fp, result in s0
 * (conventions.txt); the asm saves every other register. Asm callers keep
 * t1, t2, t4-t7 live and rely on f12/f14 surviving the libultra calls. */
u32 tex_get_impl(s32 id, u8 *param) {
    u8 *cur = gTexCacheEnd;
    u8 *p;
    u8 *buf;
    u32 phys;

    for (p = gTexCache; p != cur; p += 8) {
        if (*(s32 *) p == id) {
            return ((u32 *) p)[1];
        }
    }
    *(s32 *) cur = id;
    gTexCacheEnd = cur + 8;
    gTexDecodeReq.param = param;
    LOAD_TABLE_ENTRY(id, buf);
    phys = (u32) buf - 0x80000000;
    ((u32 *) cur)[1] = phys;
    return phys;
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/5BF40/tex_get_impl.s")
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
void tex_load_palette_impl(s32 id, void *dest);

/* C entry of tex_load_palette_impl (0x802A0EE0).
 * tex_load_palette_impl(id, dest) (the asm moves them to t6/s1 and saves s1).
 * 2E490.c declares it (u16 id, void *dest); the asm uses the whole register. */
void tex_load_palette(s32 id, void *dest) {
    tex_load_palette_impl(id, dest);
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/5BF40/tex_load_palette.s")
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
/* DMAs a 0x100-byte palette and waits (0x802A0F0C).
 * DMAs entry `id` of the gTexTable table (ROM 0x4CE0 + w[2id] up to the next
 * entry's start) into the 0x100-byte buffer `dest` and waits for it.
 * asm: id in t6, dest in s1; it saves every register but s0/s1 (unchanged). */
void tex_load_palette_impl(s32 id, void *dest) {
    u32 *e;

    osInvalDCache(dest, 0x100);
    e = gTexTable + id * 2;
    osPiStartDma(&gDmaIoMesg, OS_MESG_PRI_NORMAL, OS_READ, TABLE_ROM_BASE + e[0], dest, e[2] - e[0],
                 &textureDmaMessageQ);
    osRecvMesg(&textureDmaMessageQ, NULL, OS_MESG_BLOCK);
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/5BF40/tex_load_palette_impl.s")
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING

/* C entry of tex_queue_load_impl (0x802A1040).
 * C-callable entry: tex_queue_load_impl(id, dest, param) (moved to t6/s1/fp). The
 * whole id register is used (168B0.c declares u16, 32E00.c/43A60.c s16). */
void tex_queue_load(s32 id, u8 *dest, u8 *param) {
    tex_queue_load_impl(id, dest, param);
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/5BF40/tex_queue_load.s")
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
extern u8 gTexDmaMesgs[];

/* Starts a texture DMA and queues its decode (0x802A1074).
 * Queued load of table entry `id` into `dest`: invalidates 0x1000 bytes,
 * starts the DMA with the next 0x14-byte OSIoMesg slot of gTexDmaMesgs
 * (nextdma++) without waiting, and queues the decode request
 * (dest, size, type, param) with tex_queue_decode.
 * Register convention: id in t6, dest in s1, param in fp (conventions.txt);
 * the asm saves every register it uses. Asm caller fx_get_texture keeps t9. */
void tex_queue_load_impl(s32 id, u8 *dest, u8 *param) {
    TexTableEntry *e;
    s32 slot;

    osInvalDCache(dest, 0x1000);
    slot = nextdma++;
    e = (TexTableEntry *) ((u8 *) gTexTable + id * 8);
    osPiStartDma((OSIoMesg *) (gTexDmaMesgs + slot * 0x14), OS_MESG_PRI_NORMAL, OS_READ, TABLE_ROM_BASE + e->rom,
                 dest, e[1].rom - e->rom, &textureDmaMessageQ);
    tex_queue_decode((s32) dest, e->size, e->type, (s32) param);
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/5BF40/tex_queue_load_impl.s")
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
extern u8 gTexDmaMesgs[]; /* OSIoMesg slots, 0x14 bytes each */

/* Starts a palette DMA (no_palette_dmas counts them) (0x802A11C4).
 * Like tex_load_palette_impl but doesn't wait: counts no_palette_dmas up and issues the
 * DMA with the next 0x14-byte OSIoMesg slot of gTexDmaMesgs (nextdma++).
 * asm: id in t6, dest in s1; it saves every register but s0/s1 (unchanged).
 * Asm callers rely on preserved: fx_spawn keeps a3, t6. (Its survey
 * outputs f12/f14 are what the libultra calls leave.) */
void tex_queue_palette(s32 id, void *dest) {
    u32 *e;
    s32 slot;

    osInvalDCache(dest, 0x100);
    no_palette_dmas++;
    slot = nextdma++;
    e = gTexTable + id * 2;
    osPiStartDma((OSIoMesg *) (gTexDmaMesgs + slot * 0x14), OS_MESG_PRI_NORMAL, OS_READ,
                 TABLE_ROM_BASE + e[0], dest, e[2] - e[0], &textureDmaMessageQ);
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/5BF40/tex_queue_palette.s")
#endif
