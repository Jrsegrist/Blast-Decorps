#include "common.h"
#include <ultra64.h>
#ifndef NON_MATCHING /* legacy declarations */
/* Matching build: IDO compiled the matched code here against older
 * declarations of these, which the file keeps; the NON_MATCHING build
 * uses game/game.h's. */
#define LEGACY_gHeapPtr
#define LEGACY_gSfxBank
#endif /* legacy declarations */
#include "game/game.h"
/* Views: this file reads these shared variables (game/variables.h) as other types. */
#ifdef NON_MATCHING
#define gHeapPtr (*(Vtx * N64P *) &gHeapPtr)
#endif
/* end of views */

/* Collectible boxes: each is an axis-aligned textured cube (8 vertices) built
 * from a per-type table, collected when the player comes within range, then
 * faded out. */

/* Per-type box description, at 0x802FDB40. */
typedef struct {
    s16 x1;
    s16 x0;
    s16 y0;
    s16 y1;
    s16 z0;
    s16 z1;
    s16 tex0Id;
    s16 tex1Id;
    s16 radarSize; /* passed to radar_set_marker */
    s16 radius;    /* collect distance */
    u8 trigger;    /* compared with crate_update's argument */
    u8 pad15;
} BoxType; /* size 0x16 */

/* Input placement record. */
typedef struct BoxSpawn {
    s16 x;
    s16 y;
    s16 z;
    s16 type;
} BoxSpawn; /* size 0x8 */

typedef struct {
    s16 x;
    s16 y;
    s16 z;
    u8 type;
    u8 collected;
    s16 alpha;
    s16 padA;
    s32 tex0;
    s32 tex1;
    Vtx * N64P vtx;
} Box; /* size 0x18 */

/* Vtx texture coordinates; a macro in the original (the two stores come out
 * in comma-expression order). */
#define SET_TC(vtx, s, t) ((vtx).v.tc[0] = (s), (vtx).v.tc[1] = (t))

extern BoxType gCrateTypes[];
#ifndef NON_MATCHING
extern Vtx *gHeapPtr;
#ifndef NON_MATCHING
extern s32 gSfxBank;
#endif
#endif
extern Box gCrates[];
extern s32 gCrateCount;


void crate_build_vertices(Vtx *v, u8 type, s16 x, s16 y, s16 z);

#ifdef PORT_HOST
/* The boxes' vertices (fresh heap memory) get ob and tc only: flag and
 * colour bytes are whatever the heap held on the N64.  port_vtx_stale
 * (port/src/load/swap.c) gives the native records those bytes first. */
void port_vtx_stale(void *v, u32 n);
#endif

/* The level's ammo crates (0x8028C190).
 * Spawn boxes from the records [arg0, arg1). */
void crate_spawn_all(BoxSpawn *arg0, BoxSpawn *arg1) {
    gCrateCount = 0;
    while (arg0 != arg1) {
        gCrates[gCrateCount].x = arg0->x;
        gCrates[gCrateCount].y = arg0->y;
        gCrates[gCrateCount].z = arg0->z;
        gCrates[gCrateCount].type = arg0->type;
        gCrates[gCrateCount].collected = 0;
        gCrates[gCrateCount].alpha = 0xFF;
        gCrates[gCrateCount].tex0 = tex_get(gCrateTypes[gCrates[gCrateCount].type].tex0Id, 0);
        gCrates[gCrateCount].tex1 = tex_get(gCrateTypes[gCrates[gCrateCount].type].tex1Id, 0);
        gCrates[gCrateCount].vtx = gHeapPtr;
        gHeapPtr += 8;
#ifdef PORT_HOST
        port_vtx_stale(gCrates[gCrateCount].vtx, 8);
#endif
        crate_build_vertices(gCrates[gCrateCount].vtx, gCrates[gCrateCount].type, gCrates[gCrateCount].x,
                      gCrates[gCrateCount].y, gCrates[gCrateCount].z);
        gCrateCount++;
        arg0++;
    }
}

/* A crate's vertices (0x8028C41C).
 * Fill the 8 cube vertices of a box of the given type at (x, y, z). */
