#include <windows.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "platform/port_cc.h"
#include "rdram.h"
#include "load/port_load.h"
#include "load/sha1.h"

static uint8_t *g_rom;
static size_t g_rom_size;
static char g_err[1024];

const char *rdram_error(void) {
    return g_err;
}

/* record (and print) why loading failed; returns -1 */
static int fail(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(g_err, sizeof g_err, fmt, ap);
    va_end(ap);
    fprintf(stderr, "rdram: %s\n", g_err);
    return -1;
}

/* fopen for a UTF-8 path (bc.exe's paths), else the ANSI code page */
static FILE *open_path(const char *path, const char *mode) {
    wchar_t wp[4096], wm[8];
    if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path, -1, wp, 4096) > 0 &&
        MultiByteToWideChar(CP_UTF8, 0, mode, -1, wm, 8) > 0)
        return _wfopen(wp, wm);
    return fopen(path, mode);
}

int rom_normalise(uint8_t *p, size_t n) {
    size_t i;
    if (n < 4) return -1;
    if (p[0] == 0x80 && p[1] == 0x37 && p[2] == 0x12 && p[3] == 0x40) return ROM_Z64;
    if (p[0] == 0x37 && p[1] == 0x80 && p[2] == 0x40 && p[3] == 0x12) {   /* .v64: bytes swapped in pairs */
        for (i = 0; i + 1 < n; i += 2) {
            uint8_t t = p[i];
            p[i] = p[i + 1];
            p[i + 1] = t;
        }
        return ROM_V64;
    }
    if (p[0] == 0x40 && p[1] == 0x12 && p[2] == 0x37 && p[3] == 0x80) {   /* .n64: little-endian words */
        for (i = 0; i + 3 < n; i += 4) {
            uint32_t v;
            memcpy(&v, p + i, 4);
            v = PORT_BSWAP32(v);
            memcpy(p + i, &v, 4);
        }
        return ROM_N64;
    }
    return -1;
}

#ifdef BC_ASAN
/* the AddressSanitizer build: N64 memory is bc_rdram.dll's section at
 * 0x80000000 (rdram_asan_dll.c: ASan's shadow memory would otherwise take the
 * range before the exe runs) */
__declspec(dllimport) extern unsigned char bc_rdram[];
#endif

int rdram_map(void) {
#ifdef BC_ASAN
    /* the DLL's one section starts at 0x80000000 and covers the 8 MB (the
     * array itself follows a few bytes of the linker's own data): writable
     * and zero, as VirtualAlloc's would be */
    void *p = NULL;
    DWORD old;
    if ((uintptr_t) bc_rdram >= RDRAM_BASE && (uintptr_t) bc_rdram < RDRAM_BASE + 0x1000 &&
        VirtualProtect((void *) (uintptr_t) RDRAM_BASE, RDRAM_SIZE, PAGE_READWRITE, &old)) {
        p = (void *) (uintptr_t) RDRAM_BASE;
        memset(p, 0, RDRAM_SIZE);
    } else {
        fprintf(stderr, "rdram: bc_rdram.dll's memory is at %p, not 0x%08X\n", (void *) bc_rdram, RDRAM_BASE);
    }
#else
    void *p = VirtualAlloc((void *) (uintptr_t) RDRAM_BASE, RDRAM_SIZE, MEM_RESERVE | MEM_COMMIT,
                           PAGE_READWRITE);
#endif
    if (p != (void *) (uintptr_t) RDRAM_BASE) {
        MEMORY_BASIC_INFORMATION mbi;
        unsigned long err = GetLastError();
        if (VirtualQuery((void *) (uintptr_t) RDRAM_BASE, &mbi, sizeof mbi))
            fprintf(stderr, "rdram: region there: alloc base %p state 0x%lx type 0x%lx size 0x%lx\n",
                    mbi.AllocationBase, mbi.State, mbi.Type, (unsigned long) mbi.RegionSize);
        return fail("VirtualAlloc(0x%08X) failed (got %p, error %lu); is the exe linked with "
                    "--large-address-aware?", RDRAM_BASE, p, err);
    }
#if UINTPTR_MAX > 0xFFFFFFFFu
    /* 64-bit: N64 memory holds 4-byte pointers (N64P), so every host address
     * the game can store there must be below 4 GB: the exe (fixed base, no
     * ASLR), the heap and the stacks (no high-entropy VA: bottom-up) */
    {
        void *heap = malloc(64);
        int local;
        uintptr_t top = (uintptr_t) 1 << 32;
#ifdef BC_ASAN
        /* AddressSanitizer's allocator lives above 4 GB.  Only the port's host
         * code keeps heap blocks (the game C has no heap and is given none), so
         * the ASan build allows it; a heap address that did reach N64 memory
         * would be cut to 4 bytes and fault at once */
        if ((uintptr_t) heap >= top) {
            fprintf(stderr, "rdram: AddressSanitizer build: heap above 4 GB (%p)\n", heap);
            free(heap);
            heap = NULL;
        }
#endif
        if ((uintptr_t) heap >= top || (uintptr_t) &local >= top || (uintptr_t) &rdram_map >= top) {
            free(heap);
            return fail("host memory above 4 GB (heap %p, stack %p, code %p): the 64-bit build needs "
                        "it below (linked without dynamic base and high-entropy VA?)", heap, (void *) &local,
                        (void *) &rdram_map);
        }
        free(heap);
    }
#endif
    return 0;
}

