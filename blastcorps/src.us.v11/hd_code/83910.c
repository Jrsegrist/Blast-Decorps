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
/* Port-phase rewrites (functional, not matching): the vehicles of types
 * 0x0B, 0x11 and 0x12 run identical asm (three copies differing only in
 * their globals and type id), so the C keeps one implementation driven by a
 * per-vehicle descriptor (sVeh83910) and the asm-named entry points pick the
 * descriptor. The asm points $gp at the vehicle's block; it is a fixed
 * global per copy, so the C takes it from the descriptor. */
#define VEH_U8(v, off) (*(u8 *) ((u8 *) (v) + (off)))
#define VEH_S8(v, off) (*(s8 *) ((u8 *) (v) + (off)))
#define VEH_U16(v, off) (*(u16 *) ((u8 *) (v) + (off)))
#define VEH_S16(v, off) (*(s16 *) ((u8 *) (v) + (off)))
#define VEH_S32(v, off) (*(s32 *) ((u8 *) (v) + (off)))
#define VEH_F32(v, off) (*(f32 *) ((u8 *) (v) + (off)))
/* obj + the word at obj + off (the model header's self-relative offsets) */
#define OBJ_PTR(obj, off) ((u8 *) (obj) + *(s32 *) ((u8 *) (obj) + (off)))


typedef struct {
    u8 *veh;             /* the vehicle block (asm $gp) */
    u8 *chan;            /* animation channel table */
    s32 *pos;            /* x, y, z */
    u8 * N64P *data;     /* model data */
    u64 * N64P *bufA;    /* save-buffer pair (veh_sync_buffers / mtx_copy_at) */
    u64 * N64P *bufB;
    u8 *flag;            /* set while a hit has been undone */
    s32 type;            /* vehicle type / part tag */
    void (*model)(void); /* this copy's model update */
} VehDesc83910;

extern u8 gBarge0BState[];
extern u8 gBarge11State[];
extern u8 gBarge12State[];
extern u8 gBarge0BAnim[];
extern u8 gBarge11Anim[];
extern u8 gBarge12Anim[];
extern s32 gBarge0BPos[]; /* x, y, z (gBarge0BPos, D_803F874C, D_803F8750) */
extern s32 gBarge11Pos[];
extern s32 gBarge12Pos[];
extern u8 * N64P gBarge0BModel;
extern u8 * N64P gBarge11Model;
extern u8 * N64P gBarge12Model;
extern u64 * N64P gBarge0BMtxA;
extern u64 * N64P gBarge0BMtxB;
extern u64 * N64P gBarge11MtxA;
extern u64 * N64P gBarge11MtxB;
extern u64 * N64P gBarge12MtxA;
extern u64 * N64P gBarge12MtxB;
extern u8 gBarge0BBounced;
extern u8 gBarge11Bounced;
extern u8 gBarge12Bounced;
extern u8 gBargeNonBlockingParts[];

void barge_spawn_0b(s32 x, s32 y, s32 z, s32 angle, u8 *data, s32 fp);
void barge_spawn_11(s32 x, s32 y, s32 z, s32 angle, u8 *data, s32 fp);
void barge_spawn_12(s32 x, s32 y, s32 z, s32 angle, u8 *data, s32 fp);
void barge_update_0b(void);
void barge_update_11(void);
void barge_update_12(void);
void barge_build_model_0b(void);
void barge_build_model_11(void);
void barge_build_model_12(void);
void barge_bounce(u8 *state);
void barge_set_physics(void);

static const VehDesc83910 sVeh83910[3] = {
    { gBarge0BState, gBarge0BAnim, gBarge0BPos, &gBarge0BModel, &gBarge0BMtxA, &gBarge0BMtxB, &gBarge0BBounced, 0x0B,
      barge_build_model_0b },
    { gBarge11State, gBarge11Anim, gBarge11Pos, &gBarge11Model, &gBarge11MtxA, &gBarge11MtxB, &gBarge11Bounced, 0x11,
      barge_build_model_11 },
    { gBarge12State, gBarge12Anim, gBarge12Pos, &gBarge12Model, &gBarge12MtxA, &gBarge12MtxB, &gBarge12Bounced, 0x12,
      barge_build_model_12 },
};

