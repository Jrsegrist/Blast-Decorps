/* libultra src/io/conteeplongwrite.c, from decompals/ultralib (see Makefile UL_*).
 * Blast Corps' SDK predates ultralib's oldest supported version here:
 * - an address past EEPROM_MAXBLOCKS returns -1 up front;
 * - OS_USEC_TO_CYCLES scales by the run-time osClockRate (an __ll_mul and
 *   an __ull_div) rather than a constant.
 * Otherwise this is ultralib's source. */
#include "PR/os_internal.h"
#include "PRinternal/controller.h"

#undef OS_USEC_TO_CYCLES
#define OS_USEC_TO_CYCLES(n) (((u64)(n) * osClockRate) / 1000000LL)

s32 osEepromLongWrite(OSMesgQueue* mq, u8 address, u8* buffer, int length) {
    s32 ret = 0;

    if (address > EEPROM_MAXBLOCKS) {
        return -1;
    }

    while (length > 0) {
        ERRCK(osEepromWrite(mq, address, buffer));
        length -= EEPROM_BLOCK_SIZE;
        address++;
        buffer += EEPROM_BLOCK_SIZE;
        osSetTimer(&__osEepromTimer, OS_USEC_TO_CYCLES(12000), 0, &__osEepromTimerQ, &__osEepromTimerMsg);
        osRecvMesg(&__osEepromTimerQ, NULL, OS_MESG_BLOCK);
    }

    return ret;
}
