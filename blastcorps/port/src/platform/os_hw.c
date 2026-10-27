/* VI, PI, SP/DP, AI and the small libultra leftovers (caches, address
 * translation, osInitialize), headless. */
#include "plat.h"
#include "../audio/port_audio.h"
void port_mark_gfx_task(unsigned int dl); /* load/swap.c */

/* ---- osInitialize --------------------------------------------------------- */

u8 *plat_rom;
u32 plat_rom_size;

#define LOWMEM(off) (*(u32 *) (0x80000300u + (off)))

void osInitialize(void) {
    /* what IPL3 + the SDK's osInitialize leave in low memory */
    LOWMEM(0x00) = 1;           /* osTvType: NTSC */
    LOWMEM(0x04) = 0;           /* osRomType: cartridge */
    LOWMEM(0x08) = 0xB0000000;  /* osRomBase */
    LOWMEM(0x0C) = 0;           /* osResetType: cold */
    LOWMEM(0x18) = 0x00800000;  /* osMemSize (the emulator's default 8 MB) */
    /* the old SDK takes osViClock from its .data (NTSC value); set it in
     * host order whatever the image swap did with it */
    osViClock = 0x02E6D354;
    host_set_fpu_mode();
}

/* ---- libm / libc bits ------------------------------------------------------ */

/* ultralib's sinf/cosf are weak aliases of __sinf/__cosf; strong definitions
 * here make sure the C runtime's versions never win (the game needs the
 * SDK's polynomial for N64-identical results). */
extern float __sinf(float);
extern float __cosf(float);
float sinf(float x) {
    return __sinf(x);
}
float cosf(float x) {
    return __cosf(x);
}

void bcopy(const void *src, void *dst, int n) {
    PORT_MEMMOVE(dst, src, n);
}
void bzero(void *p, int n) {
    PORT_MEMSET(p, 0, n);
}

/* ---- caches, address translation ----------------------------------------- */

void osInvalDCache(void *p, s32 n) {
    (void) p;
    (void) n;
}
void osInvalICache(void *p, s32 n) {
    (void) p;
    (void) n;
}
void osWritebackDCache(void *p, s32 n) {
    (void) p;
    (void) n;
}
void osWritebackDCacheAll(void) {
}

u32 osVirtualToPhysical(void *addr) {
    u32 a = (u32) addr;
    /* KSEG0/KSEG1 -> physical.  Host pointers (stack, heap of the exe) have no
     * N64 physical address: pass them through so a bug shows up as a host
     * address in a DL rather than as silent aliasing. */
    if (a >= 0x80000000u && a < 0xC0000000u) return a & 0x1FFFFFFF;
    return a;
}

/* ---- VI ------------------------------------------------------------------ */

static void *g_vi_cur = (void *) 0x80000000, *g_vi_next = (void *) 0x80000000;
static OSMesgQueue *g_vi_mq;
static OSMesg g_vi_msg;
static u32 g_vi_retrace_count = 1, g_vi_retrace_left = 1;
static OSViMode *g_vi_mode;
static u8 g_vi_black;
/* the VI control word of the next context (libultra __osViNext->control):
 * osViSetMode loads the mode's, osViSetSpecialFeatures edits it */
static u32 g_vi_ctrl;

void osCreateViManager(OSPri pri) {
    (void) pri;
}

void osViSetMode(OSViMode *m) {
    g_vi_mode = m;
    g_vi_ctrl = m->comRegs.ctrl;
}

void osViBlack(u8 active) {
    g_vi_black = active;
}

/* Cumulative, like the ROM's (ultralib io/visetspecial.c): each call edits the
 * control word.  Blast Corps calls it twice (GAMMA_OFF, then DITHER_FILTER_ON);
 * keeping only the last call's bits left the mode's gamma correction on, and
 * the windowed build's picture came out washed out. */
