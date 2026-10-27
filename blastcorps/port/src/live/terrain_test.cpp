/* bc_cameratest.exe's tests of the terrain tools (terrain.cpp), the editor's
 * steps and mod files with geometry and textures (level_edit.cpp), and the
 * texture codec (src/load/tex_codec.c, the twin of bcfiles texture.py / png.py),
 * all on synthetic data built here (nothing from the ROM).
 *
 * `bc_cameratest --texcodec DIR` runs the codec over DIR/cases.txt instead
 * (port/tools/texcodec_cross.py: PNGs and slots made by the Python twin, the
 * outputs compared byte for byte). */
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "level_edit.h"
#include "terrain.h"

extern "C" {
#include "load/tex_codec.h"
}

using namespace live;
using live::terrain::P3;

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

/* a level with an N x N grid of 50-unit quads at y 0 (x, z 0 .. 50N): one sub-list (Rare's G_CULLDL prologue)
 * called from fixed list 0, the first half of the quads drawn with texture 7, the rest with texture 9 (32 x 32
 * RGBA16); the same triangles as the floor (a 1 x 1 ground grid); no walls; a driver spawn and an RDU on the grid */
const int kGrid = 4, kStep = 50;
int gridTri(int qx, int qz, int k) {
    return 2 * (qz * kGrid + qx) + k;
}
/* walls: also a wall 100 high standing on the grid's edge z 0, x 0 .. 50 (two triangles in grid A, heading 0,
 * side 1, fail 2): its foot is the first drawn and floor triangle's edge; walls 2: and a wall triangle on the far
 * edge z 200 from x 100 to 200 that isn't welded to the floor (its foot spans two floor edges, as Rare's walls
 * along a road often do) */
Bytes makeGridLevel(int walls = 0) {
    std::vector<Bytes> sec(31);
    auto S = [&](unsigned slot) -> Bytes & {
        for (int i = 0; i < 31; i++)
            if (kChain[i] == slot) return sec[i];
        return sec[0];
    };
    struct T {
        int v[9];
    };
    std::vector<T> tris;
    for (int qz = 0; qz < kGrid; qz++)
        for (int qx = 0; qx < kGrid; qx++) {
            const int x0 = qx * kStep, z0 = qz * kStep, x1 = x0 + kStep, z1 = z0 + kStep;
            tris.push_back({{x0, 0, z0, x1, 0, z0, x1, 0, z1}});
            tris.push_back({{x0, 0, z0, x1, 0, z1, x0, 0, z1}});
        }
    put32(S(0x30), uint32_t(4 + 0x14 * tris.size()));
    for (const T &t : tris) {
        for (int k = 0; k < 9; k++) put16(S(0x30), t.v[k]);
        S(0x30).push_back(1), S(0x30).push_back(0);
    }
    if (walls) {
        /* (walls 3: the first wall's foot corner at (51, 1, 0), a unit off the floor's (50, 0, 0): Rare's way; walls 4:
         * also a wall whose foot crosses the far one's at the floor's corner (150, 0, 200), along x 150) */
        const int wv[4][9] = {{0, 0, 0, walls >= 3 ? 51 : 50, walls >= 3 ? 1 : 0, 0, 50, 100, 0}, {0, 0, 0, 50, 100, 0, 0, 100, 0},
                              {100, 0, 200, 200, 0, 200, 150, 100, 200}, {150, 0, 150, 150, 0, 250, 150, 100, 200}};
        const int nw = walls >= 4 ? 4 : walls >= 2 ? 3 : 2;
        put32(S(0x6C), uint32_t(4 + 0x16 * nw));
        for (int i = 0; i < nw; i++) {
            for (int k = 0; k < 9; k++) put16(S(0x6C), wv[i][k]);
            put16(S(0x6C), 0), S(0x6C).push_back(1), S(0x6C).push_back(2);
        }
    } else {
        put32(S(0x6C), 4);
    }
    put32(S(0x70), 4);
    put16(S(0x48), 0), put16(S(0x48), 10);
    const int ext = kGrid * kStep;
    for (int v : {-50, -50, ext + 50, ext + 50}) put16(S(0x4C), v);
    /* the driver at the grid's middle, an RDU at (25, 0, 25) */
    S(0x50).push_back(0), put16(S(0x50), ext / 2), put16(S(0x50), 0), put16(S(0x50), ext / 2), put16(S(0x50), 0);
    S(0x54).push_back(0), put16(S(0x54), 0), put16(S(0x54), 0), put16(S(0x54), 0), put16(S(0x54), 0);
    put16(S(0x34), 25), put16(S(0x34), 3), put16(S(0x34), 25);
    S(0x60).push_back(0xFF);
    put32(S(0x74), 0);
    Bytes verts;
    auto vtx = [&](int x, int y, int z, int s, int t) {
        put16(verts, x), put16(verts, y), put16(verts, z), put16(verts, 0), put16(verts, s), put16(verts, t);
        verts.push_back(200), verts.push_back(180), verts.push_back(160), verts.push_back(255);
    };
    for (int z : {0, ext})
        for (int x : {0, ext})
            for (int k = 0; k < 2; k++) vtx(x, 0, z, 0, 0);   /* the box (Rare's corner order: y, then x, then z) */
    Bytes dl;
    auto cmd = [&](uint32_t w0, uint32_t w1) { put32(dl, w0), put32(dl, w1); };
    cmd(0xE7000000u, 0);                                      /* 0x00: the prefix */
    cmd(0x04700080u, 0x08000000u), cmd(0xBE000000u, 0x140);  /* 0x08: the sub-list, its box */
    cmd(0xBB000001u, 0xFFFFFFFFu);                            /* G_TEXTURE on */
    cmd(0xF5100000u, 0), cmd(0xF2000000u, 0x0007C07Cu);       /* tile 0: RGBA16, 32 x 32 */
    cmd(0xFD100000u, 7);
    int vi = 8;
    for (int q = 0; q < kGrid * kGrid; q++) {
        if (q == kGrid * kGrid / 2) cmd(0xFD100000u, 9);
        const int qx = q % kGrid, qz = q / kGrid, x0 = qx * kStep, z0 = qz * kStep, x1 = x0 + kStep, z1 = z0 + kStep;
        vtx(x0, 0, z0, x0 * 8, z0 * 8), vtx(x1, 0, z0, x1 * 8, z0 * 8), vtx(x1, 0, z1, x1 * 8, z1 * 8),
            vtx(x0, 0, z1, x0 * 8, z1 * 8);
        cmd(0x04300040u, 0x08000000u | uint32_t(vi * 16));
        cmd(0xBF000000u, 0x00000A14u), cmd(0xBF000000u, 0x0000141Eu);
        vi += 4;
    }
    cmd(0xB8000000u, 0);
    const uint32_t fixed0 = uint32_t(dl.size());
    cmd(0x06000000u, 8), cmd(0xB8000000u, 0);                 /* fixed 0 calls the sub-list */
    const uint32_t fixed1 = uint32_t(dl.size());
    cmd(0xB8000000u, 0);
    const uint32_t fixed2 = uint32_t(dl.size());
    cmd(0xB8000000u, 0);
    const uint32_t fixed3 = uint32_t(dl.size());
    cmd(0xB8000000u, 0);
    Bytes out(0xC8, 0);
    const int h[12] = {1, 1, 1000, 1000, 1, 1, 1000, 1000, 1, 1, 1000, 1000};
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
    const uint32_t rel[8] = {fixed0, fixed0, 0, 8, fixed0, fixed1, fixed2, fixed3};
    for (int i = 0; i < 8; i++) w32(dls[i], base + rel[i]);
    w32(0x84, base + uint32_t(dl.size()));
    return out;
}

/* the corner of the model at (x, z) (y any) */
int posAt(const terrain::Model &m, int x, int z) {
    for (size_t i = 0; i < m.positions().size(); i++)
        if (m.positions()[i].cur.x == x && m.positions()[i].cur.z == z) return int(i);
    return -1;
}

/* the edited file's y of the vertex at (x, z) that the first G_VTX after its list's box loads (-99999: none) */
int fileY(const Bytes &f, int x, int z) {
    const uint32_t vend = get32(f, 0x20);
    for (uint32_t a = 0xC8 + 8 * 16; a + 16 <= vend; a += 16)
        if (get16s(f, a) == x && get16s(f, a + 4) == z) return get16s(f, a + 2);
    return -99999;
}

/* T-junctions in a level file: (set, corner) pairs where a corner of any drawn, floor or wall triangle lies on an
 * edge of a triangle of `set` (within a map unit of it, strictly between its ends); -1: the file doesn't open */
