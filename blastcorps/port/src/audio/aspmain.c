/* Blast Corps audio microcode (aspMain, BC/DKR variant) command-list
 * interpreter.  The behaviour of every command, including its rounding,
 * saturation, block sizes and its use of DMEM, was established by reading
 * the microcode's instruction stream and by running the real microcode on an
 * LLE RSP next to this file (port/tools/audio/, README "Audio").  Nothing
 * from the ROM is embedded: the constant tables (resampler filter, masks,
 * ramps) are read from the task's ucode_data into DMEM at run time, as the
 * RSP's boot code does.
 *
 * The commands are written as the vector-unit operations the microcode
 * performs (a small model of the RSP vector unit below), because the exact
 * results depend on the accumulator's width, on where it saturates and on
 * the vector flags.  The model follows the publicly documented RSP vector
 * instruction semantics.
 *
 * DMEM map used by this microcode:
 *   0x000-0x2CF  ucode data: constants, jump table, masks, ramps, resampler
 *                filter table (64 phases x 4 taps at 0x0D0)
 *   0x320        segment table (16 words; only segment 0 is cleared per task)
 *   0x360        parameters set by A_SETBUFF / A_SETVOL / A_SETLOOP
 *   0x380        command buffer (0x140 bytes per DMA)
 *   0x4C0        A_LOADADPCM table
 *   0x5C0        sample buffers: every DMEM offset in a command is relative to this
 *   0xF90        command state scratch (resampler, envelope mixer, pole filter) */
#include <string.h>
#include "aspmain.h"

#define P_IN      0x360
#define P_OUT     0x362
#define P_COUNT   0x364
#define P_VOL_L   0x366
#define P_VOL_R   0x368
#define P_DRY_R   0x36A   /* A_AUX buffers: dry right, wet left, wet right */
#define P_WET_L   0x36C
#define P_WET_R   0x36E
#define P_LOOP    0x370   /* A_SETLOOP's address (32-bit) overlays the left target and rate high */
#define P_RAMP_L  0x370   /* target, rate high, rate low */
#define P_RAMP_R  0x376
#define P_DRY     0x37C
#define P_WET     0x37E
#define SEGTAB    0x320
#define CMDBUF    0x380
#define TABLE     0x4C0
#define BUFBASE   0x5C0
#define SCRATCH   0xF90

/* ------------------------------------------------------------ DMEM access */

static inline uint32_t lbu(const AspState *s, uint32_t a) { return s->dmem[a & 0xFFF]; }
static inline uint32_t lhu(const AspState *s, uint32_t a) { return (lbu(s, a) << 8) | lbu(s, a + 1); }
static inline int32_t lh(const AspState *s, uint32_t a) { return (int16_t) lhu(s, a); }
static inline uint32_t lw(const AspState *s, uint32_t a) { return (lhu(s, a) << 16) | lhu(s, a + 2); }
static inline void sb(AspState *s, uint32_t a, uint32_t v) { s->dmem[a & 0xFFF] = (uint8_t) v; }
static inline void sh(AspState *s, uint32_t a, uint32_t v) { sb(s, a, v >> 8); sb(s, a + 1, v); }
static inline void sw(AspState *s, uint32_t a, uint32_t v) { sh(s, a, v >> 16); sh(s, a + 2, v); }

/* the microcode's address translation: segment table entry + low 24 bits */
static uint32_t seg_addr(const AspState *s, uint32_t w1) {
    return (w1 & 0xFFFFFF) + lw(s, SEGTAB + ((w1 >> 24) << 2));
}

/* ------------------------------------------------------------------- DMA */
/* SP DMA as the RSP performs it: DMEM and RDRAM addresses 8-aligned, the
 * length (len-1 in the register) rounded up to 8, optional rows/skip. */

static void dma_check(AspState *s, uint32_t mem, uint32_t lenm1) {
    if ((mem & 0x1000) && s->imem_dma++ == 0 && s->log) s->log("asp: DMA to IMEM (mem 0x%x)\n", mem);
    if ((lenm1 >> 12) != 0 && s->odd_dma++ == 0 && s->log) s->log("asp: DMA with rows (len-1 0x%x)\n", lenm1);
}

static void dma_read(AspState *s, const AspBus *b, uint32_t mem, uint32_t dram, uint32_t lenm1, int kind) {
    uint32_t len = ((lenm1 & 0xFFF) | 7) + 1, rows = ((lenm1 >> 12) & 0xFF) + 1, skip = lenm1 >> 20;
    uint8_t buf[0x1000];
    uint32_t r, i;
    dma_check(s, mem, lenm1);
    mem &= 0x1FF8;
    dram &= 0xFFFFF8;
    for (r = 0; r < rows; r++) {
        b->read(b->ctx, dram, buf, len, kind);
        if (!(mem & 0x1000))
            for (i = 0; i < len; i++) s->dmem[(mem + i) & 0xFFF] = buf[i];
        mem = (mem & 0x1000) | ((mem + len) & 0xFFF);
        dram = (dram + len + skip) & 0xFFFFF8;
    }
    s->pending_dma = 1;
}

static void dma_write(AspState *s, const AspBus *b, uint32_t mem, uint32_t dram, uint32_t lenm1) {
    uint32_t len = ((lenm1 & 0xFFF) | 7) + 1, rows = ((lenm1 >> 12) & 0xFF) + 1, skip = lenm1 >> 20;
    uint8_t buf[0x1000];
    uint32_t r, i;
    dma_check(s, mem, lenm1);
    mem &= 0x1FF8;
    dram &= 0xFFFFF8;
    for (r = 0; r < rows; r++) {
        if (mem & 0x1000) memset(buf, 0, len);   /* IMEM: not modelled */
        else
            for (i = 0; i < len; i++) buf[i] = s->dmem[(mem + i) & 0xFFF];
        b->write(b->ctx, dram, buf, len, ASP_DMA_DATA);
        mem = (mem & 0x1000) | ((mem + len) & 0xFFF);
        dram = (dram + len + skip) & 0xFFFFF8;
    }
    s->pending_dma = 1;
}

/* --------------------------------------------------------- vector unit */
/* Registers hold 8 lanes of 16 bits; register byte b is lane b/2, high byte
 * first (big-endian, as the loads and stores see them). */

typedef int16_t Vec[8];

