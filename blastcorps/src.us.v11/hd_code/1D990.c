#include "common.h"
#include <ultra64.h>
#ifndef NON_MATCHING /* legacy declarations */
/* Matching build: IDO compiled the matched code here against older
 * declarations of these, which the file keeps; the NON_MATCHING build
 * uses game/game.h's. */
#define LEGACY_nss
#define LEGACY_obj_count_destroyed_path_buildings
#endif /* legacy declarations */
#include "game/game.h"
/* Views: this file reads these shared variables (game/variables.h) as other types. */
#define gLevelInfo ((LevelInfo *) gLevelInfo)
#define gYoshiSprites ((Anim30 *) gYoshiSprites)
#define gYoshiGameItems ((YoshiItem *) gYoshiGameItems)
#define players ((PlayerInfo *) players)
#ifdef NON_MATCHING
#define nss (*(u32 *) &nss)
#endif
/* end of views */

/* Level objectives and the HUD's objective/timer text: per-level setup,
 * the countdown intro, and the per-mission-type progress checks */

/* (gLevelInfo[]: LevelInfo, game/types.h) */

typedef struct {
    u8 id;
    u8 pad;
    s16 pos[3];
} Entry08;

typedef struct {
    u8 id;
    u8 pad;
    s16 pos[4];
} Entry0A;

/* Animated sprite definition (gYoshiSprites, 0x30 bytes), as in 26570.c */
typedef struct {
    /* 0x00 */ u8 pad0[4];
    /* 0x04 */ u8 count;
    /* 0x05 */ u8 pad5;
    /* 0x06 */ u16 ids[10];
    /* 0x1A */ u8 nframes;
    /* 0x1B */ u8 frames[11];
    /* 0x26 */ u8 speed; /* frames per animation step */
    /* 0x27 */ u8 pad27;
    /* 0x28 */ f32 scale;
    /* 0x2C */ u8 unk2C;
    /* 0x2D */ u8 unk2D;
    /* 0x2E */ s8 unk2E;
    /* 0x2F */ u8 unk2F;
} Anim30;

/* (players[]: PlayerInfo, game/types.h) */

extern LevelInfo * N64P gCurLevelInfo;
extern u8 gWideFovLevels[];
extern Entry0A gTrainLineLevels[];
#ifndef NON_MATCHING
extern u32 nss;
#endif
extern char gHudLines[][20]; /* HUD text lines (progress, lap times) */
extern u8 gTurboEarned;
extern s32 gTurboFrame;
extern u8 gRaceLap;
extern u16 gLapTimes[];
extern Entry08 gFixedEyeLevels[];
extern s32 gIntroStartTime;
extern u32 gIntroSecond;
extern s16 gHudObjectiveY;
extern char * N64P gObjectiveName;
extern s32 gObjectiveWord;
extern char gObjectiveBuildingsName[]; /* "BUILDINGS" */
extern s32 gObjectiveBuildingsWord;
extern u8 gIntroPluralS[];   /* " S" */
extern void * N64P gTurboStartImages[];
extern u8 gTurboFalseStart;
extern Anim30 * N64P gObjectiveAnim;
extern Anim30 * N64P gObjectiveAnim2;
extern u8 gObjectiveSprite;
extern u8 gObjectiveSprite2;
extern char gHudTimeText[];
extern u8 gRaceQuadsCrossed;  /* race: quadrants crossed this lap */
extern u8 gRacePrevQuad;  /* race: previous quadrant */
extern u8 gRaceQuad;  /* race: current quadrant */
extern u8 gBestLap;  /* race: best lap */
extern u16 gBestLapTime; /* race: best lap time */
extern s32 gRaceStartTime;
extern char gLapsLeftText[];
extern char gLapsLeftJText[];
extern s16 gTurboBannerY; /* turbo-start banner y offset */
extern u8 gTurboBannerImage;  /* turbo-start banner image (1-5) */
extern u8 gTurboBannerPrevImage;
extern char gIntroObjectiveText[];
extern char gIntroTimeText[];
extern s16 gIntroObjectiveJText[];
extern s16 gIntroTimeJText[];
extern u16 gSecondsLeft;
extern u16 gPrevSecondsLeft;

#ifndef NON_MATCHING
u8 obj_count_destroyed_path_buildings(void);
#endif
void hud_mission_intro(void);
void hud_check_rdus(void);
void hud_check_targets(void);
void hud_check_path(void);
void hud_check_damage(void);
void hud_update_race(void);
s32 hud_in_box(s16 x, s16 y, s16 x0, s16 y0, s16 x1, s16 y1);
Gfx *hud_draw_turbo_start(Gfx * N64P *, Gfx *);

