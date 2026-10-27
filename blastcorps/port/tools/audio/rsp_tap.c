/* rsp_tap.so: a mupen64plus RSP plugin that forwards everything to an LLE
 * RSP plugin (cxd4, used here only as a test oracle; it is loaded at run
 * time and nothing of it is copied) and checks every audio task against the
 * port's interpreter (port/src/audio/aspmain.c): before the task it copies
 * RDRAM and DMEM, runs the LLE RSP, runs aspmain on the copy, and compares
 * all 8 MB of RDRAM (and reports the sample-buffer DMEM).
 * Environment:
 *   TAP_LLE=path       the LLE RSP plugin (default: cxd4 under ~/thirdparty/ref)
 *   TAP_OUT=file       report (default stderr)
 *   TAP_CAPTURE=file   write each audio task's inputs and the LLE's results
 *                      (capture.h; asp_replay replays them offline)
 *   TAP_CAPTURE_MAX=n  at most n tasks captured (default all)
 *   TAP_HLE=path       non-audio tasks go to this plugin (e.g. rsp-hle)
 *   TAP_LLE_UCODE=a,b  ...except tasks of these microcodes (hex RDRAM
 *                      addresses), which run on the LLE RSP too
 *   TAP_NOCHECK=1      audio tasks on the LLE RSP only (no aspmain check)
 * Linux only (dlopen). */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "m64p_min.h"
#include "capture.h"
#include "ranges.h"

#define RDRAM_SIZE 0x800000u

static void *lle, *hle;
static m64p_error (*lle_startup)(m64p_dynlib_handle, void *, void (*)(void *, int, const char *));
static m64p_error (*lle_shutdown)(void);
static void (*lle_initiate)(RSP_INFO, unsigned int *);
static unsigned int (*lle_cycles)(unsigned int);
static void (*lle_romclosed)(void);
/* other tasks (graphics) go to this plugin when TAP_HLE is set (e.g. the
 * HLE RSP: faster, and what the project's emulator runs use) */
static m64p_error (*hle_startup)(m64p_dynlib_handle, void *, void (*)(void *, int, const char *));
static m64p_error (*hle_shutdown)(void);
static void (*hle_initiate)(RSP_INFO, unsigned int *);
static unsigned int (*hle_cycles)(unsigned int);
static void (*hle_romclosed)(void);
static RSP_INFO info;
static FILE *rep, *cap;
static unsigned cap_max = ~0u, cap_n;
static uint8_t *pre, *work;   /* RDRAM copies (emulator layout: byte A at A^3) */
static AspState st;
static unsigned n_tasks, n_same, n_diff, n_dmem_diff, n_empty;
static unsigned long long diff_bytes;
static RangeList rd, wr;
static uint32_t lle_ucode[8];
static unsigned n_lle_ucode, n_lle_other, no_check;

static void logf_(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vfprintf(rep ? rep : stderr, fmt, ap);
    va_end(ap);
    fflush(rep ? rep : stderr);
}

static void bus_read(void *ctx, uint32_t addr, uint8_t *dst, uint32_t len, int kind) {
    uint32_t i;
    (void) ctx;
    for (i = 0; i < len; i++) dst[i] = work[((addr + i) & (RDRAM_SIZE - 1)) ^ 3];
    ranges_add(&rd, addr & (RDRAM_SIZE - 1), len, (uint32_t) kind);
}
static void bus_write(void *ctx, uint32_t addr, const uint8_t *src, uint32_t len, int kind) {
    uint32_t i;
    (void) ctx;
    for (i = 0; i < len; i++) work[((addr + i) & (RDRAM_SIZE - 1)) ^ 3] = src[i];
    ranges_add(&wr, addr & (RDRAM_SIZE - 1), len, (uint32_t) kind);
}