static inline uint8_t vbyte(const int16_t *v, int b) {
    uint16_t x = (uint16_t) v[(b >> 1) & 7];
    return (b & 1) ? (uint8_t) x : (uint8_t) (x >> 8);
}
static inline void vsetbyte(int16_t *v, int b, uint8_t x) {
    uint16_t o = (uint16_t) v[(b >> 1) & 7];
    o = (b & 1) ? (uint16_t) ((o & 0xFF00) | x) : (uint16_t) ((o & 0x00FF) | (x << 8));
    v[(b >> 1) & 7] = (int16_t) o;
}

/* element selector e (0..15) applied to lane i */
static inline int esel(int e, int i) {
    if (e < 2) return i;
    if (e < 4) return (i & ~1) | (e & 1);
    if (e < 8) return (i & ~3) | (e & 3);
    return e & 7;
}

static inline int64_t acc48(int64_t a) { return (int64_t) ((uint64_t) a << 16) >> 16; }
static inline uint16_t acc_h(int64_t a) { return (uint16_t) (a >> 32); }
static inline uint16_t acc_m(int64_t a) { return (uint16_t) (a >> 16); }
static inline uint16_t acc_l(int64_t a) { return (uint16_t) a; }

/* the accumulator read-out of the multiply instructions: the middle (slice 1)
 * or low (slice 0) 16 bits, clamped when the accumulator doesn't fit */
static inline uint16_t acc_sat(int64_t a, int slice, uint16_t neg, uint16_t pos) {
    int16_t h = (int16_t) acc_h(a);
    int16_t m = (int16_t) acc_m(a);
    if (h < 0) {
        if ((uint16_t) h != 0xFFFF || m >= 0) return neg;
    } else {
        if (h != 0 || m < 0) return pos;
    }
    return slice ? acc_m(a) : acc_l(a);
}

static inline int16_t clamp16(int32_t x) { return x > 32767 ? 32767 : x < -32768 ? -32768 : (int16_t) x; }

enum { MULF, MACF, MUDL, MADL, MUDM, MADM, MUDN, MADN, MUDH, MADH };

static void vmul(AspVU *u, int op, int vd, int vs, int vt, int e) {
    Vec t, r;
    int i;
    for (i = 0; i < 8; i++) t[i] = u->v[vt][esel(e, i)];
    for (i = 0; i < 8; i++) {
        int32_t s = u->v[vs][i], x = t[i];
        int64_t a = u->acc[i];
        uint16_t res;
        switch (op) {
            case MULF: a = (int64_t) s * x * 2 + 0x8000; break;
            case MACF: a += (int64_t) s * x * 2; break;
            case MUDL: a = (int64_t) (((uint32_t) (uint16_t) s * (uint32_t) (uint16_t) x) >> 16); break;
            case MADL: a += (int64_t) (((uint32_t) (uint16_t) s * (uint32_t) (uint16_t) x) >> 16); break;
            case MUDM: a = (int64_t) s * (uint16_t) x; break;
            case MADM: a += (int64_t) s * (uint16_t) x; break;
            case MUDN: a = (int64_t) (uint16_t) s * x; break;
            case MADN: a += (int64_t) (uint16_t) s * x; break;
            case MUDH: a = (int64_t) (s * x) * 65536; break;
            default: /* MADH */ a += (int64_t) (s * x) * 65536; break;
        }
        a = acc48(a);
        u->acc[i] = a;
        switch (op) {
            case MUDL: case MADL: case MUDN: case MADN: res = acc_sat(a, 0, 0x0000, 0xFFFF); break;
            default: res = acc_sat(a, 1, 0x8000, 0x7FFF); break;
        }
        r[i] = (int16_t) res;
    }
    memcpy(u->v[vd], r, sizeof r);
}

#define VMULF(d, s, t, e) vmul(u, MULF, d, s, t, e)
#define VMACF(d, s, t, e) vmul(u, MACF, d, s, t, e)
#define VMUDL(d, s, t, e) vmul(u, MUDL, d, s, t, e)
#define VMUDM(d, s, t, e) vmul(u, MUDM, d, s, t, e)
#define VMADM(d, s, t, e) vmul(u, MADM, d, s, t, e)
#define VMUDN(d, s, t, e) vmul(u, MUDN, d, s, t, e)
#define VMADN(d, s, t, e) vmul(u, MADN, d, s, t, e)
#define VMUDH(d, s, t, e) vmul(u, MUDH, d, s, t, e)
#define VMADH(d, s, t, e) vmul(u, MADH, d, s, t, e)

/* VSAR: read out one slice of the accumulator (e 8 high, 9 middle, 10 low) */
static void VSAR(AspVU *u, int vd, int e) {
    int i;
    for (i = 0; i < 8; i++)
        u->v[vd][i] = (int16_t) (e == 8 ? acc_h(u->acc[i]) : e == 9 ? acc_m(u->acc[i]) : e == 10 ? acc_l(u->acc[i]) : 0);
}

static inline void set_accl(AspVU *u, int i, uint16_t x) { u->acc[i] = (u->acc[i] & ~(int64_t) 0xFFFF) | x; }

/* VADD / VSUB: saturating, with the carry/borrow from VCO, which they clear */
static void vaddsub(AspVU *u, int sub, int vd, int vs, int vt, int e) {
    Vec t, r;
    int i;
    for (i = 0; i < 8; i++) t[i] = u->v[vt][esel(e, i)];
    for (i = 0; i < 8; i++) {
        int32_t c = (u->vcol >> i) & 1;
        int32_t x = sub ? u->v[vs][i] - t[i] - c : u->v[vs][i] + t[i] + c;
        set_accl(u, i, (uint16_t) x);
        r[i] = clamp16(x);
    }
    u->vcol = u->vcoh = 0;
    memcpy(u->v[vd], r, sizeof r);
}

/* VADDC: unsigned add, carry out to VCO */
static void VADDC(AspVU *u, int vd, int vs, int vt, int e) {
    Vec t, r;
    int i;
    uint16_t co = 0;
    for (i = 0; i < 8; i++) t[i] = u->v[vt][esel(e, i)];
    for (i = 0; i < 8; i++) {
        uint32_t x = (uint32_t) (uint16_t) u->v[vs][i] + (uint16_t) t[i];
        set_accl(u, i, (uint16_t) x);
        r[i] = (int16_t) (uint16_t) x;
        if (x >> 16) co |= 1 << i;
    }
    u->vcol = co;
    u->vcoh = 0;
    memcpy(u->v[vd], r, sizeof r);
}

