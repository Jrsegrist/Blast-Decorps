#include "common.h"
#include <ultra64.h>
#include "game/game.h"
/* Views: this file reads these shared variables (game/variables.h) as other types. */
#define seg2Base ((u8 *) seg2Base)
#define gGlobeLevels ((GlobeLevel *) gGlobeLevels)
#define gLvlselMarkerFrames ((u16 * N64P *) gLvlselMarkerFrames)
#define gAcademyMarkers ((AcademyMarker *) gAcademyMarkers)
#define gLevelInfo ((LevelInfo *) gLevelInfo)
#define gDynamicBufs ((u8 *) gDynamicBufs)
#define players ((PlayerInfo *) players)
/* end of views */

/* front-end level select: level availability, the level icon display list
 * and the starfield background */

/* (gGlobeLevels[]: GlobeLevel, game/types.h) */

/* (players[]: PlayerInfo, game/types.h) */

/* (gAcademyMarkers: AcademyMarker, game/types.h.) */


typedef struct {
    Gfx * N64P dl[9];
} DlTable;

extern Gfx gLvlselAcademyIconDl[];
extern DlTable gLvlselIconDls; /* per-category display lists */
extern u16 gStarfieldPerspNorm; /* perspNorm */
extern u16 * N64P gStarTextures; /* star textures, 16x16 RGBA16 each */
extern Vtx gLvlselGlobeVtx[]; /* globe vertices, 6 faces x 64 */
extern s32 gGlobeGridX[64]; /* face grid x */
extern s32 gGlobeGridY[64]; /* face grid y */
extern s32 gGlobeGridS[64]; /* face grid s */
extern s32 gGlobeGridT[64]; /* face grid t */

s32 fe_lvlsel_level_open(s32);

#pragma intrinsic (sqrtf)

/* The globe mesh: 6 faces of 8x8, radius 250 (0x801F0570).
 * globe vertices: an 8x8 grid on each of the six cube faces, pushed out to
 * radius 250 (positions, texture coords, normals) */
