/* bc.exe's analog throttle ([controller] analog_throttle): the game-state side of live_input.h's
 * throttleEncode / throttleGameY (which bc_inputtest tests on their own).
 *  - each controller read (plat_input_read: the player's pad or an --input file's 4th value): the trigger's
 *    pressure goes into the raw pad (the A button, the stick y) for the vehicle and control method now;
 *  - PORT_THROTTLE (45BB0.c cont_update, the digital method's vehicles, after the game turned A into y 80):
 *    the raw y in its place, so veh_throttle's top speed is bands[13] * y / 80.  In play only for a read
 *    this code encoded; in the replay after a level (the game replays the raw pad it recorded) only if this
 *    level's play had encoded reads; never in the attract demos (recorded on an N64).
 * With analog_throttle off nothing is encoded and the hook changes nothing: the game gets its pad as
 * before. */
#include <windows.h>

#include <cstdint>
#include <cstdio>

#include "live_input.h"
#include "live_setup.h"

extern "C" {
#include "plat_host.h"
unsigned plat_frames(void); /* save.c */
}

namespace live {

namespace {

template <typename T> T rd(uint32_t a) {
    return *reinterpret_cast<volatile T *>(uintptr_t(a));
}
constexpr unsigned long long kModeLevel = 0x4, kModeReplay = 0x40, kModeReplayCarrierEnd = 0x400,
                             kModeLevelStart = 0x2000;

const Config *g_cfg;
bool g_log;                 /* --throttle-log */
bool g_lastEncoded;         /* the last read put the pressure in the raw y (the digital method) */
bool g_levelEncoded;        /* ... some read since the level started (its replay needs the hook) */
unsigned g_reads, g_partial, g_full, g_applied, g_replayApplied;

ThrottleState stateNow() {
    ThrottleState s;
    s.play = rd<unsigned long long>(0x80364A90u) == kModeLevel && rd<uint8_t>(0x802E8BD0u) == 0;   /* gGameMode, gPaused */
    s.vehicle = rd<uint8_t>(0x80364456u);   /* gCurVehicle */
    const unsigned player = rd<uint8_t>(0x80364AE8u);   /* playerNumber */
    s.stickSpeed = player <= 3 && s.vehicle < 32 &&
                   (rd<uint32_t>(0x80364BE0u + player * 0x100u) & (1u << s.vehicle)) != 0;   /* gPlayerControlModes */
    return s;
}

void inputHook(unsigned short *button, signed char *x, signed char *y, int trigger) {
    (void) x;
    const unsigned long long mode = rd<unsigned long long>(0x80364A90u);
    if (mode == kModeLevelStart) g_levelEncoded = false;
    g_lastEncoded = false;
    if (g_cfg == nullptr) return;
    const ThrottleState s = stateNow();
    int yy = *y;
    unsigned short b = *button;
    const bool on = g_cfg->analogThrottle && trigger >= 0;
    if (on) {
        throttleEncode(s, b, yy, trigger);
        if (s.play && throttleVehicle(s.vehicle) && !s.stickSpeed) {
            g_lastEncoded = g_levelEncoded = true;
            g_reads++;
            g_partial += yy > 0 && yy < 80;
            g_full += yy == 80;
        }
    }
    /* --throttle-log: every read in play; the speed (gLastSpeed: veh_throttle's last) and where the player is
     * (gCamAtX/Y/Z) */
    if (g_log && s.play)
        host_log("live: throttle read frame %u: vehicle %d%s trigger %d -> buttons %04X y %d; speed %d at %.2f %.2f "
                 "%.2f\n",
                 plat_frames(), s.vehicle, s.stickSpeed ? " (speed on stick)" : "", on ? trigger : -1, b, yy,
                 int(rd<int16_t>(0x803ED400u)), rd<float>(0x80365084u), rd<float>(0x80365088u), rd<float>(0x8036508Cu));
    *button = b;
    *y = (signed char) yy;
}

void gameHook(int rawY, unsigned short *button, signed char *y) {
    const unsigned long long mode = rd<unsigned long long>(0x80364A90u);
    bool use;
    if (mode == kModeLevel) use = g_lastEncoded;
    else if (mode == kModeReplay || mode == kModeReplayCarrierEnd) use = g_levelEncoded;
    else use = false;   /* (the attract demos, the globe's sequences, the map view: the pad as it is) */
    if (!use || !throttleVehicle(rd<uint8_t>(0x80364456u))) return;
    const int ny = throttleGameY(rawY, *button, *y);
    if (ny != *y) (mode == kModeLevel ? g_applied : g_replayApplied)++;
    *y = (signed char) ny;
}

}  // namespace

void throttleSetup(const Config *cfg, bool log) {
    g_cfg = cfg;
    g_log = log;
    host_throttle_input_hook = inputHook;
    host_throttle_hook = gameHook;
}

void throttleSummary() {
    if (g_cfg == nullptr) return;
    if (!g_cfg->analogThrottle && g_reads == 0) return;
    host_log("live: analog throttle (%s, %s): %u reads driving a vehicle, %u with part pressure, %u full; the game's "
             "throttle set from it %u times in play, %u in replays\n",
             g_cfg->analogThrottle ? "on" : "off now", padNames(g_cfg->throttlePad).c_str(), g_reads, g_partial, g_full,
             g_applied, g_replayApplied);
}

}  // namespace live