void osViSetSpecialFeatures(u32 f) {
    if (f & OS_VI_GAMMA_ON) g_vi_ctrl |= VI_CTRL_GAMMA_ON;
    if (f & OS_VI_GAMMA_OFF) g_vi_ctrl &= ~VI_CTRL_GAMMA_ON;
    if (f & OS_VI_GAMMA_DITHER_ON) g_vi_ctrl |= VI_CTRL_GAMMA_DITHER_ON;
    if (f & OS_VI_GAMMA_DITHER_OFF) g_vi_ctrl &= ~VI_CTRL_GAMMA_DITHER_ON;
    if (f & OS_VI_DIVOT_ON) g_vi_ctrl |= VI_CTRL_DIVOT_ON;
    if (f & OS_VI_DIVOT_OFF) g_vi_ctrl &= ~VI_CTRL_DIVOT_ON;
    if (f & OS_VI_DITHER_FILTER_ON) {
        g_vi_ctrl |= VI_CTRL_DITHER_FILTER_ON;
        g_vi_ctrl &= ~VI_CTRL_ANTIALIAS_MASK;
    }
    if (f & OS_VI_DITHER_FILTER_OFF) {
        g_vi_ctrl &= ~VI_CTRL_DITHER_FILTER_ON;
        if (g_vi_mode != NULL) g_vi_ctrl |= g_vi_mode->comRegs.ctrl & VI_CTRL_ANTIALIAS_MASK;
    }
}

void osViSetEvent(OSMesgQueue *mq, OSMesg msg, u32 retraceCount) {
    g_vi_mq = mq;
    g_vi_msg = msg;
    g_vi_retrace_count = retraceCount ? retraceCount : 1;
    g_vi_retrace_left = g_vi_retrace_count;
}

void osViSwapBuffer(void *fb) {
    g_vi_next = fb;
}

void *osViGetCurrentFramebuffer(void) {
    return g_vi_cur;
}

void *osViGetNextFramebuffer(void) {
    return g_vi_next;
}

/* The registers libultra's VI manager programs for the frame on screen
 * (__osViSwapContext: field 0 of a non-interlaced mode; osViBlack blanks
 * the picture through hStart; osViSetSpecialFeatures edits the control
 * word).  Only the windowed build looks at them. */
static void vi_regs(HostViRegs *r) {
    const OSViMode *m = g_vi_mode;
    PORT_MEMSET(r, 0, sizeof *r);
    if (m == NULL) return;
    r->status = g_vi_ctrl;
    r->origin = osVirtualToPhysical(g_vi_cur) + m->fldRegs[0].origin;
    r->width = m->comRegs.width;
    r->burst = m->comRegs.burst;
    r->v_sync = m->comRegs.vSync;
    r->h_sync = m->comRegs.hSync;
    r->leap = m->comRegs.leap;
    r->h_start = g_vi_black ? 0 : m->comRegs.hStart;
    r->x_scale = m->comRegs.xScale;
    r->y_scale = m->fldRegs[0].yScale;
    r->v_start = m->fldRegs[0].vStart;
    r->v_burst = m->fldRegs[0].vBurst;
    r->intr = m->fldRegs[0].vIntr;
}

/* the VI manager's work at each retrace */
void plat_vi_retrace(void) {
    g_vi_cur = g_vi_next;
    if (plat_cfg.live != NULL) {
        HostViRegs r;
        vi_regs(&r);
        plat_cfg.live->vi(&r, plat_vi_count, plat_now, plat_stats.frames);
    }
    if (g_vi_mq != NULL && --g_vi_retrace_left == 0) {
        g_vi_retrace_left = g_vi_retrace_count;
        osSendMesg(g_vi_mq, g_vi_msg, OS_MESG_NOBLOCK);
    }
}

/* ---- PI ------------------------------------------------------------------ */

void osCreatePiManager(OSPri pri, OSMesgQueue *cmdQ, OSMesg *cmdBuf, s32 cmdMsgCnt) {
    (void) pri;
    (void) cmdQ;
    (void) cmdBuf;
    (void) cmdMsgCnt;
}

static u32 rom_offset(u32 devAddr) {
    /* the game passes ROM offsets; accept PI bus addresses too */
    if (devAddr >= 0x10000000u && devAddr < 0x20000000u) devAddr -= 0x10000000u;
    return devAddr;
}

/* The DMA happens at once (the PI manager outranks every game thread, and
 * the transfer takes no virtual time); the completion message goes to mq
 * like the PI manager's. */
