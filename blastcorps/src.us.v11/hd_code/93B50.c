#include "common.h"
#include <ultra64.h>
#include "game/game.h"

/* libaudio alCSPGetVol: the compressed sequence player's volume (0x802D8310). */
s16 alCSPGetVol(ALCSPlayer *arg0) {
    return *(s16 *) ((u8 *) arg0 + 0x32);
}
