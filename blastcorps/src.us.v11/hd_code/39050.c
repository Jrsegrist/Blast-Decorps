#include "common.h"
#include <ultra64.h>
#ifndef NON_MATCHING /* legacy declarations */
/* Matching build: IDO compiled the matched code here against older
 * declarations of these, which the file keeps; the NON_MATCHING build
 * uses game/game.h's. */
#define LEGACY_gHeapPtr
#define LEGACY_gVehiclesEnd
#endif /* legacy declarations */
#include "game/game.h"
/* Views: this file reads these shared variables (game/variables.h) as other types. */
#define gVehicles ((Digger *) gVehicles)
#ifdef NON_MATCHING
#define gHeapPtr (*(s32 *) &gHeapPtr)
#define gVehiclesEnd (*(Digger * N64P *) &gVehiclesEnd)
#endif
/* end of views */

#define ABS(x) ((x) >= 0.0f ? (x) : -(x))

/* One entry of the digger/object table at gVehicles (0x74 bytes each);
 * gVehiclesEnd points one past the last live entry. */
typedef struct {
    u8 pad0[0x5C];
    s32 type;  /* 0x5C: digger weight class, see bridge_digger_weight */
    s32 pad60;
    s32 unk64; /* 0x64 */
    s32 pad68;
    s32 unk6C; /* 0x6C */
    s32 unk70; /* 0x70 */
} Digger;

#ifndef NON_MATCHING
extern Digger *gVehiclesEnd;
#endif
extern s32 gBridgeWaveClock;

f32 sinf(f32);
s32 bridge_point_in_quad(s32 arg0, s32 arg1, void *arg2, void *arg3);
f32 bridge_cross_fraction(s32, s32, s32 *, s32 *);
#ifdef NON_MATCHING
f32 bridge_digger_weight(u8 type);
#else
f32 bridge_digger_weight(); /* K&R */
#endif
f32 bridge_max_load(s32 *a, s32 *b, s32 arg2);
void bridge_sag_group(s32 arg0, s32 arg1, s32 arg2);

/* Animated water surface, one entry per level that has one (0x34 bytes). The surface is an
 * (nx + 1) x (nz + 1) vertex grid over x0..x1, z0..z1, displaced by two sine waves. */
typedef struct {
    u8 id;     /* level id */
    u8 nx;     /* 0x01: grid cells along x */
    u8 nz;     /* 0x02: grid cells along z */
    u8 pad3;
    s16 x0;    /* 0x04 */
    s16 z0;    /* 0x06 */
    s16 x1;    /* 0x08 */
    s16 z1;    /* 0x0A */
    s16 y;     /* 0x0C: rest height */
    s16 ampX;  /* 0x0E */
    s16 ampZ;  /* 0x10 */
    s16 pad12;
    f32 rateX; /* 0x14 */
    f32 rateZ; /* 0x18 */
    f32 lenX;  /* 0x1C */
    f32 lenZ;  /* 0x20 */
    u8 unk24;  /* 0x24: nonzero = single texture (tex), else three (gWaterAnimTexIds) */
    u8 pad25;
    s16 tex;   /* 0x26 */
    s32 rm0;   /* 0x28: render mode, two-texture case */
    s32 rm1;   /* 0x2C */
    u8 zbuf;   /* 0x30: use G_ZBUFFER, two-texture case */
    u8 unk31;  /* 0x31: draw layer */
    u8 pad32[2];
} Water;

extern Water gWaterLevels[];
extern s16 gWaterAnimTexIds[];
#ifndef NON_MATCHING
extern s32 gHeapPtr; /* bump allocator for display memory */
#endif
extern Vtx * N64P gWaterVtx[2]; /* double-buffered water grid */
extern s32 gWaterCullVtx[2];
extern s32 gWaterClock; /* wave clock */
extern s32 gWaterTextures[3];
extern s32 gWaterDls[2];
extern u8 gWaterTexFrame;
extern s16 gWaterTexBlend;
extern u8 gWaterSingleTex;
extern u8 gWaterIndex; /* index into gWaterLevels */

void water_flat_normal(Vtx *v, s32 i0, s32 i1, s32 i2);
void water_box_corners(Vtx *v, s16 x0, s16 y0, s16 z0, s16 x1, s16 y1, s16 z1);

/* Per-level random quad spawner (0x22 bytes); ranges are passed to util_rand_range (random in range). */
typedef struct {
    u8 id; /* level id */
    u8 pad1;
    s16 x0, z0; /* 0x02 */
    s16 x1, z1; /* 0x06 */
    s16 y0, y1; /* 0x0A */
    u8 count;   /* 0x0E */
    u8 padF;
    s16 r0, r1; /* 0x10 */
    s16 tex;    /* 0x14 */
    u8 tu, tv;  /* 0x16 */
    s16 w0, w1; /* 0x18 */
    s16 d0, d1; /* 0x1C */
    u8 alpha;   /* 0x20 */
    u8 pad21;
} QuadSpawn;

typedef struct {
    s16 unk0;
    s16 x, z;
    s16 w, d; /* half extents */
    s16 angle;
} Quad;

extern QuadSpawn gDriftQuadSpawns[];
extern Quad gDriftQuads[];
extern Vtx gDriftQuadVtx[2][12][4];
extern u8 gDriftQuadSpawn;
extern s32 gDriftQuadCount;
extern s32 gDriftQuadTex;


/* Level spawn table for the proximity objects below (one entry). */
typedef struct {
    u8 id; /* level id */
    u8 pad1;
    s16 x, y, z;
} MineSpawn;

/* A placed proximity object (0x50 bytes). */
typedef struct {
    s16 x, y, z;
    s16 pad6;
    Vtx v[4];  /* 0x08 */
    u8 active; /* 0x48 */
} Mine;

extern MineSpawn gPowerPillSpawns[];
extern Vtx gPowerPillVtx[];
extern Mine gPowerPills[];
extern s32 gPowerPillCount; /* number of placed objects */
extern s32 gPowerPillTex;


/* Levels 0, 4, 15, 16, 20: the bridges sag under the vehicles (0x8027D810). */
void bridge_update_levels(s32 arg0) {
    switch (arg0) {
    case 0:
        bridge_sag_group(1, 7, 0x320000);
        break;
    case 4:
        bridge_sag_group(1, 7, 0xA00000);
        break;
    case 16:
        bridge_sag_group(1, 7, 0x3C0000);
        bridge_sag_group(2, 7, 0x3C0000);
        break;
    case 20:
        bridge_sag_group(1, 7, 0x820000);
        bridge_sag_group(2, 7, 0x820000);
        break;
    case 15:
        bridge_sag_group(1, 7, 0x410000);
        bridge_sag_group(2, 7, 0x410000);
        break;
    }
}

/* A travelling sine on a group (no C caller) (0x8027D8F4). */
void bridge_wave_group(s32 arg0, s32 arg1, s32 arg2) {
    s32 i;
    s32 j;
    f32 step;
    f32 s;
    f32 angle;

    j = 1;
    step = 6.28318 / (arg1 + 1);
    angle = gBridgeWaveClock / 20.0;
    for (i = gBridgeWaveClock; i < arg1 + gBridgeWaveClock; i++) {
        s = sinf(angle);
        lvl_group_move_part(arg0, j++, 0, -s * arg2, 0);
        angle += step;
    }
    gBridgeWaveClock++;
}

