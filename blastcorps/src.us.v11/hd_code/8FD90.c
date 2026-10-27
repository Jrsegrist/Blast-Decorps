#include "common.h"
#include <ultra64.h>
#include "game/game.h"

/* libultra osDpSetStatus: writes DPC_STATUS_REG (0x802D4550). */
void osDpSetStatus(u32 arg0) {
    *(volatile u32 *) 0xA410000C = arg0;
}
