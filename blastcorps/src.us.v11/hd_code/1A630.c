#include "common.h"
#include <ultra64.h>
#include "game/game.h"

void snd_free_state(void *arg0);

/* Rare's sound player, an early relative of GoldenEye's snd.c (itself
 * derived from libultra's sndplayer.c). Layouts follow GoldenEye's. */
typedef struct {
    s32 attackTime;
    s32 decayTime;
    s32 releaseTime;
    u8 attackVolume;
    u8 decayVolume;
} SndEnvelope;

typedef struct {
    u8 velocityMin;
    u8 velocityMax;
    u8 keyMin;
    u8 keyMax;
    u8 keyBase;
    s8 detune;
} SndKeyMap;

typedef struct {
    SndEnvelope * N64P envelope;
    SndKeyMap * N64P keyMap;
    void * N64P wavetable;
    u8 samplePan;
    u8 sampleVolume;
} SndSound;

typedef struct SndState {
    struct SndState * N64P next;
    struct SndState * N64P prev;
    SndSound * N64P sound;
    u8 voice[0x1C];
    f32 basePitch;
    f32 pitch;
    struct SndState * N64P handle; /* where the caller keeps it (snd_play's third argument) */
    s16 vol;
    u8 priority;
    s32 retries;
    u8 pan;
    u8 fxMix;
    u8 flags;
    u8 playingState;
} SndState;

typedef struct {
    u16 type;
    SndState * N64P state;
    union {
        s32 data;
        f32 pitch;
    } u;
    void * N64P ptr;
} SndEvent;

/* The port: SndEvent locals start zeroed.  alEvtqPostEvent copies the whole record into the event queue (the
 * audio heap), including the two pad bytes after type and the fields an event type doesn't set; uninitialised,
 * those are host stack bytes that vary from run to run (the N64's are whatever its stack held). */
#if defined(NON_MATCHING) && defined(PORT_HOST)
#define SND_EVT_ZERO = { 0 }
#else
#define SND_EVT_ZERO
#endif

typedef struct {
    s16 priority;
    s16 fxBus;
    u8 unityPitch;
} SndVoiceConfig;


/* Sound player globals. 1A630.c owns this 16-byte .data block (0x802E8CE0,
 * GoldenEye's D_800243E4): defining them here is what lets the paired
 * head/tail stores in snd_alloc_state share one %hi, as in the target. */
extern u8 gSndPlayer[];
ALLink gSndActive = { NULL, NULL }; /* active list: next = head, prev = tail */
void * N64P gSndFree = NULL;            /* free list */
void * N64P gSndp = N64_DPTR(gSndPlayer);      /* the sound player */
extern u16 * N64P gSndSlotVolumes;
s32 snd_voice_handler(void *node);
void snd_handle_event(void *sndp, SndEvent *event);

#define SNDP_FIELD(type, off) (*(type *) ((u8 *) gSndp + (off)))

/* The sound player: state and event pools, slot volumes (GoldenEye's sndNewPlayerInit) (0x8025EDF0).
 * GoldenEye: sndNewPlayerInit. */
void snd_init(SndConfig *c) {
    u32 i;
    u8 *ptr;
    SndEvent evt SND_EVT_ZERO;
    SndState *sState;

    SNDP_FIELD(s32, 0x48) = c->maxSounds;
    SNDP_FIELD(s32, 0x40) = 0;
    SNDP_FIELD(s32, 0x4C) = 33000;
    ptr = alHeapDBAlloc(0, 0, c->heap, 1, c->maxStates * sizeof(SndState));
    SNDP_FIELD(void * N64P, 0x44) = ptr;
    ptr = alHeapDBAlloc(0, 0, c->heap, 1, c->maxEvents * 28);
    alEvtqNew((ALEventQueue *) ((u8 *) gSndp + 0x14), (ALEventListItem *) ptr, c->maxEvents);
    gSndFree = SNDP_FIELD(void * N64P, 0x44);

    for (i = 1; i < c->maxStates; i++) {
        sState = SNDP_FIELD(SndState * N64P, 0x44);
        alLink((ALLink *) &sState[i], (ALLink *) (&sState[i] - 1));
    }

    gSndSlotVolumes = alHeapDBAlloc(0, 0, c->heap, sizeof(s16), c->slotCount);
    for (i = 0; i < c->slotCount; i++) {
        gSndSlotVolumes[i] = 0x7FFF;
    }

    SNDP_FIELD(void * N64P, 0x38) = alGlobals;
    SNDP_FIELD(void * N64P, 0x00) = NULL;
    SNDP_FIELD(void * N64P, 0x08) = snd_voice_handler;
    SNDP_FIELD(void * N64P, 0x04) = gSndp;
    alSynAddPlayer(SNDP_FIELD(void * N64P, 0x38), gSndp);

    evt.type = 0x20;
    alEvtqPostEvent((ALEventQueue *) ((u8 *) gSndp + 0x14), (ALEvent *) &evt, SNDP_FIELD(s32, 0x4C));
    SNDP_FIELD(s32, 0x50) = alEvtqNextEvent((ALEventQueue *) ((u8 *) gSndp + 0x14), (ALEvent *) ((u8 *) gSndp + 0x28));
}

