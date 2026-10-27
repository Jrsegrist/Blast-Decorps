#include "common.h"
#include <ultra64.h>
#include "game/game.h"

/* osGetCount: raw COP0 Count-register read (mfc0 $9) - genuine
 * hand-written SDK leaf stub (osGetCount()-style), not expressible in
 * plain C. Permanently GLOBAL_ASM. */
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/95060/osGetCount.s")
