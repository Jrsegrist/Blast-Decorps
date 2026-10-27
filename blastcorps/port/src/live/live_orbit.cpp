/* bc.exe's optional free orbit camera ([camera] mode = orbit; see
 * live_orbit.h and orbit_math.h).
 *
 * How it stays render-only:
 *  - the game's camera: at the end of the game's camera function (00000.c
 *    cam_update, PORT_ICUT_IF -> host_camera_hook) the eye (from the view
 *    matrix the game just built, DynamicBuf.unk140) and the point it looks at
 *    (gCamAtX/Y/Z) are copied for the DynamicBuf being built; read only;
 *  - the pictures: RT64 patch 0006 calls ViewHook for every perspective
 *    projection of every picture it renders (in-between pictures of a higher
 *    frame rate too).  A projection whose view is the frame's game view gets
 *    the orbit camera's view instead: target and game eye interpolated with
 *    the picture's weight, the orbit's direction and distance at the
 *    picture's own time (between the two steps around it: Shared, the same
 *    weight; the view turns evenly from picture to picture).  The projection
 *    is the game's own (fov, aspect, widescreen).  The game never sees any of
 *    it;
 *  - what the game culled: the level's geometry and the buildings whose cells
 *    the game's visibility walk didn't list would be missing from an orbit
 *    view.  The drawing paths (PORT_DRAW_CELLS, 5FD50.c, 77E20.c) get the
 *    game's cells and those the orbit camera can see (its own view test,
 *    with margins for turning: drawCells), unlisted buildings drawn as they
 *    are, without their per-object updates (the game leaves those alone:
 *    they stay as they are).  The game's logic keeps reading its own list
 *    (77E20.c's per-object updates run only for listed objects, so the list
 *    must not change);
 *  - the backdrop (2D2D810.c: a 2D quad scrolled by the camera's heading and
 *    pitch, and the ground colour below its horizon): while RT64 reads the
 *    frame, its vertices and the fill rectangle are given the orbit view's
 *    heading and pitch (the vertices are written just for RT64's read and put
 *    back right after it: RDRAM is as the game left it);
 *  - collision: the eye is kept above the level's ground triangles and the
 *    buildings' collision triangles (read-only copies of the game's height
 *    tests: 62740.c ground_scan_grid's grid, the 0x60-byte triangles at
 *    gCollTris, binned by x/z each step: orbit::TriGrid), pulled in along
 *    the line to the target.
 * The controls: in play, the camera bindings (default: the right stick) drive
 * the camera and not the N64 buttons bound to the same inputs (the C buttons'
 * right stick); outside play the right stick is the C buttons as before. */
#include <windows.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>
#include <mutex>
#include <vector>

#include "gbi/rt64_gbi.h"
#include "hle/rt64_application.h"
#include "gbi/rt64_display_list.h"
#include "hle/rt64_rsp.h"
#include "hle/rt64_state.h"
#include "hle/rt64_workload_queue.h"
#include "render/rt64_projection_processor.h"

#include "live_interp.h"
#include "live_orbit.h"
#include "live_wide.h"
#include "orbit_math.h"

extern "C" {
#include "plat_host.h"
unsigned plat_frames(void); /* save.c */
/* plat_core.c: the backdrop's commands for a heading and pitch (host memory) */
int port_backdrop_gfx(void *out, int max, unsigned dyn, int level, int heading, int pitch, short vtx[4][3]);
}

