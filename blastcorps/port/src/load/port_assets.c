/* Runtime assets: byte order on arrival (see port_load.h).
 *
 * Every ROM -> RDRAM transfer reaches port_on_dma (platform PI layer) and,
 * when the game decompresses a block, port_on_load (hd_rom_load and 5CB60's
 * packed-object loader).  The ROM start identifies the asset; its kind says at
 * which of the two points it is swapped and how.  The schemas come from the
 * game's structures and were checked against the widths the code reads each
 * asset at (port/tools/m64widths.py over the attract demos; port/README.md).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "rdram.h"
#include "load/port_load.h"
#include "load/port_mods.h"
#include "platform/port_dma.h"

int port_load_verbose;

#define P(a) ((uint8_t *) (uintptr_t) (a))

static inline uint32_t rd32(uint32_t a) {
    uint32_t v;
    memcpy(&v, P(a), 4);
    return v;
}
static inline uint16_t rd16(uint32_t a) {
    uint16_t v;
    memcpy(&v, P(a), 2);
    return v;
}
static void sw16(uint32_t a, uint32_t n) { port_bswap_n(P(a), n, 2); }
static void sw32(uint32_t a, uint32_t n) { port_bswap_n(P(a), n, 4); }

/* Records of STRIDE bytes over [a, a + len): LEAVES is "W@OFF ..." (hex
 * offsets), the multi-byte fields of one record. */
typedef struct {
    uint8_t n;
    struct { uint16_t off; uint8_t w; } f[64];
} Leaves;

static void parse_leaves(const char *s, Leaves *l) {
    unsigned w, off;
    int n;
    for (l->n = 0; l->n < 64 && sscanf(s, " %u@%x%n", &w, &off, &n) == 2; s += n, l->n++) {
        l->f[l->n].off = (uint16_t) off;
        l->f[l->n].w = (uint8_t) w;
    }
}

static void swap_leaves(uint32_t a, const char *leaves) {
    Leaves l;
    int i;
    parse_leaves(leaves, &l);
    for (i = 0; i < l.n; i++) port_bswap_n(P(a + l.f[i].off), 1, l.f[i].w);
}

static void swap_records(uint32_t a, uint32_t len, uint32_t stride, const char *leaves) {
    Leaves l;
    uint32_t r;
    int i;
    parse_leaves(leaves, &l);
    for (r = 0; r + stride <= len; r += stride)
        for (i = 0; i < l.n; i++) port_bswap_n(P(a + r + l.f[i].off), 1, l.f[i].w);
}

/* ------------------------------------------------------------------ kinds */

enum {
    K_NONE,      /* stays big-endian (compressed input, byte data, RSP-only data) */
    K_FE,        /* the front-end overlay (text + data image) */
    K_TEXTAB,    /* ROM 0x4CE0: the texture/entry table, 8-byte {u32 rom, u16 size, u16 type} */
    K_TEXTURE,   /* an entry of that table: packed texture, see port_texture_input/output */
    K_PKTAB,     /* ROM 0x6EC4C0: u32 offsets of the packed objects */
    K_BANK,      /* .ctl sound bank (ALBankFile) */
    K_SEQFILE,   /* ALSeqFile (s16 revision, s16 count, {u32 offset, s32 len}[]) */
    K_TUNE,      /* compressed MIDI sequence: ALCMidiHdr (17 words) + bytes */
    K_LEVEL,     /* a level: level data + its display lists */
    K_PACKED,    /* a packed object (building/vehicle model, 5CB60) */
    K_MODEL,     /* a vehicle model pair (*.raw + *_dl.raw) */
    K_STATIC,    /* the static segment (segment 1) */
    K_DEMOS,     /* the attract-mode recordings */
    K_IMAGE,     /* textures/images loaded whole (title, world textures): texels and packed tokens */
    K_SCENE,     /* front-end scenes (usa_star, ninlogo, reflectlogo): offsets + display lists */
    K_UNKNOWN,
    K_COUNT
};

static const char *kind_name[K_COUNT] = {
    "none", "front end", "texture table", "texture", "packed table", "sound bank", "sequence file",
    "tune", "level", "packed object", "model", "static segment", "demos", "image", "scene", "unknown",
};

static struct {
    uint32_t loads, bytes, unswapped;
} stats[K_COUNT];