/* Half-sine sag of a group's parts by load (0x8027DA10). */
void bridge_sag_group(s32 arg0, s32 arg1, s32 arg2) {
    f32 r;
    f32 s;
    f32 step;
    s32 i;
    s16 *p;
    s32 pad[5];
    s32 x[4];
    s32 z[4];

    p = lvl_group_get(arg0);
    for (i = 0; i < 4; i++) {
        x[i] = p[i * 2] << 5;
        z[i] = p[i * 2 + 1] << 5;
    }
    r = bridge_max_load(x, z, arg2);
    step = 3.14159 / (arg1 + 1);
    for (i = 0; i < arg1; i++) {
        s = sinf((i + 1) * step);
        lvl_group_move_part(arg0, i + 1, 0, -s * r, 0);
    }
}

/* The heaviest load on a span (0x8027DB5C). */
f32 bridge_max_load(s32 *a, s32 *b, s32 arg2) {
    s32 i;
    f32 max;
    f32 v;
    f32 w;
    f32 t;
    s32 type;

    i = 0;
    max = 0.0f;
    while (&gVehicles[i] != gVehiclesEnd) {
        if ((type = gVehicles[i].type) != VEH_CHOPPER && (type != VEH_DRIVER || gDriverHidden == 0)) {
            if (bridge_point_in_quad(gVehicles[i].unk64, gVehicles[i].unk6C, a, b) != 0) {
                if (gVehicles[i].unk70 != 0) {
                    t = bridge_cross_fraction(gVehicles[i].unk64, gVehicles[i].unk6C, a, b);
                    if (t <= 0.5) {
                        t = t * 2.0;
                    } else {
                        t = (1.0 - t) * 2.0;
                    }
                    w = bridge_digger_weight(gVehicles[i].type);
                    v = t * w;
                    if (max < v) {
                        max = v;
                    }
                }
            }
        }
        i++;
    }
    return arg2 * max;
}

/* Where a vehicle crosses the span (0..1) (0x8027DD88).
 * Where segment p[0]..p[1] (x) / q[0]..q[1] (y) crosses the segment centred on (arg0, arg1)
 * with half-extent (p[1] - p[2], q[1] - q[2]); returns the crossing's fraction along the
 * first segment. Line-intersection maths as in Graphics Gems II (lines_intersect).
 * The a1/b1/c1 line must stay a single source line: IDO schedules it differently otherwise. */
f32 bridge_cross_fraction(s32 arg0, s32 arg1, s32 *p, s32 *q) {
    f32 a1, a2, b1, b2, c1, c2;
    f32 r1, r2, r3, r4;
    f32 denom, offset, num;
    f32 pos;
    f32 t;
    f32 x1, y1, x2, y2, x3, y3, x4, y4;

    x1 = p[0];
    y1 = q[0];
    x2 = p[1];
    y2 = q[1];
    x3 = p[1] - p[2] + arg0;
    y3 = q[1] - q[2] + arg1;
    x4 = arg0 - (p[1] - p[2]);
    y4 = arg1 - (q[1] - q[2]);

    a1 = y2 - y1; b1 = x1 - x2; c1 = x2 * y1 - x1 * y2;
    r3 = a1 * x3 + b1 * y3 + c1;
    r4 = a1 * x4 + b1 * y4 + c1;
    a2 = y4 - y3;
    b2 = x3 - x4;
    c2 = x4 * y3 - x3 * y4;
    r1 = a2 * x1 + b2 * y1 + c2;
    r2 = a2 * x2 + b2 * y2 + c2;
    denom = a1 * b2 - a2 * b1;
    if (denom < 0.0f) {
        offset = -denom / 2.0f;
    } else {
        offset = denom / 2.0f;
    }
    if (ABS(x2 - x1) > ABS(y2 - y1)) {
        num = b1 * c2 - b2 * c1;
        pos = ((num < 0.0f) ? num - offset : num + offset) / denom;
        t = (pos - x1) / (x2 - x1);
    } else {
        num = a2 * c1 - a1 * c2;
        pos = ((num < 0.0f) ? num - offset : num + offset) / denom;
        t = (pos - y1) / (y2 - y1);
    }
    return t;
}


/* 1 if (x, z) is inside the quad (0x8027E164). */
s32 bridge_point_in_quad(s32 arg0, s32 arg1, void *arg2, void *arg3) {
    if (ground_in_triangle(arg0, arg1, *(s32 *)((u8 *) arg2 + 0x0), *(s32 *)((u8 *) arg3 + 0x0),
                       *(s32 *)((u8 *) arg2 + 0x4), *(s32 *)((u8 *) arg3 + 0x4),
                       *(s32 *)((u8 *) arg2 + 0x8), *(s32 *)((u8 *) arg3 + 0x8)) != 0) {
        return 1;
    }
    if (ground_in_triangle(arg0, arg1, *(s32 *)((u8 *) arg2 + 0x0), *(s32 *)((u8 *) arg3 + 0x0),
                       *(s32 *)((u8 *) arg2 + 0x8), *(s32 *)((u8 *) arg3 + 0x8),
                       *(s32 *)((u8 *) arg2 + 0xC), *(s32 *)((u8 *) arg3 + 0xC)) != 0) {
        return 1;
    }
    return 0;
}

/* A vehicle type's weight on the bridges ("DIGGER WEIGHT NOT SET") (0x8027E228). */
#ifdef NON_MATCHING
f32 bridge_digger_weight(u8 type)
#else
f32 bridge_digger_weight(type)
    u8 type;
#endif
{
    switch (type) {
    case VEH_DRIVER:
        return 0.3f;
    case VEH_SIDESWIPE:
        return 0.8f;
    case VEH_BACKLASH:
        return 0.8f;
    case VEH_RAMDOZER:
        return 0.8f;
    case VEH_THUNDERFIST:
        return 0.8f;
    case VEH_CYCLONE:
        return 0.4f;
    case VEH_SKYFALL:
        return 0.8f;
    case VEH_AMDREAM:
        return 0.6f;
    case VEH_BALLISTA:
        return 0.6f;
    case VEH_POLICE:
        return 0.8f;
    case VEH_ATEAM:
        return 0.6f;
    case VEH_STARSKI:
        return 0.6f;
    case VEH_JBOMB:
        return 0.0f;
    case VEH_CARRIER:
        return 0.8f;
    default:
        debug_printf("DIGGER WEIGHT NOT SET\n");
    }
#ifdef NON_MATCHING
    /* The original falls off the end here: f0 is whatever the caller left
     * in it (the debug printf is empty). No digger type takes this path. */
    return 0.0f;
#endif
}

/* The level's water (0x8027E344).
 * Set up the water surface for level id: allocate its vertex/display buffers, load its textures,
 * and lay out the grid's x/z positions and texture coordinates. */
