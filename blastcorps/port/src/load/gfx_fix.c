/* Graphics data the load layer could not give the native layout: see
 * port_load.h.  Two kinds of areas hold it:
 *
 *  - WORD areas: display-list areas of runtime assets, swapped as u32 words
 *    by the schemas (the CPU walks and patches the commands as words).
 *    Whatever else they hold ends up in the emulator's layout: a Vtx
 *    {s16 x, y, z, flag, s, t; u8 r, g, b, a} becomes two halfword pairs and
 *    a byte-reversed colour word.  Example: the ninlogo scene (ROM 0x6EA850)
 *    is offsets, vertices and display lists in one block; its lists reach
 *    the vertices through segment 6, which only the running game knows.
 *  - RAW areas: the parts of the data images (hd_code and front end .data)
 *    that no C declaration or traced read typed, left as ROM bytes.  Some
 *    are display lists, vertices and matrices only the RSP reads (e.g. the
 *    front-end list at 0x80208FA8 that a level's lists call).
 *
 * Before the renderer runs a task, port_gfx_fix_task follows its display
 * lists with the real segment table and converts, inside those areas only,
 * each word once per load (a per-word map):
 *                         WORD area              RAW area
 *   commands              as is                  bswap32 (before reading)
 *   G_VTX vertices        halves 0-5, colour     each halfword
 *                         bytes back
 *   G_MTX matrices        as is                  bswap32
 *   viewport (MOVEMEM)    halfword pairs         each halfword
 *   lights, lookAt        bytes back             as is
 *   texels, palettes      bytes back (bswap32)   as is
 * Data the game builds itself is outside every area and never touched. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "platform/port_cc.h"
#include "rdram.h"
#include "port_load.h"

int port_gfx_debug;

#define MAX_AREAS 256

enum { AREA_WORDS, AREA_RAW };

typedef struct {
    uint32_t lo, hi;   /* physical [lo, hi), word aligned */
    int kind;
    uint32_t *orig;    /* the words as loaded: a word is converted only while
                        * it still holds them (once converted it doesn't, and
                        * memory the game reused for something else is left
                        * alone) */
} Area;

static Area g_area[MAX_AREAS];
static int g_nareas;

static uint32_t phys(uint32_t a) {
    return a & 0x00FFFFFFu;
}

void port_gfx_forget(uint32_t addr, uint32_t len) {
    uint32_t lo = phys(addr), hi = lo + len;
    int i;
    for (i = 0; i < g_nareas; i++) {
        if (g_area[i].lo < hi && lo < g_area[i].hi) {
            free(g_area[i].orig);
            g_area[i] = g_area[--g_nareas];
            i--;
        }
    }
}

static void add_area(uint32_t addr, uint32_t len, int kind) {
    uint32_t lo = (phys(addr) + 3) & ~3u, hi = (phys(addr) + len) & ~3u;
    if (hi <= lo || hi > RDRAM_SIZE) return;
    port_gfx_forget(lo, hi - lo);
    if (g_nareas == MAX_AREAS) {   /* the oldest goes */
        free(g_area[0].orig);
        memmove(&g_area[0], &g_area[1], (MAX_AREAS - 1) * sizeof(Area));
        g_nareas--;
    }
    g_area[g_nareas].lo = lo;
    g_area[g_nareas].hi = hi;
    g_area[g_nareas].kind = kind;
    g_area[g_nareas].orig = malloc(hi - lo);
    if (g_area[g_nareas].orig == NULL) return;
    memcpy(g_area[g_nareas].orig, (void *) (uintptr_t) (RDRAM_BASE + lo), hi - lo);
    g_nareas++;
    if (port_gfx_debug && getenv("BC_GFX_WATCH") != NULL) {
        uint32_t wa = (uint32_t) strtoul(getenv("BC_GFX_WATCH"), NULL, 16) & 0x00FFFFFFu;
        if (lo <= wa && wa < hi)
            fprintf(stderr, "gfx_fix: watch %08X: new %s area %08X-%08X (word there %08X)\n", (unsigned) wa,
                    kind == AREA_WORDS ? "word" : "raw", (unsigned) lo, (unsigned) hi,
                    (unsigned) *(uint32_t *) (uintptr_t) (RDRAM_BASE + wa));
    }
}

/* is ADDR inside a RAW area (data-image bytes left as the ROM has them, which
 * only bc.exe's renderer converts)?  swap.c's display-list units skip them.
 * With W, the word there as loaded (host value of the ROM's big-endian word),
 * the same in bc_headless and bc.exe whether or not the renderer converted it */
