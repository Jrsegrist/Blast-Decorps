#include "common.h"
#include <ultra64.h>
#include "game/game.h"

extern f32 sqrtf(f32);
extern u16 gTargetArrowTex[]; /* 32x32 RGBA16 arrow texture */
extern Vtx gTargetArrowVtx[];

/* The 3D arrow toward the next target (0x80282C80).
 *
 * Draws the on-screen arrow pointing from the player towards (x, z), tinted
 * green-to-red by the player's distance from (x2, z2) and blinking when within
 * 250 units. Positions are scaled down by 32 first; y and y2 are unused.
 */
void hud_draw_target_arrow(Gfx **gfxp, Mtx *mtx, s32 x, s32 y, s32 z, s32 x2, s32 y2, s32 z2) {
    Gfx *gfx;
    f32 dist;
#ifdef NON_MATCHING
    f32 angle = 0; /* every quadrant test sets it; never read unset */
#else
    f32 angle;
#endif
    u8 r;
    u8 g;
    s16 t;
    f32 mf[4][4];
    f32 mf2[4][4];
    s32 px;
    s32 pz;
    u8 close;

    gfx = *gfxp;
    x >>= 5;
    y >>= 5;
    z >>= 5;
    x2 >>= 5;
    y2 >>= 5;
    z2 >>= 5;
    px = gCarrierTargetX >> 5;
    pz = gCarrierTargetZ >> 5;
    dist = sqrtf((px - x) * (px - x) + (pz - z) * (pz - z));
    if (dist < 1.0) {
        dist = 1.0f;
    }
    if (px >= x && pz >= z) {
        angle = math_asin_c((f32) (px - x) / dist * 65536.0) >> 4;
    }
    if (px >= x && pz < z) {
        angle = (math_asin_c((f32) (z - pz) / dist * 65536.0) >> 4) + 0x400;
    }
    if (px < x && pz < z) {
        angle = (math_asin_c((f32) (x - px) / dist * 65536.0) >> 4) + 0x800;
    }
    if (px < x && pz >= z) {
        angle = (math_asin_c((f32) (pz - z) / dist * 65536.0) >> 4) + 0xC00;
    }
    angle = angle * 0.08791208791208792;
    angle = 360.0 - angle - 45.0;
    angle = angle + (gCamYaw * 360.0 / 4095.0 - 135.0);
    dist = sqrtf((x2 - px) * (x2 - px) + (z2 - pz) * (z2 - pz));
    if (dist > 1500.0f) {
        g = 0xFF;
        r = 0;
    } else if (dist < 500.0f) {
        r = 0xFF;
        g = 0;
    } else {
        t = (dist - 500.0f) / 1000.0f * 511.0f;
        if (t < 0x100) {
            g = t, r = 0xFF;
        } else {
            g = 0xFF, r = 0x1FE - t;
        }
    }
    if (dist < 250.0f) {
        close = 1;
    } else {
        close = 0;
    }
    if ((gScheduler.frameCount % 30 >= 16 || !close) && gCarrierTargetDist != 9999999) {
        guRotateF(mf, 20.0f, 1.0f, 0.0f, 0.0f);
        guRotateF(mf2, -angle, 0.0f, 0.0f, 1.0f);
        guMtxCatF(mf, mf2, mf);
        guTranslateF(mf2, -150.0f, -230.0f, -800.0f);
        guMtxCatF(mf, mf2, mf);
        guMtxF2L(mf, &mtx[0x56]);
        gSPMatrix(gfx++, &seg2Base[2], G_MTX_PROJECTION | G_MTX_LOAD | G_MTX_NOPUSH);
        gImmp1(gfx++, G_RDPHALF_1, gPerspNorm);
        gSPMatrix(gfx++, &seg2Base[0x56], G_MTX_MODELVIEW | G_MTX_LOAD | G_MTX_NOPUSH);
        gSPClearGeometryMode(gfx++, 0xFFFFFFFF);
        gSPSetGeometryMode(gfx++, 0x61204);
        gDPPipeSync(gfx++);
        gDPSetCycleType(gfx++, G_CYC_1CYCLE);
        if (gHudAlpha == 0xFF) {
            gDPSetRenderMode(gfx++, 0x00552008, 0);
        } else {
            gDPSetRenderMode(gfx++, 0x005041C8, 0);
        }
        gDPSetCombine(gfx++, 0x119623, 0xFF2FFFFF);
        gDPSetPrimColor(gfx++, 0, 0, r, g, 0, gHudAlpha);
        gSPTexture(gfx++, 0x7C0, 0x7C0, 0, G_TX_RENDERTILE, G_ON);
        gDPLoadTextureBlock(gfx++, OS_K0_TO_PHYSICAL(gTargetArrowTex), G_IM_FMT_RGBA, G_IM_SIZ_16b, 32, 32, 0,
                            G_TX_CLAMP, G_TX_CLAMP, G_TX_NOMASK, G_TX_NOMASK, G_TX_NOLOD, G_TX_NOLOD);
        gSPVertex(gfx++, osVirtualToPhysical(gTargetArrowVtx), 10, 0);
        gSP1Triangle(gfx++, 0, 1, 2, 0);
        gSP1Triangle(gfx++, 0, 2, 3, 0);
        gSP1Triangle(gfx++, 3, 1, 0, 0);
        gSP1Triangle(gfx++, 1, 4, 2, 0);
        gSP1Triangle(gfx++, 4, 5, 2, 0);
        gSP1Triangle(gfx++, 5, 3, 2, 0);
        gSP1Triangle(gfx++, 6, 7, 8, 0);
        gSP1Triangle(gfx++, 9, 6, 8, 0);
        gSP1Triangle(gfx++, 7, 9, 8, 0);
        gDPPipeSync(gfx++);
    }
    *gfxp = gfx;
}