s32 osPiStartDma(OSIoMesg *mb, s32 pri, s32 direction, u32 devAddr, void *vAddr, u32 nbytes,
                 OSMesgQueue *mq) {
    u32 off = rom_offset(devAddr);
    const double t0 = host_ms();   /* (host_prof: bc.exe's long game steps) */
    mb->hdr.type = OS_MESG_TYPE_DMAREAD;
    mb->hdr.pri = (u8) pri;
    mb->hdr.retQueue = mq;
    mb->dramAddr = vAddr;
    mb->devAddr = devAddr;
    mb->size = nbytes;
    if (direction == OS_READ) {
        u8 *dst = vAddr;
        unsigned int phys;
        /* (a ROM from the shiftable build: this ROM's place and length of the original offset's file) */
        u32 len = port_dma_len(off, nbytes, rom_map_dma(off, nbytes, &phys));
        u32 n = len, avail = phys < plat_rom_size ? plat_rom_size - phys : 0;
        if (n > avail) {
            if (host_verbose) host_log("pi: dma past ROM end (0x%X+0x%X)\n", (unsigned) phys, (unsigned) n);
            PORT_MEMSET(dst + avail, 0, n - avail);
            n = avail;
        }
        if (n) PORT_MEMCPY(dst, plat_rom + phys, n);
        if (phys != off || len != nbytes) {
            off = rom_map_virt(off);   /* (a piece of a hd_rom_load: its original offset) */
            if (host_verbose)
                host_log("pi: dma rom %06X+%X: this ROM's %06X+%X\n", (unsigned) off, (unsigned) nbytes, (unsigned) phys,
                         (unsigned) len);
        }
        rom_map_fixup(dst, off, len);
        port_on_dma((u32) vAddr, off, len);
        plat_stats.pi_dmas++;
        plat_stats.pi_bytes += nbytes;
    } else if (host_verbose) {
        host_log("pi: write DMA to 0x%X ignored\n", (unsigned) devAddr);
    }
    host_prof.dma_ms += host_ms() - t0;
    host_prof.dma_n++;
    host_prof.dma_bytes += nbytes;
    if (mq != NULL) osSendMesg(mq, (OSMesg) mb, OS_MESG_NOBLOCK);
    return 0;
}

/* the visibility test's display list (5FD50.c cull_rsp_test): cull_start's locals on the host stack, whose
 * address changes from run to run, so the task in N64 memory holds the list's N64 address instead */
static const u32 *g_cull_dl;
void port_cull_data(const void *dl) {
    g_cull_dl = (const u32 *) dl;
}

/* BC_CULL_LOG=1: one line per test on stderr (answer, matrices, corners) in
 * the format of the LLE reference logs (scratchpad tools, gfx2 notes) */
static void cull_log(const u32 *dl, u32 size, u32 v) {
    static int on = -1;
    int i, j;
    if (on < 0) on = host_env("BC_CULL_LOG");
    if (!on) return;
    host_log("cull native | v=%08x", (unsigned) v);
    for (i = 0; i + 1 < (int) (size / 4); i += 2) {
        u32 w0 = dl[i], w1 = dl[i + 1];
        if ((w0 >> 24) == 0x01) {
            const u32 *m = (const u32 *) (unsigned long) (0x80000000u | (w1 & 0x7FFFFF));
            host_log(" mtx%02x=", (unsigned) ((w0 >> 16) & 0xFF));
            for (j = 0; j < 16; j++) host_log("%08x", (unsigned) m[j]);
        } else if ((w0 >> 24) == 0x04) {
            const Vtx *vx = (const Vtx *) (unsigned long) (w1 | ((u32) (unsigned long) dl & 0xE0000000u));
            host_log(" vtx=");
            for (j = 0; j < 8; j++)
                host_log("%04x%04x%04x%020x", (unsigned) (u16) vx[j].v.ob[0], (unsigned) (u16) vx[j].v.ob[1],
                        (unsigned) (u16) vx[j].v.ob[2], 0u);
        }
    }
    host_log("\n");
}

/* One 32-bit PI read.  Inside the ROM: the big-endian word, as lw gives it.
 * The game's only use (hd_boot) reads 16 words at 0xFFB000, past the
 * ROM, as a debug command-line *string*: there the words are returned so
 * that their bytes in host memory spell plat_cfg.cmdline (empty: zeros), at
 * most 63 bytes of it so that the 64-byte buffer always ends in a zero
 * (parse_cmdline reads up to one). */
