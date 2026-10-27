/* bc.exe's start (live_rt64.h): the command line (extraArg: the options live_rt64.cpp's header comment
 * lists; host_main takes bc_headless's), bc.ini and --set, the log (bc.log), the ROM (argument, bc.ini or a
 * file dialog, then the check), the crash handlers, and main. */
#include <windows.h>
#include <shellapi.h>
#include <io.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <SDL.h>

#include "live_input.h"
#include "live_interp.h"
#include "live_orbit.h"
#include "live_rt64.h"
#include "live_setup.h"
#include "live_texdump.h"
#include "live_viewer.h"
#include "live_wide.h"

extern "C" {
#include "plat_host.h"
#include "load/port_load.h"
}

namespace bcexe {

/* ---- start-up ---------------------------------------------------------------- */
unsigned num(const char *s) {
    return unsigned(strtoul(s, nullptr, 0));
}

/* options that set a bc.ini setting for the run (main applies them) */
const std::pair<const char *, const char *> kSetAliases[] = {
    {"--resolution", "video.resolution"},
    {"--msaa", "video.msaa"},
    {"--filtering", "video.filtering"},
    {"--aspect", "video.aspect"},
    {"--hud-position", "video.hud_position"},
    {"--framerate", "video.framerate"},
    {"--interpolation", "video.interpolation"},
    {"--fps-counter", "video.fps_counter"},
    {"--texture-pack", "video.texture_pack"},   /* (and turns it on) */
    {"--dump-textures", "video.texture_dump"},  /* (and turns it on) */
    {"--camera", "camera.mode"},
    {"--present-pacing", "video.present_pacing"},
};

int extraArg(int argc, char **argv, int *i, HostOpts *o) {
    const char *a = argv[*i];
    auto next = [&]() -> const char * {
        if (*i + 1 >= argc) host_fatal("%s needs a value", a);
        return argv[++*i];
    };
    for (const auto &alias : kSetAliases)
        if (!strcmp(a, alias.first)) {
            next();
            return 1;
        }
    if (!strcmp(a, "--api")) g_opt.vulkan = !strcmp(next(), "vulkan");
    else if (!strcmp(a, "--fullscreen")) g_opt.fullscreen = true;
    else if (!strcmp(a, "--windowed")) g_opt.fullscreen = false;
    else if (!strcmp(a, "--vsync")) g_opt.vsync = true;
    else if (!strcmp(a, "--no-vsync")) g_opt.vsync = false;
    else if (!strcmp(a, "--no-rom-check")) g_opt.romCheck = false;
    else if (!strcmp(a, "--no-mods")) g_noMods = true;   /* bc.ini's [mods] enabled not used (--mod still is) */
    else if (!strcmp(a, "--pace-log")) g_opt.paceLog = next();
    else if (!strcmp(a, "--flip-log")) g_flip.path = next();
    else if (!strcmp(a, "--perf-log")) g_perf.path = next();
    else if (!strcmp(a, "--spike-log")) {
        const char *s = next();
        g_ft.spikeLog = static_cast<FILE *>(host_fopen(s, "w"));
        if (g_ft.spikeLog == nullptr) host_fatal("--spike-log %s: can't write it", s);
        fprintf(g_ft.spikeLog, "t_ms,interval_ms,target_ms,cause,cause_name,retrace_ms,game_ms,lists_ms,orbit_ms,"
                               "submit_ms,gpu_ms,render_ms,frame_ms,shaders,textures,orbit_active,segment,frame,queue_full,"
                               "content_step_ms,weight\n");
    }
    else if (!strcmp(a, "--picture-log")) {
        const char *s = next();
        g_ft.pictureLog = static_cast<FILE *>(host_fopen(s, "w"));
        if (g_ft.pictureLog == nullptr) host_fatal("--picture-log %s: can't write it", s);
        fprintf(g_ft.pictureLog, "t_ms,interval_ms,target_ms,content_ms,weight,workload,picture,pictures,sync_interval,"
                                 "suspended,ready_ms\n");
    }
    else if (!strcmp(a, "--view-log")) live::orbitSetViewLog(next());
    else if (!strcmp(a, "--config") || !strcmp(a, "--no-config")) {
        if (a[2] == 'c') next();   /* read before host_main (main) */
    } else if (!strcmp(a, "--saves")) {
        g_cfg.saves = next();
        o->eeprom_path = (g_eepromPath = live::absPath(g_cfg, g_cfg.saves) + "\\blastcorps.eep").c_str();
        o->mpk_path = g_cfg.pak ? (g_mpkPath = live::absPath(g_cfg, g_cfg.saves) + "\\blastcorps.mpk").c_str() : nullptr;
    } else if (!strcmp(a, "--no-pak")) {
        o->mpk_path = nullptr;
    } else if (!strcmp(a, "--no-saves")) {
        o->eeprom_path = nullptr;
        o->mpk_path = nullptr;
    }
    else if (!strcmp(a, "--set")) next();   /* applied with the config file (main) */
    else if (!strcmp(a, "--low-test")) next();   /* read first thing (main) */
    else if (!strcmp(a, "--present-legacy")) g_presentLegacy = true;
    else if (!strcmp(a, "--ubershaders-only")) g_ubershadersOnly = true;
    else if (!strcmp(a, "--window")) {
        const char *s = next();
        if (sscanf(s, "%ux%u", &g_opt.windowW, &g_opt.windowH) != 2 || g_opt.windowW < 160 || g_opt.windowH < 120)
            host_fatal("--window %s: WIDTHxHEIGHT, e.g. 1680x720", s);
    } else if (!strcmp(a, "--wshot")) {
        std::string s = next();
        size_t p = 0;
        while (p < s.size()) {
            size_t q = s.find(',', p);
            if (q == std::string::npos) q = s.size();
            g_opt.wshots.push_back(num(s.substr(p, q - p).c_str()));
            p = q + 1;
        }
        std::sort(g_opt.wshots.begin(), g_opt.wshots.end());
    } else if (!strcmp(a, "--menu-shot")) g_opt.menuShot = std::max(1u, num(next()));
    else if (!strcmp(a, "--menu-set")) g_opt.menuSets.push_back(next());
    else if (!strcmp(a, "--texture-hashes")) g_hashFrame = num(next());
    else if (!strcmp(a, "--display-hz")) g_interp.displayHz = std::max(1u, num(next()));
    else if (!strcmp(a, "--interp-log")) g_interp.log = true;
    else if (!strcmp(a, "--orbit-log")) live::orbitSetLog(true);
    else if (!strcmp(a, "--orbit-steplog")) live::orbitSetStepLog(true);
    else if (!strcmp(a, "--throttle-log")) g_throttleLog = true;
    else if (!strcmp(a, "--orbit-repeatable")) live::orbitSetRepeatable(true);
    else if (!strcmp(a, "--orbit-cull")) {
        const char *s = next();
        if (!live::orbitSetCull(s)) host_fatal("--orbit-cull %s: all or view", s);
    }
    else if (!strcmp(a, "--orbit-test")) {
        const char *s = next();
        if (!live::orbitSetTest(s)) host_fatal("--orbit-test %s: YAWSPEED[:PITCH[:ZOOM]], e.g. 60:25:1.5, or sweep", s);
    }
    else if (!strcmp(a, "--tag-test")) live::interpTagsSetTest(num(next()));
    else if (!strcmp(a, "--wseq")) {
        const char *s = next();
        if (sscanf(s, "%u:%u", &g_wseq.frame, &g_wseq.ms) != 2) host_fatal("--wseq %s: FRAME:MILLISECONDS", s);
    }
    else if (!strcmp(a, "--viewer")) {
        /* --viewer [LEVEL]: the level viewer (live_viewer.h), on LEVEL (default 0) */
        int level = 0;
        if (*i + 1 < argc && argv[*i + 1][0] >= '0' && argv[*i + 1][0] <= '9') level = int(num(next()));
        if (level < 0 || level >= 60) host_fatal("--viewer %d: levels are 0..59", level);
        live::viewerStartAtBoot(level);
        g_viewerBoot = true;
    } else if (!strcmp(a, "--viewer-open")) {
        g_viewerOpenFrame = std::max(1u, num(next()));   /* (testing) open the viewer over the game at frame F */
    } else if (!strcmp(a, "--viewer-test")) {
        const char *s = next();
        if (!live::viewerSetTest(s)) host_fatal("--viewer-test %s: OP;OP;... (viewer_script.cpp runTestOp)", s);
    }
    else if (!strcmp(a, "--no-pace")) g_opt.pace = false;
    else if (!strcmp(a, "--pace-from")) g_opt.paceFrom = num(next());
    else if (!strcmp(a, "--scale")) g_opt.scale = std::max(1u, num(next()));
    else if (!strcmp(a, "--shot-dir")) g_opt.shotDir = next();
    else if (!strcmp(a, "--shot-every")) g_opt.shotEvery = num(next());
    else if (!strcmp(a, "--no-gfx-fix")) g_opt.gfxFix = false;
    else if (!strcmp(a, "--mute")) g_opt.mute = true;
    else if (!strcmp(a, "--volume")) g_opt.volume = std::max(0, std::min(100, int(num(next()))));
    else if (!strcmp(a, "--gfx-fix-log")) port_gfx_debug = 1;
    else if (!strcmp(a, "--dl-dump-every")) g_opt.dlDumpEvery = num(next());
    else if (!strcmp(a, "--dl-skip")) {
        const char *s = next(), *colon = strchr(s, ':');
        g_skipLo = num(s);
        g_skipHi = colon != nullptr ? num(colon + 1) : g_skipLo + 8;
    }
    else if (!strcmp(a, "--dl-dump")) {
        const char *s = next(), *colon = strchr(s, ':');
        g_opt.dlDumpFrame = num(s);
        if (colon != nullptr) g_opt.dlDumpTasks = num(colon + 1);
    }
    else if (!strcmp(a, "--shot")) {
        std::string s = next();
        size_t p = 0;
        while (p < s.size()) {
            size_t q = s.find(',', p);
            if (q == std::string::npos) q = s.size();
            g_opt.shots.push_back(num(s.substr(p, q - p).c_str()));
            p = q + 1;
        }
        std::sort(g_opt.shots.begin(), g_opt.shots.end());
    } else
        return 0;
    return 1;
}

/* ---- the process: GUI program, log, config, ROM ----------------------------- */

/* GUI subsystem: stderr exists when the parent handed us one (a console
 * redirect, WSL, a script); otherwise the log goes to bc.log next to the
 * exe (or in %TEMP% if that folder can't be written) */
void setupLog(const std::string &dir) {
    HANDLE h = GetStdHandle(STD_ERROR_HANDLE);
    if (h != nullptr && h != INVALID_HANDLE_VALUE && GetFileType(h) != FILE_TYPE_UNKNOWN) return;
    static std::string path;
    path = dir + "bc.log";
    FILE *f = _wfreopen(live::wide(path).c_str(), L"w", stderr);
    if (f == nullptr) {
        wchar_t tmp[MAX_PATH + 1];
        GetTempPathW(MAX_PATH + 1, tmp);
        path = live::utf8(tmp) + "bc.log";
        f = _wfreopen(live::wide(path).c_str(), L"w", stderr);
    }
    if (f != nullptr) {
        setvbuf(stderr, nullptr, _IONBF, 0);
        host_log_path = path.c_str();
        /* stdout (RT64's messages) into the same open file */
        _dup2(_fileno(stderr), _fileno(stdout));
        setvbuf(stdout, nullptr, _IONBF, 0);
    }
}

/* host_main's hooks */
void optsHook(HostOpts *o) {
    g_opt.vulkan = g_cfg.vulkan;
    g_opt.scale = g_cfg.scale;
    g_opt.fullscreen = g_cfg.fullscreen;
    g_opt.vsync = g_cfg.vsync;
    g_opt.volume = g_cfg.volume;
    g_opt.mute = g_cfg.mute;
    o->cheats_save = g_cfg.cheatsSaveSeparate ? 1 : 0;   /* (--cheats-save on the command line overrides it) */
    if (!g_cfg.file.empty() && !g_cfg.saves.empty()) {
        const std::string dir = live::absPath(g_cfg, g_cfg.saves);
        g_eepromPath = dir + "\\blastcorps.eep";
        o->eeprom_path = g_eepromPath.c_str();
        if (g_cfg.pak) {
            g_mpkPath = dir + "\\blastcorps.mpk";
            o->mpk_path = g_mpkPath.c_str();
        }
    }
}

void romHook(HostOpts *o) {
    static std::string chosen;
    if (o->rom_path != nullptr) {   /* given on the command line */
        if (g_opt.romCheck) {
            std::string why = live::romCheck(o->rom_path);
            if (!why.empty()) host_fatal("%s", why.c_str());
        }
        return;
    }
    if (!g_cfg.rom.empty()) {
        chosen = live::absPath(g_cfg, g_cfg.rom);
        std::string why = live::romCheck(chosen);
        if (why.empty()) {
            o->rom_path = chosen.c_str();
            return;
        }
        host_log("live: configured ROM: %s\n", why.c_str());
        if (host_no_msgbox) host_fatal("The ROM set in %s can't be used: %s", g_cfg.file.c_str(), why.c_str());
        host_message(("The ROM set in " + g_cfg.file + " can't be used:\n\n" + why + "\n\nPlease choose your ROM.")
                         .c_str(), 0);
    }
    /* --no-msgbox: no file dialog either */
    if (host_no_msgbox) host_fatal("No ROM was given (--no-msgbox: no file dialog).");
    for (;;) {
        chosen = live::romDialog();
        if (chosen.empty())
            host_fatal("Blast Corps needs your own copy of the game: a ROM image of Blast Corps (USA) (Rev 1), "
                       "also known as v1.1, as a .z64, .v64 or .n64 file.\n\n"
                       "No ROM was chosen, so the game will close. Start it again to choose one, or put its path "
                       "in bc.ini (rom = ...).");
        std::string why = live::romCheck(chosen);
        if (why.empty()) break;
        host_log("live: chosen ROM: %s\n", why.c_str());
        host_message((why + "\n\nPlease choose another file.").c_str(), 1);
    }
    o->rom_path = chosen.c_str();
    if (live::configSetRom(g_cfg, chosen)) host_log("live: ROM %s remembered in %s\n", chosen.c_str(), g_cfg.file.c_str());
}

/* an uncaught C++ exception (RT64, the standard library): a crash report */
void terminateHandler() {
    static char why[512];
    const char *what = "not a std::exception";
    try {
        std::exception_ptr e = std::current_exception();
        if (e) std::rethrow_exception(e);
        what = "std::terminate called without an exception";
    } catch (const std::exception &x) {
        snprintf(why, sizeof why, "uncaught C++ exception: %s", x.what());
        host_crash_now(why);
    } catch (...) {
    }
    snprintf(why, sizeof why, "uncaught C++ exception (%s)", what);
    host_crash_now(why);
}

/* SDL_assert_release failures: a crash report (SDL's own handler shows a box) */
SDL_AssertState SDLCALL sdlAssert(const SDL_AssertData *d, void *) {
    static char why[512];
    snprintf(why, sizeof why, "SDL assertion failed: %s (%s:%d, %s)", d->condition, d->filename, d->linenum,
             d->function);
    host_crash_now(why);
}

void crashTestCxx() {
    throw std::runtime_error("crash test (--crash-test cxx)");
}

}  // namespace bcexe

