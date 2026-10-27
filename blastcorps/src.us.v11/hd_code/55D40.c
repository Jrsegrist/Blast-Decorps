#include "common.h"
#include <ultra64.h>
#include "game/game.h"

extern s32 gCreditsScroll;
extern s32 gCreditsEndTimer;

/* Starts the credits (0x8029A500). */
void credits_init(void) {
    gCreditsScroll = -0xef;
    gCreditsEndTimer = 0;
}
typedef struct {
    u32 flags;
    u8 color;
    u8 size;
    char str[42];
} CreditLine;

extern CreditLine gCreditLines[];


/* Draws the visible credit lines and scrolls them (0x8029A518).
 * Credits scroller: draws the visible credit lines, advances the scroll, and ends the credits */
Gfx *credits_draw(Gfx * N64P *gfxp, Gfx *gfx) {
    Gfx *ret = gfx;
    CreditLine *line;
    s32 i;
#ifdef NON_MATCHING
    s32 x = 0; /* never assigned: the original passes whatever its stack slot (sp+0x58) holds; not traced */
#else
    s32 x;
#endif
    s32 y = 0;
    s32 color;

    for (i = 0; i < 0x52; i++) {
        line = &gCreditLines[i];
        if (line->flags & 1) {
            color = 0xA0;
        } else if (line->flags & 4) {
            color = 0x6A;
        } else if (line->flags & 8) {
            color = 0xD5;
        } else {
            color = 0;
        }
        if (y - gCreditsScroll >= -0x31 && y - gCreditsScroll < 0xF0) {
            text_print_gradient(gfxp, (u8 *) line->str, 0, 0, color, x, y - gCreditsScroll, line->size, line->size, 1,
                          gYoshiColors[line->color][0], gYoshiColors[line->color][1], gYoshiColors[line->color][2],
                          gYoshiColors[line->color][3], gYoshiColors[line->color][4], gYoshiColors[line->color][5],
                          gYoshiColors[line->color][6], gYoshiColors[line->color][7]);
        }
        if (line->flags & 0x20) {
            y += 0x26;
        }
        if (line->flags & 0x10) {
            y += 0x16;
        }
        if (line->flags & 0x40) {
            y += 0x11;
        }
    }
    if (((s32) gFramesInMode) == 100) {
        yoshi_demand(YOSHI_DEMAND_OPEN | YW_CREDITS_START);
    }
    if ((u32) ((s32) gFramesInMode) >= 0x18C) {
        gCreditsScroll++;
    }
    if (gCarrierHit) {
        gCreditsEndTimer++;
    }
    if (gCreditsEndTimer == 0x7D) {
        yoshi_demand(YOSHI_DEMAND_OPEN | YW_CREDITS_END);
        music_push_tune(MUS_FINALE);
    }
    if (gCreditsEndTimer >= 0x7E && yoshiState == 1 && areWeFading() == 0) {
        fade_to_mode_speed(MODE_SEQ_END, 0.75f);
    }
    return ret;
}
