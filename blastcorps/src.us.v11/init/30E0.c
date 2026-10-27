#include "common.h"
#include <ultra64.h>

s32 __osSiDeviceBusy(void) {
    register u32 stat = IO_READ(SI_STATUS_REG);

    if (stat & (SI_STATUS_DMA_BUSY | SI_STATUS_RD_BUSY)) {
        return 1;
    }
    return 0;
}
