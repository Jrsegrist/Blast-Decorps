#include "common.h"
#include <ultra64.h>
#include "game/game.h"

/* The file after sched.c. Its .rodata (two pairs of doubles at
 * 0x8030C4D0) and .bss (0x8036BFC0) each start on a fresh 16-byte
 * boundary, which is what left the 8 zero bytes after sched.c's jump
 * table. The .bss is placed by hd_code_bss.us.v11.ld. */
f32 gBackdropOffset; /* added to the backdrop's visible height (from the level area; +32 on levels 0x11, 0x26, 0x34, 0x3B) */
u8 gBackdropLoaded;   /* backdrop textures loaded */
u8 gBackdropVtxBuf;   /* vertex double-buffer index */
f32 gBackdropScrollS;
f32 gBackdropHeight;
f32 gBackdropUnderflow;

/* One backdrop texture, 0xC bytes; two per level */
typedef struct {
    /* 0x00 */ s16 id;
    /* 0x04 */ void * N64P ptr;
    /* 0x08 */ u8 unk8; /* log2 of the s scale */
    /* 0x09 */ u8 unk9; /* log2 of the t scale */
    /* 0x0A */ u8 unkA; /* cms */
    /* 0x0B */ u8 unkB; /* cmt */
} FadeEntry;

typedef struct {
    u16 fmt[2];
} FmtPair;

extern FadeEntry gBackdropTextures[][2];
extern Vtx gBackdropVtx[2][4];
extern FmtPair gBackdropTileFormats;


/* The two-layer sky backdrop; *height = the visible rows (0x80271FD0).
 * Draws the two-layer scrolling backdrop and returns the new display
 * list end; *height gets the backdrop's visible height */
