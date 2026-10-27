/* Level-record edits and mod files (level_mod.c): plain C, no RDRAM, no
 * renderer, so bc_cameratest (edit_test.cpp) tests it on synthetic level
 * images and bc_headless, bc.exe and bc.exe's editor share it.
 *
 * A level file here is the inflated file as the ROM has it (both gzip
 * members back to back, big-endian: the bytes hd_rom_load writes before the
 * port swaps them, docs/systems/levels.md).  An LmLevel holds it as a chain
 * of sections; the editable records (the record kinds below) can be read,
 * changed, added (a copy of another record of the kind) and deleted, and
 * lm_build lays the file out again the way tools_port/bcfiles level.py
 * writes it: every section in the header's order at its alignment, the
 * zero padding recomputed, the display-list block on 16 bytes with the
 * visibility records' absolute offsets moved with it.  With no change the
 * build is the original file byte for byte.
 *
 * A mod file (.bcmod) is JSON:
 *
 *   {"format": "bcmod/1", "name": "...", "description": "...",
 *    "levels": [{"level": 1,
 *                "original_sha1": "<SHA-1 of the level file as the ROM has it>",
 *                "edited_sha1": "<SHA-1 of the file after the changes>",
 *                "changes": [
 *                  {"op": "set", "kind": "object", "index": 0, "fields": {"x": 1880, "z": 1880}},
 *                  {"op": "add", "kind": "rdu", "index": 100, "copy_of": 99, "fields": {"x": 2000}},
 *                  {"op": "delete", "kind": "tnt", "index": 3}]}]}
 *
 * "bcmod/2" files (tools_port/bcfiles mod.py) may also carry, per level,
 * "geometry": an edit of the level's triangles, applied after the changes with
 * every derived section regenerated (level_geo.h); bcmod/1 files load as
 * before.
 *
 * Changes apply in order; an index counts the records as they are after the
 * changes before it.  "add" inserts a copy of record copy_of (counted before
 * the insert) at index and then sets its fields, so a mod holds no ROM bytes
 * beyond the field values it sets.  original_sha1 refuses a mod made for
 * another ROM's level; edited_sha1 (optional) checks that the result is the
 * one its maker saw (this engine and tools_port/bcfiles mod.py build the same
 * bytes). */
#ifndef LEVEL_MOD_H
#define LEVEL_MOD_H

#include <stddef.h>
#include <stdint.h>

#include "level_geo.h"
#include "sha1.h"   /* lm_sha1, lm_sha1_hex */

