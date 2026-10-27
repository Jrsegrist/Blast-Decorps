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
#define seg2Base ((u8 *) seg2Base)
#ifdef NON_MATCHING
#define gHeapPtr (*(Vtx * N64P *) &gHeapPtr)
#endif
/* end of views */

/* gTntCrates: gTntCount 0x48-byte crates (gTntCrateList: this typed view of them). */
typedef struct {
    /* 0x00 */ s32 x;           /* position, 1/32 units (y: snapped to the ground) */
    /* 0x04 */ s32 y;
    /* 0x08 */ s32 z;
    /* 0x0C */ s16 heading;     /* the push direction (coll_step_xz) */
    /* 0x0E */ u8 type;         /* box type (gTntTypes) */
    /* 0x0F */ u8 pad0F;
    /* 0x10 */ s16 fuse;        /* frames left once lit (the record's fuse byte x 60) */
    /* 0x12 */ s16 fuseTotal;   /* the whole fuse (the throb speeds up as it burns) */
    /* 0x14 */ s32 litFrame;    /* gFramesInMode when a vehicle lit it; 0: not lit */
    /* 0x18 */ u8 live;         /* 0 once exploded */
    /* 0x19 */ u8 blastDelay;   /* 5 at the explosion; the blast probe runs when it counts down to 0 */
    /* 0x1A */ s16 blastRadius; /* coll_blast_at_probe's radius (0: none) */
    /* 0x1C */ s16 blastAmount; /*   and damage amount; also a pushed crate's hit amount */
    /* 0x1E */ s16 pushSpeed;   /* the player's speed + 8 when pushed (capped), slowing down */
    /* 0x20 */ s16 throb;       /* the lit crate's texture blend (prim LOD), 0..0xFF and back */
    /* 0x22 */ u8 throbDown;
    /* 0x23 */ u8 groundTri;    /* gGroundTriId under it: the vehicle deck it stands on */
    /* 0x24 */ u8 riding;       /* carried by a vehicle (tnt_attach_to_vehicle) */
    /* 0x25 */ u8 pad25;
    /* 0x26 */ s16 deckX;       /* its place on the deck (veh_deck_to_local_c) */
    /* 0x28 */ s16 deckZ;
    /* 0x2A */ s16 rotY;        /* 0..4095 = one turn */
    /* 0x2C */ u32 tex[4];      /* face textures */
    /* 0x3C */ Vtx * N64P vtx;  /* 8 corner vertices */
    /* 0x40 */ s32 tickSound;   /* the fuse's tick loop (SFX_TICK_LOOP) handle */
    /* 0x44 */ s32 scrapeSound; /* the push scrape loop (SFX_SCRAPE_LOOP) handle */
} TntCrate;
extern TntCrate gTntCrateList[];

/* gTntTypes: 0x18-byte box definitions (min/max extents, face textures
 * and two radii). */
typedef struct {
    /* 0x00 */ s16 x0, x1, y0, y1, z0, z1;
    /* 0x0C */ s16 tex[4];
    /* 0x14 */ s16 radius;       /* the vehicles' probe (coll_probe_vehicles), the radar marker, tnt_test_point */
    /* 0x16 */ s16 objectRadius; /* a pushed crate's probe of the objects (coll_probe_objects) */
} TntType;
extern TntType gTntTypeList[];
/* &gTntTypeList[0].radius as the raw address the matching code spells it
 * with (the table sits 0xB0 lower in US 1.0, after its shorter .text) */
#if VERSION_US_V10
#define TNT_RADIUS_ADDR 0x802FDAFC
#else
#define TNT_RADIUS_ADDR 0x802FDBAC
#endif

extern s32 gTntCount;
extern s32 gTntPrevPlayerX;
extern s32 gTntPrevPlayerY;
extern s32 gTntPrevPlayerZ;
extern u8 gTntPrevVehicle;
#ifndef NON_MATCHING
extern Vtx *gHeapPtr; /* vertex allocator */
#endif

void tnt_build_vertices(Vtx *v, u8 type);

#ifdef PORT_HOST
/* tnt_build_vertices writes ob and tc only: the boxes' flag and colour bytes are
 * whatever the heap held on the N64 (port_vtx_stale, port/src/load/swap.c). */
