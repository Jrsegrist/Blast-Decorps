#include "common.h"
#include <ultra64.h>
#include "game/game.h"

/* Spline control point, 0xC bytes */
typedef struct {
    /* 0x00 */ s16 x;
    /* 0x02 */ s16 y;
    /* 0x04 */ s16 z;
    /* 0x06 */ s16 rx;
    /* 0x08 */ s16 ry;
    /* 0x0A */ s16 rz;
} PathPoint;

/* Spline path, 0x84 bytes */
typedef struct {
    /* 0x00 */ PathPoint pts[10];
    /* 0x78 */ s32 count;
    /* 0x7C */ f32 tension;
    /* 0x80 */ u8 speed;
    /* 0x81 */ u8 mode; /* 0: y from the spline, 1: y from the ground */
    /* 0x82 */ u8 pad82[2];
} Path;

typedef struct {
    /* 0x00 */ s32 seg;
    /* 0x04 */ s32 t; /* 0..999 within the segment */
} PathState;

typedef struct {
    /* 0x00 */ s32 dir; /* 1, 2, 4, 8 */
    /* 0x04 */ s32 angle; /* degrees */
    /* 0x08 */ s32 last; /* last junction visited */
} WalkState;

/* Junction on a walker's grid, 8 bytes */
typedef struct {
    /* 0x00 */ u8 level;
    /* 0x02 */ s16 x;
    /* 0x04 */ s16 z;
    /* 0x06 */ u8 exits; /* bitmask of dirs */
    /* 0x07 */ u8 pad7;
} Junction;

typedef struct {
    /* 0x00 */ u32 time;
    /* 0x04 */ s32 angle;
} SwingState;

extern Path gSplinePaths[];
extern f32 gSplineBasis[4][4];
extern f32 gSplineT;
extern f32 gSplineT2;
extern f32 gSplineT3;
extern Junction gMazeJunctions[];

#ifdef NON_MATCHING
void spline_walk_maze(f32 mf[4][4], s16 x, s16 z, s16 ox, s16 oz, s16 *px, s16 *py, s16 *pz, WalkState *state,
                   s32 speed);
void spline_follow_path(f32 mf[4][4], s16 x, s16 z, s16 *px, s16 *py, s16 *pz, PathState *state, s32 idx);
#else
void spline_walk_maze(); /* K&R */
void spline_follow_path(); /* K&R */
#endif
f32 spline_eval(f32 a, f32 b, f32 c, f32 d);
void spline_set_basis(s32 idx);
void spline_swing(f32 mf[4][4], s32 limit, s16 *x, s16 *y, s16 *z, SwingState *state, s32 delay);
void spline_bob(f32 mf[4][4], s16 *x, s16 *y, s16 *z, s32 *counter, s32 amp);
void spline_orbit(f32 mf[4][4], s16 *x, s16 *y, s16 *z, s32 *angle, s32 speed);

/* An object's pose by motion type 1..5 (0x802933A0). */
void spline_object_matrix(s32 x, s32 y, s32 z, s32 type, Mtx *mtx, void *state, Gfx *gfx1, Gfx *gfx2, s32 idx,
                   s32 arg9, s32 arg10, s32 arg11) {
    s16 rx;
    s16 ry;
    s16 rz;
    f32 mf[4][4];
    f32 tmp[4][4];

    rx = 0;
    ry = 0;
    rz = 0;
    switch (type) {
        case 1:
            spline_bob(mf, &rx, &ry, &rz, (s32 *) state, idx);
            break;
        case 2:
            spline_orbit(mf, &rx, &ry, &rz, (s32 *) state, idx);
            break;
        case 3:
            spline_follow_path(mf, x >> 5, z >> 5, &rx, &ry, &rz, state, idx);
            break;
        case 4:
            spline_walk_maze(mf, x >> 5, z >> 5, arg9 >> 5, arg11 >> 5, &rx, &ry, &rz, state, idx);
            break;
        case 5:
            spline_swing(mf, 90, &rx, &ry, &rz, (SwingState *) state, idx);
            break;
        default:
            guTranslateF(mf, 0.0f, 0.0f, 0.0f);
            break;
    }
    if (type == 3 && gSplinePaths[idx].mode == 1) {
        y = 0;
    }
    guTranslateF(tmp, x / 32.0f, y / 32.0f, z / 32.0f);
    guMtxCatF(mf, tmp, mf);
    guMtxF2L(mf, mtx);
    gSplinePosX = (rx << 5) + x;
    gSplinePosY = (ry << 5) + y;
    gSplinePosZ = (rz << 5) + z;
    gSPMatrix(gfx1++, osVirtualToPhysical(mtx), G_MTX_MODELVIEW | G_MTX_MUL | G_MTX_PUSH);
    gSPMatrix(gfx2++, osVirtualToPhysical(mtx), G_MTX_MODELVIEW | G_MTX_MUL | G_MTX_PUSH);
}

