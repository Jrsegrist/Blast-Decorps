/* osGetTime / osGetCount from the virtual clock, with injection.
 *
 * The game reads osGetTime() every frame as a random source (00000.c
 * gFrameDrawTime, camera shake, animation choice), so matching an emulator
 * trace needs the emulator's values:
 *
 *  --gettime FILE     one value per line (decimal or 0x hex), consumed by
 *                     successive osGetTime calls; "@N V" sets the value of
 *                     call N (0-based) and later lines continue from there.
 *                     Calls the file doesn't cover fall back to the clock.
 *  plat_gettime_hook  the same from C, for anything smarter.
 *
 * Frame pacing: running game code takes no virtual time, so the time a frame
 * costs on the N64 (CPU + RCP) is charged when its gfx task completes:
 *  --gfx-cycles N     every frame task completes N counts after it starts
 *                     (default 781250 = one retrace);
 *  --frame-done FILE  per frame: "@N VI[.F]" (or one "VI[.F]" per line from
 *                     frame 1): frame N's task completes at retrace VI plus
 *                     fraction F of a period (default .5), e.g. taken from an
 *                     emulator trace; frames the file doesn't cover use
 *                     --gfx-cycles.
 * --gettime-cost N charges N counts per osGetTime/osGetCount call. */
#include "plat.h"

int (*plat_gettime_hook)(u32 call, u64 now, u64 *t);

typedef struct {
    u64 *v;
    u32 n, cap;
} Table;

static Table g_gettime, g_framedone;
static u32 g_calls;

static void table_set(Table *t, u32 idx, u64 v) {
    if (idx >= t->cap) {
        u32 ncap = t->cap ? t->cap * 2 : 1024;
        while (ncap <= idx) ncap *= 2;
        t->v = host_realloc(t->v, ncap * sizeof(u64));
        while (t->cap < ncap) t->v[t->cap++] = ~0ull;
    }
    t->v[idx] = v;
    if (idx + 1 > t->n) t->n = idx + 1;
}

static u64 table_get(const Table *t, u32 idx) {
    return idx < t->n ? t->v[idx] : ~0ull;
}

/* number: decimal or 0x hex; with `vis`, decimal VI[.F] -> count units */
static int parse_val(const char **pp, u64 *out, int vis) {
    const char *p = *pp;
    u64 v = 0;
    int base = 10, any = 0;
    while (*p == ' ' || *p == '\t') p++;
    if (p[0] == '0' && (p[1] == 'x' || p[1] == 'X')) {
        base = 16;
        p += 2;
    }
    for (;; p++) {
        int d;
        if (*p >= '0' && *p <= '9') d = *p - '0';
        else if (base == 16 && *p >= 'a' && *p <= 'f') d = *p - 'a' + 10;
        else if (base == 16 && *p >= 'A' && *p <= 'F') d = *p - 'A' + 10;
        else break;
        v = v * base + d;
        any = 1;
    }
    if (vis) {
        u64 frac_num = PLAT_VI_PERIOD / 2, scale = 1;
        if (*p == '.') {
            u64 f = 0;
            p++;
            while (*p >= '0' && *p <= '9' && scale < 1000000) f = f * 10 + (*p++ - '0'), scale *= 10;
            frac_num = f * PLAT_VI_PERIOD / scale;
        }
        v = v * PLAT_VI_PERIOD + frac_num;
    }
    *pp = p;
    *out = v;
    return any;
}

static void load_table(Table *t, const char *path, const char *what, u32 first, int vis) {
    unsigned size;
    char *text, *p, *end;
    u32 idx = first, lines = 0;
    text = host_read_file(path, &size);
    if (text == NULL) host_fatal("can't read %s file %s", what, path);
    p = text;
    end = text + size;
    while (p < end) {
        char *eol = p;
        const char *q;
        u64 v, n;
        while (eol < end && *eol != '\n') eol++;
        *eol = 0;
        q = p;
        while (*q == ' ' || *q == '\t') q++;
        if (*q == '@') {
            q++;
            if (parse_val(&q, &n, 0)) idx = (u32) n;
        }
        if (*q != '#' && parse_val(&q, &v, vis)) {
            table_set(t, idx++, v);
            lines++;
        }
        p = eol + 1;
    }
    if (!plat_cfg.quiet) host_log("clock: %u %s values from %s\n", (unsigned) lines, what, path);
}

static Table g_framesp;

void plat_clock_init(const char *gettime_path, const char *frame_done_path) {
    if (gettime_path) load_table(&g_gettime, gettime_path, "--gettime", 0, 0);
    if (frame_done_path) load_table(&g_framedone, frame_done_path, "--frame-done", 1, 1);
    if (plat_cfg.frame_sp_path) load_table(&g_framesp, plat_cfg.frame_sp_path, "--frame-sp", 1, 1);
}

