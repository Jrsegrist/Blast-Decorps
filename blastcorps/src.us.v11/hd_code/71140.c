#include "common.h"
#include <ultra64.h>
#include "game/game.h"
/* Views: this file reads these shared variables (game/variables.h) as other types. */
#ifdef NON_MATCHING
#define gHeapPtr (*(s32 *) &gHeapPtr)
#endif
/* end of views */

#ifdef NON_MATCHING
/* Register-block types shared by this file's rewrites (each mirrors the
 * callee's definition in 62740.c / 60F60.c; later per-function copies are
 * guarded out). */
extern u8 gBacklashState[];  /* vehicle 5's state block (the asm's $gp) */
extern u32 gBacklashPos[]; /* its position x, y, z */
extern u64 * N64P gBacklashMtxA;  /* its two matrix buffers */
extern u64 * N64P gBacklashMtxB;
extern u8 * N64P gBacklashModel;   /* its model header */
void backlash_build_model(MtxChainRegs *regs);
void backlash_set_physics(void);
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
extern u8 gBacklashAnim[];  /* vehicle 5's animation channels */
extern s8 gBacklashDeflecting;
extern u8 gBacklashSmokeCooldown;
extern u8 gBacklashSteerAnim;
extern f32 gBacklashTilt;
extern f32 gBacklashLean;
extern s16 gBacklashYawBase;

/* cvt.w.s under the FCSR's default rounding (nearest, ties to even). */
#define CVT_W_S_5900(out, x)                                            \
    do {                                                                \
        f32 _x = (x);                                                   \
        s32 _r = (s32) _x;                                              \
        f32 _f = _x - (f32) _r;                                         \
                                                                        \
        if (_f > 0.5f || (_f == 0.5f && (_r & 1))) {                    \
            _r++;                                                       \
        } else if (_f < -0.5f || (_f == -0.5f && (_r & 1))) {           \
            _r--;                                                       \
        }                                                               \
        (out) = _r;                                                     \
    } while (0)

/* Sets up Backlash (type 5) (0x802B5900).
 * Level setup of vehicle 5 (asm caller lvl_spawn_vehicles, the level's object
 * list), the shape of crane_spawn (75490): model header -> gBacklashModel,
 * two 0x800-byte matrix buffers from gHeapPtr (gBacklashMtxA / gBacklashMtxB;
 * the allocator advances by 0x1000), veh_add_record(5, 1, buffers, model),
 * veh_reset_state on gBacklashState, wheel-slot offsets (+-0x168, +-0x17C) at
 * +0x52..+0x68, position gBacklashPos = (x, y, z), heading +0x4C/+0x4E/+0x74,
 * the ground slots (veh_init_ground, key 5, y written back to D_803EEB3C), the
 * model channels (anim_init_model on gBacklashAnim, channel 0 set up, both
 * buffers animated), the speed band table +0x78..+0x94 (-0x78, 0, 2, 0,
 * 0x50, 6, 0x50, 0x64, 4, 0x64, 0x8C, 2, 0x8C, 0xDC, 2), veh_widen_bands;
 * gBacklashDeflecting = gBacklashSmokeCooldown = 0, level gBacklashSteerAnim = 50, gBacklashTilt =
 * gBacklashLean = 0.5; parts (veh_register_parts, tag 5, scale 0x4268);
 * veh_add_shadow(5, 0x64, 0x2D, 0x2D); one update with +0x9A set
 * (backlash_update); the root matrix copied from buffer B to A
 * (mtx_copy_at); finally gBacklashYawBase = heading + round(camera yaw *
 * 4096 / 360), less 0xFFF when >= 0x1000. The asm's add/addi trap on
 * overflow.
 * Register convention (conventions.txt): model s2, x t7, y s3, z s0,
 * heading s1, and fp, which the asm hands to veh_init_ground (reaching
 * gSlotSurface when a slot's grid scan finds no triangle: the dispatcher's
 * leftover fp, FIDELITY AUDIT); veh_init_ground's FP inputs (passed through)
 * are 0 here. The asm restores t0-t5 (lvl_spawn_vehicles keeps t1/t2 live),
 * points $gp at gBacklashState and leaves s0-s7, fp and f20-f28 as its callees
 * leave them (clobbers). */
