/* libultra src/io/devmgr.c, from decompals/ultralib (see Makefile UL_*).
 * Blast Corps' SDK predates ultralib's oldest supported version here: it
 * has no 64DD/EPI support, so __osDevMgrMain only handles plain PI DMA
 * reads/writes and loopback messages. The remaining code is ultralib's. */
#include "PR/os_internal.h"
#include "PR/rcp.h"
#include "PRinternal/piint.h"

void __osDevMgrMain(void* args) {
    OSIoMesg* mb;
    OSMesg em;
    OSMesg dummy;
    s32 ret;
    OSDevMgr* dm;

    dm = (OSDevMgr*)args;
    mb = NULL;
    ret = 0;

    while (TRUE) {
        osRecvMesg(dm->cmdQueue, (OSMesg)&mb, OS_MESG_BLOCK);

        switch (mb->hdr.type) {
            case OS_MESG_TYPE_DMAREAD:
                osRecvMesg(dm->acsQueue, &dummy, OS_MESG_BLOCK);
                ret = dm->dma(OS_READ, mb->devAddr, mb->dramAddr, mb->size);
                break;
            case OS_MESG_TYPE_DMAWRITE:
                osRecvMesg(dm->acsQueue, &dummy, OS_MESG_BLOCK);
                ret = dm->dma(OS_WRITE, mb->devAddr, mb->dramAddr, mb->size);
                break;
            case OS_MESG_TYPE_LOOPBACK:
                osSendMesg(mb->hdr.retQueue, mb, OS_MESG_NOBLOCK);
                ret = -1;
                break;
            default:
                ret = -1;
                break;
        }

        if (ret == 0) {
            osRecvMesg(dm->evtQueue, &em, OS_MESG_BLOCK);
            osSendMesg(mb->hdr.retQueue, mb, OS_MESG_NOBLOCK);
            osSendMesg(dm->acsQueue, NULL, OS_MESG_NOBLOCK);
        }
    }
}