/* The player's synth callback: runs due events (GoldenEye's sndPlayerVoiceHandler) (0x8025F044).
 * GoldenEye: sndPlayerVoiceHandler. */
s32 snd_voice_handler(void *node) {
    void *sndp;
    SndEvent evt SND_EVT_ZERO;

    sndp = node;
    do {
        switch (*(s16 *) ((u8 *) sndp + 0x28)) {
            case 0x20:
                evt.type = 0x20;
                alEvtqPostEvent((ALEventQueue *) ((u8 *) sndp + 0x14), (ALEvent *) &evt, *(s32 *) ((u8 *) sndp + 0x4C));
                break;
            default:
                snd_handle_event(sndp, (SndEvent *) ((u8 *) sndp + 0x28));
                break;
        }
        *(s32 *) ((u8 *) sndp + 0x50) = alEvtqNextEvent((ALEventQueue *) ((u8 *) sndp + 0x14), (ALEvent *) ((u8 *) sndp + 0x28));
    } while (*(s32 *) ((u8 *) sndp + 0x50) == 0);

    *(s32 *) ((u8 *) sndp + 0x54) += *(s32 *) ((u8 *) sndp + 0x50);
    return *(s32 *) ((u8 *) sndp + 0x50);
}

extern s16 gSndVoiceCount;
extern u16 * N64P gSndSlotVolumes;
void snd_dispose(void *arg0);
void snd_post_pitch_step(void *arg0);
void snd_remove_events(void *, void *, u16);
u16 snd_count_states(u16 *arg0, u16 *arg1);

#define SNDP_DRVR(sndp) (*(void * N64P *) ((u8 *) (sndp) + 0x38))
#define SNDP_EVTQ(sndp) ((void *) ((u8 *) (sndp) + 0x14))
#define SNDP_MAXSOUNDS(sndp) (*(s32 *) ((u8 *) (sndp) + 0x48))
#define SLOT_VOLUME(keyMap) (((s16 *) gSndSlotVolumes)[(keyMap)->keyMin & 0x3F])