/* Model update (barge_build_model_0b / 97C0 / 995C): gModelRot = (0, heading, 0)
 * and mtx_build_model(x, y, z, 0x88B8, m) with m = the current save buffer +
 * the word at (model + header word 0x18) + 4; then the part positions
 * (veh_place_parts, model sections 1..2), the matrix-chain records
 * (veh_points_transform, sections 0..1), the triangles (veh_deck_transform, section 2)
 * and the channel models (veh_update_tris, section 3), with the current save
 * buffer as matrix base.
 * Fidelity: the asm hands mtx_chain_transform's extra registers through these
 * calls as its caller left them (a3, s1, s0; s2 as mtx_build_model leaves it;
 * read only for a part whose matrix chain is empty). The C starts them at 0
 * (v1 / a0 = y / z, which the asm's callees preserve). */
static void veh83910_model(const VehDesc83910 *d) {
    u8 *veh = d->veh;
    s32 *pos = d->pos;
    u8 *data = *d->data;
    u8 *base;
    s16 *verts;
    MtxChainRegs regs;

    base = (u8 *) (gBufferIndex != 0 ? *d->bufA : *d->bufB);
    base += *(s32 *) (OBJ_PTR(data, 0x18) + 4);
    gModelRot[0] = 0;
    gModelRot[2] = 0;
    gModelRot[1] = VEH_U16(veh, VS_HEADING);
    mtx_build_model(pos[0], pos[1], pos[2], 0x88B8, (s32 *) base);

    base = (u8 *) (gBufferIndex != 0 ? *d->bufA : *d->bufB);
    regs.v1 = pos[1];
    regs.a0 = pos[2];
    regs.a3 = 0;
    regs.s1 = 0;
    regs.s2 = 0;
    regs.s0 = 0;
    data = *d->data;
    veh_place_parts(pos[0], pos[1], pos[2], d->type, OBJ_PTR(data, 4), OBJ_PTR(data, 8), base, &regs);
    /* the asm's v1 / a0 (y / z) survive veh_place_parts */
    regs.v1 = pos[1];
    regs.a0 = pos[2];
    data = *d->data;
    veh_points_transform(d->type, (s16 *) OBJ_PTR(data, 0), (s16 *) OBJ_PTR(data, 4), base, &regs);
    data = *d->data;
    veh_deck_transform(d->type, (u16 *) OBJ_PTR(data, 8), base, &regs, &verts);
    data = *d->data;
    veh_update_tris(VEH_U16(veh, VS_HEADING), (AnimChannel *) d->chan, pos[0], pos[2], d->type, OBJ_PTR(data, 0xC), base);
}

/* Per-frame update (barge_update_0b / 8FA8 / 92C0, through barge_update):
 * save the vehicle (veh_snapshot), engine sound 0x71 (veh_update_horn),
 * timers (barge_set_physics), engine level |speed| >> 5 (veh_set_engine_level);
 * throttle (veh_throttle), steering (veh_turn_heading), slope ratio
 * (veh_slope) and drag (veh_apply_slope), move along the heading
 * (veh_move_along_heading), ground contact (veh_ground_contact); animation channels
 * (anim_update_model between the two save buffers), model update, collisions
 * (veh_collision_setup, veh_collide_spheres, veh_collide_world, obj_collide_vehicle). If those
 * reported a hit (gCollHit): the first time (flag clear) restore the saved
 * state (veh_rollback), bounce (barge_bounce), set the flag, update the
 * model again and stop; otherwise (or with no hit, which clears the flag)
 * publish position / speed / headings to gPlayerX.. and veh_set_record.
 * Fidelity: veh_ground_contact's FP inputs (the asm's f12-f26 at that point:
 * veh_move_along_heading's sine leftovers and the caller's f20-f26) start at 0 here;
 * they only pass through to its FP outputs, which nothing here reads. */
