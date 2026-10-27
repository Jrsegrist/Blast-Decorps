/* libultra src/io/conteepread.c, from decompals/ultralib (see Makefile UL_*).
 * Blast Corps' SDK predates ultralib's oldest supported version here:
 * - osEepromRead checks the address before taking the SI, and accepts only
 *   a 4K EEPROM (sdata.type == CONT_EEPROM); a status error returns
 *   CONT_NO_RESPONSE_ERROR without releasing the SI;
 * - PIF RAM is filled with 0xFF and pifstatus cleared before the read DMA;
 * - __osPackEepReadData fills all 16 PIF RAM words (pifstatus included).
 * Otherwise this is ultralib's source. */
#include "PRinternal/macros.h"
#include "PR/rcp.h"
#include "PRinternal/controller.h"
#include "PRinternal/siint.h"

OSPifRam __osEepPifRam;
static void __osPackEepReadData(u8 address);

s32 osEepromRead(OSMesgQueue* mq, u8 address, u8* buffer) {
    s32 ret = 0;
    int i = 0;
    u8* ptr = (u8*)&__osEepPifRam.ramarray;
    OSContStatus sdata;
    __OSContEepromFormat eepromformat;

    if (address > EEPROM_MAXBLOCKS) {
        return -1;
    }

    __osSiGetAccess();
    ret = __osEepStatus(mq, &sdata);

    if (ret != 0 || sdata.type != CONT_EEPROM) {
        return CONT_NO_RESPONSE_ERROR;
    }

    while (sdata.status & CONT_EEPROM_BUSY) {
        __osEepStatus(mq, &sdata);
    }

    __osPackEepReadData(address);
    ret = __osSiRawStartDma(OS_WRITE, &__osEepPifRam); // send command to pif
    osRecvMesg(mq, NULL, OS_MESG_BLOCK);

    for (i = 0; i < ARRLEN(__osEepPifRam.ramarray) + 1; i++) {
        __osEepPifRam.ramarray[i] = CONT_CMD_NOP;
    }

    __osEepPifRam.pifstatus = 0;
    ret = __osSiRawStartDma(OS_READ, &__osEepPifRam); // recv response
    __osContLastCmd = CONT_CMD_READ_EEPROM;
    osRecvMesg(mq, NULL, OS_MESG_BLOCK);

    for (i = 0; i < MAXCONTROLLERS; i++) {
        // skip the first 4 bytes
        ptr++;
    }

    eepromformat = *(__OSContEepromFormat*)ptr;
    ret = CHNL_ERR(eepromformat);

    if (ret == 0) {
        for (i = 0; i < ARRLEN(eepromformat.data); i++) {
            *buffer++ = eepromformat.data[i];
        }
    }
    __osSiRelAccess();
    return ret;
}

static void __osPackEepReadData(u8 address) {
    u8* ptr = (u8*)&__osEepPifRam.ramarray;
    __OSContEepromFormat eepromformat;
    int i;

    for (i = 0; i < ARRLEN(__osEepPifRam.ramarray) + 1; i++) {
        __osEepPifRam.ramarray[i] = CONT_CMD_NOP;
    }

    __osEepPifRam.pifstatus = CONT_CMD_EXE;

    eepromformat.txsize = CONT_CMD_READ_EEPROM_TX;
    eepromformat.rxsize = CONT_CMD_READ_EEPROM_RX;
    eepromformat.cmd = CONT_CMD_READ_EEPROM;
    eepromformat.address = address;

    for (i = 0; i < ARRLEN(eepromformat.data); i++) {
        eepromformat.data[i] = 0;
    }

    for (i = 0; i < MAXCONTROLLERS; i++) {
        *ptr++ = 0;
    }

    *(__OSContEepromFormat*)(ptr) = eepromformat;
    ptr += sizeof(__OSContEepromFormat);
    ptr[0] = CONT_CMD_END;
}
