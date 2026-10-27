#include "common.h"
#include <ultra64.h>

/*
 * ROM entry point. IPL3 jumps here directly. Zeroes bss, then switches to
 * the real stack and jumps into the next-stage boot code - inherently
 * irreducible to C since it reassigns $sp and computes its own jump target.
 */
#pragma GLOBAL_ASM("asm/nonmatchings/init/0000/entry.s")
