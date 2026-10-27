/* bc_texturetest.exe: tests of the texture-pack tools on synthetic data (no
 * ROM, no renderer): the TMEM decoder the texture dump uses
 * (src/live/tmem_decode.cpp, against values worked out by hand from RT64's
 * TextureDecoder.hlsli / Formats.hlsli rules) and bc_packtool's build
 * (packtool.cpp: rt64.json, checks, unchanged pictures, .rtz, DDS mipmaps)
 * in a temporary folder.  Exit status 0: all pass. */
#include <windows.h>

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "../live/tmem_decode.h"
#include "miniz/miniz.h"
#include "packtool.h"

namespace fs = std::filesystem;

static int g_pass, g_fail;

static void check(bool ok, const std::string &what) {
    if (ok) g_pass++;
    else {
        g_fail++;
        printf("FAIL: %s\n", what.c_str());
    }
}

static std::vector<uint8_t> px(const std::vector<uint8_t> &tm, const tmem::Tile &t, unsigned w, unsigned h, uint32_t tlut) {
    std::vector<uint8_t> out(size_t(w) * h * 4);
    tmem::decode(tm.data(), t, w, h, tlut, out.data());
    return out;
}

static bool texel(const std::vector<uint8_t> &p, unsigned w, unsigned x, unsigned y, int r, int g, int b, int a) {
    const uint8_t *q = &p[(size_t(y) * w + x) * 4];
    return q[0] == r && q[1] == g && q[2] == b && q[3] == a;
}

