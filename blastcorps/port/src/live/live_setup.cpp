/* bc.exe's player-facing setup: config file, bindings, ROM check (see
 * live_setup.h). */
#include <windows.h>
#include <commdlg.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

#include "live_setup.h"

extern "C" {
#include "plat_host.h"
#include "rdram.h"
}
#include "load/sha1.h"

namespace live {

const unsigned short kButtonBits[kButtons] = {
    0x8000, 0x4000, 0x2000, 0x1000, 0x0800, 0x0400, 0x0200, 0x0100, 0x0020, 0x0010, 0x0008, 0x0004, 0x0002, 0x0001,
};
const char *const kActionNames[A_COUNT] = {
    "a", "b", "z", "start", "d_up", "d_down", "d_left", "d_right", "l", "r", "c_up", "c_down", "c_left", "c_right",
    "stick_up", "stick_down", "stick_left", "stick_right", "quit", "menu", "fps_counter", "debug_cheats",
    "camera_left", "camera_right", "camera_up", "camera_down", "camera_zoom_in", "camera_zoom_out", "camera_reset",
    "camera_zoom_hold",
};
const char *const kActionLabels[A_COUNT] = {
    "A", "B", "Z", "Start", "D-pad up", "D-pad down", "D-pad left", "D-pad right", "L", "R", "C up", "C down",
    "C left", "C right", "Stick up", "Stick down", "Stick left", "Stick right", "Quit the game", "Settings menu",
    "Frame-rate counter", "Debug cheats on/off", "Orbit camera: turn left", "Orbit camera: turn right", "Orbit camera: look up",
    "Orbit camera: look down", "Orbit camera: zoom in", "Orbit camera: zoom out", "Orbit camera: reset",
    "Orbit camera: hold to zoom with look up/down",
};

/* The defaults ARE this text: configDefaults parses it, and a missing
 * bc.ini is created from it. */
static const char kDefaultIni[] =
    "; Blast Corps (native Windows port) settings.\r\n"
    "; Created with the defaults on the first start; edit it while the game is closed.\r\n"
    "; Lines starting with ; are comments. Relative paths are relative to this folder.\r\n"
    "\r\n"
    "[game]\r\n"
    "; Your Blast Corps (USA) (Rev 1) ROM: .z64, .v64 or .n64. Empty: the game asks for it\r\n"
    "; with a file dialog on the next start and remembers the answer here.\r\n"
    "rom =\r\n"
    "; Folder for the saves: the EEPROM (blastcorps.eep, 512 bytes) and the Controller Pak\r\n"
    "; (blastcorps.mpk), in the same formats as mupen64plus. Empty: nothing is saved.\r\n"
    "saves = saves\r\n"
    "; A Controller Pak plugged into controller 1: 1 = yes, 0 = no. With a pak the game keeps\r\n"
    "; its progress on the pak instead of the cartridge's EEPROM, as on the N64.\r\n"
    "controller_pak = 0\r\n"
    "; The game's own debug cheats (from the development cartridge): 1 = on, 0 = off (default).\r\n"
    "; In a level: C-right completes it, C-up + C-right fails it, Z + C-right is a perfect\r\n"
    "; finish (every building, RDU and person). Also: A skips the promotion animation, Z in the\r\n"
    "; academy gives the marker. The debug_cheats key (F3) switches them while you play.\r\n"
    "debug_cheats = 0\r\n"
    "; Where the game saves once the cheats were on: separate (default: blastcorps.cheats.eep\r\n"
    "; and .mpk next to the real saves, for the rest of that run; the real saves are not\r\n"
    "; touched) or normal (the real saves: cheated finishes count as real ones).\r\n"
    "cheats_save = separate\r\n"
    "; The defaults this file was made with (don't change it): when a newer version changes a\r\n"
    "; default, a setting still at the old default gets the new one, once.\r\n"
    "settings_version = 2\r\n"
    "\r\n"
    "[video]\r\n"
    "; Graphics API: d3d12 or vulkan.\r\n"
    "api = d3d12\r\n"
    "; Window size: 320x240 times this. The window can be resized; the picture keeps 4:3.\r\n"
    "scale = 3\r\n"
    "; Start in fullscreen: 1 = yes, 0 = in a window. Alt+Enter or F11 switches.\r\n"
    "fullscreen = 0\r\n"
    "; Wait for the display's refresh (no tearing): 1 = yes, 0 = no.\r\n"
    "vsync = 1\r\n"
    "; Frame pacing with vsync: timer (the default: the renderer's own timer, the lowest\r\n"
    "; latency, even on a PC that isn't busy) or vsync (pictures handed to the display on its\r\n"
    "; refreshes: stays even while the PC is busy with other work, about 5-10 ms more latency),\r\n"
    "; vrr (experimental, an even pace a little under the display's rate: on G-Sync / FreeSync\r\n"
    "; displays timer is smoother too) or\r\n"
    "; auto (picks one from the display).\r\n"
    "present_pacing = timer\r\n"
    "; The settings below are optional enhancements; their defaults show the game as before.\r\n"
    "; The settings menu (F1) changes them too.\r\n"
    "; Internal resolution: window = from the window's size (the default), 1 = the N64's own\r\n"
    "; 320x240, 2, 3, 4 ... = that many times it (sharper; needs a faster graphics card).\r\n"
    "resolution = window\r\n"
    "; Multisample anti-aliasing: 0 (off), 2, 4 or 8.\r\n"
    "msaa = 0\r\n"
    "; How the picture is scaled to the window: aa (anti-aliased pixel scaling, the default),\r\n"
    "; linear (smooth) or nearest (sharp pixels).\r\n"
    "filtering = aa\r\n"
    "; Aspect ratio: original (the N64's 4:3), 16:9, 16:10, 21:9, any W:H, or window (as wide\r\n"
    "; as the window). Widescreen shows more of the world at the sides; the HUD, menus and\r\n"
    "; other 2D pictures keep their shape in the middle.\r\n"
    "aspect = original\r\n"
    "; Widescreen HUD position: centered (the default: the HUD in the 4:3 middle, as on the N64)\r\n"
    "; or edges (its left and right parts at the screen's edges; centred messages stay centred).\r\n"
    "hud_position = centered\r\n"
    "; Frame rate: original (the game's own 20 or 30 pictures a second, the default), display\r\n"
    "; (as many as the display's refresh rate) or a number, e.g. 60, 120, 144. Above the\r\n"
    "; game's own rate the renderer adds in-between pictures (smoother motion; the game itself\r\n"
    "; still runs at its own pace). Needs vsync or a display at least that fast.\r\n"
    "framerate = original\r\n"
    "; How the in-between pictures match the objects of two frames: tagged (the default: by each\r\n"
    "; object's identity in the game) or auto (the renderer guesses from what is drawn).\r\n"
    "interpolation = tagged\r\n"
    "; Frame-rate counter: off (default), on (pictures shown a second and the game's own frames\r\n"
    "; a second) or detailed (also frame times and a graph). The fps_counter key (F2) switches it.\r\n"
    "fps_counter = off\r\n"
    "; Its corner: top-left (default), top-right, bottom-left or bottom-right.\r\n"
    "fps_counter_corner = top-left\r\n"
    "; A texture pack in RT64's format (a folder with an rt64.json, or an .rtz file; relative\r\n"
    "; paths are relative to this folder) and whether it is used: 1 = yes, 0 = no (default).\r\n"
    "texture_pack =\r\n"
    "texture_pack_enabled = 0\r\n"
    "; Reload the texture pack by itself when it changes (its rt64.json or .rtz file is\r\n"
    "; rewritten, e.g. by bc_packtool --watch): 1 = yes, 0 = no (default).\r\n"
    "texture_pack_reload = 0\r\n"
    "; Texture dump, for making texture packs: 1 = write every texture the game shows into\r\n"
    "; this folder as <hash>.png (once each; see README.txt), 0 = no (default).\r\n"
    "texture_dump = texture_dump\r\n"
    "texture_dump_enabled = 0\r\n"
    "\r\n"
    "[audio]\r\n"
    "; Volume in percent (0-100); - and = change it in the game, M switches the sound off and on.\r\n"
    "volume = 100\r\n"
    "; Start with the sound off: 1 = yes.\r\n"
    "mute = 0\r\n"
    "\r\n"
    "[camera]\r\n"
    "; The camera: original (the game's own, the default) or orbit (an optional free camera:\r\n"
    "; the right stick, the camera keys or the mouse turn it around the player's vehicle, or\r\n"
    "; the driver on foot, tilt it and zoom it). Only what is shown changes: the game itself\r\n"
    "; still uses its own camera. In orbit mode the right stick drives the camera while you\r\n"
    "; play, not the C buttons (the keyboard's C buttons still work).\r\n"
    "mode = original\r\n"
    "; Turning speed in percent (100 = default), across (x) and up and down (y).\r\n"
    "sensitivity_x = 100\r\n"
    "sensitivity_y = 100\r\n"
    "; Invert the turning: 1 = yes.\r\n"
    "invert_x = 0\r\n"
    "invert_y = 0\r\n"
    "; The view you set (turn, tilt): 1 = kept relative to the game's own camera, which follows\r\n"
    "; your vehicle (default: it turns with the vehicle, your angle kept, like a chase camera),\r\n"
    "; 0 = fixed in the world (a compass direction: driving a circle, it swings round you).\r\n"
    "follow = 1\r\n"
    "; Ease back behind the player when the camera input has been idle for recentre_delay\r\n"
    "; seconds: 1 = yes, 0 = the camera and its zoom stay where you left them (default; the\r\n"
    "; camera_reset button, or a new level, puts them back).\r\n"
    "recentre = 0\r\n"
    "recentre_delay = 1.5\r\n"
    "; The game steers the driver on foot, the J-Bomb, Thunderfist and the Cyclone Suit relative\r\n"
    "; to its own camera. relative_steering = 1 (default): in orbit mode the stick is turned to\r\n"
    "; match the orbit's view (up = away from you); 0 = the game's own camera directions.\r\n"
    "relative_steering = 1\r\n"
    "; With relative_steering = 0: 1 = ease back for those characters even with recentre = 0\r\n"
    "; (default), 0 = no.\r\n"
    "recentre_on_foot = 1\r\n"
    "; Buildings between the camera and you: off (default: the camera never moves for them, you\r\n"
    "; may see through them; it is still kept above the ground), smooth (it rises over them or\r\n"
    "; moves in calmly, only for lasting, wide obstructions) or strict (at once, every time).\r\n"
    "collision = off\r\n"
    "; Zoom limits, in times the game's own camera distance.\r\n"
    "zoom_min = 0.5\r\n"
    "zoom_max = 2.5\r\n"
    "; Tilt limits, in degrees above the horizontal (negative: from below).\r\n"
    "tilt_min = -10\r\n"
    "tilt_max = 80\r\n"
    "; Mouse look: hold a mouse button and move the mouse to turn, the wheel zooms. 1 = on.\r\n"
    "mouse = 1\r\n"
    "\r\n"
    "[mods]\r\n"
    "; Level mods: .bcmod files (made with the level viewer's editor, see README.txt) in this\r\n"
    "; folder. They change the level files in memory as the game reads them from your ROM; the\r\n"
    "; ROM itself is never changed. A mod made for another ROM version is refused.\r\n"
    "folder = mods\r\n"
    "; The mods to use, by file name, separated with commas (empty: none, the game as it is).\r\n"
    "enabled =\r\n"
    "\r\n"
    "[keyboard]\r\n"
    "; The N64 controller from the keyboard. Values are SDL key names, for example X, Space,\r\n"
    "; Return, Left Shift, Right Ctrl, Keypad 8, Up, F5. Several keys: separate them with\r\n"
    "; commas. Empty: not bound. Fixed keys: Alt+Enter/F11 fullscreen, M sound, - and = volume.\r\n"
    "; menu opens the settings menu (Esc or the menu key closes it).\r\n"
    "a = X\r\n"
    "b = C\r\n"
    "z = Z, Space\r\n"
    "start = Return\r\n"
    "l = A\r\n"
    "r = S\r\n"
    "c_up = I\r\n"
    "c_down = K\r\n"
    "c_left = J\r\n"
    "c_right = L\r\n"
    "d_up = T\r\n"
    "d_down = G\r\n"
    "d_left = F\r\n"
    "d_right = H\r\n"
    "stick_up = Up\r\n"
    "stick_down = Down\r\n"
    "stick_left = Left\r\n"
    "stick_right = Right\r\n"
    "quit = Escape\r\n"
    "menu = F1\r\n"
    "; fps_counter switches the frame-rate counter (off, on, detailed).\r\n"
    "fps_counter = F2\r\n"
    "; debug_cheats switches the game's debug cheats ([game] debug_cheats) for this run.\r\n"
    "debug_cheats = F3\r\n"
    "; The orbit camera ([camera] mode = orbit).\r\n"
    "camera_left = Keypad 4\r\n"
    "camera_right = Keypad 6\r\n"
    "camera_up = Keypad 8\r\n"
    "camera_down = Keypad 2\r\n"
    "camera_zoom_in = Keypad +\r\n"
    "camera_zoom_out = Keypad -\r\n"
    "camera_reset = Keypad 5\r\n"
    "camera_zoom_hold =\r\n"
    "\r\n"
    "[controller]\r\n"
    "; The N64 controller from the first game controller (Xbox/XInput and others SDL knows).\r\n"
    "; Buttons: a b x y back guide start leftstick rightstick leftshoulder rightshoulder\r\n"
    "; dpup dpdown dpleft dpright. Axes pushed one way: leftx- leftx+ lefty- lefty+ rightx-\r\n"
    "; rightx+ righty- righty+ (y- is up), lefttrigger, righttrigger. Several: commas.\r\n"
    "a = a\r\n"
    "b = b, x\r\n"
    "z = lefttrigger, righttrigger\r\n"
    "start = start\r\n"
    "l = leftshoulder\r\n"
    "r = rightshoulder\r\n"
    "c_up = righty-\r\n"
    "c_down = righty+\r\n"
    "c_left = rightx-\r\n"
    "c_right = rightx+\r\n"
    "d_up = dpup\r\n"
    "d_down = dpdown\r\n"
    "d_left = dpleft\r\n"
    "d_right = dpright\r\n"
    "quit =\r\n"
    "; The N64 analog stick comes from: left, right or none.\r\n"
    "stick = left\r\n"
    "; Dead zone of that stick (0-32767).\r\n"
    "deadzone = 7000\r\n"
    "; Its sensitivity in percent: 100 = the N64 stick's full range at full tilt (default).\r\n"
    "sensitivity = 100\r\n"
    "; Which game controller plays: 1 = the first one found (default), 2 = the second ...,\r\n"
    "; or any = all of them together.\r\n"
    "pad = 1\r\n"
    "; Analog throttle: 1 = the throttle input below drives your vehicle with its pressure\r\n"
    "; (half way: about half the top speed; fully pressed: exactly the A button), and it\r\n"
    "; presses no other N64 button (with the default z binding the left trigger stays Z:\r\n"
    "; brake, get out). The A button still drives at full throttle. 0 = off (default).\r\n"
    "analog_throttle = 0\r\n"
    "throttle = righttrigger\r\n"
    "; The trigger's dead zone (0-32767) and response (1 = linear; 2 = finer at light pressure).\r\n"
    "throttle_deadzone = 1500\r\n"
    "throttle_curve = 1\r\n"
    "; Buttons that, held together, open the settings menu (joined with +).\r\n"
    "menu = back+start\r\n"
    "; Buttons that, held together, switch the frame-rate counter (e.g. back+y; empty: none).\r\n"
    "fps_counter =\r\n"
    "; ... that switch the debug cheats (empty: none).\r\n"
    "debug_cheats =\r\n"
    "; The orbit camera ([camera] mode = orbit). While it is on in play, these inputs drive\r\n"
    "; the camera and not the N64 buttons bound to the same inputs (the C buttons above).\r\n"
    "camera_left = rightx-\r\n"
    "camera_right = rightx+\r\n"
    "camera_up = righty-\r\n"
    "camera_down = righty+\r\n"
    "; Zoom: hold camera_zoom_hold (Y) and push the right stick up (in) or down (out); or bind\r\n"
    "; camera_zoom_in / camera_zoom_out to buttons that zoom while held. (Older versions zoomed\r\n"
    "; out with a left stick click, easy to do by accident while steering: that old default is\r\n"
    "; changed to this one when bc.ini is read.)\r\n"
    "camera_zoom_in =\r\n"
    "camera_zoom_out =\r\n"
    "camera_zoom_hold = y\r\n"
    "camera_reset = rightstick\r\n";

/* ---- strings and paths ----------------------------------------------------- */
std::string utf8(const std::wstring &w) {
    if (w.empty()) return std::string();
    int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), int(w.size()), nullptr, 0, nullptr, nullptr);
    std::string s(size_t(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), int(w.size()), &s[0], n, nullptr, nullptr);
    return s;
}

