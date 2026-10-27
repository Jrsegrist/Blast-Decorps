/* Level geometry: the triangles and their derived sections (see level_geo.h).
 * Written for the port from the formats in docs/systems/levels.md and
 * mirrors tools_port/bcfiles/geometry.py step by step (same orders, same
 * rounding), so the two build the same bytes. */
#include "level_geo.h"
#include "level_mod.h"
#include "host_util.h"

#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------ helpers */
static int s16at(const uint8_t *p) {
    return (int) (int16_t) get_be16(p);
}

static int buf_u32(Buf *b, uint32_t v) {
    uint8_t q[4];
    put_be32(q, v);
    return buf_add(b, q, 4);
}
static int buf_u16(Buf *b, unsigned v) {
    uint8_t q[2];
    put_be16(q, v);
    return buf_add(b, q, 2);
}

/* grow-on-demand arrays */
#define VEC(T) struct { T *a; int n, cap; }
#define VPUSH(v, x) (((v).n < (v).cap || vgrow((void **) &(v).a, &(v).cap, sizeof *(v).a, (v).n + 1) == 0) \
                         ? ((v).a[(v).n++] = (x), 0) : -1)
typedef VEC(int) IntVec;
typedef VEC(uint32_t) U32Vec;
typedef VEC(int16_t) I16Vec;
typedef VEC(uint64_t) U64Vec;
static int vgrow(void **a, int *cap, size_t sz, int need) {
    int c = *cap ? *cap : 16;
    void *q;
    while (c < need) c *= 2;
    q = realloc(*a, sz * (size_t) c);
    if (q == NULL) return -1;
    *a = q, *cap = c;
    return 0;
}

static uint64_t fnv(const void *p, size_t n, uint64_t h) {
    const uint8_t *b = (const uint8_t *) p;
    size_t i;
    for (i = 0; i < n; i++) h = (h ^ b[i]) * 1099511628211ull;
    return h;
}
#define FNV0 1469598103934665603ull

/* a hash set/map of fixed-size byte keys -> int (open addressing) */
typedef struct {
    int ksize, cap, n;
    uint8_t *keys;
    int *vals;
    uint8_t *used;
} Map;
static void map_free(Map *m) {
    free(m->keys), free(m->vals), free(m->used);
    memset(m, 0, sizeof *m);
}
static int map_init(Map *m, int ksize, int expect) {
    int cap = 64;
    while (cap < expect * 2 + 16) cap *= 2;
    memset(m, 0, sizeof *m);
    m->ksize = ksize, m->cap = cap;
    m->keys = (uint8_t *) calloc((size_t) cap, (size_t) ksize);
    m->vals = (int *) calloc((size_t) cap, sizeof(int));
    m->used = (uint8_t *) calloc((size_t) cap, 1);
    return m->keys && m->vals && m->used ? 0 : -1;
}
static int *map_find(const Map *m, const void *k) {
    uint64_t h = fnv(k, (size_t) m->ksize, FNV0);
    int i = (int) (h & (uint64_t) (m->cap - 1));
    while (m->used[i]) {
        if (!memcmp(m->keys + (size_t) i * (size_t) m->ksize, k, (size_t) m->ksize)) return &m->vals[i];
        i = (i + 1) & (m->cap - 1);
    }
    return NULL;
}
static int map_put(Map *m, const void *k, int v) {
    uint64_t h;
    int i;
    if ((m->n + 1) * 2 > m->cap) {
        Map b;
        int j;
        if (map_init(&b, m->ksize, m->cap)) return -1;
        for (j = 0; j < m->cap; j++)
            if (m->used[j]) map_put(&b, m->keys + (size_t) j * (size_t) m->ksize, m->vals[j]);
        map_free(m);
        *m = b;
    }
    h = fnv(k, (size_t) m->ksize, FNV0);
    i = (int) (h & (uint64_t) (m->cap - 1));
    while (m->used[i]) {
        if (!memcmp(m->keys + (size_t) i * (size_t) m->ksize, k, (size_t) m->ksize)) {
            m->vals[i] = v;
            return 0;
        }
        i = (i + 1) & (m->cap - 1);
    }
    m->used[i] = 1;
    memcpy(m->keys + (size_t) i * (size_t) m->ksize, k, (size_t) m->ksize);
    m->vals[i] = v;
    m->n++;
    return 0;
}

/* ------------------------------------------------------------------ vertices and triangles */
typedef struct {
    uint8_t b[16];   /* a Vtx as the file has it (BE): x y z flag s t r g b a */
} Vx;
static int vx_x(const Vx *v) { return s16at(v->b); }
static int vx_y(const Vx *v) { return s16at(v->b + 2); }
static int vx_z(const Vx *v) { return s16at(v->b + 4); }
static void vx_setpos(Vx *v, int x, int y, int z) {
    put_be16(v->b, (unsigned) x & 0xFFFF), put_be16(v->b + 2, (unsigned) y & 0xFFFF), put_be16(v->b + 4, (unsigned) z & 0xFFFF);
}

typedef struct {
    int16_t v[9];
    uint8_t x[3];    /* ground: surface, flag; walls: heading (2 bytes BE), side, b15 -> x[0..1] heading, x[2] side */
    uint8_t x3;      /* walls: b15 */
} Tri;               /* ground: v + surface (x[0]) + flag (x[1]); walls: v + heading + side + b15 */
typedef VEC(Tri) TriVec;
typedef VEC(Vx) VxVec;

enum { OP_C, OP_DL, OP_T, OP_VTX, OP_TRI, OP_CULL };
typedef struct {
    int kind;
    uint32_t w0, w1;     /* C: the command; DL: w0, target; T: w0 (w1 = flag); TRI: the command */
    int nv;
    Vx *v;               /* T/TRI: 3; VTX: n; CULL: the 8 box vertices (as the file had them) */
    int off;             /* VTX: the vertex index it loaded */
    uint8_t pat[8][3];   /* CULL: min/max per axis of each box corner */
    uint32_t cw0, cw1;   /* CULL: the G_CULLDL command */
} Op;
typedef VEC(Op) OpVec;

enum { P_PREFIX, P_PACKED, P_REC, P_GRP, P_SUB, P_FIXED, P_RAW };
typedef struct {
    int kind;
    uint32_t start, end;
    int refk, refi;
    int setup, setup0;
    int frozen, restructured, hascull;
    OpVec ops;
    OpVec orig;
} Piece;
typedef VEC(Piece) PieceVec;

typedef struct {
    PieceVec pc;
    TriVec ground;
    TriVec walls[2];
    TriVec tieT[2];    /* wall_ties: Rare's walls at a grown cell's high edge (tieT[k].a[i] in cell tieC[k].a[i]) */
    IntVec tieC[2];
    VxVec overts;      /* the file's segment-8 vertices */
    Map moves;           /* (x, y, z) -> index into movedTo */
    I16Vec movedTo;
} Geo;

static void op_free(Op *o) {
    free(o->v);
    o->v = NULL;
}
static Op op_copy(const Op *o) {
    Op c = *o;
    if (o->nv) {
        c.v = (Vx *) malloc(sizeof(Vx) * (size_t) o->nv);
        if (c.v) memcpy(c.v, o->v, sizeof(Vx) * (size_t) o->nv);
    } else {
        c.v = NULL;
    }
    return c;
}
static int op_eq(const Op *a, const Op *b) {
    if (a->kind != b->kind || a->w0 != b->w0 || a->nv != b->nv) return 0;
    switch (a->kind) {
        case OP_C: case OP_DL: case OP_TRI: case OP_T:
            if (a->w1 != b->w1) return 0;
            break;
        case OP_VTX:
            if (a->off != b->off) return 0;
            break;
        case OP_CULL:
            if (memcmp(a->pat, b->pat, sizeof a->pat) || a->cw0 != b->cw0 || a->cw1 != b->cw1) return 0;
            break;
    }
    return a->nv == 0 || !memcmp(a->v, b->v, sizeof(Vx) * (size_t) a->nv);
}
static void piece_free(Piece *p) {
    int i;
    for (i = 0; i < p->ops.n; i++) op_free(&p->ops.a[i]);
    for (i = 0; i < p->orig.n; i++) op_free(&p->orig.a[i]);
    free(p->ops.a), free(p->orig.a);
}
static void geo_free(Geo *g) {
    int i;
    for (i = 0; i < g->pc.n; i++) piece_free(&g->pc.a[i]);
    free(g->pc.a), free(g->ground.a), free(g->walls[0].a), free(g->walls[1].a), free(g->overts.a);
    free(g->tieT[0].a), free(g->tieT[1].a), free(g->tieC[0].a), free(g->tieC[1].a);
    map_free(&g->moves);
    free(g->movedTo.a);
    memset(g, 0, sizeof *g);
}

/* the parsed file: header words, the sections the geometry needs */
typedef struct {
    const uint8_t *f;
    size_t size;
    uint32_t H[0x2A];       /* header words 0x20 .. 0xC4 */
    unsigned u16[12];       /* header u16s */
    int32_t dlprofile;
    uint32_t base, dlend;   /* the display-list block */
    /* visibility records / groups: absolute offsets as in the file */
    U32Vec rec[4];   /* 5 words each */
    U32Vec grp[4];   /* start alt end id n cells... */
    IntVec grpAt[4];      /* where each group starts in grp[k] */
} File;
#define HW(fl, slot) ((fl)->H[((slot) - 0x20) / 4])

static void file_free(File *fl) {
    int k;
    for (k = 0; k < 4; k++) free(fl->rec[k].a), free(fl->grp[k].a), free(fl->grpAt[k].a);
}

static int file_open(File *fl, const uint8_t *f, size_t size, char *err, size_t errlen) {
    int i, k;
    memset(fl, 0, sizeof *fl);
    fl->f = f, fl->size = size;
    if (size < 0xC8) {
        seterr(err, errlen, "not a level file");
        return -1;
    }
    for (i = 0; i < 0x2A; i++) fl->H[i] = get_be32(f + 0x20 + 4 * i);
    for (i = 0; i < 12; i++) fl->u16[i] = get_be16(f + 2 * i);
    fl->dlprofile = (int32_t) get_be32(f + 0x1C);
    fl->base = HW(fl, 0x78), fl->dlend = HW(fl, 0x84);
    if (fl->dlend < fl->base || fl->dlend > size || HW(fl, 0x20) < 0xC8 || HW(fl, 0x20) > size) {
        seterr(err, errlen, "the header's display-list or vertex offsets are outside the file");
        return -1;
    }
    for (k = 0; k < 4; k++) {
        uint32_t lo = HW(fl, 0xA0 + 4 * k), hi = HW(fl, 0xA4 + 4 * k), p;
        for (p = lo; p + 0x14 <= hi && hi <= size; p += 0x14)
            for (i = 0; i < 5; i++)
                if (VPUSH(fl->rec[k], get_be32(f + p + 4 * i))) return -1;
        lo = HW(fl, 0xB0 + 4 * k), hi = HW(fl, 0xB4 + 4 * k);
        for (p = lo; p + 0x14 <= hi && hi <= size;) {
            int32_t n = (int32_t) get_be32(f + p + 0x10);
            if (n < 0 || p + 0x14 + 4 * (uint32_t) n > hi) {
                seterr(err, errlen, "visibility group past its range");
                return -1;
            }
            if (VPUSH(fl->grpAt[k], fl->grp[k].n)) return -1;
            for (i = 0; i < 5 + n; i++)
                if (VPUSH(fl->grp[k], get_be32(f + p + 4 * i))) return -1;
            p += 0x14 + 4 * (uint32_t) n;
        }
    }
    return 0;
}

/* a grid section: per cell a BE u32 end (from the grid's start) and tsize-byte triangles */
static int parse_grid(const File *fl, unsigned slot, unsigned nextslot, unsigned ncells, int tsize, TriVec * cellsTri,
                      IntVec * cellStart, char *err, size_t errlen) {
    const uint8_t *f = fl->f;
    uint32_t lo = HW(fl, slot), hi = HW(fl, nextslot), p = lo;
    unsigned c;
    for (c = 0; c < ncells; c++) {
        uint32_t e;
        if (p + 4 > hi) {
            seterr(err, errlen, "grid 0x%X: cell %u past the section", slot, c);
            return -1;
        }
        e = lo + get_be32(f + p);
        if (e < p + 4 || e > hi || (e - p - 4) % (uint32_t) tsize) {
            seterr(err, errlen, "grid 0x%X: cell %u ends at 0x%X", slot, c, (unsigned) e);
            return -1;
        }
        if (VPUSH(*cellStart, cellsTri->n)) return -1;
        for (p += 4; p < e; p += (uint32_t) tsize) {
            Tri t;
            int k;
            memset(&t, 0, sizeof t);
            for (k = 0; k < 9; k++) t.v[k] = (int16_t) s16at(f + p + 2 * k);
            if (tsize == 0x14) {
                t.x[0] = f[p + 0x12], t.x[1] = f[p + 0x13];
            } else {
                t.x[0] = f[p + 0x12], t.x[1] = f[p + 0x13], t.x[2] = f[p + 0x14], t.x3 = f[p + 0x15];
            }
            if (VPUSH(*cellsTri, t)) return -1;
        }
    }
    if (VPUSH(*cellStart, cellsTri->n)) return -1;
    return 0;
}

static int tri_eq(const Tri *a, const Tri *b) {
    return !memcmp(a->v, b->v, sizeof a->v) && !memcmp(a->x, b->x, sizeof a->x) && a->x3 == b->x3;
}

