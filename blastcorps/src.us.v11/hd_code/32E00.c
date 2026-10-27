#include "common.h"
#include <ultra64.h>
#ifndef NON_MATCHING /* legacy declarations */
/* Matching build: IDO compiled the matched code here against older
 * declarations of these, which the file keeps; the NON_MATCHING build
 * uses game/game.h's. */
#define LEGACY_gCarrierPanelEvents
#define LEGACY_gCarrierPanelEventsEnd
#endif /* legacy declarations */
#include "game/game.h"
/* Views: this file reads these shared variables (game/variables.h) as other types. */
#define seg2Base ((u8 *) seg2Base)
#ifdef NON_MATCHING
#define gCarrierPanelEvents (*(CarrierPanelEvent * N64P *) &gCarrierPanelEvents)
#define gCarrierPanelEventsEnd (*(CarrierPanelEvent * N64P *) &gCarrierPanelEventsEnd)
#endif
/* end of views */

/* The digger code (this file's .bss starts at 0x8036C8D0) */

/* 50-entry ring buffer of digger samples, indexed gDigSampleTail..gDigSampleHead */
typedef struct {
    /* 0x00 */ s16 unk0;
    /* 0x02 */ s16 unk2;
    /* 0x04 */ u8 unk4;   /* digger state when sampled */
    /* 0x08 */ s32 time;
} DigEntry;

/* Level section 0x58: a HUD panel shown once the missile carrier passes a z (8 bytes) */
typedef struct {
    /* 0x00 */ s16 carrierZ; /* fires when gCarrierZ >> 5 passes it */
    /* 0x02 */ u8 type;      /* hud_panel_start's arguments */
    /* 0x03 */ u8 arg1;
    /* 0x04 */ u8 arg2;
    /* 0x05 */ u8 sound;
    /* 0x06 */ u8 done;      /* set once shown */
} CarrierPanelEvent;

#ifndef NON_MATCHING
extern CarrierPanelEvent *gCarrierPanelEvents; /* first */
extern CarrierPanelEvent *gCarrierPanelEventsEnd; /* end */
#endif
extern u8 gHudPanelLoops;
extern u8 gHudPanelLoopCount;
extern u8 gHudPanelReversing;
extern u8 gHudPanelPingPong;
extern u8 gHudPanelFrame;
extern u8 gHudPanelFrameTick;
extern u8 gHudPanelFrameDelay;
extern u8 gHudPanelFrameCount;
extern s16 * N64P gHudPanelFrameIds; /* event animation frame ids */
extern u8 gHudPanelType;
extern u8 gHudPanelStaticTimer;
extern u8 gHudPanelStaticAlpha;      /* event panel alpha */
extern s16 gHudPanelAnim0[];
extern s16 gHudPanelAnim1[];
extern s16 gHudPanelAnim2[];
extern s16 gHudPanelAnim3[];
extern s16 gHudPanelAnim4[];
extern Vtx gHudPanelVtx[8];

extern DigEntry gDigSamples[50];
extern u8 gDigWeakPartHits;
extern void * N64P gHudPanelImageBufs[2];
extern u8 gDigSampleTail;
extern u8 gDigSampleHead;
extern u8 gDigStrongPartHits;
extern u8 gDigStrongHits;
extern u8 gDigWeakHits;
extern u8 gPanelActive;

s32 dig_hit_is_weak(void);
s32 dig_part_hit_is_weak(void);
void dig_count_samples(void);
void dig_check_doing_well(void);
void dig_check_wrong_digger(void);
void dig_check_misuse(void);

/* The message panel's two 40x40 frame buffers (0x802775C0). */
void hud_panel_init(void) {
    gPanelActive = 0;
    gHudPanelImageBufs[0] = gHeapPtr;
    gHudPanelImageBufs[1] = gHeapPtr = (u8 *) gHeapPtr + 0xC80;
    gHeapPtr = (u8 *) gHeapPtr + 0xC80;
    gDigSampleTail = 0;
    gDigSampleHead = 0;
}

