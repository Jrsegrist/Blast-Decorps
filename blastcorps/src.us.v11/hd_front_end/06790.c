#include "common.h"
#include <ultra64.h>
#include "game/game.h"
/* Views: this file reads these shared variables (game/variables.h) as other types. */
#define gRankTitles ((RankName *) gRankTitles)
#define players ((PlayerInfo *) players)
/* end of views */

/*
 * Promotion screen: "CONGRATULATIONS / ON YOUR PROMOTION!" plus the new
 * rank's name, over a spinning, growing 3D scene (scene 0 of 0DE70.c).
 */


/* (players[]: PlayerInfo, game/types.h) */

typedef struct {
    u8 * N64P unk0; /* rank name, words separated by spaces */
    u8 pad4[4];
} RankName;

extern s32 gPromoScaleState; /* scale state */
extern s32 gPromoTextState; /* text fade state */
extern f32 gPromoAngle; /* scene angle */
extern f32 gPromoScale; /* scene scale */
extern s32 gPromoHoldTimer;
extern s16 gPromoTitleAlpha; /* title alpha */
extern s16 gPromoRankAlpha; /* rank name alpha */
extern s32 gPromoRank; /* rank */
extern u8 gPromoRankLine1[]; /* rank name, first line */
extern u8 gPromoRankLine2[]; /* rank name, second line */
extern s16 gPromoPulse;
extern u16 gPromoTitle1JText[];
extern u16 gPromoTitle2JText[];


/* title lines and their glyph lists */
u8 * N64P gPromoTitle1 = N64_DPTR((u8 *) "CONGRATULATIONS");
u8 * N64P gPromoTitle2 = N64_DPTR((u8 *) "ON YOUR PROMOTION!");
u16 * N64P gPromoTitle1Glyphs = N64_DPTR(gPromoTitle1JText);
u16 * N64P gPromoTitle2Glyphs = N64_DPTR(gPromoTitle2JText);
s8 gPromoPulseDir = 1; /* colour cycle direction */

/* The promotion screen: scene, state, the rank (0x801ED790).
 * Promotion screen init: load scene 0 and reset the screen's state. */
void fe_promo_init(void) {
    fe_scene_load(0);
    gPromoScaleState = 0;
    gPromoTextState = 0;
    gPromoAngle = gPromoScale = 0.0f;
    gPromoTitleAlpha = gPromoRankAlpha = 0;
    gPromoRank = players[playerNumber].title;
}

/* "CONGRATULATIONS / ON YOUR PROMOTION!" and the rank (0x801ED800).
 * Promotion screen frame: animate, draw the text and the scene; *arg3 += commands written. */