/* Rare's per-cell lists -> one global order keeping every cell's (grid_order) */
typedef struct {
    Tri t;
    int occ;
} TKey;
typedef VEC(TKey) TKeyVec;
static int grid_order(const TriVec * cells, const IntVec * cs, TriVec * out) {
    Map idx, edges;
    TKeyVec keys = {0};
    IntVec succA = {0}, succB = {0};
    int *indeg = NULL, *heap = NULL, nh = 0, i, c, r = -1;
    int ncell = cs->n - 1;
    int *cellKeys = NULL;
    IntVec adjStart = {0};
    int *adj = NULL;
    if (map_init(&idx, sizeof(TKey), cells->n) || map_init(&edges, 8, cells->n)) return -1;
    cellKeys = (int *) malloc(sizeof(int) * (size_t) (cells->n + 1));
    if (cellKeys == NULL) goto done;
    for (c = 0; c < ncell; c++) {
        /* occurrences within this cell */
        for (i = cs->a[c]; i < cs->a[c + 1]; i++) {
            TKey k;
            int j, *v;
            memset(&k, 0, sizeof k);
            k.t = cells->a[i];
            k.occ = 0;
            for (j = cs->a[c]; j < i; j++)
                if (tri_eq(&cells->a[j], &cells->a[i])) k.occ++;
            v = map_find(&idx, &k);
            if (v == NULL) {
                if (map_put(&idx, &k, keys.n) || VPUSH(keys, k)) goto done;
                cellKeys[i] = keys.n - 1;
            } else {
                cellKeys[i] = *v;
            }
            if (i > cs->a[c]) {
                int e[2];
                e[0] = cellKeys[i - 1], e[1] = cellKeys[i];
                if (map_find(&edges, e) == NULL) {
                    if (map_put(&edges, e, 1) || VPUSH(succA, e[0]) || VPUSH(succB, e[1])) goto done;
                }
            }
        }
    }
    indeg = (int *) calloc((size_t) keys.n + 1, sizeof(int));
    heap = (int *) malloc(sizeof(int) * ((size_t) keys.n + 1));
    if (indeg == NULL || heap == NULL) goto done;
    /* adjacency in the edges' first-seen order (Python's set iteration doesn't matter: ties go by index) */
    {
        int *cnt = (int *) calloc((size_t) keys.n + 1, sizeof(int));
        if (cnt == NULL) goto done;
        for (i = 0; i < succA.n; i++) cnt[succA.a[i]]++, indeg[succB.a[i]]++;
        if (vgrow((void **) &adjStart.a, &adjStart.cap, sizeof(int), keys.n + 1)) {
            free(cnt);
            goto done;
        }
        adjStart.n = keys.n + 1;
        adjStart.a[0] = 0;
        for (i = 0; i < keys.n; i++) adjStart.a[i + 1] = adjStart.a[i] + cnt[i];
        adj = (int *) malloc(sizeof(int) * ((size_t) succA.n + 1));
        if (adj == NULL) {
            free(cnt);
            goto done;
        }
        memset(cnt, 0, sizeof(int) * ((size_t) keys.n + 1));
        for (i = 0; i < succA.n; i++) adj[adjStart.a[succA.a[i]] + cnt[succA.a[i]]++] = succB.a[i];
        free(cnt);
    }
    /* Kahn, the smallest first-appearance index first (a min-heap of indexes) */
#define HPUSH(x) do { int q_ = nh++; heap[q_] = (x); while (q_ && heap[(q_ - 1) / 2] > heap[q_]) { int t_ = heap[q_]; \
        heap[q_] = heap[(q_ - 1) / 2]; heap[(q_ - 1) / 2] = t_; q_ = (q_ - 1) / 2; } } while (0)
    for (i = 0; i < keys.n; i++)
        if (indeg[i] == 0) HPUSH(i);
    out->n = 0;
    {
        int done_ = 0;
        while (nh) {
            int top = heap[0], q = 0, e;
            heap[0] = heap[--nh];
            for (;;) {
                int l = 2 * q + 1, rr = l + 1, m = q;
                if (l < nh && heap[l] < heap[m]) m = l;
                if (rr < nh && heap[rr] < heap[m]) m = rr;
                if (m == q) break;
                {
                    int t_ = heap[q];
                    heap[q] = heap[m], heap[m] = t_;
                }
                q = m;
            }
            if (VPUSH(*out, keys.a[top].t)) goto done;
            done_++;
            for (e = adjStart.a[top]; e < adjStart.a[top + 1]; e++)
                if (--indeg[adj[e]] == 0) HPUSH(adj[e]);
        }
        if (done_ != keys.n) {
            /* no order keeps every cell's: first appearance */
            out->n = 0;
            for (i = 0; i < keys.n; i++)
                if (VPUSH(*out, keys.a[i].t)) goto done;
        }
    }
#undef HPUSH
    r = 0;
done:
    map_free(&idx), map_free(&edges);
    free(keys.a), free(succA.a), free(succB.a), free(indeg), free(heap), free(cellKeys), free(adjStart.a), free(adj);
    return r;
}

/* ------------------------------------------------------------------ the 2-D tests (geometry.py) */
static int tri_meets_rect(const int16_t *v9, int64_t rx0, int64_t rz0, int64_t rx1, int64_t rz1) {
    int64_t P[3][2] = {{v9[0], v9[2]}, {v9[3], v9[5]}, {v9[6], v9[8]}};
    int64_t R[4][2];
    int64_t mnx = P[0][0], mxx = P[0][0], mnz = P[0][1], mxz = P[0][1];
    int i, k, flat = 1;
    for (i = 1; i < 3; i++) {
        if (P[i][0] < mnx) mnx = P[i][0];
        if (P[i][0] > mxx) mxx = P[i][0];
        if (P[i][1] < mnz) mnz = P[i][1];
        if (P[i][1] > mxz) mxz = P[i][1];
    }
    if (mxx < rx0 || mnx > rx1 || mxz < rz0 || mnz > rz1) return 0;
    R[0][0] = rx0, R[0][1] = rz0, R[1][0] = rx1, R[1][1] = rz0, R[2][0] = rx1, R[2][1] = rz1, R[3][0] = rx0,
    R[3][1] = rz1;
    for (i = 0; i < 3; i++) {
        const int64_t *a = P[i], *b = P[(i + 1) % 3], *c = P[(i + 2) % 3];
        int64_t nx = a[1] - b[1], nz = b[0] - a[0];
        int64_t sc = nx * (c[0] - a[0]) + nz * (c[1] - a[1]);
        int all = 1;
        if (sc == 0) continue;
        flat = 0;
        for (k = 0; k < 4; k++) {
            int64_t s = nx * (R[k][0] - a[0]) + nz * (R[k][1] - a[1]);
            if (sc > 0 ? !(s < 0) : !(s > 0)) all = 0;
        }
        if (all) return 0;
    }
    if (flat) {
        /* the corners on one line: the first farthest pair (p outer, q inner) as Python's max() */
        int bi = 0, bj = 0;
        int64_t best = -1;
        for (i = 0; i < 3; i++)
            for (k = 0; k < 3; k++) {
                int64_t dx = P[i][0] - P[k][0], dz = P[i][1] - P[k][1], d = dx * dx + dz * dz;
                if (d > best) best = d, bi = i, bj = k;
            }
        {
            int64_t nx = P[bi][1] - P[bj][1], nz = P[bj][0] - P[bi][0], mn = 0, mx = 0;
            if (nx || nz) {
                for (k = 0; k < 4; k++) {
                    int64_t s = nx * (R[k][0] - P[bi][0]) + nz * (R[k][1] - P[bi][1]);
                    if (k == 0 || s < mn) mn = s;
                    if (k == 0 || s > mx) mx = s;
                }
                if (mn > 0 || mx < 0) return 0;
            }
        }
    }
    return 1;
}

static int covers(const int16_t *v9, int64_t x0, int64_t z0, int64_t x1, int64_t z1) {
    int64_t P[3][2] = {{v9[0], v9[2]}, {v9[3], v9[5]}, {v9[6], v9[8]}};
    int64_t C[4][2] = {{x0, z0}, {x1, z0}, {x1, z1}, {x0, z1}};
    int k, i;
    for (k = 0; k < 4; k++) {
        int pos = 1, neg = 1;
        for (i = 0; i < 3; i++) {
            const int64_t *a = P[i], *b = P[(i + 1) % 3];
            int64_t s = (b[0] - a[0]) * (C[k][1] - a[1]) - (b[1] - a[1]) * (C[k][0] - a[0]);
            if (!(s > 0)) pos = 0;
            if (!(s < 0)) neg = 0;
        }
        if (!pos && !neg) return 0;
    }
    return 1;
}

static int64_t fdiv(int64_t a, int64_t b) {   /* Python's // */
    int64_t q = a / b;
    if ((a % b != 0) && ((a < 0) != (b < 0))) q--;
    return q;
}

static int tri_cells(const int16_t *v9, int cw, int cd, int cols, int rows, int margin, int halfOpen, int *out) {
    int64_t mnx = v9[0], mxx = v9[0], mnz = v9[2], mxz = v9[2];
    int64_t cx0, cx1, cz0, cz1, cx, cz;
    int n = 0, lo = halfOpen ? 0 : 1, k;
    for (k = 1; k < 3; k++) {
        if (v9[3 * k] < mnx) mnx = v9[3 * k];
        if (v9[3 * k] > mxx) mxx = v9[3 * k];
        if (v9[3 * k + 2] < mnz) mnz = v9[3 * k + 2];
        if (v9[3 * k + 2] > mxz) mxz = v9[3 * k + 2];
    }
    cx0 = fdiv(mnx - margin, cw) - lo;
    if (cx0 < 0) cx0 = 0;
    cx1 = fdiv(mxx + margin, cw) + lo;
    if (cx1 > cols - 1) cx1 = cols - 1;
    cz0 = fdiv(mnz - margin, cd) - lo;
    if (cz0 < 0) cz0 = 0;
    cz1 = fdiv(mxz + margin, cd) + lo;
    if (cz1 > rows - 1) cz1 = rows - 1;
    for (cz = cz0; cz <= cz1; cz++)
        for (cx = cx0; cx <= cx1; cx++) {
            int64_t r0 = cx * cw - margin, r1 = cz * cd - margin, r2 = (cx + 1) * cw + margin, r3 = (cz + 1) * cd + margin;
            if (tri_meets_rect(v9, r0, r1, r2, r3) && !covers(v9, r0, r1, r2, r3)) out[n++] = (int) (cz * cols + cx);
        }
    return n;
}

/* Rare's wall-grid choices the half-open rule doesn't make (geometry.py wall_ties_of): a wall touching a cell's
 * grown rectangle only at its high edge is in that cell in 11 of the 48 such cases on the US levels, with nothing
 * in the stored s16 geometry to tell them apart; an unchanged triangle keeps them (build_grid) */
static int wall_ties(const TriVec * cells, const IntVec * cs, int cw, int cd, int cols, int rows, TriVec * tieT,
                     IntVec * tieC) {
    int c, i, k, cl[512], ho[512];
    for (c = 0; c + 1 < cs->n; c++)
        for (i = cs->a[c]; i < cs->a[c + 1]; i++) {
            const int nh = tri_cells(cells->a[i].v, cw, cd, cols, rows, 50, 1, ho);
            int nc, in = 0, dup = 0;
            for (k = 0; k < nh; k++) in |= ho[k] == c;
            if (in) continue;
            nc = tri_cells(cells->a[i].v, cw, cd, cols, rows, 50, 0, cl);
            for (k = 0; k < nc; k++) in |= cl[k] == c;
            if (!in) continue;
            for (k = 0; k < tieT->n; k++) dup |= tieC->a[k] == c && tri_eq(&tieT->a[k], &cells->a[i]);
            if (dup) continue;
            if (VPUSH(*tieT, cells->a[i]) || VPUSH(*tieC, c)) return -1;
        }
    return 0;
}

static int tris_overlap(const int16_t *a9, const int16_t *b9) {
    int64_t A[3][2] = {{a9[0], a9[2]}, {a9[3], a9[5]}, {a9[6], a9[8]}};
    int64_t B[3][2] = {{b9[0], b9[2]}, {b9[3], b9[5]}, {b9[6], b9[8]}};
    int w, i, k;
    for (w = 0; w < 2; w++) {
        int64_t (*P)[2] = w ? B : A;
        for (i = 0; i < 3; i++) {
            int64_t nx = P[i][1] - P[(i + 1) % 3][1], nz = P[(i + 1) % 3][0] - P[i][0];
            int64_t mna = 0, mxa = 0, mnb = 0, mxb = 0;
            if (nx == 0 && nz == 0) continue;
            for (k = 0; k < 3; k++) {
                int64_t sa = nx * A[k][0] + nz * A[k][1], sb = nx * B[k][0] + nz * B[k][1];
                if (k == 0 || sa < mna) mna = sa;
                if (k == 0 || sa > mxa) mxa = sa;
                if (k == 0 || sb < mnb) mnb = sb;
                if (k == 0 || sb > mxb) mxb = sb;
            }
            if (mxa <= mnb || mxb <= mna) return 0;
        }
    }
    return 1;
}

static int wall_heading(const int16_t *v) {
    double d1[3], d2[3], nx, nz, h;
    int i;
    for (i = 0; i < 3; i++) d1[i] = (double) v[i] - v[3 + i], d2[i] = (double) v[i] - v[6 + i];
    nx = d1[1] * d2[2] - d1[2] * d2[1];
    nz = d1[0] * d2[1] - d1[1] * d2[0];
    h = rint(atan2(nx, nz) / (2 * 3.141592653589793) * 4096);   /* Python round(): ties to even */
    return ((int) h) & 0xFFF;
}

