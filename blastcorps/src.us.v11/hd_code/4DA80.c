#include "common.h"
#include <ultra64.h>
#include "game/game.h"

/*
 * Ballistic shells: up to four lobbed projectiles fired from one point through
 * another, flying on a fixed-gravity arc, pitched along their path, and
 * exploding when they leave the map, hit the ground or touch something.
 */

typedef struct {
    /* 0x00 */ s32 x;
    /* 0x04 */ s32 y;
    /* 0x08 */ s32 z;
    /* 0x0C */ s32 prevY;
    /* 0x10 */ s16 vx;
    /* 0x12 */ s16 vz;
    /* 0x14 */ s16 yaw;
    /* 0x16 */ s16 pitch;
    /* 0x18 */ u8 type;
    /* 0x1A */ s16 unk1A;
    /* 0x1C */ s32 y0; /* launch height */
    /* 0x20 */ s32 vy; /* launch vertical speed */
    /* 0x24 */ s32 t;  /* frames since launch */
    /* 0x28 */ u8 active;
    /* 0x29 */ u8 alpha;
} Shell; /* size 0x2C */

/* Per-type shell data: model display list, collision id, explosion and trail parameters. */
typedef struct {
    /* 0x000 */ Gfx dl[180];
    /* 0x5A0 */ s16 unk5A0;
    /* 0x5A4 */ s32 unk5A4;
    /* 0x5A8 */ u8 unk5A8;
    /* 0x5A9 */ u8 unk5A9;
    /* 0x5AC */ s32 unk5AC;
    /* 0x5B0 */ u8 unk5B0;
} ShellType; /* size 0x5B8 */

/* Per-frame dynamic buffer (segment 2); the shell matrices sit at 0xD00. */
typedef struct Dyn {
    u8 pad[0xD00];
    Mtx mtx[4];
} Dyn;

extern ShellType gShellTypes[];
extern Shell gShells[4];

f32 sqrtf(f32);
void shell_explode(s32 i);

/* Frees all four shells (0x80292240). */
void shell_init(void) {
    s32 i;

    for (i = 0; i < 4; i++) {
        gShells[i].active = 0;
    }
}

/* Fires a shell through two points; 0 if all four fly (0x80292288). */
s32 shell_fire(s16 speed, s32 x0, s32 y0, s32 z0, s32 x1, s32 y1, s32 z1, u8 type, s16 arg8) {
    s32 i;
    u8 found;
    f32 dx;
    f32 dy;
    f32 dz;
    f32 dist;

    i = 0;
    found = 0;
    while (!found && i < 4) {
        if (gShells[i].active) {
            i++;
        } else {
            found = 1;
        }
    }
    if (!found) {
        return 0;
    }
    gShells[i].active = 1;
    gShells[i].type = type;
    gShells[i].unk1A = arg8;
    gShells[i].x = x1;
    gShells[i].y = y1;
    gShells[i].z = z1;
    gShells[i].y0 = y1;
    gShells[i].t = 0;
    gShells[i].alpha = 255;
    dx = x1 - x0, dy = y1 - y0, dz = z1 - z0;
    guNormalize(&dx, &dy, &dz);
    gShells[i].vx = speed * dx;
    gShells[i].vz = speed * dz;
    gShells[i].vy = speed * dy;
    dist = sqrtf((x1 - x0) * (x1 - x0) + (z1 - z0) * (z1 - z0));
    if (dist < 1.0) {
        dist = 1.0f;
    }
    if (x1 >= x0 && z1 >= z0) {
        gShells[i].yaw = math_asin_c((x1 - x0) * 65535.0 / dist) >> 4;
    }
    if (x1 >= x0 && z1 < z0) {
        gShells[i].yaw = (math_asin_c((z0 - z1) * 65535.0 / dist) >> 4) + 0x400;
    }
    if (x1 < x0 && z1 < z0) {
        gShells[i].yaw = (math_asin_c((x0 - x1) * 65535.0 / dist) >> 4) + 0x800;
    }
    if (x1 < x0 && z1 >= z0) {
        gShells[i].yaw = (math_asin_c((z1 - z0) * 65535.0 / dist) >> 4) + 0xC00;
    }
    return 1;
}

