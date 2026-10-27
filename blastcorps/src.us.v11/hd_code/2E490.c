#include "common.h"
#include <ultra64.h>
#include "game/game.h"

extern u8 gSpriteCount;

/* Sprite slots (this file's .bss, 0x8036BFE0) */
extern void * N64P gSpriteImages[64][2];    /* frame images */
extern u8 gSpriteStrips[64];          /* frame count */
extern u8 gSpriteFlags[64];          /* flags */
extern f32 gSpriteScale[64];         /* scale */
extern Vtx * N64P gSpriteQuads[2][64][2]; /* per-buffer quads, two orientations */


Gfx *spr_draw_quad(Gfx *gdl, u8 slot, s16 x, s16 y, s32 flip, s32 size, s32 half, f32 scale, u8 frame);

#ifdef PORT_HOST
/* spr_draw_quad's sprite quads (fresh heap memory) get ob and tc only: their
 * flag and colour bytes are whatever the heap held, which the RSP shades the
 * sprite with (G_SHADE).  port_vtx_stale (port/src/load/swap.c) gives the
 * native records the N64's stale bytes first, as for 42240.c's HUD quads;
 * only the four it is about to write (each orientation's other four stay
 * untouched heap bytes on the N64; converting again is harmless). */
void port_vtx_stale(void *v, u32 n);
#endif

/* Frees every sprite slot (at mode setup) (0x80272C50). */
void spr_reset(void) {
    gSpriteCount = 0;
}

/* Loads sprites into the next slots; returns the first (0x80272C5C). */
u8 spr_load(u16 *ids, u16 *palIds, u8 count, u8 frames, u8 flags, f32 scale) {
    void *pal;
    s32 slot;
    s32 j;
    s32 m;
    s32 k;
    s32 start = gSpriteCount;

    if (!gTexTableLoaded) {
        tex_load_table();
    }
    k = 0;
    for (slot = start; slot < count + start; slot++, k += palIds ? 0 : 1) {
        if (palIds) {
            align_up((s32 *) &gHeapPtr, 16);
            tex_load_palette(palIds[slot - start], pal = gHeapPtr);
            gHeapPtr = (u8 *) gHeapPtr + 0x80;
        } else {
            align_up((s32 *) &gHeapPtr, 16);
            pal = NULL;
        }
        for (j = 0; j < frames; j++) {
            gSpriteImages[slot][j] = gHeapPtr;
            tex_load_to_heap(ids[frames * k + j], pal);
        }
        gSpriteStrips[slot] = frames;
        gSpriteFlags[slot] = flags;
        if (flags & 4) {
            for (j = 0; j < 2; j++) {
                for (m = 0; m < 2; m++) {
                    gSpriteQuads[j][slot][m] = (Vtx *) gHeapPtr;
                    gHeapPtr = (u8 *) gHeapPtr + 0x80;
                }
            }
        }
        gSpriteScale[slot] = scale;
    }
    gSpriteCount = slot;
    return start;
}

/* Pre-2.0I texture-rectangle form (G_RDPHALF_2/CONT = 0xB3/0xB2), as in 17E10.c */
#ifndef MAX
#define MAX(a, b) ((a) > (b) ? (a) : (b))
#endif
#ifndef MIN
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#endif

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


/* Draws a sprite slot (shadow and/or main pass) (0x80272ED8).
 * Draw sprite slot `slot` at (x, y): one 32-pixel-tall texture strip per frame column,
 * either through spr_draw_quad (flag 4: vertex quads) or as scaled texture rectangles.
 * mode selects an optional tinted shadow pass and the main pass. */