#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define MAX(a, b) ((a) > (b) ? (a) : (b))
#define MAX0(x) ((x) < 0 ? 0 : (x))

/* Pre-2.0I scissored texture rectangle (as in 17E10.c and 51690.c) */
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

void cam_set_level_fovy(u8 arg0) {
    s32 i;

    gCamFovy = 45.0f;
    i = 0;
    gWideFovLevel = 0;
    do {
        if (gWideFovLevels[i] == arg0) {
            gCamFovy = 80.0f;
            gWideFovLevel = 1;
        } else {
            i++;
        }
    } while (i < 5 && !gWideFovLevel);
}

/* The BCT chopper's start position from the level info (0x802621DC). */
void lvl_init_chopper_pos(u8 arg0) {
    LevelInfo *p;

    p = &gLevelInfo[arg0];
    gChopperX = p->chopperPos[0] << 5;
    gChopperY = p->chopperPos[1] << 5;
    gChopperZ = p->chopperPos[2] << 5;
}

/* If the level has a rail line (table gTrainLineLevels), sets gTrainOnLine and the line's ends (x0, z0)-(x1, z1) in
 * world units (0x80262238). */
void lvl_init_train_line(u8 arg0) {
    s32 i;
    u8 found;

    i = 0;
    found = 0;
    gTrainOnLine = 0;
    do {
        if (gTrainLineLevels[i].id == arg0) {
            found = 1;
        } else {
            i++;
        }
    } while (!found && i < 1);
    if (found) {
        gTrainOnLine = 1;
        gTrainLineX0 = gTrainLineLevels[i].pos[0] << 5;
        gTrainLineZ0 = gTrainLineLevels[i].pos[1] << 5;
        gTrainLineX1 = gTrainLineLevels[i].pos[2] << 5;
        gTrainLineZ1 = gTrainLineLevels[i].pos[3] << 5;
    }
}

/* Fixed eye, the objective icon's sprites, the timer text (0x80262320).
 * Level start: set up the objective, its HUD icon and (for most mission
 * types) load the HUD graphics from ROM */
void hud_init_objective(u8 arg0) {
    s32 i;
    u8 anim;
    u8 unused;
    u8 found;
    s32 size;

    i = 0;
    anim = 0;
    unused = 0;
    found = 0;
    do {
        if (gFixedEyeLevels[i].id == arg0) {
            found = 1;
        } else {
            i++;
        }
    } while (!found && i < 4);
    if (found) {
        gCamFixedEye = 1;
        gCamFixedEyeX = gFixedEyeLevels[i].pos[0] << 16;
        gCamFixedEyeY = gFixedEyeLevels[i].pos[1] << 16;
        gCamFixedEyeZ = gFixedEyeLevels[i].pos[2] << 16;
    } else {
        gCamFixedEye = 0;
    }
    gCurLevelInfo = &gLevelInfo[arg0];
    gJbombLiftFadeY = gCurLevelInfo->jbombLiftFadeY << 5;
    gJbombMaxY = gCurLevelInfo->jbombMaxY << 5;
    gCamLift = gCurLevelInfo->camLift;
    gCamLiftMax = gCurLevelInfo->camLiftMax << 5;
    gCamLiftStep = gCurLevelInfo->camLiftStep;
    gCamLiftRadius = gCurLevelInfo->camLiftRadius << 5;
    gIntroStartTime = gScheduler.frameCount;
    gIntroSecond = -1;
    gHudObjectiveY = 0;
    switch (gLevelKind) {
        case LEVEL_KIND_TARGETS:
        case LEVEL_KIND_MISSION_TIMED:
            anim = 6;
            break;
        case LEVEL_KIND_BUILDINGS:
            anim = yoshi_get_target_building(&gObjectiveName, &gObjectiveWord);
            gHudObjectiveY = 2;
            break;
        case LEVEL_KIND_RDUS:
        case LEVEL_KIND_PILL_MAZE:
            anim = 9;
            gHudObjectiveY = -2;
            break;
        case LEVEL_KIND_MISSION:
        case LEVEL_KIND_RACE:
        case LEVEL_KIND_DAMAGE:
            break;
    }
    if (anim == 6) {
        gHudObjectiveY = 6;
        gObjectiveName = gObjectiveBuildingsName;
        unused = 0;
        gObjectiveWord = gObjectiveBuildingsWord;
    }
    if (gLevelKind != LEVEL_KIND_MISSION) {
        size = romControllerPicture - romTrafficLights;
        if (gLevelKind != LEVEL_KIND_MISSION_TIMED && gNextGameMode == MODE_LEVEL_START) {
            hd_rom_load((u32) romTrafficLights, (u32) gHeapPtr, (u32 *) &size, 10, 0, 1);
            for (i = 0; i < 5; i++) {
                gTurboStartImages[i] = (i << 15) + (u8 *) gHeapPtr;
            }
            gHeapPtr = (u8 *) gHeapPtr + size;
        }
        if (gNextGameMode == MODE_REPLAY) {
            gTurboStartState = 0;
        } else {
            gTurboStartState = 1;
        }
    } else {
        gTurboStartState = 0;
    }
    gTurboFalseStart = 0;
    gTurboActive = 0;
    if (gNextGameMode == MODE_REPLAY) {
        gTurboPending = 0;
    } else {
        gTurboPending = 0;
        gTurboEarned = 0;
    }
    gObjectiveAnim = &gYoshiSprites[anim];
    if (gLevelInfo[arg0].kind == LEVEL_KIND_BUILDINGS) {
        gObjectiveAnim2 = NULL;
        gObjectiveSprite = spr_load(gObjectiveAnim->ids, 0, gObjectiveAnim->count, gObjectiveAnim->unk2C, gObjectiveAnim->unk2D,
                                   gObjectiveAnim->scale * 0.5);
    } else {
        gObjectiveAnim2 = NULL;
        if (anim) {
            gObjectiveSprite = spr_load(gObjectiveAnim->ids, 0, gObjectiveAnim->count, gObjectiveAnim->unk2C, gObjectiveAnim->unk2D,
                                       1.0f);
        } else {
            gObjectiveAnim = NULL;
        }
    }
    if (gNextGameMode & (MODE_REPLAY | MODE_REPLAY_CARRIER_END)) {
        hud_format_time(gHudTimeText, gCurLevelInfo->medalTimes[3] - gTimeLeft, 0);
    } else {
        gRaceLap = 0;
    }
}