f32 hud_heading_deg(s16 x1, s16 z1, s16 x2, s16 z2);
extern u8 gRadarDiscTex[];
extern u8 gRadarGridTex[];
extern u8 gRadarSweepTex[];
extern Vtx gRadarDiscVtx[];
extern Vtx gRadarSweepVtx[];
extern Vtx gRadarBlipVtx[][12]; /* per radar: target blip, player blip, heading arrow quads */
extern f32 gRadarSweepAngle;
extern Mtx gRadarArrowMtx[];

/* The radar: carrier and target blips, heading arrow, sweep (0x8028376C).
 *
 * Draws radar `idx`, centred on (x, z) and rotated with the camera: zoom
 * (scale) and texture shift depend on the distance to (x2, z2); blips for
 * (x2, z2) (blinking when zoomed in) and the player; an arrow pointing toward
 * (gEndObjectX, gEndObjectZ); and a spinning sweep overlay.
 */
void hud_draw_radar(Gfx **gfxp, Mtx *mtx, u8 idx, s32 x, s32 z, s32 x2, s32 z2) {
    Gfx *gfx;
    s32 dist;
    u8 shift;
    s32 scale;
    s16 sx;
    s16 sy;
    s16 px;
    s16 py;
    Mtx rot;
    f32 mf[4][4];
    f32 mf2[4][4];
    f32 ox;
    f32 oy;
    f32 oz;
    f32 heading;

    gfx = *gfxp;
    dist = util_dist2d(x, z, x2, z2);
    if (dist < 0x55F0) {
        shift = 10;
        scale = 1200;
    }
    if (dist >= 0x55F0 && dist < 0xABE0) {
        shift = 0;
        scale = 2400;
    }
    if (dist >= 0xABE0) {
        shift = 15;
        scale = 4800;
    }
    if (gHaveCarrier == 0) {
        shift = 10;
        scale = 1200;
    }
    guRotateF(mf, 360.0 - (gCamHeading - 180.0), 0.0f, 1.0f, 0.0f);
    guMtxXFMF(mf, (x - x2) / scale, 0.0f, (z - z2) / scale, &ox, &oy, &oz);
    sx = 58.0f + ox;
    sy = 195.0f + oz;
    gRadarBlipVtx[idx][0].v.ob[0] = sx - 2;
    gRadarBlipVtx[idx][0].v.ob[1] = sy - 2;
    gRadarBlipVtx[idx][1].v.ob[0] = sx - 2;
    gRadarBlipVtx[idx][1].v.ob[1] = sy + 1;
    gRadarBlipVtx[idx][2].v.ob[0] = sx + 1;
    gRadarBlipVtx[idx][2].v.ob[1] = sy + 1;
    gRadarBlipVtx[idx][3].v.ob[0] = sx + 1;
    gRadarBlipVtx[idx][3].v.ob[1] = sy - 2;
    obj_find_carrier_target();
    guMtxXFMF(mf, (x - gCarrierTargetX) / scale, 0.0f, (z - gCarrierTargetZ) / scale, &ox, &oy, &oz);
    px = 58.0f + ox;
    py = 195.0f + oz;
    gRadarBlipVtx[idx][4].v.ob[0] = px - 2;
    gRadarBlipVtx[idx][4].v.ob[1] = py - 2;
    gRadarBlipVtx[idx][5].v.ob[0] = px - 2;
    gRadarBlipVtx[idx][5].v.ob[1] = py + 1;
    gRadarBlipVtx[idx][6].v.ob[0] = px + 1;
    gRadarBlipVtx[idx][6].v.ob[1] = py + 1;
    gRadarBlipVtx[idx][7].v.ob[0] = px + 1;
    gRadarBlipVtx[idx][7].v.ob[1] = py - 2;

    PORT_ITAG(gfx, PORT_ITAG_RADAR, 0);
    gSPMatrix(gfx++, &seg2Base[3], G_MTX_PROJECTION | G_MTX_LOAD | G_MTX_NOPUSH);
    gSPMatrix(gfx++, &seg2Base[7], G_MTX_MODELVIEW | G_MTX_LOAD | G_MTX_NOPUSH);
    gSPClearGeometryMode(gfx++, 0xFFFFFFFF);
    gSPSetGeometryMode(gfx++, G_SHADE | G_SHADING_SMOOTH);
    gDPPipeSync(gfx++);
    gDPSetTextureLOD(gfx++, G_TL_TILE);
    gSPTexture(gfx++, 0xFFFF, 0xFFFF, 0, G_TX_RENDERTILE, G_ON);
    gDPSetTextureImage(gfx++, G_IM_FMT_IA, G_IM_SIZ_16b, 1, OS_K0_TO_PHYSICAL(gRadarDiscTex));
    gDPSetTile(gfx++, G_IM_FMT_IA, G_IM_SIZ_16b, 0, 0, G_TX_LOADTILE, 0, 0, 0, 0, 0, 0, 0);
    gDPLoadSync(gfx++);
    gDPLoadBlock(gfx++, G_TX_LOADTILE, 0, 0, 0x7F, 0x400);
    gDPSetTextureImage(gfx++, G_IM_FMT_IA, G_IM_SIZ_16b, 1, OS_K0_TO_PHYSICAL(gRadarGridTex));
    gDPTileSync(gfx++);
    gDPSetTile(gfx++, G_IM_FMT_IA, G_IM_SIZ_16b, 0, 0x100, G_TX_LOADTILE, 0, 0, 0, 0, 0, 0, 0);
    gDPLoadSync(gfx++);
    gDPLoadBlock(gfx++, G_TX_LOADTILE, 0, 0, 0x3FF, 0x200);
    gDPSetTile(gfx++, G_IM_FMT_IA, G_IM_SIZ_8b, 2, 0, G_TX_RENDERTILE, 0, G_TX_MIRROR, 4, 0, G_TX_MIRROR, 4, 0);
    gDPSetTileSize(gfx++, G_TX_RENDERTILE, 2, 2, 0x3E, 0x3E);
    gDPSetTile(gfx++, G_IM_FMT_IA, G_IM_SIZ_4b, 4, 0x100, 1, 0, G_TX_MIRROR, 6, shift, G_TX_MIRROR, 6, shift);
    gDPSetTileSize(gfx++, 1, 2, 2, 0xFC, 0xFC);
    gDPSetCycleType(gfx++, G_CYC_2CYCLE);
    gDPSetRenderMode(gfx++, 0x0C184340, 0);
    gDPSetCombine(gfx++, 0x3497FF, 0x4FFE7E38);
    gDPSetPrimColor(gfx++, 0, 0, 0xFF, 0xFF, 0xFF, (gHudAlpha < 0x3F) ? gHudAlpha : 0x3F);
    gSPVertex(gfx++, osVirtualToPhysical(gRadarDiscVtx), 4, 0);
    gSP1Triangle(gfx++, 0, 1, 2, 0);
    gSP1Triangle(gfx++, 0, 2, 3, 0);
    gSPTexture(gfx++, 0xFFFF, 0xFFFF, 0, G_TX_RENDERTILE, G_OFF);
    gDPPipeSync(gfx++);
    gDPSetCycleType(gfx++, G_CYC_1CYCLE);
    gDPSetCombine(gfx++, 0xFFFFFF, 0xFFFE773B);
    gDPSetPrimColor(gfx++, 0, 0, 0, 0, 0, gHudAlpha);
    if (gHaveCarrier != 0) {
        gDPSetRenderMode(gfx++, 0x005041C8, 0);
        if ((gScheduler.frameCount % 40 > 20 || shift != 10) && sx >= 0x22 && sx < 0x53 && sy >= 0xAB && sy < 0xDC) {
            gSPVertex(gfx++, osVirtualToPhysical(gRadarBlipVtx[idx]), 4, 0);
            gSP1Triangle(gfx++, 0, 1, 2, 0);
            gSP1Triangle(gfx++, 0, 2, 3, 0);
        }
        if (px >= 0x22 && px < 0x53 && py >= 0xB0 && py < 0xD7 && gCarrierTargetDist != 9999999) {
            gSPVertex(gfx++, osVirtualToPhysical(&gRadarBlipVtx[idx][4]), 4, 0);
            gSP1Triangle(gfx++, 0, 1, 2, 0);
            gSP1Triangle(gfx++, 0, 2, 3, 0);
        }
    }
    heading = hud_heading_deg(x >> 5, z >> 5, gEndObjectX, gEndObjectZ);
    guRotateF(mf, -((360.0 - (gCamHeading - 180.0)) + (heading + 180.0)), 0.0f, 0.0f, 1.0f);
    guTranslateF(mf2, 58.0f, 195.0f, 0.0f);
    guMtxCatF(mf, mf2, mf);
    guMtxF2L(mf, &gRadarArrowMtx[idx]);
    gDPPipeSync(gfx++);
    if (gHudAlpha == 0xFF) {
        gDPSetRenderMode(gfx++, 0x00552048, 0);
    } else {
        gDPSetRenderMode(gfx++, 0x005041C8, 0);
    }
    gSPMatrix(gfx++, osVirtualToPhysical(&gRadarArrowMtx[idx]), G_MTX_MODELVIEW | G_MTX_MUL | G_MTX_PUSH);
    gSPVertex(gfx++, osVirtualToPhysical(&gRadarBlipVtx[idx][8]), 4, 0);
    gSP1Triangle(gfx++, 0, 1, 2, 0);
    gSP1Triangle(gfx++, 0, 2, 3, 0);
    gSPPopMatrix(gfx++, G_MTX_MODELVIEW);
    if (gHaveCarrier != 0) {
        gSPTexture(gfx++, 0xFFFF, 0xFFFF, 0, G_TX_RENDERTILE, G_ON);
        gDPPipeSync(gfx++);
        gDPSetCycleType(gfx++, G_CYC_1CYCLE);
        gDPSetRenderMode(gfx++, 0x00504340, 0);
        gDPSetCombine(gfx++, 0xFF97FF, 0xFF2E7F3F);
        gDPSetPrimColor(gfx++, 0, 0, 0, 0, 0, (gHudAlpha >= 0x80) ? 0x7F : gHudAlpha);
        gDPLoadTextureBlock(gfx++, OS_K0_TO_PHYSICAL(gRadarSweepTex), G_IM_FMT_IA, G_IM_SIZ_8b, 8, 32, 0,
                            G_TX_CLAMP, G_TX_CLAMP, G_TX_NOMASK, G_TX_NOMASK, G_TX_NOLOD, G_TX_NOLOD);
        guRotate(&mtx[23], gRadarSweepAngle, 0.0f, 0.0f, 1.0f);
        guTranslate(&rot, 58.0f, 195.0f, 0.0f);
        guMtxCatL(&mtx[23], &rot, &mtx[23]);
        gRadarSweepAngle += 4.0;
        gSPMatrix(gfx++, &seg2Base[23], G_MTX_MODELVIEW | G_MTX_LOAD | G_MTX_NOPUSH);
        gSPVertex(gfx++, osVirtualToPhysical(gRadarSweepVtx), 4, 0);
        gSP1Triangle(gfx++, 0, 1, 2, 0);
        gSP1Triangle(gfx++, 1, 2, 3, 0);
    }
    PORT_ITAG(gfx, PORT_ITAG_HUD, 0);
    gDPPipeSync(gfx++);
    *gfxp = gfx;
}

