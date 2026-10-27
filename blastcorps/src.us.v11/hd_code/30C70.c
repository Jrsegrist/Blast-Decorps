#include "common.h"
#include <ultra64.h>
#include "game/game.h"
/* Views: this file reads these shared variables (game/variables.h) as other types. */
#define seg2Base ((u8 *) seg2Base)
/* end of views */

/* This file's .bss starts at 0x8036C790 */
extern s32 gMarkedTarget;
extern s16 * N64P gShownTargets[10]; /* point lists to keep on screen */
extern Vtx gLocatorVtx[][4];   /* off-screen marker quads */
extern Mtx gLocatorMtx[];      /* off-screen marker matrices */
extern s32 gMarkerBounce; /* target bounce offset */
extern s32 gMarkerBounceCount; /* target bounce count */
extern u8 gMarkerBounceDir;  /* target bounce direction */

typedef struct SpriteVtxBuf {
    u8 pad[0x1E00];
    Vtx vtx[1]; /* sprite quads */
} SpriteVtxBuf;

void hud_mtx_xform(Mtx *m, f32 x, f32 y, f32 z, f32 w, f32 *ox, f32 *oy, f32 *oz, f32 *ow);
s32 hud_target_was_shown(void);
void hud_forget_offscreen_targets(void *arg0);

/* Clears the shown-target list and the marker state (0x80275430). */
void hud_init_target_marker(void) {
    s32 i;

    for (i = 0; i < 10; i++) {
        gShownTargets[i] = 0;
    }
    gMarkedTargetPts = 0;
    gMarkerArrowsOnScreen = 0;
}

/* Four bouncing arrows round the next target, coloured by gCarrierEta (0x80275478).
 * Picks the on-screen target marker, colours it by time left, bounces it and draws its four arrows */
