/* Level-record edits and mod files (see level_mod.h).  Written for the port
 * from the layouts in docs/systems/levels.md and tools_port/bcfiles/level.py
 * (the same layout rules, so both build the same bytes). */
#include "level_mod.h"
#include "host_util.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int buf_set(Buf *b, const void *data, size_t n) {
    b->n = 0;
    return buf_add(b, data, n);
}

/* ------------------------------------------------------------------ the layout */
/* the header's chain (levels.md, level.py CHAIN) and the alignment each section starts at */
static const unsigned kChain[] = {0x20, 0x24, 0x28, 0x2C, 0x30, 0x34, 0x38, 0x3C, 0x40, 0x44, 0x48,
                                  0x4C, 0x50, 0x54, 0x58, 0x5C, 0x60, 0x64, 0x68, 0x6C, 0x70, 0x74,
                                  0xA0, 0xA4, 0xA8, 0xAC, 0xB0, 0xB4, 0xB8, 0xBC, 0xC0, 0x78};
#define NCHAIN ((int) (sizeof kChain / sizeof kChain[0]))
#define NSECT (NCHAIN - 1)
static unsigned align_of(unsigned slot) {
    switch (slot) {
        case 0x2C: case 0x74: case 0xA0: case 0xA4: case 0xA8: case 0xAC: case 0xB0: case 0xB4: case 0xB8:
        case 0xBC:
            return 4;
        case 0x50: case 0x54: case 0x60: case 0x64: case 0x68: case 0x6C: case 0x70: case 0xC0:
            return 1;
        case 0x78:
            return 16;
        default:
            return 2;
    }
}
static const unsigned kDlSlots[] = {0x7C, 0x80, 0x84, 0x88, 0x8C, 0x90, 0x94, 0x98, 0x9C};

/* ---- the record kinds ---- */
enum { T_U8, T_S16, T_U16 };
typedef struct {
    const char *name;
    uint8_t off, type;
    long lo, hi;
} FieldDef;
typedef struct {
    const char *key;
    unsigned slot;     /* section */
    unsigned size;     /* fixed record size (slots: the head, 10) */
    int max;           /* records the game's arrays hold */
    int nf;
    FieldDef f[LM_MAX_FIELDS];
} KindDef;

static const KindDef kKinds[LM_KINDS] = {
    /* (gVehicles holds 12 records: the spawns, the carrier, the chopper, the shuttle, a race's ghost) */
    {"vehicle", 0x50, 9, 8, 5,{{"type", 0, T_U8, 0, 18}, {"x", 1, T_S16, -32768, 32767}, {"y", 3, T_S16, -32768, 32767},
                                {"z", 5, T_S16, -32768, 32767}, {"heading", 7, T_S16, 0, 4095}}},
    {"carrier", 0x54, 9, 1, 5, {{"speed", 0, T_U8, 0, 255}, {"x", 1, T_S16, -32768, 32767}, {"z", 3, T_S16, -32768, 32767},
                                {"heading", 5, T_S16, 0, 4095}, {"limit", 7, T_S16, -32768, 32767}}},
    {"object", 0x5C, 14, 55, 8, {{"x", 0, T_U16, 0, 65535}, {"y", 2, T_U16, 0, 65535}, {"z", 4, T_U16, 0, 65535},
                                 {"type", 6, T_U16, 0, 511}, {"path", 8, T_U8, 0, 255}, {"people", 9, T_U8, 0, 255},
                                 {"motion", 10, T_U16, 0, 5}, {"tag", 12, T_U16, 0, 65535}}},
    {"rdu", 0x34, 6, 400, 3, {{"x", 0, T_S16, -32768, 32767}, {"y", 2, T_S16, -32768, 32767}, {"z", 4, T_S16, -32768, 32767}}},
    {"tnt", 0x38, 12, 20, 7, {{"x", 0, T_S16, -32768, 32767}, {"y", 2, T_S16, -32768, 32767}, {"z", 4, T_S16, -32768, 32767},
                              {"type", 6, T_U8, 0, 1}, {"timer", 7, T_U8, 0, 255}, {"unk8", 8, T_S16, -32768, 32767},
                              {"unkA", 10, T_S16, -32768, 32767}}},
    {"crate", 0x20, 8, 15, 4, {{"x", 0, T_S16, -32768, 32767}, {"y", 2, T_S16, -32768, 32767}, {"z", 4, T_S16, -32768, 32767},
                               {"type", 6, T_S16, 0, 1}}},
    {"commpt", 0x28, 8, 2, 4, {{"x", 0, T_S16, -32768, 32767}, {"y", 2, T_S16, -32768, 32767}, {"z", 4, T_S16, -32768, 32767},
                               {"w", 6, T_U16, 0, 65535}}},
    {"block", 0x3C, 8, 8, 4, {{"x", 0, T_S16, -32768, 32767}, {"y", 2, T_S16, -32768, 32767}, {"z", 4, T_S16, -32768, 32767},
                              {"model", 6, T_S16, 0, 2}}},
    {"slot", 0x3C, 10, 8, 5, {{"x", 0, T_S16, -32768, 32767}, {"y", 2, T_S16, -32768, 32767}, {"z", 4, T_S16, -32768, 32767},
                              {"model", 6, T_U8, 0, 2}, {"target", 8, T_S16, -32768, 32767}}},
};
#define SLOT_TRI 0x16
#define MAX_SLOT_TRIS 100   /* gDynTris: 0x2580 bytes of 0x60-byte triangles */

const char *lm_kind_key(int kind) {
    return kind >= 0 && kind < LM_KINDS ? kKinds[kind].key : "";
}

int lm_kind_from_key(const char *key) {
    int k;
    for (k = 0; key != NULL && k < LM_KINDS; k++)
        if (strcmp(key, kKinds[k].key) == 0) return k;
    return -1;
}

int lm_field_count(int kind) {
    return kind >= 0 && kind < LM_KINDS ? kKinds[kind].nf : 0;
}

const char *lm_field_name(int kind, int field) {
    return field >= 0 && field < lm_field_count(kind) ? kKinds[kind].f[field].name : "";
}

int lm_field_index(int kind, const char *name) {
    int f;
    for (f = 0; name != NULL && f < lm_field_count(kind); f++)
        if (strcmp(kKinds[kind].f[f].name, name) == 0) return f;
    return -1;
}

void lm_field_range(int kind, int field, long *lo, long *hi) {
    if (field < 0 || field >= lm_field_count(kind)) {
        *lo = *hi = 0;
        return;
    }
    *lo = kKinds[kind].f[field].lo;
    *hi = kKinds[kind].f[field].hi;
}

int lm_kind_max(int kind) {
    return kind >= 0 && kind < LM_KINDS ? kKinds[kind].max : 0;
}

/* ---- the level ---- */
typedef struct {
    int n, cap;
    Buf *r;
} List;

enum { M_RAW, M_RECORDS, M_BLOCKS };
typedef struct {
    int mode;
    Buf raw;        /* M_RAW: the section's bytes (to the next section, minus padding the layout recomputes) */
    List a, b;      /* M_RECORDS: a; M_BLOCKS: a = blocks, b = slots (each with its triangles) */
    int present;    /* M_BLOCKS: the counts are there (a section under 2 bytes has none) */
    Buf tail;       /* bytes after the records that aren't padding */
} Sect;

struct LmLevel {
    uint8_t head[0xC8];
    Buf vtx;               /* 0xC8 .. the 0x20 offset */
    Sect s[NSECT];
    Buf dl;                /* [0x78, 0x84) */
    Buf trailer;           /* after 0x84 */
    uint32_t oldBase;
    uint32_t oldDl[9];     /* kDlSlots' offsets */
};

static int sect_of(unsigned slot) {
    int i;
    for (i = 0; i < NSECT; i++)
        if (kChain[i] == slot) return i;
    return -1;
}

static void list_free(List *l) {
    int i;
    for (i = 0; i < l->n; i++) free(l->r[i].p);
    free(l->r);
    memset(l, 0, sizeof *l);
}

static int list_insert(List *l, int at, const uint8_t *p, size_t n) {
    Buf b = {0};
    if (l->n == l->cap) {
        int cap = l->cap ? l->cap * 2 : 16;
        Buf *q = (Buf *) realloc(l->r, sizeof(Buf) * (size_t) cap);
        if (q == NULL) return -1;
        l->r = q, l->cap = cap;
    }
    if (buf_set(&b, p, n)) return -1;
    memmove(l->r + at + 1, l->r + at, sizeof(Buf) * (size_t) (l->n - at));
    l->r[at] = b;
    l->n++;
    return 0;
}

