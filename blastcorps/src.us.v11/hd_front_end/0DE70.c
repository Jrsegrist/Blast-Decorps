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
#define gDynamicBufs ((FeDyn *) gDynamicBufs)
#ifdef NON_MATCHING
#define gHeapPtr (*(s32 *) &gHeapPtr)
#endif
/* end of views */

/*
 * Front-end 3D scene setup: loads one of three compressed scene blobs, and
 * builds the display lists that draw it (two per-frame-buffer camera setups).
 */


/* header of an inflated scene blob: offsets relative to its start */
typedef struct {
    s32 unk0;
    s32 unk4;
    u8 pad8[0xC];
    s32 unk14; /* segment 6 base */
    s32 unk18;
    s32 unk1C; /* display list */
    s32 unk20;
} FeHdr;

#ifndef NON_MATCHING
extern s32 gHeapPtr; /* heap pointer */
#endif

/* ROM addresses of the compressed scene blobs */
extern u8 romNinLogoScene[];
extern u8 romReflectLogoScene[];
extern u8 romPackedObjTable[];

extern u32 gFeSceneSize;
extern u8 * N64P gFeSceneRom;
extern Gfx * N64P gFeSceneDl;
extern void * N64P gFeSceneSeg6;
extern u8 * N64P gCommPointModel;
extern void * N64P gCommPointBufs[2];
extern Gfx * N64P gCommPointDls[2][2];
extern Mtx gCommPointTranslate;
extern Mtx gCommPointScale;
extern Mtx gCommPointTilt;
extern u8 gCommPointAnim[];
extern u32 gCommPointAngle;
extern u16 gCommPointPerspNorm;


/* Inflates scene blob 0/1/2 onto the heap (0x801F4E70).
 * Inflate scene blob `arg0` onto the heap and record its parts. */
void fe_scene_load(u8 arg0) {
    FeHdr *hdr;
    void *p;

    tex_load_table();
    switch (arg0) {
        case 0:
            gFeSceneRom = romUsaStarScene;
            gFeSceneSize = romNinLogoScene - romUsaStarScene;
            break;
        case 1:
            gFeSceneRom = romNinLogoScene;
            gFeSceneSize = romReflectLogoScene - romNinLogoScene;
            break;
        case 2:
            gFeSceneRom = romReflectLogoScene;
            gFeSceneSize = romPackedObjTable - romReflectLogoScene;
            break;
    }
    hd_rom_load((u32) gFeSceneRom, (u32) ((void *) N64_IPTR(gHeapPtr)), &gFeSceneSize, 12, 10, 1);
    hdr = (FeHdr *) N64_IPTR(gHeapPtr);
    gHeapPtr += gFeSceneSize;
    gFeSceneDl = (Gfx *) (hdr->unk1C + (u32) hdr);
    p = (void *) (hdr->unk20 + (u32) hdr);
    gFeSceneSeg6 = (void *) (hdr->unk14 + (u32) hdr);
    tex_resolve_dl((u32 *) gFeSceneDl, p);
}

/* Draws the inflated scene (0x801F4FBC).
 * Draw the scene loaded by fe_scene_load. */
Gfx *fe_scene_draw(FeDyn *dyn, Gfx *arg1) {
    Gfx *gdl = arg1;

    gSPSegment(gdl++, 6, gFeSceneSeg6);
    gSPSegment(gdl++, 7, &gFeSceneRot[gBufferIndex]);
    gImmp1(gdl++, G_RDPHALF_1, gPerspNorm);
    gSPLookAtX(gdl++, &dyn->lookAt);
    gSPLookAtY(gdl++, (u8 *) &dyn->lookAt + 0x10);
    gSPMatrix(gdl++, &dyn->persp, G_MTX_PROJECTION | G_MTX_LOAD | G_MTX_NOPUSH);
    gSPMatrix(gdl++, &dyn->view, G_MTX_PROJECTION | G_MTX_MUL | G_MTX_NOPUSH);
    gSPMatrix(gdl++, &dyn->sceneRotate, G_MTX_MODELVIEW | G_MTX_LOAD | G_MTX_NOPUSH);
    gSPMatrix(gdl++, &dyn->sceneTranslate, G_MTX_MODELVIEW | G_MTX_MUL | G_MTX_NOPUSH);
    gSPMatrix(gdl++, &dyn->sceneScale, G_MTX_MODELVIEW | G_MTX_MUL | G_MTX_NOPUSH);
    gSPDisplayList(gdl++, gFeSceneDl);
    return gdl;
}

