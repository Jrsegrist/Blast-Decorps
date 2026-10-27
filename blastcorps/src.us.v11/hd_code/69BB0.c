#include "common.h"
#include <ultra64.h>
#include "game/game.h"
#ifdef NON_MATCHING
/* The asm reads the level levelno (an s32) with lbu: its most significant
 * byte, 0 for every level. The word, shifted, gives that on either byte order. */
#define LEVEL_MSB ((u8) ((u32) levelno >> 24))
#endif

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
extern u8 gDriverState[];  /* vehicle 0's state block (the asm's $gp) */
extern u8 gDriverAnim[];  /* vehicle 0's animation channel table (AnimChannel, 56040.c) */
extern s32 gDriverExitGroundY;   /* exit-path ground height */
extern u8 gDriverExitChecking;    /* exit walk being set up */
extern u8 gDriverExitDir;    /* exit-walk direction: 0 +z, 1 -z, 2 +x, else -x */
extern s32 gDriverExitStop;   /* exit-walk stop coordinate */
extern u8 gDriverNonBlockingParts[];
extern u8 gDriverExitWalks[];  /* exit-walk table: 5-byte records {level, vehicle, dir, mul, zone}, level -1 ends */
s32 driver_in_exit_zone(s32 *mode);
s32 driver_check_exit_point(s32 dist, s32 key, s32 *fp, TriSideOut *f);
s32 veh_collide_world_fp(s32 kind, s32 fp); /* 56040.c: veh_collide_world with the asm's fp out */
u8 *driver_build_model(void);
extern u8 * N64P gDriverMtxA;  /* vehicle 0's model buffers */
extern u8 * N64P gDriverMtxB;
extern s16 gDriverDeflectHeading;  /* vehicle 0's last ring-steered heading */
extern s8 gDriverDeflecting;   /* ring steering active */
void driver_update_anims(void);
void driver_exit_walk_step(s32 key, s32 fp, TriSideOut *f);
void driver_set_physics(void);
s32 driver_steer_rate(void);
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
extern u8 * N64P gDriverModel; /* vehicle 0's model header */

/* Sets up the driver on foot (type 0) (0x802AE370).
 * Sets up vehicle 0 (object 0) from the model header `hdr` at (x, y, z) with
 * heading `heading` (called by the object dispatcher lvl_spawn_vehicles):
 * gDriverModel = hdr, two 0xC80-byte buffers from the heap gHeapPtr
 * (gDriverMtxA, gDriverMtxB; the cursor moves 0x1900), the object record
 * (veh_add_record(0, 0, buf1, buf2, hdr)), the state block gDriverState reset
 * (veh_reset_state) with its wheel table +0x52..+0x68 = (+-30, 30) / (+-35, 35)
 * pairs, position gDriverPos..810, headings +0x4C/+0x4E/+0x74, exit-walk flags
 * gDriverCanExit = 1, gDriverExitWalk/Checking = 0, gDriverDeflecting = 0, +0xA1 = 0; the initial
 * ground under the wheels (veh_init_ground, key 0); the animated model
 * (anim_init_model(buf2, buf1, gDriverAnim, hdr)) with channel 0 started and
 * animated into each buffer in turn; the throttle bands +0x78..+0x94; the
 * parts (veh_register_parts, tag 0, scale 0x4E20); marker record 0
 * (veh_add_shadow(0, 0x28, 15, 15)); one frame of driver_update with byte
 * +0x9A set (no driving); the model matrix copied from the second buffer to
 * the first (mtx_copy_at); channels 1-4 set up (1: 0, 0, 0 / 0.5, 1; 2-4: 2,
 * 0, 1 / 0.5, 1) and gCamDistance / gCamHeightBase = 0xD48, 0.
 * Register convention (conventions.txt): hdr s2, x t7, y s3, z s0, heading
 * s1; veh_init_ground's fp and f12-f26 inputs are the dispatcher's leftovers
 * (here fp, f). The asm saves t0-t5 (asm caller lvl_spawn_vehicles keeps t1, t2
 * live), points $gp at gDriverState and leaves s0-s7, fp and the FP registers
 * as its callees leave them. Its addi traps on overflow. Fidelity note: the
 * driver_update call inherits leftovers in the asm (fp from anim_update_model's
 * callees etc.); the C version uses its own defaults. */