void port_vtx_stale(void *v, u32 n);
#endif

/* The level's TNT crates (0x8028D4C0).
 * Load the boxes from level data: 12-byte records (s16 x, y, z; u8 type,
 * u8 fuse in seconds; s16 blast radius, blast amount). Positions are scaled by
 * 32 and snapped to the ground; each box gets its face textures and 8 vertices. */
void tnt_spawn_all(u8 *rec, u8 *end) {
    gTntCount = 0;
    gTntPrevPlayerX = 0;
    gTntPrevPlayerY = 0;
    gTntPrevPlayerZ = 0;
    gTntPrevVehicle = 0;
    while (rec != end) {
        gTntCrateList[gTntCount].x = *(s16 *) (rec + 0) << 5;
        gTntCrateList[gTntCount].y = *(s16 *) (rec + 2) << 5;
        gTntCrateList[gTntCount].z = *(s16 *) (rec + 4) << 5;
        gTntCrateList[gTntCount].y = coll_ground_height(gTntCrateList[gTntCount].x,
                                                            gTntCrateList[gTntCount].z,
                                                            gTntCrateList[gTntCount].y);
        gTntCrateList[gTntCount].groundTri = gGroundTriId;
        gTntCrateList[gTntCount].type = rec[6];
        gTntCrateList[gTntCount].fuse = rec[7] * 60;
        gTntCrateList[gTntCount].fuseTotal = gTntCrateList[gTntCount].fuse;
        gTntCrateList[gTntCount].tex[0] =
            tex_get(gTntTypeList[gTntCrateList[gTntCount].type].tex[0], 0);
        gTntCrateList[gTntCount].tex[1] =
            tex_get(gTntTypeList[gTntCrateList[gTntCount].type].tex[1], 0);
        gTntCrateList[gTntCount].tex[2] =
            tex_get(gTntTypeList[gTntCrateList[gTntCount].type].tex[2], 0);
        gTntCrateList[gTntCount].tex[3] =
            tex_get(gTntTypeList[gTntCrateList[gTntCount].type].tex[3], 0);
        gTntCrateList[gTntCount].heading = 0;
        gTntCrateList[gTntCount].litFrame = 0;
        gTntCrateList[gTntCount].pushSpeed = 0;
        gTntCrateList[gTntCount].blastDelay = 0;
        gTntCrateList[gTntCount].blastRadius = *(s16 *) (rec + 8);
        gTntCrateList[gTntCount].blastAmount = *(s16 *) (rec + 10);
        gTntCrateList[gTntCount].throb = 0;
        gTntCrateList[gTntCount].throbDown = 0;
        gTntCrateList[gTntCount].riding = 0;
        gTntCrateList[gTntCount].deckX = 0;
        gTntCrateList[gTntCount].deckZ = 0;
        gTntCrateList[gTntCount].rotY = 0;
        gTntCrateList[gTntCount].live = 1;
        gTntCrateList[gTntCount].vtx = gHeapPtr;
        gHeapPtr += 8;
#ifdef PORT_HOST
        port_vtx_stale(gTntCrateList[gTntCount].vtx, 8);
#endif
        tnt_build_vertices(gTntCrateList[gTntCount].vtx, gTntCrateList[gTntCount].type);
        /* One statement (a for-loop increment, probably). */
        gTntCount++, rec += 12;
    }
}

/* A TNT crate's vertices (0x8028DA5C).
 * Fill the 8 corner Vtx of box type `type`, with texture coords 0 or 31.0
 * (0x3E0). Corners 2 and 5 need the comma form (probably a macro):
 * separate statements store the two tcs in the wrong order. */
