/* loadcheck: the byte-order layer (port/src/load/) against the original code.
 *
 * Real game C (NON_MATCHING, native) loads real assets from the user's ROM
 * through the game's own loaders (hd_rom_load, lvl_load_file, 5BF40's
 * texture loads), with osPiStartDma emulated here the way the platform layer
 * does it (copy from the ROM, then port_on_dma).  The same cases run through
 * the ORIGINAL MIPS code in unicorn on big-endian memory
 * (port/tools/loadref.py); `make -C port loadcheck` compares the two outputs
 * line by line.
 *
 *   T5  port_802E8BF8_word: the gUseEeprom flag/word-table alias (00000.c)
 *   T6  texture entries: DMA + tex_decode decode, output bytes (BE) CRC
 *   T7  every level: lvl_load_file load + lvl_read_alpha_zones object-stream unpack
 *   T8  every level: cam_apply_height_zone height zones at random positions
 *   T9  (needs a trace) every read the N64 code made of each traced asset
 *   TA  every level: dig_coach_update's panel events (section 0x58) as the
 *       carrier's z sweeps the s16 range: the z each event fires at
 *   TB  every level, the ROM's file and the same level with its derived
 *       sections rebuilt from its own triangles (tools_port/bcfiles geometry,
 *       files in BC_LC_LEVELS): the game's ground and wall queries
 *       (coll_ground_height, coll_begin_probe + coll_probe_tris) at the same
 *       random points over both, after lvl_read_header and the grid builders
 *   TC  (native only) TB's queries over a dense lattice: the number of points
 *       where the rebuilt file answers differently
 *   TD  (native only) the port's own geometry engine (load/level_geo.c) over
 *       every level: its rebuild's SHA-1 (tools/geocheck.py: the same file as
 *       bcfiles geo-rebuild's) and its keep-Rare rebuild's (the ROM's file)
 *
 * usage: loadcheck.exe ROM [TESTS [FACTS]]   (TESTS: characters, default 5678A;
 *        TB/TC read the rebuilt levels from the folder in BC_LC_LEVELS)
 */
#include <windows.h>
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "rdram.h"
#include "load/port_load.h"

typedef int16_t s16;
typedef int32_t s32;
typedef uint8_t u8;
typedef uint32_t u32;

#define V8(a) (*(volatile uint8_t *) (uintptr_t) (a))
#define V16(a) (*(volatile uint16_t *) (uintptr_t) (a))
#define V32(a) (*(volatile uint32_t *) (uintptr_t) (a))

/* game functions under test */
s32 port_802E8BF8_word(s32 i);
void tex_load_table(void);
void tex_load_to_heap_impl(s32 id, u8 *param);
void lvl_load_file(s32 level, s32 dest, s32 *size);
void lvl_read_alpha_zones(u8 *obj);
void cam_apply_height_zone(u8 *level);
void dig_coach_update(s32 now);
void lvl_read_header(u8 *obj);
void lvl_build_ground_grid(u8 *obj);
u8 *lvl_build_coll_grid_a(u8 *obj, s32 id, s32 b4F, s32 *s1io);
u8 *lvl_build_coll_grid_b(u8 *obj, s32 id, s32 b4F, s32 *s1io);
void lvl_build_part_tris(u8 *obj, s32 *s1io);
s32 coll_ground_height(s32 x, s32 z, s32 y);
void coll_begin_probe(s32 x, s32 y, s32 z);
void coll_probe_tris(s32 x, s32 y, s32 z, s32 r, s32 kind, s32 tag);
int port_level_set_edit(int level, const uint8_t *file, size_t size, const char *source);

/* ---- the platform surface these need (as port/src/platform does it) ---- */
static uint8_t *g_rom;
static size_t g_romsize;

