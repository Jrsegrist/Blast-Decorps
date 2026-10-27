#include "common.h"
#include <ultra64.h>
#ifndef NON_MATCHING /* legacy declarations */
/* Matching build: IDO compiled the matched code here against older
 * declarations of these, which the file keeps; the NON_MATCHING build
 * uses game/game.h's. */
#define LEGACY_gSfxBank
#endif /* legacy declarations */
#include "game/game.h"
/* Views: this file reads these shared variables (game/variables.h) as other types. */
#define players ((PlayerInfo *) players)
/* end of views */

/* (players[]: PlayerInfo, game/types.h) */

#ifndef NON_MATCHING
extern s32 gSfxBank;
#endif
/* .bss, defined here (paired u64 stores share one lui) */
u64 gSeqDoneMode; /* loop mask used when the sequence finishes */
u64 gSeqAbortMode; /* loop mask used when it is aborted */
u8 gSeqDoneLevel;  /* level to go to when finished */
u8 gSeqAbortLevel;  /* level to go to when aborted */
u8 gSeqStartLevel;  /* level the sequence starts in */
u8 gSeqNumber;  /* current sequence number */
u8 gSeqSkippable;


#define LEVEL_DONE(l) ((players[playerNumber].medal[l] > 0 && players[playerNumber].medal[l] < 6) ? 1 : 0)

/* "prepare sequence %d" (0x802995F0).
 * Set up level sequence seq: its start level, exit levels and loop masks.
 * gSeqStartLevel is read back through ugen's store cache here, so the rank
 * index is not folded into the address. */
void seq_prepare(s32 seq) {
    gSeqNumber = seq;
    debug_printf("prepare sequence %d\n", seq);
    switch (gSeqNumber) {
        case 1:
            gSeqStartLevel = 0x26;
            gSeqAbortLevel = 0x28;
            gSeqDoneLevel = 0x28;
            gSeqDoneMode = 0x4000;
            gSeqAbortMode = 0x4000;
            gSeqSkippable = 1;
            break;
        case 3:
            gSeqStartLevel = 0x26;
            gSeqAbortLevel = 0x2B;
            gSeqDoneLevel = 0x2B;
            gSeqDoneMode = 0x4000;
            gSeqAbortMode = 0x4000;
            gSeqSkippable = 1;
            break;
        case 0:
            gSeqStartLevel = 0x31;
            gSeqAbortLevel = 0x32;
            gSeqDoneLevel = 0x32;
            gSeqDoneMode = 0x4000;
            gSeqAbortMode = 0x4000;
            gSeqSkippable = 0;
            break;
        case 4:
            gSeqStartLevel = 0x2F;
            gSeqAbortLevel = 0;
            gSeqDoneLevel = 0;
            gSeqDoneMode = 0x4000;
            gSeqAbortMode = 0x4000;
            gSeqSkippable = 1;
            break;
        case 5:
            gSeqStartLevel = 0x37;
            gSeqAbortLevel = 0x37;
            gSeqDoneLevel = 0x37;
            if (LEVEL_DONE(gSeqStartLevel)) {
                gSeqDoneMode = 0x80;
                gSeqAbortMode = 0x80;
            } else {
                gSeqDoneMode = 0x2000;
                gSeqAbortMode = 0x2000;
            }
            gSeqSkippable = 1;
            break;
        case 6:
            gSeqStartLevel = 0x1C;
            gSeqAbortLevel = 0x1C;
            gSeqDoneLevel = 0x1C;
            if (LEVEL_DONE(gSeqStartLevel)) {
                gSeqDoneMode = 0x80;
                gSeqAbortMode = 0x80;
            } else {
                gSeqDoneMode = 0x2000;
                gSeqAbortMode = 0x2000;
            }
            gSeqSkippable = 1;
            break;
        case 7:
            gSeqStartLevel = 0x35;
            gSeqAbortLevel = 0x35;
            gSeqDoneLevel = 0x35;
            if (LEVEL_DONE(gSeqStartLevel)) {
                gSeqDoneMode = 0x80;
                gSeqAbortMode = 0x80;
            } else {
                gSeqDoneMode = 0x2000;
                gSeqAbortMode = 0x2000;
            }
            gSeqSkippable = 1;
            break;
        case 8:
            gSeqStartLevel = 7;
            gSeqAbortLevel = 7;
            gSeqDoneLevel = 7;
            if (LEVEL_DONE(gSeqStartLevel)) {
                gSeqDoneMode = 0x80;
                gSeqAbortMode = 0x80;
            } else {
                gSeqDoneMode = 0x2000;
                gSeqAbortMode = 0x2000;
            }
            gSeqSkippable = 1;
            break;
        case 9:
            gSeqStartLevel = 0x13;
            gSeqAbortLevel = 0x13;
            gSeqDoneLevel = 0x13;
            if (LEVEL_DONE(gSeqStartLevel)) {
                gSeqDoneMode = 0x80;
                gSeqAbortMode = 0x80;
            } else {
                gSeqDoneMode = 0x2000;
                gSeqAbortMode = 0x2000;
            }
            gSeqSkippable = 1;
            break;
        default:
            debug_printf("unknown sequence number %d\n", gSeqNumber);
            break;
    }
}