void tnt_build_vertices(Vtx *v, u8 type) {
    v[0].v.ob[0] = gTntTypeList[type].x1;
    v[0].v.ob[1] = gTntTypeList[type].y0;
    v[0].v.ob[2] = gTntTypeList[type].z0;
    v[0].v.tc[1] = 0;
    v[0].v.tc[0] = 0;
    v[1].v.ob[0] = gTntTypeList[type].x1;
    v[1].v.ob[1] = gTntTypeList[type].y1;
    v[1].v.ob[2] = gTntTypeList[type].z0;
    v[1].v.tc[1] = 0x3E0;
    v[1].v.tc[0] = 0;
    v[2].v.ob[0] = gTntTypeList[type].x1;
    v[2].v.ob[1] = gTntTypeList[type].y1;
    v[2].v.ob[2] = gTntTypeList[type].z1;
    v[2].v.tc[0] = 0x3E0, v[2].v.tc[1] = 0x3E0;
    v[3].v.ob[0] = gTntTypeList[type].x1;
    v[3].v.ob[1] = gTntTypeList[type].y0;
    v[3].v.ob[2] = gTntTypeList[type].z1;
    v[3].v.tc[0] = 0x3E0;
    v[3].v.tc[1] = 0;
    v[4].v.ob[0] = gTntTypeList[type].x0;
    v[4].v.ob[1] = gTntTypeList[type].y0;
    v[4].v.ob[2] = gTntTypeList[type].z0;
    v[4].v.tc[0] = 0x3E0;
    v[4].v.tc[1] = 0;
    v[5].v.ob[0] = gTntTypeList[type].x0;
    v[5].v.ob[1] = gTntTypeList[type].y1;
    v[5].v.ob[2] = gTntTypeList[type].z0;
    v[5].v.tc[0] = 0x3E0, v[5].v.tc[1] = 0x3E0;
    v[6].v.ob[0] = gTntTypeList[type].x0;
    v[6].v.ob[1] = gTntTypeList[type].y1;
    v[6].v.ob[2] = gTntTypeList[type].z1;
    v[6].v.tc[1] = 0x3E0;
    v[6].v.tc[0] = 0;
    v[7].v.ob[0] = gTntTypeList[type].x0;
    v[7].v.ob[1] = gTntTypeList[type].y0;
    v[7].v.ob[2] = gTntTypeList[type].z1;
    v[7].v.tc[1] = 0;
    v[7].v.tc[0] = 0;
}

/* (tnt_explode) The mix of struct-field and per-field-symbol accesses below
 * is deliberate: it is the spelling whose schedule matches. */
extern u8 gTntCrates;
extern u8 gTntCrate0Live;
extern u8 gTntCrate0BlastDelay;
extern u8 gTntCrate0TickSnd;
extern u8 gTntCrate0ScrapeSnd;
#ifndef NON_MATCHING
extern s32 gSfxBank;
#endif

TntCrate *tnt_find_lit(void);

/* A TNT crate explodes (0x8028DD64). */
void tnt_explode(u8 crate) {
    TntCrate *match;

    coll_spawn_blast_fx(gTntCrateList[crate].x, gTntCrateList[crate].y, gTntCrateList[crate].z);
    *(&gTntCrate0BlastDelay + crate * 0x48) = 5;
    gTntCrateList[crate].live = 0;
    gShakeTime = 10;
    gShakeSize = 0x190;
    if (gTntCrateList[crate].tickSound != 0) {
        snd_stop((void *) N64_IPTR((gTntCrateList[crate].tickSound)));
        match = tnt_find_lit();
        if (match != NULL) {
            snd_play(gSfxBank, SFX_TICK_LOOP, (void *) N64_IPTR(((s32) &match->tickSound)));
        }
    }
    if (*(s32 *) (&gTntCrate0ScrapeSnd + crate * 0x48) != 0) {
        snd_stop((void *) N64_IPTR((*(s32 *) (&gTntCrate0ScrapeSnd + crate * 0x48))));
    }
    snd_play(gSfxBank, SFX_EXPLOSION, 0);
}

extern u8 gTntCrates;
extern s32 gTntCount;

/* The first live crate with a lit fuse, or NULL (0x8028DE94). */
TntCrate *tnt_find_lit(void) {
    s32 i;

    for (i = 0; i < gTntCount; i++) {
        if (gTntCrateList[i].live != 0 && gTntCrateList[i].litFrame != 0) {
            return &gTntCrateList[i];
        }
    }
    return NULL;
}

extern s16 gTntPushSpeedMax[]; /* speed cap per mode */


