/* libultra src/os/syncputchars.c, built from decompals/ultralib (see Makefile UL_*).
 * Blast Corps' SDK predates ultralib's oldest supported version here: its
 * rdbPacket header byte had a 2-bit type in the top bits (andi 3, sll 6)
 * and the 2-bit length in the bottom bits, so swap in that layout before
 * the source is included. */
#define rdbPacket __rdbPacket_ultralib
#include "PR/rdb.h"
#undef rdbPacket
typedef struct {
    unsigned type : 2;
    unsigned : 4;
    unsigned length : 2;
    char buf[3];
} rdbPacket;
#include "src/os/syncputchars.c"