int port_gfx_in_raw(uint32_t addr, uint32_t *w) {
    uint32_t a = phys(addr);
    int i;
    for (i = 0; i < g_nareas; i++)
        if (g_area[i].kind == AREA_RAW && g_area[i].lo <= a && a < g_area[i].hi) {
            if (w != NULL) *w = g_area[i].orig ? PORT_BSWAP32(g_area[i].orig[((a & ~3u) - g_area[i].lo) >> 2]) : 0;
            return 1;
        }
    return 0;
}

void port_gfx_word_area(uint32_t addr, uint32_t len) {
    add_area(addr, len, AREA_WORDS);
}

void port_gfx_raw_area(uint32_t addr, uint32_t len) {
    add_area(addr, len, AREA_RAW);
}

/* what is being converted */
enum { OBJ_CMD, OBJ_VTX, OBJ_MTX, OBJ_VIEWPORT, OBJ_LIGHT, OBJ_TEXELS };

static uint32_t g_converted;
static uint32_t g_watch = 1;   /* debugging: BC_GFX_WATCH=physical address (with --gfx-fix-log) */

static uint32_t *word_at(uint32_t pa) {
    return (uint32_t *) (uintptr_t) (RDRAM_BASE + pa);
}

static uint32_t halves(uint32_t v) {
    return (v >> 16) | (v << 16);
}

static uint32_t bytes_in_halves(uint32_t v) {
    return ((v & 0x00FF00FFu) << 8) | ((v >> 8) & 0x00FF00FFu);
}

/* the native form of loaded word V of an object in an area of KIND */
static uint32_t native_word(int kind, int obj, int colour, uint32_t v) {
    if (kind == AREA_WORDS) {
        switch (obj) {
            case OBJ_VTX: return colour ? PORT_BSWAP32(v) : halves(v);
            case OBJ_VIEWPORT: return halves(v);
            case OBJ_LIGHT:
            case OBJ_TEXELS: return PORT_BSWAP32(v);
            default: return v;   /* commands, matrices: words already */
        }
    }
    switch (obj) {
        case OBJ_CMD:
        case OBJ_MTX: return PORT_BSWAP32(v);
        case OBJ_VTX: return colour ? v : bytes_in_halves(v);
        case OBJ_VIEWPORT: return bytes_in_halves(v);
        default: return v;   /* lights, texels: bytes already */
    }
}

/* convert the words of [lo, hi) that lie in an area and still hold what
 * was loaded; for vertices, lo is the first vertex */
static void convert(uint32_t lo, uint32_t hi, int obj) {
    int i;
    lo &= ~3u;
    hi = (hi + 3) & ~3u;
    if (hi > RDRAM_SIZE || lo >= hi) return;
    for (i = 0; i < g_nareas; i++) {
        Area *ar = &g_area[i];
        uint32_t a = lo > ar->lo ? lo : ar->lo, b = hi < ar->hi ? hi : ar->hi;
        for (; a < b; a += 4) {
            uint32_t *w = word_at(a), v = *w, loaded = ar->orig[(a - ar->lo) / 4];
            int colour = obj == OBJ_VTX && ((a - lo) & 0xC) == 0xC;
            uint32_t nv = native_word(ar->kind, obj, colour, loaded);
            if (v == loaded && nv != v) {
                *w = nv;
                g_converted += 4;
                if (port_gfx_debug && a == g_watch)
                    fprintf(stderr, "gfx_fix: watch %08X converted as obj %d (range %08X-%08X, %s area %08X-%08X): "
                            "%08X -> %08X\n", (unsigned) a, obj, (unsigned) lo, (unsigned) hi,
                            ar->kind == AREA_WORDS ? "word" : "raw", (unsigned) ar->lo, (unsigned) ar->hi,
                            (unsigned) v, (unsigned) nv);
            } else if (v != loaded && v != nv && port_gfx_debug) {
                fprintf(stderr, "gfx_fix: %08X (obj %d, %s area %08X) changed by the game: %08X, loaded %08X\n",
                        (unsigned) a, obj, ar->kind == AREA_WORDS ? "word" : "raw", (unsigned) ar->lo,
                        (unsigned) v, (unsigned) loaded);
            }
        }
    }
}

static int in_any_area(uint32_t lo, uint32_t hi) {
    int i;
    for (i = 0; i < g_nareas; i++)
        if (g_area[i].lo < hi && lo < g_area[i].hi) return 1;
    return 0;
}

