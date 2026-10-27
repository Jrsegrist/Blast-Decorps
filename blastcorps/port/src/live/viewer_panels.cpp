/* The level viewer's overlays and panels (viewer_internal.h): markers, labels, the carrier's route, height
 * zones and collision outlines over the picture; the model view; the Inspector, the Level viewer panel, the
 * editor's Changes panel and the status line (Dear ImGui, drawn over the picture by live_menu.cpp's overlay). */
#include <windows.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "imgui/imgui.h"
#include "imgui/imgui_internal.h"   /* (FindWindowByName: the controller's panel) */

#include "live_menu.h"
#include "viewer_internal.h"

extern "C" {
#include "plat_host.h"
}

namespace live {
namespace vw {

/* ---- the panels --------------------------------------------------------------------- */
struct Marker {
    int rec;
    float x, y, r;
    double depth;
};
std::vector<Marker> g_markers;
std::string g_status;
bool g_cleanShot;                 /* this tick's picture is a clean screenshot: no panels, no overlays */
bool g_overlayShot;               /* ... with the overlays, no panels */

ImU32 kindColor(int k) {
    switch (k) {
    case viewer::REC_VEHICLE: return IM_COL32(80, 200, 255, 255);
    case viewer::REC_CARRIER: return IM_COL32(255, 80, 80, 255);
    case viewer::REC_OBJECT: return IM_COL32(255, 210, 80, 255);
    case viewer::REC_RDU: return IM_COL32(120, 255, 120, 255);
    case viewer::REC_TNT: return IM_COL32(255, 120, 40, 255);
    case viewer::REC_CRATE: return IM_COL32(200, 160, 255, 255);
    case viewer::REC_COMMPT: return IM_COL32(80, 255, 230, 255);
    case viewer::REC_BLOCK: case viewer::REC_BLOCKSLOT: return IM_COL32(230, 230, 230, 255);
    case viewer::REC_GOBJECT: return IM_COL32(255, 170, 120, 255);
    case viewer::REC_GVEHICLE: return IM_COL32(120, 160, 255, 255);
    default: return IM_COL32(200, 200, 200, 255);
    }
}

/* the picture rectangle and the projection the overlays use */
bool overlayProjection(double proj[4][4], double &rx, double &ry, double &rw, double &rh) {
    {
        std::lock_guard<std::mutex> lock(g_vsMutex);
        if (g_vs.haveProj) memcpy(proj, g_vs.proj, sizeof g_vs.proj);
        else memcpy(proj, g_fz.proj, sizeof g_fz.proj);
    }
    if (!(std::fabs(proj[0][0]) > 1e-9)) return false;
    const ImGuiIO &io = ImGui::GetIO();
    viewer::pictureRect(io.DisplaySize.x, io.DisplaySize.y, std::fabs(proj[1][1] / proj[0][0]), rx, ry, rw, rh);
    return true;
}

void drawOverlays() {
    g_markers.clear();
    g_ov.ok = false;
    if (!g_fz.level) return;
    double view[4][4], proj[4][4], rx, ry, rw, rh;
    currentView(view);
    if (!overlayProjection(proj, rx, ry, rw, rh)) return;
    g_ov.ok = true;
    memcpy(g_ov.view, view, sizeof view);
    memcpy(g_ov.proj, proj, sizeof proj);
    g_ov.rx = rx, g_ov.ry = ry, g_ov.rw = rw, g_ov.rh = rh;
    ImDrawList *dl = ImGui::GetBackgroundDrawList();
    const float font = uiFontPx();
    auto proj2 = [&](const Vec &p, float &sx, float &sy, double *depth) {
        double x, y;
        if (!viewer::project(view, proj, p, rx, ry, rw, rh, x, y, depth)) return false;
        sx = float(x), sy = float(y);
        return true;
    };
    const Vec eye = g_mv.on ? Vec() : g_cam.pos;
    /* collision outlines near the camera */
    if (g_opt.collision && !g_mv.on) {
        unsigned n = 0;
        for (const Tri &t : g_tris) {
            const double cx = (t.v[0] + t.v[3] + t.v[6]) / 3.0, cz = (t.v[2] + t.v[5] + t.v[8]) / 3.0;
            if ((cx - eye.x) * (cx - eye.x) + (cz - eye.z) * (cz - eye.z) > 900.0 * 900.0) continue;
            float sx[3], sy[3];
            bool ok = true;
            for (int k = 0; k < 3 && ok; k++) ok = proj2({t.v[3 * k], t.v[3 * k + 1], t.v[3 * k + 2]}, sx[k], sy[k], nullptr);
            if (!ok) continue;
            const ImU32 col = t.src == 0 ? IM_COL32(255, 90, 90, 150) : t.src == 1 ? IM_COL32(255, 160, 60, 150)
                                                                                   : IM_COL32(90, 200, 255, 150);
            dl->AddTriangle(ImVec2(sx[0], sy[0]), ImVec2(sx[1], sy[1]), ImVec2(sx[2], sy[2]), col, 1.0f);
            if (++n >= 6000) break;
        }
    }
    if (!g_opt.markers || g_mv.on) {
        if (terrainMode()) drawTerrainOverlay(dl);
        return;
    }
    /* the carrier's route */
    for (const Record &r : g_recs)
        if (r.kind == viewer::REC_CARRIER && r.type != 0 && g_opt.kinds[viewer::REC_CARRIER]) {
            double dx, dz;
            viewer::headingDir(r.heading, dx, dz);
            const viewer::Field *len = nullptr;
            for (const viewer::Field &f : r.fields)
                if (f.name == "route length") len = &f;
            const double L = len ? atof(len->value.c_str()) : 0;
            float px = 0, py = 0;
            bool have = false;
            for (int i = 0; i <= 40; i++) {
                const double x = r.x + dx * L * i / 40.0, z = r.z + dz * L * i / 40.0;
                float sx, sy;
                const bool ok = proj2({x, groundAt(x, z, r.y) + 3, z}, sx, sy, nullptr);
                if (ok && have) dl->AddLine(ImVec2(px, py), ImVec2(sx, sy), IM_COL32(255, 60, 60, 220), 3.0f);
                have = ok, px = sx, py = sy;
            }
        }
    /* zones */
    if (g_opt.zones)
        for (const Record &r : g_recs) {
            if (!r.hasBox) continue;
            const Vec c[4] = {{r.x0, r.y, r.z0}, {r.x1, r.y, r.z0}, {r.x1, r.y, r.z1}, {r.x0, r.y, r.z1}};
            float sx[4], sy[4];
            bool ok = true;
            for (int k = 0; k < 4 && ok; k++) ok = proj2(c[k], sx[k], sy[k], nullptr);
            if (!ok) continue;
            for (int k = 0; k < 4; k++)
                dl->AddLine(ImVec2(sx[k], sy[k]), ImVec2(sx[(k + 1) & 3], sy[(k + 1) & 3]),
                            r.kind == viewer::REC_CAMZONE ? IM_COL32(255, 255, 0, 160) : IM_COL32(0, 255, 255, 160), 1.5f);
        }
    const bool ed = editing();
    for (size_t i = 0; i < g_recs.size(); i++) {
        const Record &r = g_recs[i];
        if (!r.hasPos || r.hasBox || !g_opt.kinds[r.kind]) continue;
        /* the editor: a record being dragged is drawn where it goes; edited records stand out */
        const bool dragged = ed && g_drag.mode != 0 && r.kind == g_drag.kind && r.index == g_drag.index;
        const Vec at = dragged ? g_drag.cur : Vec{r.x, r.y, r.z};
        const double heading = dragged && g_drag.mode == 5 ? g_drag.heading : r.heading;
        const int mark = ed && isEditable(r.kind) ? g_ed.edited(r.kind, r.index) : 0;
        float sx, sy;
        double depth;
        if (!proj2(at, sx, sy, &depth) || depth < 1.0) continue;
        if (sx < -50 || sy < -50 || sx > ImGui::GetIO().DisplaySize.x + 50 || sy > ImGui::GetIO().DisplaySize.y + 50)
            continue;
        const ImU32 col = mark == 1 ? IM_COL32(255, 70, 255, 255) : mark == 2 ? IM_COL32(255, 150, 40, 255)
                                                                             : kindColor(r.kind);
        const float rad = std::max(4.0f, std::min(30.0f, float(r.radius * proj[1][1] / depth * rh * 0.5)));
        const bool sel = int(i) == g_sel;
        dl->AddCircle(ImVec2(sx, sy), rad, col, 16, sel ? 3.0f : mark ? 2.5f : 1.5f);
        if (mark) dl->AddCircle(ImVec2(sx, sy), rad + 4, col, 16, 1.0f);
        if (r.hasHeading) {
            double dx, dz;
            viewer::headingDir(heading, dx, dz);
            float hx, hy;
            if (proj2({at.x + dx * r.radius * 1.5, at.y, at.z + dz * r.radius * 1.5}, hx, hy, nullptr))
                dl->AddLine(ImVec2(sx, sy), ImVec2(hx, hy), col, 2.0f);
        }
        /* (labels near the camera only: the RDUs, lined up by the dozen, nearer still) */
        const double labelDist = r.kind == viewer::REC_RDU ? g_opt.labelDist * 0.4 : g_opt.labelDist;
        if (g_opt.labels && (sel || mark || depth < labelDist)) {
            const std::string text = mark ? r.label + (mark == 1 ? " *" : " (mod)") : r.label;
            const ImVec2 tp(sx + rad + 3, sy - font * 0.4f);
            dl->AddText(ImVec2(tp.x + 1, tp.y + 1), IM_COL32(0, 0, 0, 200), text.c_str());
            dl->AddText(tp, col, text.c_str());
        }
        g_markers.push_back({int(i), sx, sy, rad, depth});
    }
    if (ed && !terrainMode()) drawGizmo(dl);
    if (terrainMode()) drawTerrainOverlay(dl);
}

void selectAt(float x, float y) {
    /* the marker under the point (the nearest to the camera of those within a few pixels of it) */
    int best = -1;
    double bestScore = 1e30;
    for (const Marker &m : g_markers) {
        const double d = std::hypot(m.x - x, m.y - y);
        if (d > m.r + 10) continue;
        const double score = d + m.depth * 1e-4;
        if (score < bestScore) {
            bestScore = score;
            best = m.rec;
        }
    }
    g_sel = best;
    if (best >= 0) host_log("live: viewer: selected %s\n", g_recs[best].title.c_str());
}

void openModel(uint32_t kind, uint32_t index, int rec, const std::string &title) {
    g_mv = ModelView();
    g_mv.on = true;
    g_mv.kind = kind;
    g_mv.index = index;
    g_mv.rec = rec;
    g_mv.title = title;
    /* a first centre from the record; the replay measures the model's drawing */
    if (rec >= 0 && g_recs[rec].hasPos) {
        g_mv.centre = {g_recs[rec].x, g_recs[rec].y, g_recs[rec].z};
        g_mv.radius = std::max(20.0, g_recs[rec].radius);
    } else {
        g_mv.centre = g_cam.pos;
        g_mv.radius = 50;
    }
    g_mv.dist = g_mv.radius * 3.0;
    g_measure = true;
    host_log("live: viewer: model view of %s (tag kind %u, index %u)\n", title.c_str(), kind, index);
}

/* the model a record stands for */
bool modelFor(int rec) {
    if (rec < 0) return false;
    const Record &r = g_recs[rec];
    if (r.kind == viewer::REC_GVEHICLE) {
        openModel(K_OBJ, uint32_t(r.index), rec, r.title);
        return true;
    }
    if (r.kind == viewer::REC_GOBJECT) {
        openModel(K_BUILDING, uint32_t(r.index), rec, r.title);
        return true;
    }
    if (r.kind == viewer::REC_VEHICLE || r.kind == viewer::REC_CARRIER) {
        int v = -1;
        if (r.kind == viewer::REC_CARRIER) {
            for (const Record &x : g_recs)
                if (x.kind == viewer::REC_GVEHICLE && x.type == 0xFF) v = x.index;
        } else {
            v = vehicleOf(r);
        }
        if (v < 0) return false;
        openModel(K_OBJ, uint32_t(v), rec,
                  r.title + " (" + viewer::vehicleModelFile(r.kind == viewer::REC_CARRIER ? 0xFF : r.type) + ")");
        return true;
    }
    if (r.kind == viewer::REC_OBJECT) {
        const int g = findRecord(viewer::REC_GOBJECT, r.index);
        if (g < 0) return false;
        openModel(K_BUILDING, uint32_t(r.index), rec, r.title);
        return true;
    }
    return false;
}

std::string shotPath(const std::string &name) {
    std::string dir = g_env.shotDir;
    if (dir.empty()) {
        dir = g_env.cfg != nullptr ? g_env.cfg->dir + "screenshots" : "screenshots";
        CreateDirectoryW(wide(dir).c_str(), nullptr);
    }
    return dir + "\\" + name + ".png";
}

std::string autoShotName(bool overlays) {
    char b[96];
    SYSTEMTIME t;
    GetLocalTime(&t);
    snprintf(b, sizeof b, "viewer_L%02d_%04u%02u%02u_%02u%02u%02u%s", g_fz.levelNo, t.wYear, t.wMonth, t.wDay, t.wHour,
             t.wMinute, t.wSecond, overlays ? "_overlays" : "");
    return b;
}

void inspector() {
    const ImGuiIO &io = ImGui::GetIO();
    const float f = uiFontPx() * kPanelScale;
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x - f * 0.5f, f * 0.5f), ImGuiCond_FirstUseEver, ImVec2(1, 0));
    /* (above the editor's Changes panel, bottom right) */
    ImGui::SetNextWindowSize(ImVec2(f * 21, std::min(io.DisplaySize.y * 0.54f, f * 34)), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowBgAlpha(0.85f);
    if (!ImGui::Begin("Inspector", nullptr, ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing)) {
        ImGui::End();
        return;
    }
    ImGui::SetWindowFontScale(kPanelScale);
    if (g_sel < 0 || g_sel >= int(g_recs.size())) {
        ImGui::TextWrapped("Click a marker (or pick a record in the Records list) to see it here.");
        ImGui::End();
        return;
    }
    const Record &r = g_recs[g_sel];
    ImGui::TextWrapped("%s", r.title.c_str());
    ImGui::TextDisabled("%s, at 0x%06X%s", viewer::recKindName(r.kind), r.offset,
                        r.kind >= viewer::REC_GOBJECT ? " (RDRAM)" : " in the level file");
    if (!r.model.empty()) ImGui::Text("Model: %s", r.model.c_str());
    if (r.hasPos) ImGui::Text("Position: %.0f, %.0f, %.0f", r.x, r.y, r.z);
    if (r.hasHeading) ImGui::Text("Heading: %.1f degrees", r.heading);
    if (ImGui::Button("Look at it") && r.hasPos) {
        g_mv.on = false;
        g_cam.lookAt({r.x, r.y, r.z}, std::max(120.0, r.radius * 5));
    }
    ImGui::SameLine();
    if (ImGui::Button("Model view")) modelFor(g_sel);
    int ek, ei;
    if (selEditable(ek, ei)) {
        /* the editor: the record's fields as numbers (Enter or leaving the field makes the change) */
        const int mark = g_ed.edited(ek, ei);
        ImGui::Separator();
        ImGui::TextColored(mark == 1 ? ImVec4(1, 0.3f, 1, 1) : mark == 2 ? ImVec4(1, 0.6f, 0.15f, 1) : ImVec4(0.6f, 1, 0.6f, 1),
                           "%s", mark == 1 ? "Edited (not in the ROM's file)" : mark == 2 ? "Changed by the loaded mod"
                                                                                       : "Editable: as the ROM has it");
        ImGui::PushItemWidth(f * 7);
        for (int fi = 0; fi < lm_field_count(ek); fi++) {
            long v = 0, lo, hi;
            g_ed.get(ek, ei, fi, v);
            lm_field_range(ek, fi, &lo, &hi);
            int iv = int(v);
            ImGui::PushID(fi);
            /* (typed: the change when the field is left; its - / + buttons, by mouse or controller: at once - the
             * buttons' own ids are the active ones, so the field never reads as "deactivated after an edit") */
            const bool stepped = ImGui::InputInt(lm_field_name(ek, fi), &iv, 1, 16) && !ImGui::GetIO().WantTextInput;
            if (stepped || ImGui::IsItemDeactivatedAfterEdit()) {
                iv = int(std::max(lo, std::min(hi, long(iv))));
                /* (- / + held or pressed again and again: one undo step for the burst) */
                if (iv != v && stepped) g_after.push_back([=] { pushEditBurst(ek, ei, fi, iv); });
                else if (iv != v) g_after.push_back([=] { pushEdit(edit::makeSet(ek, ei, {{fi, iv}}), ek, ei); });
            }
            if (!strcmp(lm_field_name(ek, fi), "heading")) {
                ImGui::SameLine();
                ImGui::TextDisabled("%.1f deg", edit::degreesFromHeading(v));
            }
            ImGui::PopID();
        }
        ImGui::PopItemWidth();
        if (ImGui::Button("Duplicate (Ctrl+D)")) g_after.push_back(duplicateSel);
        ImGui::SameLine();
        ImGui::BeginDisabled(ek == viewer::REC_CARRIER);
        if (ImGui::Button("Delete (Del)")) g_after.push_back(deleteSel);
        ImGui::EndDisabled();
        if (kindHasY(ek)) {
            ImGui::SameLine();
            if (ImGui::Button("On the ground (G)")) g_after.push_back(groundSel);
        }
        if (kindHasHeading(ek)) {
            if (ImGui::Button("-15 deg")) g_after.push_back([] { rotateSel(-15); });
            ImGui::SameLine();
            if (ImGui::Button("+15 deg (R)")) g_after.push_back([] { rotateSel(15); });
        }
        ImGui::TextDisabled("%d of at most %d %s records", g_ed.count(ek), lm_kind_max(ek), lm_kind_key(ek));
        ImGui::Separator();
        /* (the rest below is the record as it is now, from the editor's copy) */
        if (!ImGui::CollapsingHeader("As the viewer reads it")) {
            ImGui::End();
            return;
        }
    }
    if (ImGui::BeginTable("fields", 2, ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_RowBg)) {
        for (const viewer::Field &fl : r.fields) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(fl.name.c_str());
            ImGui::TableNextColumn();
            ImGui::TextWrapped("%s", fl.value.c_str());
        }
        ImGui::EndTable();
    }
    /* the runtime record the placement or spawn became */
    int other = -1;
    if (r.kind == viewer::REC_OBJECT) other = findRecord(viewer::REC_GOBJECT, r.index);
    if (r.kind == viewer::REC_VEHICLE) {
        const int v = vehicleOf(r);
        if (v >= 0) other = findRecord(viewer::REC_GVEHICLE, v);
    }
    if (other >= 0 && ImGui::CollapsingHeader("Its runtime record")) {
        const Record &o = g_recs[other];
        ImGui::TextWrapped("%s", o.title.c_str());
        for (const viewer::Field &fl : o.fields) ImGui::TextWrapped("%s: %s", fl.name.c_str(), fl.value.c_str());
    }
    if (ImGui::CollapsingHeader("Raw bytes", ImGuiTreeNodeFlags_DefaultOpen))
        ImGui::TextUnformatted(viewer::hexDump(r.raw).c_str());
    ImGui::End();
}

