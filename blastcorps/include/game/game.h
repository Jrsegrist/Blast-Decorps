#ifndef GAME_GAME_H
#define GAME_GAME_H

/*
 * The game's shared declarations, one copy each: include this after
 * <ultra64.h> instead of declaring other files' functions and variables
 * locally.
 *
 *   types.h      structures used by more than one file
 *   regs.h       register structs of the NON_MATCHING asm rewrites
 *   functions.h  every function used outside its file
 *   variables.h  every variable used by more than one file
 */
#include "game/types.h"
#include "game/object.h"
#include "game/regs.h"
#include "game/functions.h"
#include "game/variables.h"
#include "game/port.h"

/*
 * LEAKED(reg): an argument the original passes in a register its (matched,
 * IDO-compiled) caller never sets for the call: whatever an earlier call or
 * statement left in `reg`. The NON_MATCHING rewrites take these registers
 * as parameters; call sites that pass LEAKED(...) still need the value
 * traced from the original (port_followups.md, agent "trace"). Until then
 * they pass 0.
 */
#define LEAKED(reg) 0

/*
 * Game modes: one bit each of gGameMode / gNextGameMode (u64; hd.c's debug
 * output calls them game_mode and loop_done).  Named from what the code
 * shows a mode is (hd.c's mode switches, the setup each mode runs, the front
 * end's fe_back_loop); every bit the code uses has a name (bits 35 and 60
 * are never used).  "Never entered": handled somewhere, but nothing sets it.
 * Each macro is the literal the code used, so the compiled code is the same.
 * Asserts keep their literals: Rare's assert macros put the expression's
 * text into the ROM.
 */
#define MODE_ATTRACT 0x2                    /* the attract demos ("PRESS START") */
#define MODE_LEVEL 0x4                      /* a level in play */
#define MODE_BOOT_SCREEN1 0x10              /* the front end's boot screens (fe_boot_screen_init) */
#define MODE_BOOT_SCREEN2 0x20
#define MODE_MAP_VIEW 0x100                 /* "MISSILE VIEW" / "SHUTTLE VIEW" */
#define MODE_LEVEL_START 0x2000             /* a level's file is loaded, then MODE_LEVEL */
#define MODE_WORLD 0x4000                   /* the level-select globe ("World screen centred on level") */
#define MODE_LOAD_GAME 0x10000              /* the load-game menu */
#define MODE_NAME_ENTRY 0x40000             /* "ENTER NAME!" */
#define MODE_ERASE_MENU 0x400000            /* the erase-game menu */
#define MODE_RESULTS 0x8000000              /* after a level: results, saving */
#define MODE_VEHICLE_SELECT 0x20000000      /* digger_loop.c */
#define MODE_PROMOTION 0x40000000           /* "CONGRATULATIONS ON YOUR PROMOTION!" */
#define MODE_BEST_TIMES 0x40000000000       /* bestTimes.c */
#define MODE_GLOBE_SEQ 0x100000000000       /* the globe's sequences */
#define MODE_ATTRACT_PICTURE 0x1000000000000 /* the attract mode's picture screen */
#define MODE_BOOT_CHECK 0x20000000000000    /* the boot-time EEPROM / Controller Pak check */
#define MODE_ERASE_SAVE 0x40000000000000    /* "ERASE SAVED GAME!" */
#define MODE_GUEST_PLAYER 0x100000000000000 /* "BECOME GUEST PLAYER:" */
#define MODE_ATTRACT_START 0x400000000000000 /* the start of the attract sequence */
#define MODE_YOSHI_PAGES 0x2000000000000000 /* the text-window pages ("USE Z/R TO TURN PAGES") */
#define MODE_QUIT_GAME 0x4000000000000000   /* "QUIT GAME!" */
/* (naming phase 3) */
#define MODE_INTRO_APPROACH 0x1                  /* level intro: the BCT chopper nears its landing site */
#define MODE_LEVEL_CLEAR 0x8                     /* end object touched, no carrier: clear tune, then results */
#define MODE_REPLAY 0x40                         /* the replay of the player's recorded run */
#define MODE_LEVEL_STATUS 0x80                   /* a completed level picked on the globe: medals, menu */
#define MODE_CARRIER_END 0x200                   /* a carrier/shuttle level ends (hit or done): carrier camera */
#define MODE_REPLAY_CARRIER_END 0x400            /* the same, in a replay */
#define MODE_INTRO_FLY 0x800                     /* level intro: the BCT chopper flies in */
#define MODE_INTRO_LANDING 0x1000                /* level intro: the chopper lands and drops the driver */
#define MODE_PAK_CHECKED 0x8000                  /* after the boot pak check: load menu, pak check or boot check */
#define MODE_NEW_PLAYER 0x20000                  /* an empty slot chosen, then MODE_NAME_ENTRY */
#define MODE_ERASE_CONFIRM 0x80000               /* erase this player? */
#define MODE_ERASE_PLAYER_CHOSEN 0x100000        /* a player picked in the erase menu */
#define MODE_LOAD_GAME_RETURN 0x200000           /* back to the load-game menu */
#define MODE_TO_ERASE_MENU 0x800000              /* on to the erase menu */
#define MODE_NAME_ENTERED 0x1000000              /* a new player named: saved, then the intro sequence */
#define MODE_PLAYER_CHOSEN 0x2000000             /* a load-menu slot chosen */
#define MODE_TIMED_END 0x4000000                 /* an objective level ended (done, time out or failed) */
#define MODE_PAK_CHECK 0x10000000                /* waiting for the Controller Pak check (no EEPROM) */
#define MODE_PRE_PROMOTION 0x80000000            /* (never entered) a delay, then MODE_PROMOTION */
#define MODE_PAK_FILES 0x100000000               /* pak manager: the file list */
#define MODE_UNUSED_33 0x200000000               /* (never entered) tune 0x21 */
#define MODE_UNUSED_WINDOW_13 0x400000000        /* (never entered) text window 0x13 */
#define MODE_ACADEMY_MARKER 0x1000000000         /* the academy marker reached: message window, paused */
#define MODE_PAK_MANAGER_CHECKED 0x2000000000    /* pak manager: the check's reply */
#define MODE_PAK_FILE_CHOSEN 0x4000000000        /* pak manager: a file picked */
#define MODE_PAK_DELETE_CONFIRM 0x8000000000     /* pak manager: delete this file? */
#define MODE_PAK_FILE_DELETED 0x10000000000      /* pak manager: file deleted, back to the list */
#define MODE_UNUSED_41 0x20000000000             /* (never entered) */
#define MODE_PAK_MANAGER 0x80000000000           /* START held at boot: pak check for the file manager */
#define MODE_SEQ_END 0x200000000000              /* request: a sequence ended -> seq_finish(0) */
#define MODE_SEQ_SKIP 0x400000000000             /* request: a sequence skipped -> seq_finish(1) */
#define MODE_ACADEMY_MESSAGE 0x800000000000      /* the game state's (academy) message window and tune */
#define MODE_ACADEMY_NEXT 0x2000000000000        /* request: academy_next_mode picks the mode */
#define MODE_REGRADE_NOTICE 0x4000000000000      /* game state 10: missions regraded notice */
#define MODE_REGRADE_NOTICE_END 0x8000000000000  /* then a fade to the world */
#define MODE_UNUSED_52 0x10000000000000          /* (never entered) tune 0x28 */
#define MODE_BEST_TIMES_UNUSED 0x80000000000000  /* (never entered) a best-times variant */
#define MODE_BEST_TIMES_RETURN 0x200000000000000 /* back into best times without rebuilding it */
#define MODE_NO_CONTROLLER 0x800000000000000     /* no controller 1 at boot */
#define MODE_LOAD_GAME_EXIT 0x8000000000000000   /* B on the load menu, then the attract sequence */

