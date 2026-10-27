/* bc.exe: the windowed build.  The game logic and the platform layer are
 * bc_headless's (port/src/platform/); this file adds what a player sees and
 * does (HostLive, plat_host.h):
 *  - a window (SDL2) and the RT64 renderer (port/rt64/: RT64 built with
 *    RT64_NATIVE_HOST_LAYOUT, RDRAM = this process's memory at 0x80000000);
 *  - every graphics task the game starts goes to RT64 (loadUCodeGBI +
 *    processDisplayLists); cull_rsp_test's visibility test stays with the
 *    platform (answered "visible");
 *  - every retrace: the VI registers of the frame on screen, RT64's
 *    updateScreen, and real-time pacing (60 retraces per second of virtual
 *    time; the game logic steps exactly as in bc_headless);
 *  - controller 1 from the keyboard or an SDL game controller;
 *  - screenshots (PNG, read back from RDRAM after RT64 wrote the frame).
 *
 *   bc.exe ROM [bc_headless options] [--api d3d12|vulkan] [--shot F1,F2,...]
 *          [--shot-dir DIR] [--shot-every N] [--no-pace] [--scale N]
 *          [--dl-dump FRAME[:TASKS]] [--no-gfx-fix] [--gfx-fix-log]
 *     --shot F,..       save the picture on screen once the game reached frame F
 *                       (bc_FFFFFFF.png in --shot-dir; --shot-every N: every N frames)
 *     --no-pace         run as fast as possible (default: 60 retraces a second)
 *     --scale N         window size 320x240 times N (default 3; bc.ini scale)
 *     --dl-dump F[:N]   print N tasks' display lists from frame F (default 12)
 *     --dl-dump-every N print the first task's display lists every N frames
 *     --dl-skip LO:HI   draw triangles whose commands lie in [LO, HI) (physical)
 *                       as no-ops (find which list draws something)
 *     --no-gfx-fix      don't convert graphics data in display-list areas
 *                       (port/src/load/gfx_fix.c; to see what it does)
 *     --gfx-fix-log     log graphics data the game changed in those areas
 *     --mute            start with the sound off (M toggles it)
 *     --volume N        sound volume in percent (default 100; - and = keys step it)
 *     --fullscreen / --windowed, --vsync / --no-vsync
 *     --saves DIR       keep the EEPROM and Controller Pak files in DIR
 *     --no-saves        keep no save files (the EEPROM starts blank, no pak)
 *     --no-pak          no Controller Pak (bc.ini controller_pak = 0)
 *     --config FILE     settings file (default bc.ini next to the exe)
 *     --no-config       no settings file: built-in defaults, no saves unless
 *                       --eeprom/--mpk/--saves (tests and comparisons use this,
 *                       so bc.exe runs like bc_headless with the same options)
 *     --no-rom-check    accept any Blast Corps-layout ROM (e.g. the NON_MATCHING
 *                       test ROM) instead of only the US v1.1 release
 *     --pace-log FILE   per retrace: pacing and present times (CSV), and a
 *                       summary in the log at exit
 *     --flip-log FILE   (testing) every present RT64 made and, with D3D12, every display
 *                       refresh and the present it showed (DXGI frame statistics), CSV
 *     --perf-log FILE   (testing) twice a second: game frames, RT64's pictures and their GPU
 *                       time, render-thread and processDisplayLists time, pacing waits, the
 *                       orbit camera's host work, draw calls and triangles, CSV
 *     --spike-log FILE  (testing) every present interval over 1.5x the target with what the
 *                       retraces around it had and the cause it was put down to, CSV
 *     --pace-from F    (testing) real-time pacing only from game frame F (as fast as it
 *                       goes before: measured runs get to play sooner)
 *     --present-pacing P  timer (default: RT64's own present timer, the lowest latency) or
 *                       vsync (with vsync on, pictures go to the display on its refreshes,
 *                       port patch 0005: even under load, about 5-10 ms more latency);
 *                       short for --set video.present_pacing=P (bc.ini, the settings menu);
 *                       also vrr (G-Sync / FreeSync) and auto (applyPresentPacing)
 *     --present-legacy  (testing) RT64's own present timer, as before port patch 0007
 *     --ubershaders-only  (testing) RT64 draws everything with its ubershader (as it draws a
 *                       new material until its own shader is compiled): its GPU cost
 *   Frame times (present-to-present intervals, CPU and GPU time per picture, the
 *   display's state) go to the log every 10 s and at exit (frameLog).
 *     --set S.K=V       a bc.ini setting for this run ([S] K = V; not saved), e.g.
 *                       --set audio.volume=50
 *     --cmdline STR     (headless_main.c) the game's debug command line, as in
 *                       bc_headless: "-c" turns on the debug cheats (so does
 *                       --set game.debug_cheats=1, which puts -c there);
 *                       --cheats-save normal|separate, --cheats-on F as there
 *     --resolution R    internal resolution: window (default), 1 (320x240), 2, 3, 4 ...
 *     --msaa N          multisample anti-aliasing: 0 (default), 2, 4, 8
 *     --filtering F     the picture's scaling: aa (default), linear, nearest
 *     --aspect A        widescreen: original (4:3, default), 16:9, 16:10, 21:9, W:H, window
 *     --hud-position P  widescreen HUD: centered (default, the 4:3 middle) or edges
 *     --framerate F     pictures a second: original (the game's 20/30, default), display, or N
 *                       (RT64 interpolates in-between pictures; the game runs as before)
 *     --interpolation M tagged (default: objects paired by the game's tags, live_interp.cpp)
 *                       or auto (RT64 pairs them itself); only with --framerate above original
 *     --fps-counter M   the frame-rate counter: off (default), on, detailed (F2 switches it)
 *     --camera M        original (default) or orbit: the free orbit camera (live_orbit.cpp;
 *                       render only, the game runs as with its own camera)
 *     --orbit-test Y[:P[:Z]]  (testing) the orbit camera turns by itself: Y degrees a second,
 *                       pitch P degrees, zoom Z; sweep: the stress test (turning both ways
 *                       and flicks, tilting, zooming: orbit_math.h sweepAt)
 *     --orbit-log       log both cameras every 30 frames (and the collision's heights); check
 *                       every picture's view against its drawing list and every collision
 *                       query against a test of every triangle (summary at exit)
 *     --orbit-steplog   (testing) log every step of the orbit camera's collision
 *     --throttle-log    (testing) log every controller read's speed and position with the
 *                       analog throttle (live_throttle.cpp)
 *     --orbit-cull M    (testing) all: the drawing paths get every cell (as before the orbit
 *                       view's test), view: the game's cells and the orbit view's (default)
 *     --orbit-repeatable  (testing; kept for old scripts) pictures always use the orbit at their
 *                       own time now (the same pictures every run)
 *     --view-log FILE   (testing) every picture drawn with the orbit camera's view: when it was
 *                       rendered, its frame and weight, the view's heading and pitch, CSV
 *     --picture-log FILE  (testing) every present: its interval and what the picture shows (its
 *                       content time: the frame and interpolation weight, port patch 0008), CSV
 *     --interp-log      log every frame's camera motion (the interpolation cut test) and
 *                       tagged objects that jumped
 *     --tag-test MASK   (testing) tagged interpolation experiments: 1 no vertex blending, 2 HUD
 *                       matched like the rest, 4 no decomposition, 8 ORDER_AUTO, 16 every ID auto,
 *                       32 no pivots for parts turning about an axle (live_interp.cpp wheelPivot),
 *                       64 / 512 with --interp-log: vehicle parts' turns and pivots / vehicle 1's matrices
 *     --display-hz N    (testing) tell RT64 the display runs at N Hz (its cap on --framerate)
 *     --wseq F:MS       (testing) from frame F, every new picture the window shows for MS ms
 *                       (seq_F_NN.png + seq_F.txt with the times, in --shot-dir)
 *     --texture-pack P  use this RT64 texture pack (folder with rt64.json, or .rtz)
 *     --texture-hashes F  log the RT64 hashes of the textures loaded at frame F
 *     --dump-textures DIR  write every texture the game shows to DIR as <hash>.png, once
 *                       each, with DIR\index.csv (live_texdump.cpp; video.texture_dump)
 *     --window WxH      the window's size (desktop points) instead of 320x240 x scale
 *     --wshot F,..      save the window's picture (RT64's output at the window's size,
 *                       win_FFFFFFF.png in --shot-dir) once the game reached frame F
 *     --menu-shot F     open the settings menu at frame F, save its picture
 *                       (menu_FFFFFFF.png) and close it again (testing)
 *     --menu-set S.K=V  in that menu, change a setting as a player would (applied
 *                       at once, written to bc.ini when the menu closes)
 *     --viewer [LEVEL]  the level and model viewer (live_viewer.h), on LEVEL (default 0)
 *     --viewer-test S   (testing) a script of viewer actions, "OP;OP;..." (viewer_script.cpp runTestOp)
 *     --viewer-open F   (testing) open the viewer over the game at frame F
 *     --mod FILE        (headless_main.c) a level mod (.bcmod), also bc.ini's [mods] enabled;
 *     --no-mods         bc.ini's [mods] enabled not used
 *     --low-test MASK   (testing) the host memory below 4 GB (plat_host.h host_low_reserve):
 *                       1 crowd it before the start-up reservation, 2 after, 4 no reservation
 *     --no-msgbox       (headless_main.c) no dialog of any kind: errors, crash
 *                       reports, RT64/SDL boxes only logged; no ROM file dialog
 *
 * Crashes (port/src/platform/crash.c): a report in the log and in
 * bc-crash-*.txt, a minidump bc-crash-*.dmp next to bc.log, a message box.
 *
 * Player setup (live_setup.cpp): bc.ini next to the exe holds the ROM path,
 * graphics API, window, sound, key and controller bindings and the saves
 * folder (created with commented defaults on the first start; command-line
 * options override it).  Without a ROM argument and a working configured
 * ROM, a file dialog asks for one; the ROM is checked (Blast Corps (USA)
 * (Rev 1) by SHA-1, .z64/.v64/.n64) and remembered.  The settings menu
 * (live_menu.cpp; F1, or Back+Start on a controller) changes the settings
 * while the game is paused and writes them to bc.ini.  bc.exe is a Windows
 * GUI program: errors show a message box; the log goes to stderr when it
 * has one, else to bc.log next to the exe.
 *
 * Sound: the buffers the game hands the AI (port/src/audio/) are queued to
 * an SDL audio device at the AI's rate.  The game makes them in virtual time,
 * which the pacing keeps on real time; a slight resampling (at most 0.5%)
 * holds the device queue near 60 ms against clock drift, and a queue that
 * grew past 0.3 s (after a stall) is dropped.  With --no-pace nothing plays.
 */
#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>
#include <io.h>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include <SDL.h>
#include <SDL_syswm.h>

#include "hle/rt64_application.h"
#include "hle/rt64_workload_queue.h"
#include "common/rt64_tmem_hasher.h"
#include "render/rt64_render_target.h"
#include "render/rt64_texture_cache.h"
#include "rhi/rt64_render_hooks.h"
#include "plume_d3d12.h"

#include "render/rt64_raster_shader_cache.h"

#include "live_input.h"
#include "live_interp.h"
#include "live_menu.h"
#include "live_orbit.h"
#include "orbit_math.h"
#include "live_wide.h"
#include "live_setup.h"
#include "live_texdump.h"
#include "live_viewer.h"

#include "live_rt64.h"

extern "C" {
#include "plat_host.h"
#include "rdram.h"
#include "load/port_load.h"
#include "load/port_mods.h"
unsigned plat_frames(void); /* save.c */
}

