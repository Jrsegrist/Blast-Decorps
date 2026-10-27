#include "common.h"
#include <ultra64.h>
#include "game/game.h"

/* Music player: tune selection, push/pop tune stack, volume fades and audio init */

/* saved-tune stack entry (0x1F4 bytes) */
typedef struct {
    u32 chanState[64];
    ALCSeqMarker marker;
    s32 tempo;
    u8 tune;
} TuneState;

/* Blast Corps' SDK ALCSPlayer has one more word before state than 2.0I's */
typedef struct {
    u8 pad0[0x2C];
    s32 state;
    u8 pad30[0x30];
    u32 * N64P chanState;
} BCSeqPlayer;

typedef struct {
    s32 maxVVoices;
    s32 maxPVoices;
    s32 maxUpdates;
    s32 maxFXbusses;
    void * N64P dmaproc;
    ALHeap * N64P heap;
    s32 outputRate;
    u8 fxType;
    s32 * N64P params;
} SynConfig;


extern u8 romMusicBankCtl[];
extern u8 romMusicBankTbl[];
extern u8 romSfxBankCtl[];
extern u8 romSfxBankTbl[];
extern u8 romSequenceFile[];

extern u8 gSeqIndex;
extern s16 gTuneVolumes[];
extern f32 gMusicMasterVolume;
extern u8 gLevelDangerTunes[];
extern u8 gLevelDoneTunes[];
extern u8 gLevelClearTunes[];
extern u16 gEffectSounds[];
extern u8 gLevelTunes[];
extern s32 gSoundGroups[][6];
extern TuneState gTuneStack[];
extern TuneState * N64P gTuneStackTop;
extern s32 gSeqSizes[];
extern void * N64P gSeqBuffer;
extern ALSeqFile * N64P gSeqFile;
extern ALCSeq gSeqs[];
extern u8 gMusicTune;
extern f32 gMusicVolume;
extern f32 gMusicStartVolume;
extern f32 gMusicPopVolume;
extern ALHeap gAudioHeap;
extern void * N64P gMusicBank;
extern u8 gAudioHeapBuf[];

#define SEQP ((BCSeqPlayer *) gMusicPlayer)

void music_pop_tune_at(f32 arg0);

/* Stops the player, loads the tune's sequence, plays it (0x80260C20). */
void music_play_tune(u8 tune, f32 vol) {
    void *rom;

    gSeqIndex ^= 1;
    alCSPStop(gMusicPlayer);
    gMusicFading = 0;
    gMusicVolume = vol;
    gMusicTune = tune;
    rom = gSeqFile->seqArray[tune].offset;
    hd_rom_load((u32) rom, (u32) gSeqBuffer, (u32 *) &gSeqSizes[tune], 0, 0, 0);
    alCSeqNew(&gSeqs[gSeqIndex], gSeqBuffer);
    alCSPSetSeq(gMusicPlayer, &gSeqs[gSeqIndex]);
    alCSPPlay(gMusicPlayer);
    alCSPSetVol(gMusicPlayer, gTuneVolumes[gMusicTune] * gMusicVolume * gMusicMasterVolume);
}

/* The music master volume (pause menu: 1.0 / 0.7) (0x80260D7C). */
void music_set_master_volume(f32 vol) {
    gMusicMasterVolume = vol;
    alCSPSetVol(gMusicPlayer, gTuneVolumes[gMusicTune] * gMusicVolume * vol);
}

/* The music master volume (0x80260DF0). */
f32 music_get_master_volume(void) {
    return gMusicMasterVolume;
}

/* Pushes the level's tension tune (carrier near, "1000 FT!") (0x80260DFC). */
void music_push_danger_tune(void) {
    music_push_tune(gLevelDangerTunes[levelno]);
}

