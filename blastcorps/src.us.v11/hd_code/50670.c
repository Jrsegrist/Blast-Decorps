#include "common.h"
#include <ultra64.h>
#include "game/game.h"

/* ghostdigger.c: records the player's path and plays it back as a ghost */

/* One recorded sample, 0x14 bytes */
typedef struct {
    /* 0x00 */ s32 x;
    /* 0x04 */ s32 y;
    /* 0x08 */ s32 z;
    /* 0x0C */ s16 rx;
    /* 0x0E */ s16 ry;
    /* 0x10 */ s16 rz;
    /* 0x12 */ s16 time;
} GhostSample;

typedef struct {
    /* 0x00 */ s32 playCount;
    /* 0x04 */ u32 recCount;
} GhostCounts;

/* Render-mode remap entry, 8 bytes */
typedef struct {
    /* 0x00 */ u32 from;
    /* 0x04 */ u32 to;
} GhostRenderMode;

/* Trigger quad, 0x2C bytes */
typedef struct {
    /* 0x00 */ s32 x[4];
    /* 0x10 */ s32 z[4];
    /* 0x20 */ s8 list[8];
    /* 0x28 */ u8 id;
} GhostQuad;

void ghost_get_pose(s32 *x, s32 *y, s32 *z, s16 *rx, s16 *ry, s16 *rz);
s16 ghost_lerp_angle(s16 from, s16 to, f32 t);

extern s32 gGhostVehicleScales[];
extern GhostRenderMode gGhostRenderModeMap[];
extern GhostQuad gCullOverrideQuads[];

extern GhostSample * N64P gGhostSamples[];
extern GhostCounts gGhostCounts[];
extern s32 gGhostSampleIndex;
extern u8 gGhostVehicle;
extern u8 gGhostValid;
extern s32 gGhostRecStartTime;
extern s32 gGhostPlayStartTime;
extern u32 gGhostBestTime;
extern u8 gGhostOverrun;

/* The ghost digger's sample buffers (0x80294E30). */
void ghost_init(void) {
    gGhostSamples[0] = (GhostSample *) 0x80055400;
    gGhostSamples[1] = (GhostSample *) 0x80065400;
    gGhostBestTime = stats_tenths_to_frames(0xFFFF) - 1;
    gGhostValid = 0;
}

/* Starts recording the player's path (0x80294E88). */
void ghost_start_recording(void) {
    gGhostCounts[0].recCount = 0;
    gGhostRecStartTime = -1;
    gGhostRecording = 1;
    gGhostOverrun = 0;
}

/* Starts the ghost's playback (0x80294EB8). */
void ghost_start_playback(void) {
    if (gGhostValid) {
        gGhostPlaying = 1;
        gGhostPlayStartTime = -1;
        gGhostSampleIndex = 0;
        gGhostModelType = gGhostVehicle;
    }
}

/* One sample ("OVERRUN GHOST DIGGER ARRAY") (0x80294F00). */
void ghost_record(void) {
    if (gGameMode & (MODE_LEVEL | MODE_MAP_VIEW)) {
        if (gGhostRecStartTime == -1) {
            gGhostRecStartTime = ((s32) PORT_GVI("ghost_record", gScheduler.retraceCount));
        }
        if (gGhostCounts[0].recCount < 0xCCC) {
            gGhostSamples[1][gGhostCounts[0].recCount].x = gPlayerX;
            gGhostSamples[1][gGhostCounts[0].recCount].y = gPlayerY;
            gGhostSamples[1][gGhostCounts[0].recCount].z = gPlayerZ;
            gGhostSamples[1][gGhostCounts[0].recCount].rx = gModelRot[0];
            gGhostSamples[1][gGhostCounts[0].recCount].ry = gModelRot[1];
            gGhostSamples[1][gGhostCounts[0].recCount].rz = gModelRot[2];
            gGhostSamples[1][gGhostCounts[0].recCount].time = ((s32) PORT_GVI("ghost_record", gScheduler.retraceCount)) - gGhostRecStartTime;
            gGhostCounts[0].recCount++;
        } else {
            gGhostOverrun = 1;
            debug_printf("OVERRUN GHOST DIGGER ARRAY\n");
        }
    }
}

