/* asp_lle: run captured audio tasks (capture.h, e.g. from bc_headless
 * --audio-capture) on an LLE RSP plugin (cxd4; loaded at run time and used
 * only as a test oracle) with the ROM's own boot code and audio microcode,
 * and compare what it writes with the capture's results (aspmain's).
 *   asp_lle CAPTURE HD_CODE_BIN [-n MAX] [-v] [-lle PLUGIN.so]
 * HD_CODE_BIN: the decompressed hd_code segment (build/hd_code.us.v11.bin),
 * which holds rspboot (0x802E6820), the audio microcode text (0x802E68F0)
 * and its data (0x8030EB90).  Nothing from it is written anywhere.
 * The harness is the plugin's host: it exports the few mupen64plus core
 * functions the plugin looks up (build with -rdynamic). */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include "m64p_min.h"
#include "capfile.h"

#define HD_VRAM 0x802447C0u
#define BOOT 0x802E6820u
#define TEXT 0x802E68F0u
#define DATA 0x8030EB90u

/* ---- the core API the plugin calls --------------------------------------- */
EXPORT m64p_error CoreGetAPIVersions(int *cfg, int *dbg, int *vid, int *extra) {
    if (cfg) *cfg = 0x020100;
    if (dbg) *dbg = 0x020000;
    if (vid) *vid = 0x030000;
    if (extra) *extra = 0;
    return 0;
}
EXPORT m64p_error ConfigOpenSection(const char *name, void **h) { (void) name; *h = (void *) 1; return 0; }
EXPORT m64p_error ConfigDeleteSection(const char *name) { (void) name; return 0; }
EXPORT m64p_error ConfigSetParameter(void *h, const char *n, int t, const void *v) { (void) h; (void) n; (void) t; (void) v; return 0; }
EXPORT m64p_error ConfigGetParameter(void *h, const char *n, int t, void *v, int s) {
    (void) h; (void) t; (void) s;
    if (!strcmp(n, "Version")) { *(float *) v = 1.0f; return 0; }
    return 1;
}
EXPORT m64p_error ConfigSetDefaultFloat(void *h, const char *n, float v, const char *d) { (void) h; (void) n; (void) v; (void) d; return 0; }
EXPORT m64p_error ConfigSetDefaultBool(void *h, const char *n, int v, const char *d) { (void) h; (void) n; (void) v; (void) d; return 0; }
EXPORT int ConfigGetParamBool(void *h, const char *n) { (void) h; (void) n; return 0; }
EXPORT m64p_error CoreDoCommand(int c, int p, void *d) { (void) c; (void) p; (void) d; return 0; }

static void dbgcb(void *ctx, int level, const char *msg) {
    (void) ctx;
    if (level <= 2) fprintf(stderr, "lle: %s\n", msg);
}

static uint8_t *rdram;          /* emulator layout: byte A at A^3 */
static uint8_t spmem[0x2000];   /* DMEM, IMEM */
static unsigned int mi_intr, sp_mem, sp_dram, sp_rd, sp_wr, sp_status, sp_full, sp_busy, sp_pc, sp_sema;
static unsigned int dpc[8];
static void check_int(void) {}
static void nop(void) {}

static void put_be(uint32_t addr, const uint8_t *src, uint32_t len) {
    uint32_t i;
    for (i = 0; i < len; i++) rdram[((addr + i) & 0xFFFFFF) ^ 3] = src[i];
}