namespace live {

namespace {

using orbit::Vec3;

/* game state (fixed N64 addresses, host order; the game's names, where it has
 * one, in the comments: tools_port/names.txt) */
template <typename T> T rd(uint32_t a) {
    return *reinterpret_cast<volatile T *>(uintptr_t(a));
}
#define BC_MODE rd<unsigned long long>(0x80364A90u)   /* gGameMode */
#define BC_LEVEL rd<int>(0x802E8BDCu)                 /* levelno */
/* gDynamicBufs[2] */
constexpr uint32_t kDyn = 0x803156F8u, kDynSize = 0x21498u, kDynDl = 0x48B0u, kDynDlEnd = 0xA3A0u, kDynView = 0x140u,
                   kDynProj = 0x80u, kDynCull = 0x180u;

RT64::Application *g_app;
Config *g_cfg;
SDL_Window *g_window;
Config g_padCfg;          /* the bindings without the camera's inputs (orbit active) */
bool g_log;
bool g_test;              /* --orbit-test */
bool g_repeatable;        /* --orbit-repeatable (kept for old scripts: pictures are now always repeatable, viewHook) */
bool g_cullAll;           /* --orbit-cull all: draw every cell (testing; as before the view test) */
std::atomic<unsigned> g_checkPictures, g_checkMissPictures, g_checkMissCells;   /* --orbit-log */
unsigned g_gameCellsChecked, g_gameCellsOutside;                                 /* --orbit-log */
OrbitPerf g_perf;         /* the host work since the start (main thread; orbitPerfTake, orbitFrameTake) */
std::atomic<uint64_t> g_viewTicks, g_views;   /* RT64's render thread: time in viewHook (QPC ticks), calls */
FILE *g_viewLog;          /* --view-log (render thread writes) */

/* ---- the game's camera, per frame ------------------------------------------- */
struct Cam {
    bool valid = false;
    unsigned frame = 0;
    double view[4][4] = {};   /* the frame's view matrix (DynamicBuf.unk140) */
    double proj[4][4] = {};   /* ... and projection (DynamicBuf.unk80, guPerspective) */
    double cullView[4][4] = {};   /* ... and culling view (DynamicBuf.cullView: the eye pushed back) */
    Vec3 eye, target;
    bool snap = false, onFoot = false, play = false;
    bool relChar = false;     /* a character the game steers relative to its camera (relativeCharacter) */
    unsigned long long mode = 0;
    int level = 0;
};
Cam g_built[2];           /* by DynamicBuf (host_camera_hook) */
Cam g_last;               /* the last frame drawn */
unsigned g_cuts;          /* the game's camera cuts in play (the orbit's view kept) */
unsigned g_resets;        /* the orbit put back behind the player: new levels (orbitVi) */
unsigned g_userResets;    /* ... camera_reset presses */
unsigned g_resetsIgnored; /* camera_reset pressed with the camera stick pushed far (taken for an accident) */

void mtxToDouble(uint32_t addr, double m[4][4]) {
    orbit::mtxFromN64(reinterpret_cast<const uint32_t *>(uintptr_t(addr)), m);
}

/* normal play: the camera follows the player (no front end, map, intro or
 * ending views) */
bool playMode(unsigned long long mode) {
    return mode == 4;
}

/* The game steers the player relative to its camera (45BB0.c cont_update: the analog control method, mode 0,
 * calls cont_stick_to_steer, which adds gCamHeading to the stick's angle) for the driver on foot (vehicle 0),
 * Thunderfist (2), the J-Bomb (9) and the Cyclone Suit (16) - unless the player chose the other control method
 * for it (gPlayerControlModes[playerNumber].controlModes, a bit per vehicle type).  Every other vehicle reads
 * the stick as left / right steering and throttle (cont_process_stick), whatever the camera. */
bool relativeCharacter() {
    const unsigned v = rd<uint8_t>(0x80364456u);   /* gCurVehicle */
    if (v != 0 && v != 2 && v != 9 && v != 16) return false;
    const unsigned player = rd<uint8_t>(0x80364AE8u);   /* playerNumber */
    if (player > 3) return false;
    return (rd<uint32_t>(0x80364BE0u + player * 0x100u) & (1u << v)) == 0;   /* gPlayerControlModes */
}

void cameraHook(int snap) {
    const unsigned dyn = rd<uint8_t>(0x8035805Cu) & 1;   /* gBufferIndex */
    Cam &c = g_built[dyn];
    c.valid = false;
    mtxToDouble(kDyn + dyn * kDynSize + kDynView, c.view);
    mtxToDouble(kDyn + dyn * kDynSize + kDynProj, c.proj);
    mtxToDouble(kDyn + dyn * kDynSize + kDynCull, c.cullView);
    Vec3 fwd;
    if (!orbit::viewEye(c.view, c.eye, fwd)) return;
    /* gCamAtX/Y/Z */
    c.target = {rd<float>(0x80365084u), rd<float>(0x80365088u), rd<float>(0x8036508Cu)};
    c.snap = snap != 0;
    c.mode = BC_MODE;
    c.level = BC_LEVEL;
    c.onFoot = rd<uint8_t>(0x80364456u) == 0;   /* gCurVehicle */
    c.relChar = relativeCharacter();
    c.play = playMode(c.mode);
    c.frame = plat_frames();
    c.valid = std::isfinite(c.target.x) && std::isfinite(c.target.y) && std::isfinite(c.target.z);
}

/* ---- the orbit, shared with RT64's render thread ------------------------------ */
orbit::Orbit g_orbit;
orbit::Limits g_lim;
bool g_active;            /* orbit mode, in play, the game's camera known */
double g_yawRate, g_pitchRate;   /* degrees a second now (the step's input) */
LARGE_INTEGER g_freq;

/* the cells a frame's drawing list had, with their boxes (--orbit-log: each
 * picture's view is checked against them) */
struct CellBox {
    Vec3 lo, hi;
};
struct CellSet {
    std::vector<CellBox> boxes;
    std::vector<uint8_t> drawn;
    double tanX = 0, tanY = 0, farDist = 0;
    Vec3 eye, target;             /* the view the list was made with ... */
    double margin = 0, back = 0;  /* ... and its margins */
};

/* what each rendered frame (workload) needs */
struct Entry {
    uint64_t workload = 0;
    Cam cur, prev;
    bool active = false;
    std::shared_ptr<const CellSet> cells;   /* --orbit-log */
    /* the orbit steps (Shared::steps) the frame and the frame before it were made after: a picture
     * with weight w between them shows the orbit at prevStep + w (curStep - prevStep) (viewHook) */
    uint64_t curStep = 0, prevStep = 0;
    /* (--orbit-log) the target marker's sides' middles in this frame's and the frame before's orbit view */
    bool mk = false, mkPrev = false;
    int mkX[4] = {}, mkY[4] = {}, mkPX[4] = {}, mkPY[4] = {};
    uint32_t mkPts = 0;
};
void markerCheck(const Entry &e, float w, const Vec3 &eye, const Vec3 &target);
std::mutex g_mutex;       /* g_ring, g_shared */
std::vector<Entry> g_ring;
/* The orbit after each step (orbitVi, once a retrace: the game's virtual time).
 * A picture's view is the orbit at the picture's own time, between the two
 * steps around it (Orbit::between), as the game's camera and the world in it
 * are interpolated by the picture's weight: the view moves on evenly from
 * picture to picture at any frame rate, whenever RT64's render thread draws
 * them (it draws a frame's in-between pictures together, ahead of their
 * presents).  The input is read and the collision, rise and pull-in worked
 * out once a step, as before; pictures between steps blend those results. */
struct Snap {
    uint64_t step = 0;
    orbit::Orbit orbit;
    orbit::Limits lim;
};
constexpr size_t kSnaps = 64;   /* ~1 s of steps (an entry's frames are a few steps old at most) */
struct Shared {
    orbit::Orbit orbit;           /* the newest */
    orbit::Limits lim;
    uint64_t step = 0;            /* steps taken (the newest snapshot's) */
    Snap snaps[kSnaps];           /* by step % kSnaps */
} g_shared;

/* (g_mutex held) the orbit at step position `pos` (steps, fractional): between the snapshots around it, the
 * nearest one kept when it is out of range */
void orbitAt(double pos, orbit::Orbit &o, orbit::Limits &lim) {
    const Shared &s = g_shared;
    if (s.step == 0) {
        o = s.orbit, lim = s.lim;
        return;
    }
    const uint64_t oldest = s.step >= kSnaps ? s.step - kSnaps + 1 : 1;
    pos = std::max(double(oldest), std::min(double(s.step), pos));
    const uint64_t a = uint64_t(std::floor(pos)), b = std::min(s.step, a + 1);
    const Snap &sa = s.snaps[a % kSnaps], &sb = s.snaps[b % kSnaps];
    o = orbit::Orbit::between(sa.orbit, sb.orbit, pos - double(a));
    lim = sb.lim;
}
Cam g_prevDrawn;          /* the frame drawn before (main thread) */
bool g_lastCut;           /* the last frame drawn was a cut (orbitBeforeTask) */
std::atomic<unsigned> g_hooked;   /* pictures drawn with the orbit's view (render thread) */
unsigned g_bdFrames;      /* frames whose backdrop followed the orbit */

bool sameView(const interop::float4x4 &a, const double b[4][4]) {
    for (int r = 0; r < 4; r++)
        for (int c = 0; c < 4; c++) {
            const double x = double(a[r][c]), y = b[r][c];
            if (std::fabs(x - y) > 1e-3 * std::max(1.0, std::fabs(y))) return false;
        }
    return true;
}

void toFloat(const double m[4][4], interop::float4x4 &out) {
    for (int r = 0; r < 4; r++)
        for (int c = 0; c < 4; c++) out[r][c] = float(m[r][c]);
}

void viewHookTimed(const RT64::ViewHookParams &p, interop::float4x4 &view, interop::float4x4 &proj,
                   interop::float4x4 &prevView, interop::float4x4 &prevProj);
/* RT64's render thread: every perspective projection of every picture */
void viewHook(const RT64::ViewHookParams &p, interop::float4x4 &view, interop::float4x4 &proj,
              interop::float4x4 &prevView, interop::float4x4 &prevProj) {
    LARGE_INTEGER t0, t1;
    QueryPerformanceCounter(&t0);
    viewHookTimed(p, view, proj, prevView, prevProj);
    QueryPerformanceCounter(&t1);
    g_viewTicks += uint64_t(t1.QuadPart - t0.QuadPart);
    g_views++;
}
void viewHookTimed(const RT64::ViewHookParams &p, interop::float4x4 &view, interop::float4x4 &proj,
                   interop::float4x4 &prevView, interop::float4x4 &prevProj) {
    (void) proj;
    (void) prevProj;
    Entry e;
    /* the orbit at the picture's time and at the time of the picture before it (RT64's previous
     * view: its motion vectors), by the weights (Shared) */
    orbit::Orbit oCur, oPrev;
    orbit::Limits limCur, limPrev;
    double posCur;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        const Entry *found = nullptr;
        for (const Entry &x : g_ring)
            if (x.workload == p.workloadId) found = &x;
        if (found == nullptr)   /* a re-submitted workload (the settings menu's pause): the newest of that view */
            for (auto it = g_ring.rbegin(); it != g_ring.rend() && found == nullptr; ++it)
                if (sameView(*p.gameView, it->cur.view)) found = &*it;
        if (found == nullptr || !found->active) return;
        if (!sameView(*p.gameView, found->cur.view)) return;   /* another projection (shadow, sub-view) */
        e = *found;
        const double span = double(e.curStep - e.prevStep);
        posCur = double(e.prevStep) + double(p.curFrameWeight) * span;
        orbitAt(posCur, oCur, limCur);
        orbitAt(double(e.prevStep) + double(p.prevFrameWeight) * span, oPrev, limPrev);
    }
    Vec3 curEye, curTarget;
    auto camFor = [&](float w, const orbit::Orbit &o, const orbit::Limits &lim, interop::float4x4 &out, bool cur) {
        orbit::Orbit::Game g;
        g.target = orbit::lerp(e.prev.target, e.cur.target, w);
        g.eye = orbit::lerp(e.prev.eye, e.cur.eye, w);
        Vec3 eye;
        o.eyeFor(g, 0.0, 0.0, lim, eye);
        double m[4][4];
        orbit::lookAt(eye, g.target, m);
        toFloat(m, out);
        if (cur) curEye = eye, curTarget = g.target;
    };
    camFor(p.curFrameWeight, oCur, limCur, view, true);
    camFor(p.prevFrameWeight, oPrev, limPrev, prevView, false);
    g_hooked++;
    if (e.mk && e.mkPrev) markerCheck(e, p.curFrameWeight, curEye, curTarget);   /* --orbit-log */
    if (g_viewLog != nullptr) {   /* --view-log */
        LARGE_INTEGER now;
        QueryPerformanceCounter(&now);
        double yaw, pitch;
        orbit::angles(curEye - curTarget, yaw, pitch);
        fprintf(g_viewLog, "%.3f,%llu,%u,%.4f,%.4f,%.4f,%.4f,%.4f\n", double(now.QuadPart) * 1e3 / double(g_freq.QuadPart),
                (unsigned long long) p.workloadId, e.cur.frame, double(p.curFrameWeight), yaw, pitch, oCur.yaw(), posCur);
    }
    /* --orbit-log: every cell this picture's view can see must be in the
     * frame's drawing list */
    if (e.cells) {
        const CellSet &cs = *e.cells;
        const orbit::ViewVolume exact(curEye, curTarget, cs.tanX, cs.tanY, 0.0, 0.0, cs.farDist);
        unsigned missed = 0;
        size_t first = 0;
        for (size_t i = 0; i < cs.boxes.size(); i++)
            if (!cs.drawn[i] && exact.boxVisibleExact(cs.boxes[i].lo, cs.boxes[i].hi)) {
                if (missed++ == 0) first = i;
            }
        g_checkPictures++;
        if (missed != 0) {
            g_checkMissCells += missed;
            if (g_checkMissPictures++ < 10) {
                const CellBox &b = cs.boxes[first];
                host_log("live: orbit drawing list: a picture of frame %u sees %u cells not drawn (cell %u box %.0f %.0f "
                         "%.0f .. %.0f %.0f %.0f; picture eye %.1f %.1f %.1f at %.1f %.1f %.1f; list eye %.1f %.1f %.1f at "
                         "%.1f %.1f %.1f, margin %.1f back %.1f)\n",
                         e.cur.frame, missed, unsigned(first), b.lo.x, b.lo.y, b.lo.z, b.hi.x, b.hi.y, b.hi.z, curEye.x,
                         curEye.y, curEye.z, curTarget.x, curTarget.y, curTarget.z, cs.eye.x, cs.eye.y, cs.eye.z,
                         cs.target.x, cs.target.y, cs.target.z, cs.margin, cs.back);
            }
        }
    }
}

/* --view-log with the game's own camera (testing): every perspective projection's view as RT64
 * interpolated it, unchanged (the first of each picture is the frame's) */
void gameViewLog(const RT64::ViewHookParams &p, interop::float4x4 &view, interop::float4x4 &, interop::float4x4 &,
                 interop::float4x4 &) {
    double m[4][4];
    for (int r = 0; r < 4; r++)
        for (int c = 0; c < 4; c++) m[r][c] = double(view[r][c]);
    Vec3 eye, fwd;
    if (!orbit::viewEye(m, eye, fwd)) return;
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    double yaw, pitch;
    orbit::angles(fwd * -1.0, yaw, pitch);
    fprintf(g_viewLog, "%.3f,%llu,%u,%.4f,%.4f,%.4f,%.4f,%.4f\n", double(now.QuadPart) * 1e3 / double(g_freq.QuadPart),
            (unsigned long long) p.workloadId, p.transformsIndex, double(p.curFrameWeight), yaw, pitch, 0.0, 0.0);
}

/* ---- ground height (read-only copies of the game's tests) --------------------- */
using orbit::triHeight;

/* every building's collision triangles: the gObjects records (0x803F4030,
 * 0xFC bytes each, up to gObjectsEnd at 0x803F7654) hold the range [+4, +8) of 0x60-byte triangles (77E20.c
 * CollisionTri: s32 vertices at +0x28 in world / 4 units, kept where the
 * building is) */
template <typename F> void forEachObstacle(F f) {
    const uint32_t end = rd<uint32_t>(0x803F7654u);
    if (end < 0x803F4030u || end > 0x803F4030u + 0xFCu * 64) return;
    for (uint32_t o = 0x803F4030u; o + 0xFC <= end; o += 0xFC) {
        const uint32_t a0 = rd<uint32_t>(o + 4), a1 = rd<uint32_t>(o + 8);
        if (a0 < 0x80000000u || a1 < a0 || a1 > 0x80800000u || a1 - a0 > 0x60u * 4096) continue;
        for (uint32_t a = a0; a + 0x60 <= a1; a += 0x60) f(reinterpret_cast<const int32_t *>(uintptr_t(a + 0x28)));
    }
}

/* the obstacles' triangles in drawing units, binned by x/z (orbit::TriGrid):
 * read again before each step's queries (the game moves and breaks them
 * between frames).  Testing every triangle for each of a step's many height
 * queries (~800 x 1700-2200 triangles) took most of a frame's time. */
orbit::TriGrid g_obstacles;
void groundRefresh();
std::vector<int32_t> g_obstRaw, g_obstNew;   /* the vertices the grid was made from; this step's */
void obstaclesRead() {
    g_obstNew.clear();
    forEachObstacle([&](const int32_t *v) { g_obstNew.insert(g_obstNew.end(), v, v + 9); });
    if (g_obstNew != g_obstRaw || g_obstacles.size() * 9 != g_obstNew.size()) {   /* (mostly unchanged) */
        g_obstRaw.swap(g_obstNew);
        g_perf.obstacleBuilds++;
        g_obstacles.clear();
        for (size_t i = 0; i + 9 <= g_obstRaw.size(); i += 9) {
            const int32_t *v = &g_obstRaw[i];
            const Vec3 p[3] = {{v[0] / 8.0, v[1] / 8.0, v[2] / 8.0}, {v[3] / 8.0, v[4] / 8.0, v[5] / 8.0},
                               {v[6] / 8.0, v[7] / 8.0, v[8] / 8.0}};
            g_obstacles.add(p);
        }
        g_obstacles.build();
    }
    groundRefresh();
}

/* the level's ground: the collision grid (62740.c ground_scan_grid): cells of
 * gGroundCellW x gGroundCellD world units, gGroundCols columns, lists in
 * gGroundCells[] of 0x14-byte triangles (9 s16 in 1/32 world units).  A
 * height query tests the list of the cell it is in (up to ~500 triangles);
 * each cell's list is also kept binned (orbit::TriGrid, the same triangles),
 * checked against the game's bytes before each step and made again if they
 * changed. */
template <typename F> void groundCellTris(uint32_t a, uint32_t end, F f) {
    if (a >= 0x80000000u && end >= a && end - a < 0x40000 && end < 0x80800000u) {
        end -= 4;
        for (; a + 0x14 <= end; a += 0x14) {
            const int16_t *t = reinterpret_cast<const int16_t *>(uintptr_t(a));
            const Vec3 p[3] = {{double(t[0]), double(t[1]), double(t[2])},
                               {double(t[3]), double(t[4]), double(t[5])},
                               {double(t[6]), double(t[7]), double(t[8])}};
            f(p);
        }
    }
}
struct GroundCell {
    uint32_t a = 0, end = 0;
    std::vector<uint8_t> raw;     /* the list's bytes it was made from */
    orbit::TriGrid grid;
};
struct GroundCache {
    uint32_t cw = 0, cd = 0;
    int cols = 0, rows = 0;
    std::vector<GroundCell> cells;
} g_ground;

bool groundGrid(uint32_t &cw, uint32_t &cd, int &cols, int &rows) {
    cw = rd<uint32_t>(0x803BE718u), cd = rd<uint32_t>(0x803BE71Cu);
    cols = rd<uint16_t>(0x803BE720u), rows = rd<uint16_t>(0x803BE722u);
    return cw != 0 && cd != 0 && cols > 0 && rows > 0 && cols * rows < 0x10000;
}

void groundRefresh() {
    uint32_t cw, cd;
    int cols, rows;
    if (!groundGrid(cw, cd, cols, rows)) {
        g_ground = GroundCache();
        return;
    }
    if (cw != g_ground.cw || cd != g_ground.cd || cols != g_ground.cols || rows != g_ground.rows) {
        g_ground = GroundCache();
        g_ground.cw = cw, g_ground.cd = cd, g_ground.cols = cols, g_ground.rows = rows;
        g_ground.cells.resize(size_t(cols) * rows);
    }
    const uint32_t *cells = reinterpret_cast<const uint32_t *>(uintptr_t(0x803BDB10u));
    for (int i = 0; i < cols * rows; i++) {
        GroundCell &g = g_ground.cells[i];
        const uint32_t a = cells[i], end = cells[i + 1];
        const bool ok = a >= 0x80000000u && end >= a && end - a < 0x40000 && end < 0x80800000u;
        const size_t n = ok ? end - a : 0;
        if (a == g.a && end == g.end && n == g.raw.size() &&
            (n == 0 || memcmp(g.raw.data(), reinterpret_cast<const void *>(uintptr_t(a)), n) == 0))
            continue;
        g.a = a, g.end = end;
        g_perf.groundBuilds++;
        g.raw.assign(reinterpret_cast<const uint8_t *>(uintptr_t(a)), reinterpret_cast<const uint8_t *>(uintptr_t(a)) + n);
        g.grid.clear();
        groundCellTris(a, end, [&](const Vec3 *p) { g.grid.add(p); });
        g.grid.build();
    }
}

/* drawing units (world / 32); the highest surface under x, z that is at most
 * kReach above y (a bridge deck or roof higher above isn't the ground there) */
constexpr double kReach = 40.0;
double g_lastTerrain, g_lastObstacle;   /* (--orbit-log) the two sources of the last query */
double groundHeightOf(double x, double y, double z, bool obstacles);
double groundHeight(double x, double y, double z) {
    return groundHeightOf(x, y, z, true);
}
/* the terrain alone (collision = off: kept above it, through buildings) */
double terrainHeight(double x, double y, double z) {
    return groundHeightOf(x, y, z, false);
}
double groundHeightOf(double x, double y, double z, bool obstacles) {
    g_perf.queries++;
    double best = -1e31, h;
    g_lastTerrain = g_lastObstacle = -1e31;
    bool obstacle = false;
    auto take = [&](double v) {
        if (v <= y + kReach) best = std::max(best, v);
        double &l = obstacle ? g_lastObstacle : g_lastTerrain;
        if (v <= y + kReach) l = std::max(l, v);
    };
    uint32_t cw, cd;
    int cols, rows;
    if (groundGrid(cw, cd, cols, rows)) {
        const int cx = std::max(0, std::min(cols - 1, int(std::floor(x * 32.0 / cw))));
        const int cz = std::max(0, std::min(rows - 1, int(std::floor(z * 32.0 / cd))));
        const uint32_t *cells = reinterpret_cast<const uint32_t *>(uintptr_t(0x803BDB10u));
        const uint32_t a = cells[cz * cols + cx], end = cells[cz * cols + cx + 1];
        const size_t i = size_t(cz) * cols + cx;
        if (cols == g_ground.cols && rows == g_ground.rows && cw == g_ground.cw && cd == g_ground.cd &&
            g_ground.cells[i].a == a && g_ground.cells[i].end == end)
            g_ground.cells[i].grid.forEachAt(x, z, [&](const Vec3 *p) {
                if (triHeight(p, x, z, h)) take(h);
            });
        else
            groundCellTris(a, end, [&](const Vec3 *p) {
                if (triHeight(p, x, z, h)) take(h);
            });
    }
    /* buildings and other obstacles: the 0x60-byte collision triangles
     * (77E20.c CollisionTri: s32 vertices at +0x28 in world / 4 units), as
     * obstaclesRead last read them */
    if (!obstacles) return best;
    obstacle = true;
    g_obstacles.forEachAt(x, z, [&](const Vec3 *p) {
        double hh;
        if (triHeight(p, x, z, hh)) take(hh);
    });
    return best;
}

/* --orbit-log: every query also against all the obstacle triangles one by one
 * (as before the grid); the answers must be the same */
unsigned g_heightChecks, g_heightMismatches;
double groundHeightChecked(double x, double y, double z) {
    const double h = groundHeight(x, y, z), lastT = g_lastTerrain, lastO = g_lastObstacle;
    double o = -1e31;
    forEachObstacle([&](const int32_t *v) {
        const Vec3 p[3] = {{v[0] / 8.0, v[1] / 8.0, v[2] / 8.0}, {v[3] / 8.0, v[4] / 8.0, v[5] / 8.0},
                           {v[6] / 8.0, v[7] / 8.0, v[8] / 8.0}};
        double hh;
        if (triHeight(p, x, z, hh) && hh <= y + kReach) o = std::max(o, hh);
    });
    double t = -1e31;
    uint32_t cw, cd;
    int cols, rows;
    if (groundGrid(cw, cd, cols, rows)) {
        const int cx = std::max(0, std::min(cols - 1, int(std::floor(x * 32.0 / cw))));
        const int cz = std::max(0, std::min(rows - 1, int(std::floor(z * 32.0 / cd))));
        const uint32_t *cells = reinterpret_cast<const uint32_t *>(uintptr_t(0x803BDB10u));
        groundCellTris(cells[cz * cols + cx], cells[cz * cols + cx + 1], [&](const Vec3 *p) {
            double hh;
            if (triHeight(p, x, z, hh) && hh <= y + kReach) t = std::max(t, hh);
        });
    }
    const double all = std::max(t, o);
    g_heightChecks++;
    if (all != h || o != lastO || t != lastT) {
        if (g_heightMismatches++ < 10)
            host_log("live: orbit collision grid: query %.3f %.3f %.3f gave %.6f, every triangle %.6f (frame %u)\n", x, y, z,
                     h, all, plat_frames());
    }
    return h;
}

/* ---- input ------------------------------------------------------------------ */
double padValue(const Config &c, const PadState *pad, const PadInput &in) {
    if (in.button >= 0) return pad->button(in.button) ? 1.0 : 0.0;
    const int v = pad->axis(in.axis);
    if (in.axis == SDL_CONTROLLER_AXIS_TRIGGERLEFT || in.axis == SDL_CONTROLLER_AXIS_TRIGGERRIGHT)
        return v > 8000 ? std::min(1.0, (v - 8000) / 24000.0) : 0.0;
    const double d = double(v) * in.dir;
    if (d <= c.deadzone) return 0.0;
    return std::min(1.0, (d - c.deadzone) / (32767.0 - c.deadzone));
}

double actionValue(const Config &c, const PadSet &pads, const Uint8 *keys, int act) {
    double v = 0;
    if (keys != nullptr)
        for (SDL_Scancode sc : c.keys[act])
            if (keys[sc]) v = 1.0;
    for (const PadState *pad : pads.active(c))
        for (const PadInput &in : c.pad[act]) v = std::max(v, padValue(c, pad, in));
    return v;
}

double g_mouseX, g_mouseY, g_wheel;   /* since the last step */
bool g_dragging;
bool g_resetHeld;

/* --orbit-test (g_test: above); sweep: the stress test (orbit::sweepAt, from the steps in play); hold:DEG: kept
 * DEG from the game's camera */
double g_testYaw, g_testPitch = 1e9, g_testZoom = 0, g_testHold = 1e9;
bool g_setPending;                /* --orbit-test set:YAW[:PITCH[:ZOOM]] (once, at the first step in play) */
double g_setYaw, g_setPitch = 1e9, g_setZoom = 0;
orbit::CollisionStats g_cstats;   /* how calm the collision was (orbitSummary) */
bool g_stepLog;                   /* --orbit-steplog */
/* camera-relative steering (steerHook) */
unsigned g_steerReads, g_steerTurned;
double g_steerMaxTurn;
bool g_sweep;
unsigned g_sweepSteps;

void buildPadConfig() {
    g_padCfg = *g_cfg;
    for (int b = 0; b < kButtons; b++) {
        std::vector<PadInput> keep;
        for (const PadInput &in : g_padCfg.pad[b]) {
            bool cam = false;
            for (int a = kCameraActions; a < A_COUNT; a++)
                for (const PadInput &c : g_cfg->pad[a])
                    cam |= c.button == in.button && c.axis == in.axis && c.dir == in.dir;
            if (!cam) keep.push_back(in);
        }
        g_padCfg.pad[b] = keep;
        std::vector<SDL_Scancode> keys;
        for (SDL_Scancode k : g_padCfg.keys[b]) {
            bool cam = false;
            for (int a = kCameraActions; a < A_COUNT; a++)
                for (SDL_Scancode c : g_cfg->keys[a]) cam |= c == k;
            if (!cam) keys.push_back(k);
        }
        g_padCfg.keys[b] = keys;
    }
}

/* ---- host work (g_perf: above; the frame-time report, --perf-log) ----------------- */
struct PerfTimer {
    double &acc;
    LARGE_INTEGER t0;
    explicit PerfTimer(double &a) : acc(a) {
        QueryPerformanceCounter(&t0);
    }
    ~PerfTimer() {
        LARGE_INTEGER t1;
        QueryPerformanceCounter(&t1);
        acc += double(t1.QuadPart - t0.QuadPart) * 1e3 / double(g_freq.QuadPart);
    }
};

unsigned listLength(const short *l) {
    unsigned n = 0;
    while (l[n] != -1) n++;
    return n;
}

/* ---- how fast the orbit's view moves (the drawing list's margins) ---------------
 * Each step: the angle the view direction turned and the distance the eye
 * moved (drawing units); the drawing list takes the largest of the last
 * kMotionSteps, as speeds. */
constexpr int kMotionSteps = 8;
struct Motion {
    double turn[kMotionSteps] = {}, move[kMotionSteps] = {};
    int next = 0;
    bool have = false;
    Vec3 eye, dir;
    double turnPerSec() const { return *std::max_element(turn, turn + kMotionSteps) * 60.0; }
    double movePerStep() const { return *std::max_element(move, move + kMotionSteps); }
    void reset() { *this = Motion(); }
    void add(const Vec3 &e, const Vec3 &target) {
        const Vec3 d = orbit::normalize(target - e);
        if (have) {
            const double c = std::max(-1.0, std::min(1.0, orbit::dot(d, dir)));
            turn[next] = std::acos(c) * 180.0 / 3.14159265358979;
            move[next] = orbit::length(e - eye);
            next = (next + 1) % kMotionSteps;
        }
        eye = e, dir = d, have = true;
    }
} g_motion;

/* ---- the drawing list (PORT_DRAW_CELLS) ------------------------------------------
 * The game's drawing paths (5FD50.c's level lists, 77E20.c's buildings) draw
 * the cells of this list: the game's own (its camera's visibility walk,
 * gVisibleCells, unchanged: the game's logic reads only that one) and the
 * cells the orbit camera can see, ascending, -1 at the end.  The cells (the
 * level grid: lvl_build_visible_dl's cells, cull_test_cells' boxes) get the
 * game's own box (its grid of ground height ranges, as cull_box_corners) grown
 * by the collision triangles of the buildings standing in them (taller than
 * the ground, or reaching over the cell's edge).  The orbit's view is a
 * pyramid (orbit::ViewVolume) with the frame's projection, widescreen and
 * interpolation width (host_cull_wide), from the newest camera the game made
 * (the frame before's: the level's lists are built before cam_update), and
 * margins for that and for the pictures shown until the next list (the
 * view may turn and the eye move meanwhile): 20
 * degrees plus what the view turned in 0.25 s at its recent speed, the apex
 * 4 units plus 10 steps of the eye's recent motion back, and the far plane.
 * Turning faster than that can cover (a margin over 70 degrees: from 200
 * degrees a second), or the camera unknown: every cell, as before.  (The
 * cells cost little to draw: the margins are generous; a sudden mouse flick
 * from rest is the one case they can lag.)  Made once a frame (the
 * paths ask for it once per record), for the game's list it was given. */
struct DrawList {
    unsigned frame = ~0u;
    bool fresh = false;           /* made since the last frame task (orbitAfterTask) */
    uint32_t lvl = 0;
    std::vector<short> game;      /* the game's list it was made for */
    std::vector<short> list;      /* the drawing list */
    std::shared_ptr<CellSet> cells;
    unsigned all = 0, made = 0;   /* lists with every cell (fast turns, camera unknown), lists made */
} g_draw;

uint32_t levelData() {
    return rd<uint32_t>(0x80358074u);   /* gLevelData */
}

/* the n cells' boxes: the game's (ground heights) grown by its buildings' collision triangles */
void cellBoxes(uint32_t lvl, unsigned cols, unsigned rows, std::vector<CellBox> &boxes) {
    const int xs = rd<int16_t>(lvl + 4), zs = rd<int16_t>(lvl + 6);
    const uint32_t grid = lvl + rd<uint32_t>(lvl + 0x48);
    boxes.resize(size_t(cols) * rows);
    for (unsigned z = 0; z < rows; z++)
        for (unsigned x = 0; x < cols; x++) {
            const uint32_t g = grid + ((z * cols + x) << 2);
            CellBox &b = boxes[z * cols + x];
            /* cull_box_corners: unsigned 32-bit products, as s32 */
            b.lo = {double(int32_t(x * uint32_t(xs))), double(rd<int16_t>(g)), double(int32_t(z * uint32_t(zs)))};
            b.hi = {double(int32_t((x + 1) * uint32_t(xs))), double(rd<int16_t>(g + 2)),
                    double(int32_t((z + 1) * uint32_t(zs)))};
        }
    /* gObjects records: cell at +0xE8, collision triangles [+4, +8) (forEachObstacle) */
    const uint32_t end = rd<uint32_t>(0x803F7654u);
    if (end < 0x803F4030u || end > 0x803F4030u + 0xFCu * 64) return;
    for (uint32_t o = 0x803F4030u; o + 0xFC <= end; o += 0xFC) {
        const unsigned cell = rd<uint8_t>(o + 0xE8);
        if (cell >= boxes.size()) continue;
        const uint32_t a0 = rd<uint32_t>(o + 4), a1 = rd<uint32_t>(o + 8);
        if (a0 < 0x80000000u || a1 < a0 || a1 > 0x80800000u || a1 - a0 > 0x60u * 4096) continue;
        CellBox &b = boxes[cell];
        for (uint32_t a = a0; a + 0x60 <= a1; a += 0x60) {
            const int32_t *v = reinterpret_cast<const int32_t *>(uintptr_t(a + 0x28));
            for (int k = 0; k < 3; k++) {
                const Vec3 p = {v[k * 3] / 8.0, v[k * 3 + 1] / 8.0, v[k * 3 + 2] / 8.0};
                b.lo = {std::min(b.lo.x, p.x), std::min(b.lo.y, p.y), std::min(b.lo.z, p.z)};
                b.hi = {std::max(b.hi.x, p.x), std::max(b.hi.y, p.y), std::max(b.hi.z, p.z)};
            }
        }
    }
}

bool sameList(const short *a, const std::vector<short> &b) {
    for (size_t i = 0; i < b.size(); i++)
        if (a[i] != b[i]) return false;
    return true;   /* (b ends with its -1) */
}

short *drawCells(short *game) {
    if (!g_active) return game;
    PerfTimer timer(g_perf.cellsMs);
    const unsigned cols = rd<uint16_t>(0x803BE714u), rows = rd<uint16_t>(0x803BE716u);   /* gLevelCols, gLevelRows */
    const unsigned n = cols * rows;
    if (n == 0 || n > 0x7FFF) return game;
    const unsigned frame = plat_frames();
    const uint32_t lvl = levelData();
    if (frame == g_draw.frame && lvl == g_draw.lvl && !g_draw.list.empty() && sameList(game, g_draw.game))
        return g_draw.list.data();
    g_draw.frame = frame;
    g_draw.lvl = lvl;
    g_draw.game.assign(game, game + listLength(game) + 1);
    g_draw.made++;
    auto cs = std::make_shared<CellSet>();
    cs->drawn.assign(n, 0);
    for (size_t i = 0; i + 1 < g_draw.game.size(); i++)
        if (unsigned(uint16_t(g_draw.game[i])) < n) cs->drawn[uint16_t(g_draw.game[i])] = 1;
    /* the newest camera the game made: the level's lists are built before the
     * frame's own camera (cam_update), so it is the frame before's; the
     * margins cover the difference */
    const Cam &c = g_built[0].valid && (!g_built[1].valid || int(g_built[0].frame - g_built[1].frame) > 0) ? g_built[0]
                                                                                                             : g_built[1];
    double nearZ = 0, farZ = 0;
    const double s = -c.proj[2][3];
    bool all = g_cullAll || !c.valid || frame - c.frame > 4 || !orbit::perspectiveRange(c.proj, nearZ, farZ) ||
               !(std::fabs(c.proj[0][0]) > 1e-9 && std::fabs(c.proj[1][1]) > 1e-9);
    const double margin = 20.0 + g_motion.turnPerSec() * 0.25;
    if (margin > 70.0) all = true;
    if (all) {
        g_draw.all++;
        std::fill(cs->drawn.begin(), cs->drawn.end(), uint8_t(1));
    } else {
        cellBoxes(lvl, cols, rows, cs->boxes);
        Vec3 eye;
        g_orbit.eyeFor({c.eye, c.target}, 0, 0, g_lim, eye);
        cs->tanX = host_cull_wide * s / std::fabs(c.proj[0][0]);
        cs->tanY = s / std::fabs(c.proj[1][1]);
        cs->farDist = farZ * 1.02;
        cs->eye = eye;
        cs->target = c.target;
        cs->margin = margin;
        cs->back = 4.0 + 10.0 * g_motion.movePerStep();
        const orbit::ViewVolume vol(eye, c.target, cs->tanX, cs->tanY, margin, cs->back, cs->farDist);
        for (unsigned i = 0; i < n; i++)
            if (!cs->drawn[i] && vol.boxVisible(cs->boxes[i].lo, cs->boxes[i].hi)) cs->drawn[i] = 1;
        Vec3 cullEye, cullFwd;
        if (g_log && orbit::viewEye(c.cullView, cullEye, cullFwd)) {
            /* the same test from the game's culling view (its eye pushed back: cam_eye_scale) should see
             * (about) the cells the game listed (it walks the grid over a few frames: lags a little) */
            const orbit::ViewVolume gameVol(cullEye, c.target, cs->tanX, cs->tanY, 0.0, 0.0, cs->farDist);
            for (size_t i = 0; i + 1 < g_draw.game.size(); i++) {
                const unsigned k = uint16_t(g_draw.game[i]);
                if (k >= n) continue;
                g_gameCellsChecked++;
                if (!gameVol.boxVisible(cs->boxes[k].lo, cs->boxes[k].hi)) g_gameCellsOutside++;
            }
        }
    }
    g_draw.list.clear();
    for (unsigned i = 0; i < n; i++)
        if (cs->drawn[i]) g_draw.list.push_back(short(i));
    g_draw.list.push_back(-1);
    g_perf.lists++;
    g_perf.listsAll += all;
    g_perf.cells += unsigned(g_draw.list.size() - 1);
    g_perf.gameCells += unsigned(g_draw.game.size() - 1);
    g_draw.fresh = true;
    if (g_log && !all) g_draw.cells = cs;
    else g_draw.cells.reset();
    return g_draw.list.data();
}

/* ---- the backdrop -------------------------------------------------------------- */
struct Backdrop {
    bool on = false;          /* this frame's task: give the backdrop the orbit's heading and pitch */
    bool vtxDone = false, fillDone = false;
    int cimg = 0;             /* G_SETCIMG commands seen */
    int heading = 0, pitch = 0;
    int height = 0;           /* the horizon (lines from the top) of the orbit's view */
    RT64::DisplayList gfx[64];   /* the backdrop's commands for the orbit's view (port_backdrop_gfx) */
    int count = 0;
    short vtx[4][3] = {};
    RT64::DisplayList cycle = {}, fillColor = {};
    bool haveCycle = false, haveColor = false;
} g_bd;

/* the game's heading (0..0xFFF) and pitch values of a camera (00000.c
 * cam_update: gCamYaw, gCamPitch) */
void headingPitch(const Vec3 &eye, const Vec3 &at, int &heading, int &pitch) {
    const Vec3 d = eye - at;
    double yaw, pd;
    orbit::angles(d, yaw, pd);
    heading = int(yaw * 4096.0 / 360.0) & 0xFFF;
    const double dist = std::max(1.0, orbit::length(d));
    const double a = std::asin(std::min(1.0, std::fabs(d.y) / dist)) * 180.0 / 3.14159265358979;
    const double b = std::asin(std::min(1.0, std::fabs(d.y) / 20000.0)) * 180.0 / 3.14159265358979;
    const double p = (a - b) * 2048.0 / 90.0;
    pitch = int(d.y >= 0 ? p : -p);
}

/* the orbit's backdrop vertex values into gBackdropVtx[0 or 1] at `a`
 * (physical), saved first; put back by restoreVtx */
int16_t g_vtxSaved[4][8];
int16_t *patchVtx(uint32_t a, const short vals[4][3]) {
    int16_t *v = reinterpret_cast<int16_t *>(uintptr_t(0x80000000u | a));
    memcpy(g_vtxSaved, v, sizeof g_vtxSaved);
    for (int i = 0; i < 4; i++) {
        int16_t *q = v + i * 8;   /* Vtx: ob[3], flag, tc[2], cn[4] (host order) */
        if (vals[i][0] != 0x7FFF) q[1] = vals[i][0];
        q[4] = vals[i][1];
        q[5] = vals[i][2];
    }
    return v;
}
void restoreVtx(int16_t *v) {
    memcpy(v, g_vtxSaved, sizeof g_vtxSaved);   /* RDRAM as the game left it */
}

/* ---- the sky between frames ----------------------------------------------------
 * The backdrop is a 2D quad whose texture coordinates follow the camera's
 * heading (2D810.c: half a texel per 1/4096 turn): drawn once a game frame,
 * so with more pictures than game frames the sky stepped every game frame
 * while the world (and the orbit's view) turned every picture, and the
 * whole-unit heading added steps of half a texel (6 pixels at 4K).  In the
 * orbit's view the quad gets (orbitBeforeTask) texture coordinates from the
 * view's heading in fractions of a unit, unwrapped (continuous from frame to
 * frame: a whole texture repeat, 4096 in the vertex values, taken off when
 * they run far), and its draw an interpolation ID of its own with texture
 * coordinates and vertices interpolated (RT64's matrix group, set just for
 * its G_VTX and put back after it): RT64 then moves the sky picture by
 * picture between the two frames, as it does the world.  A cut (or a new
 * repeat taken off) gets a new ID: no blend from the frame before. */
constexpr uint32_t kSkyId = 0x534B5900u;   /* "SKY" */
struct SkyTrack {
    bool have = false;
    double heading = 0;       /* unwrapped (1/4096 turns) */
    double anchor = 0;        /* vertex s values taken off (multiples of 4096) */
    uint32_t epoch = 0;
    unsigned tagged = 0, newIds = 0;
} g_sky;

void skyGroupBegin(RT64::State *state, RT64::TransformGroup &saved) {
    RT64::RSP &rsp = *state->rsp;
    saved = rsp.extended.modelMatrixIdStack[rsp.extended.modelMatrixIdStackSize - 1];
    rsp.matrixId(kSkyId + (g_sky.epoch & 0xFF), false, false, false, G_EX_COMPONENT_INTERPOLATE,
                 G_EX_COMPONENT_INTERPOLATE, G_EX_COMPONENT_INTERPOLATE, G_EX_COMPONENT_INTERPOLATE,
                 G_EX_COMPONENT_INTERPOLATE, G_EX_COMPONENT_INTERPOLATE, G_EX_COMPONENT_INTERPOLATE, G_EX_COMPONENT_SKIP,
                 G_EX_COMPONENT_SKIP, G_EX_ORDER_LINEAR, G_EX_ASPECT_AUTO, G_EX_EDIT_NONE, false, false);
    rsp.modelViewProjChanged = true;   /* (its own transform for the quad) */
    g_sky.tagged++;
}
void skyGroupEnd(RT64::State *state, const RT64::TransformGroup &saved) {
    RT64::RSP &rsp = *state->rsp;
    rsp.extended.modelMatrixIdStack[rsp.extended.modelMatrixIdStackSize - 1] = saved;
    rsp.extended.modelMatrixIdStackChanged = true;
    rsp.modelViewProjChanged = true;   /* (the next vertices get a transform of their own again) */
}

/* ---- the target marker ---------------------------------------------------------
 * hud_draw_target_marker (30C70.c) puts four bouncing arrows round the next
 * target in 2D: the four corner points of its marker (gMarkedTargetPts)
 * projected with the game's view (DynamicBuf.unk140) and projection (unk80)
 * as hud_project_point does, an arrow at the middle of each side of their
 * box.  In the orbit view they stayed where the game's camera saw the
 * target and floated over whatever the orbit showed there.  After RT64 read
 * their vertices (G_VTX of 16 at DynamicBuf + 0x1F00), its own copies of
 * them are moved: each arrow by how far its side's middle is in the orbit's
 * view (the same projection, the frame's orbit view) from where the game
 * put it; the bounce is kept.  Which points: the marked target's, else the
 * nearest one's (gNearestTargetPts; the marked pointer is cleared in the
 * frame the marker ends), whichever puts the arrows where the game drew them.
 * A corner behind the orbit's eye hides them.  RDRAM is not changed. */
struct Marker {
    bool on = false;          /* this frame */
    uint32_t vtx = 0;         /* the frame's DynamicBuf + 0x1F00 (physical) */
    double view[4][4] = {}, proj[4][4] = {}, orbitView[4][4] = {};
    uint32_t pts[2] = {};     /* gMarkedTargetPts, gNearestTargetPts */
    unsigned moved = 0, hidden = 0, unmatched = 0;
    /* (--orbit-log) where this frame put the sides' middles in the orbit's view, and the points used */
    bool placed = false;
    int ox[4] = {}, oy[4] = {};
    uint32_t placedPts = 0;
} g_mk;

void markerFrame(const Cam &c, unsigned dyn, const double ro[4][4]) {
    g_mk.on = true;
    g_mk.vtx = (kDyn + dyn * kDynSize + 0x1F00u) & 0x7FFFFFu;
    memcpy(g_mk.view, c.view, sizeof g_mk.view);
    memcpy(g_mk.proj, c.proj, sizeof g_mk.proj);
    memcpy(g_mk.orbitView, ro, sizeof g_mk.orbitView);
    g_mk.pts[0] = rd<uint32_t>(0x8036C794u);   /* gMarkedTargetPts */
    g_mk.pts[1] = rd<uint32_t>(0x8036C790u);   /* gNearestTargetPts */
}

/* hud_project_point (30C70.c) with view v: false when behind the eye (0x4000, 0x4000) */
bool hudProject(const double v[4][4], const double p[4][4], const double w[3], int &sx, int &sy) {
    double e[4];
    for (int c = 0; c < 4; c++) e[c] = w[0] * v[0][c] + w[1] * v[1][c] + w[2] * v[2][c] + v[3][c];
    if (e[2] >= 0.0) {
        sx = sy = 0x4000;
        return false;
    }
    double q[4];
    for (int c = 0; c < 4; c++) q[c] = e[0] * p[0][c] + e[1] * p[1][c] + e[2] * p[2][c] + e[3] * p[3][c];
    double x = 160.0 + 160.0 * (q[0] / q[3]);
    double y = 240.0 - (120.0 + 120.0 * (q[1] / q[3]));
    if (std::fabs(x) >= 16384.0) x = x >= 0 ? 16384.0 : -16384.0;
    if (std::fabs(y) >= 16384.0) y = y >= 0 ? 16384.0 : -16384.0;
    sx = int(int16_t(int(x)));
    sy = int(int16_t(int(y)));
    return true;
}

/* the middles of the box's sides (arrows 0 top, 1 bottom, 2 left, 3 right) as the game works them out */
bool markerEdges(const double v[4][4], const double p[4][4], uint32_t pts, int ex[4], int ey[4]) {
    int minX = 0x7FFF, maxX = -0x8000, minY = 0x7FFF, maxY = -0x8000;
    bool front = true;
    for (int i = 0; i < 4; i++) {
        const double w[3] = {double(rd<int16_t>(pts + i * 6)), double(rd<int16_t>(pts + i * 6 + 2)),
                             double(rd<int16_t>(pts + i * 6 + 4))};
        int sx, sy;
        if (!hudProject(v, p, w, sx, sy)) front = false;
        minX = std::min(minX, sx), maxX = std::max(maxX, sx);
        minY = std::min(minY, sy), maxY = std::max(maxY, sy);
    }
    const int mx = (maxX - minX) / 2 + minX, my = (maxY - minY) / 2 + minY;
    ex[0] = mx, ey[0] = minY;
    ex[1] = mx, ey[1] = maxY;
    ex[2] = minX, ey[2] = my;
    ex[3] = maxX, ey[3] = my;
    return front;
}

bool markerVertices(RT64::State *state, RT64::DisplayList **dl, const GbiOriginal *originals) {
    const uint32_t w0 = (*dl)->w0, w1 = (*dl)->w1;
    const uint32_t a = state->rsp->fromSegmented(w1) & 0x7FFFFF;
    if (!g_mk.on || a != g_mk.vtx || !interpMarkerVertices(a, w0)) return false;
    RT64::WorkloadQueue &q = *state->ext.workloadQueue;
    auto &d = q.workloads[q.writeCursor].drawData;
    const uint32_t first = d.vertexCount();
    originals[0x04](state, dl);
    if (d.vertexCount() < first + 16 || d.posFloats.size() < size_t(first + 16) * 3) return true;
    float *pf = &d.posFloats[size_t(first) * 3];
    double cx[4], cy[4];
    for (int k = 0; k < 4; k++) {
        cx[k] = cy[k] = 0;
        for (int j = 0; j < 4; j++) cx[k] += pf[(k * 4 + j) * 3] / 4.0, cy[k] += pf[(k * 4 + j) * 3 + 1] / 4.0;
    }
    int gx[4], gy[4], ox[4], oy[4];
    bool found = false, front = false;
    for (uint32_t pts : g_mk.pts) {
        if (pts < 0x80000000u || pts >= 0x807FFFE8u) continue;
        markerEdges(g_mk.view, g_mk.proj, pts, gx, gy);
        if (std::fabs(cx[0] - gx[0]) <= 2.0 && std::fabs(cx[1] - gx[1]) <= 2.0 && std::fabs(cy[2] - gy[2]) <= 2.0 &&
            std::fabs(cy[3] - gy[3]) <= 2.0) {
            front = markerEdges(g_mk.orbitView, g_mk.proj, pts, ox, oy);
            found = true;
            g_mk.placed = front;
            g_mk.placedPts = pts;
            memcpy(g_mk.ox, ox, sizeof ox);
            memcpy(g_mk.oy, oy, sizeof oy);
            break;
        }
    }
    if (!found) {
        g_mk.unmatched++;
        return true;
    }
    for (int k = 0; k < 4; k++)
        for (int j = 0; j < 4; j++) {
            float *v = &pf[(k * 4 + j) * 3];
            if (front) {
                v[0] += float(ox[k] - gx[k]);
                v[1] += float(oy[k] - gy[k]);
            } else {
                v[0] = v[1] = -2000.0f;   /* behind the orbit's eye: off the picture */
            }
        }
    (front ? g_mk.moved : g_mk.hidden)++;
    /* what RT64 derived from the positions (rt64_rsp.cpp setVertexCommon) */
    const RT64::RSP &rsp = *state->rsp;
    const interop::RSPViewport &vp = rsp.viewportStack[rsp.viewportStackSize - 1];
    for (uint32_t i = first; i < first + 16 && i < d.posTransformed.size() && i < d.posScreen.size(); i++) {
        const float *p = &d.posFloats[size_t(i) * 3];
        const hlslpp::float4 tf = hlslpp::mul(hlslpp::float4(p[0], p[1], p[2], 1.0f), rsp.modelViewProjMatrix);
        d.posTransformed[i] = tf;
        d.posScreen[i] = (tf.xyz / hlslpp::float3(tf.w, -tf.w, tf.w)) * vp.scale + vp.translate;
    }
    return true;
}

/* --orbit-log (RT64's render thread, each picture of a frame with the marker in this and the frame before):
 * where the picture's own view puts the sides' middles against where RT64 shows the arrows (blended
 * between the two frames' by the picture's weight); the largest difference in N64 pixels */
struct MarkerCheck {
    std::mutex m;
    unsigned pictures = 0, over1 = 0, over3 = 0, logged = 0, offscreen = 0;
    double worst = 0, sum = 0;
} g_mkCheck;
void markerCheck(const Entry &e, float w, const Vec3 &eye, const Vec3 &target) {
    if (!g_log || e.mkPts == 0) return;
    double v[4][4];
    orbit::lookAt(eye, target, v);
    int x[4], y[4];
    if (!markerEdges(v, e.cur.proj, e.mkPts, x, y)) return;
    double err = 0;
    bool seen = false;   /* an arrow on the picture (16:9 HUD edges: x up to 60 beyond 0..320) */
    for (int k = 0; k < 4; k++) {
        const double bx = e.mkPX[k] + (e.mkX[k] - e.mkPX[k]) * double(w), by = e.mkPY[k] + (e.mkY[k] - e.mkPY[k]) * double(w);
        if (!(x[k] >= -60 && x[k] <= 380 && y[k] >= 0 && y[k] <= 240)) continue;
        err = std::max(err, std::hypot(bx - x[k], by - y[k]));   /* (the arrows on the picture) */
        seen = true;
    }
    std::lock_guard<std::mutex> lock(g_mkCheck.m);
    if (!seen) {
        g_mkCheck.offscreen++;
        return;
    }
    g_mkCheck.pictures++;
    g_mkCheck.over1 += err > 1.5;
    g_mkCheck.over3 += err > 3.0;
    g_mkCheck.worst = std::max(g_mkCheck.worst, err);
    g_mkCheck.sum += err;
    if (g_mkCheck.logged < 60 && err > 1.5) {
        g_mkCheck.logged++;
        char s[256];
        int n = 0;
        for (int k = 0; k < 4 && n < 200; k++)
            n += snprintf(s + n, sizeof s - n, " %d,%d -> %d,%d: %d,%d", e.mkPX[k], e.mkPY[k], e.mkX[k], e.mkY[k], x[k], y[k]);
        host_log("live: orbit marker check frame %u weight %.3f: largest difference %.2f; arrows (frame before -> frame: "
                 "picture's view)%s\n", e.cur.frame, double(w), err, s);
    }
}

bool commandHook(RT64::State *state, RT64::DisplayList **dl, const GbiOriginal *originals) {
    if (!g_bd.on) return false;
    const uint32_t w0 = (*dl)->w0, w1 = (*dl)->w1;
    const uint8_t op = uint8_t(w0 >> 24);
    if (op == 0x04 && g_mk.on && markerVertices(state, dl, originals)) return true;
    if (g_bd.fillDone) return false;
    /* (the frame's first fill rectangle clears the depth image: the backdrop
     * comes after the second G_SETCIMG, the frame buffer's) */
    if (op == 0xFF) g_bd.cimg++;
    if (g_bd.cimg < 2) return false;
    if (op == 0x04 && !g_bd.vtxDone) {   /* G_VTX: the game drew its backdrop quad (gBackdropVtx[buf]) */
        const uint32_t a = state->rsp->fromSegmented(w1) & 0x7FFFFF;
        if (a != 0x2FA820 && a != 0x2FA860) return false;
        g_bd.vtxDone = true;
        if (g_bd.count == 0) {   /* the orbit view shows no sky: an empty quad */
            int16_t *v = reinterpret_cast<int16_t *>(uintptr_t(0x80000000u | a));
            short flat[4][3];
            for (int i = 0; i < 4; i++) flat[i][0] = v[1], flat[i][1] = v[i * 8 + 4], flat[i][2] = v[i * 8 + 5];
            int16_t *p = patchVtx(a, flat);
            originals[op](state, dl);
            restoreVtx(p);
            return true;
        }
        int16_t *p = patchVtx(a, g_bd.vtx);
        RT64::TransformGroup saved;
        skyGroupBegin(state, saved);
        originals[op](state, dl);
        skyGroupEnd(state, saved);
        restoreVtx(p);
        g_bdFrames++;
        return true;
    }
    /* the fill mode the ground colour is drawn with (to restore after a backdrop of our own) */
    if (op == 0xBA && !g_bd.vtxDone) g_bd.cycle = **dl, g_bd.haveCycle = true;
    if (op == 0xF7) g_bd.fillColor = **dl, g_bd.haveColor = true;
    if (op == 0xF6) {   /* G_FILLRECT: the ground colour below the horizon */
        g_bd.fillDone = true;
        if (!g_bd.vtxDone && g_bd.count > 0 && g_bd.haveCycle && g_bd.haveColor) {
            /* the game drew no backdrop (its camera saw no sky) but the orbit's
             * does: the commands the game would have written, from host memory */
            int16_t *p = patchVtx(0x2FA820, g_bd.vtx);
            for (int i = 0; i < g_bd.count; i++) {
                RT64::DisplayList *c = &g_bd.gfx[i];
                const uint32_t cop = c->w0 >> 24, cw1 = c->w1;
                const RT64::WorkloadQueue &q = *state->ext.workloadQueue;
                const uint32_t first = q.workloads[q.writeCursor].drawData.vertexCount();
                RT64::TransformGroup saved;
                if (cop == 0x04) skyGroupBegin(state, saved);
                originals[cop](state, &c);
                if (cop == 0x04) skyGroupEnd(state, saved);
                if (cop == 0x04) wideAfterVertex(state, cw1, first);   /* widescreen: as wide as the picture */
            }
            restoreVtx(p);
            RT64::DisplayList *c = &g_bd.cycle;
            originals[0xBA](state, &c);
            c = &g_bd.fillColor;
            originals[0xF7](state, &c);
            g_bdFrames++;
        }
        const int uly = std::max(0, std::min(239, g_bd.height <= 0 ? 0 : g_bd.height - 1));
        RT64::DisplayList cmd = **dl;
        cmd.w1 = (w1 & ~0xFFFu) | (uint32_t(uly) << 2);
        RT64::DisplayList *p = &cmd;
        originals[op](state, &p);
        return true;
    }
    return false;
}

/* ---- billboards ---------------------------------------------------------------
 * Smoke, explosions and particles (60F60.c fx_build_matrix, 43A60.c) are quads
 * the game turns to face its own camera (a rotation by the camera's pitch
 * and heading in their model matrix).  In the orbit view they are turned to
 * face the orbit camera instead: a model matrix whose rotation is (up to its
 * uniform scale) about the inverse of the game view's rotation gets
 * A' = A Rg Ro^T (row vectors: world = local A, camera = world R; for a
 * billboard A = s Rg^T, so A' = s Ro^T).  Only RT64's copy of the matrix
 * changes. */
struct Billboards {
    bool on = false;          /* this frame's task */
    double rg[3][3] = {}, x[3][3] = {};   /* the game view's rotation; Rg Ro^T */
    Vec3 gameEye, orbitEye;   /* the frame's eyes */
    Vec3 gameRight, gameBack, orbitRight, orbitBack;   /* the views' right and backward axes, level */
    unsigned turned = 0;      /* by A Rg Ro^T (facing the game's view: the middle of its picture) */
    unsigned faced = 0;       /* rebuilt to face the orbit's eye (billboardFace) */
} g_bb;

/* The game's billboards (60F60.c fx_build_matrix, 43A60.c ptcl_draw) are not
 * turned by the inverse of its view: their model matrix is a uniform scale,
 * a turn about x by the pitch from the billboard up (or down) to the game's
 * eye, then a turn about y by the camera's heading (gCamYaw), so a billboard
 * away from the middle of the picture is tilted against the view by up to
 * half the field of view.  A Rg was then too far from s I for the test above
 * (12 %), and those billboards kept facing the game's camera: seen from the
 * orbit camera's side they were thin slivers, stretched across the picture
 * (smoke clouds, explosions), and one popped between the two as it crossed
 * the picture.  Recognised by that build (rows of equal length at right
 * angles; x level and along the game camera's right; the normal tilted
 * towards the game's eye exactly), such a matrix is rebuilt the same way
 * for the orbit's eye: x along the orbit camera's right, the normal at the
 * pitch from the billboard to the orbit's eye, the rows' lengths and signs
 * (which way the quad faces, any mirroring) kept. */
Vec3 level(const Vec3 &v) {
    const Vec3 h = {v.x, 0.0, v.z};
    const double l = orbit::length(h);
    return l > 1e-9 ? h * (1.0 / l) : Vec3{};
}
bool billboardFace(hlslpp::float4x4 &m) {
    Vec3 r[3];
    double len[3];
    for (int i = 0; i < 3; i++) {
        r[i] = {double(float(m[i][0])), double(float(m[i][1])), double(float(m[i][2]))};
        len[i] = orbit::length(r[i]);
        if (!(len[i] > 1e-9)) return false;
    }
    const double lmax = std::max({len[0], len[1], len[2]}), lmin = std::min({len[0], len[1], len[2]});
    if (lmax > lmin * 1.03) return false;
    Vec3 n[3];
    for (int i = 0; i < 3; i++) n[i] = r[i] * (1.0 / len[i]);
    if (std::fabs(orbit::dot(n[0], n[1])) > 0.03 || std::fabs(orbit::dot(n[0], n[2])) > 0.03 ||
        std::fabs(orbit::dot(n[1], n[2])) > 0.03)
        return false;
    /* x level, along the game camera's right */
    const double sx = orbit::dot(n[0], g_bb.gameRight);
    if (std::fabs(n[0].y) > 0.03 || std::fabs(sx) < 0.996) return false;
    /* the normal: the camera's backward axis tilted by the pitch to the eye */
    const Vec3 p = {double(float(m[3][0])), double(float(m[3][1])), double(float(m[3][2]))};
    const Vec3 dg = g_bb.gameEye - p;
    const double dgl = orbit::length(dg);
    if (!(dgl > 1e-6)) return false;
    const double eg = std::asin(std::max(-1.0, std::min(1.0, dg.y / dgl)));
    const Vec3 up = {0, 1, 0};
    const Vec3 ng = g_bb.gameBack * std::cos(eg) + up * std::sin(eg);
    const double sn = orbit::dot(n[2], ng);
    if (std::fabs(sn) < 0.996) return false;
    /* the same build for the orbit's eye */
    const Vec3 dor = g_bb.orbitEye - p;
    const double dol = orbit::length(dor);
    if (!(dol > 1e-6)) return false;
    const double eo = std::asin(std::max(-1.0, std::min(1.0, dor.y / dol)));
    const Vec3 nz = (g_bb.orbitBack * std::cos(eo) + up * std::sin(eo)) * (sn < 0 ? -1.0 : 1.0);
    const Vec3 nx = g_bb.orbitRight * (sx < 0 ? -1.0 : 1.0);
    const double sy = orbit::dot(n[1], orbit::cross(n[2], n[0]));
    const Vec3 ny = orbit::cross(nz, nx) * (sy < 0 ? -1.0 : 1.0);
    const Vec3 out[3] = {nx * len[0], ny * len[1], nz * len[2]};
    float f[4][4];
    for (int i = 0; i < 4; i++)
        for (int c = 0; c < 4; c++) f[i][c] = float(m[i][c]);
    for (int i = 0; i < 3; i++) {
        f[i][0] = float(out[i].x);
        f[i][1] = float(out[i].y);
        f[i][2] = float(out[i].z);
    }
    m = hlslpp::float4x4(f[0][0], f[0][1], f[0][2], f[0][3], f[1][0], f[1][1], f[1][2], f[1][3], f[2][0], f[2][1],
                         f[2][2], f[2][3], f[3][0], f[3][1], f[3][2], f[3][3]);
    return true;
}

void matrixHook(RT64::State *state, uint8_t params) {
    if (!g_bb.on) return;
    RT64::RSP &rsp = *state->rsp;
    if (params & rsp.projMask) return;
    hlslpp::float4x4 &m = rsp.modelMatrixStack[rsp.modelMatrixStackSize - 1];
    if (billboardFace(m)) {
        rsp.modelViewProjChanged = true;
        g_bb.faced++;
        return;
    }
    double a[3][3], b[3][3];
    for (int r = 0; r < 3; r++)
        for (int c = 0; c < 3; c++) a[r][c] = float(m[r][c]);
    double s = 0;
    for (int r = 0; r < 3; r++)
        for (int c = 0; c < 3; c++) {
            b[r][c] = a[r][0] * g_bb.rg[0][c] + a[r][1] * g_bb.rg[1][c] + a[r][2] * g_bb.rg[2][c];
            if (r == c) s += b[r][c] / 3.0;
        }
    if (s <= 1e-6) return;
    for (int r = 0; r < 3; r++)
        for (int c = 0; c < 3; c++)
            if (std::fabs(b[r][c] - (r == c ? s : 0.0)) > 0.12 * s) return;   /* not facing the game camera */
    float f[4][4];
    for (int r = 0; r < 4; r++)
        for (int c = 0; c < 4; c++)
            f[r][c] = (r < 3 && c < 3) ? float(a[r][0] * g_bb.x[0][c] + a[r][1] * g_bb.x[1][c] + a[r][2] * g_bb.x[2][c])
                                       : float(m[r][c]);
    m = hlslpp::float4x4(f[0][0], f[0][1], f[0][2], f[0][3], f[1][0], f[1][1], f[1][2], f[1][3], f[2][0], f[2][1],
                         f[2][2], f[2][3], f[3][0], f[3][1], f[3][2], f[3][3]);
    rsp.modelViewProjChanged = true;
    g_bb.turned++;
}

/* ---- camera-relative steering ([camera] relative_steering) --------------------
 * The characters the game steers relative to its own camera (relativeCharacter)
 * would go where the stick points in the game's view, not the orbit's: with the
 * view turned round, up on the stick went towards the viewer.  Each controller
 * read (plat_input_read, the file's or the player's) while the orbit shows a
 * turned view in play, the stick is turned by the difference between the
 * game's view and the orbit's (orbit::steerTurn), its length kept, before the
 * game reads it.  Nothing else changes: no stick, the game's own view (orbit
 * not engaged), the pause menu (gPaused), another vehicle or control method,
 * the orbit off: the game gets the input as it was. */
void steerHook(unsigned short *button, signed char *x, signed char *y) {
    (void) button;
    if (!g_active || g_cfg == nullptr || !g_cfg->camRelativeSteering) return;
    if (*x == 0 && *y == 0) return;
    const Cam &c = g_last;
    if (!c.valid || !c.play || !g_orbit.engaged()) return;
    if (BC_MODE != 4 || rd<uint8_t>(0x802E8BD0u) != 0 /* gPaused */ || !relativeCharacter()) return;
    g_steerReads++;
    double gy, gp, vy, vp;
    orbit::angles(c.eye - c.target, gy, gp);
    Vec3 eye;
    g_orbit.eyeFor({c.eye, c.target}, 0, 0, g_lim, eye);
    orbit::angles(eye - c.target, vy, vp);
    const double turn = orbit::steerTurn(gy, vy);
    if (std::fabs(turn) < 0.5) return;
    int ox, oy;
    orbit::rotateStick(*x, *y, turn, ox, oy);
    if (g_log && (g_steerTurned % 60) == 0)
        host_log("live: orbit steering: stick %d,%d -> %d,%d (game view %.1f, orbit view %.1f: turned %.1f; vehicle %u, "
                 "frame %u)\n", *x, *y, ox, oy, gy, vy, turn, unsigned(rd<uint8_t>(0x80364456u)), plat_frames());
    *x = (signed char) ox;
    *y = (signed char) oy;
    g_steerTurned++;
    g_steerMaxTurn = std::max(g_steerMaxTurn, std::fabs(turn));
}

}  // namespace