s32 osPiStartDma(void *mb, s32 pri, s32 dir, u32 devAddr, void *dram, u32 size, void *mq) {
    (void) mb; (void) pri; (void) dir; (void) mq;
    if (devAddr + size > g_romsize) {
        fprintf(stderr, "DMA past the ROM end: %08X+%X\n", devAddr, size);
        exit(4);
    }
    memcpy(dram, g_rom + devAddr, size);
    port_on_dma((u32) (uintptr_t) dram, devAddr, size);
    return 0;
}
s32 osRecvMesg(void *mq, void *msg, s32 flag) { (void) mq; (void) msg; (void) flag; return 0; }
void osInvalDCache(void *p, s32 n) { (void) p; (void) n; }
void osWritebackDCache(void *p, s32 n) { (void) p; (void) n; }
/* the platform layer's game hooks (include/game/port.h) */
void port_fe_loaded(void) {}
void port_spin(void) {}
void port_itag(const void *gdl, int kind, int index) { (void) gdl; (void) kind; (void) index; }
short *port_draw_cells(short *game) { return game; }
void port_viewer_frame(void) {}
void port_wtag(const void *gdl, int kind, int x0, int x1) { (void) gdl; (void) kind; (void) x0; (void) x1; }
unsigned int port_gvi_read(const char *fn, unsigned int v) { (void) fn; return v; }
void debug_printf(const char *fmt, ...) { (void) fmt; }
/* the SDK's block copy/clear (bc_headless: os_hw.c); gcc turns the game's
 * calls into memset/memmove, clang (the x86_64 build) calls them */
void bzero(void *p, int n) { memset(p, 0, (size_t) n); }
void bcopy(const void *src, void *dst, int n) { memmove(dst, src, (size_t) n); }
void port_stub_hit(const char *name) {
    fprintf(stderr, "STUB called: %s\n", name);
    exit(3);
}

/* an access violation inside a case is reported as CRASH for that case */
static jmp_buf crash_jmp;
static volatile int crash_armed;
static LONG CALLBACK on_fault(EXCEPTION_POINTERS *ep) {
    if (crash_armed && ep->ExceptionRecord->ExceptionCode == EXCEPTION_ACCESS_VIOLATION) {
        crash_armed = 0;
        longjmp(crash_jmp, 1);
    }
    return EXCEPTION_CONTINUE_SEARCH;
}

static uint32_t crc32(const uint8_t *p, uint32_t n) {
    uint32_t c = 0xFFFFFFFFu, k;
    while (n--) {
        c ^= *p++;
        for (k = 0; k < 8; k++) c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1)));
    }
    return ~c;
}

static uint32_t rng_state;
static uint32_t rnd(void) {
    rng_state = rng_state * 1103515245u + 12345u;
    return rng_state >> 8;
}

#define HEAP     0x80100000u   /* texture / level loads go here */
#define PARAM    0x801F0000u   /* synthetic u16 table for texture types 4 and 5 */
#define HEAPPTR  0x80358070u   /* gHeapPtr */

static void t5(void) {
    int flag, i;
    for (flag = 0; flag < 2; flag++)
        for (i = 0; i < 19; i++) {
            V8(0x802E8BF8) = (uint8_t) flag;   /* the game's own u8 store (00000.c) */
            printf("T5 %d.%02d %08X\n", flag, i, (unsigned) port_802E8BF8_word(i));
        }
}

static void t6(void) {
    uint32_t id, i, tab;
    /* big-endian, as in the game: these tables are texture entries loaded as
     * stored (type 0) data, which the port leaves in N64 byte order */
    for (i = 0; i < 0x200; i++) {
        uint16_t v = (uint16_t) (i * 0x2B7 + 0x55);
        V8(PARAM + i * 2) = (uint8_t) (v >> 8);
        V8(PARAM + i * 2 + 1) = (uint8_t) v;
    }
    V32(HEAPPTR) = HEAP;
    V8(0x803B9888) = 0;
    tex_load_table();                 /* DMAs the 0x8000-byte table to the heap */
    tab = V32(0x803B8D44);
    for (id = 0; id < 0xFFF; id += 7) {
        uint32_t rom = V32(tab + id * 8), next = V32(tab + id * 8 + 8);
        uint16_t type = V16(tab + id * 8 + 6);
        uint32_t buf = 0x80180000u, n;
        if (next <= rom || next - rom > 0x8000 || type > 6) continue;
        V32(HEAPPTR) = buf;
        tex_load_to_heap_impl((s32) id, (type == 4 || type == 5) ? (u8 *) (uintptr_t) PARAM : NULL);
        n = V32(HEAPPTR) - buf;
        printf("T6 %03X %d %05X %08X\n", id, type, n, crc32((const uint8_t *) (uintptr_t) buf, n));
    }
}

/* level loads: returns the decompressed size or 0 */
static s32 load_level(int lvl) {
    s32 size = 0;
    lvl_load_file(lvl, (s32) HEAP, &size);
    return size;
}