void water_init(s32 id) {
    s32 stepX;
    s32 stepZ;
    s32 x;
    s32 z;
    s32 i;
    s32 j;
    s32 n;
    s32 tv;
    s32 tu;
    s32 size;
    u8 found;

    n = 0;
    tv = 0;
    tu = 0;
    found = 0;
    gWaterIndex = 0;
    do {
        if (gWaterLevels[gWaterIndex].id == id) {
            found = 1;
        } else {
            gWaterIndex++;
        }
    } while (!found && gWaterIndex < 3);
    if (!found) {
        gWaterOn = 0;
        return;
    }
    gWaterOn = 1;
    gWaterVtx[0] = (Vtx *) N64_IPTR(gHeapPtr);
    gHeapPtr += (gWaterLevels[gWaterIndex].nx + 1) * (gWaterLevels[gWaterIndex].nz + 1) * sizeof(Vtx);
    gWaterVtx[1] = (Vtx *) N64_IPTR(gHeapPtr);
    gHeapPtr += (gWaterLevels[gWaterIndex].nx + 1) * (gWaterLevels[gWaterIndex].nz + 1) * sizeof(Vtx);
#ifdef PORT_HOST
    {
        /* every vertex gets x, z, s, t here and y every frame; the flag and
         * colour bytes stay whatever the heap held on the N64 (the RSP shades
         * the water with them): give the native records those bytes */
        void port_vtx_stale(void *v, u32 n);
        u32 nv = (gWaterLevels[gWaterIndex].nx + 1) * (gWaterLevels[gWaterIndex].nz + 1);
        port_vtx_stale(gWaterVtx[0], nv);
        port_vtx_stale(gWaterVtx[1], nv);
    }
#endif
    gWaterCullVtx[0] = gHeapPtr;
    gHeapPtr += 0x12C0;
    gWaterCullVtx[1] = gHeapPtr;
    gHeapPtr += 0x12C0;
    size = gWaterLevels[gWaterIndex].nx * gWaterLevels[gWaterIndex].nz * 2 * 8;
    size += (gWaterLevels[gWaterIndex].nx / 8 + 1) * gWaterLevels[gWaterIndex].nz * 8;
    size += 0x1C20;
    gWaterDls[0] = gHeapPtr;
    gHeapPtr += size;
    gWaterDls[1] = gHeapPtr;
    gHeapPtr += size;
    gWaterLayer = gWaterLevels[gWaterIndex].unk31;
    if ((gWaterSingleTex = gWaterLevels[gWaterIndex].unk24) != 0) {
        gWaterTextures[0] = gHeapPtr;
        tex_get(gWaterLevels[gWaterIndex].tex, 0);
    } else {
        for (i = 0; i < 3; i++) {
            gWaterTextures[i] = gHeapPtr;
            tex_get(gWaterAnimTexIds[i], 0);
        }
    }
    gWaterTexFrame = 0;
    gWaterTexBlend = 0;
    stepX = (gWaterLevels[gWaterIndex].x1 - gWaterLevels[gWaterIndex].x0) / gWaterLevels[gWaterIndex].nx;
    stepZ = (gWaterLevels[gWaterIndex].z1 - gWaterLevels[gWaterIndex].z0) / gWaterLevels[gWaterIndex].nz;
    x = gWaterLevels[gWaterIndex].x0;
    z = gWaterLevels[gWaterIndex].z0;
    for (i = 0; i <= gWaterLevels[gWaterIndex].nz; i++) {
        tu = 0;
        for (j = 0; j <= gWaterLevels[gWaterIndex].nx; j++) {
            gWaterVtx[0][n].v.ob[0] = x;
            gWaterVtx[0][n].v.ob[2] = z;
            gWaterVtx[1][n].v.ob[0] = x;
            gWaterVtx[1][n].v.ob[2] = z;
            gWaterVtx[0][n].v.tc[0] = tv << 5;
            gWaterVtx[0][n].v.tc[1] = tu << 5;
            gWaterVtx[1][n].v.tc[0] = tv << 5;
            gWaterVtx[1][n].v.tc[1] = tu << 5;
            tu ^= 0x1F;
            n++;
            x += stepX;
        }
        tv ^= 0x1F;
        x = gWaterLevels[gWaterIndex].x0;
        z += stepZ;
    }
    gWaterClock = 0;
}

/* Wave clock and vertex heights (0x8027E9B8).
 * Per-frame water update: advance the wave clock, then rebuild the heights of vertex buffer buf. */
void water_update(u8 buf) {
    s32 i;
    s32 j;
    s32 stepX;
    s32 stepZ;
    register f32 s;

    if (gWaterOn != 0 && gPaused == 0) {
        gWaterClock++;
        gWaterTexBlend += 15;
        if (gWaterTexBlend >= 255) {
            gWaterTexBlend = 0;
            if (++gWaterTexFrame >= 3) {
                gWaterTexFrame = 0;
            }
        }
    }
    if (gWaterOn != 0) {
        stepX = (gWaterLevels[gWaterIndex].x1 - gWaterLevels[gWaterIndex].x0) / gWaterLevels[gWaterIndex].nx;
        for (i = 0; i <= gWaterLevels[gWaterIndex].nx; i++) {
            for (j = 0; j <= gWaterLevels[gWaterIndex].nz; j++) {
                s = sinf(((i + 1) * stepX) / gWaterLevels[gWaterIndex].lenX + gWaterClock / gWaterLevels[gWaterIndex].rateX);
                gWaterVtx[buf][i + j * (gWaterLevels[gWaterIndex].nz + 1)].v.ob[1] =
                    gWaterLevels[gWaterIndex].ampX * s + gWaterLevels[gWaterIndex].y;
            }
        }
        stepZ = (gWaterLevels[gWaterIndex].z1 - gWaterLevels[gWaterIndex].z0) / gWaterLevels[gWaterIndex].nz;
        for (i = 0; i <= gWaterLevels[gWaterIndex].nz; i++) {
            for (j = 0; j <= gWaterLevels[gWaterIndex].nx; j++) {
                s = sinf(((i + 1) * stepZ) / gWaterLevels[gWaterIndex].lenZ + gWaterClock / gWaterLevels[gWaterIndex].rateZ);
                gWaterVtx[buf][(gWaterLevels[gWaterIndex].nz + 1) * i + j].v.ob[1] =
                    gWaterLevels[gWaterIndex].ampZ * s + gWaterVtx[buf][(gWaterLevels[gWaterIndex].nz + 1) * i + j].v.ob[1];
            }
        }
    }
}

/* The water height at a point (0x8027EED8).
 * Water height at (x, z) into *y; returns 1 if (x, z) is over the water grid. */
