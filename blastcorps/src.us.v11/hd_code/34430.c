#include "common.h"
#include <ultra64.h>
#ifndef NON_MATCHING /* legacy declarations */
/* Matching build: IDO compiled the matched code here against older
 * declarations of these, which the file keeps; the NON_MATCHING build
 * uses game/game.h's. */
#define LEGACY_gDepthBufferPhys
#endif /* legacy declarations */
#include "game/game.h"
/* Views: this file reads these shared variables (game/variables.h) as other types. */
#ifdef NON_MATCHING
#define gDepthBufferPhys (*(void * N64P *) &gDepthBufferPhys)
#endif
/* end of views */

/* mb.c (this file's .bss starts at 0x8036CB60) */

/* KSEG0 address to physical, as the game spells it */
#define PHYS(x) ((u32) (x) & 0x1FFFFFFF)

#define MB_ASSERT(EX, line) \
    if (!(EX)) debug_printf("\n --- ASSERTION FAULT - %s - %s, line %d\n\n", #EX, "mb.c", line)
/* The file's second assert string has a bell */
#define MB_ASSERT_BELL(EX, line) \
    if (!(EX)) debug_printf("\n\a --- ASSERTION FAULT - %s - %s, line %d\n\n", #EX, "mb.c", line)

/* One recorded sample: three 16.16 values and a position in 1/32 units */
typedef struct {
    f32 a;
    f32 b;
    f32 c;
    f32 x;
    f32 y;
    f32 z;
} MbSample;

/* SetOtherMode_L values swapped when copying a display list */
typedef struct {
    u32 from;
    u32 to;
} MbRemap;

extern MbSample gMbSamples[11]; /* ring of samples */
extern s32 gMbHead;         /* ring head */
extern s32 gMbTail;         /* ring tail */
extern Mtx gMbCopyMtx[][10];   /* view matrices per slot */
extern u8 * N64P gMbBuffer;         /* 0x5460-byte buffer */
extern u8 gMbState;
extern s32 gMbCopies;
extern MbRemap gMbRenderModeRemap[3];
extern f32 gMbViewAngle;         /* view angle, degrees */
extern s32 gMbMaxCopies;
extern f32 gMbSpacing;
extern Vtx gMbStripVtx[];       /* 6 x 4 grid of view points */
extern Gfx gMbViewDl[40];     /* display list for the view */
extern Mtx gMbProjMtx;         /* its projection */
extern Mtx gMbLookAtMtx;         /* its look-at */
extern Vp gMbViewport;
#ifndef NON_MATCHING
extern void *gDepthBufferPhys; /* depth buffer */
#endif

s32 mb_get_samples(s32 n, s32 *a, s32 *b);
void mb_draw_strip(Gfx **gfxp, s32 offset, s32 v);
s32 mb_mtx4_invert(f32 out[4][4], f32 in[4][4]);

/* Copies a display list to the heap, remapping render modes (0x80278BF0).
 * Copies a display list to the heap, dropping G_ENDDLs and remapping render modes */
void mb_copy_dl(Gfx *src, Gfx *end, Gfx * N64P *dstp) {
    Gfx *dst;
    s8 c;
    u32 v;
    u8 found;
    s32 i;
    u32 w1;

    *dstp = (Gfx *) gHeapPtr;
    dst = *dstp;
    gHeapPtr += (end - src) * sizeof(Gfx) - 16;
    while (src != end) {
        switch (c = src->words.w0 >> 24) {
            case (s8) G_ENDDL:
                src++;
                break;
            case (s8) G_SETOTHERMODE_L:
                if ((v = (src->words.w0 >> 8) & 0xFFFF) == 3) {
                    dst->words.w0 = src->words.w0;
                    w1 = src->words.w1;
                    i = 0;
                    found = 0;
                    do {
                        if (gMbRenderModeRemap[i].from == w1) {
                            found = 1;
                        } else {
                            i++;
                        }
                    } while (!found && i < 3);
                    MB_ASSERT(found, 157);
                    dst->words.w1 = gMbRenderModeRemap[i].to;
                    dst++, src++;
                } else {
                    *dst++ = *src++;
                }
                break;
            default:
                *dst++ = *src++;
                break;
        }
    }
    gSPEndDisplayList(dst++);
}

#ifdef PORT_HOST
/* mb_init passes the heap pointer's value, not its address, to the
 * round-up helper (an original slip): it "rounds" whatever s32 the heap holds
 * there.  On the N64 that word is big-endian bytes of the previous user (e.g.
 * a text vertex's x/y); do the same arithmetic on those bytes (port_n64_byte)
 * and put the result back where the N64 bytes live in the host layout. */
u8 port_n64_byte(const void *p);
void port_n64_store_byte(void *p, u8 v);

static void port_round_stale(u8 *p, s32 align) {
    s32 v = (port_n64_byte(p) << 24) | (port_n64_byte(p + 1) << 16) | (port_n64_byte(p + 2) << 8) | port_n64_byte(p + 3);
    s32 k;

    align_up(&v, align);
    for (k = 0; k < 4; k++) {
        port_n64_store_byte(p + k, (u8) (v >> (24 - 8 * k)));
    }
}
#define ROUND_HEAP_WORD(p, align) port_round_stale((u8 *) (p), align)
#else
#define ROUND_HEAP_WORD(p, align) align_up((s32 *) (p), align)
#endif

/* The motion blur buffer, state reset (0x80278E3C).
 * Allocates the buffers and resets the state */
void mb_init(void) {
    ROUND_HEAP_WORD(gHeapPtr, 0x40);
    gMbBuffer = gHeapPtr;
    ROUND_HEAP_WORD(gHeapPtr = gHeapPtr + 0x5460, 8);
    gMbState = 0;
    gMbHead = 0;
    gMbTail = 0;
    gMbCopies = 0;
    gTurboActive = 0;
}

/* N copies, spacing, view angle (0x80278EB0).
 * Sets up the view: count n (at most 10), scale (at most 1), and a view angle from the latest sample (25 degrees if none); fills a 6x4 grid of points 100 units out */
void mb_start(s32 n, f32 scale, s32 arg2) {
    f32 mf1[4][4];
    f32 mf2[4][4];
    f32 p[3];
    f32 q[3];
    f32 ang;
    s32 i;
    s32 k = 0;
    f32 step;
    f32 yaw;
    u8 ok;
    s32 i0;
    s32 i1;
    s32 r;
    s32 pad;
    s32 v;

    if (n >= 11) {
        n = 10;
    }
    if (scale > 1.0f) {
        scale = 1.0f;
    }
    gMbMaxCopies = n;
    gMbSpacing = scale;
    gMbCopies = 0;
    gMbState = 1;
    ok = mb_get_samples(1, &i0, &i1);
    if (ok) {
        r = util_dist3d(gMbSamples[i0].a, gMbSamples[i0].b, gMbSamples[i0].c, gMbSamples[i0].x,
                          gMbSamples[i0].y, gMbSamples[i0].z);
        if (r < 1.0) {
            r = 1;
        }
        v = math_atan_c((arg2 <<= 16) / r);
        gMbViewAngle = (f32) v / 65536.0 * 360.0;
    } else {
        gMbViewAngle = 25.0f;
    }
    ang = gMbViewAngle / 2.0;
    step = gMbViewAngle / 6.0;
    yaw = ang * 1.3333334f;
    for (i = 0; i < 6; i++) {
        guRotateF(mf1, ang, 1.0f, 0.0f, 0.0f);
        guMtxXFMF(mf1, 0.0f, 0.0f, -100.0f, &p[0], &p[1], &p[2]);
        guRotateF(mf2, yaw, 0.0f, 1.0f, 0.0f);
        guMtxXFMF(mf2, p[0], p[1], p[2], &q[0], &q[1], &q[2]);
        gMbStripVtx[k].v.ob[0] = q[0];
        gMbStripVtx[k].v.ob[1] = q[1];
        gMbStripVtx[k].v.ob[2] = q[2];
        guRotateF(mf2, -yaw, 0.0f, 1.0f, 0.0f);
        guMtxXFMF(mf2, p[0], p[1], p[2], &q[0], &q[1], &q[2]);
        gMbStripVtx[k + 3].v.ob[0] = q[0];
        gMbStripVtx[k + 3].v.ob[1] = q[1];
        gMbStripVtx[k + 3].v.ob[2] = q[2];
        guRotateF(mf1, ang - step, 1.0f, 0.0f, 0.0f);
        guMtxXFMF(mf1, 0.0f, 0.0f, -100.0f, &p[0], &p[1], &p[2]);
        guRotateF(mf2, yaw, 0.0f, 1.0f, 0.0f);
        guMtxXFMF(mf2, p[0], p[1], p[2], &q[0], &q[1], &q[2]);
        gMbStripVtx[k + 1].v.ob[0] = q[0];
        gMbStripVtx[k + 1].v.ob[1] = q[1];
        gMbStripVtx[k + 1].v.ob[2] = q[2];
        guRotateF(mf2, -yaw, 0.0f, 1.0f, 0.0f);
        guMtxXFMF(mf2, p[0], p[1], p[2], &q[0], &q[1], &q[2]);
        gMbStripVtx[k + 2].v.ob[0] = q[0];
        gMbStripVtx[k + 2].v.ob[1] = q[1];
        gMbStripVtx[k + 2].v.ob[2] = q[2];
        k += 4;
        ang -= step;
    }
}

/* The blur's fade-out (unless gTurboActive holds it) (0x802794A4). */
void mb_stop(void) {
    if (!gTurboActive) {
        if (gMbCopies < 2) {
            gMbState = 0;
        } else {
            gMbState = 2;
        }
    }
}

/* Motion blur off at once (0x802794E4). */
void mb_cancel(void) {
    gMbState = 0;
}

/* 1 while the motion blur is on (0x802794F0). */
u8 mb_is_active(void) {
    s32 unused;

    return !(gMbState == 0);
}

/* The player's position and the camera eye into the ring (0x80279514).
 * Pushes a sample onto the ring, dropping the oldest when full */
void mb_push_sample(s32 x, s32 y, s32 z, s32 a, s32 b, s32 c) {
    gMbSamples[gMbHead].a = (f32) a / 65536.0;
    gMbSamples[gMbHead].b = (f32) b / 65536.0;
    gMbSamples[gMbHead].c = (f32) c / 65536.0;
    gMbSamples[gMbHead].x = x / 32.0f;
    gMbSamples[gMbHead].y = y / 32.0f;
    gMbSamples[gMbHead].z = z / 32.0f;
    gMbHead++;
    if (gMbHead == 11) {
        gMbHead = 0;
    }
    if (gMbHead == gMbTail) {
        gMbTail++;
        if (gMbTail == 11) {
            gMbTail = 0;
        }
    }
}

/* Ring indices n and n + 1 back (0x802796D8).
 * Fetches the ring indices n and n + 1 steps back from the head; 0 if not there */
s32 mb_get_samples(s32 n, s32 *a, s32 *b) {
    s32 idx = gMbHead;

    while (n--) {
        if (idx) {
            idx--;
        } else {
            idx = 10;
        }
        if (idx == gMbTail) {
            return 0;
        }
    }
    *a = idx;
    if (idx) {
        idx--;
    } else {
        idx = 10;
    }
    *b = idx;
    return 1;
}

/* The player's vehicle rendered into the 120x90 texture (0x80279778).
 * Renders the 120x90 view into the buffer: clears it, sets the projection from the view angle and looks from (a, b, c) (16.16) at (x, y, z) (1/32), then calls dl */
void mb_render_view(s32 x, s32 y, s32 z, s32 a, s32 b, s32 c, void *dl, void *seg6, void *seg7, s32 alpha) {
    Gfx *gdl;
    u16 perspNorm;
    s32 pad;
    f32 tx;
    f32 ty;
    f32 tz;
    f32 ex;
    f32 ey;
    f32 ez;
    s32 pad2[8];
    f32 dx;
    f32 dz;

    if (gMbState) {
        gdl = gMbViewDl;
        gSPViewport(gdl++, PHYS(&gMbViewport));
        gSPClearGeometryMode(gdl++, 0xFFFFFFFF);
        gSPSegment(gdl++, 0, 0);
        gSPSegment(gdl++, 6, PHYS(seg6));
        gSPSegment(gdl++, 7, PHYS(seg7));
        gDPPipeSync(gdl++);
        gDPSetScissor(gdl++, G_SC_NON_INTERLACE, 0, 0, 120, 90);
        gDPSetColorDither(gdl++, G_CD_DISABLE);
        gDPSetCycleType(gdl++, G_CYC_FILL);
        gSPClearGeometryMode(gdl++, G_ZBUFFER);
        gDPSetDepthImage(gdl++, gDepthBufferPhys);
        gDPSetColorImage(gdl++, G_IM_FMT_RGBA, G_IM_SIZ_16b, 120, gDepthBufferPhys);
        gDPSetFillColor(gdl++, 0xFFFCFFFC);
        gDPFillRectangle(gdl++, 0, 0, 119, 89);
        gDPPipeSync(gdl++);
        gDPSetColorImage(gdl++, G_IM_FMT_RGBA, G_IM_SIZ_16b, 120, PHYS(gMbBuffer));
        gDPSetFillColor(gdl++, 0);
        gDPFillRectangle(gdl++, 0, 0, 119, 89);
        gDPPipeSync(gdl++);
        guPerspective(&gMbProjMtx, &perspNorm, gMbViewAngle, 1.3333334f, 100.0f, 5000.0f, 1.0f);
        gSPMatrix(gdl++, PHYS(&gMbProjMtx), G_MTX_PROJECTION | G_MTX_LOAD | G_MTX_NOPUSH);
        gImmp1(gdl++, G_RDPHALF_1, perspNorm); /* old F3D gSPPerspNormalize */
        tx = x / 32.0f;
        ty = y / 32.0f;
        tz = z / 32.0f;
        ex = (f32) a / 65536.0;
        ey = (f32) b / 65536.0;
        ez = (f32) c / 65536.0;
        dx = ex - tx;
        if (dx < 0.0) {
            dx = 0.0 - dx;
        }
        dz = ez - tz;
        if (dz < 0.0) {
            dz = 0.0 - dz;
        }
        if (dx > 0.5 || dz > 0.5) {
            guLookAt(&gMbLookAtMtx, ex, ey, ez, tx, ty, tz, 0.0f, 1.0f, 0.0f);
        } else {
            guLookAt(&gMbLookAtMtx, ex + 2.0, ey, ez + 2.0, tx, ty, tz, 0.0f, 1.0f, 0.0f);
        }
        gSPMatrix(gdl++, PHYS(&gMbLookAtMtx), G_MTX_MODELVIEW | G_MTX_LOAD | G_MTX_NOPUSH);
        gDPSetEnvColor(gdl++, 0, 0, 0, alpha);
        gSPDisplayList(gdl++, PHYS(dl));
        gSPEndDisplayList(gdl++);
        rsp_send_task((u64 *) gMbViewDl, gdl - gMbViewDl, 2, 0, 0x54D, 0);
    }
}

/* Fading copies along the recorded path (0x80279EE8).
 * Draws up to gMbCopies trailing copies of the view, fading out, along the recorded path */
void mb_draw(Gfx **gfxp, s32 arg1, u8 slot) {
    Gfx *gdl = *gfxp;
    u8 flag;
    s32 off = 0;
    s32 vi = 0;
    f32 mf[4][4];
    f32 inv[4][4];
    s32 i;
    u8 b;
    u8 a;
    s32 n = 1;
    s32 i0;
    s32 i1;
    f32 t = 0.0f;
    f32 tx;
    f32 ty;
    f32 tz;
    f32 ea;
    f32 eb;
    f32 ec;
    f32 dx;
    f32 dz;

    switch (gMbState) {
        case 1:
            gMbCopies++;
            if (gMbCopies == gMbMaxCopies) {
                gMbState = 3;
            }
            break;
        case 2:
            gMbCopies--;
            if (gMbCopies == 0) {
                gMbState = 0;
            }
            break;
    }
    if (gMbState) {
        gDPPipeSync(gdl++);
        gDPSetColorDither(gdl++, G_CD_NOISE);
        gDPSetAlphaDither(gdl++, G_AD_NOTPATTERN);
        gSPClearGeometryMode(gdl++, 0xFFFFFFFF);
        gSPSetGeometryMode(gdl++, G_SHADE | G_SHADING_SMOOTH);
        gDPSetCycleType(gdl++, G_CYC_1CYCLE);
        gDPSetRenderMode(gdl++, G_RM_XLU_SURF, G_RM_XLU_SURF2);
        gDPSetCombine(gdl++, 0x119623, 0xFF2FFFFF);
        gDPSetTextureFilter(gdl++, G_TF_BILERP);
        gSPTexture(gdl++, 0xFFFF, 0xFFFF, 0, G_TX_RENDERTILE, G_ON);
        a = 30 / (gMbMaxCopies - 1);
        b = 40 - a * (gMbMaxCopies - gMbCopies);
        for (i = 0; i < gMbCopies; i++) {
            gDPSetPrimColor(gdl++, 0, 0, 0xFF, 0xFF, 0xFF, b);
            flag = mb_get_samples(n, &i0, &i1);
            if (!flag) {
                break;
            }
            tx = (gMbSamples[i1].x - gMbSamples[i0].x) * t + gMbSamples[i0].x;
            ty = (gMbSamples[i1].y - gMbSamples[i0].y) * t + gMbSamples[i0].y;
            tz = (gMbSamples[i1].z - gMbSamples[i0].z) * t + gMbSamples[i0].z;
            ea = (gMbSamples[i1].a - gMbSamples[i0].a) * t + gMbSamples[i0].a;
            eb = (gMbSamples[i1].b - gMbSamples[i0].b) * t + gMbSamples[i0].b;
            ec = (gMbSamples[i1].c - gMbSamples[i0].c) * t + gMbSamples[i0].c;
            t += gMbSpacing;
            if (t >= 1.0) {
                n++;
                t = 0.0f;
            }
            dx = ea - tx;
            if (dx < 0.0) {
                dx = 0.0 - dx;
            }
            dz = ec - tz;
            if (dz < 0.0) {
                dz = 0.0 - dz;
            }
            if (dx > 0.5 || dz > 0.5) {
                guLookAtF(mf, ea, eb, ec, tx, ty, tz, 0.0f, 1.0f, 0.0f);
            } else {
                guLookAtF(mf, ea + 2.0, eb, ec + 2.0, tx, ty, tz, 0.0f, 1.0f, 0.0f);
            }
            flag = mb_mtx4_invert(inv, mf);
            MB_ASSERT_BELL(flag, 547);
            guMtxF2L(inv, &gMbCopyMtx[slot][i]);
            gSPMatrix(gdl++, PHYS(&gMbCopyMtx[slot][i]), G_MTX_MODELVIEW | G_MTX_MUL | G_MTX_PUSH);
            gSPVertex(gdl++, PHYS(gMbStripVtx), 16, 0);
            vi = 0;
            off = 0;
            mb_draw_strip(&gdl, off, vi);
            off += 0xE10, vi += 4;
            mb_draw_strip(&gdl, off, vi);
            off += 0xE10, vi += 4;
            mb_draw_strip(&gdl, off, vi);
            off += 0xE10, vi += 4;
            mb_draw_strip(&gdl, off, vi);
            off += 0xE10, vi += 4;
            gSPVertex(gdl++, PHYS(&gMbStripVtx[16]), 8, 0);
            vi = 0;
            mb_draw_strip(&gdl, off, vi);
            off += 0xE10, vi += 4;
            mb_draw_strip(&gdl, off, vi);
            off += 0xE10, vi += 4;
            gSPPopMatrix(gdl++, G_MTX_MODELVIEW);
            b -= a;
        }
        gDPPipeSync(gdl++);
        gDPSetColorDither(gdl++, G_CD_MAGICSQ);
        gDPPipeSync(gdl++);
        *gfxp = gdl;
    }
}

/* One 120x15 strip of the texture (0x8027A7DC).
 * Draws a 120x15 RGBA16 strip from the buffer at offset as two triangles from vertex v */
void mb_draw_strip(Gfx **gfxp, s32 offset, s32 v) {
    Gfx *gdl = *gfxp;
    s32 pad;

    gDPLoadTextureBlock(gdl++, gMbBuffer + offset - 0x80000000, G_IM_FMT_RGBA, G_IM_SIZ_16b, 120, 15, 0, G_TX_CLAMP,
                        G_TX_CLAMP, G_TX_NOMASK, G_TX_NOMASK, G_TX_NOLOD, G_TX_NOLOD);
    gSP1Triangle(gdl++, v, v + 1, v + 2, 0);
    gSP1Triangle(gdl++, v, v + 2, v + 3, 0);
    *gfxp = gdl;
}

/* 3x3 inverse helper: row order (0x8027AA04).
 * Swaps rows of a 3x3 matrix so the axes come out in order */
void mb_mtx3_reorder_rows(f32 m[][3], s32 a, s32 b, s32 c) {
    s32 i;
    s32 t;
    f32 tmp;

    if (a != 0) {
        if (b == 0) {
            for (i = 0; i < 3; i++) {
                tmp = m[b][i];
                m[b][i] = m[a][i];
                m[a][i] = tmp;
            }
            t = b;
            b = a;
            a = t;
        } else {
            for (i = 0; i < 3; i++) {
                tmp = m[c][i];
                m[c][i] = m[a][i];
                m[a][i] = tmp;
            }
            t = c;
            c = a;
            a = t;
        }
    }
    if (b != 1) {
        for (i = 0; i < 3; i++) {
            tmp = m[c][i];
            m[c][i] = m[b][i];
            m[b][i] = tmp;
        }
        t = c;
        c = b;
        b = t;
    }
}

/* 3x3 inverse helper: back substitution (0x8027AC00).
 * Finishes the 3x3 inversion for columns 1 and 2 (pivot rows q and r), then reorders the rows; 0 if singular */
s32 mb_mtx3_invert_finish(f32 m[3][3], f32 out[3][3], s32 p) {
    s32 q;
    s32 r;
    s32 a;
    s32 b;
    s32 i;
    f32 s;
    f32 aa;
    f32 ab;

    if (p == 0) {
        a = 1, b = 2;
    } else if (p == 1) {
        a = 0, b = 2;
    } else {
        a = 0, b = 1;
    }
    if ((aa = m[a][1]) < 0.0f) {
        aa = -aa;
    }
    if ((ab = m[b][1]) < 0.0f) {
        ab = -ab;
    }
    if (aa > ab) {
        q = a;
    } else {
        q = b;
    }
    if (q == a) {
        r = b;
    } else {
        r = a;
    }
    if (m[q][1] < 1e-8 && m[q][1] > -1e-8) {
        return 0;
    }
    s = 1.0 / m[q][1];
    m[q][1] = 1.0f;
    m[q][2] *= s;
    out[q][q] = s;
    out[q][p] *= s;
    for (i = 0; i < 3; i++) {
        if (i != q) {
            s = -m[i][1];
            m[i][1] = 0.0f;
            m[i][2] += s * m[q][2];
            out[i][q] = out[q][q] * s;
            out[i][p] += s * out[q][p];
        }
    }
    if (m[r][2] < 1e-8 && m[r][2] > -1e-8) {
        return 0;
    }
    s = 1.0 / m[r][2];
    m[r][2] = 1.0f;
    out[r][r] = s;
    out[r][p] *= s;
    out[r][q] *= s;
    for (i = 0; i < 3; i++) {
        if (i != r) {
            s = -m[i][2];
            m[i][2] = 0.0f;
            out[i][p] += s * out[r][p];
            out[i][q] += s * out[r][q];
            out[i][r] += s * out[r][r];
        }
    }
    mb_mtx3_reorder_rows(out, p, q, r);
    return 1;
}

/* 3x3 inverse (0x8027B200).
 * Inverts a 3x3 matrix into out by elimination: pivots on the largest column-0 entry, then hands over to mb_mtx3_invert_finish; 0 if singular */
s32 mb_mtx3_invert(f32 m[3][3], f32 out[3][3]) {
    s32 i;
    s32 p;
    f32 s;
    f32 ax;
    f32 ay;
    f32 az;

    out[0][0] = out[1][1] = out[2][2] = 1.0f;
    out[0][1] = out[0][2] = out[1][0] = out[1][2] = out[2][0] = out[2][1] = 0.0f;
    if ((ax = m[0][0]) < 0.0f) {
        ax = -ax;
    }
    if ((ay = m[1][0]) < 0.0f) {
        ay = -ay;
    }
    if ((az = m[2][0]) < 0.0f) {
        az = -az;
    }
    if (ax > ay) {
        if (ax > az) {
            p = 0;
        } else {
            p = 2;
        }
    } else if (ay > az) {
        p = 1;
    } else {
        p = 2;
    }
    if (m[p][0] < 1e-8 && m[p][0] > -1e-8) {
        return 0;
    }
    s = 1.0 / m[p][0];
    m[p][0] = 1.0f;
    m[p][1] *= s;
    m[p][2] *= s;
    out[p][p] = s;
    for (i = 0; i < 3; i++) {
        if (i != p) {
            s = -m[i][0];
            m[i][0] = 0.0f;
            m[i][1] += s * m[p][1];
            m[i][2] += s * m[p][2];
            out[i][p] = out[p][p] * s;
        }
    }
    if (!mb_mtx3_invert_finish(m, out, p)) {
        return 0;
    }
    return 1;
}

/* 4x4 inverse helper (0x8027B5D0).
 * Splits a 4x4 matrix into translation v, scale s and the rest (pivoting rows if m[3][3] is ~0) */
s32 mb_mtx4_split_projective(f32 m[4][4], f32 *v, f32 *s, s32 *row) {
    s32 i;
    s32 j;
    f32 tmp;
    f32 max;

    *row = -1;
    if (((m[3][3] > 0.0f) ? m[3][3] : -m[3][3]) < 1e-8) {
        max = 0.0f;
        for (i = 0; i < 4; i++) {
            if (m[i][3] > max) {
                *row = i;
                max = m[*row][3];
            } else if (m[i][3] < -max) {
                *row = i;
                max = -m[*row][3];
            }
        }
        if (*row < 0) {
            return 0;
        }
        for (j = 0; j < 4; j++) {
            tmp = m[3][j];
            m[3][j] = m[*row][j];
            m[*row][j] = tmp;
        }
    }
    v[0] = -m[0][3];
    v[1] = -m[1][3];
    v[2] = -m[2][3];
    *s = 1.0 / m[3][3];
    m[0][3] = m[1][3] = m[2][3] = 0.0f;
    m[3][3] = 1.0f;
    m[3][0] *= *s;
    m[3][1] *= *s;
    m[3][2] *= *s;
    for (i = 0; i < 3; i++) {
        m[0][i] += v[0] * m[3][i];
        m[1][i] += v[1] * m[3][i];
        m[2][i] += v[2] * m[3][i];
    }
    return 1;
}

/* 4x4 inverse (0x8027B87C).
 * Inverts a 4x4 matrix (general or affine); 0 if singular */
s32 mb_mtx4_invert(f32 out[4][4], f32 in[4][4]) {
    f32 m[4][4];
    s32 i;
    s32 j;
    s32 affine;
    f32 a[3][3];
    f32 inv[3][3];
    f32 s;
    f32 tmp;
    f32 v[4];
    f32 t[4];
    s32 row;
    s32 pad;

    for (i = 0; i < 4; i++) {
        for (j = 0; j < 4; j++) {
            m[i][j] = in[i][j];
            out[i][j] = 0.0f;
        }
    }
    out[0][0] = 1.0f;
    out[1][1] = 1.0f;
    out[2][2] = 1.0f;
    out[3][3] = 1.0f;
    affine = m[0][3] == 0.0 && m[1][3] == 0.0 && m[2][3] == 0.0 && m[3][3] == 1.0;
    if (!affine) {
        if (!mb_mtx4_split_projective(m, v, &s, &row)) {
            return 0;
        }
    }
    t[0] = m[3][0];
    t[1] = m[3][1];
    t[2] = m[3][2];
    for (i = 0; i < 3; i++) {
        for (j = 0; j < 3; j++) {
            a[i][j] = m[i][j];
        }
    }
    if (!mb_mtx3_invert(a, inv)) {
        return 0;
    }
    for (i = 0; i < 3; i++) {
        for (j = 0; j < 3; j++) {
            out[i][j] = inv[i][j];
        }
    }
    for (i = 0; i < 3; i++) {
        for (j = 0; j < 3; j++) {
            out[3][i] -= t[j] * inv[j][i];
        }
    }
    if (!affine) {
        for (i = 0; i < 4; i++) {
            out[i][3] += v[0] * out[i][0] + v[1] * out[i][1] + v[2] * out[i][2];
            out[i][3] *= s;
        }
        if (row >= 0) {
            for (i = 0; i < 4; i++) {
                tmp = out[i][3];
                out[i][3] = out[i][row];
                out[i][row] = tmp;
            }
        }
    }
    return 1;
}