static void t7(void) {
    int lvl;
    for (lvl = 0; lvl < 60; lvl++) {
        uint32_t e, end;
        s32 size = load_level(lvl);
        if (setjmp(crash_jmp)) {
            printf("T7 %02d.size CRASH\n", lvl);
            continue;
        }
        crash_armed = 1;
        lvl_read_alpha_zones((u8 *) (uintptr_t) HEAP);
        crash_armed = 0;
        end = V32(0x803BDFD4);
        printf("T7 %02d.size %05X first %02X n %u\n", lvl, (unsigned) size, V8(0x80364A6E),
               (unsigned) ((end - 0x803BDFD8u) / 0x24));
        for (e = 0x803BDFD8u; e < end && e < 0x803BDFD8u + 0x24 * 400; e += 0x24) {
            uint32_t k, n = V8(e + 0x13);
            printf("T7 %02d.%04X %08X %08X %08X %08X %02X %02X %02X %02X %02X ", lvl, (unsigned) ((e - 0x803BDFD8u) / 0x24),
                   V32(e), V32(e + 4), V32(e + 8), V32(e + 0xC), V8(e + 0x10), V8(e + 0x11), V8(e + 0x12),
                   V8(e + 0x13), V8(e + 0x14));
            for (k = 0; k < n && k < 0xF; k++) printf("%02X", V8(e + 0x15 + k));
            printf("\n");
        }
    }
}

static void t8(void) {
    int lvl, i;
    rng_state = 8;
    for (lvl = 0; lvl < 60; lvl++) {
        load_level(lvl);
        for (i = 0; i < 40; i++) {
            uint32_t a = (rnd() % 0x400) << 5;
            a |= rnd() & 31;
            V32(0x803643E0) = a;
            a = (rnd() % 0x400) << 5;
            a |= rnd() & 31;
            V32(0x803643E8) = a;
            V16(0x80364450) = rnd() & 0xFFFF;
            V16(0x8036444E) = 0x1234;
            V8(0x80364411) = 0x55;
            if (setjmp(crash_jmp)) {
                printf("T8 %02d.%02d CRASH\n", lvl, i);
                continue;
            }
            crash_armed = 1;
            cam_apply_height_zone((u8 *) (uintptr_t) HEAP);
            crash_armed = 0;
            printf("T8 %02d.%02d %04X %02X\n", lvl, i, V16(0x8036444E), V8(0x80364411));
        }
    }
}

/* TA: the panel events [level + hdr[0x58], level + hdr[0x5C]) (8-byte
 * CarrierPanelEvent records, 32E00.c), read by the game's own dig_coach_update right
 * after the load: with gHaveCarrier set and gCarrierZ = k << 5 for k = -0x8000
 * .. 0x7FF0 in steps of 0x10, the first k at which each event's done byte
 * (+6) is set (it fires once k > its s16 z).  gGameMode 0 makes
 * hud_panel_start return at once; gDigHit 0 and an empty sample ring skip
 * the coaching checks; now = 0 skips the level-start message.  (The emulator
 * runs of verify-levels end before any carrier gets that far.) */
#define DIG_TRIGGERS     0x803BE6FCu
#define DIG_TRIGGERS_END 0x803BE700u
static void ta(void) {
    int lvl;
    for (lvl = 0; lvl < 60; lvl++) {
        uint32_t lo, hi, p, i, n;
        int32_t k;
        uint32_t fired[64];
        load_level(lvl);
        lo = HEAP + V32(HEAP + 0x58);
        hi = HEAP + V32(HEAP + 0x5C);
        n = (hi - lo) / 8;
        printf("TA %02d n %u\n", lvl, (unsigned) n);
        if (n == 0 || n > 64) continue;
        V32(DIG_TRIGGERS) = lo;
        V32(DIG_TRIGGERS_END) = hi;
        V32(0x80364A90) = 0;   /* gGameMode (u64) */
        V32(0x80364A94) = 0;
        V8(0x803643DB) = 1;    /* gHaveCarrier */
        V8(0x803643D6) = 0;    /* gCarrierHit */
        V8(0x8036CB2F) = 0;    /* gDigHit */
        V8(0x8036CB28) = 0;    /* gDigSampleTail */
        V8(0x8036CB29) = 0;    /* gDigSampleHead */
        for (i = 0; i < n; i++) fired[i] = 0x7FFFFFFFu;
        if (setjmp(crash_jmp)) {
            printf("TA %02d CRASH\n", lvl);
            continue;
        }
        crash_armed = 1;
        for (k = -0x8000; k < 0x8000; k += 0x10) {
            V32(0x803EF6E4) = (uint32_t) k << 5;   /* gCarrierZ */
            dig_coach_update(0);
            for (i = 0, p = lo; i < n; i++, p += 8)
                if (fired[i] == 0x7FFFFFFFu && V8(p + 6)) fired[i] = (uint32_t) k;
        }
        crash_armed = 0;
        for (i = 0, p = lo; i < n; i++, p += 8) {
            if (fired[i] == 0x7FFFFFFFu)
                printf("TA %02d.%02u never (%u %u %u %u)\n", lvl, (unsigned) i, V8(p + 2), V8(p + 3), V8(p + 4),
                       V8(p + 5));
            else
                printf("TA %02d.%02u fires at z %d (%u %u %u %u)\n", lvl, (unsigned) i, (int) (int32_t) fired[i],
                       V8(p + 2), V8(p + 3), V8(p + 4), V8(p + 5));
        }
    }
}