/* Samples digger hits, runs the coaching checks and the carrier's panel events (0x80277620). */
void dig_coach_update(s32 now) {
    u8 done = 0;
    CarrierPanelEvent *p = gCarrierPanelEvents;
    s16 limit;

    while (!done && gDigSampleTail != gDigSampleHead) {
        if (now - gDigSamples[gDigSampleTail].time > 1000) {
            if (++gDigSampleTail == 50) {
                gDigSampleTail = 0;
            }
        } else {
            done = 1;
        }
    }
    if (gDigHit) {
        if (gDigSampleHead + 1 != gDigSampleTail && !(gDigSampleHead == 49 && gDigSampleTail == 0)) {
            gDigSamples[gDigSampleHead].unk0 = dig_hit_is_weak();
            gDigSamples[gDigSampleHead].unk2 = dig_part_hit_is_weak();
            gDigSamples[gDigSampleHead].unk4 = gDigHitVehicle;
            gDigSamples[gDigSampleHead].time = now;
            if (++gDigSampleHead == 50) {
                gDigSampleHead = 0;
            }
        }
    }
    if (gDigHit) {
        dig_count_samples();
        dig_check_doing_well();
        dig_check_wrong_digger();
        dig_check_misuse();
    }
    if (now == 50 && !gCarrierHit) {
        hud_panel_start(2, 1, 2, snd_random_id(SND_GROUP_LEVEL_START));
    }
    if (gHaveCarrier) {
        limit = gCarrierZ >> 5;
        while (p < gCarrierPanelEventsEnd) {
            if (!p->done && limit > p->carrierZ) {
                hud_panel_start(p->type, p->arg1, p->arg2, p->sound);
                p->done = 1;
            }
            p++;
        }
    }
    gDigHit = 0;
}

/* Most recent hits strong: "DOING REALLY WELL" (0x802778FC). */
void dig_check_doing_well(void) {
    switch (gCurVehicle) {
        case VEH_BACKLASH:
            if (gDigStrongHits >= 16 && gDigStrongPartHits >= 16) {
                hud_panel_start(3, 1, 1, SFX_COACH_WELL_BACKLASH);
                gDigSampleTail = 0;
                gDigSampleHead = 0;
                debug_printf("DOING REALLY WELL\n");
            }
            break;
        case VEH_RAMDOZER:
            if (gDigStrongHits >= 41 && gDigStrongPartHits >= 41) {
                hud_panel_start(4, 1, 1, snd_random_id(SND_GROUP_DOING_WELL));
                gDigSampleTail = 0;
                gDigSampleHead = 0;
                debug_printf("DOING REALLY WELL\n");
            }
            break;
        case VEH_SKYFALL:
            if (gDigStrongHits >= 11 && gDigStrongPartHits >= 11) {
                hud_panel_start(0, 1, 3, SFX_COACH_WELL_SKYFALL);
                gDigSampleTail = 0;
                gDigSampleHead = 0;
                debug_printf("DOING REALLY WELL\n");
            }
            break;
        case VEH_JBOMB:
            if (gDigStrongHits >= 26 && gDigStrongPartHits >= 26) {
                hud_panel_start(4, 1, 1, snd_random_id(SND_GROUP_DOING_WELL));
                gDigSampleTail = 0;
                gDigSampleHead = 0;
                debug_printf("DOING REALLY WELL\n");
            }
            break;
    }
}

/* Strong hits but weak part damage: "USING WRONG DIGGER" (0x80277AE0). */
void dig_check_wrong_digger(void) {
    switch (gCurVehicle) {
        case VEH_SKYFALL:
        case VEH_RAMDOZER:
        case VEH_BACKLASH:
        case VEH_JBOMB:
            if (gDigStrongHits >= 6 && gDigStrongPartHits < 2) {
                hud_panel_start(1, 1, 3, SFX_COACH_WRONG_DIGGER);
                gDigSampleTail = 0;
                gDigSampleHead = 0;
                debug_printf("USING WRONG DIGGER\n");
            }
            break;
    }
}

