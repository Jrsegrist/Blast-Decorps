#include "common.h"
#include <ultra64.h>
#include "game/game.h"

/* audio.c: Rare's copy of the SDK demos' audio manager (audiomgr.c). The
 * thread renders one audio frame per retrace message from the scheduler and
 * streams sample data through a small cache of 0x200-byte DMA buffers.
 * Names follow the demo's (amCreateAudioMgr, audFrameCt, __amMain,
 * __amHandleFrameMsg, __amHandleDoneMsg, __amDMA, __amDmaNew,
 * __clearAudioDMA, ...). */

/* (BcScTask, BcSched: sched.c's, game/types.h) */
typedef struct {
    /* 0x00 */ s16 * N64P data;
    /* 0x04 */ s16 frameSamples;
    /* 0x08 */ BcScTask task;
} AudioInfo;

typedef struct {
    /* 0x000 */ Acmd * N64P ACMDList[2];
    /* 0x008 */ AudioInfo * N64P audioInfo[3];
    /* 0x018 */ OSThread thread;
    /* 0x1C8 */ OSMesgQueue audioFrameMsgQ;
    /* 0x1E0 */ OSMesg audioFrameMsgBuf[8];
    /* 0x200 */ OSMesgQueue audioReplyMsgQ;
    /* 0x218 */ OSMesg audioReplyMsgBuf[8];
    /* 0x238 */ ALGlobals g;
} AMAudioMgr;

typedef struct {
    /* 0x00 */ ALLink node;
    /* 0x08 */ u32 startAddr;
    /* 0x0C */ u32 lastFrame;
    /* 0x10 */ u8 * N64P ptr;
} AMDMABuffer;

typedef struct {
    /* 0x00 */ u8 initialized;
    /* 0x04 */ AMDMABuffer * N64P firstUsed;
    /* 0x08 */ AMDMABuffer * N64P firstFree;
} AMDMAState;

extern AMAudioMgr __am;
extern AMDMAState gAudDmaState;
/* .data (0x802F3AF0); the reverb template in amCreateAudioMgr and
 * __amHandleDoneMsg's firstTime follow in source order */
u32 audFrameCt = 0; /* audFrameCt */
u32 nextDMA = 0; /* nextDMA */
s32 curAcmdList = 0; /* curAcmdList */
extern s32 gAudDmaMaxSearch; /* deepest DMA list search */
extern OSMesgQueue audDMAMessageQ; /* audDMAMessageQ */
/* the old SDK's OSIoMesg, without piHandle */
typedef struct {
    OSIoMesgHdr hdr;
    void * N64P dramAddr;
    u32 devAddr;
    u32 size;
} OldIoMesg;

extern OldIoMesg audDMAIOMesgBuf[]; /* audDMAIOMesgBuf */
extern u32 minFrameSize; /* minFrameSize */
extern u32 frameSize; /* frameSize */
extern u32 maxFrameSize; /* maxFrameSize */
extern s32 gAudCmdLen; /* cmdLen */
extern BcScClient gAudioSchedClient; /* the audio thread's scheduler client */
extern u8 gAspMainData[];
/* .bss (0x80368050, placed by hd_code_bss.us.v11.ld); the first two words
 * are unreferenced placeholders */
s32 D_80368050;
s32 D_80368054;
u64 gAudioPrevFrameStart; /* last audio frame start */
u64 gAudioFrameStart; /* audio frame start */
u64 gAudioFrameEnd; /* audio frame end */

void __clearAudioDMA(void);
ALDMAproc __amDmaNew(AMDMAState **state);
void __amMain(void *arg);
extern s32 osViClock;
extern OSMesg audDMAMessageBuf[];
extern u64 gAudioThreadStack[]; /* audio thread stack */
extern AMDMABuffer dmaBuffs[72];
s32 __amDMA(s32 addr, s32 len, void *state);
/* __amHandleFrameMsg */
void __amHandleFrameMsg(AudioInfo *info, AudioInfo *lastInfo);
void __amHandleDoneMsg(AudioInfo *info);