/* ---- TB / TC: the ground and wall queries over a rebuilt level ----
 * The level is loaded (the ROM's file, or the rebuilt one through the
 * PORT_LEVEL hook, as a mod's would be), then lvl_read_header, the ground
 * grid, collision grids A and B and the moving-part triangles are built as
 * initlevel does (heap at 0x80180000, ids 0), with no objects, push blocks,
 * switch or gate triangles.  A ground query: gGroundHitTri cleared,
 * coll_ground_height(x, z, y), then the height, the hit triangle's surface
 * and flag bytes and gGroundTriId.  A wall query: gFailTrigger cleared,
 * coll_begin_probe + coll_probe_tris(x, y, z, r, 0xBE (the camera's kind), 0),
 * then the free arc, gCollHit, gFreeArcCollapsed, gFailTrigger.  Points are in
 * world units inside both grids (the game's lookups have no bounds checks). */
#define TB_HEAP        0x80180000u
#define A_GOBJECTS     0x803F4030u
#define A_GOBJECTSEND  0x803F7654u
#define A_GDYNTRIS     0x803F9330u
#define A_GDYNTRISEND  0x803FB8B0u
#define A_GSWITCHTRIS  0x803BD308u
#define A_GSWITCHEND   0x803BD30Cu
#define A_GCOLLTRIS    0x803B9890u
#define A_GCOLLTRISEND 0x803BD300u
#define A_LEVELNO      0x802E8BDCu
#define A_GROUNDHIT    0x803EBC08u
#define A_GROUNDTRIID  0x803F932Cu
#define A_ARCSTART     0x803A7410u
#define A_ARCEND       0x803A7412u
#define A_COLLHIT      0x803A7424u
#define A_ARCCOLLAPSED 0x803A742Fu
#define A_FAILTRIGGER  0x803BE738u
#define A_HITSPEED     0x803F77FCu

static uint8_t *g_rebuilt[60];
static size_t g_rebuilt_size[60];

static int tb_read_rebuilt(void) {
    const char *dir = getenv("BC_LC_LEVELS");
    int lvl, n = 0;
    if (dir == NULL) return 0;
    for (lvl = 0; lvl < 60; lvl++) {
        char path[1024];
        FILE *f;
        long sz;
        snprintf(path, sizeof path, "%s\\level_%02d.bin", dir, lvl);
        f = fopen(path, "rb");
        if (f == NULL) continue;
        fseek(f, 0, SEEK_END);
        sz = ftell(f);
        fseek(f, 0, SEEK_SET);
        g_rebuilt[lvl] = (uint8_t *) malloc((size_t) sz);
        if (g_rebuilt[lvl] && fread(g_rebuilt[lvl], 1, (size_t) sz, f) == (size_t) sz) {
            g_rebuilt_size[lvl] = (size_t) sz;
            n++;
        }
        fclose(f);
    }
    return n;
}

