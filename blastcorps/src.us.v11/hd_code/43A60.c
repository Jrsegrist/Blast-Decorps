#include "common.h"
#include <ultra64.h>
#include "game/game.h"

/*
 * Billboard particle emitter: up to 50 textured quads spawned in bursts from
 * one point, flung out in a random cone, pulled down by gravity, bounced off a
 * floor height and animated through a per-state texture sequence. Textures are
 * streamed into a small cache on demand.
 */

/* ParticleDef (the emitter definitions, list gParticleDefs) is in game/types.h. */

typedef struct {
    /* 0x00 */ u8 active;
    /* 0x04 */ s32 pos[3];
    /* 0x10 */ s16 vel[3];
    /* 0x16 */ u8 state; /* 0 birth, 1 loop, 3 death */
    /* 0x17 */ u8 life;
    /* 0x18 */ s16 frame;
    /* 0x1A */ s16 fallTime;
    /* 0x1C */ s16 age;
} Particle; /* size 0x20 */

typedef struct {
    u8 key;  /* particle index */
    s32 val; /* texture address, sort key */
} SortEntry;

extern ParticleDef * N64P gParticleDefs[];
extern Vtx gParticleQuad[4];
extern ParticleDef * N64P gParticleDef;
extern Particle gParticles[50];
extern Mtx gParticleMtx[][50];
extern s32 gEmitterX; /* emitter position */
extern s32 gEmitterY;
extern s32 gEmitterZ;
extern u8 gEmitterBursts; /* bursts emitted */
extern s32 gEmitterFloorY; /* floor height */
extern u8 gEmitterActive;  /* emitter busy */
extern u8 gEmitterCooldown;  /* cooldown */
extern u8 * N64P gParticleTexCache; /* texture cache */
extern s16 gParticleTexIds[];
extern s32 gParticleTexCount; /* textures cached */
extern s32 gParticleTexSize; /* bytes per texture */

void ptcl_set_render_state(Gfx **gdl);
u32 ptcl_get_texture(s16 id);
void ptcl_sort_by_texture(SortEntry *a, s32 n);

/* Old SDK form of the 4-bit dxt helper: no MAX(1, ...) clamp. */
#undef TXL2WORDS_4b
#define TXL2WORDS_4b(txls) ((txls) / 16)

/* Frees all 50 particles; takes the texture cache (0x80288220). */
void ptcl_init(void) {
    s32 i;

    for (i = 0; i < 50; i++) {
        gParticles[i].active = 0;
    }
    gEmitterActive = 0;
    gEmitterCooldown = 0;
    gParticleTexCache = gHeapPtr;
    gHeapPtr += 0x1400;
}

/* Starts emitter type at (x, y, z); 0 if one is still running (0x80288284). */
s32 ptcl_start(u8 type, s32 x, s32 y, s32 z, s32 floor) {
    s32 unused;
#ifdef NON_MATCHING
    s32 bpp = 0; /* formats without a case leave it indeterminate in the original */
#else
    s32 bpp;
#endif

    if (gEmitterActive == 0 && gEmitterCooldown == 0) {
        gEmitterX = x;
        gEmitterY = y;
        gEmitterZ = z;
        gEmitterFloorY = floor;
        gParticleDef = gParticleDefs[type];
        gEmitterBursts = 0;
        gParticleQuad[0].v.ob[0] = -gParticleDef->size;
        gParticleQuad[0].v.ob[1] = gParticleDef->size;
        gParticleQuad[0].v.tc[0] = 0;
        gParticleQuad[0].v.tc[1] = 0;
        gParticleQuad[0].v.cn[0] = gParticleDef->r;
        gParticleQuad[0].v.cn[1] = gParticleDef->g;
        gParticleQuad[0].v.cn[2] = gParticleDef->b;
        gParticleQuad[0].v.cn[3] = gParticleDef->a;
        gParticleQuad[1].v.ob[0] = gParticleDef->size;
        gParticleQuad[1].v.ob[1] = gParticleDef->size;
        gParticleQuad[1].v.tc[0] = 0;
        gParticleQuad[1].v.tc[1] = gParticleDef->texH << 5;
        gParticleQuad[1].v.cn[0] = gParticleDef->r;
        gParticleQuad[1].v.cn[1] = gParticleDef->g;
        gParticleQuad[1].v.cn[2] = gParticleDef->b;
        gParticleQuad[1].v.cn[3] = gParticleDef->a;
        gParticleQuad[2].v.ob[0] = gParticleDef->size;
        gParticleQuad[2].v.ob[1] = -gParticleDef->size;
        gParticleQuad[2].v.tc[0] = gParticleDef->texW << 5;
        gParticleQuad[2].v.tc[1] = gParticleDef->texH << 5;
        gParticleQuad[2].v.cn[0] = gParticleDef->r;
        gParticleQuad[2].v.cn[1] = gParticleDef->g;
        gParticleQuad[2].v.cn[2] = gParticleDef->b;
        gParticleQuad[2].v.cn[3] = gParticleDef->a;
        gParticleQuad[3].v.ob[0] = -gParticleDef->size;
        gParticleQuad[3].v.ob[1] = -gParticleDef->size;
        gParticleQuad[3].v.tc[0] = 0;
        gParticleQuad[3].v.tc[1] = gParticleDef->texH << 5;
        gParticleQuad[3].v.cn[0] = gParticleDef->r;
        gParticleQuad[3].v.cn[1] = gParticleDef->g;
        gParticleQuad[3].v.cn[2] = gParticleDef->b;
        gParticleQuad[3].v.cn[3] = gParticleDef->a;
        switch (gParticleDef->siz) {
            case 0:
                bpp = 4;
                break;
            case 1:
                bpp = 8;
                break;
            case 2:
                bpp = 16;
                break;
            case 3:
                bpp = 32;
                break;
        }
        gParticleTexSize = gParticleDef->texW * gParticleDef->texH * bpp / 8;
        gParticleTexCount = 0;
        gEmitterActive = 1;
        return 1;
    }
    return 0;
}

