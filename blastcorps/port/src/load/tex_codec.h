/* Texture slots for mods (tex_codec.c): the C twin of tools_port/bcfiles
 * texture.py and png.py, so that bc.exe and bc_headless apply a mod's
 * texture-table slots (bcmod/2 "textures") with the same bytes
 * `bcfiles mod-export` writes into a ROM.
 *
 *   PNG   decode: 8-bit RGB / RGBA, not interlaced, every filter (what png.py
 *         reads; with `any`, also grey, grey + alpha, palette and 16-bit
 *         images, for the editor's import, which stores them again as RGBA);
 *         encode: RGBA8 (or RGB8), filter 0, stored deflate blocks (any zlib reads it)
 *   N64   the texture table's packings (decode 0-6; encode 0, 1, 2, 3, 6 with
 *         texture.py's greedy literals and back-references), RGBA8 <-> texels
 *         (RGBA16/32, IA4/8/16, I4/8), encode_slot (the image into a slot:
 *         the slot's packing, the rest of a larger slot kept)
 *
 * Plain C, no RDRAM (bc_cameratest tests it).  Written for the port from the
 * formats in docs/systems/files.md and the PNG specification. */
#ifndef TEX_CODEC_H
#define TEX_CODEC_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define TC_TABLE 0x4CE0u      /* the texture table in the ROM: 4096 entries {u32 offset; u16 size; u16 packing} */
#define TC_ENTRIES 0x1000

/* ---- PNG ---- */
/* -> *rgba (malloc'd, w * h * 4); 0 ok, -1 and *err.  any: also the formats png.py doesn't read */
int tc_png_decode(const uint8_t *png, size_t n, int any, uint32_t *w, uint32_t *h, uint8_t **rgba, char *err,
                  size_t errlen);
/* RGBA8 -> a PNG file's bytes (malloc'd) */
uint8_t *tc_png_encode(const uint8_t *rgba, uint32_t w, uint32_t h, size_t *n);
/* the same for RGB8 (channels 3) or RGBA8 (4) pixels (w, h > 0): also bc.exe's screenshots (live_shots.cpp) */
uint8_t *tc_png_encode_px(const uint8_t *px, uint32_t w, uint32_t h, int channels, size_t *n);
/* base64 (RFC 4648, padded); decode: 0 ok */
char *tc_base64_encode(const uint8_t *p, size_t n);
int tc_base64_decode(const char *s, uint8_t **out, size_t *n);

/* ---- the table's packings ---- */
/* the packed bytes (exactly the entry's size of them) -> texels (malloc'd); pal: packings 4 and 5 (may be NULL) */
int tc_decode(const uint8_t *packed, size_t n, int ptype, const uint8_t *pal, size_t paln, uint8_t **out,
              size_t *outn);
/* texels rounded to what a packing's literals hold (in place) */
void tc_quantize(uint8_t *t, size_t n, int ptype);
/* representable texels -> the packing's stream (malloc'd); 0 ok, -1 and *err (packings 4, 5) */
int tc_pack(const uint8_t *t, size_t n, int ptype, uint8_t **out, size_t *outn, char *err, size_t errlen);

/* ---- texels ---- */
/* RGBA8 -> texels (fmt 0 RGBA, 3 IA, 4 I; siz in bits: 4, 8, 16, 32); 0 ok, -1 and *err */
int tc_from_rgba8(const uint8_t *rgba, uint32_t w, uint32_t h, int fmt, int siz, uint8_t **out, size_t *outn,
                  char *err, size_t errlen);
/* texels -> RGBA8 (w * h * 4 bytes into out); siz in bits; tlut: CI (16-bit RGBA5551 entries) or NULL */
void tc_to_rgba8(const uint8_t *t, size_t n, int fmt, int siz, uint32_t w, uint32_t h, const uint8_t *tlut,
                 size_t tlutn, uint8_t *out);
/* an image for a slot (texture.py encode_slot): its texel format, the slot's packing and decoded texels
 * (`orig`, orign: a slot that holds more keeps the rest) -> the packed bytes (malloc'd); 0 ok, -1 and *err */