static void vlogic(AspVU *u, int xor_, int vd, int vs, int vt, int e) {
    Vec t, r;
    int i;
    for (i = 0; i < 8; i++) t[i] = u->v[vt][esel(e, i)];
    for (i = 0; i < 8; i++) {
        r[i] = xor_ ? (int16_t) (u->v[vs][i] ^ t[i]) : (int16_t) (u->v[vs][i] & t[i]);
        set_accl(u, i, (uint16_t) r[i]);
    }
    memcpy(u->v[vd], r, sizeof r);
}
#define VAND(d, s, t, e) vlogic(u, 0, d, s, t, e)
#define VXOR(d, s, t, e) vlogic(u, 1, d, s, t, e)
#define VZERO(d) vlogic(u, 1, d, d, d, 0)

/* VGE: signed max, ties broken by VCO */
static void VGE(AspVU *u, int vd, int vs, int vt, int e) {
    Vec t, r;
    int i;
    uint16_t cc = 0;
    for (i = 0; i < 8; i++) t[i] = u->v[vt][esel(e, i)];
    for (i = 0; i < 8; i++) {
        int16_t a = u->v[vs][i], b = t[i];
        int ne = ((u->vcol >> i) & 1) && ((u->vcoh >> i) & 1);
        int ge = a > b || (a == b && !ne);
        if (ge) cc |= 1 << i;
        r[i] = ge ? a : b;
        set_accl(u, i, (uint16_t) r[i]);
    }
    u->vccl = cc;
    u->vcch = 0;
    u->vcol = u->vcoh = 0;
    memcpy(u->v[vd], r, sizeof r);
}

/* VCL: clip test low (with VCO clear: unsigned min) */
static void VCL(AspVU *u, int vd, int vs, int vt, int e) {
    Vec t, r;
    int i;
    for (i = 0; i < 8; i++) t[i] = u->v[vt][esel(e, i)];
    for (i = 0; i < 8; i++) {
        uint16_t a = (uint16_t) u->v[vs][i], b = (uint16_t) t[i], res;
        int col = (u->vcol >> i) & 1, coh = (u->vcoh >> i) & 1;
        if (col) {
            if (coh) {
                res = ((u->vccl >> i) & 1) ? (uint16_t) -b : a;
            } else {
                uint16_t sum = (uint16_t) (a + b);
                int carry = (uint32_t) a + b > 0xFFFF;
                int le = ((u->vce >> i) & 1) ? (!sum || !carry) : (!sum && !carry);
                u->vccl = (uint16_t) ((u->vccl & ~(1 << i)) | (le << i));
                res = le ? (uint16_t) -b : a;
            }
        } else {
            if (coh) {
                res = ((u->vcch >> i) & 1) ? b : a;
            } else {
                int ge = (int32_t) a - (int32_t) b >= 0;
                u->vcch = (uint16_t) ((u->vcch & ~(1 << i)) | (ge << i));
                res = ge ? b : a;
            }
        }
        set_accl(u, i, res);
        r[i] = (int16_t) res;
    }
    u->vcol = u->vcoh = 0;
    u->vce = 0;
    memcpy(u->v[vd], r, sizeof r);
}

/* loads and stores (element = starting register byte) */
static void LQV(AspState *s, int vt, int e, uint32_t a) {
    int end = 16 + e - (int) (a & 15), b;
    if (end > 16) end = 16;
    for (b = e; b < end; b++) vsetbyte(s->vu.v[vt], b, (uint8_t) lbu(s, a++));
}
static void LRV(AspState *s, int vt, int e, uint32_t a) {
    int b = 16 - ((int) (a & 15) - e);
    a &= ~15u;
    for (; b < 16; b++) vsetbyte(s->vu.v[vt], b & 15, (uint8_t) lbu(s, a++));
}
static void LNV(AspState *s, int vt, int e, uint32_t a, int n) {   /* lsv/llv/ldv */
    int b;
    for (b = e; b < e + n; b++) vsetbyte(s->vu.v[vt], b & 15, (uint8_t) lbu(s, a++));
}
#define LSV(vt, e, a) LNV(s, vt, e, a, 2)
#define LDV(vt, e, a) LNV(s, vt, e, a, 8)
static void SNV(AspState *s, int vt, int e, uint32_t a, int n) {   /* ssv/slv/sdv */
    int b;
    for (b = e; b < e + n; b++) sb(s, a++, vbyte(s->vu.v[vt], b & 15));
}
#define SSV(vt, e, a) SNV(s, vt, e, a, 2)
#define SLV(vt, e, a) SNV(s, vt, e, a, 4)
#define SDV(vt, e, a) SNV(s, vt, e, a, 8)
static void SQV(AspState *s, int vt, int e, uint32_t a) {
    int end = e + (16 - (int) (a & 15)), b;
    for (b = e; b < end; b++) sb(s, a++, vbyte(s->vu.v[vt], b & 15));
}
static void MTC2(AspState *s, int vd, int e, uint32_t x) {
    vsetbyte(s->vu.v[vd], e, (uint8_t) (x >> 8));
    if (e != 15) vsetbyte(s->vu.v[vd], e + 1, (uint8_t) x);
}
static int32_t MFC2(AspState *s, int vs, int e) {
    return (int16_t) ((vbyte(s->vu.v[vs], e) << 8) | vbyte(s->vu.v[vs], (e + 1) & 15));
}

/* ------------------------------------------------------------- commands */

/* A_ADPCM: decode 9-byte frames (header: scale shift << 4 | predictor; 16
 * 4-bit residuals) into 16-sample blocks.  The output buffer first receives
 * the 16-sample state (zeros with A_INIT, else from RDRAM: the loop state
 * with A_LOOP), decoding follows it; the last 16 samples are saved back.
 * Each sample: residual << shift (shift >= 12: << 12), then
 *   out[i] = sat16((book0[i]*x[-2] + book1[i]*x[-1] + sum_{j<i} book1[i-1-j]*in[j]
 *                   + in[i]*2048) (low 32 bits) >> 11)
 * per 8-sample half, x[-1] and x[-2] being the previous half's last outputs. */
