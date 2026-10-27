#include "common.h"
#include <ultra64.h>
#include "game/game.h"
/* Views: this file reads these shared variables (game/variables.h) as other types. */
#define gFrameBuffers ((FrameBuffer *) gFrameBuffers)
#define gDynamicBufs ((FrameData *) gDynamicBufs)
/* end of views */


/* RSP task submission: builds a scheduler task (sched.c's BcScTask, game/types.h) for
 * one of the microcode slots, hands it to the scheduler and waits for its done
 * message.  The task's client is the main thread's gMainSchedClient; the
 * scheduler posts msg ((slot << 16) | id) on msgQ (gTaskDoneQueue) when it's done. */

/* Per-framebuffer data written back before a task runs */
typedef struct {
    u8 data[0x21498];
} FrameData;

typedef struct {
    u16 px[320 * 240];
} FrameBuffer;

extern u8 gL3dText[]; /* microcode text/data pairs */
extern u8 gL3dData[];
extern u8 gF3dText[];
extern u8 gF3dData[];
extern u8 * N64P gUcodeText[]; /* ucode text per slot */
extern u8 * N64P gUcodeData[]; /* ucode data per slot */
extern BcScTask gRspTasks[][2];
extern u64 gDramStack[]; /* dram stack */


/* Fills in the microcode slots: slot 0 the front end's line ucode, 1-3 the
 * game's (0x80284DB0). */
void rsp_init_ucode_table(void) {
    gUcodeText[0] = gL3dText;
    gUcodeData[0] = gL3dData;
    gUcodeText[1] = gF3dText;
    gUcodeData[1] = gF3dData;
    gUcodeText[2] = gF3dText;
    gUcodeData[2] = gF3dData;
    gUcodeText[3] = gF3dText;
    gUcodeData[3] = gF3dData;
}

/* Build and send an RSP task running microcode slot `slot` over the display
 * list `data` (`count` commands); `yield` makes it a frame task (flag 0x40:
 * swaps the frame buffer when done); its done message is (slot << 16) | id
 * on gTaskDoneQueue (0x80284E54). */
void rsp_send_task(u64 *data, s32 count, u8 slot, u8 yield, s32 id, u8 writebackAll) {
    BcScTask *task;
    s32 size;

    size = count * 8;
    task = &gRspTasks[slot][gBufferIndex];
    gRspSlotBusy[slot] = 1;
    task->list.t.type = M_GFXTASK;
    if (slot == 4) {
        task->list.t.flags = 2;
    } else {
        task->list.t.flags = 0;
    }
    task->list.t.ucode_boot = (u64 *) gRspBootText;
    task->list.t.ucode_boot_size = gAspMainText - gRspBootText;
    task->list.t.ucode = (u64 *) gUcodeText[slot];
    task->list.t.ucode_data = (u64 *) gUcodeData[slot];
    task->list.t.ucode_size = 0x1000;
    task->list.t.ucode_data_size = 0x800;
    task->list.t.dram_stack = gDramStack;
    task->list.t.dram_stack_size = 0x400;
    task->list.t.output_buff = gRdpOutputBuffer;
    task->list.t.output_buff_size = (u64 *) ((u8 *) gRdpOutputBuffer + 0xA000);
    task->list.t.data_ptr = data;
    task->list.t.data_size = size;
    task->list.t.yield_data_ptr = gYieldBuffer;
    task->list.t.yield_data_size = 0x900;
    task->next = NULL;
    task->msgQ = &gTaskDoneQueue;
    task->msg = (OSMesg) ((slot << 16) | id);
    task->flags = 3;
    if (yield) {
        task->flags |= 0x40;
    }
    task->framebuffer = &gFrameBuffers[gBufferIndex];
    task->client = &gMainSchedClient;
    if (writebackAll) {
        osWritebackDCacheAll();
    } else {
        osWritebackDCache(task, sizeof(BcScTask));
        osWritebackDCache(data, size);
        osWritebackDCache(&gDynamicBufs[gBufferIndex], sizeof(FrameData));
    }
    osSendMesg(&gScheduler.interruptQ, task, OS_MESG_BLOCK);
}

/* Wait for task `id` to finish, clearing the busy flag of every task that
 * reports in (0x80285110). */
void rsp_wait_task(s32 id) {
    OSMesg msg;

    do {
        osRecvMesg(&gTaskDoneQueue, &msg, OS_MESG_BLOCK);
        gRspSlotBusy[(u32) msg >> 16] = 0;
        msg = (OSMesg) ((u32) msg & 0xFFFF);
        if ((s32) msg != id) {
            debug_printf("Task %d received message %d\n", id, msg);
        }
    } while ((s32) msg != id);
}
