/* libultra src/libc/ldiv.c, built from decompals/ultralib (see Makefile UL_*).
 * IDO -O3 emits functions in reverse source order, so lldiv comes first.
 * ldiv runs past the end of the last code subsegment (0xA0BA0). */
#include "src/libc/ldiv.c"