/* The level-done tune at the old tempo; notes the start time (0x80260E2C). */
void music_play_done_tune(void) {
    gDoneTuneStartTime = ((s32) gScheduler.frameCount);
    gMusicSavedTempo = alCSPGetTempo(gMusicPlayer);
    music_start_tune(gLevelDoneTunes[levelno]);
}

/* The level's mode-8 tune (cleared, no carrier) (0x80260E80). */
void music_play_clear_tune(void) {
    gDoneTuneStartTime = ((s32) gScheduler.frameCount);
    music_start_tune(gLevelClearTunes[levelno]);
}

/* No callers: only calls music_push_danger_tune (0x80260EC0). */
void music_push_danger_tune_stub(void) {
    music_push_danger_tune();
}

/* Pushes a tune ("push tune %d") (0x80260EE0). */
void music_push_tune(u8 tune) {
    if (gMusicPopState) {
        debug_printf("OH DEAR - pushing tune but we're still popping!\n");
    } else {
        debug_printf("push tune %d\n", tune);
        gTuneStackTop->tune = gMusicTune;
        gMusicTune = tune;
        gTuneStackEmpty = 0;
        gMusicPushState = 1;
    }
}

/* Pops the tune stack; the old tune restarts at a volume (0x80260F60). */
void music_pop_tune_at(f32 vol) {
    debug_printf("1 pop tune");
    if (gTuneStackTop == gTuneStack) {
        return;
    }
    gTuneStackTop--;
    if (gTuneStackTop == gTuneStack) {
        gTuneStackEmpty = 1;
    }
    debug_printf("2 pop tune %d\n", gTuneStackTop->tune);
    gMusicTune = gTuneStackTop->tune;
    alCSPStop(gMusicPlayer);
    gMusicPopState = 2;
    gMusicPopVolume = vol;
}

/* Pops the tune stack, fading up from 0 (0x8026101C). */
void music_pop_tune(void) {
    music_pop_tune_at(0.0f);
}

/* The game's name (hd.c's debug text "popTuneImmediate();") (0x80261040). */
void popTuneImmediate(void) {
    music_pop_tune_at(1.0f);
}

/* Audio thread: the pop state machine (0x80261068). */
void music_update_pop(void) {
    u32 i;
    ALCSeqMarker marker;

    switch (gMusicPopState) {
        case 2:
            if (alCSPGetState(gMusicPlayer) == 0) {
                music_play_tune(gMusicTune, gMusicPopVolume);
                gMusicPopState = 1;
            }
            break;
        case 1:
            alCSeqGetLoc(&gSeqs[gSeqIndex], &marker);
            if (alCSPGetState(gMusicPlayer) == 1 && marker.lastTicks != 0) {
                alCSeqSetLoc(&gSeqs[gSeqIndex], &gTuneStackTop->marker);
                alCSPSetTempo(gMusicPlayer, gTuneStackTop->tempo);
                for (i = 0; i < 64; i++) {
                    SEQP->chanState[i] = gTuneStackTop->chanState[i];
                }
                gMusicPopState = 0;
                if (gMusicPopVolume != 1.0) {
                    music_fade_to(1.0f);
                }
            }
            break;
    }
}

/* Audio thread: pops when a pushed tune has ended ("auto popping") (0x802611F0). */
void music_update_auto_pop(void) {
    ALCSeqMarker marker;

    alCSeqGetLoc(&gSeqs[gSeqIndex], &marker);
    if (!gMusicPushState && !gMusicPopState && alCSPGetState(gMusicPlayer) == 0 && marker.lastTicks != 0) {
        debug_printf("auto popping\n");
        music_pop_tune();
    }
}