void fe_lvlsel_build_globe_vtx(void) {
    s32 i;
    s32 j;
    s32 k;
    s32 face;
    f32 half;
    f32 step;
    f32 len;
    f32 px;
    f32 py;
    f32 pz;
    s32 unused[2];

    half = 250.0f;
    step = half * 2.0 / 7.0;
    for (j = 0; j < 8; j++) {
        for (i = 0; i < 8; i++) {
            gGlobeGridX[i + j * 8] = i * step - half;
            gGlobeGridY[i + j * 8] = j * step - half;
            gGlobeGridS[i + j * 8] = (i * 32) << 5;
            gGlobeGridT[i + j * 8] = (j * 32) << 5;
        }
    }
    for (k = 0; k < 64; k++) {
        px = gGlobeGridX[k];
        py = gGlobeGridY[k];
        pz = 250.0f;
        len = sqrtf(px * px + py * py + pz * pz);
        gLvlselGlobeVtx[k].v.ob[0] = px / len * 250.0;
        gLvlselGlobeVtx[k].v.ob[1] = py / len * 250.0;
        gLvlselGlobeVtx[k].v.ob[2] = pz / len * 250.0;
        gLvlselGlobeVtx[k].v.tc[0] = gGlobeGridS[k];
        gLvlselGlobeVtx[k].v.tc[1] = gGlobeGridT[k];
        gLvlselGlobeVtx[k].n.n[0] = px / len * 127.0f;
        gLvlselGlobeVtx[k].n.n[1] = py / len * 127.0f;
        gLvlselGlobeVtx[k].n.n[2] = pz / len * 127.0f;
        gLvlselGlobeVtx[k].n.a = 0xFF;
    }
    face = 0x40;
    for (k = 0; k < 64; k++) {
        px = gGlobeGridX[k];
        py = gGlobeGridY[k];
        pz = 250.0f;
        len = sqrtf(px * px + py * py + pz * pz);
        gLvlselGlobeVtx[face + k].v.ob[0] = px / len * 250.0;
        gLvlselGlobeVtx[face + k].v.ob[1] = py / len * 250.0;
        gLvlselGlobeVtx[face + k].v.ob[2] = -pz / len * 250.0;
        gLvlselGlobeVtx[face + k].v.tc[0] = gGlobeGridS[k];
        gLvlselGlobeVtx[face + k].v.tc[1] = gGlobeGridT[k];
        gLvlselGlobeVtx[face + k].n.n[0] = px / len * 127.0f;
        gLvlselGlobeVtx[face + k].n.n[1] = py / len * 127.0f;
        gLvlselGlobeVtx[face + k].n.n[2] = -pz / len * 127.0f;
        gLvlselGlobeVtx[face + k].n.a = 0xFF;
    }
    face = 0x80;
    for (k = 0; k < 64; k++) {
        px = gGlobeGridX[k];
        py = gGlobeGridY[k];
        pz = 250.0f;
        len = sqrtf(px * px + py * py + pz * pz);
        gLvlselGlobeVtx[face + k].v.ob[0] = px / len * 250.0;
        gLvlselGlobeVtx[face + k].v.ob[1] = pz / len * 250.0;
        gLvlselGlobeVtx[face + k].v.ob[2] = py / len * 250.0;
        gLvlselGlobeVtx[face + k].v.tc[0] = gGlobeGridS[k];
        gLvlselGlobeVtx[face + k].v.tc[1] = gGlobeGridT[k];
        gLvlselGlobeVtx[face + k].n.n[0] = px / len * 127.0f;
        gLvlselGlobeVtx[face + k].n.n[1] = pz / len * 127.0f;
        gLvlselGlobeVtx[face + k].n.n[2] = py / len * 127.0f;
        gLvlselGlobeVtx[face + k].n.a = 0xFF;
    }
    face = 0xC0;
    for (k = 0; k < 64; k++) {
        px = gGlobeGridX[k];
        py = gGlobeGridY[k];
        pz = 250.0f;
        len = sqrtf(px * px + py * py + pz * pz);
        gLvlselGlobeVtx[face + k].v.ob[0] = px / len * 250.0;
        gLvlselGlobeVtx[face + k].v.ob[1] = -pz / len * 250.0;
        gLvlselGlobeVtx[face + k].v.ob[2] = py / len * 250.0;
        gLvlselGlobeVtx[face + k].v.tc[0] = gGlobeGridS[k];
        gLvlselGlobeVtx[face + k].v.tc[1] = gGlobeGridT[k];
        gLvlselGlobeVtx[face + k].n.n[0] = px / len * 127.0f;
        gLvlselGlobeVtx[face + k].n.n[1] = -pz / len * 127.0f;
        gLvlselGlobeVtx[face + k].n.n[2] = py / len * 127.0f;
        gLvlselGlobeVtx[face + k].n.a = 0xFF;
    }
    face = 0x100;
    for (k = 0; k < 64; k++) {
        px = gGlobeGridX[k];
        py = gGlobeGridY[k];
        pz = 250.0f;
        len = sqrtf(px * px + py * py + pz * pz);
        gLvlselGlobeVtx[face + k].v.ob[0] = pz / len * 250.0;
        gLvlselGlobeVtx[face + k].v.ob[1] = px / len * 250.0;
        gLvlselGlobeVtx[face + k].v.ob[2] = py / len * 250.0;
        gLvlselGlobeVtx[face + k].v.tc[0] = gGlobeGridS[k];
        gLvlselGlobeVtx[face + k].v.tc[1] = gGlobeGridT[k];
        gLvlselGlobeVtx[face + k].n.n[0] = pz / len * 127.0f;
        gLvlselGlobeVtx[face + k].n.n[1] = px / len * 127.0f;
        gLvlselGlobeVtx[face + k].n.n[2] = py / len * 127.0f;
        gLvlselGlobeVtx[face + k].n.a = 0xFF;
    }
    face = 0x140;
    for (k = 0; k < 64; k++) {
        px = gGlobeGridX[k];
        py = gGlobeGridY[k];
        pz = 250.0f;
        len = sqrtf(px * px + py * py + pz * pz);
        gLvlselGlobeVtx[face + k].v.ob[0] = -pz / len * 250.0;
        gLvlselGlobeVtx[face + k].v.ob[1] = px / len * 250.0;
        gLvlselGlobeVtx[face + k].v.ob[2] = py / len * 250.0;
        gLvlselGlobeVtx[face + k].v.tc[0] = gGlobeGridS[k];
        gLvlselGlobeVtx[face + k].v.tc[1] = gGlobeGridT[k];
        gLvlselGlobeVtx[face + k].n.n[0] = -pz / len * 127.0f;
        gLvlselGlobeVtx[face + k].n.n[1] = px / len * 127.0f;
        gLvlselGlobeVtx[face + k].n.n[2] = py / len * 127.0f;
        gLvlselGlobeVtx[face + k].n.a = 0xFF;
    }
}