/* Bursts, birth/loop/death sequences, gravity, bounce (0x802886A0). */
void ptcl_update(void) {
    s32 n;
    s32 slot;
    u8 full;
    u8 found;
    s16 dy;
    f32 mf[4][4];
    f32 x;
    f32 y;
    f32 z;
    f32 rx;
    f32 ry;
    f32 rz;
    s32 angle;
    s16 len;

    n = 0;
    slot = 0;
    full = 0;
    if (gEmitterActive == 0) {
        if (gEmitterCooldown != 0) {
            gEmitterCooldown--;
        }
    } else {
        gEmitterActive = 0;
        if (gEmitterBursts < gParticleDef->bursts) {
            while (!full && n < gParticleDef->count) {
                found = 0;
                while (!found && slot < 50) {
                    if (gParticles[slot].active == 0) {
                        found = 1;
                    } else {
                        slot++;
                    }
                }
                if (found) {
                    PORT_ISPAWN(PORT_ITAG_PARTICLE, slot);
                    gParticles[slot].active = 1;
                    gParticles[slot].pos[0] = gEmitterX;
                    gParticles[slot].pos[1] = gEmitterY;
                    gParticles[slot].pos[2] = gEmitterZ;
                    angle = util_rand_range(0, gParticleDef->spread);
                    guRotateF(mf, angle, 0.0f, 0.0f, 1.0f);
                    guMtxXFMF(mf, 0.0f,
                              (util_rand_range(-gParticleDef->speedVar, gParticleDef->speedVar) + gParticleDef->speed) / 32.0f,
                              0.0f, &x, &y, &z);
                    angle = util_rand_range(0, 359);
                    guRotateF(mf, angle, 0.0f, 1.0f, 0.0f);
                    guMtxXFMF(mf, x, y, z, &rx, &ry, &rz);
                    gParticles[slot].vel[0] = rx * 32.0f;
                    gParticles[slot].vel[1] = ry * 32.0f;
                    gParticles[slot].vel[2] = rz * 32.0f;
                    gParticles[slot].state = 0;
                    gParticles[slot].frame = 0;
                    gParticles[slot].fallTime = 0;
                    gParticles[slot].age = 0;
                    gParticles[slot].life =
                        util_rand_range(-gParticleDef->lifeVar, gParticleDef->lifeVar) + gParticleDef->life;
                    slot++;
                } else {
                    full = 1;
                }
                n++;
            }
            gEmitterBursts++;
        }
        for (n = 0; n < 50; n++) {
            if (gParticles[n].active) {
                gEmitterActive = 1;
                switch (gParticles[n].state) {
                    case 0:
                        len = *gParticleDef->seqBirth;
                        if (gParticles[n].frame == len) {
                            gParticles[n].state = 1;
                            gParticles[n].frame = 0;
                        }
                        break;
                    case 1:
                        len = *gParticleDef->seqLoop;
                        if (gParticles[n].frame == len) {
                            gParticles[n].frame = 0;
                        }
                        break;
                    case 3:
                        len = *gParticleDef->seqDeath;
                        if (gParticles[n].frame == len) {
                            gParticles[n].active = 0;
                        }
                        break;
                }
                if (gParticles[n].age == gParticles[n].life) {
                    gParticles[n].state = 3;
                    gParticles[n].frame = 0;
                }
            }
        }
        for (n = 0; n < 50; n++) {
            if (gParticles[n].active) {
                gParticles[n].pos[0] += gParticles[n].vel[0];
                gParticles[n].pos[2] += gParticles[n].vel[2];
                dy = gParticles[n].vel[1] + gParticles[n].fallTime * gParticleDef->gravity;
                gParticles[n].pos[1] += dy;
                if (gParticles[n].pos[1] < gEmitterFloorY) {
                    gParticles[n].pos[1] = gEmitterFloorY;
                    gParticles[n].vel[1] = -(dy * 16 / gParticleDef->bounce);
                    gParticles[n].fallTime = 0;
                }
            }
            gParticles[n].fallTime++;
            gParticles[n].frame++;
            gParticles[n].age++;
        }
        if (gEmitterActive == 0) {
            gEmitterCooldown = 3;
        }
    }
}

