#include "common.h"
#include <ultra64.h>
#include "game/game.h"
/* Views: this file reads these shared variables (game/variables.h) as other types. */
#define seg2Base ((u8 *) seg2Base)
/* end of views */

/* A zone: an x/z shape (six values for ground_in_triangle) spanning ymin..ymax */
typedef struct {
    s16 a;
    s16 b;
    s16 c;
    s16 d;
    s16 e;
    s16 f;
    s16 ymin;
    s16 ymax;
} Zone;

/* Zones per level */
typedef struct {
    Zone * N64P start;
    Zone * N64P end;
    u8 level;
} ZoneList;

extern ZoneList gTrailZones[11];


/* One trail segment: a quad (4 corners x/y/z), two countdown timers, an
 * "open" flag and a style byte */
typedef struct {
    s16 v[12];
    u8 a;
    u8 b;
    u8 c;
    u8 d;
} Trail; /* 0x1C */

extern Trail gTrails[80]; /* ring, oldest at gTrailTail, newest at gTrailHead */

/* The trail's vertex and display-list buffer (seen through segment 2) */
typedef struct TrailBuf {
    u8 pad[0x2000];
    Vtx vtx[451];
    Gfx dl[1];
} TrailBuf;

void trail_fade_oldest(void);
void trail_box_corners(s16 x0, s16 y0, s16 z0, s16 x1, s16 y1, s16 z1, Vtx *v, s32 i);

/* A point inside one of the level's no-track zones (0x8027BCF0).
 * Is (x, y, z) inside one of the current level's zones? (zones span ymin..ymax; the x/z test is ground_in_triangle) */
/* K&R: the caller passes unconverted ints */
#ifdef NON_MATCHING
s32 trail_in_zone(s16 x, s16 y, s16 z)
#else
s32 trail_in_zone(x, y, z)
    s16 x;
    s16 y;
    s16 z;
#endif
{
    s32 i = 0;
    u8 found = 0;
    Zone *p;
    Zone *end;

    do {
        if (gTrailZones[i].level == levelno) {
            found = 1;
        } else {
            i++;
        }
    } while (i < 11 && !found);
    if (!found) {
        return 0;
    }
    p = gTrailZones[i].start;
    end = gTrailZones[i].end;
    while (p != end) {
        if (y >= p->ymin && y <= p->ymax) {
            if (ground_in_triangle(x, z, p->a, p->b, p->c, p->d, p->e, p->f)) {
                return 1;
            }
        }
        p++;
    }
    return 0;
}

extern u8 gTrailTail;
extern u8 gTrailHead;
extern u8 gTrailSegCalls;
extern s32 gTrailLastFrame;

/* Empties the tyre-track ring (0x8027BE4C).
 * The dead mid-function `addiu sp` pair is an unused local (-O1 gives
 * every declared local a frame slot, even one never touched). */
void trail_init(void) {
    s32 unused;

    gTrailTail = 0;
    gTrailHead = 0;
    gTrailSegCalls = 0;
    gTrailLastFrame = -1;
}

/* Adds a tyre-track segment (wheel offsets, yaw) (0x8027BE7C).
 * Adds a trail segment at (x, y, z) (1/32 units), rotated by yaw; closes it every period calls or after a skipped frame */
