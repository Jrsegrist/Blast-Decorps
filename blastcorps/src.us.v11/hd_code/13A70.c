#include "common.h"
#include <ultra64.h>
#ifndef NON_MATCHING /* legacy declarations */
/* Matching build: IDO compiled the matched code here against older
 * declarations of these, which the file keeps; the NON_MATCHING build
 * uses game/game.h's. */
#define LEGACY_gObjectsEnd
#endif /* legacy declarations */
#include "game/game.h"
/* Views: this file reads these shared variables (game/variables.h) as other types. */
#define seg2Base (*(VtxBuf *) seg2Base)
#define gObjects ((LevelObject *) gObjects)
#ifdef NON_MATCHING
/* (the same type as variables.h's, but reading the pointers through their
 * address keeps IDO's NON_MATCHING code, and so the NM ROM, as it was) */
#define gVehicleShadows (*(VehShadow * N64P *) &gVehicleShadows)
#define gVehicleShadowsEnd (*(VehShadow * N64P *) &gVehicleShadowsEnd)
#define gObjectsEnd (*(LevelObject * N64P *) &gObjectsEnd)
#endif
/* end of views */

/* Shadow/marker records (0x1040-byte entries with a 4 KB buffer each), the
 * 64x64 off-screen render that fills one, and the ground-shadow quads drawn
 * under objects of type 0xBA..0xBC. */

#define PHYS(x) ((u32)(x) & 0x1FFFFFFF)

/* XLU surfaces with CVG_DST_SAVE where the 2.0I headers have CVG_DST_FULL */
#define RM_ZB_XLU_SAVE(clk) (RM_ZB_XLU_SURF(clk) | CVG_DST_SAVE)
#define RM_XLU_SAVE(clk) (RM_XLU_SURF(clk) | CVG_DST_SAVE)

/* (the shadow records are VehShadow, game/types.h) */

/* (gObjects records: LevelObject, game/object.h) */

typedef struct VtxBuf {
    u8 pad[0x3900];
    Vtx vtx[1];
} VtxBuf;

extern Gfx gShadowRenderDl[];
extern Vp gShadowViewport;
extern Mtx gShadowProjMtx;
extern Mtx gShadowPosMtx;
extern Mtx gShadowTiltMtx;
extern Mtx gShadowTurnMtx;
extern Mtx gShadowDistMtx;
#ifndef NON_MATCHING
extern LevelObject *gObjectsEnd;
#endif


/* Appends a gVehicleShadows record for a vehicle type (0x80258230). */
void veh_add_shadow(u8 id, s32 arg1, s16 arg2, s16 arg3) {
    gVehicleShadowsEnd->type = id;
    gVehicleShadowsEnd->layer = 0;
    gVehicleShadowsEnd->renderDist = arg1;
    gVehicleShadowsEnd->halfWidth = arg2;
    gVehicleShadowsEnd->halfDepth = arg3;
    gVehicleShadowsEnd->rotX = 0;
    gVehicleShadowsEnd->rotY = 0;
    gVehicleShadowsEnd->rotZ = 0;
    gVehicleShadowsEnd++;
}

/* Sets the type's shadow position (J-Bomb's: the floor below) (0x802582C4). */
void veh_update_shadow(u8 id, s32 x, s32 y, s32 z, s32 arg4, s32 arg5, s32 arg6, s32 arg7) {
    u8 found = 0;
    VehShadow *p;
    u8 hit = 0;
    u8 mode = 0;
    u8 ok;
    s16 h;
    s32 d1;
    s32 d2;
    s32 d3;

    if (id == 9) {
        hit = ground_over_object(x, arg4, z);
        if (hit) {
            mode = 1;
        }
        ok = water_get_height(x >> 5, z >> 5, &h);
        if (ok) {
            d1 = arg4 - (h << 5);
            d3 = arg4 - y;
            if (hit) {
                d2 = arg4 - gObjectFloorY;
            } else {
                d2 = 9999999;
            }
            if (d1 > 0 && d1 < d2 && d1 < d3) {
                gObjectFloorY = h << 5;
                mode = 2;
            }
        }
    }
    for (p = gVehicleShadows; !found;) {
        if (p->type == id) {
            p->x = x;
            if (mode) {
                p->floorY = gObjectFloorY;
            } else {
                p->floorY = y;
            }
            p->z = z;
            p->groundY = y;
            p->rotX = arg5;
            p->rotY = arg7;
            p->rotZ = arg6;
            p->bodyY = arg4;
            p->layer = mode;
            found = 1;
        } else {
            p++;
        }
    }
}