#define ABS(x) ((x) < 0 ? -(x) : (x))

/* The maze walker on gMazeJunctions: chases the player; while gPowerPillTimer runs it turns
 * back, flees and blinks (0x802936AC). */
#ifdef NON_MATCHING
void spline_walk_maze(f32 mf[4][4], s16 x, s16 z, s16 ox, s16 oz, s16 *px, s16 *py, s16 *pz, WalkState *state,
                   s32 speed)
#else
void spline_walk_maze(mf, x, z, ox, oz, px, py, pz, state, speed)
    f32 mf[4][4];
    s16 x;
    s16 z;
    s16 ox;
    s16 oz;
    s16 *px;
    s16 *py;
    s16 *pz;
    WalkState *state;
    s32 speed;
#endif
{
    f32 m[4][4];
    s32 i;
    u8 found;
#ifdef NON_MATCHING
    s16 targetAngle = 0; /* directions without a case leave it indeterminate in the original */
#else
    s16 targetAngle;
#endif
    s16 cx;
    s16 cz;
    s16 dx;
    s16 dz;
    s16 adx;
    s16 adz;
    u8 exits;
    u8 want;
    s32 back;
    s16 cur;
    s16 target;
    s16 diff;
    s32 offset;

    i = 0;
    found = 0;
    if (gPowerPillTimer == 0 && gPowerPillSound != 0) {
        snd_stop((void *) N64_IPTR(gPowerPillSound));
    }
    if (gPaused == 0) {
        if (((u8) gPowerPillEaten)) {
            state->last = -1;
            switch (state->dir) {
                case 1:
                    state->dir = 2;
                    break;
                case 2:
                    state->dir = 1;
                    break;
                case 4:
                    state->dir = 8;
                    break;
                case 8:
                    state->dir = 4;
                    break;
            }
        }
        while (i < 102 && !found) {
            if (gMazeJunctions[i].level == levelno && gMazeJunctions[i].x - 5 <= ox && gMazeJunctions[i].z - 5 <= oz &&
                ox < gMazeJunctions[i].x + 5 && oz < gMazeJunctions[i].z + 5 && state->last != i) {
                found = 1;
            } else {
                i++;
            }
        }
        if (found) {
            state->last = i;
            cx = gPlayerX >> 5, cz = gPlayerZ >> 5;
            if (gPowerPillTimer == 0) {
                dx = cx - ox, dz = cz - oz;
            } else {
                dx = ox - cx;
                dz = oz - cz;
            }
            adx = ABS(dx);
            adz = ABS(dz);
            switch (state->dir) {
                case 1:
                    back = 2;
                    break;
                case 2:
                    back = 1;
                    break;
                case 4:
                    back = 8;
                    break;
                case 8:
                    back = 4;
                    break;
                default:
                    back = 0;
                    break;
            }
            exits = (gMazeJunctions[i].exits & back) ? (gMazeJunctions[i].exits & (back ^ 0xFF)) : gMazeJunctions[i].exits;
            if (dz > 0 && adz >= adx) {
                want = 1;
            }
            if (dz <= 0 && adz >= adx) {
                want = 2;
            }
            if (dx > 0 && adx >= adz) {
                want = 4;
            }
            if (dx <= 0 && adx >= adz) {
                want = 8;
            }
            if (exits & want) {
                state->dir = want;
            } else {
                if (want == 1) {
                    if (exits & 4) {
                        state->dir = 4;
                    } else if (exits & 8) {
                        state->dir = 8;
                    } else {
                        state->dir = 2;
                    }
                }
                if (want == 2) {
                    if (exits & 8) {
                        state->dir = 8;
                    } else if (exits & 4) {
                        state->dir = 4;
                    } else {
                        state->dir = 1;
                    }
                }
                if (want == 4) {
                    if (exits & 1) {
                        state->dir = 1;
                    } else if (exits & 2) {
                        state->dir = 2;
                    } else {
                        state->dir = 8;
                    }
                }
                if (want == 8) {
                    if (exits & 2) {
                        state->dir = 2;
                    } else if (exits & 1) {
                        state->dir = 1;
                    } else {
                        state->dir = 4;
                    }
                }
            }
        }
        if (gPowerPillTimer == 0) {
            offset = speed;
        } else {
            offset = speed * 3 / 2;
        }
        switch (state->dir) {
            case 1:
                *px = ox - x;
                *pz = oz - z + offset;
                targetAngle = 270;
                break;
            case 2:
                *px = ox - x;
                *pz = oz - z - offset;
                targetAngle = 90;
                break;
            case 4:
                *px = ox - x + offset;
                *pz = oz - z;
                targetAngle = 0;
                break;
            case 8:
                *px = ox - x - offset;
                *pz = oz - z;
                targetAngle = 180;
                break;
        }
        *py = 0;
        cur = (state->angle << 16) / 360;
        target = (targetAngle << 16) / 360;
        diff = target - cur;
        if ((diff > 0 ? diff : -diff) < 2000) {
            state->angle = targetAngle;
        } else {
            if (diff > 0) {
                state->angle += 10;
            } else {
                state->angle -= 10;
            }
            if (state->angle >= 360) {
                state->angle -= 360;
            }
            if (state->angle < 0) {
                state->angle += 360;
            }
        }
    } else {
        *px = ox - x;
        *py = 0;
        *pz = oz - z;
    }
    guRotateF(mf, state->angle, 0.0f, 1.0f, 0.0f);
    if (gPowerPillTimer == 0 || (gScheduler.frameCount & 0xF) >= 7) {
        guTranslateF(m, *px, *py, *pz);
    } else {
        guTranslateF(m, 0.0f, 20000.0f, 0.0f);
    }
    guMtxCatF(mf, m, mf);
}

