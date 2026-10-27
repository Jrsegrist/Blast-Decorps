/* libultra src/os/destroythread.c, from decompals/ultralib (see Makefile UL_*).
 * Blast Corps' SDK predates ultralib's oldest supported version here: its
 * active-queue unlink walks tlnext until NULL (ultralib's non-IDO pre-2.0J
 * form) instead of stopping at the priority -1 sentinel. Otherwise this is
 * ultralib's source. */
#include "PR/os_internal.h"
#include "PRinternal/osint.h"

void osDestroyThread(OSThread* t) {
    register u32 saveMask;
    register OSThread* pred;
    register OSThread* succ;

    saveMask = __osDisableInt();

    if (t == NULL) {
        t = __osRunningThread;
    } else if (t->state != OS_STATE_STOPPED) {
        __osDequeueThread(t->queue, t);
    }

    if (__osActiveQueue == t) {
        __osActiveQueue = __osActiveQueue->tlnext;
    } else {
        pred = __osActiveQueue;
        succ = pred->tlnext;
        while (succ != NULL) {
            if (succ == t) {
                pred->tlnext = t->tlnext;
                break;
            }
            pred = succ;
            succ = pred->tlnext;
        }
    }

    if (t == __osRunningThread) {
        __osDispatchThread();
    }

    __osRestoreInt(saveMask);
}