/* ------------------------------------------------------------------ the display-list block */
#define VTX_CACHE 16
static int is_vstate(uint32_t w0, uint32_t w1) {
    const unsigned op = w0 >> 24;
    if (op == 0x01 || op == 0xBD || op == 0x03 || op == 0xBC || op == 0xBB || op == 0x06 || op == 0xBE) return 1;
    if ((op == 0xB6 || op == 0xB7) && (w1 & 0xF0000u)) return 1;
    return 0;
}
static const uint8_t kBoxOrder[8][3] = {{0, 0, 0}, {0, 1, 0}, {1, 0, 0}, {1, 1, 0}, {0, 0, 1}, {0, 1, 1}, {1, 0, 1}, {1, 1, 1}};

static int list_end(const uint8_t *dl, uint32_t n, uint32_t q, uint32_t *end) {
    while (q + 8 <= n) {
        uint32_t w0 = get_be32(dl + q);
        q += 8;
        if (w0 >> 24 == 0xB8) {
            *end = q;
            return 0;
        }
    }
    return -1;
}

static int aabb(const Op *ops, int n, int out[6]) {
    int i, k, any = 0;
    for (i = 0; i < n; i++) {
        if (ops[i].kind != OP_T && ops[i].kind != OP_TRI) continue;
        for (k = 0; k < 3; k++) {
            const Vx *v = &ops[i].v[k];
            int x = vx_x(v), y = vx_y(v), z = vx_z(v);
            if (!any) out[0] = out[1] = x, out[2] = out[3] = y, out[4] = out[5] = z, any = 1;
            if (x < out[0]) out[0] = x;
            if (x > out[1]) out[1] = x;
            if (y < out[2]) out[2] = y;
            if (y > out[3]) out[3] = y;
            if (z < out[4]) out[4] = z;
            if (z > out[5]) out[5] = z;
        }
    }
    return any;
}
static void aabb_vx(const Vx *v, int n, int out[6]) {
    int i;
    for (i = 0; i < n; i++) {
        int x = vx_x(&v[i]), y = vx_y(&v[i]), z = vx_z(&v[i]);
        if (i == 0) out[0] = out[1] = x, out[2] = out[3] = y, out[4] = out[5] = z;
        if (x < out[0]) out[0] = x;
        if (x > out[1]) out[1] = x;
        if (y < out[2]) out[2] = y;
        if (y > out[3]) out[3] = y;
        if (z < out[4]) out[4] = z;
        if (z > out[5]) out[5] = z;
    }
}
static void box_from(const uint8_t pat[8][3], const Vx *rest, const int bb[6], Vx out[8]) {
    int i;
    for (i = 0; i < 8; i++) {
        out[i] = rest[i];
        vx_setpos(&out[i], bb[pat[i][0]], bb[2 + pat[i][1]], bb[4 + pat[i][2]]);
    }
}

typedef struct {
    uint32_t start, end;
    int kind, refk, refi, setup;
} PieceDef;
typedef VEC(PieceDef) PieceDefVec;

static int add_piece(PieceDefVec * ps, int kind, uint32_t a, uint32_t b, int rk, int ri, char *err, size_t errlen) {
    int i;
    PieceDef d;
    if (a == b) return 0;
    for (i = 0; i < ps->n; i++)
        if (ps->a[i].start == a) {
            if (ps->a[i].end != b) {
                seterr(err, errlen, "two lists start at 0x%X with different ends", (unsigned) a);
                return -1;
            }
            return 1;   /* (exists) */
        }
    d.start = a, d.end = b, d.kind = kind, d.refk = rk, d.refi = ri, d.setup = 0;
    return VPUSH(*ps, d);
}

static int cmp_piece(const void *a, const void *b) {
    const PieceDef *x = (const PieceDef *) a, *y = (const PieceDef *) b;
    return x->start < y->start ? -1 : x->start > y->start;
}

static int decode_piece(Piece *pc, const uint8_t *dl, const Vx *verts, int nverts, char *err, size_t errlen) {
    const int ncmd = (int) ((pc->end - pc->start) / 8);
    Vx slot[VTX_CACHE];
    int slotEpoch[VTX_CACHE], loaded[VTX_CACHE];
    int epoch = 0, i, frozen = 0, k;
    OpVec ops = {0};
    if ((pc->end - pc->start) % 8) {
        seterr(err, errlen, "piece 0x%X..0x%X is not whole commands", (unsigned) pc->start, (unsigned) pc->end);
        return -1;
    }
    if (pc->kind == P_RAW) {
        for (i = 0; i < ncmd; i++) {
            Op o;
            memset(&o, 0, sizeof o);
            o.kind = OP_C, o.w0 = get_be32(dl + pc->start + 8 * i), o.w1 = get_be32(dl + pc->start + 8 * i + 4);
            if (VPUSH(pc->ops, o)) return -1;
            o.v = NULL;
            if (VPUSH(pc->orig, o)) return -1;
        }
        pc->frozen = 1;
        return 0;
    }
    pc->setup0 = pc->setup;
    memset(loaded, 0, sizeof loaded);
    for (i = 0; i < ncmd; i++) {
        uint32_t w0 = get_be32(dl + pc->start + 8 * i), w1 = get_be32(dl + pc->start + 8 * i + 4);
        unsigned op = w0 >> 24;
        Op o;
        memset(&o, 0, sizeof o);
        o.w0 = w0, o.w1 = w1;
        if (op == 0x04) {
            int n = (int) ((w0 >> 20) & 0xF) + 1, v0 = (int) ((w0 >> 16) & 0xF);
            unsigned seg = w1 >> 24, off = w1 & 0xFFFFFF;
            if (seg != 8 || off % 16 || (int) (off / 16) + n > nverts || v0 + n > VTX_CACHE) {
                seterr(err, errlen, "G_VTX %08X %08X at 0x%X: not segment-8 vertices of the file", (unsigned) w0,
                       (unsigned) w1, (unsigned) (pc->start + 8 * i));
                goto fail;
            }
            o.kind = OP_VTX, o.nv = n, o.off = (int) (off / 16);
            o.v = (Vx *) malloc(sizeof(Vx) * (size_t) n);
            if (o.v == NULL) goto fail;
            memcpy(o.v, verts + off / 16, sizeof(Vx) * (size_t) n);
            for (k = 0; k < n; k++) slot[v0 + k] = o.v[k], slotEpoch[v0 + k] = epoch, loaded[v0 + k] = 1;
            if (i < pc->setup) frozen = 1;
        } else if (op == 0xBF) {
            int idx[3], bad = 0;
            for (k = 0; k < 3; k++) {
                const unsigned b = (w1 >> (16 - 8 * k)) & 0xFF;
                idx[k] = (int) (b / 10);
                if (b % 10 || idx[k] >= VTX_CACHE || !loaded[idx[k]]) bad = 1;
            }
            if (bad) {
                seterr(err, errlen, "G_TRI1 %08X at 0x%X uses a vertex never loaded", (unsigned) w1,
                       (unsigned) (pc->start + 8 * i));
                goto fail;
            }
            o.kind = OP_TRI, o.nv = 3;
            o.v = (Vx *) malloc(sizeof(Vx) * 3);
            if (o.v == NULL) goto fail;
            for (k = 0; k < 3; k++) {
                o.v[k] = slot[idx[k]];
                if (slotEpoch[idx[k]] != epoch) frozen = 1;
            }
        } else if (op == 0x06) {
            o.kind = OP_DL;
            epoch++;
        } else {
            o.kind = OP_C;
            if (op == 0xBE && !(i == 1 && ops.n >= 1 && ops.a[0].kind == OP_VTX && pc->setup == 0)) frozen = 1;
            if (is_vstate(w0, w1)) epoch++;
        }
        if (VPUSH(ops, o)) {
            op_free(&o);
            goto fail;
        }
    }
    if (pc->kind == P_PACKED) frozen = 1;
    /* the sub-lists' prologue: a box and G_CULLDL */
    pc->hascull = 0;
    if (ops.n >= 2 && ops.a[0].kind == OP_VTX && ops.a[1].kind == OP_C && ops.a[1].w0 >> 24 == 0xBE && pc->setup == 0) {
        int bb[6], bbox[6];
        if (aabb(ops.a + 2, ops.n - 2, bb) && (aabb_vx(ops.a[0].v, ops.a[0].nv, bbox), !memcmp(bb, bbox, sizeof bb))) {
            pc->hascull = 1;
        } else {
            frozen = 1;
        }
    }
    pc->frozen = frozen;
    /* the file's own commands (keep mode, frozen pieces, moved-only pieces) */
    for (i = 0; i < ops.n; i++) {
        Op c = op_copy(&ops.a[i]);
        if (VPUSH(pc->orig, c)) goto fail;
    }
    if (!frozen) {
        int start = 0;
        int setupNew = 0;
        if (pc->hascull) {
            Op c;
            Vx canon[8];
            int bb[6], useCanon = 0;
            memset(&c, 0, sizeof c);
            c.kind = OP_CULL, c.w0 = ops.a[0].w0, c.cw0 = ops.a[1].w0, c.cw1 = ops.a[1].w1, c.nv = ops.a[0].nv;
            aabb(ops.a + 2, ops.n - 2, bb);
            if (c.nv == 8) {
                box_from(kBoxOrder, ops.a[0].v, bb, canon);
                useCanon = !memcmp(canon, ops.a[0].v, sizeof canon);
            }
            for (k = 0; k < c.nv && k < 8; k++) {
                int a;
                if (useCanon) {
                    for (a = 0; a < 3; a++) c.pat[k][a] = kBoxOrder[k][a];
                } else {
                    const Vx *v = &ops.a[0].v[k];
                    c.pat[k][0] = (uint8_t) (vx_x(v) == bb[0] ? 0 : 1);
                    c.pat[k][1] = (uint8_t) (vx_y(v) == bb[2] ? 0 : 1);
                    c.pat[k][2] = (uint8_t) (vx_z(v) == bb[4] ? 0 : 1);
                }
            }
            if (c.nv != 8) {
                /* (a box of another size: kept as the file has it, via the frozen path) */
                pc->frozen = 1;
                goto keep;
            }
            c.v = ops.a[0].v;   /* (the box as the file had it: Python's o[6]; rest = v[3:]) */
            ops.a[0].v = NULL;
            pc->ops.n = 0;
            if (VPUSH(pc->ops, c)) goto fail;
            start = 2;
        }
        for (i = start; i < ops.n; i++) {
            Op *o = &ops.a[i];
            if (o->kind == OP_VTX) continue;
            if (o->kind == OP_TRI) {
                Op t;
                t = *o;
                t.kind = OP_T;
                t.w1 = (o->w1 >> 24) & 0xFF;
                o->v = NULL;
                if (VPUSH(pc->ops, t)) goto fail;
            } else {
                Op t = *o;
                o->v = NULL;
                if (VPUSH(pc->ops, t)) goto fail;
            }
        }
        if (pc->setup) {
            for (i = 0; i < pc->setup && i < ops.n; i++)
                if (ops.a[i].kind != OP_VTX) setupNew++;
            pc->setup = setupNew;
        }
        for (i = 0; i < ops.n; i++) op_free(&ops.a[i]);
        free(ops.a);
        return 0;
    }
keep:
    /* frozen: the commands as they are */
    for (i = 0; i < ops.n; i++) {
        if (VPUSH(pc->ops, ops.a[i])) goto fail;
        ops.a[i].v = NULL;
    }
    free(ops.a);
    return 0;
fail:
    for (i = 0; i < ops.n; i++) op_free(&ops.a[i]);
    free(ops.a);
    return -1;
}

