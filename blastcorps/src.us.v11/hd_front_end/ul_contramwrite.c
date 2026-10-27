/* libultra src/io/contramwrite.c, from decompals/ultralib (see Makefile UL_*),
 * in its pre-2.0J form (separate __osPackRamWriteData).
 * Blast Corps' SDK predates ultralib's oldest supported version here:
 * - __osContRamWrite fills all 16 PIF RAM words with 0xFF and clears
 *   pifstatus before each read DMA;
 * - a channel error is returned as is (no PFS_ERR_NOPACK);
 * - __osPackRamWriteData clears all 16 words, pifstatus included, first.
 * Otherwise this is ultralib's source. */
#include "PRinternal/macros.h"
#include "PR/os_internal.h"
#include "PR/rcp.h"
#include "PRinternal/controller.h"
#include "PRinternal/siint.h"

#define READFORMAT(ptr) ((__OSContRamReadFormat*)(ptr))

static void __osPackRamWriteData(int channel, u16 address, u8* buffer);

#if defined(NON_MATCHING) && defined(PORT_HOST)
/* port/src/platform/save.c: the block as the record holds it now (a save routine hands the Pfs code the
 * record's big-endian copy; on the N64 the record itself, read block by block as the transfer goes) */
void port_save_refresh(const void* block, int n);
#endif

s32 __osContRamWrite(OSMesgQueue* mq, int channel, u16 address, u8* buffer, int force) {
    s32 ret = 0;
    s32 i;
    u8* ptr = (u8*)&__osPfsPifRam;
    __OSContRamReadFormat ramreadformat;
    s32 retry = 2;

    if ((force != TRUE) && (address < PFS_LABEL_AREA) && (address != 0)) {
        return 0;
    }

    __osSiGetAccess();
    __osContLastCmd = CONT_CMD_WRITE_PAK;
#if defined(NON_MATCHING) && defined(PORT_HOST)
    port_save_refresh(buffer, BLOCKSIZE);
#endif
    __osPackRamWriteData(channel, address, buffer);
    ret = __osSiRawStartDma(OS_WRITE, &__osPfsPifRam);
    osRecvMesg(mq, NULL, OS_MESG_BLOCK);

    do {
        for (i = 0; i < ARRLEN(__osPfsPifRam.ramarray) + 1; i++) {
            __osPfsPifRam.ramarray[i] = 0xFF;
        }

        __osPfsPifRam.pifstatus = 0;
        ret = __osSiRawStartDma(OS_READ, &__osPfsPifRam);
        osRecvMesg(mq, NULL, OS_MESG_BLOCK);
        ptr = (u8*)&__osPfsPifRam;

        if (channel != 0) {
            for (i = 0; i < channel; i++) {
                ptr++;
            }
        }

        ramreadformat = *READFORMAT(ptr);

        ret = CHNL_ERR(ramreadformat);
        if (ret == 0) {
            if (__osContDataCrc(buffer) != ramreadformat.datacrc) {
                ret = __osPfsGetStatus(mq, channel);

                if (ret != 0) {
                    __osSiRelAccess();
                    return ret;
                }

                ret = PFS_ERR_CONTRFAIL;
            }
        }
    } while ((ret == PFS_ERR_CONTRFAIL) && retry-- >= 0);

    __osSiRelAccess();

    return ret;
}

static void __osPackRamWriteData(int channel, u16 address, u8* buffer) {
    u8* ptr;
    __OSContRamReadFormat ramreadformat;
    int i;

    ptr = (u8*)__osPfsPifRam.ramarray;

    for (i = 0; i < ARRLEN(__osPfsPifRam.ramarray) + 1; i++) {
        __osPfsPifRam.ramarray[i] = 0;
    }

    __osPfsPifRam.pifstatus = CONT_CMD_EXE;
    ramreadformat.dummy = CONT_CMD_NOP;
    ramreadformat.txsize = CONT_CMD_WRITE_PAK_TX;
    ramreadformat.rxsize = CONT_CMD_WRITE_PAK_RX;
    ramreadformat.cmd = CONT_CMD_WRITE_PAK;
    ramreadformat.address = (address << 0x5) | __osContAddressCrc(address);
    ramreadformat.datacrc = CONT_CMD_NOP;

    for (i = 0; i < ARRLEN(ramreadformat.data); i++) {
        ramreadformat.data[i] = *buffer++;
    }

    if (channel != 0) {
        for (i = 0; i < channel; i++) {
            *ptr++ = CONT_CMD_REQUEST_STATUS;
        }
    }

    *(__OSContRamReadFormat*)ptr = ramreadformat;
    ptr += sizeof(__OSContRamReadFormat);
    ptr[0] = CONT_CMD_END;
}