/* Most hits weak: "USING DIGGER INCORRECTLY" (0x80277B84). */
void dig_check_misuse(void) {
    switch (gCurVehicle) {
        case VEH_SKYFALL:
        case VEH_RAMDOZER:
        case VEH_BACKLASH:
            if (gDigWeakHits >= 31 && gDigStrongPartHits < 6) {
                hud_panel_start(2, 1, 2, SFX_COACH_MISUSE);
                gDigSampleTail = 0;
                gDigSampleHead = 0;
                debug_printf("USING DIGGER INCORRECTLY\n");
            }
            break;
    }
}

/* Counts strong and weak hits in the sample ring (0x80277C20). */
void dig_count_samples(void) {
    u8 i;

    gDigStrongPartHits = 0;
    i = gDigSampleTail;
    gDigStrongHits = 0;
    gDigWeakPartHits = 0;
    gDigWeakHits = 0;
    while (i != gDigSampleHead) {
        if (!gDigSamples[i].unk2) {
            gDigStrongPartHits++;
        } else {
            gDigWeakPartHits++;
        }
        if (!gDigSamples[i].unk0) {
            gDigStrongHits++;
        } else {
            gDigWeakHits++;
        }
        if (++i == 50) {
            i = 0;
        }
    }
}

/* 1 if the hit's damage is under the vehicle's limit (0x80277D34). */
s32 dig_hit_is_weak(void) {
    switch (gDigHitVehicle) {
        case VEH_BACKLASH:
            if (gDigHitDamage > 400) {
                return 0;
            }
            return 1;
        case VEH_RAMDOZER:
            if (gDigHitDamage > 400) {
                return 0;
            }
            return 1;
        case VEH_JBOMB:
            if (gDigHitDamage >= 100) {
                return 0;
            }
            return 1;
        case VEH_SKYFALL:
            if (gDigHitDamage >= 100) {
                return 0;
            }
            return 1;
    }
    return 1;
}

/* The same on the per-part damage (0x80277E08). */
s32 dig_part_hit_is_weak(void) {
    switch (gDigHitVehicle) {
        case VEH_BACKLASH:
            if (gDigHitDamagePerPart > 80) {
                return 0;
            }
            return 1;
        case VEH_RAMDOZER:
            if (gDigHitDamagePerPart > 80) {
                return 0;
            }
            return 1;
        case VEH_JBOMB:
            if (gDigHitDamagePerPart >= 100) {
                return 0;
            }
            return 1;
        case VEH_SKYFALL:
            if (gDigHitDamagePerPart >= 100) {
                return 0;
            }
            return 1;
    }
    return 1;
}

/* Starts message animation type 0..4 of the coaching panel, with its sound (0x80277EDC).
 * Starts a digger event. A K&R definition: callers pass plain ints, and the
 * u8 parameters read their low bytes */
#ifdef NON_MATCHING
void hud_panel_start(u8 type, u8 arg1, s32 arg2, u8 sound)
#else
void hud_panel_start(type, arg1, arg2, sound)
    u8 type;
    u8 arg1;
    s32 arg2;
    u8 sound;