void hud_draw_target_marker(SpriteVtxBuf *arg0, Gfx **gfxp, u8 arg2) {
    s16 i;
    s16 k;
    s16 vtxIdx = 16;
    Gfx *gdl = *gfxp;
    s16 sx;
    s16 sy;
    s16 px;
    s16 py;
    s16 minX;
    s16 maxX;
    s16 minY;
    s16 maxY;
    u8 r;
    u8 g;
    s16 t;
    s16 *pts;
    s32 ret;

    minX = 0x7FFF, maxX = -0x8000, minY = 0x7FFF, maxY = -0x8000;
    ret = obj_find_nearest_target();
    pts = gNearestTargetPts;
    gMarkerArrowsOnScreen = 0;
    if (!gTargetsCleared && !ret && gLevelKind == LEVEL_KIND_MISSION && !gPaused) {
        if (!yoshi_show_hint(YW_HINT_TARGETS_CLEARED)) {
            yoshi_demand(YOSHI_DEMAND_OPEN | YW_TARGETS_CLEARED);
            hud_panel_start(3, 1, 2, SFX_SUCCESS);
        }
        gTargetsCleared = 1;
    }
    if (gNearestTargetPts != NULL && (!hud_target_was_shown() || arg2) && gMarkedTargetPts == NULL) {
        for (i = 0; i < 4; i++) {
            hud_project_point(arg0, gNearestTargetPts[0], gNearestTargetPts[1], gNearestTargetPts[2], &sx, &sy, NULL, NULL, NULL, 1.0f);
            gNearestTargetPts += 3;
            if (sx < minX) {
                minX = sx;
            }
            if (sx > maxX) {
                maxX = sx;
            }
            if (sy < minY) {
                minY = sy;
            }
            if (sy > maxY) {
                maxY = sy;
            }
        }
        gMarkerArrowsOnScreen = 0;
        for (k = 0; k < 4; k++) {
            switch (k) {
                case 0:
                    px = (maxX - minX) / 2 + minX;
                    py = minY;
                    break;
                case 1:
                    px = (maxX - minX) / 2 + minX;
                    py = maxY;
                    break;
                case 2:
                    px = minX;
                    py = (maxY - minY) / 2 + minY;
                    break;
                case 3:
                    px = maxX;
                    py = (maxY - minY) / 2 + minY;
                    break;
            }
            if (px < 310 && px >= 11 && py < 230 && py >= 11) {
                gMarkedTargetPts = pts;
                gMarkedTarget = ret;
                gMarkedTargetKind = gNearestTargetKind;
                gMarkerBounceCount = 0;
            }
        }
    }
    if (gMarkedTargetPts != NULL) {
        obj_calc_target_time(gMarkedTarget);
        if (gCarrierEta >= 1501 || !gHaveCarrier) {
            g = 0xFF;
            r = 0;
        } else if (gCarrierEta < 500) {
            r = 0xFF;
            g = 0;
        } else {
            t = (gCarrierEta - 500) / 1000.0f * 511.0f;
            if (t < 256) {
                g = t, r = 0xFF;
            } else {
                g = 0xFF, r = 0x1FE - t;
            }
        }
        pts = gMarkedTargetPts;
        for (i = 0; i < 4; i++) {
            hud_project_point(arg0, pts[0], pts[1], pts[2], &sx, &sy, NULL, NULL, NULL, 1.0f);
            pts += 3;
            if (sx < minX) {
                minX = sx;
            }
            if (sx > maxX) {
                maxX = sx;
            }
            if (sy < minY) {
                minY = sy;
            }
            if (sy > maxY) {
                maxY = sy;
            }
        }
        for (k = 0; k < 4; k++) {
            switch (k) {
                case 0:
                    px = (maxX - minX) / 2 + minX, py = minY - gMarkerBounce;
                    break;
                case 1:
                    px = (maxX - minX) / 2 + minX, py = maxY + gMarkerBounce;
                    break;
                case 2:
                    px = minX - gMarkerBounce;
                    py = (maxY - minY) / 2 + minY;
                    break;
                case 3:
                    px = maxX + gMarkerBounce;
                    py = (maxY - minY) / 2 + minY;
                    break;
            }
            if (px < 310 && px >= 11 && py < 230 && py >= 11) {
                gMarkerArrowsOnScreen++;
            }
            vtxIdx = hud_sprite_quad(arg0, k, vtxIdx, px, py, 8, 8, r, g, 0, 0xFF);
        }
        if (!gMarkerBounceDir) {
            gMarkerBounce++;
            if (gMarkerBounce == 10) {
                gMarkerBounceDir = 1;
                gMarkerBounceCount++;
            }
        } else {
            gMarkerBounce--;
            if (gMarkerBounce == 0) {
                gMarkerBounceDir = 0;
                gMarkerBounceCount++;
            }
        }
        if (gMarkerBounceCount == 5) {
            gMarkedTargetPts = NULL;
        }
        gdl = hud_sprite_setup(gdl, 0);
        gSPVertex(gdl++, (u32) seg2Base + 0x1F00, 16, 0);
        vtxIdx = 0;
        for (k = 0; k < 4; k++) {
            gSP1Triangle(gdl++, vtxIdx, vtxIdx + 1, vtxIdx + 2, 0);
            gSP1Triangle(gdl++, vtxIdx, vtxIdx + 2, vtxIdx + 3, 0);
            vtxIdx += 4;
        }
    }
    hud_forget_offscreen_targets(arg0);
    *gfxp = gdl;
}

