/* An interpreter for Blast Corps' audio microcode command lists ("acmd",
 * the ABI of the SDK's aspMain; this ROM's build is the "Blast Corps / Diddy
 * Kong Racing" variant).  It executes an audio task's command list against
 * RDRAM the way the RSP does, with a model of the RSP's data memory (DMEM)
 * and of the vector unit operations each command uses, so its output matches
 * the real microcode bit for bit.  See port/README.md ("Audio").
 *
 * Plain C, no libultra types: the same file builds into the Windows port
 * and into the Linux test tools (which run it next to an LLE RSP). */
#ifndef ASPMAIN_H
#define ASPMAIN_H

#include <stdint.h>

/* What a DMA moves, so a bus can convert the layout of memory the CPU wrote
 * in its own byte order.  In N64 memory everything is big-endian; the port
 * keeps the command list and the 16-bit tables/loop states in host order. */
enum {
    ASP_DMA_UCODE_DATA,   /* the microcode's data segment (task ucode_data) */
    ASP_DMA_CMDLIST,      /* the command list: 32-bit words */
    ASP_DMA_TABLE,        /* A_LOADADPCM: ADPCM codebook / pole filter coefficients, s16[] */
    ASP_DMA_LOOP_STATE,   /* A_ADPCM with A_LOOP: the loop's start state (SETLOOP), s16[16] */
    ASP_DMA_DATA          /* everything else: sample data and the microcode's own state blocks */
};

typedef struct AspBus {
    void *ctx;
    /* RDRAM -> DMEM: `len` bytes (a multiple of 8) at physical `addr`
     * (8-aligned), delivered in N64 (big-endian) byte order */
    void (*read)(void *ctx, uint32_t addr, uint8_t *dst, uint32_t len, int kind);
    /* DMEM -> RDRAM: `len` bytes in N64 byte order (always ASP_DMA_DATA) */
    void (*write)(void *ctx, uint32_t addr, const uint8_t *src, uint32_t len, int kind);
} AspBus;

/* The OSTask fields the microcode uses (physical or KSEG0 addresses). */
typedef struct AspTask {
    uint32_t ucode_data, ucode_data_size;
    uint32_t data_ptr, data_size;
} AspTask;

typedef struct AspVU {
    int16_t v[32][8];
    int64_t acc[8];            /* 48-bit accumulator per lane, sign-extended */
    uint16_t vcol, vcoh, vccl, vcch;
    uint8_t vce;
} AspVU;

typedef struct AspState {
    uint8_t dmem[4096];        /* persists between tasks, as on the RSP */
    AspVU vu;
    uint32_t pending_dma;      /* the microcode's s6 "a DMA may be in flight" flag (no effect here) */
    /* statistics */
    uint32_t tasks, cmds, cmd_count[16], bad_cmds, imem_dma, odd_dma, empty_tasks;
    void (*log)(const char *fmt, ...);   /* optional diagnostics */
} AspState;

void asp_init(AspState *s);
/* run one audio task (the RSP boot loads ucode_data into DMEM 0 first, like rspboot) */
void asp_run_task(AspState *s, const AspBus *bus, const AspTask *t);

#endif