/* The objective and time-limit text, countdown sounds (0x80262840).
 * Mission intro: one step per second. Second 0 prints the objective and the
 * time limit, 1-4 play the countdown sounds */
void hud_mission_intro(void) {
    s32 secs;
    u16 mins;
    u16 rem;

    secs = (u32) (gScheduler.frameCount - gIntroStartTime) / 60;
    if (secs != gIntroSecond) {
        switch (secs) {
            case 0:
                gIntroObjectiveJText[0] = 0xFFF;
                gIntroTimeJText[0] = 0xFFF;
                switch (gLevelKind) {
                    case LEVEL_KIND_RACE:
                        sprintf(gIntroObjectiveText, "FINISH %d LAPS IN", gCurLevelInfo->target);
                        break;
                    case LEVEL_KIND_TARGETS:
                    case LEVEL_KIND_BUILDINGS:
                        if (levelno == 0x34) {
                            sprintf(gIntroObjectiveText, "DESTROY TARGETS IN");
                        } else {
                            sprintf(gIntroObjectiveText, "DESTROY %s IN", (char *) gObjectiveName); /* (char *): N64P via varargs */
                        }
                        break;
                    case LEVEL_KIND_MISSION_TIMED:
                        if (levelno == 0x32) {
                            sprintf(gIntroObjectiveText, "CLEAR SHUTTLE PATH");
                        } else {
                            sprintf(gIntroObjectiveText, "CLEAR CARRIER PATH");
                        }
                        break;
                    case LEVEL_KIND_DAMAGE:
                        sprintf(gIntroObjectiveText, "CAUSE $%d DAMAGE", gCurLevelInfo->target);
                        break;
                    case LEVEL_KIND_RDUS:
                    case LEVEL_KIND_PILL_MAZE:
                        sprintf(gIntroObjectiveText, "FIND %d RDUS IN", gCurLevelInfo->target);
                        break;
                }
                mins = gCurLevelInfo->medalTimes[3] / 600;
                rem = (gCurLevelInfo->medalTimes[3] / 10) % 60;
                sprintf(gIntroTimeText, "%d MINUTE%c %d SECONDS", mins, gIntroPluralS[mins != 1], rem);
                gYoshiGameItems[36].text = (u8 *) gIntroObjectiveText;
                gYoshiGameItems[37].text = (u8 *) gIntroTimeText;
                gYoshiGameItems[36].jtext = (u16 *) gIntroObjectiveJText;
                gYoshiGameItems[37].jtext = (u16 *) gIntroTimeJText;
                yoshi_demand(YOSHI_DEMAND_OPEN | YW_MISSION_INTRO);
                snd_play(gSfxBank, snd_random_id(SND_GROUP_MISSION_INTRO), 0);
                break;
            case 1:
                snd_play(gSfxBank, SFX_COUNTDOWN_BEEP, 0);
                break;
            case 2:
                snd_play(gSfxBank, SFX_COUNTDOWN_BEEP, 0);
                break;
            case 3:
                snd_play(gSfxBank, SFX_COUNTDOWN_BEEP, 0);
                break;
            case 4:
                snd_play(gSfxBank, SFX_COUNTDOWN_GO, 0);
                break;
        }
    }
    gIntroSecond = secs;
    if (secs == 4 && gBufferIndex == gPausedBufferIndex) {
        gNextGameMode = MODE_LEVEL;
    }
}