s32 water_get_height(s16 x, s16 z, s16 *y) {
    s32 pad;
    f32 step;
    f32 t;
    register f32 s;

    if (gWaterOn == 0) {
        return 0;
    }
    if (x < gWaterLevels[gWaterIndex].x0 || x > gWaterLevels[gWaterIndex].x1 || z < gWaterLevels[gWaterIndex].z0 ||
        z > gWaterLevels[gWaterIndex].z1) {
        *y = gWaterLevels[gWaterIndex].y;
        return 0;
    }
    step = (f32) (gWaterLevels[gWaterIndex].x1 - gWaterLevels[gWaterIndex].x0) / gWaterLevels[gWaterIndex].nx;
    t = (x - gWaterLevels[gWaterIndex].x0) / step;
    s = sinf(((t + 1.0f) * step) / gWaterLevels[gWaterIndex].lenX + gWaterClock / gWaterLevels[gWaterIndex].rateX);
    *y = gWaterLevels[gWaterIndex].ampX * s + gWaterLevels[gWaterIndex].y;
    step = (f32) (gWaterLevels[gWaterIndex].z1 - gWaterLevels[gWaterIndex].z0) / gWaterLevels[gWaterIndex].nz;
    t = (z - gWaterLevels[gWaterIndex].z0) / step;
    s = sinf(((t + 1.0f) * step) / gWaterLevels[gWaterIndex].lenZ + gWaterClock / gWaterLevels[gWaterIndex].rateZ);
    *y = gWaterLevels[gWaterIndex].ampZ * s + *y;
    return 1;
}

/* Draws the water (0x8027F1F8).
 * Draw the water surface into gfx when this is its layer. The grid is emitted in strips of up to
 * 8x2 vertices into a sub-display-list (gWaterDls[buf]); every 32 vertices the strip batch is
 * closed and given a bounding box (8 verts in gWaterCullVtx[buf]) for gSPCullDisplayList. With a
 * single texture the face normals are recomputed for environment mapping, otherwise two textures
 * are blended with LOD fraction gWaterTexBlend. */
void water_draw(Gfx **gfx, u8 buf, u8 layer) {
    Gfx *gdl;
    s32 a;
    s32 b;
    s32 pad; /* unused */
    s32 total;
    s32 cnt;
    s32 col;
    s32 k;
    u8 done;
    s32 m;
    s32 rowlen;
    u8 next;
    s16 minX;
    s16 maxX;
    s16 minY;
    s16 maxY;
    s16 minZ;
    s16 maxZ;
    Gfx *dl;
    Vtx *bbox;
    s32 vcount;

    gdl = *gfx;
    done = 0;
    dl = (Gfx *) N64_IPTR(gWaterDls[buf]);
    bbox = (Vtx *) N64_IPTR(gWaterCullVtx[buf]);
    if (gWaterOn == 0 || layer != gWaterLayer) {
        return;
    }
    gDPPipeSync(gdl++);
    if (gWaterSingleTex != 0) {
        gDPSetCycleType(gdl++, G_CYC_1CYCLE);
        gDPSetRenderMode(gdl++, G_RM_ZB_XLU_SURF, G_RM_ZB_XLU_SURF2);
        gSPClearGeometryMode(gdl++, 0xFFFFFFFF);
        gSPSetGeometryMode(gdl++, G_ZBUFFER | G_SHADE | G_SHADING_SMOOTH | G_CULL_BACK | G_LIGHTING | G_TEXTURE_GEN);
        gSPTexture(gdl++, 0x07C0, 0x07C0, 0, G_TX_RENDERTILE, G_ON);
        gDPSetCombine(gdl++, 0xFFFFFF, 0xFFFCF67B);
        gDPSetPrimColor(gdl++, 0, 0, 0x00, 0x00, 0x00, 0xAA);
        gDPLoadTextureBlock(gdl++, OS_K0_TO_PHYSICAL(gWaterTextures[0]), G_IM_FMT_RGBA, G_IM_SIZ_16b, 32, 32, 0,
                            G_TX_CLAMP, G_TX_CLAMP, G_TX_NOMASK, G_TX_NOMASK, G_TX_NOLOD, G_TX_NOLOD);
    } else {
        gDPSetCycleType(gdl++, G_CYC_2CYCLE);
        gDPSetRenderMode(gdl++, gWaterLevels[gWaterIndex].rm0, gWaterLevels[gWaterIndex].rm1);
        gSPClearGeometryMode(gdl++, 0xFFFFFFFF);
        if (gWaterLevels[gWaterIndex].zbuf != 0) {
            gSPSetGeometryMode(gdl++, G_ZBUFFER | G_SHADE | G_SHADING_SMOOTH | G_CULL_BACK);
        } else {
            gSPSetGeometryMode(gdl++, G_SHADE | G_SHADING_SMOOTH | G_CULL_BACK);
        }
        gSPTexture(gdl++, 0xFFFF, 0xFFFF, 0, G_TX_RENDERTILE, G_ON);
        gDPSetCombine(gdl++, 0x277FFF, 0x1FFCFE3B);
        gDPSetTextureLOD(gdl++, G_TL_TILE);
        if (gWaterTexFrame == 2) {
            next = 0;
        } else {
            next = gWaterTexFrame + 1;
        }
        gDPSetTextureImage(gdl++, G_IM_FMT_RGBA, G_IM_SIZ_16b, 1, OS_K0_TO_PHYSICAL(gWaterTextures[gWaterTexFrame]));
        gDPSetTile(gdl++, G_IM_FMT_RGBA, G_IM_SIZ_16b, 0, 0x000, G_TX_LOADTILE, 0, 0, 0, 0, 0, 0, 0);
        gDPLoadSync(gdl++);
        gDPLoadBlock(gdl++, G_TX_LOADTILE, 0, 0, 1023, 256);
        gDPSetTextureImage(gdl++, G_IM_FMT_RGBA, G_IM_SIZ_16b, 1, OS_K0_TO_PHYSICAL(gWaterTextures[next]));
        gDPTileSync(gdl++);
        gDPSetTile(gdl++, G_IM_FMT_RGBA, G_IM_SIZ_16b, 0, 0x100, G_TX_LOADTILE, 0, 0, 0, 0, 0, 0, 0);
        gDPLoadSync(gdl++);
        gDPLoadBlock(gdl++, G_TX_LOADTILE, 0, 0, 1023, 256);
        gDPSetTile(gdl++, G_IM_FMT_RGBA, G_IM_SIZ_16b, 8, 0x000, G_TX_RENDERTILE, 0, G_TX_CLAMP, 5, G_TX_NOLOD,
                   G_TX_CLAMP, 5, G_TX_NOLOD);
        gDPSetTileSize(gdl++, G_TX_RENDERTILE, 2, 2, 0x7E, 0x7E);
        gDPSetTile(gdl++, G_IM_FMT_RGBA, G_IM_SIZ_16b, 8, 0x100, 1, 0, G_TX_CLAMP, 5, G_TX_NOLOD, G_TX_CLAMP, 5,
                   G_TX_NOLOD);
        gDPSetTileSize(gdl++, 1, 2, 2, 0x7E, 0x7E);
        gDPSetPrimColor(gdl++, 0, gWaterTexBlend, 0x00, 0x00, 0x00, 0xAA);
    }
    total = (gWaterLevels[gWaterIndex].nx + 1) * (gWaterLevels[gWaterIndex].nz + 1);
    a = 0;
    b = gWaterLevels[gWaterIndex].nx + 1;
    col = 0;
    vcount = 0;
    while (!done) {
        if (gWaterLevels[gWaterIndex].nx - col + 1 >= 9) {
            cnt = 8;
        } else {
            cnt = gWaterLevels[gWaterIndex].nx - col + 1;
        }
        if (vcount == 0) {
            gSPDisplayList(gdl++, osVirtualToPhysical(dl));
            gSPVertex(dl++, osVirtualToPhysical(bbox), 8, 0);
            gSPCullDisplayList(dl++, 0, 7);
            minZ = minY = minX = 0x7FFF;
            maxZ = maxY = maxX = -0x8000;
        }
        gSPVertex(dl++, osVirtualToPhysical(&gWaterVtx[buf][a]), cnt, 0);
        gSPVertex(dl++, osVirtualToPhysical(&gWaterVtx[buf][b]), cnt, 8);
        for (m = 0; m < cnt; m++) {
            if (gWaterVtx[buf][a + m].v.ob[0] < minX) {
                minX = gWaterVtx[buf][a + m].v.ob[0];
            }
            if (gWaterVtx[buf][a + m].v.ob[1] < minY) {
                minY = gWaterVtx[buf][a + m].v.ob[1];
            }
            if (gWaterVtx[buf][a + m].v.ob[2] < minZ) {
                minZ = gWaterVtx[buf][a + m].v.ob[2];
            }
            if (gWaterVtx[buf][a + m].v.ob[0] > maxX) {
                maxX = gWaterVtx[buf][a + m].v.ob[0];
            }
            if (gWaterVtx[buf][a + m].v.ob[1] > maxY) {
                maxY = gWaterVtx[buf][a + m].v.ob[1];
            }
            if (gWaterVtx[buf][a + m].v.ob[2] > maxZ) {
                maxZ = gWaterVtx[buf][a + m].v.ob[2];
            }
        }
        for (m = 0; m < cnt; m++) {
            if (gWaterVtx[buf][b + m].v.ob[0] < minX) {
                minX = gWaterVtx[buf][b + m].v.ob[0];
            }
            if (gWaterVtx[buf][b + m].v.ob[1] < minY) {
                minY = gWaterVtx[buf][b + m].v.ob[1];
            }
            if (gWaterVtx[buf][b + m].v.ob[2] < minZ) {
                minZ = gWaterVtx[buf][b + m].v.ob[2];
            }
            if (gWaterVtx[buf][b + m].v.ob[0] > maxX) {
                maxX = gWaterVtx[buf][b + m].v.ob[0];
            }
            if (gWaterVtx[buf][b + m].v.ob[1] > maxY) {
                maxY = gWaterVtx[buf][b + m].v.ob[1];
            }
            if (gWaterVtx[buf][b + m].v.ob[2] > maxZ) {
                maxZ = gWaterVtx[buf][b + m].v.ob[2];
            }
        }
        k = 0;
        for (m = 0; m < cnt - 1; m++) {
            gSP1Triangle(dl++, k + 1, k, k + 8, 0);
            if (gWaterSingleTex != 0) {
                water_flat_normal(gWaterVtx[buf], a + k, a + k + 1, b + k);
            }
            gSP1Triangle(dl++, k + 8, k + 9, k + 1, 0);
            if (gWaterSingleTex != 0) {
                water_flat_normal(gWaterVtx[buf], b + k, b + k + 1, a + k + 1);
            }
            k++;
        }
        rowlen = cnt - 1;
        col += rowlen;
        a += rowlen, b += rowlen;
        if (gWaterLevels[gWaterIndex].nx == col) {
            col = 0;
            a++, b++;
        }
        vcount += cnt * 2;
        if (vcount >= 32) {
            gSPEndDisplayList(dl++);
            water_box_corners(bbox, minX, minY, minZ, maxX, maxY, maxZ);
            vcount = 0;
            bbox += 8;
        }
        if (b == total) {
            done = 1;
        }
    }
    if (vcount != 0) {
        gSPEndDisplayList(dl++);
        water_box_corners(bbox, minX, minY, minZ, maxX, maxY, maxZ);
        bbox += 8;
    }
    gDPPipeSync(gdl++);
    gDPSetTextureLOD(gdl++, G_TL_LOD);
    gDPPipeSync(gdl++);
    *gfx = gdl;
}

