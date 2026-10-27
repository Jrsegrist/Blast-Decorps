#include "common.h"

/*
 * Hand-written asm (sd $ra frames that save s0-s7, gp and fp): o32 entry
 * points through which the front end's C calls hd_code functions that take
 * their arguments in other registers (model loading, display-list setup and
 * the texture-animation (anim_tex_*) setters of 56040.c).
 *
 * The NON_MATCHING versions call those functions' C rewrites directly.  The
 * asm compares and passes whole registers, so the parameters are s32 here
 * where 00000.c / 0DE70.c declare u8 (the callers pass zero-extended values).
 */

#ifdef NON_MATCHING
#include <ultra64.h>
#include "game/game.h"


/* texture-animation tables in hd_code's 7D9D0 text blob */


/* Lvl_load_model, two heap buffers, display lists, veh_add_record (0x80202100).
 *
 * Load model `type` (lvl_load_model), reserve two 6000-byte heap buffers
 * (heap[0], heap[1]) and set up the two display-list ranges in dl[0..3]
 * (start in the model data at its header offsets 0x1C / 0x2C, write position
 * in the second buffer), then veh_add_record(type, 0, heap[0], heap[1], model).
 * The loads and stores keep the asm's order (the last call's arguments are
 * read back from memory).
 */
void fe_load_model(s32 type, u8 * N64P *model, u8 * N64P *heap, u8 * N64P *dl) {
    Out802A396C o;
    u8 *data;
    u8 *h;
    s32 off1C;
    s32 off2C;

    lvl_load_model(type, &o);
    data = o.s2;
    *model = data;
    h = gHeapPtr;
    heap[0] = h;
    h += 0x1770;
    heap[1] = h;
    h += 0x1770;
    gHeapPtr = h;
    off1C = *(s32 *) (data + 0x1C);
    dl[0] = data + off1C;
    dl[1] = h;
    off2C = *(s32 *) (data + 0x2C);
    dl[2] = data + off2C;
    dl[3] = h + off2C - off1C;
    veh_add_record(type, 0, (s32) heap[0], (s32) heap[1], *model);
}

/* anim_update_model(base, other, ch) for the front end's o32 C: steps a model's 32 channels into one matrix
 * buffer (0x802021FC).
 * anim_update_model with the arguments rotated (asm: v0 = a1, v1 = a2, t0 = a0).
 * The asm saves only integer registers, so it leaves f20/f30 as the asm
 * anim_update_model leaves them (conventions.txt: clobbers f20,f30). */
void fe_anim_update_model(void *ch, void *base, void *other) {
    anim_update_model(base, other, ch);
}

/* anim_init_model(bufs[1], bufs[0], ch, hdr) through an o32 entry: sets up a model's channels and both matrix
 * buffers (the result is dropped) (0x80202270).
 * anim_init_model(bufs[1], bufs[0], ch, hdr); its result is dropped */
void fe_anim_init_model(u8 *hdr, u32 * N64P *bufs, void *ch) {
    anim_init_model(bufs[1], bufs[0], ch, hdr);
}

/* Sets channel idx of an animation table (speed, reverse, loop mode, interpolation and tension), then plays
 * it with no loop limit (0x802022EC).
 * the five setters of channel `idx` of table `base` */
void fe_anim_start_channel(void *base, s32 idx, s32 v040C, s32 v0480, f32 f, s32 v039C, s32 v03D4) {
    anim_set_speed(base, idx, v039C);
    anim_set_reverse(base, idx, v03D4);
    anim_set_loop_mode(base, idx, v040C);
    anim_set_interp(f, base, idx, v0480);
    anim_play(base, idx, -1);
}

#define RESET_CHANNEL(tbl)         \
    anim_tex_set_speed((s32) (tbl), 0); \
    anim_tex_set_reverse((s32) (tbl), 0); \
    anim_tex_set_loop_mode((s32) (tbl), 0); \
    anim_tex_play((s32) (tbl), -1)

/* Resets the vehicle's texture animations (speed 0, forward, wrap, play) for Backlash (kind 5) and Ramdozer
 * (kind 4), as backlash_enter / ramdozer_enter do (0x80202380).
 * reset the texture animations of vehicle kind 5 or 4.  (After the kind-4
 * block the asm has a third, unreachable one that would set up gTexAnimPoliceLightsA and
 * gTexAnimPoliceLightsB with 100.) */
void fe_veh_reset_tex_anims(s32 kind) {
    if (kind == VEH_BACKLASH) {
        RESET_CHANNEL(gBacklashYawTexAnimA);
        RESET_CHANNEL(gBacklashYawTexAnimB);
    } else if (kind == VEH_RAMDOZER) {
        RESET_CHANNEL(gRamdozerTrackTexAnimA);
        RESET_CHANNEL(gRamdozerTrackTexAnimB);
        RESET_CHANNEL(gRamdozerHeadingTexAnim);
    }
}

/* Sets the heading-driven texture-animation frames of Backlash / Ramdozer from val, a heading in 4096ths of a
 * turn (as their _update_anims do) (0x802025D0).
 * texture-animation frames from `val` for vehicle kind 5 or 4 */
void fe_veh_set_heading_tex_anims(s32 kind, s32 val) {
    if (kind == VEH_BACKLASH) {
        if (val >= 0x800) {
            val -= 0x800;
        }
        val = (u32) val / 0x55;
        anim_tex_set_frame(0.0f, (s32) gBacklashYawTexAnimA, val);
        anim_tex_set_frame(0.0f, (s32) gBacklashYawTexAnimB, val);
    } else if (kind == VEH_RAMDOZER) {
        if (val >= 0x355 && val < 0x8AB) {
            val -= 0x355;
        } else {
            val = 0;
        }
        val = (u32) val / 0x4C;
        anim_tex_set_frame(0.0f, (s32) gRamdozerHeadingTexAnim, val);
        anim_tex_set_speed((s32) gRamdozerTrackTexAnimA, 0x32);
        anim_tex_set_speed((s32) gRamdozerTrackTexAnimB, 0x32);
    }
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_front_end/1B100/fe_load_model.s")

#pragma GLOBAL_ASM("asm/nonmatchings/hd_front_end/1B100/fe_anim_update_model.s")

#pragma GLOBAL_ASM("asm/nonmatchings/hd_front_end/1B100/fe_anim_init_model.s")

#pragma GLOBAL_ASM("asm/nonmatchings/hd_front_end/1B100/fe_anim_start_channel.s")

#pragma GLOBAL_ASM("asm/nonmatchings/hd_front_end/1B100/fe_veh_reset_tex_anims.s")

#pragma GLOBAL_ASM("asm/nonmatchings/hd_front_end/1B100/fe_veh_set_heading_tex_anims.s")
#endif
