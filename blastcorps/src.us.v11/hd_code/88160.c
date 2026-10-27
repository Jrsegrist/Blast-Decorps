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


extern u8 gAteamState[];  /* this vehicle's state block (the asm's $gp) */
extern u8 gAteamAnim[];  /* its channel table / save area */
extern u32 gAteamPos[]; /* x, y, z */
extern u8 * N64P gAteamModel;   /* model header */
extern u64 * N64P gAteamMtxA;  /* save copy pair (veh_sync_buffers) */
extern u64 * N64P gAteamMtxB;

void ateam_build_model(void);

#define VEH14_S16(off) (*(s16 *) (gAteamState + (off)))
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
extern u8 gAteamDustTimer;
extern u8 gAteamEscaping;
extern u8 gAteamThrottleHold;

/* Sets up the A-Team van (type 14) (0x802CC920).
 * Set up vehicle 14 (the level-object dispatcher lvl_spawn_vehicles's case 0xE):
 * as police_spawn (86F60) with this vehicle's data (header gAteamModel, save
 * copies gAteamMtxA/930C from gHeapPtr, block gAteamState, channels
 * gAteamAnim, position gAteamPos..9300), id 0xE, wheel offsets
 * +-250/400 and +-320/500, its speed bands, scale 0x55F0, and only
 * gAteamDustTimer..14 cleared (there is no phase byte). One update with +0x9A set
 * (ateam_update), then the model matrix copy (mtx_copy_at).
 * Register convention as police_spawn: hdr s2, x t7, y s3, z s0, heading
 * s1, fp, FP state f12-f26 in, f22-f26 out through f (conventions.txt). The
 * asm saves t0-t5 and leaves $gp = gAteamState; fp/f20 (as anim_update_model
 * leaves them) and f12/f14 (as ateam_update leaves them), which its caller
 * hands to the next object's setup, aren't modelled. Asm caller
 * lvl_spawn_vehicles keeps t1, t2 live (a mixed N64 build would need a thunk). */