static int geo_from_file(Geo *g, const File *fl, char *err, size_t errlen) {
    const uint8_t *f = fl->f, *dl = fl->f + fl->base;
    const uint32_t ndl = fl->dlend - fl->base;
    PieceDefVec ps = {0};
    uint32_t pos = 0, q;
    int i, k, r = -1;
    static const unsigned kFixed[4] = {0x90, 0x94, 0x98, 0x9C};
    memset(g, 0, sizeof *g);
    if (map_init(&g->moves, 6, 16)) return -1;
    for (q = 0xC8; q + 16 <= HW(fl, 0x20); q += 16) {
        Vx v;
        memcpy(v.b, f + q, 16);
        if (VPUSH(g->overts, v)) goto done;
    }
#define REL(slot) (HW(fl, slot) - fl->base)
    if (add_piece(&ps, P_PREFIX, REL(0x88), REL(0x8C), 0, 0, err, errlen) < 0) goto done;
    if (add_piece(&ps, P_PACKED, REL(0x7C), REL(0x80), 0, 0, err, errlen) < 0) goto done;
    for (k = 0; k < 4; k++) {
        for (i = 0; i < fl->rec[k].n / 5; i++) {
            const uint32_t *w = fl->rec[k].a + 5 * i;
            int j, rr = add_piece(&ps, P_REC, w[0] - fl->base, w[2] - fl->base, k, i, err, errlen);
            if (rr < 0) goto done;
            for (j = 0; j < ps.n; j++)
                if (ps.a[j].start == w[0] - fl->base) ps.a[j].setup = (int) ((w[1] - w[0]) / 8);
        }
        for (i = 0; i < fl->grpAt[k].n; i++) {
            const uint32_t *w = fl->grp[k].a + fl->grpAt[k].a[i];
            int j, rr = add_piece(&ps, P_GRP, w[0] - fl->base, w[2] - fl->base, k, i, err, errlen);
            if (rr < 0) goto done;
            for (j = 0; j < ps.n; j++)
                if (ps.a[j].start == w[0] - fl->base) ps.a[j].setup = (int) ((w[1] - w[0]) / 8);
        }
    }
    for (k = 0; k < 4; k++) {
        uint32_t e;
        if (list_end(dl, ndl, REL(kFixed[k]), &e)) {
            seterr(err, errlen, "fixed list 0x%X has no G_ENDDL", kFixed[k]);
            goto done;
        }
        if (add_piece(&ps, P_FIXED, REL(kFixed[k]), e, (int) kFixed[k], 0, err, errlen) < 0) goto done;
    }
    for (q = 0; q + 8 <= ndl; q += 8) {
        uint32_t w0 = get_be32(dl + q), w1 = get_be32(dl + q + 4), e;
        int j, have = 0;
        if (w0 >> 24 != 0x06) continue;
        if (w1 >= ndl || w1 % 8) {
            seterr(err, errlen, "G_DL at 0x%X calls 0x%X, outside the block", (unsigned) q, (unsigned) w1);
            goto done;
        }
        for (j = 0; j < ps.n; j++)
            if (ps.a[j].start == w1) have = 1;
        if (have) continue;
        if (list_end(dl, ndl, w1, &e)) {
            seterr(err, errlen, "list at 0x%X has no G_ENDDL", (unsigned) w1);
            goto done;
        }
        if (add_piece(&ps, P_SUB, w1, e, 0, 0, err, errlen) < 0) goto done;
    }
#undef REL
    qsort(ps.a, (size_t) ps.n, sizeof *ps.a, cmp_piece);
    for (i = 0; i < ps.n; i++) {
        Piece pc;
        memset(&pc, 0, sizeof pc);
        if (ps.a[i].start < pos) {
            seterr(err, errlen, "lists overlap at 0x%X", (unsigned) ps.a[i].start);
            goto done;
        }
        if (ps.a[i].start > pos) {
            pc.kind = P_RAW, pc.start = pos, pc.end = ps.a[i].start;
            if (decode_piece(&pc, dl, g->overts.a, g->overts.n, err, errlen) || VPUSH(g->pc, pc)) goto done;
            memset(&pc, 0, sizeof pc);
        }
        pc.kind = ps.a[i].kind, pc.start = ps.a[i].start, pc.end = ps.a[i].end;
        pc.refk = ps.a[i].refk, pc.refi = ps.a[i].refi, pc.setup = ps.a[i].setup;
        if (decode_piece(&pc, dl, g->overts.a, g->overts.n, err, errlen)) {
            piece_free(&pc);
            goto done;
        }
        if (VPUSH(g->pc, pc)) goto done;
        pos = ps.a[i].end;
    }
    if (pos < ndl) {
        Piece pc;
        memset(&pc, 0, sizeof pc);
        pc.kind = P_RAW, pc.start = pos, pc.end = ndl;
        if (decode_piece(&pc, dl, g->overts.a, g->overts.n, err, errlen) || VPUSH(g->pc, pc)) goto done;
    }
    /* the grids, as one global order each */
    {
        TriVec cells = {0};
        IntVec cs = {0};
        const unsigned gcells = fl->u16[4] * fl->u16[5], ccells = fl->u16[8] * fl->u16[9];
        const int cw = (int) fl->u16[10], cd = (int) fl->u16[11], cols = (int) fl->u16[8], rows = (int) fl->u16[9];
        int rr = parse_grid(fl, 0x30, 0x34, gcells, 0x14, (void *) &cells, (void *) &cs, err, errlen) ||
                 grid_order((void *) &cells, (void *) &cs, (void *) &g->ground);
        cells.n = cs.n = 0;
        rr = rr || parse_grid(fl, 0x6C, 0x70, ccells, 0x16, (void *) &cells, (void *) &cs, err, errlen) ||
             grid_order((void *) &cells, (void *) &cs, (void *) &g->walls[0]) ||
             wall_ties((void *) &cells, (void *) &cs, cw, cd, cols, rows, (void *) &g->tieT[0], (void *) &g->tieC[0]);
        cells.n = cs.n = 0;
        rr = rr || parse_grid(fl, 0x70, 0x74, ccells, 0x16, (void *) &cells, (void *) &cs, err, errlen) ||
             grid_order((void *) &cells, (void *) &cs, (void *) &g->walls[1]) ||
             wall_ties((void *) &cells, (void *) &cs, cw, cd, cols, rows, (void *) &g->tieT[1], (void *) &g->tieC[1]);
        free(cells.a), free(cs.a);
        if (rr) goto done;
    }
    r = 0;
done:
    free(ps.a);
    if (r) geo_free(g);
    return r;
}

/* the render triangles in block order: (piece, op) */
typedef struct {
    int pi, oi;
} TriRef;
typedef VEC(TriRef) TriRefVec;
static int render_tris(const Geo *g, TriRefVec * out) {
    int i, k;
    out->n = 0;
    for (i = 0; i < g->pc.n; i++)
        for (k = 0; k < g->pc.a[i].ops.n; k++) {
            const int kind = g->pc.a[i].ops.a[k].kind;
            if (kind == OP_T || kind == OP_TRI) {
                TriRef t;
                t.pi = i, t.oi = k;
                if (VPUSH(*out, t)) return -1;
            }
        }
    return 0;
}

/* ------------------------------------------------------------------ emission */
typedef struct {
    VxVec v;
    Map index;     /* (hash, len) -> nothing; we keep a list per hash for byte compares */
    IntVec seqOff, seqLen;
    U64Vec seqHash;
} VArr;
static void va_free(VArr *va) {
    free(va->v.a), free(va->seqOff.a), free(va->seqLen.a), free(va->seqHash.a);
    map_free(&va->index);
}
static int va_init(VArr *va, const Vx *base, int n) {
    int i;
    memset(va, 0, sizeof *va);
    if (map_init(&va->index, 8, 256)) return -1;
    for (i = 0; i < n; i++)
        if (VPUSH(va->v, base[i])) return -1;
    return 0;
}
/* add(vs): the offset of an earlier add of the same sequence, else appended */
static int va_add(VArr *va, const Vx *vs, int n) {
    uint64_t h = fnv(vs, sizeof(Vx) * (size_t) n, FNV0) ^ (uint64_t) n;
    int *first = map_find(&va->index, &h), i, j;
    if (first) {
        for (j = *first; j >= 0; j = -1) {
            /* walk the chain of equal hashes (kept in order of adding) */
            int k;
            for (k = 0; k < va->seqHash.n; k++)
                if (va->seqHash.a[k] == h && va->seqLen.a[k] == n &&
                    !memcmp(va->v.a + va->seqOff.a[k], vs, sizeof(Vx) * (size_t) n))
                    return va->seqOff.a[k];
        }
    }
    i = va->v.n;
    for (j = 0; j < n; j++)
        if (VPUSH(va->v, vs[j])) return -1;
    if (VPUSH(va->seqOff, i) || VPUSH(va->seqLen, n) || VPUSH(va->seqHash, h)) return -1;
    if (first == NULL && map_put(&va->index, &h, va->seqHash.n - 1)) return -1;
    return i;
}
static int va_add_at(VArr *va, const Vx *vs, int n, int off) {
    if (off >= 0 && off + n <= va->v.n && !memcmp(va->v.a + off, vs, sizeof(Vx) * (size_t) n)) return off;
    return va_add(va, vs, n);
}

typedef struct {
    U32Vec w;     /* pairs */
    IntVec dlAt;       /* command index of each G_DL */
    U32Vec dlTgt;
    int setup;           /* rec/grp: commands before the alternate start (-1 none) */
} Emit;
static void emit_free(Emit *e) {
    free(e->w.a), free(e->dlAt.a), free(e->dlTgt.a);
}
static int emit_cmd(Emit *e, uint32_t w0, uint32_t w1) {
    return VPUSH(e->w, w0) || VPUSH(e->w, w1);
}
static int vx_in(const Vx *list, int n, const Vx *v) {
    int i;
    for (i = 0; i < n; i++)
        if (!memcmp(&list[i], v, sizeof *v)) return i;
    return -1;
}

static int emit_batch(Emit *e, VArr *va, const Op *ops, int nops, int i, Vx *loaded, int *nloaded) {
    Vx batch[VTX_CACHE];
    int nb = 0, j, off, k;
    for (j = i; j < nops; j++) {
        const Op *o = &ops[j];
        if (o->kind == OP_T) {
            Vx need[3];
            int nn = 0;
            for (k = 0; k < 3; k++)
                if (vx_in(batch, nb, &o->v[k]) < 0 && vx_in(need, nn, &o->v[k]) < 0) need[nn++] = o->v[k];
            if (nb + nn > VTX_CACHE) break;
            for (k = 0; k < nn; k++) batch[nb++] = need[k];
        } else if (o->kind == OP_DL || o->kind == OP_VTX || o->kind == OP_TRI || o->kind == OP_CULL ||
                   (o->kind == OP_C && is_vstate(o->w0, o->w1))) {
            break;
        }
    }
    off = va_add(va, batch, nb);
    if (off < 0) return -1;
    if (emit_cmd(e, 0x04000000u | ((uint32_t) (nb - 1) << 20) | (uint32_t) (nb * 16), 0x08000000u | (uint32_t) (off * 16)))
        return -1;
    memcpy(loaded, batch, sizeof(Vx) * (size_t) nb);
    *nloaded = nb;
    return 0;
}

static int emit_piece(const Piece *pc, VArr *va, int keep, Emit *e) {
    const Op *ops = keep ? pc->orig.a : pc->ops.a;
    const int nops = keep ? pc->orig.n : pc->ops.n;
    const int setup = keep ? pc->setup0 : pc->setup;
    Vx loaded[VTX_CACHE];
    int nloaded = 0, i, k;
    e->setup = -1;
    for (i = 0; i < nops; i++) {
        const Op *o = &ops[i];
        if (i == setup && (pc->kind == P_REC || pc->kind == P_GRP)) e->setup = e->w.n / 2;
        switch (o->kind) {
            case OP_CULL: {
                int bb[6], off;
                Vx box[8];
                if (aabb(ops, nops, bb)) box_from(o->pat, o->v, bb, box);
                else memcpy(box, o->v, sizeof box);
                off = va_add(va, box, 8);
                if (off < 0 || emit_cmd(e, o->w0, 0x08000000u | (uint32_t) (off * 16)) || emit_cmd(e, o->cw0, o->cw1))
                    return -1;
                nloaded = 0;
                break;
            }
            case OP_C:
                if (emit_cmd(e, o->w0, o->w1)) return -1;
                if (is_vstate(o->w0, o->w1)) nloaded = 0;
                break;
            case OP_DL:
                if (VPUSH(e->dlAt, e->w.n / 2) || VPUSH(e->dlTgt, o->w1) || emit_cmd(e, o->w0, 0)) return -1;
                nloaded = 0;
                break;
            case OP_VTX: {
                int off = keep ? va_add_at(va, o->v, o->nv, o->off) : va_add(va, o->v, o->nv);
                if (off < 0 || emit_cmd(e, o->w0, 0x08000000u | (uint32_t) (off * 16))) return -1;
                nloaded = 0;
                break;
            }
            case OP_TRI:
                if (emit_cmd(e, o->w0, o->w1)) return -1;
                break;
            case OP_T: {
                int idx[3], all = 1;
                for (k = 0; k < 3; k++)
                    if (vx_in(loaded, nloaded, &o->v[k]) < 0) all = 0;
                if (!all && emit_batch(e, va, ops, nops, i, loaded, &nloaded)) return -1;
                for (k = 0; k < 3; k++) idx[k] = vx_in(loaded, nloaded, &o->v[k]);
                if (emit_cmd(e, o->w0, (o->w1 & 0xFF) << 24 | (uint32_t) (idx[0] * 10) << 16 | (uint32_t) (idx[1] * 10) << 8 |
                                           (uint32_t) (idx[2] * 10)))
                    return -1;
                break;
            }
        }
    }
    if ((pc->kind == P_REC || pc->kind == P_GRP) && e->setup < 0) e->setup = setup >= nops ? e->w.n / 2 : 0;
    return 0;
}

static int move_of(const Geo *g, const Vx *v, Vx *out) {
    int16_t key[3];
    int *p;
    *out = *v;
    key[0] = (int16_t) vx_x(v), key[1] = (int16_t) vx_y(v), key[2] = (int16_t) vx_z(v);
    p = map_find(&g->moves, key);
    if (p == NULL) return 0;
    vx_setpos(out, g->movedTo.a[*p], g->movedTo.a[*p + 1], g->movedTo.a[*p + 2]);
    return 1;
}

static int emit_moved(const Geo *g, const Piece *pc, VArr *va, const int *excl, int nexcl, Emit *e) {
    const Op *cull = pc->ops.n && pc->ops.a[0].kind == OP_CULL ? &pc->ops.a[0] : NULL;
    int i, j;
    e->setup = -1;
    for (i = 0; i < pc->orig.n; i++) {
        const Op *o = &pc->orig.a[i];
        if (i == pc->setup0 && (pc->kind == P_REC || pc->kind == P_GRP)) e->setup = e->w.n / 2;
        if (o->kind == OP_VTX) {
            Vx *vs = (Vx *) malloc(sizeof(Vx) * (size_t) (o->nv + 8));
            int off = o->off, n = o->nv, ok = 1;
            if (vs == NULL) return -1;
            for (j = 0; j < n; j++) move_of(g, &o->v[j], &vs[j]);
            if (i == 0 && cull != NULL) {
                int bb[6];
                if (aabb(pc->ops.a, pc->ops.n, bb)) box_from(cull->pat, cull->v, bb, vs);
                else memcpy(vs, cull->v, sizeof(Vx) * 8);
                n = 8;
            }
            for (j = 0; j < n; j++)
                if (off + j >= nexcl || excl[off + j] != 1) ok = 0;
            if (ok) memcpy(va->v.a + off, vs, sizeof(Vx) * (size_t) n);
            else off = va_add(va, vs, n);
            free(vs);
            if (off < 0 || emit_cmd(e, o->w0, 0x08000000u | (uint32_t) (off * 16))) return -1;
        } else if (o->kind == OP_DL) {
            if (VPUSH(e->dlAt, e->w.n / 2) || VPUSH(e->dlTgt, o->w1) || emit_cmd(e, o->w0, 0)) return -1;
        } else {
            if (emit_cmd(e, o->w0, o->w1)) return -1;
        }
    }
    if ((pc->kind == P_REC || pc->kind == P_GRP) && e->setup < 0) e->setup = pc->setup0 >= pc->orig.n ? e->w.n / 2 : 0;
    return 0;
}

