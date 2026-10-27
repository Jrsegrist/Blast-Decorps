/* asp_replay: rerun captured audio tasks (capture.h) through aspmain and
 * compare what they write with the capture's reference results.
 * usage: asp_replay FILE [-n MAX] [-t TASK] [-q]
 *   -t TASK: print up to 64 differences of that task */
#include "capfile.h"

static uint8_t *mem;   /* RDRAM in N64 byte order */

static void bus_read(void *ctx, uint32_t addr, uint8_t *dst, uint32_t len, int kind) {
    (void) ctx; (void) kind;
    memcpy(dst, mem + (addr & (CAP_MEM - 1)), len);
}
static void bus_write(void *ctx, uint32_t addr, const uint8_t *src, uint32_t len, int kind) {
    (void) ctx; (void) kind;
    memcpy(mem + (addr & (CAP_MEM - 1)), src, len);
}
static void put(uint32_t addr, const uint8_t *src, uint32_t len) {
    memcpy(mem + (addr & (CAP_MEM - 1)), src, len);
}
static void logf_(const char *fmt, ...) { (void) fmt; }

int main(int argc, char **argv) {
    FILE *f;
    static CapRecord c;
    AspState st;
    unsigned max = ~0u, n = 0, same = 0, diff = 0, detail = ~0u, quiet = 0, i;
    unsigned long long bytes = 0, compared = 0;
    int r;
    if (argc < 2) {
        fprintf(stderr, "usage: asp_replay FILE [-n MAX] [-t TASK] [-q]\n");
        return 2;
    }
    for (i = 2; i < (unsigned) argc; i++) {
        if (!strcmp(argv[i], "-n") && i + 1 < (unsigned) argc) max = (unsigned) atoi(argv[++i]);
        else if (!strcmp(argv[i], "-t") && i + 1 < (unsigned) argc) detail = (unsigned) atoi(argv[++i]);
        else if (!strcmp(argv[i], "-q")) quiet = 1;
    }
    f = fopen(argv[1], "rb");
    if (!f) { perror(argv[1]); return 1; }
    mem = calloc(1, CAP_MEM);
    asp_init(&st);
    st.log = (void (*)(const char *, ...)) logf_;
    while (n < max && (r = cap_next(f, &c)) == 1) {
        unsigned nd = 0, shown = 0;
        memcpy(st.dmem, c.dmem, 4096);
        st.vu = c.vu;
        cap_apply_reads(&c, put);
        asp_run_task(&st, &(AspBus){ NULL, bus_read, bus_write }, &c.h.task);
        for (i = 0; i < c.wr.n; i++) {
            uint32_t a;
            compared += c.wr.r[i].len;
            for (a = c.wr.r[i].addr; a < c.wr.r[i].addr + c.wr.r[i].len; a++) {
                if (!c.mark[a]) continue;
                if (mem[a] != c.exp[a]) {
                    if (nd == 0 && !quiet && diff < 30) printf("task %u: differs\n", c.h.index);
                    if ((c.h.index == detail && shown < 64) || (shown < 4 && diff < 30 && !quiet)) {
                        printf("  %06x (in %06x+%x): aspmain %02x ref %02x\n", a, c.wr.r[i].addr, c.wr.r[i].len, mem[a],
                               c.exp[a]);
                        shown++;
                    }
                    nd++;
                }
                mem[a] = c.exp[a];   /* continue from the reference's state */
            }
        }
        n++;
        if (nd) { diff++; bytes += nd; } else same++;
    }
    if (r < 0) fprintf(stderr, "bad capture record\n");
    printf("replay: %u tasks, %u identical, %u different (%llu of %llu written bytes)\n", n, same, diff, bytes,
           compared);
    printf("commands %u:", st.cmds);
    for (i = 0; i < 16; i++) printf(" %u", st.cmd_count[i]);
    printf("\n");
    return diff != 0;
}