/* ---- the interface ---------------------------------------------------------- */
void orbitSetup(RT64::Application *app, Config *cfg, SDL_Window *window) {
    g_app = app;
    g_cfg = cfg;
    g_window = window;
    QueryPerformanceFrequency(&g_freq);
    orbitApply();
}

void orbitApply() {
    const bool on = g_cfg->cameraMode == 1;
    host_camera_hook = on ? cameraHook : nullptr;
    host_draw_cells_hook = on ? drawCells : nullptr;
    host_input_hook = on ? steerHook : nullptr;
    RT64::ViewHook = on ? viewHook : g_viewLog != nullptr ? gameViewLog : nullptr;
    interpSetCommandHook(on ? commandHook : nullptr, on ? matrixHook : nullptr);
    if (!on) {
        g_active = false;
        g_orbit.reset(true);
        g_built[0].valid = g_built[1].valid = false;
        std::lock_guard<std::mutex> lock(g_mutex);
        g_ring.clear();
    }
    g_lim.pitchMin = g_cfg->camPitchMin;
    g_lim.pitchMax = g_cfg->camPitchMax;
    g_lim.zoomMin = g_cfg->camZoomMin;
    g_lim.zoomMax = g_cfg->camZoomMax;
    if (g_lim.follow != g_cfg->camFollow) g_orbit.reset(false);   /* (the kept angles mean something else) */
    g_lim.follow = g_cfg->camFollow;
    g_lim.recentre = g_cfg->camRecentre;
    g_lim.recentreDelay = g_cfg->camRecentreDelay;
    g_lim.collision = g_cfg->camCollision;
    buildPadConfig();
    host_log("live: camera %s\n", on ? "orbit (free camera, render only)" : "original");
}