static int pieces_ops_eq(const Piece *a, const Piece *b) {
    int i;
    if (a->ops.n != b->ops.n) return 0;
    for (i = 0; i < a->ops.n; i++)
        if (!op_eq(&a->ops.a[i], &b->ops.a[i])) return 0;
    return 1;
}

typedef struct {
    Buf dl;
    VArr va;
    U32Vec bOld, bNew;     /* orig offset -> new offset (start: first set wins; end: last) */
    U32Vec pStart, pEnd;   /* per piece: new start, end */
    IntVec pSetup;
} DlOut;
static void dlout_free(DlOut *d) {
    free(d->dl.p), free(d->bOld.a), free(d->bNew.a), free(d->pStart.a), free(d->pEnd.a), free(d->pSetup.a);
    va_free(&d->va);
}
static int bound_find(const DlOut *d, uint32_t old, uint32_t *nw) {
    int i;
    for (i = 0; i < d->bOld.n; i++)
        if (d->bOld.a[i] == old) {
            *nw = d->bNew.a[i];
            return 1;
        }
    return 0;
}
static int bound_set(DlOut *d, uint32_t old, uint32_t nw, int overwrite) {
    int i;
    for (i = 0; i < d->bOld.n; i++)
        if (d->bOld.a[i] == old) {
            if (overwrite) d->bNew.a[i] = nw;
            return 0;
        }
    return VPUSH(d->bOld, old) || VPUSH(d->bNew, nw);
}

static int build_dl(const Geo *g, const Geo *base, DlOut *d, char *err, size_t errlen) {
    int i, j, aligned = 0;
    int *excl = NULL, nexcl = 0;
    U32Vec fixAt = {0}, fixTgt = {0};
    memset(d, 0, sizeof *d);
    if (base != NULL && base->pc.n == g->pc.n) {
        aligned = 1;
        for (i = 0; i < g->pc.n; i++)
            if (g->pc.a[i].start != base->pc.a[i].start) aligned = 0;
    }
    if (aligned) {
        nexcl = base->overts.n;
        excl = (int *) calloc((size_t) nexcl + 1, sizeof(int));
        if (excl == NULL) return -1;
        for (i = 0; i < base->pc.n; i++) {
            const Piece *pc = &base->pc.a[i];
            /* (each piece counts once per vertex) */
            char *seen = (char *) calloc((size_t) nexcl + 1, 1);
            if (seen == NULL) goto oom;
            for (j = 0; j < pc->orig.n; j++) {
                const Op *o = &pc->orig.a[j];
                int k;
                if (o->kind != OP_VTX) continue;
                for (k = 0; k < o->nv; k++)
                    if (o->off + k < nexcl && !seen[o->off + k]) seen[o->off + k] = 1, excl[o->off + k]++;
            }
            free(seen);
        }
    }
    if (va_init(&d->va, base ? base->overts.a : NULL, base ? base->overts.n : 0)) goto oom;
    for (i = 0; i < g->pc.n; i++) {
        const Piece *pc = &g->pc.a[i];
        const int same = aligned && pieces_ops_eq(pc, &base->pc.a[i]);
        Emit e;
        uint32_t at = (uint32_t) d->dl.n;
        int r;
        memset(&e, 0, sizeof e);
        if (aligned && !same && !pc->restructured) r = emit_moved(g, pc, &d->va, excl, nexcl, &e);
        else r = emit_piece(pc, &d->va, same, &e);
        if (r) {
            emit_free(&e);
            goto oom;
        }
        if (bound_set(d, pc->start, at, 0)) goto oom;
        for (j = 0; j < e.dlAt.n; j++)
            if (VPUSH(fixAt, at + 8 * (uint32_t) e.dlAt.a[j]) || VPUSH(fixTgt, e.dlTgt.a[j])) goto oom;
        for (j = 0; j < e.w.n; j++)
            if (buf_u32(&d->dl, e.w.a[j])) goto oom;
        if (VPUSH(d->pStart, at) || VPUSH(d->pEnd, (uint32_t) d->dl.n) || VPUSH(d->pSetup, e.setup)) goto oom;
        if (bound_set(d, pc->end, (uint32_t) d->dl.n, 1)) goto oom;
        emit_free(&e);
    }
    for (i = 0; i < fixAt.n; i++) {
        int found = 0;
        for (j = 0; j < g->pc.n; j++)
            if (g->pc.a[j].start == fixTgt.a[i]) {
                put_be32(d->dl.p + fixAt.a[i] + 4, d->pStart.a[j]);
                found = 1;
                break;
            }
        if (!found) {
            seterr(err, errlen, "G_DL to 0x%X: no list starts there", (unsigned) fixTgt.a[i]);
            free(excl), free(fixAt.a), free(fixTgt.a);
            return -1;
        }
    }
    free(excl), free(fixAt.a), free(fixTgt.a);
    /* the vertices no load uses any more (a re-packed list's old ones) dropped, the loads moved down
     * (geometry.py compact_vertices); the file's own unloaded vertices stay */
    {
        const int nv = d->va.v.n;
        char *used = (char *) calloc((size_t) nv + 1, 1);
        int *nidx = (int *) calloc((size_t) nv + 1, sizeof(int));
        int all = 1, kk = 0;
        uint32_t q;
        if (used == NULL || nidx == NULL) {
            free(used), free(nidx);
            return -1;
        }
        if (base != NULL) {
            char *ld = (char *) calloc((size_t) base->overts.n + 1, 1);
            if (ld == NULL) {
                free(used), free(nidx);
                return -1;
            }
            for (i = 0; i < base->pc.n; i++)
                for (j = 0; j < base->pc.a[i].orig.n; j++) {
                    const Op *o = &base->pc.a[i].orig.a[j];
                    int s = -1, n = 0, k;
                    if (o->kind == OP_VTX) s = o->off, n = o->nv;
                    else if (o->kind == OP_C && o->w0 >> 24 == 0x04 && o->w1 >> 24 == 8)
                        s = (int) ((o->w1 & 0xFFFFFF) / 16), n = (int) ((o->w0 >> 20) & 0xF) + 1;
                    for (k = 0; s >= 0 && k < n; k++)
                        if (s + k < base->overts.n) ld[s + k] = 1;
                }
            for (i = 0; i < base->overts.n && i < nv; i++)
                if (!ld[i]) used[i] = 1;
            free(ld);
        }
        for (q = 0; q + 8 <= d->dl.n; q += 8) {
            const uint32_t w0 = get_be32(d->dl.p + q), w1 = get_be32(d->dl.p + q + 4);
            if (w0 >> 24 == 0x04 && w1 >> 24 == 8) {
                const int off = (int) ((w1 & 0xFFFFFF) / 16), n = (int) ((w0 >> 20) & 0xF) + 1;
                int k;
                for (k = off; k < off + n && k < nv; k++) used[k] = 1;
            }
        }
        for (i = 0; i < nv; i++)
            if (!used[i]) all = 0;
        if (!all) {
            for (i = 0; i < nv; i++) nidx[i] = kk, kk += used[i];
            for (q = 0; q + 8 <= d->dl.n; q += 8) {
                const uint32_t w0 = get_be32(d->dl.p + q), w1 = get_be32(d->dl.p + q + 4);
                if (w0 >> 24 == 0x04 && w1 >> 24 == 8) {
                    const int off = (int) ((w1 & 0xFFFFFF) / 16);
                    put_be32(d->dl.p + q + 4, 0x08000000u | (uint32_t) (nidx[off] * 16));
                }
            }
            for (i = 0, kk = 0; i < nv; i++)
                if (used[i]) d->va.v.a[kk++] = d->va.v.a[i];
            d->va.v.n = kk;
        }
        free(used), free(nidx);
    }
    return 0;
oom:
    seterr(err, errlen, "out of memory");
    free(excl), free(fixAt.a), free(fixTgt.a);
    return -1;
}

/* ------------------------------------------------------------------ the derived sections */
/* a triangle's cells: tri_cells, plus the tie cells Rare gave this triangle (wall_ties; ties NULL: none) */
static int tri_cells_tied(const Tri *t, int cw, int cd, int cols, int rows, int margin, int halfOpen,
                          const TriVec * tieT, const IntVec * tieC, int *out) {
    int n = tri_cells(t->v, cw, cd, cols, rows, margin, halfOpen, out), j, k;
    if (tieT == NULL) return n;
    for (j = 0; j < tieT->n; j++) {
        int have = 0, at;
        if (tieC->a[j] >= cols * rows || n >= 512 || !tri_eq(&tieT->a[j], t)) continue;
        for (k = 0; k < n; k++) have |= out[k] == tieC->a[j];
        if (have) continue;
        /* (ascending, as tri_cells lists them) */
        for (at = n; at > 0 && out[at - 1] > tieC->a[j]; at--) out[at] = out[at - 1];
        out[at] = tieC->a[j];
        n++;
    }
    return n;
}

static int build_grid(const TriVec * tris, unsigned cols, unsigned rows, int cw, int cd, int margin, int halfOpen,
                      const TriVec * tieT, const IntVec * tieC, int tsize, TriVec * cellsOut, IntVec * cs) {
    const int ncell = (int) (cols * rows);
    int *cnt, *pos, i, c, k, cells[512];
    cnt = (int *) calloc((size_t) ncell + 1, sizeof(int));
    pos = (int *) calloc((size_t) ncell + 1, sizeof(int));
    (void) tsize;
    if (cnt == NULL || pos == NULL) {
        free(cnt), free(pos);
        return -1;
    }
    for (i = 0; i < tris->n; i++) {
        int n = tri_cells_tied(&tris->a[i], cw, cd, (int) cols, (int) rows, margin, halfOpen, tieT, tieC, cells);
        for (k = 0; k < n; k++) cnt[cells[k]]++;
    }
    cs->n = 0;
    if (vgrow((void **) &cs->a, &cs->cap, sizeof(int), ncell + 1)) goto oom;
    cs->n = ncell + 1;
    cs->a[0] = 0;
    for (c = 0; c < ncell; c++) cs->a[c + 1] = cs->a[c] + cnt[c];
    cellsOut->n = 0;
    if (cs->a[ncell] && vgrow((void **) &cellsOut->a, &cellsOut->cap, sizeof(Tri), cs->a[ncell])) goto oom;
    cellsOut->n = cs->a[ncell];
    for (i = 0; i < tris->n; i++) {
        int n = tri_cells_tied(&tris->a[i], cw, cd, (int) cols, (int) rows, margin, halfOpen, tieT, tieC, cells);
        for (k = 0; k < n; k++) cellsOut->a[cs->a[cells[k]] + pos[cells[k]]++] = tris->a[i];
    }
    free(cnt), free(pos);
    return 0;
oom:
    free(cnt), free(pos);
    return -1;
}

static int grid_bytes(const TriVec * cells, const IntVec * cs, int tsize, Buf *out) {
    int c, i;
    out->n = 0;
    for (c = 0; c + 1 < cs->n; c++) {
        const uint32_t end = (uint32_t) (out->n + 4 + (size_t) tsize * (size_t) (cs->a[c + 1] - cs->a[c]));
        if (buf_u32(out, end)) return -1;
        for (i = cs->a[c]; i < cs->a[c + 1]; i++) {
            const Tri *t = &cells->a[i];
            int k;
            for (k = 0; k < 9; k++)
                if (buf_u16(out, (unsigned) t->v[k] & 0xFFFF)) return -1;
            if (tsize == 0x14) {
                if (buf_add(out, t->x, 2)) return -1;
            } else {
                if (buf_add(out, t->x, 3) || buf_add(out, &t->x3, 1)) return -1;
            }
        }
    }
    return 0;
}

static int cells_same(const TriVec * a, const IntVec * as, const TriVec * b, const IntVec * bs, int c) {
    int i, n = as->a[c + 1] - as->a[c];
    if (n != bs->a[c + 1] - bs->a[c]) return 0;
    for (i = 0; i < n; i++)
        if (!tri_eq(&a->a[as->a[c] + i], &b->a[bs->a[c] + i])) return 0;
    return 1;
}

