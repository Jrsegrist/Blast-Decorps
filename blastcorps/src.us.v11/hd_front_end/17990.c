#include "common.h"
#include <ultra64.h>
#ifndef NON_MATCHING /* legacy declarations */
/* Matching build: IDO compiled the matched code here against older
 * declarations of these, which the file keeps; the NON_MATCHING build
 * uses game/game.h's. */
#define LEGACY_gSfxBank
#endif /* legacy declarations */
#include "game/game.h"
/* Views: this file reads these shared variables (game/variables.h) as other types. */
#define gYoshiFeItems ((YoshiItem *) gYoshiFeItems)
#define gYoshiWindows ((YoshiWindow *) gYoshiWindows)
#define gDynamicBufs ((FeDyn *) gDynamicBufs)
/* end of views */

/* back_loop.c: the front end's per-frame loop (menus behind the yoshi windows) */


#ifndef NON_MATCHING
extern s32 gSfxBank;
#endif
extern u8 gPakReady;
extern char gUnlockCountText[];
extern s16 gPakDeleteItem;
extern s16 gLastYoshiSelection;
extern s32 gPakCheckTickSound;

int sprintf(char *, const char *, ...);

/* Rare's assert; line numbers are the original back_loop.c's */
#define BACK_ASSERT(EX, line) \
    if (!(EX)) \
    debug_printf("\n\a --- ASSERTION FAULT - %s - %s, line %d\n\n", #EX, "back_loop.c", line)

/* The front end's frame: window selections, display list, next mode (0x801FE990).
 * One front-end frame: handles pak replies and the menu (yoshi) selection for
 * the current mode, starts the previous display list and builds this frame's,
 * then picks the next mode once the current one is idle. */
void fe_back_loop(void) {
    s32 i;
    Gfx *gdl;
    OSMesg msg;

    if (gGameMode & (MODE_PAK_CHECK | MODE_PAK_MANAGER)) {
        if (gPakCheckTickSound == 0) {
            snd_play(gSfxBank, SFX_TICK_LOOP, &gPakCheckTickSound);
        }
        if (yoshiState == 2) {
            osRecvMesg(&pakToGameMessageQ, &msg, 1);
            BACK_ASSERT(MQ_IS_EMPTY(&pakToGameMessageQ), 62);
            gPakReady = !msg;
            if (gGameMode == MODE_PAK_CHECK) {
                if (gPakReady) {
                    fe_player_load_slots();
                }
                gNextGameMode = MODE_PAK_CHECKED;
            } else {
                gNextGameMode = MODE_PAK_MANAGER_CHECKED;
            }
            snd_stop((void *) N64_IPTR(gPakCheckTickSound));
            yoshi_demand(YOSHI_DEMAND_OFF);
        }
    }
    if (yoshiSelection) {
        debug_printf("yoshiSelection in back_loop is %d %d\n", yoshiSelection, lowest_set_bit(gGameMode));
        switch (gGameMode) {
            case MODE_LOAD_GAME:
                if (yoshiSelection == 0xFFFF) {
                    gNextGameMode = MODE_LOAD_GAME_EXIT;
                    fe_player_ticker_set_text(4, 1);
                } else if (gYoshiWindows[YW_PLAYER_MENU].sel - 2 < 4) {
                    gNextGameMode = MODE_PLAYER_CHOSEN;
                } else if (gLoadMenuFullSlot == 4) {
                    gPakReady = 0;
                    gIgnorePak = 1;
                    gNextGameMode = MODE_PAK_CHECKED;
                } else {
                    gNextGameMode = MODE_TO_ERASE_MENU;
                }
                break;
            case MODE_PAK_FILES:
                if (yoshiSelection != 0xFFFF && fe_pak_has_files()) {
                    gPakDeleteItem = yoshiSelection;
                    gNextGameMode = MODE_PAK_FILE_CHOSEN;
                }
                break;
            case MODE_ERASE_MENU:
                if (yoshiSelection == 0xFFFF) {
                    fe_player_ticker_set_text(4, 1);
                    gNextGameMode = MODE_LOAD_GAME_RETURN;
                } else {
                    playerNumber = yoshiSelection - 2;
                    if (playerNumber < 4) {
                        gNextGameMode = MODE_ERASE_PLAYER_CHOSEN;
                    } else {
                        gNextGameMode = MODE_LOAD_GAME_RETURN;
                    }
                }
                break;
            case MODE_ERASE_CONFIRM:
                if (yoshiSelection == 0xC) {
                    fe_player_new_game(playerNumber, 0, 0);
                }
                fe_player_ticker_set_text(4, 1);
                gNextGameMode = MODE_TO_ERASE_MENU;
                break;
            case MODE_PAK_DELETE_CONFIRM:
                if (yoshiSelection == 0xC) {
                    osSendMesg(&gPakRequestQueue, (OSMesg) ((gPakDeleteItem << 16) | 5), 1);
                }
                gNextGameMode = MODE_PAK_FILE_DELETED;
                break;
            case MODE_LEVEL_STATUS:
            case MODE_RESULTS:
                if (yoshiSelection == 0xFFFF) {
                    gNextGameMode = MODE_WORLD;
                } else {
                    i = gYoshiWindows[currentYoshiWindow].itemCount + gYoshiWindows[currentYoshiWindow].firstItem - yoshiSelection - 1;
                    switch (i) {
                        case 0:
                            gNextGameMode = MODE_WORLD;
                            break;
                        case 1:
                            gNextGameMode = MODE_REPLAY;
                            break;
                        case 2:
                            if (!(((s32) gLevelKind) & LEVEL_KIND_ANY_MISSION)) {
                                if (((s32) gLevelKind) == LEVEL_KIND_RACE && gGameMode == MODE_RESULTS) {
                                    gGhostRetry = 1;
                                }
                                gNextGameMode = MODE_VEHICLE_SELECT;
                            } else {
                                gNextGameMode = MODE_LEVEL_START;
                            }
                            break;
                        case 3:
                            gNextGameMode = MODE_BEST_TIMES;
                            break;
                    }
                }
                break;
            case MODE_BEST_TIMES:
                if (yoshiSelection == 0xFFFF) {
                    playerNumber = gLoadedPlayer;
                }
                if (playerNumber == gLoadedPlayer || playerNumber == playerNumberAtStart || yoshiSelection == 0xFFFF) {
                    gNextGameMode = gBestTimesReturnMode;
                } else {
                    gNextGameMode = MODE_GUEST_PLAYER;
                }
                break;
            case MODE_REGRADE_NOTICE:
                fe_player_panel_init(4);
                gNextGameMode = MODE_REGRADE_NOTICE_END;
                break;
            case MODE_ATTRACT_PICTURE:
                fade_to_mode(MODE_BOOT_CHECK);
                break;
            case MODE_ERASE_SAVE:
                if (yoshiSelection == 0xC) {
                    fe_player_new_game(playerNumber, 0, 1);
                }
                break;
            case MODE_GUEST_PLAYER:
                fe_player_ticker_set_text(4, 1);
                break;
            case MODE_QUIT_GAME:
                if (yoshiSelection == 0xC) {
                    fade_to_mode_speed(MODE_ATTRACT_START, 0.5f);
                } else {
                    fade_to_mode_speed(MODE_WORLD, 0.5f);
                }
                break;
            default:
                debug_printf("backdrop illegal yoshi selection\n");
                break;
        }
        gLastYoshiSelection = yoshiSelection;
        yoshiSelection = 0;
    }
    nextdma = 0;
    no_palette_dmas = 0;
    tex_reset_decode_queue();
    rsp_send_task((u64 *) (gDynamicBufs[gBufferIndex].dl), gFrameDlLen, 1, 1, 0x4D2, 0);
    gBufferIndex ^= 1;
    gdl = gDynamicBufs[gBufferIndex].dl;
    gSPSegment(gdl++, 0, 0);
    gSPSegment(gdl++, 2, osVirtualToPhysical(&gDynamicBufs[gBufferIndex]));
    gSPSegment(gdl++, 1, osVirtualToPhysical(gStaticSegment));
    gSPDisplayList(gdl++, seg1RdpInitDl);
    gSPDisplayList(gdl++, seg1RspInitDl);
    gSPClipRatio(gdl++, FRUSTRATIO_3);
    gDPSetCycleType(gdl++, G_CYC_FILL);
    if (gGameMode & (MODE_LEVEL_STATUS | MODE_RESULTS | MODE_PRE_PROMOTION)) {
        gDPSetDepthImage(gdl++, gDepthBufferPhys);
        gDPSetColorImage(gdl++, G_IM_FMT_RGBA, G_IM_SIZ_16b, 320, gDepthBufferPhys);
        gDPSetFillColor(gdl++, 0xFFFCFFFC);
        gDPFillRectangle(gdl++, 220, 110, 300, 160);
    } else if (gGameMode & MODE_PROMOTION) {
        gDPSetDepthImage(gdl++, gDepthBufferPhys);
        gDPSetColorImage(gdl++, G_IM_FMT_RGBA, G_IM_SIZ_16b, 320, gDepthBufferPhys);
        gDPSetFillColor(gdl++, 0xFFFCFFFC);
        gDPFillRectangle(gdl++, 0, 0, 319, 239);
    }
    gDPSetColorImage(gdl++, G_IM_FMT_RGBA, G_IM_SIZ_16b, 320, gFrameBufferPhys[gBufferIndex]);
    text_begin_frame();
    gdl = fe_bg_draw(gdl, (s32) &gDynamicBufs[gBufferIndex], &gFrameDlLen);
    gDPPipeSync(gdl++);
    gDPSetCycleType(gdl++, G_CYC_1CYCLE);
    if (gGameMode & (MODE_REGRADE_NOTICE | MODE_REGRADE_NOTICE_END)) {
        gdl = yoshi_draw(gdl, (s32) &gDynamicBufs[gBufferIndex], &gFrameDlLen);
    }
    if (gGameMode == MODE_REGRADE_NOTICE) {
        if (gFramesInMode == 0) {
            music_start_tune_at(MUS_UNK_0x27_STATE10, 0);
            music_push_tune(MUS_PROMOTION);
        }
        if (gYoshiTypeState == 0 && playerNumber == 4) {
            playerNumber = playerNumberAtStart;
            fe_player_ticker_reset(playerNumber);
            gYoshiWindows[YW_REGRADE_NOTICE].flags |= 0x20;
        }
    }
    if (gGameMode & (MODE_LEVEL_STATUS | MODE_PAK_CHECKED | MODE_LOAD_GAME | MODE_NEW_PLAYER | MODE_ERASE_CONFIRM | MODE_ERASE_PLAYER_CHOSEN | MODE_LOAD_GAME_RETURN | MODE_ERASE_MENU | MODE_TO_ERASE_MENU | MODE_PLAYER_CHOSEN | MODE_RESULTS | MODE_PAK_CHECK | MODE_BEST_TIMES | MODE_ATTRACT_PICTURE | MODE_REGRADE_NOTICE | MODE_REGRADE_NOTICE_END | MODE_BEST_TIMES_UNUSED | MODE_GUEST_PLAYER | MODE_LOAD_GAME_EXIT)) {
        gdl = fe_player_draw_panel(gdl, (struct PlayerSelDyn *) &gDynamicBufs[gBufferIndex], 0xC2);
    }
    if (gGameMode & (MODE_ATTRACT | MODE_PAK_CHECKED | MODE_LOAD_GAME | MODE_NEW_PLAYER | MODE_NAME_ENTRY | MODE_ERASE_PLAYER_CHOSEN | MODE_LOAD_GAME_RETURN | MODE_ERASE_MENU | MODE_TO_ERASE_MENU | MODE_NAME_ENTERED | MODE_PLAYER_CHOSEN | MODE_PAK_CHECK | MODE_PAK_FILES | MODE_UNUSED_33 | MODE_PAK_MANAGER_CHECKED | MODE_PAK_FILE_CHOSEN | MODE_PAK_DELETE_CONFIRM | MODE_PAK_FILE_DELETED | MODE_UNUSED_41 | MODE_BEST_TIMES | MODE_PAK_MANAGER | MODE_REGRADE_NOTICE | MODE_REGRADE_NOTICE_END | MODE_BEST_TIMES_UNUSED | MODE_GUEST_PLAYER | MODE_NO_CONTROLLER | MODE_LOAD_GAME_EXIT)) {
        gdl = title_draw_attract(gdl, (s32) &gDynamicBufs[gBufferIndex], gBufferIndex, &gFrameDlLen);
    }
    if (gGameMode & (MODE_LOAD_GAME | MODE_ERASE_MENU)) {
        u8 old = playerNumber;

        playerNumber = gYoshiWindows[YW_PLAYER_MENU].sel - 2;
        if (playerNumber != old) {
            if (old == 4) {
                fe_player_ticker_set_text(playerNumber, 0);
            } else {
                fe_player_ticker_set_text(playerNumber, 1);
            }
        }
    }
    cont_start_read();
    if (gGameMode & MODE_PROMOTION) {
        gdl = fe_promo_draw(gdl, &gDynamicBufs[gBufferIndex], gBufferIndex, &gFrameDlLen);
    }
    if (gGameMode == MODE_ATTRACT_PICTURE) {
        gdl = fe_attract_pic_draw((s32) gDynamicBufs, gdl);
    }
    gdl = hd_debug_dl_stub(gdl, (DynamicBuf *) &gDynamicBufs[gBufferIndex], &gFrameDlLen);
    if (gGameMode & (MODE_LEVEL_STATUS | MODE_RESULTS | MODE_PRE_PROMOTION)) {
        gdl = fe_player_draw_medals(gdl, &gDynamicBufs[gBufferIndex], &gFrameDlLen);
        if (gUnlockCount != -1 && yoshiState == 2) {
            gdl = fe_comm_point_draw(&gDynamicBufs[gBufferIndex], gdl);
            gSPClearGeometryMode(gdl++, G_ZBUFFER);
        }
    }
    if (gGameMode == MODE_PRE_PROMOTION) {
        if (gPromoCueDelay) {
            gPromoCueDelay--;
        } else if (gYoshiTypeState != 2 && yoshiState == 2) {
            yoshi_demand(YOSHI_DEMAND_OFF);
            snd_play(gSfxBank, SFX_PROMOTION_CUE, NULL);
        }
    }
    cont_update();
    if (gGameMode == MODE_ACADEMY_MESSAGE) {
        academy_play_state_tune();
    }
    if ((gGameMode & (MODE_LEVEL_STATUS | MODE_RESULTS | MODE_PRE_PROMOTION)) && gScheduler.frameCount % 20 >= 6 && gUnlockCount != -1 && yoshiState == 2) {
        s32 x;
        s32 y;

        x = 0x118, y = 0x8C;
        sprintf(gUnlockCountText, "%d", gUnlockCount);
        text_print((Gfx * N64P *) &gDynamicBufs[gBufferIndex], (u8 *) gUnlockCountText, 0, 1, 0, x, y, 0x1A, 0x16, 1, 0, 0, 0, gYoshiAlpha);
        text_print_gradient((Gfx * N64P *) &gDynamicBufs[gBufferIndex], (u8 *) gUnlockCountText, 0, 1, 0, x + 2, y + 2, 0x12, 0x12, 1, 0xFF, 0xFF, 0,
                      gYoshiAlpha, 0xFF, 0, 0, gYoshiAlpha);
    }
    if (gGameMode & MODE_BEST_TIMES) {
        fe_best_next_player();
        gdl = fe_best_draw_icon((s32) &gDynamicBufs[gBufferIndex], gdl);
    }
#ifdef PORT_HOST
    PORT_WTAG(gdl, PORT_WTAG_TICKER_TEXT, 0, 0); /* bc.exe's widescreen: the ticker beyond 4:3 */
#endif
    text_flush_screen(&gdl, (Mtx *) &gDynamicBufs[gBufferIndex]);
#ifdef PORT_HOST
    PORT_WTAG(gdl, PORT_WTAG_TICKER_TEXT, 1, 0);
#endif
    gdl = fade_draw((s32) &gDynamicBufs[gBufferIndex], gdl);
    if (!(gGameMode & (MODE_REGRADE_NOTICE | MODE_REGRADE_NOTICE_END))) {
        gdl = yoshi_draw(gdl, (s32) &gDynamicBufs[gBufferIndex], &gFrameDlLen);
    }
    if (yoshiState != 1 && currentYoshiWindow == YW_NAME_ENTRY) {
        gdl = fe_player_name_entry_draw(gdl, (struct PlayerSelDyn *) &gDynamicBufs[gBufferIndex], &gFrameDlLen);
    }
    gDPFullSync(gdl++);
    gSPEndDisplayList(gdl++);
    gFrameDlLen = gdl - gDynamicBufs[gBufferIndex].dl;
    for (i = 0; i < nextdma; i++) {
        osRecvMesg(&textureDmaMessageQ, NULL, 1);
    }
    for (i = 0; i < nextdma - no_palette_dmas; i++) {
        tex_decode_next();
    }
    if ((gGameMode & (MODE_PAK_CHECKED | MODE_NEW_PLAYER | MODE_ERASE_PLAYER_CHOSEN | MODE_LOAD_GAME_RETURN | MODE_TO_ERASE_MENU | MODE_NAME_ENTERED | MODE_PLAYER_CHOSEN | MODE_PRE_PROMOTION | MODE_PAK_FILES | MODE_UNUSED_WINDOW_13 | MODE_PAK_MANAGER_CHECKED | MODE_PAK_FILE_CHOSEN | MODE_PAK_FILE_DELETED | MODE_UNUSED_41 | MODE_ACADEMY_MESSAGE | MODE_ATTRACT_PICTURE | MODE_REGRADE_NOTICE_END | MODE_UNUSED_52 | MODE_ERASE_SAVE | MODE_BEST_TIMES_UNUSED | MODE_GUEST_PLAYER | MODE_LOAD_GAME_EXIT)) && yoshiState == 1 && !fade_window_held() && !areWeFading() &&
        gNextGameMode == 0 && gYoshiPendingWindow == NO_YOSHI_WINDOW) {
        switch (gGameMode) {
            case MODE_NEW_PLAYER:
                gNextGameMode = MODE_NAME_ENTRY;
                break;
            case MODE_LOAD_GAME_RETURN:
                gNextGameMode = MODE_LOAD_GAME;
                break;
            case MODE_PAK_FILE_DELETED:
                gNextGameMode = MODE_PAK_FILES;
                break;
            case MODE_ERASE_PLAYER_CHOSEN:
                gNextGameMode = MODE_ERASE_CONFIRM;
                gYoshiFeItems[9].text = gYoshiFeItems[playerNumber + 2].text;
                gYoshiFeItems[9].jtext = NULL;
                gYoshiFeItems[9].w = gYoshiFeItems[9].h = 0x14;
                yoshi_demand(YOSHI_DEMAND_OPEN | YW_CONFIRM);
                break;
            case MODE_PAK_FILE_CHOSEN:
                gNextGameMode = MODE_PAK_DELETE_CONFIRM;
                gYoshiFeItems[9].text = gYoshiFeItems[gPakDeleteItem].text;
                gYoshiFeItems[9].jtext = gYoshiFeItems[gPakDeleteItem].jtext;
                gYoshiFeItems[9].w = gYoshiFeItems[9].h = 0xF;
                yoshi_demand(YOSHI_DEMAND_OPEN | YW_CONFIRM);
                break;
            case MODE_TO_ERASE_MENU:
                gNextGameMode = MODE_ERASE_MENU;
                break;
            case MODE_PLAYER_CHOSEN:
                gLoadedPlayer = playerNumber;
                playerNumberAtStart = playerNumber;
                if (gSlotState[playerNumber] == 1) {
                    fade_to_mode(MODE_WORLD);
                } else {
                    gNextGameMode = MODE_NEW_PLAYER;
                }
                break;
            case MODE_LOAD_GAME_EXIT:
                fade_to_mode(MODE_ATTRACT_START);
                break;
            case MODE_NAME_ENTERED:
                osSendMesg(&gPakRequestQueue, (OSMesg) ((playerNumber << 16) | 7), 1);
                osSendMesg(&gPakRequestQueue, (OSMesg) ((playerNumber << 16) | 0x15 | 0x01000000), 1);
                seq_prepare(4);
                fade_to_mode(MODE_GLOBE_SEQ);
                break;
            case MODE_PRE_PROMOTION:
                gNextGameMode = MODE_PROMOTION;
                break;
            case MODE_PAK_CHECKED:
                if (gIgnorePak) {
                    fade_to_mode(MODE_BOOT_CHECK);
                } else if (gPakReady) {
                    gNextGameMode = MODE_LOAD_GAME;
                } else {
                    gNextGameMode = MODE_PAK_CHECK;
                }
                break;
            case MODE_PAK_MANAGER_CHECKED:
                if (gPakReady) {
                    gNextGameMode = MODE_PAK_FILES;
                } else {
                    gNextGameMode = MODE_PAK_MANAGER;
                }
                break;
            case MODE_ERASE_SAVE:
            case MODE_PAK_FILES:
                fade_to_mode_speed(MODE_BOOT_SCREEN1, 0.5f);
                break;
            case MODE_ACADEMY_MESSAGE:
                fade_to_mode_speed(MODE_ACADEMY_NEXT, 0.75f);
                music_fade_to(0.0f);
                break;
            case MODE_ATTRACT_PICTURE:
                fade_to_mode_speed(MODE_ATTRACT, 0.5f);
                break;
            case MODE_REGRADE_NOTICE_END:
                playerNumber = playerNumberAtStart;
                fade_to_mode_speed(MODE_WORLD, 0.6f);
                break;
            case MODE_BEST_TIMES_UNUSED:
                if (playerNumber == gLoadedPlayer || playerNumber == playerNumberAtStart) {
                    gNextGameMode = gBestTimesReturnMode;
                } else {
                    gNextGameMode = MODE_GUEST_PLAYER;
                }
                break;
            case MODE_GUEST_PLAYER:
                if (gLastYoshiSelection != 0xC) {
                    gNextGameMode = MODE_BEST_TIMES_RETURN;
                } else {
                    fade_to_mode(gBestTimesReturnMode);
                }
                break;
            default:
                debug_printf("illegal yoshi wait game mode %d\n", lowest_set_bit(gGameMode));
                break;
        }
    }
}
