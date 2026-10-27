/* bc.exe's controller handling (see live_input.h). */
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>

#include "live_input.h"

extern "C" {
#include "plat_host.h"
unsigned plat_frames(void); /* save.c (input_test.cpp has its own) */
}

namespace live {

/* ---- controllers ---------------------------------------------------------------- */
static SDL_JoystickID instanceOf(SDL_GameController *p) {
    return SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(p));
}

void PadSet::scan() {
    for (size_t i = 0; i < pads.size();)
        if (pads[i].handle == nullptr || !SDL_GameControllerGetAttached(pads[i].handle)) {
            host_log("live: controller %u removed (%s, id %d; frame %u)\n", unsigned(i + 1), pads[i].name.c_str(),
                     int(pads[i].id), plat_frames());
            if (pads[i].handle != nullptr) SDL_GameControllerClose(pads[i].handle);
            pads.erase(pads.begin() + std::ptrdiff_t(i));
        } else {
            i++;
        }
    for (int i = 0; i < SDL_NumJoysticks(); i++) {
        if (!SDL_IsGameController(i)) continue;
        const SDL_JoystickID id = SDL_JoystickGetDeviceInstanceID(i);
        bool have = false;
        for (const PadState &p : pads) have |= p.id == id;
        if (have) continue;
        SDL_GameController *p = SDL_GameControllerOpen(i);
        if (p == nullptr) {
            host_log("live: controller (device %d) can't be opened: %s\n", i, SDL_GetError());
            continue;
        }
        PadState s;
        s.handle = p;
        s.id = instanceOf(p);
        s.name = SDL_GameControllerName(p) ? SDL_GameControllerName(p) : "?";
        pads.push_back(s);
        host_log("live: controller %u: %s (id %d; frame %u)\n", unsigned(pads.size()), s.name.c_str(), int(id),
                 plat_frames());
    }
    sample();
}

void PadSet::sample() {
    for (PadState &p : pads) {
        if (p.handle == nullptr) continue;
        for (int a = 0; a < SDL_CONTROLLER_AXIS_MAX; a++) p.axes[a] = SDL_GameControllerGetAxis(p.handle, SDL_GameControllerAxis(a));
        uint32_t b = 0;
        for (int k = 0; k < SDL_CONTROLLER_BUTTON_MAX && k < 32; k++)
            if (SDL_GameControllerGetButton(p.handle, SDL_GameControllerButton(k))) b |= 1u << k;
        p.buttons = b;
    }
}

void PadSet::closeAll() {
    for (PadState &p : pads)
        if (p.handle != nullptr) SDL_GameControllerClose(p.handle);
    pads.clear();
}

PadSet PadSet::copy() const {
    PadSet s = *this;
    for (PadState &p : s.pads) p.handle = nullptr;
    return s;
}

std::vector<const PadState *> PadSet::active(const Config &c) const {
    std::vector<const PadState *> out;
    if (c.padIndex < 0) {
        for (const PadState &p : pads) out.push_back(&p);
    } else if (size_t(c.padIndex) < pads.size()) {
        out.push_back(&pads[size_t(c.padIndex)]);
    }
    return out;
}

bool PadSet::isActive(const Config &c, SDL_JoystickID which) const {
    for (const PadState *p : active(c))
        if (p->id == which) return true;
    return false;
}

std::vector<std::string> PadSet::names() const {
    std::vector<std::string> out;
    for (const PadState &p : pads) out.push_back(p.name);
    return out;
}

/* ---- the controllers' thread ------------------------------------------------------- */
bool InputThread::start(std::string &error) {
    wake_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    quit_ = false;
    thread_ = std::thread([this] { run(); });
    std::unique_lock<std::mutex> lock(mutex_);
    cv_.wait(lock, [this] { return ready_ || failed_; });
    if (failed_) {
        error = error_;
        lock.unlock();
        thread_.join();
        return false;
    }
    return true;
}

void InputThread::stop() {
    if (!thread_.joinable()) return;
    quit_ = true;
    SetEvent(static_cast<HANDLE>(wake_));
    thread_.join();
    CloseHandle(static_cast<HANDLE>(wake_));
    wake_ = nullptr;
}

PadSet InputThread::latest() {
    std::lock_guard<std::mutex> lock(mutex_);
    return latest_;
}

unsigned InputThread::version() {
    std::lock_guard<std::mutex> lock(mutex_);
    return version_;
}

bool InputThread::waitNewer(unsigned v, int ms) {
    std::unique_lock<std::mutex> lock(mutex_);
    return cv_.wait_for(lock, std::chrono::milliseconds(ms), [&] { return version_ > v; });
}

InputThread::Stats InputThread::take() {
    std::lock_guard<std::mutex> lock(mutex_);
    Stats s = stats_;
    stats_.updates = 0, stats_.maxMs = 0, stats_.slow = 0;
    return s;
}