void ateam_spawn(u8 *hdr, s32 x, s32 y, s32 z, s32 heading, s32 fp, TriSideOut *f) {
    u32 p;
    u8 *h;
    u8 *copyA;
    u8 *copyB;

    gAteamModel = hdr;
    p = gHeapPtr;
    gAteamMtxA = (u64 *) p;
    gAteamMtxB = (u64 *) (p + 0x100);
    gHeapPtr = p + 0x200;
    veh_add_record(VEH_ATEAM, 0, (s32) gAteamMtxA, (s32) gAteamMtxB, hdr);
    veh_reset_state(gAteamState);
    VEH14_S16(VS_WHEEL_OFFSETS) = 250;
    VEH14_S16(0x54) = 400;
    VEH14_S16(0x56) = -250;
    VEH14_S16(0x58) = 400;
    VEH14_S16(0x5A) = 250;
    VEH14_S16(0x5C) = -400;
    VEH14_S16(VS_STAND_OFFSETS) = 320;
    VEH14_S16(0x60) = 500;
    VEH14_S16(0x62) = -320;
    VEH14_S16(0x64) = 500;
    VEH14_S16(0x66) = 320;
    VEH14_S16(0x68) = -500;
    gAteamPos[0] = x;
    gAteamPos[1] = y;
    gAteamPos[2] = z;
    VEH14_S16(VS_HEADING) = heading;
    VEH14_S16(VS_MOVE_HEADING) = heading;
    VEH14_S16(VS_MOMENTUM_HEADING) = heading;
    veh_init_ground((s16 *) (gAteamState + 0x52), gAteamPos[1], x, z, (s32 *) (gAteamState + 4),
                  (s32 *) &gAteamPos[1], (s16 *) (gAteamState + 0x4C), 0xE, fp, gAteamState, f);
    copyA = (u8 *) gAteamMtxA;
    copyB = (u8 *) gAteamMtxB;
    anim_init_model((u32 *) copyB, (u32 *) copyA, (AnimChannel *) gAteamAnim, gAteamModel);
    anim_set_speed((AnimChannel *) gAteamAnim, 0, 100);
    anim_set_reverse((AnimChannel *) gAteamAnim, 0, 0);
    anim_set_loop_mode((AnimChannel *) gAteamAnim, 0, 0);
    anim_set_interp(0.0f, (AnimChannel *) gAteamAnim, 0, 0);
    anim_play((AnimChannel *) gAteamAnim, 0, 1);
    anim_update_model(copyA, copyB, (AnimChannel *) gAteamAnim);
    anim_rewind(0, (AnimChannel *) gAteamAnim);
    anim_play((AnimChannel *) gAteamAnim, 0, 1);
    anim_update_model(copyB, copyA, (AnimChannel *) gAteamAnim);
    VEH14_S16(VS_BANDS) = -180;
    VEH14_S16(0x7A) = 0;
    VEH14_S16(0x7C) = 8;
    VEH14_S16(0x7E) = 0;
    VEH14_S16(0x80) = 80;
    VEH14_S16(0x82) = 8;
    VEH14_S16(0x84) = 80;
    VEH14_S16(0x86) = 160;
    VEH14_S16(0x88) = 8;
    VEH14_S16(0x8A) = 160;
    VEH14_S16(0x8C) = 250;
    VEH14_S16(0x8E) = 8;
    VEH14_S16(0x90) = 250;
    VEH14_S16(0x92) = 330;
    VEH14_S16(0x94) = 4;
    gAteamDustTimer = 0;
    gAteamEscaping = 0;
    gAteamThrottleHold = 0;
    h = gAteamModel;
    veh_register_parts(0xE, h + *(s32 *) (h + 4), h + *(s32 *) (h + 8), 0x55F0);
    veh_add_shadow(VEH_ATEAM, 60, 25, 25);
    gAteamState[0x9A] = 1;
    ateam_update();
    gAteamState[0x9A] = 0;
    h = gAteamModel;
    h += *(s32 *) (h + 0x18);
    mtx_copy_at((u8 *) gAteamMtxB, (u8 *) gAteamMtxA, *(s32 *) (h + 4));
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/88160/ateam_spawn.s")
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
extern u8 gAteamState[]; /* this vehicle's state block */

/* Getting into the van (0x802CCC8C).
 * Enter vehicle type 14: clears the byte at +0x99, gCamDistance / gCamHeightBase = 3000,
 * 1000, then veh_start_engine_sound(arg0, 0x7B) (arg0 passes straight through; hd.c
 * calls this with no arguments and veh_start_engine_sound ignores it). The asm also
 * points $gp at gAteamState and leaves it there (conventions.txt: clobbers
 * gp); C code doesn't use $gp. Same shape as amdream_enter (72B80). */
void ateam_enter(s32 arg0) {
    gAteamState[0x99] = 0;
    gCamDistance = 3000;
    gCamHeightBase = 1000;
    veh_start_engine_sound(arg0, 0x7B);
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/88160/ateam_enter.s")
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
/* Functional rewrite (tools_port/eqcheck.py verified). Vehicle-type 14
 * "can exit" check: 0 if any of the vehicle's bytes 0x96..0x98 equals 1,
 * else 1 (shape shared with ballista_exit_check in 853D0.c). 00000.c declares it
 * void and ignores the result, but the asm returns 0/1 in v0. */
extern u8 gAteamState[];

/* 1 unless a wheel slot is airborne (0x802CCCD8). */
s32 ateam_exit_check(void) {
    if (gAteamState[0x96] == 1 || gAteamState[0x97] == 1 || gAteamState[0x98] == 1) {
        return 0;
    }
    return 1;
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/88160/ateam_exit_check.s")
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
extern u8 gAteamState[];
extern u64 * N64P gAteamMtxA; /* save copy pair (veh_sync_buffers) */
extern u64 * N64P gAteamMtxB;

/* Getting out of the van (0x802CCD34).
 * Leave vehicle type 14 (called from hd.c): clears the speed (s16 at +0x76),
 * veh_sync_buffers(gAteamMtxA, gAteamMtxB, 0x100), then stops the looping
 * sounds (veh_stop_engine_sounds). The asm points $gp at gAteamState around the calls
 * and restores it. */
void ateam_leave(void) {
    *(s16 *) (gAteamState + 0x76) = 0;
    veh_sync_buffers(gAteamMtxA, gAteamMtxB, 0x100);
    veh_stop_engine_sounds();
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/88160/ateam_leave.s")
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING

/* Placed van on a moving level part: re-ground it (0x802CCD80).
 * Re-seat vehicle 14 on the ground (hd.c calls it): as police_reground
 * (86F60): wheel heights (veh_reground, key 0xE), model placement
 * (ateam_build_model), veh_set_record(z, 0xE, x, y, block). The asm hands
 * veh_reground its caller's fp (which reaches gSlotSurface[0..2] and +0x50)
 * and FP registers; the C passes 0 (see the port notes: fidelity). */
void ateam_reground(void) {
    TriSideOut f;
    Out802A9A60 out;

    f.pz = 0.0f;
    f.cross = 0.0f;
    f.cz = 0.0f;
    f.side = 0.0f;
    f.sideZ = 0.0f;
    f.dz = 0.0f;
    veh_reground((s16 *) (gAteamState + 0x52), gAteamPos[1], gAteamPos[0], gAteamPos[2],
                  (s32 *) (gAteamState + 4), (s32 *) &gAteamPos[1], (s16 *) (gAteamState + 0x4C), 0xE, 0,
                  gAteamState, &f, &out);
    ateam_build_model();
    veh_set_record(gAteamPos[2], 0xE, gAteamPos[0], gAteamPos[1], gAteamState);
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/88160/ateam_reground.s")
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
extern u32 gAteamPos[]; /* x, y, z */
/* Veh_zone_alpha at its position (0x802CCE90).
 * Zone level lookup (veh_zone_alpha) for vehicle id 0xE at its position
 * gAteamPos..+8; returns veh_zone_alpha's v1 (the zone list end).
 * Register convention: the asm passes veh_zone_alpha's scan registers t6, t7,
 * s0-s4 through (in and out; here through r), leaves a3 = id and clobbers
 * s5, s6 (conventions.txt). Asm caller ateam_update keeps f12 and f14 live
 * (a mixed N64 build would need a thunk; the native port won't). */
s32 ateam_zone_alpha(ZoneScanRegs *r) {
    return veh_zone_alpha(0xE, gAteamPos[0], gAteamPos[1], gAteamPos[2], r);
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/88160/ateam_zone_alpha.s")
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING

/* Records its position on the deck it stands on (0x802CCED4).
 * As police_ride_attach (86F60) for vehicle 14 (gAteamState, position
 * gAteamPos..9300): value pairs on triangle `id` at the position -> +0x6A/
 * +0x6C, heading -> +0x6E, and at the position plus tbl[0] (+0x52) rotated by
 * the heading -> +0x70/+0x72; returns &gAteamState[0x52] (the asm's v1).
 * Register convention: id in a3; veh_deck_to_local's FP results pass through
 * r (conventions.txt). The asm saves t0, t1, t3, t4 (asm caller
 * veh_attach_riders_walk keeps t0 and t1 live), points $gp at gAteamState and leaves
 * s4 = $gp + 0x4C. */
s32 ateam_ride_attach(s32 id, InterpRegs *r) {
    s32 x = gAteamPos[0];
    s32 z = gAteamPos[2];
    s32 dx;
    s32 dz;

    veh_deck_to_local(id, x, z, r);
    *(s16 *) (gAteamState + 0x6A) = r->t3;
    *(s16 *) (gAteamState + 0x6C) = r->t4;
    *(u16 *) (gAteamState + 0x6E) = *(u16 *) (gAteamState + 0x4C);
    dx = veh_wheel_offset(0, (s16 *) (gAteamState + 0x52), (s16 *) (gAteamState + 0x4C), &dz);
    veh_deck_to_local(id, x + dx, z + dz, r);
    *(s16 *) (gAteamState + 0x70) = r->t3;
    *(s16 *) (gAteamState + 0x72) = r->t4;
    return (s32) (gAteamState + 0x52);
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/88160/ateam_ride_attach.s")
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
void ateam_set_physics(void);

/* Moves it with the deck under it (0x802CCF60).
 * Steer vehicle 14 along triangle `id` (asm caller veh_move_riders_walk): as
 * police_ride_follow (86F60) with this vehicle's block gAteamState, position
 * gAteamPos.., timers ateam_set_physics, ground-contact id 0xE and divisors
 * 800/500, model ateam_build_model.
 * Register convention: id a3, f24 in; veh_ground_contact's s3 (return value) and
 * FP results (f) out (conventions.txt). The asm saves a3, t0-t2 (asm caller
 * veh_move_riders_walk keeps a3, t0, t1 live) and leaves $gp = gAteamState. */
s32 ateam_ride_follow(s32 id, TriSideOut *f) {
    InterpRegs r;
    Regs802A8768 rr;
    s32 heading;
    s32 dist;

    r.f24 = f->sideZ;
    heading = veh_follow_heading((s16 *) (gAteamState + 0x52), VEH14_S16(VS_DECK_POS), (u16 *) (gAteamState + 0x6E), id,
                            VEH14_S16(0x6C), VEH14_S16(VS_DECK_FRONT), VEH14_S16(0x72), &dist, &r);
    VEH14_S16(VS_MOVE_HEADING) = heading;
    VEH14_S16(VS_HEADING) = heading;
    veh_deck_to_world(id, VEH14_S16(VS_DECK_POS), VEH14_S16(0x6C), &r);
    ateam_set_physics();
    gBounceSound = 1;
    f->pz = r.f12;
    *(s32 *) &f->cross = r.f14;
    f->cz = r.f20;
    f->side = r.f22;
    f->sideZ = r.f24;
    f->dz = r.f26;
    rr.s3 = dist;
    veh_ground_contact(gAteamState, 0xE, (s32 *) &gAteamPos[0], (s32 *) &gAteamPos[1], (s32 *) &gAteamPos[2], r.t3,
                  r.t4, 800, 500, (s16 *) (gAteamState + 0x4C), gAteamState + 0x96, (s16 *) (gAteamState + 0x52),
                  (s32 *) (gAteamState + 0x28), (s32 *) (gAteamState + 0x40), (s32 *) (gAteamState + 0x34),
                  (s32 *) (gAteamState + 4), &rr, f);
    ateam_build_model();
    veh_set_record(gAteamPos[2], 0xE, gAteamPos[0], gAteamPos[1], gAteamState);
    return rr.s3;
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/88160/ateam_ride_follow.s")
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
extern s16 gAteamEscapeHeading; /* steering target */
extern f32 gAteamEscapeTurnRate;
extern u8 gAteamNonBlockingParts[];
void ateam_update_effects(s32 t6, s32 t7, s32 s0, s32 s1, s32 s2, s32 s3, s32 s4);
s32 ateam_steer_rate(void);

/* The van driven by the player, per frame (0x802CD068).
 * Per-frame update of vehicle 14 (hd.c, and once from ateam_spawn): as
 * police_update (86F60) with this vehicle's data and constants: zone lookup
 * ateam_zone_alpha, engine sound 0x54, effects ateam_update_effects (it also takes the
 * zone scan's s4), timers ateam_set_physics, turn rate ateam_steer_rate, speed band
 * delta 0x19 (countdown gAteamThrottleHold), acceleration rate 0x4E20, top speed
 * 800.0, reverse flag gAteamEscaping / target gAteamEscapeHeading, ground-contact
 * divisors 800/500, model ateam_build_model, collision sphere table gAteamNonBlockingParts,
 * steering scale gAteamEscapeTurnRate, id 0xE.
 * Register notes as there: the zone scan registers and the FP state handed
 * to veh_ground_contact are the caller's leftovers in the asm (0 here); f12/f14
 * and v1 are left as the callees leave them (not modelled). The asm saves
 * every callee-saved register. */
void ateam_update(void) {
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
    ateam_zone_alpha(&zr);
    veh_snapshot(gAteamState, gAteamAnim, (s32 *) &gAteamPos[0], (s32 *) &gAteamPos[1], (s32 *) &gAteamPos[2]);
    veh_update_horn(0x54);
    if ((s8) gAteamState[0x9A] == 0) {
        ateam_update_effects(zr.t6, zr.t7, zr.s0, zr.s1, zr.s2, zr.s3, zr.s4);
    }
    if (gTurboPending != 0) {
        veh_start_turbo(gAteamState);
    }
    ateam_set_physics();
    veh_steer(ateam_steer_rate(), (u16 *) (gAteamState + 0x4C));
    if (gAteamThrottleHold == 0) {
        veh_throttle(gAteamState, (s16 *) (gAteamState + 0x76), 3, gAteamState + 0x96, (s16 *) (gAteamState + 0x78),
                      0x19);
    } else {
        gAteamThrottleHold--;
    }
    veh_turn_heading(gAteamState, (u16 *) (gAteamState + 0x74), 0x4E20, (s16 *) (gAteamState + 0x76),
                  (u16 *) (gAteamState + 0x4C), (u16 *) (gAteamState + 0x4E), (s8 *) (gAteamState + 0x99), 1);
    scale = veh_slope((s16 *) (gAteamState + 0x76), gAteamState + 0x96, (s32 *) (gAteamState + 4),
                          (f32 *) gAteamState);
    veh_apply_slope(gAteamState, (s16 *) (gAteamState + 0x76), 0xE, (s8 *) (gAteamState + 0x96),
                  (s32 *) (gAteamState + 4), 1, 800.0f);
    if ((s8) gAteamEscaping != 0) {
        veh_set_target_heading(gAteamState, &gAteamEscapeHeading);
    }
    x = veh_move_along_heading(scale, *(u16 *) (gAteamState + 0x4E), (s16 *) (gAteamState + 0x76), (s32 *) &gAteamPos[0],
                      (s32 *) &gAteamPos[2], &o);
    gBounceSound = 1;
    f.pz = 0.0f;
    f.cross = 0.0f;
    f.cz = 0.0f;
    f.side = 0.0f;
    f.sideZ = 0.0f;
    f.dz = 0.0f;
    rr.s3 = o.s3;
    veh_ground_contact(gAteamState, 0xE, (s32 *) &gAteamPos[0], (s32 *) &gAteamPos[1], (s32 *) &gAteamPos[2], x, o.t1,
                  800, 500, (s16 *) (gAteamState + 0x4C), gAteamState + 0x96, (s16 *) (gAteamState + 0x52),
                  (s32 *) (gAteamState + 0x28), (s32 *) (gAteamState + 0x40), (s32 *) (gAteamState + 0x34),
                  (s32 *) (gAteamState + 4), &rr, &f);
    if (gBufferIndex != 0) {
        anim_update_model((u8 *) gAteamMtxA, (u8 *) gAteamMtxB, (AnimChannel *) gAteamAnim);
    } else {
        anim_update_model((u8 *) gAteamMtxB, (u8 *) gAteamMtxA, (AnimChannel *) gAteamAnim);
    }
    ateam_build_model();
    veh_collision_setup(gAteamPos[2], (s32) gAteamNonBlockingParts, 1, 1, gAteamPos[0], gAteamPos[1], 7, VEH14_S16(VS_SPEED), 0x96, 0,
                  0xE, gAteamState);
    veh_collide_spheres(0xE, gAteamState);
    veh_collide_world(0xE);
    if (gCollBlocked == 0) {
        gCollHit = 0;
        gCollVehicleAnim = gAteamAnim;
        obj_collide_vehicle(0xE, gAteamState);
        if (gCollHit == 0) {
            gAteamEscaping = 0;
            goto publish;
        }
        gAteamEscaping = 0;
        if (gBufferIndex != 0) {
            veh_rollback(gAteamState, gAteamAnim, (s32 *) &gAteamPos[0], (s32 *) &gAteamPos[1],
                          (s32 *) &gAteamPos[2], gAteamMtxB, gAteamMtxA, 0x100, &end);
        } else {
            veh_rollback(gAteamState, gAteamAnim, (s32 *) &gAteamPos[0], (s32 *) &gAteamPos[1],
                          (s32 *) &gAteamPos[2], gAteamMtxA, gAteamMtxB, 0x100, &end);
        }
        gAteamThrottleHold = 5;
        v = VEH14_S16(VS_SPEED);
        if (v >= 0) {
            if (v < 50) {
                v = 50;
            }
        } else if (!(v < -49)) {
            v = -50;
        }
        VEH14_S16(VS_SPEED) = -v >> 1;
        ateam_build_model();
        goto publish;
    }
    veh_prepare_escape(gAteamState);
    gAteamEscaping = 1;
    d = *(u16 *) (gAteamState + 0x4E) - 0x800;
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
    veh_reverse_if_behind(gAteamState);
    turn = veh_turn_into_free_arc(gAteamState, *(u16 *) (gAteamState + 0x4E), *(u16 *) (gAteamState + 0x4C), &cur, gAteamEscapeTurnRate);
    gAteamEscapeHeading = cur;
    VEH14_S16(VS_MOVE_HEADING) = cur;
    VEH14_S16(VS_MOMENTUM_HEADING) = cur;
    veh_step_heading(gAteamState, turn, d, &target);
    veh_hit_slowdown(gAteamState, 0);
    gCollVehicleAnim = gAteamAnim;
    obj_collide_vehicle(0xE, gAteamState);
publish:
    gPlayerX = gAteamPos[0];
    gPlayerY = gAteamPos[1];
    gPlayerZ = gAteamPos[2];
    gPlayerSpeed = VEH14_S16(VS_SPEED);
    gPlayerMoveHeading = *(u16 *) (gAteamState + 0x4E);
    gPlayerHeading = *(u16 *) (gAteamState + 0x4C);
    veh_set_record(gAteamPos[2], 0xE, gAteamPos[0], gAteamPos[1], gAteamState);
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/88160/ateam_update.s")
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
extern u8 gAteamDustTimer;   /* countdown */
void ateam_add_tyre_trail(void);

/* Tyre tracks, dust, engine level (0x802CD578).
 * Tyre trail (ateam_add_tyre_trail), then the countdown gAteamDustTimer: when nonzero
 * it just counts down; at zero, if the byte at +0x99 is set, it restarts at
 * 1 and, when fewer than 15 gEffects records are active (fx_count_active),
 * two fx_spawn records are set up (def gFxExhaustSmoke, data 0x29810, tag 1,
 * type 1 at (0xE, 1, 1) and (0xE, 2, 1)). Then always veh_set_engine_level(|speed|
 * >> 5) (speed = s16 at +0x76).
 * Register convention: the asm passes t6, t7, s0-s4 through to
 * fx_spawn (t6 and s1 in its in/out block); $gp (= gAteamState) is read
 * as the global. It leaves fx_spawn's s1 and changes s5-s7
 * (conventions.txt: clobbers). */
void ateam_update_effects(s32 t6, s32 t7, s32 s0, s32 s1, s32 s2, s32 s3, s32 s4) {
    Io802A6274 io;
    s32 v;

    ateam_add_tyre_trail();
    if (gAteamDustTimer != 0) {
        gAteamDustTimer--;
    } else if (gAteamState[0x99] != 0) {
        gAteamDustTimer = 1;
        if (fx_count_active() < 15) {
            io.a3 = 1;
            io.t6 = t6;
            io.s1 = s1;
            fx_spawn(&io, gFxExhaustSmoke, 0x29810, 1, 0xE, 1, 1, t7, s0, s2, s3, s4, 1);
            io.a3 = 1;
            fx_spawn(&io, gFxExhaustSmoke, 0x29810, 1, 0xE, 2, 1, t7, s0, s2, s3, s4, 1);
        }
    }
    v = *(s16 *) (gAteamState + 0x76);
    if (v < 0) {
        v = -v;
    }
    veh_set_engine_level((u32) v >> 5);
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/88160/ateam_update_effects.s")
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
extern u32 gAteamPos[]; /* [0], [2]: trail x, z */

/* Tyre tracks while skidding (0x802CD660).
 * Tyre trail (shape of skyfall_add_tyre_trail in 6E200; $gp = gAteamState): if the
 * byte at +0x99 is set, +0x98 isn't 1, +0x50 < 3 and +0x9B is clear,
 * trail_add(3, y, 250, -400, -400, -400, gAteamPos[0], gAteamPos[2],
 * yaw, 3, 50, 50, 0).
 * Register note: the asm saves and restores every integer register; asm
 * caller ateam_update_effects keeps a0-a3, t6, t7 live (and reads f12/f14 after
 * the call, which the asm doesn't touch but trail_add may); a mixed N64
 * build would need a thunk. */
void ateam_add_tyre_trail(void) {
    if (gAteamState[0x99] != 0 && gAteamState[0x98] != 1 && gAteamState[0x50] < 3 && gAteamState[0x9B] == 0) {
        trail_add(3, *(s32 *) (gAteamState + 0x1C), 250, -400, -400, -400, gAteamPos[0], gAteamPos[2],
                      *(u16 *) (gAteamState + 0x4E), 3, 50, 50, 0);
    }
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/88160/ateam_add_tyre_trail.s")
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
extern u8 * N64P gAteamModel;    /* model header: word offsets to the part lists */

/* Its model matrix, parts and points (0x802CD800).
 * Place vehicle 14's model: as police_build_model (86F60) with this vehicle's
 * header gAteamModel, save copies gAteamMtxA/930C, heading at +0x4C of
 * gAteamState, position gAteamPos..9300, scale 0x55F0 and id 0xE.
 * Register notes as there: the asm's $gp (= gAteamState) is read as the
 * global; the a3/s0/s1 leftovers handed to veh_place_parts / veh_points_transform
 * (only used by a point entry with no matrices) are 0 here; its s1, s4,
 * f12, f14 results (survey: read by ateam_update) aren't modelled.
 * Clobbers s1, s2, s4-s7 (conventions.txt). */
void ateam_build_model(void) {
    MtxChainRegs regs;
    u8 *hdr = gAteamModel;
    u8 *base;
    s32 *m;

    m = (s32 *) N64_IPTR(*(s32 *) (hdr + *(s32 *) (hdr + 0x18) + 4) +
                 (s32) (gBufferIndex ? (u8 *) gAteamMtxA : (u8 *) gAteamMtxB));
    gModelRot[1] = *(u16 *) (gAteamState + 0x4C);
    mtx_build_model(gAteamPos[0], gAteamPos[1], gAteamPos[2], 0x55F0, m);
    base = gBufferIndex ? (u8 *) gAteamMtxA : (u8 *) gAteamMtxB;
    hdr = gAteamModel;
    regs.v1 = 0;
    regs.a0 = 0;
    regs.a3 = 0;
    regs.s1 = 0;
    regs.s2 = (s32) m;
    regs.s0 = 0;
    veh_place_parts(gAteamPos[0], gAteamPos[1], gAteamPos[2], 0xE, hdr + *(s32 *) (hdr + 4),
                  hdr + *(s32 *) (hdr + 8), base, &regs);
    hdr = gAteamModel;
    regs.v1 = gAteamPos[1];
    regs.a0 = gAteamPos[2];
    veh_points_transform(0xE, (s16 *) (hdr + *(s32 *) hdr), (s16 *) (hdr + *(s32 *) (hdr + 4)), base, &regs);
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/88160/ateam_build_model.s")
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

extern f32 gAteamSteerDivisor;

/* Its steering rate (0x802CD938).
 * Speed (s16 at +0x76 of gAteamState, the asm's $gp) / 11.0 when any of the
 * bytes at +0x96/+0x97/+0x98 is 1, else / gAteamSteerDivisor, rounded to nearest.
 * The asm returns it in s3 (see tools_port/conventions.txt). Its asm caller
 * ateam_update keeps a0-a3 live (a mixed N64 build would need a thunk).
 * Same shape as sideswipe_steer_rate (6B4A0). */
s32 ateam_steer_rate(void) {
    f32 div;
    s32 r;

    if (gAteamState[0x96] == 1 || gAteamState[0x97] == 1 || gAteamState[0x98] == 1) {
        div = 11.0f;
    } else {
        div = gAteamSteerDivisor;
    }
    CVT_W_S(r, *(s16 *) (gAteamState + 0x76) / div);
    return r;
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/88160/ateam_steer_rate.s")
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
/* Functional rewrite (tools_port/eqcheck.py verified). Same "set timers"
 * shape as carrier_set_physics (75490.c). Asm callers rely on preserved registers:
 * ateam_ride_follow on t3, t4, f12, f14; ateam_update on a0-a3 (mixed N64 build
 * would need a thunk). */

/* Its gravity and bounce (0x802CD9AC). */
void ateam_set_physics(void) {
    gGravity = gLevelGravity * 4.0f;
    gBounceThreshold = 60;
    gBounceDamping = 3;
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/88160/ateam_set_physics.s")
#endif