/*
 * fe_lvlsel_build_globe_dl builds the globe display list: inflates worldtextures.raw
 * (ROM romWorldTexturesRaw..romAttractPictures) to the heap, carves the texture pointers out of
 * its tail, builds the vertices (fe_lvlsel_build_globe_vtx) and emits, per face and grid cell,
 * five mip levels of a 17x17..2x2 RGBA16 texture plus two triangles.
 * Matched once the mip loop became a for loop with all three counters in its
 * header (`for (lod = 0, tmem = 0, texoff = 0; ...; lod++, tmem += step,
 * texoff += ww + 3)`) and two unused words sat below `size` (frame 0xB0).
 * romWorldTexturesRaw is a real symbol (undefined_syms.hd_front_end).
 */
typedef struct {
    u16 tex[49][0x1A4];
} Globe; /* one face's 7x7 cells, five mip levels each */

/* Inflates the world textures, the globe display list (0x801F1568). */
Gfx *fe_lvlsel_build_globe_dl(void) {
    u8 *base;
    s32 size;
    s32 pad[2];
    s32 i;
    s32 j;
    s32 idx;
    s32 k;
    s32 faceOff;
    s32 flip;
    s32 lod;
    s32 texoff;
    s32 tmem;
    u32 step;
    s32 w;
    u32 line;
    u32 ww;
    Gfx *gfx;
    Gfx *dl;

    base = gHeapPtr;
    size = romAttractPictures - romWorldTexturesRaw;
    hd_rom_load((u32) romWorldTexturesRaw, (u32) gHeapPtr, (u32 *) &size, 0xD, 0, 1);
    gHeapPtr += size;
    gStarTextures = (u16 *) (gHeapPtr - 0x6600);
    for (i = 0; i < 3; i++) {
        gLvlselMarkerFrames[i] = (u16 *) gHeapPtr - (3 - i) * 0x800 - 0x1800;
    }
    gLvlselGlowTex = gHeapPtr - 0x3000;
    gLvlselGlowTex2 = gHeapPtr - 0x2000;
    gLvlselSunTex = gHeapPtr - 0x1000;
    gfx = (Gfx *) gHeapPtr;
    dl = gfx;
    fe_lvlsel_build_globe_vtx();
    gSPTexture(gfx++, 0x8000, 0x8000, 4, G_TX_RENDERTILE, G_ON);
    for (k = 0; k < 6; k++) {
        faceOff = k << 6;
        gSPVertex(gfx++, &gLvlselGlobeVtx[faceOff], 8, 0);
        flip = 8;
        for (i = 0; i < 7; i++) {
            gSPVertex(gfx++, &gLvlselGlobeVtx[faceOff + i * 8 + 8], 8, flip);
            for (j = 0; j < 7; j++) {
                idx = i * 7 + j;
                for (lod = 0, tmem = 0, texoff = 0; lod < 5; lod++, tmem += step, texoff += ww + 3) {
                    w = (16 >> lod) + 1;
                    line = (w + 3) >> 2;
                    step = w * line;
                    ww = w * w;
                    gDPSetTextureImage(gfx++, G_IM_FMT_RGBA, G_IM_SIZ_16b, w, &((Globe *) base)[k].tex[idx][texoff]);
                    gDPSetTile(gfx++, G_IM_FMT_RGBA, G_IM_SIZ_16b, line, tmem, G_TX_LOADTILE, 0, 0, 0, 0, 0, 0, 0);
                    gDPLoadTile(gfx++, G_TX_LOADTILE, 0, 0, w << 2, w << 2);
                    gDPSetTile(gfx++, G_IM_FMT_RGBA, G_IM_SIZ_16b, line, tmem, lod, 0, 0, 0, lod, 0, 0, lod);
                    gDPSetTileSize(gfx++, lod, ((j * 16) << 2) >> lod, ((i * 16) << 2) >> lod,
                                   ((j * 16 + 16) << 2) >> lod, ((i * 16 + 16) << 2) >> lod);
                }
                /* faces 1, 2 and 5 wind the other way */
                if (k == 1 || k == 2 || k == 5) {
                    if (flip == 8) {
                        gSP1Triangle(gfx++, j, j + 8, j + 9, 0);
                        gSP1Triangle(gfx++, j, j + 9, j + 1, 0);
                    } else {
                        gSP1Triangle(gfx++, j + 8, j, j + 1, 0);
                        gSP1Triangle(gfx++, j + 8, j + 1, j + 9, 0);
                    }
                } else if (flip == 8) {
                    gSP1Triangle(gfx++, j, j + 9, j + 8, 0);
                    gSP1Triangle(gfx++, j, j + 1, j + 9, 0);
                } else {
                    gSP1Triangle(gfx++, j + 8, j + 1, j, 0);
                    gSP1Triangle(gfx++, j + 8, j + 9, j + 1, 0);
                }
            }
            flip ^= 8;
        }
    }
    gSPEndDisplayList(gfx++);
    gHeapPtr += (gfx - dl) * sizeof(Gfx);
    return dl;
}

