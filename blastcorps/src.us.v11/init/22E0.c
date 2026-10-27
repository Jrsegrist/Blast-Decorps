#include "common.h"
#include <ultra64.h>

extern s32 __osSiDeviceBusy(void);

s32 __osSiRawWriteIo(u32 devAddr, u32 data) {
    if (__osSiDeviceBusy()) {
        return -1;
    }
    IO_WRITE(devAddr, data);
    return 0;
}
