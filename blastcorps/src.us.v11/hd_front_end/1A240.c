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
#define gYoshiFeItems ((YoshiItem *) gYoshiFeItems)
#ifdef NON_MATCHING
#define gHeapPtr (*(s32 *) &gHeapPtr)
#endif
/* end of views */

/*
 * Front-end picture screen: four 160x120 RGBA16 images inflated from ROM,
 * one drawn in 32x8 tiles with a fading frame (cross-fades via prim/env alpha).
 */

/* Pre-2.0I texture rectangle: the RDP-half commands are 0xB3/0xB2 (see hd_code 17E10). */
#define OLD_RDPHALF_2 0xB3
#define OLD_RDPHALF_CONT 0xB2

#define gSPTextureRectangleOld(pkt, xl, yl, xh, yh, tile, s, t, dsdx, dtdy)                \
    {                                                                                        \
        Gfx *_g = (Gfx *) (pkt);                                                             \
                                                                                             \
        _g->words.w0 = (_SHIFTL(G_TEXRECT, 24, 8) | _SHIFTL(xh, 12, 12) | _SHIFTL(yh, 0, 12)); \
        _g->words.w1 = (_SHIFTL(tile, 24, 3) | _SHIFTL(xl, 12, 12) | _SHIFTL(yl, 0, 12));    \
        gImmp1(pkt, OLD_RDPHALF_2, (_SHIFTL(s, 16, 16) | _SHIFTL(t, 0, 16)));                \
        gImmp1(pkt, OLD_RDPHALF_CONT, (_SHIFTL(dsdx, 16, 16) | _SHIFTL(dtdy, 0, 16)));       \
    }

extern s16 gAttractPicSounds[];      /* sound per picture */
extern char gAttractPicTitles[][18]; /* picture titles */
extern void * N64P gAttractPicTitleJText[];
#ifndef NON_MATCHING
extern s32 gHeapPtr; /* heap pointer */
#endif

extern u8 * N64P gAttractPics[4]; /* the four inflated pictures */
extern u8 gAttractPicNo;     /* current picture */
extern u8 gAttractPicEnvAlpha;     /* env alpha */
extern u8 gAttractPicPrimAlpha;     /* prim alpha */
extern s32 gAttractPicFade;    /* fade state */
extern s32 gAttractStaticSound;


/* The attract screen's four pictures; selects one (0x80201240).
 * Inflate the pictures and select picture `arg0`. */
void fe_attract_pic_load(s32 arg0) {
    s32 size;
    s32 i;

    size = romTrafficLights - romAttractPictures;
    hd_rom_load((u32) romAttractPictures, (u32) ((void *) N64_IPTR(gHeapPtr)), (u32 *) &size, 13, 0, 1);
    for (i = 0; i < 4; i++) {
        gAttractPics[i] = (u8 *) N64_IPTR((i * 160 * 120 * 2 + gHeapPtr));
    }
    gHeapPtr += size;
    gAttractPicNo = arg0;
    gAttractPicPrimAlpha = 0;
    gAttractPicEnvAlpha = 0;
    gYoshiFeItems[175].text = gAttractPicTitles[arg0];
    gYoshiFeItems[175].jtext = gAttractPicTitleJText[arg0];
}

/* Flicker and cross-fade of the picture, its frame (0x80201364).
 * Draw the current picture and its frame, stepping the fade. */
