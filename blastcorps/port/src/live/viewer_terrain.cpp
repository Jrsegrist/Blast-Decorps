/* The level viewer's editor: the Terrain mode (viewer_internal.h; the model: terrain.h).
 * Brushes (raise, lower, smooth, flatten), a ramp between two points, a selection (moved by hand, split for detail),
 * painting with the level's own textures and texture slots from PNGs.  Each stroke is one step of the editor's
 * change list (undo / redo with the record edits).
 * The preview: while a stroke is made and until Apply, the frozen picture shows the moved corners.  The vertices
 * the game's display lists already point to (the level file's segment 8 in RDRAM) are written with the new values
 * for the replay only: replay() writes them before RT64 reads the frame's lists and puts the game's values back
 * right after, so RDRAM holds what the game left in it whenever the game could look.  The lists' G_CULLDL tests
 * are skipped while a preview is on (their boxes are the old ones).  Splits, paint and texture slots change the
 * lists themselves: Apply shows them.  Apply rebuilds the level file (lg_apply: display lists, collision grids,
 * visibility) and the game sets the level up again from it, so what is shown afterwards is the game running it. */
#include <windows.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cwchar>
#include <string>
#include <unordered_map>
#include <vector>

#include "imgui/imgui.h"

#include "live_menu.h"
#include "viewer_internal.h"

#include "load/port_mods.h"
#include "load/tex_codec.h"

extern "C" {
#include "plat_host.h"
}

