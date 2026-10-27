/* libultra src/io/conteepprobe.c, from decompals/ultralib (see Makefile UL_*).
 * Blast Corps' SDK predates ultralib's oldest supported version here: there
 * is no 16K EEPROM, so the probe only tests the CONT_EEPROM type bit and
 * returns 1 or 0. */
#include "PR/os_internal.h"
#include "PRinternal/controller.h"
#include "PRinternal/siint.h"

s32 osEepromProbe(OSMesgQueue* mq) {
    s32 ret = 0;
    OSContStatus sdata;

    __osSiGetAccess();
    ret = __osEepStatus(mq, &sdata);

    if (ret == 0 && (sdata.type & CONT_EEPROM) != 0) {
        ret = 1;
    } else {
        ret = 0;
    }

    __osSiRelAccess();
    return ret;
}
