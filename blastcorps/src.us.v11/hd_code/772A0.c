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

#ifdef NON_MATCHING
/* Port-phase rewrites (functional, not matching) of vehicle type 7. Its asm
 * points $gp at the block gTrainState (a fixed global here, so the C reads
 * it directly). Spawn, per-frame and model update follow the same pattern as
 * the three vehicles of 83910.c. */
#define VEH_U8(v, off) (*(u8 *) ((u8 *) (v) + (off)))
#define VEH_S8(v, off) (*(s8 *) ((u8 *) (v) + (off)))
#define VEH_U16(v, off) (*(u16 *) ((u8 *) (v) + (off)))
#define VEH_S16(v, off) (*(s16 *) ((u8 *) (v) + (off)))
#define VEH_S32(v, off) (*(s32 *) ((u8 *) (v) + (off)))
#define VEH_F32(v, off) (*(f32 *) ((u8 *) (v) + (off)))
/* obj + the word at obj + off (the model header's self-relative offsets) */
#define OBJ_PTR(obj, off) ((u8 *) (obj) + *(s32 *) ((u8 *) (obj) + (off)))

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


extern u8 gTrainState[];  /* this vehicle's block (the asm's $gp) */
extern u8 gTrainAnim[];  /* its animation channel table */
extern s32 gTrainX;   /* x */
extern s32 gTrainY;   /* y */
extern s32 gTrainZ;   /* z */
extern u8 * N64P gTrainModel;   /* model data */
extern u64 * N64P gTrainMtxA;  /* save-buffer pair */
extern u64 * N64P gTrainMtxB;
extern s32 gTrainPart3Clear;   /* frame counters */
extern s32 gTrainPart2Clear;
extern u8 gTrainSparkTimer;
extern u8 gTrainUnusedFlag;
extern u8 gTrainNonBlockingParts[];

s32 train_zone_alpha(ZoneScanRegs *r);
void train_update_effects(s32 t6, s32 t7, s32 s0, s32 s1, s32 s2, s32 s3, s32 s4);
void train_build_model(void);
void train_set_physics(void);
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
/* Sets up the train (type 7) (0x802BBA60).
 * Spawn vehicle type 7, called from lvl_spawn_vehicles's object loop with the
 * spawn record in t7 (x), s3 (y), s0 (z), s1 (heading), s2 (model data) and
 * fp (the ground-scan byte the previous object left; see veh_init_ground):
 * as 83910.c's veh83910_init (save buffers from the heap, model record,
 * block reset, wheel offsets, gear bands, ground, animation channel 0, parts
 * with scale 0x3A98, veh_add_shadow, one frame (train_update) with byte
 * +0x9A set, buffer copy), and in addition gTrainSparkTimer = gTrainUnusedFlag = 0 and
 * both frame counters gTrainPart3Clear / gTrainPart2Clear = 999999.
 * The asm saves t0-t5: asm caller lvl_spawn_vehicles keeps t1 and t2 live (a
 * mixed N64 build would need a thunk). It leaves s2-s4, fp and f12-f26 as
 * its callees leave them, and lvl_spawn_vehicles hands fp and f12-f26 on to the
 * next object's spawn (not modelled; see the fidelity notes).
 * Fidelity: veh_init_ground's FP inputs (the dispatcher's f12-f26) start at 0;
 * they only pass through to its FP outputs. */
