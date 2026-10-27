#include "common.h"
#include <ultra64.h>
#ifndef NON_MATCHING /* legacy declarations */
/* Matching build: IDO compiled the matched code here against older
 * declarations of these, which the file keeps; the NON_MATCHING build
 * uses game/game.h's. */
#define LEGACY_gHeapPtr
#endif /* legacy declarations */
#include "game/game.h"
/* Views: this file reads these shared variables (game/variables.h) as other types. */
#ifdef NON_MATCHING
#define gHeapPtr (*(u32 *) &gHeapPtr)
#endif
/* end of views */

/* HUD panels drawn over the game: a 40x40 textured dial with a needle
 * (rotated by gSkyfallBoostMeter, 0..100 -> 270..450 degrees), and two 32x32 icons
 * with a number printed beside them. Each panel fades in (+10 per frame up
 * to 0xFF) while its mode argument matches and fades out otherwise; the
 * alpha is also capped by gHudAlpha. */

#define MIN(a, b) ((a) < (b) ? (a) : (b))

#ifdef PORT_HOST
/* The quads below are built in fresh heap memory, and their flag and colour
 * bytes (and the needle's texture coordinates) are never written: the RSP
 * shades the dial and the icons with whatever bytes the heap held there (an
 * original slip).  port_vtx_stale (port/src/load/swap.c) first gives the
 * native records the N64's stale bytes, so they draw (and compare) the same. */
void port_vtx_stale(void *v, u32 n);
#endif

#ifndef NON_MATCHING
extern u32 gHeapPtr; /* heap pointer */
#endif

/* dial */
extern u32 gHudDialTex; /* texture */
extern Vtx * N64P gHudDialVtx; /* 4 dial + 4 needle vertices */
extern Mtx * N64P gHudDialNeedleRotMtx; /* needle rotation, one per frame */
extern Mtx * N64P gHudDialNeedlePosMtx; /* needle translation, one per frame */
extern s16 gHudDialAlpha;  /* alpha */
/* icon 1 */
extern u32 gHudMissileIconTex;
extern Vtx * N64P gHudMissileIconVtx;
extern s16 gHudMissileIconAlpha;
/* icon 2 */
extern u32 gHudRamIconTex;
extern Vtx * N64P gHudRamIconVtx;
extern s16 gHudRamIconAlpha;


/* Skyfall's boost dial (0x80286A00).
 * Load the dial texture (0x760) and build its vertices */
void hud_init_boost_dial(void) {
    gHudDialLoaded = 1;
    gHudDialTex = gHeapPtr;
    tex_get(0x760, 0);
    gHudDialNeedleRotMtx = (Mtx *) gHeapPtr;
    gHudDialNeedlePosMtx = (Mtx *) (gHeapPtr += 0x80);
    gHudDialVtx = (Vtx *) (gHeapPtr += 0x80);
    gHeapPtr += 0x80;
#ifdef PORT_HOST
    port_vtx_stale(gHudDialVtx, 8);
#endif
    gHudDialVtx[0].v.ob[0] = 38;
    gHudDialVtx[0].v.ob[1] = 123;
    gHudDialVtx[0].v.ob[2] = -5;
    gHudDialVtx[0].v.tc[0] = 0;
    gHudDialVtx[0].v.tc[1] = 1248;
    gHudDialVtx[1].v.ob[0] = 77;
    gHudDialVtx[1].v.ob[1] = 123;
    gHudDialVtx[1].v.ob[2] = -5;
    gHudDialVtx[1].v.tc[0] = 1248;
    gHudDialVtx[1].v.tc[1] = 1248;
    gHudDialVtx[2].v.ob[0] = 77;
    gHudDialVtx[2].v.ob[1] = 173;
    gHudDialVtx[2].v.ob[2] = -5;
    gHudDialVtx[2].v.tc[0] = 1248;
    gHudDialVtx[2].v.tc[1] = 0;
    gHudDialVtx[3].v.ob[0] = 38;
    gHudDialVtx[3].v.ob[1] = 173;
    gHudDialVtx[3].v.ob[2] = -5;
    gHudDialVtx[3].v.tc[0] = 0;
    gHudDialVtx[3].v.tc[1] = 0;
    gHudDialVtx[4].v.ob[0] = -1;
    gHudDialVtx[4].v.ob[1] = 0;
    gHudDialVtx[4].v.ob[2] = -5;
    gHudDialVtx[5].v.ob[0] = 1;
    gHudDialVtx[5].v.ob[1] = 0;
    gHudDialVtx[5].v.ob[2] = -5;
    gHudDialVtx[6].v.ob[0] = -1;
    gHudDialVtx[6].v.ob[1] = -17;
    gHudDialVtx[6].v.ob[2] = -5;
    gHudDialVtx[7].v.ob[0] = 1;
    gHudDialVtx[7].v.ob[1] = -17;
    gHudDialVtx[7].v.ob[2] = -5;
    gHudDialAlpha = 0;
}