s32 osPiRawReadIo(u32 devAddr, u32 *data) {
    u32 off = rom_offset(devAddr);
    unsigned int phys;
    rom_map_dma(off, 4, &phys);
    if (phys + 4 <= plat_rom_size) {
        const u8 *p = plat_rom + phys;
        *data = ((u32) p[0] << 24) | ((u32) p[1] << 16) | ((u32) p[2] << 8) | p[3];
    } else {
        u8 b[4] = {0, 0, 0, 0};
        const char *s = plat_cfg.cmdline;
        u32 i, base = off - 0xFFB000u;
        if (s != NULL && off >= 0xFFB000u && off < 0xFFB040u) {
            u32 len = 0;
            while (s[len]) len++;
            for (i = 0; i < 4; i++)
                if (base + i < len && base + i < 63) b[i] = (u8) s[base + i];
        }
        PORT_MEMCPY(data, b, 4);
    }
    return 0;
}

u32 osPiGetStatus(void) {
    return 0;
}

/* ---- SP / DP --------------------------------------------------------------- */

static OSTask *g_sp_task;
static u8 g_sp_yield;

void osSpTaskLoad(OSTask *t) {
    g_sp_task = t;
}

#define UCODE_CULL 0x802E77B0u  /* gCullUcodeText: the RDP-output-to-DRAM F3D (5FD50) */

/* cull_rsp_test's visibility test on the CPU.  The task's list (00000.c
 * cull_start) loads a projection and a modelview matrix, 8 box corners
 * and 12 triangles; the RSP writes RDP commands to output_buff, and the game
 * treats a lone sync (0xE8000000) as "not visible".  Model, checked against
 * the real ucode on an LLE RSP (mupen64plus + cxd4 + angrylion, 3921 tests
 * over the attract demos, all equal): a triangle produces output iff, in clip
 * space (v * MV * P), it is not entirely outside one of the planes
 * x = -w, x = w, y = -w, y = w, z = -w (near) and clipping it against those
 * five planes leaves a polygon.  The list's addresses are RDRAM physical,
 * except the corners, which live next to the list on the game thread's
 * (host) stack. */
static double cull_mtx(const u32 *w, int i) {
    u32 ip = (w[i / 2] >> ((i & 1) ? 0 : 16)) & 0xFFFF, fp = (w[8 + i / 2] >> ((i & 1) ? 0 : 16)) & 0xFFFF;
    return (double) (s32) ((ip << 16) | fp) / 65536.0;
}

static int cull_clip(double (*p)[4], int n, int plane, double (*out)[4]) {
    int i, m = 0;
    for (i = 0; i < n; i++) {
        double *a = p[i], *b = p[(i + 1) % n];
        double da, db;
        switch (plane) {
            case 0: da = a[3] + a[2], db = b[3] + b[2]; break;   /* near */
            case 1: da = a[3] - a[0], db = b[3] - b[0]; break;
            case 2: da = a[3] + a[0], db = b[3] + b[0]; break;
            case 3: da = a[3] - a[1], db = b[3] - b[1]; break;
            default: da = a[3] + a[1], db = b[3] + b[1]; break;
        }
        if (da >= 0) {
            int k;
            for (k = 0; k < 4; k++) out[m][k] = a[k];
            m++;
        }
        if ((da >= 0) != (db >= 0)) {
            double t = da / (da - db);
            int k;
            for (k = 0; k < 4; k++) out[m][k] = a[k] + (b[k] - a[k]) * t;
            m++;
        }
    }
    return m;
}

/* bc.exe's widescreen (an option, plat_host.h): the renderer shows k times
 * the 4:3 view's width, so the test accepts |x| <= k w (x / k against the
 * original planes).  1: the exact model above. */
double host_cull_wide = 1.0;