/* Draws the ghost (0x80295120). */
void ghost_draw(Gfx **gfxp, Mtx *mtx) {
    Gfx *gdl = *gfxp;
    s32 x;
    s32 y;
    s32 z;
    s16 rx;
    s16 ry;
    s16 rz;

    if (gGhostPlaying) {
        ghost_get_pose(&x, &y, &z, &rx, &ry, &rz);
        mtx_build_model_rot(x, y, z, rx, ry, rz, gGhostVehicleScales[gGhostVehicle], &mtx[83]);
        gSPSegment(gdl++, 6, osVirtualToPhysical(gGhostVtx));
        gSPSegment(gdl++, 7, osVirtualToPhysical(gGhostMtx));
        gSPMatrix(gdl++, &seg2Base[83], G_MTX_MODELVIEW | G_MTX_MUL | G_MTX_NOPUSH);
        gDPPipeSync(gdl++);
        gDPSetEnvColor(gdl++, 0, 0, 0, 255);
        gDPSetPrimColor(gdl++, 0, 0, 255, 255, 255, 100);
        gSPClearGeometryMode(gdl++, -1);
        gSPDisplayList(gdl++, osVirtualToPhysical(gGhostDl));
        gSPMatrix(gdl++, &seg2Base[7], G_MTX_MODELVIEW | G_MTX_LOAD | G_MTX_NOPUSH);
        gDPPipeSync(gdl++);
    }
    *gfxp = gdl;
}

/* The ghost's pose at this frame (0x80295394). */
void ghost_get_pose(s32 *x, s32 *y, s32 *z, s16 *rx, s16 *ry, s16 *rz) {
    u8 found;
    s32 t;
    s32 next;
    s32 span;
    s32 elapsed;
    f32 frac;

    t = ((s32) gScheduler.retraceCount) - gGhostPlayStartTime;
    if (gGameMode == MODE_LEVEL_START) {
        *x = gGhostSamples[0][0].x;
        *y = gGhostSamples[0][0].y;
        *z = gGhostSamples[0][0].z;
        *rx = gGhostSamples[0][0].rx;
        *ry = gGhostSamples[0][0].ry;
        *rz = gGhostSamples[0][0].rz;
        gGhostPlayStartTime = ((s32) gScheduler.retraceCount);
    } else {
        found = 0;
        while (gGhostSampleIndex < gGhostCounts[0].playCount - 2 && !found) {
            if (t >= gGhostSamples[0][gGhostSampleIndex].time && t < gGhostSamples[0][gGhostSampleIndex + 1].time) {
                found = 1;
            } else {
                gGhostSampleIndex++;
            }
        }
        found = 0;
        next = gGhostSampleIndex + 1;
        while (next < gGhostCounts[0].playCount - 2 && !found) {
            if (t <= gGhostSamples[0][next].time) {
                found = 1;
            } else {
                next++;
            }
        }
        span = gGhostSamples[0][next].time - gGhostSamples[0][gGhostSampleIndex].time;
        if (next >= gGhostCounts[0].playCount - 2) {
            elapsed = gGhostSamples[0][next].time - gGhostSamples[0][gGhostSampleIndex].time;
        } else {
            elapsed = t - gGhostSamples[0][gGhostSampleIndex].time;
        }
        frac = (f32) elapsed / (f32) span;
        *x = (gGhostSamples[0][next].x - gGhostSamples[0][gGhostSampleIndex].x) * frac + gGhostSamples[0][gGhostSampleIndex].x;
        *y = (gGhostSamples[0][next].y - gGhostSamples[0][gGhostSampleIndex].y) * frac + gGhostSamples[0][gGhostSampleIndex].y;
        *z = (gGhostSamples[0][next].z - gGhostSamples[0][gGhostSampleIndex].z) * frac + gGhostSamples[0][gGhostSampleIndex].z;
        *rx = ghost_lerp_angle(gGhostSamples[0][gGhostSampleIndex].rx, gGhostSamples[0][next].rx, frac);
        *ry = ghost_lerp_angle(gGhostSamples[0][gGhostSampleIndex].ry, gGhostSamples[0][next].ry, frac);
        *rz = ghost_lerp_angle(gGhostSamples[0][gGhostSampleIndex].rz, gGhostSamples[0][next].rz, frac);
    }
}

