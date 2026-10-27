/* bc.exe's level and model viewer (see live_viewer.h).
 *
 * States:
 *  - off;
 *  - load: a level was asked for (host_viewer_request: the game takes it at the end of a frame in a level or the
 *    attract demo, game/port.h PORT_VIEWER_FRAME) and the game runs, unpaced, silent, with no controller
 *    input, until that level has been in play (or its start) for a few frames;
 *  - capture: the game runs two more frames with every cell of the level in the drawing paths
 *    (PORT_DRAW_CELLS: as many as the game's display-list buffers hold) and its PORT_ITAG tags recorded;
 *  - frozen: the game waits inside the retrace hook (viewerVi, called from live_rt64.cpp liveVi) and the viewer
 *    runs its own loop: the graphics tasks of the last captured frame go to RT64 again every tick, the free
 *    camera's view replacing the game's (RT64::ViewHook), the categories turned off left out of the drawing
 *    (the command hook, live_interp.h), the far plane moved out; the panels and overlays are ImGui over the
 *    picture.  RDRAM is only read.
 * Leaving a viewer opened over play lets the game go on from where it was held (two frames on: the capture);
 * once the viewer loaded a level, leaving restarts bc.exe (ViewerEnv::restart).
 *
 * Its parts (the editor, the terrain tools, the panels, input, the test script): viewer_internal.h.
 *
 * No GPL code: written against RT64's (MIT) API and the game's own data. */
#include <windows.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <functional>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "gbi/rt64_display_list.h"
#include "gbi/rt64_gbi.h"
#include "hle/rt64_application.h"
#include "hle/rt64_rsp.h"
#include "hle/rt64_state.h"
#include "hle/rt64_workload_queue.h"
#include "imgui/imgui.h"
#include "imgui/imgui_internal.h"   /* (the controller's panel when the frozen loop ends: NavWindow) */
#include "render/rt64_projection_processor.h"

#include "live_interp.h"
#include "live_menu.h"
#include "live_orbit.h"
#include "live_wide.h"
#include "viewer_internal.h"

extern "C" {
#include "plat_host.h"
unsigned plat_frames(void); /* save.c */
}

