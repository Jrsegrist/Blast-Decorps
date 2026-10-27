/* bc_packtool's work (see packtool.h).
 *
 * In:  a folder of pictures named by RT64 texture hash (<16 hex digits>.png,
 *      or anything after a first dot: <hash>.mine.png), in any subfolders,
 *      as bc.exe's texture dump writes them; any size (2x, 4x ... the
 *      original).  The dump's index.csv (when found) gives each texture's
 *      original size and transparency for the checks, and its PNGs show
 *      which pictures weren't changed.
 * Out: <packs>\<name>\ with rt64.json (RT64's replacement database,
 *      configuration version 3, hash version 5: every texture listed with its
 *      hash and path) and the pictures (PNG as given, or DDS with mipmaps),
 *      and/or <packs>\<name>.rtz (the same in one zip, stored).  rt64.json is
 *      written last (bc.exe's texture_pack_reload watches it).
 * Checks (warnings; the texture is still included): a hash the dump never
 *      saw, a size that isn't a whole multiple of the original or changes
 *      its shape, a power-of-two original made non-power-of-two, a picture
 *      with no transparency replacing one that had some.  Left out: files not
 *      named by a hash, unreadable ones, a second file for the same hash,
 *      pictures identical to the dump's (unless keepUnchanged). */
#include "packtool.h"

#include <windows.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_WINDOWS_UTF8
#include "stb/stb_image.h"
#include "miniz/miniz.h"

namespace packtool {

namespace fs = std::filesystem;

namespace {

struct Orig {
    unsigned w = 0, h = 0;
    std::string alpha, format, file;
};

std::string hex16(uint64_t h) {
    char b[24];
    snprintf(b, sizeof b, "%016llx", (unsigned long long) h);
    return b;
}

std::string lower(std::string s) {
    for (char &c : s) c = char(tolower((unsigned char) c));
    return s;
}

std::string u8(const fs::path &p) {
    return p.u8string();
}

bool pow2(unsigned v) {
    return v != 0 && (v & (v - 1)) == 0;
}

std::string jsonStr(const std::string &s) {
    std::string o = "\"";
    for (unsigned char c : s) {
        if (c == '"' || c == '\\') o += '\\', o += char(c);
        else if (c < 0x20) {
            char b[8];
            snprintf(b, sizeof b, "\\u%04x", c);
            o += b;
        } else o += char(c);
    }
    return o + "\"";
}

bool readFile(const fs::path &p, std::vector<uint8_t> &out) {
    std::ifstream in(p, std::ios::binary);
    if (!in) return false;
    out.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    return true;
}

bool writeFile(const fs::path &p, const void *data, size_t n) {
    std::ofstream out(p, std::ios::binary | std::ios::trunc);
    out.write(static_cast<const char *>(data), std::streamsize(n));
    return bool(out);
}

/* replace `dst` with `tmp` (the pack folder may be in use by bc.exe: retried a little) */
bool replaceFile(const fs::path &tmp, const fs::path &dst) {
    for (int i = 0; i < 20; i++) {
        if (MoveFileExW(tmp.wstring().c_str(), dst.wstring().c_str(), MOVEFILE_REPLACE_EXISTING)) return true;
        Sleep(100);
    }
    return false;
}

std::map<uint64_t, Orig> readIndex(const fs::path &csv) {
    std::map<uint64_t, Orig> m;
    std::ifstream in(csv);
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        std::vector<std::string> f;
        std::stringstream ss(line);
        std::string x;
        while (std::getline(ss, x, ',')) f.push_back(x);
        uint64_t h;
        if (f.size() < 7 || !parseHash(f[0], h)) continue;
        Orig o;
        o.file = f[1];
        o.format = f[2];
        o.w = unsigned(atoi(f[4].c_str()));
        o.h = unsigned(atoi(f[5].c_str()));
        o.alpha = f[6];
        m[h] = o;
    }
    return m;
}

bool opaque(const std::vector<uint8_t> &px) {
    for (size_t i = 3; i < px.size(); i += 4)
        if (px[i] != 255) return false;
    return true;
}

struct Entry {
    uint64_t hash;
    fs::path src;
    std::string rel;     /* in the pack, '/' separated, with its extension */
    std::string shift;   /* "" = the pack's default */
};

}  // namespace

