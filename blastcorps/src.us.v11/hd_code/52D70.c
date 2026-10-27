#include "common.h"
#include <ultra64.h>
#include "game/game.h"
/* Views: this file reads these shared variables (game/variables.h) as other types. */
#define gAcademyMarkers ((AcademyMarker *) gAcademyMarkers)
#define players ((PlayerInfo *) players)
/* end of views */

/* (players[]: PlayerInfo, game/types.h) */

/* (gAcademyMarkers: AcademyMarker, game/types.h.) */

extern u8 gAcademyMarkerTouched;
extern Mtx * N64P gAcademyMarkerMtx;
extern u8 gAcademyBits;


/* Places the level's academy marker (0x80297530).
 * K&R in the matching build (hd.c passes an int). */
#ifdef NON_MATCHING
void academy_place_marker(u8 arg0)
#else
void academy_place_marker(arg0)
    u8 arg0;
#endif
{
    u8 found;
    u8 idx;

    found = 0;
    if (gNextGameMode == MODE_LEVEL_START) {
        found = academy_find_level(arg0, &idx);
    } else {
        found = 0;
    }
    gAcademyBits = players[playerNumber].academy;
    if (found) {
        gAcademyMarkerOn = 1;
        gAcademyMarkerX = gAcademyMarkers[idx].x;
        gAcademyMarkerY = gAcademyMarkers[idx].y;
        gAcademyMarkerZ = gAcademyMarkers[idx].z;
        gAcademyMarkerIndex = idx;
        gAcademyMarkerTouched = 0;
        gAcademyMarkerMtx = (Mtx *) gHeapPtr;
        gHeapPtr = (u8 *) gHeapPtr + 0x80;
        guTranslate(gAcademyMarkerMtx, 0, 0, 0);
        guTranslate(gAcademyMarkerMtx + 1, 0, 0, 0);
    } else {
        gAcademyMarkerOn = 0;
    }
}

/* The level's entry in the academy table (0x8029766C). */
u8 academy_find_level(u8 arg0, u8 *arg1) {
    u8 found;
    s32 i;

    found = 0;
    i = 0;
    do {
        if (gAcademyMarkers[i].id == arg0) {
            found = 1;
        } else {
            i++;
        }
    } while (!found && i < 6);
    if (arg1 != NULL) {
        *arg1 = i;
    }
    return found;
}

/* Draws the academy marker (0x802976E8). */
void academy_draw_marker(Gfx **arg0) {
    Gfx *gdl = *arg0;

    if (gAcademyMarkerOn) {
        gSPSegment(gdl++, 6, osVirtualToPhysical(gAcademyMarkerVtx));
        gSPSegment(gdl++, 7, osVirtualToPhysical(gAcademyMarkerMtx));
        gSPClearGeometryMode(gdl++, -1);
        gSPDisplayList(gdl++, osVirtualToPhysical(gAcademyMarkerDl));
        gDPPipeSync(gdl++);
    }
    *arg0 = gdl;
}

/* Within 0x50: the bit set, mode switch (0x80297804). */
void academy_check_marker(s32 x, s32 y, s32 z) {
    if (gAcademyMarkerOn && players[playerNumber].gameState < 5) {
        if (gAcademyMarkerTouched) {
            if (util_dist3d(x >> 5, y >> 5, z >> 5, gAcademyMarkerX, gAcademyMarkerY, gAcademyMarkerZ) >= 0x8D) {
                gAcademyMarkerTouched = 0;
            }
        } else if (util_dist3d(x >> 5, y >> 5, z >> 5, gAcademyMarkerX, gAcademyMarkerY, gAcademyMarkerZ) < 0x50) {
            gAcademyMarkerTouched = 1;
            gAcademyBits |= 1 << gAcademyMarkerIndex;
            gNextGameMode = MODE_ACADEMY_MARKER;
        }
    }
}

/* The markers into players[].academy bits (0x80297960). */
void academy_save_markers(void) {
    if (gDebugCheats && (gContButtons & Z_TRIG) && gAcademyMarkerOn) {
        gAcademyBits |= 1 << gAcademyMarkerIndex;
    }
    players[playerNumber].academy = gAcademyBits;
}
