#include "common.h"
#include <ultra64.h>
#ifndef NON_MATCHING /* legacy declarations */
/* Matching build: IDO compiled the matched code here against older
 * declarations of these, which the file keeps; the NON_MATCHING build
 * uses game/game.h's. */
#define LEGACY_gSfxBank
#endif /* legacy declarations */
#include "game/game.h"
/* Views: this file reads these shared variables (game/variables.h) as other types. */
#define seg2Base ((u8 *) seg2Base)
/* end of views */

/* The people: a pool of 20 little people who run out of a destroyed object
 * (people_spawn, from obj_spawn_people), wait inside a box, walk away from
 * the player and are picked up by the BCT chopper (type 0xFE; drawn only
 * while gHaveChopper); its record type was called Debris before. The audio
 * manager that follows is a separate file, 22EE0.c (audio.c). */

typedef struct {
    /* 0x00 */ s16 x;
    /* 0x02 */ s16 y;
    /* 0x04 */ s16 z;
    /* 0x06 */ s16 targetX; /* run-out target x */
    /* 0x08 */ s16 targetZ; /* run-out target z */
    /* 0x0A */ s16 homeX; /* home box centre x */
    /* 0x0C */ s16 homeZ; /* home box centre z */
    /* 0x0E */ s16 homeSize; /* home box half size */
    /* 0x10 */ s16 fleeSize; /* half size of the area where the player makes them flee */
    /* 0x12 */ u8 cell; /* the level grid cell (people_spawn's arg4; people_is_visible) */
    /* 0x13 */ u8 animFrame; /* the sprite's frame (6 idle, 8 walking) */
    /* 0x14 */ u8 animTick; /* frames (idle) or speed (walking) since the last frame step */
    /* 0x15 */ u8 state; /* 0 free, 5 running out, 1 idle, 4 walking away, 2 chosen for pickup, 3 boarding */
    /* 0x16 */ s16 headingOffset; /* random -500..500 added to the player's heading when fleeing */
    /* 0x18 */ s16 heading; /* 0..0xFFF */
    /* 0x1A */ u8 stepFwd; /* the last flee step went along the heading (1) or against it (0) */
    /* 0x1B */ u8 stepHold; /* frames the last choice is kept after a change (5) */
    /* 0x1C */ u8 speed; /* ramps toward gPlayerSpeed / 40 */
    /* 0x1D */ u8 pad1D[3];
    /* 0x20 */ f32 lastDist; /* last distance to the run-out target */
} Person; /* size 0x24 */

extern Person gPeople[20];
extern s16 gPeopleStepX;
extern s16 gPeopleStepZ;
extern s32 gChopperPickupTimer;
extern s32 gChopperPickupIndex;
extern s32 gChopperRetargetX;
#ifndef NON_MATCHING
extern s32 gSfxBank;
#endif

s32 people_player_near(s32 arg0);
void people_step(s32 arg0);

/* Empties the people pool; the released count from the status or 0 (0x80264C20).
 * Clear the pool and reset the timers */
void people_init(s32 arg0) {
    s32 i;

    for (i = 0; i < 20; i++) {
        gPeople[i].state = 0;
    }
    gRandSeed = osGetCount();
    gChopperPickupTimer = 99999999;
    if (arg0) {
        gScorePeople = gPeopleRestored;
    } else {
        gScorePeople = 0;
    }
}

/* N people run out of a destroyed object at (x, y, z) (0x80264CB4).
 * Spawn arg5 people around (arg0, arg2) inside a box of half size arg3 */
