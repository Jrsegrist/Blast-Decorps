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
#define gYoshiFeItems ((YoshiItem *) gYoshiFeItems)
#define gGlobeLevels ((FeLevelEntry *) gGlobeLevels)
#define gLevelInfo ((LevelInfo *) gLevelInfo)
#define gYoshiSprites ((IconInfo *) gYoshiSprites)
#define gDynamicBufs ((FeDyn *) gDynamicBufs)
#define players ((PlayerInfo *) players)
#ifdef NON_MATCHING
#define gHeapPtr (*(s32 *) &gHeapPtr)
#endif
/* end of views */

/*
 * stats.c (named by its assert): end-of-level results. Grades the time,
 * records best times and units, and sets up the results screen.
 */

/* Pre-2.0I texture rectangle: the RDP-half commands are 0xB3/0xB2 (see hd_code 17E10). */
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


/* (players[]: PlayerInfo, game/types.h) */

typedef struct {
    u8 pad0[4];
    char * N64P name; /* level name */
    u8 pad8[0x10];
    s8 commRoutes[0x18]; /* -1 terminated */
} FeLevelEntry;     /* 0x30 bytes, one per level */

/* (gLevelInfo[]: LevelInfo, game/types.h) */


/* Animated sprite definition, 0x30 bytes (26570.c's SpriteAnim, 1D990.c's
 * Anim30): only ten u16 sprite ids at 6; bytes from 0x1A are u8 frame data
 * and an f32 scale (the byte-order swap table merges these views). */
typedef struct {
    s16 x;
    s16 y;
    u8 unk4;
    u8 pad5;
    u16 unk6[10];
    u8 pad1A[0xE];
    f32 scale;
    u8 unk2C;
    u8 unk2D;
    u8 pad2E[2];
} IconInfo;

extern char gStatsLevelName[];
extern u16 gNewBestJText[];
extern u16 gBestToDateJText[];
extern u16 gBestStaysJText[];
extern u16 gGuestBestJText[];
#ifndef NON_MATCHING
extern s32 gHeapPtr; /* heap pointer */
#endif
extern u16 gBootSceneAngle; /* scene angle */
extern u8 * N64P gBootBannerTex; /* 256x32 IA8 banner */
extern u8 * N64P gBootIconTex; /* 40x24 IA8 icon */
extern u16 gBootScene; /* scene */
extern f32 gBootCamDist; /* camera distance */
extern f32 gBootSpinSpeed; /* spin speed */

/* ROM bounds of two compressed blobs (the first ends where the second starts) */
extern u8 romBootBannerEnd[];
extern u8 romBootIcon[];
extern u8 romBootIconEnd[];

int sprintf(char *, const char *, ...);
#ifdef NON_MATCHING
u8 fe_stats_grade_timed_level(u8 arg0, u8 arg1, u8 arg2);
#else
u8 fe_stats_grade_timed_level(); /* K&R */
#endif