/* The water's flat normal (0x802802D4).
 * Flat-shade triangle (i0, i1, i2) of v: store its face normal, scaled to length 120, in all three vertices. */
void water_flat_normal(Vtx *v, s32 i0, s32 i1, s32 i2) {
    f32 e1[3];
    f32 e2[3];
    f32 pad[3];
    f32 scale;
    f32 nx;
    f32 ny;
    f32 nz;
    f32 len;

    e1[0] = v[i2].v.ob[0] - v[i0].v.ob[0];
    e1[1] = v[i2].v.ob[1] - v[i0].v.ob[1];
    e1[2] = v[i2].v.ob[2] - v[i0].v.ob[2];
    e2[0] = v[i1].v.ob[0] - v[i0].v.ob[0];
    e2[1] = v[i1].v.ob[1] - v[i0].v.ob[1];
    e2[2] = v[i1].v.ob[2] - v[i0].v.ob[2];
    nx = e1[1] * e2[2] - e1[2] * e2[1];
    ny = e1[2] * e2[0] - e1[0] * e2[2];
    nz = e1[0] * e2[1] - e1[1] * e2[0];
    len = sqrtf(nx * nx + ny * ny + nz * nz);
    if (len < 1.0) {
        len = 1.0f;
    }
    scale = 120.0 / len;
    nx *= scale;
    ny *= scale;
    nz *= scale;
    v[i0].v.cn[0] = (s8) nx;
    v[i0].v.cn[1] = (s8) ny;
    v[i0].v.cn[2] = (s8) nz;
    v[i0].v.cn[3] = 0;
    v[i1].v.cn[0] = (s8) nx;
    v[i1].v.cn[1] = (s8) ny;
    v[i1].v.cn[2] = (s8) nz;
    v[i1].v.cn[3] = 0;
    v[i2].v.cn[0] = (s8) nx;
    v[i2].v.cn[1] = (s8) ny;
    v[i2].v.cn[2] = (s8) nz;
    v[i2].v.cn[3] = 0;
}

/* The water's cull box (0x8028072C).
 * Set the 8 corner positions of the box (x0..x1, y0..y1, z0..z1). */
void water_box_corners(Vtx *v, s16 x0, s16 y0, s16 z0, s16 x1, s16 y1, s16 z1) {
#ifdef PORT_HOST
    {
        /* a cull box: only the corners' positions are written (and read, by
         * gSPCullDisplayList); the rest keeps the heap's bytes on the N64 */
        void port_vtx_unread(void *v, u32 n);
        port_vtx_unread(v, 8);
    }
#endif
    v[0].v.ob[0] = x0;
    v[0].v.ob[1] = y0;
    v[0].v.ob[2] = z0;
    v[1].v.ob[0] = x0;
    v[1].v.ob[1] = y1;
    v[1].v.ob[2] = z0;
    v[2].v.ob[0] = x1;
    v[2].v.ob[1] = y0;
    v[2].v.ob[2] = z0;
    v[3].v.ob[0] = x1;
    v[3].v.ob[1] = y1;
    v[3].v.ob[2] = z0;
    v[4].v.ob[0] = x0;
    v[4].v.ob[1] = y0;
    v[4].v.ob[2] = z1;
    v[5].v.ob[0] = x0;
    v[5].v.ob[1] = y1;
    v[5].v.ob[2] = z1;
    v[6].v.ob[0] = x1;
    v[6].v.ob[1] = y0;
    v[6].v.ob[2] = z1;
    v[7].v.ob[0] = x1;
    v[7].v.ob[1] = y1;
    v[7].v.ob[2] = z1;
}