/* One sound event: play, stop, pan, volume, pitch, fx, decay, end (GoldenEye's sndHandleEvent) (0x8025F0F0). */
void snd_handle_event(void *sndp, SndEvent *event) {
    SndVoiceConfig config;
    SndSound *sound;
    SndKeyMap *keyMap;
    void *voice;
    u8 pan;
    SndEvent spAC SND_EVT_ZERO;
    SndEvent nextStateEvent SND_EVT_ZERO;
    s32 delta;
    s32 fxMix;
    s32 volume;
    s32 panTmp;
    s32 limitReached;
    s32 isEventForSingleSound;
    s32 lastInSequence;
    s32 pad58;
    s32 isVoiceAllocated;
    SndState *soundState;
    SndState *nextState;

    voice = NULL;
    lastInSequence = TRUE;
    isVoiceAllocated = FALSE;
    nextState = NULL;

    do {
        if (nextState != NULL) {
            nextStateEvent.state = soundState;
            nextStateEvent.type = event->type;
            nextStateEvent.u.data = event->u.data;
            event = &nextStateEvent;
        }

        soundState = event->state;
        sound = soundState->sound;

        if (sound == NULL) {
            u16 numFree;
            u16 numAlloc;

            snd_count_states(&numFree, &numAlloc);
            debug_printf("Bad soundState: voices =%d, states free =%d, states busy =%d, type %d data %x\n",
                          gSndVoiceCount, numFree, numAlloc, event->type, event->u.data);
            return;
        }

        keyMap = sound->keyMap;
        nextState = soundState->next;

        switch (event->type) {
            case 1:
                if (soundState->playingState != 5 && soundState->playingState != 4) {
                    return;
                }
                if (soundState->playingState == 1) {
                    debug_printf("playing a playing sound\n");
                }

                config.fxBus = 0;
                config.priority = soundState->priority;
                config.unityPitch = 0;

                limitReached = gSndVoiceCount >= SNDP_MAXSOUNDS(sndp);

                if (!limitReached || (soundState->flags & 0x50)) {
                    isVoiceAllocated = alSynAllocVoice(SNDP_DRVR(sndp), (ALVoice *) soundState->voice, (ALVoiceConfig *) &config);
                }

                if (!isVoiceAllocated) {
                    if ((soundState->flags & 0x52) || soundState->retries > 0) {
                        soundState->playingState = 4;
                        soundState->retries--;
                        alEvtqPostEvent(SNDP_EVTQ(sndp), (ALEvent *) event, 33333);
                    } else if (limitReached) {
                        SndState *iterState = (SndState *) gSndActive.prev;

                        do {
                            if (!(iterState->flags & 0x52) && (iterState->flags & 4) &&
                                iterState->playingState != 3) {
                                SndEvent interruptEvent SND_EVT_ZERO;

                                limitReached = FALSE;
                                interruptEvent.type = 0x80;
                                interruptEvent.state = iterState;
                                iterState->playingState = 3;
                                alEvtqPostEvent(SNDP_EVTQ(sndp), (ALEvent *) &interruptEvent, 1000);
                                alSynSetVol(SNDP_DRVR(sndp), (ALVoice *) iterState->voice, 0, 1000);
                            }
                            iterState = iterState->prev;
                        } while (limitReached && iterState != NULL);

                        if (!limitReached) {
                            soundState->retries = 2;
                            alEvtqPostEvent(SNDP_EVTQ(sndp), (ALEvent *) event, 1001);
                        } else {
                            snd_dispose(soundState);
                        }
                    } else {
                        snd_dispose(soundState);
                    }
                    return;
                }

                soundState->flags |= 4;
                alSynStartVoice(SNDP_DRVR(sndp), (ALVoice *) soundState->voice, sound->wavetable);
                soundState->playingState = 1;
                gSndVoiceCount++;

                delta = sound->envelope->attackTime / soundState->pitch / soundState->basePitch;
                volume = MAX(0, SLOT_VOLUME(keyMap) *
                                    (sound->envelope->attackVolume * soundState->vol * sound->sampleVolume / 16129) /
                                    32767 - 1);
                alSynSetVol(SNDP_DRVR(sndp), (ALVoice *) soundState->voice, 0, 0);
                alSynSetVol(SNDP_DRVR(sndp), (ALVoice *) soundState->voice, volume, delta);

                panTmp = soundState->pan + sound->samplePan - 0x40;
                pan = MIN(MAX(panTmp, 0), 0x7F);
                alSynSetPan(SNDP_DRVR(sndp), (ALVoice *) soundState->voice, pan);

                alSynSetPitch(SNDP_DRVR(sndp), (ALVoice *) soundState->voice, soundState->pitch * soundState->basePitch);

                fxMix = (soundState->fxMix + (keyMap->keyMax & 0xF)) * 8;
                fxMix = MIN(127, MAX(0, fxMix));
                alSynSetFXMix(SNDP_DRVR(sndp), (ALVoice *) soundState->voice, fxMix);

                spAC.type = 0x40;
                spAC.state = soundState;
                delta = sound->envelope->attackTime / soundState->pitch / soundState->basePitch;
                alEvtqPostEvent(SNDP_EVTQ(sndp), (ALEvent *) &spAC, delta);
                break;

            case 2:
            case 0x400:
            case 0x1000:
                if (event->type != 0x1000 || (soundState->flags & 2)) {
                    switch (soundState->playingState) {
                        case 1:
                            snd_remove_events(SNDP_EVTQ(sndp), soundState, 0x40);
                            delta = sound->envelope->releaseTime / soundState->basePitch / soundState->pitch;
                            alSynSetVol(SNDP_DRVR(sndp), (ALVoice *) soundState->voice, 0, delta);
                            if (delta != 0) {
                                spAC.type = 0x80;
                                spAC.state = soundState;
                                alEvtqPostEvent(SNDP_EVTQ(sndp), (ALEvent *) &spAC, delta);
                                soundState->playingState = 2;
                            } else {
                                snd_dispose(soundState);
                            }
                            break;
                        case 4:
                        case 5:
                            snd_dispose(soundState);
                            break;
                    }
                    if (event->type == 2) {
                        event->type = 0x1000;
                    }
                }
                break;

            case 4:
                soundState->pan = event->u.data;
                if (soundState->playingState == 1) {
                    panTmp = soundState->pan + sound->samplePan - 0x40;
                    pan = MIN(MAX(panTmp, 0), 0x7F);
                    alSynSetPan(SNDP_DRVR(sndp), (ALVoice *) soundState->voice, pan);
                }
                break;

            case 0x10:
                soundState->pitch = event->u.pitch;
                if (soundState->playingState == 1) {
                    alSynSetPitch(SNDP_DRVR(sndp), (ALVoice *) soundState->voice, soundState->pitch * soundState->basePitch);
                    if (soundState->flags & 0x20) {
                        snd_post_pitch_step(soundState);
                    }
                }
                break;

            case 0x100:
                soundState->fxMix = event->u.data;
                if (soundState->playingState == 1) {
                    fxMix = (soundState->fxMix + (keyMap->keyMax & 0xF)) * 8;
                    fxMix = MIN(127, MAX(0, fxMix));
                    alSynSetFXMix(SNDP_DRVR(sndp), (ALVoice *) soundState->voice, fxMix);
                }
                break;

            case 8:
                soundState->vol = event->u.data;
                if (soundState->playingState == 1) {
                    volume = MAX(0, SLOT_VOLUME(keyMap) *
                                        (sound->envelope->decayVolume * soundState->vol * sound->sampleVolume / 16129) /
                                        32767 - 1);
                    alSynSetVol(SNDP_DRVR(sndp), (ALVoice *) soundState->voice, volume, 1000);
                }
                break;

            case 0x800:
                if (soundState->playingState == 1) {
                    delta = sound->envelope->releaseTime / soundState->basePitch / soundState->pitch;
                    volume = MAX(0, SLOT_VOLUME(keyMap) *
                                        (sound->envelope->decayVolume * soundState->vol * sound->sampleVolume / 16129) /
                                        32767 - 1);
                    alSynSetVol(SNDP_DRVR(sndp), (ALVoice *) soundState->voice, volume, delta);
                }
                break;

            case 0x40:
                if (!(soundState->flags & 2)) {
                    volume = MAX(0, SLOT_VOLUME(keyMap) *
                                        (sound->envelope->decayVolume * soundState->vol * sound->sampleVolume / 16129) /
                                        32767 - 1);
                    delta = sound->envelope->decayTime / soundState->basePitch / soundState->pitch;
                    alSynSetVol(SNDP_DRVR(sndp), (ALVoice *) soundState->voice, volume, delta);

                    spAC.type = 2;
                    spAC.state = soundState;
                    alEvtqPostEvent(SNDP_EVTQ(sndp), (ALEvent *) &spAC, delta);

                    if (soundState->flags & 0x20) {
                        snd_post_pitch_step(soundState);
                    }
                }
                break;

            case 0x80:
                snd_dispose(soundState);
                break;

            case 0x200:
                if (soundState->flags & 0x10) {
                    void *newState;

                    newState = snd_play(event->ptr, event->u.data, soundState->handle);
                    snd_modify(newState, 8, soundState->vol);
                    snd_modify(newState, 4, soundState->pan);
                    snd_modify(newState, 0x100, soundState->fxMix);
                    snd_modify(newState, 0x10, *(s32 *) &soundState->pitch);
                }
                break;

            default:
                debug_printf("Nonsense sndp event\n");
                break;
        }

        isEventForSingleSound = event->type & (1 | 0x10 | 0x40 | 0x80 | 0x200);
        soundState = nextState;

        if (soundState != NULL && !isEventForSingleSound) {
            lastInSequence = soundState->flags & 1;
        }
    } while (!lastInSequence && soundState != NULL && !isEventForSingleSound);
}

