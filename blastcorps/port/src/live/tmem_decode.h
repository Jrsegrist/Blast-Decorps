/* A texture as RT64 sees it: the 4 KB of TMEM RT64 keeps with each texture
 * of its cache (Texture::bytesTMEM) decoded to RGBA8, exactly as RT64's GPU
 * decoder does (shaders/TextureDecodeCS.hlsl -> TextureDecoder.hlsli
 * sampleTMEM, Formats.hlsli).  Every value RT64's shader produces is an
 * integer / 255 written to an R8G8B8A8_UNORM texture, so this byte-level
 * port gives the same bytes.  Used by the texture dump (live_texdump.cpp);
 * no RT64 headers needed. */
#ifndef TMEM_DECODE_H
#define TMEM_DECODE_H

#include <cstdint>
#include <string>

namespace tmem {

/* the RDP's texture formats (gbi.h G_IM_FMT_* / G_IM_SIZ_*) and TLUT modes
 * (G_TT_*: the other-mode bits as RT64 passes them) */
enum { FMT_RGBA = 0, FMT_YUV = 1, FMT_CI = 2, FMT_IA = 3, FMT_I = 4 };
enum { SIZ_4 = 0, SIZ_8 = 1, SIZ_16 = 2, SIZ_32 = 3 };
enum : uint32_t { TT_NONE = 0, TT_RGBA16 = 2u << 14, TT_IA16 = 3u << 14 };

struct Tile {
    unsigned fmt = 0, siz = 0;
    unsigned tmem = 0;      /* LoadTile::tmem: TMEM address in 64-bit words */
    unsigned line = 0;      /* LoadTile::line: row stride in 64-bit words */
    unsigned palette = 0;   /* LoadTile::palette */
};

/* decode width x height texels to RGBA8 (4 bytes a texel, rows top down);
 * tmem = 4096 bytes */
void decode(const uint8_t *tmem, const Tile &t, unsigned width, unsigned height, uint32_t tlut, uint8_t *rgba);

/* "CI4", "RGBA16", "IA8" ... (format and texel size as loaded) */
std::string formatName(unsigned fmt, unsigned siz);
/* "-", "RGBA16", "IA16" */
std::string tlutName(uint32_t tlut);

}  // namespace tmem

#endif