static void list_remove(List *l, int at) {
    free(l->r[at].p);
    memmove(l->r + at, l->r + at + 1, sizeof(Buf) * (size_t) (l->n - at - 1));
    l->n--;
}

void lm_free(LmLevel *lv) {
    int i;
    if (lv == NULL) return;
    free(lv->vtx.p);
    for (i = 0; i < NSECT; i++) {
        free(lv->s[i].raw.p);
        free(lv->s[i].tail.p);
        list_free(&lv->s[i].a);
        list_free(&lv->s[i].b);
    }
    free(lv->dl.p);
    free(lv->trailer.p);
    free(lv);
}

static int is_record_slot(unsigned slot, unsigned *size) {
    int k;
    for (k = 0; k < LM_KINDS; k++)
        if (kKinds[k].slot == slot && slot != 0x3C) {
            *size = kKinds[k].size;
            return 1;
        }
    return 0;
}

/* the push-block section: s16 n, n blocks of 8, s16 m, m slots of 10 + 0x16 * tris; the end, or 0 */
static size_t parse_blocks(const uint8_t *b, size_t lo, size_t hi, Sect *s) {
    size_t p = lo;
    unsigned n, m, i;
    if (hi - lo < 2) return 0;
    n = get_be16(b + p), p += 2;
    if (p + 8 * (size_t) n + 2 > hi) return 0;
    for (i = 0; i < n; i++, p += 8)
        if (s && list_insert(&s->a, s->a.n, b + p, 8)) return 0;
    m = get_be16(b + p), p += 2;
    for (i = 0; i < m; i++) {
        size_t len;
        if (p + 10 > hi) return 0;
        len = 10 + SLOT_TRI * (size_t) b[p + 7];
        if (p + len > hi) return 0;
        if (s && list_insert(&s->b, s->b.n, b + p, len)) return 0;
        p += len;
    }
    return p;
}

LmLevel *lm_open(const uint8_t *file, size_t size, char *err, size_t errlen) {
    LmLevel *lv;
    uint32_t hdr[0x2A];
    int i;
    unsigned cols, rows;
    if (file == NULL || size < 0xC8) {
        seterr(err, errlen, "not a level file (under 0xC8 bytes)");
        return NULL;
    }
    for (i = 0; i < 0x2A; i++) hdr[i] = get_be32(file + 0x20 + 4 * i);
#define H(slot) hdr[((slot) - 0x20) / 4]
    for (i = 0; i < NCHAIN; i++) {
        const uint32_t o = H(kChain[i]);
        if (o < 0xC8 || o > size || (i > 0 && o < H(kChain[i - 1]))) {
            seterr(err, errlen, "header offset 0x%X = 0x%X is not in one ascending chain (file 0x%X bytes)",
                   kChain[i], (unsigned) o, (unsigned) size);
            return NULL;
        }
    }
    if (H(0x84) < H(0x78) || H(0x84) > size) {
        seterr(err, errlen, "display-list end 0x%X outside the file", (unsigned) H(0x84));
        return NULL;
    }
    lv = (LmLevel *) calloc(1, sizeof *lv);
    if (lv == NULL) return NULL;
    memcpy(lv->head, file, 0xC8);
    cols = get_be16(file + 0x10), rows = get_be16(file + 0x12);
    if (buf_set(&lv->vtx, file + 0xC8, H(0x20) - 0xC8)) goto oom;
    for (i = 0; i < NSECT; i++) {
        const unsigned slot = kChain[i];
        const size_t lo = H(slot), hi = H(kChain[i + 1]);
        Sect *s = &lv->s[i];
        size_t end = hi, k;
        unsigned rsize, pad;
        int zero = 1;
        if (slot == 0x3C) {
            Sect tmp;
            memset(&tmp, 0, sizeof tmp);
            end = parse_blocks(file, lo, hi, &tmp);
            if (end != 0 || hi - lo < 2) {
                *s = tmp;
                s->mode = M_BLOCKS;
                s->present = end != 0;
                if (end == 0) end = lo;
            } else {
                list_free(&tmp.a), list_free(&tmp.b);
                s->mode = M_RAW;
                end = hi;
            }
        } else if (is_record_slot(slot, &rsize)) {
            const size_t n = slot == 0x54 ? (hi - lo >= 9 ? 1 : 0) : (hi - lo) / rsize;
            s->mode = M_RECORDS;
            for (k = 0; k < n; k++)
                if (list_insert(&s->a, s->a.n, file + lo + k * rsize, rsize)) goto oom;
            end = lo + n * rsize;
        } else if (slot == 0x70) {
            /* collision grid B: per cell a BE u32 end offset (from the grid's start), then 0x16-byte triangles */
            size_t p = lo;
            unsigned c;
            for (c = 0; c < cols * rows; c++) {
                size_t e;
                if (p + 4 > hi) break;
                e = lo + get_be32(file + p);
                if (e < p + 4 || e > hi || (e - p - 4) % 0x16) break;
                p = e;
            }
            end = c == cols * rows ? p : hi;
            s->mode = M_RAW;
        } else if (slot == 0xC0) {
            end = lo;
            s->mode = M_RAW;
        } else {
            s->mode = M_RAW;
        }
        if (s->mode == M_RAW && buf_set(&s->raw, file + lo, end - lo)) goto oom;
        /* the rest: padding the layout recomputes, or a tail kept as it is */
        pad = (unsigned) ((align_of(kChain[i + 1]) - end % align_of(kChain[i + 1])) % align_of(kChain[i + 1]));
        for (k = end; k < hi; k++) zero &= file[k] == 0;
        if (!(hi - end == pad && zero) && buf_set(&s->tail, file + end, hi - end)) goto oom;
    }
    if (buf_set(&lv->dl, file + H(0x78), H(0x84) - H(0x78))) goto oom;
    if (buf_set(&lv->trailer, file + H(0x84), size - H(0x84))) goto oom;
    lv->oldBase = H(0x78);
    for (i = 0; i < 9; i++) lv->oldDl[i] = H(kDlSlots[i]);
#undef H
    return lv;
oom:
    seterr(err, errlen, "out of memory");
    lm_free(lv);
    return NULL;
}

static int copy_list(List *d, const List *s) {
    int i;
    memset(d, 0, sizeof *d);
    for (i = 0; i < s->n; i++)
        if (list_insert(d, i, s->r[i].p, s->r[i].n)) return -1;
    return 0;
}

LmLevel *lm_clone(const LmLevel *lv) {
    LmLevel *c;
    int i;
    if (lv == NULL || (c = (LmLevel *) calloc(1, sizeof *c)) == NULL) return NULL;
    memcpy(c->head, lv->head, sizeof c->head);
    c->oldBase = lv->oldBase;
    memcpy(c->oldDl, lv->oldDl, sizeof c->oldDl);
    if (buf_set(&c->vtx, lv->vtx.p, lv->vtx.n) || buf_set(&c->dl, lv->dl.p, lv->dl.n) ||
        buf_set(&c->trailer, lv->trailer.p, lv->trailer.n))
        goto oom;
    for (i = 0; i < NSECT; i++) {
        c->s[i].mode = lv->s[i].mode;
        c->s[i].present = lv->s[i].present;
        if (buf_set(&c->s[i].raw, lv->s[i].raw.p, lv->s[i].raw.n) ||
            buf_set(&c->s[i].tail, lv->s[i].tail.p, lv->s[i].tail.n) || copy_list(&c->s[i].a, &lv->s[i].a) ||
            copy_list(&c->s[i].b, &lv->s[i].b))
            goto oom;
    }
    return c;
oom:
    lm_free(c);
    return NULL;
}

/* the list a kind's records are in (NULL: the section isn't editable) */
static List *list_of(const LmLevel *lv, int kind) {
    int i;
    Sect *s;
    if (kind < 0 || kind >= LM_KINDS) return NULL;
    i = sect_of(kKinds[kind].slot);
    s = (Sect *) &lv->s[i];
    if (kind == LM_BLOCK || kind == LM_SLOT) {
        if (s->mode != M_BLOCKS || !s->present) return NULL;
        return kind == LM_BLOCK ? &s->a : &s->b;
    }
    return s->mode == M_RECORDS ? &s->a : NULL;
}

int lm_count(const LmLevel *lv, int kind) {
    const List *l = lv ? list_of(lv, kind) : NULL;
    return l ? l->n : 0;
}

