/* Edited level files in the port (see port_mods.h). */
#include <windows.h>

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "rdram.h"
#include "load/port_load.h"
#include "load/port_mods.h"
#include "load/tex_codec.h"
#include "load/host_util.h"

static void rebuild_patches(void);

static void log_stderr(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
}
void (*port_mods_log_hook)(const char *fmt, ...) = log_stderr;
#define LOG port_mods_log_hook

/* The level files' ROM starts (lvl_load_file's romLagpRaw .. romLevel59Raw, tools_port/bcfiles romfs.py
 * LEVEL_ROM; US 1.0 and 1.1 share them): each is two gzip members back to back. */
static const uint32_t kLevelRom[60] = {
    0x4ACC10, 0x4A5660, 0x4B8960, 0x4BFD60, 0x4C3AC0, 0x4D5F90, 0x4E2F70, 0x4E4E80, 0x4E7C00, 0x4E8F70,
    0x4F5C10, 0x500520, 0x507E80, 0x511340, 0x523080, 0x52CD00, 0x532700, 0x53E9B0, 0x54A820, 0x552DE0,
    0x555000, 0x560E90, 0x5652D0, 0x56F3F0, 0x5721E0, 0x5736E0, 0x57A2C0, 0x580B60, 0x588CE0, 0x58BE80,
    0x597B80, 0x59B7D0, 0x5A5840, 0x5B0B10, 0x5B5A30, 0x5B8BB0, 0x5C4C80, 0x5CA9C0, 0x5CCF50, 0x5D1060,
    0x5DC830, 0x5E6EE0, 0x5EC800, 0x5F3A80, 0x6014B0, 0x60A710, 0x613AA0, 0x61DD70, 0x621AF0, 0x6269E0,
    0x630C30, 0x635700, 0x63CA10, 0x641F30, 0x644810, 0x646080, 0x647550, 0x654FC0, 0x660950, 0x665F80};
/* ROM_OBJ_TABLE (rdram.h): 512 BE u32 offsets of the buildings; an unused entry: the end */
#define MAX_LEVEL 0x100000u       /* (the largest inflated level is about 0x50000 bytes) */
#define MAX_GROWTH 0x10000u       /* an edited file may be at most this much larger than the ROM's */

typedef struct {
    uint8_t *orig;
    size_t origSize;
    uint8_t *edit;
    size_t editSize;
    char source[160];
    LmChange *modChanges;   /* the mod's changes (port_level_mod_changes) */
    int nModChanges;
    LgEdit *modGeo;         /* the mod's geometry (port_level_mod_geo) */
    char modName[128];
} LevelState;
static LevelState g_lv[60];

typedef struct {
    char name[128];
} ModInfo;
static ModInfo *g_mods;
static int g_nmods;

/* texture slots: the mods' and the editor's, packed; the patched ROM ranges built from them */
typedef struct {
    LmTexture t;            /* (png copied) */
    uint8_t *packed;        /* the packed bytes, zeros to the slot's room, then the entry's new size (tex_store) */
    size_t n;
    int editor;
    uint8_t entry[8], term[4];   /* a new slot: its table entry, the next entry's offset (rebuild_patches) */
} TexSlot;
/* The port's own texture slots ("new": bc.exe only): an empty table entry (no bytes in the ROM; 457 of them, in runs)
 * gets its texture at a ROM address past the 8 MB ROM, in the cartridge's address range, as a larger cartridge
 * holding it there would have it: the entry's offset, size and packing (stored), and the next entry's offset (where
 * the DMA ends; that entry is empty too) are patched in the table the game DMAs, the texels at that address. */
#define NEW_SLOT_BASE 0x02000000u
static TexSlot *g_tex;
static int g_ntex;
static char g_texModName[128];
typedef struct {
    uint32_t rom, n;
    const uint8_t *bytes;
    uint32_t want;   /* a grown slot (n more than the game asks for, `want`): only a DMA of the slot itself, whole */
} RomPatch;
/* A slot whose picture packs to more than its room (the bytes up to the next entry's offset) takes more: the packed
 * bytes rounded up to 8 (as bcfiles mod-export's shift path lays them out: the texture's ROM file grows, the ones after
 * it move and the table follows).  The game asks for the room it had (its table here keeps every offset): that DMA
 * gets the whole grown slot (port_rom_grown); the packed bytes may not be more than the texture unpacks to (what any
 * buffer it is loaded into holds) */
static uint32_t grown_room(size_t n, uint32_t room) {
    return n > room ? (uint32_t) ((n + 7) & ~(size_t) 7) : room;
}
static RomPatch *g_patch;
int port_rom_patches;

/* the hook's snapshot of what lies under the ROM file's bytes (PORT_LEVEL_PRE) */
static struct {
    int level;
    uint32_t dest, len;
    uint8_t *bytes;
    void *units;
} g_snap = {-1, 0, 0, NULL, NULL};