void InputThread::run() {
    LARGE_INTEGER freq, t0, t1;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&t0);
    SetThreadDescription(GetCurrentThread(), L"bc controllers");
    PadSet pads;
    if (SDL_InitSubSystem(SDL_INIT_GAMECONTROLLER) != 0) {
        std::lock_guard<std::mutex> lock(mutex_);
        failed_ = true;
        error_ = SDL_GetError();
        cv_.notify_all();
        return;
    }
    pads.scan();
    QueryPerformanceCounter(&t1);
    {
        std::lock_guard<std::mutex> lock(mutex_);
        stats_.initMs = double(t1.QuadPart - t0.QuadPart) * 1e3 / double(freq.QuadPart);
        latest_ = pads.copy();
        version_++;
        ready_ = true;
        cv_.notify_all();
    }
    unsigned n = 0;
    while (!quit_) {
        /* about every millisecond, or at once when Windows has messages for SDL's device windows (Raw Input's
         * reports and device changes) */
        HANDLE h = static_cast<HANDLE>(wake_);
        MsgWaitForMultipleObjects(1, &h, FALSE, 1, QS_ALLINPUT);
        if (quit_) break;
        QueryPerformanceCounter(&t0);
        MSG msg;
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        SDL_JoystickUpdate();   /* the drivers' updates and device detection */
        if (testHook) {
            SDL_LockJoysticks();
            testHook();
            SDL_UnlockJoysticks();
        }
        if (++n % 16 == 0) pads.scan();   /* (devices that came or went: ~16 ms) */
        else pads.sample();
        QueryPerformanceCounter(&t1);
        const double ms = double(t1.QuadPart - t0.QuadPart) * 1e3 / double(freq.QuadPart);
        std::lock_guard<std::mutex> lock(mutex_);
        latest_ = pads.copy();
        version_++;
        stats_.updates++;
        stats_.maxMs = std::max(stats_.maxMs, ms);
        stats_.slow += ms > kSlowMs;
        cv_.notify_all();
    }
    pads.closeAll();
    SDL_QuitSubSystem(SDL_INIT_GAMECONTROLLER);
}

/* ---- the N64 pad ------------------------------------------------------------------- */
/* an axis as an N64 stick value (-80..80 at full tilt), 0 inside the dead
 * zone; the stick sensitivity scales it (100 %: the full N64 range) */
static int axis(const Config &c, const PadState *pad, int a, bool stick) {
    int v = pad->axis(a);
    if (v > -c.deadzone && v < c.deadzone) return 0;
    if (!stick || c.sensitivity == 100) return v * 80 / 32767;
    return int(double(v) * 80.0 * c.sensitivity / (100.0 * 32767.0));
}

static bool padActive(const Config &c, const PadState *pad, const PadInput &in) {
    if (in.button >= 0) return pad->button(in.button);
    if (in.axis == SDL_CONTROLLER_AXIS_TRIGGERLEFT || in.axis == SDL_CONTROLLER_AXIS_TRIGGERRIGHT)
        return pad->axis(in.axis) > 12000;
    return axis(c, pad, in.axis, false) * in.dir > 40;
}

/* ---- the analog throttle (live_input.h) ------------------------------------------------ */
int triggerAmount(int v, int deadzone, double curve) {
    deadzone = std::max(0, std::min(32000, deadzone));
    if (v <= deadzone) return 0;
    double t = std::min(1.0, double(v - deadzone) / double(32767 - deadzone));
    if (curve > 0 && curve != 1.0) t = std::pow(t, curve);
    return std::max(1, std::min(255, int(std::lround(t * 255.0))));
}

bool throttleVehicle(int v) {
    switch (v) {
        case 1: case 3: case 4: case 5: case 7: case 8: case 10: case 11: case 13: case 14: case 15: case 17: case 18:
            return true;
        default:   /* the driver on foot, Thunderfist, the Crane, the J-Bomb, the Cyclone Suit; others */
            return false;
    }
}

static int throttleY(int trigger) {   /* 0..255 -> 0..80, 1 at the least pressure */
    return trigger <= 0 ? 0 : std::max(1, std::min(80, (trigger * 80 + 127) / 255));
}

void throttleEncode(const ThrottleState &s, unsigned short &button, int &y, int trigger) {
    if (trigger < 0) return;
    const unsigned short kA = 0x8000;
    if (!s.play || !throttleVehicle(s.vehicle)) {   /* the A button */
        if (trigger >= kTriggerPress) button |= kA;
        return;
    }
    if (s.stickSpeed) {   /* the game's analog method: the larger of the stick and the trigger */
        if (button & kA) return;
        const int t = throttleY(trigger);
        if (t >= 80) button |= kA;   /* (full: the A button itself) */
        else y = std::max(y, t);
        return;
    }
    /* digital method: the raw y (unused by the game here) carries the pressure; the A button full */
    if (button & kA) {
        y = 80;
    } else if (trigger > 0) {
        button |= kA;
        y = throttleY(trigger);
    } else {
        y = 0;
    }
}

int throttleGameY(int rawY, unsigned short button, int y) {
    /* (y 80: the game's A; the raw y 80 or 0: full or not ours) */
    if ((button & 0x8000) && y == 80 && rawY >= 1 && rawY < 80) return rawY;
    return y;
}