static long get_field(const uint8_t *r, const FieldDef *f) {
    switch (f->type) {
        case T_U8: return r[f->off];
        case T_S16: return (long) (int16_t) get_be16(r + f->off);
        default: return (long) get_be16(r + f->off);
    }
}

static void put_field(uint8_t *r, const FieldDef *f, long v) {
    if (f->type == T_U8) r[f->off] = (uint8_t) v;
    else put_be16(r + f->off, (unsigned) (v & 0xFFFF));
}

int lm_get(const LmLevel *lv, int kind, int index, int field, long *value) {
    const List *l = lv ? list_of(lv, kind) : NULL;
    if (l == NULL || index < 0 || index >= l->n || field < 0 || field >= kKinds[kind].nf) return -1;
    *value = get_field(l->r[index].p, &kKinds[kind].f[field]);
    return 0;
}

int lm_set(LmLevel *lv, int kind, int index, int field, long value, char *err, size_t errlen) {
    List *l = lv ? list_of(lv, kind) : NULL;
    const FieldDef *f;
    if (l == NULL || index < 0 || index >= l->n) {
        seterr(err, errlen, "no %s %d", lm_kind_key(kind), index);
        return -1;
    }
    if (field < 0 || field >= kKinds[kind].nf) {
        seterr(err, errlen, "%s has no field %d", lm_kind_key(kind), field);
        return -1;
    }
    f = &kKinds[kind].f[field];
    if (value < f->lo || value > f->hi) {
        seterr(err, errlen, "%s %s: %ld is outside %ld..%ld", lm_kind_key(kind), f->name, value, f->lo, f->hi);
        return -1;
    }
    if (kind == LM_SLOT && field < 3) {
        /* a slot's triangles (nine BE s16 vertices each) move with it */
        uint8_t *r = l->r[index].p;
        const long d = value - get_field(r, f);
        unsigned t, v;
        for (t = 0; t < r[7]; t++)
            for (v = 0; v < 3; v++) {
                uint8_t *p = r + 10 + SLOT_TRI * t + 6 * v + 2 * (unsigned) field;
                const long nv = (long) (int16_t) get_be16(p) + d;
                if (nv < -32768 || nv > 32767) {
                    seterr(err, errlen, "slot %d: moving it puts a triangle outside the s16 range", index);
                    return -1;
                }
            }
        for (t = 0; t < r[7]; t++)
            for (v = 0; v < 3; v++) {
                uint8_t *p = r + 10 + SLOT_TRI * t + 6 * v + 2 * (unsigned) field;
                put_be16(p, (unsigned) (((long) (int16_t) get_be16(p) + d) & 0xFFFF));
            }
    }
    put_field(l->r[index].p, f, value);
    return 0;
}

int lm_insert_copy(LmLevel *lv, int kind, int at, int from, char *err, size_t errlen) {
    List *l = lv ? list_of(lv, kind) : NULL;
    if (kind == LM_CARRIER) {
        seterr(err, errlen, "a level has one carrier record: it can't be added");
        return -1;
    }
    if (l == NULL || from < 0 || from >= l->n || at < 0 || at > l->n) {
        seterr(err, errlen, "add %s at %d as a copy of %d: no such record", lm_kind_key(kind), at, from);
        return -1;
    }
    if (l->n >= kKinds[kind].max) {
        seterr(err, errlen, "a level holds at most %d %s records (the game's arrays)", kKinds[kind].max,
               lm_kind_key(kind));
        return -1;
    }
    {
        Buf copy = {0};
        int r;
        if (buf_set(&copy, l->r[from].p, l->r[from].n)) return -1;
        r = list_insert(l, at, copy.p, copy.n);
        free(copy.p);
        if (r) seterr(err, errlen, "out of memory");
        return r;
    }
}

int lm_delete(LmLevel *lv, int kind, int index, char *err, size_t errlen) {
    List *l = lv ? list_of(lv, kind) : NULL;
    if (kind == LM_CARRIER) {
        seterr(err, errlen, "the carrier record can't be deleted (set its speed to 0: no carrier)");
        return -1;
    }
    if (l == NULL || index < 0 || index >= l->n) {
        seterr(err, errlen, "no %s %d", lm_kind_key(kind), index);
        return -1;
    }
    list_remove(l, index);
    return 0;
}

int lm_check(const LmLevel *lv, char *err, size_t errlen) {
    int k, i, drivers = 0, tris = 0;
    for (k = 0; k < LM_KINDS; k++)
        if (lm_count(lv, k) > kKinds[k].max) {
            seterr(err, errlen, "%d %s records: the game holds at most %d", lm_count(lv, k), kKinds[k].key,
                   kKinds[k].max);
            return -1;
        }
    for (i = 0; i < lm_count(lv, LM_VEHICLE); i++) {
        long t;
        lm_get(lv, LM_VEHICLE, i, 0, &t);
        if (t == 0) drivers++;
        if (t == 12 || t > 18) {
            seterr(err, errlen, "vehicle spawn %d: type %ld has no vehicle (the game stops its list there)", i, t);
            return -1;
        }
    }
    if (list_of(lv, LM_VEHICLE) != NULL && drivers == 0) {
        seterr(err, errlen, "no driver spawn (type 0): the game looks for one without an end");
        return -1;
    }
    {
        const List *l = list_of(lv, LM_SLOT);
        for (i = 0; l != NULL && i < l->n; i++) tris += l->r[i].p[7];
    }
    if (tris > MAX_SLOT_TRIS) {
        seterr(err, errlen, "the slots have %d triangles: the game holds at most %d", tris, MAX_SLOT_TRIS);
        return -1;
    }
    return 0;
}

static int list_bytes(Buf *out, const List *l) {
    int i;
    for (i = 0; i < l->n; i++)
        if (buf_add(out, l->r[i].p, l->r[i].n)) return -1;
    return 0;
}

uint8_t *lm_build(const LmLevel *lv, size_t *size, uint32_t *split) {
    Buf out = {0};
    uint32_t off[NCHAIN], base;
    size_t visAt[8], visLen[8];
    int i, ok = 1;
    if (buf_add(&out, lv->head, 0xC8) || buf_add(&out, lv->vtx.p, lv->vtx.n)) goto oom;
    for (i = 0; i < NSECT; i++) {
        const Sect *s = &lv->s[i];
        const unsigned a = align_of(kChain[i]);
        if (out.n % a && buf_add(&out, NULL, a - out.n % a)) goto oom;
        off[i] = (uint32_t) out.n;
        if (s->mode == M_RAW) {
            if (buf_add(&out, s->raw.p, s->raw.n)) goto oom;
        } else if (s->mode == M_RECORDS) {
            if (list_bytes(&out, &s->a)) goto oom;
        } else if (s->present) {
            uint8_t c[2];
            put_be16(c, (unsigned) s->a.n);
            if (buf_add(&out, c, 2) || list_bytes(&out, &s->a)) goto oom;
            put_be16(c, (unsigned) s->b.n);
            if (buf_add(&out, c, 2) || list_bytes(&out, &s->b)) goto oom;
        }
        if (kChain[i] >= 0xA0 && kChain[i] <= 0xBC) {
            visAt[(kChain[i] - 0xA0) / 4] = off[i];
            visLen[(kChain[i] - 0xA0) / 4] = s->raw.n;
        }
        if (buf_add(&out, s->tail.p, s->tail.n)) goto oom;
    }
    if (out.n % 16 && buf_add(&out, NULL, 16 - out.n % 16)) goto oom;
    base = (uint32_t) out.n;
    off[NSECT] = base;
    if (buf_add(&out, lv->dl.p, lv->dl.n) || buf_add(&out, lv->trailer.p, lv->trailer.n)) goto oom;
    /* the header: the chain, the display-list words (relative to the block), 0xC4 as it was */
    for (i = 0; i < NCHAIN; i++) put_be32(out.p + kChain[i], off[i]);
    for (i = 0; i < 9; i++) put_be32(out.p + kDlSlots[i], lv->oldDl[i] - lv->oldBase + base);
    /* the visibility records and groups hold absolute offsets into the block: moved with it */
    if (base != lv->oldBase) {
        const uint32_t d = base - lv->oldBase;
        int k;
        for (k = 0; k < 4; k++) {   /* records: start, alt, end, id, cell */
            size_t p;
            for (p = 0; p + 0x14 <= visLen[k]; p += 0x14) {
                int w;
                for (w = 0; w < 3; w++) {
                    uint8_t *q = out.p + visAt[k] + p + 4 * (size_t) w;
                    put_be32(q, get_be32(q) + d);
                }
            }
        }
        for (k = 4; k < 8; k++) {   /* groups: start, alt, end, id, n, then n cells (fixed only if the walk holds) */
            size_t p = 0;
            ok = 1;
            while (p + 0x14 <= visLen[k]) {
                const int32_t n = (int32_t) get_be32(out.p + visAt[k] + p + 0x10);
                if (n < 0 || p + 0x14 + 4 * (size_t) n > visLen[k]) {
                    ok = 0;
                    break;
                }
                p += 0x14 + 4 * (size_t) n;
            }
            for (p = 0; ok && p + 0x14 <= visLen[k];) {
                int w;
                for (w = 0; w < 3; w++) {
                    uint8_t *q = out.p + visAt[k] + p + 4 * (size_t) w;
                    put_be32(q, get_be32(q) + d);
                }
                p += 0x14 + 4 * (size_t) get_be32(out.p + visAt[k] + p + 0x10);
            }
        }
    }
    *size = out.n;
    if (split) *split = base;
    return out.p;
oom:
    free(out.p);
    return NULL;
}

