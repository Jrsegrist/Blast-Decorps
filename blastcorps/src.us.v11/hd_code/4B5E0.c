#include "common.h"
#include <ultra64.h>
#ifndef NON_MATCHING /* legacy declarations */
/* Matching build: IDO compiled the matched code here against older
 * declarations of these, which the file keeps; the NON_MATCHING build
 * uses game/game.h's. */
#define LEGACY_gSfxBank
#define LEGACY_gDynTrisEnd
#endif /* legacy declarations */
#include "game/game.h"
/* Views: this file reads these shared variables (game/variables.h) as other types. */
#define seg2Base ((u8 *) seg2Base)
#define gBoxTargets ((BoxTarget *) gBoxTargets)
#ifdef NON_MATCHING
#define gDynTrisEnd (*(s32 *) &gDynTrisEnd)
#endif
/* end of views */

/* The push blocks (gBlocks, gBlockCount 0x38-byte entries; positions in 1/32
 * units): the player pushes them along (speed, heading); near a free slot of
 * their model they latch onto it, glide over it and bounce down into it, then
 * stay settled there (and fill the slot's target box). */
typedef struct {
    /* 0x00 */ s32 x;
    /* 0x04 */ s32 y;
    /* 0x08 */ s32 z;
    /* 0x0C */ s16 speed;      /* pushed: coll_step_xz's step, -8 a frame */
    /* 0x0E */ s16 heading;    /* 0..0xFFF */
    /* 0x10 */ u8 model;       /* index into gBlockModels */
    /* 0x11 */ u8 settled;     /* in its slot for good */
    /* 0x12 */ u8 latched;     /* gliding to / bouncing into gBlockSlots[slot] */
    /* 0x13 */ u8 slot;        /* which gBlockSlots entry */
    /* 0x14 */ s32 bounceVel;  /* the bounce: h = bounceVel * t - 4 t^2 */
    /* 0x18 */ s32 bounceTime;
    /* 0x1C */ s32 bounceBase; /* height the bounce starts from */
    /* 0x20 */ u8 unk20;       /* cleared at spawn, never read */
    /* 0x21 */ u8 pad21[3];
    /* 0x24 */ s32 slotDist;   /* x/z distance to the slot: the glide stops when it grows */
    /* 0x28 */ u8 groundId;    /* gGroundTriId under it: the vehicle (moveable) it stands on, 0 = the ground */
    /* 0x29 */ u8 onVehicle;   /* rides that vehicle (block_attach_to_vehicle; gBlock0Riding is gBlocks[0]'s) */
    /* 0x2A */ s16 deckX;      /* its place on the vehicle's deck (veh_deck_to_local_c) */
    /* 0x2C */ s16 deckZ;
    /* 0x2E */ u8 pad2E[2];
    /* 0x30 */ u32 tex;        /* texture (KSEG0 address, tex_get) */
    /* 0x34 */ s32 scrapeSound; /* the looping SFX_SCRAPE_LOOP while it moves (sound handle) */
} PushBlock;

/* The slots the blocks go into (gBlockSlots, gBlockSlotCount 0x1C-byte entries):
 * a place for a block of one model; its collision triangles are switched off
 * once the block has settled in it. */
typedef struct {
    /* 0x00 */ s32 x;
    /* 0x04 */ s32 y;
    /* 0x08 */ s32 z;
    /* 0x0C */ s32 floor;     /* y - the model's sinkDepth: where a block comes to rest */
    /* 0x10 */ u8 model;      /* index into gBlockModels */
    /* 0x11 */ u8 taken;      /* a block has latched onto it */
    /* 0x14 */ s32 trisStart; /* its gDynTris range (coll_add_dyn_tris; coll_disable_tris) */
    /* 0x18 */ s32 trisEnd;
} BlockSlot;

/* (The target boxes, gBoxTargets: BoxTarget, game/types.h.) */

/* gBlockModels: 0x290-byte models: a display list, then texture size and
 * a radius. */
typedef struct {
    /* 0x000 */ Gfx dl[0x50];
    /* 0x280 */ s16 texId;      /* texture id for tex_get */
    /* 0x282 */ u8 width;
    /* 0x283 */ u8 height;
    /* 0x284 */ s16 radius;
    /* 0x286 */ s16 latchRange; /* a block within this of a free slot latches onto it */
    /* 0x288 */ s32 sinkDepth;  /* how far below the slot's y the block ends up */
    /* 0x28C */ u8 pad28C[4];
} BlockModel;