/* Time left, beeps, the per-mission-kind checks (0x80262BF4).
 * Per-frame objective update: the time limit, the countdown beeps, and the
 * progress check for the current mission type */
void hud_update_objective(void) {
    if (!gLevelDone && !gCarrierHit) {
        gTimeLeft = gCurLevelInfo->medalTimes[3] - MIN(gCurLevelInfo->medalTimes[3], stats_frames_to_tenths(gScheduler.retraceCount - gLevelStartTime));
    }
    hud_format_time(gHudTimeText, gTimeLeft, 1);
    switch (gGameMode) {
        case MODE_LEVEL_START:
            hud_mission_intro();
            break;
        case MODE_TIMED_END:
            if (gCarrierHit && areWeFading() == 0 && yoshiState == 1) {
                if ((players[playerNumber].medal[levelno] > 0 && players[playerNumber].medal[levelno] < 6) ? 1
                                                                                                                 : 0) {
                    fade_to_mode_speed(MODE_RESULTS, 0.25f);
                } else {
                    fade_to_mode_speed(MODE_REPLAY, 0.25f);
                }
            }
            break;
        default:
            if (gTimeLeft == 0) {
                cmo_hit_request = 1;
            } else {
                gSecondsLeft = gTimeLeft / 10;
                if (gSecondsLeft < 10 && gSecondsLeft != gPrevSecondsLeft) {
                    snd_play(gSfxBank, SFX_TIME_TICK_0 - gSecondsLeft, 0);
                }
                gPrevSecondsLeft = gSecondsLeft;
            }
            if (gFailTrigger && gLevelKind == LEVEL_KIND_PILL_MAZE) {
                snd_play(gSfxBank, SFX_GHOST_CAUGHT, 0);
            }
            if (gFailTrigger) {
                cmo_hit_request = 1;
            }
            if (gCarrierHit) {
                snd_stop_all_looping();
                snd_stop_all_repeating();
                yoshi_demand(YOSHI_DEMAND_OPEN | YOSHI_DEMAND_FORCE | YW_MISSION_FAILED);
                gYoshiPendingWindow = NO_YOSHI_WINDOW;
                gNextGameMode = MODE_TIMED_END;
            } else if (gLevelDone) {
                gNextGameMode = MODE_TIMED_END;
            } else {
                switch (gLevelKind) {
                    case LEVEL_KIND_RACE:
                        hud_update_race();
                        break;
                    case LEVEL_KIND_TARGETS:
                        hud_check_targets();
                        break;
                    case LEVEL_KIND_BUILDINGS:
                    case LEVEL_KIND_MISSION_TIMED:
                        hud_check_path();
                        break;
                    case LEVEL_KIND_DAMAGE:
                        hud_check_damage();
                        break;
                    case LEVEL_KIND_RDUS:
                    case LEVEL_KIND_PILL_MAZE:
                        hud_check_rdus();
                        break;
                }
            }
            break;
    }
}

/* Level kinds 0x10/0x40: the RDUs found (0x80262FD0).
 * Mission type 0x10/0x40 (find the RDUs): progress text */
void hud_check_rdus(void) {
    if (gScoreRdus >= gCurLevelInfo->target) {
        gLevelDoneRequest = 1;
    }
    sprintf(gHudLines[0], "%d/%d", gScoreRdus, gCurLevelInfo->target);
}

/* Level kind 4: the targets destroyed (0x8026303C).
 * Mission type 4 (destroy targets) */