/* 2D render state and the 32x32 IA sprite texture (0x80275DA4). */
Gfx *hud_sprite_setup(Gfx *gfx, u8 arg1) {
    Gfx *gdl = gfx;

    if (!arg1) {
        gSPMatrix(gdl++, (u32) seg2Base + 0xC0, G_MTX_PROJECTION | G_MTX_LOAD | G_MTX_NOPUSH);
        gSPMatrix(gdl++, (u32) seg2Base + 0x1C0, G_MTX_MODELVIEW | G_MTX_LOAD | G_MTX_NOPUSH);
    }
    gSPClearGeometryMode(gdl++, 0xFFFFFFFF);
    gSPSetGeometryMode(gdl++, G_SHADE | G_SHADING_SMOOTH);
    gDPPipeSync(gdl++);
    gDPSetCycleType(gdl++, G_CYC_1CYCLE);
    gDPSetRenderMode(gdl++, G_RM_XLU_SURF, G_RM_XLU_SURF2);
    gDPSetCombineMode(gdl++, G_CC_MODULATERGBA, G_CC_MODULATERGBA);
    gSPTexture(gdl++, 0xFFFF, 0xFFFF, 0, G_TX_RENDERTILE, G_ON);
    gDPLoadTextureBlock(gdl++, (u32) gHudArrowTex - 0x80000000, G_IM_FMT_IA, G_IM_SIZ_8b, 32, 32, 0, G_TX_CLAMP,
                        G_TX_CLAMP, G_TX_NOMASK, G_TX_NOMASK, G_TX_NOLOD, G_TX_NOLOD);
    return gdl;
}

/* A one-colour sprite quad (0x80276080). */
s32 hud_sprite_quad(SpriteVtxBuf *arg0, u8 arg1, s32 arg2, s32 arg3, s32 arg4, s32 arg5, s32 arg6, u8 r, u8 g, u8 b, u8 a) {
    return hud_sprite_quad_colors(arg0, arg1, arg2, arg3, arg4, arg5, arg6, r, g, b, a, r, g, b, a, r, g, b, a, r, g, b, a);
}

/* Four vertices centred on x, y with per-corner colours (0x80276130).
 * Writes a quad of 4 Vtx centred on (x, y), half size (w, h); arg1 picks the texture orientation */
s32 hud_sprite_quad_colors(SpriteVtxBuf *arg0, u8 arg1, s32 arg2, s32 x, s32 y, s32 w, s32 h,
                  u8 r0, u8 g0, u8 b0, u8 a0, u8 r1, u8 g1, u8 b1, u8 a1,
                  u8 r2, u8 g2, u8 b2, u8 a2, u8 r3, u8 g3, u8 b3, u8 a3) {
    s32 pad;

    switch (arg1) {
        case 0:
            arg0->vtx[arg2].v.tc[0] = 0, arg0->vtx[arg2].v.tc[1] = 0;
            arg0->vtx[arg2 + 1].v.tc[0] = 0x3E0, arg0->vtx[arg2 + 1].v.tc[1] = 0;
            arg0->vtx[arg2 + 2].v.tc[0] = 0x3E0, arg0->vtx[arg2 + 2].v.tc[1] = 0x3E0;
            arg0->vtx[arg2 + 3].v.tc[0] = 0, arg0->vtx[arg2 + 3].v.tc[1] = 0x3E0;
            break;
        case 1:
            arg0->vtx[arg2].v.tc[0] = 0, arg0->vtx[arg2].v.tc[1] = 0x3E0;
            arg0->vtx[arg2 + 1].v.tc[0] = 0x3E0, arg0->vtx[arg2 + 1].v.tc[1] = 0x3E0;
            arg0->vtx[arg2 + 2].v.tc[0] = 0x3E0, arg0->vtx[arg2 + 2].v.tc[1] = 0;
            arg0->vtx[arg2 + 3].v.tc[0] = 0, arg0->vtx[arg2 + 3].v.tc[1] = 0;
            break;
        case 2:
            arg0->vtx[arg2].v.tc[0] = 0, arg0->vtx[arg2].v.tc[1] = 0;
            arg0->vtx[arg2 + 1].v.tc[0] = 0, arg0->vtx[arg2 + 1].v.tc[1] = 0x3E0;
            arg0->vtx[arg2 + 2].v.tc[0] = 0x3E0, arg0->vtx[arg2 + 2].v.tc[1] = 0x3E0;
            arg0->vtx[arg2 + 3].v.tc[0] = 0x3E0, arg0->vtx[arg2 + 3].v.tc[1] = 0;
            break;
        case 3:
            arg0->vtx[arg2].v.tc[0] = 0, arg0->vtx[arg2].v.tc[1] = 0x3E0;
            arg0->vtx[arg2 + 1].v.tc[0] = 0, arg0->vtx[arg2 + 1].v.tc[1] = 0;
            arg0->vtx[arg2 + 2].v.tc[0] = 0x3E0, arg0->vtx[arg2 + 2].v.tc[1] = 0;
            arg0->vtx[arg2 + 3].v.tc[0] = 0x3E0, arg0->vtx[arg2 + 3].v.tc[1] = 0x3E0;
            break;
    }
    arg0->vtx[arg2].v.ob[0] = x - w;
    arg0->vtx[arg2].v.ob[1] = y - h;
    arg0->vtx[arg2].v.ob[2] = -10;
    arg0->vtx[arg2].v.flag = 0;
    arg0->vtx[arg2].v.cn[0] = r0;
    arg0->vtx[arg2].v.cn[1] = g0;
    arg0->vtx[arg2].v.cn[2] = b0;
    arg0->vtx[arg2].v.cn[3] = a0;
    arg2++;
    arg0->vtx[arg2].v.ob[0] = x + w;
    arg0->vtx[arg2].v.ob[1] = y - h;
    arg0->vtx[arg2].v.ob[2] = -10;
    arg0->vtx[arg2].v.flag = 0;
    arg0->vtx[arg2].v.cn[0] = r1;
    arg0->vtx[arg2].v.cn[1] = g1;
    arg0->vtx[arg2].v.cn[2] = b1;
    arg0->vtx[arg2].v.cn[3] = a1;
    arg2++;
    arg0->vtx[arg2].v.ob[0] = x + w;
    arg0->vtx[arg2].v.ob[1] = y + h;
    arg0->vtx[arg2].v.ob[2] = -10;
    arg0->vtx[arg2].v.flag = 0;
    arg0->vtx[arg2].v.cn[0] = r2;
    arg0->vtx[arg2].v.cn[1] = g2;
    arg0->vtx[arg2].v.cn[2] = b2;
    arg0->vtx[arg2].v.cn[3] = a2;
    arg2++;
    arg0->vtx[arg2].v.ob[0] = x - w;
    arg0->vtx[arg2].v.ob[1] = y + h;
    arg0->vtx[arg2].v.ob[2] = -10;
    arg0->vtx[arg2].v.flag = 0;
    arg0->vtx[arg2].v.cn[0] = r3;
    arg0->vtx[arg2].v.cn[1] = g3;
    arg0->vtx[arg2].v.cn[2] = b3;
    arg0->vtx[arg2].v.cn[3] = a3;
    arg2++;
    return arg2;
}