/* Fuses, throb, pushes, collisions, sounds (0x8028DF14).
 * Per-frame update of the boxes (vehicle = gCurVehicle, 0 on foot; the box
 * sibling of 4B5E0's block_update). Counts down blastDelay (then runs the
 * blast probe), burns the fuse and runs the throb, pushes moving boxes along
 * (coll_step_xz), sets their pushSpeed from the player's movement when hit,
 * handles collision, heading and slow-down, and keeps the box's scrape loop
 * (scrapeSound) alive while it moves. The two-in-one assignments are
 * deliberate: separate statements schedule differently. */
void tnt_update(u8 vehicle) {
    s32 i;
    u8 hit;
    s32 dx;
    s32 dy;
    s32 dz;
    s16 old10;
    s16 old12;
    u8 moved;
    f32 f;
    s16 local;
    u8 drop;

    for (i = 0; i < gTntCount; i++) {
        if (gTntCrateList[i].blastDelay != 0) {
            gTntCrateList[i].blastDelay--;
            if (gTntCrateList[i].blastDelay == 0) {
                gCollX = gTntCrateList[i].x;
                gCollY = gTntCrateList[i].y;
                gCollZ = gTntCrateList[i].z;
                if (gTntCrateList[i].blastRadius != 0) {
                    coll_blast_at_probe(gTntCrateList[i].blastAmount, gTntCrateList[i].blastRadius);
                }
            }
        }
        if (gTntCrateList[i].live != 0) {
            if (gTntCrateList[i].litFrame != 0 && gTntCrateList[i].fuse != 0) {
                gTntCrateList[i].fuse--;
                if (gTntCrateList[i].fuse <= 0) {
                    tnt_explode(i);
                }
            }
            if (gTntCrateList[i].litFrame != 0 && gTntCrateList[i].fuseTotal != 0) {
                f = (f32) gTntCrateList[i].fuse / (f32) gTntCrateList[i].fuseTotal;
                f = 1.0 - f;
                f = f * 50.0;
                if (gTntCrateList[i].throbDown != 0) {
                    gTntCrateList[i].throb -= (s16) f;
                    if (gTntCrateList[i].throb < 0) {
                        gTntCrateList[i].throb = -gTntCrateList[i].throb;
                        gTntCrateList[i].throbDown = 0;
                    }
                } else {
                    gTntCrateList[i].throb += (s16) f;
                    if (gTntCrateList[i].throb >= 0x100) {
                        gTntCrateList[i].throb = 0x1FE - gTntCrateList[i].throb;
                        gTntCrateList[i].throbDown = 1;
                    }
                }
            }
            if (vehicle == 0) {
                gTntCrateList[i].pushSpeed = 0;
            }
            if (gTntCrateList[i].pushSpeed != 0 && radar_has_marker(i + 0x100) == 0) {
                coll_step_xz(gTntCrateList[i].x, gTntCrateList[i].z, gTntCrateList[i].pushSpeed,
                              gTntCrateList[i].heading);
                gTntCrateList[i].x = gCollStepX;
                gTntCrateList[i].z = gCollStepZ;
                gTntCrateList[i].y = coll_ground_height(gTntCrateList[i].x, gTntCrateList[i].z,
                                                           gTntCrateList[i].y);
                gTntCrateList[i].groundTri = gGroundTriId;
            }
            coll_begin_probe(gTntCrateList[i].x, gTntCrateList[i].y, gTntCrateList[i].z);
            hit = coll_probe_vehicles(gTntTypeList[gTntCrateList[i].type].radius);
            if (hit != 0) {
                if (gProbeHitCarrier != 0) {
                    cmo_hit_request = 1;
                    tnt_explode(i);
                }
                if (gProbeHitPlayer != 0) {
                    gVehSlowed = 1;
                }
                if (gTntCrateList[i].litFrame == 0 && vehicle != 0) {
                    if (tnt_find_lit() == NULL) {
                        snd_play(gSfxBank, SFX_TICK_LOOP, (void *) N64_IPTR(((s32) &gTntCrateList[i].tickSound)));
                    }
                    gTntCrateList[i].litFrame = ((s32) gFramesInMode);
                }
                if (gTntPrevVehicle == vehicle) {
                    dx = gPlayerX - gTntPrevPlayerX, dy = gPlayerY - gTntPrevPlayerY;
                    dz = gPlayerZ - gTntPrevPlayerZ;
                    gTntCrateList[i].pushSpeed = sqrtf(dx * dx + dy * dy + dz * dz) + 8.0f;
                    if (gTntCrateList[i].pushSpeed > gTntPushSpeedMax[vehicle] && levelno != 0x22) {
                        gTntCrateList[i].pushSpeed = gTntPushSpeedMax[vehicle];
                    }
                } else {
                    gTntCrateList[i].pushSpeed = 0;
                }
            }
            if (gTntCrateList[i].pushSpeed != 0) {
                old10 = gFreeArcStart, old12 = gFreeArcEnd;
                moved = 0;
                coll_probe_tris(gTntCrateList[i].x, gTntCrateList[i].y, gTntCrateList[i].z,
                              gTntTypeList[gTntCrateList[i].type].radius, 201, 0);
                if (old10 != gFreeArcStart || old12 != gFreeArcEnd) {
                    moved = 1;
                }
                if (gTntCrateList[i].blastRadius != 0) {
                    local = 0;
                } else {
                    local = gTntCrateList[i].blastAmount;
                }
                if (coll_probe_objects(gTntTypeList[gTntCrateList[i].type].objectRadius, local) != 0) {
                    tnt_explode(i);
                }
            } else {
                moved = 0;
            }
            drop = 0;
            if (gFreeArcStart != 0 || gFreeArcEnd != 0xFFF) {
                if (veh_free_arc_length() < 100) {
                    drop = 1;
                } else if (moved) {
                    gTntCrateList[i].heading = coll_heading_in_ring(gTntCrateList[i].heading);
                } else {
                    gTntCrateList[i].heading = veh_free_arc_mid();
                }
            }
            if ((drop || vehicle == 0) && gTntCrateList[i].live != 0) {
                radar_set_marker(i + 0x100, gTntCrateList[i].x, gTntCrateList[i].y,
                              gTntCrateList[i].z, gTntTypeList[gTntCrateList[i].type].radius);
                gTntCrateList[i].pushSpeed = 0;
            } else {
                radar_remove_marker(i + 0x100);
            }
            if (gTntCrateList[i].pushSpeed > 0) {
                gTntCrateList[i].pushSpeed -= (levelno != 0x2B) ? 8 : 4;
            } else {
                gTntCrateList[i].pushSpeed = 0;
            }
            if (gTntCrateList[i].pushSpeed > 0 && gTntCrateList[i].scrapeSound == 0 &&
                gTntCrateList[i].live != 0) {
                snd_play(gSfxBank, SFX_SCRAPE_LOOP, (void *) N64_IPTR(((s32) &gTntCrateList[i].scrapeSound)));
                if (gCurVehicle == VEH_RAMDOZER) {
                    yoshi_show_hint(YW_HINT_RAMDOZER_PUSH);
                }
            }
            if (gTntCrateList[i].scrapeSound != 0 && gTntCrateList[i].pushSpeed == 0) {
                snd_stop((void *) N64_IPTR((gTntCrateList[i].scrapeSound)));
            }
        }
    }
    gTntPrevPlayerX = gPlayerX;
    gTntPrevPlayerY = gPlayerY;
    gTntPrevPlayerZ = gPlayerZ;
    gTntPrevVehicle = vehicle;
}