bool orbitActive() {
    return g_active;
}

const Config &orbitPadConfig() {
    return g_active ? g_padCfg : *g_cfg;
}

bool orbitEvent(const SDL_Event &e) {
    if (!g_active || !g_cfg->camMouse) {
        if (g_dragging) {
            SDL_SetRelativeMouseMode(SDL_FALSE);
            g_dragging = false;
        }
        return false;
    }
    switch (e.type) {
    case SDL_MOUSEBUTTONDOWN:
        if (e.button.button == SDL_BUTTON_LEFT || e.button.button == SDL_BUTTON_RIGHT) {
            g_dragging = true;
            SDL_SetRelativeMouseMode(SDL_TRUE);
            return true;
        }
        if (e.button.button == SDL_BUTTON_MIDDLE) {
            g_resetHeld = true;
            return true;
        }
        break;
    case SDL_MOUSEBUTTONUP:
        if (g_dragging && (SDL_GetMouseState(nullptr, nullptr) & (SDL_BUTTON_LMASK | SDL_BUTTON_RMASK)) == 0) {
            g_dragging = false;
            SDL_SetRelativeMouseMode(SDL_FALSE);
        }
        return true;
    case SDL_MOUSEMOTION:
        if (g_dragging) {
            g_mouseX += e.motion.xrel;
            g_mouseY += e.motion.yrel;
            return true;
        }
        break;
    case SDL_MOUSEWHEEL:
        g_wheel += e.wheel.preciseY != 0.0f ? e.wheel.preciseY : float(e.wheel.y);
        return true;
    default:
        break;
    }
    return false;
}