static void testDecode() {
    std::vector<uint8_t> tm(0x1000, 0);
    tmem::Tile t;
    /* RGBA16 2x2, stride 8 bytes (line 1): row 0 = 0xF801 (red, a=1), 0x07C0 (green, a=0);
       row 1 is odd: its 32-bit words are swapped in pairs, so texel (0,1) sits at byte 8 + 4 */
    t.fmt = tmem::FMT_RGBA;
    t.siz = tmem::SIZ_16;
    t.line = 1;
    tm[0] = 0xF8, tm[1] = 0x01, tm[2] = 0x07, tm[3] = 0xC0;
    tm[8 + 4] = 0x00, tm[8 + 5] = 0x3F;   /* blue, a=1 */
    tm[8 + 6] = 0xFF, tm[8 + 7] = 0xFF;   /* white, a=1 */
    auto p = px(tm, t, 2, 2, tmem::TT_NONE);
    check(texel(p, 2, 0, 0, 255, 0, 0, 255), "RGBA16 red");
    check(texel(p, 2, 1, 0, 0, 255, 0, 0), "RGBA16 green, alpha 0");
    check(texel(p, 2, 0, 1, 0, 0, 255, 255), "RGBA16 odd row (word swap): blue");
    check(texel(p, 2, 1, 1, 255, 255, 255, 255), "RGBA16 odd row: white");
    /* 5-bit expansion: r = 1 -> (1 << 3) | (1 >> 2) = 8 */
    tm[0] = 0x08, tm[1] = 0x01;
    p = px(tm, t, 1, 1, tmem::TT_NONE);
    check(texel(p, 1, 0, 0, 8, 0, 0, 255), "RGBA16 5-bit expansion");

    /* CI4 with an RGBA16 palette 3: index nibbles 0x2 (even column: high nibble) and 0xF */
    std::fill(tm.begin(), tm.end(), uint8_t(0));
    t = tmem::Tile();
    t.fmt = tmem::FMT_CI;
    t.siz = tmem::SIZ_4;
    t.line = 1;
    t.palette = 3;
    tm[0] = 0x2F;
    const unsigned pal = 0x800 + (3 << 7);
    tm[pal + (2 << 3)] = 0x07, tm[pal + (2 << 3) + 1] = 0xC1;    /* index 2: green */
    tm[pal + (15 << 3)] = 0x84, tm[pal + (15 << 3) + 1] = 0x20;  /* index 15: r 16, g 16, b 16, a 0 */
    p = px(tm, t, 2, 1, tmem::TT_RGBA16);
    check(texel(p, 2, 0, 0, 0, 255, 0, 255), "CI4 RGBA16 palette, even column = high nibble");
    check(texel(p, 2, 1, 0, 132, 132, 132, 0), "CI4 RGBA16 palette, odd column");
    /* the same TMEM with no TLUT: CI4 reads as I8 of (palette << 4 | index), alpha too */
    p = px(tm, t, 2, 1, tmem::TT_NONE);
    check(texel(p, 2, 0, 0, 0x32, 0x32, 0x32, 0x32), "CI4 without a TLUT");

    /* CI8 with an IA16 palette */
    std::fill(tm.begin(), tm.end(), 0);
    t = tmem::Tile();
    t.fmt = tmem::FMT_CI;
    t.siz = tmem::SIZ_8;
    t.line = 1;
    tm[0] = 0x81;
    tm[0x800 + (0x81 << 3)] = 0x40, tm[0x800 + (0x81 << 3) + 1] = 0x80;
    p = px(tm, t, 1, 1, tmem::TT_IA16);
    check(texel(p, 1, 0, 0, 0x40, 0x40, 0x40, 0x80), "CI8 IA16 palette");

    /* IA8, IA4, I4, I8, IA16 */
    std::fill(tm.begin(), tm.end(), 0);
    t = tmem::Tile();
    t.fmt = tmem::FMT_IA;
    t.siz = tmem::SIZ_8;
    t.line = 1;
    tm[0] = 0xA5;
    p = px(tm, t, 1, 1, tmem::TT_NONE);
    check(texel(p, 1, 0, 0, 0xAA, 0xAA, 0xAA, 0x55), "IA8");
    t.siz = tmem::SIZ_4;
    tm[0] = 0xE3;   /* even: 0xE -> i 0b111 -> 255, a 0; odd: 0x3 -> i 0b001 -> 0x24 | ... a 1 */
    p = px(tm, t, 2, 1, tmem::TT_NONE);
    check(texel(p, 2, 0, 0, 255, 255, 255, 0), "IA4 even");
    check(texel(p, 2, 1, 0, 0x24, 0x24, 0x24, 255), "IA4 odd");
    t.fmt = tmem::FMT_I;
    p = px(tm, t, 2, 1, tmem::TT_NONE);
    check(texel(p, 2, 0, 0, 0xEE, 0xEE, 0xEE, 0xEE) && texel(p, 2, 1, 0, 0x33, 0x33, 0x33, 0x33), "I4 (alpha = intensity)");
    t.siz = tmem::SIZ_8;
    p = px(tm, t, 1, 1, tmem::TT_NONE);
    check(texel(p, 1, 0, 0, 0xE3, 0xE3, 0xE3, 0xE3), "I8");
    t.fmt = tmem::FMT_IA;
    t.siz = tmem::SIZ_16;
    tm[0] = 0x7F, tm[1] = 0x10;
    p = px(tm, t, 1, 1, tmem::TT_NONE);
    check(texel(p, 1, 0, 0, 0x7F, 0x7F, 0x7F, 0x10), "IA16");

    /* RGBA32: red/green in the low half of TMEM, blue/alpha in the high half */
    std::fill(tm.begin(), tm.end(), 0);
    t = tmem::Tile();
    t.fmt = tmem::FMT_RGBA;
    t.siz = tmem::SIZ_32;
    t.line = 1;
    tm[0] = 10, tm[1] = 20, tm[0x800] = 30, tm[0x801] = 40;
    tm[2] = 50, tm[3] = 60, tm[0x802] = 70, tm[0x803] = 80;
    p = px(tm, t, 2, 1, tmem::TT_NONE);
    check(texel(p, 2, 0, 0, 10, 20, 30, 40) && texel(p, 2, 1, 0, 50, 60, 70, 80), "RGBA32 split halves");

    /* a tile at a TMEM address: tmem 0x10 words = byte 0x80 */
    std::fill(tm.begin(), tm.end(), 0);
    t = tmem::Tile();
    t.fmt = tmem::FMT_I;
    t.siz = tmem::SIZ_8;
    t.line = 1;
    t.tmem = 0x10;
    tm[0x80] = 0x99;
    p = px(tm, t, 1, 1, tmem::TT_NONE);
    check(texel(p, 1, 0, 0, 0x99, 0x99, 0x99, 0x99), "tile TMEM address");
    check(tmem::formatName(tmem::FMT_CI, tmem::SIZ_4) == "CI4" && tmem::tlutName(tmem::TT_IA16) == "IA16", "names");
}

/* ---- the pack builder ---------------------------------------------------------- */
static std::vector<uint8_t> solid(unsigned w, unsigned h, uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    std::vector<uint8_t> v(size_t(w) * h * 4);
    for (size_t i = 0; i < v.size(); i += 4) v[i] = r, v[i + 1] = g, v[i + 2] = b, v[i + 3] = a;
    return v;
}

static void png(const fs::path &p, const std::vector<uint8_t> &px, unsigned w, unsigned h) {
    fs::create_directories(p.parent_path());
    check(packtool::writePng(p.u8string(), px.data(), w, h), "write " + p.u8string());
}

