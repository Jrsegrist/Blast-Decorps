#include "common.h"
#include <ultra64.h>

/*
 * Exception vector trampoline: jumps to the real handler at 0x80221040.
 * The universal shape of libultra's __osExceptionPreamble regardless of
 * what the handler itself turns out to be (see init/2340 - the handler
 * body here looks like a custom Rare implementation, not stock SDK).
 */
#pragma GLOBAL_ASM("asm/nonmatchings/init/2330/__osExceptionPreamble.s")
