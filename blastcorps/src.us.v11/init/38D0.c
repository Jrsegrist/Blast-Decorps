#include "common.h"
#include <ultra64.h>

/*
 * TLB-probe fallback used by osVirtualToPhysical for addresses outside
 * KSEG0/KSEG1 (tlbp/tlbr aren't expressible in C).
 */
#pragma GLOBAL_ASM("asm/nonmatchings/init/38D0/__osProbeTLB.s")
