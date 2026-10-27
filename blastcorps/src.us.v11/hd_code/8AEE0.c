#include "common.h"
#include <ultra64.h>
#include "game/game.h"

#ifdef NON_MATCHING
/* Shared declarations for the vehicle type 15 (state block gStarskiState) and
 * type 16 (gCycloneState) rewrites below. The structs mirror the register
 * blocks of the callees' C rewrites (56040.c, 62740.c). */


extern u8 gStarskiState[]; /* vehicle 15 state block */
extern u8 gCycloneState[]; /* vehicle 16 state block */
extern u32 gStarskiPos[]; /* vehicle 15 x, y, z */
extern u32 gCyclonePos[]; /* vehicle 16 x, y, z */
extern u8 * N64P gStarskiModel;   /* vehicle 15 model */
extern u8 * N64P gCycloneModel;   /* vehicle 16 model */
extern u64 * N64P gStarskiMtxA;
extern u64 * N64P gStarskiMtxB;
extern u64 * N64P gCycloneMtxA;
extern u64 * N64P gCycloneMtxB;
extern u8 gStarskiAnim[];  /* vehicle 15 animation channel table */
extern u8 gCycloneAnim[];  /* vehicle 16 animation channel table */
extern u8 gStarskiDustTimer;
extern s8 gStarskiEscaping;
extern u8 gStarskiThrottleHold;
extern s16 gStarskiEscapeHeading;
extern s16 gCycloneEscapeHeading;
extern s8 gCycloneEscaping;
extern u8 D_803FC999;
extern u8 gCycloneNearArcEnd;
extern f32 gStarskiEscapeTurnRate;
extern u8 gStarskiNonBlockingParts[];
extern u8 gCycloneNonBlockingParts[];


s32 starski_zone_alpha(ZoneScanRegs *r);
void starski_update_effects(s32 t6, s32 t7, s32 s0, s32 s1, s32 s2, s32 s3, s32 s4);
s32 starski_steer_rate(void);
void starski_set_physics(void);
s32 cyclone_zone_alpha(ZoneScanRegs *r);
void cyclone_update_moves(void);
s32 cyclone_steer_rate(void);
void cyclone_set_physics(void);
s32 starski_build_model(s32 a3, s32 s0, s32 s1, VehMtxOut *out);
s32 cyclone_build_model(s32 a3, s32 s0, s32 s1, VehMtxOut *out);

/* Ring distance used by both per-frame functions: the target heading turned
 * half a circle (wrapping by 0xFFF), minus the ring midpoint, as a distance
 * mod 0xFFF folded at 0x800. */
#define PORT_RING_DIST(d, heading, mid) \
    do {                                \
        (d) = (heading) - 0x800;        \
        if ((d) < 0) {                  \
            (d) += 0xFFF;               \
        }                               \
        (d) -= (mid);                   \
        if ((d) < 0) {                  \
            (d) = -(d);                 \
        }                               \
        if ((d) > 0x800) {              \
            (d) = 0xFFF - (d);          \
        }                               \
    } while (0)
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
/* Sets up vehicle type 15 (starski) (0x802CF6A0).
 * Spawn vehicle type 15 (called from the level-object dispatcher
 * lvl_spawn_vehicles with a spawn record decoded into registers): model `model`
 * (gStarskiModel), two 0x100-byte matrix buffers taken from gHeapPtr (into
 * gStarskiMtxA/B), veh_add_record(0xF, 0, ...), the record reset
 * (veh_reset_state), the wheel offsets at +0x52..+0x68 and speed bands at
 * +0x78..+0x94, position (x, y, z) and heading, the ground slots
 * (veh_init_ground), the model's animation channels (anim_init_model, channel 0
 * set up and run twice through anim_update_model), the object parts
 * (veh_register_parts, scale 0x32C8) and veh_add_shadow; then one frame of
 * starski_update with +0x9A set, and the matrix copy mtx_copy_at.
 * Register convention (conventions.txt): model s2, x t7, y s3, z s0, heading
 * s1, fp; t6 and veh_init_ground's FP state (f) only pass through to
 * starski_update. The asm restores t0-t5 (asm caller lvl_spawn_vehicles keeps t1
 * and t2 live) and leaves s0-s7, fp, gp and f20-f30 changed; the survey's
 * s2-s4, fp, f12-f26 "outputs" are rewritten by lvl_spawn_vehicles before use.
 * Leaked registers (FIDELITY): starski_update's zone registers here are the
 * stub-model values - t6 the caller's, t7 anim_init_model's result (the asm
 * expects the C veh_add_shadow to keep t6/t7), s0 = z and s1 = +4, which in
 * the game anim_init_model (s0 = s1 = 0) and anim_update_model have overwritten;
 * likewise f14-f26 after anim_update_model (f20). */
