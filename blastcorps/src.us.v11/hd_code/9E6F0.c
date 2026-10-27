#include "common.h"
#include <ultra64.h>
#include "game/game.h"

/* __osSetCompare: raw COP0 Compare-register write (mtc0 $11, i.e.
 * osSetCompare()/osSetTimer()-style) - genuine hand-written SDK leaf
 * stub, not expressible in plain C. Permanently GLOBAL_ASM. */
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/9E6F0/__osSetCompare.s")