/* ROM map (us v1.1).  Ranges are [lo, hi). */
#define ROM_TEXTAB      0x004CE0u
#define ROM_TEXTURES_LO 0x00CCE0u
#define ROM_TEXTURES_HI 0x350950u
#define ROM_MUSBANK     0x350950u
#define ROM_MUSTBL      0x3539A0u
#define ROM_SFXBANK     0x3A1920u
#define ROM_SFXTBL      0x3A48C0u
#define ROM_SEQFILE     0x44F5C0u
#define ROM_SEQ_HI      0x487050u
#define ROM_IMAGES_LO   0x487050u   /* titlelogo, titlepicture, nink, 64k, copyright */
#define ROM_MODELS_LO   0x48FE90u   /* vehicle models: *.raw / *_dl.raw */
#define ROM_LEVELS_LO   0x4A5660u
#define ROM_LEVELS_HI   0x66C900u
#define ROM_WORLDTEX_LO 0x66C900u   /* world, amber, traffic, controller textures */
#define ROM_DEMOS       0x6A9F10u
#define ROM_LOGOS_LO    0x6E8980u   /* usa_star, ninlogo, reflectlogo */
#define ROM_PKTAB       0x6EC4C0u
#define ROM_PACKED_LO   0x6ECCC0u
#define ROM_STATIC      0x787F40u
#define ROM_STATIC_END  0x788000u
#define ROM_FE          0x7E3AD0u

/* the front end's ROM start also comes from the game's own pointer
 * gFeRomStartPtr (46C20.c; it differs in the NM test ROM) */
static uint32_t fe_rom(void) {
    uint32_t p = rd32(0x802FDB30u);
    return (p >= RDRAM_BASE && p < RDRAM_BASE + RDRAM_SIZE) ? rd32(p) : ROM_FE;
}

static int classify(uint32_t rom, uint32_t len) {
    (void) len;
    if (rom == ROM_FE || rom == fe_rom()) return K_FE;
    if (rom == ROM_TEXTAB) return K_TEXTAB;
    if (rom >= ROM_TEXTURES_LO && rom < ROM_TEXTURES_HI) return K_TEXTURE;
    if (rom == ROM_MUSBANK || rom == ROM_SFXBANK) return K_BANK;
    if (rom == ROM_SEQFILE) return K_SEQFILE;
    if (rom > ROM_SEQFILE && rom < ROM_SEQ_HI) return K_TUNE;
    if (rom >= ROM_TEXTURES_HI && rom < ROM_SEQFILE) return K_NONE; /* .tbl samples: RSP only */
    if (rom >= ROM_IMAGES_LO && rom < ROM_MODELS_LO) return K_IMAGE;
    if (rom >= ROM_MODELS_LO && rom < ROM_LEVELS_LO) return K_MODEL;
    if (rom >= ROM_LEVELS_LO && rom < ROM_LEVELS_HI) return K_LEVEL;
    if (rom >= ROM_WORLDTEX_LO && rom < ROM_DEMOS) return K_IMAGE;
    if (rom == ROM_DEMOS) return K_DEMOS;
    if (rom >= ROM_DEMOS && rom < ROM_LOGOS_LO) return K_IMAGE;    /* FE background pictures */
    if (rom >= ROM_LOGOS_LO && rom < ROM_PKTAB) return K_SCENE;
    if (rom == ROM_PKTAB) return K_PKTAB;
    if (rom >= ROM_PACKED_LO && rom < ROM_STATIC) return K_PACKED;
    if (rom == ROM_STATIC) return K_STATIC;
    return K_UNKNOWN;
}

/* ------------------------------------------------------------- sound bank */

/* alBnkfNew turns the .ctl's offsets into pointers in place; it reads the
 * structures natively, so they are swapped field by field, walking the file
 * the way alBnkfNew does.  Envelopes, key maps, wave tables, loops and books
 * are shared between sounds: a byte map makes every object swap once. */
typedef struct {
    uint32_t base, len;
    uint8_t *done;
} Bank;

static int bank_once(Bank *b, uint32_t off, uint32_t size) {
    /* (no wrap-around: an offset near 4 GB must not pass as off + size <= len) */
    if (off == 0 || off >= b->len || size > b->len - off || b->done[off]) return 0;
    b->done[off] = 1;
    return 1;
}