int tJunctions(const Bytes &f, int set, std::string *first = nullptr) {
    char e[300];
    LgMesh *m = lg_mesh_open(f.data(), f.size(), e, sizeof e);
    if (m == nullptr) return -1;
    std::vector<P3> pts;
    for (int s = 0; s < 4; s++)
        for (int i = 0; i < lg_mesh_count(m, s); i++)
            for (int k = 0; k < 3; k++) {
                const LgTri *t = lg_mesh_tri(m, s, i);
                P3 p;
                p.x = t->p[k][0], p.y = t->p[k][1], p.z = t->p[k][2];
                if (std::find(pts.begin(), pts.end(), p) == pts.end()) pts.push_back(p);
            }
    int n = 0;
    for (int i = 0; i < lg_mesh_count(m, set); i++) {
        const LgTri *t = lg_mesh_tri(m, set, i);
        for (int k = 0; k < 3; k++) {
            const double ax = t->p[k][0], ay = t->p[k][1], az = t->p[k][2];
            const double bx = t->p[(k + 1) % 3][0], by = t->p[(k + 1) % 3][1], bz = t->p[(k + 1) % 3][2];
            const double ex = bx - ax, ey = by - ay, ez = bz - az, l2 = ex * ex + ey * ey + ez * ez;
            if (l2 < 25) continue;
            for (const P3 &p : pts) {
                const double tt = ((p.x - ax) * ex + (p.y - ay) * ey + (p.z - az) * ez) / l2, len = std::sqrt(l2);
                if (tt * len <= 2.0 || (1 - tt) * len <= 2.0) continue;   /* (more than 2 from the ends: terrain.cpp) */
                if ((p.x == ax && p.y == ay && p.z == az) || (p.x == bx && p.y == by && p.z == bz)) continue;
                const double qx = ax + ex * tt - p.x, qy = ay + ey * tt - p.y, qz = az + ez * tt - p.z;
                if (qx * qx + qy * qy + qz * qz <= 1.0) {
                    if (n == 0 && first) {
                        char b[160];
                        snprintf(b, sizeof b, "(%d, %d, %d) on (%g, %g, %g)-(%g, %g, %g)", p.x, p.y, p.z, ax, ay, az, bx, by, bz);
                        *first = b;
                    }
                    n++;
                }
            }
        }
    }
    lg_mesh_free(m);
    return n;
}

bool applyEdit(const Bytes &lvl, const LgEdit &e, Bytes &out, std::string &err) {
    uint8_t *o = nullptr;
    size_t on = 0;
    char eb[400];
    if (lg_apply(lvl.data(), lvl.size(), lvl.data(), lvl.size(), &e, 3, &o, &on, eb, sizeof eb)) {
        err = eb;
        return false;
    }
    out.assign(o, o + on);
    free(o);
    return true;
}

void terrainTests() {
    const Bytes lvl = makeGridLevel();
    std::string err;
    terrain::Model m;
    check(m.open(lvl.data(), lvl.size(), err), ("terrain: the model opens a synthetic grid level " + err).c_str());
    if (!m.isOpen()) return;
    const int nc = (kGrid + 1) * (kGrid + 1);
    check(int(m.positions().size()) == nc, "terrain: corners are welded by position (25 of the 4 x 4 grid)");
    check(int(m.drawn().size()) == 2 * kGrid * kGrid && int(m.floor().size()) == 2 * kGrid * kGrid,
          "terrain: 32 drawn and 32 floor triangles");
    const LgTri *t0 = m.stateOf(0), *tl = m.stateOf(2 * kGrid * kGrid - 1);
    check(t0 && t0->tex == 7 && tl && tl->tex == 9 && t0->w == 32 && t0->h == 32 && t0->siz == 16 && t0->fmt == 0 &&
              t0->timg == 0 && tl->timg == 1,
          "terrain: each drawn triangle knows its texture (id, run, RGBA16 32 x 32)");
    /* brushes */
    terrain::Brush b;
    b.radius = 60, b.strength = 10, b.falloff = 0;
    std::unordered_map<int, double> cur;
    int skipped = 0;
    m.brush(terrain::B_RAISE, b, 100, 100, 0, cur, skipped);
    int raised = 0;
    for (const auto &kv : cur) raised += std::lround(kv.second) == 10;
    /* within 60 of (100, 100): the centre, 4 at 50, 4 at 70.7 (out) */
    check(cur.size() == 5 && raised == 5 && skipped == 0, "terrain: raise, a hard edge: the corners within the radius, +10");
    b.falloff = 1, b.radius = 100;
    cur.clear();
    m.brush(terrain::B_RAISE, b, 100, 100, 0, cur, skipped);
    const int c = posAt(m, 100, 100), e = posAt(m, 150, 100);
    check(std::fabs(cur[c] - 10) < 1e-9 && std::fabs(cur[e] - 5) < 1e-9,
          "terrain: raise with falloff 1: 10 at the centre, 5 halfway out (smoothstep)");
    m.brush(terrain::B_LOWER, b, 100, 100, 0, cur, skipped);
    check(std::fabs(cur[c]) < 1e-9 && std::fabs(cur[e]) < 1e-9, "terrain: lower undoes raise");
    /* a stroke becomes a MOVE step; applied, exported, lg_apply'd */
    cur.clear();
    b.falloff = 0, b.radius = 10, b.strength = 40;
    m.brush(terrain::B_RAISE, b, 100, 100, 0, cur, skipped);
    terrain::Step st = m.strokeStep(cur, "raise");
    check(st.moves.size() == 1 && st.moves[0].first == c && st.moves[0].second.y == 40, "terrain: a stroke's step: one corner to 40");
    check(m.apply(st, err) && m.positions()[c].cur.y == 40, "terrain: the step applies");
    /* smooth: the spike goes towards its neighbours' mean */
    cur.clear();
    b.strength = 1;   /* (smooth / flatten: the fraction of the way) */
    m.brush(terrain::B_SMOOTH, b, 100, 100, 0, cur, skipped);
    check(std::fabs(cur[c]) < 1e-9, "terrain: smooth (all the way) puts the spike on its neighbours' mean (0)");
    cur.clear();
    b.radius = 1000;
    m.brush(terrain::B_FLATTEN, b, 100, 100, 25, cur, skipped);
    bool all25 = cur.size() == size_t(nc);
    for (const auto &kv : cur) all25 = all25 && std::fabs(kv.second - 25) < 1e-9;
    check(all25, "terrain: flatten to a picked height (25): every corner in reach");
    /* ramp along x at z 100 from y 0 to 100: x 0 -> 0, x 100 -> 50, x 200 -> 100 */
    terrain::Brush rb;
    rb.radius = 30, rb.strength = 1, rb.falloff = 0;
    const double ra[3] = {0, 0, 100}, rbp[3] = {200, 100, 100};
    terrain::Step rs = m.ramp(ra, rbp, rb, skipped);
    int y0 = -1, y1 = -1, y2 = -1;
    for (const auto &mv : rs.moves) {
        if (mv.second.x == 50 && mv.second.z == 100) y0 = mv.second.y;
        if (mv.second.x == 100 && mv.second.z == 100) y1 = mv.second.y;
        if (mv.second.x == 200 && mv.second.z == 100) y2 = mv.second.y;
    }
    check(y0 == 25 && y2 == 100 && y1 == 40 + 10 /* (40 -> 50: the corner at 40 goes to 50) */ && rs.moves.size() == 4,
          "terrain: ramp: the corners near the segment go to its height there (25, 50, 100)");
    check(m.apply(rs, err), "terrain: the ramp's step applies");
    /* the edit: moves of the level's corners */
    LgEdit ed;
    check(m.exportEdit(ed) && ed.nmove == 4 && ed.nrem[0] == 0 && ed.nrender == 0, "terrain: the edit holds 4 moves");
    Bytes r;
    check(applyEdit(lvl, ed, r, err), ("terrain: lg_apply takes the edit " + err).c_str());
    if (!r.empty()) check(fileY(r, 200, 100) == 100 && fileY(r, 150, 100) == 75, "terrain: the edited level's vertices moved");
    /* the floor follows: heightAt over the floor triangles */
    double hy = 0;
    check(m.heightAt(125, 100, hy) && std::fabs(hy - 62.5) < 1e-6, "terrain: the floor's height between moved corners (62.5)");
    /* import(export) gives the same edit */
    {
        terrain::Model m2;
        m2.open(lvl.data(), lvl.size(), err);
        LgEdit e2;
        check(m2.import(ed, err) && m2.exportEdit(e2) && e2.nmove == ed.nmove &&
                  !memcmp(e2.move, ed.move, sizeof(LgMove) * size_t(ed.nmove)),
              "terrain: a model that imports the edit exports the same one");
        lg_edit_free(&e2);
    }
    lg_edit_free(&ed);
    /* subdivide: one drawn triangle and its floor twin split four ways; the two neighbours on its inner edges (and
     * their floor twins) split two ways along them, so no corner is left on a whole edge (no T-junction) */
    terrain::Step sd;
    sd.kind = terrain::ST_SUBDIVIDE;
    sd.tris = {gridTri(0, 0, 0)};
    const size_t np = m.positions().size();
    check(m.apply(sd, err), ("terrain: subdivide " + err).c_str());
    check(m.positions().size() == np + 3 && m.drawn().size() == size_t(2 * kGrid * kGrid + 8) &&
              m.drawn()[gridTri(0, 0, 0)].removed && m.drawn()[gridTri(0, 0, 1)].removed &&
              m.drawn()[gridTri(1, 0, 1)].removed && m.floor().size() == size_t(2 * kGrid * kGrid + 8),
          "terrain: subdivide makes 3 midpoints, 4 + 2 + 2 drawn and floor triangles (the neighbours split in two)");
    check(m.lastInfo().selected == 1 && m.lastInfo().neighbours == 2 && m.lastInfo().floors == 3 && m.lastInfo().walls == 0 &&
              m.lastSplit().size() == 4 && m.lastSplit()[0] == 2 * kGrid * kGrid,
          "terrain: ... the split's counts; the selected triangle's 4 pieces are what it selects next");
    /* every midpoint is free now (no corner follows a whole edge) */
    const int md = posAt(m, 25, 25), mf = posAt(m, 25, 0), mr = posAt(m, 50, 25);
    check(md >= 0 && mf >= 0 && mr >= 0 && m.movable(md) && m.movable(mf) && m.movable(mr),
          "terrain: every midpoint can move (both sides of its edge hold it)");
    check(m.seams().empty(), "terrain: no seam left (no corner on another triangle's edge)");
    check(m.exportEdit(ed) && ed.nrem[0] == 3 && ed.nrender == 8 && ed.nrem[1] == 3 && ed.nground == 8 &&
              ed.render[0].after == gridTri(0, 0, 0),
          "terrain: the edit removes the 3 triangles and adds 8 after them (and the floor's)");
    check(applyEdit(lvl, ed, r, err), ("terrain: lg_apply takes the subdivision " + err).c_str());
    {
        std::string w0, w1;
        const int j0 = tJunctions(r, LG_RENDER, &w0), j1 = tJunctions(r, LG_GROUND, &w1);
        check(j0 == 0 && j1 == 0, ("terrain: the edited level has no T-junction (drawn, floor) " + w0 + w1).c_str());
    }
    lg_edit_free(&ed);
    /* a piece split again (nested): its neighbours - the middle piece, a half of the diagonal's neighbour - follow */
    sd.tris = {m.lastSplit()[0]};
    check(m.apply(sd, err) && m.lastInfo().selected == 1 && m.lastInfo().neighbours == 2 && m.seams().empty(),
          ("terrain: a piece split again: its two inner neighbours split along it, no seam " + err).c_str());
    /* the midpoint moved by a brush: the neighbours' halves follow it (it is their corner too) */
    {
        terrain::Step mv;
        mv.kind = terrain::ST_MOVE;
        P3 v = m.positions()[md].cur;
        v.y += 30;
        mv.moves = {{md, v}};
        check(m.apply(mv, err), "terrain: the diagonal's midpoint raised");
        check(m.exportEdit(ed) && applyEdit(lvl, ed, r, err), ("terrain: lg_apply takes it " + err).c_str());
        std::string w0, w1;
        const int j0 = tJunctions(r, LG_RENDER, &w0), j1 = tJunctions(r, LG_GROUND, &w1);
        check(j0 == 0 && j1 == 0, ("terrain: raised, still no T-junction (drawn, floor) " + w0 + w1).c_str());
        lg_edit_free(&ed);
        mv.moves = {{md, m.positions()[md].orig}};
        m.apply(mv, err);
    }
    /* paint: a triangle of texture 7's run joins texture 9's (planar coordinates) */
    terrain::Step pt;
    pt.kind = terrain::ST_PAINT;
    const int victim = gridTri(1, 0, 0), run9 = gridTri(0, kGrid / 2, 0);
    pt.tris = {victim};
    pt.run = run9;
    pt.planar = true;
    check(m.apply(pt, err), ("terrain: paint " + err).c_str());
    const terrain::DrawTri &np2 = m.drawn().back();
    check(m.drawn()[victim].removed && np2.after == run9 && m.stateOf(int(m.drawn().size()) - 1)->tex == 9 &&
              np2.st[0][0] == 50 * 8 && np2.st[0][1] == 0,
          "terrain: the painted triangle joins the run of texture 9, coordinates projected at the run's density");
    terrain::Step rt;
    rt.kind = terrain::ST_RETEX;
    rt.piece = m.stateOf(0)->piece, rt.k = 0, rt.tex = 11;
    check(m.apply(rt, err), "terrain: a run's texture id changed (settimg)");
    check(m.exportEdit(ed) && ed.nsettimg == 1 && ed.settimg[0].tex == 11, "terrain: ... in the edit");
    check(applyEdit(lvl, ed, r, err), ("terrain: lg_apply takes subdivisions, paint and settimg " + err).c_str());
    {
        terrain::Model m2;
        m2.open(lvl.data(), lvl.size(), err);
        LgEdit e2;
        Bytes r2;
        const bool ok = m2.import(ed, err) && m2.exportEdit(e2) && applyEdit(lvl, e2, r2, err);
        check(ok && r2 == r, "terrain: import then export: the same edited level (adds bound to the corners they share)");
        lg_edit_free(&e2);
    }
    lg_edit_free(&ed);
    /* refusals */
    terrain::Step bad;
    bad.kind = terrain::ST_PAINT;
    bad.tris = {victim};   /* (removed) */
    bad.run = run9;
    check(!m.apply(bad, err), "terrain: painting a triangle that's gone is refused");
    bad.kind = terrain::ST_SUBDIVIDE;
    check(!m.apply(bad, err), "terrain: splitting a triangle that's gone is refused");
    /* picking */
    const double o[3] = {110, 500, 120}, d[3] = {0, -1, 0};
    double tt, hit[3];
    const int pk = m.pick(o, d, tt, hit);
    check(pk >= 0 && std::fabs(hit[0] - 110) < 1e-6 && std::fabs(hit[2] - 120) < 1e-6, "terrain: a ray from above picks a triangle");
    /* weights */
    terrain::Brush wb;
    wb.radius = 100, wb.falloff = 0.5;
    check(terrain::brushWeight(wb, 50) == 1.0 && terrain::brushWeight(wb, 100) == 0.0 &&
              std::fabs(terrain::brushWeight(wb, 75) - 0.5) < 1e-9,
          "terrain: brush weights (flat inside, smoothstep to the edge)");
}