void trail_add(u8 period, s32 y, s16 x1, s16 z1, s16 x2, s16 z2, s32 x, s32 z, s16 yaw, u8 halfw, u8 a,
                   u8 b, u8 d) {
    f32 mf[4][4];
    f32 p1x;
    f32 p1z;
    f32 p2x;
    f32 p2z;
    f32 py;
    f32 hx;
    f32 hz;
    s16 c1x;
    s16 c1z;
    s16 c2x;
    s16 c2z;

    if (trail_in_zone(x >> 5, y >> 5, z >> 5)) {
        return;
    }
    guRotateF(mf, (f32) yaw / 4095.0 * 360.0, 0.0f, 1.0f, 0.0f);
    guMtxXFMF(mf, x1, 0.0f, z1, &p1x, &py, &p1z);
    guMtxXFMF(mf, x2, 0.0f, z2, &p2x, &py, &p2z);
    c1x = (s32) ((f32) x + p1x) >> 5;
    c1z = (s32) ((f32) z + p1z) >> 5;
    c2x = (s32) ((f32) x + p2x) >> 5;
    c2z = (s32) ((f32) z + p2z) >> 5;
    y >>= 5;
    guMtxXFMF(mf, halfw, 0.0f, 0.0f, &hx, &py, &hz);
    if (gTrailLastFrame + 1 != ((s32) gFramesInMode) || gTrailLastFrame == -1) {
        gTrails[gTrailHead].c = 1;
        gTrailHead++;
        if (gTrailHead == 80) {
            gTrailHead = 0;
        }
        if (gTrailHead == gTrailTail) {
            gTrailTail++;
            if (gTrailTail == 80) {
                gTrailTail = 0;
            }
        }
    }
    gTrails[gTrailHead].d = d;
    gTrails[gTrailHead].v[0] = c1x + (s16) hx;
    gTrails[gTrailHead].v[1] = y;
    gTrails[gTrailHead].v[2] = c1z + (s16) hz;
    gTrails[gTrailHead].v[3] = c1x - (s16) hx;
    gTrails[gTrailHead].v[4] = y;
    gTrails[gTrailHead].v[5] = c1z - (s16) hz;
    gTrails[gTrailHead].v[6] = c2x + (s16) hx;
    gTrails[gTrailHead].v[7] = y;
    gTrails[gTrailHead].v[8] = c2z + (s16) hz;
    gTrails[gTrailHead].v[9] = c2x - (s16) hx;
    gTrails[gTrailHead].v[10] = y;
    gTrails[gTrailHead].v[11] = c2z - (s16) hz;
    gTrails[gTrailHead].a = a;
    gTrails[gTrailHead].b = b;
    gTrailSegCalls++;
    if (gTrailSegCalls >= period) {
        gTrailSegCalls = 0;
        gTrails[gTrailHead].c = 0;
        gTrailHead++;
        if (gTrailHead == 80) {
            gTrailHead = 0;
        }
        if (gTrailHead == gTrailTail) {
            gTrailTail++;
            if (gTrailTail == 80) {
                gTrailTail = 0;
            }
        }
    }
    gTrailLastFrame = ((s32) gFramesInMode);
}

/* The tracks' strips and cull boxes (0x8027C4C8).
 * Draws the trail: each run of open segments becomes triangle strips in a sub display list, with a cull box for long runs */