std::wstring wide(const std::string &s) {
    if (s.empty()) return std::wstring();
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), int(s.size()), nullptr, 0);
    std::wstring w(size_t(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), int(s.size()), &w[0], n);
    return w;
}

std::string exeDir() {
    std::wstring buf(32768, L'\0');
    DWORD n = GetModuleFileNameW(nullptr, &buf[0], DWORD(buf.size()));
    buf.resize(n);
    size_t slash = buf.find_last_of(L"\\/");
    return utf8(slash == std::wstring::npos ? std::wstring(L".\\") : buf.substr(0, slash + 1));
}

static bool isAbsolute(const std::string &p) {
    return (p.size() >= 2 && p[1] == ':') || (!p.empty() && (p[0] == '\\' || p[0] == '/'));
}

std::string absPath(const Config &c, const std::string &p) {
    return isAbsolute(p) ? p : c.dir + p;
}

std::string modsFolder(const Config &c) {
    std::string dir = absPath(c, c.modsDir.empty() ? "mods" : c.modsDir);
    if (!dir.empty() && dir.back() != '\\' && dir.back() != '/') dir += "\\";
    return dir;
}

static std::string trim(const std::string &s) {
    size_t a = s.find_first_not_of(" \t\r\n"), b = s.find_last_not_of(" \t\r\n");
    return a == std::string::npos ? std::string() : s.substr(a, b - a + 1);
}

