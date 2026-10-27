/* The texture dump (see live_texdump.h).
 *
 * Finding textures: RT64's TextureCache keeps every texture it has decoded in
 * textureMap (slots: textures[i] with its TMEM copy, tile and TLUT mode,
 * hashes[i]); globalVersion changes whenever a slot is filled.  Each retrace
 * the main thread looks for slots holding a texture it hasn't seen there yet
 * (an upload: the first, or again after RT64 evicted it) under RT64's
 * textureMapMutex, and queues the TMEM of hashes new to this session.  The
 * hash is RT64's own (TMEMHasher, the version texture packs use), so it is
 * exactly what a pack's rt64.json lists.  Raw-TMEM textures (decodeTMEM off:
 * RT64 samples TMEM in the shader, hashed differently) have no single
 * picture and are only counted.
 *
 * Writing: a worker thread decodes (tmem_decode.cpp, RT64's decoder in
 * bytes), encodes PNG (miniz, inside RT64's library) and writes the file and,
 * at most every two seconds and when stopping, index.csv (through a .tmp
 * file).  Nothing here writes to N64 memory or changes what RT64 draws. */
#include <windows.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <deque>
#include <filesystem>
#include <fstream>
#include <map>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "render/rt64_texture_cache.h"
#include "miniz/miniz.h"

#include "live_setup.h"
#include "live_texdump.h"
#include "tmem_decode.h"

extern "C" {
#include "plat_host.h"
}