static std::string slurp(const fs::path &p) {
    std::ifstream in(p, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

static bool has(const packtool::Result &r, const std::string &s) {
    for (const std::string &l : r.lines)
        if (l.find(s) != std::string::npos) return true;
    return false;
}

static void testPack() {
    wchar_t tmpw[MAX_PATH];
    GetTempPathW(MAX_PATH, tmpw);
    const fs::path root = fs::path(tmpw) / ("bc_texturetest_" + std::to_string(GetCurrentProcessId()));
    std::error_code ec;
    fs::remove_all(root, ec);
    const fs::path dump = root / "texture_dump", src = root / "mine", packs = root / "packs";
    /* a synthetic dump: six textures and the index */
    const char *H[] = {"00000000000000a1", "00000000000000a2", "00000000000000a3", "00000000000000a4",
                       "00000000000000a5", "00000000000000a6"};
    png(dump / "level_00" / (std::string(H[0]) + ".png"), solid(8, 8, 200, 0, 0, 255), 8, 8);
    png(dump / "level_00" / (std::string(H[1]) + ".png"), solid(8, 8, 0, 200, 0, 0), 8, 8);
    png(dump / "frontend" / (std::string(H[2]) + ".png"), solid(16, 8, 0, 0, 200, 255), 16, 8);
    png(dump / "frontend" / (std::string(H[3]) + ".png"), solid(8, 8, 9, 9, 9, 255), 8, 8);
    png(dump / "frontend" / (std::string(H[4]) + ".png"), solid(8, 8, 50, 50, 50, 255), 8, 8);
    {
        std::ofstream idx(dump / "index.csv", std::ios::binary);
        idx << "hash,file,format,tlut,width,height,alpha,first_seen,first_frame,uploads\r\n";
        idx << H[0] << ",level_00/" << H[0] << ".png,RGBA16,-,8,8,opaque,level_00,1000,1\r\n";
        idx << H[1] << ",level_00/" << H[1] << ".png,RGBA16,-,8,8,binary,level_00,1000,1\r\n";
        idx << H[2] << ",frontend/" << H[2] << ".png,CI4,RGBA16,16,8,opaque,frontend,10,3\r\n";
        idx << H[3] << ",frontend/" << H[3] << ".png,IA8,-,8,8,opaque,frontend,10,1\r\n";
        idx << H[4] << ",frontend/" << H[4] << ".png,I8,-,8,8,opaque,frontend,10,1\r\n";
        idx << H[5] << ",,raw,-,32,32,-,frontend,10,1\r\n";
    }
    /* the edited pictures */
    png(src / "level_00" / (std::string(H[0]) + ".png"), solid(32, 32, 255, 255, 0, 255), 32, 32);   /* 4x: fine */
    png(src / "level_00" / (std::string(H[1]) + ".png"), solid(16, 16, 0, 0, 0, 255), 16, 16);       /* lost transparency */
    png(src / "frontend" / (std::string(H[2]) + ".png"), solid(24, 24, 1, 2, 3, 255), 24, 24);       /* not a multiple, shape */
    png(src / "frontend" / (std::string(H[3]) + ".png"), solid(8, 8, 9, 9, 9, 255), 8, 8);           /* unchanged */
    png(src / "frontend" / (std::string(H[4]) + ".png"), solid(8, 8, 60, 50, 50, 255), 8, 8);        /* same size, changed */
    png(src / (std::string(H[5]) + ".png"), solid(32, 32, 0, 0, 0, 255), 32, 32);                    /* raw: can't */
    png(src / "00000000000000ff.png", solid(8, 8, 1, 1, 1, 255), 8, 8);                              /* unknown hash */
    png(src / "notahash.png", solid(8, 8, 1, 1, 1, 255), 8, 8);                                      /* not a hash name */

    packtool::Options o;
    o.source = src.u8string();
    o.dump = dump.u8string();
    o.packs = packs.u8string();
    o.rtz = true;
    o.quiet = true;
    packtool::Result r = packtool::build(o);
    check(r.ok, "build ok");
    check(r.textures == 5, "5 textures packed (got " + std::to_string(r.textures) + ")");
    check(r.unchanged == 1, "1 unchanged left out");
    check(r.skipped == 3, "3 files left out (unchanged, raw, not a hash) (got " + std::to_string(r.skipped) + ")");
    check(has(r, "notahash.png: not named"), "not-a-hash message");
    check(has(r, "raw TMEM"), "raw message");
    check(has(r, "00000000000000ff isn't in the dump's index"), "unknown hash warning");
    check(has(r, "fully opaque"), "lost transparency warning");
    check(has(r, "24x24 isn't a whole multiple of the original 16x8"), "not a multiple warning");
    check(has(r, "isn't a power of two"), "power of two warning");
    check(r.warnings == 4, "4 warnings (got " + std::to_string(r.warnings) + ")");
    check(r.packDir == (packs / "mine").u8string(), "pack folder " + r.packDir);
    const std::string json = slurp(packs / "mine" / "rt64.json");
    check(json.find("\"hashVersion\": 5") != std::string::npos && json.find("\"autoPath\": \"rt64\"") != std::string::npos &&
              json.find("\"configurationVersion\": 3") != std::string::npos &&
              json.find("\"defaultOperation\": \"preload\"") != std::string::npos,
          "rt64.json configuration");
    check(json.find("{\"path\": \"level_00/00000000000000a1.png\", \"hashes\": {\"rt64\": \"00000000000000a1\", \"rice\": \"\"}}") !=
              std::string::npos,
          "rt64.json entry (scaled: default shift)");
    check(json.find("\"frontend/00000000000000a5.png\", \"hashes\": {\"rt64\": \"00000000000000a5\", \"rice\": \"\"}, \"shift\": \"none\"}") !=
              std::string::npos,
          "rt64.json entry (original size: shift none)");
    check(json.find("00000000000000a4") == std::string::npos, "unchanged texture not listed");
    check(fs::is_regular_file(packs / "mine" / "level_00" / "00000000000000a1.png") &&
              !fs::exists(packs / "mine" / "frontend" / "00000000000000a4.png"),
          "pack files");
    /* the .rtz: a zip with rt64.json and the pictures */
    mz_zip_archive zip = {};
    const std::string rtz = (packs / "mine.rtz").u8string();
    check(r.rtzPath == rtz, "rtz path");
    bool zok = mz_zip_reader_init_file(&zip, rtz.c_str(), 0) != 0;
    check(zok && mz_zip_reader_get_num_files(&zip) == 6, "rtz: 6 files");
    if (zok) {
        size_t n = 0;
        void *j = mz_zip_reader_extract_file_to_heap(&zip, "rt64.json", &n, 0);
        check(j != nullptr && std::string(static_cast<char *>(j), n) == json, "rtz rt64.json = the folder's");
        mz_free(j);
        check(mz_zip_reader_locate_file(&zip, "frontend/00000000000000a3.png", nullptr, 0) >= 0, "rtz picture");
        mz_zip_reader_end(&zip);
    }
    /* again with mipmaps, keeping the unchanged one, into another folder: stale files go */
    fs::create_directories(packs / "mine2" / "old");
    png(packs / "mine2" / "old" / "00000000000000b9.png", solid(4, 4, 0, 0, 0, 255), 4, 4);
    o.out = (packs / "mine2").u8string();
    o.rtz = false;
    o.mipmaps = true;
    o.keepUnchanged = true;
    r = packtool::build(o);
    check(r.ok && r.textures == 6, "mipmaps build: 6 textures");
    const std::string dds = slurp(packs / "mine2" / "level_00" / "00000000000000a1.dds");
    uint32_t w = 0, h = 0, mips = 0;
    if (dds.size() >= 128) {
        memcpy(&h, &dds[12], 4);
        memcpy(&w, &dds[16], 4);
        memcpy(&mips, &dds[28], 4);
    }
    check(dds.compare(0, 4, "DDS ") == 0 && w == 32 && h == 32 && mips == 6, "DDS 32x32, 6 levels");
    check(dds.size() == 128 + 4 * (32 * 32 + 16 * 16 + 8 * 8 + 4 * 4 + 2 * 2 + 1), "DDS size");
    check(dds.size() > 128 && uint8_t(dds[128 + 4 * (32 * 32 + 16 * 16 + 8 * 8 + 4 * 4 + 2 * 2)]) == 255 &&
              uint8_t(dds[128 + 4 * (32 * 32 + 16 * 16 + 8 * 8 + 4 * 4 + 2 * 2) + 2]) == 0,
          "DDS 1x1 level = the colour");
    check(!fs::exists(packs / "mine2" / "old" / "00000000000000b9.png"), "stale picture removed");
    check(slurp(packs / "mine2" / "rt64.json").find("00000000000000a4.dds") != std::string::npos, "kept unchanged, as DDS");
    /* the alpha-weighted box filter: a transparent texel doesn't darken its neighbours */
    std::vector<uint8_t> two = {255, 255, 255, 255, 0, 0, 0, 0};
    std::vector<uint8_t> d2 = packtool::makeDds(two.data(), 2, 1);
    check(d2.size() == 128 + 8 + 4 && uint8_t(d2[136]) == 255 && uint8_t(d2[139]) == 128, "box filter weights colour by alpha");
    uint64_t hh;
    check(packtool::parseHash("0123456789abcdef.mine.png", hh) && hh == 0x0123456789abcdefull &&
              !packtool::parseHash("0123456789abcde.png", hh),
          "hash names");
    fs::remove_all(root, ec);
}

int main() {
    testDecode();
    testPack();
    printf("bc_texturetest: %d/%d passed\n", g_pass, g_pass + g_fail);
    return g_fail == 0 ? 0 : 1;
}