/* The dial's needle from gSkyfallBoostMeter (0x80286C60).
 * Draw the dial while arg3 == 3 or it is still fading out */
void hud_draw_boost_dial(Gfx **gfxp, s32 arg1, u8 frame, u8 arg3) {
    Gfx *gdl = *gfxp;

    if (arg3 != 3 && gHudDialAlpha == 0) {
        return;
    }
    if (arg3 == 3 && gHudDialAlpha < 0xFF) {
        gHudDialAlpha += 10;
        if (gHudDialAlpha > 0xFF) {
            gHudDialAlpha = 0xFF;
        }
    }
    if (arg3 != 3 && gHudDialAlpha != 0) {
        gHudDialAlpha -= 10;
        if (gHudDialAlpha < 0) {
            gHudDialAlpha = 0;
        }
    }
    gSPMatrix(gdl++, &seg2Base[3], G_MTX_PROJECTION | G_MTX_LOAD | G_MTX_NOPUSH);
    gSPMatrix(gdl++, &seg2Base[7], G_MTX_MODELVIEW | G_MTX_LOAD | G_MTX_NOPUSH);
    gDPPipeSync(gdl++);
    gDPSetCycleType(gdl++, G_CYC_1CYCLE);
    gDPSetRenderMode(gdl++, G_RM_XLU_SURF, G_RM_XLU_SURF2);
    gSPClearGeometryMode(gdl++, 0xFFFFFFFF);
    gSPSetGeometryMode(gdl++, G_SHADE | G_SHADING_SMOOTH);
    gSPTexture(gdl++, 0xFFFF, 0xFFFF, 0, G_TX_RENDERTILE, G_ON);
    gDPSetPrimColor(gdl++, 0, 0, 0xFF, 0xFF, 0xFF, MIN(gHudDialAlpha, gHudAlpha));
    gDPSetCombineMode(gdl++, G_CC_MODULATERGBA_PRIM, G_CC_MODULATERGBA_PRIM);
    gDPLoadTextureBlock(gdl++, OS_K0_TO_PHYSICAL(gHudDialTex), G_IM_FMT_RGBA, G_IM_SIZ_16b, 40, 40, 0,
                        G_TX_CLAMP, G_TX_CLAMP, G_TX_NOMASK, G_TX_NOMASK, G_TX_NOLOD, G_TX_NOLOD);
    gSPVertex(gdl++, OS_K0_TO_PHYSICAL(gHudDialVtx), 4, 0);
    gSP1Triangle(gdl++, 0, 1, 2, 0);
    gSP1Triangle(gdl++, 0, 2, 3, 0);
    gDPPipeSync(gdl++);
    gSPTexture(gdl++, 0xFFFF, 0xFFFF, 0, G_TX_RENDERTILE, G_OFF);
    gDPSetRenderMode(gdl++, G_RM_AA_XLU_SURF, G_RM_AA_XLU_SURF2);
    gDPSetCombineMode(gdl++, G_CC_PRIMITIVE, G_CC_PRIMITIVE);
    guTranslate(&gHudDialNeedlePosMtx[frame], 56.0f, 151.0f, 0.0f);
    gSPMatrix(gdl++, OS_K0_TO_PHYSICAL(&gHudDialNeedlePosMtx[frame]), G_MTX_MODELVIEW | G_MTX_MUL | G_MTX_PUSH);
    guRotate(&gHudDialNeedleRotMtx[frame], (u32) gSkyfallBoostMeter / 100.0f * 180.0 + 270.0, 0.0f, 0.0f, 1.0f);
    gSPMatrix(gdl++, OS_K0_TO_PHYSICAL(&gHudDialNeedleRotMtx[frame]), G_MTX_MODELVIEW | G_MTX_MUL | G_MTX_NOPUSH);
    gSPVertex(gdl++, OS_K0_TO_PHYSICAL(&gHudDialVtx[4]), 4, 0);
    gSP1Triangle(gdl++, 0, 1, 2, 0);
    gSP1Triangle(gdl++, 1, 2, 3, 0);
    gSPPopMatrix(gdl++, G_MTX_MODELVIEW);
    gDPPipeSync(gdl++);
    *gfxp = gdl;
}

