#include "common.h"
#include <ultra64.h>
#include "game/game.h"

/* front-end 3D scene: a small hierarchy of nodes (gSolarNodes[7]) drawn with a
 * float matrix stack, a look-at camera and a depth-sorted display list table */

typedef f32 MtxF[4][4];

typedef struct Node {
    /* 0x00 */ f32 size;        /* the drawn size (gSolarEarthSize = the Earth's: scale 1) */
    /* 0x04 */ f32 unk4;
    /* 0x08 */ f32 period;      /* orbit period (0 = it doesn't move) */
    /* 0x0C */ f32 orbitRadius; /* fe_globe_latlon_to_xyz's radius */
    /* 0x10 */ struct Node * N64P next;  /* next sibling */
    /* 0x14 */ struct Node * N64P child; /* first child */
    /* 0x18 */ u8 rgb[4];       /* the glow sprite's colour */
    /* 0x1C */ f32 angle;       /* orbit angle */
    /* 0x20 */ f32 posX;        /* world position from the matrix stack (the camera's target) */
    /* 0x24 */ f32 posY;
    /* 0x28 */ f32 posZ;
    /* 0x2C */ f32 eyeX;        /* the camera eye when the camera looks at this node */
    /* 0x30 */ f32 eyeY;
    /* 0x34 */ f32 eyeZ;
    /* 0x38 */ f32 depth;       /* distance from the eye (draw sort key) */
} Node; /* 0x3C */

typedef struct {
    s32 node; /* node index */
    Gfx * N64P dl;
} DrawEntry;

extern Node gSolarNodes[];
extern f32 gSolarEarthSize; /* gSolarNodes[3].size */
extern f32 gSolarEarthPeriod; /* gSolarNodes[3].period */
extern Node gSolarSunNode; /* gSolarNodes[6] */
extern Vtx gSolarSpriteVtx[][2][4];
extern MtxF gSolarMtxStack[];
extern s32 gSolarMtxTop; /* matrix stack index into gSolarMtxStack */
extern f32 gSolarEyeX; /* camera eye */
extern f32 gSolarEyeY;
extern f32 gSolarEyeZ;
extern f32 gSolarAtX; /* camera target */
extern f32 gSolarAtY;
extern f32 gSolarAtZ;
extern DrawEntry gSolarDrawList[];
extern s16 gSolarCamHold;

f32 sqrtf(f32);
void fe_solar_build_mtx(Node *);
Gfx *fe_solar_draw_glow(Gfx *, u8 *, Node *, f32);
Gfx *fe_solar_draw_sun(Gfx *, u8 *, Node *, f32);
void fe_solar_build_node_dls(Gfx *, u8 *);
void fe_solar_update_node(Node *, f32);
s32 fe_solar_cmp_depth(DrawEntry *, DrawEntry *);

/* Node matrices, the look-at camera, depth-sorted node lists (0x801F3450).
 * draw the node scene: build the matrices, set the camera, sort and emit the display lists */
Gfx *fe_solar_draw(Gfx *gdl, u8 *dyn) {
    Gfx *gfx;
    Node *nodes;
    Node *cam;
    u8 found;
    s32 i;

    gfx = gdl;
    nodes = gSolarNodes;
    cam = &nodes[&nodes[gGlobePlanet] - nodes];
    gImmp1(gfx++, G_RDPHALF_1, gPerspNorm);
    gSolarMtxTop = 4;
    guMtxIdentF(gSolarMtxStack[4]);
    fe_solar_build_mtx(&gSolarSunNode);
    if (gSolarCamHold != 1) {
        gSolarEyeX = cam->eyeX;
        gSolarEyeY = cam->eyeY;
        gSolarEyeZ = cam->eyeZ;
        gSolarAtX = cam->posX;
        gSolarAtY = cam->posY;
        gSolarAtZ = cam->posZ;
    }
    guLookAt((Mtx *) (dyn + 0x140), gSolarEyeX + 1.0f, gSolarEyeY, gSolarEyeZ, gSolarAtX, gSolarAtY, gSolarAtZ,
             0.0f, 1.0f, 0.0f);
    fe_solar_build_node_dls((Gfx *) (dyn + 0xACB0), dyn);
    text_shell_sort((u8 *) gSolarDrawList, 7, sizeof(DrawEntry), (s32 (*)(void *, void *)) fe_solar_cmp_depth);
    for (i = 0, found = 0; i < 7; i++) {
        if (!found || gGlobePlanet != 3) {
            gSPDisplayList(gfx++, gSolarDrawList[i].dl);
        }
        if (gSolarDrawList[i].node == 3) {
            found = 1;
        }
    }
    return gfx;
}