void crate_build_vertices(Vtx *v, u8 type, s16 x, s16 y, s16 z) {
    v[0].v.ob[0] = gCrateTypes[type].x0 + x;
    v[0].v.ob[1] = gCrateTypes[type].y0 + y;
    v[0].v.ob[2] = gCrateTypes[type].z0 + z;
    SET_TC(v[0], 0, 0);
    v[1].v.ob[0] = gCrateTypes[type].x0 + x;
    v[1].v.ob[1] = gCrateTypes[type].y1 + y;
    v[1].v.ob[2] = gCrateTypes[type].z0 + z;
    SET_TC(v[1], 0, 0x1E0);
    v[2].v.ob[0] = gCrateTypes[type].x0 + x;
    v[2].v.ob[1] = gCrateTypes[type].y1 + y;
    v[2].v.ob[2] = gCrateTypes[type].z1 + z;
    SET_TC(v[2], 0x1E0, 0x1E0);
    v[3].v.ob[0] = gCrateTypes[type].x0 + x;
    v[3].v.ob[1] = gCrateTypes[type].y0 + y;
    v[3].v.ob[2] = gCrateTypes[type].z1 + z;
    SET_TC(v[3], 0x1E0, 0);
    v[4].v.ob[0] = gCrateTypes[type].x1 + x;
    v[4].v.ob[1] = gCrateTypes[type].y0 + y;
    v[4].v.ob[2] = gCrateTypes[type].z0 + z;
    SET_TC(v[4], 0x1E0, 0);
    v[5].v.ob[0] = gCrateTypes[type].x1 + x;
    v[5].v.ob[1] = gCrateTypes[type].y1 + y;
    v[5].v.ob[2] = gCrateTypes[type].z0 + z;
    SET_TC(v[5], 0x1E0, 0x1E0);
    v[6].v.ob[0] = gCrateTypes[type].x1 + x;
    v[6].v.ob[1] = gCrateTypes[type].y1 + y;
    v[6].v.ob[2] = gCrateTypes[type].z1 + z;
    SET_TC(v[6], 0, 0x1E0);
    v[7].v.ob[0] = gCrateTypes[type].x1 + x;
    v[7].v.ob[1] = gCrateTypes[type].y0 + y;
    v[7].v.ob[2] = gCrateTypes[type].z1 + z;
    SET_TC(v[7], 0, 0);
}

/* Pickups (+10 ammo by type), radar marks (0x8028C874).
 * Per-frame update: collect boxes of the given trigger class that are within
 * range, mark the rest on the radar, and fade out collected ones. */
void crate_update(u8 trigger) {
    s32 i;
    s32 dist;

    for (i = 0; i < gCrateCount; i++) {
        if (gCrates[i].collected == 0) {
            if (gCrateTypes[gCrates[i].type].trigger == trigger) {
                radar_remove_marker(i + 0x10000);
                dist = util_dist3d(gPlayerX >> 5, gPlayerY >> 5, gPlayerZ >> 5, gCrates[i].x,
                                     gCrates[i].y, gCrates[i].z);
                if (dist <= gCrateTypes[gCrates[i].type].radius) {
                    gCrates[i].collected = 1;
                    snd_play(gSfxBank, SFX_CRATE_PICKUP, 0);
                    switch (gCrates[i].type) {
                        case 0:
                            gBallistaMissiles += 10;
                            break;
                        case 1:
                            gSideswipeRamCharges += 10;
                            break;
                    }
                }
            } else {
                radar_set_marker(i + 0x10000, gCrates[i].x << 5, gCrates[i].y << 5,
                              gCrates[i].z << 5, gCrateTypes[gCrates[i].type].radarSize);
            }
        } else {
            gCrates[i].alpha -= 20;
            if (gCrates[i].alpha < 0) {
                gCrates[i].alpha = 0;
            }
        }
    }
}

/* Draws the crates (0x8028CB30).
 * Draw every box that is uncollected or still fading. */
