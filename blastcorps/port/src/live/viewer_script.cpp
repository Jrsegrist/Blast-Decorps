/* The level viewer's test script, --viewer-test "OP;OP;..." (viewer_internal.h; the ops: runTestOp,
 * runEditOp, runTerrainOp, vpadOp): camera moves, records, screenshots, the editor's and the terrain tools'
 * actions through the same paths the mouse takes, and a virtual game controller. */
#include <windows.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "imgui/imgui.h"
#include "imgui/imgui_internal.h"   /* (the test log's view of the panels' navigation: padstate) */

#include "live_menu.h"
#include "viewer_internal.h"

#include "load/tex_codec.h"

extern "C" {
#include "plat_host.h"
}

namespace live {
namespace vw {

/* ---- the test script (--viewer-test) ------------------------------------------------- */
std::vector<std::string> g_test;
size_t g_testPos;
int g_testWait;

bool takeFlag(const std::string &v) {
    return v != "0" && v != "off" && v != "false";
}

void doShot(const std::string &name, bool overlays, bool panels) {
    /* a picture with the panels (and, clean, the overlays) left out: built and presented, then read */
    g_cleanShot = !overlays && !panels;
    g_overlayShot = overlays && !panels;
    {
        /* (the panels leave the picture for it: the controller's panel gets it back afterwards) */
        const ImGuiContext *gc = ImGui::GetCurrentContext();
        if (g_padNav && gc && gc->NavWindow && gc->NavWindow->Name[0] != '#') {
            g_padNavWin = gc->NavWindow->RootWindow->Name;
            g_padNavFocus = true;
        }
    }
    for (int i = 0; i < 2; i++) {
        uiFrame(buildUi, false);
        replay();
    }
    if (g_cleanShot) uiHide();
    replay();
    g_env.saveWindow(shotPath(name));
    g_cleanShot = g_overlayShot = false;
    g_shots++;
}

/* the editor's test ops (--viewer-test):
 *   eset:KIND:I:FIELD=V,FIELD=V   a change of fields          edup:KIND:I    duplicate (Ctrl+D)
 *   estep:KIND:I:FIELD:D[:N]      the Inspector's - / + pressed N times (D each; one undo step per burst)
 *   ewarn                         the Changes panel's warning about the carrier's route (logged)
 *   edel:KIND:I                   delete                      eundo / eredo
 *   edrag:KIND:I:X,Z              drag the gizmo's square to x, z (through the picking, as the mouse would)
 *   erot:KIND:I:DEG               drag the gizmo's ring to heading DEG
 *   eaxis:KIND:I:A:D              drag arrow A (x, y or z) by D map units
 *   egrid:N / esnap:0|1           the grid, the ground snap
 *   eapply / eplay                Apply / Play from here       esave:NAME[:enable]   save the mod */
void runEditOp(const std::string &name, const std::string &arg) {
    std::vector<std::string> p;
    for (size_t a = 0;;) {
        const size_t b = arg.find(':', a);
        p.push_back(arg.substr(a, b == std::string::npos ? std::string::npos : b - a));
        if (b == std::string::npos) break;
        a = b + 1;
    }
    int kind = -1, index = p.size() > 1 ? atoi(p[1].c_str()) : 0;
    for (int k = 0; k < viewer::REC_KINDS; k++)
        if (!p.empty() && p[0] == viewer::recKindKey(k)) kind = k;
    auto selectIt = [&]() {
        reselect(kind, index);
        if (g_sel < 0) host_log("live: viewer test: no record %s\n", arg.c_str());
        uiFrame(buildUi, false);   /* (the overlays' projection of this view, for the picking) */
        return g_sel >= 0;
    };
    if (name == "eset" && p.size() >= 3 && kind >= 0) {
        std::vector<edit::FieldValue> f;
        size_t a = 0;
        const std::string &s = p[2];
        while (a < s.size()) {
            size_t b = s.find(',', a);
            if (b == std::string::npos) b = s.size();
            const std::string kv = s.substr(a, b - a);
            const size_t eq = kv.find('=');
            const int fi = lm_field_index(kind, kv.substr(0, eq).c_str());
            if (fi >= 0 && eq != std::string::npos) f.push_back({fi, atol(kv.c_str() + eq + 1)});
            a = b + 1;
        }
        pushEdit(edit::makeSet(kind, index, f), kind, index);
    } else if (name == "edup" && kind >= 0) {
        if (selectIt()) duplicateSel();
    } else if (name == "edel" && kind >= 0) {
        if (selectIt()) deleteSel();
    } else if (name == "estep" && p.size() >= 4 && kind >= 0) {
        /* estep:KIND:I:FIELD:D[:N]   the field's - / + pressed N times (D each), as the Inspector's buttons do */
        const int fi = lm_field_index(kind, p[2].c_str());
        const long d = atol(p[3].c_str());
        const int n = p.size() > 4 ? std::max(1, atoi(p[4].c_str())) : 1;
        for (int i = 0; i < n && fi >= 0; i++) pushEditBurst(kind, index, fi, g_ed.get(kind, index, p[2].c_str()) + d);
        host_log("live: viewer test: estep: %d presses, %u steps in the change list\n", n, unsigned(g_ed.steps().size()));
    } else if (name == "eundo") {
        editUndo();
    } else if (name == "eredo") {
        editRedo();
    } else if (name == "egrid") {
        g_eopt.grid = std::max(0, atoi(arg.c_str()));
    } else if (name == "esnap") {
        g_eopt.snapGround = takeFlag(arg);
    } else if ((name == "edrag" || name == "erot" || name == "eaxis") && kind >= 0 && p.size() >= 3) {
        if (!selectIt()) return;
        const Gizmo g = gizmo();
        if (!g.ok) {
            host_log("live: viewer test: %s: no gizmo (not editing, or off screen)\n", name.c_str());
            return;
        }
        ImVec2 s0 = g.c, s1;
        int mode = 1;
        bool ok = true;
        if (name == "edrag") {
            double x = 0, z = 0;
            sscanf(p[2].c_str(), "%lf,%lf", &x, &z);
            ok = projectPoint({x, g.p.y, z}, s1);
        } else if (name == "erot") {
            mode = 5;
            const double h0 = edit::degreesFromHeading(g_ed.get(kind, index, "heading")) * 3.14159265358979 / 180.0,
                         h1 = atof(p[2].c_str()) * 3.14159265358979 / 180.0, r = g.len * 1.35;
            ok = projectPoint({g.p.x + std::sin(h0) * r, g.p.y, g.p.z + std::cos(h0) * r}, s0) &&
                 projectPoint({g.p.x + std::sin(h1) * r, g.p.y, g.p.z + std::cos(h1) * r}, s1);
        } else {
            const int a = p[2] == "x" ? 0 : p[2] == "y" ? 1 : 2;
            const double d = p.size() > 3 ? atof(p[3].c_str()) : 0;
            mode = 2 + a;
            s0 = ImVec2((g.c.x + g.tip[a].x) / 2, (g.c.y + g.tip[a].y) / 2);
            ok = projectPoint({g.p.x + (a == 0 ? g.len / 2 + d : 0), g.p.y + (a == 1 ? g.len / 2 + d : 0),
                               g.p.z + (a == 2 ? g.len / 2 + d : 0)}, s1);
        }
        const int hit = gizmoHit(s0.x, s0.y);
        host_log("live: viewer test: %s: the gizmo at (%.0f, %.0f) is part %d (want %d)\n", name.c_str(), s0.x, s0.y,
                 hit, mode);
        if (!ok) {
            host_log("live: viewer test: %s: the target isn't in the picture (move the camera)\n", name.c_str());
            return;
        }
        if (!beginDrag(mode, s0.x, s0.y)) {
            host_log("live: viewer test: %s: the drag didn't start\n", name.c_str());
            return;
        }
        updateDrag((s0.x + s1.x) / 2, (s0.y + s1.y) / 2);
        updateDrag(s1.x, s1.y);
        endDrag();
    } else if (name == "ewarn") {
        const std::string w = carrierWarning();
        host_log("live: viewer test: carrier warning: %s\n", w.empty() ? "(none)" : w.c_str());
    } else if (name == "eapply") {
        applyEdits();
    } else if (name == "eplay") {
        playEdits();
    } else if (name == "esave") {
        saveMod(p.empty() ? "test" : p[0], p.size() > 1 && takeFlag(p[1]));
    } else {
        host_log("live: viewer test: %s:%s not understood\n", name.c_str(), arg.c_str());
        return;
    }
    if (!g_editMsg.empty()) host_log("live: viewer test: %s -> %s\n", name.c_str(), g_editMsg.c_str());
}

/* the terrain tools' test ops (--viewer-test; through the picking, as the mouse would, where a point is given):
 *   tmode:1|0                  the Terrain mode            ttool:raise|lower|smooth|flatten|ramp|select|paint
 *   tbrush:R,S,F[,A]           radius, strength (units/s), falloff, amount     tflat:Y   the flatten height
 *   tstroke:X1,Z1,X2,Z2,N      a stroke of N applications from (X1, Z1) to (X2, Z2)
 *   tramp:X1,Z1,X2,Z2,RISE     a ramp clicked from one point to the other, its end RISE above the ground
 *   tselect:X,Z[,add]          a click with the select tool    tclear   tsplit   tseal   tmove:DX,DY,DZ
 *   tpaint:TEX[,keep]          the selection painted          tretex:TEX   the selection's run switched
 *   timport:PNG                a picture into the selection's first slot, used     tfollow   tinfo */
bool worldToScreen(double x, double z, double yOff, ImVec2 &s) {
    double y = 0;
    if (!g_ed.terrain().heightAt(x, z, y)) return false;
    return projectPoint({x, y + yOff, z}, s);
}

void runTerrainOp(const std::string &name, const std::string &arg) {
    double v[6] = {0, 0, 0, 0, 0, 0};
    const int nv = sscanf(arg.c_str(), "%lf,%lf,%lf,%lf,%lf,%lf", &v[0], &v[1], &v[2], &v[3], &v[4], &v[5]);
    if (name == "tmode") {
        if (!g_editMode) setEditMode(true);
        g_tv.on = takeFlag(arg) && g_ed.terrainOk();
        host_log("live: viewer test: terrain mode %s\n", g_tv.on ? "on" : "off (or no terrain)");
        return;
    }
    if (!terrainMode()) {
        host_log("live: viewer test: %s: not in the Terrain mode\n", name.c_str());
        return;
    }
    uiFrame(buildUi, false);   /* (the overlays' projection of this view, for the picking) */
    if (name == "ttool") {
        for (int t = 0; t < TT_COUNT; t++) {
            std::string n = kToolNames[t];
            for (char &c : n) c = char(tolower((unsigned char) c));
            if (n == arg) g_tv.tool = t;
        }
    } else if (name == "tbrush" && nv >= 3) {
        g_tv.brush.radius = v[0], g_tv.rate = v[1], g_tv.brush.falloff = v[2];
        if (nv >= 4) g_tv.amount = v[3];
    } else if (name == "tapplyeach") {
        g_tv.applyEach = takeFlag(arg);
    } else if (name == "tflat" && nv >= 1) {
        g_tv.flatY = v[0], g_tv.flatPicked = true;
    } else if (name == "tstroke" && nv >= 5) {
        const int n = std::max(1, int(v[4]));
        const auto t0 = std::chrono::steady_clock::now();
        for (int i = 0; i < n; i++) {
            const double t = n == 1 ? 0 : double(i) / (n - 1);
            ImVec2 s;
            if (!worldToScreen(v[0] + (v[2] - v[0]) * t, v[1] + (v[3] - v[1]) * t, 0, s)) continue;
            if (!g_tv.stroking) {
                terrainMouseDown(s.x, s.y, false, false);
            } else {
                terrainHover(s.x, s.y);
                applyBrushOnce(g_tv.rate / 30.0);
            }
        }
        const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        host_log("live: viewer test: stroke: %u applications, %u corners, %.2f ms (picking and brush)\n", g_tv.applications,
                 unsigned(g_tv.cur.size()), ms);
        terrainMouseUp();
    } else if (name == "tramp" && nv >= 5) {
        g_tv.tool = TT_RAMP;
        g_tv.rampN = 0;
        g_tv.rampRise = v[4];
        ImVec2 a, b;
        if (worldToScreen(v[0], v[1], 0, a) && worldToScreen(v[2], v[3], 0, b)) {
            terrainMouseDown(a.x, a.y, false, false);
            terrainMouseDown(b.x, b.y, false, false);
            makeRamp();
        } else {
            host_log("live: viewer test: tramp: a point isn't in the picture\n");
        }
    } else if (name == "tselect" && nv >= 2) {
        ImVec2 s;
        if (worldToScreen(v[0], v[1], 0, s)) {
            if (g_tv.tool != TT_SELECT && g_tv.tool != TT_PAINT) g_tv.tool = TT_SELECT;
            terrainMouseDown(s.x, s.y, arg.find("add") != std::string::npos, false);
            if (!g_tv.hoverOk) host_log("live: viewer test: tselect: no triangle under (%.0f, %.0f) on the screen\n", s.x, s.y);
            else host_log("live: viewer test: tselect: triangle %d at (%.0f, %.0f, %.0f)\n", g_tv.hoverTri, g_tv.hover[0],
                          g_tv.hover[1], g_tv.hover[2]);
        } else {
            host_log("live: viewer test: tselect: the point isn't in the picture\n");
        }
    } else if (name == "tclick" || name == "tstrokec") {   /* at the picture's centre (tstrokec:N applications) */
        const ImGuiIO &io = ImGui::GetIO();
        const auto t0 = std::chrono::steady_clock::now();
        terrainMouseDown(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f, arg.find("add") != std::string::npos, false);
        for (int i = 1; name == "tstrokec" && i < std::max(1, int(v[0])); i++) {
            terrainHover(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f);
            applyBrushOnce(g_tv.rate / 30.0);
        }
        const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        if (name == "tstrokec")
            host_log("live: viewer test: stroke: %u applications, %u corners, %.2f ms (picking and brush)\n",
                     g_tv.applications, unsigned(g_tv.cur.size()), ms);
        terrainMouseUp();
    } else if (name == "tclear") {
        g_tv.selTri.clear(), g_tv.selPos.clear();
    } else if (name == "tsplit") {
        splitSelection();
    } else if (name == "tseal") {
        host_log("live: viewer test: seams: %u\n", unsigned(g_tv.seams));
        sealSeams();
    } else if (name == "tmove" && nv >= 3) {
        moveSelection(int(v[0]), int(v[1]), int(v[2]));
    } else if (name == "tpaint" && nv >= 1) {
        g_tv.planar = arg.find("keep") == std::string::npos;
        paintSelection(int(v[0]));
    } else if (name == "tretex" && nv >= 1) {
        retexRun(int(v[0]));
    } else if (name == "timport") {
        const std::vector<SlotUse> slots = selectionSlots();
        if (slots.empty()) {
            host_log("live: viewer test: timport: no slot (select a triangle)\n");
        } else {
            snprintf(g_tv.importPath, sizeof g_tv.importPath, "%s", arg.c_str());
            g_tv.importSlot = slots[0].tex;
            if (importLoad(arg, slots[0])) importAdd();
            host_log("live: viewer test: timport: %s\n", g_tv.importMsg.c_str());
        }
    } else if (name == "tload") {   /* the picture loaded into the selection's first slot, shown, not used yet */
        const std::vector<SlotUse> slots = selectionSlots();
        if (!slots.empty()) {
            snprintf(g_tv.importPath, sizeof g_tv.importPath, "%s", arg.c_str());
            g_tv.importSlot = slots[0].tex;
            importLoad(arg, slots[0]);
            host_log("live: viewer test: tload: %s\n", g_tv.importMsg.c_str());
        }
    } else if (name == "tuse") {
        importAdd();
    } else if (name == "timportnew") {
        const std::vector<SlotUse> slots = selectionSlots();
        if (slots.empty()) {
            host_log("live: viewer test: timportnew: no slot (select a triangle)\n");
        } else {
            snprintf(g_tv.importPath, sizeof g_tv.importPath, "%s", arg.c_str());
            g_tv.importSlot = slots[0].tex;
            if (importLoad(arg, slots[0], true)) importAdd();
            host_log("live: viewer test: timportnew: %s\n", g_tv.importMsg.c_str());
        }
    } else if (name == "tfollow") {
        followRecords();
    } else if (name == "tpal") {
        std::string s;
        for (const PalEntry &e : palette()) {
            char b[64];
            snprintf(b, sizeof b, " %d:%s%d:%dx%d:%d", e.tex, tc_fmt_name(e.fmt), e.siz, e.w, e.h, e.count);
            s += b;
        }
        host_log("live: viewer test: palette:%s\n", s.c_str());
        s.clear();
        for (int t : texturesHere()) s += " " + std::to_string(t);
        host_log("live: viewer test: textures here:%s\n", s.c_str());
        const std::vector<SlotUse> su = selectionSlots();
        for (const SlotUse &u : su) host_log("live: viewer test: selection slot %d (%s %d-bit %dx%d)\n", u.tex, tc_fmt_name(u.fmt), u.siz, u.w, u.h);
    } else if (name == "tinfo") {
        const terrain::Model &m = g_ed.terrain();
        unsigned live = 0, prot = 0;
        for (const terrain::DrawTri &t : m.drawn()) live += !t.removed;
        for (const terrain::Pos &p : m.positions()) prot += p.prot;
        host_log("live: viewer test: terrain: %u corners (%u protected), %u drawn triangles live; selection %u/%u; file %u "
                 "bytes; lists %d/%d %d/%d %d/%d %d/%d; last rebuild %.1f ms; preview %u corners mapped, %u written (replay %.2f ms); "
                 "seams %u; walls %u; corners following another mesh's edge %d\n",
                 unsigned(m.positions().size()), prot, live, unsigned(g_tv.selTri.size()), unsigned(g_tv.selPos.size()),
                 unsigned(g_ed.image().size()), g_ed.listN[0], g_ed.listCap[0], g_ed.listN[1], g_ed.listCap[1],
                 g_ed.listN[2], g_ed.listCap[2], g_ed.listN[3], g_ed.listCap[3], g_ed.buildMs, unsigned(g_tv.mapCorners),
                 g_tv.previewed, g_tv.previewMs, unsigned(g_tv.seams), unsigned(m.walls().size()), m.followerCount());
    } else {
        host_log("live: viewer test: %s:%s not understood\n", name.c_str(), arg.c_str());
        return;
    }
    if (!g_editMsg.empty()) host_log("live: viewer test: %s -> %s\n", name.c_str(), g_editMsg.c_str());
}

/* a virtual game controller for the test script: the same path a real one takes (SDL's controller API, the
 * controllers' thread, the viewer's camera, tools and - Start - ImGui's panel navigation)
 *   vpad:attach          plug it in            vpad:A+X[,N]   buttons held for N frames (2), then let go
 *   vpadaxis:lx,V        an axis to V (lx ly rx ry lt rt; -32768..32767; triggers rest at -32768) */
SDL_Joystick *g_vpad;
uint32_t g_vpadHeld;
int g_vpadFrames;

int padButtonNamed(const std::string &n) {
    static const char *const names[] = {"a", "b", "x", "y", "back", "guide", "start", "l3", "r3", "lb", "rb", "up", "down",
                                        "left", "right"};
    std::string l = n;
    for (char &c : l) c = char(tolower((unsigned char) c));
    for (int i = 0; i < int(sizeof names / sizeof names[0]); i++)
        if (l == names[i]) return i;   /* (SDL_CONTROLLER_BUTTON_A .. DPAD_RIGHT, in order) */
    return -1;
}

void vpadOp(const std::string &name, const std::string &arg) {
    if (name == "vpad" && arg == "attach") {
        if (g_vpad == nullptr) {
            SDL_VirtualJoystickDesc d;
            SDL_zero(d);
            d.version = SDL_VIRTUAL_JOYSTICK_DESC_VERSION;
            d.type = SDL_JOYSTICK_TYPE_GAMECONTROLLER;
            d.naxes = SDL_CONTROLLER_AXIS_MAX;
            d.nbuttons = SDL_CONTROLLER_BUTTON_MAX;
            d.name = "bc.exe test pad";
            const int idx = SDL_JoystickAttachVirtualEx(&d);
            g_vpad = idx >= 0 ? SDL_JoystickOpen(idx) : nullptr;
            if (g_vpad != nullptr) {
                SDL_JoystickSetVirtualAxis(g_vpad, SDL_CONTROLLER_AXIS_TRIGGERLEFT, -32768);
                SDL_JoystickSetVirtualAxis(g_vpad, SDL_CONTROLLER_AXIS_TRIGGERRIGHT, -32768);
            }
            host_log("live: viewer test: virtual controller %s\n", g_vpad ? "attached" : SDL_GetError());
        }
        g_testWait = 40;   /* (the controllers' thread finds it within ~16 ms; ImGui on its device event) */
        return;
    }
    if (g_vpad == nullptr) {
        host_log("live: viewer test: %s: no virtual controller (vpad:attach)\n", name.c_str());
        return;
    }
    if (name == "vpad") {
        const size_t c = arg.find(',');
        const int frames = c == std::string::npos ? 2 : std::max(1, atoi(arg.c_str() + c + 1));
        std::string list = arg.substr(0, c), bad;
        size_t p = 0;
        while (p <= list.size()) {
            const size_t q = list.find('+', p);
            const std::string b = list.substr(p, q == std::string::npos ? std::string::npos : q - p);
            const int bi = padButtonNamed(b);
            if (bi >= 0) SDL_JoystickSetVirtualButton(g_vpad, bi, 1), g_vpadHeld |= 1u << bi;
            else bad += " " + b;
            if (q == std::string::npos) break;
            p = q + 1;
        }
        if (!bad.empty()) host_log("live: viewer test: vpad: not a button:%s\n", bad.c_str());
        g_vpadFrames = frames;
        g_testWait = frames + 3;
        SDL_Delay(8);   /* (sampled before this frame's camera step reads it) */
    } else if (name == "vpaddown" || name == "vpadup") {   /* a button held / let go (for combinations over frames) */
        const int bi = padButtonNamed(arg);
        if (bi >= 0) SDL_JoystickSetVirtualButton(g_vpad, bi, name == "vpaddown" ? 1 : 0);
        else host_log("live: viewer test: %s: not a button: %s\n", name.c_str(), arg.c_str());
        g_testWait = 3;
        SDL_Delay(8);
    } else if (name == "vpadaxis") {
        static const char *const axes[] = {"lx", "ly", "rx", "ry", "lt", "rt"};
        const size_t c = arg.find(',');
        const std::string an = arg.substr(0, c);
        const int v = c == std::string::npos ? 0 : atoi(arg.c_str() + c + 1);
        for (int i = 0; i < 6; i++)
            if (an == axes[i]) SDL_JoystickSetVirtualAxis(g_vpad, i, Sint16(std::max(-32768, std::min(32767, v))));
        g_testWait = 3;
    }
}

/* (after each viewer frame: a held virtual button lets go after its frames; the controllers' thread is given
 * a moment to sample every change, so a press of N frames is seen by exactly N frames) */
void vpadAfterFrame() {
    if (g_vpadFrames > 0 && --g_vpadFrames == 0) {
        for (int b = 0; b < 32; b++)
            if (g_vpadHeld & (1u << b)) SDL_JoystickSetVirtualButton(g_vpad, b, 0);
        g_vpadHeld = 0;
        SDL_Delay(8);
    }
}

void runTestOp() {
    if (g_testWait > 0) {
        g_testWait--;
        return;
    }
    if (g_testPos >= g_test.size()) return;
    const std::string op = g_test[g_testPos++];
    host_log("live: viewer test: %s\n", op.c_str());
    const size_t c = op.find(':');
    const std::string name = op.substr(0, c), arg = c == std::string::npos ? "" : op.substr(c + 1);
    const size_t eq = arg.find('=');
    const std::string key = arg.substr(0, eq), val = eq == std::string::npos ? "" : arg.substr(eq + 1);
    if (name == "wait") {
        g_testWait = std::max(0, atoi(arg.c_str()));
    } else if (name == "cam") {
        if (arg == "start") jumpToStart();
        else if (arg == "game") cameraFromGame(), g_mv.on = false;
        else if (arg == "top") cameraTop();
        else {
            double x, y, z, yaw, pitch;
            if (sscanf(arg.c_str(), "%lf,%lf,%lf,%lf,%lf", &x, &y, &z, &yaw, &pitch) == 5) {
                g_mv.on = false;
                g_cam.pos = {x, y, z}, g_cam.yaw = yaw, g_cam.pitch = pitch;
            }
        }
    } else if (name == "move") {   /* move:FWD,RIGHT,UP (drawing units) */
        double f = 0, r = 0, u = 0;
        sscanf(arg.c_str(), "%lf,%lf,%lf", &f, &r, &u);
        g_cam.move(f, r, 0, 1.0);
        g_cam.pos.y += u;
    } else if (name == "show") {
        for (int k = 0; k < CAT_COUNT; k++)
            if (key == kCatNames[k]) g_opt.show[k] = takeFlag(val);
        if (key == "far") g_opt.farOut = takeFlag(val);
        if (key == "plain") g_opt.plain = takeFlag(val);
        if (key == "markers") g_opt.markers = takeFlag(val);
        if (key == "labels") g_opt.labels = takeFlag(val);
        if (key == "collision") g_opt.collision = takeFlag(val);
        if (key == "zones") g_opt.zones = takeFlag(val);
        if (key == "labeldist") g_opt.labelDist = std::max(0.0, atof(val.c_str()));
        if (key == "cells") g_opt.allCells = takeFlag(val);
        for (int k = 0; k < viewer::REC_KINDS; k++)
            if (key == viewer::recKindKey(k)) g_opt.kinds[k] = takeFlag(val);
    } else if (name == "select" || name == "dump") {   /* select:KIND:I */
        const size_t c2 = arg.find(':');
        const std::string kk = arg.substr(0, c2);
        const int idx = c2 == std::string::npos ? 0 : atoi(arg.c_str() + c2 + 1);
        for (int k = 0; k < viewer::REC_KINDS; k++)
            if (kk == viewer::recKindKey(k)) g_sel = findRecord(k, idx);
        if (g_sel >= 0) {
            const Record &r = g_recs[g_sel];
            host_log("live: viewer: record %s (%s, 0x%06X):\n", r.title.c_str(), viewer::recKindName(r.kind), r.offset);
            for (const viewer::Field &f : r.fields) host_log("live: viewer:   %s: %s\n", f.name.c_str(), f.value.c_str());
            std::string hex = viewer::hexDump(r.raw);
            for (char &ch : hex)
                if (ch == '\n') ch = '|';
            host_log("live: viewer:   raw: %s\n", hex.c_str());
            if (name == "select" && r.hasPos) {
                g_mv.on = false;
                g_cam.lookAt({r.x, r.y, r.z}, std::max(150.0, r.radius * 6));
            }
        } else {
            host_log("live: viewer test: no record %s\n", arg.c_str());
        }
    } else if (name == "model") {   /* model:KIND:I (a record) or model:off */
        if (arg == "off") {
            g_mv.on = false;
        } else {
            const size_t c2 = arg.find(':');
            const std::string kk = arg.substr(0, c2);
            const int idx = c2 == std::string::npos ? 0 : atoi(arg.c_str() + c2 + 1);
            int rec = -1;
            for (int k = 0; k < viewer::REC_KINDS; k++)
                if (kk == viewer::recKindKey(k)) rec = findRecord(k, idx);
            if (!modelFor(rec)) host_log("live: viewer test: no model for %s\n", arg.c_str());
            g_testWait = 3;   /* (measured by the next replays) */
        }
    } else if (name == "orbit") {   /* orbit:YAW,PITCH[,DISTANCE FACTOR] */
        double y = g_mv.yaw, p = g_mv.pitch, d = 1.0;
        sscanf(arg.c_str(), "%lf,%lf,%lf", &y, &p, &d);
        g_mv.yaw = y, g_mv.pitch = p, g_mv.dist = std::max(1.0, g_mv.radius * 2.6 * d);
    } else if (name == "shot" || name == "oshot" || name == "pshot") {   /* clean / overlays / as on screen */
        doShot(arg, name != "shot", name == "pshot");
    } else if (name == "vpad" || name == "vpadaxis" || name == "vpaddown" || name == "vpadup") {
        vpadOp(name, arg);
    } else if (name == "padstate") {   /* what the controller works now, the editor's state (for the test log) */
        const ImGuiIO &io = ImGui::GetIO();
        host_log("live: viewer test: padstate: controller %s; panels %s; edit %d terrain %d tool %d; selection %u tris "
                 "%u corners; record %d; steps %u; ImGui gamepad %d nav %d active %d visible %d; joysticks %d; camera %.0f "
                 "%.0f %.0f yaw %.0f; brush %.0f rate %.0f; aim %d tri %d at %.0f %.0f %.0f stroking %d ramp %d; mod name "
                 "\"%s\"; saved %s\n",
                 g_padNav ? "on the panels" : "on the camera", g_opt.ui ? "shown" : "hidden", int(g_editMode), int(g_tv.on),
                 g_tv.tool, unsigned(g_tv.selTri.size()), unsigned(g_tv.selPos.size()), g_sel, unsigned(g_ed.steps().size()),
                 (io.BackendFlags & ImGuiBackendFlags_HasGamepad) != 0, (io.ConfigFlags & ImGuiConfigFlags_NavEnableGamepad) != 0,
                 int(io.NavActive), int(io.NavVisible), SDL_NumJoysticks(), g_cam.pos.x, g_cam.pos.y, g_cam.pos.z, g_cam.yaw,
                 g_tv.brush.radius, g_tv.rate, int(g_tv.hoverOk), g_tv.hoverTri, g_tv.hover[0], g_tv.hover[1], g_tv.hover[2],
                 int(g_tv.stroking), g_tv.rampN, g_modName, g_savedPath.empty() ? "-" : g_savedPath.c_str());
        ImGuiContext *gc = ImGui::GetCurrentContext();
        if (gc && arg == "log") {   /* padstate:log: ImGui's navigation log from here on; padstate:dump prints it */
            gc->DebugLogFlags |= ImGuiDebugLogFlags_EventNav;
            gc->DebugLogBuf.clear();
        }
        if (gc && arg == "dump") host_log("live: viewer test: ImGui log:\n%s\n", gc->DebugLogBuf.c_str());
        if (gc && gc->NavWindow)
            host_log("live: viewer test: padstate: nav window %s id %08X rect %.0f,%.0f-%.0f,%.0f scroll %.0f target %.0f "
                     "inner %.0f-%.0f\n", gc->NavWindow->Name, unsigned(gc->NavId), gc->NavWindow->NavRectRel[0].Min.x,
                     gc->NavWindow->NavRectRel[0].Min.y, gc->NavWindow->NavRectRel[0].Max.x, gc->NavWindow->NavRectRel[0].Max.y,
                     gc->NavWindow->Scroll.y, gc->NavWindow->ScrollTarget.y, gc->NavWindow->InnerRect.Min.y,
                     gc->NavWindow->InnerRect.Max.y);
        if (gc && gc->NavWindowingTarget)
            host_log("live: viewer test: padstate: choosing a window (hold X): %s\n", gc->NavWindowingTarget->Name);
    } else if (name == "edit") {   /* the editor: edit:on / edit:off */
        setEditMode(takeFlag(arg));
    } else if (name.size() > 1 && name[0] == 'e' && name != "edit") {
        runEditOp(name, arg);
    } else if (name.size() > 1 && name[0] == 't') {
        runTerrainOp(name, arg);
    } else if (name == "ui") {
        g_opt.ui = takeFlag(arg);
    } else if (name == "level") {
        requestLevel(atoi(arg.c_str()));
    } else if (name == "recapture") {
        g_recapture = true;
    } else if (name == "leave") {
        g_leave = true;
    } else if (name == "quit") {
        g_quitAfter = true;
        g_leave = true;
    } else {
        host_log("live: viewer test: %s not understood\n", op.c_str());
    }
}

}  // namespace vw
}  // namespace live
