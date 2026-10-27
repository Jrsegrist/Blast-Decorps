#include "common.h"
#include <ultra64.h>
#include "game/game.h"

extern u8 * N64P gPadDisplayImage;
extern u8 * N64P gPadDisplayButtonTex;
extern u8 * N64P gPadDisplayTriggerTex;
extern u8 * N64P gPadDisplayZTex;

/* The on-screen controller picture (0x80295E50).
 * Carves four buffers out of the heap and loads ROM asset 0x6A8DA0 into the first. */
void hud_load_pad_image(void) {
    s32 size;

    gPadDisplayImage = gHeapPtr;
    gPadDisplayButtonTex = gPadDisplayImage + 0x3200;
    gPadDisplayTriggerTex = gPadDisplayButtonTex + 0xF0;
    gPadDisplayZTex = gPadDisplayTriggerTex + 0x180;
    size = romDemoRecordings - romControllerPicture;
    hd_rom_load((u32) romControllerPicture, (u32) gPadDisplayImage, (u32 *) &size, 10, 0, 1);
    gHeapPtr = (u8 *) gHeapPtr + size;
    gPadDisplayAlpha = 0;
    gPadDisplayLoaded = 1;
}

/* Pre-2.0I scissored texture rectangle (see 17E10.c): G_RDPHALF_2/G_RDPHALF_CONT
 * are 0xB3/0xB2 and there are no (s16) casts. */
#define OLD_RDPHALF_2 0xB3
#define OLD_RDPHALF_CONT 0xB2

#define gSPScisTextureRectangleOld(pkt, xl, yl, xh, yh, tile, s, t, dsdx, dtdy)             \
    {                                                                                        \
        Gfx *_g = (Gfx *) (pkt);                                                             \
                                                                                             \
        _g->words.w0 = (_SHIFTL(G_TEXRECT, 24, 8) | _SHIFTL(MAX(xh, 0), 12, 12) |            \
                        _SHIFTL(MAX(yh, 0), 0, 12));                                         \
        _g->words.w1 = (_SHIFTL(tile, 24, 3) | _SHIFTL(MAX(xl, 0), 12, 12) |                 \
                        _SHIFTL(MAX(yl, 0), 0, 12));                                         \
        gImmp1(pkt, OLD_RDPHALF_2,                                                           \
               (_SHIFTL(((s) - MIN(((xl) * (dsdx)) >> 7, 0)), 16, 16) |                      \
                _SHIFTL(((t) - MIN(((yl) * (dtdy)) >> 7, 0)), 0, 16)));                      \
        gImmp1(pkt, OLD_RDPHALF_CONT, (_SHIFTL(dsdx, 16, 16) | _SHIFTL(dtdy, 0, 16)));       \
    }

Gfx *hud_draw_pad_image(Gfx *gfx, s16 x, s16 y);

/* The demo's on-screen controller (gRecButtons) (0x80295EFC).
 *
 * Draws the on-screen controller at (x, y) with translucency alpha: the
 * pad image (with a drop shadow), Z and L/R trigger highlights, coloured
 * A/B/Start and D-pad left/right button highlights, and the stick position.
 */