extern void * N64P gSndp;

void snd_remove_events(void *, void *, u16);

/* Stops and frees a sound's voice and state, drops its events (GoldenEye's sndDisposeSound) (0x8026005C). */
void snd_dispose(void *arg0) {
    if (*((u8 *) arg0 + 0x3e) & 4) {
        alSynStopVoice((ALSynth *) N64_IPTR((*(s32 *) ((u8 *) gSndp + 0x38))), (ALVoice *) ((u8 *) arg0 + 0xc));
        alSynFreeVoice((ALSynth *) N64_IPTR((*(s32 *) ((u8 *) gSndp + 0x38))), (ALVoice *) ((u8 *) arg0 + 0xc));
    }
    snd_free_state(arg0);
    snd_remove_events((u8 *) gSndp + 0x14, arg0, 0xffff);
}

f32 alCents2Ratio(s32);

/* Posts a pitch event 1/30 s ahead (GoldenEye's sndCreatePitchEvent) (0x802600D8). */
void snd_post_pitch_step(void *arg0) {
    SndEvent evt SND_EVT_ZERO;
    f32 pitch;

    pitch = (f32) (alCents2Ratio(*(s8 *) ((u8 *) ((SndState *) arg0)->sound->keyMap + 5)) *
                   (f32) ((SndState *) arg0)->pitch);
    evt.state = arg0;
    evt.type = 0x10;
    evt.u.data = *(s32 *) &pitch;
    alEvtqPostEvent((ALEventQueue *) ((u8 *) gSndp + 0x14), (ALEvent *) &evt, 33333);
}