EXPORT m64p_error CALL PluginStartup(m64p_dynlib_handle core, void *ctx, void (*dbg)(void *, int, const char *)) {
    const char *p = getenv("TAP_LLE");
    char def[512];
    if (!p) {
        snprintf(def, sizeof def, "%s/thirdparty/ref/mupen64plus-rsp-cxd4/projects/unix/mupen64plus-rsp-cxd4-sse2.so",
                 getenv("HOME"));
        p = def;
    }
    lle = dlopen(p, RTLD_NOW | RTLD_LOCAL);
    if (!lle) {
        fprintf(stderr, "rsp_tap: %s\n", dlerror());
        return M64ERR_INPUT_INVALID;
    }
    lle_startup = dlsym(lle, "PluginStartup");
    lle_shutdown = dlsym(lle, "PluginShutdown");
    lle_initiate = dlsym(lle, "InitiateRSP");
    lle_cycles = dlsym(lle, "DoRspCycles");
    lle_romclosed = dlsym(lle, "RomClosed");
    if (getenv("TAP_HLE")) {
        hle = dlopen(getenv("TAP_HLE"), RTLD_NOW | RTLD_LOCAL);
        if (!hle) {
            fprintf(stderr, "rsp_tap: %s\n", dlerror());
            return M64ERR_INPUT_INVALID;
        }
        hle_startup = dlsym(hle, "PluginStartup");
        hle_shutdown = dlsym(hle, "PluginShutdown");
        hle_initiate = dlsym(hle, "InitiateRSP");
        hle_cycles = dlsym(hle, "DoRspCycles");
        hle_romclosed = dlsym(hle, "RomClosed");
        if (hle_startup(core, ctx, dbg) != M64ERR_SUCCESS) return M64ERR_INPUT_INVALID;
    }
    if (getenv("TAP_OUT")) rep = fopen(getenv("TAP_OUT"), "w");
    if (getenv("TAP_CAPTURE")) cap = fopen(getenv("TAP_CAPTURE"), "wb");
    if (getenv("TAP_CAPTURE_MAX")) cap_max = (unsigned) atoi(getenv("TAP_CAPTURE_MAX"));
    if (getenv("TAP_LLE_UCODE")) {
        const char *s = getenv("TAP_LLE_UCODE");
        char *e;
        while (*s && n_lle_ucode < 8) {
            lle_ucode[n_lle_ucode++] = (uint32_t) strtoul(s, &e, 16) & 0x7FFFFF;
            if (e == s) break;
            s = *e == ',' ? e + 1 : e;
        }
    }
    no_check = getenv("TAP_NOCHECK") != NULL;
    pre = malloc(RDRAM_SIZE);
    work = malloc(RDRAM_SIZE);
    asp_init(&st);
    st.log = logf_;
    return lle_startup(core, ctx, dbg);
}

static void summary(void) {
    unsigned i;
    logf_("tap: audio tasks %u, identical %u, different %u (%llu bytes), DMEM buffer area different %u; "
          "%u empty lists not compared; %u other tasks on the LLE RSP (TAP_LLE_UCODE)%s\n", n_tasks, n_same, n_diff,
          diff_bytes, n_dmem_diff, n_empty, n_lle_other, no_check ? "; TAP_NOCHECK: not compared" : "");
    logf_("tap: commands %u:", st.cmds);
    for (i = 0; i < 16; i++) logf_(" %u", st.cmd_count[i]);
    logf_("\n");
}

EXPORT m64p_error CALL PluginShutdown(void) {
    summary();
    if (rep) fclose(rep);
    if (cap) fclose(cap);
    rep = cap = NULL;
    if (hle_shutdown) hle_shutdown();
    return lle_shutdown ? lle_shutdown() : M64ERR_SUCCESS;
}

EXPORT m64p_error CALL PluginGetVersion(m64p_plugin_type *type, int *ver, int *api, const char **name, int *caps) {
    if (type) *type = M64PLUGIN_RSP;
    if (ver) *ver = 0x010000;
    if (api) *api = 0x020000;
    if (name) *name = "rsp-tap (LLE + aspmain check)";
    if (caps) *caps = 0;
    return M64ERR_SUCCESS;
}

EXPORT void CALL InitiateRSP(RSP_INFO i, unsigned int *cycles) {
    info = i;
    lle_initiate(i, cycles);
    if (hle_initiate) hle_initiate(i, cycles);
}

EXPORT void CALL RomClosed(void) {
    summary();
    if (cap) fflush(cap);
    if (lle_romclosed) lle_romclosed();
    if (hle_romclosed) hle_romclosed();
}

static uint32_t dw(uint32_t off) { return *(uint32_t *) (info.DMEM + off); }

static void put_range(const uint8_t *img, const Range *r) {
    CapRange c = { r->addr, r->len, r->kind };
    uint32_t i;
    fwrite(&c, sizeof c, 1, cap);
    for (i = 0; i < r->len; i++) fputc(img[((r->addr + i) & (RDRAM_SIZE - 1)) ^ 3], cap);
}

