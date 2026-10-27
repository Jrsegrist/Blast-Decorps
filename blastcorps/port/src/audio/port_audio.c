/* The platform's audio (see port_audio.h).
 *
 * RDRAM layout the interpreter sees (aspmain.h's DMA kinds): the game's C
 * writes the command list as host-order u32 words (like display lists), and
 * the ADPCM codebooks / pole-filter coefficients and the ADPCM loop states
 * as host-order s16 (the load layer swaps them in the sound banks:
 * port_assets.c bank_wavetable; libaudio computes the filter coefficients
 * and copies the loop state natively).  Everything else the microcode moves
 * stays in N64 byte order: sample data from the ROM, the microcode's own
 * state blocks, reverb delay lines and the output buffers the AI plays,
 * which no C code reads.  The bus below converts the first group to the
 * big-endian bytes the RSP would see.  The microcode's data segment (its
 * constant tables) is taken from the ROM's hd data image, since the load
 * layer may have swapped that range of RDRAM by type. */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "rdram.h"
#include "plat_host.h"
#include "audio/aspmain.h"
#include "audio/port_audio.h"

static AspState g_asp;
static uint8_t *g_hd_data;          /* the ROM's hd data image (big-endian) */
static const HostLive *g_live;
static int g_off;
static FILE *g_wav, *g_wav_idx;   /* FILE.wav and FILE.wav.frames (game frame -> sample offset) */
static uint32_t g_wav_bytes, g_wav_rate;
unsigned plat_frames(void);       /* save.c */
static unsigned g_buffers, g_rejected;
static int g_wav_all;               /* --wav-all: also the buffers a full AI FIFO dropped */
static unsigned long long g_frames_out;

/* --audio-capture: each task's inputs and results (port/tools/audio/capture.h) */
static FILE *g_cap;
static unsigned g_cap_max = ~0u, g_cap_n;
typedef struct { uint32_t addr, len, kind; } CapRange;
static struct { CapRange r; uint8_t *data; } *g_reads, *g_writes;
static unsigned g_nreads, g_nwrites, g_capreads, g_capwrites;

static inline uint8_t *ram(uint32_t a) {
    return (uint8_t *) (uintptr_t) (RDRAM_BASE + (a & (RDRAM_SIZE - 1)));
}

static void cap_add(int wr, uint32_t addr, const uint8_t *data, uint32_t len, int kind) {
    unsigned *n = wr ? &g_nwrites : &g_nreads, *cap = wr ? &g_capwrites : &g_capreads;
    if (*n == *cap) {
        *cap = *cap ? *cap * 2 : 256;
        if (wr) g_writes = host_realloc(g_writes, *cap * sizeof *g_writes);
        else g_reads = host_realloc(g_reads, *cap * sizeof *g_reads);
    }
    {
        CapRange r = { addr & (RDRAM_SIZE - 1), len, (uint32_t) kind };
        uint8_t *d = host_realloc(NULL, len);
        memcpy(d, data, len);
        if (wr) g_writes[*n].r = r, g_writes[*n].data = d;
        else g_reads[*n].r = r, g_reads[*n].data = d;
    }
    (*n)++;
}

static void bus_read(void *ctx, uint32_t addr, uint8_t *dst, uint32_t len, int kind) {
    uint32_t i;
    (void) ctx;
    addr &= RDRAM_SIZE - 1;
    switch (kind) {
        case ASP_DMA_UCODE_DATA: {
            uint32_t base = HD_DATA_VRAM & (RDRAM_SIZE - 1);
            if (g_hd_data != NULL && addr >= base && addr + len <= base + HD_DATA_SIZE) {
                memcpy(dst, g_hd_data + (addr - base), len);
                break;
            }
            memcpy(dst, ram(addr), len);
            break;
        }
        case ASP_DMA_CMDLIST:   /* host u32 words */
            for (i = 0; i < len; i += 4) {
                uint32_t w = *(const uint32_t *) ram(addr + i);
                dst[i] = (uint8_t) (w >> 24), dst[i + 1] = (uint8_t) (w >> 16);
                dst[i + 2] = (uint8_t) (w >> 8), dst[i + 3] = (uint8_t) w;
            }
            break;
        case ASP_DMA_TABLE:
        case ASP_DMA_LOOP_STATE:   /* host s16 */
            for (i = 0; i < len; i += 2) {
                uint16_t h = *(const uint16_t *) ram(addr + i);
                dst[i] = (uint8_t) (h >> 8), dst[i + 1] = (uint8_t) h;
            }
            break;
        default:
            memcpy(dst, ram(addr), len);
            break;
    }
    if (g_cap) cap_add(0, addr, dst, len, kind);
}

static void bus_write(void *ctx, uint32_t addr, const uint8_t *src, uint32_t len, int kind) {
    (void) ctx;
    memcpy(ram(addr), src, len);
    if (g_cap) cap_add(1, addr, src, len, kind);
}

static void asp_log(const char *fmt, ...) {
    char buf[256];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    host_log("%s", buf);
}

static void put32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t) v, p[1] = (uint8_t) (v >> 8), p[2] = (uint8_t) (v >> 16), p[3] = (uint8_t) (v >> 24);
}

static void wav_header(void) {
    uint8_t h[44];
    memcpy(h, "RIFF", 4);
    put32(h + 4, 36 + g_wav_bytes);
    memcpy(h + 8, "WAVEfmt ", 8);
    put32(h + 16, 16);
    h[20] = 1, h[21] = 0, h[22] = 2, h[23] = 0;
    put32(h + 24, g_wav_rate);
    put32(h + 28, g_wav_rate * 4);
    h[32] = 4, h[33] = 0, h[34] = 16, h[35] = 0;
    memcpy(h + 36, "data", 4);
    put32(h + 40, g_wav_bytes);
    fseek(g_wav, 0, SEEK_SET);
    fwrite(h, 44, 1, g_wav);
    fseek(g_wav, 0, SEEK_END);
}