/* The particles as camera-facing quads, sorted by texture (0x80288DF0). */
void ptcl_draw(Gfx **gdl, u8 buf) {
    s32 n;
    s32 i;
    Gfx *gfx;
    f32 mf1[4][4];
    f32 mf2[4][4];
    s32 dist;
    s32 dy;
    s32 ang;
    s32 ratio;
    s32 absRatio;
#ifdef NON_MATCHING
    s32 texId = 0; /* states without a case leave it indeterminate in the original */
#else
    s32 texId;
#endif
    SortEntry entries[50];
    s32 count;
    s32 lastTex;

    gfx = *gdl;
    count = 0;
    lastTex = -1;
    if (gEmitterActive) {
        ptcl_set_render_state(&gfx);
        for (n = 0; n < 50; n++) {
            if (gParticles[n].active) {
                switch (gParticles[n].state) {
                    case 0:
                        texId = gParticleDef->seqBirth[gParticles[n].frame];
                        break;
                    case 1:
                        texId = gParticleDef->seqLoop[gParticles[n].frame];
                        break;
                    case 3:
                        texId = gParticleDef->seqDeath[gParticles[n].frame];
                        break;
                }
                entries[count].key = n;
                entries[count].val = ptcl_get_texture(texId);
                count++;
            }
        }
        /* sort by texture so each one is loaded once */
        ptcl_sort_by_texture(entries, count);
        for (i = 0; i < count; i++) {
            n = entries[i].key;
            if (entries[i].val != lastTex) {
                lastTex = entries[i].val;
                gDPPipeSync(gfx++);
                switch (gParticleDef->siz) {
                    case 0:
                        gDPLoadTextureBlock_4b(gfx++, OS_PHYSICAL_TO_K0(lastTex), gParticleDef->fmt, gParticleDef->texW,
                                               gParticleDef->texH, 0, G_TX_CLAMP, G_TX_CLAMP, G_TX_NOMASK,
                                               G_TX_NOMASK, G_TX_NOLOD, G_TX_NOLOD);
                        break;
                    case 1:
                        gDPLoadTextureBlock(gfx++, OS_PHYSICAL_TO_K0(lastTex), gParticleDef->fmt, G_IM_SIZ_8b,
                                            gParticleDef->texW, gParticleDef->texH, 0, G_TX_CLAMP, G_TX_CLAMP,
                                            G_TX_NOMASK, G_TX_NOMASK, G_TX_NOLOD, G_TX_NOLOD);
                        break;
                    case 2:
                        gDPLoadTextureBlock(gfx++, OS_PHYSICAL_TO_K0(lastTex), gParticleDef->fmt, G_IM_SIZ_16b,
                                            gParticleDef->texW, gParticleDef->texH, 0, G_TX_CLAMP, G_TX_CLAMP,
                                            G_TX_NOMASK, G_TX_NOMASK, G_TX_NOLOD, G_TX_NOLOD);
                        break;
                    case 3:
                        gDPLoadTextureBlock(gfx++, OS_PHYSICAL_TO_K0(lastTex), gParticleDef->fmt, G_IM_SIZ_32b,
                                            gParticleDef->texW, gParticleDef->texH, 0, G_TX_CLAMP, G_TX_CLAMP,
                                            G_TX_NOMASK, G_TX_NOMASK, G_TX_NOLOD, G_TX_NOLOD);
                        break;
                }
            }
            /* tilt the billboard toward the camera's height, then face its yaw */
            dist = util_dist3d(gCamEyeX >> 11, gCamEyeY >> 11, gCamEyeZ >> 11, gParticles[n].pos[0],
                                 gParticles[n].pos[1], gParticles[n].pos[2]);
            if (dist == 0) {
                dist = 1;
            }
            dy = (gCamEyeY >> 11) - gParticles[n].pos[1];
            dy <<= 16;
            ratio = dy / dist;
            if (ratio < 0) {
                absRatio = -ratio;
            } else {
                absRatio = ratio;
            }
            ang = math_asin_c(absRatio);
            if (ratio > 0) {
                ang = -ang;
            }
            guRotateF(mf1, (f32) ang / 65536.0 * 360.0, 1.0f, 0.0f, 0.0f);
            guRotateF(mf2, (f32) gCamYaw / 4095.0 * 360.0, 0.0f, 1.0f, 0.0f);
            guMtxCatF(mf1, mf2, mf1);
            guTranslateF(mf2, gParticles[n].pos[0] / 32.0f, gParticles[n].pos[1] / 32.0f,
                         gParticles[n].pos[2] / 32.0f);
            guMtxCatF(mf1, mf2, mf1);
            guMtxF2L(mf1, &gParticleMtx[buf][n]);
            PORT_ITAG(gfx, PORT_ITAG_PARTICLE, n);
            gSPMatrix(gfx++, osVirtualToPhysical(&gParticleMtx[buf][n]), G_MTX_MODELVIEW | G_MTX_MUL | G_MTX_PUSH);
            gSPVertex(gfx++, osVirtualToPhysical(gParticleQuad), 4, 0);
            gSP1Triangle(gfx++, 0, 1, 2, 0);
            gSP1Triangle(gfx++, 0, 2, 3, 0);
            gSPPopMatrix(gfx++, G_MTX_MODELVIEW);
        }
        PORT_ITAG(gfx, PORT_ITAG_NONE, 0);
        *gdl = gfx;
    }
}