uint8_t *rom_bytes(size_t *size) {
    if (size) *size = g_rom_size;
    return g_rom;
}

/* ---- ROMs from the shiftable build (docs/shiftable.md) ----------------------------------------------------
 * The port runs its own code, so every ROM offset it uses (the symbols linked into the game code, the
 * constants of the level and model tables, the ones here and in port_assets.c / port_mods.c) is the
 * original ROM's.  A ROM built by `make SHIFT=1` in which files moved or changed size carries a map
 * (tools_port/shift/romlayout.py): runs of the original ROM that moved by one amount each, as
 * (original start, original end, new start, new end of the data).  Every read of the ROM goes through it:
 * an original offset in, this ROM's out.  The retail ROM, and an unchanged shift build (the same bytes),
 * have no map, and nothing is translated. */
typedef struct {
    uint32_t vs, ve, ps, pe;
} RomRun;
typedef struct {
    RomRun *run;
    int n;
    uint32_t flags;
} RomMap;
static RomMap g_map;

#define MAP_RAM_MOVED 1u
#define ROM_ORIG_SIZE 0x800000u

static uint32_t be32(const uint8_t *p) {
    return (uint32_t) p[0] << 24 | (uint32_t) p[1] << 16 | (uint32_t) p[2] << 8 | p[3];
}

static uint32_t crc32_of(const uint8_t *p, size_t n) {
    uint32_t c = 0xFFFFFFFFu;
    size_t i;
    int k;
    for (i = 0; i < n; i++) {
        c ^= p[i];
        for (k = 0; k < 8; k++) c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1)));
    }
    return ~c;
}

