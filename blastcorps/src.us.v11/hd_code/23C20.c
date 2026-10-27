#include "common.h"
#include <ultra64.h>
#ifndef NON_MATCHING /* legacy declarations */
/* Matching build: IDO compiled the matched code here against older
 * declarations of these, which the file keeps; the NON_MATCHING build
 * uses game/game.h's. */
#define LEGACY_gVehiclesEnd
#define LEGACY_gHintLastHitObject
#endif /* legacy declarations */
#include "game/game.h"
/* Views: this file reads these shared variables (game/variables.h) as other types. */
#define gYoshiGameItems ((YoshiItem *) gYoshiGameItems)
#define gYoshiWindows ((YoshiWindow *) gYoshiWindows)
#define gVehicles ((VehRecord *) gVehicles)
#define players ((PlayerInfo *) players)
#ifdef NON_MATCHING
#define gVehiclesEnd (*(VehRecord * N64P *) &gVehiclesEnd)
#define gHintLastHitObject (*(s32 *) &gHintLastHitObject)
#endif
/* end of views */

/* utils2.c (from its assert strings): camera spline paths, triggers, small maths helpers. */

f32 sqrtf(f32);
f32 util_shuttle_path_eval(f32 x, f32 y, f32 z, f32 w, f32 a, f32 b, f32 c);
f32 util_cam_path_eval(f32 x, f32 y, f32 z, f32 w, f32 a, f32 b, f32 c);