/* hd.c's LEVEL_DONE */
#define LEVEL_DONE(l) (players[playerNumber].medal[l] > 0 && players[playerNumber].medal[l] < 6) ? 1 : 0

/* A level can be picked: done, level 0, or unlocked (0x801F1DA8).
 * is the level available: done, level 0, or unlocked by a done level */
s32 fe_lvlsel_level_open(s32 level) {
    GlobeLevel *info;
    s32 i;
    s32 j;

    if (fe_globe_level_locked(level)) {
        return 0;
    }
    if (lvl_planet(level) != 3) {
        return 0;
    }
    if ((LEVEL_DONE(level)) || level == 0) {
        return 1;
    }
    for (i = 0; i < 60; i++) {
        if (LEVEL_DONE(i)) {
            info = &gGlobeLevels[i];
            for (j = 0; j < 8 && info->routes[j] != -1; j++) {
                if (info->routes[j] == level) {
                    return 1;
                }
            }
            for (j = 0; j < 4 && info->commRoutes[j] != -1; j++) {
                if (info->commRoutes[j] == level && (players[playerNumber].commPts[i] & (1 << j))) {
                    return 1;
                }
            }
        }
    }
    return 0;
}

/* Icons of the academy levels found (0x801F2000).
 * display list of the level icons (one quad per level with its bit set in
 * the player's unk90), drawn in two passes */
Gfx *fe_lvlsel_build_academy_dl(void) {
    Vtx *vtx;
    u8 *heap;
    Gfx *dl;
    Gfx *gfx;
    s32 i;
    s32 pass;

    vtx = (Vtx *) gHeapPtr;
    heap = gHeapPtr;
    gHeapPtr += 0x300;
    gfx = (Gfx *) gHeapPtr;
    dl = gfx;
    gSPClearGeometryMode(gfx++, 0xFFFFFFFF);
    gSPSetGeometryMode(gfx++, G_SHADE | G_SHADING_SMOOTH | G_CULL_BACK);
    gSPTexture(gfx++, 0x8000, 0x8000, 5, G_TX_RENDERTILE, G_ON);
    gDPSetTextureLOD(gfx++, G_TL_LOD);
    gDPSetCycleType(gfx++, G_CYC_2CYCLE);
    gDPSetRenderMode(gfx++, G_RM_PASS, G_RM_XLU_SURF2);
    for (pass = 0; pass < 2; pass++) {
        gDPPipeSync(gfx++);
        if (pass != 0) {
            gDPSetCombine(gfx++, 0x26A00A, 0x1F0C93FF);
        } else {
            gSPDisplayList(gfx++, gLvlselAcademyIconDl);
            gDPSetCombine(gfx++, 0x26A1FF, 0x1F1492FF);
        }
        for (i = 0; i < 60; i++) {
            s32 j;
            s32 found;

            for (j = 0, found = 0; j < 6 && !found; j++) {
                if (gAcademyMarkers[j].id == i) {
                    found = 1;
                }
            }
            /* j is one past the match here, so bit (j + 31) & 31 = j - 1 */
            if (found && (players[playerNumber].academy & (1 << PORT_SHAMT(j + 31)))) {
                fe_globe_quad_at_level(vtx, i, pass * 1.5 + 2.5, pass * 1.5 + 4.0, 32, 32, 2.25f, 1);
                gSPVertex(gfx++, vtx, 4, 0);
                gSP1Triangle(gfx++, 0, 1, 2, 0);
                gSP1Triangle(gfx++, 2, 3, 0, 0);
                vtx += 4;
            }
        }
    }
    gDPPipeSync(gfx++);
    gSPEndDisplayList(gfx++);
    gHeapPtr = (u8 *) gfx;
    osWritebackDCache(heap, 0x300);
    return dl;
}