namespace live {
namespace vw {

void mtxToDouble(uint32_t addr, double m[4][4]) {
    orbit::mtxFromN64(reinterpret_cast<const uint32_t *>(uintptr_t(addr)), m);
}

ViewerEnv g_env;

int catOf(uint32_t kind, bool sky) {
    switch (kind) {
    case K_TERRAIN: return CAT_TERRAIN;
    case K_HUD: case K_RADAR: return CAT_HUD;
    case K_OBJ: case K_SHADOW: case K_SHADOWFE: return CAT_VEHICLES;
    case K_BUILDING: case K_BPART: case K_FALL: case K_RISE: case K_GSHADOW: return CAT_BUILDINGS;
    case K_BILLBOARD: case K_PARTICLE: case K_PEOPLE: case K_MARKER: return CAT_EFFECTS;
    default: return sky ? CAT_SKY : CAT_OTHER;
    }
}

Options g_opt;

/* ---- the state ------------------------------------------------------------------ */
enum State { S_OFF, S_LOAD, S_CAPTURE, S_FROZEN };
State g_state = S_OFF;
bool g_wanted;                    /* the menu's button: open over play */
int g_menuLevel = 0;              /* the menu's level picker */
bool g_loadedLevel;               /* the viewer loaded a level: leaving restarts bc.exe */
int g_bootLevel = -2;             /* --viewer LEVEL (-2: not given) */
int g_wantLevel = -1;             /* the level the load waits for */
unsigned g_loadStart, g_captureFrames, g_freezes, g_replays, g_shots, g_loads;
unsigned g_capturedFrame;         /* plat_frames() of the frozen frame */
bool g_recapture;                 /* a recapture asked for (the frozen loop returns) */
bool g_leave, g_quitAfter;
bool g_resetCam = true;           /* the next freeze starts from the game's camera (a new level, a new entry) */

/* the graphics tasks of the frames */
struct Task {
    unsigned ucode, data, dl;
    bool frameDl;
};
std::vector<Task> g_cur, g_frozenTasks;
/* the tags of the display list being built and of the frozen one (address -> kind, index) */
struct Tag {
    uint32_t kind, index;
};
std::unordered_map<uint32_t, Tag> g_tagsBuilding, g_tags;
bool g_recordTags;
unsigned g_tagCount;

/* the frozen frame */
Frozen g_fz;

/* ---- the records ---------------------------------------------------------------- */
std::vector<Record> g_recs;       /* the level file's, then gObjects', then gVehicles' */
std::vector<std::string> g_notes;
viewer::LevelHeader g_hdr;
int g_sel = -1;                   /* the selected record */
std::string g_levelNames[60];
bool g_namesRead;
std::vector<Tri> g_tris;

/* ---- the camera ----------------------------------------------------------------- */
FlyCam g_cam;
ModelView g_mv;

/* the view the render thread gives RT64 (viewHook) */
ViewShare g_vs;
std::mutex g_vsMutex;

void toFloat(const double m[4][4], float out[4][4]) {
    for (int r = 0; r < 4; r++)
        for (int c = 0; c < 4; c++) out[r][c] = float(m[r][c]);
}

bool sameView(const interop::float4x4 &a, const float b[4][4]) {
    for (int r = 0; r < 4; r++)
        for (int c = 0; c < 4; c++) {
            const double x = double(a[r][c]), y = double(b[r][c]);
            if (std::fabs(x - y) > 1e-3 * std::max(1.0, std::fabs(y))) return false;
        }
    return true;
}

void viewHook(const RT64::ViewHookParams &p, interop::float4x4 &view, interop::float4x4 &proj,
              interop::float4x4 &prevView, interop::float4x4 &prevProj) {
    std::lock_guard<std::mutex> lock(g_vsMutex);
    if (!g_vs.on || !sameView(*p.gameView, g_vs.game)) return;
    for (int r = 0; r < 4; r++)
        for (int c = 0; c < 4; c++) view[r][c] = prevView[r][c] = g_vs.view[r][c];
    double pm[4][4];
    for (int r = 0; r < 4; r++)
        for (int c = 0; c < 4; c++) pm[r][c] = double(proj[r][c]);
    if (g_vs.farOut&& viewer::setFar(pm, kFar))
        for (int r = 0; r < 4; r++)
            for (int c = 0; c < 4; c++) proj[r][c] = float(pm[r][c]);
    for (int r = 0; r < 4; r++)
        for (int c = 0; c < 4; c++) {
            prevProj[r][c] = proj[r][c];
            g_vs.proj[r][c] = double(proj[r][c]);
        }
    g_vs.haveProj = true;
    g_vs.hooked++;
}

/* the camera's view now (model view: orbiting the model) */
void currentView(double m[4][4]) {
    if (g_mv.on) {
        FlyCam c;
        c.yaw = g_mv.yaw, c.pitch = -g_mv.pitch;
        c.lookAt(g_mv.centre, g_mv.dist);
        c.view(m);
        return;
    }
    g_cam.view(m);
}

void publishView() {
    double m[4][4];
    currentView(m);
    std::lock_guard<std::mutex> lock(g_vsMutex);
    toFloat(m, g_vs.view);
    g_vs.farOut= g_opt.farOut;
}

/* ---- the ground (2-D records, the carrier's route) --------------------------------- */
double groundAt(double x, double z, double fallback) {
    const uint32_t cw = rd<uint32_t>(kGroundCellW), cd = rd<uint32_t>(kGroundCellW + 4);
    const int cols = rd<uint16_t>(kGroundCellW + 8), rows = rd<uint16_t>(kGroundCellW + 10);
    if (cw == 0 || cd == 0 || cols <= 0 || rows <= 0 || cols * rows >= 0x10000) return fallback;
    const int cx = std::max(0, std::min(cols - 1, int(std::floor(x * 32.0 / cw))));
    const int cz = std::max(0, std::min(rows - 1, int(std::floor(z * 32.0 / cd))));
    const uint32_t a = rd<uint32_t>(kGroundCells + 4u * uint32_t(cz * cols + cx)),
                   e = rd<uint32_t>(kGroundCells + 4u * uint32_t(cz * cols + cx + 1));
    if (!inRam(a) || e < a + 4 || e - a > 0x40000 || !inRam(e)) return fallback;
    double best = -1e31, h;
    for (uint32_t t = a; t + 0x14 <= e - 4; t += 0x14) {
        const int16_t *v = reinterpret_cast<const int16_t *>(uintptr_t(t));
        const orbit::Vec3 p[3] = {{double(v[0]), double(v[1]), double(v[2])}, {double(v[3]), double(v[4]), double(v[5])},
                                  {double(v[6]), double(v[7]), double(v[8])}};
        if (orbit::triHeight(p, x, z, h)) best = std::max(best, h);
    }
    return best > -1e30 ? best : fallback;
}

/* ---- reading the level ------------------------------------------------------------ */
uint32_t levelData() {
    return rd<uint32_t>(kLevelData);
}

/* the level file's extent: up to the heap pointer (what follows it is the level's own heap: the parser only
 * reads inside the header's sections) */
size_t levelSpan(uint32_t lvl) {
    const uint32_t heap = rd<uint32_t>(kHeapPtr);
    if (!inRam(lvl, 0xC8) || heap <= lvl || !inRam(heap - 1)) return 0;
    return size_t(heap - lvl);
}

void readNames() {
    if (g_namesRead || rd<uint8_t>(kFrontEnd) == 0) return;
    g_namesRead = true;
    unsigned ok = 0;
    for (int i = 0; i < 60; i++) {
        const uint32_t p = rd<uint32_t>(kGlobeLevels + uint32_t(i) * 0x30u + 4);
        std::string s;
        if (inRam(p, 32))
            for (int k = 0; k < 31; k++) {
                const char c = rd<char>(p + uint32_t(k));
                if (c == 0) break;
                if (c < 0x20 || c > 0x7E) {
                    s.clear();
                    break;
                }
                s += c;
            }
        if (!s.empty()) ok++;
        g_levelNames[i] = s;
    }
    host_log("live: viewer: %u level names read from the globe's table\n", ok);
}

std::string kindName(unsigned k) {
    switch (k) {
    case 1: return "mission";
    case 0x80: return "timed mission";
    case 2: return "race";
    case 4: return "targets";
    case 8: return "damage";
    case 0x10: return "RDUs";
    case 0x20: return "buildings";
    case 0x40: return "power-pill maze";
    case 0: return "no kind (sequence or special)";
    default: {
        char b[16];
        snprintf(b, sizeof b, "kind 0x%X", k);
        return b;
    }
    }
}

std::string levelLabel(int i) {
    char b[160];
    const unsigned k = rd<uint8_t>(kLevelInfo + uint32_t(i) * 0x44u);
    snprintf(b, sizeof b, "%2d  %s  (%s)", i, g_levelNames[i].empty() ? "level" : g_levelNames[i].c_str(),
             kindName(k).c_str());
    return b;
}

/* the vehicle a type-1 spawn stands for on a bonus level: the lowest vehicle bit of the level's LevelInfo, else
 * Sideswipe */
int bonusVehicle(int level) {
    const uint32_t bits = rd<uint32_t>(kLevelInfo + uint32_t(level) * 0x44u + 0x2C);
    for (int t = 1; t <= 18; t++)
        if (t != 6 && t != 7 && t != 11 && t != 12 && t < 17 && (bits >> t) & 1) return t;
    return 1;
}

/* ---- the level editor's state (viewer_edit.cpp, viewer_terrain.cpp) --------------------------------- */
edit::Editor g_ed;
bool g_editMode;                  /* the editor's panels, gizmo and keys are on */
std::vector<Record> g_rtRecs;     /* the runtime records (gObjects, gVehicles) as read at the freeze */

/* the level file's records: the editor's copy while it edits this level, else the file in RDRAM */
void readFileRecords() {
    const bool fromEditor = g_editMode && g_ed.isOpen() && g_ed.level() == g_fz.levelNo;
    std::vector<Record> recs;
    if (fromEditor) {
        const std::vector<uint8_t> &img = g_ed.image();
        recs = viewer::parseLevel(img.data(), img.size(), &g_notes, true);
    } else {
        const uint32_t lvl = levelData();
        const size_t span = levelSpan(lvl);
        if (span != 0) recs = viewer::parseLevel(reinterpret_cast<const uint8_t *>(uintptr_t(lvl)), span, &g_notes);
    }
    /* 2-D records on the ground */
    for (Record &r : recs)
        if (r.hasPos && !r.yKnown) r.y = groundAt(r.x, r.z, 0.0) + 2;
    g_recs = recs;
    g_recs.insert(g_recs.end(), g_rtRecs.begin(), g_rtRecs.end());
}

void readRecords() {
    g_recs.clear();
    g_rtRecs.clear();
    g_notes.clear();
    g_tris.clear();
    g_sel = -1;
    const uint32_t lvl = levelData();
    const size_t span = levelSpan(lvl);
    std::string why;
    if (span == 0 || !viewer::parseHeader(reinterpret_cast<const uint8_t *>(uintptr_t(lvl)), span, g_hdr, &why)) {
        g_notes.push_back("no level data: " + why);
        return;
    }
    /* the runtime records */
    const uint32_t oe = rd<uint32_t>(kGObjectsEnd);
    if (oe >= kGObjects && oe <= kGObjects + 0xFCu * 64)
        for (uint32_t o = kGObjects, i = 0; o + 0xFC <= oe; o += 0xFC, i++)
            g_rtRecs.push_back(viewer::gameObjectRecord(reinterpret_cast<const uint8_t *>(uintptr_t(o)), int(i), o));
    const uint32_t ve = rd<uint32_t>(kGVehiclesEnd);
    if (ve >= kGVehicles && ve <= kGVehicles + 0x74u * 32)
        for (uint32_t v = kGVehicles, i = 0; v + 0x74 <= ve; v += 0x74, i++)
            g_rtRecs.push_back(viewer::gameVehicleRecord(reinterpret_cast<const uint8_t *>(uintptr_t(v)), int(i), v));
    readFileRecords();
    /* collision: grids A and B (0x6C, 0x70: per cell a BE u32 end offset, then 0x16-byte triangles of nine BE
     * s16) and the buildings' triangles (gObjects [+4, +8): 0x60-byte records, s32 vertices / 8 at +0x28) */
    const uint8_t *L = reinterpret_cast<const uint8_t *>(uintptr_t(lvl));
    const unsigned cells = g_hdr.collCols * g_hdr.collRows;
    for (int g = 0; g < 2 && cells > 0 && cells < 0x10000; g++) {
        const uint32_t start = g_hdr.offAt(g == 0 ? 0x6C : 0x70);
        if (start >= span) continue;
        uint32_t p = start;
        for (unsigned c = 0; c < cells && p + 4 <= span; c++) {
            const uint32_t end = start + (uint32_t(L[p]) << 24 | uint32_t(L[p + 1]) << 16 | uint32_t(L[p + 2]) << 8 | L[p + 3]);
            if (end < p + 4 || end > span) break;
            for (uint32_t t = p + 4; t + 0x16 <= end; t += 0x16) {
                Tri tr;
                for (int k = 0; k < 9; k++) tr.v[k] = float(int16_t((L[t + 2 * k] << 8) | L[t + 2 * k + 1]));
                tr.src = uint8_t(g);
                g_tris.push_back(tr);
            }
            p = end;
        }
    }
    if (oe >= kGObjects && oe <= kGObjects + 0xFCu * 64)
        for (uint32_t o = kGObjects; o + 0xFC <= oe; o += 0xFC) {
            const uint32_t a0 = rd<uint32_t>(o + 4), a1 = rd<uint32_t>(o + 8);
            if (!inRam(a0) || a1 < a0 || !inRam(a1) || a1 - a0 > 0x60u * 4096) continue;
            for (uint32_t a = a0; a + 0x60 <= a1; a += 0x60) {
                Tri tr;
                const int32_t *v = reinterpret_cast<const int32_t *>(uintptr_t(a + 0x28));
                for (int k = 0; k < 9; k++) tr.v[k] = float(v[k] / 8.0);
                tr.src = 2;
                g_tris.push_back(tr);
            }
        }
    unsigned byKind[viewer::REC_KINDS] = {};
    for (const Record &r : g_recs) byKind[r.kind]++;
    char line[512];
    int n = 0;
    for (int k = 0; k < viewer::REC_KINDS && n < 480; k++)
        if (byKind[k]) n += snprintf(line + n, sizeof line - n, " %s %u,", viewer::recKindKey(k), byKind[k]);
    if (n > 0) line[n - 1] = 0;
    else line[0] = 0;
    host_log("live: viewer: level %d records:%s; %u collision triangles; %u notes\n", rd<int>(kLevelNo), line,
             unsigned(g_tris.size()), unsigned(g_notes.size()));
    for (const std::string &s : g_notes) host_log("live: viewer: note: %s\n", s.c_str());
}

int findRecord(int kind, int index) {
    for (size_t i = 0; i < g_recs.size(); i++)
        if (g_recs[i].kind == kind && g_recs[i].index == index) return int(i);
    return -1;
}

/* the runtime vehicle record a spawn became: the k-th spawn of its type is the k-th gVehicles record of that
 * type (type 1 on bonus levels: the player's pick) */
int vehicleOf(const Record &spawn) {
    int type = spawn.type, k = 0;
    const unsigned kind = rd<uint8_t>(kLevelInfo + uint32_t(rd<int>(kLevelNo)) * 0x44u);
    if (type == 1 && kind != 1 && kind != 0x80) type = rd<uint8_t>(kDigger);
    for (const Record &r : g_recs)
        if (r.kind == viewer::REC_VEHICLE && r.index < spawn.index && r.type == spawn.type) k++;
    for (const Record &r : g_recs)
        if (r.kind == viewer::REC_GVEHICLE && r.type == type && k-- == 0) return r.index;
    return -1;
}

/* the player's start: the driver's spawn (type 0) */
bool playerStart(Vec &p, double &heading) {
    for (const Record &r : g_recs)
        if (r.kind == viewer::REC_VEHICLE && r.type == 0) {
            p = {r.x, r.y, r.z};
            heading = r.heading;
            return true;
        }
    return false;
}

void jumpToStart() {
    Vec p;
    double h;
    if (!playerStart(p, h)) return;
    g_mv.on = false;
    g_cam.yaw = h;
    g_cam.pitch = -25;
    g_cam.lookAt(p, 160);
    host_log("live: viewer: camera at the player's start (%.0f %.0f %.0f)\n", p.x, p.y, p.z);
}

void cameraFromGame() {
    orbit::Vec3 eye, fwd;
    if (!orbit::viewEye(g_fz.view, eye, fwd)) return;
    g_cam.pos = {eye.x, eye.y, eye.z};
    double yaw, pitch;
    orbit::angles(fwd, yaw, pitch);
    g_cam.yaw = yaw, g_cam.pitch = pitch;
}

/* above the level, looking down at its middle */
void cameraTop() {
    g_mv.on = false;
    const double cx = (g_hdr.minX + g_hdr.maxX) / 2.0, cz = (g_hdr.minZ + g_hdr.maxZ) / 2.0;
    const double size = std::max(1000.0, double(std::max(g_hdr.maxX - g_hdr.minX, g_hdr.maxZ - g_hdr.minZ)));
    g_cam.yaw = 0;
    g_cam.pitch = -60;
    g_cam.lookAt({cx, groundAt(cx, cz, 0), cz}, size * 0.75);
}

/* ---- the capture's drawing list (PORT_DRAW_CELLS) -------------------------------------
 * Every cell, as long as the game's level display-list buffers hold the lists (lvl_alloc_dl_buffers' sizes;
 * an upper bound of what lvl_build_visible_dl copies for a set of cells: the fixed part, each listed
 * record's whole range, each group record with a listed cell): cells nearest the camera first. */
std::vector<short> g_cellList;
short *cellsHook(short *game) {
    return g_cellList.empty() ? game : g_cellList.data();
}

unsigned dlCapacity(int level, int profile, int list) {
    static const unsigned special[5][5] = {{16, 50, 50, 50, 200}, {29, 50, 50, 50, 200}, {17, 50, 50, 50, 50},
                                           {11, 3000, 50, 50, 200}, {10, 50, 50, 50, 2000}};
    for (const auto &s : special)
        if (int(s[0]) == level) return s[1 + list];
    static const unsigned p0[4] = {6000, 5000, 100, 2000}, p1[4] = {1000, 1000, 100, 2000};
    return profile == 0 ? p0[list] : profile == 1 ? p1[list] : 0;
}

void buildCellList() {
    g_cellList.clear();
    g_fz.cells = g_fz.cellsDrawn = 0;
    const uint32_t lvl = levelData();
    const size_t span = levelSpan(lvl);
    viewer::LevelHeader h;
    if (!g_opt.allCells || span == 0 ||
        !viewer::parseHeader(reinterpret_cast<const uint8_t *>(uintptr_t(lvl)), span, h))
        return;
    const unsigned n = h.cols * h.rows;
    const int level = rd<int>(kLevelNo);
    const uint8_t *L = reinterpret_cast<const uint8_t *>(uintptr_t(lvl));
    auto s32 = [&](uint32_t o) { return int32_t(uint32_t(L[o]) | uint32_t(L[o + 1]) << 8 | uint32_t(L[o + 2]) << 16 |
                                                uint32_t(L[o + 3]) << 24); };
    /* per list: the fixed part, then the cost of each cell's records and the groups (with their cells) */
    struct Group {
        unsigned cost;
        std::vector<int> cells;
    };
    unsigned used[4], cap[4], nrec[4] = {}, ngrp[4] = {};
    std::vector<std::vector<unsigned>> cellCost(4, std::vector<unsigned>(n, 0));
    std::vector<std::vector<Group>> groups(4);
    const unsigned fixed = (h.offAt(0x8C) >= h.offAt(0x88) ? (h.offAt(0x8C) - h.offAt(0x88)) / 8 : 0) + 1;
    for (int k = 0; k < 4; k++) {
        used[k] = fixed;
        cap[k] = dlCapacity(level, h.dlProfile, k);
        const uint32_t r0 = h.offAt(0xA0 + 4 * k), r1 = h.offAt(0xA4 + 4 * k);
        for (uint32_t r = r0; r + 0x14 <= r1 && r + 0x14 <= span; r += 0x14) {
            const int32_t a = s32(r), b = s32(r + 4), e = s32(r + 8), cell = s32(r + 0x10);
            const int32_t len = std::max(e - a, e - b);
            if (cell >= 0 && unsigned(cell) < n && len > 0) cellCost[k][cell] += unsigned(len / 8);
            nrec[k]++;
        }
        const uint32_t g0 = h.offAt(0xB0 + 4 * k), g1 = h.offAt(0xB4 + 4 * k);
        for (uint32_t g = g0; g + 0x14 <= g1 && g + 0x14 <= span;) {
            const int32_t cnt = s32(g + 0x10);
            if (cnt < 0 || cnt > 4096) break;
            Group gr;
            gr.cost = unsigned(std::max(0, s32(g + 8) - s32(g)) / 8);
            for (int i = 0; i < cnt && g + 0x14 + 4 * i + 4 <= span; i++) gr.cells.push_back(s32(g + 0x14 + 4 * i));
            groups[k].push_back(gr);
            ngrp[k]++;
            g += 0x14 + 4 * uint32_t(cnt);
        }
    }
    /* nearest first (from the camera, else the player) */
    Vec c = g_cam.pos;
    std::vector<int> order(n);
    for (unsigned i = 0; i < n; i++) order[i] = int(i);
    const double cw = h.cellW ? h.cellW : 1, cd = h.cellD ? h.cellD : 1;
    std::sort(order.begin(), order.end(), [&](int a, int b) {
        const double ax = (a % h.cols + 0.5) * cw - c.x, az = (a / h.cols + 0.5) * cd - c.z;
        const double bx = (b % h.cols + 0.5) * cw - c.x, bz = (b / h.cols + 0.5) * cd - c.z;
        return ax * ax + az * az < bx * bx + bz * bz;
    });
    std::vector<uint8_t> on(n, 0);
    std::vector<std::vector<uint8_t>> grpOn(4);
    for (int k = 0; k < 4; k++) grpOn[k].assign(groups[k].size(), 0);
    for (int cell : order) {
        unsigned add[4];
        bool fits = true;
        for (int k = 0; k < 4; k++) {
            add[k] = cellCost[k][cell];
            for (size_t g = 0; g < groups[k].size(); g++)
                if (!grpOn[k][g] && std::find(groups[k][g].cells.begin(), groups[k][g].cells.end(), cell) !=
                                        groups[k][g].cells.end())
                    add[k] += groups[k][g].cost;
            if (used[k] + add[k] + 8 > cap[k]) fits = false;
        }
        if (!fits) continue;
        on[cell] = 1;
        for (int k = 0; k < 4; k++) {
            used[k] += add[k];
            for (size_t g = 0; g < groups[k].size(); g++)
                if (!grpOn[k][g] && std::find(groups[k][g].cells.begin(), groups[k][g].cells.end(), cell) !=
                                        groups[k][g].cells.end())
                    grpOn[k][g] = 1;
        }
    }
    /* the game's own cells are always in (the game drew them with these buffers already) */
    const short *game = reinterpret_cast<const short *>(uintptr_t(0x803C30A8u));   /* gVisibleCells */
    for (unsigned i = 0; i < n + 1 && game[i] != -1; i++)
        if (unsigned(uint16_t(game[i])) < n) on[uint16_t(game[i])] = 1;
    for (unsigned i = 0; i < n; i++)
        if (on[i]) g_cellList.push_back(short(i));
    g_cellList.push_back(-1);
    g_fz.cells = n;
    g_fz.cellsDrawn = unsigned(g_cellList.size() - 1);
    host_log("live: viewer: capture draws %u of %u cells (level lists %u/%u %u/%u %u/%u %u/%u commands of the "
             "game's buffers; records %u %u %u %u, groups %u %u %u %u)\n",
             g_fz.cellsDrawn, n, used[0], cap[0], used[1], cap[1], used[2], cap[2], used[3], cap[3], nrec[0], nrec[1],
             nrec[2], nrec[3], ngrp[0], ngrp[1], ngrp[2], ngrp[3]);
}

/* ---- tags (PORT_ITAG, plat_core.c's viewer hook) ---------------------------------------- */
void itagHook(unsigned addr, int kind, int index) {
    if (!g_recordTags || kind >= 30) return;
    g_tagsBuilding[addr & 0xFFFFFF] = {uint32_t(kind), uint32_t(index)};
    g_tagCount++;
}

/* ---- the command hook (RT64 reading a replayed task) ----------------------------------- */
struct Owner {
    uint32_t kind = K_NONE, index = 0;
};
std::vector<Owner> g_owners;
Owner g_owner, g_pendingOwner;
bool g_hasPending;
int g_cimg;
bool g_inSky, g_skyDone;
bool g_mainTask;                  /* the frame's own task (the categories and the sky apply to it only: the
                                     offscreen ones, e.g. the vehicle shadows' textures, are drawn as they are) */
unsigned g_skipped, g_drawn;
bool g_measure;                   /* this replay measures the model view's model */

bool isTerrain(uint32_t phys) {
    if (!g_fz.level) return false;
    phys &= 0x7FFFFF;
    for (uint32_t t : g_fz.terrain)
        if (t != 0 && (t & 0x7FFFFF) == phys) return true;
    return g_fz.levelCalls != 0 && (g_fz.levelCalls & 0x7FFFFF) == phys;
}

bool ownerShown(const Owner &o) {
    if (!g_mv.on) return true;
    if (o.kind != g_mv.kind) return false;
    return g_mv.kind == K_OBJ ? (o.index & 0xFF) == g_mv.index : o.index == g_mv.index;
}

void measureVtx(RT64::State *state, uint32_t w0, uint32_t w1) {
    const unsigned n = ((w0 >> 20) & 15) + 1;
    const uint32_t a = state->rsp->fromSegmented(w1) & 0x7FFFFF;
    if (a + n * 16 > 0x800000) return;
    const RT64::RSP &rsp = *state->rsp;
    const hlslpp::float4x4 &m = rsp.modelMatrixStack[rsp.modelMatrixStackSize - 1];
    double M[4][4];
    for (int r = 0; r < 4; r++)
        for (int c = 0; c < 4; c++) M[r][c] = double(float(m[r][c]));
    for (unsigned i = 0; i < n; i++) {
        const int16_t *v = reinterpret_cast<const int16_t *>(state->RDRAM + a + i * 16);
        const double x = v[0], y = v[1], z = v[2];
        const double w[3] = {x * M[0][0] + y * M[1][0] + z * M[2][0] + M[3][0],
                             x * M[0][1] + y * M[1][1] + z * M[2][1] + M[3][1],
                             x * M[0][2] + y * M[1][2] + z * M[2][2] + M[3][2]};
        if (!std::isfinite(w[0]) || !std::isfinite(w[1]) || !std::isfinite(w[2])) continue;
        for (int k = 0; k < 3; k++) {
            if (g_mv.vtx == 0 || w[k] < g_mv.lo[k]) g_mv.lo[k] = w[k];
            if (g_mv.vtx == 0 || w[k] > g_mv.hi[k]) g_mv.hi[k] = w[k];
        }
        g_mv.vtx++;
    }
}

/* ---- the driver in the model view ------------------------------------------------------
 * The driver's gVehicles record isn't drawn while he sits in a vehicle (veh_draw_layer leaves VEH_DRIVER out
 * unless he has left one: gVehEntered and not gDriverHidden), so at a level's start his model view was empty.  It
 * shows him anyway: the frame's first vehicle commands of draw layer 0 (veh_draw_layer's own: its segments, env
 * colour, geometry mode, list, matrix) are run once more just before it, host side, with the driver's segments,
 * alpha and display list - his own list over his own part matrices (segment 7: the pose his last update left).
 * Render only: RDRAM and the game's state are untouched, and only the model view of him runs it. */
RT64::DisplayList g_drvCmds[24];
int g_drvState = -1;              /* -1: not wanted this replay; 0: wanted, not run yet; 1: run (or given up) */
bool g_inDrv;                     /* RT64 reads g_drvCmds (its host addresses name no tag) */
uint32_t g_drvReturn;             /* the vehicle command it runs before (where it goes back) */
unsigned g_drvRuns;

/* the model view shows the driver's record and the frame didn't draw it */
bool driverWanted() {
    if (!g_mv.on || g_mv.kind != K_OBJ || !g_fz.level) return false;
    const uint32_t rec = kGVehicles + g_mv.index * 0x74u;
    if (rec + 0x74u > kGVehiclesEnd || rd<int32_t>(rec + 0x5C) != 0) return false;   /* (VEH_DRIVER) */
    for (const auto &t : g_tags)
        if (t.second.kind == K_OBJ && (t.second.index & 0xFF) == g_mv.index) return false;
    return true;
}

/* at the frame's first layer-0 vehicle (its tag's command, `here`): the driver's version of its commands, then a
 * branch back to it; false: not that place, or its commands aren't veh_draw_layer's */
bool driverInject(RT64::State *state, RT64::DisplayList **dl) {
    const uint8_t *p = reinterpret_cast<const uint8_t *>(*dl);
    if (p < state->RDRAM || p >= state->RDRAM + 0x800000) return false;
    const uint32_t here = uint32_t(p - state->RDRAM) & 0xFFFFFF;
    auto it = g_tags.find(here);
    if (it == g_tags.end() || it->second.kind != K_OBJ || (it->second.index >> 8) != 0) return false;
    g_drvState = 1;
    const uint32_t veh = kGVehicles + (it->second.index & 0xFF) * 0x74u, drv = kGVehicles + g_mv.index * 0x74u;
    auto phys = [](uint32_t a) { return a & 0x1FFFFFFFu; };
    int n = 0, odd = -1;
    bool seg6 = false, list = false, mtx = false;
    for (int k = 0; k < 16 && !mtx; k++) {
        RT64::DisplayList c = (*dl)[k];
        const uint8_t op = uint8_t(c.w0 >> 24);
        if (op == 0xBC && (c.w0 & 0xFF) == 6) {   /* G_MOVEWORD G_MW_SEGMENT */
            const unsigned seg = ((c.w0 >> 8) & 0xFFFF) / 4;
            if (seg == 6) c.w1 = phys(rd<uint32_t>(drv + 0x00)), seg6 = true;
            if (seg == 7) {
                odd = c.w1 == phys(rd<uint32_t>(veh + 0x04)) ? 1 : 0;
                c.w1 = phys(rd<uint32_t>(drv + (odd ? 0x04 : 0x08)));
            }
        } else if (op == 0xFB) {   /* G_SETENVCOLOR: the driver's alpha */
            c.w1 = (c.w1 & 0xFFFFFF00u) | (rd<uint32_t>(drv + 0x60) & 0xFF);
        } else if (op == 0x06 && !list) {   /* the vehicle's list: the driver's (layer 0) */
            if (odd < 0) break;
            c.w1 = phys(rd<uint32_t>(drv + (odd ? 0x0C : 0x30)));
            list = (c.w0 & 0x00FF0000u) == 0;   /* (a call, not a branch) */
        } else if (op == 0x01 && list) {
            mtx = true;   /* (the matrix load after the list: the end of the vehicle's commands) */
        } else if (op != 0xE7 && op != 0xB6 && op != 0xB7 && op != 0x01) {
            break;   /* (not veh_draw_layer's) */
        }
        g_drvCmds[n++] = c;
    }
    if (!seg6 || odd < 0 || !list || !mtx) {
        host_log("live: viewer: the driver's model: the vehicles' commands at 0x%06X aren't the expected ones\n", here);
        return false;
    }
    g_drvCmds[n].w0 = 0x06010000u, g_drvCmds[n].w1 = here;   /* G_DL, a branch back */
    g_drvReturn = here;
    g_inDrv = true;
    g_owner = {K_OBJ, g_mv.index};
    *dl = g_drvCmds - 1;   /* (RT64 steps to the next command after the hook) */
    if (g_drvRuns++ == 0)
        host_log("live: viewer: the driver isn't drawn in this frame (he sits in a vehicle): his own list drawn for "
                 "the model view (%d commands before the vehicle at 0x%06X; segment 7 %s)\n", n, here, odd ? "odd" : "even");
    return true;
}

/* every command (G_MTX and G_POPMTX too: a tag may name one's address): the owner of what follows at its
 * display-list level (a called list inherits it; the level lists' calls make their contents terrain) */
void ownerHook(RT64::State *state, uint32_t addr) {
    const size_t level = state->returnAddressStack.size();
    while (g_owners.size() > level) {
        g_owner = g_owners.back();
        g_owners.pop_back();
    }
    while (g_owners.size() < level) {
        g_owners.push_back(g_owner);
        if (g_hasPending) g_owner = g_pendingOwner;
        g_hasPending = false;
    }
    g_hasPending = false;
    if (g_inDrv) {
        if (addr != g_drvReturn) return;   /* (the driver's commands: no tags) */
        g_inDrv = false;
    }
    auto it = g_tags.find(addr);
    if (it != g_tags.end()) g_owner = {it->second.kind, it->second.index};
}

bool g_previewNoCull;   /* the terrain preview is on: the lists' G_CULLDL boxes are the old ones */

bool commandHook(RT64::State *state, RT64::DisplayList **dl, const GbiOriginal *orig) {
    const uint32_t w0 = (*dl)->w0, w1 = (*dl)->w1;
    const uint8_t op = uint8_t(w0 >> 24);
    if (!g_mainTask || !g_fz.level) return false;   /* (a front-end frame: drawn as the game drew it) */
    if (g_drvState == 0 && driverInject(state, dl)) return true;
    if (op == 0xBE && g_previewNoCull) return true;   /* (skipped: drawn) */
    if (op == 0xFF) g_cimg++;
    const bool sky = g_cimg >= 2 && !g_skyDone;
    if (op == 0x06 && g_owner.kind == K_NONE) {   /* G_DL: the level's lists are terrain */
        const uint32_t target = state->rsp->fromSegmented(w1);
        if (isTerrain(target)) {
            if ((w0 >> 16) & 1) g_owner.kind = K_TERRAIN;   /* (a branch: the same level) */
            else g_pendingOwner = {K_TERRAIN, 0}, g_hasPending = true;
        }
    }
    const int cat = catOf(g_owner.kind, sky);
    /* the sky's ground fill: a plain background (full screen) in its place */
    if (sky && (g_opt.plain || g_mv.on)) {
        if (op == 0xF7) {   /* G_SETFILLCOLOR */
            RT64::DisplayList c = **dl;
            c.w1 = 0x294B294Bu;   /* RGBA5551 (40, 40, 40)-ish grey, twice */
            RT64::DisplayList *p = &c;
            orig[op](state, &p);
            return true;
        }
        if (op == 0xF6) {   /* G_FILLRECT: from the top */
            g_skyDone = true;
            RT64::DisplayList c = **dl;
            c.w1 = 0;
            RT64::DisplayList *p = &c;
            orig[op](state, &p);
            return true;
        }
    }
    if (sky && op == 0xF6) g_skyDone = true;
    const bool draw = op == 0xBF || op == 0xB5 || op == 0xB1 || op == 0xE4 || op == 0xE5 || op == 0xF6 || op == 0x09;
    if (op == 0x04 && g_measure && g_mv.on && ownerShown(g_owner) && cat != CAT_SKY && cat != CAT_HUD)
        measureVtx(state, w0, w1);
    if (!draw) return false;
    bool hide = !g_opt.show[cat];
    if (g_mv.on) hide = cat == CAT_SKY ? true : cat == CAT_HUD ? true : !ownerShown(g_owner);
    if (sky && (g_opt.plain || g_mv.on) && op != 0xF6) hide = true;
    if (cat == CAT_SKY && !g_opt.show[CAT_SKY] && op == 0xF6) hide = false;   /* (the ground fill clears the picture) */
    if (g_cimg < 2) hide = false;   /* (the depth clear) */
    if (!hide) {
        g_drawn++;
        return false;
    }
    g_skipped++;
    if ((op == 0xE4 || op == 0xE5) && uint8_t((*dl)[1].w0 >> 24) == 0xB4 && uint8_t((*dl)[2].w0 >> 24) == 0xB3)
        *dl += 2;   /* (a texture rectangle's two halves) */
    return true;
}

void resetReplay() {
    g_owners.clear();
    g_owner = Owner();
    g_hasPending = false;
    g_cimg = 0;
    g_inSky = g_skyDone = false;
    g_inDrv = false;
}

/* the frozen frame's tasks to RT64 again */

void replay() {
    RT64::Application *app = g_env.app;
    publishView();
    previewWrite();   /* (the terrain tools' moved corners, for this replay only) */
    g_drvState = driverWanted() ? 0 : -1;
    for (const Task &t : g_frozenTasks) {
        app->interpreter->loadUCodeGBI(t.ucode & 0x3FFFFFF, t.data & 0x3FFFFFF, true);
        if (app->interpreter->hleGBI == nullptr) continue;
        resetReplay();
        g_mainTask = t.frameDl;
        interpTagsBeforeTask(t.dl, t.frameDl);
        wideBeforeTask();
        app->processDisplayLists(app->core.RDRAM, t.dl & 0x3FFFFFF, 0, true);
        interpTagsAfterTask(t.frameDl);
        wideAfterTask(t.frameDl);
    }
    previewRestore();
    /* a new picture: the VI as it was (RT64 presents when the VI differs: the line counter alternates) */
    *g_env.viOrigin = g_fz.origin;
    *g_env.viCurrentLine = (g_replays & 1) ? 2 : 0;
    app->updateScreen();
    g_replays++;
}

/* ---- the frozen frame ---------------------------------------------------------------- */
/* the capture's: the drawing paths' cells, the tags */
void captureHooksOn() {
    host_draw_cells_hook = cellsHook;
    host_viewer_itag_hook = itagHook;
    g_recordTags = true;
}

/* the replays': who draws what, the free camera's view */
void frozenHooksOn() {
    host_draw_cells_hook = nullptr;
    host_viewer_itag_hook = nullptr;
    g_recordTags = false;
    interpSetCommandHook(commandHook, nullptr);
    interpSetOwnerHook(ownerHook);
    RT64::ViewHook = viewHook;
}

void hooksOff() {
    {
        std::lock_guard<std::mutex> lock(g_vsMutex);
        g_vs.on = false;
    }
    g_cellList.clear();
    host_viewer_itag_hook = nullptr;
    g_recordTags = false;
    interpSetOwnerHook(nullptr);
    orbitApply();   /* the camera's hooks as the settings have them (the command hook, the view hook, the cells) */
}

/* the free camera's position for the cell list's order: the game's camera of the buffer being built */
void camAtGameEye() {
    const unsigned dyn = rd<uint8_t>(0x8035805Cu) & 1;   /* gBufferIndex */
    double v[4][4];
    mtxToDouble(kDyn + dyn * kDynSize + kDynView, v);
    orbit::Vec3 e, f;
    if (orbit::viewEye(v, e, f)) g_cam.pos = {e.x, e.y, e.z};
}

void beginCapture() {
    if (g_resetCam) camAtGameEye();
    g_state = S_CAPTURE;
    g_captureFrames = 0;
    g_cur.clear();
    g_tagsBuilding.clear();
    hooksOff();
    captureHooksOn();
    {
        std::lock_guard<std::mutex> lock(g_vsMutex);
        g_vs.on = false;   /* (the game's own view while it steps) */
    }
    buildCellList();
    host_log("live: viewer: capturing (frame %u, level %d, mode %08llX)\n", plat_frames(), rd<int>(kLevelNo),
             rd<unsigned long long>(kGameMode));
}

/* the frozen frame's DynamicBuf, view, terrain lists and picture */
void setupFrozen() {
    g_fz = Frozen();
    g_fz.mode = rd<unsigned long long>(kGameMode);
    g_fz.levelNo = rd<int>(kLevelNo);
    g_fz.lvl = levelData();
    const Task *frame = nullptr;
    for (const Task &t : g_frozenTasks)
        if (t.frameDl) frame = &t;
    uint32_t cimg = 0;
    if (frame != nullptr) {
        const uint32_t dl = frame->dl & 0x3FFFFFF;
        g_fz.dyn = (dl - (kDyn & 0x3FFFFFF)) / kDynSize;
        if (g_fz.dyn <= 1 && (g_fz.mode & (MODE_LEVEL | MODE_LEVEL_START | MODE_MAP_VIEW | MODE_CARRIER_END | 0x2)) != 0) {
            const uint32_t base = kDyn + g_fz.dyn * kDynSize;
            mtxToDouble(base + kDynView, g_fz.view);
            mtxToDouble(base + kDynProj, g_fz.proj);
            orbit::Vec3 e, f;
            g_fz.level = orbit::viewEye(g_fz.view, e, f);
            for (int i = 0; i < 8; i++) g_fz.terrain[i] = rd<uint32_t>(kLevelDl + 4u * uint32_t(i));
            for (int i = 0; i < 4; i++) g_fz.terrain[8 + i] = rd<uint32_t>(kLevelFileDl + 4u * uint32_t(i));
            g_fz.levelCalls = base + kDynLevelCalls;
        }
        /* the colour image: the frame's second G_SETCIMG (the first is the depth clear's) */
        const uint32_t *w = reinterpret_cast<const uint32_t *>(uintptr_t(0x80000000u | dl));
        int seen = 0;
        for (unsigned i = 0; i < 0xB5E; i++) {
            const uint32_t op = w[i * 2] >> 24;
            if (op == 0xB8) break;
            if (op == 0xFF && ++seen == 2) {
                cimg = w[i * 2 + 1] & 0x7FFFFF;
                break;
            }
        }
    }
    /* the VI origin of that picture: the frame buffer's start plus the VI's offset into its buffer */
    const uint32_t vi = *g_env.viOrigin & 0x7FFFFF, fb0 = 0x400, fbSize = 320 * 240 * 2;
    g_fz.origin = *g_env.viOrigin;
    if (cimg != 0 && vi >= fb0 && vi < fb0 + 2 * fbSize) g_fz.origin = cimg + (vi - fb0) % fbSize;
    {
        std::lock_guard<std::mutex> lock(g_vsMutex);
        toFloat(g_fz.view, g_vs.game);
        g_vs.on = g_fz.level;
        g_vs.haveProj = false;
    }
    host_log("live: viewer: frozen at frame %u (level %d, mode %08llX): %u tasks, %u tags, %s; picture 0x%06X (VI "
             "0x%06X)\n",
             plat_frames(), g_fz.levelNo, g_fz.mode, unsigned(g_frozenTasks.size()), unsigned(g_tags.size()),
             g_fz.level ? "the free camera works" : "not a level frame: the game's own camera only", g_fz.origin,
             vi);
}

/* ---- the frozen loop ------------------------------------------------------------------ */
void statusLine() {
    char b[256];
    snprintf(b, sizeof b, "%s  -  frozen at game frame %u%s", levelLabel(std::max(0, g_fz.levelNo)).c_str(),
             g_capturedFrame, g_fz.level ? "" : " (not a level: the game's own view)");
    g_status = b;
}

void runFrozen() {
    g_state = S_FROZEN;
    g_freezes++;
    setupFrozen();
    frozenHooksOn();
    readRecords();
    if (g_applyPending) {
        g_applyPending = false;
        g_applyMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - g_applyStart).count();
        host_log("live: editor: apply: the edited level is up and frozen again after %.0f ms\n", g_applyMs);
    }
    if (g_resetCam) cameraFromGame();
    g_resetCam = false;
    if (g_padNav && !g_padNavWin.empty()) g_padNavFocus = true;
    statusLine();
    g_leave = g_recapture = false;
    SDL_ShowCursor(SDL_ENABLE);
    LARGE_INTEGER freq, last, now;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&last);
    while (g_state == S_FROZEN) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) viewerEvent(e);
        QueryPerformanceCounter(&now);
        const double dt = std::min(0.1, double(now.QuadPart - last.QuadPart) / double(freq.QuadPart));
        last = now;
        if (!g_test.empty()) runTestOp();
        stepCamera(dt);
        terrainTick(dt);
        if (g_cleanShot || g_overlayShot) {
            const bool overlays = g_overlayShot;
            doShot(autoShotName(overlays), overlays);
            host_log("live: viewer: screenshot saved\n");
        }
        /* (a test script doesn't see the desktop's mouse: whoever moves it can't steer the session) */
        if (g_opt.ui || g_opt.markers || g_opt.collision) uiFrame(buildUi, g_test.empty());
        else uiHide();
        if (!g_test.empty()) vpadAfterFrame();
        /* the edits the panels asked for (after the frame: they replace the records the panels read) */
        while (!g_after.empty()) {
            std::vector<std::function<void()>> todo;
            todo.swap(g_after);
            for (auto &fn : todo) fn();
        }
        replay();
        if (g_mv.on && g_measure && g_mv.vtx > 0) {
            /* the model's own extent, from what it drew */
            g_mv.centre = {(g_mv.lo[0] + g_mv.hi[0]) / 2, (g_mv.lo[1] + g_mv.hi[1]) / 2, (g_mv.lo[2] + g_mv.hi[2]) / 2};
            g_mv.radius = std::max(5.0, 0.5 * std::sqrt((g_mv.hi[0] - g_mv.lo[0]) * (g_mv.hi[0] - g_mv.lo[0]) +
                                                        (g_mv.hi[1] - g_mv.lo[1]) * (g_mv.hi[1] - g_mv.lo[1]) +
                                                        (g_mv.hi[2] - g_mv.lo[2]) * (g_mv.hi[2] - g_mv.lo[2])));
            g_mv.dist = g_mv.radius * 2.6;
            g_mv.measured = true;
            g_measure = false;
            host_log("live: viewer: model %s: %u vertices, centre %.0f %.0f %.0f, radius %.0f\n", g_mv.title.c_str(),
                     g_mv.vtx, g_mv.centre.x, g_mv.centre.y, g_mv.centre.z, g_mv.radius);
        }
        if (g_leave) {
            leave();
            break;
        }
        if (g_recapture) {
            uiHide();
            beginCapture();
            break;
        }
        if (g_state != S_FROZEN) break;   /* (a level load: requestLevel) */
        Sleep(10);
    }
    {
        /* (the controller on the panels: the panel it was on gets it back when the viewer holds the game again) */
        const ImGuiContext *gc = ImGui::GetCurrentContext();
        g_padNavWin = g_padNav && gc && gc->NavWindow && gc->NavWindow->Name[0] != '#' ? gc->NavWindow->RootWindow->Name : "";
    }
    uiHide();
    if (g_looking) {
        g_looking = false;
        SDL_SetRelativeMouseMode(SDL_FALSE);
    }
}

