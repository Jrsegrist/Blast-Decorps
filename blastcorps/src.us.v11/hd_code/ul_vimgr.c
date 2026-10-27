/* libultra src/io/vimgr.c, from decompals/ultralib (see Makefile UL_*).
 * Blast Corps' SDK predates ultralib's oldest supported version here: it
 * has no EPI layer, so osCreateViManager sets no edma handler, and its
 * OSIoMesg has no piHandle (0x14 bytes), which shifts the static messages
 * and the retrace counter in .bss. Otherwise this is ultralib's source. */
#include "PRinternal/macros.h"
#include "PR/os_internal.h"
#include "PR/ultraerror.h"
#include "PR/rcp.h"
#include "PRinternal/viint.h"
#include "PRinternal/osint.h"

/* OSIoMesg as this SDK has it: no piHandle */
typedef struct {
    OSIoMesgHdr hdr;
    void* dramAddr;
    u32 devAddr;
    u32 size;
} __OSIoMesgOld;

OSDevMgr __osViDevMgr = { 0 };
static OSThread viThread;
static STACK(viThreadStack, OS_VIM_STACKSIZE) ALIGNED(0x10);
static OSMesgQueue viEventQueue ALIGNED(0x8);
static OSMesg viEventBuf[5] ALIGNED(0x8);
static __OSIoMesgOld viRetraceMsg ALIGNED(0x8);
static __OSIoMesgOld viCounterMsg ALIGNED(0x8);

static void viMgrMain(void* arg);
void osCreateViManager(OSPri pri) {
    u32 savedMask;
    OSPri oldPri;
    OSPri myPri;

    if (__osViDevMgr.active) {
        return;
    }
    __osTimerServicesInit();
    osCreateMesgQueue(&viEventQueue, viEventBuf, ARRLEN(viEventBuf));
    viRetraceMsg.hdr.type = OS_MESG_TYPE_VRETRACE;
    viRetraceMsg.hdr.pri = OS_MESG_PRI_NORMAL;
    viRetraceMsg.hdr.retQueue = NULL;
    viCounterMsg.hdr.type = OS_MESG_TYPE_COUNTER;
    viCounterMsg.hdr.pri = OS_MESG_PRI_NORMAL;
    viCounterMsg.hdr.retQueue = NULL;
    osSetEventMesg(OS_EVENT_VI, &viEventQueue, &viRetraceMsg);
    osSetEventMesg(OS_EVENT_COUNTER, &viEventQueue, &viCounterMsg);
    oldPri = -1;
    myPri = osGetThreadPri(NULL);

    if (myPri < pri) {
        oldPri = myPri;
        osSetThreadPri(NULL, pri);
    }

    savedMask = __osDisableInt();
    __osViDevMgr.active = TRUE;
    __osViDevMgr.thread = &viThread;
    __osViDevMgr.cmdQueue = &viEventQueue;
    __osViDevMgr.evtQueue = &viEventQueue;
    __osViDevMgr.acsQueue = NULL;
    __osViDevMgr.dma = NULL;
    osCreateThread(&viThread, 0, viMgrMain, &__osViDevMgr, STACK_START(viThreadStack), pri);
    __osViInit();
    osStartThread(&viThread);
    __osRestoreInt(savedMask);

    if (oldPri != -1) {
        osSetThreadPri(NULL, oldPri);
    }
}

static void viMgrMain(void* arg) {
    __OSViContext* vc;
    OSDevMgr* dm;
    OSIoMesg* mb;
    static u16 retrace;
    s32 first;
    u32 count;

    mb = NULL;
    first = 0;
    vc = __osViGetCurrentContext();
    retrace = vc->retraceCount;
    if (retrace == 0) {
        retrace = 1;
    }
    dm = (OSDevMgr*)arg;

    while (TRUE) {
        osRecvMesg(dm->evtQueue, (OSMesg)&mb, OS_MESG_BLOCK);
        switch (mb->hdr.type) {
            case OS_MESG_TYPE_VRETRACE:
                __osViSwapContext();
                retrace--;

                if (retrace == 0) {
                    vc = __osViGetCurrentContext();
                    if (vc->msgq != NULL) {
                        osSendMesg(vc->msgq, vc->msg, OS_MESG_NOBLOCK);
                    }
                    retrace = vc->retraceCount;
                }

                __osViIntrCount++;

                if (first) {
                    count = osGetCount();
                    __osCurrentTime = count;
                    first = 0;
                }

                count = __osBaseCounter;
                __osBaseCounter = osGetCount();
                count = __osBaseCounter - count;
                __osCurrentTime = __osCurrentTime + count;
                break;
            case OS_MESG_TYPE_COUNTER:
                __osTimerInterrupt();
                break;
            default:
                break;
        }
    }
}
