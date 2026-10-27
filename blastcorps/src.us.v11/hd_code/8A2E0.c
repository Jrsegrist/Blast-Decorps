#include "common.h"
#include <ultra64.h>
#include "game/game.h"
/* Views: this file reads these shared variables (game/variables.h) as other types. */
#define gEffectScratch (*(Debris8A2E0 *) gEffectScratch)
#define gCommPts ((CommPoint *) gCommPts)
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
/* gCommPts: table of 0x14-byte entries, gCommPtCount of them in use: s32
 * x, y, z at +0, a word at +0xC (0x280), a flag halfword at +0x10 (a
 * bit/flag state, see 409D0's stats_set_level_flag/stats_test_level_flag) and the record's
 * halfword +6 at +0x12. */
typedef struct {
    /* 0x00 */ s32 pos[3];
    /* 0x0C */ s32 unkC;
    /* 0x10 */ u16 taken; /* picked up (commpt_update; saved per level by commpt_save_flags) */
    /* 0x12 */ u16 unk12;
} CommPoint; /* size 0x14 */


extern u8 gCommPtTakenAnims[]; /* channel tables of the boxes' two models */
extern u8 gCommPtFreeAnims[];
extern u8 * N64P gCommPtBoxModel;  /* the boxes' model data */
extern u8 * N64P gCommPtTakenPose0;  /* four 0x300-byte save copies */
extern u8 * N64P gCommPtTakenPose1;
extern u8 * N64P gCommPtFreePose0;
extern u8 * N64P gCommPtFreePose1;
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
void commpt_set_box_verts(s32 x, s32 y, s32 z);

/* Load channels for one model: anim_init_model(b, a, ch, gCommPtBoxModel), channel 0
 * = (100, 0, 0, 0.0, 1), run it into both copies (anim_update_model, then
 * anim_rewind and anim_play(1) between them). */
#define BOX_MODEL_CHANNELS(ch, a, b)                                          \
    {                                                                         \
        anim_init_model((u32 *) (b), (u32 *) (a), (AnimChannel *) (ch), gCommPtBoxModel); \
        anim_set_speed((AnimChannel *) (ch), 0, 100);                     \
        anim_set_reverse((AnimChannel *) (ch), 0, 0);                       \
        anim_set_loop_mode((AnimChannel *) (ch), 0, 0);                       \
        anim_set_interp(0.0f, (AnimChannel *) (ch), 0, 0);                 \
        anim_play((AnimChannel *) (ch), 0, 1);                       \
        anim_update_model((a), (b), (AnimChannel *) (ch));                   \
        anim_rewind(0, (AnimChannel *) (ch));                          \
        anim_play((AnimChannel *) (ch), 0, 1);                       \
        anim_update_model((b), (a), (AnimChannel *) (ch));                   \
    }

/* Level load: the communication points (model 0x96) (0x802CEAA0).
 * Level setup of the boxes (pickups, level object in obj; its list at
 * obj + obj[0x28] .. obj + obj[0x2C], 8-byte records {s16 x, y, z; u16 w}):
 * gCommPtCount = 0; if the list isn't empty, load model 0x96 (lvl_load_model)
 * into gCommPtBoxModel, then for each record: entry pos = (x, y, z) << 5, the
 * box vertices (commpt_set_box_verts(x, y, z), unshifted), unkC = 0x280, flag 0,
 * unk12 = w, gCommPtCount++. If any box exists: four 0x300-byte copies from
 * the gHeapPtr bump pointer (gCommPtTakenPose0..EC; trapping adds), model
 * channels gCommPtTakenAnims (copies 1E0/1E4) with channels 1 and 2 set to (2, 0,
 * 0, -1), and gCommPtFreeAnims (copies 1E8/1EC).
 * Register convention: obj in t0 (conventions.txt); the asm saves t0 (asm
 * caller initlevel keeps it) and leaves t1/t2 (list end, the model
 * pointer), lvl_load_model's s2/s4 and the channel routines' s0-s5, fp,
 * f20, f30 changed; the survey lists t1, t2, s3, f12, f14 as read by
 * initlevel (dead there, not modelled). Its loop uses `!=` (the list
 * must end on a record boundary). */
