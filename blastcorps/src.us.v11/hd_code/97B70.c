#include "common.h"
#include <ultra64.h>
#include "game/game.h"

/* __osDisableInt/__osRestoreInt: raw COP0 Status-register read-modify-write
 * (`mfc0`/`mtc0 $12`, clearing or setting a bit directly) - same hand-
 * written COP0-leaf-stub family as init's __osSetSR/__osGetSR.
 * Permanently GLOBAL_ASM. */
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/97B70/__osDisableInt.s")

#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/97B70/__osRestoreInt.s")
