#include "common.h"
#include <ultra64.h>
#ifndef NON_MATCHING /* legacy declarations */
/* Matching build: IDO compiled the matched code here against older
 * declarations of these, which the file keeps; the NON_MATCHING build
 * uses game/game.h's. */
#define LEGACY_gYoshiHeapItems
#define LEGACY_gRdus
#define LEGACY_yoshi_draw
#endif /* legacy declarations */
#include "game/game.h"
/* Views: this file reads these shared variables (game/variables.h) as other types. */
#define gYoshiFeItems ((YoshiItem *) gYoshiFeItems)
#define gLevelInfo ((LevelInfo *) gLevelInfo)
#define gYoshiSprites ((SpriteAnim *) gYoshiSprites)
#define gYoshiGameItems ((YoshiItem *) gYoshiGameItems)
#define gYoshiWindows ((YoshiWindow *) gYoshiWindows)
#define players ((PlayerHintView *) players)
#ifdef NON_MATCHING
#define gYoshiHeapItems (*(YoshiItem * N64P *) &gYoshiHeapItems)
#define gRdus (*(RduRecord * N64P *) &gRdus)
#endif
/* end of views */

#define YOSHI_ASSERT(EX, line) \
    if (!(EX)) debug_printf("\n\a --- ASSERTION FAULT - %s - %s, line %d\n\n", #EX, "yoshi.c", line)

/* One entry of the path table, 0x14 bytes */
typedef struct {
    /* 0x00 */ u8 id;
    /* 0x01 */ char name[15];
    /* 0x10 */ s32 unk10;
} TargetBuilding;

/* (A window item: YoshiItem, game/types.h.) */

/* Animated sprite, 0x30 bytes */
typedef struct {
    /* 0x00 */ s16 x;
    /* 0x02 */ s16 y;
    /* 0x04 */ u8 count;   /* sprites (spr_load's count) */
    /* 0x05 */ u8 unk5;
    /* 0x06 */ u16 ids[10]; /* sprite ids */
    /* 0x1A */ u8 numFrames;
    /* 0x1B */ u8 frames[10];
    /* 0x25 */ u8 mode;
    /* 0x26 */ u8 speed;
    /* 0x27 */ u8 unk27;
    /* 0x28 */ f32 scale;
    /* 0x2C */ u8 sprFrames; /* spr_load's frames */
    /* 0x2D */ u8 sprFlags;  /* spr_load's flags (| 4 unless the window has flag 0x20000) */
    /* 0x2E */ s8 preloaded; /* -1 (yoshi_init): the window loads it (yoshi_load_window_sprites), else its spriteBase */
    /* 0x2F */ u8 unk2F;
} SpriteAnim;

/* This file's view of a PlayerInfo (game/types.h): yoshi_show_hint indexes
 * the hint counts by window id from 0x88 (ids 0x46..0x57 = PlayerInfo.hintShows). */
typedef struct {
    /* 0x00 */ u8 pad0[0x18];
    /* 0x18 */ u8 medal[0x70]; /* per level */
    /* 0x88 */ u8 hintShowsById[9]; /* [window id]: times shown */
    /* 0x91 */ u8 gameState;
    /* 0x92 */ u8 bestVehicle[0x6E];
} PlayerHintView;

/* 0x22 bytes */
typedef struct {
    /* 0x00 */ u8 id;
    /* 0x02 */ s16 ids[16];
} YoshiHintList;

/* Yoshi path node, 0x88 bytes */
typedef struct {
    /* 0x00 */ s16 x;
    /* 0x02 */ s16 y;
    /* 0x04 */ s16 z;
    /* 0x06 */ u8 visited;
    /* 0x07 */ u8 cell;
    /* 0x08 */ Vtx vtx[2][4]; /* double-buffered quad */
} RduRecord;

/* (gLevelInfo[]: LevelInfo, game/types.h) */

#define ABS(x) ((x) > 0 ? (x) : -(x))
#define MIN255(x) ((x) >= 256 ? 255 : (x))
#define MAX0(x) ((x) < 0 ? 0 : (x))

#define VIDI_ASSERT(EX, line) \
    if (!(EX)) debug_printf("\n\a --- ASSERTION FAULT - %s - %s, line %d\n\n", #EX, "./vidiPrint.h", line)

#define YOSHI_OFF 1

/* (A window: YoshiWindow, game/types.h.) */

/* Frame buffers passed in as arg1 */
#define YMTX(off) ((Mtx *) N64_IPTR(arg1 + (off)))

u16 yoshi_next_item(u16 arg0, u16 arg1, u16 start, u16 mask);
void yoshi_select_items(struct YoshiWindow *arg0);
#if defined(NON_MATCHING) && defined(PORT_HOST)
/* port: the matched callers below pass only the node, &gRdus[i] (an N64P,
 * 4 bytes), to this unprototyped declaration, which the definition reads as a
 * native RduRecord * (8) and a dead arg1 (overwritten before use): the true
 * prototype, and the calls pass a native pointer and 0 (#undef before the
 * definition) */
s32 rdu_is_visible(RduRecord *node, s32 arg1);
#define rdu_is_visible(node) rdu_is_visible((RduRecord *) (node), 0)
#else
s32 rdu_is_visible();
#endif
void yoshi_load_window_sprites(struct YoshiWindow *arg0);
Gfx *yoshi_run_window(Gfx *gfx, s32 arg1, s32 *count);
void yoshi_start_typing(YoshiWindow *arg0);
void *yoshi_get_item_text(YoshiWindow *arg0, u16 idx, u8 japanese);
u8 yoshi_scroll_fade(u16 *arg0, u16 *arg1, s16 arg2);
u16 yoshi_prev_item(u16 lo, u16 hi, u16 mask);

extern u16 yoshiDemandV;
extern s16 gYoshiPulse;
extern s8 gYoshiPulseDir;
extern u32 gYoshiLastTime;
extern f32 gYoshiOpenTime;
extern f32 gYoshiScrollGoal;
extern f32 gYoshiScroll;
extern s32 gYoshiScrollY;
extern f32 gYoshiOpen;
extern u16 gYoshiPrevButton;
extern u16 gYoshiNextButton;
extern s32 gYoshiArrowBob;
extern s32 gYoshiArrowDir;
extern u8 gYoshiSpriteTick[];
extern Gfx gYoshiWindowDl[];
extern u8 D_8036BAE8[];
extern u8 gLevelKindSprites[];
extern u16 gYoshiTypeBuf[];
extern u16 gYoshiTypeBufNext[];
/* Per-language special characters: [0] for English, [1] for Japanese */
extern u16 gYoshiTypeCursor[];
extern u32 gYoshiTypeTime;
extern u32 gYoshiStateTime;
/* Debug switches set from the command line */
extern s32 gDebugArgV; /* -v */
extern s32 gDebugArgS; /* -s */
extern s32 gDebugArgJ; /* -j */
extern s32 gDebugArgM; /* -m */
extern u16 gRduLitOrderNext[];
extern s32 gRduLitOrderLen;
#ifndef NON_MATCHING
extern RduRecord *gRdus;
#endif
extern f32 gRduLastHeading;
extern u8 gRduVtxSet;
extern Vtx gRduQuad[]; /* quad template */
extern u8 gRduTexUnlit[]; /* 16x16 RGBA32 textures */
extern u8 gRduTexLit[];
extern s32 gRduPickupRadius[];
extern YoshiHintList gYoshiHints[];
extern u8 gYoshiHintMaxShows[];
extern u8 gYoshiHintShown[];
extern u16 gYoshiTypeItem;
extern u16 gYoshiTypeChar;
extern YoshiItem * N64P gYoshiItems;
#ifndef NON_MATCHING
extern YoshiItem *gYoshiHeapItems;
#endif
extern u8 gYoshiSpriteFrame[];
extern TargetBuilding gTargetBuildings[];

/* A level's hint window, once per level and up to its show limit (0x8026AD30). */
u8 yoshi_show_hint(s16 arg0) {
    YoshiHintList *trig;
    u8 found = 0;
    s32 i;
    s32 j;
    s32 c1;
    s32 c2;

    if (!(gGameMode & (MODE_LEVEL | MODE_MAP_VIEW | MODE_LEVEL_START))) {
        return 0;
    }
    if (players[playerNumber].gameState >= 11) {
        return 0;
    }
    for (i = 0; i < 8 && !found; i++) {
        trig = &gYoshiHints[i];
        if (trig->id == levelno) {
            for (j = 0; j < 16 && !found && trig->ids[j] != -1; j++) {
                if (trig->ids[j] == arg0) {
                    c1 = players[playerNumber].hintShowsById[arg0] < gYoshiHintMaxShows[arg0];
                    c2 = levelno == 0;
                    if (!gYoshiHintShown[arg0] && (c2 || c1)) {
                        if (c1 && !c2) {
                            players[playerNumber].hintShowsById[arg0]++;
                        }
                        gYoshiHintShown[arg0] = 1;
                        yoshi_demand(arg0 | YOSHI_DEMAND_OPEN | YOSHI_DEMAND_FORCE);
                        found = 1;
                    }
                }
            }
        }
    }
    return found;
}

/* Requests a window open/close (yoshiDemandV) (0x8026AF6C). */
void yoshi_demand(u16 yd) {
    u16 oldWindow = yoshiDemandV & 0xFF;
    u16 newWindow = yd & 0xFF;

    if (yoshiDemandV) {
        YOSHI_ASSERT(!yoshiDemandV, 1312);
        debug_printf("NEW: %x OLD:%x\n", yd, yoshiDemandV);
    }
    if (yd & YOSHI_DEMAND_OFF) {
        YOSHI_ASSERT(yd==YOSHI_DEMAND_OFF, 1317);
    }
    if (newWindow == YW_MOON_DONE || newWindow == YW_SHUTTLE_DONE || newWindow == YW_LEVEL_DONE || newWindow == YW_MISSION_FAILED) {
        gYoshiPendingWindow = NO_YOSHI_WINDOW;
    }
    if (oldWindow == YW_MOON_DONE || oldWindow == YW_SHUTTLE_DONE || oldWindow == YW_LEVEL_DONE || oldWindow == YW_MISSION_FAILED) {
        YOSHI_ASSERT(1==0, 1324);
        debug_printf("OH MY GOD!\n");
        return;
    }
    if (yoshiDemandV) {
        YOSHI_ASSERT(!yoshiDemandV, 1331);
        debug_printf("GOING FOR NEW: %x OLD:%x\n", yd, yoshiDemandV);
    }
    yoshiDemandV = yd;
}

/* The pending window demand, yoshiDemandV (0x8026B10C). */
u16 yoshi_get_demand(void) {
    return yoshiDemandV;
}

/* Resets the window state; preloads this mode's window sprites (0x8026B118).
 * Resets the Yoshi state and sets up the level's path nodes */
void yoshi_init(u8 arg0) {
    YoshiWindow *arg;
    YoshiHintList *trig;
    u8 found;
    s32 i;
    s32 j;

    yoshiState = YOSHI_OFF;
    yoshiSelection = 0;
    yoshiDemandV = 0;
    gYoshiPendingWindow = NO_YOSHI_WINDOW;
    currentYoshiWindow = NO_YOSHI_WINDOW;
    gYoshiPulse = 0;
    arg = NULL;
    gYoshiPulseDir = 1;
    if (!arg0) {
        for (i = 0; i < 108; i++) {
            arg = &gYoshiWindows[i];
            if (arg->flags & 0x100) {
                arg->flags |= 0x80;
            }
        }
    }
    gYoshiWindows[YW_STATUS].timeout = 3;
    for (i = 0; i < 18; i++) {
        D_8036BAE8[i] = 0;
    }
    for (i = 0; i < 75; i++) {
        gYoshiSprites[i].preloaded = -1;
    }
    switch (gNextGameMode) {
        case MODE_LEVEL_STATUS:
        case MODE_RESULTS:
            gYoshiFeItems[25].sprite = 0;
            /* fallthrough */
        case MODE_PROMOTION:
            arg = &gYoshiWindows[gYoshiResultsWindowByKind[lowest_set_bit(gLevelInfo[levelno].kind)]];
            if (gUseEeprom) {
                gYoshiFeItems[29].flags &= ~1;
                gYoshiFeItems[29].sprite = 11;
            }
            if (gLevelInfo[levelno].kind == LEVEL_KIND_MISSION) {
                if (academy_marker_found(levelno)) {
                    gYoshiFeItems[18].sprite = 24;
                } else {
                    gYoshiFeItems[18].sprite = 0;
                }
            } else if (gLevelInfo[levelno].kind == LEVEL_KIND_BUILDINGS) {
                gYoshiFeItems[23].flags &= ~0x400;
                gYoshiFeItems[26].flags &= ~0x400;
                gYoshiFeItems[27].sprite = 0;
                gYoshiFeItems[28].sprite = 0;
            } else {
                gYoshiFeItems[23].sprite = gLevelKindSprites[lowest_set_bit(gLevelInfo[levelno].kind)];
                gYoshiFeItems[26].sprite = gLevelKindSprites[lowest_set_bit(gLevelInfo[levelno].kind)];
                gYoshiFeItems[27].sprite = 0;
                gYoshiFeItems[28].sprite = 0;
            }
            break;
        case MODE_BEST_TIMES:
            arg = &gYoshiWindows[YW_BEST_TIMES];
            break;
        case MODE_REGRADE_NOTICE:
            arg = &gYoshiWindows[YW_REGRADE_NOTICE];
            break;
        case MODE_REPLAY:
            arg = &gYoshiWindows[gYoshiReplayWindowByKind[lowest_set_bit(gLevelInfo[levelno].kind)]];
            if (gLevelInfo[levelno].kind == LEVEL_KIND_BUILDINGS) {
                gYoshiGameItems[27].flags &= ~0x400;
                gYoshiGameItems[28].flags &= ~0x400;
                gYoshiGameItems[29].sprite = 0;
                gYoshiGameItems[30].sprite = 0;
            } else {
                gYoshiGameItems[27].sprite = gLevelKindSprites[lowest_set_bit(gLevelInfo[levelno].kind)];
                gYoshiGameItems[28].sprite = gLevelKindSprites[lowest_set_bit(gLevelInfo[levelno].kind)];
                gYoshiGameItems[29].sprite = 0;
                gYoshiGameItems[30].sprite = 0;
            }
            break;
        case MODE_LEVEL_START:
            if (((players[playerNumber].medal[levelno] > 0 && players[playerNumber].medal[levelno] < 6) ? 1 : 0) &&
                gLevelInfo[levelno].kind == LEVEL_KIND_MISSION) {
                arg = &gYoshiWindows[YW_STATUS];
            } else {
                arg = NULL;
            }
            break;
        case MODE_GLOBE_SEQ:
            switch (levelno) {
                case 55:
                    arg = &gYoshiWindows[YW_DEMO_BACKLASH];
                    break;
                case 28:
                    arg = &gYoshiWindows[YW_DEMO_SIDESWIPE];
                    break;
                case 53:
                    arg = &gYoshiWindows[YW_DEMO_THUNDERFIST];
                    break;
                case 7:
                    arg = &gYoshiWindows[YW_DEMO_SKYFALL];
                    break;
                case 19:
                    arg = &gYoshiWindows[YW_DEMO_JBOMB];
                    break;
                default:
                    arg = NULL;
                    break;
            }
            break;
        default:
            arg = NULL;
            break;
    }
    if (arg) {
        yoshi_load_window_sprites(arg);
    }
    if (gNextGameMode & (MODE_LEVEL | MODE_LEVEL_START)) {
        for (i = 0, found = 0; i < 8 && !found; i++) {
            trig = &gYoshiHints[i];
            if (trig->id == levelno) {
                for (j = 0; j < 16 && trig->ids[j] != -1; j++) {
                    yoshi_load_window_sprites(&gYoshiWindows[trig->ids[j]]);
                }
            }
        }
    }
}

/* Destroy-building levels: the target's icon in the briefing (0x8026B8F8). */
void yoshi_set_target_icons(void) {
    if (gLevelKind & LEVEL_KIND_BUILDINGS) {
        gYoshiFeItems[23].flags |= 0x400;
        gYoshiFeItems[26].flags |= 0x400;
        gYoshiGameItems[27].flags |= 0x400;
        gYoshiGameItems[28].flags |= 0x400;
        gYoshiFeItems[23].sprite = gYoshiFeItems[26].sprite = gYoshiGameItems[27].sprite = gYoshiGameItems[28].sprite =
            yoshi_get_target_building(NULL, NULL);
        if (gNextGameMode == MODE_REPLAY) {
            yoshi_load_window_sprites(&gYoshiWindows[gYoshiReplayWindowByKind[lowest_set_bit(gLevelKind)]]);
        } else {
            yoshi_load_window_sprites(&gYoshiWindows[gYoshiResultsWindowByKind[lowest_set_bit(gLevelKind)]]);
        }
    }
}

/* Loads the animated sprites a window's items use (0x8026BA7C). */
void yoshi_load_window_sprites(YoshiWindow *arg0) {
    SpriteAnim *snd;
    s32 i;
    u8 v;
    YoshiItem *node;

    v = 4;
    yoshi_select_items(arg0);
    if (arg0->flags & 0x20000) {
        v = 0;
    }
    for (i = arg0->firstItem; i < arg0->firstItem + arg0->itemCount; i++) {
        node = &gYoshiItems[i];
        if (node->flags & 0x400) {
            snd = &gYoshiSprites[node->sprite];
            if (snd->preloaded == -1) {
                node->spriteBase = spr_load((u16 *) snd->ids, 0, snd->count, snd->sprFrames, snd->sprFlags | v, 1.0f);
                gYoshiSpriteFrame[node->sprite] = 0;
            } else {
                node->spriteBase = snd->preloaded;
            }
        }
    }
}

#ifdef NON_MATCHING
/* Runs and draws the current window (0x8026BBD0). */
Gfx *yoshi_draw(Gfx *gfx, s32 arg1, s32 *count) {
#else
void yoshi_draw(Gfx *gfx, s32 arg1, s32 *count) {
#endif
    Gfx *gdl = gfx;

    YOSHI_ASSERT(!(yoshiState==YOSHI_OFF && currentYoshiWindow!=NO_YOSHI_WINDOW), 1567);
    gdl = yoshi_run_window(gdl, arg1, count);
    YOSHI_ASSERT(!(yoshiState==YOSHI_OFF && currentYoshiWindow!=NO_YOSHI_WINDOW), 1571);
    gDPPipeSync(gdl++);
    *count += gdl - gfx;
#ifdef NON_MATCHING
    /* The original returns nothing (a void function in the matching build)
     * but leaves yoshi_run_window's result in v0, and its callers (hd.c, the
     * front end) take that as the new display-list end: the next command
     * they write replaces the pipesync above. */
    return gdl - 1;
#endif
}

#define PRESSED(m) ((gContButtons & (m)) && !(gContPrevButtons & (m)))
#define FRAMES(t) ((t) * 60 / 60)

/* Demands, input, open/close, arrows, sprites and text of the window (0x8026BCE0).
 * Runs and draws the current Yoshi window: handles open/close demands, menu
 * input, the open/close animation, the scroll arrows, sprites and text */
Gfx *yoshi_run_window(Gfx *gfx, s32 arg1, s32 *count) {
    YoshiWindow *arg;
    YoshiItem *node;
    u8 *text;
    u16 *wtext;
    Gfx *gdl;
    u16 sound;
    u16 i;
    u16 halfW;
    u16 halfH;
    u16 dt;
    u16 redraw;
    u8 *c;
    f32 one;
    u16 window;
    u16 force;
    u32 keep;
    u8 done;

    gdl = gfx;
    dt = (PORT_GVI("yoshi_run_window", gScheduler.frameCount) - gYoshiLastTime) * 15;
    one = 1.0f;
    gYoshiLastTime = PORT_GVI_LAST(gScheduler.frameCount);
    gYoshiPulse += gYoshiPulseDir * dt;
    redraw = 0;
    if (gYoshiPulse >= 256) {
        gYoshiPulse = 255;
        gYoshiPulseDir = -gYoshiPulseDir;
    }
    if (gYoshiPulse < 0) {
        gYoshiPulse = 0;
        gYoshiPulseDir = -gYoshiPulseDir;
    }
    c = gYoshiColors[16];
    c[1] = 255 - gYoshiPulse;
    c[5] = gYoshiPulse;
    c = gYoshiColors[17];
    c[1] = 170 - gYoshiPulse * 2 / 3;
    c[5] = gYoshiPulse * 2 / 3;
    c = gYoshiColors[18];
    c[0] = c[1] = 255 - gYoshiPulse;
    c[4] = c[5] = gYoshiPulse;
    c = gYoshiColors[19];
    c[2] = c[1] = 255 - gYoshiPulse;
    c[6] = c[5] = gYoshiPulse;
    c = gYoshiColors[20];
    c[2] = 255 - gYoshiPulse;
    c[6] = gYoshiPulse;
    if (gGameMode == MODE_CARRIER_END && gHaveCarrier && gCarrierHit) {
        gYoshiPendingWindow = NO_YOSHI_WINDOW;
        if (yoshiState == 4 || yoshiState == 2) {
            debug_printf("putting off!\n");
            yoshi_demand(YOSHI_DEMAND_OFF);
        }
    }
    if (currentYoshiWindow == NO_YOSHI_WINDOW && (yoshiDemandV & YOSHI_DEMAND_OFF)) {
        YOSHI_ASSERT(1==0, 1627);
        yoshiDemandV = 0;
        return gfx;
    }
    if (yoshiDemandV) {
        window = yoshiDemandV & 0xFF;
        force = yoshiDemandV & YOSHI_DEMAND_FORCE;
        keep = 0;
        debug_printf("yoshiDemand=%x\n", yoshiDemandV);
        if (yoshiDemandV & YOSHI_DEMAND_OPEN) {
            gYoshiPendingWindow = NO_YOSHI_WINDOW;
            if (currentYoshiWindow != NO_YOSHI_WINDOW) {
                keep = gYoshiWindows[currentYoshiWindow].flags & 0x8000000;
            }
            if (yoshiState == YOSHI_OFF || force || keep) {
                currentYoshiWindow = window;
                yoshiState = YOSHI_OFF;
            } else {
                gYoshiPendingWindow = window;
            }
        }
        if (yoshiState != 8) {
            redraw = 1;
        }
        yoshiDemandV = 0;
    }
    if (currentYoshiWindow == NO_YOSHI_WINDOW) {
        currentYoshiWindow = gYoshiPendingWindow;
        gYoshiPendingWindow = NO_YOSHI_WINDOW;
        if (currentYoshiWindow == NO_YOSHI_WINDOW) {
            return gfx;
        }
        redraw = 1;
    }
    arg = &gYoshiWindows[currentYoshiWindow];
    yoshi_select_items(arg);
    if ((arg->flags & 0x20) && yoshiState == 2) {
        if ((PRESSED(0x8000) || ((arg->flags & 0x80000000) && PRESSED(0x1000))) && arg->atEnd) {
            sound = gYoshiItems[arg->sel].sound;
            if (sound) {
                snd_play(gSfxBank, sound, NULL);
            }
            if (gYoshiItems[arg->sel].flags & 0x10) {
                gUnpauseRequest = 1;
            }
            yoshiSelection = arg->sel;
            redraw = 1;
        }
        if (PRESSED(0x4000)) {
            if ((arg->flags & 0x20000000) && arg->atEnd) {
                snd_play(gSfxBank, SFX_BACK, NULL);
                yoshiSelection = 0xFFFF;
                redraw = 1;
                if (arg->flags & 0x40000000) {
                    gUnpauseRequest = 1;
                }
            } else {
                snd_play(gSfxBank, SFX_DENIED, NULL);
            }
        }
    }
    if (redraw) {
        gYoshiStateTime = PORT_GVI("yoshi_run_window", gScheduler.frameCount);
        switch (yoshiState) {
            case 8:
                gYoshiStateTime = PORT_GVI("yoshi_run_window", gScheduler.frameCount) - gYoshiOpenTime * gYoshiOpen * gYoshiScale;
            case 1:
                yoshiState = 4;
                gYoshiScale = 1.0f;
                if (arg->flags & 0x18) {
                    gYoshiOpenTime = 40.0f;
                } else {
                    gYoshiOpenTime = 13.333333f;
                }
                yoshi_start_typing(arg);
                sound = arg->openSound;
                if (sound) {
                    snd_play(gSfxBank, sound, NULL);
                }
                if (!(arg->flags & 0x400)) {
                    for (i = arg->firstItem; !(gYoshiItems[i].flags & 1) && i < arg->firstItem + arg->itemCount; i++) {
                    }
                    arg->sel = i;
                }
                for (i = arg->firstItem; i < arg->firstItem + arg->itemCount; i++) {
                    node = &gYoshiItems[i];
                    if (node->flags & 0x20) {
                        if (arg->flags & 0x80000) {
                            node->x = font_center_x(arg->w / 2, node->w, node->text, (s32) node->jtext);
                        } else {
                            node->x = font_center_x(arg->w / 2, node->w, node->text, (s32) node->jtext);
                        }
                    }
                }
                if (arg->flags & 1) {
                    gPauseRequest = 1;
                }
                if (arg->flags & 2) {
                    music_fade_to(0.0f);
                }
                if (arg->flags & 0x100000) {
                    gYoshiScrollGoal = arg->h;
                } else {
                    gYoshiScrollGoal = arg->h / 2 - gYoshiItems[arg->sel].y;
                    if (arg->flags & 0x40000) {
                        gYoshiScrollGoal -= gYoshiItems[arg->sel].h / 2;
                    }
                }
                gYoshiScroll = gYoshiScrollGoal;
                break;
            case 4:
                gYoshiStateTime = (gYoshiOpen - one) * gYoshiOpenTime + PORT_GVI("yoshi_run_window", gScheduler.frameCount);
            case 2:
                yoshiState = 8;
                sound = arg->closeSound;
                if (sound) {
                    snd_play(gSfxBank, sound, NULL);
                }
                if (arg->flags & 0x200000) {
                    gUnpauseRequest = 1;
                }
                if (arg->flags & 4) {
                    music_fade_to(1.0f);
                }
                break;
        }
    }
    switch (yoshiState) {
        case 2:
            if (arg->flags & 0x100000) {
                done = gYoshiScroll < (f32) (arg->h / 8 - gYoshiItems[arg->firstItem + arg->itemCount - 1].y);
            } else {
                done = arg->timeout != 0 && (PORT_GVI("yoshi_run_window", gScheduler.frameCount) - gYoshiStateTime) / 60.0f > arg->timeout &&
                       (!(arg->flags & 0x400000) || !(gYoshiTypeState != 0));
            }
            if (done) {
                sound = arg->closeSound;
                if (sound) {
                    snd_play(gSfxBank, sound, NULL);
                }
                if (arg->flags & 0x2000) {
                    music_fade_to(0.0f);
                }
                yoshiState = 8;
                gYoshiStateTime = PORT_GVI("yoshi_run_window", gScheduler.frameCount);
            }
            break;
        case 4:
            gYoshiOpen = (PORT_GVI("yoshi_run_window", gScheduler.frameCount) - gYoshiStateTime) / gYoshiOpenTime;
            if (gYoshiOpen > one) {
                gYoshiStateTime = PORT_GVI_LAST(gScheduler.frameCount);
                yoshiState = 2;
                gYoshiOpen = one;
                if (arg->flags & 0x40) {
                    gYoshiPrevButton = 0x200;
                    gYoshiNextButton = 0x100;
                } else {
                    gYoshiPrevButton = 0x800;
                    gYoshiNextButton = 0x400;
                }
                if (arg->flags & 0x10000000) {
                    arg->atEnd = 1;
                } else {
                    arg->atEnd = 0;
                }
                if (yoshi_next_item(arg->firstItem, arg->itemCount, arg->sel, 1) == arg->sel) {
                    arg->atEnd = 1;
                }
            }
            break;
        case 8:
            gYoshiOpen = one - (PORT_GVI("yoshi_run_window", gScheduler.frameCount) - gYoshiStateTime) / gYoshiOpenTime;
            if (gYoshiOpen < 0.001) {
                gYoshiOpen = 0.0f;
                yoshiState = 1;
                if (arg->flags & 0x2000000) {
                    gUnpauseRequest = 1;
                }
                if (arg->flags & 0x100) {
                    arg->flags &= ~0x80;
                }
                currentYoshiWindow = NO_YOSHI_WINDOW;
                return gfx;
            }
            break;
    }
    if (yoshiState != 1) {
        gYoshiAlpha = (hd_sinf(gYoshiOpen * gYoshiScale / one * 1.57 + 4.71) + 1.0) * 255.0;
    }
    if (yoshiState != 1 && gYoshiOpen * gYoshiScale > 0.1) {
        halfW = arg->w / 2;
        halfH = arg->h / 2;
        guOrtho(YMTX(0x1240), -arg->x - halfW, -arg->x - halfW + 319, -arg->y - halfH + 239, -arg->y - halfH,
                -256.0f, 256.0f, 256.0f);
        gSPMatrix(gdl++, OS_K0_TO_PHYSICAL(YMTX(0x1240)), G_MTX_PROJECTION | G_MTX_LOAD | G_MTX_NOPUSH);
        if (arg->flags & 0x10) {
            guRotate(YMTX(0x12C0), 180.0 - gYoshiOpen * gYoshiScale / one * 180.0, 2.0f, 0.0f, 1.0f);
            gSPMatrix(gdl++, OS_K0_TO_PHYSICAL(YMTX(0x12C0)), G_MTX_MODELVIEW | G_MTX_LOAD | G_MTX_NOPUSH);
        } else {
            guTranslate(YMTX(0x12C0), 0.0f, 0.0f, 0.0f);
            gSPMatrix(gdl++, OS_K0_TO_PHYSICAL(YMTX(0x12C0)), G_MTX_MODELVIEW | G_MTX_LOAD | G_MTX_NOPUSH);
        }
        gSPMatrix(gdl++, OS_K0_TO_PHYSICAL(YMTX(0x1300)), G_MTX_MODELVIEW | G_MTX_MUL | G_MTX_PUSH);
        gSPPopMatrix(gdl++, G_MTX_MODELVIEW);
        if (arg->flags & 8) {
            guScale(YMTX(0x1300), halfW * gYoshiOpen * gYoshiScale / 1000.0f,
                    halfH * gYoshiOpen * gYoshiScale / 1000.0f, 1.0f);
            gSPMatrix(gdl++, OS_K0_TO_PHYSICAL(YMTX(0x1300)), G_MTX_MODELVIEW | G_MTX_MUL | G_MTX_PUSH);
        } else {
            guScale(YMTX(0x1300), halfW / 1000.0f, halfH / 1000.0f, 1.0f);
            gSPMatrix(gdl++, OS_K0_TO_PHYSICAL(YMTX(0x1300)), G_MTX_MODELVIEW | G_MTX_MUL | G_MTX_PUSH);
        }
        gDPPipeSync(gdl++);
        gDPSetRenderMode(gdl++, 0x504340, 0);
        gDPSetCombineMode(gdl++, G_CC_SHADE, G_CC_SHADE);
        gSPClearGeometryMode(gdl++, 0xFFFFFFFF);
        gSPSetGeometryMode(gdl++, G_SHADE | G_SHADING_SMOOTH);
        gSPTexture(gdl++, 0xFFFF, 0xFFFF, 0, G_TX_RENDERTILE, G_OFF);
        if (!(arg->flags & 0x200)) {
            gSPDisplayList(gdl++, gYoshiWindowDl);
        }
        gSPPopMatrix(gdl++, G_MTX_MODELVIEW);
        if (arg->flags & 8) {
            guScale(YMTX(0x1280), gYoshiOpen, gYoshiOpen, 1.0f);
            gSPMatrix(gdl++, OS_K0_TO_PHYSICAL(YMTX(0x1280)), G_MTX_MODELVIEW | G_MTX_MUL | G_MTX_NOPUSH);
        }
        if ((arg->flags & 0x20) && yoshiState == 2) {
            s32 up;
            s32 down;

            up = 0;
            down = 0;
            if (gYoshiPrevButton == 0x800) {
                if (gContStickY >= 31 && gContPrevStickY < 31) {
                    up = 1;
                } else {
                    up = 0;
                }
            } else if (gContStickX < -30 && gContPrevStickX >= -30) {
                up = 1;
            } else {
                up = 0;
            }
            if (gYoshiNextButton == 0x400) {
                if (gContStickY < -30 && gContPrevStickY >= -30) {
                    down = 1;
                } else {
                    down = 0;
                }
            } else if (gContStickX >= 31 && gContPrevStickX < 31) {
                down = 1;
            } else {
                down = 0;
            }
            if (PRESSED(gYoshiPrevButton) || up) {
                u16 prev;

                prev = yoshi_prev_item(arg->firstItem, arg->sel, 1);
                sound = arg->moveSound;
                if (sound) {
                    if (prev != arg->sel) {
                        snd_play(gSfxBank, sound, NULL);
                    } else {
                        snd_play(gSfxBank, SFX_DENIED, NULL);
                    }
                }
                gYoshiScrollGoal += gYoshiItems[arg->sel].y - gYoshiItems[prev].y;
                arg->sel = prev;
            } else if (((((arg->atEnd ? 0 : 0x8000) | gYoshiNextButton) & gContButtons) &&
                        !(((arg->atEnd ? 0 : 0x8000) | gYoshiNextButton) & gContPrevButtons)) ||
                       down) {
                u16 next;

                next = yoshi_next_item(arg->firstItem, arg->itemCount, arg->sel, 1);
                if (yoshi_next_item(arg->firstItem, arg->itemCount, next, 1) == next) {
                    arg->atEnd = 1;
                }
                sound = arg->moveSound;
                if (sound) {
                    if (next != arg->sel) {
                        snd_play(gSfxBank, sound, NULL);
                    } else {
                        snd_play(gSfxBank, SFX_DENIED, NULL);
                    }
                }
                gYoshiScrollGoal += gYoshiItems[arg->sel].y - gYoshiItems[next].y;
                arg->sel = next;
                gYoshiStateTime = PORT_GVI("yoshi_run_window", gScheduler.frameCount);
            }
        }
        if (arg->flags & 0x4000) {
            if (arg->flags & 0x100000) {
                if (arg->timeout == 0 || !((PORT_GVI("yoshi_run_window", gScheduler.frameCount) - gYoshiStateTime) / 60.0f < arg->timeout)) {
                    if (arg->flags & 0x800000) {
                        gYoshiScroll -= 0.5;
                    } else {
                        gYoshiScroll -= 1.0;
                    }
                }
            } else {
                gYoshiScroll += (gYoshiScrollGoal - gYoshiScroll) * 0.1;
            }
            if (arg->flags & 0x10000) {
                s32 cc;
                s16 off;

                cc = 0;
                if (arg->flags & 0x40000) {
                    off = 0x12;
                } else {
                    off = 0x1C;
                }
                if (!(gGameMode & (MODE_BOOT_SCREEN1 | MODE_BOOT_SCREEN2 | MODE_LEVEL_STATUS | MODE_PAK_CHECKED | MODE_LOAD_GAME | MODE_NEW_PLAYER | MODE_NAME_ENTRY | MODE_ERASE_CONFIRM | MODE_ERASE_PLAYER_CHOSEN | MODE_LOAD_GAME_RETURN | MODE_ERASE_MENU | MODE_TO_ERASE_MENU | MODE_NAME_ENTERED | MODE_PLAYER_CHOSEN | MODE_RESULTS | MODE_PAK_CHECK | MODE_PRE_PROMOTION | MODE_PAK_FILES | MODE_UNUSED_33 | MODE_UNUSED_WINDOW_13 | MODE_PAK_MANAGER_CHECKED | MODE_PAK_FILE_CHOSEN | MODE_PAK_DELETE_CONFIRM | MODE_PAK_FILE_DELETED | MODE_UNUSED_41 | MODE_BEST_TIMES | MODE_PAK_MANAGER | MODE_ATTRACT_PICTURE | MODE_REGRADE_NOTICE | MODE_REGRADE_NOTICE_END | MODE_UNUSED_52 | MODE_BOOT_CHECK | MODE_ERASE_SAVE | MODE_BEST_TIMES_UNUSED | MODE_GUEST_PLAYER | MODE_NO_CONTROLLER | MODE_QUIT_GAME | MODE_LOAD_GAME_EXIT)) || gBufferIndex) {
                    gYoshiArrowBob += gYoshiArrowDir;
                }
                if (gYoshiArrowDir < 0) {
                    gYoshiArrowBob += gYoshiArrowDir * 2;
                }
                if (gYoshiArrowBob < 0 || gYoshiArrowBob >= 8) {
                    gYoshiArrowBob -= gYoshiArrowDir * 2;
                    gYoshiArrowDir = -gYoshiArrowDir;
                }
                if (yoshi_next_item(arg->firstItem, arg->itemCount, arg->sel, 1) != arg->sel) {
                    u8 *c2;

                    c2 = gYoshiColors[18];
                    cc = hud_sprite_quad_colors((struct SpriteVtxBuf *) N64_IPTR(arg1), 0, cc, -halfW, halfH - gYoshiArrowBob - off, 16, gYoshiArrowBob / 2 + 10,
                                       c2[0], c2[1], c2[2], gYoshiAlpha, c2[4], c2[5], c2[6], gYoshiAlpha,
                                       c2[0], c2[1], c2[2], gYoshiAlpha, c2[4], c2[5], c2[6], gYoshiAlpha);
                    cc = hud_sprite_quad((struct SpriteVtxBuf *) N64_IPTR(arg1), 0, cc, -3 - halfW, halfH - gYoshiArrowBob - off + 3, 16,
                                       gYoshiArrowBob / 2 + 10, 0, 0, 0, gYoshiAlpha / 2);
                    gdl = hud_sprite_setup(gdl, 1);
                    gSPVertex(gdl++, arg1 + 0x1E00, 8, 0);
                    gSP1Triangle(gdl++, 4, 5, 6, 0);
                    gSP1Triangle(gdl++, 4, 6, 7, 0);
                    gSP1Triangle(gdl++, 0, 1, 2, 0);
                    gSP1Triangle(gdl++, 0, 2, 3, 0);
                }
                if (yoshi_prev_item(arg->firstItem, arg->sel, 1) != arg->sel) {
                    u8 *c3;

                    c3 = gYoshiColors[18];
                    /* The ROM reloads c3[0] for the second colour here (unlike the call above, where
                     * IDO reuses the first load). No plain spelling found that defeats ugen's load
                     * cache; the volatile cast reproduces it with identical code. */
                    cc = hud_sprite_quad_colors((struct SpriteVtxBuf *) N64_IPTR(arg1), 1, cc, -halfW, gYoshiArrowBob - halfH + off, 16, gYoshiArrowBob / 2 + 10,
                                       c3[0], c3[1], c3[2], gYoshiAlpha, c3[4], c3[5], c3[6], gYoshiAlpha,
                                       ((volatile u8 *) c3)[0], c3[1], c3[2], gYoshiAlpha, c3[4], c3[5], c3[6], gYoshiAlpha);
                    cc = hud_sprite_quad((struct SpriteVtxBuf *) N64_IPTR(arg1), 1, cc, -3 - halfW, gYoshiArrowBob - halfH + off - 3, 16,
                                       gYoshiArrowBob / 2 + 10, 0, 0, 0, gYoshiAlpha / 2);
                    gdl = hud_sprite_setup(gdl, 1);
                    gSPVertex(gdl++, &((Vtx *) N64_IPTR((arg1 + 0x1D80)))[cc], 8, 0);
                    gSP1Triangle(gdl++, 4, 5, 6, 0);
                    gSP1Triangle(gdl++, 4, 6, 7, 0);
                    gSP1Triangle(gdl++, 0, 1, 2, 0);
                    gSP1Triangle(gdl++, 0, 2, 3, 0);
                }
            }
        } else {
            gYoshiScroll = 0.0f;
        }
        gYoshiScrollY = gYoshiScroll;
        if (arg->flags & 0x1000) {
            if (arg->flags & 0x20000) {
                gdl = spr_begin(gdl);
            } else {
                gdl = spr_begin_quads(gdl);
            }
            for (i = arg->firstItem; i < arg->firstItem + arg->itemCount; i++) {
                node = &gYoshiItems[i];
                if (!(node->flags & 0x800) && (node->flags & 0x400) && (!(node->flags & 0x300) || i <= gYoshiTypeItem)) {
                    SpriteAnim *snd;
                    s16 x;
                    s16 y;
                    u8 visible;
                    u8 mode;
                    u8 prev;
                    u8 cur;
                    u8 frame;

                    snd = &gYoshiSprites[node->sprite];
                    if (arg->flags & 0x20000) {
                        x = arg->x;
                    } else {
                        x = -halfW;
                    }
                    if (arg->flags & 0x20000) {
                        y = arg->y;
                    } else {
                        y = -halfH;
                    }
                    visible = 1;
                    mode = snd->mode;
                    if ((node->flags & 1) && i != arg->sel) {
                        visible = 0;
                    }
                    prev = gYoshiSpriteTick[node->sprite];
                    cur = gYoshiSpriteTick[node->sprite] = FRAMES(PORT_GVI("yoshi_run_window", gScheduler.frameCount)) / snd->speed % snd->numFrames;
                    if (cur != prev && (visible || gYoshiSpriteFrame[node->sprite])) {
                        gYoshiSpriteFrame[node->sprite] = (gYoshiSpriteFrame[node->sprite] + 1) % snd->numFrames;
                    }
                    frame = snd->frames[gYoshiSpriteFrame[node->sprite]];
                    if (frame) {
                        if (visible) {
                            if (i == arg->sel && (node->flags & 0x40)) {
                                mode |= 8;
                            }
                            gdl = spr_draw(gdl, node->spriteBase + frame - 1, snd->x + node->x + x,
                                                ((node->flags & 0x1000) ? gYoshiScrollY : 0) + (snd->y + node->y + y),
                                                yoshi_scroll_fade((u16 *) arg, (u16 *) node, node->y + snd->y - halfH + gYoshiScrollY + 8) *
                                                    gYoshiOpen * gYoshiScale,
                                                mode, snd->scale);
                        } else {
                            gdl = spr_draw(gdl, node->spriteBase + frame - 1, snd->x + node->x + x,
                                                ((node->flags & 0x1000) ? gYoshiScrollY : 0) + (snd->y + node->y + y),
                                                yoshi_scroll_fade((u16 *) arg, (u16 *) node, node->y + snd->y - halfH + gYoshiScrollY + 8) *
                                                    gYoshiOpen * gYoshiScale * 0.7,
                                                mode & ~1, snd->scale);
                        }
                    }
                }
            }
            if (arg->flags & 0x20000) {
                gdl = spr_end(gdl);
            } else {
                gdl = spr_pipe_sync(gdl);
            }
        }
        if (currentYoshiWindow < 0x62 || currentYoshiWindow >= 0x6C || gGameMode == MODE_ATTRACT) {
            for (i = arg->firstItem; i < arg->firstItem + arg->itemCount; i++) {
                node = &gYoshiItems[i];
                wtext = NULL;
                text = yoshi_get_item_text(arg, i, 0);
                if ((node->flags & 0x80) && !(node->flags & 0x800)) {
                    if (i == arg->sel) {
                        text_print_gradient((Gfx * N64P *) N64_IPTR(arg1), text, wtext, node->flags & 8, 0, node->x - halfW - 3,
                                      ((node->flags & 0x1000) ? gYoshiScrollY : 0) + (node->y - halfH) + 3, node->w,
                                      node->h, 1, 0, 0, 0,
                                      gYoshiAlpha * gYoshiColors[node->selColor][3] * yoshi_scroll_fade((u16 *) arg, (u16 *) node, node->y - halfH + gYoshiScrollY) / 65025 / 2,
                                      0, 0, 0,
                                      gYoshiAlpha * gYoshiColors[node->selColor][3] * yoshi_scroll_fade((u16 *) arg, (u16 *) node, node->y - halfH + gYoshiScrollY + node->h) / 65025 / 2);
                    } else if (!(node->flags & 4) || FRAMES(PORT_GVI("yoshi_run_window", gScheduler.frameCount) % 23) < 16) {
                        text_print_gradient((Gfx * N64P *) N64_IPTR(arg1), text, wtext, node->flags & 8, 0, node->x - halfW - 3,
                                      ((node->flags & 0x1000) ? gYoshiScrollY : 0) + (node->y - halfH) + 3, node->w,
                                      node->h, 1, 0, 0, 0,
                                      gYoshiAlpha * gYoshiColors[node->color][3] * yoshi_scroll_fade((u16 *) arg, (u16 *) node, node->y - halfH + gYoshiScrollY) / 65025 / 2,
                                      0, 0, 0,
                                      gYoshiAlpha * gYoshiColors[node->color][3] * yoshi_scroll_fade((u16 *) arg, (u16 *) node, node->y - halfH + gYoshiScrollY + node->h) / 65025 / 2);
                    }
                }
            }
        }
        for (i = arg->firstItem; i < arg->firstItem + arg->itemCount; i++) {
            node = &gYoshiItems[i];
            wtext = NULL;
            text = yoshi_get_item_text(arg, i, 0);
            if (!(node->flags & 0x800)) {
                if (i == arg->sel) {
                    if ((!(node->flags & 4) || FRAMES(PORT_GVI("yoshi_run_window", gScheduler.frameCount) % 23) < 16) &&
                        (!(node->flags & 0x40) || FRAMES(PORT_GVI("yoshi_run_window", gScheduler.frameCount) % 15) < 11)) {
                        text_print_gradient((Gfx * N64P *) N64_IPTR(arg1), text, wtext, node->flags & 8, 0, node->x - halfW,
                                      ((node->flags & 0x1000) ? gYoshiScrollY : 0) + (node->y - halfH), node->w,
                                      node->h, 1, gYoshiColors[node->selColor][0], gYoshiColors[node->selColor][1],
                                      gYoshiColors[node->selColor][2],
                                      gYoshiAlpha * gYoshiColors[node->selColor][3] * yoshi_scroll_fade((u16 *) arg, (u16 *) node, node->y - halfH + gYoshiScrollY) / 65025,
                                      gYoshiColors[node->selColor][4], gYoshiColors[node->selColor][5],
                                      gYoshiColors[node->selColor][6],
                                      gYoshiAlpha * gYoshiColors[node->selColor][7] * yoshi_scroll_fade((u16 *) arg, (u16 *) node, node->y - halfH + gYoshiScrollY + node->h) / 65025);
                    }
                } else if (!(node->flags & 4) || FRAMES(PORT_GVI("yoshi_run_window", gScheduler.frameCount) % 23) < 16) {
                    text_print_gradient((Gfx * N64P *) N64_IPTR(arg1), text, wtext, node->flags & 8, 0, node->x - halfW,
                                  ((node->flags & 0x1000) ? gYoshiScrollY : 0) + (node->y - halfH), node->w, node->h,
                                  1, gYoshiColors[node->color][0], gYoshiColors[node->color][1],
                                  gYoshiColors[node->color][2],
                                  gYoshiAlpha * gYoshiColors[node->color][3] * yoshi_scroll_fade((u16 *) arg, (u16 *) node, node->y - halfH + gYoshiScrollY) / 65025,
                                  gYoshiColors[node->color][4], gYoshiColors[node->color][5], gYoshiColors[node->color][6],
                                  gYoshiAlpha * gYoshiColors[node->color][7] * yoshi_scroll_fade((u16 *) arg, (u16 *) node, node->y - halfH + gYoshiScrollY + node->h) / 65025);
                }
            }
        }
        text_flush(&gdl, arg1);
    }
    return gdl;
}

/* Arms the typewriter reveal of a window's text items (0x8026EF70). */
void yoshi_start_typing(YoshiWindow *arg0) {
    if (arg0->flags & 0x80) {
        gYoshiTypeItem = yoshi_next_item(arg0->firstItem, arg0->itemCount, arg0->firstItem - 1, 0x100);
        gYoshiTypeChar = 0;
        if (gYoshiTypeItem + 1 == arg0->firstItem) {
            gYoshiTypeState = 0;
        } else {
            gYoshiTypeState = 1;
        }
    } else {
        gYoshiTypeState = 0;
    }
}

/* An item's text (partial while typing) (0x8026F004).
 * Returns the text of message node idx, revealing it one character at a
 * time while gYoshiTypeState is 2 */
void *yoshi_get_item_text(YoshiWindow *arg0, u16 idx, u8 japanese) {
    YoshiItem *node = &gYoshiItems[idx];
    u8 lang = japanese ? 1 : 0;
    u8 *text = node->text;
    u16 *jtext = node->jtext;
    u16 ch;
    s32 i;
    u16 next;

    gYoshiTypeBuf[0] = gTextTerminator[lang];
    switch (gYoshiTypeState) {
        case 0:
            if (japanese) {
                return jtext;
            }
            return text;
        case 1:
            if (yoshiState == 2) {
                gYoshiTypeState = 2;
                gYoshiTypeTime = PORT_GVI("yoshi_get_item_text", gScheduler.frameCount);
            }
            break;
        case 2:
            if (idx < gYoshiTypeItem) {
                if (japanese) {
                    return jtext;
                }
                return text;
            }
            if (idx <= gYoshiTypeItem) {
                if (PORT_GVI("yoshi_get_item_text", gScheduler.frameCount) - gYoshiTypeTime >= 5) {
                    gYoshiTypeTime = PORT_GVI_LAST(gScheduler.frameCount);
                    gYoshiTypeChar++;
                    if (japanese) {
                        ch = jtext[gYoshiTypeChar];
                    } else {
                        ch = text[gYoshiTypeChar];
                    }
                    if (ch == gFontAltBlankChar[lang]) {
                        snd_play(gSfxBank, SFX_TYPE_ALT, NULL);
                    } else if (ch != gTextSpace[lang] && ch != gTextTerminator[lang] && ch != gFontNoAdvanceChar[lang]) {
                        snd_play(gSfxBank, SFX_TYPE_KEY, NULL);
                    }
                }
                if (japanese) {
                    VIDI_ASSERT(jtext, 70);
                } else {
                    VIDI_ASSERT(text, 70);
                }
                i = 0;
                if (japanese) {
                    while (jtext[i] != gTextTerminator[lang]) {
#ifdef NON_MATCHING
                        /* unsequenced in the original: IDO stores at the old i
                         * (see 168B0.c font_narrow) */
                        gYoshiTypeBuf[i] = jtext[i];
                        i++;
#else
                        gYoshiTypeBuf[i] = jtext[i++];
#endif
                    }
                } else {
                    while (text[i] != gTextTerminator[lang]) {
                        gYoshiTypeBuf[i] = text[i];
                        i++;
                    }
                }
                gYoshiTypeBuf[i] = gTextTerminator[lang];
                if (gYoshiTypeBuf[gYoshiTypeChar] == gTextTerminator[lang]) {
                    next = yoshi_next_item(arg0->firstItem, arg0->itemCount, gYoshiTypeItem, 0x100);
                    if (next == gYoshiTypeItem) {
                        gYoshiTypeState = 0;
                        if ((arg0->flags & 0x400000) && yoshiState == 2) {
                            gYoshiStateTime = PORT_GVI("yoshi_get_item_text", gScheduler.frameCount);
                        }
                    } else {
                        snd_play(gSfxBank, SFX_TYPE_NEWLINE, NULL);
                        gYoshiTypeItem = next;
                    }
                    gYoshiTypeChar = 0;
                } else {
                    gYoshiTypeBuf[gYoshiTypeChar] = gTextTerminator[lang];
                    if (!(node->flags & 0x4000) && PORT_GVI("yoshi_get_item_text", gScheduler.frameCount) % 10 >= 6) {
                        gYoshiTypeBuf[gYoshiTypeChar] = gYoshiTypeCursor[lang];
                        gYoshiTypeBufNext[gYoshiTypeChar] = gTextTerminator[lang];
                    }
                }
                if (japanese) {
                    return gYoshiTypeBuf;
                }
                return font_narrow(gYoshiTypeBuf);
            }
            break;
    }
    if ((node->flags & 0x100) || (node->flags & 0x200)) {
        if (japanese) {
            return gYoshiTypeBuf;
        }
        return font_narrow(gYoshiTypeBuf);
    }
    if (japanese) {
        return jtext;
    }
    return text;
}

/* The alpha of a scrolling item by its height (0x8026F644). */
u8 yoshi_scroll_fade(u16 *arg0, u16 *arg1, s16 arg2) {
    if (*arg1 & 0x1000) {
        return MAX0(MIN255(512 - (ABS(arg2) * 512) / (arg0[1] / 3)));
    }
    return 255;
}

/* The previous item with a flag in mask (else hi) (0x8026F82C). */
u16 yoshi_prev_item(u16 lo, u16 hi, u16 mask) {
    s32 i;

    for (i = hi - 1; i >= lo; i--) {
        if (gYoshiItems[i].flags & mask) {
            return i;
        }
    }
    return hi;
}

/* The next item with a flag in mask (else start) (0x8026F8A8). */
u16 yoshi_next_item(u16 arg0, u16 arg1, u16 start, u16 mask) {
    s32 i;

    for (i = start + 1; i < arg0 + arg1; i++) {
        if (gYoshiItems[i].flags & mask) {
            return i;
        }
    }
    return start;
}

/* Index of the lowest set bit (a game mode's number) (0x8026F92C). */
s32 lowest_set_bit(u64 in) {
    u64 i;

    YOSHI_ASSERT(in, 2270);
    if (!in) {
        return -1;
    }
    for (i = 0; !((1LL << i) & in); i++) {
    }
    return i;
}

/* The path building's icon id, name and word ("path builing") (0x8026FA38). */
u8 yoshi_get_target_building(char * N64P *name, s32 *arg1) {
    s32 i;
    s32 result = 0;

    debug_printf("path builing=%d\n", gPathBuilding);
    for (i = 0; i < 7 && result == 0; i++) {
        if (gTargetBuildings[i].id == gPathBuilding) {
            result = i + 26;
        }
    }
    if (result == 0) {
        result = 26;
        i = 1;
    }
    if (name) {
        *name = gTargetBuildings[i - 1].name;
    }
    if (arg1) {
        *arg1 = gTargetBuildings[i - 1].unk10;
    }
    return result;
}

/* Points gYoshiItems at the window's item table (0x8026FB50). */
void yoshi_select_items(YoshiWindow *arg0) {
    if (arg0->flags & 0x8000) {
        gYoshiItems = gYoshiHeapItems;
    } else if (arg0->flags & 0x800) {
        gYoshiItems = gYoshiFeItems;
    } else {
        gYoshiItems = gYoshiGameItems;
    }
}

/* The level's RDU records on the heap ("FIND %d RDUS IN") (0x8026FBB0). */
void rdu_init(s16 *pos, s16 *end) {
    s32 pad;

    gRduCount = 0;
    gScoreRdus = 0;
    if (gGameMode != MODE_REPLAY) {
        gRduLitOrderLen = *gRduLitOrder = 0;
    }
    gRdus = (RduRecord *) gHeapPtr;
    gRduVtxSet = 0;
    gRduLastHeading = 999999.0f;
    while (pos != end) {
        gRdus[gRduCount].x = pos[0];
        gRdus[gRduCount].y = pos[1];
        gRdus[gRduCount].z = pos[2];
        gRdus[gRduCount].visited = 0;
        gRdus[gRduCount].cell = pos[0] / (gLevelCellW >> 5) + pos[2] / (gLevelCellD >> 5) * gLevelCols;
        util_copy64((u64 *) (gRdus[gRduCount].vtx[0]), (u64 *) gRduQuad, sizeof(gRdus->vtx[0]));
        util_copy64((u64 *) (gRdus[gRduCount].vtx[1]), (u64 *) gRduQuad, sizeof(gRdus->vtx[1]));
        gRduCount++, pos += 3;
    }
    gHeapPtr = (u8 *) ((RduRecord *) gHeapPtr + gRduCount);
}

/* 1 if RDU i is lit (0x8026FE6C). */
u8 rdu_is_lit(s32 arg0) {
    return gRdus[arg0].visited;
}

/* Lights RDU i and counts it (status restore) (0x8026FE8C). */
void rdu_set_lit(s32 arg0) {
    gRdus[arg0].visited = 1;
    gScoreRdus++;
}

/* Lights the RDUs the player passes over (0x8026FEC4). */
void rdu_update(void) {
    s32 i;
    s32 pad;
    s32 dist;
    u8 cell;
    u8 flag;

    flag = 0;
    cell = gPlayerZ / gLevelCellD * gLevelCols + gPlayerX / gLevelCellW;
    for (i = 0; i < gRduCount; i++) {
        if (gRdus[i].cell == cell && !gRdus[i].visited) {
            dist = util_dist3d(gPlayerX >> 5, gPlayerY >> 5, gPlayerZ >> 5,
                                 gRdus[i].x, gRdus[i].y, gRdus[i].z);
            if (dist < gRduPickupRadius[gCurVehicle]) {
                if (++gScoreRdus >= 4 && !gPaused) {
                    yoshi_show_hint(YW_HINT_RDUS);
                }
                if (gGameMode != MODE_REPLAY) {
                    gRduLitOrder[gRduLitOrderLen] = gRduLitOrderNext[gRduLitOrderLen] = i;
                    gRduLitOrderLen++;
                }
                fx_spawn_node_hit(gRdus[i].x, gRdus[i].y + 5, gRdus[i].z);
                gRdus[i].visited = 1;
                if (!flag) {
                    flag = 1;
                    if (gLevelKind == LEVEL_KIND_PILL_MAZE) {
                        snd_play(gSfxBank, SFX_RDU_GHOST_LEVEL, NULL);
                    } else {
                        snd_play(gSfxBank, SFX_RDU, NULL);
                    }
                }
            }
        }
    }
}

/* The visible RDUs as camera-facing quads (0x802701A8).
 * Draws the Yoshi path nodes, rotating their quads to face the camera */
void rdu_draw(Gfx **gfx, s32 arg1) {
    Gfx *gdl;
    s32 i;
    s32 j;
    f32 mtx[4][4];
    f32 ox[4];
    f32 oy[4];
    f32 oz[4];

    gdl = *gfx;
    gSPClearGeometryMode(gdl++, 0xFFFFFFFF);
    gSPSetGeometryMode(gdl++, G_ZBUFFER | G_SHADE | G_SHADING_SMOOTH);
    gDPPipeSync(gdl++);
    gDPSetCycleType(gdl++, G_CYC_1CYCLE);
    gDPSetRenderMode(gdl++, G_RM_ZB_XLU_SURF, G_RM_ZB_XLU_SURF2);
    gDPSetCombineMode(gdl++, G_CC_MODULATERGBA, G_CC_MODULATERGBA);
    gSPTexture(gdl++, 0xFFFF, 0xFFFF, 0, G_TX_RENDERTILE, G_ON);
    if (gCamHeading != gRduLastHeading) {
        gRduVtxSet ^= 1;
        guRotateF(mtx, gCamHeading - 135.0, 0.0f, 1.0f, 0.0f);
        for (i = 0; i < 4; i++) {
            guMtxXFMF(mtx, gRduQuad[i].v.ob[0], gRduQuad[i].v.ob[1], gRduQuad[i].v.ob[2], &ox[i], &oy[i],
                      &oz[i]);
        }
        for (i = 0; i < gRduCount; i++) {
            for (j = 0; j < 4; j++) {
                gRdus[i].vtx[gRduVtxSet][j].v.ob[0] = (s16) ox[j] + gRdus[i].x;
                gRdus[i].vtx[gRduVtxSet][j].v.ob[1] = (s16) oy[j] + gRdus[i].y;
                gRdus[i].vtx[gRduVtxSet][j].v.ob[2] = (s16) oz[j] + gRdus[i].z;
            }
        }
    }
    gDPLoadTextureBlock(gdl++, osVirtualToPhysical(gRduTexUnlit), G_IM_FMT_RGBA, G_IM_SIZ_32b, 16, 16, 0,
                        G_TX_CLAMP, G_TX_CLAMP, G_TX_NOMASK, G_TX_NOMASK, G_TX_NOLOD, G_TX_NOLOD);
    for (i = 0; i < gRduCount; i++) {
        if (!gRdus[i].visited && rdu_is_visible(&gRdus[i])) {
            gSPVertex(gdl++, gRdus[i].vtx[gRduVtxSet], 4, 0);
            gSP1Triangle(gdl++, 0, 1, 2, 0);
            gSP1Triangle(gdl++, 0, 2, 3, 0);
        }
    }
    gDPPipeSync(gdl++);
    gDPLoadTextureBlock(gdl++, osVirtualToPhysical(gRduTexLit), G_IM_FMT_RGBA, G_IM_SIZ_32b, 16, 16, 0,
                        G_TX_CLAMP, G_TX_CLAMP, G_TX_NOMASK, G_TX_NOMASK, G_TX_NOLOD, G_TX_NOLOD);
    for (i = 0; i < gRduCount; i++) {
        if (gRdus[i].visited && rdu_is_visible(&gRdus[i])) {
            gSPVertex(gdl++, gRdus[i].vtx[gRduVtxSet], 4, 0);
            gSP1Triangle(gdl++, 0, 1, 2, 0);
            gSP1Triangle(gdl++, 0, 2, 3, 0);
        }
    }
    gDPPipeSync(gdl++);
    *gfx = gdl;
    gRduLastHeading = gCamHeading;
}

#if defined(NON_MATCHING) && defined(PORT_HOST)
#undef rdu_is_visible
#endif
/* 1 if the RDU's grid cell is in gVisibleCells (0x80270A54). */
s32 rdu_is_visible(RduRecord *node, s32 arg1) {
    s32 i;
    u8 v;
    s32 pad;

    i = 0;
    v = node->cell;
    for (; gVisibleCells[i] != 0xFFFF; ) {
        arg1 = gVisibleCells[i++] == v;
        if (arg1) {
            return 1;
        }
    }
    return 0;
}

/* Splits the debug command line (hd_boot) into words and sets a debug switch
 * for each leading "-x" argument: -d -v -s -j -m -l -c (gDebugCheats), -C
 * (0x80270AE0). */
void parse_cmdline(u8 *cmdline) {
    s32 argc;
    u8 *args[33];
    u8 **argv;
    s32 pad;
    u8 *p;

    argc = 1;
    argv = args;
    if (cmdline != NULL && *cmdline != '\0') {
        p = cmdline;
        while (*p != '\0') {
            while (*p != '\0' && *p == ' ') {
                *p = '\0';
                p++;
            }
            if (*p != '\0') {
                args[argc] = p;
                argc++;
            }
            while (*p != '\0' && *p != ' ') {
                p++;
            }
        }
        while (argc > 1 && argv[1][0] == '-') {
            switch (argv[1][1]) {
                case 'd':
                    gDebugFlagD = 1;
                    break;
                case 'v':
                    gDebugArgV = 1;
                    break;
                case 's':
                    gDebugArgS = 1;
                    break;
                case 'j':
                    gDebugArgJ = 1;
                    break;
                case 'm':
                    gDebugArgM = 1;
                    break;
                case 'l':
                    gDebugRamEeprom = 1;
                    break;
                case 'c':
                    gDebugCheats = 1;
                    break;
                case 'C':
                    gDebugStepGameState = 1;
                    gDebugCheats = 1;
                    break;
            }
            argc--;
            argv++;
        }
    }
}
