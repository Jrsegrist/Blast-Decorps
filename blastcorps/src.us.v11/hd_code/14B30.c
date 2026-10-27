#include "common.h"
#include <ultra64.h>
#include "game/game.h"

/* drawtext.c (from its assert): batched text quads. */

typedef struct {
    u16 key;
    s32 vtx;
    u8 * N64P tex;
} TextQuad; /* size 0xC */

extern s32 maxCharacters;
extern Vtx * N64P gTextVtx[2];
extern TextQuad * N64P gTextQuads;
extern s32 gTextFlushed;
extern s32 gTextCount;
extern s32 gTextVtxCount;
#ifdef PORT_HOST
void port_unit_mark(void *p, u32 n, s32 width);
u8 port_n64_byte(const void *p);

void port_garbage(const void *p, u32 len);

/* A quad text_queue_string just wrote: record its units, for the stale-byte
 * emulation (a later user of this heap memory that leaves bytes unwritten,
 * 2E490.c's sprite quads, sees the N64's big-endian key / vertex index /
 * texture words).  The two padding bytes after the key are never written nor
 * read (the heap's old bytes, which the sort moves around): text_init
 * logs them as garbage for the comparator. */
static void port_textquad(TextQuad *q) {
    port_unit_mark(&q->key, 1, 2);
    port_unit_mark(&q->vtx, 1, 4);
    port_unit_mark(&q->tex, 1, 4);
}
#endif

#define VTX(n) gTextVtx[gBufferIndex][n].v

void text_set_render_state(Gfx **gdlp, s32 arg1);
void text_flush_quads(Gfx **gdlp, s32 arg1);
void text_copy_bytes(u8 *dst, u8 *src, s32 n);
s32 text_compare_keys(u16 *arg0, u16 *arg1);

/* Per-mode maxCharacters, the text vertex and quad buffers (0x802592F0). */
void text_init(void) {
    s32 i;

    if ((gNextGameMode & (MODE_LEVEL_STATUS | MODE_PAK_CHECKED | MODE_LOAD_GAME | MODE_NEW_PLAYER | MODE_NAME_ENTRY | MODE_ERASE_CONFIRM | MODE_ERASE_PLAYER_CHOSEN | MODE_LOAD_GAME_RETURN | MODE_ERASE_MENU | MODE_TO_ERASE_MENU | MODE_NAME_ENTERED | MODE_PLAYER_CHOSEN | MODE_RESULTS | MODE_PAK_CHECK | MODE_PROMOTION | MODE_PRE_PROMOTION | MODE_PAK_FILES | MODE_UNUSED_33 | MODE_UNUSED_WINDOW_13 | MODE_PAK_MANAGER_CHECKED | MODE_PAK_FILE_CHOSEN | MODE_PAK_DELETE_CONFIRM | MODE_PAK_FILE_DELETED | MODE_UNUSED_41 | MODE_BEST_TIMES | MODE_PAK_MANAGER | MODE_ACADEMY_MESSAGE | MODE_ATTRACT_PICTURE | MODE_REGRADE_NOTICE | MODE_REGRADE_NOTICE_END | MODE_UNUSED_52 | MODE_BOOT_CHECK | MODE_ERASE_SAVE | MODE_BEST_TIMES_UNUSED | MODE_GUEST_PLAYER | MODE_NO_CONTROLLER | MODE_QUIT_GAME | MODE_LOAD_GAME_EXIT)) || gNextGameMode == MODE_GLOBE_SEQ || gNextGameMode == MODE_ATTRACT ||
        levelno == 0x28 || levelno == 0x32) {
        maxCharacters = 0x200;
    } else if (gNextGameMode == MODE_REPLAY) {
        maxCharacters = 0x9C;
    } else {
        maxCharacters = 0xAC;
    }
    for (i = 0; i < 2; i++) {
        gTextVtx[i] = (Vtx *) gHeapPtr;
        gHeapPtr += maxCharacters * 16 * 4;
    }
    gTextQuads = (TextQuad *) gHeapPtr;
    gHeapPtr += maxCharacters * 12;
#ifdef PORT_HOST
    for (i = 0; i < maxCharacters; i++) {
        port_garbage((u8 *) &gTextQuads[i].key + 2, 2);
    }
#endif
    font_reset_cache();
}