/* walls follow splits: a wall standing on a split edge is split along it (the new corner is the floor's), so a
 * brush that moves the corner moves the wall's foot with the floor; and SEAL closes an old mod's seams */
void wallSeamTests() {
    const Bytes lvl = makeGridLevel(true);
    std::string err;
    terrain::Model m;
    check(m.open(lvl.data(), lvl.size(), err) && m.walls().size() == 2 && m.walls()[0].heading == 0 &&
              m.walls()[0].side == 1 && m.walls()[0].fail == 2 && m.walls()[0].origId == 0,
          ("seam: the grid level with a wall opens (2 wall triangles, heading 0, side 1, fail 2) " + err).c_str());
    if (!m.isOpen()) return;
    terrain::Step sd;
    sd.kind = terrain::ST_SUBDIVIDE;
    sd.tris = {gridTri(0, 0, 0)};
    check(m.apply(sd, err) && m.lastInfo().walls == 1 && m.walls().size() == 4 && m.walls()[0].removed &&
              !m.walls()[1].removed,
          ("seam: the wall triangle on the split edge is split in two along it (the other kept) " + err).c_str());
    const int foot = posAt(m, 25, 0);
    bool shared = false;
    for (size_t i = 2; i < m.walls().size(); i++)
        for (int k = 0; k < 3; k++) shared = shared || m.walls()[i].pos[k] == foot;
    check(foot >= 0 && shared, "seam: the wall's new corner is the floor's midpoint (one corner, welded)");
    /* a brush raises the midpoint: the floor and the wall's foot go up together */
    terrain::Brush b;
    b.radius = 10, b.strength = 20, b.falloff = 0;
    std::unordered_map<int, double> cur;
    int skipped = 0;
    m.brush(terrain::B_RAISE, b, 25, 0, 0, cur, skipped);
    check(m.apply(m.strokeStep(cur, "raise"), err) && m.positions()[foot].cur.y == 20, "seam: the brush raises the midpoint (y 20)");
    LgEdit ed;
    bool wallUp = false;
    if (m.exportEdit(ed)) {
        for (int i = 0; i < ed.nwall[0]; i++)
            for (int k = 0; k < 3; k++)
                wallUp = wallUp || (ed.wall[0][i].v[3 * k] == 25 && ed.wall[0][i].v[3 * k + 1] == 20 && ed.wall[0][i].v[3 * k + 2] == 0 &&
                                    ed.wall[0][i].a == 0 && ed.wall[0][i].b == 1 && ed.wall[0][i].c == 2);
        check(ed.nrem[2] == 1 && ed.rem[2][0] == 0 && ed.nwall[0] == 2 && wallUp,
              "seam: the edit removes the wall triangle and adds its two halves (heading, side, fail kept; the foot at y 20)");
    }
    Bytes r;
    check(applyEdit(lvl, ed, r, err), ("seam: lg_apply takes it " + err).c_str());
    lg_edit_free(&ed);
    if (!r.empty()) {
        char e[300];
        LgMesh *g = lg_mesh_open(r.data(), r.size(), e, sizeof e);
        bool found = false;
        if (g) {
            for (int i = 0; i < lg_mesh_count(g, LG_WALLS_A); i++) {
                const LgTri *t = lg_mesh_tri(g, LG_WALLS_A, i);
                for (int k = 0; k < 3; k++)
                    found = found || (t->p[k][0] == 25 && t->p[k][1] == 20 && t->p[k][2] == 0 && t->flag == 0 && t->b == 1 && t->c == 2);
            }
            check(lg_mesh_count(g, LG_WALLS_A) == 3 && found, "seam: the edited level's collision grid A holds the 3 wall triangles, "
                                                              "one with its foot on the raised corner");
            lg_mesh_free(g);
        }
        std::string w;
        const int t0 = tJunctions(r, LG_RENDER, &w), t1 = tJunctions(r, LG_GROUND, &w), t2 = tJunctions(r, LG_WALLS_A, &w);
        check(t0 == 0 && t1 == 0 && t2 == 0, ("seam: no T-junction in the drawn, floor or wall triangles " + w).c_str());
    }
    /* an old mod's split (T0 four ways only: corners on the neighbours' and the wall's whole edges) closed by SEAL */
    {
        terrain::Model s;
        s.open(lvl.data(), lvl.size(), err);
        LgEdit old;
        memset(&old, 0, sizeof old);
        int rr[1] = {0}, rg[1] = {0};
        old.nrem[0] = 1, old.rem[0] = rr, old.nrem[1] = 1, old.rem[1] = rg;
        const int P[6][3] = {{0, 0, 0}, {50, 0, 0}, {50, 0, 50}, {25, 0, 0}, {50, 0, 25}, {25, 0, 25}};
        const int parts[4][3] = {{0, 3, 5}, {3, 1, 4}, {5, 4, 2}, {3, 4, 5}};
        LgAddRender ar[4];
        LgAddTri ag[4];
        memset(ar, 0, sizeof ar), memset(ag, 0, sizeof ag);
        for (int q = 0; q < 4; q++) {
            ar[q].after = 0;
            ag[q].a = 1;
            for (int k = 0; k < 3; k++) {
                const int *p = P[parts[q][k]];
                ar[q].v[k][0] = int16_t(p[0]), ar[q].v[k][1] = int16_t(p[1]), ar[q].v[k][2] = int16_t(p[2]);
                ar[q].v[k][3] = int16_t(p[0] * 8), ar[q].v[k][4] = int16_t(p[2] * 8);
                for (int c = 5; c < 9; c++) ar[q].v[k][c] = 255;
                ag[q].v[3 * k] = int16_t(p[0]), ag[q].v[3 * k + 1] = int16_t(p[1]), ag[q].v[3 * k + 2] = int16_t(p[2]);
            }
        }
        old.nrender = 4, old.render = ar, old.nground = 4, old.ground = ag;
        check(s.import(old, err) && s.seams().size() == 3, ("seam: an old split imported: 3 corners on whole edges (two "
                                                             "neighbours', the wall's) " + err).c_str());
        Bytes before;
        applyEdit(lvl, old, before, err);
        check(tJunctions(before, LG_RENDER) > 0, "seam: ... T-junctions in its level (the check sees them)");
        terrain::Step seal;
        seal.kind = terrain::ST_SEAL;
        check(s.apply(seal, err) && s.seams().empty() && s.lastInfo().neighbours == 2 && s.lastInfo().walls == 1,
              ("seam: SEAL splits the two neighbours, their floor twins and the wall at those corners " + err).c_str());
        LgEdit e2;
        Bytes r2;
        check(s.exportEdit(e2) && applyEdit(lvl, e2, r2, err) && tJunctions(r2, LG_RENDER) == 0 && tJunctions(r2, LG_GROUND) == 0 &&
                  tJunctions(r2, LG_WALLS_A) == 0,
              ("seam: ... and its level has no T-junction left " + err).c_str());
        lg_edit_free(&e2);
        check(!s.apply(seal, err), "seam: SEAL with no seam left is refused (nothing to do)");
    }
    /* a wall not welded to the floor: its foot spans two floor edges; the midpoint of one lies on it, so it is split
     * there too (the corner shared), and a brush moves its foot with the floor */
    {
        const Bytes l2 = makeGridLevel(2);
        terrain::Model g;
        check(g.open(l2.data(), l2.size(), err) && g.walls().size() == 3, "seam: a level with a wall along two floor edges");
        terrain::Step s2;
        s2.kind = terrain::ST_SUBDIVIDE;
        s2.tris = {gridTri(2, 3, 1)};   /* (its edge z 200, x 100 .. 150 is under the wall's foot) */
        check(g.apply(s2, err) && g.lastInfo().walls == 2 && g.walls()[2].removed && g.walls().size() == 7 && g.seams().empty(),
              ("seam: the wall whose foot the midpoint (125, 0, 200) lies on is split at it, then its new edge at the "
               "floor's corner (150, 0, 200) on it (no seam) " + err).c_str());
        const int mid = posAt(g, 125, 200);
        terrain::Brush b2;
        b2.radius = 10, b2.strength = 15, b2.falloff = 0;
        std::unordered_map<int, double> c2;
        int sk = 0;
        g.brush(terrain::B_RAISE, b2, 125, 200, 0, c2, sk);
        LgEdit e3;
        Bytes r3;
        bool up = false;
        if (g.apply(g.strokeStep(c2, "raise"), err) && g.exportEdit(e3)) {
            for (int i = 0; i < e3.nwall[0]; i++)
                for (int k = 0; k < 3; k++)
                    up = up || (e3.wall[0][i].v[3 * k] == 125 && e3.wall[0][i].v[3 * k + 1] == 15 && e3.wall[0][i].v[3 * k + 2] == 200);
            check(mid >= 0 && up && applyEdit(l2, e3, r3, err), ("seam: ... raised, the wall's foot goes up with the floor " + err).c_str());
            lg_edit_free(&e3);
        }
        if (!r3.empty())
            check(tJunctions(l2, LG_WALLS_A) == 1 && tJunctions(r3, LG_WALLS_A) == 0 && tJunctions(r3, LG_RENDER) == 0 &&
                      tJunctions(r3, LG_GROUND) == 0,
                  "seam: no T-junction left (the level's own, the floor corner on the wall's foot, closed on the split edge)");
    }
    /* a neighbour that can't be split (a corner of it protected) keeps its edge: the selected triangle splits around
     * it (here: no protected corner in a synthetic grid; the split of every triangle at once leaves none) */
    {
        terrain::Model a;
        a.open(lvl.data(), lvl.size(), err);
        terrain::Step all;
        all.kind = terrain::ST_SUBDIVIDE;
        for (int i = 0; i < 2 * kGrid * kGrid; i++) all.tris.push_back(i);
        check(a.apply(all, err) && a.lastInfo().selected == 32 && a.lastInfo().neighbours == 0 && a.seams().empty(),
              "seam: every triangle split at once: 32 four-way splits, no neighbour left to split, no seam");
    }
}