Gfx *backdrop_draw(Gfx *gfx, u32 arg1, u16 level, s16 arg3, s16 arg4, s32 *height) {
    Gfx *gdl = gfx;
    FmtPair fmts = gBackdropTileFormats;
    s32 i;
    FadeEntry *entry;
    f32 scroll;
    f32 s0;
    f32 t0;
    f32 s1;
    f32 t1;

    if (!gBackdropLoaded) {
        *height = 0;
        return gdl;
    }
    scroll = 120.0 - (f32) arg4 * 0.22 + gBackdropOffset;
    gBackdropHeight = (0.0 > scroll) ? 0.0 : scroll;
    gBackdropUnderflow = (0.0 > -scroll) ? 0.0 : -scroll;
    *height = gBackdropHeight;
    if (*height <= 0) {
        return gdl;
    }
    gBackdropScrollS = (arg3 - 2048.0) * 0.5;
    gBackdropVtx[gBackdropVtxBuf][2].v.ob[1] = gBackdropHeight * 4.0 - 1.0;
    gBackdropVtx[gBackdropVtxBuf][3].v.ob[1] = gBackdropHeight * 4.0 - 1.0;
    gSPClearGeometryMode(gdl++, 0xFFFFFFFF);
    gSPSetGeometryMode(gdl++, G_SHADE | G_SHADING_SMOOTH);
    gSPTexture(gdl++, 0x8000, 0x8000, 0, G_TX_RENDERTILE, G_ON);
    gDPPipeSync(gdl++);
    gDPSetCombine(gdl++, 0xFC24A3FF, 0x1FFC9238);
    gDPSetCycleType(gdl++, G_CYC_2CYCLE);
    gDPSetRenderMode(gdl++, G_RM_PASS, G_RM_OPA_SURF2);
    gDPSetTextureFilter(gdl++, G_TF_BILERP);
    for (i = 0; i < 2; i++) {
        entry = &gBackdropTextures[level][i];
        gDPSetTextureImage(gdl++, G_IM_FMT_RGBA, G_IM_SIZ_16b, 1, entry->ptr);
        gDPTileSync(gdl++);
        gDPSetTile(gdl++, G_IM_FMT_RGBA, G_IM_SIZ_16b, 0, i * 256, G_TX_LOADTILE, 0, 0, 0, 0, 0, 0, 0);
        gDPLoadSync(gdl++);
        gDPLoadBlock(gdl++, G_TX_LOADTILE, 0, 0, 1023, 256);
    }
    gSPMatrix(gdl++, arg1 + 0x80000100, G_MTX_PROJECTION | G_MTX_LOAD | G_MTX_NOPUSH);
    gSPMatrix(gdl++, arg1 + 0x800001C0, G_MTX_MODELVIEW | G_MTX_LOAD | G_MTX_NOPUSH);
    gDPTileSync(gdl++);
    gDPSetTextureLOD(gdl++, G_TL_TILE);
    for (i = 0; i < 2; i++) {
        entry = &gBackdropTextures[level][i];
        gDPSetTile(gdl++, fmts.fmt[i], G_IM_SIZ_16b, 8, i * 256, i, 0, entry->unkB & 3, 5, i, entry->unkA & 3, 5, i);
        gDPSetTileSize(gdl++, i, 0, 0, 31 << G_TEXTURE_IMAGE_FRAC, 31 << G_TEXTURE_IMAGE_FRAC);
    }
    s0 = (gBackdropScrollS + 16.0f) / (f32) (1 << entry->unk8);
    s1 = 319.0 / (f32) (1 << entry->unk8);
    gBackdropVtx[gBackdropVtxBuf][0].v.tc[0] = s0 * 32.0;
    gBackdropVtx[gBackdropVtxBuf][1].v.tc[0] = (s0 + s1) * 32.0;
    gBackdropVtx[gBackdropVtxBuf][2].v.tc[0] = s0 * 32.0;
    gBackdropVtx[gBackdropVtxBuf][3].v.tc[0] = (s0 + s1) * 32.0;
    t0 = (gBackdropHeight - gBackdropOffset - gBackdropUnderflow) / (f32) (1 << entry->unk9);
    t1 = (gBackdropHeight - 1.0) / (f32) (1 << entry->unk9);
    gBackdropVtx[gBackdropVtxBuf][0].v.tc[1] = t0 * 32.0;
    gBackdropVtx[gBackdropVtxBuf][1].v.tc[1] = t0 * 32.0;
    gBackdropVtx[gBackdropVtxBuf][2].v.tc[1] = (t0 - t1) * 32.0;
    gBackdropVtx[gBackdropVtxBuf][3].v.tc[1] = (t0 - t1) * 32.0;
    gSPVertex(gdl++, gBackdropVtx[gBackdropVtxBuf], 4, 0);
    gDPPipeSync(gdl++);
    gSP1Triangle(gdl++, 0, 1, 2, 0);
    gSP1Triangle(gdl++, 1, 2, 3, 0);
    gBackdropVtxBuf ^= 1;
    return gdl;
}

/* The backdrop's offset from the level area and mode; its textures (0x802729F0).
 * Sets up the backdrop for a level: its height offset from the screen area
 * and the level type, and loads its two textures */
void backdrop_init(u16 type, u16 level) {
    s32 i;
    f32 area;
    u16 id;

    area = gGroundCols * gGroundCellW;
    area = (gGroundRows * gGroundCellD + area) / 32.0f;
    gBackdropLoaded = 1;
    switch (type) {
        case 0:
        case 1:
        case 2:
        case 0x40:
        case 0x800:
        case 0x1000:
            gBackdropOffset = 1200000.0 / area;
            break;
        case 4:
        case 0x100:
        case 0x2000:
            gBackdropOffset = 600000.0 / area;
            break;
        default:
            gBackdropOffset = 0.0f;
            break;
    }
    if (level == 0x34 || level == 0x3B || level == 0x26 || level == 0x11) {
        gBackdropOffset += 32.0f;
    }
    for (i = 0; i < 2; i++) {
        gBackdropTextures[level][i].ptr = gHeapPtr;
        id = gBackdropTextures[level][i].id;
        if (id) {
            tex_load_to_heap(id, 0);
        } else {
            gBackdropLoaded = 0;
        }
    }
}

/* Empty, with one unused s32 parameter; no callers (0x80272C40). */
void backdrop_unused_stub(s32 arg0) {
}
