#include "common.h"
#include <ultra64.h>
#include "game/game.h"
/* Views: this file reads these shared variables (game/variables.h) as other types. */
#ifdef NON_MATCHING
#define gHeapPtr (*(s32 *) &gHeapPtr)
#define gModelRot (*(s16 *) gModelRot)
#endif
/* end of views */

#ifdef NON_MATCHING
/* Register-block types shared by this file's rewrites (each mirrors the
 * callee's definition in 62740.c / 60F60.c; later per-function copies are
 * guarded out). */
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
extern u8 * N64P gCarrierMtxA;   /* the 0xFF object's two 0xC00-byte matrix buffers */
extern u8 * N64P gCarrierMtxB;
extern u8 * N64P gCarrierModel;   /* its model header */
extern u8 gCarrierState[];  /* its state block */
extern u8 gCarrierAnim[];  /* its animation channels */
extern s32 gCarrierStartX;   /* start point x, z */
extern s16 gCarrierLastSoundSpeed;
extern u8 gCarrierExplodeDelay;
extern u8 gCarrierExplodeFrames;
extern u8 gCarrierExplodeStarted;
extern u8 gCarrierTexAnim[];  /* its anim_tex_find entry key */
void carrier_load_collide_dists(void);
s32 carrier_build_model(MtxChainRegs *regs);

/* Sets up the missile carrier (type 0xFF, lvl_spawn_carrier) (0x802B9C50).
 * Level setup of the type-0xFF object (asm caller lvl_spawn_carrier): model
 * header -> gCarrierModel, two 0xC00-byte matrix buffers from gHeapPtr
 * (gCarrierMtxA / gCarrierMtxB; the allocator advances by 0x1800),
 * veh_add_record(0xFF, 0, buffers, model); position x/z (and the start point
 * gCarrierStartX/Z), target speed gCarrierTargetSpeed, gCarrierRouteLen = limit, gCarrierLastSoundSpeed =
 * 0; veh_reset_state on gCarrierState, heading +0x4E/+0x4C; effect counters
 * gCarrierExplodeDelay = 5, gCarrierExplodeFrames = gCarrierExplodeStarted = 0; carrier_load_collide_dists (level spawn
 * point); wheel-slot offsets (+-0x190 / 0x4B0, +-0x1A4 / 0x4D8) at
 * +0x52..+0x68; the ground slots (veh_init_ground at y 0x7FFF, writing y to
 * gCarrierY, key 0xFF); the model channels (anim_init_model on gCarrierAnim,
 * channel 0 set up, both buffers animated); parts (veh_register_parts, tag 0xFF,
 * scale 0x59D8); veh_add_shadow(0xFF, 0x96, 0x3C, 0x3C); the model rebuild
 * carrier_build_model; one update with +0x9A set (carrier_update); the root matrix
 * copied from buffer B to A (mtx_copy_at); channels 1 and 2 reset and
 * restarted with -1; the gCarrierTexAnim entry set (0x64, 0, 1) and restarted.
 * The asm's addi/add trap on overflow.
 * Register convention (conventions.txt): model s2, x t4, z t5, target speed
 * s1, limit t7, heading t6, and fp, which the asm hands to veh_init_ground
 * (reaching gSlotSurface when a slot's grid scan finds no triangle: the
 * dispatcher's leftover fp, FIDELITY AUDIT). veh_init_ground's FP inputs (the
 * dispatcher's, passed through) and carrier_build_model's chain inputs (a3 left by
 * the C veh_add_shadow, s1 / s0 = 0 from anim_init_model; only read for a
 * zero-matrix point) are 0 here. The asm restores t0-t5 and points $gp at
 * gCarrierState (clobbers). */