/* welds across meshes: corners a unit apart are one corner (a group), and a corner on another mesh's edge follows
 * the edge (the far wall's foot spans two floor edges: the floor's corner (150, 0, 200) lies on it) */
void weldTests() {
    const Bytes lvl = makeGridLevel(3);
    std::string err;
    terrain::Model m;
    check(m.open(lvl.data(), lvl.size(), err), ("weld: the grid level with Rare-style walls opens " + err).c_str());
    if (!m.isOpen()) return;
    const int c50 = posAt(m, 50, 0), c51 = posAt(m, 51, 0), t150 = posAt(m, 150, 200), w100 = posAt(m, 100, 200),
              w200 = posAt(m, 200, 200);
    check(c50 >= 0 && c51 >= 0 && m.groupOf(c50) == m.groupOf(c51) && m.groupMembers(c50).size() == 2,
          "weld: the wall's foot (51, 1, 0) and the floor's corner (50, 0, 0) are one group");
    check(t150 >= 0 && !m.movable(t150) && m.positions()[size_t(t150)].host >= 0 && m.followerCount() == 1,
          "weld: the floor's corner on the far wall's foot follows it (can't move by itself; the only follower)");
    /* a move of the floor's corner moves the wall's foot with it (by the same amount: its unit apart kept) */
    terrain::Step mv;
    mv.kind = terrain::ST_MOVE;
    P3 v = m.positions()[size_t(c50)].cur;
    v.y += 10;
    mv.moves = {{c50, v}};
    check(m.apply(mv, err) && m.positions()[size_t(c51)].cur.y == 11 && m.positions()[size_t(c51)].cur.x == 51 &&
              m.lastWeld().partners == 1 && m.lastWeld().followers == 0,
          ("weld: (50, 0, 0) up 10 takes (51, 1, 0) to (51, 11, 0) (one partner) " + err).c_str());
    /* the wall's foot ends raised 20 and 40: the corner on it goes to 30 (its middle) */
    P3 a = m.positions()[size_t(w100)].cur, b = m.positions()[size_t(w200)].cur;
    a.y = 20, b.y = 40;
    mv.moves = {{w100, a}, {w200, b}};
    check(m.apply(mv, err) && m.positions()[size_t(t150)].cur.y == 30 && m.lastWeld().followers == 1,
          ("weld: the far wall's foot ends up 20 and 40: the floor's corner on it follows to 30 " + err).c_str());
    std::unordered_map<int, double> cur;
    int sk = 0;
    terrain::Brush br;
    br.radius = 10, br.strength = 15, br.falloff = 0;
    m.brush(terrain::B_RAISE, br, 150, 200, 0, cur, sk);
    check(cur.empty(), "weld: a brush on the follower alone moves nothing (it keeps to the wall's foot)");
    LgEdit ed;
    Bytes r;
    check(m.exportEdit(ed) && applyEdit(lvl, ed, r, err), ("weld: lg_apply takes the welded moves " + err).c_str());
    if (!r.empty()) check(fileY(r, 150, 200) == 30, "weld: the edited level holds the corner on the wall's foot at 30");
    lg_edit_free(&ed);
    /* the resolve() the preview uses: the same as apply's */
    {
        terrain::Model q;
        q.open(lvl.data(), lvl.size(), err);
        P3 u = q.positions()[size_t(w200)].cur;
        u.y = 50;
        terrain::WeldInfo wi;
        const auto full = q.resolve({{w200, u}}, &wi);
        bool has150 = false;
        for (const auto &f : full) has150 = has150 || (f.first == t150 && f.second.y == 25);
        check(full.size() == 2 && has150 && wi.followers == 1, "weld: resolve: the far foot's end up 50 takes the corner on it to 25");
    }
    /* a split of the floor triangle on the foot frees the corner (a corner of the wall's pieces now) */
    {
        terrain::Model s;
        s.open(lvl.data(), lvl.size(), err);
        terrain::Step sd;
        sd.kind = terrain::ST_SUBDIVIDE;
        sd.tris = {gridTri(3, 3, 1)};   /* (its edge z 200, x 150 .. 200 is on the foot) */
        check(s.apply(sd, err) && s.movable(t150) && s.followerCount() == 0 && s.seams().empty(),
              ("weld: the wall split at the floor's corner: it is free, no seam " + err).c_str());
    }
    /* crossing edges: the floor's corner (150, 0, 200) on two walls' feet (z 200 and x 150): it follows one; a move
     * that takes the feet apart there splits both at it (a T-junction fixed), and the corner goes to the mean */
    {
        const Bytes l4 = makeGridLevel(4);
        terrain::Model c;
        check(c.open(l4.data(), l4.size(), err) && c.walls().size() == 4, ("cross: a level with two walls' feet crossing " + err).c_str());
        const int p = posAt(c, 150, 200), a = posAt(c, 100, 200), b = posAt(c, 200, 200);
        check(p >= 0 && !c.movable(p) && c.followerCount() == 1, "cross: the corner on both feet follows (one of them)");
        terrain::Step st;
        st.kind = terrain::ST_MOVE;
        P3 va = c.positions()[size_t(a)].cur, vb = c.positions()[size_t(b)].cur;
        va.y = 20, vb.y = 40;
        st.moves = {{a, va}, {b, vb}};
        check(c.apply(st, err) && c.lastWeld().tjunctions == 1 && c.positions()[size_t(p)].cur.y == 15 && c.movable(p) &&
                  c.walls()[2].removed && c.walls()[3].removed && c.walls().size() == 8,
              ("cross: the far foot's ends up 20 and 40 (30 at the corner), the other foot still (0): both walls split "
               "at the corner, which goes to 15 and is free " + err).c_str());
        LgEdit e;
        Bytes r;
        check(c.exportEdit(e) && applyEdit(l4, e, r, err) && tJunctions(r, LG_WALLS_A) == 0,
              ("cross: ... its level takes the edit, no T-junction left in the walls " + err).c_str());
        lg_edit_free(&e);
        /* a move that keeps the feet together there (both lifted the same) splits nothing */
        terrain::Model k;
        k.open(l4.data(), l4.size(), err);
        const int q1 = posAt(k, 150, 150), q2 = posAt(k, 150, 250);
        P3 u1 = k.positions()[size_t(q1)].cur, u2 = k.positions()[size_t(a)].cur, u3 = k.positions()[size_t(b)].cur;
        P3 u4 = q2 >= 0 ? k.positions()[size_t(q2)].cur : u1;
        u1.y = 10, u2.y = 10, u3.y = 10, u4.y = 10;
        st.moves = {{q1, u1}, {a, u2}, {b, u3}, {q2, u4}};
        check(k.apply(st, err) && k.lastWeld().tjunctions == 0 && k.walls().size() == 4,
              ("cross: ... a move that keeps them together there splits nothing " + err).c_str());
    }
}

