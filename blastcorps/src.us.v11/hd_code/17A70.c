#include "common.h"
#include <ultra64.h>
#include "game/game.h"

/* kiunzip: a second copy of the gzip front end in init/1660.c, used by the
 * game to inflate assets at run time (inflate itself is hd_inflate). */

extern s32 hd_header_bytes;  /* header bytes */
extern s32 hd_method;  /* compression method */
extern s32 hd_exit_code;  /* exit code */

void kiunzip_clear_bufs(void);
s32 kiunzip_get_method(void);

extern s32 hd_insize;
extern s32 hd_bytes_out;
extern s32 hd_bytes_in;

#define GZIP_FHCRC 2
#define GZIP_FEXTRA 4
#define GZIP_FNAME 8
#define GZIP_FCOMMENT 0x10

/* Inflates the gzip member at *src into *dst (window work), then advances both pointers past the input read
 * and the output written; the run-time copy of init's gzip front end (0x8025C230).
 * Inflates the gzip stream at *arg0 into *arg1, then advances both
 * pointers past the consumed input and the produced output. */
void kiunzip(u8 * N64P *arg0, u8 * N64P *arg1, s32 arg2) {
    hd_inbuf = *arg0;
    hd_slide = *arg1;
    gHdInflateHuftArena = arg2;

    kiunzip_clear_bufs();

    if (*(hd_inbuf + hd_inptr) != 0x1F) {
        hd_inptr += 1;
    }
    hd_method = kiunzip_get_method();
    if (hd_method >= 0) {
        hd_inflate();
        *arg0 += hd_inptr;
        *arg1 += hd_outcnt;
    }
}

/* gzip get_method: skips the magic, checks method 8 ("kiunzip: unknown method %d"), skips the flags, mtime,
 * crc, extra, name and comment fields (0x8025C30C).
 * get_method: parses the gzip member header and returns the method (8). */
s32 kiunzip_get_method(void) {
    u8 flags;
    u32 xlen;

    hd_inptr += 2;
    hd_method = -1;
    hd_header_bytes = 0;
    hd_method = *(hd_inptr + hd_inbuf);
    hd_inptr += 1;

    if (hd_method != 8) {
        debug_printf("kiunzip: unknown method %d -- get newer version of gzip\n", hd_method);
        hd_exit_code = 1;
        return -1;
    }

    flags = *(hd_inptr + hd_inbuf);
    hd_inptr += 1;
    hd_inptr += 6;

    if (flags & GZIP_FHCRC) {
        hd_inptr += 2;
    }

    if (flags & GZIP_FEXTRA) {
        xlen = *(hd_inptr + hd_inbuf);
        hd_inptr += 1;
        xlen |= *(hd_inbuf + hd_inptr) << 8;
        hd_inptr += 1;
        hd_inptr += xlen;
    }

    /* !! (not plain != 0) gives the target's materialized boolean in $s0 */
    if (flags & GZIP_FNAME) {
        while (!!hd_inbuf[hd_inptr++]) {
        }
    }

    if (flags & GZIP_FCOMMENT) {
        while (!!hd_inbuf[hd_inptr++]) {
        }
    }

    hd_header_bytes = hd_inptr + 8;
    return hd_method;
}

/* Reverses the low n bits of code (gzip's bi_reverse); no callers (0x8025C56C).
 * Reverses the low arg1 bits of arg0. */
u32 kiunzip_reverse_bits(u32 arg0, s32 arg1, u32 arg2) {
    s32 pad;

    arg2 = 0;
    do {
        arg2 |= arg0 & 1;
        arg0 >>= 1, arg2 <<= 1;
        arg1--;
    } while (arg1 > 0);
    return arg2 >> 1;
}

/* gzip clear_bufs: zeroes the input and output positions and the byte counters (0x8025C5A0). */
void kiunzip_clear_bufs(void) {
    hd_outcnt = 0;
    hd_inptr = 0;
    hd_insize = 0;
    hd_bytes_out = 0;
    hd_bytes_in = 0;
}
