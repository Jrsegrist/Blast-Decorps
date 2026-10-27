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
/* Port-phase shared declarations for the vehicle 1 driver below
 * (sideswipe_spawn, sideswipe_reground, sideswipe_update, sideswipe_build_model). The asm
 * reaches the record through $gp (gSideswipeState); the C reads it directly.
 * The helpers are copies of 6E200.c's (vehicles 3 and 4), trimmed to what
 * vehicle 1 uses (dedupe into a shared port header later). */
#define VEH_U8(v, off) (*(u8 *) ((u8 *) (v) + (off)))
#define VEH_S8(v, off) (*(s8 *) ((u8 *) (v) + (off)))
#define VEH_U16(v, off) (*(u16 *) ((u8 *) (v) + (off)))
#define VEH_S16(v, off) (*(s16 *) ((u8 *) (v) + (off)))


extern u8 gSideswipeState[];  /* vehicle 1 record */
extern u32 gSideswipePos[]; /* vehicle 1 PortVehPos */
extern u8 gSideswipeAnim[];  /* vehicle 1 animation channels */
#define VPOS1 ((PortVehPos *) gSideswipePos)

extern u8 gSideswipeNonBlockingParts[];
extern f32 gSideswipeDeflectTurnRate;


/* Place the vehicle model's parts (as 6E200.c's port_veh_parts): the current
 * matrix buffer (gBufferIndex ? bufA : bufB) plus the word at model +
 * model[0x18] + 4 is the model matrix m; gModelRotY = heading (u16 +0x4C),
 * mtx_build_model(x, y, z, scale, m); then veh_place_parts(x, y, z, id, model +
 * model[4], model + model[8], buffer) and veh_points_transform(id, model +
 * model[0], model + model[4], buffer). Returns the buffer (the asm's s4).
 * `r` carries mtx_chain_transform's pass-through registers: mtx_build_model leaves
 * a3 = 0 (from mtx_mul) and s2 = m in the asm, v1/a0 are still y/z at
 * veh_points_transform; s0/s1 come from the caller. They only matter for a
 * zero-count (untransformed) part, whose record then gets s0/s1/s2 >> 11. */
static u8 *port_veh_parts(u8 *veh, PortVehPos *p, s32 id, s32 scale, MtxChainRegs *r) {
    u8 *m = p->model;
    s32 off = *(s32 *) (m + *(s32 *) (m + 0x18) + 4);
    s32 *mtx;
    u8 *base;

    mtx = (s32 *) ((gBufferIndex != 0 ? (u8 *) p->bufA : (u8 *) p->bufB) + off);
    gModelRotY = VEH_U16(veh, VS_HEADING);
    mtx_build_model(p->x, p->y, p->z, scale, mtx);
    r->a3 = 0;
    r->s2 = (s32) mtx;
    base = (gBufferIndex != 0) ? (u8 *) p->bufA : (u8 *) p->bufB;
    m = p->model;
    veh_place_parts(p->x, p->y, p->z, id, m + *(s32 *) (m + 4), m + *(s32 *) (m + 8), base, r);
    r->v1 = p->y;
    r->a0 = p->z;
    m = p->model;
    veh_points_transform(id, (s16 *) (m + *(s32 *) m), (s16 *) (m + *(s32 *) (m + 4)), base, r);
    return base;
}

/* Put the vehicle on the ground (as 6E200.c's port_veh_ground):
 * veh_reground (key vid) at the position with the caller's fp, parts (s0 =
 * z, s1 = veh_reground's s1 in the asm), veh_set_record. The asm also hands
 * veh_reground the caller's f12-f26 (register leak; FP side results only,
 * zero here). */
static void port_veh_ground(u8 *veh, PortVehPos *p, s32 vid, s32 fp, u8 *(*parts)(MtxChainRegs *)) {
    TriSideOut f;
    Out802A9A60 o;
    MtxChainRegs mc;
    s32 z = p->z;

    f.pz = f.cross = f.cz = f.side = f.sideZ = f.dz = 0.0f;
    veh_reground((s16 *) (veh + 0x52), p->y, p->x, z, (s32 *) (veh + 4), &p->y, (s16 *) (veh + 0x4C), vid, fp,
                  veh, &f, &o);
    mc.s0 = z;
    mc.s1 = (s32) o.s1;
    parts(&mc);
    veh_set_record(p->z, vid, p->x, p->y, veh);
}

/* Vehicle setup, first part (as 6E200.c): model header, two 0x800-byte
 * matrix buffers from the heap, veh_add_record(id, 0, bufA, bufB, model),
 * veh_reset_state, wheel offset table (+0x52, 12 halves), position, heading
 * (+0x4C, +0x4E, +0x74). */