static void cmd_adpcm(AspState *s, const AspBus *bus, uint32_t w0, uint32_t w1) {
    AspVU *u = &s->vu;
    uint32_t s5, s4, s3, s2, s1, at, t3, t5, t4;
    int32_t t6, v0r;
    uint32_t flags = (w0 >> 16) & 0xFF;

    LQV(s, 31, 0, 0);
    VZERO(27);
    s5 = lhu(s, P_IN);
    VZERO(25);
    VZERO(24);
    s4 = s5 + 1;
    s3 = lhu(s, P_OUT);
    VZERO(13); VZERO(14);
    s2 = lhu(s, P_COUNT);
    VZERO(15); VZERO(16); VZERO(17); VZERO(18); VZERO(19);
    s1 = seg_addr(s, w1);
    SQV(s, 27, 0, s3);
    SQV(s, 27, 0, s3 + 16);
    if (!(flags & 1)) {
        uint32_t src = (flags & 2) ? lw(s, P_LOOP) : s1;
        dma_read(s, bus, s3, src, 31, (flags & 2) ? ASP_DMA_LOOP_STATE : ASP_DMA_DATA);
    }
    LDV(25, 0, 0x30);
    LDV(24, 8, 0x30);
    LDV(23, 0, 0x38);
    LDV(23, 8, 0x38);
    LQV(s, 27, 0, s3 + 16);
    s3 += 32;
    if (s2 != 0) {
        LDV(1, 0, s4);
        at = lbu(s, s5);
        t3 = (at & 0xF) << 5;
        VAND(3, 25, 1, 8);
        t5 = t3 + TABLE;
        VAND(4, 24, 1, 9);
        t6 = (int32_t) (at >> 4);
        VAND(5, 25, 1, 10);
        VAND(6, 24, 1, 11);
        t6 = 12 - t6;
        v0r = t6 - 1;
        MTC2(s, 22, 0, 0x8000u >> (v0r & 31));
        LQV(s, 21, 0, t5);
        LQV(s, 20, 0, t5 + 16);
        LRV(s, 19, 0, t5 + 30);
        LRV(s, 18, 0, t5 + 28);
        LRV(s, 17, 0, t5 + 26);
        LRV(s, 16, 0, t5 + 24);
        LRV(s, 15, 0, t5 + 22);
        LRV(s, 14, 0, t5 + 20);
        LRV(s, 13, 0, t5 + 18);
        for (;;) {
            int32_t cur_t6 = t6;
            s4 += 9;
            VMUDN(30, 3, 23, 0);
            s5 += 9;
            VMADN(30, 4, 23, 0);
            LDV(1, 0, s4);
            VMUDN(29, 5, 23, 0);
            at = lbu(s, s5);
            VMADN(29, 6, 23, 0);
            t3 = at & 0xF;
            if (cur_t6 > 0) {
                VMUDM(30, 30, 22, 8);
                VMUDM(29, 29, 22, 8);
            }
            t3 <<= 5;
            VAND(3, 25, 1, 8);
            t5 = t3 + TABLE;
            VAND(4, 24, 1, 9);
            VAND(5, 25, 1, 10);
            VAND(6, 24, 1, 11);
            t6 = (int32_t) (at >> 4);
            VMUDH(2, 21, 27, 14);
            VMADH(2, 20, 27, 15);
            t6 = 12 - t6;
            VMADH(2, 19, 30, 8);
            v0r = t6 - 1;
            VMADH(2, 18, 30, 9);
            VMADH(2, 17, 30, 10);
            VMADH(2, 16, 30, 11);
            VMADH(28, 15, 30, 12);
            MTC2(s, 22, 0, 0x8000u >> (v0r & 31));
            VMADH(2, 14, 30, 13);
            VMADH(2, 13, 30, 14);
            VMADH(2, 30, 31, 13);
            VSAR(u, 26, 9);
            VSAR(u, 28, 8);
            VMUDN(2, 26, 31, 12);
            VMADH(28, 28, 31, 12);
            VMUDH(2, 19, 29, 8);
            t4 = t5 - 2;
            VMADH(2, 18, 29, 9);
            LRV(s, 19, 0, t4 + 32);
            VMADH(2, 17, 29, 10);
            t4 -= 2;
            VMADH(2, 16, 29, 11);
            LRV(s, 18, 0, t4 + 32);
            VMADH(2, 15, 29, 12);
            t4 -= 2;
            VMADH(2, 14, 29, 13);
            LRV(s, 17, 0, t4 + 32);
            VMADH(2, 13, 29, 14);
            t4 -= 2;
            VMADH(2, 29, 31, 13);
            LRV(s, 16, 0, t4 + 32);
            VMADH(2, 21, 28, 14);
            t4 -= 2;
            VMADH(2, 20, 28, 15);
            LRV(s, 15, 0, t4 + 32);
            VSAR(u, 26, 9);
            t4 -= 2;
            VSAR(u, 27, 8);
            LRV(s, 14, 0, t4 + 32);
            t4 -= 2;
            LRV(s, 13, 0, t4 + 32);
            LQV(s, 21, 0, t5);
            VMUDN(2, 26, 31, 12);
            LQV(s, 20, 0, t5 + 16);
            VMADH(27, 27, 31, 12);
            s2 -= 32;
            SDV(28, 0, s3);
            SDV(28, 8, s3 + 8);
            SDV(27, 0, s3 + 16);
            SDV(27, 8, s3 + 24);
            s3 += 32;
            if ((int32_t) s2 <= 0) break;
        }
    }
    dma_write(s, bus, s3 - 32, s1, 31);
    s->pending_dma = 0;
}

/* A_POLEF: two-pole filter with the A_LOADADPCM table (row 0: y[-2]
 * coefficients, row 1: y[-1] coefficients, the latter also scaled in DMEM by
 * gain*4/65536 and used for the input taps), gain on the current input:
 *   out = sat16(sum >> 14); the state is the last 4 outputs. */
