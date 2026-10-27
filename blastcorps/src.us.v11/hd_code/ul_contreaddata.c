/* libultra src/io/contreaddata.c, from decompals/ultralib (see Makefile UL_*).
 * Blast Corps' SDK predates ultralib's oldest supported version here:
 * - osContStartReadData fills all 16 PIF RAM words with 0xFF and clears
 *   pifstatus before the read DMA;
 * - __osPackReadData clears all 16 words, pifstatus included.
 * Otherwise this is ultralib's source. */
#include "PRinternal/macros.h"
#include "PR/os_internal.h"
#include "PRinternal/controller.h"
#include "PRinternal/siint.h"

static void __osPackReadData(void);

s32 osContStartReadData(OSMesgQueue* mq) {
    s32 ret = 0;
    s32 i;

    __osSiGetAccess();

    if (__osContLastCmd != CONT_CMD_READ_BUTTON) {
        __osPackReadData();
        ret = __osSiRawStartDma(OS_WRITE, __osContPifRam.ramarray);
        osRecvMesg(mq, NULL, OS_MESG_BLOCK);
    }

    for (i = 0; i < ARRLEN(__osContPifRam.ramarray) + 1; i++) {
        __osContPifRam.ramarray[i] = 0xFF;
    }

    __osContPifRam.pifstatus = 0;
    ret = __osSiRawStartDma(OS_READ, __osContPifRam.ramarray);
    __osContLastCmd = CONT_CMD_READ_BUTTON;
    __osSiRelAccess();

    return ret;
}

void osContGetReadData(OSContPad* data) {
    u8* ptr = (u8*)__osContPifRam.ramarray;
    __OSContReadFormat readformat;
    int i;

    for (i = 0; i < __osMaxControllers; i++, ptr += sizeof(__OSContReadFormat), data++) {
        readformat = *(__OSContReadFormat*)ptr;
        data->errno = CHNL_ERR(readformat);

        if (data->errno != 0) {
            continue;
        }

        data->button = readformat.button;
        data->stick_x = readformat.stick_x;
        data->stick_y = readformat.stick_y;
    }
}

static void __osPackReadData(void) {
    u8* ptr = (u8*)__osContPifRam.ramarray;
    __OSContReadFormat readformat;
    int i;

    for (i = 0; i < ARRLEN(__osContPifRam.ramarray) + 1; i++) {
        __osContPifRam.ramarray[i] = 0;
    }

    __osContPifRam.pifstatus = CONT_CMD_EXE;
    readformat.dummy = CONT_CMD_NOP;
    readformat.txsize = CONT_CMD_READ_BUTTON_TX;
    readformat.rxsize = CONT_CMD_READ_BUTTON_RX;
    readformat.cmd = CONT_CMD_READ_BUTTON;
    readformat.button = 0xFFFF;
    readformat.stick_x = -1;
    readformat.stick_y = -1;

    for (i = 0; i < __osMaxControllers; i++) {
        *(__OSContReadFormat*)ptr = readformat;
        ptr += sizeof(__OSContReadFormat);
    }

    *ptr = CONT_CMD_END;
}