static std::string lower(std::string s) {
    for (char &ch : s) ch = char(tolower((unsigned char) ch));
    return s;
}

static std::vector<std::string> splitList(const std::string &v) {
    std::vector<std::string> out;
    size_t p = 0;
    while (p <= v.size()) {
        size_t q = v.find(',', p);
        if (q == std::string::npos) q = v.size();
        std::string item = trim(v.substr(p, q - p));
        if (!item.empty()) out.push_back(item);
        p = q + 1;
    }
    return out;
}

static bool readText(const std::string &path, std::string &out) {
    unsigned size = 0;
    char *p = static_cast<char *>(host_read_file(path.c_str(), &size));
    if (p == nullptr) return false;
    out.assign(p, size);
    free(p);
    if (out.compare(0, 3, "\xEF\xBB\xBF") == 0) out.erase(0, 3);   /* Notepad's BOM */
    return true;
}

/* ---- parsing ------------------------------------------------------------------ */
static bool parseBool(const std::string &v, bool &out) {
    std::string l = lower(v);
    if (l == "1" || l == "yes" || l == "true" || l == "on") return out = true, true;
    if (l == "0" || l == "no" || l == "false" || l == "off") return out = false, true;
    return false;
}

static bool parseInt(const std::string &v, int lo, int hi, int &out) {
    char *end;
    long n = strtol(v.c_str(), &end, 10);
    if (v.empty() || *end != 0 || n < lo || n > hi) return false;
    out = int(n);
    return true;
}

static bool parsePad(const std::string &item, PadInput &in) {
    std::string s = lower(item);
    int dir = 0;
    if (!s.empty() && (s.back() == '+' || s.back() == '-')) {
        dir = s.back() == '+' ? 1 : -1;
        s.pop_back();
    }
    SDL_GameControllerButton b = SDL_GameControllerGetButtonFromString(s.c_str());
    if (b != SDL_CONTROLLER_BUTTON_INVALID && dir == 0) {
        in.button = b;
        return true;
    }
    SDL_GameControllerAxis a = SDL_GameControllerGetAxisFromString(s.c_str());
    if (a == SDL_CONTROLLER_AXIS_INVALID) return false;
    bool trigger = a == SDL_CONTROLLER_AXIS_TRIGGERLEFT || a == SDL_CONTROLLER_AXIS_TRIGGERRIGHT;
    if (trigger ? dir == -1 : dir == 0) return false;
    in.axis = a;
    in.dir = trigger ? 1 : dir;
    return true;
}