int main(int argc, char **argv) {
    const char *plug = NULL;
    char def[512];
    void *lib, *self;
    m64p_error (*startup)(m64p_dynlib_handle, void *, void (*)(void *, int, const char *));
    void (*initiate)(RSP_INFO, unsigned int *);
    unsigned int (*cycles)(unsigned int);
    RSP_INFO info;
    FILE *f, *hb;
    uint8_t *hd;
    long hdsize;
    CapHeader h;
    static CapRecord c;
    int rc = 0;
    unsigned max = ~0u, verbose = 0, n = 0, same = 0, diff = 0, empty = 0, i;
    unsigned long long bytes = 0, compared = 0;

    if (argc < 3) {
        fprintf(stderr, "usage: asp_lle CAPTURE HD_CODE_BIN [-n MAX] [-v] [-lle PLUGIN.so]\n");
        return 2;
    }
    for (i = 3; i < (unsigned) argc; i++) {
        if (!strcmp(argv[i], "-n") && i + 1 < (unsigned) argc) max = (unsigned) atoi(argv[++i]);
        else if (!strcmp(argv[i], "-v")) verbose = 1;
        else if (!strcmp(argv[i], "-lle") && i + 1 < (unsigned) argc) plug = argv[++i];
    }
    if (!plug) {
        snprintf(def, sizeof def, "%s/thirdparty/ref/mupen64plus-rsp-cxd4/projects/unix/mupen64plus-rsp-cxd4-sse2.so",
                 getenv("HOME"));
        plug = def;
    }
    hb = fopen(argv[2], "rb");
    if (!hb) { perror(argv[2]); return 1; }
    fseek(hb, 0, SEEK_END);
    hdsize = ftell(hb);
    fseek(hb, 0, SEEK_SET);
    hd = malloc(hdsize);
    if (fread(hd, 1, hdsize, hb) != (size_t) hdsize) return 1;
    fclose(hb);
    if (DATA + 0x800 - HD_VRAM > (uint32_t) hdsize) { fprintf(stderr, "hd_code bin too small\n"); return 1; }

    rdram = mmap(NULL, 0x1000000, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    lib = dlopen(plug, RTLD_NOW | RTLD_LOCAL);
    if (!lib) { fprintf(stderr, "%s\n", dlerror()); return 1; }
    self = dlopen(NULL, RTLD_NOW);
    startup = dlsym(lib, "PluginStartup");
    initiate = dlsym(lib, "InitiateRSP");
    cycles = dlsym(lib, "DoRspCycles");
    if (startup(self, NULL, dbgcb) != 0) { fprintf(stderr, "PluginStartup failed\n"); return 1; }
    memset(&info, 0, sizeof info);
    info.RDRAM = rdram;
    info.DMEM = spmem;
    info.IMEM = spmem + 0x1000;
    info.MI_INTR_REG = &mi_intr;
    info.SP_MEM_ADDR_REG = &sp_mem;
    info.SP_DRAM_ADDR_REG = &sp_dram;
    info.SP_RD_LEN_REG = &sp_rd;
    info.SP_WR_LEN_REG = &sp_wr;
    info.SP_STATUS_REG = &sp_status;
    info.SP_DMA_FULL_REG = &sp_full;
    info.SP_DMA_BUSY_REG = &sp_busy;
    info.SP_PC_REG = &sp_pc;
    info.SP_SEMAPHORE_REG = &sp_sema;
    info.DPC_START_REG = &dpc[0];
    info.DPC_END_REG = &dpc[1];
    info.DPC_CURRENT_REG = &dpc[2];
    info.DPC_STATUS_REG = &dpc[3];
    info.DPC_CLOCK_REG = &dpc[4];
    info.DPC_BUFBUSY_REG = &dpc[5];
    info.DPC_PIPEBUSY_REG = &dpc[6];
    info.DPC_TMEM_REG = &dpc[7];
    info.CheckInterrupts = check_int;
    info.ProcessDlistList = nop;
    info.ProcessAlistList = nop;
    info.ProcessRdpList = nop;
    info.ShowCFB = nop;
    initiate(info, NULL);

    /* the microcode text and data and the boot code, at their addresses */
    put_be(TEXT & 0xFFFFFF, hd + (TEXT - HD_VRAM), 0xF80);
    put_be(DATA & 0xFFFFFF, hd + (DATA - HD_VRAM), 0x800);

    f = fopen(argv[1], "rb");
    if (!f) { perror(argv[1]); return 1; }
    while (n < max && (rc = cap_next(f, &c)) == 1) {
        unsigned nd = 0, shown = 0;
        uint32_t task[16];
        h = c.h;
        if ((int32_t) h.task.data_size <= 0) {   /* aspmain skips empty lists (see asp_run_task) */
            empty++;
            continue;
        }
        cap_apply_reads(&c, put_be);
        /* RSP state: DMEM as before the task, the OSTask at 0xFC0, rspboot in IMEM */
        for (i = 0; i < 0xFC0; i++) spmem[i ^ 3] = c.dmem[i];
        memset(task, 0, sizeof task);
        task[0] = 2;                       /* M_AUDTASK */
        task[2] = BOOT;
        task[3] = 0xD0;
        task[4] = TEXT;
        task[5] = 0xF80;
        task[6] = h.task.ucode_data;
        task[7] = h.task.ucode_data_size;
        task[12] = h.task.data_ptr;
        task[13] = h.task.data_size;
        memcpy(spmem + 0xFC0, task, sizeof task);   /* host words = emulator layout */
        for (i = 0; i < 0xD0; i++) spmem[0x1000 + (i ^ 3)] = hd[BOOT - HD_VRAM + i];
        sp_pc = 0;
        sp_status = 0;
        sp_full = sp_busy = sp_sema = 0;
        mi_intr = 0;
        cycles(0x7FFFFFFF);
        for (i = 0; i < c.wr.n; i++) {
            uint32_t a;
            compared += c.wr.r[i].len;
            for (a = c.wr.r[i].addr; a < c.wr.r[i].addr + c.wr.r[i].len; a++) {
                uint8_t lle = rdram[a ^ 3];
                if (!c.mark[a]) continue;
                if (lle != c.exp[a]) {
                    if (nd == 0 && diff < 30) printf("task %u: differs\n", h.index);
                    if (shown < 4 && diff < 30) {
                        printf("  %06x (in %06x+%x): aspmain %02x lle %02x\n", a, c.wr.r[i].addr, c.wr.r[i].len,
                               c.exp[a], lle);
                        shown++;
                    }
                    nd++;
                }
            }
        }
        if (verbose) printf("task %u: status %08x pc %03x, %u bytes differ\n", h.index, sp_status, sp_pc, nd);
        n++;
        if (nd) { diff++; bytes += nd; } else same++;
    }
    if (rc < 0) fprintf(stderr, "bad capture record\n");
    printf("lle: %u tasks, %u identical, %u different (%llu of %llu written bytes); %u empty lists skipped\n", n, same,
           diff, bytes, compared, empty);
    return diff != 0;
}
