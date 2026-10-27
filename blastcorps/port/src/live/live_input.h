/* bc.exe's controller handling (live_input.cpp): the game controllers SDL
 * knows, the N64 pad made from them and the keyboard, and the gate that
 * keeps the inputs that closed the settings menu from reaching the game.
 * No window or renderer: tools/.. bc_inputtest.exe (src/live/input_test.cpp)
 * drives it with SDL's virtual controllers. */
#ifndef LIVE_INPUT_H
#define LIVE_INPUT_H

#include <atomic>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <SDL.h>

#include "live_setup.h"

namespace live {

/* one controller's state, as last sampled */
struct PadState {
    SDL_GameController *handle = nullptr;   /* (the thread that owns the controllers only; null in copies) */
    SDL_JoystickID id = -1;
    std::string name;
    Sint16 axes[SDL_CONTROLLER_AXIS_MAX] = {};
    uint32_t buttons = 0;                   /* bit per SDL_GameControllerButton */
    int axis(int a) const { return a >= 0 && a < SDL_CONTROLLER_AXIS_MAX ? axes[a] : 0; }
    bool button(int b) const { return b >= 0 && b < 32 && ((buttons >> b) & 1) != 0; }
};

/* Every game controller SDL reports is opened, in the order they appeared.
 * A controller that disconnects and comes back (new SDL instance id) is
 * closed and opened again, so `pad = 1` follows it.  The state the game and
 * the camera read is the one `sample` took (plain data: a copy can be read
 * on another thread, no SDL call). */
struct PadSet {
    std::vector<PadState> pads;