/* Per-frame dynamic buffer: one matrix per box at 0x600. */
typedef struct Dyn48D00 {
    /* 0x000 */ u8 pad[0x600];
    /* 0x600 */ Mtx mtx[1];
} Dyn48D00;

/* Load two 32x32 RGBA16 textures (a into tmem 0, b into 0x100) and set up
 * render tiles 0 and 1 for them (trilinear blend between the two). */
#define BOX_TEX_PAIR(g, a, b)                                                                              \
    gDPSetTextureImage(g++, G_IM_FMT_RGBA, G_IM_SIZ_16b, 1, (a) - 0x80000000);                              \
    gDPSetTile(g++, G_IM_FMT_RGBA, G_IM_SIZ_16b, 0, 0, G_TX_LOADTILE, 0, 0, 0, 0, 0, 0, 0);                  \
    gDPLoadSync(g++);                                                                                      \
    gDPLoadBlock(g++, G_TX_LOADTILE, 0, 0, 0x3FF, 0x100);                                                  \
    gDPSetTextureImage(g++, G_IM_FMT_RGBA, G_IM_SIZ_16b, 1, (b) - 0x80000000);                              \
    gDPTileSync(g++);                                                                                      \
    gDPSetTile(g++, G_IM_FMT_RGBA, G_IM_SIZ_16b, 0, 0x100, G_TX_LOADTILE, 0, 0, 0, 0, 0, 0, 0);              \
    gDPLoadSync(g++);                                                                                      \
    gDPLoadBlock(g++, G_TX_LOADTILE, 0, 0, 0x3FF, 0x100);                                                  \
    gDPSetTile(g++, G_IM_FMT_RGBA, G_IM_SIZ_16b, 8, 0, 0, 0, 0, 5, 0, 0, 5, 0);                              \
    gDPSetTileSize(g++, 0, 2, 2, 0x7E, 0x7E);                                                              \
    gDPSetTile(g++, G_IM_FMT_RGBA, G_IM_SIZ_16b, 8, 0x100, 1, 0, 0, 5, 0, 0, 5, 0);                          \
    gDPSetTileSize(g++, 1, 2, 2, 0x7E, 0x7E);