/* Heading 0..360 from one point to another (0x80284ADC).
 *
 * Heading in degrees (0..360) from (x1, z1) to (x2, z2), one quadrant at a time.
 * math_asin_c maps a 0..65535 sine to a 0..0x3FFF binary angle. No return if
 * no quadrant test passes (original falls off the end).
 */
f32 hud_heading_deg(s16 x1, s16 z1, s16 x2, s16 z2) {
    f32 dist;

    dist = sqrtf((x2 - x1) * (x2 - x1) + (z2 - z1) * (z2 - z1));
    if (dist < 1.0) {
        return 0.0f;
    }
    if (x2 >= x1 && z2 >= z1) {
        return math_asin_c((x2 - x1) * 65535.9 / dist) / 65536.0 * 360.0;
    }
    if (x2 >= x1 && z2 < z1) {
        return (math_asin_c((z1 - z2) * 65535.9 / dist) + 0x4000) / 65536.0 * 360.0;
    }
    if (x2 < x1 && z2 < z1) {
        return (math_asin_c((x1 - x2) * 65535.9 / dist) + 0x8000) / 65536.0 * 360.0;
    }
    if (x2 < x1 && z2 >= z1) {
        return (math_asin_c((z2 - z1) * 65535.9 / dist) + 0xC000) / 65536.0 * 360.0;
    }
#ifdef NON_MATCHING
    return 0.0f; /* not reached: the four tests cover every case */
#endif
}