static void parseText(Config &c, const std::string &text, const std::string &name) {
    std::string section;
    size_t pos = 0;
    int lineNo = 0;
    auto warn = [&](const std::string &msg) {
        c.warnings.push_back(name + " line " + std::to_string(lineNo) + ": " + msg);
    };
    while (pos < text.size()) {
        size_t eol = text.find('\n', pos);
        if (eol == std::string::npos) eol = text.size();
        std::string line = trim(text.substr(pos, eol - pos));
        pos = eol + 1;
        lineNo++;
        if (line.empty() || line[0] == ';' || line[0] == '#') continue;
        if (line[0] == '[') {
            section = lower(trim(line.substr(1, line.find(']') == std::string::npos ? std::string::npos
                                                                                   : line.find(']') - 1)));
            continue;
        }
        size_t eq = line.find('=');
        if (eq == std::string::npos) {
            warn("\"" + line + "\" is not \"key = value\"");
            continue;
        }
        std::string key = lower(trim(line.substr(0, eq))), v = trim(line.substr(eq + 1));
        bool ok = true;
        int n;
        if (section == "game") {
            if (key == "rom") c.rom = v;
            else if (key == "saves") c.saves = v;
            else if (key == "controller_pak") ok = parseBool(v, c.pak);
            else if (key == "debug_cheats") ok = parseBool(v, c.debugCheats);
            else if (key == "cheats_save") {
                std::string l = lower(v);
                if (l == "separate") c.cheatsSaveSeparate = true;
                else if (l == "normal") c.cheatsSaveSeparate = false;
                else ok = false;
            }
            else if (key == "settings_version") ok = parseInt(v, 1, 1000, c.settingsVersion);
            else ok = false;
        } else if (section == "video") {
            if (key == "api") {
                std::string l = lower(v);
                if (l == "d3d12" || l == "directx" || l == "dx12") c.vulkan = false;
                else if (l == "vulkan") c.vulkan = true;
                else ok = false;
            } else if (key == "scale") ok = parseInt(v, 1, 16, n) && (c.scale = unsigned(n), true);
            else if (key == "fullscreen") ok = parseBool(v, c.fullscreen);
            else if (key == "vsync") ok = parseBool(v, c.vsync);
            else if (key == "present_pacing") {
                std::string l = lower(v);
                if (l == "timer" || l == "default" || l == "low-latency") c.vsyncPacing = false, c.pacingOther = 0;
                else if (l == "vsync" || l == "smooth") c.vsyncPacing = true, c.pacingOther = 0;
                else if (l == "vrr" || l == "gsync" || l == "g-sync" || l == "freesync") c.vsyncPacing = false, c.pacingOther = 2;
                else if (l == "auto") c.vsyncPacing = false, c.pacingOther = 3;
                else ok = false;
            } else if (key == "resolution") {
                std::string l = lower(v);
                if (l == "window" || l == "auto") c.resolution = 0;
                else if (l == "original" || l == "native" || l == "n64") c.resolution = 1;
                else if (!l.empty() && l.back() == 'x') ok = parseInt(l.substr(0, l.size() - 1), 1, 16, c.resolution);
                else ok = parseInt(l, 1, 16, c.resolution);
            } else if (key == "msaa") {
                std::string l = lower(v);
                if (l == "off" || l == "none") c.msaa = 0;
                else ok = parseInt(l, 0, 8, n) && (n == 0 || n == 2 || n == 4 || n == 8) && (c.msaa = n, true);
            } else if (key == "aspect") {
                std::string l = lower(v);
                if (l == "original" || l == "4:3" || l == "off") c.aspect = 0;
                else if (l == "window" || l == "match" || l == "expand") c.aspect = -1;
                else {
                    /* W:H or a number, from 4:3 (the N64's) up to 4:1 */
                    double a = -1;
                    const size_t colon = l.find(':');
                    int w = 0, h = 0;
                    if (colon != std::string::npos) {
                        if (parseInt(trim(l.substr(0, colon)), 1, 10000, w) && parseInt(trim(l.substr(colon + 1)), 1, 10000, h))
                            a = double(w) / h;
                    } else {
                        char *end;
                        a = strtod(l.c_str(), &end);
                        if (l.empty() || *end != 0) a = -1;
                    }
                    ok = a >= 4.0 / 3.0 - 1e-6 && a <= 4.0;
                    if (ok) c.aspect = std::fabs(a - 4.0 / 3.0) < 1e-6 ? 0 : a;
                }
            } else if (key == "hud_position" || key == "hud") {
                std::string l = lower(v);
                if (l == "centered" || l == "centred" || l == "center" || l == "centre" || l == "4:3" || l == "0") c.hudPosition = 0;
                else if (l == "edges" || l == "edge" || l == "1") c.hudPosition = 1;
                else ok = false;
            } else if (key == "framerate" || key == "frame_rate" || key == "fps") {
                std::string l = lower(v);
                if (l == "original" || l == "off" || l == "game" || l == "0") c.framerate = 0;
                else if (l == "display" || l == "monitor" || l == "auto") c.framerate = -1;
                else ok = parseInt(l, 30, 1000, c.framerate);
            } else if (key == "interpolation") {
                std::string l = lower(v);
                if (l == "tagged" || l == "tags" || l == "explicit") c.interpolation = 1;
                else if (l == "auto" || l == "automatic") c.interpolation = 0;
                else ok = false;
            } else if (key == "fps_counter") {   /* ("fps" is framerate's alias, above) */
                std::string l = lower(v);
                if (l == "off" || l == "0" || l == "no") c.fpsCounter = 0;
                else if (l == "on" || l == "1" || l == "yes" || l == "simple") c.fpsCounter = 1;
                else if (l == "detailed" || l == "2" || l == "full") c.fpsCounter = 2;
                else ok = false;
            } else if (key == "fps_counter_corner") {
                std::string l = lower(v);
                int k = -1;
                for (int i = 0; i < 4; i++)
                    if (l == cornerName(i)) k = i;
                if (l == "topleft" || l == "tl") k = 0;
                else if (l == "topright" || l == "tr") k = 1;
                else if (l == "bottomleft" || l == "bl") k = 2;
                else if (l == "bottomright" || l == "br") k = 3;
                if (k < 0) ok = false;
                else c.fpsCorner = k;
            } else if (key == "texture_pack") {
                c.texturePack = v;
            } else if (key == "texture_pack_enabled") {
                ok = parseBool(v, c.texturePackOn);
            } else if (key == "texture_pack_reload") {
                ok = parseBool(v, c.texturePackReload);
            } else if (key == "texture_dump") {
                c.textureDump = v.empty() ? "texture_dump" : v;
            } else if (key == "texture_dump_enabled") {
                ok = parseBool(v, c.textureDumpOn);
            } else if (key == "filtering") {
                std::string l = lower(v);
                if (l == "aa" || l == "pixel" || l == "default") c.filtering = 0;
                else if (l == "linear" || l == "bilinear" || l == "smooth") c.filtering = 1;
                else if (l == "nearest" || l == "sharp" || l == "point") c.filtering = 2;
                else ok = false;
            } else ok = false;
        } else if (section == "camera") {
            auto num = [&](double lo, double hi, double &out) {
                char *end;
                const double d = strtod(v.c_str(), &end);
                if (v.empty() || *end != 0 || !(d >= lo && d <= hi)) return false;
                out = d;
                return true;
            };
            if (key == "mode") {
                std::string l = lower(v);
                if (l == "original" || l == "off" || l == "game" || l == "0") c.cameraMode = 0;
                else if (l == "orbit" || l == "free" || l == "on" || l == "1") c.cameraMode = 1;
                else ok = false;
            } else if (key == "sensitivity_x") ok = parseInt(v, 10, 500, c.camSensX);
            else if (key == "sensitivity_y") ok = parseInt(v, 10, 500, c.camSensY);
            else if (key == "invert_x") ok = parseBool(v, c.camInvertX);
            else if (key == "invert_y") ok = parseBool(v, c.camInvertY);
            else if (key == "follow") ok = parseBool(v, c.camFollow);
            else if (key == "recentre" || key == "recenter") ok = parseBool(v, c.camRecentre);
            else if (key == "recentre_on_foot" || key == "recenter_on_foot") ok = parseBool(v, c.camRecentreOnFoot);
            else if (key == "recentre_delay" || key == "recenter_delay") ok = num(0.0, 30.0, c.camRecentreDelay);
            else if (key == "zoom_min") ok = num(0.2, 1.0, c.camZoomMin);
            else if (key == "zoom_max") ok = num(1.0, 5.0, c.camZoomMax);
            else if (key == "tilt_min") ok = num(-60.0, 60.0, c.camPitchMin);
            else if (key == "tilt_max") ok = num(0.0, 89.0, c.camPitchMax);
            else if (key == "mouse") ok = parseBool(v, c.camMouse);
            else if (key == "relative_steering") ok = parseBool(v, c.camRelativeSteering);
            else if (key == "collision") {
                std::string l = lower(v);
                if (l == "smooth" || l == "on" || l == "1") c.camCollision = 0;
                else if (l == "off" || l == "none" || l == "0") c.camCollision = 1;
                else if (l == "strict" || l == "2") c.camCollision = 2;
                else ok = false;
            } else ok = false;
            if (c.camPitchMin > c.camPitchMax) std::swap(c.camPitchMin, c.camPitchMax);
        } else if (section == "mods") {
            if (key == "folder") c.modsDir = v.empty() ? "mods" : v;
            else if (key == "enabled") c.mods = splitList(v);
            else ok = false;
        } else if (section == "audio") {
            if (key == "volume") ok = parseInt(v, 0, 100, c.volume);
            else if (key == "mute") ok = parseBool(v, c.mute);
            else ok = false;
        } else if (section == "keyboard" || section == "controller") {
            int act = -1;
            for (int i = 0; i < A_COUNT; i++)
                if (key == kActionNames[i]) act = i;
            if (section == "controller" && key == "stick") {
                std::string l = lower(v);
                if (l == "left") c.padStick = 0;
                else if (l == "right") c.padStick = 1;
                else if (l == "none" || l.empty()) c.padStick = -1;
                else ok = false;
            } else if (section == "controller" && key == "deadzone") {
                ok = parseInt(v, 0, 32767, c.deadzone);
            } else if (section == "controller" && key == "analog_throttle") {
                ok = parseBool(v, c.analogThrottle);
            } else if (section == "controller" && key == "throttle") {
                c.throttlePad.clear();
                for (const std::string &item : splitList(v)) {
                    PadInput in;
                    if (parsePad(item, in)) c.throttlePad.push_back(in);
                    else warn("unknown controller input \"" + item + "\" for throttle");
                }
            } else if (section == "controller" && key == "throttle_deadzone") {
                ok = parseInt(v, 0, 32000, c.throttleDeadzone);
            } else if (section == "controller" && key == "throttle_curve") {
                char *end;
                const double d = strtod(v.c_str(), &end);
                ok = !v.empty() && *end == 0 && d >= 0.25 && d <= 4.0;
                if (ok) c.throttleCurve = d;
            } else if (section == "controller" && key == "sensitivity") {
                ok = parseInt(v, 10, 400, c.sensitivity);
            } else if (section == "controller" && key == "pad") {
                std::string l = lower(v);
                if (l == "any" || l == "all") c.padIndex = -1;
                else ok = parseInt(l, 1, 16, n) && (c.padIndex = n - 1, true);
            } else if (section == "controller" && (key == "menu" || key == "fps_counter" || key == "debug_cheats")) {
                std::vector<int> combo;
                size_t p = 0;
                while (ok && p <= v.size()) {
                    size_t q = v.find('+', p);
                    if (q == std::string::npos) q = v.size();
                    std::string item = lower(trim(v.substr(p, q - p)));
                    p = q + 1;
                    if (item.empty()) continue;
                    SDL_GameControllerButton b = SDL_GameControllerGetButtonFromString(item.c_str());
                    if (b == SDL_CONTROLLER_BUTTON_INVALID) ok = false;
                    else combo.push_back(b);
                }
                if (ok) (key == "menu" ? c.menuPad : key == "fps_counter" ? c.fpsPad : c.cheatsPad) = combo;
            } else if (act < 0) {
                ok = false;
            } else if (section == "keyboard") {
                c.keys[act].clear();
                for (const std::string &item : splitList(v)) {
                    SDL_Scancode sc = SDL_GetScancodeFromName(item.c_str());
                    if (sc == SDL_SCANCODE_UNKNOWN) warn("unknown key name \"" + item + "\" for " + key);
                    else c.keys[act].push_back(sc);
                }
                continue;
            } else {
                if (act >= A_STICK_UP && act <= A_STICK_RIGHT) {
                    ok = false;
                } else {
                    c.pad[act].clear();
                    for (const std::string &item : splitList(v)) {
                        PadInput in;
                        if (parsePad(item, in)) c.pad[act].push_back(in);
                        else warn("unknown controller input \"" + item + "\" for " + key);
                    }
                    continue;
                }
            }
        } else {
            warn("unknown section [" + section + "]");
            continue;
        }
        if (!ok) warn("\"" + line + "\" is not understood (unknown key or bad value; using the default)");
    }
}