/* the level (rebuilt or not) loaded and its grids built */
static void tb_setup(int lvl, int rebuilt) {
    s32 s1 = 0;
    if (rebuilt)
        port_level_set_edit(lvl, g_rebuilt[lvl], g_rebuilt_size[lvl], "rebuilt (loadcheck TB)");
    load_level(lvl);
    if (rebuilt) port_level_set_edit(lvl, NULL, 0, NULL);
    V32(HEAPPTR) = TB_HEAP;
    V32(A_LEVELNO) = (uint32_t) lvl;
    lvl_read_header((u8 *) (uintptr_t) HEAP);
    lvl_build_ground_grid((u8 *) (uintptr_t) HEAP);
    lvl_build_coll_grid_a((u8 *) (uintptr_t) HEAP, 0, 0, &s1);
    lvl_build_coll_grid_b((u8 *) (uintptr_t) HEAP, 0, 0, &s1);
    lvl_build_part_tris((u8 *) (uintptr_t) HEAP, &s1);
    V32(A_GOBJECTSEND) = A_GOBJECTS;
    V32(A_GDYNTRISEND) = A_GDYNTRIS;
    V32(A_GSWITCHTRIS) = 0;
    V32(A_GSWITCHEND) = 0;
    V32(A_GCOLLTRISEND) = A_GCOLLTRIS;
    V16(A_HITSPEED) = 0;
}

static uint32_t tb_ground(s32 x, s32 z, s32 y) {
    s32 h;
    uint32_t t;
    V32(A_GROUNDHIT) = 0;
    V8(A_GROUNDTRIID) = 0;
    h = coll_ground_height(x, z, y);
    t = V32(A_GROUNDHIT);
    return (uint32_t) h * 2654435761u ^ (t ? (uint32_t) V8(t + 0x12) << 8 | V8(t + 0x13) : 0xFFFF0000u) ^
           (uint32_t) V8(A_GROUNDTRIID) << 24;
}

static uint32_t tb_probe(s32 x, s32 y, s32 z, s32 r) {
    V8(A_FAILTRIGGER) = 0;
    coll_begin_probe(x, y, z);
    coll_probe_tris(x, y, z, r, 0xBE, 0);
    return (uint32_t) V16(A_ARCSTART) | (uint32_t) V16(A_ARCEND) << 12 | (uint32_t) V8(A_COLLHIT) << 24 |
           (uint32_t) V8(A_ARCCOLLAPSED) << 25 | (uint32_t) (V8(A_FAILTRIGGER) != 0) << 26;
}

typedef struct { s32 x, y, z, r; } TbPoint;

/* the sample points of a level (the same in loadref.py): from its header (the
 * rebuilt file keeps the header) */
static int tb_points(int lvl, TbPoint *g, int ng, TbPoint *p, int np) {
    uint32_t gw, gd, cw, cd;
    int i;
    rng_state = 0xB0u + (uint32_t) lvl;
    gw = (uint32_t) V16(HEAP + 8) * V16(HEAP + 0xC);
    gd = (uint32_t) V16(HEAP + 0xA) * V16(HEAP + 0xE);
    cw = (uint32_t) V16(HEAP + 0x10) * V16(HEAP + 0x14);
    cd = (uint32_t) V16(HEAP + 0x12) * V16(HEAP + 0x16);
    if (cw > gw) cw = gw;
    if (cd > gd) cd = gd;
    if (gw == 0 || gd == 0 || cw == 0 || cd == 0) return 0;
    /* (one rnd() per statement: the order is the reference's) */
    for (i = 0; i < ng; i++) {
        uint32_t k = rnd() % 4, a, b;
        a = rnd() % gw;
        b = rnd() % 32;
        g[i].x = (s32) (a * 32 + b);
        a = rnd() % gd;
        b = rnd() % 32;
        g[i].z = (s32) (a * 32 + b);
        g[i].y = k == 0 ? 0 : k == 1 ? (s32) (rnd() % 0x10000) : k == 2 ? (s32) (rnd() % 0x40000) : 0x7FFFF;
        g[i].r = 0;
    }
    for (i = 0; i < np; i++) {
        static const s32 radii[4] = {0x100, 0x400, 0xC00, 0x1800};
        uint32_t a, b;
        a = rnd() % cw;
        b = rnd() % 32;
        p[i].x = (s32) (a * 32 + b);
        a = rnd() % cd;
        b = rnd() % 32;
        p[i].z = (s32) (a * 32 + b);
        p[i].r = radii[rnd() % 4];
        p[i].y = 0x40000;   /* the probe goes at the ground under this + r / 2 */
    }
    return 1;
}

