#include "common.h"
#include <ultra64.h>
#include "game/game.h"

#define PHYS(x) ((u32)(x) & 0x1FFFFFFF)

/* Display-list helpers: render-state setup and a textured-quad drawer. */

/* XLU surfaces with CVG_DST_SAVE where the 2.0I headers have CVG_DST_FULL */
#define RM_ZB_XLU_SAVE(clk) (RM_ZB_XLU_SURF(clk) | CVG_DST_SAVE)
#define RM_XLU_SAVE(clk) (RM_XLU_SURF(clk) | CVG_DST_SAVE)

/* The old SDK's 4-bit LoadBlock dxt has no MAX(1, ...) clamp. */
#undef TXL2WORDS_4b
#define TXL2WORDS_4b(txls) ((txls) / 16)

/* Starts textured, shaded drawing (0x80257540). */
Gfx *gfx_begin_textured(Gfx *gfx) {
    gDPPipeSync(gfx++);
    gDPSetCycleType(gfx++, G_CYC_1CYCLE);
    gSPClearGeometryMode(gfx++, 0xFFFFFFFF);
    gSPSetGeometryMode(gfx++, G_SHADE | G_SHADING_SMOOTH | G_LOD);
    gSPTexture(gfx++, 0xFFFF, 0xFFFF, 0, G_TX_RENDERTILE, G_ON);
    return gfx;
}

/* Four vertices and a texture block, two triangles (0x802575F4).
 * Draws a textured quad: loads vtx[0..3], loads the texture block (size in
 * the low byte of fmtsiz, format in the high byte), picks a render mode and
 * combiner for the format, and emits two triangles. */
Gfx *gfx_draw_textured_quad(Gfx *gfx, Vtx *vtx, void *timg, s16 fmtsiz, s32 width, s32 height, s32 zbuf) {
    u32 img;

    gDPPipeSync(gfx++);
    if (zbuf) {
        gSPSetGeometryMode(gfx++, G_ZBUFFER);
    } else {
        gSPClearGeometryMode(gfx++, G_ZBUFFER);
    }
    img = PHYS(timg);
    gSPVertex(gfx++, PHYS(vtx), 4, 0);
    switch ((u8) fmtsiz) {
        case G_IM_SIZ_4b:
            gDPLoadTextureBlock_4b(gfx++, img, (u8) (fmtsiz >> 8), width, height, 0,
                                   G_TX_CLAMP, G_TX_CLAMP, G_TX_NOMASK, G_TX_NOMASK, G_TX_NOLOD, G_TX_NOLOD);
            break;
        case G_IM_SIZ_8b:
            gDPLoadTextureBlock(gfx++, img, (u8) (fmtsiz >> 8), G_IM_SIZ_8b, width, height, 0,
                                G_TX_CLAMP, G_TX_CLAMP, G_TX_NOMASK, G_TX_NOMASK, G_TX_NOLOD, G_TX_NOLOD);
            break;
        case G_IM_SIZ_16b:
            gDPLoadTextureBlock(gfx++, img, (u8) (fmtsiz >> 8), G_IM_SIZ_16b, width, height, 0,
                                G_TX_CLAMP, G_TX_CLAMP, G_TX_NOMASK, G_TX_NOMASK, G_TX_NOLOD, G_TX_NOLOD);
            break;
        case G_IM_SIZ_32b:
            gDPLoadTextureBlock(gfx++, img, (u8) (fmtsiz >> 8), G_IM_SIZ_32b, width, height, 0,
                                G_TX_CLAMP, G_TX_CLAMP, G_TX_NOMASK, G_TX_NOMASK, G_TX_NOLOD, G_TX_NOLOD);
            break;
    }
    switch ((u8) (fmtsiz >> 8)) {
        case G_IM_FMT_RGBA:
            if (zbuf) {
                gDPSetRenderMode(gfx++, RM_ZB_XLU_SAVE(1), RM_ZB_XLU_SAVE(2));
            } else {
                gDPSetRenderMode(gfx++, RM_XLU_SAVE(1), RM_XLU_SAVE(2));
            }
            gDPSetCombineMode(gfx++, G_CC_MODULATERGBA, G_CC_MODULATERGBA);
            break;
        case G_IM_FMT_IA:
            if (zbuf) {
                gDPSetRenderMode(gfx++, RM_ZB_XLU_SAVE(1), RM_ZB_XLU_SAVE(2));
            } else {
                gDPSetRenderMode(gfx++, RM_XLU_SAVE(1), RM_XLU_SAVE(2));
            }
            gDPSetCombineMode(gfx++, G_CC_MODULATERGBA, G_CC_MODULATERGBA);
            break;
        case G_IM_FMT_I:
            if (zbuf) {
                gDPSetRenderMode(gfx++, G_RM_ZB_OPA_SURF, G_RM_ZB_OPA_SURF2);
            } else {
                gDPSetRenderMode(gfx++, G_RM_OPA_SURF, G_RM_OPA_SURF2);
            }
            gDPSetCombineMode(gfx++, G_CC_MODULATERGB, G_CC_MODULATERGB);
            break;
    }
    gSP1Triangle(gfx++, 0, 1, 3, 0);
    gSP1Triangle(gfx++, 0, 2, 3, 0);
    return gfx;
}