namespace live {

namespace {

namespace fs = std::filesystem;

/* the game state (fixed addresses, as plat_core.c's trace) */
#define BC_MODE (*(volatile unsigned long long *) uintptr_t(0x80364A90u))
#define BC_LEVEL (*(volatile int *) uintptr_t(0x802E8BDCu))

struct Row {
    std::string file;       /* relative to the dump folder, '/' separated ("" = not written) */
    std::string format, tlut, alpha, where;
    unsigned width = 0, height = 0, frame = 0;
    unsigned long long uploads = 0;
};

struct Job {
    uint64_t hash;
    tmem::Tile tile;
    unsigned width, height;
    uint32_t tlut;
    std::string where;
    bool raw;                            /* no picture: only listed */
    std::array<uint8_t, 0x1000> bytes;
};

/* shared with the worker (g_mutex) */
std::mutex g_mutex;
std::condition_variable g_cv;
std::deque<Job> g_jobs;
std::map<uint64_t, Row> g_rows;          /* the index */
std::unordered_map<uint64_t, std::string> g_onDisk;   /* <hash>.png found below the folder */
bool g_dirty, g_stopping;
unsigned g_written, g_failed;
std::thread g_worker;
std::string g_dir;                       /* with a trailing backslash */
std::atomic<bool> g_on{false};

/* the main thread's */
std::vector<const RT64::Texture *> g_seen;   /* per cache slot, the texture last seen there */
uint32_t g_lastVersion = ~0u;
std::unordered_set<uint64_t> g_session;  /* hashes found this session */
unsigned g_raw;                          /* raw-TMEM textures (not dumped) */

std::string hex16(uint64_t h) {
    char b[24];
    snprintf(b, sizeof b, "%016llx", (unsigned long long) h);
    return b;
}

bool parseHash(const std::string &name, uint64_t &h) {
    /* "<16 hex digits>" up to the first dot (RT64's auto-path rule) */
    const std::string s = name.substr(0, name.find('.'));
    if (s.size() != 16) return false;
    for (char c : s)
        if (!isxdigit((unsigned char) c)) return false;
    h = strtoull(s.c_str(), nullptr, 16);
    return true;
}

const char *kHeader = "hash,file,format,tlut,width,height,alpha,first_seen,first_frame,uploads";

void readIndex() {
    std::ifstream in(fs::path(wide(g_dir + "index.csv")));
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        std::vector<std::string> f;
        std::stringstream ss(line);
        std::string x;
        while (std::getline(ss, x, ',')) f.push_back(x);
        uint64_t h;
        if (f.size() < 10 || !parseHash(f[0], h)) continue;
        Row r;
        r.file = f[1];
        r.format = f[2];
        r.tlut = f[3];
        r.width = unsigned(atoi(f[4].c_str()));
        r.height = unsigned(atoi(f[5].c_str()));
        r.alpha = f[6];
        r.where = f[7];
        r.frame = unsigned(atoi(f[8].c_str()));
        r.uploads = strtoull(f[9].c_str(), nullptr, 10);
        g_rows[h] = r;
    }
}

/* (g_mutex held) */
bool writeIndex() {
    std::vector<std::pair<uint64_t, const Row *>> v;
    for (const auto &r : g_rows) v.push_back({r.first, &r.second});
    std::sort(v.begin(), v.end(), [](const auto &a, const auto &b) {
        if (a.second->where != b.second->where) return a.second->where < b.second->where;
        if (a.second->frame != b.second->frame) return a.second->frame < b.second->frame;
        return a.first < b.first;
    });
    const std::string tmp = g_dir + "index.csv.tmp";
    {
        std::ofstream out(fs::path(wide(tmp)), std::ios::binary);
        if (!out) return false;
        out << kHeader << "\r\n";
        for (const auto &e : v) {
            const Row &r = *e.second;
            out << hex16(e.first) << ',' << r.file << ',' << r.format << ',' << r.tlut << ',' << r.width << ','
                << r.height << ',' << r.alpha << ',' << r.where << ',' << r.frame << ',' << r.uploads << "\r\n";
        }
        if (!out) return false;
    }
    return MoveFileExW(wide(tmp).c_str(), wide(g_dir + "index.csv").c_str(), MOVEFILE_REPLACE_EXISTING) != 0;
}

std::string alphaClass(const std::vector<uint8_t> &px) {
    bool partial = false, zero = false;
    for (size_t i = 3; i < px.size(); i += 4) {
        if (px[i] == 0) zero = true;
        else if (px[i] != 255) partial = true;
    }
    return partial ? "translucent" : zero ? "binary" : "opaque";
}

void workerLoop() {
    auto lastIndex = std::chrono::steady_clock::now();
    std::unique_lock<std::mutex> lock(g_mutex);
    for (;;) {
        g_cv.wait_for(lock, std::chrono::milliseconds(500), [] { return g_stopping || !g_jobs.empty(); });
        while (!g_jobs.empty()) {
            Job j = std::move(g_jobs.front());
            g_jobs.pop_front();
            lock.unlock();
            std::vector<uint8_t> px(size_t(j.width) * j.height * 4);
            tmem::decode(j.bytes.data(), j.tile, j.width, j.height, j.tlut, px.data());
            const std::string alpha = alphaClass(px);
            size_t len = 0;
            void *png = tdefl_write_image_to_png_file_in_memory_ex(px.data(), int(j.width), int(j.height), 4, &len,
                                                                    MZ_DEFAULT_LEVEL, MZ_FALSE);
            const std::string rel = j.where + "/" + hex16(j.hash) + ".png";
            bool ok = false;
            if (png != nullptr) {
                std::error_code ec;
                fs::create_directories(fs::path(wide(g_dir + j.where)), ec);
                std::ofstream out(fs::path(wide(g_dir + j.where + "\\" + hex16(j.hash) + ".png")), std::ios::binary);
                out.write(static_cast<const char *>(png), std::streamsize(len));
                ok = bool(out);
                mz_free(png);
            }
            lock.lock();
            Row &r = g_rows[j.hash];
            r.alpha = alpha;
            if (ok) {
                r.file = rel;
                g_onDisk[j.hash] = rel;
                g_written++;
                if (g_written % 100 == 0) host_log("live: texture dump: %u textures written\n", g_written);
            } else if (g_failed++ == 0) {
                host_log("live: texture dump: could not write %s%s\n", g_dir.c_str(), rel.c_str());
            }
            g_dirty = true;
        }
        const auto now = std::chrono::steady_clock::now();
        if (g_dirty && (g_stopping || now - lastIndex > std::chrono::seconds(2))) {
            if (!writeIndex()) host_log("live: texture dump: could not write %sindex.csv\n", g_dir.c_str());
            g_dirty = false;
            lastIndex = now;
        }
        if (g_stopping && g_jobs.empty()) return;
    }
}

}  // namespace

bool texDumpStart(const std::string &dirIn, std::string &why) {
    if (g_on) texDumpStop();
    std::string dir = dirIn;
    if (!dir.empty() && dir.back() != '\\' && dir.back() != '/') dir += '\\';
    std::error_code ec;
    fs::create_directories(fs::path(wide(dir)), ec);
    if (!fs::is_directory(fs::path(wide(dir)), ec)) {
        why = "can't make the folder " + dir;
        host_log("live: texture dump: %s\n", why.c_str());
        return false;
    }
    std::lock_guard<std::mutex> lock(g_mutex);
    g_dir = dir;
    g_rows.clear();
    g_onDisk.clear();
    g_jobs.clear();
    g_written = g_failed = 0;
    g_dirty = g_stopping = false;
    readIndex();
    /* every <hash>.png already below the folder (moved or sorted by hand too) */
    for (auto it = fs::recursive_directory_iterator(fs::path(wide(dir)), ec); !ec && it != fs::recursive_directory_iterator();
         it.increment(ec)) {
        if (!it->is_regular_file(ec)) continue;
        const fs::path &p = it->path();
        std::string ext = utf8(p.extension().wstring());
        for (char &c : ext) c = char(tolower((unsigned char) c));
        uint64_t h;
        if (ext != ".png" || !parseHash(utf8(p.filename().wstring()), h)) continue;
        std::string rel = utf8(fs::relative(p, fs::path(wide(dir)), ec).generic_wstring());
        g_onDisk[h] = rel;
        auto r = g_rows.find(h);
        if (r != g_rows.end()) r->second.file = rel;
    }
    g_seen.clear();
    g_lastVersion = ~0u;
    g_session.clear();
    g_raw = 0;
    g_on = true;
    g_worker = std::thread(workerLoop);
    host_log("live: texture dump on: %s (%u textures there already)\n", dir.c_str(), unsigned(g_onDisk.size()));
    return true;
}

