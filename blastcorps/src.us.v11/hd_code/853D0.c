#include "common.h"
#include <ultra64.h>
#include "game/game.h"

/* FILE-WIDE FINDING: this file's functions save $ra via the 64-bit `sd`/`ld`
 * doubleword form, not the normal 32-bit `sw`/`lw` pair - the same signature
 * documented in full in hd_code/77E20.c's top-of-file note and the project
 * skill file, confirming (with no exceptions found) that this file is part
 * of the same large hand-written-assembly module. Any pragma below without
 * a more specific comment follows this convention; a few have their own
 * more specific non-ABI explanation where one was already worked out. */
/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
extern u8 gBallistaState[];  /* this vehicle's state block (the asm's $gp) */
extern u8 gBallistaAnim[];  /* its animation channel table */
extern u32 gBallistaPos[]; /* x, y, z */
extern u8 * N64P gBallistaModel;   /* its model */
extern u64 * N64P gBallistaMtxA;  /* save copy pair */
extern u64 * N64P gBallistaMtxB;
extern f32 gBallistaLean;
extern u8 gBallistaDustTimer;
extern u8 D_803F8B77;
extern s8 gBallistaEscaping;
extern u8 gBallistaThrottleHold;
extern u8 gBallistaBounced;
extern u8 gBallistaJumping;
extern u8 gBallistaFireCooldown;
extern u8 gBallistaFireSide;

/* Sets up Ballista (type 10) (0x802C9B90).
 * Load vehicle 10 (the 5CB60 dispatcher lvl_spawn_vehicles, record type 10):
 * model `model` (gBallistaModel), save copies gBallistaMtxA / gBallistaMtxB taken
 * 0x700 bytes each from gHeapPtr (veh_add_record(10, 0, copies, model)),
 * record reset (veh_reset_state), wheel table +0x52..0x68, position (x, y,
 * z), heading `heading` at +0x4C/+0x4E/+0x74, gBallistaLean = 0.5, ground
 * heights (veh_init_ground, key 10, fp), the model's animation channels
 * (anim_init_model, channel 0 set to 100 / 0 / 0 / 0.0 / restart, run on both
 * copies with anim_update_model and anim_rewind between), tuning values
 * +0x78..0x94, its flags and counters, parts (veh_register_parts, scale 0x3E80),
 * veh_add_shadow(10, 100, 45, 45), one update with +0x9A set
 * (ballista_update), copy the model matrix from the second copy to the first
 * (mtx_copy_at) and gHaveBallista = 1.
 * Register convention (conventions.txt): x t7, y s3, z s0, heading s1,
 * model s2, fp (handed to veh_init_ground, which stores it into gSlotSurface:
 * the dispatcher's leftover $fp in the game). The FP block veh_init_ground
 * passes through starts at 0 here (not read first). The asm saves t0-t5
 * (asm caller lvl_spawn_vehicles keeps t1, t2 live: a mixed N64 build would need
 * a thunk) and leaves s2-s4, fp and f12-f26 as its callees leave them
 * (survey: read by lvl_spawn_vehicles's next records; not modelled) and $gp =
 * the block. Adds are trapping in the asm. */