#endif
{
#ifdef NON_MATCHING
    u8 pos = 0; /* types without a case (with gLevelKind == 1) leave it indeterminate in the original */
#else
    u8 pos;
#endif

    if ((gGameMode & (MODE_LEVEL | MODE_LEVEL_CLEAR | MODE_CARRIER_END | MODE_LEVEL_START | MODE_TIMED_END | MODE_YOSHI_PAGES)) && !gPanelActive) {
        if (sound) {
            snd_play(gSfxBank, sound, NULL);
        }
        gPanelActive = 1;
        gHudPanelLoops = arg2;
        gHudPanelLoopCount = 0;
        gHudPanelReversing = 0;
        gHudPanelFrame = 0;
        gHudPanelFrameTick = 0;
        gHudPanelPingPong = arg1;
        gHudPanelType = type;
        gHudPanelStaticTimer = 0;
        switch (type) {
            case 0:
                pos = 0;
                gHudPanelFrameCount = 13;
                gHudPanelFrameIds = gHudPanelAnim0;
                gHudPanelFrameDelay = 1;
                break;
            case 1:
                pos = 1;
                gHudPanelFrameCount = 21;
                gHudPanelFrameIds = gHudPanelAnim1;
                gHudPanelFrameDelay = 1;
                break;
            case 2:
                pos = 0;
                gHudPanelFrameCount = 21;
                gHudPanelFrameIds = gHudPanelAnim2;
                gHudPanelFrameDelay = 2;
                break;
            case 3:
                pos = 1;
                gHudPanelFrameCount = 30;
                gHudPanelFrameIds = gHudPanelAnim3;
                gHudPanelFrameDelay = 1;
                break;
            case 4:
                pos = 0;
                gHudPanelFrameCount = 34;
                gHudPanelFrameIds = gHudPanelAnim4;
                gHudPanelFrameDelay = 1;
                break;
        }
        if (gLevelKind != LEVEL_KIND_MISSION) {
            pos = 1;
        }
        switch (pos) {
            case 0:
                gHudPanelVtx[0].v.ob[0] = 32, gHudPanelVtx[0].v.ob[1] = 68;
                gHudPanelVtx[1].v.ob[0] = 78, gHudPanelVtx[1].v.ob[1] = 68;
                gHudPanelVtx[2].v.ob[0] = 78, gHudPanelVtx[2].v.ob[1] = 23;
                gHudPanelVtx[3].v.ob[0] = 32, gHudPanelVtx[3].v.ob[1] = 23;
                gHudPanelVtx[4].v.ob[0] = 26, gHudPanelVtx[4].v.ob[1] = 73;
                gHudPanelVtx[5].v.ob[0] = 84, gHudPanelVtx[5].v.ob[1] = 73;
                gHudPanelVtx[6].v.ob[0] = 84, gHudPanelVtx[6].v.ob[1] = 13;
                gHudPanelVtx[7].v.ob[0] = 26, gHudPanelVtx[7].v.ob[1] = 13;
                break;
            case 1:
                gHudPanelVtx[0].v.ob[0] = 238, gHudPanelVtx[0].v.ob[1] = 222;
                gHudPanelVtx[1].v.ob[0] = 284, gHudPanelVtx[1].v.ob[1] = 222;
                gHudPanelVtx[2].v.ob[0] = 284, gHudPanelVtx[2].v.ob[1] = 177;
                gHudPanelVtx[3].v.ob[0] = 238, gHudPanelVtx[3].v.ob[1] = 177;
                gHudPanelVtx[4].v.ob[0] = 232, gHudPanelVtx[4].v.ob[1] = 227;
                gHudPanelVtx[5].v.ob[0] = 290, gHudPanelVtx[5].v.ob[1] = 227;
                gHudPanelVtx[6].v.ob[0] = 290, gHudPanelVtx[6].v.ob[1] = 167;
                gHudPanelVtx[7].v.ob[0] = 232, gHudPanelVtx[7].v.ob[1] = 167;
                break;
        }
    }
}

/* Stops the message panel (0x80278318). */
void hud_panel_stop(void) {
    gPanelActive = 0;
}

/* Inflates the frame, animates, fades and draws the panel (0x80278324).
 * Animates and draws the digger event panel */