/* ------------------------------------------------------------------ geometry (level_geo.c) */
int lm_set_raw_section(LmLevel *lv, unsigned slot, const uint8_t *p, size_t n) {
    const int i = sect_of(slot);
    if (i < 0 || lv->s[i].mode != M_RAW) return -1;
    lv->s[i].tail.n = 0;
    return buf_set(&lv->s[i].raw, p, n);
}

int lm_set_display(LmLevel *lv, const uint8_t *vtx, size_t nvtx, const uint8_t *dl, size_t ndl, const uint32_t rel[9]) {
    int i;
    if (buf_set(&lv->vtx, vtx, nvtx) || buf_set(&lv->dl, dl, ndl)) return -1;
    for (i = 0; i < 9; i++) lv->oldDl[i] = lv->oldBase + rel[i];
    /* the block's member is zero-padded to 16 bytes after the lists (level.py dl_trailer None) */
    return buf_set(&lv->trailer, NULL, (16 - ndl % 16) % 16);
}

uint32_t lm_dl_base(const LmLevel *lv) {
    return lv->oldBase;
}

/* ------------------------------------------------------------------ changes */
int lm_apply(LmLevel *lv, const LmChange *c, char *err, size_t errlen) {
    LmLevel *undo;
    int i, r = 0;
    if (c->kind < 0 || c->kind >= LM_KINDS) {
        seterr(err, errlen, "unknown record kind %d", c->kind);
        return -1;
    }
    if (c->nfields < 0 || c->nfields > LM_MAX_FIELDS) {
        seterr(err, errlen, "%d fields", c->nfields);
        return -1;
    }
    undo = lm_clone(lv);
    if (undo == NULL) {
        seterr(err, errlen, "out of memory");
        return -1;
    }
    switch (c->op) {
        case LM_OP_SET:
            break;
        case LM_OP_ADD:
            r = lm_insert_copy(lv, c->kind, c->index, c->copy_of, err, errlen);
            break;
        case LM_OP_DELETE:
            r = lm_delete(lv, c->kind, c->index, err, errlen);
            break;
        default:
            seterr(err, errlen, "unknown change %d", c->op);
            r = -1;
    }
    if (c->op != LM_OP_DELETE)
        for (i = 0; r == 0 && i < c->nfields; i++) r = lm_set(lv, c->kind, c->index, c->field[i], c->value[i], err, errlen);
    if (r != 0) {
        /* back to how it was */
        LmLevel tmp = *lv;
        *lv = *undo;
        *undo = tmp;
    }
    lm_free(undo);
    return r;
}

void lm_describe(const LmChange *c, char *out, size_t n) {
    size_t k;
    int i;
    const char *op = c->op == LM_OP_SET ? "set" : c->op == LM_OP_ADD ? "add" : "delete";
    if (c->op == LM_OP_ADD)
        k = (size_t) snprintf(out, n, "add %s %d (copy of %d)", lm_kind_key(c->kind), c->index, c->copy_of);
    else
        k = (size_t) snprintf(out, n, "%s %s %d", op, lm_kind_key(c->kind), c->index);
    for (i = 0; i < c->nfields && k < n; i++)
        k += (size_t) snprintf(out + k, n - k, "%s %s %ld", i ? "," : ":", lm_field_name(c->kind, c->field[i]),
                               c->value[i]);
}

/* ------------------------------------------------------------------ JSON */
/* a small reader: objects, arrays, strings, integers, true / false / null */
enum { J_NULL, J_BOOL, J_NUM, J_STR, J_ARR, J_OBJ };
typedef struct JVal JVal;
struct JVal {
    int type;
    long num;
    char *str;           /* J_STR; a member's key */
    int n;
    JVal *items;         /* J_ARR, J_OBJ (members carry key in .key) */
    char *key;
};

typedef struct {
    const char *p, *end;
    char *err;
    size_t errlen;
    int line;
} JParse;

static void jfree(JVal *v) {
    int i;
    if (v == NULL) return;
    for (i = 0; i < v->n; i++) jfree(&v->items[i]);
    free(v->items);
    free(v->str);
    free(v->key);
    memset(v, 0, sizeof *v);
}

static void jws(JParse *j) {
    while (j->p < j->end && (*j->p == ' ' || *j->p == '\t' || *j->p == '\r' || *j->p == '\n')) {
        if (*j->p == '\n') j->line++;
        j->p++;
    }
}

static int jfail(JParse *j, const char *what) {
    seterr(j->err, j->errlen, "JSON line %d: %s", j->line, what);
    return -1;
}

static int jstring(JParse *j, char **out) {
    Buf b = {0};
    if (j->p >= j->end || *j->p != '"') return jfail(j, "a string expected");
    j->p++;
    while (j->p < j->end && *j->p != '"') {
        char c = *j->p++;
        if ((unsigned char) c < 0x20) {
            free(b.p);
            return jfail(j, "a control character in a string");
        }
        if (c == '\\') {
            if (j->p >= j->end) break;
            c = *j->p++;
            switch (c) {
                case 'n': c = '\n'; break;
                case 't': c = '\t'; break;
                case 'r': c = '\r'; break;
                case 'b': c = '\b'; break;
                case 'f': c = '\f'; break;
                case 'u': {
                    unsigned u = 0, k;
                    uint8_t enc[3];
                    for (k = 0; k < 4; k++) {
                        char h = j->p < j->end ? *j->p++ : 0;
                        u = u * 16 + (unsigned) (h >= '0' && h <= '9' ? h - '0' : h >= 'a' && h <= 'f' ? h - 'a' + 10
                                                 : h >= 'A' && h <= 'F' ? h - 'A' + 10 : 0);
                    }
                    /* (UTF-8; surrogate pairs come out as two 3-byte sequences: names only) */
                    if (u < 0x80) {
                        enc[0] = (uint8_t) u;
                        if (buf_add(&b, enc, 1)) goto oom;
                    } else if (u < 0x800) {
                        enc[0] = (uint8_t) (0xC0 | u >> 6), enc[1] = (uint8_t) (0x80 | (u & 63));
                        if (buf_add(&b, enc, 2)) goto oom;
                    } else {
                        enc[0] = (uint8_t) (0xE0 | u >> 12), enc[1] = (uint8_t) (0x80 | ((u >> 6) & 63)),
                        enc[2] = (uint8_t) (0x80 | (u & 63));
                        if (buf_add(&b, enc, 3)) goto oom;
                    }
                    continue;
                }
                default: break;   /* \" \\ \/ */
            }
        }
        if (buf_add(&b, &c, 1)) goto oom;
    }
    if (j->p >= j->end) {
        free(b.p);
        return jfail(j, "a string without its end");
    }
    j->p++;
    if (buf_add(&b, "", 1)) goto oom;
    *out = (char *) b.p;
    return 0;
oom:
    free(b.p);
    return jfail(j, "out of memory");
}

static int jvalue(JParse *j, JVal *v, int depth);