uint32_t port_gfx_fix_task(uint32_t dl) {
    uint32_t seg[16], stack[10], pc = phys(dl);
    uint32_t timg = 0, timg_siz = 0, timg_width = 0;
    int sp = 0, steps = 0;
    if (g_nareas == 0) return 0;
    if (g_watch == 1) {
        const char *s = getenv("BC_GFX_WATCH");
        g_watch = s != NULL ? (uint32_t) strtoul(s, NULL, 16) & 0x00FFFFFFu : 0xFFFFFFFFu;
    }
    memset(seg, 0, sizeof seg);
    g_converted = 0;
#define SEGADDR(a) (phys(seg[((a) >> 24) & 15] + ((a) & 0x00FFFFFFu)) & (RDRAM_SIZE - 1))
    while (steps++ < 200000 && pc + 8 <= RDRAM_SIZE) {
        uint32_t w0, w1;
        convert(pc, pc + 8, OBJ_CMD);   /* a command in a raw area: host order first */
        w0 = word_at(pc)[0];
        w1 = word_at(pc)[1];
        pc += 8;
        switch (w0 >> 24) {
            case 0x01: {   /* G_MTX */
                uint32_t a = SEGADDR(w1);
                convert(a, a + 64, OBJ_MTX);
                break;
            }
            case 0x03: {   /* G_MOVEMEM: index, size */
                uint32_t idx = (w0 >> 16) & 0xFF, size = w0 & 0xFFFF, a = SEGADDR(w1);
                if (idx == 0x80) convert(a, a + size, OBJ_VIEWPORT);
                else if (idx >= 0x82 && idx <= 0x94) convert(a, a + size, OBJ_LIGHT);   /* lookAt, lights */
                else if (idx == 0x9E) convert(a, a + 64, OBJ_MTX);                       /* forced MVP */
                break;
            }
            case 0x04: {   /* G_VTX */
                uint32_t a = SEGADDR(w1), size = w0 & 0xFFFF;
                convert(a, a + size, OBJ_VTX);
                break;
            }
            case 0x06:     /* G_DL: call or branch */
                if (((w0 >> 16) & 1) == 0) {
                    if (sp == 10) return g_converted;
                    stack[sp++] = pc;
                }
                pc = SEGADDR(w1);
                break;
            case 0xB8:     /* G_ENDDL */
                if (sp == 0) return g_converted;
                pc = stack[--sp];
                break;
            case 0xBC:     /* G_MOVEWORD G_MW_SEGMENT */
                if ((w0 & 0xFF) == 6) seg[((w0 >> 8) & 0xFFFF) / 4 & 15] = w1;
                break;
            case 0xFD:     /* G_SETTIMG */
                timg = SEGADDR(w1);
                timg_siz = (w0 >> 19) & 3;
                timg_width = (w0 & 0xFFF) + 1;
                break;
            case 0xF3: {   /* G_LOADBLOCK: lrs + 1 texels from uls */
                uint32_t uls = (w0 >> 12) & 0xFFF, lrs = (w1 >> 12) & 0xFFF;
                uint32_t a = timg + ((uls << timg_siz) >> 1), n = ((lrs - uls + 1) << timg_siz) >> 1;
                if (timg && lrs >= uls && in_any_area(a, a + n)) convert(a, a + n, OBJ_TEXELS);
                break;
            }
            case 0xF4: {   /* G_LOADTILE: a rectangle (10.2 coordinates) */
                uint32_t uls = ((w0 >> 12) & 0xFFF) >> 2, ult = (w0 & 0xFFF) >> 2;
                uint32_t lrs = ((w1 >> 12) & 0xFFF) >> 2, lrt = (w1 & 0xFFF) >> 2;
                uint32_t row = (timg_width << timg_siz) >> 1, t;
                if (!timg || lrs < uls || lrt < ult) break;
                for (t = ult; t <= lrt; t++) {
                    uint32_t a = timg + t * row + ((uls << timg_siz) >> 1);
                    uint32_t n = ((lrs - uls + 1) << timg_siz) >> 1;
                    if (in_any_area(a, a + n)) convert(a, a + n, OBJ_TEXELS);
                }
                break;
            }
            case 0xF0: {   /* G_LOADTLUT: count 16-bit entries */
                uint32_t n = (((w1 >> 14) & 0x3FF) + 1) * 2;
                if (timg && in_any_area(timg, timg + n)) convert(timg, timg + n, OBJ_TEXELS);
                break;
            }
            default:
                break;
        }
    }
    return g_converted;
#undef SEGADDR
}
