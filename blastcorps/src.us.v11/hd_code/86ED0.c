#include "common.h"
#include <ultra64.h>
#include "game/game.h"

/* FILE-WIDE FINDING: this file's functions save $ra via the 64-bit `sd`/`ld`
 * doubleword form, not the normal 32-bit `sw`/`lw` pair - the same signature
 * documented in full in hd_code/77E20.c's top-of-file note and the project
 * skill file, confirming (with no exceptions found) that this file is part
 * of the same large hand-written-assembly module. Any pragma below without
 * a more specific comment follows this convention; a few have their own
 * more specific non-ABI explanation where one was already worked out. */
/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING

/* Shared by eight vehicle modules, each calling it with its own state block
 * in $gp (the C takes it as a parameter; see tools_port/conventions.txt).
 * Plays sound 0x55 unless the next game mode is 0x40 (a full 64-bit
 * compare), sets the vehicle's speed (s16 at +0x76) to 1.5 * gCamTiltMax
 * (gCamTiltMax + (gCamTiltMax >> 1), truncated to 16 bits), clears
 * gTurboPending, calls mb_start(6, 0.25f, 100) and sets gTurboActive = 1.
 * The asm returns with a0 = 1 and a1-a3 as mb_start left them; its asm
 * callers go on to calls that don't read them. */
void veh_start_turbo(u8 *state) {
    s32 v;

    if (gNextGameMode != MODE_REPLAY) {
        snd_play(gSfxBank, SFX_TURBO, NULL);
    }
    v = gCamTiltMax;
    *(s16 *) (state + 0x76) = v + (v >> 1);
    gTurboPending = 0;
    mb_start(6, 0.25f, 100);
    gTurboActive = 1;
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/86ED0/veh_start_turbo.s")
#endif
