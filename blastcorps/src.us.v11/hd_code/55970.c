#include "common.h"
#include <ultra64.h>
#include "game/game.h"

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

extern u8 romCopyright[];
extern u8 romVehicleModels[];
extern void * N64P gDemoBannerTex;
extern s16 gDemoBannerAlpha;

/* The 256x16 attract-demo banner from ROM (0x8029A130).
 * Load the 256x16 IA8 overlay texture from ROM into the heap */
void hud_load_demo_banner(void) {
    s32 size = romVehicleModels - romCopyright;

    hd_rom_load((u32) romCopyright, (u32) gHeapPtr, (u32 *) &size, 0xC, 0, 1);
    gDemoBannerTex = gHeapPtr;
    gHeapPtr = (u8 *) gHeapPtr + size;
    gDemoBannerAlpha = 0;
}

/* Draws it, fading in (0x8029A1A8).
 * Draw it as a textured rectangle, fading in by 0x40 per frame */
Gfx *hud_draw_demo_banner(s32 arg0, Gfx *arg1) {
    Gfx *gfx = arg1;

    gDPPipeSync(gfx++);
    gSPTexture(gfx++, 0, 0, 0, G_TX_RENDERTILE, G_OFF);
    gDPSetTexturePersp(gfx++, G_TP_NONE);
    gDPSetRenderMode(gfx++, G_RM_XLU_SURF, G_RM_XLU_SURF2);
    gDPSetCombineMode(gfx++, G_CC_MODULATERGBA_PRIM, G_CC_MODULATERGBA_PRIM);
    if (gDemoBannerAlpha + 0x40 >= 0x100) {
        gDemoBannerAlpha = 0xFF;
    } else {
        gDemoBannerAlpha += 0x40;
    }
    gDPSetPrimColor(gfx++, 0, 0, 0xFF, 0xFF, 0xFF, gDemoBannerAlpha);
    gDPLoadTextureBlock(gfx++, gDemoBannerTex, G_IM_FMT_IA, G_IM_SIZ_8b, 256, 16, 0, G_TX_CLAMP, G_TX_CLAMP,
                        G_TX_NOMASK, G_TX_NOMASK, G_TX_NOLOD, G_TX_NOLOD);
    gSPTextureRectangleOld(gfx++, 32 << 2, 208 << 2, 288 << 2, 224 << 2, G_TX_RENDERTILE, 0, 0, 1 << 10, 1 << 10);
    gDPPipeSync(gfx++);
    gDPSetTexturePersp(gfx++, G_TP_PERSP);
    return gfx;
}