std::string exeDir() {
    wchar_t buf[MAX_PATH * 4];
    DWORD n = GetModuleFileNameW(nullptr, buf, DWORD(sizeof buf / sizeof buf[0]));
    std::wstring w(buf, n);
    w.resize(w.find_last_of(L"\\/") + 1);
    return fs::path(w).u8string();
}

bool parseHash(const std::string &name, uint64_t &h) {
    const std::string s = name.substr(0, name.find('.'));
    if (s.size() != 16) return false;
    for (char c : s)
        if (!isxdigit((unsigned char) c)) return false;
    h = strtoull(s.c_str(), nullptr, 16);
    return true;
}

bool readPng(const std::string &path, std::vector<uint8_t> &rgba, unsigned &w, unsigned &h) {
    std::vector<uint8_t> bytes;
    if (!readFile(fs::u8path(path), bytes) || bytes.empty()) return false;
    int iw, ih, n;
    stbi_uc *d = stbi_load_from_memory(bytes.data(), int(bytes.size()), &iw, &ih, &n, 4);
    if (d == nullptr) return false;
    w = unsigned(iw);
    h = unsigned(ih);
    rgba.assign(d, d + size_t(iw) * ih * 4);
    stbi_image_free(d);
    return true;
}

bool writePng(const std::string &path, const uint8_t *rgba, unsigned w, unsigned h) {
    size_t len = 0;
    void *png = tdefl_write_image_to_png_file_in_memory_ex(rgba, int(w), int(h), 4, &len, MZ_DEFAULT_LEVEL, MZ_FALSE);
    if (png == nullptr) return false;
    const bool ok = writeFile(fs::u8path(path), png, len);
    mz_free(png);
    return ok;
}

/* an uncompressed RGBA8 DDS (legacy header: ddspp and RT64 read it as
 * R8G8B8A8_UNORM) with every mipmap down to 1x1; each level a 2x2 box filter
 * of the one above, colours weighted by alpha (no dark fringes) */
std::vector<uint8_t> makeDds(const uint8_t *rgba, unsigned w, unsigned h) {
    unsigned levels = 1;
    for (unsigned m = std::max(w, h); m > 1; m >>= 1) levels++;
    std::vector<uint8_t> out(128, 0);
    auto put = [&](size_t off, uint32_t v) { memcpy(&out[off], &v, 4); };
    put(0, 0x20534444u);                       /* "DDS " */
    put(4, 124);                               /* header size */
    put(8, 0x1 | 0x2 | 0x4 | 0x8 | 0x1000 | 0x20000);   /* caps, height, width, pitch, pixel format, mip count */
    put(12, h);
    put(16, w);
    put(20, w * 4);
    put(28, levels);
    put(76, 32);                               /* pixel format size */
    put(80, 0x40 | 0x1);                       /* RGB | alpha pixels */
    put(88, 32);
    put(92, 0x000000FFu);
    put(96, 0x0000FF00u);
    put(100, 0x00FF0000u);
    put(104, 0xFF000000u);
    put(108, 0x1000 | 0x400000 | 0x8);         /* texture, mipmap, complex */
    std::vector<uint8_t> cur(rgba, rgba + size_t(w) * h * 4);
    unsigned cw = w, ch = h;
    for (unsigned l = 0; l < levels; l++) {
        out.insert(out.end(), cur.begin(), cur.end());
        if (l + 1 == levels) break;
        const unsigned nw = std::max(1u, cw / 2), nh = std::max(1u, ch / 2);
        std::vector<uint8_t> nx(size_t(nw) * nh * 4);
        for (unsigned y = 0; y < nh; y++)
            for (unsigned x = 0; x < nw; x++) {
                unsigned sum[4] = {}, wsum = 0, n = 0;
                for (unsigned dy = 0; dy < 2; dy++)
                    for (unsigned dx = 0; dx < 2; dx++) {
                        const unsigned sx = std::min(cw - 1, x * 2 + dx), sy = std::min(ch - 1, y * 2 + dy);
                        const uint8_t *p = &cur[(size_t(sy) * cw + sx) * 4];
                        for (int c = 0; c < 3; c++) sum[c] += p[c] * p[3];
                        sum[3] += p[3];
                        wsum += p[3];
                        n++;
                    }
                uint8_t *q = &nx[(size_t(y) * nw + x) * 4];
                for (int c = 0; c < 3; c++) {
                    if (wsum != 0) q[c] = uint8_t((sum[c] + wsum / 2) / wsum);
                    else {
                        unsigned s = 0;
                        for (unsigned dy = 0; dy < 2; dy++)
                            for (unsigned dx = 0; dx < 2; dx++)
                                s += cur[(size_t(std::min(ch - 1, y * 2 + dy)) * cw + std::min(cw - 1, x * 2 + dx)) * 4 + c];
                        q[c] = uint8_t((s + 2) / 4);
                    }
                }
                q[3] = uint8_t((sum[3] + n / 2) / n);
            }
        cur.swap(nx);
        cw = nw;
        ch = nh;
    }
    return out;
}