static void port_veh_init_head(u8 *veh, PortVehPos *p, s32 id, u8 *model, const s16 *wheels, s32 x, s32 y,
                               s32 z, s32 heading) {
    u8 *heap;
    s32 i;

    p->model = model;
    heap = gHeapPtr;
    p->bufA = (u64 *) heap;
    heap += 0x800;
    p->bufB = (u64 *) heap;
    heap += 0x800;
    gHeapPtr = heap;
    veh_add_record(id, 0, (s32) p->bufA, (s32) p->bufB, model);
    veh_reset_state(veh);
    for (i = 0; i < 12; i++) {
        VEH_S16(veh, 0x52 + i * 2) = wheels[i];
    }
    p->x = x;
    p->y = y;
    p->z = z;
    VEH_S16(veh, VS_HEADING) = heading;
    VEH_S16(veh, VS_MOVE_HEADING) = heading;
    VEH_S16(veh, VS_MOMENTUM_HEADING) = heading;
}

/* Second part: ground slots (veh_init_ground, key id, with the incoming fp;
 * the caller's f12-f26 also go in there in the asm: register leak, FP side
 * results only, zero here), animation channel 0 reset between the two
 * buffers, band table (+0x78, 15 halves). */
static void port_veh_init_anim(u8 *veh, PortVehPos *p, s32 id, u8 *ch, const s16 *bands, s32 x, s32 z, s32 fp) {
    TriSideOut f;
    u64 *a;
    u64 *b;
    s32 i;

    f.pz = f.cross = f.cz = f.side = f.sideZ = f.dz = 0.0f;
    veh_init_ground((s16 *) (veh + 0x52), p->y, x, z, (s32 *) (veh + 4), &p->y, (s16 *) (veh + 0x4C), id, fp,
                  veh, &f);
    a = p->bufA;
    b = p->bufB;
    anim_init_model((u32 *) b, (u32 *) a, (AnimChannel *) ch, p->model);
    anim_set_speed((AnimChannel *) ch, 0, 100);
    anim_set_reverse((AnimChannel *) ch, 0, 0);
    anim_set_loop_mode((AnimChannel *) ch, 0, 0);
    anim_set_interp(0.0f, (AnimChannel *) ch, 0, 0);
    anim_play((AnimChannel *) ch, 0, 1);
    anim_update_model((u8 *) a, (u8 *) b, (AnimChannel *) ch);
    anim_rewind(0, (AnimChannel *) ch);
    anim_play((AnimChannel *) ch, 0, 1);
    anim_update_model((u8 *) b, (u8 *) a, (AnimChannel *) ch);
    for (i = 0; i < 15; i++) {
        VEH_S16(veh, 0x78 + i * 2) = bands[i];
    }
}

/* Last part: veh_register_parts(id, model + model[4], model + model[8], scale),
 * veh_add_shadow(id, a1, a2, a2), one update with +0x9A set, and the model
 * matrix copied bufB -> bufA (mtx_copy_at). */
static void port_veh_init_tail(u8 *veh, PortVehPos *p, s32 id, s32 scale, s32 a1, s32 a2,
                               void (*update)(s32, s32, s32, s32, s32, s32, s32)) {
    u8 *m = p->model;

    veh_register_parts(id, m + *(s32 *) (m + 4), m + *(s32 *) (m + 8), scale);
    veh_add_shadow(id, a1, a2, a2);
    VEH_U8(veh, VS_SPAWN_UPDATE) = 1;
    /* The asm enters the update with t6, t7, s0-s4 as veh_add_shadow (C) and
     * anim_update_model left them (register leak); they only pass through
     * veh_zone_alpha unused. */
    update(0, 0, 0, 0, 0, 0, 0);
    VEH_U8(veh, VS_SPAWN_UPDATE) = 0;
    m = p->model;
    mtx_copy_at((u8 *) p->bufB, (u8 *) p->bufA, *(s32 *) (m + *(s32 *) (m + 0x18) + 4));
}

/* Middle of the per-frame update (as 6E200.c's port_veh_move): speed/heading
 * (veh_turn_heading with `rate`), drive force (veh_slope, veh_apply_slope
 * kind/div), veh_set_target_heading when `drift`, move along the heading
 * (veh_move_along_heading), gBounceSound = 1, ground contact (veh_ground_contact), animation
 * (anim_update_model), parts, veh_collision_setup, veh_collide_spheres, veh_collide_world. */
