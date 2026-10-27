/* libultra src/gu/mtxutil.c, built from decompals/ultralib (see Makefile UL_*).
 * Blast Corps' SDK predates ultralib's oldest supported version here:
 * its FIX32TOF divided by 65536.0 (guMtxL2F's div.s) rather than
 * multiplying by 1/65536, so redefine it before the source is included. */
#include "PR/gu.h"
#undef FIX32TOF
#define FIX32TOF(x) ((float) (x) / (float) 0x00010000)
#include "src/gu/mtxutil.c"
