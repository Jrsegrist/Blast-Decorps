/* bc_cameratest.exe's tests of the level editor: src/load/level_mod.c (the
 * records, the layout, the change list, mod files and their hashes) and
 * level_edit.cpp (undo / redo, the marks, the gizmo's ray math), on a
 * synthetic level file built here in the ROM's byte order (nothing from the
 * ROM). */
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "level_edit.h"
#include "viewer_records.h"

using namespace live;

namespace {

void (*g_check)(bool, const char *);
void check(bool ok, const char *what) {
    g_check(ok, what);
}

typedef std::vector<uint8_t> Bytes;

void put16(Bytes &b, int v) {
    b.push_back(uint8_t(v >> 8)), b.push_back(uint8_t(v));
}
void put32(Bytes &b, uint32_t v) {
    for (int i = 3; i >= 0; i--) b.push_back(uint8_t(v >> (8 * i)));
}
uint32_t get32(const Bytes &b, size_t at) {
    return uint32_t(b[at]) << 24 | uint32_t(b[at + 1]) << 16 | uint32_t(b[at + 2]) << 8 | b[at + 3];
}
int get16s(const Bytes &b, size_t at) {
    return int16_t(uint16_t(b[at] << 8 | b[at + 1]));
}

const unsigned kChain[] = {0x20, 0x24, 0x28, 0x2C, 0x30, 0x34, 0x38, 0x3C, 0x40, 0x44, 0x48, 0x4C, 0x50, 0x54, 0x58, 0x5C,
                           0x60, 0x64, 0x68, 0x6C, 0x70, 0x74, 0xA0, 0xA4, 0xA8, 0xAC, 0xB0, 0xB4, 0xB8, 0xBC, 0xC0};
unsigned alignOf(unsigned s) {
    switch (s) {
    case 0x2C: case 0x74: case 0xA0: case 0xA4: case 0xA8: case 0xAC: case 0xB0: case 0xB4: case 0xB8: case 0xBC:
        return 4;
    case 0x50: case 0x54: case 0x60: case 0x64: case 0x68: case 0x6C: case 0x70: case 0xC0:
        return 1;
    default:
        return 2;
    }
}

/* a level file as the ROM holds it (every field big-endian), laid out by the rules of levels.md / level.py:
 * each section at its alignment, the display-list block on 16 bytes, its lists' offsets in the header and
 * absolute offsets into it in the visibility records and groups */
Bytes makeLevel() {
    std::vector<Bytes> sec(31);
    auto S = [&](unsigned slot) -> Bytes & {
        for (int i = 0; i < 31; i++)
            if (kChain[i] == slot) return sec[i];
        return sec[0];
    };
    /* 0x20 crates: 2 */
    for (int i = 0; i < 2; i++) put16(S(0x20), 100 + i), put16(S(0x20), -5), put16(S(0x20), 300), put16(S(0x20), i);
    /* 0x24 guard triangles: 1 (0x14) */
    for (int i = 0; i < 10; i++) put16(S(0x24), i);
    /* 0x28 comm points: 1 */
    put16(S(0x28), -1200), put16(S(0x28), 40), put16(S(0x28), 2500), put16(S(0x28), 7);
    /* 0x30 ground grid 1 x 1: one cell, one triangle */
    put32(S(0x30), 4 + 0x14);
    for (int i = 0; i < 10; i++) put16(S(0x30), i * 3);
    /* 0x34 RDUs: 3 */
    for (int i = 0; i < 3; i++) put16(S(0x34), 1500 + 10 * i), put16(S(0x34), 20), put16(S(0x34), 2500);
    /* 0x38 TNT: 1 */
    put16(S(0x38), 10), put16(S(0x38), 11), put16(S(0x38), 12), S(0x38).push_back(1), S(0x38).push_back(5),
        put16(S(0x38), -2), put16(S(0x38), 9);
    /* 0x3C push blocks: 1 block, 1 slot with 2 triangles */
    put16(S(0x3C), 1);
    put16(S(0x3C), 200), put16(S(0x3C), 0), put16(S(0x3C), -300), put16(S(0x3C), 2);
    put16(S(0x3C), 1);
    put16(S(0x3C), 210), put16(S(0x3C), 1), put16(S(0x3C), -310), S(0x3C).push_back(1), S(0x3C).push_back(2),
        put16(S(0x3C), 1);
    for (int t = 0; t < 2; t++) {
        for (int v = 0; v < 3; v++) put16(S(0x3C), 210 + v), put16(S(0x3C), 1), put16(S(0x3C), -310 - v);
        put16(S(0x3C), 0x400), S(0x3C).push_back(1), S(0x3C).push_back(0);
    }
    /* 0x40 camera zone: 1 */
    for (int v : {-100, -200, 300, 400, 55}) put16(S(0x40), v);
    /* 0x48 visibility grid 1 x 1; 0x4C the level box */
    put16(S(0x48), 0), put16(S(0x48), 100);
    for (int v : {-4000, -3000, 4000, 3000}) put16(S(0x4C), v);
    /* 0x50 vehicle spawns: the driver, a Ramdozer on it */
    for (int t : {0, 4}) S(0x50).push_back(uint8_t(t)), put16(S(0x50), 100), put16(S(0x50), 5), put16(S(0x50), -200),
        put16(S(0x50), 0x400);
    /* 0x54 the carrier */
    S(0x54).push_back(28), put16(S(0x54), 1980), put16(S(0x54), 2018), put16(S(0x54), 0), put16(S(0x54), 3000);
    /* 0x5C object placements: 2 */
    for (int i = 0; i < 2; i++) {
        put16(S(0x5C), 1234 + i), put16(S(0x5C), 56), put16(S(0x5C), 4321), put16(S(0x5C), 0x12 + i);
        S(0x5C).push_back(uint8_t(i)), S(0x5C).push_back(3), put16(S(0x5C), 0), put16(S(0x5C), i);
    }
    /* 0x60 alpha zones: the default byte only */
    S(0x60).push_back(0xFF);
    /* 0x6C collision grid A 1 x 1: empty cell; 0x70 grid B: one triangle (0x16) */
    put32(S(0x6C), 4);
    put32(S(0x70), 4 + 0x16);
    for (int i = 0; i < 11; i++) put16(S(0x70), 7 * i);
    /* 0x74 part groups: none */
    put32(S(0x74), 0);
    /* the display-list block: 5 commands */
    Bytes dl;
    for (int i = 0; i < 5; i++) put32(dl, 0x06000000u), put32(dl, uint32_t(8 * i));
    /* lay it out twice: the visibility records hold the block's offset */
    Bytes out;
    uint32_t base = 0;
    for (int pass = 0; pass < 2; pass++) {
        Bytes vr, vg;
        put32(vr, base + 0), put32(vr, base + 8), put32(vr, base + 16), put32(vr, 1), put32(vr, 0);
        put32(vg, base + 8), put32(vg, base + 8), put32(vg, base + 24), put32(vg, 2), put32(vg, 1), put32(vg, 0);
        S(0xA0) = vr;
        S(0xB0) = vg;
        out.assign(0xC8, 0);
        const int h[12] = {1, 1, 1000, 1000, 1, 1, 500, 500, 1, 1, 800, 800};
        for (int i = 0; i < 12; i++) out[2 * i] = uint8_t(h[i] >> 8), out[2 * i + 1] = uint8_t(h[i]);
        out[0x18] = out[0x19] = out[0x1A] = 0xFF, out[0x1B] = 0xFC;   /* gravity -4 */
        out[0x1F] = 1;
        for (int v = 0; v < 2; v++)
            for (int k = 0; k < 16; k++) out.push_back(uint8_t(k + 16 * v));
        uint32_t off[31];
        for (int i = 0; i < 31; i++) {
            while (out.size() % alignOf(kChain[i])) out.push_back(0);
            off[i] = uint32_t(out.size());
            out.insert(out.end(), sec[i].begin(), sec[i].end());
        }
        while (out.size() % 16) out.push_back(0);
        base = uint32_t(out.size());
        if (pass == 1) {
            out.insert(out.end(), dl.begin(), dl.end());
            while (out.size() % 16) out.push_back(0);
            auto w32 = [&](unsigned slot, uint32_t v) {
                for (int k = 0; k < 4; k++) out[slot + k] = uint8_t(v >> (24 - 8 * k));
            };
            for (int i = 0; i < 31; i++) w32(kChain[i], off[i]);
            w32(0x78, base);
            const unsigned dls[8] = {0x7C, 0x80, 0x88, 0x8C, 0x90, 0x94, 0x98, 0x9C};
            for (int i = 0; i < 8; i++) w32(dls[i], base + 8 * (i % 5));
            w32(0x84, base + uint32_t(dl.size()));
        }
    }
    return out;
}

uint32_t hdr(const Bytes &b, unsigned slot) {
    return get32(b, slot);
}

/* a level with a little terrain for level_geo.c (as test_bcfiles.py geometry_level): a 200 x 100 quad at y 10
 * (two drawn triangles in a sub-list with Rare's G_CULLDL prologue, called from fixed list 0; the same two as
 * floor triangles in both cells of a 2 x 1 ground grid of 100-unit cells), a wall at x 150 in cell 1 of grid A (the
 * half-open rule's answer; wallTie: in cell 0 too, as Rare listed 11 walls exactly 50 units from a cell) */
Bytes makeGeoLevel(bool wallTie = false) {
    std::vector<Bytes> sec(31);
    auto S = [&](unsigned slot) -> Bytes & {
        for (int i = 0; i < 31; i++)
            if (kChain[i] == slot) return sec[i];
        return sec[0];
    };
    const int t1[9] = {0, 10, 0, 200, 10, 0, 200, 10, 100}, t2[9] = {0, 10, 0, 200, 10, 100, 0, 10, 100};
    const int wall[9] = {150, 10, 0, 150, 60, 0, 150, 60, 100};
    for (int c = 0; c < 2; c++) {
        put32(S(0x30), uint32_t(S(0x30).size() + 4 + 2 * 0x14));
        for (const int *t : {t1, t2}) {
            for (int k = 0; k < 9; k++) put16(S(0x30), t[k]);
            S(0x30).push_back(1), S(0x30).push_back(0);
        }
        if (c == 1 || wallTie) {
            put32(S(0x6C), uint32_t(S(0x6C).size() + 4 + 0x16));
            for (int k = 0; k < 9; k++) put16(S(0x6C), wall[k]);
            put16(S(0x6C), 0x400), S(0x6C).push_back(1), S(0x6C).push_back(0);
        } else {
            put32(S(0x6C), uint32_t(S(0x6C).size() + 4));
        }
        put32(S(0x70), uint32_t(S(0x70).size() + 4));
        put16(S(0x48), 0), put16(S(0x48), 10);
    }
    for (int v : {-50, -50, 250, 150}) put16(S(0x4C), v);
    S(0x50).push_back(0), put16(S(0x50), 10), put16(S(0x50), 0), put16(S(0x50), 10), put16(S(0x50), 0);
    S(0x54).push_back(0), put16(S(0x54), 0), put16(S(0x54), 0), put16(S(0x54), 0), put16(S(0x54), 0);
    S(0x60).push_back(0xFF);
    put32(S(0x74), 0);
    Bytes verts;
    auto vtx = [&](int x, int y, int z, int s, int t, int r, int g, int b, int a) {
        put16(verts, x), put16(verts, y), put16(verts, z), put16(verts, 0), put16(verts, s), put16(verts, t);
        verts.push_back(uint8_t(r)), verts.push_back(uint8_t(g)), verts.push_back(uint8_t(b)), verts.push_back(uint8_t(a));
    };
    for (int z : {0, 100})
        for (int x : {0, 200})
            for (int k = 0; k < 2; k++) vtx(x, 10, z, 0, 0, 0, 0, 0, 0);   /* the box, Rare's corner order */
    const int quad[4][3] = {{0, 10, 0}, {200, 10, 0}, {200, 10, 100}, {0, 10, 100}};
    for (const auto &q : quad) vtx(q[0], q[1], q[2], q[0] * 4, q[2] * 4, 200, 100, 50, 255);
    Bytes dl;
    const uint32_t cmds[][2] = {{0xE7000000u, 0},                                          /* 0x00 prefix */
                                {0x04700080u, 0x08000000u}, {0xBE000000u, 0x140},           /* 0x08 the sub-list */
                                {0xFD100000u, 7}, {0x04300040u, 0x08000080u},
                                {0xBF000000u, 0x00000A14u}, {0xBF000000u, 0x0000141Eu}, {0xB8000000u, 0},
                                {0x06000000u, 8}, {0xB8000000u, 0},                         /* 0x40 fixed 0 */
                                {0xB8000000u, 0}, {0xB8000000u, 0}, {0xB8000000u, 0}};
    for (const auto &c : cmds) put32(dl, c[0]), put32(dl, c[1]);
    Bytes out(0xC8, 0);
    const int h[12] = {2, 1, 100, 100, 2, 1, 100, 100, 2, 1, 100, 100};
    for (int i = 0; i < 12; i++) out[2 * i] = uint8_t(h[i] >> 8), out[2 * i + 1] = uint8_t(h[i]);
    out[0x18] = out[0x19] = out[0x1A] = 0xFF, out[0x1B] = 0xFC;
    out[0x1F] = 1;
    out.insert(out.end(), verts.begin(), verts.end());
    uint32_t off[31];
    for (int i = 0; i < 31; i++) {
        while (out.size() % alignOf(kChain[i])) out.push_back(0);
        off[i] = uint32_t(out.size());
        out.insert(out.end(), sec[i].begin(), sec[i].end());
    }
    while (out.size() % 16) out.push_back(0);
    const uint32_t base = uint32_t(out.size());
    out.insert(out.end(), dl.begin(), dl.end());
    while (out.size() % 16) out.push_back(0);
    auto w32 = [&](unsigned slot, uint32_t v) {
        for (int k = 0; k < 4; k++) out[slot + k] = uint8_t(v >> (24 - 8 * k));
    };
    for (int i = 0; i < 31; i++) w32(kChain[i], off[i]);
    w32(0x78, base);
    const unsigned dls[8] = {0x7C, 0x80, 0x88, 0x8C, 0x90, 0x94, 0x98, 0x9C};
    const uint32_t rel[8] = {0x40, 0x40, 0, 8, 0x40, 0x50, 0x58, 0x60};
    for (int i = 0; i < 8; i++) w32(dls[i], base + rel[i]);
    w32(0x84, base + uint32_t(dl.size()));
    return out;
}

/* the visibility grid's cell c (min, max) */
void visCell(const Bytes &b, int c, int &mn, int &mx) {
    const uint32_t at = get32(b, 0x48) + 4 * uint32_t(c);
    mn = get16s(b, at), mx = get16s(b, at + 2);
}

void geoTests() {
    char err[512];
    const Bytes g0 = makeGeoLevel();
    uint8_t *o = nullptr;
    size_t on = 0;
    check(lg_rebuild(g0.data(), g0.size(), 1, &o, &on, err, sizeof err) == 0 && on == g0.size() &&
              !memcmp(o, g0.data(), on),
          "geo: keeping Rare's data, a level comes back byte for byte");
    free(o);
    int mn, mx;
    check(lg_rebuild(g0.data(), g0.size(), 0, &o, &on, err, sizeof err) == 0, "geo: a full rebuild");
    if (o) {
        const Bytes r(o, o + on);
        visCell(r, 0, mn, mx);
        check(mn == 0 && mx == 10, "geo: the visibility heights are the drawn vertices' (cell 0)");
        /* the wall at x 150 only touches cell 0's grown rectangle at its high edge: in cell 1 alone */
        const uint32_t a = get32(r, 0x6C);
        check(get32(r, a) == 4 && get32(r, a + 4) == 8 + 0x16, "geo: wall cells are half-open (the wall is in cell 1 only)");
        free(o);
    }
    {
        /* a file listing the wall in cell 0 too: the full rebuild keeps that for the unchanged wall */
        const Bytes gt = makeGeoLevel(true);
        uint8_t *t = nullptr;
        size_t tn = 0;
        check(lg_rebuild(gt.data(), gt.size(), 0, &t, &tn, err, sizeof err) == 0, "geo: a full rebuild (wall tie)");
        if (t) {
            const Bytes r(t, t + tn);
            const uint32_t a = get32(r, 0x6C);
            check(get32(r, a) == 4 + 0x16 && get32(r, a + 4 + 0x16) == 8 + 2 * 0x16,
                  "geo: a wall Rare listed exactly 50 units from a cell keeps that cell in a full rebuild");
            free(t);
        }
    }
    LgEdit e;
    memset(&e, 0, sizeof e);
    check(lg_apply(g0.data(), g0.size(), g0.data(), g0.size(), &e, 3, &o, &on, err, sizeof err) == 0 && on == g0.size() &&
              !memcmp(o, g0.data(), on),
          "geo: an empty edit gives the file back");
    free(o);
    LgMove mv = {{200, 10, 100}, {200, 300, 100}};
    e.move = &mv, e.nmove = 1;
    check(lg_apply(g0.data(), g0.size(), g0.data(), g0.size(), &e, 3, &o, &on, err, sizeof err) == 0, "geo: a move");
    if (o) {
        const Bytes r(o, o + on);
        visCell(r, 1, mn, mx);
        int mn0, mx0;
        visCell(r, 0, mn0, mx0);
        check(mn == 0 && mx == 300 && mn0 == 0 && mx0 == 10, "geo: the moved corner raises its cell's height only");
        check(on == g0.size(), "geo: moved vertices are written over the old ones (the file keeps its size)");
        /* the culling box grew: the sub-list's first G_VTX loads a box with y up to 300 */
        const uint32_t base = get32(r, 0x78), w1 = get32(r, base + 8 + 4);
        int ymax = -99999;
        for (int k = 0; k < 8; k++) ymax = std::max(ymax, get16s(r, 0xC8 + 16 * ((w1 & 0xFFFFFF) / 16 + k) + 2));
        check(ymax == 300, "geo: the G_CULLDL box is the moved list's bounding box");
        free(o);
    }
    e.move = nullptr, e.nmove = 0;
    LgAddRender add;
    memset(&add, 0, sizeof add);
    add.after = 1;
    const int16_t av[3][9] = {{0, 10, 100, 0, 0, 9, 9, 9, 255}, {200, 10, 100, 0, 0, 9, 9, 9, 255},
                              {100, 20, 190, 0, 0, 9, 9, 9, 255}};
    memcpy(add.v, av, sizeof av);
    int rem0 = 0;
    e.render = &add, e.nrender = 1, e.rem[0] = &rem0, e.nrem[0] = 1;
    check(lg_apply(g0.data(), g0.size(), g0.data(), g0.size(), &e, 3, &o, &on, err, sizeof err) == 0,
          "geo: a triangle removed and one added");
    if (o) {
        const Bytes r(o, o + on);
        const uint32_t base = get32(r, 0x78), end = get32(r, 0x84);
        int tris = 0;
        for (uint32_t q = base; q < end; q += 8) tris += (get32(r, q) >> 24) == 0xBF;
        check(tris == 2, "geo: the list draws the kept and the added triangle");
        free(o);
    }
    int bad = 99;
    e.render = nullptr, e.nrender = 0, e.rem[0] = &bad;
    check(lg_apply(g0.data(), g0.size(), g0.data(), g0.size(), &e, 3, &o, &on, err, sizeof err) != 0 && o == nullptr,
          "geo: removing a triangle that isn't there is refused");
    /* mod files: bcmod/2 carries geometry, bcmod/1 can't */
    {
        char sha[41];
        lm_sha1_hex(g0.data(), g0.size(), sha);
        std::string t2 = std::string("{\"format\": \"bcmod/2\", \"levels\": [{\"level\": 3, \"original_sha1\": \"") + sha +
                         "\", \"geometry\": {\"move\": [[200, 10, 100, 200, 40, 100]]}}]}";
        LmMod m;
        check(lm_mod_parse(t2.c_str(), t2.size(), &m, err, sizeof err) == 0 && m.nlevels == 1 && m.levels[0].geo &&
                  m.levels[0].geo->nmove == 1 && m.format == 2,
              "geo: a bcmod/2 level with geometry and no changes reads");
        if (m.nlevels == 1) {
            uint8_t *x = nullptr;
            size_t xn = 0;
            check(lm_mod_apply(&m.levels[0], g0.data(), g0.size(), &x, &xn, err, sizeof err) == 0 && x != nullptr,
                  "geo: a geometry mod applies");
            if (x) {
                const Bytes r(x, x + xn);
                visCell(r, 1, mn, mx);
                check(mx == 40, "geo: ... with its derived sections regenerated");
                free(x);
            }
        }
        lm_mod_free(&m);
        std::string t1 = std::string("{\"format\": \"bcmod/1\", \"levels\": [{\"level\": 3, \"original_sha1\": \"") + sha +
                         "\", \"changes\": [], \"geometry\": {}}]}";
        check(lm_mod_parse(t1.c_str(), t1.size(), &m, err, sizeof err) != 0, "geo: bcmod/1 can't carry geometry");
        std::string t3 = std::string("{\"format\": \"bcmod/2\", \"levels\": [{\"level\": 3, \"original_sha1\": \"") + sha +
                         "\", \"geometry\": {\"move\": [[1, 2, 3, 4, 5, 70000]]}}]}";
        check(lm_mod_parse(t3.c_str(), t3.size(), &m, err, sizeof err) != 0, "geo: a coordinate outside s16 is refused");
    }
}

}  // namespace