/* Per-frame dynamic buffer: one matrix per entry at 0xB00. */
typedef struct Dyn4B5E0 {
    /* 0x000 */ u8 pad[0xB00];
    /* 0xB00 */ Mtx mtx[1];
} Dyn4B5E0;

extern PushBlock gBlocks[];
extern u8 gBlock0Riding;
extern s32 gBlockCount;
extern BlockSlot gBlockSlots[];
extern s32 gBlockSlotCount;
extern s32 gBlockPrevPlayerX;
extern s32 gBlockPrevPlayerY;
extern s32 gBlockPrevPlayerZ;
extern u8 gBlockPrevVehicle;
extern s32 gBlockPrevCarrierX;
extern s32 gBlockPrevCarrierY;
extern s32 gBlockPrevCarrierZ;
#ifndef NON_MATCHING
extern s32 gDynTrisEnd;
#endif
extern BlockModel gBlockModels[];
extern s16 gBlockPushSpeedMax[];
#ifndef NON_MATCHING
extern s32 gSfxBank;
#endif

void block_fill_target(s32);

/* Load this file's objects from level data: reset the counters, then
 * read gBlockCount 0x38-byte entries (s16 x, y, z, model; positions are
 * stored scaled by 32, height snapped by coll_ground_height, texture looked
 * up by tex_get), then gBlockSlotCount 0x1C-byte records (s16 x, y, z,
 * u8 model, u8 n, s16 flag, then n 22-byte items for coll_add_dyn_tris).
 * Flagged records also get a gBoxTargets box (+-40 units).
 * NON_MATCHING: the hand-written coll_add_dyn_tris reads registers this function
 * never sets for it; the NM build passes what they hold here: v0 = the last
 * call's result (coll_reset_dyn_tris leaves &gDynTris, tex_get its result;
 * coll_add_dyn_tris leaves v0 alone), t6 = &gBlockSlots[i], t9 = the old
 * gDynTrisEnd, and s1 = the caller's s1 (this function never touches the
 * s-registers), which the NM build takes as an extra parameter from
 * initlevel (5CB60.c). */
