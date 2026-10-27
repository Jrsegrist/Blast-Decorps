#include "common.h"
#include <ultra64.h>
#include "game/game.h"

/* osWritebackDCacheAll: libultra's hand-written os/writebackdcacheall.s
 * (a `cache` loop). Permanently GLOBAL_ASM. */
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/91F50/osWritebackDCacheAll.s")
