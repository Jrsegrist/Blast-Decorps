#include "common.h"
#include <ultra64.h>
#include "game/game.h"

/* osSetIntMask: libultra's hand-written os/setintmask.s (COP0 Status and
 * MI mask writes). Permanently GLOBAL_ASM. */
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/92EA0/osSetIntMask.s")