static void cmd_polef(AspState *s, const AspBus *bus, uint32_t w0, uint32_t w1) {
    AspVU *u = &s->vu;
    uint32_t s5, s4, s3, s2, t5, t6;
    uint32_t flags = (w0 >> 16) & 0xFF;

    LQV(s, 31, 0, 0);
    VZERO(28);
    s5 = lhu(s, P_IN);
    VZERO(17);
    s4 = lhu(s, P_OUT);
    VZERO(18);
    s3 = lhu(s, P_COUNT);
    VZERO(19);
    if (s3 == 0) return;
    t6 = w0 & 0xFFFF;
    MTC2(s, 31, 10, t6);
    t6 <<= 2;
    MTC2(s, 16, 0, t6);
    VZERO(20); VZERO(21); VZERO(22); VZERO(23);
    s2 = seg_addr(s, w1);
    SLV(28, 0, SCRATCH);
    if (!(flags & 1)) {
        dma_read(s, bus, SCRATCH, s2, 7, ASP_DMA_DATA);
    }
    s->pending_dma = 0;
    t5 = TABLE;
    MTC2(s, 14, 0, 4);
    LQV(s, 24, 0, t5 + 16);
    VMUDM(16, 24, 16, 8);
    LDV(28, 8, SCRATCH);
    SQV(s, 16, 0, t5 + 16);
    LQV(s, 25, 0, t5);
    LRV(s, 23, 0, t5 + 30);
    LRV(s, 22, 0, t5 + 28);
    LRV(s, 21, 0, t5 + 26);
    LRV(s, 20, 0, t5 + 24);
    LRV(s, 19, 0, t5 + 22);
    LRV(s, 18, 0, t5 + 20);
    LRV(s, 17, 0, t5 + 18);
    LDV(30, 0, s5);
    LDV(30, 8, s5 + 8);
    for (;;) {
        VMUDH(16, 25, 28, 14);
        s5 += 16;
        VMADH(16, 24, 28, 15);
        s3 -= 16;
        VMADH(16, 23, 30, 8);
        VMADH(16, 22, 30, 9);
        VMADH(16, 21, 30, 10);
        VMADH(16, 20, 30, 11);
        VMADH(28, 19, 30, 12);
        VMADH(16, 18, 30, 13);
        VMADH(16, 17, 30, 14);
        VMADH(16, 30, 31, 13);
        LDV(30, 0, s5);
        VSAR(u, 26, 9);
        LDV(30, 8, s5 + 8);
        VSAR(u, 28, 8);
        VMUDN(16, 26, 14, 8);
        VMADH(28, 28, 14, 8);
        SDV(28, 0, s4);
        SDV(28, 8, s4 + 8);
        s4 += 16;
        if ((int32_t) s3 <= 0) break;
    }
    dma_write(s, bus, s4 - 8, s2, 7);
}

/* A_RESAMPLE: 4-tap polyphase resampler.  Output n uses the 4 samples at
 * input position P_n = frac + 2*pitch*n (16.16, pitch = w0 low 16 bits as
 * UQ1.15), starting 4 samples before the input (the state's samples), and
 * filter phase (P_n & 0xFFFF) >> 10 of the table at DMEM 0x0D0:
 *   out = sat(sat(p0 + p1) + sat(p2 + p3)),  pk = sat16((x*c*2 + 0x8000) >> 16).
 * State (32 bytes): 4 samples, position fraction, and (BC variant) the
 * input's 16-byte tail with its alignment, restored before the input by
 * flag 2. */
static void cmd_resample(AspState *s, const AspBus *bus, uint32_t w0, uint32_t w1) {
    AspVU *u = &s->vu;
    int32_t t0, s3, s2, t3;
    uint32_t v0, a3, s5, s4;
    uint32_t r_s1, r_t1, r_t5, r_a1, r_s0, r_t0, r_t4, r_a0, r_t7, r_a3, r_t3, r_v1, r_t6, r_a2, r_t2, r_v0;

    t0 = lh(s, P_IN);
    s3 = lh(s, P_OUT);
    s2 = lh(s, P_COUNT);
    v0 = seg_addr(s, w1);
    sw(s, SCRATCH + 0x40, v0);
    a3 = w0 >> 16;
    if (!(a3 & 1)) {
        dma_read(s, bus, SCRATCH, v0, 31, ASP_DMA_DATA);
        s->pending_dma = 0;
    } else {
        s->pending_dma = 0;
        sh(s, SCRATCH + 8, 0);
        VZERO(16);
        SDV(16, 0, SCRATCH);
    }
    if (a3 & 2) {
        t3 = lh(s, SCRATCH + 10);
        LQV(s, 3, 0, SCRATCH + 16);
        SDV(3, 0, (uint32_t) (t0 - 16));
        SDV(3, 8, (uint32_t) (t0 - 8));
        t0 -= t3;
    }
    t0 -= 8;
    LSV(23, 14, SCRATCH + 8);
    LDV(16, 0, SCRATCH);
    SDV(16, 0, (uint32_t) t0);
    MTC2(s, 18, 4, (uint32_t) t0);
    MTC2(s, 18, 6, 0xD0);
    MTC2(s, 18, 8, w0);
    MTC2(s, 18, 10, 0x40);
    LQV(s, 31, 0, 0x60);
    LQV(s, 25, 0, 0x50);
    vaddsub(u, 1, 25, 25, 31, 0);
    LQV(s, 30, 0, 0x70);
    LQV(s, 29, 0, 0x80);
    LQV(s, 28, 0, 0x90);
    LQV(s, 27, 0, 0xA0);
    LQV(s, 26, 0, 0xB0);
    vaddsub(u, 1, 25, 25, 31, 0);
    LQV(s, 24, 0, 0xC0);
    s5 = SCRATCH + 0x20;
    s4 = SCRATCH + 0x30;
    VZERO(22);
    VMUDM(23, 31, 23, 15);
    VMADM(22, 25, 18, 12);
    VMADN(23, 31, 30, 8);
    VMUDN(21, 31, 18, 10);
    VMADN(21, 22, 30, 10);
    VMUDL(17, 23, 18, 13);
    VMUDN(17, 17, 30, 12);
    VMADN(17, 31, 18, 11);
    LQV(s, 25, 0, 0x50);
    SQV(s, 21, 0, s5);
    SQV(s, 17, 0, s4);
    SSV(23, 14, SCRATCH + 8);
#define LOADADDRS() \
    r_s1 = lh(s, s5), r_t1 = lh(s, s4), r_t5 = lh(s, s5 + 8), r_a1 = lh(s, s4 + 8), \
    r_s0 = lh(s, s5 + 2), r_t0 = lh(s, s4 + 2), r_t4 = lh(s, s5 + 10), r_a0 = lh(s, s4 + 10), \
    r_t7 = lh(s, s5 + 4), r_a3 = lh(s, s4 + 4), r_t3 = lh(s, s5 + 12), r_v1 = lh(s, s4 + 12), \
    r_t6 = lh(s, s5 + 6), r_a2 = lh(s, s4 + 6), r_t2 = lh(s, s5 + 14), r_v0 = lh(s, s4 + 14)
    LOADADDRS();
    for (;;) {
        /* this batch's samples and coefficients (addresses from the last sqv) */
        LDV(16, 0, r_s1);
        LDV(15, 0, r_t1);
        LDV(16, 8, r_t5);
        LDV(15, 8, r_a1);
        LDV(14, 0, r_s0);
        LDV(13, 0, r_t0);
        LDV(14, 8, r_t4);
        LDV(13, 8, r_a0);
        LDV(12, 0, r_t7);
        LDV(11, 0, r_a3);
        LDV(12, 8, r_t3);
        LDV(11, 8, r_v1);
        LDV(10, 0, r_t6);
        LDV(9, 0, r_a2);
        LDV(10, 8, r_t2);
        LDV(9, 8, r_v0);
        /* the next batch's positions (the microcode interleaves these) */
        VMUDM(23, 31, 23, 15);
        VMADH(23, 31, 22, 15);
        VMADM(22, 25, 18, 12);
        VMADN(23, 31, 30, 8);
        VMUDN(21, 31, 18, 10);
        VMADN(21, 22, 30, 10);
        VMUDL(17, 23, 18, 13);
        VMUDN(17, 17, 30, 12);
        VMADN(17, 31, 18, 11);
        VMULF(8, 16, 15, 0);
        VMULF(7, 14, 13, 0);
        SQV(s, 21, 0, s5);
        VMULF(6, 12, 11, 0);
        SQV(s, 17, 0, s4);
        VMULF(5, 10, 9, 0);
        LOADADDRS();
        vaddsub(u, 0, 8, 8, 8, 3);
        vaddsub(u, 0, 7, 7, 7, 3);
        vaddsub(u, 0, 6, 6, 6, 3);
        vaddsub(u, 0, 5, 5, 5, 3);
        vaddsub(u, 0, 8, 8, 8, 6);
        vaddsub(u, 0, 7, 7, 7, 6);
        vaddsub(u, 0, 6, 6, 6, 6);
        vaddsub(u, 0, 5, 5, 5, 6);
        VMUDN(4, 29, 8, 4);
        VMADN(4, 28, 7, 4);
        VMADN(4, 27, 6, 4);
        VMADN(4, 26, 5, 4);
        s2 -= 16;
        SQV(s, 4, 0, (uint32_t) s3);
        if (s2 <= 0) break;
        s3 += 16;
    }
#undef LOADADDRS
    {
        uint32_t s1 = r_s1, a2, a1, a0;
        SSV(23, 0, SCRATCH + 8);
        LDV(16, 0, s1);
        SDV(16, 0, SCRATCH);
        a2 = (uint32_t) lh(s, P_IN);
        s1 += 8;
        a1 = s1 - a2;
        a0 = a1 & 0xF;
        s1 -= a0;
        if (a0 != 0) a0 = 16 - a0;
        sh(s, SCRATCH + 10, a0);
        LDV(3, 0, s1);
        LDV(3, 8, s1 + 8);
        SQV(s, 3, 0, SCRATCH + 16);
        dma_write(s, bus, SCRATCH, lw(s, SCRATCH + 0x40), 31);
    }
}