EXPORT unsigned int CALL DoRspCycles(unsigned int cycles) {
    AspTask t;
    unsigned r, i;
    uint8_t dmem_pre[4096];
    AspVU vu_pre;
    uint32_t a, ndiff = 0, shown = 0;

    if ((*info.SP_STATUS_REG & 3) != 0) return hle_cycles ? hle_cycles(cycles) : lle_cycles(cycles);
    if (dw(0xFC0) != 2) {
        /* TAP_LLE_UCODE: tasks of these microcodes run on the LLE RSP too
         * (e.g. Blast Corps' visibility test, whose output the CPU reads and
         * which the HLE plugin doesn't know) */
        unsigned k;
        for (k = 0; k < n_lle_ucode; k++)
            if ((dw(0xFC0 + 0x10) & 0x7FFFFF) == lle_ucode[k]) {
                n_lle_other++;
                return lle_cycles(cycles);
            }
        return hle_cycles ? hle_cycles(cycles) : lle_cycles(cycles);
    }
    if (no_check) {   /* TAP_NOCHECK: LLE only, no aspmain comparison */
        n_tasks++;
        return lle_cycles(cycles);
    }

    memcpy(pre, info.RDRAM, RDRAM_SIZE);
    memcpy(work, info.RDRAM, RDRAM_SIZE);
    for (i = 0; i < 4096; i++) dmem_pre[i] = info.DMEM[i ^ 3];
    t.ucode_data = dw(0xFC0 + 0x18);
    t.ucode_data_size = dw(0xFC0 + 0x1C);
    t.data_ptr = dw(0xFC0 + 0x30);
    t.data_size = dw(0xFC0 + 0x34);

    r = lle_cycles(cycles);
    if ((int32_t) t.data_size <= 0) {   /* aspmain skips empty lists (asp_run_task); not compared */
        n_empty++;
        return r;
    }

    memcpy(st.dmem, dmem_pre, 4096);
    vu_pre = st.vu;
    rd.n = wr.n = 0;
    asp_run_task(&st, &(AspBus){ NULL, bus_read, bus_write }, &t);
    n_tasks++;
    /* everything the LLE changed counts as written too */
    for (a = 0; a < RDRAM_SIZE; a += 4)
        if (*(uint32_t *) (info.RDRAM + a) != *(uint32_t *) (pre + a)) ranges_add(&wr, a, 4, ASP_DMA_DATA);
    ranges_merge(&rd);
    ranges_merge(&wr);
    for (i = 0; i < wr.n; i++)
        for (a = wr.r[i].addr; a < wr.r[i].addr + wr.r[i].len; a++) {
            uint32_t x = a ^ 3;
            if (work[x] != info.RDRAM[x]) {
                if (ndiff == 0 && n_diff < 40) logf_("task %u: RDRAM differs\n", n_tasks);
                if (shown < 6 && n_diff < 40) {
                    uint32_t b = a & ~1u;
                    logf_("  %06x: aspmain %02x%02x lle %02x%02x\n", b, work[b ^ 3], work[(b + 1) ^ 3],
                          info.RDRAM[b ^ 3], info.RDRAM[(b + 1) ^ 3]);
                    shown++;
                }
                ndiff++;
            }
        }
    if (n_tasks % 2000 == 0) summary();
    if (ndiff == 0) n_same++;
    else {
        n_diff++;
        diff_bytes += ndiff;
    }
    for (i = 0x5C0; i < 0xF90; i++)
        if (st.dmem[i] != info.DMEM[i ^ 3]) {
            n_dmem_diff++;
            if (n_dmem_diff <= 10) logf_("task %u: DMEM %03x aspmain %02x lle %02x\n", n_tasks, i, st.dmem[i], info.DMEM[i ^ 3]);
            break;
        }
    if (cap && cap_n < cap_max) {
        CapHeader h;
        memset(&h, 0, sizeof h);
        memcpy(h.magic, "ASPC", 4);
        h.index = n_tasks;
        h.task = t;
        h.n_reads = rd.n;
        h.n_writes = wr.n;
        fwrite(&h, sizeof h, 1, cap);
        fwrite(dmem_pre, 4096, 1, cap);
        fwrite(&vu_pre, sizeof vu_pre, 1, cap);
        for (i = 0; i < rd.n; i++) put_range(pre, &rd.r[i]);
        for (i = 0; i < wr.n; i++) put_range(info.RDRAM, &wr.r[i]);
        cap_n++;
    }
    return r;
}