/* the editor: terrain steps in the change list, undo / redo, the mod file with geometry and textures, and a
 * level opened again from that mod (the save keeps them: geo1's gap) */
void editorTests() {
    const Bytes lvl = makeGridLevel();
    edit::Editor ed;
    check(ed.open(3, lvl.data(), lvl.size()) && ed.terrainOk(), "editor: opens the grid level with its terrain");
    const std::string sha0 = ed.imageSha1();
    terrain::Step st;
    st.kind = terrain::ST_MOVE;
    const int c = posAt(ed.terrain(), 100, 100);
    P3 v = ed.terrain().positions()[c].cur;
    v.y = 30;
    st.moves = {{c, v}};
    st.what = "raise";
    edit::EditStep es;
    es.type = edit::STEP_TERRAIN;
    es.ter = st;
    check(ed.pushStep(es) && ed.imageSha1() != sha0 && ed.terrain().positions()[c].cur.y == 30,
          "editor: a terrain step changes the image");
    check(ed.listN[0] == 2 && ed.listCap[0] == 1000, "editor: the visible lists' budgets are known (profile 1: 1000)");
    check(ed.undo() && ed.imageSha1() == sha0 && ed.redo() && ed.imageSha1() != sha0, "editor: undo / redo a stroke");
    /* records follow the ground: the driver at (100, 100) stands on the raised corner */
    std::vector<LmChange> f = ed.followGround();
    check(f.size() == 1 && f[0].kind == LM_VEHICLE && f[0].value[0] == 30, "editor: the driver's spawn follows the ground up (+30)");
    edit::EditStep fs;
    fs.type = edit::STEP_RECORDS;
    fs.recs = f;
    check(ed.pushStep(fs) && ed.get(LM_VEHICLE, 0, "y") == 30 && ed.followGround().empty(),
          "editor: ... as one step; then nothing more to follow");
    /* a texture slot */
    edit::EditStep ts;
    ts.type = edit::STEP_TEXTURE;
    ts.tex.id = 7, ts.tex.fmt = 0, ts.tex.siz = 16, ts.tex.sha1 = std::string(40, 'a'), ts.tex.png = "iVBORw0KGgo=";
    check(ed.pushStep(ts) && ed.textures().size() == 1, "editor: a texture slot in the change list");
    ed.push(edit::makeSet(LM_RDU, 0, {{lm_field_index(LM_RDU, "x"), 30}}));
    const std::string text = ed.modText("t", "d");
    LmMod m;
    char e[512];
    const bool parsed = lm_mod_parse(text.c_str(), text.size(), &m, e, sizeof e) == 0;
    check(parsed && m.format == 2 && m.nlevels == 1 && m.levels[0].geo && m.levels[0].geo->nmove == 1 &&
              m.ntextures == 1 && m.textures[0].id == 7 && m.levels[0].nchanges == 2,
          "editor: the mod file holds the geometry, the texture slot and the record changes (bcmod/2)");
    if (parsed && m.nlevels == 1) {
        uint8_t *o = nullptr;
        size_t on = 0;
        check(lm_mod_apply(&m.levels[0], lvl.data(), lvl.size(), &o, &on, e, sizeof e) == 0,
              "editor: the mod applies (its edited_sha1 matches)");
        free(o);
        /* opened again from the mod (as bc.exe does when it is loaded), saved again with one more edit */
        std::vector<LmChange> base(m.levels[0].changes, m.levels[0].changes + m.levels[0].nchanges);
        std::vector<edit::TexSlot> bt;
        edit::TexSlot t;
        t.id = m.textures[0].id, t.fmt = m.textures[0].fmt, t.siz = m.textures[0].siz, t.sha1 = m.textures[0].slot_sha1;
        t.png = m.textures[0].png;
        bt.push_back(t);
        edit::Editor e2;
        check(e2.open(3, lvl.data(), lvl.size(), base, "t", m.levels[0].geo, bt) && e2.imageSha1() == ed.imageSha1(),
              "editor: a level opened from the mod is the same file");
        const std::string again = e2.modText("t", "d");
        LmMod m2;
        check(lm_mod_parse(again.c_str(), again.size(), &m2, e, sizeof e) == 0 && m2.levels[0].geo &&
                  m2.levels[0].geo->nmove == 1 && m2.ntextures == 1 &&
                  !strcmp(m2.levels[0].edited_sha1, m.levels[0].edited_sha1),
              "editor: saved again, the mod keeps its geometry and textures (round trip)");
        lm_mod_free(&m2);
        terrain::Step s2;
        s2.kind = terrain::ST_MOVE;
        const int c2 = posAt(e2.terrain(), 0, 0);
        P3 v2 = e2.terrain().positions()[c2].cur;
        v2.y = -20;
        s2.moves = {{c2, v2}};
        edit::EditStep es2;
        es2.type = edit::STEP_TERRAIN;
        es2.ter = s2;
        check(e2.pushStep(es2), "editor: a new stroke over the mod's geometry");
        const std::string third = e2.modText("t", "d");
        check(lm_mod_parse(third.c_str(), third.size(), &m2, e, sizeof e) == 0 && m2.levels[0].geo &&
                  m2.levels[0].geo->nmove == 2,
              "editor: ... saved with the mod's move and the new one");
        lm_mod_free(&m2);
    }
    lm_mod_free(&m);
    /* a new slot of the port's own ("new", "from") survives a write and a read */
    {
        LmMod w;
        memset(&w, 0, sizeof w);
        LmTexture t;
        memset(&t, 0, sizeof t);
        t.id = 741, t.fmt = 0, t.siz = 16, t.isNew = 1, t.from = 1549;
        snprintf(t.slot_sha1, sizeof t.slot_sha1, "da39a3ee5e6b4b0d3255bfef95601890afd80709");
        t.png = const_cast<char *>("iVBORw0KGgo=");
        w.ntextures = 1, w.textures = &t;
        char *txt = lm_mod_write(&w);
        LmMod r;
        check(txt && lm_mod_parse(txt, strlen(txt), &r, e, sizeof e) == 0 && r.format == 2 && r.ntextures == 1 &&
                  r.textures[0].isNew && r.textures[0].from == 1549 && r.textures[0].id == 741,
              "editor: a new texture slot (\"new\", \"from\") is written and read back");
        if (txt) lm_mod_free(&r);
        free(txt);
    }
    /* refusals: the image growing past the port's room */
    edit::Editor e3;
    e3.open(3, lvl.data(), lvl.size());
    terrain::Step big;
    big.kind = terrain::ST_SUBDIVIDE;
    for (int i = 0; i < 2 * kGrid * kGrid; i++) big.tris.push_back(i);
    edit::EditStep bs;
    bs.type = edit::STEP_TERRAIN;
    bs.ter = big;
    check(e3.pushStep(bs), "editor: every triangle split once (fits)");
    const auto t0 = std::chrono::steady_clock::now();
    int rounds = 0;
    bool refused = false;
    while (!refused && rounds < 6) {
        big.tris.clear();
        for (size_t i = 0; i < e3.terrain().drawn().size(); i++)
            if (!e3.terrain().drawn()[i].removed) big.tris.push_back(int(i));
        bs.ter = big;
        refused = !e3.pushStep(bs);
        rounds++;
    }
    const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    char b[200];
    snprintf(b, sizeof b, "editor: splitting on and on is refused with a reason (round %d: %s)", rounds, e3.error().c_str());
    check(refused && !e3.error().empty(), b);
    printf("TIME editor rebuilds: %d rounds of splits, %.1f ms, the last build %.1f ms\n", rounds, ms, e3.buildMs);
}