/* Drops shown targets no longer on screen (0x8027656C). */
void hud_forget_offscreen_targets(void *arg0) {
    s32 i;
    s32 j;
    s16 *pts;
    s16 sx;
    s16 sy;
    s16 px;
    s16 py;
    s16 minX;
    s16 maxX;
    s16 minY;
    s16 maxY;
    u8 found;

    for (i = 0; i < 10; i++) {
        if (gShownTargets[i]) {
            minX = 0x7FFF, maxX = -0x8000;
            minY = 0x7FFF, maxY = -0x8000;
            pts = gShownTargets[i];
            for (j = 0; j < 4; j++) {
                hud_project_point(arg0, pts[0], pts[1], pts[2], &sx, &sy, 0, 0, 0, 1.0f);
                pts += 3;
                if (sx < minX) {
                    minX = sx;
                }
                if (sx > maxX) {
                    maxX = sx;
                }
                if (sy < minY) {
                    minY = sy;
                }
                if (sy > maxY) {
                    maxY = sy;
                }
            }
            j = 0;
            found = 0;
            while (j < 4 && !found) {
                switch (j) {
                    case 0:
                        px = (maxX - minX) / 2 + minX;
                        py = minY;
                        break;
                    case 1:
                        px = (maxX - minX) / 2 + minX;
                        py = maxY;
                        break;
                    case 2:
                        px = minX;
                        py = (maxY - minY) / 2 + minY;
                        break;
                    case 3:
                        px = maxX;
                        py = (maxY - minY) / 2 + minY;
                        break;
                }
                if (px < 310 && px >= 11 && py < 230 && py >= 11) {
                    found = 1;
                }
                j++;
            }
            if (!found) {
                gShownTargets[i] = NULL;
            }
        }
    }
}

