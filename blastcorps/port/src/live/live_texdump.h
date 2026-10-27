/* bc.exe's texture dump (live_texdump.cpp): while it is on, every texture
 * RT64 decodes is written once as <folder>\<where>\<hash>.png, the RGBA RT64
 * itself sees (tmem_decode.cpp), named by RT64's replacement hash (version 5)
 * so a file maps 1:1 to a texture-pack entry; <where> is frontend or
 * level_NN (where it was first seen).  <folder>\index.csv lists every texture
 * found (hash, file, format, size, alpha, where and when first seen, how
 * often RT64 loaded it).  Read-only towards the game and RT64: the main
 * thread copies a new texture's TMEM from RT64's cache, a worker thread
 * decodes it and writes the files. */
#ifndef LIVE_TEXDUMP_H
#define LIVE_TEXDUMP_H

#include <string>

namespace RT64 {
struct TextureCache;
}

namespace live {

/* start dumping into `dir` (absolute; created if missing).  Textures whose
 * <hash>.png is already anywhere below it are not written again, and an
 * index.csv already there is kept and added to.  False (and `why`) if the
 * folder can't be made. */
bool texDumpStart(const std::string &dir, std::string &why);
/* stop: the files queued are still written, then the index */
void texDumpStop();
bool texDumpOn();
/* main thread, every retrace while on: find the textures RT64 has loaded
 * since the last call (frame = the game's frame count) */
void texDumpPoll(RT64::TextureCache *cache, unsigned frame);
/* "312 new this session, 1204 in the folder, 3 to write" */
std::string texDumpStatus();

}  // namespace live

#endif
