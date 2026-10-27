#include "common.h"
#include <ultra64.h>
#include "game/game.h"
/* Views: this file reads these shared variables (game/variables.h) as other types. */
#define gDynamicBufs ((FeDyn *) gDynamicBufs)
#define gFrameBufferPhys ((void * N64P *) gFrameBufferPhys)
/* end of views */


extern s32 gPakErrorFrames;


/* Empty; the save thread (fe_pak_thread) calls it once per request, after cont_wait_read(): a stripped debug
 * or profiling hook (0x801EE390). */
void fe_pak_request_hook(void) {
}

/* The save thread's error screen: a black frame and a window (0x801EE398).
 * draw one front-end frame: a cleared screen plus the yoshi window `arg0` */
void fe_pak_error_frame(s32 arg0) {
    FeDyn *buf;
    Gfx *gdl;
    s32 i;

    buf = &gDynamicBufs[gBufferIndex ^ 1];
    nextdma = 0;
    no_palette_dmas = 0;
    gdl = buf->dl;
    tex_reset_decode_queue();
    font_age_cache();
    if (currentYoshiWindow != arg0) {
        yoshi_demand(arg0 | YOSHI_DEMAND_OPEN | YOSHI_DEMAND_FORCE);
        snd_stop_all_looping();
        gPakErrorFrames = 0;
    }
    if (gPakErrorFrames == 2) {
        osViBlack(0);
    }
    text_begin_frame();
    rsp_send_task((u64 *) (gDynamicBufs[gBufferIndex].dl), gFrameDlLen, 1, 1, 0x4D2, 0);
    gBufferIndex ^= 1;
    gSPSegment(gdl++, 0, 0);
    gSPSegment(gdl++, 2, osVirtualToPhysical(buf));
    gSPSegment(gdl++, 1, osVirtualToPhysical(gStaticSegment));
    gSPDisplayList(gdl++, seg1RdpInitDl);
    gSPDisplayList(gdl++, seg1RspInitDl);
    gDPSetCycleType(gdl++, G_CYC_FILL);
    gDPSetColorImage(gdl++, G_IM_FMT_RGBA, G_IM_SIZ_16b, 320, gFrameBufferPhys[gBufferIndex]);
    gDPPipeSync(gdl++);
    gDPSetFillColor(gdl++, 0x00010001);
    gDPFillRectangle(gdl++, 0, 0, 319, 239);
    gDPSetCycleType(gdl++, G_CYC_1CYCLE);
    gdl = yoshi_draw(gdl, (s32) &gDynamicBufs[gBufferIndex], &gFrameDlLen);
    gDPFullSync(gdl++);
    gSPEndDisplayList(gdl++);
    gFrameDlLen = gdl - buf->dl;
    for (i = 0; i < nextdma; i++) {
        osRecvMesg(&textureDmaMessageQ, NULL, OS_MESG_BLOCK);
    }
    for (i = 0; i < nextdma - no_palette_dmas; i++) {
        tex_decode_next();
    }
    rsp_wait_task(0x4D2);
    gPakErrorFrames++;
}
