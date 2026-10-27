#ifndef GAME_SOUND_H
#define GAME_SOUND_H

/* Sound-effect ids (gSfxBank, snd_play's id) and music tune ids (music_*),
 * named from their trigger sites (naming phase 3).  Several ids are shared by
 * unrelated events; the comment lists them.  Per-level tune tables
 * (gLevelTunes ...), gEffectSounds and gSoundGroups are ROM data. */

/* ---- sound effects: snd_play(gSfxBank, SFX_x, handle) ---- */
#define SFX_CLUNK                 0x01 /* name-entry letter lands; Ramdozer bump */
#define SFX_JBOMB_THRUST          0x02 /* J-Bomb jet loop (also its select sound) */
#define SFX_BALLISTA_MISSILE      0x03 /* Ballista L/R missile */
#define SFX_ENGINE_BACKLASH       0x05
#define SFX_SHUTTLE_POINT         0x06 /* shuttle path points 1, 2 reached */
#define SFX_SCRAPE_LOOP           0x07 /* pushed TNT box / block sliding */
#define SFX_SHUTTLE_POINT3        0x08 /* shuttle path point 3 reached */
#define SFX_SKID                  0x0A /* hard turn (pitch by speed); Ramdozer pull-away */
#define SFX_ENGINE_RAMDOZER       0x0B
#define SFX_THUD                  0x0C /* bounce / collision; name-entry D-pad */
#define SFX_HORN_POLICE           0x0D
#define SFX_JBOMB_AIR             0x0F /* J-Bomb airborne loop */
#define SFX_EXPLOSION             0x10 /* TNT, shells */
#define SFX_CHOPPER_LOOP          0x13
#define SFX_STEP_FOOT_A           0x14 /* the driver on foot */
#define SFX_STEP_FOOT_B           0x15
#define SFX_PROMOTION_CUE         0x1C /* before MODE_PROMOTION */
#define SFX_MENU_MOVE             0x1D /* cursor move / page turn */
#define SFX_SELECT                0x1E /* confirm */
#define SFX_ENGINE_TRAIN          0x20
#define SFX_TRAIN_WHISTLE         0x21 /* the train's L/R loop */
#define SFX_TYPE_KEY              0x22 /* yoshi typewriter character */
#define SFX_TYPE_NEWLINE          0x23 /* typewriter: the next item */
#define SFX_PEOPLE_SCREAM         0x24 /* survivors run out / flee */
#define SFX_CHOPPER_DISPATCH      0x25 /* the chopper is sent for survivors */
#define SFX_CARRIER_ALARM         0x26 /* loop while the carrier is near */
#define SFX_RDU                   0x27 /* an RDU lit */
#define SFX_CHOPPER_LAND          0x28
#define SFX_TRAIN_BRAKE           0x29
#define SFX_ERROR                 0x2B /* refused / invalid */
#define SFX_FAIL_CRASH            0x31 /* gCarrierHit */
#define SFX_MISSION_FAILED        0x32 /* 0xB4 frames after the crash */
#define SFX_ENGINE_SIDESWIPE      0x36
#define SFX_SIDESWIPE_RAM         0x37
#define SFX_RDU_GHOST_LEVEL       0x3B /* an RDU lit, level kind 0x40 */
#define SFX_GHOST_CAUGHT          0x3C /* level kind 0x40: hit without the pill */
#define SFX_GHOST_EATEN           0x3D /* level kind 0x40: hit with the pill */
#define SFX_GLOBE_PLANET_MOVE     0x3F
#define SFX_ROLL_CRASH            0x4B /* Thunderfist / Cyclone move hits */
#define SFX_MECH_STEP_A           0x4E /* Thunderfist / Cyclone */
#define SFX_MECH_STEP_B           0x4F
#define SFX_ENGINE_MECH           0x50 /* Thunderfist / Cyclone */
#define SFX_ROLL_LOOP             0x51 /* Thunderfist roll / Cyclone charge */
#define SFX_HORN_ATEAM            0x54
#define SFX_TURBO                 0x55
#define SFX_COACH_MISUSE          0x58 /* "USING DIGGER INCORRECTLY" */
#define SFX_JBOMB_STEP_A          0x5A
#define SFX_JBOMB_STEP_B          0x5B
#define SFX_JBOMB_STOMP           0x5C
#define SFX_COMMPT_BURST          0x5E
#define SFX_COACH_WELL_BACKLASH   0x63
#define SFX_BLOCK_FALL            0x64
#define SFX_SHUTTLE_EVENT2        0x66
#define SFX_UNK_0x68_CHIME        0x68 /* carrier alarm 2 -> 0; the debug-cheat boot */
#define SFX_STATIC                0x69 /* panel / attract picture flicker */
#define SFX_SHUTTLE_LOOP          0x6A
#define SFX_PANEL_WARNING         0x6C /* too far from the carrier; respawn */
#define SFX_CRANE_ACTION          0x6D /* L/R */
#define SFX_CRANE_SWING           0x6E /* left / right */
#define SFX_CRANE_ARM             0x6F /* A/B */
#define SFX_CRATE_PICKUP          0x70
#define SFX_HORN_BARGE            0x71
#define SFX_ENGINE_BARGE          0x72
#define SFX_TICK_LOOP             0x73 /* TNT fuse; pak wait */
#define SFX_CARRIER_LOOP          0x75
#define SFX_HORN_STARSKI          0x76
#define SFX_VOICE_DITCHING        0x77 /* "DITCHING IN SEA!" */
#define SFX_SHUTTLE_EVENT3        0x78
#define SFX_ENGINE_ATEAM          0x7B
#define SFX_ROUTE_REVEAL_LOOP     0x7C /* globe: a new route drawing */
#define SFX_JBOMB_STOMP_LAND      0x7D
#define SFX_SHUTTLE_LOOP2         0x7F
#define SFX_SUCCESS               0x82 /* targets cleared; "SUCCESSFUL LANDING!" */
#define SFX_SHUTTLE_ROAR          0x83 /* path event 0xB: shake + flames */
#define SFX_ENGINE_SKYFALL        0x8C
#define SFX_SKYFALL_BOOST         0x8D
#define SFX_HORN_RAMDOZER         0x8F
#define SFX_NAME_WHEEL_TICK       0x90
#define SFX_TYPE_ALT              0x91 /* name letter picked; typewriter special char */
#define SFX_ENGINE_BALLISTA       0x94
#define SFX_COUNTDOWN_BEEP        0x96 /* mission intro 3, 2, 1 */
#define SFX_COUNTDOWN_GO          0x99
#define SFX_TIME_TICK_9           0x9A /* the last 10 s: SFX_TIME_TICK_0 - seconds */
#define SFX_TIME_TICK_0           0xA3
#define SFX_HORN_AMDREAM          0xA4
#define SFX_POWER_PILL            0xB0
#define SFX_COACH_WELL_SKYFALL    0xB2
#define SFX_COACH_WRONG_DIGGER    0xB3 /* "USING WRONG DIGGER" */
#define SFX_BOOT2_LAND            0xB8
#define SFX_BOOT1_IN              0xBA
#define SFX_BOOT1_LAND            0xBB
#define SFX_BOOT2_IN              0xBD
#define SFX_ALL_BONUSES           0xC6
#define SFX_ENGINE_CAR            0xCE /* American Dream, police, type 0xF */
#define SFX_POWER_PILL_LOOP       0xCF
#define SFX_DENIED                0xD0 /* can't move / blocked */
#define SFX_VOICE_1000FT          0xD1
#define SFX_VOICE_2000FT          0xD3
#define SFX_VOICE_3000FT          0xD5
#define SFX_VOICE_FINAL_APPROACH  0xD7
#define SFX_VOICE_LANDING_ABORTED 0xDA
#define SFX_CAM_OUT               0xDC /* C-down zoom out; C-left turn */
#define SFX_CAM_IN                0xDD /* C-up zoom in; C-right turn */
#define SFX_BACK                  0xDE /* B / cancel */
#define SFX_MAP_VIEW              0xDF /* map view on / off */
#define SFX_NAME_DELETE           0xE4
#define SFX_MEDAL                 0xE6 /* medal ranks 1-4 */
#define SFX_MEDAL_NONE            0xEA /* rank 5: done without a medal */