void train_spawn(s32 x, s32 y, s32 z, s32 angle, u8 *data, s32 fp) {
    u8 *veh = gTrainState;
    u8 *heap;
    u64 *a;
    u64 *b;
    TriSideOut tri;

    gTrainModel = data;
    heap = gHeapPtr;
    gTrainMtxA = (u64 *) heap;
    gTrainMtxB = (u64 *) (heap + 0x800);
    gHeapPtr = heap + 0x1000;
    veh_add_record(VEH_TRAIN, 0, (s32) gTrainMtxA, (s32) gTrainMtxB, data);
    veh_reset_state(veh);
    VEH_S16(veh, VS_WHEEL_OFFSETS) = 0x50;
    VEH_S16(veh, 0x54) = 0x50;
    VEH_S16(veh, 0x56) = -0x50;
    VEH_S16(veh, 0x58) = 0x50;
    VEH_S16(veh, 0x5A) = 0x50;
    VEH_S16(veh, 0x5C) = -0x50;
    VEH_S16(veh, VS_STAND_OFFSETS) = 0x5A;
    VEH_S16(veh, 0x60) = 0x5A;
    VEH_S16(veh, 0x62) = -0x5A;
    VEH_S16(veh, 0x64) = 0x5A;
    VEH_S16(veh, 0x66) = 0x5A;
    VEH_S16(veh, 0x68) = -0x5A;
    gTrainX = x;
    gTrainY = y;
    gTrainZ = z;
    VEH_S16(veh, VS_HEADING) = angle;
    VEH_S16(veh, VS_MOVE_HEADING) = angle;
    VEH_S16(veh, VS_MOMENTUM_HEADING) = angle;
    tri.pz = 0.0f;
    tri.cross = 0.0f;
    tri.cz = 0.0f;
    tri.side = 0.0f;
    tri.sideZ = 0.0f;
    tri.dz = 0.0f;
    veh_init_ground(&VEH_S16(veh, VS_WHEEL_OFFSETS), gTrainY, x, z, &VEH_S32(veh, 4), &gTrainY, &VEH_S16(veh, VS_HEADING), 7,
                  fp, veh, &tri);
    a = gTrainMtxA;
    b = gTrainMtxB;
    anim_init_model((u32 *) b, (u32 *) a, (AnimChannel *) gTrainAnim, gTrainModel);
    anim_set_speed((AnimChannel *) gTrainAnim, 0, 0x64);
    anim_set_reverse((AnimChannel *) gTrainAnim, 0, 0);
    anim_set_loop_mode((AnimChannel *) gTrainAnim, 0, 0);
    anim_set_interp(0.0f, (AnimChannel *) gTrainAnim, 0, 0);
    anim_play((AnimChannel *) gTrainAnim, 0, 1);
    anim_update_model((u8 *) a, (u8 *) b, (AnimChannel *) gTrainAnim);
    anim_rewind(0, (AnimChannel *) gTrainAnim);
    anim_play((AnimChannel *) gTrainAnim, 0, 1);
    anim_update_model((u8 *) b, (u8 *) a, (AnimChannel *) gTrainAnim);
    VEH_S16(veh, VS_BANDS) = -0xB4;
    VEH_S16(veh, 0x7A) = 0;
    VEH_S16(veh, 0x7C) = 1;
    VEH_S16(veh, 0x7E) = 0;
    VEH_S16(veh, 0x80) = 0x50;
    VEH_S16(veh, 0x82) = 1;
    VEH_S16(veh, 0x84) = 0x50;
    VEH_S16(veh, 0x86) = 0x8C;
    VEH_S16(veh, 0x88) = 1;
    VEH_S16(veh, 0x8A) = 0x8C;
    VEH_S16(veh, 0x8C) = 0xBE;
    VEH_S16(veh, 0x8E) = 1;
    VEH_S16(veh, 0x90) = 0xBE;
    VEH_S16(veh, 0x92) = 0xFA;
    VEH_S16(veh, 0x94) = 1;
    gTrainSparkTimer = 0;
    gTrainUnusedFlag = 0;
    gTrainPart3Clear = 999999;
    gTrainPart2Clear = 999999;
    data = gTrainModel;
    veh_register_parts(7, OBJ_PTR(data, 4), OBJ_PTR(data, 8), 0x3A98);
    veh_add_shadow(VEH_TRAIN, 0x96, 0x2D, 0x2D);
    VEH_U8(veh, VS_SPAWN_UPDATE) = 1;
    train_update();
    VEH_U8(veh, VS_SPAWN_UPDATE) = 0;
    data = gTrainModel;
    mtx_copy_at((u8 *) gTrainMtxB, (u8 *) gTrainMtxA, *(s32 *) (OBJ_PTR(data, 0x18) + 4));
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/772A0/train_spawn.s")
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING

/* Getting into the train: engine sound 0x20 (0x802BBDC8).
 * Enter vehicle type 7: gCamDistance / gCamHeightBase = 3000, 0, then veh_start_engine_sound(arg0,
 * 0x20) (arg0 passes straight through; hd.c calls this with no arguments
 * and veh_start_engine_sound ignores it). The asm also points $gp at gTrainState and
 * leaves it there (conventions.txt: clobbers gp); C code doesn't use $gp.
 * Same shape as barge_enter (83910) and amdream_enter (72B80). */
void train_enter(s32 arg0) {
    gCamDistance = 3000;
    gCamHeightBase = 0;
    veh_start_engine_sound(arg0, 0x20);
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/772A0/train_enter.s")
#endif

/* train_exit_check: `return 1;` wrapped in a dead `addiu sp,sp,-8`/`sd
 * $ra,($sp)`/`ld $ra,($sp)`/`addiu sp,sp,8` frame that saves/restores
 * nothing. Confirmed via probe compiles that this is NOT reachable from
 * any plausible C: IDO's own codegen for a real function call (verified
 * directly - a genuine `void f(void) { g(); }`) always uses the 24-byte
 * o32 argument-shadow frame (`sw $ra`), never this 8-byte `sd $ra` style;
 * a leaf with no calls at all (like this one) gets no frame whatsoever
 * unless a local variable is declared, and a declared local only
 * reserves the frame, it doesn't explain a `return 1;` needing one in
 * the first place. Same hand-written-leaf-stub character as init's
 * __osGetSR and this segment's COP0 stubs. Permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
/* Always 1 (0x802BBE10).
 * Functional rewrite (tools_port/eqcheck.py verified): always returns 1
 * (vehicle-type 7 "can exit" check; same as barge_exit_check). 00000.c
 * declares it void and ignores the result, but the asm returns 1 in v0. */
s32 train_exit_check(void) {
    return 1;
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/772A0/train_exit_check.s")
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
extern u64 * N64P gTrainMtxA; /* save copy pair (veh_sync_buffers) */
extern u64 * N64P gTrainMtxB;

/* Getting out of the train (0x802BBE2C).
 * Leave vehicle type 7 (called from hd.c): veh_sync_buffers(gTrainMtxA,
 * gTrainMtxB, 0x800), then stop the looping sounds (veh_stop_engine_sounds). The asm
 * points $gp at gTrainState around the calls and restores it. */
void train_leave(void) {
    veh_sync_buffers(gTrainMtxA, gTrainMtxB, 0x800);
    veh_stop_engine_sounds();
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/772A0/train_leave.s")
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
/* (gTrainX / 9C / A0 = x, y, z are declared at the top of the file.) */
/* Veh_zone_alpha at the train's position (0x802BBE74).
 * Zone level lookup (veh_zone_alpha) for vehicle id 7 at its position
 * gTrainX..+8; returns veh_zone_alpha's v1 (the zone list end).
 * Register convention: the asm passes veh_zone_alpha's scan registers t6, t7,
 * s0-s4 through (in and out; here through r), leaves a3 = id and clobbers
 * s5, s6 (conventions.txt). Asm caller train_update keeps a0, f12 and f14 live
 * (a mixed N64 build would need a thunk; the native port won't). */
s32 train_zone_alpha(ZoneScanRegs *r) {
    return veh_zone_alpha(7, gTrainX, gTrainY, gTrainZ, r);
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/772A0/train_zone_alpha.s")
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
/* The train driven by the player: throttle only, its rail line (0x802BBEB8).
 * Per-frame update of vehicle type 7 (hd.c's vehicle switch, and the spawn):
 * zone lookup (train_zone_alpha) whose scan registers feed the dust effect
 * train_update_effects (only while byte +0x9A is clear), timers (train_set_physics);
 * throttle (veh_throttle), steering (veh_turn_heading), slope ratio
 * (veh_slope), drag (veh_apply_slope, kind 7), move along the heading
 * (veh_move_along_heading). With gTrainOnLine set, x is then pinned to the line from
 * (gTrainLineX0, gTrainLineZ0) to (gTrainLineX1, gTrainLineZ1) at the current z:
 * x = x0 + round((x1 - x0) * (z - z0) / (z1 - z0)) (the asm also rounds
 * 100 * the ratio into a register it then overwrites; z1 == z0 would make
 * that cvt.w.s fault). Ground contact (veh_ground_contact), animation channels
 * (anim_update_model), model update (train_build_model), both frame counters + 1.
 * While byte +0x9A is clear: collisions (veh_collision_setup, veh_collide_spheres,
 * veh_collide_world, obj_collide_vehicle) and, if they flagged gCollBlocked, the bumper
 * check: with contact 3 (veh_contacts_has) the speed becomes 0 if contact 2 is
 * also set, else gTrainPart3Clear = 0 and -40 once gTrainPart2Clear >= 6 (else 0); with
 * only contact 2, gTrainPart2Clear = 0 and +40 once gTrainPart3Clear >= 6 (else 0).
 * Finally position / speed / headings go to gPlayerX.. and veh_set_record.
 * The asm saves s0-s7, gp, fp and f20-f30. The asm's adds/subs trap on
 * overflow (game range).
 * Fidelity: train_zone_alpha's scan registers are passed through from the
 * caller (t6, t7, s0-s4 as hd.c's compiled caller or the spawn leave them)
 * and reach train_update_effects's fx_spawn records only when no zone is looked
 * at (empty zone list); the C starts them at 0. veh_ground_contact's FP inputs
 * (f12/f14 as veh_move_along_heading's sine leaves them, the caller's f20-f26) start
 * at 0; they only reach its FP outputs, which nothing here reads. */
void train_update(void) {
    u8 *veh = gTrainState;
    ZoneScanRegs zr;
    Regs802A8768 r;
    Out802A860C o;
    TriSideOut tri;
    s32 x;
    s32 x0;
    s32 z0;
    s32 c2;
    s32 c3;
    f32 f;
    f32 k;

    zr.t6 = 0;
    zr.t7 = 0;
    zr.s0 = 0;
    zr.s1 = 0;
    zr.s2 = 0;
    zr.s3 = 0;
    zr.s4 = 0;
    train_zone_alpha(&zr);
    if (VEH_S8(veh, VS_SPAWN_UPDATE) == 0) {
        train_update_effects(zr.t6, zr.t7, zr.s0, zr.s1, zr.s2, zr.s3, zr.s4);
    }
    train_set_physics();
    veh_throttle(veh, &VEH_S16(veh, VS_SPEED), 3, &VEH_U8(veh, VS_SLOT_AIRBORNE), &VEH_S16(veh, VS_BANDS), 6);
    veh_turn_heading(veh, &VEH_U16(veh, VS_MOMENTUM_HEADING), 0x2328, &VEH_S16(veh, VS_SPEED), &VEH_U16(veh, VS_HEADING),
                  &VEH_U16(veh, VS_MOVE_HEADING), &VEH_S8(veh, VS_SKIDDING), 0);
    f = veh_slope(&VEH_S16(veh, VS_SPEED), &VEH_U8(veh, VS_SLOT_AIRBORNE), &VEH_S32(veh, 4), &VEH_F32(veh, 0));
    veh_apply_slope(veh, &VEH_S16(veh, VS_SPEED), 7, &VEH_S8(veh, VS_SLOT_AIRBORNE), &VEH_S32(veh, 4), 1, 160.0f);
    x = veh_move_along_heading(f, VEH_U16(veh, VS_MOVE_HEADING), &VEH_S16(veh, VS_SPEED), &gTrainX, &gTrainZ, &o);
    if (gTrainOnLine != 0) {
        z0 = gTrainLineZ0;
        k = (f32) (gTrainZ - z0) / (f32) (gTrainLineZ1 - z0);
        x0 = gTrainLineX0;
        CVT_W_S(x, (f32) (gTrainLineX1 - x0) * k);
        x += x0;
        gTrainX = x;
    }
    gBounceSound = 0;
    r.s3 = o.s3;
    tri.pz = 0.0f;
    tri.cross = 0.0f;
    tri.cz = 0.0f;
    tri.side = 0.0f;
    tri.sideZ = 0.0f;
    tri.dz = 0.0f;
    veh_ground_contact(veh, 7, &gTrainX, &gTrainY, &gTrainZ, x, o.t1, 0xA0, 0xA0, &VEH_S16(veh, VS_HEADING),
                  &VEH_U8(veh, VS_SLOT_AIRBORNE), &VEH_S16(veh, VS_WHEEL_OFFSETS), &VEH_S32(veh, VS_SLOT_LAUNCH_VEL), &VEH_S32(veh, VS_SLOT_AIR_TIME),
                  &VEH_S32(veh, VS_SLOT_LAUNCH_Y), &VEH_S32(veh, 4), &r, &tri);
    if (gBufferIndex != 0) {
        anim_update_model((u8 *) gTrainMtxA, (u8 *) gTrainMtxB, (AnimChannel *) gTrainAnim);
    } else {
        anim_update_model((u8 *) gTrainMtxB, (u8 *) gTrainMtxA, (AnimChannel *) gTrainAnim);
    }
    train_build_model();
    gTrainPart3Clear++;
    gTrainPart2Clear++;
    if (VEH_S8(veh, VS_SPAWN_UPDATE) == 0) {
        /* b0 / h2 are the model update's leftover t0 (x) and t2 (7) */
        veh_collision_setup(gTrainZ, (s32) gTrainNonBlockingParts, 0, 0, gTrainX, gTrainY, gTrainX,
                      VEH_S16(veh, VS_SPEED), 7, 0, 7, veh);
        veh_collide_spheres(7, veh);
        veh_collide_world(7);
        gCollVehicleAnim = gTrainAnim;
        obj_collide_vehicle(7, veh);
        if (gCollBlocked != 0) {
            c2 = veh_contacts_has(2);
            c3 = veh_contacts_has(3);
            if (c3 != 0) {
                if (c2 != 0) {
                    VEH_S16(veh, VS_SPEED) = 0;
                } else {
                    gTrainPart3Clear = 0;
                    VEH_S16(veh, VS_SPEED) = (gTrainPart2Clear >= 6) ? -0x28 : 0;
                }
            } else if (c2 != 0) {
                gTrainPart2Clear = 0;
                VEH_S16(veh, VS_SPEED) = (gTrainPart3Clear >= 6) ? 0x28 : 0;
            }
        }
    }
    gPlayerX = gTrainX;
    gPlayerY = gTrainY;
    gPlayerZ = gTrainZ;
    gPlayerSpeed = VEH_S16(veh, VS_SPEED);
    gPlayerMoveHeading = VEH_U16(veh, VS_MOVE_HEADING);
    gPlayerHeading = VEH_U16(veh, VS_HEADING);
    veh_set_record(gTrainZ, 7, gTrainX, gTrainY, veh);
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/772A0/train_update.s")
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
extern u8 gTrainState[];  /* this vehicle's state block (the asm's $gp) */
extern u8 gTrainSparkTimer;    /* countdown */

/* Brake sparks and sound 0x29, engine pitch, the L/R whistle loop (0x802BC2C8).
 * Countdown gTrainSparkTimer: when nonzero it just counts down. At zero, if the
 * flag for the speed's sign (s16 at +0x76: > 0 -> gHeldBOrZ, else
 * gHeldA) is set, two fx_spawn records are set up (def gFxSparks,
 * data 0x9C40, tag 0, type 1 at (7, 2, 1) and (7, 3, 1)), sound 0x29 is
 * played (snd_play) and the countdown restarts at 1. Then always
 * veh_set_engine_level(|speed >> 4|) and veh_update_horn(0x21).
 * Register convention: the asm passes t6, t7, s0-s4 through to
 * fx_spawn (t6 and s1 in its in/out block); $gp (= gTrainState) is read
 * as the global. It leaves fx_spawn's s1 and changes s5
 * (conventions.txt: clobbers). */
void train_update_effects(s32 t6, s32 t7, s32 s0, s32 s1, s32 s2, s32 s3, s32 s4) {
    Io802A6274 io;
    s32 v;

    if (gTrainSparkTimer != 0) {
        gTrainSparkTimer--;
    } else if ((*(s16 *) (gTrainState + 0x76) > 0) ? (gHeldBOrZ != 0) : (gHeldA != 0)) {
        io.a3 = 0;
        io.t6 = t6;
        io.s1 = s1;
        fx_spawn(&io, gFxSparks, 0x9C40, 1, 7, 2, 1, t7, s0, s2, s3, s4, 0);
        io.a3 = 0;
        fx_spawn(&io, gFxSparks, 0x9C40, 1, 7, 3, 1, t7, s0, s2, s3, s4, 0);
        snd_play(gSfxBank, SFX_TRAIN_BRAKE, NULL);
        gTrainSparkTimer = 1;
    }
    v = *(s16 *) (gTrainState + 0x76) >> 4;
    if (v < 0) {
        v = -v;
    }
    veh_set_engine_level(v);
    veh_update_horn(0x21);
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/772A0/train_update_effects.s")
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
/* Matrices, parts, points and its deck triangles (0x802BC3D0).
 * Model update of vehicle type 7: as 83910.c's veh83910_model (scale
 * 0x3A98, tag 7), then gTrainClosed = veh_tris_all_solid_impl(7). The asm reads the
 * block through $gp (always gTrainState). It leaves t0 = x and t2 = 7 for
 * train_update's veh_collision_setup call (the C caller passes them itself) and
 * s0-s7, fp, f20-f28 as its callees do (conventions.txt: clobbers).
 * Fidelity: mtx_chain_transform's extra registers (a3, s1, s2, s0 as the caller
 * and mtx_build_model leave them; read only for a part with an empty matrix
 * chain) start at 0 here. */
void train_build_model(void) {
    u8 *veh = gTrainState;
    u8 *data = gTrainModel;
    u8 *base;
    s16 *verts;
    MtxChainRegs regs;

    base = (u8 *) (gBufferIndex != 0 ? gTrainMtxA : gTrainMtxB);
    base += *(s32 *) (OBJ_PTR(data, 0x18) + 4);
    gModelRot[0] = 0;
    gModelRot[2] = 0;
    gModelRot[1] = VEH_U16(veh, VS_HEADING);
    mtx_build_model(gTrainX, gTrainY, gTrainZ, 0x3A98, (s32 *) base);

    base = (u8 *) (gBufferIndex != 0 ? gTrainMtxA : gTrainMtxB);
    regs.v1 = gTrainY;
    regs.a0 = gTrainZ;
    regs.a3 = 0;
    regs.s1 = 0;
    regs.s2 = 0;
    regs.s0 = 0;
    data = gTrainModel;
    veh_place_parts(gTrainX, gTrainY, gTrainZ, 7, OBJ_PTR(data, 4), OBJ_PTR(data, 8), base, &regs);
    /* the asm's v1 / a0 (y / z) survive veh_place_parts */
    regs.v1 = gTrainY;
    regs.a0 = gTrainZ;
    data = gTrainModel;
    veh_points_transform(7, (s16 *) OBJ_PTR(data, 0), (s16 *) OBJ_PTR(data, 4), base, &regs);
    data = gTrainModel;
    veh_deck_transform(7, (u16 *) OBJ_PTR(data, 8), base, &regs, &verts);
    data = gTrainModel;
    veh_update_tris(VEH_U16(veh, VS_HEADING), (AnimChannel *) gTrainAnim, gTrainX, gTrainZ, 7, OBJ_PTR(data, 0xC), base);
    gTrainClosed = veh_tris_all_solid_impl(7);
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/772A0/train_build_model.s")
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
/* Functional rewrite (tools_port/eqcheck.py verified). Same "set timers"
 * shape as carrier_set_physics (75490.c). Asm caller train_update relies on
 * a0-a3 being preserved (mixed N64 build would need a thunk). */

/* Gravity x2, bounce 60/4 (0x802BC578). */
void train_set_physics(void) {
    gGravity = gLevelGravity * 2.0f;
    gBounceThreshold = 60;
    gBounceDamping = 4;
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/772A0/train_set_physics.s")
#endif