int port_level_original(int level, const uint8_t **file, size_t *size) {
    LevelState *s;
    size_t romSize = 0;
    uint8_t *rom = rom_bytes(&romSize);
    if (level < 0 || level >= 60 || rom == NULL) return -1;
    s = &g_lv[level];
    if (s->orig == NULL) {
        uint8_t *buf = (uint8_t *) malloc(MAX_LEVEL);
        uint32_t next = 0, next2 = 0;
        long a, b;
        if (buf == NULL) return -1;
        /* (a ROM from the shiftable build: where it has the file, rdram.h rom_phys) */
        a = rom_gunzip_next(rom_phys(kLevelRom[level]), buf, MAX_LEVEL, &next);
        b = a > 0 ? rom_gunzip_next(next, buf + a, MAX_LEVEL - (size_t) a, &next2) : -1;
        if (a <= 0 || b <= 0) {
            LOG("mods: level %d: the ROM has no level file at 0x%06X (a ROM with its files moved, without the "
                "shiftable build's ROM map?)\n", level, (unsigned) rom_phys(kLevelRom[level]));
            free(buf);
            return -1;
        }
        s->origSize = (size_t) (a + b);
        s->orig = (uint8_t *) realloc(buf, s->origSize);
        if (s->orig == NULL) s->orig = buf;
    }
    if (file) *file = s->orig;
    if (size) *size = s->origSize;
    return 0;
}

int port_level_edited(int level, const uint8_t **file, size_t *size) {
    if (level < 0 || level >= 60 || g_lv[level].edit == NULL) return 0;
    if (file) *file = g_lv[level].edit;
    if (size) *size = g_lv[level].editSize;
    return 1;
}

int port_level_set_edit(int level, const uint8_t *file, size_t size, const char *source) {
    LevelState *s;
    const uint8_t *orig;
    size_t origSize;
    if (level < 0 || level >= 60 || port_level_original(level, &orig, &origSize)) return -1;
    s = &g_lv[level];
    free(s->edit);
    s->edit = NULL;
    s->editSize = 0;
    s->source[0] = 0;
    if (file == NULL) {
        LOG("mods: level %d: the ROM's file again\n", level);
        return 0;
    }
    if (size > origSize + MAX_GROWTH) {
        LOG("mods: level %d: the edited file is 0x%X bytes, more than 0x%X over the ROM's: not used\n", level,
            (unsigned) size, MAX_GROWTH);
        return -1;
    }
    s->edit = (uint8_t *) malloc(size ? size : 1);
    if (s->edit == NULL) return -1;
    memcpy(s->edit, file, size);
    s->editSize = size;
    snprintf(s->source, sizeof s->source, "%s", source ? source : "an edit");
    {
        char sha[41];
        lm_sha1_hex(file, size, sha);
        LOG("mods: level %d: the game reads an edited file from its next load (%s; 0x%X bytes, the ROM's 0x%X; "
            "SHA-1 %s)\n", level, s->source, (unsigned) size, (unsigned) origSize, sha);
    }
    return 0;
}

const LgEdit *port_level_mod_geo(int level) {
    return level >= 0 && level < 60 ? g_lv[level].modGeo : NULL;
}

int port_level_mod_changes(int level, const LmChange **changes, int *n, const char **modName) {
    if (level < 0 || level >= 60 || g_lv[level].modChanges == NULL) return 0;
    if (changes) *changes = g_lv[level].modChanges;
    if (n) *n = g_lv[level].nModChanges;
    if (modName) *modName = g_lv[level].modName;
    return 1;
}

int port_level_check_objects(const uint8_t *file, size_t size, char *err, size_t errlen) {
    size_t romSize = 0;
    const uint8_t *rom = rom_bytes(&romSize);
    LmLevel *lv;
    uint32_t tab[512], end = 0, at = rom_phys(ROM_OBJ_TABLE);
    int i, n, r = 0;
    if (rom == NULL || romSize < at + 0x800) return 0;
    for (i = 0; i < 512; i++) {
        const uint8_t *p = rom + at + 4 * i;
        tab[i] = (uint32_t) p[0] << 24 | (uint32_t) p[1] << 16 | (uint32_t) p[2] << 8 | p[3];
        if (tab[i] > end) end = tab[i];
    }
    lv = lm_open(file, size, err, errlen);
    if (lv == NULL) return -1;
    n = lm_count(lv, LM_OBJECT);
    for (i = 0; i < n && r == 0; i++) {
        long t;
        lm_get(lv, LM_OBJECT, i, lm_field_index(LM_OBJECT, "type"), &t);
        if (t < 0 || t >= 512 || tab[t] >= end) {
            if (err) snprintf(err, errlen, "object %d: type 0x%lX has no building in the ROM's object table", i, t);
            r = -1;
        }
    }
    lm_free(lv);
    return r;
}