static void port_veh_move(u8 *veh, PortVehPos *p, s32 id, u8 *ch, s32 rate, s32 kind, f32 div, s32 drift,
                          s16 *driftAngle, s32 divB, s32 divA, u8 *(*parts)(MtxChainRegs *), u8 *a1, s32 b2,
                          s32 b0, s32 h2) {
    Out802A860C o;
    Regs802A8768 r8;
    TriSideOut f;
    MtxChainRegs mc;
    f32 fl;
    s32 x;

    veh_turn_heading(veh, (u16 *) (veh + 0x74), rate, (s16 *) (veh + 0x76), (u16 *) (veh + 0x4C),
                  (u16 *) (veh + 0x4E), (s8 *) (veh + 0x99), 1);
    fl = veh_slope((s16 *) (veh + 0x76), veh + 0x96, (s32 *) (veh + 4), (f32 *) veh);
    veh_apply_slope(veh, (s16 *) (veh + 0x76), kind, (s8 *) (veh + 0x96), (s32 *) (veh + 4), 1, div);
    if (drift != 0) {
        veh_set_target_heading(veh, driftAngle);
    }
    x = veh_move_along_heading(fl, VEH_U16(veh, VS_MOVE_HEADING), (s16 *) (veh + 0x76), &p->x, &p->z, &o);
    gBounceSound = 1;
    r8.s3 = o.s3;
    /* f12-f26 here: veh_move_along_heading's temporaries and the caller's registers
     * (register leak, FP side results only). */
    f.pz = f.cross = f.cz = f.side = f.sideZ = f.dz = 0.0f;
    veh_ground_contact(veh, id, &p->x, &p->y, &p->z, x, o.t1, divB, divA, (s16 *) (veh + 0x4C), veh + 0x96,
                  (s16 *) (veh + 0x52), (s32 *) (veh + 0x28), (s32 *) (veh + 0x40), (s32 *) (veh + 0x34),
                  (s32 *) (veh + 4), &r8, &f);
    if (gBufferIndex != 0) {
        anim_update_model((u8 *) p->bufA, (u8 *) p->bufB, (AnimChannel *) ch);
    } else {
        anim_update_model((u8 *) p->bufB, (u8 *) p->bufA, (AnimChannel *) ch);
    }
    /* s0/s1: anim_update_model's leftovers in the asm (register leak). */
    mc.s0 = 0;
    mc.s1 = 0;
    parts(&mc);
    veh_collision_setup(p->z, (s32) a1, b2, 1, p->x, p->y, b0, VEH_S16(veh, VS_SPEED), h2, 0, id, veh);
    veh_collide_spheres(id, veh);
    veh_collide_world(id);
}

/* The turn after a collision report (as 6E200.c's port_veh_turn). */
static void port_veh_turn(u8 *veh, f32 scale, s16 *target) {
    s32 d;
    s32 cur;
    s32 turn;
    s32 tmp;

    d = VEH_U16(veh, VS_MOVE_HEADING) - 0x800;
    if (d < 0) {
        d += 0xFFF;
    }
    d -= veh_free_arc_mid();
    if (d < 0) {
        d = -d;
    }
    if (d > 0x800) {
        d = 0xFFF - d;
    }
    veh_reverse_if_behind(veh);
    turn = veh_turn_into_free_arc(veh, VEH_U16(veh, VS_MOVE_HEADING), VEH_U16(veh, VS_HEADING), &cur, scale);
    *target = cur;
    VEH_S16(veh, VS_MOVE_HEADING) = cur;
    VEH_S16(veh, VS_MOMENTUM_HEADING) = cur;
    veh_step_heading(veh, turn, d, &tmp);
    veh_hit_slowdown(veh, 0);
}

/* Publish position, speed, headings, then the veh_set_record record. */
static void port_veh_publish(u8 *veh, PortVehPos *p, s32 id) {
    gPlayerX = p->x;
    gPlayerY = p->y;
    gPlayerZ = p->z;
    gPlayerSpeed = VEH_S16(veh, VS_SPEED);
    gPlayerMoveHeading = VEH_U16(veh, VS_MOVE_HEADING);
    gPlayerHeading = VEH_U16(veh, VS_HEADING);
    veh_set_record(p->z, id, p->x, p->y, veh);
}

u8 *sideswipe_build_model(MtxChainRegs *r);

static const s16 sVeh1Wheels[12] = { 0x168, 0x168, -0x168, 0x168, 0x168, -0x168,
                                     0x17C, 0x17C, -0x17C, 0x17C, 0x17C, -0x17C };