/* A_ENVMIXER: volume ramps on the left and right, each sample mixed into
 * the dry pair (dry gain) and with A_AUX the wet pair (wet gain):
 *   buf = sat16((buf*0x7FFF*2 + 0x8000 + in*sat16((vol*gain*2 + 0x8000) >> 16)*2) >> 16).
 * The volume is 16.16 per lane; with A_INIT lane i of the first 8 samples
 * gets vol + rate*(i+1)/8 (rate is per 8 samples), then the whole vector
 * steps by rate (saturating high half) and is clamped at the target
 * (unsigned min when rate > 0, signed max otherwise).  A_INIT takes the
 * targets, rates and gains from A_SETVOL; later calls use the ones saved
 * in the 80-byte state.  With A_INIT the first block is followed by at
 * least one more (16 bytes = 8 samples each). */
static void cmd_envmixer(AspState *s, const AspBus *bus, uint32_t w0, uint32_t w1) {
    AspVU *u = &s->vu;
    uint32_t v0, t4, t2, t5, s3, s2, s1, s0, t7;
    int32_t t6, s5, s4;

    v0 = seg_addr(s, w1);
    VZERO(0);
    LQV(s, 31, 0, 0x60);
    LQV(s, 10, 0, 0x00);
    t4 = w0 >> 16;
    t2 = t4 & 1;
    LQV(s, 24, 0, P_RAMP_L);
    if (!t2) {
        dma_read(s, bus, SCRATCH, v0, 79, ASP_DMA_DATA);
        LQV(s, 20, 0, SCRATCH);
        LQV(s, 21, 0, SCRATCH + 16);
        LQV(s, 18, 0, SCRATCH + 32);
        LQV(s, 19, 0, SCRATCH + 48);
        LQV(s, 24, 0, SCRATCH + 64);
    }
    s->pending_dma = 0;
    t5 = (uint32_t) lh(s, P_IN);
    s3 = (uint32_t) lh(s, P_OUT);
    s2 = (uint32_t) lh(s, P_DRY_R);
    s1 = (uint32_t) lh(s, P_WET_L);
    s0 = (uint32_t) lh(s, P_WET_R);
    t6 = lh(s, P_COUNT);
    t7 = 16;
    s5 = MFC2(s, 24, 2);
    s4 = MFC2(s, 24, 8);
    if (!(t4 & 8)) {
        s1 = SCRATCH + 0x50;
        s0 = s1;
        t7 = 0;
    }
    LQV(s, 30, 0, 0xC0);
    if (t2) {
        LQV(s, 17, 0, t5);
        LQV(s, 29, 0, s3);
        LQV(s, 27, 0, s1);
        VZERO(21);
        LSV(20, 14, P_VOL_L);
        VMUDL(23, 30, 24, 10);
        VMADN(23, 30, 24, 9);
        VMADM(22, 31, 0, 8);
        VMADM(21, 31, 21, 15);
        VMADH(20, 31, 20, 15);
        VMADN(21, 31, 0, 8);
        if (s5 > 0) VCL(u, 20, 20, 24, 8);
        else VGE(u, 20, 20, 24, 8);
        VMULF(16, 20, 24, 14);
        VMULF(15, 20, 24, 15);
        VMULF(29, 29, 10, 14);
        VMACF(29, 17, 16, 0);
        VMULF(27, 27, 10, 14);
        VMACF(27, 17, 15, 0);
        SQV(s, 29, 0, s3);
        SQV(s, 27, 0, s1);
        LQV(s, 28, 0, s2);
        LQV(s, 26, 0, s0);
        VZERO(19);
        LSV(18, 14, P_VOL_R);
        VMUDL(23, 30, 24, 13);
        VMADN(23, 30, 24, 12);
        VMADM(22, 31, 0, 8);
        VMADM(19, 31, 19, 15);
        VMADH(18, 31, 18, 15);
        VMADN(19, 31, 0, 8);
        if (s4 > 0) VCL(u, 18, 18, 24, 11);
        else VGE(u, 18, 18, 24, 11);
        VMULF(16, 18, 24, 14);
        VMULF(15, 18, 24, 15);
        VMULF(28, 28, 10, 14);
        VMACF(28, 17, 16, 0);
        VMULF(26, 26, 10, 14);
        VMACF(26, 17, 15, 0);
        SQV(s, 28, 0, s2);
        SQV(s, 26, 0, s0);
        t6 -= 16;
        t5 += 16;
        s3 += 16;
        s2 += 16;
        s1 += t7;
        s0 += t7;
    }
    VADDC(u, 21, 21, 24, 10);
    vaddsub(u, 0, 20, 20, 24, 9);
    for (;;) {
        LQV(s, 17, 0, t5);
        if (s5 > 0) VCL(u, 20, 20, 24, 8);
        else VGE(u, 20, 20, 24, 8);
        VADDC(u, 19, 19, 24, 13);
        LQV(s, 29, 0, s3);
        vaddsub(u, 0, 18, 18, 24, 12);
        LQV(s, 27, 0, s1);
        VMULF(16, 20, 24, 14);
        SQV(s, 20, 0, SCRATCH);
        VMULF(15, 20, 24, 15);
        SQV(s, 21, 0, SCRATCH + 16);
        VMULF(29, 29, 10, 14);
        VMACF(29, 17, 16, 0);
        LQV(s, 28, 0, s2);
        VMULF(27, 27, 10, 14);
        LQV(s, 26, 0, s0);
        VMACF(27, 17, 15, 0);
        SQV(s, 29, 0, s3);
        if (s4 > 0) VCL(u, 18, 18, 24, 11);
        else VGE(u, 18, 18, 24, 11);
        VADDC(u, 21, 21, 24, 10);
        SQV(s, 27, 0, s1);
        vaddsub(u, 0, 20, 20, 24, 9);
        VMULF(16, 18, 24, 14);
        t6 -= 16;
        VMULF(15, 18, 24, 15);
        s3 += 16;
        VMULF(28, 28, 10, 14);
        s1 += t7;
        VMACF(28, 17, 16, 0);
        t5 += 16;
        VMULF(26, 26, 10, 14);
        VMACF(26, 17, 15, 0);
        SQV(s, 28, 0, s2);
        s2 += 16;
        SQV(s, 26, 0, s0);
        if (t6 <= 0) break;
        s0 += t7;
    }
    SQV(s, 18, 0, SCRATCH + 32);
    SQV(s, 19, 0, SCRATCH + 48);
    SQV(s, 24, 0, SCRATCH + 64);
    dma_write(s, bus, SCRATCH, v0, 79);
}

