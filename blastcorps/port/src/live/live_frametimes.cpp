/* bc.exe's measurements (live_rt64.h): --pace-log, --flip-log, the frame-rate counter (F2), the frame-time
 * report in bc.log (always on: every 10 s, per settings segment and at exit) with the spikes' causes, the
 * content times (RT64 patch 0008), long retraces and RT64's texture census, and --perf-log.  They only read
 * clocks and counters: the game never sees them. */
#include <windows.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <SDL.h>

#include "hle/rt64_application.h"
#include "hle/rt64_workload_queue.h"
#include "render/rt64_raster_shader_cache.h"
#include "render/rt64_texture_cache.h"
#include "plume_d3d12.h"

#include "live_interp.h"
#include "live_menu.h"
#include "live_orbit.h"
#include "live_rt64.h"
#include "live_setup.h"
#include "orbit_math.h"

extern "C" {
#include "plat_host.h"
unsigned plat_frames(void); /* save.c */
}

namespace bcexe {

/* ---- pacing measurement (--pace-log) ---------------------------------------- */
std::vector<PaceRec> g_pace;
double *g_presentTimes;              /* RT64's present thread: each picture presented */
std::atomic<size_t> g_presents;
unsigned g_paceResets;
LARGE_INTEGER g_freq, g_t0, g_tStart;


double secondsSince(const LARGE_INTEGER &t0) {
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    return double(now.QuadPart - t0.QuadPart) / double(g_freq.QuadPart);
}

void presentHook(plume::RenderCommandList *, plume::RenderFramebuffer *) {
    size_t i = g_presents.fetch_add(1);
    if (i < kMaxPresents) g_presentTimes[i] = secondsSince(g_tStart);
}

/* ---- flip timing (--flip-log, testing) --------------------------------------
 * When the pictures really reached the display, not when they were drawn:
 *  - RT64's present thread reports every swap chain present (port patch 0005,
 *    PresentQueue::presentedHook): its submission time (the retrace hand-over
 *    that queued it), the present call's time, the sync interval it used, and
 *    (D3D12) DXGI's present count;
 *  - a thread polls DXGI's frame statistics (IDXGISwapChain::
 *    GetFrameStatistics) every millisecond: every display refresh (its count and
 *    QPC time) and which present was on screen from which refresh.
 * Both go to FILE at exit (tools: the pace_flip.py analysis); Vulkan gets the
 * present records only. */
FlipLog g_flip;

double steadyMs(const RT64::Timestamp &t) {
    const double ns = double(std::chrono::duration_cast<std::chrono::nanoseconds>(t.time_since_epoch()).count());
    return ns / 1e6 - double(g_tStart.QuadPart) * 1e3 / double(g_freq.QuadPart);
}

/* (called by framePresented, the present thread's hook, with --flip-log) */
void flipPresented(const RT64::PresentQueue::PresentedInfo &p, double content) {
    PresentedRec r = {p.presentId, p.frameIndex, p.frameCount, p.hold, 0, steadyMs(p.submitTime),
                      steadyMs(p.presentTime), content, p.contentWeight};
    UINT n = 0;
    if (g_flip.dxgi != nullptr && SUCCEEDED(g_flip.dxgi->GetLastPresentCount(&n))) r.dxgiCount = n;
    std::lock_guard<std::mutex> lock(g_flip.mutex);
    if (g_flip.presents.size() < kMaxPresents) g_flip.presents.push_back(r);
}

void flipThread() {
    DXGI_FRAME_STATISTICS last = {};
    while (!g_flip.stop) {
        DXGI_FRAME_STATISTICS s = {};
        if (SUCCEEDED(g_flip.dxgi->GetFrameStatistics(&s)) &&
            (s.SyncRefreshCount != last.SyncRefreshCount || s.PresentCount != last.PresentCount)) {
            last = s;
            const double t = double(s.SyncQPCTime.QuadPart - g_tStart.QuadPart) * 1e3 / double(g_freq.QuadPart);
            std::lock_guard<std::mutex> lock(g_flip.mutex);
            if (g_flip.flips.size() < kMaxPresents) g_flip.flips.push_back({s.SyncRefreshCount, s.PresentCount, s.PresentRefreshCount, t});
        }
        Sleep(1);   /* (1 ms timer resolution: every refresh is seen) */
    }
}

void flipStart() {
    if (g_flip.path == nullptr) return;
    if (!g_opt.vulkan) {
        auto *sc = static_cast<plume::D3D12SwapChain *>(g_app->swapChain.get());
        g_flip.dxgi = sc->d3d;
        g_flip.thread = std::thread(flipThread);
    }
}

/* at exit (host_exit_hook), or before the renderer ends (shutdown); once */
void flipWrite() {
    if (g_flip.path == nullptr) return;
    const char *path = g_flip.path;
    g_flip.path = nullptr;
    g_flip.stop = true;
    if (g_flip.thread.joinable()) g_flip.thread.join();
    std::lock_guard<std::mutex> lock(g_flip.mutex);
    FILE *f = static_cast<FILE *>(host_fopen(path, "w"));
    if (f == nullptr) return;
    fprintf(f, "kind,a,b,c,d,e,f,g,h,i\n");
    fprintf(f, "info,%u,%s,%s,%s,%s,,,,\n", displayHz(), g_opt.vsync ? "vsync" : "novsync",
            g_opt.fullscreen ? "fullscreen" : "windowed", g_opt.vulkan ? "vulkan" : "d3d12",
            g_app->presentQueue->vsyncPacing ? "vsyncpacing" : "timerpacing");
    for (const FlipRec &r : g_flip.flips)
        fprintf(f, "flip,%u,%u,%u,%.4f,,,,,\n", r.syncRefresh, r.presentCount, r.presentRefresh, r.t);
    /* present: id, picture, pictures, hold, DXGI count, submitted, presented, content time (ms; -1
     * unknown), weight */
    for (const PresentedRec &r : g_flip.presents)
        fprintf(f, "present,%llu,%u,%u,%u,%u,%.4f,%.4f,%.4f,%.4f\n", r.presentId, r.frameIndex, r.frameCount,
                r.syncInterval, r.dxgiCount, r.submit, r.present, r.content, double(r.weight));
    fclose(f);
    host_log("live: flip log: %u refreshes, %u presents -> %s; vsync pacing: %u pictures dropped to catch up, "
             "%u holds shortened, %u fallbacks to RT64's pacing\n", unsigned(g_flip.flips.size()),
             unsigned(g_flip.presents.size()), path, g_app ? unsigned(g_app->presentQueue->pacingDrops) : 0u,
             g_app ? unsigned(g_app->presentQueue->pacingTrims) : 0u,
             g_app ? unsigned(g_app->presentQueue->pacingFallbacks) : 0u);
}

/* ---- the frame-rate counter (video.fps_counter, F2) --------------------------
 * Presented pictures are counted on RT64's present thread (drawHook), the
 * game's frames from the platform's frame count; twice a second the numbers
 * go to the overlay (live_menu.cpp), which draws them in a corner.  Off,
 * nothing of this runs.  It only reads clocks: the game never sees it. */
struct Counter {
    std::atomic<bool> on{false};
    std::mutex mutex;                 /* the present times (present thread -> main thread) */
    std::vector<double> times;        /* present times (s) since the last update */
    std::vector<float> graph;         /* the last present intervals (ms) */
    double lastPresent = 0, lastUpdate = -1;
    unsigned lastFrames = 0;
    live::CounterStats stats;
} g_counter;

/* (the present thread: framePresented, at every swap-chain present) */
void counterPresentAt(double t) {
    std::lock_guard<std::mutex> lock(g_counter.mutex);
    g_counter.times.push_back(t);
}
void counterGraph(live::CounterStats &s);
void counterStutterReset();

void counterApply() {
    const bool on = g_cfg.fpsCounter != 0;
    if (on != g_counter.on.load()) {
        g_counter.on = on;
        std::lock_guard<std::mutex> lock(g_counter.mutex);
        g_counter.times.clear();
        g_counter.graph.clear();
        g_counter.lastUpdate = -1;
        counterStutterReset();
    }
    g_counter.stats.mode = g_cfg.fpsCounter;
    g_counter.stats.corner = g_cfg.fpsCorner;
    live::counterSet(g_counter.stats);
}

/* each retrace (main thread) */
void counterTick(unsigned frames) {
    if (!g_counter.on) {
        if (live::overlayWanted()) live::counterFrame();   /* the overlay's marker / note (the debug cheats) */
        return;
    }
    const double now = secondsSince(g_tStart);
    if (g_counter.lastUpdate < 0) {
        g_counter.lastUpdate = now;
        g_counter.lastFrames = frames;
    } else if (now - g_counter.lastUpdate >= 0.5) {
        std::vector<double> t;
        {
            std::lock_guard<std::mutex> lock(g_counter.mutex);
            t.swap(g_counter.times);
        }
        const double dt = now - g_counter.lastUpdate;
        live::CounterStats &s = g_counter.stats;
        s.shown = double(t.size()) / dt;
        s.game = double(frames - g_counter.lastFrames) / dt;
        double sum = 0, mx = 0;
        unsigned n = 0;
        double prev = g_counter.lastPresent;
        for (double x : t) {
            if (prev > 0) {
                const double ms = (x - prev) * 1e3;
                sum += ms;
                mx = std::max(mx, ms);
                n++;
            }
            prev = x;
        }
        if (!t.empty()) g_counter.lastPresent = t.back();
        s.frameMs = n != 0 ? sum / n : 0;
        s.frameMsMax = mx;
        counterGraph(s);   /* the last 240 present intervals, the stutters (frame times) */
        s.note = g_cfg.framerate == 0 ? "original frame rate"
                 : live::interpTagsOn() ? "interpolated, tagged" : "interpolated, auto";
        g_counter.lastUpdate = now;
        g_counter.lastFrames = frames;
        live::counterSet(s);
    }
    live::counterFrame();
}

/* the counter key / controller buttons: off -> on -> detailed -> off (for
 * this run; the menu saves it) */
void counterCycle() {
    g_cfg.fpsCounter = (g_cfg.fpsCounter + 1) % 3;
    host_log("live: frame-rate counter %s\n", live::fpsCounterName(g_cfg.fpsCounter).c_str());
    counterApply();
}

unsigned displayHz() {
    HMONITOR mon = MonitorFromWindow(HWND(host_gui_window), MONITOR_DEFAULTTONEAREST);
    MONITORINFOEXW mi = {};
    mi.cbSize = sizeof mi;
    DEVMODEW dm = {};
    dm.dmSize = sizeof dm;
    if (!GetMonitorInfoW(mon, &mi) || !EnumDisplaySettingsW(mi.szDevice, ENUM_CURRENT_SETTINGS, &dm)) return 0;
    return dm.dmDisplayFrequency;
}

/* ---- frame times (always measured; bc.log every 10 s and at exit) -----------
 * Every swap-chain present (RT64's present thread, port patch 0005/0007's
 * presentedHook): the interval from the present before, against the target
 * (one picture at RT64's target rate when it draws in-between pictures,
 * else the game's own rate), into a histogram (0.05 ms bins: percentiles
 * without keeping the intervals) and a ring of the last 240 for the F2
 * counter's graph; with D3D12, DXGI's frame statistics show whether the
 * display's refreshes followed the presents (a variable refresh rate).  The
 * main thread's time per retrace is split into the game (and the host's
 * per-frame work), reading the display lists (processDisplayLists), RT64's
 * updateScreen and the pacing wait; the GPU's time per rendered picture comes
 * from RT64's timestamp queries.  A block of lines in the log whenever a
 * video setting changes and at exit, one line every 10 s.  Costs a few clock
 * reads per picture.  It only reads clocks: the game never sees it. */

/* ---- what each picture shows (its content time; port patch 0008) -------------
 * RT64 reports with every present the workload the picture was rendered from
 * and its interpolation weight.  The game frames' times are their retraces'
 * virtual times (liveVi's `when`: the retrace each frame's display list came
 * after); a picture shows the frame before's time plus its weight of the way to
 * its own frame's.  At an even rate each picture's content advances by one
 * period of the target; on a fixed-refresh display (DXGI frame statistics) each
 * refresh then shows one period's more.  The frame-time report counts the
 * steps (even, repeated, skipped), their spread against the present calls'
 * intervals and against the display's refreshes. */
struct ContentMap {
    std::mutex mutex;
    struct Rec {
        uint64_t workload = 0;
        double ms = 0;
    } recs[64];
    unsigned pos = 0;
} g_content;
double g_viWhenMs = -1;                  /* the last retrace's virtual time (ms; main thread) */

/* (main thread) a frame's workload was submitted */
void contentFrame(uint64_t workload) {
    if (g_viWhenMs < 0) return;
    std::lock_guard<std::mutex> lock(g_content.mutex);
    if (g_content.pos > 0 && g_content.recs[(g_content.pos - 1) % 64].workload == workload) return;
    g_content.recs[g_content.pos % 64] = {workload, g_viWhenMs};
    g_content.pos++;
}

/* (present thread) the content time (virtual ms) of a picture; -1 if unknown */
double contentMs(uint64_t workload, float weight) {
    if (weight < 0.0f || workload == 0) return -1;
    std::lock_guard<std::mutex> lock(g_content.mutex);
    const unsigned n = std::min(64u, g_content.pos);
    for (unsigned k = 1; k <= n; k++) {
        const ContentMap::Rec &r = g_content.recs[(g_content.pos - k) % 64];
        if (r.workload != workload) continue;
        if (k == n) return weight >= 1.0f ? r.ms : -1.0;   /* (no frame before it known) */
        const ContentMap::Rec &prev = g_content.recs[(g_content.pos - k - 1) % 64];
        return prev.ms + double(weight) * (r.ms - prev.ms);
    }
    return -1;
}

FrameTimes g_ft;

void counterPresentAt(double t);
void flipPresented(const RT64::PresentQueue::PresentedInfo &p, double content);

void framePresented(const RT64::PresentQueue::PresentedInfo &p) {
    const double content = contentMs(p.contentWorkload, p.contentWeight);
    if (g_flip.path != nullptr) flipPresented(p, content);
    const double t = steadyMs(p.presentTime);
    const double target = p.targetRate > 0 && p.targetRate > p.viOriginalRate ? 1000.0 / p.targetRate
                          : p.viOriginalRate > 0 ? 1000.0 / p.viOriginalRate : 1000.0 / 30.0;
    if (g_counter.on) counterPresentAt(t / 1e3);
    std::lock_guard<std::mutex> lock(g_ft.mutex);
    g_ft.syncCount[std::min(4u, p.syncInterval)]++;
    if (p.readyTime != RT64::Timestamp()) {
        const double w = steadyMs(p.presentTime) - steadyMs(p.readyTime);
        if (w > 0 && w < 250) g_ft.segBusy.presentWait += w, g_ft.winBusy.presentWait += w;
    }
    double contentStep = -1;
    if (g_ft.last >= 0 && !g_ft.suspended) {
        const double ms = t - g_ft.last;
        if (ms > 250.0) {
            g_ft.gaps++;
        } else {
            g_ft.seg.add(ms, target);
            g_ft.win.add(ms, target);
            g_ft.ring[g_ft.ringPos] = float(ms);
            g_ft.ringPos = (g_ft.ringPos + 1) % 240;
            g_ft.ringN = std::min(240u, g_ft.ringN + 1);
            if (content >= 0 && g_ft.lastContent >= 0) {
                contentStep = content - g_ft.lastContent;
                g_ft.segC.add(contentStep, ms, target);
                g_ft.winC.add(contentStep, ms, target);
            } else {
                g_ft.segC.unknown++;
                g_ft.winC.unknown++;
            }
            if (ms > 1.5 * target) {
                g_ft.stutters++;
                if (g_ft.spikes.size() < 4096) g_ft.spikes.push_back({t, ms, target, contentStep, p.contentWeight});
            }
        }
    }
    if (g_ft.pictureLog != nullptr)
        fprintf(g_ft.pictureLog, "%.3f,%.3f,%.3f,%.3f,%.4f,%llu,%u,%u,%u,%u,%.3f\n", t, g_ft.last >= 0 ? t - g_ft.last : -1.0,
                target, content, double(p.contentWeight), (unsigned long long) p.contentWorkload, p.frameIndex,
                p.frameCount, p.syncInterval, g_ft.suspended ? 1u : 0u,
                p.readyTime != RT64::Timestamp() ? steadyMs(p.readyTime) : -1.0);
    g_ft.last = t;
    g_ft.lastContent = content;
    g_ft.targetMs = target;
    if (g_ft.dxgi != nullptr) {
        /* this present's DXGI count -> its content (what the display showed, below) */
        UINT count = 0;
        if (SUCCEEDED(g_ft.dxgi->GetLastPresentCount(&count))) {
            g_ft.counted[g_ft.countedPos % 64] = {count, content};
            g_ft.countedPos++;
        }
        DXGI_FRAME_STATISTICS s = {};
        if (SUCCEEDED(g_ft.dxgi->GetFrameStatistics(&s))) {
            /* the newest picture on screen came at this refresh: its content against the picture
             * before's (consecutive presents, both seen at the refresh they came on) */
            if (s.PresentCount != g_ft.shownPrevCount && s.PresentRefreshCount == s.SyncRefreshCount &&
                s.SyncRefreshCount != 0) {
                double shownContent = -1;
                for (unsigned k = 1; k <= std::min(64u, g_ft.countedPos); k++) {
                    const FrameTimes::Counted &c = g_ft.counted[(g_ft.countedPos - k) % 64];
                    if (c.count == s.PresentCount) {
                        shownContent = c.content;
                        break;
                    }
                }
                const double shownMs = double(s.SyncQPCTime.QuadPart - g_tStart.QuadPart) * 1e3 / double(g_freq.QuadPart);
                if (!g_ft.suspended && s.PresentCount == g_ft.shownPrevCount + 1 && shownContent >= 0 &&
                    g_ft.shownPrevContent >= 0 && shownMs - g_ft.shownPrevMs < 250.0) {
                    const double off = (shownMs - g_ft.shownPrevMs) - (shownContent - g_ft.shownPrevContent);
                    g_ft.segC.addDisplay(off, target);
                    g_ft.winC.addDisplay(off, target);
                }
                g_ft.shownPrevCount = s.PresentCount;
                g_ft.shownPrevMs = shownMs;
                g_ft.shownPrevContent = shownContent;
            }
            if (g_ft.lastRefresh != 0 && s.SyncRefreshCount == g_ft.lastRefresh + 1) {
                const double ms = double(s.SyncQPCTime.QuadPart - g_ft.lastQpc) * 1e3 / double(g_freq.QuadPart);
                const unsigned hz = g_ft.hz;
                if (ms > 0 && ms < 100) {
                    g_ft.refSum += ms;
                    g_ft.refSq += ms * ms;
                    g_ft.refN++;
                    if (hz != 0 && ms > 1.2 * 1000.0 / hz) g_ft.refLong++;
                }
            }
            g_ft.lastRefresh = s.SyncRefreshCount;
            g_ft.lastQpc = s.SyncQPCTime.QuadPart;
            /* what the display showed: a newer present on screen since the last
             * look -> the one before stayed (its refresh to this one's) refreshes;
             * presents in between were never shown (sampled once a present:
             * approximate when two came on screen between two presents) */
            if (s.PresentCount > g_ft.shownCount && s.PresentRefreshCount > 0) {
                if (g_ft.shownCount != 0 && !g_ft.suspended && s.PresentRefreshCount > g_ft.shownRefresh) {
                    const unsigned hold = s.PresentRefreshCount - g_ft.shownRefresh;
                    g_ft.holds[std::min(4u, hold)]++;
                    g_ft.neverShown += s.PresentCount - g_ft.shownCount - 1;
                }
                g_ft.shownCount = s.PresentCount;
                g_ft.shownRefresh = s.PresentRefreshCount;
            }
        }
    }
}

/* RT64's counters now (patch 0007) */
void rt64Counters(Busy &b) {
    const RT64::WorkloadQueue &w = *g_app->workloadQueue;
    const RT64::PresentQueue &q = *g_app->presentQueue;
    b.rendered = w.picturesRendered;
    b.reduced = w.framesReduced;
    b.late = w.framesSkippedLate;
    b.cut = w.framesCut;
    b.skipped = q.presentsSkipped;
    b.resyncs = q.scheduleResyncs;
}


/* the spikes' causes and the orbit camera's work of a span (frameLog) */
void causeLog(const char *head, const Busy &b, double seconds) {
    using namespace live::orbit;
    const unsigned *c = b.causes;
    unsigned n = 0;
    for (int i = 0; i < SPIKE_CAUSES; i++) n += c[i];
    host_log("live: frame times %s: spikes by cause %u: main thread (game %u, display lists %u, orbit camera %u, "
             "RT64 submit %u, other %u), GPU %u, RT64 render thread %u, new shaders %u, new textures %u, present/other %u; "
             "retraces missed %u (longest %.1f ms), longest RT64 workload %.1f ms; RT64 given %u new shaders "
             "(%u compiled), %u new textures\n",
             head, n, c[SPIKE_MAIN_GAME], c[SPIKE_MAIN_LISTS], c[SPIKE_MAIN_ORBIT], c[SPIKE_MAIN_SUBMIT],
             c[SPIKE_MAIN_OTHER], c[SPIKE_GPU],
             c[SPIKE_RENDER], c[SPIKE_SHADERS], c[SPIKE_TEXTURES], c[SPIKE_PRESENT], b.missed, b.retraceMax,
             b.renderMax, b.shaders, b.shadersReady, b.textures);
    const live::OrbitPerf &o = b.orbit;
    if (o.views == 0 && o.lists == 0 && o.stepMs <= 0) return;
    (void) seconds;
    const double r = double(std::max(1u, b.retraces));
    host_log("live: frame times %s: orbit camera ms per retrace: step %.3f (collision queries %.0f), drawing lists "
             "%.3f (%u made, %u with every cell, %.1f cells, the game's %.1f), frame setup %.3f; longest retrace's "
             "%.2f; view hook %.4f ms per picture (%u pictures); grids made again: obstacles %u, ground cells %u; "
             "pulled in close to the player %u of %u steps\n",
             head, o.stepMs / r, o.queries / r, o.cellsMs / r, o.lists, o.listsAll,
             o.lists ? double(o.cells) / o.lists : 0.0, o.lists ? double(o.gameCells) / o.lists : 0.0, o.taskMs / r,
             b.orbitMax, o.views ? o.viewMs / o.views : 0.0, o.views, o.obstacleBuilds, o.groundBuilds, o.closeSteps,
             o.steps);
}

/* what the pictures showed (ContentStats) of a span */
void contentLog(const char *head, const ContentStats &c) {
    const Hist &s = c.step;
    if (s.n == 0 && c.unknown == 0) return;
    char disp[200] = "no frame statistics";
    if (c.dispN > 0)
        snprintf(disp, sizeof disp, "%u pairs, refresh less content step sd %.3f ms max %.2f, %u off by over half a "
                 "period", c.dispN, ContentStats::sd(c.dispSum, c.dispSq, c.dispN), c.dispMax, c.dispOff);
    host_log("live: frame times %s: content (the time each picture shows, port patch 0008): steps ms mean %.3f sd "
             "%.3f min %.2f max %.2f; even %u, repeated %u, skipped %u, unknown %u; present-call interval less "
             "content step sd %.3f max %.2f; on the display: %s\n",
             head, s.mean(), s.sd(), s.n ? s.pct(0.0) : 0.0, s.max, c.even, c.repeated, c.skipped, c.unknown,
             ContentStats::sd(c.judSum, c.judSq, c.judN), c.judMax, disp);
}

/* the log lines of a span: `head` names it */
void frameLog(const char *head, const Hist &h, const Busy &b, double seconds, bool full, const ContentStats &cs) {
    Busy now;
    rt64Counters(now);
    const double pics = double(std::max<uint64_t>(1, h.n + 1));
    if (!full) {
        host_log("live: frame times %s: %llu pictures in %.1f s, interval ms mean %.2f sd %.2f p99 %.2f max %.1f, "
                 "over 1.5x %llu, over 2x %llu; CPU ms per picture game %.2f lists %.2f submit %.2f wait %.2f; "
                 "GPU %.2f ms\n",
                 head, (unsigned long long) h.n, seconds, h.mean(), h.sd(), h.pct(0.99), h.max,
                 (unsigned long long) h.over15, (unsigned long long) h.over2, b.game / pics, b.dl / pics,
                 b.submit / pics, b.wait / pics, b.gpuN ? b.gpu / b.gpuN : 0.0);
        causeLog(head, b, seconds);
        contentLog(head, cs);
        return;
    }
    const double target = h.n ? h.targetSum / double(h.n) : 0;
    host_log("live: frame times (%s): %llu presents in %.1f s (%.1f/s), target %.2f ms; present-to-present ms: "
             "mean %.3f sd %.3f p50 %.2f p95 %.2f p99 %.2f max %.2f; over 1.5x the target %llu, over 2x %llu; "
             "%u gaps over 250 ms left out\n",
             head, (unsigned long long) h.n, seconds, seconds > 0 ? double(h.n) / seconds : 0.0, target, h.mean(),
             h.sd(), h.pct(0.5), h.pct(0.95), h.pct(0.99), h.max, (unsigned long long) h.over15,
             (unsigned long long) h.over2, g_ft.gaps);
    host_log("live: frame times (%s): CPU ms per presented picture (main thread): game %.3f, display lists %.3f, "
             "RT64 submit %.3f, pacing wait %.3f; present thread waiting %.3f; GPU ms per rendered picture %.3f "
             "(max %.2f, %u pictures)\n",
             head, b.game / pics, b.dl / pics, b.submit / pics, b.wait / pics, b.presentWait / pics,
             b.gpuN ? b.gpu / b.gpuN : 0.0, b.gpuMax, b.gpuN);
    char vrr[160] = "no frame statistics";
    if (g_ft.refN > 0) {
        const double m = g_ft.refSum / g_ft.refN, sd = std::sqrt(std::max(0.0, g_ft.refSq / g_ft.refN - m * m));
        snprintf(vrr, sizeof vrr, "refresh ms mean %.3f sd %.3f, %u of %u refreshes over 1.2x the nominal: %s", m, sd,
                 g_ft.refLong, g_ft.refN,
                 g_ft.refLong * 20 > g_ft.refN ? "the refresh followed the presents (variable refresh seen)"
                                               : "fixed refresh");
    }
    /* (the segment's own settings: frameSettings, kept up to date while it ran; not the new ones that end it) */
    host_log("live: frame times (%s): %s; %s; sync intervals 0:%u 1:%u 2+:%u\n", head, g_ft.settings.c_str(), vrr,
             g_ft.syncCount[0], g_ft.syncCount[1], g_ft.syncCount[2] + g_ft.syncCount[3] + g_ft.syncCount[4]);
    if (g_ft.refN > 0)
        host_log("live: frame times (%s): on the display (DXGI frame statistics): pictures shown 1 refresh %u, 2 %u, "
                 "3 %u, 4+ %u; %u presents replaced before they were shown\n",
                 head, g_ft.holds[1], g_ft.holds[2], g_ft.holds[3], g_ft.holds[4], g_ft.neverShown);
    host_log("live: frame times (%s): RT64 rendered %u pictures; in-between pictures not drawn: %u reduced (present "
             "thread late), %u skipped (rendering late), %u cut (next frame waiting); %u presents skipped; even "
             "timer restarted %u times (a picture over a period late)\n",
             head, now.rendered - b.rendered, now.reduced - b.reduced, now.late - b.late, now.cut - b.cut,
             now.skipped - b.skipped, now.resyncs - b.resyncs);
    char h2[200];
    snprintf(h2, sizeof h2, "(%s)", head);
    causeLog(h2, b, seconds);
    contentLog(h2, cs);
}

/* the settings the frame times depend on, as they are now (main thread): at a segment's start and every
 * half second while it runs (g_ft.settings), so the segment's log block names its own settings and not
 * the new ones the change that ends it applied a moment before */
std::string frameSettings() {
    int pw = 0, ph = 0;
    if (g_window != nullptr) SDL_GetWindowSizeInPixels(g_window, &pw, &ph);
    float sx, sy;
    {
        std::scoped_lock<std::mutex> lock(g_app->sharedQueueResources->configurationMutex);
        sx = g_app->sharedQueueResources->resolutionScale.x;
        sy = g_app->sharedQueueResources->resolutionScale.y;
    }
    const bool tearing = !g_opt.vulkan && static_cast<plume::D3D12Interface *>(g_app->renderInterface.get())->allowTearing;
    char buf[400];
    snprintf(buf, sizeof buf, "display %u Hz, RT64 target %u/s (game %u/s), pacing %s, vsync %s, %s, tearing %s, "
             "window %dx%d, drawn at %.0fx%.0f, MSAA %ux",
             g_ft.hz, unsigned(g_app->sharedQueueResources->targetRate),
             unsigned(g_app->sharedQueueResources->viOriginalRate), pacingName(), g_opt.vsync ? "on" : "off",
             g_opt.fullscreen ? "fullscreen" : "windowed", tearing ? "supported" : "not supported", pw, ph,
             320.0f * sx, 240.0f * sy, g_app->userConfig.msaaSampleCount());
    return buf;
}

/* a new settings segment (start-up, a video setting changed): the last one's block in the log */
void frameSegment(const char *why) {
    if (g_app == nullptr) return;
    const double now = secondsSince(g_tStart);
    const unsigned hz = displayHz();
    std::lock_guard<std::mutex> lock(g_ft.mutex);
    if (g_ft.segment > 0 && g_ft.seg.n > 0) {
        char head[160];
        snprintf(head, sizeof head, "segment %u, until %s", g_ft.segment, why);
        frameLog(head, g_ft.seg, g_ft.segBusy, now - g_ft.segStart, true, g_ft.segC);
    }
    g_ft.segment++;
    g_ft.hz = hz;
    g_ft.settings = frameSettings();
    g_ft.segC = ContentStats();
    g_ft.seg.reset();
    g_ft.segBusy = Busy();
    rt64Counters(g_ft.segBusy);
    g_ft.segStart = now;
    g_ft.gaps = 0;
    g_ft.refSum = g_ft.refSq = 0;
    g_ft.refN = g_ft.refLong = 0;
    memset(g_ft.syncCount, 0, sizeof g_ft.syncCount);
    memset(g_ft.holds, 0, sizeof g_ft.holds);
    g_ft.neverShown = 0;
}

void busyAdd(double Busy::*f, double ms) {
    g_ft.segBusy.*f += ms;
    g_ft.winBusy.*f += ms;
}

/* ---- spike attribution ------------------------------------------------------
 * Each retrace (frameTick) keeps a record: the main thread's loop since the
 * retrace before (less its pacing wait) split into the game, reading the
 * display lists, the orbit camera's host work and RT64's updateScreen; the
 * longest picture on the GPU and the longest RT64 workload (render thread)
 * that finished since; the shaders RT64 was given to compile (new materials:
 * drawn with its ubershader until compiled) and the textures it added.  A
 * spike (a present interval over 1.5x the target) is put down, once the
 * retraces around it are in, to the first cause that applies over the
 * retraces of its interval (orbit::spikeCause): a missed retrace (by its
 * largest part), the GPU, the render thread, new shaders, new textures, else
 * the present side.  Counts per 10 s and per segment; --spike-log FILE has
 * every spike (testing).  Clock and counter reads only. */
void addOrbit(live::OrbitPerf &a, const live::OrbitPerf &d) {
    a.stepMs += d.stepMs, a.cellsMs += d.cellsMs, a.readMs += d.readMs, a.taskMs += d.taskMs, a.viewMs += d.viewMs;
    a.lists += d.lists, a.cells += d.cells, a.gameCells += d.gameCells, a.queries += d.queries;
    a.listsAll += d.listsAll, a.obstacleBuilds += d.obstacleBuilds, a.groundBuilds += d.groundBuilds;
    a.views += d.views;
    a.steps += d.steps, a.closeSteps += d.closeSteps;
}

/* (g_ft.mutex held) the spikes whose retraces are in */
void spikeAttribute(double nowMs) {
    size_t keep = 0;
    const uint32_t gameRate = g_app->sharedQueueResources->viOriginalRate;
    for (size_t k = 0; k < g_ft.spikes.size(); k++) {
        const PendingSpike sp = g_ft.spikes[k];
        if (sp.t > nowMs - 60.0) {
            g_ft.spikes[keep++] = sp;
            continue;
        }
        live::orbit::SpikeWindow w;
        w.targetMs = sp.target;
        w.frameMs = gameRate > 0 ? 1000.0 / gameRate : 1000.0 / 30.0;
        const double from = sp.t - sp.ms - w.periodMs, to = sp.t + 2.0;
        double longest = -1;
        for (unsigned i = 0; i < g_ft.slotN; i++) {
            const Slot &s = g_ft.slots[i];
            if (s.t < from || s.t > to) continue;
            if (s.w.retraceMs > longest) {   /* the main thread's split: its longest retrace's */
                longest = s.w.retraceMs;
                w.retraceMs = s.w.retraceMs, w.gameMs = s.w.gameMs, w.listsMs = s.w.listsMs;
                w.orbitMs = s.w.orbitMs, w.submitMs = s.w.submitMs, w.queueFull = s.w.queueFull;
                w.otherMs = s.w.otherMs;
            }
            w.gpuMs = std::max(w.gpuMs, s.w.gpuMs);
            w.renderMs = std::max(w.renderMs, s.w.renderMs);
            w.shaders += s.w.shaders;
            w.textures += s.w.textures;
        }
        const int c = live::orbit::spikeCause(w);
        g_ft.segBusy.causes[c]++;
        g_ft.winBusy.causes[c]++;
        if (g_ft.spikeLog != nullptr)
            fprintf(g_ft.spikeLog, "%.3f,%.3f,%.3f,%d,%s,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%u,%u,%d,%u,%u,%d,%.3f,%.4f\n",
                    sp.t, sp.ms, sp.target, c, live::orbit::spikeCauseName(c), w.retraceMs, w.gameMs, w.listsMs,
                    w.orbitMs, w.submitMs, w.gpuMs, w.renderMs, w.frameMs, w.shaders, w.textures,
                    live::orbitActive() ? 1 : 0, g_ft.segment, plat_frames(), w.queueFull ? 1 : 0, sp.contentStep,
                    double(sp.weight));
    }
    g_ft.spikes.resize(keep);
}

/* RT64's shader and texture counts now: shaders submitted (new materials) and compiled, textures */
void rt64Assets(size_t &shaders, size_t &ready, size_t &textures) {
    RT64::RasterShaderCache *sc = g_app->rasterShaderCache.get();
    shaders = ready = textures = 0;
    if (sc != nullptr) {
        {
            std::lock_guard<std::mutex> lock(sc->submissionMutex);
            shaders = sc->shaderHashes.size();
        }
        ready = sc->shaderCount();
    }
    RT64::TextureCache *tc = g_app->textureCache.get();
    if (tc != nullptr) {
        std::lock_guard<std::mutex> lock(tc->textureMapMutex);
        textures = tc->textureMap.hashMap.size();
    }
}

/* each retrace (main thread), after updateScreen: the GPU's times, the spikes, the 10 s line.
 * viEnterMs: this retrace's start (ms since start); gameMs: the main thread since the last retrace
 * less reading the display lists (dlMs); submitMs: this retrace's updateScreen; waitMs: its pacing wait */
bool g_queueFullTask;                /* a frame task since the last retrace found RT64's queue full (main thread) */

/* A long retrace (the main thread's loop over kLongStepMs between two retraces, less its pacing wait):
 * where the time went.  The game's step: the host's work inside it (plat_host.h host_prof: PI DMAs with the
 * load-time byte swaps, audio tasks, gfx tasks with RT64 reading their display lists, save files written)
 * and the rest, the game's own code (with its inflate); then RT64's updateScreen, and the host's work after
 * the retrace (g_tail: events and controllers, with the once-a-second controller rescan, the orbit camera's
 * step, screenshots).  Logged (the first 40, then every 20th). */
constexpr double kLongStepMs = 50.0;
Tail g_tail, g_tailRun;
void longStepLog(double retraceMs, double gameMs, double dlMs, double submitMs, double orbitStepMs) {
    static HostProf last;
    static unsigned n;
    const HostProf now = host_prof;
    const HostProf d = {now.dma_ms - last.dma_ms, now.audio_ms - last.audio_ms, now.gfx_ms - last.gfx_ms,
                        now.save_ms - last.save_ms, now.dma_n - last.dma_n, now.audio_n - last.audio_n,
                        now.gfx_n - last.gfx_n, now.save_n - last.save_n, now.dma_bytes - last.dma_bytes};
    last = now;
    if (retraceMs < kLongStepMs || g_ft.suspended) return;
    if (n++ >= 40 && n % 20 != 0) return;
    const double step = gameMs + dlMs;
    host_log("live: long retrace %.0f ms at frame %u (level %d, game mode %llX, vehicle %u): the game's step %.1f ms "
             "(PI DMAs %u, %llu KB, %.1f ms; audio tasks %u %.1f ms; gfx tasks %u %.1f ms, RT64 reading display lists "
             "%.1f; save files written %u %.1f ms; the game's own code with its inflate %.1f ms), RT64 submit %.1f "
             "ms, after the retrace %.1f ms (events %.1f: SDL's pump %.1f, queue %.1f, %u events: window %.1f, keys "
             "%.1f, mouse %.1f, controllers %.1f, other %.1f; pad state %.1f; controller rescans %u %.1f; orbit step "
             "%.1f; the controllers' own thread's longest update %.1f ms) (#%u)\n",
             retraceMs, plat_frames(), *(volatile int *) uintptr_t(0x802E8BDCu) /* levelno */,
             *(volatile unsigned long long *) uintptr_t(0x80364A90u) /* gGameMode */,
             unsigned(*(volatile uint8_t *) uintptr_t(0x80364456u)) /* gCurVehicle */, step, d.dma_n,
             (unsigned long long) (d.dma_bytes / 1024), d.dma_ms, d.audio_n, d.audio_ms, d.gfx_n, d.gfx_ms, dlMs,
             d.save_n, d.save_ms, std::max(0.0, step - d.dma_ms - d.audio_ms - d.gfx_ms - d.save_ms), submitMs,
             g_tail.ms, g_tail.eventsMs, g_tail.pumpMs, g_tail.queueMs, g_tail.events, g_tail.windowMs, g_tail.keyMs,
             g_tail.mouseMs, g_tail.padMs, g_tail.otherMs, g_tail.inputMs, g_tail.scans, g_tail.scanMs, orbitStepMs,
             g_tail.padThreadMs, n);
}

/* The textures RT64 made (its texture map's creation frames), by size: what the steady "new textures" of
 * the 10 s lines are.  Sampled every other retrace (RT64 evicts a texture unused for a few frames: the
 * hash map's growth counts a texture each time it comes back), logged with the 10 s lines. */
struct TexCensus {
    uint64_t mark = 0;                                  /* the newest creation frame seen */
    std::vector<std::pair<unsigned, unsigned>> sizes;   /* (w << 16 | h, count) this window */
    unsigned made = 0, live = 0;
} g_tex;
void textureCensus(bool log) {
    RT64::TextureCache *tc = g_app->textureCache.get();
    if (tc == nullptr) return;
    uint64_t mark = g_tex.mark;
    {
        std::lock_guard<std::mutex> lock(tc->textureMapMutex);
        const RT64::TextureMap &m = tc->textureMap;
        g_tex.live = 0;
        for (size_t i = 0; i < m.textures.size() && i < m.creationFrames.size(); i++) {
            if (m.textures[i] == nullptr) continue;
            g_tex.live++;
            if (m.creationFrames[i] <= g_tex.mark) continue;
            mark = std::max(mark, m.creationFrames[i]);
            g_tex.made++;
            const unsigned key = unsigned(m.textures[i]->width) << 16 | (unsigned(m.textures[i]->height) & 0xFFFF);
            auto it = std::find_if(g_tex.sizes.begin(), g_tex.sizes.end(), [&](const auto &s) { return s.first == key; });
            if (it == g_tex.sizes.end()) g_tex.sizes.push_back({key, 1});
            else it->second++;
        }
    }
    g_tex.mark = mark;
    if (!log) return;
    if (g_tex.made > 0) {
        auto &sz = g_tex.sizes;
        std::sort(sz.begin(), sz.end(), [](const auto &a, const auto &b) { return a.second > b.second; });
        std::string s;
        for (size_t k = 0; k < sz.size() && k < 6; k++) {
            char b[48];
            snprintf(b, sizeof b, "%s%ux%u: %u", k ? ", " : "", sz[k].first >> 16, sz[k].first & 0xFFFF, sz[k].second);
            s += b;
        }
        host_log("live: frame times (10 s): RT64 made %u textures (by size %s%s); %u in its cache now\n", g_tex.made,
                 s.c_str(), sz.size() > 6 ? ", ..." : "", g_tex.live);
    }
    g_tex.made = 0;
    g_tex.sizes.clear();
}

void frameTick(double viEnterMs, double gameMs, double dlMs, double submitMs, double waitMs) {
    {
        static unsigned texTick;
        const bool winEnd = g_ft.winStart > 0 && secondsSince(g_tStart) - g_ft.winStart >= 10.0;
        if (winEnd || ++texTick % 2 == 0) textureCensus(winEnd);
    }
    const live::OrbitPerf orb = live::orbitFrameTake();
    const bool queueFull = g_queueFullTask;
    g_queueFullTask = false;
    size_t shaders, ready, textures;
    rt64Assets(shaders, ready, textures);
    static unsigned settingsTick;
    std::string settings;
    if (++settingsTick % 30 == 0) settings = frameSettings();   /* (the segment's settings, frameSettings) */
    std::lock_guard<std::mutex> lock(g_ft.mutex);
    if (!settings.empty() && g_ft.segment > 0) g_ft.settings.swap(settings);
    busyAdd(&Busy::game, gameMs);
    busyAdd(&Busy::submit, submitMs);
    busyAdd(&Busy::wait, waitMs);
    Slot slot;
    slot.t = viEnterMs;
    const RT64::ProfilingTimer &p = g_app->workloadQueue->rendererGPUProfiler;
    const uint32_t idx = p.index();
    for (uint32_t i = g_ft.gpuIdx; i != idx; i = uint32_t((i + 1) % p.size())) {
        const double ms = p.data()[i];
        slot.w.gpuMs = std::max(slot.w.gpuMs, ms);
        for (Busy *b : {&g_ft.segBusy, &g_ft.winBusy}) b->gpu += ms, b->gpuMax = std::max(b->gpuMax, ms), b->gpuN++;
    }
    g_ft.gpuIdx = idx;
    /* RT64's workloads (render thread) finished since (its profiler's history, read without its lock) */
    const RT64::ProfilingTimer &wp = g_app->workloadQueue->workloadProfiler;
    const uint32_t widx = wp.index();
    for (uint32_t i = g_ft.wlIdx; i != widx; i = uint32_t((i + 1) % wp.size()))
        slot.w.renderMs = std::max(slot.w.renderMs, wp.data()[i]);
    g_ft.wlIdx = widx;
    /* the retrace's loop: the game (less the orbit's work inside it), the lists, the orbit (its step after
     * the retrace before, its lists and frame setup), the retrace before's updateScreen */
    if (g_ft.lastViEnter >= 0) slot.w.retraceMs = std::max(0.0, viEnterMs - g_ft.lastViEnter - g_ft.lastWait);
    longStepLog(slot.w.retraceMs, gameMs, dlMs, g_ft.lastSubmit, orb.stepMs);
    slot.w.orbitMs = orb.stepMs + orb.cellsMs + orb.taskMs;
    slot.w.gameMs = std::max(0.0, gameMs - orb.cellsMs - orb.taskMs);
    slot.w.listsMs = dlMs;
    slot.w.submitMs = g_ft.lastSubmit;
    slot.w.otherMs = std::max(0.0, slot.w.retraceMs - slot.w.gameMs - slot.w.listsMs - slot.w.orbitMs - slot.w.submitMs);
    slot.w.queueFull = queueFull;
    if (shaders < g_ft.shaderSeen || ready < g_ft.shaderReadySeen) g_ft.shaderSeen = g_ft.shaderReadySeen = 0;   /* (cache made again: MSAA) */
    slot.w.shaders = unsigned(shaders - g_ft.shaderSeen);
    const unsigned newReady = unsigned(ready - g_ft.shaderReadySeen);
    slot.w.textures = textures > g_ft.textureSeen ? unsigned(textures - g_ft.textureSeen) : 0;
    g_ft.shaderSeen = shaders, g_ft.shaderReadySeen = ready, g_ft.textureSeen = textures;
    g_ft.lastViEnter = viEnterMs;
    g_ft.lastWait = waitMs;
    g_ft.lastSubmit = submitMs;
    for (Busy *b : {&g_ft.segBusy, &g_ft.winBusy}) {
        addOrbit(b->orbit, orb);
        b->retraces++;
        b->orbitMax = std::max(b->orbitMax, slot.w.orbitMs);
        b->retraceMax = std::max(b->retraceMax, slot.w.retraceMs);
        b->renderMax = std::max(b->renderMax, slot.w.renderMs);
        b->shaders += slot.w.shaders;
        b->shadersReady += newReady;
        b->textures += slot.w.textures;
        b->missed += slot.w.retraceMs > 1.5 * slot.w.periodMs && slot.w.retraceMs < 250.0;
    }
    g_ft.slots[g_ft.slotPos] = slot;
    g_ft.slotPos = (g_ft.slotPos + 1) % 64;
    g_ft.slotN = std::min(64u, g_ft.slotN + 1);
    spikeAttribute(viEnterMs);
    const double now = secondsSince(g_tStart);
    if (g_ft.winStart <= 0) {
        g_ft.winStart = now;
    } else if (now - g_ft.winStart >= 10.0) {
        if (g_ft.win.n > 0) frameLog("(10 s)", g_ft.win, g_ft.winBusy, now - g_ft.winStart, false, g_ft.winC);
        g_ft.win.reset();
        g_ft.winC = ContentStats();
        g_ft.winBusy = Busy();
        g_ft.winStart = now;
    }
}

/* the F2 counter (detailed): the last 240 intervals, the stutters since it was turned on */
unsigned g_stutterBase;
void counterGraph(live::CounterStats &s) {
    std::lock_guard<std::mutex> lock(g_ft.mutex);
    s.graph.clear();
    for (unsigned i = 0; i < g_ft.ringN; i++) s.graph.push_back(g_ft.ring[(g_ft.ringPos + 240 - g_ft.ringN + i) % 240]);
    s.targetMs = g_ft.targetMs;
    s.stutters = g_ft.stutters - g_stutterBase;
}
void counterStutterReset() {
    std::lock_guard<std::mutex> lock(g_ft.mutex);
    g_stutterBase = g_ft.stutters;
}

/* at exit */
void frameEnd() {
    {
        std::lock_guard<std::mutex> lock(g_ft.mutex);
        if (g_ft.spikeLog != nullptr) fclose(g_ft.spikeLog);   /* (--spike-log) */
        g_ft.spikeLog = nullptr;
        if (g_ft.pictureLog != nullptr) fclose(g_ft.pictureLog);   /* (--picture-log) */
        g_ft.pictureLog = nullptr;
    }
    static bool done;
    if (g_app == nullptr || g_ft.segment == 0 || done) return;
    done = true;
    const double now = secondsSince(g_tStart);
    std::lock_guard<std::mutex> lock(g_ft.mutex);
    if (g_ft.seg.n == 0) return;
    char head[64];
    snprintf(head, sizeof head, "segment %u, at exit", g_ft.segment);
    frameLog(head, g_ft.seg, g_ft.segBusy, now - g_ft.segStart, true, g_ft.segC);
}

struct Stats {
    double mean = 0, sd = 0, p01 = 0, p99 = 0, min = 0, max = 0;
    size_t n = 0;
};
Stats stats(std::vector<double> v) {
    Stats s;
    s.n = v.size();
    if (v.empty()) return s;
    std::sort(v.begin(), v.end());
    double sum = 0, sq = 0;
    for (double x : v) sum += x;
    s.mean = sum / double(v.size());
    for (double x : v) sq += (x - s.mean) * (x - s.mean);
    s.sd = std::sqrt(sq / double(v.size()));
    s.min = v.front();
    s.max = v.back();
    s.p01 = v[v.size() / 100];
    s.p99 = v[v.size() - 1 - v.size() / 100];
    return s;
}

void writePaceLog() {
    if (g_opt.paceLog == nullptr || g_pace.size() < 2) return;
    FILE *f = static_cast<FILE *>(host_fopen(g_opt.paceLog, "w"));
    if (f != nullptr) {
        fprintf(f, "kind,index,t_ms,due_ms,update_ms,frames\n");
        for (size_t i = 0; i < g_pace.size(); i++)
            fprintf(f, "vi,%u,%.3f,%.3f,%.3f,%u\n", unsigned(i), g_pace[i].t * 1e3, g_pace[i].due * 1e3,
                    g_pace[i].update * 1e3, g_pace[i].frames);
        size_t np = std::min(g_presents.load(), kMaxPresents);
        for (size_t i = 0; i < np; i++) fprintf(f, "present,%u,%.3f,,,\n", unsigned(i), g_presentTimes[i] * 1e3);
        fclose(f);
    }
    /* skip the first second (start-up, shader compiles) */
    std::vector<double> vi, upd, pr;
    double t0 = g_pace.front().t + 1.0;
    for (size_t i = 1; i < g_pace.size(); i++) {
        if (g_pace[i].t < t0) continue;
        vi.push_back((g_pace[i].t - g_pace[i - 1].t) * 1e3);
        upd.push_back(g_pace[i].update * 1e3);
    }
    size_t np = std::min(g_presents.load(), kMaxPresents);
    for (size_t i = 1; i < np; i++)
        if (g_presentTimes[i] >= t0) pr.push_back((g_presentTimes[i] - g_presentTimes[i - 1]) * 1e3);
    Stats a = stats(vi), u = stats(upd), p = stats(pr);
    size_t over = 0, prOver = 0;
    for (double d : vi) over += d > 1.5 * 1000.0 / 60.0;
    for (double d : pr) prOver += d > 1.5 * 1000.0 / 60.0;
    const double span = g_pace.back().t - g_pace.front().t;
    host_log("live: pacing: %u retraces in %.2f s real time = %.3f/s (virtual 60.000); display %u Hz, vsync %s, %s; "
             "%u pacing resets\n",
             unsigned(g_pace.size()), span, double(g_pace.size() - 1) / span, displayHz(), g_opt.vsync ? "on" : "off",
             g_opt.fullscreen ? "fullscreen" : "windowed", g_paceResets);
    host_log("live: pacing: retrace interval ms mean %.3f sd %.3f p1 %.3f p99 %.3f min %.3f max %.3f, %u > 25 ms\n",
             a.mean, a.sd, a.p01, a.p99, a.min, a.max, unsigned(over));
    host_log("live: pacing: updateScreen ms mean %.3f p99 %.3f max %.3f\n", u.mean, u.p99, u.max);
    host_log("live: pacing: %u presents (%u retraces); present interval ms mean %.3f sd %.3f p1 %.3f p99 %.3f "
             "max %.3f, %u > 25 ms\n",
             unsigned(np), unsigned(g_pace.size()), p.mean, p.sd, p.p01, p.p99, p.max, unsigned(prOver));
}

/* ---- where the time goes (--perf-log FILE, testing) --------------------------
 * Twice a second one line: the game's frames; RT64's rendered pictures and
 * their GPU time (RT64's own timestamp queries, WorkloadQueue::
 * rendererGPUProfiler) and its render thread's time per workload; the main
 * thread's time in processDisplayLists and waiting for the paced retrace
 * (the rest of the window is the game's and the host's work); the orbit
 * camera's host work; the frame workloads' draw calls and triangles.  It
 * only reads clocks and counters (the profilers' histories are read without
 * their thread's lock: testing numbers). */
PerfLog g_perf;
double g_perfDlSeconds;              /* time in processDisplayLists (--perf-log) */

void perfTake(const RT64::ProfilingTimer &p, uint32_t &idx, double &sum, double &mx, unsigned &n) {
    const uint32_t now = p.index();
    const size_t size = p.size();
    const double *d = p.data();
    for (uint32_t i = idx; i != now; i = uint32_t((i + 1) % size)) {
        sum += d[i];
        mx = std::max(mx, d[i]);
        n++;
    }
    idx = now;
}

/* the frame's workload (the frame's main task: the workload it was written
 * to, just after processDisplayLists) */
void perfWorkload(int cursor) {
    if (g_perf.path == nullptr) return;
    const RT64::WorkloadQueue &q = *g_app->workloadQueue;
    const RT64::Workload &w = q.workloads[cursor];
    unsigned calls = 0;
    for (uint32_t i = 0; i < w.fbPairCount && i < w.fbPairs.size(); i++) calls += w.fbPairs[i].gameCallCount;
    if (calls == 0) return;
    g_perf.cur.workloads++;
    g_perf.cur.calls += calls;
    g_perf.cur.tris += double(w.drawData.faceIndices.size()) / 3.0 + double(w.drawData.rawTriVertexCount()) / 3.0;
}

/* each retrace, after updateScreen */
void perfTick(unsigned frames) {
    if (g_perf.path == nullptr) return;
    const double now = secondsSince(g_tStart);
    if (g_perf.start < 0) {
        g_perf.start = now;
        g_perf.cur = PerfWin();
        g_perf.cur.t = now;
        g_perf.cur.frames = frames;
        g_perf.gpuIdx = g_app->workloadQueue->rendererGPUProfiler.index();
        g_perf.wlIdx = g_app->workloadQueue->workloadProfiler.index();
        g_perf.dlStart = g_perfDlSeconds;
        live::orbitPerfTake();
        return;
    }
    if (now - g_perf.cur.t < 0.5) return;
    PerfWin &w = g_perf.cur;
    w.span = now - w.t;
    w.frames = frames - w.frames;
    w.frameEnd = frames;
    perfTake(g_app->workloadQueue->rendererGPUProfiler, g_perf.gpuIdx, w.gpuSum, w.gpuMax, w.gpuN);
    perfTake(g_app->workloadQueue->workloadProfiler, g_perf.wlIdx, w.wlSum, w.wlMax, w.wlN);
    w.dlMs = (g_perfDlSeconds - g_perf.dlStart) * 1e3;
    g_perf.dlStart = g_perfDlSeconds;
    w.orbit = live::orbitPerfTake();
    w.t -= g_perf.start;
    if (g_perf.wins.size() < 100000) g_perf.wins.push_back(w);
    g_perf.cur = PerfWin();
    g_perf.cur.t = now;
    g_perf.cur.frames = frames;
}

void perfWait(double seconds) {
    if (g_perf.path != nullptr) g_perf.cur.waitMs += seconds * 1e3;
}

void perfWrite() {
    if (g_perf.path == nullptr) return;
    const char *path = g_perf.path;
    g_perf.path = nullptr;
    FILE *f = static_cast<FILE *>(host_fopen(path, "w"));
    if (f == nullptr) return;
    fprintf(f, "t_s,span_s,frame_end,frames,renders,gpu_ms_sum,gpu_ms_max,workloads_rt64,wl_ms_sum,wl_ms_max,dl_ms,dl_tasks,"
               "wait_ms,frame_workloads,calls,tris,orbit_step_ms,orbit_cells_ms,orbit_lists,orbit_cells,game_cells,"
               "orbit_read_ms,orbit_queries,orbit_task_ms,orbit_view_ms,orbit_views,orbit_lists_all\n");
    for (const PerfWin &w : g_perf.wins)
        fprintf(f, "%.3f,%.3f,%u,%u,%u,%.3f,%.3f,%u,%.3f,%.3f,%.3f,%u,%.3f,%u,%.0f,%.0f,%.3f,%.3f,%u,%u,%u,%.3f,%u,%.3f,%.3f,%u,"
                   "%u\n", w.t,
                w.span, w.frameEnd, w.frames, w.gpuN, w.gpuSum, w.gpuMax, w.wlN, w.wlSum, w.wlMax, w.dlMs, w.dlTasks, w.waitMs,
                w.workloads, w.calls, w.tris, w.orbit.stepMs, w.orbit.cellsMs, w.orbit.lists, w.orbit.cells,
                w.orbit.gameCells, w.orbit.readMs, w.orbit.queries, w.orbit.taskMs, w.orbit.viewMs, w.orbit.views,
                w.orbit.listsAll);
    fclose(f);
    host_log("live: perf log: %u windows -> %s\n", unsigned(g_perf.wins.size()), path);
}

}  // namespace bcexe