void mainPanel() {
    const float f = uiFontPx() * kPanelScale;
    ImGui::SetNextWindowPos(ImVec2(f * 0.5f, f * 0.5f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(f * 21, std::min(ImGui::GetIO().DisplaySize.y * 0.9f, f * 40)),
                             ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowBgAlpha(0.85f);
    if (!ImGui::Begin("Level viewer", nullptr, ImGuiWindowFlags_NoSavedSettings)) {
        ImGui::End();
        return;
    }
    ImGui::SetWindowFontScale(kPanelScale);
    ImGui::TextWrapped("%s", g_status.c_str());
    /* (Leave isn't the first item - a controller's A right after Start lands on the first - and asks first) */
    if (ImGui::Button("Player start")) jumpToStart();
    ImGui::SameLine();
    if (ImGui::Button("Overview")) cameraTop();
    ImGui::SameLine();
    if (ImGui::Button(g_loadedLevel ? "Leave..." : "Leave the viewer...")) ImGui::OpenPopup("Leave the viewer?");
    if (ImGui::BeginPopupModal("Leave the viewer?", nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings)) {
        ImGui::SetWindowFontScale(kPanelScale);
        ImGui::TextUnformatted(g_loadedLevel ? "Leaving restarts the game (the viewer loaded a level)."
                                             : "The game goes on from where the viewer held it.");
        if (editing() && g_ed.changed() && g_savedSha != g_ed.imageSha1())
            ImGui::TextUnformatted("Unsaved edits are kept in the mods folder (autosave_Lnn.bcmod).");
        if (ImGui::Button("Stay")) ImGui::CloseCurrentPopup();   /* (the first item: what A presses at once) */
        ImGui::SameLine();
        if (ImGui::Button("Leave")) {
            g_leave = true;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
    if (ImGui::Button("Screenshot (F12)")) g_cleanShot = true;
    ImGui::SameLine();
    if (ImGui::Button("With overlays (Shift+F12)")) g_overlayShot = true;
    if (ImGui::CollapsingHeader("Level", ImGuiTreeNodeFlags_DefaultOpen)) {
        static int pick = -1;
        if (pick < 0) pick = std::max(0, g_fz.levelNo);
        const std::string cur = levelLabel(pick);
        if (ImGui::BeginCombo("##level", cur.c_str())) {
            for (int i = 0; i < 60; i++)
                if (ImGui::Selectable(levelLabel(i).c_str(), i == pick)) pick = i;
            ImGui::EndCombo();
        }
        ImGui::SameLine();
        const bool fe = rd<uint8_t>(kFrontEnd) != 0;
        ImGui::BeginDisabled(fe);
        if (ImGui::Button("Load")) requestLevel(pick);
        ImGui::EndDisabled();
        ImGui::TextDisabled(fe ? "Levels load from a level or the attract demo (not the menus)."
                               : "Loaded by the game's own level set-up.");
        if (!g_fz.level)
            ImGui::TextWrapped("This picture isn't a level's (the menus, the globe): it is shown as the game drew it. "
                               "The free camera, the overlays and the model view work in levels.");
    }
    if (!g_fz.level) {
        ImGui::End();
        return;
    }
    if (ImGui::CollapsingHeader("Camera", ImGuiTreeNodeFlags_DefaultOpen)) {
        float sp = float(g_opt.speed);
        if (ImGui::SliderFloat("Speed", &sp, 50.0f, 8000.0f, "%.0f units/s", ImGuiSliderFlags_Logarithmic))
            g_opt.speed = sp;
        bool farOut = g_opt.farOut;
        if (ImGui::Checkbox("Far draw distance", &farOut)) g_opt.farOut = farOut;
        ImGui::SameLine();
        ImGui::TextDisabled("(the game's: 10000)");
        ImGui::TextDisabled("WASD move, Q/E down/up, Shift fast, Ctrl slow, right mouse button: look, wheel: speed");
    }
    if (ImGui::CollapsingHeader("Editor", ImGuiTreeNodeFlags_DefaultOpen)) {
        bool on = g_editMode;
        if (ImGui::Checkbox("Edit the level (Ctrl+E)", &on)) g_after.push_back([on] { setEditMode(on); });
        if (g_editMode) {
            /* ("##mode": the "Records" radio button had the ID of the "Records" section header below, so a
             * controller's D-pad, moving to it, landed on the header instead) */
            if (ImGui::RadioButton("Records##mode", !g_tv.on)) g_tv.on = false;
            ImGui::SameLine();
            ImGui::BeginDisabled(!g_ed.terrainOk());
            if (ImGui::RadioButton("Terrain (Ctrl+T)##mode", g_tv.on)) g_tv.on = true;
            ImGui::EndDisabled();
        }
        if (g_editMode && g_tv.on) {
            ImGui::TextDisabled("Shape the ground with the brushes, make ramps, split and move triangles, paint them "
                                "with the level's textures, put your own pictures in texture slots (the Terrain panel).");
        } else if (g_editMode) {
            ImGui::Checkbox("Moved records sit on the ground", &g_eopt.snapGround);
            static const int grids[] = {0, 8, 16, 32, 64, 128};
            int gi = 0;
            for (int i = 0; i < 6; i++)
                if (grids[i] == g_eopt.grid) gi = i;
            ImGui::SetNextItemWidth(f * 6);
            if (ImGui::Combo("Grid (x, z)", &gi, "none\0 8\0 16\0 32\0 64\0 128\0")) g_eopt.grid = grids[gi];
            ImGui::TextDisabled("Click a marker, drag its square (on the ground), an arrow (along x, y or z) or the "
                                "ring (heading). Ctrl+Z undo, Ctrl+Y redo, Ctrl+D duplicate, Del delete, R turn.");
        } else {
            ImGui::TextDisabled("Move, turn, add and delete vehicles, buildings, RDUs, TNT, crates, communication "
                                "points, push blocks and the carrier's start; then play the level with them.");
        }
        if (!g_editMsg.empty()) ImGui::TextWrapped("%s", g_editMsg.c_str());
    }
    if (ImGui::CollapsingHeader("Show", ImGuiTreeNodeFlags_DefaultOpen)) {
        for (int c = 0; c < CAT_COUNT; c++) {
            bool b = g_opt.show[c];
            if (ImGui::Checkbox(kCatLabels[c], &b)) g_opt.show[c] = b;
        }
        bool plain = g_opt.plain;
        if (ImGui::Checkbox("Plain background", &plain)) g_opt.plain = plain;
        bool all = g_opt.allCells;
        if (ImGui::Checkbox("Draw every cell (else what the game's camera saw)", &all)) {
            g_opt.allCells = all;
        }
        if (ImGui::Button("Recapture")) g_recapture = true;
        ImGui::SameLine();
        ImGui::TextDisabled("(the game steps 2 frames)");
        if (g_fz.cells) ImGui::TextDisabled("%u of %u cells drawn", g_fz.cellsDrawn, g_fz.cells);
    }
    if (ImGui::CollapsingHeader("Overlays")) {
        bool b = g_opt.markers;
        if (ImGui::Checkbox("Markers", &b)) g_opt.markers = b;
        ImGui::SameLine();
        b = g_opt.labels;
        if (ImGui::Checkbox("Labels", &b)) g_opt.labels = b;
        float ld = float(g_opt.labelDist);
        if (ImGui::SliderFloat("Label distance", &ld, 100.0f, 30000.0f, "%.0f", ImGuiSliderFlags_Logarithmic))
            g_opt.labelDist = ld;
        b = g_opt.collision;
        if (ImGui::Checkbox("Collision outlines (near the camera)", &b)) g_opt.collision = b;
        b = g_opt.zones;
        if (ImGui::Checkbox("Height zones", &b)) g_opt.zones = b;
        for (int k = 0; k < viewer::REC_KINDS; k++) {
            if (k == viewer::REC_PANEL || k == viewer::REC_CAMZONE || k == viewer::REC_CHOPPERZONE) continue;
            b = g_opt.kinds[k];
            ImGui::PushStyleColor(ImGuiCol_Text, kindColor(k));
            if (ImGui::Checkbox(viewer::recKindName(k), &b)) g_opt.kinds[k] = b;
            ImGui::PopStyleColor();
        }
    }
    if (ImGui::CollapsingHeader("Records")) {
        ImGui::BeginChild("recs", ImVec2(0, f * 12), true);
        for (size_t i = 0; i < g_recs.size(); i++) {
            ImGui::PushID(int(i));
            if (ImGui::Selectable(g_recs[i].title.c_str(), int(i) == g_sel)) g_sel = int(i);
            ImGui::PopID();
        }
        ImGui::EndChild();
        for (const std::string &n : g_notes) ImGui::TextDisabled("note: %s", n.c_str());
    }
    if (ImGui::CollapsingHeader("Models")) {
        if (g_mv.on) {
            ImGui::Text("Showing %s", g_mv.title.c_str());
            if (ImGui::Button("Back to the level")) g_mv.on = false;
        }
        ImGui::BeginChild("models", ImVec2(0, f * 10), true);
        for (size_t i = 0; i < g_recs.size(); i++) {
            const Record &r = g_recs[i];
            if (r.kind != viewer::REC_GVEHICLE && r.kind != viewer::REC_GOBJECT) continue;
            ImGui::PushID(int(i));
            const std::string t = r.kind == viewer::REC_GVEHICLE ? r.title + " - " + r.model : r.title;
            if (ImGui::Selectable(t.c_str(), g_mv.on && g_mv.rec == int(i))) modelFor(int(i));
            ImGui::PopID();
        }
        ImGui::EndChild();
        ImGui::TextDisabled("Vehicles and objects of this level, drawn by the game's own display lists.");
    }
    ImGui::End();
}

/* an on-screen keyboard for a name (a mod's: letters, digits, - _ . and space) or a file path (also : \ / ( ) ' &),
 * for a controller alone: a popup of keys ImGui's navigation walks (D-pad, A types, B closes) */
bool g_oskShift;
void nameKeyboard(const char *popup, char *buf, size_t n, bool path) {
    if (!ImGui::BeginPopupModal(popup, nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings)) return;
    ImGui::SetWindowFontScale(kPanelScale);
    ImGui::Text("%s: %s_", path ? "Path" : "Name", buf);
    static const char *const rows[5] = {"1234567890", "qwertyuiop", "asdfghjkl-", "zxcvbnm_. ", ":\\/()'&+,="};
    const float f = uiFontPx() * kPanelScale;
    for (int r = 0; r < (path ? 5 : 4); r++) {
        for (int k = 0; rows[r][k]; k++) {
            if (k) ImGui::SameLine();
            char c = rows[r][k];
            if (g_oskShift && c >= 'a' && c <= 'z') c = char(c - 'a' + 'A');
            char label[16];
            snprintf(label, sizeof label, "%s##k%d%d", c == ' ' ? "space" : std::string(1, c).c_str(), r, k);
            if (ImGui::Button(label, ImVec2(c == ' ' ? f * 3.2f : f * 1.6f, f * 1.5f))) {
                const size_t len = strlen(buf);
                if (len + 1 < n) buf[len] = c, buf[len + 1] = 0;
            }
        }
    }
    if (ImGui::Button(g_oskShift ? "abc" : "ABC", ImVec2(f * 3.2f, 0))) g_oskShift = !g_oskShift;
    ImGui::SameLine();
    if (ImGui::Button("Delete", ImVec2(f * 3.2f, 0))) {
        const size_t len = strlen(buf);
        if (len) buf[len - 1] = 0;
    }
    ImGui::SameLine();
    if (ImGui::Button("Clear", ImVec2(f * 3.2f, 0))) buf[0] = 0;
    ImGui::SameLine();
    if (ImGui::Button("Done", ImVec2(f * 3.2f, 0)) || ImGui::IsKeyPressed(ImGuiKey_GamepadFaceRight, false))
        ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
}

/* the editor's change list, and what to do with it */
void changesPanel() {
    const ImGuiIO &io = ImGui::GetIO();
    const float f = uiFontPx() * kPanelScale;
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x - f * 0.5f, io.DisplaySize.y - f * 2.5f), ImGuiCond_FirstUseEver,
                            ImVec2(1, 1));
    ImGui::SetNextWindowSize(ImVec2(f * 24, std::min(f * 21, io.DisplaySize.y * 0.4f)), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowBgAlpha(0.85f);
    if (!ImGui::Begin("Changes", nullptr, ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing)) {
        ImGui::End();
        return;
    }
    ImGui::SetWindowFontScale(kPanelScale);
    const auto &base = g_ed.baseChanges();
    const auto &ch = g_ed.steps();
    ImGui::Text("Level %d: %u change%s%s", g_ed.level(), unsigned(ch.size()), ch.size() == 1 ? "" : "s",
                g_ed.dirty() ? "  (not in the game yet: Apply)" : "  (the game runs them)");
    ImGui::BeginChild("changelist", ImVec2(0, f * 8), true);
    for (size_t i = 0; i < base.size(); i++)
        ImGui::TextDisabled("mod %s: %s", g_ed.baseName().c_str(), edit::describe(base[i]).c_str());
    if (g_ed.hasBaseGeo()) ImGui::TextDisabled("mod %s: its terrain", g_ed.baseName().c_str());
    for (size_t i = 0; i < ch.size(); i++) {
        ImGui::PushID(int(i));
        const std::string t = std::to_string(i + 1) + ". " + edit::describe(ch[i]);
        if (ImGui::Selectable(t.c_str(), false) && ch[i].type == edit::STEP_RECORD && ch[i].rec.op != LM_OP_DELETE) {
            const int k = ch[i].rec.kind, idx = ch[i].rec.index;
            g_after.push_back([k, idx] { reselect(k, idx); });
        }
        ImGui::PopID();
    }
    if (base.empty() && ch.empty() && !g_ed.hasBaseGeo()) ImGui::TextDisabled("No changes yet.");
    ImGui::EndChild();
    ImGui::BeginDisabled(!g_ed.canUndo());
    if (ImGui::Button("Undo (Ctrl+Z)")) g_after.push_back(editUndo);
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(!g_ed.canRedo());
    if (ImGui::Button("Redo (Ctrl+Y)")) g_after.push_back(editRedo);
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(ch.empty());
    if (ImGui::Button("Undo all")) g_after.push_back([] {
        g_ed.revertAll();
        g_editMsg = "Every change undone (Redo brings them back).";
        readFileRecords();
        terrainChanged();
        g_sel = -1;
    });
    ImGui::EndDisabled();
    ImGui::Separator();
    {
        /* (before Apply: what an edit put on the carrier's route) */
        const std::string w = carrierWarning();
        if (!w.empty()) {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.55f, 0.25f, 1.0f));
            ImGui::TextWrapped("%s", w.c_str());
            ImGui::PopStyleColor();
        }
    }
    if (ImGui::Button("Apply (reload the level)")) g_after.push_back(applyEdits);
    ImGui::SameLine();
    if (ImGui::Button("Play from here")) g_after.push_back(playEdits);
    ImGui::TextDisabled("Apply: the game sets the level up again from the edited file. Play: leaves the viewer into "
                        "the edited level (a test run: saving is off until bc.exe is closed).");
    ImGui::Separator();
    ImGui::SetNextItemWidth(f * 12);
    ImGui::InputText("##modname", g_modName, sizeof g_modName);
    ImGui::SameLine();
    if (ImGui::Button("Save mod")) {
        const std::string n = g_modName;
        const bool en = g_modEnable;
        g_after.push_back([n, en] { saveMod(n, en); });
    }
    ImGui::SameLine();
    if (ImGui::Button("Keys...")) ImGui::OpenPopup("The mod's name");   /* (a controller's keyboard) */
    nameKeyboard("The mod's name", g_modName, sizeof g_modName, false);
    ImGui::Checkbox("Use it from the next start (bc.ini [mods])", &g_modEnable);
    ImGui::TextDisabled("%s<name>.bcmod: a small file with the changes only (no game data): share it.",
                        modsDir().c_str());
    if (!g_savedPath.empty())
        ImGui::TextDisabled("%s %s", g_ed.imageSha1() == g_savedSha ? "Saved:" : "Changed since", g_savedPath.c_str());
    /* the level's heap: an edited level that holds more uses more of it */
    const uint32_t heap = rd<uint32_t>(kHeapPtr);
    if (heap > 0x80000000u && heap < 0x8021ED00u)
        ImGui::TextDisabled("Level heap ends at 0x%08X: %u KB below the load buffer (0x8021ED00)", heap,
                            unsigned((0x8021ED00u - heap) / 1024));
    ImGui::End();
}

bool g_padNav;                    /* the controller works the panels (Start) */
bool g_padNavFocus;               /* (focus a panel on the next frame) */
std::string g_padNavWin;          /* (the panel the controller was on when the frozen loop ended: Apply) */

void buildUi() {
    if (g_cleanShot) return;   /* (nothing over the picture) */
    drawOverlays();
    if (g_overlayShot || !g_opt.ui) return;
    mainPanel();
    inspector();
    if (editing()) changesPanel();
    if (terrainMode()) terrainPanel();
    if (g_padNavFocus) {
        /* the controller was given to the panels: the one for what is being done takes it (or, back from an
         * Apply, the one it was on) */
        g_padNavFocus = false;
        const char *w = terrainMode() ? "Terrain" : g_sel >= 0 ? "Inspector" : "Level viewer";
        if (!g_padNavWin.empty() && ImGui::FindWindowByName(g_padNavWin.c_str()) != nullptr) w = g_padNavWin.c_str();
        ImGui::SetWindowFocus(w);
        g_padNavWin.clear();
    }
    /* the status line */
    const ImGuiIO &io = ImGui::GetIO();
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y - 4), ImGuiCond_Always, ImVec2(0.5f, 1));
    ImGui::SetNextWindowBgAlpha(0.5f);
    if (ImGui::Begin("##viewerstatus", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs |
                                                    ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings |
                                                    ImGuiWindowFlags_NoFocusOnAppearing |
                                                    ImGuiWindowFlags_NoBringToFrontOnFocus)) {
        ImGui::SetWindowFontScale(kPanelScale);
        if (g_padNav)
            ImGui::Text("Controller on the panels: D-pad / stick: move, A: press, B: back, hold X + shoulders: other "
                        "panels, Start / Back: back to the camera");
        else if (g_mv.on)
            ImGui::Text("Model view: %s   (drag: turn, wheel: zoom, Esc / B: back)", g_mv.title.c_str());
        else
            ImGui::Text("x %.0f  y %.0f  z %.0f   yaw %.0f  pitch %.0f   speed %.0f   (Tab: panels, F12: screenshot, "
                        "Esc: leave)",
                        g_cam.pos.x, g_cam.pos.y, g_cam.pos.z, g_cam.yaw, g_cam.pitch, g_opt.speed);
    }
    ImGui::End();
}

}  // namespace vw
}  // namespace live