void orbitVi(const PadSet &pads, const Uint8 *keys) {
    /* one step a retrace (the game's virtual time: 60 a second in real time
     * when paced; --no-pace test runs then turn the camera the same per frame) */
    const double dt = 1.0 / 60.0;
    if (g_cfg->cameraMode != 1) return;
    const Cam &c = g_last;
    const bool was = g_active;
    g_active = c.valid && c.play;
    /* The view the player set (yaw, tilt, zoom) is kept through everything (getting in and out, pause, the
     * map view, message windows, the game's camera cuts) until camera_reset; a new level (its start, or
     * another level number) starts from the game's camera again, the zoom kept. */
    {
        static int lastLevel = -1;
        static bool wasStarting;
        const int level = BC_LEVEL;
        const bool starting = BC_MODE == 0x2000; /* MODE_LEVEL_START */
        if ((starting && !wasStarting) || level != lastLevel) {
            host_log("live: orbit camera: a new level (%d%s, frame %u): %s, zoom %.2f kept\n", level,
                     starting ? ", its start" : "", plat_frames(),
                     g_orbit.engaged() ? "back behind the player" : "behind the player", g_orbit.zoom());
            g_resets++;
        }
        if (starting || level != lastLevel) g_orbit.reset(false);
        lastLevel = level;
        wasStarting = starting;
    }
    if (!g_active) {
        if (was) host_log("live: orbit camera inactive (game mode %08llX, frame %u; the view is kept)\n", c.mode,
                          plat_frames());
        g_motion.reset();
        g_yawRate = g_pitchRate = 0;
        g_mouseX = g_mouseY = g_wheel = 0;
        return;
    }
    if (!was) {
        unsigned tris = 0;
        forEachObstacle([&](const int32_t *) { tris++; });
        /* the ground grid's lists (groundHeight) */
        unsigned ground = 0, most = 0;
        const int cols = rd<uint16_t>(0x803BE720u), rows = rd<uint16_t>(0x803BE722u);
        const uint32_t *cells = reinterpret_cast<const uint32_t *>(uintptr_t(0x803BDB10u));
        if (cols > 0 && rows > 0 && cols * rows < 0x10000)
            for (int i = 0; i < cols * rows; i++) {
                const uint32_t a = cells[i], e = cells[i + 1];
                if (a < 0x80000000u || e < a + 4 || e - a >= 0x40000) continue;
                const unsigned n = (e - 4 - a) / 0x14;
                ground += n;
                most = std::max(most, n);
            }
        const uint32_t lvl = levelData();
        const bool lvlOk = lvl >= 0x80000000u && lvl < 0x80800000u;
        host_log("live: orbit camera active (level %d, frame %u, vehicle %u; %u obstacle triangles, ground grid %dx%d "
                 "with %u triangles, at most %u in a cell; drawing cells %ux%u of %dx%d units)\n",
                 c.level, plat_frames(), unsigned(rd<uint8_t>(0x80364456u)), tris, cols, rows, ground, most,
                 unsigned(rd<uint16_t>(0x803BE714u)), unsigned(rd<uint16_t>(0x803BE716u)),
                 lvlOk ? int(rd<int16_t>(lvl + 4)) : 0, lvlOk ? int(rd<int16_t>(lvl + 6)) : 0);
    }
    static bool lastFoot;
    if (c.onFoot != lastFoot) {
        lastFoot = c.onFoot;
        if (g_log) host_log("live: orbit camera: the player is %s (frame %u)\n", c.onFoot ? "on foot" : "in a vehicle",
                            plat_frames());
    }
    /* input: rates from the sticks and keys, steps from the mouse */
    const Config &cf = *g_cfg;
    const double sx = cf.camSensX / 100.0 * (cf.camInvertX ? -1.0 : 1.0);
    const double sy = cf.camSensY / 100.0 * (cf.camInvertY ? -1.0 : 1.0);
    auto curve = [](double v) { return v * std::fabs(v) * 0.6 + v * 0.4; };
    const double turn = curve(actionValue(cf, pads, keys, A_CAM_RIGHT) - actionValue(cf, pads, keys, A_CAM_LEFT));
    double look = curve(actionValue(cf, pads, keys, A_CAM_UP) - actionValue(cf, pads, keys, A_CAM_DOWN));
    /* zoom: while camera_zoom_hold is held, look up / down zoom in / out (and the view doesn't tilt); the zoom
     * buttons zoom while held; both at the same rate (about twice as far in 0.8 s) */
    const bool zoomHold = actionValue(cf, pads, keys, A_CAM_ZOOM_HOLD) > 0.5;
    double zoomRate = actionValue(cf, pads, keys, A_CAM_ZOOM_OUT) - actionValue(cf, pads, keys, A_CAM_ZOOM_IN);
    if (zoomHold) {
        zoomRate -= look;
        look = 0;
    }
    orbit::Input in;
    /* stick right: the view turns right (the eye goes to smaller yaw); look up: the eye goes down */
    g_yawRate = -turn * 150.0 * sx;
    g_pitchRate = -look * 90.0 * sy;
    in.yaw = g_yawRate * dt - g_mouseX * 0.25 * sx;
    in.pitch = g_pitchRate * dt + g_mouseY * 0.2 * sy;
    in.zoom = zoomRate * 1.2 * dt - g_wheel * 0.15;
    if (g_log && in.zoom != 0.0)
        host_log("live: orbit zoom input %.4f (log2; buttons %.2f, hold %d, wheel %.2f; step dt %.4f s, frame %u)\n",
                 in.zoom, zoomRate, zoomHold ? 1 : 0, g_wheel, dt, plat_frames());
    /* (a click of the right stick pushed far over while turning or zooming - its default binding - is taken
     * for an accident: the view would jump back; a reset is a click with the stick near its centre) */
    const bool busy = std::fabs(actionValue(cf, pads, keys, A_CAM_RIGHT) - actionValue(cf, pads, keys, A_CAM_LEFT)) > 0.5 ||
                      std::fabs(actionValue(cf, pads, keys, A_CAM_UP) - actionValue(cf, pads, keys, A_CAM_DOWN)) > 0.5;
    const bool pressed = actionValue(cf, pads, keys, A_CAM_RESET) > 0.5;
    const bool reset = (pressed && !busy) || g_resetHeld;
    static bool resetWas, ignoredWas;
    if (pressed && busy && !ignoredWas) g_resetsIgnored++;
    ignoredWas = pressed && busy;
    in.reset = reset && !resetWas;
    resetWas = pressed || g_resetHeld;   /* (held on from an ignored click: no reset when the stick comes back) */
    if (in.reset) {
        g_userResets++;
        if (g_log) host_log("live: orbit camera: camera_reset (frame %u)\n", plat_frames());
    }
    g_resetHeld = false;
    g_mouseX = g_mouseY = g_wheel = 0;
    if (g_setPending) {   /* --orbit-test set:YAW[:PITCH[:ZOOM]]: put there once, then the player's input (none) */
        double gy, gp, vy, vp;
        orbit::angles(c.eye - c.target, gy, gp);
        g_orbit.viewAngles(gy, gp, g_lim, vy, vp);
        in = orbit::Input();
        in.yaw = orbit::wrap180(gy + g_setYaw - vy) + 1e-9;
        if (g_setPitch < 1e8) in.pitch = g_setPitch - vp;
        if (g_setZoom > 0) in.zoom = std::log2(g_setZoom / g_orbit.zoom());
        g_setPending = false;
        host_log("live: orbit test: the view set to the game's yaw %+.0f, pitch %.0f, zoom %.2f (frame %u)\n", g_setYaw,
                 g_setPitch < 1e8 ? g_setPitch : vp, g_setZoom > 0 ? g_setZoom : g_orbit.zoom(), plat_frames());
    }
    if (g_sweep) {   /* --orbit-test sweep: turning, tilting and zooming by itself */
        const orbit::Sweep sw = orbit::sweepAt(g_sweepSteps++ * dt, g_lim);
        g_testYaw = sw.yawRate;
        g_testPitch = sw.pitch;
        g_testZoom = sw.zoom;
    }
    if (g_test) {   /* --orbit-test: turning by itself */
        in = orbit::Input();
        g_yawRate = g_testYaw;
        g_pitchRate = 0;
        in.yaw = g_testYaw * dt;
        double gy, gp, vy, vp;
        orbit::angles(c.eye - c.target, gy, gp);
        g_orbit.viewAngles(gy, gp, g_lim, vy, vp);
        if (g_testHold < 1e8) {   /* hold:DEG: the game's camera's yaw + DEG */
            in.yaw = orbit::wrap180(gy + g_testHold - vy) + 1e-9;
            g_yawRate = 0;
        }
        if (g_testPitch < 1e8) in.pitch = (g_testPitch - vp) * std::min(1.0, dt * 4.0) + 1e-9;
        if (g_testZoom > 0) in.zoom = std::log2(g_testZoom / g_orbit.zoom());
    }
    orbit::Limits lim = g_lim;
    /* (the characters the game steers relative to its camera: with relative steering off, recentring keeps the
     * stick from feeling rotated for long) */
    lim.recentre = g_test ? false : (cf.camRecentre || (c.relChar && cf.camRecentreOnFoot && !cf.camRelativeSteering));
    orbit::Orbit::Game g{c.eye, c.target};
    {
        PerfTimer timer(g_perf.stepMs);
        {
            PerfTimer readTimer(g_perf.readMs);
            obstaclesRead();
        }
        if (g_log) g_orbit.step(g, in, dt, lim, groundHeightChecked, terrainHeight);
        else g_orbit.step(g, in, dt, lim, groundHeight, terrainHeight);
    }
    Vec3 stepEye;
    g_orbit.eyeFor(g, 0, 0, lim, stepEye);
    if (g_orbit.engaged() || std::fabs(g_orbit.zoom() - 1.0) > 1e-6) {
        g_perf.steps++;
        g_perf.closeSteps += g_orbit.distanceRatio() < 0.3;
        g_cstats.add(g_orbit.distanceRatio(), g_orbit.rise(), g_orbit.eyeInside(), dt, g_orbit.lift(),
                     stepEye.y - c.target.y);
        if (g_stepLog)   /* --orbit-steplog: every step's collision state */
            host_log("live: orbit step frame %u: pull-in %.3f (to %.3f) rise %.1f (to %.1f) lift %.1f inside %d\n",
                     plat_frames(), g_orbit.distanceRatio(), g_orbit.ratioTarget(), g_orbit.rise(), g_orbit.riseTarget(),
                     g_orbit.lift(), g_orbit.eyeInside() ? 1 : 0);
    } else {
        g_cstats.restart();
    }
    g_motion.add(stepEye, c.target);
    std::lock_guard<std::mutex> lock(g_mutex);
    g_shared.orbit = g_orbit;
    g_shared.lim = lim;
    g_shared.step++;
    Snap &snap = g_shared.snaps[g_shared.step % kSnaps];
    snap.step = g_shared.step;
    snap.orbit = g_orbit;
    snap.lim = lim;
}