/* Adds the marked target to the shown list (0x8027684C). */
void hud_remember_marked_target(void) {
    s32 i;

    i = 0;
    while (i < 10) {
        if (!gShownTargets[i]) {
            gShownTargets[i] = gMarkedTargetPts;
            return;
        }
        i++;
    }
}

/* 1 if the nearest target was already marked (0x802768A8). */
s32 hud_target_was_shown(void) {
    s32 i;

    i = 0;
    while (i < 10) {
        if (gShownTargets[i] && gShownTargets[i] == gNearestTargetPts) {
            return 1;
        }
        i++;
    }
    return 0;
}

/* A world point to screen x, y (0x4000 if behind) (0x8027690C).
 * Projects a point to screen coordinates, clamped to +-0x4000 (0x4000 when behind the camera) */
void hud_project_point(void *arg0, f32 x, f32 y, f32 z, s16 *sx, s16 *sy, Mtx *arg6, Mtx *arg7, Mtx *arg8, f32 arg9) {
    f32 w = 1.0f;

    if (arg8 != NULL) {
        hud_mtx_xform(arg8, x, y, z, w, &x, &y, &z, &w);
    }
    if (arg7 != NULL) {
        hud_mtx_xform(arg7, x, y, z, w, &x, &y, &z, &w);
    }
    if (arg6 != NULL) {
        hud_mtx_xform(arg6, x, y, z, w, &x, &y, &z, &w);
    }
    hud_mtx_xform((Mtx *) ((u8 *) arg0 + 0x140), x, y, z, w, &x, &y, &z, &w);
    if (z >= 0.0) {
        *sx = 0x4000;
        *sy = 0x4000;
        return;
    }
    hud_mtx_xform((Mtx *) ((u8 *) arg0 + 0x80), x, y, z, w, &x, &y, &z, &w);
    x = x * (gPerspNorm / 65535.0);
    y = y * (gPerspNorm / 65535.0);
    w = w * (gPerspNorm / 65535.0);
    x = x / w;
    y = y / w;
    x = ((320.0f * arg9) / 2.0f) * x;
    y = ((240.0f * arg9) / 2.0f) * y;
    x = ((320.0f * arg9) / 2.0f) + x;
    y = ((240.0f * arg9) / 2.0f) + y;
    y = (240.0f * arg9) - y;
    if (((x > 0.0f) ? x : -x) >= 16384.0f) {
        x = ((x >= 0.0f) ? 1 : -1) << 14;
    }
    if (((y > 0.0f) ? y : -y) >= 16384.0f) {
        y = ((y >= 0.0f) ? 1 : -1) << 14;
    }
    *sx = x;
    *sy = y;
}

/* A fixed-point Mtx times (x, y, z, w) (0x80276D1C). */
void hud_mtx_xform(Mtx *m, f32 x, f32 y, f32 z, f32 w, f32 *ox, f32 *oy, f32 *oz, f32 *ow) {
    f32 mf[4][4];

    guMtxL2F(mf, m);
    *ox = mf[0][0] * x + mf[1][0] * y + mf[2][0] * z + mf[3][0];
    *oy = mf[0][1] * x + mf[1][1] * y + mf[2][1] * z + mf[3][1];
    *oz = mf[0][2] * x + mf[1][2] * y + mf[2][2] * z + mf[3][2];
    *ow = mf[0][3] * x + mf[1][3] * y + mf[2][3] * z + mf[3][3];
}

/* The edge-of-screen marker toward the player (missile view) (0x80276E50).
 * Draws a marker at the screen edge pointing towards an off-screen point (x, y, z in 1/32 units) */