void backlash_spawn(u8 *model, s32 x, s32 y, s32 z, s32 heading, s32 fp) {
    TriSideOut f;
    s32 buf;
    u8 *hdr;
    s32 yaw;
    s32 a;

    gBacklashModel = model;
    buf = gHeapPtr;
    gBacklashMtxA = (u64 *) N64_IPTR(buf);
    gBacklashMtxB = (u64 *) N64_IPTR((buf + 0x800));
    gHeapPtr = buf + 0x1000;
    veh_add_record(VEH_BACKLASH, 1, (s32) gBacklashMtxA, (s32) gBacklashMtxB, model);
    veh_reset_state(gBacklashState);
    /* wheel slot offsets */
    *(s16 *) (gBacklashState + 0x52) = 0x168;
    *(s16 *) (gBacklashState + 0x54) = 0x168;
    *(s16 *) (gBacklashState + 0x56) = -0x168;
    *(s16 *) (gBacklashState + 0x58) = 0x168;
    *(s16 *) (gBacklashState + 0x5A) = 0x168;
    *(s16 *) (gBacklashState + 0x5C) = -0x168;
    *(s16 *) (gBacklashState + 0x5E) = 0x17C;
    *(s16 *) (gBacklashState + 0x60) = 0x17C;
    *(s16 *) (gBacklashState + 0x62) = -0x17C;
    *(s16 *) (gBacklashState + 0x64) = 0x17C;
    *(s16 *) (gBacklashState + 0x66) = 0x17C;
    *(s16 *) (gBacklashState + 0x68) = -0x17C;
    gBacklashPos[0] = x;
    gBacklashPos[1] = y;
    gBacklashPos[2] = z;
    *(s16 *) (gBacklashState + 0x4C) = heading;
    *(s16 *) (gBacklashState + 0x4E) = heading;
    *(s16 *) (gBacklashState + 0x74) = heading;
    f.pz = 0.0f;
    f.cross = 0.0f;
    f.cz = 0.0f;
    f.side = 0.0f;
    f.sideZ = 0.0f;
    f.dz = 0.0f;
    veh_init_ground((s16 *) (gBacklashState + 0x52), gBacklashPos[1], x, z, (s32 *) (gBacklashState + 4),
                  (s32 *) &gBacklashPos[1], (s16 *) (gBacklashState + 0x4C), 5, fp, gBacklashState, &f);
    hdr = gBacklashModel;
    anim_init_model((u32 *) gBacklashMtxB, (u32 *) gBacklashMtxA, (AnimChannel *) gBacklashAnim, hdr);
    anim_set_speed((AnimChannel *) gBacklashAnim, 0, 0x64);
    anim_set_reverse((AnimChannel *) gBacklashAnim, 0, 0);
    anim_set_loop_mode((AnimChannel *) gBacklashAnim, 0, 0);
    anim_set_interp(0.0f, (AnimChannel *) gBacklashAnim, 0, 0);
    anim_play((AnimChannel *) gBacklashAnim, 0, 1);
    anim_update_model((u8 *) gBacklashMtxA, (u8 *) gBacklashMtxB, (AnimChannel *) gBacklashAnim);
    anim_rewind(0, (AnimChannel *) gBacklashAnim);
    anim_play((AnimChannel *) gBacklashAnim, 0, 1);
    anim_update_model((u8 *) gBacklashMtxB, (u8 *) gBacklashMtxA, (AnimChannel *) gBacklashAnim);
    /* speed bands */
    *(s16 *) (gBacklashState + 0x78) = -0x78;
    *(s16 *) (gBacklashState + 0x7A) = 0;
    *(s16 *) (gBacklashState + 0x7C) = 2;
    *(s16 *) (gBacklashState + 0x7E) = 0;
    *(s16 *) (gBacklashState + 0x80) = 0x50;
    *(s16 *) (gBacklashState + 0x82) = 6;
    *(s16 *) (gBacklashState + 0x84) = 0x50;
    *(s16 *) (gBacklashState + 0x86) = 0x64;
    *(s16 *) (gBacklashState + 0x88) = 4;
    *(s16 *) (gBacklashState + 0x8A) = 0x64;
    *(s16 *) (gBacklashState + 0x8C) = 0x8C;
    *(s16 *) (gBacklashState + 0x8E) = 2;
    *(s16 *) (gBacklashState + 0x90) = 0x8C;
    *(s16 *) (gBacklashState + 0x92) = 0xDC;
    *(s16 *) (gBacklashState + 0x94) = 2;
    veh_widen_bands(gBacklashState);
    gBacklashDeflecting = 0;
    gBacklashSmokeCooldown = 0;
    gBacklashSteerAnim = 0x32;
    gBacklashTilt = 0.5f;
    gBacklashLean = 0.5f;
    hdr = gBacklashModel;
    veh_register_parts(5, hdr + *(s32 *) (hdr + 4), hdr + *(s32 *) (hdr + 8), 0x4268);
    veh_add_shadow(VEH_BACKLASH, 0x64, 0x2D, 0x2D);
    gBacklashState[0x9A] = 1;
    backlash_update();
    gBacklashState[0x9A] = 0;
    hdr = gBacklashModel;
    mtx_copy_at((u8 *) gBacklashMtxB, (u8 *) gBacklashMtxA, *(s32 *) (hdr + *(s32 *) (hdr + 0x18) + 4));
    CVT_W_S_5900(yaw, gCamHeading * 4096.0f / 360.0f);
    a = *(u16 *) (gBacklashState + 0x4C) + yaw;
    if (a >= 0x1000) {
        a -= 0xFFF;
    }
    gBacklashYawBase = a;
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/71140/backlash_spawn.s")
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
extern u8 gBacklashState[]; /* this vehicle's state block */
extern u8 gBacklashAnim[]; /* animation channel table (AnimChannel, 56040.c) */

/* Getting into Backlash: engine sound 5 (0x802B5CD8).
 * Enter vehicle type 5 (called from 00000.c / 17210.c): clears byte 0x99 of
 * the state block, sets fields 0x14 / 0x11 / 0x12 of animation channels 1, 2,
 * 4, 5 of gBacklashAnim (channel 2 also gets 0.5f / 0 via anim_set_frame) and
 * restarts each with -1, sets the gBacklashYawTexAnimA and gBacklashYawTexAnimB entries' fields to
 * 0 and restarts them, gCamDistance / gCamHeightBase = 3400, 300, then veh_start_engine_sound(a0, 5)
 * with a0 = gBacklashAnim left over from the setters (veh_start_engine_sound ignores it).
 * The asm points $gp at gBacklashState and leaves it there (conventions.txt:
 * clobbers gp) and returns with v1 = -1 (the last setter's v1); C callers
 * ignore both. */
void backlash_enter(void) {
    gBacklashState[0x99] = 0;
    anim_set_speed((AnimChannel *) gBacklashAnim, 1, 0);
    anim_set_reverse((AnimChannel *) gBacklashAnim, 1, 0);
    anim_set_loop_mode((AnimChannel *) gBacklashAnim, 1, 0);
    anim_play((AnimChannel *) gBacklashAnim, 1, -1);
    anim_set_speed((AnimChannel *) gBacklashAnim, 2, 0);
    anim_set_reverse((AnimChannel *) gBacklashAnim, 2, 0);
    anim_set_loop_mode((AnimChannel *) gBacklashAnim, 2, 1);
    anim_set_frame(0.5f, (AnimChannel *) gBacklashAnim, 2, 0);
    anim_play((AnimChannel *) gBacklashAnim, 2, -1);
    anim_set_speed((AnimChannel *) gBacklashAnim, 4, 0);
    anim_set_reverse((AnimChannel *) gBacklashAnim, 4, 0);
    anim_set_loop_mode((AnimChannel *) gBacklashAnim, 4, 1);
    anim_play((AnimChannel *) gBacklashAnim, 4, -1);
    anim_set_speed((AnimChannel *) gBacklashAnim, 5, 0);
    anim_set_reverse((AnimChannel *) gBacklashAnim, 5, 0);
    anim_set_loop_mode((AnimChannel *) gBacklashAnim, 5, 1);
    anim_play((AnimChannel *) gBacklashAnim, 5, -1);
    anim_tex_set_speed((s32) gBacklashYawTexAnimA, 0);
    anim_tex_set_reverse((s32) gBacklashYawTexAnimA, 0);
    anim_tex_set_loop_mode((s32) gBacklashYawTexAnimA, 0);
    anim_tex_play((s32) gBacklashYawTexAnimA, -1);
    anim_tex_set_speed((s32) gBacklashYawTexAnimB, 0);
    anim_tex_set_reverse((s32) gBacklashYawTexAnimB, 0);
    anim_tex_set_loop_mode((s32) gBacklashYawTexAnimB, 0);
    anim_tex_play((s32) gBacklashYawTexAnimB, -1);
    gCamDistance = 0xD48;
    gCamHeightBase = 0x12C;
    veh_start_engine_sound((s32) gBacklashAnim, 5);
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/71140/backlash_enter.s")
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
extern u8 gBacklashState[]; /* this vehicle's state block */

/* 1 unless a wheel slot is airborne (0x802B5F04).
 * Exit check for vehicle type 5, same shape as skyfall_exit_check (6E200): returns
 * 1 when none of the bytes at +0x96, +0x97, +0x98 equals 1, else 0 (s32; the
 * C caller in hd.c declares it void and returns the leftover v0). */
s32 backlash_exit_check(void) {
    if (gBacklashState[0x96] == 1 || gBacklashState[0x97] == 1 || gBacklashState[0x98] == 1) {
        return 0;
    }
    return 1;
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/71140/backlash_exit_check.s")
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
extern u64 * N64P gBacklashMtxA; /* save copy pair (veh_sync_buffers) */
extern u64 * N64P gBacklashMtxB;

/* Getting out of Backlash (0x802B5F60).
 * Leave vehicle type 5 (called from hd.c): zeroes the speed (s16 at +0x76 of
 * the state block), veh_sync_buffers(gBacklashMtxA, gBacklashMtxB, 0x800), then
 * veh_stop_engine_sounds(). The asm saves and restores $gp. */
void backlash_leave(void) {
    *(s16 *) (gBacklashState + 0x76) = 0;
    veh_sync_buffers(gBacklashMtxA, gBacklashMtxB, 0x800);
    veh_stop_engine_sounds();
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/71140/backlash_leave.s")
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING

/* Placed Backlash on a moving level part: re-ground it (0x802B5FAC).
 * Place vehicle 5 on the ground at its position (called from hd.c's
 * veh_update_others when the player switches to it): veh_reground fills the
 * three wheel slots' height records (veh + 4) and sets the y position
 * gBacklashPos[1] (its `mid`) from them, key 5; then the model is rebuilt
 * (backlash_build_model) and veh_set_record(z, 5, x, y, veh) called.
 * Leaked registers (FIDELITY AUDIT): veh_reground also takes fp (stored
 * into gSlotSurface[0..2], whose average goes to veh + 0x50) and f12-f26; the
 * asm passes on whatever its C caller (hd.c veh_update_others) has in $fp
 * ($s8) and the FP registers. This passes 0 for fp (veh + 0x50 = 0) and
 * the FP state, and 0 for backlash_build_model's chain inputs (a3 is the caller's,
 * s1 / s0 veh_reground's dst + 9 and the z position; only read for a
 * zero-matrix point). The asm saves every s- and FP register; it returns
 * with v1 = veh_set_record's (preserved) y, unused by its C caller. */
void backlash_reground(void) {
    TriSideOut f;
    Out802A9A60 out;
    MtxChainRegs regs;

    f.pz = 0.0f;
    f.cross = 0.0f;
    f.cz = 0.0f;
    f.side = 0.0f;
    f.sideZ = 0.0f;
    f.dz = 0.0f;
    veh_reground((s16 *) (gBacklashState + 0x52), gBacklashPos[1], gBacklashPos[0], gBacklashPos[2],
                  (s32 *) (gBacklashState + 4), (s32 *) &gBacklashPos[1], (s16 *) (gBacklashState + 0x4C), 5, 0, gBacklashState,
                  &f, &out);
    regs.a3 = 0;
    regs.s1 = 0;
    regs.s0 = 0;
    backlash_build_model(&regs);
    veh_set_record(gBacklashPos[2], 5, gBacklashPos[0], gBacklashPos[1], gBacklashState);
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/71140/backlash_reground.s")
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
extern u32 gBacklashPos[]; /* x, y, z */
/* Veh_zone_alpha at Backlash's position (0x802B60BC).
 * Zone level lookup (veh_zone_alpha) for vehicle id 5 at its position
 * gBacklashPos..+8; returns veh_zone_alpha's v1 (the zone list end).
 * Register convention: the asm passes veh_zone_alpha's scan registers t6, t7,
 * s0-s4 through (in and out; here through r), leaves a3 = id and clobbers
 * s5, s6 (conventions.txt). */
s32 backlash_zone_alpha(ZoneScanRegs *r) {
    return veh_zone_alpha(5, gBacklashPos[0], gBacklashPos[1], gBacklashPos[2], r);
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/71140/backlash_zone_alpha.s")
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING

/* Records Backlash's position on the deck it stands on (0x802B6100).
 * Riding a deck, setup for vehicle 5 (asm caller veh_attach_riders_walk, record
 * kind 5): the value pair at its position on triangle `id` (veh_deck_to_local)
 * goes to s16 +0x6A/+0x6C of the state block, the heading (u16 +0x4C) to
 * +0x6E, and the pair at the position plus wheel slot 0's offset
 * (veh_wheel_offset on veh + 0x52, rotated by the heading) to +0x70/+0x72.
 * Register convention (conventions.txt): id in a3; veh_deck_to_local's FP results
 * (f24 also in, chained through both calls) pass through r. The asm saves and
 * restores t0, t1, t3, t4 (asm caller veh_attach_riders_walk keeps t0 and t1 live),
 * points $gp at gBacklashState and leaves s4 = veh + 0x4C (clobbers), and returns
 * with v1 = veh + 0x52 and t5/t6 = the slot offset; veh_attach_riders_walk doesn't
 * read them (the next record's function gets them as scratch). The asm's
 * `add`s trap on overflow. Same shape as jbomb_ride_attach (7FB50) plus the
 * second sample. */
void backlash_ride_attach(s32 id, InterpRegs *r) {
    s32 dx;
    s32 dz;

    veh_deck_to_local(id, gBacklashPos[0], gBacklashPos[2], r);
    *(s16 *) (gBacklashState + 0x6A) = r->t3;
    *(s16 *) (gBacklashState + 0x6C) = r->t4;
    *(s16 *) (gBacklashState + 0x6E) = *(u16 *) (gBacklashState + 0x4C);
    dx = veh_wheel_offset(0, (s16 *) (gBacklashState + 0x52), (s16 *) (gBacklashState + 0x4C), &dz);
    veh_deck_to_local(id, gBacklashPos[0] + dx, gBacklashPos[2] + dz, r);
    *(s16 *) (gBacklashState + 0x70) = r->t3;
    *(s16 *) (gBacklashState + 0x72) = r->t4;
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/71140/backlash_ride_attach.s")
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
/* Moves Backlash with the deck under it (0x802B618C).
 * Riding a deck, step of vehicle 5 (asm caller veh_move_riders_walk, record kind
 * 5): steer from the stored pair (s16 +0x6A/+0x6C) towards (+0x70/+0x72)
 * on triangle `id` with heading u16 +0x6E (veh_follow_heading); the result
 * becomes the heading (+0x4E and +0x4C). The pair at (+0x6A, +0x6C) is
 * re-sampled (veh_deck_to_world) and becomes the ground-contact position:
 * backlash_set_physics, gBounceSound = 1, veh_ground_contact (id 5, divisors 0x2D0 /
 * 0x2D0, s3 = veh_follow_heading's distance), the model rebuild backlash_build_model and
 * veh_set_record(z, 5, x, y, veh).
 * Register convention (conventions.txt): id in a3, f24 in, f24/f26 out (f),
 * result = veh_ground_contact's s3 (the asm's s3). The FP state chains as in the
 * asm: f24 -> veh_follow_heading -> veh_deck_to_world (f12-f26 out) ->
 * veh_ground_contact (in/out). The asm saves and restores a3, t0, t1, t2 (asm
 * caller veh_move_riders_walk keeps them live), points $gp at gBacklashState and leaves
 * s0-s2, s4-s7, fp, f20, f22 changed (clobbers). backlash_build_model's chain inputs
 * (veh_ground_contact's leftover a3 / s1 / s0) are 0 here: only read for a
 * zero-matrix point. */
s32 backlash_ride_follow(s32 id, TriSideOut *f) {
    InterpRegs ir;
    TriSideOut tf;
    Regs802A8768 r;
    MtxChainRegs regs;
    s32 s3;
    s32 h;

    ir.f24 = f->sideZ;
    h = veh_follow_heading((s16 *) (gBacklashState + 0x52), *(s16 *) (gBacklashState + 0x6A), (u16 *) (gBacklashState + 0x6E), id,
                      *(s16 *) (gBacklashState + 0x6C), *(s16 *) (gBacklashState + 0x70), *(s16 *) (gBacklashState + 0x72), &s3,
                      &ir);
    *(s16 *) (gBacklashState + 0x4E) = h;
    *(s16 *) (gBacklashState + 0x4C) = h;
    veh_deck_to_world(id, *(s16 *) (gBacklashState + 0x6A), *(s16 *) (gBacklashState + 0x6C), &ir);
    backlash_set_physics();
    gBounceSound = 1;
    tf.pz = ir.f12;
    tf.cross = *(f32 *) &ir.f14;
    tf.cz = ir.f20;
    tf.side = ir.f22;
    tf.sideZ = ir.f24;
    tf.dz = ir.f26;
    r.s3 = s3;
    veh_ground_contact(gBacklashState, 5, (s32 *) &gBacklashPos[0], (s32 *) &gBacklashPos[1], (s32 *) &gBacklashPos[2], ir.t3,
                  ir.t4, 0x2D0, 0x2D0, (s16 *) (gBacklashState + 0x4C), gBacklashState + 0x96, (s16 *) (gBacklashState + 0x52),
                  (s32 *) (gBacklashState + 0x28), (s32 *) (gBacklashState + 0x40), (s32 *) (gBacklashState + 0x34),
                  (s32 *) (gBacklashState + 4), &r, &tf);
    regs.a3 = 0;
    regs.s1 = 0;
    regs.s0 = 0;
    backlash_build_model(&regs);
    veh_set_record(gBacklashPos[2], 5, gBacklashPos[0], gBacklashPos[1], gBacklashState);
    f->sideZ = tf.sideZ;
    f->dz = tf.dz;
    return r.s3;
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/71140/backlash_ride_follow.s")
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
extern s16 gBacklashGrip;    /* engine rate */
extern s16 gBacklashDeflectHeading;    /* heading kept while bouncing off */
extern u8 gBacklashSmokeCooldown;     /* spawn cooldown */
extern s8 gBacklashDeflecting;     /* collision flag */
extern s8 gBacklashHitSound;     /* sound to start this frame (-1 = none) */
extern u8 gBacklashNonBlockingParts[];
extern f32 gBacklashDeflectTurnRate;
s32 backlash_zone_alpha(ZoneScanRegs *r);
void backlash_update_anims(void);
s32 backlash_steer_rate(void);
void backlash_add_tyre_trail(void);

#define VEH5(off, T) (*(T *) (gBacklashState + (off)))

/* Backlash driven by the player, per frame (0x802B6294).
 * Per-frame update of vehicle 5 (gBacklashState; called from hd.c's vehicle
 * switch and from backlash_spawn):
 * 1. Zone lookup (backlash_zone_alpha), veh_snapshot (position from gBacklashAnim),
 *    the animation/sound update backlash_update_anims while byte +0x9A is clear,
 *    veh_start_turbo when gTurboPending is set, backlash_set_physics.
 * 2. Driving: veh_steer_throttle(rate = backlash_steer_rate, heading +0x4C, speed +0x76,
 *    mode 3, flags +0x96, bands +0x78, 8), veh_turn_heading(+0x74, rate
 *    gBacklashGrip, +0x4C -> +0x4E, flag +0x99, sound 1), f = veh_slope,
 *    veh_apply_slope(kind 5, clamp 1, 720.0), veh_set_target_heading when gBacklashDeflecting is
 *    set, the new position veh_move_along_heading(f, +0x4E, ...), gBounceSound = 1 and
 *    the ground contact veh_ground_contact (id 5, divisors 0x2D0 / 0x2D0).
 * 3. Model: anim_update_model on the current buffer pair; while +0x99 is set
 *    mb_start(6, 0.25, 100) unless mb_is_active, else mb_stop;
 *    the tyre trail backlash_add_tyre_trail; the spawn cooldown gBacklashSmokeCooldown (at 0 with
 *    +0x99 set: restart at 1 and, with fewer than 4 fx_spawn records,
 *    one more: def gFxExhaustSmoke, data 0x30D40, type 1 at (5, 1, 1));
 *    veh_check_wrap, the rebuild backlash_build_model.
 * 4. Collision: veh_collision_setup (table gBacklashNonBlockingParts, 1, 1, b0 8, speed, 100, 0,
 *    kind 5), veh_collide_spheres(5), veh_collide_world(5); gBacklashHitSound = 12. Without a
 *    hit (gCollBlocked == 0): gCollHit = 0, gBacklashHitSound = -1 and, unless
 *    gLevelKind == 0x40, obj_collide_vehicle(5) (gCollVehicleAnim = gBacklashAnim); if that
 *    set gCollHit the vehicle bounces: gBacklashDeflecting = 0, veh_rollback
 *    (restore the saved state, 0x800 bytes), speed = -max(|v|, 80) / 2 with
 *    v's sign (0 counts as positive) and the model rebuilt; else gBacklashDeflecting
 *    = 0. With a hit: veh_prepare_escape, gBacklashDeflecting = 1, then the heading
 *    correction: d = |((+0x4E - 0x800) wrapped by +0xFFF) - veh_free_arc_mid()|
 *    folded to <= 0x800, veh_reverse_if_behind, veh_turn_into_free_arc(+0x4E -> +0x4C,
 *    gBacklashDeflectTurnRate) whose new heading goes to gBacklashDeflectHeading, +0x4E and +0x74,
 *    veh_step_heading(its turn, d), veh_hit_slowdown(0) and, unless gLevelKind ==
 *    0x40, obj_collide_vehicle(5).
 * 5. Player globals gPlayerX/Y/Z = the position, gPlayerSpeed/MoveHeading/Heading =
 *    speed, +0x4E, +0x4C; veh_set_record(z, 5, x, y, veh); veh_maze_object_hits(z,
 *    gBacklashNonBlockingParts, x, y, b0, 5, veh) with gCollVehicleAnim = gBacklashAnim; sound
 *    gBacklashHitSound when >= 0 (snd_play).
 * Leaked registers (FIDELITY AUDIT): veh_maze_object_hits's b0 is the asm's t0 =
 * &gPlayerZ (kept). Passed as 0 here: the zone scan's t6/t7/s0-s4 inputs
 * (the caller's registers, dead afterwards); veh_ground_contact's f14-f26 inputs
 * (pass-through); backlash_build_model's chain inputs (only read for a
 * zero-matrix point); fx_spawn's t6/s1/w24/w28/w18/w1C/w2C (the asm
 * hands over t7 = &gBacklashPos and anim_update_model's leftover s0/s2-s4 - after
 * the C callees mb_is_active / mb_start / mb_stop, which may
 * change t7; a type-1 record with z != 0 ignores them all). The asm's
 * `addi`/`sub` trap on overflow. It saves every s- and FP register it
 * changes and returns with f12/f14/v1 as its last callees leave them (read
 * by backlash_spawn; not modelled). */
void backlash_update(void) {
    ZoneScanRegs zr;
    MtxChainRegs regs;
    Regs802A8768 r;
    TriSideOut f;
    Out802A860C out;
    Io802A6274 io;
    u64 *end;
    f32 step;
    s32 x;
    s32 v;
    s32 d;
    s32 turn;
    s32 cur;
    s32 tgt;

    zr.t6 = 0;
    zr.t7 = 0;
    zr.s0 = 0;
    zr.s1 = 0;
    zr.s2 = 0;
    zr.s3 = 0;
    zr.s4 = 0;
    backlash_zone_alpha(&zr);
    veh_snapshot(gBacklashState, gBacklashAnim, (s32 *) &gBacklashPos[0], (s32 *) &gBacklashPos[1], (s32 *) &gBacklashPos[2]);
    if ((s8) gBacklashState[0x9A] == 0) {
        backlash_update_anims();
    }
    if (gTurboPending != 0) {
        veh_start_turbo(gBacklashState);
    }
    backlash_set_physics();

    /* 2. */
    veh_steer_throttle(backlash_steer_rate(), &VEH5(VS_HEADING, u16), gBacklashState, &VEH5(VS_SPEED, s16), 3, gBacklashState + 0x96,
                  &VEH5(VS_BANDS, s16), 8);
    veh_turn_heading(gBacklashState, &VEH5(VS_MOMENTUM_HEADING, u16), (u16) gBacklashGrip, &VEH5(VS_SPEED, s16), &VEH5(VS_HEADING, u16),
                  &VEH5(VS_MOVE_HEADING, u16), (s8 *) (gBacklashState + 0x99), 1);
    step = veh_slope(&VEH5(VS_SPEED, s16), gBacklashState + 0x96, &VEH5(4, s32), (f32 *) gBacklashState);
    veh_apply_slope(gBacklashState, &VEH5(VS_SPEED, s16), 5, (s8 *) (gBacklashState + 0x96), &VEH5(4, s32), 1, 720.0f);
    if (gBacklashDeflecting != 0) {
        veh_set_target_heading(gBacklashState, &gBacklashDeflectHeading);
    }
    x = veh_move_along_heading(step, VEH5(VS_MOVE_HEADING, u16), &VEH5(VS_SPEED, s16), (s32 *) &gBacklashPos[0], (s32 *) &gBacklashPos[2],
                      &out);
    gBounceSound = 1;
    r.s3 = out.s3;
    f.pz = step;
    f.cross = 0.0f;
    f.cz = 0.0f;
    f.side = 0.0f;
    f.sideZ = 0.0f;
    f.dz = 0.0f;
    veh_ground_contact(gBacklashState, 5, (s32 *) &gBacklashPos[0], (s32 *) &gBacklashPos[1], (s32 *) &gBacklashPos[2], x, out.t1,
                  0x2D0, 0x2D0, &VEH5(VS_HEADING, s16), gBacklashState + 0x96, &VEH5(VS_WHEEL_OFFSETS, s16), &VEH5(VS_SLOT_LAUNCH_VEL, s32),
                  &VEH5(VS_SLOT_AIR_TIME, s32), &VEH5(VS_SLOT_LAUNCH_Y, s32), &VEH5(4, s32), &r, &f);

    /* 3. */
    if (gBufferIndex != 0) {
        anim_update_model((u8 *) gBacklashMtxA, (u8 *) gBacklashMtxB, (AnimChannel *) gBacklashAnim);
    } else {
        anim_update_model((u8 *) gBacklashMtxB, (u8 *) gBacklashMtxA, (AnimChannel *) gBacklashAnim);
    }
    if (gBacklashState[0x99] != 0) {
        if (mb_is_active() == 0) {
            mb_start(6, 0.25f, 100);
        }
    } else {
        mb_stop();
    }
    backlash_add_tyre_trail();
    if (gBacklashSmokeCooldown != 0) {
        gBacklashSmokeCooldown--;
    } else if (gBacklashState[0x99] != 0) {
        gBacklashSmokeCooldown = 1;
        if (fx_count_active() < 4) {
            io.a3 = 1;
            io.t6 = 0;
            io.s1 = 0;
            fx_spawn(&io, gFxExhaustSmoke, 0x30D40, 1, 5, 1, 1, 0, 0, 0, 0, 0, 1);
        }
    }
    veh_check_wrap((s32 *) &gBacklashPos[0], (s32 *) &gBacklashPos[1], (s32 *) &gBacklashPos[2]);
    regs.a3 = 0;
    regs.s1 = 0;
    regs.s0 = 0;
    backlash_build_model(&regs);

    /* 4. */
    veh_collision_setup(gBacklashPos[2], (s32) gBacklashNonBlockingParts, 1, 1, gBacklashPos[0], gBacklashPos[1], 8, VEH5(VS_SPEED, s16), 0x64, 0,
                  5, gBacklashState);
    veh_collide_spheres(5, gBacklashState);
    veh_collide_world(5);
    gBacklashHitSound = 12;
    if (gCollBlocked == 0) {
        gCollHit = 0;
        gBacklashHitSound = -1;
        if (((s32) gLevelKind) == LEVEL_KIND_PILL_MAZE) {
            goto no_hit;
        }
        gCollVehicleAnim = gBacklashAnim;
        obj_collide_vehicle(5, gBacklashState);
        if (gCollHit == 0) {
            goto no_hit;
        }
        gBacklashDeflecting = 0;
        if (gBufferIndex != 0) {
            veh_rollback(gBacklashState, gBacklashAnim, (s32 *) &gBacklashPos[0], (s32 *) &gBacklashPos[1],
                          (s32 *) &gBacklashPos[2], gBacklashMtxB, gBacklashMtxA, 0x800, &end);
        } else {
            veh_rollback(gBacklashState, gBacklashAnim, (s32 *) &gBacklashPos[0], (s32 *) &gBacklashPos[1],
                          (s32 *) &gBacklashPos[2], gBacklashMtxA, gBacklashMtxB, 0x800, &end);
        }
        v = VEH5(VS_SPEED, s16);
        if (v < 0) {
            if (v > -0x50) {
                v = -0x50;
            }
        } else if (v < 0x50) {
            v = 0x50;
        }
        VEH5(VS_SPEED, s16) = -v >> 1;
        backlash_build_model(&regs);
        goto globals;
    }
    veh_prepare_escape(gBacklashState);
    gBacklashDeflecting = 1;
    d = VEH5(VS_MOVE_HEADING, u16) - 0x800;
    if (d < 0) {
        d += 0xFFF;
    }
    d -= veh_free_arc_mid();
    if (d < 0) {
        d = -d;
    }
    if (d >= 0x801) {
        d = 0xFFF - d;
    }
    veh_reverse_if_behind(gBacklashState);
    turn = veh_turn_into_free_arc(gBacklashState, VEH5(VS_MOVE_HEADING, u16), VEH5(VS_HEADING, u16), &cur, gBacklashDeflectTurnRate);
    gBacklashDeflectHeading = cur;
    VEH5(VS_MOVE_HEADING, s16) = cur;
    VEH5(VS_MOMENTUM_HEADING, s16) = cur;
    veh_step_heading(gBacklashState, turn, d, &tgt);
    veh_hit_slowdown(gBacklashState, 0);
    if (((s32) gLevelKind) != LEVEL_KIND_PILL_MAZE) {
        gCollVehicleAnim = gBacklashAnim;
        obj_collide_vehicle(5, gBacklashState);
    }
    goto globals;
no_hit:
    gBacklashDeflecting = 0;

globals:
    /* 5. */
    gPlayerX = gBacklashPos[0];
    gPlayerY = gBacklashPos[1];
    gPlayerZ = gBacklashPos[2];
    gPlayerSpeed = VEH5(VS_SPEED, s16);
    gPlayerMoveHeading = VEH5(VS_MOVE_HEADING, u16);
    gPlayerHeading = VEH5(VS_HEADING, u16);
    veh_set_record(gBacklashPos[2], 5, gBacklashPos[0], gBacklashPos[1], gBacklashState);
    gCollVehicleAnim = gBacklashAnim;
    veh_maze_object_hits(gBacklashPos[2], (s32) gBacklashNonBlockingParts, gBacklashPos[0], gBacklashPos[1], (s32) &gPlayerZ, 5, gBacklashState);
    if (gBacklashHitSound >= 0) {
        snd_play(gSfxBank, gBacklashHitSound, NULL);
    }
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/71140/backlash_update.s")
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
/* cvt.w.s under the FCSR's default rounding (nearest, ties to even); a C cast
 * truncates instead. */
#define CVT_W_S(out, x)                                                 \
    do {                                                                \
        f32 _x = (x);                                                   \
        s32 _r = (s32) _x;                                              \
        f32 _f = _x - (f32) _r;                                         \
                                                                        \
        if (_f > 0.5f || (_f == 0.5f && (_r & 1))) {                    \
            _r++;                                                       \
        } else if (_f < -0.5f || (_f == -0.5f && (_r & 1))) {           \
            _r--;                                                       \
        }                                                               \
        (out) = _r;                                                     \
    } while (0)

extern u32 gBacklashPos[]; /* [0], [2]: trail x, z */
extern f32 gBacklashLean;   /* left/right balance, 0.5 = centred */

/* Tyre tracks while skidding (0x802B69F8).
 * Tyre trail (shape of skyfall_add_tyre_trail in 6E200; $gp = gBacklashState). The two
 * sides' a/b values are 40 -+ d, where d = round(|gBacklashLean - 0.5| * 2 *
 * 20): a = 40 - d, b = 40 + d when gBacklashLean <= 0.5, else (NaN included)
 * a = 40 + d, b = 40 - d. Then trail_add(3, y, 400, -400, -400,
 * -400, gBacklashPos[0], gBacklashPos[2], yaw, 5, a, b, 0). The value is only
 * computed when the trail is drawn.
 * Register note: the asm saves and restores every integer register; asm
 * caller backlash_update keeps a0-a3, t6, t7 live (and reads f12/f14 after
 * the call, which the asm doesn't touch but trail_add may); a mixed N64
 * build would need a thunk. */
void backlash_add_tyre_trail(void) {
    f32 bal;
    s32 d;
    s32 a;
    s32 b;

    if (gBacklashState[0x99] == 0 || gBacklashState[0x98] == 1 || gBacklashState[0x50] >= 3 || gBacklashState[0x9B] != 0) {
        return;
    }
    bal = gBacklashLean;
    if (bal <= 0.5f) {
        CVT_W_S(d, (0.5f - bal) * 2.0f * 20.0f);
        b = d + 40;
        a = 40 - d;
    } else {
        CVT_W_S(d, (bal - 0.5f) * 2.0f * 20.0f);
        b = 40 - d;
        a = d + 40;
    }
    trail_add(3, *(s32 *) (gBacklashState + 0x1C), 400, -400, -400, -400, gBacklashPos[0], gBacklashPos[2],
                  *(u16 *) (gBacklashState + 0x4E), 5, a, b, 0);
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/71140/backlash_add_tyre_trail.s")
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
extern s16 gBacklashYawBase;   /* angle offset */
extern f32 gBacklashTilt;   /* channel-4 position, 0.5 = rest */
extern u8 gBacklashSteerAnim;    /* level 0..100 */
extern f32 gBacklashLeanLeftStep;   /* balance / position steps */
extern f32 gBacklashLeanRightStep;
extern f32 gBacklashLeanReturnStep;
extern f32 gBacklashTiltDownStep;
extern f32 gBacklashTiltUpStep;
extern f32 gBacklashTiltReturnStep;
f32 backlash_lean_limit(s32 side);

/* Moves f by step towards 0.5, stopping at 0.5 (NaN stays NaN). */
#define TOWARDS_HALF(f, step)       \
    if ((f) < 0.5f) {               \
        (f) += (step);              \
        if (!((f) <= 0.5f)) {       \
            (f) = 0.5f;             \
        }                           \
    } else {                        \
        (f) -= (step);              \
        if ((f) < 0.5f) {           \
            (f) = 0.5f;             \
        }                           \
    }

/* Animations from input and speed; engine pitch (0x802B6C28).
 * Per-frame animation/sound update of vehicle 5 (asm caller backlash_update;
 * the asm reads the state block through $gp = gBacklashState, read directly
 * here). The heading (+0x4C) plus the camera yaw in 4096ths, minus
 * gBacklashYawBase, folded into 0..0x7FF, / 0x55 goes to the gBacklashYawTexAnimA and
 * gBacklashYawTexAnimB entries (anim_tex_set_frame). The balance gBacklashLean moves by
 * gBacklashLeanLeftStep down to backlash_lean_limit(0) with gHeldLeft, by gBacklashLeanRightStep up to
 * backlash_lean_limit(1) with gHeldRight, else by gBacklashLeanReturnStep towards 0.5, and goes to
 * channel 5. gBacklashTilt moves, unless veh_near_top_speed(veh, 10) (near a gear
 * bound), by gBacklashTiltDownStep down to 0 with gHeldBOrZ or by gBacklashTiltUpStep up to 1
 * with gHeldA, otherwise by gBacklashTiltReturnStep towards 0.5, and goes to channel
 * 4. Channel 1 gets the direction (speed < 0) and |speed| / 14 (also passed to
 * veh_set_engine_level); the level gBacklashSteerAnim moves by 5 (down to 0 / up to 100) or
 * by 10 towards 50 and goes to channel 2 as level / 100.
 * The asm clobbers s2, s3, s5, s6 (conventions.txt). */
void backlash_update_anims(void) {
    u8 *base = gBacklashAnim;
    s32 yaw;
    s32 a;
    f32 f;
    f32 lim;
    s32 speed;
    s32 level;
    s32 near;

    CVT_W_S(yaw, gCamHeading * 4096.0f / 360.0f);
    a = *(u16 *) (gBacklashState + 0x4C) + yaw;
    if (a >= 0x1000) {
        a -= 0xFFF;
    }
    a -= gBacklashYawBase;
    if (a < 0) {
        a += 0xFFF;
    }
    if (a >= 0x800) {
        a -= 0x800;
    }
    a = (u32) a / 0x55;
    anim_tex_set_frame(0.0f, (s32) gBacklashYawTexAnimA, a);
    anim_tex_set_frame(0.0f, (s32) gBacklashYawTexAnimB, a);

    f = gBacklashLean;
    if (gHeldLeft != 0) {
        f -= gBacklashLeanLeftStep;
        lim = backlash_lean_limit(0);
        if (f < lim) {
            f = lim;
        }
    } else if (gHeldRight != 0) {
        f += gBacklashLeanRightStep;
        lim = backlash_lean_limit(1);
        if (!(f <= lim)) {
            f = lim;
        }
    } else {
        TOWARDS_HALF(f, gBacklashLeanReturnStep);
    }
    gBacklashLean = f;
    anim_set_frame(f, (AnimChannel *) base, 5, 0);

    f = gBacklashTilt;
    near = veh_near_top_speed(gBacklashState, 10);
    if (near == 0 && gHeldBOrZ != 0) {
        f -= gBacklashTiltDownStep;
        if (f < 0.0f) {
            f = 0.0f;
        }
    } else if (near == 0 && gHeldA != 0) {
        f += gBacklashTiltUpStep;
        if (!(f <= 1.0f)) {
            f = 1.0f;
        }
    } else {
        TOWARDS_HALF(f, gBacklashTiltReturnStep);
    }
    gBacklashTilt = f;
    anim_set_frame(f, (AnimChannel *) base, 4, 0);

    speed = *(s16 *) (gBacklashState + 0x76);
    if (speed < 0) {
        anim_set_reverse((AnimChannel *) base, 1, 1);
        speed = -speed;
    } else {
        anim_set_reverse((AnimChannel *) base, 1, 0);
    }
    if (speed != 0) {
        speed = (u32) speed / 14;
    }
    anim_set_speed((AnimChannel *) base, 1, speed);
    veh_set_engine_level(speed);

    level = gBacklashSteerAnim;
    if (gHeldLeft != 0) {
        level -= 5;
        if (level < 0) {
            level = 0;
        }
    } else if (gHeldRight != 0) {
        level += 5;
        if (level >= 0x65) {
            level = 0x64;
        }
    } else if (level < 0x32) {
        level += 10;
        if (level >= 0x33) {
            level = 0x32;
        }
    } else {
        level -= 10;
        if (level < 0x32) {
            level = 0x32;
        }
    }
    gBacklashSteerAnim = level;
    anim_set_frame((f32) level / 100.0f, (AnimChannel *) base, 2, 0);
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/71140/backlash_update_anims.s")
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
extern u8 * N64P gBacklashModel;  /* this vehicle's model header */

/* Backlash's model matrix, parts and points (0x802B7030).
 * Rebuilds vehicle 5's model matrices and collision points. The model
 * header gBacklashModel holds offsets (from the header) to: the part records at
 * +4 .. +8, the point list at +0 .. +4 and, at +0x18, a word whose +4 is the
 * root matrix's offset into the current matrix buffer (gBacklashMtxA when
 * gBufferIndex is set, else gBacklashMtxB). With rotation y = the heading (u16
 * +0x4C of gBacklashState, the asm's $gp) in gModelRotY, the root matrix is built
 * at position gBacklashPos[0..2] with scale 0x4268 (mtx_build_model); then the
 * parts are placed (veh_place_parts, tag 5) and the points transformed
 * (veh_points_transform, id 5) through the current buffer.
 * Register convention: mtx_chain_transform's chain registers pass through regs
 * (a3, s1, s0 in; v1, a0, a3, s1, s2 out; conventions.txt). As in the asm,
 * s2 = the root matrix (mtx_build_model leaves it there) and, for
 * veh_points_transform, v1/a0 = position y/z (veh_place_parts preserves them). The
 * asm also leaves s4 = the buffer, s5/s6 = position y/z, s7 = 0x4268 and
 * mtx_build_model's f12/f14 scratch (conventions.txt: clobbers); its asm
 * callers don't read them (the survey lists their later saves/passes).
 * Asm callers keep t6, t7 live. */
void backlash_build_model(MtxChainRegs *regs) {
    u8 *hdr = gBacklashModel;
    u8 *buf = gBufferIndex != 0 ? (u8 *) gBacklashMtxA : (u8 *) gBacklashMtxB;
    s32 *m = (s32 *) (*(s32 *) (hdr + *(s32 *) (hdr + 0x18) + 4) + buf);

    gModelRotY = *(u16 *) (gBacklashState + 0x4C);
    mtx_build_model(gBacklashPos[0], gBacklashPos[1], gBacklashPos[2], 0x4268, m);
    regs->s2 = (s32) m;
    buf = gBufferIndex != 0 ? (u8 *) gBacklashMtxA : (u8 *) gBacklashMtxB;
    hdr = gBacklashModel;
    veh_place_parts(gBacklashPos[0], gBacklashPos[1], gBacklashPos[2], 5, hdr + *(s32 *) (hdr + 4),
                  hdr + *(s32 *) (hdr + 8), buf, regs);
    regs->v1 = gBacklashPos[1];
    regs->a0 = gBacklashPos[2];
    hdr = gBacklashModel;
    veh_points_transform(5, (s16 *) (hdr + *(s32 *) hdr), (s16 *) (hdr + *(s32 *) (hdr + 4)), buf, regs);
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/71140/backlash_build_model.s")
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
extern f32 gBacklashSteerRateDiv;

/* Backlash's steering rate (by speed) (0x802B7168).
 * Speed (s16 at +0x76 of gBacklashState, the asm's $gp) / 6.0 when any of the
 * bytes at +0x96/+0x97/+0x98 is 1, else / gBacklashSteerRateDiv, rounded to nearest.
 * The asm returns it in s3 (see tools_port/conventions.txt). Its asm caller
 * backlash_update keeps a0-a3 live (a mixed N64 build would need a thunk).
 * Same shape as sideswipe_steer_rate (6B4A0). */
s32 backlash_steer_rate(void) {
    f32 div;
    s32 r;

    if (gBacklashState[0x96] == 1 || gBacklashState[0x97] == 1 || gBacklashState[0x98] == 1) {
        div = 6.0f;
    } else {
        div = gBacklashSteerRateDiv;
    }
    CVT_W_S(r, *(s16 *) (gBacklashState + 0x76) / div);
    return r;
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/71140/backlash_steer_rate.s")
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
/* 0.5 +- |speed|/240*0.5: the channel-5 bound (0x802B71DC).
 * 0.5 +- |speed| / 240 * 0.5 (plus when side != 0, minus otherwise), speed
 * being the s16 at +0x76 of gBacklashState (the asm's $gp). The asm takes side
 * in s2, returns the value in f2 and leaves s3 = 240 (see
 * tools_port/conventions.txt). Its asm caller backlash_update_anims keeps a1-a3, f12
 * and f14 live (a mixed N64 build would need a thunk). Same shape as
 * ballista_lean_limit (853D0, divisor 280). */
f32 backlash_lean_limit(s32 side) {
    s32 speed = *(s16 *) (gBacklashState + 0x76);
    f32 v;

    if (speed < 0) {
        speed = -speed;
    }
    v = (f32) speed / 240.0f * 0.5f;
    if (side != 0) {
        return v + 0.5f;
    }
    return 0.5f - v;
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/71140/backlash_lean_limit.s")
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
extern s16 gBacklashGrip;

/* Gravity x2, bounce, grip by L/R (0x802B7240).
 * Vehicle-module setup leaf (shape of driver_set_physics in 69BB0, other
 * constants): gGravity = gLevelGravity * 2, gBounceThreshold/Damping = 60, 2, then
 * gBacklashGrip = 3200 if either button flag gHeldL/gHeldR is set, else 9000.
 * Register note: the asm touches only at/v0/v1/f0/f2. Its asm callers keep
 * registers live across the call (backlash_ride_follow: t3, t4, f12, f14;
 * backlash_update: a0-a3); a mixed N64 build would need a thunk preserving
 * those, the native port does not. */
void backlash_set_physics(void) {
    gGravity = gLevelGravity * 2.0f;
    gBounceThreshold = 60;
    gBounceDamping = 2;
    gBacklashGrip = (gHeldL != 0 || gHeldR != 0) ? 3200 : 9000;
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/71140/backlash_set_physics.s")
#endif

/* backlash_save_state: two-address trampoline into veh_serialize_state, same
 * confirmed-unreachable-from-C 8-byte sd-$ra frame as veh_check_wrap
 * (hd_code/679E0.c). Permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
extern u8 gBacklashState[];  /* this vehicle's state block: 0xA6 bytes are (de)serialized */
extern u32 gBacklashPos[]; /* plus these three words */

/* Backlash's state into a buffer (0x802B72DC).
 * Serialize this vehicle's state (gBacklashState[0..0xA5] plus the three words
 * gBacklashPos[0..2]) into `dst` via veh_serialize_state. Returns the callee's result
 * (0xB2, the byte count): the asm leaves the callee's v0 untouched. */
s32 backlash_save_state(u8 *dst) {
    return veh_serialize_state(dst, gBacklashState, gBacklashPos);
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/71140/backlash_save_state.s")
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
/* Backlash's state from a buffer (recording start) (0x802B7308).
 * Inverse of backlash_save_state: restore this vehicle's state from `src` via
 * veh_deserialize_state. Called from 17210.c as `void backlash_load_state(void *)`. */
void backlash_load_state(void *src) {
    veh_deserialize_state(src, gBacklashState, gBacklashPos);
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/71140/backlash_load_state.s")
#endif
