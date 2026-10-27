#include "common.h"
#include <ultra64.h>
#ifndef NON_MATCHING /* legacy declarations */
/* Matching build: IDO compiled the matched code here against older
 * declarations of these, which the file keeps; the NON_MATCHING build
 * uses game/game.h's. */
#define LEGACY_gHeapPtr
#define LEGACY_gLevelData
#define LEGACY_gLevelSegment8
#define LEGACY_gVehiclesEnd
#define LEGACY_gMusicPlayer
#define LEGACY_gSfxBank
#define LEGACY_gRdus
#define LEGACY_gMarkedTargetPts
#define LEGACY_gRdpOutputBuffer
#define LEGACY_gDriverPos
#endif /* legacy declarations */
#include "game/game.h"
/* Views: this file reads these shared variables (game/variables.h) as other types. */
#define gYoshiFeItems ((YoshiItem *) gYoshiFeItems)
#define gPakThread ((u8 *) &gPakThread)
#define gLevelInfo ((LevelInfo *) gLevelInfo)
#define gYoshiGameItems ((YoshiItem *) gYoshiGameItems)
#define gYoshiWindows ((YoshiWindow *) gYoshiWindows)
#define gVehicles ((VehRecord *) gVehicles)
#define players ((PlayerInfo *) players)
#define pakBuffer ((u8 *) pakBuffer)
#ifdef NON_MATCHING
#define gHeapPtr (*(s32 *) &gHeapPtr)
#define gLevelSegment8 (*(u32 *) &gLevelSegment8)
#define gVehiclesEnd (*(VehRecord * N64P *) &gVehiclesEnd)
#define gRdus (*(Node88 * N64P *) &gRdus)
#define gMarkedTargetPts (*(MarkerPts * N64P *) &gMarkedTargetPts)
#define gRdpOutputBuffer (*(s32 *) &gRdpOutputBuffer)
#define gDriverPos (*(s32 *) gDriverPos)
#endif
/* end of views */

/* hd.c (from its assert strings): boot, the main game thread and the
 * frame loop. Owns .rodata at 0x80307B90.
 *
 * Variables defined here (the tell is a shared `lui` on paired stores).
 * Only part of hd.c's .bss is modelled so far: the section is placed at
 * 0x80310D80 by hd_code_bss.us.v11.ld, and the later variables get their
 * real addresses from undefined_syms (absolute symbols win).
 *
 * The game mode (gGameMode, one bit of a u64; hd.c's debug output calls it
 * game_mode and the next one loop_done). What the code shows of the bits:
 *   0x2 attract mode (the demo recordings, "PRESS START"); 0x4 a level in
 *   play; 0x100 its map view ("MISSILE VIEW" / "SHUTTLE VIEW"); 0x10, 0x20
 *   the front end's boot screens; 0x2000 a level starts (its file is loaded,
 *   then 0x4); 0x4000 the world screen ("World screen centred on level");
 *   0x40000 name entry ("ENTER NAME!"); 0x8000000 after a level (results,
 *   saving); 0x20000000 the vehicle select screen; 0x100000000000 the
 *   globe's sequences; 0x20000000000000 the boot-time EEPROM / pak check
 *   ("NO EE PRESENT"); 0x40000000000000 "ERASE SAVED GAME!";
 *   0x100000000000000 "BECOME GUEST PLAYER:"; 0x400000000000000 the start
 *   of the attract sequence; 0x2000000000000000 the Yoshi window pages
 *   ("USE Z/R TO TURN PAGES"); 0x4000000000000000 "QUIT GAME!". */
u64 gMainThreadStack[0x400]; /* 0x80310D80: the main thread's stack, its low half filled with a guard pattern */
u64 gGameMode; /* 0x80364A90: the current game mode */
u64 gNextGameMode; /* 0x80364A98: the mode to switch to; nonzero ends the frame loop */
u64 gFrameDrawTime; /* osGetTime() at the frame's draw (seeds the screen shake and part jitter) */
u64 gPrevGameMode; /* 0x80364A88 */
u64 gGameModeAfterNext; /* 0x80364AA0: set during a switch; becomes gNextGameMode after it */
u64 gFrameStartTime; /* 0x80364AD0: osGetTime() at the start of the frame */
u8 gCamSnap; /* 0x80364412: the camera jumps to the wanted eye and target this frame */

void hd_idle_thread(void *);
void hd_main_thread(void *);

extern OSThread gIdleThread;
extern u64 gIdleThreadStack[]; /* idle thread stack */
extern u8 gPiMesgQueue[];
extern u8 gPiMesgBuf[];
extern u32 gHudMoney;

/* (A gVehicles record: VehRecord, game/types.h.) */
#ifndef NON_MATCHING
extern VehRecord *gVehiclesEnd;
#endif


extern u8 romStaticSegment[]; /* the static segment (segment 1: display lists) in ROM */
extern u8 romStaticSegmentEnd[];
extern u8 gPendingTune;
extern u8 gStaticSegmentBuf[];
#ifndef NON_MATCHING
extern s32 gHeapPtr; /* heap pointer */
#ifndef NON_MATCHING
extern s32 gRdpOutputBuffer;
#endif
#endif
/* (gLevelInfo[]: LevelInfo, game/types.h) */
extern u8 gVehicleShadowPool[];

void hd_clear_framebuffer(Gfx *, s32 *);
void hd_close_display_list(Gfx *, s32 *);

#define TOPLEVEL_DL_SIZE 0xB5E

