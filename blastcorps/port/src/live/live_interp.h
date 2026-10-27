/* bc.exe's tagged frame interpolation (live_interp.cpp): who draws what, from
 * the game's PORT_ITAG tags, turned into RT64 transform-group IDs while RT64
 * reads each display list.  Host side only: the game's display lists and
 * RDRAM are never changed. */
#ifndef LIVE_INTERP_H
#define LIVE_INTERP_H

#include <cstdint>

namespace RT64 {
struct Application;
struct State;
struct DisplayList;
}

namespace live {

/* on: tags are recorded and applied (video.framerate above the game's own
 * and video.interpolation = tagged); off: RT64's own matching, the GBI
 * untouched (and the game's tags cost one call each) */
void interpTagsSetup(RT64::Application *app);
void interpTagsEnable(bool on);
bool interpTagsOn();
/* video.interpolation = auto with in-between pictures: RT64's own matching
 * for everything but two things, which get the IDs tagged interpolation gives
 * them: the globe's route lines (RT64's matching left them unpaired: they
 * jumped ahead of the turning globe) and the target marker's arrows (2D:
 * never blended, they stepped at the game's rate over a smoothly turning
 * view) */
void interpTagsAutoIds(bool on);
/* around RT64's processDisplayLists of a task (after loadUCodeGBI) */
void interpTagsBeforeTask(unsigned dl, bool mainFrame);
void interpTagsAfterTask(bool mainFrame);
/* PORT_ICUT_IF since the last main frame (cleared by reading) */
bool interpTagsTakeCut();
/* --interp-log: log jumps (a tagged object that teleported) and per-frame counts */
void interpTagsSetLog(bool on);
/* --tag-test MASK (experiments; see live_interp.cpp g_test) */
void interpTagsSetTest(unsigned mask);
/* one line for the log at exit (tag counts, time spent) */
void interpTagsSummary();

/* Another host feature's look at each GBI command while RT64 reads a task
 * (the orbit camera's backdrop, live_orbit.cpp): the GBI table is wrapped
 * while a hook is set, tags or not.  The hook runs before the command
 * (except G_MTX / G_POPMTX, which it doesn't see); it returns true if it
 * ran the command itself (with RT64's own functions, `originals[op]`, also
 * for commands of its own), false to let it run as usual.  It must leave
 * RDRAM as it found it. */
typedef void (*GbiOriginal)(RT64::State *state, RT64::DisplayList **dl);
typedef bool (*GbiCommandHook)(RT64::State *state, RT64::DisplayList **dl, const GbiOriginal *originals);
/* ... and after every G_MTX RT64 ran (`params`: the command's parameter
 * byte), e.g. to adjust the matrix RT64 now holds */
typedef void (*GbiMatrixHook)(RT64::State *state, uint8_t params);
void interpSetCommandHook(GbiCommandHook hook, GbiMatrixHook mtxHook);
/* widescreen's (live_wide.cpp): a command hook asked after the orbit camera's,
 * and a hook after every G_VTX RT64 ran, whoever ran it (`w1`: the command's
 * address word, `first`: the workload's vertex count before it) */
typedef void (*GbiVertexHook)(RT64::State *state, uint32_t w1, uint32_t first);
/* ... and one before every command, G_MTX and G_POPMTX included, with the
 * command's physical address (the game's PORT_WTAG tags name addresses) */
typedef void (*GbiAddressHook)(RT64::State *state, uint32_t addr);
void interpSetWideHooks(GbiCommandHook hook, GbiVertexHook vtxHook, GbiAddressHook addrHook);
/* the level viewer's (live_viewer.cpp): an address hook asked first, before every command (G_MTX and
 * G_POPMTX too: the tags it follows may name their addresses) */
void interpSetOwnerHook(GbiAddressHook hook);

/* the G_VTX of hud_draw_target_marker's four bouncing arrows (30C70.c: 16
 * vertices at DynamicBuf + 0x1F00, either buffer): `phys` the vertices'
 * physical address, `w0` the command's first word.  Tagged interpolation
 * blends them (their own ID, vertices interpolated); the orbit camera moves
 * them to where its view shows the target (live_orbit.cpp) */
bool interpMarkerVertices(uint32_t phys, uint32_t w0);

}  // namespace live

#endif