/* Unqueues a sound's events of a type mask (GoldenEye's sndRemoveEvents) (0x80260148). */
void snd_remove_events(void *arg0, void *arg1, u16 arg2) {
    void *list;
    void *next;
    void *entry2;
    void *dup;
    void *addr;
    s32 savedState;

    savedState = osSetIntMask(1);
    list = *(void * N64P *) ((u8 *) arg0 + 8);
    while (list != NULL) {
        next = *(void * N64P *) list;
        addr = (u8 *) list + 0xc;
        entry2 = list;
        dup = next;

        if (*(void * N64P *) ((u8 *) list + 0x10) == arg1) {
            if ((*(u16 *) ((u8 *) list + 0xc) & arg2) != 0) {
                if (next != NULL) {
                    *(s32 *) ((u8 *) next + 8) += *(s32 *) ((u8 *) list + 8);
                }
                alUnlink(list);
                alLink(list, arg0);
            }
        }
        list = next;
    }

    osSetIntMask(savedState);
}

extern void * N64P gSndFree;

/* Free and busy sound-state counts (GoldenEye's sndCountAllocList) (0x80260210). */
u16 snd_count_states(u16 *arg0, u16 *arg1) {
    s32 savedState;
    u16 count1;
    u16 count2;
    u16 count3;
    void *list1;
    void *list2;
    void *list3;

    savedState = osSetIntMask(1);

    list1 = gSndActive.next;
    list2 = gSndFree;
    list3 = gSndActive.prev;

    count1 = 0;
    if (list1 != NULL) {
        do {
            count1 = count1 + 1;
        } while ((list1 = *(void * N64P *) list1) != NULL);
    }

    count2 = 0;
    if (list2 != NULL) {
        do {
            count2 = count2 + 1;
        } while ((list2 = *(void * N64P *) list2) != NULL);
    }

    count3 = 0;
    if (list3 != NULL) {
        do {
            count3 = count3 + 1;
        } while ((list3 = *(void * N64P *) ((u8 *) list3 + 4)) != NULL);
    }

    *arg0 = count2;
    *arg1 = count1;

    osSetIntMask(savedState);
    return count3;
}

/* A free SndState onto the active list (GoldenEye's sndSetupSound) (0x80260300).
 * GoldenEye: sndSetupSound. */