static void bank_wavetable(Bank *b, uint32_t off) {
    uint32_t a = b->base + off, loop, book;
    uint8_t type;
    if (!bank_once(b, off, 0x14)) return;
    sw32(a, 2);           /* base (offset into the .tbl), len */
    type = P(a)[8];       /* type, flags: bytes */
    sw32(a + 0xC, 2);     /* loop, book (ADPCM) / loop (raw) */
    loop = rd32(a + 0xC);
    book = rd32(a + 0x10);
    if (type == 0) {      /* AL_ADPCM_WAVE */
        if (bank_once(b, loop, 0x2C)) {
            sw32(b->base + loop, 3);
            sw16(b->base + loop + 0xC, 16);
        }
        if (bank_once(b, book, 8)) {
            uint32_t order, npred;
            sw32(b->base + book, 2);
            order = rd32(b->base + book);
            npred = rd32(b->base + book + 4);
            if ((uint64_t) order * npred * 8 * 2 + 8 + book <= b->len)
                sw16(b->base + book + 8, order * npred * 8);
        }
    } else if (bank_once(b, loop, 0xC)) {   /* AL_RAW16_WAVE */
        sw32(b->base + loop, 3);
    }
}

static void bank_sound(Bank *b, uint32_t off) {
    uint32_t a = b->base + off;
    if (!bank_once(b, off, 0x10)) return;
    sw32(a, 3);           /* envelope, keyMap, wavetable offsets; pan/volume/flags bytes */
    if (bank_once(b, rd32(a), 0xE)) sw32(b->base + rd32(a), 3);   /* ALEnvelope times */
    /* ALKeyMap (rd32(a + 4)) is bytes */
    bank_wavetable(b, rd32(a + 8));
}

static void bank_instrument(Bank *b, uint32_t off) {
    uint32_t a = b->base + off, n, i;
    if (!bank_once(b, off, 0x10)) return;
    sw16(a + 0xC, 2);     /* bendRange, soundCount */
    n = (uint16_t) rd16(a + 0xE);
    if ((uint64_t) 0x10 + n * 4 + off > b->len) return;
    sw32(a + 0x10, n);
    for (i = 0; i < n; i++) bank_sound(b, rd32(a + 0x10 + i * 4));
}

static void swap_bank(uint32_t base, uint32_t len) {
    Bank b;
    uint32_t nbanks, i, j;
    b.base = base;
    b.len = len;
    b.done = calloc(len + 1, 1);
    sw16(base, 2);        /* revision, bankCount */
    nbanks = rd16(base + 2);
    sw32(base + 4, nbanks);
    for (i = 0; i < nbanks; i++) {
        uint32_t off = rd32(base + 4 + i * 4), a = base + off, ninst;
        if (!bank_once(&b, off, 0x10)) continue;
        sw16(a, 1);       /* instCount; flags, pad bytes */
        sw32(a + 4, 2);   /* sampleRate, percussion */
        ninst = (uint16_t) rd16(a);
        if ((uint64_t) off + 0xC + ninst * 4 > len) continue;
        sw32(a + 0xC, ninst);
        if (rd32(a + 8)) bank_instrument(&b, rd32(a + 8));
        for (j = 0; j < ninst; j++)
            if (rd32(a + 0xC + j * 4)) bank_instrument(&b, rd32(a + 0xC + j * 4));
    }
    free(b.done);
}

/* --------------------------------------------------------------- textures */

/* The packed texture tokens are s16 (types 1-6); type 0 is stored bytes. */
void port_texture_input(uint8_t *data, uint32_t size, int32_t type) {
    if (type >= 1 && type <= 6) port_bswap_n(data, size / 2, 2);
}

/* Decoded texels: the decoders store u16 (types 1, 4) or u32 (2, 5) values
 * natively; the renderer reads texture memory as big-endian bytes. */
void port_texture_output(uint8_t *dst, uint32_t len, int32_t type) {
    if (port_load_verbose)
        fprintf(stderr, "load: decode rom 000000 -> %08X len %X: texture type %d\n", (unsigned) (uintptr_t) dst,
                (unsigned) len, (int) type);
    switch (type) {
        case 1:
        case 4:
            port_bswap_n(dst, len / 2, 2);
            break;
        case 2:
        case 5:
            port_bswap_n(dst, len / 4, 4);
            break;
    }
    port_unit_mark(dst, len, 1);   /* (big-endian bytes now) */
}