/* ---- mods ---- */
static FILE *open_utf8(const char *path, const char *mode) {
    wchar_t wp[4096], wm[8];
    if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path, -1, wp, 4096) > 0 &&
        MultiByteToWideChar(CP_UTF8, 0, mode, -1, wm, 8) > 0)
        return _wfopen(wp, wm);
    return fopen(path, mode);
}

static char *read_text(const char *path, size_t *len) {
    FILE *f = open_utf8(path, "rb");
    long n;
    char *p;
    if (f == NULL) return NULL;
    fseek(f, 0, SEEK_END);
    n = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (n < 0 || n > 0x1000000 || (p = (char *) malloc((size_t) n + 1)) == NULL) {
        fclose(f);
        return NULL;
    }
    if (fread(p, 1, (size_t) n, f) != (size_t) n) {
        fclose(f);
        free(p);
        return NULL;
    }
    fclose(f);
    p[n] = 0;
    *len = (size_t) n;
    return p;
}

int port_mods_add(const char *path, char *err, size_t errlen) {
    LmMod m;
    size_t len = 0;
    char *text = read_text(path, &len), e[512], name[128];
    uint8_t *out[60];
    size_t outSize[60];
    uint8_t **packed = NULL;
    size_t *packedN = NULL;
    int i, k;
    if (text == NULL) {
        snprintf(err, errlen, "can't read %s", path);
        return -1;
    }
    if (lm_mod_parse(text, len, &m, e, sizeof e)) {
        free(text);
        snprintf(err, errlen, "%s: %s", path, e);
        return -1;
    }
    free(text);
    if (m.name[0]) {
        snprintf(name, sizeof name, "%s", m.name);
    } else {
        const char *b = path + strlen(path);
        while (b > path && b[-1] != '\\' && b[-1] != '/') b--;
        snprintf(name, sizeof name, "%s", b);
    }
    memset(out, 0, sizeof out);
    for (i = 0; i < m.nlevels; i++) {
        const LmModLevel *ml = &m.levels[i];
        const uint8_t *orig;
        size_t origSize;
        if (port_level_original(ml->level, &orig, &origSize)) {
            snprintf(err, errlen, "%s: level %d: the ROM's level file can't be read", name, ml->level);
            goto fail;
        }
        if (g_lv[ml->level].edit != NULL) {
            snprintf(err, errlen, "%s: level %d is already changed by %s (two mods can't change the same level)",
                     name, ml->level, g_lv[ml->level].source);
            goto fail;
        }
        if (lm_mod_apply(ml, orig, origSize, &out[ml->level], &outSize[ml->level], e, sizeof e)) {
            snprintf(err, errlen, "%s: %s", name, e);
            goto fail;
        }
        if (port_level_check_objects(out[ml->level], outSize[ml->level], e, sizeof e)) {
            snprintf(err, errlen, "%s: level %d: %s", name, ml->level, e);
            goto fail;
        }
        if (outSize[ml->level] > origSize + MAX_GROWTH) {
            snprintf(err, errlen, "%s: level %d grows by more than 0x%X bytes", name, ml->level, MAX_GROWTH);
            goto fail;
        }
    }
    /* the texture slots, packed (all or nothing, with the levels) */
    if (m.ntextures > 0) {
        packed = (uint8_t **) calloc((size_t) m.ntextures, sizeof(uint8_t *));
        packedN = (size_t *) calloc((size_t) m.ntextures, sizeof(size_t));
        if (packed == NULL || packedN == NULL) {
            snprintf(err, errlen, "out of memory");
            goto fail;
        }
        for (k = 0; k < m.ntextures; k++) {
            int j;
            for (j = 0; j < g_ntex; j++)
                if (g_tex[j].t.id == m.textures[k].id && !g_tex[j].editor) {
                    snprintf(err, errlen, "%s: texture %d is already changed by %s (two mods can't change the same "
                             "texture)", name, m.textures[k].id, g_texModName);
                    goto fail;
                }
            if (port_tex_pack(&m.textures[k], &packed[k], &packedN[k], e, sizeof e)) {
                snprintf(err, errlen, "%s: %s", name, e);
                goto fail;
            }
        }
    }
    /* all good: in place */
    for (i = 0; i < m.nlevels; i++) {
        const LmModLevel *ml = &m.levels[i];
        LevelState *s = &g_lv[ml->level];
        char src[200];
        snprintf(src, sizeof src, "mod %s", name);
        port_level_set_edit(ml->level, out[ml->level], outSize[ml->level], src);
        free(s->modChanges);
        s->modChanges = (LmChange *) malloc(sizeof(LmChange) * (size_t) (ml->nchanges ? ml->nchanges : 1));
        if (s->modChanges) memcpy(s->modChanges, ml->changes, sizeof(LmChange) * (size_t) ml->nchanges);
        s->nModChanges = s->modChanges ? ml->nchanges : 0;
        if (s->modGeo) lg_edit_free(s->modGeo), free(s->modGeo), s->modGeo = NULL;
        if (ml->geo != NULL && !lg_edit_empty(ml->geo)) {
            s->modGeo = (LgEdit *) calloc(1, sizeof(LgEdit));
            if (s->modGeo && lm_geo_copy(s->modGeo, ml->geo)) free(s->modGeo), s->modGeo = NULL;
        }
        snprintf(s->modName, sizeof s->modName, "%s", name);
    }
    for (k = 0; k < m.ntextures; k++) {
        TexSlot *q = (TexSlot *) realloc(g_tex, sizeof(TexSlot) * (size_t) (g_ntex + 1));
        if (q == NULL) break;
        g_tex = q;
        memset(&g_tex[g_ntex], 0, sizeof(TexSlot));
        g_tex[g_ntex].t = m.textures[k];
        g_tex[g_ntex].t.png = m.textures[k].png;
        m.textures[k].png = NULL;   /* (kept: the editor saves it again) */
        g_tex[g_ntex].packed = packed[k], g_tex[g_ntex].n = packedN[k];
        packed[k] = NULL;
        LOG("mods: %s: texture %d (%s %d-bit): %u packed bytes\n", name, g_tex[g_ntex].t.id,
            tc_fmt_name(g_tex[g_ntex].t.fmt), g_tex[g_ntex].t.siz, (unsigned) g_tex[g_ntex].n);
        g_ntex++;
    }
    if (m.ntextures > 0) {
        snprintf(g_texModName, sizeof g_texModName, "%s", name);
        rebuild_patches();
    }
    {
        ModInfo *q = (ModInfo *) realloc(g_mods, sizeof(ModInfo) * (size_t) (g_nmods + 1));
        if (q != NULL) {
            g_mods = q;
            snprintf(g_mods[g_nmods++].name, sizeof g_mods[0].name, "%s", name);
        }
    }
    LOG("mods: %s (%s): %d level%s, %d texture slot%s\n", name, path, m.nlevels, m.nlevels == 1 ? "" : "s",
        m.ntextures, m.ntextures == 1 ? "" : "s");
    for (k = 0; k < 60; k++) free(out[k]);
    for (k = 0; packed && k < m.ntextures; k++) free(packed[k]);
    free(packed), free(packedN);
    lm_mod_free(&m);
    return 0;
fail:
    for (k = 0; k < 60; k++) free(out[k]);
    for (k = 0; packed && k < m.ntextures; k++) free(packed[k]);
    free(packed), free(packedN);
    lm_mod_free(&m);
    return -1;
}