static int jitems(JParse *j, JVal *v, int depth, char close) {
    j->p++;
    jws(j);
    if (j->p < j->end && *j->p == close) {
        j->p++;
        return 0;
    }
    for (;;) {
        JVal item;
        JVal *q;
        memset(&item, 0, sizeof item);
        jws(j);
        if (close == '}') {
            if (jstring(j, &item.key)) return -1;
            jws(j);
            if (j->p >= j->end || *j->p != ':') {
                free(item.key);
                return jfail(j, "':' expected");
            }
            j->p++;
        }
        if (jvalue(j, &item, depth + 1)) {
            jfree(&item);
            return -1;
        }
        q = (JVal *) realloc(v->items, sizeof(JVal) * (size_t) (v->n + 1));
        if (q == NULL) {
            jfree(&item);
            return jfail(j, "out of memory");
        }
        v->items = q;
        v->items[v->n++] = item;
        jws(j);
        if (j->p < j->end && *j->p == ',') {
            j->p++;
            continue;
        }
        if (j->p < j->end && *j->p == close) {
            j->p++;
            return 0;
        }
        return jfail(j, close == '}' ? "',' or '}' expected" : "',' or ']' expected");
    }
}

static int jvalue(JParse *j, JVal *v, int depth) {
    jws(j);
    if (depth > 32) return jfail(j, "nested too deeply");
    if (j->p >= j->end) return jfail(j, "a value expected");
    switch (*j->p) {
        case '{': v->type = J_OBJ; return jitems(j, v, depth, '}');
        case '[': v->type = J_ARR; return jitems(j, v, depth, ']');
        case '"': v->type = J_STR; return jstring(j, &v->str);
        default: break;
    }
    if (j->end - j->p >= 4 && !strncmp(j->p, "true", 4)) return v->type = J_BOOL, v->num = 1, j->p += 4, 0;
    if (j->end - j->p >= 5 && !strncmp(j->p, "false", 5)) return v->type = J_BOOL, v->num = 0, j->p += 5, 0;
    if (j->end - j->p >= 4 && !strncmp(j->p, "null", 4)) return v->type = J_NULL, j->p += 4, 0;
    if (*j->p == '-' || (*j->p >= '0' && *j->p <= '9')) {
        int neg = *j->p == '-';
        long n = 0;
        if (neg) j->p++;
        if (j->p >= j->end || *j->p < '0' || *j->p > '9') return jfail(j, "a number expected");
        while (j->p < j->end && *j->p >= '0' && *j->p <= '9') {
            if (n > 100000000L) return jfail(j, "a number too large");
            n = n * 10 + (*j->p++ - '0');
        }
        if (j->p < j->end && (*j->p == '.' || *j->p == 'e' || *j->p == 'E'))
            return jfail(j, "a whole number expected");
        v->type = J_NUM;
        v->num = neg ? -n : n;
        return 0;
    }
    return jfail(j, "a value expected");
}

static const JVal *jget(const JVal *o, const char *key) {
    int i;
    if (o == NULL || o->type != J_OBJ) return NULL;
    for (i = 0; i < o->n; i++)
        if (strcmp(o->items[i].key, key) == 0) return &o->items[i];
    return NULL;
}

/* ------------------------------------------------------------------ mods */
void lm_mod_free(LmMod *m) {
    int i;
    if (m == NULL) return;
    for (i = 0; i < m->nlevels; i++) {
        free(m->levels[i].changes);
        if (m->levels[i].geo) {
            lg_edit_free(m->levels[i].geo);
            free(m->levels[i].geo);
        }
    }
    free(m->levels);
    for (i = 0; i < m->ntextures; i++) free(m->textures[i].png);
    free(m->textures);
    memset(m, 0, sizeof *m);
}

/* ---- a bcmod/2 level's "geometry" (geometry.py apply_edit's checks on the values) ---- */
static int jint(const JVal *v, long lo, long hi, long *out) {
    if (v == NULL || v->type != J_NUM || v->num < lo || v->num > hi) return -1;
    *out = v->num;
    return 0;
}

static int jints(const JVal *a, int n, long lo, long hi, long *out) {
    int i;
    if (a == NULL || a->type != J_ARR || a->n != n) return -1;
    for (i = 0; i < n; i++)
        if (jint(&a->items[i], lo, hi, &out[i])) return -1;
    return 0;
}

static int parse_geo(const JVal *gv, LgEdit *e, int level, char *err, size_t errlen) {
    const JVal *mv = jget(gv, "move"), *rm = jget(gv, "remove"), *ad = jget(gv, "add"), *st = jget(gv, "settimg");
    static const char *kRem[4] = {"render", "ground", "walls_a", "walls_b"};
    long v[27];
    int i, k;
    memset(e, 0, sizeof *e);
    if (mv != NULL) {
        if (mv->type != J_ARR) goto bad;
        e->move = (LgMove *) calloc((size_t) mv->n + 1, sizeof(LgMove));
        if (e->move == NULL) goto oom;
        for (i = 0; i < mv->n; i++) {
            if (jints(&mv->items[i], 6, -32768, 32767, v)) {
                seterr(err, errlen, "level %d geometry: move %d: six whole numbers in -32768..32767 expected", level, i);
                return -1;
            }
            for (k = 0; k < 3; k++) e->move[i].from[k] = (int16_t) v[k], e->move[i].to[k] = (int16_t) v[3 + k];
            e->nmove++;
        }
    }
    if (rm != NULL) {
        if (rm->type != J_OBJ) goto bad;
        for (k = 0; k < 4; k++) {
            const JVal *a = jget(rm, kRem[k]);
            if (a == NULL) continue;
            if (a->type != J_ARR) goto bad;
            e->rem[k] = (int *) calloc((size_t) a->n + 1, sizeof(int));
            if (e->rem[k] == NULL) goto oom;
            for (i = 0; i < a->n; i++) {
                if (jint(&a->items[i], -100000000L, 100000000L, v)) goto bad;
                e->rem[k][e->nrem[k]++] = (int) v[0];
            }
        }
    }
    if (ad != NULL) {
        const JVal *a;
        if (ad->type != J_OBJ) goto bad;
        if ((a = jget(ad, "render")) != NULL) {
            if (a->type != J_ARR) goto bad;
            e->render = (LgAddRender *) calloc((size_t) a->n + 1, sizeof(LgAddRender));
            if (e->render == NULL) goto oom;
            for (i = 0; i < a->n; i++) {
                const JVal *o = &a->items[i], *vs = jget(o, "v"), *fl = jget(o, "flag");
                LgAddRender *r = &e->render[e->nrender];
                long af, f = 0;
                int c;
                if (jint(jget(o, "after"), -100000000L, 100000000L, &af)) {
                    seterr(err, errlen, "level %d geometry: add render %d: \"after\" (a triangle id) expected", level, i);
                    return -1;
                }
                if (vs == NULL || vs->type != J_ARR || vs->n != 3) goto badv;
                for (c = 0; c < 3; c++) {
                    long w[9];
                    int q;
                    const JVal *x = &vs->items[c];
                    if (x->type != J_ARR || x->n != 9) goto badv;
                    for (q = 0; q < 9; q++)
                        if (jint(&x->items[q], q < 5 ? -32768 : -100000000L, q < 5 ? 32767 : 100000000L, &w[q])) goto badv;
                    for (q = 0; q < 9; q++) r->v[c][q] = (int16_t) (q < 5 ? w[q] : (w[q] < -1 ? -1 : w[q] > 256 ? 256 : w[q]));
                }
                if (fl != NULL && jint(fl, -100000000L, 100000000L, &f)) goto bad;
                r->after = (int) af, r->flag = (int) (f & 0xFF);
                e->nrender++;
            }
        }
        if ((a = jget(ad, "ground")) != NULL) {
            if (a->type != J_ARR) goto bad;
            e->ground = (LgAddTri *) calloc((size_t) a->n + 1, sizeof(LgAddTri));
            if (e->ground == NULL) goto oom;
            for (i = 0; i < a->n; i++) {
                const JVal *o = &a->items[i];
                LgAddTri *t = &e->ground[e->nground];
                long sf = 1, fl = 0;
                int q;
                if (jints(jget(o, "v"), 9, -32768, 32767, v)) {
                    seterr(err, errlen, "level %d geometry: add ground %d: \"v\": nine coordinates expected", level, i);
                    return -1;
                }
                if ((jget(o, "surface") && jint(jget(o, "surface"), 0, 255, &sf)) ||
                    (jget(o, "flag") && jint(jget(o, "flag"), 0, 255, &fl))) {
                    seterr(err, errlen, "level %d geometry: add ground %d: surface / flag 0..255", level, i);
                    return -1;
                }
                for (q = 0; q < 9; q++) t->v[q] = (int16_t) v[q];
                t->a = (int) sf, t->b = (int) fl;
                e->nground++;
            }
        }
        for (k = 0; k < 2; k++) {
            a = jget(ad, k ? "walls_b" : "walls_a");
            if (a == NULL) continue;
            if (a->type != J_ARR) goto bad;
            e->wall[k] = (LgAddTri *) calloc((size_t) a->n + 1, sizeof(LgAddTri));
            if (e->wall[k] == NULL) goto oom;
            for (i = 0; i < a->n; i++) {
                const JVal *o = &a->items[i];
                LgAddTri *t = &e->wall[k][e->nwall[k]];
                long h = -1, sd = 0, fa = 0;
                int q;
                if (jints(jget(o, "v"), 9, -32768, 32767, v)) {
                    seterr(err, errlen, "level %d geometry: add %s %d: \"v\": nine coordinates expected", level,
                           k ? "walls_b" : "walls_a", i);
                    return -1;
                }
                if (jget(o, "heading") && jint(jget(o, "heading"), 0, 0xFFF, &h)) {
                    seterr(err, errlen, "level %d geometry: add %s %d: heading 0..4095", level, k ? "walls_b" : "walls_a", i);
                    return -1;
                }
                if ((jget(o, "side") && jint(jget(o, "side"), -100000000L, 100000000L, &sd)) ||
                    (jget(o, "fail") && jint(jget(o, "fail"), -100000000L, 100000000L, &fa)))
                    goto bad;
                for (q = 0; q < 9; q++) t->v[q] = (int16_t) v[q];
                t->a = (int) h, t->b = (int) (sd & 0xFF), t->c = (int) (fa & 0xFF);
                e->nwall[k]++;
            }
        }
    }
    if (st != NULL) {
        if (st->type != J_ARR) goto bad;
        e->settimg = (LgSettimg *) calloc((size_t) st->n + 1, sizeof(LgSettimg));
        if (e->settimg == NULL) goto oom;
        for (i = 0; i < st->n; i++) {
            if (jints(&st->items[i], 3, -100000000L, 100000000L, v)) goto bad;
            e->settimg[i].piece = (int) v[0], e->settimg[i].k = (int) v[1], e->settimg[i].tex = (uint32_t) v[2];
            e->nsettimg++;
        }
    }
    return 0;
badv:
    seterr(err, errlen, "level %d geometry: add render: \"v\": three [x, y, z, s, t, r, g, b, a] expected", level);
    return -1;
bad:
    seterr(err, errlen, "level %d: \"geometry\" doesn't read (see docs/tools.md, Geometry mods)", level);
    return -1;
oom:
    seterr(err, errlen, "out of memory");
    return -1;
}

