/* bc.exe's level and model viewer (live_viewer.cpp): `bc.exe --viewer [LEVEL]`
 * or the settings menu's Viewer tab.
 *
 * The game logic is held still the way the settings menu holds it: the game
 * waits inside the retrace hook (live_rt64.cpp liveVi) while the viewer runs,
 * so not one instruction of it runs.  The picture keeps being drawn by the
 * game's own display lists: the graphics tasks of the last frame are given to
 * RT64 again and again, with a free camera's view (RT64 patch 0006's view
 * hook), categories of drawing left out (the game's PORT_ITAG tags say who
 * draws what) and the far plane moved out.  Overlays (markers, labels,
 * collision outlines) and the panels are Dear ImGui drawn over the picture,
 * never into it.  A level is loaded by the game's own set-up (game/port.h
 * PORT_VIEWER_FRAME asks for MODE_LEVEL_START).  Leaving goes back to normal
 * play when the viewer only looked at the game as it was; after the viewer
 * loaded a level it restarts bc.exe (a fresh boot), since the game was not
 * brought to that level by play (its player record, saves and status block
 * don't belong to it). */
#ifndef LIVE_VIEWER_H
#define LIVE_VIEWER_H

#include <cstdint>
#include <functional>
#include <string>

#include <SDL.h>

#include "live_input.h"
#include "live_setup.h"

namespace RT64 {
struct Application;
}

namespace live {

struct ViewerEnv {
    RT64::Application *app = nullptr;
    SDL_Window *window = nullptr;
    Config *cfg = nullptr;
    uint32_t *viOrigin = nullptr;             /* live_rt64.cpp's VI registers RT64 reads */
    uint32_t *viCurrentLine = nullptr;
    std::function<PadSet()> pads;            /* the controllers' latest sample */
    std::function<void(const std::string &path)> saveWindow;   /* the window's picture as a PNG */
    std::function<void()> toggleFullscreen;
    std::function<void(int code)> quit;      /* exit bc.exe */
    std::function<void()> restart;           /* start bc.exe again without the viewer, then exit */
    std::string shotDir;                     /* screenshots ("" = the exe's folder\screenshots) */
};

void viewerSetup(const ViewerEnv &env);

/* --viewer [LEVEL]: start in the viewer (LEVEL -1: the first level picked; 0 by default) */
void viewerStartAtBoot(int level);
/* --viewer-test SPEC (testing): a script of viewer actions (viewer_script.cpp runTestOp) */
bool viewerSetTest(const std::string &spec);
/* the settings menu's Viewer tab (ImGui, inside the menu's frame) */
void viewerMenuTab();
/* asked to open from the menu (the menu then closes) */
bool viewerWanted();
/* open it over the game as it is now (testing: --viewer-open FRAME; the menu's button) */
void viewerOpen();

/* the game's state the rest of bc.exe needs */
bool viewerActive();      /* from the request until the viewer is left: no pacing, no sound, neutral input */

/* live_rt64.cpp's hooks: every graphics task (before RT64 reads it), each retrace (after the game's step,
 * before the settings menu); viewerVi runs the frozen viewer itself when it is time (and returns when it is
 * left or the game must step: a level load, a recapture) */
void viewerGfxTask(unsigned ucode, unsigned ucodeData, unsigned dl, bool frameDl);
void viewerVi(unsigned frames);
/* one line for the log at exit */
void viewerSummary();

}  // namespace live

#endif