/* A_MIXER: out = sat16((out*0x7FFF*2 + 0x8000 + in*gain*2) >> 16), in
 * 32-byte blocks (the count rounded up) */
static void cmd_mixer(AspState *s, uint32_t w0, uint32_t w1) {
    AspVU *u = &s->vu;
    int32_t s2;
    uint32_t v0, at;

    LQV(s, 31, 0, 0);
    s2 = (int32_t) lhu(s, P_COUNT);
    if (s2 == 0) return;
    v0 = (w1 & 0xFFFF) + BUFBASE;
    at = (w1 >> 16) + BUFBASE;
    MTC2(s, 30, 0, w0 & 0xFFFF);
    s->pending_dma = 0;
    LQV(s, 25, 0, v0);
    LQV(s, 29, 0, at);
    LQV(s, 24, 0, v0 + 16);
    LQV(s, 28, 0, at + 16);
    VMULF(25, 25, 31, 14);
    LQV(s, 27, 0, at + 32);
    LQV(s, 23, 0, v0 + 32);
    VMACF(25, 29, 30, 8);
    s2 -= 32;
    VMULF(24, 24, 31, 14);
    LQV(s, 26, 0, at + 48);
    LQV(s, 22, 0, v0 + 48);
    at += 64;
    v0 += 64;
    VMACF(24, 28, 30, 8);
    if (s2 > 0) {
        for (;;) {
            VMULF(23, 23, 31, 14);
            SQV(s, 25, 0, v0 - 64);
            LQV(s, 29, 0, at);
            LQV(s, 25, 0, v0);
            VMACF(23, 27, 30, 8);
            VMULF(22, 22, 31, 14);
            SQV(s, 24, 0, v0 - 48);
            LQV(s, 28, 0, at + 16);
            LQV(s, 24, 0, v0 + 16);
            VMACF(22, 26, 30, 8);
            at += 64;
            VMULF(25, 25, 31, 14);
            SQV(s, 23, 0, v0 - 32);
            LQV(s, 27, 0, at - 32);
            LQV(s, 23, 0, v0 + 32);
            VMACF(25, 29, 30, 8);
            s2 -= 64;
            VMULF(24, 24, 31, 14);
            SQV(s, 22, 0, v0 - 16);
            LQV(s, 26, 0, at - 16);
            LQV(s, 22, 0, v0 + 48);
            VMACF(24, 28, 30, 8);
            v0 += 64;
            if (s2 <= 0) break;
        }
        s2 += 32;
        if (s2 <= 0) return;
    }
    SQV(s, 25, 0, v0 - 64);
    SQV(s, 24, 0, v0 - 48);
}

/* A_INTERLEAVE: left/right (w1 high/low) into the A_SETBUFF output as L,R
 * pairs; count bytes per channel, rounded up to 16 */
static void cmd_interleave(AspState *s, uint32_t w1) {
    int32_t a0 = (int32_t) lhu(s, P_COUNT);
    uint32_t out = lhu(s, P_OUT), right, left;
    int k;
    if (a0 == 0) return;
    right = (w1 & 0xFFFF) + BUFBASE;
    left = (w1 >> 16) + BUFBASE;
    s->pending_dma = 0;
    for (;;) {
        LQV(s, 1, 0, left);
        LQV(s, 2, 0, right);
        for (k = 0; k < 8; k++) {
            SSV(1, 2 * k, out + 4 * k);
            SSV(2, 2 * k, out + 4 * k + 2);
        }
        a0 -= 16;
        left += 16;
        right += 16;
        out += 32;
        if (a0 <= 0) break;
    }
}

