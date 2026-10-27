#include "common.h"
#include <ultra64.h>
#include "game/game.h"

/* sqrtf: `jr $ra; sqrt.s $f0,$f12`, libultra's hand-written gu/sqrtf.s
 * (ultralib has it as assembly too). Permanently GLOBAL_ASM. */
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/91820/sqrtf.s")