void hud_panel_draw(Gfx **gfx, s32 arg1, u8 buf) {
    Gfx *gdl = *gfx;

    /* widescreen HUD sides (port.h): the top-left or bottom-right panel */
    PORT_WTAG(gdl, PORT_WTAG_SPAN, gHudPanelVtx[4].v.ob[0], gHudPanelVtx[5].v.ob[0]);
    if (gPanelActive) {
        tex_queue_load(gHudPanelFrameIds[gHudPanelFrame], gHudPanelImageBufs[buf], 0);
        switch (gHudPanelPingPong) {
            case 1:
                if (!gHudPanelReversing) {
                    gHudPanelFrameTick++;
                    if (gHudPanelFrameTick == gHudPanelFrameDelay) {
                        gHudPanelFrameTick = 0;
                        gHudPanelFrame++;
                        if (gHudPanelFrame + 1 == gHudPanelFrameCount) {
                            gHudPanelReversing = 1;
                            gHudPanelLoopCount++;
                        }
                    }
                } else {
                    gHudPanelFrameTick++;
                    if (gHudPanelFrameTick == gHudPanelFrameDelay) {
                        gHudPanelFrameTick = 0;
                        gHudPanelFrame--;
                        if (gHudPanelFrame == 0) {
                            gHudPanelReversing = 0;
                            gHudPanelLoopCount++;
                        }
                    }
                }
                break;
            case 0:
                gHudPanelFrameTick++;
                if (gHudPanelFrameTick == gHudPanelFrameDelay) {
                    gHudPanelFrameTick = 0;
                    gHudPanelFrame++;
                    if (gHudPanelFrame == gHudPanelFrameCount) {
                        gHudPanelFrame = 0;
                        gHudPanelLoopCount++;
                    }
                }
                break;
        }
        if (gHudPanelLoopCount == gHudPanelLoops) {
            gPanelActive = 0;
        }
        if (gHudPanelLoopCount == 0 && gHudPanelFrame < 5) {
            if (gHudPanelFrame == 0) {
                snd_play(gSfxBank, SFX_STATIC, NULL);
            }
            gHudPanelStaticAlpha = 0x50;
        } else if (gHudPanelStaticTimer) {
            gHudPanelStaticAlpha -= 15;
            gHudPanelStaticTimer--;
        } else if (!util_rand_range(0, 20)) {
            gHudPanelStaticTimer = 5;
            gHudPanelStaticAlpha = 0x50;
            snd_play(gSfxBank, SFX_STATIC, NULL);
        } else {
            gHudPanelStaticAlpha = 0;
        }
        gSPMatrix(gdl++, (u32) seg2Base + 0xC0, G_MTX_PROJECTION | G_MTX_LOAD | G_MTX_NOPUSH);
        gSPMatrix(gdl++, (u32) seg2Base + 0x1C0, G_MTX_MODELVIEW | G_MTX_LOAD | G_MTX_NOPUSH);
        gDPPipeSync(gdl++);
        gDPSetCycleType(gdl++, G_CYC_2CYCLE);
        gDPSetRenderMode(gdl++, G_RM_PASS, G_RM_OPA_SURF2);
        gSPClearGeometryMode(gdl++, 0xFFFFFFFF);
        gSPSetGeometryMode(gdl++, G_SHADE | G_SHADING_SMOOTH);
        gSPTexture(gdl++, 0xFFFF, 0xFFFF, 0, G_TX_RENDERTILE, G_ON);
        gDPSetCombine(gdl++, 0xFC757E44, 0xFFFFF83C);
        gDPSetPrimColor(gdl++, 0, 0, 0, 0, 0, gHudPanelStaticAlpha);
        gDPLoadTextureBlock(gdl++, (u32) gHudPanelImageBufs[buf] - 0x80000000, G_IM_FMT_RGBA, G_IM_SIZ_16b, 40, 40, 0,
                            G_TX_MIRROR | G_TX_CLAMP, G_TX_MIRROR | G_TX_CLAMP, G_TX_NOMASK, G_TX_NOMASK, G_TX_NOLOD,
                            G_TX_NOLOD);
        gSPVertex(gdl++, (u32) gHudPanelVtx - 0x80000000, 8, 0);
        gSP1Triangle(gdl++, 0, 1, 2, 0);
        gSP1Triangle(gdl++, 0, 2, 3, 0);
        gDPPipeSync(gdl++);
        gDPSetCycleType(gdl++, G_CYC_1CYCLE);
        gDPSetRenderMode(gdl++, G_RM_XLU_SURF, G_RM_XLU_SURF2);
        gDPSetCombineMode(gdl++, G_CC_MODULATERGBA, G_CC_MODULATERGBA);
        gDPLoadTextureBlock(gdl++, (u32) gHudFrameTex - 0x80000000, G_IM_FMT_RGBA, G_IM_SIZ_32b, 32, 32, 0,
                            G_TX_MIRROR | G_TX_CLAMP, G_TX_MIRROR | G_TX_CLAMP, G_TX_NOMASK, G_TX_NOMASK, G_TX_NOLOD,
                            G_TX_NOLOD);
        gSP1Triangle(gdl++, 4, 5, 6, 0);
        gSP1Triangle(gdl++, 4, 6, 7, 0);
        gDPPipeSync(gdl++);
    }
    PORT_WTAG(gdl, PORT_WTAG_NONE, 0, 0);
    *gfx = gdl;
}