/* Level kinds: gLevelInfo[].kind (LevelInfo.kind, ROM data) and gLevelKind, its copy for the
 * current level (hd_setup_level). One bit each (lowest_set_bit indexes the per-kind window
 * tables gYoshiResultsWindowByKind / gYoshiReplayWindowByKind). From the per-kind switches in 1D990.c (hud_setup_objective,
 * hud_mission_intro's objective text, hud_update_objective's checks). */
#define LEVEL_KIND_MISSION        1     /* main (carrier) mission, untimed; saves a status */
#define LEVEL_KIND_RACE           2     /* "FINISH %d LAPS IN"; hud_update_race */
#define LEVEL_KIND_TARGETS        4     /* "DESTROY TARGETS IN"; hud_check_targets */
#define LEVEL_KIND_DAMAGE         8     /* "CAUSE $%d DAMAGE"; hud_check_damage */
#define LEVEL_KIND_RDUS           0x10  /* "FIND %d RDUS IN"; hud_check_rdus */
#define LEVEL_KIND_BUILDINGS      0x20  /* "DESTROY %s IN" (yoshi_get_target_building); hud_check_path */
#define LEVEL_KIND_PILL_MAZE      0x40  /* RDUs + power pill / ghost (SFX_GHOST_CAUGHT, SFX_RDU_GHOST_LEVEL) */
#define LEVEL_KIND_MISSION_TIMED  0x80  /* a main mission retyped once gameState >= 11 on the world
                                         * (fe_player_update_level_kinds): timed, "CLEAR CARRIER PATH" */
#define LEVEL_KIND_ANY_MISSION    0x81  /* LEVEL_KIND_MISSION | LEVEL_KIND_MISSION_TIMED (the code's mask) */