/* Random drifting textured quads per level (0x802807D8).
 * Scatter the level's random textured quads (gDriftQuadSpawns[n].count of them, up to 12): random position,
 * size and angle within the table's ranges, written into both vertex buffers. */
void fx_init_drift_quads(u8 id) {
    s32 i;
    s32 j;
    s16 x;
    s16 y;
    s16 z;
    s16 w;
    s16 d;
    s32 unused; /* set to 0, never read */
    u8 found;

    gDriftQuadCount = 0;
    unused = 0;
    found = 0;
    gDriftQuadSpawn = 0;
    do {
        if (gDriftQuadSpawns[gDriftQuadSpawn].id == id) {
            found = 1;
        } else {
            gDriftQuadSpawn++;
        }
    } while (!found && gDriftQuadSpawn < 4);
    if (found) {
        gDriftQuadCount = gDriftQuadSpawns[gDriftQuadSpawn].count;
        gDriftQuadTex = gHeapPtr;
        tex_get(gDriftQuadSpawns[gDriftQuadSpawn].tex, 0);
        for (i = 0; i < gDriftQuadCount; i++) {
            x = util_rand_range(gDriftQuadSpawns[gDriftQuadSpawn].x0, gDriftQuadSpawns[gDriftQuadSpawn].x1);
            y = util_rand_range(gDriftQuadSpawns[gDriftQuadSpawn].y0, gDriftQuadSpawns[gDriftQuadSpawn].y1);
            z = util_rand_range(gDriftQuadSpawns[gDriftQuadSpawn].z0, gDriftQuadSpawns[gDriftQuadSpawn].z1);
            w = util_rand_range(gDriftQuadSpawns[gDriftQuadSpawn].w0, gDriftQuadSpawns[gDriftQuadSpawn].w1);
            d = util_rand_range(gDriftQuadSpawns[gDriftQuadSpawn].d0, gDriftQuadSpawns[gDriftQuadSpawn].d1);
            w >>= 1, d >>= 1;
            gDriftQuads[i].w = w;
            gDriftQuads[i].d = d;
            gDriftQuads[i].x = x;
            gDriftQuads[i].z = z;
            gDriftQuads[i].unk0 = util_rand_range(gDriftQuadSpawns[gDriftQuadSpawn].r0, gDriftQuadSpawns[gDriftQuadSpawn].r1);
            gDriftQuads[i].angle = util_rand_range(0, 0xFFF);
            for (j = 0; j < 2; j++) {
                gDriftQuadVtx[j][i][0].v.ob[0] = x - w;
                gDriftQuadVtx[j][i][0].v.ob[1] = y;
                gDriftQuadVtx[j][i][0].v.ob[2] = z - d;
                gDriftQuadVtx[j][i][0].v.tc[0] = 0;
                gDriftQuadVtx[j][i][0].v.tc[1] = 0;
                gDriftQuadVtx[j][i][1].v.ob[0] = x + w;
                gDriftQuadVtx[j][i][1].v.ob[1] = y;
                gDriftQuadVtx[j][i][1].v.ob[2] = z - d;
                gDriftQuadVtx[j][i][1].v.tc[0] = gDriftQuadSpawns[gDriftQuadSpawn].tu << 5;
                gDriftQuadVtx[j][i][1].v.tc[1] = 0;
                gDriftQuadVtx[j][i][2].v.ob[0] = x + w;
                gDriftQuadVtx[j][i][2].v.ob[1] = y;
                gDriftQuadVtx[j][i][2].v.ob[2] = z + d;
                gDriftQuadVtx[j][i][2].v.tc[0] = gDriftQuadSpawns[gDriftQuadSpawn].tu << 5;
                gDriftQuadVtx[j][i][2].v.tc[1] = gDriftQuadSpawns[gDriftQuadSpawn].tv << 5;
                gDriftQuadVtx[j][i][3].v.ob[0] = x - w;
                gDriftQuadVtx[j][i][3].v.ob[1] = y;
                gDriftQuadVtx[j][i][3].v.ob[2] = z + d;
                gDriftQuadVtx[j][i][3].v.tc[0] = 0;
                gDriftQuadVtx[j][i][3].v.tc[1] = gDriftQuadSpawns[gDriftQuadSpawn].tv << 5;
            }
        }
    }
}

/* Moves, wraps and draws the drifting quads (0x80280F34).
 * Move the random quads (coll_step_xz steps each one; the result comes back in gCollStepX/Z),
 * wrap them around the spawn area, rebuild their corners in vertex buffer buf and draw them. */