/* The spinning communication-point model on the results screens (0x801F51C8).
 * Draw the spinning model set up by fe_comm_point_load (turns 3 degrees a frame). */
Gfx *fe_comm_point_draw(FeDyn *dyn, Gfx *arg1) {
    Gfx *gdl = arg1;
    void *seg;

    gImmp1(gdl++, G_RDPHALF_1, gCommPointPerspNorm);
    gSPLookAtX(gdl++, &dyn->lookAt);
    gSPLookAtY(gdl++, (u8 *) &dyn->lookAt + 0x10);
    gSPMatrix(gdl++, &dyn->commPointPersp, G_MTX_PROJECTION | G_MTX_LOAD | G_MTX_NOPUSH);
    gSPMatrix(gdl++, &dyn->view, G_MTX_PROJECTION | G_MTX_MUL | G_MTX_NOPUSH);
    gDPSetEnvColor(gdl++, 0, 0, 0, gYoshiAlpha);
    {
        s32 base;
        Mtx *m;

        base = ((FeHdr *) (((FeHdr *) gCommPointModel)->unk18 + gCommPointModel))->unk4;
        m = (Mtx *) ((u32) gCommPointBufs[gBufferIndex] + base);
        fe_anim_update_model(gCommPointAnim, gCommPointBufs[gBufferIndex], gCommPointBufs[gBufferIndex ^ 1]);
        gCommPointAngle += 3;
        guRotate(m, gCommPointAngle % 360, 0.0f, 1.0f, 0.0f);
        osWritebackDCache(m, sizeof(Mtx));
    }
    seg = (void *) (((FeHdr *) gCommPointModel)->unk14 + gCommPointModel);
    gSPSegment(gdl++, 6, seg);
    gSPSegment(gdl++, 7, gCommPointBufs[gBufferIndex]);
    gSPMatrix(gdl++, &gCommPointTilt, G_MTX_MODELVIEW | G_MTX_LOAD | G_MTX_NOPUSH);
    gSPMatrix(gdl++, &gCommPointTranslate, G_MTX_MODELVIEW | G_MTX_MUL | G_MTX_PUSH);
    gSPMatrix(gdl++, &gCommPointScale, G_MTX_MODELVIEW | G_MTX_MUL | G_MTX_PUSH);
    gSPDisplayList(gdl++, gCommPointDls[0][gBufferIndex]);
    gSPMatrix(gdl++, &gCommPointTilt, G_MTX_MODELVIEW | G_MTX_LOAD | G_MTX_NOPUSH);
    gSPMatrix(gdl++, &gCommPointTranslate, G_MTX_MODELVIEW | G_MTX_MUL | G_MTX_PUSH);
    gSPMatrix(gdl++, &gCommPointScale, G_MTX_MODELVIEW | G_MTX_MUL | G_MTX_PUSH);
    gSPDisplayList(gdl++, gCommPointDls[1][gBufferIndex]);
    return gdl;
}

/* Loads the communication-point model, both cameras (0x801F55D8).
 * Load the spinning model and set up both frame buffers' cameras. */
void fe_comm_point_load(void) {
    s32 i;
    FeDyn *dyn;

    fe_load_model(0x96, &gCommPointModel, (u8 * N64P *) gCommPointBufs, (u8 * N64P *) gCommPointDls[0]);
    guTranslate(&gCommPointTranslate, 150.0f, -45.0f, 0.0f);
    guScale(&gCommPointScale, 1.5f, 1.5f, 1.5f);
    fe_anim_init_model(gCommPointModel, (u32 * N64P *) gCommPointBufs, gCommPointAnim);
    fe_anim_start_channel(gCommPointAnim, 1, 0, 0, 0.0f, 2, 0);
    fe_anim_start_channel(gCommPointAnim, 2, 0, 0, 0.0f, 1, 1);
    guRotate(&gCommPointTilt, 20.0f, 1.0f, 0.0f, 0.0f);
    for (i = 0; i < 2; i++) {
        dyn = &gDynamicBufs[i];
        guPerspective(&dyn->commPointPersp, &gCommPointPerspNorm, 45.0f, 1.3333334f, 40.0f, 4000.0f, 1.0f);
        guLookAtReflect(&dyn->view, &dyn->lookAt, 0.0f, 1.0f, 400.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f);
    }
}
