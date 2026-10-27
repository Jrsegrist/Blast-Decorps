#include "common.h"
#include <ultra64.h>
#include "game/game.h"

void alSynDelete(ALSynth *drvr) {
    drvr->head = NULL;
}