SndState *snd_alloc_state(void *bank, SndSound *sound) {
    SndState *state;
    SndKeyMap *keymap = sound->keyMap;
    s32 decayTimeFlag;
    OSIntMask mask;

    state = (SndState *) gSndFree;

    if (state != NULL) {
        mask = osSetIntMask(OS_IM_NONE);
        gSndFree = state->next;
        alUnlink((ALLink *) state);
        if (gSndActive.next != NULL) {
            state->next = (SndState *) gSndActive.next;
            state->prev = NULL;
            gSndActive.next->prev = (ALLink *) state;
            gSndActive.next = (ALLink *) state;
        } else {
            state->next = state->prev = NULL;
            gSndActive.next = (ALLink *) state;
            gSndActive.prev = (ALLink *) state;
        }
        decayTimeFlag = (sound->envelope->decayTime == -1);
        state->sound = sound;
        state->priority = decayTimeFlag + 0x40;
        state->playingState = 5;
        state->pitch = 1.0f;
        state->retries = 2;
        state->flags = keymap->keyMax & 0xF0;
        state->handle = NULL;
        if (state->flags & 0x20) {
            state->basePitch = alCents2Ratio(keymap->keyBase * 100 - 6000);
        } else {
            state->basePitch = alCents2Ratio(keymap->keyBase * 100 + keymap->detune - 6000);
        }
        if (decayTimeFlag) {
            state->flags |= 2;
        }
        state->fxMix = 0;
        state->pan = 0x40;
        state->vol = 0x7FFF;
        osSetIntMask(mask);
    }
    return state;
}

extern s16 gSndVoiceCount;

/* Back to the free list; clears the caller's handle (GoldenEye's sndUnlinkClearSound) (0x802604FC). */
void snd_free_state(void *arg0) {
    if (gSndActive.next == arg0) {
        gSndActive.next = *(void * N64P *) arg0;
    }
    if (gSndActive.prev == arg0) {
        gSndActive.prev = *(void * N64P *) ((u8 *) arg0 + 4);
    }
    alUnlink(arg0);

    if (gSndFree != NULL) {
        *(void * N64P *) arg0 = gSndFree;
        *(s32 *) ((u8 *) arg0 + 4) = 0;
        *(s32 *) ((u8 *) gSndFree + 4) = (s32) arg0;
        gSndFree = arg0;
    } else {
        *(s32 *) ((u8 *) arg0 + 4) = 0;
        *(void * N64P *) arg0 = *(void * N64P *) ((u8 *) arg0 + 4);
        gSndFree = arg0;
    }

    if (*(u8 *) ((u8 *) arg0 + 0x3e) & 4) {
        gSndVoiceCount--;
    }
    *(u8 *) ((u8 *) arg0 + 0x3f) = 0;
    if (*(void * N64P *) ((u8 *) arg0 + 0x30) != NULL) {
        if (**(void * N64P * N64P *) ((u8 *) arg0 + 0x30) == arg0) {
            **(void * N64P * N64P *) ((u8 *) arg0 + 0x30) = NULL;
        }
        *(void * N64P *) ((u8 *) arg0 + 0x30) = NULL;
    }
}

/* A sound's voice priority (NULL-safe) (0x80260618). */
void snd_set_priority(void *arg0, u8 arg1) {
    if (arg0 != NULL) {
        *(s8 *)((u8 *) arg0 + 0x36) = (s8)(s16)(u8) arg1;
    }
}

/* A sound's playingState, 0 when NULL or finished (0x80260634). */
u8 snd_get_state(void *arg0) {
    if (arg0 != NULL) {
        return *(u8 *) ((u8 *) arg0 + 0x3f);
    }
    return 0;
}

extern void * N64P gSndp;
SndState *snd_alloc_state(void *, SndSound *);

/* The functions below post sound events built in separate locals (eventCode,
 * eventParam, ...) that IDO lays out as one SndEvent: type, then state, data
 * and ptr in the following stack slots. Other compilers place locals freely,
 * so the NON_MATCHING build passes a real SndEvent. Fields the original takes
 * from unused stack slots are 0 here. */
#ifdef NON_MATCHING
static ALEvent *sndEvt(SndEvent *e, u16 type, void *state, s32 data, void *ptr) {
#ifdef PORT_HOST
    SndEvent z SND_EVT_ZERO;
    *e = z;   /* (the pad bytes after type too) */
#endif
    e->type = type;
    e->state = state;
    e->u.data = data;
    e->ptr = ptr;
    return (ALEvent *) e;
}
#define SND_EVT_DECL SndEvent sndEvtTmp;
#define SND_EVT(type_, state_, data_, ptr_) sndEvt(&sndEvtTmp, (type_), (void *) N64_IPTR(state_), (s32) (data_), (void *) N64_IPTR(ptr_))
#else
#define SND_EVT_DECL
#define SND_EVT(type_, state_, data_, ptr_) ((ALEvent *) &(type_))
#endif