/* Ballista's missiles icon (0x802873AC).
 * Load icon 1 (0x996) and build its quad */
void hud_init_missile_icon(void) {
    gHudMissileIconLoaded = 1;
    gHudMissileIconTex = gHeapPtr;
    tex_get(0x996, 0);
    gHudMissileIconVtx = (Vtx *) gHeapPtr;
    gHeapPtr += 0x40;
#ifdef PORT_HOST
    port_vtx_stale(gHudMissileIconVtx, 4);
#endif
    gHudMissileIconVtx[0].v.ob[0] = 34;
    gHudMissileIconVtx[0].v.ob[1] = 135;
    gHudMissileIconVtx[0].v.ob[2] = -5;
    gHudMissileIconVtx[0].v.tc[0] = 0;
    gHudMissileIconVtx[0].v.tc[1] = 992;
    gHudMissileIconVtx[1].v.ob[0] = 64;
    gHudMissileIconVtx[1].v.ob[1] = 135;
    gHudMissileIconVtx[1].v.ob[2] = -5;
    gHudMissileIconVtx[1].v.tc[0] = 992;
    gHudMissileIconVtx[1].v.tc[1] = 992;
    gHudMissileIconVtx[2].v.ob[0] = 64;
    gHudMissileIconVtx[2].v.ob[1] = 165;
    gHudMissileIconVtx[2].v.ob[2] = -5;
    gHudMissileIconVtx[2].v.tc[0] = 992;
    gHudMissileIconVtx[2].v.tc[1] = 0;
    gHudMissileIconVtx[3].v.ob[0] = 34;
    gHudMissileIconVtx[3].v.ob[1] = 165;
    gHudMissileIconVtx[3].v.ob[2] = -5;
    gHudMissileIconVtx[3].v.tc[0] = 0;
    gHudMissileIconVtx[3].v.tc[1] = 0;
    gHudMissileIconAlpha = 0;
}

/* The icon and gBallistaMissiles (0x80287530).
 * Draw icon 1 and its number (gBallistaMissiles) while arg3 == 10 or still fading out */