void fx_draw_drift_quads(Gfx **gfx, u8 buf) {
    Gfx *gdl;
    s32 i;
    s16 dx;
    s16 dz;

    gdl = *gfx;
    if (gDriftQuadCount != 0) {
        gDPPipeSync(gdl++);
        gDPSetCycleType(gdl++, G_CYC_1CYCLE);
        gDPSetRenderMode(gdl++, 0x00504340, 0);
        gSPClearGeometryMode(gdl++, 0xFFFFFFFF);
        gSPSetGeometryMode(gdl++, G_SHADE | G_SHADING_SMOOTH);
        gSPTexture(gdl++, 0xFFFF, 0xFFFF, 0, G_TX_RENDERTILE, G_ON);
        gDPSetCombineMode(gdl++, G_CC_MODULATERGBA_PRIM, G_CC_MODULATERGBA_PRIM);
        gDPSetPrimColor(gdl++, 0, 0, 0xFF, 0xFF, 0xFF, gDriftQuadSpawns[gDriftQuadSpawn].alpha);
        gDPLoadTextureBlock(gdl++, OS_K0_TO_PHYSICAL(gDriftQuadTex), G_IM_FMT_IA, G_IM_SIZ_16b,
                            gDriftQuadSpawns[gDriftQuadSpawn].tu, gDriftQuadSpawns[gDriftQuadSpawn].tv, 0, G_TX_CLAMP, G_TX_CLAMP,
                            G_TX_NOMASK, G_TX_NOMASK, G_TX_NOLOD, G_TX_NOLOD);
        for (i = 0; i < gDriftQuadCount; i++) {
            coll_step_xz(gDriftQuads[i].x << 5, gDriftQuads[i].z << 5, gDriftQuads[i].unk0, gDriftQuads[i].angle);
            dx = (gCollStepX >> 5) - gDriftQuads[i].x;
            dz = (gCollStepZ >> 5) - gDriftQuads[i].z;
            gDriftQuads[i].x = gCollStepX >> 5;
            gDriftQuads[i].z = gCollStepZ >> 5;
            /* note: the wrap amounts index the spawn table by i, not gDriftQuadSpawn */
            if (gDriftQuads[i].x > gDriftQuadSpawns[gDriftQuadSpawn].x1) {
                gDriftQuads[i].x = gDriftQuadSpawns[gDriftQuadSpawn].x0;
                dx -= gDriftQuadSpawns[i].x1 - gDriftQuadSpawns[i].x0;
            }
            if (gDriftQuads[i].x < gDriftQuadSpawns[gDriftQuadSpawn].x0) {
                gDriftQuads[i].x = gDriftQuadSpawns[gDriftQuadSpawn].x1;
                dx += gDriftQuadSpawns[i].x1 - gDriftQuadSpawns[i].x0;
            }
            if (gDriftQuads[i].z > gDriftQuadSpawns[gDriftQuadSpawn].z1) {
                gDriftQuads[i].z = gDriftQuadSpawns[gDriftQuadSpawn].z0;
                dz -= gDriftQuadSpawns[i].z1 - gDriftQuadSpawns[i].z0;
            }
            if (gDriftQuads[i].z < gDriftQuadSpawns[gDriftQuadSpawn].z0) {
                gDriftQuads[i].z = gDriftQuadSpawns[gDriftQuadSpawn].z1;
                dz += gDriftQuadSpawns[i].z1 - gDriftQuadSpawns[i].z0;
            }
            gDriftQuadVtx[buf][i][0].v.ob[0] = gDriftQuads[i].x - gDriftQuads[i].w;
            gDriftQuadVtx[buf][i][0].v.ob[2] = gDriftQuads[i].z - gDriftQuads[i].d;
            gDriftQuadVtx[buf][i][1].v.ob[0] = gDriftQuads[i].x + gDriftQuads[i].w;
            gDriftQuadVtx[buf][i][1].v.ob[2] = gDriftQuads[i].z - gDriftQuads[i].d;
            gDriftQuadVtx[buf][i][2].v.ob[0] = gDriftQuads[i].x + gDriftQuads[i].w;
            gDriftQuadVtx[buf][i][2].v.ob[2] = gDriftQuads[i].z + gDriftQuads[i].d;
            gDriftQuadVtx[buf][i][3].v.ob[0] = gDriftQuads[i].x - gDriftQuads[i].w;
            gDriftQuadVtx[buf][i][3].v.ob[2] = gDriftQuads[i].z + gDriftQuads[i].d;
            gSPVertex(gdl++, OS_K0_TO_PHYSICAL(gDriftQuadVtx[buf][i]), 4, 0);
            gSP1Triangle(gdl++, 0, 1, 2, 0);
            gSP1Triangle(gdl++, 0, 2, 3, 0);
        }
        gDPPipeSync(gdl++);
    }
    *gfx = gdl;
}

/* Places the level's power pill (0x80281A70). */
void fx_init_power_pill(s32 arg0) {
    s32 i;
    s32 j;

    gPowerPillCount = 0;
    for (i = 0; i < 1; i++) {
        if (gPowerPillSpawns[i].id == arg0) {
            gPowerPills[gPowerPillCount].x = gPowerPillSpawns[i].x;
            gPowerPills[gPowerPillCount].y = gPowerPillSpawns[i].y;
            gPowerPills[gPowerPillCount].z = gPowerPillSpawns[i].z;
            util_copy64((u64 *) (gPowerPills[gPowerPillCount].v), (u64 *) gPowerPillVtx, sizeof(gPowerPills->v));
            for (j = 0; j < 4; j++) {
                gPowerPills[gPowerPillCount].v[j].v.ob[0] += gPowerPills[gPowerPillCount].x;
                gPowerPills[gPowerPillCount].v[j].v.ob[1] += gPowerPills[gPowerPillCount].y;
                gPowerPills[gPowerPillCount].v[j].v.ob[2] += gPowerPills[gPowerPillCount].z;
            }
            gPowerPills[gPowerPillCount].active = 1;
            gPowerPillCount++;
        }
    }
    if (gPowerPillCount != 0) {
        gPowerPillTex = tex_get(0x546, 0);
    }
    gPowerPillTimer = 0;
    gPowerPillEaten = 0;
}

/* Eaten within 40: the 400-frame pill timer, sounds (0x80281CE4). */
void fx_update_power_pill(void) {
    s32 i;
    s32 dist;

    gPowerPillEaten = 0;
    if (gPowerPillTimer != 0) {
        gPowerPillTimer--;
    }
    for (i = 0; i < gPowerPillCount; i++) {
        if (gPowerPills[i].active != 0) {
            dist = util_dist3d(gPlayerX >> 5, gPlayerY >> 5, gPlayerZ >> 5,
                                 gPowerPills[i].x, gPowerPills[i].y, gPowerPills[i].z);
            if (dist < 40) {
                gPowerPillTimer = 400;
                gPowerPillEaten = 1;
                gPowerPills[i].active = 0;
                snd_play(gSfxBank, SFX_POWER_PILL, NULL);
                if (gPowerPillSound == 0) {
                    snd_play(gSfxBank, SFX_POWER_PILL_LOOP, &gPowerPillSound);
                }
            }
        }
    }
}

/* Draws the uneaten power pills (0x80281E44). */
void fx_draw_power_pill(Gfx **gfx) {
    Gfx *gdl;
    s32 i;

    gdl = *gfx;
    if (gPowerPillCount != 0) {
        gSPClearGeometryMode(gdl++, 0xFFFFFFFF);
        gSPSetGeometryMode(gdl++, G_ZBUFFER | G_SHADE | G_SHADING_SMOOTH);
        gDPPipeSync(gdl++);
        gDPSetCycleType(gdl++, G_CYC_1CYCLE);
        gDPSetRenderMode(gdl++, G_RM_ZB_XLU_SURF, G_RM_ZB_XLU_SURF2);
        gDPSetCombineMode(gdl++, G_CC_MODULATERGBA, G_CC_MODULATERGBA);
        gSPTexture(gdl++, 0xFFFF, 0xFFFF, 0, G_TX_RENDERTILE, G_ON);
        gDPLoadTextureBlock(gdl++, gPowerPillTex, G_IM_FMT_RGBA, G_IM_SIZ_16b, 32, 32, 0,
                            G_TX_CLAMP, G_TX_CLAMP, G_TX_NOMASK, G_TX_NOMASK, G_TX_NOLOD, G_TX_NOLOD);
        for (i = 0; i < gPowerPillCount; i++) {
            if (gPowerPills[i].active != 0) {
                gSPVertex(gdl++, gPowerPills[i].v, 4, 0);
                gSP1Triangle(gdl++, 0, 1, 2, 0);
                gSP1Triangle(gdl++, 0, 2, 3, 0);
            }
        }
        gDPPipeSync(gdl++);
    }
    *gfx = gdl;
}


extern s32 gVehOverlayTex; /* overlay texture */
extern s16 gVehOverlayAlpha; /* overlay alpha */
extern u8 gVehOverlayOn;  /* overlay on */
extern Vtx gVehOverlayVtx[];


/* Loads the vehicle overlay texture (0x802821D0). */
void hud_init_veh_overlay(void) {
    gVehOverlayTex = gHeapPtr;
    tex_load_to_heap(0xA98, 0);
    gHeapPtr += 0x800;
    gVehOverlayAlpha = 0;
    gVehOverlayOn = 0;
}

