/* TMEM -> RGBA8 as RT64's texture decoder (see tmem_decode.h).  Each
 * function follows its HLSL counterpart in RT64 43373749's
 * shaders/TextureDecoder.hlsli and Formats.hlsli line for line, in bytes. */
#include "tmem_decode.h"

namespace tmem {

namespace {

constexpr uint32_t kBytes = 0x1000, kPalette = 0x800, kMask8 = 0xFFF, kMask16 = 0x7FF;

struct Px {
    uint8_t r, g, b, a;
};
Px gray(uint32_t i) {   /* a scalar float4 in HLSL: all four the same */
    return {uint8_t(i), uint8_t(i), uint8_t(i), uint8_t(i)};
}
const Px kBlack = {0, 0, 0, 255};

/* implLoadTMEM: odd rows have their 32-bit words swapped in pairs */
uint32_t load(const uint8_t *T, uint32_t rel, uint32_t mask, uint32_t orAddr, bool oddRow, uint32_t start, uint32_t rowSize) {
    /* (HLSL: x / 0 = 0xFFFFFFFF for uints, times 0 = 0) */
    const uint32_t rowStart = rowSize != 0 ? (rel / rowSize) * rowSize : 0;
    const uint32_t word = (rel - rowStart) / 4;
    const uint32_t addr = oddRow ? start + rowStart + ((word ^ 1) * 4) + (rel & 3) : start + rel;
    return T[((addr & mask) | orAddr) & kMask8];
}

Px i4(uint32_t v) {
    return gray((v << 4) | v);
}
Px ia4(uint32_t v) {
    uint32_t i = v & 0xE;
    i = (i << 4) | (i << 1) | (i >> 2);
    return {uint8_t(i), uint8_t(i), uint8_t(i), uint8_t((v & 1) ? 255 : 0)};
}
Px ia8(uint32_t v) {
    uint32_t i = (v >> 4) & 0xF, a = v & 0xF;
    i = (i << 4) | i;
    a = (a << 4) | a;
    return {uint8_t(i), uint8_t(i), uint8_t(i), uint8_t(a)};
}
Px rgba16(uint32_t v) {
    const uint32_t r = (v >> 11) & 0x1F, g = (v >> 6) & 0x1F, b = (v >> 1) & 0x1F;
    return {uint8_t((r << 3) | (r >> 2)), uint8_t((g << 3) | (g >> 2)), uint8_t((b << 3) | (b >> 2)),
            uint8_t((v & 1) ? 255 : 0)};
}
Px ia16(uint32_t v) {
    const uint32_t i = (v >> 8) & 0xFF;
    return {uint8_t(i), uint8_t(i), uint8_t(i), uint8_t(v & 0xFF)};
}

/* sampleTMEM */
Px sample(const uint8_t *T, uint32_t x, uint32_t y, uint32_t siz, uint32_t fmt, uint32_t address, uint32_t stride,
          uint32_t tlut, uint32_t palette) {
    const bool oddRow = (y & 1) != 0, oddColumn = (x & 1) != 0;
    const bool isRgba32 = fmt == FMT_RGBA && siz == SIZ_32;
    const bool usesTlut = tlut > 0;
    const uint32_t shift = isRgba32 ? 2 : siz;
    const uint32_t mask = (isRgba32 || usesTlut) ? kMask16 : kMask8;
    const uint32_t pixelAddress = y * stride + ((x << shift) >> 1);
    const uint32_t p0 = load(T, pixelAddress + 0, mask, 0, oddRow, address, stride);
    const uint32_t p1 = load(T, pixelAddress + 1, mask, 0, oddRow, address, stride);
    const uint32_t v4 = (p0 >> (oddColumn ? 0 : 4)) & 0xF;
    if (usesTlut) {
        const uint32_t pa = siz == SIZ_4 ? kPalette + (palette << 7) + (v4 << 3) : kPalette + (p0 << 3);
        const uint32_t pv = T[(pa + 1) & kMask8] | (T[pa & kMask8] << 8);
        if (tlut == TT_RGBA16) return rgba16(pv);
        if (tlut == TT_IA16) return ia16(pv);
        return kBlack;
    }
    const uint32_t pixelAddress2 = isRgba32 ? pixelAddress : pixelAddress + 2;
    const uint32_t orAddr = isRgba32 ? (kBytes >> 1) : 0;
    const uint32_t p2 = load(T, pixelAddress2 + 0, mask, orAddr, oddRow, address, stride);
    const uint32_t p3 = load(T, pixelAddress2 + 1, mask, orAddr, oddRow, address, stride);
    switch (siz) {
    case SIZ_4:
        switch (fmt) {
        case FMT_CI: return gray((palette << 4) | v4);   /* (I8 of the palette index; & 0xFF as UNORM) */
        case FMT_IA: return ia4(v4);
        case FMT_I:
        case FMT_RGBA: return i4(v4);
        default: return kBlack;
        }
    case SIZ_8:
        switch (fmt) {
        case FMT_IA: return ia8(p0);
        case FMT_I:
        case FMT_RGBA:
        case FMT_CI: return gray(p0);
        default: return kBlack;
        }
    case SIZ_16: {
        const uint32_t v16 = p1 | (p0 << 8);
        switch (fmt) {
        case FMT_RGBA: return rgba16(v16);
        case FMT_IA: return ia16(v16);
        case FMT_CI:
        case FMT_I: return {uint8_t(p0), uint8_t(p1), uint8_t(p0), uint8_t(p1)};
        default: return kBlack;
        }
    }
    case SIZ_32:
        switch (fmt) {
        case FMT_RGBA: return {uint8_t(p0), uint8_t(p1), uint8_t(p2), uint8_t(p3)};
        case FMT_CI:
        case FMT_IA:
        case FMT_I:
            return oddColumn ? Px{uint8_t(p0), uint8_t(p1), uint8_t(p0), uint8_t(p1)}
                             : Px{uint8_t(p2), uint8_t(p3), uint8_t(p2), uint8_t(p3)};
        default: return kBlack;
        }
    default: return kBlack;
    }
}

}  // namespace

void decode(const uint8_t *T, const Tile &t, unsigned width, unsigned height, uint32_t tlut, uint8_t *rgba) {
    /* TextureDecodeCB: address = tmem << 3, stride = line << 3 */
    const uint32_t address = uint32_t(t.tmem) << 3, stride = uint32_t(t.line) << 3;
    for (unsigned y = 0; y < height; y++)
        for (unsigned x = 0; x < width; x++) {
            const Px p = sample(T, x, y, t.siz, t.fmt, address, stride, tlut, t.palette);
            uint8_t *o = rgba + (size_t(y) * width + x) * 4;
            o[0] = p.r;
            o[1] = p.g;
            o[2] = p.b;
            o[3] = p.a;
        }
}

std::string formatName(unsigned fmt, unsigned siz) {
    static const char *const fmts[] = {"RGBA", "YUV", "CI", "IA", "I"};
    static const char *const sizes[] = {"4", "8", "16", "32"};
    return std::string(fmt < 5 ? fmts[fmt] : "?") + (siz < 4 ? sizes[siz] : "?");
}

std::string tlutName(uint32_t tlut) {
    return tlut == TT_RGBA16 ? "RGBA16" : tlut == TT_IA16 ? "IA16" : "-";
}

}  // namespace tmem