void configDefaults(Config &c) {
    std::string dir = c.dir;
    c = Config();
    c.dir = dir;
    parseText(c, kDefaultIni, "defaults");
    c.warnings.clear();
}

/* A file of an older settings version (configLoad): the settings whose default changed since get the new
 * default where they still have the old one (the player likely never chose it); a value the player set to
 * something else stays.  The version is written with them, so this happens once: a value set back to the
 * old default afterwards stays too. */
void configMigrate(Config &c, const std::string &file) {
    if (c.settingsVersion >= kSettingsVersion) return;
    std::vector<Setting> save;
    std::string changed;
    if (c.settingsVersion < 2) {
        /* version 2: the orbit camera stays where the player put it (recentre 1 -> 0) and buildings no
         * longer move it (collision smooth -> off) */
        if (c.camRecentre) {
            c.camRecentre = false;
            save.push_back({"camera", "recentre"});
            changed += " recentre 1 -> 0;";
        }
        if (c.camCollision == 0) {
            c.camCollision = 1;
            save.push_back({"camera", "collision"});
            changed += " collision smooth -> off;";
        }
    }
    const int from = c.settingsVersion;
    c.settingsVersion = kSettingsVersion;
    save.push_back({"game", "settings_version"});
    const bool ok = configSave(c, save);
    host_log("live: %s: settings version %d -> %d:%s%s\n", file.c_str(), from, kSettingsVersion,
             changed.empty() ? " no setting at an old default" : changed.c_str(),
             ok ? " (written back)" : " (couldn't write the file: used for this run only)");
}

