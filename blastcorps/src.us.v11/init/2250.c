#include "common.h"
#include <ultra64.h>

u32 osPiGetStatus(void) {
    return IO_READ(PI_STATUS_REG);
}