/* Angle interpolation for the ghost (0x80295924). */
s16 ghost_lerp_angle(s16 from, s16 to, f32 t) {
    s16 d = to - from;

    if (d >= -0x800) {
        if (d > 0x800) {
            d -= 0xFFF;
        }
    } else {
        d += 0xFFF;
    }
    t *= d;
    t += from;
    if (t < 0.0) {
        t += 4095.0;
    }
    if (t > 4095.0) {
        t -= 4095.0;
    }
    return t;
}

/* Keeps the run if its time is the best (0x80295A20). */
void ghost_keep_if_best(u32 arg0) {
    u8 *dst;
    u8 *src;
    s32 i;

    if (gGhostBestTime >= arg0) {
        if (gGhostOverrun == 0) {
            gGhostValid = 1;
        } else {
            gGhostValid = 0;
        }
        dst = (u8 *) gGhostSamples[0];
        src = (u8 *) gGhostSamples[1];
        for (i = 0; i < 0x10000; i++) {
            dst[i] = src[i];
        }
        gGhostVehicle = gDiggerVehicle;
        gGhostCounts[0].playCount = gGhostCounts[0].recCount;
        gGhostBestTime = arg0;
    }
}

/* The ghost's translucent display list (0x80295AE0). */
void ghost_make_translucent_dl(Gfx *gdl, Gfx *end) {
    s8 cmd;
    u32 shift;
    u8 found;
    s32 i;
    u32 mode;

    while (gdl != end) {
        cmd = gdl->words.w0 >> 24;
        switch ((s8) cmd) {
            case (s8) G_SETCOMBINE:
                {
                    Gfx *_g = (Gfx *) (gdl++);
                    _g->words.w0 = 0xFC119623;
                    _g->words.w1 = 0xFF2FFFFF;
                }
                break;
            case (s8) G_SETOTHERMODE_L:
                shift = (gdl->words.w0 >> 8) & 0xFFFF;
                if (shift == G_MDSFT_RENDERMODE) {
                    mode = gdl->words.w1;
                    i = 0;
                    found = 0;
                    do {
                        if (gGhostRenderModeMap[i].from == mode) {
                            found = 1;
                        } else {
                            i++;
                        }
                    } while (!found && i < 6);
                    if (!found) {
                        debug_printf("\n\a --- ASSERTION FAULT - %s - %s, line %d\n\n", "found", "ghostdigger.c", 338);
                    }
                    gdl->words.w1 = gGhostRenderModeMap[i].to;
                }
                gdl++;
                break;
            default:
                gdl++;
                break;
        }
    }
}

/* Mode 4: a fixed visible-cell list while inside the level's quad (0x80295C70). */
void cull_override_visible_cells(u8 id, s32 px, s32 pz) {
    s32 i;
    s32 j;

    for (i = 0; i < 1; i++) {
        if (gCullOverrideQuads[i].id == id) {
            if (ground_in_triangle(px, pz, gCullOverrideQuads[i].x[0], gCullOverrideQuads[i].z[0], gCullOverrideQuads[i].x[1],
                              gCullOverrideQuads[i].z[1], gCullOverrideQuads[i].x[2], gCullOverrideQuads[i].z[2]) ||
                ground_in_triangle(px, pz, gCullOverrideQuads[i].x[0], gCullOverrideQuads[i].z[0], gCullOverrideQuads[i].x[2],
                              gCullOverrideQuads[i].z[2], gCullOverrideQuads[i].x[3], gCullOverrideQuads[i].z[3])) {
                j = 0;
                while (gCullOverrideQuads[i].list[j] != -1) {
                    gVisibleCells[j] = gCullOverrideQuads[i].list[j];
                    j++;
                }
                gVisibleCells[j] = 0xFFFF;
            }
        }
    }
}