/* ---- texture slots ---- */
/* The ROM as tc_entry reads it (the table at TC_TABLE, its offsets from there): a ROM from the shiftable build may
 * have moved the table (rdram.h rom_phys), and its offsets are from its own place, this ROM's.  Unmoved (every
 * retail ROM): the ROM itself. */
static const uint8_t *tex_rom(size_t *n) {
    size_t romSize = 0;
    const uint8_t *rom = rom_bytes(&romSize);
    const uint32_t tab = rom_phys(TC_TABLE);
    *n = romSize;
    if (rom == NULL || tab <= TC_TABLE || tab - TC_TABLE >= romSize) return rom;
    *n = romSize - (tab - TC_TABLE);
    return rom + (tab - TC_TABLE);
}

/* the original ROM offset of a table offset (what the game's DMA asks for: osPiStartDma hands port_on_dma original
 * offsets, and the table it reads holds them, rom_map_fixup); unmoved: TC_TABLE + off */
static uint32_t tex_orig(uint32_t off) {
    return rom_virt(rom_phys(TC_TABLE) + off);
}

static int cmp_id(const void *a, const void *b) {
    return g_tex[*(const int *) a].t.id - g_tex[*(const int *) b].t.id;
}

static void rebuild_patches(void) {
    int i, n = 0, nn = 0, *news;
    uint32_t at = NEW_SLOT_BASE;
    free(g_patch);
    g_patch = (RomPatch *) calloc((size_t) (3 * g_ntex + 1), sizeof(RomPatch));
    news = (int *) calloc((size_t) g_ntex + 1, sizeof(int));
    port_rom_patches = 0;
    if (g_patch == NULL || news == NULL) {
        free(news);
        return;
    }
    /* the mods' first, then the editor's (an editor slot replaces a mod's of the same id: written last) */
    for (int pass = 0; pass < 2; pass++)
        for (i = 0; i < g_ntex; i++) {
            const TexSlot *s = &g_tex[i];
            uint32_t off;
            unsigned size, packing, room;
            int j, shadowed = 0;
            if (s->editor != pass) continue;
            if (!pass)
                for (j = 0; j < g_ntex; j++)
                    if (g_tex[j].editor && g_tex[j].t.id == s->t.id) shadowed = 1;
            if (s->t.isNew && !shadowed) news[nn++] = i;
            if (s->t.isNew || shadowed || port_tex_slot(s->t.id, &off, &size, &packing, &room, NULL)) continue;
            (void) size, (void) packing;
            /* the slot's room: the packed bytes and zeros after them (as bcfiles mod-export writes it), at the slot's
             * original offset (a ROM from the shiftable build may have moved it); a grown slot's: more */
            const uint32_t eff = grown_room(s->n, room);
            g_patch[n].rom = tex_orig(off), g_patch[n].n = eff, g_patch[n].bytes = s->packed;
            g_patch[n].want = eff > room ? room : 0;
            n++;
            /* the table entry's size (big-endian u16 at +4), the packed length */
            g_patch[n].rom = TC_TABLE + 8u * (uint32_t) s->t.id + 4, g_patch[n].n = 2;
            g_patch[n].bytes = s->packed + eff;   /* (kept after the room: tex_store) */
            n++;
        }
    /* the new slots, in id order, one after the other from NEW_SLOT_BASE */
    qsort(news, (size_t) nn, sizeof(int), cmp_id);
    for (i = 0; i < nn; i++) {
        TexSlot *s = &g_tex[news[i]];
        const uint32_t room = (uint32_t) s->n;
        g_patch[n].rom = at, g_patch[n].n = room, g_patch[n].bytes = s->packed;
        n++;
        put_be32(s->entry, at - TC_TABLE);
        s->entry[4] = (uint8_t) (room >> 8), s->entry[5] = (uint8_t) room, s->entry[6] = 0, s->entry[7] = 0;   /* stored */
        g_patch[n].rom = TC_TABLE + 8u * (uint32_t) s->t.id, g_patch[n].n = 8, g_patch[n].bytes = s->entry;
        n++;
        at += room;
        if (i + 1 >= nn || g_tex[news[i + 1]].t.id != s->t.id + 1) {
            put_be32(s->term, at - TC_TABLE);
            g_patch[n].rom = TC_TABLE + 8u * (uint32_t) (s->t.id + 1), g_patch[n].n = 4, g_patch[n].bytes = s->term;
            n++;
        }
    }
    free(news);
    port_rom_patches = n;
}

