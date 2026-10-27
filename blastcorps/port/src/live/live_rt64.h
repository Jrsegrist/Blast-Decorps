/* bc.exe's own state, shared by its parts (all in namespace bcexe):
 *   live_rt64.cpp        the window, RT64, input, sound, the frame-rate interpolation's cuts, events, the
 *                        platform's HostLive hooks, the settings menu's changes, start-up (prestart)
 *   live_frametimes.cpp  measuring: --pace-log, --flip-log, the frame-rate counter, the frame-time
 *                        report and its spike causes, --perf-log
 *   live_shots.cpp       screenshots (--shot, --wshot, --menu-shot, the viewer's) and --wseq
 *   live_start.cpp       the command line (extraArg), bc.ini, the log, the ROM, main
 * live_rt64.cpp's header comment describes bc.exe and lists its options. */
#ifndef LIVE_RT64_H
#define LIVE_RT64_H

#include <windows.h>

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <SDL.h>

#include "hle/rt64_application.h"

#include "live_input.h"
#include "live_orbit.h"
#include "live_setup.h"
#include "orbit_math.h"

extern "C" {
#include "plat_host.h"
}

#ifndef BC_VERSION
#define BC_VERSION "dev"
#endif

struct IDXGISwapChain;

namespace bcexe {

/* ---- options ------------------------------------------------------------- */
struct LiveOpts {
    bool vulkan = false;
    bool pace = true;
    unsigned paceFrom = 0;           /* --pace-from F (testing): as fast as it goes before game frame F */
    unsigned scale = 3;              /* window size: 320x240 times this */
    bool fullscreen = false;
    bool vsync = true;
    std::vector<unsigned> shots;     /* frame numbers (game frames) to save */
    unsigned shotEvery = 0;
    const char *shotDir = ".";
    unsigned dlDumpFrame = ~0u, dlDumpTasks = 12, dlDumpEvery = 0;
    bool gfxFix = true;
    bool mute = false;
    int volume = 100;                /* percent */
    bool romCheck = true;
    const char *paceLog = nullptr;
    unsigned windowW = 0, windowH = 0;   /* --window WxH: the window's size in desktop points */
    std::vector<unsigned> wshots;        /* --wshot: frames whose window picture is saved */
    unsigned menuShot = ~0u;             /* --menu-shot F: open the menu at frame F, save its picture, close it */
    std::vector<std::string> menuSets;   /* --menu-set S.K=V: changed in that menu (as a player would) */
};
extern LiveOpts g_opt;
extern live::Config g_cfg;                  /* bc.ini (or the built-in defaults) */
extern std::string g_eepromPath, g_mpkPath; /* the saves the config names */

/* ---- RT64 state (live_rt64.cpp) ---- */
extern SDL_Window *g_window;
extern RT64::Application *g_app;
extern unsigned g_spIntr, g_dpIntr, g_tasks, g_unknownUcode, g_fixedBytes, g_otherUcodeTasks;
extern double g_dlSeconds;                  /* time in processDisplayLists (frame-rate option on) */
extern unsigned g_dlTasks;
uint8_t *rdramBase();
extern bool g_throttleLog;                  /* --throttle-log */

struct Interp {
    uint32_t lastOrigin = 0;
    unsigned lastSwapVi = 0;
    unsigned intervals[3] = {2, 2, 2};   /* VIs between the last frame-buffer swaps */
    unsigned lastFrame = ~0u;
    unsigned long long lastMode = ~0ull;
    int lastLevel = -1000;
    bool haveCam = false;
    double eye[3] = {}, fwd[3] = {}, lastMove = 0;
    unsigned rate = 0;                   /* last rate given to RT64 (0: a cut) */
    unsigned cuts = 0, frames = 0;
    unsigned displayHz = 0;              /* --display-hz (testing): RT64's display rate */
    bool log = false;                    /* --interp-log: every frame's camera motion */
};
extern Interp g_interp;
extern uint32_t g_skipLo, g_skipHi;         /* --dl-skip */
extern bool g_viewerBoot;                   /* --viewer: the level viewer starts at the first retrace */
extern bool g_noMods;                       /* --no-mods: bc.ini's [mods] enabled not used */
extern unsigned g_viewerOpenFrame;          /* --viewer-open F (testing): opened over the game at frame F */
extern unsigned g_hashFrame;                /* --texture-hashes */
extern bool g_presentLegacy;                /* --present-legacy (testing): RT64's timer as before patch 0007 */
extern bool g_ubershadersOnly;              /* --ubershaders-only (testing) */
const char *pacingName();
void prestart(HostOpts *o);

/* ---- screenshots (live_shots.cpp) ---- */
void saveFrame(const HostViRegs &r, unsigned frame);
void saveWindow(const char *name);
void saveWindowTo(const std::string &file);
struct WinSeq {
    unsigned frame = ~0u, ms = 0;
    std::thread thread;
    bool started = false;
};
extern WinSeq g_wseq;
void winSeqThread(unsigned frame, unsigned ms);

/* ---- measuring (live_frametimes.cpp) ---- */
struct PaceRec {
    double t;        /* real time when the retrace's picture went to RT64 (s) */
    double due;      /* its due time on the paced clock (s) */
    double update;   /* time spent in updateScreen (s) */
    unsigned frames; /* game frames so far */
};
extern std::vector<PaceRec> g_pace;
constexpr size_t kMaxPresents = 1 << 20;
extern double *g_presentTimes;              /* RT64's present thread: each picture presented */
extern std::atomic<size_t> g_presents;
extern unsigned g_paceResets;
extern LARGE_INTEGER g_freq, g_t0, g_tStart;
unsigned displayHz();
double secondsSince(const LARGE_INTEGER &t0);
void presentHook(plume::RenderCommandList *, plume::RenderFramebuffer *);
struct FlipRec { unsigned syncRefresh, presentCount, presentRefresh; double t; };
struct PresentedRec {
    unsigned long long presentId;
    unsigned frameIndex, frameCount, syncInterval, dxgiCount;
    double submit, present;
    double content;   /* what the picture shows (contentMs; -1 unknown) */
    float weight;     /* its interpolation weight (port patch 0008) */
};
struct FlipLog {
    const char *path = nullptr;
    std::mutex mutex;
    std::vector<FlipRec> flips;
    std::vector<PresentedRec> presents;
    std::thread thread;
    std::atomic<bool> stop{false};
    IDXGISwapChain *dxgi = nullptr;
};
extern FlipLog g_flip;
void flipStart();
void flipWrite();
void counterApply();
void counterTick(unsigned frames);
void counterCycle();
struct Hist {
    static constexpr int kBins = 5000;   /* 0.05 ms each, up to 250 ms */
    std::vector<uint32_t> bins = std::vector<uint32_t>(kBins + 1);
    uint64_t n = 0, over15 = 0, over2 = 0;
    double sum = 0, sq = 0, max = 0, targetSum = 0;
    void add(double ms, double target) {
        bins[std::max(0, std::min(kBins, int(ms / 0.05)))]++;   /* (a content step can be negative) */
        n++;
        sum += ms;
        sq += ms * ms;
        max = std::max(max, ms);
        targetSum += target;
        over15 += ms > 1.5 * target;
        over2 += ms > 2.0 * target;
    }
    double pct(double p) const {
        if (n == 0) return 0;
        const uint64_t want = uint64_t(std::ceil(p * double(n)));
        uint64_t c = 0;
        for (int i = 0; i <= kBins; i++)
            if ((c += bins[i]) >= std::max<uint64_t>(1, want)) return std::min(max, (i + 0.5) * 0.05);
        return max;
    }
    double mean() const { return n ? sum / double(n) : 0; }
    double sd() const { return n > 1 ? std::sqrt(std::max(0.0, sq / double(n) - mean() * mean())) : 0; }
    void reset() { *this = Hist(); }
};
/* main-thread and GPU times of a span */
struct Busy {
    double game = 0, dl = 0, submit = 0, wait = 0, gpu = 0, gpuMax = 0, presentWait = 0;
    unsigned gpuN = 0, retraces = 0;
    uint32_t rendered = 0, reduced = 0, late = 0, cut = 0, skipped = 0, resyncs = 0;   /* RT64's counters at the start */
    /* the spikes (intervals over 1.5x the target) by cause (spikeAttribute), the orbit camera's work, new
     * shaders (submitted / compiled) and textures, the longest retrace's main-thread work and RT64 workload */
    unsigned causes[live::orbit::SPIKE_CAUSES] = {};
    live::OrbitPerf orbit;
    double orbitMax = 0, retraceMax = 0, renderMax = 0;
    unsigned shaders = 0, shadersReady = 0, textures = 0, missed = 0;   /* (missed: retraces over 1.5 periods) */
};
/* the retraces' records for the spike attribution (main thread) */
struct Slot {
    double t = 0;                        /* the retrace (ms since start) */
    live::orbit::SpikeWindow w;          /* its values (maxima filled per spike) */
};
struct PendingSpike {
    double t, ms, target;                /* present time (ms since start), interval, target */
    double contentStep;                  /* its picture's content step (ms; -1 unknown) */
    float weight;                        /* its picture's interpolation weight */
};
/* the content steps of a span */
struct ContentStats {
    Hist step;                           /* content step per presented picture (ms) */
    unsigned even = 0, repeated = 0, skipped = 0, unknown = 0;
    double judSum = 0, judSq = 0, judMax = 0;   /* present-call interval less the content step */
    unsigned judN = 0;
    unsigned dispN = 0, dispOff = 0;     /* on the display: consecutive pictures; off by over half a period */
    double dispSum = 0, dispSq = 0, dispMax = 0;   /* refresh interval less the content step */
    void add(double s, double interval, double target) {
        step.add(s, target);
        even += std::fabs(s - target) <= 0.1 * target;
        repeated += s < 0.5 * target;
        skipped += s > 1.5 * target;
        const double j = interval - s;
        judSum += j, judSq += j * j, judN++;
        judMax = std::max(judMax, std::fabs(j));
    }
    void addDisplay(double off, double target) {
        dispN++;
        dispSum += off, dispSq += off * off;
        dispMax = std::max(dispMax, std::fabs(off));
        dispOff += std::fabs(off) > 0.5 * target;
    }
    static double sd(double sum, double sq, unsigned n) {
        if (n < 2) return 0;
        const double m = sum / n;
        return std::sqrt(std::max(0.0, sq / n - m * m));
    }
};
struct FrameTimes {
    std::mutex mutex;                    /* present thread -> main thread */
    Hist seg, win;                       /* this settings segment; the last 10 s */
    double last = -1;                    /* the last present (ms since start) */
    unsigned gaps = 0;                   /* intervals over 250 ms (loads, the menu): not in the statistics */
    float ring[240] = {};
    unsigned ringPos = 0, ringN = 0;
    unsigned stutters = 0;               /* over 1.5x the target since the counter was turned on */
    double targetMs = 0;
    unsigned syncCount[5] = {};          /* presents by sync interval 0..4 */
    /* the display's refreshes (DXGI frame statistics): intervals of one refresh */
    UINT lastRefresh = 0;
    LONGLONG lastQpc = 0;
    double refSum = 0, refSq = 0;
    unsigned refN = 0, refLong = 0;      /* refreshes over 1.2x the nominal period: variable refresh */
    UINT shownCount = 0, shownRefresh = 0;   /* the present on screen and the refresh it came at */
    unsigned holds[5] = {};              /* pictures shown 0..4+ refreshes */
    unsigned neverShown = 0;             /* presents replaced before they were shown */
    Busy segBusy, winBusy;
    double segStart = 0, winStart = 0;
    unsigned segment = 0;
    bool suspended = false;              /* the menu is open */
    IDXGISwapChain *dxgi = nullptr;
    uint32_t gpuIdx = 0;
    double viExit = -1;                  /* the main thread left liveVi (s) */
    double dlInTick = 0;                 /* processDisplayLists time since then */
    unsigned hz = 0;                     /* the display's rate (main thread, at each segment) */
    /* spike attribution: spikes the present thread saw (attributed by the main thread once the retraces
     * around them are in), the last retraces */
    std::vector<PendingSpike> spikes;
    Slot slots[64];
    unsigned slotPos = 0, slotN = 0;
    double lastViEnter = -1, lastWait = 0, lastSubmit = 0;
    uint32_t wlIdx = 0;
    size_t shaderSeen = 0, shaderReadySeen = 0, textureSeen = 0;
    FILE *spikeLog = nullptr;            /* --spike-log FILE (testing) */
    /* content times (ContentStats): the last present's; DXGI present counts -> content, the last shown */
    ContentStats segC, winC;
    double lastContent = -1;
    struct Counted {
        UINT count = 0;
        double content = -1;
    } counted[64];
    unsigned countedPos = 0;
    UINT shownPrevCount = 0;
    double shownPrevMs = 0, shownPrevContent = -1;
    /* the segment's settings as they were (frameSettings; the log line names them, not the new ones) */
    std::string settings;
    FILE *pictureLog = nullptr;          /* --picture-log FILE (testing): every present */
};
extern FrameTimes g_ft;
extern double g_viWhenMs;                  /* the last retrace's virtual time (ms; main thread) */
void contentFrame(uint64_t workload);
void framePresented(const RT64::PresentQueue::PresentedInfo &p);
void frameSegment(const char *why);
void busyAdd(double Busy::*f, double ms);
extern bool g_queueFullTask;                /* a frame task since the last retrace found RT64's queue full (main thread) */
struct Tail {
    double ms = 0, scanMs = 0, eventsMs = 0;   /* the host's work after the retrace; its controller rescans, events */
    unsigned scans = 0;
    /* the events' time by kind: SDL's pump (the first SDL_PollEvent: the window's messages, and before the
     * controllers' thread SDL's controller updates and device detection), the handlers of window, keyboard
     * and text, mouse, controller and other events, then reading the pad state (pollInput) */
    double pumpMs = 0, queueMs = 0, windowMs = 0, keyMs = 0, mouseMs = 0, padMs = 0, otherMs = 0, inputMs = 0;
    unsigned events = 0;
    double padThreadMs = 0;   /* the controllers' thread's longest update meanwhile (it doesn't hold the game up) */
};
extern Tail g_tail, g_tailRun;
void frameTick(double viEnterMs, double gameMs, double dlMs, double submitMs, double waitMs);
void frameEnd();
void writePaceLog();
struct PerfWin {
    double t = 0, span = 0;
    unsigned frames = 0, frameEnd = 0, gpuN = 0, wlN = 0, dlTasks = 0, workloads = 0;
    double gpuSum = 0, gpuMax = 0, wlSum = 0, wlMax = 0, dlMs = 0, waitMs = 0, calls = 0, tris = 0;
    live::OrbitPerf orbit;
};
struct PerfLog {
    const char *path = nullptr;
    std::vector<PerfWin> wins;
    PerfWin cur;
    double start = -1;
    uint32_t gpuIdx = 0, wlIdx = 0;
    double dlStart = 0;              /* processDisplayLists time before this window (any frame rate) */
};
extern PerfLog g_perf;
extern double g_perfDlSeconds;             /* time in processDisplayLists (--perf-log) */
void perfWorkload(int cursor);
void perfTick(unsigned frames);
void perfWait(double seconds);
void perfWrite();

}  // namespace bcexe

#endif