static int cull_visible(const u32 *dl, u32 size) {
    double mv[4][4], pr[4][4], c[16][4];
    u32 seg[16] = {0};
    int have = 0, i, j, k;
    const u32 host = (u32) (unsigned long) dl;
    PORT_MEMSET(c, 0, sizeof c);
    for (i = 0; i + 1 < (int) (size / 4); i += 2) {
        u32 w0 = dl[i], w1 = dl[i + 1];
        u32 a = seg[(w1 >> 24) & 15] + (w1 & 0xFFFFFF);
        const u8 *p;
        if (w1 - (host & 0x1FFFFFFF) + 0x10000u < 0x20000u)   /* PHYS() of the host stack */
            p = (const u8 *) (unsigned long) (w1 | (host & 0xE0000000u));
        else
            p = (const u8 *) (unsigned long) (0x80000000u | (a & 0x7FFFFF));
        switch (w0 >> 24) {
            case 0x01: {   /* G_MTX (load only in this list) */
                double (*m)[4] = (w0 & 0x10000) ? pr : mv;
                for (j = 0; j < 16; j++) m[j / 4][j % 4] = cull_mtx((const u32 *) p, j);
                have |= (w0 & 0x10000) ? 1 : 2;
                break;
            }
            case 0x04: {   /* G_VTX */
                int n = ((w0 >> 20) & 15) + 1, v0 = (w0 >> 16) & 15;
                double mvp[4][4];
                if (have != 3) return 1;
                for (j = 0; j < 4; j++)
                    for (k = 0; k < 4; k++)
                        mvp[j][k] = mv[j][0] * pr[0][k] + mv[j][1] * pr[1][k] + mv[j][2] * pr[2][k] + mv[j][3] * pr[3][k];
                for (j = 0; j < n && v0 + j < 16; j++) {
                    const Vtx *v = (const Vtx *) p + j;
                    double x = v->v.ob[0], y = v->v.ob[1], z = v->v.ob[2];
                    for (k = 0; k < 4; k++) c[v0 + j][k] = x * mvp[0][k] + y * mvp[1][k] + z * mvp[2][k] + mvp[3][k];
                    if (host_cull_wide != 1.0) c[v0 + j][0] /= host_cull_wide;
                }
                break;
            }
            case 0xBF: {   /* G_TRI1 */
                double poly[2][16][4];
                int n = 3, cur = 0, pl, codes[3];
                for (j = 0; j < 3; j++) {
                    const double *q = c[((w1 >> (16 - 8 * j)) & 0xFF) / 10 & 15];
                    codes[j] = (q[0] < -q[3]) | (q[0] > q[3]) << 1 | (q[1] < -q[3]) << 2 | (q[1] > q[3]) << 3 |
                               (q[2] < -q[3]) << 4;
                    for (k = 0; k < 4; k++) poly[0][j][k] = q[k];
                }
                if (codes[0] & codes[1] & codes[2]) break;
                for (pl = 0; pl < 5 && n > 0; pl++, cur ^= 1) n = cull_clip(poly[cur], n, pl, poly[cur ^ 1]);
                if (n >= 3) return 1;
                break;
            }
            case 0xBC:     /* G_MOVEWORD G_MW_SEGMENT */
                if ((w0 & 0xFF) == 6) seg[((w0 >> 8) & 0xFFFF) / 4 & 15] = w1 & 0xFFFFFF;
                break;
            case 0xB8:     /* G_ENDDL */
                return 0;
            default:       /* G_DL into the static segment (viewport, modes), syncs */
                break;
        }
    }
    return 0;
}

static void complete(u32 delay, int dp) {
    if (delay == 0) {
        plat_post_event(OS_EVENT_SP);
        if (dp) plat_post_event(OS_EVENT_DP);
    } else {
        plat_event_add(plat_now + delay, PEV_SP_DONE, NULL);
        if (dp) plat_event_add(plat_now + delay, PEV_DP_DONE, NULL);
    }
}

/* The RCP's work happens at once; the game hears about it in virtual time:
 *  - audio (M_AUDTASK): the command list runs on the microcode interpreter
 *    (port/src/audio/), which writes the samples into RDRAM; SP done.
 *  - graphics: dropped and counted.  Tasks are submitted through the game's
 *    scheduler (2C560.c), which keeps the OSTask at +0x10 of its own task
 *    record (BcScTask, flags at +0x08).  Flag 0x40 marks the frame's last
 *    task, the one that ends in a full sync: it gets SP and DP done, the
 *    others (shadow passes, the cull test) SP done only, like the hardware.
 *  - cull_rsp_test's visibility test (ucode gCullUcodeText, which writes RDP
 *    commands to output_buff for the CPU to read): answered by cull_visible
 *    (a CPU model of that ucode) through the first output word (a lone
 *    sync, 0xE8000000, means not visible), or with --clock by the
 *    emulator's word. */