#define TB_NG 600
#define TB_NP 600
static void tb(void) {
    int lvl, nreb = tb_read_rebuilt();
    static TbPoint g[TB_NG], p[TB_NP];
    static uint32_t res[2][TB_NG + TB_NP];
    printf("TB rebuilt %d\n", nreb);
    for (lvl = 0; lvl < 60; lvl++) {
        int pass, i, dg = 0, dp = 0;
        if (g_rebuilt[lvl] == NULL) continue;
        for (pass = 0; pass < 2; pass++) {
            uint32_t cg, cp;
            if (setjmp(crash_jmp)) {
                printf("TB %02d.%s CRASH\n", lvl, pass ? "rebuilt" : "rom");
                goto next;
            }
            crash_armed = 1;
            tb_setup(lvl, pass);
            if (!tb_points(lvl, g, TB_NG, p, TB_NP)) {
                crash_armed = 0;
                goto next;
            }
            for (i = 0; i < TB_NG; i++) res[pass][i] = tb_ground(g[i].x, g[i].z, g[i].y);
            for (i = 0; i < TB_NP; i++) {
                s32 h = coll_ground_height(p[i].x, p[i].z, p[i].y);
                if (h >= 0x40000) h = 0;   /* no ground there: at 0 (far up, the original's dadd traps) */
                res[pass][TB_NG + i] = tb_probe(p[i].x, h + p[i].r / 2, p[i].z, p[i].r);
            }
            crash_armed = 0;
            cg = crc32((const uint8_t *) res[pass], 4 * TB_NG);
            cp = crc32((const uint8_t *) (res[pass] + TB_NG), 4 * TB_NP);
            printf("TB %02d.%s ground %08X probe %08X\n", lvl, pass ? "rebuilt" : "rom", cg, cp);
        }
        for (i = 0; i < TB_NG; i++) dg += res[0][i] != res[1][i];
        for (i = 0; i < TB_NP; i++) dp += res[0][TB_NG + i] != res[1][TB_NG + i];
        printf("TB %02d.diff ground %d/%d probe %d/%d\n", lvl, dg, TB_NG, dp, TB_NP);
    next:;
    }
}

/* TC: native only, a lattice: ground every 12 map units (y cycling through
 * four heights), walls every 48 with four radii */
static void tc(void) {
    int lvl, nreb = tb_read_rebuilt();
    printf("TC rebuilt %d\n", nreb);
    for (lvl = 0; lvl < 60; lvl++) {
        uint32_t gw, gd, cw, cd, x, z, n, k;
        uint32_t *res[2];
        long ng = 0, np = 0, dg = 0, dp = 0;
        int pass, shown = 0;
        if (g_rebuilt[lvl] == NULL) continue;
        load_level(lvl);
        gw = (uint32_t) V16(HEAP + 8) * V16(HEAP + 0xC);
        gd = (uint32_t) V16(HEAP + 0xA) * V16(HEAP + 0xE);
        cw = (uint32_t) V16(HEAP + 0x10) * V16(HEAP + 0x14);
        cd = (uint32_t) V16(HEAP + 0x12) * V16(HEAP + 0x16);
        if (cw > gw) cw = gw;
        if (cd > gd) cd = gd;
        n = (gw / 12 + 1) * (gd / 12 + 1) + 4 * (cw / 48 + 1) * (cd / 48 + 1);
        res[0] = (uint32_t *) malloc(4 * (size_t) n);
        res[1] = (uint32_t *) malloc(4 * (size_t) n);
        for (pass = 0; pass < 2; pass++) {
            uint32_t q = 0;
            static const s32 ys[4] = {0, 0x3000, 0x12000, 0x7FFFF};
            static const s32 radii[4] = {0x100, 0x400, 0xC00, 0x1800};
            if (setjmp(crash_jmp)) {
                printf("TC %02d.%s CRASH\n", lvl, pass ? "rebuilt" : "rom");
                goto next;
            }
            crash_armed = 1;
            tb_setup(lvl, pass);
            for (z = 0; z < gd; z += 12)
                for (x = 0; x < gw; x += 12)
                    res[pass][q++] = tb_ground((s32) x * 32 + 7, (s32) z * 32 + 7, ys[(x / 12 + z / 12) & 3]);
            ng = q;
            for (z = 0; z < cd; z += 48)
                for (x = 0; x < cw; x += 48)
                    for (k = 0; k < 4; k++) {
                        s32 h = coll_ground_height((s32) x * 32 + 5, (s32) z * 32 + 5, 0x40000);
                        if (h >= 0x40000) h = 0;
                        res[pass][q++] = tb_probe((s32) x * 32 + 5, h + radii[k] / 2, (s32) z * 32 + 5, radii[k]);
                    }
            np = q - ng;
            crash_armed = 0;
        }
        for (k = 0; k < (uint32_t) (ng + np); k++) {
            if (res[0][k] == res[1][k]) continue;
            if (k < (uint32_t) ng) dg++;
            else dp++;
            if (shown++ < 3) printf("TC %02d.e%d %s sample %u: rom %08X rebuilt %08X\n", lvl, shown,
                                    k < (uint32_t) ng ? "ground" : "probe", k, res[0][k], res[1][k]);
        }
        printf("TC %02d ground %ld diff %ld probe %ld diff %ld\n", lvl, ng, dg, np, dp);
    next:
        free(res[0]);
        free(res[1]);
    }
}