void commpt_setup(u8 *obj) {
    Out802A396C o;
    CommPoint *e;
    s16 *p;
    s16 *end;
    u32 h;

    gCommPtCount = 0;
    p = (s16 *) (obj + *(s32 *) (obj + 0x28));
    end = (s16 *) (obj + *(s32 *) (obj + 0x2C));
    if (p != end) {
        lvl_load_model(0x96, &o);
        gCommPtBoxModel = o.s2;
        e = gCommPts;
        while (p != end) {
            e->pos[0] = p[0] << 5;
            e->pos[1] = p[1] << 5;
            e->pos[2] = p[2] << 5;
            commpt_set_box_verts(p[0], p[1], p[2]);
            e->unkC = 0x280;
            e->taken = 0;
            e->unk12 = p[3];
            gCommPtCount++;
            e++;
            p += 4;
        }
    }
    if (gCommPtCount == 0) {
        return;
    }
    h = gHeapPtr;
    gCommPtTakenPose0 = (u8 *) h;
    gCommPtTakenPose1 = (u8 *) (h + 0x300);
    gCommPtFreePose0 = (u8 *) (h + 0x600);
    gCommPtFreePose1 = (u8 *) (h + 0x900);
    gHeapPtr = h + 0xC00;
    BOX_MODEL_CHANNELS(gCommPtTakenAnims, gCommPtTakenPose0, gCommPtTakenPose1);
    anim_set_speed((AnimChannel *) gCommPtTakenAnims, 1, 2);
    anim_set_reverse((AnimChannel *) gCommPtTakenAnims, 1, 0);
    anim_set_loop_mode((AnimChannel *) gCommPtTakenAnims, 1, 0);
    anim_play((AnimChannel *) gCommPtTakenAnims, 1, -1);
    anim_set_speed((AnimChannel *) gCommPtTakenAnims, 2, 2);
    anim_set_reverse((AnimChannel *) gCommPtTakenAnims, 2, 0);
    anim_set_loop_mode((AnimChannel *) gCommPtTakenAnims, 2, 0);
    anim_play((AnimChannel *) gCommPtTakenAnims, 2, -1);
    BOX_MODEL_CHANNELS(gCommPtFreeAnims, gCommPtFreePose0, gCommPtFreePose1);
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/8A2E0/commpt_setup.s")
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
extern Vtx gCommPtVtx[]; /* 8 vertices per box */

/* The 8 corner vertices of point n (0x802CEE14).
 * Writes the 8 corner positions of the box x-10..x+10, y..y+20, z-10..z+20
 * into the vertices of box gCommPtCount (only ob[] is written). The asm takes
 * x, y, z in t4, t5, t6 (conventions.txt) and saves everything it uses; its
 * asm caller commpt_setup keeps a1-a3, t1-t3, f12 and f14 live across the
 * call (a mixed N64 build would need a thunk, the native port doesn't). */
void commpt_set_box_verts(s32 x, s32 y, s32 z) {
    Vtx *v = &gCommPtVtx[gCommPtCount * 8];
    s32 x0 = x - 10;
    s32 x1 = x + 10;
    s32 y1 = y + 20;
    s32 z0 = z - 10;
    s32 z1 = z + 20;

    v[0].v.ob[0] = x0, v[0].v.ob[1] = y, v[0].v.ob[2] = z0;
    v[1].v.ob[0] = x0, v[1].v.ob[1] = y1, v[1].v.ob[2] = z0;
    v[2].v.ob[0] = x1, v[2].v.ob[1] = y, v[2].v.ob[2] = z0;
    v[3].v.ob[0] = x1, v[3].v.ob[1] = y1, v[3].v.ob[2] = z0;
    v[4].v.ob[0] = x0, v[4].v.ob[1] = y, v[4].v.ob[2] = z1;
    v[5].v.ob[0] = x0, v[5].v.ob[1] = y1, v[5].v.ob[2] = z1;
    v[6].v.ob[0] = x1, v[6].v.ob[1] = y, v[6].v.ob[2] = z1;
    v[7].v.ob[0] = x1, v[7].v.ob[1] = y1, v[7].v.ob[2] = z1;
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/8A2E0/commpt_set_box_verts.s")
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING

#define K0_PHYS(p) ((u32) (p) - 0x80000000)

/* The display list of every communication point (0x802CEEFC).
 * Draw the boxes (00000.c: gdl = commpt_draw(gdl, gBufferIndex, dl2, mtx)):
 * if there are any, gdl gets segment 6 = the model data's part at
 * gCommPtBoxModel + [5] (G_MOVEWORD 0xBC001806); then per box i: segment 7 = the
 * box's save copy (taken: gCommPtTakenPose1 / gCommPtTakenPose0, else gCommPtFreePose1 /
 * gCommPtFreePose0, the second of each pair when `cur` is clear) and a G_DL to
 * dl2; dl2 gets G_VTX of the box's 8 vertices (gCommPtVtx + 0x80 i), a
 * G_CULLDL 0..8, its translation matrix (mtx_translate(pos << 11) into
 * mtx, converted by mtx_to_n64, G_MTX 0x01040040), G_DLs to the model's
 * parts [9] and [11], G_POPMTX and G_ENDDL; mtx advances 0x40 per box. Then
 * the box channels run into the two copies (anim_update_model on gCommPtTakenAnims,
 * current copy first). Returns the advanced gdl; dl2 and mtx aren't handed
 * back (the caller doesn't need them). cur is tested as a whole register\n * (00000.c declares it u8). The asm's pointer adds trap; it saves
 * s0-s7, gp, fp but not the f20/f30 that anim_update_model changes
 * (conventions.txt: clobbers). */
Gfx *commpt_draw(Gfx *gdl, s32 cur, Gfx *dl2, Mtx *mtx) {
    u8 *data = gCommPtBoxModel;
    CommPoint *e;
    u8 *seg;
    s32 n;
    s32 i;

    if (gCommPtCount == 0) {
        return gdl;
    }
    gdl->words.w0 = 0xBC001806;
    gdl->words.w1 = K0_PHYS(data + *(s32 *) (data + 0x14));
    gdl++;
    e = gCommPts;
    for (n = gCommPtCount, i = 0; n != 0; n--, e++, i++) {
        if (e->taken != 0) {
            seg = cur ? gCommPtTakenPose1 : gCommPtTakenPose0;
        } else {
            seg = cur ? gCommPtFreePose1 : gCommPtFreePose0;
        }
        gdl[0].words.w0 = 0xBC001C06;
        gdl[0].words.w1 = K0_PHYS(seg);
        gdl[1].words.w0 = 0x06000000;
        gdl[1].words.w1 = K0_PHYS(dl2);
        dl2[0].words.w0 = 0x04700080;
        dl2[0].words.w1 = (u32) ((u8 *) gCommPtVtx + 0x80 * i);
        dl2[1].words.w0 = 0xBE000000;
        dl2[1].words.w1 = 0x140;
        gdl += 2;
        dl2 += 2;
        mtx_translate(e->pos[0] << 11, e->pos[1] << 11, e->pos[2] << 11, (s32 *) mtx);
        mtx_to_n64((u16 *) mtx);
        dl2->words.w0 = 0x01040040;
        dl2->words.w1 = K0_PHYS(mtx);
        dl2++;
        data = gCommPtBoxModel;
        dl2[0].words.w0 = 0x06000000;
        dl2[0].words.w1 = K0_PHYS(data + *(s32 *) (data + 0x24));
        dl2[1].words.w0 = 0x06000000;
        dl2[1].words.w1 = K0_PHYS(data + *(s32 *) (data + 0x2C));
        dl2[2].words.w0 = 0xBD000000;
        dl2[2].words.w1 = 0;
        dl2[3].words.w0 = 0xB8000000;
        dl2[3].words.w1 = 0;
        dl2 += 4;
        mtx++;
    }
    if (cur != 0) {
        anim_update_model(gCommPtTakenPose1, gCommPtTakenPose0, (AnimChannel *) gCommPtTakenAnims);
    } else {
        anim_update_model(gCommPtTakenPose0, gCommPtTakenPose1, (AnimChannel *) gCommPtTakenAnims);
    }
    return gdl;
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/8A2E0/commpt_draw.s")
#endif

#ifdef NON_MATCHING
void commpt_burst(s32 *pos);

float sqrtf(float);
#pragma intrinsic(sqrtf)

/* cvt.w.s: float -> s32 rounding to nearest, ties to even (the game's FCSR
 * mode), not truncation like a C cast. */
static s32 port_cvt_w_s(f32 x) {
    s32 t = (s32) x;
    f32 frac = x - (f32) t;

    if (frac > 0.5f || (frac == 0.5f && (t & 1))) {
        t++;
    } else if (frac < -0.5f || (frac == -0.5f && (t & 1))) {
        t--;
    }
    return t;
}
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
/* The player touches a point: taken, burst, maybe the level end (0x802CF1A4).
 * Pickups: for each of the gCommPtCount entries of gCommPts not yet taken
 * (flag +0x10 == 0) whose position is within 0x4C of the player (both >> 5,
 * squared distance summed in 32 bits, sqrt.s and cvt.w.s rounding; the sum
 * must not overflow, or the asm's cvt.w.s of a NaN would trap), mark it
 * taken, set gCommPtTakenAnims's sound fields 2 and 1 to 0.0 (anim_set_frame), call
 * stats_finish_bonus_level with the countdown value (gCommPtCount for entry 0, down to 1:
 * the same quirk as commpt_save_flags), then stats_show_commpt_hint and the
 * commpt_burst burst at the entry. The player position is read once. The
 * asm saves every register (k1, gp, sp, fp included) around the two C calls
 * only because it keeps its loop state in t0-t4 across them; its only
 * caller is C (hd_game_frame), so no asm caller relies on anything. */
void commpt_update(void) {
    s32 px = gPlayerX >> 5;
    s32 py = gPlayerY >> 5;
    s32 pz = gPlayerZ >> 5;
    CommPoint *e = gCommPts;
    s32 n;

    for (n = gCommPtCount; n != 0; n--, e++) {
        s32 dx;
        s32 dy;
        s32 dz;

        if (e->taken != 0) {
            continue;
        }
        dx = (e->pos[0] >> 5) - px;
        dy = (e->pos[1] >> 5) - py;
        dz = (e->pos[2] >> 5) - pz;
        if (port_cvt_w_s(sqrtf((f32) (s32) ((u32) (dx * dx) + (u32) (dy * dy) + (u32) (dz * dz)))) < 0x4C) {
            e->taken = 1;
            anim_set_frame(0.0f, (AnimChannel *) gCommPtTakenAnims, 2, 0);
            anim_set_frame(0.0f, (AnimChannel *) gCommPtTakenAnims, 1, 0);
            stats_finish_bonus_level(n);
            stats_show_commpt_hint();
            commpt_burst(e->pos);
        }
    }
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/8A2E0/commpt_update.s")
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
/* The 0x38-byte debris record handed to obj_queue_effect (77E20.c's EffectSpawn). */
typedef struct {
    /* 0x00 */ s32 pos[3];
    /* 0x0C */ s32 unkC;
    /* 0x10 */ s32 unk10[6];
    /* 0x28 */ u32 unk28;
    /* 0x2C */ u16 unk2C;
    /* 0x2E */ u8 unk2E;
    /* 0x2F */ u8 pad2F;
    /* 0x30 */ u8 unk30;
    /* 0x31 */ u8 kind;   /* the effect kind (EFFECT_KIND_*) */
    /* 0x32 */ u8 pad32[2];
    /* 0x34 */ u8 unk34;
    /* 0x35 */ u8 unk35;
    /* 0x36 */ u8 pad36[2];
} Debris8A2E0;


/* 24 effect records and sound 0x5E at a point (0x802CF3E0).
 * Burst at pos (s32 x, y, z): fx_finish_all(), frees every gEffectQueue slot
 * (obj_clear_effect_queue), then 24 debris records (gEffectScratch, through
 * obj_queue_effect) at (x, y, z) << 11, the i-th one 0xA0000 * i higher, with
 * unkC = 50000 + 0x36B0 * i, unk2C = i and kind EFFECT_KIND_COMMPT_BURST (0x15); finally
 * plays sound 0x5E (snd_play(gSfxBank, 0x5E, NULL)). The asm takes pos
 * in t1 and preserves every register (all of them saved); asm caller
 * commpt_update keeps t0-t4 live across the call. The asm's trapping `add`s
 * on the height and unkC can't overflow for game coordinates. */
void commpt_burst(s32 *pos) {
    Debris8A2E0 *d = &gEffectScratch;
    s32 x;
    s32 y;
    s32 z;
    s32 w;
    s32 i;

    fx_finish_all();
    obj_clear_effect_queue();
    x = pos[0] << 11;
    y = pos[1] << 11;
    z = pos[2] << 11;
    for (i = 0, w = 50000; i != 24; i++) {
        d->pos[1] = y;
        y += 0xA0000;
        d->unk28 = 0xFC180000;
        d->unkC = w;
        d->unk2C = i;
        d->pos[0] = x;
        d->pos[2] = z;
        w += 0x36B0;
        d->unk10[0] = 0;
        d->unk10[1] = 0;
        d->unk10[2] = 0;
        d->unk10[3] = 0;
        d->unk10[4] = 0;
        d->unk10[5] = 0;
        d->unk2E = 0;
        d->unk30 = 0;
        d->kind = EFFECT_KIND_COMMPT_BURST;
        d->unk34 = 0;
        d->unk35 = 0;
        obj_queue_effect((u32 *) d);
    }
    snd_play(gSfxBank, SFX_COMMPT_BURST, NULL);
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/8A2E0/commpt_burst.s")
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
/* The taken points into the per-level flag bits (0x802CF5B0).
 * For each of the gCommPtCount entries whose +0x10 flag is set, call
 * stats_set_level_flag. Quirk kept from the asm: the argument is the countdown value
 * (gCommPtCount for entry 0, down to 1 for the last entry), not the index. */
void commpt_save_flags(void) {
    CommPoint *e = gCommPts;
    u8 n;

    for (n = gCommPtCount; n != 0; n--, e++) {
        if (e->taken != 0) {
            stats_set_level_flag(n);
        }
    }
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/8A2E0/commpt_save_flags.s")
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
/* The per-level flag bits into the taken flags (0x802CF628).
 * Refresh every entry's +0x10 flag from stats_test_level_flag, passing the same
 * countdown value as commpt_save_flags (stored as a halfword). */
void commpt_load_flags(void) {
    CommPoint *e = gCommPts;
    u8 n;

    for (n = gCommPtCount; n != 0; n--, e++) {
        e->taken = stats_test_level_flag(n);
    }
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/8A2E0/commpt_load_flags.s")
#endif