/* ---- the texture codec ---- */
struct TexVector {
    int fmt, siz, ptype, w, h, pattern, seed, withOrig, mips, palN, palSeed;
    const char *sha1;
};

/* texcodec_vectors.py palette(): n u16 colours from the LCG */
std::vector<uint8_t> lcgPalette(int n, int seed) {
    uint32_t s = uint32_t(seed);
    auto next = [&]() {
        s = s * 1103515245u + 12345u;
        return unsigned((s >> 16) & 0xFF);
    };
    std::vector<uint8_t> out;
    for (int i = 0; i < n; i++) {
        const unsigned hi = next(), lo = next();
        const unsigned v = (hi << 8 | lo) & 0x7FFF;
        out.push_back(uint8_t(v >> 8)), out.push_back(uint8_t(v));
    }
    return out;
}
#include "tex_vectors.h"

std::vector<uint8_t> lcgImage(int w, int h, int pattern, int seed) {
    uint32_t s = uint32_t(seed);
    auto next = [&]() {
        s = s * 1103515245u + 12345u;
        return uint8_t((s >> 16) & 0xFF);
    };
    std::vector<uint8_t> out;
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            if (pattern == 0) {
                const uint8_t r = next(), g = next(), bb = next(), a = next();
                out.push_back(r), out.push_back(g), out.push_back(bb), out.push_back(a);
            } else if (pattern == 1) {
                out.push_back(uint8_t((x * 255) / std::max(1, w - 1)));
                out.push_back(uint8_t((y * 255) / std::max(1, h - 1)));
                out.push_back(uint8_t(((x + y) * 7) & 255));
                out.push_back((x + y) & 1 ? 255 : 100);
            } else {
                const bool on = ((x / 4) + (y / 4)) & 1;
                const uint8_t c[4] = {uint8_t(on ? 250 : 30), uint8_t(on ? 200 : 30), uint8_t(on ? 20 : 30),
                                      uint8_t(on ? 255 : 0)};
                out.insert(out.end(), c, c + 4);
            }
        }
    return out;
}