void starski_spawn(u8 *model, s32 x, s32 y, s32 z, s32 heading, s32 fpIn, s32 t6, TriSideOut *f) {
    u8 *v = gStarskiState;
    u8 *buf;
    u64 *b0;
    u64 *b1;
    s32 *s3v;
    s32 t7;

    gStarskiModel = model;
    buf = gHeapPtr;
    gStarskiMtxA = (u64 *) buf;
    gStarskiMtxB = (u64 *) (buf + 0x100);
    gHeapPtr = buf + 0x200;
    veh_add_record(VEH_STARSKI, 0, (s32) gStarskiMtxA, (s32) gStarskiMtxB, model);
    veh_reset_state(v);
    *(s16 *) (v + 0x52) = 0xAF;
    *(s16 *) (v + 0x54) = 0xFA;
    *(s16 *) (v + 0x56) = -0xAF;
    *(s16 *) (v + 0x58) = 0xFA;
    *(s16 *) (v + 0x5A) = 0xAF;
    *(s16 *) (v + 0x5C) = -0xFA;
    *(s16 *) (v + 0x5E) = 0x140;
    *(s16 *) (v + 0x60) = 0x1F4;
    *(s16 *) (v + 0x62) = -0x140;
    *(s16 *) (v + 0x64) = 0x1F4;
    *(s16 *) (v + 0x66) = 0x140;
    *(s16 *) (v + 0x68) = -0x1F4;
    gStarskiPos[0] = x;
    gStarskiPos[1] = y;
    gStarskiPos[2] = z;
    *(s16 *) (v + 0x4C) = heading;
    *(s16 *) (v + 0x4E) = heading;
    *(s16 *) (v + 0x74) = heading;
    s3v = veh_init_ground((s16 *) (v + 0x52), gStarskiPos[1], x, z, (s32 *) (v + 4), (s32 *) &gStarskiPos[1],
                        (s16 *) (v + 0x4C), 0xF, fpIn, v, f);
    b0 = gStarskiMtxA;
    b1 = gStarskiMtxB;
    t7 = anim_init_model((u32 *) b1, (u32 *) b0, (AnimChannel *) gStarskiAnim, gStarskiModel);
    anim_set_speed((AnimChannel *) gStarskiAnim, 0, 0x64);
    anim_set_reverse((AnimChannel *) gStarskiAnim, 0, 0);
    anim_set_loop_mode((AnimChannel *) gStarskiAnim, 0, 0);
    anim_set_interp(0.0f, (AnimChannel *) gStarskiAnim, 0, 0);
    anim_play((AnimChannel *) gStarskiAnim, 0, 1);
    anim_update_model((u8 *) b0, (u8 *) b1, (AnimChannel *) gStarskiAnim);
    anim_rewind(0, (AnimChannel *) gStarskiAnim);
    anim_play((AnimChannel *) gStarskiAnim, 0, 1);
    anim_update_model((u8 *) b1, (u8 *) b0, (AnimChannel *) gStarskiAnim);
    *(s16 *) (v + 0x78) = -0xB4;
    *(s16 *) (v + 0x7A) = 0;
    *(s16 *) (v + 0x7C) = 2;
    *(s16 *) (v + 0x7E) = 0;
    *(s16 *) (v + 0x80) = 0x118;
    *(s16 *) (v + 0x82) = 4;
    *(s16 *) (v + 0x84) = 0x118;
    *(s16 *) (v + 0x86) = 0x12C;
    *(s16 *) (v + 0x88) = 5;
    *(s16 *) (v + 0x8A) = 0x12C;
    *(s16 *) (v + 0x8C) = 0x136;
    *(s16 *) (v + 0x8E) = 3;
    *(s16 *) (v + 0x90) = 0x136;
    *(s16 *) (v + 0x92) = 0x140;
    *(s16 *) (v + 0x94) = 2;
    gStarskiDustTimer = 0;
    gStarskiEscaping = 0;
    gStarskiThrottleHold = 0;
    model = gStarskiModel;
    veh_register_parts(0xF, model + *(s32 *) (model + 4), model + *(s32 *) (model + 8), 0x32C8);
    veh_add_shadow(VEH_STARSKI, 0x3C, 0x19, 0x19);
    v[0x9A] = 1;
    starski_update(t6, t7, z, (s32) (v + 4), (s32) &gStarskiPos[1], (s32) s3v, (s32) (v + 0x4C), f->cross, f->cz,
                  f->side, f->sideZ, f->dz);
    v[0x9A] = 0;
    model = gStarskiModel;
    mtx_copy_at((u8 *) gStarskiMtxB, (u8 *) gStarskiMtxA, *(s32 *) (model + *(s32 *) (model + 0x18) + 4));
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/8AEE0/starski_spawn.s")
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
extern u8 gStarskiState[]; /* this vehicle's state block */

/* Getting into type 15 (0x802CFA0C).
 * Enter vehicle type 15: clears the byte at +0x99, gCamDistance / gCamHeightBase = 3000,
 * 1000, then veh_start_engine_sound(arg0, 0xCE) (arg0 passes straight through; hd.c
 * calls this with no arguments and veh_start_engine_sound ignores it). The asm also
 * points $gp at gStarskiState and leaves it there (conventions.txt: clobbers
 * gp); C code doesn't use $gp. Same shape as amdream_enter (72B80). */
void starski_enter(s32 arg0) {
    gStarskiState[0x99] = 0;
    gCamDistance = 3000;
    gCamHeightBase = 1000;
    veh_start_engine_sound(arg0, 0xCE);
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/8AEE0/starski_enter.s")
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
/* Functional rewrite (tools_port/eqcheck.py verified). Vehicle-type 15
 * "can exit" check: 0 if any of the vehicle's bytes 0x96..0x98 equals 1,
 * else 1 (shape shared with ballista_exit_check in 853D0.c). 00000.c declares it
 * void and ignores the result, but the asm returns 0/1 in v0. */
extern u8 gStarskiState[];

/* 1 unless a wheel slot is airborne (0x802CFA58). */
s32 starski_exit_check(void) {
    if (gStarskiState[0x96] == 1 || gStarskiState[0x97] == 1 || gStarskiState[0x98] == 1) {
        return 0;
    }
    return 1;
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/8AEE0/starski_exit_check.s")
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
extern u8 gStarskiState[];
extern u64 * N64P gStarskiMtxA;
extern u64 * N64P gStarskiMtxB;

/* Getting out of type 15 (0x802CFAB4).
 * Vehicle-type 15 exit: zero the speed (s16 at +0x76), copy 0x100 bytes
 * between the two buffers gStarskiMtxA/gStarskiMtxB point at (veh_sync_buffers),
 * then veh_stop_engine_sounds. Same shape as amdream_leave (72B80). */
void starski_leave(void) {
    *(s16 *) (gStarskiState + 0x76) = 0;
    veh_sync_buffers(gStarskiMtxA, gStarskiMtxB, 0x100);
    veh_stop_engine_sounds();
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/8AEE0/starski_leave.s")
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
/* Placed type 15 on a moving level part: re-ground it (0x802CFB00).
 * Re-ground vehicle type 15 (called from 00000.c's veh_update_others): the
 * ground slots (veh_reground, key 0xF) at its position, the model matrices
 * (starski_build_model), then the gVehicles record (veh_set_record).
 * Register convention: fp and a3 (passed through to veh_reground and
 * starski_build_model) and the FP pass-through state f12-f26; the C caller declares
 * it `void (void)`, so in the game these are whatever that C code left (the
 * port's caller passes garbage too; update 00000.c's prototype in the port
 * headers). The survey's v1 output (left = y by veh_set_record) isn't used by
 * the C caller. */
void starski_reground(s32 fpIn, s32 a3, f32 f12, f32 f14, f32 f20, f32 f22, f32 f24, f32 f26) {
    u8 *v = gStarskiState;
    TriSideOut f;
    Out802A9A60 o9;
    VehMtxOut mo;
    s32 x = gStarskiPos[0];
    s32 z = gStarskiPos[2];

    f.pz = f12;
    f.cross = f14;
    f.cz = f20;
    f.side = f22;
    f.sideZ = f24;
    f.dz = f26;
    veh_reground((s16 *) (v + 0x52), gStarskiPos[1], x, z, (s32 *) (v + 4), (s32 *) &gStarskiPos[1],
                  (s16 *) (v + 0x4C), 0xF, fpIn, v, &f, &o9);
    starski_build_model(a3, z, (s32) o9.s1, &mo);
    veh_set_record(gStarskiPos[2], 0xF, gStarskiPos[0], gStarskiPos[1], v);
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/8AEE0/starski_reground.s")
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
extern u32 gStarskiPos[]; /* x, y, z */
/* Veh_zone_alpha at its position (0x802CFC10).
 * Zone level lookup (veh_zone_alpha) for vehicle id 0xF at its position
 * gStarskiPos..+8; returns veh_zone_alpha's v1 (the zone list end).
 * Register convention: the asm passes veh_zone_alpha's scan registers t6, t7,
 * s0-s4 through (in and out; here through r), leaves a3 = id and clobbers
 * s5, s6 (conventions.txt). Asm caller starski_update keeps f12 and f14 live
 * (a mixed N64 build would need a thunk; the native port won't). */
s32 starski_zone_alpha(ZoneScanRegs *r) {
    return veh_zone_alpha(0xF, gStarskiPos[0], gStarskiPos[1], gStarskiPos[2], r);
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/8AEE0/starski_zone_alpha.s")
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
/* Records its position on the deck it stands on (0x802CFC54).
 * Vehicle type 15 on a moving platform (asm caller veh_attach_riders_walk): the
 * value pair at its position on triangle `id` (veh_deck_to_local) goes to the
 * halves +0x6A/+0x6C and the heading to +0x6E; then the pair at the position
 * plus wheel 0's offset rotated by the heading (veh_wheel_offset) to +0x70/+0x72.
 * Returns the asm's v1, &gStarskiState[0x52] (veh_deck_to_local keeps it).
 * Register convention (conventions.txt): id a3, f24 in through r (chained
 * between the calls), outputs v1 and r's f24/f26; the asm restores t0, t1,
 * t3, t4 (asm caller keeps t0, t1 live) and leaves gp = gStarskiState, s4 and
 * f20-f28 changed. */
s32 starski_ride_attach(s32 id, InterpRegs *r) {
    u8 *v = gStarskiState;
    s32 x = gStarskiPos[0];
    s32 z = gStarskiPos[2];
    s32 dx;
    s32 dz;

    veh_deck_to_local(id, x, z, r);
    *(s16 *) (v + 0x6A) = r->t3;
    *(s16 *) (v + 0x6C) = r->t4;
    *(s16 *) (v + 0x6E) = *(u16 *) (v + 0x4C);
    dx = veh_wheel_offset(0, (s16 *) (v + 0x52), (s16 *) (v + 0x4C), &dz);
    veh_deck_to_local(id, x + dx, z + dz, r);
    *(s16 *) (v + 0x70) = r->t3;
    *(s16 *) (v + 0x72) = r->t4;
    return (s32) (v + 0x52);
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/8AEE0/starski_ride_attach.s")
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
/* Moves it with the deck under it (0x802CFCE0).
 * Vehicle type 15 carried by a moving platform (asm caller veh_move_riders_walk):
 * new heading from the platform triangle `id` (veh_follow_heading on the pairs
 * starski_ride_attach stored) into +0x4E and +0x4C, the new position pair
 * (veh_deck_to_world at +0x6A/+0x6C), starski_set_physics, then ground contact
 * (veh_ground_contact with gBounceSound = 1, divisors 0x1F4/0x15E) at that point,
 * the model matrices (starski_build_model) and the gVehicles record.
 * Returns veh_ground_contact's s3.
 * Register convention (conventions.txt): id a3, f24 in through r; outputs
 * s3 and f24/f26 (veh_ground_contact's, through r). The asm restores a3, t0-t2
 * (asm caller keeps a3, t0, t1 live) and leaves gp = gStarskiState, s0-s2,
 * s4-s7, fp, f20, f22, f28 changed. veh_deck_to_world's f12-f26 feed
 * veh_ground_contact's FP state (f14 as raw bits). starski_build_model gets a3 = +0x34,
 * s0 = +0x96, s1 = &z, as the asm leaves them for it. */
s32 starski_ride_follow(s32 id, InterpRegs *r) {
    u8 *v = gStarskiState;
    TriSideOut f;
    Regs802A8768 r8;
    VehMtxOut mo;
    s32 s3;
    s32 h;

    h = veh_follow_heading((s16 *) (v + 0x52), *(s16 *) (v + 0x6A), (u16 *) (v + 0x6E), id, *(s16 *) (v + 0x6C),
                      *(s16 *) (v + 0x70), *(s16 *) (v + 0x72), &s3, r);
    *(s16 *) (v + 0x4E) = h;
    *(s16 *) (v + 0x4C) = h;
    veh_deck_to_world(id, *(s16 *) (v + 0x6A), *(s16 *) (v + 0x6C), r);
    starski_set_physics();
    gBounceSound = 1;
    f.pz = r->f12;
    *(s32 *) &f.cross = r->f14;
    f.cz = r->f20;
    f.side = r->f22;
    f.sideZ = r->f24;
    f.dz = r->f26;
    r8.s3 = s3;
    veh_ground_contact(v, 0xF, (s32 *) &gStarskiPos[0], (s32 *) &gStarskiPos[1], (s32 *) &gStarskiPos[2], r->t3, r->t4,
                  0x1F4, 0x15E, (s16 *) (v + 0x4C), v + 0x96, (s16 *) (v + 0x52), (s32 *) (v + 0x28),
                  (s32 *) (v + 0x40), (s32 *) (v + 0x34), (s32 *) (v + 4), &r8, &f);
    starski_build_model((s32) (v + 0x34), (s32) (v + 0x96), (s32) &gStarskiPos[2], &mo);
    veh_set_record(gStarskiPos[2], 0xF, gStarskiPos[0], gStarskiPos[1], v);
    r->f24 = f.sideZ;
    r->f26 = f.dz;
    return r8.s3;
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/8AEE0/starski_ride_follow.s")
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
/* Type 15 driven by the player, per frame (0x802CFDE8).
 * Per-frame update of vehicle type 15 (called from 00000.c's veh_update_player
 * and once from starski_spawn): zone level (starski_zone_alpha), state save
 * (veh_snapshot), sound 0x76, effects (starski_update_effects unless +0x9A), the
 * gTurboPending hook (veh_start_turbo), timers (starski_set_physics), steering
 * (veh_steer with the rate from starski_steer_rate), throttle (veh_throttle,
 * skipped while the gStarskiThrottleHold countdown runs), heading, slope and speed
 * (veh_turn_heading / 83B8 / 843C, veh_set_target_heading while gStarskiEscaping), the move
 * (veh_move_along_heading), ground contact (veh_ground_contact, gBounceSound = 1), animation
 * (anim_update_model on the current matrix buffer), model matrices
 * (starski_build_model), the vehicle-state reset and collision passes
 * (veh_collision_setup, veh_collide_spheres, veh_collide_world). Then: on a ring hit
 * (gCollBlocked) steer along the ring (veh_prepare_escape, veh_reverse_if_behind,
 * veh_turn_into_free_arc with scale gStarskiEscapeTurnRate, veh_step_heading, veh_hit_slowdown) and
 * collide (obj_collide_vehicle); else collide and, if that hit something
 * (gCollHit), restore the saved state (veh_rollback), start a 5-frame
 * countdown, bounce back at half of at least 50 the other way and redo the
 * matrices. Finally the position/speed/headings go to gPlayerX.. and the
 * gVehicles record (veh_set_record).
 * Register convention (conventions.txt): the zone registers t6, t7, s0-s4
 * (starski_zone_alpha in/out, then starski_update_effects) and the FP pass-through state
 * f14-f26 (veh_ground_contact; f12 is veh_slope's result by then). The C
 * caller declares it `void (void)` (garbage in the game as in the port;
 * update 00000.c's prototype in the port headers). The survey's outputs (v1
 * = y; f12/f14, sine-routine leftovers that starski_spawn doesn't use) are
 * not modelled.
 * Leaked registers (FIDELITY): starski_build_model's a3/s0/s1 (only read by
 * mtx_chain_transform for a matrix count of 0) are the stub-model values: a3 =
 * +0x34 (second call: veh_rollback's dst2End), s0 = +0x96, s1 = &z (second
 * call: the first call's s1); in the game anim_update_model, veh_collide_spheres and
 * obj_collide_vehicle have changed s0/s1 by then. The v1 handed to veh_step_heading is
 * the ring distance computed after veh_free_arc_mid (the asm's dead-looking
 * code feeds it). */
void starski_update(s32 t6, s32 t7, s32 s0, s32 s1, s32 s2, s32 s3, s32 s4, f32 f14, f32 f20, f32 f22, f32 f24,
                   f32 f26) {
    u8 *v = gStarskiState;
    ZoneScanRegs zr;
    TriSideOut f;
    Regs802A8768 r8;
    Out802A860C o;
    VehMtxOut mo;
    u64 *end;
    u64 *src;
    u64 *dst2;
    f32 f12;
    s32 x;
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
    starski_zone_alpha(&zr);
    veh_snapshot(v, gStarskiAnim, (s32 *) &gStarskiPos[0], (s32 *) &gStarskiPos[1], (s32 *) &gStarskiPos[2]);
    veh_update_horn(0x76);
    if (v[0x9A] == 0) {
        starski_update_effects(zr.t6, zr.t7, zr.s0, zr.s1, zr.s2, zr.s3, zr.s4);
    }
    if (gTurboPending != 0) {
        veh_start_turbo(v);
    }
    starski_set_physics();
    veh_steer(starski_steer_rate(), (u16 *) (v + 0x4C));
    if (gStarskiThrottleHold == 0) {
        veh_throttle(v, (s16 *) (v + 0x76), 3, v + 0x96, (s16 *) (v + 0x78), 0x10);
    } else {
        gStarskiThrottleHold--;
    }
    veh_turn_heading(v, (u16 *) (v + 0x74), 0x1F40, (s16 *) (v + 0x76), (u16 *) (v + 0x4C), (u16 *) (v + 0x4E),
                  (s8 *) (v + 0x99), 1);
    f12 = veh_slope((s16 *) (v + 0x76), v + 0x96, (s32 *) (v + 4), (f32 *) v);
    veh_apply_slope(v, (s16 *) (v + 0x76), 0xF, (s8 *) (v + 0x96), (s32 *) (v + 4), 1, 500.0f);
    if (gStarskiEscaping != 0) {
        veh_set_target_heading(v, &gStarskiEscapeHeading);
    }
    x = veh_move_along_heading(f12, *(u16 *) (v + 0x4E), (s16 *) (v + 0x76), (s32 *) &gStarskiPos[0],
                      (s32 *) &gStarskiPos[2], &o);
    gBounceSound = 1;
    r8.s3 = o.s3;
    f.pz = f12;
    f.cross = f14;
    f.cz = f20;
    f.side = f22;
    f.sideZ = f24;
    f.dz = f26;
    veh_ground_contact(v, 0xF, (s32 *) &gStarskiPos[0], (s32 *) &gStarskiPos[1], (s32 *) &gStarskiPos[2], x, o.t1, 0x1F4,
                  0x15E, (s16 *) (v + 0x4C), v + 0x96, (s16 *) (v + 0x52), (s32 *) (v + 0x28), (s32 *) (v + 0x40),
                  (s32 *) (v + 0x34), (s32 *) (v + 4), &r8, &f);
    if (gBufferIndex != 0) {
        anim_update_model((u8 *) gStarskiMtxA, (u8 *) gStarskiMtxB, (AnimChannel *) gStarskiAnim);
    } else {
        anim_update_model((u8 *) gStarskiMtxB, (u8 *) gStarskiMtxA, (AnimChannel *) gStarskiAnim);
    }
    starski_build_model((s32) (v + 0x34), (s32) (v + 0x96), (s32) &gStarskiPos[2], &mo);
    veh_collision_setup(gStarskiPos[2], (s32) gStarskiNonBlockingParts, 1, 1, gStarskiPos[0], gStarskiPos[1], 7, *(s16 *) (v + 0x76),
                  0x96, 0, 0xF, v);
    veh_collide_spheres(0xF, v);
    veh_collide_world(0xF);
    if (gCollBlocked == 0) {
        gCollHit = 0;
        gCollVehicleAnim = gStarskiAnim;
        obj_collide_vehicle(0xF, v);
        if (gCollHit != 0) {
            gStarskiEscaping = 0;
            if (gBufferIndex != 0) {
                src = gStarskiMtxB;
                dst2 = gStarskiMtxA;
            } else {
                src = gStarskiMtxA;
                dst2 = gStarskiMtxB;
            }
            veh_rollback(v, gStarskiAnim, (s32 *) &gStarskiPos[0], (s32 *) &gStarskiPos[1], (s32 *) &gStarskiPos[2],
                          src, dst2, 0x100, &end);
            gStarskiThrottleHold = 5;
            d = *(s16 *) (v + 0x76);
            if (d >= 0) {
                if (d < 0x32) {
                    d = 0x32;
                }
            } else if (d >= -0x31) {
                d = -0x32;
            }
            d = -d;
            *(s16 *) (v + 0x76) = d >> 1;
            starski_build_model((s32) end, (s32) (v + 0x96), mo.s1, &mo);
        } else {
            gStarskiEscaping = 0;
        }
    } else {
        veh_prepare_escape(v);
        gStarskiEscaping = 1;
        turn = veh_free_arc_mid();
        PORT_RING_DIST(d, *(u16 *) (v + 0x4E), turn);
        veh_reverse_if_behind(v);
        turn = veh_turn_into_free_arc(v, *(u16 *) (v + 0x4E), *(u16 *) (v + 0x4C), &cur, gStarskiEscapeTurnRate);
        gStarskiEscapeHeading = cur;
        *(s16 *) (v + 0x4E) = cur;
        *(s16 *) (v + 0x74) = cur;
        veh_step_heading(v, turn, d, &tgt);
        veh_hit_slowdown(v, 0);
        gCollVehicleAnim = gStarskiAnim;
        obj_collide_vehicle(0xF, v);
    }
    gPlayerX = gStarskiPos[0];
    gPlayerY = gStarskiPos[1];
    gPlayerZ = gStarskiPos[2];
    gPlayerSpeed = *(s16 *) (v + 0x76);
    gPlayerMoveHeading = *(u16 *) (v + 0x4E);
    gPlayerHeading = *(u16 *) (v + 0x4C);
    veh_set_record(gStarskiPos[2], 0xF, gStarskiPos[0], gStarskiPos[1], v);
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/8AEE0/starski_update.s")
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
extern u8 gStarskiDustTimer;   /* effect cooldown */
void starski_add_tyre_trail(void);

/* Tyre tracks, dust, engine level (0x802D02F8).
 * Per-frame effects for vehicle type 15 ($gp = gStarskiState, read as the
 * global): the tyre trail (starski_add_tyre_trail); then, if the cooldown
 * gStarskiDustTimer is nonzero it just counts down, else when byte +0x99 is set the
 * cooldown becomes 1 and, with fewer than 15 active fx_spawn records,
 * four are set up (def gFxExhaustSmoke, type 1 at (15, 1..4, 1), data 0x29810
 * for the first two and 0x1D4C0 for the last two). Finally
 * veh_set_engine_level(|speed| >> 5) with speed the s16 at +0x76.
 * Register convention (conventions.txt): t6, t7, s0-s4 pass through to
 * fx_spawn (t6 and s1 in its in/out block, chained between the calls);
 * the asm changes s1 and s5-s7. Same shape as amdream_update_effects (72B80). */
void starski_update_effects(s32 t6, s32 t7, s32 s0, s32 s1, s32 s2, s32 s3, s32 s4) {
    Io802A6274 io;
    s32 v;

    starski_add_tyre_trail();
    if (gStarskiDustTimer != 0) {
        gStarskiDustTimer--;
    } else if (gStarskiState[0x99] != 0) {
        gStarskiDustTimer = 1;
        if (fx_count_active() < 15) {
            io.t6 = t6;
            io.s1 = s1;
            io.a3 = 1;
            fx_spawn(&io, gFxExhaustSmoke, 0x29810, 1, 15, 1, 1, t7, s0, s2, s3, s4, 1);
            io.a3 = 1;
            fx_spawn(&io, gFxExhaustSmoke, 0x29810, 1, 15, 2, 1, t7, s0, s2, s3, s4, 1);
            io.a3 = 1;
            fx_spawn(&io, gFxExhaustSmoke, 0x1D4C0, 1, 15, 3, 1, t7, s0, s2, s3, s4, 1);
            io.a3 = 1;
            fx_spawn(&io, gFxExhaustSmoke, 0x1D4C0, 1, 15, 4, 1, t7, s0, s2, s3, s4, 1);
        }
    }
    v = *(s16 *) (gStarskiState + 0x76);
    if (v < 0) {
        v = -v;
    }
    veh_set_engine_level((u32) v >> 5);
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/8AEE0/starski_update_effects.s")
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
extern u32 gStarskiPos[]; /* [0], [2]: trail x, z */

/* Tyre tracks while skidding (0x802D0438).
 * Tyre trail (shape of skyfall_add_tyre_trail in 6E200; $gp = gStarskiState): if the
 * byte at +0x99 is set, +0x98 isn't 1, +0x50 < 3 and +0x9B is clear,
 * trail_add(3, y, 250, -400, -400, -400, gStarskiPos[0], gStarskiPos[2],
 * yaw, 3, 50, 50, 0).
 * Register note: the asm saves and restores every integer register; asm
 * caller starski_update_effects keeps a0-a3, t6, t7 live (and reads f12/f14 after
 * the call, which the asm doesn't touch but trail_add may); a mixed N64
 * build would need a thunk. */
void starski_add_tyre_trail(void) {
    if (gStarskiState[0x99] != 0 && gStarskiState[0x98] != 1 && gStarskiState[0x50] < 3 && gStarskiState[0x9B] == 0) {
        trail_add(3, *(s32 *) (gStarskiState + 0x1C), 250, -400, -400, -400, gStarskiPos[0], gStarskiPos[2],
                      *(u16 *) (gStarskiState + 0x4E), 3, 50, 50, 0);
    }
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/8AEE0/starski_add_tyre_trail.s")
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
/* Its model matrix, parts and points (0x802D05D8).
 * Model matrices of vehicle type 15: the root matrix (mtx_build_model at its
 * position, scale 0x32C8, rotation y = heading +0x4C only - gModelRot[0]/[2]
 * are left as they are) into the current matrix buffer (gBufferIndex picks
 * gStarskiMtxA or gStarskiMtxB) at the offset the model's +0x18 table gives;
 * then the model's part list (+4..+8, veh_place_parts, tag 0xF) and point list
 * (+0..+4, veh_points_transform) through the matrix chain.
 * Register convention (conventions.txt): a3, s0, s1 pass into the matrix
 * chain registers (mtx_chain_transform reads them only for a count of 0), s2 is the
 * matrix mtx_build_model leaves there; outputs t0 = 0xF (the return value),
 * t2 = end of the point list, s1 = the chain's y', s4 = the buffer. The asm
 * also leaves f12/f14 as the sine routines did (not modelled) and clobbers
 * s2, s5-s7; asm callers keep t6/t7 live (a mixed N64 build would need a
 * thunk). */
s32 starski_build_model(s32 a3, s32 s0, s32 s1, VehMtxOut *out) {
    u8 *model = gStarskiModel;
    u8 *base;
    s32 *m;
    MtxChainRegs regs;
    s32 x;
    s32 y;
    s32 z;

    m = (s32 *) N64_IPTR(*(s32 *) (model + *(s32 *) (model + 0x18) + 4) +
                 (s32) (gBufferIndex != 0 ? gStarskiMtxA : gStarskiMtxB));
    gModelRot[1] = *(u16 *) (gStarskiState + 0x4C);
    mtx_build_model(gStarskiPos[0], gStarskiPos[1], gStarskiPos[2], 0x32C8, m);
    base = (u8 *) (gBufferIndex != 0 ? gStarskiMtxA : gStarskiMtxB);
    model = gStarskiModel;
    x = gStarskiPos[0];
    y = gStarskiPos[1];
    z = gStarskiPos[2];
    regs.a3 = a3;
    regs.s1 = s1;
    regs.s2 = (s32) m;
    regs.s0 = s0;
    veh_place_parts(x, y, z, 0xF, model + *(s32 *) (model + 4), model + *(s32 *) (model + 8), base, &regs);
    model = gStarskiModel;
    regs.v1 = y;
    regs.a0 = z;
    veh_points_transform(0xF, (s16 *) (model + *(s32 *) model), (s16 *) (model + *(s32 *) (model + 4)), base, &regs);
    out->t2 = (s32) (model + *(s32 *) (model + 4));
    out->s1 = regs.s1;
    out->s4 = (s32) base;
    return 0xF;
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/8AEE0/starski_build_model.s")
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

extern f32 gStarskiSteerDivisor;

/* Its steering rate (0x802D0710).
 * Speed (s16 at +0x76 of gStarskiState, the asm's $gp) / 11.0 when any of the
 * bytes at +0x96/+0x97/+0x98 is 1, else / gStarskiSteerDivisor, rounded to nearest.
 * The asm returns it in s3 (see tools_port/conventions.txt). Its asm caller
 * starski_update keeps a0-a3 live (a mixed N64 build would need a thunk).
 * Same shape as sideswipe_steer_rate (6B4A0). */
s32 starski_steer_rate(void) {
    f32 div;
    s32 r;

    if (gStarskiState[0x96] == 1 || gStarskiState[0x97] == 1 || gStarskiState[0x98] == 1) {
        div = 11.0f;
    } else {
        div = gStarskiSteerDivisor;
    }
    CVT_W_S(r, *(s16 *) (gStarskiState + 0x76) / div);
    return r;
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/8AEE0/starski_steer_rate.s")
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
/* Functional rewrite (tools_port/eqcheck.py verified). Same "set timers"
 * shape as carrier_set_physics (75490.c). Asm callers rely on preserved registers:
 * starski_ride_follow on t3, t4, f12, f14; starski_update on a0-a3 (mixed N64 build
 * would need a thunk). */

/* Its gravity and bounce (0x802D0784). */
void starski_set_physics(void) {
    gGravity = gLevelGravity * 4.0f;
    gBounceThreshold = 60;
    gBounceDamping = 3;
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/8AEE0/starski_set_physics.s")
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
/* Sets up the Cyclone Suit (type 16) (0x802D07E0).
 * Spawn vehicle type 16 (from the dispatcher lvl_spawn_vehicles), the shape of
 * starski_spawn: model gCycloneModel, 0x1400-byte matrix buffers gCycloneMtxA/B,
 * veh_add_record(0x10, 1, ...), wheel offsets and speed bands, state bytes
 * +0xA1/A2 = 0 and +0xA3 = 1, the ground slots, the animation channels of
 * gCycloneAnim (channel 0, then channel 1 also started), the parts (scale
 * 0x2134), veh_add_shadow; one frame of cyclone_update with +0x9A set; then
 * veh_sync_buffers copies buffer 98C to 988, the matrix copy mtx_copy_at, and
 * gEngineSound (the special-move sound handle) is cleared.
 * Register convention and leaked registers: as starski_spawn (zone registers
 * and f14-f26 passed to cyclone_update as the stub model leaves them). */
void cyclone_spawn(u8 *model, s32 x, s32 y, s32 z, s32 heading, s32 fpIn, s32 t6, TriSideOut *f) {
    u8 *v = gCycloneState;
    u8 *buf;
    u64 *b0;
    u64 *b1;
    s32 *s3v;
    s32 t7;

    gCycloneModel = model;
    buf = gHeapPtr;
    gCycloneMtxA = (u64 *) buf;
    gCycloneMtxB = (u64 *) (buf + 0x1400);
    gHeapPtr = buf + 0x2800;
    veh_add_record(VEH_CYCLONE, 1, (s32) gCycloneMtxA, (s32) gCycloneMtxB, model);
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
    gCyclonePos[0] = x;
    gCyclonePos[1] = y;
    gCyclonePos[2] = z;
    *(s16 *) (v + 0x4C) = heading;
    v[0xA1] = 0;
    v[0xA2] = 0;
    *(s16 *) (v + 0x4E) = heading;
    *(s16 *) (v + 0x74) = heading;
    v[0xA3] = 1;
    s3v = veh_init_ground((s16 *) (v + 0x52), gCyclonePos[1], x, z, (s32 *) (v + 4), (s32 *) &gCyclonePos[1],
                        (s16 *) (v + 0x4C), 0x10, fpIn, v, f);
    b0 = gCycloneMtxA;
    b1 = gCycloneMtxB;
    t7 = anim_init_model((u32 *) b1, (u32 *) b0, (AnimChannel *) gCycloneAnim, gCycloneModel);
    anim_set_speed((AnimChannel *) gCycloneAnim, 0, 0x64);
    anim_set_reverse((AnimChannel *) gCycloneAnim, 0, 0);
    anim_set_loop_mode((AnimChannel *) gCycloneAnim, 0, 0);
    anim_set_interp(0.0f, (AnimChannel *) gCycloneAnim, 0, 0);
    anim_play((AnimChannel *) gCycloneAnim, 0, 1);
    anim_update_model((u8 *) b0, (u8 *) b1, (AnimChannel *) gCycloneAnim);
    anim_rewind(0, (AnimChannel *) gCycloneAnim);
    anim_play((AnimChannel *) gCycloneAnim, 0, 1);
    anim_update_model((u8 *) b1, (u8 *) b0, (AnimChannel *) gCycloneAnim);
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
    gCycloneEscaping = 0;
    gCycloneNearArcEnd = 0;
    D_803FC999 = 0;
    model = gCycloneModel;
    veh_register_parts(0x10, model + *(s32 *) (model + 4), model + *(s32 *) (model + 8), 0x2134);
    veh_add_shadow(VEH_CYCLONE, 0x64, 0x2D, 0x2D);
    anim_set_frame(0.0f, (AnimChannel *) gCycloneAnim, 1, 0);
    anim_play((AnimChannel *) gCycloneAnim, 1, 1);
    v[0x9A] = 1;
    cyclone_update(t6, t7, z, (s32) (v + 4), (s32) &gCyclonePos[1], (s32) s3v, (s32) (v + 0x4C), f->cross, f->cz,
                  f->side, f->sideZ, f->dz);
    v[0x9A] = 0;
    veh_sync_buffers(gCycloneMtxB, gCycloneMtxA, 0x1400);
    model = gCycloneModel;
    mtx_copy_at((u8 *) gCycloneMtxB, (u8 *) gCycloneMtxA, *(s32 *) (model + *(s32 *) (model + 0x18) + 4));
    gEngineSound = NULL;
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/8AEE0/cyclone_spawn.s")
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
/* Functional rewrite (tools_port/eqcheck.py verified). Vehicle-type 16
 * "can exit" check: 0 if any of the vehicle's bytes 0x96..0x98 equals 1 or
 * its byte 0xA1 is nonzero, else 1 (shape shared with ballista_exit_check in
 * 853D0.c). 00000.c declares it void and ignores the result, but the asm
 * returns 0/1 in v0. */
extern u8 gCycloneState[];

/* The Cyclone Suit's exit check (0x802D0B90). */
s32 cyclone_exit_check(void) {
    if (gCycloneState[0x96] == 1 || gCycloneState[0x97] == 1 || gCycloneState[0x98] == 1 || gCycloneState[0xA1] != 0) {
        return 0;
    }
    return 1;
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/8AEE0/cyclone_exit_check.s")
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
extern u8 gCycloneState[];
extern u64 * N64P gCycloneMtxA;
extern u64 * N64P gCycloneMtxB;
extern void * N64P gCycloneEngineSound; /* engine sound handle */
extern u8 gCycloneAnim[];  /* this vehicle's animation channel table */

/* Getting out of the Cyclone Suit (0x802D0BF8).
 * Vehicle-type 16 exit: zero the speed (s16 at +0x76), copy 0x1400 bytes
 * between the gCycloneMtxA/gCycloneMtxB buffers, clear animation channel 0x1F's
 * active flag (anim_stop), veh_stop_engine_sounds, then stop the engine sound
 * (snd_stop(gCycloneEngineSound)). The survey lists v1 as an output read by
 * veh_leave, but that caller is C and v1 is just snd_stop's
 * leftover. */
void cyclone_leave(void) {
    *(s16 *) (gCycloneState + 0x76) = 0;
    veh_sync_buffers(gCycloneMtxA, gCycloneMtxB, 0x1400);
    anim_stop(0x1F, (AnimChannel *) gCycloneAnim);
    veh_stop_engine_sounds();
    snd_stop(gCycloneEngineSound);
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/8AEE0/cyclone_leave.s")
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
extern u8 gCycloneState[]; /* this vehicle's state block */
extern void * N64P gCycloneEngineSound; /* engine sound handle */
extern u8 gTexAnimCyclone[]; /* key of this vehicle's anim_tex_find entry */
extern u8 gCycloneAnim[]; /* this vehicle's animation channel table */

/* Getting into the Cyclone Suit (0x802D0C68).
 * Vehicle-type 16 setup (called from 00000.c / 17210.c), the shape of
 * thunderfist_enter: clears byte 0x99 of the state block, gCamDistance / gCamHeightBase = 2000,
 * -1000, starts the engine sound 0x50 (handle in gCycloneEngineSound), sets the
 * gTexAnimCyclone entry's fields (100, 0, 0, -1) and the animation channels 7, 8,
 * 9 (and 1, 5, 6, 3) of gCycloneAnim.
 * The asm also points $gp at gCycloneState and leaves it there (conventions.txt:
 * clobbers gp) and returns with v1 = 0 (the last setter's v1); C callers
 * ignore both. */
void cyclone_enter(void) {
    gCycloneState[0x99] = 0;
    gCamDistance = 0x7D0;
    gCamHeightBase = -0x3E8;
    snd_play(gSfxBank, SFX_ENGINE_MECH, &gCycloneEngineSound);
    anim_tex_set_speed((s32) gTexAnimCyclone, 0x64);
    anim_tex_set_reverse((s32) gTexAnimCyclone, 0);
    anim_tex_set_loop_mode((s32) gTexAnimCyclone, 0);
    anim_tex_play((s32) gTexAnimCyclone, -1);
    anim_set_speed((AnimChannel *) gCycloneAnim, 7, 2);
    anim_set_loop_mode((AnimChannel *) gCycloneAnim, 7, 1);
    anim_set_interp(0.5f, (AnimChannel *) gCycloneAnim, 7, 1);
    anim_set_speed((AnimChannel *) gCycloneAnim, 8, 2);
    anim_set_loop_mode((AnimChannel *) gCycloneAnim, 8, 1);
    anim_set_interp(0.5f, (AnimChannel *) gCycloneAnim, 8, 1);
    anim_set_speed((AnimChannel *) gCycloneAnim, 9, 4);
    anim_set_loop_mode((AnimChannel *) gCycloneAnim, 9, 1);
    anim_set_interp(0.5f, (AnimChannel *) gCycloneAnim, 9, 1);
    anim_set_interp(0.5f, (AnimChannel *) gCycloneAnim, 1, 1);
    anim_set_interp(0.5f, (AnimChannel *) gCycloneAnim, 5, 1);
    anim_set_interp(0.5f, (AnimChannel *) gCycloneAnim, 6, 1);
    anim_set_interp(0.0f, (AnimChannel *) gCycloneAnim, 3, 0);
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/8AEE0/cyclone_enter.s")
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
/* The reground body of the others; never called (no type-16 case) (0x802D0E44).
 * Re-ground vehicle type 16, as starski_reground (key 0x10, cyclone_build_model). No
 * callers are known; same register convention as starski_reground. */
void cyclone_reground(s32 fpIn, s32 a3, f32 f12, f32 f14, f32 f20, f32 f22, f32 f24, f32 f26) {
    u8 *v = gCycloneState;
    TriSideOut f;
    Out802A9A60 o9;
    VehMtxOut mo;
    s32 x = gCyclonePos[0];
    s32 z = gCyclonePos[2];

    f.pz = f12;
    f.cross = f14;
    f.cz = f20;
    f.side = f22;
    f.sideZ = f24;
    f.dz = f26;
    veh_reground((s16 *) (v + 0x52), gCyclonePos[1], x, z, (s32 *) (v + 4), (s32 *) &gCyclonePos[1],
                  (s16 *) (v + 0x4C), 0x10, fpIn, v, &f, &o9);
    cyclone_build_model(a3, z, (s32) o9.s1, &mo);
    veh_set_record(gCyclonePos[2], 0x10, gCyclonePos[0], gCyclonePos[1], v);
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/8AEE0/cyclone_reground.s")
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
extern u32 gCyclonePos[]; /* x, y, z */
/* Veh_zone_alpha at its position (0x802D0F54).
 * Zone level lookup (veh_zone_alpha) for vehicle id 0x10 at its position
 * gCyclonePos..+8; returns veh_zone_alpha's v1 (the zone list end).
 * Register convention: the asm passes veh_zone_alpha's scan registers t6, t7,
 * s0-s4 through (in and out; here through r), leaves a3 = id and clobbers
 * s5, s6 (conventions.txt). Asm caller cyclone_update keeps f12 and f14 live
 * (a mixed N64 build would need a thunk; the native port won't). */
s32 cyclone_zone_alpha(ZoneScanRegs *r) {
    return veh_zone_alpha(0x10, gCyclonePos[0], gCyclonePos[1], gCyclonePos[2], r);
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/8AEE0/cyclone_zone_alpha.s")
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
/* The Cyclone Suit driven by the player, per frame (0x802D0F98).
 * Per-frame update of vehicle type 16 (from 00000.c's veh_update_player and once
 * from cyclone_spawn), the shape of starski_update: zone level
 * (cyclone_zone_alpha), the animation state machine (cyclone_update_moves unless +0x9A),
 * timers (cyclone_set_physics), steering with the rate from cyclone_steer_rate,
 * throttle (veh_throttle, delta 12), braking when state +0xA1 is 0
 * (veh_coast), heading (rate 0x59D8, no sound), slope and speed (kind
 * 0x10, no clamp, 120.0), veh_set_target_heading while gCycloneEscaping, the move, ground
 * contact (gBounceSound = 0, divisors 0x78), animation (anim_update_model on
 * gCycloneAnim), model matrices (cyclone_build_model), the vehicle-state reset, the
 * collision passes and obj_collide_vehicle (always). On a ring hit (gCollBlocked):
 * gCycloneEscaping = 1, gCycloneNearArcEnd = 1 if the ring distance is under 0x190 (which
 * also makes the v1 handed to veh_step_heading 1), and the ring steering with
 * scale 0.25 (the new heading also to gCycloneEscapeHeading); else gCycloneEscaping and
 * gCycloneNearArcEnd are cleared. Finally gPlayerX.. and the gVehicles record.
 * Register convention: as starski_update (zone registers t6, t7, s0-s4 into
 * cyclone_zone_alpha, whose results are dead here; f14-f26 into veh_ground_contact).
 * veh_collision_setup gets b0 = cyclone_build_model's t0 (0x10) and h2 = its t2 (the
 * end of the model's point list), as the asm leaves them - a slip in the
 * original (starski_update passes 7 and 0x96 there).
 * Leaked registers (FIDELITY): cyclone_update_moves (asm: anim_blend_channels) changes
 * f20/f30 in the game before f20 reaches veh_ground_contact; cyclone_build_model's
 * a3/s0/s1 are the stub-model values (+0x34, +0x96, &z) where in the game
 * anim_update_model has changed s0/s1. */
void cyclone_update(s32 t6, s32 t7, s32 s0, s32 s1, s32 s2, s32 s3, s32 s4, f32 f14, f32 f20, f32 f22, f32 f24,
                   f32 f26) {
    u8 *v = gCycloneState;
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
    cyclone_zone_alpha(&zr);
    if (v[0x9A] == 0) {
        cyclone_update_moves();
    }
    cyclone_set_physics();
    veh_steer(cyclone_steer_rate(), (u16 *) (v + 0x4C));
    veh_throttle(v, (s16 *) (v + 0x76), 3, v + 0x96, (s16 *) (v + 0x78), 0xC);
    if (v[0xA1] == 0) {
        veh_coast(v);
    }
    veh_turn_heading(v, (u16 *) (v + 0x74), 0x59D8, (s16 *) (v + 0x76), (u16 *) (v + 0x4C), (u16 *) (v + 0x4E),
                  (s8 *) (v + 0x99), 0);
    f12 = veh_slope((s16 *) (v + 0x76), v + 0x96, (s32 *) (v + 4), (f32 *) v);
    veh_apply_slope(v, (s16 *) (v + 0x76), 0x10, (s8 *) (v + 0x96), (s32 *) (v + 4), 0, 120.0f);
    if (gCycloneEscaping != 0) {
        veh_set_target_heading(v, &gCycloneEscapeHeading);
    }
    x = veh_move_along_heading(f12, *(u16 *) (v + 0x4E), (s16 *) (v + 0x76), (s32 *) &gCyclonePos[0],
                      (s32 *) &gCyclonePos[2], &o);
    gBounceSound = 0;
    r8.s3 = o.s3;
    f.pz = f12;
    f.cross = f14;
    f.cz = f20;
    f.side = f22;
    f.sideZ = f24;
    f.dz = f26;
    veh_ground_contact(v, 0x10, (s32 *) &gCyclonePos[0], (s32 *) &gCyclonePos[1], (s32 *) &gCyclonePos[2], x, o.t1, 0x78,
                  0x78, (s16 *) (v + 0x4C), v + 0x96, (s16 *) (v + 0x52), (s32 *) (v + 0x28), (s32 *) (v + 0x40),
                  (s32 *) (v + 0x34), (s32 *) (v + 4), &r8, &f);
    if (gBufferIndex != 0) {
        anim_update_model((u8 *) gCycloneMtxA, (u8 *) gCycloneMtxB, (AnimChannel *) gCycloneAnim);
    } else {
        anim_update_model((u8 *) gCycloneMtxB, (u8 *) gCycloneMtxA, (AnimChannel *) gCycloneAnim);
    }
    tag = cyclone_build_model((s32) (v + 0x34), (s32) (v + 0x96), (s32) &gCyclonePos[2], &mo);
    veh_collision_setup(gCyclonePos[2], (s32) gCycloneNonBlockingParts, 0, 0, gCyclonePos[0], gCyclonePos[1], tag, *(s16 *) (v + 0x76),
                  mo.t2, 0, 0x10, v);
    veh_collide_spheres(0x10, v);
    veh_collide_world(0x10);
    gCollVehicleAnim = gCycloneAnim;
    obj_collide_vehicle(0x10, v);
    if (gCollBlocked != 0) {
        veh_prepare_escape(v);
        gCycloneEscaping = 1;
        gCycloneNearArcEnd = 0;
        turn = veh_free_arc_mid();
        PORT_RING_DIST(d, *(u16 *) (v + 0x4E), turn);
        if (d < 0x190) {
            d = 1;
            gCycloneNearArcEnd = 1;
        }
        veh_reverse_if_behind(v);
        turn = veh_turn_into_free_arc(v, *(u16 *) (v + 0x4E), *(u16 *) (v + 0x4C), &cur, 0.25f);
        gCycloneEscapeHeading = cur;
        *(s16 *) (v + 0x4E) = cur;
        *(s16 *) (v + 0x74) = cur;
        veh_step_heading(v, turn, d, &tgt);
        veh_hit_slowdown(v, 0);
    } else {
        gCycloneEscaping = 0;
        gCycloneNearArcEnd = 0;
    }
    gPlayerX = gCyclonePos[0];
    gPlayerY = gCyclonePos[1];
    gPlayerZ = gCyclonePos[2];
    gPlayerSpeed = *(s16 *) (v + 0x76);
    gPlayerMoveHeading = *(u16 *) (v + 0x4E);
    gPlayerHeading = *(u16 *) (v + 0x4C);
    veh_set_record(gCyclonePos[2], 0x10, gCyclonePos[0], gCyclonePos[1], v);
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/8AEE0/cyclone_update.s")
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
extern s16 gCycloneStepFrame;  /* last channel unk13 seen (sound trigger) */
extern u8 gCycloneNearArcEnd;
extern f32 gCycloneBlurSpacing;

#define VEH16_U8(o) (gCycloneState[o])
#define VEH16_SPEED (*(s16 *) (gCycloneState + 0x76))
/* anim_get_state's results: [0] = unk10 (active), [1] = unk11, [6] = unk13. */
#define CHAN_GET(idx, e) anim_get_state((idx), (AnimChannel *) gCycloneAnim, (e))

/* Channel 0x1F: unk14 = val, unk11 = unk12 = 0, restart (anim_play). */
static void port_802D1360_chan1F(s32 val) {
    anim_set_speed((AnimChannel *) gCycloneAnim, 0x1F, val);
    anim_set_reverse((AnimChannel *) gCycloneAnim, 0x1F, 0);
    anim_set_loop_mode((AnimChannel *) gCycloneAnim, 0x1F, 0);
    anim_play((AnimChannel *) gCycloneAnim, 0x1F, 1);
}

/* Moving, no special state: drive the gear channel chosen by +0xA2 (0, 1, 2
 * -> channel 1, 5, 6). If it isn't active, pick a gear from |speed| (< 60,
 * < 110, else), restart that channel (val 2 if the inactive channel's unk11
 * was set, else 1), set +0xA2 and try again. An active channel gets
 * direction (speed < 0) and rate |speed| / 24. */
static void port_802D1360_gear(void) {
    s32 e[8];
    s32 idx;
    s32 v;

    switch (VEH16_U8(VS_ACTION_AUX)) {
        case 0:
            idx = 1;
            break;
        case 1:
            idx = 5;
            break;
        case 2:
            idx = 6;
            break;
        default: /* the asm executes `syscall` here */
            return;
    }
    for (;;) {
        CHAN_GET(idx, e);
        if (e[0] != 0) {
            break;
        }
        v = VEH16_SPEED;
        if (v < 0) {
            v = -v;
        }
        if (v < 60) {
            idx = 1;
            VEH16_U8(VS_ACTION_AUX) = 0;
        } else if (v < 110) {
            idx = 5;
            VEH16_U8(VS_ACTION_AUX) = 1;
        } else {
            idx = 6;
            VEH16_U8(VS_ACTION_AUX) = 2;
        }
        anim_play((AnimChannel *) gCycloneAnim, idx, (e[1] != 0) ? 2 : 1);
        anim_set_frame(0.0f, (AnimChannel *) gCycloneAnim, idx, 0);
    }
    v = VEH16_SPEED;
    anim_set_reverse((AnimChannel *) gCycloneAnim, idx, v < 0);
    if (v < 0) {
        v = -v;
    }
    if (v != 0) {
        v = (u32) v / 24;
    }
    anim_set_speed((AnimChannel *) gCycloneAnim, idx, v);
}

/* State 0 (+0xA1 == 0): driving. */
static void port_802D1360_state0(void) {
    s32 e[8];
    s32 r;
    s16 prev;

    /* the unk13 of the first active channel of 1, 5, 6 triggers a sound when
     * it changes to 6 (0x4E) or 2 (0x4F) */
    CHAN_GET(1, e);
    if (e[0] == 1 || (CHAN_GET(5, e), e[0] == 1) || (CHAN_GET(6, e), e[0] == 1)) {
        prev = gCycloneStepFrame;
        gCycloneStepFrame = e[6];
        if (e[6] != prev) {
            if (e[6] == 6) {
                snd_play(gSfxBank, SFX_MECH_STEP_A, NULL);
            } else if (e[6] == 2) {
                snd_play(gSfxBank, SFX_MECH_STEP_B, NULL);
            }
        }
    }

    if (VEH16_SPEED == 0) {
        /* standing: leave the moving pose once (+0xA3), then idle anims */
        if (VEH16_U8(VS_MOVING) != 0) {
            anim_set_frame(0.0f, (AnimChannel *) gCycloneAnim, 7, 0);
            if (CHAN_GET(0x1F, e), e[0] != 0) {
                anim_stop(0x1F, (AnimChannel *) gCycloneAnim);
                anim_blend_channels(0x1F, 7, (AnimChannel *) gCycloneAnim);
            } else if (CHAN_GET(1, e), e[0] != 0) {
                anim_stop(1, (AnimChannel *) gCycloneAnim);
                anim_blend_channels(1, 7, (AnimChannel *) gCycloneAnim);
            } else if (CHAN_GET(5, e), e[0] != 0) {
                anim_stop(5, (AnimChannel *) gCycloneAnim);
                anim_blend_channels(5, 7, (AnimChannel *) gCycloneAnim);
            } else if (CHAN_GET(6, e), e[0] != 0) {
                anim_stop(6, (AnimChannel *) gCycloneAnim);
                anim_blend_channels(6, 7, (AnimChannel *) gCycloneAnim);
            } else {
                anim_set_frame(0.0f, (AnimChannel *) gCycloneAnim, 1, 0);
                anim_blend_channels(1, 7, (AnimChannel *) gCycloneAnim);
            }
            port_802D1360_chan1F(0x1E);
        }
        VEH16_U8(VS_MOVING) = 0;
        if (CHAN_GET(0x1F, e), e[0] == 1) {
            return;
        }
        if (CHAN_GET(7, e), e[0] == 1) {
            return;
        }
        if (CHAN_GET(8, e), e[0] == 1) {
            return;
        }
        if (CHAN_GET(9, e), e[0] == 1) {
            return;
        }
        if (util_rand2_range(0, 0x1E) != 0) {
            return;
        }
        r = util_rand2_range(0, 2);
        if (r == 0) {
            r = 7;
        } else if (r == 1) {
            r = 8;
        } else {
            r = 9;
        }
        anim_set_frame(0.0f, (AnimChannel *) gCycloneAnim, r, 0);
        anim_play((AnimChannel *) gCycloneAnim, r, 1);
        return;
    }

    /* moving: enter the moving pose once (+0xA3) */
    if (VEH16_U8(VS_MOVING) != 1) {
        anim_set_frame(0.0f, (AnimChannel *) gCycloneAnim, 1, 0);
        if (CHAN_GET(7, e), e[0] != 0) {
            anim_blend_channels(7, 1, (AnimChannel *) gCycloneAnim);
            anim_stop(7, (AnimChannel *) gCycloneAnim);
        } else if (CHAN_GET(8, e), e[0] != 0) {
            anim_blend_channels(8, 1, (AnimChannel *) gCycloneAnim);
            anim_stop(8, (AnimChannel *) gCycloneAnim);
        } else if (CHAN_GET(9, e), e[0] != 0) {
            anim_blend_channels(9, 1, (AnimChannel *) gCycloneAnim);
            anim_stop(9, (AnimChannel *) gCycloneAnim);
        } else {
            anim_set_frame(0.0f, (AnimChannel *) gCycloneAnim, 7, 0);
            anim_blend_channels(7, 1, (AnimChannel *) gCycloneAnim);
        }
        port_802D1360_chan1F(0x28);
    }
    VEH16_U8(VS_MOVING) = 1;
    if (CHAN_GET(0x1F, e), e[0] == 1) {
        return;
    }
    gVehAttacking = 0;
    if (((gControlMethod == 0 && (gHeldA != 0 || gHeldB != 0)) || gHeldL != 0 || gHeldR != 0) &&
        VEH16_SPEED >= 0x96) {
        /* start the special move (state 1) from the current gear channel */
        VEH16_U8(VS_ACTION) = 1;
        anim_set_frame(0.0f, (AnimChannel *) gCycloneAnim, 2, 0);
        switch (VEH16_U8(VS_ACTION_AUX)) {
            case 0:
                r = 1;
                break;
            case 1:
                r = 5;
                break;
            case 2:
                r = 6;
                break;
            default: /* the asm executes `syscall` here */
                return;
        }
        anim_blend_channels(r, 2, (AnimChannel *) gCycloneAnim);
        anim_stop(r, (AnimChannel *) gCycloneAnim);
        port_802D1360_chan1F(0x50);
        mb_start(6, gCycloneBlurSpacing, 0x64);
        return;
    }
    port_802D1360_gear();
}

/* States 1 and 2: the special move. Hitting something (+0x9C) halves the
 * speed and goes to state 3; +0x9D or gCycloneNearArcEnd ends it (state 0);
 * otherwise speed = 280 and, in state 1, channel 2 is started (state 2) once
 * channel 0x1F has finished; in state 2 the move ends when channel 2 has. */
static void port_802D1360_state12(s32 state) {
    s32 e[8];

    if (VEH16_U8(VS_HIT_PART_DOWN) != 0) {
        VEH16_SPEED = VEH16_SPEED >> 1;
        if (state == 1) {
            mb_stop();
            VEH16_U8(VS_ACTION) = 3;
            anim_set_frame(0.0f, (AnimChannel *) gCycloneAnim, 3, 0);
            anim_blend_channels(0x1F, 3, (AnimChannel *) gCycloneAnim);
        } else {
            VEH16_U8(VS_ACTION) = 3;
            anim_set_frame(0.0f, (AnimChannel *) gCycloneAnim, 3, 0);
            anim_blend_channels(2, 3, (AnimChannel *) gCycloneAnim);
            anim_stop(2, (AnimChannel *) gCycloneAnim);
        }
        port_802D1360_chan1F(0x21);
        gVehAttacking = 1;
        return;
    }
    if (VEH16_U8(VS_HIT_OBJECT) == 0 && gCycloneNearArcEnd == 0) {
        VEH16_SPEED = 0x118;
        if (state == 1) {
            if (CHAN_GET(0x1F, e), e[0] == 1) {
                return;
            }
            anim_set_speed((AnimChannel *) gCycloneAnim, 2, 0xA);
            anim_set_reverse((AnimChannel *) gCycloneAnim, 2, 0);
            anim_set_loop_mode((AnimChannel *) gCycloneAnim, 2, 0);
            anim_play((AnimChannel *) gCycloneAnim, 2, 1);
            VEH16_U8(VS_ACTION) = 2;
            return;
        }
        if (CHAN_GET(2, e), e[0] == 1) {
            return;
        }
    }
    if (state == 1) {
        if (VEH16_SPEED >= 0) {
            VEH16_SPEED = 0x3C;
        }
        veh_stop_engine_sounds();
        anim_set_frame(0.0f, (AnimChannel *) gCycloneAnim, 1, 0);
        anim_blend_channels(0x1F, 1, (AnimChannel *) gCycloneAnim);
    } else {
        veh_stop_engine_sounds();
        if (VEH16_SPEED >= 0) {
            VEH16_SPEED = 0x3C;
        }
        anim_set_frame(0.0f, (AnimChannel *) gCycloneAnim, 1, 0);
        anim_stop(2, (AnimChannel *) gCycloneAnim);
        anim_blend_channels(2, 1, (AnimChannel *) gCycloneAnim);
    }
    port_802D1360_chan1F(0x32);
    mb_stop();
    VEH16_U8(VS_ACTION) = 0;
}

/* Walk and gears, the charge (states 1-2), the crash (3-4) (0x802D1360).
 * Animation / state machine of vehicle type 16 ($gp = gCycloneState, read as
 * the global; channels in gCycloneAnim, an AnimChannel table), on the
 * state byte +0xA1: 0 driving (gears, idle anims, starting the special move
 * with the inputs at speed >= 150), 1 and 2 the special move, 3 the crash
 * (sound 0x4B, veh_prev_contacts_clear, then channel 3 -> state 4), 4 recovering (back
 * to state 0 when channel 3 has finished). Sound 0x51 (handle gEngineSound) is
 * started on the special move. Out-of-range states/gears hit a `syscall` in
 * the asm; the C just returns (keep +0xA1 in 0..4 and +0xA2 in 0..2).
 * Register note: anim_blend_channels changes f20 and f30 and the asm leaves them
 * that way; asm caller cyclone_update reads f20 afterwards (conventions.txt:
 * clobbers s5, f20, f30), which C can't reproduce. */
void cyclone_update_moves(void) {
    s32 e[8];

    switch (VEH16_U8(VS_ACTION)) {
        case 0:
            port_802D1360_state0();
            break;
        case 1:
            if (gEngineSound == NULL) {
                snd_play(gSfxBank, SFX_ROLL_LOOP, &gEngineSound);
            }
            port_802D1360_state12(1);
            break;
        case 2:
            port_802D1360_state12(2);
            break;
        case 3:
            if (gEngineSound != NULL) {
                veh_stop_engine_sounds();
                snd_play(gSfxBank, SFX_ROLL_CRASH, NULL);
            }
            veh_prev_contacts_clear();
            if (CHAN_GET(0x1F, e), e[0] == 1) {
                break;
            }
            gVehAttacking = 0;
            mb_stop();
            anim_set_speed((AnimChannel *) gCycloneAnim, 3, 5);
            anim_set_reverse((AnimChannel *) gCycloneAnim, 3, 0);
            anim_set_loop_mode((AnimChannel *) gCycloneAnim, 3, 0);
            anim_play((AnimChannel *) gCycloneAnim, 3, 1);
            VEH16_U8(VS_ACTION) = 4;
            break;
        case 4:
            veh_prev_contacts_clear();
            if (CHAN_GET(3, e), e[0] == 1) {
                break;
            }
            gVehAttacking = 1;
            anim_set_frame(0.0f, (AnimChannel *) gCycloneAnim, 1, 0);
            anim_set_frame(0.0f, (AnimChannel *) gCycloneAnim, 5, 0);
            anim_set_frame(0.0f, (AnimChannel *) gCycloneAnim, 6, 0);
            VEH16_U8(VS_ACTION) = 0;
            break;
        default: /* the asm executes `syscall` here */
            break;
    }
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/8AEE0/cyclone_update_moves.s")
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
/* Its model matrix, parts and points (0x802D22F4).
 * Model matrices of vehicle type 16, as starski_build_model but with scale 0x2134,
 * tag 0x10, buffers gCycloneMtxA/B, model gCycloneModel, and the rotation set to
 * (0, heading + 0x400 wrapped by 0xFFF when >= 0x1000, 0). Same register
 * convention (outputs t0 = 0x10, t2, s1, s4: cyclone_update passes t0 and t2
 * on to veh_collision_setup). */
s32 cyclone_build_model(s32 a3, s32 s0, s32 s1, VehMtxOut *out) {
    u8 *model = gCycloneModel;
    u8 *base;
    s32 *m;
    MtxChainRegs regs;
    s32 a;
    s32 x;
    s32 y;
    s32 z;

    m = (s32 *) N64_IPTR(*(s32 *) (model + *(s32 *) (model + 0x18) + 4) +
                 (s32) (gBufferIndex != 0 ? gCycloneMtxA : gCycloneMtxB));
    a = *(u16 *) (gCycloneState + 0x4C) + 0x400;
    if (a >= 0x1000) {
        a -= 0xFFF;
    }
    gModelRot[1] = a;
    gModelRot[0] = 0;
    gModelRot[2] = 0;
    mtx_build_model(gCyclonePos[0], gCyclonePos[1], gCyclonePos[2], 0x2134, m);
    base = (u8 *) (gBufferIndex != 0 ? gCycloneMtxA : gCycloneMtxB);
    model = gCycloneModel;
    x = gCyclonePos[0];
    y = gCyclonePos[1];
    z = gCyclonePos[2];
    regs.a3 = a3;
    regs.s1 = s1;
    regs.s2 = (s32) m;
    regs.s0 = s0;
    veh_place_parts(x, y, z, 0x10, model + *(s32 *) (model + 4), model + *(s32 *) (model + 8), base, &regs);
    model = gCycloneModel;
    regs.v1 = y;
    regs.a0 = z;
    veh_points_transform(0x10, (s16 *) (model + *(s32 *) model), (s16 *) (model + *(s32 *) (model + 4)), base, &regs);
    out->t2 = (s32) (model + *(s32 *) (model + 4));
    out->s1 = regs.s1;
    out->s4 = (s32) base;
    return 0x10;
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/8AEE0/cyclone_build_model.s")
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
/* Its steering rate (0x802D2444).
 * 0 when the speed (s16 at +0x76 of gCycloneState, the asm's $gp) is 0, else 5
 * when the byte at +0xA1 is 1..4, else 110. The asm returns it in s3 (see
 * tools_port/conventions.txt). Its asm caller cyclone_update keeps a0-a3 live
 * across the call (a mixed N64 build would need a thunk). Same shape as
 * thunderfist_steer_rate (6C5E0). */
s32 cyclone_steer_rate(void) {
    u8 t;

    if (*(s16 *) (gCycloneState + 0x76) == 0) {
        return 0;
    }
    t = gCycloneState[0xA1];
    if (t == 2 || t == 1 || t == 3 || t == 4) {
        return 5;
    }
    return 110;
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/8AEE0/cyclone_steer_rate.s")
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
/* Functional rewrite (tools_port/eqcheck.py verified). Same "set timers"
 * shape as carrier_set_physics (75490.c). Asm caller cyclone_update relies on a0-a3
 * being preserved (mixed N64 build would need a thunk). */

/* Its gravity and bounce (0x802D249C). */
void cyclone_set_physics(void) {
    gGravity = gLevelGravity * 4.0f;
    gBounceThreshold = 40;
    gBounceDamping = 3;
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/8AEE0/cyclone_set_physics.s")
#endif

/* cyclone_save_state/cyclone_load_state: two-address trampolines into
 * veh_serialize_state/veh_deserialize_state, same confirmed-unreachable-from-C 8-byte
 * sd-$ra frame as veh_check_wrap (hd_code/679E0.c). Permanently
 * GLOBAL_ASM. */
#ifdef NON_MATCHING
extern u8 gCycloneState[];  /* this vehicle's state block: 0xA6 bytes are (de)serialized */
extern u32 gCyclonePos[]; /* plus these three words */

/* Its state into a buffer (0x802D24F8).
 * Serialize this vehicle's state (gCycloneState[0..0xA5] plus the three words
 * gCyclonePos[0..2]) into `dst` via veh_serialize_state. Returns the callee's result
 * (0xB2, the byte count): the asm leaves the callee's v0 untouched. */
s32 cyclone_save_state(u8 *dst) {
    return veh_serialize_state(dst, gCycloneState, gCyclonePos);
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/8AEE0/cyclone_save_state.s")
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
/* Its state from a buffer (recording start) (0x802D2524).
 * Inverse of cyclone_save_state: restore this vehicle's state from `src` via
 * veh_deserialize_state. Called from 17210.c as `void cyclone_load_state(void *)`. */
void cyclone_load_state(void *src) {
    veh_deserialize_state(src, gCycloneState, gCyclonePos);
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/8AEE0/cyclone_load_state.s")
#endif

/* Writes a0 to the COP0 Compare register (the game's own stub, sd-$ra frame); no callers (0x802D2550).
 * hd_set_cop0_compare: `mtc0 $a0, $11` (write COP0 Compare register) wrapped in a
 * dead $ra save/restore frame - same hand-written COP0-leaf-stub character
 * as __osSetSR in init/2330.c. Permanently GLOBAL_ASM. */
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/8AEE0/hd_set_cop0_compare.s")