/* an entry with no bytes in the ROM (size 0, DMA length 0), not the last */
static int slot_empty(int id) {
    size_t romSize = 0;
    const uint8_t *rom = tex_rom(&romSize);
    TcEntry e;
    return rom != NULL && id >= 0 && id < TC_ENTRIES - 1 && tc_entry(rom, romSize, id, &e) == 0 && e.size == 0 &&
           e.dmaLen == 0;
}

/* an entry a new slot can take: empty, and so are the ones before and after it (the entry before a texture's ends
 * its DMA: a real texture there would read up to the new slot's place; the one after ends the new slot's) */
static int slot_free(int id) {
    return slot_empty(id - 1) && slot_empty(id) && slot_empty(id + 1);
}

int port_tex_new_id(const int *taken, int ntaken) {
    int id, k;
    for (id = 0; id < TC_ENTRIES - 1; id++) {
        int used = 0;
        if (!slot_free(id)) continue;
        for (k = 0; k < ntaken; k++) used = used || taken[k] == id;
        for (k = 0; k < g_ntex; k++) used = used || g_tex[k].t.id == id;
        if (!used) return id;
    }
    return -1;
}

void port_rom_overlay(void *dst, uint32_t off, uint32_t n) {
    int i;
    for (i = 0; i < port_rom_patches; i++) {
        const RomPatch *p = &g_patch[i];
        const uint32_t a = p->rom > off ? p->rom : off, b = p->rom + p->n < off + n ? p->rom + p->n : off + n;
        if (p->want && off != p->rom) continue;   /* (a grown slot overlaps the textures after it: its own DMA only) */
        if (a < b) memcpy((uint8_t *) dst + (a - off), p->bytes + (a - p->rom), b - a);
    }
}

uint32_t port_rom_grown(uint32_t off, uint32_t asked, uint32_t len) {
    int i;
    for (i = 0; i < port_rom_patches; i++) {
        const RomPatch *p = &g_patch[i];
        if (p->want && p->rom == off && asked == p->want && p->n > len) return p->n;
    }
    return len;
}

const LmTexture *port_mods_textures(int *n, const char **modName) {
    static LmTexture *list;
    int i, k = 0;
    free(list);
    list = (LmTexture *) calloc((size_t) g_ntex + 1, sizeof(LmTexture));
    for (i = 0; list && i < g_ntex; i++)
        if (!g_tex[i].editor) list[k++] = g_tex[i].t;
    if (n) *n = k;
    if (modName) *modName = g_texModName;
    return list;
}