void texTests() {
    char err[256];
    int ok = 0, n = 0;
    std::string firstBad;
    for (const TexVector &v : kTexVectors) {
        const std::vector<uint8_t> rgba = lcgImage(v.w, v.h, v.pattern, v.seed);
        std::vector<uint8_t> orig;
        if (v.withOrig) {
            uint8_t *t = nullptr;
            size_t tn = 0;
            if (v.mips > 1) {
                tn = tc_mip_layout(uint32_t(v.w), uint32_t(v.h), v.siz, v.mips, nullptr);
                for (size_t i = 0; i < tn + 64; i++) orig.push_back(uint8_t((i * 37 + size_t(v.seed)) & 0xFF));
            } else if (tc_from_rgba8(rgba.data(), uint32_t(v.w), uint32_t(v.h), v.fmt, v.siz, &t, &tn, err, sizeof err) == 0) {
                for (size_t i = 0; i < tn + 64; i++) orig.push_back(uint8_t((i * 37 + size_t(v.seed)) & 0xFF));
                free(t);
            }
        }
        uint8_t *p = nullptr;
        size_t pn = 0;
        std::string got = "error";
        int rc;
        if (v.palN > 0) {
            /* (a palette packing's slot: the picture alone; withOrig 2: 64 bytes more, refused) */
            const std::vector<uint8_t> pal = lcgPalette(v.palN, v.palSeed);
            size_t on = 0;
            if (v.withOrig) {
                uint8_t *t = nullptr;
                if (tc_from_rgba8(rgba.data(), uint32_t(v.w), uint32_t(v.h), v.fmt, v.siz, &t, &on, err, sizeof err) == 0) free(t);
                if (v.withOrig == 2) on += 64;
            }
            rc = tc_encode_slot_pal(rgba.data(), uint32_t(v.w), uint32_t(v.h), v.fmt, v.siz, v.ptype, on, pal.data(), pal.size(),
                                    &p, &pn, err, sizeof err);
        } else {
            rc = tc_encode_slot_mips(rgba.data(), uint32_t(v.w), uint32_t(v.h), v.fmt, v.siz, v.ptype,
                                     v.withOrig ? orig.data() : nullptr, orig.size(), v.mips, &p, &pn, err, sizeof err);
        }
        if (rc == 0) {
            char h[41];
            lm_sha1_hex(p, pn, h);
            got = h;
            free(p);
        }
        n++;
        if (got == v.sha1) ok++;
        else if (firstBad.empty()) {
            char b[200];
            snprintf(b, sizeof b, "fmt %d siz %d packing %d %dx%d pattern %d mips %d%s: %s, Python %s", v.fmt, v.siz, v.ptype,
                     v.w, v.h, v.pattern, v.mips, v.withOrig ? " (slot texels)" : "", got.c_str(), v.sha1);
            firstBad = b;
        }
    }
    char b[300];
    snprintf(b, sizeof b, "tex: encode_slot equals texture.py's on %d of %d synthetic cases%s%s", ok, n,
             firstBad.empty() ? "" : "; first difference: ", firstBad.c_str());
    check(ok == n && n > 0, b);
    /* levels of detail: a 32 x 32 RGBA16 slot in 4 levels is laid out as the level lists' tiles read it (tile 1 at
     * TMEM 256 = byte 2048, tile 2 at 320, tile 3 at 336; 1360 texels), odd rows' words swapped; level 1 the means */
    {
        TcMipLevel lay[4];
        const size_t total = tc_mip_layout(32, 32, 16, 4, lay);
        check(total == 2720 && lay[1].offset == 2048 && lay[2].offset == 2560 && lay[3].offset == 2688 && lay[1].line == 32 &&
                  lay[3].line == 8 && lay[3].w == 4,
              "tex: mip layout of 32 x 32 RGBA16 in 4 levels: the lists' tiles (TMEM 0, 256, 320, 336; 1360 texels)");
        check(tc_mip_layout(16, 64, 16, 4, nullptr) == 2752 && tc_mip_layout(128, 8, 16, 4, nullptr) == 2720 &&
                  tc_mip_layout(16, 32, 16, 4, nullptr) == 1376,
              "tex: ... 16 x 64 (1376 texels: 2 x 8 rows padded to 8 bytes), 128 x 8, 16 x 32 as the lists load them");
        std::vector<uint8_t> img(4 * 2 * 4);
        for (int i = 0; i < 8; i++) img[4 * i] = uint8_t(i * 32), img[4 * i + 1] = 0, img[4 * i + 2] = 0, img[4 * i + 3] = 255;
        uint8_t *t = nullptr;
        size_t tn = 0;
        bool mipOk = tc_mip_texels(img.data(), 4, 2, 0, 16, 2, nullptr, 0, &t, &tn, err, sizeof err) == 0 && tn == 24;
        auto r16 = [&](size_t at) { return unsigned(t[at] << 8 | t[at + 1]) >> 11; };
        if (mipOk) {
            /* row 0: texels 0 1 2 3; row 1 (odd): 6 7 4 5; level 1 (2 x 1): means of 0 1 4 5 and 2 3 6 7 */
            mipOk = r16(0) == 0 && r16(6) == 96 >> 3 && r16(8) == 192 >> 3 && r16(12) == 128 >> 3 &&
                    r16(16) == 80 >> 3 && r16(18) == 144 >> 3;
            free(t);
        }
        check(mipOk, "tex: mip texels: odd rows' 32-bit words swapped, level 1 the 2 x 2 means");
    }
    /* PNG: our writer, our reader, every filter */
    const std::vector<uint8_t> img = lcgImage(13, 7, 0, 5);
    size_t pn = 0;
    uint8_t *png = tc_png_encode(img.data(), 13, 7, &pn);
    uint32_t w = 0, h = 0;
    uint8_t *back = nullptr;
    check(png && tc_png_decode(png, pn, 0, &w, &h, &back, err, sizeof err) == 0 && w == 13 && h == 7 &&
              !memcmp(back, img.data(), img.size()),
          "tex: a PNG written (stored deflate) reads back");
    free(back);
    /* the same rows with filters 1..4 (each row its own), a stored zlib stream */
    {
        const size_t stride = 13 * 4;
        std::vector<uint8_t> raw, prev(stride, 0);
        for (int y = 0; y < 7; y++) {
            const int ft = y % 5;
            const uint8_t *line = img.data() + y * stride;
            raw.push_back(uint8_t(ft));
            for (size_t k = 0; k < stride; k++) {
                const int a = k >= 4 ? line[k - 4] : 0, bb = prev[k], c = k >= 4 ? prev[k - 4] : 0;
                int pr = 0;
                if (ft == 1) pr = a;
                else if (ft == 2) pr = bb;
                else if (ft == 3) pr = (a + bb) >> 1;
                else if (ft == 4) {
                    const int pa = abs(bb - c), pb = abs(a - c), pc = abs(a + bb - 2 * c);
                    pr = pa <= pb && pa <= pc ? a : (pb <= pc ? bb : c);
                }
                raw.push_back(uint8_t(line[k] - pr));
            }
            prev.assign(line, line + stride);
        }
        /* a PNG around it: take ours and swap its IDAT for these rows (stored, with their Adler-32) */
        std::vector<uint8_t> z = {0x78, 0x01, 1, uint8_t(raw.size()), uint8_t(raw.size() >> 8), uint8_t(~raw.size()),
                                  uint8_t(~raw.size() >> 8)};
        z.insert(z.end(), raw.begin(), raw.end());
        uint32_t a1 = 1, a2 = 0;
        for (uint8_t q : raw) a1 = (a1 + q) % 65521, a2 = (a2 + a1) % 65521;
        for (int i = 3; i >= 0; i--) z.push_back(uint8_t((a2 << 16 | a1) >> (8 * i)));
        std::vector<uint8_t> f(png, png + 33);   /* signature + IHDR */
        auto chunk = [&](const char *kind, const std::vector<uint8_t> &d) {
            Bytes q;
            put32(q, uint32_t(d.size()));
            f.insert(f.end(), q.begin(), q.end());
            f.insert(f.end(), kind, kind + 4);
            f.insert(f.end(), d.begin(), d.end());
            put32(q, 0);   /* (the reader doesn't check chunk CRCs, as png.py doesn't) */
            f.insert(f.end(), q.end() - 4, q.end());
        };
        chunk("IDAT", z);
        chunk("IEND", {});
        uint8_t *r = nullptr;
        check(tc_png_decode(f.data(), f.size(), 0, &w, &h, &r, err, sizeof err) == 0 && !memcmp(r, img.data(), img.size()),
              "tex: PNG rows with filters 1-4 (sub, up, average, Paeth) read back");
        free(r);
    }
    free(png);
    check(tc_png_decode((const uint8_t *) "nope", 4, 0, &w, &h, &back, err, sizeof err) != 0, "tex: not a PNG: refused");
    /* base64 */
    const uint8_t raw[5] = {0, 1, 2, 0xFE, 0xFF};
    for (size_t l = 0; l <= 5; l++) {
        char *s = tc_base64_encode(raw, l);
        uint8_t *d = nullptr;
        size_t dn = 0;
        const bool good = s && tc_base64_decode(s, &d, &dn) == 0 && dn == l && (l == 0 || !memcmp(d, raw, l));
        free(s), free(d);
        if (!good) {
            check(false, "tex: base64 round trip");
            return;
        }
    }
    check(true, "tex: base64 round trip (0..5 bytes)");
    char *s = tc_base64_encode((const uint8_t *) "Man", 3);
    check(s && !strcmp(s, "TWFu"), "tex: base64 of \"Man\" is TWFu");
    free(s);
}

}  // namespace

void terrainTestsAll(void (*chk)(bool, const char *)) {
    g_check = chk;
    terrainTests();
    wallSeamTests();
    weldTests();
    editorTests();
    texTests();
}

/* --budgets DIR: lg_budgets of DIR/level_NN.bin (inflated level files, the ROM's byte order; written from the
 * player's ROM at test time) -> one line per level "L<n> <n0> <n1> <n2> <n3>" (geometry.py list_commands' twin) */
int budgetsCross(const char *dir) {
    int done = 0;
    for (int lv = 0; lv < 60; lv++) {
        char path[1024];
        snprintf(path, sizeof path, "%s/level_%02d.bin", dir, lv);
        FILE *f = fopen(path, "rb");
        if (!f) continue;
        std::vector<uint8_t> b;
        uint8_t buf[65536];
        size_t k;
        while ((k = fread(buf, 1, sizeof buf, f)) > 0) b.insert(b.end(), buf, buf + k);
        fclose(f);
        int n[4], cap[4];
        char err[300];
        if (lg_budgets(b.data(), b.size(), lv, n, cap, err, sizeof err)) printf("L%d ERR %s\n", lv, err);
        else printf("L%d %d %d %d %d\n", lv, n[0], n[1], n[2], n[3]);
        done++;
    }
    return done ? 0 : 2;
}

/* --subdiv DIR: the terrain model's SUBDIVIDE over DIR/cases.txt (port/tools/subdiv_cross.py; level files written
 * from the player's ROM at test time): per line "NAME LEVELFILE LEVELS ID,ID,... [OPS]" the drawn triangles split
 * LEVELS times (each time the last split's pieces; 0: not split), then the moves OPS (";"-separated: "sel:DY" the
 * selection's movable corners up DY, as the editor's Select tool moves them; "disc:X,Z,R,S,F" one application of
 * the raise brush at (X, Z), radius R, strength S, falloff F, as a stroke's step), the edit written to
 * DIR/NAME.c.json (geometry.py apply_edit's form, the twin of bcfiles terrain.py's Model.export) or "ERR ...", and
 * what the welds did to DIR/NAME.c.weld ("partners followers protected tjunctions" summed over the moves) */