void osSpTaskStartGo(OSTask *t) {
    g_sp_yield = 0;
    if (t->t.type == M_AUDTASK) {
        static int log = -1;
        double t0;
        plat_stats.aud_tasks++;
        /* BC_AUDIO_LOG=1: when each audio task starts (compare.py's emulator
         * log has the same per audio frame: its Z lines) */
        if (log < 0) log = host_env("BC_AUDIO_LOG");
        if (log)
            host_log("audtask %u retrace %u +%u frame %u\n", (unsigned) plat_stats.aud_tasks, (unsigned) plat_vi_count,
                     (unsigned) (plat_now % PLAT_VI_PERIOD), (unsigned) plat_stats.frames);
        t0 = host_ms();   /* (host_prof) */
        port_audio_task(osVirtualToPhysical(t->t.ucode_data), t->t.ucode_data_size,
                        osVirtualToPhysical(t->t.data_ptr), t->t.data_size);
        host_prof.audio_ms += host_ms() - t0;
        host_prof.audio_n++;
        complete(plat_cfg.aud_cycles, 0);
        return;
    }
    plat_stats.gfx_tasks++;
    {
        const double t0 = host_ms();   /* (host_prof: the units walk and bc.exe's renderer) */
        /* the units the lists hold, for the stale-byte emulation (swap.c) */
        if ((u32) t->t.ucode != UCODE_CULL) port_mark_gfx_task(osVirtualToPhysical(t->t.data_ptr));
        /* bc.exe: the renderer draws the task now; its completion still comes
         * at the modelled virtual time below, as in bc_headless */
        if (plat_cfg.live != NULL && (u32) t->t.ucode != UCODE_CULL)
            plat_cfg.live->gfx_task(osVirtualToPhysical(t->t.ucode), osVirtualToPhysical(t->t.ucode_data),
                                    osVirtualToPhysical(t->t.data_ptr), t->t.data_size);
        host_prof.gfx_ms += host_ms() - t0;
        host_prof.gfx_n++;
    }
    if ((u32) t->t.ucode == UCODE_CULL) {
        u64 v, m;
        /* the list on the host stack (port_cull_data; the task word holds its N64 address) */
        const u32 *dl = g_cull_dl != NULL ? g_cull_dl : (const u32 *) t->t.data_ptr;
        g_cull_dl = NULL;
        plat_stats.cull_tasks++;
        m = cull_visible(dl, t->t.data_size) ? 0xCC000000u : 0xE8000000u;
        cull_log(dl, t->t.data_size, (u32) m);
        /* following an emulator (--clock): its answer for this test (an LLE
         * RSP's with compare.py emu --lle; mupen64plus's HLE RSP doesn't run
         * this ucode and always says "visible"), checked against the model;
         * else the CPU model of the real ucode (cull_visible) */
        if (plat_clock_key_take_name(3, "cull_rsp_test", &v)) {
            plat_stats.cull_keyed++;
            if (((u32) v == 0xE8000000u) != ((u32) m == 0xE8000000u)) {
                plat_stats.cull_disagree++;
                if (host_verbose)
                    host_log("cull: frame %u: emulator %08x, model %08x\n", (unsigned) plat_stats.frames,
                             (unsigned) v, (unsigned) m);
            }
        } else {
            v = m;
        }
        if ((u32) v == 0xE8000000u) plat_stats.cull_hidden++;
        if (t->t.output_buff != NULL) *(u32 *) t->t.output_buff = (u32) v;
        complete(plat_cfg.small_gfx_cycles, 0);
        return;
    }
    if (*(u32 *) ((u8 *) t - 0x10 + 0x08) & 0x40) {
        static u32 started;
        u64 when = plat_frame_done_time(++started), sp = plat_frame_sp_time(started);
        if (when != ~0ull && sp != ~0ull && sp < when) {
            /* the RSP part ends first: the RSP is free for other tasks */
            plat_event_add(sp > plat_now ? sp : plat_now, PEV_SP_DONE, NULL);
            plat_event_add(when > plat_now ? when : plat_now, PEV_DP_DONE, NULL);
            return;
        }
        complete(when == ~0ull ? plat_cfg.gfx_cycles : (when > plat_now ? (u32) (when - plat_now) : 0), 1);
    } else {
        complete(plat_cfg.small_gfx_cycles, 0);
    }
}

