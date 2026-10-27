/* address range lists for the capture tools */
#ifndef RANGES_H
#define RANGES_H

#include <stdint.h>
#include <stdlib.h>

typedef struct {
    uint32_t addr, len, kind;
} Range;

typedef struct {
    Range *r;
    unsigned n, cap;
} RangeList;

static inline void ranges_add(RangeList *l, uint32_t addr, uint32_t len, uint32_t kind) {
    if (l->n && l->r[l->n - 1].addr + l->r[l->n - 1].len == addr && l->r[l->n - 1].kind == kind) {
        l->r[l->n - 1].len += len;
        return;
    }
    if (l->n == l->cap) {
        l->cap = l->cap ? l->cap * 2 : 256;
        l->r = (Range *) realloc(l->r, l->cap * sizeof(Range));
    }
    l->r[l->n].addr = addr;
    l->r[l->n].len = len;
    l->r[l->n].kind = kind;
    l->n++;
}

static int ranges_cmp(const void *a, const void *b) {
    const Range *x = (const Range *) a, *y = (const Range *) b;
    return x->addr < y->addr ? -1 : x->addr > y->addr;
}

/* sort and merge overlapping/adjacent ranges (the first range's kind wins) */
static inline void ranges_merge(RangeList *l) {
    unsigned i, o = 0;
    if (l->n == 0) return;
    qsort(l->r, l->n, sizeof(Range), ranges_cmp);
    for (i = 1; i < l->n; i++) {
        Range *c = &l->r[o];
        if (l->r[i].addr <= c->addr + c->len) {
            uint32_t end = l->r[i].addr + l->r[i].len;
            if (end > c->addr + c->len) c->len = end - c->addr;
        } else {
            l->r[++o] = l->r[i];
        }
    }
    l->n = o + 1;
}

#endif