static void veh83910_frame(const VehDesc83910 *d) {
    u8 *veh = d->veh;
    s32 *pos = d->pos;
    s32 s;
    s32 x;
    f32 f;
    Out802A860C o;
    Regs802A8768 r;
    TriSideOut tri;
    u64 *src;
    u64 *dst;
    u64 *end;

    veh_snapshot(veh, d->chan, &pos[0], &pos[1], &pos[2]);
    veh_update_horn(0x71);
    barge_set_physics();
    s = VEH_S16(veh, VS_SPEED);
    if (s < 0) {
        s = -s;
    }
    veh_set_engine_level((u32) s >> 5);
    veh_throttle(veh, &VEH_S16(veh, VS_SPEED), 3, &VEH_U8(veh, VS_SLOT_AIRBORNE), &VEH_S16(veh, VS_BANDS), 6);
    veh_turn_heading(veh, &VEH_U16(veh, VS_MOMENTUM_HEADING), 0x2328, &VEH_S16(veh, VS_SPEED), &VEH_U16(veh, VS_HEADING),
                  &VEH_U16(veh, VS_MOVE_HEADING), &VEH_S8(veh, VS_SKIDDING), 0);
    f = veh_slope(&VEH_S16(veh, VS_SPEED), &VEH_U8(veh, VS_SLOT_AIRBORNE), &VEH_S32(veh, 4), &VEH_F32(veh, 0));
    veh_apply_slope(veh, &VEH_S16(veh, VS_SPEED), d->type, &VEH_S8(veh, VS_SLOT_AIRBORNE), &VEH_S32(veh, 4), 1, 160.0f);
    x = veh_move_along_heading(f, VEH_U16(veh, VS_MOVE_HEADING), &VEH_S16(veh, VS_SPEED), &pos[0], &pos[2], &o);
    gBounceSound = 0;
    r.s3 = o.s3;
    tri.pz = 0.0f;
    tri.cross = 0.0f;
    tri.cz = 0.0f;
    tri.side = 0.0f;
    tri.sideZ = 0.0f;
    tri.dz = 0.0f;
    veh_ground_contact(veh, d->type, &pos[0], &pos[1], &pos[2], x, o.t1, 0xA0, 0xA0, &VEH_S16(veh, VS_HEADING),
                  &VEH_U8(veh, VS_SLOT_AIRBORNE), &VEH_S16(veh, VS_WHEEL_OFFSETS), &VEH_S32(veh, VS_SLOT_LAUNCH_VEL), &VEH_S32(veh, VS_SLOT_AIR_TIME),
                  &VEH_S32(veh, VS_SLOT_LAUNCH_Y), &VEH_S32(veh, 4), &r, &tri);
    if (gBufferIndex != 0) {
        anim_update_model((u8 *) *d->bufA, (u8 *) *d->bufB, (AnimChannel *) d->chan);
    } else {
        anim_update_model((u8 *) *d->bufB, (u8 *) *d->bufA, (AnimChannel *) d->chan);
    }
    d->model();
    /* b0 / h2 are the model update's leftover t0 (x) and t2 (the type) */
    veh_collision_setup(pos[2], (s32) gBargeNonBlockingParts, 0, 0, pos[0], pos[1], pos[0], VEH_S16(veh, VS_SPEED), d->type, 0,
                  d->type, veh);
    veh_collide_spheres(d->type, veh);
    veh_collide_world(d->type);
    gCollVehicleAnim = d->chan;
    obj_collide_vehicle(d->type, veh);
    if (gCollHit != 0) {
        if (*d->flag == 0) {
            if (gBufferIndex != 0) {
                src = *d->bufB;
                dst = *d->bufA;
            } else {
                src = *d->bufA;
                dst = *d->bufB;
            }
            veh_rollback(veh, d->chan, &pos[0], &pos[1], &pos[2], src, dst, 0x800, &end);
            barge_bounce(veh);
            *d->flag = 1;
            d->model();
            return;
        }
    } else {
        *d->flag = 0;
    }
    gPlayerX = pos[0];
    gPlayerY = pos[1];
    gPlayerZ = pos[2];
    gPlayerSpeed = VEH_S16(veh, VS_SPEED);
    gPlayerMoveHeading = VEH_U16(veh, VS_MOVE_HEADING);
    gPlayerHeading = VEH_U16(veh, VS_HEADING);
    veh_set_record(pos[2], d->type, pos[0], pos[1], veh);
}

/* Spawn (barge_spawn_0b / 8470 / 8790, through barge_spawn): model data
 * `data`, two 0x800-byte save buffers from the heap gHeapPtr, model record
 * (veh_add_record), reset the block (veh_reset_state) and its wheel offsets
 * (+0x52..+0x68) and gear bands (+0x78..+0x94), position and headings,
 * ground (veh_init_ground), animation channel 0 (anim_init_model, the setters,
 * anim_update_model both ways), parts (veh_register_parts), veh_add_shadow, then one
 * frame (barge_update) with byte +0x9A set, and the buffer copy
 * (mtx_copy_at). The asm's addi on the heap pointer traps on overflow.
 * Fidelity: veh_init_ground's FP inputs (the dispatcher's f12-f26) start at 0;
 * they only pass through to its FP outputs. */