void requestLevel(int level) {
    if (level < 0 || level >= 60) return;
    g_wantLevel = level;
    g_loadedLevel = true;
    g_loads++;
    g_resetCam = true;
    g_state = S_LOAD;
    g_loadStart = plat_frames();
    g_mv.on = false;
    g_sel = -1;
    hooksOff();
    host_viewer_request(level, bonusVehicle(level));
    host_log("live: viewer: loading level %d (%s) by the game's level set-up\n", level, levelLabel(level).c_str());
}

void leave() {
    hooksOff();
    g_state = S_OFF;
    uiHide();
    autosave();
    if (g_quitAfter) {
        host_log("live: viewer: done (--viewer-test quit)\n");
        g_env.quit(0);
    }
    if (g_loadedLevel) {
        host_log("live: viewer: left after loading a level: restarting the game\n");
        g_env.restart();
        return;
    }
    host_log("live: viewer: left; the game goes on from frame %u\n", plat_frames());
}

}  // namespace vw

using namespace vw;

/* ---- the interface -------------------------------------------------------------------- */
void viewerSetup(const ViewerEnv &env) {
    g_env = env;
}

void viewerStartAtBoot(int level) {
    g_bootLevel = level < 0 ? 0 : level;
}

bool viewerSetTest(const std::string &spec) {
    g_test.clear();
    size_t p = 0;
    while (p <= spec.size()) {
        size_t q = spec.find(';', p);
        if (q == std::string::npos) q = spec.size();
        if (q > p) g_test.push_back(spec.substr(p, q - p));
        p = q + 1;
    }
    return !g_test.empty();
}

