#include "common.h"
#include <ultra64.h>
#include "game/game.h"
/* Views: this file reads these shared variables (game/variables.h) as other types. */
#define gTaskDoneQueue ((u8 *) &gTaskDoneQueue)
#define gYieldBuffer ((u8 *) gYieldBuffer)
/* end of views */

/* FILE-WIDE FINDING: this file's functions save $ra via the 64-bit `sd`/`ld`
 * doubleword form, not the normal 32-bit `sw`/`lw` pair - the same signature
 * documented in full in hd_code/77E20.c's top-of-file note and the project
 * skill file, confirming (with no exceptions found) that this file is part
 * of the same large hand-written-assembly module. Any pragma below without
 * a more specific comment follows this convention; a few have their own
 * more specific non-ABI explanation where one was already worked out. */

#ifdef NON_MATCHING
/* Shared declarations for the NON_MATCHING (port) rewrites below. */
extern s16 gCullDelay;
extern u8 gCullQuadStack[];
extern u8 * N64P gCullQuadTop;
extern s16 * N64P gCullCellsEnd;
extern s16 gCullCells[];

/* Body shared by cull_restart_now / cull_restart: gCullDelay = value, reset
 * the gCullQuadTop cursor, fill both index lists with 0..n-1 for n =
 * gLevelCols * gLevelRows, point gCullCellsEnd past the gCullCells list and
 * terminate the gVisibleCells list with -1. The asm loops with `!=` on the
 * 32-bit product. The -1 is stored after gCullCellsEnd, so with n == 100 it
 * overwrites gCullCellsEnd's high half, as in the asm. */
#define RESET_INDEX_LISTS(value)                \
    {                                           \
        u32 n_;                                 \
        u32 i_;                                 \
                                                \
        gCullDelay = (value);                   \
        gCullQuadTop = gCullQuadStack;                \
        n_ = (u32) ((u16) gLevelCols) * gLevelRows;     \
        for (i_ = 0; i_ != n_; i_++) {          \
            gCullCells[i_] = i_;                \
            gVisibleCells[i_] = i_;                \
        }                                       \
        gCullCellsEnd = &gCullCells[n_];           \
        gVisibleCells[n_] = -1;                    \
    }
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
/* cull_restart(0).
 * Asm callers rely on preserved: initlevel keeps t0, f12, f14 (the asm
 * saves t0-t3). */