void people_spawn(s16 arg0, s16 arg1, s16 arg2, s16 arg3, u8 arg4, s32 arg5) {
    s32 pad;
    s32 n;
    s32 i;
    u8 found;
    s16 r;

    i = 0;
    n = 0;
    gScorePeople += arg5;
    if (arg5 != 0 && gPaused == 0) {
        yoshi_show_hint(YW_HINT_PEOPLE);
    }
    if (arg3 > 200) {
        arg3 = 200;
    }
    while (n < arg5 && i < 20) {
        if (n == 2) {
            snd_play(gSfxBank, SFX_PEOPLE_SCREAM, 0);
        }
        found = 0;
        while (i < 20 && !found) {
            if (gPeople[i].state == 0) {
                found = 1;
            } else {
                i++;
            }
        }
        if (found) {
            PORT_ISPAWN(PORT_ITAG_PEOPLE, i);
            gPeople[i].x = arg0;
            gPeople[i].z = arg2;
            gPeople[i].y = arg1;
            r = arg3 / 5;
            gPeople[i].homeX = util_rand2_range(-r, r) + (arg0 - arg3);
            gPeople[i].homeZ = util_rand2_range(-r, r) + (arg2 + arg3);
            gPeople[i].fleeSize = arg3 * 3 / 2;
            gPeople[i].homeSize = arg3;
            gPeople[i].targetX = gPeople[i].homeX + util_rand2_range(-arg3, arg3);
            gPeople[i].targetZ = gPeople[i].homeZ + util_rand2_range(-arg3, arg3);
            gPeople[i].animFrame = 0;
            gPeople[i].animTick = 0;
            gPeople[i].headingOffset = util_rand2_range(-500, 500);
            gPeople[i].cell = arg4;
            gPeople[i].speed = 0;
            gPeople[i].state = 5;
            gPeople[i].heading = 4000;
            gPeople[i].lastDist = 100000000.0f;
            gPeople[i].stepHold = 0;
        }
        i++, n++;
    }
}

void people_flee(void);
void people_run_out(void);
void people_dispatch_chopper(void);

/* Per frame: flee, run out, chopper pickup (0x8026510C). */
void people_update(void) {
    people_flee();
    people_run_out();
    people_dispatch_chopper();
}

/* People running out (state 5) reach their spot, then idle (0x8026513C).
 * Move homing debris (state 5) 5 units a frame toward its target; it lands
 * (state 1) once it arrives or starts moving away */
void people_run_out(void) {
    s16 dx;
    s16 dz;
    s16 adx;
    s16 adz;
    f32 dist;
    s16 sx;
    s16 sz;
    s32 i;

    for (i = 0; i < 20; i++) {
        if (gPeople[i].state == 5) {
            dx = gPeople[i].targetX - gPeople[i].x;
            if (dx >= 0) {
                adx = dx;
            } else {
                adx = -dx;
            }
            dz = gPeople[i].targetZ - gPeople[i].z;
            if (dz >= 0) {
                adz = dz;
            } else {
                adz = -dz;
            }
            dist = util_dist2d(gPeople[i].targetX, gPeople[i].targetZ, gPeople[i].x, gPeople[i].z);
            if (dist < 1.0 || gPeople[i].lastDist < dist) {
                gPeople[i].state = 1;
            } else {
                gPeople[i].lastDist = dist;
                sx = adx / dist * 5.0f;
                sz = adz / dist * 5.0f;
                if (dx >= 0) {
                    gPeople[i].x = gPeople[i].x + sx;
                } else {
                    gPeople[i].x -= sx;
                }
                if (dz >= 0) {
                    gPeople[i].z += sz;
                } else {
                    gPeople[i].z -= sz;
                }
            }
        }
    }
}

/* While the player is near, a person walks away from the player (0x80265428).
 * Wander: step each active piece along its heading, clamped to its box,
 * preferring the direction away from the player */