#define ASSERT(EX, line) \
    if (!(EX)) debug_printf("\n --- ASSERTION FAULT - %s - %s, line %d\n\n", #EX, "utils2.c", line)

/* A boarding zone (gBoardZones, 0x14 bytes; one entry is searched): on foot in the
 * zone's quad (two triangles 0-1-2 and 0-2-3, x/z >> 5) of the zone's vehicle, and
 * within radius of it, the driver boards it (util_check_board_zone) */
typedef struct {
    u8 id;       /* level */
    u8 vehicle;  /* vehicle type */
    s16 x0;
    s16 z0;
    s16 x1;
    s16 z1;
    s16 x2;
    s16 z2;
    s16 x3;
    s16 z3;
    s16 radius;
} BoardZone;

/* A camera path (gCamPaths, 3): its cardinal-spline tension and speed */
typedef struct {
    s32 id;
    f32 tension;
    u8 speed;  /* gCamPathT step factor */
    u8 pad9[3];
} CamPath;

/* A camera path's control point (gCamPathPoints, 90) */
typedef struct {
    s32 id;     /* the path */
    u8 index;   /* point number along it */
    u8 pad5;
    s16 x;
    s16 y;
    s16 z;
    u8 speed;
    u8 padD[3];
} CamPathPoint;

/* A shuttle flight path (gShuttlePaths, 3): per level and phase */
typedef struct {
    s32 id;       /* level */
    u8 phase;     /* 0 the intro fly-over (MODE_INTRO_FLY), 1 in the level */
    u8 kind;      /* gShuttlePathKind (1: the end fades to MODE_SEQ_END) */
    u8 pad6[2];
    f32 tension;
    s16 triggerY; /* gShuttleTriggerY, >> 5 */
    s16 minY;     /* the shuttle never goes below it (-1: no limit), >> 5 */
    u8 speed;     /* gShuttlePathT step factor */
    u8 end0;      /* last point of branch 0 (the landing), 1 (aborted) and 2 */
    u8 end1;
    u8 end2;
} ShuttlePath;

/* A shuttle path's control point (gShuttlePathPoints, 49): with angles and events */
typedef struct {
    s32 id;      /* level */
    u8 phase;    /* as ShuttlePath.phase */
    u8 idx;
    s16 x;
    s16 y;
    s16 z;
    s16 rot[3];
    u8 speed;
    u8 branch;   /* gShuttlePathBranch: 0 the landing, 1 aborted, ... */
    u8 event;
    u8 pad15[3];
} ShuttlePathPoint;

/* (This file writes the landing messages into window item 42 and their sound into
 * window YW_SHUTTLE_MESSAGE: YoshiItem, YoshiWindow; the vehicles: VehRecord;
 * game/types.h.) */

/* (players[]: PlayerInfo, game/types.h) */

extern BoardZone gBoardZones[];
extern CamPath gCamPaths[];
extern CamPathPoint gCamPathPoints[];
extern ShuttlePath gShuttlePaths[];
extern ShuttlePathPoint gShuttlePathPoints[];
extern u16 gShuttleLandingAbortedJText[];
extern u16 gShuttleDitchingJText[];
extern u16 gShuttleFinalApproachJText[];
extern u16 gShuttleLandedJText[];
#ifndef NON_MATCHING
extern VehRecord *gVehiclesEnd;
#endif
extern u8 gCamPathIndex;
extern f32 gCamPathBasis[4][4];
extern s32 gCamPathT;
extern u8 gCamPathPoint;
extern f32 gShuttlePathBasis[4][4];
extern s32 gShuttlePathT;
extern u8 gShuttlePathPoint;
extern u8 gShuttlePathIndex;
extern u8 gShuttlePathAltBranch[4];
extern u8 gShuttlePathBranch;
extern u8 gShuttlePathWindow[4];
extern u8 gShuttlePathInLevel;
extern s32 gRandSeed2;
extern u8 gHintNearVehicle;
#ifndef NON_MATCHING
extern s32 gHintLastHitObject;
#endif
extern u8 gHintPrevVehicle;
extern u8 gHintLastVehicle;

/* On foot near a zone's vehicle: switch to it (0x802683E0).
 * Checks the trigger zones for the current level; sets gNextVehicle on a hit. */
void util_check_board_zone(void) {
    s32 i;
    u8 found;
    u8 hit;
    VehRecord *p;

    i = 0;
    found = 0;
    if (gCurVehicle || gDriverExitWalk) {
        return;
    }
    while (!found && i < 1) {
        if (gBoardZones[i].id == levelno) {
            p = gVehicles;
            hit = 0;
            while (!hit && gVehiclesEnd != p) {
                if (gBoardZones[i].vehicle == p->type) {
                    hit = 1;
                } else {
                    p++;
                }
            }
            if (hit) {
                if (ground_in_triangle(p->x >> 5, p->z >> 5, gBoardZones[i].x0, gBoardZones[i].z0,
                                  gBoardZones[i].x1, gBoardZones[i].z1, gBoardZones[i].x2, gBoardZones[i].z2)
                    || ground_in_triangle(p->x >> 5, p->z >> 5, gBoardZones[i].x0, gBoardZones[i].z0,
                                     gBoardZones[i].x2, gBoardZones[i].z2, gBoardZones[i].x3, gBoardZones[i].z3)) {
                    if (util_dist3d(p->x, p->y, p->z, gPlayerX, gPlayerY, gPlayerZ) < gBoardZones[i].radius) {
                        gNextVehicle = gBoardZones[i].vehicle;
                        found = 1;
                    }
                }
            }
        }
        i++;
    }
}

/* The level's camera path, its spline basis (0x80268664).
 * Selects spline path id and builds its cardinal-spline basis matrix in gCamPathBasis. */
void util_cam_path_init(s32 id) {
    f32 s;

    gCamPathActive = 0;
    gCamPathIndex = 0;
    do {
        if (gCamPaths[gCamPathIndex].id == id) {
            gCamPathActive = 1;
        } else {
            gCamPathIndex++;
        }
    } while (gCamPathIndex < 3 && !gCamPathActive);
    if (gCamPathActive) {
        s = gCamPaths[gCamPathIndex].tension;
        gCamPathBasis[0][0] = -s;
        gCamPathBasis[0][1] = s * 2.0;
        gCamPathBasis[0][2] = -s;
        gCamPathBasis[0][3] = 0.0f;
        gCamPathBasis[1][0] = 2.0 - s;
        gCamPathBasis[1][1] = s - 3.0;
        gCamPathBasis[1][2] = 0.0f;
        gCamPathBasis[1][3] = 1.0f;
        gCamPathBasis[2][0] = s - 2.0;
        gCamPathBasis[2][1] = 3.0 - s * 2.0;
        gCamPathBasis[2][2] = s;
        gCamPathBasis[2][3] = 0.0f;
        gCamPathBasis[3][0] = s;
        gCamPathBasis[3][1] = -s;
        gCamPathBasis[3][2] = 0.0f;
        gCamPathBasis[3][3] = 0.0f;
        gCamPathT = 0;
        gCamPathPoint = 1;
    }
}

/* GCamPathX/Y/Z along the path (0x802688C4).
 * Advances along the path: evaluates the spline at the four current control points. */
void util_cam_path_step(s32 path) {
    s32 j;
    s32 i;
    u8 pts[4];
    u8 want[4];
    f32 t;
    f32 t2;
    f32 t3;
    s32 pad[4];
    u8 found;

    if (gCamPathActive) {
        want[0] = gCamPathPoint - 1;
        want[1] = gCamPathPoint;
        want[2] = gCamPathPoint + 1;
        want[3] = gCamPathPoint + 2;
        for (j = 0; j < 4; j++) {
            i = 0;
            found = 0;
            while (i < 90 && !found) {
                if (gCamPathPoints[i].id == path && gCamPathPoints[i].index == want[j]) {
                    found = 1;
                } else {
                    i++;
                }
            }
            ASSERT(found, 181);
            pts[j] = i;
        }
        t = (f32) gCamPathT / 1000.0;
        t2 = t * t;
        t3 = t2 * t;
        gCamPathX = util_cam_path_eval(gCamPathPoints[pts[0]].x << 5, gCamPathPoints[pts[1]].x << 5, gCamPathPoints[pts[2]].x << 5,
                                   gCamPathPoints[pts[3]].x << 5, t, t2, t3);
        gCamPathY = util_cam_path_eval(gCamPathPoints[pts[0]].y << 5, gCamPathPoints[pts[1]].y << 5, gCamPathPoints[pts[2]].y << 5,
                                   gCamPathPoints[pts[3]].y << 5, t, t2, t3);
        gCamPathZ = util_cam_path_eval(gCamPathPoints[pts[0]].z << 5, gCamPathPoints[pts[1]].z << 5, gCamPathPoints[pts[2]].z << 5,
                                   gCamPathPoints[pts[3]].z << 5, t, t2, t3);
        if (!gLevelDone && !gCarrierHit && !areWeFading()) {
            gCamPathT += gCamPaths[gCamPathIndex].speed *
                          ((gCamPathPoints[pts[2]].speed - gCamPathPoints[pts[1]].speed) * t + gCamPathPoints[pts[1]].speed);
        }
        if (gCamPathT >= 1000) {
            gCamPathT = 0;
            gCamPathPoint++;
        }
    }
}

/* Evaluates the camera path (0x80268D84).
 * Evaluates one spline coordinate with basis gCamPathBasis. */
f32 util_cam_path_eval(f32 x, f32 y, f32 z, f32 w, f32 a, f32 b, f32 c) {
    s32 i0;
    s32 i1;
    s32 i2;
    s32 i3;

    i0 = gCamPathBasis[0][0] * x + gCamPathBasis[1][0] * y + gCamPathBasis[2][0] * z + gCamPathBasis[3][0] * w;
    i1 = gCamPathBasis[0][1] * x + gCamPathBasis[1][1] * y + gCamPathBasis[2][1] * z + gCamPathBasis[3][1] * w;
    i2 = gCamPathBasis[0][2] * x + gCamPathBasis[1][2] * y + gCamPathBasis[2][2] * z + gCamPathBasis[3][2] * w;
    i3 = gCamPathBasis[0][3] * x + gCamPathBasis[1][3] * y + gCamPathBasis[2][3] * z + gCamPathBasis[3][3] * w;
    return i0 * c + b * i1 + a * i2 + i3;
}


/* 1 if the level has a shuttle path; its kind (0x80268EE8). */
s32 util_find_shuttle_path(s32 id) {
    s32 i;

    i = 0;
    do {
        if (gShuttlePaths[i].id == id) {
            gShuttlePathKind = gShuttlePaths[i].kind;
            return 1;
        }
        i++;
    } while (i < 3);
    return 0;
}

/* The shuttle's flight path (0x80268F54).
 * Picks the second path set (gShuttlePaths) for this level and builds basis gShuttlePathBasis. */
void util_shuttle_path_init(void) {
    u8 found;
    f32 s;

    found = 0;
    if (gNextGameMode == MODE_INTRO_FLY) {
        gShuttlePathInLevel = 0;
    } else {
        gShuttlePathInLevel = 1;
    }
    gShuttlePathIndex = 0;
    while (!found) {
        if (gShuttlePaths[gShuttlePathIndex].id == levelno && gShuttlePaths[gShuttlePathIndex].phase == gShuttlePathInLevel) {
            found = 1;
        } else {
            gShuttlePathIndex++;
        }
    }
    s = gShuttlePaths[gShuttlePathIndex].tension;
    gShuttlePathBasis[0][0] = -s;
    gShuttlePathBasis[0][1] = s * 2.0;
    gShuttlePathBasis[0][2] = -s;
    gShuttlePathBasis[0][3] = 0.0f;
    gShuttlePathBasis[1][0] = 2.0 - s;
    gShuttlePathBasis[1][1] = s - 3.0;
    gShuttlePathBasis[1][2] = 0.0f;
    gShuttlePathBasis[1][3] = 1.0f;
    gShuttlePathBasis[2][0] = s - 2.0;
    gShuttlePathBasis[2][1] = 3.0 - s * 2.0;
    gShuttlePathBasis[2][2] = s;
    gShuttlePathBasis[2][3] = 0.0f;
    gShuttlePathBasis[3][0] = s;
    gShuttlePathBasis[3][1] = -s;
    gShuttlePathBasis[3][2] = 0.0f;
    gShuttlePathBasis[3][3] = 0.0f;
    gShuttleTriggerY = gShuttlePaths[gShuttlePathIndex].triggerY << 5;
    gShuttlePathT = 0;
    gShuttlePathPoint = 1;
    gShuttlePathAltBranch[0] = 0;
    gShuttlePathAltBranch[1] = 0;
    gShuttlePathAltBranch[2] = 0;
    gShuttlePathAltBranch[3] = 0;
    gShuttlePathBranch = 0;
    gShuttleAborted = 0;
    gShuttleCamFollow = 0;
}

/* The shuttle along its path: position, angles, landing events (0x80269258).
 * Camera/aircraft path follower on the gShuttlePathPoints spline set: position, angles, and the landing messages. */
void util_shuttle_path_step(void) {
    s32 j;
    s32 i;
    u8 pts[4];
    f32 t;
    f32 t2;
    f32 t3;
    f32 a0;
    f32 a1;
    f32 a2;
    f32 a3;
    f32 r0;
    f32 r1;
    f32 r2;
    u8 found;
    u8 lim;

    gShuttlePathWindow[0] = gShuttlePathPoint - 1;
    gShuttlePathWindow[1] = gShuttlePathPoint;
    gShuttlePathWindow[2] = gShuttlePathPoint + 1;
    gShuttlePathWindow[3] = gShuttlePathPoint + 2;
    for (j = 0; j < 4; j++) {
        i = 0;
        found = 0;
        while (i < 49 && !found) {
            if (gShuttlePathPoints[i].id == levelno && gShuttlePathPoints[i].phase == gShuttlePathInLevel
                && gShuttlePathPoints[i].idx == gShuttlePathWindow[j] && gShuttlePathPoints[i].branch == gShuttlePathBranch) {
                found = 1;
            } else {
                i++;
            }
        }
        if (!found) {
            i = 0;
            while (i < 49 && !found) {
                if (gShuttlePathPoints[i].id == levelno && gShuttlePathPoints[i].phase == gShuttlePathInLevel
                    && gShuttlePathPoints[i].idx == gShuttlePathWindow[j] && gShuttlePathPoints[i].branch == gShuttlePathAltBranch[j]) {
                    found = 1;
                } else {
                    i++;
                }
            }
        }
        ASSERT(found, 387);
        pts[j] = i;
    }
    t = (f32) gShuttlePathT / 1000.0;
    t2 = t * t;
    t3 = t2 * t;

    a0 = gShuttlePathPoints[pts[0]].rot[0];
    a1 = gShuttlePathPoints[pts[1]].rot[0];
    a2 = gShuttlePathPoints[pts[2]].rot[0];
    a3 = gShuttlePathPoints[pts[3]].rot[0];
    util_unwrap_angle(a0, &a1);
    util_unwrap_angle(a1, &a2);
    util_unwrap_angle(a2, &a3);
    r0 = util_shuttle_path_eval(a0, a1, a2, a3, t, t2, t3);
    gShuttleRotX = r0 / 360.0 * 4095.0;

    a0 = gShuttlePathPoints[pts[0]].rot[1];
    a1 = gShuttlePathPoints[pts[1]].rot[1];
    a2 = gShuttlePathPoints[pts[2]].rot[1];
    a3 = gShuttlePathPoints[pts[3]].rot[1];
    util_unwrap_angle(a0, &a1);
    util_unwrap_angle(a1, &a2);
    util_unwrap_angle(a2, &a3);
    r1 = util_shuttle_path_eval(a0, a1, a2, a3, t, t2, t3);
    gShuttleRotY = r1 / 360.0 * 4095.0;

    a0 = gShuttlePathPoints[pts[0]].rot[2];
    a1 = gShuttlePathPoints[pts[1]].rot[2];
    a2 = gShuttlePathPoints[pts[2]].rot[2];
    a3 = gShuttlePathPoints[pts[3]].rot[2];
    util_unwrap_angle(a0, &a1);
    util_unwrap_angle(a1, &a2);
    util_unwrap_angle(a2, &a3);
    r2 = util_shuttle_path_eval(a0, a1, a2, a3, t, t2, t3);
    gShuttleRotZ = r2 / 360.0 * 4095.0;

    gShuttlePos = util_shuttle_path_eval(gShuttlePathPoints[pts[0]].x << 5, gShuttlePathPoints[pts[1]].x << 5, gShuttlePathPoints[pts[2]].x << 5,
                               gShuttlePathPoints[pts[3]].x << 5, t, t2, t3);
    gShuttlePosY = util_shuttle_path_eval(gShuttlePathPoints[pts[0]].y << 5, gShuttlePathPoints[pts[1]].y << 5, gShuttlePathPoints[pts[2]].y << 5,
                               gShuttlePathPoints[pts[3]].y << 5, t, t2, t3);
    gShuttlePosZ = util_shuttle_path_eval(gShuttlePathPoints[pts[0]].z << 5, gShuttlePathPoints[pts[1]].z << 5, gShuttlePathPoints[pts[2]].z << 5,
                               gShuttlePathPoints[pts[3]].z << 5, t, t2, t3);
    if (gShuttlePaths[gShuttlePathIndex].minY != -1 && gShuttlePosY < gShuttlePaths[gShuttlePathIndex].minY << 5) {
        gShuttlePosY = gShuttlePaths[gShuttlePathIndex].minY << 5;
    }
    gShuttlePathEvent = 0;
    if (!gLevelDone && !gCarrierHit && !areWeFading()) {
        gShuttlePathT += gShuttlePaths[gShuttlePathIndex].speed *
                      ((gShuttlePathPoints[pts[2]].speed - gShuttlePathPoints[pts[1]].speed) * t + gShuttlePathPoints[pts[1]].speed);
    }
    if (gShuttlePathT >= 1000) {
        gShuttlePathT = 0;
        gShuttlePathPoint++;
        switch (gShuttlePathBranch) {
            case 0:
                lim = gShuttlePaths[gShuttlePathIndex].end0;
                break;
            case 1:
                lim = gShuttlePaths[gShuttlePathIndex].end1;
                break;
            case 2:
                lim = gShuttlePaths[gShuttlePathIndex].end2;
                break;
        }
        if (gShuttlePathPoint >= lim) {
            switch (gShuttlePathBranch) {
                case 0:
                    if (gShuttlePathKind == 1) {
                        fade_to_mode_speed(MODE_SEQ_END, 0.5f);
                    } else {
                        gLevelDoneRequest = 1;
                        gPauseRequest = 1;
                    }
                    break;
                case 1:
                    cmo_hit_request = 1;
                    gPauseRequest = 1;
                    break;
            }
        }
        switch (gShuttlePathPoints[pts[2]].event) {
            case 0:
                break;
            case 1:
                if (((s32) gLevelKind) == LEVEL_KIND_MISSION_TIMED) {
                    gShuttlePathBranch = 1;
                } else {
                    obj_tally(0);
                    if (gScoreBuildings < gBuildingCount) {
                        gShuttleAborted = 1;
                        gShuttlePathBranch = 1;
                    }
                    gShuttleCamFollow = 1;
                    gPlayerHidden = 1;
                }
                break;
            case 2:
            case 3:
            case 11:
            case 12:
            case 13:
                gShuttlePathEvent = gShuttlePathPoints[pts[2]].event;
                break;
            case 5:
                gYoshiGameItems[42].text = "LANDING ABORTED!";
                gYoshiGameItems[42].jtext = gShuttleLandingAbortedJText;
                gYoshiWindows[YW_SHUTTLE_MESSAGE].openSound = 0xDA;
                if (gGameMode & (MODE_LEVEL | MODE_MAP_VIEW)) {
                    yoshi_demand(YOSHI_DEMAND_OPEN | YW_SHUTTLE_MESSAGE);
                }
                break;
            case 6:
                gYoshiGameItems[42].text = "DITCHING IN SEA!";
                gYoshiGameItems[42].jtext = gShuttleDitchingJText;
                gYoshiWindows[YW_SHUTTLE_MESSAGE].openSound = 0x77;
                if (gGameMode & (MODE_LEVEL | MODE_MAP_VIEW)) {
                    yoshi_demand(YOSHI_DEMAND_OPEN | YW_SHUTTLE_MESSAGE);
                }
                break;
            case 4:
                gYoshiGameItems[42].text = "ON FINAL APPROACH!";
                gYoshiGameItems[42].jtext = gShuttleFinalApproachJText;
                gYoshiWindows[YW_SHUTTLE_MESSAGE].openSound = 0xD7;
                if (gGameMode & (MODE_LEVEL | MODE_MAP_VIEW)) {
                    yoshi_demand(YOSHI_DEMAND_OPEN | YW_SHUTTLE_MESSAGE);
                }
                break;
            case 7:
                gYoshiGameItems[42].text = "SUCCESSFUL LANDING!";
                gYoshiGameItems[42].jtext = gShuttleLandedJText;
                gYoshiWindows[YW_SHUTTLE_MESSAGE].openSound = 0x82;
                if (gGameMode & (MODE_LEVEL | MODE_MAP_VIEW)) {
                    yoshi_demand(YOSHI_DEMAND_OPEN | YW_SHUTTLE_MESSAGE);
                }
                break;
            case 8:
                gYoshiGameItems[42].text = "3000 FT!";
                gYoshiGameItems[42].jtext = NULL;
                gYoshiWindows[YW_SHUTTLE_MESSAGE].openSound = 0xD5;
                if (gGameMode & (MODE_LEVEL | MODE_MAP_VIEW)) {
                    yoshi_demand(YOSHI_DEMAND_OPEN | YW_SHUTTLE_MESSAGE);
                }
                break;
            case 9:
                gYoshiGameItems[42].text = "2000 FT!";
                gYoshiGameItems[42].jtext = NULL;
                gYoshiWindows[YW_SHUTTLE_MESSAGE].openSound = 0xD3;
                if (gGameMode & (MODE_LEVEL | MODE_MAP_VIEW)) {
                    yoshi_demand(YOSHI_DEMAND_OPEN | YW_SHUTTLE_MESSAGE);
                }
                break;
            case 10:
                gYoshiGameItems[42].text = "1000 FT!";
                gYoshiGameItems[42].jtext = NULL;
                gYoshiWindows[YW_SHUTTLE_MESSAGE].openSound = 0xD1;
                if (gGameMode & (MODE_LEVEL | MODE_MAP_VIEW)) {
                    yoshi_demand(YOSHI_DEMAND_OPEN | YW_SHUTTLE_MESSAGE);
                    music_push_danger_tune();
                }
                break;
        }
    }
}

/* Evaluates the shuttle path (0x8026A184).
 * Same as util_cam_path_eval with basis gShuttlePathBasis. */
f32 util_shuttle_path_eval(f32 x, f32 y, f32 z, f32 w, f32 a, f32 b, f32 c) {
    s32 i0;
    s32 i1;
    s32 i2;
    s32 i3;

    i0 = gShuttlePathBasis[0][0] * x + gShuttlePathBasis[1][0] * y + gShuttlePathBasis[2][0] * z + gShuttlePathBasis[3][0] * w;
    i1 = gShuttlePathBasis[0][1] * x + gShuttlePathBasis[1][1] * y + gShuttlePathBasis[2][1] * z + gShuttlePathBasis[3][1] * w;
    i2 = gShuttlePathBasis[0][2] * x + gShuttlePathBasis[1][2] * y + gShuttlePathBasis[2][2] * z + gShuttlePathBasis[3][2] * w;
    i3 = gShuttlePathBasis[0][3] * x + gShuttlePathBasis[1][3] * y + gShuttlePathBasis[2][3] * z + gShuttlePathBasis[3][3] * w;
    return i0 * c + b * i1 + a * i2 + i3;
}

/* *angle into (ref - 180, ref + 180] (0x8026A2E8).
 * Wraps *angle into (ref - 180, ref + 180]. */
void util_unwrap_angle(f32 ref, f32 *angle) {
    f32 d = *angle - ref;

    if (d > 180.0) {
        *angle -= 360.0;
    } else if (d < -180.0) {
        *angle += 360.0;
    }
}

/* Decimal string, no leading zeros (0x8026A378).
 * Writes n as decimal digits (no leading zeros, at most 9 digits) plus a NUL. */
void util_itoa(s32 n, u8 *buf) {
    s32 div;
    u8 digit;
    u8 printed;
    u8 started;

    div = 100000000;
    printed = 0;
    started = 0;
    do {
        digit = n / div;
        if (started || digit) {
            *buf++ = digit + '0';
            started = printed = 1;
        }
        n -= digit * div;
        div /= 10;
    } while (div != 0);
    if (!printed) {
        *buf++ = '0';
    }
    *buf = 0;
}

/* A matrix: z rotation, then y, about (x, y, z) (0x8026A454).
 * Rotation about the point (x, y, z): z axis by rx, then y axis by ry (units of 1/11.375 degree). */
void util_rotate_about_point(s16 x, s16 y, s16 z, s16 rx, s16 ry, Mtx *m) {
    f32 a[4][4];
    f32 b[4][4];

    guTranslateF(a, -x, -y, -z);
    guRotateF(b, (f32) rx / 11.375, 0.0f, 0.0f, 1.0f);
    guMtxCatF(a, b, a);
    guRotateF(b, (f32) ry / 11.375, 0.0f, 1.0f, 0.0f);
    guMtxCatF(a, b, a);
    guTranslateF(b, x, y, z);
    guMtxCatF(a, b, a);
    guMtxF2L(a, m);
}

/* Copies size bytes as u64s (0x8026A5CC).
 * Copies size bytes in 8-byte units. */
void util_copy64(u64 *dst, u64 *src, s32 size) {
    s32 unused;

    size >>= 3;
    while (size--) {
        *dst++ = *src++;
    }
}

/* 2-D distance (64-bit squares, then sqrtf) (0x8026A610).
 * 2D distance (the squares summed in 64 bits, then sqrtf). */
s32 util_dist2d(s32 x1, s32 y1, s32 x2, s32 y2) {
    s64 dx;
    s64 dy;

    dx = x1 - x2, dy = y1 - y2;
    dx = dx * dx;
    dy = dy * dy;
    return sqrtf(dx + dy);
}

/* 3-D distance (64-bit squares, then sqrtf) (0x8026A6F0).
 * 3D distance (the squares summed in 64 bits, then sqrtf). */
s32 util_dist3d(s32 x1, s32 y1, s32 z1, s32 x2, s32 y2, s32 z2) {
    s64 dx;
    s64 dy;
    s64 dz;

    dx = x1 - x2, dy = y1 - y2, dz = z1 - z2;
    dx = dx * dx;
    dy = dy * dy;
    dz = dz * dz;
    return sqrtf(dx + dy + dz);
}

/* LCG random number in [lo, hi] (0x8026A828).
 * LCG random integer in [lo, hi], rounded. */
s32 util_rand_range(s32 lo, s32 hi) {
    f32 f;

    gRandSeed = PORT_WRAP_ADD(PORT_WRAP_MUL(gRandSeed, 0x41C64E6D), 0x3039);
    f = (gRandSeed & 0x7FFFFFFF) / 2147483648.0f;
    f = (hi - lo) * f + lo;
    return f + 0.5;
}

/* Seeds util_rand_range from the CPU counter (0x8026A8BC). */
void util_seed_rand(void) {
    gRandSeed = osGetCount();
}

/* The second, fixed-seed generator (0x8026A8E0).
 * Same with the second seed. */
s32 util_rand2_range(s32 lo, s32 hi) {
    f32 f;

    gRandSeed2 = PORT_WRAP_ADD(PORT_WRAP_MUL(gRandSeed2, 0x41C64E6D), 0x3039);
    f = (gRandSeed2 & 0x7FFFFFFF) / 2147483648.0f;
    f = (hi - lo) * f + lo;
    return f + 0.5;
}

/* The fixed seed of util_rand2_range (0x8026A974). */
void util_seed_rand2(void) {
    gRandSeed2 = 0x9BA0D;
}

/* Clears the hint-event latches (0x8026A988). */
void util_reset_hint_events(void) {
    gHintNearVehicle = 0;
    gHintBuildingHit = 0;
    gHintLastHitObject = 0;
    gHintPrevVehicle = 0;
    gHintLastVehicle = 0;
}

/* Per frame: hint windows near other vehicles and at level spots (0x8026A9B4).
 * Per-frame event checks: proximity to vehicles, level-specific triggers (yoshi_show_hint). */
void util_check_hint_events(void) {
    VehRecord *p;
    u8 done;
    s32 minDist;
    s32 d;

    minDist = 999999;
    if (gTrainContactFrames == 15) {
        yoshi_show_hint(YW_HINT_TRAIN);
    }
    if (levelno == 0x12 && (gPlayerX >> 5) < 0x578
        && util_dist2d(gPlayerX >> 5, gPlayerZ >> 5, 0x531, 0x1061) < 0x82) {
        yoshi_show_hint(YW_HINT_LEVEL12_SPOT);
    }
    if (gCurVehicle != gHintLastVehicle) {
        gHintPrevVehicle = gHintLastVehicle;
    }
    if (gCurVehicle) {
        gHintLastVehicle = gCurVehicle;
    }
    p = gVehicles;
    done = 0;
    while (!done && gVehiclesEnd != p) {
        if (p->type != gHintPrevVehicle && p->type != gCurVehicle && gCurVehicle != VEH_DRIVER && p->type != VEH_DRIVER
            && p->type != VEH_CHOPPER && p->type != VEH_CARRIER && p->type != VEH_TRAIN && p->type != VEH_CRANE) {
            d = util_dist3d(gPlayerX >> 5, gPlayerY >> 5, gPlayerZ >> 5, p->x >> 5, p->y >> 5,
                              p->z >> 5);
            if (d < minDist) {
                minDist = d;
            }
            if (d < 100 && !gHintNearVehicle && (!veh_is_riding(p->type) || !gTrainClosed)) {
                yoshi_show_hint(YW_HINT_OTHER_VEHICLE);
                gHintNearVehicle = 1;
                done = 1;
            }
        }
        p++;
    }
    if (gHintNearVehicle && minDist > 400) {
        gHintNearVehicle = 0;
    }
    if (levelno == 0 && gMarkerArrowsOnScreen >= 2) {
        yoshi_show_hint(YW_HINT_MARKER_ARROWS);
    }
    if (levelno == 0 && gCurVehicle == VEH_TRAIN && (gPlayerX >> 5) >= 0x899) {
        yoshi_show_hint(YW_HINT_LEVEL0_TRAIN);
    }
    if (gHintBuildingHit) {
        if (!((players[playerNumber].medal[levelno] > 0 && players[playerNumber].medal[levelno] < 6) ? 1 : 0)
            && !gTargetsCleared) {
            if (!yoshi_show_hint(YW_HINT_BUILDING_HIT)) {
                yoshi_show_hint(YW_HINT_BUILDING_HIT2);
            }
            gHintBuildingHit = 0;
        }
    }
}
