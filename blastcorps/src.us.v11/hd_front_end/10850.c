#include "common.h"
#include <ultra64.h>
#ifndef NON_MATCHING /* legacy declarations */
/* Matching build: IDO compiled the matched code here against older
 * declarations of these, which the file keeps; the NON_MATCHING build
 * uses game/game.h's. */
#define LEGACY_gYoshiHeapItems
#endif /* legacy declarations */
#include "game/game.h"
/* Views: this file reads these shared variables (game/variables.h) as other types. */
#define gLevelInfo ((LevelInfo *) gLevelInfo)
#define players ((PlayerInfo *) players)
#define gYoshiWindows ((YoshiWindow *) gYoshiWindows)
#ifdef NON_MATCHING
#define gYoshiHeapItems (*(YoshiItem * N64P *) &gYoshiHeapItems)
#endif
/* end of views */

/*
 * bestTimes.c (assert file name at 0x8020FF70): the best-times screen, a
 * menu of 4 rows per vehicle slot (one per player) built from the save records.
 */

int sprintf(char *, const char *, ...);
void bcopy(const void *, void *, int);

#define BT_ASSERT(EX, line) \
    if (!(EX)) debug_printf("\n\a --- ASSERTION FAULT - %s - %s, line %d\n\n", #EX, "bestTimes.c", line)

/* pfsHandler.c's message to the save thread: command, argument, player, reply wanted */
#define PAK_MSG(cmd, arg, pn, reply) ((cmd) | ((arg) << 8) | ((pn) << 16) | ((reply) << 24))

/* (players[]: PlayerInfo, game/types.h) */

/* (gLevelInfo[]: LevelInfo, game/types.h) */

/* (The window, YW_BEST_TIMES, and its rows, window items built on the heap:
 * YoshiWindow, YoshiItem, game/types.h.  While fe_best_build_menu sorts the rows,
 * an item's sound holds the row's best time, its color the player and its
 * spriteBase the vehicle slot.) */

#if VERSION_US_V10
/* US 1.0 took the item count from the window (itemCount), which on a timed
 * mission includes the blank last item; US 1.1 keeps its own count of the time
 * rows, so the highlight loops no longer touch that extra item. */
#define BEST_ITEM_COUNT (gYoshiWindows[YW_BEST_TIMES].itemCount)
#else
extern u32 gBestItemCount;        /* number of best-time menu items */
#define BEST_ITEM_COUNT gBestItemCount
#endif
extern u8 gBestItemPlayer[];       /* player of each item */
extern u8 gBestRowSlot[];       /* level of each row */
#ifndef NON_MATCHING
extern YoshiItem *gYoshiHeapItems;
#endif
extern char gBestRowText[][0x20];
extern char gBestRowPrefix[][4];


s32 fe_best_slot_shown(u8 lvl);
s32 fe_best_cmp(YoshiItem *a, YoshiItem *b);
s32 fe_best_player_listed(u8 pn);

/* The best-times menu: per vehicle slot, a row per player (0x801F7850).
 * build the best-times menu */
void fe_best_build_menu(void) {
    PlayerInfo *p;
    YoshiWindow *hdr;
    YoshiItem *item;
    s32 lvl;
    s32 i;
    s32 n;
    char timeStr[0x20];
    char buf[0x20];

    p = &players[playerNumber];
    hdr = &gYoshiWindows[YW_BEST_TIMES];
    gYoshiHeapItems = (YoshiItem *) gHeapPtr;
    gHeapPtr += 0x71C;
    for (i = 0; i < 4; i++) {
        if (gSlotState[i] == 1 && saveIt[i] == 0
            && ((players[i].medal[levelno] > 0 && players[i].medal[levelno] < 6) ? 1 : 0)) {
            osSendMesg(&gPakRequestQueue, (OSMesg) PAK_MSG(8, levelno, i, 1), OS_MESG_BLOCK);
            osRecvMesg(&pakToGameMessageQ, NULL, OS_MESG_BLOCK);
        } else {
            fe_best_clear_times(i);
        }
    }
    lvl = 0;
    n = 0;
    for (; lvl < 16; lvl++) {
        if (fe_best_slot_shown(lvl) && gBestTimes[playerNumberAtStart][gVehicleTimeSlot[lvl]] > 0) {
            for (i = 0; i < 4; i++) {
                item = &gYoshiHeapItems[n * 4 + i];
                p = &players[i];
                if (gSlotState[i] == 1 && gBestTimes[i][gVehicleTimeSlot[lvl]] > 0
                    && (gLevelInfo[levelno].kind != LEVEL_KIND_MISSION_TIMED || p->gameState >= 11)) {
                    hud_format_time(timeStr, gBestTimes[i][gVehicleTimeSlot[lvl]], 0);
                    sprintf(gBestRowText[n * 4 + i], "%-7.7s %s", p, timeStr);
                    item->text = gBestRowText[n * 4 + i];
                    item->jtext = 0;
                    item->sprite = fe_stats_grade_time(gBestTimes[i][gVehicleTimeSlot[lvl]], levelno, players[i].gameState) % 5 + 0x12;
                    item->color = i;
                } else {
                    item->text = 0;
                    item->jtext = 0;
                    item->sprite = 0;
                    item->color = 4;
                }
                item->flags = 0x1400;
                item->x = 0x24;
                item->w = 0x10;
                item->h = 0x11;
                item->sound = gBestTimes[i][gVehicleTimeSlot[lvl]];
                item->spriteBase = lvl;
            }
            text_shell_sort((u8 *) &gYoshiHeapItems[n * 4], 4, 0x1C, (s32 (*)(void *, void *)) fe_best_cmp);
            for (i = 0; i < 4; i++) {
                item = &gYoshiHeapItems[n * 4 + i];
                item->y = i * 17;
                if (i == 2) {
                    item->flags |= 1;
                }
                if (item->text != 0) {
                    bcopy(item->text, buf, font_strlen((u8 *) item->text) + 1);
                    sprintf(item->text, "%s %s", gBestRowPrefix[i], buf);
                }
            }
            n++;
        }
    }
    text_shell_sort((u8 *) gYoshiHeapItems, n, 0x70, (s32 (*)(void *, void *)) fe_best_cmp);
    for (lvl = 0; lvl < n * 4; lvl++) {
        item = &gYoshiHeapItems[lvl];
        gBestItemPlayer[lvl] = item->color;
        if (!(lvl & 3)) {
            gBestRowSlot[lvl / 4] = item->spriteBase;
        }
        item->sound = 0x1E;
        item->y += lvl / 4 * 100;
    }
    if (gLevelInfo[levelno].kind == LEVEL_KIND_MISSION_TIMED) {
        item = &gYoshiHeapItems[lvl];
        item->text = 0;
        item->jtext = 0;
        item->flags = 0x400;
        item->x = -0x20;
        item->y = 0x28;
        item->sprite = 0xD;
        item->sound = item->spriteBase = 0;
        hdr->itemCount = n * 4 + 1;
    } else {
        hdr->itemCount = n * 4;
    }
    hdr->sel = 2;
#if !VERSION_US_V10
    gBestItemCount = n * 4;
#endif
    fe_best_color_items();
    fe_load_vehicle_icons();
}

/* A vehicle slot the player knows (special levels: slot 0) (0x801F7F74).
 * whether best-time slot `lvl` is shown (special levels: only slot 0) */
s32 fe_best_slot_shown(u8 lvl) {
    if (gLevelInfo[levelno].kind == LEVEL_KIND_MISSION_TIMED) {
        return lvl == 0;
    }
    return (players[playerNumberAtStart].vehiclesUnlocked & (1 << lvl)) ? 1 : 0;
}

/* Sort: items with text first, then by time (0x801F7FF4).
 * qsort comparator: items with text first, then by time */
s32 fe_best_cmp(YoshiItem *a, YoshiItem *b) {
    if (a->text != 0 && b->text != 0) {
        return a->sound - b->sound;
    }
    if (a->text != 0) {
        return -1;
    }
    return 1;
}

/* Z/R: highlights the next player with entries (0x801F803C).
 * cycle the highlighted player on a button press */
void fe_best_next_player(void) {
    s32 next;
    s32 i;

    if ((gContButtons & (Z_TRIG | R_TRIG)) && !(gContPrevButtons & (Z_TRIG | R_TRIG))) {
        next = (playerNumber + 1) % 4;
        i = 0;
        for (; i < 4 && (gSlotState[next % 4] != 1 || fe_best_player_listed(next % 4) == 0); i++, next = (next + 1) % 4) {
        }
        if (playerNumber != next) {
            snd_play(gSfxBank, SFX_MENU_MOVE, 0);
            playerNumber = next;
            fe_player_ticker_set_text(playerNumber, 1);
            fe_best_color_items();
        } else {
            snd_play(gSfxBank, SFX_DENIED, 0);
        }
    }
}

/* Whether the player has entries (0x801F81B4).
 * whether player `pn` has an item in the menu */
s32 fe_best_player_listed(u8 pn) {
    u32 n;
    u32 i;

#if VERSION_US_V10
    n = BEST_ITEM_COUNT;
    i = 0;
#else
    i = 0;
    n = BEST_ITEM_COUNT;
#endif
    for (; i < n && gBestItemPlayer[i] != pn; i++) {
    }
    return i != n;
}

/* The highlight and colours by player (0x801F8228).
 * highlight and colour the items by player */
void fe_best_color_items(void) {
    YoshiItem *p;
#if VERSION_US_V10
    s32 i;
#else
    u32 i;
#endif

    for (i = 0; i < BEST_ITEM_COUNT; i++) {
        p = &gYoshiHeapItems[i];
        if (gBestItemPlayer[i] == playerNumber) {
            p->flags |= 4;
        } else {
            p->flags &= ~4;
        }
        if (gBestItemPlayer[i] == playerNumberAtStart) {
            p->color = p->selColor = 6;
        } else if (gBestItemPlayer[i] == playerNumber) {
            p->color = p->selColor = 2;
        } else {
            p->color = p->selColor = 7;
        }
    }
}

/* Zeroes a player's times unless the level is done (0x801F8354).
 * clear player `pn`'s best times unless the current level is ranked */
void fe_best_clear_times(u8 pn) {
    s32 i;

#if VERSION_US_V10
    BT_ASSERT(frontEndPresent, 276);
#else
    BT_ASSERT(frontEndPresent, 279);
#endif
    if (!((players[pn].medal[levelno] > 0 && players[pn].medal[levelno] < 6) ? 1 : 0)) {
        for (i = 0; i < 16; i++) {
            gBestTimes[pn][gVehicleTimeSlot[i]] = 0;
        }
    }
}

/* The current row's vehicle icon (0x801F8440).
 * draw the current row's level icon */
Gfx *fe_best_draw_icon(s32 arg0, Gfx *gfx) {
    Gfx *g;
    s16 off;

    off = 0;
    g = gfx;
    if (gLevelInfo[levelno].kind != LEVEL_KIND_MISSION_TIMED) {
        g = spr_begin(g);
        g = spr_draw(g, gBestRowSlot[gYoshiWindows[YW_BEST_TIMES].sel / 4] + gVehicleIconTex, 0x18 - off, 100, 0xFF - off * 2, 1, 1.0f);
        g = spr_end(g);
    }
    return g;
}