/* Yoshi (text) windows: currentYoshiWindow / gYoshiPendingWindow, gYoshiWindows[id] (ROM data,
 * 108 windows) and yoshi_demand(flags | id). Named from the code that opens or tests them;
 * ids no code names are left out (see the notes). */
#define NO_YOSHI_WINDOW        -1      /* same text as 26570.c's own define (yoshi.c's name) */
#define YOSHI_DEMAND_OFF       0x4000  /* same text as 26570.c's own define: close the window */
#define YOSHI_DEMAND_OPEN      0x8000  /* yoshi_run_window: open window (yd & 0xFF) */
#define YOSHI_DEMAND_FORCE     0x2000  /* yoshi_run_window: open now, even over an open window */
#define YW_PAUSE               0x00    /* START in a level (hd_main_thread); map view while it is open */
#define YW_PAUSE_OPTIONS       0x01    /* pause item 1; B returns to YW_PAUSE; YW_MUSIC_VOLUME and
                                        * YW_CONTROL_METHOD return to it */
#define YW_CARRIER_ALARM       0x02    /* carrier in the near zone (gCarrierDanger == 2, SFX_CARRIER_ALARM) */
#define YW_CARRIER_DANGER      0x03    /* carrier in the far zone (gCarrierDanger == 1, danger tune) */
#define YW_LEVEL_DONE          0x05    /* hud_level_complete (gLevelDoneWindow), SND_GROUP_LEVEL_DONE */
#define YW_STATUS              0x06    /* stats_finish_level (pause item 2); replay-end menu (sel 0x17/0x18) */
#define YW_LAPS_LEFT           0x08    /* hud_update_race: "%d LAPS LEFT!" */
#define YW_MISSION_INTRO       0x09    /* hud_mission_intro: objective and time limit */
#define YW_PLAYER_MENU         0x0A    /* load-game and erase menus (sel - 2 = player slot) */
#define YW_NAME_ENTRY          0x0B    /* MODE_NAME_ENTRY "ENTER NAME!" */
#define YW_CONFIRM             0x0C    /* QUIT GAME! / ERASE SAVED GAME! / BECOME GUEST PLAYER: /
                                        * erase / pak-delete confirm (item 0xC = yes) */
#define YW_MUSIC_VOLUME        0x0D    /* pause item 9: items 40 / 39 = music volume 1.0 / 0.7 */
#define YW_MISSION_FAILED      0x0E    /* gCarrierHit (hud_update_objective, hud_mission_failed) */
#define YW_PAK_CHECK           0x11    /* MODE_PAK_CHECK / MODE_PAK_MANAGER, closed on the pak reply */
#define YW_PAK_FILES           0x12    /* MODE_PAK_FILES: the pak file list */
#define YW_UNUSED_13           0x13    /* MODE_UNUSED_WINDOW_13 (never entered) */
#define YW_ACADEMY_MARKER      0x15    /* MODE_ACADEMY_MARKER (academy_build_window) */
#define YW_BEST_TIMES          0x16    /* MODE_BEST_TIMES */
#define YW_GAME_STATE_BASE     0x16    /* academy_announce_state: window YW_GAME_STATE_BASE + gameState */
#define YW_SHUTTLE_MESSAGE     0x17    /* shuttle path events: "LANDING ABORTED!", "3000 FT!" ... */
#define YW_MOON_DONE           0x1E    /* level 0x28 (lvl_planet: Moon) done */
#define YW_SHUTTLE_DONE        0x23    /* level 0x32 done (MUS_FINALE) */
#define YW_SHUTTLE_INTRO       0x24    /* MODE_INTRO_FLY on level 0x32 */
#define YW_DEMO0_MSG2          0x25    /* hud_attract_messages: attract demo 0, frame 400 */
#define YW_DEMO0_MSG3          0x26    /* attract demo 0, frame 700 */
#define YW_SEQ2F_MSG1          0x27    /* level 0x2F sequence (playback 0 outside MODE_ATTRACT), frame 100 */
#define YW_SEQ2F_MSG2          0x28    /* frame 300 */
#define YW_SEQ2F_MSG3          0x29    /* frame 500 */
#define YW_SEQ2F_MSG4          0x2A    /* frame 700 */
#define YW_DEMO1_MSG1          0x2B    /* attract demo 1, frame 180 */
#define YW_DEMO1_MSG2          0x2C    /* attract demo 1, frame 470 */
#define YW_DEMO2_MSG           0x2D    /* attract demo 2, frame 180 */
#define YW_DEMO3_MSG           0x2E    /* attract demo 3 */
#define YW_DEMO4_MSG           0x2F    /* attract demo 4 */
#define YW_DEMO5_MSG           0x30    /* attract demo 5 */
#define YW_DEMO6_MSG1          0x31    /* attract demo 6, frame 180 */
#define YW_DEMO6_MSG2          0x32    /* attract demo 6, frame 470 */
#define YW_DEMO7_MSG           0x33    /* attract demo 7 */
#define YW_DEMO8_MSG           0x34    /* attract demo 8 */
#define YW_ATTRACT_PICTURE     0x35    /* MODE_ATTRACT_PICTURE */
#define YW_CREDITS_START       0x36    /* credits_draw, frame 100 */
#define YW_CREDITS_END         0x37    /* credits_draw, end timer (with MUS_FINALE) */
#define YW_REGRADE_NOTICE      0x38    /* MODE_REGRADE_NOTICE (game state 10) */
#define YW_BONUS_RDUS          0x39    /* stats_award_bonuses: every RDU lit */
#define YW_BONUS_PEOPLE        0x3A    /* every person released (gScorePeople == gPeopleTotal) */
#define YW_BONUS_BUILDINGS     0x3B    /* every building destroyed (gScoreBuildings == gBuildingCount) */
#define YW_BONUS_ALL           0x3C    /* all three (with SFX_ALL_BONUSES) */
#define YW_TARGETS_CLEARED     0x3D    /* mission targets cleared (hud_draw_target_marker), SFX_SUCCESS */
#define YW_DEMO0_MSG1          0x3E    /* attract demo 0, frame 150 */
#define YW_INTRO_GAME_STATE0   0x3F    /* level intro (chopper), frame 10, only at gameState 0 */
#define YW_INTRO_FRAME150      0x40    /* level intro (chopper), frame 150, not level 0x32 */
#define YW_DEMO_BACKLASH       0x41    /* seq_start_level: vehicle demo, level 55 (vehicle 5) */
#define YW_DEMO_SIDESWIPE      0x42    /* level 28 (vehicle 1) */
#define YW_DEMO_THUNDERFIST    0x43    /* level 53 (vehicle 2) */
#define YW_DEMO_SKYFALL        0x44    /* level 7 (vehicle 3) */
#define YW_DEMO_JBOMB          0x45    /* level 19 (vehicle 9) */
/* Hint windows 0x46..0x57 (yoshi_show_hint: per-level lists gYoshiHints, shown counts in
 * PlayerInfo.hintShows[id - YW_HINT_FIRST]) */
