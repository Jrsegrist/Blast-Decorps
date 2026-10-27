#include "common.h"
#include <ultra64.h>
#include "game/game.h"
/* Views: this file reads these shared variables (game/variables.h) as other types. */
#define gRadarMarkers ((RadarMarker *) gRadarMarkers)
/* end of views */

/* FILE-WIDE FINDING: this file's functions save $ra via the 64-bit `sd`/`ld`
 * doubleword form, not the normal 32-bit `sw`/`lw` pair - the same signature
 * documented in full in hd_code/77E20.c's top-of-file note and the project
 * skill file, confirming (with no exceptions found) that this file is part
 * of the same large hand-written-assembly module. Any pragma below without
 * a more specific comment follows this convention; a few have their own
 * more specific non-ABI explanation where one was already worked out. */

#ifdef NON_MATCHING
/* Functional rewrites (verified with tools_port/eqcheck.py). gRadarMarkers is a
 * 25-slot table of radar/map markers keyed by an id; a free slot has
 * id == -1. Callers: 479D0 (ids 0x10000+), 48D00 (0x100+), 4B5E0 (0+). */
typedef struct {
    /* 0x00 */ s32 id;
    /* 0x04 */ s32 x;
    /* 0x08 */ s32 y;
    /* 0x0C */ s32 z;
    /* 0x10 */ s32 size; /* 479D0 passes its radarSize here */
} RadarMarker; /* size 0x14 */

#define RADAR_MARKER_COUNT 25
#define RADAR_MARKER_FREE (-1)

#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
/* Clears the radar marker table (0x802CE840).
 * Clear the marker table: mark every slot free. */
void radar_clear_markers(void) {
    s32 i;

    for (i = 0; i < RADAR_MARKER_COUNT; i++) {
        gRadarMarkers[i].id = RADAR_MARKER_FREE;
    }
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/8A080/radar_clear_markers.s")
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
/* Adds or updates marker id (x, y, z, size) (0x802CE880).
 * Add or update marker `id`: reuse its slot if present, else claim the first
 * free slot. If the table is full, nothing happens. */
void radar_set_marker(s32 id, s32 x, s32 y, s32 z, s32 size) {
    RadarMarker *m;
    s32 i;

    for (i = 0, m = gRadarMarkers; i < RADAR_MARKER_COUNT; i++, m++) {
        if (m->id == id) {
            goto found;
        }
    }
    for (i = 0, m = gRadarMarkers; i < RADAR_MARKER_COUNT; i++, m++) {
        if (m->id == RADAR_MARKER_FREE) {
            m->id = id;
            goto found;
        }
    }
    return;

found:
    m->x = x;
    m->y = y;
    m->z = z;
    m->size = size;
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/8A080/radar_set_marker.s")
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
/* Removes marker id (0x802CE90C).
 * Remove marker `id` (first matching slot only). */
void radar_remove_marker(s32 id) {
    s32 i;

    for (i = 0; i < RADAR_MARKER_COUNT; i++) {
        if (gRadarMarkers[i].id == id) {
            gRadarMarkers[i].id = RADAR_MARKER_FREE;
            return;
        }
    }
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/8A080/radar_remove_marker.s")
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
/* 1 if marker id is in the table (0x802CE958).
 * Returns 1 if marker `id` is in the table, else 0. */
s32 radar_has_marker(s32 id) {
    s32 i;

    for (i = 0; i < RADAR_MARKER_COUNT; i++) {
        if (gRadarMarkers[i].id == id) {
            return 1;
        }
    }
    return 0;
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/8A080/radar_has_marker.s")
#endif

/* coll_reset_dyn_tris: `gDynTrisEnd = &gDynTris;` wrapped in the same dead
 * `addiu sp,sp,-8`/`sd $ra`/`ld $ra`/`addiu sp,sp,8` frame as
 * train_exit_check/barge_exit_check - confirmed hand-written by the same
 * probe evidence (no calls, no locals, nothing that could need a frame;
 * IDO never generates this shape on its own). Permanently GLOBAL_ASM. */
#ifdef NON_MATCHING

/* Resets the dynamic collision triangles' write cursor (0x802CE9A4).
 * Reset a buffer cursor (read by 4B5E0) to the start of gDynTris. */
void coll_reset_dyn_tris(void) {
    gDynTrisEnd = gDynTris;
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/8A080/coll_reset_dyn_tris.s")
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM.
 * It sets up `count` collision triangles at *gDynTrisEnd (lvl_build_coll_tri,
 * byte 0x51 = 1, h52 = a2, b56 = item byte 0x14, b55 = 0) from 0x16-byte
 * items at a0, but also passes lvl_build_coll_tri the registers v0 (id, byte
 * 0x50), t6 (byte 0x57), t9 (byte 0x4F) and s1 (byte 0x58, chained) that its
 * only caller, the IDO-compiled block_spawn_all (4B5E0.c), never sets for it:
 * there they hold a stale call result, &gBlockSlots[i], the old gDynTrisEnd
 * and the caller's caller's s1. */
#ifdef NON_MATCHING

/* N items into 0x60-byte collision triangles (0x802CE9C8).
 * The NM build used to keep this asm, which `jal`s the C lvl_build_coll_tri with
 * its asm register convention: the C read garbage arguments and faulted on
 * the first level load (the attract-mode demo). The leaked registers are now
 * explicit parameters; block_spawn_all passes what they hold in the original
 * (see there). Per 0x16-byte item: rec[0x51] = 1, then lvl_build_coll_tri(rec,
 * item, id, h52, b57, item[0x14], s1 chain, b4F, b55 = 0). The asm saves and
 * restores s1 (each call's chain starts from the caller's s1) and leaves
 * gp = 0. Like the asm, n counts down with `!=`.
 * asm: items a0, n a1, h52 a2, id v0, b57 t6, b4F t9, s1. */
void coll_add_dyn_tris(u8 *items, s32 n, s32 h52, s32 id, s32 b57, s32 b4F, s32 s1) {
    u8 *rec = gDynTrisEnd;

    while (n != 0) {
        n--;
        rec[0x51] = 1;
        rec = lvl_build_coll_tri(rec, items, id, h52, b57, items[0x14], &s1, b4F, 0);
        items += 0x16;
    }
    gDynTrisEnd = rec;
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/8A080/coll_add_dyn_tris.s")
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
/* Makes a range of collision triangles not solid (0x802CEA68).
 * Clear byte 0x51 of each 0x60-byte record in [start, end). Like the asm,
 * this loops with `!=`, so `end` must be `start + n * 0x60`. */
void coll_disable_tris(u8 *start, u8 *end) {
    u8 *p;

    for (p = start; p != end; p += 0x60) {
        p[0x51] = 0;
    }
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/8A080/coll_disable_tris.s")
#endif
