#include "common.h"

/*
 * IDO's bundled libkmc bzero (declared in IRIX's bstring.h), not SDK/game source -
 * SGI ships this precompiled, so there's no original C to recover here.
 */
#pragma GLOBAL_ASM("asm/nonmatchings/init/1EA0/bzero.s")