#define YW_HINT_FIRST          0x46
#define YW_HINT_RDUS           0x46    /* rdu_update: 4th RDU lit */
#define YW_HINT_COMMPT         0x47    /* stats_show_commpt_hint */
#define YW_HINT_PEOPLE         0x48    /* people_spawn: people run out */
#define YW_HINT_CARRIER_NEAR   0x49    /* carrier in the far zone (else YW_CARRIER_DANGER) */
#define YW_HINT_CAR            0x4A    /* veh_apply_switch: into vehicle 8, 13, 14 or 15 (car engines) */
#define YW_HINT_TARGETS_CLEARED 0x4B   /* targets cleared (else YW_TARGETS_CLEARED) */
#define YW_HINT_OTHER_VEHICLE  0x4C    /* util_check_hint_events: within 100 of another vehicle */
#define YW_HINT_MARKER_ARROWS  0x4D    /* level 0, two marker arrows on screen */
#define YW_HINT_BUILDING_HIT   0x4E    /* hit a non-path building, level not done */
#define YW_HINT_BUILDING_HIT2  0x4F    /* the same, when YW_HINT_BUILDING_HIT is used up */
#define YW_HINT_TRAIN          0x50    /* gTrainContactFrames == 15 */
#define YW_HINT_LEVEL12_SPOT   0x51    /* level 0x12, near (0x531, 0x1061) */
#define YW_HINT_START_A        0x52    /* every level start */
#define YW_HINT_LEVEL0_TRAIN   0x53    /* level 0, in vehicle 7 (train) past x 0x899 */
#define YW_HINT_RAMDOZER_PUSH  0x54    /* Ramdozer (vehicle 4) starts pushing a block */
#define YW_HINT_START_DONE     0x55    /* level start, mission already done */
#define YW_HINT_START_B        0x56    /* every level start */
#define YW_HINT_START_NEW      0x57    /* level start otherwise */
#define YW_HINT_LAST           0x57
#define YW_CONTROL_METHOD      0x58    /* cont_show_method_option: "CONTROL METHOD:" */
#define YW_NO_CONTROLLER       0x59    /* MODE_NO_CONTROLLER */
#define YW_PAGES_FIRST         0x5E    /* MODE_YOSHI_PAGES (pause item 8): first page, Z/R turn pages */
#define YW_PAGES_LAST          0x6B

#include "game/sound.h"

#endif