Gfx *fe_promo_draw(Gfx *arg0, FeDyn *dyn, u8 arg2, s32 *arg3) {
    Gfx *gdl = arg0;
    s32 i;
    s32 j;
    PlayerInfo *p;
    s32 size;

    if ((gContButtons & A_BUTTON) && !(gContPrevButtons & A_BUTTON) && gDebugCheats) {
        gPromoScaleState = 2;
        gPromoTextState = 3;
    }
    switch (gPromoScaleState) {
        case 0:
            gPromoScale = sins(gFramesInMode * 0x4000 * 60 / 60 / 90) * 2.85 / 32767.0;
            if (gPromoScale >= 2.84) {
                gPromoScale = 2.84f;
                gPromoScaleState = 1;
                gPromoHoldTimer = 0x91;
            }
            break;
        case 1:
            if (gPromoHoldTimer-- == 0) {
                gPromoScaleState = 2;
            }
            break;
        case 2:
            gPromoScale *= 0.9;
            break;
        case 3:
            break;
    }
    switch (gPromoTextState) {
        case 0:
            if (gFramesInMode == 50) {
                gPromoTextState = 1;
                gPromoTitleAlpha = 0;
                gPromoRankAlpha = 0;
            }
            break;
        case 1:
            if (gPromoTitleAlpha + 16 > 0xFF) {
                gPromoTitleAlpha = 0xFF;
            } else {
                gPromoTitleAlpha += 16;
            }
            if (gFramesInMode == 200) {
                gPromoTextState = 2;
            }
            break;
        case 2:
            if (gPromoTitleAlpha - 32 < 0) {
                gPromoTitleAlpha = 0;
            } else {
                gPromoTitleAlpha -= 32;
            }
            if (gPromoRankAlpha + 16 > 0xFF) {
                gPromoRankAlpha = 0xFF;
            } else {
                gPromoRankAlpha += 16;
            }
            if (gFramesInMode == 290) {
                gPromoTextState = 3;
            }
            break;
        case 3:
            if (gPromoRankAlpha - 32 < 0) {
                gPromoRankAlpha = 0;
            } else {
                gPromoRankAlpha -= 32;
            }
            if (gPromoRankAlpha == 0) {
                gNextGameMode = MODE_RESULTS;
            }
            break;
    }
    if (gPromoTitleAlpha > 0 || gPromoRankAlpha > 0) {
        p = &players[playerNumber];
        text_print((Gfx * N64P *) dyn, gPromoTitle1, gPromoTitle1Glyphs, 0, 0x9C, 0, 0x18, 0x1A, 0x1A, 1, 0, 0, 0, gPromoTitleAlpha / 2);
        text_print((Gfx * N64P *) dyn, gPromoTitle2, gPromoTitle2Glyphs, 0, 0x9D, 0, 0xCB, 0x16, 0x16, 1, 0, 0, 0, gPromoTitleAlpha / 2);
        /* split the rank name into two lines */
        for (i = 0, j = 0; j < gRankTitleLine1Words[gPromoRank]; i++) {
            if ((gPromoRankLine1[i] = gRankTitles[gPromoRank].unk0[i]) == ' ') {
                j++;
            }
        }
        gPromoRankLine1[i - 1] = 0;
        for (j = i; gRankTitles[gPromoRank].unk0[i] != 0; i++) {
            gPromoRankLine2[i - j] = gRankTitles[gPromoRank].unk0[i];
        }
        gPromoRankLine2[i - j] = 0;
        if (font_strlen(gPromoRankLine1) >= 14 || font_strlen(gPromoRankLine2) >= 14) {
            size = 0x1D;
        } else {
            size = 0x21;
        }
        text_print((Gfx * N64P *) dyn, gPromoRankLine1, NULL, 0, 0x9D, 0, 0x58, size, size, 1, 0, 0, 0, gPromoRankAlpha / 2);
        text_print((Gfx * N64P *) dyn, gPromoRankLine2, NULL, 0, 0x9D, 0, 0x76, size, size, 1, 0, 0, 0, gPromoRankAlpha / 2);
        gPromoPulse += gPromoPulseDir * 15;
        if (gPromoPulse > 0xFF) {
            gPromoPulse -= 30;
            gPromoPulseDir = -gPromoPulseDir;
        }
        if (gPromoPulse < 0) {
            gPromoPulse += 30;
            gPromoPulseDir = -gPromoPulseDir;
        }
        text_print_gradient((Gfx * N64P *) dyn, gPromoTitle1, gPromoTitle1Glyphs, 0, 0xA0, 0, 0x14, 0x1A, 0x1A, 1, 0xFF, 0xFF - gPromoPulse, 0,
                      gPromoTitleAlpha, 0xFF, gPromoPulse, 0, gPromoTitleAlpha);
        text_print_gradient((Gfx * N64P *) dyn, gPromoTitle2, gPromoTitle2Glyphs, 0, 0xA0, 0, 0xC8, 0x16, 0x16, 1, 0xFF, 0xFF - gPromoPulse, 0,
                      gPromoTitleAlpha, 0xFF, gPromoPulse, 0, gPromoTitleAlpha);
        text_print_gradient((Gfx * N64P *) dyn, gPromoRankLine1, NULL, 0, 0xA0, 0, 0x55, size, size, 1, 0xFF, 0xFF - gPromoPulse, 0,
                      gPromoRankAlpha, 0xFF, gPromoPulse, 0, gPromoRankAlpha);
        text_print_gradient((Gfx * N64P *) dyn, gPromoRankLine2, NULL, 0, 0xA0, 0, 0x73, size, size, 1, 0xFF, 0xFF - gPromoPulse, 0,
                      gPromoRankAlpha, 0xFF, gPromoPulse, 0, gPromoRankAlpha);
    }
    gPromoAngle += 12.0 - gPromoScale * 2.0f;
    if (gPromoAngle > 360.0) {
        gPromoAngle -= 360.0;
    }
    if (gFramesInMode < 2) {
        guPerspective(&dyn->persp, &gPerspNorm, 45.0f, 1.3333334f, 40.0f, 4000.0f, 1.0f);
        guLookAtReflect(&dyn->view, &dyn->lookAt, 5.0f, 7.0f, 400.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f);
        guMtxIdent(&dyn->sceneTranslate);
        guMtxIdent(&dyn->sceneRotate);
    }
    guScale(&dyn->sceneScale, gPromoScale / 8.0f, gPromoScale / 8.0f, gPromoScale / 8.0f);
    guRotate(&gFeSceneRot[arg2], gPromoAngle, 1.0f, 1.0f, 1.0f);
    if (gPromoScale > 0.2) {
        gdl = fe_scene_draw(dyn, gdl);
    }
    *arg3 += gdl - arg0;
    return gdl;
}