void hud_draw_missile_icon(Gfx **gfxp, Gfx * N64P *gfxp2, u8 frame, u8 arg3) {
    Gfx *gdl = *gfxp;
    char buf[20];

    if (arg3 != 10 && gHudMissileIconAlpha == 0) {
        return;
    }
    if (arg3 == 10 && gHudMissileIconAlpha < 0xFF) {
        gHudMissileIconAlpha += 10;
        if (gHudMissileIconAlpha > 0xFF) {
            gHudMissileIconAlpha = 0xFF;
        }
    }
    if (arg3 != 10 && gHudMissileIconAlpha != 0) {
        gHudMissileIconAlpha -= 10;
        if (gHudMissileIconAlpha < 0) {
            gHudMissileIconAlpha = 0;
        }
    }
    gSPMatrix(gdl++, &seg2Base[3], G_MTX_PROJECTION | G_MTX_LOAD | G_MTX_NOPUSH);
    gSPMatrix(gdl++, &seg2Base[7], G_MTX_MODELVIEW | G_MTX_LOAD | G_MTX_NOPUSH);
    gDPPipeSync(gdl++);
    gDPSetCycleType(gdl++, G_CYC_1CYCLE);
    gDPSetRenderMode(gdl++, G_RM_XLU_SURF, G_RM_XLU_SURF2);
    gSPClearGeometryMode(gdl++, 0xFFFFFFFF);
    gSPSetGeometryMode(gdl++, G_SHADE | G_SHADING_SMOOTH);
    gSPTexture(gdl++, 0xFFFF, 0xFFFF, 0, G_TX_RENDERTILE, G_ON);
    gDPSetPrimColor(gdl++, 0, 0, 0xFF, 0xFF, 0xFF, MIN(gHudMissileIconAlpha, gHudAlpha));
    gDPSetCombineMode(gdl++, G_CC_MODULATERGBA_PRIM, G_CC_MODULATERGBA_PRIM);
    gDPLoadTextureBlock(gdl++, OS_K0_TO_PHYSICAL(gHudMissileIconTex), G_IM_FMT_RGBA, G_IM_SIZ_16b, 32, 32, 0,
                        G_TX_CLAMP, G_TX_CLAMP, G_TX_NOMASK, G_TX_NOMASK, G_TX_NOLOD, G_TX_NOLOD);
    gSPVertex(gdl++, OS_K0_TO_PHYSICAL(gHudMissileIconVtx), 4, 0);
    gSP1Triangle(gdl++, 0, 1, 2, 0);
    gSP1Triangle(gdl++, 0, 2, 3, 0);
    gDPPipeSync(gdl++);
    util_itoa(gBallistaMissiles, (u8 *) buf);
    text_print_gradient(gfxp2, (u8 *) buf, 0, 1, 0, 0x40, 0x91, 15, 15, 1, 0xFF, 0xFF, 0, MIN(gHudMissileIconAlpha, gHudAlpha), 0xFF, 0, 0,
                  MIN(gHudMissileIconAlpha, gHudAlpha));
    gDPPipeSync(gdl++);
    *gfxp = gdl;
}

/* Sideswipe's side-ram icon (0x80287AE4).
 * Load icon 2 (0x998) and build its quad */
void hud_init_ram_icon(void) {
    gHudRamIconLoaded = 1;
    gHudRamIconTex = gHeapPtr;
    tex_get(0x998, 0);
    gHudRamIconVtx = (Vtx *) gHeapPtr;
    gHeapPtr += 0x40;
#ifdef PORT_HOST
    port_vtx_stale(gHudRamIconVtx, 4);
#endif
    gHudRamIconVtx[0].v.ob[0] = 34;
    gHudRamIconVtx[0].v.ob[1] = 145;
    gHudRamIconVtx[0].v.ob[2] = -5;
    gHudRamIconVtx[0].v.tc[0] = 0;
    gHudRamIconVtx[0].v.tc[1] = 992;
    gHudRamIconVtx[1].v.ob[0] = 64;
    gHudRamIconVtx[1].v.ob[1] = 145;
    gHudRamIconVtx[1].v.ob[2] = -5;
    gHudRamIconVtx[1].v.tc[0] = 992;
    gHudRamIconVtx[1].v.tc[1] = 992;
    gHudRamIconVtx[2].v.ob[0] = 64;
    gHudRamIconVtx[2].v.ob[1] = 175;
    gHudRamIconVtx[2].v.ob[2] = -5;
    gHudRamIconVtx[2].v.tc[0] = 992;
    gHudRamIconVtx[2].v.tc[1] = 0;
    gHudRamIconVtx[3].v.ob[0] = 34;
    gHudRamIconVtx[3].v.ob[1] = 175;
    gHudRamIconVtx[3].v.ob[2] = -5;
    gHudRamIconVtx[3].v.tc[0] = 0;
    gHudRamIconVtx[3].v.tc[1] = 0;
    gHudRamIconAlpha = 0;
}

