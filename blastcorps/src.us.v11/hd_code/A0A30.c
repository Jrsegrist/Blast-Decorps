#include "common.h"
#include <ultra64.h>
#include "game/game.h"

/* __osGetCause: raw COP0 Cause-register read (mfc0 $13), libultra's
 * hand-written os/getcause.s - not expressible in plain C. Permanently
 * GLOBAL_ASM. */
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/A0A30/__osGetCause.s")
