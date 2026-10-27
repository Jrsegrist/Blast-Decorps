/* Level geometry (level_geo.c): a level file's triangles and the sections the
 * game derives from them, regenerated after an edit - the C twin of
 * tools_port/bcfiles/geometry.py (the same rules, so both build the same
 * bytes; docs/tools.md "Geometry mods", docs/systems/levels.md).
 *
 *   ground grid (0x30)        a floor triangle is listed in every cell its x-z
 *                             footprint meets, not in a cell lying strictly
 *                             inside it; one global order (Rare's per-cell
 *                             orders kept)
 *   wall grids (0x6C, 0x70)   the same with the cells grown by 50 map units,
 *                             half-open
 *   visibility heights (0x48) min(0, y) / max(0, y) of the drawn vertices in
 *                             each cell's closed rectangle
 *   bounds (0x4C)             the floor triangles' extent +-50
 *   display lists (0x78..)    every list written again: triangles re-packed into
 *                             G_VTX loads of at most 16 between the same
 *                             vertex-state commands, G_CULLDL boxes the lists'
 *                             bounding boxes, offsets moved; lists the edit
 *                             doesn't reach keep their commands and vertices
 *
 * Wherever an edit doesn't reach, Rare's data is kept, so an empty edit gives
 * the file back byte for byte.  Plain C, no RDRAM: bc_cameratest tests it, and
 * bc_headless / bc.exe apply geometry mods (bcmod/2) with it when a level loads. */
#ifndef LEVEL_GEO_H
#define LEVEL_GEO_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* a geometry edit (a bcmod/2 level's "geometry"; geometry.py apply_edit) */
typedef struct {
    int16_t from[3], to[3];
} LgMove;
typedef struct {
    int after;             /* render triangle id: inserted after it, in its state */
    int16_t v[3][9];       /* per corner x, y, z, s, t, r, g, b, a */
    int flag;
} LgAddRender;
typedef struct {
    int16_t v[9];
    int a, b, c;           /* ground: surface, flag; walls: heading (-1: the normal's), side, fail */
} LgAddTri;
typedef struct {
    int piece, k;
    uint32_t tex;
} LgSettimg;
typedef struct {
    int nmove, nrem[4], nrender, nground, nwall[2], nsettimg;
    LgMove *move;
    int *rem[4];           /* render, ground, walls_a, walls_b ids (of the unedited level) */
    LgAddRender *render;
    LgAddTri *ground, *wall[2];
    LgSettimg *settimg;
} LgEdit;

void lg_edit_free(LgEdit *e);
int lg_edit_empty(const LgEdit *e);

/* the edit over the level file `cur` (the record changes already made), with
 * `orig` (the ROM's file) for what the edit doesn't reach; levelno for the
 * visible lists' sizes.  0 ok (*out malloc'd), -1 and *err */
int lg_apply(const uint8_t *cur, size_t cursize, const uint8_t *orig, size_t origsize, const LgEdit *e, int levelno,
             uint8_t **out, size_t *outsize, char *err, size_t errlen);
/* (testing) every derived section regenerated from the file's own triangles, Rare's data not kept (bcfiles
 * geo-rebuild), or kept (keep: the file back) */
int lg_rebuild(const uint8_t *file, size_t size, int keep, uint8_t **out, size_t *outsize, char *err, size_t errlen);

/* ---- a level's triangles as an edit names them (bc.exe's terrain tools) ----
 * Sets: 0 drawn (render: the block's order, Geometry.render_tris), 1 floor (ground), 2 walls_a, 3 walls_b (their
 * global orders): the ids an LgEdit's removes and "after" count. */
enum { LG_RENDER, LG_GROUND, LG_WALLS_A, LG_WALLS_B };
typedef struct {
    int16_t p[3][3];       /* the corners (x, y, z) */
    int16_t st[3][2];      /* drawn: texture coordinates (s, t) */
    uint8_t rgba[3][4];    /* drawn: vertex colours */
    uint16_t vflag[3];     /* drawn: the Vtx flag words */
    int piece;             /* drawn: its list (the settimg op's piece number); else -1 */
    int frozen;            /* drawn: in a list kept as it is (bridges): nothing can be added after it */
    int flag;              /* drawn: the G_TRI1 flag byte; floor: surface; walls: heading */
    int b, c;              /* floor: its flag; walls: side, fail */
    int tex;               /* drawn: the texture id of its state (a walk of the block in order; -1: untextured) */
    int timg;              /* that G_SETTIMG's number in its list (an settimg op's k; -1: an earlier list set it) */
    int fmt, siz, w, h;    /* the render tile: format (0 RGBA, 2 CI, 3 IA, 4 I), bits per texel, width, height */
    int mips;              /* drawn: the levels of detail its texture is drawn with (G_TEXTURE's level + 1 after a
                              G_LOADBLOCK with dxt 0: a mipmapped slot, tex_codec.h); 1: none */
    int tlut;              /* drawn, CI: the texture id of the palette (G_LOADTLUT's G_SETTIMG; -1: none), its palette
                              number (the render tile's) and entries loaded */
    int pal, tlutn;
} LgTri;
typedef struct LgMesh LgMesh;
LgMesh *lg_mesh_open(const uint8_t *file, size_t size, char *err, size_t errlen);
void lg_mesh_free(LgMesh *m);
int lg_mesh_count(const LgMesh *m, int set);
const LgTri *lg_mesh_tri(const LgMesh *m, int set, int i);
/* a corner of a bridge, a moving part, a switch or a gate: lg_apply refuses to move it */
int lg_mesh_protected(const LgMesh *m, const int16_t p[3]);

/* the per-frame visible lists with every cell visible, counted as lvl_build_visible_dl copies them (a record of a
 * run from its alternate start, the groups likewise): commands per list (n) and what lvl_alloc_dl_buffers gives
 * them (cap; 0: levels 10, 11, 16, 17, 29 size them themselves); 0 ok.  (lg_apply refuses an edit that takes a
 * list past its cap, or past the level's own count should that be more already: no US level) */
int lg_budgets(const uint8_t *file, size_t size, int levelno, int n[4], int cap[4], char *err, size_t errlen);

#ifdef __cplusplus
}
#endif

#endif