int port_tex_slot(int id, uint32_t *offset, unsigned *size, unsigned *packing, uint32_t *room, char sha1[41]) {
    size_t romSize = 0;
    const uint8_t *rom = tex_rom(&romSize);
    TcEntry e, nx;
    if (rom == NULL || tc_entry(rom, romSize, id, &e) || TC_TABLE + (size_t) e.offset + e.size > romSize) return -1;
    if (offset) *offset = e.offset;
    if (size) *size = e.size;
    if (packing) *packing = e.type;
    if (room) {
        /* the length the game's DMA asks for: up to the next entry's original offset (a ROM from the shiftable build
         * can have bytes between two textures that the port doesn't read, rom_map_dma) */
        uint32_t v0 = tex_orig(e.offset), v1;
        *room = e.dmaLen;
        if (rom_map_active() && id + 1 < TC_ENTRIES && tc_entry(rom, romSize, id + 1, &nx) == 0 &&
            (v1 = tex_orig(nx.offset)) >= v0 && v1 - v0 <= e.dmaLen)
            *room = v1 - v0;
    }
    if (sha1) lm_sha1_hex(rom + TC_TABLE + e.offset, e.size, sha1);
    return 0;
}

int port_tex_texels(int id, uint8_t **texels, size_t *n) {
    size_t romSize = 0;
    const uint8_t *rom = tex_rom(&romSize);
    uint32_t off;
    unsigned size, packing, room;
    if (port_tex_slot(id, &off, &size, &packing, &room, NULL)) return -1;
    return tc_decode(rom + TC_TABLE + off, size, (int) packing, NULL, 0, texels, n);
}

/* the packed bytes, zeros to the slot's room, then the table entry's new size (big-endian: rebuild_patches) */
static int tex_store(uint8_t **packed, size_t n, uint32_t room) {
    uint8_t *q = (uint8_t *) realloc(*packed, (size_t) room + 2);
    if (q == NULL) return -1;
    memset(q + n, 0, room - n);
    q[room] = (uint8_t) (n >> 8), q[room + 1] = (uint8_t) n;
    *packed = q;
    return 0;
}

