#include "common.h"
#include <ultra64.h>
#include "game/game.h"
/* Views: this file reads these shared variables (game/variables.h) as other types. */
#ifdef NON_MATCHING
#define gHeapPtr (*(u32 *) &gHeapPtr)
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


extern u8 gPoliceState[];  /* this vehicle's state block (the asm's $gp) */
extern u8 gPoliceAnim[];  /* its channel table / save area */
extern u32 gPolicePos[]; /* x, y, z */
extern u8 * N64P gPoliceModel;   /* model header */
extern u64 * N64P gPoliceMtxA;  /* save copy pair (veh_sync_buffers) */
extern u64 * N64P gPoliceMtxB;

void police_build_model(void);
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
extern u8 gPoliceDustTimer;
extern u8 gPoliceEscaping;
extern u8 gPoliceThrottleHold;
extern u8 gPoliceLightPhase;

#define VEH13_S16(off) (*(s16 *) (gPoliceState + (off)))

/* Sets up the police car (type 13) (0x802CB720).
 * Set up vehicle 13 (the level-object dispatcher lvl_spawn_vehicles's case 0xD):
 * model header hdr -> gPoliceModel; two 0x100-byte save copies from the
 * gHeapPtr bump pointer (gPoliceMtxA, gPoliceMtxB; trapping adds);
 * veh_add_record(0xD, 0, copies, hdr); reset the record (veh_reset_state) and
 * set its wheel offsets (+0x52..+0x68), position (gPolicePos..30) and heading
 * (+0x4C, +0x4E, +0x74); ground heights for the three wheels
 * (veh_init_ground(+0x52, y, x, z, +4, &D_803F8F2C, +0x4C, 0xD, fp, block, f));
 * load the model channels (anim_init_model), set channel 0 (anim_set_speed 100,
 * 03D4/040C 0, 0480 0.0, 0290 1), run them into both copies (anim_update_model,
 * anim_rewind, anim_play); the speed bands at +0x78..+0x94; clear
 * gPoliceDustTimer..45; the bounding spheres (veh_register_parts, tag 0xD, scale
 * 0x4268); veh_add_shadow(0xD, 60, 25, 25); one update with +0x9A set
 * (police_update); and finally copy the model matrix (word +4 of the entry
 * at hdr + hdr[6]) from gPoliceMtxB to gPoliceMtxA (mtx_copy_at).
 * Register convention: hdr s2, x t7, y s3, z s0, heading s1, fp (handed to
 * veh_init_ground), FP state f12-f26 in, f22-f26 out through f
 * (conventions.txt). The asm saves t0-t5, points $gp at gPoliceState and
 * leaves it there. It also leaves fp, f20 (as anim_update_model leaves them) and
 * f12/f14 (as police_update leaves them), which its caller hands to the next
 * object's setup (veh_init_ground's fp feeds gSlotSurface / +0x50 there): not
 * modelled (see the port notes). Asm caller lvl_spawn_vehicles keeps t1, t2 live
 * (a mixed N64 build would need a thunk; the native port won't). */