/* Enters the sequence's start level (0x80299C0C).
 * Enter the sequence's first level */
void seq_enter_start_level(void) {
    levelno = gSeqStartLevel;
}

/* Starts the sequence level (demos, credits ...) (0x80299C20).
 * Per-level sequence start actions (cutscenes, jingles) */
void seq_start_level(void) {
    switch (levelno) {
        case 47:
            gRecDemoNumber = 0;
            gRecDemoNoLogo = 0;
            rec_start_playback(0, &levelno);
            break;
        case 55:
            gRecDemoNumber = 9;
            gRecDemoNoLogo = 0;
            rec_start_playback(9, &levelno);
            gDiggerVehicle = VEH_BACKLASH;
            yoshi_demand(YOSHI_DEMAND_OPEN | YW_DEMO_BACKLASH);
            hud_load_pad_image();
            break;
        case 28:
            gRecDemoNumber = 10;
            gRecDemoNoLogo = 0;
            rec_start_playback(10, &levelno);
            gDiggerVehicle = VEH_SIDESWIPE;
            yoshi_demand(YOSHI_DEMAND_OPEN | YW_DEMO_SIDESWIPE);
            hud_load_pad_image();
            break;
        case 53:
            gRecDemoNumber = 11;
            gRecDemoNoLogo = 0;
            rec_start_playback(11, &levelno);
            gDiggerVehicle = VEH_THUNDERFIST;
            yoshi_demand(YOSHI_DEMAND_OPEN | YW_DEMO_THUNDERFIST);
            hud_load_pad_image();
            break;
        case 7:
            gRecDemoNumber = 12;
            gRecDemoNoLogo = 0;
            rec_start_playback(12, &levelno);
            gDiggerVehicle = VEH_SKYFALL;
            yoshi_demand(YOSHI_DEMAND_OPEN | YW_DEMO_SKYFALL);
            hud_load_pad_image();
            break;
        case 19:
            gRecDemoNumber = 13;
            gRecDemoNoLogo = 0;
            rec_start_playback(13, &levelno);
            gDiggerVehicle = VEH_JBOMB;
            yoshi_demand(YOSHI_DEMAND_OPEN | YW_DEMO_JBOMB);
            hud_load_pad_image();
            break;
        case 49:
            credits_init();
            break;
        case 38:
            rec_start_replay();
            break;
    }
}

/* "finishing sequence and going to level" (0x80299E10).
 * Finish the current sequence level: record completion, pick the next level */
void seq_finish(s32 arg0) {
    snd_stop_all();
    switch (levelno) {
        case 38:
        case 47:
        case 49:
            players[playerNumber].medal[levelno] = 5;
            break;
        case 55:
            players[playerNumber].demosSeen |= 1;
            break;
        case 28:
            players[playerNumber].demosSeen |= 2;
            break;
        case 53:
            players[playerNumber].demosSeen |= 4;
            break;
        case 7:
            players[playerNumber].demosSeen |= 8;
            break;
        case 19:
            players[playerNumber].demosSeen |= 0x10;
            break;
    }
    if (arg0) {
        snd_play(gSfxBank, SFX_SELECT, 0);
        gNextGameMode = gSeqDoneMode;
        levelno = gSeqDoneLevel;
    } else {
        gNextGameMode = gSeqAbortMode;
        levelno = gSeqAbortLevel;
    }
    debug_printf("finishing sequence and going to level %d\n", levelno);
}

/* "get loop done for world %d" (0x80299FE8).
 * Return the loop mask for a level, starting its sequence where it has one */
u64 seq_get_loop_done(u8 level) {
    u64 mask;

    switch (level) {
        case 38:
            mask = 0x100000000000;
            seq_prepare(1);
            break;
        case 55:
            mask = 0x100000000000;
            seq_prepare(5);
            break;
        case 28:
            mask = 0x100000000000;
            seq_prepare(6);
            break;
        case 53:
            mask = 0x100000000000;
            seq_prepare(7);
            break;
        case 7:
            mask = 0x100000000000;
            seq_prepare(8);
            break;
        case 19:
            mask = 0x100000000000;
            seq_prepare(9);
            break;
        default:
            mask = fe_player_loop_done_mode(level);
            break;
    }
    debug_printf("get loop done for world %d\n", lowest_set_bit(mask));
    return mask;
}
