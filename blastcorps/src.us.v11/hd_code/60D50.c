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
/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
/* A rectangle in a per-level zone list (coordinates are world units >> 5). */
typedef struct {
    /* 0x00 */ s16 minX;
    /* 0x02 */ s16 minZ;
    /* 0x04 */ s16 maxX;
    /* 0x06 */ s16 maxZ;
    /* 0x08 */ s16 height;
} HeightZone; /* size 0xA */

#define HEIGHT_ZONE_NONE 3000 /* sentinel height value */


/* GCamHeight from the highest height zone over the player (0x802A5510).
 * `level` is the loaded level header (gLevelData). Its words at 0x40/0x44 are
 * the start/end offsets of a HeightZone list. Finds the highest zone that
 * contains the player (inclusive bounds, starting from -1). A result of
 * HEIGHT_ZONE_NONE sets gCamNoHeightZone; otherwise gCamHeight = height * 32 +
 * gCamHeightBase and gCamNoHeightZone is cleared. The asm loops with `!=`, so the end
 * offset must be start + n * 10. */
void cam_apply_height_zone(u8 *level) {
    HeightZone *zone = (HeightZone *) (level + *(s32 *) (level + 0x40));
    HeightZone *end = (HeightZone *) (level + *(s32 *) (level + 0x44));
    /* logical shifts (srl) in the asm, then signed compares */
    s32 px = (u32) gPlayerX >> 5;
    s32 pz = (u32) gPlayerZ >> 5;
    s32 best = -1;

    for (; zone != end; zone++) {
        if (px < zone->minX || pz < zone->minZ || zone->maxX < px || zone->maxZ < pz) {
            continue;
        }
        if (zone->height >= best) {
            best = zone->height;
        }
    }

    if (best == HEIGHT_ZONE_NONE) {
        gCamNoHeightZone = 1;
    } else {
        gCamHeight = (best << 5) + ((u16) gCamHeightBase);
        gCamNoHeightZone = 0;
    }
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/60D50/cam_apply_height_zone.s")
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING

/* The chopper's cruise height from the zones under it (0x802A5604).
 * Like cam_apply_height_zone, for the point (gChopperX, gChopperZ) and the
 * HeightZone list at level+0x44 / +0x48: gChopperCruiseY = (highest containing
 * zone's height, or -1) << 5. The end offset must be start + n * 10.
 * Asm callers rely on preserved: chopper_fly keeps a2, a3, t7, f12, f14. */
void chopper_find_cruise_height(u8 *level) {
    HeightZone *zone = (HeightZone *) (level + *(s32 *) (level + 0x44));
    HeightZone *end = (HeightZone *) (level + *(s32 *) (level + 0x48));
    /* logical shifts (srl) in the asm, then signed compares */
    s32 px = (u32) gChopperX >> 5;
    s32 pz = (u32) gChopperZ >> 5;
    s32 best = -1;

    for (; zone != end; zone++) {
        if (px < zone->minX || pz < zone->minZ || zone->maxX < px || zone->maxZ < pz) {
            continue;
        }
        if (zone->height >= best) {
            best = zone->height;
        }
    }
    gChopperCruiseY = best << 5;
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/60D50/chopper_find_cruise_height.s")
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
extern s16 gVehCamDefaults[][3]; /* per vehicle type: {gCamDistance, gCamHeightBase, return value} */

/* The level's camera distance and height base; returns the at-height (0x802A56C4).
 * Loads the triple for the level's start vehicle type gStartVehicle: stores the
 * first two entries in gCamDistance / gCamHeightBase and returns the third
 * (sign-extended). */
s32 cam_load_level_defaults(void) {
    s16 *entry = gVehCamDefaults[gStartVehicle];

    gCamDistance = entry[0];
    gCamHeightBase = entry[1];
    return entry[2];
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/60D50/cam_load_level_defaults.s")
#endif
