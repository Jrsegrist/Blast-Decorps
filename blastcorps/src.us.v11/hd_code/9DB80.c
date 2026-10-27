#include "common.h"
#include <ultra64.h>
#include "game/game.h"

/* bcopy: libultra's hand-written libc/bcopy.s. Permanently GLOBAL_ASM. */
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/9DB80/bcopy.s")

void __osSpSetStatus(u32 arg0) {
    *(volatile u32 *) 0xA4040010 = arg0;
}