int port_tex_pack(const LmTexture *t, uint8_t **packed, size_t *n, char *err, size_t errlen) {
    size_t romSize = 0;
    const uint8_t *rom = tex_rom(&romSize);
    uint32_t off, room;
    unsigned size, packing;
    char sha[41], e[256];
    uint8_t *png = NULL, *rgba = NULL, *orig = NULL;
    size_t pngN = 0, origN = 0;
    uint32_t w, h;
    int r = -1;
    *packed = NULL;
    if (t->isNew) {
        /* the port's own slot: the image stored (packing 0) in an empty entry (and the empty one after it ends it) */
        uint8_t *tex = NULL;
        size_t tn = 0;
        lm_sha1_hex((const uint8_t *) "", 0, sha);
        if (!slot_free(t->id)) {
            snprintf(err, errlen, "texture %d: a new slot must be an empty table entry with empty ones before and after it",
                     t->id);
            return -1;
        }
        if (strcmp(sha, t->slot_sha1) != 0) {
            snprintf(err, errlen, "texture %d: a new slot's slot_sha1 is the empty one's (%s)", t->id, sha);
            return -1;
        }
        if (t->fmt < 0 || t->siz < 0 || t->png == NULL || tc_base64_decode(t->png, &png, &pngN)) {
            snprintf(err, errlen, "texture %d: \"fmt\", \"siz\" and \"png\" expected", t->id);
            return -1;
        }
        if (t->from >= 0 && port_tex_texels(t->from, &orig, &origN)) orig = NULL, origN = 0;
        if (tc_png_decode(png, pngN, 0, &w, &h, &rgba, e, sizeof e) ||
            (t->mips > 1 ? tc_mip_texels(rgba, w, h, t->fmt, t->siz, t->mips, orig, origN, &tex, &tn, e, sizeof e)
                         : tc_from_rgba8(rgba, w, h, t->fmt, t->siz, &tex, &tn, e, sizeof e))) {
            snprintf(err, errlen, "texture %d: %s", t->id, e);
            free(png), free(rgba), free(tex), free(orig);
            return -1;
        }
        /* the texels after the picture's (other tiles; the smaller levels of detail unless made from the picture) as
         * the slot it stands in for has them */
        if (orig != NULL && origN > tn) {
            uint8_t *q = (uint8_t *) realloc(tex, origN);
            if (q != NULL) {
                memcpy(q + tn, orig + tn, origN - tn);
                tex = q, tn = origN;
            }
        }
        free(orig), orig = NULL;
        if (tc_pack(tex, tn, 0, packed, n, e, sizeof e)) {
            snprintf(err, errlen, "texture %d: %s", t->id, e);
            free(png), free(rgba), free(tex);
            return -1;
        }
        free(png), free(rgba), free(tex);
        if (*n > 0xFFF8 || tex_store(packed, *n, (uint32_t) *n)) {
            snprintf(err, errlen, "texture %d: %u bytes: too large for a slot", t->id, (unsigned) *n);
            free(*packed);
            *packed = NULL;
            return -1;
        }
        return 0;
    }
    if (port_tex_slot(t->id, &off, &size, &packing, &room, sha)) {
        snprintf(err, errlen, "texture %d: no such table entry in this ROM", t->id);
        return -1;
    }
    if (strcmp(sha, t->slot_sha1) != 0) {
        snprintf(err, errlen, "texture %d: made for another texture (SHA-1 %s, this ROM's is %s)", t->id, t->slot_sha1, sha);
        return -1;
    }
    if (t->fmt < 0 || t->siz < 0) {
        snprintf(err, errlen, "texture %d: \"fmt\" and \"siz\" (the texel format the level's lists use) expected", t->id);
        return -1;
    }
    if (t->png == NULL || tc_base64_decode(t->png, &png, &pngN)) {
        snprintf(err, errlen, "texture %d: the PNG doesn't read (not base64)", t->id);
        return -1;
    }
    if (tc_png_decode(png, pngN, 0, &w, &h, &rgba, e, sizeof e)) {
        snprintf(err, errlen, "texture %d: the PNG doesn't read (%s)", t->id, e);
        goto done;
    }
    if (packing == 4 || packing == 5) {
        /* a palette packing: the picture takes the colours of the slot's palette entry (kept: it is shared) */
        uint32_t poff, proom;
        unsigned psize, ppack;
        uint8_t pal[0x100];
        size_t paln;
        if (!t->hasPalette || port_tex_slot(t->palette, &poff, &psize, &ppack, &proom, NULL)) {
            snprintf(err, errlen, "texture %d: packing %u needs its palette entry (\"palette\")", t->id, packing);
            goto done;
        }
        (void) psize, (void) ppack;
        paln = proom < sizeof pal ? proom : sizeof pal;
        memset(pal, 0, sizeof pal);
        memcpy(pal, rom + TC_TABLE + poff, paln);
        if (tc_decode(rom + TC_TABLE + off, size, (int) packing, pal, sizeof pal, &orig, &origN) ||
            tc_encode_slot_pal(rgba, w, h, t->fmt, t->siz, (int) packing, origN, pal, paln, packed, n, e, sizeof e)) {
            snprintf(err, errlen, "texture %d: %s", t->id, e);
            goto done;
        }
    } else if (tc_decode(rom + TC_TABLE + off, size, (int) packing, NULL, 0, &orig, &origN)) {
        snprintf(err, errlen, "out of memory");
        goto done;
    } else if (tc_encode_slot_mips(rgba, w, h, t->fmt, t->siz, (int) packing, orig, origN, t->mips, packed, n, e,
                                   sizeof e)) {
        snprintf(err, errlen, "texture %d: %s", t->id, e);
        goto done;
    }
    if (*n > room && (room == 0 || *n > 0xFFFF || grown_room(*n, room) > (origN > room ? origN : room))) {
        snprintf(err, errlen, "texture %d: the image packs to %u bytes, more than the texture unpacks to (%u; the slot "
                 "can grow up to that) - try fewer colours or a smaller image", t->id, (unsigned) *n, (unsigned) origN);
        free(*packed);
        *packed = NULL;
        goto done;
    }
    /* (more than its room: the slot grows, grown_room) */
    if (tex_store(packed, *n, grown_room(*n, room))) goto done;
    r = 0;
done:
    free(png), free(rgba), free(orig);
    return r;
}

int port_tex_editor_set(const LmTexture *t, int n, char *err, size_t errlen) {
    uint8_t **pk = NULL;
    size_t *pn = NULL;
    int i, k, r = -1;
    if (n > 0) {
        pk = (uint8_t **) calloc((size_t) n, sizeof(uint8_t *));
        pn = (size_t *) calloc((size_t) n, sizeof(size_t));
        if (pk == NULL || pn == NULL) goto done;
        for (i = 0; i < n; i++)
            if (port_tex_pack(&t[i], &pk[i], &pn[i], err, errlen)) goto done;
    }
    /* the editor's earlier ones out, the new ones in */
    for (i = 0, k = 0; i < g_ntex; i++) {
        if (g_tex[i].editor) {
            free(g_tex[i].packed), free(g_tex[i].t.png);
            continue;
        }
        g_tex[k++] = g_tex[i];
    }
    g_ntex = k;
    for (i = 0; i < n; i++) {
        TexSlot *q = (TexSlot *) realloc(g_tex, sizeof(TexSlot) * (size_t) (g_ntex + 1));
        if (q == NULL) goto done;
        g_tex = q;
        memset(&g_tex[g_ntex], 0, sizeof(TexSlot));
        g_tex[g_ntex].t = t[i];
        g_tex[g_ntex].t.png = (char *) malloc(strlen(t[i].png ? t[i].png : "") + 1);
        if (g_tex[g_ntex].t.png) strcpy(g_tex[g_ntex].t.png, t[i].png ? t[i].png : "");
        g_tex[g_ntex].packed = pk[i], g_tex[g_ntex].n = pn[i], g_tex[g_ntex].editor = 1;
        pk[i] = NULL;
        g_ntex++;
    }
    rebuild_patches();
    if (n > 0) LOG("mods: the editor's %d texture slot%s used from the next loads\n", n, n == 1 ? "" : "s");
    r = 0;
done:
    for (i = 0; pk && i < n; i++) free(pk[i]);
    free(pk), free(pn);
    return r;
}