/* Rare's assert; line numbers are the original hd.c's */
#define HD_ASSERT(EX, line) \
    if (!(EX)) \
    debug_printf("\n\a --- ASSERTION FAULT - %s - %s, line %d\n\n", #EX, "hd.c", line)

/* (players[]: PlayerInfo, game/types.h) */
extern u8 gVehEntered;
extern s32 gEnterRefusedSound;
#ifndef NON_MATCHING
extern s32 gSfxBank;
#endif
s32 veh_enter(s32);

extern OSMesg gDmaMesgBuf[];
extern OSMesg gTextureDmaMesgBuf[];
extern u64 gSchedStack[];
extern OSMesg gTaskDoneMesgBuf[];
extern u8 gDepthBuffer[];
extern f32 gCamHeadingGoal;
extern u8 gCamTurnPos;
extern u8 gCamTurnNeg;
#define PHYS(x) ((u32)(x) & 0x1FFFFFFF)

u8 cam_fixed_heading_area(void);

#ifndef NON_MATCHING
extern s32 gLevelData;
#endif

extern f32 gFailSpinAngle;
extern f32 gFailSpinRate;
extern f32 gFailSpinScale;
extern u8 gFailSpinDone;

/* A queued "$N" popup, a destroyed object's value (hud_update_value_popups):
 * gValuePopups is a ring of 5 (gValuePopupHead .. gValuePopupTail); the head one is
 * shown, fading in and out over its timer, through gValuePopupValue/X/Y/Alpha/Forward. */
typedef struct {
    s32 value;
    s16 x;
    s16 y;
    u8 forward;
    u8 timer; /* frames left (30 at the start; it fades in over the first and out over the last 15) */
} ValuePopup; /* 0xC bytes */
extern ValuePopup gValuePopups[5];
extern u8 gValuePopupHead;
extern u8 gValuePopupTail;
extern s32 gValuePopupValue;
extern u8 gValuePopupAlpha;
extern s16 gValuePopupX;
extern s16 gValuePopupY;
extern u8 gValuePopupForward;

extern u8 gVehDrawBufferIndex;


extern u16 gCamFixedIconSprIds[]; /* sprite ids (spr_load) */
extern s32 gCamEyeHeight;
extern u8 gCamLiftBlocked;
extern f32 gCamTilt;
extern f32 gCamTurn;
extern s16 gCamPitch;
extern f32 gAttractCamZoom;
extern s32 gTimeInLevel;
extern u8 gCarrierDanger;
extern u8 gCamFixedIconSprite;
#ifndef NON_MATCHING
extern s32 gDriverPos;
#endif
void lvl_load_file(s32, s32, s32 *);
void lvl_alloc_dl_buffers(void);


extern s32 gLevelDl0[2];
extern s32 gLevelDl1[2];
extern s32 gLevelDl2[2];
extern s32 gLevelDl3[2];
extern s32 gLeaveRefusedSound;
u8 veh_exit_check(void);
s32 veh_is_loaded(u8);

extern u8 gCarrierDangerPrev;
extern s32 gCarrierAlarmSound;

/* (The Yoshi windows and their items: YoshiWindow, YoshiItem, game/types.h.) */

extern u8 romLagpRaw[]; /* lagp */
extern u8 romChimpRaw[]; /* chimp */
extern u8 romValleyRaw[]; /* valley */
extern u8 romFactRaw[]; /* fact */
extern u8 romDipRaw[]; /* dip */
extern u8 romBeetleRaw[]; /* beetle */
extern u8 romBonus1Raw[]; /* bonus1 */
extern u8 romBonus2Raw[]; /* bonus2 */
extern u8 romBonus3Raw[]; /* bonus3 */
extern u8 romLevel9Raw[]; /* level9 */
extern u8 romLevel10Raw[]; /* level10 */
extern u8 romLevel11Raw[]; /* level11 */
extern u8 romLevel12Raw[]; /* level12 */
extern u8 romLevel13Raw[]; /* level13 */
extern u8 romLevel14Raw[]; /* level14 */
extern u8 romLevel15Raw[]; /* level15 */
extern u8 romLevel16Raw[]; /* level16 */
extern u8 romLevel17Raw[]; /* level17 */
extern u8 romLevel18Raw[]; /* level18 */
extern u8 romLevel19Raw[]; /* level19 */
extern u8 romLevel20Raw[]; /* level20 */
extern u8 romLevel21Raw[]; /* level21 */
extern u8 romLevel22Raw[]; /* level22 */
extern u8 romLevel23Raw[]; /* level23 */
extern u8 romLevel24Raw[]; /* level24 */
extern u8 romLevel25Raw[]; /* level25 */
extern u8 romLevel26Raw[]; /* level26 */
extern u8 romLevel27Raw[]; /* level27 */
extern u8 romLevel28Raw[]; /* level28 */
extern u8 romLevel29Raw[]; /* level29 */
extern u8 romLevel30Raw[]; /* level30 */
extern u8 romLevel31Raw[]; /* level31 */
extern u8 romLevel32Raw[]; /* level32 */
extern u8 romLevel33Raw[]; /* level33 */
extern u8 romLevel34Raw[]; /* level34 */
extern u8 romLevel35Raw[]; /* level35 */
extern u8 romLevel36Raw[]; /* level36 */
extern u8 romLevel37Raw[]; /* level37 */
extern u8 romLevel38Raw[]; /* level38 */
extern u8 romLevel39Raw[]; /* level39 */
extern u8 romLevel40Raw[]; /* level40 */
extern u8 romLevel41Raw[]; /* level41 */
extern u8 romLevel42Raw[]; /* level42 */
extern u8 romLevel43Raw[]; /* level43 */
extern u8 romLevel44Raw[]; /* level44 */
extern u8 romLevel45Raw[]; /* level45 */
extern u8 romLevel46Raw[]; /* level46 */
extern u8 romLevel47Raw[]; /* level47 */
extern u8 romLevel48Raw[]; /* level48 */
extern u8 romLevel49Raw[]; /* level49 */
extern u8 romLevel50Raw[]; /* level50 */
extern u8 romLevel51Raw[]; /* level51 */
extern u8 romLevel52Raw[]; /* level52 */
extern u8 romLevel53Raw[]; /* level53 */
extern u8 romLevel54Raw[]; /* level54 */
extern u8 romLevel55Raw[]; /* level55 */
extern u8 romLevel56Raw[]; /* level56 */
extern u8 romLevel57Raw[]; /* level57 */
extern u8 romLevel58Raw[]; /* level58 */
extern u8 romLevel59Raw[]; /* level59 */

extern s32 gShadowVtxCount;

#ifndef NON_MATCHING
extern u32 gLevelSegment8;
#endif
extern u16 gShuttleViewGlyphs[]; /* glyph lists */
extern u16 gMissileViewGlyphs[];
extern u16 gTurnPagesGlyphs[];
extern u16 gMoveMapGlyphs[];
void veh_draw_shadows(Gfx **, DynamicBuf *, u8);
void veh_draw_chopper_shadow(Gfx **, DynamicBuf *);
void veh_update_fail_spin(void);
void cam_update(Mtx *, LookAt *, Mtx *);
#define SEG2(off) ((u8 *) seg2Base + (off))

#define MS_ASSERT(EX, line) \
    if (!(EX)) \
    debug_printf("\n\a --- ASSERTION FAULT - %s - %s, line %d\n\n", #EX, "./master_switch.c", line)
extern u16 gEnterNameGlyphs[];
extern u16 gEraseSaveGlyphs[];
extern u16 gQuitGameGlyphs[];
extern u16 gGuestPlayerGlyphs[];
extern u8 gResultsGrade;
extern u32 gReplayStartTime;
extern u8 gRankGained;
extern s32 gAttractPicIndex;
extern u32 gFrameCpuTime;
extern u32 gFrameStartLag;
extern u8 gWorldEntered;
extern u8 gCheatResultPending;
extern u8 gReplayStatus[];
#define LEVEL_SAVE_SIZE 0x40
void hd_game_frame(void);
void hd_init(void);
void hd_check_stack(void);
#define MODE_PAGE(m) gYoshiWindows[gYoshiResultsWindowByKind[lowest_set_bit(m)]]
#define MODE_ENTRY(m) gYoshiFeItems[MODE_PAGE(m).firstItem + MODE_PAGE(m).itemCount - 2]

extern s32 gMapViewScrollZ;
extern u8 gCamZoomInLock;
extern s32 gMapViewScrollSpeed;
extern f32 gCamZoom;
extern u8 gCamWindowView;
#ifndef NON_MATCHING
extern s32 gMusicPlayer;
#endif
#define NUM_TEXTURE_DMAS 0x90
void cull_start(Mtx *, Mtx *);
void veh_apply_switch(void);
void veh_leave(void);
void veh_update_others(void);
void veh_update_player(void);
void carrier_update_proximity(u32);
void veh_update_riders(void);
void hud_update_value_popups(void);
void hud_tick_money_counter(void);
Gfx *hd_build_frame_dl(DynamicBuf *, s32 *);
void hd_handle_yoshi_selection(u16 *);
#define LEVEL_DONE(l) (players[playerNumber].medal[l] > 0 && players[playerNumber].medal[l] < 6) ? 1 : 0

/* (end of declarations) */

/* hd_code's entry (init jumps here): osInitialize, the debug command line
 * (16 words at PI address 0xFFB000, parse_cmdline), then the idle thread
 * (0x802447C0). */
void hd_boot(void) {
    u32 i;
    s32 pad[2];
    u32 addr;
    u32 buf[16];
    s32 pad2[2];

    osInitialize();
    for (addr = 0xFFB000, i = 0; i < 16; i++, addr += 4) {
        osPiRawReadIo(addr, &buf[i]);
    }
    parse_cmdline((u8 *) buf);
    osCreateThread(&gIdleThread, 1, hd_idle_thread, NULL, gIdleThreadStack + 0x40, 10);
    osStartThread(&gIdleThread);
}

/* The idle thread: starts the PI manager and the main thread (twice unless
 * -d), drops to priority 0 and spins (0x80244870). */
void hd_idle_thread(void *arg0) {
    s32 pad;

    osDpSetStatus(4);
    osCreatePiManager(0x96, (OSMesgQueue *) gPiMesgQueue, (void * N64P *) gPiMesgBuf, 0xC2);
    osCreateThread(&gMainThread, 3, hd_main_thread, arg0, gMainThreadStack + 0x400, 10);
    osStartThread(&gMainThread);
    if (gDebugFlagD == 0) {
        osStartThread(&gMainThread);
    }
    osSetThreadPri(NULL, 0);
    while (1) {
    }
}

/* The main thread: hd_init, then forever: switch to gNextGameMode (the
 * set-up of each mode, master_switch.c's asserts; a switch can queue the next
 * one in gGameModeAfterNext), then run frames of the mode (hd_game_frame or
 * the front end's) until a new mode is asked for (0x80244930). */
void hd_main_thread(void *arg) {
    s32 i;
    s32 start2;
    void * N64P msg;
    u8 newPlayer;
    s32 start;

    hd_init();
    while (1) {
        gPendingTune = 0;
        do {
            debug_printf("game mode switch from %d to %d\n", lowest_set_bit(gGameMode), lowest_set_bit(gNextGameMode));
            gGameModeAfterNext = 0;
            switch (gNextGameMode) {
                case MODE_BOOT_SCREEN2:
                    hd_mode_setup();
                    fe_boot_screen_init(2);
                    break;
                case MODE_BOOT_SCREEN1:
                    hd_mode_setup();
                    fe_boot_screen_init(1);
                    start2 = gScheduler.frameCount;
                    while ((u32) (gScheduler.frameCount - start2) < 15) {
                        PORT_SPIN();
                    }
                    break;
                case MODE_PAK_FILES:
                    fe_pak_list_files();
                    yoshi_demand(YOSHI_DEMAND_OPEN | YW_PAK_FILES);
                    gPendingTune = music_tune_for_mode(gNextGameMode);
                    break;
                case MODE_PAK_MANAGER:
                    hd_mode_setup();
                    title_setup_logo();
                    fe_bg_load(1);
                    osSendMesg(&gPakRequestQueue, (OSMesg) 0x01000001, 1);
                    yoshi_demand(YOSHI_DEMAND_OPEN | YW_PAK_CHECK);
                    break;
                case MODE_LOAD_GAME:
                    gPendingTune = music_tune_for_mode(gNextGameMode);
                    title_setup_logo();
                    fe_player_build_load_menu();
                    fe_player_ticker_reset(playerNumber);
                    yoshi_demand(YOSHI_DEMAND_OPEN | YW_PLAYER_MENU);
                    break;
                case MODE_PAK_CHECK:
                    osSendMesg(&gPakRequestQueue, (OSMesg) 0x01000001, 1);
                    fe_player_ticker_reset(4);
                    yoshi_demand(YOSHI_DEMAND_OPEN | YW_PAK_CHECK);
                    break;
                case MODE_BOOT_CHECK:
                    snd_stop_all();
                    hd_mode_setup();
                    tex_load_table();
                    title_setup_logo();
                    fe_bg_load(1);
                    osSendMesg(&gPakRequestQueue, (OSMesg) 0x0100000F, 1);
                    osRecvMesg(&pakToGameMessageQ, &msg, 1);
                    if (msg != NULL || gIgnorePak) {
                        gUseEeprom = 1;
                    } else {
                        gUseEeprom = 0;
                    }
                    gIgnorePak = 0;
                    if (!gUseEeprom) {
                        fe_player_panel_init(4);
                        gGameModeAfterNext = MODE_PAK_CHECK;
                    } else {
                        playerNumber = 0;
                        gLoadedPlayer = 0;
                        playerNumberAtStart = 0, osSendMesg(&gPakRequestQueue, (OSMesg) 0x01000010, 1);
                        osRecvMesg(&pakToGameMessageQ, &gEepromPresent, 1);
                        if (gEepromPresent == NULL) {
                            debug_printf("NO EE PRESENT! - USING DUMMY EE\n");
                        }
                        osSendMesg(&gPakRequestQueue, (OSMesg) 0x01000006, 1);
                        osRecvMesg(&pakToGameMessageQ, &msg, 1);
                        if (msg == NULL) {
                            msg = (void *) N64_IPTR(fe_pak_restore_from_eeprom());
                        }
                        if (msg != NULL) {
                            fe_player_new_game(playerNumber, 1, 1);
                            gGameModeAfterNext = MODE_NAME_ENTRY;
                        } else {
                            gSlotState[playerNumber] = 1;
                            gGameModeAfterNext = MODE_WORLD;
                        }
                    }
                    break;
                case MODE_REGRADE_NOTICE:
                    hd_mode_setup();
                    tex_load_table();
                    title_setup_logo();
                    fe_bg_load(4);
                    fe_player_panel_init(4);
                    yoshi_demand(YOSHI_DEMAND_OPEN | YW_REGRADE_NOTICE);
                    break;
                case MODE_ATTRACT_START:
                    gRecDemoNumber = -1;
                    gRecDemoNoLogo = 1;
                    gWorldEntered = 0;
                    gGameModeAfterNext = MODE_ATTRACT;
                    gIgnorePak = 0;
                    fe_player_update_level_kinds();
                    break;
                case MODE_ATTRACT:
                    switch (gRecDemoNumber) {
                        case 1:
                        case 3:
                        case 4:
                        case 6:
                            if (gGameMode == MODE_ATTRACT) {
                                gNextGameMode = MODE_ATTRACT_PICTURE;
                                hd_mode_setup();
                                fe_bg_load(7);
                                fe_attract_pic_load(gAttractPicIndex & 3);
                                yoshi_demand(YOSHI_DEMAND_OPEN | YW_ATTRACT_PICTURE);
                                fe_player_panel_init(gAttractPicIndex & 3);
                                gAttractPicIndex++;
                                break;
                            }
                        default:
                        gRecDemoNumber++;
                        if (gRecDemoNumber == 9) {
                            gRecDemoNumber = 0;
                            gRecDemoNoLogo = 0;
                        } else {
                            gRecDemoNoLogo ^= 1;
                        }
                        gPadDisplayAlpha = 0;
                        hd_mode_setup();
                        title_setup_logo();
                        hud_load_pad_image();
                        rec_start_playback(gRecDemoNumber, &levelno);
                        if (gRecDemoNumber == 0) {
                            hud_load_demo_banner();
                        }
                        hd_setup_level(0);
                        break;
                    }
                    break;
                case MODE_WORLD:
                    if (playerNumber != playerNumberAtStart) {
                        gLoadedPlayer = playerNumberAtStart;
                        playerNumber = playerNumberAtStart;
                    }
                    hd_load_front_end();
                    debug_printf("World screen centred on level %d\n", levelno);
                    playerNumber = playerNumberAtStart;
                    fe_player_update_level_kinds();
                    if (!gWorldEntered) {
                        gWorldEntered = 1;
                        gRouteRevealFlags = 0;
                        gLevelFirstClear = 0;
                        fe_open_reachable_levels(8);
                        levelno = players[playerNumber].levelno;
                        debug_printf("going to level %d\n", levelno);
                    }
                    if ((gGhostRetry = 0, ((players[playerNumber].medal[levelno] > 0 && players[playerNumber].medal[levelno] < 6) ? 1 : 0)) &&
                        levelno >= 0x2B && levelno < 0x2E) {
                        if (!((players[playerNumber].medal[levelno + 1] > 0 && players[playerNumber].medal[levelno + 1] < 6) ? 1 : 0)) {
                            levelno++;
                        }
                    }
                    hd_mode_setup();
                    fe_player_save_pending();
                    osViBlack(1);
                    tex_load_table();
                    fe_globe_init(levelno);
                    break;
                case MODE_ACADEMY_MESSAGE:
                    gPendingTune = music_tune_for_mode(gNextGameMode);
                    break;
                case MODE_UNUSED_WINDOW_13:
                    yoshi_demand(YOSHI_DEMAND_OPEN | YW_UNUSED_13);
                    break;
                case MODE_NO_CONTROLLER:
                    hd_mode_setup();
                    fe_bg_load(1);
                    title_setup_logo();
                    yoshi_demand(YOSHI_DEMAND_OPEN | YW_NO_CONTROLLER);
                    break;
                case MODE_BEST_TIMES:
                    hd_mode_setup();
                    fe_bg_load(3);
                    fe_player_panel_init(playerNumber);
                    title_setup_logo();
                    gBestTimesReturnMode = gGameMode;
                    yoshi_demand(YOSHI_DEMAND_OPEN | YW_BEST_TIMES);
                    break;
                case MODE_BEST_TIMES_RETURN:
                    fe_player_ticker_set_text(playerNumber, 1);
                    fe_best_color_items();
                    yoshi_demand(YOSHI_DEMAND_OPEN | YW_BEST_TIMES);
                    gNextGameMode = MODE_BEST_TIMES;
                    break;
                case MODE_LEVEL_START:
                    hd_mode_setup();
                    if (gLevelInfo[levelno].kind != LEVEL_KIND_MISSION) {
                        hd_setup_level(0);
                        stats_copy_result((u8 *) &nss, (u8 *) &gScoreOld);
                        stats_copy_result((u8 *) &nss, (u8 *) &gScoreRes);
                        gPauseRequest = 1;
                    } else {
                        if ((players[playerNumber].medal[levelno] > 0 && players[playerNumber].medal[levelno] < 6) ? 1 : 0) {
                            hd_setup_level((s32) pakBuffer);
                        } else {
                            hd_setup_level(0);
                        }
                        stats_copy_result((u8 *) &nss, (u8 *) &gScoreOld);
                        stats_copy_result((u8 *) &nss, (u8 *) &gScoreRes);
                        gGameModeAfterNext = MODE_LEVEL;
                    }
                    break;
                case MODE_LEVEL:
                    switch (gGameMode) {
                        case MODE_ACADEMY_MARKER:
                            music_pop_tune();
                            snd_set_mode_slot_volumes(gNextGameMode);
                            gUnpauseRequest = 1;
                            break;
                        case MODE_MAP_VIEW:
                            gCamSnap = 1;
                            break;
                        case MODE_LEVEL_START:
                            if (gLevelInfo[levelno].kind != LEVEL_KIND_MISSION) {
                                gPendingTune = music_tune_for_mode(gNextGameMode);
                            }
                            gUnpauseRequest = 1;
                            gUnpauseRequest &= !yoshi_show_hint(YW_HINT_START_A);
                            /* one line: the line break changes as1's schedule */
                            gUnpauseRequest &= !yoshi_show_hint(YW_HINT_START_B); if ((((players[playerNumber].medal[levelno] > 0 && players[playerNumber].medal[levelno] < 6) ? 1 : 0)) &&
                                gLevelInfo[levelno].kind == LEVEL_KIND_MISSION) {
                                gUnpauseRequest &= !yoshi_show_hint(YW_HINT_START_DONE);
                            } else {
                                gUnpauseRequest &= !yoshi_show_hint(YW_HINT_START_NEW);
                            }
                            gPauseRequest = !gUnpauseRequest;
                            debug_printf("Unpause at start = %d\n", gUnpauseRequest);
                            gLevelStartTime = ((s32) PORT_GVI("hd_main_thread:4A58", gScheduler.retraceCount));
                            gPlayFrames = 0;
                            rec_start_recording();
                            debug_printf("snew ip=%8d : tc=%5d : bd=%2d : cr=%2d : rt=%3d : coin=%1d : bdn=%1d\n",
                                          nss.ip, nss.tc, nss.bd, nss.cr, nss.rt,
                                          nss.coin, nss.bdn);
                            debug_printf("sold ip=%8d : tc=%5d : bd=%2d : cr=%2d : rt=%3d : coin=%1d : bdn=%1d\n",
                                          gScoreOld.ip, gScoreOld.tc, gScoreOld.bd, gScoreOld.cr, gScoreOld.rt,
                                          gScoreOld.coin, gScoreOld.bdn);
                            debug_printf("sres ip=%8d : tc=%5d : bd=%2d : cr=%2d : rt=%3d : coin=%1d : bdn=%1d\n",
                                          gScoreRes.ip, gScoreRes.tc, gScoreRes.bd, gScoreRes.cr, gScoreRes.rt,
                                          gScoreRes.coin, gScoreRes.bdn);
                            debug_printf("srs2 ip=%8d : tc=%5d : bd=%2d : cr=%2d : rt=%3d : coin=%1d : bdn=%1d\n",
                                          gScoreRs2.ip, gScoreRs2.tc, gScoreRs2.bd, gScoreRs2.cr, gScoreRs2.rt,
                                          gScoreRs2.coin, gScoreRs2.bdn);
                            break;
                    }
                    break;
                case MODE_MAP_VIEW:
                    gCamSnap = 1;
                    break;
                case MODE_LEVEL_STATUS:
                    newPlayer = 0;
                    switch (gGameMode) {
                        case MODE_GLOBE_SEQ:
                            hd_load_front_end();
                            gPendingTune = music_tune_for_mode(gNextGameMode);
                        case MODE_WORLD:
                            newPlayer = stats_save_status();
                            break;
                        case MODE_BEST_TIMES:
                        case MODE_BEST_TIMES_UNUSED:
                        case MODE_GUEST_PLAYER:
                            if (playerNumber != gLoadedPlayer) {
                                gLoadedPlayer = playerNumber, debug_printf("switching to new player ...........\n");
                                stats_save_status();
                            }
                            break;
                    }
                    hd_mode_setup();
                    if (newPlayer) {
                        osSendMesg(&gPakRequestQueue, (OSMesg) ((levelno << 8) | 0xD | (playerNumber << 16)), 1);
                    }
                    MODE_ENTRY(gLevelKind).flags &= ~1;
                    MODE_ENTRY(gLevelKind).flags |= 0x800;
                    yoshi_set_target_icons();
                    yoshi_demand(gYoshiResultsWindowByKind[lowest_set_bit(gLevelKind)] | YOSHI_DEMAND_OPEN);
                    MODE_PAGE(gLevelKind).sel = MODE_PAGE(gLevelKind).firstItem + MODE_PAGE(gLevelKind).itemCount - 3;
                    MODE_PAGE(gLevelKind).flags &= ~8;
                    stats_copy_result((u8 *) &nss, (u8 *) &gScoreOld);
                    gResultsGrade = fe_stats_grade_level(&gRankGained, 1, 0), fe_player_panel_init(playerNumber);
                    fe_player_medals_init(gResultsGrade);
                    fe_player_medals_show_quiet(gResultsGrade);
                    fe_bg_load(1);
                    gUnlockCount = fe_stats_count_unlocks();
                    if (gUnlockCount != -1) {
                        fe_comm_point_load();
                    }
                    break;
                case MODE_INTRO_FLY:
                    hd_mode_setup();
                    if (gGameMode == MODE_WORLD) {
                        osRecvMesg(&pakToGameMessageQ, NULL, 1);
                    }
                    osViBlack(1);
                    hd_setup_level(0);
                    chopper_fly_to_carrier();
                    if (levelno == 0x32) {
                        yoshi_demand(YOSHI_DEMAND_OPEN | YW_SHUTTLE_INTRO);
                    }
                    break;
                case MODE_PROMOTION:
                    hd_mode_setup();
                    start = gScheduler.frameCount;
                    while ((u32) (gScheduler.frameCount - start) < 15) {
                        PORT_SPIN();
                    }
                    fe_promo_init();
                    fe_bg_load(3);
                    gUnlockCount = fe_stats_count_unlocks();
                    if (gUnlockCount != -1) {
                        fe_comm_point_load();
                    }
                    fe_player_panel_init(playerNumber);
                    fe_player_medals_init(gResultsGrade);
                    break;
                case MODE_RESULTS:
                    switch (gGameMode) {
                        case MODE_REPLAY:
                        case MODE_REPLAY_CARRIER_END:
                        case MODE_VEHICLE_SELECT:
                            hd_mode_setup();
                            fe_bg_load(1);
                            gUnlockCount = fe_stats_count_unlocks();
                            if (gUnlockCount != -1) {
                                fe_comm_point_load();
                            }
                            fe_player_panel_init(playerNumber);
                            stats_copy_result((u8 *) &gScoreRs2, (u8 *) &nss);
                            gResultsGrade = fe_stats_grade_level(&gRankGained, 1, 0);
                            fe_player_medals_init(gResultsGrade);
                            fe_player_medals_show_quiet(gResultsGrade);
                            break;
                        case MODE_PROMOTION:
                            gPendingTune = music_tune_for_mode(gNextGameMode);
                            break;
                        case MODE_BEST_TIMES:
                        case MODE_BEST_TIMES_UNUSED:
                        case MODE_GUEST_PLAYER:
                            if (playerNumber != gLoadedPlayer) {
                                debug_printf("switching to new player ...........\n");
                                gLoadedPlayer = playerNumber;
                                gLevelFirstClear = 0;
                                stats_save_status();
                            }
                            hd_mode_setup();
                            gUnlockCount = fe_stats_count_unlocks();
                            if (gUnlockCount != -1) {
                                fe_comm_point_load();
                            }
                            fe_player_panel_init(playerNumber);
                            gResultsGrade = fe_stats_grade_level(&gRankGained, 1, 0);
                            fe_bg_load(1);
                            fe_player_medals_init(gResultsGrade);
                            fe_player_medals_show_quiet(gResultsGrade);
                            break;
                        default:
                            MS_ASSERT(!saveIt[playerNumber] || saveIt[playerNumber]==levelno+1, 488);
                            saveIt[playerNumber] = levelno + 1;
                            if (gLevelDone) {
                                obj_tally(gLevelInfo[levelno].kind == LEVEL_KIND_BUILDINGS || gLevelInfo[levelno].kind == LEVEL_KIND_MISSION_TIMED);
                            }
                            gLevelFirstClear = !((players[playerNumber].medal[levelno] > 0 && players[playerNumber].medal[levelno] < 6) ? 1 : 0);
                            if (playerNumber == playerNumberAtStart) {
                                gRouteRevealFlags |= gLevelFirstClear;
                            }
                            if (gLevelKind == LEVEL_KIND_MISSION && !gLevelFirstClear) {
                                for (i = 0; i < 0x40; i++) {
                                    gReplayStatus[i] = pakBuffer[i];
                                }
                            }
                            commpt_save_flags();
                            academy_save_markers();
                            if (gLevelKind == LEVEL_KIND_MISSION) {
                                MS_ASSERT(record_status(pakBuffer)<=LEVEL_SAVE_SIZE-4, 513);
                                MS_ASSERT(!saveLevel || saveLevel==levelno+1, 515);
                                saveLevel = levelno + 1;
                                if ((u32) (stats_frames_to_tenths(gTimeInLevel) + nss.tc) >= 60000) {
                                    nss.tc = 59999;
                                } else {
                                    nss.tc = stats_frames_to_tenths(gTimeInLevel) + nss.tc;
                                }
                            } else if (gLevelDone) {
                                hud_race_time_left();
                                nss.tc = gLevelInfo[levelno].medalTimes[3] - gTimeLeft;
                            } else {
                                nss.tc = 0xFFFF;
                            }
                            MS_ASSERT(nss.tc, 530);
                            if (nss.tc == 0) {
                                nss.tc = 1;
                            }
                            hd_mode_setup();
                            if (gCheatResultPending) {
                                nss.bd = gBuildingCount;
                                gCheatResultPending = 0;
                            }
                            if (gLevelFirstClear) {
                                stats_copy_result((u8 *) &nss, (u8 *) &gScoreOld);
                            } else {
                                stats_copy_result((u8 *) &gScoreRes, (u8 *) &gScoreOld);
                            }
                            gResultsGrade = fe_stats_grade_level(&gRankGained, 1, 1), stats_copy_result((u8 *) &nss, (u8 *) &gScoreRs2);
                            if (gRankGained) {
                                gGameModeAfterNext = MODE_PROMOTION;
                                gPromoCueDelay = 0xDC;
                                gPendingTune = 0;
                            } else {
                                fe_player_medals_init(gResultsGrade);
                                fe_bg_load(1);
                                gUnlockCount = fe_stats_count_unlocks();
                                if (gUnlockCount != -1) {
                                    fe_comm_point_load();
                                }
                                fe_player_panel_init(playerNumber);
                            }
                            MODE_ENTRY(gLevelKind).flags |= 1;
                            MODE_ENTRY(gLevelKind).flags &= ~0x800;
                            break;
                    }
                    if (gLevelFirstClear && playerNumber == playerNumberAtStart) {
                        MODE_PAGE(gLevelKind).sel = MODE_PAGE(gLevelKind).firstItem + MODE_PAGE(gLevelKind).itemCount - 1;
                    } else {
                        MODE_PAGE(gLevelKind).sel = MODE_PAGE(gLevelKind).firstItem + MODE_PAGE(gLevelKind).itemCount - 3;
                    }
                    MODE_PAGE(gLevelKind).flags |= 8;
                    yoshi_set_target_icons();
                    if (gUseEeprom) {
                        gYoshiFeItems[29].flags &= ~1;
                    }
                    yoshi_demand(gYoshiResultsWindowByKind[lowest_set_bit(gLevelKind)] | YOSHI_DEMAND_OPEN);
                    break;
                case MODE_REPLAY:
                    if (gGameMode & (MODE_CARRIER_END | MODE_TIMED_END)) {
                        hd_load_front_end();
                        nss.tc = stats_frames_to_tenths(gTimeInLevel), stats_copy_result((u8 *) &nss, (u8 *) &gScoreOld);
                        fe_stats_grade_level(&gRankGained, 0, 1);
                    }
                    gYoshiGameItems[24].flags |= 1;
                    gYoshiGameItems[24].flags &= ~0x800;
                    gYoshiGameItems[23].flags |= 1;
                    gYoshiGameItems[23].flags &= ~0x800;
                    gYoshiGameItems[17].flags &= ~1;
                    gYoshiGameItems[17].flags |= 0x800;
                    if ((players[playerNumber].medal[levelno] > 0 && players[playerNumber].medal[levelno] < 6) ? 1 : 0) {
                        gYoshiWindows[YW_STATUS].sel = 0x18, gYoshiWindows[7].sel = 0x22;
                    } else {
                        gYoshiWindows[YW_STATUS].sel = 0x17, gYoshiWindows[7].sel = 0x21;
                    }
                    gReplayMenuShown = 0;
                    gReplayStartTime = PORT_GVI("hd_main_thread:4A54", gScheduler.frameCount);
                    hd_mode_setup();
                    if (gLevelInfo[levelno].kind == LEVEL_KIND_MISSION &&
                        ((players[playerNumber].medal[levelno] > 0 && players[playerNumber].medal[levelno] < 6) ? 1 : 0) &&
                        !gLevelFirstClear) {
                        hd_setup_level((s32) gReplayStatus);
                    } else {
                        hd_setup_level(0);
                    }
                    yoshi_set_target_icons();
                    rec_start_replay();
                    break;
                case MODE_VEHICLE_SELECT:
                    hd_mode_setup();
                    if (gGameMode == MODE_WORLD) {
                        osRecvMesg(&pakToGameMessageQ, NULL, 1);
                    }
                    osViBlack(1);
                    tex_load_table();
                    if (fe_digger_init()) {
                        fe_bg_load(2);
                    } else {
                        gGameModeAfterNext = MODE_LEVEL_START;
                    }
                    break;
                case MODE_NAME_ENTRY:
                    osSendMesg(&gPakRequestQueue, (OSMesg) ((playerNumber << 16) | 0x14 | 0x01000000), 1);
                    osRecvMesg(&pakToGameMessageQ, NULL, 1);
                    fe_player_name_entry_open("ENTER NAME!", gEnterNameGlyphs, 7, 0x1E, (char *) &players[playerNumber]);
                    yoshi_demand(YOSHI_DEMAND_OPEN | YW_NAME_ENTRY);
                    title_setup_logo();
                    gPendingTune = music_tune_for_mode(gNextGameMode);
                    break;
                case MODE_QUIT_GAME:
                    hd_mode_setup();
                    fe_bg_load(1);
                    gYoshiFeItems[9].text = "QUIT GAME!", gYoshiFeItems[9].jtext = gQuitGameGlyphs, gYoshiFeItems[9].w = gYoshiFeItems[9].h = 22;
                    yoshi_demand(YOSHI_DEMAND_OPEN | YW_CONFIRM);
                    break;
                case MODE_ACADEMY_MARKER:
                    music_push_tune(MUS_ACADEMY_MARKER);
                    snd_set_slot_volume(0, 0);
                    academy_reset_window_cursor();
                    yoshi_demand(YOSHI_DEMAND_OPEN | YW_ACADEMY_MARKER);
                    gPauseRequest = 1;
                    break;
                case MODE_GLOBE_SEQ:
                    seq_enter_start_level();
                    gPendingTune = music_tune_for_mode(gNextGameMode);
                    hd_mode_setup();
                    seq_start_level();
                    if (gGameMode == MODE_WORLD || levelno == 0x2F) {
                        osRecvMesg(&pakToGameMessageQ, NULL, 1);
                    }
                    osViBlack(1);
                    hd_setup_level(0);
                    break;
                case MODE_ERASE_SAVE:
                    hd_mode_setup();
                    osSendMesg(&gPakRequestQueue, (OSMesg) 0x01000010, 1);
                    osRecvMesg(&pakToGameMessageQ, &gEepromPresent, 1);
                    if (gEepromPresent != NULL) {
                        title_setup_logo();
                        fe_bg_load(1);
                        gYoshiFeItems[9].text = "ERASE SAVED GAME!", gYoshiFeItems[9].jtext = gEraseSaveGlyphs, gYoshiFeItems[9].w = gYoshiFeItems[9].h = 20;
                        yoshi_demand(YOSHI_DEMAND_OPEN | YW_CONFIRM);
                    } else {
                        gGameModeAfterNext = MODE_BOOT_SCREEN1;
                    }
                    break;
                case MODE_GUEST_PLAYER:
                    gYoshiFeItems[9].text = "BECOME GUEST PLAYER:", gYoshiFeItems[9].jtext = gGuestPlayerGlyphs, gYoshiFeItems[9].w = gYoshiFeItems[9].h = 20;
                    yoshi_demand(YOSHI_DEMAND_OPEN | YW_CONFIRM);
                    break;
                case MODE_ERASE_MENU:
                    fe_player_build_erase_menu();
                    yoshi_demand(YOSHI_DEMAND_OPEN | YW_PLAYER_MENU);
                    break;
                case MODE_LEVEL_CLEAR:
                    gPauseRequest = 1;
                    music_play_clear_tune();
                    break;
            }
            gPrevGameMode = gGameMode;
            gGameMode = gNextGameMode;
            gNextGameMode = gGameModeAfterNext;
        } while (gNextGameMode != 0);
        if (gPendingTune) {
            music_start_tune(gPendingTune);
        }
        while (gNextGameMode == 0) {
            gFrameStartTime = osGetTime();
            font_age_cache();
            if (gGameMode & MODE_WORLD) {
                gFrameStartLag = (osGetTime() - gLastRetraceTime) / 7825;
                fe_globe_frame();
                gFrameCpuTime = (osGetTime() - gFrameStartTime) / 7825;
                if (gRspSlotBusy[2]) {
                    rsp_wait_task(0x4D2);
                }
                if (gRspSlotBusy[0]) {
                    rsp_wait_task(0x4D2);
                }
                rsp_wait_task(0x4D2);
            } else if (gGameMode & (MODE_LEVEL_STATUS | MODE_PAK_CHECKED | MODE_LOAD_GAME | MODE_NEW_PLAYER | MODE_NAME_ENTRY | MODE_ERASE_CONFIRM | MODE_ERASE_PLAYER_CHOSEN | MODE_LOAD_GAME_RETURN | MODE_ERASE_MENU | MODE_TO_ERASE_MENU | MODE_NAME_ENTERED | MODE_PLAYER_CHOSEN | MODE_RESULTS | MODE_PAK_CHECK | MODE_PROMOTION | MODE_PRE_PROMOTION | MODE_PAK_FILES | MODE_UNUSED_33 | MODE_UNUSED_WINDOW_13 | MODE_PAK_MANAGER_CHECKED | MODE_PAK_FILE_CHOSEN | MODE_PAK_DELETE_CONFIRM | MODE_PAK_FILE_DELETED | MODE_UNUSED_41 | MODE_BEST_TIMES | MODE_PAK_MANAGER | MODE_ACADEMY_MESSAGE | MODE_ATTRACT_PICTURE | MODE_REGRADE_NOTICE | MODE_REGRADE_NOTICE_END | MODE_UNUSED_52 | MODE_BOOT_CHECK | MODE_ERASE_SAVE | MODE_BEST_TIMES_UNUSED | MODE_GUEST_PLAYER | MODE_NO_CONTROLLER | MODE_QUIT_GAME | MODE_LOAD_GAME_EXIT)) {
                gFrameStartLag = (osGetTime() - gLastRetraceTime) / 7825;
                fe_back_loop();
                gFrameCpuTime = (osGetTime() - gFrameStartTime) / 7825;
                rsp_wait_task(0x4D2);
            } else if (gGameMode & MODE_VEHICLE_SELECT) {
                fe_digger_frame();
                rsp_wait_task(0x4D2);
            } else if (gGameMode & (MODE_BOOT_SCREEN1 | MODE_BOOT_SCREEN2)) {
                fe_boot_screen_frame();
                rsp_wait_task(0x4D2);
            } else {
                gFrameStartLag = (osGetTime() - gLastRetraceTime) / 7825;
                hd_game_frame();
                gFrameCpuTime = (osGetTime() - gFrameStartTime) / 7825;
                if (gRspSlotBusy[1]) {
                    rsp_wait_task(0x61F);
                }
                if (gRspSlotBusy[2]) {
                    rsp_wait_task(0x54D);
                }
                rsp_wait_task(0x4D2);
            }
            hd_check_stack();
            gFramesInMode++;
            gPlayFrames++;
            if (!gPaused) {
                gUnpausedFrames++;
            }
            PORT_VIEWER_FRAME(); /* bc.exe's level viewer: a level it asked for (game/port.h) */
        }
        if ((gGameMode & (MODE_LEVEL | MODE_MAP_VIEW)) && (gCarrierHit || gLevelDone)) {
            rec_record(0, 0, 0);
        }
        HD_ASSERT(!areWeFading(), 627);
        switch (gNextGameMode) {
            case MODE_SEQ_SKIP:
                seq_finish(1);
                break;
            case MODE_SEQ_END:
                seq_finish(0);
                break;
            case MODE_ACADEMY_NEXT:
                academy_next_mode();
                break;
        }
        if (playerNumber == playerNumberAtStart && gNextGameMode == MODE_WORLD && academy_advance_state()) {
            academy_announce_state();
        }
    }
}

/* One frame of the in-game modes: hands the frame built last time to the RSP
 * (rsp_send_task), pause handling, the updates (camera zoom, vehicles,
 * objects, mission state), then this frame's display list (hd_build_frame_dl
 * and the overlays) (0x802475D8). */
void hd_game_frame(void) {
    u32 vis;
    s32 i;
    s32 j;
    u8 found;
    Gfx *gdl;
    u8 zoom;
    s32 pos;
    s32 lo;
    s32 hi;
    s16 alpha;

    if (gDebugCheats && (gContButtons & R_CBUTTONS) && !(gContPrevButtons & R_CBUTTONS) && (gGameMode & (MODE_LEVEL | MODE_MAP_VIEW))) {
        gLevelDoneRequest = 1;
        cmo_hit_request = 0;
        if (gContButtons & U_CBUTTONS) {
            if (gLevelKind != LEVEL_KIND_MISSION || !(LEVEL_DONE(levelno))) {
                gLevelDoneRequest = 0;
                cmo_hit_request = 1;
            }
        }
        if ((gContButtons & Z_TRIG) && gLevelDoneRequest) {
            gCheatAllDestroyed = 1;
            gScoreRdus = gRduCount;
            gScorePeople = gPeopleTotal;
            gCheatResultPending = 1;
            stats_set_level_flag(2);
            stats_set_level_flag(1);
        }
        if ((LEVEL_DONE(levelno)) && gLevelKind == LEVEL_KIND_MISSION && gLevelDoneRequest) {
            gPauseRequest = 1;
            fade_to_mode(MODE_RESULTS);
        }
    }
    nextdma = 0;
    no_palette_dmas = 0;
    gCarrierContact = 0;
    if (gFramesInMode) {
        if (gBufferIndex == 0) {
            if (gFramesInMode >= 6) {
                cull_start(&gDynamicBufs[gBufferIndex].persp, &gDynamicBufs[gBufferIndex].cullView);
            }
        } else {
            i = 0;
            found = 0;
            while (!found) {
                if (gVehicles[i].type == gCurVehicle) {
                    found = 1;
                } else {
                    i++;
                }
            }
            j = 0;
            found = 0;
            while (!found) {
                if (gVehicleShadows[j].type == gCurVehicle) {
                    found = 1;
                } else {
                    j++;
                }
            }
            veh_render_shadow_tex(gVehicleShadows[j].tex, gVehicleShadows[j].x, gVehicleShadows[j].bodyY, gVehicleShadows[j].z,
                          gVehicleShadows[j].renderDist, (Gfx *) N64_IPTR(((gVehicles + i)->shadowDl)), (gVehicles + i)->seg6, (gVehicles + i)->seg7Odd);
        }
    }
    i = 0;
    found = 0;
    while (!found) {
        if (gVehicles[i].type == gCurVehicle) {
            found = 1;
        } else {
            i++;
        }
    }
    if (gBufferIndex == 0) {
        mb_render_view(gPlayerX, gPlayerY, gPlayerZ, gCamEyeX, gCamEyeY, gCamEyeZ, (void *) N64_IPTR((gVehicles[i].blurDl)),
                      gVehicles[i].seg6, gVehicles[i].seg7Even, gVehicles[i].alpha);
    } else {
        mb_render_view(gPlayerX, gPlayerY, gPlayerZ, gCamEyeX, gCamEyeY, gCamEyeZ, (void *) N64_IPTR((gVehicles[i].blurDl)),
                      gVehicles[i].seg6, gVehicles[i].seg7Odd, gVehicles[i].alpha);
    }
    gFrameDrawTime = osGetTime();
    cont_start_read();
    rsp_send_task((u64 *) (gDynamicBufs[gBufferIndex].dl), gFrameDlLen, 3, 1, 0x4D2, 1);
    tex_reset_decode_queue();
    gBufferIndex ^= 1;
    switch (gPaused) {
        case 0:
            if (gPauseRequest) {
                gPaused = 1;
                gUnpauseRequest = 0;
                gPauseRequest = 0;
                gPausedBufferIndex = gBufferIndex ^ 1;
                if (gGameMode & (MODE_LEVEL | MODE_MAP_VIEW)) {
                    if (!gBonusLevelExit) {
                        snd_set_slot_volume(0, 0);
                    }
                    music_fade_to(0.5f);
                    if (gGameMode == MODE_LEVEL && (gHaveCarrier || gHaveShuttle) && currentYoshiWindow == YW_PAUSE) {
                        gNextGameMode = MODE_MAP_VIEW;
                        cull_restart(10);
                        gMapViewScrollZ = 0;
                    }
                }
            }
            break;
        case 1:
            if (gUnpauseRequest && gBufferIndex != gPausedBufferIndex) {
                gPauseRequest = 0;
                gUnpauseRequest = 0;
                gPaused = 0;
                if (gGameMode & (MODE_LEVEL | MODE_MAP_VIEW)) {
                    if (gPlayFrames) {
                        debug_printf("LOCKING KEYS!\n");
                        gSwallowB = 1;
                    }
                    snd_set_mode_slot_volumes(gGameMode);
                    music_fade_to(1.0f);
                    if (gGameMode == MODE_MAP_VIEW && (gHaveCarrier || gHaveShuttle)) {
                        gNextGameMode = MODE_LEVEL;
                        cull_restart(10);
                    }
                    if (gUnpausedFrames == 0) {
                        gPlayFrames = 0;
                    }
                }
            }
            break;
    }
    if (gPaused) {
        gVehDrawBufferIndex = gPausedBufferIndex;
    } else {
        gVehDrawBufferIndex = gBufferIndex;
    }
    if (gHaveCarrier && (gGameMode & (MODE_LEVEL | MODE_MAP_VIEW))) {
        carrier_update_sound();
    }
    if (gHaveChopper && (gGameMode & (MODE_INTRO_APPROACH | MODE_LEVEL | MODE_MAP_VIEW | MODE_INTRO_FLY | MODE_INTRO_LANDING))) {
        chopper_update_sound();
    }
    util_check_board_zone();
    gVehJustSwitched = 0;
    if (gCurVehicle == VEH_DRIVER && !(gGameMode & (MODE_INTRO_APPROACH | MODE_INTRO_FLY | MODE_INTRO_LANDING))) {
        veh_apply_switch();
    }
    cont_update();
    if (gGameMode & (MODE_LEVEL | MODE_MAP_VIEW)) {
        zoom = 0;
        if ((gContButtons & D_CBUTTONS) && !(gContButtons & U_CBUTTONS)) {
            if (gCamZoom < 1.5 && gGameMode == MODE_LEVEL) {
                zoom = 2;
                if (!(gContPrevButtons & D_CBUTTONS)) {
                    snd_play(gSfxBank, SFX_CAM_OUT, NULL);
                }
            } else if (!(gContPrevButtons & D_CBUTTONS) && gGameMode != MODE_MAP_VIEW) {
                zoom = 3;
            }
        }
        if (zoom == 0) {
            if ((gContButtons & U_CBUTTONS) && !(gContButtons & D_CBUTTONS)) {
                if (gCamZoom > 1.0 && gGameMode == MODE_LEVEL && gCamZoomInLock == 0) {
                    zoom = 1;
                    if (!(gContPrevButtons & U_CBUTTONS)) {
                        snd_play(gSfxBank, SFX_CAM_IN, NULL);
                    }
                } else if (!(gContPrevButtons & U_CBUTTONS) && gGameMode == MODE_MAP_VIEW) {
                    zoom = 4;
                }
            } else {
                gCamZoomInLock = 0;
            }
        }
        switch (zoom) {
            case 0:
                break;
            case 2:
                gCamZoom = gCamZoom + 0.05;
                if (gCamZoom > 1.5) {
                    gCamZoom = 1.5f;
                }
                break;
            case 1:
                gCamZoom = gCamZoom - 0.05;
                if (gCamZoom < 1.0) {
                    gCamZoom = 1.0f;
                }
                break;
            case 3:
                if (gHaveCarrier || gHaveShuttle) {
                    cull_restart(10);
                    gNextGameMode = MODE_MAP_VIEW;
                    gMapViewScrollZ = 0;
                    snd_play(gSfxBank, SFX_MAP_VIEW, NULL);
                }
                break;
            case 4:
                gCamZoomInLock = 1;
                if (gHaveCarrier || gHaveShuttle) {
                    cull_restart(10);
                    gNextGameMode = MODE_LEVEL;
                    snd_play(gSfxBank, SFX_MAP_VIEW, NULL);
                }
                break;
        }
    }
    if (gGameMode == MODE_MAP_VIEW && gPaused && yoshiState == 2 && gHaveCarrier && currentYoshiWindow == YW_PAUSE) {
        if (!(gContButtons & Z_TRIG) && !(gContButtons & R_TRIG)) {
            gMapViewScrollSpeed = 0;
        } else {
            gMapViewScrollSpeed += 100;
            if (gMapViewScrollSpeed > 800) {
                gMapViewScrollSpeed = 800;
            }
            if (gContButtons & Z_TRIG) {
                gMapViewScrollZ += gMapViewScrollSpeed;
            }
            if (gContButtons & R_TRIG) {
                gMapViewScrollZ -= gMapViewScrollSpeed;
            }
        }
        pos = gCarrierZ + gMapViewScrollZ;
        lo = gCarrierStartZ - 400;
        hi = gCarrierStartZ + gCarrierRouteLen;
        if (pos > hi) {
            gMapViewScrollZ = hi - gCarrierZ;
        }
        if (pos < lo) {
            gMapViewScrollZ = lo - gCarrierZ;
        }
        if (gContButtons & (Z_TRIG | R_TRIG)) {
            gYoshiWindows[YW_PAUSE].x = (gYoshiWindows[YW_PAUSE].x - 0x18 < -0x100) ? -0x100 : gYoshiWindows[YW_PAUSE].x - 0x18;
            gYoshiScale = (gYoshiScale * 0.8 > 0.1) ? gYoshiScale * 0.8 : 0.1;
            gYoshiWindows[YW_PAUSE].flags &= ~0x20;
        } else {
            gYoshiWindows[YW_PAUSE].x = (gYoshiWindows[YW_PAUSE].x + 0x20 > 0x20) ? 0x20 : gYoshiWindows[YW_PAUSE].x + 0x20;
            gYoshiScale = (gYoshiScale / 0.8 < 1.0) ? gYoshiScale / 0.8 : 1.0;
            if (gYoshiScale == 1.0) {
                gYoshiWindows[YW_PAUSE].flags |= 0x20;
            }
        }
    } else {
        gMapViewScrollSpeed = 0;
        gYoshiWindows[YW_PAUSE].x = (gYoshiWindows[YW_PAUSE].x + 0x20 > 0x20) ? 0x20 : gYoshiWindows[YW_PAUSE].x + 0x20;
        gYoshiScale = (gYoshiScale / 0.8 < 1.0) ? gYoshiScale / 0.8 : 1.0;
        if (gYoshiScale == 1.0) {
            gYoshiWindows[YW_PAUSE].flags |= 0x20;
        }
    }
    if (gHeldZ && !gPaused && gCurVehicle && !gPlayerSpeed && (gLevelKind == LEVEL_KIND_MISSION || gLevelKind == LEVEL_KIND_MISSION_TIMED)) {
        veh_leave();
    }
    veh_update_others();
    if ((gContButtons & B_BUTTON) && !(gContPrevButtons & B_BUTTON) && !areWeFading() && gNextGameMode == 0) {
        switch (gGameMode) {
            case MODE_GLOBE_SEQ:
                if (levelno != 0x2F && levelno != 0x31 && (levelno != 0x26 || gPrevGameMode == MODE_WORLD)) {
                    snd_play(gSfxBank, SFX_BACK, NULL);
                    gNextGameMode = MODE_WORLD;
                }
                break;
            case MODE_REPLAY:
            case MODE_REPLAY_CARRIER_END:
                snd_play(gSfxBank, SFX_BACK, NULL);
                if (LEVEL_DONE(levelno)) {
                    gNextGameMode = MODE_RESULTS;
                } else {
                    gNextGameMode = MODE_WORLD;
                }
                break;
            case MODE_INTRO_FLY:
            case MODE_INTRO_LANDING:
                if (levelno != 0x32 || gPrevGameMode == MODE_WORLD) {
                    snd_play(gSfxBank, SFX_BACK, NULL);
                    gNextGameMode = MODE_WORLD;
                }
                break;
        }
    }
    if (gHaveChopper && (!gPaused || (gGameMode & (MODE_INTRO_APPROACH | MODE_INTRO_FLY | MODE_INTRO_LANDING)))) {
        chopper_update();
        if (gGameMode & (MODE_INTRO_APPROACH | MODE_INTRO_FLY | MODE_INTRO_LANDING)) {
            if (!areWeFading()) {
                if (gNextGameMode == 0 || (gNextGameMode & (MODE_INTRO_APPROACH | MODE_INTRO_FLY | MODE_INTRO_LANDING))) {
                    driver_update();
                }
                chopper_update_mode();
            }
        } else {
            people_update();
        }
    }
    if (!gPaused && !gPlayerHidden) {
        veh_update_player();
    }
    if (gGhostRecording && !gPaused) {
        ghost_record();
    }
    if (gHaveCarrier && (gLevelKind & LEVEL_KIND_ANY_MISSION)) {
        if (!(gGameMode & (MODE_INTRO_APPROACH | MODE_INTRO_FLY | MODE_INTRO_LANDING))) {
            carrier_update();
        }
        if ((gGameMode & (MODE_LEVEL | MODE_MAP_VIEW)) && !yoshi_get_demand() && !gPaused && !gLevelDone && !gCarrierHit) {
            carrier_update_proximity(vis = obj_find_carrier_target());
        }
    }
    if (gHaveShuttle && !gPaused) {
        shuttle_update();
        if (levelno == 0x32 && yoshiState == 1 && gFramesInMode > 50 && (gGameMode & (MODE_INTRO_APPROACH | MODE_INTRO_FLY | MODE_INTRO_LANDING)) && !areWeFading()) {
            fade_to_mode(MODE_LEVEL_START);
        }
    }
    anim_run_mtx_copies();
    if (!gPaused) {
        gVehSlowed = 0;
        gPathBuildingsDestroyed = obj_path_buildings_destroyed();
        obj_check_level_end();
        veh_update_riders();
        tnt_ride_vehicle(gCurVehicle);
        block_ride_vehicle(gCurVehicle);
        util_cam_path_step(levelno);
        if (!(gGameMode & (MODE_INTRO_APPROACH | MODE_INTRO_FLY | MODE_INTRO_LANDING))) {
            rdu_update();
        }
        fx_update_power_pill();
        if (gGameMode & (MODE_LEVEL | MODE_MAP_VIEW)) {
            academy_check_marker(gPlayerX, gPlayerY, gPlayerZ);
        }
#ifdef NON_MATCHING
        /* The asm takes its param (the texture decoder's type-4/5 table) in
         * $fp: 0 here in every level (traced). */
        anim_tex_update_all(LEAKED(s8));
#else
        anim_tex_update_all();
#endif
        if ((gGameMode & (MODE_LEVEL | MODE_MAP_VIEW)) && (gLevelKind & LEVEL_KIND_MISSION)) {
            hud_update_value_popups();
            hud_tick_money_counter();
        }
        if (gLevelKind != LEVEL_KIND_MISSION && gTurboActive && (u32) (((s32) gScheduler.retraceCount) - gLevelStartTime) > 90) {
            gTurboActive = 0;
            mb_stop();
        }
    }
    if ((gCarrierHit || gLevelDone || (levelno == 0x32 && gTargetsCleared && !(LEVEL_DONE(0x32)))) && gLevelKind == LEVEL_KIND_MISSION && gTimeInLevel == 0) {
        debug_printf("TIME IN LEVEL=%d\n", gTimeInLevel = ((s32) gScheduler.retraceCount) - gLevelStartTime);
    }
    cam_apply_height_zone(gLevelData);
    if (gGameMode & (MODE_INTRO_APPROACH | MODE_INTRO_FLY | MODE_INTRO_LANDING)) {
        cull_restart(2);
    }
    if (currentYoshiWindow == YW_CONTROL_METHOD && yoshiState != 8 && !gCamFixedEye) {
        if (!gCamWindowView) {
            cull_restart(10);
        }
    } else if (gCamWindowView) {
        cull_restart(10);
    }
    obj_draw_all((u32 *) (&gDynamicBufs[gBufferIndex].subDls[0x1E0]), (u32 *) (&gDynamicBufs[gBufferIndex].subDls[0x578]),
                  (u32 *) (&gDynamicBufs[gBufferIndex].subDls[0x910]), (u32 *) (&gDynamicBufs[gBufferIndex].subDls[0x112B0]),
                  (s32) &gDynamicBufs[gBufferIndex], (s32) (&gDynamicBufs[gBufferIndex].mtxPool[0x2D]), (u32 *) (&gDynamicBufs[gBufferIndex].subDls[0x17070]),
                  (u32 *) (&gDynamicBufs[gBufferIndex].subDls[0x170D8]));
    if (gGameMode == MODE_LEVEL) {
        cull_override_visible_cells(levelno, gPlayerX, gPlayerZ);
    }
    lvl_build_visible_dls((u32 *) N64_IPTR(gLevelDl0[gBufferIndex]), (u32 *) N64_IPTR(gLevelDl1[gBufferIndex]), (u32 *) N64_IPTR(gLevelDl2[gBufferIndex]), (u32 *) N64_IPTR(gLevelDl3[gBufferIndex]),
                  (u32 *) (&gDynamicBufs[gBufferIndex].subDls[0x140]));
    water_update(gBufferIndex);
    if (!gPaused || (gHaveCarrier && gCarrierHit) || gBonusLevelExit) {
        obj_run_effect_queue();
        obj_update_delayed_damage();
    }
    if (!gPaused) {
        tnt_update(gCurVehicle);
        block_update(gCurVehicle);
        shell_update_all();
        crate_update(gCurVehicle);
    }
    if (gLevelKind != LEVEL_KIND_MISSION) {
        if (gGameMode & MODE_REPLAY) {
            hud_replay_turbo();
        }
        if (gGameMode & (MODE_LEVEL | MODE_MAP_VIEW | MODE_LEVEL_START | MODE_TIMED_END)) {
            hud_update_objective();
        }
    } else if (gFailTrigger) {
        if (!(LEVEL_DONE(levelno))) {
            cmo_hit_request = 1;
        } else if (!areWeFading()) {
            gLevelDoneRequest = 1;
            snd_play(gSfxBank, SFX_ERROR, NULL);
            if (gGameMode == MODE_REPLAY) {
                fade_to_mode(MODE_REPLAY);
            } else {
                fade_to_mode(MODE_RESULTS);
            }
        }
    }
    fx_draw_all();
    ptcl_update();
    if (!gPaused) {
        commpt_update();
    }
    mb_push_sample(gPlayerX, gPlayerY, gPlayerZ, gCamEyeX, gCamEyeY, gCamEyeZ);
    dig_coach_update(gFramesInMode);
    bridge_update_levels(levelno);
    lvl_groups_update_tris();
    if (!gPaused) {
        util_check_hint_events();
    }
    gdl = hd_build_frame_dl(&gDynamicBufs[gBufferIndex], &gFrameDlLen);
    if ((gGameMode & (MODE_LEVEL | MODE_REPLAY | MODE_REPLAY_CARRIER_END | MODE_LEVEL_START | MODE_TIMED_END)) && gLevelKind != LEVEL_KIND_MISSION) {
        gdl = hud_draw_objective(gdl, (Gfx * N64P *) &gDynamicBufs[gBufferIndex], (u32 *) &gFrameDlLen);
    }
    if (gGameMode & (MODE_ATTRACT | MODE_GLOBE_SEQ)) {
        if (gGameMode == MODE_ATTRACT) {
            PORT_ITAG(gdl, PORT_ITAG_NONE, 0); /* the attract mode's bobbing logo: blended */
            gdl = title_draw_attract(gdl, (s32) &gDynamicBufs[gBufferIndex], gBufferIndex, &gFrameDlLen);
            PORT_ITAG(gdl, PORT_ITAG_HUD, 0);
        }
        if (gRecDemoNoLogo || gLogoState == 3 || gRecDemoNumber >= 9) {
            if ((yoshiState == 1 || yoshiState == 8 || gRecDemoNumber >= 9) && (!gRecDemoNoLogo || gFramesInMode <= gRecDemoLength)) {
                if (gPadDisplayAlpha + 20 > 0xFF) {
                    gPadDisplayAlpha = 0xFF;
                } else {
                    gPadDisplayAlpha += 20;
                }
            } else if (gPadDisplayAlpha - 20 < 0) {
                gPadDisplayAlpha = 0;
            } else {
                gPadDisplayAlpha -= 20;
            }
        }
        if (gPadDisplayAlpha && gRecDemoNumber && gPadDisplayLoaded) {
            PORT_WTAG(gdl, PORT_WTAG_LEFT, 0, 0);
            gdl = hud_draw_pad((s32) &gDynamicBufs[gBufferIndex], gdl, gPadDisplayAlpha / 2 - 103,
                                0x55 - ((gGameMode == MODE_GLOBE_SEQ) << 5), gPadDisplayAlpha);
        }
        if (gRecDemoNumber == 0 && gLogoState == 3 && gGameMode == MODE_ATTRACT) {
            PORT_WTAG(gdl, PORT_WTAG_NONE, 0, 0);
            gdl = hud_draw_demo_banner((s32) gDynamicBufs, gdl);
        }
        if (gGameMode == MODE_ATTRACT || levelno == 0x2F) {
            hud_attract_messages();
        }
    }
    if (gGameMode & (MODE_INTRO_APPROACH | MODE_INTRO_FLY | MODE_INTRO_LANDING)) {
        if (gFramesInMode == 150 && levelno != 0x32) {
            yoshi_demand(YOSHI_DEMAND_OPEN | YW_INTRO_FRAME150);
        }
        if (!players[playerNumber].gameState && gFramesInMode == 10) {
            yoshi_demand(YOSHI_DEMAND_OPEN | YW_INTRO_GAME_STATE0);
        }
    }
    PORT_WTAG(gdl, PORT_WTAG_NONE, 0, 0);
    gdl = hd_debug_dl_stub(gdl, &gDynamicBufs[gBufferIndex], &gFrameDlLen);
    if ((gGameMode & (MODE_REPLAY | MODE_REPLAY_CARRIER_END)) && gScheduler.frameCount - gReplayStartTime > 160 && !gReplayMenuShown) {
        gReplayMenuShown = 1;
        if (yoshiState == 1) {
            if (!(LEVEL_DONE(levelno)) || gLevelKind == LEVEL_KIND_MISSION) {
                yoshi_demand(gYoshiReplayWindowByKind[lowest_set_bit(gLevelKind)] | YOSHI_DEMAND_OPEN);
            }
        }
    }
    if (gGameMode == MODE_LEVEL_CLEAR && !areWeFading() && (!alCSPGetState(gMusicPlayer) || gScheduler.frameCount - gDoneTuneStartTime > 300)) {
        fade_to_mode(MODE_RESULTS);
    }
    if (levelno == 0x31 && yoshiState == 1) {
        gdl = credits_draw((Gfx * N64P *) &gDynamicBufs[gBufferIndex], gdl);
    }
    PORT_WTAG(gdl, PORT_WTAG_TEXT_SIDES, 0, 0);
    text_flush_screen(&gdl, (Mtx *) &gDynamicBufs[gBufferIndex]);
    PORT_WTAG(gdl, PORT_WTAG_NONE, 0, 0);
    if (gGameMode != MODE_ATTRACT) {
        gdl = fade_draw((s32) &gDynamicBufs[gBufferIndex], gdl);
    }
    if (gGameMode == MODE_YOSHI_PAGES) {
        if ((gContButtons & R_TRIG) && !(gContPrevButtons & R_TRIG)) {
            if (currentYoshiWindow < YW_PAGES_LAST && yoshiState != 8 && yoshiState != 1) {
                yoshi_demand((currentYoshiWindow + 1) | YOSHI_DEMAND_OPEN);
                snd_play(gSfxBank, SFX_MENU_MOVE, NULL);
            } else {
                snd_play(gSfxBank, SFX_DENIED, NULL);
            }
        }
        if ((gContButtons & Z_TRIG) && !(gContPrevButtons & Z_TRIG)) {
            if (currentYoshiWindow >= YW_PAGES_FIRST + 1 && yoshiState != 8 && yoshiState != 1) {
                yoshi_demand((currentYoshiWindow - 1) | YOSHI_DEMAND_OPEN);
                snd_play(gSfxBank, SFX_MENU_MOVE, NULL);
            } else {
                snd_play(gSfxBank, SFX_DENIED, NULL);
            }
        }
    }
    if ((((gContButtons & START_BUTTON) && !(gContPrevButtons & START_BUTTON)) || ((gContButtons & A_BUTTON) && !(gContPrevButtons & A_BUTTON))) &&
        !areWeFading() && gYoshiPendingWindow == NO_YOSHI_WINDOW && gNextGameMode == 0 && !cmo_hit_request && !gLevelDoneRequest && currentYoshiWindow != YW_PAUSE) {
        if ((yoshi_get_demand() & YOSHI_DEMAND_OPEN) && (yoshi_get_demand() & 1)) {
            yoshi_get_demand();
        }
        switch (gGameMode) {
            case MODE_INTRO_APPROACH:
            case MODE_INTRO_FLY:
            case MODE_INTRO_LANDING:
                if (levelno != 0x32 || gPrevGameMode == MODE_WORLD) {
                    gNextGameMode = MODE_LEVEL_START;
                    snd_play(gSfxBank, SFX_SELECT, NULL);
                }
                break;
            case MODE_REPLAY:
            case MODE_REPLAY_CARRIER_END:
                if ((LEVEL_DONE(levelno)) && gLevelKind != LEVEL_KIND_MISSION) {
                    gNextGameMode = MODE_RESULTS;
                    snd_play(gSfxBank, SFX_SELECT, NULL);
                } else if (yoshiState == 1) {
                    yoshi_demand(gYoshiReplayWindowByKind[lowest_set_bit(gLevelKind)] | YOSHI_DEMAND_OPEN);
                }
                break;
            case MODE_ATTRACT:
                if (gAttractStartOk) {
                    snd_play(gSfxBank, SFX_SELECT, NULL);
                    snd_set_slot_volume(0, 0);
                    snd_set_slot_volume(5, 0);
                    fade_to_mode(MODE_BOOT_CHECK);
                }
                break;
            case MODE_GLOBE_SEQ:
                if (gSeqSkippable) {
                    gNextGameMode = MODE_SEQ_SKIP;
                }
                break;
            case MODE_LEVEL:
            case MODE_MAP_VIEW:
                if ((gContButtons & START_BUTTON) && !gPaused) {
                    if ((LEVEL_DONE(levelno)) && gLevelKind == LEVEL_KIND_MISSION) {
                        gYoshiGameItems[2].flags |= 1;
                        gYoshiGameItems[2].color = 7;
                    } else {
                        gYoshiGameItems[2].flags &= ~1;
                        gYoshiGameItems[2].color = 8;
                    }
                    yoshi_demand(YOSHI_DEMAND_OPEN | YW_PAUSE);
                }
                break;
        }
    }
    gdl = yoshi_draw(gdl, (s32) &gDynamicBufs[gBufferIndex], &gFrameDlLen);
    if (yoshiSelection && !cmo_hit_request && !gLevelDoneRequest) {
        hd_handle_yoshi_selection(&yoshiSelection);
    }
    if ((gGameMode & (MODE_LEVEL | MODE_REPLAY | MODE_REPLAY_CARRIER_END)) && gYoshiPendingWindow == NO_YOSHI_WINDOW && gTurboStartState == 0 &&
        !((currentYoshiWindow != NO_YOSHI_WINDOW) ? gYoshiWindows[currentYoshiWindow].flags & 0x01000000 : 0)) {
        alpha = (gGameMode & (MODE_REPLAY | MODE_REPLAY_CARRIER_END)) ? 0xB4 : 0xFF;
        if (gHudAlpha < alpha) {
            gHudAlpha = (s32) ((alpha < gHudAlpha + 16) ? alpha : gHudAlpha + 16);
        } else {
            gHudAlpha = alpha;
        }
    } else if (gHudAlpha >= 0) {
        gHudAlpha = (s32) ((gHudAlpha - 16 < 0) ? 0 : gHudAlpha - 16);
    } else {
        gHudAlpha = 0;
    }
    if (gGameMode == MODE_ATTRACT) {
        gdl = fade_draw((s32) &gDynamicBufs[gBufferIndex], gdl);
    }
    hd_close_display_list(gdl, &gFrameDlLen);
    for (i = 0; i < nextdma; i++) {
        osRecvMesg(&textureDmaMessageQ, NULL, 1);
    }
    HD_ASSERT(MQ_IS_EMPTY(&textureDmaMessageQ), 1509);
    HD_ASSERT(nextdma-no_palette_dmas<NUM_TEXTURE_DMAS, 1510);
    for (i = 0; i < nextdma - no_palette_dmas; i++) {
        tex_decode_next();
    }
    gPartJitterX = gFrameDrawTime % 40 - 20;
    gPartJitterY = gFrameDrawTime / 100 % 40 - 20;
    gPartJitterZ = gFrameDrawTime / 10000 % 40 - 20;
    gCarrierHitSeen = gCarrierHit;
    if (!gLevelDone && !gCarrierHit) {
        if (gLevelDoneRequest) {
            HD_ASSERT(!cmo_hit_request, 1529);
            gLevelDone = 1;
        } else if (cmo_hit_request) {
            gCarrierHit = 1;
        }
        gLevelDoneRequest = 0;
        cmo_hit_request = 0;
    }
}

/* Queues the value of a destroyed object (gValuePopupNew) as a "$N" popup placed by the camera heading quadrant,
 * and fades the current popup in and out over 30 frames (0x8024A348).
 * The "$value" popups: queues gValuePopupNew (a destroyed object's value) and fades the current one */
void hud_update_value_popups(void) {
    s32 pad;

    if (gValuePopupNew != 0) {
        if (gValuePopupTail + 1 != gValuePopupHead && (gValuePopupTail != 4 || gValuePopupHead != 0)) {
            gValuePopups[gValuePopupTail].value = gValuePopupNew;
            gValuePopups[gValuePopupTail].timer = 30;
            if (gPlayerMoveHeading >= 0xE00 || gPlayerMoveHeading < 0x200) {
                if (gLevelKind == LEVEL_KIND_DAMAGE) {
                    gValuePopups[gValuePopupTail].x = 30;
                    gValuePopups[gValuePopupTail].y = 60;
                    gValuePopups[gValuePopupTail].forward = 1;
                } else {
                    gValuePopups[gValuePopupTail].x = 30;
                    gValuePopups[gValuePopupTail].y = 30;
                    gValuePopups[gValuePopupTail].forward = 1;
                }
            }
            if (gPlayerMoveHeading >= 0x200 && gPlayerMoveHeading < 0x600) {
                gValuePopups[gValuePopupTail].x = 30;
                gValuePopups[gValuePopupTail].y = 160;
                gValuePopups[gValuePopupTail].forward = 1;
            }
            if (gPlayerMoveHeading >= 0x600 && gPlayerMoveHeading < 0xA00) {
                gValuePopups[gValuePopupTail].x = 250;
                gValuePopups[gValuePopupTail].y = 160;
                gValuePopups[gValuePopupTail].forward = 0;
            }
            if (gPlayerMoveHeading >= 0xA00 && gPlayerMoveHeading < 0xE00) {
                gValuePopups[gValuePopupTail].x = 250;
                gValuePopups[gValuePopupTail].y = 37;
                gValuePopups[gValuePopupTail].forward = 0;
            }
            gValuePopupTail++;
            if (gValuePopupTail == 5) {
                gValuePopupTail = 0;
            }
        }
        gValuePopupNew = 0;
    }
    if (gValuePopupHead != gValuePopupTail) {
        gValuePopupValue = gValuePopups[gValuePopupHead].value;
        gValuePopupX = gValuePopups[gValuePopupHead].x;
        gValuePopupY = gValuePopups[gValuePopupHead].y;
        gValuePopupForward = gValuePopups[gValuePopupHead].forward;
        if (gValuePopups[gValuePopupHead].timer >= 16) {
            gValuePopupAlpha = 255.0f - (f32) (gValuePopups[gValuePopupHead].timer - 15) / 15.0f * 255.0f;
        } else {
            gValuePopupAlpha = (f32) gValuePopups[gValuePopupHead].timer / 15.0f * 255.0f;
        }
        if (!(gValuePopups[gValuePopupHead].timer--)) {
            gValuePopupHead++;
            if (gValuePopupHead == 5) {
                gValuePopupHead = 0;
            }
        }
    } else {
        gValuePopupValue = 0;
    }
}

/* Sets the carrier danger level (0 far, 1 near, 2 very near) from the distance to the carrier's target with
 * per-level thresholds; on a change pushes or pops the tension tune, starts or stops the alarm and opens the
 * warning windows (0x8024A92C).
 * Picks the music intensity from the distance to the carrier (CMO) */
void carrier_update_proximity(u32 dist) {
    u32 far;
    u32 near;

    switch (levelno) {
        case 29:
            far = 22000;
            near = 14000;
            break;
        case 12:
            far = 26000;
            near = 20000;
            break;
        case 58:
            far = 36000;
            near = 30000;
            break;
        case 18:
            far = 36000;
            near = 28000;
            break;
        case 16:
            far = 33000;
            near = 23000;
            break;
        case 15:
            far = 24000;
            near = 18000;
            break;
        case 13:
            far = 55000;
            near = 35000;
            break;
        case 14:
            if (gCarrierZ > 177600) {
                far = 25000;
                near = 20000;
            } else if (gCarrierZ > 80000) {
                far = 22000;
                near = 17500;
            } else {
                far = 14000;
                near = 8000;
            }
            break;
        case 9:
            if (gCarrierZ > 100320) {
                far = 36000;
                near = 18000;
            } else {
                far = 12000;
                near = 6000;
            }
            break;
        default:
            far = 12000;
            near = 6000;
            break;
    }
    gCarrierDangerPrev = gCarrierDanger;
    if (dist < far) {
        if (dist < near) {
            gCarrierDanger = 2;
        } else {
            gCarrierDanger = 1;
        }
    } else {
        gCarrierDanger = 0;
    }
    if (gCarrierDanger != gCarrierDangerPrev) {
        if (gCarrierDangerPrev == 0 && util_dist2d(gCarrierX, gCarrierZ, gPlayerX, gPlayerZ) > 20000) {
            hud_panel_start(1, 1, 7, SFX_PANEL_WARNING);
            debug_printf("TOO FAR AWAY FROM CMO\n");
        }
        switch (gCarrierDanger) {
            case 1:
                switch (gCarrierDangerPrev) {
                    case 0:
                        music_push_danger_tune();
                    case 2:
                        snd_stop((void *) N64_IPTR(gCarrierAlarmSound));
                        break;
                }
                break;
            case 2:
                switch (gCarrierDangerPrev) {
                    case 0:
                        music_push_danger_tune();
                    case 1:
                        snd_play(gSfxBank, SFX_CARRIER_ALARM, &gCarrierAlarmSound);
                        break;
                }
                break;
            case 0:
                snd_stop((void *) N64_IPTR(gCarrierAlarmSound));
                if (gLevelInfo[levelno].kind != LEVEL_KIND_MISSION_TIMED || !obj_path_buildings_destroyed()) {
                    debug_printf("popTuneImmediate();\n");
                    popTuneImmediate();
                } else {
                    debug_printf("WILL THIS FIX IT!!?\n");
                }
                if (gCarrierDangerPrev == 2) {
                    snd_play(gSfxBank, SFX_UNK_0x68_CHIME, NULL);
                }
                break;
        }
    }
    switch (gCarrierDanger) {
        case 1:
            if (!yoshi_show_hint(YW_HINT_CARRIER_NEAR) && currentYoshiWindow != YW_CARRIER_DANGER) {
                yoshi_demand(YOSHI_DEMAND_OPEN | YW_CARRIER_DANGER);
            }
            break;
        case 2:
            if (currentYoshiWindow != YW_CARRIER_ALARM) {
                yoshi_demand(YOSHI_DEMAND_OPEN | YW_CARRIER_ALARM);
            }
            break;
        case 0:
            if ((yoshiState == 2 || yoshiState == 4) && !yoshi_get_demand() &&
                (currentYoshiWindow == YW_CARRIER_DANGER || currentYoshiWindow == YW_CARRIER_ALARM || currentYoshiWindow == YW_HINT_CARRIER_NEAR)) {
                yoshi_demand(YOSHI_DEMAND_OFF);
            }
            break;
    }
}

/* Moves the displayed money (gHudMoney) toward the money total (gLevelMoney) by a tenth of the gap plus 10
 * each frame (0x8024ADD8).
 * Eases gHudMoney towards gLevelMoney by a tenth of the gap plus 10 */
void hud_tick_money_counter(void) {
    u32 d;

    d = gLevelMoney - gHudMoney;
    gHudMoney = d / 10 + gHudMoney;
    gHudMoney += 10;
    if (gLevelMoney < gHudMoney) {
        gHudMoney = gLevelMoney;
    }
}

/* Applies a pending vehicle switch (gNextVehicle): the type's enter routine
 * (veh_enter), the player's flag for the type (0x8024AE2C). */
void veh_apply_switch(void) {
    s32 pad;

    if (gNextVehicle != VEH_DRIVER && gNextVehicle != VEH_CARRIER) {
        if (veh_has_partial_rider(gNextVehicle) == 0) {
            veh_attach_riders(gNextVehicle);
            tnt_attach_to_vehicle(gNextVehicle);
            block_attach_to_vehicle(gNextVehicle);
            cont_steer_reset();
            mb_cancel();
            if (veh_enter(gNextVehicle)) {
                gCurVehicle = gNextVehicle;
                driver_stop_blend_anim();
                if (!gPaused && gFramesInMode >= 11 &&
                    (gCurVehicle == VEH_AMDREAM || gCurVehicle == VEH_STARSKI || gCurVehicle == VEH_POLICE || gCurVehicle == VEH_ATEAM)) {
                    yoshi_show_hint(YW_HINT_CAR);
                }
            }
            players[playerNumber].vehiclesUnlocked |= 1 << gCurVehicle;
            gDriverHidden = 1;
            gVehEntered = 1;
            gVehJustSwitched = 1;
        } else if (!snd_get_state((void *) N64_IPTR(gEnterRefusedSound))) {
            snd_play(gSfxBank, SFX_ERROR, &gEnterRefusedSound);
        }
    }
}

/* Calls the type's enter routine ("changing to digger %d": the game's word
 * for its vehicles); returns 0 if id is not a vehicle (0x8024AFA8). */
s32 veh_enter(s32 id) {
    u8 ok = 1;

    switch (id) {
        case VEH_SIDESWIPE:
            sideswipe_enter();
            break;
        case VEH_THUNDERFIST:
            thunderfist_enter();
            break;
        case VEH_SKYFALL:
            skyfall_enter();
            break;
        case VEH_RAMDOZER:
            ramdozer_enter();
            break;
        case VEH_BACKLASH:
            backlash_enter();
            break;
        case VEH_CRANE:
            crane_enter();
            break;
        case VEH_TRAIN:
#ifdef NON_MATCHING
            train_enter(LEAKED(a0));
#else
            train_enter();
#endif
            break;
        case VEH_AMDREAM:
#ifdef NON_MATCHING
            amdream_enter(LEAKED(a0));
#else
            amdream_enter();
#endif
            break;
        case VEH_JBOMB:
            jbomb_enter();
            break;
        case VEH_BALLISTA:
            ballista_enter();
            break;
        case VEH_BARGE_0B:
        case VEH_BARGE_11:
        case VEH_BARGE_12:
#ifdef NON_MATCHING
            barge_enter(LEAKED(a0));
#else
            barge_enter();
#endif
            break;
        case VEH_POLICE:
#ifdef NON_MATCHING
            police_enter(LEAKED(a0));
#else
            police_enter();
#endif
            break;
        case VEH_ATEAM:
#ifdef NON_MATCHING
            ateam_enter(LEAKED(a0));
#else
            ateam_enter();
#endif
            break;
        case VEH_STARSKI:
#ifdef NON_MATCHING
            starski_enter(LEAKED(a0));
#else
            starski_enter();
#endif
            break;
        case VEH_CYCLONE:
            cyclone_enter();
            break;
        default:
            ok = 0;
            break;
    }
    if (ok && (gGameMode & (MODE_LEVEL | MODE_MAP_VIEW))) {
        rec_record((players[playerNumber].controlModes & (1 << id)) ? 0x80 : 0x40, 0, 0);
    }
    if (ok) {
        debug_printf("changing to digger %d\n", id);
    }
    return ok;
}

#if defined(NON_MATCHING) && defined(PORT_HOST)
/* Windows port, byte order: veh_leave reads the word table that starts
 * at the u8 EEPROM flag gUseEeprom (set at boot), and on the N64 that flag is
 * word 0's most significant byte.  The image keeps word 0 big-endian
 * (port/data/image_overrides.txt), so word 0 is assembled from its bytes;
 * the others are host-order words.  Checked by port/tools/loadref.py (T5). */
s32 port_802E8BF8_word(s32 i) {
    u8 *t = &gUseEeprom;

    if (i == 0) {
        return (t[0] << 24) | (t[1] << 16) | (t[2] << 8) | t[3];
    }
    return ((s32 *) t)[i];
}
#endif

/* Leaves the current vehicle (0x8024B188). */
void veh_leave(void) {
    u8 res = 0;
    u8 ok = 0;

    res = veh_exit_check();
    if (res == 1) {
#if defined(NON_MATCHING) && defined(PORT_HOST)
        ok = driver_try_exit(port_802E8BF8_word(gCurVehicle));
#else
        ok = driver_try_exit(((s32 *) &gUseEeprom)[gCurVehicle]);
#endif
    }
    if (ok) {
        gDriverHidden = 0;
        mb_cancel();
        tnt_detach_all();
        block_detach_all();
        cont_steer_reset();
        switch (gCurVehicle) {
            case VEH_RAMDOZER:
                ramdozer_leave();
                break;
            case VEH_SKYFALL:
                skyfall_leave();
                break;
            case VEH_BACKLASH:
                backlash_leave();
                break;
            case VEH_THUNDERFIST:
                thunderfist_leave();
                break;
            case VEH_SIDESWIPE:
                sideswipe_leave();
                break;
            case VEH_TRAIN:
                train_leave();
                break;
            case VEH_AMDREAM:
                amdream_leave();
                break;
            case VEH_JBOMB:
                jbomb_leave();
                break;
            case VEH_BALLISTA:
                ballista_leave();
                break;
            case VEH_BARGE_0B:
            case VEH_BARGE_11:
            case VEH_BARGE_12:
                barge_leave(gCurVehicle);
                break;
            case VEH_POLICE:
                police_leave();
                break;
            case VEH_ATEAM:
                ateam_leave();
                break;
            case VEH_CRANE:
                crane_leave();
                if (veh_is_loaded(VEH_RAMDOZER)) {
                    ramdozer_leave();
                }
                if (veh_is_loaded(VEH_POLICE)) {
                    police_leave();
                }
                break;
            case VEH_STARSKI:
                starski_leave();
                break;
            case VEH_CYCLONE:
                cyclone_leave();
                break;
        }
        gCurVehicle = VEH_DRIVER;
        if (gGameMode & (MODE_LEVEL | MODE_MAP_VIEW)) {
            rec_record((players[playerNumber].controlModes & 1) ? 0x80 : 0x40, 0, 0);
        }
    }
    if (res == 0 || (res != 2 && !ok)) {
        if (!snd_get_state((void *) N64_IPTR(gLeaveRefusedSound))) {
            snd_play(gSfxBank, SFX_ERROR, &gLeaveRefusedSound);
        }
    }
}

/* 1 if some gVehicles record has type id (0x8024B418).
 * Is any entry of gVehicles (up to gVehiclesEnd) tagged with id? */
s32 veh_is_loaded(u8 id) {
    s32 i;

    i = 0;
    while (&gVehicles[i] != gVehiclesEnd) {
        if (gVehicles[i].type == id) {
            return 1;
        }
        i++;
    }
    return 0;
}

/* Per-vehicle exit check for the current vehicle (gCurVehicle). Declared u8
 * but has no return statement: callers read the handler's leftover v0. */
#ifdef NON_MATCHING
/* The current type's exit check (0 no, 1 yes, 2 silently no) (0x8024B4B8). */
u8 veh_exit_check(void) {
    /* An id without a handler returns whatever v0 held in the original
     * (indeterminate; only vehicle ids reach this). */
    s32 ret = 0;

    switch (gCurVehicle) {
        case VEH_RAMDOZER:
            ret = ramdozer_exit_check();
            break;
        case VEH_SKYFALL:
            ret = skyfall_exit_check();
            break;
        case VEH_BACKLASH:
            ret = backlash_exit_check();
            break;
        case VEH_THUNDERFIST:
            ret = thunderfist_exit_check();
            break;
        case VEH_SIDESWIPE:
            ret = sideswipe_exit_check();
            break;
        case VEH_CRANE:
            ret = crane_exit_check();
            break;
        case VEH_TRAIN:
            ret = train_exit_check();
            break;
        case VEH_AMDREAM:
            ret = amdream_exit_check();
            break;
        case VEH_JBOMB:
            ret = jbomb_exit_check();
            break;
        case VEH_BALLISTA:
            ret = ballista_exit_check();
            break;
        case VEH_BARGE_0B:
        case VEH_BARGE_11:
        case VEH_BARGE_12:
            ret = barge_exit_check();
            break;
        case VEH_POLICE:
            ret = police_exit_check();
            break;
        case VEH_ATEAM:
            ret = ateam_exit_check();
            break;
        case VEH_STARSKI:
            ret = starski_exit_check();
            break;
        case VEH_CYCLONE:
            ret = cyclone_exit_check();
            break;
    }
    return ret;
}
#else
u8 veh_exit_check(void) {
    switch (gCurVehicle) {
        case VEH_RAMDOZER:
            ramdozer_exit_check();
            break;
        case VEH_SKYFALL:
            skyfall_exit_check();
            break;
        case VEH_BACKLASH:
            backlash_exit_check();
            break;
        case VEH_THUNDERFIST:
            thunderfist_exit_check();
            break;
        case VEH_SIDESWIPE:
            sideswipe_exit_check();
            break;
        case VEH_CRANE:
            crane_exit_check();
            break;
        case VEH_TRAIN:
            train_exit_check();
            break;
        case VEH_AMDREAM:
            amdream_exit_check();
            break;
        case VEH_JBOMB:
            jbomb_exit_check();
            break;
        case VEH_BALLISTA:
            ballista_exit_check();
            break;
        case VEH_BARGE_0B:
        case VEH_BARGE_11:
        case VEH_BARGE_12:
            barge_exit_check();
            break;
        case VEH_POLICE:
            police_exit_check();
            break;
        case VEH_ATEAM:
            ateam_exit_check();
            break;
        case VEH_STARSKI:
            starski_exit_check();
            break;
        case VEH_CYCLONE:
            cyclone_exit_check();
            break;
    }
}
#endif


/* Vehicles standing on the player's vehicle follow it (0x8024B5E8). */
void veh_update_riders(void) {
    gRiderPass = 1;
    veh_move_riders(gCurVehicle);
}

#ifdef NON_MATCHING
/* Leaked-register inputs of the hand-asm vehicle functions called below (and
 * anim_tex_update_all in hd_game_frame), which this file declares `void (void)`
 * (traced in the original ROM with mupen64plus breakpoints over attract demos
 * 0-8, nine levels; see the port notes, trace_values.md):
 *  - $fp ($s8): 0 at veh_update_others, veh_update_player and anim_tex_update_all in every
 *    level (hd_game_frame and the IDO code above it never set $s8, and the
 *    hand asm they call before these points restores it). The re-ground
 *    functions of veh_update_others save and restore fp themselves, so every one
 *    of them gets 0. It reaches state: veh_reground stores it into
 *    gSlotSurface[0..2] and veh+0x50 = their average (the ground byte), and
 *    anim_tex_update_all hands it to the texture decoder as the type-4/5 table.
 *  - starski_reground's a3 (the previous loop iteration's leftover; only read by
 *    starski_build_model for a zero-matrix point) and its FP pass-through state
 *    f12-f26 (only FP side results of the triangle scans): 0. Not reached in
 *    the demos.
 *  - the per-frame updates' zone registers t6, t7, s0-s4 (and f14-f26 for
 *    thunderfist_update / starski_update / cyclone_update). Measured: t6 = this
 *    switch's case label (the jump-table target, e.g. 0x8024B7F8), t7 = the
 *    level's leftover small integer (2..7, also in t8), s0 = 0, s1 = 1 / 0x30
 *    / 0x3A, s2/s3/s4 = 0x803B8D40 / 0x803B8578 / 0x803B8570. They only pass
 *    through veh_zone_alpha (unchanged when the level has no zones, as in 8 of
 *    the 9 demo levels) into the type-1 fx_spawn effect records, which
 *    never store them (and the FP state only feeds FP side results): dead.
 *    0, as the other updates (driver_update, backlash_update, ...) use. */
#endif

/* The per-frame routine of every placed vehicle but the player's (by type)
 * (0x8024B618). */
void veh_update_others(void) {
    s32 i;
    s32 id;

    i = 0;
    while (&gVehicles[i] != gVehiclesEnd) {
        if (gVehicles[i].onMovingPart != 0) {
            id = gVehicles[i].type;
            if (id != gCurVehicle && id != VEH_CARRIER && id != VEH_CHOPPER && id != VEH_DRIVER) {
                switch (id) {
                    case VEH_BACKLASH:
                        backlash_reground();
                        break;
                    case VEH_SIDESWIPE:
#ifdef NON_MATCHING
                        sideswipe_reground(LEAKED(fp));
#else
                        sideswipe_reground();
#endif
                        break;
                    case VEH_RAMDOZER:
#ifdef NON_MATCHING
                        ramdozer_reground(LEAKED(fp));
#else
                        ramdozer_reground();
#endif
                        break;
                    case VEH_AMDREAM:
                        amdream_reground();
                        break;
                    case VEH_POLICE:
                        police_reground();
                        break;
                    case VEH_ATEAM:
                        ateam_reground();
                        break;
                    case VEH_STARSKI:
#ifdef NON_MATCHING
                        starski_reground(LEAKED(fp), LEAKED(a3), LEAKED(f12), LEAKED(f14), LEAKED(f20), LEAKED(f22), LEAKED(f24),
                                      LEAKED(f26));
#else
                        starski_reground();
#endif
                        break;
                    case VEH_JBOMB:
#ifdef NON_MATCHING
                        jbomb_reground(LEAKED(fp));
#else
                        jbomb_reground();
#endif
                        break;
                    case VEH_SKYFALL:
#ifdef NON_MATCHING
                        skyfall_reground(LEAKED(fp));
#else
                        skyfall_reground();
#endif
                        break;
                    default:
                        debug_printf("MOVEABLE GEOMETRY MOVE ROUTINE NOT WRITTEN YET\n");
                        break;
                }
            }
        }
        i++;
    }
}

/* The per-frame routine of the player's vehicle (by gCurVehicle; 0: on
 * foot) (0x8024B7AC). */
void veh_update_player(void) {
    gRiderPass = 0;
    switch (gCurVehicle) {
        case VEH_DRIVER:
            gNextVehicle = VEH_DRIVER;
            driver_update();
            break;
        case VEH_SIDESWIPE:
#ifdef NON_MATCHING
            sideswipe_update(LEAKED(t6), LEAKED(t7), LEAKED(s0), LEAKED(s1), LEAKED(s2), LEAKED(s3), LEAKED(s4));
#else
            sideswipe_update();
#endif
            break;
        case VEH_THUNDERFIST:
#ifdef NON_MATCHING
            thunderfist_update(LEAKED(t6), LEAKED(t7), LEAKED(s0), LEAKED(s1), LEAKED(s2), LEAKED(s3), LEAKED(s4),
                          LEAKED(f14), LEAKED(f20), LEAKED(f22), LEAKED(f24), LEAKED(f26));
#else
            thunderfist_update();
#endif
            break;
        case VEH_SKYFALL:
#ifdef NON_MATCHING
            skyfall_update(LEAKED(t6), LEAKED(t7), LEAKED(s0), LEAKED(s1), LEAKED(s2), LEAKED(s3), LEAKED(s4));
#else
            skyfall_update();
#endif
            break;
        case VEH_RAMDOZER:
#ifdef NON_MATCHING
            ramdozer_update(LEAKED(t6), LEAKED(t7), LEAKED(s0), LEAKED(s1), LEAKED(s2), LEAKED(s3), LEAKED(s4));
#else
            ramdozer_update();
#endif
            break;
        case VEH_BACKLASH:
            backlash_update();
            break;
        case VEH_CRANE:
            crane_update();
            break;
        case VEH_TRAIN:
            train_update();
            break;
        case VEH_AMDREAM:
            amdream_update();
            break;
        case VEH_JBOMB:
            jbomb_update();
            break;
        case VEH_BALLISTA:
            ballista_update();
            break;
        case VEH_BARGE_0B:
        case VEH_BARGE_11:
        case VEH_BARGE_12:
            barge_update(gCurVehicle);
            break;
        case VEH_POLICE:
            police_update();
            break;
        case VEH_ATEAM:
            ateam_update();
            break;
        case VEH_STARSKI:
#ifdef NON_MATCHING
            starski_update(LEAKED(t6), LEAKED(t7), LEAKED(s0), LEAKED(s1), LEAKED(s2), LEAKED(s3), LEAKED(s4),
                          LEAKED(f14), LEAKED(f20), LEAKED(f22), LEAKED(f24), LEAKED(f26));
#else
            starski_update();
#endif
            break;
        case VEH_CYCLONE:
#ifdef NON_MATCHING
            cyclone_update(LEAKED(t6), LEAKED(t7), LEAKED(s0), LEAKED(s1), LEAKED(s2), LEAKED(s3), LEAKED(s4),
                          LEAKED(f14), LEAKED(f20), LEAKED(f22), LEAKED(f24), LEAKED(f26));
#else
            cyclone_update();
#endif
            break;
    }
}

/* Each frame (hd_game_frame): the visibility test's display list, a box of 8
 * vertices (12 triangles) under this frame's projection and culling view;
 * cull_step_delayed fills in the box per block of level cells (0x8024B8F4). */
void cull_start(Mtx *proj, Mtx *view) {
    Gfx dl[50];
    Gfx *gdl = dl;
    Vtx v[8];
    s32 i;

    for (i = 0; i < 8; i++) {
        v[i].v.flag = 0;
        v[i].v.tc[0] = 0;
        v[i].v.tc[1] = 0;
        v[i].v.cn[0] = 0;
        v[i].v.cn[1] = 0;
        v[i].v.cn[2] = 0;
        v[i].v.cn[3] = 0;
    }
    gSPSegment(gdl++, 0, 0);
    gSPSegment(gdl++, 1, osVirtualToPhysical(gStaticSegment));
    gSPDisplayList(gdl++, seg1RspInitDl);
    gSPMatrix(gdl++, PHYS(proj), G_MTX_PROJECTION | G_MTX_LOAD | G_MTX_NOPUSH);
    gImmp1(gdl++, G_RDPHALF_1, gPerspNorm);
    gSPMatrix(gdl++, PHYS(view), G_MTX_MODELVIEW | G_MTX_LOAD | G_MTX_NOPUSH);
    gSPVertex(gdl++, PHYS(v), 8, 0);
    gSP1Triangle(gdl++, 0, 1, 4, 0);
    gSP1Triangle(gdl++, 1, 4, 5, 0);
    gSP1Triangle(gdl++, 0, 3, 4, 0);
    gSP1Triangle(gdl++, 3, 4, 7, 0);
    gSP1Triangle(gdl++, 2, 3, 7, 0);
    gSP1Triangle(gdl++, 2, 6, 7, 0);
    gSP1Triangle(gdl++, 1, 2, 5, 0);
    gSP1Triangle(gdl++, 2, 5, 6, 0);
    gSP1Triangle(gdl++, 4, 5, 6, 0);
    gSP1Triangle(gdl++, 4, 6, 7, 0);
    gSP1Triangle(gdl++, 0, 1, 2, 0);
    gSP1Triangle(gdl++, 0, 2, 3, 0);
    gDPTileSync(gdl++);
    gSPEndDisplayList(gdl++);
    osWritebackDCache(dl, (s32) (gdl - dl) * sizeof(Gfx));
    osWritebackDCache(proj, 0x40);
    osWritebackDCache(view, 0x40);
    cull_step_delayed((s32) gLevelData, dl, v, (s32) (gdl - dl) * sizeof(Gfx));
}

/* Acts on the item selected in a text window (*sel) for the current game mode (pause menu, music volume,
 * controller mode, replay end), then clears *sel (0x8024BDA4).
 * Handles a Yoshi (menu) selection *sel for the current game mode */
void hd_handle_yoshi_selection(u16 *sel) {
    switch (gGameMode) {
        case MODE_LEVEL:
        case MODE_MAP_VIEW:
            switch (*sel) {
                case 1:
                    if (gCurVehicle == VEH_CRANE || gCurVehicle == VEH_BARGE_0B || gCurVehicle == VEH_BARGE_11 || gCurVehicle == VEH_BARGE_12) {
                        gYoshiGameItems[7].flags &= ~1;
                        gYoshiGameItems[7].color = 8;
                    } else {
                        gYoshiGameItems[7].flags |= 1;
                        gYoshiGameItems[7].color = 7;
                    }
                    yoshi_demand(YOSHI_DEMAND_OPEN | YW_PAUSE_OPTIONS);
                    gNextGameMode = MODE_LEVEL;
                    break;
                case 2:
                    stats_finish_level(gLevelStartTime);
                    break;
                case 7:
                    cont_show_method_option();
                    break;
                case 3:
                    if (gLevelInfo[levelno].kind & LEVEL_KIND_ANY_MISSION) {
                        fade_to_mode_speed(MODE_LEVEL_START, 0.5f);
                    } else {
                        fade_to_mode_speed(MODE_VEHICLE_SELECT, 0.5f);
                    }
                    break;
                case 8:
                    gNextGameMode = MODE_YOSHI_PAGES;
                    yoshi_demand(YOSHI_DEMAND_OPEN | YW_PAGES_FIRST);
                    gCamSnap = 1;
                    break;
                case 4:
                    fade_to_mode_speed(MODE_WORLD, 0.5f);
                    break;
                case 9:
                    if (music_get_master_volume() == 1.0) {
                        gYoshiWindows[YW_MUSIC_VOLUME].sel = 40;
                    } else {
                        gYoshiWindows[YW_MUSIC_VOLUME].sel = 39;
                    }
                    yoshi_demand(YOSHI_DEMAND_OPEN | YW_MUSIC_VOLUME);
                    break;
                case 0x1A7:
                case 0x1A8:
                    if (((*sel - 0x1A7) << gCurVehicle) ^ ((1 << gCurVehicle) & 0x10205)) {
                        debug_printf("selected controller mode yes\n");
                        players[playerNumber].controlModes |= 1 << gCurVehicle;
                        rec_record(0x80, 0, 0);
                    } else {
                        debug_printf("selected controller mode no\n");
                        players[playerNumber].controlModes &= ~(1 << gCurVehicle);
                        rec_record(0x40, 0, 0);
                    }
                    break;
                case 17:
                    gYoshiWindows[YW_STATUS].flags |= 0x80;
                    break;
                case 0xFFFF:
                    switch (currentYoshiWindow) {
                        case YW_PAUSE_OPTIONS:
                        case YW_STATUS:
                            debug_printf("TESTING PAUSE2 %d %d %d\n", gPaused, gPauseRequest, gUnpauseRequest);
                            yoshi_demand(YOSHI_DEMAND_OPEN | YW_PAUSE);
                            if (gHaveCarrier || gHaveShuttle) {
                                gNextGameMode = MODE_MAP_VIEW;
                                cull_restart(10);
                            }
                            break;
                        case YW_MUSIC_VOLUME:
                        case YW_CONTROL_METHOD:
                            debug_printf("TESTING PAUSE3 %d %d %d\n", gPaused, gPauseRequest, gUnpauseRequest);
                            yoshi_demand(YOSHI_DEMAND_OPEN | YW_PAUSE_OPTIONS);
                            break;
                    }
                    break;
                case 40:
                    music_set_master_volume(1.0f);
                    break;
                case 39:
                    music_set_master_volume(0.7f);
                    break;
            }
            break;
        case MODE_ACADEMY_MARKER:
            gNextGameMode = MODE_LEVEL;
            break;
        case MODE_REPLAY:
        case MODE_REPLAY_CARRIER_END:
            if ((players[playerNumber].medal[levelno] > 0 && players[playerNumber].medal[levelno] < 6) ? 1 : 0) {
                if (*sel == 0x18 || *sel == 0x22 || *sel == 0xFFFF) {
                    gNextGameMode = MODE_RESULTS;
                } else {
                    gNextGameMode = MODE_LEVEL_START;
                }
            } else {
                if (*sel == 0x18 || *sel == 0x22 || *sel == 0xFFFF) {
                    gNextGameMode = MODE_WORLD;
                } else {
                    gNextGameMode = MODE_LEVEL_START;
                }
            }
            break;
        case MODE_YOSHI_PAGES:
            if (*sel == 0xFFFF) {
                debug_printf("TESTING PAUSE %d %d %d\n", gPaused, gPauseRequest, gUnpauseRequest);
                yoshi_demand(YOSHI_DEMAND_OPEN | YW_PAUSE_OPTIONS);
            }
            gNextGameMode = MODE_LEVEL;
            break;
        default:
            debug_printf("Yoshi selection in illegal game mode\n");
            break;
    }
    *sel = 0;
}

/* Empty display-list hook (probably a compiled-out debug overlay): sets *len to 0 and returns gdl unchanged
 * (0x8024C404).
 * An empty hook after the frame's list (probably a compiled-out debug
 * overlay): zeroes *arg2 (gFrameDlLen; hd_close_display_list sets it again). */
Gfx *hd_debug_dl_stub(Gfx *arg0, DynamicBuf *arg1, s32 *arg2) {
    *arg2 = 0;
    return arg0;
}

/* Builds the level frame's top-level display list in dyn: segments, depth
 * clear, the matrices (ortho, cam_update's views, guPerspective with
 * gCamFovy), the sky, the level's display lists, vehicles, objects, effects
 * and the HUD; *len = its length (0x8024C414). */
Gfx *hd_build_frame_dl(DynamicBuf *dyn, s32 *len) {
    Gfx *gdl = dyn->dl;
    char money[16];
    char bonus[16];
    s32 height;

    gSPSegment(gdl++, 0, 0);
    gSPSegment(gdl++, 2, osVirtualToPhysical(dyn));
    gSPSegment(gdl++, 1, osVirtualToPhysical(gStaticSegment));
    gSPDisplayList(gdl++, seg1RdpInitDl);
    gSPDisplayList(gdl++, seg1RspInitDl);
    gDPPipeSync(gdl++);
    gDPSetCycleType(gdl++, G_CYC_FILL);
    gSPClearGeometryMode(gdl++, G_ZBUFFER);
    gDPSetDepthImage(gdl++, gDepthBufferPhys);
    gDPSetColorImage(gdl++, G_IM_FMT_RGBA, G_IM_SIZ_16b, 320, gDepthBufferPhys);
    gDPSetFillColor(gdl++, 0xFFFCFFFC);
    gDPFillRectangle(gdl++, 0, 0, 319, 239);
    guTranslate(&dyn->identity, 0.0f, 0.0f, 0.0f);
    guOrtho(&dyn->ortho, 0.0f, 319.0f, 239.0f, 0.0f, -20000.0f, 20000.0f, 1.0f);
    guOrtho(&dyn->orthoHi, 0.0f, 1279.0f, 959.0f, 0.0f, -20000.0f, 20000.0f, 1.0f);
    cam_update(&dyn->view, &dyn->lookAt, &dyn->cullView);
    gDPSetColorImage(gdl++, G_IM_FMT_RGBA, G_IM_SIZ_16b, 320, gFrameBufferPhys[gBufferIndex]);
    gdl = backdrop_draw(gdl, (u32) dyn, levelno, gCamYaw, gCamPitch, &height);
    gDPPipeSync(gdl++);
    gDPSetCycleType(gdl++, G_CYC_FILL);
    switch (levelno) {
        case 13:
        case 14:
        case 16:
        case 52:
            gDPSetFillColor(gdl++, 0xD55FD55F);
            break;
        case 15:
            gDPSetFillColor(gdl++, 0x10511051);
            break;
        default:
            gDPSetFillColor(gdl++, 0x00010001);
            break;
    }
    gDPPipeSync(gdl++);
    gDPFillRectangle(gdl++, 0, (height <= 0) ? 0 : height - 1, 319, 239);
    gDPPipeSync(gdl++);
    gSPLookAtX(gdl++, &seg2Base[0xF0]);
    gSPLookAtY(gdl++, (u8 *) &seg2Base[0xF0] + 0x10);
    gSPMatrix(gdl++, &seg2Base[2], G_MTX_PROJECTION | G_MTX_LOAD | G_MTX_NOPUSH);
    gSPMatrix(gdl++, &seg2Base[5], G_MTX_PROJECTION | G_MTX_MUL | G_MTX_NOPUSH);
    gSPMatrix(gdl++, &seg2Base[7], G_MTX_MODELVIEW | G_MTX_LOAD | G_MTX_NOPUSH);
    gImmp1(gdl++, G_RDPHALF_1, gPerspNorm);
    switch (gGameMode) {
        case MODE_REPLAY:
        case MODE_REPLAY_CARRIER_END:
            guPerspective(&dyn->persp, &gPerspNorm, gCamFovy, 1.3333334f, 10.0f, 20000.0f, 1.0f);
            break;
        case MODE_INTRO_APPROACH:
        case MODE_INTRO_FLY:
        case MODE_INTRO_LANDING:
            guPerspective(&dyn->persp, &gPerspNorm, gCamFovy, 1.3333334f, 10.0f, 10000.0f, 1.0f);
            break;
        default:
            guPerspective(&dyn->persp, &gPerspNorm, gCamFovy, 1.3333334f, 10.0f, 10000.0f, 1.0f);
            break;
    }
    gSPSetOtherMode(gdl++, G_SETOTHERMODE_H, 6, 2, 0);
    gSPClipRatio(gdl++, FRUSTRATIO_3);
    water_draw(&gdl, gBufferIndex, 0);
    gSPSegment(gdl++, 8, PHYS(gLevelSegment8));
    gSPClearGeometryMode(gdl++, -1);
    gSPDisplayList(gdl++, osVirtualToPhysical((void *) N64_IPTR(gLevelDl0[gBufferIndex])));
    gSPDisplayList(gdl++, gLevelFileDl0);
    gSPClearGeometryMode(gdl++, -1);
    gSPSetGeometryMode(gdl++, G_ZBUFFER);
    gSPDisplayList(gdl++, osVirtualToPhysical((void *) N64_IPTR(gLevelDl1[gBufferIndex])));
    gSPDisplayList(gdl++, gLevelFileDl1);
    if (!gCarrierHit && !gLevelDone) {
        gSPClearGeometryMode(gdl++, -1);
        gSPSetGeometryMode(gdl++, G_ZBUFFER | G_SHADE | G_SHADING_SMOOTH | G_CULL_BACK | G_LOD);
        gSPDisplayList(gdl++, SEG2(0x21410));
        gDPPipeSync(gdl++);
    }
    switch (gBufferIndex) {
        case 0:
            gSPSegment(gdl++, 10, osVirtualToPhysical(gObjPartMtx0));
            break;
        case 1:
            gSPSegment(gdl++, 10, osVirtualToPhysical(gObjPartMtx1));
            break;
    }
    gDPPipeSync(gdl++);
    gSPSetGeometryMode(gdl++, G_ZBUFFER | G_SHADE | G_SHADING_SMOOTH | G_CULL_BACK);
    gDPSetCycleType(gdl++, G_CYC_1CYCLE);
    gDPSetCombineMode(gdl++, G_CC_SHADE, G_CC_SHADE);
    gSPDisplayList(gdl++, SEG2(0x0A4E0));
    if (!(gGameMode & (MODE_REPLAY | MODE_REPLAY_CARRIER_END))) {
        trail_draw(&gdl, (struct TrailBuf *) dyn);
    }
    veh_update_fail_spin();
    veh_draw_shadows(&gdl, dyn, 0);
    obj_draw_ground_shadows(&gdl, (struct VtxBuf *) dyn);
    if (!gPaused && gLevelKind != LEVEL_KIND_PILL_MAZE && !(gGameMode & (MODE_REPLAY | MODE_REPLAY_CARRIER_END)) && !gPlayerHidden) {
        mb_draw(&gdl, (s32) dyn, gBufferIndex);
    }
    gSPClearGeometryMode(gdl++, -1);
    gSPSetGeometryMode(gdl++, G_ZBUFFER | G_SHADE | G_SHADING_SMOOTH | G_CULL_BACK | G_LOD);
    gSPDisplayList(gdl++, SEG2(0x0A580));
    gDPPipeSync(gdl++);
    gdl = commpt_draw(gdl, gBufferIndex, (Gfx *) dyn->subDls, &dyn->mtxPool[0x3C]);
    veh_draw_shadows(&gdl, dyn, 1);
    if (gHaveChopper) {
        veh_draw_chopper_shadow(&gdl, dyn);
    }
    rdu_draw(&gdl, (s32) dyn);
    fx_draw_power_pill(&gdl);
    tnt_draw(&gdl, (struct Dyn48D00 *) dyn);
    block_draw(&gdl, (struct Dyn4B5E0 *) dyn);
    shell_draw_all(&gdl, (struct Dyn *) dyn);
    crate_draw(&gdl, (s32) dyn);
    switch (gBufferIndex) {
        case 0:
            gSPDisplayList(gdl++, osVirtualToPhysical(gFxDlA0));
            break;
        case 1:
            gSPDisplayList(gdl++, osVirtualToPhysical(gFxDlA1));
            break;
    }
    ptcl_draw(&gdl, gBufferIndex);
    if (!(gGameMode & MODE_ATTRACT) || !gRecDemoNoLogo) {
        veh_draw_layer(&gdl, 0);
    }
    academy_draw_marker(&gdl);
    gSPClearGeometryMode(gdl++, -1);
    gSPSetGeometryMode(gdl++, G_ZBUFFER);
    gSPDisplayList(gdl++, osVirtualToPhysical((void *) N64_IPTR(gLevelDl2[gBufferIndex])));
    gSPDisplayList(gdl++, gLevelFileDl2);
    if (!(gGameMode & MODE_ATTRACT) || !gRecDemoNoLogo) {
        veh_draw_layer(&gdl, 1);
    }
    gSPClearGeometryMode(gdl++, -1);
    gSPSetGeometryMode(gdl++, G_ZBUFFER);
    gSPDisplayList(gdl++, osVirtualToPhysical((void *) N64_IPTR(gLevelDl3[gBufferIndex])));
    gSPDisplayList(gdl++, gLevelFileDl3);
    gSPClearGeometryMode(gdl++, -1);
    gSPSetGeometryMode(gdl++, G_ZBUFFER | G_SHADE | G_SHADING_SMOOTH | G_CULL_BACK | G_LOD);
    gSPDisplayList(gdl++, SEG2(0x0A918));
    gDPPipeSync(gdl++);
    if (!gCarrierHit && !gLevelDone) {
        gSPClearGeometryMode(gdl++, -1);
        gSPSetGeometryMode(gdl++, G_ZBUFFER | G_SHADE | G_SHADING_SMOOTH | G_CULL_BACK | G_LOD);
        gSPDisplayList(gdl++, SEG2(0x21478));
        gDPPipeSync(gdl++);
    }
    if (!(gGameMode & MODE_ATTRACT) || !gRecDemoNoLogo) {
        veh_draw_layer(&gdl, 2);
    }
    ghost_draw(&gdl, (Mtx *) dyn);
    if (gGameMode & (MODE_INTRO_APPROACH | MODE_LEVEL | MODE_MAP_VIEW | MODE_INTRO_FLY | MODE_INTRO_LANDING | MODE_LEVEL_START | MODE_ACADEMY_MARKER | MODE_YOSHI_PAGES)) {
        fx_draw_drift_quads(&gdl, gBufferIndex);
    }
    water_draw(&gdl, gBufferIndex, 1);
    veh_draw_shadows(&gdl, dyn, 2);
    if (gHaveChopper) {
        people_draw(&gdl, (struct DynBuf *) dyn);
    }
    switch (gBufferIndex) {
        case 0:
            gSPDisplayList(gdl++, osVirtualToPhysical(gFxDlB0));
            break;
        case 1:
            gSPDisplayList(gdl++, osVirtualToPhysical(gFxDlB1));
            break;
    }
    if (gHaveCarrier) {
        fx_draw_carrier_blast(&gdl, gBufferIndex);
    }
    PORT_ITAG(gdl, PORT_ITAG_HUD, 0);
    if (gHudDialLoaded && (gGameMode & (MODE_LEVEL | MODE_MAP_VIEW))) {
        PORT_WTAG(gdl, PORT_WTAG_LEFT, 0, 0); /* widescreen HUD sides: see port.h */
        hud_draw_boost_dial(&gdl, (s32) dyn, gBufferIndex, gCurVehicle);
    }
    if (gGameMode & (MODE_LEVEL | MODE_LEVEL_CLEAR | MODE_CARRIER_END | MODE_LEVEL_START | MODE_TIMED_END | MODE_YOSHI_PAGES)) {
        hud_panel_draw(&gdl, (s32) dyn, gBufferIndex);
    }
    if (gGameMode == MODE_MAP_VIEW && gHaveCarrier) {
        PORT_WTAG(gdl, PORT_WTAG_NONE, 0, 0); /* follows the player on screen */
        hud_draw_player_locator(&gdl, dyn, gBufferIndex, gPlayerX, gPlayerY, gPlayerZ);
    }
    text_begin_frame();
    if (gHudMissileIconLoaded && (gGameMode & (MODE_LEVEL | MODE_MAP_VIEW))) {
        PORT_WTAG(gdl, PORT_WTAG_LEFT, 0, 0);
        hud_draw_missile_icon(&gdl, (Gfx * N64P *) dyn, gBufferIndex, gCurVehicle);
    }
    if (gHudRamIconLoaded && (gGameMode & (MODE_LEVEL | MODE_MAP_VIEW))) {
        PORT_WTAG(gdl, PORT_WTAG_LEFT, 0, 0);
        hud_draw_ram_icon(&gdl, (Gfx * N64P *) dyn, gBufferIndex, gCurVehicle);
    }
    if (gGameMode & (MODE_LEVEL | MODE_MAP_VIEW)) {
        hud_draw_veh_overlay(&gdl, gCurVehicle);
    }
    if ((gLevelKind & LEVEL_KIND_MISSION) && (gGameMode & (MODE_LEVEL | MODE_LEVEL_CLEAR | MODE_MAP_VIEW | MODE_CARRIER_END | MODE_TIMED_END))) {
        util_itoa(gHudMoney, (u8 *) &money[1]);
        money[0] = '$';
        text_print((Gfx * N64P *) dyn, (u8 *) money, 0, 1, 0, 0x118, 0x12, 0x14, 0x14, 0, 0xFF, 0xFF, 0xFF, gHudAlpha);
        if (gValuePopupValue && !gPaused) {
            util_itoa(gValuePopupValue, (u8 *) &bonus[1]);
            bonus[0] = '$';
            text_print((Gfx * N64P *) dyn, (u8 *) bonus, 0, 1, 0, gValuePopupX, gValuePopupY, 0x23, 0x23, gValuePopupForward, 0xFF, 0xFF, 0xFF,
                          gValuePopupAlpha);
        }
    }
    if ((gGameMode & (MODE_LEVEL | MODE_MAP_VIEW | MODE_YOSHI_PAGES)) && (!(gLevelKind & LEVEL_KIND_ANY_MISSION) || gHaveCarrier || gHaveShuttle)) {
        PORT_WTAG(gdl, PORT_WTAG_NONE, 0, 0); /* follows the target in the 3D view */
        hud_draw_target_marker((struct SpriteVtxBuf *) dyn, &gdl, (gGameMode & MODE_MAP_VIEW) || currentYoshiWindow == YW_HINT_MARKER_ARROWS || currentYoshiWindow == YW_HINT_CARRIER_NEAR);
    }
    if (gNextGameMode == 0 && !areWeFading()) {
        if (!(gGameMode & (MODE_LEVEL | MODE_LEVEL_CLEAR | MODE_MAP_VIEW | MODE_CARRIER_END | MODE_LEVEL_START | MODE_TIMED_END | MODE_ACADEMY_MARKER | MODE_YOSHI_PAGES)) && PORT_GVI("hd_build_frame_dl", gScheduler.frameCount) % 50 * 60 / 60 >= 21 && yoshiState == 1 &&
            (!(gGameMode & MODE_ATTRACT) || (gRecDemoNumber && (gLogoState == 3 || gRecDemoNumber == 1))) &&
            (gGameMode != MODE_GLOBE_SEQ || gSeqSkippable) &&
            (!(gGameMode & (MODE_INTRO_APPROACH | MODE_INTRO_FLY | MODE_INTRO_LANDING)) || players[playerNumber].gameState) && levelno != 0x2F) {
            text_print((Gfx * N64P *) dyn, (u8 *) ("PRESS START"), 0, 1, 0, 0x5C, 0xC4, 0x1A, 0x1A, 1, 0xFF, 0xFF, 0xFF, 0xFF);
        }
        if (PORT_GVI("hd_build_frame_dl", gScheduler.frameCount) % 40 * 60 / 60 >= 16) {
            if (gPaused) {
                if (gGameMode == MODE_YOSHI_PAGES && yoshiState == 2) {
                    text_print((Gfx * N64P *) dyn, (u8 *) ("USE Z/R TO TURN PAGES"), gTurnPagesGlyphs, 0, 0, 0x18, 0x14, 0xF, 0xF, 1, 0xFF, 0xFF,
                                  0xFF, 0xFF);
                } else if (gGameMode == MODE_MAP_VIEW && currentYoshiWindow == YW_PAUSE && yoshiState == 2 && gHaveCarrier &&
                           !(gContButtons & (Z_TRIG | R_TRIG))) {
                    text_print((Gfx * N64P *) dyn, (u8 *) ("USE Z/R TO MOVE MAP"), gMoveMapGlyphs, 0, 0, 0x18, 0x14, 0xF, 0xF, 1, 0xFF, 0xFF,
                                  0xFF, 0xFF);
                }
            } else if (gGameMode == MODE_MAP_VIEW) {
                if (gHaveShuttle) {
                    text_print((Gfx * N64P *) dyn, (u8 *) ("SHUTTLE VIEW"), gShuttleViewGlyphs, 0, 0, 0x18, 0x14, 0xF, 0xF, 1, 0xFF, 0xFF, 0xFF,
                                  0xFF);
                } else {
                    text_print((Gfx * N64P *) dyn, (u8 *) ("MISSILE VIEW"), gMissileViewGlyphs, 0, 0, 0x18, 0x14, 0xF, 0xF, 1, 0xFF, 0xFF, 0xFF,
                                  0xFF);
                }
            }
        }
    }
    if (((gGameMode & (MODE_REPLAY | MODE_REPLAY_CARRIER_END)) || levelno == 0x26) && yoshiState == 1 && !areWeFading()) {
        if ((gGameMode & (MODE_REPLAY | MODE_REPLAY_CARRIER_END)) && gLevelKind != LEVEL_KIND_MISSION) {
            PORT_WTAG(gdl, PORT_WTAG_RIGHT, 0, 0);
            spr_draw_blinking(&gdl, (s32) dyn, gRecReplaySprite, 0x108, 0x12);
        } else {
            PORT_WTAG(gdl, PORT_WTAG_LEFT, 0, 0);
            spr_draw_blinking(&gdl, (s32) dyn, gRecReplaySprite, 0x18, 0x12);
        }
    }
    if ((gGameMode & (MODE_LEVEL | MODE_MAP_VIEW)) && gCamFixedEye) {
        PORT_WTAG(gdl, PORT_WTAG_RIGHT, 0, 0);
        spr_draw_blinking(&gdl, (s32) dyn, gCamFixedIconSprite, 0x108, 0xBE);
    }
    if ((gGameMode & (MODE_LEVEL | MODE_MAP_VIEW)) && gLevelKind == LEVEL_KIND_MISSION && !gPaused) {
        stats_award_bonuses();
    }
    if (gHaveCarrier && gGameMode == MODE_LEVEL) {
        PORT_WTAG(gdl, PORT_WTAG_LEFT, 0, 0);
        hud_draw_target_arrow(&gdl, (Mtx *) dyn, gPlayerX, gPlayerY, gPlayerZ, gCarrierX, gCarrierY, gCarrierZ);
    }
    if ((gLevelInfo[levelno].kind & LEVEL_KIND_ANY_MISSION) && gGameMode == MODE_LEVEL) {
        if (levelno != 0x32 ||
            ((players[playerNumber].medal[0x32] > 0 && players[playerNumber].medal[0x32] < 6) ? 1 : 0)) {
            PORT_WTAG(gdl, PORT_WTAG_LEFT, 0, 0);
            hud_draw_radar(&gdl, (Mtx *) dyn, gBufferIndex, gPlayerX, gPlayerZ, gCarrierX, gCarrierZ);
        }
    }
    PORT_WTAG(gdl, PORT_WTAG_NONE, 0, 0); /* full-screen fades, the text's own sides */
    if (gHaveCarrier || gHaveShuttle) {
        switch (gGameMode) {
            case MODE_LEVEL:
            case MODE_MAP_VIEW:
            case MODE_CARRIER_END:
            case MODE_GLOBE_SEQ:
                hud_level_complete(&gdl, (s32) dyn, gBufferIndex);
                hud_mission_failed(&gdl, (s32) dyn, gBufferIndex);
            case MODE_REPLAY:
            case MODE_REPLAY_CARRIER_END:
                if ((gCarrierHit || gLevelDone || cmo_hit_request || gLevelDoneRequest) && (gGameMode & (MODE_LEVEL | MODE_REPLAY | MODE_MAP_VIEW))) {
                    cull_restart(0x32);
                    if (gGameMode == MODE_REPLAY) {
                        gNextGameMode = MODE_REPLAY_CARRIER_END;
                    } else {
                        gNextGameMode = MODE_CARRIER_END;
                    }
                }
                break;
        }
    }
    PORT_WTAG(gdl, PORT_WTAG_TEXT_SIDES, 0, 0);
    text_flush_screen(&gdl, (Mtx *) dyn);
    PORT_WTAG(gdl, PORT_WTAG_NONE, 0, 0);
    *len = gdl - dyn->dl;
    return gdl;
}

/* Draws the shadow quads of every vehicle on the given layer (0x8024E4F4). */
void veh_draw_shadows(Gfx **gfx, DynamicBuf *dyn, u8 layer) {
    Gfx *gdl = *gfx;
    s32 i;
    s16 unused;
    s16 x;
    s16 y;
    s16 z;
    s16 w;
    s16 d;
    u8 all;
    f32 mf[4][4];
    f32 tmp[4][4];
    s16 th;
    s16 tw;
    u8 spin;

    if (layer == 0) {
        gShadowVtxCount = 0;
    }
    gDPPipeSync(gdl++);
    gDPSetTexturePersp(gdl++, G_TP_PERSP);
    gDPSetCycleType(gdl++, G_CYC_1CYCLE);
    gSPClearGeometryMode(gdl++, -1);
    gSPSetGeometryMode(gdl++, G_SHADE | G_SHADING_SMOOTH | G_CULL_BACK | G_LOD);
    gSPTexture(gdl++, 0xFFFF, 0xFFFF, 0, G_TX_RENDERTILE, G_ON);
    gDPSetCombineMode(gdl++, G_CC_MODULATERGBA, G_CC_MODULATERGBA);
    gDPSetTextureFilter(gdl++, G_TF_BILERP);
    gDPSetRenderMode(gdl++, 0x504340, 0);
    all = !gDriverHidden && (gVehEntered || (gGameMode & (MODE_INTRO_APPROACH | MODE_INTRO_FLY | MODE_INTRO_LANDING)));
    i = 0;
    while (&gVehicleShadows[i] != gVehicleShadowsEnd) {
        if ((gVehicleShadows[i].type != VEH_DRIVER || all) && gVehicleShadows[i].type != VEH_CHOPPER &&
            (gVehicleShadows[i].type != VEH_CARRIER || !gCarrierExplodeDone) &&
            (gVehicleShadows[i].type == VEH_SHUTTLE || !gPlayerHidden || !gHaveShuttle) && gVehicleShadows[i].layer == layer) {
            x = gVehicleShadows[i].x >> 5;
            y = gVehicleShadows[i].floorY >> 5;
            z = gVehicleShadows[i].z >> 5;
            w = gVehicleShadows[i].halfWidth;
            d = gVehicleShadows[i].halfDepth;
            spin = gVehicleShadows[i].type == gCurVehicle && gSurfaceMax == SURFACE_SPIN_SHADOW;
            if (spin) {
                guRotateF(tmp, (f32) gPlayerHeading * 360.0 / 4096.0, 0.0f, 1.0f, 0.0f);
            }
            guRotateF(mf, (f32) -gVehicleShadows[i].rotY * 360.0 / 4096.0, 0.0f, 1.0f, 0.0f);
            if (spin) {
                guMtxCatF(tmp, mf, mf);
            }
            guRotateF(tmp, (f32) gVehicleShadows[i].rotX * 360.0 / 4096.0, 1.0f, 0.0f, 0.0f);
            guMtxCatF(mf, tmp, tmp);
            guRotateF(mf, (f32) gVehicleShadows[i].rotZ * 360.0 / 4096.0, 0.0f, 0.0f, 1.0f);
            guMtxCatF(tmp, mf, mf);
            guRotateF(tmp, (f32) gVehicleShadows[i].rotY * 360.0 / 4096.0, 0.0f, 1.0f, 0.0f);
            guMtxCatF(mf, tmp, mf);
            guTranslateF(tmp, x, y, z);
            guMtxCatF(mf, tmp, mf);
            guMtxF2L(mf, &dyn->mtxPool[i]);
            if (spin) {
                gDPLoadTextureBlock(gdl++, OS_K0_TO_PHYSICAL(gHudArrowTex), G_IM_FMT_IA, G_IM_SIZ_8b, 32, 32, 0,
                                    G_TX_CLAMP, G_TX_CLAMP, G_TX_NOMASK, G_TX_NOMASK, G_TX_NOLOD, G_TX_NOLOD);
                th = 32, tw = 32;
            } else {
                gDPLoadTextureBlock(gdl++, PHYS((u32) gVehicleShadows + i * sizeof(VehShadow)), G_IM_FMT_IA, G_IM_SIZ_8b,
                                    64, 64, 0, G_TX_CLAMP, G_TX_CLAMP, G_TX_NOMASK, G_TX_NOMASK, G_TX_NOLOD,
                                    G_TX_NOLOD);
                th = 64, tw = 64;
            }
            dyn->shadowVtx[gShadowVtxCount].v.ob[0] = -w;
            dyn->shadowVtx[gShadowVtxCount].v.ob[1] = 0;
            dyn->shadowVtx[gShadowVtxCount].v.ob[2] = d;
            dyn->shadowVtx[gShadowVtxCount].v.flag = 0;
            dyn->shadowVtx[gShadowVtxCount].v.tc[0] = (th - 1) << 5;
            dyn->shadowVtx[gShadowVtxCount].v.tc[1] = (tw - 1) << 5;
            dyn->shadowVtx[gShadowVtxCount].v.cn[0] = 10;
            dyn->shadowVtx[gShadowVtxCount].v.cn[1] = 10;
            dyn->shadowVtx[gShadowVtxCount].v.cn[2] = 10;
            dyn->shadowVtx[gShadowVtxCount].v.cn[3] = 0x8C;
            gShadowVtxCount++;
            dyn->shadowVtx[gShadowVtxCount].v.ob[0] = w;
            dyn->shadowVtx[gShadowVtxCount].v.ob[1] = 0;
            dyn->shadowVtx[gShadowVtxCount].v.ob[2] = d;
            dyn->shadowVtx[gShadowVtxCount].v.flag = 0;
            dyn->shadowVtx[gShadowVtxCount].v.tc[0] = 0;
            dyn->shadowVtx[gShadowVtxCount].v.tc[1] = (tw - 1) << 5;
            dyn->shadowVtx[gShadowVtxCount].v.cn[0] = 10;
            dyn->shadowVtx[gShadowVtxCount].v.cn[1] = 10;
            dyn->shadowVtx[gShadowVtxCount].v.cn[2] = 10;
            dyn->shadowVtx[gShadowVtxCount].v.cn[3] = 0x8C;
            gShadowVtxCount++;
            dyn->shadowVtx[gShadowVtxCount].v.ob[0] = w;
            dyn->shadowVtx[gShadowVtxCount].v.ob[1] = 0;
            dyn->shadowVtx[gShadowVtxCount].v.ob[2] = -d;
            dyn->shadowVtx[gShadowVtxCount].v.flag = 0;
            dyn->shadowVtx[gShadowVtxCount].v.tc[0] = 0;
            dyn->shadowVtx[gShadowVtxCount].v.tc[1] = 0;
            dyn->shadowVtx[gShadowVtxCount].v.cn[0] = 10;
            dyn->shadowVtx[gShadowVtxCount].v.cn[1] = 10;
            dyn->shadowVtx[gShadowVtxCount].v.cn[2] = 10;
            dyn->shadowVtx[gShadowVtxCount].v.cn[3] = 0x8C;
            gShadowVtxCount++;
            dyn->shadowVtx[gShadowVtxCount].v.ob[0] = -w;
            dyn->shadowVtx[gShadowVtxCount].v.ob[1] = 0;
            dyn->shadowVtx[gShadowVtxCount].v.ob[2] = -d;
            dyn->shadowVtx[gShadowVtxCount].v.flag = 0;
            dyn->shadowVtx[gShadowVtxCount].v.tc[0] = (th - 1) << 5;
            dyn->shadowVtx[gShadowVtxCount].v.tc[1] = 0;
            dyn->shadowVtx[gShadowVtxCount].v.cn[0] = 10;
            dyn->shadowVtx[gShadowVtxCount].v.cn[1] = 10;
            dyn->shadowVtx[gShadowVtxCount].v.cn[2] = 10;
            dyn->shadowVtx[gShadowVtxCount].v.cn[3] = 0x8C;
            gShadowVtxCount++;
            PORT_ITAG(gdl, PORT_ITAG_SHADOW, i);
            if (gCarrierHit && !(gLevelKind & LEVEL_KIND_ANY_MISSION) && gVehicleShadows[i].type == gCurVehicle) {
                gSPMatrix(gdl++, &seg2Base[0x55], G_MTX_MODELVIEW | G_MTX_MUL | G_MTX_PUSH);
            }
            gSPMatrix(gdl++, &seg2Base[i + 11], G_MTX_MODELVIEW | G_MTX_MUL | G_MTX_PUSH);
            gSPVertex(gdl++, (Vtx *) seg2Base + gShadowVtxCount + 0x158, 4, 0);
            gSP1Triangle(gdl++, 0, 1, 2, 0);
            gSP1Triangle(gdl++, 0, 2, 3, 0);
            gSPPopMatrix(gdl++, G_MTX_MODELVIEW);
            if (gCarrierHit && !(gLevelKind & LEVEL_KIND_ANY_MISSION) && gVehicleShadows[i].type == gCurVehicle) {
                gSPPopMatrix(gdl++, G_MTX_MODELVIEW);
            }
            gDPPipeSync(gdl++);
        }
        i++;
    }
    PORT_ITAG(gdl, PORT_ITAG_NONE, 0);
    *gfx = gdl;
}

/* Draws the shadow quad under vehicle 0xFE, the BCT chopper (0x8024F520). */
void veh_draw_chopper_shadow(Gfx **gfx, DynamicBuf *dyn) {
    Gfx *gdl = *gfx;
    s32 i;
    u8 found;
    s16 x;
    s16 y;
    s16 z;
    s16 w;
    s16 d;

    gDPPipeSync(gdl++);
    gDPSetTexturePersp(gdl++, G_TP_PERSP);
    gDPSetCycleType(gdl++, G_CYC_1CYCLE);
    gSPClearGeometryMode(gdl++, -1);
    gSPSetGeometryMode(gdl++, G_SHADE | G_SHADING_SMOOTH | G_CULL_BACK | G_LOD);
    gSPTexture(gdl++, 0xFFFF, 0xFFFF, 0, G_TX_RENDERTILE, G_ON);
    gDPSetCombineMode(gdl++, G_CC_MODULATERGBA, G_CC_MODULATERGBA);
    gDPSetTextureFilter(gdl++, G_TF_BILERP);
    gDPSetRenderMode(gdl++, 0x504340, 0);
    found = 0;
    i = 0;
    while (!found) {
        if (gVehicleShadows[i].type == VEH_CHOPPER) {
            found = 1;
        } else {
            i++;
        }
    }
    gDPLoadTextureBlock(gdl++, PHYS((u32) gVehicleShadows + i * sizeof(VehShadow)), G_IM_FMT_IA, G_IM_SIZ_8b, 64, 64, 0, G_TX_CLAMP, G_TX_CLAMP,
                        G_TX_NOMASK, G_TX_NOMASK, G_TX_NOLOD, G_TX_NOLOD);
    x = gVehicleShadows[i].x >> 5;
    y = gVehicleShadows[i].floorY >> 5;
    z = gVehicleShadows[i].z >> 5;
    w = gVehicleShadows[i].halfWidth;
    d = gVehicleShadows[i].halfDepth;
    dyn->chopperShadowVtx[0].v.ob[0] = -w;
    dyn->chopperShadowVtx[0].v.ob[1] = 0;
    dyn->chopperShadowVtx[0].v.ob[2] = d;
    dyn->chopperShadowVtx[0].v.flag = 0;
    dyn->chopperShadowVtx[0].v.tc[0] = 0x7E0;
    dyn->chopperShadowVtx[0].v.tc[1] = 0x7E0;
    dyn->chopperShadowVtx[0].v.cn[0] = 10;
    dyn->chopperShadowVtx[0].v.cn[1] = 10;
    dyn->chopperShadowVtx[0].v.cn[2] = 10;
    dyn->chopperShadowVtx[0].v.cn[3] = 0x8C;
    dyn->chopperShadowVtx[1].v.ob[0] = w;
    dyn->chopperShadowVtx[1].v.ob[1] = 0;
    dyn->chopperShadowVtx[1].v.ob[2] = d;
    dyn->chopperShadowVtx[1].v.flag = 0;
    dyn->chopperShadowVtx[1].v.tc[0] = 0;
    dyn->chopperShadowVtx[1].v.tc[1] = 0x7E0;
    dyn->chopperShadowVtx[1].v.cn[0] = 10;
    dyn->chopperShadowVtx[1].v.cn[1] = 10;
    dyn->chopperShadowVtx[1].v.cn[2] = 10;
    dyn->chopperShadowVtx[1].v.cn[3] = 0x8C;
    dyn->chopperShadowVtx[2].v.ob[0] = w;
    dyn->chopperShadowVtx[2].v.ob[1] = 0;
    dyn->chopperShadowVtx[2].v.ob[2] = -d;
    dyn->chopperShadowVtx[2].v.flag = 0;
    dyn->chopperShadowVtx[2].v.tc[0] = 0;
    dyn->chopperShadowVtx[2].v.tc[1] = 0;
    dyn->chopperShadowVtx[2].v.cn[0] = 10;
    dyn->chopperShadowVtx[2].v.cn[1] = 10;
    dyn->chopperShadowVtx[2].v.cn[2] = 10;
    dyn->chopperShadowVtx[2].v.cn[3] = 0x8C;
    dyn->chopperShadowVtx[3].v.ob[0] = -w;
    dyn->chopperShadowVtx[3].v.ob[1] = 0;
    dyn->chopperShadowVtx[3].v.ob[2] = -d;
    dyn->chopperShadowVtx[3].v.flag = 0;
    dyn->chopperShadowVtx[3].v.tc[0] = 0x7E0;
    dyn->chopperShadowVtx[3].v.tc[1] = 0;
    dyn->chopperShadowVtx[3].v.cn[0] = 10;
    dyn->chopperShadowVtx[3].v.cn[1] = 10;
    dyn->chopperShadowVtx[3].v.cn[2] = 10;
    dyn->chopperShadowVtx[3].v.cn[3] = 0x8C;
    guTranslate(&dyn->chopperShadowTrans, x, y, z);
    guRotate(&dyn->chopperShadowRot, (f32) gChopperShadowHeading * 360.0 / 4096.0, 0.0f, 1.0f, 0.0f);
    PORT_ITAG(gdl, PORT_ITAG_SHADOWFE, 0);
    gSPMatrix(gdl++, &seg2Base[9], G_MTX_MODELVIEW | G_MTX_MUL | G_MTX_PUSH);
    gSPMatrix(gdl++, &seg2Base[10], G_MTX_MODELVIEW | G_MTX_MUL | G_MTX_NOPUSH);
    gSPVertex(gdl++, &seg2Base[0x63], 4, 0);
    gSP1Triangle(gdl++, 0, 1, 2, 0);
    gSP1Triangle(gdl++, 0, 2, 3, 0);
    gSPPopMatrix(gdl++, G_MTX_MODELVIEW);
    PORT_ITAG(gdl, PORT_ITAG_NONE, 0);
    *gfx = gdl;
}

/* Emits the display lists of every vehicle record (gVehicles) for one of the
 * frame's three layers (0x8024FC2C). */
void veh_draw_layer(Gfx **gfx, u8 lod) {
    Gfx *gdl = *gfx;
    s32 i;
    u8 unused;
    u8 alt;
    u8 all;

    all = !gDriverHidden && (gVehEntered || (gGameMode & (MODE_INTRO_APPROACH | MODE_INTRO_FLY | MODE_INTRO_LANDING)));
    i = 0;
    while (&gVehicles[i] != gVehiclesEnd) {
        if ((gVehicles[i].type != VEH_DRIVER || all) && (gVehicles[i].type != VEH_CARRIER || !gCarrierExplodeDone) &&
            (gVehicles[i].type == VEH_SHUTTLE || !gPlayerHidden || !gHaveShuttle)) {
            PORT_ITAG(gdl, PORT_ITAG_OBJ, i | (lod << 8));
            gSPSegment(gdl++, 6, osVirtualToPhysical(gVehicles[i].seg6));
            if ((gGameMode & (MODE_INTRO_APPROACH | MODE_INTRO_FLY | MODE_INTRO_LANDING)) && (gVehicles[i].type == VEH_CHOPPER || gVehicles[i].type == VEH_DRIVER)) {
                alt = gBufferIndex;
            } else {
                alt = gVehDrawBufferIndex;
            }
            if (alt) {
                gSPSegment(gdl++, 7, osVirtualToPhysical(gVehicles[i].seg7Odd));
            } else {
                gSPSegment(gdl++, 7, osVirtualToPhysical(gVehicles[i].seg7Even));
            }
            gDPPipeSync(gdl++);
            gDPSetEnvColor(gdl++, 0, 0, 0, gVehicles[i].alpha);
            gSPClearGeometryMode(gdl++, -1);
            if (gCarrierHit && !(gLevelKind & LEVEL_KIND_ANY_MISSION) && gVehicles[i].type == gCurVehicle) {
                gSPMatrix(gdl++, &seg2Base[0x54], G_MTX_MODELVIEW | G_MTX_LOAD | G_MTX_NOPUSH);
            }
            if (alt) {
                switch (lod) {
                    case 0:
                        gSPDisplayList(gdl++, osVirtualToPhysical(gVehicles[i].dlOdd0));
                        break;
                    case 1:
                        gSPDisplayList(gdl++, osVirtualToPhysical(gVehicles[i].dlOdd1));
                        break;
                    case 2:
                        gSPDisplayList(gdl++, osVirtualToPhysical(gVehicles[i].dlOdd2));
                        break;
                }
            } else {
                switch (lod) {
                    case 0:
                        gSPDisplayList(gdl++, osVirtualToPhysical(gVehicles[i].dlEven0));
                        break;
                    case 1:
                        gSPDisplayList(gdl++, osVirtualToPhysical(gVehicles[i].dlEven1));
                        break;
                    case 2:
                        gSPDisplayList(gdl++, osVirtualToPhysical(gVehicles[i].dlEven2));
                        break;
                }
            }
            PORT_ITAG(gdl, PORT_ITAG_NONE, 0);
            gSPMatrix(gdl++, &seg2Base[7], G_MTX_MODELVIEW | G_MTX_LOAD | G_MTX_NOPUSH);
        }
        i++;
    }
    *gfx = gdl;
}

/* When a level without a carrier fails (gCarrierHit), spins the player's vehicle and its shadow around the
 * player while shrinking them, then spawns explosion effect 0x13 once the scale reaches 0 (0x802502EC).
 * Spins and shrinks the player's current vehicle when a level without a carrier fails */
void veh_update_fail_spin(void) {
    f32 mf[4][4];
    f32 tmp[4][4];
    s32 i;
    s16 x;
    s16 y;
    s16 z;

    if (gCarrierHit != 0 && !(gLevelKind & LEVEL_KIND_ANY_MISSION)) {
        guTranslateF(mf, -(f32) gPlayerX / 32.0, -(f32) gPlayerY / 32.0, -(f32) gPlayerZ / 32.0);
        guScaleF(tmp, gFailSpinScale, gFailSpinScale, gFailSpinScale);
        guMtxCatF(mf, tmp, mf);
        guRotateF(tmp, gFailSpinAngle, 0.0f, 1.0f, 0.0f);
        guMtxCatF(mf, tmp, mf);
        guTranslateF(tmp, (f32) gPlayerX / 32.0, (f32) gPlayerY / 32.0, (f32) gPlayerZ / 32.0);
        guMtxCatF(mf, tmp, mf);
        guMtxF2L(mf, &gDynamicBufs[gBufferIndex].failSpinVehMtx);
        i = 0;
        while (gVehicleShadows[i].type != gCurVehicle) {
            i++;
        }
        x = gVehicleShadows[i].x >> 5;
        y = gVehicleShadows[i].floorY >> 5;
        z = gVehicleShadows[i].z >> 5;
        guTranslateF(mf, -x, -y, -z);
        guScaleF(tmp, gFailSpinScale, gFailSpinScale, gFailSpinScale);
        guMtxCatF(mf, tmp, mf);
        guRotateF(tmp, gFailSpinAngle, 0.0f, 1.0f, 0.0f);
        guMtxCatF(mf, tmp, mf);
        guTranslateF(tmp, x, y, z);
        guMtxCatF(mf, tmp, mf);
        guMtxF2L(mf, &gDynamicBufs[gBufferIndex].failSpinShadowMtx);
        if (gFailSpinDone == 0) {
            gFailSpinScale = gFailSpinScale - 0.04;
            if (gFailSpinScale < 0.0) {
                gFailSpinScale = 0.0f;
            }
        }
        if (gFailSpinScale == 0.0 && gFailSpinDone == 0) {
            fx_spawn_at(gPlayerX, gPlayerY, gPlayerZ, 0x13, 400000);
            gFailSpinDone = 1;
        }
        gFailSpinAngle += gFailSpinRate;
        gFailSpinRate = gFailSpinRate + 3.0;
    }
}

/* camera */
/* gMarkedTargetPts: the marked target's 4 marker points (ObjectModel.markerPts, s16 x, y, z
 * each); the camera aims at the middle of points 0 and 3 */
typedef struct {
    s16 x0;
    s16 y0;
    s16 z0;
    u8 pad6[0xC];
    s16 x3;
    s16 y3;
    s16 z3;
} MarkerPts;
/* gRdus: an RDU (26570.c's RduRecord) */
typedef struct {
    s16 x; /* position, >> 5 */
    s16 y;
    s16 z;
    u8 pad6[0x82];
} Node88; /* 0x88 bytes */
#ifndef NON_MATCHING
extern Node88 *gRdus;
#endif
extern u8 gCamAtSettled; /* target easing settled */
extern s32 gCamAtHeight; /* target height offset */
extern u8 gCamOnEndObject;
extern u8 gCamOnMarkedTarget;
extern u8 gCamOnCarrierTarget;
extern s32 gCamEyeGoalX; /* wanted eye x, y, z (<< 11) */
extern s32 gCamEyeGoalY;
extern s32 gCamEyeGoalZ;
extern s16 gCamTiltMin;
extern f32 gCamAtGoalX; /* wanted target x, y, z */
extern f32 gCamAtGoalY;
extern f32 gCamAtGoalZ;
extern f32 gCamAtX; /* target x, y, z */
extern f32 gCamAtY;
extern f32 gCamAtZ;
extern f32 gCamAtEaseX; /* target easing deltas */
extern f32 gCamAtEaseY;
extern f32 gCamAtEaseZ;
extern u16 gCamEasedHeading; /* eased heading */
extern s16 gCamType; /* camera type */
extern s16 gCamPrevType; /* previous camera type */
#ifndef NON_MATCHING
extern MarkerPts *gMarkedTargetPts;
#endif
f32 cam_eye_scale(f32, f32, f32, f32, f32, f32);
void cam_polar_offset(s32, f32, s32 *, s32 *);
void cam_turn_heading(void);

#define SINE(a) hd_sinf((f32) (s16) (a) / 16384.0f * 1.5708)
#define COSINE(a) hd_cosf((f32) (s16) (a) / 16384.0f * 1.5708)

/* The camera: chooses the wanted eye and target for the mode and the
 * vehicle (gCamType), eases toward them (or snaps, gCamSnap), adds the screen
 * shake, and builds the drawing view (mtx, lookAt: guLookAtReflect) and the
 * culling view (view: the eye pulled in by cam_eye_scale) (0x802507C8). */
void cam_update(Mtx *mtx, LookAt *lookAt, Mtx *view) {
    f32 rateX;
    f32 rateY;
    f32 rateZ;
    f32 dampX;
    f32 dampY;
    f32 dampZ;
    s32 pad;
    s16 ang;
    s16 sn;
    s16 cs;
#ifdef NON_MATCHING
    s32 offX = 0; /* camera modes without a case leave it indeterminate in the original */
#else
    s32 offX;
#endif
#ifdef NON_MATCHING
    s32 offZ = 0; /* camera modes without a case leave it indeterminate in the original */
#else
    s32 offZ;
#endif
    f32 diff;
#ifdef NON_MATCHING
    s32 radius = 0; /* camera ids without a case leave it indeterminate in the original */
#else
    s32 radius;
#endif
#ifdef NON_MATCHING
    f32 tiltRate = 0.0f; /* camera ids without a case leave it indeterminate in the original */
#else
    f32 tiltRate;
#endif
#ifdef NON_MATCHING
    f32 turnRate = 0.0f; /* camera ids without a case leave it indeterminate in the original */
#else
    f32 turnRate;
#endif
    s32 shakeX;
    s32 shakeY;
    s32 shakeZ;
    f32 eyeX;
    f32 eyeY;
    f32 eyeZ;
    f32 posX;
    f32 posY;
    f32 posZ;
    f32 scale;
    f32 dist;
    s16 tilt;
    u8 inShuttle;
    s32 px;
    s32 pz;
    u8 changed;
    f32 adx;
    f32 adz;
    f32 atX;
    f32 atZ;
    s16 angle;
    s32 tx;
    s32 ty;
    s32 tz;
    s8 quadrant;
    s16 pitch2;
    s16 pitch1;

    changed = 0;
    cam_turn_heading();
    if (gShuttleCamFollow && (gGameMode & (MODE_CARRIER_END | MODE_REPLAY_CARRIER_END))) {
        gShuttleCamFollow = 0;
    }
    inShuttle = gHaveShuttle && ((gGameMode & (MODE_REPLAY | MODE_MAP_VIEW | MODE_INTRO_FLY)) || gShuttleCamFollow);
    if (gGameMode == MODE_LEVEL_CLEAR || (currentYoshiWindow == YW_HINT_TARGETS_CLEARED && yoshiState == 2 && (u16) gYoshiWindows[currentYoshiWindow].sel == 0x14D)) {
        if (!gCamOnEndObject) {
            changed = 1;
        }
        gCamOnEndObject = 1;
    } else {
        if (gCamOnEndObject) {
            changed = 1;
        }
        gCamOnEndObject = 0;
    }
    if (currentYoshiWindow == YW_HINT_CARRIER_NEAR && yoshiState == 2 && (u16) gYoshiWindows[currentYoshiWindow].sel == 0x130) {
        if (!gCamOnCarrierTarget) {
            changed = 1;
        }
        gCamOnCarrierTarget = 1;
    } else {
        if (gCamOnCarrierTarget) {
            changed = 1;
        }
        gCamOnCarrierTarget = 0;
    }
    if (currentYoshiWindow == YW_HINT_MARKER_ARROWS && yoshiState == 2 && (u16) gYoshiWindows[currentYoshiWindow].sel != 0x164) {
        if (!gCamOnMarkedTarget) {
            changed = 1;
        }
        gCamOnMarkedTarget = 1;
    } else {
        if (gCamOnMarkedTarget) {
            changed = 1;
        }
        gCamOnMarkedTarget = 0;
    }
    if (currentYoshiWindow == YW_CONTROL_METHOD && yoshiState != 8 && !gCamFixedEye) {
        if (!gCamWindowView) {
            gCamSnap = changed = 1;
        }
        gCamWindowView = 1;
    } else {
        if (gCamWindowView) {
            gCamSnap = changed = 1;
        }
        gCamWindowView = 0;
    }
    gCamSnap |= inShuttle || gGameMode == MODE_MAP_VIEW;
    gCamSnap |= gFramesInMode < 2;
    if (gCarrierHit && (gGameMode & (MODE_CARRIER_END | MODE_REPLAY_CARRIER_END | MODE_GLOBE_SEQ))) {
        rateX = 0.2f;
        rateY = 0.1f;
        rateZ = 0.2f;
    } else if ((yoshiState == 2 && (currentYoshiWindow == YW_HINT_MARKER_ARROWS || currentYoshiWindow == YW_HINT_TARGETS_CLEARED || currentYoshiWindow == YW_HINT_CARRIER_NEAR)) ||
               (gLevelDone && (gGameMode & (MODE_CARRIER_END | MODE_REPLAY_CARRIER_END | MODE_GLOBE_SEQ))) || gGameMode == MODE_LEVEL_CLEAR) {
        rateX = 0.08f;
        rateY = 0.03f;
        rateZ = 0.08f;
    } else if (gGameMode & (MODE_INTRO_APPROACH | MODE_INTRO_FLY | MODE_INTRO_LANDING)) {
        rateX = 0.06f;
        rateY = 0.03f;
        rateZ = 0.03f;
    } else if ((gLevelKind == LEVEL_KIND_RDUS || gLevelKind == LEVEL_KIND_PILL_MAZE) && gGameMode == MODE_REPLAY) {
        rateX = 0.1f;
        rateY = 0.03f;
        rateZ = 0.1f;
    } else if (gCurVehicle == VEH_JBOMB) {
        rateX = 0.5f;
        rateY = 0.06f;
        rateZ = 0.5f;
    } else {
        rateX = 0.5f;
        rateY = 0.03f;
        rateZ = 0.5f;
    }
    dampX = 0.95f;
    dampY = 0.97f;
    dampZ = 0.95f;
    if (gGameMode == MODE_REPLAY && levelno == 0x3B) {
        rateX = 1.0f;
        rateY = 1.0f;
        rateZ = 1.0f;
        dampX = 1.0f;
        dampY = 1.0f;
        dampZ = 1.0f;
    }
    switch (gGameMode) {
        case MODE_MAP_VIEW:
        case MODE_CARRIER_END:
        case MODE_REPLAY_CARRIER_END:
            if (gHaveShuttle) {
                gCamType = VEH_SHUTTLE;
            } else {
                gCamType = VEH_CARRIER;
            }
            break;
        case MODE_INTRO_APPROACH:
        case MODE_INTRO_FLY:
        case MODE_INTRO_LANDING:
            if (gHaveShuttle) {
                gCamType = VEH_SHUTTLE;
            } else {
                gCamType = VEH_CHOPPER;
            }
            break;
        default:
            gCamType = gCurVehicle;
            break;
    }
    tilt = gPlayerSpeed;
    if (!gVehEntered) {
        gCamAtHeight = cam_load_level_defaults();
    }
    if (((gGameMode & MODE_ATTRACT) && gRecDemoNoLogo) || !gVehEntered) {
        radius = 0;
        tiltRate = 0.0f;
        turnRate = 0.0f;
    } else {
        switch (gCamType) {
            case VEH_TRAIN:
                radius = 11;
                tiltRate = 0.75f;
                turnRate = 30.0f;
                gCamAtHeight = 0;
                gCamTiltMax = 0xEB;
                gCamTiltMin = -0xA5;
                break;
            case VEH_BALLISTA:
                radius = 9;
                tiltRate = 0.75f;
                turnRate = 30.0f;
                gCamAtHeight = 0;
                gCamTiltMax = 0xF5;
                gCamTiltMin = -0x87;
                break;
            case VEH_BARGE_0B:
            case VEH_BARGE_11:
            case VEH_BARGE_12:
                radius = 11;
                tiltRate = 0.75f;
                turnRate = 30.0f;
                gCamAtHeight = 0;
                gCamTiltMax = 0xEB;
                gCamTiltMin = -0xA5;
                break;
            case VEH_AMDREAM:
                radius = 7;
                tiltRate = 3.0f;
                turnRate = 30.0f;
                gCamAtHeight = 0;
                gCamTiltMax = 0x14F;
                gCamTiltMin = -0xA5;
                break;
            case VEH_STARSKI:
                radius = 7;
                tiltRate = 3.0f;
                turnRate = 30.0f;
                gCamAtHeight = 0;
                gCamTiltMax = 0x131;
                gCamTiltMin = -0xA5;
                break;
            case VEH_POLICE:
                radius = 7;
                tiltRate = 3.0f;
                turnRate = 30.0f;
                gCamAtHeight = 0;
                gCamTiltMax = 0x145;
                gCamTiltMin = -0xA5;
                break;
            case VEH_ATEAM:
                radius = 7;
                tiltRate = 3.0f;
                turnRate = 30.0f;
                gCamAtHeight = 0;
                gCamTiltMax = 0x13B;
                gCamTiltMin = -0xA5;
                break;
            case VEH_JBOMB:
                radius = 11;
                tiltRate = 0.75f;
                turnRate = 30.0f;
                gCamAtHeight = 1000;
                gCamTiltMax = 0x87;
                gCamTiltMin = -0x55;
                break;
            case VEH_CRANE:
                radius = 11;
                tiltRate = 0.75f;
                turnRate = 30.0f;
                gCamAtHeight = 5000;
                gCamTiltMax = 0;
                gCamTiltMin = 0;
                break;
            case VEH_BACKLASH:
                radius = 9;
                tiltRate = 0.75f;
                turnRate = 30.0f;
                gCamAtHeight = 0;
                gCamTiltMax = 0xCD;
                gCamTiltMin = -0x69;
                break;
            case VEH_RAMDOZER:
                radius = 9;
                tiltRate = 0.75f;
                turnRate = 30.0f;
                gCamAtHeight = 0;
                gCamTiltMax = 0xEB;
                gCamTiltMin = -0xA5;
                break;
            case VEH_SKYFALL:
                radius = 7;
                tiltRate = 3.0f;
                turnRate = 30.0f;
                gCamAtHeight = 0;
                gCamTiltMax = 0xEB;
                gCamTiltMin = -0xA5;
                break;
            case VEH_THUNDERFIST:
                radius = 11;
                tiltRate = 0.75f;
                turnRate = 30.0f;
                gCamAtHeight = 2000;
                gCamTiltMax = 0x91;
                gCamTiltMin = -0x55;
                break;
            case VEH_CYCLONE:
                radius = 2;
                tiltRate = 0.75f;
                turnRate = 30.0f;
                gCamAtHeight = 600;
                gCamTiltMax = 0x91;
                gCamTiltMin = -0x55;
                break;
            case VEH_SIDESWIPE:
                radius = 11;
                tiltRate = 0.75f;
                turnRate = 30.0f;
                gCamAtHeight = 0;
                gCamTiltMax = 0xCD;
                gCamTiltMin = -0xA5;
                break;
            case VEH_DRIVER:
                radius = 20;
                tiltRate = 0.25f;
                turnRate = 15.0f;
                gCamAtHeight = 0;
                gCamTiltMax = 0x55;
                gCamTiltMin = -0x19;
                break;
            case VEH_CARRIER:
                radius = 11;
                tiltRate = 0.75f;
                turnRate = 30.0f;
                tilt = tilt / 2;
                gCamTiltMax = 0;
                gCamTiltMin = 0;
                break;
            case VEH_CHOPPER:
                radius = 11;
                tiltRate = 0.75f;
                turnRate = 30.0f;
                tilt = gChopperCamSpeed;
                gCamAtHeight = 0;
                gPlayerMoveHeading = gChopperCamHeading;
                gCamTiltMax = 0;
                gCamTiltMin = 0;
                break;
            case VEH_SHUTTLE:
                radius = 11;
                tiltRate = 0.75f;
                turnRate = 30.0f;
                gCamAtHeight = 0;
                tilt = tilt / 2;
                gPlayerMoveHeading = gPlayerMoveHeading / 2;
                gCamTiltMax = 0;
                gCamTiltMin = 0;
                break;
        }
    }
    if (gGameMode == MODE_REPLAY) {
        radius = radius / 2;
    }
    if (tilt > gCamTiltMax) {
        tilt = gCamTiltMax;
    }
    if (tilt < gCamTiltMin) {
        tilt = gCamTiltMin;
    }
    if ((tilt > 0 && gHeldA && !gPaused && (gGameMode & (MODE_ATTRACT | MODE_LEVEL | MODE_MAP_VIEW))) ||
        (tilt < 0 && gHeldB && !gPaused && (gGameMode & (MODE_ATTRACT | MODE_LEVEL | MODE_MAP_VIEW)))) {
        if (gCamTilt < tilt) {
            gCamTilt += tiltRate;
        }
        if (gCamTilt > tilt) {
            gCamTilt -= tiltRate;
        }
    } else {
        if (gCamTilt < tilt && tilt <= 0) {
            gCamTilt += tiltRate * 10.0f;
            if (tilt < gCamTilt) {
                gCamTilt = tilt;
            }
        }
        if (gCamTilt > tilt && tilt >= 0) {
            gCamTilt = gCamTilt - tiltRate * 10.0f;
            if (gCamTilt < tilt) {
                gCamTilt = tilt;
            }
        }
    }
    diff = gPlayerMoveHeading - gCamTurn;
    if (diff < 0.0) {
        diff = 0.0 - diff;
    }
    if (diff > 2048.0) {
        if (gCamTurn > gPlayerMoveHeading) {
            gCamTurn = gCamTurn + turnRate;
            if (gCamTurn > 4095.0) {
                gCamTurn = gCamTurn - 4095.0;
                if (gPlayerMoveHeading < gCamTurn) {
                    gCamTurn = gPlayerMoveHeading;
                }
            }
        } else {
            gCamTurn = gCamTurn - turnRate;
            if (gCamTurn < 0.0) {
                gCamTurn = gCamTurn + 4095.0;
                if (gCamTurn < gPlayerMoveHeading) {
                    gCamTurn = gPlayerMoveHeading;
                }
            }
        }
    } else {
        if (gCamTurn > gPlayerMoveHeading) {
            gCamTurn = gCamTurn - turnRate;
            if (gCamTurn < gPlayerMoveHeading) {
                gCamTurn = gPlayerMoveHeading;
            }
        }
        if (gCamTurn < gPlayerMoveHeading) {
            gCamTurn = gCamTurn + turnRate;
            if (gPlayerMoveHeading < gCamTurn) {
                gCamTurn = gPlayerMoveHeading;
            }
        }
    }
    ang = (s16) gCamTurn % 1024;
    cs = hd_cosf((f32) ang / 1024.0f * 1.5708) * ((f32) radius * gCamTilt);
    sn = hd_sinf((f32) ang / 1024.0f * 1.5708) * ((f32) radius * gCamTilt);
    if (gCamTurn >= 0.0f && gCamTurn < 1024.0f) {
        offX = sn;
        offZ = cs;
    }
    if (gCamTurn >= 1024.0f && gCamTurn < 2048.0f) {
        offX = cs;
        offZ = -sn;
    }
    if (gCamTurn >= 2048.0f && gCamTurn < 3072.0f) {
        offX = -sn, offZ = -cs;
    }
    if (gCamTurn >= 3072.0f && gCamTurn < 4096.0f) {
        offX = -cs;
        offZ = sn;
    }
    switch (gGameMode) {
        case MODE_MAP_VIEW:
            if (gHaveShuttle) {
                if (gCamSnap) {
                    gCamEasedHeading = gShuttleHeading * 16;
                } else {
                    gCamEasedHeading = (s16) ((u16) gShuttleHeading * 16 - gCamEasedHeading) / 10 + (s16) gCamEasedHeading;
                }
                gCamEyeGoalX = (s32) (gShuttlePos - SINE(gCamEasedHeading) * 8000.0f + offX) << 11;
                gCamEyeGoalY = (gShuttlePosY + 0xDAC) << 11;
                gCamEyeGoalZ = (s32) (gShuttlePosZ - COSINE(gCamEasedHeading) * 8000.0f + offZ) << 11;
            } else {
                gCamEyeGoalX = (gCarrierX + 4000) << 11;
                gCamEyeGoalY = (gCarrierY + 25000) << 11;
                gCamEyeGoalZ = (gCarrierZ + gMapViewScrollZ + 7000) << 11;
            }
            break;
        case MODE_REPLAY:
            switch (gLevelKind) {
                case LEVEL_KIND_MISSION:
                case LEVEL_KIND_MISSION_TIMED:
                    if (gHaveCarrier) {
                        gCamEyeGoalX = (gCarrierX + offX) << 11;
                        gCamEyeGoalY = (gCarrierY + 2000) << 11;
                        gCamEyeGoalZ = (gCarrierZ + offZ) << 11;
                    }
                    if (gHaveShuttle) {
                        if (gFramesInMode < 2) {
                            gCamEasedHeading = gShuttleHeading * 16;
                        } else {
                            gCamEasedHeading = (s16) ((u16) gShuttleHeading * 16 - gCamEasedHeading) / 10 + (s16) gCamEasedHeading;
                        }
                        gCamEyeGoalX = (s32) (gShuttlePos - SINE(gCamEasedHeading) * 8000.0f + offX) << 11;
                        gCamEyeGoalY = (gShuttlePosY + 0xDAC) << 11;
                        gCamEyeGoalZ = (s32) (gShuttlePosZ - COSINE(gCamEasedHeading) * 8000.0f + offZ) << 11;
                    }
                    if (gHaveCarrier || gHaveShuttle) {
                        break;
                    }
                case LEVEL_KIND_TARGETS:
                case LEVEL_KIND_DAMAGE:
                    if (!gCamSnap) {
                        gCamEasedHeading = (s16) ((u16) gPlayerMoveHeading * 16 - gCamEasedHeading) / 10 + (s16) gCamEasedHeading;
                    } else {
                        gCamEasedHeading = gPlayerMoveHeading * 16;
                    }
                    gCamEyeGoalX = (s32) (gPlayerX - SINE(gCamEasedHeading) * (gCamDistance * 2.0f) + offX) << 11;
                    gCamEyeGoalY = (gCamHeight + gPlayerY) << 11;
                    gCamEyeGoalZ = (s32) (gPlayerZ - COSINE(gCamEasedHeading) * (gCamDistance * 2.0f) + offZ) << 11;
                    break;
                case LEVEL_KIND_RACE:
                case LEVEL_KIND_BUILDINGS:
                    if (!gCamSnap) {
                        gCamEasedHeading = (s16) ((u16) gPlayerMoveHeading * 16 - gCamEasedHeading) / 10 + (s16) gCamEasedHeading;
                    } else {
                        gCamEasedHeading = gPlayerMoveHeading * 16;
                    }
                    gCamEyeGoalX = (s32) (SINE(gCamEasedHeading) * (gCamDistance * 2.0f) + gPlayerX + offX) << 11;
                    gCamEyeGoalY = (gCamHeight + gPlayerY + gPadStickY * 50) << 11;
                    gCamEyeGoalZ = (s32) (COSINE(gCamEasedHeading) * (gCamDistance * 2.0f) + gPlayerZ + offZ) << 11;
                    break;
                case LEVEL_KIND_RDUS:
                case LEVEL_KIND_PILL_MAZE:
                    gCamEyeGoalX = ((gRdus[gRduLitOrder[gScoreRdus]].x << 5) + offX) << 11;
                    gCamEyeGoalY = ((gRdus[gRduLitOrder[gScoreRdus]].y << 5) + 2000) << 11;
                    gCamEyeGoalZ = ((gRdus[gRduLitOrder[gScoreRdus]].z << 5) + offZ) << 11;
                    break;
            }
            break;
        case MODE_CARRIER_END:
            if (gHaveShuttle) {
                gCamEyeGoalX = (gShuttlePos + offX - 6000) << 11;
                gCamEyeGoalY = (gShuttlePosY + 0xD48) << 11;
                gCamEyeGoalZ = (gShuttlePosZ + offZ + 6000) << 11;
            } else if (gCarrierHit) {
                gCamEyeGoalX = (gCarrierX + offX + 16000) << 11;
                gCamEyeGoalY = (gCarrierY + 15000) << 11;
                gCamEyeGoalZ = (gCarrierZ + offZ - 3000) << 11;
            } else {
                gCamEyeGoalX = (gCarrierX + offX + 4000) << 11;
                gCamEyeGoalY = (gCarrierY + 2400) << 11;
                if (levelno == 0) {
                    gCamEyeGoalY += 0x5DC000;
                }
                gCamEyeGoalZ = (gCarrierZ + offZ - 4000) << 11;
            }
            break;
        case MODE_REPLAY_CARRIER_END:
            if (gHaveShuttle) {
                gCamEyeGoalX = (gShuttlePos + offX + 6000) << 11;
                gCamEyeGoalY = (gShuttlePosY + 0xD48) << 11;
                gCamEyeGoalZ = (gShuttlePosZ + offZ - 6000) << 11;
            } else if (gCarrierHit) {
                gCamEyeGoalX = (gCarrierX + offX - 16000) << 11;
                gCamEyeGoalY = (gCarrierY + 15000) << 11;
                gCamEyeGoalZ = (gCarrierZ - offZ + 3000) << 11;
            } else {
                gCamEyeGoalX = (gCarrierX + offX - 4000) << 11;
                gCamEyeGoalY = (gCarrierY + 2400) << 11;
                if (levelno == 0) {
                    gCamEyeGoalY += 0x5DC000;
                }
                gCamEyeGoalZ = (gCarrierZ + offZ + 4000) << 11;
            }
            break;
        case MODE_INTRO_APPROACH:
        case MODE_INTRO_FLY:
            if (gHaveShuttle) {
                if (gFramesInMode < 2) {
                    gCamEasedHeading = gShuttleHeading * 16;
                } else {
                    gCamEasedHeading = (s16) ((u16) gShuttleHeading * 16 - gCamEasedHeading) / 10 + (s16) gCamEasedHeading;
                }
                gCamEyeGoalX = (s32) (gShuttlePos - SINE(gCamEasedHeading) * 8000.0f + offX) << 11;
                gCamEyeGoalY = (gShuttlePosY + 0xDAC) << 11;
                gCamEyeGoalZ = (s32) (gShuttlePosZ - COSINE(gCamEasedHeading) * 8000.0f + offZ) << 11;
            } else {
                gCamEyeGoalX = (gPlayerX + offX - 500) << 11;
                gCamEyeGoalY = (gPlayerY + 6000) << 11;
                gCamEyeGoalZ = (gPlayerZ + offZ + 500) << 11;
            }
            break;
        case MODE_INTRO_LANDING:
            gCamEyeGoalX = (gPlayerX + offX - 4000) << 11;
            gCamEyeGoalY = (gPlayerY + 5000) << 11;
            gCamEyeGoalZ = (gPlayerZ + offZ + 4000) << 11;
            break;
        default:
            if (gCamFixedEye) {
                gCamEyeGoalX = gCamFixedEyeX;
                gCamEyeGoalY = gCamFixedEyeY;
                gCamEyeGoalZ = gCamFixedEyeZ;
            } else {
                if (gLevelKind == LEVEL_KIND_PILL_MAZE) {
                    gCamDistance = 9000;
                    gCamHeight = 11000;
                    gCamEyeHeight = 11000;
                }
                if (gHaveShuttle && gCamPathActive) {
                    gCamEyeGoalX = (gShuttlePos + gCamPathX) << 11;
                    gCamEyeGoalY = (gShuttlePosY + gCamPathY) << 11;
                    gCamEyeGoalZ = (gShuttlePosZ + gCamPathZ) << 11;
                } else if (gHaveCarrier && gCamPathActive) {
                    gCamEyeGoalX = (gCarrierX + gCamPathX) << 11;
                    gCamEyeGoalY = (gCarrierY + gCamPathY) << 11;
                    gCamEyeGoalZ = (gCarrierZ + gCamPathZ) << 11;
                } else if (gCamOnEndObject) {
                    cam_polar_offset(2500, gCamHeading, &px, &pz);
                    gCamEyeGoalX = ((gEndObjectX << 5) + px) << 11;
                    gCamEyeGoalY = ((gEndObjectY << 5) + 5000) << 11;
                    gCamEyeGoalZ = ((gEndObjectZ << 5) + pz) << 11;
                } else if (gCamOnCarrierTarget) {
                    cam_polar_offset(2500, gCamHeading, &px, &pz);
                    gCamEyeGoalX = (px + gCarrierTargetX) << 11;
                    gCamEyeGoalY = (gCarrierTargetY + 5000) << 11;
                    gCamEyeGoalZ = (pz + gCarrierTargetZ) << 11;
                } else if (gCamOnMarkedTarget) {
                    if (gMarkedTargetPts != NULL) {
                        cam_polar_offset(2500, gCamHeading, &px, &pz);
                        gCamEyeGoalX = ((gMarkedTargetPts->x3 + gMarkedTargetPts->x0) / 2 << 16) + (px << 11);
                        gCamEyeGoalY = ((gMarkedTargetPts->y3 + gMarkedTargetPts->y0) / 2 << 16) + 0x9C4000;
                        gCamEyeGoalZ = ((gMarkedTargetPts->z3 + gMarkedTargetPts->z0) / 2 << 16) + (pz << 11);
                    }
                } else {
                    if (gCamWindowView) {
                        angle = (f32) (gPlayerMoveHeading * 360 / 4096) - gCamHeading + 180.0f;
                        if (angle < 0) {
                            angle += 360;
                        }
                        if (angle >= 360) {
                            angle %= 360;
                        }
                        if (gCurVehicle == VEH_THUNDERFIST || gCurVehicle == VEH_JBOMB) {
                            cam_polar_offset(gCamDistance, angle, &px, &pz);
                        } else {
                            cam_polar_offset(gCamDistance / 2, angle, &px, &pz);
                        }
                        gCamEyeGoalY = (gCamEyeHeight * 3 / 5 + gPlayerY) << 11;
                        offX = 0;
                        offZ = 0;
                    } else {
                        cam_polar_offset(gCamDistance, gCamHeading, &px, &pz);
                        if (gCamNoHeightZone && gCamHeading >= 134.0 && gCamHeading <= 136.0) {
                            gCamEyeGoalY = (gPlayerY + 0x3908) << 11;
                        } else {
                            gCamEyeGoalY = (gPlayerY + gCamEyeHeight) << 11;
                        }
                    }
                    gCamEyeGoalX = (gPlayerX + px + offX) << 11;
                    gCamEyeGoalZ = (gPlayerZ + pz + offZ) << 11;
                }
            }
            break;
    }
    switch (gGameMode) {
        case MODE_MAP_VIEW:
            if (gHaveShuttle) {
                gCamAtGoalX = gShuttlePos / 32.0f;
                gCamAtGoalY = (gShuttlePosY + 2000) / 32.0f;
                gCamAtGoalZ = gShuttlePosZ / 32.0f;
            } else {
                gCamAtGoalX = gCarrierX / 32.0f;
                gCamAtGoalY = gCarrierY / 32.0f;
                gCamAtGoalZ = (gCarrierZ + gMapViewScrollZ + 11000) / 32.0f;
            }
            break;
        case MODE_CARRIER_END:
        case MODE_REPLAY_CARRIER_END:
            if (gHaveShuttle) {
                gCamAtGoalX = (gShuttlePos + offX) / 32.0f;
                gCamAtGoalY = (gShuttlePosY + 2000) / 32.0f;
                gCamAtGoalZ = (gShuttlePosZ + offZ) / 32.0f;
            } else {
                gCamAtGoalX = (gCarrierX + offX) / 32.0f;
                gCamAtGoalY = gCarrierY / 32.0f;
                gCamAtGoalZ = (gCarrierZ + offZ) / 32.0f;
            }
            break;
        case MODE_INTRO_APPROACH:
        case MODE_INTRO_FLY:
        case MODE_INTRO_LANDING:
            if (gHaveShuttle) {
                gCamAtGoalX = gShuttlePos / 32.0f;
                gCamAtGoalY = (gShuttlePosY + 2000) / 32.0f;
                gCamAtGoalZ = gShuttlePosZ / 32.0f;
            } else {
                gCamAtGoalX = (gChopperX + offX) / 32.0f;
                gCamAtGoalY = gChopperDropY / 32.0f;
                gCamAtGoalZ = (gChopperZ + offZ) / 32.0f;
            }
            break;
        case MODE_REPLAY:
            if (gHaveShuttle) {
                gCamAtGoalX = gShuttlePos / 32.0f;
                gCamAtGoalY = (gShuttlePosY + 2000) / 32.0f;
                gCamAtGoalZ = gShuttlePosZ / 32.0f;
                break;
            }
        default:
            if (gHaveShuttle && gCamPathActive) {
                gCamAtGoalX = gShuttlePos / 32.0f;
                gCamAtGoalY = (gShuttlePosY + 6000) / 32.0f;
                gCamAtGoalZ = gShuttlePosZ / 32.0f;
            } else if (gHaveCarrier && gCamPathActive) {
                gCamAtGoalX = gCarrierX / 32.0f;
                gCamAtGoalY = gCarrierY / 32.0f;
                gCamAtGoalZ = gCarrierZ / 32.0f;
            } else if (gCamOnEndObject) {
                gCamAtGoalX = gEndObjectX;
                gCamAtGoalY = gEndObjectY;
                gCamAtGoalZ = gEndObjectZ;
            } else if (gCamOnCarrierTarget) {
                gCamAtGoalX = gCarrierTargetX / 32.0f;
                gCamAtGoalY = gCarrierTargetY / 32.0f;
                gCamAtGoalZ = gCarrierTargetZ / 32.0f;
            } else if (gCamOnMarkedTarget) {
                if (gMarkedTargetPts != NULL) {
                    gCamAtGoalX = (f32) (gMarkedTargetPts->x3 + gMarkedTargetPts->x0) / 2.0;
                    gCamAtGoalY = (f32) (gMarkedTargetPts->y3 + gMarkedTargetPts->y0) / 2.0;
                    gCamAtGoalZ = (f32) (gMarkedTargetPts->z3 + gMarkedTargetPts->z0) / 2.0;
                }
            } else {
                gCamAtGoalX = (gPlayerX + offX) / 32.0f;
                gCamAtGoalY = (gPlayerY + gCamAtHeight) / 32.0f;
                gCamAtGoalZ = (gPlayerZ + offZ) / 32.0f;
            }
            break;
    }
    if (gCarrierHit && levelno == 0x31) {
        gCamEyeGoalX = (gCarrierX + offX + 16000) << 11;
        gCamEyeGoalY = (gCarrierY + 15000) << 11;
        gCamEyeGoalZ = (gCarrierZ + offZ - 3000) << 11;
    }
    if (gHaveShuttle && gShuttleCamFollow) {
        if (gCamSnap) {
            gCamEasedHeading = gShuttleHeading * 16;
        } else {
            gCamEasedHeading = (s16) ((u16) gShuttleHeading * 16 - gCamEasedHeading) / 10 + (s16) gCamEasedHeading;
        }
        gCamEyeGoalX = (s32) (gShuttlePos - SINE(gCamEasedHeading) * 8000.0f + offX) << 11;
        gCamEyeGoalY = (gShuttlePosY + 0xDAC) << 11;
        gCamEyeGoalZ = (s32) (gShuttlePosZ - COSINE(gCamEasedHeading) * 8000.0f + offZ) << 11;
        gCamAtGoalX = gShuttlePos / 32.0f;
        gCamAtGoalY = (gShuttlePosY + 2000) / 32.0f;
        gCamAtGoalZ = gShuttlePosZ / 32.0f;
    }
    if (gGameMode == MODE_REPLAY && levelno == 0x3B) {
        gCamEyeGoalX = (gPlayerX + 0x1900) << 11;
        gCamEyeGoalY = (gPlayerY + 0x3070) << 11;
        gCamEyeGoalZ = (gPlayerZ - 0x1900) << 11;
        gCamAtGoalX = gPlayerX / 32.0f;
        gCamAtGoalY = gPlayerY / 32.0f;
        gCamAtGoalZ = gPlayerZ / 32.0f;
    }
    if ((gGameMode & (MODE_LEVEL | MODE_MAP_VIEW | MODE_LEVEL_START | MODE_YOSHI_PAGES)) && !gCamPathActive && !inShuttle && gGameMode != MODE_MAP_VIEW && levelno != 0x23 &&
        !gCamWindowView) {
        tx = gCamAtGoalX * 65536.0;
        ty = gCamAtGoalY * 65536.0;
        tz = gCamAtGoalZ * 65536.0;
        if (levelno == 0x10 && gCamAtGoalX > 8718.0f && gCamAtGoalX < 8919.0f && gCamAtGoalZ > 9051.0f &&
            gCamAtGoalZ < 9251.0f) {
            gCamZoom = 1.5f;
            rateY = 0.1f;
        }
        gCamEyeGoalX = (gCamEyeGoalX - tx) * gCamZoom + tx;
        gCamEyeGoalY = (gCamEyeGoalY - ty) * gCamZoom + ty;
        gCamEyeGoalZ = (gCamEyeGoalZ - tz) * gCamZoom + tz;
    }
    if (!gCamSnap) {
        gCamEyeX = (gCamEyeGoalX - gCamEyeX) * rateX + gCamEyeX;
        gCamEyeY = (gCamEyeGoalY - gCamEyeY) * rateY + gCamEyeY;
        gCamEyeZ = (gCamEyeGoalZ - gCamEyeZ) * rateZ + gCamEyeZ;
    } else {
        gCamEyeX = gCamEyeGoalX;
        gCamEyeY = gCamEyeGoalY;
        gCamEyeZ = gCamEyeGoalZ;
        gCamPrevType = !gCamType;
    }
    if (gCamLift && (gCamHeading < 134.0 || gCamHeading > 136.0)) {
        coll_begin_probe(gCamEyeX >> 11, gCamEyeY >> 11, gCamEyeZ >> 11);
        coll_probe_tris(gCamEyeX >> 11, gCamEyeY >> 11, gCamEyeZ >> 11,
                      gCamLiftBlocked ? gCamLiftRadius : gCamLiftRadius + 1600, 0xBE, 0);
        if (gCamLiftBlocked) {
            if (gCollHit) {
                gCamEyeHeight += gCamLiftStep;
                if (gCamEyeHeight > gCamLiftMax) {
                    gCamEyeHeight = gCamLiftMax;
                }
                gCamLiftBlocked = 1;
            } else {
                gCamLiftBlocked = 0;
            }
        } else if (gCollHit) {
            gCamLiftBlocked = 1;
        } else {
            gCamEyeHeight -= gCamLiftStep;
            if (gCamEyeHeight < gCamHeight) {
                gCamEyeHeight = gCamHeight;
            }
            gCamLiftBlocked = 0;
        }
    }
    if (!gCamLift) {
        gCamEyeHeight = gCamHeight;
    } else if (gCamHeading >= 134.0 && gCamHeading <= 136.0) {
        gCamEyeHeight -= gCamLiftStep;
        if (gCamEyeHeight < gCamHeight) {
            gCamEyeHeight = gCamHeight;
        }
    }
    if (gCamType != gCamPrevType || changed) {
        gCamAtEaseX = gCamAtX - gCamAtGoalX;
        gCamAtEaseY = gCamAtY - gCamAtGoalY;
        gCamAtEaseZ = gCamAtZ - gCamAtGoalZ;
        gCamPrevType = gCamType;
        gCamAtSettled = 0;
    }
    if (!gCamAtSettled && !gCamSnap) {
        gCamAtEaseX = gCamAtEaseX * 0.95;
        gCamAtX = gCamAtEaseX + gCamAtGoalX;
        gCamAtEaseY = gCamAtEaseY * 0.97;
        gCamAtY = gCamAtEaseY + gCamAtGoalY;
        gCamAtEaseZ = gCamAtEaseZ * 0.95;
        gCamAtZ = gCamAtEaseZ + gCamAtGoalZ;
        if (gCamAtEaseX < 0.1 && gCamAtEaseX > -0.1 && gCamAtEaseZ < 0.1 && gCamAtEaseZ > -0.1 && gCamAtEaseY < 0.1 &&
            gCamAtEaseY > -0.1) {
            gCamAtSettled = 1;
        }
    } else {
        gCamAtX = gCamAtGoalX;
        gCamAtY = gCamAtGoalY;
        gCamAtZ = gCamAtGoalZ;
        gCamAtSettled = 1;
    }
    if (gShakeSize == 0) {
        gShakeSize = 1;
    }
    if (gShakeTime && (gGameMode & (MODE_ATTRACT | MODE_LEVEL | MODE_CARRIER_END | MODE_GLOBE_SEQ | MODE_YOSHI_PAGES))) {
        shakeX = gFrameDrawTime % gShakeSize - (gShakeSize >> 1);
        shakeY = gFrameDrawTime / 100 % gShakeSize - (gShakeSize >> 1);
        shakeZ = gFrameDrawTime / 10000 % gShakeSize - (gShakeSize >> 1);
    } else {
        shakeX = 0;
        shakeY = 0;
        shakeZ = 0;
    }
    if (gShakeTime) {
        gShakeTime--;
        gShakeSize = gShakeSize - gShakeSize / 6;
        gShakeSize = gShakeSize + 1;
    }
    eyeX = (f32) gCamEyeX / 65536.0;
    eyeY = (f32) gCamEyeY / 65536.0;
    eyeZ = (f32) gCamEyeZ / 65536.0;
    if ((gGameMode & MODE_ATTRACT) && gRecDemoNoLogo && gFramesInMode > gRecDemoLength) {
        eyeX = (eyeX - gCamAtX) * gAttractCamZoom + gCamAtX;
        eyeY = (eyeY - gCamAtY) * gAttractCamZoom + gCamAtY;
        eyeZ = (eyeZ - gCamAtZ) * gAttractCamZoom + gCamAtZ;
        if (gAttractCamZoom > 0.4) {
            gAttractCamZoom = gAttractCamZoom - 0.01;
        }
    }
    atX = (gCamAtX * 32.0f + shakeX) / 32.0f;
    atZ = (gCamAtZ * 32.0f + shakeZ) / 32.0f;
    adx = eyeX - atX;
    if (adx < 0.0) {
        adx = 0.0 - adx;
    }
    adz = eyeZ - atZ;
    if (adz < 0.0) {
        adz = 0.0 - adz;
    }
    if (adx > 0.5 || adz > 0.5) {
        guLookAtReflect(mtx, lookAt, eyeX, eyeY, eyeZ, (gCamAtX * 32.0f + shakeX) / 32.0f,
                        (gCamAtY * 32.0f + shakeY) / 32.0f, (gCamAtZ * 32.0f + shakeZ) / 32.0f, 0.0f, 1.0f, 0.0f);
    } else {
        guLookAtReflect(mtx, lookAt, eyeX + 2.0, eyeY, eyeZ + 2.0, (gCamAtX * 32.0f + shakeX) / 32.0f,
                        (gCamAtY * 32.0f + shakeY) / 32.0f, (gCamAtZ * 32.0f + shakeZ) / 32.0f, 0.0f, 1.0f, 0.0f);
    }
    dist = sqrtf((eyeX - gCamAtX) * (eyeX - gCamAtX) + (eyeZ - gCamAtZ) * (eyeZ - gCamAtZ));
    if (dist < 1.0) {
        dist = 1.0f;
    }
    if (eyeX >= gCamAtX && eyeZ >= gCamAtZ) {
        gCamYaw = math_asin_c((eyeX - gCamAtX) * 65535.9 / dist) >> 4;
        quadrant = 0;
    }
    if (eyeX >= gCamAtX && eyeZ < gCamAtZ) {
        gCamYaw = (math_asin_c((gCamAtZ - eyeZ) * 65535.9 / dist) >> 4) + 0x400; quadrant = 1;
    }
    if (eyeX < gCamAtX && eyeZ < gCamAtZ) {
        gCamYaw = (math_asin_c((gCamAtX - eyeX) * 65535.9 / dist) >> 4) + 0x800; quadrant = 2;
    }
    if (eyeX < gCamAtX && eyeZ >= gCamAtZ) {
        gCamYaw = (math_asin_c((eyeZ - gCamAtZ) * 65535.9 / dist) >> 4) + 0xC00; quadrant = 3;
    }
    dist = sqrtf((eyeX - gCamAtX) * (eyeX - gCamAtX) + (eyeZ - gCamAtZ) * (eyeZ - gCamAtZ) +
                 (eyeY - gCamAtY) * (eyeY - gCamAtY));
    if (eyeY >= gCamAtY) {
        pitch1 = math_asin_c((eyeY - gCamAtY) * 65535.9 / dist) >> 3;
    } else {
        pitch1 = 0x1000 - (math_asin_c((gCamAtY - eyeY) * 65535.9 / dist) >> 3);
    }
    if (eyeY >= gCamAtY) {
        pitch2 = math_asin_c((65535.0 < (eyeY - gCamAtY) * 65535.9 / 20000.0) ? 65535.0 : (eyeY - gCamAtY) * 65535.9 / 20000.0) >> 3;
    } else {
        pitch2 = 0x1000 - (math_asin_c((65535.0 < (gCamAtY - eyeY) * 65535.9 / 20000.0) ? 65535.0 : (gCamAtY - eyeY) * 65535.9 / 20000.0) >> 3);
    }
    gCamPitch = pitch1 - pitch2;
    scale = cam_eye_scale(eyeX, eyeY, eyeZ, gCamAtX, gCamAtY, gCamAtZ);
    posX = (eyeX - gCamAtX) * scale + gCamAtX;
    posY = (eyeY - gCamAtY) * scale + gCamAtY;
    posZ = (eyeZ - gCamAtZ) * scale + gCamAtZ;
    if (adx > 0.5 || adz > 0.5) {
        guLookAt(view, posX, posY, posZ, gCamAtX, gCamAtY, gCamAtZ, 0.0f, 1.0f, 0.0f);
    } else {
        guLookAt(view, posX + 2.0, posY, posZ + 2.0, gCamAtX, gCamAtY, gCamAtZ, 0.0f, 1.0f, 0.0f);
    }
    PORT_ICUT_IF(gCamSnap);
    gCamSnap = 0;
}

/* The culling view's eye factor for the current vehicle type: the eye is
 * moved to target + (eye - target) * factor, the factor reduced so that the
 * distance stays within 10000 (0x80254E54). */
f32 cam_eye_scale(f32 x0, f32 y0, f32 z0, f32 x1, f32 y1, f32 z1) {
#ifdef NON_MATCHING
    f32 speed = 0.0f; /* ids without a case leave the original's stack slot as it was (indeterminate) */
#else
    f32 speed;
#endif
    f32 dx;
    f32 dy;
    f32 dz;
    f32 dist;

    switch (gCurVehicle) {
        case VEH_DRIVER:
            speed = 3.5f;
            break;
        case VEH_SIDESWIPE:
            speed = 1.4f;
            break;
        case VEH_THUNDERFIST:
            speed = 1.6f;
            break;
        case VEH_SKYFALL:
            speed = 4.8f;
            break;
        case VEH_RAMDOZER:
            speed = 5.0f;
            break;
        case VEH_BACKLASH:
            speed = 5.0f;
            break;
        case VEH_CRANE:
            speed = 1.6f;
            break;
        case VEH_TRAIN:
            speed = 1.6f;
            break;
        case VEH_AMDREAM:
            speed = 4.8f;
            break;
        case VEH_JBOMB:
            speed = 1.6f;
            break;
        case VEH_BALLISTA:
            speed = 5.0f;
            break;
        case VEH_BARGE_0B:
        case VEH_BARGE_11:
        case VEH_BARGE_12:
            speed = 1.6f;
            break;
        case VEH_POLICE:
            speed = 4.8f;
            break;
        case VEH_ATEAM:
            speed = 4.8f;
            break;
        case VEH_STARSKI:
            speed = 4.8f;
            break;
        case VEH_CYCLONE:
            speed = 1.6f;
            break;
    }
    dx = x0 - x1;
    dx = dx * dx;
    dy = y0 - y1;
    dy = dy * dy;
    dz = z0 - z1;
    dz = dz * dz;
    dist = sqrtf(dx + dy + dz);
    if (dist * speed > 10000.0f) {
        speed = speed - (dist * speed - 10000.0f) / dist;
    }
    return speed;
}

/* Polar to cartesian: point at angle (degrees) on a circle of radius r*sqrt(2)
 * (0x80255034). */
void cam_polar_offset(s32 r, f32 angle, s32 *outX, s32 *outY) {
    f32 len;
    f32 r2;
    s32 x;
    s32 y;
    f32 a;

    r2 = r * r;
    len = sqrtf(r2 + r2);
    a = angle;
    a = a / 360.0;
    a = a * 6.28318;
    x = sinf(a) * len;
    y = sqrtf(len * len - x * x);
    if (angle >= 90.0 && angle < 270.0) {
        y = 0.0 - y;
    }
    if (y >= -100 && y <= 100) {
        y = 0;
    }
    *outX = x;
    *outY = y;
}

/* C-right / C-left turn gCamHeading 45 degrees, 3 degrees a frame (with a
 * sound); not where cam_fixed_heading_area holds it (0x80255190). */
void cam_turn_heading(void) {
    u8 ok;

    if (gPaused == 0) {
        ok = cam_fixed_heading_area();
        if (ok && ((gHeldCRight && !gPrevHeldCRight) || (gHeldCLeft && !gPrevHeldCLeft)) && (gGameMode & (MODE_LEVEL | MODE_MAP_VIEW))) {
            snd_play(gSfxBank, SFX_DENIED, NULL);
        }
        if (gHeldCRight && !gPrevHeldCRight && !gCamTurnPos && !gCamTurnNeg && !ok && gGameMode != MODE_LEVEL_START) {
            gCamHeadingGoal = gCamHeading + 45.0;
            if (gCamHeadingGoal >= 360.0) {
                gCamHeadingGoal = gCamHeadingGoal - 360.0;
            }
            gCamTurnPos = 1;
            if (gGameMode != MODE_REPLAY) {
                snd_play(gSfxBank, SFX_CAM_IN, NULL);
            }
        }
        if (gCamTurnPos) {
            if (gCamHeading < gCamHeadingGoal) {
                gCamHeading = gCamHeading + 3.0;
                if (gCamHeading >= gCamHeadingGoal) {
                    gCamTurnPos = 0;
                    gCamHeading = gCamHeadingGoal;
                }
            } else {
                gCamHeading = gCamHeading + 3.0;
                if (gCamHeading >= 360.0) {
                    gCamHeading = gCamHeading - 360.0;
                    if (gCamHeading >= gCamHeadingGoal) {
                        gCamTurnPos = 0;
                        gCamHeading = gCamHeadingGoal;
                    }
                }
            }
        }
        if (gHeldCLeft && !gPrevHeldCLeft && !gCamTurnPos && !gCamTurnNeg && !ok && gGameMode != MODE_LEVEL_START) {
            gCamHeadingGoal = gCamHeading - 45.0;
            if (gCamHeadingGoal < 0.0) {
                gCamHeadingGoal = gCamHeadingGoal + 360.0;
            }
            gCamTurnNeg = 1;
            if (gGameMode != MODE_REPLAY) {
                snd_play(gSfxBank, SFX_CAM_OUT, NULL);
            }
        }
        if (gCamTurnNeg) {
            if (gCamHeading > gCamHeadingGoal) {
                gCamHeading = gCamHeading - 3.0;
                if (gCamHeading <= gCamHeadingGoal) {
                    gCamTurnNeg = 0;
                    gCamHeading = gCamHeadingGoal;
                }
            } else {
                gCamHeading = gCamHeading - 3.0;
                if (gCamHeading < 0.0) {
                    gCamHeading = gCamHeading + 360.0;
                    if (gCamHeading <= gCamHeadingGoal) {
                        gCamTurnNeg = 0;
                        gCamHeading = gCamHeadingGoal;
                    }
                }
            }
        }
        if (gCamTurnNeg || gCamTurnPos) {
            cull_restart(2);
        }
    }
}

/* 1 where the heading is held at 135 degrees (and turned back to it): the
 * attract mode, the sequences and some areas of levels 4, 13, 16 and 59
 * (0x80255628). */
u8 cam_fixed_heading_area(void) {
    u8 ok = 0;

    if (gGameMode == MODE_GLOBE_SEQ || gGameMode == MODE_ATTRACT) {
        ok = 1;
    } else {
        switch (levelno) {
            case 4:
                if (ground_in_triangle(gPlayerX >> 5, gPlayerZ >> 5, 0xE56, 0x8EC, 0xBB8, 0x6A4, 0x1068, 0x1F4) ||
                    ground_in_triangle(gPlayerX >> 5, gPlayerZ >> 5, 0xE56, 0x8EC, 0x1068, 0x1F4, 0x1324, 0x4B0)) {
                    ok = 1;
                }
                break;
            case 16:
                if (gPlayerX > 0x46500 && gPlayerZ > 0x3E800) {
                    ok = 1;
                }
                break;
            case 13:
                if (gPlayerX > 0x42680 && gPlayerZ > 0x46500) {
                    ok = 1;
                }
                break;
            case 0x3B:
                ok = 1;
                break;
        }
    }
    if (ok) {
        if (gCamHeadingGoal < 134.0 || gCamHeadingGoal > 136.0) {
            gCamTurnNeg = 0;
            gCamTurnPos = 0;
            gCamHeadingGoal = 135.0f;
            if (gCamHeadingGoal < gCamHeading) {
                if (gCamHeading - gCamHeadingGoal > 180.0) {
                    gCamTurnPos = 1;
                } else {
                    gCamTurnNeg = 1;
                }
            } else {
                gCamTurnPos = 1;
            }
        }
    }
    return ok;
}

/* Clears the current frame buffer with a fill rectangle (0x802558C8). */
void hd_clear_framebuffer(Gfx *arg0, s32 *len) {
    Gfx *gdl = arg0;

    gDPPipeSync(gdl++);
    gDPSetCycleType(gdl++, G_CYC_FILL);
    gDPSetColorImage(gdl++, G_IM_FMT_RGBA, G_IM_SIZ_16b, 320, gFrameBufferPhys[gBufferIndex]);
    gDPSetFillColor(gdl++, 0x00010001);
    gDPPipeSync(gdl++);
    gDPFillRectangle(gdl++, 0, 0, 319, 239);
    *len += gdl - arg0;
}

/* Closes the top-level display list and returns its length in *length
 * (0x802559F8). */
void hd_close_display_list(Gfx *arg0, s32 *length) {
    Gfx *gdl = arg0;

    gDPFullSync(gdl++);
    gSPEndDisplayList(gdl++);
    *length = gdl - gDynamicBufs[gBufferIndex].dl;
    HD_ASSERT(*length<TOPLEVEL_DL_SIZE, 3665);
}

/* Once, from the main thread: the DMA and texture-DMA queues, the scheduler
 * and the main thread's client, the controllers, audio, VI features, the frame
 * buffer addresses, the first game mode (from the controller pattern and the
 * boot flags) and the stack guard pattern (0x80255AD0). */
void hd_init(void) {
    s32 i;
    f32 one;
    s32 pad[4];
    u8 status;

    one = 1.0f;
    gGameMode = MODE_BOOT_SCREEN2;
    osCreateMesgQueue(&gDmaMesgQueue, gDmaMesgBuf, 50);
    osCreateMesgQueue(&textureDmaMessageQ, gTextureDmaMesgBuf, 0x90);
    sched_create(&gScheduler, gSchedStack + 0x400, 13, (osTvType != 1) ? 0x10 : 2, 1);
    osCreateMesgQueue(&gTaskDoneQueue, gTaskDoneMesgBuf, 16);
    sched_add_client(&gScheduler, &gMainSchedClient, &gTaskDoneQueue, 1, 1);
    status = cont_init();
    music_init_audio();
    debug_printf("audio inited\n");
    osViSetSpecialFeatures(OS_VI_GAMMA_OFF);
    osViSetSpecialFeatures(OS_VI_DITHER_FILTER_ON);
    gFrameBufferPhys[0] = PHYS(gFrameBuffers[0]);
    gFrameBufferPhys[1] = PHYS(gFrameBuffers[1]);
    gDepthBufferPhys = PHYS(gDepthBuffer);
    rsp_init_ucode_table();
    osWritebackDCacheAll();
    cont_boot_check_start();
    if (!(status & 1)) {
        gNextGameMode = MODE_NO_CONTROLLER;
    } else if (gBootPakManager) {
        gNextGameMode = MODE_PAK_MANAGER;
    } else if (gBootEraseSave) {
        gNextGameMode = MODE_ERASE_SAVE;
    } else {
        gNextGameMode = MODE_BOOT_SCREEN1;
    }
    for (i = 0x1FF; i >= 0; i--) {
        gMainThreadStack[i] = 0x1122334455667788LL;
    }
}

/* After every frame: checks the main thread's stack guard pattern from the
 * top down and prints where it ends (0x80255D34). */
void hd_check_stack(void) {
    u8 bad;
    s32 i;

    bad = 0;
    for (i = 0x1FF; i >= 0 && !bad; i--) {
        if (gMainThreadStack[i] != 0x1122334455667788LL) {
            bad = 1;
            debug_printf("stack end =%d\n", i);
        }
    }
}

/* Called by most mode switches before the new mode starts: screen off,
 * frame counters zeroed, the static segment loaded, the heap reset (RDP
 * output buffer, ghost buffer, vehicle shadow records, Yoshi windows) and the
 * per-mode subsystems reset (0x80255DC8). */
void hd_mode_setup(void) {
    u32 end;
    s32 pad;
    u32 size;
    s32 old;

    size = romStaticSegmentEnd - romStaticSegment;
    osViBlack(1);
    gPendingTune = music_tune_for_mode(gNextGameMode);
    osWritebackDCacheAll();
    osInvalDCache((void *) 0x80000000, 0x400000);
    gHudMoney = 0;
    gUnpausedFrames = 0;
    gPlayFrames = 0;
    gFramesInMode = 0;
    gBufferIndex = 0;
    gHudAlpha = 0;
    util_seed_rand();
    util_seed_rand2();
    cont_clear();
    cont_steer_reset();
    gStaticSegment = gStaticSegmentBuf;
    hd_rom_load((u32) romStaticSegment, (u32) gStaticSegmentBuf, &size, 10, 0, 2);
    end = (u32) (romStaticSegmentEnd - romStaticSegment) + (u32) gStaticSegmentBuf;
    debug_printf("Static end = 0x%x, space=0x%x (%d) bytes\n", end, 0x80400000 - end, 0x80400000 - end);
    gFrameDlLen = 0;
    hd_clear_framebuffer(gDynamicBufs[gBufferIndex].dl, &gFrameDlLen);
    hd_close_display_list(gDynamicBufs[gBufferIndex].dl, &gFrameDlLen);
    gHeapPtr = 0x8004B400;
    align_up(&gHeapPtr, 0x10);
    gRdpOutputBuffer = gHeapPtr;
    gHeapPtr += 0xA000;
    if (gLevelInfo[levelno].kind == LEVEL_KIND_RACE && !(gNextGameMode & (MODE_ATTRACT | MODE_GLOBE_SEQ))) {
        debug_printf("Allocating ghost buffer memory\n");
        gHeapPtr += 0x20000;
    }
    gTexTableLoaded = 0;
    tex_load_table();
    gVehicleShadows = (VehShadow *) (((u32) gVehicleShadowPool + 0x40) & ~0x3F);
    mb_init();
    cmo_hit_request = 0;
    gLevelDoneRequest = 0;
    gCarrierHitSeen = 0;
    gCarrierHit = 0;
    gLevelDone = 0;
    gPauseRequest = 0;
    gUnpauseRequest = 0;
    gPaused = 0;
    gBonusLevelExit = 0;
    gRecVehiclePending = 0;
    if (gNextGameMode & (MODE_BOOT_SCREEN1 | MODE_BOOT_SCREEN2 | MODE_LEVEL_STATUS | MODE_WORLD | MODE_PAK_CHECKED | MODE_LOAD_GAME | MODE_NEW_PLAYER | MODE_NAME_ENTRY | MODE_ERASE_CONFIRM | MODE_ERASE_PLAYER_CHOSEN | MODE_LOAD_GAME_RETURN | MODE_ERASE_MENU | MODE_TO_ERASE_MENU | MODE_NAME_ENTERED | MODE_PLAYER_CHOSEN | MODE_RESULTS | MODE_PAK_CHECK | MODE_VEHICLE_SELECT | MODE_PROMOTION | MODE_PRE_PROMOTION | MODE_PAK_FILES | MODE_UNUSED_33 | MODE_UNUSED_WINDOW_13 | MODE_PAK_MANAGER_CHECKED | MODE_PAK_FILE_CHOSEN | MODE_PAK_DELETE_CONFIRM | MODE_PAK_FILE_DELETED | MODE_UNUSED_41 | MODE_BEST_TIMES | MODE_PAK_MANAGER | MODE_ACADEMY_MESSAGE | MODE_ATTRACT_PICTURE | MODE_REGRADE_NOTICE | MODE_REGRADE_NOTICE_END | MODE_UNUSED_52 | MODE_BOOT_CHECK | MODE_ERASE_SAVE | MODE_BEST_TIMES_UNUSED | MODE_GUEST_PLAYER | MODE_NO_CONTROLLER | MODE_QUIT_GAME | MODE_LOAD_GAME_EXIT)) {
        hd_load_front_end();
    }
    academy_place_marker(levelno);
    spr_reset();
    if (gNextGameMode == MODE_BEST_TIMES) {
        fe_best_build_menu();
    }
    old = gHeapPtr;
    yoshi_init(0);
    debug_printf("Yoshi windows allocated %d bytes, %x\n", gHeapPtr - old, gHeapPtr);
    gVehiclesEnd = gVehicles;
    cont_wait_read();
    text_init();
    gPadDisplayLoaded = 0;
}

/* Inflates level's data file (lagp, chimp, valley, ..., level59: the ROM symbols
 * below; docs/systems/levels.md) to dest; *size gets its size (0x8025615C). */
void lvl_load_file(s32 level, s32 dest, s32 *size) {
    u8 *start;

    switch (level) {
        case 1:
            start = romLagpRaw;
            *size = romChimpRaw - romLagpRaw;
            break;
        case 0:
            start = romChimpRaw;
            *size = romValleyRaw - romChimpRaw;
            break;
        case 2:
            start = romValleyRaw;
            *size = romFactRaw - romValleyRaw;
            break;
        case 3:
            start = romFactRaw;
            *size = romDipRaw - romFactRaw;
            break;
        case 4:
            start = romDipRaw;
            *size = romBeetleRaw - romDipRaw;
            break;
        case 5:
            start = romBeetleRaw;
            *size = romBonus1Raw - romBeetleRaw;
            break;
        case 6:
            start = romBonus1Raw;
            *size = romBonus2Raw - romBonus1Raw;
            break;
        case 7:
            start = romBonus2Raw;
            *size = romBonus3Raw - romBonus2Raw;
            break;
        case 8:
            start = romBonus3Raw;
            *size = romLevel9Raw - romBonus3Raw;
            break;
        case 9:
            start = romLevel9Raw;
            *size = romLevel10Raw - romLevel9Raw;
            break;
        case 10:
            start = romLevel10Raw;
            *size = romLevel11Raw - romLevel10Raw;
            break;
        case 11:
            start = romLevel11Raw;
            *size = romLevel12Raw - romLevel11Raw;
            break;
        case 12:
            start = romLevel12Raw;
            *size = romLevel13Raw - romLevel12Raw;
            break;
        case 13:
            start = romLevel13Raw;
            *size = romLevel14Raw - romLevel13Raw;
            break;
        case 14:
            start = romLevel14Raw;
            *size = romLevel15Raw - romLevel14Raw;
            break;
        case 15:
            start = romLevel15Raw;
            *size = romLevel16Raw - romLevel15Raw;
            break;
        case 16:
            start = romLevel16Raw;
            *size = romLevel17Raw - romLevel16Raw;
            break;
        case 17:
            start = romLevel17Raw;
            *size = romLevel18Raw - romLevel17Raw;
            break;
        case 18:
            start = romLevel18Raw;
            *size = romLevel19Raw - romLevel18Raw;
            break;
        case 19:
            start = romLevel19Raw;
            *size = romLevel20Raw - romLevel19Raw;
            break;
        case 20:
            start = romLevel20Raw;
            *size = romLevel21Raw - romLevel20Raw;
            break;
        case 21:
            start = romLevel21Raw;
            *size = romLevel22Raw - romLevel21Raw;
            break;
        case 22:
            start = romLevel22Raw;
            *size = romLevel23Raw - romLevel22Raw;
            break;
        case 23:
            start = romLevel23Raw;
            *size = romLevel24Raw - romLevel23Raw;
            break;
        case 24:
            start = romLevel24Raw;
            *size = romLevel25Raw - romLevel24Raw;
            break;
        case 25:
            start = romLevel25Raw;
            *size = romLevel26Raw - romLevel25Raw;
            break;
        case 26:
            start = romLevel26Raw;
            *size = romLevel27Raw - romLevel26Raw;
            break;
        case 27:
            start = romLevel27Raw;
            *size = romLevel28Raw - romLevel27Raw;
            break;
        case 28:
            start = romLevel28Raw;
            *size = romLevel29Raw - romLevel28Raw;
            break;
        case 29:
            start = romLevel29Raw;
            *size = romLevel30Raw - romLevel29Raw;
            break;
        case 30:
            start = romLevel30Raw;
            *size = romLevel31Raw - romLevel30Raw;
            break;
        case 31:
            start = romLevel31Raw;
            *size = romLevel32Raw - romLevel31Raw;
            break;
        case 32:
            start = romLevel32Raw;
            *size = romLevel33Raw - romLevel32Raw;
            break;
        case 33:
            start = romLevel33Raw;
            *size = romLevel34Raw - romLevel33Raw;
            break;
        case 34:
            start = romLevel34Raw;
            *size = romLevel35Raw - romLevel34Raw;
            break;
        case 35:
            start = romLevel35Raw;
            *size = romLevel36Raw - romLevel35Raw;
            break;
        case 36:
            start = romLevel36Raw;
            *size = romLevel37Raw - romLevel36Raw;
            break;
        case 37:
            start = romLevel37Raw;
            *size = romLevel38Raw - romLevel37Raw;
            break;
        case 38:
            start = romLevel38Raw;
            *size = romLevel39Raw - romLevel38Raw;
            break;
        case 39:
            start = romLevel39Raw;
            *size = romLevel40Raw - romLevel39Raw;
            break;
        case 40:
            start = romLevel40Raw;
            *size = romLevel41Raw - romLevel40Raw;
            break;
        case 41:
            start = romLevel41Raw;
            *size = romLevel42Raw - romLevel41Raw;
            break;
        case 42:
            start = romLevel42Raw;
            *size = romLevel43Raw - romLevel42Raw;
            break;
        case 43:
            start = romLevel43Raw;
            *size = romLevel44Raw - romLevel43Raw;
            break;
        case 44:
            start = romLevel44Raw;
            *size = romLevel45Raw - romLevel44Raw;
            break;
        case 45:
            start = romLevel45Raw;
            *size = romLevel46Raw - romLevel45Raw;
            break;
        case 46:
            start = romLevel46Raw;
            *size = romLevel47Raw - romLevel46Raw;
            break;
        case 47:
            start = romLevel47Raw;
            *size = romLevel48Raw - romLevel47Raw;
            break;
        case 48:
            start = romLevel48Raw;
            *size = romLevel49Raw - romLevel48Raw;
            break;
        case 49:
            start = romLevel49Raw;
            *size = romLevel50Raw - romLevel49Raw;
            break;
        case 50:
            start = romLevel50Raw;
            *size = romLevel51Raw - romLevel50Raw;
            break;
        case 51:
            start = romLevel51Raw;
            *size = romLevel52Raw - romLevel51Raw;
            break;
        case 52:
            start = romLevel52Raw;
            *size = romLevel53Raw - romLevel52Raw;
            break;
        case 53:
            start = romLevel53Raw;
            *size = romLevel54Raw - romLevel53Raw;
            break;
        case 54:
            start = romLevel54Raw;
            *size = romLevel55Raw - romLevel54Raw;
            break;
        case 55:
            start = romLevel55Raw;
            *size = romLevel56Raw - romLevel55Raw;
            break;
        case 56:
            start = romLevel56Raw;
            *size = romLevel57Raw - romLevel56Raw;
            break;
        case 57:
            start = romLevel57Raw;
            *size = romLevel58Raw - romLevel57Raw;
            break;
        case 58:
            start = romLevel58Raw;
            *size = romLevel59Raw - romLevel58Raw;
            break;
        case 59:
            start = romLevel59Raw;
            *size = romWorldTexturesRaw - romLevel59Raw;
            break;
    }
    PORT_LEVEL_PRE(level, dest); /* an edited level file (mods, bc.exe's editor: game/port.h) */
    hd_rom_load((u32) start, (u32) ((void *) N64_IPTR(dest)), (u32 *) size, 12, 10, 1);
    PORT_LEVEL_POST(level, start, dest, size);
}

/* hd.c's level setup (arg0: a saved status stream or 0): the level's state,
 * its file on the heap (lvl_load_file), initlevel, the display list buffers,
 * the vehicles' shadows, HUD and camera start values (0x80256A34). */
void hd_setup_level(s32 arg0) {
    s32 i;
    s32 size;
    VehShadow *v;
    u8 found;
    s32 old;

    gHudDialLoaded = 0;
    gHaveSkyfall = 0;
    gHudMissileIconLoaded = 0;
    gHaveBallista = 0;
    gHudRamIconLoaded = 0;
    gHaveSideswipe = 0;
    gDriverHidden = 0;
    gVehEntered = 0;
    gVehicleShadowsEnd = gVehicleShadows;
    gLevelKind = gLevelInfo[levelno].kind;
    gShakeTime = 0;
    gShakeSize = 0;
    gPlayerX = 0;
    gPlayerY = 0;
    gPlayerZ = 0;
    gPlayerSpeed = 0;
    gCamHeading = 135.0f;
    gCamHeadingGoal = 135.0f;
    gCamTurnPos = 0;
    gCamTurnNeg = 0;
    gCamEyeHeight = 3000;
    gCamLiftBlocked = 1;
    gTrainContactFrames = 0;
    gVehJustSwitched = 0;
    gPlayerHidden = 0;
    gCurVehicle = VEH_DRIVER;
    if (frontEndPresent) {
        HD_ASSERT(!pakBusy, 4162);
        HD_ASSERT(MQ_IS_EMPTY(&pakToGameMessageQ), 4163);
        sched_remove_client(&gScheduler, &gPakSchedClient);
        osDestroyThread((OSThread *) gPakThread);
    }
    gGhostRecording = 0;
    gGhostPlaying = 0;
    if (gLevelKind == LEVEL_KIND_RACE && gNextGameMode == MODE_LEVEL_START) {
        if (!gGhostRetry) {
            ghost_init();
        }
        ghost_start_recording();
        ghost_start_playback();
    }
    lvl_load_file(levelno, gHeapPtr, &size);
    gLevelData = (u8 *) N64_IPTR(gHeapPtr);
    gHeapPtr += size;
    align_up(&gHeapPtr, 0x10);
    stats_level_start();
    hud_init_target_marker();
    lvl_init_chopper_pos(levelno);
    lvl_init_train_line(levelno);
    cam_set_level_fovy(levelno);
    gTurboPending = 0;
    radar_clear_markers();
    debug_printf("enter initlevel game_mode=%d loop_done=%d\n", lowest_set_bit(gGameMode), lowest_set_bit(gNextGameMode));
    old = gHeapPtr;
    initlevel(gLevelData, arg0);
    debug_printf("exit initlevel allocated %d bytes, %x\n", gHeapPtr - old, gHeapPtr);
    lvl_alloc_dl_buffers();
    if (gNextGameMode != MODE_ATTRACT) {
        if ((players[playerNumber].medal[levelno] > 0 && players[playerNumber].medal[levelno] < 6) ? 1 : 0) {
            commpt_load_flags();
        }
    }
    obj_tally(gLevelKind == LEVEL_KIND_BUILDINGS || gLevelKind == LEVEL_KIND_MISSION_TIMED);
    hud_init_objective(levelno);
    if (gCamFixedEye) {
        gCamFixedIconSprite = spr_load((u16 *) gCamFixedIconSprIds, 0, 1, 1, 1, 1.0f);
    }
    hud_panel_init();
    if (gHaveSkyfall) {
        hud_init_boost_dial();
    }
    if (gHaveBallista) {
        hud_init_missile_icon();
    }
    if (gHaveSideswipe) {
        hud_init_ram_icon();
    }
    hud_init_veh_overlay();
    fx_init_carrier_blast();
    fx_init_power_pill(levelno);
    if (gLevelInfo[levelno].kind == LEVEL_KIND_MISSION) {
        people_init(arg0);
    }
    ptcl_init();
    trail_init();
    shell_init();
    water_init(levelno);
    fx_init_drift_quads(levelno);
    util_cam_path_init(levelno);
    util_reset_hint_events();
    v = gVehicleShadows;
    while (v != gVehicleShadowsEnd) {
        i = 0;
        found = 0;
        while (!found) {
            if (gVehicles[i].type == v->type) {
                found = 1;
            } else {
                i++;
            }
        }
        veh_render_shadow_tex(v, v->x, v->bodyY, v->z, v->renderDist, (Gfx *) N64_IPTR((gVehicles[i].shadowDl)), gVehicles[i].seg6,
                      gVehicles[i].seg7Odd);
        rsp_wait_task(0x61F);
        v++;
    }
    backdrop_init(gNextGameMode, levelno);
    gCamYaw = 0x2000;
    gCamPitch = 0x2000;
    gCurVehicle = VEH_DRIVER;
    gNextVehicle = 0;
    gCamEyeX = 0;
    gCamEyeY = 0;
    gCamEyeZ = 0;
    gCamTilt = 0.0f;
    gCamTurn = 0.0f;
    gAttractCamZoom = 1.0f;
    gValuePopupHead = 0;
    gValuePopupTail = 0;
    gValuePopupNew = 0;
    gValuePopupValue = 0;
    gCarrierDanger = 0;
    gFailSpinAngle = 0.0f;
    gFailSpinRate = 1.0f;
    gFailSpinScale = 1.0f;
    gFailSpinDone = 0;
    gPlayerX = gDriverPos;
    gPlayerY = gDriverY;
    gPlayerZ = gDriverZ;
    D_802FDB14 = 0;
    if (gUseEeprom) {
        gLevelMoney = nss.ip;
    } else {
        gLevelMoney = players[playerNumber].money;
    }
    gTimeInLevel = 0;
    gLevelStartTime = 0;
    if (gRecVehiclePending) {
        rec_restore_start_vehicle();
    }
    debug_printf("Level %d: mem_pool=0x%x, code seg=0x%x, space=%d bytes\n", levelno, gHeapPtr, 0x802447C0,
                  0x8021ED00 - gHeapPtr);
    if (gAcademyMarkerOn) {
        academy_build_window(levelno);
    }
    cam_load_level_defaults();
    fx_alloc_texture_caches();
}

/* Allocates the per-level display list buffers (two of each) from the heap
 * (0x80257234). */
void lvl_alloc_dl_buffers(void) {
#ifdef NON_MATCHING
    s32 a = 0; /* levels without a case leave it indeterminate in the original */
#else
    s32 a;
#endif
#ifdef NON_MATCHING
    s32 b = 0; /* levels without a case leave it indeterminate in the original */
#else
    s32 b;
#endif
#ifdef NON_MATCHING
    s32 c = 0; /* levels without a case leave it indeterminate in the original */
#else
    s32 c;
#endif
#ifdef NON_MATCHING
    s32 d = 0; /* levels without a case leave it indeterminate in the original */
#else
    s32 d;
#endif

    switch (levelno) {
        case 16:
        case 29:
            a = 50;
            b = 50;
            c = 50;
            d = 200;
            break;
        case 17:
            a = 50;
            b = 50;
            c = 50;
            d = 50;
            break;
        case 11:
            a = 3000;
            b = 50;
            c = 50;
            d = 200;
            break;
        case 10:
            a = 50;
            b = 50;
            c = 50;
            d = 2000;
            break;
        default:
            switch (gLevelDlProfile) {
                case 0:
                    a = 6000;
                    b = 5000;
                    c = 100;
                    d = 2000;
                    break;
                case 1:
                    a = 1000;
                    b = 1000;
                    c = 100;
                    d = 2000;
                    break;
            }
            break;
    }
    gLevelDl0[0] = gHeapPtr;
    gHeapPtr += a * 8;
    gLevelDl1[0] = gHeapPtr;
    gHeapPtr += b * 8;
    gLevelDl2[0] = gHeapPtr;
    gHeapPtr += c * 8;
    gLevelDl3[0] = gHeapPtr;
    gHeapPtr += d * 8;
    gLevelDl0[1] = gHeapPtr;
    gHeapPtr += a * 8;
    gLevelDl1[1] = gHeapPtr;
    gHeapPtr += b * 8;
    gLevelDl2[1] = gHeapPtr;
    gHeapPtr += c * 8;
    gLevelDl3[1] = gHeapPtr;
    gHeapPtr += d * 8;
}

/* Rounds *arg0 up to a multiple of arg1 (the heap pointer) (0x80257490). */
void align_up(s32 *arg0, s32 arg1) {
    s32 r;

    r = *arg0 % arg1;
    if (r != 0) {
        r = arg1 - r;
    }
    *arg0 += r;
}


/* sinf (0x802574F0); the camera's SINE() */
f32 hd_sinf(f32 arg0) {
    return sinf(arg0);
}


/* cosf (0x80257514); the camera's COSINE() */
f32 hd_cosf(f32 arg0) {
    return cosf(arg0);
}
