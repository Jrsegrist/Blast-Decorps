#include "common.h"
#include <ultra64.h>
#include "game/game.h"
/* Views: this file reads these shared variables (game/variables.h) as other types. */
#define gLevelInfo ((LevelInfo *) gLevelInfo)
#define players ((PlayerInfo *) players)
/* end of views */

/* (gLevelInfo[]: LevelInfo, game/types.h) */

/* (players[]: PlayerInfo, game/types.h) */


/* "restoring from EE memory": rebuilds the RAM save image (0x80201E80).
 * Restore the current player's records from EEPROM: one pak-thread request
 * (0xA) per even level, then for each completed level other than 0x26, 0x2F
 * and 0x31 requests 0xD (when gLevelInfo[level].unk0 == 1) and 9, copying a
 * time from gEepTimes into gBestTimes. Stops early on a non-zero reply. */
s32 fe_pak_restore_from_eeprom(void) {
    u8 i;
    OSMesg msg;

    msg = 0;
    debug_printf("restoring from EE memory\n");
    for (i = 0; i < 0x3C && msg == 0; i++) {
        if (!(i & 1)) {
            osSendMesg(&gPakRequestQueue, (OSMesg) ((i << 8) | 0xA | (playerNumber << 16) | 0x01000000), 1);
            osRecvMesg(&pakToGameMessageQ, &msg, 1);
        }
        if (((players[playerNumber].medal[i] > 0 && players[playerNumber].medal[i] < 6) ? 1 : 0) &&
            i != 0x31 && i != 0x2F && i != 0x26 && msg == 0) {
            if (gLevelInfo[i].kind == LEVEL_KIND_MISSION) {
                pakBuffer[0] = 0x1234567887654321;
                osSendMesg(&gPakRequestQueue, (OSMesg) ((i << 8) | 0xD | (playerNumber << 16) | 0x01000000), 1);
                osRecvMesg(&pakToGameMessageQ, NULL, 1);
            }
            fe_best_clear_times(playerNumber);
            gBestTimes[playerNumber][gVehicleTimeSlot[players[playerNumber].bestVehicle[i]]] = gEepTimes[i * 2];
            osSendMesg(&gPakRequestQueue, (OSMesg) ((i << 8) | 9 | (playerNumber << 16) | 0x01000000), 1);
            osRecvMesg(&pakToGameMessageQ, NULL, 1);
        }
    }
    return (s32) msg;
}