/* Audio thread: saves the playing tune, starts the pushed one (0x80261284). */
void music_update_push(void) {
    u32 i;

    switch (gMusicPushState) {
        case 1:
            if (alCSPGetState(gMusicPlayer) == 1) {
                alCSeqGetLoc(&gSeqs[gSeqIndex], &gTuneStackTop->marker);
                gTuneStackTop->tempo = alCSPGetTempo(gMusicPlayer);
                for (i = 0; i < 64; i++) {
                    gTuneStackTop->chanState[i] = SEQP->chanState[i];
                }
                alCSPStop(gMusicPlayer);
                gTuneStackTop++;
                gMusicPushState = 2;
            }
            break;
        case 2:
            if (alCSPGetState(gMusicPlayer) == 0) {
                music_play_tune(gMusicTune, 1.0f);
                gMusicPushState = 0;
            }
            break;
    }
}

#define ABSF(x) ((x) > 0.0f ? (x) : -(x))

/* Audio thread: eases the sequence volume toward the target (0x802613C8). */
void music_update_fade(void) {
    f32 cur;
    f32 target;
    s16 vol;

    cur = alCSPGetVol(gMusicPlayer);
    target = gTuneVolumes[gMusicTune] * gMusicMasterVolume;
    vol = cur + (target * gMusicVolume - cur) * 0.075;
    if (ABSF(vol - target * gMusicVolume) < 10.0f) {
        vol = target * gMusicVolume;
        gMusicFading = 0;
    }
    alCSPSetVol(gMusicPlayer, vol);
}

/* Audio thread: re-applies the saved tempo once playing (0x80261528). */
void music_restore_tempo(void) {
    if (SEQP->state == 1) {
        alCSPSetTempo(gMusicPlayer, gMusicSavedTempo);
        gMusicSavedTempo = 0;
    }
}

/* Fades the music to a volume scale (0x80261570). */
void music_fade_to(f32 arg0) {
    gMusicVolume = arg0;
    gMusicFading = 1;
}

/* Audio heap, banks, sequence file, synth, players, snd_init (0x80261588). */
void music_init_audio(void) {
    SndConfig sndc;
    ALSeqpConfig seqc;
    SynConfig sync;
    ALBankFile *sfxBank;
    ALBankFile *musBank;
    s32 size2;
    s32 size1;
    u32 i;
    s32 pad[2];
    s32 seqSize;
    s32 hdrSize;

    alHeapInit(&gAudioHeap, gAudioHeapBuf, 0x2A280);
    size2 = size1 = romMusicBankTbl - romMusicBankCtl;
    hd_rom_load((u32) romMusicBankCtl, 0x8004B400, (u32 *) &size2, 0xD, 0, 2);
    musBank = alHeapDBAlloc(0, 0, &gAudioHeap, 1, size2);
    hd_rom_load((u32) romMusicBankCtl, (u32) musBank, (u32 *) &size1, 0xD, 0, 2);
    alBnkfNew(musBank, romMusicBankTbl);
    gMusicBank = musBank->bankArray[0];
    size2 = size1 = romSfxBankTbl - romSfxBankCtl;
    hd_rom_load((u32) romSfxBankCtl, 0x8004B400, (u32 *) &size2, 0xD, 0, 2);
    sfxBank = alHeapDBAlloc(0, 0, &gAudioHeap, 1, size2);
    hd_rom_load((u32) romSfxBankCtl, (u32) sfxBank, (u32 *) &size1, 0xD, 0, 2);
    alBnkfNew(sfxBank, romSfxBankTbl);
    gSfxBank = sfxBank->bankArray[0];
    gSeqFile = alHeapDBAlloc(0, 0, &gAudioHeap, 1, 4);
    hdrSize = 4;
    hd_rom_load((u32) romSequenceFile, (u32) gSeqFile, (u32 *) &hdrSize, 0, 0, 0);
    seqSize = gSeqFile->seqCount * 8 + 4;
    gSeqFile = alHeapDBAlloc(0, 0, &gAudioHeap, 1, 0x214);
    hd_rom_load((u32) romSequenceFile, (u32) gSeqFile, (u32 *) &seqSize, 0, 0, 0);
    alSeqFileNew(gSeqFile, romSequenceFile);
    gSeqBuffer = alHeapDBAlloc(0, 0, &gAudioHeap, 1, 0x21AE);
    for (i = 0; i < 0x42; i++) {
        gSeqSizes[i] = gSeqFile->seqArray[i].len;
        if (gSeqSizes[i] & 1) {
            gSeqSizes[i]++;
        }
    }
    sync.maxVVoices = 0;
    sync.maxPVoices = 0x18;
    sync.maxUpdates = 0x80;
    sync.maxFXbusses = 1;
    sync.dmaproc = NULL;
    sync.fxType = 6;
    sync.outputRate = 0;
    sync.heap = &gAudioHeap;
    amCreateAudioMgr((ALSynConfig *) &sync, 0xC);
    seqc.maxVoices = 0x18;
    seqc.maxEvents = 0x20;
    seqc.maxChannels = 0x10;
    seqc.heap = &gAudioHeap;
    seqc.initOsc = NULL;
    seqc.updateOsc = NULL;
    seqc.stopOsc = NULL;
    gMusicPlayer = alHeapDBAlloc(0, 0, &gAudioHeap, 1, 0x7C);
    alCSPNew(gMusicPlayer, &seqc);
    alCSPSetBank(gMusicPlayer, gMusicBank);
    sndc.maxEvents = 0x40;
    sndc.maxStates = 0x20;
    sndc.maxSounds = 8;
    sndc.slotCount = 8;
    sndc.heap = &gAudioHeap;
    snd_init(&sndc);
    debug_printf("%d bytes audio heap left over\n", gAudioHeap.len - (gAudioHeap.cur - gAudioHeap.base));
    gMusicFading = 0;
    gMusicPopState = 0;
    gMusicStartVolume = gMusicVolume = 1.0f;
    gTuneStackEmpty = 1;
    gTuneStackTop = gTuneStack;
    amStartAudioMgr();
}

