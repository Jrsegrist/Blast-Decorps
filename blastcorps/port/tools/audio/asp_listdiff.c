/* asp_listdiff: compare the command lists of two captures task by task
 * (e.g. the emulator's, made by the ROM's synthesizer, and bc_headless's,
 * made by the native one, on the same clock).
 *   asp_listdiff A B [-n MAX] [-v N] [-skip K]
 * -skip K: B's task i is compared with A's task i+K.  Per command type: how
 * often the commands match exactly, match but for RDRAM addresses, or differ
 * in a parameter; -v N prints the first N parameter differences. */
#include "capfile.h"

static uint8_t *mem;
static void put(uint32_t addr, const uint8_t *src, uint32_t len) { memcpy(mem + (addr & (CAP_MEM - 1)), src, len); }

static const char *names[16] = { "SPNOOP", "ADPCM", "CLEARBUFF", "ENVMIXER", "LOADBUFF", "RESAMPLE", "SAVEBUFF",
                                 "SEGMENT", "SETBUFF", "SETVOL", "DMEMMOVE", "LOADADPCM", "MIXER", "INTERLEAVE",
                                 "POLEF", "SETLOOP" };
/* commands whose w1 is an RDRAM address */
static int w1_is_addr(uint32_t c) { return c == 1 || c == 3 || c == 4 || c == 5 || c == 6 || c == 11 || c == 14 || c == 15; }

typedef struct { uint32_t *w; unsigned n; uint32_t index; } List;

static int load(FILE *f, CapRecord *c, List *l) {
    uint32_t a, p;
    if (cap_next(f, c) != 1) return 0;
    cap_apply_reads(c, put);
    p = c->h.task.data_ptr & 0xFFFFFF;
    l->n = c->h.task.data_size / 8;
    l->w = realloc(l->w, (l->n * 2 + 2) * sizeof *l->w);
    l->index = c->h.index;
    for (a = 0; a < l->n * 2; a++) {
        const uint8_t *q = mem + p + a * 4;
        l->w[a] = (uint32_t) q[0] << 24 | q[1] << 16 | q[2] << 8 | q[3];
    }
    return 1;
}

int main(int argc, char **argv) {
    FILE *fa, *fb;
    static CapRecord ca, cb;
    List la = { 0 }, lb = { 0 };
    unsigned max = ~0u, verbose = 0, skip = 0, n = 0, i, same_tasks = 0, len_diff = 0, shown = 0;
    unsigned exact[16] = { 0 }, addr_only[16] = { 0 }, param[16] = { 0 };
    if (argc < 3) { fprintf(stderr, "usage: asp_listdiff A B [-n MAX] [-v N] [-skip K]\n"); return 2; }
    for (i = 3; i < (unsigned) argc; i++) {
        if (!strcmp(argv[i], "-n") && i + 1 < (unsigned) argc) max = (unsigned) atoi(argv[++i]);
        else if (!strcmp(argv[i], "-v") && i + 1 < (unsigned) argc) verbose = (unsigned) atoi(argv[++i]);
        else if (!strcmp(argv[i], "-skip") && i + 1 < (unsigned) argc) skip = (unsigned) atoi(argv[++i]);
    }
    fa = fopen(argv[1], "rb");
    fb = fopen(argv[2], "rb");
    if (!fa || !fb) { perror("open"); return 1; }
    mem = calloc(1, CAP_MEM);
    for (i = 0; i < skip; i++)
        if (!load(fa, &ca, &la)) return 1;
    while (n < max && load(fa, &ca, &la) && load(fb, &cb, &lb)) {
        unsigned k, m = la.n < lb.n ? la.n : lb.n, td = 0;
        n++;
        if (la.n != lb.n) len_diff++;
        for (k = 0; k < m; k++) {
            uint32_t a0 = la.w[2 * k], a1 = la.w[2 * k + 1], b0 = lb.w[2 * k], b1 = lb.w[2 * k + 1];
            uint32_t c = (a0 >> 24) & 0x7F;
            if (c >= 16) c = 0;
            if (a0 == b0 && a1 == b1) exact[c]++;
            else if (a0 == b0 && w1_is_addr(c) && (a0 >> 24) == (b0 >> 24)) addr_only[c]++;
            else {
                param[c]++;
                td++;
                if (shown < verbose) {
                    printf("task %u/%u cmd %u: A %-10s %08x %08x | B %08x %08x\n", la.index, lb.index, k, names[c], a0,
                           a1, b0, b1);
                    shown++;
                }
            }
        }
        if (la.n == lb.n && td == 0) same_tasks++;
    }
    printf("listdiff: %u task pairs, %u with the same commands but for RDRAM addresses, %u of different length\n", n,
           same_tasks, len_diff);
    for (i = 0; i < 16; i++)
        if (exact[i] + addr_only[i] + param[i])
            printf("  %-10s exact %8u  addresses only %8u  parameters %8u (%.2f%%)\n", names[i], exact[i], addr_only[i],
                   param[i], 100.0 * param[i] / (exact[i] + addr_only[i] + param[i]));
    return 0;
}