void viewerMenuTab() {
    readNames();
    ImGui::TextWrapped("The level viewer holds the game still and lets you fly around the level, look at its "
                       "records (vehicles, buildings, RDUs, TNT, the carrier's route ...) and at each model on its own.");
    const unsigned long long mode = rd<unsigned long long>(kGameMode);
    const bool level = (mode & (MODE_LEVEL | MODE_MAP_VIEW | MODE_LEVEL_START)) != 0;
    if (ImGui::Button("Open the viewer here")) {
        g_wanted = true;
        menuClose();
    }
    ImGui::TextDisabled(level ? "Leaving it goes back to the game where it was (two frames on)."
                              : "Not in a level now: the viewer shows the picture as it is (no free camera).");
    ImGui::Separator();
    const std::string cur = levelLabel(g_menuLevel);
    if (ImGui::BeginCombo("Level", cur.c_str())) {
        for (int i = 0; i < 60; i++)
            if (ImGui::Selectable(levelLabel(i).c_str(), i == g_menuLevel)) g_menuLevel = i;
        ImGui::EndCombo();
    }
    const bool canLoad = rd<uint8_t>(kFrontEnd) == 0;
    ImGui::BeginDisabled(!canLoad);
    if (ImGui::Button("Load it in the viewer")) {
        requestLevel(g_menuLevel);
        menuClose();
    }
    ImGui::EndDisabled();
    ImGui::TextWrapped(canLoad ? "Loads the level with the game's own set-up. Leaving the viewer afterwards restarts "
                                 "the game (the level wasn't reached by play)."
                               : "Levels can be loaded from a level or the attract demo (the menus' save thread must "
                                 "not be running). Or start bc.exe --viewer LEVEL.");
}

