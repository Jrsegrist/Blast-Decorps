/* bc.exe's widescreen fixes for the game's full-width 2D (live_wide.cpp).
 * Host side only: RDRAM and the game's display lists are never changed. */
#ifndef LIVE_WIDE_H
#define LIVE_WIDE_H

#include <cstdint>

namespace RT64 {
struct State;
}

namespace live {

/* the picture's width over the N64's 4:3 width (1 with video.aspect =
 * original: then nothing is touched, and the GBI isn't wrapped for it) */
void wideSetFactor(double k);
/* video.hud_position = edges: the HUD's left and right parts at the screen's
 * edges (widescreen only; RT64's extAspectRatio must be Expand) */
void wideSetHudEdges(bool on);
/* around RT64's processDisplayLists of each task */
void wideBeforeTask();
void wideAfterTask(bool frameDl);

/* after RT64 ran a G_VTX (`w1` its address word; `first` the workload's
 * vertex count before it): live_interp.cpp's dispatch, and the orbit camera
 * for the backdrop commands of its own */
void wideAfterVertex(RT64::State *state, uint32_t w1, uint32_t first);

/* one line for the log at exit */
void wideSummary();

}  // namespace live

#endif
