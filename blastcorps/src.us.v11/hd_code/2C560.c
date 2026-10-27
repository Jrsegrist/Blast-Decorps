#include "common.h"
#include <ultra64.h>
#include "game/game.h"

/* sched.c's .bss (0x8036BEF0-0x8036BFC0), defined here in declaration
 * order so IDO lays it out like the original (placed by
 * hd_code_bss.us.v11.ld). Defining them matters for codegen: stores to a
 * variable defined in the same file share one %hi. Unreferenced holes are
 * placeholders. */
u64 gSchedYieldStart;   /* time of the last RSP yield */
u64 gSchedYieldMax;   /* the longest yield (statistics, reset every 480 retraces) */
u64 gSchedYieldTime;   /* how long the last yield took */
s32 D_8036BF08;
s32 D_8036BF0C;
s32 gPreNmi;      /* 0x8036BF10: the reset button was pressed (pre-NMI): everything stops */
s32 gSchedSwapFrame;   /* the retrace after the last buffer swap: a frame done before it waits (gDeferredSwapTask) */
s32 gSchedPrevSwapFrame;
s32 gDeferredSwapTask; /* 0x8036BF1C: a finished frame task whose swap waits for the next retrace */
s32 gSchedRdpTime;   /* the last frame's RDP time, 1/100 of a retrace */
s32 gSchedRspTime;   /* the last frame's RSP time, 1/100 of a retrace */
s32 D_8036BF28;
s32 gSchedGfxStartLag;   /* retrace to the frame task's start, 1/100 of a retrace */
u8 D_8036BF30[8];
OSTime gLastRetraceTime; /* 0x8036BF38: osGetTime() at the last retrace */
u64 gSchedAudioPrevStart;   /* last audio frame time */
u64 gSchedAudioStart;   /* audio task start time */
u64 gSchedAudioEnd;   /* audio task end time */
/* unreferenced words; scalars, since IDO 8-aligns arrays of this size */
s32 D_8036BF58;
s32 D_8036BF5C;
s32 D_8036BF60;
s32 D_8036BF64;
s32 D_8036BF68;
s32 D_8036BF6C;
s32 D_8036BF70;
s32 D_8036BF74;
OSTimer gAudioFrameTimer; /* 0x8036BF78: starts the audio frame between retraces (mq/msg at 0x8036BF90/94) */
u8 D_8036BF98[0x20];
u32 gSchedStatRetraces;   /* frame counter (wraps statistics every 480 frames) */
s32 gSchedTaskDone;

/* Blast Corps' scheduler: a Rare-modified copy of the SDK's sched.c
 * (GoldenEye's src/sched.c is a later version). Its types (BcSched,
 * BcScClient, BcScTask) are in game/types.h. */

