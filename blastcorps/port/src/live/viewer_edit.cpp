/* The level viewer's editor: the record edits, the gizmo and the mod files (viewer_internal.h).
 * The editor works on a host-side copy of the level file (edit::Editor over port_mods.h's copy of the ROM's
 * file, plus the changes a loaded mod already makes to it); the markers and the inspector show that copy
 * while editing.  Apply gives the game the edited file (port_level_set_edit) and reloads the level through
 * the game's own set-up, so what is shown afterwards is the game running it. */
#include <windows.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <functional>
#include <string>
#include <vector>

#include "imgui/imgui.h"

#include "live_menu.h"
#include "viewer_internal.h"

#include "load/port_mods.h"

extern "C" {
#include "plat_host.h"
}

namespace live {
namespace vw {

Drag g_drag;
EditOpts g_eopt;
std::string g_editMsg;            /* the last edit's result (the Changes panel) */
std::string g_savedSha;           /* the edited file last saved to a mod file */
std::string g_savedPath;
char g_modName[64] = "my_level_mod";
bool g_modEnable = true;
bool g_playAfterLoad;             /* "Play from here": leave the viewer into play once the level is up */
bool g_testRun;                   /* this run played an edited level: saving is off */
std::chrono::steady_clock::time_point g_applyStart;   /* Apply: the reload's time, to the frozen picture */
bool g_applyPending;
double g_applyMs;
/* the overlays' projection of this tick (the gizmo's picking) */
OverlayView g_ov;
/* edits asked for by the panels: run after the UI frame is built (they replace the records the panels read) */
std::vector<std::function<void()>> g_after;

bool isEditable(int kind) {
    return kind >= viewer::REC_VEHICLE && kind <= viewer::REC_BLOCKSLOT;
}
bool kindHasY(int kind) {
    return kind != viewer::REC_CARRIER;
}
bool kindHasHeading(int kind) {
    return kind == viewer::REC_VEHICLE || kind == viewer::REC_CARRIER;
}
bool editing() {
    return g_editMode && g_ed.isOpen() && g_ed.level() == g_fz.levelNo && g_fz.level && !g_mv.on;
}

/* the editor on the frozen level (opened on the ROM's file and the loaded mods' changes) */
bool editorOpen() {
    if (!g_fz.level || g_fz.levelNo < 0 || g_fz.levelNo >= 60) {
        g_editMsg = "The editor works on a level frame.";
        return false;
    }
    if (g_ed.isOpen() && g_ed.level() == g_fz.levelNo) return true;
    /* another level's edits: kept for when it comes back (the game keeps its applied file too) */
    static std::vector<edit::EditStep> stash[60];
    if (g_ed.isOpen()) stash[g_ed.level()] = g_ed.steps();
    const uint8_t *orig = nullptr;
    size_t n = 0;
    if (port_level_original(g_fz.levelNo, &orig, &n)) {
        g_editMsg = "The level file can't be read from the ROM.";
        return false;
    }
    std::vector<LmChange> base;
    const LmChange *ch = nullptr;
    int nch = 0;
    const char *modName = nullptr;
    std::string baseName;
    if (port_level_mod_changes(g_fz.levelNo, &ch, &nch, &modName)) {
        base.assign(ch, ch + nch);
        baseName = modName ? modName : "";
    }
    /* the loaded mods' geometry for this level and their texture slots: the editor keeps them (and saves them) */
    std::vector<edit::TexSlot> baseTex;
    {
        int nt = 0;
        const char *tn = nullptr;
        const LmTexture *mt = port_mods_textures(&nt, &tn);
        for (int i = 0; i < nt; i++) {
            edit::TexSlot t;
            t.id = mt[i].id, t.fmt = mt[i].fmt, t.siz = mt[i].siz, t.isNew = mt[i].isNew != 0, t.from = mt[i].from;
            t.mips = mt[i].mips;
            t.palette = mt[i].hasPalette ? mt[i].palette : -1;
            t.sha1 = mt[i].slot_sha1;
            t.png = mt[i].png ? mt[i].png : "";
            baseTex.push_back(t);
        }
        if (nt > 0 && baseName.empty() && tn != nullptr) baseName = tn;
    }
    if (!g_ed.open(g_fz.levelNo, orig, n, base, baseName, port_level_mod_geo(g_fz.levelNo), baseTex)) {
        g_editMsg = "The level can't be edited: " + g_ed.error();
        host_log("live: editor: level %d: %s\n", g_fz.levelNo, g_ed.error().c_str());
        return false;
    }
    g_savedSha = g_ed.imageSha1();
    g_savedPath.clear();
    terrainOpened();
    for (const edit::EditStep &s : stash[g_fz.levelNo]) g_ed.pushStep(s);
    stash[g_fz.levelNo].clear();
    /* what the game runs now: this level's edited file, if it was given one */
    const uint8_t *cur = nullptr;
    size_t curSize = 0;
    if (port_level_edited(g_fz.levelNo, &cur, &curSize)) {
        char sha[41];
        lm_sha1_hex(cur, curSize, sha);
        if (sha == g_ed.imageSha1()) g_ed.markApplied();
    }
    host_log("live: editor: level %d opened (file 0x%X bytes, SHA-1 %s%s%s%s; terrain tools %s, %u corners, %u drawn "
             "triangles; %.1f ms)\n",
             g_fz.levelNo, unsigned(n), g_ed.originalSha1().c_str(), baseName.empty() ? "" : "; the changes of mod ",
             baseName.c_str(), g_ed.hasBaseGeo() ? " (with its geometry)" : "", g_ed.terrainOk() ? "on" : "off",
             unsigned(g_ed.terrain().positions().size()), unsigned(g_ed.terrain().drawn().size()), g_ed.buildMs);
    return true;
}

void reselect(int kind, int index) {
    g_sel = findRecord(kind, index);
}

void setEditMode(bool on) {
    int kind = -1, index = -1;
    if (g_sel >= 0 && g_sel < int(g_recs.size())) kind = g_recs[g_sel].kind, index = g_recs[g_sel].index;
    if (on && !editorOpen()) return;
    g_editMode = on;
    g_drag = Drag();
    readFileRecords();
    if (kind >= 0) reselect(kind, index);
    host_log("live: editor: %s\n", on ? "on" : "off");
}

bool pushEdit(const LmChange &c, int selKind, int selIndex) {
    if (!g_ed.push(c)) {
        g_editMsg = "Not done: " + g_ed.error();
        host_log("live: editor: refused: %s: %s\n", edit::describe(c).c_str(), g_ed.error().c_str());
        return false;
    }
    g_editMsg = edit::describe(c);
    host_log("live: editor: %s (%u changes; file SHA-1 %s)\n", g_editMsg.c_str(), unsigned(g_ed.changes().size()),
             g_ed.imageSha1().c_str());
    readFileRecords();
    if (selKind >= 0) reselect(selKind, selIndex);
    else g_sel = -1;
    return true;
}

/* the carrier's route (the line the overlay draws: its start, heading, route length) and what the edits put on it:
 * ground moved under or beside it (kCarrierHalfWidth either side, from kCarrierBehind before its start to its end),
 * records (buildings, vehicles, blocks) the editor moved or added there - the game crashes the carrier when it hits
 * something or its wheels' heights part (carrier_check_crash).  "" if nothing; cached per edited file. */
constexpr double kCarrierHalfWidth = 160, kCarrierBehind = 200;
std::string carrierWarning() {
    static std::string sha, text;
    if (!editing() || !g_ed.isOpen()) return std::string();
    const std::string now = g_ed.imageSha1();
    if (now == sha) return text;
    sha = now, text.clear();
    const Record *car = nullptr;
    for (const Record &r : g_recs)
        if (r.kind == viewer::REC_CARRIER && r.type != 0) car = &r;
    if (car == nullptr) return text;
    double dx, dz, L = 0;
    viewer::headingDir(car->heading, dx, dz);
    for (const viewer::Field &f : car->fields)
        if (f.name == "route length") L = atof(f.value.c_str());
    /* distance from (x, z) to the route's segment, in x-z */
    auto onRoute = [&](double x, double z) {
        const double px = x - car->x, pz = z - car->z;
        const double along = px * dx + pz * dz, across = std::fabs(px * dz - pz * dx);
        return along >= -kCarrierBehind && along <= L + kCarrierHalfWidth && across <= kCarrierHalfWidth;
    };
    int moved = 0, up = 0, down = 0;
    if (g_ed.terrainOk()) {
        const terrain::Model &m = g_ed.terrain();
        for (const terrain::Pos &p : m.positions()) {
            if (!p.level || p.cur == p.orig || !onRoute(p.cur.x, p.cur.z)) continue;
            moved++;
            up = std::max(up, p.cur.y - p.orig.y), down = std::min(down, p.cur.y - p.orig.y);
        }
    }
    std::string recs;
    int nrec = 0;
    for (const Record &r : g_recs) {
        if (r.kind != viewer::REC_OBJECT && r.kind != viewer::REC_VEHICLE && r.kind != viewer::REC_BLOCK) continue;
        if (!r.hasPos || g_ed.edited(r.kind, r.index) != 1 || !onRoute(r.x, r.z)) continue;
        if (nrec++ < 3) recs += (recs.empty() ? "" : ", ") + r.title;
    }
    if (moved == 0 && nrec == 0) return text;
    char b[300] = "";
    if (moved)
        snprintf(b, sizeof b, "%d corner%s of the ground under or beside it moved (up to %+d / %+d)", moved,
                 moved == 1 ? "" : "s", up, down);
    text = std::string("The carrier's route: ") + b;
    if (nrec) text += std::string(moved ? "; " : "") + recs + (nrec > 3 ? " and more" : "") + " moved or added onto it";
    text += ". The carrier may crash as the level starts (it hits what's in its way, or its wheels' heights part): "
            "keep its route clear, or Apply and see.";
    host_log("live: editor: warning: %s\n", text.c_str());
    return text;
}

/* a field's - / + held (repeating) or pressed again and again: one undo step for the burst - the presses less
 * than kBurstMs apart on the same field of the same record replace the burst's step instead of adding one (and a
 * burst back where it began leaves no step) */
struct Burst {
    int kind = -1, index = -1, field = -1;
    bool onTop = false;           /* the burst's step is the change list's last */
    size_t steps = 0;             /* the list's length after it */
    std::chrono::steady_clock::time_point t;
};
Burst g_burst;
constexpr double kBurstMs = 700;

bool pushEditBurst(int kind, int index, int field, long value) {
    const auto now = std::chrono::steady_clock::now();
    const double ms = std::chrono::duration<double, std::milli>(now - g_burst.t).count();
    const bool same = g_burst.kind == kind && g_burst.index == index && g_burst.field == field && ms < kBurstMs;
    const bool replace = same && g_burst.onTop && g_ed.steps().size() == g_burst.steps && !g_ed.steps().empty() &&
                         g_ed.steps().back().type == edit::STEP_RECORD;
    if (replace) {
        g_ed.undo();   /* (the burst's step so far: the new value goes in its place) */
        long v0 = 0;
        g_ed.get(kind, index, field, v0);
        if (v0 == value) {
            g_burst.onTop = false, g_burst.t = now;
            g_editMsg = "Back where it was (" + std::string(lm_field_name(kind, field)) + " " + std::to_string(value) + ")";
            host_log("live: editor: %s: no change left of the burst\n", g_editMsg.c_str());
            readFileRecords();
            reselect(kind, index);
            return true;
        }
    }
    if (!pushEdit(edit::makeSet(kind, index, {{field, value}}), kind, index)) {
        if (replace) g_ed.redo();   /* (refused: the burst's step back as it was) */
        g_burst = Burst();
        return false;
    }
    g_burst.kind = kind, g_burst.index = index, g_burst.field = field;
    g_burst.onTop = true, g_burst.steps = g_ed.steps().size(), g_burst.t = now;
    return true;
}

void editUndo() {
    if (!g_ed.canUndo()) return;
    const edit::EditStep s = g_ed.steps().back();
    g_ed.undo();
    g_editMsg = "Undone: " + edit::describe(s);
    host_log("live: editor: undo %s (%.1f ms)\n", edit::describe(s).c_str(), g_ed.buildMs);
    readFileRecords();
    terrainChanged();
    if (s.type == edit::STEP_RECORD)
        reselect(s.rec.kind, std::min(s.rec.index, std::max(0, g_ed.count(s.rec.kind) - 1)));
}

void editRedo() {
    if (!g_ed.canRedo()) return;
    if (!g_ed.redo()) {
        g_editMsg = "Not redone: " + g_ed.error();
        return;
    }
    const edit::EditStep s = g_ed.steps().back();
    g_editMsg = "Redone: " + edit::describe(s);
    host_log("live: editor: redo %s (%.1f ms)\n", edit::describe(s).c_str(), g_ed.buildMs);
    readFileRecords();
    terrainChanged();
    if (s.type == edit::STEP_RECORD) reselect(s.rec.kind, s.rec.op == LM_OP_DELETE ? -1 : s.rec.index);
}

/* the selected record, if the editor can change it */
bool selEditable(int &kind, int &index) {
    if (!editing() || g_sel < 0 || g_sel >= int(g_recs.size()) || !isEditable(g_recs[g_sel].kind)) return false;
    kind = g_recs[g_sel].kind, index = g_recs[g_sel].index;
    return index < g_ed.count(kind);
}

void duplicateSel() {
    int kind, index;
    if (!selEditable(kind, index)) return;
    /* the copy goes next to it, 48 map units along x (snapped to the grid) */
    const int fx = lm_field_index(kind, "x");
    const long x = g_ed.get(kind, index, "x");
    long nx = edit::snapTo(double(x + 48), g_eopt.grid);
    long lo, hi;
    lm_field_range(kind, fx, &lo, &hi);
    if (nx > hi) nx = edit::snapTo(double(x - 48), g_eopt.grid);
    pushEdit(edit::makeAdd(kind, index + 1, index, {{fx, nx}}), kind, index + 1);
}

void deleteSel() {
    int kind, index;
    if (!selEditable(kind, index)) return;
    pushEdit(edit::makeDelete(kind, index), -1, -1);
}

void rotateSel(double deg) {
    int kind, index;
    if (!selEditable(kind, index) || !kindHasHeading(kind)) return;
    const double d = edit::degreesFromHeading(g_ed.get(kind, index, "heading")) + deg;
    pushEdit(edit::makeSet(kind, index, {{lm_field_index(kind, "heading"), edit::headingFromDegrees(d)}}), kind,
             index);
}

void groundSel() {
    int kind, index;
    if (!selEditable(kind, index) || !kindHasY(kind)) return;
    const double x = double(g_ed.get(kind, index, "x")), z = double(g_ed.get(kind, index, "z"));
    const double y = groundAt(x, z, double(g_ed.get(kind, index, "y")));
    pushEdit(edit::makeSet(kind, index, {{lm_field_index(kind, "y"), std::lround(y)}}), kind, index);
}

/* a field's value clamped to its range */
long clampField(int kind, const char *name, double v) {
    long lo, hi;
    lm_field_range(kind, lm_field_index(kind, name), &lo, &hi);
    return std::max(lo, std::min(hi, long(std::lround(v))));
}

std::string modsDir() {
    return g_env.cfg != nullptr ? modsFolder(*g_env.cfg) : "mods\\";
}

/* the editor's file for the game's next load of the level (NULL: the ROM's, or the mod's as loaded) */
void giveEditsToGame() {
    const int level = g_ed.level();
    /* the texture slots: the editor's own (with the mods' it started from), else the mods' as loaded */
    bool ownTex = false;
    for (const edit::EditStep &s : g_ed.steps()) ownTex = ownTex || s.type == edit::STEP_TEXTURE;
    {
        std::vector<edit::TexSlot> ts;
        if (ownTex) ts = g_ed.textures();
        std::vector<LmTexture> lt(ts.size());
        for (size_t i = 0; i < ts.size(); i++) {
            memset(&lt[i], 0, sizeof lt[i]);
            lt[i].id = ts[i].id, lt[i].fmt = ts[i].fmt, lt[i].siz = ts[i].siz, lt[i].isNew = ts[i].isNew;
            lt[i].from = ts[i].from, lt[i].mips = ts[i].mips;
            lt[i].hasPalette = ts[i].palette >= 0, lt[i].palette = ts[i].palette >= 0 ? ts[i].palette : 0;
            snprintf(lt[i].slot_sha1, sizeof lt[i].slot_sha1, "%s", ts[i].sha1.c_str());
            lt[i].png = const_cast<char *>(ts[i].png.c_str());
        }
        char err[300];
        if (port_tex_editor_set(lt.empty() ? nullptr : lt.data(), int(lt.size()), err, sizeof err)) {
            g_editMsg = std::string("The texture slots aren't used: ") + err;
            host_log("live: editor: %s\n", g_editMsg.c_str());
        }
    }
    terrainApplied();
    if (g_ed.steps().empty()) {
        /* nothing of the editor's: what the game read before (the loaded mod's file, or the ROM's) */
        const LmChange *ch;
        int n;
        const char *name;
        if (port_level_mod_changes(level, &ch, &n, &name))
            port_level_set_edit(level, g_ed.image().data(), g_ed.image().size(), (std::string("mod ") + name).c_str());
        else
            port_level_set_edit(level, nullptr, 0, nullptr);
    } else {
        port_level_set_edit(level, g_ed.image().data(), g_ed.image().size(), "the level editor");
    }
    g_ed.markApplied();
}


void applyEdits() {
    if (!g_ed.isOpen()) return;
    giveEditsToGame();
    g_editMsg = "Applied: the level is loaded again with the edits.";
    host_log("live: editor: apply: level %d reloaded with the edited file (SHA-1 %s, %u steps, %u bytes)\n",
             g_ed.level(), g_ed.imageSha1().c_str(), unsigned(g_ed.steps().size()), unsigned(g_ed.image().size()));
    requestLevel(g_ed.level());
    g_resetCam = false;   /* (the camera stays where it was) */
    g_applyStart = std::chrono::steady_clock::now();
    g_applyPending = true;
}

void playEdits() {
    if (!g_ed.isOpen()) return;
    giveEditsToGame();
    g_testRun = true;
    host_saves_block("a test run of an edited level");
    overlayMarker("TEST RUN of an edited level - saving is off");
    g_playAfterLoad = true;
    host_log("live: editor: play from here: level %d, edited file SHA-1 %s; saving off for this run\n", g_ed.level(),
             g_ed.imageSha1().c_str());
    requestLevel(g_ed.level());
}

bool saveMod(const std::string &name, bool enable) {
    std::string clean;
    for (char c : name)
        if (isalnum((unsigned char) c) || c == '_' || c == '-' || c == '.' || c == ' ') clean += c;
    while (!clean.empty() && (clean.back() == ' ' || clean.back() == '.')) clean.pop_back();
    if (clean.empty()) {
        g_editMsg = "Give the mod a name (letters, digits, - and _).";
        return false;
    }
    const std::string dir = modsDir();
    CreateDirectoryW(wide(dir).c_str(), nullptr);
    const std::string path = dir + clean + ".bcmod";
    char desc[200];
    LgEdit ge;
    bool geo = false;
    if (g_ed.geometry(ge)) geo = !lg_edit_empty(&ge), lg_edit_free(&ge);
    const size_t ntex = g_ed.textures().size();
    snprintf(desc, sizeof desc, "level %d, %u record change%s%s%s, made with bc.exe's level editor", g_ed.level(),
             unsigned(g_ed.baseChanges().size() + g_ed.changes().size()),
             g_ed.baseChanges().size() + g_ed.changes().size() == 1 ? "" : "s", geo ? ", terrain" : "",
             ntex ? (ntex == 1 ? ", a texture slot" : ", texture slots") : "");
    const std::string text = g_ed.modText(clean, desc);
    if (text.empty() || host_write_file(path.c_str(), text.data(), unsigned(text.size())) != 0) {
        g_editMsg = "Can't write " + path;
        host_log("live: editor: can't write %s\n", path.c_str());
        return false;
    }
    g_savedSha = g_ed.imageSha1();
    g_savedPath = path;
    g_editMsg = "Saved " + path;
    host_log("live: editor: saved %s (%u bytes; level %d, edited file SHA-1 %s)\n", path.c_str(),
             unsigned(text.size()), g_ed.level(), g_savedSha.c_str());
    if (enable && g_env.cfg != nullptr) {
        Config &c = *g_env.cfg;
        if (std::find(c.mods.begin(), c.mods.end(), clean) == c.mods.end()) c.mods.push_back(clean);
        if (configSave(c, {{"mods", "enabled"}}))
            g_editMsg += " (enabled in bc.ini: used from the next start)";
        else
            g_editMsg += " (not enabled: no bc.ini in this run)";
    }
    return true;
}

/* edits not saved in a mod file when the viewer is left: kept in mods\autosave_LNN.bcmod */
void autosave() {
    if (!g_ed.isOpen() || g_ed.steps().empty() || g_ed.imageSha1() == g_savedSha) return;
    char name[32];
    snprintf(name, sizeof name, "autosave_L%02d", g_ed.level());
    const std::string msg = g_editMsg;
    if (saveMod(name, false)) host_log("live: editor: unsaved edits kept in %s\n", g_savedPath.c_str());
    g_editMsg = msg;
}

/* ---- the gizmo -------------------------------------------------------------------------- */

bool projectPoint(const Vec &p, ImVec2 &s, double *depth) {
    double x, y;
    if (!g_ov.ok || !viewer::project(g_ov.view, g_ov.proj, p, g_ov.rx, g_ov.ry, g_ov.rw, g_ov.rh, x, y, depth))
        return false;
    s = ImVec2(float(x), float(y));
    return true;
}

/* the selected record's position (the drag's while dragging) */
bool selPosition(Vec &p, int &kind, int &index) {
    if (!selEditable(kind, index)) return false;
    const Record &r = g_recs[g_sel];
    if (!r.hasPos) return false;
    p = g_drag.mode != 0 && g_drag.kind == kind && g_drag.index == index ? g_drag.cur : Vec{r.x, r.y, r.z};
    return true;
}

Gizmo gizmo() {
    Gizmo g;
    int kind, index;
    if (!selPosition(g.p, kind, index)) return g;
    double depth;
    if (!projectPoint(g.p, g.c, &depth) || depth < 1.0) return g;
    g.len = 70.0 * depth / (std::max(1e-6, std::fabs(g_ov.proj[1][1])) * g_ov.rh * 0.5);
    const Vec dirs[3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
    for (int a = 0; a < 3; a++) {
        if (a == 1 && !kindHasY(kind)) continue;
        g.axis[a] = projectPoint({g.p.x + dirs[a].x * g.len, g.p.y + dirs[a].y * g.len, g.p.z + dirs[a].z * g.len},
                                 g.tip[a]);
    }
    if (kindHasHeading(kind))
        for (int i = 0; i <= 48; i++) {
            const double t = i * 2.0 * 3.14159265358979 / 48;
            ImVec2 s;
            if (!projectPoint({g.p.x + std::sin(t) * g.len * 1.35, g.p.y, g.p.z + std::cos(t) * g.len * 1.35}, s)) {
                g.ring.clear();
                break;
            }
            g.ring.push_back(s);
        }
    g.ok = true;
    return g;
}

float segDist(ImVec2 p, ImVec2 a, ImVec2 b) {
    const float dx = b.x - a.x, dy = b.y - a.y, l2 = dx * dx + dy * dy;
    float t = l2 > 0 ? ((p.x - a.x) * dx + (p.y - a.y) * dy) / l2 : 0;
    t = std::max(0.0f, std::min(1.0f, t));
    return std::hypot(p.x - (a.x + t * dx), p.y - (a.y + t * dy));
}

/* the gizmo part under a window point: 0 none, else Drag::mode */
int gizmoHit(float x, float y) {
    const Gizmo g = gizmo();
    if (!g.ok) return 0;
    const ImVec2 p(x, y);
    if (std::hypot(x - g.c.x, y - g.c.y) <= 11) return 1;
    int best = 0;
    float bd = 8;
    for (int a = 0; a < 3; a++)
        if (g.axis[a]) {
            const float d = segDist(p, g.c, g.tip[a]);
            if (d < bd) bd = d, best = 2 + a;
        }
    if (best) return best;
    for (size_t i = 1; i < g.ring.size(); i++)
        if (segDist(p, g.ring[i - 1], g.ring[i]) < 7) return 5;
    return 0;
}

bool screenRay(float sx, float sy, edit::V3 &o, edit::V3 &d) {
    return g_ov.ok && edit::pickRay(g_ov.view, g_ov.proj, g_ov.rx, g_ov.ry, g_ov.rw, g_ov.rh, sx, sy, o, d);
}

bool beginDrag(int mode, float sx, float sy) {
    int kind, index;
    Vec p;
    edit::V3 o, d;
    if (mode == 0 || !selPosition(p, kind, index) || !screenRay(sx, sy, o, d)) return false;
    g_drag = Drag();
    g_drag.mode = mode, g_drag.kind = kind, g_drag.index = index;
    g_drag.start = g_drag.cur = p;
    g_drag.startHeading = g_drag.heading =
        kindHasHeading(kind) ? edit::degreesFromHeading(g_ed.get(kind, index, "heading")) : 0;
    double gx, gz, t;
    if (mode == 1 || mode == 5) {
        if (!edit::rayPlaneY(o, d, p.y, gx, gz)) return g_drag = Drag(), false;
        g_drag.grabX = p.x - gx, g_drag.grabZ = p.z - gz;
        g_drag.a0 = std::atan2(gx - p.x, gz - p.z) * 180.0 / 3.14159265358979;
    } else {
        const edit::V3 a = {mode == 2 ? 1.0 : 0.0, mode == 3 ? 1.0 : 0.0, mode == 4 ? 1.0 : 0.0};
        if (!edit::rayAxis(o, d, {p.x, p.y, p.z}, a, t)) return g_drag = Drag(), false;
        g_drag.t0 = t;
    }
    return true;
}

void updateDrag(float sx, float sy) {
    edit::V3 o, d;
    if (g_drag.mode == 0 || !screenRay(sx, sy, o, d)) return;
    const int kind = g_drag.kind;
    Vec n = g_drag.start;
    double gx, gz, t;
    switch (g_drag.mode) {
    case 1:
        if (!edit::rayPlaneY(o, d, g_drag.start.y, gx, gz)) return;
        n.x = double(edit::snapTo(gx + g_drag.grabX, g_eopt.grid));
        n.z = double(edit::snapTo(gz + g_drag.grabZ, g_eopt.grid));
        if (kindHasY(kind) && g_eopt.snapGround) n.y = std::round(groundAt(n.x, n.z, g_drag.start.y));
        break;
    case 2: case 3: case 4: {
        const edit::V3 a = {g_drag.mode == 2 ? 1.0 : 0.0, g_drag.mode == 3 ? 1.0 : 0.0, g_drag.mode == 4 ? 1.0 : 0.0};
        if (!edit::rayAxis(o, d, {g_drag.start.x, g_drag.start.y, g_drag.start.z}, a, t)) return;
        const double dv = t - g_drag.t0;
        if (g_drag.mode == 2) n.x = double(edit::snapTo(g_drag.start.x + dv, g_eopt.grid));
        if (g_drag.mode == 3) n.y = std::round(g_drag.start.y + dv);
        if (g_drag.mode == 4) n.z = double(edit::snapTo(g_drag.start.z + dv, g_eopt.grid));
        break;
    }
    case 5: {
        if (!edit::rayPlaneY(o, d, g_drag.start.y, gx, gz)) return;
        const double a = std::atan2(gx - g_drag.start.x, gz - g_drag.start.z) * 180.0 / 3.14159265358979;
        double h = g_drag.startHeading + (a - g_drag.a0);
        if (g_eopt.grid > 0) h = std::round(h / 15.0) * 15.0;   /* (with a grid: 15-degree steps) */
        g_drag.heading = edit::degreesFromHeading(edit::headingFromDegrees(h));
        break;
    }
    default:
        return;
    }
    n.x = double(clampField(kind, "x", n.x)), n.z = double(clampField(kind, "z", n.z));
    if (kindHasY(kind)) n.y = double(clampField(kind, "y", n.y));
    g_drag.cur = n;
    g_drag.moved = true;
}

void endDrag() {
    if (g_drag.mode == 0) return;
    const Drag d = g_drag;
    g_drag = Drag();
    if (!d.moved) return;
    std::vector<edit::FieldValue> f;
    if (d.mode == 5) {
        const int h = edit::headingFromDegrees(d.heading);
        if (h != g_ed.get(d.kind, d.index, "heading")) f.push_back({lm_field_index(d.kind, "heading"), h});
    } else {
        const long x = std::lround(d.cur.x), y = std::lround(d.cur.y), z = std::lround(d.cur.z);
        if (x != g_ed.get(d.kind, d.index, "x")) f.push_back({lm_field_index(d.kind, "x"), x});
        if (kindHasY(d.kind) && y != g_ed.get(d.kind, d.index, "y")) f.push_back({lm_field_index(d.kind, "y"), y});
        if (z != g_ed.get(d.kind, d.index, "z")) f.push_back({lm_field_index(d.kind, "z"), z});
    }
    if (!f.empty()) pushEdit(edit::makeSet(d.kind, d.index, f), d.kind, d.index);
}

void drawGizmo(ImDrawList *dl) {
    const Gizmo g = gizmo();
    if (!g.ok) return;
    const ImU32 cols[3] = {IM_COL32(240, 70, 70, 255), IM_COL32(80, 230, 80, 255), IM_COL32(80, 140, 255, 255)};
    const int active = g_drag.mode;
    if (!g.ring.empty()) {
        const ImU32 rc = active == 5 ? IM_COL32(255, 255, 120, 255) : IM_COL32(255, 220, 80, 200);
        dl->AddPolyline(g.ring.data(), int(g.ring.size()), rc, 0, active == 5 ? 3.5f : 2.0f);
        /* the heading's mark on the ring */
        const double h = (active == 5 ? g_drag.heading
                                      : edit::degreesFromHeading(g_ed.get(g_recs[g_sel].kind, g_recs[g_sel].index,
                                                                          "heading"))) * 3.14159265358979 / 180.0;
        ImVec2 s;
        if (projectPoint({g.p.x + std::sin(h) * g.len * 1.35, g.p.y, g.p.z + std::cos(h) * g.len * 1.35}, s)) {
            dl->AddLine(g.c, s, rc, 2.0f);
            dl->AddCircleFilled(s, 6.0f, rc, 12);
        }
    }
    for (int a = 0; a < 3; a++) {
        if (!g.axis[a]) continue;
        const bool on = active == 2 + a;
        dl->AddLine(g.c, g.tip[a], cols[a], on ? 5.0f : 3.0f);
        const float dx = g.tip[a].x - g.c.x, dy = g.tip[a].y - g.c.y, l = std::max(1.0f, std::hypot(dx, dy));
        const ImVec2 u(dx / l, dy / l), nrm(-u.y, u.x);
        dl->AddTriangleFilled(ImVec2(g.tip[a].x + u.x * 12, g.tip[a].y + u.y * 12),
                              ImVec2(g.tip[a].x + nrm.x * 6, g.tip[a].y + nrm.y * 6),
                              ImVec2(g.tip[a].x - nrm.x * 6, g.tip[a].y - nrm.y * 6), cols[a]);
        static const char *const names[3] = {"x", "y", "z"};
        dl->AddText(ImVec2(g.tip[a].x + u.x * 16 - 3, g.tip[a].y + u.y * 16 - 7), cols[a], names[a]);
    }
    const ImU32 cc = active == 1 ? IM_COL32(255, 255, 255, 255) : IM_COL32(255, 255, 255, 200);
    dl->AddRectFilled(ImVec2(g.c.x - 6, g.c.y - 6), ImVec2(g.c.x + 6, g.c.y + 6), IM_COL32(0, 0, 0, 140));
    dl->AddRect(ImVec2(g.c.x - 7, g.c.y - 7), ImVec2(g.c.x + 7, g.c.y + 7), cc, 0, 0, 2.0f);
    if (active != 0) {
        char b[96];
        if (active == 5) snprintf(b, sizeof b, "heading %.1f", g_drag.heading);
        else snprintf(b, sizeof b, "%.0f, %.0f, %.0f", g_drag.cur.x, g_drag.cur.y, g_drag.cur.z);
        dl->AddText(ImVec2(g.c.x + 12, g.c.y + 10), IM_COL32(255, 255, 255, 255), b);
    }
}
}  // namespace vw
}  // namespace live
