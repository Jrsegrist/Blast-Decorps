#include "common.h"
#include <ultra64.h>
#include "game/game.h"
/* Views: this file reads these shared variables (game/variables.h) as other types. */
#ifdef NON_MATCHING
#define gDriverPos (*(s32 *) gDriverPos)
#endif
/* end of views */

/* FILE-WIDE FINDING: this file's functions save $ra via the 64-bit `sd`/`ld`
 * doubleword form, not the normal 32-bit `sw`/`lw` pair - the same signature
 * documented in full in hd_code/77E20.c's top-of-file note and the project
 * skill file, confirming (with no exceptions found) that this file is part
 * of the same large hand-written-assembly module. Any pragma below without
 * a more specific comment follows this convention; a few have their own
 * more specific non-ABI explanation where one was already worked out. */

#ifdef NON_MATCHING
/* Register blocks of the 62740.c helpers these vehicle functions call (the
 * asm passes them in registers; the C rewrites take pointers). */
extern u8 gAmdreamState[];  /* vehicle 8's state block (the asm's $gp) */
extern u32 gAmdreamPos[]; /* vehicle 8's position x, y, z */
extern u8 * N64P gAmdreamModel;   /* vehicle 8's model header */
extern u64 * N64P gAmdreamMtxA;  /* vehicle 8's model buffers */
extern u64 * N64P gAmdreamMtxB;
void amdream_build_model(void);
void amdream_set_physics(void);
void chopper_build_model(void);
void chopper_fly(void);
f32 chopper_update_anims(void);
void chopper_update_shadow(s32 t3, s32 fp, TriSideOut *f);
extern u8 gChopperAnim[]; /* the flying vehicle's animation channel table (AnimChannel, 56040.c) */
extern u8 gChopperState[]; /* the flying vehicle's state block */
extern u8 * N64P gChopperModel;  /* its model header */
extern u8 * N64P gChopperMtxA;  /* its model buffers */
extern u8 * N64P gChopperMtxB;
extern s32 gChopperGroundY;
extern s16 gChopperLastSoundValue;
extern s16 gChopperTurn;
extern u8 gAmdreamAnim[]; /* vehicle 8's 0x300-byte animation channel table (also saved/restored) */
extern s16 gAmdreamDeflectHeading;  /* vehicle 8's last ring-steered heading */
extern s8 gAmdreamDeflecting;   /* ring steering active */
extern u8 gAmdreamThrottleLock;   /* throttle lockout frames */
extern u8 gAmdreamNonBlockingParts[];
extern f32 gAmdreamDeflectTurnRate;
s32 amdream_zone_alpha(ZoneScanRegs *r);
void amdream_update_effects(s32 t6, s32 t7, s32 s0, s32 s1, s32 s2, s32 s3, s32 s4);
s32 amdream_steer_rate(void);

#define VEH8_S16(off) (*(s16 *) (gAmdreamState + (off)))
#define VEH8_U16(off) (*(u16 *) (gAmdreamState + (off)))

/* InterpRegs (veh_deck_to_local/veh_deck_to_world results) -> TriSideOut (the FP
 * registers veh_ground_contact and the triangle scans pass through). */
#define INTERP_TO_TRISIDE(f, r)              \
    do {                                     \
        (f).pz = (r).f12;                    \
        (f).cross = *(f32 *) &(r).f14;       \
        (f).cz = (r).f20;                    \
        (f).side = (r).f22;                  \
        (f).sideZ = (r).f24;                 \
        (f).dz = (r).f26;                    \
    } while (0)
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
extern u8 gAmdreamSmokeCooldown; /* effect cooldown */

/* Sets up the American Dream (type 8) (0x802B7340).
 * Sets up vehicle 8 (object 8) from the model header `hdr` at (x, y, z) with
 * heading `heading` (called by the object dispatcher lvl_spawn_vehicles):
 * gAmdreamModel = hdr, two 0x100-byte buffers from the heap gHeapPtr
 * (gAmdreamMtxA, gAmdreamMtxB; the cursor moves 0x200), the object record
 * (veh_add_record(8, 0, buf1, buf2, hdr)), the state block gAmdreamState reset
 * (veh_reset_state) with its wheel table +0x52..+0x68 = (+-200, 350) /
 * (+-320, 500) pairs, position gAmdreamPos..20, headings +0x4C/+0x4E/+0x74;
 * the initial ground under the wheels (veh_init_ground, key 8); the animated
 * model (anim_init_model(buf2, buf1, gAmdreamAnim, hdr)) with channel 0 started
 * and animated into each buffer in turn; the throttle bands +0x78..+0x94;
 * gAmdreamSmokeCooldown/Deflecting/ThrottleLock = 0; the parts (veh_register_parts, tag 8, scale 0x32C8);
 * marker record 8 (veh_add_shadow(8, 0x3C, 25, 25)); one frame of
 * amdream_update with byte +0x9A set (no effects); and the model matrix copied
 * from the second buffer to the first (mtx_copy_at).
 * Register convention (conventions.txt): hdr s2, x t7, y s3, z s0, heading
 * s1; veh_init_ground's fp and f12-f26 inputs are the dispatcher's leftovers
 * (here fp, f). The asm saves t0-t5 (asm caller lvl_spawn_vehicles keeps t1, t2
 * live), points $gp at gAmdreamState and leaves s0-s7, fp and the FP registers
 * as its callees leave them. Its addi traps on overflow. Fidelity note: the
 * amdream_update call inherits leftover registers in the asm (its zone scan
 * inputs t6, t7, s0-s4); the C version uses its own defaults. */
