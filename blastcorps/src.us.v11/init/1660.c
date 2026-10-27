/* init 0x1660: the gzip member reader around inflate (kiunzip, the header
 * parser, the bit reverser, the buffer reset). */
#include "common.h"
#include <ultra64.h>
extern s32 method;
extern s32 gInflateHuftArena;
extern u8 *inbuf;
extern s32 slide;
extern s32 header_bytes;
extern s32 exit_code;
extern s32 gGzipUnknownMethodMsg[];
extern s32 bytes_in;
extern s32 bytes_out;
extern s32 insize;
extern s32 inptr;
extern s32 outcnt;

void gzip_clear_bufs(void);
s32  gzip_get_method(void);
extern s32 inflate(void);
extern void init_debug_printf(void *arg0, s32 arg1);
/* Inflates the gzip member at *arg0 into *arg1 (arg2: the huft arena), skipping one leading pad byte, then
 * advances both pointers; the game's "kiunzip" (0x80220360). */
void gzip_unzip(s32 *arg0, s32 *arg1, s32 arg2) {
    inbuf = *arg0;
    slide = *arg1;
    gInflateHuftArena = arg2;

    gzip_clear_bufs();

    if (*(inbuf + inptr) != 0x1F) /* a pad byte before the member */
        inptr += 1;
    method = gzip_get_method();
    if (method >= 0) {
        inflate();
        *arg0 += inptr;
        *arg1 += outcnt;
    }
}

#define GZIP_FHCRC 2
#define GZIP_FEXTRA 4
#define GZIP_FNAME 8
#define GZIP_FCOMMENT 0x10

/* gzip_get_method: parses a GZIP member header (RFC 1952) starting 2 bytes
 * into the stream (the caller/gzip_unzip already looked at, but did not
 * consume, the first magic byte). Skips the second magic byte, reads the
 * compression method (must be 8/DEFLATE, else reports an error via
 * init_debug_printf and returns -1), reads the FLG flags byte, skips the fixed
 * MTIME+XFL+OS fields, then conditionally skips FHCRC/FEXTRA/FNAME/FCOMMENT
 * based on the flag bits - same structure as GNU gzip's own get_method()
 * (gzip.c), but trimmed to pure skip-only logic: no CRC verification, no
 * filename extraction, no encrypted/reserved-flag rejection, since Rare's
 * own compressor guarantees well-formed headers (same pattern confirmed
 * throughout the INFLATE implementation in init/0050.c/init/0E30.c).
 *
 * `*(inptr + inbuf)` vs `inbuf[inptr]` vs
 * `*(inbuf + inptr)` below are NOT a style inconsistency - each
 * spelling was needed to get IDO to pick the exact operand order the target
 * ROM uses for that specific access's address-add instruction. Addition is
 * commutative so all three compute the same value; don't "clean up" the
 * mixed forms without re-diffing against the target afterward.
 */
s32 gzip_get_method(void) {
    u8 flags;
    u32 xlen;

    inptr += 2;
    method = -1;
    header_bytes = 0;
    method = *(inptr + inbuf);
    inptr += 1;

    if (method != 8) {
        init_debug_printf(gGzipUnknownMethodMsg, method);
        exit_code = 1;
        return -1;
    }

    flags = *(inptr + inbuf);
    inptr += 1;
    inptr += 6;

    if (flags & GZIP_FHCRC) {
        inptr += 2;
    }

    if (flags & GZIP_FEXTRA) {
        xlen = *(inptr + inbuf);
        inptr += 1;
        xlen |= *(inbuf + inptr) << 8;
        inptr += 1;
        inptr += xlen;
    }

    if (flags & GZIP_FNAME) {
        while (inbuf[inptr++] != 0) {
        }
    }

    if (flags & GZIP_FCOMMENT) {
        while (inbuf[inptr++] != 0) {
        }
    }

    header_bytes = inptr + 8;
    return method;
}

/* Reverses the low `arg1` bits of `arg0` (canonical Huffman codes are
 * MSB-first, the bitstream LSB-first). arg2 is a dead parameter reused as the
 * accumulator; hd_code's copy (17A70) is byte-identical. */
u32 reverse_bits(u32 arg0, s32 arg1, u32 arg2) {
    s32 pad;

    arg2 = 0;
    do {
        arg2 |= arg0 & 1;
        arg0 >>= 1, arg2 <<= 1;
        arg1--;
    } while (arg1 > 0);
    return arg2 >> 1;
}

/* gzip's clear_bufs: zeroes outcnt, inptr and the byte counters before a new member (0x802206D0). */
void gzip_clear_bufs(void) {
    outcnt = 0;
    inptr = 0;
    insize = 0;
    bytes_out = 0;
    bytes_in = 0;
}