void osSpTaskYield(void) {
    g_sp_yield = 1;
}

OSYieldResult osSpTaskYielded(OSTask *t) {
    (void) t;
    /* tasks never get the chance to yield: they finish first */
    return 0;
}

u32 __osSpGetStatus(void) {
    return 1; /* SP_STATUS_HALT */
}
void __osSpSetStatus(u32 v) {
    (void) v;
}
s32 __osSpSetPc(u32 pc) {
    (void) pc;
    return 0;
}

/* osDpSetStatus / osDpGetStatus (8FD90.c / 96300.c write the DPC register) */
void osDpSetStatus(u32 v) {
    (void) v;
}
u32 osDpGetStatus(void) {
    return 0;
}

/* ---- AI -------------------------------------------------------------------- */
/* A two-entry DMA FIFO playing at the programmed rate, timed by the virtual
 * clock.  The audio manager sizes each frame from osAiGetLength, and the
 * synthesizer's sequencer state (which the game polls) follows the sample
 * counts.  Each accepted buffer also goes to the output (port_audio.c: the
 * WAV file, bc.exe's speakers), which never feeds back into this timing. */

static u32 g_ai_dacrate;      /* VI clocks per sample */
static u64 g_ai_start;        /* when the playing buffer started */
static u32 g_ai_len[2];       /* playing, queued (bytes; 0 = none) */

static u64 ai_cycles(u32 bytes) {
    /* bytes/4 samples, each dacrate VI-clock ticks, in CPU counts */
    u64 vi_ticks = (u64) (bytes / 4) * g_ai_dacrate;
    return vi_ticks * PLAT_COUNT_HZ / (u32) osViClock;
}

static void ai_update(void) {
    while (g_ai_len[0] != 0 && g_ai_dacrate != 0) {
        u64 end = g_ai_start + ai_cycles(g_ai_len[0]);
        if (plat_now < end) break;
        g_ai_len[0] = g_ai_len[1];
        g_ai_len[1] = 0;
        g_ai_start = end;
    }
}

s32 osAiSetFrequency(u32 frequency) {
    unsigned int dacRate;
    unsigned char bitRate;
    float f;

    f = (s32) osViClock / (float) frequency + .5f;
    dacRate = f;
    if (dacRate < 132) return -1;
    bitRate = dacRate / 66;
    if (bitRate > 16) bitRate = 16;
    (void) bitRate;
    g_ai_dacrate = dacRate;
    return (s32) osViClock / (s32) dacRate;
}

s32 osAiSetNextBuffer(void *buf, u32 size) {
    /* AI_LEN holds 18 bits, 8-byte units (the audio manager's first frame
     * can ask for a "negative" size when osAiGetLength reads junk, as it
     * does in mupen64plus before the first DMA; the hardware plays the low
     * bits) */
    const double t0 = host_ms();   /* (host_prof: the output, bc.exe's sound device) */
    s32 r = 0;
    size &= 0x3FFF8;
    ai_update();
    if (g_ai_len[0] == 0) {
        g_ai_len[0] = size;
        g_ai_start = plat_now;
    } else if (g_ai_len[1] == 0) {
        g_ai_len[1] = size;
    } else {
        r = -1;
    }
    port_audio_ai_buffer(osVirtualToPhysical(buf), size, g_ai_dacrate, (u32) osViClock, r == 0);
    host_prof.audio_ms += host_ms() - t0;
    return r;
}

u32 osAiGetLength(void) {
    u64 done;
    ai_update();
    /* the emulator's value for this caller in this retrace (--clock, compare.py) */
    if (plat_clock_key_take(2, PORT_RETADDR(), &done)) return (u32) done;
    if (g_ai_len[0] == 0 || g_ai_dacrate == 0) return 0;
    done = (plat_now - g_ai_start) * (u32) osViClock / PLAT_COUNT_HZ / g_ai_dacrate * 4;
    return done >= g_ai_len[0] ? 0 : g_ai_len[0] - (u32) done;
}

u32 osAiGetStatus(void) {
    return g_ai_len[1] != 0 ? 0x80000000u : 0;
}