/* Empties the text queue for a new frame (0x80259450). */
void text_begin_frame(void) {
    gTextFlushed = 0;
    gTextCount = 0;
    gTextVtxCount = 0;
}

/* The text quads' render state (0x8025946C). */
void text_set_render_state(Gfx **gdlp, s32 arg1) {
    Gfx *gdl = *gdlp;

    gSPClearGeometryMode(gdl++, 0xFFFFFFFF);
    gSPSetGeometryMode(gdl++, G_SHADE | G_SHADING_SMOOTH);
    gDPPipeSync(gdl++);
    gDPSetCycleType(gdl++, G_CYC_1CYCLE);
    gDPSetRenderMode(gdl++, G_RM_XLU_SURF, G_RM_XLU_SURF2);
    gDPSetCombineMode(gdl++, G_CC_MODULATERGBA, G_CC_MODULATERGBA);
    gDPSetTextureFilter(gdl++, G_TF_BILERP);
    gSPTexture(gdl++, 0xFFFF, 0xFFFF, 0, G_TX_RENDERTILE, G_ON);
    *gdlp = gdl;
}

/* A generic Shell sort (the front end uses it too) (0x802595E0).
 * Shell sort (h = 3h + 1) of n elements of the given size. */
void text_shell_sort(u8 *base, s32 n, s32 size, s32 (*cmp)(void *, void *)) {
    s32 i;
    s32 j;
    s32 h;
    u8 *b;
    u8 tmp[256];

    b = base;
    for (h = 1; h <= n / 9; h = 3 * h + 1) {
    }
    for (; h > 0; h /= 3) {
        for (i = h; i < n; i++) {
            text_copy_bytes(tmp, b + size * i, size);
            j = i;
            while (j >= h && cmp(b + (j - h) * size, tmp) > 0) {
                text_copy_bytes(b + size * j, b + (j - h) * size, size);
                j -= h;
            }
            text_copy_bytes(b + size * j, tmp, size);
        }
    }
}

/* Copies n bytes (0x802597D8). */
void text_copy_bytes(u8 *dst, u8 *src, s32 n) {
    s32 i;

    for (i = 0; i < n; i++) {
        dst[i] = src[i];
    }
}

/* The sort comparator: the quads' u16 keys (0x80259814). */
s32 text_compare_keys(u16 *arg0, u16 *arg1) {
    return *arg0 - *arg1;
}

/* Sorts the queued text quads by key and emits them (0x80259824). */
void text_flush_quads(Gfx **gdlp, s32 arg1) {
    u8 *tex = NULL;
    Gfx *gdl = *gdlp;
    s32 i;

    text_shell_sort((u8 *) &gTextQuads[gTextFlushed], gTextCount - gTextFlushed, sizeof(TextQuad), (s32 (*)(void *, void *)) text_compare_keys);
    for (i = gTextFlushed; i < gTextCount; i++) {
        if (gTextQuads[i].tex != tex) {
            tex = gTextQuads[i].tex;
            if (i == gTextFlushed) {
                gDPLoadTextureBlock_4b(gdl++, OS_K0_TO_PHYSICAL(tex), G_IM_FMT_I, 32, 32, 0, G_TX_CLAMP, G_TX_CLAMP,
                                       G_TX_NOMASK, G_TX_NOMASK, G_TX_NOLOD, G_TX_NOLOD);
            } else {
                gDPSetTextureImage(gdl++, G_IM_FMT_I, G_IM_SIZ_16b, 1, OS_K0_TO_PHYSICAL(tex));
                gDPLoadSync(gdl++);
                gDPLoadBlock(gdl++, G_TX_LOADTILE, 0, 0, 0xFF, 0x400);
            }
        }
        gSPVertex(gdl++, &gTextVtx[gBufferIndex][gTextQuads[i].vtx * 4], 4, 0);
        gSP1Triangle(gdl++, 0, 1, 2, 0);
        gSP1Triangle(gdl++, 0, 2, 3, 0);
    }
    osWritebackDCache(&gTextVtx[gBufferIndex][gTextFlushed * 4], (gTextCount - gTextFlushed) * 64);
    gTextFlushed = gTextCount;
    *gdlp = gdl;
}

/* Draws the queued text with the current matrices (0x80259BD4). */
void text_flush(Gfx **gdlp, s32 arg1) {
    Gfx *gdl = *gdlp;

    text_set_render_state(&gdl, arg1);
    text_flush_quads(&gdl, arg1);
    *gdlp = gdl;
}