static int hexsha(const JVal *v, char out[41]) {
    int i;
    out[0] = 0;
    if (v == NULL || v->type == J_NULL) return 0;
    if (v->type != J_STR || strlen(v->str) != 40) return -1;
    for (i = 0; i < 40; i++) {
        char c = v->str[i];
        if (c >= 'A' && c <= 'F') c = (char) (c - 'A' + 'a');
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return -1;
        out[i] = c;
    }
    out[40] = 0;
    return 0;
}

int lm_mod_parse(const char *text, size_t len, LmMod *m, char *err, size_t errlen) {
    JParse j;
    JVal root;
    const JVal *fmt, *lvls, *v;
    int i, k;
    memset(m, 0, sizeof *m);
    memset(&root, 0, sizeof root);
    j.p = text, j.end = text + len, j.err = err, j.errlen = errlen, j.line = 1;
    if (len >= 3 && (unsigned char) text[0] == 0xEF && (unsigned char) text[1] == 0xBB && (unsigned char) text[2] == 0xBF)
        j.p += 3;   /* (Notepad's BOM) */
    if (jvalue(&j, &root, 0)) goto fail;
    jws(&j);
    if (j.p != j.end) {
        jfail(&j, "text after the mod's object");
        goto fail;
    }
    fmt = jget(&root, "format");
    if (root.type != J_OBJ || fmt == NULL || fmt->type != J_STR ||
        (strcmp(fmt->str, "bcmod/1") != 0 && strcmp(fmt->str, "bcmod/2") != 0)) {
        seterr(err, errlen, "not a Blast Corps mod (\"format\": \"bcmod/1\" or \"bcmod/2\" missing)");
        goto fail;
    }
    m->format = strcmp(fmt->str, "bcmod/2") == 0 ? 2 : 1;
    if ((v = jget(&root, "textures")) != NULL) {
        if (m->format != 2 || v->type != J_ARR) {
            seterr(err, errlen, "geometry and textures need \"format\": \"bcmod/2\"");
            goto fail;
        }
        m->textures = (LmTexture *) calloc((size_t) v->n + 1, sizeof(LmTexture));
        if (m->textures == NULL) goto oom;
        for (i = 0; i < v->n; i++) {
            const JVal *t = &v->items[i], *id = jget(t, "id"), *png = jget(t, "png"), *sh = jget(t, "slot_sha1");
            const JVal *fm = jget(t, "fmt"), *sz = jget(t, "siz"), *nw = jget(t, "new"), *fr = jget(t, "from");
            const JVal *mp = jget(t, "mips"), *pl = jget(t, "palette");
            LmTexture *x = &m->textures[m->ntextures];
            if (t->type != J_OBJ || id == NULL || id->type != J_NUM || png == NULL || png->type != J_STR || sh == NULL ||
                sh->type != J_STR || hexsha(sh, x->slot_sha1) || x->slot_sha1[0] == 0) {
                seterr(err, errlen, "\"textures\": {\"id\", \"png\" (base64), \"slot_sha1\"} expected");
                goto fail;
            }
            if (id->num < 0 || id->num >= 0x1000) {
                seterr(err, errlen, "texture %ld: no such table entry (0..4095)", id->num);
                goto fail;
            }
            x->id = (int) id->num;
            x->fmt = fm != NULL && fm->type == J_NUM ? (int) fm->num : -1;
            x->siz = sz != NULL && sz->type == J_NUM ? (int) sz->num : -1;
            x->isNew = nw != NULL && nw->type == J_BOOL && nw->num != 0;
            x->from = fr != NULL && fr->type == J_NUM && fr->num >= 0 && fr->num < 0x1000 ? (int) fr->num : -1;
            if (mp != NULL && (mp->type != J_NUM || mp->num < 0 || mp->num > 12)) {
                seterr(err, errlen, "texture %d: \"mips\": 0..12 expected", x->id);
                goto fail;
            }
            x->mips = mp != NULL ? (int) mp->num : 0;
            if (pl != NULL && (pl->type != J_NUM || pl->num < 0 || pl->num >= 0x1000)) {
                seterr(err, errlen, "texture %d: \"palette\": a table entry 0..4095 expected", x->id);
                goto fail;
            }
            x->hasPalette = pl != NULL, x->palette = pl != NULL ? (int) pl->num : 0;
            x->png = (char *) malloc(strlen(png->str) + 1);
            if (x->png == NULL) goto oom;
            strcpy(x->png, png->str);
            m->ntextures++;
            for (k = 0; k + 1 < m->ntextures; k++)
                if (m->textures[k].id == x->id) {
                    seterr(err, errlen, "texture %d is listed twice", x->id);
                    goto fail;
                }
        }
    }
    if ((v = jget(&root, "name")) != NULL && v->type == J_STR) snprintf(m->name, sizeof m->name, "%s", v->str);
    if ((v = jget(&root, "description")) != NULL && v->type == J_STR)
        snprintf(m->description, sizeof m->description, "%s", v->str);
    lvls = jget(&root, "levels");
    if (lvls == NULL || lvls->type != J_ARR) {
        seterr(err, errlen, "\"levels\": a list expected");
        goto fail;
    }
    m->levels = (LmModLevel *) calloc((size_t) (lvls->n ? lvls->n : 1), sizeof(LmModLevel));
    if (m->levels == NULL) goto oom;
    for (i = 0; i < lvls->n; i++) {
        const JVal *L = &lvls->items[i], *ch;
        LmModLevel *ml = &m->levels[m->nlevels++];
        v = jget(L, "level");
        if (L->type != J_OBJ || v == NULL || v->type != J_NUM || v->num < 0 || v->num > 59) {
            seterr(err, errlen, "levels[%d]: \"level\" (0..59) expected", i);
            goto fail;
        }
        ml->level = (int) v->num;
        for (k = 0; k < i; k++)
            if (m->levels[k].level == ml->level) {
                seterr(err, errlen, "level %d is listed twice", ml->level);
                goto fail;
            }
        if (hexsha(jget(L, "original_sha1"), ml->original_sha1) || ml->original_sha1[0] == 0) {
            seterr(err, errlen, "level %d: \"original_sha1\" (40 hex digits) expected", ml->level);
            goto fail;
        }
        if (hexsha(jget(L, "edited_sha1"), ml->edited_sha1)) {
            seterr(err, errlen, "level %d: \"edited_sha1\" is not 40 hex digits", ml->level);
            goto fail;
        }
        ch = jget(L, "changes");
        if ((v = jget(L, "geometry")) != NULL) {
            if (m->format != 2) {
                seterr(err, errlen, "geometry and textures need \"format\": \"bcmod/2\"");
                goto fail;
            }
            if (v->type != J_OBJ) {
                seterr(err, errlen, "level %d: \"geometry\": an object expected", ml->level);
                goto fail;
            }
            ml->geo = (LgEdit *) calloc(1, sizeof(LgEdit));
            if (ml->geo == NULL) goto oom;
            if (parse_geo(v, ml->geo, ml->level, err, errlen)) goto fail;
        }
        if (ch == NULL && ml->geo != NULL) {
            ml->changes = (LmChange *) calloc(1, sizeof(LmChange));
            if (ml->changes == NULL) goto oom;
            continue;
        }
        if (ch == NULL || ch->type != J_ARR) {
            seterr(err, errlen, "level %d: \"changes\": a list expected", ml->level);
            goto fail;
        }
        ml->changes = (LmChange *) calloc((size_t) (ch->n ? ch->n : 1), sizeof(LmChange));
        if (ml->changes == NULL) goto oom;
        for (k = 0; k < ch->n; k++) {
            const JVal *c = &ch->items[k], *op = jget(c, "op"), *kind = jget(c, "kind"), *ix = jget(c, "index"),
                       *cp = jget(c, "copy_of"), *fl = jget(c, "fields");
            LmChange *x = &ml->changes[ml->nchanges];
            int f;
            if (c->type != J_OBJ || op == NULL || op->type != J_STR || kind == NULL || kind->type != J_STR ||
                ix == NULL || ix->type != J_NUM) {
                seterr(err, errlen, "level %d change %d: \"op\", \"kind\" and \"index\" expected", ml->level, k);
                goto fail;
            }
            x->op = !strcmp(op->str, "set") ? LM_OP_SET : !strcmp(op->str, "add") ? LM_OP_ADD
                    : !strcmp(op->str, "delete") ? LM_OP_DELETE : -1;
            x->kind = lm_kind_from_key(kind->str);
            x->index = (int) ix->num;
            if (x->op < 0 || x->kind < 0) {
                seterr(err, errlen, "level %d change %d: op \"%s\" kind \"%s\" not known", ml->level, k, op->str,
                       kind->str);
                goto fail;
            }
            if (x->op == LM_OP_ADD) {
                if (cp == NULL || cp->type != J_NUM) {
                    seterr(err, errlen, "level %d change %d: an add needs \"copy_of\"", ml->level, k);
                    goto fail;
                }
                x->copy_of = (int) cp->num;
            }
            if (fl != NULL && fl->type != J_OBJ) {
                seterr(err, errlen, "level %d change %d: \"fields\": an object expected", ml->level, k);
                goto fail;
            }
            for (f = 0; fl != NULL && f < fl->n; f++) {
                const int fi = lm_field_index(x->kind, fl->items[f].key);
                if (fi < 0 || fl->items[f].type != J_NUM || x->nfields >= LM_MAX_FIELDS) {
                    seterr(err, errlen, "level %d change %d: field \"%s\" of %s (a number) not known", ml->level, k,
                           fl->items[f].key, kind->str);
                    goto fail;
                }
                x->field[x->nfields] = fi;
                x->value[x->nfields++] = fl->items[f].num;
            }
            ml->nchanges++;
        }
    }
    jfree(&root);
    return 0;
oom:
    seterr(err, errlen, "out of memory");
fail:
    jfree(&root);
    lm_mod_free(m);
    return -1;
}

