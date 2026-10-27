/* libultra src/io/pfsinitpak.c, from decompals/ultralib (see Makefile UL_*).
 * Blast Corps' SDK predates ultralib's oldest supported version here:
 * - osPfsInitPak leaves the ID area to __osGetId (contpfs.c) instead of
 *   reading and repairing it itself;
 * - __osPfsGetStatus lives in this object and polls every channel through
 *   __osPfsRequestData/__osPfsGetInitData (pfsisplug.c), where
 *   pfsgetstatus.c later used per-channel helpers. */
#include "PRinternal/macros.h"
#include "PR/os_internal.h"
#include "PRinternal/controller.h"
#include "PRinternal/siint.h"

s32 osPfsInitPak(OSMesgQueue* queue, OSPfs* pfs, int channel) {
    s32 ret = 0;

    __osSiGetAccess();

    ret = __osPfsGetStatus(queue, channel);

    __osSiRelAccess();

    if (ret != 0) {
        return ret;
    }

    pfs->queue = queue;
    pfs->channel = channel;
    pfs->status = 0;

    ERRCK(__osGetId(pfs));

    ret = osPfsChecker(pfs);
    pfs->status |= PFS_INITIALIZED;

    return ret;
}

s32 __osPfsGetStatus(OSMesgQueue* queue, int channel) {
    s32 ret = 0;
    OSMesg dummy;
    u8 pattern;
    OSContStatus data[MAXCONTROLLERS];

    __osPfsRequestData(CONT_CMD_REQUEST_STATUS);

    ret = __osSiRawStartDma(OS_WRITE, &__osPfsPifRam);
    osRecvMesg(queue, &dummy, OS_MESG_BLOCK);

    ret = __osSiRawStartDma(OS_READ, &__osPfsPifRam);
    osRecvMesg(queue, &dummy, OS_MESG_BLOCK);

    __osPfsGetInitData(&pattern, data);

    if (((data[channel].status & CONT_CARD_ON) != 0) && ((data[channel].status & CONT_CARD_PULL) != 0)) {
        return PFS_ERR_NEW_PACK;
    } else if ((data[channel].errno != 0) || ((data[channel].status & CONT_CARD_ON) == 0)) {
        return PFS_ERR_NOPACK;
    } else if ((data[channel].status & CONT_ADDR_CRC_ER) != 0) {
        return PFS_ERR_CONTRFAIL;
    }

    return ret;
}
