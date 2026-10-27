#include "common.h"
#include <ultra64.h>
#include "game/game.h"

/* recording.c (from its assert strings): demo/attract-mode controller recording and playback */

typedef struct {
    u8 btnHi;
    u8 btnLo;
    u8 count;
    s8 stickX;
    s8 stickY;
} RecEntry;

extern s32 gRecCount;
extern s32 gRecIndex;
extern s32 gRecLength;
extern s32 gRecRunCount;
extern s32 gRecRunLeft;
extern u16 gRecLastButtons;
extern s8 gRecLastStickX;
extern s8 gRecLastStickY;
extern u8 gRecSkipFirst;
extern RecEntry * N64P gRecPlayback;
extern void * N64P gRecVehicleState;
extern RecEntry gRecBuffer[];
extern u16 gRecReplaySprIds[]; /* sprite ids (spr_load) */


#define ASSERT(EX, line) if (!(EX)) debug_printf("\n\a --- ASSERTION FAULT - %s - %s, line %d\n\n", #EX, "recording.c", line)

void rec_start_playback(s32 arg0, s32 *arg1) {
    s32 i;
    u8 *rom;
    s32 size;
    s32 len;
    u16 unk26;
    u16 unk24;
    u8 *p;

    rom = romDemoRecordings;
    size = romFeBgPicture1 - rom;
    p = gHeapPtr;
    hd_rom_load((u32) rom, (u32) p, (u32 *) &size, 9, 0, 1);
    gHeapPtr = (u8 *) gHeapPtr + size;
    align_up((s32 *) &gHeapPtr, 0x10);
    for (i = 0; i < arg0; i++) {
        len = *(s16 *) (p + 0x140C);
        p = p + len + 0x140E;
    }
    gRecDemoLength = *(u16 *) p;
    unk26 = *(u16 *) (p + 2);
    gRecLength = *(u16 *) (p + 8);
    *arg1 = p[0xA];
    gRecVehicle = p[0xB];
    gRecPlayback = (RecEntry *) (p + 0xC);
    len = *(s16 *) (p + 0x140C);
    p = p + 0x140E;
    align_up((s32 *) ((void **) &p), 2);
    gRecVehicleState = p;
    gRecIndex = 0;
    gRecRunLeft = gRecPlayback->count;
    gRecVehiclePending = 1;
}

/* Starts recording the player's pad: empties the RAM recording and skips the first rec_record call
 * (0x8025BB38). */
void rec_start_recording(void) {
    gRecCount = 0;
    gRecSkipFirst = 1;
}

/* Starts playing back the RAM recording just made (the post-level replay) and loads the replay sprites
 * (0x8025BB50). */
void rec_start_replay(void) {
    gRecLength = gRecCount;
    gRecIndex = 0;
    gRecPlayback = gRecBuffer;
    gRecReplaySprite = spr_load((u16 *) gRecReplaySprIds, 0, 1, 1, 1, 1.0f);
    gRecRunLeft = gRecPlayback[gRecIndex].count;
}

void rec_record(u16 buttons, s8 x, s8 y) {
    if (gRecSkipFirst == 0) {
        if (buttons != gRecLastButtons || x != gRecLastStickX || y != gRecLastStickY || gRecRunCount == 0xFF || gNextGameMode != 0) {
            if (gRecCount < 0x400) {
                gRecBuffer[gRecCount].btnHi = gRecLastButtons >> 8;
                gRecBuffer[gRecCount].btnLo = gRecLastButtons & 0xFF;
                gRecBuffer[gRecCount].stickX = gRecLastStickX;
                gRecBuffer[gRecCount].stickY = gRecLastStickY;
                gRecBuffer[gRecCount].count = gRecRunCount;
                gRecCount++;
                gRecRunCount = 0;
            }
        } else {
            gRecRunCount++;
        }
    } else {
        gRecRunCount = 0;
    }
    gRecLastButtons = buttons;
    gRecLastStickX = x;
    gRecLastStickY = y;
    gRecSkipFirst = 0;
}

/* At level setup after rec_start_playback: loads the recording's saved vehicle state, enters that vehicle and
 * makes it the current one (0x8025BD98). */
void rec_restore_start_vehicle(void) {
    switch (gRecVehicle) {
        case 3:
            skyfall_load_state(gRecVehicleState);
            skyfall_enter();
            break;
        case 0:
            driver_load_state(gRecVehicleState);
            break;
        case 1:
            sideswipe_load_state(gRecVehicleState);
            sideswipe_enter();
            break;
        case 2:
            thunderfist_load_state(gRecVehicleState);
            thunderfist_enter();
            break;
        case 16:
            cyclone_load_state(gRecVehicleState);
            cyclone_enter();
            break;
        case 4:
            ramdozer_load_state(gRecVehicleState);
            ramdozer_enter();
            break;
        case 5:
            backlash_load_state(gRecVehicleState);
            backlash_enter();
            break;
        case 9:
            jbomb_load_state(gRecVehicleState);
            jbomb_enter();
            break;
        case 10:
            ballista_load_state(gRecVehicleState);
            ballista_enter();
            break;
    }
    if (gRecVehicle) {
        gDriverHidden = 1;
    }
    gCurVehicle = gRecVehicle;
    gRecVehiclePending = 0;
}

void rec_next(void) {
    s32 pad[2];

    if (gRecIndex > gRecLength || (gHaveCarrier && gCarrierHit && (gGameMode & (MODE_REPLAY | MODE_REPLAY_CARRIER_END)))) {
        if (yoshiState != 1 || ((gGameMode & (MODE_REPLAY | MODE_REPLAY_CARRIER_END)) && !gReplayMenuShown)) {
            if (!gPaused) {
                gPauseRequest = 1;
            }
        } else if (!areWeFading()) {
            switch (gGameMode) {
                case MODE_ATTRACT:
                    fade_to_mode_speed(MODE_ATTRACT, 0.25f);
                    break;
                case MODE_GLOBE_SEQ:
                    debug_printf("sequence playback over\n");
                    fade_to_mode(MODE_SEQ_END);
                    break;
                case MODE_REPLAY:
                case MODE_REPLAY_CARRIER_END:
                    gPauseRequest = 1;
                    if (gGameMode == MODE_REPLAY_CARRIER_END) {
                        fade_to_mode_speed(MODE_REPLAY, 1.25f);
                    } else {
                        fade_to_mode_speed(MODE_REPLAY, 0.5f);
                    }
                    break;
                default:
                    ASSERT(1==0, 388);
                    break;
            }
        }
    } else {
        gRecButtons = (gRecPlayback[gRecIndex].btnHi << 8) + gRecPlayback[gRecIndex].btnLo;
        gRecStickX = gRecPlayback[gRecIndex].stickX;
        gRecStickY = gRecPlayback[gRecIndex].stickY;
        if (--gRecRunLeft < 0) {
            gRecIndex++;
            gRecRunLeft = gRecPlayback[gRecIndex].count;
        }
        /* meant to swap a lone A and a lone B, but the second test sees the first's result: a lone A
         * stays A and a lone B becomes A */
        if ((gGameMode & (MODE_ATTRACT | MODE_GLOBE_SEQ)) && gRecVehicle != VEH_JBOMB) {
            if ((gRecButtons & (A_BUTTON | B_BUTTON)) == A_BUTTON) {
                gRecButtons &= ~A_BUTTON;
                gRecButtons |= B_BUTTON;
            }
            if ((gRecButtons & (A_BUTTON | B_BUTTON)) == B_BUTTON) {
                gRecButtons &= ~B_BUTTON;
                gRecButtons |= A_BUTTON;
            }
        }
    }
}