void trail_draw(Gfx **gfxp, TrailBuf *buf) {
    Gfx *gdl = *gfxp;
    u8 cur = gTrailTail;
    u8 start;
    u8 end;
    u8 j;
    u8 done = 0;
    Gfx *dl = buf->dl;
    s32 dlIdx = 0;
    s32 vbase = 0;
    s32 k;
    s16 minX;
    s16 minY;
    s16 minZ;
    s16 maxX;
    s16 maxY;
    s16 maxZ;
    u8 t;
    u8 nv;
    s32 n;
    s32 m;
    u8 style;

    trail_fade_oldest();
    gDPPipeSync(gdl++);
    gDPSetCycleType(gdl++, G_CYC_1CYCLE);
    gDPSetRenderMode(gdl++, 0x5041C8, 0);
    gSPClearGeometryMode(gdl++, 0xFFFFFFFF);
    gSPSetGeometryMode(gdl++, G_SHADE | G_SHADING_SMOOTH);
    gSPTexture(gdl++, 0xFFFF, 0xFFFF, 0, G_TX_RENDERTILE, G_OFF);
    gDPSetCombine(gdl++, 0xFFFFFF, 0xFFFDF8FC);
    gDPSetPrimColor(gdl++, 0, 0, 0, 0, 0, 0);
    while (!done) {
        start = cur;
        style = gTrails[cur].d;
        while (cur != gTrailHead && !gTrails[cur].c) {
            cur++;
            if (cur == 80) {
                cur = 0;
            }
        }
        end = cur;
        if (cur == gTrailHead) {
            done = 1;
        }
        if (start != end) {
            gSPDisplayList(gdl++, dlIdx * sizeof(Gfx) + 0x3C30 + (u32) seg2Base);
            minZ = minY = minX = 0x7FFF;
            maxZ = maxY = maxX = -0x8000;
            k = vbase;
            j = start;
            while (j != end) {
                buf->vtx[k].v.ob[0] = gTrails[j].v[0];
                buf->vtx[k].v.ob[1] = gTrails[j].v[1];
                buf->vtx[k].v.ob[2] = gTrails[j].v[2];
                buf->vtx[k].v.cn[3] = gTrails[j].a;
                k++;
                buf->vtx[k].v.ob[0] = gTrails[j].v[3];
                buf->vtx[k].v.ob[1] = gTrails[j].v[4];
                buf->vtx[k].v.ob[2] = gTrails[j].v[5];
                buf->vtx[k].v.cn[3] = gTrails[j].a;
                k++;
                if (gTrails[j].v[0] < minX) {
                    minX = gTrails[j].v[0];
                }
                if (gTrails[j].v[1] < minY) {
                    minY = gTrails[j].v[1];
                }
                if (gTrails[j].v[2] < minZ) {
                    minZ = gTrails[j].v[2];
                }
                if (gTrails[j].v[0] > maxX) {
                    maxX = gTrails[j].v[0];
                }
                if (gTrails[j].v[1] > maxY) {
                    maxY = gTrails[j].v[1];
                }
                if (gTrails[j].v[2] > maxZ) {
                    maxZ = gTrails[j].v[2];
                }
                if (gTrails[j].v[3] < minX) {
                    minX = gTrails[j].v[3];
                }
                if (gTrails[j].v[4] < minY) {
                    minY = gTrails[j].v[4];
                }
                if (gTrails[j].v[5] < minZ) {
                    minZ = gTrails[j].v[5];
                }
                if (gTrails[j].v[3] > maxX) {
                    maxX = gTrails[j].v[3];
                }
                if (gTrails[j].v[4] > maxY) {
                    maxY = gTrails[j].v[4];
                }
                if (gTrails[j].v[5] > maxZ) {
                    maxZ = gTrails[j].v[5];
                }
                j++;
                if (j == 80) {
                    j = 0;
                }
            }
            if (!style) {
                j = start;
                while (j != end) {
                    buf->vtx[k].v.ob[0] = gTrails[j].v[6];
                    buf->vtx[k].v.ob[1] = gTrails[j].v[7];
                    buf->vtx[k].v.ob[2] = gTrails[j].v[8];
                    buf->vtx[k].v.cn[3] = gTrails[j].b;
                    k++;
                    buf->vtx[k].v.ob[0] = gTrails[j].v[9];
                    buf->vtx[k].v.ob[1] = gTrails[j].v[10];
                    buf->vtx[k].v.ob[2] = gTrails[j].v[11];
                    buf->vtx[k].v.cn[3] = gTrails[j].b;
                    k++;
                    /* (sic) both corners test v[6..8] for the minimum and v[9..11] for the maximum */
                    if (gTrails[j].v[6] < minX) {
                        minX = gTrails[j].v[6];
                    }
                    if (gTrails[j].v[7] < minY) {
                        minY = gTrails[j].v[7];
                    }
                    if (gTrails[j].v[8] < minZ) {
                        minZ = gTrails[j].v[8];
                    }
                    if (gTrails[j].v[9] > maxX) {
                        maxX = gTrails[j].v[9];
                    }
                    if (gTrails[j].v[10] > maxY) {
                        maxY = gTrails[j].v[10];
                    }
                    if (gTrails[j].v[11] > maxZ) {
                        maxZ = gTrails[j].v[11];
                    }
                    if (gTrails[j].v[6] < minX) {
                        minX = gTrails[j].v[6];
                    }
                    if (gTrails[j].v[7] < minY) {
                        minY = gTrails[j].v[7];
                    }
                    if (gTrails[j].v[8] < minZ) {
                        minZ = gTrails[j].v[8];
                    }
                    if (gTrails[j].v[9] > maxX) {
                        maxX = gTrails[j].v[9];
                    }
                    if (gTrails[j].v[10] > maxY) {
                        maxY = gTrails[j].v[10];
                    }
                    if (gTrails[j].v[11] > maxZ) {
                        maxZ = gTrails[j].v[11];
                    }
                    j++;
                    if (j == 80) {
                        j = 0;
                    }
                }
            }
            n = k - vbase;
            if (style) {
                m = 0;
            } else {
                m = n >> 1;
            }
            if (n >= 41) {
                trail_box_corners(minX, minY, minZ, maxX, maxY, maxZ, buf->vtx, k);
                gSPVertex(dl++, k * sizeof(Vtx) + 0x2000 + (u32) seg2Base, 8, 0);
                gSPCullDisplayList(dl++, 0, 7);
                dlIdx += 2;
                k += 8;
            }
            if (n >= 3) {
                do {
                    if (n >= 17) {
                        nv = 16;
                    } else {
                        nv = n;
                    }
                    gSPVertex(dl++, vbase * sizeof(Vtx) + 0x2000 + (u32) seg2Base, nv, 0);
                    t = 0;
                    vbase += nv - 2;
                    dlIdx++;
                    for (; t < nv - 2;) {
                        if (m != 2) {
                            gSP1Triangle(dl++, t, t + 1, t + 2, 0);
                            gSP1Triangle(dl++, t + 1, t + 2, t + 3, 0);
                            dlIdx += 2;
                            m -= 2;
                            t += 2;
                        } else {
                            m = 0;
                            t += 4;
                        }
                    }
                    n = n - nv + 2;
                } while (n >= 3);
            }
            vbase = k;
            gSPEndDisplayList(dl++);
            dlIdx++;
        }
        cur++;
        if (cur == 80) {
            cur = 0;
        }
    }
    gDPPipeSync(gdl++);
    *gfxp = gdl;
}

