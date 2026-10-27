/* The level viewer's input (viewer_internal.h): keyboard, mouse and game controllers while it holds the
 * game; the free camera's and the model view's step. */
#include <windows.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>

#include "imgui/imgui.h"
#include "imgui/imgui_internal.h"   /* (the panel X + shoulders choose: NavWindowingTarget) */

#include "live_menu.h"
#include "viewer_internal.h"

extern "C" {
#include "plat_host.h"
}

namespace live {
namespace vw {

/* ---- input ------------------------------------------------------------------------- */
bool g_looking;                   /* right mouse button held: mouse look */
double g_mdx, g_mdy, g_wheel;
unsigned short g_padPrev;

/* window points (SDL's, in desktop points) -> the picture's pixels (ImGui's) */
void windowToPixels(int wx, int wy, float &x, float &y) {
    int ww, wh, pw, ph;
    SDL_GetWindowSize(g_env.window, &ww, &wh);
    SDL_GetWindowSizeInPixels(g_env.window, &pw, &ph);
    x = float(wx) * pw / std::max(1, ww);
    y = float(wy) * ph / std::max(1, wh);
}

void viewerEvent(SDL_Event &e) {
    if (e.type == SDL_QUIT) g_env.quit(0);
    const bool uiMouse = uiWantsMouse() && !g_looking;
    if (g_opt.ui) uiEvent(e);
    if (e.type == SDL_CONTROLLERDEVICEADDED || e.type == SDL_CONTROLLERDEVICEREMOVED) {
        /* (ImGui hears a controller come and go even with the panels hidden: it opens it for their navigation) */
        if (!g_opt.ui) uiDeviceEvent(e);
        host_log("live: viewer: controller %s (SDL %d)\n", e.type == SDL_CONTROLLERDEVICEADDED ? "connected" : "disconnected",
                 int(e.cdevice.which));
    }
    switch (e.type) {
    case SDL_KEYDOWN: {
        if (e.key.repeat || uiWantsKeyboard()) break;
        const SDL_Scancode sc = e.key.keysym.scancode;
        const bool ctrl = (e.key.keysym.mod & KMOD_CTRL) != 0, shift = (e.key.keysym.mod & KMOD_SHIFT) != 0;
        /* the editor's keys */
        if (ctrl && sc == SDL_SCANCODE_E) {
            setEditMode(!g_editMode);
            break;
        }
        if (ctrl && sc == SDL_SCANCODE_T && editing()) {
            g_tv.on = !g_tv.on && g_ed.terrainOk();
            break;
        }
        if (terrainKey(sc, ctrl, shift)) break;
        if (editing()) {
            bool used = true;
            if (ctrl && sc == SDL_SCANCODE_Z && !shift) editUndo();
            else if (ctrl && (sc == SDL_SCANCODE_Y || (sc == SDL_SCANCODE_Z && shift))) editRedo();
            else if (ctrl && sc == SDL_SCANCODE_D) duplicateSel();
            else if (sc == SDL_SCANCODE_DELETE) deleteSel();
            else if (sc == SDL_SCANCODE_R && !ctrl) rotateSel(shift ? -15 : 15);
            else if (sc == SDL_SCANCODE_G && !ctrl) groundSel();
            else used = false;
            if (used) break;
        }
        if (sc == SDL_SCANCODE_ESCAPE) {
            if (g_tv.stroking || g_tv.rampN) g_tv.stroking = false, g_tv.cur.clear(), g_tv.rampN = 0;   /* (called off) */
            else if (g_drag.mode != 0) g_drag = Drag();   /* (a drag called off) */
            else if (g_mv.on) g_mv.on = false;
            else g_leave = true;
        } else if (sc == SDL_SCANCODE_TAB || sc == SDL_SCANCODE_F1) {
            g_opt.ui = !g_opt.ui;
        } else if (sc == SDL_SCANCODE_F12) {
            if (e.key.keysym.mod & KMOD_SHIFT) g_overlayShot = true;
            else g_cleanShot = true;
        } else if (sc == SDL_SCANCODE_F11 || (sc == SDL_SCANCODE_RETURN && (e.key.keysym.mod & KMOD_ALT))) {
            g_env.toggleFullscreen();
        } else if (sc == SDL_SCANCODE_HOME || sc == SDL_SCANCODE_P) {
            jumpToStart();
        } else if (sc == SDL_SCANCODE_O) {
            cameraTop();
        } else if (sc == SDL_SCANCODE_M) {
            if (g_mv.on) g_mv.on = false;
            else modelFor(g_sel);
        } else if (sc == SDL_SCANCODE_F && g_sel >= 0 && g_recs[g_sel].hasPos) {
            g_mv.on = false;
            g_cam.lookAt({g_recs[g_sel].x, g_recs[g_sel].y, g_recs[g_sel].z}, std::max(120.0, g_recs[g_sel].radius * 5));
        }
        break;
    }
    case SDL_MOUSEBUTTONDOWN:
        if (e.button.button == SDL_BUTTON_RIGHT || (g_mv.on && e.button.button == SDL_BUTTON_LEFT && !uiMouse)) {
            g_looking = true;
            SDL_SetRelativeMouseMode(SDL_TRUE);
        } else if (e.button.button == SDL_BUTTON_LEFT && !uiMouse) {
            float x, y;
            windowToPixels(e.button.x, e.button.y, x, y);
            if (terrainMode()) {
                const SDL_Keymod km = SDL_GetModState();
                terrainMouseDown(x, y, (km & KMOD_SHIFT) != 0, (km & KMOD_CTRL) != 0);
                break;
            }
            /* the editor: the gizmo of the selected record first, else a selection */
            const int hit = editing() ? gizmoHit(x, y) : 0;
            if (hit == 0 || !beginDrag(hit, x, y)) selectAt(x, y);
        }
        break;
    case SDL_MOUSEBUTTONUP:
        if (e.button.button == SDL_BUTTON_LEFT) terrainMouseUp();
        if (e.button.button == SDL_BUTTON_LEFT && g_drag.mode != 0) endDrag();
        if (g_looking && (SDL_GetMouseState(nullptr, nullptr) & (SDL_BUTTON_LMASK | SDL_BUTTON_RMASK)) == 0) {
            g_looking = false;
            SDL_SetRelativeMouseMode(SDL_FALSE);
        }
        break;
    case SDL_MOUSEMOTION:
        if (g_looking) g_mdx += e.motion.xrel, g_mdy += e.motion.yrel;
        if (!g_looking) {
            float x, y;
            windowToPixels(e.motion.x, e.motion.y, x, y);
            g_tv.mx = x, g_tv.my = y, g_tv.padAim = false;
        }
        if (g_drag.mode != 0) {
            float x, y;
            windowToPixels(e.motion.x, e.motion.y, x, y);
            updateDrag(x, y);
        }
        break;
    case SDL_MOUSEWHEEL:
        if (!uiMouse) g_wheel += e.wheel.preciseY != 0.0f ? e.wheel.preciseY : float(e.wheel.y);
        break;
    default:
        break;
    }
}

/* X held, a shoulder pressed: the panel before / after the one ImGui would focus on X's release, in this fixed
 * order (those shown); ImGui focuses it when X is let go */
void panelCycle(int dir) {
    static const char *const kOrder[] = {"Level viewer", "Inspector", "Changes", "Terrain"};
    const int n = int(sizeof kOrder / sizeof kOrder[0]);
    ImGuiContext *g = ImGui::GetCurrentContext();
    if (g == nullptr || g->NavWindowingTarget == nullptr) return;   /* (ImGui sets it when X is pressed) */
    int cur = -1;
    for (int i = 0; i < n; i++)
        if (!strcmp(g->NavWindowingTarget->RootWindow->Name, kOrder[i])) cur = i;
    for (int step = 1; step <= n; step++) {
        const int i = ((cur < 0 ? (dir > 0 ? -1 : 0) : cur) + dir * step + 2 * n) % n;
        ImGuiWindow *w = ImGui::FindWindowByName(kOrder[i]);
        if (w == nullptr || !w->WasActive || w->Hidden) continue;
        g->NavWindowingTarget = g->NavWindowingTargetAnim = w;
        g->NavWindowingHighlightAlpha = 1.0f;   /* (a choice made: X's release focuses it, doesn't toggle the menu layer) */
        g->NavWindowingToggleLayer = false;
        host_log("live: viewer: controller: panel %s (X + %s)\n", kOrder[i], dir > 0 ? "RB" : "LB");
        return;
    }
}

double axis(const PadState &p, int a) {
    const double v = p.axis(a) / 32767.0;
    return std::fabs(v) < 0.2 ? 0.0 : (v - (v > 0 ? 0.2 : -0.2)) / 0.8;
}

void stepCamera(double dt) {
    const Uint8 *k = SDL_GetKeyboardState(nullptr);
    const bool typing = uiWantsKeyboard();
    double fwd = 0, rt = 0, up = 0, turnX = 0, turnY = 0;
    double mult = 1.0;
    if (!typing) {
        fwd += (k[SDL_SCANCODE_W] || k[SDL_SCANCODE_UP] ? 1 : 0) - (k[SDL_SCANCODE_S] || k[SDL_SCANCODE_DOWN] ? 1 : 0);
        rt += (k[SDL_SCANCODE_D] || k[SDL_SCANCODE_RIGHT] ? 1 : 0) - (k[SDL_SCANCODE_A] || k[SDL_SCANCODE_LEFT] ? 1 : 0);
        up += (k[SDL_SCANCODE_E] || k[SDL_SCANCODE_SPACE] ? 1 : 0) - (k[SDL_SCANCODE_Q] || k[SDL_SCANCODE_C] ? 1 : 0);
        if (k[SDL_SCANCODE_LSHIFT] || k[SDL_SCANCODE_RSHIFT]) mult = 4.0;
        if (k[SDL_SCANCODE_LCTRL] || k[SDL_SCANCODE_RCTRL]) mult = 0.25;
    }
    /* controllers: left stick moves, right stick looks, triggers down / up, shoulders: speed, A: select at the
     * centre, B: back, Y: player start, Back: leave, Start: panels */
    unsigned short buttons = 0;
    int16_t axes[SDL_CONTROLLER_AXIS_MAX] = {};
    double padFwd = 0;   /* (the left stick forward: in the Terrain mode along the ground, see below) */
    if (g_env.pads) {
        const PadSet pads = g_env.pads();
        for (const PadState &p : pads.pads) {
            padFwd -= axis(p, SDL_CONTROLLER_AXIS_LEFTY);
            rt += axis(p, SDL_CONTROLLER_AXIS_LEFTX);
            turnX += axis(p, SDL_CONTROLLER_AXIS_RIGHTX);
            turnY += axis(p, SDL_CONTROLLER_AXIS_RIGHTY);
            up += p.axis(SDL_CONTROLLER_AXIS_TRIGGERRIGHT) / 32767.0 - p.axis(SDL_CONTROLLER_AXIS_TRIGGERLEFT) / 32767.0;
            buttons |= uint16_t(p.buttons & 0xFFFF);
            for (int a = 0; a < SDL_CONTROLLER_AXIS_MAX; a++)
                if (std::abs(int(p.axis(a))) > std::abs(int(axes[a]))) axes[a] = p.axes[a];
        }
    }
    unsigned short pressed = uint16_t(buttons & ~g_padPrev);
    g_padPrev = buttons;
    /* hold X + LB / RB on the panels: the next panel in a fixed order (ImGui's own goes by the last used); the
     * shoulders aren't ImGui's then */
    unsigned short uiButtons = buttons;
    const unsigned short shoulders = (1u << SDL_CONTROLLER_BUTTON_LEFTSHOULDER) | (1u << SDL_CONTROLLER_BUTTON_RIGHTSHOULDER);
    if (g_padNav && (buttons & (1u << SDL_CONTROLLER_BUTTON_X))) {
        uiButtons = uint16_t(uiButtons & ~shoulders);
        if (pressed & shoulders) panelCycle((pressed & (1u << SDL_CONTROLLER_BUTTON_RIGHTSHOULDER)) ? 1 : -1);
    }
    uiPadState(uiButtons, axes);   /* (the panels' navigation reads it when the controller is on them) */
    /* Start: the controller on the panels (shown) <-> on the camera (the panels stay); the left stick's click shows
     * or hides the panels (as Tab) */
    if ((pressed & (1u << SDL_CONTROLLER_BUTTON_LEFTSTICK)) && !g_padNav) {
        g_opt.ui = !g_opt.ui;
        host_log("live: viewer: controller: panels %s\n", g_opt.ui ? "shown" : "hidden");
    }
    if (pressed & (1u << SDL_CONTROLLER_BUTTON_START)) {
        if (g_padNav) g_padNav = false;
        else g_opt.ui = true, g_padNav = true, g_padNavFocus = true;
        uiPadNav(g_padNav);
        host_log("live: viewer: controller %s\n", g_padNav ? "on the panels" : "on the camera (panels shown)");
        pressed = uint16_t(pressed & ~(1u << SDL_CONTROLLER_BUTTON_START));
    }
    if (g_padNav && !g_opt.ui) {   /* (the panels were hidden with Tab) */
        g_padNav = false;
        uiPadNav(false);
    }
    if (g_padNav) {
        /* (the panels have it: ImGui's navigation; Back hands it back to the camera too) */
        if (pressed & (1u << SDL_CONTROLLER_BUTTON_BACK)) {
            g_padNav = false;
            uiPadNav(false);
            host_log("live: viewer: controller on the camera (panels shown)\n");
        }
        fwd = rt = up = turnX = turnY = 0;
        if (!typing) {
            fwd += (k[SDL_SCANCODE_W] || k[SDL_SCANCODE_UP] ? 1 : 0) - (k[SDL_SCANCODE_S] || k[SDL_SCANCODE_DOWN] ? 1 : 0);
            rt += (k[SDL_SCANCODE_D] || k[SDL_SCANCODE_RIGHT] ? 1 : 0) - (k[SDL_SCANCODE_A] || k[SDL_SCANCODE_LEFT] ? 1 : 0);
            up += (k[SDL_SCANCODE_E] || k[SDL_SCANCODE_SPACE] ? 1 : 0) - (k[SDL_SCANCODE_Q] || k[SDL_SCANCODE_C] ? 1 : 0);
        }
        pressed = 0;
        buttons = 0;
    }
    if (terrainMode()) {
        /* (A, B, X, the shoulders and the D-pad work the terrain tools) */
        terrainPad(buttons, pressed);
        pressed = uint16_t(pressed & ~((1u << SDL_CONTROLLER_BUTTON_A) | (1u << SDL_CONTROLLER_BUTTON_B) |
                                       (1u << SDL_CONTROLLER_BUTTON_X) | (1u << SDL_CONTROLLER_BUTTON_LEFTSHOULDER) |
                                       (1u << SDL_CONTROLLER_BUTTON_RIGHTSHOULDER)));
    }
    if (pressed & (1u << SDL_CONTROLLER_BUTTON_LEFTSHOULDER)) g_opt.speed = std::max(50.0, g_opt.speed / 1.5);
    if (pressed & (1u << SDL_CONTROLLER_BUTTON_RIGHTSHOULDER)) g_opt.speed = std::min(8000.0, g_opt.speed * 1.5);
    if (pressed & (1u << SDL_CONTROLLER_BUTTON_Y)) jumpToStart();
    if (pressed & (1u << SDL_CONTROLLER_BUTTON_BACK)) g_leave = true;
    if (pressed & (1u << SDL_CONTROLLER_BUTTON_B)) {
        if (g_mv.on) g_mv.on = false;
        else g_sel = -1;
    }
    if (pressed & (1u << SDL_CONTROLLER_BUTTON_A)) {
        const ImGuiIO &io = ImGui::GetIO();
        selectAt(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f);
    }
    /* the stick forward: in the Terrain mode along the ground at the camera's height (the tools work at the
     * picture's centre: flying along the view would keep pointing at the same spot - a ramp's end wouldn't move),
     * else along the view */
    if (g_padNav) padFwd = 0;
    double flat = 0;
    if (terrainMode() && !g_mv.on) flat = padFwd;
    else fwd += padFwd;
    if (g_mv.on) {
        g_mv.yaw -= g_mdx * 0.3 + turnX * 120 * dt + rt * 90 * dt;
        g_mv.pitch = std::max(-85.0, std::min(85.0, g_mv.pitch + g_mdy * 0.3 + turnY * 90 * dt));
        g_mv.dist = std::max(g_mv.radius * 0.6, g_mv.dist * std::pow(0.85, g_wheel) * std::pow(0.4, fwd * dt));
    } else {
        if (g_wheel != 0) g_opt.speed = std::max(50.0, std::min(8000.0, g_opt.speed * std::pow(1.25, g_wheel)));
        g_cam.turn(-g_mdx * 0.15 - turnX * 140 * dt, -g_mdy * 0.15 - turnY * 100 * dt);
        g_cam.move(std::max(-1.0, std::min(1.0, fwd)), std::max(-1.0, std::min(1.0, rt)),
                   std::max(-1.0, std::min(1.0, up)), g_opt.speed * mult * dt);
        if (flat != 0) {
            const Vec f = g_cam.forward();
            const double h = std::sqrt(f.x * f.x + f.z * f.z);
            const double d = std::max(-1.0, std::min(1.0, flat)) * g_opt.speed * dt;
            if (h > 1e-6) g_cam.pos.x += f.x / h * d, g_cam.pos.z += f.z / h * d;
        }
    }
    g_mdx = g_mdy = g_wheel = 0;
}

}  // namespace vw
}  // namespace live