void texDumpStop() {
    if (!g_on) return;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        g_stopping = true;
        g_dirty = true;   /* the upload counts */
    }
    g_cv.notify_all();
    if (g_worker.joinable()) g_worker.join();
    g_on = false;
    host_log("live: texture dump off: %u new textures written (%u found this session, %u raw-TMEM ones not "
             "dumpable), %u in %s, index.csv\n",
             g_written, unsigned(g_session.size()), g_raw, unsigned(g_onDisk.size()), g_dir.c_str());
}

bool texDumpOn() {
    return g_on;
}

void texDumpPoll(RT64::TextureCache *cache, unsigned frame) {
    if (!g_on || cache == nullptr) return;
    const unsigned long long mode = BC_MODE;
    /* in a level (in play, its start and intro (0x800), the map views, its text windows, an attract
     * demo): level_NN; the rest is the front end */
    const bool level = mode == 0x4 || mode == 0x2000 || mode == 0x800 || mode == 0x100 || mode == 0x2 ||
                       mode == 0x2000000000000000ull;
    char where[16];
    if (level) snprintf(where, sizeof where, "level_%02d", BC_LEVEL);
    else snprintf(where, sizeof where, "frontend");
    std::vector<Job> jobs;
    std::vector<uint64_t> uploads;
    {
        std::lock_guard<std::mutex> lock(cache->textureMapMutex);
        const RT64::TextureMap &m = cache->textureMap;
        if (m.globalVersion == g_lastVersion) return;
        g_lastVersion = m.globalVersion;
        if (g_seen.size() < m.textures.size()) g_seen.resize(m.textures.size(), nullptr);
        for (size_t i = 0; i < m.textures.size(); i++) {
            const RT64::Texture *t = m.textures[i];
            if (t == nullptr || t == g_seen[i]) continue;
            g_seen[i] = t;
            const uint64_t h = m.hashes[i];
            uploads.push_back(h);
            if (!g_session.insert(h).second) continue;
            Job j;
            j.hash = h;
            j.raw = !t->decodeTMEM || t->bytesTMEM.size() < 0x1000 || t->width == 0 || t->height == 0 ||
                    t->width > 4096 || t->height > 4096;
            if (j.raw) g_raw++;
            j.tile.fmt = t->loadTile.fmt;
            j.tile.siz = t->loadTile.siz;
            j.tile.tmem = t->loadTile.tmem;
            j.tile.line = t->loadTile.line;
            j.tile.palette = t->loadTile.palette;
            j.width = t->width;
            j.height = t->height;
            j.tlut = t->tlut;
            j.where = where;
            if (!j.raw) memcpy(j.bytes.data(), t->bytesTMEM.data(), 0x1000);
            jobs.push_back(std::move(j));
        }
    }
    if (uploads.empty()) return;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        for (uint64_t h : uploads) g_rows[h].uploads++;
        for (Job &j : jobs) {
            Row &r = g_rows[j.hash];
            if (r.where.empty()) {
                r.where = j.where;
                r.frame = frame;
                r.format = j.raw ? "raw" : tmem::formatName(j.tile.fmt, j.tile.siz);
                r.tlut = j.raw ? "-" : tmem::tlutName(j.tlut);
                r.width = j.width;
                r.height = j.height;
                if (j.raw) r.alpha = "-";
            }
            if (j.raw) continue;   /* RT64 samples its TMEM directly: no picture to dump */
            auto d = g_onDisk.find(j.hash);
            if (d != g_onDisk.end()) {
                r.file = d->second;   /* there already: not written again */
                continue;
            }
            g_jobs.push_back(std::move(j));
        }
        g_dirty = true;
    }
    g_cv.notify_all();
}

std::string texDumpStatus() {
    std::lock_guard<std::mutex> lock(g_mutex);
    char b[160];
    snprintf(b, sizeof b, "%u new this session, %u in the folder%s", g_written, unsigned(g_onDisk.size()),
             g_jobs.empty() ? "" : (", " + std::to_string(g_jobs.size()) + " to write").c_str());
    return b;
}

}  // namespace live