#ifdef NON_MATCHING
/* The push blocks, their slots, the target boxes (0x8028FDA0). */
void block_spawn_all(u8 *arg0, u8 *arg1, s32 s1) {
    s32 v0 = (s32) gDynTris;
#else
void block_spawn_all(u8 *arg0, u8 *arg1) {
#endif
    s32 i;

    gBlockCount = 0;
    gBlockSlotCount = 0;
    gBoxTargetCount = 0;
    gBlockPrevPlayerX = 0;
    gBlockPrevPlayerY = 0;
    gBlockPrevPlayerZ = 0;
    gBlockPrevCarrierX = 0;
    gBlockPrevCarrierY = 0;
    gBlockPrevCarrierZ = 0;
    gBlockPrevVehicle = 0;
    gVehSlowed = 0;
    coll_reset_dyn_tris();
    if (arg0 != arg1) {
        gBlockCount = *(s16 *) arg0;
        arg0 += 2;
        for (i = 0; i < gBlockCount; i++) {
            gBlocks[i].x = *(s16 *) (arg0 + 0) << 5;
            gBlocks[i].y = *(s16 *) (arg0 + 2) << 5;
            gBlocks[i].z = *(s16 *) (arg0 + 4) << 5;
            gBlocks[i].model = *(s16 *) (arg0 + 6);
            arg0 += 8;
            gBlocks[i].y = coll_ground_height(gBlocks[i].x, gBlocks[i].z, gBlocks[i].y);
            gBlocks[i].heading = 0;
            gBlocks[i].speed = 0;
            gBlocks[i].settled = 0;
            gBlocks[i].latched = 0;
            gBlocks[i].unk20 = 0;
            gBlocks[i].bounceVel = 0;
            gBlocks[i].bounceTime = 0;
            gBlocks[i].onVehicle = 0;
            gBlocks[i].tex = tex_get(gBlockModels[gBlocks[i].model].texId, 0);
#ifdef NON_MATCHING
            v0 = (s32) gBlocks[i].tex;
#endif
        }
        gBlockSlotCount = *(s16 *) arg0;
        arg0 += 2;
        for (i = 0; i < gBlockSlotCount; i++) {
            gBlockSlots[i].x = *(s16 *) (arg0 + 0) << 5;
            gBlockSlots[i].y = *(s16 *) (arg0 + 2) << 5;
            gBlockSlots[i].z = *(s16 *) (arg0 + 4) << 5;
            gBlockSlots[i].model = arg0[6];
            gBlockSlots[i].floor = gBlockSlots[i].y - gBlockModels[gBlockSlots[i].model].sinkDepth;
            gBlockSlots[i].taken = 0;
            gBlockSlots[i].trisStart = gDynTrisEnd;
#ifdef NON_MATCHING
            coll_add_dyn_tris(arg0 + 10, arg0[7], gBlockSlots[i].model, v0, (s32) &gBlockSlots[i],
                          (s32) gBlockSlots[i].trisStart, s1);
#else
            coll_add_dyn_tris(arg0 + 10, arg0[7], gBlockSlots[i].model);
#endif
            gBlockSlots[i].trisEnd = gDynTrisEnd;
            if (*(s16 *) (arg0 + 8) != 0) {
                gBoxTargets[gBoxTargetCount].x = gBlockSlots[i].x;
                gBoxTargets[gBoxTargetCount].y = gBlockSlots[i].y;
                gBoxTargets[gBoxTargetCount].z = gBlockSlots[i].z;
                gBoxTargets[gBoxTargetCount].y0 = gBlockSlots[i].y >> 5;
                gBoxTargets[gBoxTargetCount].y1 = gBlockSlots[i].y >> 5;
                gBoxTargets[gBoxTargetCount].y2 = gBlockSlots[i].y >> 5;
                gBoxTargets[gBoxTargetCount].y3 = gBlockSlots[i].y >> 5;
                gBoxTargets[gBoxTargetCount].x0 = (gBlockSlots[i].x >> 5) - 40;
                gBoxTargets[gBoxTargetCount].z0 = (gBlockSlots[i].z >> 5) - 40;
                gBoxTargets[gBoxTargetCount].x1 = (gBlockSlots[i].x >> 5) + 40;
                gBoxTargets[gBoxTargetCount].z1 = (gBlockSlots[i].z >> 5) - 40;
                gBoxTargets[gBoxTargetCount].x2 = (gBlockSlots[i].x >> 5) - 40;
                gBoxTargets[gBoxTargetCount].z2 = (gBlockSlots[i].z >> 5) + 40;
                gBoxTargets[gBoxTargetCount].x3 = (gBlockSlots[i].x >> 5) + 40;
                gBoxTargets[gBoxTargetCount].z3 = (gBlockSlots[i].z >> 5) + 40;
                gBoxTargets[gBoxTargetCount].filled = 0;
                gBoxTargets[gBoxTargetCount].slot = i;
                gBoxTargetCount++;
            }
            arg0 = arg0[7] * 22 + arg0 + 10;
        }
    }
}

/* Push, settle into a slot, bounce (0x802906C0).
 * Per-frame update of the gBlocks entries (arg0 = current mode).
 * Free entries with speed are pushed along by coll_step_xz and
 * may latch onto a free gBlockSlots slot of their model within range; hitting one
 * (coll_probe_vehicles) resets the speed from the player's movement since last
 * frame (capped by gBlockPushSpeedMax[arg0]); then collision, a heading update
 * and a slow-down. Latched entries glide toward their slot, then bounce
 * into it (h = v*t - 4t^2) until the bounce dies out. Also keeps a looping
 * sound (scrapeSound) alive while an entry moves. The two-in-one assignments
 * below are deliberate: separate statements schedule differently. */
void block_update(u8 arg0) {
    s32 i;
    s32 j;
    u8 hit;
    s32 dx;
    s32 dy;
    s32 dz;
    s16 old10;
    s16 old12;
    u8 moved;
    s32 dist;
    s32 step;
    s32 h;
    s32 hprev;
    s32 diff;
    u8 drop;
    u8 found;
    s32 tx;
    s32 tz;
    s32 floor;
    u8 near;

    for (i = 0; i < gBlockCount; i++) {
        if (gBlocks[i].settled == 0 && gBlocks[i].latched == 0) {
            if (arg0 == 0) {
                gBlocks[i].speed = 0;
            }
            if (gBlocks[i].speed != 0 && radar_has_marker(i) == 0) {
                coll_step_xz(gBlocks[i].x, gBlocks[i].z, gBlocks[i].speed, gBlocks[i].heading);
                gBlocks[i].x = gCollStepX;
                gBlocks[i].z = gCollStepZ;
                gBlocks[i].y = coll_ground_height(gBlocks[i].x, gBlocks[i].z, gBlocks[i].y);
                gBlocks[i].groundId = gGroundTriId;
                found = 0;
                j = 0;
                while (j < gBlockSlotCount && !found) {
                    if (gBlocks[i].model == gBlockSlots[j].model && gBlocks[i].groundId == 0 &&
                        gBlockSlots[j].taken == 0) {
                        dx = (gBlocks[i].x - gBlockSlots[j].x) >> 5;
                        dy = (gBlocks[i].y - gBlockSlots[j].y) >> 5;
                        dz = (gBlocks[i].z - gBlockSlots[j].z) >> 5;
                        dist = sqrtf(dx * dx + dy * dy + dz * dz);
                        if (dist <= gBlockModels[gBlocks[i].model].latchRange) {
                            gBlocks[i].slotDist = util_dist2d(gBlocks[i].x, gBlocks[i].z,
                                                                gBlockSlots[j].x, gBlockSlots[j].z);
                            gBlocks[i].bounceBase = gBlocks[i].y;
                            gBlocks[i].latched = 1;
                            gBlocks[i].slot = j;
                            gBlockSlots[j].taken = 1;
                            found = 1;
                        }
                    }
                    j++;
                }
            }
            coll_begin_probe(gBlocks[i].x, gBlocks[i].y, gBlocks[i].z);
            hit = coll_probe_vehicles(gBlockModels[gBlocks[i].model].radius);
            if (hit != 0) {
                if (gProbeHitCarrier == 0 && gProbeHitPlayer != 0) {
                    gVehSlowed = 1;
                }
                near = gPlayerSpeed != 0 || arg0 == 9;
                if ((gBlockPrevVehicle == arg0 && near) || gProbeHitCarrier != 0) {
                    if (gProbeHitCarrier == 0) {
                        dx = gPlayerX - gBlockPrevPlayerX,
                        dy = gPlayerY - gBlockPrevPlayerY,
                        dz = gPlayerZ - gBlockPrevPlayerZ;
                    } else {
                        dx = gCarrierX - gBlockPrevCarrierX,
                        dy = gCarrierY - gBlockPrevCarrierY,
                        dz = gCarrierZ - gBlockPrevCarrierZ;
                    }
                    gBlocks[i].speed = sqrtf(dx * dx + dy * dy + dz * dz) + 10.0f;
                    if (gBlocks[i].speed > gBlockPushSpeedMax[arg0]) {
                        gBlocks[i].speed = gBlockPushSpeedMax[arg0];
                    }
                } else {
                    gBlocks[i].speed = 0;
                }
            }
            if (gBlocks[i].speed != 0) {
                old10 = gFreeArcStart, old12 = gFreeArcEnd;
                moved = 0;
                coll_probe_tris(gBlocks[i].x, gBlocks[i].y, gBlocks[i].z,
                              gBlockModels[gBlocks[i].model].radius, 200, gBlocks[i].model);
                coll_probe_objects(gBlockModels[gBlocks[i].model].radius, 0);
                if (old10 != gFreeArcStart || old12 != gFreeArcEnd) {
                    moved = 1;
                }
            } else {
                moved = 0;
            }
            drop = 0;
            if (gFreeArcStart != 0 || gFreeArcEnd != 0xFFF) {
                if (veh_free_arc_length() < 300) {
                    drop = 1;
                } else if (moved) {
                    gBlocks[i].heading = coll_heading_in_ring(gBlocks[i].heading);
                } else {
                    gBlocks[i].heading = veh_free_arc_mid();
                }
            }
            if ((drop || arg0 == 0) && gBlocks[i].latched == 0) {
                radar_set_marker(i, gBlocks[i].x, gBlocks[i].y, gBlocks[i].z,
                              gBlockModels[gBlocks[i].model].radius);
                gBlocks[i].speed = 0;
            } else {
                radar_remove_marker(i);
            }
            if (gBlocks[i].speed > 0) {
                gBlocks[i].speed -= 8;
            } else {
                gBlocks[i].speed = 0;
            }
        }
        if (gBlocks[i].latched != 0) {
            tx = gBlockSlots[gBlocks[i].slot].x;
            tz = gBlockSlots[gBlocks[i].slot].z;
            if (gBlocks[i].x != tx || gBlocks[i].z != tz) {
                dist = util_dist2d(gBlocks[i].x, gBlocks[i].z, tx, tz);
                if (dist != 0) {
                    dx = tx - gBlocks[i].x;
                    if (dx > 0) {
                        step = dx;
                    } else {
                        step = -dx;
                    }
                    step <<= 10;
                    step /= dist;
                    step *= gBlocks[i].speed;
                    step >>= 10;
                    if (dx < 0) {
                        step = -step;
                    }
                    gBlocks[i].x += step;
                    dz = tz - gBlocks[i].z;
                    if (dz > 0) {
                        step = dz;
                    } else {
                        step = -dz;
                    }
                    step <<= 10;
                    step /= dist;
                    step *= gBlocks[i].speed;
                    step >>= 10;
                    if (dz < 0) {
                        step = -step;
                    }
                    gBlocks[i].z += step;
                    dist = util_dist2d(gBlocks[i].x, gBlocks[i].z, tx, tz);
                    if (dist >= gBlocks[i].slotDist) {
                        gBlocks[i].x = tx;
                        gBlocks[i].z = tz;
                    } else {
                        gBlocks[i].slotDist = dist;
                    }
                }
            } else {
                if (gBlocks[i].y == gBlocks[i].bounceBase) {
                    snd_play(gSfxBank, SFX_BLOCK_FALL, NULL);
                }
                floor = gBlockSlots[gBlocks[i].slot].floor;
                h = gBlocks[i].bounceVel * gBlocks[i].bounceTime + (gBlocks[i].bounceTime * -4) * gBlocks[i].bounceTime;
                gBlocks[i].y = gBlocks[i].bounceBase + h;
                if (gBlocks[i].y < floor) {
                    gBlocks[i].y = floor;
                    hprev = gBlocks[i].bounceVel * (gBlocks[i].bounceTime - 1) + ((gBlocks[i].bounceTime - 1) * -4) * (gBlocks[i].bounceTime - 1);
                    diff = h - hprev;
                    if (diff < 0) {
                        diff = -diff;
                    }
                    if (diff < 20) {
                        gBlocks[i].latched = 0;
                        gBlocks[i].settled = 1;
                        coll_disable_tris((u8 *) N64_IPTR((gBlockSlots[gBlocks[i].slot].trisStart)), (u8 *) N64_IPTR((gBlockSlots[gBlocks[i].slot].trisEnd)));
                        block_fill_target(gBlocks[i].slot);
                    } else {
                        gBlocks[i].bounceVel = diff >> 1;
                        gBlocks[i].bounceTime = 0;
                        gBlocks[i].bounceBase = floor;
                        snd_play(gSfxBank, SFX_THUD, NULL);
                    }
                } else {
                    gBlocks[i].bounceTime++;
                }
            }
        }
        if (gBlocks[i].speed != 0 && gBlocks[i].latched == 0 && gBlocks[i].settled == 0 &&
            gBlocks[i].scrapeSound == 0) {
            snd_play(gSfxBank, SFX_SCRAPE_LOOP, &gBlocks[i].scrapeSound);
        }
        if (gBlocks[i].scrapeSound != 0 &&
            (gBlocks[i].speed == 0 || gBlocks[i].latched != 0 || gBlocks[i].settled != 0)) {
            snd_stop((void *) N64_IPTR((gBlocks[i].scrapeSound)));
        }
    }
    gBlockPrevPlayerX = gPlayerX;
    gBlockPrevPlayerY = gPlayerY;
    gBlockPrevPlayerZ = gPlayerZ;
    gBlockPrevVehicle = arg0;
    gBlockPrevCarrierX = gCarrierX;
    gBlockPrevCarrierY = gCarrierY;
    gBlockPrevCarrierZ = gCarrierZ;
}

/* Marks the slot's target box done (0x80291724).
 * Sets filled in the first gBoxTargets entry whose slot is arg0. */
void block_fill_target(s32 arg0) {
    u8 found;
    s32 i;

    found = 0;
    for (i = 0; i < gBoxTargetCount;) {
        if (gBoxTargets[i].slot == arg0) {
            gBoxTargets[i].filled = 1;
            found = 1;
        } else {
            i++;
        }
        if (found) {
            break;
        }
    }
}

/* Draws the push blocks (0x802917B0).
 * Draw every entry: a translated, textured copy of its model. */
void block_draw(Gfx **gdl, Dyn4B5E0 *dyn) {
    Gfx *gfx;
    s32 i;

    gfx = *gdl;
    if (gBlockCount > 0) {
        gDPPipeSync(gfx++);
        gDPSetCycleType(gfx++, G_CYC_1CYCLE);
        gDPSetRenderMode(gfx++, G_RM_AA_ZB_OPA_SURF, G_RM_AA_ZB_OPA_SURF2);
        gDPSetCombineMode(gfx++, G_CC_DECALRGBA, G_CC_DECALRGBA);
        gSPClearGeometryMode(gfx++, 0xFFFFFFFF);
        gSPSetGeometryMode(gfx++, G_ZBUFFER | G_SHADE | G_SHADING_SMOOTH | G_CULL_BACK);
        gSPTexture(gfx++, 0xFFFF, 0xFFFF, 0, G_TX_RENDERTILE, G_ON);
    }
    for (i = 0; i < gBlockCount; i++) {
        guTranslate(&dyn->mtx[i], gBlocks[i].x / 32.0f, gBlocks[i].y / 32.0f,
                    gBlocks[i].z / 32.0f);
        gSPMatrix(gfx++, i * sizeof(Mtx) + 0xB00 + (u32) seg2Base, G_MTX_MODELVIEW | G_MTX_MUL | G_MTX_PUSH);
        gDPLoadTextureBlock(gfx++, gBlocks[i].tex - 0x80000000, G_IM_FMT_RGBA, G_IM_SIZ_16b,
                            gBlockModels[gBlocks[i].model].width, gBlockModels[gBlocks[i].model].height, 0,
                            G_TX_MIRROR | G_TX_CLAMP, G_TX_MIRROR | G_TX_CLAMP, G_TX_NOMASK, G_TX_NOMASK,
                            G_TX_NOLOD, G_TX_NOLOD);
        gSPDisplayList(gfx++, osVirtualToPhysical(&gBlockModels[gBlocks[i].model]));
        gSPPopMatrix(gfx++, G_MTX_MODELVIEW);
    }
    gDPPipeSync(gfx++);
    *gdl = gfx;
}

/* Blocks standing on a vehicle ride on it (0x80291ED8).
 * For entries standing on vehicle arg0 (groundId): clear speed, set onVehicle and call
 * veh_deck_to_local_c on their position. */
void block_attach_to_vehicle(u8 arg0) {
    s32 i;

    for (i = 0; i < gBlockCount; i++) {
        if (gBlocks[i].groundId == arg0) {
            gBlocks[i].speed = 0;
            *(&gBlock0Riding + i * 0x38) = 1;
            veh_deck_to_local_c(arg0, gBlocks[i].x, gBlocks[i].z, &gBlocks[i].deckX,
                          &gBlocks[i].deckZ);
        }
    }
}

/* Carried blocks move with the vehicle (0x80291FAC).
 * Update entries with onVehicle set: move them (veh_deck_to_world_c), then snap
 * their height with coll_ground_height. */
void block_ride_vehicle(u8 arg0) {
    s32 i;

    for (i = 0; i < gBlockCount; i++) {
        if (gBlocks[i].onVehicle != 0) {
            veh_deck_to_world_c(arg0, gBlocks[i].deckX, gBlocks[i].deckZ, (s32 *) &gBlocks[i],
                          &gBlocks[i].z);
            gBlocks[i].y = coll_ground_height(gBlocks[i].x, gBlocks[i].z, gBlocks[i].y);
        }
    }
}

/* Unpins all blocks (0x80292084).
 * Clear every entry's onVehicle. */
void block_detach_all(void) {
    s32 i;

    for (i = 0; i < gBlockCount; i++) {
        gBlocks[i].onVehicle = 0;
    }
}

/* GCollHit if a sphere touches a block (0x802920DC).
 * Set gCollHit if the point (arg0, arg1, arg2) is within arg3 of any
 * idle entry's model radius (all in 1/32 units). */
void block_test_sphere(s32 arg0, s32 arg1, s32 arg2, s32 arg3) {
    s32 i;
    s32 dist;

    arg0 >>= 5, arg1 >>= 5, arg2 >>= 5, arg3 >>= 5;
    for (i = 0; i < gBlockCount; i++) {
        if (gBlocks[i].settled == 0 && gBlocks[i].onVehicle == 0) {
            dist = util_dist3d(arg0, arg1, arg2, gBlocks[i].x >> 5, gBlocks[i].y >> 5,
                                 gBlocks[i].z >> 5);
            if (dist <= (gBlockModels[gBlocks[i].model].radius >> 5) + arg3) {
                gCollHit = 1;
            }
        }
    }
}