#ifdef __cplusplus
extern "C" {
#endif

/* the editable record kinds (the viewer's RecKind numbers and recKindKey names) */
enum {
    LM_VEHICLE,   /* 0x50 vehicle spawn: type, x, y, z, heading */
    LM_CARRIER,   /* 0x54 the carrier record: speed, x, z, heading, limit (one; no add or delete) */
    LM_OBJECT,    /* 0x5C object placement: x, y, z, type, path, people, motion, tag */
    LM_RDU,       /* 0x34: x, y, z */
    LM_TNT,       /* 0x38: x, y, z, type, timer, unk8, unkA */
    LM_CRATE,     /* 0x20 ammo crate: x, y, z, type */
    LM_COMMPT,    /* 0x28 communication point: x, y, z, w */
    LM_BLOCK,     /* 0x3C push block: x, y, z, model */
    LM_SLOT,      /* 0x3C push-block slot: x, y, z, model, target (its triangles move with x, y, z) */
    LM_KINDS
};
#define LM_MAX_FIELDS 8

const char *lm_kind_key(int kind);               /* "vehicle" ... ("" for a bad kind) */
int lm_kind_from_key(const char *key);           /* -1 if unknown */
int lm_field_count(int kind);
const char *lm_field_name(int kind, int field);
int lm_field_index(int kind, const char *name);  /* -1 if the kind has no such field */
void lm_field_range(int kind, int field, long *lo, long *hi);
/* the most records of a kind the game's arrays hold (the counts the level's set-up writes into fixed arrays) */
int lm_kind_max(int kind);

typedef struct LmLevel LmLevel;

/* a level file (copied); NULL and *err when its header chain doesn't parse */
LmLevel *lm_open(const uint8_t *file, size_t size, char *err, size_t errlen);
void lm_free(LmLevel *lv);
LmLevel *lm_clone(const LmLevel *lv);
int lm_count(const LmLevel *lv, int kind);
/* 0 ok, -1 no such record or field */
int lm_get(const LmLevel *lv, int kind, int index, int field, long *value);
int lm_set(LmLevel *lv, int kind, int index, int field, long value, char *err, size_t errlen);
int lm_insert_copy(LmLevel *lv, int kind, int at, int from, char *err, size_t errlen);
int lm_delete(LmLevel *lv, int kind, int index, char *err, size_t errlen);
/* the file laid out again (malloc'd; *split: the first gzip member's length, header word 0x78) */
uint8_t *lm_build(const LmLevel *lv, size_t *size, uint32_t *split);
/* the rules the game's set-up relies on: one driver spawn, known vehicle types, the arrays' sizes.
 * 0 ok, -1 and *err */
int lm_check(const LmLevel *lv, char *err, size_t errlen);
/* (level_geo.c) a section kept as bytes replaced (0x30, 0x48, 0x4C, 0x6C, 0x70, 0xA0..0xBC); 0 ok */
int lm_set_raw_section(LmLevel *lv, unsigned slot, const uint8_t *p, size_t n);
/* (level_geo.c) the segment-8 vertices and the display-list block replaced; rel: the header's display-list words
 * (0x7C, 0x80, 0x84, 0x88 .. 0x9C) relative to the block; the visibility records and groups (sections 0xA0..0xBC)
 * must hold offsets as if the block were at lm_dl_base (lm_build moves them with it) */
int lm_set_display(LmLevel *lv, const uint8_t *vtx, size_t nvtx, const uint8_t *dl, size_t ndl, const uint32_t rel[9]);
uint32_t lm_dl_base(const LmLevel *lv);

/* ---- changes and mods ---- */
enum { LM_OP_SET, LM_OP_ADD, LM_OP_DELETE };
typedef struct {
    int op, kind, index, copy_of;
    int nfields;
    int field[LM_MAX_FIELDS];
    long value[LM_MAX_FIELDS];
} LmChange;
/* one change; 0 ok, -1 and *err (the level is left as it was) */
int lm_apply(LmLevel *lv, const LmChange *c, char *err, size_t errlen);
/* "set object 3: x 1880, z 1880" */
void lm_describe(const LmChange *c, char *out, size_t outlen);

typedef struct {
    int level;
    char original_sha1[41], edited_sha1[41];   /* "" = not given */
    int nchanges;
    LmChange *changes;
    LgEdit *geo;                               /* bcmod/2 "geometry" (NULL: none; level_geo.h) */
} LmModLevel;
/* a bcmod/2 texture slot: {"id", "fmt", "siz", "png" (base64), "slot_sha1"[, "mips": n][, "new": true]}
 * (tex_codec.h applies it: the image packed the slot's way, at load time) */
typedef struct {
    int id;
    int fmt, siz;                              /* the texel format the level lists use (siz in bits); -1: not given */
    int mips;                                  /* "mips": the levels of detail the lists draw it with, made from the
                                                  image (0: not given: the slot's own smaller levels kept) */
    int hasPalette, palette;                   /* "palette": a palette packing's (4, 5) palette entry, kept: the
                                                  picture takes its colours (hasPalette 0: none) */
    int isNew;                                 /* "new": a slot of the port's own (bc.exe only; port_mods.h) */
    int from;                                  /* a new slot: the slot whose texels after the image's it keeps ("from"; -1) */
    char slot_sha1[41];                        /* SHA-1 of the slot's packed bytes in the ROM it was made for */
    char *png;                                 /* the image, base64 as in the file (malloc'd) */
} LmTexture;
typedef struct {
    char name[128];
    char description[512];
    int nlevels;
    LmModLevel *levels;
    int format;                                /* 1 or 2 */
    int ntextures;                             /* bcmod/2 "textures" */
    LmTexture *textures;
} LmMod;

/* parse a .bcmod's text; 0 ok, -1 and *err (*m then holds nothing) */
int lm_mod_parse(const char *text, size_t len, LmMod *m, char *err, size_t errlen);
void lm_mod_free(LmMod *m);
/* the mod as .bcmod text (malloc'd, NUL-terminated): bcmod/2 when a level has geometry or there are textures */
char *lm_mod_write(const LmMod *m);
/* a copy of a geometry edit (malloc'd arrays; free with lg_edit_free); 0 ok */
int lm_geo_copy(LgEdit *dst, const LgEdit *src);
/* one level of a mod over the original file: the hash checks, every change, lm_check, the build.
 * 0 ok (*out malloc'd), -1 and *err */
int lm_mod_apply(const LmModLevel *ml, const uint8_t *orig, size_t size, uint8_t **out, size_t *outsize,
                 char *err, size_t errlen);

#ifdef __cplusplus
}
#endif

#endif