void people_flee(void) {
    s32 i;
    s32 pad;
    s32 x;
    s32 z;
    s32 x2;
    s32 z2;
    s32 d1;
    s32 d2;

    for (i = 0; i < 20; i++) {
        if ((gPeople[i].state == 1 || gPeople[i].state == 4 || gPeople[i].state == 2) && people_player_near(i)) {
            people_step(i);
            x = gPeople[i].x + gPeopleStepX;
            z = gPeople[i].z + gPeopleStepZ;
            x2 = gPeople[i].x - gPeopleStepX;
            z2 = gPeople[i].z - gPeopleStepZ;
            if (x < gPeople[i].homeX - gPeople[i].homeSize) {
                x = gPeople[i].homeX - gPeople[i].homeSize;
            }
            if (z < gPeople[i].homeZ - gPeople[i].homeSize) {
                z = gPeople[i].homeZ - gPeople[i].homeSize;
            }
            if (x >= gPeople[i].homeX + gPeople[i].homeSize) {
                x = gPeople[i].homeX + gPeople[i].homeSize - 1;
            }
            if (z >= gPeople[i].homeZ + gPeople[i].homeSize) {
                z = gPeople[i].homeZ + gPeople[i].homeSize - 1;
            }
            if (x2 < gPeople[i].homeX - gPeople[i].homeSize) {
                x2 = gPeople[i].homeX - gPeople[i].homeSize;
            }
            if (z2 < gPeople[i].homeZ - gPeople[i].homeSize) {
                z2 = gPeople[i].homeZ - gPeople[i].homeSize;
            }
            if (x2 >= gPeople[i].homeX + gPeople[i].homeSize) {
                x2 = gPeople[i].homeX + gPeople[i].homeSize - 1;
            }
            if (z2 >= gPeople[i].homeZ + gPeople[i].homeSize) {
                z2 = gPeople[i].homeZ + gPeople[i].homeSize - 1;
            }
            d1 = util_dist2d(gPlayerX >> 5, gPlayerZ >> 5, x, z);
            d2 = util_dist2d(gPlayerX >> 5, gPlayerZ >> 5, x2, z2);
            if (gPeople[i].stepHold) {
                if (gPeople[i].stepFwd) {
                    d2 = 0;
                } else {
                    d1 = 0;
                }
                gPeople[i].stepHold--;
            }
            if (d1 < d2) {
                gPeople[i].x = x2;
                gPeople[i].z = z2;
                gPeople[i].heading += 0x800;
                if (gPeople[i].heading >= 0x1000) {
                    gPeople[i].heading -= 0xFFF;
                }
                if (gPeople[i].stepFwd) {
                    gPeople[i].stepHold = 5;
                }
                gPeople[i].stepFwd = 0;
            } else {
                gPeople[i].x = x;
                gPeople[i].z = z;
                if (!gPeople[i].stepFwd) {
                    gPeople[i].stepHold = 5;
                }
                gPeople[i].stepFwd = 1;
            }
            if (i == 1 && gPeople[i].state != 4) {
                snd_play(gSfxBank, SFX_PEOPLE_SCREAM, 0);
            }
            if (gPeople[i].state == 2) {
                gChopperMode = 6;
            }
            gPeople[i].state = 4;
        } else if (gPeople[i].state == 4) {
            gPeople[i].state = 1;
        }
    }
}

/* 1 if the player is inside person i's area (0x80265A0C).
 * Is the player (gPlayerX/Z, >> 5) inside piece arg0's area? */
s32 people_player_near(s32 arg0) {
    s16 b3;
    s16 b2;
    s16 b1;
    s16 b0;

    b1 = gPeople[arg0].homeX - gPeople[arg0].fleeSize;
    b0 = gPeople[arg0].homeZ - gPeople[arg0].fleeSize;
    b3 = gPlayerX >> 5, b2 = gPlayerZ >> 5;
    if (b3 < b1 || b2 < b0) {
        return 0;
    }
    b1 = gPeople[arg0].homeX + gPeople[arg0].fleeSize;
    b0 = gPeople[arg0].homeZ + gPeople[arg0].fleeSize;
    if (b3 >= b1 || b2 >= b0) {
        return 0;
    }
    return 1;
}

/* Person i's heading and this frame's step (0x80265B7C).
 * Turn piece arg0 and work out this frame's step (gPeopleStepX/Z) from its
 * heading and a speed that ramps toward gPlayerSpeed / 40 */
