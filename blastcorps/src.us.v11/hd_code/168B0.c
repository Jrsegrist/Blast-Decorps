#include "common.h"
#include <ultra64.h>
#include "game/game.h"

/* font.c (from its assert strings): text/glyph-slot cache and string helpers */

extern u8 gGlyphAge[80];
extern u16 gGlyphIds[80];
extern u8 gGlyphTextures[][0x200];
extern u8 gNarrowTextBuf[];
extern u16 gWideTextBuf[];


#define ASSERT(EX, line) if (!(EX)) debug_printf("\n\a --- ASSERTION FAULT - %s - %s, line %d\n\n", #EX, "font.c", line)

/* Empties the glyph cache (0x8025B070). */
void font_reset_cache(void) {
    s32 i;

    for (i = 0; i < 80; i++) {
        gGlyphAge[i] = 0;
        gGlyphIds[i] = 0;
    }
}

/* A glyph's 32x32 I4 texture, cached or loaded (0x8025B0B8). */
u8 *font_get_glyph(u16 arg0) {
    s32 i;
    u8 found;

    found = 0;
    for (i = 0; i < 80 && !found;) {
        if (gGlyphIds[i] == arg0) {
            found = 1;
        } else {
            i++;
        }
    }
    if (!found) {
        for (i = 0; i < 80 && !found;) {
            if (gGlyphIds[i] == 0) {
                tex_queue_load(arg0, gGlyphTextures[i], 0);
                gGlyphIds[i] = arg0;
                found = 1;
            } else {
                i++;
            }
        }
    }
    if (!found) {
        for (i = 0; i < 80 && !found;) {
            if (gGlyphAge[i] == 0) {
                tex_queue_load(arg0, gGlyphTextures[i], 0);
                gGlyphIds[i] = arg0;
                found = 1;
            } else {
                i++;
            }
        }
    }
    ASSERT(found, 81);
    if (found) {
        gGlyphAge[i] = 3;
    }
    return gGlyphTextures[i];
}

/* Ages the glyph slots (once a frame) (0x8025B2B8). */
void font_age_cache(void) {
    s32 i;

    for (i = 0; i < 80; i++) {
        if (gGlyphAge[i] != 0) {
            gGlyphAge[i]--;
        }
    }
}

/* String length without the '*' characters (0x8025B300). */
s32 font_strlen(u8 *arg0) {
    s32 i;
    s32 n;

    i = 0;
    n = 0;
    if (arg0 != NULL) {
        for (; *(i + arg0) != 0; i++) {
            if (arg0[i] == '*') {
                n++;
            }
        }
    }
    return i - n;
}

/* Wide string length (0xFFE ends), 0x1000 codes not counted (0x8025B370). */
s32 font_wstrlen(u16 *arg0) {
    s32 i;
    s32 n;

    i = 0;
    n = 0;
    if (arg0 != NULL) {
        for (; arg0[i] != 0xFFE; i++) {
            if (arg0[i] == 0x1000) {
                n++;
            }
        }
    }
    return i - n;
}

/* 1 if the strings differ, else 0 (0x8025B3F0). */
s32 font_strcmp(u8 *a, u8 *b) {
    s32 i;
    s32 len;

    if ((len = font_strlen(a)) != font_strlen(b)) {
        return 1;
    }
    for (i = 0; i < len; i++) {
        if (a[i] != b[i]) {
            return 1;
        }
    }
    return 0;
}

/* The x that centres a string on x (0x8025B498). */
s16 font_center_x(s16 x, u16 scale, u8 *str, s32 arg3) {
    s32 unused;
    u16 result;
    register s32 len;

    unused = 0;
    len = font_strlen(str);
    result = (s32)(x - ((gFontAdvance[0] * (len - 1) + 1.0f) * scale) / 2.0);
    return result;
}

/* A u16 string into the narrow buffer (0x8025B558). */
u8 *font_narrow(u16 *arg0) {
    s32 i;

#ifdef NON_MATCHING
    /* `gNarrowTextBuf[i] = arg0[i++]` is unsequenced: IDO stores at the old i,
     * gcc at the new one (found by port/tools/compare.py: the text came out
     * empty natively) */
    for (i = 0; arg0[i] != 0 && i < 0xFF; i++) {
        gNarrowTextBuf[i] = arg0[i];
    }
#else
    for (i = 0; arg0[i] != 0 && i < 0xFF;) {
        gNarrowTextBuf[i] = arg0[i++];
    }
#endif
    gNarrowTextBuf[i] = 0;
    return gNarrowTextBuf;
}

/* Expands 0x1003 (a string) and 0x1004 (a number) (0x8025B5D4). */
u16 *font_format_wstr(u16 *dst, u16 *src, u16 *str, s32 num) {
    u8 buf[12];
    s32 i;
    s32 j;
    s32 k;

    i = 0;
    j = 0;
    do {
        switch (src[i]) {
            case 0x1003:
                for (k = 0; str[k] != 0xFFE; k++, j++) {
                    dst[j] = str[k];
                }
                break;
            case 0x1004:
                sprintf((char *) buf, "%d", num);
                for (k = 0; buf[k] != 0; k++, j++) {
                    dst[j] = buf[k] - 0x20;
                }
                break;
            default:
                dst[j] = src[i];
                j++;
                break;
        }
        i++;
    } while (src[i] != 0xFFE);
    dst[j] = 0xFFE;
    return dst;
}

/* An ASCII string to wide font codes (0x8025B7AC). */
u16 *font_to_wstr(u8 *str) {
    s32 i;

    for (i = 0; i < font_strlen(str); i++) {
        switch (str[i]) {
            case ' ':
                gWideTextBuf[i] = 0x1002;
                break;
            case '.':
                gWideTextBuf[i] = 0x3C;
                break;
            case '1':
            case '2':
            case '3':
            case '4':
                gWideTextBuf[i] = str[i] - 0x20;
                break;
            default:
                gWideTextBuf[i] = str[i] - 0x27;
                break;
            case '/':
                gWideTextBuf[i] = 0x3D;
                break;
            case ':':
                gWideTextBuf[i] = 0x3E;
                break;
        }
    }
    gWideTextBuf[i] = 0xFFE;
    return gWideTextBuf;
}

/* Appends a wide string (0x8025B918). */
void font_wstrcat(u16 *dst, u16 *src) {
    s32 i;
    s32 j;

    for (i = 0, j = font_wstrlen(dst); i < font_wstrlen(src); i++, j++) {
        dst[j] = src[i];
    }
    dst[j] = 0xFFE;
}