/* A track segment's cull box (0x8027D350).
 * Writes the 8 corners of the box (x0..x1, y0..y1, z0..z1) into v[i..i+7] */
void trail_box_corners(s16 x0, s16 y0, s16 z0, s16 x1, s16 y1, s16 z1, Vtx *v, s32 i) {
    v[i].v.ob[0] = x0;
    v[i].v.ob[1] = y0;
    v[i].v.ob[2] = z0;
    i++;
    v[i].v.ob[0] = x0;
    v[i].v.ob[1] = y1;
    v[i].v.ob[2] = z0;
    i++;
    v[i].v.ob[0] = x1;
    v[i].v.ob[1] = y0;
    v[i].v.ob[2] = z0;
    i++;
    v[i].v.ob[0] = x1;
    v[i].v.ob[1] = y1;
    v[i].v.ob[2] = z0;
    i++;
    v[i].v.ob[0] = x0;
    v[i].v.ob[1] = y0;
    v[i].v.ob[2] = z1;
    i++;
    v[i].v.ob[0] = x0;
    v[i].v.ob[1] = y1;
    v[i].v.ob[2] = z1;
    i++;
    v[i].v.ob[0] = x1;
    v[i].v.ob[1] = y0;
    v[i].v.ob[2] = z1;
    i++;
    v[i].v.ob[0] = x1;
    v[i].v.ob[1] = y1;
    v[i].v.ob[2] = z1;
}

/* Past 70 in use, fades and drops the oldest (0x8027D5AC).
 * Once the 80-entry trail ring is nearly full (71+), counts down the oldest entries' two timers and drops them when both reach 0 */
void trail_fade_oldest(void) {
    s32 n;
    u8 j;

    if (gTrailHead >= gTrailTail) {
        n = gTrailHead - gTrailTail;
    } else {
        n = gTrailHead - gTrailTail + 80;
    }
    if (n >= 71) {
        if (gTrails[gTrailTail].a <= 0) {
            gTrails[gTrailTail].a = 0;
        } else {
            gTrails[gTrailTail].a--;
        }
        if (gTrails[gTrailTail].b <= 0) {
            gTrails[gTrailTail].b = 0;
        } else {
            gTrails[gTrailTail].b--;
        }
        if (gTrails[gTrailTail].a == 0 && gTrails[gTrailTail].b == 0) {
            j = gTrailTail + 1;
            if (j == 80) {
                j = 0;
            }
            if (gTrails[j].a <= 0) {
                gTrails[j].a = 0;
            } else {
                gTrails[j].a--;
            }
            if (gTrails[j].b <= 0) {
                gTrails[j].b = 0;
            } else {
                gTrails[j].b--;
            }
            if (gTrails[j].a == 0 && gTrails[j].b == 0) {
                gTrailTail = j;
            }
        }
    }
}