/* Rare's assert, with the original sched.c line numbers passed explicitly. */
#define SCHED_ASSERT(EX, line) \
    if (!(EX)) debug_printf("\n\a --- ASSERTION FAULT - %s - %s, line %d\n\n", #EX, "sched.c", line)

#define M_GFXTASK 1
#define M_AUDTASK 2
#define RSP_STATE_SUSPENDED 3

extern OSViMode osViModeTable[];
void __scMain(void *arg);

/* The SDK's osCreateScheduler (PR/sched.h declares that name with the SDK's
 * OSSched): the VI manager, the mode, the interrupt messages and the
 * scheduler thread (0x80270D20). */
void sched_create(BcSched *sc, void *stack, OSPri priority, u8 mode, u8 numFields) {
    sc->audioListTail = (BcScTask *) &sc->audioListHead;
    sc->gfxListTail = (BcScTask *) &sc->gfxListHead;
    gPreNmi = 0;
    gDeferredSwapTask = 0;
    osCreateMesgQueue(&sc->interruptQ, sc->intBuf, 16);
    osCreateMesgQueue(&sc->cmdQ, sc->cmdMsgBuf, 16);
    osCreateViManager(0xFE);
    osViSetMode(&osViModeTable[mode]);
    osViBlack(TRUE);
    osSetEventMesg(OS_EVENT_SP, &sc->interruptQ, (OSMesg) 0x29B);
    osSetEventMesg(OS_EVENT_DP, &sc->interruptQ, (OSMesg) 0x29C);
    osSetEventMesg(OS_EVENT_PRENMI, &sc->interruptQ, (OSMesg) 0x29D);
    osSetEventMesg(OS_EVENT_FAULT, &sc->interruptQ, (OSMesg) 0x2A0);
    osViSetEvent(&sc->interruptQ, (OSMesg) 0x29A, numFields);
    osCreateThread(&sc->thread, 5, __scMain, sc, stack, priority);
    osStartThread(&sc->thread);
}

/* osScAddClient, with two extra client fields (period, kind) (0x80270E50). */
void sched_add_client(BcSched *sc, BcScClient *c, OSMesgQueue *msgQ, s32 arg3, s32 arg4) {
    OSIntMask mask;

    mask = osSetIntMask(OS_IM_NONE);
    c->msgQ = msgQ;
    c->next = sc->clientList;
    sc->clientList = c;
    c->period = arg3;
    c->kind = arg4;
    osSetIntMask(mask);
}

/* osScRemoveClient (0x80270ECC). */
void sched_remove_client(BcSched *sc, BcScClient *c) {
    BcScClient *client = sc->clientList;
    BcScClient *prev = NULL;
    OSIntMask mask;

    mask = osSetIntMask(OS_IM_NONE);
    while (client != NULL) {
        if (client == c) {
            if (prev) {
                prev->next = c->next;
            } else {
                sc->clientList = c->next;
            }
            break;
        }
        prev = client;
        client = client->next;
    }
    osSetIntMask(mask);
}

/* osScGetCmdQ: &sc->cmdQ (0x80270F74). */
void *sched_get_cmd_queue(void *arg0) {
    return (u8 *) arg0 + 0x58;
}

void __scHandleRetrace(BcSched *sc);
void sched_audio_request(BcSched *sc);
void __scHandleRSP(BcSched *sc);
void __scHandleRDP(BcSched *sc);
void sched_add_task(BcSched *sc, BcScTask *t);

/* The scheduler thread (the SDK's __scMain): dispatches the retrace, RSP and
 * RDP done, audio timer and pre-NMI messages; anything else is a task to queue
 * (0x80270F7C). */
void __scMain(void *arg) {
    OSMesg msg;
    BcSched *sc = (BcSched *) arg;
    BcScClient *client;

    while (TRUE) {
        osRecvMesg(&sc->interruptQ, &msg, OS_MESG_BLOCK);
        if (!(get_cop0_status() & 0x1000)) {
            for (client = sc->clientList; client != NULL; client = client->next) {
                osSendMesg(client->msgQ, (OSMesg) 0x29D, OS_MESG_NOBLOCK);
            }
            gPreNmi = 1;
            osViBlack(TRUE);
            debug_printf("GO %x\n", osDpGetStatus());
            osDpSetStatus(4);
            debug_printf("current=%x start=%x end=%x dpstat=%x spstat=%x\n", IO_READ(DPC_CURRENT_REG),
                          IO_READ(DPC_START_REG), IO_READ(DPC_END_REG), IO_READ(DPC_STATUS_REG), IO_READ(SP_STATUS_REG));
            debug_printf("GO %x\n", osDpGetStatus());
            while (TRUE) {
            }
        }
        switch ((s32) msg) {
            case 0x29A:
                if (++gSchedStatRetraces % 480 == 0) {
                    gSchedYieldMax = gSchedYieldTime;
                    D_8036BF08 = D_8036BF0C;
                }
                __scHandleRetrace(sc);
                break;
            case 0x29E:
                sched_audio_request(sc);
                break;
            case 0x29B:
                __scHandleRSP(sc);
                break;
            case 0x29C:
                __scHandleRDP(sc);
                break;
            case 0x29F:
                osSendMesg(gAudioFrameTimer.mq, gAudioFrameTimer.msg, OS_MESG_BLOCK);
                break;
            case 0x29D:
                for (client = sc->clientList; client != NULL; client = client->next) {
                    osSendMesg(client->msgQ, (OSMesg) 0x29D, OS_MESG_NOBLOCK);
                }
                gPreNmi = 1;
                osViBlack(TRUE);
                debug_printf("%x\n", osDpGetStatus());
                osDpSetStatus(4);
                debug_printf("current=%x start=%x end=%x dpstat=%x spstat=%x\n", IO_READ(DPC_CURRENT_REG),
                              IO_READ(DPC_START_REG), IO_READ(DPC_END_REG), IO_READ(DPC_STATUS_REG), IO_READ(SP_STATUS_REG));
                debug_printf("%x\n", osDpGetStatus());
                while (TRUE) {
                }
            case 0x2A0:
                debug_printf(" *** CPU FAULT *** - UNFREEZING RDP?\n");
                while (osViGetCurrentFramebuffer() != osViGetNextFramebuffer()) {
                }
                osDpSetStatus(4);
                while (TRUE) {
                }
            default:
                sched_add_task(sc, (BcScTask *) msg);
                break;
        }
    }
}

void __scAppendList(BcSched *sc, BcScTask *t);
s32 sched_send_mesg(OSMesgQueue *mq, OSMesg msg, s32 flag);
s32 __scTaskComplete(BcSched *sc, BcScTask *t);
extern u8 gSchedTimeGfxStart;
void sched_start_next_task(BcSched *sc, s32 availRCP);
void __scYield(BcSched *sc);

/* Queue a task, then run the scheduler if the RSP is idle (0x802712B4). */
void sched_add_task(BcSched *sc, BcScTask *t) {
    __scAppendList(sc, t);
    if (sc->curRSPTask == NULL) {
        sched_start_next_task(sc, 1);
    }
}


/* Message 0x29E, the audio thread's frame (audio.c): yield the running RSP
 * task for it, or start the audio task when the RSP is idle (0x802712FC). */
void sched_audio_request(BcSched *sc) {
    if (sc->curRSPTask != NULL) {
        __scYield(sc);
    } else {
        gSchedYieldTime = 0;
        sched_start_next_task(sc, 0);
    }
}


/* The SDK's __scHandleRetrace: counts the retrace (not while paused),
 * swaps in a deferred frame, queues the command-queue tasks due this retrace,
 * arms the audio timer every other retrace and tells the retrace clients
 * (0x80271358). */
void __scHandleRetrace(BcSched *sc) {
    BcScTask *rspTask;
    BcScClient *client;
    s32 i;
    s32 count;

    sc->frameCount++;
    /* (PORT_EMU: the main thread sets/clears gPaused after its frame's
     * CPU time; a retrace inside that time sees the old value) */
    if (!PORT_EMU("__scHandleRetrace:8BD0", gPaused)) {
        sc->retraceCount++;
    }
    gLastRetraceTime = osGetTime();
    if (gDeferredSwapTask) {
        osViSwapBuffer(((BcScTask *) N64_IPTR(gDeferredSwapTask))->framebuffer);
        gSchedPrevSwapFrame = gSchedSwapFrame;
        gSchedSwapFrame = sc->frameCount + 1;
        osDpSetStatus(8);
        if (((BcScTask *) N64_IPTR(gDeferredSwapTask))->msgQ != NULL) {
            sched_send_mesg(((BcScTask *) N64_IPTR(gDeferredSwapTask))->msgQ, ((BcScTask *) N64_IPTR(gDeferredSwapTask))->msg, OS_MESG_NOBLOCK);
        }
        gDeferredSwapTask = 0;
    } else if (osViGetCurrentFramebuffer() == osViGetNextFramebuffer()) {
        if (osDpGetStatus() & 2) {
            sc->rdpStartTime = osGetTime();
            osDpSetStatus(4);
        }
    }

    count = sc->cmdQ.validCount;
    for (i = 0; i < count; i++) {
        SCHED_ASSERT(osRecvMesg(&sc->cmdQ, (OSMesg *)&rspTask, OS_MESG_NOBLOCK) != -1, 445);
        if (sc->frameCount % ((u32 *) rspTask->client)[2] == 0) {
            __scAppendList(sc, rspTask);
        } else {
            osSendMesg(&sc->cmdQ, (OSMesg) rspTask, OS_MESG_NOBLOCK);
        }
    }

    if (sc->audioListHead != NULL && !(sc->frameCount & 1)) {
        osSetTimer(&gAudioFrameTimer, 280000, 0, ((OSMesgQueue * N64P *) sc->audioListHead->client)[1], (OSMesg) 5);
    }

    for (client = sc->clientList; client != NULL; client = client->next) {
        if (client->kind == 3) {
            osSendMesg(client->msgQ, (OSMesg) 0x29A, OS_MESG_NOBLOCK);
        }
    }
}

/* The SDK's __scHandleRSP: the running RSP task finished or yielded; a
 * yielded graphics task lets the audio task run (0x802715DC). */
void __scHandleRSP(BcSched *sc) {
    BcScTask *t;
    OSTime now;

    SCHED_ASSERT(sc->curRSPTask, 498);
    t = sc->curRSPTask;
    sc->curRSPTask = NULL;
    if (t->state == RSP_STATE_SUSPENDED) {
        gSchedYieldTime = osGetTime() - gSchedYieldStart;
        if (gSchedYieldTime > 550000) {
            debug_printf("Silly yield time of %llu ticks\n", gSchedYieldTime);
        }
        if (gSchedYieldTime > gSchedYieldMax) {
            gSchedYieldMax = gSchedYieldTime;
        }
        if (!osSpTaskYielded(&t->list)) {
            t->state = 2;
            t->flags |= 4;
            __scTaskComplete(sc, t);
        }
        SCHED_ASSERT(sc->audioListHead, 538);
        if (sc->audioListHead == NULL) {
            debug_printf("Yield took %llu, max %llu\n", gSchedYieldTime, gSchedYieldMax);
        }
        sched_start_next_task(sc, 0);
    } else {
        if (t->flags & 0x40) {
            now = osGetTime();
            gSchedRspTime = (now - sc->gfxStartTime) / 7825;
            gSchedTimeGfxStart = 1;
        } else if (t->list.t.type == M_AUDTASK) {
            gSchedAudioEnd = osGetTime();
            gSchedAudioPrevStart = gSchedAudioStart;
        }
        t->state = 2;
        t->flags |= 4;
        SCHED_ASSERT(sc->curRSPTask==0, 560);
        if (__scTaskComplete(sc, t) && sc->gfxListHead != NULL && sc->gfxListHead->flags != 0x47) {
            sched_start_next_task(sc, 1);
        }
    }
}


/* The SDK's __scHandleRDP: a frame task's RDP part is done: swap its buffer
 * (or defer the swap to the next retrace), turn the screen on at the third
 * frame of a mode (0x80271904). */
void __scHandleRDP(BcSched *sc) {
    BcScTask *t;
    OSTime now;

    SCHED_ASSERT(sc->curRDPTask, 586);
    t = sc->curRDPTask;
    sc->curRDPTask = NULL;
    t->flags |= 8;
    if (sc->frameCount != gSchedSwapFrame || (gGameMode & (MODE_BOOT_SCREEN1 | MODE_BOOT_SCREEN2 | MODE_LEVEL_STATUS | MODE_PAK_CHECKED | MODE_LOAD_GAME | MODE_NEW_PLAYER | MODE_NAME_ENTRY | MODE_ERASE_CONFIRM | MODE_ERASE_PLAYER_CHOSEN | MODE_LOAD_GAME_RETURN | MODE_ERASE_MENU | MODE_TO_ERASE_MENU | MODE_NAME_ENTERED | MODE_PLAYER_CHOSEN | MODE_RESULTS | MODE_PAK_CHECK | MODE_PRE_PROMOTION | MODE_PAK_FILES | MODE_UNUSED_33 | MODE_UNUSED_WINDOW_13 | MODE_PAK_MANAGER_CHECKED | MODE_PAK_FILE_CHOSEN | MODE_PAK_DELETE_CONFIRM | MODE_PAK_FILE_DELETED | MODE_UNUSED_41 | MODE_BEST_TIMES | MODE_PAK_MANAGER | MODE_ATTRACT_PICTURE | MODE_REGRADE_NOTICE | MODE_REGRADE_NOTICE_END | MODE_UNUSED_52 | MODE_BOOT_CHECK | MODE_ERASE_SAVE | MODE_BEST_TIMES_UNUSED | MODE_GUEST_PLAYER | MODE_NO_CONTROLLER | MODE_QUIT_GAME | MODE_LOAD_GAME_EXIT))) {
        gDeferredSwapTask = 0;
        osViSwapBuffer(t->framebuffer);
        gSchedPrevSwapFrame = gSchedSwapFrame;
        gSchedSwapFrame = sc->frameCount + 1;
        osDpSetStatus(8);
    } else {
        gDeferredSwapTask = (s32) t;
    }
    now = osGetTime();
    gSchedRdpTime = (now - sc->rdpStartTime) / 7825;
    if (((s32) gFramesInMode) == 3) {
        osViBlack(FALSE);
    }
    __scTaskComplete(sc, t);
}

s32 sched_send_mesg(OSMesgQueue *mq, OSMesg msg, s32 flag);

/* The SDK's __scTaskComplete: once a task has every part it needs done,
 * unlinks it and posts its done message (0x80271A84). */
s32 __scTaskComplete(BcSched *sc, BcScTask *t) {
    s32 rv;
    s32 needs;
    s32 done;
    s32 type;

    needs = t->flags & 3;
    done = (t->flags >> 2) & 3;
    type = t->list.t.type;
    if (!(t->flags & 0x40)) {
        needs &= 1;
        done &= 1;
    }
    if (needs == done) {
        if (type == M_GFXTASK) {
            SCHED_ASSERT(sc->gfxListHead, 636);
            sc->gfxListHead = sc->gfxListHead->next;
            if (sc->gfxListHead == NULL) {
                sc->gfxListTail = (BcScTask *) &sc->gfxListHead;
            }
        }
        if (t->msgQ != NULL) {
            if (!gDeferredSwapTask || type != M_GFXTASK) {
                if (t->flags & 0x40) {
                    rv = sched_send_mesg(t->msgQ, t->msg, OS_MESG_NOBLOCK);
                } else {
                    rv = osSendMesg(t->msgQ, t->msg, OS_MESG_NOBLOCK);
                }
                SCHED_ASSERT(rv!=-1, 649);
            }
        }
        gSchedTaskDone = 1;
    } else {
        gSchedTaskDone = 0;
    }
    return gSchedTaskDone;
}

/* The SDK's __scAppendList: a task onto the audio or graphics list (0x80271C24). */
void __scAppendList(BcSched *sc, BcScTask *t) {
    long type = t->list.t.type;

    SCHED_ASSERT((type == M_AUDTASK) || (type == M_GFXTASK), 668);
    if (type == M_AUDTASK) {
        sc->audioListTail->next = t;
        sc->audioListTail = t;
    } else {
        sc->gfxListTail->next = t;
        sc->gfxListTail = t;
    }
    t->next = NULL;
    t->state = 2;
}


/* Start the next RSP task: audio when availRCP is 0, else graphics (the
 * SDK's __scSchedule and __scExec in one) (0x80271CE4). */
void sched_start_next_task(BcSched *sc, s32 availRCP) {
    BcScTask *t;
    OSTime now;

    SCHED_ASSERT(!sc->curRSPTask, 696);
    if (availRCP == 0) {
        t = sc->audioListHead;
        SCHED_ASSERT(t, 701);
        if (t == NULL) {
            return;
        }
        sc->audioListHead = sc->audioListHead->next;
        if (sc->audioListHead == NULL) {
            sc->audioListTail = (BcScTask *) &sc->audioListHead;
        }
        gSchedAudioStart = osGetTime();
    } else {
        t = sc->gfxListHead;
        if (gSchedTimeGfxStart) {
            sc->gfxStartTime = osGetTime();
            now = osGetTime();
            gSchedGfxStartLag = (now - gLastRetraceTime) / 7825;
            gSchedTimeGfxStart = 0;
        }
    }
    t->state = 1;
    osSpTaskLoad(&t->list);
    osSpTaskStartGo(&t->list);
    sc->curRSPTask = t;
    if (t->flags & 0x40) {
        sc->curRDPTask = t;
    }
}

/* The SDK's __scYield: asks the running graphics task to yield (0x80271E88). */
void __scYield(BcSched *sc) {
    SCHED_ASSERT(sc->curRSPTask->list.t.type != M_AUDTASK, 735);
    if (sc->curRSPTask->list.t.type == M_GFXTASK) {
        SCHED_ASSERT(sc->curRSPTask->state != RSP_STATE_SUSPENDED, 739);
        sc->curRSPTask->state = RSP_STATE_SUSPENDED;
        gSchedYieldStart = osGetTime();
        osSpTaskYield();
    }
}


/* osSendMesg wrapper for frame tasks' done messages; the timing values are
 * computed but never used (leftover debug code) (0x80271F48). */
s32 sched_send_mesg(OSMesgQueue *mq, OSMesg msg, s32 flag) {
    OSTime remaining;
    OSTime now;
    s32 unused;

    remaining = gLastRetraceTime + 391250 - osGetTime();
    now = osGetTime();
    unused = 0;
    return osSendMesg(mq, msg, flag);
}
