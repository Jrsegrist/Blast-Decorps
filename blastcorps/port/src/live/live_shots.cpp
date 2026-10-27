/* bc.exe's screenshots (live_rt64.h): the picture the VI shows, from RDRAM (--shot, --shot-every), the
 * window's picture as presented (--wshot, --menu-shot, the level viewer's shots), and --wseq's sequence of
 * every picture the window shows. */
#include <windows.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <thread>
#include <vector>

#include "hle/rt64_application.h"

#include "live_rt64.h"

extern "C" {
#include "plat_host.h"
#include "load/tex_codec.h"
}

namespace bcexe {

/* ---- screenshots: RGBA16 frame from RDRAM -> PNG --------------------------- */
/* RGB pixels as a PNG file (tex_codec.c's encoder: stored deflate blocks) */
bool writePng(const char *path, unsigned w, unsigned h, const std::vector<uint8_t> &rgb) {
    size_t n = 0;
    uint8_t *png = tc_png_encode_px(rgb.data(), w, h, 3, &n);
    const bool ok = png != nullptr && host_write_file(path, png, unsigned(n)) == 0;
    free(png);
    return ok;
}

/* the picture the VI shows: origin, width, and the line count from vStart and yScale */
void saveFrame(const HostViRegs &r, unsigned frame) {
    unsigned w = r.width, vs = r.v_start >> 16, ve = r.v_start & 0x3FF;
    unsigned h = ve > vs ? (ve - vs) / 2 * (r.y_scale & 0xFFF) / 0x400 : 0;
    if (w == 0 || h == 0 || w > 1024 || h > 1024 || (r.status & 3) != 2) {
        host_log("live: frame %u: no 16-bit picture on screen (status %X width %u)\n", frame, r.status, w);
        return;
    }
    if (h > 240) h = 240;
    std::vector<uint8_t> rgb(w * h * 3);
    const uint8_t *fb = rdramBase() + (r.origin & 0x7FFFFF);
    for (unsigned i = 0; i < w * h; i++) {
        unsigned p = (fb[i * 2] << 8) | fb[i * 2 + 1];   /* big-endian RGBA5551 */
        rgb[i * 3 + 0] = uint8_t(((p >> 11) & 31) * 255 / 31);
        rgb[i * 3 + 1] = uint8_t(((p >> 6) & 31) * 255 / 31);
        rgb[i * 3 + 2] = uint8_t(((p >> 1) & 31) * 255 / 31);
    }
    char path[512];
    snprintf(path, sizeof path, "%s/bc_%07u.png", g_opt.shotDir, frame);
    if (writePng(path, w, h, rgb))
        host_log("live: saved %s (%ux%u, fb 0x%08X; %u tasks drawn, %u not F3D (L3D), %u bytes converted)\n", path,
                 w, h, r.origin, g_tasks, g_otherUcodeTasks, g_fixedBytes);
}

/* the window's picture as presented (RT64's output at the window's size:
 * internal resolution, widescreen, the settings menu): --wshot, --menu-shot.
 * Waits until RT64 presented what it was given, then asks the compositor for
 * the client area (PrintWindow with PW_RENDERFULLCONTENT, which also works
 * for a flip-model swap chain). */
void saveWindowTo(const std::string &path);
void saveWindow(const char *name) {
    char path[512];
    snprintf(path, sizeof path, "%s/%s.png", g_opt.shotDir, name);
    saveWindowTo(path);
}

void saveWindowTo(const std::string &file) {
    const char *path = file.c_str();
    if (g_app == nullptr || host_gui_window == nullptr) return;
    g_app->presentQueue->waitForPresentId(g_app->state->presentId);
    Sleep(120);
    HWND hw = HWND(host_gui_window);
    RECT rc;
    GetClientRect(hw, &rc);
    const int w = rc.right - rc.left, h = rc.bottom - rc.top;
    if (w <= 0 || h <= 0) return;
    HDC wdc = GetDC(hw), mdc = CreateCompatibleDC(wdc);
    BITMAPINFO bi = {};
    bi.bmiHeader.biSize = sizeof bi.bmiHeader;
    bi.bmiHeader.biWidth = w;
    bi.bmiHeader.biHeight = -h;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    void *bits = nullptr;
    HBITMAP bmp = CreateDIBSection(wdc, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    HGDIOBJ old = SelectObject(mdc, bmp);
    const BOOL ok = PrintWindow(hw, mdc, PW_CLIENTONLY | 0x2 /* PW_RENDERFULLCONTENT */);
    std::vector<uint8_t> rgb(size_t(w) * h * 3);
    const uint8_t *p = static_cast<const uint8_t *>(bits);
    for (size_t i = 0; i < size_t(w) * h; i++) {
        rgb[i * 3 + 0] = p[i * 4 + 2];
        rgb[i * 3 + 1] = p[i * 4 + 1];
        rgb[i * 3 + 2] = p[i * 4 + 0];
    }
    SelectObject(mdc, old);
    DeleteObject(bmp);
    DeleteDC(mdc);
    ReleaseDC(hw, wdc);
    if (ok && writePng(path, unsigned(w), unsigned(h), rgb)) host_log("live: saved %s (%dx%d window)\n", path, w, h);
    else host_log("live: window picture %s failed\n", path);
}

/* --wseq F:MS (testing the frame rate option): from game frame F, a thread
 * takes the window's picture every 2 ms for MS milliseconds and keeps each
 * new one (a picture differing from the one before), so every picture the
 * display got (interpolated ones too) is saved: seq_FFFFFFF_NN.png in
 * --shot-dir, with the time it was first seen in seq_FFFFFFF.txt */
WinSeq g_wseq;

bool grabWindow(std::vector<uint8_t> &rgb, int &w, int &h) {
    HWND hw = HWND(host_gui_window);
    RECT rc;
    GetClientRect(hw, &rc);
    w = rc.right - rc.left;
    h = rc.bottom - rc.top;
    if (w <= 0 || h <= 0) return false;
    /* from the screen (the composed desktop: fast enough for every picture
     * at 60 a second, unlike PrintWindow; the window must be uncovered) */
    POINT org = {0, 0};
    ClientToScreen(hw, &org);
    HDC wdc = GetDC(nullptr), mdc = CreateCompatibleDC(wdc);
    BITMAPINFO bi = {};
    bi.bmiHeader.biSize = sizeof bi.bmiHeader;
    bi.bmiHeader.biWidth = w;
    bi.bmiHeader.biHeight = -h;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    void *bits = nullptr;
    HBITMAP bmp = CreateDIBSection(wdc, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    HGDIOBJ old = SelectObject(mdc, bmp);
    const BOOL ok = BitBlt(mdc, 0, 0, w, h, wdc, org.x, org.y, SRCCOPY);
    rgb.resize(size_t(w) * h * 3);
    const uint8_t *p = static_cast<const uint8_t *>(bits);
    for (size_t i = 0; i < size_t(w) * h; i++) {
        rgb[i * 3 + 0] = p[i * 4 + 2];
        rgb[i * 3 + 1] = p[i * 4 + 1];
        rgb[i * 3 + 2] = p[i * 4 + 0];
    }
    SelectObject(mdc, old);
    DeleteObject(bmp);
    DeleteDC(mdc);
    ReleaseDC(nullptr, wdc);
    return ok != 0;
}

void winSeqThread(unsigned frame, unsigned ms) {
    struct Pic { double t; std::vector<uint8_t> rgb; };
    std::vector<Pic> pics;
    int w = 0, h = 0;
    LARGE_INTEGER t0, freq;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&t0);
    auto msNow = [&]() {
        LARGE_INTEGER now;
        QueryPerformanceCounter(&now);
        return double(now.QuadPart - t0.QuadPart) * 1000.0 / double(freq.QuadPart);
    };
    std::vector<uint8_t> cur;
    while (msNow() < ms && pics.size() < 200) {
        int cw, ch;
        if (grabWindow(cur, cw, ch) && (pics.empty() || cur != pics.back().rgb)) {
            w = cw, h = ch;
            pics.push_back({msNow(), cur});
        }
        Sleep(1);
    }
    char path[512];
    snprintf(path, sizeof path, "%s/seq_%07u.txt", g_opt.shotDir, frame);
    std::string times;
    for (size_t i = 0; i < pics.size(); i++) {
        char name[512];
        snprintf(name, sizeof name, "%s/seq_%07u_%02u.png", g_opt.shotDir, frame, unsigned(i));
        if (pics[i].rgb.size() == size_t(w) * h * 3) writePng(name, unsigned(w), unsigned(h), pics[i].rgb);
        char line[64];
        snprintf(line, sizeof line, "%02u %.2f\n", unsigned(i), pics[i].t);
        times += line;
    }
    host_write_file(path, times.data(), unsigned(times.size()));
    host_log("live: --wseq: %u different pictures in %u ms from frame %u\n", unsigned(pics.size()), ms, frame);
}

}  // namespace bcexe
