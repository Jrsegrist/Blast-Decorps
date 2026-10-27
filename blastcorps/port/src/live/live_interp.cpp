/* bc.exe's tagged frame interpolation (see live_interp.h).
 *
 * With a frame rate above the game's own, RT64 draws in-between pictures,
 * blending each transform of the last game frame with the one it matches in
 * the frame before.  Left to itself (G_EX_ID_AUTO) it guesses the pairs from
 * what is drawn (same render state and triangle count, nearest position),
 * which mixes up look-alike objects (debris, smoke puffs, identical vehicles),
 * blends objects that spawned into the slot of another one, and steps
 * whatever it can't pair.  Here the game names its objects instead:
 *
 *  - the game (PORT_HOST builds only) calls PORT_ITAG(gdl, kind, index) where
 *    it starts drawing an object (include/game/port.h); host_itag_hook
 *    records "the commands from this display-list address on belong to
 *    (kind, index)" in a host table.  Nothing is written to RDRAM, and the
 *    game never reads anything back, so it runs exactly as with the option
 *    off.  PORT_ISPAWN starts a new generation of a slot (a new object),
 *    PORT_ICUT_IF a camera snap;
 *  - while RT64 reads the task (Interpreter::processDisplayLists), every GBI
 *    command goes through dispatch() first (the GBI's function table, a
 *    public table of plain function pointers, is wrapped: RT64 itself is not
 *    changed).  A command at a tagged address makes that object the owner of
 *    what follows at that display-list level (and in the lists it calls); a
 *    model matrix the owner loads gets the ID hash(kind, index, generation,
 *    which matrix: its segment-relative address, else its order in the
 *    owner's drawing), set with RSP::matrixId (the call behind RT64's extended
 *    gEXMatrixGroup), so RT64 pairs it with the same ID of the frame before
 *    (G_EX_ORDER_LINEAR) and blends position, rotation and scale.  An ID that
 *    didn't exist in the frame before (a new object, a new generation) is
 *    shown as it is;
 *  - objects drawn as world-space vertices rebuilt every frame (people, the
 *    ground shadows of moving objects) get a transform of their own per
 *    object, with RT64's vertex interpolation (each vertex blended with the
 *    same vertex of the frame before);
 *  - the HUD and 2D (from PORT_ITAG_HUD on) get G_EX_ID_IGNORE: drawn exactly
 *    as the game drew them, never blended (text, the dial, the radar's
 *    blips), except the radar's turning sweep and heading arrow
 *    (PORT_ITAG_RADAR) and the attract mode's bobbing logo (untagged: AUTO);
 *  - untagged drawing (the terrain, the backdrop) keeps RT64's own matching
 *    (G_EX_ID_AUTO), as with video.interpolation = auto;
 *  - a tagged object that moved much further than it moves normally (a
 *    respawn, a teleport) starts a new generation: no blend across the jump;
 *  - two things the game draws untagged get IDs here, from what they are:
 *    the globe's route lines (11530.c fe_globe_draw_route, G_LINE3D of the
 *    L3D microcode: RT64 draws each line as a quad of its own, so the line
 *    draw call's triangle count changes as segments fade in and out, and
 *    RT64's own matching (AUTO) compares draw calls by it: the lines were
 *    left unpaired, and in the in-between pictures they showed the next
 *    frame's position over a globe still half way there, jumping ahead and
 *    back while the globe turned) and
 *    the four bouncing arrows round the next target (30C70.c
 *    hud_draw_target_marker: 2D, placed by the game's projection of the
 *    target; their vertices are blended, as the 3D view they point into is).
 *    With video.interpolation = auto (RT64's own matching, no tags) these
 *    two get the same IDs all the same (interpTagsAutoIds): the lines
 *    jumped ahead of the turning globe there too, and the arrows (2D, never
 *    blended by RT64's matching) stepped at the game's rate over a view
 *    turning smoothly: they trailed and caught up with their target, most
 *    in the orbit camera's fast turns.  Nothing else changes.
 *
 * No GPL code: written against RT64's (MIT) API. */
#include <windows.h>

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <iterator>
#include <unordered_map>
#include <vector>

#include "gbi/rt64_gbi.h"
#include "hle/rt64_application.h"
#include "hle/rt64_interpreter.h"
#include "hle/rt64_rsp.h"
#include "hle/rt64_state.h"
#include "hle/rt64_workload_queue.h"

#include "live_interp.h"

extern "C" {
#include "plat_host.h"
unsigned plat_frames(void);
}