Gfx *spr_draw(Gfx *arg0, u8 slot, s16 x, s16 y, u8 alpha, u8 mode, f32 scale) {
    s32 i;
    Gfx *gdl;
    u8 a;
    s32 xl;
    s32 yl;
    s32 half;
    s32 n;

    gdl = arg0;
    a = alpha;
    scale *= gSpriteScale[slot];
    n = gSpriteStrips[slot];
    if ((mode & 8) && gScheduler.frameCount % 20 < 7) {
        a >>= 1;
    }
    gDPPipeSync(gdl++);
    if (gSpriteFlags[slot] & 4) {
        if (scale > 1.0) {
            gDPSetTextureFilter(gdl++, G_TF_BILERP);
        } else {
            gDPSetTextureFilter(gdl++, G_TF_POINT);
        }
    }
    for (i = 0; i < n - ((gSpriteFlags[slot] & 8) ? 1 : 0); i++) {
        if (gSpriteFlags[slot] & 2) {
            gDPLoadTextureBlock(gdl++, gSpriteImages[slot][i], G_IM_FMT_RGBA, G_IM_SIZ_32b, n << 5, 32, 0,
                                G_TX_CLAMP, G_TX_MIRROR, G_TX_NOMASK, n + 4, G_TX_NOLOD, G_TX_NOLOD);
        } else {
            gDPLoadTextureBlock(gdl++, gSpriteImages[slot][i], G_IM_FMT_RGBA, G_IM_SIZ_16b, n << 5, 32, 0,
                                G_TX_CLAMP, G_TX_MIRROR, G_TX_NOMASK, n + 4, G_TX_NOLOD, G_TX_NOLOD);
        }
        if (mode) {
            gDPPipeSync(gdl++);
            if (mode & 4) {
                gDPSetPrimColor(gdl++, 0, 0, alpha, alpha, alpha, 0xFF);
            } else {
                gDPSetPrimColor(gdl++, 0, 0, 0, 0, 0, alpha / 2);
            }
            half = n + 2.0f * scale;
            gDPSetRenderMode(gdl++, 0x00504240, 0);
            gDPSetCombine(gdl++, 0xFF97FF, 0xFF2DFEFF);
            if (gSpriteFlags[slot] & 4) {
                gdl = spr_draw_quad(gdl, slot, x, y, i, n, half, scale, 1);
            } else {
                xl = (x - half) * 4;
                yl = (y + half) * 4 + (i << 5) * 4 * scale;
                gSPScisTextureRectangleOld(gdl++, xl, yl, x * 4 + (((n << 5) - half - 1) << 2) * scale,
                                           (y + half) * 4 + (((i << 5) + 32) << 2) * scale, 0, 0,
                                           (gSpriteFlags[slot] & 1) ? (n << 5) << 5 : 0,
                                           (s32) (1024.0f / scale), (s32) (1024.0f / scale));
            }
        }
        if (!(mode & 6)) {
            if (!(mode & 1)) {
                half = n + 2.0f * scale;
            } else {
                half = 0;
            }
            gDPPipeSync(gdl++);
            gDPSetPrimColor(gdl++, 0, 0, 0xFF, 0, 0, a);
            if ((gSpriteFlags[slot] & 2) || a != 0xFF || !(gGameMode & (MODE_BOOT_SCREEN1 | MODE_BOOT_SCREEN2 | MODE_LEVEL_STATUS | MODE_PAK_CHECKED | MODE_LOAD_GAME | MODE_NEW_PLAYER | MODE_NAME_ENTRY | MODE_ERASE_CONFIRM | MODE_ERASE_PLAYER_CHOSEN | MODE_LOAD_GAME_RETURN | MODE_ERASE_MENU | MODE_TO_ERASE_MENU | MODE_NAME_ENTERED | MODE_PLAYER_CHOSEN | MODE_RESULTS | MODE_PAK_CHECK | MODE_PRE_PROMOTION | MODE_PAK_FILES | MODE_UNUSED_33 | MODE_UNUSED_WINDOW_13 | MODE_PAK_MANAGER_CHECKED | MODE_PAK_FILE_CHOSEN | MODE_PAK_DELETE_CONFIRM | MODE_PAK_FILE_DELETED | MODE_UNUSED_41 | MODE_BEST_TIMES | MODE_PAK_MANAGER | MODE_ATTRACT_PICTURE | MODE_REGRADE_NOTICE | MODE_REGRADE_NOTICE_END | MODE_UNUSED_52 | MODE_BOOT_CHECK | MODE_ERASE_SAVE | MODE_BEST_TIMES_UNUSED | MODE_GUEST_PLAYER | MODE_NO_CONTROLLER | MODE_QUIT_GAME | MODE_LOAD_GAME_EXIT))) {
                gDPSetRenderMode(gdl++, 0x00504240, 0);
            } else {
                gDPSetRenderMode(gdl++, 0x0F0A7008, 0);
            }
            gDPSetCombine(gdl++, 0x15162A, 0xFF2FFFFF);
            if (gSpriteFlags[slot] & 4) {
                gdl = spr_draw_quad(gdl, slot, x, y, i, n, half, scale, 0);
            } else {
                xl = (x - half) * 4;
                yl = (y + half) * 4 + (i << 5) * 4 * scale;
                gSPScisTextureRectangleOld(gdl++, xl, yl, x * 4 + (((n << 5) - half - 1) << 2) * scale,
                                           (y + half) * 4 + (((i << 5) + 32) << 2) * scale, 0, 0,
                                           (gSpriteFlags[slot] & 1) ? (n << 5) << 5 : 0,
                                           (s32) (1024.0f / scale), (s32) (1024.0f / scale));
            }
        }
    }
    gDPPipeSync(gdl++);
    return gdl;
}