u64 plat_frame_done_time(u32 frame) {
    return table_get(&g_framedone, frame);
}

/* --frame-sp: when frame N's task leaves the RSP (its RDP part runs on) */
u64 plat_frame_sp_time(u32 frame) {
    return table_get(&g_framesp, frame);
}

/* ---- symbols (--syms: `nm -n` of the exe) ---------------------------------
 * Names native code addresses: the keyed clock below and the crash report. */
typedef struct {
    u32 addr;
    const char *name;
} Sym;
static Sym *g_syms;
static u32 g_nsyms;

void plat_syms_load(const char *path) {
    unsigned size;
    char *text = host_read_file(path, &size), *p, *end;
    u32 cap = 0;
    if (text == NULL) host_fatal("can't read --syms file %s", path);
    for (p = text, end = text + size; p < end;) {
        char *eol = p, *name, *dot;
        u64 a = 0;
        int n = 0;
        while (eol < end && *eol != '\n' && *eol != '\r') eol++;
        *eol = 0;
        while (n < 16 && ((*p >= '0' && *p <= '9') || (*p >= 'a' && *p <= 'f')))
            a = a * 16 + (u64) (*p <= '9' ? *p - '0' : *p - 'a' + 10), p++, n++;
        /* "XXXXXXXX T name" (x86_64: 16 digits; the exe is below 4 GB):
         * text symbols only */
        if ((n == 8 || n == 16) && a >> 32 == 0 && p[0] == ' ' && (p[1] == 'T' || p[1] == 't') && p[2] == ' ') {
            name = p + 3;
            /* i686 C symbols have a leading _ (8-digit addresses); x86_64
             * names are the C names, so SDK names like __amMain keep both */
            if (n == 8 && *name == '_') name++;
            dot = name;
            while (*dot && *dot != '.') dot++; /* func.constprop.0, .isra.0, .part.0 */
            *dot = 0;
            if (g_nsyms == cap) {
                cap = cap ? cap * 2 : 4096;
                g_syms = host_realloc(g_syms, cap * sizeof(Sym));
            }
            g_syms[g_nsyms].addr = (u32) a;
            g_syms[g_nsyms].name = name;
            g_nsyms++;
        }
        p = eol + 1;
        while (p < end && (*p == '\n' || *p == '\r')) p++;
    }
}

/* native address of function NAME (0 if unknown) */
u32 plat_sym_addr(const char *name) {
    u32 i;
    for (i = 0; i < g_nsyms; i++) {
        const char *a = g_syms[i].name, *b = name;
        while (*a && *a == *b) a++, b++;
        if (*a == *b) return g_syms[i].addr;
    }
    return 0;
}

/* name of the function containing native address A (NULL if unknown) */
const char *plat_sym_name(u32 a, u32 *off) {
    u32 lo = 0, hi = g_nsyms;
    if (g_nsyms == 0 || a < g_syms[0].addr) return NULL;
    while (hi - lo > 1) {
        u32 mid = (lo + hi) / 2;
        if (g_syms[mid].addr <= a) lo = mid;
        else hi = mid;
    }
    if (off) *off = a - g_syms[lo].addr;
    return g_syms[lo].name;
}

/* ---- keyed clock values (--clock FILE) ------------------------------------
 * The emulator side of the comparison (port/tools/compare.py) logs every
 * osGetTime / osGetCount result with the function it returned to.  Lines
 * "T NAME VI VALUE" / "C NAME VI VALUE" (VALUE hex) queue VALUE for the
 * next call of osGetTime / osGetCount from function NAME in (native) retrace
 * VI, so the values reach the same callers whatever order the threads
 * interleave in.  Other calls get the emulator's clock at the current
 * virtual time ("M R1 C1 PERIOD" line; the virtual clock follows the
 * emulator's through --sync).  Needs --syms. */
typedef struct {
    const char *name;
    u64 *v;
    u32 *vi;    /* the (native) retrace the value was read in */
    u32 n, cap, next, misses, used;
} KeyQ;
#define MAX_KEYS 256
#define NKINDS 5
/* osGetTime, osGetCount, osAiGetLength, cull test, retrace-counter reads */
static KeyQ g_keys[NKINDS][MAX_KEYS];
static u32 g_nkeys[NKINDS];
static u32 g_key_nocaller[NKINDS];
static const char *const g_kind_name[NKINDS] = {"osGetTime", "osGetCount", "osAiGetLength", "cull test",
                                                 "retrace counter"};
/* "M R1 C1 PERIOD": the emulator's count at native time t is
 * C1 + (t - R1 * PLAT_VI_PERIOD) * PERIOD / PLAT_VI_PERIOD */
static int g_emu_map;
static u64 g_emu_r1, g_emu_c1;
static double g_emu_period;