int subdivCross(const char *dir) {
    const std::string d = dir;
    FILE *f = fopen((d + "/cases.txt").c_str(), "r");
    if (f == nullptr) {
        printf("no %s/cases.txt\n", dir);
        return 2;
    }
    static char line[1 << 16];
    int n = 0;
    while (fgets(line, sizeof line, f)) {
        static char ids[60000], ops[8192];
        char name[128], file[256];
        int levels = 1;
        ops[0] = 0;
        if (sscanf(line, "%127s %255s %d %59999s %8191s", name, file, &levels, ids, ops) < 4) continue;
        terrain::WeldInfo wsum;
        std::vector<uint8_t> lvl;
        if (FILE *g = fopen((d + "/" + file).c_str(), "rb")) {
            uint8_t buf[65536];
            size_t k;
            while ((k = fread(buf, 1, sizeof buf, g)) > 0) lvl.insert(lvl.end(), buf, buf + k);
            fclose(g);
        }
        std::string out, err;
        terrain::Model m;
        if (!m.open(lvl.data(), lvl.size(), err)) {
            out = "ERR " + err;
        } else {
            std::vector<int> sel;
            for (char *p = strtok(ids, ","); p; p = strtok(nullptr, ",")) sel.push_back(atoi(p));
            for (int l = 0; l < levels && out.empty(); l++) {
                terrain::Step s;
                s.kind = terrain::ST_SUBDIVIDE;
                s.tris = sel;
                if (!m.apply(s, err)) out = "ERR " + err;
                sel = m.lastSplit();
            }
            std::vector<std::string> opl;
            for (char *p = strtok(ops, ";"); p; p = strtok(nullptr, ";")) opl.push_back(p);
            for (const std::string &op : opl) {
                if (!out.empty()) break;
                terrain::Step s;
                s.kind = terrain::ST_MOVE;
                if (op.compare(0, 4, "sel:") == 0) {
                    const int dy = atoi(op.c_str() + 4);
                    std::vector<int> ps;
                    for (int t : sel)
                        for (int k = 0; k < 3; k++) ps.push_back(m.drawn()[size_t(t)].pos[k]);
                    std::sort(ps.begin(), ps.end());
                    ps.erase(std::unique(ps.begin(), ps.end()), ps.end());
                    for (int p : ps) {
                        if (!m.movable(p)) continue;
                        P3 v = m.positions()[size_t(p)].cur;
                        v.y = std::max(-32768, std::min(32767, v.y + dy));
                        s.moves.push_back({p, v});
                    }
                } else if (op.compare(0, 5, "disc:") == 0) {
                    double x = 0, z = 0;
                    terrain::Brush b;
                    if (sscanf(op.c_str() + 5, "%lf,%lf,%lf,%lf,%lf", &x, &z, &b.radius, &b.strength, &b.falloff) != 5) {
                        out = "ERR bad op " + op;
                        break;
                    }
                    std::unordered_map<int, double> cur;
                    int skipped = 0;
                    m.brush(terrain::B_RAISE, b, x, z, 0, cur, skipped);
                    s = m.strokeStep(cur, "raise");
                } else {
                    out = "ERR bad op " + op;
                    break;
                }
                if (s.moves.empty()) continue;
                if (!m.apply(s, err)) out = "ERR " + err;
                wsum.partners += m.lastWeld().partners, wsum.followers += m.lastWeld().followers;
                wsum.protSkipped += m.lastWeld().protSkipped, wsum.tjunctions += m.lastWeld().tjunctions;
            }
            LgEdit e;
            if (out.empty() && m.exportEdit(e)) {
                char b[400];
                auto put = [&](const char *s) { out += s; };
                put("{\"move\": [");
                for (int i = 0; i < e.nmove; i++) {
                    snprintf(b, sizeof b, "%s[%d, %d, %d, %d, %d, %d]", i ? ", " : "", e.move[i].from[0], e.move[i].from[1],
                             e.move[i].from[2], e.move[i].to[0], e.move[i].to[1], e.move[i].to[2]);
                    put(b);
                }
                static const char *kRem[4] = {"render", "ground", "walls_a", "walls_b"};
                put("],\n \"remove\": {");
                for (int k = 0; k < 4; k++) {
                    snprintf(b, sizeof b, "%s\"%s\": [", k ? ", " : "", kRem[k]);
                    put(b);
                    for (int i = 0; i < e.nrem[k]; i++) snprintf(b, sizeof b, "%s%d", i ? ", " : "", e.rem[k][i]), put(b);
                    put("]");
                }
                put("},\n \"add\": {\"render\": [");
                for (int i = 0; i < e.nrender; i++) {
                    const LgAddRender &r = e.render[i];
                    snprintf(b, sizeof b, "%s\n  {\"after\": %d, \"flag\": %d, \"v\": [", i ? "," : "", r.after, r.flag);
                    put(b);
                    for (int c = 0; c < 3; c++) {
                        snprintf(b, sizeof b, "%s[%d, %d, %d, %d, %d, %d, %d, %d, %d]", c ? ", " : "", r.v[c][0], r.v[c][1],
                                 r.v[c][2], r.v[c][3], r.v[c][4], r.v[c][5], r.v[c][6], r.v[c][7], r.v[c][8]);
                        put(b);
                    }
                    put("]}");
                }
                put("],\n \"ground\": [");
                for (int i = 0; i < e.nground; i++) {
                    const LgAddTri &t = e.ground[i];
                    snprintf(b, sizeof b, "%s\n  {\"v\": [%d, %d, %d, %d, %d, %d, %d, %d, %d], \"surface\": %d, \"flag\": %d}",
                             i ? "," : "", t.v[0], t.v[1], t.v[2], t.v[3], t.v[4], t.v[5], t.v[6], t.v[7], t.v[8], t.a, t.b);
                    put(b);
                }
                for (int k = 0; k < 2; k++) {
                    put(k ? "],\n \"walls_b\": [" : "],\n \"walls_a\": [");
                    for (int i = 0; i < e.nwall[k]; i++) {
                        const LgAddTri &t = e.wall[k][i];
                        snprintf(b, sizeof b,
                                 "%s\n  {\"v\": [%d, %d, %d, %d, %d, %d, %d, %d, %d], \"heading\": %d, \"side\": %d, \"fail\": %d}",
                                 i ? "," : "", t.v[0], t.v[1], t.v[2], t.v[3], t.v[4], t.v[5], t.v[6], t.v[7], t.v[8], t.a, t.b, t.c);
                        put(b);
                    }
                }
                put("]}}\n");
                lg_edit_free(&e);
            } else if (out.empty()) {
                out = "ERR out of memory";
            }
        }
        FILE *q = fopen((d + "/" + name + ".c.json").c_str(), "w");
        if (q) fputs(out.c_str(), q), fclose(q);
        q = fopen((d + "/" + name + ".c.weld").c_str(), "w");
        if (q) fprintf(q, "%d %d %d %d\n", wsum.partners, wsum.followers, wsum.protSkipped, wsum.tjunctions), fclose(q);
        n++;
    }
    fclose(f);
    printf("subdiv: %d cases written\n", n);
    return 0;
}

/* --texcodec DIR: the codec over DIR/cases.txt (port/tools/texcodec_cross.py); 0: all written */
int texcodecCross(const char *dir) {
    const std::string d = dir;
    FILE *f = fopen((d + "/cases.txt").c_str(), "r");
    if (f == nullptr) {
        printf("no %s/cases.txt\n", dir);
        return 2;
    }
    char line[512];
    int n = 0;
    while (fgets(line, sizeof line, f)) {
        char name[128], pngName[128], origName[128] = "-";
        int fmt, siz, ptype, mips = 0;
        if (sscanf(line, "%127s %127s %d %d %d %127s %d", name, pngName, &fmt, &siz, &ptype, origName, &mips) < 5) continue;
        auto readAll = [](const std::string &p, std::vector<uint8_t> &out) {
            FILE *g = fopen(p.c_str(), "rb");
            if (!g) return false;
            uint8_t buf[65536];
            size_t k;
            while ((k = fread(buf, 1, sizeof buf, g)) > 0) out.insert(out.end(), buf, buf + k);
            fclose(g);
            return true;
        };
        std::vector<uint8_t> png, orig;
        readAll(d + "/" + pngName, png);
        const bool haveOrig = strcmp(origName, "-") != 0 && readAll(d + "/" + origName, orig);
        uint32_t w = 0, h = 0;
        uint8_t *rgba = nullptr;
        char err[256];
        std::string out;
        if (tc_png_decode(png.data(), png.size(), 0, &w, &h, &rgba, err, sizeof err)) {
            out = std::string("ERR ") + err;
        } else {
            FILE *g = fopen((d + "/" + name + ".c.rgba").c_str(), "wb");
            if (g) fwrite(rgba, 1, size_t(w) * h * 4, g), fclose(g);
            uint8_t *p = nullptr;
            size_t pn = 0;
            if (tc_encode_slot_mips(rgba, w, h, fmt, siz, ptype, haveOrig ? orig.data() : nullptr, orig.size(), mips, &p, &pn,
                                    err, sizeof err)) {
                out = std::string("ERR ") + err;
            } else {
                FILE *q = fopen((d + "/" + name + ".c.bin").c_str(), "wb");
                if (q) fwrite(p, 1, pn, q), fclose(q);
                free(p);
                out = "OK";
            }
            free(rgba);
        }
        FILE *q = fopen((d + "/" + name + ".c.txt").c_str(), "w");
        if (q) fprintf(q, "%s\n", out.c_str()), fclose(q);
        n++;
    }
    fclose(f);
    printf("texcodec: %d cases written\n", n);
    return 0;
}