using namespace bcexe;

int main(int, char **) {
    host_gui = 1;
    /* the command line as UTF-8 (paths with any characters) */
    int argc = 0;
    wchar_t **wargv = CommandLineToArgvW(GetCommandLineW(), &argc);
    static std::vector<std::string> args;
    std::vector<char *> argv;
    for (int i = 0; i < argc; i++) args.push_back(live::utf8(wargv[i]));
    for (std::string &s : args) argv.push_back(&s[0]);
    argv.push_back(nullptr);
    /* first: the host memory below 4 GB RT64 reads through extended addresses
     * (the widescreen ticker), before SDL, RT64 and the graphics driver take
     * the address space (plat_host.h) */
    for (int i = 1; i + 1 < argc; i++)
        if (args[i] == "--low-test") host_low_test = num(args[i + 1].c_str());
    host_low_reserve();
    host_crash_init(argc, argv.data(), "bc", BC_VERSION);   /* reads --no-msgbox, --crash-dir */
    std::set_terminate(terminateHandler);
    SDL_SetAssertionHandler(sdlAssert, nullptr);
    host_crash_test_cxx = crashTestCxx;
    QueryPerformanceFrequency(&g_freq);
    QueryPerformanceCounter(&g_tStart);

    g_cfg.dir = live::exeDir();
    setupLog(g_cfg.dir);
    /* crash dumps next to the log (bc.log's folder; the exe's when the log is stderr) */
    if (std::find(args.begin(), args.end(), std::string("--crash-dir")) == args.end()) {
        std::string dir = g_cfg.dir;
        if (host_log_path != nullptr) {
            dir = host_log_path;
            dir.resize(dir.find_last_of("\\/") + 1);
        }
        host_crash_set_dir(dir.c_str());
    }
    std::string configFile = g_cfg.dir + "bc.ini";
    bool noConfig = false;
    for (int i = 1; i < argc; i++) {
        if (args[i] == "--no-config") noConfig = true;
        else if (args[i] == "--config" && i + 1 < argc) configFile = live::absPath(g_cfg, args[i + 1]);
    }
    if (noConfig) live::configDefaults(g_cfg);
    else live::configLoad(g_cfg, configFile);
    /* --set section.key=value: a setting for this run (over bc.ini; not saved);
     * --resolution X etc. are short for --set video.resolution=X */
    for (int i = 1; i + 1 < argc; i++) {
        std::string spec;
        if (args[i] == "--set") spec = args[i + 1];
        for (const auto &alias : kSetAliases)
            if (args[i] == alias.first) spec = std::string(alias.second) + "=" + args[i + 1];
        if (spec.empty()) continue;
        if (args[i] == "--texture-pack") g_cfg.texturePackOn = true;
        if (args[i] == "--dump-textures") g_cfg.textureDumpOn = true;
        i++;
        if (!live::configSet(g_cfg, spec) && host_no_msgbox) host_fatal("%s", g_cfg.warnings.back().c_str());
    }
    host_exit_hook = [] {
        if (g_wseq.thread.joinable()) g_wseq.thread.join();
        flipWrite();
        frameEnd();
        writePaceLog();
        perfWrite();
        if (g_dlTasks != 0)
            host_log("live: frame rate option: %.3f ms per task in processDisplayLists (%u tasks, interpolation %s)\n",
                     g_dlSeconds * 1e3 / g_dlTasks, g_dlTasks, live::interpTagsOn() ? "tagged" : "auto");
        live::texDumpStop();   /* (the files still queued are written, then the index) */
        live::interpTagsSummary();
        live::wideSummary();
        host_low_summary();
        live::orbitSummary();
        live::throttleSummary();
        live::viewerSummary();
    };
    host_opts_hook = optsHook;
    host_rom_hook = romHook;
    return host_main(argc, argv.data(), extraArg, prestart);
}
