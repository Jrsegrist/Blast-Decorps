#include "common.h"
#include <ultra64.h>
#include "game/game.h"
/* Views: this file reads these shared variables (game/variables.h) as other types. */
#define gVehicles ((u8 *) gVehicles)
#ifdef NON_MATCHING
#define gObjShadowTex (*(s32 *) &gObjShadowTex)
#define gGhostDl (*(u8 * N64P *) &gGhostDl)
#define gLevelFileDl0 (*(u8 * N64P *) &gLevelFileDl0)
#define gLevelFileDl1 (*(u8 * N64P *) &gLevelFileDl1)
#define gLevelFileDl2 (*(u8 * N64P *) &gLevelFileDl2)
#define gLevelFileDl3 (*(u8 * N64P *) &gLevelFileDl3)
#endif
/* end of views */

#ifdef NON_MATCHING
/* Shared declarations for the NON_MATCHING (port) rewrites below. Most of
 * these functions take a level object (in t0 or t4 in the asm) whose header
 * holds offsets of its sub-lists: OBJ_PTR(obj, off) = obj + *(s32 *)(obj + off). */

#define OBJ_PTR(obj, off) ((obj) + *(s32 *) ((obj) + (off)))
/* Big-endian 16/32-bit fields at any alignment (the asm assembles them from bytes). */
#define BE16U(p) (((p)[0] << 8) | (p)[1])
#define BE16S(p) ((s16) BE16U(p))
#define BE32(p) (((u32) (p)[0] << 24) | ((p)[1] << 16) | ((p)[2] << 8) | (p)[3])

/* (Out802A32CC, SpawnFP: game/regs.h.) */
void lvl_load_model_plain(s32 type, Out802A32CC *out);

#endif

/* get_cop0_status: `mfc0 $v0, $12` (read COP0 Status register) wrapped in a
 * dead $ra save/restore frame - same hand-written COP0-leaf-stub character
 * as __osGetSR in init/2330.c. Permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
u32 __osGetSR(void);

/* The COP0 Status register (0x802A1320).
 * Returns the COP0 Status register (the asm is a bare `mfc0 $v0, $12`);
 * the port's platform layer supplies __osGetSR. */