/* --------------------------------------------------------------- sequences */

/* ALSeqFile: s16 revision, s16 seqCount, then {u32 offset, s32 len} per
 * sequence.  The game loads the first 4 bytes alone (to get the count), then
 * the whole table. */
static void swap_seqfile(uint32_t a, uint32_t len) {
    if (len < 4) return;
    sw16(a, 2);
    sw32(a + 4, (len - 4) / 4);
}

/* compressed MIDI (alCSeqNew): ALCMidiHdr = 16 track offsets + division, then
 * the event bytes */
static void swap_tune(uint32_t a, uint32_t len) {
    if (len >= 0x44) sw32(a, 17);
}

/* -------------------------------------------------------- level and models */

#include "load/schemas.h"

/* ----------------------------------------------------------------- hooks */

static int apply(int kind, uint32_t dst, uint32_t rom, uint32_t len, int at_load) {
    switch (kind) {
        case K_FE:              /* inflated by hd_load_front_end (hd_rom_load) */
            if (!at_load) return 0;
            port_load_image_fe();
            return 1;
        case K_TEXTAB:          /* tex_load_table: one raw DMA */
            if (at_load) return 0;
            swap_records(dst, len, 8, "4@0 2@4 2@6");
            return 1;
        case K_PKTAB:           /* lvl_load_object_table: one raw DMA */
            if (at_load) return 0;
            sw32(dst, len / 4);
            return 1;
        case K_BANK:            /* hd_rom_load, Rare LZSS (twice: size probe, then the heap copy) */
            if (!at_load) return 0;
            swap_bank(dst, len);
            return 1;
        case K_SEQFILE:         /* hd_rom_load uncompressed: 4 bytes, then the table */
            if (!at_load) return 0;
            swap_seqfile(dst, len);
            return 1;
        case K_TUNE:            /* music_play_tune: hd_rom_load uncompressed */
            if (!at_load) return 0;
            swap_tune(dst, len);
            return 1;
        case K_LEVEL:
        case K_PACKED:
        case K_MODEL:
        case K_STATIC:
        case K_DEMOS:
        case K_SCENE:
            if (!at_load) return 0;
            return schema_apply(kind, dst, rom, len);
        default:
            return 0;
    }
}

static void handle(uint32_t dst, uint32_t rom, uint32_t len, int at_load) {
    int kind = classify(rom, len);
    int done;
    port_gfx_forget(dst, len);   /* graphics areas the new data replaces */
    if (dst >= 0x8021ED00u && dst < 0x80244000u && !at_load) return;   /* compressed staging */
    port_unit_mark((void *) (uintptr_t) dst, len, 1);   /* ROM bytes; the schema's swaps re-mark */
    done = apply(kind, dst, rom, len, at_load);
    if (done) {
        stats[kind].loads++;
        stats[kind].bytes += len;
    } else if (kind != K_NONE && kind != K_TEXTURE && kind != K_IMAGE) {
        stats[kind].unswapped++;
    }
    if (port_load_verbose)
        fprintf(stderr, "load: %s rom %06X -> %08X len %X: %s%s\n", at_load ? "load" : "dma ", rom, dst, len,
                kind_name[kind], done ? "" : " (as is)");
}

void port_on_dma(uint32_t dst, uint32_t rom, uint32_t len) {
    /* a mod's texture slots: the bytes a ROM holding them would give (port_mods.h; none: nothing done) */
    if (port_rom_patches) port_rom_overlay(P(dst), rom, len);
    handle(dst, rom, len, 0);
}

uint32_t port_dma_len(uint32_t rom, uint32_t asked, uint32_t len) {
    return port_rom_patches ? port_rom_grown(rom, asked, len) : len;
}

void port_on_load(uint32_t rom, uint32_t dst, uint32_t len) {
    handle(dst, rom, len, 1);
}

void port_load_report(void) {
    int k;
    fprintf(stderr, "byte order on load:\n");
    for (k = 0; k < K_COUNT; k++)
        if (stats[k].loads || stats[k].unswapped)
            fprintf(stderr, "  %-16s %5u swapped (%u bytes), %u left as is\n", kind_name[k], stats[k].loads,
                    stats[k].bytes, stats[k].unswapped);
}