/* the map of ROM image P (N bytes, .z64 order): 1 found (M filled, M->run allocated), 0 none, -1 broken (WHY) */
static int map_parse(const uint8_t *p, size_t n, RomMap *m, char *why, size_t whylen) {
    uint32_t at, cnt, i;
    const uint8_t *h;
    memset(m, 0, sizeof *m);
    if (n < ROM_ORIG_SIZE || memcmp(p + n - 16, "BCSHIFT!", 8) != 0) return 0;
    at = be32(p + n - 8);
    h = p + at;
    if (at < 0x1000 || at % 16 || (size_t) at + 32 > n - 16 || memcmp(h, "BCSHMAP1", 8) != 0 || be32(h + 8) != 1) {
        snprintf(why, whylen, "its ROM map is not one this port reads");
        return -1;
    }
    cnt = be32(h + 12);
    if (cnt == 0 || cnt > 0x10000 || (size_t) at + 32 + (size_t) cnt * 16 > n - 16 ||
        crc32_of(h, 32 + (size_t) cnt * 16) != be32(p + n - 4) || crc32_of(h + 32, (size_t) cnt * 16) != be32(h + 20)) {
        snprintf(why, whylen, "its ROM map is damaged");
        return -1;
    }
    m->run = (RomRun *) malloc(sizeof(RomRun) * cnt);
    if (m->run == NULL) {
        snprintf(why, whylen, "out of memory");
        return -1;
    }
    for (i = 0; i < cnt; i++) {
        RomRun *r = &m->run[i];
        r->vs = be32(h + 32 + 16 * i), r->ve = be32(h + 36 + 16 * i);
        r->ps = be32(h + 40 + 16 * i), r->pe = be32(h + 44 + 16 * i);
        if (r->vs >= r->ve || r->ve > ROM_ORIG_SIZE || r->ps > r->pe || r->pe > n || (i && r->vs < m->run[i - 1].ve) ||
            (i && r->ps < m->run[i - 1].pe)) {
            free(m->run);
            m->run = NULL;
            snprintf(why, whylen, "its ROM map is inconsistent (run %u)", (unsigned) i);
            return -1;
        }
    }
    m->n = (int) cnt;
    m->flags = be32(h + 16);
    return 1;
}

/* the run holding original offset V (vs <= V < ve), or -1 */
static int map_find(const RomMap *m, uint32_t v) {
    int lo = 0, hi = m->n;
    while (lo < hi) {
        int mid = (lo + hi) / 2;
        if (m->run[mid].vs <= v) lo = mid + 1;
        else hi = mid;
    }
    return lo > 0 && v < m->run[lo - 1].ve ? lo - 1 : -1;
}

static uint32_t map_phys(const RomMap *m, uint32_t v) {
    int i;
    if (m->n == 0) return v;
    if (v >= ROM_PHYS_WINDOW && v - ROM_PHYS_WINDOW < g_rom_size) return v - ROM_PHYS_WINDOW;
    i = map_find(m, v);
    return i < 0 ? v : m->run[i].ps + (v - m->run[i].vs);
}

/* the end of a read that ends at original offset V: where a run ends there, the end of its data (a file
 * that grew is read whole; the alignment zeros after one are left out, as the shift build's *_end symbols
 * do) */
static uint32_t map_phys_end(const RomMap *m, uint32_t v) {
    int i;
    if (m->n == 0) return v;
    if (v >= ROM_PHYS_WINDOW && v - ROM_PHYS_WINDOW <= g_rom_size) return v - ROM_PHYS_WINDOW;
    i = map_find(m, v - 1);
    if (i >= 0 && m->run[i].ve == v) return m->run[i].pe;
    return map_phys(m, v);
}

int rom_map_active(void) {
    return g_map.n > 0;
}

uint32_t rom_phys(uint32_t v) {
    return map_phys(&g_map, v);
}

uint32_t rom_phys_end(uint32_t v) {
    return map_phys_end(&g_map, v);
}

uint32_t rom_virt(uint32_t p) {
    int i;
    if (g_map.n == 0) return p;
    for (i = 0; i < g_map.n; i++)
        if (g_map.run[i].ps <= p && p < g_map.run[i].pe) return g_map.run[i].vs + (p - g_map.run[i].ps);
    return ROM_PHYS_WINDOW + p;   /* (a file the original ROM doesn't have: read where it is) */
}

unsigned int rom_map_virt(unsigned int off) {
    if (g_map.n == 0) return off;
    return off >= ROM_PHYS_WINDOW && off - ROM_PHYS_WINDOW < g_rom_size ? rom_virt(off - ROM_PHYS_WINDOW) : off;
}

void port_rom_span(unsigned int *rom, unsigned int *size) {
    uint32_t a, b;
    if (g_map.n == 0) return;
    a = map_phys(&g_map, *rom);
    b = map_phys_end(&g_map, *rom + *size);
    if (b < a) return;
    *rom = ROM_PHYS_WINDOW + a;
    *size = b - a;
}