static void jesc(Buf *b, const char *s) {
    buf_add(b, "\"", 1);
    for (; *s; s++) {
        char e[8];
        if (*s == '"' || *s == '\\') {
            e[0] = '\\', e[1] = *s;
            buf_add(b, e, 2);
        } else if ((unsigned char) *s < 0x20) {
            snprintf(e, sizeof e, "\\u%04x", (unsigned char) *s);
            buf_add(b, e, 6);
        } else {
            buf_add(b, s, 1);
        }
    }
    buf_add(b, "\"", 1);
}

static void jputs(Buf *b, const char *s) {
    buf_add(b, s, strlen(s));
}

/* a geometry edit as parse_geo reads it */
static void geo_json(Buf *b, const LgEdit *e) {
    static const char *kRem[4] = {"render", "ground", "walls_a", "walls_b"};
    char line[400];
    int i, k, c, any;
    jputs(b, "{");
    any = 0;
    if (e->nmove) {
        jputs(b, "\n       \"move\": [");
        for (i = 0; i < e->nmove; i++) {
            const LgMove *m = &e->move[i];
            snprintf(line, sizeof line, "%s[%d, %d, %d, %d, %d, %d]", i ? (i % 4 ? ", " : ",\n        ") : "", m->from[0],
                     m->from[1], m->from[2], m->to[0], m->to[1], m->to[2]);
            jputs(b, line);
        }
        jputs(b, "]");
        any = 1;
    }
    if (e->nrem[0] || e->nrem[1] || e->nrem[2] || e->nrem[3]) {
        jputs(b, any ? ",\n       \"remove\": {" : "\n       \"remove\": {");
        for (k = 0; k < 4; k++) {
            snprintf(line, sizeof line, "%s\"%s\": [", k ? ", " : "", kRem[k]);
            jputs(b, line);
            for (i = 0; i < e->nrem[k]; i++) {
                snprintf(line, sizeof line, "%s%d", i ? ", " : "", e->rem[k][i]);
                jputs(b, line);
            }
            jputs(b, "]");
        }
        jputs(b, "}");
        any = 1;
    }
    if (e->nrender || e->nground || e->nwall[0] || e->nwall[1]) {
        int first = 1;
        jputs(b, any ? ",\n       \"add\": {" : "\n       \"add\": {");
        if (e->nrender) {
            jputs(b, "\"render\": [");
            for (i = 0; i < e->nrender; i++) {
                const LgAddRender *r = &e->render[i];
                snprintf(line, sizeof line, "%s\n         {\"after\": %d, \"v\": [", i ? "," : "", r->after);
                jputs(b, line);
                for (c = 0; c < 3; c++) {
                    snprintf(line, sizeof line, "%s[%d, %d, %d, %d, %d, %d, %d, %d, %d]", c ? ", " : "", r->v[c][0],
                             r->v[c][1], r->v[c][2], r->v[c][3], r->v[c][4], r->v[c][5], r->v[c][6], r->v[c][7], r->v[c][8]);
                    jputs(b, line);
                }
                snprintf(line, sizeof line, "], \"flag\": %d}", r->flag);
                jputs(b, line);
            }
            jputs(b, "]");
            first = 0;
        }
        if (e->nground) {
            jputs(b, first ? "\"ground\": [" : ",\n        \"ground\": [");
            for (i = 0; i < e->nground; i++) {
                const LgAddTri *t = &e->ground[i];
                snprintf(line, sizeof line, "%s\n         {\"v\": [%d, %d, %d, %d, %d, %d, %d, %d, %d], \"surface\": %d, \"flag\": %d}",
                         i ? "," : "", t->v[0], t->v[1], t->v[2], t->v[3], t->v[4], t->v[5], t->v[6], t->v[7], t->v[8], t->a,
                         t->b);
                jputs(b, line);
            }
            jputs(b, "]");
            first = 0;
        }
        for (k = 0; k < 2; k++) {
            if (!e->nwall[k]) continue;
            jputs(b, first ? (k ? "\"walls_b\": [" : "\"walls_a\": [") : (k ? ",\n        \"walls_b\": [" : ",\n        \"walls_a\": ["));
            for (i = 0; i < e->nwall[k]; i++) {
                const LgAddTri *t = &e->wall[k][i];
                snprintf(line, sizeof line, "%s\n         {\"v\": [%d, %d, %d, %d, %d, %d, %d, %d, %d]", i ? "," : "",
                         t->v[0], t->v[1], t->v[2], t->v[3], t->v[4], t->v[5], t->v[6], t->v[7], t->v[8]);
                jputs(b, line);
                if (t->a >= 0) {
                    snprintf(line, sizeof line, ", \"heading\": %d", t->a);
                    jputs(b, line);
                }
                snprintf(line, sizeof line, ", \"side\": %d, \"fail\": %d}", t->b, t->c);
                jputs(b, line);
            }
            jputs(b, "]");
            first = 0;
        }
        jputs(b, "}");
        any = 1;
    }
    if (e->nsettimg) {
        jputs(b, any ? ",\n       \"settimg\": [" : "\n       \"settimg\": [");
        for (i = 0; i < e->nsettimg; i++) {
            snprintf(line, sizeof line, "%s[%d, %d, %u]", i ? ", " : "", e->settimg[i].piece, e->settimg[i].k,
                     (unsigned) e->settimg[i].tex);
            jputs(b, line);
        }
        jputs(b, "]");
    }
    jputs(b, "}");
}