/* The particles' render state (0x80289EF4). */
void ptcl_set_render_state(Gfx **gdl) {
    Gfx *gfx = *gdl;

    gDPPipeSync(gfx++);
    gSPClearGeometryMode(gfx++, -1);
    gSPSetGeometryMode(gfx++, G_ZBUFFER | G_SHADE | G_SHADING_SMOOTH);
    gDPSetRenderMode(gfx++, G_RM_ZB_XLU_SURF, G_RM_ZB_XLU_SURF2);
    switch (gParticleDef->fmt) {
        case G_IM_FMT_RGBA:
            gDPSetCombineMode(gfx++, G_CC_MODULATERGBA, G_CC_MODULATERGBA);
            break;
        case G_IM_FMT_IA:
            gDPSetCombineMode(gfx++, G_CC_MODULATEIA, G_CC_MODULATEIA);
            break;
        case G_IM_FMT_I:
            gDPSetCombineMode(gfx++, G_CC_MODULATEI, G_CC_MODULATEI);
            break;
    }
    gSPTexture(gfx++, 0xFFFF, 0xFFFF, 0, G_TX_RENDERTILE, G_ON);
    *gdl = gfx;
}

/* Texture id to cache slot (loads it on a miss) (0x8028A0A0).
 * Returns the physical address of texture `id`, loading it into the cache if needed. */
u32 ptcl_get_texture(s16 id) {
    u8 found;
    s32 i;
    u8 *p;

    found = 0;
    i = 0;
    while (!found && i < gParticleTexCount) {
        if (gParticleTexIds[i] == id) {
            found = 1;
        } else {
            i++;
        }
    }
    if (found) {
        return osVirtualToPhysical(i * gParticleTexSize + gParticleTexCache);
    }
    p = gParticleTexCount * gParticleTexSize + gParticleTexCache;
    gParticleTexIds[gParticleTexCount] = id;
    gParticleTexCount++;
    tex_queue_load(id, p, 0);
    return osVirtualToPhysical(p);
}

/* Shell sort of the draw list by texture (0x8028A1D0).
 * Shell sort (Knuth gaps) by val, ascending. */
void ptcl_sort_by_texture(SortEntry *a, s32 n) {
    s32 i;
    s32 j;
    s32 h;
    SortEntry tmp;

    for (h = 1; h <= n / 9; h = h * 3 + 1) {
    }
    for (; h > 0; h /= 3) {
        for (i = h; i < n; i++) {
            tmp.key = a[i].key;
            tmp.val = a[i].val;
            j = i;
            while (j >= h && a[j - h].val > tmp.val) {
                a[j].key = a[j - h].key;
                a[j].val = a[j - h].val;
                j -= h;
            }
            a[j].key = tmp.key;
            a[j].val = tmp.val;
        }
    }
}
