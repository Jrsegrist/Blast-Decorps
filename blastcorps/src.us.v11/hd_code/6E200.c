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
/* Port-phase shared declarations for the vehicle 3 / vehicle 4 drivers below
 * (skyfall_spawn .. ramdozer_build_model). The asm reaches each vehicle's record
 * through $gp (gSkyfallState / gRamdozerState); the C reads the record directly. */
#define VEH_U8(v, off) (*(u8 *) ((u8 *) (v) + (off)))
#define VEH_S8(v, off) (*(s8 *) ((u8 *) (v) + (off)))
#define VEH_U16(v, off) (*(u16 *) ((u8 *) (v) + (off)))
#define VEH_S16(v, off) (*(s16 *) ((u8 *) (v) + (off)))


extern u8 gSkyfallState[];  /* vehicle 3 record */
extern u8 gRamdozerState[];  /* vehicle 4 record */
extern u32 gSkyfallPos[]; /* vehicle 3 PortVehPos */
extern u32 gRamdozerPos[]; /* vehicle 4 PortVehPos */
extern u8 gSkyfallAnim[];  /* vehicle 3 animation channels */
extern u8 gRamdozerAnim[];  /* vehicle 4 animation channels */
#define VPOS3 ((PortVehPos *) gSkyfallPos)
#define VPOS4 ((PortVehPos *) gRamdozerPos)

extern u8 gSkyfallNonBlockingParts[];
extern u8 gRamdozerNonBlockingParts[];
extern f32 gSkyfallDeflectTurnRate;
extern f32 gRamdozerDeflectTurnRate;


/* Shared body of skyfall_build_model / ramdozer_build_model: place the vehicle model's
 * parts. The current matrix buffer (gBufferIndex ? bufA : bufB) plus the word
 * at model + model[0x18] + 4 is the model matrix m; gModelRotY = heading
 * (u16 +0x4C), mtx_build_model(x, y, z, scale, m); then veh_place_parts(x, y, z,
 * id, model + model[4], model + model[8], buffer) and veh_points_transform(id,
 * model + model[0], model + model[4], buffer). Returns the buffer (the asm's
 * s4). `r` carries mtx_chain_transform's pass-through registers: mtx_build_model
 * leaves a3 = 0 (from mtx_mul) and s2 = m in the asm, and v1/a0 are
 * still y/z (veh_place_parts preserves them) at veh_points_transform; s0/s1 come from
 * the caller. They only matter for a zero-count (untransformed) part, whose
 * record then gets s0/s1/s2 >> 11. */
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

/* Shared body of skyfall_ride_attach / ramdozer_ride_attach: the value pairs at the
 * vehicle's position and at its front wheel (offset 0 of the wheel table at
 * +0x52 rotated by the heading at +0x4C) on triangle `id` (veh_deck_to_local) go
 * to +0x6A/+0x6C and +0x70/+0x72; +0x6E = heading. r's f24 chains through
 * both lookups. Returns veh + 0x52 (the asm's v1, preserved through the
 * callees). */
static s32 port_veh_track(u8 *veh, PortVehPos *p, s32 id, InterpRegs *r) {
    s32 x = p->x;
    s32 z = p->z;
    s32 dx;
    s32 dz;

    veh_deck_to_local(id, x, z, r);
    VEH_S16(veh, VS_DECK_POS) = r->t3;
    VEH_S16(veh, 0x6C) = r->t4;
    VEH_S16(veh, VS_DECK_HEADING) = VEH_U16(veh, VS_HEADING);
    dx = veh_wheel_offset(0, (s16 *) (veh + 0x52), (s16 *) (veh + 0x4C), &dz);
    veh_deck_to_local(id, x + dx, z + dz, r);
    VEH_S16(veh, VS_DECK_FRONT) = r->t3;
    VEH_S16(veh, 0x72) = r->t4;
    return (s32) (veh + 0x52);
}

/* InterpRegs FP results <-> the TriSideOut veh_ground_contact takes (f12 .. f26). */
static void port_interp_to_tri(InterpRegs *r, TriSideOut *f) {
    f->pz = r->f12;
    *(s32 *) &f->cross = r->f14;
    f->cz = r->f20;
    f->side = r->f22;
    f->sideZ = r->f24;
    f->dz = r->f26;
}

static void port_tri_to_interp(TriSideOut *f, InterpRegs *r) {
    r->f12 = f->pz;
    r->f14 = *(s32 *) &f->cross;
    r->f20 = f->cz;
    r->f22 = f->side;
    r->f24 = f->sideZ;
    r->f26 = f->dz;
}

/* Shared body of skyfall_ride_follow / ramdozer_ride_follow (drive along triangle `id`):
 * heading h = veh_follow_heading from the two value pairs at +0x6A/+0x6C and
 * +0x70/+0x72 (and the target +0x6E) -> +0x4E and +0x4C; the pair at
 * +0x6A/+0x6C again by veh_deck_to_world is the position for the ground contact
 * veh_ground_contact (vid, divB, divA), whose s3 input is veh_follow_heading's
 * distance and whose FP inputs are veh_deck_to_world's results; `setup` (the
 * vehicle's setup leaf) runs before it, and gBounceSound = 1 when `flag40B`.
 * Then parts (`parts`; s0 = flags pointer, s1 = &z in the asm) and the
 * veh_set_record record. Returns veh_ground_contact's s3. */
static s32 port_veh_follow(u8 *veh, PortVehPos *p, s32 vid, s32 id, InterpRegs *r, void (*setup)(void),
                           s32 flag40B, s32 divB, s32 divA, u8 *(*parts)(MtxChainRegs *)) {
    Regs802A8768 r8;
    TriSideOut f;
    MtxChainRegs mc;
    s32 s3;
    s32 h;

    h = veh_follow_heading((s16 *) (veh + 0x52), VEH_S16(veh, VS_DECK_POS), (u16 *) (veh + 0x6E), id, VEH_S16(veh, 0x6C),
                      VEH_S16(veh, VS_DECK_FRONT), VEH_S16(veh, 0x72), &s3, r);
    VEH_S16(veh, VS_MOVE_HEADING) = h;
    VEH_S16(veh, VS_HEADING) = h;
    veh_deck_to_world(id, VEH_S16(veh, VS_DECK_POS), VEH_S16(veh, 0x6C), r);
    setup();
    if (flag40B) {
        gBounceSound = 1;
    }
    r8.s3 = s3;
    port_interp_to_tri(r, &f);
    veh_ground_contact(veh, vid, &p->x, &p->y, &p->z, r->t3, r->t4, divB, divA, (s16 *) (veh + 0x4C), veh + 0x96,
                  (s16 *) (veh + 0x52), (s32 *) (veh + 0x28), (s32 *) (veh + 0x40), (s32 *) (veh + 0x34),
                  (s32 *) (veh + 4), &r8, &f);
    port_tri_to_interp(&f, r);
    mc.s0 = (s32) (veh + 0x96);
    mc.s1 = (s32) &p->z;
    parts(&mc);
    veh_set_record(p->z, vid, p->x, p->y, veh);
    return r8.s3;
}

/* Shared body of skyfall_reground / ramdozer_reground (put the vehicle on the
 * ground): veh_reground (key vid) at the position, with the caller's fp;
 * parts (s0 = z, s1 = veh_reground's s1 in the asm); veh_set_record. The
 * asm also hands veh_reground the caller's f12-f26 (a register leak of the
 * C caller that only feeds the FP side results; zero here). */
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

/* First part of the vehicle setup (skyfall_spawn / ramdozer_spawn): model
 * header, two 0x800-byte matrix buffers from the heap, veh_add_record(id, 0,
 * bufA, bufB, model), veh_reset_state, the wheel offset table (+0x52, 12
 * halves), position, heading (+0x4C, +0x4E, +0x74). */
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

/* Second part: ground slots (veh_init_ground, key id, with the incoming fp; the
 * caller's f12-f26 also go in there in the asm, a register leak that only
 * feeds FP side results: zero here), animation channel 0 reset between the
 * two buffers (anim_init_model, setters, anim_update_model both ways), band table
 * (+0x78, 15 halves). */
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
 * veh_add_shadow(id, a1, a2, a2), one run of the per-frame update with +0x9A
 * set (`update`, which then skips its sound/effects step), and the current
 * frame's model matrix copied bufB -> bufA (mtx_copy_at). */