void hud_check_targets(void) {
    s16 h;

    obj_tally(0);
    if (gScoreBuildings >= gCurLevelInfo->target) {
        gLevelDoneRequest = 1;
    }
    if (gWaterOn) {
        water_get_height(gPlayerX >> 5, gPlayerZ >> 5, &h);
        if (h > (gPlayerY >> 5)) {
            cmo_hit_request = 1;
        }
    } else if (gCurLevelInfo->waterY > (gPlayerY >> 5)) {
        cmo_hit_request = 1;
    }
    sprintf(gHudLines[0], "%d/%d", gScoreBuildings, gBuildingCount);
}

/* Level kinds 0x20/0x80: the path cleared (0x80263140).
 * Mission types 0x20/0x80 (destroy targets, clear the path) */
void hud_check_path(void) {
    s16 h;

    obj_tally(1);
    switch (gLevelKind) {
        case LEVEL_KIND_BUILDINGS:
            if (gScoreBuildings >= gBuildingCount) {
                gLevelDoneRequest = 1;
            }
            break;
        case LEVEL_KIND_MISSION_TIMED:
            if (gPathBuildingsDestroyed || (levelno == 0x32 && gScoreBuildings >= gBuildingCount)) {
                gLevelDoneRequest = 1;
            }
            break;
    }
    if (gWaterOn) {
        water_get_height(gPlayerX >> 5, gPlayerZ >> 5, &h);
        if (h > (gPlayerY >> 5)) {
            cmo_hit_request = 1;
        }
    } else if (gCurLevelInfo->waterY > (gPlayerY >> 5)) {
        cmo_hit_request = 1;
    }
    switch (gLevelKind) {
        case LEVEL_KIND_BUILDINGS:
            sprintf(gHudLines[0], "%d/%d", gScoreBuildings, gBuildingCount);
            break;
        case LEVEL_KIND_MISSION_TIMED:
            if (levelno == 0x32) {
                sprintf(gHudLines[0], "%d/%d", gScoreBuildings, gBuildingCount);
            } else {
                sprintf(gHudLines[0], "%d/%d", obj_count_destroyed_path_buildings(), gBuildingCount);
            }
            break;
    }
}

/* Level kind 8: the damage done ("$%d LEFT") (0x80263358).
 * Mission type 8 (cause $N damage) */
void hud_check_damage(void) {
    s32 left;

    obj_tally(0);
    if (nss >= gCurLevelInfo->target) {
        gLevelDoneRequest = 1;
    }
    left = gCurLevelInfo->target - nss;
    if (left < 0) {
        left = 0;
    }
    sprintf(gHudLines[0], "$%d LEFT", left);
}

/* Level kind 2: start box, quadrants, lap times (0x802633E0).
 * Mission type 2 (race): start line, lap times, quadrant tracking */