Result build(const Options &o) {
    Result r;
    /* (quiet: the per-file "skipped" lines aren't printed; warnings and the summary are) */
    auto say = [&](const std::string &s) {
        r.lines.push_back(s);
        if (!o.quiet || s.rfind("  skipped", 0) != 0) printf("%s\n", s.c_str());
    };
    auto warn = [&](const std::string &s) {
        r.warnings++;
        say("  warning: " + s);
    };
    std::error_code ec;
    const fs::path src = fs::u8path(o.source);
    if (!fs::is_directory(src, ec)) {
        say("error: " + o.source + " is not a folder");
        return r;
    }
    std::string name = o.name;
    if (name.empty()) name = u8(fs::absolute(src, ec).lexically_normal().filename());
    if (name.empty()) name = "pack";
    const fs::path packs = o.packs.empty() ? fs::u8path(exeDir() + "packs") : fs::u8path(o.packs);
    const fs::path outDir = o.out.empty() ? packs / fs::u8path(name) : fs::u8path(o.out);
    if (o.shift != "auto" && o.shift != "half" && o.shift != "none") {
        say("error: --shift " + o.shift + ": auto, half or none");
        return r;
    }
    if (o.operation != "preload" && o.operation != "stream" && o.operation != "stall") {
        say("error: operation " + o.operation + ": preload, stream or stall");
        return r;
    }

    /* the dump: its index and pictures */
    fs::path dump = o.dump.empty() ? fs::u8path(exeDir() + "texture_dump") : fs::u8path(o.dump);
    if (!fs::is_regular_file(dump / "index.csv", ec) && o.dump.empty() && fs::is_regular_file(src / "index.csv", ec))
        dump = src;   /* (pictures edited in the dump folder itself) */
    std::map<uint64_t, Orig> index;
    const bool haveIndex = fs::is_regular_file(dump / "index.csv", ec);
    if (haveIndex) {
        index = readIndex(dump / "index.csv");
        say("dump index: " + u8(dump / "index.csv") + " (" + std::to_string(index.size()) + " textures)");
    } else {
        say("no texture dump index found (" + u8(dump / "index.csv") + "): sizes and hashes can't be checked");
    }

    /* the pictures */
    std::vector<fs::path> files;
    for (auto it = fs::recursive_directory_iterator(src, ec); !ec && it != fs::recursive_directory_iterator(); it.increment(ec)) {
        if (!it->is_regular_file(ec)) continue;
        const std::string ext = lower(u8(it->path().extension()));
        if (ext == ".png" || ext == ".dds") files.push_back(it->path());
    }
    std::sort(files.begin(), files.end());
    std::map<uint64_t, Entry> entries;
    for (const fs::path &f : files) {
        const std::string relFwd = f.lexically_relative(src).generic_u8string();
        uint64_t h;
        if (!parseHash(u8(f.filename()), h)) {
            say("  skipped " + relFwd + ": not named <hash>.png (16 hex digits, as the texture dump names them)");
            r.skipped++;
            continue;
        }
        if (entries.count(h)) {
            say("  skipped " + relFwd + ": a second file for " + hex16(h) + " (using " + entries[h].rel + ")");
            r.skipped++;
            continue;
        }
        const bool isDds = lower(u8(f.extension())) == ".dds";
        std::vector<uint8_t> px;
        unsigned w = 0, hh = 0;
        if (isDds) {
            std::vector<uint8_t> b;
            if (!readFile(f, b) || b.size() < 128 || memcmp(b.data(), "DDS ", 4) != 0) {
                say("  skipped " + relFwd + ": not a DDS file");
                r.skipped++;
                continue;
            }
            memcpy(&hh, &b[12], 4);
            memcpy(&w, &b[16], 4);
        } else if (!readPng(u8(f), px, w, hh)) {
            say("  skipped " + relFwd + ": can't read it as a PNG");
            r.skipped++;
            continue;
        }
        Entry e;
        e.hash = h;
        e.src = f;
        std::string outRel = relFwd;
        if (o.mipmaps && !isDds) outRel = outRel.substr(0, outRel.size() - 4) + ".dds";
        e.rel = outRel;
        auto oi = index.find(h);
        if (oi != index.end() && oi->second.format == "raw") {
            say("  skipped " + relFwd + ": RT64 doesn't decode this texture (raw TMEM): it can't be replaced");
            r.skipped++;
            continue;
        }
        if (haveIndex && oi == index.end()) {
            warn(relFwd + ": " + hex16(h) + " isn't in the dump's index (a texture the dump never saw: it may never match)");
        } else if (oi != index.end() && oi->second.w != 0 && oi->second.h != 0) {
            const Orig &og = oi->second;
            const std::string orig = std::to_string(og.w) + "x" + std::to_string(og.h);
            const std::string now = std::to_string(w) + "x" + std::to_string(hh);
            if (w % og.w != 0 || hh % og.h != 0)
                warn(relFwd + ": " + now + " isn't a whole multiple of the original " + orig);
            else if (w / og.w != hh / og.h)
                warn(relFwd + ": " + now + " changes the shape of the original " + orig + " (scaled " +
                     std::to_string(w / og.w) + "x across, " + std::to_string(hh / og.h) + "x down)");
            if ((pow2(og.w) && !pow2(w)) || (pow2(og.h) && !pow2(hh)))
                warn(relFwd + ": " + now + " isn't a power of two (the original " + orig + " is)");
            if (!isDds && (og.alpha == "binary" || og.alpha == "translucent") && opaque(px))
                warn(relFwd + ": the original has transparency (" + og.alpha +
                     ") but this picture is fully opaque: its transparent parts will be drawn solid");
            /* unchanged: the same pixels as the dump's picture */
            if (!isDds && !o.keepUnchanged && w == og.w && hh == og.h && !og.file.empty()) {
                std::vector<uint8_t> dp;
                unsigned dw, dh;
                if (readPng(u8(dump / fs::u8path(og.file)), dp, dw, dh) && dw == w && dh == hh && dp == px) {
                    r.unchanged++;
                    r.skipped++;
                    continue;
                }
            }
            /* at the original size RT64's half-texel shift (meant for scaled pictures) would move it */
            if (o.shift == "auto" && w == og.w && hh == og.h) e.shift = "none";
        }
        if (o.shift == "none") e.shift = "none";
        entries[h] = e;
    }
    if (r.unchanged) say("  left out " + std::to_string(r.unchanged) + " pictures identical to the dump's (--keep-unchanged keeps them)");
    if (entries.empty()) {
        say("error: no textures to pack in " + o.source +
            (r.unchanged ? " (every picture is the same as the dump's: change some first)" : ""));
        return r;
    }

    /* rt64.json */
    std::string json = "{\n  \"configuration\": {\n    \"autoPath\": \"rt64\",\n    \"configurationVersion\": 3,\n"
                       "    \"defaultOperation\": " + jsonStr(o.operation) + ",\n    \"defaultShift\": " +
                       jsonStr(o.shift == "none" ? "none" : "half") + ",\n    \"hashVersion\": 5\n  },\n  \"textures\": [\n";
    bool first = true;
    for (const auto &kv : entries) {
        const Entry &e = kv.second;
        json += std::string(first ? "" : ",\n") + "    {\"path\": " + jsonStr(e.rel) + ", \"hashes\": {\"rt64\": " +
                jsonStr(hex16(e.hash)) + ", \"rice\": \"\"}" + (e.shift.empty() ? "" : ", \"shift\": " + jsonStr(e.shift)) + "}";
        first = false;
    }
    json += "\n  ],\n  \"operationFilters\": [],\n  \"shiftFilters\": [],\n  \"extraFiles\": []\n}\n";

    /* the pictures as they go into the pack */
    auto content = [&](const Entry &e, std::vector<uint8_t> &bytes) {
        if (!readFile(e.src, bytes)) return false;
        if (o.mipmaps && lower(u8(e.src.extension())) == ".png") {
            std::vector<uint8_t> px;
            unsigned w, h;
            if (!readPng(u8(e.src), px, w, h)) return false;
            bytes = makeDds(px.data(), w, h);
        }
        return true;
    };

    if (o.folder) {
        fs::create_directories(outDir, ec);
        if (!fs::is_directory(outDir, ec)) {
            say("error: can't make the folder " + u8(outDir));
            return r;
        }
        /* what an earlier build left: hash-named pictures no longer in the pack */
        for (auto it = fs::recursive_directory_iterator(outDir, ec); !ec && it != fs::recursive_directory_iterator(); it.increment(ec)) {
            uint64_t h;
            const std::string ext = lower(u8(it->path().extension()));
            if (!it->is_regular_file(ec) || (ext != ".png" && ext != ".dds") || !parseHash(u8(it->path().filename()), h)) continue;
            auto e = entries.find(h);
            const std::string rel = it->path().lexically_relative(outDir).generic_u8string();
            if (e == entries.end() || e->second.rel != rel) {
                std::error_code e2;
                fs::remove(it->path(), e2);
            }
        }
        for (const auto &kv : entries) {
            const Entry &e = kv.second;
            const fs::path dst = outDir / fs::u8path(e.rel);
            fs::create_directories(dst.parent_path(), ec);
            std::vector<uint8_t> bytes, old;
            if (!content(e, bytes)) {
                say("error: can't read " + u8(e.src));
                return r;
            }
            if (readFile(dst, old) && old == bytes) continue;   /* unchanged since the last build */
            if (!writeFile(dst, bytes.data(), bytes.size())) {
                say("error: can't write " + u8(dst));
                return r;
            }
        }
        const fs::path tmp = outDir / "rt64.json.tmp";
        if (!writeFile(tmp, json.data(), json.size()) || !replaceFile(tmp, outDir / "rt64.json")) {
            say("error: can't write " + u8(outDir / "rt64.json"));
            return r;
        }
        r.packDir = u8(outDir);
    }
    if (o.rtz) {
        const fs::path rtz = o.out.empty() ? packs / fs::u8path(name + ".rtz") : fs::u8path(o.out + ".rtz");
        fs::create_directories(rtz.parent_path(), ec);
        const fs::path tmp = fs::u8path(u8(rtz) + ".tmp");
        mz_zip_archive zip = {};
        bool ok = mz_zip_writer_init_heap(&zip, 0, 0) != 0;
        for (const auto &kv : entries) {
            std::vector<uint8_t> bytes;
            ok = ok && content(kv.second, bytes) &&
                 mz_zip_writer_add_mem(&zip, kv.second.rel.c_str(), bytes.data(), bytes.size(), MZ_NO_COMPRESSION);
        }
        ok = ok && mz_zip_writer_add_mem(&zip, "rt64.json", json.data(), json.size(), MZ_BEST_COMPRESSION);
        void *buf = nullptr;
        size_t len = 0;
        ok = ok && mz_zip_writer_finalize_heap_archive(&zip, &buf, &len);
        if (ok) ok = writeFile(tmp, buf, len) && replaceFile(tmp, rtz);
        mz_zip_writer_end(&zip);
        if (!ok) {
            say("error: can't write " + u8(rtz));
            return r;
        }
        r.rtzPath = u8(rtz);
    }
    r.textures = unsigned(entries.size());
    r.ok = true;
    say(std::to_string(r.textures) + " textures packed" + (r.warnings ? ", " + std::to_string(r.warnings) + " warnings" : "") +
        (r.skipped ? ", " + std::to_string(r.skipped) + " files left out" : "") + ":");
    if (!r.packDir.empty()) say("  " + r.packDir + "  (folder: choose it in the game's settings, F1 > Textures)");
    if (!r.rtzPath.empty()) say("  " + r.rtzPath + "  (one file: for sharing)");
    return r;
}

}  // namespace packtool
