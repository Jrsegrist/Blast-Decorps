/* bc_packtool.exe: makes an RT64 texture pack for bc.exe from edited
 * textures of its texture dump (packtool.cpp).  A console program; dropping
 * a folder on it in Explorer works too (it then waits for Enter before its
 * window closes). */
#include <windows.h>

#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <thread>
#include <vector>

#include "packtool.h"

namespace fs = std::filesystem;

static const char kUsage[] =
    "bc_packtool SOURCE [options]\n"
    "  Makes a texture pack for Blast Corps (bc.exe) from your edited textures.\n"
    "  SOURCE: a folder of pictures named <hash>.png, as bc.exe's texture dump writes them\n"
    "  (F1 > Textures > Dump textures; subfolders are fine), edited or redrawn at any size\n"
    "  (2x, 4x ... the original). By default the pack goes to packs\\<SOURCE's name> next to\n"
    "  this program, where the game's settings menu (F1 > Textures) lists it.\n"
    "options:\n"
    "  --name NAME        the pack's name (default: SOURCE's folder name)\n"
    "  --out DIR          write the pack folder here instead\n"
    "  --rtz              also write packs\\NAME.rtz: the whole pack in one file, for sharing\n"
    "  --rtz-only         only the .rtz\n"
    "  --dump DIR         the texture dump (default: texture_dump next to this program): its\n"
    "                     index.csv checks hashes, sizes and transparency, and pictures identical\n"
    "                     to its own are left out\n"
    "  --keep-unchanged   keep pictures identical to the dump's\n"
    "  --mipmaps          DDS files with mipmaps instead of PNG: smoother where big pictures are\n"
    "                     shown small (distant ground), bigger files\n"
    "  --shift S          auto (default): pictures at their original size as they are, scaled ones\n"
    "                     with RT64's half-texel shift; half or none for all\n"
    "  --stream           load pictures in the background as they are needed (RT64's default for\n"
    "                     big packs) instead of all when the pack loads\n"
    "  --watch            keep running and build again whenever a picture in SOURCE changes (with\n"
    "                     the game's \"Reload the pack by itself\" setting the game shows it at once)\n"
    "  --quiet            only the summary and warnings\n";

static std::string utf8(const wchar_t *w) {
    const int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
    std::string s(n > 0 ? n - 1 : 0, '\0');
    if (n > 1) WideCharToMultiByte(CP_UTF8, 0, w, -1, &s[0], n, nullptr, nullptr);
    return s;
}

/* the files of SOURCE with their sizes and times (a change rebuilds) */
static std::string signature(const std::string &dir) {
    std::string s;
    std::error_code ec;
    for (auto it = fs::recursive_directory_iterator(fs::u8path(dir), ec); !ec && it != fs::recursive_directory_iterator();
         it.increment(ec)) {
        std::error_code e2;
        if (!it->is_regular_file(e2)) continue;
        s += it->path().u8string() + "|" + std::to_string(it->file_size(e2)) + "|" +
             std::to_string(it->last_write_time(e2).time_since_epoch().count()) + "\n";
    }
    return s;
}

static int run(int argc, char **argv) {
    packtool::Options o;
    bool watch = false;
    for (int i = 1; i < argc; i++) {
        const std::string a = argv[i];
        auto next = [&]() -> std::string {
            if (i + 1 >= argc) {
                fprintf(stderr, "%s needs a value\n\n%s", a.c_str(), kUsage);
                exit(2);
            }
            return argv[++i];
        };
        if (a == "--name") o.name = next();
        else if (a == "--out") o.out = next();
        else if (a == "--packs") o.packs = next();
        else if (a == "--rtz") o.rtz = true;
        else if (a == "--rtz-only") o.rtz = true, o.folder = false;
        else if (a == "--dump") o.dump = next();
        else if (a == "--keep-unchanged") o.keepUnchanged = true;
        else if (a == "--mipmaps") o.mipmaps = true;
        else if (a == "--shift") o.shift = next();
        else if (a == "--stream") o.operation = "stream";
        else if (a == "--watch") watch = true;
        else if (a == "--quiet") o.quiet = true;
        else if (a == "-h" || a == "--help" || a == "/?") {
            printf("%s", kUsage);
            return 0;
        } else if (!a.empty() && a[0] == '-') {
            fprintf(stderr, "unknown option %s\n\n%s", a.c_str(), kUsage);
            return 2;
        } else if (o.source.empty()) o.source = a;
        else {
            fprintf(stderr, "one SOURCE folder only (%s?)\n\n%s", a.c_str(), kUsage);
            return 2;
        }
    }
    if (o.source.empty()) {
        printf("%s", kUsage);
        return 2;
    }
    packtool::Result r = packtool::build(o);
    if (!watch) return r.ok ? 0 : 1;
    printf("watching %s for changes (Ctrl+C stops)\n", o.source.c_str());
    std::string sig = signature(o.source);
    for (;;) {
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        std::string now = signature(o.source);
        if (now == sig) continue;
        /* wait until the editor has finished writing */
        do {
            sig = now;
            std::this_thread::sleep_for(std::chrono::milliseconds(700));
            now = signature(o.source);
        } while (now != sig);
        printf("\n-- a picture changed: building again\n");
        packtool::build(o);
    }
}

int main() {
    int argc = 0;
    wchar_t **wargv = CommandLineToArgvW(GetCommandLineW(), &argc);
    std::vector<std::string> args;
    for (int i = 0; i < argc; i++) args.push_back(utf8(wargv[i]));
    std::vector<char *> argv;
    for (std::string &s : args) argv.push_back(&s[0]);
    argv.push_back(nullptr);
    SetConsoleOutputCP(CP_UTF8);
    setvbuf(stdout, nullptr, _IONBF, 0);   /* (--watch runs until stopped: a redirected log stays current) */
    const int rc = run(argc, argv.data());
    /* started from Explorer (a folder dropped on it, or a double click): the
     * console is ours alone and closes with us, so wait for Enter first */
    DWORD pids[2];
    if (GetConsoleProcessList(pids, 2) == 1 && GetFileType(GetStdHandle(STD_INPUT_HANDLE)) == FILE_TYPE_CHAR) {
        printf("\nPress Enter to close this window.");
        fflush(stdout);
        getchar();
    }
    return rc;
}