int tc_encode_slot(const uint8_t *rgba, uint32_t w, uint32_t h, int fmt, int siz, int ptype, const uint8_t *orig,
                   size_t orign, uint8_t **packed, size_t *packedn, char *err, size_t errlen);
/* the same for a slot its lists draw with `mips` levels of detail (> 1; texture.py encode_slot(mips=)): the smaller
 * levels made from the image (tc_mip_texels) instead of kept */
int tc_encode_slot_mips(const uint8_t *rgba, uint32_t w, uint32_t h, int fmt, int siz, int ptype, const uint8_t *orig,
                        size_t orign, int mips, uint8_t **packed, size_t *packedn, char *err, size_t errlen);

/* ---- levels of detail (mipmaps; texture.py mip_layout / mip_down / mip_texels) ----
 * A mipmapped slot (its lists: one G_LOADBLOCK with dxt 0, tiles 0..n-1 at the levels): level k is (w >> k) x
 * (h >> k) (at least 1), rows padded to 8 bytes, the levels back to back; every odd row's 32-bit words swapped (the
 * TMEM row interleave the load leaves to the data) */
typedef struct {
    uint32_t offset, line, w, h;   /* bytes from the slot's start, bytes per row */
} TcMipLevel;
/* the layout (out: `levels` entries, may be NULL) -> the total bytes */
size_t tc_mip_layout(uint32_t w, uint32_t h, int siz, int levels, TcMipLevel *out);
/* RGBA8 -> the next level (2 x 2 rounded means; malloc'd) */
int tc_mip_down(const uint8_t *rgba, uint32_t w, uint32_t h, uint8_t **out, uint32_t *ow, uint32_t *oh);
/* the image and its levels as the slot's texels (malloc'd, tc_mip_layout's total bytes; what the layout doesn't
 * cover from orig, else 0); 0 ok */
int tc_mip_texels(const uint8_t *rgba, uint32_t w, uint32_t h, int fmt, int siz, int levels, const uint8_t *orig,
                  size_t orign, uint8_t **out, size_t *outn, char *err, size_t errlen);

/* ---- the palette packings (4, 5; texture.py palette_texels): the slot's own palette kept (every palette of the
 * game is shared by many textures: effect frames, medal sprites), each texel its nearest colour ----
 * 4: RGBA16 texels in pairs, a token's bytes (index << 1) | alpha bit (64 entries: a pair's first byte < 0x80);
 * 5: RGBA32 texels, a token (index << 4) | 4-bit alpha (128 entries).  pal: the palette entry's bytes */
int tc_palette_texels(const uint8_t *rgba, uint32_t w, uint32_t h, int ptype, const uint8_t *pal, size_t paln,
                      uint8_t **texels, size_t *tn, uint16_t **toks, size_t *nt, char *err, size_t errlen);
/* texels with their literal tokens (one per 4-byte unit) -> the stream (back-references as tc_pack) */
int tc_pack_tokens(const uint8_t *t, size_t n, int ptype, const uint16_t *toks, uint8_t **out, size_t *outn, char *err,
                   size_t errlen);
/* a picture for a palette packing's slot (fmt 0 and siz 16 for packing 4, 32 for 5; orign: the slot's decoded
 * size, the picture must fill it (0: not checked)) -> the packed bytes (malloc'd); 0 ok */
int tc_encode_slot_pal(const uint8_t *rgba, uint32_t w, uint32_t h, int fmt, int siz, int ptype, size_t orign,
                       const uint8_t *pal, size_t paln, uint8_t **packed, size_t *packedn, char *err, size_t errlen);

/* ---- the ROM's table ---- */
typedef struct {
    uint32_t offset;      /* from TC_TABLE */
    unsigned size, type;  /* packed size, packing */
    uint32_t dmaLen;      /* to the next entry's offset (the last: its size) */
} TcEntry;
/* entry i of the table in a ROM image (big-endian, .z64 order); 0 ok */
int tc_entry(const uint8_t *rom, size_t romsize, int i, TcEntry *e);
const char *tc_fmt_name(int fmt);   /* "RGBA", "YUV", "CI", "IA", "I" */

#ifdef __cplusplus
}
#endif

#endif
