/* Edited level files in the port (port_mods.c): mods (.bcmod, level_mod.h)
 * applied in memory to the level files read from the player's ROM, and the
 * level editor's edits (bc.exe's viewer).  The game reads an edited file
 * instead of the ROM's through one hook around lvl_load_file's hd_rom_load
 * (game/port.h PORT_LEVEL_PRE / PORT_LEVEL_POST); the ROM itself, on disk
 * and in memory, is never changed.  With no mod and no edit, nothing here
 * runs: the game reads the ROM's files exactly as before. */
#ifndef PORT_MODS_H
#define PORT_MODS_H

#include <stddef.h>
#include <stdint.h>

#include "load/level_mod.h"

#ifdef __cplusplus
extern "C" {
#endif

/* log lines go here (default: stderr; the exes point it at their log) */
extern void (*port_mods_log_hook)(const char *fmt, ...);

/* load a .bcmod (UTF-8 path): every level it changes is checked against the ROM's file (its SHA-1) and built;
 * from then on the game reads the edited files.  0 ok, -1 and *err (nothing of the mod is used) */
int port_mods_add(const char *path, char *err, size_t errlen);
int port_mods_count(void);
const char *port_mods_name(int i);   /* the mod's name (or its file name) */

/* the ROM's level file, inflated (both gzip members, big-endian), cached; 0 ok */
int port_level_original(int level, const uint8_t **file, size_t *size);
/* the edited file the game reads for this level (a mod's or the editor's); 0 if there is one */
int port_level_edited(int level, const uint8_t **file, size_t *size);
/* put an edited file in place for the level's next load (copied; NULL: the ROM's again); source: for the log */
int port_level_set_edit(int level, const uint8_t *file, size_t size, const char *source);
/* the changes the loaded mods make to a level, in order (the editor starts from them); 0 if any */
int port_level_mod_changes(int level, const LmChange **changes, int *n, const char **modName);
/* every object placement's type has a building in the ROM's object table; 0 ok, -1 and *err */
int port_level_check_objects(const uint8_t *file, size_t size, char *err, size_t errlen);
/* (testing: bc_headless --mod-out) every edited file to DIR\level_NN.bin; the count written, -1 on an error */
int port_mods_write_levels(const char *dir);
/* (testing: --level-dir) every DIR\level_NN.bin there is becomes that level's edited file, used as it is (e.g.
 * bcfiles geo-rebuild's levels); the count read, -1 and *err on an error */
int port_mods_read_levels(const char *dir, char *err, size_t errlen);
/* the geometry edit the loaded mod makes to a level (NULL: none) */
const LgEdit *port_level_mod_geo(int level);

/* ---- texture slots (bcmod/2 "textures", the editor's imports) ----
 * A slot's image is packed the slot's way (tex_codec.h) when the mod loads; from then on the game's DMAs of the
 * slot's ROM bytes and of its table entry (the packed size) read the new ones (port_rom_overlay, called by
 * osPiStartDma), as they would from a ROM that held them (bcfiles mod-export writes the same bytes).  With none,
 * nothing changes. */
extern int port_rom_patches;                 /* how many byte ranges are patched (0: none) */
void port_rom_overlay(void *dst, uint32_t off, uint32_t n);
/* A slot can grow past its room (the bytes to the next entry's offset), up to what the texture unpacks to: it takes
 * its packed bytes rounded up to 8, as bcfiles mod-export's shift path lays it out (the ROM file grows, the table's
 * later offsets follow).  The game, whose table here keeps every offset, asks for the old room: port_dma_len hands
 * that DMA the grown length (this; `len` otherwise) */
uint32_t port_rom_grown(uint32_t off, uint32_t asked, uint32_t len);
/* the textures of the loaded mods (the editor starts from them) */
const LmTexture *port_mods_textures(int *n, const char **modName);
/* a slot of the player's ROM: its table entry and the SHA-1 of its packed bytes; 0 ok */
int port_tex_slot(int id, uint32_t *offset, unsigned *size, unsigned *packing, uint32_t *room, char sha1[41]);
/* a slot's texels as the ROM holds them (malloc'd); 0 ok */
int port_tex_texels(int id, uint8_t **texels, size_t *n);
/* a mod's texture entry checked and packed (the slot's SHA-1, the PNG, its room); 0 ok (*packed malloc'd) */
int port_tex_pack(const LmTexture *t, uint8_t **packed, size_t *n, char *err, size_t errlen);
/* the editor's texture slots for the game's next loads (replacing the mods' for the same ids; n 0: none);
 * all or nothing: 0 ok, -1 and *err */
int port_tex_editor_set(const LmTexture *t, int n, char *err, size_t errlen);
/* a free id for a new slot of the port's own ("new": an empty table entry with an empty one after it, not used by
 * a loaded slot nor in `taken`); -1: none.  bc.exe puts its bytes past the ROM's end; bcfiles mod-export puts them
 * in the ROM's free space at its end (or a file of their own on the shift path) and points the entry there */
int port_tex_new_id(const int *taken, int ntaken);

#ifdef __cplusplus
}
#endif

#endif