unsigned int rom_map_dma(unsigned int off, unsigned int n, unsigned int *phys) {
    uint32_t a, b;
    int i;
    if (g_map.n == 0 || n == 0) {
        *phys = off;
        return n;
    }
    a = map_phys(&g_map, off);
    *phys = a;
    if (off >= ROM_PHYS_WINDOW) return n;
    /* a whole-file read (it ends where a run ends: the file, or the next one, moved): to its data's end */
    i = map_find(&g_map, off + n - 1);
    if (i >= 0 && g_map.run[i].ve == off + n && (b = g_map.run[i].pe) >= a) return b - a;
    return n;
}

/* the two tables the game reads from the ROM hold offsets from themselves; the port's code takes them as
 * original offsets (it adds the original table address): their entries are turned into original offsets */
void rom_map_fixup(void *dst, unsigned int off, unsigned int n) {
    uint8_t *d = (uint8_t *) dst;
    uint32_t base, i, cnt, stride;
    if (g_map.n == 0) return;
    if (off == ROM_TEX_TABLE && n == 0x8000u) cnt = 0x1000, stride = 8;
    else if (off == ROM_OBJ_TABLE && n == 0x800u) cnt = 0x200, stride = 4;
    else return;
    base = map_phys(&g_map, off);
    for (i = 0; i < cnt; i++) {
        uint8_t *e = d + i * stride;
        uint32_t v = rom_virt(base + be32(e)) - off;
        e[0] = (uint8_t) (v >> 24), e[1] = (uint8_t) (v >> 16), e[2] = (uint8_t) (v >> 8), e[3] = (uint8_t) v;
    }
}

/* the ROM file into g_rom, in .z64 (big-endian) order */
static int load_rom(const char *path) {
    FILE *f = open_path(path, "rb");
    long n;
    if (!f) return fail("Can't open the ROM file %s", path);
    fseek(f, 0, SEEK_END);
    n = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (n < 0x800000 || n > 0x4000000) {
        fclose(f);
        return fail("%s is not a Blast Corps ROM (%ld bytes; the game is 8 MB)", path, n);
    }
    g_rom_size = (size_t) n;
    g_rom = malloc(g_rom_size);
    if (g_rom == NULL || fread(g_rom, 1, g_rom_size, f) != g_rom_size) {
        fclose(f);
        return fail("Can't read the ROM file %s", path);
    }
    fclose(f);
    if (rom_normalise(g_rom, g_rom_size) < 0)
        return fail("%s is not an N64 ROM image (.z64, .v64 or .n64)", path);
    /* a ROM from the shiftable build with files moved: its map (rom_phys) */
    {
        char why[512];
        int r = map_parse(g_rom, g_rom_size, &g_map, why, sizeof why);
        if (r < 0) return fail("%s: %s", path, why);
        if (r > 0) {
            if (rom_shift_check(g_rom, g_rom_size, why, sizeof why) > 0) fprintf(stderr, "rdram: %s\n", why);
            else fprintf(stderr, "rdram: WARNING a ROM from the shiftable build the port can't run as built: %s\n", why);
        }
    }
    return 0;
}

static long gunzip_at(const uint8_t *rom, size_t size, uint32_t off, void *dst, size_t dstmax, uint32_t *next) {
    const uint8_t *p = rom + off;
    size_t i = 10, used = 0;
    long n;
    if ((size_t) off + 18 > size || p[0] != 0x1F || p[1] != 0x8B || p[2] != 8) return -1;
    if (p[3] & 4) i += 2 + (p[i] | (p[i + 1] << 8));
    if (p[3] & 8) while (off + i < size && p[i++]) {}
    if (p[3] & 16) while (off + i < size && p[i++]) {}
    if (p[3] & 2) i += 2;
    if (off + i >= size) return -1;
    n = inflate_raw_used(p + i, size - off - i, dst, dstmax, &used);
    if (next) *next = (uint32_t) (off + i + used + 8);
    return n;
}