Gfx *hud_draw_pad(s32 arg0, Gfx *arg1, s16 x, s16 y, u8 alpha) {
    Gfx *gfx;
    s32 i;

    gfx = arg1;
    gDPPipeSync(gfx++);
    gSPTexture(gfx++, 0, 0, 0, G_TX_RENDERTILE, G_OFF);
    gDPSetTexturePersp(gfx++, G_TP_NONE);
    gDPSetCycleType(gfx++, G_CYC_1CYCLE);
    gDPSetRenderMode(gfx++, 0x00404240, 0);
    gDPSetCombine(gfx++, 0xFF97FF, 0xFF2DFEFF);
    gDPSetPrimColor(gfx++, 0, 0, 0, 0, 0, alpha / 2);
    gfx = hud_draw_pad_image(gfx, x - 4, y + 4);
    gDPPipeSync(gfx++);
    gDPSetCombine(gfx++, 0xFF97FF, 0xFF2CFE7F);
    gDPSetPrimColor(gfx++, 0, 0, 0, 0, 0, alpha);
    gfx = hud_draw_pad_image(gfx, x, y);
    if (gRecButtons & (L_TRIG | R_TRIG)) {
        gDPLoadTextureBlock(gfx++, gPadDisplayTriggerTex, G_IM_FMT_RGBA, G_IM_SIZ_16b, 32, 6, 0, G_TX_MIRROR,
                            G_TX_CLAMP, 5, G_TX_NOMASK, G_TX_NOLOD, G_TX_NOLOD);
        if (gRecButtons & R_TRIG) {
            gSPScisTextureRectangleOld(gfx++, (x + 0x30) << 2, (y + 8) << 2, (x + 0x4F) << 2,
                                       (y + 0xD) << 2, 0, 0, 0, 1 << 10, 1 << 10);
        }
        if (gRecButtons & L_TRIG) {
            gSPScisTextureRectangleOld(gfx++, x << 2, (y + 8) << 2, (x + 0x1F) << 2, (y + 0xD) << 2, 0,
                                       32 << 5, 0, 1 << 10, 1 << 10);
        }
    }
    if (gRecButtons & Z_TRIG) {
        gDPLoadTextureBlock(gfx++, gPadDisplayZTex, G_IM_FMT_RGBA, G_IM_SIZ_16b, 24, 24, 0, G_TX_CLAMP,
                            G_TX_CLAMP, G_TX_NOMASK, G_TX_NOMASK, G_TX_NOLOD, G_TX_NOLOD);
        gSPScisTextureRectangleOld(gfx++, (x + 9) << 2, (y + 0x33) << 2, (x + 0x20) << 2,
                                   (y + 0x4A) << 2, 0, 0, 0, 1 << 10, 1 << 10);
    }
    gDPPipeSync(gfx++);
    gDPSetCombine(gfx++, 0x119623, 0xFF2FFFFF);
    gDPLoadTextureBlock(gfx++, gPadDisplayButtonTex, G_IM_FMT_IA, G_IM_SIZ_16b, 12, 10, 0, G_TX_CLAMP,
                        G_TX_CLAMP, G_TX_NOMASK, G_TX_NOMASK, G_TX_NOLOD, G_TX_NOLOD);
    for (i = 0; i < 16; i++) {
        u8 draw;
        u16 ox;
        u16 oy;

        draw = 1;
        if (gRecButtons & (1 << i)) {
            gDPPipeSync(gfx++);
            switch (1 << i) {
                case 0x8000:
                    gDPSetPrimColor(gfx++, 0, 0, 0, 0, 0xFF, alpha);
                    ox = 0x39, oy = 0x1E;
                    break;
                case 0x4000:
                    gDPSetPrimColor(gfx++, 0, 0, 0, 0xFF, 0, alpha);
                    ox = 0x34, oy = 0x19;
                    break;
                case 0x1000:
                    gDPSetPrimColor(gfx++, 0, 0, 0xFF, 0, 0, alpha);
                    ox = 0x23, oy = 0x1B;
                    break;
                case 0x200:
                    gDPSetPrimColor(gfx++, 0, 0, 0x50, 0x50, 0x50, alpha);
                    ox = 4, oy = 0x16;
                    break;
                case 0x100:
                    gDPSetPrimColor(gfx++, 0, 0, 0x50, 0x50, 0x50, alpha);
                    ox = 0x10, oy = 0x16;
                    break;
                default:
                    draw = 0;
                    break;
            }
            if (draw) {
                gSPScisTextureRectangleOld(gfx++, (x + ox) << 2, (y + oy) << 2, (x + ox + 11) << 2,
                                           (y + oy + 9) << 2, 0, 0, 0, 1 << 10, 1 << 10);
            }
        }
    }
    gDPPipeSync(gfx++);
    gDPSetPrimColor(gfx++, 0, 0, 0xDC, 0xDC, 0xDC, alpha);
    gSPScisTextureRectangleOld(gfx++, (x + gRecStickX / 18 + 0x23) << 2, (y - gRecStickY / 18 + 0x28) << 2,
                               (x + gRecStickX / 18 + 0x2E) << 2, (y - gRecStickY / 18 + 0x31) << 2, 0, 0,
                               0, 1 << 10, 1 << 10);
    gDPPipeSync(gfx++);
    gDPSetTexturePersp(gfx++, G_TP_PERSP);
    return gfx;
}

/* Clamp a tile's last texel to the 80x80 image. */
#define LIM79(a) ((a) > 79 ? 79 : (a))

/* The 80x80 picture as 32x32 tiles (0x8029700C).
 * Draws the 80x80 RGBA16 image in gPadDisplayImage at (x, y) as 32x32 tiles. */
Gfx *hud_draw_pad_image(Gfx *gfx, s16 x, s16 y) {
    s32 i;
    s32 j;

    for (i = 0; i < 80; i += 31) {
        for (j = 0; j < 80; j += 31) {
            gDPLoadTextureTile(gfx++, gPadDisplayImage, G_IM_FMT_RGBA, G_IM_SIZ_16b, 80, 0, i, j,
                               LIM79(i + 31), LIM79(j + 31), 0, G_TX_CLAMP, G_TX_CLAMP, 0, 0,
                               G_TX_NOLOD, G_TX_NOLOD);
            gSPScisTextureRectangleOld(gfx++, (x + i) << 2, (y + j) << 2,
                                       (LIM79(i + 31) + x) << 2, (LIM79(j + 31) + y) << 2, 0,
                                       i << 5, j << 5, 1 << 10, 1 << 10);
        }
    }
    return gfx;
}
