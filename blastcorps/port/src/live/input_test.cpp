/* bc_inputtest.exe: tests of bc.exe's controller handling (live_input.cpp)
 * with SDL's virtual game controllers: no window, no ROM, no renderer.
 *   - a controller is found, its buttons and stick make the N64 pad;
 *   - unplugged and plugged in again (a new SDL instance id): `pad = 1`
 *     follows it, also when the device events are missed (rescan only);
 *   - the input gate after the settings menu: each held input is blocked
 *     only until it is let go, new inputs pass at once, and nothing held
 *     (a stuck key, a drifting stick, a resting trigger) keeps it closed
 *     longer than InputGate::kMaxPolls reads;
 *   - the keyboard bindings, the menu's button combination;
 *   - bc.ini settings (live_setup.cpp): parsing, defaults, and saving a
 *     menu change and reading it back (files kept in memory).
 * Exit status 0 if every check passes. */
#include <algorithm>
#include <atomic>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <vector>

#include <SDL.h>

#include "live_input.h"
#include "live_setup.h"

/* files in memory (the bc.ini tests: configLoad / configSave) */
static std::map<std::string, std::string> &files() {
    static std::map<std::string, std::string> m;
    return m;
}

/* the platform services live_input.cpp / live_setup.cpp use */
extern "C" {
static unsigned g_frame;
unsigned plat_frames(void) { return g_frame; }
void host_log(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    fputs("  log: ", stdout);
    vprintf(fmt, ap);
    va_end(ap);
}
void *host_read_file(const char *path, unsigned *size) {
    auto it = files().find(path);
    if (it == files().end()) return nullptr;
    char *p = static_cast<char *>(malloc(it->second.size() + 1));
    memcpy(p, it->second.data(), it->second.size());
    *size = unsigned(it->second.size());
    return p;
}
int host_write_file(const char *path, const void *data, unsigned size) {
    files()[path].assign(static_cast<const char *>(data), size);
    return 0;
}
int rom_normalise(unsigned char *, size_t) { return -1; }
int rom_shift_check(const unsigned char *, size_t, char *, size_t) { return 0; }
long rom_unmapped_shift(const unsigned char *, size_t) { return -1; }
}

namespace {

int g_fail, g_pass;

void check(bool ok, const char *what) {
    printf("%s %s\n", ok ? "ok  " : "FAIL", what);
    (ok ? g_pass : g_fail)++;
}

/* The test's virtual controllers, by SDL instance id. A real controller on this PC can still show up beside
 * them: the hints in main() turn off XInput, DirectInput, Raw Input and HIDAPI, but SDL 2.26 has no switch for
 * its Windows.Gaming.Input driver ("SDL_JOYSTICK_WGI" is a later SDL's), and that driver lists an Xbox pad
 * a moment after SDL starts (seen: "Xbox One Game Controller", GUID driver byte 'w', ~1 s in).
 * So a virtual pad is found and unplugged by its instance id, never by a device index (a real device listed
 * by an earlier SDL driver comes first and moves the indexes: SDL_JoystickDetachVirtual(index) then misses). */
std::vector<SDL_JoystickID> g_ours;

bool ours(SDL_JoystickID id) {
    return std::find(g_ours.begin(), g_ours.end(), id) != g_ours.end();
}

/* a virtual game controller (SDL maps it like an Xbox pad): its instance id, -1 if it can't be attached */
SDL_JoystickID attach() {
    SDL_VirtualJoystickDesc d;
    SDL_zero(d);
    d.version = SDL_VIRTUAL_JOYSTICK_DESC_VERSION;
    d.type = SDL_JOYSTICK_TYPE_GAMECONTROLLER;
    d.naxes = SDL_CONTROLLER_AXIS_MAX;
    d.nbuttons = SDL_CONTROLLER_BUTTON_MAX;
    d.name = "virtual test pad";
    if (SDL_JoystickAttachVirtualEx(&d) < 0) return -1;
    SDL_JoystickID id = -1;   /* the newest virtual device (instance ids only grow) */
    for (int i = 0; i < SDL_NumJoysticks(); i++)
        if (SDL_JoystickIsVirtual(i)) id = std::max(id, SDL_JoystickGetDeviceInstanceID(i));
    if (id >= 0) g_ours.push_back(id);
    return id;
}

/* unplug a virtual controller by its instance id */
bool detach(SDL_JoystickID id) {
    for (int i = 0; i < SDL_NumJoysticks(); i++)
        if (SDL_JoystickGetDeviceInstanceID(i) == id) return SDL_JoystickDetachVirtual(i) == 0;
    return false;
}

bool has(const live::PadSet &s, SDL_JoystickID id) {
    for (const live::PadState &p : s.pads)
        if (p.id == id) return true;
    return false;
}

/* the test's own pads of a set (a copy: the main thread's view of InputThread::latest()) */
live::PadSet oursIn(const live::PadSet &s) {
    live::PadSet o;
    for (const live::PadState &p : s.pads)
        if (ours(p.id)) o.pads.push_back(p);
    return o;
}

/* PadSet::scan, then any other controller closed and dropped (the tests count and index the pads) */
void scanOurs(live::PadSet &s) {
    s.scan();
    for (size_t i = 0; i < s.pads.size();)
        if (ours(s.pads[i].id)) {
            i++;
        } else {
            if (s.pads[i].handle != nullptr) SDL_GameControllerClose(s.pads[i].handle);
            s.pads.erase(s.pads.begin() + std::ptrdiff_t(i));
        }
}

/* (a virtual joystick's trigger axis rests at -32768: the controller API maps it to 0) */
const Sint16 kTriggerRest = -32768;

SDL_Joystick *joyOf(live::PadSet &s, size_t i) {
    SDL_Joystick *j = i < s.pads.size() ? SDL_GameControllerGetJoystick(s.pads[i].handle) : nullptr;
    static SDL_Joystick *seen[16];
    bool known = false;
    for (SDL_Joystick *k : seen) known |= k == j;
    if (j != nullptr && !known) {   /* a new one: triggers at rest */
        SDL_JoystickSetVirtualAxis(j, SDL_CONTROLLER_AXIS_TRIGGERLEFT, kTriggerRest);
        SDL_JoystickSetVirtualAxis(j, SDL_CONTROLLER_AXIS_TRIGGERRIGHT, kTriggerRest);
        for (SDL_Joystick *&k : seen)
            if (k == nullptr) {
                k = j;
                break;
            }
    }
    return j;
}

live::PadSet *g_sampled;   /* the set pump() samples after SDL's update */

void pump() {
    SDL_Event e;
    SDL_PumpEvents();
    while (SDL_PollEvent(&e)) {
    }
    if (g_sampled != nullptr) g_sampled->sample();
}

double nowMs() {
    return double(SDL_GetPerformanceCounter()) * 1e3 / double(SDL_GetPerformanceFrequency());
}

}  // namespace