void amdream_spawn(u8 *hdr, s32 x, s32 y, s32 z, s32 heading, s32 fp, TriSideOut *f) {
    u8 *buf;
    s16 *h;

    gAmdreamModel = hdr;
    buf = gHeapPtr;
    gAmdreamMtxA = (u64 *) buf;
    gAmdreamMtxB = (u64 *) (buf + 0x100);
    gHeapPtr = buf + 0x200;
    veh_add_record(VEH_AMDREAM, 0, (s32) gAmdreamMtxA, (s32) gAmdreamMtxB, hdr);
    veh_reset_state(gAmdreamState);
    h = (s16 *) (gAmdreamState + 0x52);
    h[0] = 0xC8;
    h[1] = 0x15E;
    h[2] = -0xC8;
    h[3] = 0x15E;
    h[4] = 0xC8;
    h[5] = -0x15E;
    h[6] = 0x140;
    h[7] = 0x1F4;
    h[8] = -0x140;
    h[9] = 0x1F4;
    h[10] = 0x140;
    h[11] = -0x1F4;
    gAmdreamPos[0] = x;
    gAmdreamPos[1] = y;
    gAmdreamPos[2] = z;
    VEH8_S16(VS_HEADING) = heading;
    VEH8_S16(VS_MOVE_HEADING) = heading;
    VEH8_S16(VS_MOMENTUM_HEADING) = heading;
    veh_init_ground((s16 *) (gAmdreamState + 0x52), gAmdreamPos[1], x, z, (s32 *) (gAmdreamState + 4),
                  (s32 *) &gAmdreamPos[1], (s16 *) (gAmdreamState + 0x4C), 8, fp, gAmdreamState, f);
    anim_init_model((u32 *) gAmdreamMtxB, (u32 *) gAmdreamMtxA, (AnimChannel *) gAmdreamAnim, gAmdreamModel);
    anim_set_speed((AnimChannel *) gAmdreamAnim, 0, 0x64);
    anim_set_reverse((AnimChannel *) gAmdreamAnim, 0, 0);
    anim_set_loop_mode((AnimChannel *) gAmdreamAnim, 0, 0);
    anim_set_interp(0.0f, (AnimChannel *) gAmdreamAnim, 0, 0);
    anim_play((AnimChannel *) gAmdreamAnim, 0, 1);
    anim_update_model((u8 *) gAmdreamMtxA, (u8 *) gAmdreamMtxB, (AnimChannel *) gAmdreamAnim);
    anim_rewind(0, (AnimChannel *) gAmdreamAnim);
    anim_play((AnimChannel *) gAmdreamAnim, 0, 1);
    anim_update_model((u8 *) gAmdreamMtxB, (u8 *) gAmdreamMtxA, (AnimChannel *) gAmdreamAnim);
    h = (s16 *) (gAmdreamState + 0x78);
    h[0] = -0xB4;
    h[1] = 0;
    h[2] = 2;
    h[3] = 0;
    h[4] = 0x50;
    h[5] = 4;
    h[6] = 0x50;
    h[7] = 0xA0;
    h[8] = 5;
    h[9] = 0xA0;
    h[10] = 0xFA;
    h[11] = 3;
    h[12] = 0xFA;
    h[13] = 0x15E;
    h[14] = 2;
    gAmdreamSmokeCooldown = 0;
    gAmdreamDeflecting = 0;
    gAmdreamThrottleLock = 0;
    hdr = gAmdreamModel;
    veh_register_parts(8, hdr + *(s32 *) (hdr + 4), hdr + *(s32 *) (hdr + 8), 0x32C8);
    veh_add_shadow(VEH_AMDREAM, 0x3C, 0x19, 0x19);
    gAmdreamState[0x9A] = 1;
    amdream_update();
    gAmdreamState[0x9A] = 0;
    hdr = gAmdreamModel;
    mtx_copy_at((u8 *) gAmdreamMtxB, (u8 *) gAmdreamMtxA, *(s32 *) (hdr + *(s32 *) (hdr + 0x18) + 4));
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/72B80/amdream_spawn.s")
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
extern u8 gAmdreamState[]; /* this vehicle's state block */

/* Getting into the American Dream: engine sound 0xCE (0x802B76AC).
 * Enter vehicle type 8: clears the byte at +0x99, gCamDistance / gCamHeightBase = 3000,
 * 1000, then veh_start_engine_sound(arg0, 0xCE) (arg0 passes straight through; hd.c
 * calls this with no arguments and veh_start_engine_sound ignores it). The asm also
 * points $gp at gAmdreamState and leaves it there (conventions.txt: clobbers
 * gp); C code doesn't use $gp. Same shape as train_enter (772A0),
 * ateam_enter (88160), starski_enter (8AEE0). */
void amdream_enter(s32 arg0) {
    gAmdreamState[0x99] = 0;
    gCamDistance = 3000;
    gCamHeightBase = 1000;
    veh_start_engine_sound(arg0, 0xCE);
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/72B80/amdream_enter.s")
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
extern u8 gAmdreamState[]; /* this vehicle's state block */

/* 1 unless a wheel slot is airborne (0x802B76F8).
 * Exit check for vehicle type 8, same shape as skyfall_exit_check (6E200): returns
 * 1 when none of the bytes at +0x96, +0x97, +0x98 equals 1, else 0 (s32; the
 * C caller in hd.c declares it void and returns the leftover v0). */
s32 amdream_exit_check(void) {
    if (gAmdreamState[0x96] == 1 || gAmdreamState[0x97] == 1 || gAmdreamState[0x98] == 1) {
        return 0;
    }
    return 1;
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/72B80/amdream_exit_check.s")
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
extern u64 * N64P gAmdreamMtxA;
extern u64 * N64P gAmdreamMtxB;

/* Getting out of the American Dream (0x802B7754).
 * Vehicle-type 8 exit: zero the speed (s16 at +0x76), copy 0x100 bytes
 * between the two buffers gAmdreamMtxA/gAmdreamMtxB point at (veh_sync_buffers),
 * then veh_stop_engine_sounds. Same shape as starski_leave (8AEE0). */
void amdream_leave(void) {
    *(s16 *) (gAmdreamState + 0x76) = 0;
    veh_sync_buffers(gAmdreamMtxA, gAmdreamMtxB, 0x100);
    veh_stop_engine_sounds();
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/72B80/amdream_leave.s")
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
/* Placed American Dream on a moving level part: re-ground it (0x802B77A0).
 * Vehicle 8 while not driven (hd.c, veh_update_others): follow the ground under
 * its wheels (veh_reground at gAmdreamPos x/y/z, key 8; heights into +4,
 * middle height into D_803EEF1C), place the model (amdream_build_model) and report
 * the position (veh_set_record(z, 8, x, y, gAmdreamState)).
 * Fidelity note: the asm passes veh_reground whatever its C caller left in
 * fp (it ends up as gSlotSurface[0..2] and the byte +0x50) and in f12-f26 (the
 * triangle scans' FP pass-through); the C passes 0 and zeros. */
void amdream_reground(void) {
    TriSideOut f;
    Out802A9A60 o;

    f.pz = f.cross = f.cz = f.side = f.sideZ = f.dz = 0.0f;
    veh_reground((s16 *) (gAmdreamState + 0x52), gAmdreamPos[1], gAmdreamPos[0], gAmdreamPos[2],
                  (s32 *) (gAmdreamState + 4), (s32 *) &gAmdreamPos[1], (s16 *) (gAmdreamState + 0x4C), 8, 0,
                  gAmdreamState, &f, &o);
    amdream_build_model();
    veh_set_record(gAmdreamPos[2], 8, gAmdreamPos[0], gAmdreamPos[1], gAmdreamState);
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/72B80/amdream_reground.s")
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
extern u32 gAmdreamPos[]; /* x, y, z */
/* Veh_zone_alpha at its position (0x802B78B0).
 * Zone level lookup (veh_zone_alpha) for vehicle id 8 at its position
 * gAmdreamPos..+8; returns veh_zone_alpha's v1 (the zone list end).
 * Register convention: the asm passes veh_zone_alpha's scan registers t6, t7,
 * s0-s4 through (in and out; here through r), leaves a3 = id and clobbers
 * s5, s6 (conventions.txt). Asm caller amdream_update keeps f12 and f14 live
 * (a mixed N64 build would need a thunk; the native port won't). */
s32 amdream_zone_alpha(ZoneScanRegs *r) {
    return veh_zone_alpha(8, gAmdreamPos[0], gAmdreamPos[1], gAmdreamPos[2], r);
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/72B80/amdream_zone_alpha.s")
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
/* Records its position on the deck it stands on (0x802B78F4).
 * Value pairs on triangle `id` (veh_deck_to_local) at vehicle 8's x/z
 * (gAmdreamPos[0], [2]) -> s16s at +0x6A/+0x6C, and at that point offset by
 * entry 0 of the wheel table +0x52 rotated by the heading +0x4C
 * (veh_wheel_offset) -> +0x70/+0x72; +0x6E = the heading. The second
 * veh_deck_to_local's f24 input is the first one's f24 result.
 * Register convention (conventions.txt): id a3, f24 in; veh_deck_to_local's FP
 * results pass through (here through r). The asm points $gp at gAmdreamState
 * without restoring it, leaves s4 = +0x4C, and saves t0, t1, t3, t4 (asm
 * caller veh_attach_riders_walk keeps t0 and t1 live: a mixed N64 build would need a
 * thunk). Same shape as jbomb_ride_attach (7FB50) plus the second point. The
 * asm's adds trap on overflow. */
void amdream_ride_attach(s32 id, InterpRegs *r) {
    s32 x = gAmdreamPos[0];
    s32 z = gAmdreamPos[2];
    s32 dx;
    s32 dz;

    veh_deck_to_local(id, x, z, r);
    VEH8_S16(VS_DECK_POS) = r->t3;
    VEH8_S16(0x6C) = r->t4;
    VEH8_S16(VS_DECK_HEADING) = VEH8_U16(VS_HEADING);
    dx = veh_wheel_offset(0, (s16 *) (gAmdreamState + 0x52), (s16 *) (gAmdreamState + 0x4C), &dz);
    veh_deck_to_local(id, x + dx, z + dz, r);
    VEH8_S16(VS_DECK_FRONT) = r->t3;
    VEH8_S16(0x72) = r->t4;
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/72B80/amdream_ride_attach.s")
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
/* Moves it with the deck under it (0x802B7980).
 * Vehicle 8 driven along a path triangle `id` (dispatched by veh_move_riders_walk):
 * heading +0x4E = +0x4C = veh_follow_heading (steer from the +0x6A/+0x6C point
 * toward the +0x70/+0x72 point, heading +0x6E), then the value pair at
 * (+0x6A, +0x6C) on the triangle (veh_deck_to_world) is the new x/z for
 * veh_ground_contact (ground contact; id 8, divisors 0x2BC/0x190, slot result s3
 * from veh_follow_heading, FP state from veh_deck_to_world), after amdream_set_physics and
 * gBounceSound = 1. Then the model is placed (amdream_build_model) and the position
 * reported (veh_set_record(z, 8, x, y)).
 * Register convention (conventions.txt): id a3; f24 comes in from the
 * dispatcher (whatever the previous vehicle function left) and the f24
 * veh_ground_contact leaves goes back out (here through *f24). The asm points $gp
 * at gAmdreamState and leaves s0-s7, fp, f20-f28 as its callees leave them; it
 * saves a3, t0, t1, t2 (asm caller veh_move_riders_walk keeps a3, t0, t1 live: a
 * mixed N64 build would need a thunk). */
void amdream_ride_follow(s32 id, f32 *f24) {
    InterpRegs r;
    TriSideOut f;
    Regs802A8768 rr;
    s32 s3;
    s32 h;

    r.f24 = *f24;
    h = veh_follow_heading((s16 *) (gAmdreamState + 0x52), VEH8_S16(VS_DECK_POS), (u16 *) (gAmdreamState + 0x6E), id,
                      VEH8_S16(0x6C), VEH8_S16(VS_DECK_FRONT), VEH8_S16(0x72), &s3, &r);
    VEH8_S16(VS_MOVE_HEADING) = h;
    VEH8_S16(VS_HEADING) = h;
    veh_deck_to_world(id, VEH8_S16(VS_DECK_POS), VEH8_S16(0x6C), &r);
    amdream_set_physics();
    gBounceSound = 1;
    INTERP_TO_TRISIDE(f, r);
    rr.s3 = s3;
    veh_ground_contact(gAmdreamState, 8, (s32 *) &gAmdreamPos[0], (s32 *) &gAmdreamPos[1], (s32 *) &gAmdreamPos[2], r.t3,
                  r.t4, 0x2BC, 0x190, (s16 *) (gAmdreamState + 0x4C), gAmdreamState + 0x96, (s16 *) (gAmdreamState + 0x52),
                  (s32 *) (gAmdreamState + 0x28), (s32 *) (gAmdreamState + 0x40), (s32 *) (gAmdreamState + 0x34),
                  (s32 *) (gAmdreamState + 4), &rr, &f);
    *f24 = f.sideZ;
    amdream_build_model();
    veh_set_record(gAmdreamPos[2], 8, gAmdreamPos[0], gAmdreamPos[1], gAmdreamState);
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/72B80/amdream_ride_follow.s")
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
/* Ring-steering branch of amdream_update (gCollBlocked set): veh_prepare_escape,
 * gAmdreamDeflecting = 1, then steer along the ring: veh_reverse_if_behind, veh_turn_into_free_arc
 * (heading +0x4E toward +0x4C, scale gAmdreamDeflectTurnRate; the new index goes to
 * gAmdreamDeflectHeading, +0x4E and +0x74), veh_step_heading with that turn, veh_hit_slowdown
 * (limit 0) and the object collision pass (obj_collide_vehicle).
 * The asm also computes |(+0x4E - 0x800, wrapped) - ring midpoint| (folded
 * to <= 0x800) and then always branches past its use (a `slti 0xC8` test
 * that is never reached); the value only survives in v1 as veh_step_heading's
 * pass-through input, which is reproduced. */
static void port_b7a88_ring(void) {
    s32 mid;
    s32 d;
    s32 cur;
    s32 turn;
    s32 target;

    veh_prepare_escape(gAmdreamState);
    gAmdreamDeflecting = 1;
    mid = veh_free_arc_mid();
    d = VEH8_U16(VS_MOVE_HEADING) - 0x800;
    if (d < 0) {
        d += 0xFFF;
    }
    d -= mid;
    if (d < 0) {
        d = -d;
    }
    if (d >= 0x801) {
        d = 0xFFF - d;
    }
    veh_reverse_if_behind(gAmdreamState);
    turn = veh_turn_into_free_arc(gAmdreamState, VEH8_U16(VS_MOVE_HEADING), VEH8_U16(VS_HEADING), &cur, gAmdreamDeflectTurnRate);
    gAmdreamDeflectHeading = cur;
    VEH8_S16(VS_MOVE_HEADING) = cur;
    VEH8_S16(VS_MOMENTUM_HEADING) = cur;
    veh_step_heading(gAmdreamState, turn, d, &target);
    veh_hit_slowdown(gAmdreamState, 0);
    gCollVehicleAnim = gAmdreamAnim;
    obj_collide_vehicle(8, gAmdreamState);
}

/* Crash branch of amdream_update (the object pass reported a hit): restore
 * the state saved at the start of the frame (veh_rollback, 0x100 bytes of
 * the other model buffer back), lock the throttle for 5 frames, bounce the
 * speed to -max(|v|, 50) / 2 with the sign flipped (arithmetic >> 1), and
 * re-place the model (amdream_build_model). */
static void port_b7a88_crash(void) {
    u64 *end;
    s32 v;

    gAmdreamDeflecting = 0;
    if (gBufferIndex != 0) {
        veh_rollback(gAmdreamState, gAmdreamAnim, (s32 *) &gAmdreamPos[0], (s32 *) &gAmdreamPos[1],
                      (s32 *) &gAmdreamPos[2], gAmdreamMtxB, gAmdreamMtxA, 0x100, &end);
    } else {
        veh_rollback(gAmdreamState, gAmdreamAnim, (s32 *) &gAmdreamPos[0], (s32 *) &gAmdreamPos[1],
                      (s32 *) &gAmdreamPos[2], gAmdreamMtxA, gAmdreamMtxB, 0x100, &end);
    }
    gAmdreamThrottleLock = 5;
    v = VEH8_S16(VS_SPEED);
    if (v >= 0) {
        if (v < 0x32) {
            v = 0x32;
        }
    } else if (!(v < -0x31)) {
        v = -0x32;
    }
    VEH8_S16(VS_SPEED) = -v >> 1;
    amdream_build_model();
}

/* The American Dream driven by the player, per frame (0x802B7A88).
 * Per-frame update of vehicle 8 while driven (hd.c's veh_update_player, and
 * amdream_spawn). Zone lookup (amdream_zone_alpha), save the state for a crash
 * rollback (veh_snapshot), engine sound 0xA4 (veh_update_horn); unless byte
 * +0x9A is set, trail and effects (amdream_update_effects, fed the zone scan
 * registers); veh_start_turbo while gTurboPending is set; amdream_set_physics; steering
 * (veh_steer at the rate amdream_steer_rate returns) and, unless the throttle
 * lockout gAmdreamThrottleLock is counting down, throttle (veh_throttle: mode 3,
 * delta 0x10); heading (veh_turn_heading: rate 6000, with sound), slope
 * (veh_slope into the f32 at +0, then veh_apply_slope: kind 8, clamp, div
 * 700.0), veh_set_target_heading while gAmdreamDeflecting is set; move (veh_move_along_heading along
 * +0x4E) and ground contact (veh_ground_contact, id 8, divisors 0x2BC/0x190, after
 * gBounceSound = 1); animate (anim_update_model into the current buffer of
 * gAmdreamMtxA/B) and place the model (amdream_build_model); collision state reset
 * (veh_collision_setup: b2 = b3 = 1, b0 = 7, h1 = speed, h2 = 0x96, b8 = 8),
 * part contacts (veh_collide_spheres(8)) and world collision (veh_collide_world(8)).
 * Without a ring hit (gCollBlocked clear): gCollHit = 0 and the object pass
 * (obj_collide_vehicle) - a hit there takes the crash branch, else gAmdreamDeflecting = 0;
 * with one: the ring-steering branch. Finally the player position/speed/
 * headings gPlayerX..Z / gPlayerSpeed..40 are copied from the vehicle and
 * the position reported (veh_set_record(z, 8, x, y)).
 * Fidelity notes: the asm feeds amdream_zone_alpha its C caller's t6, t7, s0-s4
 * (scan registers, 0 here) and veh_ground_contact the FP registers left by
 * veh_move_along_heading's sine routines and earlier code (zeros here). The asm's
 * add/neg trap on overflow. */
void amdream_update(void) {
    ZoneScanRegs zr;
    Out802A860C o;
    Regs802A8768 rr;
    TriSideOut f;
    s32 x;
    f32 slope;

    zr.t6 = zr.t7 = zr.s0 = zr.s1 = zr.s2 = zr.s3 = zr.s4 = 0;
    amdream_zone_alpha(&zr);
    veh_snapshot(gAmdreamState, gAmdreamAnim, (s32 *) &gAmdreamPos[0], (s32 *) &gAmdreamPos[1],
                  (s32 *) &gAmdreamPos[2]);
    veh_update_horn(0xA4);
    if ((s8) gAmdreamState[0x9A] == 0) {
        amdream_update_effects(zr.t6, zr.t7, zr.s0, zr.s1, zr.s2, zr.s3, zr.s4);
    }
    if (gTurboPending != 0) {
        veh_start_turbo(gAmdreamState);
    }
    amdream_set_physics();
    veh_steer(amdream_steer_rate(), (u16 *) (gAmdreamState + 0x4C));
    if (gAmdreamThrottleLock == 0) {
        veh_throttle(gAmdreamState, (s16 *) (gAmdreamState + 0x76), 3, gAmdreamState + 0x96, (s16 *) (gAmdreamState + 0x78),
                      0x10);
    } else {
        gAmdreamThrottleLock--;
    }
    veh_turn_heading(gAmdreamState, (u16 *) (gAmdreamState + 0x74), 0x1770, (s16 *) (gAmdreamState + 0x76),
                  (u16 *) (gAmdreamState + 0x4C), (u16 *) (gAmdreamState + 0x4E), (s8 *) (gAmdreamState + 0x99), 1);
    slope = veh_slope((s16 *) (gAmdreamState + 0x76), gAmdreamState + 0x96, (s32 *) (gAmdreamState + 4),
                          (f32 *) gAmdreamState);
    veh_apply_slope(gAmdreamState, (s16 *) (gAmdreamState + 0x76), 8, (s8 *) (gAmdreamState + 0x96), (s32 *) (gAmdreamState + 4),
                  1, 700.0f);
    if (gAmdreamDeflecting != 0) {
        veh_set_target_heading(gAmdreamState, &gAmdreamDeflectHeading);
    }
    x = veh_move_along_heading(slope, VEH8_U16(VS_MOVE_HEADING), (s16 *) (gAmdreamState + 0x76), (s32 *) &gAmdreamPos[0],
                      (s32 *) &gAmdreamPos[2], &o);
    gBounceSound = 1;
    rr.s3 = o.s3;
    f.pz = f.cross = f.cz = f.side = f.sideZ = f.dz = 0.0f;
    veh_ground_contact(gAmdreamState, 8, (s32 *) &gAmdreamPos[0], (s32 *) &gAmdreamPos[1], (s32 *) &gAmdreamPos[2], x, o.t1,
                  0x2BC, 0x190, (s16 *) (gAmdreamState + 0x4C), gAmdreamState + 0x96, (s16 *) (gAmdreamState + 0x52),
                  (s32 *) (gAmdreamState + 0x28), (s32 *) (gAmdreamState + 0x40), (s32 *) (gAmdreamState + 0x34),
                  (s32 *) (gAmdreamState + 4), &rr, &f);
    if (gBufferIndex != 0) {
        anim_update_model((u8 *) gAmdreamMtxA, (u8 *) gAmdreamMtxB, (AnimChannel *) gAmdreamAnim);
    } else {
        anim_update_model((u8 *) gAmdreamMtxB, (u8 *) gAmdreamMtxA, (AnimChannel *) gAmdreamAnim);
    }
    amdream_build_model();
    veh_collision_setup(gAmdreamPos[2], (s32) gAmdreamNonBlockingParts, 1, 1, gAmdreamPos[0], gAmdreamPos[1], 7, VEH8_S16(VS_SPEED), 0x96, 0,
                  8, gAmdreamState);
    veh_collide_spheres(8, gAmdreamState);
    veh_collide_world(8);
    if (gCollBlocked == 0) {
        gCollHit = 0;
        gCollVehicleAnim = gAmdreamAnim;
        obj_collide_vehicle(8, gAmdreamState);
        if (gCollHit != 0) {
            port_b7a88_crash();
        } else {
            gAmdreamDeflecting = 0;
        }
    } else {
        port_b7a88_ring();
    }
    gPlayerX = gAmdreamPos[0];
    gPlayerY = gAmdreamPos[1];
    gPlayerZ = gAmdreamPos[2];
    gPlayerSpeed = VEH8_S16(VS_SPEED);
    gPlayerMoveHeading = VEH8_U16(VS_MOVE_HEADING);
    gPlayerHeading = VEH8_U16(VS_HEADING);
    veh_set_record(gAmdreamPos[2], 8, gAmdreamPos[0], gAmdreamPos[1], gAmdreamState);
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/72B80/amdream_update.s")
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
extern u8 gAmdreamSmokeCooldown;   /* effect cooldown */
void amdream_add_tyre_trail(void);

/* Tyre tracks, skid smoke, engine pitch (0x802B7F98).
 * Per-frame effects for vehicle type 8 ($gp = gAmdreamState, read as the
 * global): the tyre trail (amdream_add_tyre_trail); then, if the cooldown
 * gAmdreamSmokeCooldown is nonzero it just counts down, else when byte +0x99 is set the
 * cooldown becomes 1 and, with fewer than 15 active fx_spawn records,
 * four are set up (def gFxExhaustSmoke, type 1 at (8, 1..4, 1), data 0x29810
 * for the first two and 0x1D4C0 for the last two). Finally
 * veh_set_engine_level(|speed| >> 5) with speed the s16 at +0x76.
 * Register convention (conventions.txt): t6, t7, s0-s4 pass through to
 * fx_spawn (t6 and s1 in its in/out block, chained between the calls);
 * the asm changes s1 and s5-s7. Same shape as starski_update_effects (8AEE0). */
void amdream_update_effects(s32 t6, s32 t7, s32 s0, s32 s1, s32 s2, s32 s3, s32 s4) {
    Io802A6274 io;
    s32 v;

    amdream_add_tyre_trail();
    if (gAmdreamSmokeCooldown != 0) {
        gAmdreamSmokeCooldown--;
    } else if (gAmdreamState[0x99] != 0) {
        gAmdreamSmokeCooldown = 1;
        if (fx_count_active() < 15) {
            io.t6 = t6;
            io.s1 = s1;
            io.a3 = 1;
            fx_spawn(&io, gFxExhaustSmoke, 0x29810, 1, 8, 1, 1, t7, s0, s2, s3, s4, 1);
            io.a3 = 1;
            fx_spawn(&io, gFxExhaustSmoke, 0x29810, 1, 8, 2, 1, t7, s0, s2, s3, s4, 1);
            io.a3 = 1;
            fx_spawn(&io, gFxExhaustSmoke, 0x1D4C0, 1, 8, 3, 1, t7, s0, s2, s3, s4, 1);
            io.a3 = 1;
            fx_spawn(&io, gFxExhaustSmoke, 0x1D4C0, 1, 8, 4, 1, t7, s0, s2, s3, s4, 1);
        }
    }
    v = *(s16 *) (gAmdreamState + 0x76);
    if (v < 0) {
        v = -v;
    }
    veh_set_engine_level((u32) v >> 5);
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/72B80/amdream_update_effects.s")
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
extern u32 gAmdreamPos[]; /* [0], [2]: trail x, z */

/* Tyre tracks while skidding (0x802B80D8).
 * Tyre trail (shape of skyfall_add_tyre_trail in 6E200; $gp = gAmdreamState): if the
 * byte at +0x99 is set, +0x98 isn't 1, +0x50 < 3 and +0x9B is clear,
 * trail_add(3, y, 250, -400, -400, -400, gAmdreamPos[0], gAmdreamPos[2],
 * yaw, 3, 50, 50, 0).
 * Register note: the asm saves and restores every integer register; asm
 * caller amdream_update_effects keeps a0-a3, t6, t7 live (and reads f12/f14 after
 * the call, which the asm doesn't touch but trail_add may); a mixed N64
 * build would need a thunk. */
void amdream_add_tyre_trail(void) {
    if (gAmdreamState[0x99] != 0 && gAmdreamState[0x98] != 1 && gAmdreamState[0x50] < 3 && gAmdreamState[0x9B] == 0) {
        trail_add(3, *(s32 *) (gAmdreamState + 0x1C), 250, -400, -400, -400, gAmdreamPos[0], gAmdreamPos[2],
                      *(u16 *) (gAmdreamState + 0x4E), 3, 50, 50, 0);
    }
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/72B80/amdream_add_tyre_trail.s")
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
/* Its model matrix, parts and points (0x802B8278).
 * Places vehicle 8's model ($gp = gAmdreamState, read directly): rotation y =
 * +0x4C (x/z rotation left as they are) and position gAmdreamPos..20 at scale
 * 0x32C8 into the matrix at header word [header word 0x18 + 4] of the current
 * buffer (mtx_build_model); then its parts (veh_place_parts, tag 8, header words
 * 4/8) and points (veh_points_transform, id 8, header words 0/4), all relative to the
 * header gAmdreamModel.
 * mtx_chain_transform's register in/outs (regs; only used for an entry with no
 * matrices): s2 = the matrix (mtx_build_model leaves it there), a3/s1/s0 are
 * the asm callers' leftovers (0 here; fidelity note), and before
 * veh_points_transform v1/a0 = y/z (veh_place_parts restores them). The asm's add
 * traps on overflow. Asm caller amdream_update keeps t6, t7 live (a mixed N64
 * build would need a thunk). */
void amdream_build_model(void) {
    u8 *hdr = gAmdreamModel;
    u8 *m = *(u8 * N64P *) (hdr + *(s32 *) (hdr + 0x18) + 4);
    MtxChainRegs regs;
    u8 *base;

    m += (N64_A32) (gBufferIndex != 0 ? gAmdreamMtxA : gAmdreamMtxB);
    gModelRot[1] = VEH8_U16(VS_HEADING);
    mtx_build_model(gAmdreamPos[0], gAmdreamPos[1], gAmdreamPos[2], 0x32C8, (s32 *) m);
    base = (u8 *) (gBufferIndex != 0 ? gAmdreamMtxA : gAmdreamMtxB);
    regs.a3 = 0;
    regs.s1 = 0;
    regs.s2 = (s32) m;
    regs.s0 = 0;
    veh_place_parts(gAmdreamPos[0], gAmdreamPos[1], gAmdreamPos[2], 8, hdr + *(s32 *) (hdr + 4),
                  hdr + *(s32 *) (hdr + 8), base, &regs);
    hdr = gAmdreamModel;
    regs.v1 = gAmdreamPos[1];
    regs.a0 = gAmdreamPos[2];
    veh_points_transform(8, (s16 *) (hdr + *(s32 *) hdr), (s16 *) (hdr + *(s32 *) (hdr + 4)), base, &regs);
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/72B80/amdream_build_model.s")
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

extern f32 gAmdreamSteerRateDiv;

/* Its steering rate (by speed) (0x802B83B0).
 * Speed (s16 at +0x76 of gAmdreamState, the asm's $gp) / 11.0 when any of the
 * bytes at +0x96/+0x97/+0x98 is 1, else / gAmdreamSteerRateDiv, rounded to nearest.
 * The asm returns it in s3 (see tools_port/conventions.txt). Its asm caller
 * amdream_update keeps a0-a3 live (a mixed N64 build would need a thunk).
 * Same shape as sideswipe_steer_rate (6B4A0). */
s32 amdream_steer_rate(void) {
    f32 div;
    s32 r;

    if (gAmdreamState[0x96] == 1 || gAmdreamState[0x97] == 1 || gAmdreamState[0x98] == 1) {
        div = 11.0f;
    } else {
        div = gAmdreamSteerRateDiv;
    }
    CVT_W_S(r, *(s16 *) (gAmdreamState + 0x76) / div);
    return r;
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/72B80/amdream_steer_rate.s")
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING

/* Gravity x4, bounce 60/3 (0x802B8424).
 * Vehicle-module setup leaf (shape of driver_set_physics in 69BB0, other
 * constants): gGravity = gLevelGravity * 4, gBounceThreshold/Damping = 60, 3.
 * Register note: the asm touches only at/v0/v1/f0/f2. Its asm callers keep
 * registers live across the call (amdream_ride_follow: t3, t4, f12, f14;
 * amdream_update: a0-a3); a mixed N64 build would need a thunk preserving
 * those, the native port does not. */
void amdream_set_physics(void) {
    gGravity = gLevelGravity * 4.0f;
    gBounceThreshold = 60;
    gBounceDamping = 3;
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/72B80/amdream_set_physics.s")
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
/* Sets up the BCT chopper (type 0xFE, lvl_spawn_chopper) (0x802B8480).
 * Sets up the flying vehicle (object 0xFE) from the model header `hdr`
 * (called by the object dispatcher lvl_spawn_chopper): gChopperModel = hdr, two
 * 0x800-byte buffers from the heap gHeapPtr (gChopperMtxA, gChopperMtxB; the
 * cursor moves 0x1000), the object record (veh_add_record(0xFE, a1Val, buf1,
 * buf2, hdr)), state block gChopperState: heading +0x4C/+0x4E = 0, speed +0x76 =
 * 20, gChopperCamHeading / gChopperLastSoundValue / gChopperCamSpeed = 0; the animated model
 * (anim_init_model(buf2, buf1, gChopperAnim, hdr)) and its channels (0: 100, 0,
 * 0, 0.0f/0, started; animated into each buffer in turn with channel 0
 * reset in between; 1: 9 looping; 2, 3 looping; 4: 1, 1, 1 started), state
 * gChopperMode / gChopperTurn / gChopperTouchdown / gChopperGroundY = 0, marker record 0xFE
 * (veh_add_shadow(0xFE, 0x78, 0x2D, 0x2D)), one frame of chopper_update, and
 * the model matrix copied from the second buffer to the first
 * (mtx_copy_at at header word [header word 0x18 + 4]).
 * Register convention (conventions.txt): a1Val a1, hdr s2. The asm saves
 * t0-t5 (asm caller lvl_spawn_chopper keeps t1 live), points $gp at gChopperState
 * and leaves s0-s7, fp, f20, f30 as its callees leave them. Its addi traps on
 * overflow. Fidelity note: the asm caller's fp, f24 and f26 reach
 * chopper_update's callees as leftovers (not modelled, see chopper_update). */
void chopper_spawn(s32 a1Val, u8 *hdr) {
    u8 *buf;

    gChopperModel = hdr;
    buf = gHeapPtr;
    gChopperMtxA = buf;
    gChopperMtxB = buf + 0x800;
    gHeapPtr = buf + 0x1000;
    veh_add_record(VEH_CHOPPER, a1Val, (s32) gChopperMtxA, (s32) gChopperMtxB, hdr);
    *(s16 *) (gChopperState + 0x4C) = 0;
    *(s16 *) (gChopperState + 0x4E) = 0;
    gChopperCamHeading = 0;
    *(s16 *) (gChopperState + 0x76) = 0x14;
    gChopperLastSoundValue = 0;
    gChopperCamSpeed = 0;
    anim_init_model((u32 *) gChopperMtxB, (u32 *) gChopperMtxA, (AnimChannel *) gChopperAnim, gChopperModel);
    anim_set_speed((AnimChannel *) gChopperAnim, 0, 0x64);
    anim_set_reverse((AnimChannel *) gChopperAnim, 0, 0);
    anim_set_loop_mode((AnimChannel *) gChopperAnim, 0, 0);
    anim_set_interp(0.0f, (AnimChannel *) gChopperAnim, 0, 0);
    anim_play((AnimChannel *) gChopperAnim, 0, 1);
    anim_update_model(gChopperMtxA, gChopperMtxB, (AnimChannel *) gChopperAnim);
    anim_rewind(0, (AnimChannel *) gChopperAnim);
    anim_play((AnimChannel *) gChopperAnim, 0, 1);
    anim_update_model(gChopperMtxB, gChopperMtxA, (AnimChannel *) gChopperAnim);
    anim_set_speed((AnimChannel *) gChopperAnim, 1, 9);
    anim_set_reverse((AnimChannel *) gChopperAnim, 1, 0);
    anim_set_loop_mode((AnimChannel *) gChopperAnim, 1, 0);
    anim_play((AnimChannel *) gChopperAnim, 1, -1);
    anim_play((AnimChannel *) gChopperAnim, 2, -1);
    anim_play((AnimChannel *) gChopperAnim, 3, -1);
    anim_set_speed((AnimChannel *) gChopperAnim, 4, 1);
    anim_set_reverse((AnimChannel *) gChopperAnim, 4, 1);
    anim_set_loop_mode((AnimChannel *) gChopperAnim, 4, 1);
    anim_play((AnimChannel *) gChopperAnim, 4, 1);
    gChopperMode = 0;
    gChopperTurn = 0;
    gChopperTouchdown = 0;
    gChopperGroundY = 0;
    veh_add_shadow(VEH_CHOPPER, 0x78, 0x2D, 0x2D);
    chopper_update();
    hdr = gChopperModel;
    mtx_copy_at(gChopperMtxB, gChopperMtxA, *(s32 *) (hdr + *(s32 *) (hdr + 0x18) + 4));
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/72B80/chopper_spawn.s")
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
/* Rounded 3-D distance from (ax, ay, az) to (bx, by, bz) (62740.c; asm
 * convention in tools_port/conventions.txt: t3-t5, t6, t7, s0 -> s1). */

extern void * N64P gChopperSound;  /* this sound's handle, NULL = none */

/* Its loop sound 0x13, volume and pan by distance (0x802B8794).
 * Positional sound 0x13 at gChopperX/Y/Z (called from hd.c). d is the
 * rounded distance from the player gPlayerX/Y/Z. Beyond 16000 the sound
 * is stopped (snd_stop) and its handle cleared. Otherwise it is
 * started if it isn't playing, its volume (parameter 8) set to 0x7FFF -
 * max(d - 500, 0) and its pan (parameter 4) to 64 + (player x - source
 * x) / 32, clamped to 0..127. The asm's `sub`/`add` trap on overflow; C
 * doesn't. Register note: the asm saves and restores every register. Same
 * shape as carrier_update_sound (75490). */
void chopper_update_sound(void) {
    s32 dx = gPlayerX - gChopperX;
    s32 dist;
    s32 pan;

    dist = math_dist3(gPlayerX, gPlayerY, gPlayerZ, gChopperX, gChopperY, gChopperZ);
    if (dist > 16000) {
        if (gChopperSound != NULL) {
            snd_stop(gChopperSound);
            gChopperSound = NULL;
        }
        return;
    }
    if (gChopperSound == NULL) {
        snd_play(gSfxBank, SFX_CHOPPER_LOOP, &gChopperSound);
    }
    dist -= 500;
    if (dist < 0) {
        dist = 0;
    }
    snd_modify(gChopperSound, 8, 0x7FFF - dist);
    pan = 0x40 + (dx >> 5);
    if (pan < 0) {
        pan = 0;
    } else if (pan >= 0x80) {
        pan = 0x7F;
    }
    snd_modify(gChopperSound, 4, pan);
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/72B80/chopper_update_sound.s")
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
/* The chopper, per frame: fly, animate, place, shadow (0x802B899C).
 * Per-frame update of the flying vehicle (hd.c's hd_game_frame, and
 * chopper_spawn): state machine (chopper_fly), sound/animation channels
 * (chopper_update_anims), animate the model into the current buffer
 * (anim_update_model; gBufferIndex picks gChopperMtxA or gChopperMtxB), place it
 * (chopper_build_model), copy point record 1 of object 0xFE (veh_find_point) to the
 * marker position gChopperDropX/Y/Z, and drop the marker on the ground
 * (chopper_update_shadow).
 * chopper_update_shadow's t3 (the height when the marker is outside the level) is the
 * header pointer gChopperModel: chopper_build_model leaves it in t3 and nothing after
 * changes it - reproduced here. Fidelity note: its fp and f12-f26 inputs are
 * leftovers (fp from anim_update_model's callees, the FP registers from the C
 * caller and the callees above); the C passes 0 and zeros. The asm saves every
 * callee-saved register. */
void chopper_update(void) {
    TriSideOut f;
    u8 *rec;

    chopper_fly();
    chopper_update_anims();
    if (gBufferIndex != 0) {
        anim_update_model(gChopperMtxA, gChopperMtxB, (AnimChannel *) gChopperAnim);
    } else {
        anim_update_model(gChopperMtxB, gChopperMtxA, (AnimChannel *) gChopperAnim);
    }
    chopper_build_model();
    veh_find_point(0xFE, 1, &rec);
    gChopperDropX = ((s32 *) rec)[0];
    gChopperDropY = ((s32 *) rec)[1];
    gChopperDropZ = ((s32 *) rec)[2];
    f.pz = f.cross = f.cz = f.side = f.sideZ = f.dz = 0.0f;
    chopper_update_shadow((s32) gChopperModel, 0, &f);
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/72B80/chopper_update.s")
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
extern u8 gChopperState[]; /* this vehicle's state block */
extern s32 gChopperGroundY;

/* Copies its state out; the game mode 0x1000/0x2000 steps (0x802B8AE4).
 * Copies two halfwords of the vehicle block (+0x4E, +0x76) to gChopperCamHeading /
 * gChopperCamSpeed, and the marker position gChopperDropX..1C to gDriverPos..10 and
 * gChopperLandY. Then, in game mode 0x1000: if gChopperMode == 5 or
 * gChopperGroundY >= gChopperDropY, calls fade_to_mode(0x2000). In any other mode:
 * if gChopperMode == 4, sets the next game mode to 0x1000.
 * The game-mode compare is a full 64-bit compare in the asm (ld/beq). */
void chopper_update_mode(void) {
    gChopperCamHeading = *(s16 *) (gChopperState + 0x4E);
    gChopperCamSpeed = *(s16 *) (gChopperState + 0x76);
    gDriverPos = gChopperDropX;
    gDriverY = gChopperDropY;
    gDriverZ = gChopperDropZ;
    gChopperLandY = gChopperGroundY;
    if (gGameMode == MODE_INTRO_LANDING) {
        if (gChopperMode == 5 || !(gChopperGroundY < gChopperDropY)) {
            fade_to_mode(MODE_LEVEL_START);
        }
    } else if (gChopperMode == 4) {
        gNextGameMode = MODE_INTRO_LANDING;
    }
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/72B80/chopper_update_mode.s")
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING

/* The ground height under it, its shadow (0x802B8C18).
 * Shadow/marker 0xFE at the sound source gChopperX/Y/Z ($gp = gChopperState,
 * read as the global): when x and z are positive and inside the level
 * (gBoundsMaxX / gBoundsMaxZ << 5), the ground height under it comes from
 * ground_slot_height(slot 0, x, z, gChopperGroundY, skip 0xFE) and is stored to
 * gChopperGroundY; otherwise the height is the incoming t3 (whatever the asm
 * callers left there). Then veh_update_shadow(0xFE, x, height, z, gChopperY, 0,
 * 0, (s16) +0x4C) and gChopperShadowHeading = +0x4C.
 * Register convention (conventions.txt): t3 and fp (ground_slot_height's fp input)
 * come in from the asm callers, f12-f26 pass through ground_slot_height (here
 * through f). The asm's v1 (read by hd_game_frame via chopper_update) is just
 * veh_update_shadow's leftover; f12/f14 too after that C call. */
void chopper_update_shadow(s32 t3, s32 fp, TriSideOut *f) {
    s32 x = gChopperX;
    s32 z = gChopperZ;
    s32 y = t3;

    if (x > 0 && z > 0 && x < (gBoundsMaxX << 5) && z < (gBoundsMaxZ << 5)) {
        y = ground_slot_height(0, x, z, gChopperGroundY, 0xFE, gChopperState, fp, f);
        gChopperGroundY = y;
    }
    veh_update_shadow(0xFE, gChopperX, y, gChopperZ, gChopperY, 0, 0, *(s16 *) (gChopperState + 0x4C));
    gChopperShadowHeading = *(s16 *) (gChopperState + 0x4C);
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/72B80/chopper_update_shadow.s")
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
extern s32 gChopperBestDist; /* best distance so far */
extern s16 gChopperTurn; /* turn rate */
extern char gChopperMovingToZoomMsg[];
extern u8 gChopperAnim[]; /* animation channel table (AnimChannel, 56040.c) */
s32 chopper_target_distance(void);

#define VEH8D04_S16(off) (*(s16 *) (gChopperState + (off)))
#define VEH8D04_U16(off) (*(u16 *) (gChopperState + (off)))
#define ABS_8D04(v) ((v) < 0 ? -(v) : (v))

/* Move cur toward target by at most 0x14 (signed compares). */
static s32 port_approach_14(s32 cur, s32 target) {
    if (cur != target) {
        if (!(target < cur)) {
            cur += 0x14;
            if (target < cur) {
                cur = target;
            }
        } else {
            cur -= 0x14;
            if (cur < target) {
                cur = target;
            }
        }
    }
    return cur;
}

/* One axis of the state-3 approach: pos moves toward target by
 * (|target - pos| << 10) / dist * speed >> 10 (unsigned 32-bit arithmetic,
 * then the sign of target - pos). */
static s32 port_step_axis(s32 pos, s32 target, s32 dist, s32 speed) {
    s32 d = target - pos;
    u32 m = (u32) ABS_8D04(d) << 10;

    m = (m / (u32) dist) * (u32) speed >> 10;
    if (d < 0) {
        m = -m;
    }
    return pos + m;
}

/* State 1 (approaching the target gChopperTargetX/Z): bx/bz are the asm's
 * leftover registers from chopper_target_distance (gChopperX/Z, or gChopperRetargetZ as bx
 * after the debug warp). Returns 1 to go on to the heading update. */
static s32 port_state1(void) {
    s32 ax = gChopperTargetX;
    s32 az = gChopperTargetZ;
    s32 bx = gChopperX;
    s32 bz = gChopperZ;
    s32 dist = chopper_target_distance();
    s32 limit;
    s32 a;
    s32 rx;
    s32 rz;
    s32 d1;
    s32 d2;
    s32 t0;
    s32 t3;
    s32 t2;

    if (gGameMode == MODE_INTRO_FLY) {
        if (levelno == 0x1A || levelno == 4) {
            limit = 48000;
        } else if (levelno == 0x1D || levelno == 0x3A || levelno == 0xD) {
            limit = 70000;
        } else {
            limit = 30000;
        }
        if (!(limit < dist)) {
            gNextGameMode = MODE_INTRO_APPROACH;
            debug_printf(gChopperMovingToZoomMsg);
            gChopperTargetX = (&gChopperRetargetZ)[-1];
            bx = gChopperRetargetZ;
            gChopperTargetZ = bx;
            yoshi_demand(YOSHI_DEMAND_OFF);
        }
    }
    if (dist < 0x7D0) {
        gChopperMode = 2;
        return 1;
    }
    t0 = VEH8D04_S16(VS_SPEED) + 2;
    if (t0 >= 0xA1) {
        t0 = 0xA0;
    }
    VEH8D04_S16(VS_SPEED) = t0;
    a = veh_follow_angle(ax, az, 0, dist, bx, bz);
    if (a >= 0x401) {
        a = veh_follow_angle(ax, az, dist, 0, bx, bz) + 0x400;
        if (a >= 0x801) {
            a = veh_follow_angle(ax, az, 0, -dist, bx, bz) + 0x800;
        }
    }
    rx = math_rotate_xz_inv(0, dist, a, &rz);
    d1 = ABS_8D04(rx + bx - ax) + ABS_8D04(rz + bz - az);
    rx = math_rotate_xz_inv(0, dist, 0xFFF - a, &rz);
    d2 = ABS_8D04(rx + bx - ax) + ABS_8D04(rz + bz - az);
    if (!(d1 < d2)) {
        a = 0xFFF - a;
    }
    t0 = VEH8D04_U16(VS_MOVE_HEADING) - a;
    t3 = t0;
    if (t3 >= 0) {
        if (t3 >= 0x800) {
            t3 = 0xFFF - t3;
        }
    } else if (t3 < -0x7FF) {
        t3 += 0xFFF;
    } else {
        t3 = -t3;
    }
    t3 = (u32) t3 >> 6;
    if ((t0 > 0) ? (t0 < 0x800) : (t0 < -0x800)) {
        t2 = gChopperTurn - 1;
        if (t2 < -t3) {
            t2 = -t3;
        }
    } else {
        t2 = gChopperTurn + 1;
        if (t3 < t2) {
            t2 = t3;
        }
    }
    gChopperTurn = t2;
    return 1;
}

/* State 3 (homing in): returns 1 when the target is reached (go to state
 * 4), 0 when still closing in. */
static s32 port_state3(void) {
    s32 t = gChopperTurn;
    s32 dist;
    s32 speed;

    if (t >= 0) {
        t--;
        if (t < 0) {
            t = 0;
        }
    } else {
        t++;
        if (t > 0) {
            t = 0;
        }
    }
    gChopperTurn = t;
    dist = chopper_target_distance();
    if (dist == 0) {
        return 1;
    }
    speed = VEH8D04_S16(VS_SPEED);
    gChopperX = port_step_axis(gChopperX, gChopperTargetX, dist, speed);
    gChopperZ = port_step_axis(gChopperZ, gChopperTargetZ, dist, speed);
    dist = chopper_target_distance();
    if (dist < gChopperBestDist) {
        gChopperBestDist = dist;
        return 0;
    }
    return 1;
}

/* State 4 (landing at gChopperLandY + 0xFA0). */
static void port_state4(void) {
    s32 target = gChopperLandY + 0xFA0;
    s32 out[8];
    s32 v;
    void *h;

    if (target != gChopperY) {
        gChopperY = port_approach_14(gChopperY, target);
        return;
    }
    anim_get_state(4, (AnimChannel *) gChopperAnim, out);
    if (out[0] == 1) {
        return;
    }
    gChopperTouchdown = 1;
    if (gGameMode != MODE_INTRO_LANDING) {
        v = util_dist2d(gPlayerX, gPlayerX, gChopperX, gChopperZ);
        v = 0x88B8 - (v << 1);
        if (v >= 0xFA1) {
            if (v >= 0x8000) {
                v = 0x7FFF;
            }
            h = snd_play(gSfxBank, SFX_CHOPPER_LAND, NULL);
            snd_modify(h, 8, v);
        }
    }
    gChopperMode = 5;
    anim_play((AnimChannel *) gChopperAnim, 4, 1);
}

/* Its state machine on gChopperMode (approach, land, climb) (0x802B8D04).
 * Per-frame update of the flying vehicle gChopperState (the asm points $gp at
 * it), a state machine on gChopperMode (any other value hits the asm's
 * `syscall` debug trap and then runs as 6):
 *   6: if animation channel 4 of gChopperAnim (anim_get_state) has a nonzero
 *      float, restart it (anim_set_reverse, anim_play); state 0, then as 0.
 *   0: gChopperTurn one step toward 4; heading update.
 *   1: approach gChopperTargetX/Z (port_state1); heading update.
 *   2: slow down by 4 to 0x14 (heading update while above), then state 3
 *      with gChopperBestDist = distance, then as 3.
 *   3: gChopperTurn one step toward 0, move straight at the target
 *      (port_state3); when reached: position = target, state 4 and channel 4
 *      restarted (anim_play).
 *   4: descend (gChopperY toward gChopperLandY + 0xFA0, 0x14 a frame); there,
 *      once channel 4 is done: gChopperTouchdown = 1, a landing sound outside game
 *      mode 0x1000, state 5, channel 4 restarted.
 *   5: wait for channel 4 (gChopperLiftAnimDone = 1 when done), climb back to
 *      gChopperCruiseY, then state 0 once both.
 * Heading update: heading +0x4E (and +0x4C) += gChopperTurn (wrapped by 0xFFF)
 * and the position gChopperX/Z advanced by the speed +0x76 along it
 * (veh_move_along_heading). Finally, outside states 4/5: chopper_find_cruise_height(gLevelData)
 * and gChopperY 0x14 a frame toward gChopperCruiseY. The asm's add/sub/neg trap
 * on overflow; the state-3 divides trap on a zero distance (not reached).
 * Register convention: no inputs. The asm leaves gp = gChopperState and its
 * callees' t6, t7, s0-s4, fp, f12/f14 (listed as read by chopper_update;
 * not modelled) and clobbers s0-s4, s6, fp (conventions.txt). Unlike the
 * asm, which relies on chopper_target_distance leaving gChopperX/Z and the old
 * gChopperTargetX/Z in t3/t5/t6/s0, the C reads those values itself. */
void chopper_fly(void) {
    s32 out[8];
    s32 t;
    Out802A860C o;

    switch (gChopperMode) {
        default: /* syscall */
        case 6:
            anim_get_state(4, (AnimChannel *) gChopperAnim, out);
            if (!(*(f32 *) &out[7] == 0.0f)) {
                anim_set_reverse((AnimChannel *) gChopperAnim, 4, 1);
                anim_play((AnimChannel *) gChopperAnim, 4, 1);
            }
            gChopperMode = 0;
            /* fallthrough */
        case 0:
            t = gChopperTurn;
            if (t >= 4) {
                t--;
                if (t < 4) {
                    t = 4;
                }
            } else {
                t++;
                if (t >= 5) {
                    t = 4;
                }
            }
            gChopperTurn = t;
            goto heading;
        case 1:
            port_state1();
            goto heading;
        case 2:
            t = VEH8D04_S16(VS_SPEED);
            if (t >= 0x15) {
                VEH8D04_S16(VS_SPEED) = t - 4;
                goto heading;
            }
            VEH8D04_S16(VS_SPEED) = 0x14;
            gChopperMode = 3;
            gChopperBestDist = chopper_target_distance();
            /* fallthrough */
        case 3:
            if (port_state3()) {
                gChopperX = gChopperTargetX;
                gChopperZ = gChopperTargetZ;
                gChopperMode = 4;
                anim_play((AnimChannel *) gChopperAnim, 4, 1);
            }
            goto tail;
        case 4:
            port_state4();
            goto tail;
        case 5:
            anim_get_state(4, (AnimChannel *) gChopperAnim, out);
            if (out[0] != 1) {
                gChopperLiftAnimDone = 1;
            }
            if (gChopperCruiseY != gChopperY) {
                gChopperY = port_approach_14(gChopperY, gChopperCruiseY);
            } else if (gChopperLiftAnimDone != 0) {
                gChopperMode = 0;
                gChopperLiftAnimDone = 0;
            }
            goto tail;
    }
heading:
    t = VEH8D04_U16(VS_MOVE_HEADING) + gChopperTurn;
    if (t >= 0x1000) {
        t -= 0xFFF;
    } else if (t < 0) {
        t += 0xFFF;
    }
    VEH8D04_S16(VS_MOVE_HEADING) = t;
    VEH8D04_S16(VS_HEADING) = t;
    gChopperX = veh_move_along_heading(0.0f, t, &VEH8D04_S16(VS_SPEED), &gChopperX, &gChopperZ, &o);
    gChopperZ = o.t1;
tail:
    if (gChopperMode != 4 && gChopperMode != 5) {
        chopper_find_cruise_height(gLevelData);
        gChopperY = port_approach_14(gChopperY, gChopperCruiseY);
    }
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/72B80/chopper_fly.s")
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING

/* X/z distance to gChopperTargetX/Z (0x802B988C).
 * Distance (math_dist3, y = 0) from (gChopperX, gChopperZ) to
 * (gChopperTargetX, gChopperTargetZ), returned (the asm's s1; conventions.txt). The
 * asm also leaves t6 = gChopperTargetX, t7 = 0, s0 = gChopperTargetZ and
 * math_dist3's scratch in t3/t5; asm caller chopper_fly reloads those
 * before using them, and keeps a0-a3, f12, f14 live (a mixed N64 build
 * would need a thunk; the native port won't). */
s32 chopper_target_distance(void) {
    return math_dist3(gChopperX, 0, gChopperZ, gChopperTargetX, 0, gChopperTargetZ);
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/72B80/chopper_target_distance.s")
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
extern s16 gChopperLastSoundValue;  /* last value sent to the sound */
extern s16 gChopperTurn;
extern f32 gChopperPitchBase;
extern f32 gChopperPitchScale;
extern u8 gChopperAnim[]; /* animation channel table (AnimChannel, 56040.c) */

/* Engine pitch and channels 2/3 from speed and turn (0x802B98E0).
 * Sound and animation channels for vehicle gChopperState (the asm's $gp; s16 at
 * +0x76). v = 160 - that value. When v differs from gChopperLastSoundValue (which is set
 * to v) and the sound gChopperSound is playing, its parameter 0x10 is set to the
 * float gChopperPitchBase + v * gChopperPitchScale (passed as raw bits). Channel 2 of
 * gChopperAnim gets v / 320.0 (value 0) and channel 3 gets, from
 * t = gChopperTurn (with +-1 treated as 0), (32 - t) / 64.0 for t >= 0 or
 * -t / 64.0 + 0.5 for t < 0 (anim_set_frame). Returns v / 320.0 (the asm's
 * f20; conventions.txt). Asm caller chopper_update keeps a2, a3, t7, f12, f14
 * live (a mixed N64 build would need a thunk; the native port won't). */
f32 chopper_update_anims(void) {
    s32 v = 0xA0 - *(s16 *) (gChopperState + 0x76);
    s16 last = gChopperLastSoundValue;
    f32 ratio = (f32) v / 320.0f;
    s32 t;

    gChopperLastSoundValue = v;
    if (last != v && gChopperSound != NULL) {
        f32 p = gChopperPitchBase + (f32) v * gChopperPitchScale;

        snd_modify(gChopperSound, 0x10, *(s32 *) &p);
    }
    anim_set_frame(ratio, (AnimChannel *) gChopperAnim, 2, 0);
    t = gChopperTurn;
    if (t == 1 || t == -1) {
        t = 0;
    }
    if (t >= 0) {
        anim_set_frame((f32) (0x20 - t) / 64.0f, (AnimChannel *) gChopperAnim, 3, 0);
    } else {
        anim_set_frame((f32) -t / 64.0f + 0.5f, (AnimChannel *) gChopperAnim, 3, 0);
    }
    return ratio;
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/72B80/chopper_update_anims.s")
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
extern u8 * N64P gChopperModel; /* the flying vehicle's model header */
extern u8 * N64P gChopperMtxA; /* its model buffers */
extern u8 * N64P gChopperMtxB;

/* Its model matrix and points (0x802B9B4C).
 * Places the flying vehicle's model ($gp = gChopperState, read directly):
 * rotation (0, +0x4C, 0) and position gChopperX/Y/Z at scale 0x5208 into
 * the matrix at header word [header word 0x18 + 4] of the current buffer
 * (mtx_build_model), then its points (veh_points_transform, id 0xFE, header words
 * 0/4, relative to the header gChopperModel).
 * mtx_chain_transform's register in/outs (regs; only used for an entry with no
 * matrices): s2 = the matrix (mtx_build_model leaves it there); v1, a0 (as
 * mtx_build_model leaves them), a3, s1, s0 (the asm caller's leftovers) are 0
 * here (fidelity note). The asm's add traps on overflow. */
void chopper_build_model(void) {
    u8 *hdr = gChopperModel;
    u8 *m = *(u8 * N64P *) (hdr + *(s32 *) (hdr + 0x18) + 4);
    MtxChainRegs regs;

    m += (N64_A32) (gBufferIndex != 0 ? gChopperMtxA : gChopperMtxB);
    gModelRot[0] = 0;
    gModelRot[2] = 0;
    gModelRot[1] = *(u16 *) (gChopperState + 0x4C);
    mtx_build_model(gChopperX, gChopperY, gChopperZ, 0x5208, (s32 *) m);
    regs.v1 = 0;
    regs.a0 = 0;
    regs.a3 = 0;
    regs.s1 = 0;
    regs.s2 = (s32) m;
    regs.s0 = 0;
    hdr = gChopperModel;
    veh_points_transform(0xFE, (s16 *) (hdr + *(s32 *) hdr), (s16 *) (hdr + *(s32 *) (hdr + 4)),
                  gBufferIndex != 0 ? gChopperMtxA : gChopperMtxB, &regs);
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/72B80/chopper_build_model.s")
#endif