/* One icon per open level, by medal (0x801F2428).
 * display list of the level select icons, one category (colour/combiner) at a time,
 * flushing vertices 16 at a time */
Gfx *fe_lvlsel_build_level_icons_dl(void) {
    GlobeLevel *info;
    Vtx *vtx;
    Gfx *gfx;
    Gfx *dl;
    s32 lvl;
    s32 j;
    s32 n;
    s32 flushed;
    s32 pending;
    u8 found;
    s8 cat;
    u8 avail;
    f32 scale;
    DlTable dls; /* initialised from gLvlselIconDls */
    Gfx *cur;
    Gfx *prev;
    u8 r;
    u8 g;
    u8 b;
    u8 a;

    n = 0;
    flushed = 0;
    vtx = (Vtx *) gHeapPtr;
    dls = gLvlselIconDls;
    prev = NULL;
    gHeapPtr += 0x1E00;
    gfx = (Gfx *) gHeapPtr;
    dl = gfx;
    gDPPipeSync(gfx++);
    gDPSetTextureLOD(gfx++, G_TL_LOD);
    gDPSetCycleType(gfx++, G_CYC_2CYCLE);
    gDPSetRenderMode(gfx++, G_RM_PASS, G_RM_XLU_SURF2);
    gSPTexture(gfx++, 0x8000, 0x8000, 5, G_TX_RENDERTILE, G_ON);
    for (cat = 8; cat >= 0; cat--) {
        cur = dls.dl[cat];
        if (cur != prev) {
            gSPDisplayList(gfx++, cur);
        }
        prev = cur;
        gDPPipeSync(gfx++);
        switch (cat) {
            case 0:
            case 8:
                gDPSetCombine(gfx++, 0x26A1FF, 0x1F14933F);
                r = 0, g = 0, b = 0, a = 0xFF;
                break;
            case 1:
            case 2:
            case 3:
            case 4:
                gDPSetCombine(gfx++, 0x26A00A, 0x1F0C93FF);
                r = 0, g = 0, b = 0, a = 0xFF;
                break;
            case 5:
                gDPSetCombine(gfx++, 0x26A1FF, 0x1F0C933F);
                r = 0x50, g = 0x50, b = 0x50, a = 0xFF;
                break;
            case 6:
                gDPSetCombine(gfx++, 0x26A1FF, 0x1F0C933F);
                r = 0xFF, g = 0, b = 0, a = 0xFF;
                break;
            case 7:
                gDPSetCombine(gfx++, 0x26A1FF, 0x1F0C933F);
                r = 0, g = 0xFF, b = 0, a = 0xFF;
                break;
        }
        for (lvl = 0; lvl < 60; lvl++) {
            avail = (cat == 6 || cat == 7) && (LEVEL_DONE(lvl)) && players[playerNumber].medal[lvl] != 4;
            if (players[playerNumber].medal[lvl] == cat || avail) {
                info = &gGlobeLevels[lvl];
                if (fe_lvlsel_level_open(lvl)) {
                    if (avail) {
                        for (j = 0, found = 0; j < 4 && info->commRoutes[j] != -1 && !found; j++) {
                            if (!(players[playerNumber].commPts[lvl] & (1 << j))) {
                                found = 1;
                            }
                        }
                        if ((found && cat == 6) || (!found && cat == 7)) {
                            continue;
                        }
                    }
                    if (gLevelInfo[lvl].kind & LEVEL_KIND_ANY_MISSION) {
                        scale = 1.75f;
                    } else {
                        scale = 1.0f;
                    }
                    if (avail) {
                        scale += 0.3;
                    }
                    fe_globe_quad_at_level(&vtx[n], lvl, 0.0f, 0.0f, 32, 32, scale, 0);
                    for (j = 0; j < 4; j++) {
                        vtx[n + j].v.cn[0] = r;
                        vtx[n + j].v.cn[1] = g;
                        vtx[n + j].v.cn[2] = b;
                        vtx[n + j].v.cn[3] = a;
                    }
                    n += 4;
                    pending = n - flushed;
                    if (!(pending & 0xF)) {
                        Vtx *v = &vtx[n - 16];

                        gSPVertex(gfx++, v, 16, 0);
                        for (j = 0; j < 16; j += 4) {
                            gSP1Triangle(gfx++, j, j + 1, j + 2, 0);
                            gSP1Triangle(gfx++, j + 2, j + 3, j, 0);
                        }
                    }
                }
            }
        }
        pending = n - flushed;
        if (pending % 16) {
            Vtx *v = &vtx[n - pending % 16];

            gSPVertex(gfx++, v, pending % 16, 0);
            for (j = 0; j < pending % 16; j += 4) {
                gSP1Triangle(gfx++, j, j + 1, j + 2, 0);
                gSP1Triangle(gfx++, j + 2, j + 3, j, 0);
            }
            flushed = n;
        }
    }
    gDPPipeSync(gfx++);
    gSPEndDisplayList(gfx++);
    gHeapPtr = (u8 *) gfx;
    osWritebackDCache(vtx, 0x1E00);
    return dl;
}

