#include "common.h"
#include <ultra64.h>

/* (not <PR/os_internal.h>: its __osSetHWIntrRoutine is the older
 * two-argument one; this is the three-argument variant with a stack) */
extern u32 __osDisableInt(void);
extern void __osRestoreInt(u32);

typedef struct {
    s32 handler;  /* (the SDK's __OSHWIntr: handler, stackEnd; s32 here, as the matched code has them) */
    s32 stackEnd;
} HandlerEntry;

extern HandlerEntry __osHwIntTable[];

/* libultra __osSetHWIntrRoutine (the three-argument variant): with interrupts off, stores the handler and its
 * stack in __osHwIntTable[interrupt]; no callers in init (0x80222400). */
void __osSetHWIntrRoutine(s32 interrupt, s32 handler, s32 stackEnd) {
    register u32 savedMask;
    HandlerEntry *entry;

    savedMask = __osDisableInt();
    entry = &__osHwIntTable[interrupt];
    entry->handler = handler;
    entry->stackEnd = stackEnd;
    __osRestoreInt(savedMask);
}