/* The sound of an effect id ("effect id %d out of range!") (0x802619D0). */
void snd_play_effect(u32 id) {
    if (id >= EFFECT_KIND_COUNT) {
        debug_printf("effect id %d out of range!\n", id);
    } else if (gEffectSounds[id] != 0) {
        snd_play(gSfxBank, gEffectSounds[id], NULL);
    }
}

/* On a mode switch: stops sounds, picks the next tune (0x80261A44). */
u8 music_tune_for_mode(u64 event) {
    u8 tune;
    u8 force;

    force = 0;
    tune = gMusicTune;
    if (event != 4) {
        snd_stop_all_looping();
    }
    snd_stop_all_repeating();
    snd_set_mode_slot_volumes(event);
    if (gGameMode & (MODE_BOOT_SCREEN1 | MODE_BOOT_SCREEN2 | MODE_LEVEL_STATUS | MODE_WORLD | MODE_PAK_CHECKED | MODE_LOAD_GAME | MODE_NEW_PLAYER | MODE_NAME_ENTRY | MODE_ERASE_CONFIRM | MODE_ERASE_PLAYER_CHOSEN | MODE_LOAD_GAME_RETURN | MODE_ERASE_MENU | MODE_TO_ERASE_MENU | MODE_NAME_ENTERED | MODE_PLAYER_CHOSEN | MODE_RESULTS | MODE_PAK_CHECK | MODE_VEHICLE_SELECT | MODE_PROMOTION | MODE_PRE_PROMOTION | MODE_PAK_FILES | MODE_UNUSED_33 | MODE_UNUSED_WINDOW_13 | MODE_PAK_MANAGER_CHECKED | MODE_PAK_FILE_CHOSEN | MODE_PAK_DELETE_CONFIRM | MODE_PAK_FILE_DELETED | MODE_UNUSED_41 | MODE_BEST_TIMES | MODE_PAK_MANAGER | MODE_ACADEMY_MESSAGE | MODE_ATTRACT_PICTURE | MODE_REGRADE_NOTICE | MODE_REGRADE_NOTICE_END | MODE_UNUSED_52 | MODE_BOOT_CHECK | MODE_ERASE_SAVE | MODE_BEST_TIMES_UNUSED | MODE_GUEST_PLAYER | MODE_NO_CONTROLLER | MODE_QUIT_GAME | MODE_LOAD_GAME_EXIT)) {
        snd_stop_slot(0);
        snd_stop_slot(5);
    }
    gMusicStartVolume = 1.0f;
    switch (event) {
        case 2:
            tune = 0x11;
            break;
        case 0x40:
            tune = 0xA;
            break;
        case 0x800:
        case 0x1000:
            tune = 3;
            break;
        case 0x10000:
            tune = 0x21;
            break;
        case 0x20000:
        case 0x40000:
        case 0x100000000:
        case 0x200000000:
        case 0x40000000000000:
            tune = 0x21;
            break;
        case 0x80000000:
            music_fade_to(0.0f);
            break;
        case 0x8000000:
            tune = 0xE;
            break;
        case 0x4000:
        case 0x80:
            if (lvl_planet(levelno) == 3) {
                tune = 0xC;
            } else {
                tune = 0x13;
            }
            break;
        case 0x40000000:
            tune = 0xF;
            break;
        case 0x2000:
            if (gLevelInfo[levelno][0] != 1) {
                music_fade_to(0.0f);
                break;
            }
        case 4:
            tune = gLevelTunes[levelno];
            force = 1;
            break;
        case 0x20000000:
            tune = 0x1D;
            break;
        case 0x100000000000:
            tune = gLevelTunes[levelno];
            force = 1;
            if (levelno == 0x26) {
                gMusicStartVolume = 0.7f;
            }
            break;
        case 0x4000000000000:
            music_fade_to(0.0f);
            break;
        case 0x10000000000000:
            tune = 0x28;
            break;
        case 0x800000000000:
            music_fade_to(0.0f);
            break;
        case 0x4000000000000000:
            break;
    }
    if (tune != gMusicTune || force) {
        music_fade_to(0.0f);
        return tune;
    }
    return 0;
}