void port_audio_init(const struct HostOpts *o) {
    asp_init(&g_asp);
    g_asp.log = asp_log;
    g_live = o->live;
    g_off = o->audio_off;
    g_hd_data = host_realloc(NULL, HD_DATA_SIZE);
    if (rom_gunzip(ROM_HD_DATA, g_hd_data, HD_DATA_SIZE) <= 0) {
        host_log("audio: can't inflate the hd data image; ucode data from RDRAM\n");
        free(g_hd_data);
        g_hd_data = NULL;
    }
    g_wav_all = o->wav_all;
    if (o->wav_path != NULL) {
        g_wav = fopen(o->wav_path, "wb");
        if (g_wav == NULL) host_fatal("can't write %s", o->wav_path);
        g_wav_rate = 22050;
        wav_header();
        {
            char idx[1024];
            snprintf(idx, sizeof idx, "%s.frames", o->wav_path);
            g_wav_idx = fopen(idx, "w");
        }
    }
    if (o->audio_capture != NULL) {
        g_cap = fopen(o->audio_capture, "wb");
        if (g_cap == NULL) host_fatal("can't write %s", o->audio_capture);
        if (o->audio_capture_max != 0) g_cap_max = o->audio_capture_max;
    }
}

void port_audio_task(unsigned ucode_data, unsigned ucode_data_size, unsigned data_ptr, unsigned data_size) {
    AspTask t;
    uint8_t dmem[4096];
    AspVU vu;
    unsigned i;
    if (g_off) return;
    t.ucode_data = ucode_data;
    t.ucode_data_size = ucode_data_size;
    t.data_ptr = data_ptr;
    t.data_size = data_size;
    if (g_cap != NULL && g_cap_n < g_cap_max) {
        memcpy(dmem, g_asp.dmem, sizeof dmem);
        vu = g_asp.vu;
        g_nreads = g_nwrites = 0;
    } else if (g_cap != NULL) {
        fclose(g_cap);
        g_cap = NULL;
    }
    asp_run_task(&g_asp, &(AspBus){ NULL, bus_read, bus_write }, &t);
    if (g_cap != NULL) {
        /* capture.h: header, DMEM, VU, reads (in order: a replay applies them
         * last to first, so the first read of a byte wins), writes */
        struct { char magic[4]; uint32_t index; AspTask task; uint32_t n_reads, n_writes; } h;
        memcpy(h.magic, "ASPC", 4);
        h.index = g_asp.tasks;
        h.task = t;
        h.n_reads = g_nreads;
        h.n_writes = g_nwrites;
        fwrite(&h, sizeof h, 1, g_cap);
        fwrite(dmem, sizeof dmem, 1, g_cap);
        fwrite(&vu, sizeof vu, 1, g_cap);
        for (i = 0; i < g_nreads; i++) {
            fwrite(&g_reads[i].r, sizeof(CapRange), 1, g_cap);
            fwrite(g_reads[i].data, g_reads[i].r.len, 1, g_cap);
            free(g_reads[i].data);
        }
        for (i = 0; i < g_nwrites; i++) {
            fwrite(&g_writes[i].r, sizeof(CapRange), 1, g_cap);
            fwrite(g_writes[i].data, g_writes[i].r.len, 1, g_cap);
            free(g_writes[i].data);
        }
        g_cap_n++;
    }
}

void port_audio_ai_buffer(unsigned addr, unsigned bytes, unsigned dacrate, unsigned vi_clock, int accepted) {
    static int16_t *buf;
    static unsigned cap;
    unsigned frames = bytes / 4, i, rate = dacrate ? vi_clock / dacrate : 22050;
    const uint8_t *p = ram(addr);
    if (!accepted) {
        g_rejected++;
        if (!g_wav_all) return;
    } else {
        g_buffers++;
        g_frames_out += frames;
    }
    if (frames == 0 || (g_wav == NULL && (g_live == NULL || g_live->audio == NULL))) return;
    if (frames * 2 > cap) {
        cap = frames * 2;
        buf = host_realloc(buf, cap * sizeof *buf);
    }
    /* the AI plays big-endian 16-bit L/R pairs */
    if ((addr & (RDRAM_SIZE - 1)) + bytes > RDRAM_SIZE) return;
    for (i = 0; i < frames * 2; i++) buf[i] = (int16_t) ((p[i * 2] << 8) | p[i * 2 + 1]);
    if (g_wav != NULL) {
        if (g_wav_bytes == 0) g_wav_rate = rate;
        if (g_wav_idx != NULL) fprintf(g_wav_idx, "%u %u\n", plat_frames(), g_wav_bytes / 4);
        fwrite(buf, 4, frames, g_wav);
        g_wav_bytes += frames * 4;
    }
    if (g_live != NULL && g_live->audio != NULL && accepted) g_live->audio(buf, frames, rate);
}

void port_audio_close(void) {
    if (g_wav != NULL) {
        wav_header();
        fclose(g_wav);
        g_wav = NULL;
    }
    if (g_wav_idx != NULL) {
        fclose(g_wav_idx);
        g_wav_idx = NULL;
    }
    if (g_cap != NULL) {
        fclose(g_cap);
        g_cap = NULL;
    }
    if (g_asp.tasks != 0)
        host_log("audio: %u tasks (%u commands, %u unknown, %u empty), %u AI buffers (%llu sample frames), %u dropped "
                 "by a full AI FIFO\n", g_asp.tasks, g_asp.cmds, g_asp.bad_cmds, g_asp.empty_tasks, g_buffers,
                 g_frames_out, g_rejected);
}