bool configLoad(Config &c, const std::string &file) {
    configDefaults(c);
    c.file = file;
    std::string text;
    if (readText(file, text)) {
        c.settingsVersion = 1;   /* (a file without the line: made before it existed) */
        parseText(c, text, file);
        configMigrate(c, file);
        /* the debug cheats' key (F3 by default, new in this version): not when the player's file already uses
         * the key for something else (kept for that; the menu can bind another) */
        for (int a = 0; a < A_COUNT; a++) {
            if (a == A_CHEATS) continue;
            for (SDL_Scancode k : c.keys[a]) {
                auto &ck = c.keys[A_CHEATS];
                auto it = std::find(ck.begin(), ck.end(), k);
                if (it == ck.end()) continue;
                ck.erase(it);
                host_log("live: %s: %s is bound to %s, so it doesn't switch the debug cheats\n", file.c_str(),
                         SDL_GetScancodeName(k), kActionNames[a]);
            }
        }
        /* the old default zoom bindings (Y zooms in, a left stick click out: easy to click while steering
         * hard): to the new default (hold Y and use the right stick), written back; bindings the player
         * changed stay */
        const Setting zin{"controller", "camera_zoom_in"}, zout{"controller", "camera_zoom_out"},
            zhold{"controller", "camera_zoom_hold"};
        if (text.find("camera_zoom_hold") == std::string::npos && configValue(c, zin) == "y" &&
            configValue(c, zout) == "leftstick") {
            c.pad[A_CAM_ZOOM_IN].clear();
            c.pad[A_CAM_ZOOM_OUT].clear();
            if (configSave(c, {zin, zout, zhold}))
                host_log("live: %s: the old zoom buttons (Y in, left stick click out) changed to the new default: "
                         "hold Y and push the right stick up or down\n", file.c_str());
        }
        return true;
    }
    if (host_write_file(file.c_str(), kDefaultIni, unsigned(sizeof kDefaultIni - 1)) != 0) {
        c.warnings.push_back("can't create " + file + " (settings and the ROM's location won't be remembered)");
        return false;
    }
    host_log("live: created %s with the default settings\n", file.c_str());
    return true;
}

/* Rewrite the lines of `values` ({section, key} -> value) in the file's
 * text: an existing "key = value" line of the section gets the new value
 * (the first one; the key keeps its spelling), a missing one is added at
 * the end of its section (after its last line, before the blank lines and
 * the next section's header), a missing
 * section at the end of the file.  Everything else stays as it was. */
static std::string rewriteText(const std::string &text, const std::vector<std::pair<Setting, std::string>> &values) {
    std::vector<bool> done(values.size(), false);
    std::string out, section;
    bool inSection = false;
    auto flush = [&](const std::string &sec) {   /* the section's missing keys */
        for (size_t i = 0; i < values.size(); i++)
            if (!done[i] && values[i].first.section == sec) {
                out += values[i].first.key + " = " + values[i].second + "\r\n";
                done[i] = true;
            }
    };
    size_t pos = 0;
    while (pos < text.size()) {
        size_t eol = text.find('\n', pos);
        std::string raw = text.substr(pos, eol == std::string::npos ? std::string::npos : eol + 1 - pos);
        pos = eol == std::string::npos ? text.size() : eol + 1;
        std::string line = trim(raw);
        if (!line.empty() && line[0] == '[') {
            if (inSection) {
                /* the missing keys after the section's last line, before the blank lines that end it */
                const size_t last = out.find_last_not_of(" \t\r\n");
                size_t cut = last == std::string::npos ? 0 : out.find('\n', last);
                cut = cut == std::string::npos ? out.size() : cut + 1;
                const std::string blank = out.substr(cut);
                out.resize(cut);
                flush(section);
                out += blank;
            }
            size_t close = line.find(']');
            section = lower(trim(line.substr(1, close == std::string::npos ? std::string::npos : close - 1)));
            inSection = true;
        } else if (inSection && !line.empty() && line[0] != ';' && line[0] != '#') {
            size_t eq = line.find('=');
            if (eq != std::string::npos) {
                std::string key = trim(line.substr(0, eq)), lkey = lower(key);
                bool replaced = false;
                for (size_t i = 0; i < values.size(); i++)
                    if (!done[i] && values[i].first.section == section && values[i].first.key == lkey) {
                        out += key + " = " + values[i].second + "\r\n";
                        done[i] = replaced = true;
                        break;
                    }
                if (replaced) continue;
            }
        }
        out += raw;
    }
    if (inSection) {
        bool missing = false;
        for (size_t i = 0; i < values.size(); i++) missing |= !done[i] && values[i].first.section == section;
        if (missing && !out.empty() && out.back() != '\n') out += "\r\n";
        flush(section);
    }
    for (size_t i = 0; i < values.size(); i++) {
        if (done[i]) continue;
        out += std::string(out.empty() || out.back() == '\n' ? "" : "\r\n") + "\r\n[" + values[i].first.section +
               "]\r\n";
        flush(values[i].first.section);
    }
    return out;
}

static bool writeValues(Config &c, const std::vector<std::pair<Setting, std::string>> &values) {
    if (c.file.empty()) return false;
    std::string text;
    if (!readText(c.file, text)) text = kDefaultIni;
    const std::string out = rewriteText(text, values);
    if (out == text) return true;
    if (host_write_file(c.file.c_str(), out.data(), unsigned(out.size())) != 0) {
        host_log("live: can't write %s\n", c.file.c_str());
        return false;
    }
    return true;
}

bool configSetRom(Config &c, const std::string &rom) {
    c.rom = rom;
    return writeValues(c, {{{"game", "rom"}, rom}});
}