void orbitBeforeTask(uint32_t dl, bool frameDl) {
    g_bd = Backdrop();
    g_bb.on = false;
    g_mk.on = false;
    if (frameDl) g_mk.placed = false;
    if (g_cfg->cameraMode != 1 || !frameDl) return;
    const bool skyWas = g_sky.have;   /* (the frame before had the orbit's sky: SkyTrack) */
    g_sky.have = false;
    PerfTimer timer(g_perf.taskMs);
    dl &= 0x3FFFFFF;
    const unsigned dyn = (dl - (kDyn & 0x3FFFFFF)) / kDynSize;
    if (dyn > 1 || !g_built[dyn].valid) return;
    Cam c = g_built[dyn];
    /* a cut (the game snapped its camera, a new mode or level): no blend from the frame before.  The
     * orbit keeps the player's view (a new level: orbitVi starts it from the game's camera again) */
    const bool cut = !g_last.valid || c.snap || c.mode != g_last.mode || c.level != g_last.level;
    if (cut && c.play) {
        g_cuts++;
        if (g_log) host_log("live: orbit camera: a cut (frame %u; the view kept)\n", plat_frames());
    }
    g_prevDrawn = cut ? c : g_last;
    g_lastCut = cut;
    g_last = c;
    if (g_active && c.play && (g_orbit.engaged() || std::fabs(g_orbit.zoom() - 1.0) > 1e-6)) {
        orbit::Orbit::Game g{c.eye, c.target};
        Vec3 eye;
        g_orbit.eyeFor(g, 0, 0, g_lim, eye);
        headingPitch(eye, c.target, g_bd.heading, g_bd.pitch);
        g_bd.on = true;
        const double scroll = 120.0 - double(int16_t(g_bd.pitch)) * 0.22 + rd<float>(0x8036BFC0u);
        g_bd.height = int(std::max(0.0, scroll));
        /* billboards: Rg (the game view's rotation) and Rg Ro^T */
        double ro[4][4];
        orbit::lookAt(eye, c.target, ro);
        g_bb.on = true;
        for (int r = 0; r < 3; r++)
            for (int k = 0; k < 3; k++) {
                g_bb.rg[r][k] = c.view[r][k];
                g_bb.x[r][k] = c.view[r][0] * ro[k][0] + c.view[r][1] * ro[k][1] + c.view[r][2] * ro[k][2];
            }
        /* (billboardFace: the eyes and the views' level right and backward axes; backward = right x up,
         * which holds looking straight down too) */
        g_bb.gameEye = c.eye;
        g_bb.orbitEye = eye;
        g_bb.gameRight = level({c.view[0][0], c.view[1][0], c.view[2][0]});
        g_bb.orbitRight = level({ro[0][0], ro[1][0], ro[2][0]});
        g_bb.gameBack = orbit::cross(g_bb.gameRight, {0, 1, 0});
        g_bb.orbitBack = orbit::cross(g_bb.orbitRight, {0, 1, 0});
        /* the target marker's arrows (markerVertices) */
        markerFrame(c, dyn, ro);
        static_assert(sizeof(RT64::DisplayList) == 8, "a Gfx command");
        g_bd.count = port_backdrop_gfx(g_bd.gfx, 64, kDyn + dyn * kDynSize, c.level, g_bd.heading, g_bd.pitch,
                                       g_bd.vtx);
        /* the sky's texture across: from the view's heading in fractions of a unit, unwrapped (SkyTrack) */
        if (g_bd.count > 0) {
            double yaw, pd;
            orbit::angles(eye - c.target, yaw, pd);
            const double h = yaw * 4096.0 / 360.0;
            g_sky.have = true;
            if (!skyWas || cut) {
                g_sky.heading = h;
                g_sky.epoch++, g_sky.newIds++;
            } else {
                double d = std::fmod(h - g_sky.heading, 4096.0);
                if (d >= 2048.0) d -= 4096.0;
                if (d < -2048.0) d += 4096.0;
                g_sky.heading += d;
            }
            const unsigned shift = rd<uint8_t>(0x802FA280u + uint32_t(c.level) * 0x18u + 0x14u) & 15;   /* [1].sShift */
            const double scale = double(1u << shift);
            double s0 = ((g_sky.heading - 2048.0) * 0.5 + 16.0) / scale * 32.0, s1 = 319.0 / scale * 32.0;
            /* (the vertex values are s16: a whole number of texture repeats taken off when they run far) */
            if (std::fabs(s0 - g_sky.anchor) > 16384.0) {
                g_sky.anchor = std::floor(s0 / 4096.0 + 0.5) * 4096.0;
                g_sky.epoch++, g_sky.newIds++;
            }
            s0 -= g_sky.anchor;
            for (int i = 0; i < 4; i++) g_bd.vtx[i][1] = short(std::lround((i & 1) ? s0 + s1 : s0));
        }
        if (g_log && plat_frames() % 30 == 0) {
            int gh, gp;
            headingPitch(c.eye, c.target, gh, gp);
            obstaclesRead();
            const double ge = groundHeight(eye.x, eye.y, eye.z), te = g_lastTerrain, ob = g_lastObstacle;
            const double gt = groundHeight(c.target.x, c.target.y, c.target.z);
            host_log("live: orbit frame %u%s: target %.0f %.0f %.0f, game eye %.0f %.0f %.0f (heading %X pitch %d), "
                     "orbit eye %.0f %.0f %.0f (heading %X pitch %d), yaw %.1f pitch %.1f zoom %.2f pull-in %.2f lift %.0f "
                     "rise %.1f, "
                     "ground under eye %.0f (terrain %.0f, obstacles %.0f), under the target %.0f\n",
                     plat_frames(), c.onFoot ? " (on foot)" : "", c.target.x, c.target.y, c.target.z, c.eye.x, c.eye.y, c.eye.z, gh, gp, eye.x,
                     eye.y, eye.z, g_bd.heading, g_bd.pitch, g_orbit.yaw(), g_orbit.pitch(), g_orbit.zoom(),
                     g_orbit.distanceRatio(), g_orbit.lift(), g_orbit.rise(), ge, te, ob, gt);
        }
    }
}