/* a grid's bytes: generated from tris; with a base, cells generated alike for both keep the file's bytes */
static int grid_section(const File *fl, unsigned slot, unsigned nextslot, const TriVec * tris, const TriVec * btris,
                        const Geo *g, const Geo *bg, int wk, unsigned cols, unsigned rows, int cw, int cd, int margin,
                        int halfOpen, int tsize, Buf *out) {
    TriVec nc = {0}, oc = {0}, fc = {0}, mix = {0};
    IntVec ns = {0}, os = {0}, fs = {0}, ms = {0};
    int r = -1, c, i;
    char e[8];
    /* wk: the wall grid (0, 1) whose ties apply, -1 the ground grid */
    const TriVec *tt = wk >= 0 ? &g->tieT[wk] : NULL, *btt = wk >= 0 && bg ? &bg->tieT[wk] : NULL;
    const IntVec *tc = wk >= 0 ? &g->tieC[wk] : NULL, *btc = wk >= 0 && bg ? &bg->tieC[wk] : NULL;
    if (build_grid((void *) tris, cols, rows, cw, cd, margin, halfOpen, tt, tc, tsize, (void *) &nc, (void *) &ns))
        goto done;
    if (btris == NULL) {
        r = grid_bytes((void *) &nc, (void *) &ns, tsize, out);
        goto done;
    }
    if (build_grid((void *) btris, cols, rows, cw, cd, margin, halfOpen, btt, btc, tsize, (void *) &oc, (void *) &os) ||
        parse_grid(fl, slot, nextslot, cols * rows, tsize, (void *) &fc, (void *) &fs, e, sizeof e))
        goto done;
    if (VPUSH(ms, 0)) goto done;
    for (c = 0; c < (int) (cols * rows); c++) {
        const int same = cells_same((void *) &nc, (void *) &ns, (void *) &oc, (void *) &os, c);
        const TriVec *src = same ? (void *) &fc : (void *) &nc;
        const IntVec *ss = same ? (void *) &fs : (void *) &ns;
        for (i = ss->a[c]; i < ss->a[c + 1]; i++)
            if (VPUSH(mix, src->a[i])) goto done;
        if (VPUSH(ms, mix.n)) goto done;
    }
    r = grid_bytes((void *) &mix, (void *) &ms, tsize, out);
done:
    free(nc.a), free(oc.a), free(fc.a), free(mix.a), free(ns.a), free(os.a), free(fs.a), free(ms.a);
    return r;
}

static int vis_heights(const Geo *g, unsigned cols, unsigned rows, int cw, int cd, int16_t *mn, int16_t *mx) {
    Map seen;
    int i, k, t;
    if (map_init(&seen, 6, 4096)) return -1;
    for (i = 0; i < (int) (cols * rows); i++) mn[i] = 0, mx[i] = 0;
    for (i = 0; i < g->pc.n; i++)
        for (k = 0; k < g->pc.a[i].ops.n; k++) {
            const Op *o = &g->pc.a[i].ops.a[k];
            if (o->kind != OP_T && o->kind != OP_TRI) continue;
            for (t = 0; t < 3; t++) {
                int16_t key[3];
                int x = vx_x(&o->v[t]), y = vx_y(&o->v[t]), z = vx_z(&o->v[t]), a, b;
                int xs[2], zs[2], nxs = 1, nzs = 1;
                key[0] = (int16_t) x, key[1] = (int16_t) y, key[2] = (int16_t) z;
                if (map_find(&seen, key)) continue;
                if (map_put(&seen, key, 1)) {
                    map_free(&seen);
                    return -1;
                }
                xs[0] = (int) fdiv(x, cw), zs[0] = (int) fdiv(z, cd);
                if (((x % cw) + cw) % cw == 0) xs[nxs++] = xs[0] - 1;
                if (((z % cd) + cd) % cd == 0) zs[nzs++] = zs[0] - 1;
                for (a = 0; a < nxs; a++) {
                    if (xs[a] < 0 || xs[a] >= (int) cols) continue;
                    for (b = 0; b < nzs; b++) {
                        int c;
                        if (zs[b] < 0 || zs[b] >= (int) rows) continue;
                        c = zs[b] * (int) cols + xs[a];
                        if (y < mn[c]) mn[c] = (int16_t) y;
                        if (y > mx[c]) mx[c] = (int16_t) y;
                    }
                }
            }
        }
    map_free(&seen);
    return 0;
}

static int bounds_of(const Geo *g, int16_t out[4]) {
    int i, k;
    if (g->ground.n == 0) return 0;
    for (i = 0; i < g->ground.n; i++)
        for (k = 0; k < 3; k++) {
            const int x = g->ground.a[i].v[3 * k], z = g->ground.a[i].v[3 * k + 2];
            if ((i == 0 && k == 0) || x < out[0]) out[0] = (int16_t) x;
            if ((i == 0 && k == 0) || z < out[1]) out[1] = (int16_t) z;
            if ((i == 0 && k == 0) || x > out[2]) out[2] = (int16_t) x;
            if ((i == 0 && k == 0) || z > out[3]) out[3] = (int16_t) z;
        }
    out[0] = (int16_t) (out[0] - 50), out[1] = (int16_t) (out[1] - 50), out[2] = (int16_t) (out[2] + 50),
    out[3] = (int16_t) (out[3] + 50);
    return 1;
}

/* ------------------------------------------------------------------ edits (geometry.py apply_edit) */
static int protected_pos(const Geo *g, const File *fl, Map *out) {
    const uint8_t *f = fl->f;
    int i, j, k;
    uint32_t p, hi;
    for (i = 0; i < g->pc.n; i++) {
        const Piece *pc = &g->pc.a[i];
        if (!pc->frozen || pc->kind == P_RAW) continue;
        for (j = 0; j < pc->orig.n; j++)
            if (pc->orig.a[j].kind == OP_VTX)
                for (k = 0; k < pc->orig.a[j].nv; k++) {
                    int16_t key[3];
                    const Vx *v = &pc->orig.a[j].v[k];
                    key[0] = (int16_t) vx_x(v), key[1] = (int16_t) vx_y(v), key[2] = (int16_t) vx_z(v);
                    if (map_put(out, key, 1)) return -1;
                }
    }
#define PUT9(q)                                                                       \
    do {                                                                              \
        int t_;                                                                       \
        for (t_ = 0; t_ < 3; t_++) {                                                  \
            int16_t key_[3];                                                          \
            key_[0] = (int16_t) s16at((q) + 6 * t_), key_[1] = (int16_t) s16at((q) + 6 * t_ + 2); \
            key_[2] = (int16_t) s16at((q) + 6 * t_ + 4);                              \
            if (map_put(out, key_, 1)) return -1;                                     \
        }                                                                             \
    } while (0)
    /* 0x74: the moving-part groups' items */
    p = HW(fl, 0x74), hi = HW(fl, 0xA0);
    if (p + 4 <= hi && get_be32(f + p) != 0) {
        p += 4;
        for (;;) {
            int16_t next;
            int32_t count;
            if (p + 0x2C > hi) break;
            next = (int16_t) s16at(f + p);
            count = (int32_t) get_be32(f + p + 0x28);
            p += 0x2C;
            for (i = 0; i < count && p + 0x44 <= hi; i++, p += 0x44) PUT9(f + p);
            if (next == -1) break;
        }
    }
    /* 0x64: switch groups */
    p = HW(fl, 0x64), hi = HW(fl, 0x68);
    while (p + 3 <= hi) {
        unsigned nk = f[p + 1], nt;
        p += 2 + nk;
        if (p >= hi) break;
        nt = f[p++];
        for (i = 0; i < (int) nt && p + 0x14 <= hi; i++, p += 0x14) PUT9(f + p);
    }
    /* 0x68: gate groups */
    p = HW(fl, 0x68), hi = HW(fl, 0x6C);
    while (p + 3 <= hi) {
        unsigned kind = f[p + 1], n = f[p + 2], m, kk;
        p += 3;
        p += kind == 0 ? 12 * n + 4 : 2 * n;
        if (p >= hi) break;
        m = f[p++];
        for (i = 0; i < (int) m && p + 0x15 <= hi; i++, p += 0x15) PUT9(f + p);
        if (p >= hi) break;
        kk = f[p];
        p += 1 + kk;
    }
#undef PUT9
    return 0;
}

static int geo_apply(Geo *g, const LgEdit *ed, const File *fl, char *err, size_t errlen) {
    TriRefVec rt = {0};
    Map prot;
    int i, k, r = -1;
    int *opBase = NULL, *tidOf = NULL;
    char *gone = NULL;
    if (render_tris(g, (void *) &rt)) return -1;
    if (map_init(&prot, 6, 1024)) goto done;
    /* moves */
    if (ed->nmove) {
        Map mv;
        I16Vec to = {0};
        if (map_init(&mv, 6, ed->nmove)) goto done;
        for (i = 0; i < ed->nmove; i++) {
            int *p = map_find(&mv, ed->move[i].from);
            if (p != NULL) {
                if (memcmp(&to.a[*p], ed->move[i].to, 6)) {
                    seterr(err, errlen, "move: (%d, %d, %d) is moved to two places", ed->move[i].from[0],
                           ed->move[i].from[1], ed->move[i].from[2]);
                    map_free(&mv), free(to.a);
                    goto done;
                }
                continue;
            }
            if (map_put(&mv, ed->move[i].from, to.n)) goto done;
            for (k = 0; k < 3; k++)
                if (VPUSH(to, ed->move[i].to[k])) goto done;
        }
        if (protected_pos(g, fl, &prot)) goto done;
        for (i = 0; i < ed->nmove; i++)
            if (map_find(&prot, ed->move[i].from)) {
                seterr(err, errlen, "move: a corner belongs to a bridge, a moving part, a switch or a gate (e.g. (%d, %d, %d)): "
                       "those can't be moved", ed->move[i].from[0], ed->move[i].from[1], ed->move[i].from[2]);
                map_free(&mv), free(to.a);
                goto done;
            }
        map_free(&g->moves);
        g->moves = mv;
        free(g->movedTo.a);
        g->movedTo.a = to.a, g->movedTo.n = to.n, g->movedTo.cap = to.cap;
        for (i = 0; i < g->pc.n; i++) {
            Piece *pc = &g->pc.a[i];
            for (k = 0; k < pc->ops.n; k++) {
                Op *o = &pc->ops.a[k];
                int j;
                if (o->kind != OP_T && o->kind != OP_TRI && o->kind != OP_VTX) continue;
                for (j = 0; j < o->nv; j++) move_of(g, &o->v[j], &o->v[j]);
            }
        }
        {
            TriVec *lists[3];
            int l;
            lists[0] = (void *) &g->ground, lists[1] = (void *) &g->walls[0], lists[2] = (void *) &g->walls[1];
            for (l = 0; l < 3; l++)
                for (i = 0; i < lists[l]->n; i++)
                    for (k = 0; k < 3; k++) {
                        int *p = map_find(&g->moves, &lists[l]->a[i].v[3 * k]);
                        if (p) memcpy(&lists[l]->a[i].v[3 * k], &g->movedTo.a[*p], 6);
                    }
        }
    }
    /* removes */
    gone = (char *) calloc((size_t) rt.n + 1, 1);
    if (gone == NULL) goto done;
    for (i = 0; i < ed->nrem[0]; i++) {
        const int id = ed->rem[0][i];
        if (id < 0 || id >= rt.n) {
            seterr(err, errlen, "remove render %d: no such triangle (0..%d)", id, rt.n - 1);
            goto done;
        }
        gone[id] = 1;
    }
    {
        TriVec *lists[3];
        int l;
        lists[0] = (void *) &g->ground, lists[1] = (void *) &g->walls[0], lists[2] = (void *) &g->walls[1];
        for (l = 0; l < 3; l++) {
            char *drop = (char *) calloc((size_t) lists[l]->n + 1, 1);
            int w = 0;
            if (drop == NULL) goto done;
            for (i = 0; i < ed->nrem[l + 1]; i++) {
                const int id = ed->rem[l + 1][i];
                if (id < 0 || id >= lists[l]->n) {
                    seterr(err, errlen, "remove %s %d: no such triangle (0..%d)", l == 0 ? "ground" : l == 1 ? "walls_a" : "walls_b",
                           id, lists[l]->n - 1);
                    free(drop);
                    goto done;
                }
                drop[id] = 1;
            }
            for (i = 0; i < lists[l]->n; i++)
                if (!drop[i]) lists[l]->a[w++] = lists[l]->a[i];
            lists[l]->n = w;
            free(drop);
        }
    }
    /* adds: drawn (after an id, counted before the removes) */
    for (i = 0; i < ed->nrender; i++) {
        const int id = ed->render[i].after;
        if (id < 0 || id >= rt.n) {
            seterr(err, errlen, "add render: \"after\" %d: no such triangle", id);
            goto done;
        }
        if (g->pc.a[rt.a[id].pi].frozen) {
            seterr(err, errlen, "add render after %d: that triangle is in a list kept as it is", id);
            goto done;
        }
        for (k = 0; k < 3; k++) {
            int c;
            for (c = 5; c < 9; c++)
                if (ed->render[i].v[k][c] < 0 || ed->render[i].v[k][c] > 255) {
                    seterr(err, errlen, "add render: colour %d: 0..255 expected", ed->render[i].v[k][c]);
                    goto done;
                }
        }
    }
    /* per op (numbered over the pieces as they are now): its render id; the pieces an edit reaches */
    {
        int total = 0;
        opBase = (int *) calloc((size_t) g->pc.n + 1, sizeof(int));
        if (opBase == NULL) goto done;
        for (i = 0; i < g->pc.n; i++) opBase[i] = total, total += g->pc.a[i].ops.n;
        tidOf = (int *) malloc(sizeof(int) * ((size_t) total + 1));
        if (tidOf == NULL) goto done;
        for (i = 0; i < total; i++) tidOf[i] = -1;
        for (k = 0; k < rt.n; k++) {
            tidOf[opBase[rt.a[k].pi] + rt.a[k].oi] = k;
            if (gone[k]) g->pc.a[rt.a[k].pi].restructured = 1;
        }
        for (k = 0; k < ed->nrender; k++) g->pc.a[rt.a[ed->render[k].after].pi].restructured = 1;
    }
    for (i = 0; i < g->pc.n; i++) {
        Piece *pc = &g->pc.a[i];
        OpVec nw = {0};
        int oi, before = 0;
        if (!pc->restructured) continue;
        for (oi = 0; oi < pc->ops.n; oi++) {
            int tid = tidOf[opBase[i] + oi], a;
            if (tid >= 0 && gone[tid]) {
                if (oi < pc->setup) before++;
                op_free(&pc->ops.a[oi]);
            } else if (VPUSH(nw, pc->ops.a[oi])) {
                goto done;
            }
            pc->ops.a[oi].v = NULL;
            for (a = 0; tid >= 0 && a < ed->nrender; a++) {
                Op t;
                int c;
                if (ed->render[a].after != tid) continue;
                memset(&t, 0, sizeof t);
                t.kind = OP_T, t.w0 = 0xBF000000u, t.w1 = (uint32_t) (ed->render[a].flag & 0xFF), t.nv = 3;
                t.v = (Vx *) calloc(3, sizeof(Vx));
                if (t.v == NULL) goto done;
                for (c = 0; c < 3; c++) {
                    const int16_t *s = ed->render[a].v[c];
                    uint8_t *b = t.v[c].b;
                    put_be16(b, (unsigned) s[0] & 0xFFFF), put_be16(b + 2, (unsigned) s[1] & 0xFFFF), put_be16(b + 4, (unsigned) s[2] & 0xFFFF);
                    put_be16(b + 6, 0), put_be16(b + 8, (unsigned) s[3] & 0xFFFF), put_be16(b + 10, (unsigned) s[4] & 0xFFFF);
                    b[12] = (uint8_t) s[5], b[13] = (uint8_t) s[6], b[14] = (uint8_t) s[7], b[15] = (uint8_t) s[8];
                }
                if (VPUSH(nw, t)) goto done;
            }
        }
        if (pc->kind == P_REC || pc->kind == P_GRP) pc->setup -= before;
        free(pc->ops.a);
        pc->ops.a = nw.a, pc->ops.n = nw.n, pc->ops.cap = nw.cap;
    }
    /* floor: first in the order; Rare's under them lose their stop flag */
    if (ed->nground) {
        TriVec all = {0};
        for (i = 0; i < ed->nground; i++) {
            Tri t;
            memset(&t, 0, sizeof t);
            memcpy(t.v, ed->ground[i].v, sizeof t.v);
            if (ed->ground[i].a < 0 || ed->ground[i].a > 255 || ed->ground[i].b < 0 || ed->ground[i].b > 255) {
                seterr(err, errlen, "add ground: surface / flag 0..255");
                free(all.a);
                goto done;
            }
            t.x[0] = (uint8_t) ed->ground[i].a, t.x[1] = (uint8_t) ed->ground[i].b;
            if (VPUSH(all, t)) goto done;
        }
        for (i = 0; i < g->ground.n; i++) {
            Tri *t = &g->ground.a[i];
            if (!t->x[1]) continue;
            for (k = 0; k < ed->nground; k++)
                if (tris_overlap(t->v, ed->ground[k].v)) {
                    t->x[1] = 0;
                    break;
                }
        }
        for (i = 0; i < g->ground.n; i++)
            if (VPUSH(all, g->ground.a[i])) goto done;
        free(g->ground.a);
        g->ground.a = all.a, g->ground.n = all.n, g->ground.cap = all.cap;
    }
    for (k = 0; k < 2; k++)
        for (i = 0; i < ed->nwall[k]; i++) {
            Tri t;
            int h = ed->wall[k][i].a < 0 ? wall_heading(ed->wall[k][i].v) : ed->wall[k][i].a;
            if (h > 0xFFF) {
                seterr(err, errlen, "add walls: heading 0..4095");
                goto done;
            }
            memset(&t, 0, sizeof t);
            memcpy(t.v, ed->wall[k][i].v, sizeof t.v);
            t.x[0] = (uint8_t) (h >> 8), t.x[1] = (uint8_t) h, t.x[2] = (uint8_t) ed->wall[k][i].b, t.x3 = (uint8_t) ed->wall[k][i].c;
            if (VPUSH(g->walls[k], t)) goto done;
        }
    for (i = 0; i < ed->nsettimg; i++) {
        const LgSettimg *s = &ed->settimg[i];
        Piece *pc;
        int n = -1, done1 = 0, l;
        if (s->piece < 0 || s->piece >= g->pc.n) {
            seterr(err, errlen, "settimg: no piece %d", s->piece);
            goto done;
        }
        pc = &g->pc.a[s->piece];
        for (l = 0; l < 2; l++) {
            OpVec *ops = l ? (void *) &pc->orig : (void *) &pc->ops;
            n = -1;
            for (k = 0; k < ops->n; k++)
                if (ops->a[k].kind == OP_C && ops->a[k].w0 >> 24 == 0xFD && ++n == s->k) {
                    ops->a[k].w1 = s->tex;
                    if (!l) done1 = 1;
                    break;
                }
        }
        if (!done1) {
            seterr(err, errlen, "settimg: piece %d has no G_SETTIMG %d", s->piece, s->k);
            goto done;
        }
    }
    r = 0;
done:
    free(rt.a), free(gone), free(opBase), free(tidOf);
    map_free(&prot);
    return r;
}