/* Follows spline path idx (its tension); y from the spline or the water (0x80293F84). */
#ifdef NON_MATCHING
void spline_follow_path(f32 mf[4][4], s16 x, s16 z, s16 *px, s16 *py, s16 *pz, PathState *state, s32 idx)
#else
void spline_follow_path(mf, x, z, px, py, pz, state, idx)
    f32 mf[4][4];
    s16 x;
    s16 z;
    s16 *px;
    s16 *py;
    s16 *pz;
    PathState *state;
    s32 idx;
#endif
{
    s32 seg;
    s32 last;
    f32 rotX;
    f32 rotY;
    f32 rotZ;
    f32 m[4][4];
    u8 k[4];
    f32 p0;
    f32 p1;
    f32 p2;
    f32 p3;

    spline_set_basis(idx);
    if (gPaused == 0) {
        state->t += gSplinePaths[idx].speed;
        if (state->t >= 1000) {
            state->t = 0;
            state->seg++;
            if (state->seg >= gSplinePaths[idx].count - 1) {
                state->seg = 0;
            }
        }
    }
    seg = state->seg;
    last = gSplinePaths[idx].count - 1;
    if (seg <= 0) {
        k[0] = last + seg - 1;
    } else {
        k[0] = seg - 1;
    }
    k[1] = seg;
    if (seg + 1 >= last) {
        k[2] = seg - last + 1;
    } else {
        k[2] = seg + 1;
    }
    if (seg + 2 >= last) {
        k[3] = seg - last + 2;
    } else {
        k[3] = seg + 2;
    }
    gSplineT = (f32) state->t / 1000.0;
    gSplineT2 = gSplineT * gSplineT;
    gSplineT3 = gSplineT2 * gSplineT;

    p0 = gSplinePaths[idx].pts[k[0]].rx, p1 = gSplinePaths[idx].pts[k[1]].rx, p2 = gSplinePaths[idx].pts[k[2]].rx,
    p3 = gSplinePaths[idx].pts[k[3]].rx;
    util_unwrap_angle(p0, &p1);
    util_unwrap_angle(p1, &p2);
    util_unwrap_angle(p2, &p3);
    rotX = spline_eval(p0, p1, p2, p3);

    p0 = gSplinePaths[idx].pts[k[0]].ry, p1 = gSplinePaths[idx].pts[k[1]].ry, p2 = gSplinePaths[idx].pts[k[2]].ry,
    p3 = gSplinePaths[idx].pts[k[3]].ry;
    util_unwrap_angle(p0, &p1);
    util_unwrap_angle(p1, &p2);
    util_unwrap_angle(p2, &p3);
    rotY = spline_eval(p0, p1, p2, p3);

    p0 = gSplinePaths[idx].pts[k[0]].rz, p1 = gSplinePaths[idx].pts[k[1]].rz, p2 = gSplinePaths[idx].pts[k[2]].rz,
    p3 = gSplinePaths[idx].pts[k[3]].rz;
    util_unwrap_angle(p0, &p1);
    util_unwrap_angle(p1, &p2);
    util_unwrap_angle(p2, &p3);
    rotZ = spline_eval(p0, p1, p2, p3);

    *px = spline_eval(gSplinePaths[idx].pts[k[0]].x, gSplinePaths[idx].pts[k[1]].x, gSplinePaths[idx].pts[k[2]].x,
                        gSplinePaths[idx].pts[k[3]].x);
    *pz = spline_eval(gSplinePaths[idx].pts[k[0]].z, gSplinePaths[idx].pts[k[1]].z, gSplinePaths[idx].pts[k[2]].z,
                        gSplinePaths[idx].pts[k[3]].z);
    switch (gSplinePaths[idx].mode) {
        case 0:
            *py = spline_eval(gSplinePaths[idx].pts[k[0]].y, gSplinePaths[idx].pts[k[1]].y,
                                gSplinePaths[idx].pts[k[2]].y, gSplinePaths[idx].pts[k[3]].y);
            break;
        case 1:
            water_get_height(*px + x, *pz + z, py);
            break;
    }
    guRotateF(m, rotX, 1.0f, 0.0f, 0.0f);
    guRotateF(mf, rotY, 0.0f, 1.0f, 0.0f);
    guMtxCatF(m, mf, m);
    guRotateF(mf, rotZ, 0.0f, 0.0f, 1.0f);
    guMtxCatF(m, mf, m);
    guTranslateF(mf, *px, *py, *pz);
    guMtxCatF(m, mf, mf);
}

