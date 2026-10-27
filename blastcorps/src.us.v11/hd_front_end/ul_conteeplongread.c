/* libultra src/io/conteeplongread.c, from decompals/ultralib (see Makefile UL_*).
 * Blast Corps' SDK predates ultralib's oldest supported version here:
 * - an address past EEPROM_MAXBLOCKS returns -1 up front;
 * - every block read is followed by the same 12ms timer wait as
 *   osEepromLongWrite, with OS_USEC_TO_CYCLES scaled by the run-time
 *   osClockRate.
 * Otherwise this is ultralib's source. */
#include "PR/os_internal.h"
#include "PRinternal/controller.h"

#undef OS_USEC_TO_CYCLES
#define OS_USEC_TO_CYCLES(n) (((u64)(n) * osClockRate) / 1000000LL)

s32 osEepromLongRead(OSMesgQueue* mq, u8 address, u8* buffer, int length) {
    s32 ret = 0;

    if (address > EEPROM_MAXBLOCKS) {
        return -1;
    }

    while (length > 0) {
        ERRCK(osEepromRead(mq, address, buffer));
        length -= EEPROM_BLOCK_SIZE;
        address++;
        buffer += EEPROM_BLOCK_SIZE;
        osSetTimer(&__osEepromTimer, OS_USEC_TO_CYCLES(12000), 0, &__osEepromTimerQ, &__osEepromTimerMsg);
        osRecvMesg(&__osEepromTimerQ, NULL, OS_MESG_BLOCK);
    }

    return ret;
}