#define DUMMY_LEVELS(l) ((l) == 49 || (l) == 47 || (l) == 38)
#define STATS_ASSERT(EX, line) \
    if (!(EX))                 \
    debug_printf("\n\a --- ASSERTION FAULT - %s - %s, line %d\n\n", #EX, "stats.c", line)

#define PRINT_SCORE(name, s)                                                                                       \
    debug_printf(name " ip=%8d : tc=%5d : bd=%2d : cr=%2d : rt=%3d : coin=%1d : bdn=%1d\n", (s).ip, (s).tc, (s).bd, \
                  (s).cr, (s).rt, (s).coin, (s).bdn)

/* Grades the level; records units, best time and medal (0x801EE800).
 * Finish a level: grade it, update units and best times. *arg0 = 1 if a rank was gained. */
u8 fe_stats_grade_level(u8 *arg0, u8 arg1, u8 arg2) {
    PlayerInfo *p;
    LevelInfo *l;
    u32 t;
    u8 ret;
    s32 pad[2];

    p = &players[playerNumber];
    l = &gLevelInfo[levelno];
    PRINT_SCORE("new", nss);
    PRINT_SCORE("old", gScoreOld);
    PRINT_SCORE("res", gScoreRes);
    PRINT_SCORE("rs2", gScoreRs2);
    debug_printf("units %d\n", p->units);
    if (gLevelInfo[levelno].kind == LEVEL_KIND_MISSION) {
        t = stats_print_status();
        if (arg2) {
            if (arg1) {
                if (t >= 100) {
                    nss.coin = 3;
                } else if (t >= 90) {
                    nss.coin = 2;
                } else if (t >= 70) {
                    nss.coin = 1;
                } else {
                    nss.coin = 5;
                }
                if (gLevelFirstClear) {
                    debug_printf("Units up 3\n");
                    p->units += 3;
                }
                nss.bdn = 1;
            } else {
                nss.coin = 0;
            }
        }
        ret = nss.coin;
    } else {
        ret = fe_stats_grade_timed_level(levelno, arg1, arg2);
    }
    sprintf(gStatsLevelName, "%s", (char *) gGlobeLevels[levelno].name); /* (char *): a N64P through varargs */
    *arg0 = 0;
    if (arg1 && arg2) {
        STATS_ASSERT(!DUMMY_LEVELS(levelno), 94);
        if (gLevelInfo[levelno].kind == LEVEL_KIND_MISSION) {
            p->money = ((s32) gLevelMoney);
        }
        if (p->units < 360) {
            debug_printf("UNITS UP %d\n", nss.coin % 5 - gScoreOld.coin % 5);
            p->units += nss.coin % 5 - gScoreOld.coin % 5;
        }
        if (p->units == 354) {
            p->units += 6;
        }
        if (p->units / 12 > p->title) {
            *arg0 = 1;
            p->title++;
        }
        if (!(gLevelInfo[levelno].kind & LEVEL_KIND_ANY_MISSION)) {
            p->bestVehicle[levelno] = nss.bdn;
        }
        gBestTimes[playerNumber][gVehicleTimeSlot[nss.bdn]] = nss.tc;
        if (gLevelFirstClear && gLevelInfo[levelno].kind == LEVEL_KIND_MISSION) {
            gBestTimes[playerNumber][gVehicleTimeSlot[0]] = nss.tc;
        }
        p->medal[levelno] = ret;
        fe_player_ticker_reset(playerNumber);
    }
    return ret;
}

/* results screen banner and its glyph list, by outcome */
char * N64P gBestBanners[] = { N64_DPTR("YOUR NEW BEST!"), N64_DPTR("BEST TO DATE"), N64_DPTR("YOUR BEST STAYS"),
                              N64_DPTR("GUEST BEST IS") };
u16 * N64P gBestBannerGlyphs[] = { N64_DPTR(gNewBestJText), N64_DPTR(gBestToDateJText), N64_DPTR(gBestStaysJText),
                             N64_DPTR(gGuestBestJText) };

/* Grade a timed level `arg0` and record the best time; returns the grade (0x801EEDB4).
 * K&R in the matching build (its callers pass unmasked ints). */
#ifdef NON_MATCHING
u8 fe_stats_grade_timed_level(u8 arg0, u8 arg1, u8 arg2)
#else
u8 fe_stats_grade_timed_level(arg0, arg1, arg2)
    u8 arg0;
    u8 arg1;
    u8 arg2;
#endif
{
    s32 x;
    s32 pad;
    s32 idx;
    PlayerInfo *p;
    LevelInfo *l;
    IconInfo *e;
    YoshiItem *m;
    char buf[32];
    u16 old;

    p = &players[playerNumber];
    l = &gLevelInfo[arg0];
    if (gNextGameMode == MODE_RESULTS && arg1 && l->kind == LEVEL_KIND_RACE) {
        ghost_keep_if_best(stats_tenths_to_frames(nss.tc));
    }
    if (arg2 && arg1) {
        if (gLevelInfo[arg0].kind == LEVEL_KIND_MISSION_TIMED) {
            nss.bdn = 0;
        } else if (nss.tc <= gScoreOld.tc) {
            nss.bdn = gDiggerVehicle;
        } else if (nss.tc != 0xFFFF &&
                   ((old = gBestTimes[playerNumber][gVehicleTimeSlot[gDiggerVehicle]]) == 0 || nss.tc < old)) {
            gBestTimes[playerNumber][gVehicleTimeSlot[gDiggerVehicle]] = nss.tc;
        }
    }
    if (nss.tc <= gScoreOld.tc && arg1) {
        nss.coin = fe_stats_grade_time(nss.tc, arg0, players[playerNumber].gameState);
    } else {
        nss.tc = gScoreOld.tc;
    }
    if (nss.tc < gScoreOld.tc && !gLevelFirstClear) {
        x = 0x484;
        if (arg2) {
            x = 0x584;
        }
    } else {
        x = 0x480;
    }
    hud_format_time(buf, nss.tc, 0);
    sprintf(&gStatusLines[0x80], "****%s*", buf);
    if (arg1) {
        m = &gYoshiFeItems[25];
        gYoshiFeItems[25].flags = x;
        debug_printf("getting icon %d\n", nss.bdn);
        m->sprite = nss.bdn + 0x22;
        e = &gYoshiSprites[m->sprite];
        m->spriteBase = spr_load(e->unk6, 0, e->unk4, e->unk2C, e->unk2D | 4, 1.0f);
        if (playerNumber != playerNumberAtStart) {
            idx = 3;
        } else if (gNextGameMode == MODE_LEVEL_STATUS || gLevelFirstClear) {
            idx = 1;
        } else if (nss.tc < gScoreOld.tc) {
            idx = 0;
        } else {
            idx = 2;
        }
        gYoshiFeItems[24].text = gBestBanners[idx];
        gYoshiFeItems[24].jtext = gBestBannerGlyphs[idx];
    }
    return nss.coin;
}

/* The level's unlock bits owned (max 2), -1 if none (0x801EF1E0).
 * Count the current level's entries (at most 2) whose bit is set for the player. */
s8 fe_stats_count_unlocks(void) {
    FeLevelEntry *e;
    s32 i;
    s32 count;

    e = &gGlobeLevels[levelno];
    if (e->commRoutes[0] == -1) {
        return -1;
    }
    for (i = 0, count = 0; e->commRoutes[i] != -1 && i < 2; i++) {
        if (players[playerNumber].commPts[levelno] & (1 << i)) {
            count++;
        }
    }
    return count;
}

/* A time's medal against level `arg1`'s thresholds: 4 platinum (needs game state arg2 >= 12), 3 gold,
 * 2 silver, 1 bronze, else 5 (0x801EF2BC). */
#ifdef NON_MATCHING
u8 fe_stats_grade_time(u16 arg0, u8 arg1, u8 arg2)
#else
u8 fe_stats_grade_time(arg0, arg1, arg2)
    u16 arg0;
    u8 arg1;
    u8 arg2;
#endif
{
    u8 ret;
    LevelInfo *l;

    l = &gLevelInfo[arg1];
    if (l->medalTimes[0] >= arg0 && arg2 >= 12) {
        ret = 4;
    } else if (l->medalTimes[1] >= arg0) {
        ret = 3;
    } else if (l->medalTimes[2] >= arg0) {
        ret = 2;
    } else if (l->medalTimes[3] >= arg0) {
        ret = 1;
    } else {
        ret = 5;
    }
    return ret;
}

/* The boot screens (modes 0x10, 0x20): scene and textures (0x801EF380).
 * Boot screen (modes 0x10 / 0x20) init: load scene `arg0` and inflate the two blobs onto the heap. */
void fe_boot_screen_init(s32 arg0) {
    s32 size1;
    s32 size2;

    size1 = romBootBannerEnd - romBootBanner;
    size2 = romBootIconEnd - romBootIcon;
    fe_scene_load(arg0);
    if (arg0 == 2) {
        gBootSceneAngle = 90;
    } else {
        gBootSceneAngle = 0;
    }
    hd_rom_load((u32) romBootBanner, (u32) ((void *) N64_IPTR(gHeapPtr)), (u32 *) &size1, 12, 0, 1);
    gBootBannerTex = (u8 *) N64_IPTR(gHeapPtr);
    gHeapPtr += size1;
    hd_rom_load((u32) romBootIcon, (u32) ((void *) N64_IPTR(gHeapPtr)), (u32 *) &size2, 12, 0, 1);
    gBootIconTex = (u8 *) N64_IPTR(gHeapPtr);
    gHeapPtr += size2;
    gBootScene = arg0;
    gBootCamDist = 0.0f;
    gBootSpinSpeed = 3.0f;
}

/* 8 unreferenced zero bytes sit between the last string and fe_boot_screen_frame's
 * double 7600.0 (0x8020EFA4..AB); probably compiled-out debug strings. */
const u32 D_8020EFA4[2] = { 0, 0 };

/* One boot-screen frame (0x801EF4AC).
 * Boot screen frame: builds the whole top-level display list (spinning
 * scene, banner and icon fading in, final fade out). */
void fe_boot_screen_frame(void) {
    FeDyn *dyn;
    Gfx *gdl;
    s32 i;
    s32 xo;
    s32 yo;
    s16 c;

    dyn = &gDynamicBufs[gBufferIndex ^ 1];
    gdl = dyn->dl;
    cont_update();
    rsp_send_task((u64 *) (gDynamicBufs[gBufferIndex].dl), gFrameDlLen, 1, 1, 0x4D2, 0);
    gBufferIndex ^= 1;
    gSPSegment(gdl++, 0, 0);
    gSPSegment(gdl++, 2, osVirtualToPhysical(dyn));
    gSPSegment(gdl++, 1, osVirtualToPhysical(gStaticSegment));
    gSPDisplayList(gdl++, seg1RdpInitDl);
    gSPDisplayList(gdl++, seg1RspInitDl);
    gDPSetCycleType(gdl++, G_CYC_FILL);
    gDPSetDepthImage(gdl++, gDepthBufferPhys);
    gDPSetColorImage(gdl++, G_IM_FMT_RGBA, G_IM_SIZ_16b, 320, gDepthBufferPhys);
    gDPSetFillColor(gdl++, 0xFFFCFFFC);
    gDPFillRectangle(gdl++, 0, 0, 319, 239);
    gDPSetColorImage(gdl++, G_IM_FMT_RGBA, G_IM_SIZ_16b, 320, gFrameBufferPhys[gBufferIndex]);
    gDPPipeSync(gdl++);
    if (gBootScene == 1) {
        if (gFramesInMode * 185 / 20 > 185) {
            c = 185;
        } else {
            c = gFramesInMode * 185 / 20;
        }
    } else {
        c = 0;
    }
    gDPSetFillColor(gdl++, (GPACK_RGBA5551(c, c, c, 1) << 16) | GPACK_RGBA5551(c, c, c, 1));
    gDPFillRectangle(gdl++, 0, 0, 319, 239);
    gDPSetCycleType(gdl++, G_CYC_1CYCLE);
    cont_start_read();
    if (gFramesInMode == 250) {
        if (gGameMode == MODE_BOOT_SCREEN1) {
            gNextGameMode = MODE_BOOT_SCREEN2;
        } else {
            gNextGameMode = MODE_ATTRACT_START;
            if (gDebugCheats) {
                snd_play(gSfxBank, SFX_UNK_0x68_CHIME, NULL);
            }
        }
    }
    if (gFramesInMode < 2) {
        guPerspective(&dyn->persp, &gPerspNorm, 45.0f, 1.3333334f, 40.0f, 8000.0f, 0.25f);
        if (gBootScene == 1) {
            guTranslate(&dyn->sceneTranslate, 0.0f, -130.0f, 0.0f);
            guRotate(&dyn->sceneRotate, 35.0f, 0.1f, 0.0f, 0.0f);
        } else {
            guTranslate(&dyn->sceneTranslate, 0.0f, 0.0f, 0.0f);
            guRotate(&dyn->sceneRotate, -10.0f, 0.1f, 0.0f, 0.0f);
        }
    }
    if (gFramesInMode >= 20) {
        if (gFramesInMode == 20 && gBootScene == 1) {
            snd_play(gSfxBank, SFX_BOOT1_IN, NULL);
        } else if (gFramesInMode == 20 && gBootScene == 2) {
            snd_play(gSfxBank, SFX_BOOT2_IN, NULL);
        }
        if (gFramesInMode < 80) {
            gBootCamDist = (80 - gFramesInMode) * 7600.0 / 60.0 + 400.0;
        }
        if (gFramesInMode == 75 && gBootScene == 2) {
            snd_play(gSfxBank, SFX_BOOT2_LAND, NULL);
        } else if (gFramesInMode == 75 && gBootScene == 1) {
            snd_play(gSfxBank, SFX_BOOT1_LAND, NULL);
        }
        guLookAtReflect(&dyn->view, &dyn->lookAt, 1.0f, 0.0f, gBootCamDist, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f);
        gBootSceneAngle += gBootSpinSpeed;
        guRotate(&gFeSceneRot[gBufferIndex], gBootSceneAngle % 360, 0.0f, 1.0f, 0.0f);
        guScale(&dyn->sceneScale, 1.5f, 1.5f, 1.5f);
        gDPSetRenderMode(gdl++, G_RM_AA_ZB_OPA_INTER, G_RM_NOOP2);
        gdl = fe_scene_draw(dyn, gdl);
    }
    if (gFramesInMode > 80 && gBootScene == 1) {
        gDPPipeSync(gdl++);
        gSPTexture(gdl++, 0, 0, 0, 0, G_OFF);
        gDPSetTexturePersp(gdl++, G_TP_NONE);
        gDPSetRenderMode(gdl++, G_RM_XLU_SURF, G_RM_XLU_SURF2);
        gDPSetCombine(gdl++, 0x119623, 0xFF2FFFFF);
        gDPSetPrimColor(gdl++, 0, 0, 0x28, 0, 0xFF, (gFramesInMode * 6 - 480 < 255) ? gFramesInMode * 6 - 480 : 255);
        /* the banner, in 32-texel-wide strips */
        xo = 26, yo = 42;
        for (i = 0; i < 256; i += 32) {
            gDPLoadTextureTile(gdl++, gBootBannerTex, G_IM_FMT_IA, G_IM_SIZ_8b, 256, 32, i, 0, i + 31, 31, 0, G_TX_CLAMP,
                               G_TX_CLAMP, G_TX_NOMASK, G_TX_NOMASK, G_TX_NOLOD, G_TX_NOLOD);
            gSPTextureRectangleOld(gdl++, (i + xo) << 2, yo << 2, (i + xo + 32) << 2, (yo + 32) << 2,
                                   G_TX_RENDERTILE, i << 5, 0, 1 << 10, 1 << 10);
        }
        gDPPipeSync(gdl++);
        gDPSetPrimColor(gdl++, 0, 0, 0xFF, 0, 0x28, (gFramesInMode * 4 - 320 < 255) ? gFramesInMode * 4 - 320 : 255);
        gDPLoadTextureBlock(gdl++, gBootIconTex, G_IM_FMT_IA, G_IM_SIZ_8b, 40, 24, 0, G_TX_CLAMP, G_TX_CLAMP,
                            G_TX_NOMASK, G_TX_NOMASK, G_TX_NOLOD, G_TX_NOLOD);
        gSPTextureRectangleOld(gdl++, 256 << 2, 20 << 2, (256 + 40) << 2, (20 + 24) << 2, G_TX_RENDERTILE, 0, 0,
                               1 << 10, 1 << 10);
        gDPSetTexturePersp(gdl++, G_TP_PERSP);
    }
    if (gFramesInMode >= 221) {
        gDPPipeSync(gdl++);
        gDPSetCycleType(gdl++, G_CYC_1CYCLE);
        gDPSetRenderMode(gdl++, G_RM_XLU_SURF, G_RM_XLU_SURF2);
        gDPSetCombineMode(gdl++, G_CC_PRIMITIVE, G_CC_PRIMITIVE);
        gDPSetPrimColor(gdl++, 0, 0, 0, 0, 0, (gFramesInMode - 220) * 255 / 30);
        gDPFillRectangle(gdl++, 0, 0, 319, 239);
    }
    gDPFullSync(gdl++);
    gSPEndDisplayList(gdl++);
    gFrameDlLen = gdl - dyn->dl;
}