namespace live {

namespace {

/* the tag kinds (include/game/port.h) */
enum {
    K_NONE = 0, K_HUD = 1, K_OBJ, K_SHADOW, K_SHADOWFE, K_BUILDING, K_BPART, K_FALL, K_RISE, K_BILLBOARD,
    K_PARTICLE, K_PEOPLE, K_GSHADOW, K_MARKER, K_RADAR, K_CUT = 30, K_SPAWN = 31
};

/* the L3D microcode's line command (port/rt64 patch 0004) */
constexpr uint8_t kLine3D = 0xB5;
/* the arrows' vertices: DynamicBuf (gDynamicBufs[2], 0x803156F8, 0x21498 bytes each) + 0x1F00 */
constexpr uint32_t kMarkerVtx0 = 0x3156F8u + 0x1F00u, kMarkerVtx1 = kMarkerVtx0 + 0x21498u;

/* world-space vertices rebuilt every frame: vertex interpolation */
bool vertexKind(uint32_t k) {
    return k == K_PEOPLE || k == K_GSHADOW;
}

RT64::Application *g_app;
bool g_on;
bool g_autoIds;          /* interpTagsAutoIds: interpolation = auto, the route lines get their ID all the same */
bool g_log;
GbiCommandHook g_hook;   /* interpSetCommandHook (the orbit camera) */
GbiMatrixHook g_mtxHook;
GbiCommandHook g_wideHook;   /* interpSetWideHooks (widescreen, live_wide.cpp) */
GbiVertexHook g_vtxHook;
GbiAddressHook g_addrHook;
GbiAddressHook g_ownerHook;   /* interpSetOwnerHook (the level viewer, live_viewer.cpp) */

/* the vertices RT64 has in the workload being built */
uint32_t vertexCount(RT64::State *state) {
    const RT64::WorkloadQueue &q = *state->ext.workloadQueue;
    return q.workloads[q.writeCursor].drawData.vertexCount();
}

bool anyHook() {
    return g_hook != nullptr || g_mtxHook != nullptr || g_wideHook != nullptr || g_vtxHook != nullptr ||
           g_addrHook != nullptr || g_ownerHook != nullptr || g_autoIds;
}

/* the tags of the display lists being built (physical address -> kind, index) */
struct Tag {
    uint32_t kind, index;
};
std::unordered_map<uint32_t, Tag> g_tags;
bool g_cut;
unsigned g_spawnsSeen;
/* object generations (PORT_ISPAWN, jumps) */
struct Gen {
    uint32_t gen = 0;
    unsigned jumpFrame = ~0u;
};
std::unordered_map<uint32_t, Gen> g_gen;

uint32_t slotKey(uint32_t kind, uint32_t index) {
    return (kind << 24) ^ index;
}

void itagHook(unsigned addr, int kind, int index) {
    if (kind == K_CUT) {
        g_cut = true;
    } else if (kind == K_SPAWN) {
        g_gen[slotKey(uint32_t(index) >> 16, uint32_t(index) & 0xFFFF)].gen++;
        g_spawnsSeen++;
    } else {
        g_tags[addr & 0xFFFFFF] = {uint32_t(kind), uint32_t(index)};
    }
}

/* ---- while RT64 reads a task ---------------------------------------------- */
struct Owner {
    uint32_t kind = K_NONE, index = 0, ordinal = 0;
};
std::vector<Owner> g_owners;   /* per display-list level (RT64's return-address stack) */
Owner g_owner;
constexpr int kDepth = 32;     /* RSP_MATRIX_STACK_SIZE */
uint32_t g_ids[kDepth];        /* the ID of each modelview matrix-stack level */
uint8_t g_vpos[kDepth];
int g_depth;
uint32_t g_applied;            /* the ID last given to RT64 */
uint8_t g_appliedVpos;
struct Pivot {
    bool on = false;
    float p[3] = {};               /* in the transform's model space */
};
Pivot g_piv[kDepth];           /* each level's pivot (a part turning about an axle: wheelPivot) */
unsigned g_frame;              /* main frames seen */
bool g_mainTask;

/* jump detection: where each tagged transform was in the last frame */
struct Track {
    unsigned frame = 0;
    float pos[3] = {};
    float move = 0;
    float rot[3][3] = {};   /* the rotation (rows normalised) */
    bool haveRot = false;
    /* a part multiplied onto its parent: its matrix relative to the parent last frame, and the
     * pivot found from the last two (wheelPivot) */
    double local[4][3] = {};
    unsigned localFrame = ~0u;
    double cand[3] = {};    /* the last frame's solution */
    bool haveCand = false;
    Pivot pivot;
};
std::unordered_map<uint64_t, Track> g_tracks;
constexpr float kJumpMin = 400.0f;      /* world units in one frame ... */
constexpr float kJumpFactor = 8.0f;     /* ... and this many times its last move */

/* statistics */
unsigned g_tasks, g_tagged, g_jumps;
unsigned g_frameOwners, g_frameMtx, g_frameKinds[32];   /* this frame (--interp-log) */
float g_maxMove;                                          /* largest tagged move since the last log */
unsigned long long g_cmds;
double g_taskSeconds;
LARGE_INTEGER g_t0, g_freq;

/* original functions of each GBI whose table was wrapped */
struct Wrapped {
    RT64::GBI *gbi;
    RT64::GBIFunction orig[UCODE_MAP_SIZE];
};
std::vector<Wrapped> g_wrapped;
const RT64::GBIFunction *g_cur;   /* the current task's originals */

uint32_t mix(uint32_t h, uint32_t v) {
    h ^= v + 0x9E3779B9u + (h << 6) + (h >> 2);
    return h;
}

uint32_t ownerId(const Owner &o, uint32_t ordinal) {
    uint32_t gen = 0;
    auto it = g_gen.find(slotKey(o.kind, o.index));
    if (it != g_gen.end()) gen = it->second.gen;
    uint32_t h = mix(mix(mix(mix(0x42430000u, o.kind), o.index), gen), ordinal);
    if (h == G_EX_ID_IGNORE || h == G_EX_ID_AUTO) h = 0x12345678u;
    return h;
}

unsigned g_test;   /* --tag-test (experiments): 1 no vertex interpolation, 2 HUD auto, 4 no decompose,
                      8 ORDER_AUTO, 16 IDs auto (wrapping and tags only), 32 no pivots (parts turned
                      about their model origin, as before), 64 (with --interp-log) every vehicle part's
                      turn logged, 512 (with --interp-log) vehicle 1's matrices logged */

void apply(RT64::State *state, uint32_t id, uint8_t vpos, const Pivot *pivot = nullptr) {
    if (g_test & 1) vpos = G_EX_COMPONENT_SKIP;
    if ((g_test & 2) && id == G_EX_ID_IGNORE) id = G_EX_ID_AUTO;
    if (g_test & 16) id = G_EX_ID_AUTO;
    if ((g_test & 32) || (pivot != nullptr && !pivot->on)) pivot = nullptr;
    /* (an ID names one matrix: the same ID, the same pivot) */
    if (id == g_applied && vpos == g_appliedVpos) return;
    g_applied = id;
    g_appliedVpos = vpos;
    RT64::RSP &rsp = *state->rsp;
    if (id == G_EX_ID_AUTO) {
        /* RT64's defaults (TransformGroup) */
        rsp.matrixId(id, false, false, true, G_EX_COMPONENT_AUTO, G_EX_COMPONENT_AUTO, G_EX_COMPONENT_AUTO,
                     G_EX_COMPONENT_AUTO, G_EX_COMPONENT_AUTO, G_EX_COMPONENT_SKIP, G_EX_COMPONENT_SKIP,
                     G_EX_COMPONENT_AUTO, G_EX_COMPONENT_AUTO, G_EX_ORDER_AUTO, G_EX_ASPECT_AUTO, G_EX_EDIT_NONE,
                     false, false);
    } else if (id == G_EX_ID_IGNORE) {
        rsp.matrixId(id, false, false, true, G_EX_COMPONENT_SKIP, G_EX_COMPONENT_SKIP, G_EX_COMPONENT_SKIP,
                     G_EX_COMPONENT_SKIP, G_EX_COMPONENT_SKIP, G_EX_COMPONENT_SKIP, G_EX_COMPONENT_SKIP,
                     G_EX_COMPONENT_SKIP, G_EX_COMPONENT_SKIP, G_EX_ORDER_LINEAR, G_EX_ASPECT_AUTO, G_EX_EDIT_NONE,
                     false, false);
    } else {
        rsp.matrixId(id, false, false, !(g_test & 4), G_EX_COMPONENT_INTERPOLATE, G_EX_COMPONENT_INTERPOLATE,
                     G_EX_COMPONENT_INTERPOLATE, G_EX_COMPONENT_INTERPOLATE, G_EX_COMPONENT_INTERPOLATE, vpos,
                     G_EX_COMPONENT_SKIP, G_EX_COMPONENT_AUTO, G_EX_COMPONENT_AUTO,
                     (g_test & 8) ? G_EX_ORDER_AUTO : G_EX_ORDER_LINEAR, G_EX_ASPECT_AUTO, G_EX_EDIT_NONE, false,
                     false);
    }
    /* the pivot (port patch 0007 TransformGroup::pivot) of the group matrixId just set */
    RT64::TransformGroup &g = rsp.extended.modelMatrixIdStack[rsp.extended.modelMatrixIdStackSize - 1];
    g.usePivot = pivot != nullptr;
    for (int i = 0; i < 3; i++) g.pivot[i] = pivot != nullptr ? pivot->p[i] : 0.0f;
}

/* the ID for drawing that has no matrix of its own under owner o */
uint32_t plainId(const Owner &o) {
    if (o.kind == K_HUD) return G_EX_ID_IGNORE;
    if (vertexKind(o.kind)) return ownerId(o, 0xFFFF);
    return G_EX_ID_AUTO;
}

/* a new owner at this level; geometry of its own (vertex kinds, HUD) starts
 * a transform of its own at its first vertex */
void setOwner(RT64::State *state, const Tag &t) {
    const Owner before = g_owner;
    g_owner.kind = t.kind;
    g_owner.index = t.index;
    g_owner.ordinal = 0;
    g_tagged++;
    g_frameOwners++;
    g_frameKinds[t.kind & 31]++;
    const bool own = vertexKind(t.kind) || t.kind == K_HUD;
    const bool had = vertexKind(before.kind) || before.kind == K_HUD;
    if (own || had) {
        g_ids[g_depth] = own ? plainId(g_owner) : G_EX_ID_AUTO;
        g_vpos[g_depth] = vertexKind(t.kind) ? G_EX_COMPONENT_INTERPOLATE : G_EX_COMPONENT_SKIP;
        g_piv[g_depth] = Pivot();
        state->rsp->modelViewProjChanged = true;   /* a new world transform at the next vertex */
        apply(state, g_ids[g_depth], g_vpos[g_depth]);
    }
}

/* turn statistics: tagged model matrices by how far they turned in a frame
 * (30-degree buckets), and the largest turn per kind */
unsigned g_turnHist[6];
float g_turnMaxKind[32];
unsigned g_turnLogged;

/* after a tagged model matrix: did the object jump? (then a new generation)
 * *turnDeg: how far its rotation turned since the frame before (-1: unknown) */
bool jumped(RT64::State *state, uint32_t ordinal, float *turnDeg, Track **track) {
    const RT64::RSP &rsp = *state->rsp;
    const hlslpp::float4x4 &m = rsp.modelMatrixStack[rsp.modelMatrixStackSize - 1];
    const float p[3] = {float(m[3][0]), float(m[3][1]), float(m[3][2])};
    const uint64_t key = (uint64_t(slotKey(g_owner.kind, g_owner.index)) << 32) ^ ordinal;
    Track &t = g_tracks[key];
    *track = &t;
    /* the rotation: the 3x3 part, rows normalised (the scale left out) */
    float r[3][3];
    bool rotOk = true;
    for (int i = 0; i < 3; i++) {
        const float a = float(m[i][0]), b = float(m[i][1]), c = float(m[i][2]);
        const float len = std::sqrt(a * a + b * b + c * c);
        if (!(len > 1e-6f)) rotOk = false;
        r[i][0] = rotOk ? a / len : 0;
        r[i][1] = rotOk ? b / len : 0;
        r[i][2] = rotOk ? c / len : 0;
    }
    *turnDeg = -1;
    /* (--tag-test 512 with --interp-log: vehicle 1's matrices in detail, up to 600 lines) */
    if ((g_test & 512) && g_log && g_owner.kind == K_OBJ && (g_owner.index & 0xFF) == 1 && g_turnLogged < 600 &&
        plat_frames() >= 1750) {
        g_turnLogged++;
        host_log("live: mtx frame %u owner %u key %08X len %.3f %.3f %.3f pos %.1f %.1f %.1f depth %u\n", g_frame,
                 g_owner.index, ordinal, std::sqrt(float(m[0][0] * m[0][0] + m[0][1] * m[0][1] + m[0][2] * m[0][2])),
                 std::sqrt(float(m[1][0] * m[1][0] + m[1][1] * m[1][1] + m[1][2] * m[1][2])),
                 std::sqrt(float(m[2][0] * m[2][0] + m[2][1] * m[2][1] + m[2][2] * m[2][2])), p[0], p[1], p[2],
                 unsigned(rsp.modelMatrixStackSize));
    }
    if (rotOk && t.haveRot && t.frame + 1 == g_frame) {
        /* the angle of prev^T cur: trace = sum of the elementwise products */
        double tr = 0;
        for (int i = 0; i < 3; i++)
            for (int j = 0; j < 3; j++) tr += double(t.rot[i][j]) * r[i][j];
        *turnDeg = float(std::acos(std::max(-1.0, std::min(1.0, (tr - 1.0) / 2.0))) * 57.29577951308232);
        g_turnHist[std::min(5, int(*turnDeg / 30.0f))]++;
        g_turnMaxKind[g_owner.kind & 31] = std::max(g_turnMaxKind[g_owner.kind & 31], *turnDeg);
        /* (--tag-test 64: every vehicle part's turn, up to 6000 lines) */
        if (g_log && ((g_test & 64) ? (g_owner.kind == K_OBJ && *turnDeg > 0.5f && g_turnLogged < 6000)
                                    : (*turnDeg > 60.0f && g_turnLogged < 400))) {
            g_turnLogged++;
            host_log("live: interp tag %u/%u (matrix %08X) turned %.0f deg in frame %u\n", g_owner.kind, g_owner.index,
                     ordinal, *turnDeg, g_frame);
        }
    }
    if (t.frame != g_frame) {
        memcpy(t.rot, r, sizeof r);
        t.haveRot = rotOk;
    }
    bool jump = false;
    if (t.frame + 1 == g_frame) {
        const float dx = p[0] - t.pos[0], dy = p[1] - t.pos[1], dz = p[2] - t.pos[2];
        const float d = std::sqrt(dx * dx + dy * dy + dz * dz);
        if (d > g_maxMove) g_maxMove = d;
        if (d > kJumpMin && d > kJumpFactor * t.move) {
            jump = true;
            g_jumps++;
            if (g_log)
                host_log("live: interp tag %u/%u (matrix %u) jumped %.0f units (last move %.0f): new generation\n",
                         g_owner.kind, g_owner.index, ordinal, d, t.move);
        }
        t.move = d;
    } else {
        t.move = 0;
    }
    if (t.frame != g_frame) {
        t.frame = g_frame;
        memcpy(t.pos, p, sizeof p);
    }
    return jump;
}

/* (Spinning parts: RT64 turns a matrix along the shortest arc between the two
 * frames', so a part turning over half a turn in a game frame would turn
 * backwards in between; the turn statistics, --interp-log summary, show the
 * vehicles' parts turn under 90 degrees a frame but for a few frames of
 * 125-176 in the 60 levels: none over half a turn.) */
unsigned g_pivotsFound, g_pivotsUsed, g_pivotLogged;

bool inv3(const double a[3][3], double r[3][3]) {
    const double c00 = a[1][1] * a[2][2] - a[1][2] * a[2][1], c01 = a[1][2] * a[2][0] - a[1][0] * a[2][2],
                 c02 = a[1][0] * a[2][1] - a[1][1] * a[2][0];
    const double det = a[0][0] * c00 + a[0][1] * c01 + a[0][2] * c02;
    if (!(std::fabs(det) > 1e-30)) return false;
    const double k = 1.0 / det;
    r[0][0] = c00 * k;
    r[1][0] = c01 * k;
    r[2][0] = c02 * k;
    r[0][1] = (a[0][2] * a[2][1] - a[0][1] * a[2][2]) * k;
    r[1][1] = (a[0][0] * a[2][2] - a[0][2] * a[2][0]) * k;
    r[2][1] = (a[0][1] * a[2][0] - a[0][0] * a[2][1]) * k;
    r[0][2] = (a[0][1] * a[1][2] - a[0][2] * a[1][1]) * k;
    r[1][2] = (a[0][2] * a[1][0] - a[0][0] * a[1][2]) * k;
    r[2][2] = (a[0][0] * a[1][1] - a[0][1] * a[1][0]) * k;
    return true;
}

/* A part multiplied onto its parent P (G_MTX_MUL, pushed or not: a wheel on
 * a vehicle's body) whose model origin is away from the axle it turns about:
 * the origin swings round the axle, and RT64 moves the origin along a
 * straight line between two frames while it turns the part, so in the
 * in-between pictures the part left its axle (the Backlash's and other
 * vehicles' wheels wobbled off their hubs, more the faster they spun).  The
 * axle is where the part's matrix relative to its parent, L, keeps a point
 * still between the two frames: c Lprev = c Lcur (row vectors), the least-
 * norm solution of c (Lprev - Lcur) = tcur - tprev; when that holds (a turn
 * about a fixed axle) c is the part's pivot (port patch 0007: RT64 then
 * moves c's world position along the line and turns the part about it).  A
 * pivot found is kept (the axle is fixed in the model) while the part
 * doesn't turn.  --tag-test 32: no pivots. */
Pivot wheelPivot(const RT64::RSP &rsp, const hlslpp::float4x4 &P, Track &t) {
    const hlslpp::float4x4 &M = rsp.modelMatrixStack[rsp.modelMatrixStackSize - 1];
    double p3[3][3], ip[3][3], L[4][3];
    for (int i = 0; i < 3; i++)
        for (int j = 0; j < 3; j++) p3[i][j] = double(float(P[i][j]));
    if (!inv3(p3, ip)) return t.pivot;
    /* L = M inv(P): L3 = M3 ip, tL = (tM - tP) ip */
    for (int i = 0; i < 4; i++)
        for (int j = 0; j < 3; j++) {
            double s = 0;
            for (int k = 0; k < 3; k++) {
                const double m = i < 3 ? double(float(M[i][k])) : double(float(M[3][k])) - double(float(P[3][k]));
                s += m * ip[k][j];
            }
            L[i][j] = s;
        }
    if (t.localFrame + 1 == g_frame) {
        double A[3][3], b[3], AAt[3][3], na = 0, nl = 0, tr = 0;
        for (int i = 0; i < 3; i++) {
            b[i] = L[3][i] - t.local[3][i];
            for (int j = 0; j < 3; j++) {
                A[i][j] = t.local[i][j] - L[i][j];
                na += A[i][j] * A[i][j];
                nl += L[i][j] * L[i][j];
            }
        }
        /* turned by about a degree or more relative to the parent */
        if (nl > 0 && na / nl > 3e-4) {
            for (int i = 0; i < 3; i++)
                for (int j = 0; j < 3; j++) {
                    AAt[i][j] = 0;
                    for (int k = 0; k < 3; k++) AAt[i][j] += A[i][k] * A[j][k];
                }
            tr = AAt[0][0] + AAt[1][1] + AAt[2][2];
            /* the axle's direction u: the axis of the turn Lprev Lcur^-1 (its
             * antisymmetric part); c's component along it is free (any point of
             * the axle will do): held at 0 by a penalty along u */
            double lc[3][3], ilc[3][3], R[3][3], u[3] = {0, 0, 0};
            for (int i = 0; i < 3; i++)
                for (int j = 0; j < 3; j++) lc[i][j] = L[i][j];
            if (inv3(lc, ilc)) {
                for (int i = 0; i < 3; i++)
                    for (int j = 0; j < 3; j++)
                        R[i][j] = t.local[i][0] * ilc[0][j] + t.local[i][1] * ilc[1][j] + t.local[i][2] * ilc[2][j];
                u[0] = R[1][2] - R[2][1];
                u[1] = R[2][0] - R[0][2];
                u[2] = R[0][1] - R[1][0];
                const double nu = std::sqrt(u[0] * u[0] + u[1] * u[1] + u[2] * u[2]);
                for (int i = 0; i < 3; i++) u[i] = nu > 1e-12 ? u[i] / nu : 0.0;
            }
            for (int i = 0; i < 3; i++) {
                AAt[i][i] += 1e-6 * tr;
                for (int j = 0; j < 3; j++) AAt[i][j] += tr * u[i] * u[j];
            }
            double inv[3][3], bAt[3], c[3];
            if (inv3(AAt, inv)) {
                /* c = b A^T (A A^T + e I)^-1 */
                for (int j = 0; j < 3; j++) bAt[j] = b[0] * A[j][0] + b[1] * A[j][1] + b[2] * A[j][2];
                for (int j = 0; j < 3; j++) c[j] = bAt[0] * inv[0][j] + bAt[1] * inv[1][j] + bAt[2] * inv[2][j];
                double res = 0, nb = 0, nc = 0;
                for (int j = 0; j < 3; j++) {
                    const double r = c[0] * A[0][j] + c[1] * A[1][j] + c[2] * A[2][j] - b[j];
                    res += r * r;
                    nb += b[j] * b[j];
                    nc += c[j] * c[j];
                }
                /* a turn about a fixed axle: c solves it exactly, and the axle (c the
                 * point of it nearest the origin) is where it was found the frame
                 * before (a matrix that also moves or scales gives a different point
                 * every frame) */
                double dc = 0;
                for (int j = 0; j < 3; j++) dc += (c[j] - t.cand[j]) * (c[j] - t.cand[j]);
                const bool ok = res <= 1e-4 * nb + 1e-6 && nc < 1e10 && t.haveCand &&
                                dc <= 1e-3 * (nc + 1.0);
                for (int j = 0; j < 3; j++) t.cand[j] = c[j];
                t.haveCand = true;
                if (g_log && (((g_test & 64) && g_pivotLogged < 3000) ||
                              ((g_test & 512) && (g_owner.index & 0xFF) == 1 && plat_frames() >= 1750 &&
                               g_pivotLogged < 3000))) {
                    g_pivotLogged++;
                    host_log("live: interp pivot %u/%u frame %u: %.2f %.2f %.2f, residual %.4f of %.4f%s\n",
                             g_owner.kind, g_owner.index, g_frame, c[0], c[1], c[2], std::sqrt(res), std::sqrt(nb),
                             ok ? "" : " (not a turn about a fixed axle)");
                }
                if (ok) {
                    if (!t.pivot.on) g_pivotsFound++;
                    t.pivot.on = true;
                    for (int j = 0; j < 3; j++) t.pivot.p[j] = float(c[j]);
                }
            }
        }
    }
    memcpy(t.local, L, sizeof L);
    t.localFrame = g_frame;
    if (t.pivot.on) g_pivotsUsed++;
    return t.pivot;
}

/* a command other than G_MTX / G_POPMTX: the hooks' turn (the orbit camera's,
 * then widescreen's), else RT64's own; after a G_VTX, the vertex hook */
void runHooks(RT64::State *state, RT64::DisplayList **dl, uint8_t op) {
    if (g_hook != nullptr && g_hook(state, dl, g_cur)) return;
    if (g_wideHook != nullptr && g_wideHook(state, dl, g_cur)) return;
    g_cur[op](state, dl);
}
void run(RT64::State *state, RT64::DisplayList **dl, uint8_t op) {
    if (op != 0x04 || g_vtxHook == nullptr) {
        runHooks(state, dl, op);
        return;
    }
    const uint32_t w1 = (*dl)->w1;
    const uint32_t first = vertexCount(state);
    runHooks(state, dl, op);
    g_vtxHook(state, w1, first);
}

/* ---- the globe's route lines ---------------------------------------------------
 * One ID for the lines of a task (they share the globe's matrices), a new
 * generation when the globe's matrix jumped since the last task with lines (a
 * cut: another planet, the view thrown round). */
bool g_curL3D;                 /* the task's microcode is L3D (the front end's globe) */
unsigned g_taskSerial;         /* tasks read with the GBI wrapped (tags or not) */
unsigned g_routeTask = ~0u;    /* the task the route ID was worked out for (g_taskSerial) */
uint32_t g_routeId, g_routeGen;
bool g_routeHave;
float g_routeRot[3][3], g_routePos[3];
unsigned g_routeLines, g_routeTasks, g_routeCuts;

void routeLine(RT64::State *state) {
    RT64::RSP &rsp = *state->rsp;
    if (g_routeTask != g_taskSerial) {
        g_routeTask = g_taskSerial;
        g_routeTasks++;
        const hlslpp::float4x4 &m = rsp.modelMatrixStack[rsp.modelMatrixStackSize - 1];
        float r[3][3], p[3];
        bool ok = true;
        for (int i = 0; i < 3; i++) {
            const float a = float(m[i][0]), b = float(m[i][1]), c = float(m[i][2]);
            const float len = std::sqrt(a * a + b * b + c * c);
            if (!(len > 1e-6f)) ok = false;
            r[i][0] = ok ? a / len : 0;
            r[i][1] = ok ? b / len : 0;
            r[i][2] = ok ? c / len : 0;
            p[i] = float(m[3][i]);
        }
        bool cut = !ok || !g_routeHave;
        if (ok && g_routeHave) {
            double tr = 0, d = 0, len = 0;
            for (int i = 0; i < 3; i++) {
                for (int j = 0; j < 3; j++) tr += double(g_routeRot[i][j]) * r[i][j];
                d += double(p[i] - g_routePos[i]) * double(p[i] - g_routePos[i]);
                len += double(p[i]) * double(p[i]);
            }
            const double turn = std::acos(std::max(-1.0, std::min(1.0, (tr - 1.0) / 2.0))) * 57.29577951308232;
            if (turn > 25.0 || std::sqrt(d) > 0.25 * std::sqrt(len) + 50.0) {
                cut = true;
                if (g_log)
                    host_log("live: interp globe routes: matrix turned %.0f deg / moved %.0f (task %u): new generation\n",
                             turn, std::sqrt(d), g_taskSerial);
            }
        }
        if (cut) {
            g_routeGen++;
            g_routeCuts++;
        }
        g_routeHave = ok;
        memcpy(g_routeRot, r, sizeof r);
        memcpy(g_routePos, p, sizeof p);
        g_routeId = mix(mix(0x524F5554u, g_routeGen), 0x4C494E45u);
        if (g_routeId == G_EX_ID_IGNORE || g_routeId == G_EX_ID_AUTO) g_routeId = 0x524F5554u;
    }
    g_routeLines++;
    if (g_applied != g_routeId) {
        rsp.modelViewProjChanged = true;   /* (the line's quad: a transform of its own) */
        apply(state, g_routeId, G_EX_COMPONENT_SKIP);
    }
}

/* ---- the target marker's arrows -------------------------------------------------
 * An ID of their own with the vertices blended; a new generation when the
 * marker wasn't drawn in the frame before or an arrow moved far (another
 * target). */
uint32_t g_markGen;
unsigned g_markFrame = ~0u - 1;
float g_markCentre[4][2];
unsigned g_markers, g_markGens;

uint32_t markerId(uint32_t phys) {
    const uint8_t *v = reinterpret_cast<const uint8_t *>(uintptr_t(0x80000000u | phys));
    float c[4][2];
    bool away = false;
    for (int k = 0; k < 4; k++) {
        c[k][0] = c[k][1] = 0;
        for (int j = 0; j < 4; j++) {
            c[k][0] += float(*reinterpret_cast<const int16_t *>(v + (k * 4 + j) * 16)) / 4.0f;
            c[k][1] += float(*reinterpret_cast<const int16_t *>(v + (k * 4 + j) * 16 + 2)) / 4.0f;
        }
        if (std::fabs(c[k][0] - g_markCentre[k][0]) + std::fabs(c[k][1] - g_markCentre[k][1]) > 40.0f) away = true;
    }
    if (g_markFrame + 1 != g_frame || away) {
        g_markGen++;
        g_markGens++;
    }
    g_markFrame = g_frame;
    memcpy(g_markCentre, c, sizeof c);
    g_markers++;
    uint32_t id = mix(mix(0x4D41524Bu, g_markGen), 0x41525257u);
    if (id == G_EX_ID_IGNORE || id == G_EX_ID_AUTO) id = 0x4D41524Bu;
    return id;
}

void dispatch(RT64::State *state, RT64::DisplayList **dl) {
    const uint32_t w0 = (*dl)->w0;
    const uint8_t op = uint8_t(w0 >> 24);
    if (g_ownerHook != nullptr)
        g_ownerHook(state, uint32_t(reinterpret_cast<const uint8_t *>(*dl) - state->RDRAM) & 0xFFFFFF);
    if (g_addrHook != nullptr)
        g_addrHook(state, uint32_t(reinterpret_cast<const uint8_t *>(*dl) - state->RDRAM) & 0xFFFFFF);
    if (!g_on) {   /* only the command hooks want the commands (and, interpolation = auto, interpTagsAutoIds) */
        if (op == 0x01) {
            g_cur[op](state, dl);
            if (g_mtxHook != nullptr) g_mtxHook(state, uint8_t(w0 >> 16));
            /* the drawing after the lines: RT64's own matching again (as with tags: untagged = AUTO) */
            if (g_autoIds && !(uint8_t(w0 >> 16) & state->rsp->projMask)) apply(state, G_EX_ID_AUTO, G_EX_COMPONENT_SKIP);
        } else if (op == 0xBD) {
            g_cur[op](state, dl);
            if (g_autoIds) apply(state, G_EX_ID_AUTO, G_EX_COMPONENT_SKIP);
        } else if (g_autoIds && op == 0x04 && !g_curL3D &&
                   interpMarkerVertices(state->rsp->fromSegmented((*dl)->w1) & 0x7FFFFF, w0)) {
            /* the target marker's arrows: as with tags (their own ID, vertices blended), then RT64's matching again */
            const uint32_t id = markerId(state->rsp->fromSegmented((*dl)->w1) & 0x7FFFFF);
            state->rsp->modelViewProjChanged = true;
            apply(state, id, G_EX_COMPONENT_INTERPOLATE);
            run(state, dl, op);
            state->rsp->modelViewProjChanged = true;
            apply(state, G_EX_ID_AUTO, G_EX_COMPONENT_SKIP);
        } else {
            if (g_autoIds && op == kLine3D && g_curL3D) routeLine(state);
            run(state, dl, op);
        }
        return;
    }
    g_cmds++;
    /* follow RT64's display-list levels: a called list inherits the owner */
    const size_t level = state->returnAddressStack.size();
    while (g_owners.size() > level) {
        g_owner = g_owners.back();
        g_owners.pop_back();
    }
    while (g_owners.size() < level) g_owners.push_back(g_owner);
    if (!g_tags.empty()) {
        const uint32_t addr = uint32_t(reinterpret_cast<const uint8_t *>(*dl) - state->RDRAM) & 0xFFFFFF;
        auto it = g_tags.find(addr);
        if (it != g_tags.end()) setOwner(state, it->second);
    }
    RT64::RSP &rsp = *state->rsp;
    if (op == 0x01) {   /* G_MTX */
        const uint8_t params = uint8_t(w0 >> 16);
        const uint32_t w1 = (*dl)->w1;
        const bool proj = (params & rsp.projMask) != 0;
        /* (the matrix a G_MTX_MUL multiplies onto: the part's parent, wheelPivot) */
        const bool mul = !proj && !(params & rsp.loadMask) && rsp.modelMatrixStackSize >= 1;
        hlslpp::float4x4 parent;
        if (mul && g_on && g_owner.kind == K_OBJ) parent = rsp.modelMatrixStack[rsp.modelMatrixStackSize - 1];
        g_cur[op](state, dl);
        if (g_mtxHook != nullptr) g_mtxHook(state, params);
        if (!proj) {
            if ((params & rsp.pushMask) && g_depth < kDepth - 1) g_depth++;
            uint32_t id;
            Pivot pivot;
            if (g_owner.kind == K_NONE) {
                id = G_EX_ID_AUTO;
            } else if (g_owner.kind == K_HUD) {
                id = G_EX_ID_IGNORE;
            } else {
                /* which of the owner's matrices: a segment-relative address
                 * names it (the same part of a model every frame, even when
                 * other parts are left out); a physical one (double-buffered
                 * pools) by its order in the owner's drawing */
                const uint32_t ord = g_owner.ordinal++;
                const uint32_t k = ((w1 >> 24) & 0xF) != 0 ? (0x80000000u | (w1 & 0x0FFFFFFF)) : ord;
                float turn;
                Track *t;
                if (jumped(state, k, &turn, &t)) {
                    Gen &g = g_gen[slotKey(g_owner.kind, g_owner.index)];
                    if (g.jumpFrame != g_frame) {   /* once a frame, however many of its matrices jumped */
                        g.gen++;
                        g.jumpFrame = g_frame;
                    }
                }
                if (g_owner.kind == K_OBJ && mul) pivot = wheelPivot(rsp, parent, *t);
                id = ownerId(g_owner, k);
                g_frameMtx++;
            }
            g_ids[g_depth] = id;
            g_vpos[g_depth] = G_EX_COMPONENT_SKIP;
            g_piv[g_depth] = pivot;
            apply(state, id, G_EX_COMPONENT_SKIP, &pivot);
        }
        return;
    }
    if (op == 0xBD) {   /* G_POPMTX */
        g_cur[op](state, dl);
        if ((*dl)->w1 == 0 && g_depth > 0) {
            g_depth--;
            apply(state, g_ids[g_depth], g_vpos[g_depth], &g_piv[g_depth]);
        }
        return;
    }
    if (op == 0x04 && g_owner.kind == K_HUD &&
        interpMarkerVertices(rsp.fromSegmented((*dl)->w1) & 0x7FFFFF, w0)) {
        /* the target marker's arrows: their own ID, vertices blended (just for this G_VTX) */
        const uint32_t id = markerId(rsp.fromSegmented((*dl)->w1) & 0x7FFFFF);
        rsp.modelViewProjChanged = true;
        apply(state, id, G_EX_COMPONENT_INTERPOLATE);
        run(state, dl, op);
        rsp.modelViewProjChanged = true;
        apply(state, g_ids[g_depth], g_vpos[g_depth], &g_piv[g_depth]);
        return;
    }
    if (op == kLine3D && g_curL3D) routeLine(state);
    run(state, dl, op);
}

void resetTask() {
    g_owners.clear();
    g_owner = Owner();
    g_depth = 0;
    g_ids[0] = G_EX_ID_AUTO;
    g_vpos[0] = G_EX_COMPONENT_SKIP;
    g_piv[0] = Pivot();
    g_applied = G_EX_ID_AUTO;   /* RSP::reset (each task) put RT64's ID stack back to AUTO */
    g_appliedVpos = G_EX_COMPONENT_SKIP;
}

void wrap(RT64::GBI *gbi) {
    g_curL3D = gbi->ucode == RT64::GBIUCode::L3D;
    for (const Wrapped &w : g_wrapped)
        if (w.gbi == gbi) {
            g_cur = w.orig;
            return;
        }
    g_wrapped.push_back({});
    Wrapped &w = g_wrapped.back();
    w.gbi = gbi;
    for (int i = 0; i < UCODE_MAP_SIZE; i++) {
        w.orig[i] = gbi->map[i];
        if (gbi->map[i] != nullptr) gbi->map[i] = dispatch;
    }
    g_cur = w.orig;
    host_log("live: tagged interpolation: GBI %u wrapped\n", unsigned(gbi->ucode));
}

void unwrapAll() {
    for (Wrapped &w : g_wrapped)
        for (int i = 0; i < UCODE_MAP_SIZE; i++) w.gbi->map[i] = w.orig[i];
    g_wrapped.clear();
    g_cur = nullptr;
}

}  // namespace

void interpTagsSetup(RT64::Application *app) {
    g_app = app;
    QueryPerformanceFrequency(&g_freq);
}

void interpTagsEnable(bool on) {
    if (on == g_on) return;
    g_on = on;
    g_tags.clear();
    g_cut = false;
    if (on) {
        host_itag_hook = itagHook;
    } else {
        host_itag_hook = nullptr;
        if (!anyHook()) unwrapAll();
    }
    host_log("live: tagged interpolation %s\n", on ? "on" : "off");
}

void interpTagsAutoIds(bool on) {
    if (on == g_autoIds) return;
    g_autoIds = on;
    if (!anyHook() && !g_on) unwrapAll();
    host_log("live: interpolation auto: the globe's route lines and the target marker's arrows %s\n",
             on ? "with IDs of their own" : "as RT64 matches them");
}

void interpSetCommandHook(GbiCommandHook hook, GbiMatrixHook mtxHook) {
    g_hook = hook;
    g_mtxHook = mtxHook;
    if (!anyHook() && !g_on) unwrapAll();
}

void interpSetOwnerHook(GbiAddressHook hook) {
    g_ownerHook = hook;
    if (!anyHook() && !g_on) unwrapAll();
}

void interpSetWideHooks(GbiCommandHook hook, GbiVertexHook vtxHook, GbiAddressHook addrHook) {
    g_wideHook = hook;
    g_vtxHook = vtxHook;
    g_addrHook = addrHook;
    if (!anyHook() && !g_on) unwrapAll();
}

bool interpTagsOn() {
    return g_on;
}

bool interpMarkerVertices(uint32_t phys, uint32_t w0) {
    /* F3D G_VTX: w0 low 16 bits the length in bytes (16 vertices: 256) */
    return (phys == kMarkerVtx0 || phys == kMarkerVtx1) && (w0 & 0xFFFFu) == 16u * 16u;
}

void interpTagsSetLog(bool on) {
    g_log = on;
}

void interpTagsSetTest(unsigned mask) {
    g_test = mask;
}

void interpTagsBeforeTask(unsigned dl, bool mainFrame) {
    (void) dl;
    if ((!g_on && !anyHook()) || g_app->interpreter->hleGBI == nullptr) return;
    wrap(g_app->interpreter->hleGBI);
    resetTask();
    g_taskSerial++;
    g_mainTask = mainFrame;
    if (mainFrame) g_frame++;   /* (also for interpTagsAutoIds: markerId) */
    if (!g_on) return;
    g_tasks++;
    QueryPerformanceCounter(&g_t0);
}

void interpTagsAfterTask(bool mainFrame) {
    if (!g_on) return;
    LARGE_INTEGER t1;
    QueryPerformanceCounter(&t1);
    g_taskSeconds += double(t1.QuadPart - g_t0.QuadPart) / double(g_freq.QuadPart);
    if (mainFrame) {
        if (g_log && g_frame % 50 == 0) {
            char kinds[256];
            int n = 0;
            for (int k = 0; k < 32 && n < 200; k++)
                if (g_frameKinds[k] != 0) n += snprintf(kinds + n, sizeof kinds - n, " %d:%u", k, g_frameKinds[k]);
            kinds[n] = 0;
            host_log("live: interp tags, frame %u: %u tags recorded, %u owners (kind:count%s), %u tagged matrices; "
                     "largest move %.1f\n",
                     g_frame, unsigned(g_tags.size()), g_frameOwners, kinds, g_frameMtx, g_maxMove);
            g_maxMove = 0;
        }
        g_frameOwners = g_frameMtx = 0;
        memset(g_frameKinds, 0, sizeof g_frameKinds);
        g_tags.clear();
        /* forget objects not seen for a while */
        if (g_tracks.size() > 4096)
            for (auto it = g_tracks.begin(); it != g_tracks.end();)
                it = g_frame - it->second.frame > 8 ? g_tracks.erase(it) : std::next(it);
    }
}

bool interpTagsTakeCut() {
    const bool c = g_cut;
    g_cut = false;
    return c;
}

void interpTagsSummary() {
    if (g_routeLines + g_markers != 0 && g_tasks == 0)
        host_log("live: interpolation auto: globe route lines %u in %u tasks (%u generations), target marker arrows in "
                 "%u frames (%u generations) given IDs of their own\n",
                 g_routeLines, g_routeTasks, g_routeCuts, g_markers, g_markGens);
    if (g_tasks == 0) return;
    host_log("live: tagged interpolation: %u tasks (%u frames), %llu commands, %u owner changes, %u spawns, "
             "%u jumps; %.3f ms per task reading the display lists (tags included)\n",
             g_tasks, g_frame, g_cmds, g_tagged, g_spawnsSeen, g_jumps, g_taskSeconds * 1e3 / g_tasks);
    char kinds[256];
    int n = 0;
    for (int k = 0; k < 32 && n < 200; k++)
        if (g_turnMaxKind[k] > 0) n += snprintf(kinds + n, sizeof kinds - n, " %d:%.0f", k, g_turnMaxKind[k]);
    kinds[n] = 0;
    host_log("live: tagged interpolation: matrices by their turn in a frame (0-30-60-90-120-150-180 deg): %u %u %u %u "
             "%u %u; largest per kind (kind:deg)%s; %u parts turning about an axle found, %u of their matrices "
             "blended about it%s\n",
             g_turnHist[0], g_turnHist[1], g_turnHist[2], g_turnHist[3], g_turnHist[4], g_turnHist[5], kinds,
             g_pivotsFound, g_pivotsUsed, (g_test & 32) ? " (off: --tag-test 32)" : "");
    if (g_routeLines + g_markers != 0)
        host_log("live: tagged interpolation: globe route lines %u in %u tasks (%u generations); target marker "
                 "arrows drawn in %u frames (%u generations)\n",
                 g_routeLines, g_routeTasks, g_routeCuts, g_markers, g_markGens);
}

}  // namespace live