void driver_spawn(u8 *hdr, s32 x, s32 y, s32 z, s32 heading, s32 fp, TriSideOut *f) {
    s32 *pos = (s32 *) gDriverPos;
    u8 *buf;
    s16 *h;

    gDriverModel = hdr;
    buf = gHeapPtr;
    gDriverMtxA = buf;
    gDriverMtxB = buf + 0xC80;
    gHeapPtr = buf + 0x1900;
    veh_add_record(VEH_DRIVER, 0, (s32) gDriverMtxA, (s32) gDriverMtxB, hdr);
    veh_reset_state(gDriverState);
    h = (s16 *) (gDriverState + 0x52);
    h[0] = 30;
    h[1] = 30;
    h[2] = -30;
    h[3] = 30;
    h[4] = 30;
    h[5] = -30;
    h[6] = 35;
    h[7] = 35;
    h[8] = -35;
    h[9] = 35;
    h[10] = 35;
    h[11] = -35;
    pos[0] = x;
    pos[1] = y;
    pos[2] = z;
    *(s16 *) (gDriverState + 0x4C) = heading;
    *(s16 *) (gDriverState + 0x4E) = heading;
    *(s16 *) (gDriverState + 0x74) = heading;
    gDriverCanExit = 1;
    gDriverExitWalk = 0;
    gDriverExitChecking = 0;
    gDriverDeflecting = 0;
    gDriverState[0xA1] = 0;
    veh_init_ground((s16 *) (gDriverState + 0x52), pos[1], x, z, (s32 *) (gDriverState + 4), &pos[1],
                  (s16 *) (gDriverState + 0x4C), 0, fp, gDriverState, f);
    anim_init_model((u32 *) gDriverMtxB, (u32 *) gDriverMtxA, (AnimChannel *) gDriverAnim, gDriverModel);
    anim_set_speed((AnimChannel *) gDriverAnim, 0, 0x64);
    anim_set_reverse((AnimChannel *) gDriverAnim, 0, 0);
    anim_set_loop_mode((AnimChannel *) gDriverAnim, 0, 0);
    anim_set_interp(0.0f, (AnimChannel *) gDriverAnim, 0, 0);
    anim_play((AnimChannel *) gDriverAnim, 0, 1);
    anim_update_model(gDriverMtxA, gDriverMtxB, (AnimChannel *) gDriverAnim);
    anim_rewind(0, (AnimChannel *) gDriverAnim);
    anim_play((AnimChannel *) gDriverAnim, 0, 1);
    anim_update_model(gDriverMtxB, gDriverMtxA, (AnimChannel *) gDriverAnim);
    h = (s16 *) (gDriverState + 0x78);
    h[0] = -0x28;
    h[1] = 0;
    h[2] = 2;
    h[3] = 0;
    h[4] = 0x28;
    h[5] = 2;
    h[6] = 0x28;
    h[7] = 0x3C;
    h[8] = 2;
    h[9] = 0x3C;
    h[10] = 0x50;
    h[11] = 2;
    h[12] = 0x50;
    h[13] = 0x64;
    h[14] = 2;
    hdr = gDriverModel;
    veh_register_parts(0, hdr + *(s32 *) (hdr + 4), hdr + *(s32 *) (hdr + 8), 0x4E20);
    veh_add_shadow(VEH_DRIVER, 0x28, 0xF, 0xF);
    gDriverState[0x9A] = 1;
    driver_update();
    gDriverState[0x9A] = 0;
    hdr = gDriverModel;
    mtx_copy_at(gDriverMtxB, gDriverMtxA, *(s32 *) (hdr + *(s32 *) (hdr + 0x18) + 4));
    anim_set_speed((AnimChannel *) gDriverAnim, 1, 0);
    anim_set_reverse((AnimChannel *) gDriverAnim, 1, 0);
    anim_set_loop_mode((AnimChannel *) gDriverAnim, 1, 0);
    anim_set_interp(0.5f, (AnimChannel *) gDriverAnim, 1, 1);
    anim_set_speed((AnimChannel *) gDriverAnim, 2, 2);
    anim_set_reverse((AnimChannel *) gDriverAnim, 2, 0);
    anim_set_loop_mode((AnimChannel *) gDriverAnim, 2, 1);
    anim_set_interp(0.5f, (AnimChannel *) gDriverAnim, 2, 1);
    anim_set_speed((AnimChannel *) gDriverAnim, 3, 2);
    anim_set_reverse((AnimChannel *) gDriverAnim, 3, 0);
    anim_set_loop_mode((AnimChannel *) gDriverAnim, 3, 1);
    anim_set_interp(0.5f, (AnimChannel *) gDriverAnim, 3, 1);
    anim_set_speed((AnimChannel *) gDriverAnim, 4, 2);
    anim_set_reverse((AnimChannel *) gDriverAnim, 4, 0);
    anim_set_loop_mode((AnimChannel *) gDriverAnim, 4, 1);
    anim_set_interp(0.5f, (AnimChannel *) gDriverAnim, 4, 1);
    gCamDistance = 0xD48;
    gCamHeightBase = 0;
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/69BB0/driver_spawn.s")
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
extern u8 gDriverAnim[]; /* animation channel table (AnimChannel, 56040.c) */

/* Stops the driver's blend animation (on getting into a vehicle) (0x802AE860).
 * Stops channel 31 of gDriverAnim (anim_stop; called from hd.c's vehicle
 * switch). The asm leaves v0 = 31 and v1 = &gDriverAnim; the C caller uses
 * neither. */
void driver_stop_blend_anim(void) {
    anim_stop(0x1F, (AnimChannel *) gDriverAnim);
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/69BB0/driver_stop_blend_anim.s")
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING

/* Getting out: checks the walk-out path, starts the exit walk; 1 if ok (0x802AE888).
 * Drive-in setup check for vehicle 0, called from hd.c (veh_leave) with
 * the vehicle's distance `dist`; returns 1 when the exit walk starts, else 0
 * (the C caller declares u8; the asm returns the full v0).
 * Unless gDriverCanExit is clear (then 0 at once), the first exit-walk record of
 * gDriverExitWalks (5 bytes: level (s8, -1 ends; compared with the whole word
 * levelno), vehicle type, direction, multiplier, zone mode) for this level
 * and gCurVehicle - with zone mode 0 or driver_in_exit_zone(zone mode) true - gives
 * the direction (default 0) and multiplier (default 1). The path from the
 * player along that direction is checked every 100 units up to dist * mul
 * and at dist * mul itself (driver_check_exit_point, which also positions the vehicle
 * at each point); any blocked point returns 0. Otherwise the vehicle is
 * reset (flags +0x96..+0x99 and the speed cleared, heading +0x4C/+0x4E/+0x74
 * = 0, 0x800, 0x400 or 0xC00 for direction 0, 1, 2, else), gDriverExitStop = the
 * last checked z (directions 0/1) or x, the position = the player's (y + 500
 * for vehicle types 0xB, 0x11, 0x12), gDriverExitWalk = 1, channel 1 restarted
 * looping, gCamDistance / gCamHeightBase = 0xD48, 0. gDriverExitChecking is 1 while this runs and
 * gCollExitProbe is 1 during the call.
 * Fidelity note: driver_check_exit_point's veh_reground inputs come from leftover
 * registers in the asm: key t8 = gCurVehicle << 2 (what the C caller
 * veh_leave leaves in t8 at the call), reproduced here; fp is the C
 * caller's (0: the game thread's fp) on the first check and then what the
 * previous check's veh_collide_world left (its part count when vehicle 0's sphere
 * touched the world, else unchanged), threaded through `fp` here; it ends up
 * in gSlotSurface[0..2] and +0x50. Traced in the original (Oct 2026, level 0,
 * vehicle 4): t8 = 0x10 on every check, fp 0 on the first and 0 or 2 on the
 * later ones. f12-f26 are pass-through FP state (zeros here). The asm's addi
 * traps on overflow. */
s32 driver_try_exit(s32 dist) {
    s32 *pos = (s32 *) gDriverPos;
    u8 *rec;
    s32 dir = 0;
    s32 mul = 1;
    s32 mode;
    s32 off;
    s32 key;
    s32 fp = 0;
    s32 ret = 0;
    s32 t;
    TriSideOut f;

    gCollExitProbe = 1;
    if (gDriverCanExit != 0) {
        for (rec = gDriverExitWalks; (s8) rec[0] != -1; rec += 5) {
            if ((s8) rec[0] != levelno || rec[1] != gCurVehicle) {
                continue;
            }
            if (rec[4] != 0) {
                mode = rec[4];
                if (driver_in_exit_zone(&mode) == 0) {
                    continue;
                }
            }
            dir = rec[2];
            mul = rec[3];
            break;
        }
        dist = (u32) dist * (u32) mul;
        gDriverExitDir = dir;
        gDriverExitChecking = 1;
        gDriverExitGroundY = gPlayerY;
        key = gCurVehicle << 2;
        f.pz = f.cross = f.cz = f.side = f.sideZ = f.dz = 0.0f;
        for (off = 0; !(dist < off); off += 100) {
            if (driver_check_exit_point(off, key, &fp, &f) == 0) {
                goto done;
            }
        }
        if (driver_check_exit_point(dist, key, &fp, &f) == 0) {
            goto done;
        }
        gDriverState[0x96] = 0;
        gDriverState[0x97] = 0;
        gDriverState[0x98] = 0;
        gDriverState[0x99] = 0;
        *(s16 *) (gDriverState + 0x76) = 0;
        switch (gDriverExitDir) {
            case 0:
                t = 0;
                break;
            case 1:
                t = 0x800;
                break;
            case 2:
                t = 0x400;
                break;
            default:
                t = 0xC00;
                break;
        }
        *(s16 *) (gDriverState + 0x4E) = t;
        *(s16 *) (gDriverState + 0x4C) = t;
        *(s16 *) (gDriverState + 0x74) = t;
        if (gDriverExitDir == 0 || gDriverExitDir == 1) {
            gDriverExitStop = pos[2];
        } else {
            gDriverExitStop = pos[0];
        }
        pos[0] = gPlayerX;
        t = gPlayerY;
        if (gCurVehicle == VEH_BARGE_0B || gCurVehicle == VEH_BARGE_11 || gCurVehicle == VEH_BARGE_12) {
            t += 500;
        }
        pos[1] = t;
        pos[2] = gPlayerZ;
        gDriverExitWalk = 1;
        anim_set_speed((AnimChannel *) gDriverAnim, 1, 0);
        anim_set_reverse((AnimChannel *) gDriverAnim, 1, 0);
        anim_set_loop_mode((AnimChannel *) gDriverAnim, 1, 0);
        anim_play((AnimChannel *) gDriverAnim, 1, -1);
        gCamDistance = 0xD48;
        gCamHeightBase = 0;
        ret = 1;
    }
done:
    gDriverExitChecking = 0;
    gCollExitProbe = 0;
    return ret;
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/69BB0/driver_try_exit.s")
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING

/* The exit-table zone test (1: a z band, 2: an x/z box) (0x802AEB9C).
 * Zone test for mode *mode (asm: a3 in and out, result in a1; see
 * tools_port/conventions.txt). Mode 1: 1 when z/32 is in [0xCCD, 0xDB5).
 * Mode 2: 1 when x/32 is in [0x834, 0x961) and z/32 in [0x4B0, 0x5DD).
 * Otherwise 0. The asm leaves the last value it compared (z/32, or x/32 when
 * the x test fails) in a3, so *mode gets that; other modes leave it alone.
 * Register note: asm caller driver_try_exit keeps a0, a2, t0-t3, f12 and f14
 * live across the call (a mixed N64 build would need a thunk). */
s32 driver_in_exit_zone(s32 *mode) {
    s32 v;

    if (*mode == 1) {
        *mode = v = gPlayerZ >> 5;
        return v >= 0xCCD && v < 0xDB5;
    }
    if (*mode == 2) {
        *mode = v = gPlayerX >> 5;
        if (v < 0x834 || v >= 0x961) {
            return 0;
        }
        *mode = v = gPlayerZ >> 5;
        return v >= 0x4B0 && v < 0x5DD;
    }
    return 0;
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/69BB0/driver_in_exit_zone.s")
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
/* Puts the driver at one walk-out point; 1 if it is free (0x802AEC3C).
 * One exit-walk path check for vehicle 0 ($gp = gDriverState, read directly):
 * puts the vehicle `dist` from the player in direction gDriverExitDir (gDriverPos
 * / gDriverZ; 0: z + dist, 1: z - dist, 2: x + dist, else x - dist), follows
 * the ground there (veh_reground at height gDriverExitGroundY; middle height into
 * gDriverY), places the model (driver_build_model), sets gDriverExitGroundY to the mean
 * of the wheel heights +0x10/+0x1C (logical >> 1), resets the collision state
 * (veh_collision_setup with b0 = 0 and h2 = the part-list end, as driver_build_model
 * leaves them in t0/t2), runs the world and object collision passes
 * (veh_collide_world(0), gCollVehicleAnim = gDriverAnim, obj_collide_vehicle(0)) and, outside
 * vehicle type 0xB, tnt_test_point at the position. Returns 1 when the spot is
 * free (gCollHit clear), else 0.
 * Register convention (conventions.txt): dist a2, result a3; veh_reground's
 * leftover inputs key t8, fp, f12-f26 come in from the asm caller (here key,
 * *fp, f). The asm doesn't save fp: it leaves what veh_collide_world left there
 * (the part count on a world hit, else unchanged; obj_collide_vehicle's chain
 * restores it), which the caller's next check reads, so *fp is in/out. The
 * asm saves v0-a0, a2, t0-t9 and gp, and also leaves a1 = gCollHit
 * (unread). The asm's add/sub trap on overflow. */
s32 driver_check_exit_point(s32 dist, s32 key, s32 *fp, TriSideOut *f) {
    s32 *pos = (s32 *) gDriverPos;
    s32 x;
    s32 z;
    u8 *end;
    Out802A9A60 o;

    switch (gDriverExitDir) {
        case 0:
            x = gPlayerX;
            z = gPlayerZ + dist;
            break;
        case 1:
            x = gPlayerX;
            z = gPlayerZ - dist;
            break;
        case 2:
            x = gPlayerX + dist;
            z = gPlayerZ;
            break;
        default:
            x = gPlayerX - dist;
            z = gPlayerZ;
            break;
    }
    pos[0] = x;
    pos[2] = z;
    veh_reground((s16 *) (gDriverState + 0x52), gDriverExitGroundY, x, z, (s32 *) (gDriverState + 4), &pos[1],
                  (s16 *) (gDriverState + 0x4C), key, *fp, gDriverState, f, &o);
    end = driver_build_model();
    gDriverExitGroundY = (u32) (*(s32 *) (gDriverState + 0x10) + *(s32 *) (gDriverState + 0x1C)) >> 1;
    veh_collision_setup(pos[2], (s32) gDriverNonBlockingParts, 0, 0, pos[0], pos[1], 0, 0, (s32) end, 0, 0, gDriverState);
    *fp = veh_collide_world_fp(0, *fp);
    gCollVehicleAnim = gDriverAnim;
    obj_collide_vehicle(0, gDriverState);
    if (gCurVehicle != VEH_BARGE_0B) {
        tnt_test_point(pos[0], pos[1], pos[2]);
    }
    return gCollHit == 0;
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/69BB0/driver_check_exit_point.s")
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
/* Veh_zone_alpha at the driver's position (0x802AEE84).
 * Zone level lookup (veh_zone_alpha) for vehicle id 0 at its position
 * gDriverPos..+8; returns veh_zone_alpha's v1 (the zone list end).
 * Register convention: the asm passes veh_zone_alpha's scan registers t6, t7,
 * s0-s4 through (in and out; here through r), leaves a3 = id and clobbers
 * s5, s6 (conventions.txt). Asm caller driver_update keeps t8, f12 and f14 live
 * (a mixed N64 build would need a thunk; the native port won't). */
s32 driver_zone_alpha(ZoneScanRegs *r) {
    return veh_zone_alpha(0, gDriverPos[0], gDriverPos[1], gDriverPos[2], r);
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/69BB0/driver_zone_alpha.s")
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
/* Ring-steering branch of driver_update (gCollBlocked set): veh_prepare_escape,
 * gDriverDeflecting = 1, veh_reverse_if_behind, veh_turn_into_free_arc (heading +0x4E toward +0x4C,
 * scale 1.0; the new index goes to gDriverDeflectHeading, +0x4E and +0x74),
 * veh_step_heading with that turn and veh_hit_slowdown (limit 0).
 * The asm also computes |(+0x4E - 0x800, wrapped) - ring midpoint| (folded
 * to <= 0x800) and then always branches past its use; the value only
 * survives in v1 as veh_step_heading's pass-through input, reproduced here. */
static void port_aeec8_ring(void) {
    s32 mid;
    s32 d;
    s32 cur;
    s32 turn;
    s32 target;

    veh_prepare_escape(gDriverState);
    gDriverDeflecting = 1;
    mid = veh_free_arc_mid();
    d = *(u16 *) (gDriverState + 0x4E) - 0x800;
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
    veh_reverse_if_behind(gDriverState);
    turn = veh_turn_into_free_arc(gDriverState, *(u16 *) (gDriverState + 0x4E), *(u16 *) (gDriverState + 0x4C), &cur, 1.0f);
    gDriverDeflectHeading = cur;
    *(s16 *) (gDriverState + 0x4E) = cur;
    *(s16 *) (gDriverState + 0x74) = cur;
    veh_step_heading(gDriverState, turn, d, &target);
    veh_hit_slowdown(gDriverState, 0);
}

/* The driver's per-frame update (veh_update_player case 0) (0x802AEEC8).
 * Per-frame update of vehicle 0 (hd.c's hd_game_frame / veh_update_player, and
 * driver_spawn; state block gDriverState, the asm's $gp). Zone lookup
 * (driver_zone_alpha). Unless byte +0x9A is set: animation update
 * (driver_update_anims) and, while the exit walk gDriverExitWalk runs, its step
 * (driver_exit_walk_step) instead of driving. Driving: driver_set_physics; outside game
 * modes 0x800 / 1 / 0x1000 (the next mode gNextGameMode if set, else
 * gGameMode; 64-bit compares): steering and throttle (veh_steer_throttle: rate
 * driver_steer_rate() = 0x8C, mode 3, delta 6), braking when +0xA1 == 1
 * (veh_coast), heading (veh_turn_heading: rate 9000, no sound), slope
 * (veh_slope into the f32 at +0, veh_apply_slope: kind 0, clamp, div
 * 60.0), veh_set_target_heading while gDriverDeflecting is set, move (veh_move_along_heading) and
 * ground contact (veh_ground_contact: id 0, divisors 0x3C/0x3C, after
 * gBounceSound = 0). Then: animate the model into the current buffer
 * (anim_update_model), place it (driver_build_model); unless the exit walk runs: in
 * game modes with any of bits 0x1801 gDriverDeflecting = 0; otherwise collision
 * state reset (veh_collision_setup: b0 = 0 and h2 = the part-list end as
 * driver_build_model leaves them in t0/t2, h1 = speed), part contacts
 * (veh_collide_spheres(0)), world collision (veh_collide_world(0)), the object pass
 * (obj_collide_vehicle(0)), and then the ring-steering branch when gCollBlocked is
 * set, else gDriverDeflecting = 0. Finally the player position/speed/headings
 * gPlayerX..Z / gPlayerSpeed..40 are copied from the vehicle and the
 * position reported (veh_set_record(z, 0, x, y)).
 * Fidelity notes: the asm feeds driver_zone_alpha its C caller's t6, t7, s0-s4
 * (0 here; the results are unused), driver_exit_walk_step its C caller's t8 as the
 * triangle-skip key and fp / f12-f26 leftovers (0 and zeros here; fp becomes
 * gSlotSurface[0..2] and +0x50). Traced in the original (Oct 2026, gameplay,
 * level 0): fp 0 always (the game thread's); t8 = 0x13 from veh_update_player
 * (left by people_update's callees), 0 from hd_game_frame's mode-0x1801 path,
 * heap pointers during the level intro, and util_rand2_range's seed
 * (0x47FF37D3) on frames where driver_update_anims took its random-idle path. The
 * key only makes ground_scan_moveable skip gMoveableTris triangles whose u16 id equals
 * it (and ground_cache_tri test 0xFF); those ids are vehicle types 6, 7, 0xB,
 * 0x11, 0x12 (veh_deck_transform's callers), which neither 0 nor any traced
 * leftover equals, so 0 gives the same ground. And
 * veh_ground_contact the FP registers left by veh_move_along_heading's sine routines
 * (zeros here). The asm saves every callee-saved register. */
void driver_update(void) {
    s32 *pos = (s32 *) gDriverPos;
    ZoneScanRegs zr;
    TriSideOut f;
    Out802A860C o;
    Regs802A8768 rr;
    u64 mode;
    u8 *end;
    s32 rate;
    s32 x;
    f32 slope;

    zr.t6 = zr.t7 = zr.s0 = zr.s1 = zr.s2 = zr.s3 = zr.s4 = 0;
    driver_zone_alpha(&zr);
    if ((s8) gDriverState[0x9A] == 0) {
        driver_update_anims();
        if (gDriverExitWalk != 0) {
            f.pz = f.cross = f.cz = f.side = f.sideZ = f.dz = 0.0f;
            driver_exit_walk_step(0, 0, &f);
            goto animate;
        }
    }
    driver_set_physics();
    rate = driver_steer_rate();
    mode = gNextGameMode;
    if (mode == 0) {
        mode = gGameMode;
    }
    if (mode != 0x800 && mode != 1 && mode != 0x1000) {
        veh_steer_throttle(rate, (u16 *) (gDriverState + 0x4C), gDriverState, (s16 *) (gDriverState + 0x76), 3,
                      gDriverState + 0x96, (s16 *) (gDriverState + 0x78), 6);
        if (gDriverState[0xA1] == 1) {
            veh_coast(gDriverState);
        }
        veh_turn_heading(gDriverState, (u16 *) (gDriverState + 0x74), 0x2328, (s16 *) (gDriverState + 0x76),
                      (u16 *) (gDriverState + 0x4C), (u16 *) (gDriverState + 0x4E), (s8 *) (gDriverState + 0x99), 0);
        slope = veh_slope((s16 *) (gDriverState + 0x76), gDriverState + 0x96, (s32 *) (gDriverState + 4),
                              (f32 *) gDriverState);
        veh_apply_slope(gDriverState, (s16 *) (gDriverState + 0x76), 0, (s8 *) (gDriverState + 0x96),
                      (s32 *) (gDriverState + 4), 1, 60.0f);
        if (gDriverDeflecting != 0) {
            veh_set_target_heading(gDriverState, &gDriverDeflectHeading);
        }
        x = veh_move_along_heading(slope, *(u16 *) (gDriverState + 0x4E), (s16 *) (gDriverState + 0x76), &pos[0], &pos[2], &o);
        gBounceSound = 0;
        rr.s3 = o.s3;
        f.pz = f.cross = f.cz = f.side = f.sideZ = f.dz = 0.0f;
        veh_ground_contact(gDriverState, 0, &pos[0], &pos[1], &pos[2], x, o.t1, 0x3C, 0x3C, (s16 *) (gDriverState + 0x4C),
                      gDriverState + 0x96, (s16 *) (gDriverState + 0x52), (s32 *) (gDriverState + 0x28),
                      (s32 *) (gDriverState + 0x40), (s32 *) (gDriverState + 0x34), (s32 *) (gDriverState + 4), &rr, &f);
    }
animate:
    if (gBufferIndex != 0) {
        anim_update_model(gDriverMtxA, gDriverMtxB, (AnimChannel *) gDriverAnim);
    } else {
        anim_update_model(gDriverMtxB, gDriverMtxA, (AnimChannel *) gDriverAnim);
    }
    end = driver_build_model();
    if (gDriverExitWalk == 0) {
        mode = gNextGameMode;
        if (mode == 0) {
            mode = gGameMode;
        }
        if (mode & 0x1801) {
            gDriverDeflecting = 0;
        } else {
            veh_collision_setup(pos[2], (s32) gDriverNonBlockingParts, 0, 0, pos[0], pos[1], 0, *(s16 *) (gDriverState + 0x76),
                          (s32) end, 0, 0, gDriverState);
            veh_collide_spheres(0, gDriverState);
            veh_collide_world(0);
            gCollVehicleAnim = gDriverAnim;
            obj_collide_vehicle(0, gDriverState);
            if (gCollBlocked != 0) {
                port_aeec8_ring();
            } else {
                gDriverDeflecting = 0;
            }
        }
    }
    gPlayerX = pos[0];
    gPlayerY = pos[1];
    gPlayerZ = pos[2];
    gPlayerSpeed = *(s16 *) (gDriverState + 0x76);
    gPlayerMoveHeading = *(u16 *) (gDriverState + 0x4E);
    gPlayerHeading = *(u16 *) (gDriverState + 0x4C);
    veh_set_record(pos[2], 0, pos[0], pos[1], gDriverState);
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/69BB0/driver_update.s")
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
extern u8 gDriverState[];  /* vehicle 0's state block (the asm's $gp) */
extern u8 gDriverExitDir;    /* exit-walk direction: 0 +z, 1 -z, 2 +x, else -x */
extern s32 gDriverExitStop;   /* exit-walk stop coordinate */

/* One frame of the walk away from the vehicle just left (0x802AF340).
 * Drive-in step of vehicle 0 ($gp = gDriverState, read directly): speed +0x76 =
 * 80, the position gDriverPos (x) / gDriverZ (z) moves 100 in the direction
 * gDriverExitDir, the ground is followed with veh_reground (wheel heights into
 * +4, middle height into gDriverY), and once the position reaches (or
 * passes) gDriverExitStop the exit walk ends (gDriverExitWalk = 0, speed 0). Then the
 * shadow/marker record 0 is updated: veh_update_shadow(0, x, (gSlotGroundY[1] +
 * [2]) >> 1 (logical), z, y, ...).
 * Register convention (conventions.txt): veh_reground's leftover inputs come
 * from the asm caller: key t8, fp and f12-f26 (here key, fp, f). Fidelity
 * notes: the asm's key is whatever t8 held in driver_update (its C callers'
 * leftover, or util_rand2_range's seed on driver_update_anims's random-idle path;
 * traced 0x13 / 0x47FF37D3, see driver_update: no triangle id can equal it,
 * so the C's 0 scans the same ground); fp (traced: always 0, as here) ends up
 * as gSlotSurface[0..2] and +0x50. The asm passes only five arguments to
 * veh_update_shadow: arg5 is a stale stack word (0 here), arg6/arg7 are the high
 * and low words of its own saved $ra (-1 and 0x802AEF50, the return address in
 * driver_update; veh_update_shadow keeps them as s16s), reproduced here. The asm's
 * addi traps on overflow; it changes s0-s4 (and veh_reground's s5, s6). */
void driver_exit_walk_step(s32 key, s32 fp, TriSideOut *f) {
    s32 *pos = (s32 *) gDriverPos;
    s32 x;
    s32 z;
    s32 t;
    s32 stop;
    Out802A9A60 o;

    *(s16 *) (gDriverState + 0x76) = 0x50;
    x = pos[0];
    z = pos[2];
    switch (gDriverExitDir) {
        case 0:
            z += 100;
            break;
        case 1:
            z -= 100;
            break;
        case 2:
            x += 100;
            break;
        default:
            x -= 100;
            break;
    }
    pos[0] = x;
    pos[2] = z;
    veh_reground((s16 *) (gDriverState + 0x52), pos[1], x, z, (s32 *) (gDriverState + 4), &pos[1],
                  (s16 *) (gDriverState + 0x4C), key, fp, gDriverState, f, &o);
    t = gDriverExitStop;
    switch (gDriverExitDir) {
        case 0:
            stop = !(pos[2] < t);
            break;
        case 1:
            stop = !(t < pos[2]);
            break;
        case 2:
            stop = !(pos[0] < t);
            break;
        default:
            stop = !(t < pos[0]);
            break;
    }
    if (stop) {
        gDriverExitWalk = 0;
        *(s16 *) (gDriverState + 0x76) = 0;
    }
    veh_update_shadow(0, pos[0], (u32) (gSlotGroundY[1] + gSlotGroundY[2]) >> 1, pos[2], pos[1], 0, -1,
                  (s32) 0x802AEF50);
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/69BB0/driver_exit_walk_step.s")
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
extern u8 gDriverState[];  /* this vehicle's state block (the asm's $gp) */
extern s16 gDriverWalkPose;   /* last channel-1 mode */

/* Field 0x10 of channel idx of gDriverAnim (1 = running). */
static s32 AnimChannelState(s32 idx) {
    s32 ch[8];

    anim_get_state(idx, (AnimChannel *) gDriverAnim, ch);
    return ch[0];
}

/* Idle and walk animations, footstep sounds 0x14/0x15 (0x802AF4BC).
 * Per-frame animation update of vehicle 0 (asm caller driver_update; the asm
 * reads the state block through $gp = gDriverState, read directly here). Byte
 * +0xA1 records whether the vehicle was moving last frame.
 * Stopped (speed +0x76 == 0): on the first stopped frame channel 1 is stopped
 * and channel 31 restarted with 70; then, when none of channels 31, 2, 3, 4 is
 * running and util_rand2_range(0, 20) is 0, one of channels 3 / 4 / 2 (by
 * util_rand2_range(0, 2) = 0 / 1 / else) is started.
 * Moving: on the first moving frame channel 1 is set to mode 2 and the running
 * one of channels 2, 3, 4 (else 3, after resetting it) is handed over via
 * anim_blend_channels and channel 31 restarted with 40. Then, unless channel 31 is
 * running, channel 1 gets the direction (speed < 0) and |speed| / 11 and is
 * restarted, and (outside levels 0x31 and 0x26) a change of channel 1's mode
 * to 2 or 6 starts sound 0x14 / 0x15.
 * Register note: the asm clobbers s5 (and f20/f30 through anim_blend_channels);
 * conventions.txt. Asm caller driver_update reads t6, t8, f12 and f14 after
 * the call, which the asm passes through except where its C callees change
 * them (util_rand2_range leaves its seeds in t6/t8): a mixed N64 build would need
 * a thunk, the native port won't. */
void driver_update_anims(void) {
    u8 *base = gDriverAnim;
    s32 speed = *(s16 *) (gDriverState + 0x76);
    s32 r;
    s32 ch[8];

    if (speed == 0) {
        if (gDriverState[0xA1] != 0) {
            anim_stop(1, (AnimChannel *) base);
            anim_set_frame(0.0f, (AnimChannel *) base, 3, 0);
            anim_blend_channels(1, 3, (AnimChannel *) base);
            anim_set_speed((AnimChannel *) base, 0x1F, 0x46);
            anim_set_reverse((AnimChannel *) base, 0x1F, 0);
            anim_set_loop_mode((AnimChannel *) base, 0x1F, 0);
            anim_play((AnimChannel *) base, 0x1F, 1);
        }
        gDriverState[0xA1] = 0;
        if (AnimChannelState(0x1F) == 1 || AnimChannelState(2) == 1 || AnimChannelState(3) == 1 ||
            AnimChannelState(4) == 1) {
            return;
        }
        if (util_rand2_range(0, 0x14) != 0) {
            return;
        }
        r = util_rand2_range(0, 2);
        if (r == 0) {
            anim_set_frame(0.0f, (AnimChannel *) base, 3, 0);
            anim_play((AnimChannel *) base, 3, 1);
        } else if (r == 1) {
            anim_set_frame(0.0f, (AnimChannel *) base, 4, 0);
            anim_play((AnimChannel *) base, 4, 1);
        } else {
            anim_set_frame(0.0f, (AnimChannel *) base, 2, 0);
            anim_play((AnimChannel *) base, 2, 1);
        }
        return;
    }

    if (gDriverState[0xA1] != 1) {
        anim_set_frame(0.0f, (AnimChannel *) base, 1, 2);
        if (AnimChannelState(2) != 0) {
            anim_blend_channels(2, 1, (AnimChannel *) base);
            anim_stop(2, (AnimChannel *) base);
        } else if (AnimChannelState(3) != 0) {
            anim_blend_channels(3, 1, (AnimChannel *) base);
            anim_stop(3, (AnimChannel *) base);
        } else if (AnimChannelState(4) != 0) {
            anim_blend_channels(4, 1, (AnimChannel *) base);
            anim_stop(4, (AnimChannel *) base);
        } else {
            anim_set_frame(0.0f, (AnimChannel *) base, 3, 0);
            anim_blend_channels(3, 1, (AnimChannel *) base);
        }
        anim_set_speed((AnimChannel *) base, 0x1F, 0x28);
        anim_set_reverse((AnimChannel *) base, 0x1F, 0);
        anim_set_loop_mode((AnimChannel *) base, 0x1F, 0);
        anim_play((AnimChannel *) base, 0x1F, 1);
    }
    if (AnimChannelState(0x1F) != 1) {
        speed = *(s16 *) (gDriverState + 0x76);
        if (speed < 0) {
            anim_set_reverse((AnimChannel *) base, 1, 1);
        } else {
            anim_set_reverse((AnimChannel *) base, 1, 0);
        }
        if (LEVEL_MSB != 0x31 && LEVEL_MSB != 0x26) { /* always true in the game */
            s32 old = gDriverWalkPose;
            s32 mode;

            anim_get_state(1, (AnimChannel *) base, ch);
            mode = ch[6];
            gDriverWalkPose = mode;
            if (mode != old) {
                if (mode == 2) {
                    snd_play(gSfxBank, SFX_STEP_FOOT_A, NULL);
                } else if (mode == 6) {
                    snd_play(gSfxBank, SFX_STEP_FOOT_B, NULL);
                }
            }
        }
        if (speed < 0) {
            speed = -speed;
        }
        anim_set_speed((AnimChannel *) base, 1, (u32) speed / 11);
        anim_play((AnimChannel *) base, 1, -1);
    }
    gDriverState[0xA1] = 1;
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/69BB0/driver_update_anims.s")
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
extern u8 * N64P gDriverModel;  /* vehicle 0's model header */
extern u8 * N64P gDriverMtxA;  /* vehicle 0's model buffers */
extern u8 * N64P gDriverMtxB;

/* The driver's model matrix and parts (0x802AFA64).
 * Places vehicle 0's model ($gp = gDriverState, read directly): rotation
 * (0, +0x4C, 0) and position gDriverPos..810 at scale 0x4E20 into the matrix
 * at header word [header word 0x18 + 4] of the current buffer (mtx_build_model),
 * then its parts (veh_place_parts, tag 0; header words 4 and 8 give the part
 * list, relative to the header).
 * veh_place_parts's register in/outs (only used for a part with no matrices):
 * s2 = the matrix (mtx_build_model leaves it there); a3, s1, s0 are whatever the
 * asm callers had (0 here; fidelity note). The asm's add traps on overflow.
 * Returns the end of the part list (the asm leaves it in t2, and t0 = 0; both
 * asm callers pass them on to veh_collision_setup as its h2/b0 inputs;
 * conventions.txt). Asm callers keep t6 (driver_check_exit_point) and t7
 * (driver_update) live; a mixed N64 build would need a thunk. */
u8 *driver_build_model(void) {
    u8 *hdr = gDriverModel;
    s32 *pos = (s32 *) gDriverPos;
    u8 *m = *(u8 * N64P *) (hdr + *(s32 *) (hdr + 0x18) + 4);
    MtxChainRegs regs;

    m += (N64_A32) (gBufferIndex != 0 ? gDriverMtxA : gDriverMtxB);
    gModelRot[0] = 0;
    gModelRot[2] = 0;
    gModelRot[1] = *(u16 *) (gDriverState + 0x4C);
    mtx_build_model(pos[0], pos[1], pos[2], 0x4E20, (s32 *) m);
    regs.a3 = 0;
    regs.s1 = 0;
    regs.s2 = (s32) m;
    regs.s0 = 0;
    veh_place_parts(pos[0], pos[1], pos[2], 0, hdr + *(s32 *) (hdr + 4), hdr + *(s32 *) (hdr + 8),
                  gBufferIndex != 0 ? gDriverMtxA : gDriverMtxB, &regs);
    return hdr + *(s32 *) (hdr + 8);
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/69BB0/driver_build_model.s")
#endif

/* driver_steer_rate: sets $s3 = 0x8c directly (`addiu $s3, $zero, 0x8c`)
 * inside the same dead 8-byte `sd $ra` frame as train_exit_check/
 * barge_exit_check/coll_reset_dyn_tris/veh_check_wrap - same confirmed hand-
 * written family, and doubly so here since C has no way to pin a value
 * to a specific callee-saved register by name regardless. Value is
 * never read or returned; likely a vestigial/debug leftover in the
 * original assembly. Permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
/* The driver's steering rate (0x8C) (0x802AFB84).
 * Returns 0x8C (asm: in s3, which asm caller driver_update reads; see
 * tools_port/conventions.txt). That caller keeps a0-a3, f12 and f14 live
 * across the call (a mixed N64 build would need a thunk). */
s32 driver_steer_rate(void) {
    return 0x8C;
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/69BB0/driver_steer_rate.s")
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING

/* Gravity x4, landing bounce 40/3 (0x802AFBA0).
 * Vehicle-module setup leaf: gGravity = gLevelGravity * 4, then the byte pair
 * gBounceThreshold/Damping = 40, 3. Same shape as sideswipe_set_physics (6B4A0), thunderfist_set_physics
 * (6C5E0) and, with other constants, amdream_set_physics (72B80), ramdozer_set_physics
 * (6E200), backlash_set_physics (71140).
 * Register note: the asm touches only at/v0/v1/f0/f2. Its asm caller
 * driver_update keeps a0-a3, f12 and f14 live across the call; a mixed N64
 * build would need a thunk preserving those, the native port does not. */
void driver_set_physics(void) {
    gGravity = gLevelGravity * 4.0f;
    gBounceThreshold = 40;
    gBounceDamping = 3;
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/69BB0/driver_set_physics.s")
#endif

/* driver_save_state: two-address trampoline into veh_serialize_state, same
 * confirmed-unreachable-from-C 8-byte sd-$ra frame as veh_check_wrap
 * (hd_code/679E0.c). Permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
extern u8 gDriverState[];  /* this vehicle's state block: 0xA6 bytes are (de)serialized */

/* The driver's state into a buffer (no callers) (0x802AFBFC).
 * Serialize this vehicle's state (gDriverState[0..0xA5] plus the three words
 * gDriverPos[0..2]) into `dst` via veh_serialize_state. Returns the callee's result
 * (0xB2, the byte count): the asm leaves the callee's v0 untouched. */
s32 driver_save_state(u8 *dst) {
    return veh_serialize_state(dst, gDriverState, gDriverPos);
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/69BB0/driver_save_state.s")
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
/* The driver's state from a buffer (recording start) (0x802AFC28).
 * Inverse of driver_save_state: restore this vehicle's state from `src` via
 * veh_deserialize_state. Called from 17210.c as `void driver_load_state(void *)`. */
void driver_load_state(void *src) {
    veh_deserialize_state(src, gDriverState, gDriverPos);
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/69BB0/driver_load_state.s")
#endif
