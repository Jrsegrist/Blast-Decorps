/* asp_dump: print the command lists of captured audio tasks (capture.h)
 *   asp_dump FILE [FIRST [COUNT]]
 * One line per command: name, flags and fields as abi.h spells them. */
#include "capfile.h"

static uint8_t *mem;
static void put(uint32_t addr, const uint8_t *src, uint32_t len) { memcpy(mem + (addr & (CAP_MEM - 1)), src, len); }

static const char *names[16] = { "SPNOOP", "ADPCM", "CLEARBUFF", "ENVMIXER", "LOADBUFF", "RESAMPLE", "SAVEBUFF",
                                 "SEGMENT", "SETBUFF", "SETVOL", "DMEMMOVE", "LOADADPCM", "MIXER", "INTERLEAVE",
                                 "POLEF", "SETLOOP" };

int main(int argc, char **argv) {
    FILE *f;
    static CapRecord c;
    unsigned first = 0, count = 1, n = 0, sizes = 0;
    if (argc < 2) { fprintf(stderr, "usage: asp_dump FILE [FIRST [COUNT]] [-sizes]\n"); return 2; }
    if (argc > 2) first = (unsigned) atoi(argv[2]);
    if (argc > 3) count = (unsigned) atoi(argv[3]);
    if (argc > 4 && !strcmp(argv[4], "-sizes")) sizes = 1;   /* one line per task: output frames */
    f = fopen(argv[1], "rb");
    if (!f) { perror(argv[1]); return 1; }
    mem = calloc(1, CAP_MEM);
    while (cap_next(f, &c) == 1) {
        uint32_t a, p = c.h.task.data_ptr & 0xFFFFFF;
        if (n++ < first) continue;
        if (n > first + count) break;
        cap_apply_reads(&c, put);
        if (!sizes) printf("task %u: list %06x, %u bytes\n", c.h.index, p, c.h.task.data_size);
        {
            uint32_t count_ = 0, out_frames = 0, interleaved = 0;
            for (a = p; a < p + c.h.task.data_size; a += 8) {
                const uint8_t *q = mem + a;
                uint32_t w0 = (uint32_t) q[0] << 24 | q[1] << 16 | q[2] << 8 | q[3];
                uint32_t w1 = (uint32_t) q[4] << 24 | q[5] << 16 | q[6] << 8 | q[7];
                uint32_t cmd = (w0 >> 24) & 0x7F;
                if (cmd == 8 && !((w0 >> 16) & 8)) count_ = w1 & 0xFFFF;
                if (cmd == 13) interleaved = 1;
                if (cmd == 6 && interleaved) out_frames += count_ / 4, interleaved = 0;
                if (!sizes) printf("  %-10s %02x %04x  %08x\n", cmd < 16 ? names[cmd] : "?", (w0 >> 16) & 0xFF, w0 & 0xFFFF, w1);
            }
            if (sizes) printf("%u %u\n", c.h.index, out_frames);
        }
    }
    return 0;
}