/* The cubic with gSplineBasis at gSplineT (0x80294840). */
f32 spline_eval(f32 a, f32 b, f32 c, f32 d) {
    s32 v0;
    s32 v1;
    s32 v2;
    s32 v3;

    v0 = gSplineBasis[0][0] * a + gSplineBasis[1][0] * b + gSplineBasis[2][0] * c + gSplineBasis[3][0] * d;
    v1 = gSplineBasis[0][1] * a + gSplineBasis[1][1] * b + gSplineBasis[2][1] * c + gSplineBasis[3][1] * d;
    v2 = gSplineBasis[0][2] * a + gSplineBasis[1][2] * b + gSplineBasis[2][2] * c + gSplineBasis[3][2] * d;
    v3 = gSplineBasis[0][3] * a + gSplineBasis[1][3] * b + gSplineBasis[2][3] * c + gSplineBasis[3][3] * d;
    return v0 * gSplineT3 + gSplineT2 * v1 + gSplineT * v2 + v3;
}

/* The cardinal basis from the path's tension (0x802949B0). */
void spline_set_basis(s32 idx) {
    f32 s;

    s = gSplinePaths[idx].tension;
    gSplineBasis[0][0] = -s;
    gSplineBasis[0][1] = s * 2.0;
    gSplineBasis[0][2] = -s;
    gSplineBasis[0][3] = 0.0f;
    gSplineBasis[1][0] = 2.0 - s;
    gSplineBasis[1][1] = s - 3.0;
    gSplineBasis[1][2] = 0.0f;
    gSplineBasis[1][3] = 1.0f;
    gSplineBasis[2][0] = s - 2.0;
    gSplineBasis[2][1] = 3.0 - s * 2.0;
    gSplineBasis[2][2] = s;
    gSplineBasis[2][3] = 0.0f;
    gSplineBasis[3][0] = s;
    gSplineBasis[3][1] = -s;
    gSplineBasis[3][2] = 0.0f;
    gSplineBasis[3][3] = 0.0f;
}

/* Turns up to a limit about y (a gate or door) (0x80294B64). */
void spline_swing(f32 mf[4][4], s32 limit, s16 *x, s16 *y, s16 *z, SwingState *state, s32 delay) {
    if (state->time == 0) {
        state->time = delay + gScheduler.frameCount;
    }
    if (gScheduler.frameCount > state->time && gPaused == 0) {
        state->angle++;
        if (state->angle > limit) {
            state->angle = limit;
        }
    }
    guRotateF(mf, -state->angle, 0.0f, 1.0f, 0.0f);
    *x = 0;
    *y = 0;
    *z = 0;
}

/* Bobs up and down (0x80294C50). */
void spline_bob(f32 mf[4][4], s16 *x, s16 *y, s16 *z, s32 *counter, s32 amp) {
    f32 s;

    if (gPaused == 0) {
        (*counter)++;
    }
    s = sinf(*counter / 5.0f);
    *x = 0;
    *z = 0;
    *y = amp * s;
    guTranslateF(mf, 0.0f, *y, 0.0f);
}

/* Circles on a tilted axis (0x80294D24). */
void spline_orbit(f32 mf[4][4], s16 *x, s16 *y, s16 *z, s32 *angle, s32 speed) {
    f32 m[4][4];
    f32 ox;
    f32 oy;
    f32 oz;

    if (gPaused == 0) {
        *angle += speed;
    }
    guRotateF(m, *angle, 0.2f, 0.7f, 0.1f);
    guMtxXFMF(m, 90.0f, 0.0f, 0.0f, &ox, &oy, &oz);
    *x = ox;
    *y = oy;
    *z = oz;
    guTranslateF(mf, ox, oy, oz);
}
