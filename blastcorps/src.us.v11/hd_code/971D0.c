#include "common.h"
#include <ultra64.h>
#include "game/game.h"

/* __osSetSR/__osGetSR: libultra's hand-written os/setsr.s and os/getsr.s
 * (mtc0/mfc0 $12). Permanently GLOBAL_ASM. */
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/971D0/__osSetSR.s")

#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/971D0/__osGetSR.s")
