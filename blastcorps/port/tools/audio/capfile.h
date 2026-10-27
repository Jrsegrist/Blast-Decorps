/* Reading capture.h records (asp_replay, asp_lle). */
#ifndef CAPFILE_H
#define CAPFILE_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "capture.h"
#include "ranges.h"

#define CAP_MEM 0x1000000u

typedef struct {
    CapHeader h;
    uint8_t dmem[4096];
    AspVU vu;
    /* reads, in capture order */
    CapRange *rd;
    uint8_t **rd_data;
    /* what the reference left in the bytes it wrote: last write wins */
    uint8_t *exp;          /* CAP_MEM shadow */
    uint8_t *mark;         /* CAP_MEM: byte written by the reference */
    RangeList wr;          /* merged written ranges */
} CapRecord;

static void cap_free_reads(CapRecord *c) {
    uint32_t i;
    for (i = 0; c->rd && i < c->h.n_reads; i++) free(c->rd_data[i]);
}

/* 1: a record was read; 0: end of file; -1: bad file */
static int cap_next(FILE *f, CapRecord *c) {
    uint32_t i, k;
    CapRange r;
    if (c->exp == NULL) {
        c->exp = calloc(1, CAP_MEM);
        c->mark = calloc(1, CAP_MEM);
    }
    /* clear the previous record's marks */
    for (i = 0; i < c->wr.n; i++) memset(c->mark + c->wr.r[i].addr, 0, c->wr.r[i].len);
    c->wr.n = 0;
    cap_free_reads(c);
    if (fread(&c->h, sizeof c->h, 1, f) != 1) return 0;
    if (memcmp(c->h.magic, "ASPC", 4)) return -1;
    if (fread(c->dmem, 4096, 1, f) != 1 || fread(&c->vu, sizeof c->vu, 1, f) != 1) return -1;
    c->rd = realloc(c->rd, (c->h.n_reads + 1) * sizeof *c->rd);
    c->rd_data = realloc(c->rd_data, (c->h.n_reads + 1) * sizeof *c->rd_data);
    for (i = 0; i < c->h.n_reads; i++) {
        if (fread(&c->rd[i], sizeof c->rd[i], 1, f) != 1 || c->rd[i].len > CAP_MEM) return -1;
        c->rd_data[i] = malloc(c->rd[i].len ? c->rd[i].len : 1);
        if (fread(c->rd_data[i], c->rd[i].len, 1, f) != 1) return -1;
    }
    for (i = 0; i < c->h.n_writes; i++) {
        uint8_t *d;
        if (fread(&r, sizeof r, 1, f) != 1 || r.len > CAP_MEM) return -1;
        d = malloc(r.len ? r.len : 1);
        if (fread(d, r.len, 1, f) != 1) return -1;
        for (k = 0; k < r.len; k++) {
            uint32_t a = (r.addr + k) & (CAP_MEM - 1);
            c->exp[a] = d[k];
            c->mark[a] = 1;
        }
        ranges_add(&c->wr, r.addr & (CAP_MEM - 1), r.len, r.kind);
        free(d);
    }
    ranges_merge(&c->wr);
    return 1;
}

/* the RDRAM the task read, as it was before the task: apply the reads last
 * to first, so the first read of a byte wins (bc_headless records every read
 * as it happens; rsp_tap records pre-task bytes) */
static void cap_apply_reads(const CapRecord *c, void (*put)(uint32_t addr, const uint8_t *src, uint32_t len)) {
    uint32_t i;
    for (i = c->h.n_reads; i-- > 0;) put(c->rd[i].addr, c->rd_data[i], c->rd[i].len);
}

#endif