void crate_draw(Gfx **arg0, s32 arg1) {
    Gfx *gfx;
    s32 i;
    u8 renderMode;

    gfx = *arg0;
    renderMode = 0;
    gDPPipeSync(gfx++);
    gDPSetCycleType(gfx++, G_CYC_1CYCLE);
    gDPSetCombine(gfx++, 0xFFFFFF, 0xFFFCF67B);
    gSPClearGeometryMode(gfx++, 0xFFFFFFFF);
    gSPSetGeometryMode(gfx++, G_ZBUFFER | G_SHADE | G_SHADING_SMOOTH | G_CULL_BACK);
    gSPTexture(gfx++, 0xFFFF, 0xFFFF, 0, G_TX_RENDERTILE, G_ON);
    for (i = 0; i < gCrateCount; i++) {
        if (gCrates[i].collected == 0 || gCrates[i].alpha != 0) {
            gDPPipeSync(gfx++);
            if (gCrates[i].collected == 0 && (renderMode == 0 || renderMode == 2)) {
                gDPSetRenderMode(gfx++, G_RM_AA_ZB_OPA_SURF, G_RM_AA_ZB_OPA_SURF2);
                renderMode = 1;
            }
            if (gCrates[i].collected != 0 && (renderMode == 0 || renderMode == 1)) {
                gDPSetRenderMode(gfx++, G_RM_AA_ZB_XLU_SURF, G_RM_AA_ZB_XLU_SURF2);
                renderMode = 2;
            }
            gDPSetPrimColor(gfx++, 0, 0, 0, 0, 0, gCrates[i].alpha);
            gSPVertex(gfx++, osVirtualToPhysical(gCrates[i].vtx), 8, 0);
            gDPLoadTextureBlock(gfx++, (u32) gCrates[i].tex0 - 0x80000000, G_IM_FMT_RGBA, G_IM_SIZ_16b, 16, 16, 0,
                                G_TX_CLAMP, G_TX_CLAMP, G_TX_NOMASK, G_TX_NOMASK, G_TX_NOLOD, G_TX_NOLOD);
            gSP1Triangle(gfx++, 6, 3, 2, 0);
            gSP1Triangle(gfx++, 6, 7, 3, 0);
            gSP1Triangle(gfx++, 4, 1, 0, 0);
            gSP1Triangle(gfx++, 1, 4, 5, 0);
            gSPModifyVertex(gfx++, 0, G_MWO_POINT_ST, 0x01E00000);
            gSPModifyVertex(gfx++, 1, G_MWO_POINT_ST, 0x01E001E0);
            gSPModifyVertex(gfx++, 2, G_MWO_POINT_ST, 0x000001E0);
            gSPModifyVertex(gfx++, 3, G_MWO_POINT_ST, 0x00000000);
            gSP1Triangle(gfx++, 0, 1, 2, 0);
            gSP1Triangle(gfx++, 0, 2, 3, 0);
            gSPModifyVertex(gfx++, 7, G_MWO_POINT_ST, 0x01E00000);
            gSPModifyVertex(gfx++, 6, G_MWO_POINT_ST, 0x01E001E0);
            gSPModifyVertex(gfx++, 5, G_MWO_POINT_ST, 0x000001E0);
            gSPModifyVertex(gfx++, 4, G_MWO_POINT_ST, 0x00000000);
            gSP1Triangle(gfx++, 7, 5, 4, 0);
            gSP1Triangle(gfx++, 7, 6, 5, 0);
            gDPLoadTextureBlock(gfx++, (u32) gCrates[i].tex1 - 0x80000000, G_IM_FMT_RGBA, G_IM_SIZ_16b, 16, 16, 0,
                                G_TX_CLAMP, G_TX_CLAMP, G_TX_NOMASK, G_TX_NOMASK, G_TX_NOLOD, G_TX_NOLOD);
            gSPModifyVertex(gfx++, 2, G_MWO_POINT_ST, 0x00000000);
            gSPModifyVertex(gfx++, 1, G_MWO_POINT_ST, 0x01E00000);
            gSPModifyVertex(gfx++, 5, G_MWO_POINT_ST, 0x01E001E0);
            gSPModifyVertex(gfx++, 6, G_MWO_POINT_ST, 0x000001E0);
            gSP1Triangle(gfx++, 5, 2, 1, 0);
            gSP1Triangle(gfx++, 2, 5, 6, 0);
        }
    }
    gDPPipeSync(gfx++);
    *arg0 = gfx;
}
