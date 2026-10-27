#include "common.h"
#include <ultra64.h>
#include "game/game.h"

#ifdef NON_MATCHING
/* Shared declarations for the vehicle type 2 (state block gThunderfistState)
 * rewrites below: the same code as vehicle 16's in 8AEE0.c with id 2 and
 * scale 0x6590. The structs mirror the callees' C rewrites (56040.c,
 * 62740.c). */


extern u8 gThunderfistState[];  /* vehicle 2 state block */
extern u32 gThunderfistPos[]; /* x, y, z */
extern u8 * N64P gThunderfistModel;   /* model */
extern u64 * N64P gThunderfistMtxA;
extern u64 * N64P gThunderfistMtxB;
extern u8 gThunderfistAnim[]; /* animation channel table */
extern s16 gThunderfistDeflectHeading;
extern s8 gThunderfistDeflecting;
extern u8 gThunderfistUnusedFlag;
extern u8 gThunderfistHeadOnHit;
extern u8 gThunderfistNonBlockingParts[];


s32 thunderfist_zone_alpha(ZoneScanRegs *r);
void thunderfist_update_actions(s32 t6, s32 t7, s32 s0, s32 s1, s32 s2, s32 s3, s32 s4);
s32 thunderfist_steer_rate(void);
void thunderfist_set_physics(void);
s32 thunderfist_build_model(s32 a3, s32 s0, s32 s1, VehMtxOut *out);
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
/* Sets up Thunderfist (type 2) (0x802B0DA0).
 * Spawn vehicle type 2 (from the dispatcher lvl_spawn_vehicles). Same code as
 * cyclone_spawn (8AEE0.c, vehicle 16) with id 2, scale 0x6590 and this
 * vehicle's globals: model gThunderfistModel, 0x1400-byte matrix buffers
 * gThunderfistMtxA/B, wheel offsets and speed bands, state bytes +0xA1/A2 = 0 and
 * +0xA3 = 1, the ground slots, the animation channels of gThunderfistAnim
 * (channel 0, then channel 1 also started), the parts, veh_add_shadow; one
 * frame of thunderfist_update with +0x9A set; veh_sync_buffers copies buffer CC to
 * C8, the matrix copy, and gEngineSound is cleared.
 * Register convention (conventions.txt): model s2, x t7, y s3, z s0, heading
 * s1, fp; t6 and veh_init_ground's FP state pass through to thunderfist_update. The
 * asm restores t0-t5 (lvl_spawn_vehicles keeps t1, t2 live) and leaves s0-s7, fp,
 * gp, f20-f30 changed (the survey's s2-s4, fp, f12-f26 "outputs" are
 * rewritten by lvl_spawn_vehicles before use).
 * Leaked registers (FIDELITY): thunderfist_update's zone registers here are the
 * stub-model values - t6 the caller's, t7 anim_init_model's result (the asm
 * expects the C veh_add_shadow to keep t6/t7), s0 = z, s1 = +4, which in the
 * game anim_init_model (s0 = s1 = 0) and anim_update_model have overwritten; they
 * reach thunderfist_update_actions's fx_spawn records. Likewise f14-f26 after
 * anim_update_model (f20). */