void police_spawn(u8 *hdr, s32 x, s32 y, s32 z, s32 heading, s32 fp, TriSideOut *f) {
    u32 p;
    u8 *h;
    u8 *copyA;
    u8 *copyB;

    gPoliceModel = hdr;
    p = gHeapPtr;
    gPoliceMtxA = (u64 *) p;
    gPoliceMtxB = (u64 *) (p + 0x100);
    gHeapPtr = p + 0x200;
    veh_add_record(VEH_POLICE, 0, (s32) gPoliceMtxA, (s32) gPoliceMtxB, hdr);
    veh_reset_state(gPoliceState);
    VEH13_S16(VS_WHEEL_OFFSETS) = 200;
    VEH13_S16(0x54) = 350;
    VEH13_S16(0x56) = -200;
    VEH13_S16(0x58) = 350;
    VEH13_S16(0x5A) = 200;
    VEH13_S16(0x5C) = -350;
    VEH13_S16(VS_STAND_OFFSETS) = 320;
    VEH13_S16(0x60) = 500;
    VEH13_S16(0x62) = -320;
    VEH13_S16(0x64) = 500;
    VEH13_S16(0x66) = 320;
    VEH13_S16(0x68) = -500;
    gPolicePos[0] = x;
    gPolicePos[1] = y;
    gPolicePos[2] = z;
    VEH13_S16(VS_HEADING) = heading;
    VEH13_S16(VS_MOVE_HEADING) = heading;
    VEH13_S16(VS_MOMENTUM_HEADING) = heading;
    veh_init_ground((s16 *) (gPoliceState + 0x52), gPolicePos[1], x, z, (s32 *) (gPoliceState + 4),
                  (s32 *) &gPolicePos[1], (s16 *) (gPoliceState + 0x4C), 0xD, fp, gPoliceState, f);
    copyA = (u8 *) gPoliceMtxA;
    copyB = (u8 *) gPoliceMtxB;
    anim_init_model((u32 *) copyB, (u32 *) copyA, (AnimChannel *) gPoliceAnim, gPoliceModel);
    anim_set_speed((AnimChannel *) gPoliceAnim, 0, 100);
    anim_set_reverse((AnimChannel *) gPoliceAnim, 0, 0);
    anim_set_loop_mode((AnimChannel *) gPoliceAnim, 0, 0);
    anim_set_interp(0.0f, (AnimChannel *) gPoliceAnim, 0, 0);
    anim_play((AnimChannel *) gPoliceAnim, 0, 1);
    anim_update_model(copyA, copyB, (AnimChannel *) gPoliceAnim);
    anim_rewind(0, (AnimChannel *) gPoliceAnim);
    anim_play((AnimChannel *) gPoliceAnim, 0, 1);
    anim_update_model(copyB, copyA, (AnimChannel *) gPoliceAnim);
    VEH13_S16(VS_BANDS) = -180;
    VEH13_S16(0x7A) = 0;
    VEH13_S16(0x7C) = 2;
    VEH13_S16(0x7E) = 0;
    VEH13_S16(0x80) = 80;
    VEH13_S16(0x82) = 1;
    VEH13_S16(0x84) = 80;
    VEH13_S16(0x86) = 160;
    VEH13_S16(0x88) = 2;
    VEH13_S16(0x8A) = 160;
    VEH13_S16(0x8C) = 180;
    VEH13_S16(0x8E) = 3;
    VEH13_S16(0x90) = 180;
    VEH13_S16(0x92) = 340;
    VEH13_S16(0x94) = 2;
    gPoliceDustTimer = 0;
    gPoliceEscaping = 0;
    gPoliceThrottleHold = 0;
    gPoliceLightPhase = 0;
    h = gPoliceModel;
    veh_register_parts(0xD, h + *(s32 *) (h + 4), h + *(s32 *) (h + 8), 0x4268);
    veh_add_shadow(VEH_POLICE, 60, 25, 25);
    gPoliceState[0x9A] = 1;
    police_update();
    gPoliceState[0x9A] = 0;
    h = gPoliceModel;
    h += *(s32 *) (h + 0x18);
    mtx_copy_at((u8 *) gPoliceMtxB, (u8 *) gPoliceMtxA, *(s32 *) (h + 4));
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/86F60/police_spawn.s")
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
extern u8 gPoliceState[]; /* this vehicle's state block */
extern u8 gTexAnimPoliceLightsA[]; /* channel keys (7D9D0 text blob) */
extern u8 gTexAnimPoliceLightsB[];

/* Getting into the police car (0x802CBA94).
 * Enter vehicle type 13: clears the byte at +0x99, gCamDistance / gCamHeightBase = 3000,
 * 1000, resets the two channels keyed gTexAnimPoliceLightsA and gTexAnimPoliceLightsB (unk14,
 * unk11, unk12 = 0, then anim_tex_play with -1), then veh_start_engine_sound(arg0,
 * 0xCE) (arg0 passes straight through; hd.c calls this with no arguments
 * and veh_start_engine_sound ignores it). The asm also points $gp at gPoliceState and
 * leaves it there (conventions.txt: clobbers gp), and leaves v1 = -1; C code
 * uses neither. */
void police_enter(s32 arg0) {
    gPoliceState[0x99] = 0;
    gCamDistance = 3000;
    gCamHeightBase = 1000;
    anim_tex_set_speed((s32) gTexAnimPoliceLightsA, 0);
    anim_tex_set_reverse((s32) gTexAnimPoliceLightsA, 0);
    anim_tex_set_loop_mode((s32) gTexAnimPoliceLightsA, 0);
    anim_tex_play((s32) gTexAnimPoliceLightsA, -1);
    anim_tex_set_speed((s32) gTexAnimPoliceLightsB, 0);
    anim_tex_set_reverse((s32) gTexAnimPoliceLightsB, 0);
    anim_tex_set_loop_mode((s32) gTexAnimPoliceLightsB, 0);
    anim_tex_play((s32) gTexAnimPoliceLightsB, -1);
    veh_start_engine_sound(arg0, 0xCE);
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/86F60/police_enter.s")
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
/* Functional rewrite (tools_port/eqcheck.py verified). Vehicle-type 13
 * "can exit" check: 0 if any of the vehicle's bytes 0x96..0x98 equals 1,
 * else 1 (shape shared with ballista_exit_check in 853D0.c). 00000.c declares it
 * void and ignores the result, but the asm returns 0/1 in v0. */
extern u8 gPoliceState[];

/* 1 unless a wheel slot is airborne (0x802CBB60). */
s32 police_exit_check(void) {
    if (gPoliceState[0x96] == 1 || gPoliceState[0x97] == 1 || gPoliceState[0x98] == 1) {
        return 0;
    }
    return 1;
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/86F60/police_exit_check.s")
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
extern u8 gPoliceState[];
extern u64 * N64P gPoliceMtxA; /* save copy pair (veh_sync_buffers) */
extern u64 * N64P gPoliceMtxB;

/* Getting out of the police car (0x802CBBBC).
 * Leave vehicle type 13 (called from hd.c): clears the speed (s16 at +0x76),
 * veh_sync_buffers(gPoliceMtxA, gPoliceMtxB, 0x100), then stops the looping
 * sounds (veh_stop_engine_sounds). The asm points $gp at gPoliceState around the calls
 * and restores it. */
void police_leave(void) {
    *(s16 *) (gPoliceState + 0x76) = 0;
    veh_sync_buffers(gPoliceMtxA, gPoliceMtxB, 0x100);
    veh_stop_engine_sounds();
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/86F60/police_leave.s")
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING

/* Placed police car on a moving level part: re-ground it (0x802CBC08).
 * Re-seat vehicle 13 on the ground (hd.c calls it): wheel heights through
 * veh_reground(+0x52, y, x, z, +4, &D_803F8F2C, +0x4C, 0xD, fp, block, f),
 * then the model placement (police_build_model) and veh_set_record(z, 0xD, x, y,
 * block).
 * Register notes: the asm hands veh_reground its caller's fp and FP
 * registers; fp ends up in gSlotSurface[0..2] and the block's +0x50 (ground
 * class average), so from hd.c's IDO caller that is whatever $s8 held. The C
 * passes fp = 0 and zeroed FP state (see the port notes: fidelity). The asm
 * saves every callee-saved register; it leaves v1 (survey: "read" by hd.c,
 * which can't). */
void police_reground(void) {
    TriSideOut f;
    Out802A9A60 out;

    f.pz = 0.0f;
    f.cross = 0.0f;
    f.cz = 0.0f;
    f.side = 0.0f;
    f.sideZ = 0.0f;
    f.dz = 0.0f;
    veh_reground((s16 *) (gPoliceState + 0x52), gPolicePos[1], gPolicePos[0], gPolicePos[2],
                  (s32 *) (gPoliceState + 4), (s32 *) &gPolicePos[1], (s16 *) (gPoliceState + 0x4C), 0xD, 0,
                  gPoliceState, &f, &out);
    police_build_model();
    veh_set_record(gPolicePos[2], 0xD, gPolicePos[0], gPolicePos[1], gPoliceState);
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/86F60/police_reground.s")
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
extern u32 gPolicePos[]; /* x, y, z */
/* Veh_zone_alpha at its position (0x802CBD18).
 * Zone level lookup (veh_zone_alpha) for vehicle id 0xD at its position
 * gPolicePos..+8; returns veh_zone_alpha's v1 (the zone list end).
 * Register convention: the asm passes veh_zone_alpha's scan registers t6, t7,
 * s0-s4 through (in and out; here through r), leaves a3 = id and clobbers
 * s5, s6 (conventions.txt). Asm caller police_update keeps f12 and f14 live
 * (a mixed N64 build would need a thunk; the native port won't). */
s32 police_zone_alpha(ZoneScanRegs *r) {
    return veh_zone_alpha(0xD, gPolicePos[0], gPolicePos[1], gPolicePos[2], r);
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/86F60/police_zone_alpha.s")
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING

/* Records its position on the deck it stands on (0x802CBD5C).
 * Value pairs on the gMoveableTris triangle `id` (veh_deck_to_local) at this
 * vehicle's x/z (gPolicePos[0], [2]) -> s16s at +0x6A/+0x6C, and at that
 * point plus the offset tbl[0] (+0x52) rotated by the heading (veh_wheel_offset,
 * angle at +0x4C) -> +0x70/+0x72; the heading is copied to +0x6E. Returns
 * &gPoliceState[0x52] (the asm's v1). The offset is added with trapping adds.
 * Register convention: id in a3; veh_deck_to_local's FP results (f12-f26, f24
 * also in) pass through to the caller, here through r (conventions.txt). The
 * asm saves and restores t0, t1, t3, t4 (asm caller veh_attach_riders_walk keeps t0
 * and t1 live: a mixed N64 build would need a thunk), points $gp at
 * gPoliceState and leaves s4 = $gp + 0x4C. */
s32 police_ride_attach(s32 id, InterpRegs *r) {
    s32 x = gPolicePos[0];
    s32 z = gPolicePos[2];
    s32 dx;
    s32 dz;

    veh_deck_to_local(id, x, z, r);
    *(s16 *) (gPoliceState + 0x6A) = r->t3;
    *(s16 *) (gPoliceState + 0x6C) = r->t4;
    *(u16 *) (gPoliceState + 0x6E) = *(u16 *) (gPoliceState + 0x4C);
    dx = veh_wheel_offset(0, (s16 *) (gPoliceState + 0x52), (s16 *) (gPoliceState + 0x4C), &dz);
    veh_deck_to_local(id, x + dx, z + dz, r);
    *(s16 *) (gPoliceState + 0x70) = r->t3;
    *(s16 *) (gPoliceState + 0x72) = r->t4;
    return (s32) (gPoliceState + 0x52);
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/86F60/police_ride_attach.s")
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
void police_set_physics(void);

/* Moves it with the deck under it (0x802CBDE8).
 * Steer vehicle 13 along triangle `id` (the asm caller veh_move_riders_walk): the
 * heading toward the two value pairs stored at +0x6A..+0x72 (veh_follow_heading,
 * tbl +0x52, angle +0x6E) goes to +0x4C and +0x4E; the value pair at the
 * first point (veh_deck_to_world) is the new x/z for the ground contact
 * (veh_ground_contact: id 0xD, divisors 700/400, its s3 input = veh_follow_heading's
 * distance, FP state from veh_deck_to_world); then police_set_physics timers,
 * gBounceSound = 1, police_build_model and veh_set_record(z, 0xD, x, y, block).
 * Register convention: id a3, f24 in; veh_ground_contact's s3 (return value)
 * and FP results (f) out (conventions.txt). The asm saves a3, t0-t2, points
 * $gp at gPoliceState and leaves it, and leaves s0-s2, s4-s7, fp as its
 * callees do. Asm caller veh_move_riders_walk keeps a3, t0, t1 live (a mixed N64
 * build would need a thunk). */
s32 police_ride_follow(s32 id, TriSideOut *f) {
    InterpRegs r;
    Regs802A8768 rr;
    s32 heading;
    s32 dist;

    r.f24 = f->sideZ;
    heading = veh_follow_heading((s16 *) (gPoliceState + 0x52), VEH13_S16(VS_DECK_POS), (u16 *) (gPoliceState + 0x6E), id,
                            VEH13_S16(0x6C), VEH13_S16(VS_DECK_FRONT), VEH13_S16(0x72), &dist, &r);
    VEH13_S16(VS_MOVE_HEADING) = heading;
    VEH13_S16(VS_HEADING) = heading;
    veh_deck_to_world(id, VEH13_S16(VS_DECK_POS), VEH13_S16(0x6C), &r);
    police_set_physics();
    gBounceSound = 1;
    f->pz = r.f12;
    *(s32 *) &f->cross = r.f14;
    f->cz = r.f20;
    f->side = r.f22;
    f->sideZ = r.f24;
    f->dz = r.f26;
    rr.s3 = dist;
    veh_ground_contact(gPoliceState, 0xD, (s32 *) &gPolicePos[0], (s32 *) &gPolicePos[1], (s32 *) &gPolicePos[2], r.t3,
                  r.t4, 700, 400, (s16 *) (gPoliceState + 0x4C), gPoliceState + 0x96, (s16 *) (gPoliceState + 0x52),
                  (s32 *) (gPoliceState + 0x28), (s32 *) (gPoliceState + 0x40), (s32 *) (gPoliceState + 0x34),
                  (s32 *) (gPoliceState + 4), &rr, f);
    police_build_model();
    veh_set_record(gPolicePos[2], 0xD, gPolicePos[0], gPolicePos[1], gPoliceState);
    return rr.s3;
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/86F60/police_ride_follow.s")
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
extern s16 gPoliceEscapeHeading; /* steering target */
extern f32 gPoliceEscapeTurnRate;
extern u8 gPoliceNonBlockingParts[];
void police_update_effects(s32 t6, s32 t7, s32 s0, s32 s1, s32 s2, s32 s3);
s32 police_steer_rate(void);

/* The police car driven by the player, per frame (0x802CBEF0).
 * Per-frame update of vehicle 13 (hd.c, and once from police_spawn):
 * zone level (police_zone_alpha); save the record (veh_snapshot); engine sound
 * 0xD (veh_update_horn); unless +0x9A is set, the effects (police_update_effects with
 * the zone scan registers); veh_start_turbo when gTurboPending; timers
 * (police_set_physics); turn rate from the speed (police_steer_rate -> veh_steer);
 * speed bands (veh_throttle, or count down gPoliceThrottleHold); acceleration and
 * heading (veh_turn_heading, veh_slope -> veh_apply_slope with 700.0, the
 * reverse target veh_set_target_heading when gPoliceEscaping); the move (veh_move_along_heading)
 * and ground contact (veh_ground_contact, gBounceSound = 1); channels
 * (anim_update_model, current copy first); model (police_build_model); collision
 * (veh_collision_setup, veh_collide_spheres, veh_collide_world). Then: if gCollBlocked is
 * clear, gCollHit = 0 and the object hits (obj_collide_vehicle); a hit
 * (gCollHit set) clears gPoliceEscaping, restores the saved record
 * (veh_rollback), sets gPoliceThrottleHold = 5, bounces the speed (-(clamped to at
 * least 50 away from 0) >> 1) and re-places the model; no hit just clears
 * gPoliceEscaping. If gCollBlocked is set: veh_prepare_escape, gPoliceEscaping = 1, then the
 * ring steering (veh_reverse_if_behind, veh_turn_into_free_arc -> gPoliceEscapeHeading / +0x4E / +0x74,
 * veh_step_heading, veh_hit_slowdown(0)) and the object hits. Finally the
 * position, speed and headings are published to gPlayerX.. and
 * veh_set_record(z, 0xD, x, y, block).
 * Register notes: the asm saves every callee-saved register. It hands
 * police_zone_alpha its caller's t6, t7, s0-s4 (kept by veh_zone_alpha when no
 * zone is looked at; they then reach police_update_effects's fx_spawn records,
 * whose type 1 ignores them) and veh_ground_contact its caller's FP registers: 0
 * here. veh_step_heading's v1 is the angle difference the asm leaves in v1 on
 * the steering path (its value is only returned, unused). It leaves f12/f14
 * and v1 as its callees do (survey: read by police_spawn / hd.c; not
 * modelled). */
void police_update(void) {
    ZoneScanRegs zr;
    TriSideOut f;
    Out802A860C o;
    Regs802A8768 rr;
    u64 *end;
    f32 scale;
    s32 x;
    s32 d;
    s32 v;
    s32 cur;
    s32 turn;
    s32 target;

    zr.t6 = 0;
    zr.t7 = 0;
    zr.s0 = 0;
    zr.s1 = 0;
    zr.s2 = 0;
    zr.s3 = 0;
    zr.s4 = 0;
    police_zone_alpha(&zr);
    veh_snapshot(gPoliceState, gPoliceAnim, (s32 *) &gPolicePos[0], (s32 *) &gPolicePos[1], (s32 *) &gPolicePos[2]);
    veh_update_horn(0xD);
    if ((s8) gPoliceState[0x9A] == 0) {
        police_update_effects(zr.t6, zr.t7, zr.s0, zr.s1, zr.s2, zr.s3);
    }
    if (gTurboPending != 0) {
        veh_start_turbo(gPoliceState);
    }
    police_set_physics();
    veh_steer(police_steer_rate(), (u16 *) (gPoliceState + 0x4C));
    if (gPoliceThrottleHold == 0) {
        veh_throttle(gPoliceState, (s16 *) (gPoliceState + 0x76), 3, gPoliceState + 0x96, (s16 *) (gPoliceState + 0x78),
                      0x14);
    } else {
        gPoliceThrottleHold--;
    }
    veh_turn_heading(gPoliceState, (u16 *) (gPoliceState + 0x74), 0x2EE0, (s16 *) (gPoliceState + 0x76),
                  (u16 *) (gPoliceState + 0x4C), (u16 *) (gPoliceState + 0x4E), (s8 *) (gPoliceState + 0x99), 1);
    scale = veh_slope((s16 *) (gPoliceState + 0x76), gPoliceState + 0x96, (s32 *) (gPoliceState + 4),
                          (f32 *) gPoliceState);
    veh_apply_slope(gPoliceState, (s16 *) (gPoliceState + 0x76), 0xD, (s8 *) (gPoliceState + 0x96),
                  (s32 *) (gPoliceState + 4), 1, 700.0f);
    if ((s8) gPoliceEscaping != 0) {
        veh_set_target_heading(gPoliceState, &gPoliceEscapeHeading);
    }
    x = veh_move_along_heading(scale, *(u16 *) (gPoliceState + 0x4E), (s16 *) (gPoliceState + 0x76), (s32 *) &gPolicePos[0],
                      (s32 *) &gPolicePos[2], &o);
    gBounceSound = 1;
    f.pz = 0.0f;
    f.cross = 0.0f;
    f.cz = 0.0f;
    f.side = 0.0f;
    f.sideZ = 0.0f;
    f.dz = 0.0f;
    rr.s3 = o.s3;
    veh_ground_contact(gPoliceState, 0xD, (s32 *) &gPolicePos[0], (s32 *) &gPolicePos[1], (s32 *) &gPolicePos[2], x, o.t1,
                  700, 400, (s16 *) (gPoliceState + 0x4C), gPoliceState + 0x96, (s16 *) (gPoliceState + 0x52),
                  (s32 *) (gPoliceState + 0x28), (s32 *) (gPoliceState + 0x40), (s32 *) (gPoliceState + 0x34),
                  (s32 *) (gPoliceState + 4), &rr, &f);
    if (gBufferIndex != 0) {
        anim_update_model((u8 *) gPoliceMtxA, (u8 *) gPoliceMtxB, (AnimChannel *) gPoliceAnim);
    } else {
        anim_update_model((u8 *) gPoliceMtxB, (u8 *) gPoliceMtxA, (AnimChannel *) gPoliceAnim);
    }
    police_build_model();
    veh_collision_setup(gPolicePos[2], (s32) gPoliceNonBlockingParts, 1, 1, gPolicePos[0], gPolicePos[1], 7, VEH13_S16(VS_SPEED), 0x96, 0,
                  0xD, gPoliceState);
    veh_collide_spheres(0xD, gPoliceState);
    veh_collide_world(0xD);
    if (gCollBlocked == 0) {
        gCollHit = 0;
        gCollVehicleAnim = gPoliceAnim;
        obj_collide_vehicle(0xD, gPoliceState);
        if (gCollHit == 0) {
            gPoliceEscaping = 0;
            goto publish;
        }
        gPoliceEscaping = 0;
        if (gBufferIndex != 0) {
            veh_rollback(gPoliceState, gPoliceAnim, (s32 *) &gPolicePos[0], (s32 *) &gPolicePos[1],
                          (s32 *) &gPolicePos[2], gPoliceMtxB, gPoliceMtxA, 0x100, &end);
        } else {
            veh_rollback(gPoliceState, gPoliceAnim, (s32 *) &gPolicePos[0], (s32 *) &gPolicePos[1],
                          (s32 *) &gPolicePos[2], gPoliceMtxA, gPoliceMtxB, 0x100, &end);
        }
        gPoliceThrottleHold = 5;
        v = VEH13_S16(VS_SPEED);
        if (v >= 0) {
            if (v < 50) {
                v = 50;
            }
        } else if (!(v < -49)) {
            v = -50;
        }
        VEH13_S16(VS_SPEED) = -v >> 1;
        police_build_model();
        goto publish;
    }
    veh_prepare_escape(gPoliceState);
    gPoliceEscaping = 1;
    d = *(u16 *) (gPoliceState + 0x4E) - 0x800;
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
    veh_reverse_if_behind(gPoliceState);
    turn = veh_turn_into_free_arc(gPoliceState, *(u16 *) (gPoliceState + 0x4E), *(u16 *) (gPoliceState + 0x4C), &cur, gPoliceEscapeTurnRate);
    gPoliceEscapeHeading = cur;
    VEH13_S16(VS_MOVE_HEADING) = cur;
    VEH13_S16(VS_MOMENTUM_HEADING) = cur;
    veh_step_heading(gPoliceState, turn, d, &target);
    veh_hit_slowdown(gPoliceState, 0);
    gCollVehicleAnim = gPoliceAnim;
    obj_collide_vehicle(0xD, gPoliceState);
publish:
    gPlayerX = gPolicePos[0];
    gPlayerY = gPolicePos[1];
    gPlayerZ = gPolicePos[2];
    gPlayerSpeed = VEH13_S16(VS_SPEED);
    gPlayerMoveHeading = *(u16 *) (gPoliceState + 0x4E);
    gPlayerHeading = *(u16 *) (gPoliceState + 0x4C);
    veh_set_record(gPolicePos[2], 0xD, gPolicePos[0], gPolicePos[1], gPoliceState);
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/86F60/police_update.s")
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
extern u8 gPoliceLightPhase;   /* animation phase 0, 2..13 */
extern u8 gPoliceDustTimer;   /* countdown */
extern u8 gTexAnimPoliceLightsA[]; /* channel keys (7D9D0 text blob) */
extern u8 gTexAnimPoliceLightsB[];
void police_add_tyre_trail(void);

/* The siren lights (L/R), tyre tracks, dust, engine (0x802CC400).
 * When gHeldL or gHeldR is set, the phase gPoliceLightPhase steps by one,
 * wrapping 14 to 2, else it is reset to 0; both channels keyed gTexAnimPoliceLightsA /
 * gTexAnimPoliceLightsB get unk13 = phase / 2 and unk4 = 0.0f (anim_tex_set_frame). Then
 * the tyre trail (police_add_tyre_trail) and the countdown gPoliceDustTimer: when nonzero
 * it just counts down; at zero, if the byte at +0x99 is set, it restarts at
 * 1 and, when fewer than 15 gEffects records are active (fx_count_active),
 * two fx_spawn records are set up (def gFxExhaustSmoke, data 0x29810, tag 1,
 * type 1 at (0xD, 1, 1) and (0xD, 2, 1)). Then always
 * veh_set_engine_level(|speed| >> 5) (speed = s16 at +0x76).
 * Register convention: the asm passes t6, t7, s0-s3 through to
 * fx_spawn (t6 and s1 in its in/out block; its s4 input is the phase /
 * 2 computed here); $gp (= gPoliceState) is read as the global. It leaves
 * fx_spawn's s1 and changes s4-s7 (conventions.txt: clobbers). */
void police_update_effects(s32 t6, s32 t7, s32 s0, s32 s1, s32 s2, s32 s3) {
    Io802A6274 io;
    s32 s4;
    s32 v;

    if (gHeldL != 0 || gHeldR != 0) {
        s4 = gPoliceLightPhase + 1;
        if (s4 == 14) {
            s4 = 2;
        }
        gPoliceLightPhase = s4;
        s4 = (u32) s4 >> 1;
    } else {
        s4 = 0;
        gPoliceLightPhase = 0;
    }
    anim_tex_set_frame(0.0f, (s32) gTexAnimPoliceLightsA, s4);
    anim_tex_set_frame(0.0f, (s32) gTexAnimPoliceLightsB, s4);
    police_add_tyre_trail();
    if (gPoliceDustTimer != 0) {
        gPoliceDustTimer--;
    } else if (gPoliceState[0x99] != 0) {
        gPoliceDustTimer = 1;
        if (fx_count_active() < 15) {
            io.a3 = 1;
            io.t6 = t6;
            io.s1 = s1;
            fx_spawn(&io, gFxExhaustSmoke, 0x29810, 1, 0xD, 1, 1, t7, s0, s2, s3, s4, 1);
            io.a3 = 1;
            fx_spawn(&io, gFxExhaustSmoke, 0x29810, 1, 0xD, 2, 1, t7, s0, s2, s3, s4, 1);
        }
    }
    v = *(s16 *) (gPoliceState + 0x76);
    if (v < 0) {
        v = -v;
    }
    veh_set_engine_level((u32) v >> 5);
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/86F60/police_update_effects.s")
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
extern u32 gPolicePos[]; /* [0], [2]: trail x, z */

/* Tyre tracks while skidding (0x802CC56C).
 * Tyre trail (shape of skyfall_add_tyre_trail in 6E200; $gp = gPoliceState): if the
 * byte at +0x99 is set, +0x98 isn't 1, +0x50 < 3 and +0x9B is clear,
 * trail_add(3, y, 250, -400, -400, -400, gPolicePos[0], gPolicePos[2],
 * yaw, 3, 50, 50, 0).
 * Register note: the asm saves and restores every integer register; asm
 * caller police_update_effects keeps a0-a3, t6, t7 live (and reads f12/f14 after
 * the call, which the asm doesn't touch but trail_add may); a mixed N64
 * build would need a thunk. */
void police_add_tyre_trail(void) {
    if (gPoliceState[0x99] != 0 && gPoliceState[0x98] != 1 && gPoliceState[0x50] < 3 && gPoliceState[0x9B] == 0) {
        trail_add(3, *(s32 *) (gPoliceState + 0x1C), 250, -400, -400, -400, gPolicePos[0], gPolicePos[2],
                      *(u16 *) (gPoliceState + 0x4E), 3, 50, 50, 0);
    }
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/86F60/police_add_tyre_trail.s")
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
extern u8 * N64P gPoliceModel;    /* model header: word offsets to the part lists */

/* Its model matrix, parts and points (0x802CC70C).
 * Place vehicle 13's model: m = the word at +4 of the header entry at
 * hdr + hdr[6], plus the current save copy (gPoliceMtxA when gBufferIndex is
 * set, else gPoliceMtxB); y rotation gModelRotY = the heading (u16 at +0x4C);
 * mtx_build_model(position gPolicePos..30, scale 0x4268, m). Then the
 * vehicle's parts (veh_place_parts, tag 0xD, list hdr + hdr[1] .. hdr +
 * hdr[2]) and points (veh_points_transform, id 0xD, hdr + hdr[0] .. hdr + hdr[1])
 * relative to the current save copy. The header offsets are added with
 * trapping adds.
 * Register notes: the asm's $gp (= gPoliceState) is read as the global. It
 * hands veh_place_parts whatever a3, s0, s1 held (they only matter for a point
 * entry with no matrices; 0 here) and s2 = m (as mtx_build_model leaves it),
 * and veh_points_transform v1 = y, a0 = z (veh_place_parts restores them). It leaves
 * veh_points_transform's s1, the save copy in s4 and mtx_build_model's f12/f14,
 * which the survey lists as read by police_update (its C rewrite doesn't).
 * Clobbers s1, s2, s4-s7 (conventions.txt); asm caller police_update keeps
 * t6, t7 live (a mixed N64 build would need a thunk). */
void police_build_model(void) {
    MtxChainRegs regs;
    u8 *hdr = gPoliceModel;
    u8 *base;
    s32 *m;

    m = (s32 *) N64_IPTR(*(s32 *) (hdr + *(s32 *) (hdr + 0x18) + 4) +
                 (s32) (gBufferIndex ? (u8 *) gPoliceMtxA : (u8 *) gPoliceMtxB));
    gModelRot[1] = *(u16 *) (gPoliceState + 0x4C);
    mtx_build_model(gPolicePos[0], gPolicePos[1], gPolicePos[2], 0x4268, m);
    base = gBufferIndex ? (u8 *) gPoliceMtxA : (u8 *) gPoliceMtxB;
    hdr = gPoliceModel;
    regs.v1 = 0;
    regs.a0 = 0;
    regs.a3 = 0;
    regs.s1 = 0;
    regs.s2 = (s32) m;
    regs.s0 = 0;
    veh_place_parts(gPolicePos[0], gPolicePos[1], gPolicePos[2], 0xD, hdr + *(s32 *) (hdr + 4),
                  hdr + *(s32 *) (hdr + 8), base, &regs);
    hdr = gPoliceModel;
    regs.v1 = gPolicePos[1];
    regs.a0 = gPolicePos[2];
    veh_points_transform(0xD, (s16 *) (hdr + *(s32 *) hdr), (s16 *) (hdr + *(s32 *) (hdr + 4)), base, &regs);
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/86F60/police_build_model.s")
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

extern f32 gPoliceSteerDivisor;

/* Its steering rate (0x802CC844).
 * Speed (s16 at +0x76 of gPoliceState, the asm's $gp) / 11.0 when any of the
 * bytes at +0x96/+0x97/+0x98 is 1, else / gPoliceSteerDivisor, rounded to nearest.
 * The asm returns it in s3 (see tools_port/conventions.txt). Its asm caller
 * police_update keeps a0-a3 live (a mixed N64 build would need a thunk).
 * Same shape as sideswipe_steer_rate (6B4A0). */
s32 police_steer_rate(void) {
    f32 div;
    s32 r;

    if (gPoliceState[0x96] == 1 || gPoliceState[0x97] == 1 || gPoliceState[0x98] == 1) {
        div = 11.0f;
    } else {
        div = gPoliceSteerDivisor;
    }
    CVT_W_S(r, *(s16 *) (gPoliceState + 0x76) / div);
    return r;
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/86F60/police_steer_rate.s")
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
/* Functional rewrite (tools_port/eqcheck.py verified). Same "set timers"
 * shape as carrier_set_physics (75490.c). Asm callers rely on preserved registers:
 * police_ride_follow on t3, t4, f12, f14; police_update on a0-a3 (mixed N64 build
 * would need a thunk). */

/* Its gravity and bounce (0x802CC8B8). */
void police_set_physics(void) {
    gGravity = gLevelGravity * 4.0f;
    gBounceThreshold = 150;
    gBounceDamping = 6;
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/86F60/police_set_physics.s")
#endif