static void *dupmem(const void *p, size_t n) {
    void *q = malloc(n ? n : 1);
    if (q && n) memcpy(q, p, n);
    return q;
}

int lm_geo_copy(LgEdit *d, const LgEdit *s) {
    int k;
    memset(d, 0, sizeof *d);
    if (s == NULL) return 0;
    *d = *s;
    d->move = (LgMove *) dupmem(s->move, sizeof(LgMove) * (size_t) s->nmove);
    for (k = 0; k < 4; k++) d->rem[k] = (int *) dupmem(s->rem[k], sizeof(int) * (size_t) s->nrem[k]);
    d->render = (LgAddRender *) dupmem(s->render, sizeof(LgAddRender) * (size_t) s->nrender);
    d->ground = (LgAddTri *) dupmem(s->ground, sizeof(LgAddTri) * (size_t) s->nground);
    for (k = 0; k < 2; k++) d->wall[k] = (LgAddTri *) dupmem(s->wall[k], sizeof(LgAddTri) * (size_t) s->nwall[k]);
    d->settimg = (LgSettimg *) dupmem(s->settimg, sizeof(LgSettimg) * (size_t) s->nsettimg);
    if (!d->move || !d->rem[0] || !d->rem[1] || !d->rem[2] || !d->rem[3] || !d->render || !d->ground || !d->wall[0] ||
        !d->wall[1] || !d->settimg) {
        lg_edit_free(d);
        return -1;
    }
    return 0;
}

char *lm_mod_write(const LmMod *m) {
    Buf b = {0};
    int i, k, f, v2 = m->ntextures > 0;
    char line[256];
    for (i = 0; i < m->nlevels; i++)
        if (m->levels[i].geo != NULL && !lg_edit_empty(m->levels[i].geo)) v2 = 1;
    jputs(&b, v2 ? "{\n  \"format\": \"bcmod/2\",\n  \"name\": " : "{\n  \"format\": \"bcmod/1\",\n  \"name\": ");
    jesc(&b, m->name);
    jputs(&b, ",\n  \"description\": ");
    jesc(&b, m->description);
    jputs(&b, ",\n  \"levels\": [");
    for (i = 0; i < m->nlevels; i++) {
        const LmModLevel *ml = &m->levels[i];
        snprintf(line, sizeof line, "%s\n    {\"level\": %d, \"original_sha1\": \"%s\"", i ? "," : "", ml->level,
                 ml->original_sha1);
        jputs(&b, line);
        if (ml->edited_sha1[0]) {
            snprintf(line, sizeof line, ", \"edited_sha1\": \"%s\"", ml->edited_sha1);
            jputs(&b, line);
        }
        jputs(&b, ",\n     \"changes\": [");
        for (k = 0; k < ml->nchanges; k++) {
            const LmChange *c = &ml->changes[k];
            const char *op = c->op == LM_OP_SET ? "set" : c->op == LM_OP_ADD ? "add" : "delete";
            snprintf(line, sizeof line, "%s\n       {\"op\": \"%s\", \"kind\": \"%s\", \"index\": %d", k ? "," : "", op,
                     lm_kind_key(c->kind), c->index);
            jputs(&b, line);
            if (c->op == LM_OP_ADD) {
                snprintf(line, sizeof line, ", \"copy_of\": %d", c->copy_of);
                jputs(&b, line);
            }
            if (c->op != LM_OP_DELETE && c->nfields > 0) {
                jputs(&b, ", \"fields\": {");
                for (f = 0; f < c->nfields; f++) {
                    snprintf(line, sizeof line, "%s\"%s\": %ld", f ? ", " : "", lm_field_name(c->kind, c->field[f]),
                             c->value[f]);
                    jputs(&b, line);
                }
                jputs(&b, "}");
            }
            jputs(&b, "}");
        }
        jputs(&b, ml->nchanges ? "\n     ]" : "]");
        if (ml->geo != NULL && !lg_edit_empty(ml->geo)) {
            jputs(&b, ",\n     \"geometry\": ");
            geo_json(&b, ml->geo);
        }
        jputs(&b, "}");
    }
    jputs(&b, m->nlevels ? "\n  ]" : "]");
    if (m->ntextures > 0) {
        jputs(&b, ",\n  \"textures\": [");
        for (i = 0; i < m->ntextures; i++) {
            const LmTexture *t = &m->textures[i];
            char from[32] = "", mips[48] = "";
            if (t->isNew && t->from >= 0) snprintf(from, sizeof from, ", \"from\": %d", t->from);
            if (t->mips > 1) snprintf(mips, sizeof mips, ", \"mips\": %d", t->mips);
            if (t->hasPalette) snprintf(mips + strlen(mips), sizeof mips - strlen(mips), ", \"palette\": %d", t->palette);
            snprintf(line, sizeof line, "%s\n    {\"id\": %d, \"fmt\": %d, \"siz\": %d, \"slot_sha1\": \"%s\"%s%s%s, \"png\": ",
                     i ? "," : "", t->id, t->fmt, t->siz, t->slot_sha1, mips, t->isNew ? ", \"new\": true" : "", from);
            jputs(&b, line);
            jesc(&b, t->png ? t->png : "");
            jputs(&b, "}");
        }
        jputs(&b, "\n  ]");
    }
    jputs(&b, "\n}\n");
    if (buf_add(&b, "", 1)) {
        free(b.p);
        return NULL;
    }
    return (char *) b.p;
}

int lm_mod_apply(const LmModLevel *ml, const uint8_t *orig, size_t size, uint8_t **out, size_t *outsize,
                 char *err, size_t errlen) {
    char sha[41], what[160];
    LmLevel *lv;
    int k;
    *out = NULL;
    lm_sha1_hex(orig, size, sha);
    if (strcmp(sha, ml->original_sha1) != 0) {
        seterr(err, errlen, "level %d: made for another level file (SHA-1 %s, this ROM's is %s): not your ROM's "
                            "version, or another mod changed it", ml->level, ml->original_sha1, sha);
        return -1;
    }
    lv = lm_open(orig, size, err, errlen);
    if (lv == NULL) return -1;
    for (k = 0; k < ml->nchanges; k++) {
        char e[256];
        if (lm_apply(lv, &ml->changes[k], e, sizeof e)) {
            lm_describe(&ml->changes[k], what, sizeof what);
            seterr(err, errlen, "level %d change %d (%s): %s", ml->level, k, what, e);
            lm_free(lv);
            return -1;
        }
    }
    if (lm_check(lv, what, sizeof what)) {
        seterr(err, errlen, "level %d: %s", ml->level, what);
        lm_free(lv);
        return -1;
    }
    *out = lm_build(lv, outsize, NULL);
    lm_free(lv);
    if (*out == NULL) {
        seterr(err, errlen, "out of memory");
        return -1;
    }
    if (ml->geo != NULL) {
        /* the geometry, over the record changes: every derived section regenerated (level_geo.c) */
        uint8_t *g = NULL;
        size_t gn = 0;
        char e[300];
        if (lg_apply(*out, *outsize, orig, size, ml->geo, ml->level, &g, &gn, e, sizeof e)) {
            seterr(err, errlen, "level %d geometry: %s", ml->level, e);
            free(*out);
            *out = NULL;
            return -1;
        }
        free(*out);
        *out = g, *outsize = gn;
    }
    if (ml->edited_sha1[0]) {
        lm_sha1_hex(*out, *outsize, sha);
        if (strcmp(sha, ml->edited_sha1) != 0) {
            seterr(err, errlen, "level %d: the edited file came out with SHA-1 %s, the mod says %s", ml->level, sha,
                   ml->edited_sha1);
            free(*out);
            *out = NULL;
            return -1;
        }
    }
    return 0;
}
