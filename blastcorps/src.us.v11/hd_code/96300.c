#include "common.h"
#include <ultra64.h>
#include "game/game.h"

/* libultra osDpGetStatus: reads DPC_STATUS_REG (0x802DAAC0). */
u32 osDpGetStatus(void) {
    return *(volatile u32 *) 0xA410000C;
}