bool configSet(Config &c, const std::string &spec) {
    size_t dot = spec.find('.'), eq = spec.find('=');
    if (dot == std::string::npos || eq == std::string::npos || dot > eq) {
        c.warnings.push_back("--set " + spec + ": not section.key=value");
        return false;
    }
    const size_t before = c.warnings.size();
    parseText(c, "[" + spec.substr(0, dot) + "]\n" + spec.substr(dot + 1, eq - dot - 1) + " = " + spec.substr(eq + 1),
              "--set " + spec);
    return c.warnings.size() == before;
}

/* ---- names as the file writes them -------------------------------------------- */
std::string keyNames(const std::vector<SDL_Scancode> &keys) {
    std::string s;
    for (SDL_Scancode k : keys) s += (s.empty() ? "" : ", ") + std::string(SDL_GetScancodeName(k));
    return s;
}

std::string padInputName(const PadInput &in) {
    if (in.button >= 0) {
        const char *n = SDL_GameControllerGetStringForButton(SDL_GameControllerButton(in.button));
        return n != nullptr ? n : "?";
    }
    const char *n = SDL_GameControllerGetStringForAxis(SDL_GameControllerAxis(in.axis));
    std::string s = n != nullptr ? n : "?";
    if (in.axis != SDL_CONTROLLER_AXIS_TRIGGERLEFT && in.axis != SDL_CONTROLLER_AXIS_TRIGGERRIGHT)
        s += in.dir > 0 ? "+" : "-";
    return s;
}

std::string padNames(const std::vector<PadInput> &in) {
    std::string s;
    for (const PadInput &p : in) s += (s.empty() ? "" : ", ") + padInputName(p);
    return s;
}

std::string menuPadNames(const std::vector<int> &buttons) {
    std::string s;
    for (int b : buttons) {
        const char *n = SDL_GameControllerGetStringForButton(SDL_GameControllerButton(b));
        s += (s.empty() ? "" : "+") + std::string(n != nullptr ? n : "?");
    }
    return s;
}

std::string aspectName(double a) {
    if (a == 0) return "original";
    if (a < 0) return "window";
    static const struct { int w, h; } kNamed[] = {{16, 9}, {16, 10}, {21, 9}, {4, 3}, {5, 4}, {32, 9}};
    for (const auto &n : kNamed)
        if (std::fabs(a - double(n.w) / n.h) < 1e-6) return std::to_string(n.w) + ":" + std::to_string(n.h);
    char buf[32];
    snprintf(buf, sizeof buf, "%.4g", a);
    return buf;
}

std::string framerateName(int f) {
    return f == 0 ? "original" : f < 0 ? "display" : std::to_string(f);
}

std::string fpsCounterName(int m) {
    return m == 1 ? "on" : m == 2 ? "detailed" : "off";
}

std::string cornerName(int c) {
    static const char *const names[4] = {"top-left", "top-right", "bottom-left", "bottom-right"};
    return c >= 0 && c < 4 ? names[c] : "top-left";
}

std::string configValue(const Config &c, const Setting &s) {
    const std::string &k = s.key;
    if (s.section == "game") {
        if (k == "rom") return c.rom;
        if (k == "saves") return c.saves;
        if (k == "controller_pak") return c.pak ? "1" : "0";
        if (k == "debug_cheats") return c.debugCheats ? "1" : "0";
        if (k == "cheats_save") return c.cheatsSaveSeparate ? "separate" : "normal";
        if (k == "settings_version") return std::to_string(c.settingsVersion);
    } else if (s.section == "video") {
        if (k == "api") return c.vulkan ? "vulkan" : "d3d12";
        if (k == "scale") return std::to_string(c.scale);
        if (k == "fullscreen") return c.fullscreen ? "1" : "0";
        if (k == "vsync") return c.vsync ? "1" : "0";
        if (k == "present_pacing")
            return c.vsyncPacing ? "vsync" : c.pacingOther == 2 ? "vrr" : c.pacingOther == 3 ? "auto" : "timer";
        if (k == "resolution") return c.resolution == 0 ? "window" : std::to_string(c.resolution);
        if (k == "msaa") return std::to_string(c.msaa);
        if (k == "filtering") return c.filtering == 1 ? "linear" : c.filtering == 2 ? "nearest" : "aa";
        if (k == "aspect") return aspectName(c.aspect);
        if (k == "hud_position") return c.hudPosition == 1 ? "edges" : "centered";
        if (k == "framerate") return framerateName(c.framerate);
        if (k == "interpolation") return c.interpolation ? "tagged" : "auto";
        if (k == "fps_counter") return fpsCounterName(c.fpsCounter);
        if (k == "fps_counter_corner") return cornerName(c.fpsCorner);
        if (k == "texture_pack") return c.texturePack;
        if (k == "texture_pack_enabled") return c.texturePackOn ? "1" : "0";
        if (k == "texture_pack_reload") return c.texturePackReload ? "1" : "0";
        if (k == "texture_dump") return c.textureDump;
        if (k == "texture_dump_enabled") return c.textureDumpOn ? "1" : "0";
    } else if (s.section == "camera") {
        auto d = [](double x) {
            char buf[32];
            snprintf(buf, sizeof buf, "%g", x);
            return std::string(buf);
        };
        if (k == "mode") return c.cameraMode == 1 ? "orbit" : "original";
        if (k == "sensitivity_x") return std::to_string(c.camSensX);
        if (k == "sensitivity_y") return std::to_string(c.camSensY);
        if (k == "invert_x") return c.camInvertX ? "1" : "0";
        if (k == "invert_y") return c.camInvertY ? "1" : "0";
        if (k == "follow") return c.camFollow ? "1" : "0";
        if (k == "recentre") return c.camRecentre ? "1" : "0";
        if (k == "recentre_on_foot") return c.camRecentreOnFoot ? "1" : "0";
        if (k == "recentre_delay") return d(c.camRecentreDelay);
        if (k == "zoom_min") return d(c.camZoomMin);
        if (k == "zoom_max") return d(c.camZoomMax);
        if (k == "tilt_min") return d(c.camPitchMin);
        if (k == "tilt_max") return d(c.camPitchMax);
        if (k == "mouse") return c.camMouse ? "1" : "0";
        if (k == "relative_steering") return c.camRelativeSteering ? "1" : "0";
        if (k == "collision") return c.camCollision == 1 ? "off" : c.camCollision == 2 ? "strict" : "smooth";
    } else if (s.section == "mods") {
        if (k == "folder") return c.modsDir;
        if (k == "enabled") {
            std::string v;
            for (const std::string &m : c.mods) v += (v.empty() ? "" : ", ") + m;
            return v;
        }
    } else if (s.section == "audio") {
        if (k == "volume") return std::to_string(c.volume);
        if (k == "mute") return c.mute ? "1" : "0";
    } else if (s.section == "keyboard") {
        for (int i = 0; i < A_COUNT; i++)
            if (k == kActionNames[i]) return keyNames(c.keys[i]);
    } else if (s.section == "controller") {
        if (k == "stick") return c.padStick == 0 ? "left" : c.padStick == 1 ? "right" : "none";
        if (k == "deadzone") return std::to_string(c.deadzone);
        if (k == "menu") return menuPadNames(c.menuPad);
        if (k == "fps_counter") return menuPadNames(c.fpsPad);
        if (k == "debug_cheats") return menuPadNames(c.cheatsPad);
        if (k == "sensitivity") return std::to_string(c.sensitivity);
        if (k == "pad") return c.padIndex < 0 ? "any" : std::to_string(c.padIndex + 1);
        if (k == "analog_throttle") return c.analogThrottle ? "1" : "0";
        if (k == "throttle") return padNames(c.throttlePad);
        if (k == "throttle_deadzone") return std::to_string(c.throttleDeadzone);
        if (k == "throttle_curve") {
            char buf[32];
            snprintf(buf, sizeof buf, "%g", c.throttleCurve);
            return buf;
        }
        for (int i = 0; i < A_COUNT; i++)
            if (k == kActionNames[i]) return padNames(c.pad[i]);
    }
    return std::string();
}