void ballista_spawn(s32 x, s32 y, s32 z, s32 heading, u8 *model, s32 fp) {
    TriSideOut f;
    u8 *m;

    gBallistaModel = model;
    gBallistaMtxA = (u64 *) gHeapPtr;
    gBallistaMtxB = (u64 *) (gHeapPtr + 0x700);
    gHeapPtr += 0xE00;
    veh_add_record(VEH_BALLISTA, 0, (s32) gBallistaMtxA, (s32) gBallistaMtxB, model);
    veh_reset_state(gBallistaState);
    *(s16 *) (gBallistaState + 0x52) = 0x104;
    *(s16 *) (gBallistaState + 0x54) = 0x140;
    *(s16 *) (gBallistaState + 0x56) = -0x104;
    *(s16 *) (gBallistaState + 0x58) = 0x140;
    *(s16 *) (gBallistaState + 0x5A) = 0x104;
    *(s16 *) (gBallistaState + 0x5C) = -0x140;
    *(s16 *) (gBallistaState + 0x5E) = 0x12C;
    *(s16 *) (gBallistaState + 0x60) = 0x154;
    *(s16 *) (gBallistaState + 0x62) = -0x12C;
    *(s16 *) (gBallistaState + 0x64) = 0x154;
    *(s16 *) (gBallistaState + 0x66) = 0x12C;
    *(s16 *) (gBallistaState + 0x68) = -0x154;
    gBallistaPos[0] = x;
    gBallistaPos[1] = y;
    gBallistaPos[2] = z;
    gBallistaJumping = 0;
    gBallistaFireSide = 0;
    gBallistaLean = 0.5f;
    *(s16 *) (gBallistaState + 0x4C) = heading;
    *(s16 *) (gBallistaState + 0x4E) = heading;
    *(s16 *) (gBallistaState + 0x74) = heading;
    f.pz = f.cross = f.cz = f.side = f.sideZ = f.dz = 0.0f;
    veh_init_ground((s16 *) (gBallistaState + 0x52), gBallistaPos[1], x, z, (s32 *) (gBallistaState + 4),
                  (s32 *) &gBallistaPos[1], (s16 *) (gBallistaState + 0x4C), 10, fp, gBallistaState, &f);
    anim_init_model((u32 *) gBallistaMtxB, (u32 *) gBallistaMtxA, (AnimChannel *) gBallistaAnim, gBallistaModel);
    anim_set_speed((AnimChannel *) gBallistaAnim, 0, 100);
    anim_set_reverse((AnimChannel *) gBallistaAnim, 0, 0);
    anim_set_loop_mode((AnimChannel *) gBallistaAnim, 0, 0);
    anim_set_interp(0.0f, (AnimChannel *) gBallistaAnim, 0, 0);
    anim_play((AnimChannel *) gBallistaAnim, 0, 1);
    anim_update_model((u8 *) gBallistaMtxA, (u8 *) gBallistaMtxB, (AnimChannel *) gBallistaAnim);
    anim_rewind(0, (AnimChannel *) gBallistaAnim);
    anim_play((AnimChannel *) gBallistaAnim, 0, 1);
    anim_update_model((u8 *) gBallistaMtxB, (u8 *) gBallistaMtxA, (AnimChannel *) gBallistaAnim);
    *(s16 *) (gBallistaState + 0x78) = -0x96;
    *(s16 *) (gBallistaState + 0x7A) = 0;
    *(s16 *) (gBallistaState + 0x7C) = 2;
    *(s16 *) (gBallistaState + 0x7E) = 0;
    *(s16 *) (gBallistaState + 0x80) = 0xB4;
    *(s16 *) (gBallistaState + 0x82) = 8;
    *(s16 *) (gBallistaState + 0x84) = 0xB4;
    *(s16 *) (gBallistaState + 0x86) = 0xC8;
    *(s16 *) (gBallistaState + 0x88) = 4;
    *(s16 *) (gBallistaState + 0x8A) = 0xC8;
    *(s16 *) (gBallistaState + 0x8C) = 0xFA;
    *(s16 *) (gBallistaState + 0x8E) = 2;
    *(s16 *) (gBallistaState + 0x90) = 0xFA;
    *(s16 *) (gBallistaState + 0x92) = 0x104;
    *(s16 *) (gBallistaState + 0x94) = 2;
    gBallistaEscaping = 0;
    gBallistaBounced = 0;
    gBallistaThrottleHold = 0;
    gBallistaDustTimer = 0;
    D_803F8B77 = 0x32;
    gBallistaFireCooldown = 0;
    gBallistaMissiles = 0;
    m = gBallistaModel;
    veh_register_parts(10, m + *(s32 *) (m + 4), m + *(s32 *) (m + 8), 0x3E80);
    veh_add_shadow(VEH_BALLISTA, 0x64, 0x2D, 0x2D);
    gBallistaState[0x9A] = 1;
    ballista_update();
    gBallistaState[0x9A] = 0;
    m = gBallistaModel;
    mtx_copy_at((u8 *) gBallistaMtxB, (u8 *) gBallistaMtxA, *(s32 *) (m + *(s32 *) (m + 0x18) + 4));
    gHaveBallista = 1;
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/853D0/ballista_spawn.s")
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
extern u8 gBallistaState[]; /* this vehicle's state block (the asm's $gp) */
extern u8 gBallistaAnim[]; /* its animation channel table (AnimChannel, 56040.c) */

/* Getting into Ballista (0x802C9F54).
 * Enter this vehicle (vehicle type 10; called from hd.c and 17210.c): clears
 * the byte at +0x99, sets the pair gCamDistance / gCamHeightBase to (0xD48, 1000),
 * starts sound 0x94 (veh_start_engine_sound; the asm passes its caller's a0 through
 * as the ignored first argument) and sets up channels 1-3 (0 / 0 / 0|1,
 * restart with -1) and 4-5 (8|8 / 0 / 0, 1.0 with 1).
 * The asm points $gp at gBallistaState and leaves it there (conventions.txt:
 * clobbers gp) and leaves v1 = 1 (anim_set_interp preserves it); the C callers
 * declare it void and use neither. Same shape as jbomb_enter (7FB50). */
void ballista_enter(void) {
    gBallistaState[0x99] = 0;
    gCamDistance = 0xD48;
    gCamHeightBase = 1000;
    veh_start_engine_sound(0, 0x94);
    anim_set_speed((AnimChannel *) gBallistaAnim, 1, 0);
    anim_set_reverse((AnimChannel *) gBallistaAnim, 1, 0);
    anim_set_loop_mode((AnimChannel *) gBallistaAnim, 1, 0);
    anim_play((AnimChannel *) gBallistaAnim, 1, -1);
    anim_set_speed((AnimChannel *) gBallistaAnim, 2, 0);
    anim_set_reverse((AnimChannel *) gBallistaAnim, 2, 0);
    anim_set_loop_mode((AnimChannel *) gBallistaAnim, 2, 1);
    anim_play((AnimChannel *) gBallistaAnim, 2, -1);
    anim_set_speed((AnimChannel *) gBallistaAnim, 3, 0);
    anim_set_reverse((AnimChannel *) gBallistaAnim, 3, 0);
    anim_set_loop_mode((AnimChannel *) gBallistaAnim, 3, 1);
    anim_play((AnimChannel *) gBallistaAnim, 3, -1);
    anim_set_speed((AnimChannel *) gBallistaAnim, 4, 8);
    anim_set_reverse((AnimChannel *) gBallistaAnim, 4, 0);
    anim_set_loop_mode((AnimChannel *) gBallistaAnim, 4, 0);
    anim_set_interp(1.0f, (AnimChannel *) gBallistaAnim, 4, 1);
    anim_set_speed((AnimChannel *) gBallistaAnim, 5, 8);
    anim_set_reverse((AnimChannel *) gBallistaAnim, 5, 0);
    anim_set_loop_mode((AnimChannel *) gBallistaAnim, 5, 0);
    anim_set_interp(1.0f, (AnimChannel *) gBallistaAnim, 5, 1);
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/853D0/ballista_enter.s")
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
/* Functional rewrite (tools_port/eqcheck.py verified). Vehicle-type 10
 * "can exit" check: 0 if any of the vehicle's bytes 0x96..0x98 equals 1 or
 * gBallistaJumping (= its byte 0xDC) is nonzero, else 1. Same shape as
 * police_exit_check, ateam_exit_check, starski_exit_check, cyclone_exit_check. 00000.c
 * declares it void and ignores the result, but the asm returns 0/1 in v0. */
extern u8 gBallistaState[];
extern u8 gBallistaJumping;

/* Ballista's exit check (not during a wheelie) (0x802CA140). */
s32 ballista_exit_check(void) {
    if (gBallistaState[0x96] == 1 || gBallistaState[0x97] == 1 || gBallistaState[0x98] == 1 || gBallistaJumping != 0) {
        return 0;
    }
    return 1;
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/853D0/ballista_exit_check.s")
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
extern u64 * N64P gBallistaMtxA; /* save copy pair (veh_sync_buffers) */
extern u64 * N64P gBallistaMtxB;

/* Getting out of Ballista (0x802CA1AC).
 * Teardown for this vehicle (called from veh_leave in hd.c): clears the
 * s16 at +0x76, veh_sync_buffers(gBallistaMtxA, gBallistaMtxB, 0x700) and stops its
 * sounds (veh_stop_engine_sounds). The asm saves and restores $gp. Same shape as
 * jbomb_leave (7FB50). */
void ballista_leave(void) {
    *(s16 *) (gBallistaState + 0x76) = 0;
    veh_sync_buffers(gBallistaMtxA, gBallistaMtxB, 0x700);
    veh_stop_engine_sounds();
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/853D0/ballista_leave.s")
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
extern u32 gBallistaPos[]; /* x, y, z */
void ballista_build_model(MtxChainRegs *regs);

/* The reground body of the others; never called (no type-10 case) (0x802CA1F8).
 * Move this vehicle while it isn't the player's (vehicle 10's counterpart
 * of jbomb_reground; no caller references it: veh_update_others has no case
 * 10): ground heights for its three slots (veh_reground with table +0x52,
 * angle +0x4C, heights to +4.., the middle y to D_803F8B4C, key 10), place
 * the model (ballista_build_model), then veh_set_record(z, 10, x, y, block).
 * Register convention (conventions.txt): `fp` is the asm's $fp, which goes
 * on to veh_reground (stored into gSlotSurface[0..2], so the block's +0x50
 * becomes (u8) fp): a leaked register in the game (FIDELITY), an explicit
 * parameter here. f12-f26 only pass through veh_reground (not read first;
 * the asm restores f20-f30), so the C starts them at 0. ballista_build_model gets
 * s0 = z and s1 = veh_reground's s1. The asm saves every s-register, gp,
 * fp and f20-f30. */
void ballista_reground(s32 fp) {
    TriSideOut f;
    Out802A9A60 out;
    MtxChainRegs regs;
    s32 z = gBallistaPos[2];

    f.pz = f.cross = f.cz = f.side = f.sideZ = f.dz = 0.0f;
    veh_reground((s16 *) (gBallistaState + 0x52), gBallistaPos[1], gBallistaPos[0], z, (s32 *) (gBallistaState + 4),
                  (s32 *) &gBallistaPos[1], (s16 *) (gBallistaState + 0x4C), 10, fp, gBallistaState, &f, &out);
    regs.s0 = z;
    regs.s1 = (s32) out.s1;
    ballista_build_model(&regs);
    veh_set_record(gBallistaPos[2], 10, gBallistaPos[0], gBallistaPos[1], gBallistaState);
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/853D0/ballista_reground.s")
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
extern u32 gBallistaPos[]; /* x, y, z */
/* Veh_zone_alpha at Ballista's position (0x802CA308).
 * Zone level lookup (veh_zone_alpha) for vehicle id 0xA at its position
 * gBallistaPos..+8; returns veh_zone_alpha's v1 (the zone list end).
 * Register convention: the asm passes veh_zone_alpha's scan registers t6, t7,
 * s0-s4 through (in and out; here through r), leaves a3 = id and clobbers
 * s5, s6 (conventions.txt). Asm caller ballista_update keeps f12 and f14 live
 * (a mixed N64 build would need a thunk; the native port won't). */
s32 ballista_zone_alpha(ZoneScanRegs *r) {
    return veh_zone_alpha(0xA, gBallistaPos[0], gBallistaPos[1], gBallistaPos[2], r);
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/853D0/ballista_zone_alpha.s")
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING

/* Records Ballista's position on the deck it stands on (0x802CA34C).
 * Value pairs on triangle `id` (veh_deck_to_local) at this vehicle's x/z
 * (gBallistaPos[0], [2]) -> s16s +0x6A/+0x6C of gBallistaState, and at that point
 * moved by ground slot 0 (veh_wheel_offset(0, +0x52 table, +0x4C angle)) ->
 * +0x70/+0x72; +0x6E gets a copy of the angle at +0x4C.
 * Register convention (conventions.txt): id in a3; veh_deck_to_local's FP
 * results pass through to the caller (r; f24 also an input, chained through
 * both calls). The asm saves and restores t0, t1, t3, t4 (asm caller
 * veh_attach_riders_walk keeps t0 and t1 live: a mixed N64 build would need a thunk),
 * points $gp at gBallistaState without restoring it, leaves s4 = &+0x4C and
 * v1 = &+0x52 (dead in its caller). Same shape as jbomb_ride_attach (7FB50) plus
 * the second point. The x + dx / z + dz sums are trapping in the asm. */
void ballista_ride_attach(s32 id, InterpRegs *r) {
    s32 dx;
    s32 dz;

    veh_deck_to_local(id, ((s32 *) gBallistaPos)[0], ((s32 *) gBallistaPos)[2], r);
    *(s16 *) (gBallistaState + 0x6A) = r->t3;
    *(s16 *) (gBallistaState + 0x6C) = r->t4;
    *(u16 *) (gBallistaState + 0x6E) = *(u16 *) (gBallistaState + 0x4C);
    dx = veh_wheel_offset(0, (s16 *) (gBallistaState + 0x52), (s16 *) (gBallistaState + 0x4C), &dz);
    veh_deck_to_local(id, ((s32 *) gBallistaPos)[0] + dx, ((s32 *) gBallistaPos)[2] + dz, r);
    *(s16 *) (gBallistaState + 0x70) = r->t3;
    *(s16 *) (gBallistaState + 0x72) = r->t4;
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/853D0/ballista_ride_attach.s")
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
void ballista_set_physics(void);

/* Moves Ballista with the deck under it (0x802CA3D8).
 * Set this vehicle down on triangle `id` between the two value pairs at
 * +0x6A/+0x6C and +0x70/+0x72 (ballista_ride_attach's): heading = veh_follow_heading(
 * table +0x52, pair 1, angle +0x6E, id, pair 2) into +0x4E and +0x4C, the
 * point at pair 1 (veh_deck_to_world), timers (ballista_set_physics), gBounceSound = 1,
 * ground contact (veh_ground_contact: id 10, position gBallistaPos..+8, divisors
 * 0x280 / 0x208, angle +0x4C, flags +0x96, table +0x52, records
 * +0x28/+0x40/+0x34, heights +4), place the model (ballista_build_model) and
 * veh_set_record(z, 10, x, y, block).
 * Register convention (conventions.txt): id in a3; s3 out (veh_follow_heading's
 * distance, on through veh_ground_contact's in/out slot result: *s3); the FP
 * state chains through veh_follow_heading, veh_deck_to_world and veh_ground_contact (r:
 * f24 in; f20-f26 out, f12/f14 left as mtx_build_model's cos/sin
 * temporaries, not modelled). ballista_build_model gets s0 = &+0x96 and s1 =
 * &D_803F8B50, the flags/pz pointers veh_ground_contact leaves. The asm saves and
 * restores a3, t0, t1, t2 (asm caller veh_move_riders_walk keeps a3, t0, t1 live: a
 * mixed N64 build would need a thunk) and points $gp at gBallistaState without
 * restoring it. */
void ballista_ride_follow(s32 id, s32 *s3, InterpRegs *r) {
    Regs802A8768 r8768;
    MtxChainRegs regs;
    s32 heading;

    heading = veh_follow_heading((s16 *) (gBallistaState + 0x52), *(s16 *) (gBallistaState + 0x6A), (u16 *) (gBallistaState + 0x6E),
                            id, *(s16 *) (gBallistaState + 0x6C), *(s16 *) (gBallistaState + 0x70),
                            *(s16 *) (gBallistaState + 0x72), &r8768.s3, r);
    *(s16 *) (gBallistaState + 0x4E) = heading;
    *(s16 *) (gBallistaState + 0x4C) = heading;
    veh_deck_to_world(id, *(s16 *) (gBallistaState + 0x6A), *(s16 *) (gBallistaState + 0x6C), r);
    ballista_set_physics();
    gBounceSound = 1;
    veh_ground_contact(gBallistaState, 10, (s32 *) &gBallistaPos[0], (s32 *) &gBallistaPos[1], (s32 *) &gBallistaPos[2], r->t3,
                  r->t4, 0x280, 0x208, (s16 *) (gBallistaState + 0x4C), gBallistaState + 0x96, (s16 *) (gBallistaState + 0x52),
                  (s32 *) (gBallistaState + 0x28), (s32 *) (gBallistaState + 0x40), (s32 *) (gBallistaState + 0x34),
                  (s32 *) (gBallistaState + 4), &r8768, (TriSideOut *) &r->f12);
    *s3 = r8768.s3;
    regs.s0 = (s32) (gBallistaState + 0x96);
    regs.s1 = (s32) &gBallistaPos[2];
    ballista_build_model(&regs);
    veh_set_record(gBallistaPos[2], 10, gBallistaPos[0], gBallistaPos[1], gBallistaState);
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/853D0/ballista_ride_follow.s")
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
extern u64 * N64P gBallistaMtxA;
extern u64 * N64P gBallistaMtxB;
extern u8 gBallistaThrottleHold;  /* throttle hold-off countdown */
extern s8 gBallistaEscaping;  /* "turning back to the ring" flag */
extern s16 gBallistaEscapeHeading; /* ring target heading */
extern s8 gBallistaHitSound;  /* sound to start this frame (-1 = none) */
extern u8 gBallistaBounced;  /* bounced off the ring end */
extern f32 gBallistaEscapeTurnRate;
extern u8 gBallistaNonBlockingParts[];
void ballista_update_actions(s32 t6, s32 t7, s32 s0, s32 s1, s32 s2, s32 s3, s32 s4);
s32 ballista_steer_rate(void);

#define V853_U8(off) (gBallistaState[off])
#define V853_S16(off) (*(s16 *) (gBallistaState + (off)))
#define V853_U16(off) (*(u16 *) (gBallistaState + (off)))

/* Ballista driven by the player, per frame (0x802CA4E0).
 * Per-frame update of vehicle 10 when it is the player's (veh_update_player and
 * its init ballista_spawn): zone lookup (ballista_zone_alpha), save copy
 * (veh_snapshot), effects (ballista_update_actions, unless +0x9A is set), the shared
 * veh_start_turbo (when gTurboPending), timers, steering (veh_steer at rate
 * ballista_steer_rate()), throttle (veh_throttle, held off while gBallistaThrottleHold
 * counts down), heading (veh_turn_heading), slope and drag (veh_slope /
 * veh_apply_slope), ring turn-back (veh_set_target_heading when gBallistaEscaping), the
 * forward step (veh_move_along_heading) and ground contact (veh_ground_contact), the
 * model's animation (anim_update_model on the current copy pair) and placement
 * (ballista_build_model), collision (veh_collision_setup / veh_collide_spheres /
 * veh_collide_world). Then either (no ring hit, gCollBlocked == 0) obj_collide_vehicle
 * and, if that set gCollHit, a bounce off the ring end (speed reversed
 * into 0x41..0x82 or -0x25..-0x4B, gBallistaThrottleHold = 5) unless one is already
 * running; or (ring hit) veh_prepare_escape, gBallistaEscaping = 1, and the turn back
 * toward the ring midpoint (veh_reverse_if_behind, veh_turn_into_free_arc, veh_step_heading,
 * veh_hit_slowdown(0), obj_collide_vehicle). Finally the position, speed and angles
 * go to gPlayerX.. / gPlayerSpeed.., veh_set_record(z, 10, x, y, block), and
 * sound gBallistaHitSound (12, or -1 after obj_collide_vehicle) is started if >= 0.
 * Register notes (FIDELITY, see tools_port/port_followups.md): the asm
 * hands ballista_zone_alpha (and so ballista_update_actions -> fx_spawn) the caller's
 * t6, t7, s0-s4 on the paths where veh_zone_alpha doesn't set them, and
 * veh_ground_contact the caller's f20-f26 (f12 = the constant math_cos
 * leaves, f14 its sine temporary): the C starts them at 0 (nothing reads
 * the FP ones first). ballista_build_model gets s0 = &+0x96 and s1 = &D_803F8B50
 * (what is left after veh_ground_contact; anim_update_model's own s0/s1 leftovers in
 * the game) - they only matter for a count-0 point record. veh_prepare_escape /
 * veh_collide_spheres take the block in $gp: passed explicitly (gBallistaState). The
 * asm leaves v1 and f12/f14 as its callees leave
 * them (survey: read by veh_update_player / ballista_spawn; not real uses).
 * Integer sums are trapping in the asm (game range). */
void ballista_update(void) {
    ZoneScanRegs zr;
    Out802A860C o860c;
    Regs802A8768 r8768;
    TriSideOut f;
    MtxChainRegs regs;
    s32 x;
    s32 z;
    s32 v;
    s32 mid;
    s32 cur;
    s32 turn;
    s32 tgt;

    zr.t6 = zr.t7 = zr.s0 = zr.s1 = zr.s2 = zr.s3 = zr.s4 = 0;
    ballista_zone_alpha(&zr);
    veh_snapshot(gBallistaState, gBallistaAnim, (s32 *) &gBallistaPos[0], (s32 *) &gBallistaPos[1], (s32 *) &gBallistaPos[2]);
    if ((s8) V853_U8(VS_SPAWN_UPDATE) == 0) {
        ballista_update_actions(zr.t6, zr.t7, zr.s0, zr.s1, zr.s2, zr.s3, zr.s4);
    }
    if (gTurboPending != 0) {
        veh_start_turbo(gBallistaState);
    }
    ballista_set_physics();
    veh_steer(ballista_steer_rate(), &V853_U16(VS_HEADING));
    if (gBallistaThrottleHold == 0) {
        veh_throttle(gBallistaState, &V853_S16(VS_SPEED), 3, &V853_U8(VS_SLOT_AIRBORNE), &V853_S16(VS_BANDS), 0x10);
    } else {
        gBallistaThrottleHold--;
    }
    veh_turn_heading(gBallistaState, &V853_U16(VS_MOMENTUM_HEADING), 0x3E80, &V853_S16(VS_SPEED), &V853_U16(VS_HEADING), &V853_U16(VS_MOVE_HEADING),
                  (s8 *) &V853_U8(VS_SKIDDING), 1);
    f.pz = veh_slope(&V853_S16(VS_SPEED), &V853_U8(VS_SLOT_AIRBORNE), (s32 *) (gBallistaState + 4), (f32 *) (gBallistaState + 0));
    veh_apply_slope(gBallistaState, &V853_S16(VS_SPEED), 10, (s8 *) &V853_U8(VS_SLOT_AIRBORNE), (s32 *) (gBallistaState + 4), 1, 640.0f);
    if (gBallistaEscaping != 0) {
        veh_set_target_heading(gBallistaState, &gBallistaEscapeHeading);
    }
    x = veh_move_along_heading(f.pz, V853_U16(VS_MOVE_HEADING), &V853_S16(VS_SPEED), (s32 *) &gBallistaPos[0], (s32 *) &gBallistaPos[2],
                      &o860c);
    z = o860c.t1;
    gBounceSound = 1;
    f.pz = f.cross = f.cz = f.side = f.sideZ = f.dz = 0.0f;
    r8768.s3 = o860c.s3;
    veh_ground_contact(gBallistaState, 10, (s32 *) &gBallistaPos[0], (s32 *) &gBallistaPos[1], (s32 *) &gBallistaPos[2], x, z,
                  0x280, 0x208, &V853_S16(VS_HEADING), &V853_U8(VS_SLOT_AIRBORNE), &V853_S16(VS_WHEEL_OFFSETS), (s32 *) (gBallistaState + 0x28),
                  (s32 *) (gBallistaState + 0x40), (s32 *) (gBallistaState + 0x34), (s32 *) (gBallistaState + 4), &r8768, &f);
    if (gBufferIndex != 0) {
        anim_update_model((u8 *) gBallistaMtxA, (u8 *) gBallistaMtxB, (AnimChannel *) gBallistaAnim);
    } else {
        anim_update_model((u8 *) gBallistaMtxB, (u8 *) gBallistaMtxA, (AnimChannel *) gBallistaAnim);
    }
    regs.s0 = (s32) &V853_U8(VS_SLOT_AIRBORNE);
    regs.s1 = (s32) &gBallistaPos[2];
    ballista_build_model(&regs);
    veh_collision_setup(gBallistaPos[2], (s32) gBallistaNonBlockingParts, 1, 1, gBallistaPos[0], gBallistaPos[1], 8, V853_S16(VS_SPEED), 0x64, 0, 10,
                  gBallistaState);
    veh_collide_spheres(10, gBallistaState);
    veh_collide_world(10);
    gBallistaHitSound = 12;
    if (gCollBlocked == 0) {
        gCollHit = 0;
        gCollVehicleAnim = gBallistaAnim;
        obj_collide_vehicle(10, gBallistaState);
        gBallistaHitSound = -1;
        if (gCollHit == 0) {
            gBallistaEscaping = 0;
            gBallistaBounced = 0;
        } else {
            gBallistaEscaping = 0;
            if (gBallistaBounced == 0 && gBallistaThrottleHold == 0) {
                /* bounce off the ring end */
                gBallistaThrottleHold = 5;
                v = V853_S16(VS_SPEED);
                if (v >= 0) {
                    if (v < 0x41) {
                        v = 0x41;
                    } else if (!(v < 0x83)) {
                        v = 0x82;
                    }
                } else if (!(v < -0x24)) {
                    v = -0x25;
                } else if (v < -0x4B) {
                    v = -0x4B;
                }
                V853_S16(VS_SPEED) = -v;
                gBallistaBounced = 1;
            }
        }
    } else {
        /* turn back toward the ring */
        veh_prepare_escape(gBallistaState);
        gBallistaEscaping = 1;
        mid = veh_free_arc_mid();
        v = V853_U16(VS_MOVE_HEADING) - 0x800;
        if (v < 0) {
            v += 0xFFF;
        }
        v -= mid;
        if (v < 0) {
            v = -v;
        }
        if (!(v < 0x801)) {
            v = 0xFFF - v;
        }
        veh_reverse_if_behind(gBallistaState);
        turn = veh_turn_into_free_arc(gBallistaState, V853_U16(VS_MOVE_HEADING), V853_U16(VS_HEADING), &cur, gBallistaEscapeTurnRate);
        gBallistaEscapeHeading = cur;
        V853_S16(VS_MOVE_HEADING) = cur;
        V853_S16(VS_MOMENTUM_HEADING) = cur;
        veh_step_heading(gBallistaState, turn, v, &tgt);
        veh_hit_slowdown(gBallistaState, 0);
        gCollVehicleAnim = gBallistaAnim;
        obj_collide_vehicle(10, gBallistaState);
    }
    gPlayerX = gBallistaPos[0];
    gPlayerY = gBallistaPos[1];
    gPlayerZ = gBallistaPos[2];
    gPlayerSpeed = V853_S16(VS_SPEED);
    gPlayerMoveHeading = V853_U16(VS_MOVE_HEADING);
    gPlayerHeading = V853_U16(VS_HEADING);
    veh_set_record(gBallistaPos[2], 10, gBallistaPos[0], gBallistaPos[1], gBallistaState);
    if (gBallistaHitSound >= 0) {
        snd_play(gSfxBank, gBallistaHitSound, NULL);
    }
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/853D0/ballista_update.s")
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
#ifndef CVT_W_S
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
#endif
extern f32 gBallistaLean; /* engine/tilt animation value (0.5 = centred) */
extern s32 gBallistaJumpSpeed; /* jump: a (initial speed) */
extern s32 gBallistaJumpFrame; /* jump: n (frame) */
extern s32 gBallistaJumpBaseY; /* jump: base height */
extern u16 gBallistaJumpBoosts; /* jump: boost frames */
extern u8 gBallistaDustTimer;  /* record cooldown */
extern u8 gBallistaFireCooldown;  /* horn cooldown */
extern u8 gBallistaFireSide;  /* horn side toggle */
extern f32 gBallistaLeanStepLeft;
extern f32 gBallistaLeanStepRight;
extern f32 gBallistaLeanRecentreStep;
extern f32 gBallistaJumpFullHeight;
void ballista_add_tyre_trail(void);
f32 ballista_lean_limit(s32 side);

/* a * n + round(-16 * n * n): the jump height after n frames at initial
 * speed a (32-bit products, cvt.w.s rounding; the asm's sum is a trapping
 * `add`). */
#define JUMP_HEIGHT(out, a, n)                    \
    do {                                          \
        s32 _n = (n);                             \
        s32 _q;                                   \
                                                  \
        CVT_W_S(_q, -16.0f * (f32) (_n * _n));    \
        (out) = (a) * _n + _q;                    \
    } while (0)

/* Dust, lean, missiles (L/R), wheelie, engine (0x802CAAFC).
 * Per-frame effects of vehicle type 10 ($gp = gBallistaState, read as the
 * global): with +0x99 set and no cooldown (gBallistaDustTimer), and fewer than 4
 * active gEffects records (fx_count_active), sets up a fx_spawn record
 * (def gFxExhaustSmoke, data 0x30D40, type 1 at (10, 1, 1), the caller's t6, t7,
 * s0-s4 passed through); channel 1 gets the speed sign and |speed| / 6, the
 * engine sound |speed| >> 3 (veh_set_engine_level), then the trail (ballista_add_tyre_trail).
 * gBallistaLean steers towards ballista_lean_limit's limit while gHeldLeft /
 * gHeldRight is held (else back to 0.5) and drives channel 3. The horn
 * (gHeldL/gHeldR, gBallistaMissiles presses, cooldown gBallistaFireCooldown = 5) alternates
 * channels 4 / 5 and fires shell_fire from record (10, 3|1) to (10, 4|2)
 * at speed |speed| * 320 / 260 + 640. A jump (gPadStickY >= 60, not near a
 * band bound per veh_near_top_speed) starts or boosts the parabola gBallistaJumpSpeed /
 * 68 / 6C; while it is active channel 2 gets height / gBallistaJumpFullHeight (at most
 * 1.0), and on landing it bounces (when the impact is >= 61 and no flag
 * +0x96..0x98 is 1) or ends.
 * Register convention (conventions.txt): t6, t7, s0-s4 come in (from the
 * caller's ballista_zone_alpha zone lookup) and only pass through to
 * fx_spawn; the asm changes s1-s7. Integer counters/sums use trapping
 * `addi`/`add`/`sub` in the asm: keep inputs in game range. */
void ballista_update_actions(s32 t6, s32 t7, s32 s0, s32 s1, s32 s2, s32 s3, s32 s4) {
    Io802A6274 io;
    s32 speed;
    s32 cur;
    s32 prev;
    s32 h;
    s32 x1;
    s32 y1;
    s32 z1;
    s32 *rec;
    f32 f;
    f32 lim;

    if (gBallistaDustTimer != 0) {
        gBallistaDustTimer--;
    } else if (gBallistaState[0x99] != 0) {
        gBallistaDustTimer = 1;
        if (fx_count_active() < 4) {
            io.a3 = 0;
            io.t6 = t6;
            io.s1 = s1;
            fx_spawn(&io, gFxExhaustSmoke, 0x30D40, 1, 10, 1, 1, t7, s0, s2, s3, s4, 1);
        }
    }

    speed = *(s16 *) (gBallistaState + 0x76);
    anim_set_reverse((AnimChannel *) gBallistaAnim, 1, (speed < 0) ? 1 : 0);
    if (speed < 0) {
        speed = -speed;
    }
    anim_set_speed((AnimChannel *) gBallistaAnim, 1, (u32) speed / 6);
    speed = *(s16 *) (gBallistaState + 0x76);
    if (speed < 0) {
        speed = -speed;
    }
    veh_set_engine_level((u32) speed >> 3);
    ballista_add_tyre_trail();

    f = gBallistaLean;
    if (gHeldLeft != 0) {
        f -= gBallistaLeanStepLeft;
        lim = ballista_lean_limit(0);
        if (f < lim) {
            f = lim;
        }
    } else if (gHeldRight != 0) {
        f += gBallistaLeanStepRight;
        lim = ballista_lean_limit(1);
        if (!(f <= lim)) {
            f = lim;
        }
    } else if (f < 0.5f) {
        f += gBallistaLeanRecentreStep;
        if (!(f <= 0.5f)) {
            f = 0.5f;
        }
    } else {
        f -= gBallistaLeanRecentreStep;
        if (f < 0.5f) {
            f = 0.5f;
        }
    }
    gBallistaLean = f;
    anim_set_frame(f, (AnimChannel *) gBallistaAnim, 3, 0);

    if (gBallistaFireCooldown != 0) {
        gBallistaFireCooldown--;
    } else if ((gHeldL != 0 || gHeldR != 0) && ((u16) gBallistaMissiles) != 0) {
        gBallistaMissiles--;
        snd_play(gSfxBank, SFX_BALLISTA_MISSILE, NULL);
        gBallistaFireSide ^= 1;
        if (gBallistaFireSide != 0) {
            anim_set_frame(0.0f, (AnimChannel *) gBallistaAnim, 4, 0);
            anim_play((AnimChannel *) gBallistaAnim, 4, 1);
            veh_find_point(10, 3, (u8 **) &rec);
            x1 = rec[0];
            y1 = rec[1];
            z1 = rec[2];
            veh_find_point(10, 4, (u8 **) &rec);
        } else {
            anim_set_frame(0.0f, (AnimChannel *) gBallistaAnim, 5, 0);
            anim_play((AnimChannel *) gBallistaAnim, 5, 1);
            veh_find_point(10, 1, (u8 **) &rec);
            x1 = rec[0];
            y1 = rec[1];
            z1 = rec[2];
            veh_find_point(10, 2, (u8 **) &rec);
        }
        speed = *(s16 *) (gBallistaState + 0x76);
        if (speed < 0) {
            speed = -speed;
        }
        /* the asm passes the whole word; the callee takes an s16 */
        shell_fire((u32) (speed * 0x140) / 0x104 + 0x280, rec[0], rec[1], rec[2], x1, y1, z1, 0, 0x1A4);
        gBallistaFireCooldown = 5;
    }

    if (veh_near_top_speed(gBallistaState, 10) == 0 && gPadStickY >= 0x3C) {
        if (gBallistaJumping == 0) {
            /* take off */
            gBallistaJumping = 1;
            gBallistaJumpBaseY = 0;
            gBallistaJumpFrame = 1;
            gBallistaJumpSpeed = 0x3C;
            gBallistaJumpBoosts = 0;
        } else if (gBallistaJumpBoosts < 0x17) {
            /* boost: rebase the parabola at the current height */
            gBallistaJumpBoosts++;
            JUMP_HEIGHT(cur, gBallistaJumpSpeed, gBallistaJumpFrame);
            gBallistaJumpBaseY += cur;
            JUMP_HEIGHT(prev, gBallistaJumpSpeed, gBallistaJumpFrame - 1);
            gBallistaJumpSpeed = cur - prev + 0x33;
            gBallistaJumpFrame = 1;
        }
    }

    if (gBallistaJumping != 0) {
        JUMP_HEIGHT(cur, gBallistaJumpSpeed, gBallistaJumpFrame);
        h = gBallistaJumpBaseY + cur;
        if (h <= 0) {
            /* landed: bounce with half the impact speed, or stop */
            JUMP_HEIGHT(prev, gBallistaJumpSpeed, gBallistaJumpFrame - 1);
            prev -= cur;
            if (prev < 0) {
                prev = -prev;
            }
            if (prev < 0x3D || gBallistaState[0x96] == 1 || gBallistaState[0x97] == 1 || gBallistaState[0x98] == 1) {
                goto stop;
            }
            gBallistaJumpBaseY = 0;
            gBallistaJumpSpeed = (u32) prev / 2;
            gBallistaJumpFrame = 1;
            h = 0;
        }
        f = (f32) h / gBallistaJumpFullHeight;
        if (!(f <= 1.0f)) {
            f = 1.0f;
        }
        anim_set_frame(f, (AnimChannel *) gBallistaAnim, 2, 0);
        gBallistaJumpFrame++;
        return;
    }
stop:
    gBallistaJumping = 0;
    anim_set_frame(0.0f, (AnimChannel *) gBallistaAnim, 2, 0);
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/853D0/ballista_update_actions.s")
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
extern u32 gBallistaPos[]; /* [0], [2]: trail x, z */

/* Tyre tracks while skidding (0x802CB224).
 * Trail (shape of skyfall_add_tyre_trail in 6E200; $gp = gBallistaState): if the byte at
 * +0x99 is set, +0x98 isn't 1, +0x50 < 3 and +0x9B is clear,
 * trail_add(3, y, 0, -800, 0, -800, gBallistaPos[0], gBallistaPos[2], yaw,
 * 5, 40, 0, 1): a single centred track.
 * Register note: the asm saves and restores every integer register; asm
 * caller ballista_update_actions keeps a1-a3 live (a mixed N64 build would need a
 * thunk). */
void ballista_add_tyre_trail(void) {
    if (gBallistaState[0x99] != 0 && gBallistaState[0x98] != 1 && gBallistaState[0x50] < 3 && gBallistaState[0x9B] == 0) {
        trail_add(3, *(s32 *) (gBallistaState + 0x1C), 0, -800, 0, -800, gBallistaPos[0], gBallistaPos[2],
                      *(u16 *) (gBallistaState + 0x4E), 5, 40, 0, 1);
    }
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/853D0/ballista_add_tyre_trail.s")
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
/* The lean animation's bound by speed (0x802CB3C8).
 * 0.5 +- |speed| / 280 * 0.5 (plus when side != 0, minus otherwise), speed
 * being the s16 at +0x76 of gBallistaState (the asm's $gp). The asm takes side
 * in s2, returns the value in f2 and leaves s3 = 280 (see
 * tools_port/conventions.txt). Its asm caller ballista_update_actions keeps a1-a3
 * live (a mixed N64 build would need a thunk). Same shape as backlash_lean_limit
 * (71140). */
f32 ballista_lean_limit(s32 side) {
    s32 speed = *(s16 *) (gBallistaState + 0x76);
    f32 v;

    if (speed < 0) {
        speed = -speed;
    }
    v = (f32) speed / 280.0f * 0.5f;
    if (side != 0) {
        return v + 0.5f;
    }
    return 0.5f - v;
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/853D0/ballista_lean_limit.s")
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
extern u8 * N64P gBallistaModel; /* this vehicle's model: +0 / +4 / +8 offsets of the point lists, +0x18 the matrix */

/* Ballista's model matrix, parts and points (0x802CB42C).
 * Place this vehicle's model (vehicle 10): builds its matrix (scale 0x3E80,
 * yaw = +0x4C of gBallistaState, the other two angles as they are) at the
 * model's +0x18 entry inside the current copy (gBufferIndex ? gBallistaMtxA :
 * gBallistaMtxB) with mtx_build_model, then places the parts
 * (veh_place_parts(x, y, z, 10, model + [4], model + [8], copy)) and the
 * points (veh_points_transform(10, model + [0], model + [4], copy)).
 * Register convention (conventions.txt): s0 and s1 come in for the
 * mtx_chain_transform chain, whose registers (v1, a0, a3, s1, s2) go out; regs
 * carries them. mtx_build_model leaves a3 = 0 (mtx_mul's loop counter)
 * and s2 = the matrix, which the asm then hands veh_place_parts: modelled
 * explicitly. veh_place_parts restores v1/a0, so veh_points_transform gets y and z
 * there. The asm reads the block through $gp (= gBallistaState, set by every
 * caller) and leaves t0 = 10. Its cos/sin f12/f14 temporaries, which the
 * survey lists as read by ballista_update, are not modelled (see
 * mtx_build_model). Asm caller ballista_update keeps t6, t7 live (a mixed N64
 * build would need a thunk; the native port won't). Adds are trapping in
 * the asm (pointer sums, in range). */
void ballista_build_model(MtxChainRegs *regs) {
    u8 *mdl = gBallistaModel;
    u8 *base;
    s32 *m;
    s32 x;
    s32 y;
    s32 z;

    m = (s32 *) (*(u32 *) (mdl + *(s32 *) (mdl + 0x18) + 4) +
                 (u32) (gBufferIndex != 0 ? (u8 *) gBallistaMtxA : (u8 *) gBallistaMtxB));
    gModelRot[1] = *(u16 *) (gBallistaState + 0x4C);
    mtx_build_model(gBallistaPos[0], gBallistaPos[1], gBallistaPos[2], 0x3E80, m);
    base = gBufferIndex != 0 ? (u8 *) gBallistaMtxA : (u8 *) gBallistaMtxB;
    mdl = gBallistaModel;
    x = gBallistaPos[0];
    y = gBallistaPos[1];
    z = gBallistaPos[2];
    regs->a3 = 0;
    regs->s2 = (s32) m;
    veh_place_parts(x, y, z, 10, mdl + *(s32 *) (mdl + 4), mdl + *(s32 *) (mdl + 8), base, regs);
    regs->v1 = y;
    regs->a0 = z;
    mdl = gBallistaModel;
    veh_points_transform(10, (s16 *) (mdl + *(s32 *) (mdl + 0)), (s16 *) (mdl + *(s32 *) (mdl + 4)), base, regs);
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/853D0/ballista_build_model.s")
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
#ifndef CVT_W_S
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
#endif

extern f32 gBallistaSteerDivisor;

/* Ballista's steering rate (0x802CB564).
 * Speed (s16 at +0x76 of gBallistaState, the asm's $gp) / 6.0 when any of the
 * bytes at +0x96/+0x97/+0x98 is 1, else / gBallistaSteerDivisor, rounded to nearest.
 * The asm returns it in s3 (see tools_port/conventions.txt). Its asm caller
 * ballista_update keeps a0-a3 live (a mixed N64 build would need a thunk).
 * Same shape as sideswipe_steer_rate (6B4A0). */
s32 ballista_steer_rate(void) {
    f32 div;
    s32 r;

    if (gBallistaState[0x96] == 1 || gBallistaState[0x97] == 1 || gBallistaState[0x98] == 1) {
        div = 6.0f;
    } else {
        div = gBallistaSteerDivisor;
    }
    CVT_W_S(r, *(s16 *) (gBallistaState + 0x76) / div);
    return r;
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/853D0/ballista_steer_rate.s")
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
/* Functional rewrite (tools_port/eqcheck.py verified). Same "set timers"
 * shape as carrier_set_physics (75490.c). Asm callers rely on preserved registers:
 * ballista_ride_follow on t3, t4, f12, f14; ballista_update on a0-a3 (mixed N64 build
 * would need a thunk). */

/* Ballista's gravity and bounce (0x802CB5D8). */
void ballista_set_physics(void) {
    gGravity = gLevelGravity * 4.0f;
    gBounceThreshold = 60;
    gBounceDamping = 2;
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/853D0/ballista_set_physics.s")
#endif

/* ballista_save_state: two-address trampoline into veh_serialize_state, same
 * confirmed-unreachable-from-C 8-byte sd-$ra frame as veh_check_wrap
 * (hd_code/679E0.c). Permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
extern u8 gBallistaState[];  /* this vehicle's state block: 0xA6 bytes are (de)serialized */
extern u32 gBallistaPos[]; /* plus these three words */

/* Ballista's state into a buffer (0x802CB634).
 * Serialize this vehicle's state (gBallistaState[0..0xA5] plus the three words
 * gBallistaPos[0..2]) into `dst` via veh_serialize_state. Returns the callee's result
 * (0xB2, the byte count): the asm leaves the callee's v0 untouched. */
s32 ballista_save_state(u8 *dst) {
    return veh_serialize_state(dst, gBallistaState, gBallistaPos);
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/853D0/ballista_save_state.s")
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
/* Ballista's state from a buffer (recording start) (0x802CB660).
 * Inverse of ballista_save_state: restore this vehicle's state from `src` via
 * veh_deserialize_state. Called from 17210.c as `void ballista_load_state(void *)`. */
void ballista_load_state(void *src) {
    veh_deserialize_state(src, gBallistaState, gBallistaPos);
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/853D0/ballista_load_state.s")
#endif