u32 get_cop0_status(void) {
    return __osGetSR();
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/5CB60/get_cop0_status.s")
#endif

/* FILE-WIDE FINDING: this file's functions save $ra via the 64-bit `sd`/`ld`
 * doubleword form, not the normal 32-bit `sw`/`lw` pair - the same signature
 * documented in full in hd_code/77E20.c's top-of-file note and the project
 * skill file, confirming (with no exceptions found) that this file is part
 * of the same large hand-written-assembly module. Any pragma below without
 * a more specific comment follows this convention; a few have their own
 * more specific non-ABI explanation where one was already worked out. */
/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING

/* Finds the 0x74-byte record of gVehicles whose word at 0x5C is `id` (no
 * bound: it must exist) and stores v0Val, v1Val, a0Val and obj[0x9B] in its
 * words at 0x64/0x68/0x6C/0x70.
 * asm: a0Val in a0, id in a1, v0Val in v0, v1Val in v1, obj in gp (each
 * caller points gp at its own vehicle object). It changes no register.
 * Asm callers rely on preserved: a0, a1 (skyfall_ride_follow & co), a1-a3, f12,
 * f14 (driver_update & co), a3, t0, t6 (backlash_update). */
void veh_set_record(s32 a0Val, s32 id, s32 v0Val, s32 v1Val, u8 *obj) {
    u8 *rec = gVehicles;

    while (*(s32 *) (rec + 0x5C) != id) {
        rec += 0x74;
    }
    *(s32 *) (rec + 0x64) = v0Val;
    *(s32 *) (rec + 0x68) = v1Val;
    *(s32 *) (rec + 0x6C) = a0Val;
    *(s32 *) (rec + 0x70) = obj[0x9B];
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/5CB60/veh_set_record.s")
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
void veh_copy_dl(Gfx *src, Gfx *end, Gfx * N64P *dstp);

/* Appends a 0x74-byte record to gVehicles (cursor gVehiclesEnd) for the model
 * whose header is `hdr` (offsets in it are relative to hdr): word 0x70 = 0,
 * 4 = v0Val, 8 = v1Val, 0x5C = a0Val; word 0 and 0xC..0x2C = hdr + header
 * words 0x14 and 0x24..0x44, 0x54 = hdr + word 0x48. Copies the 8-byte units
 * [hdr + w[0x1C], hdr + w[0x20]) to the heap gHeapPtr (advanced; the asm
 * loops with `!=`), and sets words 0x30..0x50 to words 0xC..0x2C moved by the
 * copy's displacement (heap - word 0xC). Unless gLevelKind == 1 and a1Val ==
 * 0, it then runs veh_copy_dl(word 0xC, word 0x18, rec + 0x58).
 * Register convention: a0Val/a1Val in a0/a1, v0Val/v1Val in v0/v1, hdr in s2
 * (conventions.txt); the asm saves t0-t6. Asm callers keep a1, a3, t7, f12,
 * f14 (some a2, t4-t6) live (a mixed build would need a thunk). */
void veh_add_record(s32 a0Val, s32 a1Val, s32 v0Val, s32 v1Val, u8 *hdr) {
    u8 *rec = gVehiclesEnd;
    u64 *src;
    u64 *end;
    u64 *dst;
    s32 delta;
    s32 i;

    gVehiclesEnd = rec + 0x74;
    *(s32 *) (rec + 0x70) = 0;
    *(s32 *) (rec + 0x4) = v0Val;
    *(s32 *) (rec + 0x8) = v1Val;
    *(s32 *) (rec + 0x5C) = a0Val;
    *(u8 * N64P *) (rec + 0x0) = OBJ_PTR(hdr, 0x14);
    for (i = 0; i < 9; i++) {
        *(u8 * N64P *) (rec + 0xC + i * 4) = OBJ_PTR(hdr, 0x24 + i * 4);
    }
    *(u8 * N64P *) (rec + 0x54) = OBJ_PTR(hdr, 0x48);
    src = (u64 *) OBJ_PTR(hdr, 0x1C);
    end = (u64 *) OBJ_PTR(hdr, 0x20);
    dst = (u64 *) gHeapPtr;
    delta = (u32) dst - *(u32 *) (rec + 0xC);
    while (src != end) {
        *dst++ = *src++;
    }
#ifdef PORT_HOST
    /* the copy is display-list commands (host-order words, like the model's
     * own): record that for the stale-byte emulation (level 6's collision
     * records land on a copy made on the bonus-level screen) */
    {
        void port_unit_mark(void *p, u32 n, s32 width);
        port_unit_mark(gHeapPtr, (u32) ((u8 *) dst - (u8 *) gHeapPtr) / 4, 4);
    }
#endif
    gHeapPtr = (u8 *) dst;
    for (i = 0; i < 9; i++) {
        *(s32 *) (rec + 0x30 + i * 4) = *(s32 *) (rec + 0xC + i * 4) + delta;
    }
    if (((s32) gLevelKind) != LEVEL_KIND_MISSION || a1Val != 0) {
        veh_copy_dl(*(Gfx * N64P *) (rec + 0xC), *(Gfx * N64P *) (rec + 0x18), (Gfx * N64P *) (rec + 0x58));
    }
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/5CB60/veh_add_record.s")
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING

/* Calls mb_copy_dl(src, end, dstp), saving every register around it (the vehicle record's display-list copy)
 * (0x802A1558).
 * Register-preserving wrapper: mb_copy_dl(src, end, dstp).
 * asm: src in t2, end in t3, dstp in t4; it saves and restores every
 * register (including v0 and s0-s7) around the call. */
void veh_copy_dl(Gfx *src, Gfx *end, Gfx * N64P *dstp) {
    mb_copy_dl(src, end, dstp);
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/5CB60/veh_copy_dl.s")
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
extern s32 gLevelStatusArg; /* initlevel's second argument (a status stream pointer or 0) */
/* u64 game-mode constants in .hd_code_data (the asm loads them by raw
 * address with ld). */
#define gModeGlobeSeqU64 (*(u64 *) 0x8030D880)
#define gModeGlobeSeqU64Spawn (*(u64 *) 0x8030D888)

void lvl_read_header(u8 *obj);
u8 *lvl_resolve_dl_textures(u8 *obj);
void lvl_resolve_tex_ids(u8 *obj, u8 *param, s32 *s1io);
void lvl_relocate_dls(u8 *obj);
u8 *lvl_build_coll_grid_a(u8 *obj, s32 id, s32 b4F, s32 *s1io);
u8 *lvl_build_coll_grid_b(u8 *obj, s32 id, s32 b4F, s32 *s1io);
void lvl_build_switch_groups(u8 *obj, s32 id, s32 b4F, s32 *s1io);
u8 *lvl_build_object_groups(u8 *obj, s32 *s1io);
void lvl_build_ground_grid(u8 *obj);
/* An Mtx's s16 halves (integer parts here): PORT_HALF swaps the two halves
 * of each word on the little-endian host, where the port keeps Mtx words
 * in host order (game/port.h). */
#define MTX_HALF(off) (PORT_HALF((off) / 2) * 2)

void lvl_build_part_tris(u8 *obj, s32 *s1io);
void lvl_init_boxes(void);
void lvl_read_alpha_zones(u8 *obj);
u8 *lvl_spawn_objects(u8 *obj, u8 *param, s32 *s1io);
s32 lvl_spawn_vehicles(u8 *obj, s32 fp);
void lvl_spawn_carrier(u8 *obj, s32 fp);
void lvl_spawn_shuttle(void);
void lvl_spawn_chopper(u8 *obj);
void lvl_load_ghost_model(Out802A32CC *out);
void lvl_load_scientist(void);

/* What the asm's v0 and t9 hold on entry from the only caller, 00000.c's
 * hd_setup_level (hd.c's level setup): its last call before initlevel that sets them
 * is lowest_set_bit(gNextGameMode) (the debug print after it is an empty stub),
 * which returns the index of the lowest set bit of the game mode (-1 for 0)
 * and leaves t9 = that index, or the mode's low word when bit 0 is set (the
 * loop doesn't run; 0 for mode 0). The 64-bit mask is doubled by adding so
 * this stays inline (no __ll_lshift call). */
static s32 port_initlevel_v0(s32 *t9) {
    u64 mode = gNextGameMode;
    u64 bit = 1;
    s32 n = 0;

    if (mode == 0) {
        *t9 = 0;
        return -1;
    }
    if (mode & 1) {
        *t9 = (u32) mode;
        return 0;
    }
    do {
        bit += bit;
        n++;
    } while (!(mode & bit));
    *t9 = n;
    return n;
}

/* initlevel (the name hd.c's debug output gives it: "enter initlevel" ...
 * "exit initlevel" around the call): level setup from the level object obj
 * (heap data; offsets as OBJ_PTR):
 * gLevelStatusArg = arg1, then the level header (lvl_read_header), the texture
 * table (tex_load_table), display lists and textures (lvl_resolve_dl_textures,
 * lvl_resolve_tex_ids, lvl_relocate_dls), the animation channels (anim_tex_init), the
 * collision triangles (lvl_build_coll_grid_a, lvl_build_coll_grid_b, lvl_build_switch_groups,
 * lvl_build_object_groups), grids and tables (lvl_build_ground_grid .. obj_clear_delayed_damage), the
 * placed objects (lvl_spawn_objects), obj_clear_effect_queue, block_spawn_all(obj words
 * 0x3C/0x40). Unless the game mode gNextGameMode is 0x80 the level's vehicles
 * and objects are spawned (lvl_spawn_vehicles). gHaveShuttle = gHaveCarrier =
 * gHaveChopper = 0; then mode 2: the type-0xFF object (lvl_spawn_carrier) unless
 * gRecDemoNumber; other modes: unless the mode is 0x80 or arg1 is set, the
 * type-0xFF object and the 0xFD vehicle (lvl_spawn_shuttle), then always the
 * 0xFE vehicle (lvl_spawn_chopper). Then lvl_load_ghost_model, commpt_setup,
 * lvl_load_scientist, gCarrierPanelEvents / gCarrierPanelEventsEnd = OBJ_PTR(obj, 0x58 / 0x5C),
 * rdu_init / tnt_spawn_all / crate_spawn_all on the obj ranges
 * 0x34-0x38, 0x38-0x3C, 0x20-0x24, frontEndPresent = 0, and when the mode is
 * neither 2 nor gModeGlobeSeqU64 (0x100000000000) and arg1 is set,
 * apply_status(arg1) (arg1 is the saved-status stream it applies).
 * Registers the asm hands on (it saves every callee-saved register itself):
 *  - s1: lvl_resolve_dl_textures's result, threaded through lvl_resolve_tex_ids and the
 *    triangle builders (s1).
 *  - fp: lvl_resolve_tex_ids's param is the caller's fp (hd_setup_level and the
 *    IDO code above it never set $s8; its value comes from whatever hand asm
 *    last left it): NULL here (it only reaches gTexDecodeReq.param, read by the
 *    decoder for packed types 4/5). From lvl_build_object_groups on, fp is its result
 *    (the gCollTris record end), handed to lvl_spawn_objects and on to the
 *    spawns: lvl_spawn_vehicles returns the fp the next object would get, and
 *    that goes to lvl_spawn_carrier.
 *  - v0 / t9 (triangle id and byte 0x4F of the gCollCellsA / gCollCellsB /
 *    group records): the caller's, never changed before those calls (every
 *    callee in between preserves them); see port_initlevel_v0.
 *  - s2-s4 into lvl_load_ghost_model (in/out; its outputs are dead here): 0.
 *  - s1 also reaches block_spawn_all's callee coll_add_dyn_tris (byte 0x58 of each
 *    record's first triangle; obj_tri_clip_free_arc tests it) after lvl_build_part_tris
 *    and lvl_spawn_objects change it: the NM build passes it to block_spawn_all as
 *    an extra argument.
 */
void initlevel(u8 *obj, s32 arg1) {
    Out802A32CC o;
    s32 s1;
    s32 id;
    s32 b4F;
    u8 *fp;

    gLevelStatusArg = arg1;
    lvl_read_header(obj);
    tex_load_table();
    s1 = (s32) lvl_resolve_dl_textures(obj);
    lvl_resolve_tex_ids(obj, NULL, &s1);
    lvl_relocate_dls(obj);
    anim_tex_init();
    id = port_initlevel_v0(&b4F);
    lvl_build_coll_grid_a(obj, id, b4F, &s1);
    lvl_build_coll_grid_b(obj, id, b4F, &s1);
    lvl_build_switch_groups(obj, id, b4F, &s1);
    fp = lvl_build_object_groups(obj, &s1);
    lvl_build_ground_grid(obj);
    lvl_build_part_tris(obj, &s1);
    lvl_init_boxes();
    anim_clear_copy_queue();
    cull_restart_now();
    lvl_read_alpha_zones(obj);
    fx_reset();
    obj_clear_delayed_damage();
    lvl_spawn_objects(obj, fp, &s1);
    obj_clear_effect_queue();
    block_spawn_all(OBJ_PTR(obj, 0x3C), OBJ_PTR(obj, 0x40), s1);
    if (gNextGameMode != MODE_LEVEL_STATUS) {
        fp = (u8 *) N64_IPTR(lvl_spawn_vehicles(obj, (s32) fp));
    }
    gHaveShuttle = 0;
    gHaveCarrier = 0;
    gHaveChopper = 0;
    if (gNextGameMode == MODE_ATTRACT) {
        if (gRecDemoNumber == 0) {
            lvl_spawn_carrier(obj, (s32) fp);
        }
    } else {
        if (gNextGameMode != MODE_LEVEL_STATUS && gLevelStatusArg == 0) {
            lvl_spawn_carrier(obj, (s32) fp);
            lvl_spawn_shuttle();
        }
        lvl_spawn_chopper(obj);
    }
    o.s2 = NULL;
    o.s3 = NULL;
    o.s4 = NULL;
    lvl_load_ghost_model(&o);
    commpt_setup(obj);
    lvl_load_scientist();
    gCarrierPanelEvents = OBJ_PTR(obj, 0x58);
    gCarrierPanelEventsEnd = OBJ_PTR(obj, 0x5C);
    rdu_init((s16 *) (OBJ_PTR(obj, 0x34)), (s16 *) (OBJ_PTR(obj, 0x38)));
    tnt_spawn_all(OBJ_PTR(obj, 0x38), OBJ_PTR(obj, 0x3C));
    crate_spawn_all((struct BoxSpawn *) (OBJ_PTR(obj, 0x20)), (struct BoxSpawn *) (OBJ_PTR(obj, 0x24)));
    frontEndPresent = 0;
    if (gNextGameMode != MODE_ATTRACT && gNextGameMode != gModeGlobeSeqU64 && gLevelStatusArg != 0) {
        apply_status((void *) N64_IPTR(gLevelStatusArg));
    }
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/5CB60/initlevel.s")
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
/* An entry of the table at 0x80306480 (stride 0x30), ended by an entry
 * whose `end` byte is -1. The asm builds the address as a raw lui/addiu. */
typedef struct {
    /* 0x00 */ s32 x;
    /* 0x04 */ s32 y;
    /* 0x08 */ s32 z;
    /* 0x0C */ u8 pad0C[8];
    /* 0x14 */ u8 radius;
    /* 0x15 */ s8 end;
    /* 0x16 */ s16 box[12]; /* four corners (x, y, z) with x/z = centre -/+ radius */
    /* 0x2E */ u8 pad2E[2];
} BoxEntry; /* size 0x30 */

#define gTriTargets ((BoxEntry *) 0x80306480)

/* Corner points of the box target table (0x802A1934).
 * For each entry of the table at 0x80306480 converts the centre (x, y, z)
 * >> 5 (arithmetic) to the four corner points of a square of half-size
 * `radius`: (x-r, y, z-r), (x+r, y, z-r), (x-r, y, z+r) at 0x16/0x1C/...,
 * i.e. box = {x-r, y, z-r, x+r, y, z-r, x-r, y, z+r, x+r, y, z+r}.
 * Asm callers rely on preserved: initlevel keeps t0, f12, f14 (the asm
 * saves v0, v1, a0-a3). */
void lvl_init_boxes(void) {
    BoxEntry *e;

    for (e = gTriTargets; e->end != -1; e++) {
        s32 x = e->x >> 5;
        s32 y = e->y >> 5;
        s32 z = e->z >> 5;
        s32 r = e->radius;

        e->box[0] = x - r;
        e->box[1] = y;
        e->box[2] = z - r;
        e->box[3] = x + r;
        e->box[4] = y;
        e->box[5] = z - r;
        e->box[6] = x - r;
        e->box[7] = y;
        e->box[8] = z + r;
        e->box[9] = x + r;
        e->box[10] = y;
        e->box[11] = z + r;
    }
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/5CB60/lvl_init_boxes.s")
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING

/* The scientist model (type 0x98) (0x802A19F4).
 * If gAcademyMarkerOn is set: load packed object 0x98 (lvl_load_model), gAcademyMarkerVtx =
 * OBJ_PTR(data, 0x14) and move each 16-byte vertex record from there up to
 * OBJ_PTR(data, 0x18) by (gAcademyMarkerX, gAcademyMarkerY, gAcademyMarkerZ) (s16 x, y, z at
 * +0/+2/+4; the asm loops with `!=` and its adds trap); gAcademyMarkerDl =
 * OBJ_PTR(data, 0x24).
 * The asm saves t0 (asm caller initlevel keeps it) and leaves
 * lvl_load_model's s2/s4 (and s5-s7) changed and f12/f14 as the libultra
 * calls leave them (the survey lists those as read by initlevel; not
 * modelled). */
void lvl_load_scientist(void) {
    Out802A396C o;
    u8 *p;
    u8 *end;

    if (gAcademyMarkerOn != 0) {
        lvl_load_model(0x98, &o);
        p = OBJ_PTR(o.s2, 0x14);
        gAcademyMarkerVtx = p;
        end = OBJ_PTR(o.s2, 0x18);
        while (p != end) {
            *(s16 *) (p + 0) += gAcademyMarkerX;
            *(s16 *) (p + 2) += gAcademyMarkerY;
            *(s16 *) (p + 4) += gAcademyMarkerZ;
            p += 0x10;
        }
        gAcademyMarkerDl = OBJ_PTR(o.s2, 0x24);
    }
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/5CB60/lvl_load_scientist.s")
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING

/* Part matrices and the moving-part triangles (0x802A1A9C).
 * Allocates from the heap gHeapPtr: 2n bytes of identity matrices (0x40-
 * byte fixed-point Mtx; gObjPartMtx0 = start, gObjPartMtx1 = start + n), then
 * unpacks the item blocks after the count word n at OBJ_PTR(obj, 0x74) into
 * 0x28-byte records (nine s16 << 5 as words, then a byte at 0x24). Each block
 * is a 0x2C-byte header (s16 tag at 0, -1 ending the list; s32 item count at
 * 0x28) followed by that many 0x44-byte items. gGroupTris = record start, gGroupTrisEnd = gHeapPtr =
 * end. The asm loops with `!=`, so n must be a multiple of 0x20.
 * asm: obj in t0; it leaves scratch values in s0, s2, s3 (no caller reads
 * them) and counts the item loops down in s1, so s1 comes back 0 when n != 0
 * (unchanged otherwise). initlevel hands that s1 on to block_spawn_all's
 * coll_add_dyn_tris, hence `s1io`.
 * Asm callers rely on preserved: initlevel keeps t0, f12, f14. */
void lvl_build_part_tris(u8 *obj, s32 *s1io) {
    u8 *hdr = OBJ_PTR(obj, 0x74);
    s32 n = *(s32 *) hdr;
    u8 *m = gHeapPtr;
    u8 *mend;

    gObjPartMtx0 = m;
    gObjPartMtx1 = m + n;
    mend = m + n + n;
    while (m != mend) {
        *(s16 *) (m + MTX_HALF(0x00)) = 1;
        *(s16 *) (m + MTX_HALF(0x02)) = 0;
        *(s32 *) (m + 0x04) = 0;
        *(s16 *) (m + MTX_HALF(0x08)) = 0;
        *(s16 *) (m + MTX_HALF(0x0A)) = 1;
        *(s32 *) (m + 0x0C) = 0;
        *(s32 *) (m + 0x10) = 0;
        *(s16 *) (m + MTX_HALF(0x14)) = 1;
        *(s16 *) (m + MTX_HALF(0x16)) = 0;
        *(s32 *) (m + 0x18) = 0;
        *(s16 *) (m + MTX_HALF(0x1C)) = 0;
        *(s16 *) (m + MTX_HALF(0x1E)) = 1;
        *(s32 *) (m + 0x20) = 0;
        *(s32 *) (m + 0x24) = 0;
        *(s32 *) (m + 0x28) = 0;
        *(s32 *) (m + 0x2C) = 0;
        *(s32 *) (m + 0x30) = 0;
        *(s32 *) (m + 0x34) = 0;
        *(s32 *) (m + 0x38) = 0;
        *(s32 *) (m + 0x3C) = 0;
        m += 0x40;
    }
    gGroupTris = m;
    if (n != 0) {
        u8 *src = hdr + 4;
        s32 tag;

        do {
            s32 count;

            tag = *(s16 *) src;
            count = *(s32 *) (src + 0x28);
            src += 0x2C;
            while (count != 0) {
#ifdef PORT_HOST
                /* bytes 0x25-0x27 are never written: keep the N64's stale
                 * bytes (port_n64_byte, see PORT_STALE_51 below) */
                void port_unit_mark(void *p, u32 n, s32 width);
                u8 port_n64_byte(const void *p);
                void port_garbage(const void *p, u32 len);
                u8 old[3];
                s32 k;

                for (k = 0; k < 3; k++) {
                    old[k] = port_n64_byte(m + 0x25 + k);
                }
#endif
                count--;
                *(s32 *) (m + 0x00) = *(s16 *) (src + 0x00) << 5;
                *(s32 *) (m + 0x04) = *(s16 *) (src + 0x02) << 5;
                *(s32 *) (m + 0x08) = *(s16 *) (src + 0x04) << 5;
                *(s32 *) (m + 0x0C) = *(s16 *) (src + 0x06) << 5;
                *(s32 *) (m + 0x10) = *(s16 *) (src + 0x08) << 5;
                *(s32 *) (m + 0x14) = *(s16 *) (src + 0x0A) << 5;
                *(s32 *) (m + 0x18) = *(s16 *) (src + 0x0C) << 5;
                *(s32 *) (m + 0x1C) = *(s16 *) (src + 0x0E) << 5;
                *(s32 *) (m + 0x20) = *(s16 *) (src + 0x10) << 5;
                m[0x24] = src[0x12];
#ifdef PORT_HOST
                for (k = 0; k < 3; k++) {
                    m[0x25 + k] = old[k];
                }
                port_unit_mark(m, 9, 4);
                port_unit_mark(m + 0x24, 4, 1);
                port_garbage(m + 0x25, 3);
#endif
                src += 0x44;
                m += 0x28;
            }
        } while (tag != -1);
        *s1io = 0;
    }
    gGroupTrisEnd = m;
    gHeapPtr = m;
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/5CB60/lvl_build_part_tris.s")
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING

/* A record range's texture ids into addresses (0x802A1C20).
 * Resolves the texture ids of the level object's records
 * [OBJ_PTR(obj, 0x2C), OBJ_PTR(obj, 0x30)): each record has a count byte
 * at +4 and count - 1 words from +0xC, each replaced by
 * tex_get_impl(word, param) (its physical address); the next record follows
 * the last word. The walk stops only at exactly the end, and a count byte of
 * 0 loops (about 2^32 times) in the asm too, so both are preconditions.
 * The asm also sets s1 = 0x80000000 for every record (a leftover that its
 * caller initlevel hands on to lvl_build_coll_grid_a's in/out s1; modelled
 * through `s1io`) and leaves t6 = the last id.
 * Register convention: obj in t0, param in fp (conventions.txt); its add/addi
 * trap. Asm caller initlevel keeps t0, t9 live. */
void lvl_resolve_tex_ids(u8 *obj, u8 *param, s32 *s1io) {
    u8 *rec = OBJ_PTR(obj, 0x2C);
    u8 *end = OBJ_PTR(obj, 0x30);
    u32 *w;
    s32 n;

    while (rec != end) {
        n = rec[4];
        w = (u32 *) (rec + 0xC);
        *s1io = 0x80000000;
        n--;
        while (n != 0) {
            n--;
            *w = tex_get_impl(*w, param);
            w++;
        }
        rec = (u8 *) w;
    }
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/5CB60/lvl_resolve_tex_ids.s")
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING

/* Relocates the level's gSPDisplayList addresses (0x802A1C88).
 * Relocates the object's display lists: every gSPDisplayList (opcode 6) in
 * [OBJ_PTR(obj, 0x90), OBJ_PTR(obj, 0x84)) gets OBJ_PTR(obj, 0x78) - 0x80000000
 * added to its address word; then gLevelFileDl0/1/2/3 = OBJ_PTR(obj, 0x90/
 * 0x94/0x98/0x9C). The asm loops with `!=` over 8-byte commands.
 * asm: obj in t0; it saves every register it uses but at.
 * Asm callers rely on preserved: initlevel keeps t0, t6, t9, f12, f14. */
void lvl_relocate_dls(u8 *obj) {
    u32 reloc = (u32) OBJ_PTR(obj, 0x78) - 0x80000000;
    u32 *cmd = (u32 *) OBJ_PTR(obj, 0x90);
    u32 *end = (u32 *) OBJ_PTR(obj, 0x84);

    while (cmd != end) {
        if ((cmd[0] >> 24) == 6) {
            cmd[1] += reloc;
        }
        cmd += 2;
    }
    gLevelFileDl0 = OBJ_PTR(obj, 0x90);
    gLevelFileDl1 = OBJ_PTR(obj, 0x94);
    gLevelFileDl2 = OBJ_PTR(obj, 0x98);
    gLevelFileDl3 = OBJ_PTR(obj, 0x9C);
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/5CB60/lvl_relocate_dls.s")
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
void lvl_pick_path_parts(void);
void lvl_load_object_table(void);
u8 *lvl_load_object(s32 index);
void lvl_end_seq_set_toughness(s32 kind, u8 *obj);
void lvl_translate_object(u8 *obj, s32 dx, s32 dy, s32 dz);
void lvl_tint_path_building_red(u8 *obj, s32 flag);
void lvl_add_object(s32 type, u8 *obj, s32 x, s32 y, s32 z, s32 flag, s32 tag, u8 *param, s32 *s1io);

/* The level's placed objects (gObjects) (0x802A1D54).
 * Level objects setup: gPeopleTotal = 0, lvl_pick_path_parts (per-level list), clear
 * a few flag bytes of the tables at gSinkingPart (1 x 0xA30: bytes 0xA2A/B),
 * gFadingParts (4 x 0x4B8: half 0x4B0, byte 0x4B2) and gTopplingParts (2 x 0x478:
 * bytes 0x470/0x472), gObjectsEnd = gObjects (record pool), lvl_load_object_table
 * (offset table). Then for each 14-byte placement in [OBJ_PTR(obj, 0x5C),
 * OBJ_PTR(obj, 0x60)) {BE u16 x, y, z, type; u8 flag, count; u16 at 0xA,
 * 0xC}: data = lvl_load_object(type) (load), lvl_end_seq_set_toughness(type, data),
 * tex_resolve_dl_impl over data's display list [OBJ_PTR(data, 0x10),
 * OBJ_PTR(data, 0x14)), data's u16 at 0xE = the u16 at 0xA,
 * lvl_translate_object(data, x, y, z), lvl_tint_path_building_red(data, flag), data[6] = count
 * and gPeopleTotal += count, then lvl_add_object(type, data, x, y, z, flag, the
 * u16 at 0xC, param). The asm loops with `!=`. Returns the list end (the
 * asm's t1 and t2).
 * Register convention: obj t0, param fp; result t1. The asm leaves s0, s2-s7
 * (and the libultra calls' f12/f14, lvl_add_object's f20-f28) changed; the
 * survey lists those as read by initlevel (dead there, not modelled).
 * s1 is modelled (`s1io`, unchanged without placements): per placement it is
 * the new gPeopleTotal sum (the register: old byte + count, not truncated),
 * then whatever lvl_add_object leaves; initlevel hands it on to
 * block_spawn_all's coll_add_dyn_tris.
 * Asm caller initlevel keeps t0 live. */
u8 *lvl_spawn_objects(u8 *obj, u8 *param, s32 *s1io) {
    Unk802A08E4Regs r;
    u8 *p;
    u8 *end;
    u8 *data;
    s32 type;
    s32 x;
    s32 y;
    s32 z;
    s32 n;

    gPeopleTotal = 0;
    lvl_pick_path_parts();
    for (p = gSinkingPart, n = 1; n != 0; n--, p += 0xA30) {
        p[0xA2A] = 0;
        p[0xA2B] = 0;
    }
    for (p = gFadingParts, n = 4; n != 0; n--, p += 0x4B8) {
        *(s16 *) (p + 0x4B0) = 0;
        p[0x4B2] = 0;
    }
    for (p = gTopplingParts, n = 2; n != 0; n--, p += 0x478) {
        p[0x470] = 0;
        p[0x472] = 0;
    }
    gObjectsEnd = gObjects;
    lvl_load_object_table();
    p = OBJ_PTR(obj, 0x5C);
    end = OBJ_PTR(obj, 0x60);
    while (p != end) {
        type = BE16U(p + 6);
        data = lvl_load_object(type);
        lvl_end_seq_set_toughness(type, data);
        tex_resolve_dl_impl((u32 *) OBJ_PTR(data, 0x10), (u32 *) OBJ_PTR(data, 0x14), &r);
        *(u16 *) (data + 0xE) = *(u16 *) (p + 0xA);
        x = BE16U(p + 0);
        y = BE16U(p + 2);
        z = BE16U(p + 4);
        lvl_translate_object(data, x, y, z);
        lvl_tint_path_building_red(data, p[8]);
        data[6] = p[9];
        *s1io = gPeopleTotal + p[9];
        gPeopleTotal = *s1io;
        lvl_add_object(type, data, x, y, z, p[8], *(u16 *) (p + 0xC), param, s1io);
        p += 0xE;
    }
    return p;
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/5CB60/lvl_spawn_objects.s")
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
extern u8 * N64P gPathPartsCursor;
extern u8 gPathPartsLvl00[], gPathPartsLvl01[], gPathPartsLvl02[], gPathPartsLvl03[], gPathPartsLvl04[];
extern u8 gPathPartsLvl05[], gPathPartsLvl09[], gPathPartsLvl0A[], gPathPartsLvl0C[], gPathPartsLvl0D[];
extern u8 gPathPartsLvl0E[], gPathPartsLvl0F[], gPathPartsLvl10[], gPathPartsLvl11[], gPathPartsLvl12[];
extern u8 gPathPartsLvl1A[], gPathPartsLvl1D[], gPathPartsLvl21[], gPathPartsLvl39[], gPathPartsLvl3A[];

/* The level's object list block (0x802A1EC8).
 * Picks the per-level data block for the current level levelno:
 * gPathParts = block, gPathPartsCursor = block + 4; both NULL for levels without
 * one.
 * Asm callers rely on preserved: lvl_spawn_objects keeps t0, f12, f14 (the asm
 * saves t2). */
void lvl_pick_path_parts(void) {
    u8 *p;

    switch (levelno) {
        case 0x00: p = gPathPartsLvl00; break;
        case 0x01: p = gPathPartsLvl01; break;
        case 0x02: p = gPathPartsLvl02; break;
        case 0x03: p = gPathPartsLvl03; break;
        case 0x04: p = gPathPartsLvl04; break;
        case 0x05: p = gPathPartsLvl05; break;
        case 0x09: p = gPathPartsLvl09; break;
        case 0x0A: p = gPathPartsLvl0A; break;
        case 0x0C: p = gPathPartsLvl0C; break;
        case 0x0D: p = gPathPartsLvl0D; break;
        case 0x0E: p = gPathPartsLvl0E; break;
        case 0x0F: p = gPathPartsLvl0F; break;
        case 0x10: p = gPathPartsLvl10; break;
        case 0x11: p = gPathPartsLvl11; break;
        case 0x12: p = gPathPartsLvl12; break;
        case 0x1A: p = gPathPartsLvl1A; break;
        case 0x1D: p = gPathPartsLvl1D; break;
        case 0x21: p = gPathPartsLvl21; break;
        case 0x39: p = gPathPartsLvl39; break;
        case 0x3A: p = gPathPartsLvl3A; break;
        default:
            gPathPartsCursor = NULL;
            gPathParts = NULL;
            return;
    }
    gPathParts = p;
    gPathPartsCursor = p + 4;
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/5CB60/lvl_pick_path_parts.s")
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
/* In game mode 0x800 (the intro fly-in), sets the vertex colours of a path building to red (0x802A20F4).
 * If flag is set and the game mode gNextGameMode is 0x800, sets bytes 0xC/0xD/0xE
 * of every 0x10-byte entry in [obj + 0x50, OBJ_PTR(obj, 0x1C)) to 0xFF, 0, 0
 * (the asm loops with `!=`).
 * asm: obj in t4, flag in s0; it changes no register but at.
 * Asm callers rely on preserved: lvl_spawn_objects keeps a2, a3, t1-t7, f12, f14. */
void lvl_tint_path_building_red(u8 *obj, s32 flag) {
    u8 *p;
    u8 *end;

    if (flag != 0 && gNextGameMode == MODE_INTRO_FLY) {
        end = OBJ_PTR(obj, 0x1C);
        for (p = obj + 0x50; p != end; p += 0x10) {
            p[0xC] = 0xFF;
            p[0xD] = 0;
            p[0xE] = 0;
        }
    }
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/5CB60/lvl_tint_path_building_red.s")
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
/* On the End Sequence level (0x31), sets the toughness of objects of kind 0x24 to 2 (0x802A2164).
 * On level 0x31, sets obj[4] = 2 for objects of kind 0x24.
 * asm: kind in t3, obj in s0; it changes no register but at.
 * Asm callers rely on preserved: lvl_spawn_objects keeps a2, a3, t1-t3, f12, f14. */
void lvl_end_seq_set_toughness(s32 kind, u8 *obj) {
    if (levelno == 0x31 && kind == 0x24) {
        obj[4] = 2;
    }
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/5CB60/lvl_end_seq_set_toughness.s")
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
void lvl_resolve_object_tex(u8 *obj, u8 *param, s32 *s1io);
void lvl_object_centre(u8 *rec, u8 *obj);
void lvl_init_object_shadow(u8 *rec, u8 *param);
void lvl_remap_break_fx_kinds(u8 *rec);

#define REC_W(rec, off) (*(s32 *) ((rec) + (off)))

/* One gObjects record and its collision triangles (0x802A21AC).
 * Set up a placed level object of `type` from its loaded data obj at (x, y, z)
 * (map units, << 5 into the record): lvl_resolve_object_tex(obj, param) resolves its
 * ids; type 0x38 also stores x/y/z in gEndObjectX/Y/Z. Takes the next
 * 0xFC-byte record from gObjectsEnd: lvl_object_centre (centre), word 0 = obj, 0x30
 * = type, 0x34 = tag, 0xC = obj's u16 at 2 << 5, 0x10/0x1C = x, 0x14/0x20 =
 * y, 0x18/0x24 = z (<< 5), 0x38..0x40 = 0, bytes 0xEA = 0, 0xEB = flag, 0xE9 =
 * n (obj's u16 at 0) and n zero bytes at 0xEC, n zero halves at 0x48 and at
 * 0x68. With the per-level list gPathPartsCursor set and flag != 0 the record is
 * appended to it (stride 0x18). Byte 0xE8 = grid cell (z / (gLevelCellD >>
 * 5)) * gLevelCols + x / (gLevelCellW >> 5) (unsigned). Then lvl_init_object_shadow(rec,
 * param), lvl_remap_break_fx_kinds(rec), and the 0x19-byte triangle entries
 * [OBJ_PTR(obj, 0x48), OBJ_PTR(obj, 0x4C)) are built at the heap gHeapPtr
 * with lvl_build_coll_tri (byte 0x51 of the current output = entry byte 0x17 == 0;
 * the asm passes &gObjectsEnd, left in v0, as the id); word 4 / 8 = the heap
 * before / after, gHeapPtr = the end. The asm's addi trap; the divides trap
 * on a zero cell size.
 * Register convention: type t3, obj t4, x t5, y t6, z t7, flag s0, tag t9,
 * param fp (conventions.txt). The asm restores t0-t2, sets s0 = 1, leaves
 * gp (the last entry's byte 0x18), lvl_build_coll_tri's s2-s7 and lvl_init_object_shadow's
 * f20-f28 changed (listed as read by lvl_spawn_objects; dead there). s1 is
 * modelled (`s1io`): lvl_resolve_object_tex's, then per entry its byte 0x16 into
 * lvl_build_coll_tri's chain; the last value reaches coll_add_dyn_tris through
 * lvl_spawn_objects / initlevel / block_spawn_all. */
void lvl_add_object(s32 type, u8 *obj, s32 x, s32 y, s32 z, s32 flag, s32 tag, u8 *param, s32 *s1io) {
    u8 *rec;
    u8 *list;
    s32 n;
    s32 i;
    u8 *e;
    u8 *eEnd;
    u8 *out;

    lvl_resolve_object_tex(obj, param, s1io);
    if (type == OBJ_KIND_END) {
        gEndObjectX = x;
        gEndObjectY = y;
        gEndObjectZ = z;
    }
    rec = gObjectsEnd;
    gObjectsEnd = rec + 0xFC;
    lvl_object_centre(rec, obj);
    REC_W(rec, 0x34) = tag;
    REC_W(rec, 0x38) = 0;
    REC_W(rec, 0x3C) = 0;
    REC_W(rec, 0x40) = 0;
    REC_W(rec, 0x30) = type;
    rec[0xEB] = flag;
    rec[0xEA] = 0;
    *(u8 * N64P *) rec = obj;
    REC_W(rec, 0xC) = *(u16 *) (obj + 2) << 5;
    REC_W(rec, 0x10) = x << 5;
    REC_W(rec, 0x1C) = x << 5;
    REC_W(rec, 0x14) = y << 5;
    REC_W(rec, 0x20) = y << 5;
    REC_W(rec, 0x18) = z << 5;
    REC_W(rec, 0x24) = z << 5;
    n = *(u16 *) obj;
    rec[0xE9] = n;
    for (i = 0; i < n; i++) {
        rec[0xEC + i] = 0;
    }
    for (i = 0; i < n; i++) {
        *(s16 *) (rec + 0x48 + i * 2) = 0;
    }
    for (i = 0; i < n; i++) {
        *(s16 *) (rec + 0x68 + i * 2) = 0;
    }
    list = gPathPartsCursor;
    if (list != NULL && flag != 0) {
        *(u8 * N64P *) list = rec;
        gPathPartsCursor = list + 0x18;
    }
    rec[0xE8] = ((u32) z / ((u32) gLevelCellD >> 5)) * ((u16) gLevelCols) + (u32) x / ((u32) gLevelCellW >> 5);
    lvl_init_object_shadow(rec, param);
    lvl_remap_break_fx_kinds(rec);
    e = OBJ_PTR(obj, 0x48);
    eEnd = OBJ_PTR(obj, 0x4C);
    out = gHeapPtr;
    REC_W(rec, 4) = (s32) out;
    while (e != eEnd) {
        out[0x51] = (e[0x17] != 0) ? 0 : 1;
        *s1io = e[0x16];
        out = lvl_build_coll_tri(out, e, (s32) &gObjectsEnd, e[0x15], e[0x17], e[0x14], s1io, tag, e[0x18]);
        e += 0x19;
    }
    REC_W(rec, 8) = (s32) out;
    gHeapPtr = out;
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/5CB60/lvl_add_object.s")
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
extern u8 gObjBreakFxRemap[];

/* Remaps the effect kind (byte 0x31) of each of an object's 0x38-byte break-effect records through the table
 * gObjBreakFxRemap (0x802A23E0).
 * rec[0] points at an object; remaps byte 0x31 of every 0x38-byte entry in
 * [OBJ_PTR(obj, 0x30), OBJ_PTR(obj, 0x38)) through the byte table gObjBreakFxRemap
 * (the asm loops with `!=`).
 * asm: rec in v1; it changes no register.
 * Asm callers rely on preserved: lvl_add_object keeps a2, a3, t4, t9, f12, f14. */
void lvl_remap_break_fx_kinds(u8 *rec) {
    u8 *obj = *(u8 * N64P *) rec;
    u8 *p = OBJ_PTR(obj, 0x30);
    u8 *end = OBJ_PTR(obj, 0x38);

    while (p != end) {
        p[0x31] = gObjBreakFxRemap[p[0x31]];
        p += 0x38;
    }
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/5CB60/lvl_remap_break_fx_kinds.s")
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
/* An object's box centre (0x802A2458).
 * rec's words at 0x28 / 0x2C = the midpoints (x0 + x3) / 2 and (z0 + z6) / 2
 * (arithmetic shift), << 5, of the s16 array at OBJ_PTR(obj, 0x1C).
 * asm: rec in v1, obj in t4; it changes no register.
 * Asm callers rely on preserved: lvl_add_object keeps a2, a3, t3-t7, t9, f12, f14. */
void lvl_object_centre(u8 *rec, u8 *obj) {
    s16 *v = (s16 *) OBJ_PTR(obj, 0x1C);

    *(s32 *) (rec + 0x28) = ((v[0] + v[3]) >> 1) << 5;
    *(s32 *) (rec + 0x2C) = ((v[2] + v[8]) >> 1) << 5;
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/5CB60/lvl_object_centre.s")
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING

/* For shadow-casting objects (types 0xBA..0xBC), loads the blob-shadow texture 0xF81 and stores the ground
 * height under the object (0x802A24BC).
 * For a record whose type (word 0x30) is 0xBA, 0xBB or 0xBC: gObjShadowTex =
 * tex_get_impl(0xF81, param) and word 0x44 = the ground height
 * coll_ground_height(word 0x10, word 0x18, word 0x14).
 * Register convention: rec in v1, param in fp (conventions.txt); the asm
 * saves and restores every integer register (f20-f28 are left as
 * coll_ground_height leaves them). Asm callers rely on preserved: lvl_add_object
 * keeps t4, t9 (and is listed as reading f12, f14, f20-f26 after it). */
void lvl_init_object_shadow(u8 *rec, u8 *param) {
    s32 type = *(s32 *) (rec + 0x30);

    if (type == OBJ_KIND_SHADOW_A || type == OBJ_KIND_SHADOW_B || type == OBJ_KIND_SHADOW_C) {
        gObjShadowTex = tex_get_impl(0xF81, param);
        *(s32 *) (rec + 0x44) = coll_ground_height(*(s32 *) (rec + 0x10), *(s32 *) (rec + 0x18), *(s32 *) (rec + 0x14));
    }
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/5CB60/lvl_init_object_shadow.s")
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING

/* The texture ids of an object's records (0x802A2608).
 * Like lvl_resolve_tex_ids for the records [OBJ_PTR(obj, 0x28), OBJ_PTR(obj, 0x2C)):
 * each record's word 0 is replaced by tex_get_impl(word, param), then (the
 * count byte at +4 read after that call) count - 1 words from +0x10 likewise;
 * the next record follows the last word. Same preconditions (exact end,
 * count >= 1).
 * Register convention: obj in t4, param in fp (conventions.txt); the asm saves
 * t3, t5-t7, s0, t9, leaves t1/t2 changed and s1 = 0x80000000 when there is
 * a record (`s1io`; it can reach coll_add_dyn_tris through lvl_add_object and
 * lvl_spawn_objects); its addi trap. Asm caller lvl_add_object keeps t3-t7, t9
 * live. */
void lvl_resolve_object_tex(u8 *obj, u8 *param, s32 *s1io) {
    u8 *rec = OBJ_PTR(obj, 0x28);
    u8 *end = OBJ_PTR(obj, 0x2C);
    u32 *w;
    s32 n;

    while (rec != end) {
        *(u32 *) rec = tex_get_impl(*(u32 *) rec, param);
        *s1io = 0x80000000;
        n = rec[4];
        w = (u32 *) (rec + 0x10);
        n--;
        while (n != 0) {
            n--;
            *w = tex_get_impl(*w, param);
            w++;
        }
        rec = (u8 *) w;
    }
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/5CB60/lvl_resolve_object_tex.s")
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
/* Moves an object's data by (dx, dy, dz) (0x802A26A8).
 * Adds (dx, dy, dz) to (x, y, z) triples, per pattern, in arrays
 * of a level object (all loops use `!=` ends):
 *  s16 y values in [0x40, 0x44); the s16 (x, z, x, z) at 0x20;
 *  0x14-byte entries of three s16 (x, y, z) in [0x24, 0x28);
 *  four s16 triples at 0x1C; 0x10-byte entries in [obj + 0x50, 0x1C) unless
 *  the u16 at obj + 0xE is set; 0x19-byte entries of three unaligned big-
 *  endian s16 triples in [0x48, 0x4C); 0x38-byte entries in [0x30, 0x34) and
 *  [0x34, 0x38) whose words 0/1/2 become (w + dx/dy/dz) << 16 and word 10
 *  (w + dy) << 16; 8-byte entries of an s16 triple in [0x2C, 0x30).
 * asm: obj in t4, dx/dy/dz in t5/t6/t7; it changes no register but at.
 * Asm callers rely on preserved: lvl_spawn_objects keeps a2, a3, t1-t7, f12, f14. */
void lvl_translate_object(u8 *obj, s32 dx, s32 dy, s32 dz) {
    s16 *h;
    s16 *hend;
    u8 *b;
    u8 *bend;
    s32 *w;
    s32 *wend;
    s32 i;
    s32 k;
    s32 d[3];

    d[0] = dx;
    d[1] = dy;
    d[2] = dz;

    h = (s16 *) OBJ_PTR(obj, 0x40);
    hend = (s16 *) OBJ_PTR(obj, 0x44);
    for (; h != hend; h++) {
        h[0] += dy;
    }

    h = (s16 *) OBJ_PTR(obj, 0x20);
    h[0] += dx;
    h[1] += dz;
    h[2] += dx;
    h[3] += dz;

    h = (s16 *) OBJ_PTR(obj, 0x24);
    hend = (s16 *) OBJ_PTR(obj, 0x28);
    for (; h != hend; h += 10) {
        for (k = 0; k < 9; k++) {
            h[k] += d[k % 3];
        }
    }

    h = (s16 *) OBJ_PTR(obj, 0x1C);
    for (i = 4; i != 0; i--) {
        h[0] += dx;
        h[1] += dy;
        h[2] += dz;
        h += 3;
    }

    if (*(u16 *) (obj + 0xE) == 0) {
        h = (s16 *) (obj + 0x50);
        hend = (s16 *) OBJ_PTR(obj, 0x1C);
        for (; h != hend; h += 8) {
            h[0] += dx;
            h[1] += dy;
            h[2] += dz;
        }
    }

    b = OBJ_PTR(obj, 0x48);
    bend = OBJ_PTR(obj, 0x4C);
    for (; b != bend; b += 0x19) {
        for (k = 0; k < 9; k++) {
            s32 v = BE16S(b + k * 2) + d[k % 3];

            b[k * 2] = v >> 8;
            b[k * 2 + 1] = v;
        }
    }

    for (i = 0x30; i != 0x38; i += 4) {
        w = (s32 *) OBJ_PTR(obj, i);
        wend = (s32 *) OBJ_PTR(obj, i + 4);
        for (; w != wend; w += 14) {
            w[0] = (w[0] + dx) << 16;
            w[1] = (w[1] + dy) << 16;
            w[2] = (w[2] + dz) << 16;
            w[10] = (w[10] + dy) << 16;
        }
    }

    h = (s16 *) OBJ_PTR(obj, 0x2C);
    hend = (s16 *) OBJ_PTR(obj, 0x30);
    for (; h != hend; h += 4) {
        h[0] += dx;
        h[1] += dy;
        h[2] += dz;
    }
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/5CB60/lvl_translate_object.s")
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
extern u8 * N64P gPackedObjOffsets; /* table of u32 offsets into the compressed block at ROM 0x6EC4C0 */
u32 lvl_align_end(u32 x);

/* The load shared by lvl_load_object / lvl_load_model_plain / lvl_load_model: DMAs
 * `size` bytes from ROM `rom` into the buffer 0x8021ED00 (cache invalidated
 * first, waiting for completion), decompresses two streams from it to the
 * heap gHeapPtr (kiunzip_entry twice, window 0x8004B400), rounds the heap
 * end up (lvl_align_end, also left in *end: the asm's a1), and returns the
 * old heap pointer (the start of the data) after moving gHeapPtr past it. */
static u8 *port_load_packed(u32 rom, u32 size, u8 **end) {
    u8 *src = (u8 *) 0x8021ED00;
    u8 *dst = gHeapPtr;
    u8 *old;

    osInvalDCache(src, size);
    osPiStartDma(&gDmaIoMesg, OS_MESG_PRI_NORMAL, OS_READ, rom, src, size, &gDmaMesgQueue);
    osRecvMesg(&gDmaMesgQueue, NULL, OS_MESG_BLOCK);
    kiunzip_entry(&src, &dst, 0x8004B400);
    kiunzip_entry(&src, &dst, 0x8004B400);
#ifdef PORT_HOST
    {
        /* Windows port: byte order on load (port/src/load/port_load.h) */
        void port_on_load(u32 rom, u32 dst, u32 len);

        port_on_load(rom, (u32) gHeapPtr, (u32) (dst - gHeapPtr));
    }
#endif
    dst = (u8 *) lvl_align_end((u32) dst);
    old = gHeapPtr;
    gHeapPtr = dst;
    *end = dst;
    return old;
}

/* A packed object by index (0x802A2A98).
 * Loads packed object `index`: port_load_packed of ROM 0x6EC4C0 + tbl[index]
 * .. + tbl[index + 1] (tbl = gPackedObjOffsets).
 * Register convention: index in t3, result in s0 (conventions.txt); the asm
 * saves t0-t6 and s1, leaves s2 (size) and s7 (buffer) changed and f12 /
 * f14 as the libultra calls leave them. Asm callers rely on preserved:
 * lvl_spawn_objects keeps t1, t2, t3. */
u8 *lvl_load_object(s32 index) {
    u32 *t = (u32 *) (gPackedObjOffsets + index * 4);
    u8 *end;

    return port_load_packed(0x6EC4C0 + t[0], t[1] - t[0], &end);
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/5CB60/lvl_load_object.s")
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
extern u8 * N64P gPackedObjOffsets;

/* The packed-object offset table (0x802A2BB0).
 * Takes 0x800 bytes from the heap gHeapPtr (start kept in gPackedObjOffsets) and
 * DMAs ROM 0x6EC4C0..+0x800 into it, waiting for completion.
 * asm: leaves the buffer address in s0 (unsaved; no caller reads it); its
 * survey "outputs" a2/a3/f12/f14 are just what the libultra calls leave.
 * Asm callers rely on preserved: lvl_spawn_objects keeps t0. */
void lvl_load_object_table(void) {
    u8 *buf = gHeapPtr;

    gPackedObjOffsets = buf;
    gHeapPtr = buf + 0x800;
    osInvalDCache(buf, 0x800);
    osPiStartDma(&gDmaIoMesg, OS_MESG_PRI_NORMAL, OS_READ, 0x6EC4C0, buf, 0x800, &gDmaMesgQueue);
    osRecvMesg(&gDmaMesgQueue, NULL, OS_MESG_BLOCK);
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/5CB60/lvl_load_object_table.s")
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING

/* The level header's alpha zones (0x802A2C54).
 * Unpacks the byte stream [OBJ_PTR(obj, 0x60), OBJ_PTR(obj, 0x64)): the first
 * byte goes to gDefaultVehAlpha, then each record (four big-endian u16, two bytes,
 * a name of byte-count n, two bytes) becomes a 0x24-byte gAlphaZones entry:
 * words 0/4/8/0xC = u16 << 5, byte 0x10, n at 0x13, the name at 0x15, the
 * last two bytes at 0x11/0x14 and 1 at 0x12. gAlphaZonesEnd = end of the entries.
 * The asm loops with `!=`, so the stream must end on a record boundary.
 * asm: obj in t0; it clobbers t1-t6.
 * Asm callers rely on preserved: initlevel keeps t0, f12, f14. */
void lvl_read_alpha_zones(u8 *obj) {
    u8 *p = OBJ_PTR(obj, 0x60);
    u8 *end = OBJ_PTR(obj, 0x64);
    u8 *e = gAlphaZones;
    u8 *name;
    u32 n;

    gDefaultVehAlpha = *p++;
    while (p != end) {
        *(s32 *) (e + 0x0) = BE16U(p + 0) << 5;
        *(s32 *) (e + 0x4) = BE16U(p + 2) << 5;
        *(s32 *) (e + 0x8) = BE16U(p + 4) << 5;
        *(s32 *) (e + 0xC) = BE16U(p + 6) << 5;
        e[0x10] = p[8];
        n = p[9];
        e[0x13] = n;
        p += 10;
        name = e + 0x15;
        while (n != 0) {
            *name++ = *p++;
            n--;
        }
        e[0x11] = p[0];
        e[0x14] = p[1];
        e[0x12] = 1;
        p += 2;
        e += 0x24;
    }
    gAlphaZonesEnd = e;
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/5CB60/lvl_read_alpha_zones.s")
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
extern u16 gCollRows;

/* Level setup from the level header obj: copies its sizes and bounds into
 * globals (u16 fields at 4/6/0xC/0xE/0x14/0x16 are stored << 5), points
 * gLevelSegment8/gGuardTris/gGuardTrisEnd/gRespawnPoints into it, marks four tables
 * empty (-1 bytes/words) and clears a set of per-level flags.
 * asm: obj in t0; it clobbers t1-t3 and f0.
 * Asm callers rely on preserved: initlevel keeps t0, t6, t9, f12, f14. */
void lvl_read_header(u8 *obj) {
    s16 *v;
    s32 i;

    gLevelGravity = *(s32 *) (obj + 0x18);
    gLevelSegment8 = obj + 0xC8;
    gGuardTris = OBJ_PTR(obj, 0x24);
    gGuardTrisEnd = OBJ_PTR(obj, 0x28);
    gLevelCols = *(u16 *) (obj + 0x0);
    gLevelRows = *(u16 *) (obj + 0x2);
    gLevelCellW = *(u16 *) (obj + 0x4) << 5;
    gLevelCellD = *(u16 *) (obj + 0x6) << 5;
    gGroundCols = *(u16 *) (obj + 0x8);
    gGroundRows = *(u16 *) (obj + 0xA);
    gGroundCellW = *(u16 *) (obj + 0xC) << 5;
    gGroundCellD = *(u16 *) (obj + 0xE) << 5;
    gCollCols = *(u16 *) (obj + 0x10);
    gCollRows = *(u16 *) (obj + 0x12);
    gCollCellW = *(u16 *) (obj + 0x14) << 5;
    gCollCellD = *(u16 *) (obj + 0x16) << 5;
    gMoveableTrisEnd = gMoveableTris;
    for (i = 0; i < 100; i++) {
        gVehPartSpheres[i * 0x14 + 0x13] = 0xFF;
    }
    for (i = 0; i < 12; i++) {
        gVehBoundSpheres[i * 0x14 + 0x11] = 0xFF;
    }
    for (i = 0; i < 12 * 4; i++) {
        gStandOn[i] = 0xFF;
    }
    for (i = 0; i < 26; i++) {
        *(s32 *) (gVehPoints + i * 0x10) = -1;
    }
    gRespawnPoints = OBJ_PTR(obj, 0x50);
    v = (s16 *) OBJ_PTR(obj, 0x4C);
    gBoundsMinX = v[0];
    gBoundsMinZ = v[1];
    gBoundsMaxX = v[2];
    gBoundsMaxZ = v[3];
    gCarrierContact = 0;
    gLastSpeed = 0;
    gEndObjectTouched = 0;
    gPathBuildingsDestroyed = 0;
    gFailTrigger = 0;
    gLevelDlProfile = *(s32 *) (obj + 0x1C);
    gTrainClosed = 0;
    gCarrierExplodeDone = 0;
    gVehSlowed = 0;
    gBlockedFrame = 0;
    gObjHitFrame = 0;
    gObjRockSign = 0;
    gObjRockRun = 0;
    gObjRockHit = 0;
    gPartSinking = 0;
    gVehAttacking = 0;
    gCollExitProbe = 0;
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/5CB60/lvl_read_header.s")
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
/* (Unk802A08E4Regs and tex_resolve_dl_impl are declared at the top of the file.) */

/* The level display list's SETTIMG texture ids (0x802A3008).
 * Resolves the G_SETTIMG texture ids in the level object's display list
 * [OBJ_PTR(obj, 0x78), OBJ_PTR(obj, 0x84)) with tex_resolve_dl_impl. Returns the
 * list end, which the asm leaves in s1 and its caller initlevel hands on
 * to lvl_build_coll_grid_a's in/out s1 when lvl_resolve_tex_ids doesn't overwrite it.
 * Register convention: obj in t0, result in s1 (conventions.txt); the asm
 * saves t0 and passes its caller's s4 through as tex_resolve_dl_impl's in/out s4
 * (`r` is a local here); s0, s2-s6 are left changed (dead in the caller).
 * Asm caller initlevel keeps t0, t6, t9 live. */
u8 *lvl_resolve_dl_textures(u8 *obj) {
    Unk802A08E4Regs r;
    u8 *end = OBJ_PTR(obj, 0x84);

    tex_resolve_dl_impl((u32 *) OBJ_PTR(obj, 0x78), (u32 *) end, &r);
    return end;
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/5CB60/lvl_resolve_dl_textures.s")
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING

/* The level's type-0xFF object: the 9-byte record at OBJ_PTR(obj, 0x54) (just
 * past lvl_spawn_vehicles's list) {u8 speed, BE s16 x, z, heading, limit}; when
 * its first byte is set, load model 0xFF (lvl_load_model) and set it up with
 * carrier_spawn(model, x << 5, z << 5, speed, limit << 5, heading, fp), then
 * gHaveCarrier = 1.
 * Register convention: obj in t0, fp (the previous object's leftover: here
 * what lvl_spawn_vehicles returns) (conventions.txt); the asm saves t0 and leaves
 * s1, s2, s4 and the setup's s0-s7, fp, gp and FP registers changed. Its add
 * traps (a heap address: in range). */
void lvl_spawn_carrier(u8 *obj, s32 fp) {
    u8 *p = OBJ_PTR(obj, 0x54);
    Out802A396C o;
    s32 speed = p[0];

    if (speed != 0) {
        lvl_load_model(0xFF, &o);
        carrier_spawn(o.s2, BE16S(p + 1) << 5, BE16S(p + 3) << 5, speed, BE16S(p + 7) << 5, BE16S(p + 5), fp);
        gHaveCarrier = 1;
    }
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/5CB60/lvl_spawn_carrier.s")
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING

/* If util_find_shuttle_path(levelno) (the current level has it): gHaveShuttle = 1,
 * load model 0xFD (lvl_load_model) and set up vehicle 0xFD with it
 * (shuttle_spawn). The asm saves t0 (not read) and leaves lvl_load_model's s2,
 * s4 and the setup's registers changed (conventions.txt: clobbers). */
void lvl_spawn_shuttle(void) {
    Out802A396C o;

    if (util_find_shuttle_path(levelno) != 0) {
        gHaveShuttle = 1;
        lvl_load_model(0xFD, &o);
        shuttle_spawn(o.s2);
    }
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/5CB60/lvl_spawn_shuttle.s")
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING

/* When the record at OBJ_PTR(obj, 0x54) (lvl_spawn_carrier's) has a nonzero first
 * byte and the game mode gLevelKind isn't 0x80: load model 0xFE
 * (lvl_load_model) and set up the flying vehicle with chopper_spawn(heap end,
 * model) - its a1 input is the heap end lvl_load_model leaves in a1 - then
 * gHaveChopper = 1.
 * Register convention: obj in t0 (conventions.txt); the asm saves t0 and
 * leaves s2, s4 and the setup's registers changed. Its add traps. */
void lvl_spawn_chopper(u8 *obj) {
    Out802A396C o;

    if (*OBJ_PTR(obj, 0x54) != 0 && ((s32) gLevelKind) != LEVEL_KIND_MISSION_TIMED) {
        lvl_load_model(0xFE, &o);
        chopper_spawn((s32) o.a1, o.s2);
        gHaveChopper = 1;
    }
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/5CB60/lvl_spawn_chopper.s")
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
extern u8 * N64P gGhostModel;

/* If a ghost is playing, loads the ghost vehicle's model, copies its matrices to the heap (padded with
 * identity matrices) and makes its display list translucent (0x802A3198).
 * If gGhostPlaying is set: load packed object gGhostModelType (lvl_load_model_plain; its s2,
 * s3, s4 results go to *out); gGhostModel = data, gGhostVtx =
 * OBJ_PTR(data, 0x14), gGhostDl = OBJ_PTR(data, 0x24), gGhostMtx = the heap
 * gHeapPtr. The block at OBJ_PTR(data, 0x18) is {s32 size, s32 bytes,
 * words[]}: its `bytes` bytes of words are copied to the heap, and the rest of
 * `size` is filled with 0x40-byte identity matrices (the asm loops with `!=`:
 * size - bytes must be a multiple of 0x40); gHeapPtr = the end. Then
 * ghost_make_translucent_dl(OBJ_PTR(data, 0x24), OBJ_PTR(data, 0x28)). The asm's adds
 * trap (heap addresses: in range).
 * Register convention: s2, s3, s4 out through `out` when loaded (in/out:
 * unchanged otherwise); the asm sets s0 = 1 then (clobbers s0) and saves t0
 * (asm caller initlevel keeps it). f12/f14 as the callees leave them are
 * listed as read by initlevel (not modelled). */
void lvl_load_ghost_model(Out802A32CC *out) {
    u8 *data;
    s32 *src;
    s32 *srcEnd;
    s32 *dst;
    s32 size;

    if (gGhostPlaying == 0) {
        return;
    }
    lvl_load_model_plain(gGhostModelType, out);
    data = out->s2;
    gGhostModel = data;
    gGhostVtx = OBJ_PTR(data, 0x14);
    gGhostDl = OBJ_PTR(data, 0x24);
    dst = (s32 *) gHeapPtr;
    gGhostMtx = (u8 *) dst;
    src = (s32 *) OBJ_PTR(data, 0x18);
    size = src[0];
    srcEnd = (s32 *) ((u8 *) (src + 2) + src[1]);
    src += 2;
    while (src != srcEnd) {
        *dst++ = *src++;
        size -= 4;
    }
    while (size != 0) {
        u8 *m = (u8 *) dst;

        *(s16 *) (m + MTX_HALF(0x00)) = 1;
        *(s16 *) (m + MTX_HALF(0x02)) = 0;
        *(s32 *) (m + 0x04) = 0;
        *(s16 *) (m + MTX_HALF(0x08)) = 0;
        *(s16 *) (m + MTX_HALF(0x0A)) = 1;
        *(s32 *) (m + 0x0C) = 0;
        *(s32 *) (m + 0x10) = 0;
        *(s16 *) (m + MTX_HALF(0x14)) = 1;
        *(s16 *) (m + MTX_HALF(0x16)) = 0;
        *(s32 *) (m + 0x18) = 0;
        *(s16 *) (m + MTX_HALF(0x1C)) = 0;
        *(s16 *) (m + MTX_HALF(0x1E)) = 1;
        *(s32 *) (m + 0x20) = 0;
        *(s32 *) (m + 0x24) = 0;
        *(s32 *) (m + 0x28) = 0;
        *(s32 *) (m + 0x2C) = 0;
        *(s32 *) (m + 0x30) = 0;
        *(s32 *) (m + 0x34) = 0;
        *(s32 *) (m + 0x38) = 0;
        *(s32 *) (m + 0x3C) = 0;
        dst += 0x10;
        size -= 0x40;
    }
    gHeapPtr = (u8 *) dst;
    ghost_make_translucent_dl((Gfx *) OBJ_PTR(data, 0x24), (Gfx *) OBJ_PTR(data, 0x28));
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/5CB60/lvl_load_ghost_model.s")
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
/* (Out802A32CC, the register results its asm caller reads: game/regs.h.) */

/* Lvl_load_model without the texture animations (0x802A32CC).
 * Loads the packed block of `type` (ROM ranges below; any other type hits
 * the asm's `syscall` debug trap and then runs as type 3, kept) with
 * port_load_packed and resolves the G_SETTIMG ids of its display list
 * [data + *(data + 0x1C), data + *(data + 0x20)) with tex_resolve_dl_impl (in/out
 * s4 = the ROM start). out: the data, tex_resolve_dl_impl's s3 / s4.
 * Register convention: type in t3; s2, s3, s4 through `out`
 * (conventions.txt). The asm saves t0-t7, s0, s1 and leaves s5-s7 changed
 * (and f12 / f14 as the callees leave them). */
void lvl_load_model_plain(s32 type, Out802A32CC *out) {
    u32 rom;
    u32 romEnd;
    u8 *end;
    u8 *data;
    Unk802A08E4Regs r;

    switch (type) {
        default: /* syscall */
        case VEH_SKYFALL:
            rom = 0x490AC0, romEnd = 0x491E00;
            break;
        case VEH_RAMDOZER:
            rom = 0x496AD0, romEnd = 0x497AF0;
            break;
        case VEH_BACKLASH:
            rom = 0x497AF0, romEnd = 0x4989E0;
            break;
        case VEH_AMDREAM:
            rom = 0x49BCE0, romEnd = 0x49C480;
            break;
        case VEH_JBOMB:
            rom = 0x49C480, romEnd = 0x49E8E0;
            break;
        case VEH_BALLISTA:
            rom = 0x49E8E0, romEnd = 0x49F7A0;
            break;
        case VEH_POLICE:
            rom = 0x49FF70, romEnd = 0x4A0720;
            break;
        case VEH_ATEAM:
            rom = 0x4A0720, romEnd = 0x4A1000;
            break;
        case VEH_STARSKI:
            rom = 0x4A1000, romEnd = 0x4A1690;
            break;
    }
    data = port_load_packed(rom, romEnd - rom, &end);
    r.s4 = (u8 *) rom;
    tex_resolve_dl_impl((u32 *) OBJ_PTR(data, 0x1C), (u32 *) OBJ_PTR(data, 0x20), &r);
    out->s2 = data;
    out->s3 = r.s3;
    out->s4 = r.s4;
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/5CB60/lvl_load_model_plain.s")
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
void lvl_find_start_vehicle(u8 *obj);

/* Spawn the level's vehicles and objects: lvl_find_start_vehicle(obj) (picks
 * gStartVehicle), gRiderPass = gHaveRideables = 0, then for each 9-byte record in
 * [OBJ_PTR(obj, 0x50), OBJ_PTR(obj, 0x54)) {u8 type, BE s16 x, y, z,
 * heading}: in game mode gNextGameMode == 2 (and == the u64 gModeGlobeSeqU64Spawn) only
 * records of type gRecVehicle are spawned (gModeGlobeSeqU64Spawn holds MODE_GLOBE_SEQ, so
 * the mode can't be both and this attract filter never runs: the asm's two `bne`s make an
 * "and" where initlevel's test of the same pair is an "or"); type 1 becomes the player's
 * vehicle gDiggerVehicle unless gLevelKind is 1 or 0x80; types 6, 7, 0xB, 0x11,
 * 0x12 set gHaveRideables. The model is loaded (lvl_load_model(type)) and the
 * type's setup runs with (x, y, z) << 5 and the heading (0 driver_spawn, 1
 * sideswipe_spawn, 2 thunderfist_spawn, 3 skyfall_spawn, 4 ramdozer_spawn, 5
 * backlash_spawn, 6 crane_spawn, 7 train_spawn, 8 amdream_spawn, 9
 * jbomb_spawn, 0xA ballista_spawn, 0xB/0x11/0x12 barge_spawn(type, ..),
 * 0xD police_spawn, 0xE ateam_spawn, 0xF starski_spawn, 0x10
 * cyclone_spawn). Any other type hits the asm's `syscall` debug trap, after
 * which it returns (the rest of the list is not spawned): return here.
 * The asm's adds trap (heap addresses: in range).
 * Leftover registers (the setups take them as by-value inputs):
 *  - fp: the ground byte veh_init_ground stores into gSlotSurface[] (and so
 *    veh+0x50) for a wheel slot whose scan finds no fp of its own. The first
 *    object gets the dispatcher's fp: initlevel's, i.e. lvl_build_object_groups's
 *    result (the gCollTris collision record end; its low byte). In the asm
 *    each later object gets whatever the previous setup left in fp (most
 *    end with anim_update_model -> anim_update_channel, which leaves byte 0x15, the
 *    interpolation mode, of the last active animation channel there; others
 *    a math_cos / scan result), and the dispatcher returns the last
 *    setup's (initlevel hands it to lvl_spawn_carrier). Traced in the
 *    original over the nine attract-demo levels (45 spawns, 16 setup types):
 *    every setup leaves 0 (linear channels), so here every object after the
 *    first gets 0, and 0 is returned once a setup has run (the C setups
 *    don't return their fp).
 *  - t6 (thunderfist_spawn, starski_spawn, cyclone_spawn: only passed through to
 *    their per-frame zone scans) and the FP state f12-f26 (only FP side
 *    results of veh_init_ground; police_spawn / ateam_spawn write f22-f26
 *    back, chained here): 0.
 * Register convention: obj in t0, fp in/out (conventions.txt). The asm saves
 * t0; the setups save t0-t5 and leave s0-s7, gp and the FP registers
 * changed. Asm caller initlevel keeps t0. */
s32 lvl_spawn_vehicles(u8 *obj, s32 fp) {
    SpawnFP f;
    Out802A396C o;
    u8 *p;
    u8 *end;
    s32 type;
    s32 x;
    s32 y;
    s32 z;
    s32 heading;

    f.f12 = 0.0f;
    f.f14 = 0.0f;
    f.f20 = 0.0f;
    f.f22 = 0.0f;
    f.f24 = 0.0f;
    f.f26 = 0.0f;
    lvl_find_start_vehicle(obj);
    gRiderPass = 0;
    gHaveRideables = 0;
    p = OBJ_PTR(obj, 0x50);
    end = OBJ_PTR(obj, 0x54);
    while (p != end) {
        type = p[0];
        if (gNextGameMode == MODE_ATTRACT && gNextGameMode == gModeGlobeSeqU64Spawn && gRecVehicle != type) {
            p += 9;
            continue;
        }
        if (type == VEH_SIDESWIPE && ((s32) gLevelKind) != LEVEL_KIND_MISSION && ((s32) gLevelKind) != LEVEL_KIND_MISSION_TIMED) {
            type = gDiggerVehicle;
        }
        if (type == VEH_CRANE || type == VEH_TRAIN || type == VEH_BARGE_0B || type == VEH_BARGE_11 || type == VEH_BARGE_12) {
            gHaveRideables = 1;
        }
        x = BE16S(p + 1) << 5;
        y = BE16S(p + 3) << 5;
        z = BE16S(p + 5) << 5;
        heading = BE16S(p + 7);
        p += 9;
        lvl_load_model(type, &o);
        switch (type) {
            case VEH_DRIVER:
                driver_spawn(o.s2, x, y, z, heading, fp, (TriSideOut *) &f);
                break;
            case VEH_SIDESWIPE:
                sideswipe_spawn(o.s2, x, y, z, heading, fp);
                break;
            case VEH_THUNDERFIST:
                thunderfist_spawn(o.s2, x, y, z, heading, fp, 0, (TriSideOut *) &f);
                break;
            case VEH_SKYFALL:
                skyfall_spawn(o.s2, x, y, z, heading, fp);
                break;
            case VEH_RAMDOZER:
                ramdozer_spawn(o.s2, x, y, z, heading, fp);
                break;
            case VEH_BACKLASH:
                backlash_spawn(o.s2, x, y, z, heading, fp);
                break;
            case VEH_CRANE:
                crane_spawn(o.s2, x, y, z, heading, fp);
                break;
            case VEH_TRAIN:
                train_spawn(x, y, z, heading, o.s2, fp);
                break;
            case VEH_AMDREAM:
                amdream_spawn(o.s2, x, y, z, heading, fp, (TriSideOut *) &f);
                break;
            case VEH_JBOMB:
                jbomb_spawn(x, y, z, heading, o.s2, fp);
                break;
            case VEH_BALLISTA:
                ballista_spawn(x, y, z, heading, o.s2, fp);
                break;
            case VEH_BARGE_0B:
            case VEH_BARGE_11:
            case VEH_BARGE_12:
                barge_spawn(type, x, y, z, heading, o.s2, fp);
                break;
            case VEH_POLICE:
                police_spawn(o.s2, x, y, z, heading, fp, (TriSideOut *) &f);
                break;
            case VEH_ATEAM:
                ateam_spawn(o.s2, x, y, z, heading, fp, (TriSideOut *) &f);
                break;
            case VEH_STARSKI:
                starski_spawn(o.s2, x, y, z, heading, fp, 0, (TriSideOut *) &f);
                break;
            case VEH_CYCLONE:
                cyclone_spawn(o.s2, x, y, z, heading, fp, 0, (TriSideOut *) &f);
                break;
            default: /* the asm's syscall, then its epilogue */
                return fp;
        }
        fp = 0; /* what the setup leaves in the asm's fp (see above) */
    }
    return fp;
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/5CB60/lvl_spawn_vehicles.s")
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING

/* The vehicle placed on the driver's start: gStartVehicle (0x802A3824).
 * 9-byte records in [OBJ_PTR(obj, 0x50), OBJ_PTR(obj, 0x54)): a kind byte and
 * three unaligned big-endian s16 (x, y, z). Finds the first record of kind 0
 * (searched from the start with no end bound: it must exist), then for every
 * nonzero-kind record at the same position sets gStartVehicle to its kind, or
 * to gDiggerVehicle unless gLevelKind is 1 or 0x80. The asm loops with `!=`.
 * asm: obj in t0; it changes no register but at.
 * Asm callers rely on preserved: lvl_spawn_vehicles keeps a1-a3, t0, f12, f14. */
void lvl_find_start_vehicle(u8 *obj) {
    u8 *p = OBJ_PTR(obj, 0x50);
    u8 *end = OBJ_PTR(obj, 0x54);
    u8 *r = p;
    s32 x;
    s32 y;
    s32 z;
    u8 kind;

    while (r[0] != VEH_DRIVER) {
        r += 9;
    }
    x = BE16S(r + 1);
    y = BE16S(r + 3);
    z = BE16S(r + 5);
    for (; p != end; p += 9) {
        kind = p[0];
        if (kind != VEH_DRIVER && BE16S(p + 1) == x && BE16S(p + 3) == y && BE16S(p + 5) == z) {
            if (((s32) gLevelKind) != LEVEL_KIND_MISSION && ((s32) gLevelKind) != LEVEL_KIND_MISSION_TIMED) {
                kind = gDiggerVehicle;
            }
            gStartVehicle = kind;
        }
    }
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/5CB60/lvl_find_start_vehicle.s")
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING

/* (Out802A396C is declared at the top of the file.) */

/* As lvl_load_model_plain for the object types below (any other type hits the
 * asm's `syscall` debug trap and then runs as type 0, kept), with
 * anim_tex_find_patches(dl, dlEnd, type) on the display list before tex_resolve_dl_impl.
 * out: the data, tex_resolve_dl_impl's s4 and the new heap end.
 * Register convention: type in t3; s2, s4, a1 through `out`
 * (conventions.txt). The asm saves t0-t7, s0, s1, s3 and leaves s5-s7
 * changed (and f12 / f14 as the callees leave them). Asm callers rely on
 * preserved: lvl_spawn_carrier keeps t1, t2, t4-t7; lvl_spawn_shuttle /
 * commpt_setup keep t1, t2; lvl_spawn_chopper keeps t1; lvl_spawn_vehicles keeps
 * t1-t3, t7. */
void lvl_load_model(s32 type, Out802A396C *out) {
    u32 rom;
    u32 romEnd;
    u8 *end;
    u8 *data;
    Unk802A08E4Regs r;

    switch (type) {
        default: /* syscall */
        case VEH_DRIVER:
            rom = 0x491E00, romEnd = 0x4929D0;
            break;
        case VEH_SIDESWIPE:
            rom = 0x4929D0, romEnd = 0x494390;
            break;
        case VEH_THUNDERFIST:
            rom = 0x494390, romEnd = 0x496AD0;
            break;
        case VEH_CYCLONE:
            rom = 0x4A1690, romEnd = 0x4A4120;
            break;
        case VEH_SKYFALL:
            rom = 0x490AC0, romEnd = 0x491E00;
            break;
        case VEH_RAMDOZER:
            rom = 0x496AD0, romEnd = 0x497AF0;
            break;
        case VEH_BACKLASH:
            rom = 0x497AF0, romEnd = 0x4989E0;
            break;
        case VEH_CRANE:
            rom = 0x49AD20, romEnd = 0x49B630;
            break;
        case VEH_TRAIN:
            rom = 0x49B630, romEnd = 0x49BCE0;
            break;
        case VEH_AMDREAM:
            rom = 0x49BCE0, romEnd = 0x49C480;
            break;
        case VEH_JBOMB:
            rom = 0x49C480, romEnd = 0x49E8E0;
            break;
        case VEH_BALLISTA:
            rom = 0x49E8E0, romEnd = 0x49F7A0;
            break;
        case VEH_BARGE_0B:
        case VEH_BARGE_11:
        case VEH_BARGE_12:
            rom = 0x49F7A0, romEnd = 0x49FF70;
            break;
        case VEH_POLICE:
            rom = 0x49FF70, romEnd = 0x4A0720;
            break;
        case VEH_ATEAM:
            rom = 0x4A0720, romEnd = 0x4A1000;
            break;
        case VEH_STARSKI:
            rom = 0x4A1000, romEnd = 0x4A1690;
            break;
        case VEH_CHOPPER:
            rom = 0x4989E0, romEnd = 0x499690;
            break;
        case VEH_CARRIER:
            rom = 0x499690, romEnd = 0x49AD20;
            break;
        case VEH_SHUTTLE:
            rom = 0x4A4120, romEnd = 0x4A5660;
            break;
        case 0x96:
            rom = 0x4903C0, romEnd = 0x490AC0;
            break;
        case 0x98:
            rom = 0x48FE90, romEnd = 0x4903C0;
            break;
    }
    data = port_load_packed(rom, romEnd - rom, &end);
    anim_tex_find_patches(OBJ_PTR(data, 0x1C), OBJ_PTR(data, 0x20), type);
    r.s4 = (u8 *) rom;
    tex_resolve_dl_impl((u32 *) OBJ_PTR(data, 0x1C), (u32 *) OBJ_PTR(data, 0x20), &r);
    out->s2 = data;
    out->s4 = r.s4;
    out->a1 = end;
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/5CB60/lvl_load_model.s")
#endif

/* lvl_build_coll_grid_a .. lvl_build_object_groups: build the level's collision triangle
 * records (lvl_build_coll_tri, 0x60 bytes each) at the heap gHeapPtr from the
 * level object's packed data. Several of lvl_build_coll_tri's byte arguments are
 * whatever the asm happens to hold in that register (loop counters, low
 * bytes of pointers); the rewrites pass the same values.
 * Register convention (conventions.txt): obj in t0, triangle id in v0, kind
 * byte in t9, and the in/out s1 that lvl_build_coll_tri threads from one call to
 * the next (through `s1io`). The asm saves t0 only, sets gp (0 or 1, passed
 * as lvl_build_coll_tri's b55) and leaves lvl_build_coll_tri's s2-s7 scratch. Its asm
 * caller initlevel keeps t0, t9, f12, f14 live. */
#ifdef NON_MATCHING
s32 lvl_has_tri_id(s32 id, u8 *end);

#if defined(NON_MATCHING) && defined(PORT_HOST)
/* Windows port: the triangle builders below (the grids, lvl_build_switch_groups,
 * lvl_build_object_groups) never set a record's byte 0x51 (the "collides" flag that
 * 56040.c's scans test), and no builder sets bytes 0x5A-0x5F, so the original
 * reads (and leaves) whatever the heap held there: on the N64 a big-endian
 * byte of the previous level's triangle records or assets (attract demo 1
 * disables some of its grid triangles that way).  The host holds the same
 * values in host order; port_n64_byte (port/src/load/swap.c) gives the byte
 * the N64 would hold from the unit the load layer, or lvl_build_coll_tri for its
 * records (port_tri_mark), last put there.  Memory other code wrote since is
 * misread -- only in these never-initialised bytes. */
void port_unit_mark(void *p, u32 n, s32 width);
u8 port_n64_byte(const void *p);
void port_garbage(const void *p, u32 len);

/* the layout of a record lvl_build_coll_tri built (CollisionTri, 77E20.c) */
static void port_tri_mark(u8 *rec) {
    port_unit_mark(rec, 4, 8);
    port_unit_mark(rec + 0x20, 11, 4);
    port_unit_mark(rec + 0x4C, 1, 2);
    port_unit_mark(rec + 0x4E, 4, 1);
    port_unit_mark(rec + 0x52, 1, 2);
    port_unit_mark(rec + 0x54, 12, 1);
}

#define PORT_STALE_51(rec) ((rec)[0x51] = port_n64_byte((rec) + 0x51))
#else
#define PORT_STALE_51(rec)
#endif

/* Grid of (s16) obj[0x10] * (s16) obj[0x12] cells (must be >= 1: the asm
 * counts down with `!=`) at OBJ_PTR(obj, off): each cell is an unaligned BE
 * word (end of its triangles, relative to the grid) followed by 0x16-byte
 * packed triangles. table[cell] = the cell's first record (table[cells] =
 * the end). Per triangle: lvl_build_coll_tri(rec, tri, id, h52 = cells left,
 * b57 = the cell end pointer, b56 = tri[0x14], s1io, b4F, 0), then
 * rec[0x59] = tri[0x15]. Returns the last cell's end pointer (the asm's t6). */
static u8 *port_build_grid(u8 *obj, s32 off, u8 * N64P *table, s32 id, s32 b4F, s32 *s1io) {
    u8 *base = OBJ_PTR(obj, off);
    u8 *t = base;
    u8 *cellEnd;
    u8 *rec = gHeapPtr;
    s32 cells = *(s16 *) (obj + 0x10) * *(s16 *) (obj + 0x12);

    do {
        *table++ = rec;
        cellEnd = base + BE32(t);
        t += 4;
        while (t != cellEnd) {
            PORT_STALE_51(rec);
            rec = lvl_build_coll_tri(rec, t, id, cells, (s32) cellEnd, t[0x14], s1io, b4F, 0);
            rec[-7] = t[0x15];
            t += 0x16;
        }
        cells--;
    } while (cells != 0);
    *table = rec;
    gHeapPtr = rec;
    return cellEnd;
}

/* The header's first collision grid (0x802A3D54).
 * Grid at obj word 0x6C, cell table gCollCellsA. */
u8 *lvl_build_coll_grid_a(u8 *obj, s32 id, s32 b4F, s32 *s1io) {
    return port_build_grid(obj, 0x6C, gCollCellsA, id, b4F, s1io);
}
#else
/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/5CB60/lvl_build_coll_grid_a.s")
#endif

#ifdef NON_MATCHING
/* The header's second collision grid (0x802A3DF8).
 * Grid at obj word 0x70, cell table gCollCellsB (otherwise lvl_build_coll_grid_a). */
u8 *lvl_build_coll_grid_b(u8 *obj, s32 id, s32 b4F, s32 *s1io) {
    return port_build_grid(obj, 0x70, gCollCellsB, id, b4F, s1io);
}
#else
/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/5CB60/lvl_build_coll_grid_b.s")
#endif

#ifdef NON_MATCHING
extern u8 * N64P gSwitchGroupsEnd;  /* end of the groups */

/* The switch groups (0x802A3E9C).
 * Named triangle groups in [OBJ_PTR(obj, 0x64), OBJ_PTR(obj, 0x68)): a tag
 * byte, a name (length byte + bytes), a count byte, then that many 0x14-byte
 * packed triangles. Each becomes a 0xFC-byte group in gSwitchGroups: [0] = name
 * length, [1..] = name, [8 + 4i] = record of triangle i, [0xF8] = count,
 * [0xF9] = tag. Records start at the heap gHeapPtr (also kept in
 * gSwitchTris; the end goes to gSwitchTrisEnd and the heap pointer); the group
 * end to gSwitchGroupsEnd. Per triangle: lvl_build_coll_tri(rec, tri, id, h52 = the
 * end pointer, b57 = triangles left after this one, b56 = the next pointer
 * slot, s1io, b4F, 1). The asm loops with `!=` / count-downs. */
void lvl_build_switch_groups(u8 *obj, s32 id, s32 b4F, s32 *s1io) {
    u8 *p = OBJ_PTR(obj, 0x64);
    u8 *end = OBJ_PTR(obj, 0x68);
    u8 *grp = gSwitchGroups;
    u8 *rec = gHeapPtr;
    u8 * N64P *slot;
    u8 *q;
    s32 n;

    gSwitchTris = rec;
    while (p != end) {
        grp[0xF9] = p[0];
        n = p[1];
        p += 2;
        grp[0] = n;
        for (q = grp + 1; n != 0; n--) {
            *q++ = *p++;
        }
        n = *p++;
        grp[0xF8] = n;
        slot = (u8 * N64P *) (grp + 8);
        while (n != 0) {
            n--;
            *slot++ = rec;
            PORT_STALE_51(rec);
            rec = lvl_build_coll_tri(rec, p, id, (s32) end, n, (s32) slot, s1io, b4F, 1);
            p += 0x14;
        }
        grp += 0xFC;
    }
    gSwitchGroupsEnd = grp;
    gSwitchTrisEnd = rec;
    gHeapPtr = rec;
}
#else
/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/5CB60/lvl_build_switch_groups.s")
#endif

#ifdef NON_MATCHING

/* The header's object groups (0x802A3F80).
 * Object groups in [OBJ_PTR(obj, 0x68), OBJ_PTR(obj, 0x6C)), each becoming a
 * 0xDC-byte group in gVehGates: bytes [0xC4..0xC6] = a, kind, n from the
 * data. Kind 0: n (>= 1) sets of six BE s16 (each << 5, as words; the third
 * of the last set is also left in s1, i.e. *s1io) and then a BE word; other
 * kinds: n (>= 1) BE s16 as halfwords. Then a count of 0x15-byte packed
 * triangles: [0xC7] = count, [0xC8 + i] = triangle i's id (its byte 0x14);
 * a record is built (lvl_build_coll_tri at the gCollTris cursor, b4F 0, b55 1)
 * only for ids no record before this group has (lvl_has_tri_id). Then a
 * length byte and that many bytes at [0xD0], [0xD1..]. h52 is the end
 * pointer, b57 the triangles left, b56 the last value read in the first part
 * (the BE word / the last s16). The new record end goes to gCollTrisEnd and is
 * returned (the asm's fp); the group end to gVehGatesEnd. */
u8 *lvl_build_object_groups(u8 *obj, s32 *s1io) {
    u8 *p = OBJ_PTR(obj, 0x68);
    u8 *end = OBJ_PTR(obj, 0x6C);
    u8 *grp = gVehGates;
    u8 *recEnd = gCollTris;
    u8 *rec;
    u8 *q;
    s32 *w;
    s32 last;
    s32 kind;
    s32 n;
    s32 id;

    while (p != end) {
        w = (s32 *) grp;
        grp[0xC4] = p[0];
        kind = p[1];
        grp[0xC5] = kind;
        n = p[2];
        grp[0xC6] = n;
        p += 3;
        if (kind == 0) {
            do {
                w[0] = BE16S(p + 0) << 5;
                w[1] = BE16S(p + 2) << 5;
                w[2] = *s1io = BE16S(p + 4) << 5;
                w[3] = BE16S(p + 6) << 5;
                w[4] = BE16S(p + 8) << 5;
                w[5] = BE16S(p + 10) << 5;
                n--;
                p += 12;
                w += 6;
            } while (n != 0);
            last = BE32(p);
            p += 4;
            *w = last;
        } else {
            do {
                last = BE16S(p);
#ifdef PORT_HOST
                /* read back as (channel, state) byte pairs (56040.c
                 * veh_gate_anim_test): keep the N64 byte order */
                ((u8 *) w)[0] = p[0];
                ((u8 *) w)[1] = p[1];
#else
                *(s16 *) w = last;
#endif
                n--;
                p += 2;
                w = (s32 *) ((u8 *) w + 2);
            } while (n != 0);
        }
        n = *p++;
        grp[0xC7] = n;
        q = grp + 0xC8;
        rec = recEnd;
        for (; n != 0; n--) {
            id = p[0x14];
            *q++ = id;
            if (lvl_has_tri_id(id, recEnd) == 0) {
                PORT_STALE_51(rec);
                rec = lvl_build_coll_tri(rec, p, id, (s32) end, n, last, s1io, 0, 1);
            }
            p += 0x15;
        }
        recEnd = rec;
        n = *p++;
        grp[0xD0] = n;
        for (q = grp + 0xD1; n != 0; n--) {
            *q++ = *p++;
        }
        grp += 0xDC;
    }
    gVehGatesEnd = grp;
    gCollTrisEnd = recEnd;
    return recEnd;
}
#else
/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/5CB60/lvl_build_object_groups.s")
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING

/* A collision triangle with that id (0x802A4168).
 * Returns 1 if a 0x60-byte record in [gCollTris, end) has byte 0x50 == id,
 * else 0. The asm loops with `!=`.
 * asm: id in v0, end in fp, result in t5; it saves t0/t1.
 * Asm callers rely on preserved: lvl_build_object_groups keeps t1-t4, t6, t7, t9. */
s32 lvl_has_tri_id(s32 id, u8 *end) {
    u8 *p;

    for (p = gCollTris; p != end; p += 0x60) {
        if (p[0x50] == id) {
            return 1;
        }
    }
    return 0;
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/5CB60/lvl_has_tri_id.s")
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
/* One 0x60-byte collision triangle (plane, axis) (0x802A41B0).
 * Sets up a 0x60-byte collision triangle record from 0x14 bytes of packed
 * vertex data: three unaligned big-endian s16 (x, y, z) points P0..P2 (each
 * << 3, stored as words at 0x28..0x48) and a u16 at 0x12 (stored at 0x4C).
 * The plane normal n = (P0 - P1) x (P0 - P2) and -(n . P1) are s64 at 0, 8,
 * 0x10 and 0x18; |n|^2 and |n| as floats at 0x24 / 0x20; byte 0x4E is the
 * dominant normal axis (0 = z, 1 = y, else 2). Bytes 0x4F..0x59 come from the
 * arguments. Returns rec + 0x60.
 * asm: rec in t4 (returned advanced in t4), v in t5, id in v0, h52 in t2,
 * b57 in t6, b56 in t7, b4F in t9, b55 in gp; b58 in s1, and s1 comes back
 * as P0.x - P2.x (lvl_build_object_groups and lvl_build_coll_grid_a feed it to the next
 * call). It also leaves the P1.x/P1.y in a2/a3 and other products in s2-s7
 * (no caller reads them).
 * Asm callers rely on preserved: lvl_add_object keeps t0, t1, t5, t9, f12,
 * f14; lvl_build_coll_grid_a/3DF8/3E9C/3F80 keep t1-t3, t5-t7, t9; coll_add_dyn_tris
 * keeps t0, t2, t5, t6, t9. */
u8 *lvl_build_coll_tri(u8 *rec, u8 *v, s32 id, s32 h52, s32 b57, s32 b56, s32 *s1io, s32 b4F, s32 b55) {
    s32 x0, y0, z0, x1, y1, z1, x2, y2, z2;
    s32 dx1, dy1, dz1, dx2, dy2, dz2;
    s64 nx, ny, nz;
    s64 ax, ay, az;
    f32 f;
    f32 sum;

#ifdef PORT_HOST
    {
        /* bytes 0x5A-0x5F are never written either (nothing reads them):
         * the N64's stale bytes too, so RDRAM compares equal */
        u8 old[6];
        s32 k;
        for (k = 0; k < 6; k++) {
            old[k] = port_n64_byte(rec + 0x5A + k);
        }
        for (k = 0; k < 6; k++) {
            rec[0x5A + k] = old[k];
        }
        port_garbage(rec + 0x5A, 6);
    }
    port_tri_mark(rec);
#endif
    rec[0x59] = 0;
    rec[0x55] = b55;
    rec[0x56] = b56;
    *(s16 *) (rec + 0x52) = h52;
    rec[0x4F] = b4F;
    rec[0x50] = id;
    rec[0x57] = b57;
    rec[0x58] = *s1io;

    x0 = BE16S(v + 0x0) << 3;
    y0 = BE16S(v + 0x2) << 3;
    z0 = BE16S(v + 0x4) << 3;
    x1 = BE16S(v + 0x6) << 3;
    y1 = BE16S(v + 0x8) << 3;
    z1 = BE16S(v + 0xA) << 3;
    x2 = BE16S(v + 0xC) << 3;
    y2 = BE16S(v + 0xE) << 3;
    z2 = BE16S(v + 0x10) << 3;
    dx1 = x0 - x1;
    dy1 = y0 - y1;
    dz1 = z0 - z1;
    dx2 = x0 - x2;
    dy2 = y0 - y2;
    dz2 = z0 - z2;
    *(s32 *) (rec + 0x28) = x0;
    *(s32 *) (rec + 0x2C) = y0;
    *(s32 *) (rec + 0x30) = z0;
    *(s32 *) (rec + 0x34) = x1;
    *(s32 *) (rec + 0x38) = y1;
    *(s32 *) (rec + 0x3C) = z1;
    *(s32 *) (rec + 0x40) = x2;
    *(s32 *) (rec + 0x44) = y2;
    *(s32 *) (rec + 0x48) = z2;

    nx = (s64) dy1 * dz2 - (s64) dz1 * dy2;
    ny = (s64) dz1 * dx2 - (s64) dx1 * dz2;
    nz = (s64) dx1 * dy2 - (s64) dy1 * dx2;
    *(s64 *) (rec + 0x0) = nx;
    *(s64 *) (rec + 0x8) = ny;
    *(s64 *) (rec + 0x10) = nz;
    f = nx;
    sum = f * f;
    f = ny;
    sum = sum + f * f;
    f = nz;
    sum = sum + f * f;
    *(s64 *) (rec + 0x18) = -(nx * x1 + ny * y1 + nz * z1);
    *(f32 *) (rec + 0x24) = sum;
    *(f32 *) (rec + 0x20) = sqrtf(sum);

    ax = (nx < 0) ? -nx : nx;
    ay = (ny < 0) ? -ny : ny;
    az = (nz < 0) ? -nz : nz;
    if (!(az < ax) && !(az < ay)) {
        rec[0x4E] = TRI_AXIS_Z;
    } else if (!(ay < ax) && !(ay < az)) {
        rec[0x4E] = TRI_AXIS_Y;
    } else {
        rec[0x4E] = TRI_AXIS_X;
    }
    rec[0x54] = TRI_SIDE_NONE;
    *(u16 *) (rec + 0x4C) = BE16U(v + 0x12);
    *s1io = dx2;
    return rec + 0x60;
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/5CB60/lvl_build_coll_tri.s")
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING

/* GGroundCells from the header (0x802A4464).
 * Builds a pointer table for a chain of length-prefixed blocks: with
 * base = OBJ_PTR(obj, 0x30) and n = (s16 at 8) * (s16 at 0xA), walks
 * p = base, p = base + (unaligned big-endian word at p), storing p + 4 for
 * each of the n blocks and once more at the end (n + 1 entries in
 * gGroundCells). The asm counts n down with `!= 0`, so n must be >= 0.
 * asm: obj in t0; it clobbers t1-t3, t5, t6, hi/lo.
 * Asm callers rely on preserved: initlevel keeps t0, f12, f14. */
void lvl_build_ground_grid(u8 *obj) {
    s32 n = *(s16 *) (obj + 8) * *(s16 *) (obj + 0xA);
    u8 *base = OBJ_PTR(obj, 0x30);
    u8 *p = base;
    u8 * N64P *out = gGroundCells;

    while (n != 0) {
        *out++ = p + 4;
        p = base + BE32(p);
        n--;
    }
    *out = p + 4;
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/5CB60/lvl_build_ground_grid.s")
#endif

/* TODO: lvl_align_end - round up to a multiple of 8 if not already a
 * multiple of 4: `if ((arg1 & 3) != 0) arg1 = (arg1 & ~7) + 8; return
 * arg1;` (target: 11 instructions, a `nop` in the branch's delay slot -
 * $a1 is the real parameter, with an unused leading parameter shadowing
 * $a0). Every type tried for the unused first parameter (void*, s32,
 * f32) makes IDO spill it into the delay slot anyway (`sw`/`swc1` at
 * 0(sp), no actual stack frame reserved for it) - the inverse of the
 * usual "unused argument still gets a dead stack home" pattern seen
 * elsewhere (backdrop_unused_stub in hd_code/2D810.c): there the target WANTS the
 * dead spill and plain C gives it one; here the target doesn't want it
 * and plain C can't be talked out of it. Logic confirmed correct. */
#ifdef NON_MATCHING
/* Rounds the heap end up to 8 (0x802A44E4).
 * Port rewrite (not matching): round x up to a multiple of 8 unless it is a
 * multiple of 4. asm: x in a1; the result in v0 and also left in a1, which
 * its asm callers (lvl_load_object, lvl_load_model_plain, lvl_load_model) read back
 * as the new heap pointer.
 * Asm callers rely on preserved: a2, a3, f12, f14. */
u32 lvl_align_end(u32 x) {
    if ((x & 3) != 0) {
        x = (x & ~7) + 8;
    }
    return x;
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/5CB60/lvl_align_end.s")
#endif
