/* libultra src/gu/lookatref.c, built from decompals/ultralib (see Makefile UL_*).
 * Blast Corps' SDK predates ultralib's oldest supported version here:
 * its FTOFRAC8 scaled and clamped in double precision (mul.d by 128.0,
 * compare with a 127.0 .rodata double), so redefine it before the source
 * is included. */
#include "PR/gu.h"
#undef FTOFRAC8
#define FTOFRAC8(x) ((int) MIN(((x) * (128.0)), 127.0) & 0xff)
#include "src/gu/lookatref.c"