Gfx *fe_attract_pic_draw(s32 arg0, Gfx *arg1) {
    Gfx *gdl = arg1;
    s32 x;
    s32 y;
    s32 xo;
    s32 yo;

    gDPPipeSync(gdl++);
    gSPTexture(gdl++, 0, 0, 0, 0, G_OFF);
    gDPSetTexturePersp(gdl++, G_TP_NONE);
    gDPSetCycleType(gdl++, G_CYC_2CYCLE);
    gDPSetCombine(gdl++, 0x757E4C, 0xFFFFFA3D);
    switch (yoshiState) {
        case 1:
        case 4:
            gAttractPicFade = 0;
            break;
        case 2:
            switch (gAttractPicFade) {
                case 0:
                    if (gAttractPicEnvAlpha + 3 > 0xFF) {
                        gAttractPicEnvAlpha = 0xFF;
                    } else {
                        gAttractPicEnvAlpha += 3;
                    }
                    if (gAttractPicEnvAlpha < 0x80) {
                        gAttractPicPrimAlpha = gAttractPicEnvAlpha;
                    } else {
                        gAttractPicPrimAlpha = 0xFF - gAttractPicEnvAlpha;
                    }
                    if (gAttractPicEnvAlpha == 0xFF) {
                        gAttractPicFade = 1;
                        snd_play(gSfxBank, gAttractPicSounds[gAttractPicNo], NULL);
                    }
                    break;
                case 1:
                    gAttractPicPrimAlpha = 0;
                    gAttractPicEnvAlpha = 0xFF;
                    if (util_rand_range(0, 40) == 0) {
                        gAttractPicFade = 2;
                    }
                    break;
                case 2:
                    if (util_rand_range(0, 30) == 0 || gAttractPicPrimAlpha == 0xFF) {
                        gAttractPicFade = 3;
                    }
                    if (gAttractPicPrimAlpha + 24 > 0xFF) {
                        gAttractPicPrimAlpha = 0xFF;
                    } else {
                        gAttractPicPrimAlpha += 24;
                    }
                    gAttractPicEnvAlpha = 0xFF - gAttractPicPrimAlpha;
                    break;
                case 3:
                    if (util_rand_range(0, 50) == 0) {
                        gAttractPicFade = 2;
                    }
                    if (gAttractPicPrimAlpha - 32 < 0) {
                        gAttractPicPrimAlpha = 0;
                    } else {
                        gAttractPicPrimAlpha -= 32;
                    }
                    gAttractPicEnvAlpha = 0xFF - gAttractPicPrimAlpha;
                    if (gAttractPicPrimAlpha == 0) {
                        gAttractPicFade = 1;
                    }
                    break;
            }
            if (fe_player_ticker_at_end()) {
                yoshi_demand(YOSHI_DEMAND_OFF);
            }
            break;
        case 8:
            if (gAttractPicEnvAlpha - 6 < 0) {
                gAttractPicEnvAlpha = 0;
            } else {
                gAttractPicEnvAlpha -= 6;
            }
            if (gAttractPicEnvAlpha < 0x80) {
                gAttractPicPrimAlpha = gAttractPicEnvAlpha;
            } else {
                gAttractPicPrimAlpha = 0xFF - gAttractPicEnvAlpha;
            }
            break;
    }
    if (gAttractPicPrimAlpha > 60 && gAttractStaticSound == 0) {
        snd_play(gSfxBank, SFX_STATIC, &gAttractStaticSound);
    }
    gDPSetPrimColor(gdl++, 0, 0, 0, 0, 0, gAttractPicPrimAlpha);
    gDPSetEnvColor(gdl++, 0, 0, 0, gAttractPicEnvAlpha);
    gDPSetRenderMode(gdl++, FORCE_BL | GBL_c1(G_BL_CLR_IN, G_BL_0, G_BL_CLR_IN, G_BL_1),
                     GBL_c2(G_BL_CLR_IN, G_BL_0, G_BL_CLR_IN, G_BL_1));
    xo = 80;
    for (x = 0; x < 160; x += 32) {
        for (yo = 32, y = 0; y < 120; y += 8) {
            gDPLoadTextureTile(gdl++, gAttractPics[gAttractPicNo], G_IM_FMT_RGBA, G_IM_SIZ_16b, 160, 120, x, y,
                               x + 31, y + 7, 0, G_TX_CLAMP, G_TX_CLAMP, G_TX_NOMASK, G_TX_NOMASK, G_TX_NOLOD,
                               G_TX_NOLOD);
            gSPTextureRectangleOld(gdl++, (x + xo) << 2, (y + yo) << 2, (x + xo + 32) << 2, (y + yo + 8) << 2,
                                   G_TX_RENDERTILE, x << 5, y << 5, 1 << 10, 1 << 10);
        }
    }
    gDPPipeSync(gdl++);
    gDPSetCycleType(gdl++, G_CYC_1CYCLE);
    gDPSetRenderMode(gdl++, G_RM_XLU_SURF, G_RM_XLU_SURF2);
    gDPSetPrimColor(gdl++, 0, 0, 200, 200, 200, 255);
    gDPSetCombine(gdl++, 0x11B223, 0xFF67FFFF);
    gDPLoadTextureBlock(gdl++, OS_K0_TO_PHYSICAL(gHudFrameTex), G_IM_FMT_RGBA, G_IM_SIZ_32b, 32, 32, 0,
                        G_TX_MIRROR | G_TX_CLAMP, G_TX_MIRROR, G_TX_NOMASK, 5, G_TX_NOLOD, G_TX_NOLOD);
    /* 200x168 frame from a 32x32 texture: dsdx = 0xA3, dtdy = 0xC3 */
    gSPTextureRectangleOld(gdl++, (xo - 16) << 2, (yo - 24) << 2, (xo + 184) << 2, (yo + 144) << 2,
                           G_TX_RENDERTILE, 0, 32 << 5, 0xA3, 0xC3);
    gDPPipeSync(gdl++);
    gDPSetTexturePersp(gdl++, G_TP_PERSP);
    return gdl;
}