/* ------------------------------------------------------------------ putting it together */
/* visible list k's commands with every cell visible, as lvl_build_visible_dl copies them into the buffer
 * lvl_alloc_dl_buffers gives it: the fixed list; the records [0xA0 + 4k, 0xA4 + 4k) in order, a run with one id
 * the first from its start and the rest from their alternate start, after each run the groups with its id from
 * their alternate start; then the groups whose id no record had (lvl_copy_visible_groups: from the start, or the
 * alternate start when the id is the one copied just before); the closing G_ENDDL (-1: the records don't read) */
static int visible_count(const File *fl, int k) {
    const uint8_t *f = fl->f;
    const uint32_t r0 = HW(fl, 0xA0 + 4 * (unsigned) k), r1 = HW(fl, 0xA4 + 4 * (unsigned) k);
    const uint32_t g0 = HW(fl, 0xB0 + 4 * (unsigned) k), g1 = HW(fl, 0xB4 + 4 * (unsigned) k);
    long n = 1 + (long) (HW(fl, 0x8C) - HW(fl, 0x88)) / 8;
    uint32_t p, q;
    IntVec ids = {0};
    int bad = r1 < r0 || g1 < g0 || r1 > fl->size || g1 > fl->size || (r1 - r0) % 0x14;
    for (p = r0; !bad && p < r1;) {
        const int32_t id = (int32_t) get_be32(f + p + 0xC);
        n += ((long) get_be32(f + p + 8) - (long) get_be32(f + p)) / 8;
        bad = VPUSH(ids, id) != 0;
        for (p += 0x14; !bad && p < r1 && (int32_t) get_be32(f + p + 0xC) == id; p += 0x14)
            n += ((long) get_be32(f + p + 8) - (long) get_be32(f + p + 4)) / 8;
        for (q = g0; !bad && q + 0x14 <= g1; q += 0x14 + 4 * get_be32(f + q + 0x10)) {
            const int32_t gid = (int32_t) get_be32(f + q + 0xC);
            if (id < gid) break;
            if (id == gid) n += ((long) get_be32(f + q + 8) - (long) get_be32(f + q + 4)) / 8;
        }
    }
    {
        int32_t prev = -2;
        for (q = g0; !bad && q + 0x14 <= g1; q += 0x14 + 4 * get_be32(f + q + 0x10)) {
            const int32_t gid = (int32_t) get_be32(f + q + 0xC);
            int i, seen = 0;
            for (i = 0; i < ids.n && !seen; i++) seen = ids.a[i] == gid;
            if (seen) continue;
            n += ((long) get_be32(f + q + 8) - (long) get_be32(f + q + (gid == prev ? 4 : 0))) / 8;
            prev = gid;
        }
    }
    free(ids.a);
    return bad ? -1 : (int) n;
}

/* the visible lists' budgets of an edited level file: list k with every cell visible may hold no more than the
 * game allocates (lvl_alloc_dl_buffers; levels 10, 11, 16, 17, 29 size them themselves), or than the original
 * level's own count where that is more already (none of the 60 US levels) */
static int check_budgets(const uint8_t *out, size_t outsize, const File *orig, int levelno, char *err, size_t errlen) {
    static const int caps0[4] = {6000, 5000, 100, 2000}, caps1[4] = {1000, 1000, 100, 2000};
    File fl;
    int k, r = 0;
    if (levelno < 0 || levelno == 10 || levelno == 11 || levelno == 16 || levelno == 17 || levelno == 29) return 0;
    if (file_open(&fl, out, outsize, err, errlen)) return -1;
    for (k = 0; k < 4 && r == 0; k++) {
        const int *caps = fl.dlprofile == 0 ? caps0 : caps1;
        const int n = visible_count(&fl, k), own = orig ? visible_count(orig, k) : 0;
        if (n < 0) {
            seterr(err, errlen, "visible list %d: the records don't read", k);
            r = -1;
        } else if (n > caps[k] && n > own) {
            if (own > caps[k])
                seterr(err, errlen, "visible list %d: %d commands with every cell visible; the game allocates %d "
                       "(the level's own: %d)", k, n, caps[k], own);
            else
                seterr(err, errlen, "visible list %d: %d commands with every cell visible; the game allocates %d", k, n,
                       caps[k]);
            r = -1;
        }
    }
    file_free(&fl);
    return r;
}

static int write_geo(const Geo *g, const File *fl, const Geo *bg, const File *bfl, int levelno, LmLevel *lv, char *err,
                     size_t errlen) {
    DlOut d;
    Buf sec = {0};
    int k, i, r = -1;
    const unsigned *u = fl->u16;
    uint32_t base = lm_dl_base(lv), rel[9], nw;
    static const unsigned kDl[9] = {0x7C, 0x80, 0x84, 0x88, 0x8C, 0x90, 0x94, 0x98, 0x9C};
    if (build_dl(g, bg, &d, err, errlen)) return -1;
    /* the header's words, the records' and groups' offsets */
    for (k = 0; k < 9; k++) {
        if (kDl[k] == 0x84) {
            rel[k] = (uint32_t) d.dl.n;
            continue;
        }
        if (!bound_find(&d, HW(fl, kDl[k]) - fl->base, &nw)) {
            seterr(err, errlen, "header word 0x%X points into a list", kDl[k]);
            goto done;
        }
        rel[k] = nw;
    }
    {
        uint8_t *vb = (uint8_t *) malloc(sizeof(Vx) * (size_t) (d.va.v.n + 1));
        if (vb == NULL) goto done;
        memcpy(vb, d.va.v.a, sizeof(Vx) * (size_t) d.va.v.n);
        if (lm_set_display(lv, vb, sizeof(Vx) * (size_t) d.va.v.n, d.dl.p, d.dl.n, rel)) {
            free(vb);
            goto done;
        }
        free(vb);
    }
    for (k = 0; k < 4; k++) {
        /* records */
        sec.n = 0;
        for (i = 0; i < fl->rec[k].n / 5; i++) {
            uint32_t w[5];
            int j, p;
            memcpy(w, fl->rec[k].a + 5 * i, sizeof w);
            for (p = 0; p < g->pc.n; p++)
                if (g->pc.a[p].kind == P_REC && g->pc.a[p].refk == k && g->pc.a[p].refi == i) {
                    w[0] = base + d.pStart.a[p];
                    w[1] = base + d.pStart.a[p] + 8 * (uint32_t) d.pSetup.a[p];
                    w[2] = base + d.pEnd.a[p];
                }
            /* (records the pieces don't own keep their offsets relative to the block) */
            for (p = 0, j = 0; p < g->pc.n; p++)
                if (g->pc.a[p].kind == P_REC && g->pc.a[p].refk == k && g->pc.a[p].refi == i) j = 1;
            if (!j) {
                w[0] = w[0] - fl->base + base, w[1] = w[1] - fl->base + base, w[2] = w[2] - fl->base + base;
            }
            for (j = 0; j < 5; j++)
                if (buf_u32(&sec, w[j])) goto done;
        }
        if (lm_set_raw_section(lv, 0xA0 + 4 * (unsigned) k, sec.p, sec.n)) goto done;
        sec.n = 0;
        for (i = 0; i < fl->grpAt[k].n; i++) {
            const uint32_t *w0 = fl->grp[k].a + fl->grpAt[k].a[i];
            uint32_t w[3];
            int j, p, own = 0;
            w[0] = w0[0] - fl->base + base, w[1] = w0[1] - fl->base + base, w[2] = w0[2] - fl->base + base;
            for (p = 0; p < g->pc.n; p++)
                if (g->pc.a[p].kind == P_GRP && g->pc.a[p].refk == k && g->pc.a[p].refi == i) {
                    w[0] = base + d.pStart.a[p];
                    w[1] = base + d.pStart.a[p] + 8 * (uint32_t) d.pSetup.a[p];
                    w[2] = base + d.pEnd.a[p];
                    own = 1;
                }
            (void) own;
            for (j = 0; j < 3; j++)
                if (buf_u32(&sec, w[j])) goto done;
            for (j = 3; j < 5 + (int) w0[4]; j++)
                if (buf_u32(&sec, w0[j])) goto done;
        }
        if (lm_set_raw_section(lv, 0xB0 + 4 * (unsigned) k, sec.p, sec.n)) goto done;
    }
    /* grids */
    sec.n = 0;
    if (grid_section(bfl ? bfl : fl, 0x30, 0x34, (void *) &g->ground, bg ? (void *) &bg->ground : NULL, g, bg, -1, u[4], u[5],
                     (int) u[6], (int) u[7], 0, 0, 0x14, &sec) ||
        lm_set_raw_section(lv, 0x30, sec.p, sec.n))
        goto done;
    for (k = 0; k < 2; k++) {
        sec.n = 0;
        if (grid_section(bfl ? bfl : fl, k ? 0x70 : 0x6C, k ? 0x74 : 0x70, (void *) &g->walls[k],
                         bg ? (void *) &bg->walls[k] : NULL, g, bg, k, u[8], u[9], (int) u[10], (int) u[11], 50, 1, 0x16,
                         &sec) ||
            lm_set_raw_section(lv, k ? 0x70 : 0x6C, sec.p, sec.n))
            goto done;
    }
    /* visibility heights */
    {
        const int n = (int) (u[0] * u[1]);
        int16_t *mn = (int16_t *) calloc((size_t) n + 1, 2), *mx = (int16_t *) calloc((size_t) n + 1, 2);
        int16_t *bmn = (int16_t *) calloc((size_t) n + 1, 2), *bmx = (int16_t *) calloc((size_t) n + 1, 2);
        int c, bad = mn == NULL || mx == NULL || bmn == NULL || bmx == NULL;
        if (!bad) bad = vis_heights(g, u[0], u[1], (int) u[2], (int) u[3], mn, mx);
        if (!bad && bg) bad = vis_heights(bg, u[0], u[1], (int) u[2], (int) u[3], bmn, bmx);
        sec.n = 0;
        for (c = 0; c < n && !bad; c++) {
            int a = mn[c], b = mx[c];
            if (bg && mn[c] == bmn[c] && mx[c] == bmx[c]) {
                const uint8_t *q = bfl->f + HW(bfl, 0x48) + 4 * (uint32_t) c;
                a = s16at(q), b = s16at(q + 2);
            }
            bad = buf_u16(&sec, (unsigned) a & 0xFFFF) || buf_u16(&sec, (unsigned) b & 0xFFFF);
        }
        free(mn), free(mx), free(bmn), free(bmx);
        if (bad || lm_set_raw_section(lv, 0x48, sec.p, sec.n)) goto done;
    }
    /* bounds */
    {
        int16_t b[4], bb[4];
        if (bounds_of(g, b)) {
            sec.n = 0;
            if (bg && bounds_of(bg, bb) && !memcmp(b, bb, sizeof b)) {
                const uint8_t *q = bfl->f + HW(bfl, 0x4C);
                for (k = 0; k < 4; k++) b[k] = (int16_t) s16at(q + 2 * k);
            }
            for (k = 0; k < 4; k++)
                if (buf_u16(&sec, (unsigned) b[k] & 0xFFFF)) goto done;
            if (lm_set_raw_section(lv, 0x4C, sec.p, sec.n)) goto done;
        }
    }
    /* (the visible lists' budgets: check_budgets, on the finished file) */
    (void) levelno;
    r = 0;
done:
    free(sec.p);
    dlout_free(&d);
    return r;
}