void people_step(s32 arg0) {
    s16 ang;
    s16 t;
    s16 s;
    s16 c;
    s16 vx;
    s16 vz;
    s32 lim;

    lim = gPlayerSpeed / 40;
    if (lim < 0) {
        lim = -lim;
    }
    if (gPlayerSpeed != 0 && lim == 0) {
        lim = 2;
    }
    ang = gPeople[arg0].headingOffset + gPlayerMoveHeading + 0x400;
    if (ang >= 0x1000) {
        ang -= 0xFFF;
    }
    gPeople[arg0].heading = ang;
    t = ang % 0x400;
    s = sins(t * 0xFFFF / 0xFFF);
    c = coss(t * 0xFFFF / 0xFFF);
    if (gPeople[arg0].speed < lim) {
        gPeople[arg0].speed++;
    }
    if (gPeople[arg0].speed > lim) {
        gPeople[arg0].speed--;
    }
    vx = gPeople[arg0].speed * s >> 15;
    vz = gPeople[arg0].speed * c >> 15;
    if (ang < 0x400) {
        gPeopleStepX = vx;
        gPeopleStepZ = vz;
    }
    if (ang >= 0x400 && ang < 0x800) {
        gPeopleStepX = vz;
        gPeopleStepZ = -vx;
    }
    if (ang >= 0x800 && ang < 0xC00) {
        gPeopleStepX = -vx;
        gPeopleStepZ = -vz;
    }
    if (ang >= 0xC00) {
        gPeopleStepX = -vz;
        gPeopleStepZ = vx;
    }
}

/* Chopper pickup: boarding, and the next person to fetch (0x80265E48).
 * When the chopper has landed (gChopperTouchdown) the chosen person boards; when it
 * is free (gChopperMode == 0) it is sent to the idle person nearest it, with a
 * sound. Rare's slip: the "farthest person" choice for `forced` is dead code,
 * since the timeout that sets forced also sets gChopperMode = 6 in the same call.
 * The first three initialisers share a line (as1 schedules by source line). */
void people_dispatch_chopper(void) {
    s32 i;
    s32 minIdx;
    s32 minDist;
    s32 maxIdx;
    s32 maxDist;
    s32 d;
    u8 found;
    u8 forced;
    s32 vol;

    minIdx = -1; minDist = 99999999; maxIdx = -1;
    maxDist = 0;
    forced = 0;
    if (gChopperTouchdown) {
        i = 0;
        found = 0;
        while (!found) {
            if (gPeople[i].state == 2) {
                gPeople[i].state = 3;
                gPeople[i].animFrame = 0;
                found = 1;
            } else {
                i++;
            }
        }
        gChopperTouchdown = 0;
        gChopperPickupTimer = 99999999;
    } else {
        gChopperPickupTimer--;
        if (gChopperPickupTimer == 0) {
            gChopperMode = 6;
            gPeople[gChopperPickupIndex].state = 1;
            forced = 1;
        }
    }
    if (gChopperMode == 0) {
        for (i = 0; i < 20; i++) {
            if (gPeople[i].state == 3) {
                gPeople[i].state = 0;
            }
        }
        for (i = 0; i < 20; i++) {
            if (gPeople[i].state == 1) {
                d = util_dist2d(gChopperX, gChopperZ, gPeople[i].x << 5, gPeople[i].z << 5);
                if (d < minDist) {
                    minIdx = i;
                    minDist = d;
                }
                if (d > maxDist) {
                    maxIdx = i;
                    maxDist = d;
                }
            }
        }
        if (minIdx != -1) {
            if (forced) {
                i = maxIdx;
            } else {
                i = minIdx;
            }
            gChopperTargetX = gPeople[i].x << 5;
            gChopperTargetZ = gPeople[i].z << 5;
            gChopperLandY = gPeople[i].y << 5;
            gPeople[i].state = 2;
            gChopperPickupTimer = 600;
            gChopperMode = 1;
            gChopperPickupIndex = i;
            if (35000 - util_dist2d(gPlayerX, gPlayerZ, gChopperTargetX, gChopperTargetZ) * 2 >= 0x8000) {
                vol = 0x7FFF;
            } else {
                vol = 35000 - util_dist2d(gPlayerX, gPlayerZ, gChopperTargetX, gChopperTargetZ) * 2;
            }
            if (vol > 4000) {
                snd_modify(snd_play(gSfxBank, SFX_CHOPPER_DISPATCH, 0), 8, vol);
            }
        }
    }
}

/* Mode 0x800 start: the chopper heads for the carrier (0x802661EC).
 * The chopper's target becomes the carrier's x/z; the player's x/z are kept
 * for the retarget (72B80.c). */