int port_mods_count(void) {
    return g_nmods;
}

const char *port_mods_name(int i) {
    return i >= 0 && i < g_nmods ? g_mods[i].name : "";
}

int port_mods_write_levels(const char *dir) {
    int level, n = 0;
    for (level = 0; level < 60; level++) {
        char path[1024];
        FILE *f;
        if (g_lv[level].edit == NULL) continue;
        snprintf(path, sizeof path, "%s\\level_%02d.bin", dir, level);
        f = open_utf8(path, "wb");
        if (f == NULL || fwrite(g_lv[level].edit, 1, g_lv[level].editSize, f) != g_lv[level].editSize) {
            if (f) fclose(f);
            LOG("mods: can't write %s\n", path);
            return -1;
        }
        fclose(f);
        n++;
    }
    return n;
}

int port_mods_read_levels(const char *dir, char *err, size_t errlen) {
    int level, n = 0;
    for (level = 0; level < 60; level++) {
        char path[1024], src[1100];
        size_t len = 0;
        char *data;
        snprintf(path, sizeof path, "%s\\level_%02d.bin", dir, level);
        data = read_text(path, &len);
        if (data == NULL) continue;
        snprintf(src, sizeof src, "--level-dir %s", path);
        if (port_level_set_edit(level, (const uint8_t *) data, len, src)) {
            snprintf(err, errlen, "%s: not used (too large, or the ROM has no level %d)", path, level);
            free(data);
            return -1;
        }
        free(data);
        n++;
    }
    return n;
}

/* ---- the game's hook (game/port.h) ---- */
void port_level_pre(int level, unsigned int dest) {
    LevelState *s;
    if (level < 0 || level >= 60 || g_lv[level].edit == NULL) return;
    s = &g_lv[level];
    /* hd_rom_load writes the ROM's file (origSize bytes) here; an edited file that is shorter must leave the rest
     * as it was, as a ROM with the edited file in it would: keep it (and its byte-order units) */
    free(g_snap.bytes);
    free(g_snap.units);
    g_snap.level = level;
    g_snap.dest = dest;
    g_snap.len = (uint32_t) s->origSize;
    if (dest < RDRAM_BASE || dest >= RDRAM_BASE + RDRAM_SIZE) g_snap.len = 0;
    else if (g_snap.len > RDRAM_BASE + RDRAM_SIZE - dest) g_snap.len = RDRAM_BASE + RDRAM_SIZE - dest;
    g_snap.bytes = (uint8_t *) malloc(g_snap.len ? g_snap.len : 1);
    if (g_snap.bytes && g_snap.len) memcpy(g_snap.bytes, (void *) (uintptr_t) dest, g_snap.len);
    g_snap.units = g_snap.len ? port_unit_save(dest, g_snap.len) : NULL;
}

void port_level_post(int level, unsigned int rom, unsigned int dest, unsigned int *size) {
    LevelState *s;
    uint32_t n;
    if (level < 0 || level >= 60 || g_lv[level].edit == NULL) return;
    s = &g_lv[level];
    n = (uint32_t) s->editSize;
    if (dest < RDRAM_BASE || n > RDRAM_BASE + RDRAM_SIZE - dest) {
        LOG("mods: level %d: no room for the edited file at 0x%08X: the ROM's is used\n", level, dest);
        return;
    }
    if (*size != s->origSize)
        LOG("mods: level %d: the game inflated 0x%X bytes, the ROM's file is 0x%X\n", level, *size,
            (unsigned) s->origSize);
    port_gfx_forget(dest, *size > n ? *size : n);
    if (g_snap.level == level && g_snap.dest == dest && g_snap.bytes != NULL && n < g_snap.len) {
        memcpy((void *) (uintptr_t) (dest + n), g_snap.bytes + n, g_snap.len - n);
        if (g_snap.units != NULL) {
            /* (the units of the whole range back, then the edited file's own: port_on_load marks them) */
            port_unit_restore(dest, g_snap.units, g_snap.len);
        }
    }
    free(g_snap.bytes);
    free(g_snap.units);
    g_snap.bytes = NULL, g_snap.units = NULL, g_snap.level = -1;
    memcpy((void *) (uintptr_t) dest, s->edit, n);
    *size = n;
    port_on_load(rom, dest, n);   /* swapped as the ROM's file was */
    LOG("mods: level %d: the edited file (%s, 0x%X bytes) loaded at 0x%08X\n", level, s->source, n, dest);
}