#define AUDIO_ASSERT(EX, line) \
    if (!(EX)) debug_printf("\n\a --- ASSERTION FAULT - %s - %s, line %d\n\n", #EX, "audio.c", line)

#define MAX_RSP_CMDS 0xABE

/* The audio manager (the SDK demo's amCreateAudioMgr) (0x802676A0).
 * amCreateAudioMgr */
void amCreateAudioMgr(ALSynConfig *c, OSPri pri) {
    s32 i;
    f32 fsize;

    c->dmaproc = (ALDMANew) __amDmaNew;
    if (osTvType != 1) {
        osViClock = 0x02E6025C;
    }
    c->outputRate = osAiSetFrequency(22050);
    fsize = (f32) c->outputRate * 2.0f / 60.0f;
    frameSize = (s32) fsize;
    if (frameSize < fsize) {
        frameSize++;
    }
    if (frameSize & 0xF) {
        frameSize = (frameSize & ~0xF) + 0x10;
    }
    minFrameSize = frameSize - 16;
    maxFrameSize = frameSize + 0x35;
    if (c->fxType == 6) {
        s32 pad = 0;
        s32 params[66] = {
            8, 6800,
            0, 160, 9830, -9830, 0, 0, 0, 0,
            160, 320, 9830, -9830, 11140, 0, 0, 9472,
            800, 2560, 16384, -16384, 4587, 0, 0, 12288,
            960, 1920, 8192, -8192, 0, 0, 0, 0,
            3200, 5600, 16384, -16384, 4587, 0, 0, 13568,
            3360, 4800, 8192, -8192, 0, 0, 0, 0,
            4800, 5440, 8192, -8192, 0, 0, 0, 0,
            0, 5920, 13000, -13000, 0, 380, 10, 17664,
        };
        c->params = params;
        alInit(&__am.g, c);
    } else {
        alInit(&__am.g, c);
    }
    dmaBuffs[0].node.prev = NULL;
    dmaBuffs[0].node.next = NULL;
    for (i = 0; i < 71; i++) {
        alLink(&dmaBuffs[i + 1].node, &dmaBuffs[i].node);
        dmaBuffs[i].ptr = alHeapAlloc(c->heap, 1, 0x200);
    }
    dmaBuffs[i].ptr = alHeapAlloc(c->heap, 1, 0x200);
    for (i = 0; i < 2; i++) {
        __am.ACMDList[i] = alHeapAlloc(c->heap, 1, 0x55F0);
    }
    for (i = 0; i < 3; i++) {
        __am.audioInfo[i] = alHeapAlloc(c->heap, 1, sizeof(AudioInfo));
        __am.audioInfo[i]->data = alHeapAlloc(c->heap, 1, maxFrameSize * 4);
    }
    osCreateMesgQueue(&__am.audioReplyMsgQ, __am.audioReplyMsgBuf, 8);
    osCreateMesgQueue(&__am.audioFrameMsgQ, __am.audioFrameMsgBuf, 8);
    osCreateMesgQueue(&audDMAMessageQ, audDMAMessageBuf, 0x48);
    osCreateThread(&__am.thread, 4, __amMain, NULL, &gAudioThreadStack[0x400], pri);
}

/* Starts the audio thread (0x80267A74).
 * amStartAudioMgr */
void amStartAudioMgr(void) {
    osStartThread(&__am.thread);
}

/* The audio thread (0x80267A9C).
 * __amMain */
void __amMain(void *arg) {
    s32 done;
    s32 msg;
    AudioInfo *lastInfo;
    s32 firstTime;

    done = 0;
    lastInfo = NULL;
    firstTime = 1;
    sched_add_client(&gScheduler, &gAudioSchedClient, &__am.audioFrameMsgQ, 2, 2);
    osSendMesg(&__am.audioFrameMsgQ, (OSMesg) 5, OS_MESG_NOBLOCK);
    while (!done) {
        osRecvMesg(&__am.audioFrameMsgQ, (OSMesg *) &msg, OS_MESG_BLOCK);
        switch (msg) {
            case 4:
                break;
            case 5:
                /* audio tasks still queued: ask the scheduler to yield the graphics task (0x29E) */
                if (gScheduler.audioListHead) {
                    osSendMesg(&gScheduler.interruptQ, (OSMesg) 0x29E, OS_MESG_BLOCK);
                }
                gAudioFrameStart = osGetTime();
                __amHandleFrameMsg(__am.audioInfo[audFrameCt % 3], lastInfo);
                gAudioFrameEnd = osGetTime();
                gAudioPrevFrameStart = gAudioFrameStart;
                if (!firstTime) {
                    osRecvMesg(&__am.audioReplyMsgQ, (OSMesg *) &lastInfo, OS_MESG_BLOCK);
                    __amHandleDoneMsg(lastInfo);
                }
                firstTime = 0;
                if (gMusicFading) {
                    music_update_fade();
                }
                if (gMusicPopState) {
                    music_update_pop();
                }
                if (gMusicPushState) {
                    music_update_push();
                }
                if (!gTuneStackEmpty) {
                    music_update_auto_pop();
                }
                if (gMusicSavedTempo) {
                    music_restore_tempo();
                }
                break;
            case 10:
                done = 1;
                break;
            case 6:
                debug_printf("No samples left\n");
                break;
        }
    }
    alClose(&__am.g);
}

#define osScGetCmdQ sched_get_cmd_queue
#define sc gScheduler
#define cmdLen gAudCmdLen

/* One audio frame (0x80267CDC). */
void __amHandleFrameMsg(AudioInfo *info, AudioInfo *lastInfo) {
    s16 *audioPtr;
    Acmd *cmdp;
    s32 samplesLeft;
    BcScTask *t;

    samplesLeft = 0;
    __clearAudioDMA();
    audioPtr = (s16 *) osVirtualToPhysical(info->data);
    if (lastInfo) {
        osAiSetNextBuffer(lastInfo->data, lastInfo->frameSamples << 2);
    }
    samplesLeft = osAiGetLength() >> 2;
    info->frameSamples = (frameSize - samplesLeft + 0x35) & ~0xF;
    if (info->frameSamples < minFrameSize) {
        info->frameSamples = minFrameSize;
    }
    cmdp = alAudioFrame(__am.ACMDList[curAcmdList], &gAudCmdLen, audioPtr, info->frameSamples);
    AUDIO_ASSERT(cmdLen <= MAX_RSP_CMDS, 336);
    t = &info->task;
    t->next = NULL;
    t->msgQ = &__am.audioReplyMsgQ;
    t->msg = (OSMesg) info;
    t->flags = 1;
    t->client = &gAudioSchedClient;
    t->list.t.data_ptr = (u64 *) __am.ACMDList[curAcmdList];
    t->list.t.data_size = (cmdp - __am.ACMDList[curAcmdList]) * sizeof(Acmd);
    t->list.t.type = M_AUDTASK;
    t->list.t.ucode_boot = (u64 *) gRspBootText;
    t->list.t.ucode_boot_size = gAspMainText - gRspBootText;
    t->list.t.flags = 0;
    t->list.t.ucode = (u64 *) gAspMainText;
    t->list.t.ucode_data = (u64 *) gAspMainData;
    t->list.t.ucode_data_size = 0x800;
    t->list.t.yield_data_ptr = NULL;
    t->list.t.yield_data_size = 0;
    osWritebackDCache(t, sizeof(BcScTask));
    osWritebackDCache(t->list.t.data_ptr, t->list.t.data_size);
    AUDIO_ASSERT(osSendMesg(osScGetCmdQ(&sc), (OSMesg) t, OS_MESG_NOBLOCK)!=-1, 361);
    curAcmdList ^= 1;
}

#undef sc
#undef osScGetCmdQ

/* Audio task done (0x80267F88).
 * __amHandleDoneMsg */
void __amHandleDoneMsg(AudioInfo *info) {
    static s32 D_802F3C04 N64_KEEP = 1; /* firstTime (never cleared: N64_KEEP) */
    u32 samplesLeft;

    samplesLeft = osAiGetLength() >> 2;
    if (samplesLeft == 0 && !D_802F3C04) {
        debug_printf("audio: ai out of samples\n");
        D_802F3C04 = 0;
    }
}

/* The synthesizer's DMA callback (0x200-byte buffer cache) (0x80267FE0).
 * __amDma */
s32 __amDMA(s32 addr, s32 len, void *state) {
    void *foundBuffer;
    s32 delta;
    s32 addrEnd;
    s32 buffEnd;
    AMDMABuffer *dmaPtr;
    AMDMABuffer *lastDmaPtr;
    AMDMABuffer *prev;
    s32 count;

    dmaPtr = gAudDmaState.firstUsed;
    count = 0;
    lastDmaPtr = NULL;
    addrEnd = addr + len;
    delta = addr & 1;
    while (dmaPtr) {
        prev = dmaPtr;
        buffEnd = dmaPtr->startAddr + 0x200;
        if (dmaPtr->startAddr > (u32) addr) {
            break;
        } else if (addrEnd <= buffEnd) {
            dmaPtr->lastFrame = audFrameCt;
            foundBuffer = dmaPtr->ptr + addr - dmaPtr->startAddr;
            return osVirtualToPhysical(foundBuffer);
        }
        lastDmaPtr = dmaPtr;
        dmaPtr = (AMDMABuffer *) dmaPtr->node.next;
        count++;
    }
    if (count > gAudDmaMaxSearch) {
        gAudDmaMaxSearch = count;
    }
    dmaPtr = gAudDmaState.firstFree;
    if (!dmaPtr) {
        debug_printf("OH DEAR - No audio DMA buffers left\n");
    }
    if (!dmaPtr) {
        return osVirtualToPhysical(gAudDmaState.firstUsed);
    }
    gAudDmaState.firstFree = (AMDMABuffer *) dmaPtr->node.next;
    alUnlink((ALLink *) dmaPtr);
    if (lastDmaPtr) {
        alLink((ALLink *) dmaPtr, (ALLink *) lastDmaPtr);
    } else if (gAudDmaState.firstUsed) {
        lastDmaPtr = gAudDmaState.firstUsed;
        gAudDmaState.firstUsed = dmaPtr;
        dmaPtr->node.next = (ALLink *) lastDmaPtr;
        dmaPtr->node.prev = NULL;
        lastDmaPtr->node.prev = (ALLink *) dmaPtr;
    } else {
        gAudDmaState.firstUsed = dmaPtr;
        dmaPtr->node.next = NULL;
        dmaPtr->node.prev = NULL;
    }
    foundBuffer = dmaPtr->ptr;
    addr -= delta;
    dmaPtr->startAddr = addr;
    dmaPtr->lastFrame = audFrameCt;
    osPiStartDma((OSIoMesg *) &audDMAIOMesgBuf[nextDMA++], OS_MESG_PRI_NORMAL, OS_READ, addr, foundBuffer, 0x200, &audDMAMessageQ);
    return osVirtualToPhysical(foundBuffer) + delta;
}

/* The DMA callback factory (0x80268254).
 * __amDmaNew */
ALDMAproc __amDmaNew(AMDMAState **state) {
    s32 pad;

    if (!gAudDmaState.initialized) {
        gAudDmaState.firstUsed = NULL;
        gAudDmaState.firstFree = &dmaBuffs[0];
        gAudDmaState.initialized = 1;
    }
    *state = &gAudDmaState;
    return (ALDMAproc) __amDMA;
}

/* Frees the old DMA buffers (0x802682A4).
 * __clearAudioDMA */
void __clearAudioDMA(void) {
    u32 i;
    OSIoMesg *iomsg;
    AMDMABuffer *dmaPtr;
    AMDMABuffer *nextPtr;

    for (i = 0; i < nextDMA; i++) {
        if (osRecvMesg(&audDMAMessageQ, (OSMesg *) &iomsg, OS_MESG_NOBLOCK) == -1) {
            debug_printf("Dma not done\n");
        }
    }
    dmaPtr = gAudDmaState.firstUsed;
    while (dmaPtr) {
        nextPtr = (AMDMABuffer *) dmaPtr->node.next;
        if (dmaPtr->lastFrame + 1 < audFrameCt) {
            if (gAudDmaState.firstUsed == dmaPtr) {
                gAudDmaState.firstUsed = (AMDMABuffer *) dmaPtr->node.next;
            }
            alUnlink((ALLink *) dmaPtr);
            if (gAudDmaState.firstFree) {
                alLink((ALLink *) dmaPtr, (ALLink *) gAudDmaState.firstFree);
            } else {
                gAudDmaState.firstFree = dmaPtr;
                dmaPtr->node.next = NULL;
                dmaPtr->node.prev = NULL;
            }
        }
        dmaPtr = nextPtr;
    }
    nextDMA = 0;
    audFrameCt++;
}