void hud_update_race(void) {
    s32 i;
    u8 left;

    if (gRaceLap == 0) {
        if (hud_in_box(gPlayerX >> 5, gPlayerZ >> 5, gCurLevelInfo->raceStartBox[0], gCurLevelInfo->raceStartBox[1],
                          gCurLevelInfo->raceStartBox[2], gCurLevelInfo->raceStartBox[3])) {
            gRaceLap = 1;
            gRaceQuadsCrossed = 0;
            gRacePrevQuad = gCurLevelInfo->raceQuads[0];
            gRaceStartTime = gScheduler.retraceCount;
            gBestLap = 1;
            gBestLapTime = 0xFFFF;
        }
    } else {
        gRaceQuad = ((gPlayerZ >> 5) - gCurLevelInfo->raceArea[1]) / ((gCurLevelInfo->raceArea[3] - gCurLevelInfo->raceArea[1]) >> 1);
        gRaceQuad *= 2;
        gRaceQuad += ((gPlayerX >> 5) - gCurLevelInfo->raceArea[0]) / ((gCurLevelInfo->raceArea[2] - gCurLevelInfo->raceArea[0]) >> 1);
        gLapTimes[gRaceLap - 1] = stats_frames_to_tenths(gScheduler.retraceCount - gLevelStartTime);
        for (i = gRaceLap - 2; i >= 0; i--) {
            gLapTimes[gRaceLap - 1] -= gLapTimes[i];
        }
        if (gContButtons & Z_TRIG) {
            debug_printf("box number=%d\n", gRaceQuad);
        }
        if (gRaceQuadsCrossed == 4) {
            if (hud_in_box(gPlayerX >> 5, gPlayerZ >> 5, gCurLevelInfo->raceStartBox[0], gCurLevelInfo->raceStartBox[1],
                              gCurLevelInfo->raceStartBox[2], gCurLevelInfo->raceStartBox[3])) {
                if (gLapTimes[gRaceLap - 1] < gBestLapTime) {
                    debug_printf("new best lap %d %d\n", gBestLapTime, gLapTimes[gRaceLap - 1]);
                    gBestLap = gRaceLap;
                    gBestLapTime = gLapTimes[gRaceLap - 1];
                }
                if (gCurLevelInfo->target - 1 < gRaceLap) {
                    gLevelDoneRequest = 1;
                } else {
                    left = gCurLevelInfo->target - gRaceLap;
                    if (!gPaused) {
                        yoshi_demand(YOSHI_DEMAND_OPEN | YW_LAPS_LEFT);
                    }
                    if (left == 1) {
                        sprintf(gLapsLeftText, "1 LAP LEFT!");
                    } else {
                        sprintf(gLapsLeftText, "%d LAPS LEFT!", left);
                    }
                    gYoshiGameItems[35].text = (u8 *) gLapsLeftText;
                    gYoshiGameItems[35].jtext = (u16 *) gLapsLeftJText;
                    alCSPSetTempo(gMusicPlayer, alCSPGetTempo(gMusicPlayer) * 0.95);
                }
                gRaceLap++;
                gRaceQuadsCrossed = 0;
            }
        } else if (gCurLevelInfo->raceQuads[gRaceQuadsCrossed] == gRacePrevQuad && gCurLevelInfo->raceQuads[(gRaceQuadsCrossed + 1) % 4] == gRaceQuad) {
            debug_printf("box cross: %d to %d\n", gRacePrevQuad, gRaceQuad);
            gRaceQuadsCrossed++;
        }
        gRacePrevQuad = gRaceQuad;
    }
    for (i = 0; i < gRaceLap && i < gCurLevelInfo->target; i++) {
        hud_format_time(gHudLines[i], gLapTimes[i], gRaceLap == i + 1);
    }
}

/* (x, y) in [x0, x1) x [y0, y1) (0x8026394C).
 * Is (x, y) inside the box [x0, x1) x [y0, y1)? */
s32 hud_in_box(s16 x, s16 y, s16 x0, s16 y0, s16 x1, s16 y1) {
    if (x >= x0 && y >= y0 && x < x1 && y < y1) {
        return 1;
    }
    return 0;
}

/* The progress or lap text, the timer, the objective icon (0x802639B4).
 * Draw the objective HUD: progress text or lap times, the time limit
 * (flashing red under ten seconds), and the objective icon(s) */