/* The overlay for the train and the barges (0x80282224).
 * Fade a textured quad (gVehOverlayVtx) in when arg1 is 7, 11, 17 or 18 and veh_tris_all_solid(arg1) is 0;
 * otherwise fade it out by 10 per frame. */
void hud_draw_veh_overlay(Gfx **gfx, u8 arg1) {
    Gfx *gdl;
    u8 flag;
    u8 flag2;

    gdl = *gfx;
    flag = arg1 == 7 || arg1 == 11 || arg1 == 17 || arg1 == 18;
    if (gVehOverlayOn != 0 && !flag) {
        if ((gVehOverlayAlpha -= 10) < 0) {
            gVehOverlayAlpha = 0;
        }
    } else {
        if (flag) {
            flag2 = !veh_tris_all_solid(arg1);
        }
        if (flag && flag2) {
            gVehOverlayAlpha = 0xFF;
            gVehOverlayOn = 1;
        } else {
            gVehOverlayAlpha = 0;
        }
    }
    if (gVehOverlayAlpha == 0) {
        gVehOverlayOn = 0;
    }
    if (gVehOverlayOn != 0) {
        /* widescreen HUD sides (port.h): by where the overlay's quad is */
        PORT_WTAG(gdl, PORT_WTAG_SPAN, gVehOverlayVtx[0].v.ob[0], gVehOverlayVtx[2].v.ob[0]);
        gSPMatrix(gdl++, &seg2Base[3], G_MTX_PROJECTION | G_MTX_LOAD | G_MTX_NOPUSH);
        gSPMatrix(gdl++, &seg2Base[7], G_MTX_MODELVIEW | G_MTX_LOAD | G_MTX_NOPUSH);
        gDPPipeSync(gdl++);
        gDPSetCycleType(gdl++, G_CYC_1CYCLE);
        gDPSetRenderMode(gdl++, G_RM_XLU_SURF, G_RM_XLU_SURF2);
        gSPClearGeometryMode(gdl++, 0xFFFFFFFF);
        gSPSetGeometryMode(gdl++, G_SHADE | G_SHADING_SMOOTH);
        gSPTexture(gdl++, 0xFFFF, 0xFFFF, 0, G_TX_RENDERTILE, G_ON);
        gDPSetCombineMode(gdl++, G_CC_MODULATERGBA_PRIM, G_CC_MODULATERGBA_PRIM);
        gDPSetPrimColor(gdl++, 0, 0, 0xFF, 0xFF, 0xFF, (gVehOverlayAlpha < gHudAlpha) ? gVehOverlayAlpha : gHudAlpha);
        gDPLoadTextureBlock(gdl++, OS_K0_TO_PHYSICAL(gVehOverlayTex), G_IM_FMT_RGBA, G_IM_SIZ_16b, 32, 32, 0,
                            G_TX_MIRROR | G_TX_CLAMP, G_TX_MIRROR | G_TX_CLAMP, G_TX_NOMASK, G_TX_NOMASK,
                            G_TX_NOLOD, G_TX_NOLOD);
        gSPVertex(gdl++, OS_K0_TO_PHYSICAL(gVehOverlayVtx), 4, 0);
        gSP1Triangle(gdl++, 0, 1, 2, 0);
        gSP1Triangle(gdl++, 0, 2, 3, 0);
        gDPPipeSync(gdl++);
        PORT_WTAG(gdl, PORT_WTAG_NONE, 0, 0);
    }
    *gfx = gdl;
}

extern u8 gBlastDomeCount;  /* number of active rings (0..2) */
extern u32 gBlastDomeTime; /* frame the last ring started */
extern Gfx gBlastDomeSeg6[];
extern Gfx gBlastDomeDl[];
extern Mtx gBlastDomeMtx[][2];
extern f32 gBlastScales[]; /* ring scales */


/* Resets the carrier blast (0x80282728). */
void fx_init_carrier_blast(void) {
    gBlastDomeCount = 0;
    gBlastDomeTime = 0;
}

/* The carrier explodes: red tint, growing dome, blast damage (0x8028273C).
 * Expanding-ring effect: tint the screen, then draw up to two growing copies of display list
 * gBlastDomeDl at (gCarrierX, gCarrierY, gCarrierZ) / 32, a new one 10 frames after the last. */
void fx_draw_carrier_blast(Gfx **gfx, u8 arg1) {
    Gfx *gdl;
    f32 mf[4][4];
    f32 scale[4][4];
    s32 i;

    gdl = *gfx;
    if (gCarrierHit != 0) {
        if (gCarrierExplodeDone != 0 && gBlastDomeCount == 0) {
            gBlastDomeCount = 1;
            gBlastScales[0] = 0.001f;
            gBlastDomeTime = gScheduler.frameCount;
        }
        gDPPipeSync(gdl++);
        gDPSetColorDither(gdl++, G_CD_DISABLE);
        if (gBlastDomeCount != 0) {
            gDPPipeSync(gdl++);
            gDPSetRenderMode(gdl++, G_RM_XLU_SURF, G_RM_XLU_SURF2);
            gDPSetCombineMode(gdl++, G_CC_PRIMITIVE, G_CC_PRIMITIVE);
            gDPSetPrimColor(gdl++, 0, 0, 0x37, 0x00, 0x00, 0x9B);
            gDPFillRectangle(gdl++, 0, 0, 319, 239);
        }
        for (i = 0; i < gBlastDomeCount; i++) {
            guTranslateF(mf, gCarrierX / 32.0f, gCarrierY / 32.0f, gCarrierZ / 32.0f);
            guScaleF(scale, gBlastScales[i], gBlastScales[i], gBlastScales[i]);
            gBlastScales[i] += 0.06;
            guMtxCatF(scale, mf, mf);
            guMtxF2L(mf, &gBlastDomeMtx[arg1][i]);
            gDPPipeSync(gdl++);
            gDPSetPrimColor(gdl++, 0, 0, 0xFF, 0x00, 0x00, 0x64);
            gSPMatrix(gdl++, osVirtualToPhysical(&gBlastDomeMtx[arg1][i]), G_MTX_MODELVIEW | G_MTX_LOAD | G_MTX_NOPUSH);
            gSPSegment(gdl++, 6, osVirtualToPhysical(gBlastDomeSeg6));
            gSPDisplayList(gdl++, osVirtualToPhysical(gBlastDomeDl));
            gSPMatrix(gdl++, &seg2Base[7], G_MTX_MODELVIEW | G_MTX_LOAD | G_MTX_NOPUSH);
        }
        if (gBlastDomeTime + 10 < gScheduler.frameCount && gBlastDomeCount != 0 && gBlastDomeCount < 2) {
            gBlastDomeTime = gScheduler.frameCount;
            gBlastScales[gBlastDomeCount] = 0.001f;
            gBlastDomeCount++;
        }
        gDPPipeSync(gdl++);
        gDPSetColorDither(gdl++, G_CD_MAGICSQ);
        gDPPipeSync(gdl++);
        if (gBlastDomeCount != 0) {
            obj_blast_around_carrier(gBlastScales[0] * 283.0f);
        }
    }
    *gfx = gdl;
}