long rom_gunzip(uint32_t off, void *dst, size_t dstmax) {
    return gunzip_at(g_rom, g_rom_size, rom_phys(off), dst, dstmax, NULL);
}

long rom_gunzip_next(uint32_t off, void *dst, size_t dstmax, uint32_t *next) {
    return gunzip_at(g_rom, g_rom_size, off, dst, dstmax, next);
}

/* SHA-1 of the inflated data images of Blast Corps (USA) (Rev 1): hd_code's (0x26A90 bytes) and the front
 * end's (0x8E50); a shift build that only moved files has them unchanged (only code holds ROM offsets) */
static const char kHdDataSha1[] = "704ddc0db4d616bdd0698431450842b0c52dddf0";
static const char kFeDataSha1[] = "adab6bb7d78d9f4f4c05468f3f0c5cf8dcc9a2fe";

static int image_matches(const uint8_t *rom, size_t size, const RomMap *m, uint32_t off, size_t want, const char *sha) {
    char hex[41];
    uint8_t *buf = (uint8_t *) malloc(want + 0x100);
    long n;
    int ok;
    if (buf == NULL) return 0;
    n = gunzip_at(rom, size, map_phys(m, off), buf, want + 0x100, NULL);
    ok = n == (long) want;
    if (ok) {
        lm_sha1_hex(buf, want, hex);
        ok = strcmp(hex, sha) == 0;
    }
    free(buf);
    return ok;
}

int rom_shift_check(const uint8_t *rom, size_t size, char *msg, size_t msglen) {
    RomMap m;
    uint32_t moved = 0;
    int i, r = map_parse(rom, size, &m, msg, msglen);
    if (r <= 0) return r;
    for (i = 0; i < m.n; i++)
        if (m.run[i].ps != m.run[i].vs) moved += m.run[i].ve - m.run[i].vs;
    if (m.flags & MAP_RAM_MOVED) {
        snprintf(msg, msglen, "its code or data moved in memory, and the port runs its own code, built from the "
                 "original layout (rebuild the port from the same source, or build the ROM from unchanged code)");
        r = -1;
    } else if (!image_matches(rom, size, &m, ROM_HD_DATA, HD_DATA_SIZE, kHdDataSha1) ||
               !image_matches(rom, size, &m, ROM_FE_DATA, FE_DATA_END - FE_DATA_VRAM, kFeDataSha1)) {
        snprintf(msg, msglen, "its game data differs from Blast Corps (USA) (Rev 1)'s, which the port's own code "
                 "needs");
        r = -1;
    } else {
        snprintf(msg, msglen, "a ROM from the shiftable build (Blast Corps (USA) (Rev 1) with files moved: %d runs in its "
                 "ROM map, 0x%X bytes of the original ROM at other offsets); the game's data is the original's",
                 m.n, (unsigned) moved);
    }
    free(m.run);
    return r;
}

/* A ROM from the shiftable build made before it wrote a ROM map (BCSHMAP1, 2026-10-25): files moved, nothing in it
 * says where.  Rebuilding the map would mean finding every unit by its bytes (an edited file can't be found that way)
 * or reading the offsets out of its own code: not reliable, and such ROMs only came out of testing.  So the port says
 * what it is and refuses it (rom_check / rdram_load): build it again with the current tools, which write the map.
 * Found by Blast Corps (USA) (Rev 1)'s game data image (its SHA-1) inflating from somewhere other than its place. */