void orbitAfterTask(bool frameDl) {
    if (g_cfg->cameraMode != 1 || !frameDl || !g_last.valid) return;
    static uint64_t lastId;
    const uint64_t id = g_app->state->workloadId;
    if (id == lastId) return;
    lastId = id;
    Entry e;
    e.workload = id;
    e.cur = g_last;
    e.prev = g_prevDrawn;
    e.active = g_active && g_last.play;
    if (g_log && g_draw.fresh) e.cells = g_draw.cells;   /* the list this frame's lists were made with */
    g_draw.fresh = false;
    if (g_log) {   /* (markerCheck) */
        static Entry last;
        e.mk = g_mk.placed;
        memcpy(e.mkX, g_mk.ox, sizeof e.mkX);
        memcpy(e.mkY, g_mk.oy, sizeof e.mkY);
        e.mkPts = g_mk.placedPts;
        e.mkPrev = last.mk && last.mkPts == e.mkPts && !g_lastCut;
        memcpy(e.mkPX, last.mkX, sizeof e.mkPX);
        memcpy(e.mkPY, last.mkY, sizeof e.mkPY);
        last.mk = e.mk, last.mkPts = e.mkPts;
        memcpy(last.mkX, e.mkX, sizeof e.mkX);
        memcpy(last.mkY, e.mkY, sizeof e.mkY);
    }
    std::lock_guard<std::mutex> lock(g_mutex);
    /* the steps the frame and the one before were made after (a cut: no blend from the frame before) */
    static uint64_t lastStep;
    e.curStep = g_shared.step;
    e.prevStep = g_lastCut || lastStep == 0 || lastStep > e.curStep ? e.curStep : lastStep;
    lastStep = e.active ? e.curStep : 0;
    g_ring.push_back(e);
    if (g_ring.size() > 8) g_ring.erase(g_ring.begin());
}