/* 0x200 random star quads (0x801F2E20).
 * starfield: projection/view matrices, then 0x200 random textured star quads
 * (vertices at segment 2 offset 0x15C0, copied to the other buffer) */
Gfx *fe_lvlsel_build_starfield_dl(void) {
    Vtx *verts;
    u8 *copy;
    Mtx *mtx;
    Gfx *gfx;
    Gfx *dl;
    s32 i;
    s32 j;
    s32 unused[4];
    s32 k;
    s32 unused2[7];

    verts = (Vtx *) (gDynamicBufs + 0x15C0);
    copy = gDynamicBufs + 0x22A58;
    mtx = (Mtx *) gHeapPtr;
    gHeapPtr += 0x80;
    gfx = (Gfx *) gHeapPtr;
    dl = gfx;
    guPerspective(mtx, &gStarfieldPerspNorm, 45.0f, 4.0f / 3.0f, 100.0f, 25000.0f, 1.0f);
    guLookAt(mtx + 1, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 1.0f, 0.0f);
    gImmp1(gfx++, G_RDPHALF_1, gStarfieldPerspNorm);
    gSPMatrix(gfx++, mtx, G_MTX_PROJECTION | G_MTX_LOAD | G_MTX_NOPUSH);
    gSPMatrix(gfx++, mtx + 1, G_MTX_MODELVIEW | G_MTX_LOAD | G_MTX_NOPUSH);
    gSPTexture(gfx++, 0x8000, 0x8000, 0, G_TX_RENDERTILE, G_ON);
    k = 0;
    for (i = 0; i < 0x200; i += 16) {
        gSPVertex(gfx++, (Vtx *) (i * 16 + 0x15C0 + (u32) seg2Base), 16, 0);
        gDPPipeSync(gfx++);
        for (j = 0; j < 16; j += 4) {
            if (((i + j) >> 2) % 43 == 0) {
                gDPLoadTextureBlock(gfx++, gStarTextures + k * 256, G_IM_FMT_RGBA, G_IM_SIZ_16b, 16, 16, 0,
                                    G_TX_CLAMP, G_TX_CLAMP, G_TX_NOMASK, G_TX_NOMASK, G_TX_NOLOD, G_TX_NOLOD);
                k++;
            }
            fe_globe_respawn_star(verts, (i + j) / 4, util_rand_range(0, 25000));
            verts[i + j].v.tc[0] = 0;
            verts[i + j].v.tc[1] = 0;
            verts[i + j + 1].v.tc[0] = 0;
            verts[i + j + 1].v.tc[1] = 0x3C0;
            verts[i + j + 2].v.tc[0] = 0x3C0;
            verts[i + j + 2].v.tc[1] = 0x3C0;
            verts[i + j + 3].v.tc[0] = 0x3C0;
            verts[i + j + 3].v.tc[1] = 0;
            gSP1Triangle(gfx++, j, j + 1, j + 2, 0);
            gSP1Triangle(gfx++, j, j + 2, j + 3, 0);
        }
    }
    for (i = 0; i < 0x200 * sizeof(Vtx); i++) {
        copy[i] = ((u8 *) verts)[i];
    }
    gDPPipeSync(gfx++);
    gSPEndDisplayList(gfx++);
    gHeapPtr += (gfx - dl) * sizeof(Gfx);
    osWritebackDCache(verts, 0x200 * sizeof(Vtx));
    return dl;
}