/* Loads the screen matrices, then draws the queued text (0x80259C24). */
void text_flush_screen(Gfx **gdlp, Mtx *arg1) {
    Gfx *gdl = *gdlp;

    gSPMatrix(gdl++, &arg1[3], G_MTX_PROJECTION | G_MTX_LOAD | G_MTX_NOPUSH);
    gSPMatrix(gdl++, &arg1[7], G_MTX_MODELVIEW | G_MTX_LOAD | G_MTX_NOPUSH);
    text_set_render_state(&gdl, (s32) arg1);
    text_flush_quads(&gdl, (s32) arg1);
    *gdlp = gdl;
}

void text_queue_string(Gfx * N64P *gfxp, u8 *str, u16 *wstr, u8 align, s32 fit, f32 x, s32 y, f32 w, s32 h, u8 forward,
                   u8 r0, u8 g0, u8 b0, u8 a0, u8 r1, u8 g1, u8 b1, u8 a1, u8 r2, u8 g2, u8 b2, u8 a2,
                   u8 r3, u8 g3, u8 b3, u8 a3);

/* A string in one flat colour (0x80259CCC).
 * Draws a string in one flat colour. */
void text_print(Gfx * N64P *gfxp, u8 *str, u16 *wstr, u8 align, s32 fit, s32 x, s32 y, s32 w, s32 h, u8 forward,
                   u8 r, u8 g, u8 b, u8 a) {
    text_queue_string(gfxp, str, wstr, align, fit, x, y, w, h, forward, r, g, b, a, r, g, b, a, r, g, b, a, r, g, b, a);
}

/* A string with top and bottom colours (0x80259DC8).
 * Draws a string with a vertical gradient (top colour, bottom colour). */
void text_print_gradient(Gfx * N64P *gfxp, u8 *str, u16 *wstr, u8 align, s32 fit, s32 x, s32 y, s32 w, s32 h, u8 forward,
                   u8 r0, u8 g0, u8 b0, u8 a0, u8 r1, u8 g1, u8 b1, u8 a1) {
    text_queue_string(gfxp, str, wstr, align, fit, x, y, w, h, forward, r0, g0, b0, a0, r0, g0, b0, a0, r1, g1, b1, a1,
                  r1, g1, b1, a1);
}

/* One quad per character, four corner colours (0x80259EC4).
 * Queues one textured quad per visible character of str (or the 0xFFE-
 * terminated wide string wstr) into the current vertex buffer and the
 * TextQuad list; text_flush_quads later sorts and draws them. Characters with
 * all four alphas zero, and the per-font space characters, only advance. */