namespace live {
namespace vw {

TerrainView g_tv;

void terrainCountSeams() {
    g_tv.seams = g_ed.terrainOk() ? g_ed.terrain().seams().size() : 0;
}

bool terrainMode() {
    return editing() && g_tv.on && g_ed.terrainOk();
}

void terrainSnapshot() {
    g_tv.applied.clear();
    for (const terrain::Pos &p : g_ed.terrain().positions()) g_tv.applied.push_back(p.cur);
    g_tv.mapFrame = ~0u;
}

void terrainOpened() {
    terrainSnapshot();
    g_tv.selTri.clear(), g_tv.selPos.clear();
    g_tv.rampN = 0;
    g_tv.stroking = false;
    terrainCountSeams();
}

void terrainApplied() {
    terrainSnapshot();
}

/* after the model changed (a step, undo, redo): the selection to what is still there */
void terrainChanged() {
    const terrain::Model &m = g_ed.terrain();
    std::vector<int> t;
    for (int i : g_tv.selTri)
        if (i >= 0 && i < int(m.drawn().size()) && !m.drawn()[i].removed) t.push_back(i);
    g_tv.selTri = t;
    std::vector<int> p;
    for (int i : g_tv.selPos)
        if (i >= 0 && i < int(m.positions().size())) p.push_back(i);
    g_tv.selPos = p;
    terrainCountSeams();
}

bool pushTerrain(const terrain::Step &st, const std::string &label) {
    edit::EditStep s;
    s.type = edit::STEP_TERRAIN;
    s.ter = st;
    s.label = label;
    if (!g_ed.pushStep(s)) {
        g_editMsg = "Not done: " + g_ed.error();
        host_log("live: editor: refused: %s: %s\n", label.c_str(), g_ed.error().c_str());
        return false;
    }
    g_editMsg = label;
    if (g_tv.applyEach) g_after.push_back(applyEdits);   /* (the game rebuilds and reloads after every stroke) */
    g_tv.strokeMs.push_back(g_ed.buildMs);
    if (g_tv.strokeMs.size() > 8) g_tv.strokeMs.erase(g_tv.strokeMs.begin());
    host_log("live: editor: %s (%u steps; file %u bytes, SHA-1 %s; rebuilt in %.1f ms)\n", label.c_str(),
             unsigned(g_ed.steps().size()), unsigned(g_ed.image().size()), g_ed.imageSha1().c_str(), g_ed.buildMs);
    terrainChanged();
    return true;
}

/* ---- the preview ---- */
void buildPreviewMap() {
    const uint32_t lvl = levelData();
    if (g_tv.mapFrame == g_capturedFrame && g_tv.mapLvl == lvl) return;
    g_tv.mapFrame = g_capturedFrame, g_tv.mapLvl = lvl;
    g_tv.map.clear();
    g_tv.mapCorners = 0;
    const size_t span = levelSpan(lvl);
    if (span < 0xC8 || g_tv.applied.empty()) return;
    const uint32_t vend = rd<uint32_t>(lvl + 0x20);
    if (vend < 0xC8 || vend > span) return;
    std::unordered_map<uint64_t, int> at;
    for (size_t i = 0; i < g_tv.applied.size(); i++) at.emplace(terrain::key3(g_tv.applied[i]), int(i));
    unsigned n = 0;
    std::vector<char> seen(g_tv.applied.size(), 0);
    for (uint32_t a = lvl + 0xC8; a + 16 <= lvl + vend; a += 16, n++) {
        terrain::P3 v;
        v.x = rd<int16_t>(a), v.y = rd<int16_t>(a + 2), v.z = rd<int16_t>(a + 4);
        auto it = at.find(terrain::key3(v));
        if (it == at.end()) continue;
        g_tv.map.push_back({a, it->second});
        if (!seen[it->second]) seen[it->second] = 1, g_tv.mapCorners++;
    }
    host_log("live: editor: preview: %u of %u vertices of the game's file are corners of the model (%u corners)\n",
             unsigned(g_tv.map.size()), n, unsigned(g_tv.mapCorners));
}

/* the stroke's step with the welds (corners of the neighbouring meshes moved with it), once per application */
void resolveStroke() {
    if (!g_tv.stroking) {
        g_tv.shown.clear();
        g_tv.shownApps = ~0u;
        return;
    }
    if (g_tv.shownApps == g_tv.applications) return;
    g_tv.shownApps = g_tv.applications;
    g_tv.shown.clear();
    const terrain::Model &m = g_ed.terrain();
    for (const auto &mv : m.resolve(m.strokeStep(g_tv.cur, "").moves, nullptr)) g_tv.shown[mv.first] = mv.second;
}

/* the corner's value the picture should show (the stroke's while it is made) */
terrain::P3 shownValue(int p) {
    if (g_tv.stroking) {
        resolveStroke();
        auto it = g_tv.shown.find(p);
        if (it != g_tv.shown.end()) return it->second;
    }
    return g_ed.terrain().positions()[p].cur;
}

/* what the last move's welds did, for the message ("" if nothing) */
std::string weldNote() {
    const terrain::WeldInfo &w = g_ed.terrain().lastWeld();
    std::string s;
    char b[200];
    if (w.partners || w.followers) {
        snprintf(b, sizeof b, "; welded: %d corner%s of the neighbouring meshes moved with them", w.partners + w.followers,
                 w.partners + w.followers == 1 ? "" : "s");
        s += b;
    }
    if (w.protSkipped) {
        snprintf(b, sizeof b, "; %d protected corner%s welded to them left where %s (a crack may show there)", w.protSkipped,
                 w.protSkipped == 1 ? "" : "s", w.protSkipped == 1 ? "it is" : "they are");
        s += b;
    }
    if (w.tjunctions) {
        snprintf(b, sizeof b, "; %d corner%s where meshes' edges cross taken into those edges (split there: no crack)",
                 w.tjunctions, w.tjunctions == 1 ? "" : "s");
        s += b;
    }
    return s;
}

std::chrono::steady_clock::time_point g_previewT0;
void previewWrite() {
    g_previewT0 = std::chrono::steady_clock::now();
    g_tv.saved.clear();
    g_tv.previewOn = false;
    if (!editing() || !g_ed.terrainOk() || g_ed.level() != g_fz.levelNo) return;
    buildPreviewMap();
    const auto &ps = g_ed.terrain().positions();
    for (const auto &e : g_tv.map) {
        if (e.second >= int(ps.size()) || size_t(e.second) >= g_tv.applied.size()) continue;
        const terrain::P3 v = shownValue(e.second);
        if (v == g_tv.applied[e.second]) continue;
        uint64_t old = 0;
        memcpy(&old, reinterpret_cast<const void *>(uintptr_t(e.first)), 6);
        g_tv.saved.push_back({e.first, old});
        int16_t *w = reinterpret_cast<int16_t *>(uintptr_t(e.first));
        w[0] = int16_t(v.x), w[1] = int16_t(v.y), w[2] = int16_t(v.z);
    }
    g_tv.previewOn = !g_tv.saved.empty();
    g_tv.previewed = unsigned(g_tv.saved.size());
    g_previewNoCull = g_tv.previewOn;
}

void previewRestore() {
    for (auto it = g_tv.saved.rbegin(); it != g_tv.saved.rend(); ++it)
        memcpy(reinterpret_cast<void *>(uintptr_t(it->first)), &it->second, 6);
    g_tv.saved.clear();
    g_tv.previewOn = false;
    g_previewNoCull = false;
    g_tv.previewMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - g_previewT0).count();
}

/* ---- pointing ---- */
void terrainHover(float sx, float sy) {
    g_tv.hoverOk = false;
    g_tv.hoverTri = -1;
    edit::V3 o, d;
    if (!g_ed.terrainOk() || !screenRay(sx, sy, o, d)) return;
    const double O[3] = {o.x, o.y, o.z}, D[3] = {d.x, d.y, d.z};
    double t, hit[3];
    const int tri = g_ed.terrain().pick(O, D, t, hit);
    if (tri < 0) return;
    g_tv.hoverTri = tri;
    memcpy(g_tv.hover, hit, sizeof hit);
    g_tv.hoverOk = true;
}

/* the point the tool works at: the mouse's, or the picture's centre for a controller */
void terrainAim() {
    const ImGuiIO &io = ImGui::GetIO();
    if (g_tv.padAim || g_tv.mx < 0) terrainHover(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f);
    else terrainHover(g_tv.mx, g_tv.my);
}

int nearestCorner(int tri, const double at[3]) {
    const terrain::Model &m = g_ed.terrain();
    int best = -1;
    double bd = 1e30;
    for (int k = 0; k < 3; k++) {
        const terrain::P3 &v = m.positions()[m.drawn()[tri].pos[k]].cur;
        const double d = (v.x - at[0]) * (v.x - at[0]) + (v.y - at[1]) * (v.y - at[1]) + (v.z - at[2]) * (v.z - at[2]);
        if (d < bd) bd = d, best = m.drawn()[tri].pos[k];
    }
    return best;
}

/* ---- the tools ---- */
void applyBrushOnce(double perApp) {
    if (!g_tv.hoverOk) return;
    terrain::BrushTool bt = terrain::B_RAISE;
    terrain::Brush b = g_tv.brush;
    switch (g_tv.tool) {
    case TT_RAISE: bt = terrain::B_RAISE, b.strength = perApp; break;
    case TT_LOWER: bt = terrain::B_LOWER, b.strength = perApp; break;
    case TT_SMOOTH: bt = terrain::B_SMOOTH, b.strength = g_tv.amount; break;
    case TT_FLATTEN: bt = terrain::B_FLATTEN, b.strength = g_tv.amount; break;
    default: return;
    }
    g_ed.terrain().brush(bt, b, g_tv.hover[0], g_tv.hover[2], g_tv.flatY, g_tv.cur, g_tv.skipped);
    g_tv.applications++;
}

void endStroke() {
    if (!g_tv.stroking) return;
    g_tv.stroking = false;
    const std::string what = std::string(kToolNames[g_tv.tool]) + " (radius " + std::to_string(int(g_tv.brush.radius)) + ")";
    terrain::Step st = g_ed.terrain().strokeStep(g_tv.cur, what);
    const int skipped = g_tv.skipped;
    g_tv.cur.clear();
    if (st.moves.empty()) {
        g_editMsg = skipped ? "Nothing moved: the brush only reached protected corners (bridges, moving parts, switches, "
                              "gates)" : "Nothing moved (no corner within the brush: make it larger, or split the "
                                         "triangles first).";
        host_log("live: editor: %s\n", g_editMsg.c_str());
        return;
    }
    char b[200];
    snprintf(b, sizeof b, "terrain: %s, %u corners%s", what.c_str(), unsigned(st.moves.size()),
             skipped ? " (protected corners skipped)" : "");
    if (pushTerrain(st, b)) {
        char m[200] = "";
        if (skipped)
            snprintf(m, sizeof m, "; %d protected corner touch%s skipped (bridges, moving parts, switches, gates)", skipped,
                     skipped == 1 ? "" : "es");
        g_editMsg = std::string(b) + m + weldNote();
        if (skipped || !weldNote().empty()) host_log("live: editor: %s\n", g_editMsg.c_str());
    }
}

void selectTri(int tri, const double at[3], bool add) {
    if (!add) g_tv.selTri.clear(), g_tv.selPos.clear();
    if (tri < 0) return;
    if (g_tv.selVerts) {
        const int p = nearestCorner(tri, at);
        auto it = std::find(g_tv.selPos.begin(), g_tv.selPos.end(), p);
        if (it == g_tv.selPos.end()) g_tv.selPos.push_back(p);
        else if (add) g_tv.selPos.erase(it);
    } else {
        auto it = std::find(g_tv.selTri.begin(), g_tv.selTri.end(), tri);
        if (it == g_tv.selTri.end()) g_tv.selTri.push_back(tri);
        else if (add) g_tv.selTri.erase(it);
    }
}

void makeRamp() {
    if (g_tv.rampN < 2) return;
    terrain::Brush b = g_tv.brush;
    b.strength = 1.0;
    int skipped = 0;
    terrain::Step st = g_ed.terrain().ramp(g_tv.rampA, g_tv.rampB, b, skipped);
    if (st.moves.empty()) {
        g_editMsg = "The ramp moves nothing (its corners are already there, or protected).";
        return;
    }
    char lb[200];
    snprintf(lb, sizeof lb, "terrain: ramp (%.0f, %.0f, %.0f) to (%.0f, %.0f, %.0f), width %.0f: %u corners%s", g_tv.rampA[0],
             g_tv.rampA[1], g_tv.rampA[2], g_tv.rampB[0], g_tv.rampB[1], g_tv.rampB[2], b.radius * 2,
             unsigned(st.moves.size()), skipped ? " (protected corners skipped)" : "");
    st.what = "ramp";
    if (pushTerrain(st, lb)) {
        g_tv.rampN = 0;
        g_editMsg += weldNote();
    }
}

/* the selection's corners (its triangles' or the picked ones) */
std::vector<int> selCorners() {
    std::vector<int> ps = g_tv.selPos;
    for (int t : g_tv.selTri)
        for (int k = 0; k < 3; k++) ps.push_back(g_ed.terrain().drawn()[t].pos[k]);
    std::sort(ps.begin(), ps.end());
    ps.erase(std::unique(ps.begin(), ps.end()), ps.end());
    return ps;
}

void moveSelection(int dx, int dy, int dz) {
    const terrain::Model &m = g_ed.terrain();
    terrain::Step st;
    st.kind = terrain::ST_MOVE;
    int skipped = 0, following = 0;
    for (int p : selCorners()) {
        if (!m.movable(p)) {
            skipped++;
            following += m.positions()[size_t(p)].host >= 0;
            continue;
        }
        terrain::P3 v = m.positions()[p].cur;
        v.x = std::max(-32768, std::min(32767, v.x + dx));
        v.y = std::max(-32768, std::min(32767, v.y + dy));
        v.z = std::max(-32768, std::min(32767, v.z + dz));
        st.moves.push_back({p, v});
    }
    if (st.moves.empty()) {
        g_editMsg = "Nothing to move (select triangles or corners; protected corners and corners that follow a "
                    "neighbour's edge don't move).";
        return;
    }
    char lb[200];
    snprintf(lb, sizeof lb, "terrain: move %u corners by (%d, %d, %d)%s", unsigned(st.moves.size()), dx, dy, dz,
             skipped ? " (protected or following corners kept)" : "");
    st.what = "move";
    if (pushTerrain(st, lb)) {
        g_editMsg += weldNote();
        if (following) {
            char b[200];
            snprintf(b, sizeof b, "; %d selected corner%s lie%s on another mesh's edge and follow%s it (split that triangle "
                     "to free %s)", following, following == 1 ? "" : "s", following == 1 ? "s" : "", following == 1 ? "s" : "",
                     following == 1 ? "it" : "them");
            g_editMsg += b;
        }
        host_log("live: editor: %s\n", g_editMsg.c_str());
    }
}

void splitSelection() {
    if (g_tv.selTri.empty()) {
        g_editMsg = "Select triangles to split (Select tool, triangles).";
        return;
    }
    terrain::Step st;
    st.kind = terrain::ST_SUBDIVIDE;
    st.tris = g_tv.selTri;
    char lb[160];
    snprintf(lb, sizeof lb, "terrain: split %u triangles (4 each; their neighbours along the edges)", unsigned(st.tris.size()));
    st.what = "split";
    if (pushTerrain(st, lb)) {
        /* the selected triangles' pieces become the selection */
        const terrain::SplitInfo &si = g_ed.terrain().lastInfo();
        g_tv.selTri = g_ed.terrain().lastSplit();
        char m[300];
        snprintf(m, sizeof m, "Split %d triangle%s; %d neighbour%s, %d floor and %d wall triangle%s split along the shared "
                 "edges (no seams)%s. Apply shows them; then brush them.", si.selected, si.selected == 1 ? "" : "s",
                 si.neighbours, si.neighbours == 1 ? "" : "s", si.floors, si.walls, si.walls == 1 ? "" : "s",
                 si.keptEdges ? "; edges along a bridge or a protected part kept whole" : "");
        g_editMsg = m;
        host_log("live: editor: %s (seams closed %d in %d rounds%s)\n", m, si.seamsClosed, si.seamRounds, si.detail.c_str());
    }
}

/* SEAL: corners left on another triangle's edge (a mod made before the editor split neighbours) */
void sealSeams() {
    terrain::Step st;
    st.kind = terrain::ST_SEAL;
    st.what = "close seams";
    const size_t n = g_tv.seams;
    char lb[160];
    snprintf(lb, sizeof lb, "terrain: close %u seam%s (triangles split at corners on their edges)", unsigned(n), n == 1 ? "" : "s");
    if (pushTerrain(st, lb)) {
        const terrain::SplitInfo &si = g_ed.terrain().lastInfo();
        char m[260];
        snprintf(m, sizeof m, "Seams closed: %d drawn, %d floor and %d wall triangle%s split at the corners on their edges.",
                 si.neighbours, si.floors, si.walls, si.walls == 1 ? "" : "s");
        g_editMsg = m;
        host_log("live: editor: %s\n", m);
    }
}

/* the level's textures (the drawn triangles' states): id -> format, size, a run to join near (x, z) */
std::vector<PalEntry> palette() {
    std::vector<PalEntry> out;
    const terrain::Model &m = g_ed.terrain();
    for (int i = 0; i < m.levelCount(LG_RENDER); i++) {
        const LgTri *t = m.levelTri(LG_RENDER, i);
        if (t->tex < 0 || t->frozen) continue;
        auto it = std::find_if(out.begin(), out.end(), [&](const PalEntry &e) {
            return e.tex == t->tex && e.fmt == t->fmt && e.siz == t->siz && e.w == t->w && e.h == t->h;
        });
        if (it == out.end()) out.push_back({t->tex, t->fmt, t->siz, t->w, t->h, 1});
        else it->count++;
    }
    std::sort(out.begin(), out.end(), [](const PalEntry &a, const PalEntry &b) { return a.tex < b.tex; });
    return out;
}

/* a run that draws texture `tex` and can take the selection: in the list of its first triangle, else a list whose
 * triangles span the selection's middle (it is drawn where they are); -1 */
int runFor(int tex, std::string &why) {
    const terrain::Model &m = g_ed.terrain();
    if (g_tv.selTri.empty()) {
        why = "Select the triangles to paint first.";
        return -1;
    }
    const LgTri *s0 = m.stateOf(g_tv.selTri[0]);
    double cx = 0, cz = 0;
    int n = 0;
    for (int t : g_tv.selTri)
        for (int k = 0; k < 3; k++) cx += m.positions()[m.drawn()[t].pos[k]].cur.x, cz += m.positions()[m.drawn()[t].pos[k]].cur.z, n++;
    cx /= std::max(1, n), cz /= std::max(1, n);
    int same = -1, best = -1;
    double bestArea = 1e300;
    std::unordered_map<int, std::array<double, 4>> box;   /* piece -> its triangles' x-z extent */
    for (int i = 0; i < m.levelCount(LG_RENDER); i++) {
        const LgTri *t = m.levelTri(LG_RENDER, i);
        auto it = box.find(t->piece);
        if (it == box.end()) it = box.emplace(t->piece, std::array<double, 4>{1e30, 1e30, -1e30, -1e30}).first;
        for (int k = 0; k < 3; k++) {
            it->second[0] = std::min(it->second[0], double(t->p[k][0])), it->second[1] = std::min(it->second[1], double(t->p[k][2]));
            it->second[2] = std::max(it->second[2], double(t->p[k][0])), it->second[3] = std::max(it->second[3], double(t->p[k][2]));
        }
    }
    for (int i = 0; i < m.levelCount(LG_RENDER); i++) {
        const LgTri *t = m.levelTri(LG_RENDER, i);
        if (t->tex != tex || t->frozen) continue;
        if (s0 != nullptr && t->piece == s0->piece) {
            same = i;
            break;
        }
        const auto &b = box[t->piece];
        if (cx >= b[0] && cx <= b[2] && cz >= b[1] && cz <= b[3]) {
            const double area = (b[2] - b[0]) * (b[3] - b[1]);
            if (area < bestArea) bestArea = area, best = i;
        }
    }
    if (same >= 0) return same;
    if (best >= 0) return best;
    char w[200];
    snprintf(w, sizeof w, "Texture %d isn't drawn by a list here: pick one this part of the level uses, or switch a "
             "whole run with \"Switch the run\".", tex);
    why = w;
    return -1;
}

/* the textures the selection can be painted with (a run of them in its list, or in a list drawn where it is) */
std::vector<int> texturesHere() {
    std::vector<int> out;
    if (g_tv.selTri.empty()) return out;
    for (const PalEntry &e : palette()) {
        std::string why;
        if (runFor(e.tex, why) >= 0) out.push_back(e.tex);
    }
    return out;
}

void paintSelection(int tex) {
    std::string why;
    const int run = runFor(tex, why);
    if (run < 0) {
        g_editMsg = why;
        return;
    }
    terrain::Step st;
    st.kind = terrain::ST_PAINT;
    st.tris = g_tv.selTri;
    st.run = run;
    st.planar = g_tv.planar;
    char lb[200];
    snprintf(lb, sizeof lb, "terrain: paint %u triangles with texture %d (%s coordinates)", unsigned(st.tris.size()), tex,
             g_tv.planar ? "projected" : "kept");
    st.what = "paint";
    if (pushTerrain(st, lb)) {
        g_tv.selTri.clear();
        g_editMsg += " (Apply shows it)";
    }
}

/* the run (a list's G_SETTIMG) of the first selected triangle (or the one pointed at) loads `tex` instead */
void retexRun(int tex) {
    const terrain::Model &m = g_ed.terrain();
    const int t = !g_tv.selTri.empty() ? g_tv.selTri[0] : g_tv.hoverTri;
    const LgTri *s = m.stateOf(t);
    if (s == nullptr || s->timg < 0 || s->frozen) {
        g_editMsg = "That triangle's texture is set by another list (or a bridge's): pick a triangle of the run.";
        return;
    }
    const std::vector<PalEntry> pal = palette();
    auto it = std::find_if(pal.begin(), pal.end(), [&](const PalEntry &e) { return e.tex == tex; });
    if (it != pal.end() && (it->fmt != s->fmt || it->siz != s->siz || it->w != s->w || it->h != s->h)) {
        char w[200];
        snprintf(w, sizeof w, "Texture %d is %s %d-bit %dx%d, the run's is %s %d-bit %dx%d: a run takes a texture of its "
                 "own format and size only.", tex, tc_fmt_name(it->fmt), it->siz, it->w, it->h, tc_fmt_name(s->fmt),
                 s->siz, s->w, s->h);
        g_editMsg = w;
        return;
    }
    terrain::Step st;
    st.kind = terrain::ST_RETEX;
    st.piece = s->piece, st.k = s->timg, st.tex = uint32_t(tex);
    char lb[160];
    snprintf(lb, sizeof lb, "terrain: list %d's texture %d: %d -> %d (every triangle of the run)", s->piece, s->timg,
             s->tex, tex);
    st.what = "retexture";
    if (pushTerrain(st, lb)) g_editMsg += " (Apply shows it)";
}

void followRecords() {
    const std::vector<LmChange> ch = g_ed.followGround();
    if (ch.empty()) {
        g_editMsg = "Every record is where the ground was: nothing to move.";
        return;
    }
    edit::EditStep s;
    s.type = edit::STEP_RECORDS;
    s.recs = ch;
    char lb[160];
    snprintf(lb, sizeof lb, "records on the new ground: %u moved up or down with it", unsigned(ch.size()));
    s.label = lb;
    if (!g_ed.pushStep(s)) {
        g_editMsg = "Not done: " + g_ed.error();
        return;
    }
    g_editMsg = lb;
    host_log("live: editor: %s\n", lb);
    readFileRecords();
}

/* ---- texture slots from PNGs ---- */
std::vector<SlotUse> selectionSlots() {
    std::vector<SlotUse> out;
    const terrain::Model &m = g_ed.terrain();
    std::vector<int> tris = g_tv.selTri;
    if (tris.empty() && g_tv.hoverTri >= 0) tris.push_back(g_tv.hoverTri);
    for (int t : tris) {
        const LgTri *s = m.stateOf(t);
        if (s == nullptr || s->tex < 0) continue;
        bool have = false;
        for (const SlotUse &u : out) have = have || u.tex == s->tex;
        if (!have) out.push_back({s->tex, s->fmt, s->siz, s->w, s->h, s->mips});
    }
    return out;
}

/* the PNGs in the "textures" folder next to bc.exe: picked from a list, a controller can choose one */
std::vector<std::string> g_pngList;
bool g_pngListed;
std::string texturesDir() {
    return exeDir() + "textures\\";
}
void listPngs() {
    g_pngList.clear();
    g_pngListed = true;
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW(wide(texturesDir() + "*.png").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    do {
        if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) g_pngList.push_back(utf8(fd.cFileName));
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    std::sort(g_pngList.begin(), g_pngList.end(), [](const std::string &a, const std::string &b) {
        return _stricmp(a.c_str(), b.c_str()) < 0;
    });
}

std::string pngDialog() {
    std::wstring buf(32768, L'\0');
    OPENFILENAMEW ofn = {};
    ofn.lStructSize = sizeof ofn;
    ofn.lpstrFilter = L"PNG pictures (*.png)\0*.png\0All files (*.*)\0*.*\0";
    ofn.lpstrFile = &buf[0];
    ofn.nMaxFile = DWORD(buf.size());
    ofn.lpstrTitle = L"Blast Corps level editor: a picture for the texture slot";
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR | OFN_HIDEREADONLY;
    if (!GetOpenFileNameW(&ofn)) return std::string();
    buf.resize(wcslen(buf.c_str()));
    return utf8(buf);
}

/* the PNG for the slot (its size and format from the lists that use it): converted, packed the slot's way,
 * checked against its room; the preview is what the slot will hold */
bool importLoad(const std::string &path, const SlotUse &u, bool asNew) {
    g_tv.importShow.clear();
    g_tv.importPng64.clear();
    unsigned n = 0;
    uint8_t *file = static_cast<uint8_t *>(host_read_file(path.c_str(), &n));
    if (file == nullptr) {
        g_tv.importMsg = "Can't read " + path;
        return false;
    }
    uint32_t w = 0, h = 0;
    uint8_t *rgba = nullptr;
    char err[300];
    const int r = tc_png_decode(file, n, 1, &w, &h, &rgba, err, sizeof err);
    free(file);
    if (r) {
        g_tv.importMsg = std::string("The PNG doesn't read: ") + err;
        return false;
    }
    if (int(w) != u.w || int(h) != u.h) {
        char b[200];
        snprintf(b, sizeof b, "The picture is %ux%u; texture %d is drawn %dx%d: make it that size.", w, h, u.tex, u.w, u.h);
        g_tv.importMsg = b;
        free(rgba);
        return false;
    }
    /* stored as an 8-bit RGBA PNG (what bcfiles reads): the mod entry */
    size_t pn = 0;
    uint8_t *png = tc_png_encode(rgba, w, h, &pn);
    char *b64 = png ? tc_base64_encode(png, pn) : nullptr;
    free(png);
    edit::TexSlot ts;
    ts.id = u.tex, ts.fmt = u.fmt, ts.siz = u.siz;
    ts.mips = u.mips > 1 && u.siz != 32 ? u.mips : 0;   /* (its smaller levels made from the picture) */
    uint32_t off, room;
    unsigned size, packing;
    char sha[41];
    if (asNew) {
        /* a slot of the port's own (an empty table entry), for the selection's run */
        std::vector<int> taken;
        for (const edit::TexSlot &t : g_ed.textures()) taken.push_back(t.id);
        ts.id = port_tex_new_id(taken.data(), int(taken.size()));
        ts.isNew = true;
        ts.from = u.tex;
        lm_sha1_hex(reinterpret_cast<const uint8_t *>(""), 0, sha);
        packing = 0, room = 0;
        if (ts.id < 0 || b64 == nullptr) {
            g_tv.importMsg = "No empty table entry is left for a new slot.";
            free(b64), free(rgba);
            return false;
        }
    } else
    if (b64 == nullptr || port_tex_slot(u.tex, &off, &size, &packing, &room, sha)) {
        g_tv.importMsg = "Texture slot " + std::to_string(u.tex) + " isn't in this ROM's table.";
        free(b64), free(rgba);
        return false;
    }
    ts.sha1 = sha;
    ts.png = b64;
    free(b64);
    LmTexture lt;
    memset(&lt, 0, sizeof lt);
    lt.id = ts.id, lt.fmt = ts.fmt, lt.siz = ts.siz, lt.isNew = ts.isNew, lt.from = ts.from, lt.mips = ts.mips;
    snprintf(lt.slot_sha1, sizeof lt.slot_sha1, "%s", sha);
    lt.png = const_cast<char *>(ts.png.c_str());
    uint8_t *packed = nullptr;
    size_t packedN = 0;
    if (port_tex_pack(&lt, &packed, &packedN, err, sizeof err)) {
        g_tv.importMsg = err;
        free(rgba);
        return false;
    }
    /* what the game will decode: the packed bytes back to texels, then to RGBA */
    uint8_t *tex = nullptr;
    size_t tn = 0;
    if (tc_decode(packed, packedN, int(packing), nullptr, 0, &tex, &tn) == 0) {
        if (ts.mips > 1) {
            /* (a mipmapped slot holds level 0's odd rows with their words swapped: back for the picture) */
            TcMipLevel l0;
            tc_mip_layout(w, h, u.siz, 1, &l0);
            for (uint32_t y = 1; y < h; y += 2)
                for (uint32_t q = 0; q + 8 <= l0.line && size_t(y + 1) * l0.line <= tn; q += 8) {
                    uint8_t *row = tex + size_t(y) * l0.line + q, t4[4];
                    memcpy(t4, row, 4), memcpy(row, row + 4, 4), memcpy(row + 4, t4, 4);
                }
        }
        g_tv.importShow.assign(size_t(w) * h * 4, 0);
        tc_to_rgba8(tex, tn, u.fmt, u.siz, w, h, nullptr, 0, g_tv.importShow.data());
        free(tex);
    }
    free(packed), free(rgba);
    g_tv.importW = w, g_tv.importH = h, g_tv.importPacked = packedN, g_tv.importRoom = asNew ? uint32_t(packedN) : room;
    g_tv.importTex = ts;
    char b[300];
    if (asNew)
        snprintf(b, sizeof b, "A new slot %d (%s %d-bit %dx%d stored, %u bytes; an empty table entry) for the run of "
                 "texture %d: \"Use it\" puts it there.", ts.id, tc_fmt_name(u.fmt), u.siz, u.w, u.h, unsigned(packedN), u.tex);
    else if (packedN > room)
        snprintf(b, sizeof b, "Texture %d (%s %d-bit %dx%d, packing %u%s): the picture packs to %u bytes, more than the "
                 "slot's %u: the slot grows (bc.exe reads it whole; a ROM export moves the textures after it).",
                 u.tex, tc_fmt_name(u.fmt), u.siz, u.w, u.h, packing,
                 ts.mips > 1 ? ", drawn with smaller copies far away: made from it" : "", unsigned(packedN), unsigned(room));
    else
        snprintf(b, sizeof b, "Texture %d (%s %d-bit %dx%d, packing %u%s): the picture packs to %u bytes of the slot's %u.",
                 u.tex, tc_fmt_name(u.fmt), u.siz, u.w, u.h, packing,
                 ts.mips > 1 ? ", drawn with smaller copies far away: made from it" : "", unsigned(packedN), unsigned(room));
    g_tv.importMsg = b;
    g_tv.importPng64 = ts.png;
    return true;
}

void importAdd() {
    if (g_tv.importPng64.empty()) return;
    edit::EditStep s;
    s.type = edit::STEP_TEXTURE;
    s.tex = g_tv.importTex;
    std::string name = g_tv.importPath;
    const size_t sl = name.find_last_of("\\/");
    if (sl != std::string::npos) name = name.substr(sl + 1);
    s.label = "texture " + std::to_string(s.tex.id) + ": " + name;
    if (s.tex.isNew) s.label = "new texture slot " + std::to_string(s.tex.id) + ": " + name;
    if (!g_ed.pushStep(s)) {
        g_editMsg = "Not done: " + g_ed.error();
        return;
    }
    g_editMsg = s.label + " (Apply shows it; Save mod keeps it)";
    host_log("live: editor: %s (%u packed bytes)\n", s.label.c_str(), unsigned(g_tv.importPacked));
    if (s.tex.isNew) retexRun(s.tex.id);   /* (a new slot is drawn where a run's G_SETTIMG names it) */
    g_tv.importPng64.clear();
    g_tv.importShow.clear();
}

/* ---- mouse, keys, controller, the tick ---- */
void terrainMouseDown(float x, float y, bool shift, bool ctrl) {
    g_tv.mx = x, g_tv.my = y, g_tv.padAim = false;
    terrainHover(x, y);
    switch (g_tv.tool) {
    case TT_RAISE: case TT_LOWER: case TT_SMOOTH: case TT_FLATTEN:
        if (g_tv.tool == TT_FLATTEN && (shift || !g_tv.flatPicked)) {
            if (g_tv.hoverOk) {
                g_tv.flatY = std::round(g_tv.hover[1]);
                g_tv.flatPicked = true;
                g_editMsg = "Flatten to height " + std::to_string(int(g_tv.flatY));
            }
            if (shift) return;
        }
        if (!g_tv.hoverOk) return;
        g_tv.stroking = true;
        g_tv.cur.clear();
        g_tv.shown.clear(), g_tv.shownApps = ~0u;
        g_tv.skipped = 0;
        g_tv.tickAcc = 0;
        g_tv.applications = 0;
        applyBrushOnce(g_tv.rate / 30.0);
        break;
    case TT_RAMP:
        if (!g_tv.hoverOk) return;
        if (g_tv.rampN >= 2) g_tv.rampN = 0;
        if (g_tv.rampN == 0) {
            memcpy(g_tv.rampA, g_tv.hover, sizeof g_tv.rampA);
            g_tv.rampA[1] = std::round(g_tv.rampA[1]);
            g_tv.rampN = 1;
            g_editMsg = "Ramp: its start is set; click its end (it rises by the \"rise\" above the ground there).";
        } else {
            memcpy(g_tv.rampB, g_tv.hover, sizeof g_tv.rampB);
            g_tv.rampB[1] = std::round(g_tv.rampB[1] + g_tv.rampRise);
            g_tv.rampN = 2;
            g_editMsg = "Ramp: set its heights in the panel, then Make the ramp.";
        }
        break;
    case TT_SELECT: case TT_PAINT:
        selectTri(g_tv.hoverOk ? g_tv.hoverTri : -1, g_tv.hover, shift || ctrl);
        break;
    default:
        break;
    }
}

void terrainMouseUp() {
    if (g_tv.stroking) endStroke();
}

void terrainTick(double dt) {
    if (!terrainMode()) {
        if (g_tv.stroking) g_tv.stroking = false, g_tv.cur.clear();
        return;
    }
    terrainAim();
    if (!g_tv.stroking) return;
    /* 30 applications a second, whatever the frame rate */
    g_tv.tickAcc += dt;
    while (g_tv.tickAcc >= 1.0 / 30.0) {
        g_tv.tickAcc -= 1.0 / 30.0;
        applyBrushOnce(g_tv.rate / 30.0);
    }
}

/* a key of the Terrain mode (true: used) */
bool terrainKey(SDL_Scancode sc, bool ctrl, bool shift) {
    if (!terrainMode() || ctrl) return false;
    if (sc >= SDL_SCANCODE_1 && sc <= SDL_SCANCODE_7) {
        g_tv.tool = int(sc - SDL_SCANCODE_1);
        g_tv.rampN = 0;
        return true;
    }
    if (sc == SDL_SCANCODE_LEFTBRACKET || sc == SDL_SCANCODE_RIGHTBRACKET) {
        const double f = sc == SDL_SCANCODE_LEFTBRACKET ? 1 / 1.25 : 1.25;
        g_tv.brush.radius = std::max(10.0, std::min(4000.0, g_tv.brush.radius * f));
        return true;
    }
    if (sc == SDL_SCANCODE_DELETE || sc == SDL_SCANCODE_BACKSPACE) {
        g_tv.selTri.clear(), g_tv.selPos.clear(), g_tv.rampN = 0;
        return true;
    }
    if (sc == SDL_SCANCODE_X && !shift) {
        splitSelection();
        return true;
    }
    if (sc == SDL_SCANCODE_PAGEUP || sc == SDL_SCANCODE_PAGEDOWN) {
        const int d = (shift ? 1 : 8) * (sc == SDL_SCANCODE_PAGEUP ? 1 : -1);
        moveSelection(0, d, 0);
        return true;
    }
    if (sc == SDL_SCANCODE_RETURN && g_tv.tool == TT_RAMP && g_tv.rampN == 2) {
        makeRamp();
        return true;
    }
    return false;
}

/* a controller in the Terrain mode: the tool works at the picture's centre (the left stick forward moves along the
 * ground, so the point moves: viewer_input.cpp); A held: the brush (a click for the others), X: the next tool,
 * shoulders: the brush's radius, D-pad up / down: the selection up / down 8 (Select), D-pad left / right: the
 * brush's strength (or amount), B: the selection or the ramp let go; the rest (split, make the ramp, paint, Apply,
 * Save...) on the panels (Start) */
void terrainPad(unsigned short buttons, unsigned short pressed) {
    static bool held;
    const bool a = (buttons & (1u << SDL_CONTROLLER_BUTTON_A)) != 0;
    if (pressed) g_tv.padAim = true;
    if (pressed & (1u << SDL_CONTROLLER_BUTTON_X)) g_tv.tool = (g_tv.tool + 1) % TT_COUNT, g_tv.rampN = 0;
    if (pressed & (1u << SDL_CONTROLLER_BUTTON_B)) g_tv.selTri.clear(), g_tv.selPos.clear(), g_tv.rampN = 0;
    if (pressed & ((1u << SDL_CONTROLLER_BUTTON_DPAD_UP) | (1u << SDL_CONTROLLER_BUTTON_DPAD_DOWN))) {
        if (g_tv.tool == TT_SELECT && (!g_tv.selTri.empty() || !g_tv.selPos.empty()))
            g_after.push_back([up = (pressed & (1u << SDL_CONTROLLER_BUTTON_DPAD_UP)) != 0] { moveSelection(0, up ? 8 : -8, 0); });
        else if (g_tv.tool == TT_RAMP)
            g_tv.rampRise = std::max(-400.0, std::min(400.0, g_tv.rampRise + ((pressed & (1u << SDL_CONTROLLER_BUTTON_DPAD_UP)) ? 10 : -10)));
    }
    if (pressed & ((1u << SDL_CONTROLLER_BUTTON_DPAD_LEFT) | (1u << SDL_CONTROLLER_BUTTON_DPAD_RIGHT))) {
        const double f = (pressed & (1u << SDL_CONTROLLER_BUTTON_DPAD_RIGHT)) ? 1.25 : 1 / 1.25;
        if (g_tv.tool == TT_RAISE || g_tv.tool == TT_LOWER) g_tv.rate = std::max(2.0, std::min(600.0, g_tv.rate * f));
        else if (g_tv.tool == TT_SMOOTH || g_tv.tool == TT_FLATTEN) g_tv.amount = std::max(0.02, std::min(1.0, g_tv.amount * f));
    }
    if (pressed & (1u << SDL_CONTROLLER_BUTTON_LEFTSHOULDER)) g_tv.brush.radius = std::max(10.0, g_tv.brush.radius / 1.25);
    if (pressed & (1u << SDL_CONTROLLER_BUTTON_RIGHTSHOULDER)) g_tv.brush.radius = std::min(4000.0, g_tv.brush.radius * 1.25);
    if (a && !held) {
        const ImGuiIO &io = ImGui::GetIO();
        g_tv.padAim = true;
        if (g_tv.tool == TT_RAMP && g_tv.rampN == 2) g_after.push_back(makeRamp);   /* (A: start, A: end, A: make it) */
        else terrainMouseDown(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f, false, false);
        g_tv.padAim = true;
    }
    if (!a && held) terrainMouseUp();
    held = a;
}

/* ---- the overlay ---- */
void drawTerrainOverlay(ImDrawList *dl) {
    const terrain::Model &m = g_ed.terrain();
    const auto &ps = m.positions();
    auto P = [&](int p, ImVec2 &s) {
        const terrain::P3 v = shownValue(p);
        return projectPoint({double(v.x), double(v.y), double(v.z)}, s);
    };
    const Vec eye = g_cam.pos;
    auto nearEye = [&](int p, double r) {
        const double dx = ps[p].cur.x - eye.x, dz = ps[p].cur.z - eye.z;
        return dx * dx + dz * dz < r * r;
    };
    /* protected parts (bridges, moving parts, switches, gates) near the camera: red */
    unsigned shown = 0;
    for (size_t i = 0; i < m.drawn().size() && shown < 4000; i++) {
        const terrain::DrawTri &t = m.drawn()[i];
        if (t.removed) continue;
        const LgTri *s = m.stateOf(int(i));
        const bool prot = (s && s->frozen) || ps[t.pos[0]].prot || ps[t.pos[1]].prot || ps[t.pos[2]].prot;
        if (!prot || !nearEye(t.pos[0], 2500)) continue;
        ImVec2 a, b, c;
        if (!P(t.pos[0], a) || !P(t.pos[1], b) || !P(t.pos[2], c)) continue;
        dl->AddTriangle(a, b, c, IM_COL32(255, 60, 60, 170), 1.5f);
        shown++;
    }
    /* the selection */
    for (int t : g_tv.selTri) {
        const terrain::DrawTri &d = m.drawn()[t];
        ImVec2 a, b, c;
        if (P(d.pos[0], a) && P(d.pos[1], b) && P(d.pos[2], c)) {
            dl->AddTriangleFilled(a, b, c, IM_COL32(255, 230, 60, 60));
            dl->AddTriangle(a, b, c, IM_COL32(255, 230, 60, 230), 2.0f);
        }
    }
    for (int p : g_tv.selPos) {
        ImVec2 s;
        if (P(p, s)) dl->AddCircleFilled(s, 5.0f, IM_COL32(255, 230, 60, 255), 10);
    }
    /* the triangle pointed at (select / paint) */
    if ((g_tv.tool == TT_SELECT || g_tv.tool == TT_PAINT) && g_tv.hoverTri >= 0) {
        const terrain::DrawTri &d = m.drawn()[g_tv.hoverTri];
        ImVec2 a, b, c;
        if (P(d.pos[0], a) && P(d.pos[1], b) && P(d.pos[2], c)) dl->AddTriangle(a, b, c, IM_COL32(255, 255, 255, 220), 1.5f);
    }
    /* the brush: a circle on the ground (and its full-strength core) */
    if (g_tv.tool <= TT_FLATTEN && g_tv.hoverOk) {
        static const ImU32 cols[4] = {IM_COL32(90, 255, 120, 230), IM_COL32(255, 140, 60, 230), IM_COL32(120, 200, 255, 230),
                                      IM_COL32(255, 255, 120, 230)};
        for (int ring = 0; ring < 2; ring++) {
            const double r = ring == 0 ? g_tv.brush.radius : g_tv.brush.radius * (1.0 - g_tv.brush.falloff);
            if (r < 2) continue;
            std::vector<ImVec2> pts;
            for (int i = 0; i <= 64; i++) {
                const double a = i * 2.0 * 3.14159265358979 / 64;
                const double x = g_tv.hover[0] + std::cos(a) * r, z = g_tv.hover[2] + std::sin(a) * r;
                double y = g_tv.hover[1];
                m.heightAt(x, z, y);
                ImVec2 s;
                if (!projectPoint({x, y + 2, z}, s)) {
                    pts.clear();
                    break;
                }
                pts.push_back(s);
            }
            if (!pts.empty())
                dl->AddPolyline(pts.data(), int(pts.size()), ring == 0 ? cols[g_tv.tool] : (cols[g_tv.tool] & 0x80FFFFFFu), 0,
                                ring == 0 ? 2.5f : 1.0f);
        }
        ImVec2 c;
        if (projectPoint({g_tv.hover[0], g_tv.hover[1], g_tv.hover[2]}, c)) dl->AddCircleFilled(c, 3.0f, cols[g_tv.tool], 8);
    }
    /* the ramp */
    if (g_tv.tool == TT_RAMP) {
        ImVec2 a, b;
        if (g_tv.rampN >= 1 && projectPoint({g_tv.rampA[0], g_tv.rampA[1], g_tv.rampA[2]}, a))
            dl->AddCircleFilled(a, 6.0f, IM_COL32(120, 255, 255, 255), 12);
        if (g_tv.rampN >= 2 && projectPoint({g_tv.rampB[0], g_tv.rampB[1], g_tv.rampB[2]}, b)) {
            dl->AddCircleFilled(b, 6.0f, IM_COL32(120, 255, 255, 255), 12);
            if (g_tv.rampN >= 2 && projectPoint({g_tv.rampA[0], g_tv.rampA[1], g_tv.rampA[2]}, a))
                dl->AddLine(a, b, IM_COL32(120, 255, 255, 220), 3.0f);
        }
        if (g_tv.hoverOk) {
            ImVec2 c;
            if (projectPoint({g_tv.hover[0], g_tv.hover[1], g_tv.hover[2]}, c)) {
                dl->AddCircle(c, 6.0f, IM_COL32(120, 255, 255, 200), 12);
                /* (its start set: where the end would go - the cross, moved with the stick) */
                if (g_tv.rampN == 1 && projectPoint({g_tv.rampA[0], g_tv.rampA[1], g_tv.rampA[2]}, a))
                    dl->AddLine(a, c, IM_COL32(120, 255, 255, 120), 2.0f);
            }
        }
    }
    /* the controller's aim */
    if (g_tv.padAim) {
        const ImGuiIO &io = ImGui::GetIO();
        const ImVec2 c(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f);
        dl->AddLine(ImVec2(c.x - 8, c.y), ImVec2(c.x + 8, c.y), IM_COL32(255, 255, 255, 200), 1.5f);
        dl->AddLine(ImVec2(c.x, c.y - 8), ImVec2(c.x, c.y + 8), IM_COL32(255, 255, 255, 200), 1.5f);
    }
}

/* ---- the panel ---- */
/* a grey hint, wrapped to the panel */
void hint(const char *s) {
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    ImGui::TextWrapped("%s", s);
    ImGui::PopStyleColor();
}

void terrainPanel() {
    const ImGuiIO &io = ImGui::GetIO();
    const float f = uiFontPx() * kPanelScale;
    ImGui::SetNextWindowPos(ImVec2(f * 22, f * 0.5f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(f * 22, std::min(io.DisplaySize.y * 0.9f, f * 44)), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowBgAlpha(0.85f);
    if (!ImGui::Begin("Terrain", nullptr, ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing)) {
        ImGui::End();
        return;
    }
    ImGui::SetWindowFontScale(kPanelScale);
    for (int t = 0; t < TT_COUNT; t++) {
        if (t && t != TT_RAMP) ImGui::SameLine();   /* (the brushes on one line, the others on the next) */
        if (ImGui::RadioButton(kToolNames[t], g_tv.tool == t)) g_tv.tool = t, g_tv.rampN = 0;
    }
    hint("Keys 1-7; [ ] brush size. Controller: X next tool, A use (at the cross; Ramp: start, end, make), shoulders: "
         "size, D-pad left / right: strength, up / down: the selection up / down (Select) or the ramp's rise, B: let "
         "go; Start: these panels.");
    float r = float(g_tv.brush.radius), fo = float(g_tv.brush.falloff);
    if (g_tv.tool <= TT_RAMP) {
        if (ImGui::SliderFloat(g_tv.tool == TT_RAMP ? "Half width" : "Radius", &r, 10.0f, 4000.0f, "%.0f",
                               ImGuiSliderFlags_Logarithmic))
            g_tv.brush.radius = r;
        if (ImGui::SliderFloat("Falloff", &fo, 0.0f, 1.0f, "%.2f")) g_tv.brush.falloff = fo;
    }
    if (g_tv.tool == TT_RAISE || g_tv.tool == TT_LOWER) {
        float s = float(g_tv.rate);
        if (ImGui::SliderFloat("Strength", &s, 2.0f, 600.0f, "%.0f units/s", ImGuiSliderFlags_Logarithmic)) g_tv.rate = s;
        hint("Hold the left button and move over the ground.");
    } else if (g_tv.tool == TT_SMOOTH || g_tv.tool == TT_FLATTEN) {
        float a = float(g_tv.amount);
        if (ImGui::SliderFloat("Amount", &a, 0.02f, 1.0f, "%.2f a step")) g_tv.amount = a;
        if (g_tv.tool == TT_FLATTEN) {
            int y = int(g_tv.flatY);
            ImGui::SetNextItemWidth(f * 6);
            if (ImGui::InputInt("Height", &y)) g_tv.flatY = y, g_tv.flatPicked = true;
            ImGui::SameLine();
            hint("(Shift+click: pick)");
        }
    } else if (g_tv.tool == TT_RAMP) {
        hint("Click the ramp's start, then its end; the end rises by:");
        float rise = float(g_tv.rampRise);
        ImGui::SetNextItemWidth(f * 8);
        if (ImGui::SliderFloat("Rise", &rise, -400.0f, 400.0f, "%.0f")) g_tv.rampRise = rise;
        if (g_tv.rampN >= 1) {
            int a[3] = {int(g_tv.rampA[0]), int(g_tv.rampA[1]), int(g_tv.rampA[2])};
            ImGui::SetNextItemWidth(f * 12);
            if (ImGui::InputInt3("Start", a)) g_tv.rampA[0] = a[0], g_tv.rampA[1] = a[1], g_tv.rampA[2] = a[2];
        }
        if (g_tv.rampN >= 2) {
            int b[3] = {int(g_tv.rampB[0]), int(g_tv.rampB[1]), int(g_tv.rampB[2])};
            ImGui::SetNextItemWidth(f * 12);
            if (ImGui::InputInt3("End", b)) g_tv.rampB[0] = b[0], g_tv.rampB[1] = b[1], g_tv.rampB[2] = b[2];
            if (ImGui::Button("Make the ramp (Enter)")) g_after.push_back(makeRamp);
            ImGui::SameLine();
        }
        if (g_tv.rampN > 0 && ImGui::Button("Clear")) g_tv.rampN = 0;
    }
    if (g_tv.tool == TT_SELECT || g_tv.tool == TT_PAINT) {
        if (g_tv.tool == TT_SELECT) {
            if (ImGui::RadioButton("Triangles", !g_tv.selVerts)) g_tv.selVerts = false, g_tv.selPos.clear();
            ImGui::SameLine();
            if (ImGui::RadioButton("Corners", g_tv.selVerts)) g_tv.selVerts = true, g_tv.selTri.clear();
        }
        ImGui::Text("%u triangle%s, %u corner%s selected (click; Shift/Ctrl+click adds; Del clears)",
                    unsigned(g_tv.selTri.size()), g_tv.selTri.size() == 1 ? "" : "s", unsigned(selCorners().size()),
                    selCorners().size() == 1 ? "" : "s");
    }
    if (g_tv.tool == TT_SELECT) {
        ImGui::SetNextItemWidth(f * 12);
        ImGui::InputInt3("Move by", g_tv.moveBy);
        ImGui::SameLine();
        if (ImGui::Button("Move")) {
            const int dx = g_tv.moveBy[0], dy = g_tv.moveBy[1], dz = g_tv.moveBy[2];
            g_after.push_back([dx, dy, dz] { moveSelection(dx, dy, dz); });
        }
        hint("PageUp / PageDown: up / down 8 (Shift: 1).");
        if (ImGui::Button("Split the selected triangles (X)")) g_after.push_back(splitSelection);
        hint("Each into 4 (and the floor under it): detail for the brushes. Neighbours and walls on the edges split "
             "along them, so no seams open.");
        if (g_tv.seams) {
            char b[96];
            snprintf(b, sizeof b, "Close seams (%u)", unsigned(g_tv.seams));
            if (ImGui::Button(b)) g_after.push_back(sealSeams);
            hint("Corners lying on another triangle's edge (splits made before): hairline cracks. This splits the "
                 "triangles there at them.");
        }
    }
    if (g_tv.tool == TT_PAINT) {
        const std::vector<PalEntry> pal = palette();
        static std::vector<int> here;
        static std::vector<int> hereFor;
        if (hereFor != g_tv.selTri) here = texturesHere(), hereFor = g_tv.selTri;
        ImGui::BeginChild("palette", ImVec2(0, f * 9), true);
        for (int pass = 0; pass < 2; pass++)
            for (const PalEntry &e : pal) {
                const bool h = std::find(here.begin(), here.end(), e.tex) != here.end();
                if (h != (pass == 0)) continue;
                char b[112];
                snprintf(b, sizeof b, "%4d  %s %d-bit %dx%d  (%d)%s", e.tex, tc_fmt_name(e.fmt), e.siz, e.w, e.h, e.count,
                         h ? "  here" : "");
                if (!h) ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.6f, 0.6f, 0.6f, 1));
                if (ImGui::Selectable(b, g_tv.paintTex == e.tex)) g_tv.paintTex = e.tex;
                if (!h) ImGui::PopStyleColor();
            }
        ImGui::EndChild();
        hint("\"here\": textures the selection can be painted with (drawn by a list where it is).");
        ImGui::Checkbox("Project the coordinates from above (else keep them)", &g_tv.planar);
        ImGui::BeginDisabled(g_tv.paintTex < 0);
        if (ImGui::Button("Paint the selection")) {
            const int t = g_tv.paintTex;
            g_after.push_back([t] { paintSelection(t); });
        }
        ImGui::SameLine();
        if (ImGui::Button("Switch the run")) {
            const int t = g_tv.paintTex;
            g_after.push_back([t] { retexRun(t); });
        }
        ImGui::EndDisabled();
        hint("Paint: the triangles join a run of this texture in their list (or one drawn where they "
                            "are). Switch: every triangle of the selected one's run.");
    }
    if (!g_tv.importShow.empty()) ImGui::SetNextItemOpen(true, ImGuiCond_Always);   /* (a picture waiting: shown) */
    if (ImGui::CollapsingHeader("Texture slot from a PNG")) {
        const std::vector<SlotUse> slots = selectionSlots();
        if (slots.empty()) hint("Select (or point at) triangles: their textures' slots are listed here.");
        for (const SlotUse &u : slots) {
            uint32_t off = 0, room = 0;
            unsigned size = 0, packing = 0;
            port_tex_slot(u.tex, &off, &size, &packing, &room, nullptr);
            char b[160];
            snprintf(b, sizeof b, "slot %d: %s %d-bit %dx%d, packing %u, %u of %u bytes", u.tex, tc_fmt_name(u.fmt), u.siz,
                     u.w, u.h, packing, size, room);
            if (ImGui::Selectable(b, g_tv.importSlot == u.tex)) g_tv.importSlot = u.tex;
        }
        ImGui::SetNextItemWidth(f * 14);
        ImGui::InputText("##png", g_tv.importPath, sizeof g_tv.importPath);
        ImGui::SameLine();
        if (ImGui::Button("Browse...")) {
            const std::string p = pngDialog();
            if (!p.empty()) snprintf(g_tv.importPath, sizeof g_tv.importPath, "%s", p.c_str());
        }
        ImGui::SameLine();
        if (ImGui::Button("Keys...##png")) ImGui::OpenPopup("The picture's path");   /* (a controller's keyboard) */
        nameKeyboard("The picture's path", g_tv.importPath, sizeof g_tv.importPath, true);
        /* (a controller: the pictures of the textures folder as a list) */
        if (!g_pngListed) listPngs();
        if (g_pngList.empty()) {
            hint(("Or put PNG pictures in " + texturesDir() + " and pick them here (a controller can).").c_str());
        } else {
            ImGui::BeginChild("pngs", ImVec2(0, f * std::min(6.0f, 1.3f * float(g_pngList.size()) + 0.5f)), true);
            const std::string dir = texturesDir();
            for (const std::string &name : g_pngList) {
                const std::string full = dir + name;
                if (ImGui::Selectable(name.c_str(), full == g_tv.importPath))
                    snprintf(g_tv.importPath, sizeof g_tv.importPath, "%s", full.c_str());
            }
            ImGui::EndChild();
        }
        if (ImGui::Button("Look in the textures folder again")) listPngs();
        ImGui::BeginDisabled(g_tv.importSlot < 0 || g_tv.importPath[0] == 0);
        if (ImGui::Button("Load into the slot")) {
            for (const SlotUse &u : slots)
                if (u.tex == g_tv.importSlot) importLoad(g_tv.importPath, u);
        }
        ImGui::SameLine();
        if (ImGui::Button("... as a new slot for the run")) {
            for (const SlotUse &u : slots)
                if (u.tex == g_tv.importSlot) importLoad(g_tv.importPath, u, true);
        }
        ImGui::EndDisabled();
        hint("A slot keeps its id: every list that draws it shows the picture. A new slot (bc.exe's own) "
                            "is drawn by the selected triangle's run only; such a mod can't go into a ROM.");
        if (!g_tv.importMsg.empty()) ImGui::TextWrapped("%s", g_tv.importMsg.c_str());
        if (!g_tv.importShow.empty()) {
            /* what the slot will hold, texel by texel */
            const float px = std::max(1.0f, std::min(6.0f, f * 10 / float(std::max(g_tv.importW, g_tv.importH))));
            const ImVec2 o = ImGui::GetCursorScreenPos();
            ImDrawList *wl = ImGui::GetWindowDrawList();
            for (uint32_t y = 0; y < g_tv.importH; y++)
                for (uint32_t x = 0; x < g_tv.importW; x++) {
                    const uint8_t *c = &g_tv.importShow[(size_t(y) * g_tv.importW + x) * 4];
                    wl->AddRectFilled(ImVec2(o.x + x * px, o.y + y * px), ImVec2(o.x + (x + 1) * px, o.y + (y + 1) * px),
                                      IM_COL32(c[0], c[1], c[2], 255));
                }
            ImGui::Dummy(ImVec2(g_tv.importW * px, g_tv.importH * px));
            if (ImGui::Button("Use it (into the mod)")) g_after.push_back(importAdd);
        }
    }
    if (ImGui::CollapsingHeader("Records and room", ImGuiTreeNodeFlags_DefaultOpen)) {
        const size_t nf = g_ed.followGround().size();
        char b[120];
        snprintf(b, sizeof b, "Put records on the new ground (%u)", unsigned(nf));
        ImGui::BeginDisabled(nf == 0);
        if (ImGui::Button(b)) g_after.push_back(followRecords);
        ImGui::EndDisabled();
        hint("Vehicle spawns, buildings, RDUs, TNT, crates ... move up or down with the ground under them.");
        const size_t sz = g_ed.image().size(), o = g_ed.origSize();
        const double used = sz > o ? double(sz - o) / double(edit::Editor::kMaxGrowth) : 0.0;
        ImGui::Text("Level file %u bytes (the ROM's %u; room for %u more: %.0f%% used)", unsigned(sz), unsigned(o),
                    unsigned(edit::Editor::kMaxGrowth), used * 100.0);
        for (int k = 0; k < 4; k++) {
            if (g_ed.listCap[k] <= 0) {
                if (k == 0) hint("Visible lists: this level sizes them itself.");
                continue;
            }
            const int cap = std::max(g_ed.listCap[k], g_ed.listOwn[k]);
            const double u = double(g_ed.listN[k]) / cap;
            if (g_ed.listOwn[k] > g_ed.listCap[k])
                ImGui::TextColored(u > 0.999 ? ImVec4(1, 0.4f, 0.3f, 1) : ImVec4(0.8f, 0.8f, 0.8f, 1),
                                   "Visible list %d: %d commands (the game allocates %d; the level's own %d: no more)", k,
                                   g_ed.listN[k], g_ed.listCap[k], g_ed.listOwn[k]);
            else
                ImGui::TextColored(u > 0.9 ? ImVec4(1, 0.4f, 0.3f, 1) : ImVec4(0.8f, 0.8f, 0.8f, 1),
                                   "Visible list %d: %d of %d commands", k, g_ed.listN[k], g_ed.listCap[k]);
        }
    }
    ImGui::Separator();
    ImGui::TextWrapped("Preview: %u corner%s shown moved%s. Apply: the level rebuilt and set up again by the game "
                       "(collision, splits, paint, texture slots).",
                       g_tv.previewed, g_tv.previewed == 1 ? "" : "s", g_ed.dirty() ? " (not applied yet)" : "");
    ImGui::Checkbox("Apply after every stroke (a quarter of a second each)", &g_tv.applyEach);
    if (!g_tv.strokeMs.empty() || g_applyMs > 0)
        ImGui::TextDisabled("Last step rebuilt in %.0f ms; last Apply %.1f s.",
                            g_tv.strokeMs.empty() ? 0.0 : g_tv.strokeMs.back(), g_applyMs / 1000.0);
    if (!g_editMsg.empty()) ImGui::TextWrapped("%s", g_editMsg.c_str());
    ImGui::End();
}

}  // namespace vw
}  // namespace live