/* snd_random_id(n) groups (gSoundGroups, ROM data) */
#define SND_GROUP_MISSION_INTRO 0
#define SND_GROUP_LEVEL_START   1
#define SND_GROUP_LEVEL_DONE    2
#define SND_GROUP_DONE_PANEL    3
#define SND_GROUP_DOING_WELL    4

/* Effect kinds: byte 0x31 of an effect record (obj_queue_effect); per kind, the effect
 * definitions gObjEffectDefLists[kind] and the sound gEffectSounds[kind] (snd_play_effect). Kinds
 * come from ROM records/tables; only one is set by code. */
#define EFFECT_KIND_COUNT        28    /* snd_play_effect: "effect id %d out of range!" */
#define EFFECT_KIND_COMMPT_BURST 0x15  /* commpt_burst's 24 records; fx_spawn tag 1 */

/* ---- music tunes: music_start_tune / music_push_tune (0x42 sequences) ---- */
#define MUS_INTRO_FLYOVER     0x03 /* MODE_INTRO_FLY / MODE_INTRO_LANDING */
#define MUS_REPLAY            0x0A
#define MUS_WORLD_EARTH       0x0C
#define MUS_RESULTS           0x0E
#define MUS_PROMOTION         0x0F
#define MUS_ATTRACT           0x11
#define MUS_WORLD_SPACE       0x13 /* the globe, not on Earth */
#define MUS_ACADEMY_MARKER    0x18 /* pushed in MODE_ACADEMY_MARKER */
#define MUS_VEHICLE_SELECT    0x1D
#define MUS_MENU              0x21 /* load / name / erase menus */
#define MUS_FINALE            0x25 /* level 0x32 done; the credits' end */
#define MUS_UNK_0x27_STATE10  0x27 /* under MUS_PROMOTION, game state 10 */
#define MUS_UNK_0x28          0x28 /* MODE_UNUSED_52 (never entered) */

#endif