/* TD (native only): the port's geometry engine (load/level_geo.c) over every
 * level's own triangles: the SHA-1 of the full rebuild (tools/geocheck.py
 * compares it with tools_port/bcfiles geo-rebuild's file) and of the
 * keep-Rare's-data rebuild (must be the ROM's file) */
int port_level_original(int level, const uint8_t **file, size_t *size);
int lg_rebuild(const uint8_t *file, size_t size, int keep, uint8_t **out, size_t *outsize, char *err, size_t errlen);
void lm_sha1_hex(const uint8_t *data, size_t len, char out[41]);
static void td(void) {
    int lvl;
    for (lvl = 0; lvl < 60; lvl++) {
        const uint8_t *f;
        size_t n, on;
        uint8_t *o;
        char err[300], a[41], b[41], c[41];
        int k;
        if (port_level_original(lvl, &f, &n)) {
            printf("TD %02d no file\n", lvl);
            continue;
        }
        lm_sha1_hex(f, n, c);
        for (k = 0; k < 2; k++) {
            if (lg_rebuild(f, n, k, &o, &on, err, sizeof err)) {
                printf("TD %02d %s: %s\n", lvl, k ? "keep" : "full", err);
                a[0] = b[0] = 0;
                break;
            }
            lm_sha1_hex(o, on, k ? b : a);
            free(o);
        }
        printf("TD %02d full %s keep %s rom %s\n", lvl, a, b, c);
    }
}

/* ---- T9: every read the original code made of each traced asset ----
 * (tools/widths.py facts over a tools/m64widths.py trace; local, not
 * committed).  Right after the asset arrives and port_on_dma/port_on_load
 * ran, a native read of WIDTH bytes at OFF must give the big-endian value the
 * N64 read there; bytes read only bytewise must be unchanged. */
void kiunzip_entry(u8 **src, u8 **dst, s32 work);
void lzss_decompress(u32 *src, u32 *dst, u32 work, u8 type);

#define T9_DST   0x80100000u
#define T9_STAGE 0x8021ED00u

static int lzss_bits(uint32_t rom) {
    if (rom == 0x787F40u) return 10;   /* static segment: hd_rom_load(.., 10, 0, 2) */
    return 13;                         /* sound banks, front-end blocks: (.., 0xD, 0, 2) */
}

static int t9_load(uint32_t rom, uint32_t size, const char *how, uint8_t **raw) {
    uint32_t src, dst;
    if (size == 0 || size > 0xF0000 || rom + 0x100 > g_romsize) return 0;
    if (rom >= 0xCCE0u && rom < 0x350950u) return 0;   /* texture entries: decoded in place (T6) */
    /* texture bundles and pictures: tokens swapped at decode time, pictures
     * tinted bytewise in the port (196F0.c); the offset table 0x6EC4C0 shares
     * its heap buffer with later decodes in the trace */
    if (rom >= 0x66C900u && rom < 0x6E8980u && rom != 0x6A9F10u) return 0;
    if (rom == 0x6EC4C0u) return 0;
    if (!strcmp(how, "dma")) {
        memcpy((void *) (uintptr_t) T9_DST, g_rom + rom, size);
    } else {
        uint32_t n = g_romsize - rom < 0x25000 ? (uint32_t) (g_romsize - rom) : 0x25000;
        memcpy((void *) (uintptr_t) T9_STAGE, g_rom + rom, n);
        src = T9_STAGE;
        dst = T9_DST;
        if (g_rom[rom] == 0x1F && g_rom[rom + 1] == 0x8B) {
            int m;
            for (m = 0; m < 4 && dst - T9_DST < size; m++)
                kiunzip_entry((u8 **) &src, (u8 **) &dst, 0x8004B400);
        } else {
            lzss_decompress(&src, &dst, 0x8004B400u, (u8) lzss_bits(rom));
        }
        if (dst - T9_DST < size) return 0;
    }
    *raw = malloc(size);
    memcpy(*raw, (void *) (uintptr_t) T9_DST, size);
    if (!strcmp(how, "dma"))
        port_on_dma(T9_DST, rom, size);
    else
        port_on_load(rom, T9_DST, size);
    return 1;
}

