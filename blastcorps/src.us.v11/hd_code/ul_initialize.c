/* libultra src/os/initialize.c, from decompals/ultralib (see Makefile UL_*).
 * Blast Corps' SDK predates ultralib's oldest supported version here:
 * - the object's data is only osClockRate and __osShutdown (no osViClock
 *   or __OSGlobalIntMask);
 * - osInitialize doesn't pick osViClock from osTvType.
 * Otherwise this is ultralib's pre-2.0J _FINALROM source. */
#include "PR/os_internal.h"
#include "PR/rcp.h"
#include "PR/os_version.h"
#include "PRinternal/piint.h"

typedef struct {
    /* 0x0 */ unsigned int inst1;
    /* 0x4 */ unsigned int inst2;
    /* 0x8 */ unsigned int inst3;
    /* 0xC */ unsigned int inst4;
} __osExceptionVector;
extern __osExceptionVector __osExceptionPreamble[];

OSTime osClockRate = OS_CLOCK_RATE;
u32 __osShutdown = 0;
u32 __osFinalrom;

void osInitialize() {
    u32 pifdata;
    u32 clock = 0;

    __osFinalrom = TRUE;

    __osSetSR(__osGetSR() | SR_CU1);    // enable fpu
    __osSetFpcCsr(FPCSR_FS | FPCSR_EV | FPCSR_RM_RN); // flush denorm to zero, enable invalid operation

    while (__osSiRawReadIo(PIF_RAM_END - 3, &pifdata)) { // last byte of joychannel ram
        ;
    }
    while (__osSiRawWriteIo(PIF_RAM_END - 3, pifdata | 8)) {
        ;
    }
    *(__osExceptionVector*)UT_VEC = *__osExceptionPreamble;
    *(__osExceptionVector*)XUT_VEC = *__osExceptionPreamble;
    *(__osExceptionVector*)ECC_VEC = *__osExceptionPreamble;
    *(__osExceptionVector*)E_VEC = *__osExceptionPreamble;
    osWritebackDCache((void*)UT_VEC, E_VEC - UT_VEC + sizeof(__osExceptionVector));
    osInvalICache((void*)UT_VEC, E_VEC - UT_VEC + sizeof(__osExceptionVector));
    osMapTLBRdb();
    osPiRawReadIo(4, &clock); // Read clock rate from the ROM header
    clock &= ~0xf;
    if (clock != 0)
    {
        osClockRate = clock;
    }
    osClockRate = osClockRate * 3 / 4;

    if (osResetType == 0) { // cold reset
        bzero(osAppNMIBuffer, OS_APP_NMI_BUFSIZE);
    }
}