Gfx *hud_draw_objective(Gfx *gdl, Gfx * N64P *arg1, u32 *arg2) {
    Gfx *g;
    s32 pad60;
    u8 grn;
    u8 red;
    u8 flash;
    s16 alpha;
    s32 pad54;
#ifdef NON_MATCHING
    s32 i = 0; /* only read when gLevelKind == 2, after the lap loop above has set it */
#else
    s32 i;
#endif

    g = gdl;
    if ((gGameMode & (MODE_CARRIER_END | MODE_TIMED_END)) && currentYoshiWindow != YW_MOON_DONE) {
        alpha = 0xFF;
    } else {
        alpha = gHudAlpha;
    }
    switch (gLevelKind) {
        case LEVEL_KIND_RACE:
            for (i = 0; i < gRaceLap - ((gGameMode & (MODE_REPLAY | MODE_REPLAY_CARRIER_END)) ? 1 : 0) && i < gCurLevelInfo->target; i++) {
                if (i + 1 == gBestLap && i + 1 != gRaceLap) {
                    /* best lap */
                    text_print(arg1, (u8 *) gHudLines[i], 0, 1, 0, 0x18, i * 18 + 0x12, 0x14, 0x14, 1, 0xFF, 0, 0,
                                  gHudAlpha);
                } else if (i + 1 == gRaceLap) {
                    /* current lap */
                    text_print(arg1, (u8 *) gHudLines[i], 0, 1, 0, 0x18, i * 18 + 0x12, 0x14, 0x14, 1, 0xFF, 0xFF, 0xFF,
                                  gHudAlpha);
                } else {
                    text_print(arg1, (u8 *) gHudLines[i], 0, 1, 0, 0x18, i * 18 + 0x12, 0x14, 0x14, 1, 0xA0, 0xA0, 0xA0,
                                  gHudAlpha);
                }
            }
            break;
        case LEVEL_KIND_TARGETS:
        case LEVEL_KIND_BUILDINGS:
        case LEVEL_KIND_MISSION_TIMED:
            text_print(arg1, (u8 *) gHudLines[0], 0, 1, 0, 0x38, 0x14, 0x14, 0x14, 1, 0xFF, 0xFF, 0xFF, alpha);
            break;
        case LEVEL_KIND_DAMAGE:
            text_print(arg1, (u8 *) gHudLines[0], 0, 1, 0, 0x1C, 0x12, 0x14, 0x14, 1, 0xFF, 0xFF, 0xFF, alpha);
            break;
        case LEVEL_KIND_RDUS:
        case LEVEL_KIND_PILL_MAZE:
            text_print(arg1, (u8 *) gHudLines[0], 0, 1, 0, 0x38, 0x12, 0x14, 0x14, 1, 0xFF, 0xFF, 0xFF, alpha);
            break;
    }
    red = 0xFF;
    grn = 0;
    if (gGameMode & (MODE_LEVEL | MODE_MAP_VIEW | MODE_TIMED_END)) {
        flash = gSecondsLeft < 10;
        if (!flash) {
            grn = 0xFF;
        }
    } else {
        flash = 0;
        grn = 0xFF;
        red = 0;
    }
    if (gSecondsLeft == 0) {
        flash = 0;
    }
    if (!flash || gScheduler.frameCount % 20 < 16) {
        if (gLevelKind == LEVEL_KIND_RACE) {
            text_print(arg1, (u8 *) gHudTimeText, 0, 1, 0, 0x18, i * 18 + 0x14, 0x10, 0x10, 1, red, grn, 0, gHudAlpha);
        } else {
            text_print(arg1, (u8 *) gHudTimeText, 0, 1, 0, 0x1C, gHudObjectiveY + 0x2A, 0x10, 0x10, 1, red, grn, 0, alpha);
        }
    }
    if (gLevelDone && gGameMode == MODE_TIMED_END) {
        hud_level_complete(&g, (s32) arg1, gBufferIndex);
    }
    if (gTurboStartState) {
        g = hud_draw_turbo_start(arg1, g);
    }
    PORT_WTAG(g, PORT_WTAG_LEFT, 0, 0); /* the objective icon (widescreen HUD sides: see port.h) */
    g = spr_begin(g);
    if (gObjectiveAnim) {
        g = spr_draw(g, gObjectiveAnim->frames[(gScheduler.frameCount / gObjectiveAnim->speed) % gObjectiveAnim->nframes] + gObjectiveSprite - 1,
                          0x18, gHudObjectiveY + 0xC, alpha, 1, 1.0f);
    }
    if (gObjectiveAnim2) {
        g = spr_draw(g, gObjectiveAnim2->frames[(gScheduler.frameCount / gObjectiveAnim2->speed) % gObjectiveAnim2->nframes] + gObjectiveSprite2 - 1,
                          0x18, gHudObjectiveY + 0xC, alpha, 1, 1.0f);
    }
    g = spr_end(g);
    PORT_WTAG(g, PORT_WTAG_NONE, 0, 0);
    arg2 += g - gdl; /* dead store to the parameter, kept for the match */
    return g;
}

/* Replays: re-arms the turbo start on its frame (0x8026420C). */
void hud_replay_turbo(void) {
    if (gTurboEarned && gPlayFrames == gTurboFrame) {
        debug_printf("Replay turbo ....\n");
        gTurboPending = 1;
    }
}

/* The turbo-start banner's state machine and drawing (0x80264264).
 * Turbo-start banner: a small state machine in gTurboStartState that scrolls the
 * countdown images in, rewards a turbo start, then scrolls them out; draws
 * the current image as eight 32-pixel-wide strips */