static void port_veh_init_tail(u8 *veh, PortVehPos *p, s32 id, s32 scale, s32 a1, s32 a2,
                               void (*update)(s32, s32, s32, s32, s32, s32, s32)) {
    u8 *m = p->model;

    veh_register_parts(id, m + *(s32 *) (m + 4), m + *(s32 *) (m + 8), scale);
    veh_add_shadow(id, a1, a2, a2);
    VEH_U8(veh, VS_SPAWN_UPDATE) = 1;
    /* The asm enters the update with t6, t7, s0-s4 as veh_add_shadow (C) and
     * anim_update_model left them (register leak); with +0x9A set they only pass
     * through veh_zone_alpha unused. */
    update(0, 0, 0, 0, 0, 0, 0);
    VEH_U8(veh, VS_SPAWN_UPDATE) = 0;
    m = p->model;
    mtx_copy_at((u8 *) p->bufB, (u8 *) p->bufA, *(s32 *) (m + *(s32 *) (m + 0x18) + 4));
}

/* Middle of the per-frame updates (skyfall_update, ramdozer_update), after the
 * steering: speed/heading (veh_turn_heading with `rate`), drive force
 * (veh_slope, veh_apply_slope kind/div), veh_set_target_heading when `drift` is
 * set, move along the heading (veh_move_along_heading), gBounceSound = 1, ground
 * contact (veh_ground_contact id/divB/divA; s3 in = veh_move_along_heading's s3), the
 * per-frame animation (anim_update_model on the buffers by gBufferIndex), parts
 * (`parts`), veh_collision_setup (a1, b2, b0, h2), veh_collide_spheres, veh_collide_world. */
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
    /* The asm hands veh_ground_contact whatever f12-f26 hold here (f12/f14 are
     * veh_move_along_heading's cosine/sine temporaries, f20-f26 the caller's): a
     * register leak that only feeds the scans' FP side results. */
    f.pz = f.cross = f.cz = f.side = f.sideZ = f.dz = 0.0f;
    veh_ground_contact(veh, id, &p->x, &p->y, &p->z, x, o.t1, divB, divA, (s16 *) (veh + 0x4C), veh + 0x96,
                  (s16 *) (veh + 0x52), (s32 *) (veh + 0x28), (s32 *) (veh + 0x40), (s32 *) (veh + 0x34),
                  (s32 *) (veh + 4), &r8, &f);
    if (gBufferIndex != 0) {
        anim_update_model((u8 *) p->bufA, (u8 *) p->bufB, (AnimChannel *) ch);
    } else {
        anim_update_model((u8 *) p->bufB, (u8 *) p->bufA, (AnimChannel *) ch);
    }
    /* s0/s1 here are anim_update_model's leftovers in the asm (register leak,
     * read only for a zero-count part). */
    mc.s0 = 0;
    mc.s1 = 0;
    parts(&mc);
    veh_collision_setup(p->z, (s32) a1, b2, 1, p->x, p->y, b0, VEH_S16(veh, VS_SPEED), h2, 0, id, veh);
    veh_collide_spheres(id, veh);
    veh_collide_world(id);
}

/* The turn after a collision report (gCollBlocked): the distance d between the
 * reversed target heading (+0x4E - 0x800, wrapped) and the ring midpoint
 * (veh_free_arc_mid), folded to 0..0x800; then veh_reverse_if_behind, the turn
 * veh_turn_into_free_arc(+0x4E, +0x4C, scale) whose new target goes to *target,
 * +0x4E and +0x74, veh_step_heading(turn, d) (d is only the asm's leftover v1
 * there; the C-side callee returns it untouched when heading == target) and
 * veh_hit_slowdown(0). */
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

/* Restore the saved vehicle state after a collision (veh_rollback; the
 * buffer copy goes from the current buffer into the other one). */
static void port_veh_restore(u8 *veh, PortVehPos *p, u8 *ch) {
    u64 *end;

    if (gBufferIndex != 0) {
        veh_rollback(veh, ch, &p->x, &p->y, &p->z, p->bufB, p->bufA, 0x800, &end);
    } else {
        veh_rollback(veh, ch, &p->x, &p->y, &p->z, p->bufA, p->bufB, 0x800, &end);
    }
}

/* Publish the vehicle's position, speed and headings, then its veh_set_record
 * record. */
static void port_veh_publish(u8 *veh, PortVehPos *p, s32 id) {
    gPlayerX = p->x;
    gPlayerY = p->y;
    gPlayerZ = p->z;
    gPlayerSpeed = VEH_S16(veh, VS_SPEED);
    gPlayerMoveHeading = VEH_U16(veh, VS_MOVE_HEADING);
    gPlayerHeading = VEH_U16(veh, VS_HEADING);
    veh_set_record(p->z, id, p->x, p->y, veh);
}

u8 *skyfall_build_model(MtxChainRegs *r);
u8 *ramdozer_build_model(MtxChainRegs *r);
void skyfall_set_physics(void);
void ramdozer_set_physics(void);

static const s16 sVeh3Wheels[12] = { 0x130, 0x16A, -0x130, 0x16A, 0x130, -0x16A,
                                     0x140, 0x172, -0x140, 0x172, 0x140, -0x172 };
static const s16 sVeh3Bands[15] = { -0xB4, 5, 2, 0, 0x78, 4, 0x78, 0xA0, 5, 0xA0, 0xDC, 2, 0xDC, 0xFA, 2 };
static const s16 sVeh4Wheels[12] = { 0xC8, 0x12C, -0xC8, 0x12C, 0xC8, -0x12C,
                                     0x12C, 0x1C2, -0x12C, 0x1C2, 0x12C, -0x1C2 };
static const s16 sVeh4Bands[15] = { -0xB4, 0, 6, 0, 0x50, 6, 0x50, 0x8C, 4, 0x8C, 0xBE, 2, 0xBE, 0xFA, 2 };
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
extern u8 gSkyfallSteerAnim;
extern f32 gSkyfallThrottleAnim;
extern u8 gSkyfallBumpTimer;
extern u8 gSkyfallSmokeCooldown;
extern s8 gSkyfallDeflecting;
extern void * N64P gSkyfallBoostSound;

/* Sets up Skyfall (type 3); sets gHaveSkyfall (0x802B29C0).
 * Vehicle 3 setup, called from the asm dispatcher lvl_spawn_vehicles: model
 * header, buffers and record (port_veh_init_head with this vehicle's wheel
 * table), gEngineSoundLevel = 0, level gSkyfallSteerAnim = 50, gSkyfallThrottleAnim = 0.5, ground
 * slots and animation reset (port_veh_init_anim, band table), veh_widen_bands,
 * meters/timers (gSkyfallBoostMeter = 100, gSkyfallBumpTimer/SmokeCooldown/Deflecting = 0, no boost sound
 * gSkyfallBoostSound), parts (scale 0x2AF8), veh_add_shadow(3, 0x50, 0x1F, 0x1F), one
 * update (skyfall_update with +0x9A set), buffer copy, gHaveSkyfall = 1.
 * Register convention: model s2, x t7, y s3, z s0, heading s1, and fp (the
 * ground slot flag seed for veh_init_ground) (conventions.txt). The asm saves
 * t0-t5 (its caller keeps t1, t2 live). Register leaks not modelled: the
 * caller's f12-f26 go into veh_init_ground (FP side results only), and the
 * dispatcher reads s2-s4, fp and f12-f26 afterwards, which hold whatever
 * skyfall_update / anim_update_model / mtx_build_model left (a mixed N64 build would
 * need a thunk). */