void lg_edit_free(LgEdit *e) {
    int k;
    if (e == NULL) return;
    free(e->move), free(e->render), free(e->ground), free(e->wall[0]), free(e->wall[1]), free(e->settimg);
    for (k = 0; k < 4; k++) free(e->rem[k]);
    memset(e, 0, sizeof *e);
}

int lg_edit_empty(const LgEdit *e) {
    return e == NULL || (e->nmove == 0 && e->nrem[0] == 0 && e->nrem[1] == 0 && e->nrem[2] == 0 && e->nrem[3] == 0 &&
                         e->nrender == 0 && e->nground == 0 && e->nwall[0] == 0 && e->nwall[1] == 0 && e->nsettimg == 0);
}

static int finish(LmLevel *lv, uint8_t **out, size_t *outsize, char *err, size_t errlen) {
    *out = lm_build(lv, outsize, NULL);
    if (*out == NULL) {
        seterr(err, errlen, "out of memory");
        return -1;
    }
    return 0;
}

int lg_apply(const uint8_t *cur, size_t cursize, const uint8_t *orig, size_t origsize, const LgEdit *e, int levelno,
             uint8_t **out, size_t *outsize, char *err, size_t errlen) {
    File fc, fo;
    Geo g, bg;
    LmLevel *lv = NULL;
    int r = -1;
    *out = NULL;
    memset(&g, 0, sizeof g), memset(&bg, 0, sizeof bg);
    if (file_open(&fc, cur, cursize, err, errlen)) return -1;
    if (file_open(&fo, orig, origsize, err, errlen)) {
        file_free(&fc);
        return -1;
    }
    if (geo_from_file(&g, &fc, err, errlen) || geo_from_file(&bg, &fo, err, errlen)) goto done;
    if (geo_apply(&g, e, &fc, err, errlen)) goto done;
    lv = lm_open(cur, cursize, err, errlen);
    if (lv == NULL) goto done;
    if (write_geo(&g, &fc, &bg, &fo, levelno, lv, err, errlen)) goto done;
    r = finish(lv, out, outsize, err, errlen);
    if (r == 0 && check_budgets(*out, *outsize, &fo, levelno, err, errlen)) {
        free(*out);
        *out = NULL;
        r = -1;
    }
done:
    lm_free(lv);
    geo_free(&g), geo_free(&bg);
    file_free(&fc), file_free(&fo);
    return r;
}

/* ------------------------------------------------------------------ the editor's view (lg_mesh_*) */
struct LgMesh {
    uint8_t *f;
    size_t size;
    File fl;
    Geo g;
    Map prot;
    LgTri *t[4];
    int n[4];
};

void lg_mesh_free(LgMesh *m) {
    int k;
    if (m == NULL) return;
    geo_free(&m->g);
    file_free(&m->fl);
    map_free(&m->prot);
    for (k = 0; k < 4; k++) free(m->t[k]);
    free(m->f);
    free(m);
}

LgMesh *lg_mesh_open(const uint8_t *file, size_t size, char *err, size_t errlen) {
    LgMesh *m = (LgMesh *) calloc(1, sizeof(LgMesh));
    TriRefVec rt = {0};
    int i, k, s;
    /* the texture state, walked through the block in order (gltf.py walk_linear: lists in a row) */
    int tex = -1, on = 0, fmt = 0, siz = 0, w = 32, h = 32, masks = 0, maskt = 0;
    int texLevel = 0, lbDxt = -1, tlut = -1, pal = 0, tlutn = 0;
    unsigned uls = 0, ult = 0, lrs = 0, lrt = 0;
    if (m == NULL) return NULL;
    m->f = (uint8_t *) malloc(size ? size : 1);
    if (m->f == NULL) goto fail;
    memcpy(m->f, file, size);
    m->size = size;
    if (file_open(&m->fl, m->f, size, err, errlen) || geo_from_file(&m->g, &m->fl, err, errlen)) goto fail;
    if (map_init(&m->prot, 6, 1024) || protected_pos(&m->g, &m->fl, &m->prot) || render_tris(&m->g, (void *) &rt)) goto fail;
    m->n[0] = rt.n, m->n[1] = m->g.ground.n, m->n[2] = m->g.walls[0].n, m->n[3] = m->g.walls[1].n;
    for (k = 0; k < 4; k++) {
        m->t[k] = (LgTri *) calloc((size_t) m->n[k] + 1, sizeof(LgTri));
        if (m->t[k] == NULL) goto fail;
    }
    s = 0;
    for (i = 0; i < m->g.pc.n; i++) {
        const Piece *pc = &m->g.pc.a[i];
        int nset = -1, kTimg = -1;
        for (k = 0; k < pc->ops.n; k++) {
            const Op *o = &pc->ops.a[k];
            const unsigned op = o->w0 >> 24;
            if (o->kind == OP_C) {
                if (op == 0xBB) on = (int) (o->w0 & 0xFF), texLevel = (int) ((o->w0 >> 11) & 7);
                else if (op == 0xFD) tex = (int) o->w1, kTimg = ++nset;
                else if (op == 0xF3) lbDxt = (int) (o->w1 & 0xFFF);
                else if (op == 0xF0) tlut = tex, tlutn = (int) ((o->w1 >> 14) & 0x3FF) + 1;
                else if (op == 0xF5 && ((o->w1 >> 24) & 7) == 0) {
                    fmt = (int) ((o->w0 >> 21) & 7), siz = 4 << ((o->w0 >> 19) & 3);
                    maskt = (int) ((o->w1 >> 14) & 0xF), masks = (int) ((o->w1 >> 4) & 0xF);
                    pal = (int) ((o->w1 >> 20) & 0xF);
                } else if (op == 0xF2 && ((o->w1 >> 24) & 7) == 0) {
                    uls = (o->w0 >> 12) & 0xFFF, ult = o->w0 & 0xFFF, lrs = (o->w1 >> 12) & 0xFFF, lrt = o->w1 & 0xFFF;
                }
                continue;
            }
            if (o->kind != OP_T && o->kind != OP_TRI) continue;
            if (s < rt.n) {
                LgTri *t = &m->t[0][s];
                int c;
                for (c = 0; c < 3; c++) {
                    const uint8_t *b = o->v[c].b;
                    t->p[c][0] = (int16_t) s16at(b), t->p[c][1] = (int16_t) s16at(b + 2), t->p[c][2] = (int16_t) s16at(b + 4);
                    t->vflag[c] = (uint16_t) get_be16(b + 6);
                    t->st[c][0] = (int16_t) s16at(b + 8), t->st[c][1] = (int16_t) s16at(b + 10);
                    memcpy(t->rgba[c], b + 12, 4);
                }
                t->piece = i;
                t->frozen = pc->frozen;
                t->flag = o->kind == OP_T ? (int) (o->w1 & 0xFF) : (int) ((o->w1 >> 24) & 0xFF);
                t->tex = on ? tex : -1;
                t->timg = kTimg;
                t->fmt = fmt, t->siz = siz;
                w = (int) ((lrs - uls) / 4 + 1), h = (int) ((lrt - ult) / 4 + 1);
                if ((int) (lrs - uls) < 0 || w <= 0) w = masks ? 1 << masks : 32;
                if ((int) (lrt - ult) < 0 || h <= 0) h = maskt ? 1 << maskt : 32;
                t->w = w, t->h = h;
                t->mips = texLevel > 0 && lbDxt == 0 ? texLevel + 1 : 1;
                t->tlut = fmt == 2 ? tlut : -1, t->pal = pal, t->tlutn = fmt == 2 ? tlutn : 0;
                t->b = t->c = 0;
                s++;
            }
        }
    }
    for (k = 1; k < 4; k++) {
        const TriVec *v = k == 1 ? (const TriVec *) &m->g.ground : (const TriVec *) &m->g.walls[k - 2];
        for (i = 0; i < v->n; i++) {
            LgTri *t = &m->t[k][i];
            const Tri *x = &v->a[i];
            int c;
            for (c = 0; c < 9; c++) t->p[c / 3][c % 3] = x->v[c];
            t->piece = -1, t->tex = -1, t->timg = -1;
            if (k == 1) t->flag = x->x[0], t->b = x->x[1];
            else t->flag = x->x[0] << 8 | x->x[1], t->b = x->x[2], t->c = x->x3;
        }
    }
    free(rt.a);
    return m;
fail:
    free(rt.a);
    lg_mesh_free(m);
    return NULL;
}

int lg_mesh_count(const LgMesh *m, int set) {
    return m && set >= 0 && set < 4 ? m->n[set] : 0;
}

const LgTri *lg_mesh_tri(const LgMesh *m, int set, int i) {
    return m && set >= 0 && set < 4 && i >= 0 && i < m->n[set] ? &m->t[set][i] : NULL;
}

int lg_mesh_protected(const LgMesh *m, const int16_t p[3]) {
    return m != NULL && map_find(&m->prot, p) != NULL;
}

int lg_budgets(const uint8_t *file, size_t size, int levelno, int n[4], int cap[4], char *err, size_t errlen) {
    File fl;
    int k, r = -1;
    static const int caps0[4] = {6000, 5000, 100, 2000}, caps1[4] = {1000, 1000, 100, 2000};
    if (file_open(&fl, file, size, err, errlen)) return -1;
    for (k = 0; k < 4; k++) {
        const int special = levelno == 10 || levelno == 11 || levelno == 16 || levelno == 17 || levelno == 29;
        cap[k] = special ? 0 : (fl.dlprofile == 0 ? caps0 : caps1)[k];
        n[k] = visible_count(&fl, k);
        if (n[k] < 0) {
            seterr(err, errlen, "visible list %d: the records don't read", k);
            goto done;
        }
    }
    r = 0;
done:
    file_free(&fl);
    return r;
}

int lg_rebuild(const uint8_t *file, size_t size, int keep, uint8_t **out, size_t *outsize, char *err, size_t errlen) {
    File fl;
    Geo g, bg;
    LmLevel *lv = NULL;
    int r = -1;
    *out = NULL;
    memset(&g, 0, sizeof g), memset(&bg, 0, sizeof bg);
    if (file_open(&fl, file, size, err, errlen)) return -1;
    if (geo_from_file(&g, &fl, err, errlen)) goto done;
    if (keep && geo_from_file(&bg, &fl, err, errlen)) goto done;
    lv = lm_open(file, size, err, errlen);
    if (lv == NULL) goto done;
    if (write_geo(&g, &fl, keep ? &bg : NULL, keep ? &fl : NULL, -1, lv, err, errlen)) goto done;
    r = finish(lv, out, outsize, err, errlen);
done:
    lm_free(lv);
    geo_free(&g), geo_free(&bg);
    file_free(&fl);
    return r;
}
