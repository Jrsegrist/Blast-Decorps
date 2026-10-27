/* bc.exe's settings menu (live_menu.cpp): an overlay drawn with Dear ImGui
 * (bundled with RT64, MIT) on top of the game's picture, opened with the
 * menu key (F1) or the controller's menu buttons (Back+Start).  The game
 * stays paused while it is open.  It edits the Config (bc.ini's settings);
 * live_rt64.cpp applies each change at once (MenuEnv::changed) and the menu
 * writes the settings it changed to bc.ini when it closes. */
#ifndef LIVE_MENU_H
#define LIVE_MENU_H

#include <functional>
#include <string>
#include <vector>

#include "live_setup.h"

namespace RT64 {
struct Application;
}
namespace plume {
struct RenderCommandList;
}

namespace live {

struct MenuEnv {
    RT64::Application *app = nullptr;
    SDL_Window *window = nullptr;
    Config *cfg = nullptr;
    std::function<void(const Setting &)> changed;          /* cfg changed: apply this setting now */
    std::function<void()> quit;                            /* the "Quit the game" button */
    std::function<std::vector<std::string>()> padNames;    /* the game controllers connected now */
    std::function<std::string()> status;                   /* one line about the renderer (resolution ...) */
    std::function<std::string()> packStatus;               /* the texture pack's state ("" = never loaded) */
    std::function<void()> reloadPack;                      /* load the texture pack again from its files */
    std::function<std::string()> dumpStatus;               /* the texture dump's counts ("" = off) */
    std::function<unsigned()> msaaSupported;               /* MSAA sample counts possible (mask: 2, 4, 8) */
    std::function<std::string()> cheatsStatus;             /* the debug cheats and where the game saves now */
    std::function<void()> viewerTab;                       /* the Viewer tab's contents (live_viewer.cpp) */
};

void menuSetup(const MenuEnv &env);   /* once RT64 runs */
void menuOpen();
void menuClose();                     /* saves the settings it changed */
bool menuIsOpen();
/* while open: every SDL event goes here first; true if the menu used it */
bool menuEvent(SDL_Event &e);
/* waiting for the key or button to bind (then every event is the menu's) */
bool menuCapturing();
/* one frame of the menu (main thread, before RT64's updateScreen) */
void menuFrame();
/* RT64's present thread, after the picture: draws the menu's last frame */
void menuDraw(plume::RenderCommandList *list);
/* RT64 is shutting down (its deinit hook): free the menu's GPU objects */
void menuShutdown();
/* testing (--menu-set): change "section.key=value" as the open menu would
 * (applied now, saved when the menu closes) */
bool menuTestSet(const std::string &spec);

/* The frame-rate counter: a small box in a corner of the window, drawn by
 * the same overlay as the menu (and with it while the menu is open).  Off,
 * nothing of it exists or runs. */
struct CounterStats {
    int mode = 0;               /* 0 off, 1 on, 2 detailed */
    int corner = 0;             /* 0 top-left, 1 top-right, 2 bottom-left, 3 bottom-right */
    double shown = 0;           /* pictures presented a second */
    double game = 0;            /* the game's own frames a second */
    double frameMs = 0, frameMsMax = 0;   /* present intervals (mean, max) */
    std::vector<float> graph;   /* the last present intervals (up to 240), ms (detailed) */
    double targetMs = 0;        /* the interval aimed at */
    unsigned stutters = 0;      /* intervals over 1.5x the target since the counter was turned on */
    std::string note;           /* e.g. "interpolated, tagged" */
};
void counterSet(const CounterStats &s);
/* main thread, about 60 times a second while the menu is closed and the
 * counter on (or overlayWanted): its ImGui frame */
void counterFrame();
/* The same overlay's other items (main thread): a small marker that stays in a
 * bottom corner ("" = none; the debug cheats are on), and a short note at the
 * top for `seconds` (a setting switched by its key).  Like the counter they
 * are drawn over the picture by RT64's present thread, never into the game's
 * frame. */
void overlayMarker(const std::string &text);
void overlayNote(const std::string &text, double seconds = 3.0);
bool overlayWanted();   /* a marker or a note is up */

/* The level viewer's panels and overlays (viewer_panels.cpp), on the same ImGui
 * context and the same present-thread drawing as the menu (main thread):
 * uiFrame builds a frame with `build` (its windows, and the background draw
 * list for markers) and shows it until the next uiFrame or uiHide; `mouse`:
 * ImGui sees the mouse.  uiEvent hands ImGui an SDL event; uiWantsMouse /
 * uiWantsKeyboard: ImGui is using them (a panel under the mouse, a text field). */
void uiFrame(const std::function<void()> &build, bool mouse);
void uiHide();
bool uiEvent(SDL_Event &e);
/* the controller works the viewer's panels (ImGui's gamepad navigation in uiFrame; off: the controller flies the
 * camera and works the tools, the panels don't hear it) */
void uiPadNav(bool on);
/* the controllers' state for that (buttons: a bit per SDL_GameControllerButton; the axes) */
void uiPadState(uint32_t buttons, const int16_t axes[SDL_CONTROLLER_AXIS_MAX]);
/* a controller came or went: ImGui's backend (the menu's and the panels' navigation) is told whatever is open */
void uiDeviceEvent(SDL_Event &e);
bool uiWantsMouse();
bool uiWantsKeyboard();
float uiFontPx();

}  // namespace live

#endif