long rom_unmapped_shift(const uint8_t *rom, size_t size) {
    uint8_t *buf;
    size_t i;
    long at = -1;
    RomMap m;
    char why[64];
    int r;
    if (size < ROM_ORIG_SIZE) return -1;
    r = map_parse(rom, size, &m, why, sizeof why);
    if (r > 0) free(m.run);
    if (r != 0) return -1;   /* (a map: not this case) */
    m.run = NULL, m.n = 0;
    if (image_matches(rom, size, &m, ROM_HD_DATA, HD_DATA_SIZE, kHdDataSha1)) return -1;   /* (where it belongs) */
    buf = (uint8_t *) malloc(HD_DATA_SIZE + 0x100);
    if (buf == NULL) return -1;
    for (i = 0x1000; i + 18 < size && at < 0; i++) {
        long n;
        char hex[41];
        if (rom[i] != 0x1F || rom[i + 1] != 0x8B || rom[i + 2] != 8) continue;
        n = gunzip_at(rom, size, (uint32_t) i, buf, HD_DATA_SIZE + 0x100, NULL);
        if (n != (long) HD_DATA_SIZE) continue;
        lm_sha1_hex(buf, HD_DATA_SIZE, hex);
        if (strcmp(hex, kHdDataSha1) == 0) at = (long) i;
    }
    free(buf);
    return at >= 0 && at != (long) ROM_HD_DATA ? at : -1;
}

int rdram_load(const char *rom_path) {
    long n;
    const PortCopy *c;
    int copies = 0;
    if (load_rom(rom_path)) return -1;
    /* hd_code .data/.rodata at its link address (the code half is native) */
    n = rom_gunzip(ROM_HD_DATA, (void *) (uintptr_t) HD_DATA_VRAM, HD_BSS_START - HD_DATA_VRAM);
    if (n != (long) HD_DATA_SIZE && rom_unmapped_shift(g_rom, g_rom_size) >= 0)
        return fail("%s: %s", rom_path, ROM_UNMAPPED_SHIFT_MSG);
    if (n != (long) HD_DATA_SIZE)
        return fail("%s: the game data inflated to %ld bytes, expected 0x%X (not Blast Corps (USA) (Rev 1)?)",
                    rom_path, n, HD_DATA_SIZE);
    /* front end: text+ucode then data (the game reloads it per menu visit;
     * here once).  Its .text bytes are only needed for the RSP ucode. */
    n = rom_gunzip(ROM_FE_TEXT, (void *) (uintptr_t) FE_VRAM, FE_DATA_VRAM - FE_VRAM);
    if (n != (long) (FE_DATA_VRAM - FE_VRAM))
        return fail("%s: the front-end code inflated to %ld bytes, expected 0x%X", rom_path, n,
                    FE_DATA_VRAM - FE_VRAM);
    n = rom_gunzip(ROM_FE_DATA, (void *) (uintptr_t) FE_DATA_VRAM, FE_BSS_END - FE_DATA_VRAM);
    if (n <= 0) return fail("%s: the front-end data didn't inflate", rom_path);
    memset((void *) (uintptr_t) (FE_DATA_VRAM + n), 0, FE_BSS_END - FE_DATA_VRAM - n);
    memset((void *) (uintptr_t) HD_BSS_START, 0, HD_BSS_END - HD_BSS_START);
    /* the images' ROM bytes into host order (port/src/load/: generated swap
     * table), before the native initialisers go over the C-defined objects */
    port_load_image_hd();
    port_load_image_fe();
    /* objects defined in the linked C: their native (host-order) initialisers */
    for (c = port_copytab; c->name; c++) {
        if (c->n64_size && c->n64_size != (uint32_t) (c->end - c->start))
            fprintf(stderr, "rdram: WARNING %s: native size 0x%X, N64 size 0x%X\n", c->name,
                    (unsigned) (c->end - c->start), c->n64_size);
        memcpy((void *) (uintptr_t) c->addr, c->start, c->end - c->start);
        copies++;
    }
    {
        const PortPtrSlot *s;
        for (s = port_ptrtab; s->addr; s++) *(uint32_t *) (uintptr_t) s->addr = s->value;
    }
    printf("rdram: hd data 0x%X bytes at 0x%08X, front end data 0x%lX bytes at 0x%08X, %d native "
           "initialisers copied\n", HD_DATA_SIZE, HD_DATA_VRAM, n, FE_DATA_VRAM, copies);
    return 0;
}