Gfx *hud_draw_turbo_start(Gfx * N64P *arg0, Gfx *gdl) {
    Gfx *g;
    s32 i;
    s32 x0;

    g = gdl;
    switch (gTurboStartState) {
        case 1:
            gTurboBannerImage = 3;
            gTurboStartState = 2;
            break;
        case 2:
            gTurboBannerY = (gScheduler.frameCount - gIntroStartTime) * 60 / 60 - 0x40;
            gTurboBannerImage = 3 - MAX0(gTurboBannerY * 2 / 10);
            if (gTurboBannerY >= 10) {
                gTurboBannerImage = 1;
                gTurboBannerY = 10;
                gTurboStartState = 3;
            }
            break;
        case 3:
            if (!gPaused) {
                gTurboStartState = 4;
            }
            if (gHeldA) {
                gTurboFalseStart = 1;
            }
            break;
        case 4:
            gTurboBannerPrevImage = gTurboBannerImage;
            if (gScheduler.retraceCount - gLevelStartTime > 6) {
                gTurboBannerImage = 5;
            } else {
                gTurboBannerImage = 4;
            }
            if (gScheduler.retraceCount - gLevelStartTime > 20) {
                gTurboStartState = 5;
                gTurboPending = gTurboEarned;
                gTurboFrame = gPlayFrames;
            }
            if (gTurboBannerPrevImage == 1 && !gHeldA) {
                gTurboEarned = !gTurboFalseStart;
            }
            if (gTurboBannerPrevImage == 4 && gTurboBannerImage == 5 && !gHeldA) {
                gTurboEarned = 0;
            }
            break;
        case 5:
            gTurboBannerY = 10 - ((gScheduler.retraceCount - gLevelStartTime) * 2 * 60 - 2400) / 60;
            if (gTurboBannerY < -0x3F) {
                /* comma (or both on one line) needed for the store to land in the jal delay slot */
                gTurboStartState = 0, debug_printf("turbo %d\n", gTurboEarned);
            }
            break;
    }
    if (gLevelKind != LEVEL_KIND_MISSION_TIMED) {
        gDPPipeSync(g++);
        gSPTexture(g++, 0, 0, 0, G_TX_RENDERTILE, G_OFF);
        gDPSetTexturePersp(g++, G_TP_NONE);
        gDPSetCycleType(g++, G_CYC_1CYCLE);
        gDPSetRenderMode(g++, G_RM_CLD_SURF, G_RM_CLD_SURF2);
        gDPSetCombine(g++, 0xFF97FF, 0xFF2CFE7F);
        gDPSetPrimColor(g++, 0, 0, 0, 0, 0, 0xFF);
        x0 = 0x20;
        for (i = 0; i < 0x100; i += 0x20) {
            gDPLoadTextureTile(g++, gTurboStartImages[gTurboBannerImage - 1], G_IM_FMT_RGBA, G_IM_SIZ_16b, 256, 0, i, 0, i + 31, 63,
                               0, G_TX_CLAMP, G_TX_CLAMP, 0, 0, G_TX_NOLOD, G_TX_NOLOD);
            gSPScisTextureRectangleOld(g++, (i + x0) << 2, 0 << 2, (i + x0 + 0x20) << 2, (gTurboBannerY + 0x3F) << 2, 0,
                                       i << 5, -(gTurboBannerY << 5), 1 << 10, 1 << 10);
        }
        gDPPipeSync(g++);
        gDPSetTexturePersp(g++, G_TP_PERSP);
    }
    return g;
}

/* Tenths of a second to "MM:SS.t" (0x80264A34).
 * Format a time in tenths of a second as "MM:SS.t" */
void hud_format_time(char *buf, u16 t, s32 arg2) {
    buf[0] = t / 6000 + '0';
    t %= 6000;
    buf[1] = t / 600 + '0';
    buf[2] = ':';
    t %= 600;
    buf[3] = t / 100 + '0';
    t %= 100;
    buf[4] = t / 10 + '0';
    t %= 10;
    buf[5] = '.';
    buf[6] = t + '0';
    buf[7] = 0;
}

/* The time limit minus the finished laps' times (0x80264AEC).
 * Race (mission type 2): time left = limit minus the finished laps' times */
void hud_race_time_left(void) {
    s32 i;
    u16 sum;

    sum = 0;
    if (gLevelKind == LEVEL_KIND_RACE && gRaceLap >= 2) {
        for (i = 0; i < gRaceLap - 1; i++) {
            sum += gLapTimes[i];
        }
        gTimeLeft = gCurLevelInfo->medalTimes[3] - sum;
    }
    if (gTimeLeft >= 60000) {
        gTimeLeft = 0;
    }
}

/* A level's globe node: Moon 0, Neptune 1, Mars 2, Earth 3, Venus 4, Mercury 5 (0x80264BA4). */
u8 lvl_planet(u8 arg0) {
    u8 ret;

    switch (arg0) {
        case 40:
            ret = 0;
            break;
        case 43:
            ret = 5;
            break;
        case 44:
            ret = 4;
            break;
        case 45:
            ret = 2;
            break;
        case 46:
            ret = 1;
            break;
        default:
            ret = 3;
            break;
    }
    return ret;
}