/* Plays sound id of a bank and its chain; *handle = the state (GoldenEye's sndPlaySfx) (0x80260650). */
void *snd_play(void *arg0, s16 arg1, void *arg2) {
    SND_EVT_DECL
    void *node;
    void *result;
    void *entry4dead;
    void *entry;
    s16 flag1;
    s32 adjusted;
    s32 scaled;
    s32 totalSomething;
    s32 pad3;
    s32 pad4;
    s32 eventParam;
    s16 pad;
    s16 eventCode;
    s32 extra2;
    s32 extra1;
    s32 eventParam2;
    s16 pad2;
    s16 eventCode2;

    result = NULL;
    flag1 = 0;
    totalSomething = 0;
    if (arg1 == 0 || (((s32) gFramesInMode) == 0 && arg1 == 0xc)) {
        goto retNull;
    }
    if (arg1 == 0x14 || arg1 == 0x15) {
        if (levelno == 0x26 || levelno == 0x31) {
            goto retNull;
        }
    }
    goto mainLogic;

retNull:
    return NULL;

mainLogic:
    do {
        entry = *(void * N64P *) ((u8 *) (*(void * N64P *) ((u8 *) arg0 + 0xc))
            + (arg1 << 2) + 0xc);
        node = snd_alloc_state(arg0, entry);
        if (node != NULL) {
            *(void * N64P *) ((u8 *) gSndp + 0x40) = node;
            eventCode = 1;
            eventParam = (s32) node;
            scaled = (s32) *(u8 *) ((u8 *) (*(void * N64P *) ((u8 *) entry + 4)) + 1)
                * 33333;

            if (*(u8 *) ((u8 *) node + 0x3e) & 0x10) {
                *(u8 *) ((u8 *) node + 0x3e) &= ~0x10;
                alEvtqPostEvent((ALEventQueue *) ((u8 *) gSndp + 0x14), SND_EVT(eventCode, eventParam, 0, 0),
                    totalSomething + 1);
                adjusted = scaled + 1;
                flag1 = arg1;
            } else {
                alEvtqPostEvent((ALEventQueue *) ((u8 *) gSndp + 0x14), SND_EVT(eventCode, eventParam, 0, 0),
                    scaled + 1);
            }
            result = node;
        } else {
            debug_printf("Sound state allocate failed - sndId %d\n", arg1);
        }

        totalSomething = totalSomething + scaled;
        entry4dead = *(void * N64P *) ((u8 *) entry + 4);
        arg1 = ((*(u8 *) ((u8 *) entry4dead + 2) & 0xc0) << 2)
            + *(u8 *) entry4dead;
    } while (arg1 != 0 && node != NULL);

    if (result != NULL) {
        *(u8 *) ((u8 *) result + 0x3e) |= 1;
        *(s32 *) ((u8 *) result + 0x30) = (s32) arg2;
        if (flag1 != 0) {
            *(u8 *) ((u8 *) result + 0x3e) |= 0x10;
            eventCode2 = 0x200;
            eventParam2 = (s32) result;
            extra1 = flag1;
            extra2 = (s32) arg0;
            alEvtqPostEvent((ALEventQueue *) ((u8 *) gSndp + 0x14),
                            SND_EVT(eventCode2, eventParam2, extra1, extra2), adjusted);
        }
    }

    if (arg2 != NULL) {
        *(void * N64P *) arg2 = result;
    }
    return result;
}

extern void * N64P gSndp;


/* Stops a sound and cancels its respawn (0x802608C8).
 * eventTail/eventHead are dead stack space never read or written here - the
 * real source likely shares one ~0x10-byte event-record local across sites
 * like this one and only fills the leading eventCode/eventParam fields,
 * leaving the rest as reserved/unused padding (same shape as a plain array
 * would need, but writing it as one keeps the 0x24/0x20-offset slots from
 * ever materializing their own address, which doesn't match target). */
void snd_stop(void *arg0) {
    SND_EVT_DECL
    s32 eventTail;
    s32 eventHead;
    s32 eventParam;
    s16 pad;
    s16 eventCode;

    eventCode = 0x400;
    eventParam = (s32) arg0;

    if (arg0 != NULL) {
        *((u8 *) arg0 + 0x3e) &= ~0x10;
        alEvtqPostEvent((ALEventQueue *) ((u8 *) gSndp + 0x14), SND_EVT(eventCode, eventParam, 0, 0), 0);
    } else {
        debug_printf("WARNING: Attempt to stop NULL sound aborted\n");
    }
}