/* Draws the TNT crates (0x8028E9E4).
 * Draw the boxes: rotate (rotY, 0..4095 = one turn) and place each one,
 * then draw its faces in three passes, retexturing the shared 8 vertices
 * with gSPModifyVertex between them; throb drives the blend (prim LOD). */
void tnt_draw(Gfx **gdl, Dyn48D00 *dyn) {
    Gfx *gfx;
    s32 i;
    f32 rot[4][4];
    f32 trans[4][4];

    gfx = *gdl;
    if (gTntCount > 0) {
        gDPPipeSync(gfx++);
        gDPSetCycleType(gfx++, G_CYC_2CYCLE);
        gDPSetRenderMode(gfx++, G_RM_PASS, G_RM_AA_ZB_OPA_SURF2);
        gDPSetCombineLERP(gfx++, TEXEL1, TEXEL0, PRIM_LOD_FRAC, TEXEL0, TEXEL1, TEXEL0, PRIM_LOD_FRAC, TEXEL0, 0, 0,
                          0, COMBINED, 0, 0, 0, SHADE);
        gSPClearGeometryMode(gfx++, 0xFFFFFFFF);
        gSPSetGeometryMode(gfx++, G_ZBUFFER | G_SHADE | G_SHADING_SMOOTH | G_CULL_BACK);
        gSPTexture(gfx++, 0xFFFF, 0xFFFF, 0, G_TX_RENDERTILE, G_ON);
        gDPSetTextureLOD(gfx++, G_TL_TILE);
    }
    for (i = 0; i < gTntCount; i++) {
        if (gTntCrateList[i].live != 0) {
            guRotateF(rot, (f32) gTntCrateList[i].rotY / 4095.0 * 360.0, 0.0f, 1.0f, 0.0f);
            guTranslateF(trans, gTntCrateList[i].x / 32.0f, gTntCrateList[i].y / 32.0f,
                         gTntCrateList[i].z / 32.0f);
            guMtxCatF(rot, trans, rot);
            guMtxF2L(rot, &dyn->mtx[i]);
            gSPMatrix(gfx++, i * sizeof(Mtx) + 0x600 + (u32) seg2Base, G_MTX_MODELVIEW | G_MTX_MUL | G_MTX_PUSH);
            gSPVertex(gfx++, osVirtualToPhysical(gTntCrateList[i].vtx), 8, 0);
            gDPPipeSync(gfx++);
            BOX_TEX_PAIR(gfx, gTntCrateList[i].tex[0], gTntCrateList[i].tex[1])
            gDPSetPrimColor(gfx++, 0, gTntCrateList[i].throb, 0, 0, 0, 0);
            gSP1Triangle(gfx++, 6, 3, 2, 0);
            gSP1Triangle(gfx++, 6, 7, 3, 0);
            gSP1Triangle(gfx++, 4, 1, 0, 0);
            gSP1Triangle(gfx++, 1, 4, 5, 0);
            gSPModifyVertex(gfx++, 0, G_MWO_POINT_ST, 0x03E00000);
            gSPModifyVertex(gfx++, 1, G_MWO_POINT_ST, 0x03E003E0);
            gSPModifyVertex(gfx++, 2, G_MWO_POINT_ST, 0x000003E0);
            gSPModifyVertex(gfx++, 3, G_MWO_POINT_ST, 0);
            gSP1Triangle(gfx++, 0, 1, 2, 0);
            gSP1Triangle(gfx++, 0, 2, 3, 0);
            gSPModifyVertex(gfx++, 7, G_MWO_POINT_ST, 0x03E00000);
            gSPModifyVertex(gfx++, 6, G_MWO_POINT_ST, 0x03E003E0);
            gSPModifyVertex(gfx++, 5, G_MWO_POINT_ST, 0x000003E0);
            gSPModifyVertex(gfx++, 4, G_MWO_POINT_ST, 0);
            gSP1Triangle(gfx++, 7, 5, 4, 0);
            gSP1Triangle(gfx++, 7, 6, 5, 0);
            BOX_TEX_PAIR(gfx, gTntCrateList[i].tex[2], gTntCrateList[i].tex[3])
            gSPModifyVertex(gfx++, 2, G_MWO_POINT_ST, 0);
            gSPModifyVertex(gfx++, 1, G_MWO_POINT_ST, 0x03E00000);
            gSPModifyVertex(gfx++, 5, G_MWO_POINT_ST, 0x03E003E0);
            gSPModifyVertex(gfx++, 6, G_MWO_POINT_ST, 0x000003E0);
            gSP1Triangle(gfx++, 5, 2, 1, 0);
            gSP1Triangle(gfx++, 2, 5, 6, 0);
            gSPPopMatrix(gfx++, G_MTX_MODELVIEW);
        }
    }
    gDPSetTextureLOD(gfx++, G_TL_LOD);
    gDPPipeSync(gfx++);
    *gdl = gfx;
}

