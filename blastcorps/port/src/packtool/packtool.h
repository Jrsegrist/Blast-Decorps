/* bc_packtool: makes an RT64 texture pack from edited textures of bc.exe's
 * texture dump (packtool.cpp; bc_packtool.exe, packtool_main.cpp).  Kept as a
 * library so bc_texturetest.exe can run it on synthetic textures. */
#ifndef PACKTOOL_H
#define PACKTOOL_H

#include <cstdint>
#include <string>
#include <vector>

namespace packtool {

struct Options {
    std::string source;          /* the folder of edited pictures named <hash>.png (subfolders too) */
    std::string name;            /* the pack's name ("" = the source folder's name) */
    std::string out;             /* the pack folder ("" = <packs>\<name>) */
    std::string packs;           /* the packs folder ("" = packs next to the exe) */
    std::string dump;            /* the texture dump ("" = texture_dump next to the exe, if there) */
    bool folder = true;          /* write the pack folder */
    bool rtz = false;            /* write <packs>\<name>.rtz (one file) */
    bool mipmaps = false;        /* DDS with mipmaps instead of PNG */
    std::string shift = "auto";  /* auto: none at the original size, half (RT64's default) when scaled */
    std::string operation = "preload";   /* RT64: preload (all when the pack loads), stream, stall */
    bool keepUnchanged = false;  /* include pictures identical to the dump's */
    bool quiet = false;
};

struct Result {
    unsigned textures = 0;       /* in the pack */
    unsigned warnings = 0;
    unsigned skipped = 0;        /* files left out (not a hash name, unreadable, duplicate, unchanged) */
    unsigned unchanged = 0;
    std::string packDir, rtzPath;
    std::vector<std::string> lines;   /* every message, as printed */
    bool ok = false;
};

/* build the pack once */
Result build(const Options &o);

/* the folder bc_packtool.exe is in (with a trailing backslash) */
std::string exeDir();

/* exposed for the tests */
bool parseHash(const std::string &fileName, uint64_t &hash);
std::vector<uint8_t> makeDds(const uint8_t *rgba, unsigned w, unsigned h);   /* RGBA8 + all mipmaps */
bool writePng(const std::string &path, const uint8_t *rgba, unsigned w, unsigned h);
bool readPng(const std::string &path, std::vector<uint8_t> &rgba, unsigned &w, unsigned &h);

}  // namespace packtool

#endif