/* Draw-table comparator: the farther node first (0x801F36B0).
 * sort comparator for gSolarDrawList: by node depth */
s32 fe_solar_cmp_depth(DrawEntry *a, DrawEntry *b) {
    Node *nodes;
    Node *na;
    Node *nb;

    nodes = gSolarNodes;
    na = &nodes[&nodes[a->node] - nodes];
    nb = &nodes[&nodes[b->node] - nodes];
    return nb->depth - na->depth;
}

/* Orbit step, then the node's and its children's matrices (0x801F374C).
 * build the matrices of a node list and its children (recursive) */
void fe_solar_build_mtx(Node *node) {
    f32 scale;
    f32 x;
    f32 y;
    f32 z;
    f32 rot;
    Mtx *m;
    MtxF mf;

    while (node != NULL) {
        scale = node->size / gSolarEarthSize;
        m = gSolarNodeMtx[node - gSolarNodes];
        rot = 0.0f;
        if (node->period != 0.0f) {
            node->angle += gSolarEarthPeriod * 360.0 / node->period / 1800.0 * 60.0 / 60.0;
        }
        fe_globe_latlon_to_xyz(&rot, &node->angle, &x, &y, &z, node->orbitRadius);
        guTranslateF(mf, -x, -y, -z);
        guScale(&m[gBufferIndex + 2], scale, scale, scale);
        guMtxCatF(gSolarMtxStack[gSolarMtxTop], mf, gSolarMtxStack[gSolarMtxTop - 1]);
        gSolarMtxTop--;
        guMtxF2L(gSolarMtxStack[gSolarMtxTop], &m[gBufferIndex]);
        osWritebackDCache(m, 0x100);
        fe_solar_update_node(node, scale);
        fe_solar_build_mtx(node->child);
        gSolarMtxTop++;
        node = node->next;
    }
}

#define ABS(x) ((x) > 0 ? (x) : -(x))

/* A planet's glow sprite (0x801F3964).
 * a node's glow sprite: a screen-space textured quad (64x64 IA8) at the
 * node's projected position, sized by scale and depth */
Gfx *fe_solar_draw_glow(Gfx *gdl, u8 *dyn, Node *node, f32 scale) {
    Gfx *gfx;
    f32 size;
    s16 sx;
    s16 sy;
    u8 *tex;
    Vtx *vtx;
    s32 isize;

    gfx = gdl;
    size = node->size * 64.0 / gSolarEarthSize;
    vtx = gSolarSpriteVtx[node - gSolarNodes][gBufferIndex];
    gDPPipeSync(gfx++);
    gDPSetPrimColor(gfx++, 0xFF, 0xFF, node->rgb[0], node->rgb[1], node->rgb[2], 0xFF);
    gDPSetRenderMode(gfx++, G_RM_XLU_SURF, G_RM_XLU_SURF2);
    gDPSetTextureFilter(gfx++, G_TF_BILERP);
    gSPClearGeometryMode(gfx++, 0xFFFFFFFF);
    gSPSetGeometryMode(gfx++, G_SHADE | G_SHADING_SMOOTH);
    gSPTexture(gfx++, 0x2000, 0x2000, 0, G_TX_RENDERTILE, G_ON);
    gDPSetCombine(gfx++, 0x11FE23, 0xFFFFF3F9);
    switch (node - gSolarNodes) {
        case 1:
        case 4:
            tex = gLvlselGlowTex2;
            break;
        default:
            tex = gLvlselGlowTex;
            break;
    }
    if (node->depth > 1.0) {
        size = size * 10000.0 / node->depth;
    }
    isize = size;
    hud_project_point(dyn, 0.0f, 0.0f, 0.0f, &sx, &sy, &gSolarNodeMtx[node - gSolarNodes][gBufferIndex],
                  &gSolarNodeMtx[node - gSolarNodes][gBufferIndex] + 2, (Mtx *) (dyn + 0x1280), 4.0f);
    if (ABS(sx) + size / 2.0 < 4096.0 && ABS(sy) + size / 2.0 < 4096.0) {
        gDPLoadTextureBlock(gfx++, tex, G_IM_FMT_IA, G_IM_SIZ_8b, 64, 64, 0, G_TX_CLAMP, G_TX_MIRROR, G_TX_NOMASK,
                            6, G_TX_NOLOD, G_TX_NOLOD);
        vtx[0].v.ob[0] = sx - isize / 2;
        vtx[0].v.ob[1] = sy - isize / 2;
        vtx[0].v.ob[2] = -10;
        vtx[0].v.tc[0] = 0;
        vtx[0].v.tc[1] = 0;
        vtx[1].v.ob[0] = sx - isize / 2;
        vtx[1].v.ob[1] = sy + isize / 2;
        vtx[1].v.ob[2] = -10;
        vtx[1].v.tc[0] = 0;
        vtx[1].v.tc[1] = 0x3F00;
        vtx[2].v.ob[0] = sx + isize / 2;
        vtx[2].v.ob[1] = sy + isize / 2;
        vtx[2].v.ob[2] = -10;
        vtx[2].v.tc[0] = 0x3F00;
        vtx[2].v.tc[1] = 0x3F00;
        vtx[3].v.ob[0] = sx + isize / 2;
        vtx[3].v.ob[1] = sy - isize / 2;
        vtx[3].v.ob[2] = -10;
        vtx[3].v.tc[0] = 0x3F00;
        vtx[3].v.tc[1] = 0;
        gSPVertex(gfx++, vtx, 4, 0);
        gSP1Triangle(gfx++, 0, 1, 2, 0);
        gSP1Triangle(gfx++, 0, 3, 2, 0);
        gDPPipeSync(gfx++);
    }
    gDPPipeSync(gfx++);
    osWritebackDCache(vtx, 4 * sizeof(Vtx));
    return gfx;
}