static const s16 sVeh1Bands[15] = { -0xB4, 0, 5, 0, 0x3C, 5, 0x3C, 0x64, 4, 0x64, 0xB4, 3, 0xB4, 0xDC, 2 };
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
extern u8 gSideswipeSteerAnim;
extern u8 gSideswipeRamCooldown;
extern s8 gSideswipeDeflecting;

/* Sets up Sideswipe (type 1); sets gHaveSideswipe (0x802AFC60).
 * Vehicle 1 setup, called from the asm dispatcher lvl_spawn_vehicles: model
 * header, buffers and record (port_veh_init_head with this vehicle's wheel
 * table), level gSideswipeSteerAnim = 50, side-ram uses gSideswipeRamCharges = 0, cooldown
 * gSideswipeRamCooldown = 0, gSideswipeDeflecting = 0, ground slots and animation reset
 * (port_veh_init_anim, band table), parts (scale 0x1B58),
 * veh_add_shadow(1, 0x96, 0x3C, 0x3C), one update (sideswipe_update with +0x9A
 * set), buffer copy, gHaveSideswipe = 1. (No veh_widen_bands, unlike vehicles 3
 * and 4.)
 * Register convention: model s2, x t7, y s3, z s0, heading s1, fp (the ground
 * slot flag seed for veh_init_ground) (conventions.txt). The asm saves t0-t5
 * (its caller keeps t1, t2 live). Register leaks not modelled: the caller's
 * f12-f26 go into veh_init_ground (FP side results only), and the dispatcher
 * reads s2-s4, fp and f12-f26 afterwards, which hold whatever sideswipe_update /
 * anim_update_model / mtx_build_model left (a mixed N64 build would need a
 * thunk). */
