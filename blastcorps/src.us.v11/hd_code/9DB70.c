#include "common.h"
#include <ultra64.h>
#include "game/game.h"

u32 __osSpGetStatus(void) {
    return *(volatile u32 *) 0xA4040010;
}
