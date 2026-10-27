/* bc.exe's optional free orbit camera (live_orbit.cpp; [camera] mode = orbit).
 *
 * Render-only: the game keeps computing its own camera for everything it
 * does; only the pictures RT64 renders use the orbit camera's view (RT64
 * patch 0006's ViewHook, per rendered picture).  Nothing the game reads
 * changes, so its per-frame trace is the same as with the option off. */
#ifndef LIVE_ORBIT_H
#define LIVE_ORBIT_H

#include <cstdint>
#include <string>

#include <SDL.h>

#include "live_input.h"
#include "live_setup.h"

namespace RT64 {
struct Application;
}

namespace live {

void orbitSetup(RT64::Application *app, Config *cfg, SDL_Window *window);
/* the settings changed (start-up, the menu): [camera], controller bindings */
void orbitApply();
/* the orbit camera is in charge now (orbit mode, in play) */
bool orbitActive();
/* the bindings the N64 pad is read with: the configuration's, or (orbit
 * active) without the inputs the camera uses */
const Config &orbitPadConfig();
/* SDL events (mouse look); true if the camera used it */
bool orbitEvent(const SDL_Event &e);
/* each retrace (main thread): the camera's input and one step */
void orbitVi(const PadSet &pads, const Uint8 *keys);
/* around RT64's processDisplayLists of a gfx task */
void orbitBeforeTask(uint32_t dl, bool frameDl);
void orbitAfterTask(bool frameDl);
/* testing: --orbit-test YAWSPEED[:PITCH[:ZOOM]] turns the camera by itself
 * (degrees a second, an absolute pitch in degrees, a zoom factor), with no
 * player input; --orbit-log logs every frame's cameras */
bool orbitSetTest(const std::string &spec);
void orbitSetLog(bool on);
void orbitSetStepLog(bool on);   /* --orbit-steplog (testing): every step's collision state */
/* testing: --orbit-cull all (the drawing paths get every cell of the level,
 * as before the orbit view's test) or view (the default: the game's cells and
 * those the orbit camera can see) */
bool orbitSetCull(const std::string &mode);
/* testing: --orbit-repeatable: kept for old scripts; each picture now always
 * uses the orbit at its own time between two steps (the same pictures every
 * run) */
void orbitSetRepeatable(bool on);
/* testing: --view-log FILE: every picture drawn with the orbit's view (CSV:
 * when RT64's render thread drew it, its workload, frame and weight, the
 * view's heading and pitch about the target, the orbit's yaw) */
void orbitSetViewLog(const char *path);
void orbitSummary();
/* the orbit's host work (always measured while the orbit mode is on: a few
 * clock reads a frame), ms: its step with the collision queries (readMs:
 * reading the obstacles and the ground, part of stepMs), the drawing list,
 * the frame task's setup (backdrop, billboards: taskMs) on the main thread;
 * RT64's render thread in the view hook (viewMs, every picture).  Counts:
 * drawing lists made (listsAll of them with every cell), the cells they had
 * and the game's own (summed over the lists), collision height queries,
 * obstacle / ground-cell grids made again (the triangles changed), pictures
 * given the orbit's view, steps with the collision pulling the eye in under
 * 30% of its distance (close to the vehicle). */
struct OrbitPerf {
    double stepMs = 0, cellsMs = 0, readMs = 0, taskMs = 0, viewMs = 0;
    unsigned lists = 0, cells = 0, gameCells = 0, queries = 0, listsAll = 0, obstacleBuilds = 0, groundBuilds = 0,
             views = 0, steps = 0, closeSteps = 0;
    OrbitPerf operator-(const OrbitPerf &o) const;
};
/* testing (--perf-log): since the last call */
OrbitPerf orbitPerfTake();
/* the frame-time report (live_frametimes.cpp frameTick, each retrace): since the last call */
OrbitPerf orbitFrameTake();

}  // namespace live

#endif
