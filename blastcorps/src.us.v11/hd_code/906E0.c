#include "common.h"
#include <ultra64.h>
#include "game/game.h"

/* osWritebackDCache: libultra's hand-written os/writebackdcache.s (`cache`
 * ops). Permanently GLOBAL_ASM. */
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/906E0/osWritebackDCache.s")