extern u8 gTntCrate0Riding;

/* Crates standing on a vehicle ride on it (on entering) (0x8028F6B4). */
void tnt_attach_to_vehicle(u8 vehicle) {
    s32 i;

    for (i = 0; i < gTntCount; i++) {
        if (gTntCrateList[i].live != 0 && gTntCrateList[i].groundTri == vehicle) {
            gTntCrateList[i].pushSpeed = 0;
            *(&gTntCrate0Riding + i * 0x48) = 1;
            veh_deck_to_local_c(vehicle, gTntCrateList[i].x, gTntCrateList[i].z,
                          &gTntCrateList[i].deckX, &gTntCrateList[i].deckZ);
        }
    }
}

extern s32 gTntCount;
extern u8 gTntCrates;

void tnt_explode(u8);

/* Carried crates move with the vehicle; explode on impact (0x8028F794). */
void tnt_ride_vehicle(u8 vehicle) {
    s32 i;
    s16 local;

    for (i = 0; i < gTntCount; i++) {
        if (*(&gTntCrates + i * 0x48 + 0x18) != 0 && *(&gTntCrates + i * 0x48 + 0x24) != 0) {
            veh_deck_to_world_c(
                vehicle,
                *(s16 *) (&gTntCrates + i * 0x48 + 0x26),
                *(s16 *) (&gTntCrates + i * 0x48 + 0x28),
                (void *) (&gTntCrates + i * 0x48),
                (void *) (&gTntCrates + i * 0x48 + 8));

            *(s32 *) (&gTntCrates + i * 0x48 + 4) = coll_ground_height(
                *(s32 *) (&gTntCrates + i * 0x48),
                *(s32 *) (&gTntCrates + i * 0x48 + 8),
                *(s32 *) (&gTntCrates + i * 0x48 + 4));

            if (*(s16 *) (&gTntCrates + i * 0x48 + 0x1A) != 0) {
                local = 0;
            } else {
                local = *(s16 *) (&gTntCrates + i * 0x48 + 0x1C);
            }

            coll_begin_probe(*(s32 *) (&gTntCrates + i * 0x48),
                          *(s32 *) (&gTntCrates + i * 0x48 + 4),
                          *(s32 *) (&gTntCrates + i * 0x48 + 8));

            if (coll_probe_objects(*(s16 *) (TNT_RADIUS_ADDR + *(&gTntCrates + i * 0x48 + 0xE) * 0x18), local) != 0) {
                tnt_explode((u8) i);
            }
        }
    }
}