static void veh83910_init(const VehDesc83910 *d, s32 x, s32 y, s32 z, s32 angle, u8 *data, s32 fp) {
    u8 *veh = d->veh;
    s32 *pos = d->pos;
    u8 *heap;
    u64 *a;
    u64 *b;
    TriSideOut tri;

    *d->data = data;
    heap = gHeapPtr;
    *d->bufA = (u64 *) heap;
    *d->bufB = (u64 *) (heap + 0x800);
    gHeapPtr = heap + 0x1000;
    veh_add_record(d->type, 0, (s32) *d->bufA, (s32) *d->bufB, data);
    *d->flag = 0;
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
    pos[0] = x;
    pos[1] = y;
    pos[2] = z;
    VEH_S16(veh, VS_HEADING) = angle;
    VEH_S16(veh, VS_MOVE_HEADING) = angle;
    VEH_S16(veh, VS_MOMENTUM_HEADING) = angle;
    tri.pz = 0.0f;
    tri.cross = 0.0f;
    tri.cz = 0.0f;
    tri.side = 0.0f;
    tri.sideZ = 0.0f;
    tri.dz = 0.0f;
    veh_init_ground(&VEH_S16(veh, VS_WHEEL_OFFSETS), pos[1], x, z, &VEH_S32(veh, 4), &pos[1], &VEH_S16(veh, VS_HEADING), d->type,
                  fp, veh, &tri);
    a = *d->bufA;
    b = *d->bufB;
    anim_init_model((u32 *) b, (u32 *) a, (AnimChannel *) d->chan, *d->data);
    anim_set_speed((AnimChannel *) d->chan, 0, 0x64);
    anim_set_reverse((AnimChannel *) d->chan, 0, 0);
    anim_set_loop_mode((AnimChannel *) d->chan, 0, 0);
    anim_set_interp(0.0f, (AnimChannel *) d->chan, 0, 0);
    anim_play((AnimChannel *) d->chan, 0, 1);
    anim_update_model((u8 *) a, (u8 *) b, (AnimChannel *) d->chan);
    anim_rewind(0, (AnimChannel *) d->chan);
    anim_play((AnimChannel *) d->chan, 0, 1);
    anim_update_model((u8 *) b, (u8 *) a, (AnimChannel *) d->chan);
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
    data = *d->data;
    veh_register_parts(d->type, OBJ_PTR(data, 4), OBJ_PTR(data, 8), 0x88B8);
    veh_add_shadow(d->type, 0x96, 0x2D, 0x2D);
    VEH_U8(veh, VS_SPAWN_UPDATE) = 1;
    barge_update(d->type);
    VEH_U8(veh, VS_SPAWN_UPDATE) = 0;
    data = *d->data;
    mtx_copy_at((u8 *) *d->bufB, (u8 *) *d->bufA, *(s32 *) (OBJ_PTR(data, 0x18) + 4));
}
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
/* Sets up a barge (dispatch by type) (0x802C80D0).
 * Spawn dispatcher for vehicle types 0x0B / 0x11 / other (0x12), called
 * from lvl_spawn_vehicles's object loop with the type in t3 and the spawn record
 * in t7 (x), s3 (y), s0 (z), s1 (heading), s2 (model data) and fp (the
 * ground-scan byte the previous object left; see veh_init_ground). The asm
 * saves t0-t5: asm caller lvl_spawn_vehicles keeps t1 and t2 live (a mixed N64
 * build would need a thunk). It leaves s2-s4, fp and f12-f26 as the spawn
 * leaves them, and lvl_spawn_vehicles hands fp and f12-f26 on to the next
 * object's spawn (not modelled; see the fidelity notes). */