/* The type's shadow y (0x802584BC). */
s32 veh_shadow_floor_y(u8 id) {
    VehShadow *p = gVehicleShadows;

    while (TRUE) {
        if (p->type == id) {
            return p->floorY;
        }
        p++;
    }
}

/* The type's y as last given to veh_update_shadow (0x80258500). */
s32 veh_shadow_src_y(u8 id) {
    VehShadow *p = gVehicleShadows;

    while (TRUE) {
        if (p->type == id) {
            return p->groundY;
        }
        p++;
    }
}

/* Renders a vehicle top-down into its 64x64 shadow texture (0x80258544). */
void veh_render_shadow_tex(void *cimg, s32 x, s32 y, s32 z, f32 dist, Gfx *dl, void *seg6, void *seg7) {
    Gfx *gdl = gShadowRenderDl;
    u16 perspNorm;
    s32 pad;

    gSPViewport(gdl++, PHYS(&gShadowViewport));
    gSPClearGeometryMode(gdl++, 0xFFFFFFFF);
    gSPTexture(gdl++, 0xFFFF, 0xFFFF, 0, G_TX_RENDERTILE, G_OFF);
    gSPSegment(gdl++, 0, 0);
    gSPSegment(gdl++, 6, PHYS(seg6));
    gSPSegment(gdl++, 7, PHYS(seg7));
    gDPPipeSync(gdl++);
    gDPSetScissor(gdl++, G_SC_NON_INTERLACE, 0, 0, 63, 63);
    gDPSetPrimColor(gdl++, 0, 0, 255, 255, 255, 255);
    gDPSetColorDither(gdl++, G_CD_DISABLE);
    gDPSetCycleType(gdl++, G_CYC_FILL);
    gDPSetColorImage(gdl++, G_IM_FMT_CI, G_IM_SIZ_8b, 64, PHYS(cimg));
    gDPSetFillColor(gdl++, 0x00010001);
    gDPFillRectangle(gdl++, 0, 0, 63, 63);
    gDPPipeSync(gdl++);
    gDPSetCycleType(gdl++, G_CYC_1CYCLE);
    gDPSetRenderMode(gdl++, GBL_c1(G_BL_CLR_IN, G_BL_0, G_BL_CLR_IN, G_BL_1), GBL_c2(G_BL_CLR_IN, G_BL_0, G_BL_CLR_IN, G_BL_1));
    gDPSetCombineMode(gdl++, G_CC_PRIMITIVE, G_CC_PRIMITIVE);
    guPerspective(&gShadowProjMtx, &perspNorm, 45.0f, 1.0f, 1.0f, 1000.0f, 1.0f);
    gSPMatrix(gdl++, PHYS(&gShadowProjMtx), G_MTX_PROJECTION | G_MTX_LOAD | G_MTX_NOPUSH);
    gImmp1(gdl++, G_RDPHALF_1, perspNorm);
    guTranslate(&gShadowDistMtx, 0.0f, 0.0f, -dist);
    gSPMatrix(gdl++, PHYS(&gShadowDistMtx), G_MTX_MODELVIEW | G_MTX_LOAD | G_MTX_NOPUSH);
    guRotate(&gShadowTiltMtx, -90.0f, 1.0f, 0.0f, 0.0f);
    gSPMatrix(gdl++, PHYS(&gShadowTiltMtx), G_MTX_MODELVIEW | G_MTX_MUL | G_MTX_NOPUSH);
    guRotate(&gShadowTurnMtx, 180.0f, 0.0f, 1.0f, 0.0f);
    gSPMatrix(gdl++, PHYS(&gShadowTurnMtx), G_MTX_MODELVIEW | G_MTX_MUL | G_MTX_NOPUSH);
    guTranslate(&gShadowPosMtx, -(x / 32.0f), -(y / 32.0f), -(z / 32.0f));
    gSPMatrix(gdl++, PHYS(&gShadowPosMtx), G_MTX_MODELVIEW | G_MTX_MUL | G_MTX_NOPUSH);
    gSPDisplayList(gdl++, PHYS(dl));
    gSPEndDisplayList(gdl++);
    osWritebackDCache(&gShadowProjMtx, 0x140);
    rsp_send_task((u64 *) gShadowRenderDl, gdl - gShadowRenderDl, 1, 0, 0x61F, 0);
}