void carrier_spawn(u8 *model, s32 x, s32 z, s32 speed, s32 limit, s32 heading, s32 fp) {
    TriSideOut f;
    MtxChainRegs regs;
    s32 buf;
    u8 *hdr;

    gCarrierModel = model;
    buf = gHeapPtr;
    gCarrierMtxA = (u8 *) N64_IPTR(buf);
    gCarrierMtxB = (u8 *) N64_IPTR((buf + 0xC00));
    gHeapPtr = buf + 0x1800;
    veh_add_record(VEH_CARRIER, 0, (s32) gCarrierMtxA, (s32) gCarrierMtxB, model);
    gCarrierX = x;
    gCarrierStartX = x;
    gCarrierZ = z;
    gCarrierStartZ = z;
    gCarrierTargetSpeed = speed;
    gCarrierLastSoundSpeed = 0;
    gCarrierRouteLen = limit;
    veh_reset_state(gCarrierState);
    *(s16 *) (gCarrierState + 0x4E) = heading;
    *(s16 *) (gCarrierState + 0x4C) = heading;
    gCarrierExplodeDelay = 5;
    gCarrierExplodeFrames = 0;
    gCarrierExplodeStarted = 0;
    carrier_load_collide_dists();
    /* wheel slot offsets */
    *(s16 *) (gCarrierState + 0x52) = 0x190;
    *(s16 *) (gCarrierState + 0x54) = 0x4B0;
    *(s16 *) (gCarrierState + 0x56) = -0x190;
    *(s16 *) (gCarrierState + 0x58) = 0x4B0;
    *(s16 *) (gCarrierState + 0x5A) = 0x190;
    *(s16 *) (gCarrierState + 0x5C) = -0x4B0;
    *(s16 *) (gCarrierState + 0x5E) = 0x1A4;
    *(s16 *) (gCarrierState + 0x60) = 0x4D8;
    *(s16 *) (gCarrierState + 0x62) = -0x1A4;
    *(s16 *) (gCarrierState + 0x64) = 0x4D8;
    *(s16 *) (gCarrierState + 0x66) = 0x1A4;
    *(s16 *) (gCarrierState + 0x68) = -0x4D8;
    f.pz = 0.0f;
    f.cross = 0.0f;
    f.cz = 0.0f;
    f.side = 0.0f;
    f.sideZ = 0.0f;
    f.dz = 0.0f;
    veh_init_ground((s16 *) (gCarrierState + 0x52), 0x7FFF, gCarrierX, gCarrierZ, (s32 *) (gCarrierState + 4),
                  &gCarrierY, (s16 *) (gCarrierState + 0x4C), 0xFF, fp, gCarrierState, &f);
    hdr = gCarrierModel;
    anim_init_model((u32 *) gCarrierMtxB, (u32 *) gCarrierMtxA, (AnimChannel *) gCarrierAnim, hdr);
    anim_set_speed((AnimChannel *) gCarrierAnim, 0, 0x64);
    anim_set_reverse((AnimChannel *) gCarrierAnim, 0, 0);
    anim_set_loop_mode((AnimChannel *) gCarrierAnim, 0, 0);
    anim_set_interp(0.0f, (AnimChannel *) gCarrierAnim, 0, 0);
    anim_play((AnimChannel *) gCarrierAnim, 0, 1);
    anim_update_model(gCarrierMtxA, gCarrierMtxB, (AnimChannel *) gCarrierAnim);
    anim_rewind(0, (AnimChannel *) gCarrierAnim);
    anim_play((AnimChannel *) gCarrierAnim, 0, 1);
    anim_update_model(gCarrierMtxB, gCarrierMtxA, (AnimChannel *) gCarrierAnim);
    hdr = gCarrierModel;
    veh_register_parts(0xFF, hdr + *(s32 *) (hdr + 4), hdr + *(s32 *) (hdr + 8), 0x59D8);
    veh_add_shadow(VEH_CARRIER, 0x96, 0x3C, 0x3C);
    regs.a3 = 0;
    regs.s1 = 0;
    regs.s0 = 0;
    carrier_build_model(&regs);
    gCarrierState[0x9A] = 1;
    carrier_update();
    gCarrierState[0x9A] = 0;
    hdr = gCarrierModel;
    mtx_copy_at(gCarrierMtxB, gCarrierMtxA, *(s32 *) (hdr + *(s32 *) (hdr + 0x18) + 4));
    anim_set_speed((AnimChannel *) gCarrierAnim, 1, 0);
    anim_set_reverse((AnimChannel *) gCarrierAnim, 1, 0);
    anim_set_loop_mode((AnimChannel *) gCarrierAnim, 1, 0);
    anim_play((AnimChannel *) gCarrierAnim, 1, -1);
    anim_set_speed((AnimChannel *) gCarrierAnim, 2, 0);
    anim_set_reverse((AnimChannel *) gCarrierAnim, 2, 0);
    anim_set_loop_mode((AnimChannel *) gCarrierAnim, 2, 1);
    anim_play((AnimChannel *) gCarrierAnim, 2, -1);
    anim_tex_set_speed((s32) gCarrierTexAnim, 0x64);
    anim_tex_set_reverse((s32) gCarrierTexAnim, 0);
    anim_tex_set_loop_mode((s32) gCarrierTexAnim, 1);
    anim_tex_play((s32) gCarrierTexAnim, -1);
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/75490/carrier_spawn.s")
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
/* Functional rewrite (tools_port/eqcheck.py verified). Looks up the current
 * level in a table of {level, x, z} records terminated by a negative level
 * and sets the spawn position (x, z in 1/32 units) from the matching record;
 * position stays 0 if the level isn't listed. The asm also saves/restores
 * v0, v1, a0; its asm caller carrier_spawn relies on a1, a3, f12, f14 being
 * preserved (a mixed N64 build would need a thunk; the native port won't). */
typedef struct {
    s16 level;
    u16 x;
    u16 z;
} LevelPos;
extern LevelPos gCarrierCollideDists[];
extern s32 gCarrierWorldHitDist;
extern s32 gCarrierObjHitDist;
extern u8 gCarrierWorldHitOn;
extern u8 gCarrierObjHitOn;

/* Per-level distances after which it collides with world / objects (0x802BA074). */
void carrier_load_collide_dists(void) {
    LevelPos *e;
    s32 level;

    gCarrierWorldHitDist = 0;
    gCarrierWorldHitOn = 0;
    gCarrierObjHitDist = 0;
    gCarrierObjHitOn = 0;
    level = levelno;
    for (e = gCarrierCollideDists; e->level >= 0; e++) {
        if (e->level == level) {
            gCarrierWorldHitDist = e->x << 5;
            gCarrierObjHitDist = e->z << 5;
            return;
        }
    }
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/75490/carrier_load_collide_dists.s")
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
/* Veh_zone_alpha at its position (0x802BA104).
 * Zone level lookup (veh_zone_alpha) for vehicle id 0xFF at its position
 * gCarrierX..+8; returns veh_zone_alpha's v1 (the zone list end).
 * Register convention: the asm passes veh_zone_alpha's scan registers t6, t7,
 * s0-s4 through (in and out; here through r), leaves a3 = id and clobbers
 * s5, s6 (conventions.txt). Asm caller carrier_update keeps f12 and f14 live
 * (a mixed N64 build would need a thunk; the native port won't). */
s32 carrier_zone_alpha(ZoneScanRegs *r) {
    return veh_zone_alpha(0xFF, gCarrierX, gCarrierY, gCarrierZ, r);
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/75490/carrier_zone_alpha.s")
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
/* Rounded 3-D distance from (ax, ay, az) to (bx, by, bz) (62740.c; asm
 * convention in tools_port/conventions.txt: t3-t5, t6, t7, s0 -> s1). */

extern void * N64P gCarrierSound;  /* this sound's handle, NULL = none */

/* Its loop sound 0x75 (0x802BA148).
 * Positional sound 0x75 at gCarrierX/Y/Z (called from hd.c); shape of
 * chopper_update_sound (72B80). d is the rounded distance from the player
 * gPlayerX/Y/Z. Beyond 16000 the sound is stopped (snd_stop) and
 * its handle cleared. Otherwise it is started if it isn't playing, its
 * volume (parameter 8) set to 0x7FFF - 2 * max(d - 4000, 0) and its pan
 * (parameter 4) to 64 + (player x - source x) / 32, clamped to 0..127. The
 * asm's `sub`/`add` trap on overflow; C doesn't. Register note: the asm
 * saves and restores every register. */
void carrier_update_sound(void) {
    s32 dx = gPlayerX - gCarrierX;
    s32 dist;
    s32 pan;

    dist = math_dist3(gPlayerX, gPlayerY, gPlayerZ, gCarrierX, gCarrierY, gCarrierZ);
    if (dist > 16000) {
        if (gCarrierSound != NULL) {
            snd_stop(gCarrierSound);
            gCarrierSound = NULL;
        }
        return;
    }
    if (gCarrierSound == NULL) {
        snd_play(gSfxBank, SFX_CARRIER_LOOP, &gCarrierSound);
    }
    dist -= 4000;
    if (dist < 0) {
        dist = 0;
    }
    snd_modify(gCarrierSound, 8, 0x7FFF - (dist << 1));
    pan = 0x40 + (dx >> 5);
    if (pan < 0) {
        pan = 0;
    } else if (pan >= 0x80) {
        pan = 0x7F;
    }
    snd_modify(gCarrierSound, 4, pan);
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/75490/carrier_update_sound.s")
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
extern u8 gCarrierState[];
extern u8 gCarrierAnim[];
extern u8 * N64P gCarrierMtxA;
extern u8 * N64P gCarrierMtxB;
s32 carrier_check_collide_dists(void);
s32 carrier_update_anims(void);
s32 carrier_scan_speed_zones(s32 *termZ, s32 *lastLevel, s32 *level);
void carrier_set_physics(void);
s32 carrier_build_model(MtxChainRegs *regs);
void carrier_check_crash(s32 h2);
void carrier_check_arrival(void);

#define VEH_BA354(off, T) (*(T *) (gCarrierState + (off)))

/* The carrier, per frame: drive along the route, collide, finish (0x802BA354).
 * Per-frame update of the type-0xFF object gCarrierState (called from hd.c's
 * hd_game_frame and from carrier_spawn): zone lookup (carrier_zone_alpha), the
 * distance flags (carrier_check_collide_dists); then, while byte +0x9A is clear, the engine
 * sound/animation (carrier_update_anims), skipping the movement when gCarrierHit or
 * gPaused is set. Movement: the zone value scan (carrier_scan_speed_zones, which may
 * set the target speed gCarrierTargetSpeed); the speed (s16 +0x76) rises by 8 towards
 * gCarrierTargetSpeed (never above it) when below it; carrier_set_physics; the drive step
 * f = veh_slope, veh_apply_slope(kind 0xFF, divisor 2400.0); the new
 * position veh_move_along_heading(f, heading u16 +0x4E, speed, x, z); gBounceSound = 1;
 * ground contact veh_ground_contact (id 0xFF, divisors 0x960 / 0x320). Always:
 * animate the model (anim_update_model on the current buffer pair), rebuild it
 * (carrier_build_model), the collision/effect step carrier_check_crash while +0x9A is
 * clear, carrier_check_arrival and veh_set_record(z, 0xFF, x, y, veh).
 * Leaked registers (FIDELITY AUDIT, see port_followups): the zone scan's
 * t6/t7/s0-s4 inputs (the caller's registers; only passed through when no
 * zone is looked at, and dead afterwards), carrier_scan_speed_zones's a2 input and its
 * a0-a3 results (dead: veh_move_along_heading doesn't read a0-a3), the chain
 * registers a3/s1/s0 going into carrier_build_model (only read for a zero-matrix
 * point, which the model data doesn't have) and veh_ground_contact's FP inputs
 * other than f12 (f14, f20-f26: the caller's or earlier callees' leftovers,
 * passed through) are 0 here. The asm's `addi` traps on overflow. The asm
 * saves every s- and FP register it changes; it returns with f12/f14/v1 as
 * its last callees leave them (read by carrier_spawn; not modelled). */
void carrier_update(void) {
    ZoneScanRegs zr;
    MtxChainRegs regs;
    Regs802A8768 r;
    TriSideOut f;
    Out802A860C out;
    s32 termZ;
    s32 lastLevel;
    s32 level;
    s32 speed;
    s32 x;
    f32 step;
    s32 end;

    zr.t6 = 0;
    zr.t7 = 0;
    zr.s0 = 0;
    zr.s1 = 0;
    zr.s2 = 0;
    zr.s3 = 0;
    zr.s4 = 0;
    carrier_zone_alpha(&zr);
    carrier_check_collide_dists();
    if ((s8) gCarrierState[0x9A] == 0) {
        carrier_update_anims();
        if ((s8) gCarrierHit != 0 || (s8) gPaused != 0) {
            goto animate;
        }
    }
    lastLevel = 0;
    carrier_scan_speed_zones(&termZ, &lastLevel, &level);
    speed = VEH_BA354(VS_SPEED, s16);
    if (speed < gCarrierTargetSpeed) {
        speed += 8;
        if (gCarrierTargetSpeed < speed) {
            speed = gCarrierTargetSpeed;
        }
    }
    VEH_BA354(VS_SPEED, s16) = speed;
    carrier_set_physics();
    step = veh_slope(&VEH_BA354(VS_SPEED, s16), gCarrierState + 0x96, &VEH_BA354(4, s32), (f32 *) gCarrierState);
    veh_apply_slope(gCarrierState, &VEH_BA354(VS_SPEED, s16), 0xFF, (s8 *) (gCarrierState + 0x96), &VEH_BA354(4, s32), 0,
                  2400.0f);
    x = veh_move_along_heading(step, VEH_BA354(VS_MOVE_HEADING, u16), &VEH_BA354(VS_SPEED, s16), &gCarrierX, &gCarrierZ, &out);
    gBounceSound = 1;
    r.s3 = out.s3;
    f.pz = step;
    f.cross = 0.0f;
    f.cz = 0.0f;
    f.side = 0.0f;
    f.sideZ = 0.0f;
    f.dz = 0.0f;
    veh_ground_contact(gCarrierState, 0xFF, &gCarrierX, &gCarrierY, &gCarrierZ, x, out.t1, 0x960, 0x320,
                  &VEH_BA354(VS_HEADING, s16), gCarrierState + 0x96, &VEH_BA354(VS_WHEEL_OFFSETS, s16), &VEH_BA354(VS_SLOT_LAUNCH_VEL, s32),
                  &VEH_BA354(VS_SLOT_AIR_TIME, s32), &VEH_BA354(VS_SLOT_LAUNCH_Y, s32), &VEH_BA354(4, s32), &r, &f);

animate:
    if (gBufferIndex != 0) {
        anim_update_model(gCarrierMtxA, gCarrierMtxB, (AnimChannel *) gCarrierAnim);
    } else {
        anim_update_model(gCarrierMtxB, gCarrierMtxA, (AnimChannel *) gCarrierAnim);
    }
    regs.a3 = 0;
    regs.s1 = 0;
    regs.s0 = 0;
    end = carrier_build_model(&regs);
    if ((s8) gCarrierState[0x9A] == 0) {
        carrier_check_crash(end);
    }
    carrier_check_arrival();
    veh_set_record(gCarrierZ, 0xFF, gCarrierX, gCarrierY, gCarrierState);
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/75490/carrier_update.s")
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
extern s32 gCarrierStartX; /* second point x, z (y = 0) */

/* The distance from the start turns its collisions on (0x802BA5A4).
 * Distance (math_dist3, y = 0) from (gCarrierX, gCarrierZ) to
 * (gCarrierStartX, gCarrierStartZ); sets gCarrierWorldHitOn when it is >= gCarrierWorldHitDist and
 * gCarrierObjHitOn when it is >= gCarrierObjHitDist (signed). Returns the distance (the
 * asm leaves it in s1; conventions.txt). The asm also leaves t7 = 0 and
 * s0 = gCarrierStartZ; asm caller carrier_update keeps a2, a3, f12, f14 live
 * (a mixed N64 build would need a thunk; the native port won't). */
s32 carrier_check_collide_dists(void) {
    s32 dist = math_dist3(gCarrierX, 0, gCarrierZ, gCarrierStartX, 0, gCarrierStartZ);

    if (dist >= gCarrierWorldHitDist) {
        gCarrierWorldHitOn = 1;
    }
    if (dist >= gCarrierObjHitDist) {
        gCarrierObjHitOn = 1;
    }
    return dist;
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/75490/carrier_check_collide_dists.s")
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
typedef struct {
    s16 z;    /* negative ends the table */
    u8 level;
    u8 value;
} ZoneEntry;
extern ZoneEntry gCarrierSpeedZones[];

/* Per-level z ranges set its target speed (0x802BA638).
 * Scans gCarrierSpeedZones (terminated by a negative z) for entries of the current
 * level (levelno) whose z <= player z / 32 (gCarrierZ >> 5); the last
 * one's value wins (0 if none). A nonzero result is stored in gCarrierTargetSpeed.
 * The asm leaves its scan registers behind and asm caller carrier_update
 * doesn't overwrite them before veh_move_along_heading reads a0-a3, so they are all
 * outputs (see tools_port/conventions.txt): result (a0), the terminator z
 * (a1), the last level byte read (a2, unchanged if the table is empty) and
 * the current level (a3). */
s32 carrier_scan_speed_zones(s32 *termZ, s32 *lastLevel, s32 *level) {
    s32 pz = gCarrierZ >> 5;
    s32 lv = levelno;
    s32 result = 0;
    ZoneEntry *e;
    s32 z;

    for (e = gCarrierSpeedZones; (z = e->z) >= 0; e++) {
        *lastLevel = e->level;
        if (e->level == lv && !(pz < z)) {
            result = e->value;
        }
    }
    if (result != 0) {
        gCarrierTargetSpeed = result;
    }
    *termZ = z;
    *level = lv;
    return result;
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/75490/carrier_scan_speed_zones.s")
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

extern u8 gCarrierState[];  /* this vehicle's state block (the asm's $gp) */
extern s16 gCarrierLastSoundSpeed;   /* last speed sent to the engine sound */
extern f32 gCarrierPitchScale;
extern f32 gCarrierYawThirdWidth;
extern f32 gCarrierWheelAnimScale;
extern u8 gCarrierAnim[];  /* animation channel table (AnimChannel, 56040.c) */

/* Engine pitch, wheel channel, the camera-angle channel (0x802BA6AC).
 * Engine sound and animation channels for vehicle gCarrierState (speed = s16 at
 * +0x76). When the speed changed since the last call (gCarrierLastSoundSpeed) and the
 * sound gCarrierSound is playing, its parameter 0x10 is set to the float
 * 1.5 + speed * gCarrierPitchScale (passed as raw bits). Then a = (gCamYaw +
 * 0x800) wrapped by -0xFFF at 0x1000; channel 2 gets value a / 0x555
 * (0, 1, else 2) and fraction (a % 0x555) / gCarrierYawThirdWidth (anim_set_frame);
 * channel 1 gets round(speed * gCarrierWheelAnimScale) (0 while gPaused is set,
 * anim_set_speed). Returns a / 0x555 (the asm's s4; conventions.txt). The asm
 * also clobbers s5; asm caller carrier_update keeps a2, a3, t7, f12, f14 live
 * (a mixed N64 build would need a thunk; the native port won't). The asm's
 * `addi` traps on overflow, which can't happen for a u16. */
s32 carrier_update_anims(void) {
    s32 speed = *(s16 *) (gCarrierState + 0x76);
    s16 last = gCarrierLastSoundSpeed;
    u32 a;
    u32 q;
    s32 v;
    f32 frac;

    gCarrierLastSoundSpeed = speed;
    if (last != speed && gCarrierSound != NULL) {
        f32 p = 1.5f + (f32) speed * gCarrierPitchScale;

        snd_modify(gCarrierSound, 0x10, *(s32 *) &p);
    }
    a = ((u16) gCamYaw) + 0x800;
    if ((s32) a >= 0x1000) {
        a -= 0xFFF;
    }
    q = a / 0x555;
    frac = (f32) (s32) (a % 0x555) / gCarrierYawThirdWidth;
    if (q == 0) {
        anim_set_frame(frac, (AnimChannel *) gCarrierAnim, 2, 0);
    } else if (q == 1) {
        anim_set_frame(frac, (AnimChannel *) gCarrierAnim, 2, 1);
    } else {
        anim_set_frame(frac, (AnimChannel *) gCarrierAnim, 2, 2);
    }
    v = gPaused != 0 ? 0 : *(s16 *) (gCarrierState + 0x76);
    CVT_W_S(v, (f32) v * gCarrierWheelAnimScale);
    anim_set_speed((AnimChannel *) gCarrierAnim, 1, v);
    return q;
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/75490/carrier_update_anims.s")
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING

/* The route length reached: the level is done (0x802BA91C).
 * Distance (math_dist3, y = 0) from (gCarrierX, gCarrierZ) to
 * (gCarrierStartX, gCarrierStartZ); when it is >= gCarrierRouteLen (signed) sets
 * gLevelDoneRequest and gPauseRequest to 1. The asm leaves the distance in s1 and
 * gCarrierStartZ in s0 (conventions.txt: clobbers s0, s1); asm caller
 * carrier_update keeps a2, a3, f12, f14 live (a mixed N64 build would need a
 * thunk; the native port won't). */
void carrier_check_arrival(void) {
    s32 dist = math_dist3(gCarrierX, 0, gCarrierZ, gCarrierStartX, 0, gCarrierStartZ);

    if (dist >= gCarrierRouteLen) {
        gLevelDoneRequest = 1;
        gPauseRequest = 1;
    }
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/75490/carrier_check_arrival.s")
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
extern u8 gCarrierNonBlockingParts[];
extern u8 gFxCarrierExplosion[]; /* fx_spawn definition */
extern u8 gCarrierExplodeDelay;   /* countdown before the effect */
extern u8 gCarrierExplodeFrames;   /* effect frame count */
extern u8 gCarrierExplodeStarted;   /* effect started */

#define ABS_BA9A0(v) ((v) < 0 ? -(v) : (v))

/* Hit or tilted: cmo_hit_request; explosion effects (0x802BA9A0).
 * Collision/effect step of the type-0xFF object (gCarrierState, the asm's $gp).
 * Unless gCarrierHit is set: veh_collision_setup((x, y, z) = gCarrierX/Y/Z, table
 * gCarrierNonBlockingParts, 0, 0, b0, speed (s16 +0x76), h2, 0, 0xFF), veh_collide_spheres(0xFF, gCarrierState);
 * then, unless gCarrierContact is set, gCollHit = 0, veh_collide_world(0xFF) when
 * gCarrierWorldHitOn is set and obj_collide_vehicle(0xFF, veh) (with gCollVehicleAnim =
 * &gCarrierAnim) when gCarrierObjHitOn is set; if gCollHit is then still 0 and the
 * three height pairs at +4/+8, +0x10/+0x14, +0x1C/+0x20 each differ by at most
 * 10000, it returns. Otherwise cmo_hit_request = gPauseRequest = 1. Then, unless
 * gCarrierHitSeen is set: hud_panel_stop, anim_stop(1, &gCarrierAnim),
 * fx_finish_all, obj_clear_effect_queue. Finally the effect: gCarrierExplodeDelay counts down
 * first; then, until gCarrierExplodeDone is set, the first frame sets up a
 * fx_spawn record (def gFxCarrierExplosion, data 0x7A120, type 1 at (0xFF, 2, 0))
 * and sets gCarrierExplodeStarted, and later frames count gCarrierExplodeFrames up to 0x12, which
 * sets gCarrierExplodeDone.
 * Leaked registers (kept): b0 is the asm's t0 = &gCarrierHit (so gImpactEffectScale
 * gets 0xD6) and h2 its t2, carrier_build_model's leftover point-list end pointer
 * (its low half goes to gImpactMinSpeed), passed in here as `h2`. The
 * fx_spawn call (type 1, z = 0) ignores the asm's other leftover
 * registers (t6, t7, s0-s4, the a3 tag aside), so they are 0 here. The asm's
 * `sub`/`addi` trap on overflow.
 * Register convention: h2 in t2 (conventions.txt); the asm reads the height
 * records through s7 = veh + 4 (always, from its one caller carrier_update)
 * and leaves its callees' registers behind (clobbers). */
void carrier_check_crash(s32 h2) {
    s32 *ys = (s32 *) (gCarrierState + 4);
    Io802A6274 io;
    s32 n;

    if (gCarrierHit == 0) {
        veh_collision_setup(gCarrierZ, (s32) gCarrierNonBlockingParts, 0, 0, gCarrierX, gCarrierY, (s32) &gCarrierHit,
                      *(s16 *) (gCarrierState + 0x76), h2, 0, 0xFF, gCarrierState);
        veh_collide_spheres(0xFF, gCarrierState);
        if (((u8) gCarrierContact) == 0) {
            gCollHit = 0;
            if (gCarrierWorldHitOn != 0) {
                veh_collide_world(0xFF);
            }
            if (gCarrierObjHitOn != 0) {
                gCollVehicleAnim = gCarrierAnim;
                obj_collide_vehicle(0xFF, gCarrierState);
            }
            if (gCollHit == 0 && ABS_BA9A0(ys[1] - ys[0]) < 0x2711 && ABS_BA9A0(ys[4] - ys[3]) < 0x2711 &&
                ABS_BA9A0(ys[7] - ys[6]) < 0x2711) {
                return;
            }
        }
        cmo_hit_request = 1;
        gPauseRequest = 1;
    }
    if (gCarrierHitSeen == 0) {
        hud_panel_stop();
        anim_stop(1, (AnimChannel *) gCarrierAnim);
        fx_finish_all();
        obj_clear_effect_queue();
    }
    if (gCarrierExplodeDelay != 0) {
        gCarrierExplodeDelay--;
        return;
    }
    if (gCarrierExplodeDone != 0) {
        return;
    }
    if (gCarrierExplodeStarted == 0) {
        io.a3 = 1;
        io.t6 = 0;
        io.s1 = 0;
        fx_spawn(&io, gFxCarrierExplosion, 0x7A120, 1, 0xFF, 2, 0, 0, 0, 0, 0, 0, 0);
        gCarrierExplodeStarted = 1;
        return;
    }
    n = gCarrierExplodeFrames + 1;
    gCarrierExplodeFrames = n;
    if (n == 0x12) {
        gCarrierExplodeDone = 1;
    }
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/75490/carrier_check_crash.s")
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
extern u8 * N64P gCarrierMtxA; /* matrix buffer pair */
extern u8 * N64P gCarrierMtxB;
extern u8 * N64P gCarrierModel; /* model header */

/* Its model matrix, parts and points (0x802BABEC).
 * Rebuilds the type-0xFF object's model matrices and points, as
 * backlash_build_model (71140) does for vehicle 5: header gCarrierModel, buffers
 * gCarrierMtxA (gBufferIndex set) / gCarrierMtxB, rotation y = u16 +0x4C of
 * gCarrierState (the asm's $gp; both callers point it there) in gModelRotY, root
 * matrix at gCarrierX/Y/Z with scale 0x59D8 (mtx_build_model), parts tag
 * 0xFF (veh_place_parts) and points id 0xFF (veh_points_transform).
 * Register convention: mtx_chain_transform's chain registers pass through regs
 * (a3, s1, s0 in; v1, a0, a3, s1, s2 out), s2 = the root matrix and v1/a0 =
 * position y/z going into veh_points_transform as in the asm. Returns the asm's t2,
 * the point list's end (veh_points_transform preserves it), which caller
 * carrier_update hands on to carrier_check_crash (conventions.txt). The asm also
 * leaves s4 = the buffer, s5/s6 = position y/z, s7 = 0x59D8 and
 * mtx_build_model's f12/f14 scratch (clobbers; not read). Asm callers keep t6,
 * t7 live. */
s32 carrier_build_model(MtxChainRegs *regs) {
    u8 *hdr = gCarrierModel;
    u8 *buf = gBufferIndex != 0 ? gCarrierMtxA : gCarrierMtxB;
    s32 *m = (s32 *) (*(s32 *) (hdr + *(s32 *) (hdr + 0x18) + 4) + buf);
    u8 *end;

    gModelRotY = *(u16 *) (gCarrierState + 0x4C);
    mtx_build_model(gCarrierX, gCarrierY, gCarrierZ, 0x59D8, m);
    regs->s2 = (s32) m;
    buf = gBufferIndex != 0 ? gCarrierMtxA : gCarrierMtxB;
    hdr = gCarrierModel;
    veh_place_parts(gCarrierX, gCarrierY, gCarrierZ, 0xFF, hdr + *(s32 *) (hdr + 4), hdr + *(s32 *) (hdr + 8),
                  buf, regs);
    regs->v1 = gCarrierY;
    regs->a0 = gCarrierZ;
    hdr = gCarrierModel;
    end = hdr + *(s32 *) (hdr + 4);
    veh_points_transform(0xFF, (s16 *) (hdr + *(s32 *) hdr), (s16 *) end, buf, regs);
    return (s32) end;
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/75490/carrier_build_model.s")
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
/* Functional rewrite (tools_port/eqcheck.py verified). Vehicle "set timers"
 * leaf, the shape shared by train_set_physics, barge_set_physics, ballista_set_physics,
 * police_set_physics, ateam_set_physics, starski_set_physics and cyclone_set_physics (only the
 * scale and the two byte values differ). Asm caller carrier_update relies on
 * a0-a3 being preserved (mixed N64 build would need a thunk). */

/* Gravity x2, bounce 60/10 (0x802BAD24). */
void carrier_set_physics(void) {
    gGravity = gLevelGravity * 2.0f;
    gBounceThreshold = 60;
    gBounceDamping = 10;
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/75490/carrier_set_physics.s")
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
extern u8 * N64P gCraneModel;    /* vehicle 6's model header */
extern u64 * N64P gCraneMtxA;   /* its two 0x800-byte matrix buffers */
extern u64 * N64P gCraneMtxB;
extern u8 gCraneState[];
extern u32 gCranePos[];
extern u8 gCraneAnim[];

/* Sets up the crane (type 6) (0x802BAD80).
 * Level setup of vehicle 6 (asm caller lvl_spawn_vehicles, the level's object
 * list): model header `model` -> gCraneModel; two 0x800-byte matrix buffers
 * from the allocator gHeapPtr (gCraneMtxA, gCraneMtxB; the allocator
 * advances by 0x1000); veh_add_record(6, 0, buffers, model); $gp record
 * gCraneState reset (veh_reset_state); wheel-slot offsets (+-0x50, +-0x5A) at
 * +0x52..+0x68; position gCranePos = (x, y, z), heading +0x4C/+0x4E/+0x74
 * = `heading`; the three ground slots (veh_init_ground, height records +4, y
 * written back to D_803EFACC, key 6); the model channels (anim_init_model,
 * channel 0 of gCraneAnim set up and both buffers animated); +0xA1 = +0xA2 =
 * 0; parts registered (veh_register_parts, tag 6, scale 0x1B58);
 * veh_add_shadow(6, 0x96, 0x2D, 0x2D); one update with +0x9A set
 * (crane_update); and the root matrix copied from buffer B to A
 * (mtx_copy_at). The asm's add/addi trap on overflow.
 * Register convention (conventions.txt): model s2, x t7, y s3, z s0,
 * heading s1, and fp, which the asm hands to veh_init_ground (its value
 * reaches gSlotSurface when a slot's grid scan finds no triangle: the
 * dispatcher's leftover fp, FIDELITY AUDIT). veh_init_ground's FP inputs
 * (the dispatcher's f12-f26, passed through) are 0 here. The asm restores
 * t0-t5 (lvl_spawn_vehicles keeps t1/t2 live), points $gp at gCraneState and
 * leaves s0-s7, fp and f20-f28 as its callees leave them (clobbers). */
void crane_spawn(u8 *model, s32 x, s32 y, s32 z, s32 heading, s32 fp) {
    TriSideOut f;
    s32 buf;
    u8 *hdr;

    gCraneModel = model;
    buf = gHeapPtr;
    gCraneMtxA = (u64 *) N64_IPTR(buf);
    gCraneMtxB = (u64 *) N64_IPTR((buf + 0x800));
    gHeapPtr = buf + 0x1000;
    veh_add_record(VEH_CRANE, 0, (s32) gCraneMtxA, (s32) gCraneMtxB, model);
    veh_reset_state(gCraneState);
    /* wheel slot offsets (x, z pairs and heights) */
    *(s16 *) (gCraneState + 0x52) = 0x50;
    *(s16 *) (gCraneState + 0x54) = 0x50;
    *(s16 *) (gCraneState + 0x56) = -0x50;
    *(s16 *) (gCraneState + 0x58) = 0x50;
    *(s16 *) (gCraneState + 0x5A) = 0x50;
    *(s16 *) (gCraneState + 0x5C) = -0x50;
    *(s16 *) (gCraneState + 0x5E) = 0x5A;
    *(s16 *) (gCraneState + 0x60) = 0x5A;
    *(s16 *) (gCraneState + 0x62) = -0x5A;
    *(s16 *) (gCraneState + 0x64) = 0x5A;
    *(s16 *) (gCraneState + 0x66) = 0x5A;
    *(s16 *) (gCraneState + 0x68) = -0x5A;
    gCranePos[0] = x;
    gCranePos[1] = y;
    gCranePos[2] = z;
    *(s16 *) (gCraneState + 0x4C) = heading;
    *(s16 *) (gCraneState + 0x4E) = heading;
    *(s16 *) (gCraneState + 0x74) = heading;
    f.pz = 0.0f;
    f.cross = 0.0f;
    f.cz = 0.0f;
    f.side = 0.0f;
    f.sideZ = 0.0f;
    f.dz = 0.0f;
    veh_init_ground((s16 *) (gCraneState + 0x52), gCranePos[1], x, z, (s32 *) (gCraneState + 4),
                  (s32 *) &gCranePos[1], (s16 *) (gCraneState + 0x4C), 6, fp, gCraneState, &f);
    hdr = gCraneModel;
    anim_init_model((u32 *) gCraneMtxB, (u32 *) gCraneMtxA, (AnimChannel *) gCraneAnim, hdr);
    anim_set_speed((AnimChannel *) gCraneAnim, 0, 0x64);
    anim_set_reverse((AnimChannel *) gCraneAnim, 0, 0);
    anim_set_loop_mode((AnimChannel *) gCraneAnim, 0, 0);
    anim_set_interp(0.0f, (AnimChannel *) gCraneAnim, 0, 0);
    anim_play((AnimChannel *) gCraneAnim, 0, 1);
    anim_update_model((u8 *) gCraneMtxA, (u8 *) gCraneMtxB, (AnimChannel *) gCraneAnim);
    anim_rewind(0, (AnimChannel *) gCraneAnim);
    anim_play((AnimChannel *) gCraneAnim, 0, 1);
    anim_update_model((u8 *) gCraneMtxB, (u8 *) gCraneMtxA, (AnimChannel *) gCraneAnim);
    gCraneState[0xA1] = 0;
    gCraneState[0xA2] = 0;
    hdr = gCraneModel;
    veh_register_parts(6, hdr + *(s32 *) (hdr + 4), hdr + *(s32 *) (hdr + 8), 0x1B58);
    veh_add_shadow(VEH_CRANE, 0x96, 0x2D, 0x2D);
    gCraneState[0x9A] = 1;
    crane_update();
    gCraneState[0x9A] = 0;
    hdr = gCraneModel;
    mtx_copy_at((u8 *) gCraneMtxB, (u8 *) gCraneMtxA, *(s32 *) (hdr + *(s32 *) (hdr + 0x18) + 4));
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/75490/crane_spawn.s")
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
extern u8 gCraneAnim[];  /* animation channel table (AnimChannel, 56040.c) */

/* Getting into the crane (0x802BB054).
 * Enter vehicle type 6 (called from hd.c): gCamDistance / gCamHeightBase = 6000, 9000, then
 * sets fields 0x14/0x11/0x12 of channels 2, 4, 5 of gCraneAnim and starts
 * channels 4 and 5 with value -1 (anim_play). The asm also points $gp at
 * gCraneState and leaves it there (conventions.txt: clobbers gp) and leaves
 * v1 = -1; the C caller uses neither. */
void crane_enter(void) {
    gCamDistance = 6000;
    gCamHeightBase = 9000;
    anim_set_speed((AnimChannel *) gCraneAnim, 2, 1);
    anim_set_reverse((AnimChannel *) gCraneAnim, 2, 0);
    anim_set_loop_mode((AnimChannel *) gCraneAnim, 2, 1);
    anim_set_speed((AnimChannel *) gCraneAnim, 4, 0);
    anim_set_reverse((AnimChannel *) gCraneAnim, 4, 0);
    anim_set_loop_mode((AnimChannel *) gCraneAnim, 4, 0);
    anim_play((AnimChannel *) gCraneAnim, 4, -1);
    anim_set_speed((AnimChannel *) gCraneAnim, 5, 0);
    anim_set_reverse((AnimChannel *) gCraneAnim, 5, 0);
    anim_set_loop_mode((AnimChannel *) gCraneAnim, 5, 1);
    anim_play((AnimChannel *) gCraneAnim, 5, -1);
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/75490/crane_enter.s")
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
extern u8 gCraneState[]; /* this vehicle's state block */

/* 1 unless the L/R action is running (0x802BB170).
 * Exit check for vehicle type 6 (called from veh_exit_check in hd.c, which
 * declares it void and returns the leftover v0): 1 when the byte at +0xA1 is
 * 0, else 0. Returns s32 (the asm's v0). The asm also points $gp at
 * gCraneState and leaves it there (conventions.txt: clobbers gp), and leaves
 * the byte in v1; C callers use neither. */
s32 crane_exit_check(void) {
    return gCraneState[0xA1] == 0;
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/75490/crane_exit_check.s")
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
extern u64 * N64P gCraneMtxA;  /* save copy pair (veh_sync_buffers) */
extern u64 * N64P gCraneMtxB;
extern void * N64P gCraneArmSound; /* sound handles, NULL = none */
extern void * N64P gCraneSwingSound;
extern void * N64P gCraneActionSound;

/* Getting out of the crane; stops its three sounds (0x802BB1A0).
 * Leave vehicle type 6 (called from hd.c): veh_sync_buffers(gCraneMtxA,
 * gCraneMtxB, 0x800), then stops the sounds gCraneSwingSound, gCraneArmSound and
 * gCraneActionSound that are playing (snd_stop); the handles are left as they
 * are. The asm saves and restores $gp. */
void crane_leave(void) {
    veh_sync_buffers(gCraneMtxA, gCraneMtxB, 0x800);
    if (gCraneSwingSound != NULL) {
        snd_stop(gCraneSwingSound);
    }
    if (gCraneArmSound != NULL) {
        snd_stop(gCraneArmSound);
    }
    if (gCraneActionSound != NULL) {
        snd_stop(gCraneActionSound);
    }
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/75490/crane_leave.s")
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
extern u32 gCranePos[]; /* x, y, z */
/* Veh_zone_alpha at the crane's position (0x802BB230).
 * Zone level lookup (veh_zone_alpha) for vehicle id 6 at its position
 * gCranePos..+8; returns veh_zone_alpha's v1 (the zone list end).
 * Register convention: the asm passes veh_zone_alpha's scan registers t6, t7,
 * s0-s4 through (in and out; here through r), leaves a3 = id and clobbers
 * s5, s6 (conventions.txt). Asm caller crane_update keeps f12 and f14 live
 * (a mixed N64 build would need a thunk; the native port won't). */
s32 crane_zone_alpha(ZoneScanRegs *r) {
    return veh_zone_alpha(6, gCranePos[0], gCranePos[1], gCranePos[2], r);
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/75490/crane_zone_alpha.s")
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
extern u8 gCraneNonBlockingParts[];
void crane_update_controls(void);
s32 crane_build_model(MtxChainRegs *regs, s32 *t2Out);

/* The crane driven by the player, per frame (0x802BB274).
 * Per-frame update of vehicle 6 (gCraneState; called from hd.c's vehicle
 * switch and from crane_spawn): zone lookup (crane_zone_alpha); the
 * channels/sounds crane_update_controls while byte +0x9A is clear; ground heights
 * of the three wheel slots (veh_update_stand_on at gCranePos[0]/[2], key 6);
 * animate the model (anim_update_model on the current buffer pair, channels
 * gCraneAnim); rebuild it (crane_build_model); clear +0xA2; vehicle-state reset
 * veh_collision_setup (table gCraneNonBlockingParts, kind 6), veh_collide_spheres(6), the part
 * lookup crane_check_part_hits, the collision pass obj_collide_vehicle(6) (with
 * gCollVehicleAnim = gCraneAnim) and veh_collide_world(6); +0xA2 = 1 if gCollHit got
 * set; finally the player globals gPlayerX/Y/Z = the position,
 * gPlayerSpeed = 0, gPlayerMoveHeading / gPlayerHeading = u16 +0x4E / +0x4C.
 * veh_collision_setup's b0 and h2 are what crane_build_model leaves in t0 / t2 (x and
 * 6), kept here.
 * Leaked registers (FIDELITY AUDIT): veh_update_stand_on's fp and f12-f26 inputs
 * are what crane_update_controls / crane_part_on_ground (or, when +0x9A is set, the
 * caller) left behind. fp only reaches gSlotSurface when ground_scan_part_tris finds
 * no triangle and the h1 path still wins (impossible for game heights: it
 * needs |h3 - y| > ~0x5F5E0FF) and f12-f26 only pass through, so this
 * passes 0. Likewise 0 for the zone scan's t6/t7/s0-s4 inputs (dead
 * afterwards), crane_build_model's chain inputs a3/s1/s0 (only read for a
 * zero-matrix point) and crane_check_part_hits's s0-s3 (only used when its lookup
 * succeeds, which reloads them). The asm saves every s- and FP register it
 * changes; it returns with v1/f12/f14 as its callees leave them (read by
 * crane_spawn; not modelled). */
void crane_update(void) {
    ZoneScanRegs zr;
    TriSideOut f;
    MtxChainRegs regs;
    s32 x;
    s32 h2;

    zr.t6 = 0;
    zr.t7 = 0;
    zr.s0 = 0;
    zr.s1 = 0;
    zr.s2 = 0;
    zr.s3 = 0;
    zr.s4 = 0;
    crane_zone_alpha(&zr);
    if ((s8) gCraneState[0x9A] == 0) {
        crane_update_controls();
    }
    f.pz = 0.0f;
    f.cross = 0.0f;
    f.cz = 0.0f;
    f.side = 0.0f;
    f.sideZ = 0.0f;
    f.dz = 0.0f;
    veh_update_stand_on(gCranePos[0], gCranePos[2], (s16 *) (gCraneState + 0x5E), (s16 *) (gCraneState + 0x4C),
                  (s32 *) (gCraneState + 4), 6, gCraneState, 0, &f);
    if (gBufferIndex != 0) {
        anim_update_model((u8 *) gCraneMtxA, (u8 *) gCraneMtxB, (AnimChannel *) gCraneAnim);
    } else {
        anim_update_model((u8 *) gCraneMtxB, (u8 *) gCraneMtxA, (AnimChannel *) gCraneAnim);
    }
    regs.a3 = 0;
    regs.s1 = 0;
    regs.s0 = 0;
    x = crane_build_model(&regs, &h2);
    gCraneState[0xA2] = 0;
    veh_collision_setup(gCranePos[2], (s32) gCraneNonBlockingParts, 0, 0, gCranePos[0], gCranePos[1], x, 0, h2, 0, 6,
                  gCraneState);
    veh_collide_spheres(6, gCraneState);
    crane_check_part_hits(0, 0, 0, 0);
    gCollVehicleAnim = gCraneAnim;
    obj_collide_vehicle(6, gCraneState);
    veh_collide_world(6);
    if (gCollHit != 0) {
        gCraneState[0xA2] = 1;
    }
    gPlayerX = gCranePos[0];
    gPlayerY = gCranePos[1];
    gPlayerZ = gCranePos[2];
    gPlayerSpeed = 0;
    gPlayerMoveHeading = *(u16 *) (gCraneState + 0x4E);
    gPlayerHeading = *(u16 *) (gCraneState + 0x4C);
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/75490/crane_update.s")
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
extern f32 gCraneArmMaxPos; /* channel 5 limits */
extern f32 gCraneArmMinPos;
s32 crane_part_on_ground(void);

/* A/B, Left/Right and L/R drive channels 5, 4 and 2 with sounds (0x802BB4C0).
 * Animation channels and sounds of vehicle type 6 ($gp = gCraneState, read as
 * the global; channels in gCraneAnim, an AnimChannel table).
 * - Byte +0xA2 set: channels 5, 4 off (unk14 = 0), channel 2 unk11 = 0 and
 *   restarted (anim_play), +0xA1 = 1.
 * - Otherwise channel 5 follows input gHeldA while its unk4 <= gCraneArmMaxPos
 *   (direction 0) or gHeldB while !(unk4 < gCraneArmMinPos) (direction 1),
 *   starting sound 0x6F (handle gCraneArmSound) if needed, else it is switched
 *   off and that sound stopped; channel 4 likewise follows gHeldLeft /
 *   gHeldRight with sound 0x6E (gCraneSwingSound). Then, when crane_part_on_ground reports
 *   ground under the level-0x11 part, channel 2 is restarted with
 *   direction 0 and +0xA1 = 1 unless it is already running in direction 1
 *   at unk4 * 100 == 100 (cvt.w.s, nearest); with no ground, if +0xA1 is 0
 *   and input gHeldL or gHeldR is set, sound 0x6D (gCraneActionSound) is
 *   started if needed, channel 2 restarted and +0xA1 = 1, and nothing else
 *   happens.
 * - Finally (all other paths): when channel 2 isn't active (unk10 != 1),
 *   +0xA1 = 0 and sound gCraneActionSound is stopped if playing (handle kept).
 * Register note: the asm leaves crane_part_on_ground's registers behind (s0-s7, fp,
 * f20-f28, t7 per the survey; conventions.txt: clobbers); the asm caller
 * crane_update passes some of them on to veh_update_stand_on unchanged, which the
 * C can't reproduce (they are crane_part_on_ground's scratch). */
void crane_update_controls(void) {
    s32 e[8];
    s32 r;

    if (gCraneState[0xA2] != 0) {
        anim_set_speed((AnimChannel *) gCraneAnim, 5, 0);
        anim_set_speed((AnimChannel *) gCraneAnim, 4, 0);
        anim_set_reverse((AnimChannel *) gCraneAnim, 2, 0);
        anim_play((AnimChannel *) gCraneAnim, 2, 1);
        gCraneState[0xA1] = 1;
        goto check2;
    }

    if (gHeldA != 0 && (anim_get_state(5, (AnimChannel *) gCraneAnim, e), ((f32 *) e)[7] <= gCraneArmMaxPos)) {
        anim_set_reverse((AnimChannel *) gCraneAnim, 5, 0);
        anim_set_speed((AnimChannel *) gCraneAnim, 5, 1);
        goto start5;
    }
    if (gHeldB != 0 && (anim_get_state(5, (AnimChannel *) gCraneAnim, e), !(((f32 *) e)[7] < gCraneArmMinPos))) {
        anim_set_reverse((AnimChannel *) gCraneAnim, 5, 1);
        anim_set_speed((AnimChannel *) gCraneAnim, 5, 1);
        goto start5;
    }
    anim_set_speed((AnimChannel *) gCraneAnim, 5, 0);
    if (gCraneArmSound != NULL) {
        snd_stop(gCraneArmSound);
    }
    goto chan4;
start5:
    if (gCraneArmSound == NULL) {
        snd_play(gSfxBank, SFX_CRANE_ARM, &gCraneArmSound);
    }

chan4:
    if (gHeldLeft != 0) {
        anim_set_reverse((AnimChannel *) gCraneAnim, 4, 0);
        anim_set_speed((AnimChannel *) gCraneAnim, 4, 1);
    } else if (gHeldRight != 0) {
        anim_set_reverse((AnimChannel *) gCraneAnim, 4, 1);
        anim_set_speed((AnimChannel *) gCraneAnim, 4, 1);
    } else {
        anim_set_speed((AnimChannel *) gCraneAnim, 4, 0);
        if (gCraneSwingSound != NULL) {
            snd_stop(gCraneSwingSound);
        }
        goto ground;
    }
    if (gCraneSwingSound == NULL) {
        snd_play(gSfxBank, SFX_CRANE_SWING, &gCraneSwingSound);
    }

ground:
    if (crane_part_on_ground() != 0) {
        anim_get_state(2, (AnimChannel *) gCraneAnim, e);
        if (e[6] == 1) {
            CVT_W_S(r, ((f32 *) e)[7] * 100.0f);
            if (r == 100) {
                goto check2;
            }
        }
        anim_set_reverse((AnimChannel *) gCraneAnim, 2, 0);
        anim_play((AnimChannel *) gCraneAnim, 2, 1);
        gCraneState[0xA1] = 1;
    } else {
        if (gCraneState[0xA1] != 0) {
            goto check2;
        }
        if (gHeldL == 0 && gHeldR == 0) {
            goto check2;
        }
        if (gCraneActionSound == NULL) {
            snd_play(gSfxBank, SFX_CRANE_ACTION, &gCraneActionSound);
        }
        anim_play((AnimChannel *) gCraneAnim, 2, 1);
        gCraneState[0xA1] = 1;
        return;
    }

check2:
    anim_get_state(2, (AnimChannel *) gCraneAnim, e);
    if (e[0] != 1) {
        gCraneState[0xA1] = 0;
        if (gCraneActionSound != NULL) {
            snd_stop(gCraneActionSound);
        }
    }
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/75490/crane_update_controls.s")
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING

/* Level 0x11: the crane's test part has ground under it (0x802BB868).
 * On level 0x11 only: finds the kind-6 / id-0x3BD part (crane_find_test_part) and
 * returns whether ground_scan_guard_tris finds ground under its position (words 0, 8
 * as x, z; word 4 as y); 0 otherwise.
 * Register convention: result in v0 (ABI). The asm leaves both callees'
 * registers behind (s0-s4, t6, t7, fp, f12-f26) and its caller
 * crane_update_controls only tests v0, so they're not modelled (conventions.txt:
 * clobbers). Its s0-s3 inputs only pass through crane_find_test_part when nothing is
 * found, and ground_scan_guard_tris's pass-through inputs a3 / f12-f26 don't affect
 * the found flag, so the C starts them at 0. */
s32 crane_part_on_ground(void) {
    Unk8029C6E4Out o;
    TriSideOut f;
    TriScanRegs r;

    if (levelno != 0x11) {
        return 0;
    }
    o.s0 = 0;
    o.s1 = 0;
    o.s2 = 0;
    o.s3 = 0;
    crane_find_test_part(&o);
    if (o.s4 == 0) {
        return 0;
    }
    f.pz = 0.0f;
    f.cross = 0.0f;
    f.cz = 0.0f;
    f.side = 0.0f;
    f.sideZ = 0.0f;
    f.dz = 0.0f;
    r.a1 = 0;
    r.a3 = 0;
    r.t6 = (s32) o.t6;
    r.t7 = o.t7;
    r.fp = 0;
    r.s1 = o.s1;
    r.s2 = o.s2;
    r.s3 = o.s3;
    r.s4 = o.s4;
    ground_scan_guard_tris(o.s0, o.s2, o.s1, &f, &r);
    return r.a1;
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/75490/crane_part_on_ground.s")
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
extern u8 * N64P gCraneModel; /* model header */

/* Matrices, parts, points and its deck triangles (0x802BB8B8).
 * Rebuilds vehicle 6's model (gCraneState, the asm's $gp; header
 * gCraneModel, buffers gCraneMtxA (gBufferIndex set) / gCraneMtxB): rotation
 * gModelRot = 0, y = heading (u16 +0x4C), z = 0; root matrix at
 * gCranePos[0..2] with scale 0x1B58 (mtx_build_model); parts tag 6
 * (veh_place_parts, header +4 .. +8), points id 6 (veh_points_transform, +0 .. +4),
 * triangles id 6 (veh_deck_transform, header +8) and the collision setup
 * veh_update_tris(heading, gCraneAnim, x, z, 6, header + [+0xC], buffer).
 * Register convention: mtx_chain_transform's chain registers pass through regs
 * (a3, s1, s0 in) as in backlash_build_model (71140). The asm returns with t0 = x
 * (gCranePos[0]) and t2 = 6, which asm caller crane_update passes on to
 * veh_collision_setup: the return value and *t2Out here (conventions.txt).
 * veh_update_tris leaves s0-s7, fp, f20-f28 (and the chain outputs) changed
 * (clobbers; crane_update only hands s0-s3 to crane_check_part_hits, which reads
 * them only when its lookup fails and then doesn't use them). */
s32 crane_build_model(MtxChainRegs *regs, s32 *t2Out) {
    u8 *hdr = gCraneModel;
    u8 *buf = gBufferIndex != 0 ? (u8 *) gCraneMtxA : (u8 *) gCraneMtxB;
    s32 *m = (s32 *) (*(s32 *) (hdr + *(s32 *) (hdr + 0x18) + 4) + buf);
    s16 *verts;

    gModelRot = 0;
    (&gModelRot)[2] = 0;
    gModelRotY = *(u16 *) (gCraneState + 0x4C);
    mtx_build_model(gCranePos[0], gCranePos[1], gCranePos[2], 0x1B58, m);
    regs->s2 = (s32) m;
    buf = gBufferIndex != 0 ? (u8 *) gCraneMtxA : (u8 *) gCraneMtxB;
    hdr = gCraneModel;
    veh_place_parts(gCranePos[0], gCranePos[1], gCranePos[2], 6, hdr + *(s32 *) (hdr + 4), hdr + *(s32 *) (hdr + 8),
                  buf, regs);
    regs->v1 = gCranePos[1];
    regs->a0 = gCranePos[2];
    hdr = gCraneModel;
    veh_points_transform(6, (s16 *) (hdr + *(s32 *) hdr), (s16 *) (hdr + *(s32 *) (hdr + 4)), buf, regs);
    hdr = gCraneModel;
    veh_deck_transform(6, (u16 *) (hdr + *(s32 *) (hdr + 8)), buf, regs, &verts);
    hdr = gCraneModel;
    veh_update_tris(*(u16 *) (gCraneState + 0x4C), (AnimChannel *) gCraneAnim, gCranePos[0], gCranePos[2], 6,
                  hdr + *(s32 *) (hdr + 0xC), buf);
    *t2Out = 6;
    return gCranePos[0];
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/75490/crane_build_model.s")
#endif