/* a duration of N emulator counts in native counts (N without --clock) */
u64 plat_emu_counts(u64 n) {
    if (!g_emu_map) return n;
    return (u64) ((double) n * PLAT_VI_PERIOD / g_emu_period + 0.5);
}

static u64 emu_time(void) {
    double d = (double) (s64) (plat_now - g_emu_r1 * PLAT_VI_PERIOD) * g_emu_period / PLAT_VI_PERIOD;
    return (u64) ((s64) g_emu_c1 + (s64) d);
}

static int str_eq(const char *a, const char *b) {
    while (*a && *a == *b) a++, b++;
    return *a == *b;
}

static KeyQ *key_find(int kind, const char *name, int add) {
    u32 i;
    for (i = 0; i < g_nkeys[kind]; i++)
        if (str_eq(g_keys[kind][i].name, name)) return &g_keys[kind][i];
    if (!add || g_nkeys[kind] == MAX_KEYS) return NULL;
    g_keys[kind][g_nkeys[kind]].name = name;
    return &g_keys[kind][g_nkeys[kind]++];
}

void plat_clock_keyed_load(const char *path) {
    unsigned size;
    char *text = host_read_file(path, &size), *p, *end;
    u32 lines = 0;
    if (text == NULL) host_fatal("can't read --clock file %s", path);
    if (g_nsyms == 0) host_fatal("--clock needs --syms");
    for (p = text, end = text + size; p < end;) {
        char *eol = p, *name;
        const char *q;
        int kind;
        u64 v;
        KeyQ *k;
        while (eol < end && *eol != '\n') eol++;
        *eol = 0;
        if (p[0] == 'M' && p[1] == ' ') {
            q = p + 2;
            for (g_emu_r1 = 0; *q >= '0' && *q <= '9'; q++) g_emu_r1 = g_emu_r1 * 10 + (u64) (*q - '0');
            for (q++, g_emu_c1 = 0; *q >= '0' && *q <= '9'; q++) g_emu_c1 = g_emu_c1 * 10 + (u64) (*q - '0');
            {
                double f = 0, scale = 1;
                for (q++; *q >= '0' && *q <= '9'; q++) f = f * 10 + (*q - '0');
                if (*q == '.')
                    for (q++; *q >= '0' && *q <= '9'; q++) f = f * 10 + (*q - '0'), scale *= 10;
                g_emu_period = f / scale;
            }
            g_emu_map = g_emu_period > 0;
        } else if ((p[0] == 'T' || p[0] == 'C' || p[0] == 'A' || p[0] == 'U' || p[0] == 'Q') && p[1] == ' ') {
            u32 vi = 0;
            kind = p[0] == 'T' ? 0 : p[0] == 'C' ? 1 : p[0] == 'A' ? 2 : p[0] == 'U' ? 3 : 4;
            name = p + 2;
            q = name;
            while (*q && *q != ' ') q++;
            if (*q == ' ') {
                *(char *) q = 0;
                q++;
                while (*q >= '0' && *q <= '9') vi = vi * 10 + (u32) (*q++ - '0');
                if (*q == ' ') q++;
                /* the value is hex, with or without 0x */
                if (q[0] == '0' && (q[1] == 'x' || q[1] == 'X')) q += 2;
                for (v = 0;; q++) {
                    if (*q >= '0' && *q <= '9') v = v * 16 + (*q - '0');
                    else if (*q >= 'a' && *q <= 'f') v = v * 16 + (*q - 'a' + 10);
                    else if (*q >= 'A' && *q <= 'F') v = v * 16 + (*q - 'A' + 10);
                    else break;
                }
                k = key_find(kind, name, 1);
                if (k == NULL) host_fatal("--clock: too many callers");
                if (k->n == k->cap) {
                    k->cap = k->cap ? k->cap * 2 : 64;
                    k->v = host_realloc(k->v, k->cap * sizeof(u64));
                    k->vi = host_realloc(k->vi, k->cap * sizeof(u32));
                }
                k->vi[k->n] = vi;
                k->v[k->n++] = v;
                lines++;
            }
        }
        p = eol + 1;
    }
    if (!plat_cfg.quiet) host_log("clock: %u keyed values from %s\n", (unsigned) lines, path);
}

/* The caller's next value read in this retrace; else the emulator's clock
 * at this (synchronised) virtual time. */
int plat_clock_key_take(int kind, void *ra, u64 *v) {
    if (g_nkeys[kind] == 0 && !g_emu_map) return 0;
    return plat_clock_key_take_name(kind, plat_sym_name((u32) ra, NULL), v);
}