void cull_restart_now(void) {
    RESET_INDEX_LISTS(0);
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/5FD50/cull_restart_now.s")
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
/* Sets gCullDelay and resets the index lists (see RESET_INDEX_LISTS). */
void cull_restart(s32 arg0) {
    RESET_INDEX_LISTS(arg0);
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/5FD50/cull_restart.s")
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
void cull_walk_grid(s32, Gfx *, Vtx *, s32);

/* Skips gCullDelay frames: while the counter is nonzero it just counts
 * down; at zero it calls cull_walk_grid with the same four arguments.
 * Register note: cull_walk_grid clobbers s0-s7 (the asm here saves s0-s7,
 * gp and fp around it), and the asm passes f12/f14 through untouched. A
 * mixed N64 build would need a thunk saving the s-registers around the
 * asm callee; the native port (callee in C) doesn't. */
void cull_step_delayed(s32 arg0, Gfx *arg1, Vtx *arg2, s32 arg3) {
    if (gCullDelay == 0) {
        cull_walk_grid(arg0, arg1, arg2, arg3);
    } else {
        gCullDelay--;
    }
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/5FD50/cull_step_delayed.s")
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
/* The two list cursors cull_test_cells advances (the asm's t3 and s5). */
typedef struct {
    /* 0x0 */ s16 * N64P quads; /* {x, z, w, h} halfword quads still to test */
    /* 0x4 */ s16 * N64P cells; /* visible single cells (z * stride + x) */
} Io802A484C;
void cull_test_cells(Io802A484C *io, void *dataPtr, Vtx *v, s32 dataSize, s32 x, s32 z, u32 w, u32 h,
                   s32 stride, u8 *grid, u32 xs, u32 zs);

/* One step of the visibility walk over the level grid (header `lvl`: s16
 * columns/stride at +0, rows at +2, scales at +4/+6, grid offset at +0x48).
 * When the quad stack gCullQuadTop is empty (at gCullQuadStack): pushes the whole
 * grid {0, 0, columns, rows}, insertion-sorts the cells found visible so far
 * ([gCullCells, gCullCellsEnd)) into gVisibleCells (ascending s16, an equal value
 * goes after the existing ones), ends it with -1 and empties the cell list.
 * Then pops a quad and tests it with cull_test_cells (task data a1-a3), which
 * may push sub-quads or add a cell; the cursors are stored back.
 * Register convention: ABI inputs (lvl is an s32 as cull_step_delayed passes it);
 * the asm leaves s0-s7 changed (conventions.txt); its add/addi trap. */
void cull_walk_grid(s32 arg0, Gfx *dataPtr, Vtx *v, s32 dataSize) {
    u8 *lvl = (u8 *) N64_IPTR(arg0);
    s16 *q = (s16 *) gCullQuadTop;
    Io802A484C io;

    if (q == (s16 *) gCullQuadStack) {
        s16 *src;
        s16 *end;
        s16 *top;
        s16 *p;

        q[0] = 0;
        q[1] = 0;
        q[2] = *(s16 *) (lvl + 0);
        q[3] = *(s16 *) (lvl + 2);
        q += 4;
        end = gCullCellsEnd;
        top = (s16 *) gVisibleCells;
        for (src = gCullCells; src != end; src++, top++) {
            s16 val = *src;
            s16 cur;

            for (p = (s16 *) gVisibleCells; p != top; p++) {
                if (val < *p) {
                    break;
                }
            }
            if (p == top) {
                *top = val;
                continue;
            }
            cur = *p;
            *p = val;
            do {
                s16 next = p[1];

                p[1] = cur;
                p++;
                cur = next;
            } while (p != top);
        }
        *top = -1;
        gCullCellsEnd = gCullCells;
    }
    q -= 4;
    io.quads = q;
    io.cells = gCullCellsEnd;
    cull_test_cells(&io, dataPtr, v, dataSize, q[0], q[1], q[2], q[3], *(s16 *) (lvl + 0),
                  lvl + *(s32 *) (lvl + 0x48), *(s16 *) (lvl + 4), *(s16 *) (lvl + 6));
    gCullCellsEnd = io.cells;
    gCullQuadTop = (u8 *) io.quads;
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/5FD50/cull_walk_grid.s")
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
/* (Io802A484C is declared above cull_walk_grid.) */

s32 cull_grid_height_range(s32 x, s32 z, s32 w, s32 h, s32 stride, u8 *grid, s32 *maxOut);
void cull_box_corners(Vtx *v, u32 x, u32 z, u32 w, u32 d, u32 xs, u32 zs, s32 y0, s32 y1);
s32 cull_rsp_test(void *dataPtr, void *wb, s32 dataSize);

#define EMIT_QUAD(q, qx, qz, qw, qh)   \
    do {                               \
        if ((qw) != 0 && (qh) != 0) {  \
            (q)[0] = (qx);             \
            (q)[1] = (qz);             \
            (q)[2] = (qw);             \
            (q)[3] = (qh);             \
            (q) += 4;                  \
        }                              \
    } while (0)

/* Visibility test of the w x h block of level-grid cells at (x, z): builds
 * its bounding box (height range from cull_grid_height_range over the s16-pair grid,
 * corners from cull_box_corners into v at scales xs, zs) and runs the RSP test
 * task (cull_rsp_test with dataPtr / v / dataSize). If it reports visible: a
 * single cell is appended to io->cells as z * stride + x; a bigger block is
 * split into quadrants (w >> 1, w - (w >> 1)) x (h >> 1, h - (h >> 1)),
 * each non-empty one appended to io->quads as {x, z, w, h}.
 * The asm takes io's cursors in t3 / s5 (both in-out), dataPtr/v/dataSize
 * in a1/a2/a3, x/z/w/h/stride in s0-s4, grid in s6, xs/zs in s7/t8
 * (conventions.txt); it saves a0, t0-t2 and s0-s3. Its `add`/`sub` trap on
 * overflow, C doesn't; that can't happen in game: the caller passes s16
 * x/z/w/h/stride with w, h >= 1, where z * stride + x, x + (w >> 1) and
 * w - (w >> 1) all stay in range (only 32-bit or negative w/h inputs trap). */
void cull_test_cells(Io802A484C *io, void *dataPtr, Vtx *v, s32 dataSize, s32 x, s32 z, u32 w, u32 h,
                   s32 stride, u8 *grid, u32 xs, u32 zs) {
    s32 max;
    s32 min = cull_grid_height_range(x, z, w, h, stride, grid, &max);
    s16 *q;
    u32 w1;
    u32 h1;

    cull_box_corners(v, x, z, w, h, xs, zs, min, max);
    if (cull_rsp_test(dataPtr, v, dataSize) == 0) {
        return;
    }
    if (w == 1 && h == 1) {
        *io->cells++ = z * stride + x;
        return;
    }
    q = io->quads;
    w1 = w >> 1;
    h1 = h >> 1;
    EMIT_QUAD(q, x, z, w1, h1);
    EMIT_QUAD(q, x + w1, z, w - w1, h1);
    EMIT_QUAD(q, x, z + h1, w1, h - h1);
    EMIT_QUAD(q, x + w1, z + h1, w - w1, h - h1);
    io->quads = q;
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/5FD50/cull_test_cells.s")
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
/* Over an h x w block (starting at column x, row z) of a grid of s16 pairs
 * with `stride` pairs per row, returns the minimum of the first halves
 * (starting from 0x7FFF) and stores the maximum of the second halves
 * (starting from -0x8000) in *maxOut. The asm counts w and h down with
 * `!= 0` (both must be >= 1).
 * asm: x/z/w/h/stride/grid in s0/s1/s2/s3/s4/s6, min in v1, max in a0.
 * Asm callers rely on preserved: cull_test_cells keeps a1-a3, t3, t8, f12, f14. */
s32 cull_grid_height_range(s32 x, s32 z, s32 w, s32 h, s32 stride, u8 *grid, s32 *maxOut) {
    u8 *row = grid + ((z * stride + x) << 2);
    u8 *p;
    s32 lo = 0x7FFF;
    s32 hi = -0x8000;
    s32 n;

    do {
        p = row;
        n = w;
        do {
            s32 a = *(s16 *) (p + 0);
            s32 b = *(s16 *) (p + 2);

            if (a < lo) {
                lo = a;
            }
            if (hi < b) {
                hi = b;
            }
            p += 4;
        } while (--n != 0);
        row += stride << 2;
    } while (--h != 0);
    *maxOut = hi;
    return lo;
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/5FD50/cull_grid_height_range.s")
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
/* Writes the positions of the 8 corners of a box into 8 Vtx (0x10 bytes
 * each) at v: x0 = x * xs, x1 = (x + w) * xs, z0 = z * zs, z1 = (z + d) * zs
 * (unsigned 32-bit products), y = y0 / y1; corners 0-3 at y0 and 4-7 at y1,
 * going (x0,z0) (x1,z0) (x1,z1) (x0,z1) and (x0,z0) (x1,z0) (x1,z1) (x0,z1).
 * Only the position halves are written.
 * asm: v in a2, x/z/w/d in s0/s1/s2/s3, xs/zs in s7/t8, y0/y1 in v1/a0; it
 * clobbers t6 and hi/lo.
 * Asm callers rely on preserved: cull_test_cells keeps a1-a3, t3, f12, f14. */
void cull_box_corners(Vtx *v, u32 x, u32 z, u32 w, u32 d, u32 xs, u32 zs, s32 y0, s32 y1) {
    s32 x0 = x * xs;
    s32 x1 = (x + w) * xs;
    s32 z0 = z * zs;
    s32 z1 = (z + d) * zs;

    v[0].v.ob[0] = x0, v[0].v.ob[1] = y0, v[0].v.ob[2] = z0;
    v[1].v.ob[0] = x1, v[1].v.ob[1] = y0, v[1].v.ob[2] = z0;
    v[2].v.ob[0] = x1, v[2].v.ob[1] = y0, v[2].v.ob[2] = z1;
    v[3].v.ob[0] = x0, v[3].v.ob[1] = y0, v[3].v.ob[2] = z1;
    v[4].v.ob[0] = x0, v[4].v.ob[1] = y1, v[4].v.ob[2] = z0;
    v[5].v.ob[0] = x1, v[5].v.ob[1] = y1, v[5].v.ob[2] = z0;
    v[6].v.ob[0] = x1, v[6].v.ob[1] = y1, v[6].v.ob[2] = z1;
    v[7].v.ob[0] = x0, v[7].v.ob[1] = y1, v[7].v.ob[2] = z1;
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/5FD50/cull_box_corners.s")
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
extern u32 gCullTask[];  /* scheduler task: header words, then an OSTask at +0x10 */
extern u8 gCullUcodeText[];   /* gfx ucode text */
extern u8 gCullUcodeData[];   /* gfx ucode data */
extern u8 gCullDramStack[];   /* dram stack (0x400) */
extern u8 gCullOutput[];   /* output buffer */
extern u8 gCullOutputEnd[];   /* output buffer end */

/* Unless the level grid is 1 x 1 (gLevelCols == gLevelRows == 1, returns 1),
 * fills the task gCullTask (an M_GFXTASK OSTask at +0x10 with data_ptr /
 * data_size from the arguments), writes back `wb` (0x80 bytes) and the task,
 * invalidates the first 0x20 bytes of the output buffer, sends the task to
 * gScheduler, calls rsp_wait_task(0x4D3), and returns 0 if the output
 * buffer's first word is 0xE8000000 (else 1).
 * asm: dataPtr/wb/dataSize in a1/a2/a3, result in t6; it saves a1-a3, t1, t3,
 * s4, s5, t8 and fp. Asm callers rely on preserved: cull_test_cells keeps t3. */
s32 cull_rsp_test(void *dataPtr, void *wb, s32 dataSize) {
    u32 *t;

    if (((u16) gLevelCols) == 1 && gLevelRows == 1) {
        return 1;
    }
    t = gCullTask;
    t[4] = M_GFXTASK;
    t[5] = 0;
    t[6] = (u32) gRspBootText;
    t[8] = (u32) gCullUcodeText;
    t[9] = 0x1000;
    t[10] = (u32) gCullUcodeData;
    t[11] = 0x800;
    t[12] = (u32) gCullDramStack;
    t[13] = 0x400;
    t[14] = (u32) gCullOutput;
    t[15] = (u32) gCullOutputEnd;
    t[18] = (u32) gYieldBuffer;
    t[19] = 0x900;
    t[2] = 1;
    t[21] = (u32) gTaskDoneQueue;
    t[22] = 0x4D3;
    t[3] = 0;
#ifdef PORT_HOST
    {
        /* (the port: cull_start's list is on a host fiber stack, whose address differs from run to run; the task
         * word gets the address the list has on the N64, on the main thread's stack (gMainThreadStack + 0x1D88:
         * 0x80312B08 in every emulator run), and the platform takes the host one from port_cull_data) */
        extern u8 gMainThreadStack[];

        port_cull_data(dataPtr);
        t[16] = (u32) (gMainThreadStack + 0x1D88);
    }
#else
    t[16] = (u32) dataPtr;
#endif
    t[7] = gAspMainText - gRspBootText;
    t[17] = dataSize;
    t[20] = (u32) &gMainSchedClient; /* BcScTask.client */
    osWritebackDCache(wb, 0x80);
    osWritebackDCache(t, 0x40);
    osInvalDCache(gCullOutput, 0x20);
    osSendMesg(&gScheduler.interruptQ, (OSMesg) t, OS_MESG_BLOCK);
    rsp_wait_task(0x4D3);
    if (*(u32 *) gCullOutput == 0xE8000000) {
        return 0;
    }
    return 1;
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/5FD50/cull_rsp_test.s")
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
u32 *lvl_build_visible_dl(u8 *lvl, u32 *dl, u8 *rec, u8 *recEnd, u8 *grp, u8 *grpEnd);
void lvl_build_dl_calls(s32 arg0, u32 *gfx, u32 *dl, u32 *end);
void lvl_step_tex_anims(u8 *base);

/* Builds the level's four visible-geometry display lists into dl0..dl3:
 * list k uses the record ranges [lvl + *(lvl + 0xA0 + 4k), lvl + *(lvl +
 * 0xA4 + 4k)) and groups [lvl + *(lvl + 0xB0 + 4k), lvl + *(lvl + 0xB4 +
 * 4k)) (lvl_build_visible_dl; its returned end pointers are dropped). Then writes
 * a gSPDisplayList for each list packed in [lvl + *(lvl + 0x7C), lvl +
 * *(lvl + 0x80)) into gfx (lvl_build_dl_calls), and steps the level's animation
 * records (lvl_step_tex_anims). lvl is gLevelData. ABI: dl0..dl3 in a0-a3, gfx
 * on the stack (00000.c declares the four as s32). The asm's offset adds
 * trap on overflow (pointer + small offset, never in game). */
void lvl_build_visible_dls(u32 *dl0, u32 *dl1, u32 *dl2, u32 *dl3, u32 *gfx) {
    u8 *lvl = gLevelData;

    lvl_build_visible_dl(lvl, dl0, lvl + *(s32 *) (lvl + 0xA0), lvl + *(s32 *) (lvl + 0xA4),
                  lvl + *(s32 *) (lvl + 0xB0), lvl + *(s32 *) (lvl + 0xB4));
    lvl_build_visible_dl(lvl, dl1, lvl + *(s32 *) (lvl + 0xA4), lvl + *(s32 *) (lvl + 0xA8),
                  lvl + *(s32 *) (lvl + 0xB4), lvl + *(s32 *) (lvl + 0xB8));
    lvl_build_visible_dl(lvl, dl2, lvl + *(s32 *) (lvl + 0xA8), lvl + *(s32 *) (lvl + 0xAC),
                  lvl + *(s32 *) (lvl + 0xB8), lvl + *(s32 *) (lvl + 0xBC));
    lvl_build_visible_dl(lvl, dl3, lvl + *(s32 *) (lvl + 0xAC), lvl + *(s32 *) (lvl + 0xB0),
                  lvl + *(s32 *) (lvl + 0xBC), lvl + *(s32 *) (lvl + 0xC0));
    lvl_build_dl_calls((s32) lvl, gfx, (u32 *) (lvl + *(s32 *) (lvl + 0x7C)),
                  (u32 *) (lvl + *(s32 *) (lvl + 0x80)));
    lvl_step_tex_anims(lvl);
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/5FD50/lvl_build_visible_dls.s")
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
/* Writes a gSPDisplayList into gfx for each display list packed back to back in [dl, end), then a closing
 * gSPEndDisplayList (0x802A4DE8).
 * Writes a gSPDisplayList (0x06000000, physical address) into `gfx` for each
 * display list packed back to back in [dl, end), stepping over each one to
 * just past its gSPEndDisplayList (0xB8000000) word, then a closing
 * gSPEndDisplayList. `end` must sit exactly after one of the lists (the asm
 * compares with `!=`). The asm also leaves gfx advanced in a1, which its only
 * caller does not read. arg0 is unused.
 * Asm callers rely on preserved: lvl_build_visible_dls keeps a0. */
void lvl_build_dl_calls(s32 arg0, u32 *gfx, u32 *dl, u32 *end) {
    u32 w0;

    while (dl != end) {
        gfx[0] = 0x06000000;
        gfx[1] = (u32) dl - 0x80000000;
        gfx += 2;
        do {
            w0 = dl[0];
            dl += 2;
        } while (w0 != 0xB8000000);
    }
    gfx[0] = 0xB8000000;
    gfx[1] = 0;
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/5FD50/lvl_build_dl_calls.s")
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
extern s16 gCullDlGroupIds[];
s32 lvl_find_tex_anim(u8 *base, s32 id);
u32 *lvl_copy_dl_tex_anim(u8 *base, u32 *dst, s32 start, s32 end, s32 patch);
u32 *lvl_copy_visible_groups(u8 *base, u32 *dst, s16 *exclEnd, u8 *rec, u8 *end);

/* Is `v` in the sorted, -1-terminated visible-cell list gVisibleCells? (Scans
 * to the first entry >= v, as the asm.) */
static s32 port_cell_listed(s32 v) {
    s16 *p = PORT_DRAW_CELLS(gVisibleCells);
    s32 t;

    for (;;) {
        t = *p++;
        if (t == -1) {
            return 0;
        }
        if (t >= v) {
            return t == v;
        }
    }
}

/* Does the n-word cell list `cells` share a value with gVisibleCells? For each
 * listed value (to the -1) the cells are scanned from the start up to the
 * first one greater than it, as the asm does. */
static s32 port_cells_listed(s32 *cells, s32 n) {
    s16 *p = PORT_DRAW_CELLS(gVisibleCells);
    s32 *c;
    s32 v;
    s32 k;

    while ((v = *p++) != -1) {
        for (c = cells, k = n; k != 0; c++, k--) {
            if (*c == v) {
                return 1;
            }
            if (v < *c) {
                break;
            }
        }
    }
    return 0;
}

/* Builds one display list for the visible part of the level: copies the
 * fixed list [lvl + *(lvl + 0x88), lvl + *(lvl + 0x8C)) to dl (8 bytes at a
 * time), then walks the 0x14-byte records [rec, recEnd) (+0 / +4 / +8: DL
 * start, alternate start, end offsets; +0xC: id; +0x10: cell). A record whose
 * cell is listed in gVisibleCells starts a group: its id is appended to the s16
 * list gCullDlGroupIds, its DL copied with lvl_copy_dl_tex_anim patched by
 * lvl_find_tex_anim(lvl, id); the following records with the same id whose cell
 * is listed are copied from their alternate start, unpatched. After each
 * group the variable-size records [grp, grpEnd) (+0xC id, +0x10 count n, n
 * cells from +0x14; sorted by id) with that id and a listed cell are copied
 * likewise (alternate start, unpatched). Finally lvl_copy_visible_groups adds the
 * remaining groups' records (leaving out the ids collected), the list is
 * ended with 0xB8000000 / 0 (G_ENDDL) and the pointer past it is returned.
 * Register convention: lvl, dl, rec, recEnd in a0-a3, grp/grpEnd in t8/t9,
 * result in a1 (conventions.txt); the asm saves t0-t2 and leaves s0-s2 and gp
 * changed; its add/addi trap and its loops use `!=`. Asm caller
 * lvl_build_visible_dls keeps a0, t0-t2 live. */
u32 *lvl_build_visible_dl(u8 *lvl, u32 *dl, u8 *rec, u8 *recEnd, u8 *grp, u8 *grpEnd) {
    u32 *src = (u32 *) (lvl + *(s32 *) (lvl + 0x88));
    u32 *srcEnd = (u32 *) (lvl + *(s32 *) (lvl + 0x8C));
    s16 *excl = gCullDlGroupIds;
    u8 *g;
    s32 id;
    s32 patch;

    while (src != srcEnd) {
        dl[0] = src[0];
        dl[1] = src[1];
        src += 2;
        dl += 2;
    }
    while (rec != recEnd) {
        if (!port_cell_listed(*(s32 *) (rec + 0x10))) {
            rec += 0x14;
            continue;
        }
        id = *(s32 *) (rec + 0xC);
        *excl++ = id;
        patch = lvl_find_tex_anim(lvl, id);
        dl = lvl_copy_dl_tex_anim(lvl, dl, *(s32 *) (rec + 0), *(s32 *) (rec + 8), patch);
        for (;;) {
            rec += 0x14;
            if (rec == recEnd || *(s32 *) (rec + 0xC) != id) {
                break;
            }
            if (port_cell_listed(*(s32 *) (rec + 0x10))) {
                dl = lvl_copy_dl_tex_anim(lvl, dl, *(s32 *) (rec + 4), *(s32 *) (rec + 8), 0);
            }
        }
        for (g = grp; g != grpEnd; g += 0x14 + (*(s32 *) (g + 0x10) << 2)) {
            if (id < *(s32 *) (g + 0xC)) {
                break;
            }
            if (id == *(s32 *) (g + 0xC) && port_cells_listed((s32 *) (g + 0x14), *(s32 *) (g + 0x10))) {
                dl = lvl_copy_dl_tex_anim(lvl, dl, *(s32 *) (g + 4), *(s32 *) (g + 8), 0);
            }
        }
    }
    dl = lvl_copy_visible_groups(lvl, dl, excl, grp, grpEnd);
    dl[0] = 0xB8000000;
    dl[1] = 0;
    return dl + 2;
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/5FD50/lvl_build_visible_dl.s")
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
/* A variable-size animation record: an 8-byte header plus nFrames words.
 * (period and timer at 0x8/0xA overlap the first frame word.) */
typedef struct {
    /* 0x00 */ s32 unk0;
    /* 0x04 */ u8 nFrames;
    /* 0x05 */ u8 frame;
    /* 0x06 */ u8 fade;     /* nonzero: alpha follows the timer */
    /* 0x07 */ u8 alpha;
    /* 0x08 */ u16 period;
    /* 0x0A */ u16 timer;
} AnimRecord;

/* Steps the level's texture animation records: advances each timer and frame and sets the alpha of fading
 * records (0x802A5020).
 * Steps each animation record in the list whose start/end offsets (from
 * `base`) are the words at base+0x2C / base+0x30: timer counts up to
 * period, then wraps to 0 and advances the frame (wrapping at nFrames);
 * a fading record gets alpha = 0xFF * timer / period. The asm loops with
 * `!=`, so the end must be exactly reached. */
void lvl_step_tex_anims(u8 *base) {
    u8 *p = base + *(s32 *) (base + 0x2C);
    u8 *end = base + *(s32 *) (base + 0x30);

    while (p != end) {
        AnimRecord *r = (AnimRecord *) p;
        u32 t = r->timer + 1;

        if (r->period == t) {
            u32 f = r->frame + 1;

            if (r->nFrames == f) {
                f = 0;
            }
            r->frame = f;
            t = 0;
        }
        if (r->fade != 0) {
            r->alpha = (0xFF * t) / r->period; /* divu; traps on period 0 */
        }
        r->timer = t;
        p += r->nFrames * 4 + 8;
    }
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/5FD50/lvl_step_tex_anims.s")
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
extern u32 gLevelTexAnimCur; /* texture address for the current frame (0 = keep) */
extern u32 gLevelTexAnimNext; /* texture address for the next frame (0 = keep) */
extern u8 gLevelTexAnimFade;  /* fade flag */
extern u8 gLevelTexAnimAlpha;  /* alpha */

/* Finds the texture animation record with this id and stores its current (and, when fading, next) image and
 * its alpha for the DL copy; nonzero if a patch is needed (0x802A50DC).
 * Looks up the animation record (see AnimRecord below; frame words from +8)
 * with unk0 == id in the list [base + *(base+0x2C), base + *(base+0x30)).
 * Not found: returns 0. Found: gLevelTexAnimCur = frame word `frame` (0 for frame
 * 0), gLevelTexAnimFade = fade; without fade returns frame != 0; with fade also sets
 * gLevelTexAnimNext = frame word frame + 1 (0 when that is nFrames) and gLevelTexAnimAlpha
 * = alpha, and returns 1. The asm walks the list with `!=`.
 * asm: base in a0, id in t4, result in gp; it saves v0, v1, a1, a2.
 * Asm callers rely on preserved: lvl_build_visible_dl keeps a0-a3, t4, t8, t9;
 * lvl_copy_visible_groups keeps a0, a1, t0, t1, t3, t8, t9. */
s32 lvl_find_tex_anim(u8 *base, s32 id) {
    u8 *p = base + *(s32 *) (base + 0x2C);
    u8 *end = base + *(s32 *) (base + 0x30);
    u32 frame;

    for (; p != end; p += p[4] * 4 + 8) {
        if (*(s32 *) p == id) {
            frame = p[5];
            if (frame != 0) {
                gLevelTexAnimCur = *(u32 *) (p + 8 + frame * 4);
            } else {
                gLevelTexAnimCur = 0;
            }
            gLevelTexAnimFade = p[6];
            if (p[6] == 0) {
                return frame != 0;
            }
            frame++;
            if (p[4] == frame) {
                gLevelTexAnimNext = 0;
            } else {
                gLevelTexAnimNext = *(u32 *) (p + 8 + frame * 4);
            }
            gLevelTexAnimAlpha = p[7];
            return 1;
        }
    }
    return 0;
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/5FD50/lvl_find_tex_anim.s")
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
/* Advances `p` past the next command with opcode `op` (no bound: it must
 * exist), leaving that command's first word in w0. */
#define SKIP_TO_CMD(p, op, w0)       \
    do {                             \
        (w0) = (p)[0];               \
        (p) += 2;                    \
    } while (((w0) >> 24) != (op))

/* Copies a display-list range to dst; when patch is set, swaps in the animated SETTIMG images and ORs the
 * fade alpha into the G_SETPRIMCOLOR (0x802A51FC).
 * Copies the display list [base + start, base + end) (8-byte commands) to dst
 * and returns dst advanced past the copy. If `patch` (lvl_find_tex_anim's
 * result): sets the address of the first G_SETTIMG (0xFD) to gLevelTexAnimCur if
 * nonzero; if gLevelTexAnimFade, also the second G_SETTIMG's to gLevelTexAnimNext if
 * nonzero, and ORs gLevelTexAnimAlpha into the first word of the next
 * G_SETPRIMCOLOR (0xFA). The copy loop uses `!=`.
 * asm: base/dst in a0/a1 (a1 returned advanced), start/end in t5/t6, patch
 * in gp (clobbered).
 * Asm callers rely on preserved: lvl_build_visible_dl keeps a0, a2, a3, t4, t7-t9. */
u32 *lvl_copy_dl_tex_anim(u8 *base, u32 *dst, s32 start, s32 end, s32 patch) {
    u32 *s = (u32 *) (base + start);
    u32 *e = (u32 *) (base + end);
    u32 *d = dst;
    u32 *q;
    u32 w0;

    while (s != e) {
        d[0] = s[0];
        d[1] = s[1];
        s += 2;
        d += 2;
    }
    if (patch != 0) {
        q = dst;
        SKIP_TO_CMD(q, 0xFD, w0);
        if (gLevelTexAnimCur != 0) {
            q[-1] = gLevelTexAnimCur;
        }
        if (gLevelTexAnimFade != 0) {
            SKIP_TO_CMD(q, 0xFD, w0);
            if (gLevelTexAnimNext != 0) {
                q[-1] = gLevelTexAnimNext;
            }
            SKIP_TO_CMD(q, 0xFA, w0);
            q[-2] = w0 | gLevelTexAnimAlpha;
        }
    }
    return d;
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/5FD50/lvl_copy_dl_tex_anim.s")
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
extern s16 gCullDlGroupIds[]; /* sorted ids to leave out, up to the caller's end pointer */

/* Appends the DLs of the group records that have a visible cell and whose ids the record pass did not already
 * copy (texture animations patched) (0x802A5334).
 * Walks the variable-size records [rec, end) (+0 / +4 / +8: display-list
 * start, alternate start and end offsets from base; +0xC: id; +0x10: count
 * n; +0x14: n sorted words) and, for each record whose word list shares a
 * value with the sorted, -1-terminated s16 list gVisibleCells and whose id
 * isn't in the sorted list [gCullDlGroupIds, exclEnd): copies its display list
 * (from the alternate start when the id equals the previous copied record's)
 * to dst, 8 bytes at a time. Unless it used the alternate start, the copy is
 * patched like lvl_copy_dl_tex_anim when lvl_find_tex_anim(base, id) is nonzero:
 * G_SETTIMG addresses from gLevelTexAnimCur / gLevelTexAnimNext and gLevelTexAnimAlpha ORed into
 * the G_SETPRIMCOLOR. Returns dst advanced past the copies.
 * The asm takes base/dst/exclEnd/rec/end in a0/a1/v1/t8/t9 and returns dst
 * in a1 (conventions.txt); it leaves lvl_find_tex_anim's result (or a D_803C32xx
 * value) in gp. Loops use `!=`, so the record walk must reach `end`
 * exactly. */
u32 *lvl_copy_visible_groups(u8 *base, u32 *dst, s16 *exclEnd, u8 *rec, u8 *end) {
    s32 prev = -2;

    while (rec != end) {
        s32 n = *(s32 *) (rec + 0x10);
        s32 *words = (s32 *) (rec + 0x14);
        s16 *l = PORT_DRAW_CELLS(gVisibleCells);
        s32 id;
        s16 *e;
        u32 *s;
        u32 *se;
        u32 *q;
        u32 w0;
        s32 patch;

        for (;;) {
            s32 val = *l;
            s32 *p;
            s32 k;

            if (val == -1) {
                goto next;
            }
            l++;
            for (p = words, k = n; k != 0; p++, k--) {
                if (*p == val) {
                    goto found;
                }
                if (val < *p) {
                    break;
                }
            }
        }
    found:
        id = *(s32 *) (rec + 0xC);
        for (e = gCullDlGroupIds; e != exclEnd; e++) {
            if (id < *e) {
                break;
            }
            if (*e == id) {
                goto next;
            }
        }
        if (id == prev) {
            s = (u32 *) (base + *(s32 *) (rec + 4));
            q = NULL;
        } else {
            s = (u32 *) (base + *(s32 *) (rec + 0));
            q = dst;
        }
        prev = id;
        patch = lvl_find_tex_anim(base, id);
        se = (u32 *) (base + *(s32 *) (rec + 8));
        while (s != se) {
            dst[0] = s[0];
            dst[1] = s[1];
            s += 2;
            dst += 2;
        }
        if (patch != 0 && q != NULL) {
            SKIP_TO_CMD(q, 0xFD, w0);
            if (gLevelTexAnimCur != 0) {
                q[-1] = gLevelTexAnimCur;
            }
            if (gLevelTexAnimFade != 0) {
                SKIP_TO_CMD(q, 0xFD, w0);
                if (gLevelTexAnimNext != 0) {
                    q[-1] = gLevelTexAnimNext;
                }
                SKIP_TO_CMD(q, 0xFA, w0);
                q[-2] = w0 | gLevelTexAnimAlpha;
            }
        }
    next:
        rec += n * 4 + 0x14;
    }
    return dst;
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/5FD50/lvl_copy_visible_groups.s")
#endif