N64Pad readPad(const Config &c, const PadSet &pads, const Uint8 *k, bool altHeld) {
    /* Alt+Enter switches fullscreen: that Enter isn't a game key */
    const bool altEnter = k != nullptr && altHeld && k[SDL_SCANCODE_RETURN];
    auto keyDown = [&](int act) {
        if (k == nullptr) return false;
        for (SDL_Scancode sc : c.keys[act])
            if (k[sc] && !(altEnter && sc == SDL_SCANCODE_RETURN)) return true;
        return false;
    };
    unsigned b = 0;
    int x = 0, y = 0;
    for (int i = 0; i < kButtons; i++)
        if (keyDown(i)) b |= kButtonBits[i];
    if (keyDown(A_STICK_LEFT)) x -= 80;
    if (keyDown(A_STICK_RIGHT)) x += 80;
    if (keyDown(A_STICK_UP)) y += 80;
    if (keyDown(A_STICK_DOWN)) y -= 80;
    const bool analog = c.analogThrottle && !c.throttlePad.empty();
    auto isThrottle = [&](const PadInput &in) {
        for (const PadInput &t : c.throttlePad)
            if (t.button == in.button && t.axis == in.axis && t.dir == in.dir) return true;
        return false;
    };
    int trigger = analog ? 0 : -1;
    for (const PadState *pad : pads.active(c)) {
        for (int i = 0; i < kButtons; i++)
            for (const PadInput &in : c.pad[i])
                if (!(analog && isThrottle(in)) && padActive(c, pad, in)) b |= kButtonBits[i];
        if (analog)
            for (const PadInput &in : c.throttlePad) {
                int v = 0;
                if (in.button >= 0) v = pad->button(in.button) ? 255 : 0;
                else if (in.axis == SDL_CONTROLLER_AXIS_TRIGGERLEFT || in.axis == SDL_CONTROLLER_AXIS_TRIGGERRIGHT)
                    v = triggerAmount(pad->axis(in.axis), c.throttleDeadzone, c.throttleCurve);
                else
                    v = triggerAmount(std::max(0, pad->axis(in.axis) * in.dir), std::max(c.throttleDeadzone, c.deadzone),
                                      c.throttleCurve);
                trigger = std::max(trigger, v);
            }
        if (c.padStick >= 0) {
            const bool right = c.padStick == 1;
            int lx = axis(c, pad, right ? SDL_CONTROLLER_AXIS_RIGHTX : SDL_CONTROLLER_AXIS_LEFTX, true);
            int ly = -axis(c, pad, right ? SDL_CONTROLLER_AXIS_RIGHTY : SDL_CONTROLLER_AXIS_LEFTY, true);
            if (lx != 0 || ly != 0) x = lx, y = ly;
        }
    }
    N64Pad p;
    p.button = (unsigned short) b;
    p.x = std::max(-80, std::min(80, x));
    p.y = std::max(-80, std::min(80, y));
    p.trigger = trigger;
    return p;
}

bool comboHeld(const Config &c, const PadSet &pads, const std::vector<int> &buttons) {
    if (buttons.empty()) return false;
    for (const PadState *pad : pads.active(c)) {
        bool all = true;
        for (int b : buttons) all &= pad->button(b);
        if (all) return true;
    }
    return false;
}

bool menuComboHeld(const Config &c, const PadSet &pads) {
    return comboHeld(c, pads, c.menuPad);
}

/* ---- the gate after the menu ------------------------------------------------------- */
std::string buttonNames(unsigned short b) {
    std::string s;
    for (int i = 0; i < kButtons; i++)
        if (b & kButtonBits[i]) s += (s.empty() ? "" : "+") + std::string(kActionLabels[i]);
    return s.empty() ? "-" : s;
}

void InputGate::close(const N64Pad &now) {
    buttons = now.button;
    stick = now.x != 0 || now.y != 0;
    trigger = now.trigger > 0;
    polls = 0;
    if (closed())
        host_log("live: input gate closed: held %s%s (frame %u)\n", buttonNames(buttons).c_str(),
                 stick ? ", stick" : "", plat_frames());
}

void InputGate::apply(N64Pad &p) {
    if (!closed()) return;
    buttons &= p.button;                /* a let-go button passes from now on */
    if (stick && p.x == 0 && p.y == 0) stick = false;
    if (trigger && p.trigger <= 0) trigger = false;
    if (++polls >= kMaxPolls && closed()) {
        host_log("live: input gate opened after %u reads with %s%s%s still held (frame %u)\n", polls,
                 buttonNames(buttons).c_str(), stick ? ", stick" : "", trigger ? ", throttle" : "", plat_frames());
        buttons = 0;
        stick = trigger = false;
        return;
    }
    p.button &= (unsigned short) ~buttons;
    if (stick) p.x = p.y = 0;
    if (trigger) p.trigger = 0;
    if (!closed()) host_log("live: input gate opened (everything let go after %u reads, frame %u)\n", polls, plat_frames());
}

}  // namespace live