/* The icon and gSideswipeRamCharges (0x80287C68).
 * Draw icon 2 and its number (gSideswipeRamCharges) while arg3 == 1 or still fading out */
void hud_draw_ram_icon(Gfx **gfxp, Gfx * N64P *gfxp2, u8 frame, u8 arg3) {
    Gfx *gdl = *gfxp;
    char buf[20];

    if (arg3 != 1 && gHudRamIconAlpha == 0) {
        return;
    }
    if (arg3 == 1 && gHudRamIconAlpha < 0xFF) {
        gHudRamIconAlpha += 10;
        if (gHudRamIconAlpha > 0xFF) {
            gHudRamIconAlpha = 0xFF;
        }
    }
    if (arg3 != 1 && gHudRamIconAlpha != 0) {
        gHudRamIconAlpha -= 10;
        if (gHudRamIconAlpha < 0) {
            gHudRamIconAlpha = 0;
        }
    }
    gSPMatrix(gdl++, &seg2Base[3], G_MTX_PROJECTION | G_MTX_LOAD | G_MTX_NOPUSH);
    gSPMatrix(gdl++, &seg2Base[7], G_MTX_MODELVIEW | G_MTX_LOAD | G_MTX_NOPUSH);
    gDPPipeSync(gdl++);
    gDPSetCycleType(gdl++, G_CYC_1CYCLE);
    gDPSetRenderMode(gdl++, G_RM_XLU_SURF, G_RM_XLU_SURF2);
    gSPClearGeometryMode(gdl++, 0xFFFFFFFF);
    gSPSetGeometryMode(gdl++, G_SHADE | G_SHADING_SMOOTH);
    gSPTexture(gdl++, 0xFFFF, 0xFFFF, 0, G_TX_RENDERTILE, G_ON);
    gDPSetPrimColor(gdl++, 0, 0, 0xFF, 0xFF, 0xFF, MIN(gHudRamIconAlpha, gHudAlpha));
    gDPSetCombineMode(gdl++, G_CC_MODULATERGBA_PRIM, G_CC_MODULATERGBA_PRIM);
    gDPLoadTextureBlock(gdl++, OS_K0_TO_PHYSICAL(gHudRamIconTex), G_IM_FMT_RGBA, G_IM_SIZ_16b, 32, 32, 0,
                        G_TX_CLAMP, G_TX_CLAMP, G_TX_NOMASK, G_TX_NOMASK, G_TX_NOLOD, G_TX_NOLOD);
    gSPVertex(gdl++, OS_K0_TO_PHYSICAL(gHudRamIconVtx), 4, 0);
    gSP1Triangle(gdl++, 0, 1, 2, 0);
    gSP1Triangle(gdl++, 0, 2, 3, 0);
    gDPPipeSync(gdl++);
    util_itoa(gSideswipeRamCharges, (u8 *) buf);
    text_print_gradient(gfxp2, (u8 *) buf, 0, 1, 0, 0x44, 0x97, 15, 15, 1, 0xFF, 0xFF, 0, MIN(gHudRamIconAlpha, gHudAlpha), 0xFF, 0, 0,
                  MIN(gHudRamIconAlpha, gHudAlpha));
    gDPPipeSync(gdl++);
    *gfxp = gdl;
}