void chopper_fly_to_carrier(void) {
    gChopperTargetX = gCarrierX;
    gChopperTargetZ = gCarrierZ;
    gChopperRetargetX = gPlayerX;
    gChopperRetargetZ = gPlayerZ;
    gChopperMode = 1;
    gChopperPickupTimer = 2000;
}

/* Draw the people: billboarded 20x32 RGBA16 sprites, animated and picked by
 * heading, plus one marker at gChopperDropX */
typedef struct DynBuf {
    Mtx mtx[100];
    Vtx vtx[1];
} DynBuf;

extern Person gPeople[20];
extern Vtx gPeopleQuadVtx[4];
extern u16 gPeopleMarkerTex[];
extern u16 gPeopleIdleTex0[], gPeopleIdleTex1[], gPeopleIdleTex2[], gPeopleIdleTex3[], gPeopleIdleTex4[], gPeopleIdleTex5[];
extern u16 gPeopleWalkATex0[], gPeopleWalkATex1[], gPeopleWalkATex2[], gPeopleWalkATex3[], gPeopleWalkATex4[], gPeopleWalkATex5[], gPeopleWalkATex6[], gPeopleWalkATex7[];
extern u16 gPeopleWalkBTex0[], gPeopleWalkBTex1[], gPeopleWalkBTex2[], gPeopleWalkBTex3[], gPeopleWalkBTex4[], gPeopleWalkBTex5[], gPeopleWalkBTex6[], gPeopleWalkBTex7[];
extern u16 gPeopleWalkSideTex0[], gPeopleWalkSideTex1[], gPeopleWalkSideTex2[], gPeopleWalkSideTex3[], gPeopleWalkSideTex4[], gPeopleWalkSideTex5[], gPeopleWalkSideTex6[], gPeopleWalkSideTex7[];

s32 people_is_visible();

#define LOAD_TEX(ptr) \
    gDPPipeSync(gdl++); \
    gDPLoadTextureBlock(gdl++, ptr, G_IM_FMT_RGBA, G_IM_SIZ_16b, 20, 32, 0, G_TX_CLAMP, G_TX_CLAMP, \
                        G_TX_NOMASK, G_TX_NOMASK, G_TX_NOLOD, G_TX_NOLOD)