/* The Sun's sprite (0x801F4110).
 * the same sprite for node 6, with a 32x32 RGBA32 texture */
Gfx *fe_solar_draw_sun(Gfx *gdl, u8 *dyn, Node *node, f32 scale) {
    Gfx *gfx;
    f32 size;
    s16 sx;
    s16 sy;
    Vtx *vtx;
    s32 isize;

    gfx = gdl;
    size = node->size * 64.0 / gSolarEarthSize;
    vtx = gSolarSpriteVtx[node - gSolarNodes][gBufferIndex];
    gDPPipeSync(gfx++);
    gDPSetPrimColor(gfx++, 0xFF, 0xFF, node->rgb[0], node->rgb[1], node->rgb[2], 0xFF);
    gDPSetRenderMode(gfx++, G_RM_XLU_SURF, G_RM_XLU_SURF2);
    gDPSetTextureFilter(gfx++, G_TF_BILERP);
    gSPClearGeometryMode(gfx++, 0xFFFFFFFF);
    gSPSetGeometryMode(gfx++, G_SHADE | G_SHADING_SMOOTH);
    gSPTexture(gfx++, 0x1000, 0x1000, 0, G_TX_RENDERTILE, G_ON);
    gDPSetCombine(gfx++, 0xFFB3FF, 0xFF64FE7F);
    if (node->depth > 1.0) {
        size = size * 10000.0 / node->depth;
    }
    isize = size;
    hud_project_point(dyn, 0.0f, 0.0f, 0.0f, &sx, &sy, &gSolarNodeMtx[node - gSolarNodes][gBufferIndex],
                  &gSolarNodeMtx[node - gSolarNodes][gBufferIndex] + 2, (Mtx *) (dyn + 0x1280), 4.0f);
    if (ABS(sx) + size / 2.0 < 4096.0 && ABS(sy) + size / 2.0 < 4096.0) {
        gDPLoadTextureBlock(gfx++, gLvlselSunTex, G_IM_FMT_RGBA, G_IM_SIZ_32b, 32, 32, 0, G_TX_CLAMP, G_TX_CLAMP,
                            G_TX_NOMASK, G_TX_NOMASK, G_TX_NOLOD, G_TX_NOLOD);
        vtx[0].v.ob[0] = sx - isize / 2;
        vtx[0].v.ob[1] = sy - isize / 2;
        vtx[0].v.ob[2] = -10;
        vtx[0].v.tc[0] = 0;
        vtx[0].v.tc[1] = 0;
        vtx[1].v.ob[0] = sx - isize / 2;
        vtx[1].v.ob[1] = sy + isize / 2;
        vtx[1].v.ob[2] = -10;
        vtx[1].v.tc[0] = 0;
        vtx[1].v.tc[1] = 0x3E00;
        vtx[2].v.ob[0] = sx + isize / 2;
        vtx[2].v.ob[1] = sy + isize / 2;
        vtx[2].v.ob[2] = -10;
        vtx[2].v.tc[0] = 0x3E00;
        vtx[2].v.tc[1] = 0x3E00;
        vtx[3].v.ob[0] = sx + isize / 2;
        vtx[3].v.ob[1] = sy - isize / 2;
        vtx[3].v.ob[2] = -10;
        vtx[3].v.tc[0] = 0x3E00;
        vtx[3].v.tc[1] = 0;
        gSPVertex(gfx++, vtx, 4, 0);
        gSP1Triangle(gfx++, 0, 1, 2, 0);
        gSP1Triangle(gfx++, 0, 3, 2, 0);
        gDPPipeSync(gfx++);
    }
    gDPPipeSync(gfx++);
    osWritebackDCache(vtx, 4 * sizeof(Vtx));
    return gfx;
}

