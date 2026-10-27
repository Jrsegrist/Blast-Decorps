#include "common.h"
#include <ultra64.h>

extern s32 __osSiDeviceBusy(void);

s32 __osSiRawReadIo(u32 devAddr, u32 *data) {
    if (__osSiDeviceBusy()) {
        return -1;
    }
    *data = IO_READ(devAddr);
    return 0;
}