int main(int, char **) {
    setvbuf(stdout, nullptr, _IONBF, 0);
    SDL_SetMainReady();
    SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");
    /* as few real controllers as SDL lets us turn off (a real pad can still be listed: see g_ours) */
    for (const char *h : {SDL_HINT_XINPUT_ENABLED, SDL_HINT_JOYSTICK_RAWINPUT, SDL_HINT_JOYSTICK_HIDAPI,
                          "SDL_JOYSTICK_WGI", "SDL_DIRECTINPUT_ENABLED"})
        SDL_SetHint(h, "0");
    if (SDL_Init(SDL_INIT_GAMECONTROLLER | SDL_INIT_EVENTS) != 0) {
        printf("FAIL SDL_Init: %s\n", SDL_GetError());
        return 1;
    }
    live::Config c;
    live::configDefaults(c);

    /* video.framerate (the frame rate option): parsing and the names written back */
    {
        live::Config f;
        live::configDefaults(f);
        check(f.framerate == 0 && live::configValue(f, {"video", "framerate"}) == "original", "framerate default: original");
        check(live::configSet(f, "video.framerate=144") && f.framerate == 144 &&
                  live::configValue(f, {"video", "framerate"}) == "144", "framerate 144");
        check(live::configSet(f, "video.framerate=display") && f.framerate == -1 &&
                  live::configValue(f, {"video", "framerate"}) == "display", "framerate display");
        check(live::configSet(f, "video.framerate=original") && f.framerate == 0, "framerate original");
        check(!live::configSet(f, "video.framerate=10") && f.framerate == 0, "framerate 10 rejected (below 30)");
        check(!live::configSet(f, "video.framerate=fast") && f.framerate == 0, "framerate 'fast' rejected");
    }
    /* video.hud_position: centered (the default) or edges */
    {
        live::Config f;
        live::configDefaults(f);
        check(f.hudPosition == 0 && live::configValue(f, {"video", "hud_position"}) == "centered",
              "hud position default: centered");
        check(live::configSet(f, "video.hud_position=edges") && f.hudPosition == 1 &&
                  live::configValue(f, {"video", "hud_position"}) == "edges", "hud position edges");
        check(live::configSet(f, "video.hud_position=centred") && f.hudPosition == 0, "hud position centred");
        check(!live::configSet(f, "video.hud_position=top") && f.hudPosition == 0, "hud position 'top' rejected");
    }
    /* video.interpolation, the frame-rate counter and its bindings */
    {
        live::Config f;
        live::configDefaults(f);
        check(f.interpolation == 1 && live::configValue(f, {"video", "interpolation"}) == "tagged",
              "interpolation default: tagged");
        check(live::configSet(f, "video.interpolation=auto") && f.interpolation == 0 &&
                  live::configValue(f, {"video", "interpolation"}) == "auto", "interpolation auto");
        check(!live::configSet(f, "video.interpolation=maybe") && f.interpolation == 0, "interpolation 'maybe' rejected");
        check(f.fpsCounter == 0 && live::configValue(f, {"video", "fps_counter"}) == "off", "fps counter default: off");
        check(live::configSet(f, "video.fps_counter=detailed") && f.fpsCounter == 2 &&
                  live::configValue(f, {"video", "fps_counter"}) == "detailed", "fps counter detailed");
        check(live::configSet(f, "video.fps_counter=on") && f.fpsCounter == 1, "fps counter on");
        check(f.fpsCorner == 0 && live::configSet(f, "video.fps_counter_corner=bottom-right") && f.fpsCorner == 3 &&
                  live::configValue(f, {"video", "fps_counter_corner"}) == "bottom-right", "fps counter corner");
        check(!live::configSet(f, "video.fps_counter_corner=middle") && f.fpsCorner == 3, "corner 'middle' rejected");
        check(f.keys[live::A_FPS].size() == 1 && f.keys[live::A_FPS][0] == SDL_SCANCODE_F2, "fps counter key F2");
        check(f.fpsPad.empty() && live::configSet(f, "controller.fps_counter=back+y") && f.fpsPad.size() == 2 &&
                  live::configValue(f, {"controller", "fps_counter"}) == "back+y", "fps counter buttons back+y");
        check(f.menuPad.size() == 2, "menu buttons untouched");
    }
    /* [mods]: none by default (the game as it is), a list, the folder */
    {
        live::Config f;
        live::configDefaults(f);
        check(f.mods.empty() && f.modsDir == "mods" && live::configValue(f, {"mods", "enabled"}).empty(),
              "mods default: none enabled, folder mods");
        check(live::configSet(f, "mods.enabled=wide start, other.bcmod ,") && f.mods.size() == 2 &&
                  f.mods[0] == "wide start" && f.mods[1] == "other.bcmod" &&
                  live::configValue(f, {"mods", "enabled"}) == "wide start, other.bcmod",
              "mods.enabled: a list (spaces kept inside names, empty items dropped)");
        check(live::configSet(f, "mods.folder=D:\\my mods") && f.modsDir == "D:\\my mods" &&
                  live::configSet(f, "mods.enabled=") && f.mods.empty(),
              "mods.folder, and an empty list");
        check(!live::configSet(f, "mods.colour=1"), "mods: an unknown key rejected");
    }
    /* [game] debug_cheats and cheats_save: defaults, values, bad values, the new file's text, an older file
     * without them (no migration: nothing rewritten), the menu's change saved; the debug_cheats key */
    {
        live::Config f;
        live::configDefaults(f);
        check(!f.debugCheats && f.cheatsSaveSeparate && live::configValue(f, {"game", "debug_cheats"}) == "0" &&
                  live::configValue(f, {"game", "cheats_save"}) == "separate",
              "debug cheats default: off, cheats_save separate");
        check(live::configSet(f, "game.debug_cheats=1") && f.debugCheats &&
                  live::configValue(f, {"game", "debug_cheats"}) == "1" && live::configSet(f, "game.debug_cheats=off") &&
                  !f.debugCheats && live::configSet(f, "game.debug_cheats=yes") && f.debugCheats,
              "game.debug_cheats 1 / off / yes");
        check(!live::configSet(f, "game.debug_cheats=maybe") && f.debugCheats, "game.debug_cheats 'maybe' rejected");
        check(live::configSet(f, "game.cheats_save=normal") && !f.cheatsSaveSeparate &&
                  live::configValue(f, {"game", "cheats_save"}) == "normal" &&
                  live::configSet(f, "game.cheats_save=Separate") && f.cheatsSaveSeparate,
              "game.cheats_save normal / Separate");
        check(!live::configSet(f, "game.cheats_save=elsewhere") && f.cheatsSaveSeparate &&
                  !live::configSet(f, "game.cheats_save=1") && f.cheatsSaveSeparate,
              "game.cheats_save 'elsewhere' and '1' rejected");
        check(f.keys[live::A_CHEATS].size() == 1 && f.keys[live::A_CHEATS][0] == SDL_SCANCODE_F3 && f.cheatsPad.empty() &&
                  live::configValue(f, {"keyboard", "debug_cheats"}) == "F3" &&
                  live::configValue(f, {"controller", "debug_cheats"}).empty(),
              "debug_cheats key F3, no controller buttons");
        check(live::configSet(f, "controller.debug_cheats=back+x") && f.cheatsPad.size() == 2 &&
                  live::configValue(f, {"controller", "debug_cheats"}) == "back+x" && f.fpsPad.empty() &&
                  f.menuPad.size() == 2,
              "debug_cheats buttons back+x (the menu's and the counter's untouched)");
        check(!live::configSet(f, "controller.debug_cheats=back+nothing"), "debug_cheats buttons: a bad name rejected");

        live::Config n;
        check(live::configLoad(n, "cheats_new.ini") && !n.debugCheats && n.cheatsSaveSeparate, "new bc.ini: cheats off");
        const std::string &t = files()["cheats_new.ini"];
        check(t.find("; In a level: C-right completes it, C-up + C-right fails it, Z + C-right is a perfect\r\n") !=
                      std::string::npos &&
                  t.find("\r\ndebug_cheats = 0\r\n") != std::string::npos &&
                  t.find("\r\ncheats_save = separate\r\n") != std::string::npos &&
                  t.find("\r\ndebug_cheats = F3\r\n") != std::string::npos &&
                  t.find("controller_pak = 0\r\n; The game's own debug cheats") != std::string::npos,
              "new bc.ini: the buttons in the comment, debug_cheats = 0, cheats_save = separate, the F3 key");

        const std::string before = "[game]\r\nrom = x.z64\r\nsettings_version = 2\r\n\r\n[keyboard]\r\nc_up = F3\r\n";
        files()["cheats_old.ini"] = before;
        live::Config o;
        check(live::configLoad(o, "cheats_old.ini") && o.warnings.empty() && !o.debugCheats && o.cheatsSaveSeparate &&
                  files()["cheats_old.ini"] == before,
              "bc.ini without the keys (version 2): the defaults, no warning, nothing rewritten");
        check(o.keys[live::A_CHEATS].empty() && o.keys[live::A_CUP].size() == 1 && o.keys[live::A_CUP][0] == SDL_SCANCODE_F3,
              "... its F3 is c_up's: the debug_cheats key is left unbound");
        o.debugCheats = true;   /* the menu's change */
        check(live::configSave(o, {{"game", "debug_cheats"}}) &&
                  files()["cheats_old.ini"] == "[game]\r\nrom = x.z64\r\nsettings_version = 2\r\ndebug_cheats = 1\r\n\r\n"
                                              "[keyboard]\r\nc_up = F3\r\n",
              "menu change saved at the end of [game]");
        live::Config r;
        check(live::configLoad(r, "cheats_old.ini") && r.debugCheats && r.warnings.empty(), "read back: on");
        files()["cheats_bad.ini"] = "[game]\r\ndebug_cheats = 2\r\ncheats_save = nowhere\r\n";
        live::Config b;
        check(live::configLoad(b, "cheats_bad.ini") && b.warnings.size() == 2 && !b.debugCheats && b.cheatsSaveSeparate,
              "bad values: two warnings, the defaults used");
    }
    /* video.present_pacing: timer (the default) or vsync; bc.ini without the key
     * (one from before it existed) gives timer; the menu's change is saved and
     * read back */
    {
        live::Config f;
        live::configDefaults(f);
        check(!f.vsyncPacing && live::configValue(f, {"video", "present_pacing"}) == "timer",
              "present pacing default: timer");
        check(live::configSet(f, "video.present_pacing=vsync") && f.vsyncPacing &&
                  live::configValue(f, {"video", "present_pacing"}) == "vsync", "present pacing vsync");
        check(live::configSet(f, "video.present_pacing=Timer") && !f.vsyncPacing, "present pacing timer");
        check(!live::configSet(f, "video.present_pacing=fast") && !f.vsyncPacing, "present pacing 'fast' rejected");
        check(live::configSet(f, "video.present_pacing=VRR") && !f.vsyncPacing && f.pacingOther == 2 &&
                  live::configValue(f, {"video", "present_pacing"}) == "vrr", "present pacing vrr");
        check(live::configSet(f, "video.present_pacing=auto") && f.pacingOther == 3 &&
                  live::configValue(f, {"video", "present_pacing"}) == "auto", "present pacing auto");
        check(live::configSet(f, "video.present_pacing=vsync") && f.vsyncPacing && f.pacingOther == 0 &&
                  live::configSet(f, "video.present_pacing=timer") && !f.vsyncPacing && f.pacingOther == 0,
              "present pacing back to vsync, then timer");

        const std::string old = "old.ini";   /* no present_pacing line */
        files()[old] = "[game]\r\nrom = x.z64\r\n\r\n[video]\r\napi = d3d12\r\nvsync = 1\r\n"
                       "framerate = 60\r\n\r\n[audio]\r\nvolume = 80\r\n";
        live::Config g;
        check(live::configLoad(g, old) && g.warnings.empty() && !g.vsyncPacing && g.framerate == 60,
              "bc.ini without present_pacing: timer, no warnings");
        g.vsyncPacing = true;   /* the menu's choice */
        check(live::configSave(g, {{"video", "present_pacing"}}), "menu change saved");
        const std::string &t = files()[old];
        check(t == "[game]\r\nrom = x.z64\r\nsettings_version = 2\r\n\r\n[video]\r\napi = d3d12\r\nvsync = 1\r\n"
                   "framerate = 60\r\npresent_pacing = vsync\r\n\r\n[audio]\r\nvolume = 80\r\n",
              "saved at the end of [video] (before its blank line), the rest kept");
        live::Config h;
        check(live::configLoad(h, old) && h.warnings.empty() && h.vsyncPacing && h.framerate == 60,
              "read back: vsync");
        h.vsyncPacing = false;
        live::configSave(h, {{"video", "present_pacing"}});
        live::Config k;
        check(live::configLoad(k, old) && !k.vsyncPacing &&
                  files()[old].find("present_pacing = timer\r\n") != std::string::npos &&
                  files()[old].find("present_pacing = vsync") == std::string::npos,
              "changed back to timer: the same line rewritten");

        const std::string fresh = "new.ini";   /* no file: created with the defaults */
        live::Config n;
        check(live::configLoad(n, fresh) && !n.vsyncPacing &&
                  files()[fresh].find("present_pacing = timer\r\n") != std::string::npos,
              "new bc.ini: present_pacing = timer");
    }
    /* the orbit camera's zoom bindings: the new default (hold Y + right stick), the old default (Y / left stick
     * click) changed to it when read, a player's own kept; [camera] collision and relative_steering */
    {
        live::Config f;
        live::configDefaults(f);
        check(f.pad[live::A_CAM_ZOOM_IN].empty() && f.pad[live::A_CAM_ZOOM_OUT].empty() &&
                  live::configValue(f, {"controller", "camera_zoom_hold"}) == "y",
              "zoom default: hold Y (no left stick click)");
        files()["oldzoom.ini"] = "[controller]\r\na = a\r\ncamera_zoom_in = y\r\ncamera_zoom_out = leftstick\r\n";
        live::Config g;
        check(live::configLoad(g, "oldzoom.ini") && g.pad[live::A_CAM_ZOOM_IN].empty() &&
                  g.pad[live::A_CAM_ZOOM_OUT].empty() && live::configValue(g, {"controller", "camera_zoom_hold"}) == "y",
              "old default zoom bindings: changed to the new default");
        const std::string &t = files()["oldzoom.ini"];
        check(t.find("camera_zoom_out = \r\n") != std::string::npos && t.find("leftstick") == std::string::npos &&
                  t.find("camera_zoom_hold = y") != std::string::npos && t.find("a = a") != std::string::npos,
              "... and written back, the rest kept");
        files()["ownzoom.ini"] = "[controller]\r\ncamera_zoom_in = y\r\ncamera_zoom_out = dpdown\r\n";
        live::Config h;
        check(live::configLoad(h, "ownzoom.ini") && live::configValue(h, {"controller", "camera_zoom_out"}) == "dpdown" &&
                  live::configValue(h, {"controller", "camera_zoom_in"}) == "y",
              "a player's own zoom bindings are kept");
        check(f.camCollision == 1 && live::configValue(f, {"camera", "collision"}) == "off" && f.camRelativeSteering &&
                  !f.camRecentre && f.camFollow,
              "camera defaults: collision off, relative steering on, no recentring, the view following");
        check(live::configSet(f, "camera.collision=off") && f.camCollision == 1 && live::configSet(f, "camera.collision=strict") &&
                  f.camCollision == 2 && !live::configSet(f, "camera.collision=wobbly") && f.camCollision == 2,
              "camera.collision off / strict, a wrong value rejected");
        check(live::configSet(f, "camera.relative_steering=0") && !f.camRelativeSteering &&
                  live::configValue(f, {"camera", "relative_steering"}) == "0",
              "camera.relative_steering 0");
    }
    /* the settings version: an older bc.ini still at the old defaults (recentre 1, collision smooth) gets the new
     * ones, once; a player's own values stay; a new file has the version */
    {
        live::Config n;
        check(live::configLoad(n, "fresh5.ini") && n.settingsVersion == live::kSettingsVersion &&
                  files()["fresh5.ini"].find("settings_version = 2\r\n") != std::string::npos && !n.camRecentre &&
                  n.camCollision == 1,
              "new bc.ini: settings_version 2, recentre 0, collision off");
        /* 76dd727's file: the old defaults written out */
        files()["v1.ini"] = "[game]\r\nrom = x.z64\r\n\r\n[camera]\r\nmode = orbit\r\nrecentre = 1\r\nrecentre_delay = 1.5\r\n"
                            "relative_steering = 1\r\nrecentre_on_foot = 1\r\ncollision = smooth\r\n\r\n[controller]\r\n"
                            "z = lefttrigger, righttrigger\r\n";
        live::Config g;
        check(live::configLoad(g, "v1.ini") && g.warnings.empty() && !g.camRecentre && g.camCollision == 1 &&
                  g.settingsVersion == 2 && g.cameraMode == 1 && g.camRecentreOnFoot,
              "version 1 at the old defaults: recentre 0, collision off");
        const std::string &t = files()["v1.ini"];
        check(t.find("recentre = 0\r\n") != std::string::npos && t.find("collision = off\r\n") != std::string::npos &&
                  t.find("[game]\r\nrom = x.z64\r\nsettings_version = 2\r\n") != std::string::npos &&
                  t.find("recentre_delay = 1.5\r\n") != std::string::npos && t.find("mode = orbit\r\n") != std::string::npos,
              "... written back with the version, the rest kept");
        /* the player then sets recentre = 1 again: it stays (the version is current) */
        g.camRecentre = true;
        live::configSave(g, {{"camera", "recentre"}});
        live::Config g2;
        check(live::configLoad(g2, "v1.ini") && g2.camRecentre && g2.camCollision == 1,
              "migrated once: recentre set back to 1 afterwards stays");
        /* a version 1 file with the player's own values: kept */
        files()["v1own.ini"] = "[camera]\r\nrecentre = 0\r\ncollision = strict\r\n";
        live::Config h;
        check(live::configLoad(h, "v1own.ini") && !h.camRecentre && h.camCollision == 2 && h.settingsVersion == 2,
              "version 1 with the player's own values (recentre 0, strict): kept");
        /* a version 1 file without the keys: the new defaults, nothing to rewrite but the version */
        files()["v1none.ini"] = "[audio]\r\nvolume = 70\r\n";
        live::Config k;
        check(live::configLoad(k, "v1none.ini") && !k.camRecentre && k.camCollision == 1 &&
                  files()["v1none.ini"] == "[audio]\r\nvolume = 70\r\n\r\n[game]\r\nsettings_version = 2\r\n",
              "version 1 without those keys: the new defaults, the version added");
        /* version 2 with smooth: kept (the player's choice) */
        files()["v2.ini"] = "[game]\r\nsettings_version = 2\r\n[camera]\r\ncollision = smooth\r\nrecentre = 1\r\n";
        live::Config m;
        check(live::configLoad(m, "v2.ini") && m.camCollision == 0 && m.camRecentre, "version 2: smooth and recentre 1 kept");
        check(live::configSet(m, "camera.follow=0") && !m.camFollow && live::configValue(m, {"camera", "follow"}) == "0",
              "camera.follow 0");
    }
    /* the analog throttle: settings, and the pad encoding (live_input.h) */
    {
        live::Config f;
        live::configDefaults(f);
        check(!f.analogThrottle && f.throttlePad.size() == 1 && f.throttlePad[0].axis == SDL_CONTROLLER_AXIS_TRIGGERRIGHT &&
                  live::configValue(f, {"controller", "throttle"}) == "righttrigger" &&
                  live::configValue(f, {"controller", "analog_throttle"}) == "0",
              "analog throttle: off by default, throttle = righttrigger");
        check(live::configSet(f, "controller.analog_throttle=1") && f.analogThrottle &&
                  live::configSet(f, "controller.throttle=lefttrigger") &&
                  f.throttlePad[0].axis == SDL_CONTROLLER_AXIS_TRIGGERLEFT && live::configSet(f, "controller.throttle_curve=2") &&
                  f.throttleCurve == 2.0 && !live::configSet(f, "controller.throttle_curve=9") &&
                  live::configSet(f, "controller.throttle_deadzone=3000") && f.throttleDeadzone == 3000,
              "analog throttle settings parsed (a curve of 9 rejected)");
        check(live::triggerAmount(0, 1500, 1.0) == 0 && live::triggerAmount(1500, 1500, 1.0) == 0 &&
                  live::triggerAmount(1501, 1500, 1.0) == 1 && live::triggerAmount(32767, 1500, 1.0) == 255 &&
                  std::abs(live::triggerAmount(16384, 1500, 1.0) - 121) <= 1 &&
                  std::abs(live::triggerAmount(16384, 1500, 2.0) - 58) <= 1,
              "trigger amount: dead zone, full, linear, curve");
        const unsigned short kA = 0x8000, kB = 0x4000;
        live::ThrottleState drive;
        drive.play = true;
        drive.vehicle = 4;   /* the Ramdozer, digital method */
        auto enc = [&](const live::ThrottleState &s, unsigned short b, int y, int trig, unsigned short &ob, int &oy) {
            ob = b, oy = y;
            live::throttleEncode(s, ob, oy, trig);
        };
        unsigned short ob;
        int oy;
        enc(drive, 0, 37, -1, ob, oy);
        check(ob == 0 && oy == 37, "no trigger value (off, or an --input line without it): unchanged");
        enc(drive, 0, 37, 0, ob, oy);
        check(ob == 0 && oy == 0, "released: no A, the raw y 0");
        enc(drive, 0, 37, 255, ob, oy);
        check(ob == kA && oy == 80, "full: A, y 80");
        enc(drive, 0, 37, 128, ob, oy);
        check(ob == kA && oy == 40, "half: A, y 40");
        enc(drive, 0, 0, 1, ob, oy);
        check(ob == kA && oy == 1, "the least pressure: A, y 1");
        enc(drive, kA, 20, 50, ob, oy);
        check(ob == kA && oy == 80, "the A button too: full");
        enc(drive, kB, 0, 0, ob, oy);
        check(ob == kB && oy == 0, "B (brake, reverse) as it was");
        /* the game's side: y 80 from A becomes the raw y; full, released or not ours unchanged */
        check(live::throttleGameY(40, kA, 80) == 40 && live::throttleGameY(80, kA, 80) == 80 &&
                  live::throttleGameY(0, kA, 80) == 80 && live::throttleGameY(40, 0, 0) == 0 &&
                  live::throttleGameY(40, kA | kB, 80) == 40 && live::throttleGameY(40, kB, -80) == -80,
              "PORT_THROTTLE: A's 80 -> the raw y; full / released / B unchanged");
        /* full and released give exactly the button's pad */
        for (int trig : {0, 255}) {
            enc(drive, 0, 55, trig, ob, oy);
            const int y = live::throttleGameY(oy, ob, (ob & kA) ? 80 : 0);
            check(y == (trig ? 80 : 0) && ob == (trig ? kA : 0), trig ? "full: the game's y as with A" : "released: as without A");
        }
        /* the analog method ("speed on 3D stick"): the larger of the stick and the trigger, full = A */
        live::ThrottleState stick = drive;
        stick.stickSpeed = true;
        enc(stick, 0, 30, 128, ob, oy);
        check(ob == 0 && oy == 40, "speed on stick: the trigger's 40 over the stick's 30");
        enc(stick, 0, 60, 128, ob, oy);
        check(ob == 0 && oy == 60, "... the stick's 60 over the trigger's 40");
        enc(stick, 0, 30, 255, ob, oy);
        check(ob == kA && oy == 30, "... full: the A button");
        /* other characters, menus, pause: the trigger is the A button */
        live::ThrottleState foot;
        foot.play = true;
        foot.vehicle = 0;
        enc(foot, 0, 50, 200, ob, oy);
        check(ob == kA && oy == 50, "on foot: a pressed trigger is A, the stick kept");
        enc(foot, 0, 50, 60, ob, oy);
        check(ob == 0 && oy == 50, "... lightly: nothing");
        live::ThrottleState menu = drive;
        menu.play = false;
        enc(menu, 0, -30, 255, ob, oy);
        check(ob == kA && oy == -30, "not in play (pause, menus): A, the stick kept");
        bool listed = true;
        for (int v : {1, 3, 4, 5, 7, 8, 10, 11, 13, 14, 15, 17, 18}) listed = listed && live::throttleVehicle(v);
        for (int v : {0, 2, 6, 9, 12, 16, 0xFD, 0xFE, 0xFF}) listed = listed && !live::throttleVehicle(v);
        check(listed, "the throttle vehicles: all but the driver, Thunderfist, the Crane, the J-Bomb, the Cyclone Suit");
    }
    live::PadSet pads;
    g_sampled = &pads;
    const unsigned short A = live::kButtonBits[live::A_A], START = live::kButtonBits[live::A_START],
                         Z = live::kButtonBits[live::A_Z];

    /* a controller appears and plays */
    SDL_JoystickID dev = attach();
    check(dev >= 0, "virtual controller attached");
    pump();
    scanOurs(pads);
    check(pads.pads.size() == 1, "scan opens it");
    SDL_Joystick *j = joyOf(pads, 0);
    SDL_JoystickSetVirtualButton(j, SDL_CONTROLLER_BUTTON_A, 1);
    SDL_JoystickSetVirtualAxis(j, SDL_CONTROLLER_AXIS_LEFTX, 32767);
    pump();
    live::N64Pad p = live::readPad(c, pads, nullptr, false);
    check(p.button == A && p.x == 80 && p.y == 0, "A + stick right reach the N64 pad");
    SDL_JoystickSetVirtualAxis(j, SDL_CONTROLLER_AXIS_TRIGGERRIGHT, 32767);
    pump();
    p = live::readPad(c, pads, nullptr, false);
    check((p.button & Z) != 0, "right trigger = Z");
    SDL_JoystickSetVirtualAxis(j, SDL_CONTROLLER_AXIS_TRIGGERRIGHT, kTriggerRest);
    SDL_JoystickSetVirtualButton(j, SDL_CONTROLLER_BUTTON_A, 0);
    SDL_JoystickSetVirtualAxis(j, SDL_CONTROLLER_AXIS_LEFTX, 0);
    pump();
    check(live::readPad(c, pads, nullptr, false).idle(), "released: idle");

    /* the analog throttle: the right trigger's pressure, no longer Z; the left trigger still Z */
    {
        live::Config t = c;
        check(live::readPad(t, pads, nullptr, false).trigger == -1, "analog throttle off: no trigger value");
        t.analogThrottle = true;
        SDL_JoystickSetVirtualAxis(j, SDL_CONTROLLER_AXIS_TRIGGERRIGHT, 16384);   /* (half: 0 .. 32767 from -32768) */
        pump();
        p = live::readPad(t, pads, nullptr, false);
        check(p.button == 0 && p.trigger > 160 && p.trigger < 210,
              "analog throttle on: the right trigger 3/4 down gives its pressure, not Z");
        SDL_JoystickSetVirtualAxis(j, SDL_CONTROLLER_AXIS_TRIGGERRIGHT, 32767);
        SDL_JoystickSetVirtualAxis(j, SDL_CONTROLLER_AXIS_TRIGGERLEFT, 32767);
        pump();
        p = live::readPad(t, pads, nullptr, false);
        check(p.button == Z && p.trigger == 255, "full: 255; the left trigger is still Z");
        SDL_JoystickSetVirtualAxis(j, SDL_CONTROLLER_AXIS_TRIGGERRIGHT, kTriggerRest);
        SDL_JoystickSetVirtualAxis(j, SDL_CONTROLLER_AXIS_TRIGGERLEFT, kTriggerRest);
        pump();
        p = live::readPad(t, pads, nullptr, false);
        check(p.trigger == 0 && p.idle(), "released: 0, idle");
        /* the gate: a trigger held when the menu closed waits for it to be let go */
        SDL_JoystickSetVirtualAxis(j, SDL_CONTROLLER_AXIS_TRIGGERRIGHT, 32767);
        pump();
        live::InputGate tg;
        tg.close(live::readPad(t, pads, nullptr, false));
        p = live::readPad(t, pads, nullptr, false);
        tg.apply(p);
        check(tg.closed() && p.trigger == 0, "gate: a held throttle is blocked");
        SDL_JoystickSetVirtualAxis(j, SDL_CONTROLLER_AXIS_TRIGGERRIGHT, kTriggerRest);
        pump();
        p = live::readPad(t, pads, nullptr, false);
        tg.apply(p);
        check(!tg.closed(), "... until let go");
    }

    /* menu combination (Back+Start) */
    SDL_JoystickSetVirtualButton(j, SDL_CONTROLLER_BUTTON_BACK, 1);
    SDL_JoystickSetVirtualButton(j, SDL_CONTROLLER_BUTTON_START, 1);
    pump();
    check(live::menuComboHeld(c, pads), "Back+Start held: menu combination");
    SDL_JoystickSetVirtualButton(j, SDL_CONTROLLER_BUTTON_BACK, 0);
    pump();
    check(!live::menuComboHeld(c, pads), "Start alone: not the combination");

    /* the gate: Start held when the menu closed */
    live::InputGate gate;
    gate.close(live::readPad(c, pads, nullptr, false));
    check(gate.closed() && gate.buttons == START, "gate closed on the held Start");
    SDL_JoystickSetVirtualButton(j, SDL_CONTROLLER_BUTTON_A, 1);
    pump();
    p = live::readPad(c, pads, nullptr, false);
    gate.apply(p);
    check(p.button == A, "while Start is held: A passes, Start is blocked");
    SDL_JoystickSetVirtualButton(j, SDL_CONTROLLER_BUTTON_START, 0);
    pump();
    p = live::readPad(c, pads, nullptr, false);
    gate.apply(p);
    check(!gate.closed() && p.button == A, "Start let go: gate open");
    SDL_JoystickSetVirtualButton(j, SDL_CONTROLLER_BUTTON_START, 1);
    pump();
    p = live::readPad(c, pads, nullptr, false);
    gate.apply(p);
    check((p.button & START) != 0, "Start pressed again: passes (the gate doesn't re-arm)");
    SDL_JoystickSetVirtualButton(j, SDL_CONTROLLER_BUTTON_START, 0);
    SDL_JoystickSetVirtualButton(j, SDL_CONTROLLER_BUTTON_A, 0);

    /* a stuck input (drifting stick past the dead zone, a held button) */
    SDL_JoystickSetVirtualAxis(j, SDL_CONTROLLER_AXIS_LEFTY, 20000);
    SDL_JoystickSetVirtualButton(j, SDL_CONTROLLER_BUTTON_B, 1);
    pump();
    gate.close(live::readPad(c, pads, nullptr, false));
    unsigned polls = 0;
    while (gate.closed() && polls < 1000) {
        p = live::readPad(c, pads, nullptr, false);
        gate.apply(p);
        polls++;
    }
    check(polls == live::InputGate::kMaxPolls, "stuck stick + button: gate opens after kMaxPolls reads");
    p = live::readPad(c, pads, nullptr, false);
    gate.apply(p);
    check(p.y != 0 && p.button != 0, "... and then they reach the game");
    SDL_JoystickSetVirtualAxis(j, SDL_CONTROLLER_AXIS_LEFTY, 0);
    SDL_JoystickSetVirtualButton(j, SDL_CONTROLLER_BUTTON_B, 0);
    pump();

    /* unplugged and plugged in again: a new instance id; pad = 1 follows */
    const SDL_JoystickID oldId = SDL_JoystickInstanceID(j);
    detach(dev);
    pump();
    scanOurs(pads);
    check(pads.pads.empty(), "unplugged: closed");
    check(live::readPad(c, pads, nullptr, false).idle(), "unplugged: idle pad");
    dev = attach();
    pump();
    scanOurs(pads);
    check(pads.pads.size() == 1 && SDL_JoystickInstanceID(joyOf(pads, 0)) != oldId, "plugged in again: reopened (new id)");
    j = joyOf(pads, 0);
    SDL_JoystickSetVirtualButton(j, SDL_CONTROLLER_BUTTON_A, 1);
    pump();
    check(live::readPad(c, pads, nullptr, false).button == A, "plugged in again: A works");
    check(pads.isActive(c, SDL_JoystickInstanceID(j)), "its events count as the playing controller's");

    /* unplug + replug between two scans (the device events missed): one rescan fixes it */
    detach(dev);
    dev = attach();
    pump();
    scanOurs(pads);
    j = joyOf(pads, 0);
    check(pads.pads.size() == 1 && j != nullptr, "replug between scans: one controller again");
    SDL_JoystickSetVirtualButton(j, SDL_CONTROLLER_BUTTON_A, 1);
    pump();
    check(live::readPad(c, pads, nullptr, false).button == A, "replug between scans: A works");
    SDL_JoystickSetVirtualButton(j, SDL_CONTROLLER_BUTTON_A, 0);

    /* two controllers: pad = 1 is the first, pad = 2 the second, any = both */
    const SDL_JoystickID dev2 = attach();
    pump();
    scanOurs(pads);
    check(pads.pads.size() == 2, "a second controller is opened");
    SDL_JoystickSetVirtualButton(joyOf(pads, 1), SDL_CONTROLLER_BUTTON_A, 1);
    pump();
    check(live::readPad(c, pads, nullptr, false).button == 0, "pad = 1: the second one doesn't play");
    c.padIndex = 1;
    check(live::readPad(c, pads, nullptr, false).button == A, "pad = 2: it does");
    c.padIndex = -1;
    check(live::readPad(c, pads, nullptr, false).button == A, "pad = any: it does");
    c.padIndex = 0;
    detach(dev2);

    /* the keyboard */
    Uint8 keys[SDL_NUM_SCANCODES] = {0};
    keys[SDL_SCANCODE_X] = 1;
    keys[SDL_SCANCODE_UP] = 1;
    p = live::readPad(c, pads, keys, false);
    check(p.button == A && p.y == 80, "keyboard X + Up");
    keys[SDL_SCANCODE_X] = 0;
    keys[SDL_SCANCODE_RETURN] = 1;
    check((live::readPad(c, pads, keys, true).button & START) == 0, "Alt+Enter isn't START");
    check((live::readPad(c, pads, keys, false).button & START) != 0, "Enter is START");

    pads.closeAll();
    g_sampled = nullptr;
    detach(dev);
    pump();

    /* the controllers' own thread (bc.exe): it finds a controller plugged in, its buttons reach the copies
     * the main thread reads, and a slow SDL update (device detection, with SDL's controller lock held) never
     * holds up a reader */
    {
        live::InputThread th;
        std::string err;
        check(th.start(err), "controllers' thread: starts SDL's controllers");
        /* (the thread's own scan opens every controller SDL lists, a real one too: the checks look for the
         * test's pad by its instance id and read it alone) */
        check(oursIn(th.latest()).pads.empty(), "... none plugged in yet");
        const SDL_JoystickID d = attach();
        bool found = false;
        for (int i = 0; i < 100 && !found; i++) {
            th.waitNewer(th.version(), 20);
            found = has(th.latest(), d);
        }
        check(found, "... a controller plugged in is found (its own rescan)");
        SDL_Joystick *vj = SDL_JoystickFromInstanceID(found ? d : -1);
        if (vj != nullptr) {
            SDL_JoystickSetVirtualAxis(vj, SDL_CONTROLLER_AXIS_TRIGGERLEFT, kTriggerRest);
            SDL_JoystickSetVirtualAxis(vj, SDL_CONTROLLER_AXIS_TRIGGERRIGHT, kTriggerRest);
            SDL_JoystickSetVirtualButton(vj, SDL_CONTROLLER_BUTTON_A, 1);
        }
        bool pressed = false;
        for (int i = 0; i < 50 && !pressed; i++) {
            th.waitNewer(th.version(), 20);
            live::PadSet s = oursIn(th.latest());
            pressed = !s.pads.empty() && live::readPad(c, s, nullptr, false).button == A && s.pads[0].handle == nullptr;
        }
        check(pressed, "... its A reaches the copies (plain data, no SDL handle)");
        /* a 300 ms update (as SDL's DirectInput enumeration of a Bluetooth pad) */
        std::atomic<bool> slow{true};
        th.testHook = [&] {
            if (slow.exchange(false)) SDL_Delay(300);
        };
        const double t0 = nowMs();
        double worst = 0;
        unsigned reads = 0;
        while (nowMs() - t0 < 400) {
            const double a = nowMs();
            live::PadSet s = th.latest();
            live::readPad(c, s, nullptr, false);
            worst = std::max(worst, nowMs() - a);
            reads++;
            SDL_Delay(1);
        }
        const live::InputThread::Stats st = th.take();
        printf("  controllers' thread: %u reads in 400 ms, the slowest %.3f ms; the thread's longest update %.0f ms\n",
               reads, worst, st.maxMs);
        check(st.maxMs >= 290 && st.slow >= 1, "... a slow update happened on the thread");
        check(worst < 5.0 && reads > 100, "... and no read of the state waited for it");
        th.testHook = nullptr;
        if (vj != nullptr) SDL_JoystickSetVirtualButton(vj, SDL_CONTROLLER_BUTTON_A, 0);
        check(detach(d), "... unplugged (detached by its instance id)");
        bool gone = false;
        for (int i = 0; i < 100 && !gone; i++) {
            th.waitNewer(th.version(), 20);
            gone = !has(th.latest(), d);
        }
        check(gone, "... unplugged: gone from the copies");
        const size_t others = th.latest().pads.size();
        if (others != 0) printf("  (%u other controller(s) on this PC seen by the thread, not counted)\n", unsigned(others));
        th.stop();
        check(!th.running(), "... stops");
    }
    SDL_Quit();
    printf("bc_inputtest: %d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