void text_queue_string(Gfx * N64P *gfxp, u8 *str, u16 *wstr, u8 align, s32 fit, f32 x, s32 y, f32 w, s32 h, u8 forward,
                   u8 r0, u8 g0, u8 b0, u8 a0, u8 r1, u8 g1, u8 b1, u8 a1, u8 r2, u8 g2, u8 b2, u8 a2,
                   u8 r3, u8 g3, u8 b3, u8 a3) {
    u16 glyph;
    u8 *p;
    u16 *wp;
    u8 done = 0;
    f32 adv;
    u8 wide = 0;
    u8 font = 0;
    s32 ch;
#ifdef PORT_HOST
    s32 port_first = gTextCount; /* this string's first quad (widescreen HUD sides) */
#endif

    if (str == NULL || *str == 0) {
        return;
    }
    p = str;
    wp = wstr;
    if (!forward) {
        if (wide) {
            while (*wp != 0xFFE) {
                wp++;
            }
            wp--;
        } else {
            while (*p != 0) {
                p++;
            }
            p--;
        }
    }
    if (fit) {
        x = font_center_x(fit, w, str, (s32) wstr);
    }
    while (!done) {
        switch (font) {
            case 0:
                adv = 0.21f;
                switch (*p) {
                    case '0':
                        adv = 0.25f;
                        glyph = 0;
                        break;
                    case '1':
                        adv = 0.25f;
                        glyph = 1;
                        break;
                    case '2':
                        adv = 0.25f;
                        glyph = 2;
                        break;
                    case '3':
                        adv = 0.25f;
                        glyph = 3;
                        break;
                    case '4':
                        adv = 0.25f;
                        glyph = 4;
                        break;
                    case '5':
                        adv = 0.25f;
                        glyph = 5;
                        break;
                    case '6':
                        adv = 0.25f;
                        glyph = 6;
                        break;
                    case '7':
                        adv = 0.25f;
                        glyph = 7;
                        break;
                    case '8':
                        adv = 0.25f;
                        glyph = 8;
                        break;
                    case '9':
                        adv = 0.25f;
                        glyph = 9;
                        break;
                    case 'A':
                        glyph = 10;
                        break;
                    case 'B':
                        glyph = 11;
                        break;
                    case 'C':
                        glyph = 12;
                        break;
                    case 'D':
                        glyph = 13;
                        break;
                    case 0x7f:
                        adv = 0.05f;
                        glyph = 14;
                        break;
                    case 'E':
                        glyph = 15;
                        break;
                    case 'F':
                        glyph = 16;
                        break;
                    case 'G':
                        glyph = 17;
                        break;
                    case 'H':
                        glyph = 18;
                        break;
                    case 'I':
                        adv = 0.27f;
                        glyph = 19;
                        break;
                    case 'J':
                        glyph = 20;
                        break;
                    case 'K':
                        glyph = 21;
                        break;
                    case 'L':
                        glyph = 22;
                        break;
                    case 'M':
                        adv = 0.05f;
                        glyph = 23;
                        break;
                    case 'N':
                        glyph = 24;
                        break;
                    case 'O':
                        glyph = 25;
                        break;
                    case 'P':
                        glyph = 26;
                        break;
                    case 'Q':
                        glyph = 27;
                        break;
                    case 'R':
                        glyph = 28;
                        break;
                    case 'S':
                        glyph = 29;
                        break;
                    case 'T':
                        glyph = 30;
                        break;
                    case 'U':
                        glyph = 31;
                        break;
                    case 'V':
                        glyph = 32;
                        break;
                    case 'W':
                        adv = 0.05f;
                        glyph = 33;
                        break;
                    case 'X':
                        glyph = 34;
                        break;
                    case 'Y':
                        glyph = 35;
                        break;
                    case 'Z':
                        glyph = 36;
                        break;
                    case '\'':
                        adv = 0.25f;
                        glyph = 38;
                        break;
                    case ')':
                        adv = 0.25f;
                        glyph = 39;
                        break;
                    case ':':
                        adv = 0.36f;
                        glyph = 40;
                        break;
                    case ',':
                        adv = 0.25f;
                        glyph = 41;
                        break;
                    case '$':
                        glyph = 42;
                        break;
                    case '!':
                        adv = 0.25f;
                        glyph = 43;
                        break;
                    case '.':
                        adv = 0.3f;
                        glyph = 44;
                        break;
                    case '-':
                        adv = 0.25f;
                        glyph = 45;
                        break;
                    case '(':
                        adv = 0.25f;
                        glyph = 46;
                        break;
                    case '%':
                        glyph = 47;
                        break;
                    case '?':
                        glyph = 48;
                        break;
                    case '#':
                        glyph = 49;
                        break;
                    case '/':
                        glyph = 50;
                        break;
                    case ' ':
                    case '&':
                        adv = 0.3f;
                        glyph = 0;
                        break;
                    case 'a':
                    case 'b':
                    case 'd':
                    case 'e':
                    case 'k':
                    case 'm':
                        glyph = *p - 0x22C;
                        break;
                    default:
                        glyph = 0;
                        adv = 0.0f;
                        break;
                }
                glyph += 0xF4C;
                break;
            case 1:
                adv = 1.0f;
                if (*wp < 0x200) {
                    glyph = *wp + 0xD4C;
                } else if (*wp == 0x1001) {
                    glyph = 0xF7D;
                } else {
                    glyph = 0xD4C;
                }
                break;
        }
        if (align == 1) {
            if (forward) {
                x -= adv * w;
            } else {
                x += adv * w;
            }
        }
        if (wide) {
            ch = *wp;
        } else {
            ch = *p;
        }
        if (ch != gTextSpace[font] && ch != gFontAltBlankChar[font] && ch != gFontNoAdvanceChar[font] &&
            (a0 != 0 || a1 != 0 || a2 != 0 || a3 != 0)) {
            VTX(gTextVtxCount).ob[0] = x;
            VTX(gTextVtxCount).ob[1] = y;
            VTX(gTextVtxCount).ob[2] = -10;
            VTX(gTextVtxCount).flag = 0;
            VTX(gTextVtxCount).tc[0] = 0;
            VTX(gTextVtxCount).tc[1] = 0x3E0;
            VTX(gTextVtxCount).cn[0] = r0;
            VTX(gTextVtxCount).cn[1] = g0;
            VTX(gTextVtxCount).cn[2] = b0;
            VTX(gTextVtxCount).cn[3] = a0;
            gTextVtxCount++;
            VTX(gTextVtxCount).ob[0] = x + w;
            VTX(gTextVtxCount).ob[1] = y;
            VTX(gTextVtxCount).ob[2] = -10;
            VTX(gTextVtxCount).flag = 0;
            VTX(gTextVtxCount).tc[0] = 0x3E0;
            VTX(gTextVtxCount).tc[1] = 0x3E0;
            VTX(gTextVtxCount).cn[0] = r1;
            VTX(gTextVtxCount).cn[1] = g1;
            VTX(gTextVtxCount).cn[2] = b1;
            VTX(gTextVtxCount).cn[3] = a1;
            gTextVtxCount++;
            VTX(gTextVtxCount).ob[0] = x + w;
            VTX(gTextVtxCount).ob[1] = y + h;
            VTX(gTextVtxCount).ob[2] = -10;
            VTX(gTextVtxCount).flag = 0;
            VTX(gTextVtxCount).tc[0] = 0x3E0;
            VTX(gTextVtxCount).tc[1] = 0;
            VTX(gTextVtxCount).cn[0] = r2;
            VTX(gTextVtxCount).cn[1] = g2;
            VTX(gTextVtxCount).cn[2] = b2;
            VTX(gTextVtxCount).cn[3] = a2;
            gTextVtxCount++;
            VTX(gTextVtxCount).ob[0] = x;
            VTX(gTextVtxCount).ob[1] = y + h;
            VTX(gTextVtxCount).ob[2] = -10;
            VTX(gTextVtxCount).flag = 0;
            VTX(gTextVtxCount).tc[0] = 0;
            VTX(gTextVtxCount).tc[1] = 0;
            VTX(gTextVtxCount).cn[0] = r3;
            VTX(gTextVtxCount).cn[1] = g3;
            VTX(gTextVtxCount).cn[2] = b3;
            VTX(gTextVtxCount).cn[3] = a3;
            gTextVtxCount++;
            gTextQuads[gTextCount].key = ch;
            if (r0 != 0 || b0 != 0 || g0 != 0) {
                gTextQuads[gTextCount].key += 0x8000;
            }
            gTextQuads[gTextCount].vtx = gTextCount;
            gTextQuads[gTextCount].tex = font_get_glyph(glyph);
#ifdef PORT_HOST
            port_textquad(&gTextQuads[gTextCount]);
#endif
            gTextCount++;
            if (!(gTextCount < maxCharacters)) {
                debug_printf("\n\a --- ASSERTION FAULT - %s - %s, line %d\n\n", "index<maxCharacters", "drawtext.c", 435);
            }
            if (gTextCount >= maxCharacters) {
                debug_printf("%d %d\n", gTextCount, maxCharacters);
            }
        }
        if (align == 1) {
            if (forward) {
                x += w - adv * w;
            } else {
                x -= w - adv * w;
            }
        } else if (ch != gFontNoAdvanceChar[font]) {
            if (forward) {
                x += w * gFontAdvance[font];
            }
            if (!forward) {
                x -= w * gFontAdvance[font];
            }
        }
        if (forward) {
            if (wide) {
                wp++;
                if (*wp == 0xFFE) {
                    done = 1;
                }
            } else {
                p++;
                if (*p == 0) {
                    done = 1;
                }
            }
        } else {
            if (wide) {
                if (wp == wstr) {
                    done = 1;
                }
                wp--;
            } else {
                if (p == str) {
                    done = 1;
                }
                p--;
            }
        }
    }
#ifdef PORT_HOST
    /* bc.exe's widescreen HUD (port.h PORT_WTAG): this string's vertices, so
     * a flush under PORT_WTAG_TEXT_SIDES can place it by the side it is on
     * (the flush sorts the quads of all strings by glyph) */
    if (gTextCount > port_first) {
        PORT_WTAG(&gTextVtx[gBufferIndex][port_first * 4], PORT_WTAG_TEXT, (gTextCount - port_first) * 4, fit);
    }
#endif
}