bool viewerWanted() {
    return g_wanted;
}

void viewerOpen() {
    if (g_state == S_OFF) g_wanted = true;
}

bool viewerActive() {
    return g_state != S_OFF;
}

void viewerGfxTask(unsigned ucode, unsigned ucodeData, unsigned dl, bool frameDl) {
    if (g_state != S_CAPTURE) return;
    g_cur.push_back({ucode, ucodeData, dl, frameDl});
    if (!frameDl) {
        if (g_cur.size() > 16) g_cur.erase(g_cur.begin());
        return;
    }
    /* the frame's display list: the tags recorded since the last one are its own */
    g_frozenTasks = g_cur;
    g_tags.swap(g_tagsBuilding);
    g_tagsBuilding.clear();
    g_cur.clear();
    g_captureFrames++;
    g_capturedFrame = plat_frames();
}

void viewerVi(unsigned frames) {
    (void) frames;
    readNames();
    if (g_bootLevel >= -1) {   /* --viewer: ask for the level now (the game takes it in the attract demo) */
        const int l = g_bootLevel;
        g_bootLevel = -2;
        requestLevel(l);
    }
    if (g_wanted && g_state == S_OFF) {
        g_wanted = false;
        g_loadedLevel = false;
        g_resetCam = true;
        beginCapture();
        return;
    }
    if (g_state == S_LOAD) {
        const unsigned long long mode = rd<unsigned long long>(kGameMode);
        const bool there = host_viewer_state() == 2 && rd<int>(kLevelNo) == g_wantLevel &&
                           (mode == MODE_LEVEL || mode == MODE_LEVEL_START) && rd<uint32_t>(kFramesInMode) >= 12;
        /* (an edited level whose carrier crashes at once - the ground under its wheels or in its way changed: the
         * game ends the level and would go back to the front end; the viewer holds it there instead, to undo) */
        const bool crashed = host_viewer_state() == 2 && rd<int>(kLevelNo) == g_wantLevel && mode == MODE_CARRIER_END &&
                             rd<uint32_t>(kFramesInMode) >= 2 && g_ed.isOpen() && !g_playAfterLoad;
        if (there || crashed) {
            host_viewer_done();
            host_log("live: viewer: level %d up after %u frames\n", g_wantLevel, plat_frames() - g_loadStart);
            if (crashed) {
                host_log("live: editor: apply: the carrier crashed as soon as the level started (an edit is under or in "
                         "front of it): held there\n");
                g_editMsg = "The carrier crashed as soon as the level started: an edit changed the ground under its "
                            "wheels or put something in its way. Undo it (or move it off the carrier's route) and Apply.";
            }
            if (g_playAfterLoad) {
                /* "Play from here": the game goes on into the edited level, as play (paced, with sound and input) */
                g_playAfterLoad = false;
                autosave();
                hooksOff();
                g_state = S_OFF;
                uiHide();
                host_log("live: viewer: test run of the edited level %d from frame %u (saving off)\n", g_wantLevel,
                         plat_frames());
                return;
            }
            beginCapture();
        } else if (plat_frames() - g_loadStart > 6000) {
            host_log("live: viewer: level %d didn't come up (mode %08llX, request state %d): giving up\n", g_wantLevel,
                     mode, host_viewer_state());
            host_viewer_done();
            leave();
        }
        return;
    }
    if (g_state == S_CAPTURE && g_captureFrames >= 2) runFrozen();
}

void viewerSummary() {
    if (g_freezes == 0 && g_loads == 0) return;
    host_log("live: viewer: %u freezes, %u levels loaded, %u pictures replayed (%u with the free camera's view), %u "
             "screenshots; %u tags recorded\n",
             g_freezes, g_loads, g_replays, g_vs.hooked, g_shots, g_tagCount);
}

}  // namespace live