void hud_draw_player_locator(Gfx **gfxp, void *view, u8 idx, s32 x, s32 y, s32 z) {
    s16 sx;
    s16 sy;
    Gfx *gdl = *gfxp;
    f32 mf[4][4];
    f32 rot[4][4];

    hud_project_point(view, x >> 5, y >> 5, z >> 5, &sx, &sy, NULL, NULL, NULL, 1.0f);
    if (sx < 30 || sx >= 291 || sy < 25 || sy >= 216) {
        if (sx < 30) {
            sx = 30;
        }
        if (sy < 25) {
            sy = 25;
        }
        if (sx >= 291) {
            sx = 290;
        }
        if (sy >= 216) {
            sy = 215;
        }
        gLocatorVtx[idx][0].v.ob[0] = -15;
        gLocatorVtx[idx][0].v.ob[1] = -15;
        gLocatorVtx[idx][0].v.ob[2] = -10;
        gLocatorVtx[idx][0].v.tc[0] = 0;
        gLocatorVtx[idx][0].v.tc[1] = 0;
        gLocatorVtx[idx][1].v.ob[0] = 15;
        gLocatorVtx[idx][1].v.ob[1] = -15;
        gLocatorVtx[idx][1].v.ob[2] = -10;
        gLocatorVtx[idx][1].v.tc[0] = 0x3E0;
        gLocatorVtx[idx][1].v.tc[1] = 0;
        gLocatorVtx[idx][2].v.ob[0] = 15;
        gLocatorVtx[idx][2].v.ob[1] = 15;
        gLocatorVtx[idx][2].v.ob[2] = -10;
        gLocatorVtx[idx][2].v.tc[0] = 0x3E0;
        gLocatorVtx[idx][2].v.tc[1] = 0x3E0;
        gLocatorVtx[idx][3].v.ob[0] = -15;
        gLocatorVtx[idx][3].v.ob[1] = 15;
        gLocatorVtx[idx][3].v.ob[2] = -10;
        gLocatorVtx[idx][3].v.tc[0] = 0;
        gLocatorVtx[idx][3].v.tc[1] = 0x3E0;
        guTranslateF(mf, sx, sy, 0.0f);
        guRotateF(rot, 135.0 - (gPlayerMoveHeading / 4095.0) * 360.0, 0.0f, 0.0f, 1.0f);
        guMtxCatF(rot, mf, mf);
        guMtxF2L(mf, &gLocatorMtx[idx]);
        gSPMatrix(gdl++, (u32) seg2Base + 0xC0, G_MTX_PROJECTION | G_MTX_LOAD | G_MTX_NOPUSH);
        gSPMatrix(gdl++, &gLocatorMtx[idx], G_MTX_MODELVIEW | G_MTX_LOAD | G_MTX_NOPUSH);
        gDPPipeSync(gdl++);
        gDPSetCycleType(gdl++, G_CYC_1CYCLE);
        gDPSetRenderMode(gdl++, G_RM_XLU_SURF, G_RM_XLU_SURF2);
        gSPClearGeometryMode(gdl++, 0xFFFFFFFF);
        gSPSetGeometryMode(gdl++, G_SHADE | G_SHADING_SMOOTH);
        gSPTexture(gdl++, 0xFFFF, 0xFFFF, 0, G_TX_RENDERTILE, G_ON);
        gDPSetCombine(gdl++, 0x119623, 0xFF2FFFFF);
        gDPSetPrimColor(gdl++, 0, 0, 0xFF, 0xFF, 0x00, 0xFF);
        gDPLoadTextureBlock(gdl++, (u32) gHudArrowTex - 0x80000000, G_IM_FMT_IA, G_IM_SIZ_8b, 32, 32, 0, G_TX_CLAMP,
                            G_TX_CLAMP, G_TX_NOMASK, G_TX_NOMASK, G_TX_NOLOD, G_TX_NOLOD);
        gSPVertex(gdl++, (u32) gLocatorVtx[idx] - 0x80000000, 4, 0);
        gSP1Triangle(gdl++, 0, 1, 2, 0);
        gSP1Triangle(gdl++, 0, 2, 3, 0);
        gDPPipeSync(gdl++);
    }
    *gfxp = gdl;
}