static void cmd_clearbuff(AspState *s, uint32_t w0, uint32_t w1) {
    AspVU *u = &s->vu;
    int32_t n = (int32_t) (w1 & 0xFFFF);
    uint32_t at;
    if (n == 0) return;
    at = (w0 & 0xFFFF) + BUFBASE;
    VZERO(1);
    n -= 16;
    s->pending_dma = 0;
    for (;;) {
        SDV(1, 0, at);
        SDV(1, 0, at + 8);
        at += 16;
        if (n <= 0) break;
        n -= 16;
    }
}

static void cmd_dmemmove(AspState *s, uint32_t w0, uint32_t w1) {
    int32_t n = (int32_t) (w1 & 0xFFFF);
    uint32_t at, v0;
    if (n == 0) return;
    at = (w0 & 0xFFFF) + BUFBASE;
    v0 = (w1 >> 16) + BUFBASE;
    s->pending_dma = 0;
    for (;;) {
        LDV(1, 0, at);
        LDV(2, 0, at + 8);
        n -= 16;
        at += 16;
        SDV(1, 0, v0);
        SDV(2, 0, v0 + 8);
        v0 += 16;
        if (n <= 0) break;
    }
}

static void cmd_setbuff(AspState *s, uint32_t w0, uint32_t w1) {
    uint32_t in = w0 + BUFBASE, out = (w1 >> 16) + BUFBASE, wr = w1 + BUFBASE;
    if (!((w0 >> 16) & 8)) {
        sh(s, P_IN, in);
        sh(s, P_OUT, out);
        sh(s, P_COUNT, w1);
    } else {
        sh(s, P_WET_R, wr);
        sh(s, P_DRY_R, in);
        sh(s, P_WET_L, out);
    }
}

static void cmd_setvol(AspState *s, uint32_t w0, uint32_t w1) {
    uint32_t f = w0 >> 16;
    if (f & 8) {
        sh(s, P_DRY, w0);
        sh(s, P_WET, w1);
    } else if (f & 4) {
        sh(s, (f & 2) ? P_VOL_L : P_VOL_R, w0);
    } else {
        uint32_t base = (f & 2) ? P_RAMP_L : P_RAMP_R;
        sh(s, base, w0);
        sh(s, base + 2, w1 >> 16);
        sh(s, base + 4, w1);
    }
}

/* ------------------------------------------------------------ dispatch */

void asp_init(AspState *s) {
    memset(s, 0, sizeof *s);
}

void asp_run_task(AspState *s, const AspBus *bus, const AspTask *t) {
    uint32_t gp, sp, w0, w1, cmd;
    int32_t k1, fp, n;

    s->tasks++;
    /* An empty command list (alAudioFrame before the synthesizer has a
     * client) would make the microcode fetch with a length of -1: a DMA of
     * 256 rows that rewrites all of DMEM with RDRAM, then one command from
     * it.  That one command is arbitrary (a SAVEBUFF of garbage can land
     * anywhere), so the interpreter runs none. */
    if ((int32_t) t->data_size <= 0) {
        if (s->empty_tasks++ == 0 && s->log) s->log("asp: empty command list (task %u) skipped\n", s->tasks);
        return;
    }
    /* rspboot: ucode_data (ucode_data_size bytes) into DMEM 0 */
    if (t->ucode_data_size != 0)
        dma_read(s, bus, 0, t->ucode_data, (t->ucode_data_size - 1) & 0xFFF, ASP_DMA_UCODE_DATA);
    gp = t->data_ptr;
    k1 = (int32_t) t->data_size;
    sw(s, SEGTAB, 0);   /* the microcode's clear loop rewrites segment 0 only */
    for (;;) {
        /* fetch up to 0x140 bytes of commands */
        n = k1 > 0x140 ? 0x140 : k1;
        fp = n;
        dma_read(s, bus, CMDBUF, gp, (uint32_t) (n - 1), ASP_DMA_CMDLIST);
        s->pending_dma = 0;
        sp = CMDBUF;
        do {
            w0 = lw(s, sp);
            w1 = lw(s, sp + 4);
            gp += 8;
            k1 -= 8;
            sp += 8;
            fp -= 8;
            cmd = (w0 >> 24) & 0x7F;
            s->cmds++;
            if (cmd < 16) s->cmd_count[cmd]++;
            switch (cmd) {
                case 0: break;                                   /* A_SPNOOP */
                case 1: cmd_adpcm(s, bus, w0, w1); break;        /* A_ADPCM */
                case 2: cmd_clearbuff(s, w0, w1); break;         /* A_CLEARBUFF */
                case 3: cmd_envmixer(s, bus, w0, w1); break;     /* A_ENVMIXER */
                case 4:                                          /* A_LOADBUFF */
                    if (lhu(s, P_COUNT) != 0)
                        dma_read(s, bus, lhu(s, P_IN), seg_addr(s, w1), lhu(s, P_COUNT) - 1, ASP_DMA_DATA);
                    break;
                case 5: cmd_resample(s, bus, w0, w1); break;     /* A_RESAMPLE */
                case 6:                                          /* A_SAVEBUFF */
                    if (lhu(s, P_COUNT) != 0)
                        dma_write(s, bus, lhu(s, P_OUT), seg_addr(s, w1), lhu(s, P_COUNT) - 1);
                    break;
                case 7:                                          /* A_SEGMENT */
                    sw(s, SEGTAB + ((w1 >> 24) << 2), w1 & 0xFFFFFF);
                    break;
                case 8: cmd_setbuff(s, w0, w1); break;           /* A_SETBUFF */
                case 9: cmd_setvol(s, w0, w1); break;            /* A_SETVOL */
                case 10: cmd_dmemmove(s, w0, w1); break;         /* A_DMEMMOVE */
                case 11:                                         /* A_LOADADPCM */
                    dma_read(s, bus, TABLE, seg_addr(s, w1), (w0 & 0xFFFF) - 1, ASP_DMA_TABLE);
                    break;
                case 12: cmd_mixer(s, w0, w1); break;            /* A_MIXER */
                case 13: cmd_interleave(s, w1); break;           /* A_INTERLEAVE */
                case 14: cmd_polef(s, bus, w0, w1); break;       /* A_POLEF */
                case 15:                                         /* A_SETLOOP */
                    sw(s, P_LOOP, seg_addr(s, w1));
                    break;
                default:
                    /* the microcode would jump through data past its table */
                    if (s->bad_cmds++ == 0 && s->log) s->log("asp: unknown command %08x %08x\n", w0, w1);
                    break;
            }
        } while (fp > 0);
        if (k1 <= 0) break;
    }
}