void editTests(void (*chk)(bool, const char *)) {
    g_check = chk;
    char err[512];
    /* ---- SHA-1 (FIPS 180 examples) ---- */
    {
        char h[41];
        lm_sha1_hex(reinterpret_cast<const uint8_t *>("abc"), 3, h);
        check(!strcmp(h, "a9993e364706816aba3e25717850c26c9cd0d89d"), "edit: SHA-1 of \"abc\"");
        lm_sha1_hex(reinterpret_cast<const uint8_t *>(""), 0, h);
        check(!strcmp(h, "da39a3ee5e6b4b0d3255bfef95601890afd80709"), "edit: SHA-1 of nothing");
        const char *m = "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
        lm_sha1_hex(reinterpret_cast<const uint8_t *>(m), strlen(m), h);
        check(!strcmp(h, "84983e441c3bd26ebaae4aa1f95129e5e54670f1"), "edit: SHA-1 of the 56-byte example (two blocks)");
    }
    const Bytes orig = makeLevel();
    LmLevel *lv = lm_open(orig.data(), orig.size(), err, sizeof err);
    check(lv != nullptr, "edit: a synthetic level file opens");
    if (lv == nullptr) {
        printf("  (%s)\n", err);
        return;
    }
    /* ---- the records and an unchanged build ---- */
    {
        size_t n = 0;
        uint32_t split = 0;
        uint8_t *b = lm_build(lv, &n, &split);
        check(b != nullptr && n == orig.size() && !memcmp(b, orig.data(), n) && split == hdr(orig, 0x78),
              "edit: an unchanged level builds byte for byte (and the split is the block's start)");
        free(b);
        check(lm_count(lv, LM_VEHICLE) == 2 && lm_count(lv, LM_CARRIER) == 1 && lm_count(lv, LM_OBJECT) == 2 &&
                  lm_count(lv, LM_RDU) == 3 && lm_count(lv, LM_TNT) == 1 && lm_count(lv, LM_CRATE) == 2 &&
                  lm_count(lv, LM_COMMPT) == 1 && lm_count(lv, LM_BLOCK) == 1 && lm_count(lv, LM_SLOT) == 1,
              "edit: every kind's records are counted");
        long v;
        check(lm_get(lv, LM_OBJECT, 1, lm_field_index(LM_OBJECT, "type"), &v) == 0 && v == 0x13,
              "edit: an object's type");
        check(lm_get(lv, LM_CARRIER, 0, lm_field_index(LM_CARRIER, "limit"), &v) == 0 && v == 3000,
              "edit: the carrier's route length");
        check(lm_get(lv, LM_CRATE, 0, lm_field_index(LM_CRATE, "y"), &v) == 0 && v == -5, "edit: a signed field");
        check(lm_get(lv, LM_RDU, 3, 0, &v) != 0 && lm_get(lv, LM_RDU, 0, 7, &v) != 0,
              "edit: no record or field past the end");
        check(lm_check(lv, err, sizeof err) == 0, "edit: the synthetic level keeps the game's rules");
        check(lm_kind_from_key("slot") == LM_SLOT && !strcmp(lm_kind_key(LM_COMMPT), "commpt") &&
                  lm_kind_from_key("nope") < 0,
              "edit: the kinds' names (the viewer's keys)");
    }
    /* ---- a set: only that record's bytes ---- */
    {
        LmLevel *c = lm_clone(lv);
        check(lm_set(c, LM_OBJECT, 0, lm_field_index(LM_OBJECT, "x"), 1880, err, sizeof err) == 0 &&
                  lm_set(c, LM_OBJECT, 0, lm_field_index(LM_OBJECT, "z"), 1881, err, sizeof err) == 0,
              "edit: set an object's x and z");
        size_t n;
        uint8_t *b = lm_build(c, &n, nullptr);
        const uint32_t o = hdr(orig, 0x5C);
        size_t diff = 0, at = 0;
        for (size_t i = 0; b && i < n && n == orig.size(); i++)
            if (b[i] != orig[i]) diff++, at = i;
        check(b && n == orig.size() && diff == 4 && at >= o && at < o + 14 && b[o] == 1880 >> 8 && b[o + 1] == (1880 & 255),
              "edit: ... changes those 4 bytes and nothing else");
        free(b);
        check(lm_set(c, LM_OBJECT, 0, lm_field_index(LM_OBJECT, "x"), -1, err, sizeof err) != 0 && strstr(err, "outside"),
              "edit: a value outside the field's range is refused");
        check(lm_set(c, LM_VEHICLE, 0, lm_field_index(LM_VEHICLE, "heading"), 5000, err, sizeof err) != 0,
              "edit: a heading over 0xFFF is refused");
        lm_free(c);
    }
    /* ---- an add of an odd-sized record: the layout moves, the padding and the block's offsets follow ---- */
    {
        LmLevel *c = lm_clone(lv);
        LmChange add = edit::makeAdd(LM_VEHICLE, 2, 1, {{lm_field_index(LM_VEHICLE, "x"), 400}});
        check(lm_apply(c, &add, err, sizeof err) == 0 && lm_count(c, LM_VEHICLE) == 3, "edit: add a vehicle spawn");
        size_t n;
        uint8_t *b = lm_build(c, &n, nullptr);
        const Bytes nb(b, b + n);
        free(b);
        bool ascending = true, aligned = true;
        for (unsigned s = 0x24; s <= 0x74; s += 4) ascending &= hdr(nb, s) >= hdr(nb, s - 4);
        aligned = hdr(nb, 0x58) % 2 == 0 && hdr(nb, 0x74) % 4 == 0 && hdr(nb, 0xA0) % 4 == 0 && hdr(nb, 0x78) % 16 == 0;
        check(ascending && aligned, "edit: ... the chain still ascends, each section on its alignment");
        check(hdr(nb, 0x54) == hdr(orig, 0x54) + 9 && hdr(nb, 0x20) == hdr(orig, 0x20),
              "edit: ... the sections after the spawns moved by 9, those before stayed");
        const uint32_t d = hdr(nb, 0x78) - hdr(orig, 0x78);
        check(d % 16 == 0 && !memcmp(nb.data() + hdr(nb, 0x78), orig.data() + hdr(orig, 0x78), hdr(orig, 0x84) - hdr(orig, 0x78)) &&
                  hdr(nb, 0x84) == hdr(orig, 0x84) + d && hdr(nb, 0x9C) == hdr(orig, 0x9C) + d,
              "edit: ... the display-list block moved whole, its header words with it");
        check(get32(nb, hdr(nb, 0xA0)) == get32(orig, hdr(orig, 0xA0)) + d &&
                  get32(nb, hdr(nb, 0xA0) + 8) == get32(orig, hdr(orig, 0xA0) + 8) + d &&
                  get32(nb, hdr(nb, 0xA0) + 12) == 1 && get32(nb, hdr(nb, 0xB0) + 4) == get32(orig, hdr(orig, 0xB0) + 4) + d &&
                  get32(nb, hdr(nb, 0xB0) + 0x10) == 1,
              "edit: ... the visibility records' and groups' absolute offsets moved with it (ids and counts kept)");
        check(!memcmp(nb.data() + hdr(nb, 0x5C), orig.data() + hdr(orig, 0x5C), 28) &&
                  !memcmp(nb.data() + hdr(nb, 0x70), orig.data() + hdr(orig, 0x70), 4 + 0x16),
              "edit: ... the objects and collision grid B unchanged at their new places");
        /* the viewer reads the edited file (the ROM's byte order) */
        std::vector<std::string> notes;
        const std::vector<viewer::Record> r = viewer::parseLevel(nb.data(), nb.size(), &notes, true);
        int veh = 0, obj = 0;
        double x2 = 0;
        for (const viewer::Record &x : r) {
            veh += x.kind == viewer::REC_VEHICLE;
            obj += x.kind == viewer::REC_OBJECT;
            if (x.kind == viewer::REC_VEHICLE && x.index == 2) x2 = x.x;
        }
        check(veh == 3 && obj == 2 && x2 == 400 && notes.empty(),
              "edit: the viewer reads the edited file (file order): the new spawn, the objects, no notes");
        /* and back: deleting it gives the original */
        LmChange del = edit::makeDelete(LM_VEHICLE, 2);
        check(lm_apply(c, &del, err, sizeof err) == 0, "edit: delete it again");
        b = lm_build(c, &n, nullptr);
        check(b && n == orig.size() && !memcmp(b, orig.data(), n), "edit: ... the file is the original again");
        free(b);
        lm_free(c);
    }
    /* ---- slots move their triangles; blocks and slots counted ---- */
    {
        LmLevel *c = lm_clone(lv);
        check(lm_set(c, LM_SLOT, 0, lm_field_index(LM_SLOT, "x"), 260, err, sizeof err) == 0, "edit: move a slot");
        size_t n;
        uint8_t *b = lm_build(c, &n, nullptr);
        const Bytes nb(b, b + n);
        free(b);
        const uint32_t s = hdr(nb, 0x3C) + 2 + 8 + 2;
        check(get16s(nb, s) == 260 && get16s(nb, s + 10) == 260 && get16s(nb, s + 16) == 261 &&
                  get16s(nb, s + 10 + 0x16) == 260 && get16s(nb, s + 12) == 1,
              "edit: ... its triangles' x moved by the same amount (y kept)");
        LmChange add = edit::makeAdd(LM_BLOCK, 1, 0, {});
        check(lm_apply(c, &add, err, sizeof err) == 0 && lm_count(c, LM_BLOCK) == 2 && lm_count(c, LM_SLOT) == 1,
              "edit: add a push block");
        b = lm_build(c, &n, nullptr);
        check(b && get16s(Bytes(b, b + n), hdr(Bytes(b, b + n), 0x3C)) == 2, "edit: ... the block count is 2");
        free(b);
        lm_free(c);
    }
    /* ---- the game's rules ---- */
    {
        LmLevel *c = lm_clone(lv);
        LmChange add = edit::makeAdd(LM_COMMPT, 1, 0, {});
        check(lm_apply(c, &add, err, sizeof err) == 0, "edit: a second communication point");
        check(lm_apply(c, &add, err, sizeof err) != 0 && strstr(err, "at most 2"),
              "edit: a third is refused (the game holds 2)");
        LmChange carrier = edit::makeAdd(LM_CARRIER, 1, 0, {});
        check(lm_apply(c, &carrier, err, sizeof err) != 0, "edit: a second carrier record is refused");
        LmChange delc = edit::makeDelete(LM_CARRIER, 0);
        check(lm_apply(c, &delc, err, sizeof err) != 0, "edit: the carrier record can't be deleted");
        LmChange deld = edit::makeDelete(LM_VEHICLE, 0);
        check(lm_apply(c, &deld, err, sizeof err) == 0 && lm_check(c, err, sizeof err) != 0 && strstr(err, "driver"),
              "edit: without the driver's spawn the level breaks the rules");
        lm_free(c);
        c = lm_clone(lv);
        LmChange t12 = edit::makeSet(LM_VEHICLE, 1, {{0, 12}});
        check(lm_apply(c, &t12, err, sizeof err) == 0 && lm_check(c, err, sizeof err) != 0,
              "edit: vehicle type 12 (no code) breaks the rules");
        LmChange bad = edit::makeSet(LM_VEHICLE, 1, {{0, 4}, {1, 1}, {4, 99999}});
        const long before = [&] { long v; lm_get(c, LM_VEHICLE, 1, 1, &v); return v; }();
        long after;
        check(lm_apply(c, &bad, err, sizeof err) != 0 && lm_get(c, LM_VEHICLE, 1, 1, &after) == 0 && after == before,
              "edit: a change with one bad field changes nothing");
        lm_free(c);
    }
    /* ---- the editor: undo, redo, marks ---- */
    {
        edit::Editor ed;
        check(ed.open(7, orig.data(), orig.size()), "editor: opens the level");
        const std::string sha0 = ed.imageSha1();
        check(sha0 == ed.originalSha1() && !ed.dirty() && !ed.changed(), "editor: unchanged: the image is the original");
        check(ed.push(edit::makeSet(LM_RDU, 1, {{0, 1600}, {2, 2600}})), "editor: move an RDU");
        check(ed.get(LM_RDU, 1, "x") == 1600 && ed.edited(LM_RDU, 1) == 1 && ed.edited(LM_RDU, 0) == 0 && ed.dirty(),
              "editor: ... it reads back, is marked, the image is dirty");
        check(ed.push(edit::makeAdd(LM_RDU, 0, 2, {{0, 10}})), "editor: duplicate an RDU to the front");
        check(ed.count(LM_RDU) == 4 && ed.edited(LM_RDU, 0) == 1 && ed.edited(LM_RDU, 2) == 1 && ed.edited(LM_RDU, 1) == 0,
              "editor: ... the marks follow the records' order");
        check(!ed.push(edit::makeDelete(LM_VEHICLE, 0)) && !ed.error().empty() && ed.changes().size() == 2,
              "editor: a change that breaks the rules is refused (and not recorded)");
        check(ed.undo() && ed.count(LM_RDU) == 3 && ed.canRedo(), "editor: undo");
        check(ed.undo() && ed.imageSha1() == sha0 && !ed.canUndo(), "editor: undo again: the original file");
        check(ed.redo() && ed.redo() && ed.count(LM_RDU) == 4 && ed.get(LM_RDU, 2, "x") == 1600,
              "editor: redo twice");
        const std::string sha2 = ed.imageSha1();
        ed.markApplied();
        check(!ed.dirty(), "editor: applied: not dirty");
        /* the mod file */
        const std::string text = ed.modText("test mod", "a \"quoted\" description");
        LmMod m;
        check(lm_mod_parse(text.data(), text.size(), &m, err, sizeof err) == 0 && m.nlevels == 1 &&
                  m.levels[0].level == 7 && m.levels[0].nchanges == 2 && !strcmp(m.name, "test mod") &&
                  !strcmp(m.description, "a \"quoted\" description"),
              "mod: written and read back");
        uint8_t *out = nullptr;
        size_t outn = 0;
        check(lm_mod_apply(&m.levels[0], orig.data(), orig.size(), &out, &outn, err, sizeof err) == 0 &&
                  outn == ed.image().size() && !memcmp(out, ed.image().data(), outn),
              "mod: applied to the original: the editor's file");
        free(out);
        Bytes other = orig;
        other[0xC8] ^= 1;
        check(lm_mod_apply(&m.levels[0], other.data(), other.size(), &out, &outn, err, sizeof err) != 0 &&
                  strstr(err, "another level file") && out == nullptr,
              "mod: refused for another ROM's file (the SHA-1)");
        m.levels[0].edited_sha1[0] ^= 1;
        check(lm_mod_apply(&m.levels[0], orig.data(), orig.size(), &out, &outn, err, sizeof err) != 0 &&
                  strstr(err, "came out"),
              "mod: a result other than the maker's is refused (edited_sha1)");
        lm_mod_free(&m);
        /* a mod's changes under the editor's own */
        edit::Editor ed2;
        std::vector<LmChange> base = {edit::makeSet(LM_TNT, 0, {{4, 9}})};
        check(ed2.open(7, orig.data(), orig.size(), base, "base") && ed2.edited(LM_TNT, 0) == 2 &&
                  ed2.get(LM_TNT, 0, "timer") == 9 && !ed2.canUndo(),
              "editor: a loaded mod's changes are under the editor's (marked, not undone)");
        ed2.push(edit::makeSet(LM_TNT, 0, {{0, 77}}));
        const std::string t2 = ed2.modText("m2", "");
        check(lm_mod_parse(t2.data(), t2.size(), &m, err, sizeof err) == 0 && m.levels[0].nchanges == 2,
              "editor: the saved mod holds the mod's changes and the editor's");
        lm_mod_free(&m);
        (void) sha2;
    }
    /* ---- the mod format's checks ---- */
    {
        LmMod m;
        const char *bad[] = {
            "{\"format\": \"other\", \"levels\": []}",
            "{\"format\": \"bcmod/1\", \"levels\": [{\"level\": 70, \"original_sha1\": \"a9993e364706816aba3e25717850c26c9cd0d89d\", \"changes\": []}]}",
            "{\"format\": \"bcmod/1\", \"levels\": [{\"level\": 1, \"original_sha1\": \"xyz\", \"changes\": []}]}",
            "{\"format\": \"bcmod/1\", \"levels\": [{\"level\": 1, \"original_sha1\": \"a9993e364706816aba3e25717850c26c9cd0d89d\", "
            "\"changes\": [{\"op\": \"set\", \"kind\": \"object\", \"index\": 0, \"fields\": {\"colour\": 1}}]}]}",
            "{\"format\": \"bcmod/1\", \"levels\": [{\"level\": 1, \"original_sha1\": \"a9993e364706816aba3e25717850c26c9cd0d89d\", "
            "\"changes\": [{\"op\": \"add\", \"kind\": \"rdu\", \"index\": 0}]}]}",
            "{\"format\": \"bcmod/1\", \"levels\": [] } x",
            "{\"format\": \"bcmod/1\", \"levels\": [{\"level\": 1, \"original_sha1\": \"a9993e364706816aba3e25717850c26c9cd0d89d\", "
            "\"changes\": [{\"op\": \"set\", \"kind\": \"rdu\", \"index\": 0, \"fields\": {\"x\": 1.5}}]}]}",
        };
        int refused = 0;
        for (const char *t : bad) refused += lm_mod_parse(t, strlen(t), &m, err, sizeof err) != 0;
        check(refused == int(sizeof bad / sizeof bad[0]),
              "mod: wrong format, level, hash, field, an add without copy_of, trailing text, a fraction: refused");
        const char *good = "\xEF\xBB\xBF{\"format\":\"bcmod/1\",\"name\":\"caf\\u00e9\",\"extra\":[1,{\"a\":null}],"
                           "\"levels\":[{\"level\":0,\"original_sha1\":\"A9993E364706816ABA3E25717850C26C9CD0D89D\","
                           "\"changes\":[{\"op\":\"delete\",\"kind\":\"tnt\",\"index\":2}]}]}";
        check(lm_mod_parse(good, strlen(good), &m, err, sizeof err) == 0 && !strcmp(m.name, "caf\xC3\xA9") &&
                  !strcmp(m.levels[0].original_sha1, "a9993e364706816aba3e25717850c26c9cd0d89d") &&
                  m.levels[0].changes[0].op == LM_OP_DELETE,
              "mod: a BOM, \\u escapes, unknown keys, upper-case hex: accepted");
        lm_mod_free(&m);
    }
    lm_free(lv);
    /* ---- the gizmo's math ---- */
    {
        viewer::FlyCam cam;
        cam.pos = {100, 400, -300};
        cam.yaw = 30, cam.pitch = -35;
        double view[4][4], proj[4][4] = {};
        cam.view(view);
        const double f = 1.0 / std::tan(22.5 * 3.14159265358979 / 180.0), n = 10, fa = 10000, aspect = 4.0 / 3.0;
        proj[0][0] = f / aspect, proj[1][1] = f, proj[2][2] = (n + fa) / (n - fa), proj[2][3] = -1,
        proj[3][2] = 2 * n * fa / (n - fa);
        const viewer::Vec target = {220, 10, -80};
        double sx, sy;
        const bool on = viewer::project(view, proj, target, 0, 0, 640, 480, sx, sy);
        edit::V3 o, d;
        const bool ray = edit::pickRay(view, proj, 0, 0, 640, 480, sx, sy, o, d);
        double x = 0, z = 0;
        check(on && ray && edit::rayPlaneY(o, d, 10, x, z) && std::fabs(x - 220) < 0.05 && std::fabs(z + 80) < 0.05,
              "gizmo: the ray under a point's picture meets its ground plane at the point");
        check(std::fabs(o.x - 100) < 1e-6 && std::fabs(o.y - 400) < 1e-6 && d.y < 0,
              "gizmo: the ray starts at the camera and points away from it");
        double t = 0;
        check(edit::rayAxis(o, d, {220, 10, -80}, {1, 0, 0}, t) && std::fabs(t) < 0.05,
              "gizmo: the nearest point of an axis through the point is the point");
        check(!edit::rayPlaneY(o, {d.x, 0, d.z}, 10, x, z) && !edit::rayPlaneY(o, d, 900, x, z),
              "gizmo: no ground point for a level ray or a plane behind the camera");
        check(edit::headingFromDegrees(90) == 0x400 && edit::headingFromDegrees(-90) == 0xC00 &&
                  edit::headingFromDegrees(360) == 0 && std::fabs(edit::degreesFromHeading(0x800) - 180) < 1e-9,
              "gizmo: headings (0 = +z, 90 = +x, 12 bits)");
        check(edit::snapTo(1923, 16) == 1920 && edit::snapTo(1923.4, 0) == 1923 && edit::snapTo(-23, 16) == -16,
              "gizmo: the grid");
    }
    geoTests();
}