int plat_clock_key_take_name(int kind, const char *name, u64 *v) {
    KeyQ *k;
    if (g_nkeys[kind] == 0 && !g_emu_map) return 0;
    k = name ? key_find(kind, name, 0) : NULL;
    if (k == NULL) g_key_nocaller[kind]++;
    else {
        /* The clock: a retrace or two either way (the native thread may reach
         * the call off when an earlier switch point couldn't be matched).
         * osAiGetLength sizes the audio frame: a value of the emulator's next
         * frame beats the native AI model's, whose state differs.  Visibility
         * tests are keyed by frame (sent so far), in order. */
        u32 slack = kind == 2 ? 3 : kind == 3 ? 0 : 2;
        u32 now = kind == 3 ? plat_stats.frames : plat_vi_count;
        /* BC_AILEN_SEQ=1 (audio comparisons): osAiGetLength's values in call
         * order, whatever the retrace, so every audio frame has the
         * emulator's size and the synthesizer output can be compared */
        static int seq = -1;
        if (seq < 0) seq = host_env("BC_AILEN_SEQ");
        if (seq && kind == 2) {
            if (k->next < k->n) {
                *v = k->v[k->next++];
                k->used++;
                return 1;
            }
            return 0;
        }
        while (k->next < k->n && k->vi[k->next] + slack < now) k->next++;
        if (k->next < k->n && k->vi[k->next] <= now + slack) {
            *v = k->v[k->next++];
            k->used++;
            return 1;
        }
        k->misses++;
        if (host_verbose)
            host_log("clock: %s from %s in retrace %u: next value is from retrace %u\n", g_kind_name[kind], name,
                     (unsigned) plat_vi_count, k->next < k->n ? (unsigned) k->vi[k->next] : 0u);
    }
    if (!g_emu_map || kind >= 2) return 0;
    *v = emu_time();
    return 1;
}

void plat_clock_report(void) {
    int kind;
    u32 i;
    for (kind = 0; kind < NKINDS; kind++) {
        if (g_nkeys[kind] == 0) continue;
        host_log("clock: %s keyed: %u calls from unkeyed callers\n", g_kind_name[kind], (unsigned) g_key_nocaller[kind]);
        for (i = 0; i < g_nkeys[kind]; i++) {
            KeyQ *k = &g_keys[kind][i];
            host_log("  %-24s used %u of %u, %u calls with no value in their retrace\n", k->name, (unsigned) k->used,
                     (unsigned) k->n, (unsigned) k->misses);
        }
    }
}

/* include/game/port.h PORT_GVI: the game's retrace counter as FN reads it
 * mid-function; following an emulator, the value it read there (it spent CPU
 * time the native code doesn't before the read: cmp_spec.py GVI_FUNCS). */
static u32 g_gvi_last;
static int g_gvi_have_last;

u32 port_gvi_read(const char *fn, u32 v) {
    u64 x;
    if (g_nkeys[4] && plat_clock_key_take_name(4, fn, &x)) v = (u32) x;
    g_gvi_last = v;
    g_gvi_have_last = 1;
    return v;
}

/* include/game/port.h PORT_GVI_LAST: a use of the counter that the original
 * code takes from the register of the PORT_GVI read just before it (IDO kept
 * the loaded value; no second load, so the emulator logs no value for it). */
u32 port_gvi_last(u32 v) {
    return g_gvi_have_last ? g_gvi_last : v;
}

/* include/game/port.h PORT_EMU: following an emulator, the value it read at
 * SITE in this very retrace (no slack: the site runs once per retrace, and a
 * neighbouring retrace's value is exactly what must not be used); else v. */
u32 port_emu_read(const char *site, u32 v) {
    KeyQ *k;
    if (g_nkeys[4] == 0 || (k = key_find(4, site, 0)) == NULL) return v;
    while (k->next < k->n && k->vi[k->next] < plat_vi_count) k->next++;
    if (k->next < k->n && k->vi[k->next] == plat_vi_count) {
        k->used++;
        return (u32) k->v[k->next++];
    }
    k->misses++;
    return v;
}

OSTime osGetTime(void) {
    u32 call = g_calls++;
    u64 v;
    plat_stats.gettime_calls++;
    plat_now += plat_cfg.count_per_gettime;
    plat_sync_point(PORT_RETADDR(), 'g', NULL);
    if (plat_clock_key_take(0, PORT_RETADDR(), &v)) return v;
    if (plat_gettime_hook && plat_gettime_hook(call, plat_now, &v)) return v;
    v = table_get(&g_gettime, call);
    return v != ~0ull ? v : plat_now;
}

int (*plat_getcount_hook)(u32 call, u64 now, u32 *c);
static u32 g_count_calls;

u32 osGetCount(void) {
    u32 call = g_count_calls++, c;
    u64 v;
    plat_now += plat_cfg.count_per_gettime;
    plat_sync_point(PORT_RETADDR(), 'c', NULL);
    if (plat_clock_key_take(1, PORT_RETADDR(), &v)) return (u32) v;
    if (plat_getcount_hook && plat_getcount_hook(call, plat_now, &c)) return c;
    return (u32) plat_now;
}