bool configSave(Config &c, const std::vector<Setting> &which) {
    std::vector<std::pair<Setting, std::string>> values;
    for (const Setting &s : which) values.push_back({s, configValue(c, s)});
    return writeValues(c, values);
}

/* ---- the ROM ------------------------------------------------------------------- */
struct KnownRom {
    const char *sha1, *name;
};
/* SHA-1 of the .z64 image (the decomp's README / *.sha1 files) */
static const char kSupportedSha1[] = "483f7161aea39de8b45c9fbc70a2c3883c4dea8c";
static const KnownRom kOtherRoms[] = {
    {"185a6ef7ba1adb243278062c81a7d4e119bda58c", "Blast Corps (USA), the first release (v1.0)"},
    {"b147fdbeb661c89107c440b00dc4810508f58636", "Blastdozer (Japan)"},
    {"460212600f8b9f0da95219c4c7330f2e626d9a7e", "Blast Corps (Europe)"},
};

std::string romCheck(const std::string &path) {
    const std::string want =
        "This port runs only Blast Corps (USA) (Rev 1), also known as v1.1 (SHA-1 " + std::string(kSupportedSha1) +
        " as .z64; .v64 and .n64 files of it work too).";
    WIN32_FILE_ATTRIBUTE_DATA fa;
    if (!GetFileAttributesExW(wide(path).c_str(), GetFileExInfoStandard, &fa))
        return "Can't find the ROM file\n" + path;
    if (fa.nFileSizeHigh != 0 || fa.nFileSizeLow > 0x4000000u)
        return path + "\nis too big to be an N64 ROM.\n\n" + want;
    unsigned size = 0;
    uint8_t *p = static_cast<uint8_t *>(host_read_file(path.c_str(), &size));
    if (p == nullptr) return "Can't read the ROM file\n" + path;
    std::string msg;
    if (size < 0x100 || rom_normalise(p, size) < 0) {
        msg = path + "\nis not an N64 ROM image.\n\n" + want;
    } else {
        char hex[41], shift[512];
        int shifted = 0;
        lm_sha1_hex(p, size, hex);
        /* a ROM from the shiftable build with files moved (docs/shiftable.md): accepted when its ROM map reads and
         * its game data is the supported ROM's (the port runs its own code; rdram.c rom_shift_check) */
        if (strcmp(hex, kSupportedSha1) != 0) shifted = rom_shift_check(p, size, shift, sizeof shift);
        if (shifted > 0) {
            host_log("live: ROM %s (SHA-1 %s): %s\n", path.c_str(), hex, shift);
        } else if (shifted < 0) {
            msg = path + "\nis a ROM from the shiftable build with files moved, but " + shift + ".\n\n" + want;
        } else if (strcmp(hex, kSupportedSha1) != 0 && rom_unmapped_shift(p, size) >= 0) {
            msg = path + "\n" + ROM_UNMAPPED_SHIFT_MSG + "\n\n" + want;
        } else if (strcmp(hex, kSupportedSha1) != 0) {
            const char *known = nullptr;
            for (const KnownRom &k : kOtherRoms)
                if (strcmp(hex, k.sha1) == 0) known = k.name;
            char title[21];
            memcpy(title, p + 0x20, 20);
            title[20] = 0;
            for (char &ch : title)
                if (ch != 0 && (ch < 0x20 || ch > 0x7E)) ch = '?';
            std::string t = trim(title);
            /* the header's country code (0x3E): the PAL and Japanese releases have their own code and data
             * layout (no dumps of them were checked here; the USA ones are 'E') */
            const char *region = nullptr;
            switch (p[0x3E]) {
                case 'J': region = "Japan"; break;
                case 'P': case 'X': case 'Y': region = "Europe (PAL)"; break;
                case 'D': region = "Germany (PAL)"; break;
                case 'F': region = "France (PAL)"; break;
                case 'U': region = "Australia (PAL)"; break;
                default: break;
            }
            if (known != nullptr)
                msg = path + "\nis " + known + ".\n\n" + want;
            else if (region != nullptr && lower(t).find("blast") != std::string::npos)
                msg = path + "\nis Blast Corps for another region (\"" + t + "\", country code '" +
                      std::string(1, char(p[0x3E])) + "': " + region + "; SHA-1 " + hex +
                      "). The other versions place the game's code and data differently, so they can't run on "
                      "this port.\n\n" + want;
            else if (lower(t).find("blast") != std::string::npos)
                msg = path + "\nlooks like Blast Corps (\"" + t + "\", version byte " + std::to_string(p[0x3F]) +
                      ") but doesn't match a known good dump (SHA-1 " + hex + "): a modified or bad dump?\n\n" + want;
            else
                msg = path + "\nis not Blast Corps (the ROM's title is \"" + t + "\").\n\n" + want;
        }
    }
    free(p);
    return msg;
}

std::string romDialog() {
    std::wstring buf(32768, L'\0');
    OPENFILENAMEW ofn = {};
    ofn.lStructSize = sizeof ofn;
    ofn.lpstrFilter = L"N64 ROMs (*.z64, *.v64, *.n64)\0*.z64;*.v64;*.n64;*.rom;*.bin\0All files (*.*)\0*.*\0";
    ofn.lpstrFile = &buf[0];
    ofn.nMaxFile = DWORD(buf.size());
    ofn.lpstrTitle = L"Blast Corps: choose your Blast Corps (USA) (Rev 1) ROM";
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR | OFN_HIDEREADONLY;
    if (!GetOpenFileNameW(&ofn)) {
        DWORD err = CommDlgExtendedError();
        if (err != 0) host_log("live: file dialog error 0x%lX\n", (unsigned long) err);
        return std::string();
    }
    buf.resize(wcslen(buf.c_str()));
    return utf8(buf);
}

}  // namespace live
