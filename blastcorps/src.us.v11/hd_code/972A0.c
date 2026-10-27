#include "common.h"
#include <ultra64.h>
#include "game/game.h"

/* libultra's hand-written os/exceptasm.s (__osExceptionPreamble through
 * __osCleanupThread: $k0/$k1 use, eret, sd/ld context saves) followed by
 * os/maptlbrdb.s (osMapTLBRdb: TLB writes). None of it comes from C;
 * permanently GLOBAL_ASM. send_mesg is a local label in exceptasm. */
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/972A0/__osExceptionPreamble.s")

#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/972A0/__osException.s")

#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/972A0/send_mesg.s")

#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/972A0/__osEnqueueAndYield.s")

#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/972A0/__osEnqueueThread.s")

#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/972A0/__osPopThread.s")

#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/972A0/__osDispatchThread.s")

#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/972A0/__osCleanupThread.s")

#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/972A0/osMapTLBRdb.s")
