/* libultra src/io/aisetnextbuf.c, from decompals/ultralib (see Makefile UL_*).
 * Blast Corps' SDK predates ultralib's oldest supported version here: its
 * hardware-bug check tests the buffer end against 0x2000 modulo 0x4000
 * (andi 0x3fff, compare with 0x2000) rather than for 8K alignment.
 * Otherwise this is ultralib's (pre-2.0J) source. */
#include "PR/os_internal.h"
#include "PR/ultraerror.h"
#include "PR/rcp.h"
#include "PRinternal/osint.h"

s32 osAiSetNextBuffer(void* bufPtr, u32 size) {
    static u8 hdwrBugFlag = FALSE;
    char* bptr;

    bptr = bufPtr;

    if (hdwrBugFlag) {
        bptr = (u8*)bufPtr - 0x2000;
    }

    if ((((u32)bufPtr + size) & 0x3fff) == 0x2000) {
        hdwrBugFlag = TRUE;
    } else {
        hdwrBugFlag = FALSE;
    }

    if (__osAiDeviceBusy()) {
        return -1;
    }

    IO_WRITE(AI_DRAM_ADDR_REG, osVirtualToPhysical(bptr));
    IO_WRITE(AI_LEN_REG, size);
    return 0;
}