void thunderfist_spawn(u8 *model, s32 x, s32 y, s32 z, s32 heading, s32 fpIn, s32 t6, TriSideOut *f) {
    u8 *v = gThunderfistState;
    u8 *buf;
    u64 *b0;
    u64 *b1;
    s32 *s3v;
    s32 t7;

    gThunderfistModel = model;
    buf = gHeapPtr;
    gThunderfistMtxA = (u64 *) buf;
    gThunderfistMtxB = (u64 *) (buf + 0x1400);
    gHeapPtr = buf + 0x2800;
    veh_add_record(VEH_THUNDERFIST, 1, (s32) gThunderfistMtxA, (s32) gThunderfistMtxB, model);
    veh_reset_state(v);
    *(s16 *) (v + 0x52) = 0;
    *(s16 *) (v + 0x54) = 0;
    *(s16 *) (v + 0x56) = 0;
    *(s16 *) (v + 0x58) = 0;
    *(s16 *) (v + 0x5A) = 0;
    *(s16 *) (v + 0x5C) = 0;
    *(s16 *) (v + 0x5E) = 0x50;
    *(s16 *) (v + 0x60) = 0x50;
    *(s16 *) (v + 0x62) = -0x50;
    *(s16 *) (v + 0x64) = 0x50;
    *(s16 *) (v + 0x66) = 0x50;
    *(s16 *) (v + 0x68) = -0x50;
    gThunderfistPos[0] = x;
    gThunderfistPos[1] = y;
    gThunderfistPos[2] = z;
    *(s16 *) (v + 0x4C) = heading;
    v[0xA1] = 0;
    v[0xA2] = 0;
    *(s16 *) (v + 0x4E) = heading;
    *(s16 *) (v + 0x74) = heading;
    v[0xA3] = 1;
    s3v = veh_init_ground((s16 *) (v + 0x52), gThunderfistPos[1], x, z, (s32 *) (v + 4), (s32 *) &gThunderfistPos[1],
                        (s16 *) (v + 0x4C), 2, fpIn, v, f);
    b0 = gThunderfistMtxA;
    b1 = gThunderfistMtxB;
    t7 = anim_init_model((u32 *) b1, (u32 *) b0, (AnimChannel *) gThunderfistAnim, gThunderfistModel);
    anim_set_speed((AnimChannel *) gThunderfistAnim, 0, 0x64);
    anim_set_reverse((AnimChannel *) gThunderfistAnim, 0, 0);
    anim_set_loop_mode((AnimChannel *) gThunderfistAnim, 0, 0);
    anim_set_interp(0.0f, (AnimChannel *) gThunderfistAnim, 0, 0);
    anim_play((AnimChannel *) gThunderfistAnim, 0, 1);
    anim_update_model((u8 *) b0, (u8 *) b1, (AnimChannel *) gThunderfistAnim);
    anim_rewind(0, (AnimChannel *) gThunderfistAnim);
    anim_play((AnimChannel *) gThunderfistAnim, 0, 1);
    anim_update_model((u8 *) b1, (u8 *) b0, (AnimChannel *) gThunderfistAnim);
    *(s16 *) (v + 0x78) = -0x64;
    *(s16 *) (v + 0x7A) = 0;
    *(s16 *) (v + 0x7C) = 4;
    *(s16 *) (v + 0x7E) = 0;
    *(s16 *) (v + 0x80) = 0xA0;
    *(s16 *) (v + 0x82) = 4;
    *(s16 *) (v + 0x84) = 0;
    *(s16 *) (v + 0x86) = 0xA0;
    *(s16 *) (v + 0x88) = 4;
    *(s16 *) (v + 0x8A) = 0;
    *(s16 *) (v + 0x8C) = 0xA0;
    *(s16 *) (v + 0x8E) = 4;
    *(s16 *) (v + 0x90) = 0;
    *(s16 *) (v + 0x92) = 0xA0;
    *(s16 *) (v + 0x94) = 4;
    gThunderfistDeflecting = 0;
    gThunderfistHeadOnHit = 0;
    gThunderfistUnusedFlag = 0;
    model = gThunderfistModel;
    veh_register_parts(2, model + *(s32 *) (model + 4), model + *(s32 *) (model + 8), 0x6590);
    veh_add_shadow(VEH_THUNDERFIST, 0x64, 0x2D, 0x2D);
    anim_set_frame(0.0f, (AnimChannel *) gThunderfistAnim, 1, 0);
    anim_play((AnimChannel *) gThunderfistAnim, 1, 1);
    v[0x9A] = 1;
    thunderfist_update(t6, t7, z, (s32) (v + 4), (s32) &gThunderfistPos[1], (s32) s3v, (s32) (v + 0x4C), f->cross, f->cz,
                  f->side, f->sideZ, f->dz);
    v[0x9A] = 0;
    veh_sync_buffers(gThunderfistMtxB, gThunderfistMtxA, 0x1400);
    model = gThunderfistModel;
    mtx_copy_at((u8 *) gThunderfistMtxB, (u8 *) gThunderfistMtxA, *(s32 *) (model + *(s32 *) (model + 0x18) + 4));
    gEngineSound = NULL;
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/6C5E0/thunderfist_spawn.s")
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
extern u8 gThunderfistState[]; /* this vehicle's state block */

/* 0 while airborne or mid-roll (0x802B1150).
 * Exit check for vehicle type 2 (called from veh_exit_check in hd.c, which
 * declares it void but returns the leftover v0). Returns 1 when none of the
 * bytes at +0x96, +0x97, +0x98 equals 1 and the byte at +0xA1 is 0, else 0.
 * Returns s32: the asm leaves the full 0/1 in v0. Same shape as
 * skyfall_exit_check/ramdozer_exit_check (6E200), backlash_exit_check (71140) and
 * amdream_exit_check (72B80), which lack the +0xA1 test. */
s32 thunderfist_exit_check(void) {
    if (gThunderfistState[0x96] == 1 || gThunderfistState[0x97] == 1 || gThunderfistState[0x98] == 1) {
        return 0;
    }
    if (gThunderfistState[0xA1] != 0) {
        return 0;
    }
    return 1;
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/6C5E0/thunderfist_exit_check.s")
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
extern u64 * N64P gThunderfistMtxA;
extern u64 * N64P gThunderfistMtxB;
extern void * N64P gThunderfistEngineSound; /* engine sound handle */
extern u8 gThunderfistAnim[];  /* this vehicle's animation channel table */

/* Getting out of Thunderfist; stops its engine sound (0x802B11B8).
 * Vehicle-type 2 shutdown (called from hd.c's veh_leave): zeroes the
 * speed (s16 at +0x76), veh_sync_buffers(gThunderfistMtxA, gThunderfistMtxB, 0x1400), stops
 * channel 31 of gThunderfistAnim, veh_stop_engine_sounds(), then stops the engine sound
 * gThunderfistEngineSound (snd_stop). The asm leaves v1 = &gThunderfistAnim only if
 * snd_stop (C) happens to keep it; the C caller doesn't use it. */
void thunderfist_leave(void) {
    *(s16 *) (gThunderfistState + 0x76) = 0;
    veh_sync_buffers(gThunderfistMtxA, gThunderfistMtxB, 0x1400);
    anim_stop(0x1F, (AnimChannel *) gThunderfistAnim);
    veh_stop_engine_sounds();
    snd_stop(gThunderfistEngineSound);
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/6C5E0/thunderfist_leave.s")
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
extern u8 gThunderfistState[]; /* this vehicle's state block */
extern void * N64P gThunderfistEngineSound; /* engine sound handle */
extern u8 gThunderfistTexAnim[]; /* key of this vehicle's anim_tex_find entry */
extern u8 gThunderfistAnim[]; /* this vehicle's animation channel table */

/* Getting into Thunderfist: engine sound 0x50 (0x802B1228).
 * Vehicle-type 2 setup (called from 00000.c / 17210.c): clears byte 0x99 of
 * the state block, gCamDistance / gCamHeightBase = 4200, 3000, starts the engine sound 0x50
 * (handle in gThunderfistEngineSound), sets the gThunderfistTexAnim entry's fields (25, 0, 0) and
 * the animation channels 7, 8, 9 (and 1, 5, 3) of gThunderfistAnim.
 * The asm also points $gp at gThunderfistState and leaves it there (conventions.txt:
 * clobbers gp) and returns with v1 = 0 (the last setter's v1); C callers
 * ignore both. */
void thunderfist_enter(void) {
    gThunderfistState[0x99] = 0;
    gCamDistance = 0x1068;
    gCamHeightBase = 0xBB8;
    snd_play(gSfxBank, SFX_ENGINE_MECH, &gThunderfistEngineSound);
    anim_tex_set_speed((s32) gThunderfistTexAnim, 0x19);
    anim_tex_set_reverse((s32) gThunderfistTexAnim, 0);
    anim_tex_set_loop_mode((s32) gThunderfistTexAnim, 0);
    anim_set_speed((AnimChannel *) gThunderfistAnim, 7, 2);
    anim_set_loop_mode((AnimChannel *) gThunderfistAnim, 7, 1);
    anim_set_interp(0.5f, (AnimChannel *) gThunderfistAnim, 7, 1);
    anim_set_speed((AnimChannel *) gThunderfistAnim, 8, 2);
    anim_set_loop_mode((AnimChannel *) gThunderfistAnim, 8, 1);
    anim_set_interp(0.5f, (AnimChannel *) gThunderfistAnim, 8, 1);
    anim_set_speed((AnimChannel *) gThunderfistAnim, 9, 4);
    anim_set_loop_mode((AnimChannel *) gThunderfistAnim, 9, 1);
    anim_set_interp(0.5f, (AnimChannel *) gThunderfistAnim, 9, 1);
    anim_set_interp(0.5f, (AnimChannel *) gThunderfistAnim, 1, 1);
    anim_set_interp(0.5f, (AnimChannel *) gThunderfistAnim, 5, 1);
    anim_set_interp(0.0f, (AnimChannel *) gThunderfistAnim, 3, 0);
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/6C5E0/thunderfist_enter.s")
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
/* The reground body of the others; never called (no type-2 case) (0x802B13D8).
 * Re-ground vehicle type 2: the ground slots (veh_reground, key 2) at its
 * position, the model matrices (thunderfist_build_model), then the gVehicles record
 * (veh_set_record). No callers are known. Same as cyclone_reground (8AEE0.c).
 * Register convention (conventions.txt): fp and a3 (passed through to
 * veh_reground and thunderfist_build_model) and the FP pass-through state f12-f26. */
void thunderfist_reground(s32 fpIn, s32 a3, f32 f12, f32 f14, f32 f20, f32 f22, f32 f24, f32 f26) {
    u8 *v = gThunderfistState;
    TriSideOut f;
    Out802A9A60 o9;
    VehMtxOut mo;
    s32 x = gThunderfistPos[0];
    s32 z = gThunderfistPos[2];

    f.pz = f12;
    f.cross = f14;
    f.cz = f20;
    f.side = f22;
    f.sideZ = f24;
    f.dz = f26;
    veh_reground((s16 *) (v + 0x52), gThunderfistPos[1], x, z, (s32 *) (v + 4), (s32 *) &gThunderfistPos[1],
                  (s16 *) (v + 0x4C), 2, fpIn, v, &f, &o9);
    thunderfist_build_model(a3, z, (s32) o9.s1, &mo);
    veh_set_record(gThunderfistPos[2], 2, gThunderfistPos[0], gThunderfistPos[1], v);
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/6C5E0/thunderfist_reground.s")
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
extern u32 gThunderfistPos[]; /* x, y, z */
/* Veh_zone_alpha at Thunderfist's position (0x802B14E8).
 * Zone level lookup (veh_zone_alpha) for vehicle id 2 at its position
 * gThunderfistPos..+8; returns veh_zone_alpha's v1 (the zone list end).
 * Register convention: the asm passes veh_zone_alpha's scan registers t6, t7,
 * s0-s4 through (in and out; here through r), leaves a3 = id and clobbers
 * s5, s6 (conventions.txt). Asm caller thunderfist_update keeps a0, f12 and f14 live
 * (a mixed N64 build would need a thunk; the native port won't). */
s32 thunderfist_zone_alpha(ZoneScanRegs *r) {
    return veh_zone_alpha(2, gThunderfistPos[0], gThunderfistPos[1], gThunderfistPos[2], r);
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/6C5E0/thunderfist_zone_alpha.s")
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
/* Thunderfist driven by the player, per frame (0x802B152C).
 * Per-frame update of vehicle type 2 (from 00000.c's veh_update_player and once
 * from thunderfist_spawn); the same code as cyclone_update (8AEE0.c) with id 2:
 * zone level (thunderfist_zone_alpha), the animation state machine (thunderfist_update_actions,
 * which takes the zone registers thunderfist_zone_alpha leaves, unless +0x9A), timers
 * (thunderfist_set_physics), steering with the rate from thunderfist_steer_rate, throttle
 * (delta 12), braking when +0xA1 is 0, heading (rate 0x59D8, no sound),
 * slope and speed (kind 2, no clamp, 120.0), veh_set_target_heading while gThunderfistDeflecting,
 * the move, ground contact (gBounceSound = 0, divisors 0x78), animation
 * (anim_update_model on gThunderfistAnim), model matrices (thunderfist_build_model), the
 * vehicle-state reset, collision passes and obj_collide_vehicle. On a ring hit
 * (gCollBlocked): gThunderfistDeflecting = 1, gThunderfistHeadOnHit = 1 if the ring distance is under
 * 0x190 (which also makes the v1 handed to veh_step_heading 1), the ring
 * steering with scale 0.25 (new heading also to gThunderfistDeflectHeading); else gThunderfistDeflecting
 * and gThunderfistHeadOnHit are cleared. Finally gPlayerX.. and the gVehicles record.
 * Register convention (conventions.txt): zone registers t6, t7, s0-s4 and the
 * FP pass-through state f14-f26 (veh_ground_contact). The C caller declares it
 * `void (void)` (garbage in the game as in the port; update 00000.c's
 * prototype in the port headers). The survey's a0 input and v1/f12/f14
 * outputs aren't real (a0 is set before use; v1 = y, f12/f14 sine-routine
 * leftovers thunderfist_spawn doesn't use). veh_collision_setup gets b0 = 2 and h2 =
 * the end of the model's point list, thunderfist_build_model's t0/t2, as the asm
 * leaves them (a slip in the original).
 * Leaked registers (FIDELITY): thunderfist_update_actions (anim_blend_channels) changes f20/f30
 * in the game before f20 reaches veh_ground_contact; thunderfist_build_model's a3/s0/s1
 * are the stub-model values (+0x34, +0x96, &z) where in the game
 * anim_update_model has changed s0/s1. */
void thunderfist_update(s32 t6, s32 t7, s32 s0, s32 s1, s32 s2, s32 s3, s32 s4, f32 f14, f32 f20, f32 f22, f32 f24,
                   f32 f26) {
    u8 *v = gThunderfistState;
    ZoneScanRegs zr;
    TriSideOut f;
    Regs802A8768 r8;
    Out802A860C o;
    VehMtxOut mo;
    f32 f12;
    s32 x;
    s32 tag;
    s32 d;
    s32 turn;
    s32 cur;
    s32 tgt;

    zr.t6 = t6;
    zr.t7 = t7;
    zr.s0 = s0;
    zr.s1 = s1;
    zr.s2 = s2;
    zr.s3 = s3;
    zr.s4 = s4;
    thunderfist_zone_alpha(&zr);
    if (v[0x9A] == 0) {
        thunderfist_update_actions(zr.t6, zr.t7, zr.s0, zr.s1, zr.s2, zr.s3, zr.s4);
    }
    thunderfist_set_physics();
    veh_steer(thunderfist_steer_rate(), (u16 *) (v + 0x4C));
    veh_throttle(v, (s16 *) (v + 0x76), 3, v + 0x96, (s16 *) (v + 0x78), 0xC);
    if (v[0xA1] == 0) {
        veh_coast(v);
    }
    veh_turn_heading(v, (u16 *) (v + 0x74), 0x59D8, (s16 *) (v + 0x76), (u16 *) (v + 0x4C), (u16 *) (v + 0x4E),
                  (s8 *) (v + 0x99), 0);
    f12 = veh_slope((s16 *) (v + 0x76), v + 0x96, (s32 *) (v + 4), (f32 *) v);
    veh_apply_slope(v, (s16 *) (v + 0x76), 2, (s8 *) (v + 0x96), (s32 *) (v + 4), 0, 120.0f);
    if (gThunderfistDeflecting != 0) {
        veh_set_target_heading(v, &gThunderfistDeflectHeading);
    }
    x = veh_move_along_heading(f12, *(u16 *) (v + 0x4E), (s16 *) (v + 0x76), (s32 *) &gThunderfistPos[0],
                      (s32 *) &gThunderfistPos[2], &o);
    gBounceSound = 0;
    r8.s3 = o.s3;
    f.pz = f12;
    f.cross = f14;
    f.cz = f20;
    f.side = f22;
    f.sideZ = f24;
    f.dz = f26;
    veh_ground_contact(v, 2, (s32 *) &gThunderfistPos[0], (s32 *) &gThunderfistPos[1], (s32 *) &gThunderfistPos[2], x, o.t1, 0x78,
                  0x78, (s16 *) (v + 0x4C), v + 0x96, (s16 *) (v + 0x52), (s32 *) (v + 0x28), (s32 *) (v + 0x40),
                  (s32 *) (v + 0x34), (s32 *) (v + 4), &r8, &f);
    if (gBufferIndex != 0) {
        anim_update_model((u8 *) gThunderfistMtxA, (u8 *) gThunderfistMtxB, (AnimChannel *) gThunderfistAnim);
    } else {
        anim_update_model((u8 *) gThunderfistMtxB, (u8 *) gThunderfistMtxA, (AnimChannel *) gThunderfistAnim);
    }
    tag = thunderfist_build_model((s32) (v + 0x34), (s32) (v + 0x96), (s32) &gThunderfistPos[2], &mo);
    veh_collision_setup(gThunderfistPos[2], (s32) gThunderfistNonBlockingParts, 0, 0, gThunderfistPos[0], gThunderfistPos[1], tag, *(s16 *) (v + 0x76),
                  mo.t2, 0, 2, v);
    veh_collide_spheres(2, v);
    veh_collide_world(2);
    gCollVehicleAnim = gThunderfistAnim;
    obj_collide_vehicle(2, v);
    if (gCollBlocked != 0) {
        veh_prepare_escape(v);
        gThunderfistDeflecting = 1;
        gThunderfistHeadOnHit = 0;
        turn = veh_free_arc_mid();
        d = *(u16 *) (v + 0x4E) - 0x800;
        if (d < 0) {
            d += 0xFFF;
        }
        d -= turn;
        if (d < 0) {
            d = -d;
        }
        if (d > 0x800) {
            d = 0xFFF - d;
        }
        if (d < 0x190) {
            d = 1;
            gThunderfistHeadOnHit = 1;
        }
        veh_reverse_if_behind(v);
        turn = veh_turn_into_free_arc(v, *(u16 *) (v + 0x4E), *(u16 *) (v + 0x4C), &cur, 0.25f);
        gThunderfistDeflectHeading = cur;
        *(s16 *) (v + 0x4E) = cur;
        *(s16 *) (v + 0x74) = cur;
        veh_step_heading(v, turn, d, &tgt);
        veh_hit_slowdown(v, 0);
    } else {
        gThunderfistDeflecting = 0;
        gThunderfistHeadOnHit = 0;
    }
    gPlayerX = gThunderfistPos[0];
    gPlayerY = gThunderfistPos[1];
    gPlayerZ = gThunderfistPos[2];
    gPlayerSpeed = *(s16 *) (v + 0x76);
    gPlayerMoveHeading = *(u16 *) (v + 0x4E);
    gPlayerHeading = *(u16 *) (v + 0x4C);
    veh_set_record(gThunderfistPos[2], 2, gThunderfistPos[0], gThunderfistPos[1], v);
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/6C5E0/thunderfist_update.s")
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
extern s16 gThunderfistStepPose;    /* last pose byte seen on channel 1/5 */
extern u8 gThunderfistHeadOnHit;
extern f32 gThunderfistRollBlurScale;

#define VEH2_SPEED (*(s16 *) (gThunderfistState + 0x76))

/* Channel idx's field 0x10 ((s8), anim_get_state's v1); out gets the rest. */
static s32 veh2_ch_get(s32 idx, s32 *out) {
    anim_get_state(idx, (AnimChannel *) gThunderfistAnim, out);
    return out[0];
}

/* Channel 31 (the blend track) starts: 039C = v, 03D4 = 0, 040C = 0, 0290 = 1. */
static void veh2_ch31_start(s32 v) {
    anim_set_speed((AnimChannel *) gThunderfistAnim, 0x1F, v);
    anim_set_reverse((AnimChannel *) gThunderfistAnim, 0x1F, 0);
    anim_set_loop_mode((AnimChannel *) gThunderfistAnim, 0x1F, 0);
    anim_play((AnimChannel *) gThunderfistAnim, 0x1F, 1);
}

/* Channel ch: 03D4 = (speed < 0), 039C = |speed| / 24. */
static void veh2_ch_speed(s32 ch) {
    s32 speed = VEH2_SPEED;

    anim_set_reverse((AnimChannel *) gThunderfistAnim, ch, speed < 0);
    if (speed < 0) {
        speed = -speed;
    }
    anim_set_speed((AnimChannel *) gThunderfistAnim, ch, (u32) speed / 24);
}

/* State 0 (+0xA1 == 0): driving. */
static void veh2_state0(s32 t6, s32 t7, s32 s0, s32 s1, s32 s2, s32 s3, s32 s4) {
    Io802A6274 io;
    s32 o[8];
    s32 r;
    s32 speed;

    /* the pose byte of channel 1 (or else 5) when it's on: 6 -> sound 0x4E, 2 -> 0x4F on a change */
    if (veh2_ch_get(1, o) == 1 || veh2_ch_get(5, o) == 1) {
        r = gThunderfistStepPose;
        gThunderfistStepPose = o[6];
        if (o[6] != r) {
            if (o[6] == 6) {
                snd_play(gSfxBank, SFX_MECH_STEP_A, NULL);
            } else if (o[6] == 2) {
                snd_play(gSfxBank, SFX_MECH_STEP_B, NULL);
            }
        }
    }

    if (VEH2_SPEED == 0) {
        /* standing: blend back to an idle pose once (+0xA3), then idle anims at random */
        if (gThunderfistState[0xA3] != 0) {
            anim_set_frame(0.0f, (AnimChannel *) gThunderfistAnim, 7, 0);
            if (veh2_ch_get(0x1F, o) != 0) {
                anim_stop(0x1F, (AnimChannel *) gThunderfistAnim);
                anim_blend_channels(0x1F, 7, (AnimChannel *) gThunderfistAnim);
            } else if (veh2_ch_get(1, o) != 0) {
                anim_stop(1, (AnimChannel *) gThunderfistAnim);
                anim_blend_channels(1, 7, (AnimChannel *) gThunderfistAnim);
            } else if (veh2_ch_get(5, o) != 0) {
                anim_stop(5, (AnimChannel *) gThunderfistAnim);
                anim_blend_channels(5, 7, (AnimChannel *) gThunderfistAnim);
            } else {
                anim_set_frame(0.0f, (AnimChannel *) gThunderfistAnim, 1, 0);
                anim_blend_channels(1, 7, (AnimChannel *) gThunderfistAnim);
            }
            veh2_ch31_start(0x1E);
        }
        gThunderfistState[0xA3] = 0;
        if (veh2_ch_get(0x1F, o) == 1 || veh2_ch_get(7, o) == 1 || veh2_ch_get(8, o) == 1 ||
            veh2_ch_get(9, o) == 1) {
            return;
        }
        if (util_rand2_range(0, 30) != 0) {
            return;
        }
        r = util_rand2_range(0, 2);
        r = (r == 0) ? 7 : (r == 1) ? 8 : 9;
        anim_set_frame(0.0f, (AnimChannel *) gThunderfistAnim, r, 0);
        anim_play((AnimChannel *) gThunderfistAnim, r, 1);
        return;
    }

    /* moving: blend from the idle pose once (+0xA3 == 1 afterwards) */
    if (gThunderfistState[0xA3] != 1) {
        anim_set_frame(0.0f, (AnimChannel *) gThunderfistAnim, 1, 0);
        if (veh2_ch_get(7, o) != 0) {
            anim_blend_channels(7, 1, (AnimChannel *) gThunderfistAnim);
            anim_stop(7, (AnimChannel *) gThunderfistAnim);
        } else if (veh2_ch_get(8, o) != 0) {
            anim_blend_channels(8, 1, (AnimChannel *) gThunderfistAnim);
            anim_stop(8, (AnimChannel *) gThunderfistAnim);
        } else if (veh2_ch_get(9, o) != 0) {
            anim_blend_channels(9, 1, (AnimChannel *) gThunderfistAnim);
            anim_stop(9, (AnimChannel *) gThunderfistAnim);
        } else {
            anim_set_frame(0.0f, (AnimChannel *) gThunderfistAnim, 7, 0);
            anim_blend_channels(7, 1, (AnimChannel *) gThunderfistAnim);
        }
        veh2_ch31_start(0x28);
    }
    gThunderfistState[0xA3] = 1;
    if (veh2_ch_get(0x1F, o) == 1) {
        return;
    }
    gVehAttacking = 0;
    if ((gControlMethod == 0 && (gHeldA != 0 || gHeldB != 0)) || gHeldL != 0 || gHeldR != 0) {
        if (VEH2_SPEED >= 0x96) {
            /* fast with a button held: switch to state 1 */
            gThunderfistState[0xA1] = 1;
            anim_set_frame(0.0f, (AnimChannel *) gThunderfistAnim, 2, 0);
            if (gThunderfistState[0xA2] == 0) {
                anim_blend_channels(1, 2, (AnimChannel *) gThunderfistAnim);
                anim_stop(1, (AnimChannel *) gThunderfistAnim);
            } else if (gThunderfistState[0xA2] == 1) {
                anim_blend_channels(5, 2, (AnimChannel *) gThunderfistAnim);
                anim_stop(5, (AnimChannel *) gThunderfistAnim);
            } else {
                return; /* asm: syscall (+0xA2 is only ever 0 or 1) */
            }
            veh2_ch31_start(0x23);
            mb_start(6, gThunderfistRollBlurScale, 100);
            return;
        }
    }

    if ((((u32) gFrameDrawTime >> 8) & 0x2F) == 0) {
        io.a3 = 1;
        io.t6 = t6;
        io.s1 = s1;
        fx_spawn(&io, gFxSparks, 0xEA60, 1, 2, 2, 0, t7, s0, s2, s3, s4, 1);
    }

    /* drive anim on channel 1 (+0xA2 == 0) or 5 (+0xA2 == 1), switching at |speed| 120 */
    if (gThunderfistState[0xA2] == 0) {
        goto ch1;
    }
    if (gThunderfistState[0xA2] != 1) {
        return; /* asm: syscall */
    }
ch5:
    if (veh2_ch_get(5, o) != 0) {
        veh2_ch_speed(5);
        return;
    }
    goto pick;
ch1:
    if (veh2_ch_get(1, o) != 0) {
        veh2_ch_speed(1);
        return;
    }
pick:
    speed = VEH2_SPEED;
    if (speed < 0) {
        speed = -speed;
    }
    if (speed >= 0x78) {
        anim_play((AnimChannel *) gThunderfistAnim, 5, o[1] != 0 ? 2 : 1);
        anim_set_frame(0.0f, (AnimChannel *) gThunderfistAnim, 5, 0);
        gThunderfistState[0xA2] = 1;
        goto ch5;
    }
    anim_play((AnimChannel *) gThunderfistAnim, 1, o[1] != 0 ? 2 : 1);
    anim_set_frame(0.0f, (AnimChannel *) gThunderfistAnim, 1, 0);
    gThunderfistState[0xA2] = 0;
    goto ch1;
}

/* Back to state 0 from state 1/2 (the shared tail of both). */
static void veh2_to_state0_tail(void) {
    veh2_ch31_start(0x32);
    mb_stop();
    gThunderfistState[0xA1] = 0;
}

/* Walk animations and the roll / impact state machine (0x802B18F4).
 * Vehicle type 2 animation/state update ($gp = gThunderfistState, read as the
 * global), a state machine on the byte at +0xA1:
 * 0: driving (veh2_state0): pose-change sounds, idle/drive blends on the
 *    channels of gThunderfistAnim, a fx_spawn record every 0x2F-masked frame,
 *    and the switch to state 1 when fast with a button held.
 * 1: starts sound 0x51 (handle gEngineSound) once; +0x9C set: speed halves and
 *    state 3; else +0x9D or gThunderfistHeadOnHit set: back to 0; else speed 446 and,
 *    once channel 31 is done, channel 2 plays and state 2.
 * 2: as 1 without the sound and with channel 2 blended away.
 * 3: stops the sound, fx_spawn (data 0x222E0 at (2, 1, 1)),
 *    veh_prev_contacts_clear, then once channel 31 is done channel 3 plays, state 4.
 * 4: same record, then once channel 3 is done back to state 0.
 * Other +0xA1 / +0xA2 values hit a `syscall` in the asm (never happens in
 * the game); this returns instead.
 * Register convention: t6, t7, s0-s4 pass through to fx_spawn (t6 and
 * s1 in its in/out block); s1, s5 change, and f20/f30 are left as
 * anim_blend_channels leaves them (conventions.txt: clobbers). The asm caller
 * thunderfist_update reads f20 afterwards and the asm expects snd_play (C)
 * to keep t6/t7 for the following fx_spawn; a mixed N64 build would
 * need a thunk, the native port doesn't. */
void thunderfist_update_actions(s32 t6, s32 t7, s32 s0, s32 s1, s32 s2, s32 s3, s32 s4) {
    Io802A6274 io;
    s32 o[8];

    switch (gThunderfistState[0xA1]) {
        case 0:
            veh2_state0(t6, t7, s0, s1, s2, s3, s4);
            return;

        case 1:
            if (gEngineSound == NULL) {
                snd_play(gSfxBank, SFX_ROLL_LOOP, &gEngineSound);
            }
            if (gThunderfistState[0x9C] != 0) {
                VEH2_SPEED = VEH2_SPEED >> 1;
                mb_stop();
                gThunderfistState[0xA1] = 3;
                anim_set_frame(0.0f, (AnimChannel *) gThunderfistAnim, 3, 3);
                anim_blend_channels(0x1F, 3, (AnimChannel *) gThunderfistAnim);
                veh2_ch31_start(0x21);
                gVehAttacking = 1;
                return;
            }
            if (gThunderfistState[0x9D] != 0 || gThunderfistHeadOnHit != 0) {
                if (VEH2_SPEED >= 0) {
                    VEH2_SPEED = 0x3C;
                }
                veh_stop_engine_sounds();
                anim_set_frame(0.0f, (AnimChannel *) gThunderfistAnim, 1, 0);
                anim_blend_channels(0x1F, 1, (AnimChannel *) gThunderfistAnim);
                veh2_to_state0_tail();
                return;
            }
            VEH2_SPEED = 0x1BE;
            if (veh2_ch_get(0x1F, o) == 1) {
                return;
            }
            anim_set_speed((AnimChannel *) gThunderfistAnim, 2, 10);
            anim_set_reverse((AnimChannel *) gThunderfistAnim, 2, 0);
            anim_set_loop_mode((AnimChannel *) gThunderfistAnim, 2, 0);
            anim_play((AnimChannel *) gThunderfistAnim, 2, 1);
            gThunderfistState[0xA1] = 2;
            return;

        case 2:
            if (gThunderfistState[0x9C] != 0) {
                VEH2_SPEED = VEH2_SPEED >> 1;
                gThunderfistState[0xA1] = 3;
                anim_set_frame(0.0f, (AnimChannel *) gThunderfistAnim, 3, 3);
                anim_blend_channels(2, 3, (AnimChannel *) gThunderfistAnim);
                anim_stop(2, (AnimChannel *) gThunderfistAnim);
                veh2_ch31_start(0x21);
                gVehAttacking = 1;
                return;
            }
            if (gThunderfistState[0x9D] == 0 && gThunderfistHeadOnHit == 0) {
                VEH2_SPEED = 0x1BE;
                if (veh2_ch_get(2, o) == 1) {
                    return;
                }
            }
            veh_stop_engine_sounds();
            if (VEH2_SPEED >= 0) {
                VEH2_SPEED = 0x3C;
            }
            anim_set_frame(0.0f, (AnimChannel *) gThunderfistAnim, 1, 0);
            anim_stop(2, (AnimChannel *) gThunderfistAnim);
            anim_blend_channels(2, 1, (AnimChannel *) gThunderfistAnim);
            veh2_to_state0_tail();
            return;

        case 3:
            if (gEngineSound != NULL) {
                veh_stop_engine_sounds();
                snd_play(gSfxBank, SFX_ROLL_CRASH, NULL);
            }
            io.a3 = 1;
            io.t6 = t6;
            io.s1 = s1;
            fx_spawn(&io, gFxSparks, 0x222E0, 1, 2, 1, 1, t7, s0, s2, s3, s4, 1);
            veh_prev_contacts_clear();
            if (veh2_ch_get(0x1F, o) == 1) {
                return;
            }
            gVehAttacking = 0;
            mb_stop();
            anim_set_speed((AnimChannel *) gThunderfistAnim, 3, 8);
            anim_set_reverse((AnimChannel *) gThunderfistAnim, 3, 0);
            anim_set_loop_mode((AnimChannel *) gThunderfistAnim, 3, 0);
            anim_play((AnimChannel *) gThunderfistAnim, 3, 1);
            gThunderfistState[0xA1] = 4;
            return;

        case 4:
            io.a3 = 1;
            io.t6 = t6;
            io.s1 = s1;
            fx_spawn(&io, gFxSparks, 0x222E0, 1, 2, 1, 1, t7, s0, s2, s3, s4, 1);
            veh_prev_contacts_clear();
            if (veh2_ch_get(3, o) == 1) {
                return;
            }
            gVehAttacking = 1;
            anim_set_frame(0.0f, (AnimChannel *) gThunderfistAnim, 1, 0);
            anim_set_frame(0.0f, (AnimChannel *) gThunderfistAnim, 5, 0);
            gThunderfistState[0xA1] = 0;
            return;

        default:
            return; /* asm: syscall */
    }
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/6C5E0/thunderfist_update_actions.s")
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
/* Thunderfist's model matrix, parts and points (0x802B2768).
 * Model matrices of vehicle type 2: the root matrix (mtx_build_model at its
 * position, scale 0x6590, rotation (0, heading + 0x400 wrapped by 0xFFF when
 * >= 0x1000, 0)) into the current matrix buffer (gBufferIndex picks
 * gThunderfistMtxA or gThunderfistMtxB) at the offset the model's +0x18 table gives; then
 * the model's part list (+4..+8, veh_place_parts, tag 2) and point list
 * (+0..+4, veh_points_transform) through the matrix chain. Same as cyclone_build_model
 * (8AEE0.c).
 * Register convention (conventions.txt): a3, s0, s1 pass into the matrix
 * chain registers (mtx_chain_transform reads them only for a count of 0), s2 is the
 * matrix mtx_build_model leaves there; outputs t0 = 2 (the return value), t2 =
 * end of the point list, s1 = the chain's y', s4 = the buffer (thunderfist_update
 * passes t0 and t2 on to veh_collision_setup). The asm also leaves f12/f14 as the
 * sine routines did (not modelled) and clobbers s2, s5-s7; asm callers keep
 * t6/t7 live (a mixed N64 build would need a thunk). */
s32 thunderfist_build_model(s32 a3, s32 s0, s32 s1, VehMtxOut *out) {
    u8 *model = gThunderfistModel;
    u8 *base;
    s32 *m;
    MtxChainRegs regs;
    s32 a;
    s32 x;
    s32 y;
    s32 z;

    m = (s32 *) N64_IPTR(*(s32 *) (model + *(s32 *) (model + 0x18) + 4) +
                 (s32) (gBufferIndex != 0 ? gThunderfistMtxA : gThunderfistMtxB));
    a = *(u16 *) (gThunderfistState + 0x4C) + 0x400;
    if (a >= 0x1000) {
        a -= 0xFFF;
    }
    gModelRot[1] = a;
    gModelRot[0] = 0;
    gModelRot[2] = 0;
    mtx_build_model(gThunderfistPos[0], gThunderfistPos[1], gThunderfistPos[2], 0x6590, m);
    base = (u8 *) (gBufferIndex != 0 ? gThunderfistMtxA : gThunderfistMtxB);
    model = gThunderfistModel;
    x = gThunderfistPos[0];
    y = gThunderfistPos[1];
    z = gThunderfistPos[2];
    regs.a3 = a3;
    regs.s1 = s1;
    regs.s2 = (s32) m;
    regs.s0 = s0;
    veh_place_parts(x, y, z, 2, model + *(s32 *) (model + 4), model + *(s32 *) (model + 8), base, &regs);
    model = gThunderfistModel;
    regs.v1 = y;
    regs.a0 = z;
    veh_points_transform(2, (s16 *) (model + *(s32 *) model), (s16 *) (model + *(s32 *) (model + 4)), base, &regs);
    out->t2 = (s32) (model + *(s32 *) (model + 4));
    out->s1 = regs.s1;
    out->s4 = (s32) base;
    return 2;
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/6C5E0/thunderfist_build_model.s")
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
/* 0 stopped, 50 rolling, else 120 (0x802B28B8).
 * 0 when the speed (s16 at +0x76 of gThunderfistState, the asm's $gp) is 0, else 50
 * when the byte at +0xA1 is 1 or 2, else 120. The asm returns it in s3 (see
 * tools_port/conventions.txt). Its asm caller thunderfist_update keeps a0-a3 live
 * across the call (a mixed N64 build would need a thunk). Same shape as
 * cyclone_steer_rate (8AEE0). */
s32 thunderfist_steer_rate(void) {
    u8 t;

    if (*(s16 *) (gThunderfistState + 0x76) == 0) {
        return 0;
    }
    t = gThunderfistState[0xA1];
    if (t == 2 || t == 1) {
        return 50;
    }
    return 120;
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/6C5E0/thunderfist_steer_rate.s")
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING

/* Thunderfist's gravity and landing bounce (0x802B2900).
 * Vehicle-module setup leaf, identical to driver_set_physics (69BB0):
 * gGravity = gLevelGravity * 4, gBounceThreshold/Damping = 40, 3.
 * Register note: the asm touches only at/v0/v1/f0/f2. Its asm caller
 * thunderfist_update keeps a0-a3 live across the call; a mixed N64 build would
 * need a thunk preserving those, the native port does not. */
void thunderfist_set_physics(void) {
    gGravity = gLevelGravity * 4.0f;
    gBounceThreshold = 40;
    gBounceDamping = 3;
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/6C5E0/thunderfist_set_physics.s")
#endif

/* thunderfist_save_state: two-address trampoline into veh_serialize_state, same
 * confirmed-unreachable-from-C 8-byte sd-$ra frame as veh_check_wrap
 * (hd_code/679E0.c). Permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
extern u8 gThunderfistState[];  /* this vehicle's state block: 0xA6 bytes are (de)serialized */
extern u32 gThunderfistPos[]; /* plus these three words */

/* Thunderfist's state into a buffer (no callers) (0x802B295C).
 * Serialize this vehicle's state (gThunderfistState[0..0xA5] plus the three words
 * gThunderfistPos[0..2]) into `dst` via veh_serialize_state. Returns the callee's result
 * (0xB2, the byte count): the asm leaves the callee's v0 untouched. */
s32 thunderfist_save_state(u8 *dst) {
    return veh_serialize_state(dst, gThunderfistState, gThunderfistPos);
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/6C5E0/thunderfist_save_state.s")
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
/* Thunderfist's state from a buffer (recording start) (0x802B2988).
 * Inverse of thunderfist_save_state: restore this vehicle's state from `src` via
 * veh_deserialize_state. Called from 17210.c as `void thunderfist_load_state(void *)`. */
void thunderfist_load_state(void *src) {
    veh_deserialize_state(src, gThunderfistState, gThunderfistPos);
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/6C5E0/thunderfist_load_state.s")
#endif