extern u8 gTntCrate0Riding;

/* Unpins all crates (on leaving a vehicle) (0x8028F93C). */
void tnt_detach_all(void) {
    s32 i;

    for (i = 0; i < gTntCount; i++) {
        *(&gTntCrate0Riding + i * 0x48) = 0;
    }
}


/* gCollHit if near a crate (0x8028F994).
 * Set gCollHit if the point (x, y, z) is within any live crate's type
 * radius (all in 1/32 units). */
void tnt_test_point(s32 x, s32 y, s32 z) {
    s32 i;
    s32 dist;

    /* One statement (likely a macro in the original): separate
     * statements schedule the three reloads in the opposite order. */
    x >>= 5, y >>= 5, z >>= 5;
    for (i = 0; i < gTntCount; i++) {
        if (gTntCrateList[i].live != 0) {
            dist = util_dist3d(x, y, z, gTntCrateList[i].x >> 5,
                                 gTntCrateList[i].y >> 5, gTntCrateList[i].z >> 5);
            if (dist <= (*(s16 *) (TNT_RADIUS_ADDR + gTntCrateList[i].type * 0x18) >> 5)) {
                gCollHit = 1;
            }
        }
    }
}

extern u8 gTntTypes[];
/* The same with a radius, uncarried crates only (0x8028FAC0). */
void tnt_test_sphere(s32 x, s32 y, s32 z, s32 r) {
    s32 i;
    s32 dist;

    x >>= 5, y >>= 5, z >>= 5, r >>= 5;
    for (i = 0; i < gTntCount; i++) {
        if (gTntCrateList[i].live != 0 && gTntCrateList[i].riding == 0) {
            dist = util_dist3d(x, y, z, gTntCrateList[i].x >> 5,
                                 gTntCrateList[i].y >> 5, gTntCrateList[i].z >> 5);
            if (dist <= (*(s16 *) ((u8 *) gTntTypes + 0x14 + gTntCrateList[i].type * 0x18) >> 5) + r) {
                gCollHit = 1;
            }
        }
    }
}


/* START held at boot (0x8028FC10).
 * Boot-time controller check: read the pads (osContStartReadData,
 * osRecvMesg, osContGetReadData), note whether START is held, latch it
 * into gBootPakManager if controller 1 has a Controller Pak in, and set
 * gBootEraseSave when START is held but was not latched. */
void cont_boot_check_start(void) {
    u8 mask;
    u8 start;
    OSContPad pads[4];

    start = 0;
    osContStartReadData(&gSiMesgQueue);
    osRecvMesg(&gSiMesgQueue, 0, 1);
    osContGetReadData(pads);
    if (pads[0].button & START_BUTTON) {
        start = 1;
    }
    if (cont_query_paks(&gSiMesgQueue, &mask) == 0 && (mask & 1)) {
        gBootPakManager = start;
    }
    gBootEraseSave = start && !gBootPakManager;
}

/* OSContStatus as bytes (u16 type, u8 status, u8 errno) */
typedef struct {
    u8 typeHi;
    u8 typeLo;
    u8 status;
    u8 errNo;
} ContStatusView;


/* Bitmask of the controllers with a Controller Pak in (0x8028FCD4).
 * Waits for a message on the SI queue (its validCount, the word at +8), then
 * sets a bit in *present for each of the 4 controllers whose status has bit 0
 * (CONT_CARD_ON: a pak is in) and no error; returns controller 1's error. */
u8 cont_query_paks(void *mq, u8 *present) {
    ContStatusView status[4];
    s32 i;

    *present = 0;
    osContStartQuery(mq);
    while (*(s32 *) ((u8 *) mq + 8) == 0) {
    }
    osRecvMesg(mq, 0, 0);
    osContGetQuery((OSContStatus *) status);
    for (i = 0; i < 4; i++) {
        if ((status[i].status & 1) && status[i].errNo == 0) {
            *present |= 1 << i;
        }
    }
    return status[0].errNo;
}