void barge_spawn(s32 type, s32 x, s32 y, s32 z, s32 angle, u8 *data, s32 fp) {
    if (type == VEH_BARGE_0B) {
        barge_spawn_0b(x, y, z, angle, data, fp);
    } else if (type == VEH_BARGE_11) {
        barge_spawn_11(x, y, z, angle, data, fp);
    } else {
        barge_spawn_12(x, y, z, angle, data, fp);
    }
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/83910/barge_spawn.s")
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
/* Sets up barge type 0xB (0x802C8150).
 * Spawn vehicle type 0x0B (see veh83910_init); register inputs as
 * barge_spawn's (conventions.txt). */
void barge_spawn_0b(s32 x, s32 y, s32 z, s32 angle, u8 *data, s32 fp) {
    veh83910_init(&sVeh83910[0], x, y, z, angle, data, fp);
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/83910/barge_spawn_0b.s")
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
/* Sets up barge type 0x11 (0x802C8470).
 * Spawn vehicle type 0x11 (see veh83910_init). */
void barge_spawn_11(s32 x, s32 y, s32 z, s32 angle, u8 *data, s32 fp) {
    veh83910_init(&sVeh83910[1], x, y, z, angle, data, fp);
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/83910/barge_spawn_11.s")
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
/* Sets up barge type 0x12 (0x802C8790).
 * Spawn vehicle type 0x12 (see veh83910_init). */
void barge_spawn_12(s32 x, s32 y, s32 z, s32 angle, u8 *data, s32 fp) {
    veh83910_init(&sVeh83910[2], x, y, z, angle, data, fp);
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/83910/barge_spawn_12.s")
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
/* Functional rewrite (tools_port/eqcheck.py verified): a0 passes straight
 * through to veh_start_engine_sound with a1 = 0x72. */

/* Getting into a barge (0x802C8AB0). */
void barge_enter(s32 arg0) {
    gCamDistance = 3000;
    gCamHeightBase = 0;
    veh_start_engine_sound(arg0, 0x72);
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/83910/barge_enter.s")
#endif

/* barge_exit_check: same dead-$ra-frame-around-`return 1;` as train_exit_check
 * in 772A0.c - confirmed hand-written, not compiler-reachable. See that
 * file's comment. Permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
/* A barge's exit check (0x802C8AF0).
 * Functional rewrite (tools_port/eqcheck.py verified): always returns 1
 * (vehicle-type 11/17/18 "can exit" check). 00000.c declares it void and
 * ignores the result, but the asm returns 1 in v0. */
s32 barge_exit_check(void) {
    return 1;
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/83910/barge_exit_check.s")
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
extern u64 * N64P gBarge0BMtxA; /* save copy pairs (veh_sync_buffers), one per vehicle */
extern u64 * N64P gBarge0BMtxB;
extern u64 * N64P gBarge11MtxA;
extern u64 * N64P gBarge11MtxB;
extern u64 * N64P gBarge12MtxA;
extern u64 * N64P gBarge12MtxB;

/* Getting out of a barge (by type) (0x802C8B0C).
 * Leave vehicle type 0x11 / 0x12 / other (11) (called from hd.c with the
 * type): veh_sync_buffers(pair for that type, 0x800), then stops the looping
 * sounds (veh_stop_engine_sounds). The asm points $gp at that vehicle's block
 * (gBarge11State / gBarge12State / gBarge0BState) around the calls and restores it.
 * The asm compares the whole register; 00000.c declares the parameter u8. */
void barge_leave(s32 type) {
    if (type == 0x11) {
        veh_sync_buffers(gBarge11MtxA, gBarge11MtxB, 0x800);
    } else if (type == 0x12) {
        veh_sync_buffers(gBarge12MtxA, gBarge12MtxB, 0x800);
    } else {
        veh_sync_buffers(gBarge0BMtxA, gBarge0BMtxB, 0x800);
    }
    veh_stop_engine_sounds();
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/83910/barge_leave.s")
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
/* A barge driven by the player (dispatch by type) (0x802C8BB8).
 * Per-frame update dispatcher (hd.c's vehicle switch, and the spawns): type
 * 0x0B -> barge_update_0b, 0x11 -> barge_update_11, else barge_update_12. The asm
 * saves s0-s7, gp, fp and f20-f30 around the call; it compares the whole
 * a0 (00000.c declares the parameter u8). It leaves v1, f12 and f14 as the
 * update leaves them (the survey lists them as read by its callers; nothing
 * there uses them). */
void barge_update(s32 type) {
    if (type == 0x0B) {
        barge_update_0b();
    } else if (type == 0x11) {
        barge_update_11();
    } else {
        barge_update_12();
    }
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/83910/barge_update.s")
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
/* Barge type 0xB driven by the player (0x802C8C90).
 * Per-frame update of vehicle type 0x0B (see veh83910_frame). The asm
 * points $gp at gBarge0BState and leaves it there, and leaves s0-s7, fp and
 * f20-f30 as its callees do (conventions.txt: clobbers). */
void barge_update_0b(void) {
    veh83910_frame(&sVeh83910[0]);
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/83910/barge_update_0b.s")
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
/* Barge type 0x11 driven by the player (0x802C8FA8).
 * Per-frame update of vehicle type 0x11 (see veh83910_frame). */
void barge_update_11(void) {
    veh83910_frame(&sVeh83910[1]);
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/83910/barge_update_11.s")
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
/* Barge type 0x12 driven by the player (0x802C92C0).
 * Per-frame update of vehicle type 0x12 (see veh83910_frame). */
void barge_update_12(void) {
    veh83910_frame(&sVeh83910[2]);
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/83910/barge_update_12.s")
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
/* Speed -max(|v|, 80) / 2 after a hit (0x802C95D8).
 * Bounce: the speed (s16 at +0x76 of the vehicle block, which the asm takes
 * in $gp: three vehicles share this, see tools_port/conventions.txt) is
 * raised to a magnitude of at least 80 keeping its sign (0 counts as
 * positive), then negated and halved (arithmetic shift). Asm callers
 * barge_update_0b, barge_update_11 and barge_update_12 keep a0-a3, f12 and f14
 * live across the call (a mixed N64 build would need a thunk). */
void barge_bounce(u8 *state) {
    s32 v = *(s16 *) (state + 0x76);

    if (v >= 0) {
        if (v < 0x50) {
            v = 0x50;
        }
    } else if (v >= -0x4F) {
        v = -0x50;
    }
    *(s16 *) (state + 0x76) = -v >> 1;
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/83910/barge_bounce.s")
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
/* Barge 0xB's model matrix, parts and deck (0x802C9624).
 * Model update of vehicle type 0x0B (see veh83910_model). The asm reads
 * the block through $gp (always gBarge0BState here). It leaves t0 = x and
 * t2 = 0x0B for its caller's veh_collision_setup call (the C caller passes them
 * itself) and s0-s7, fp, f20-f28 as its callees do (conventions.txt). */
void barge_build_model_0b(void) {
    veh83910_model(&sVeh83910[0]);
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/83910/barge_build_model_0b.s")
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
/* Barge 0x11's model matrix, parts and deck (0x802C97C0).
 * Model update of vehicle type 0x11 (see veh83910_model). */
void barge_build_model_11(void) {
    veh83910_model(&sVeh83910[1]);
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/83910/barge_build_model_11.s")
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
/* Barge 0x12's model matrix, parts and deck (0x802C995C).
 * Model update of vehicle type 0x12 (see veh83910_model). */
void barge_build_model_12(void) {
    veh83910_model(&sVeh83910[2]);
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/83910/barge_build_model_12.s")
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

extern f32 gBargeSteerDivisor;

/* A barge's steering rate (no callers) (0x802C9AF8).
 * Speed (s16 at +0x76 of the vehicle block) / gBargeSteerDivisor, rounded to
 * nearest. The asm takes the block in $gp and returns the value in s3 (see
 * tools_port/conventions.txt); nothing calls it directly, so which of this
 * file's three blocks it gets is unknown, hence the parameter. */
s32 barge_steer_rate(u8 *state) {
    s32 r;

    CVT_W_S(r, *(s16 *) (state + 0x76) / gBargeSteerDivisor);
    return r;
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/83910/barge_steer_rate.s")
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
/* Functional rewrite (tools_port/eqcheck.py verified). Same "set timers"
 * shape as carrier_set_physics (75490.c). Asm callers barge_update_0b, barge_update_11
 * and barge_update_12 rely on a0-a3, f12, f14 being preserved (mixed N64 build
 * would need a thunk). */

/* A barge's gravity and bounce (0x802C9B30). */
void barge_set_physics(void) {
    gGravity = gLevelGravity * 2.0f;
    gBounceThreshold = 60;
    gBounceDamping = 4;
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/83910/barge_set_physics.s")
#endif