/* The people as 20x32 billboards, and the one boarding (0x80266248). */
void people_draw(Gfx **gdlp, DynBuf *buf) {
    Gfx *gdl;
    s32 n;
    s32 i;
    s32 k;
    s32 j;
    u8 found;
    u8 frame;
    u16 *tex;
    u32 phys;
    u8 flip;
    f32 mf[4][4];
    f32 fx[4];
    f32 fy[4];
    f32 fz[4];
    s16 ang;

    gdl = *gdlp;
    n = 0;
    guRotateF(mf, gCamHeading - 135.0, 0.0f, 1.0f, 0.0f);
    for (k = 0; k < 4; k++) {
        guMtxXFMF(mf, gPeopleQuadVtx[k].v.ob[0], gPeopleQuadVtx[k].v.ob[1], gPeopleQuadVtx[k].v.ob[2], &fx[k], &fy[k], &fz[k]);
    }
    gSPClearGeometryMode(gdl++, 0xFFFFFFFF);
    gSPSetGeometryMode(gdl++, G_ZBUFFER | G_SHADE | G_SHADING_SMOOTH);
    gDPPipeSync(gdl++);
    gDPSetCycleType(gdl++, G_CYC_1CYCLE);
    gDPSetRenderMode(gdl++, G_RM_ZB_XLU_SURF, G_RM_ZB_XLU_SURF2);
    gDPSetCombineMode(gdl++, G_CC_MODULATERGBA, G_CC_MODULATERGBA);
    gSPTexture(gdl++, 0xFFFF, 0xFFFF, 0, G_TX_RENDERTILE, G_ON);
    for (i = 0; i < 20; i++) {
        if (people_is_visible(&gPeople[i]) &&
            (gPeople[i].state == 1 || gPeople[i].state == 2 || (gPeople[i].state == 4 && gPeople[i].speed == 0))) {
            gPeople[i].animTick++;
            if (gPeople[i].animTick >= 6) {
                gPeople[i].animFrame++;
                gPeople[i].animTick = 0;
            }
            if (gPeople[i].animFrame >= 6) {
                gPeople[i].animFrame = 0;
            }
            switch (gPeople[i].animFrame) {
                case 0: tex = gPeopleIdleTex0; break;
                case 1: tex = gPeopleIdleTex1; break;
                case 2: tex = gPeopleIdleTex2; break;
                case 3: tex = gPeopleIdleTex3; break;
                case 4: tex = gPeopleIdleTex4; break;
                case 5: tex = gPeopleIdleTex5; break;
            }
            phys = osVirtualToPhysical(tex);
            PORT_ITAG(gdl, PORT_ITAG_PEOPLE, i);
            LOAD_TEX(phys);
            util_copy64((u64 *) (&buf->vtx[n]), (u64 *) gPeopleQuadVtx, sizeof(Vtx) * 4);
            for (j = 0; j < 4; j++) {
                buf->vtx[n + j].v.ob[0] = fx[j] + gPeople[i].x;
                buf->vtx[n + j].v.ob[1] = fy[j] + gPeople[i].y;
                buf->vtx[n + j].v.ob[2] = fz[j] + gPeople[i].z;
            }
            gSPVertex(gdl++, n * sizeof(Vtx) + 0x1900 + (u32) seg2Base, 4, 0);
            n += 4;
            gSP1Triangle(gdl++, 0, 1, 2, 0);
            gSP1Triangle(gdl++, 0, 2, 3, 0);
        }
    }
    for (i = 0; i < 20; i++) {
        if (((gPeople[i].state == 4 && gPeople[i].speed != 0) || gPeople[i].state == 5) &&
            people_is_visible(&gPeople[i])) {
            gPeople[i].animTick += gPeople[i].speed;
            if (gPeople[i].animTick >= 11) {
                gPeople[i].animFrame++;
                gPeople[i].animTick = 0;
            }
            if (gPeople[i].animFrame >= 8) {
                gPeople[i].animFrame = 0;
            }
            frame = gPeople[i].animFrame;
            flip = 0;
            ang = gPeople[i].heading - (gCamHeading - 135.0) / 360.0 * 4095.0;
            if (ang < 0) {
                ang += 0xFFF;
            }
            if (ang >= 0xC00) {
                switch (frame) {
                    case 0: tex = gPeopleWalkBTex0; break;
                    case 1: tex = gPeopleWalkBTex1; break;
                    case 2: tex = gPeopleWalkBTex2; break;
                    case 3: tex = gPeopleWalkBTex3; break;
                    case 4: tex = gPeopleWalkBTex4; break;
                    case 5: tex = gPeopleWalkBTex5; break;
                    case 6: tex = gPeopleWalkBTex6; break;
                    case 7: tex = gPeopleWalkBTex7; break;
                }
            }
            if (ang >= 0x800 && ang < 0xC00) {
                flip = 1;
                switch (frame) {
                    case 0: tex = gPeopleWalkSideTex0; break;
                    case 1: tex = gPeopleWalkSideTex1; break;
                    case 2: tex = gPeopleWalkSideTex2; break;
                    case 3: tex = gPeopleWalkSideTex3; break;
                    case 4: tex = gPeopleWalkSideTex4; break;
                    case 5: tex = gPeopleWalkSideTex5; break;
                    case 6: tex = gPeopleWalkSideTex6; break;
                    case 7: tex = gPeopleWalkSideTex7; break;
                }
            }
            if (ang >= 0x400 && ang < 0x800) {
                switch (frame) {
                    case 0: tex = gPeopleWalkATex0; break;
                    case 1: tex = gPeopleWalkATex1; break;
                    case 2: tex = gPeopleWalkATex2; break;
                    case 3: tex = gPeopleWalkATex3; break;
                    case 4: tex = gPeopleWalkATex4; break;
                    case 5: tex = gPeopleWalkATex5; break;
                    case 6: tex = gPeopleWalkATex6; break;
                    case 7: tex = gPeopleWalkATex7; break;
                }
            }
            if (ang < 0x400) {
                switch (frame) {
                    case 0: tex = gPeopleWalkSideTex0; break;
                    case 1: tex = gPeopleWalkSideTex1; break;
                    case 2: tex = gPeopleWalkSideTex2; break;
                    case 3: tex = gPeopleWalkSideTex3; break;
                    case 4: tex = gPeopleWalkSideTex4; break;
                    case 5: tex = gPeopleWalkSideTex5; break;
                    case 6: tex = gPeopleWalkSideTex6; break;
                    case 7: tex = gPeopleWalkSideTex7; break;
                }
            }
            phys = osVirtualToPhysical(tex);
            PORT_ITAG(gdl, PORT_ITAG_PEOPLE, i);
            LOAD_TEX(phys);
            util_copy64((u64 *) (&buf->vtx[n]), (u64 *) gPeopleQuadVtx, sizeof(Vtx) * 4);
            if (flip) {
                buf->vtx[n].v.tc[0] = 0x260;
                buf->vtx[n + 1].v.tc[0] = 0;
                buf->vtx[n + 2].v.tc[0] = 0;
                buf->vtx[n + 3].v.tc[0] = 0x260;
            }
            for (j = 0; j < 4; j++) {
                buf->vtx[n + j].v.ob[0] = fx[j] + gPeople[i].x;
                buf->vtx[n + j].v.ob[1] = fy[j] + gPeople[i].y;
                buf->vtx[n + j].v.ob[2] = fz[j] + gPeople[i].z;
            }
            gSPVertex(gdl++, n * sizeof(Vtx) + 0x1900 + (u32) seg2Base, 4, 0);
            n += 4;
            gSP1Triangle(gdl++, 0, 1, 2, 0);
            gSP1Triangle(gdl++, 0, 2, 3, 0);
        }
    }
    if (gChopperLiftAnimDone == 0) {
        i = 0;
        found = 0;
        while (i < 20 && !found) {
            if (gPeople[i].state == 3) {
                found = 1;
                PORT_ITAG(gdl, PORT_ITAG_MARKER, 0);
                LOAD_TEX(osVirtualToPhysical(gPeopleMarkerTex));
                util_copy64((u64 *) (&buf->vtx[n]), (u64 *) gPeopleQuadVtx, sizeof(Vtx) * 4);
                for (j = 0; j < 4; j++) {
                    buf->vtx[n + j].v.ob[0] = fx[j];
                    buf->vtx[n + j].v.ob[1] = fy[j];
                    buf->vtx[n + j].v.ob[2] = fz[j];
                }
                guTranslate(&buf->mtx[8], gChopperDropX / 32.0f, gChopperDropY / 32.0f, gChopperDropZ / 32.0f);
                gSPMatrix(gdl++, 8 * sizeof(Mtx) + (u32) seg2Base, G_MTX_MODELVIEW | G_MTX_MUL | G_MTX_PUSH);
                gSPVertex(gdl++, n * sizeof(Vtx) + 0x1900 + (u32) seg2Base, 4, 0);
                n += 4;
                gSP1Triangle(gdl++, 0, 1, 2, 0);
                gSP1Triangle(gdl++, 0, 2, 3, 0);
                gSPPopMatrix(gdl++, G_MTX_MODELVIEW);
            } else {
                i++;
            }
        }
    }
    PORT_ITAG(gdl, PORT_ITAG_NONE, 0);
    gDPPipeSync(gdl++);
    *gdlp = gdl;
}

/* 1 if the person's grid cell is in gVisibleCells (0x80267614).
 * Is the node's cell in the 0xFFFF-terminated list gVisibleCells? (same as
 * 26570's rdu_is_visible with the cell at 0x12) */
s32 people_is_visible(Person *node, s32 arg1) {
    s32 i;
    u8 v;
    s32 pad;

    i = 0;
    v = node->cell;
    for (; gVisibleCells[i] != 0xFFFF; ) {
        arg1 = gVisibleCells[i++] == v;
        if (arg1) {
            return 1;
        }
    }
    return 0;
}