/* The shells' gravity arcs; explode on a hit (0x80292830). */
void shell_update_all(void) {
    s32 i;
    s16 pitch;
    s16 savedA;
    s16 savedB;
    s32 limX;
    s32 limZ;

    limX = ((u16) gLevelCols) * gLevelCellW;
    limZ = gLevelRows * gLevelCellD;
    for (i = 0; i < 4; i++) {
        if (gShells[i].active) {
            gShells[i].x += gShells[i].vx;
            gShells[i].z += gShells[i].vz;
            gShells[i].y = (gShells[i].y0 + gShells[i].vy * gShells[i].t) +
                              -12.0 * gShells[i].t * gShells[i].t;
            pitch = gShells[i].prevY - gShells[i].y;
            if (pitch >= 0x400) {
                pitch = 0x3FF;
            }
            if (pitch < -0x3FF) {
                pitch = -0x3FF;
            }
            gShells[i].pitch = pitch;
            if (gShells[i].x >= limX || gShells[i].z >= limZ || gShells[i].x < 0 ||
                gShells[i].z < 0 || gShells[i].y < 0) {
                shell_explode(i);
            } else {
                coll_begin_probe(gShells[i].x, gShells[i].y, gShells[i].z);
                coll_probe_vehicles(gShellTypes[gShells[i].type].unk5A0);
                if (gProbeHitCarrier) {
                    cmo_hit_request = 1;
                }
                savedA = gFreeArcStart, savedB = gFreeArcEnd;
                coll_probe_tris(gShells[i].x, gShells[i].y, gShells[i].z,
                              gShellTypes[gShells[i].type].unk5A0, 0xCA, 0);
                coll_probe_objects(gShellTypes[gShells[i].type].unk5A0, gShells[i].unk1A);
                if (savedA != gFreeArcStart || savedB != gFreeArcEnd) {
                    shell_explode(i);
                }
                if (gShells[i].y < coll_ground_height(gShells[i].x, gShells[i].z, gShells[i].y)) {
                    shell_explode(i);
                }
                if (gShellTypes[gShells[i].type].unk5A9) {
                    fx_spawn_at(gShells[i].x, gShells[i].y, gShells[i].z,
                                  gShellTypes[gShells[i].type].unk5B0, gShellTypes[gShells[i].type].unk5AC);
                }
                gShells[i].prevY = gShells[i].y;
                gShells[i].alpha -= 25;
                if (gShells[i].alpha < 100) {
                    gShells[i].alpha = 255;
                }
                gShells[i].t++;
            }
        }
    }
}

/* Shell i explodes: effect, shake, sound (0x80292DDC). */
void shell_explode(s32 i) {
    gShells[i].active = 0;
    fx_spawn_at(gShells[i].x, gShells[i].y, gShells[i].z, gShellTypes[gShells[i].type].unk5A8,
                  gShellTypes[gShells[i].type].unk5A4);
    gShakeTime = 10;
    gShakeSize = 400;
    snd_play(gSfxBank, SFX_EXPLOSION, 0);
}

/* Draws the flying shells (0x80292EB8). */
void shell_draw_all(Gfx **gdl, Dyn *dyn) {
    Gfx *gfx = *gdl;
    s32 i;
    f32 mf1[4][4];
    f32 mf2[4][4];

    gDPPipeSync(gfx++);
    gDPSetCycleType(gfx++, G_CYC_1CYCLE);
    gDPSetRenderMode(gfx++, G_RM_AA_ZB_OPA_SURF, G_RM_AA_ZB_OPA_SURF2);
    gDPSetCombineMode(gfx++, G_CC_SHADE, G_CC_SHADE);
    gSPClearGeometryMode(gfx++, -1);
    gSPSetGeometryMode(gfx++, G_ZBUFFER | G_SHADE | G_SHADING_SMOOTH);
    gSPTexture(gfx++, 0xFFFF, 0xFFFF, 0, G_TX_RENDERTILE, G_OFF);
    for (i = 0; i < 4; i++) {
        if (gShells[i].active && gShells[i].t >= 2) {
            guScaleF(mf1, 0.5f, 0.5f, 0.35f);
            guRotateF(mf2, gShells[i].pitch / 4095.0 * 360.0, 1.0f, 0.0f, 0.0f);
            guMtxCatF(mf1, mf2, mf1);
            guRotateF(mf2, gShells[i].yaw / 4095.0 * 360.0, 0.0f, 1.0f, 0.0f);
            guMtxCatF(mf1, mf2, mf1);
            guTranslateF(mf2, gShells[i].x / 32.0f, gShells[i].y / 32.0f, gShells[i].z / 32.0f);
            guMtxCatF(mf1, mf2, mf1);
            guMtxF2L(mf1, &dyn->mtx[i]);
            gSPMatrix(gfx++, &seg2Base[i + 52], G_MTX_MODELVIEW | G_MTX_MUL | G_MTX_PUSH);
            gDPPipeSync(gfx++);
            gDPSetPrimColor(gfx++, 0, 0, gShells[i].alpha, 0, 0, 255);
            gSPDisplayList(gfx++, osVirtualToPhysical(gShellTypes[gShells[i].type].dl));
            gSPPopMatrix(gfx++, G_MTX_MODELVIEW);
        }
    }
    gDPPipeSync(gfx++);
    *gdl = gfx;
}