void skyfall_spawn(u8 *model, s32 x, s32 y, s32 z, s32 heading, s32 fp) {
    u8 *veh = gSkyfallState;
    PortVehPos *p = VPOS3;

    port_veh_init_head(veh, p, 3, model, sVeh3Wheels, x, y, z, heading);
    gEngineSoundLevel = 0;
    gSkyfallSteerAnim = 50;
    gSkyfallThrottleAnim = 0.5f;
    port_veh_init_anim(veh, p, 3, gSkyfallAnim, sVeh3Bands, x, z, fp);
    veh_widen_bands(veh);
    gSkyfallBoostMeter = 100;
    gSkyfallBumpTimer = 0;
    gSkyfallSmokeCooldown = 0;
    gSkyfallDeflecting = 0;
    gSkyfallBoostSound = NULL;
    port_veh_init_tail(veh, p, 3, 0x2AF8, 0x50, 0x1F, skyfall_update);
    gHaveSkyfall = 1;
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/6E200/skyfall_spawn.s")
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
extern u8 gSkyfallState[]; /* vehicle type 3's state block */
extern u8 gSkyfallAnim[]; /* its animation channel table */
extern u8 gSkyfallBoostTexAnim[]; /* key of its anim_tex_set_speed.. sound entry */

/* Getting into Skyfall: engine sound 0x8C (0x802B2D7C).
 * Vehicle-type 3 setup (called from hd.c / 17210.c): clears byte 0x99 of the
 * state block, resets animation channels 1-3 of gSkyfallAnim, resets the
 * gSkyfallBoostTexAnim entry (0, 0, 0, -1), gCamDistance / gCamHeightBase = 3400, 1000, then
 * veh_start_engine_sound(&gSkyfallAnim, 0x8C) (the asm's a0 is still the table).
 * The asm points $gp at gSkyfallState and leaves it there (conventions.txt:
 * clobbers gp) and returns with v1 = -1; the C callers use neither. */
void skyfall_enter(void) {
    s32 i;

    gSkyfallState[0x99] = 0;
    for (i = 1; i <= 3; i++) {
        anim_set_speed((AnimChannel *) gSkyfallAnim, i, 0);
        anim_set_reverse((AnimChannel *) gSkyfallAnim, i, 0);
        anim_set_loop_mode((AnimChannel *) gSkyfallAnim, i, i == 1 ? 0 : 1);
        anim_play((AnimChannel *) gSkyfallAnim, i, -1);
    }
    anim_tex_set_speed((s32) gSkyfallBoostTexAnim, 0);
    anim_tex_set_reverse((s32) gSkyfallBoostTexAnim, 0);
    anim_tex_set_loop_mode((s32) gSkyfallBoostTexAnim, 0);
    anim_tex_play((s32) gSkyfallBoostTexAnim, -1);
    gCamDistance = 0xD48;
    gCamHeightBase = 0x3E8;
    veh_start_engine_sound((s32) gSkyfallAnim, 0x8C);
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/6E200/skyfall_enter.s")
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
extern u8 gSkyfallState[]; /* this vehicle's state block */

/* 1 unless a wheel slot is airborne (0x802B2EF8).
 * Exit check for vehicle type 3 (called from veh_exit_check in hd.c, which
 * declares it void but returns the leftover v0). Returns 1 when none of the
 * bytes at +0x96, +0x97, +0x98 equals 1, else 0. Returns s32: the asm
 * leaves the full 0/1 in v0. Same shape as ramdozer_exit_check (below),
 * backlash_exit_check (71140), amdream_exit_check (72B80); thunderfist_exit_check (6C5E0) adds a
 * +0xA1 test. */
s32 skyfall_exit_check(void) {
    if (gSkyfallState[0x96] == 1 || gSkyfallState[0x97] == 1 || gSkyfallState[0x98] == 1) {
        return 0;
    }
    return 1;
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/6E200/skyfall_exit_check.s")
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
extern u64 * N64P gSkyfallMtxA;
extern u64 * N64P gSkyfallMtxB;

/* Getting out of Skyfall (0x802B2F54).
 * Vehicle-type 3 shutdown (called from hd.c's veh_leave): zeroes the
 * speed (s16 at +0x76), veh_sync_buffers(gSkyfallMtxA, gSkyfallMtxB, 0x800), then
 * veh_stop_engine_sounds(). */
void skyfall_leave(void) {
    *(s16 *) (gSkyfallState + 0x76) = 0;
    veh_sync_buffers(gSkyfallMtxA, gSkyfallMtxB, 0x800);
    veh_stop_engine_sounds();
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/6E200/skyfall_leave.s")
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
/* Placed Skyfall on a moving level part: re-ground it (0x802B2FA0).
 * Put vehicle 3 on the ground (called from C veh_update_others in 00000.c as
 * `void (void)`): port_veh_ground with key 3, then skyfall_build_model.
 * Register convention: fp in (conventions.txt). The C caller doesn't set fp:
 * the asm passes on whatever an outer function left in $fp/$s8 (register
 * leak; it seeds veh_reground's gSlotSurface flags), and its f12-f26 into the
 * scans. The asm saves every callee-saved register. */
void skyfall_reground(s32 fp) {
    port_veh_ground(gSkyfallState, VPOS3, 3, fp, skyfall_build_model);
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/6E200/skyfall_reground.s")
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
extern u32 gSkyfallPos[]; /* x, y, z */
/* Veh_zone_alpha at Skyfall's position (0x802B30B0).
 * Zone level lookup (veh_zone_alpha) for vehicle id 3 at its position
 * gSkyfallPos..+8; returns veh_zone_alpha's v1 (the zone list end).
 * Register convention: the asm passes veh_zone_alpha's scan registers t6, t7,
 * s0-s4 through (in and out; here through r), leaves a3 = id and clobbers
 * s5, s6 (conventions.txt). Asm caller skyfall_update keeps f12 and f14 live
 * (a mixed N64 build would need a thunk; the native port won't). */
s32 skyfall_zone_alpha(ZoneScanRegs *r) {
    return veh_zone_alpha(3, gSkyfallPos[0], gSkyfallPos[1], gSkyfallPos[2], r);
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/6E200/skyfall_zone_alpha.s")
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
/* Records Skyfall's position on the deck it stands on (0x802B30F4).
 * Track points of vehicle 3 on triangle `id` (port_veh_track); called from
 * the asm dispatcher veh_attach_riders_walk. Returns veh + 0x52 (the asm's v1).
 * Register convention: id a3, f24 in; v1, f24, f26 out (r); the asm saves t0,
 * t1, t3, t4 (its caller keeps t0, t1 live) and leaves gp = gSkyfallState, s4 =
 * veh + 0x4C (conventions.txt: clobbers). */
s32 skyfall_ride_attach(s32 id, InterpRegs *r) {
    return port_veh_track(gSkyfallState, VPOS3, id, r);
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/6E200/skyfall_ride_attach.s")
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
/* Moves Skyfall with the deck under it (0x802B3180).
 * Drive vehicle 3 along triangle `id` (port_veh_follow: setup leaf
 * skyfall_set_physics, ground contact divisors 0x2D4 / 0x260, parts skyfall_build_model);
 * called from the asm dispatcher veh_move_riders_walk. Returns veh_ground_contact's s3.
 * Register convention: id a3, f24 in; s3, f24, f26 out (r). The asm saves
 * a3, t0-t2 (its caller keeps them live) and changes the other s-registers,
 * fp and gp (conventions.txt: clobbers). */
s32 skyfall_ride_follow(s32 id, InterpRegs *r) {
    return port_veh_follow(gSkyfallState, VPOS3, 3, id, r, skyfall_set_physics, 0, 0x2D4, 0x260, skyfall_build_model);
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/6E200/skyfall_ride_follow.s")
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
s32 skyfall_zone_alpha(ZoneScanRegs *r);
void skyfall_update_anims(s32 t6, s32 t7, s32 s0, s32 s1, s32 s2, s32 s3, s32 s4);
s32 skyfall_steer_rate(void);
extern s16 gSkyfallDeflectHeading;
extern s16 gSkyfallGrip;

/* Skyfall driven by the player, per frame (0x802B327C).
 * Vehicle 3 per-frame update (called from C veh_update_player in 00000.c as
 * `void (void)`, and from skyfall_spawn):
 * 1. zone level (skyfall_zone_alpha) with the incoming scan registers, save state
 *    (veh_snapshot), effects skyfall_update_anims (with the scan registers as the
 *    zone lookup left them) unless +0x9A is set, veh_start_turbo when
 *    gTurboPending is set;
 * 2. setup leaf skyfall_set_physics, steering veh_steer(skyfall_steer_rate()),
 *    speed bands veh_throttle unless the countdown gSkyfallBumpTimer runs;
 * 3. port_veh_move (rate gSkyfallGrip, kind 3 / 724.0, drift gSkyfallDeflecting via
 *    gSkyfallDeflectHeading, divisors 0x2D4 / 0x260, parts skyfall_build_model, veh_collision_setup
 *    with gSkyfallNonBlockingParts, 1, 6, 100);
 * 4. no collision report (gCollBlocked == 0): gCollHit = 0, collision pass
 *    obj_collide_vehicle (gCollVehicleAnim = the channel table); if it sets gCollHit,
 *    restore (port_veh_restore), countdown 5, speed bounced to -max(|v|,
 *    80) / 2 (sign kept, sra) and parts again; else gSkyfallDeflecting = 0.
 *    Report: veh_prepare_escape, gSkyfallDeflecting = 1, port_veh_turn (gSkyfallDeflectTurnRate,
 *    gSkyfallDeflectHeading), collision pass. (A bounce test of the turn distance
 *    against 200 follows an unconditional branch in the asm: dead code.)
 * 5. port_veh_publish.
 * Register convention: t6, t7, s0-s4 in (only through veh_zone_alpha to
 * skyfall_update_anims; from the C caller they are whatever it left: register
 * leak). The asm saves all callee-saved registers and leaves v1 = y; asm
 * caller skyfall_spawn reads f12/f14 afterwards (mtx_build_model's leftovers,
 * not modelled). */
void skyfall_update(s32 t6, s32 t7, s32 s0, s32 s1, s32 s2, s32 s3, s32 s4) {
    u8 *veh = gSkyfallState;
    PortVehPos *p = VPOS3;
    ZoneScanRegs zs;
    MtxChainRegs mc;
    s32 v;

    zs.t6 = t6;
    zs.t7 = t7;
    zs.s0 = s0;
    zs.s1 = s1;
    zs.s2 = s2;
    zs.s3 = s3;
    zs.s4 = s4;
    skyfall_zone_alpha(&zs);
    veh_snapshot(veh, gSkyfallAnim, &p->x, &p->y, &p->z);
    if (VEH_S8(veh, VS_SPAWN_UPDATE) == 0) {
        skyfall_update_anims(zs.t6, zs.t7, zs.s0, zs.s1, zs.s2, zs.s3, zs.s4);
    }
    if (gTurboPending != 0) {
        veh_start_turbo(veh);
    }
    skyfall_set_physics();
    veh_steer(skyfall_steer_rate(), (u16 *) (veh + 0x4C));
    if (gSkyfallBumpTimer == 0) {
        veh_throttle(veh, (s16 *) (veh + 0x76), 3, veh + 0x96, (s16 *) (veh + 0x78), 0x14);
    } else {
        gSkyfallBumpTimer--;
    }
    port_veh_move(veh, p, 3, gSkyfallAnim, (u16) gSkyfallGrip, 3, 724.0f, gSkyfallDeflecting, &gSkyfallDeflectHeading, 0x2D4, 0x260,
                  skyfall_build_model, gSkyfallNonBlockingParts, 1, 6, 100);
    if (gCollBlocked == 0) {
        gCollHit = 0;
        gCollVehicleAnim = gSkyfallAnim;
        obj_collide_vehicle(3, veh);
        if (gCollHit != 0) {
            gSkyfallDeflecting = 0;
            port_veh_restore(veh, p, gSkyfallAnim);
            gSkyfallBumpTimer = 5;
            v = VEH_S16(veh, VS_SPEED);
            if (v >= 0) {
                if (v < 0x50) {
                    v = 0x50;
                }
            } else if (v >= -0x4F) {
                v = -0x50;
            }
            VEH_S16(veh, VS_SPEED) = -v >> 1;
            /* s0/s1: leftovers of obj_collide_vehicle / anim_update_model (leak). */
            mc.s0 = 0;
            mc.s1 = 0;
            skyfall_build_model(&mc);
        } else {
            gSkyfallDeflecting = 0;
        }
    } else {
        veh_prepare_escape(veh);
        gSkyfallDeflecting = 1;
        port_veh_turn(veh, gSkyfallDeflectTurnRate, &gSkyfallDeflectHeading);
        gCollVehicleAnim = gSkyfallAnim;
        obj_collide_vehicle(3, veh);
    }
    port_veh_publish(veh, p, 3);
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/6E200/skyfall_update.s")
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
extern u8 gSkyfallSmokeCooldown;  /* fx_spawn spawn cooldown */
extern u8 gSkyfallSteerAnim;  /* 0..100 level for channel 2 */
extern u8 gSkyfallBumpTimer;  /* boost countdown */
extern u8 gSkyfallBoosting;  /* boost sound playing */
extern s16 gSkyfallBoostTrail;
extern f32 gSkyfallThrottleAnim; /* 0..1 level for channel 3 */
extern void * N64P gSkyfallBoostSound; /* boost sound handle */
extern f32 gSkyfallThrottleDownStep;
extern f32 gSkyfallThrottleUpStep;
extern f32 gSkyfallThrottleReturnStep;
void skyfall_add_tyre_trail(void);

/* Smoke, suspension/steer/wheel animations, the L/R boost (0x802B37B0).
 * Vehicle type 3 per-frame update ($gp = gSkyfallState, read as the global):
 * 1. Cooldown gSkyfallSmokeCooldown counts down; at 0 with byte +0x99 set it restarts at
 *    1 and, while fewer than 15 fx_spawn records are active, sets up two
 *    (def gFxExhaustSmoke, data 0x29810, type 1 at (3, 2, 1) and (3, 3, 1)).
 * 2. Level gSkyfallThrottleAnim (channel 3 of gSkyfallAnim): unless veh_near_top_speed(10),
 *    falls by gSkyfallThrottleDownStep to 0 with gHeldBOrZ held, or rises by gSkyfallThrottleUpStep to
 *    1 with gHeldA held; otherwise it moves by gSkyfallThrottleReturnStep toward 0.5.
 * 3. gSkyfallSteerAnim steps by -5 (gHeldLeft) / +5 (gHeldRight) within 0..100, or
 *    by 10 toward 50; channel 2 gets it / 100.
 * 4. Channel 1: field 03D4 = (speed < 0), 039C = |speed| / 30 (also passed to
 *    veh_set_engine_level).
 * 5. Boost: gSkyfallBumpTimer counts down; at 0 with gHeldL or gHeldR held
 *    and the meter gSkyfallBoostMeter >= 4: meter -= 4, start sound 0x8D once
 *    (gSkyfallBoosting), sound entry gSkyfallBoostTexAnim on, one more fx_spawn record
 *    (def gFxSkyfallBoost, data 0x186A0, type 1 at (3, 1, 1)) and speed += 40 up
 *    to 400. Otherwise stop the sound, gSkyfallBoostTrail = 120, entry off, speed
 *    above 250 drops by 25 (not below 250) and the meter refills by 1 to 100.
 * 6. skyfall_add_tyre_trail (tyre trail).
 * Register convention: the asm passes t6, t7, s0-s4 through to
 * fx_spawn (t6 and s1 in its in/out block, carried from one call to the
 * next) and changes s5-s7 (conventions.txt: clobbers). Same shape as
 * ramdozer_update_anims (below). */
void skyfall_update_anims(s32 t6, s32 t7, s32 s0, s32 s1, s32 s2, s32 s3, s32 s4) {
    Io802A6274 io;
    f32 f;
    s32 near;
    s32 v;
    s32 speed;

    io.t6 = t6;
    io.s1 = s1;

    if (gSkyfallSmokeCooldown != 0) {
        gSkyfallSmokeCooldown--;
    } else if (gSkyfallState[0x99] != 0) {
        gSkyfallSmokeCooldown = 1;
        if (fx_count_active() < 15) {
            io.a3 = 1;
            fx_spawn(&io, gFxExhaustSmoke, 0x29810, 1, 3, 2, 1, t7, s0, s2, s3, s4, 1);
            io.a3 = 1;
            fx_spawn(&io, gFxExhaustSmoke, 0x29810, 1, 3, 3, 1, t7, s0, s2, s3, s4, 1);
        }
    }

    /* 2. channel 3 level */
    f = gSkyfallThrottleAnim;
    near = veh_near_top_speed(gSkyfallState, 10);
    if (near == 0 && gHeldBOrZ != 0) {
        f -= gSkyfallThrottleDownStep;
        if (f < 0.0f) {
            f = 0.0f;
        }
    } else if (near == 0 && gHeldA != 0) {
        f += gSkyfallThrottleUpStep;
        if (!(f <= 1.0f)) {
            f = 1.0f;
        }
    } else if (f < 0.5f) {
        f += gSkyfallThrottleReturnStep;
        if (!(f <= 0.5f)) {
            f = 0.5f;
        }
    } else {
        f -= gSkyfallThrottleReturnStep;
        if (f < 0.5f) {
            f = 0.5f;
        }
    }
    gSkyfallThrottleAnim = f;
    anim_set_frame(f, (AnimChannel *) gSkyfallAnim, 3, 0);

    /* 3. channel 2 level */
    v = gSkyfallSteerAnim;
    if (gHeldLeft != 0) {
        v -= 5;
        if (v < 0) {
            v = 0;
        }
    } else if (gHeldRight != 0) {
        v += 5;
        if (v > 100) {
            v = 100;
        }
    } else if (v >= 50) {
        v -= 10;
        if (v < 50) {
            v = 50;
        }
    } else {
        v += 10;
        if (v > 50) {
            v = 50;
        }
    }
    gSkyfallSteerAnim = v;
    anim_set_frame((f32) v / 100.0f, (AnimChannel *) gSkyfallAnim, 2, 0);

    /* 4. channel 1 from the speed */
    speed = *(s16 *) (gSkyfallState + 0x76);
    anim_set_reverse((AnimChannel *) gSkyfallAnim, 1, speed < 0);
    if (speed < 0) {
        speed = -speed;
    }
    speed = (u32) speed / 30;
    anim_set_speed((AnimChannel *) gSkyfallAnim, 1, speed);
    veh_set_engine_level(speed);

    /* 5. boost */
    if (gSkyfallBumpTimer != 0) {
        gSkyfallBumpTimer--;
    } else if ((gHeldL != 0 || gHeldR != 0) && ((s8) gSkyfallBoostMeter) >= 4) {
        gSkyfallBoostMeter -= 4;
        if (gSkyfallBoosting == 0) {
            gSkyfallBoosting = 1;
            snd_play(gSfxBank, SFX_SKYFALL_BOOST, &gSkyfallBoostSound);
        }
        anim_tex_set_frame(0.0f, (s32) gSkyfallBoostTexAnim, 1);
        io.a3 = 0;
        fx_spawn(&io, gFxSkyfallBoost, 0x186A0, 1, 3, 1, 1, t7, s0, s2, s3, s4, 1);
        speed = *(s16 *) (gSkyfallState + 0x76);
        *(s16 *) (gSkyfallState + 0x76) = (speed < 400) ? speed + 40 : 400;
        skyfall_add_tyre_trail();
        return;
    }
    if (gSkyfallBoostSound != NULL) {
        snd_stop(gSkyfallBoostSound);
        gSkyfallBoostSound = NULL;
    }
    gSkyfallBoostTrail = 120;
    gSkyfallBoosting = 0;
    anim_tex_set_frame(0.0f, (s32) gSkyfallBoostTexAnim, 0);
    speed = *(s16 *) (gSkyfallState + 0x76);
    if (speed > 250) {
        speed -= 25;
        if (speed < 250) {
            speed = 250;
        }
        *(s16 *) (gSkyfallState + 0x76) = speed;
    }
    v = ((s8) gSkyfallBoostMeter) + 1;
    if (v > 100) {
        v = 100;
    }
    gSkyfallBoostMeter = v;

    /* 6. */
    skyfall_add_tyre_trail();
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/6E200/skyfall_update_anims.s")
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
extern u8 gSkyfallState[];  /* this vehicle's state block (the asm's $gp here) */
extern u32 gSkyfallPos[]; /* [0], [2]: trail x, z */
extern u8 gSkyfallBoosting;
extern s16 gSkyfallBoostTrail;

/* Tyre tracks while skidding or boosting (0x802B3C68).
 * Tyre trail. When the byte at +0x99 is set the trail's a/b value is 50;
 * else, only while gSkyfallBoosting is set, gSkyfallBoostTrail counts down by 8 to 0
 * (clamped) and its new value is used. Then, unless +0x98 is 1, +0x50 >= 3 or
 * +0x9B is set, adds a trail segment (trail_add, period 2, wheel offsets
 * (+-400, -300), position gSkyfallPos[0]/[2], yaw u16 +0x4E, half-width 4).
 * The asm passes the u16/u8 arguments as full words; the C prototype narrows
 * them (only their low bits are read).
 * Register note: the asm saves and restores every integer register; asm
 * caller skyfall_update_anims keeps a1-a3 live (a mixed N64 build would need a
 * thunk). Same shape: ramdozer_add_tyre_trail (below), backlash_add_tyre_trail (71140),
 * amdream_add_tyre_trail (72B80), ballista_add_tyre_trail (853D0), police_add_tyre_trail (86F60),
 * ateam_add_tyre_trail (88160), starski_add_tyre_trail (8AEE0). */
void skyfall_add_tyre_trail(void) {
    s32 col;

    if (gSkyfallState[0x99] != 0) {
        col = 50;
    } else {
        if (gSkyfallBoosting == 0) {
            return;
        }
        col = gSkyfallBoostTrail;
        if (col < 8) {
            col = 8;
        }
        col -= 8;
        gSkyfallBoostTrail = col;
    }
    if (gSkyfallState[0x98] != 1 && gSkyfallState[0x50] < 3 && gSkyfallState[0x9B] == 0) {
        trail_add(2, *(s32 *) (gSkyfallState + 0x1C), 400, -300, -400, -300, gSkyfallPos[0], gSkyfallPos[2],
                      *(u16 *) (gSkyfallState + 0x4E), 4, col, col, 0);
    }
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/6E200/skyfall_add_tyre_trail.s")
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
/* Skyfall's model matrix, parts and points (0x802B3E40).
 * Place vehicle 3's model parts (port_veh_parts, id 3, scale 0x2AF8).
 * Returns the current matrix buffer (the asm's s4).
 * Register convention: s0, s1 in through r (only read for a zero-count part),
 * s1 out (mtx_chain_transform's last y'); s4 = return value; clobbers s1, s2,
 * s4-s7 (conventions.txt). Asm caller skyfall_update keeps t6, t7 live; it
 * and skyfall_spawn also read f12/f14 afterwards (mtx_build_model's
 * cosine/sine temporaries, not modelled). */
u8 *skyfall_build_model(MtxChainRegs *r) {
    return port_veh_parts(gSkyfallState, VPOS3, 3, 0x2AF8, r);
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/6E200/skyfall_build_model.s")
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

/* Speed / 11 airborne, / 2.5 on the ground (0x802B3F78).
 * Speed (s16 at +0x76 of gSkyfallState, the asm's $gp) / 11.0 when any of the
 * bytes at +0x96/+0x97/+0x98 is 1, else / 2.5, rounded to nearest. The asm
 * returns it in s3 (see tools_port/conventions.txt). Its asm caller
 * skyfall_update keeps a0-a3 live (a mixed N64 build would need a thunk).
 * Same shape as sideswipe_steer_rate (6B4A0). */
s32 skyfall_steer_rate(void) {
    f32 div;
    s32 r;

    if (gSkyfallState[0x96] == 1 || gSkyfallState[0x97] == 1 || gSkyfallState[0x98] == 1) {
        div = 11.0f;
    } else {
        div = 2.5f;
    }
    CVT_W_S(r, *(s16 *) (gSkyfallState + 0x76) / div);
    return r;
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/6E200/skyfall_steer_rate.s")
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
extern s16 gSkyfallGrip;

/* Gravity, bounce 60/3, grip by the brake (0x802B3FF0).
 * Vehicle-module setup leaf (shape of ramdozer_set_physics, below): gGravity =
 * gLevelGravity * 4, gBounceThreshold/Damping = 60, 3, then gSkyfallGrip = 2000 if the button
 * byte gHeldA (speed <= 0) or gHeldBOrZ (speed > 0) is set, else 15000.
 * Speed is the s16 at +0x76 of gSkyfallState (the asm's $gp).
 * Register note: the asm touches only at/v0/v1/f0/f2. Its asm callers keep
 * registers live across the call (skyfall_ride_follow: t3, t4, f12, f14;
 * skyfall_update: a0-a3); a mixed N64 build would need a thunk. */
void skyfall_set_physics(void) {
    gGravity = gLevelGravity * 4.0f;
    gBounceThreshold = 60;
    gBounceDamping = 3;
    if (*(s16 *) (gSkyfallState + 0x76) <= 0) {
        gSkyfallGrip = (gHeldA == 0) ? 15000 : 2000;
    } else {
        gSkyfallGrip = (gHeldBOrZ == 0) ? 15000 : 2000;
    }
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/6E200/skyfall_set_physics.s")
#endif

/* skyfall_save_state/skyfall_load_state: two-address trampolines into
 * veh_serialize_state/veh_deserialize_state, same confirmed-unreachable-from-C 8-byte
 * sd-$ra frame as veh_check_wrap (hd_code/679E0.c). Permanently
 * GLOBAL_ASM. */
#ifdef NON_MATCHING
extern u8 gSkyfallState[];  /* this vehicle's state block: 0xA6 bytes are (de)serialized */
extern u32 gSkyfallPos[]; /* plus these three words */

/* Skyfall's state into a buffer (no callers) (0x802B40A8).
 * Serialize this vehicle's state (gSkyfallState[0..0xA5] plus the three words
 * gSkyfallPos[0..2]) into `dst` via veh_serialize_state. Returns the callee's result
 * (0xB2, the byte count): the asm leaves the callee's v0 untouched. */
s32 skyfall_save_state(u8 *dst) {
    return veh_serialize_state(dst, gSkyfallState, gSkyfallPos);
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/6E200/skyfall_save_state.s")
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
/* Skyfall's state from a buffer (recording start) (0x802B40D4).
 * Inverse of skyfall_save_state: restore this vehicle's state from `src` via
 * veh_deserialize_state. Called from 17210.c as `void skyfall_load_state(void *)`. */
void skyfall_load_state(void *src) {
    veh_deserialize_state(src, gSkyfallState, gSkyfallPos);
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/6E200/skyfall_load_state.s")
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
extern u8 gRamdozerSmokeCooldown;
extern u8 gRamdozerDeflecting;
extern u8 gRamdozerBumpTimer;
extern f32 gRamdozerThrottleAnim;

/* Sets up Ramdozer (type 4) (0x802B4100).
 * Vehicle 4 setup, called from the asm dispatcher lvl_spawn_vehicles; as
 * skyfall_spawn with this vehicle's tables: veh_widen_bands, then
 * gRamdozerDeflecting/BumpTimer/SmokeCooldown = 0, gEngineSoundLevel = 0, gRamdozerThrottleAnim = 0.5; parts scale
 * 0x2710, veh_add_shadow(4, 0x96, 0x32, 0x32), update ramdozer_update; no
 * final flag. Same register convention and leaks as skyfall_spawn. */
void ramdozer_spawn(u8 *model, s32 x, s32 y, s32 z, s32 heading, s32 fp) {
    u8 *veh = gRamdozerState;
    PortVehPos *p = VPOS4;

    port_veh_init_head(veh, p, 4, model, sVeh4Wheels, x, y, z, heading);
    port_veh_init_anim(veh, p, 4, gRamdozerAnim, sVeh4Bands, x, z, fp);
    veh_widen_bands(veh);
    gRamdozerDeflecting = 0;
    gRamdozerBumpTimer = 0;
    gRamdozerSmokeCooldown = 0;
    gEngineSoundLevel = 0;
    gRamdozerThrottleAnim = 0.5f;
    port_veh_init_tail(veh, p, 4, 0x2710, 0x96, 0x32, ramdozer_update);
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/6E200/ramdozer_spawn.s")
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
extern u8 gRamdozerState[]; /* vehicle type 4's state block */
extern u8 gRamdozerAnim[]; /* its animation channel table */

/* Getting into Ramdozer: engine sound 0x0B, track animations reset (0x802B448C).
 * Vehicle-type 4 setup (called from hd.c / 17210.c), shape of skyfall_enter:
 * clears byte 0x99 of the state block, resets channel 1 (03D4 only) and
 * channel 2 of gRamdozerAnim, resets the texture animations gRamdozerTrackTexAnimA/B/HeadingTexAnim
 * (0, 0, 0, -1 each), gCamDistance / gCamHeightBase = 3000, 1000, then
 * veh_start_engine_sound(&gRamdozerAnim, 0x0B). Leaves $gp = gRamdozerState (clobbers gp)
 * and v1 = -1, unused by the C callers. */
void ramdozer_enter(void) {
    gRamdozerState[0x99] = 0;
    anim_set_reverse((AnimChannel *) gRamdozerAnim, 1, 0);
    anim_tex_set_speed((s32) gRamdozerTrackTexAnimA, 0);
    anim_tex_set_reverse((s32) gRamdozerTrackTexAnimA, 0);
    anim_tex_set_loop_mode((s32) gRamdozerTrackTexAnimA, 0);
    anim_tex_play((s32) gRamdozerTrackTexAnimA, -1);
    anim_tex_set_speed((s32) gRamdozerTrackTexAnimB, 0);
    anim_tex_set_reverse((s32) gRamdozerTrackTexAnimB, 0);
    anim_tex_set_loop_mode((s32) gRamdozerTrackTexAnimB, 0);
    anim_tex_play((s32) gRamdozerTrackTexAnimB, -1);
    anim_tex_set_speed((s32) gRamdozerHeadingTexAnim, 0);
    anim_tex_set_reverse((s32) gRamdozerHeadingTexAnim, 0);
    anim_tex_set_loop_mode((s32) gRamdozerHeadingTexAnim, 0);
    anim_tex_play((s32) gRamdozerHeadingTexAnim, -1);
    anim_set_speed((AnimChannel *) gRamdozerAnim, 2, 0);
    anim_set_reverse((AnimChannel *) gRamdozerAnim, 2, 0);
    anim_set_loop_mode((AnimChannel *) gRamdozerAnim, 2, 1);
    anim_play((AnimChannel *) gRamdozerAnim, 2, -1);
    gCamDistance = 0xBB8;
    gCamHeightBase = 0x3E8;
    veh_start_engine_sound((s32) gRamdozerAnim, 0x0B);
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/6E200/ramdozer_enter.s")
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
extern u8 gRamdozerState[]; /* this vehicle's state block */

/* 1 unless a wheel slot is airborne (0x802B45FC).
 * Exit check for vehicle type 4, same shape as skyfall_exit_check (above): returns
 * 1 when none of the bytes at +0x96, +0x97, +0x98 equals 1, else 0 (s32; the
 * C caller in hd.c declares it void and returns the leftover v0). */
s32 ramdozer_exit_check(void) {
    if (gRamdozerState[0x96] == 1 || gRamdozerState[0x97] == 1 || gRamdozerState[0x98] == 1) {
        return 0;
    }
    return 1;
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/6E200/ramdozer_exit_check.s")
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
extern u64 * N64P gRamdozerMtxA;
extern u64 * N64P gRamdozerMtxB;

/* Getting out of Ramdozer (0x802B4658).
 * Vehicle-type 4 shutdown (called from hd.c's veh_leave): zeroes the
 * speed (s16 at +0x76), veh_sync_buffers(gRamdozerMtxA, gRamdozerMtxB, 0x800),
 * veh_stop_engine_sounds(), then anim_tex_set_speed(key, 0) for gRamdozerTrackTexAnimA and gRamdozerTrackTexAnimB.
 * The asm returns with v1 = 0; the C caller ignores it. */
void ramdozer_leave(void) {
    *(s16 *) (gRamdozerState + 0x76) = 0;
    veh_sync_buffers(gRamdozerMtxA, gRamdozerMtxB, 0x800);
    veh_stop_engine_sounds();
    anim_tex_set_speed((s32) gRamdozerTrackTexAnimA, 0);
    anim_tex_set_speed((s32) gRamdozerTrackTexAnimB, 0);
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/6E200/ramdozer_leave.s")
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
/* Placed Ramdozer on a moving level part: re-ground it (0x802B46C4).
 * Put vehicle 4 on the ground; as skyfall_reground (key 4, parts
 * ramdozer_build_model), same fp convention and leaks. */
void ramdozer_reground(s32 fp) {
    port_veh_ground(gRamdozerState, VPOS4, 4, fp, ramdozer_build_model);
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/6E200/ramdozer_reground.s")
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
extern u32 gRamdozerPos[]; /* x, y, z */
/* Veh_zone_alpha at Ramdozer's position (0x802B47D4).
 * Zone level lookup (veh_zone_alpha) for vehicle id 4 at its position
 * gRamdozerPos..+8; returns veh_zone_alpha's v1 (the zone list end).
 * Register convention: the asm passes veh_zone_alpha's scan registers t6, t7,
 * s0-s4 through (in and out; here through r), leaves a3 = id and clobbers
 * s5, s6 (conventions.txt). Asm caller ramdozer_update keeps f12 and f14 live
 * (a mixed N64 build would need a thunk; the native port won't). */
s32 ramdozer_zone_alpha(ZoneScanRegs *r) {
    return veh_zone_alpha(4, gRamdozerPos[0], gRamdozerPos[1], gRamdozerPos[2], r);
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/6E200/ramdozer_zone_alpha.s")
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
/* Records Ramdozer's position on the deck it stands on (0x802B4818).
 * As skyfall_ride_attach for vehicle 4. */
s32 ramdozer_ride_attach(s32 id, InterpRegs *r) {
    return port_veh_track(gRamdozerState, VPOS4, id, r);
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/6E200/ramdozer_ride_attach.s")
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
/* Moves Ramdozer with the deck under it (0x802B48A4).
 * As skyfall_ride_follow for vehicle 4: setup leaf ramdozer_set_physics, then
 * gBounceSound = 1, divisors 0x258 / 0x190, parts ramdozer_build_model. */
s32 ramdozer_ride_follow(s32 id, InterpRegs *r) {
    return port_veh_follow(gRamdozerState, VPOS4, 4, id, r, ramdozer_set_physics, 1, 0x258, 0x190, ramdozer_build_model);
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/6E200/ramdozer_ride_follow.s")
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
s32 ramdozer_zone_alpha(ZoneScanRegs *r);
void ramdozer_update_anims(s32 t6, s32 t7, s32 s0, s32 s1, s32 s2, s32 s3, s32 s4);
s32 ramdozer_steer_rate(void);
extern s16 gRamdozerGrip;
extern s16 gRamdozerDeflectHeading;

/* Ramdozer driven by the player, per frame (0x802B49AC).
 * Vehicle 4 per-frame update (called from C veh_update_player as `void (void)`,
 * and from ramdozer_spawn); as skyfall_update with:
 * - after the save, the second looping sound veh_update_horn(0x8F);
 *   effects ramdozer_update_anims;
 * - gEngineSoundLevel = speed before the setup leaf ramdozer_set_physics; steering rate
 *   ramdozer_steer_rate(); countdown gRamdozerBumpTimer;
 * - port_veh_move: rate gRamdozerGrip, kind 4 / 600.0, drift gRamdozerDeflecting via
 *   gRamdozerDeflectHeading, divisors 0x258 / 0x190, parts ramdozer_build_model, veh_collision_setup
 *   with gRamdozerNonBlockingParts, 0, 8, 120;
 * - bounce speed: |v| clamped to 62..125 (positive) / 45..90 (negative:
 *   v >= -44 gives -45, v < -90 gives -90), negated (no halving);
 * - veh_prev_contacts_clear before the collision pass on the report path.
 * Same register convention and leaks as skyfall_update. */
void ramdozer_update(s32 t6, s32 t7, s32 s0, s32 s1, s32 s2, s32 s3, s32 s4) {
    u8 *veh = gRamdozerState;
    PortVehPos *p = VPOS4;
    ZoneScanRegs zs;
    MtxChainRegs mc;
    s32 v;

    zs.t6 = t6;
    zs.t7 = t7;
    zs.s0 = s0;
    zs.s1 = s1;
    zs.s2 = s2;
    zs.s3 = s3;
    zs.s4 = s4;
    ramdozer_zone_alpha(&zs);
    veh_snapshot(veh, gRamdozerAnim, &p->x, &p->y, &p->z);
    veh_update_horn(0x8F);
    if (VEH_S8(veh, VS_SPAWN_UPDATE) == 0) {
        ramdozer_update_anims(zs.t6, zs.t7, zs.s0, zs.s1, zs.s2, zs.s3, zs.s4);
    }
    if (gTurboPending != 0) {
        veh_start_turbo(veh);
    }
    gEngineSoundLevel = VEH_S16(veh, VS_SPEED);
    ramdozer_set_physics();
    veh_steer(ramdozer_steer_rate(), (u16 *) (veh + 0x4C));
    if (gRamdozerBumpTimer == 0) {
        veh_throttle(veh, (s16 *) (veh + 0x76), 3, veh + 0x96, (s16 *) (veh + 0x78), 0x14);
    } else {
        gRamdozerBumpTimer--;
    }
    port_veh_move(veh, p, 4, gRamdozerAnim, (u16) gRamdozerGrip, 4, 600.0f, (s8) gRamdozerDeflecting, &gRamdozerDeflectHeading, 0x258,
                  0x190, ramdozer_build_model, gRamdozerNonBlockingParts, 0, 8, 120);
    if (gCollBlocked == 0) {
        gCollHit = 0;
        gCollVehicleAnim = gRamdozerAnim;
        obj_collide_vehicle(4, veh);
        if (gCollHit != 0) {
            gRamdozerDeflecting = 0;
            port_veh_restore(veh, p, gRamdozerAnim);
            gRamdozerBumpTimer = 5;
            v = VEH_S16(veh, VS_SPEED);
            if (v >= 0) {
                if (v < 0x3E) {
                    v = 0x3E;
                } else if (v >= 0x7E) {
                    v = 0x7D;
                }
            } else if (v >= -0x2C) {
                v = -0x2D;
            } else if (v < -0x5A) {
                v = -0x5A;
            }
            VEH_S16(veh, VS_SPEED) = -v;
            /* s0/s1: leftovers of obj_collide_vehicle / anim_update_model (leak). */
            mc.s0 = 0;
            mc.s1 = 0;
            ramdozer_build_model(&mc);
        } else {
            gRamdozerDeflecting = 0;
        }
    } else {
        veh_prepare_escape(veh);
        gRamdozerDeflecting = 1;
        port_veh_turn(veh, gRamdozerDeflectTurnRate, &gRamdozerDeflectHeading);
        veh_prev_contacts_clear();
        gCollVehicleAnim = gRamdozerAnim;
        obj_collide_vehicle(4, veh);
    }
    port_veh_publish(veh, p, 4);
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/6E200/ramdozer_update.s")
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
extern u8 gRamdozerSmokeCooldown;  /* fx_spawn spawn cooldown */
extern u8 gRamdozerDeflecting;  /* channel 1 restart request */
extern f32 gRamdozerThrottleAnim; /* 0..1 level for channel 2 */
extern f32 gRamdozerThrottleDownStep;
extern f32 gRamdozerThrottleUpStep;
extern f32 gRamdozerThrottleReturnStep;
void ramdozer_add_tyre_trail(void);

/* Smoke, start-off puff, throttle, tracks, bump animation (0x802B4EF8).
 * Vehicle type 4 per-frame update ($gp = gRamdozerState, read as the global),
 * shape of skyfall_update_anims (above):
 * 1. ramdozer_add_tyre_trail (tyre trail).
 * 2. Cooldown gRamdozerSmokeCooldown counts down; at 0 with byte +0x99 set it restarts
 *    at 1 and, while fewer than 14 fx_spawn records are active, sets up
 *    one (def gFxExhaustSmoke, data 0x30D40, type 1 at (4, 1, 1)).
 * 3. With gEngineSoundLevel == 0 and the speed (s16 +0x76) nonzero: sound 10
 *    (snd_play, the asm saving every register around it) and another
 *    record (data 0x1D4C0 at (4, 2, 1)).
 * 4. Level gRamdozerThrottleAnim (channel 2 of gRamdozerAnim): unless veh_near_top_speed(30),
 *    falls by gRamdozerThrottleDownStep to 0 with gHeldBOrZ held, or rises by gRamdozerThrottleUpStep
 *    to 1 with gHeldA held; otherwise it moves by gRamdozerThrottleReturnStep toward 0.5.
 * 5. Texture animations gRamdozerTrackTexAnimA/gRamdozerTrackTexAnimB: moving, reverse = (speed < 0) on
 *    both, unk14 = |speed| / 2 on both and veh_set_engine_level(|speed| / 8);
 *    standing, unk11 = (stick < 0) / (stick >= 0) and unk14 = |stick|
 *    (gPadStickX).
 * 6. Entry gRamdozerHeadingTexAnim: unk13 = (u16 +0x4C in [0x355, 0x8AB) ? it - 0x355 :
 *    0) / 76, unk4 = 0.
 * 7. With gRamdozerDeflecting set and channel 1's field 0x10 != 1: channel 1 gets
 *    (9, 1, 2) and sound 1 plays.
 * Register convention: as skyfall_update_anims, t6, t7, s0-s4 pass through to
 * fx_spawn (conventions.txt); s5-s7 change. */
void ramdozer_update_anims(s32 t6, s32 t7, s32 s0, s32 s1, s32 s2, s32 s3, s32 s4) {
    Io802A6274 io;
    s32 ch[8];
    f32 f;
    s32 near;
    s32 speed;
    s32 v;

    io.t6 = t6;
    io.s1 = s1;

    ramdozer_add_tyre_trail();

    if (gRamdozerSmokeCooldown != 0) {
        gRamdozerSmokeCooldown--;
    } else if (gRamdozerState[0x99] != 0) {
        gRamdozerSmokeCooldown = 1;
        if (fx_count_active() < 14) {
            io.a3 = 1;
            fx_spawn(&io, gFxExhaustSmoke, 0x30D40, 1, 4, 1, 1, t7, s0, s2, s3, s4, 1);
        }
    }

    if (gEngineSoundLevel == 0 && *(s16 *) (gRamdozerState + 0x76) != 0) {
        snd_play(gSfxBank, SFX_SKID, NULL);
        io.a3 = 1;
        fx_spawn(&io, gFxExhaustSmoke, 0x1D4C0, 1, 4, 2, 1, t7, s0, s2, s3, s4, 1);
    }

    f = gRamdozerThrottleAnim;
    near = veh_near_top_speed(gRamdozerState, 30);
    if (near == 0 && gHeldBOrZ != 0) {
        f -= gRamdozerThrottleDownStep;
        if (f < 0.0f) {
            f = 0.0f;
        }
    } else if (near == 0 && gHeldA != 0) {
        f += gRamdozerThrottleUpStep;
        if (!(f <= 1.0f)) {
            f = 1.0f;
        }
    } else if (f < 0.5f) {
        f += gRamdozerThrottleReturnStep;
        if (!(f <= 0.5f)) {
            f = 0.5f;
        }
    } else {
        f -= gRamdozerThrottleReturnStep;
        if (f < 0.5f) {
            f = 0.5f;
        }
    }
    gRamdozerThrottleAnim = f;
    anim_set_frame(f, (AnimChannel *) gRamdozerAnim, 2, 0);

    speed = *(s16 *) (gRamdozerState + 0x76);
    if (speed != 0) {
        anim_tex_set_reverse((s32) gRamdozerTrackTexAnimA, speed < 0);
        anim_tex_set_reverse((s32) gRamdozerTrackTexAnimB, speed < 0);
        v = (u32) (speed < 0 ? -speed : speed) >> 1;
        anim_tex_set_speed((s32) gRamdozerTrackTexAnimA, v);
        anim_tex_set_speed((s32) gRamdozerTrackTexAnimB, v);
        veh_set_engine_level((u32) v >> 2);
    } else {
        v = gPadStickX;
        anim_tex_set_reverse((s32) gRamdozerTrackTexAnimA, v < 0);
        anim_tex_set_reverse((s32) gRamdozerTrackTexAnimB, v >= 0);
        if (v < 0) {
            v = -v;
        }
        anim_tex_set_speed((s32) gRamdozerTrackTexAnimA, v);
        anim_tex_set_speed((s32) gRamdozerTrackTexAnimB, v);
    }

    v = *(u16 *) (gRamdozerState + 0x4C);
    if (v >= 0x355 && v < 0x8AB) {
        v -= 0x355;
    } else {
        v = 0;
    }
    anim_tex_set_frame(0.0f, (s32) gRamdozerHeadingTexAnim, (u32) v / 76);

    if (gRamdozerDeflecting != 0) {
        anim_get_state(1, (AnimChannel *) gRamdozerAnim, ch);
        if (ch[0] != 1) {
            anim_set_speed((AnimChannel *) gRamdozerAnim, 1, 9);
            anim_set_loop_mode((AnimChannel *) gRamdozerAnim, 1, 1);
            anim_play((AnimChannel *) gRamdozerAnim, 1, 2);
            snd_play(gSfxBank, SFX_CLUNK, NULL);
        }
    }
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/6E200/ramdozer_update_anims.s")
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
extern u8 gRamdozerState[];  /* this vehicle's state block (the asm's $gp here) */
extern u32 gRamdozerPos[]; /* [0], [2]: trail x, z */

/* Track marks while skidding (0x802B54EC).
 * Tyre trail (shape of skyfall_add_tyre_trail, above): if the byte at +0x99 is set,
 * +0x98 isn't 1, +0x50 < 3 and +0x9B is clear, trail_add(3, y, 400,
 * -300, -400, -300, gRamdozerPos[0], gRamdozerPos[2], yaw, 5, 50, 50, 0).
 * Register note: the asm saves and restores every integer register; asm
 * caller ramdozer_update_anims keeps a0-a3, t6, t7 live (and reads f12/f14 after
 * the call, which the asm doesn't touch but trail_add may); a mixed N64
 * build would need a thunk. */
void ramdozer_add_tyre_trail(void) {
    if (gRamdozerState[0x99] != 0 && gRamdozerState[0x98] != 1 && gRamdozerState[0x50] < 3 && gRamdozerState[0x9B] == 0) {
        trail_add(3, *(s32 *) (gRamdozerState + 0x1C), 400, -300, -400, -300, gRamdozerPos[0], gRamdozerPos[2],
                      *(u16 *) (gRamdozerState + 0x4E), 5, 50, 50, 0);
    }
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/6E200/ramdozer_add_tyre_trail.s")
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
/* Ramdozer's model matrix, parts and points (0x802B568C).
 * As skyfall_build_model for vehicle 4 (id 4, scale 0x2710). */
u8 *ramdozer_build_model(MtxChainRegs *r) {
    return port_veh_parts(gRamdozerState, VPOS4, 4, 0x2710, r);
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/6E200/ramdozer_build_model.s")
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
/* 22 airborne, else 75 (0x802B57C4).
 * 22 when any of the bytes at +0x96/+0x97/+0x98 of gRamdozerState (the asm's
 * $gp) is 1, else 75. The asm returns it in s3 (see
 * tools_port/conventions.txt). Its asm caller ramdozer_update keeps a0-a3 live
 * (a mixed N64 build would need a thunk). */
s32 ramdozer_steer_rate(void) {
    if (gRamdozerState[0x96] == 1 || gRamdozerState[0x97] == 1 || gRamdozerState[0x98] == 1) {
        return 22;
    }
    return 75;
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/6E200/ramdozer_steer_rate.s")
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
extern s16 gRamdozerGrip;

/* Gravity x2, bounce 110/4, grip by B/Z (0x802B5814).
 * Vehicle-module setup leaf (shape of driver_set_physics in 69BB0, other
 * constants): gGravity = gLevelGravity * 2, gBounceThreshold/Damping = 110, 4, then
 * gRamdozerGrip = 2000 if B/Z is held, else 9000.
 * Register note: the asm touches only at/v0/v1/f0/f2. Its asm callers keep
 * registers live across the call (ramdozer_ride_follow: t3, t4, f12, f14;
 * ramdozer_update: a0-a3); a mixed N64 build would need a thunk preserving
 * those, the native port does not. */
void ramdozer_set_physics(void) {
    gGravity = gLevelGravity * 2.0f;
    gBounceThreshold = 110;
    gBounceDamping = 4;
    gRamdozerGrip = (gHeldBOrZ == 0) ? 9000 : 2000;
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/6E200/ramdozer_set_physics.s")
#endif

/* ramdozer_save_state: two-address trampoline into veh_serialize_state, same
 * confirmed-unreachable-from-C 8-byte sd-$ra frame as veh_check_wrap
 * (hd_code/679E0.c). Permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
extern u8 gRamdozerState[];  /* this vehicle's state block: 0xA6 bytes are (de)serialized */
extern u32 gRamdozerPos[]; /* plus these three words */

/* Ramdozer's state into a buffer (no callers) (0x802B589C).
 * Serialize this vehicle's state (gRamdozerState[0..0xA5] plus the three words
 * gRamdozerPos[0..2]) into `dst` via veh_serialize_state. Returns the callee's result
 * (0xB2, the byte count): the asm leaves the callee's v0 untouched. */
s32 ramdozer_save_state(u8 *dst) {
    return veh_serialize_state(dst, gRamdozerState, gRamdozerPos);
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/6E200/ramdozer_save_state.s")
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
/* Ramdozer's state from a buffer (recording start) (0x802B58C8).
 * Inverse of ramdozer_save_state: restore this vehicle's state from `src` via
 * veh_deserialize_state. Called from 17210.c as `void ramdozer_load_state(void *)`. */
void ramdozer_load_state(void *src) {
    veh_deserialize_state(src, gRamdozerState, gRamdozerPos);
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/6E200/ramdozer_load_state.s")
#endif