/* Stops the active sounds whose flags include mask (0x80260934). */
void snd_stop_matching(u8 arg0) {
    SND_EVT_DECL
    s32 savedState;
    s32 unused2;
    s32 unused1;
    s32 eventParam;
    s16 pad;
    s16 eventCode;
    void *entry;

    savedState = osSetIntMask(1);
    entry = gSndActive.next;
    if (entry != NULL) {
        do {
            eventCode = 0x400;
            eventParam = (s32) entry;
            if ((*((u8 *) entry + 0x3e) & arg0) == arg0) {
                *((u8 *) entry + 0x3e) &= ~0x10;
                alEvtqPostEvent((ALEventQueue *) ((u8 *) gSndp + 0x14), SND_EVT(eventCode, eventParam, 0, 0),
                                0);
            }
            entry = *(void * N64P *) entry;
        } while (entry != NULL);
    }
    osSetIntMask(savedState);
}

/* Stops every sound (0x802609D0). */
void snd_stop_all(void) {
    snd_stop_matching(1);
}

/* Stops every repeating sound (0x802609F0). */
void snd_stop_all_repeating(void) {
    snd_stop_matching(0x11);
}

/* Stops every sustained sound (0x80260A10). */
void snd_stop_all_looping(void) {
    snd_stop_matching(3);
}

/* Stops the active sounds of a volume slot (0x80260A30). */
void snd_stop_slot(u8 arg0) {
    s32 savedState;
    void *entry;
    s32 count;

    savedState = osSetIntMask(1);
    entry = gSndActive.next;
    count = 0;
    if (entry != NULL) {
        do {
            if ((*(u8 *) ((u8 *) (*(void * N64P *) ((u8 *) (*(void * N64P *) ((u8 *) entry + 8)) + 4)) + 2) & 0x3f) == arg0) {
                snd_stop(entry);
            }
            count = count + 1;
        } while ((entry = *(void * N64P *) entry) != NULL);
    }
    osSetIntMask(savedState);
}


/* Posts a parameter change: 4 pan, 8 volume, 0x10 pitch, 0x100 fx (0x80260AB8). */
void snd_modify(void *arg0, s16 arg1, s32 arg2) {
    SND_EVT_DECL
    s32 unused;
    s32 eventExtra;
    s32 eventParam;
    s16 pad;
    s16 eventCode;

    eventCode = arg1;
    eventParam = (s32) arg0;
    eventExtra = arg2;

    if (arg0 != NULL) {
        alEvtqPostEvent((ALEventQueue *) ((u8 *) gSndp + 0x14), SND_EVT(eventCode, eventParam, eventExtra, 0),
                        0);
    } else {
        debug_printf("WARNING: Attempt to modify NULL sound aborted\n");
    }
}

extern u16 * N64P gSndSlotVolumes;

/* The volume of sound slot n (0x80260B24). */
u16 snd_get_slot_volume(u8 arg0) {
    return gSndSlotVolumes[arg0];
}

extern void * N64P gSndp;

/* Sets slot n's volume and re-applies it to its sounds (0x80260B40). */
void snd_set_slot_volume(u8 arg0, u16 arg1) {
    SND_EVT_DECL
    s32 savedState;
    void *entry;
    s32 count;
    s32 eventTail;
    s32 eventHead;
    s32 eventParam;
    s16 pad;
    s16 eventCode;

    savedState = osSetIntMask(1);
    entry = gSndActive.next;
    gSndSlotVolumes[arg0] = arg1;

    count = 0;
    while (entry != NULL) {
        if ((*(u8 *) ((u8 *) (*(void * N64P *) ((u8 *) (*(void * N64P *) ((u8 *) entry + 8)) + 4)) + 2) & 0x3f) == arg0) {
            eventCode = 0x800;
            eventParam = (s32) entry;
            alEvtqPostEvent((ALEventQueue *) ((u8 *) gSndp + 0x14), SND_EVT(eventCode, eventParam, 0, 0), 0);
        }
        count = count + 1;
        entry = *(void * N64P *) entry;
    }

    osSetIntMask(savedState);
}