/* The game mode's volume for sound slots 0 and 5 (0x80261E9C). */
void snd_set_mode_slot_volumes(u64 event) {
    switch (event) {
        case 2:
            snd_set_slot_volume(0, 0x5DC0);
            snd_set_slot_volume(5, 0x5DC0);
            break;
        case 0x20000000:
            snd_set_slot_volume(0, 0x61A8);
            break;
        case 0x40:
            snd_set_slot_volume(0, 0x4E20);
            snd_set_slot_volume(5, 0x4E20);
            break;
        case 0x800:
        case 0x1000:
            snd_set_slot_volume(0, 0x6D60);
            snd_set_slot_volume(5, 0x6D60);
            break;
        default:
            snd_set_slot_volume(0, 0x7FFF);
            snd_set_slot_volume(5, 0x7FFF);
            break;
    }
}

/* Starts a tune afresh (clears push/pop and the stack) (0x80261FB0). */
void music_start_tune(u8 tune) {
    gMusicPopState = 0;
    gMusicPushState = 0;
    gTuneStackTop = gTuneStack;
    gTuneStackEmpty = 1;
    music_play_tune(tune, gMusicStartVolume);
}

/* Clears the tune stack, starts a tune at a volume (0x80262008). */
void music_start_tune_at(u8 tune, f32 vol) {
    gTuneStackTop = gTuneStack;
    gTuneStackEmpty = 1;
    music_play_tune(tune, vol);
}

/* The current tune (0x80262050). */
u8 music_get_tune(void) {
    return gMusicTune;
}

/* A random sound id from group n (0x8026205C). */
s32 snd_random_id(s32 arg0) {
    s32 count;
    s32 j;

    count = 0;
    for (j = 0; j < 5 && gSoundGroups[arg0][j] != -1; j++, count++) {
    }
    return gSoundGroups[arg0][osGetCount() % count];
}