/* Blob shadows under objects of type 0xBA..0xBC (0x80258B78). */
void obj_draw_ground_shadows(Gfx **gdlp, VtxBuf *buf) {
    Gfx *gdl = *gdlp;
    s32 i = 0;
    s32 n = 0;
    s32 h;
    s16 x;
    s16 y;
    s16 z;
    s16 loaded = 0;

    gDPPipeSync(gdl++);
    gDPSetCycleType(gdl++, G_CYC_1CYCLE);
    gSPClearGeometryMode(gdl++, 0xFFFFFFFF);
    gSPSetGeometryMode(gdl++, G_SHADE | G_SHADING_SMOOTH);
    gSPTexture(gdl++, 0xFFFF, 0xFFFF, 0, G_TX_RENDERTILE, G_ON);
    gDPSetCombineMode(gdl++, G_CC_MODULATEIA_PRIM, G_CC_MODULATEIA_PRIM);
    gDPSetPrimColor(gdl++, 0, 0, 200, 200, 200, 200);
    while (&gObjects[i] != gObjectsEnd) {
        if (gObjects[i].destroyed == 0) {
            switch (gObjects[i].kind) {
                case OBJ_KIND_SHADOW_A:
                case OBJ_KIND_SHADOW_B:
                case OBJ_KIND_SHADOW_C:
                    h = 60 - (gObjects[i].pos[1] - gObjects[i].groundY) / 800;
                    if (h < 0) {
                        h = 0;
                    }
                    x = gObjects[i].pos[0] >> 5;
                    y = gObjects[i].groundY >> 5;
                    z = gObjects[i].pos[2] >> 5;
                    buf->vtx[n].v.ob[0] = x - h;
                    buf->vtx[n].v.ob[1] = y;
                    buf->vtx[n].v.ob[2] = z - h;
                    buf->vtx[n].v.tc[0] = 0;
                    buf->vtx[n].v.tc[1] = 0;
                    n++;
                    buf->vtx[n].v.ob[0] = x + h;
                    buf->vtx[n].v.ob[1] = y;
                    buf->vtx[n].v.ob[2] = z - h;
                    buf->vtx[n].v.tc[0] = 0x7E0;
                    buf->vtx[n].v.tc[1] = 0;
                    n++;
                    buf->vtx[n].v.ob[0] = x + h;
                    buf->vtx[n].v.ob[1] = y;
                    buf->vtx[n].v.ob[2] = z + h;
                    buf->vtx[n].v.tc[0] = 0x7E0;
                    buf->vtx[n].v.tc[1] = 0x7E0;
                    n++;
                    buf->vtx[n].v.ob[0] = x - h;
                    buf->vtx[n].v.ob[1] = y;
                    buf->vtx[n].v.ob[2] = z + h;
                    buf->vtx[n].v.tc[0] = 0;
                    buf->vtx[n].v.tc[1] = 0x7E0;
                    n++;
                    if (!loaded) {
                        gDPLoadTextureBlock(gdl++, OS_K0_TO_PHYSICAL(gObjShadowTex), G_IM_FMT_IA, G_IM_SIZ_8b, 64, 64, 0,
                                            G_TX_CLAMP, G_TX_CLAMP, G_TX_NOMASK, G_TX_NOMASK, G_TX_NOLOD, G_TX_NOLOD);
                        loaded = 1;
                    }
                    PORT_ITAG(gdl, PORT_ITAG_GSHADOW, i);
                    gDPPipeSync(gdl++);
                    if (levelno == 0x10 && x == 0x19B7 && z == 0xF99) {
                        gSPSetGeometryMode(gdl++, G_ZBUFFER);
                        gDPSetRenderMode(gdl++, RM_ZB_XLU_SAVE(1), RM_ZB_XLU_SAVE(2));
                    } else {
                        gSPClearGeometryMode(gdl++, G_ZBUFFER);
                        gDPSetRenderMode(gdl++, RM_XLU_SAVE(1), RM_XLU_SAVE(2));
                    }
                    gSPVertex(gdl++, &seg2Base.vtx[n - 4], 4, 0);
                    gSP1Triangle(gdl++, 0, 1, 3, 0);
                    gSP1Triangle(gdl++, 1, 2, 3, 0);
                    break;
            }
        }
        i++;
    }
    PORT_ITAG(gdl, PORT_ITAG_NONE, 0);
    gDPPipeSync(gdl++);
    *gdlp = gdl;
}