static int g_t9_all;

static void t9(const char *path) {
    FILE *f = fopen(path, "r");
    char line[256], how[8] = "";
    uint8_t *raw = NULL;
    uint32_t rom = 0, size = 0, nf = 0, bad = 0, multi = 0, assets = 0, badassets = 0, tf = 0, tbad = 0;
    int have = 0;
    char first[96] = "";
    if (!f) {
        printf("T9 no facts file %s\n", path);
        return;
    }
    for (;;) {
        int eof = !fgets(line, sizeof line, f);
        if (eof || line[0] == '@') {
            if (have) {
                printf("T9 %06X %s size %X facts %u bad %u multi %u%s%s\n", rom, how, size, nf, bad, multi,
                       bad ? "  e.g. " : "", first);
                assets++;
                badassets += bad != 0;
                tf += nf;
                tbad += bad;
            }
            free(raw);
            raw = NULL;
            have = 0;
            if (eof) break;
            if (sscanf(line, "@ %x %x %7s", &rom, &size, how) != 3) continue;
            have = t9_load(rom, size, how, &raw);
            nf = bad = multi = 0;
            first[0] = 0;
            continue;
        }
        if (have && line[0] != '#') {
            unsigned off, w;
            uint64_t be = 0, nat = 0;
            uint32_t k;
            if (sscanf(line, "%x %u", &off, &w) != 2 || off + w > size) continue;
            if (strchr(line, ',')) {
                multi++;
                continue;
            }
            for (k = 0; k < w; k++) be = (be << 8) | raw[off + k];
            memcpy(&nat, (void *) (uintptr_t) (T9_DST + off), w);
            nf++;
            if (be != nat) {
                if (!bad) snprintf(first, sizeof first, "+%X w%u N64 %llX native %llX", off, w,
                                   (unsigned long long) be, (unsigned long long) nat);
                if (g_t9_all)   /* (spike.exe ROM TESTS FACTS all: every wrong read) */
                    printf("T9bad %06X +%X w%u N64 %llX native %llX\n", rom, off, w, (unsigned long long) be,
                           (unsigned long long) nat);
                bad++;
            }
        }
    }
    fclose(f);
    printf("T9 total: %u assets, %u with mismatches; %u reads checked, %u wrong\n", assets, badassets, tf, tbad);
}

int main(int argc, char **argv) {
    const char *rom = argc > 1 ? argv[1] : "baserom.us.v11.z64";
    const char *tests = argc > 2 ? argv[2] : "5678A";
    setvbuf(stdout, NULL, _IOFBF, 1 << 16);
    AddVectoredExceptionHandler(1, on_fault);
    if (rdram_map() || rdram_load(rom)) return 1;
    g_rom = rom_bytes(&g_romsize);
    port_load_verbose = getenv("PORT_LOAD_VERBOSE") != NULL;
    if (strchr(tests, '5')) t5();
    if (strchr(tests, '6')) t6();
    if (strchr(tests, '7')) t7();
    if (strchr(tests, '8')) t8();
    if (strchr(tests, 'A')) ta();
    if (strchr(tests, 'B')) tb();
    if (strchr(tests, 'C')) tc();
    if (strchr(tests, 'D')) td();
    g_t9_all = argc > 4 && !strcmp(argv[4], "all");
    if (strchr(tests, '9')) t9(argc > 3 ? argv[3] : "build/lc/facts.txt");
    fflush(stdout);
    port_load_report();
    return 0;
}
