#include "common.h"
#include <ultra64.h>
#include "game/game.h"

/* __osSetFpcCsr: libultra's hand-written os/setfpccsr.s (cfc1/ctc1 $31).
 * Permanently GLOBAL_ASM. */
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/971F0/__osSetFpcCsr.s")