namespace bcexe {

/* ---- options ------------------------------------------------------------- */
LiveOpts g_opt;
live::Config g_cfg;                  /* bc.ini (or the built-in defaults) */
std::string g_eepromPath, g_mpkPath; /* the saves the config names */

/* ---- RT64 state ------------------------------------------------------------ */
SDL_Window *g_window;
RT64::Application *g_app;
uint8_t g_dmem[0x1000], g_imem[0x1000], g_header[0x40];
uint32_t MI_INTR_REG, DPC_START_REG, DPC_END_REG, DPC_CURRENT_REG, DPC_STATUS_REG, DPC_CLOCK_REG, DPC_BUFBUSY_REG,
    DPC_PIPEBUSY_REG, DPC_TMEM_REG;
uint32_t VI_STATUS_REG, VI_ORIGIN_REG, VI_WIDTH_REG, VI_INTR_REG, VI_V_CURRENT_LINE_REG, VI_TIMING_REG, VI_V_SYNC_REG,
    VI_H_SYNC_REG, VI_LEAP_REG, VI_H_START_REG, VI_V_START_REG, VI_V_BURST_REG, VI_X_SCALE_REG, VI_Y_SCALE_REG;
unsigned g_spIntr, g_dpIntr, g_tasks, g_unknownUcode, g_fixedBytes, g_otherUcodeTasks;
double g_dlSeconds;                  /* time in processDisplayLists (frame-rate option on) */
unsigned g_dlTasks;

/* RT64 raises the RSP/RDP interrupts as it finishes a task (during
 * processDisplayLists).  The game hears about task completion from the
 * platform's virtual-time model instead (os_hw.c), so that it steps exactly
 * as in bc_headless; these only count. */
void checkInterrupts() {
    if (MI_INTR_REG & 0x01) g_spIntr++;
    if (MI_INTR_REG & 0x20) g_dpIntr++;
    MI_INTR_REG = 0;
}

uint8_t *rdramBase() {
    return reinterpret_cast<uint8_t *>(uintptr_t(RDRAM_BASE));
}

/* ---- microcode bytes ---------------------------------------------------------
 * RT64 identifies a ucode by hashing its text and data.  Natively the game
 * keeps neither where the RSP would find them: hd_code's text (holding the F3D
 * ucode) is not loaded, and the data images were swapped by type.  Put the
 * ROM's big-endian bytes back at their N64 addresses (nothing native reads
 * them).  The front end's ucode text is inflated with the front end, as
 * big-endian bytes, every time the game loads it. */
struct UcodePiece {
    uint32_t addr, size;
    int image;      /* 0: hd_code text, 1: hd_code data, 2: front-end data */
};
const UcodePiece kUcode[] = {
    {0x802E53F0u, 0x1430, 0},  /* gF3dText: hd F3D text (slots 1-3) */
    {0x8030E390u, 0x0800, 1},  /* gF3dData: its data */
    {0x80210690u, 0x0800, 2},  /* gL3dData: front-end L3D data */
};
constexpr uint32_t HD_TEXT_VRAM = 0x802447C0u;

void stageUcode() {
    struct Image { uint32_t rom, vram, size; std::vector<uint8_t> bytes; };
    Image img[3] = {
        {ROM_HD_TEXT, HD_TEXT_VRAM, HD_DATA_VRAM - HD_TEXT_VRAM, {}},
        {ROM_HD_DATA, HD_DATA_VRAM, HD_DATA_SIZE, {}},
        {ROM_FE_DATA, FE_DATA_VRAM, FE_BSS_END - FE_DATA_VRAM, {}},
    };
    for (Image &i : img) {
        i.bytes.resize(i.size);
        if (rom_gunzip(i.rom, i.bytes.data(), i.size) <= 0) host_fatal("live: can't inflate ROM 0x%X", (unsigned) i.rom);
    }
    for (const UcodePiece &u : kUcode) {
        const Image &i = img[u.image];
        uint8_t *dst = reinterpret_cast<uint8_t *>(uintptr_t(u.addr));
        if (u.image == 0) {
            for (uint32_t k = 0; k < u.size; k++)
                if (dst[k] != 0) {
                    host_log("live: WARNING: 0x%08X+0x%X (ucode text) is in use natively; overwriting\n", (unsigned) u.addr,
                             (unsigned) k);
                    break;
                }
        }
        memcpy(dst, &i.bytes[u.addr - i.vram], u.size);
    }
}

/* ---- input ------------------------------------------------------------------- */
/* the controllers and the N64 pad: live_input.cpp.  bc.ini's controller.pad
 * picks which controller plays (default the first) or all of them. */
live::InputThread g_input;           /* SDL's controllers on their own thread (live_input.h) */
live::PadSet g_pads;                 /* its latest sample, taken each retrace (pumpEvents) */
unsigned short g_button;
signed char g_stickX, g_stickY;
int g_trigger = -1;                  /* the analog throttle's pressure (N64Pad::trigger) */
bool g_throttleLog;                  /* --throttle-log */
live::InputGate g_gate;              /* after the settings menu (live_input.h) */
bool g_focused = true;               /* the window has the keyboard focus */
unsigned g_lastScan;                 /* retraces since the controllers' last one-second line */
live::InputThread::Stats g_padSecond;   /* the controllers' thread over that second */

std::vector<std::string> padNames() {
    return g_pads.names();
}

/* N64 pad from the keyboard and the game controller(s), by the bindings
 * in bc.ini (live_setup.cpp has the defaults):
 *   stick: arrows (keyboard), left stick       A: X / gamepad A
 *   B: C / gamepad B or X                      Z: Z or Space / either trigger
 *   START: Enter / Start                       L, R: A, S / shoulders
 *   C buttons: I J K L / right stick           D-pad: T F G H / D-pad */
void pollInput() {
    /* (the orbit camera, in play: without the inputs it uses) */
    live::N64Pad p = live::readPad(live::orbitPadConfig(), g_pads, SDL_GetKeyboardState(nullptr),
                                   (SDL_GetModState() & KMOD_ALT) != 0);
    g_gate.apply(p);
    g_button = p.button;
    g_stickX = (signed char) p.x;
    g_stickY = (signed char) p.y;
    g_trigger = p.trigger;
}

bool menuCombo() {
    return live::menuComboHeld(g_cfg, g_pads);
}

/* ---- sound ------------------------------------------------------------------- */
SDL_AudioDeviceID g_audioDev;
unsigned g_audioRate, g_audioDrops, g_audioResets;
bool g_audioFailed;
double g_audioPos = -1.0;            /* resampler position in the next buffer (-1: the previous last frame) */
int16_t g_audioPrev[2];
std::vector<int16_t> g_audioOut;

bool openAudio(unsigned rate) {
    if (g_audioDev != 0 && rate == g_audioRate) return true;
    if (g_audioFailed) return false;
    if (g_audioDev != 0) SDL_CloseAudioDevice(g_audioDev);
    if (!SDL_WasInit(SDL_INIT_AUDIO) && SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) {
        host_log("live: no sound (SDL audio: %s)\n", SDL_GetError());
        g_audioFailed = true;
        return false;
    }
    SDL_AudioSpec want = {}, have = {};
    want.freq = int(rate);
    want.format = AUDIO_S16SYS;
    want.channels = 2;
    want.samples = 512;
    g_audioDev = SDL_OpenAudioDevice(nullptr, 0, &want, &have, 0);
    if (g_audioDev == 0) {
        host_log("live: no sound (SDL_OpenAudioDevice: %s)\n", SDL_GetError());
        g_audioFailed = true;
        return false;
    }
    g_audioRate = rate;
    SDL_PauseAudioDevice(g_audioDev, 0);
    host_log("live: sound at %u Hz (%s)\n", rate, SDL_GetCurrentAudioDriver());
    return true;
}

void liveAudio(const short *lr, unsigned frames, unsigned rate) {
    if (!g_opt.pace || frames == 0 || live::viewerActive() || !openAudio(rate)) return;
    const double target = rate * 0.060;   /* queue length held near 60 ms */
    double queued = double(SDL_GetQueuedAudioSize(g_audioDev)) / 4.0;
    if (queued > rate * 0.3) {            /* far behind real time (a stall): start over */
        SDL_ClearQueuedAudio(g_audioDev);
        g_audioResets++;
        queued = 0;
    }
    /* step through the input faster when the queue is long, slower when short */
    double step = 1.0 + std::max(-0.005, std::min(0.005, (queued - target) / target * 0.005));
    static unsigned calls;
    if (host_verbose && ++calls % 500 == 0)
        host_log("live: sound queue %.0f ms, step %.4f, resets %u (frame %u)\n", queued * 1000.0 / rate, step,
                 g_audioResets, plat_frames());
    const float gain = g_opt.mute ? 0.0f : float(g_opt.volume) / 100.0f;
    g_audioOut.clear();
    double p = g_audioPos;
    while (p < double(frames - 1)) {
        long i = long(std::floor(p));
        double f = p - double(i);
        for (int c = 0; c < 2; c++) {
            double a = i < 0 ? g_audioPrev[c] : lr[i * 2 + c];
            double b = lr[(i + 1) * 2 + c];
            double v = (a + (b - a) * f) * gain;
            g_audioOut.push_back(int16_t(std::max(-32768.0, std::min(32767.0, v))));
        }
        p += step;
    }
    g_audioPos = p - double(frames);
    g_audioPrev[0] = lr[(frames - 1) * 2];
    g_audioPrev[1] = lr[(frames - 1) * 2 + 1];
    if (!g_audioOut.empty() && SDL_QueueAudio(g_audioDev, g_audioOut.data(), Uint32(g_audioOut.size() * 2)) != 0)
        g_audioDrops++;
}

/* ---- the game's debug cheats ([game] debug_cheats, the debug_cheats key) -------------
 * On at boot, bc.exe puts -c on the game's debug command line (exactly as --cmdline -c); in a running game
 * host_cheats_set writes gDebugCheats, which the game reads every time (save.c).  A marker in the
 * overlay says they are on; switching them shows a note; both are drawn over the picture (live_menu.cpp),
 * never into the game's frame.  The saves: save.c ([game] cheats_save). */
std::string fileName(const char *p) {
    std::string s = p != nullptr ? p : "";
    const size_t slash = s.find_last_of("\\/");
    return slash == std::string::npos ? s : s.substr(slash + 1);
}

/* where the game saves now, for people ("" when the run keeps no saves) */
std::string cheatsSaveText() {
    const char *name = host_cheats_save_name();
    if (name == nullptr) return "";
    const int st = host_cheats_saves();
    if (st == 2) return "Saving to " + fileName(name) + " until the game closes (your real save is untouched).";
    if (st == 1) return "Saving to " + fileName(name) + " from the next level on (your real save is untouched).";
    if (g_cfg.debugCheats && !g_cfg.cheatsSaveSeparate) return "Cheated finishes are saved to your real save.";
    return "";
}

void cheatsMarker() {
    live::overlayMarker(g_cfg.debugCheats ? "DEBUG CHEATS" : "");
}

/* g_cfg.debugCheats changed (the menu, the key): to the game, the marker, a note (not over the open menu:
 * its Game tab says the same) */
void cheatsApply() {
    if (g_cfg.debugCheats != (host_cheats_on() != 0)) host_cheats_set(g_cfg.debugCheats ? 1 : 0);
    cheatsMarker();
    std::string note = g_cfg.debugCheats ? "Debug cheats ON\nC-right: complete   C-up + C-right: fail   Z + C-right: perfect"
                                         : "Debug cheats OFF";
    const std::string where = cheatsSaveText();
    if (!where.empty()) note += "\n" + where;
    if (!live::menuIsOpen()) live::overlayNote(note, 4.0);
    host_log("live: debug cheats %s (frame %u)%s%s\n", g_cfg.debugCheats ? "on" : "off", plat_frames(),
             where.empty() ? "" : ": ", where.c_str());
}

/* each retrace: the game's own state (--cheats-on turns them on from the platform) */
void cheatsPoll() {
    const bool on = host_cheats_on() != 0;
    if (on != g_cfg.debugCheats) {
        g_cfg.debugCheats = on;
        cheatsMarker();
    }
}

/* the debug_cheats key / controller buttons: for this run (the menu saves it) */
void cheatsToggle() {
    g_cfg.debugCheats = !g_cfg.debugCheats;
    cheatsApply();
}

[[noreturn]] void shutdown(int code) {
    if (g_audioDev != 0) {
        host_log("live: sound queue resets %u, failed queue calls %u\n", g_audioResets, g_audioDrops);
        SDL_CloseAudioDevice(g_audioDev);
    }
    host_log("live: %u gfx tasks rendered (%u with an unknown ucode), RT64 interrupts SP %u DP %u, %u bytes of "
             "graphics data in display-list areas converted\n",
             g_tasks, g_unknownUcode, g_spIntr, g_dpIntr, g_fixedBytes);
    if (g_wseq.thread.joinable()) g_wseq.thread.join();
    flipWrite();
    frameEnd();
    if (g_app != nullptr) g_app->presentQueue->presentedHook = nullptr;
    if (g_app != nullptr) g_app->end();
    if (g_window != nullptr) SDL_DestroyWindow(g_window);
    g_input.stop();
    SDL_Quit();
    host_exit(code);
}

const char kTitle[] = "Blast Corps  (F1: settings)";

void setFullscreen(bool on) {
    if (SDL_SetWindowFullscreen(g_window, on ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0) != 0) {
        host_log("live: fullscreen switch failed: %s\n", SDL_GetError());
        return;
    }
    g_opt.fullscreen = on;
    SDL_ShowCursor(on ? SDL_DISABLE : SDL_ENABLE);
    int pw, ph;
    SDL_GetWindowSizeInPixels(g_window, &pw, &ph);
    host_log("live: %s (%dx%d pixels)\n", on ? "fullscreen" : "windowed", pw, ph);
    frameSegment(on ? "fullscreen" : "windowed");
}

/* widescreen: the visibility test as wide as the picture RT64 draws
 * (host_cull_wide, os_hw.c); 1 with the N64's 4:3.  With a frame rate above
 * the game's own (interpolation), the in-between pictures show the camera
 * part of the way from the last frame's view to this one's, so the test is a
 * little wider still (kInterpCullMargin): no pop-in at the picture's sides
 * while the camera turns. */
constexpr double kInterpCullMargin = 1.1;
void updateCullWide() {
    double k = 1.0;
    if (g_cfg.aspect > 0) {
        k = g_cfg.aspect / (4.0 / 3.0);
    } else if (g_cfg.aspect < 0 && g_window != nullptr) {
        int pw = 0, ph = 0;
        SDL_GetWindowSizeInPixels(g_window, &pw, &ph);
        if (pw > 0 && ph > 0) k = double(pw) / double(ph) / (4.0 / 3.0);
    }
    if (k < 1.0) k = 1.0;
    live::wideSetHudEdges(g_cfg.hudPosition == 1);   /* the HUD at the edges (live_wide.cpp) */
    live::wideSetFactor(k);  /* the full-screen 2D (sky, fades, ...) as wide as the picture (live_wide.cpp) */
    if (g_cfg.framerate != 0) k *= kInterpCullMargin;
    if (k != host_cull_wide) {
        host_cull_wide = k;
        host_log("live: visibility test %.3f times the 4:3 width (widescreen %s, interpolation %s; frame %u)\n", k,
                 live::aspectName(g_cfg.aspect).c_str(), g_cfg.framerate != 0 ? "on" : "off", plat_frames());
    }
}

/* ---- frame rate: RT64's frame interpolation --------------------------------
 * video.framerate (bc.ini, the menu, --framerate): original (0, the default:
 * RT64's refreshRate Original, every picture as the game made it), display
 * (-1: RT64's Display, the display's refresh rate) or N (Manual, N a second;
 * RT64 caps it at the display's rate).  Above the game's own rate RT64's
 * workload queue renders extra pictures between the last two game frames
 * (rt64_workload_queue.cpp: curFrameWeight), matching the draw calls of the
 * two frames itself: the camera (projection and view), and every model
 * matrix (vehicles, debris, the train, the carrier ...) whose draw call it
 * finds again in the previous frame (G_EX_ID_AUTO, rt64_game_frame.cpp; an
 * object that jumped more than ten times as far as the frame before isn't
 * blended: RigidBody's AUTO rule).  Nothing is added to the game's display
 * lists, so the game (and N64 RAM) is exactly as with interpolation off;
 * only the pictures differ.
 *
 * What this file adds (all on the host, and only with the option on):
 *  - the game's own rate: RT64 would guess it from the VI history and turn
 *    interpolation off whenever the game's cadence wavers (a load, a slow
 *    frame); the cadence measured here (the median of the last three
 *    intervals between frame-buffer swaps) goes to State::setRefreshRate;
 *  - cuts: a frame that must not be blended with the one before (a new game
 *    mode or level: menus, the globe, mission intros, a respawn; the camera
 *    jumping or turning by more than any camera motion in play) gets rate 0,
 *    which makes RT64 show it as it is (no in-between pictures into it);
 *  - the wider visibility test (updateCullWide). */
Interp g_interp;

/* the game state the cut test reads (fixed addresses, as plat_core.c's trace) */
#define BC_MODE (*(volatile unsigned long long *) uintptr_t(0x80364A90u))
#define BC_LEVEL (*(volatile int *) uintptr_t(0x802E8BDCu))
constexpr uint32_t kDynBase = 0x003156F8u;   /* gDynamicBufs[2] (DynamicBuf, types.h), physical */
constexpr uint32_t kDynSize = 0x21498u, kDynDl = 0x48B0u, kDynDlEnd = 0xA3A0u, kDynView = 0x140u;
constexpr double kCutMove = 400.0;           /* world units the camera may move in a frame ... */
constexpr double kCutMoveFactor = 4.0;       /* ... or this many times as far as the frame before */
constexpr double kCutTurnDeg = 40.0;         /* degrees the camera may turn in a frame */
constexpr double kSnapMove = 60.0;           /* ... when the game snapped its camera */
constexpr double kSnapTurnDeg = 15.0;

/* an N64 Mtx (orbit_math.h mtxFromN64) */
void mtxToDouble(const uint8_t *p, double m[4][4]) {
    live::orbit::mtxFromN64(reinterpret_cast<const uint32_t *>(p), m);
}

/* the camera of a view matrix (guLookAt: row vectors, translation in row 3; orbit_math.h viewEye):
 * false if the 3x3 part is not a rotation (the buffer holds something else) */
bool viewCamera(const double m[4][4], double eye[3], double fwd[3]) {
    live::orbit::Vec3 e, f;
    if (!live::orbit::viewEye(m, e, f)) return false;
    eye[0] = e.x, eye[1] = e.y, eye[2] = e.z;
    fwd[0] = f.x, fwd[1] = f.y, fwd[2] = f.z;
    return true;
}

/* liveVi: the game's cadence from the frame-buffer swaps */
void interpVi(const HostViRegs &r, unsigned viCount) {
    if ((r.status & 3) == 0 || r.origin == g_interp.lastOrigin) return;
    if (g_interp.lastSwapVi != 0) {
        g_interp.intervals[0] = g_interp.intervals[1];
        g_interp.intervals[1] = g_interp.intervals[2];
        g_interp.intervals[2] = std::max(1u, std::min(6u, viCount - g_interp.lastSwapVi));
    }
    g_interp.lastOrigin = r.origin;
    g_interp.lastSwapVi = viCount;
}

/* liveGfxTask, before RT64 reads the task: once per game frame (its
 * top-level display list, inside a DynamicBuf), the rate RT64 interpolates
 * from, or 0 for a cut */
void interpTask(uint32_t dl) {
    dl &= 0x3FFFFFF;
    int dyn = -1;
    for (int i = 0; i < 2; i++)
        if (dl >= kDynBase + i * kDynSize + kDynDl && dl < kDynBase + i * kDynSize + kDynDlEnd) dyn = i;
    const unsigned frame = plat_frames();
    if (dyn < 0 || frame == g_interp.lastFrame) return;
    g_interp.lastFrame = frame;
    g_interp.frames++;
    const char *why = nullptr;
    const bool snap = live::interpTagsTakeCut();
    const unsigned long long mode = BC_MODE;
    const int level = BC_LEVEL;
    if (mode != g_interp.lastMode || level != g_interp.lastLevel) why = "game mode or level";
    g_interp.lastMode = mode;
    g_interp.lastLevel = level;
    double m[4][4], eye[3], fwd[3];
    mtxToDouble(rdramBase() + kDynBase + dyn * kDynSize + kDynView, m);
    if (viewCamera(m, eye, fwd)) {
        if (g_interp.haveCam) {
            const double dx = eye[0] - g_interp.eye[0], dy = eye[1] - g_interp.eye[1], dz = eye[2] - g_interp.eye[2];
            const double move = std::sqrt(dx * dx + dy * dy + dz * dz);
            const double dot = fwd[0] * g_interp.fwd[0] + fwd[1] * g_interp.fwd[1] + fwd[2] * g_interp.fwd[2];
            const double turn = std::acos(std::max(-1.0, std::min(1.0, dot))) * 57.29577951308232;
            if (g_interp.log)
                host_log("live: interp frame %u: camera moved %.1f turned %.1f deg (eye %.0f %.0f %.0f)\n", frame,
                         move, turn, eye[0], eye[1], eye[2]);
            if (why == nullptr && move > kCutMove && move > kCutMoveFactor * g_interp.lastMove) why = "camera jump";
            if (why == nullptr && turn > kCutTurnDeg) why = "camera turn";
            /* the game snapped its camera (PORT_ICUT_IF, tagged interpolation):
             * a cut if it really jumped (the snap is also set every frame in
             * the shuttle's and the map's views, which move smoothly) */
            if (why == nullptr && snap && ((move > kSnapMove && move > kCutMoveFactor * g_interp.lastMove) ||
                                           turn > kSnapTurnDeg))
                why = "the game's camera snap";
            g_interp.lastMove = move;
        }
        memcpy(g_interp.eye, eye, sizeof eye);
        memcpy(g_interp.fwd, fwd, sizeof fwd);
        g_interp.haveCam = true;
    } else {
        g_interp.haveCam = false;
    }
    unsigned iv[3] = {g_interp.intervals[0], g_interp.intervals[1], g_interp.intervals[2]};
    std::sort(iv, iv + 3);
    const unsigned rate = why != nullptr ? 0u : 60u / iv[1];
    if (why != nullptr) {
        g_interp.cuts++;
        host_log("live: frame %u shown without in-between pictures (%s)\n", frame, why);
    }
    if (rate != g_interp.rate || g_interp.frames == 1) g_app->state->setRefreshRate(uint16_t(rate));
    g_interp.rate = rate;
}

/* the option on or off (start-up, the menu): RT64's own guess of the rate
 * again when off */
void interpApply() {
    /* tags: only with in-between pictures and video.interpolation = tagged
     * (off: the GBI untouched; off or auto: no tag recorded) */
    live::interpTagsEnable(g_cfg.framerate != 0 && g_cfg.interpolation == 1);
    /* (auto: RT64's own matching, but the globe's route lines get their ID: live_interp.cpp routeLine) */
    live::interpTagsAutoIds(g_cfg.framerate != 0 && g_cfg.interpolation == 0);
    if (g_cfg.framerate == 0) g_app->state->setRefreshRate(UINT16_MAX);
    g_interp.frames = 0;
    g_interp.lastFrame = ~0u;
    g_interp.haveCam = false;
}

/* focus and visibility, logged: without the keyboard focus SDL delivers no
 * keys (and resets the ones held); controllers keep working
 * (SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, set at start-up) */
void windowEvent(const SDL_WindowEvent &w) {
    const char *what = nullptr;
    switch (w.event) {
        case SDL_WINDOWEVENT_FOCUS_LOST: g_focused = false; what = "lost the keyboard focus (keys stop; controllers keep working)"; break;
        case SDL_WINDOWEVENT_FOCUS_GAINED: g_focused = true; what = "has the keyboard focus"; break;
        case SDL_WINDOWEVENT_MINIMIZED: what = "minimized"; break;
        case SDL_WINDOWEVENT_RESTORED: what = "restored"; break;
        case SDL_WINDOWEVENT_HIDDEN: what = "hidden"; break;
        case SDL_WINDOWEVENT_SHOWN: what = "shown"; break;
        default: break;
    }
    if (what != nullptr) host_log("live: window %s (frame %u)\n", what, plat_frames());
}

bool isKey(int action, SDL_Scancode sc) {
    for (SDL_Scancode q : g_cfg.keys[action])
        if (q == sc) return true;
    return false;
}

/* the settings menu sees what bc.exe runs with now (keys and options may have
 * changed it since bc.ini was read) */
void openMenu() {
    g_cfg.fullscreen = g_opt.fullscreen;
    g_cfg.vsync = g_opt.vsync;
    g_cfg.volume = g_opt.volume;
    g_cfg.mute = g_opt.mute;
    live::menuOpen();
}

void handleEvent(SDL_Event e);
/* the kind of an event, for the long-retrace line: 0 window, 1 keyboard / text, 2 mouse, 3 controller, 4 other */
int eventKind(Uint32 t) {
    if (t == SDL_WINDOWEVENT || t == SDL_SYSWMEVENT) return 0;
    if (t >= SDL_KEYDOWN && t < SDL_MOUSEMOTION) return 1;
    if (t >= SDL_MOUSEMOTION && t < SDL_JOYAXISMOTION) return 2;
    if (t >= SDL_JOYAXISMOTION && t < SDL_FINGERDOWN) return 3;
    return 4;
}

void pumpEvents() {
    /* the controllers' newest state (their thread's; never waits for SDL's controller work) */
    g_pads = g_input.latest();
    SDL_Event e;
    double t = secondsSince(g_tStart);
    for (bool first = true;; first = false) {
        const bool got = SDL_PollEvent(&e) != 0;
        const double t1 = secondsSince(g_tStart);
        (first ? g_tailRun.pumpMs : g_tailRun.queueMs) += (t1 - t) * 1e3;
        if (!got) break;
        handleEvent(e);
        t = secondsSince(g_tStart);
        double *kinds[5] = {&g_tailRun.windowMs, &g_tailRun.keyMs, &g_tailRun.mouseMs, &g_tailRun.padMs,
                            &g_tailRun.otherMs};
        *kinds[eventKind(e.type)] += (t - t1) * 1e3;
        g_tailRun.events++;
    }
    const double i0 = secondsSince(g_tStart);
    pollInput();
    g_tailRun.inputMs += (secondsSince(g_tStart) - i0) * 1e3;
    /* the controllers' thread's updates: this retrace's longest (the long-retrace line), a line a second when
     * some were slow */
    const live::InputThread::Stats s = g_input.take();
    g_tailRun.padThreadMs = s.maxMs;
    g_padSecond.updates += s.updates, g_padSecond.slow += s.slow;
    g_padSecond.maxMs = std::max(g_padSecond.maxMs, s.maxMs);
    if (++g_lastScan >= 60) {
        g_lastScan = 0;
        if (g_padSecond.slow != 0)
            host_log("live: controllers: %u of %u updates on their own thread took over %.0f ms (the longest %.0f ms: "
                     "SDL's device detection; the game didn't wait) (frame %u)\n",
                     g_padSecond.slow, g_padSecond.updates, live::InputThread::kSlowMs, g_padSecond.maxMs, plat_frames());
        g_padSecond = live::InputThread::Stats();
    }
}

void handleEvent(SDL_Event e) {
    {
        if (e.type == SDL_QUIT) shutdown(0);
        /* a button event the controllers' thread queued after the sample this retrace took: the sample gets it
         * (the menu's combinations below read it) */
        if (e.type == SDL_CONTROLLERBUTTONDOWN || e.type == SDL_CONTROLLERBUTTONUP)
            for (live::PadState &p : g_pads.pads)
                if (p.id == e.cbutton.which && e.cbutton.button < 32) {
                    if (e.type == SDL_CONTROLLERBUTTONDOWN) p.buttons |= 1u << e.cbutton.button;
                    else p.buttons &= ~(1u << e.cbutton.button);
                }
        if (e.type == SDL_WINDOWEVENT && e.window.event == SDL_WINDOWEVENT_SIZE_CHANGED && g_cfg.aspect < 0)
            updateCullWide();
        /* (the controllers' thread rescans them itself) */
        if (e.type == SDL_CONTROLLERDEVICEADDED || e.type == SDL_CONTROLLERDEVICEREMOVED) {
            host_log("live: controller %s (SDL id %d; frame %u)\n",
                     e.type == SDL_CONTROLLERDEVICEADDED ? "connected" : "disconnected", int(e.cdevice.which),
                     plat_frames());
            /* (the menu's navigation opens it: ImGui's backend hears of it with the menu closed too) */
            if (!live::menuIsOpen()) live::uiDeviceEvent(e);
        }
        if (e.type == SDL_WINDOWEVENT) windowEvent(e.window);
        /* a controller used while another window has the focus (e.g. the Xbox
         * button's Game Bar, a notification that took it): fullscreen, take
         * the focus back so the keyboard works again too */
        if (!g_focused && g_opt.fullscreen && e.type == SDL_CONTROLLERBUTTONDOWN && g_pads.isActive(g_cfg, e.cbutton.which)) {
            host_log("live: controller button while unfocused: raising the window (frame %u)\n", plat_frames());
            SDL_RaiseWindow(g_window);
        }
        if (live::menuIsOpen()) {
            /* waiting for a key or button to bind: everything goes to the menu */
            if (live::menuCapturing()) {
                live::menuEvent(e);
                return;
            }
            /* the menu's own keys: Esc or the menu key close it, as do the
             * controller's menu buttons; fullscreen still switches */
            if (e.type == SDL_KEYDOWN && !e.key.repeat) {
                const SDL_Scancode sc = e.key.keysym.scancode;
                if (sc == SDL_SCANCODE_ESCAPE || isKey(live::A_MENU, sc)) {
                    live::menuClose();
                    return;
                }
                if (sc == SDL_SCANCODE_F11 || (sc == SDL_SCANCODE_RETURN && (e.key.keysym.mod & KMOD_ALT) != 0)) {
                    setFullscreen(!g_opt.fullscreen);
                    g_cfg.fullscreen = g_opt.fullscreen;
                    return;
                }
            }
            if (e.type == SDL_CONTROLLERBUTTONDOWN && menuCombo()) {
                live::menuClose();
                return;
            }
            live::menuEvent(e);
            return;
        }
        if (live::orbitEvent(e)) return;   /* mouse look */
        if (e.type == SDL_KEYDOWN && isKey(live::A_QUIT, e.key.keysym.scancode)) shutdown(0);
        if (e.type == SDL_CONTROLLERBUTTONDOWN && g_pads.isActive(g_cfg, e.cbutton.which)) {
            for (const live::PadInput &in : g_cfg.pad[live::A_QUIT])
                if (in.button == e.cbutton.button) shutdown(0);
            if (menuCombo()) openMenu();
            else if (live::comboHeld(g_cfg, g_pads, g_cfg.fpsPad)) counterCycle();
            else if (live::comboHeld(g_cfg, g_pads, g_cfg.cheatsPad)) cheatsToggle();
        }
        if (e.type == SDL_KEYDOWN && !e.key.repeat) {
            const SDL_Scancode sc = e.key.keysym.scancode;
            if (sc == SDL_SCANCODE_F11 || (sc == SDL_SCANCODE_RETURN && (e.key.keysym.mod & KMOD_ALT) != 0)) {
                setFullscreen(!g_opt.fullscreen);
            } else if (isKey(live::A_MENU, sc)) {
                openMenu();
            } else if (isKey(live::A_FPS, sc)) {
                counterCycle();
            } else if (isKey(live::A_CHEATS, sc)) {
                cheatsToggle();
            } else if (e.key.keysym.scancode == SDL_SCANCODE_M) {
                g_opt.mute = !g_opt.mute;
                host_log("live: sound %s\n", g_opt.mute ? "off" : "on");
            } else if (e.key.keysym.scancode == SDL_SCANCODE_MINUS || e.key.keysym.scancode == SDL_SCANCODE_EQUALS) {
                g_opt.volume = std::max(0, std::min(100, g_opt.volume + (e.key.keysym.scancode == SDL_SCANCODE_MINUS ? -10 : 10)));
                host_log("live: volume %d%%\n", g_opt.volume);
            }
        }
    }
}

/* ---- HostLive ---------------------------------------------------------------- */
void liveBoot() {
    stageUcode();
}

/* --dl-dump: walk an F3D display list (segments, sub-lists) and print the
 * commands that matter for the data layout, with the first vertex of each
 * G_VTX in both layouts */
unsigned g_diagTasks, g_dlLastFrame = ~0u;
void diagWalk(unsigned dl) {
    uint32_t seg[16] = {0};
    uint32_t stack[16];
    int sp = 0, n = 0;
    auto res = [&](uint32_t a) {
        uint32_t full = (seg[(a >> 24) & 15] + (a & 0xFFFFFF)) & 0xFFFFFF;
        if (full >= 0x800000) host_log("  BAD address %08X (segment %u = %08X) -> %06X\n", a, (a >> 24) & 15, seg[(a >> 24) & 15], full);
        return full & 0x7FFFFF;
    };
    uint32_t a = dl & 0x7FFFFF;
    while (n++ < 20000) {
        const uint32_t *w = reinterpret_cast<const uint32_t *>(rdramBase() + a);
        uint32_t w0 = w[0], w1 = w[1], op = w0 >> 24;
        a += 8;
        switch (op) {
        case 0x01: host_log("  %06X MTX %08X -> %06X p%02X\n", a - 8, w1, res(w1), (w0 >> 16) & 0xFF); break;
        case 0x03: host_log("  %06X MOVEMEM %02X %08X -> %06X\n", a - 8, (w0 >> 16) & 0xFF, w1, res(w1)); break;
        case 0x04: {
            uint32_t r = res(w1);
            const int16_t *h = reinterpret_cast<const int16_t *>(rdramBase() + r);
            const uint8_t *b = rdramBase() + r;
            host_log("  %06X VTX n%u v%u %08X -> %06X  native(%d,%d,%d c%02X%02X%02X%02X) emu(%d,%d,%d)\n", a - 8,
                     ((w0 >> 20) & 15) + 1, (w0 >> 16) & 15, w1, r, h[0], h[1], h[2], b[12], b[13], b[14], b[15],
                     h[1], h[0], h[3]);
            break;
        }
        case 0x06:
            host_log("  %06X DL %08X -> %06X%s\n", a - 8, w1, res(w1), ((w0 >> 16) & 1) ? " (branch)" : "");
            if (!((w0 >> 16) & 1)) { if (sp < 16) stack[sp++] = a; }
            a = res(w1);
            break;
        case 0xB8:
            if (sp == 0) return;
            a = stack[--sp];
            break;
        case 0xBC:
            if ((w0 & 0xFF) == 6) { seg[((w0 >> 8) & 0xFFFF) / 4 & 15] = w1 & 0xFFFFFF; host_log("  %06X SEG %u = %08X\n", a - 8, ((w0 >> 8) & 0xFFFF) / 4, w1); }
            break;
        case 0xFD: host_log("  %06X SETTIMG %08X -> %06X fmt %u siz %u w %u\n", a - 8, w1, res(w1), (w0 >> 21) & 7, (w0 >> 19) & 3, (w0 & 0xFFF) + 1); break;
        case 0xFF: host_log("  %06X SETCIMG %08X\n", a - 8, w1); break;
        case 0xFA: host_log("  %06X PRIM %08X\n", a - 8, w1); break;
        case 0xF9: host_log("  %06X BLENDCOL %08X\n", a - 8, w1); break;
        case 0xF8: host_log("  %06X FOGCOL %08X\n", a - 8, w1); break;
        case 0xFB: host_log("  %06X ENVCOL %08X\n", a - 8, w1); break;
        case 0xFC: host_log("  %06X COMBINE %08X %08X\n", a - 8, w0, w1); break;
        case 0xB9: host_log("  %06X OTHERMODE_L %08X %08X\n", a - 8, w0, w1); break;
        case 0xBA: host_log("  %06X OTHERMODE_H %08X %08X\n", a - 8, w0, w1); break;
        case 0xB6: host_log("  %06X CLEARGEOM %08X\n", a - 8, w1); break;
        case 0xB7: host_log("  %06X SETGEOM %08X\n", a - 8, w1); break;
        case 0xF5: host_log("  %06X SETTILE %08X %08X\n", a - 8, w0, w1); break;
        case 0xF3: host_log("  %06X LOADBLOCK %08X %08X\n", a - 8, w0, w1); break;
        case 0xF4: host_log("  %06X LOADTILE %08X %08X\n", a - 8, w0, w1); break;
        case 0xF0: host_log("  %06X LOADTLUT %08X %08X\n", a - 8, w0, w1); break;
        case 0xBF: host_log("  %06X TRI %08X\n", a - 8, w1); break;
        case 0xE4: host_log("  %06X TEXRECT %08X %08X\n", a - 8, w0, w1); break;
        case 0xF6: host_log("  %06X FILLRECT %08X %08X\n", a - 8, w0, w1); break;
        case 0xBB: host_log("  %06X TEXTURE %08X %08X\n", a - 8, w0, w1); break;
        case 0xB5: host_log("  %06X LINE3D %08X\n", a - 8, w1); break;
        default: break;
        }
    }
}

/* --dl-skip LO:HI (debugging): triangles whose commands lie in [LO, HI) (physical) are
 * turned into no-ops while RT64 draws the task, then put back */
uint32_t g_skipLo, g_skipHi;
std::vector<std::pair<uint32_t *, uint32_t>> g_skipped;
void skipWalk(unsigned dl) {
    uint32_t seg[16] = {0};
    uint32_t stack[16];
    int sp = 0, n = 0;
    auto res = [&](uint32_t a) { return ((seg[(a >> 24) & 15] + (a & 0xFFFFFF)) & 0xFFFFFF) & 0x7FFFFF; };
    uint32_t a = dl & 0x7FFFFF;
    while (n++ < 40000) {
        uint32_t *w = reinterpret_cast<uint32_t *>(rdramBase() + a);
        uint32_t op = w[0] >> 24;
        a += 8;
        if (op == 0xBF || op == 0xB1) {
            if (a - 8 >= g_skipLo && a - 8 < g_skipHi) {
                g_skipped.push_back({w, w[0]});
                w[0] = 0;   /* G_SPNOOP */
            }
        } else if (op == 0x06) {
            if (!((w[0] >> 16) & 1) && sp < 16) stack[sp++] = a;
            a = res(w[1]);
        } else if (op == 0xB8) {
            if (sp == 0) return;
            a = stack[--sp];
        } else if (op == 0xBC && (w[0] & 0xFF) == 6) {
            seg[((w[0] >> 8) & 0xFFFF) / 4 & 15] = w[1] & 0xFFFFFF;
        }
    }
}

/* the game frame's top-level display list (inside a DynamicBuf), not an
 * offscreen task */
bool isFrameDl(uint32_t dl) {
    dl &= 0x3FFFFFF;
    for (int i = 0; i < 2; i++)
        if (dl >= kDynBase + i * kDynSize + kDynDl && dl < kDynBase + i * kDynSize + kDynDlEnd) return true;
    return false;
}

void liveGfxTask(unsigned ucode, unsigned ucodeData, unsigned dataPtr, unsigned dataSize) {
    (void) dataSize;
    if (g_diagTasks < g_opt.dlDumpTasks && plat_frames() >= g_opt.dlDumpFrame) {
        host_log("DIAG task %u ucode %08X dl %08X\n", g_diagTasks, ucode, dataPtr);
        diagWalk(dataPtr);
        g_diagTasks++;
    } else if (g_opt.dlDumpEvery != 0 && plat_frames() % g_opt.dlDumpEvery == 0 && plat_frames() != g_dlLastFrame) {
        g_dlLastFrame = plat_frames();   /* the frame's first task */
        host_log("DIAG frame %u task ucode %08X dl %08X\n", g_dlLastFrame, ucode, dataPtr);
        diagWalk(dataPtr);
    }
    g_app->interpreter->loadUCodeGBI(ucode & 0x3FFFFFF, ucodeData & 0x3FFFFFF, true);
    if (g_app->interpreter->hleGBI == nullptr) {
        if (g_unknownUcode++ == 0) host_log("live: RT64 does not know ucode 0x%08X/0x%08X\n", ucode, ucodeData);
        return;
    }
    g_tasks++;
    if (g_app->interpreter->hleGBI->ucode != RT64::GBIUCode::F3D) g_otherUcodeTasks++;
    if (g_opt.gfxFix) g_fixedBytes += port_gfx_fix_task(dataPtr);
    if (g_skipHi != 0 && g_app->interpreter->hleGBI->ucode == RT64::GBIUCode::F3D) skipWalk(dataPtr);
    if (g_cfg.framerate != 0) interpTask(dataPtr);
    const bool frameDl = isFrameDl(dataPtr);
    live::viewerGfxTask(ucode, ucodeData, dataPtr, frameDl);   /* (the level viewer's capture) */
    live::orbitBeforeTask(dataPtr, frameDl);
    live::interpTagsBeforeTask(dataPtr, frameDl);
    live::wideBeforeTask();
    LARGE_INTEGER t0, t1;
    const int cursor = g_app->workloadQueue->writeCursor;
    if (frameDl) {   /* spike attribution: the queue full now: the task ends waiting for the render thread */
        RT64::WorkloadQueue &q = *g_app->workloadQueue;
        std::lock_guard<std::mutex> lock(q.cursorMutex);
        if ((q.writeCursor + 1) % int(q.workloads.size()) == q.barrierCursor) g_queueFullTask = true;
    }
    QueryPerformanceCounter(&t0);
    g_app->processDisplayLists(rdramBase(), dataPtr & 0x3FFFFFF, 0, true);
    {
        QueryPerformanceCounter(&t1);
        const double s = double(t1.QuadPart - t0.QuadPart) / double(g_freq.QuadPart);
        g_ft.dlInTick += s * 1e3;   /* frame times (main thread only) */
        if (g_cfg.framerate != 0) {
            g_dlSeconds += s;
            g_dlTasks++;
        }
        g_perfDlSeconds += s;
        g_perf.cur.dlTasks++;
    }
    if (frameDl) perfWorkload(cursor);
    if (frameDl) contentFrame(g_app->state->workloadId);   /* (the frame-time report's content times) */
    live::interpTagsAfterTask(frameDl);
    live::wideAfterTask(frameDl);
    live::orbitAfterTask(frameDl);
    for (auto &s : g_skipped) *s.first = s.second;
    g_skipped.clear();
}

unsigned long long g_when0;
bool g_paceFromSeen;
unsigned g_nextShot, g_nextWshot;
bool g_quitRequested;
bool g_viewerBoot;                   /* --viewer: the level viewer starts at the first retrace */
bool g_noMods;                       /* --no-mods: bc.ini's [mods] enabled not used */
unsigned g_viewerOpenFrame = ~0u;    /* --viewer-open F (testing): opened over the game at frame F */
unsigned g_hashFrame = ~0u;          /* --texture-hashes */
void logTextureHashes(unsigned frame);
void packWatch(unsigned viCount);

/* The settings menu is open: the game waits (inside this retrace's hook)
 * while the menu runs, about 60 times a second, over the last picture.
 * RT64's own pause (its debugger's) re-renders the last frame's work and
 * presents it again at every updateScreen, so video settings show at once.
 * The pacing starts over afterwards.  `shotFrame` != 0: --menu-shot (save
 * the window's picture after a few frames and close the menu). */
void runMenu(unsigned shotFrame) {
    {
        std::lock_guard<std::mutex> lock(g_ft.mutex);   /* frame times: the menu's pictures don't count */
        g_ft.suspended = true;
    }
    g_app->state->debuggerInspector.paused = true;
    for (unsigned n = 0; live::menuIsOpen(); n++) {
        if (shotFrame != 0 && n == 2)
            for (const std::string &s : g_opt.menuSets)
                if (!live::menuTestSet(s)) host_log("live: --menu-set %s: not understood\n", s.c_str());
        live::menuFrame();
        g_app->updateScreen();
        if (shotFrame != 0 && n == 30) {
            char name[32];
            snprintf(name, sizeof name, "menu_%07u", shotFrame);
            saveWindow(name);
            live::menuClose();
        }
        if (g_quitRequested) shutdown(0);
        Sleep(15);
        if (live::menuIsOpen()) pumpEvents();
    }
    g_app->state->debuggerInspector.paused = false;
    {
        std::lock_guard<std::mutex> lock(g_ft.mutex);
        g_ft.suspended = false;
        g_ft.last = -1;
        g_ft.shownCount = 0;
        g_ft.lastViEnter = -1;   /* (the menu's time is no missed retrace) */
    }
    if (g_quitRequested) shutdown(0);
    g_t0.QuadPart = 0;   /* pacing: start over from the next retrace */
    /* what is held now (the keys or buttons that closed the menu) doesn't
     * reach the game until let go (each on its own, at most a second) */
    g_gate.close(live::readPad(g_cfg, g_pads, SDL_GetKeyboardState(nullptr), (SDL_GetModState() & KMOD_ALT) != 0));
}

void liveVi(const HostViRegs *r, unsigned viCount, unsigned long long when, unsigned frames) {
    /* frame times: the main thread since it left the last retrace (the game's
     * step and the host's per-frame work, less reading the display lists) */
    const double viEnter = secondsSince(g_tStart);
    g_viWhenMs = double(when) / 46875.0;   /* (virtual time: the content times of the frames after it) */
    const double sinceExit = g_ft.viExit >= 0 ? (viEnter - g_ft.viExit) * 1e3 : 0.0;
    const double dlMs = g_ft.dlInTick;
    double waitMs = 0;
    g_ft.dlInTick = 0;
    if (g_cfg.framerate != 0) interpVi(*r, viCount);
    if (g_interp.displayHz != 0) g_app->sharedQueueResources->setSwapChainRate(g_interp.displayHz);
    if (r->status != VI_STATUS_REG)
        host_log("live: VI status %08X (type %u, gamma dither %u, gamma %u, divot %u, AA mode %u, dither filter %u)\n",
                 r->status, r->status & 3, (r->status >> 2) & 1, (r->status >> 3) & 1, (r->status >> 4) & 1,
                 (r->status >> 8) & 3, (r->status >> 16) & 1);
    VI_STATUS_REG = r->status;
    VI_ORIGIN_REG = r->origin;
    VI_WIDTH_REG = r->width;
    VI_INTR_REG = r->intr;
    VI_V_CURRENT_LINE_REG = 0;
    VI_TIMING_REG = r->burst;
    VI_V_SYNC_REG = r->v_sync;
    VI_H_SYNC_REG = r->h_sync;
    VI_LEAP_REG = r->leap;
    VI_H_START_REG = r->h_start;
    VI_V_START_REG = r->v_start;
    VI_V_BURST_REG = r->v_burst;
    VI_X_SCALE_REG = r->x_scale;
    VI_Y_SCALE_REG = r->y_scale;

    /* screenshots of the picture on screen once the game reached the frame */
    while (g_nextShot < g_opt.shots.size() && frames >= g_opt.shots[g_nextShot]) {
        saveFrame(*r, frames);
        g_nextShot++;
    }
    if (g_opt.shotEvery != 0 && frames != 0 && frames % g_opt.shotEvery == 0) {
        static unsigned last;
        if (last != frames) saveFrame(*r, frames), last = frames;
    }

    /* real time: retrace N of virtual time is shown at t0 + N/60 s.  The game
     * logic runs on the virtual clock either way; this only decides when each
     * retrace's picture goes to RT64 (and vsync then shows it at the
     * display's next refresh).  Waiting before handing it over (not after)
     * keeps the hand-over times regular: the game's and RT64's own work for
     * the next retrace happens after it, inside the wait's slack. */
    if (g_opt.pace && g_opt.paceFrom != 0 && frames >= g_opt.paceFrom && g_t0.QuadPart == 0 && !g_paceFromSeen) {
        g_paceFromSeen = true;   /* (testing) the frame times from here */
        frameSegment("--pace-from");
    }
    if (g_opt.pace && frames >= g_opt.paceFrom && !live::viewerActive()) {   /* (the viewer loading: as fast as it goes) */
        LARGE_INTEGER now;
        QueryPerformanceCounter(&now);
        if (g_t0.QuadPart == 0) {
            g_t0 = now;
            g_when0 = when;
        }
        const double due = double(when - g_when0) / 46875000.0;
        double t = double(now.QuadPart - g_t0.QuadPart) / double(g_freq.QuadPart);
        if (t > due + 0.25) {   /* far behind (a level load, a stall): start over */
            g_t0 = now;
            g_when0 = when;
            g_paceResets++;
        } else {
            const double waitFrom = t;
            while (t < due) {
                const double left = due - t;
                if (left > 0.002) Sleep(DWORD((left - 0.0015) * 1000.0));
                QueryPerformanceCounter(&now);
                t = double(now.QuadPart - g_t0.QuadPart) / double(g_freq.QuadPart);
            }
            perfWait(t - waitFrom);
            waitMs = (t - waitFrom) * 1e3;
        }
    }
    cheatsPoll();
    counterTick(frames);
    /* the presents this retrace queues are due at its paced time (port patch
     * 0005: the vsync pacing holds pictures by these times, so a retrace the
     * game thread was late for keeps its place on the display's timeline) */
    if (g_opt.pace && g_t0.QuadPart != 0) {
        LARGE_INTEGER now;
        QueryPerformanceCounter(&now);
        const double dueAgo = double(now.QuadPart - g_t0.QuadPart) / double(g_freq.QuadPart) -
                              double(when - g_when0) / 46875000.0;
        g_app->state->presentDueTime = RT64::Timer::current() - std::chrono::nanoseconds(int64_t(dueAgo * 1e9));
    }
    const double before = secondsSince(g_tStart);
    g_app->updateScreen();
    const double submitted = secondsSince(g_tStart);
    {
        std::lock_guard<std::mutex> lock(g_ft.mutex);
        busyAdd(&Busy::dl, dlMs);
    }
    frameTick(viEnter * 1e3, std::max(0.0, sinceExit - dlMs), dlMs, (submitted - before) * 1e3, waitMs);
    perfTick(frames);
    const double tailStart = secondsSince(g_tStart);   /* (long retraces: the host's work after the retrace) */
    g_tailRun = Tail();
    g_app->state->presentDueTime = RT64::Timestamp();
    if (g_opt.paceLog != nullptr) {
        const double after = secondsSince(g_tStart);
        const double due = g_t0.QuadPart == 0 ? 0.0
            : double(g_t0.QuadPart - g_tStart.QuadPart) / double(g_freq.QuadPart) + double(when - g_when0) / 46875000.0;
        g_pace.push_back({before, due, after - before, frames});
    }
    if (frames >= g_hashFrame) {
        logTextureHashes(frames);
        g_hashFrame = ~0u;
    }
    /* texture packs: the dump finds what RT64 loaded; a changed pack reloads */
    if (live::texDumpOn()) live::texDumpPoll(g_app->textureCache.get(), frames);
    packWatch(viCount);
    if (!g_wseq.started && frames >= g_wseq.frame) {
        g_wseq.started = true;
        g_wseq.thread = std::thread(winSeqThread, frames, g_wseq.ms);
    }
    /* pictures of the window (testing): --wshot, --menu-shot */
    while (g_nextWshot < g_opt.wshots.size() && frames >= g_opt.wshots[g_nextWshot]) {
        char name[32];
        snprintf(name, sizeof name, "win_%07u", frames);
        saveWindow(name);
        g_nextWshot++;
    }
    if (g_opt.menuShot != ~0u && frames >= g_opt.menuShot) {
        g_opt.menuShot = ~0u;
        openMenu();
        runMenu(frames);
    }
    const double eventsStart = secondsSince(g_tStart);
    pumpEvents();
    g_tailRun.eventsMs = (secondsSince(g_tStart) - eventsStart) * 1e3;
    live::orbitVi(g_pads, g_focused ? SDL_GetKeyboardState(nullptr) : nullptr);
    if (live::menuIsOpen()) runMenu(0);
    {
        /* the level viewer: it runs here while it holds the game (live_viewer.h) */
        const bool was = live::viewerActive();
        if (frames >= g_viewerOpenFrame) {
            g_viewerOpenFrame = ~0u;
            live::viewerOpen();
        }
        if (was || live::viewerWanted() || g_viewerBoot) {
            g_viewerBoot = false;
            live::viewerVi(frames);
        }
        if (was && !live::viewerActive()) {
            g_t0.QuadPart = 0;   /* pacing: start over from the next retrace */
            interpApply();
            g_gate.close(live::readPad(g_cfg, g_pads, SDL_GetKeyboardState(nullptr), (SDL_GetModState() & KMOD_ALT) != 0));
            if (g_opt.fullscreen) SDL_ShowCursor(SDL_DISABLE);
        }
    }
    g_ft.viExit = secondsSince(g_tStart);
    g_tailRun.ms = (g_ft.viExit - tailStart) * 1e3;
    g_tail = g_tailRun;
}

int liveInput(unsigned short *button, signed char *x, signed char *y) {
    static unsigned short lastButton;
    *button = g_button;
    *x = g_stickX;
    *y = g_stickY;
    if (live::viewerActive()) *button = 0, *x = 0, *y = 0;   /* (the level viewer loading or capturing: no input) */
    host_live_trigger = g_trigger;   /* (the analog throttle: plat_input_read gives it to live_throttle.cpp) */
    if (host_verbose && g_button != lastButton)
        host_log("live: pad buttons %04X stick %d,%d (frame %u)\n", g_button, g_stickX, g_stickY, plat_frames());
    lastButton = g_button;
    return 1;
}

const HostLive kLive = {liveBoot, liveGfxTask, liveVi, liveInput, liveAudio};

/* ---- settings changed in the menu ------------------------------------------- */
/* RT64's present thread, after the picture: --pace-log's times, the menu */
void drawHook(plume::RenderCommandList *list, plume::RenderFramebuffer *fb) {
    if (g_presentTimes != nullptr) presentHook(list, fb);
    live::menuDraw(list);
}

void deinitHook() {
    live::menuShutdown();
}

/* MSAA sample counts the device can render with (a mask of 1, 2, 4, 8);
 * 1 only when it lacks programmable sample locations, which RT64 needs */
unsigned msaaSupported() {
    if (g_app == nullptr || !g_app->device || !g_app->device->getCapabilities().sampleLocations) return 1;
    const bool hdr = g_app->shaderLibrary->usesHDR;
    return unsigned(g_app->device->getSampleCountsSupported(RT64::RenderTarget::colorBufferFormat(hdr)) &
                    g_app->device->getSampleCountsSupported(RT64::RenderTarget::depthBufferFormat()));
}

/* present pacing (applyPresentPacing) */
bool g_presentLegacy;                /* --present-legacy (testing): RT64's timer as before patch 0007 */
bool g_ubershadersOnly;              /* --ubershaders-only (testing) */
int g_pacing;                        /* the effective pacing: 0 timer, 1 vsync, 2 vrr */
constexpr double kVrrMargin = 0.97;  /* vrr: at most this fraction of the display's rate (3 % under, the usual cap) */
unsigned effectiveDisplayHz() {
    return g_interp.displayHz != 0 ? g_interp.displayHz : displayHz();
}

/* bc.ini's video enhancements as RT64's user configuration.  The defaults
 * (resolution = window, msaa = 0, filtering = aa) are RT64's own defaults,
 * what bc.exe always used. */
void configToRT64(RT64::UserConfiguration &u) {
    using U = RT64::UserConfiguration;
    if (g_cfg.resolution == 0) u.resolution = U::Resolution::WindowIntegerScale;
    else if (g_cfg.resolution == 1) u.resolution = U::Resolution::Original;
    else {
        u.resolution = U::Resolution::Manual;
        u.resolutionMultiplier = double(g_cfg.resolution);
    }
    u.antialiasing = g_cfg.msaa == 8 ? U::Antialiasing::MSAA8X : g_cfg.msaa == 4 ? U::Antialiasing::MSAA4X
                     : g_cfg.msaa == 2 ? U::Antialiasing::MSAA2X : U::Antialiasing::None;
    u.filtering = g_cfg.filtering == 1 ? U::Filtering::Linear : g_cfg.filtering == 2 ? U::Filtering::Nearest
                                                                                      : U::Filtering::AntiAliasedPixelScaling;
    /* widescreen: RT64 widens the viewport of every projection that covers the
     * whole width and narrows its x scale to match (no stretching); 2D
     * rectangles and partial-width views keep their 4:3 shape, centred */
    if (g_cfg.aspect == 0) {
        u.aspectRatio = U::AspectRatio::Original;
    } else if (g_cfg.aspect < 0) {
        u.aspectRatio = U::AspectRatio::Expand;
    } else {
        u.aspectRatio = U::AspectRatio::Manual;
        u.aspectTarget = g_cfg.aspect;
    }
    /* video.hud_position = edges: the HUD's left and right parts get RT64's
     * extended-GBI origins (live_wide.cpp), which move them out by the whole
     * extra width only with extAspectRatio Expand.  Centered keeps RT64's
     * default (Original: no origins are set anyway) */
    u.extAspectRatio = g_cfg.hudPosition == 1 ? U::AspectRatio::Expand : U::AspectRatio::Original;
    /* frame rate: in-between pictures (see interpTask) */
    if (g_cfg.framerate == 0) {
        u.refreshRate = U::RefreshRate::Original;
    } else if (g_cfg.framerate < 0) {
        u.refreshRate = U::RefreshRate::Display;
    } else {
        u.refreshRate = U::RefreshRate::Manual;
        u.refreshRateTarget = g_cfg.framerate;
    }
    /* present_pacing = vrr: a little under the display's highest rate (applyPresentPacing) */
    if (g_pacing == 2 && g_cfg.framerate != 0) {
        const unsigned hz = effectiveDisplayHz();
        const unsigned cap = hz != 0 ? unsigned(std::floor(double(hz) * kVrrMargin)) : 0;
        if (cap != 0 && (g_cfg.framerate < 0 || unsigned(g_cfg.framerate) > cap)) {
            u.refreshRate = U::RefreshRate::Manual;
            u.refreshRateTarget = int(cap);
        }
    }
}

/* texture pack (RT64's replacements): loaded when enabled and set, cleared
 * when turned off.  Called before the game starts and from the menu (the
 * main thread, which is the one that submits textures, as RT64 requires).
 * Returns a line for the log / menu. */
bool g_packLoaded;
std::string g_packStatus;
long long g_packStamp;   /* the pack's rt64.json (or .rtz) time when loaded (texture_pack_reload) */

/* the last-write time of a pack folder's rt64.json, or of an .rtz (0: none) */
long long packStamp(const std::string &path) {
    std::error_code ec;
    std::filesystem::path p(live::wide(path));
    if (std::filesystem::is_directory(p, ec)) p /= L"rt64.json";
    const auto t = std::filesystem::last_write_time(p, ec);
    return ec ? 0 : (long long) t.time_since_epoch().count();
}

void applyTexturePack() {
    if (g_cfg.texturePackOn && !g_cfg.texturePack.empty()) {
        const std::string path = live::absPath(g_cfg, g_cfg.texturePack);
        g_packStamp = packStamp(path);
        const bool ok = g_app->textureCache->loadReplacementDirectory(
            RT64::ReplacementDirectory(std::filesystem::path(live::wide(path))));
        g_packLoaded = true;
        g_packStatus = (ok ? "loaded " : "could not load ") + g_cfg.texturePack;
        host_log("live: texture pack %s %s\n", ok ? "loaded" : "could not load", path.c_str());
    } else if (g_packLoaded) {
        g_app->textureCache->clearReplacementDirectories();
        g_packLoaded = false;
        g_packStatus = "off";
        host_log("live: texture pack off\n");
    }
}

/* texture_pack_reload: once a second (main thread, between frames), reload
 * the pack when its rt64.json or .rtz was rewritten (bc_packtool --watch;
 * the packer writes rt64.json last) */
void packWatch(unsigned viCount) {
    if (!g_cfg.texturePackReload || !g_packLoaded || !g_cfg.texturePackOn || viCount % 60 != 0) return;
    const long long now = packStamp(live::absPath(g_cfg, g_cfg.texturePack));
    if (now != 0 && now != g_packStamp) {
        host_log("live: the texture pack changed: reloading it\n");
        applyTexturePack();
    }
}

/* the texture dump (video.texture_dump, --dump-textures; live_texdump.cpp):
 * started, stopped, or restarted in another folder */
std::string g_dumpDir;
void applyTextureDump() {
    const std::string dir = live::absPath(g_cfg, g_cfg.textureDump.empty() ? "texture_dump" : g_cfg.textureDump);
    if (g_cfg.textureDumpOn && (!live::texDumpOn() || dir != g_dumpDir)) {
        std::string why;
        g_dumpDir = dir;
        if (!live::texDumpStart(dir, why)) g_cfg.textureDumpOn = false;
    } else if (!g_cfg.textureDumpOn && live::texDumpOn()) {
        live::texDumpStop();
    }
}

/* --texture-hashes F (for making texture packs and testing): the RT64
 * hashes of the textures loaded at frame F */
void logTextureHashes(unsigned frame) {
    std::unique_lock<std::mutex> lock(g_app->textureCache->textureMapMutex);
    const RT64::TextureMap &m = g_app->textureCache->textureMap;
    unsigned n = 0;
    for (size_t i = 0; i < m.textures.size(); i++)
        if (m.textures[i] != nullptr) {
            host_log("texture %016llx %ux%u (hash version %u, frame %u)\n", (unsigned long long) m.hashes[i],
                     unsigned(m.textures[i]->width), unsigned(m.textures[i]->height),
                     unsigned(RT64::TMEMHasher::CurrentHashVersion), frame);
            n++;
        }
    host_log("live: %u textures loaded at frame %u\n", n, frame);
}

/* video.present_pacing (bc.ini, the menu, --present-pacing): timer (the
 * default) leaves the pacing to RT64's own present timer, the lowest latency
 * (measured on an idle PC: as even as vsync pacing); vsync hands the pictures
 * to the display on its refreshes (port patch 0005, PresentQueue::holdFor),
 * which stays even when the PC is busy, for about half a refresh more
 * latency.  Vsync pacing applies only with vsync on and D3D12 (the patch
 * checks), and not with --no-pace (no real time to keep). */
/*
 * Port patch 0007 adds how the pictures between the game's are scheduled
 * when the vsync pacing is off (PresentQueue::scheduling):
 *  - timer: with vsync on and a frame rate that divides the display's (120 or
 *    60 on 120 Hz), locked to the display's refreshes (one picture per
 *    refresh, or per N), no timer: RT64's timer, which counted from the last
 *    present, beat against the refreshes at the display's own rate (a picture
 *    replaced before it was shown, the one before held two refreshes);
 *    otherwise an even timer (due times one period apart);
 *  - vrr (G-Sync / FreeSync): the even timer, presented with a sync interval
 *    of 1 when vsync is on (the display refreshes when a picture comes), at a
 *    rate a little under the display's highest (kVrrMargin) so it never waits
 *    for a fixed refresh;
 *  - auto: vsync off: timer; vsync on: vsync pacing at the game's own rate (no
 *    in-between pictures), timer (locked) when the frame rate divides the
 *    display's, else timer.  Windows doesn't say whether G-Sync / FreeSync is
 *    on, so auto never picks vrr (the log reports what the refreshes did).
 * --present-legacy (testing): RT64's own timer as before patch 0007. */
const char *g_pacingNote = "";

/* the frame rate RT64 aims at (0: the game's own) for a setting */
unsigned requestedRate() {
    if (g_cfg.framerate == 0) return 0;
    const unsigned hz = effectiveDisplayHz();
    return g_cfg.framerate < 0 ? hz : (hz != 0 ? std::min(unsigned(g_cfg.framerate), hz) : unsigned(g_cfg.framerate));
}

int pickPacing() {
    if (g_cfg.vsyncPacing) {
        g_pacingNote = "";
        return 1;
    }
    if (g_cfg.pacingOther == 2) {
        g_pacingNote = "";
        return 2;
    }
    if (g_cfg.pacingOther != 3) {
        g_pacingNote = "";
        return 0;
    }
    const unsigned hz = effectiveDisplayHz(), rate = requestedRate();
    if (!g_opt.vsync) {
        g_pacingNote = " (auto: vsync off)";
        return 0;
    }
    if (rate == 0) {
        g_pacingNote = " (auto: the game's own rate with vsync)";
        return 1;
    }
    g_pacingNote = hz != 0 && hz % rate == 0 ? " (auto: locked to the refreshes)" : " (auto: even timer)";
    return 0;
}

const char *pacingName() {
    static char buf[96];
    const char *base = g_pacing == 1 ? "vsync" : g_pacing == 2 ? "vrr" : "timer";
    snprintf(buf, sizeof buf, "%s%s%s", base, g_presentLegacy && g_pacing != 1 ? " (legacy RT64 timer)" : "",
             g_pacingNote);
    return buf;
}

void applyPresentPacing() {
    const int before = g_pacing;
    g_pacing = pickPacing();
    g_app->presentQueue->vsyncPacing = g_pacing == 1 && g_opt.pace;
    using S = RT64::PresentQueue::Scheduling;
    g_app->presentQueue->scheduling = uint32_t(g_presentLegacy ? S::Legacy : g_pacing == 2 ? S::Vrr : S::Locked);
    /* vrr caps the frame rate (configToRT64) */
    if ((before == 2) != (g_pacing == 2) && g_cfg.framerate != 0) {
        configToRT64(g_app->userConfig);
        g_app->userConfig.validate();
        g_app->updateUserConfig(false);
    }
}

/* the menu changed g_cfg: apply it now where that is possible */
void settingChanged(const live::Setting &s) {
    if (s.section == "video" && (s.key == "texture_pack" || s.key == "texture_pack_enabled")) {
        applyTexturePack();
        return;
    }
    if (s.section == "video" && (s.key == "texture_dump" || s.key == "texture_dump_enabled")) {
        applyTextureDump();
        return;
    }
    if (s.section == "video" && (s.key == "fps_counter" || s.key == "fps_counter_corner")) {
        counterApply();
    } else if (s.section == "video" && s.key == "interpolation") {
        interpApply();
    } else if (s.section == "video" && (s.key == "resolution" || s.key == "filtering" || s.key == "msaa" || s.key == "aspect" ||
                                 s.key == "framerate" || s.key == "hud_position")) {
        if (s.key == "framerate") interpApply();
        const uint32_t before = g_app->userConfig.msaaSampleCount();
        if (s.key == "msaa" && g_cfg.msaa > 1 && (msaaSupported() & unsigned(g_cfg.msaa)) == 0) {
            host_log("live: %dx MSAA isn't supported by this device\n", g_cfg.msaa);
            g_cfg.msaa = int(before > 1 ? before : 0);
        }
        configToRT64(g_app->userConfig);
        g_app->userConfig.validate();
        /* new sample count: RT64 rebuilds its targets and shaders (as its inspector does) */
        if (g_app->userConfig.msaaSampleCount() != before) g_app->updateMultisampling();
        g_app->updateUserConfig(s.key == "resolution" || s.key == "aspect");
        updateCullWide();
    } else if (s.section == "video") {
        if (s.key == "fullscreen") {
            if (g_cfg.fullscreen != g_opt.fullscreen) setFullscreen(g_cfg.fullscreen);
            if (live::menuIsOpen()) SDL_ShowCursor(SDL_ENABLE);
        } else if (s.key == "scale") {
            g_opt.scale = g_cfg.scale;
            if (!g_opt.fullscreen) SDL_SetWindowSize(g_window, int(320 * g_opt.scale), int(240 * g_opt.scale));
        } else if (s.key == "vsync") {
            g_opt.vsync = g_cfg.vsync;
            g_app->swapChain->setVsyncEnabled(g_opt.vsync);
            applyPresentPacing();   /* (auto follows vsync) */
        } else if (s.key == "present_pacing") {
            /* at once: the present thread reads the flag at every picture (an atomic; the
             * same switch the patch's own fallback to RT64's timer makes) */
            applyPresentPacing();
        }
        /* api: the next start */
    } else if (s.section == "game" && s.key == "debug_cheats") {
        cheatsApply();
    } else if (s.section == "game" && s.key == "cheats_save") {
        host_cheats_set_save(g_cfg.cheatsSaveSeparate ? 1 : 0);
        if (!g_cfg.cheatsSaveSeparate && host_cheats_saves() == 2)
            host_log("live: cheats_save = normal: this run keeps saving to the cheats files (it already switched)\n");
    } else if (s.section == "audio") {
        g_opt.volume = g_cfg.volume;
        g_opt.mute = g_cfg.mute;
    } else if (s.section == "camera" || s.section == "controller" || s.section == "keyboard") {
        live::orbitApply();   /* the camera's settings, or the bindings it leaves the N64 pad */
    }
    host_log("live: setting %s.%s = %s\n", s.section.c_str(), s.key.c_str(), live::configValue(g_cfg, s).c_str());
    /* frame times: a new segment when what they depend on changed */
    if (s.section == "video" && (s.key == "framerate" || s.key == "present_pacing" || s.key == "vsync" ||
                                 s.key == "fullscreen" || s.key == "msaa" || s.key == "resolution")) {
        if (s.key == "framerate") applyPresentPacing();   /* (auto follows the frame rate) */
        const std::string why = "video." + s.key + " = " + live::configValue(g_cfg, s);
        frameSegment(why.c_str());
        host_log("live: present pacing %s\n", pacingName());
    }
}

/* the menu's status line */
std::string rendererStatus() {
    char buf[256];
    int pw = 0, ph = 0;
    SDL_GetWindowSizeInPixels(g_window, &pw, &ph);
    float sx, sy;
    {
        std::scoped_lock<std::mutex> lock(g_app->sharedQueueResources->configurationMutex);
        sx = g_app->sharedQueueResources->resolutionScale.x;
        sy = g_app->sharedQueueResources->resolutionScale.y;
    }
    snprintf(buf, sizeof buf, "%s, window %dx%d%s, drawn at %.0fx%.0f, display %u Hz",
             g_opt.vulkan ? "Vulkan" : "Direct3D 12", pw, ph, g_opt.fullscreen ? " (fullscreen)" : "", 320.0f * sx,
             240.0f * sy, displayHz());
    return buf;
}

/* the level viewer, left after it loaded a level: bc.exe again with the same command line but the viewer's
 * options (a fresh boot), then this one exits */
std::wstring quoteArg(const std::wstring &a) {
    if (!a.empty() && a.find_first_of(L" \t\"") == std::wstring::npos) return a;
    std::wstring q = L"\"";
    unsigned slashes = 0;
    for (wchar_t c : a) {
        if (c == L'\\') {
            slashes++;
            continue;
        }
        q.append(c == L'"' ? slashes * 2 + 1 : slashes, L'\\');
        slashes = 0;
        q += c;
    }
    q.append(slashes * 2, L'\\');
    return q + L"\"";
}

void restartWithoutViewer() {
    int argc = 0;
    wchar_t **wargv = CommandLineToArgvW(GetCommandLineW(), &argc);
    std::wstring cmd;
    for (int i = 0; wargv != nullptr && i < argc; i++) {
        const std::wstring a = wargv[i];
        if (a == L"--viewer") {
            if (i + 1 < argc && wargv[i + 1][0] >= L'0' && wargv[i + 1][0] <= L'9') i++;
            continue;
        }
        if (a == L"--viewer-test") {
            i++;
            continue;
        }
        cmd += (cmd.empty() ? L"" : L" ") + quoteArg(a);
    }
    wchar_t exe[MAX_PATH];
    GetModuleFileNameW(nullptr, exe, MAX_PATH);
    STARTUPINFOW si = {};
    si.cb = sizeof si;
    PROCESS_INFORMATION pi = {};
    if (CreateProcessW(exe, &cmd[0], nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi)) {
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
        host_log("live: started bc.exe again (pid %lu) without the viewer\n", (unsigned long) pi.dwProcessId);
    } else {
        host_log("live: could not start bc.exe again (error %lu)\n", (unsigned long) GetLastError());
    }
    shutdown(0);
}

/* the save files' folder must exist before the platform opens them */
void makeSaveDirs(const HostOpts *o) {
    for (const char *p : {o->eeprom_path, o->mpk_path}) {
        if (p == nullptr) continue;
        std::wstring w = live::wide(p);
        size_t slash = w.find_last_of(L"\\/");
        if (slash == std::wstring::npos || slash == 0) continue;
        std::wstring dir = w.substr(0, slash);
        wchar_t full[32768];
        if (GetFullPathNameW(dir.c_str(), 32768, full, nullptr) == 0) continue;
        int r = SHCreateDirectoryExW(nullptr, full, nullptr);
        if (r != ERROR_SUCCESS && r != ERROR_ALREADY_EXISTS && r != ERROR_FILE_EXISTS)
            g_cfg.warnings.push_back("can't create the saves folder " + live::utf8(full) +
                                     " (error " + std::to_string(r) + "): the game won't be saved");
    }
}

void prestart(HostOpts *o) {
    host_log("live: Blast Corps port %s, ROM %s\n", BC_VERSION, o->rom_path);
    host_log("live: saves: EEPROM %s, Controller Pak %s\n", o->eeprom_path ? o->eeprom_path : "(not kept)",
             o->mpk_path ? o->mpk_path : "(none)");
    /* the debug cheats at boot: [game] debug_cheats = 1 puts -c on the game's debug command line (the same as
     * --cmdline -c); a --cmdline that already turns them on counts as the setting on (for this run) */
    {
        static std::string cmd;
        const bool given = host_cmdline_cheats(o->cmdline) != 0;
        if (g_cfg.debugCheats && !given) {
            cmd = o->cmdline != nullptr && o->cmdline[0] != 0 ? "-c " + std::string(o->cmdline) : "-c";
            o->cmdline = cmd.c_str();
        }
        if (given) g_cfg.debugCheats = true;
        g_cfg.cheatsSaveSeparate = o->cheats_save != 0;   /* (bc.ini's, or --cheats-save) */
        host_log("live: debug cheats %s at boot%s, cheats_save = %s; debug command line \"%s\"\n",
                 g_cfg.debugCheats ? "on" : "off", given ? " (--cmdline)" : "",
                 g_cfg.cheatsSaveSeparate ? "separate" : "normal", o->cmdline != nullptr ? o->cmdline : "");
    }
    makeSaveDirs(o);
    /* bc.ini's [mods] enabled (--no-mods: none; --mod FILE was loaded by host_main) */
    if (!g_noMods && !g_cfg.mods.empty()) {
        const std::string dir = live::modsFolder(g_cfg);
        std::string problems;
        for (const std::string &m : g_cfg.mods) {
            std::string path = dir + m;
            if (path.size() < 6 || _stricmp(path.c_str() + path.size() - 6, ".bcmod") != 0) path += ".bcmod";
            char err[1024];
            if (port_mods_add(path.c_str(), err, sizeof err) != 0) {
                host_log("live: mods: %s not used: %s\n", m.c_str(), err);
                problems += m + ": " + err + "\n\n";
            }
        }
        if (!problems.empty())
            host_message(("These mods ([mods] enabled in bc.ini) are not used; the game starts without them:\n\n" +
                          problems).c_str(), 0);
    }
    if (port_mods_count() > 0) {
        std::string names;
        for (int i = 0; i < port_mods_count(); i++) names += std::string(i ? ", " : "") + port_mods_name(i);
        host_log("live: mods in use: %s\n", names.c_str());
    }
    if (!g_cfg.warnings.empty()) {
        std::string msg = "Some settings were not understood and their defaults are used:\n\n";
        for (const std::string &w : g_cfg.warnings) {
            host_log("live: config: %s\n", w.c_str());
            msg += w + "\n";
        }
        host_message(msg.c_str(), 0);
    }

    timeBeginPeriod(1);   /* 1 ms Sleep granularity for the pacing */
    /* DPI aware: the swap chain gets the display's real pixels (a sharp
     * picture on scaled desktops, e.g. 1920x1080 at 125 %); window sizes stay
     * in desktop points, so the window looks as large as before */
    SDL_SetHint(SDL_HINT_WINDOWS_DPI_AWARENESS, "permonitorv2");
    SDL_SetHint(SDL_HINT_WINDOWS_DPI_SCALING, "1");
    /* game controllers keep working when another window takes the focus (the
     * Game Bar of the Xbox button, a notification): SDL's default drops their
     * input until the window gets the focus back, which a controller-only
     * player in fullscreen can't do (the keyboard stops too: it can't be
     * helped; a controller button takes the focus back, pumpEvents) */
    SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");
    /* the controllers run on their own thread (live_input.h InputThread): SDL_PollEvent must not update them
     * (their device detection took up to 400 ms inside it on a PC with a Bluetooth pad) */
    SDL_SetHint(SDL_HINT_AUTO_UPDATE_JOYSTICKS, "0");
    SDL_SetMainReady();
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS) != 0) host_fatal("Can't start SDL (video): %s", SDL_GetError());
    {
        std::string err;
        if (!g_input.start(err)) host_fatal("Can't start SDL's game controllers: %s", err.c_str());
        g_pads = g_input.latest();
        host_log("live: controllers: SDL's game controller subsystem started on its own thread in %.0f ms (with "
                 "its first device detection), %u found\n",
                 g_input.take().initMs, unsigned(g_pads.pads.size()));
    }
    /* the window at 320x240 times the scale, as large as fits the desktop */
    SDL_Rect usable;
    unsigned scale = g_opt.scale;
    if (SDL_GetDisplayUsableBounds(0, &usable) == 0)
        while (scale > 1 && (320 * scale > unsigned(usable.w) || 240 * scale + 40 > unsigned(usable.h))) scale--;
    Uint32 flags = SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI;
    if (g_opt.fullscreen) flags |= SDL_WINDOW_FULLSCREEN_DESKTOP;
    const int winW = g_opt.windowW != 0 ? int(g_opt.windowW) : int(320 * scale);
    const int winH = g_opt.windowH != 0 ? int(g_opt.windowH) : int(240 * scale);
    g_window = SDL_CreateWindow(kTitle, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, winW, winH, flags);
    if (g_window == nullptr) host_fatal("Can't create the game window: %s", SDL_GetError());
    SDL_SetWindowMinimumSize(g_window, 320, 240);
    if (g_opt.fullscreen) SDL_ShowCursor(SDL_DISABLE);
    SDL_SysWMinfo wm;
    SDL_VERSION(&wm.version);
    if (!SDL_GetWindowWMInfo(g_window, &wm)) host_fatal("SDL_GetWindowWMInfo: %s", SDL_GetError());
    host_gui_window = wm.info.win.window;

    RT64::Application::Core core = {};
    core.window = wm.info.win.window;
    core.HEADER = g_header;
    core.RDRAM = rdramBase();
    core.DMEM = g_dmem;
    core.IMEM = g_imem;
    core.MI_INTR_REG = &MI_INTR_REG;
    core.DPC_START_REG = &DPC_START_REG;
    core.DPC_END_REG = &DPC_END_REG;
    core.DPC_CURRENT_REG = &DPC_CURRENT_REG;
    core.DPC_STATUS_REG = &DPC_STATUS_REG;
    core.DPC_CLOCK_REG = &DPC_CLOCK_REG;
    core.DPC_BUFBUSY_REG = &DPC_BUFBUSY_REG;
    core.DPC_PIPEBUSY_REG = &DPC_PIPEBUSY_REG;
    core.DPC_TMEM_REG = &DPC_TMEM_REG;
    core.VI_STATUS_REG = &VI_STATUS_REG;
    core.VI_ORIGIN_REG = &VI_ORIGIN_REG;
    core.VI_WIDTH_REG = &VI_WIDTH_REG;
    core.VI_INTR_REG = &VI_INTR_REG;
    core.VI_V_CURRENT_LINE_REG = &VI_V_CURRENT_LINE_REG;
    core.VI_TIMING_REG = &VI_TIMING_REG;
    core.VI_V_SYNC_REG = &VI_V_SYNC_REG;
    core.VI_H_SYNC_REG = &VI_H_SYNC_REG;
    core.VI_LEAP_REG = &VI_LEAP_REG;
    core.VI_H_START_REG = &VI_H_START_REG;
    core.VI_V_START_REG = &VI_V_START_REG;
    core.VI_V_BURST_REG = &VI_V_BURST_REG;
    core.VI_X_SCALE_REG = &VI_X_SCALE_REG;
    core.VI_Y_SCALE_REG = &VI_Y_SCALE_REG;
    core.checkInterrupts = checkInterrupts;

    RT64::ApplicationConfiguration appConfig;
    appConfig.useConfigurationFile = false;
    appConfig.detectDataPath = false;
    g_app = new RT64::Application(core, appConfig);
    g_app->userConfig.graphicsAPI =
        g_opt.vulkan ? RT64::UserConfiguration::GraphicsAPI::Vulkan : RT64::UserConfiguration::GraphicsAPI::D3D12;
    configToRT64(g_app->userConfig);
    if (g_opt.paceLog != nullptr) g_presentTimes = new double[kMaxPresents];
    /* the present thread's hook: --pace-log's times, the settings menu */
    RT64::SetRenderHooks(nullptr, drawHook, deinitHook);
    RT64::Application::SetupResult res = g_app->setup(GetCurrentThreadId());
    if (res != RT64::Application::SetupResult::Success || !g_app->device)
        host_fatal("The renderer (RT64) could not start with %s (setup result %d).\n\n"
                   "Your graphics card or driver may not support it. Try the other graphics API: set\n"
                   "    api = %s\nin bc.ini (or start bc.exe --api %s), and make sure your graphics driver is up to date.",
                   g_opt.vulkan ? "Vulkan" : "Direct3D 12", int(res), g_opt.vulkan ? "d3d12" : "vulkan",
                   g_opt.vulkan ? "d3d12" : "vulkan");
    g_app->swapChain->setVsyncEnabled(g_opt.vsync);
    applyPresentPacing();
    if (g_ubershadersOnly) {   /* (testing) every draw with RT64's ubershader: what new materials cost the GPU */
        g_app->workloadQueue->ubershadersOnly = true;
        host_log("live: --ubershaders-only: every draw with RT64's ubershader\n");
    }
    /* frame times: every present (and --flip-log's records) */
    if (!g_opt.vulkan) g_ft.dxgi = static_cast<plume::D3D12SwapChain *>(g_app->swapChain.get())->d3d;
    g_app->presentQueue->presentedHook = framePresented;
    flipStart();
    live::MenuEnv menu;
    menu.app = g_app;
    menu.window = g_window;
    menu.cfg = &g_cfg;
    menu.changed = settingChanged;
    menu.quit = [] { g_quitRequested = true; };
    menu.status = rendererStatus;
    menu.msaaSupported = msaaSupported;
    menu.padNames = padNames;
    menu.packStatus = [] { return g_packStatus; };
    menu.reloadPack = [] {
        if (g_cfg.texturePackOn && !g_cfg.texturePack.empty()) applyTexturePack();
    };
    menu.dumpStatus = [] { return live::texDumpOn() ? live::texDumpStatus() : std::string(); };
    menu.cheatsStatus = cheatsSaveText;
    menu.viewerTab = live::viewerMenuTab;
    live::menuSetup(menu);
    {
        live::ViewerEnv v;
        v.app = g_app;
        v.window = g_window;
        v.cfg = &g_cfg;
        v.viOrigin = &VI_ORIGIN_REG;
        v.viCurrentLine = &VI_V_CURRENT_LINE_REG;
        v.pads = [] { return g_input.latest(); };
        v.saveWindow = saveWindowTo;
        v.toggleFullscreen = [] { setFullscreen(!g_opt.fullscreen); };
        v.quit = [](int code) { shutdown(code); };
        v.restart = restartWithoutViewer;
        if (strcmp(g_opt.shotDir, ".") != 0) v.shotDir = g_opt.shotDir;
        live::viewerSetup(v);
    }
    live::interpTagsSetup(g_app);
    live::interpTagsSetLog(g_interp.log);
    live::interpTagsEnable(g_cfg.framerate != 0 && g_cfg.interpolation == 1);
    live::interpTagsAutoIds(g_cfg.framerate != 0 && g_cfg.interpolation == 0);
    live::orbitSetup(g_app, &g_cfg, g_window);
    live::throttleSetup(&g_cfg, g_throttleLog);
    if (g_cfg.fpsCounter != 0) counterApply();
    cheatsMarker();
    host_crash_rearm();   /* in case the renderer or a driver installed its own filter */
    int ww, wh, pw, ph;
    SDL_GetWindowSize(g_window, &ww, &wh);
    SDL_GetWindowSizeInPixels(g_window, &pw, &ph);
    host_log("live: RT64 up (%s), window %dx%d (%dx%d pixels)%s, vsync %s, display %u Hz, present pacing %s\n",
             g_opt.vulkan ? "Vulkan" : "D3D12", ww, wh, pw, ph, g_opt.fullscreen ? " fullscreen" : "",
             g_opt.vsync ? "on" : "off", displayHz(),
             g_pacing == 1 && !g_opt.pace ? "timer (--no-pace)" : pacingName());
    frameSegment("start");
    if (g_cfg.resolution != 0 || g_cfg.msaa != 0 || g_cfg.filtering != 0 || g_cfg.aspect != 0 ||
        g_cfg.framerate != 0 || g_app->userConfig.msaaSampleCount() > 1)
        host_log("live: video: resolution %s, MSAA %ux%s, filtering %s, aspect %s (HUD %s), frame rate %s\n",
                 live::configValue(g_cfg, {"video", "resolution"}).c_str(), g_app->userConfig.msaaSampleCount(),
                 g_cfg.msaa > 1 && g_app->userConfig.msaaSampleCount() < 2 ? " (not supported here)" : "",
                 live::configValue(g_cfg, {"video", "filtering"}).c_str(),
                 live::configValue(g_cfg, {"video", "aspect"}).c_str(),
                 live::configValue(g_cfg, {"video", "hud_position"}).c_str(),
                 live::configValue(g_cfg, {"video", "framerate"}).c_str());
    if (g_interp.displayHz != 0) host_log("live: --display-hz %u: RT64 told the display runs at %u Hz\n",
                                          g_interp.displayHz, g_interp.displayHz);
    updateCullWide();
    applyTexturePack();
    applyTextureDump();
    o->live = &kLive;
}

}  // namespace bcexe