    void scan();                 /* (SDL) close detached ones, open new ones, then sample; logs both */
    void sample();               /* (SDL) every controller's buttons and axes now */
    void closeAll();
    std::vector<const PadState *> active(const Config &c) const;   /* the ones that play */
    bool isActive(const Config &c, SDL_JoystickID which) const;
    std::vector<std::string> names() const;
    PadSet copy() const;         /* the data, without the handles */
};

/* bc.exe's controllers on a thread of their own.  SDL's controller work -
 * device detection (DirectInput's device enumeration, XInput, HIDAPI and
 * Raw Input opening new devices: hundreds of milliseconds with a Bluetooth
 * pad connecting or any HID device coming or going) and its device window's
 * messages - all ran inside the main thread's SDL_PollEvent before, with
 * the game waiting.  This thread starts SDL's game controller subsystem
 * itself (so SDL's device windows belong to it and it dispatches their
 * messages), updates SDL's controllers about every millisecond (and at
 * once when Windows has input for them), rescans and samples them, and
 * publishes the state; the main thread only copies the latest state
 * (`latest`), never waiting for SDL.  SDL's controller events still reach
 * the main thread's event queue (the menu's bindings, the menu buttons).
 * bc.exe sets SDL_HINT_AUTO_UPDATE_JOYSTICKS = 0 so SDL_PumpEvents leaves
 * the controllers to this thread. */
class InputThread {
public:
    struct Stats {
        double initMs = 0;           /* starting SDL's controller subsystem (with the first detection) */
        unsigned updates = 0;        /* the thread's updates since the last take */
        double maxMs = 0;            /* the longest (SDL_JoystickUpdate with detection, the rescan, the sample) */
        unsigned slow = 0;           /* updates over kSlowMs */
    };
    static constexpr double kSlowMs = 20.0;
    bool start(std::string &error);  /* waits until the controllers are first scanned; false: SDL failed */
    void stop();
    bool running() const { return thread_.joinable(); }
    PadSet latest();                 /* the newest sample (a copy; never waits on SDL) */
    unsigned version();              /* samples published so far */
    bool waitNewer(unsigned version, int ms);   /* (testing) a sample after `version`, or false after ms */
    Stats take();                    /* and restart the counts */
    /* (testing) called in the thread's update with SDL's controller lock held, e.g. to be slow */
    std::function<void()> testHook;

private:
    void run();
    std::thread thread_;
    std::mutex mutex_;
    std::condition_variable cv_;
    PadSet latest_;
    unsigned version_ = 0;
    Stats stats_;
    bool ready_ = false, failed_ = false;
    std::string error_;
    std::atomic<bool> quit_{false};
    void *wake_ = nullptr;           /* (an event: stop) */
};

/* the N64 controller's state */
struct N64Pad {
    unsigned short button = 0;
    int x = 0, y = 0;            /* -80..80 */
    int trigger = -1;            /* the analog throttle's pressure 0..255 ([controller] analog_throttle; -1: off) */
    bool idle() const { return button == 0 && x == 0 && y == 0 && trigger <= 0; }
};

/* the N64 pad from the bindings: keyboard (`keys`: SDL_GetKeyboardState, or
 * nullptr) and the playing controllers (their last sample).  With analog_throttle on, the throttle inputs
 * give `trigger` (and press no N64 button themselves) */
N64Pad readPad(const Config &c, const PadSet &pads, const Uint8 *keys, bool altHeld);

/* ---- the analog throttle ([controller] analog_throttle; live_throttle.cpp) ----------------------------
 * The game's vehicles (all but the driver on foot, Thunderfist, the Crane, the J-Bomb and the Cyclone
 * Suit) drive with 62740.c veh_throttle: A (or the stick's y in the "speed on 3D stick" control method)
 * becomes the stick y gPadStickY, +80 in the digital method; each frame the speed gains the vehicle's band
 * acceleration while it is under the top speed bands[13] * y / 80.  So a y of 80 t drives at the share t
 * of the top speed (the game's own analog control method does that with the stick).  The trigger's
 * pressure gets there as the raw pad's stick y (recorded and replayed by the game with the pad):
 *  - digital method: A pressed and the raw y = 80 t (at least 1; 80 at full pressure or with the A button
 *    itself); released: no A, y 0.  PORT_THROTTLE (after the game made y = 80 from A) puts the raw y in;
 *  - "speed on 3D stick": y = the larger of the stick's and 80 t (full pressure: A, as the button);
 *  - anything else (other characters, menus, pause): the trigger is the A button (pressed from
 *    kTriggerPress). */
constexpr int kTriggerPress = 96;   /* 0..255: about the digital trigger's 12000 of 32767 */
/* the pressure 0..255 of a trigger axis value 0..32767 through the dead zone and the curve (an exponent:
 * 1 linear, 2 finer at light pressure) */
int triggerAmount(int axisValue, int deadzone, double curve);
/* the game state the encoding depends on */
struct ThrottleState {
    bool play = false;           /* gGameMode MODE_LEVEL, not paused */
    int vehicle = 0;             /* gCurVehicle */
    bool stickSpeed = false;     /* the vehicle's control method bit: "speed on 3D stick" */
};
bool throttleVehicle(int vehicle);   /* driven by veh_throttle from A / the stick's y */
/* the raw pad as the game will read it (plat_input_read) with the trigger's pressure (-1: unchanged) */
void throttleEncode(const ThrottleState &s, unsigned short &button, int &y, int trigger);
/* PORT_THROTTLE: the game's processed y (digital method) from the raw y */
int throttleGameY(int rawY, unsigned short button, int y);
/* (live_throttle.cpp, bc.exe) the hooks, reading `cfg` as it changes; `log`: --throttle-log (every read in
 * play); the exit summary */
void throttleSetup(const Config *cfg, bool log);
void throttleSummary();

/* all of a playing controller's menu buttons held */
bool menuComboHeld(const Config &c, const PadSet &pads);
/* all of these buttons held on a playing controller (false if none given) */
bool comboHeld(const Config &c, const PadSet &pads, const std::vector<int> &buttons);

/* After the settings menu closes, the buttons and stick held at that moment
 * don't reach the game until each is let go (each button separately; the
 * stick until it is back in its dead zone), and at most `kMaxPolls` reads:
 * a stuck key, a drifting stick or a trigger that doesn't rest at 0 can't
 * keep the game from hearing the controller.  Inputs pressed afterwards
 * pass at once. */
struct InputGate {
    static constexpr unsigned kMaxPolls = 60;   /* one second of retraces */
    unsigned short buttons = 0;  /* still blocked */
    bool stick = false;
    bool trigger = false;        /* the analog throttle */
    unsigned polls = 0;
    bool closed() const { return buttons != 0 || stick || trigger; }
    void close(const N64Pad &now);
    void apply(N64Pad &p);       /* filters p; logs when the gate opens */
};

std::string buttonNames(unsigned short b);

}  // namespace live

#endif