bool orbitSetTest(const std::string &spec) {
    if (spec == "sweep") {   /* the stress test: orbit::sweepAt */
        g_test = g_sweep = true;
        return true;
    }
    if (spec.compare(0, 4, "set:") == 0) {   /* set:YAW[:PITCH[:ZOOM]]: once (the kept view's tests) */
        double y = 0, p = 1e9, z = 0;
        const int n = sscanf(spec.c_str() + 4, "%lf:%lf:%lf", &y, &p, &z);
        if (n < 1) return false;
        g_setPending = true;
        g_setYaw = y;
        g_setPitch = n >= 2 ? p : 1e9;
        g_setZoom = n >= 3 ? z : 0;
        return true;
    }
    if (spec.compare(0, 5, "hold:") == 0) {   /* hold:DEG[:PITCH]: DEG from the game's camera's yaw */
        double h = 0, p = 1e9;
        const int n = sscanf(spec.c_str() + 5, "%lf:%lf", &h, &p);
        if (n < 1) return false;
        g_test = true;
        g_testYaw = 0;
        g_testHold = h;
        g_testPitch = n >= 2 ? p : 1e9;
        return true;
    }
    double y = 0, p = 1e9, z = 0;
    const int n = sscanf(spec.c_str(), "%lf:%lf:%lf", &y, &p, &z);
    if (n < 1) return false;
    g_test = true;
    g_testYaw = y;
    g_testPitch = n >= 2 ? p : 1e9;
    g_testZoom = n >= 3 ? z : 0;
    return true;
}

void orbitSetLog(bool on) {
    g_log = on;
}

void orbitSetStepLog(bool on) {
    g_stepLog = on;
}

OrbitPerf OrbitPerf::operator-(const OrbitPerf &o) const {
    OrbitPerf d;
    d.stepMs = stepMs - o.stepMs, d.cellsMs = cellsMs - o.cellsMs, d.readMs = readMs - o.readMs;
    d.taskMs = taskMs - o.taskMs, d.viewMs = viewMs - o.viewMs;
    d.lists = lists - o.lists, d.cells = cells - o.cells, d.gameCells = gameCells - o.gameCells;
    d.queries = queries - o.queries, d.listsAll = listsAll - o.listsAll;
    d.obstacleBuilds = obstacleBuilds - o.obstacleBuilds, d.groundBuilds = groundBuilds - o.groundBuilds;
    d.views = views - o.views;
    d.steps = steps - o.steps, d.closeSteps = closeSteps - o.closeSteps;
    return d;
}

namespace {
OrbitPerf perfTotals() {
    OrbitPerf p = g_perf;
    p.viewMs = double(g_viewTicks.load()) * 1e3 / double(g_freq.QuadPart ? g_freq.QuadPart : 1);
    p.views = unsigned(g_views.load());
    return p;
}
OrbitPerf g_perfTaken, g_frameTaken;
}  // namespace

OrbitPerf orbitPerfTake() {
    const OrbitPerf p = perfTotals(), d = p - g_perfTaken;
    g_perfTaken = p;
    return d;
}

OrbitPerf orbitFrameTake() {
    const OrbitPerf p = perfTotals(), d = p - g_frameTaken;
    g_frameTaken = p;
    return d;
}

void orbitSummary() {
    if (g_viewLog != nullptr) fflush(g_viewLog);   /* (RT64's render thread may still write: not closed) */
    if (g_cfg == nullptr || g_cfg->cameraMode != 1) return;
    host_log("live: orbit camera: %u pictures drawn with its view, %u cuts (view kept), %u new-level and %u "
             "camera_reset resets (%u clicks with the stick pushed ignored), backdrop turned with it in %u frames, %u "
             "billboards built to face its eye, %u other matrices facing the game's view turned to it\n",
             g_hooked.load(), g_cuts, g_resets, g_userResets, g_resetsIgnored, g_bdFrames, g_bb.faced, g_bb.turned);
    host_log("live: orbit target marker: arrows moved to the orbit's view in %u frames, hidden (target behind its "
             "eye) in %u, left as drawn (no target points matched) in %u\n",
             g_mk.moved, g_mk.hidden, g_mk.unmatched);
    if (g_mkCheck.pictures != 0) {
        std::lock_guard<std::mutex> lock(g_mkCheck.m);
        host_log("live: orbit target marker check (--orbit-log): %u pictures with an arrow on them (%u with none), the "
                 "arrows off where the picture's own view puts the target's sides by %.2f N64 pixels on average, at "
                 "most %.2f; over 1.5: %u, over 3: %u\n",
                 g_mkCheck.pictures, g_mkCheck.offscreen, g_mkCheck.sum / g_mkCheck.pictures, g_mkCheck.worst,
                 g_mkCheck.over1, g_mkCheck.over3);
    }
    host_log("live: orbit view kept: %s, recentre %s; at the end yaw %.1f pitch %.1f%s, zoom %.2f%s\n",
             g_lim.follow ? "relative to the game's camera (follow)" : "fixed in the world", g_lim.recentre ? "on" : "off",
             g_orbit.yaw(), g_orbit.pitch(), g_lim.follow ? " (offsets)" : "", g_orbit.zoom(),
             g_orbit.engaged() ? "" : " (not engaged: the game's direction)");
    host_log("live: orbit sky: %u backdrop draws moved picture by picture (interpolated; %u new IDs: cuts, engaging, "
             "repeats taken off)\n",
             g_sky.tagged, g_sky.newIds);
    host_log("live: orbit drawing lists: %u made (%s), %u with every cell\n", g_draw.made,
             g_cullAll ? "every cell: --orbit-cull all" : "the game's cells and the orbit view's", g_draw.all);
    const orbit::CollisionStats &s = g_cstats;
    const double min = s.seconds / 60.0;
    host_log("live: orbit collision (%s): %.0f s with the orbit's own view; pull-in turning points %u (%.1f a minute), "
             "rise turning points %u (%.1f a minute); fastest pull-in %.2f, let-out %.2f (share of the distance a "
             "second), rise %.0f, lowering %.0f (degrees a second); %u steps pulled in under 0.6, %u with the eye "
             "inside an obstacle (of %u)\n",
             orbit::collisionName(g_lim.collision), s.seconds, s.ratioTurns, min > 0 ? s.ratioTurns / min : 0.0,
             s.riseTurns, min > 0 ? s.riseTurns / min : 0.0, s.maxIn, s.maxOut, s.maxUp, s.maxDown, s.pulled, s.inside,
             s.steps);
    host_log("live: orbit ground: the eye lifted over the ground at most %.1f units a second (%.2f in one step); "
             "%u steps held at the terrain under the eye; the lift moved the eye up at most %.2f units in a step, "
             "%u snaps over %.0f\n",
             s.maxLiftUp, s.maxLiftStep, g_orbit.floorHits(), s.maxEyeStep, s.eyeJumps, s.jumpStep);
    host_log("live: orbit steering (%s): %u controller reads of a camera-relative character with the stick held "
             "in a turned orbit view, %u turned (at most %.0f degrees)\n",
             g_cfg->camRelativeSteering ? "relative to the orbit's view" : "off: the game's camera", g_steerReads,
             g_steerTurned, g_steerMaxTurn);
    if (g_log)
        host_log("live: orbit checks: %u pictures checked against their drawing list, %u saw cells not drawn (%u "
                 "cells); %u collision queries, %u differing from a test of every triangle; of the game's listed "
                 "cells %u of %u outside the same test from the game's culling view\n",
                 g_checkPictures.load(), g_checkMissPictures.load(), g_checkMissCells.load(), g_heightChecks,
                 g_heightMismatches, g_gameCellsOutside, g_gameCellsChecked);
}

void orbitSetRepeatable(bool on) {
    g_repeatable = on;
}

void orbitSetViewLog(const char *path) {
    g_viewLog = static_cast<FILE *>(host_fopen(path, "w"));
    if (g_viewLog == nullptr) host_fatal("--view-log %s: can't write it", path);
    fprintf(g_viewLog, "render_ms,workload,frame,weight,view_yaw,view_pitch,orbit_yaw,step_pos\n");
}

bool orbitSetCull(const std::string &mode) {
    if (mode == "all") g_cullAll = true;
    else if (mode == "view") g_cullAll = false;
    else return false;
    return true;
}

}  // namespace live