void sideswipe_spawn(u8 *model, s32 x, s32 y, s32 z, s32 heading, s32 fp) {
    u8 *veh = gSideswipeState;
    PortVehPos *p = VPOS1;

    port_veh_init_head(veh, p, 1, model, sVeh1Wheels, x, y, z, heading);
    gSideswipeSteerAnim = 50;
    gSideswipeRamCharges = 0;
    gSideswipeRamCooldown = 0;
    gSideswipeDeflecting = 0;
    port_veh_init_anim(veh, p, 1, gSideswipeAnim, sVeh1Bands, x, z, fp);
    port_veh_init_tail(veh, p, 1, 0x1B58, 0x96, 0x3C, sideswipe_update);
    gHaveSideswipe = 1;
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/6B4A0/sideswipe_spawn.s")
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
extern u8 gSideswipeState[]; /* this vehicle's state block */
extern u8 gSideswipeAnim[]; /* animation channel table (AnimChannel, 56040.c) */
extern u8 gSideswipeTexAnim[]; /* key of this vehicle's anim_tex_find entry */

/* Getting into Sideswipe (0x802AFFD4).
 * Enter vehicle type 1 (called from 00000.c / 17210.c): clears byte 0x99 of
 * the state block, sets fields 0x14 / 0x11 / 0x12 of animation channels 1, 2,
 * 5, 3, 2 of gSideswipeAnim and restarts all but 5 with -1 (anim_play), sets
 * the gSideswipeTexAnim entry's fields (100, 0, 1) and restarts it, gCamDistance / gCamHeightBase =
 * 3400, 300, then veh_start_engine_sound(a0, 0x36) with a0 = gSideswipeAnim left over from
 * the setters (veh_start_engine_sound ignores it). The asm points $gp at gSideswipeState and
 * leaves it there (conventions.txt: clobbers gp) and returns with v1 = -1
 * (the last setter's v1); C callers ignore both. */
void sideswipe_enter(void) {
    gSideswipeState[0x99] = 0;
    anim_set_speed((AnimChannel *) gSideswipeAnim, 1, 0);
    anim_set_reverse((AnimChannel *) gSideswipeAnim, 1, 0);
    anim_set_loop_mode((AnimChannel *) gSideswipeAnim, 1, 0);
    anim_play((AnimChannel *) gSideswipeAnim, 1, -1);
    anim_set_speed((AnimChannel *) gSideswipeAnim, 2, 0);
    anim_set_reverse((AnimChannel *) gSideswipeAnim, 2, 0);
    anim_set_loop_mode((AnimChannel *) gSideswipeAnim, 2, 0);
    anim_play((AnimChannel *) gSideswipeAnim, 2, -1);
    anim_set_speed((AnimChannel *) gSideswipeAnim, 5, 7);
    anim_set_reverse((AnimChannel *) gSideswipeAnim, 5, 0);
    anim_set_loop_mode((AnimChannel *) gSideswipeAnim, 5, 1);
    anim_set_speed((AnimChannel *) gSideswipeAnim, 3, 0);
    anim_set_reverse((AnimChannel *) gSideswipeAnim, 3, 0);
    anim_set_loop_mode((AnimChannel *) gSideswipeAnim, 3, 1);
    anim_play((AnimChannel *) gSideswipeAnim, 3, -1);
    anim_set_speed((AnimChannel *) gSideswipeAnim, 2, 0);
    anim_set_reverse((AnimChannel *) gSideswipeAnim, 2, 0);
    anim_set_loop_mode((AnimChannel *) gSideswipeAnim, 2, 1);
    anim_play((AnimChannel *) gSideswipeAnim, 2, -1);
    anim_tex_set_speed((s32) gSideswipeTexAnim, 100);
    anim_tex_set_reverse((s32) gSideswipeTexAnim, 0);
    anim_tex_set_loop_mode((s32) gSideswipeTexAnim, 1);
    anim_tex_play((s32) gSideswipeTexAnim, -1);
    gCamDistance = 0xD48;
    gCamHeightBase = 0x12C;
    veh_start_engine_sound((s32) gSideswipeAnim, 0x36);
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/6B4A0/sideswipe_enter.s")
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
extern u8 gSideswipeState[]; /* this vehicle's state block */
extern u8 gSideswipeAnim[]; /* animation channel table (AnimChannel, 56040.c) */
/* Reads channel idx of base into out[0..7] (56040.c; asm convention in
 * tools_port/conventions.txt): out[0] = (s8) field 0x10, out[3] = field
 * 0x14, out[4] = (u16) field 0xC, ... */

/* 0 airborne, 5 while the side rams are out, else 1 (0x802B01DC).
 * Exit check for this vehicle type (called from veh_exit_check in hd.c, which
 * declares it void and returns the leftover v0): 0 when any of the bytes at
 * +0x96, +0x97, +0x98 of gSideswipeState is 1; else 5 when channel 5 of
 * gSideswipeAnim has field 0x10 == 1 (the asm returns the channel index it left
 * in v0, which anim_get_state preserves; kept as is); else 1. Returns s32
 * (the asm's v0). The asm saves and restores $gp; the v1 it leaves
 * (channel 5's field 0x10) isn't used by the C caller. */
s32 sideswipe_exit_check(void) {
    s32 ch[8];

    if (gSideswipeState[0x96] == 1 || gSideswipeState[0x97] == 1 || gSideswipeState[0x98] == 1) {
        return 0;
    }
    anim_get_state(5, (AnimChannel *) gSideswipeAnim, ch);
    if (ch[0] == 1) {
        return 5;
    }
    return 1;
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/6B4A0/sideswipe_exit_check.s")
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
extern u64 * N64P gSideswipeMtxA; /* save copy pair (veh_sync_buffers) */
extern u64 * N64P gSideswipeMtxB;

/* Getting out of Sideswipe (0x802B0254).
 * Leave vehicle type 1 (called from hd.c): zeroes the speed (s16 at +0x76 of
 * the state block), veh_sync_buffers(gSideswipeMtxA, gSideswipeMtxB, 0x800), then
 * veh_stop_engine_sounds(). The asm saves and restores $gp. */
void sideswipe_leave(void) {
    *(s16 *) (gSideswipeState + 0x76) = 0;
    veh_sync_buffers(gSideswipeMtxA, gSideswipeMtxB, 0x800);
    veh_stop_engine_sounds();
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/6B4A0/sideswipe_leave.s")
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
/* Placed Sideswipe on a moving level part: re-ground it (0x802B02A0).
 * Put vehicle 1 on the ground (called from C veh_update_others in 00000.c as
 * `void (void)`): port_veh_ground with key 1, then sideswipe_build_model.
 * Register convention: fp in (conventions.txt). The C caller doesn't set fp:
 * the asm passes on whatever an outer function left in $fp/$s8 (register
 * leak; it seeds veh_reground's gSlotSurface flags), and its f12-f26 into the
 * scans. The asm saves every callee-saved register. */
void sideswipe_reground(s32 fp) {
    port_veh_ground(gSideswipeState, VPOS1, 1, fp, sideswipe_build_model);
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/6B4A0/sideswipe_reground.s")
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
extern u32 gSideswipePos[]; /* x, y, z */
/* Veh_zone_alpha at Sideswipe's position (0x802B03B0).
 * Zone level lookup (veh_zone_alpha) for vehicle id 1 at its position
 * gSideswipePos..+8; returns veh_zone_alpha's v1 (the zone list end).
 * Register convention: the asm passes veh_zone_alpha's scan registers t6, t7,
 * s0-s4 through (in and out; here through r), leaves a3 = id and clobbers
 * s5, s6 (conventions.txt). */
s32 sideswipe_zone_alpha(ZoneScanRegs *r) {
    return veh_zone_alpha(1, gSideswipePos[0], gSideswipePos[1], gSideswipePos[2], r);
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/6B4A0/sideswipe_zone_alpha.s")
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
s32 sideswipe_zone_alpha(ZoneScanRegs *r);
void sideswipe_update_anims(void);
void sideswipe_set_physics(void);
s32 sideswipe_steer_rate(void);
s32 sideswipe_check_ram_hit(void);
extern s8 gSideswipeDeflecting;
extern s16 gSideswipeDeflectHeading;

/* Sideswipe driven by the player, per frame (0x802B03F4).
 * Vehicle 1 per-frame update (called from C veh_update_player in 00000.c as
 * `void (void)`, and from sideswipe_spawn):
 * 1. zone level (sideswipe_zone_alpha) with the incoming scan registers; the
 *    animation/sound update sideswipe_update_anims unless +0x9A is set;
 * 2. setup leaf sideswipe_set_physics, steering + speed bands
 *    veh_steer_throttle(sideswipe_steer_rate(), ...) (no countdown, no state save, no
 *    veh_start_turbo, unlike vehicles 3 and 4);
 * 3. port_veh_move (rate 0x4650, kind 1 / 720.0, drift gSideswipeDeflecting via
 *    gSideswipeDeflectHeading, divisors 0x2D0 / 0x2D0, parts sideswipe_build_model, veh_collision_setup
 *    with gSideswipeNonBlockingParts, 0, 7, 100);
 * 4. collision pass obj_collide_vehicle (gCollVehicleAnim = the channel table), the
 *    horn channel check sideswipe_check_ram_hit; with a collision report
 *    (gCollBlocked): veh_prepare_escape, gSideswipeDeflecting = 1 and port_veh_turn
 *    (gSideswipeDeflectTurnRate, gSideswipeDeflectHeading); else gSideswipeDeflecting = 0. (A bounce of the speed
 *    follows an unconditional branch in the asm: dead code.)
 * 5. port_veh_publish.
 * Register convention: t6, t7, s0-s4 in (only through veh_zone_alpha; from
 * the C caller they are whatever it left: register leak). The asm saves all
 * callee-saved registers and leaves v1 = y; asm caller sideswipe_spawn reads
 * f12/f14 afterwards (mtx_build_model's leftovers, not modelled). */
void sideswipe_update(s32 t6, s32 t7, s32 s0, s32 s1, s32 s2, s32 s3, s32 s4) {
    u8 *veh = gSideswipeState;
    PortVehPos *p = VPOS1;
    ZoneScanRegs zs;

    zs.t6 = t6;
    zs.t7 = t7;
    zs.s0 = s0;
    zs.s1 = s1;
    zs.s2 = s2;
    zs.s3 = s3;
    zs.s4 = s4;
    sideswipe_zone_alpha(&zs);
    if (VEH_S8(veh, VS_SPAWN_UPDATE) == 0) {
        sideswipe_update_anims();
    }
    sideswipe_set_physics();
    veh_steer_throttle(sideswipe_steer_rate(), (u16 *) (veh + 0x4C), veh, (s16 *) (veh + 0x76), 3, veh + 0x96,
                  (s16 *) (veh + 0x78), 0x14);
    port_veh_move(veh, p, 1, gSideswipeAnim, 0x4650, 1, 720.0f, gSideswipeDeflecting, &gSideswipeDeflectHeading, 0x2D0, 0x2D0,
                  sideswipe_build_model, gSideswipeNonBlockingParts, 0, 7, 100);
    gCollVehicleAnim = gSideswipeAnim;
    obj_collide_vehicle(1, veh);
    sideswipe_check_ram_hit();
    if (gCollBlocked == 0) {
        gSideswipeDeflecting = 0;
    } else {
        veh_prepare_escape(veh);
        gSideswipeDeflecting = 1;
        port_veh_turn(veh, gSideswipeDeflectTurnRate, &gSideswipeDeflectHeading);
    }
    port_veh_publish(veh, p, 1);
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/6B4A0/sideswipe_update.s")
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
extern f32 gSideswipeYawThirdWidth;
extern u8 gSideswipeRamCooldown;    /* horn cooldown (frames) */
extern u8 gSideswipeSteerAnim;    /* level 0..100 */

/* Animations, engine pitch, the side rams on L/R (sound 0x37) (0x802B07DC).
 * Per-frame animation/sound update of vehicle 1 (asm caller sideswipe_update;
 * the asm reads the state block through $gp = gSideswipeState, read directly
 * here). Channel 2 of gSideswipeAnim gets the position within one of three
 * 0x555-wide thirds of the angle gCamYaw + 0x800 (wrapped by 0xFFF) and
 * the third (0, 1, else 2). When the cooldown gSideswipeRamCooldown is 0, channel 5 isn't
 * running (field 0x10 != 1), gHeldL or gHeldR is set and gSideswipeRamCharges is
 * nonzero: one use is taken, channel 5 restarts with 2, the cooldown is set
 * to 10 and sound 0x37 starts; otherwise a nonzero cooldown counts down. The
 * level gSideswipeSteerAnim moves by 5 (down to 0 with gHeldLeft, up to 100 with
 * gHeldRight, else towards 50) and goes to channel 3 as level / 100. Channel
 * 1 gets direction (speed < 0) and |speed| / 14, which also goes to
 * veh_set_engine_level. The asm clobbers s4 and s5 (conventions.txt). */
void sideswipe_update_anims(void) {
    s32 a = ((u16) gCamYaw) + 0x800;
    s32 third;
    f32 pos;
    s32 ch[8];
    s32 level;
    s32 speed;

    if (a >= 0x1000) {
        a -= 0xFFF;
    }
    pos = (f32) (s32) ((u32) a % 0x555) / gSideswipeYawThirdWidth;
    third = (u32) a / 0x555;
    if (third == 0) {
        anim_set_frame(pos, (AnimChannel *) gSideswipeAnim, 2, 0);
    } else if (third == 1) {
        anim_set_frame(pos, (AnimChannel *) gSideswipeAnim, 2, 1);
    } else {
        anim_set_frame(pos, (AnimChannel *) gSideswipeAnim, 2, 2);
    }

    if (gSideswipeRamCooldown != 0) {
        gSideswipeRamCooldown--;
    } else {
        anim_get_state(5, (AnimChannel *) gSideswipeAnim, ch);
        if (ch[0] != 1 && (gHeldL != 0 || gHeldR != 0) && gSideswipeRamCharges != 0) {
            gSideswipeRamCharges--;
            anim_play((AnimChannel *) gSideswipeAnim, 5, 2);
            gSideswipeRamCooldown = 10;
            snd_play(gSfxBank, SFX_SIDESWIPE_RAM, NULL);
        }
    }

    level = gSideswipeSteerAnim;
    if (gHeldLeft != 0) {
        level -= 5;
        if (level < 0) {
            level = 0;
        }
    } else if (gHeldRight != 0) {
        level += 5;
        if (level >= 0x65) {
            level = 0x64;
        }
    } else if (level < 0x32) {
        level += 5;
        if (level >= 0x33) {
            level = 0x32;
        }
    } else {
        level -= 5;
        if (level < 0x32) {
            level = 0x32;
        }
    }
    gSideswipeSteerAnim = level;
    anim_set_frame((f32) level / 100.0f, (AnimChannel *) gSideswipeAnim, 3, 0);

    speed = *(s16 *) (gSideswipeState + 0x76);
    if (speed < 0) {
        anim_set_reverse((AnimChannel *) gSideswipeAnim, 1, 1);
        speed = -speed;
    } else {
        anim_set_reverse((AnimChannel *) gSideswipeAnim, 1, 0);
    }
    if (speed != 0) {
        speed = (u32) speed / 14;
    }
    anim_set_speed((AnimChannel *) gSideswipeAnim, 1, speed);
    veh_set_engine_level(speed);
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/6B4A0/sideswipe_update_anims.s")
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING

/* The rams hit an object of kind 4/5: camera shake (0x802B0AAC).
 * When channel 5 of gSideswipeAnim is active (field 0x10 != 0) and
 * veh_contacts_has(4) or veh_contacts_has(5) is nonzero: sets its field 0x11 to 1,
 * restarts it with value 1 (anim_play) and sets gShakeTime = 10,
 * gShakeSize = 600. Returns channel 5's (u16) field 0xC as read at the start
 * (the asm leaves it in a3, which asm caller sideswipe_update reads;
 * conventions.txt). The asm also clobbers s5 (10 / 600 scratch); its caller
 * keeps f12, f14 live (a mixed N64 build would need a thunk; the native port
 * won't). */
s32 sideswipe_check_ram_hit(void) {
    s32 ch[8];

    anim_get_state(5, (AnimChannel *) gSideswipeAnim, ch);
    if (ch[0] != 0 && (veh_contacts_has(4) != 0 || veh_contacts_has(5) != 0)) {
        anim_set_reverse((AnimChannel *) gSideswipeAnim, 5, 1);
        anim_play((AnimChannel *) gSideswipeAnim, 5, 1);
        gShakeTime = 10;
        gShakeSize = 600;
    }
    return ch[4];
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/6B4A0/sideswipe_check_ram_hit.s")
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
/* Sideswipe's model matrix, parts and points (0x802B0B3C).
 * Place vehicle 1's model parts (port_veh_parts, id 1, scale 0x1B58).
 * Returns the current matrix buffer (the asm's s4).
 * Register convention: s0, s1 in through r (only read for a zero-count part),
 * s1 out; s4 = return value; clobbers s1, s2, s4-s7 (conventions.txt). Asm
 * caller sideswipe_update keeps t6, t7 live and reads f12/f14 afterwards
 * (mtx_build_model's temporaries, not modelled). */
u8 *sideswipe_build_model(MtxChainRegs *r) {
    return port_veh_parts(gSideswipeState, VPOS1, 1, 0x1B58, r);
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/6B4A0/sideswipe_build_model.s")
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

extern u8 gSideswipeState[]; /* this vehicle's state block (the asm's $gp) */
extern f32 gSideswipeSteerRateDiv;

/* Sideswipe's steering rate (by speed) (0x802B0C74).
 * Speed (s16 at +0x76) scaled for the engine sound: rounded speed / 6.0 when
 * any of the bytes at +0x96/+0x97/+0x98 is 1, else speed / gSideswipeSteerRateDiv.
 * The asm returns it in s3 (see tools_port/conventions.txt). Its asm caller
 * sideswipe_update keeps a0-a3 live across the call (a mixed N64 build would
 * need a thunk). Same shape as skyfall_steer_rate (6E200), backlash_steer_rate (71140),
 * amdream_steer_rate (72B80), ballista_steer_rate (853D0), police_steer_rate (86F60),
 * ateam_steer_rate (88160), starski_steer_rate (8AEE0); only the divisors differ. */
s32 sideswipe_steer_rate(void) {
    f32 div;
    s32 r;

    if (gSideswipeState[0x96] == 1 || gSideswipeState[0x97] == 1 || gSideswipeState[0x98] == 1) {
        div = 6.0f;
    } else {
        div = gSideswipeSteerRateDiv;
    }
    CVT_W_S(r, *(s16 *) (gSideswipeState + 0x76) / div);
    return r;
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/6B4A0/sideswipe_steer_rate.s")
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING

/* Gravity x4, landing bounce 40/3 (0x802B0CE8).
 * Vehicle-module setup leaf, identical to driver_set_physics (69BB0):
 * gGravity = gLevelGravity * 4, gBounceThreshold/Damping = 40, 3.
 * Register note: the asm touches only at/v0/v1/f0/f2. Its asm caller
 * sideswipe_update keeps a0-a3 live across the call; a mixed N64 build would
 * need a thunk preserving those, the native port does not. */
void sideswipe_set_physics(void) {
    gGravity = gLevelGravity * 4.0f;
    gBounceThreshold = 40;
    gBounceDamping = 3;
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/6B4A0/sideswipe_set_physics.s")
#endif

/* sideswipe_save_state: two-address trampoline into veh_serialize_state, same
 * confirmed-unreachable-from-C 8-byte sd-$ra frame as veh_check_wrap
 * (hd_code/679E0.c). Permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
extern u8 gSideswipeState[];  /* this vehicle's state block: 0xA6 bytes are (de)serialized */
extern u32 gSideswipePos[]; /* plus these three words */

/* Sideswipe's state into a buffer (no callers) (0x802B0D44).
 * Serialize this vehicle's state (gSideswipeState[0..0xA5] plus the three words
 * gSideswipePos[0..2]) into `dst` via veh_serialize_state. Returns the callee's result
 * (0xB2, the byte count): the asm leaves the callee's v0 untouched. */
s32 sideswipe_save_state(u8 *dst) {
    return veh_serialize_state(dst, gSideswipeState, gSideswipePos);
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/6B4A0/sideswipe_save_state.s")
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
/* Sideswipe's state from a buffer (recording start) (0x802B0D70).
 * Inverse of sideswipe_save_state: restore this vehicle's state from `src` via
 * veh_deserialize_state. Called from 17210.c as `void sideswipe_load_state(void *)`. */
void sideswipe_load_state(void *src) {
    veh_deserialize_state(src, gSideswipeState, gSideswipePos);
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/6B4A0/sideswipe_load_state.s")
#endif
