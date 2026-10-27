/* ai_dump.so: a mupen64plus audio plugin that writes what the game hands the
 * AI (every AI_LEN write: the buffer at AI_DRAM_ADDR) to a 16-bit stereo WAV
 * file, at the rate the AI is programmed to (AI_DACRATE).  Nothing is played.
 *   AI_DUMP=file.wav  (default ai_dump.wav)
 *   AI_DUMP_LOG=file  one line per buffer: index, address, bytes, dacrate */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "m64p_min.h"

static AUDIO_INFO ai;
static FILE *wav, *lg;
static uint32_t data_bytes, rate = 22050, nbuf;

static void put32(uint8_t *p, uint32_t v) { p[0] = v; p[1] = v >> 8; p[2] = v >> 16; p[3] = v >> 24; }

static void header(void) {
    uint8_t h[44];
    memcpy(h, "RIFF", 4);
    put32(h + 4, 36 + data_bytes);
    memcpy(h + 8, "WAVEfmt ", 8);
    put32(h + 16, 16);
    h[20] = 1; h[21] = 0; h[22] = 2; h[23] = 0;
    put32(h + 24, rate);
    put32(h + 28, rate * 4);
    h[32] = 4; h[33] = 0; h[34] = 16; h[35] = 0;
    memcpy(h + 36, "data", 4);
    put32(h + 40, data_bytes);
    fseek(wav, 0, SEEK_SET);
    fwrite(h, 44, 1, wav);
    fseek(wav, 0, SEEK_END);
}

EXPORT m64p_error CALL PluginStartup(m64p_dynlib_handle core, void *ctx, void (*dbg)(void *, int, const char *)) {
    (void) core; (void) ctx; (void) dbg;
    wav = fopen(getenv("AI_DUMP") ? getenv("AI_DUMP") : "ai_dump.wav", "wb");
    if (getenv("AI_DUMP_LOG")) lg = fopen(getenv("AI_DUMP_LOG"), "w");
    if (wav) header();
    return M64ERR_SUCCESS;
}
EXPORT m64p_error CALL PluginShutdown(void) {
    if (wav) { header(); fclose(wav); wav = NULL; }
    if (lg) { fclose(lg); lg = NULL; }
    return M64ERR_SUCCESS;
}
EXPORT m64p_error CALL PluginGetVersion(m64p_plugin_type *type, int *ver, int *api, const char **name, int *caps) {
    if (type) *type = M64PLUGIN_AUDIO;
    if (ver) *ver = 0x010000;
    if (api) *api = 0x020000;
    if (name) *name = "ai_dump";
    if (caps) *caps = 0;
    return M64ERR_SUCCESS;
}
EXPORT int CALL InitiateAudio(AUDIO_INFO info) { ai = info; return 1; }
EXPORT int CALL RomOpen(void) { return 1; }
EXPORT void CALL RomClosed(void) { if (wav) { header(); fflush(wav); } }
EXPORT void CALL AiDacrateChanged(int system) {
    uint32_t clock = system == 1 ? 49656530 : system == 2 ? 48628316 : 48681812;
    rate = clock / (*ai.AI_DACRATE_REG + 1);
}
EXPORT void CALL AiLenChanged(void) {
    uint32_t addr = *ai.AI_DRAM_ADDR_REG & 0xFFFFF8, len = *ai.AI_LEN_REG & 0x3FFF8, i;
    if (lg) fprintf(lg, "%u %06x %u %u\n", nbuf, addr, len, *ai.AI_DACRATE_REG);
    nbuf++;
    if (!wav) return;
    for (i = 0; i < len; i += 4) {
        /* emulator RDRAM: each word is a host u32 of the big-endian word (L << 16 | R) */
        uint32_t w = *(uint32_t *) (ai.RDRAM + ((addr + i) & 0x7FFFFC));
        int16_t s[2] = { (int16_t) (w >> 16), (int16_t) w };
        fwrite(s, 4, 1, wav);
    }
    data_bytes += len;
}
EXPORT void CALL ProcessAList(void) {}
EXPORT void CALL SetSpeedFactor(int percent) { (void) percent; }
EXPORT void CALL VolumeUp(void) {}
EXPORT void CALL VolumeDown(void) {}
EXPORT int CALL VolumeGetLevel(void) { return 100; }
EXPORT void CALL VolumeSetLevel(int level) { (void) level; }
EXPORT void CALL VolumeMute(void) {}
EXPORT const char *CALL VolumeGetString(void) { return "100%"; }