/* One strip of a sprite as a textured vertex quad (0x802742D8). */
Gfx *spr_draw_quad(Gfx *gdl, u8 slot, s16 x, s16 y, s32 flip, s32 size, s32 half, f32 scale, u8 frame) {
    Vtx *vtx = gSpriteQuads[gBufferIndex][slot][frame];
    s32 x0 = x - half;
    s32 y0 = y + half;
    s32 t;

    if (gSpriteFlags[slot] & 1) {
        t = 0;
    } else {
        t = size << 5;
    }
#ifdef PORT_HOST
    port_vtx_stale(flip ? vtx + 4 : vtx, 4);
#endif
    if (flip) {
        vtx += 4;
        vtx[0].v.ob[0] = x0;
        vtx[0].v.ob[1] = y0 + 64.0f * scale - 1.0f;
        vtx[0].v.ob[2] = -10;
        vtx[0].v.tc[0] = 0;
        vtx[0].v.tc[1] = t << 5;
        vtx[1].v.ob[0] = x0 + (size << 5) * scale - 1.0f;
        vtx[1].v.ob[1] = y0 + 64.0f * scale - 1.0f;
        vtx[1].v.ob[2] = -10;
        vtx[1].v.tc[0] = ((size << 5) - 1) << 5;
        vtx[1].v.tc[1] = t << 5;
        vtx[2].v.ob[0] = x0 + (size << 5) * scale - 1.0f;
        vtx[2].v.ob[1] = y0 + 32.0f * scale - 1.0f;
        vtx[2].v.ob[2] = -10;
        vtx[2].v.tc[0] = ((size << 5) - 1) << 5;
        vtx[2].v.tc[1] = (t + 31) << 5;
        vtx[3].v.ob[0] = x0;
        vtx[3].v.ob[1] = y0 + 32.0f * scale - 1.0f;
        vtx[3].v.ob[2] = -10;
        vtx[3].v.tc[0] = 0;
        vtx[3].v.tc[1] = (t + 31) << 5;
    } else {
        vtx[0].v.ob[0] = x0;
        vtx[0].v.ob[1] = y0;
        vtx[0].v.ob[2] = -10;
        vtx[0].v.tc[0] = 0;
        vtx[0].v.tc[1] = (t + 31) << 5;
        vtx[1].v.ob[0] = x0 + (size << 5) * scale - 1.0f;
        vtx[1].v.ob[1] = y0;
        vtx[1].v.ob[2] = -10;
        vtx[1].v.tc[0] = ((size << 5) - 1) << 5;
        vtx[1].v.tc[1] = (t + 31) << 5;
        vtx[2].v.ob[0] = x0 + (size << 5) * scale - 1.0f;
        vtx[2].v.ob[1] = y0 + 32.0f * scale - 1.0f;
        vtx[2].v.ob[2] = -10;
        vtx[2].v.tc[0] = ((size << 5) - 1) << 5;
        vtx[2].v.tc[1] = t << 5;
        vtx[3].v.ob[0] = x0;
        vtx[3].v.ob[1] = y0 + 32.0f * scale - 1.0f;
        vtx[3].v.ob[2] = -10;
        vtx[3].v.tc[0] = 0;
        vtx[3].v.tc[1] = t << 5;
    }
    gSPVertex(gdl++, vtx, 4, 0);
    osWritebackDCache(vtx, sizeof(Vtx) * 4);
    gSP1Triangle(gdl++, 0, 3, 2, 0);
    gSP1Triangle(gdl++, 0, 2, 1, 0);
    return gdl;
}

/* Render state for texture-rectangle sprites (0x80274868). */
Gfx *spr_begin(Gfx *gfx) {
    Gfx *gdl = gfx;

    gDPPipeSync(gdl++);
    gSPClearGeometryMode(gdl++, 0xFFFFFFFF);
    gSPTexture(gdl++, 0, 0, 0, G_TX_RENDERTILE, G_OFF);
    gDPSetTexturePersp(gdl++, G_TP_NONE);
    gDPSetCycleType(gdl++, G_CYC_1CYCLE);
    gDPSetRenderMode(gdl++, G_RM_PASS, G_RM_OPA_SURF2);
    gDPSetTextureFilter(gdl++, G_TF_BILERP);
    return gdl;
}

/* Render state for shaded textured quads (0x80274998). */
Gfx *spr_begin_quads(Gfx *gfx) {
    Gfx *gdl = gfx;

    gDPPipeSync(gdl++);
    gSPClearGeometryMode(gdl++, 0xFFFFFFFF);
    gSPSetGeometryMode(gdl++, G_SHADE | G_SHADING_SMOOTH);
    gSPTexture(gdl++, 0xFFFF, 0xFFFF, 0, G_TX_RENDERTILE, G_ON);
    gDPSetCycleType(gdl++, G_CYC_1CYCLE);
    gDPSetRenderMode(gdl++, G_RM_PASS, G_RM_OPA_SURF2);
    return gdl;
}

/* Ends sprite drawing: perspective correction back on (0x80274AA4). */
Gfx *spr_end(Gfx *gfx) {
    Gfx *gdl = gfx;

    gDPPipeSync(gdl++);
    gDPSetTexturePersp(gdl++, G_TP_PERSP);
    return gdl;
}

/* A pipe sync (0x80274B08). */
Gfx *spr_pipe_sync(Gfx *gfx) {
    Gfx *gdl = gfx;

    gDPPipeSync(gdl++);
    return gdl;
}

/* Draws a sprite slot 28 frames of every 40 (0x80274B40). */
void spr_draw_blinking(Gfx **gfx, s32 arg1, u8 arg2, s16 arg3, s16 arg4) {
    Gfx *gdl = *gfx;

    if (gScheduler.frameCount % 40 < 28) {
        gdl = spr_begin(gdl);
        gdl = spr_draw(gdl, arg2, arg3, arg4, 0xFF, 1, 1.0f);
        gdl = spr_end(gdl);
    }
    *gfx = gdl;
}
