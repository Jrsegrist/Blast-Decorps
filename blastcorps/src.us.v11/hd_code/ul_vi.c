/* libultra src/io/vi.c, from decompals/ultralib (see Makefile UL_*).
 * Blast Corps' SDK predates ultralib's oldest supported version here:
 * - the object also defines osViClock and a copy of the boot-time TV type
 *   (its real name is unknown), and __osViInit picks the mode and the VI
 *   clock from that copy: NTSC, otherwise MPAL (no PAL support; the
 *   second mode table, osViModeMpalLan1, has MPAL timing and burst values);
 * - framep is left zero.
 * Otherwise this is ultralib's source. */
#include "PRinternal/macros.h"
#include "PR/os_internal.h"
#include "PR/R4300.h"
#include "PR/rcp.h"
#include "PRinternal/viint.h"

static __OSViContext vi[2] ALIGNED(0x8) = { 0 };
__OSViContext* __osViCurr = &vi[0];
__OSViContext* __osViNext = &vi[1];
s32 __osViTvType = OS_TV_NTSC;
s32 osViClock = VI_NTSC_CLOCK;

void __osViInit(void) {
    __osViTvType = osTvType;
    bzero(vi, sizeof(vi));
    __osViCurr = &vi[0];
    __osViNext = &vi[1];
    __osViNext->retraceCount = 1;
    __osViCurr->retraceCount = 1;

    if (__osViTvType == OS_TV_NTSC) {
        __osViNext->modep = &osViModeNtscLan1;
        osViClock = VI_NTSC_CLOCK;
    } else {
        __osViNext->modep = &osViModeMpalLan1;
        osViClock = VI_MPAL_CLOCK;
    }

    __osViNext->state = VI_STATE_BLACK;
    __osViNext->control = __osViNext->modep->comRegs.ctrl;

    while (IO_READ(VI_CURRENT_REG) > 10) { // wait for vsync?
    }

    IO_WRITE(VI_CONTROL_REG, 0); // pixel size blank (no data, no sync)
    __osViSwapContext();
}