/* Per node: eye distance, draw entry, matrices and model or sprite (0x801F4878).
 * per node: depth from the camera, draw table entry, matrices and model */
void fe_solar_build_node_dls(Gfx *gdl, u8 *dyn) {
    Gfx *gfx;
    s32 i;
    Node *nodes;
    Node *node;
    Mtx *m;
    f32 scale;
    f32 dx;
    f32 dy;
    f32 dz;

    gfx = gdl;
    for (i = 0; i < 7; i++) {
        nodes = gSolarNodes;
        node = &nodes[&nodes[i] - nodes];
        m = gSolarNodeMtx[i];
        scale = node->size / gSolarEarthSize;
        dx = node->posX - gSolarEyeX;
        dy = node->posY - gSolarEyeY;
        dz = node->posZ - gSolarEyeZ;
        node->depth = sqrtf(dx * dx + dy * dy + dz * dz);
        gSolarDrawList[node - gSolarNodes].node = node - gSolarNodes;
        gSolarDrawList[node - gSolarNodes].dl = gfx;
        switch (i) {
            case 3:
                gSPMatrix(gfx++, dyn + 0x80, G_MTX_PROJECTION | G_MTX_LOAD | G_MTX_NOPUSH);
                gSPMatrix(gfx++, dyn + 0x140, G_MTX_PROJECTION | G_MTX_MUL | G_MTX_NOPUSH);
                gSPMatrix(gfx++, &m[gBufferIndex], G_MTX_MODELVIEW | G_MTX_LOAD | G_MTX_NOPUSH);
                gSPMatrix(gfx++, &m[gBufferIndex + 2], G_MTX_MODELVIEW | G_MTX_MUL | G_MTX_NOPUSH);
                gSPMatrix(gfx++, dyn + 0x1280, G_MTX_MODELVIEW | G_MTX_MUL | G_MTX_NOPUSH);
                gfx = fe_globe_draw_earth(gfx, (s32) dyn);
                break;
            case 6:
                gSPMatrix(gfx++, dyn + 0x100, G_MTX_PROJECTION | G_MTX_LOAD | G_MTX_NOPUSH);
                gSPMatrix(gfx++, dyn + 0x1C0, G_MTX_MODELVIEW | G_MTX_LOAD | G_MTX_NOPUSH);
                gfx = fe_solar_draw_sun(gfx, dyn, node, scale);
                break;
            default:
                gSPMatrix(gfx++, dyn + 0x100, G_MTX_PROJECTION | G_MTX_LOAD | G_MTX_NOPUSH);
                gSPMatrix(gfx++, dyn + 0x1C0, G_MTX_MODELVIEW | G_MTX_LOAD | G_MTX_NOPUSH);
                gfx = fe_solar_draw_glow(gfx, dyn, node, scale);
                break;
        }
        gSPEndDisplayList(gfx++);
    }
}

/* A node's world position and its camera eye (0x801F4C3C).
 * node position from the current matrix, then move it */
void fe_solar_update_node(Node *node, f32 scale) {
    f32 w;
    f32 x;
    f32 y;

    x = 0.0f;
    y = 0.0f;
    if (node - gSolarNodes != 6) {
        w = gSolarMtxStack[gSolarMtxTop][3][3];
        node->posX = gSolarMtxStack[gSolarMtxTop][3][0] / w;
        node->posY = gSolarMtxStack[gSolarMtxTop][3][1] / w;
        node->posZ = gSolarMtxStack[gSolarMtxTop][3][2] / w;
    }
    switch (node - gSolarNodes) {
        case 3:
            fe_globe_latlon_to_xyz(&gGlobeCamLat, &gGlobeCamLon, &node->eyeX, &node->eyeY, &node->eyeZ, scale * gGlobeCamDist);
            node->eyeX += node->posX;
            node->eyeY += node->posY;
            node->eyeZ += node->posZ;
            break;
        default:
            fe_globe_latlon_to_xyz(&x, &y, &node->eyeX, &node->eyeY, &node->eyeZ, scale * gGlobeCamDist);
            node->eyeX += node->posX;
            node->eyeY += node->posY;
            node->eyeZ += node->posZ;
            break;
    }
}

/* Copies 16 words (no callers) (0x801F4E1C).
 * copy 16 words */
void fe_copy_mtx(s32 *arg0, s32 *arg1) {
    s32 *src = arg0;
    s32 *dst = arg1;
    u32 i;

    for (i = 0; i < 16; i++) {
        dst[i] = src[i];
    }
}
