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
/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
extern u8 * N64P gShuttleModel;  /* model header */
extern u8 * N64P gShuttleMtxA;  /* the two 0x800-byte save copies */
extern u8 * N64P gShuttleMtxB;
extern u8 gShuttleState[]; /* this vehicle's state block */
extern u8 gShuttleAnim[]; /* channel table */
extern void * N64P gShuttleSound; /* sound state */
extern u8 gShuttlePoint1Sounded;
extern u8 gShuttlePoint2Sounded;
extern u8 gShuttlePoint3Sounded;
extern u8 gShuttleFlameTimer;
extern u8 gShuttleFlamesOn;
extern u8 gShuttleSmokeOn;
extern u8 gShuttleSmokeTimer;
extern u8 gShuttleShakeOn;

/* Sets up the shuttle (type 0xFD, lvl_spawn_shuttle) (0x802D2570).
 * Set up vehicle 0xFD (the level-object dispatcher lvl_spawn_shuttle): model
 * header hdr -> gShuttleModel; two 0x800-byte save copies from the gHeapPtr
 * bump pointer (gShuttleMtxA/B; trapping adds); veh_add_record(0xFD, 0,
 * copies, hdr); heading/target/speed of gShuttleState and the event flags
 * gShuttlePoint1Sounded..7A (but 75) cleared; model channels gShuttleAnim (anim_init_model,
 * channel 0 = (100, 0, 0, 0.0, 1), run into both copies); channel 2's sound
 * part: mode gShuttlePathKind 1 -> anim_set_frame(0.0, 2, 0), 0 -> start sound 0x6A
 * into gShuttleSound at volume 0 (snd_play, snd_modify(.., 8, 0)) and
 * anim_set_frame(f0, 2, 1); then channels 2 = (0, 0, 1, -1) and 1 = (0, 0,
 * 0360 0.0, 1, -1); veh_add_shadow(0xFD, 120, 45, 45), util_shuttle_path_init, one
 * update with +0x9A set (shuttle_update), and the model matrix copy (word +4
 * of the entry at hdr + hdr[6], gShuttleMtxB -> gShuttleMtxA, mtx_copy_at).
 * Register notes: hdr comes in s2 (conventions.txt); the asm saves t0-t5
 * (asm caller lvl_spawn_shuttle keeps t2), points $gp at gShuttleState and leaves
 * it, and leaves the channel routines' s0-s5, fp, f20 and shuttle_update's
 * f12/f14 (survey: read by lvl_spawn_shuttle; not modelled). In mode 0 the asm
 * hands anim_set_frame whatever f0 snd_modify left (the mtc1 zero before
 * the sound calls; those IDO sound routines do no FP work): 0.0 here. Any
 * other mode hits the asm's `syscall` debug trap and then runs mode 1, as
 * here. */
void shuttle_spawn(u8 *hdr) {
    u32 p;
    u8 *h;
    void *snd;

    gShuttleModel = hdr;
    p = gHeapPtr;
    gShuttleMtxA = (u8 *) p;
    gShuttleMtxB = (u8 *) (p + 0x800);
    gHeapPtr = p + 0x1000;
    veh_add_record(VEH_SHUTTLE, 0, (s32) gShuttleMtxA, (s32) gShuttleMtxB, hdr);
    *(s16 *) (gShuttleState + 0x4C) = 0;
    *(s16 *) (gShuttleState + 0x4E) = 0;
    *(s16 *) (gShuttleState + 0x76) = 0;
    gShuttlePoint1Sounded = 0;
    gShuttlePoint2Sounded = 0;
    gShuttlePoint3Sounded = 0;
    gShuttleFlameTimer = 0;
    gShuttleFlamesOn = 0;
    gShuttleShakeOn = 0;
    gShuttleSmokeOn = 0;
    gShuttleSmokeTimer = 0;
    anim_init_model((u32 *) gShuttleMtxB, (u32 *) gShuttleMtxA, (AnimChannel *) gShuttleAnim, gShuttleModel);
    anim_set_speed((AnimChannel *) gShuttleAnim, 0, 100);
    anim_set_reverse((AnimChannel *) gShuttleAnim, 0, 0);
    anim_set_loop_mode((AnimChannel *) gShuttleAnim, 0, 0);
    anim_set_interp(0.0f, (AnimChannel *) gShuttleAnim, 0, 0);
    anim_play((AnimChannel *) gShuttleAnim, 0, 1);
    anim_update_model(gShuttleMtxA, gShuttleMtxB, (AnimChannel *) gShuttleAnim);
    anim_rewind(0, (AnimChannel *) gShuttleAnim);
    anim_play((AnimChannel *) gShuttleAnim, 0, 1);
    anim_update_model(gShuttleMtxB, gShuttleMtxA, (AnimChannel *) gShuttleAnim);
    if (gShuttlePathKind == 0) {
        snd = snd_play(gSfxBank, SFX_SHUTTLE_LOOP, &gShuttleSound);
        snd_modify(snd, 8, 0);
        anim_set_frame(0.0f, (AnimChannel *) gShuttleAnim, 2, 1);
    } else {
        anim_set_frame(0.0f, (AnimChannel *) gShuttleAnim, 2, 0);
    }
    anim_set_speed((AnimChannel *) gShuttleAnim, 2, 0);
    anim_set_reverse((AnimChannel *) gShuttleAnim, 2, 0);
    anim_set_loop_mode((AnimChannel *) gShuttleAnim, 2, 1);
    anim_play((AnimChannel *) gShuttleAnim, 2, -1);
    anim_set_speed((AnimChannel *) gShuttleAnim, 1, 0);
    anim_set_reverse((AnimChannel *) gShuttleAnim, 1, 0);
    anim_set_frame(0.0f, (AnimChannel *) gShuttleAnim, 1, 0);
    anim_set_loop_mode((AnimChannel *) gShuttleAnim, 1, 1);
    anim_play((AnimChannel *) gShuttleAnim, 1, -1);
    veh_add_shadow(VEH_SHUTTLE, 120, 45, 45);
    util_shuttle_path_init();
    gShuttleState[0x9A] = 1;
    shuttle_update();
    gShuttleState[0x9A] = 0;
    h = gShuttleModel;
    h += *(s32 *) (h + 0x18);
    mtx_copy_at(gShuttleMtxB, gShuttleMtxA, *(s32 *) (h + 4));
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/8DDB0/shuttle_spawn.s")
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
void shuttle_follow_path(void);
void shuttle_update_volume(void);
void shuttle_update_events(void);
void shuttle_build_model(void);
extern u8 * N64P gShuttleMtxA; /* the two save copies */
extern u8 * N64P gShuttleMtxB;
extern u8 gShuttleAnim[]; /* channel table */
extern u8 gShuttleState[]; /* this vehicle's state block */

/* The shuttle, per frame (0x802D291C).
 * Per-frame update of this vehicle (gShuttleState, the asm's $gp): shuttle_follow_path;
 * unless byte +0x9A is set, the sound distance (shuttle_update_volume) and the events
 * (shuttle_update_events); then the channels gShuttleAnim (anim_update_model with the
 * current save copy first: gShuttleMtxA when gBufferIndex is set, else
 * gShuttleMtxB) and the model placement (shuttle_build_model). The asm saves every
 * callee-saved register. It leaves shuttle_build_model's f12/f14 (mtx_build_model's
 * trig temporaries), which the survey lists as read by asm caller
 * shuttle_spawn (not modelled; its C rewrite doesn't read them). */
void shuttle_update(void) {
    shuttle_follow_path();
    if (gShuttleState[0x9A] == 0) {
        shuttle_update_volume();
        shuttle_update_events();
    }
    if (gBufferIndex != 0) {
        anim_update_model(gShuttleMtxA, gShuttleMtxB, (AnimChannel *) gShuttleAnim);
    } else {
        anim_update_model(gShuttleMtxB, gShuttleMtxA, (AnimChannel *) gShuttleAnim);
    }
    shuttle_build_model();
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/8DDB0/shuttle_update.s")
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING

/* Util_shuttle_path_step with gp saved (0x802D2A40).
 * Calls util_shuttle_path_step, saving gp around it (the asm caller shuttle_update
 * uses gp as its base pointer). The survey lists a0, a2, a3, t6, t7, f12 and
 * f14 as read by that caller afterwards; they're whatever util_shuttle_path_step
 * leaves, nothing this function sets. */
void shuttle_follow_path(void) {
    util_shuttle_path_step();
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/8DDB0/shuttle_follow_path.s")
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
double sqrt(double);
#pragma intrinsic(sqrt)

/* math_dist3 (62740; points in t3-t5 and t6,t7,s0, result in s1) can't be
 * called from C: this is its logic (same macro as in 77E20.c). The distance
 * between two points, rounded to nearest (ties to even) as cvt.l.d does; the
 * squares are summed as s64 and converted as hi * 2^32 + lo (one rounding,
 * like cvt.d.l; a plain cast would call __ll_to_d, which the harness can't run). */
#define DIST3_802ABCDC(out, ax, ay, az, bx, by, bz)                     \
    do {                                                                \
        s64 _dx = (s32) ((bx) - (ax));                                  \
        s64 _dy = (s32) ((by) - (ay));                                  \
        s64 _dz = (s32) ((bz) - (az));                                  \
        s64 _sq = _dx * _dx + _dy * _dy + _dz * _dz;                    \
        f64 _d = sqrt((f64) (s32) (_sq >> 32) * 4294967296.0 + (f64) (u32) _sq); \
        s32 _r = (s32) _d;                                              \
        f64 _f = _d - _r;                                               \
                                                                        \
        if (_f > 0.5 || (_f == 0.5 && (_r & 1))) {                      \
            _r++;                                                       \
        }                                                               \
        (out) = _r;                                                     \
    } while (0)

extern void * N64P gShuttleSound; /* sound state, NULL = none */

/* Its engine volume by distance from the camera (0x802D2A74).
 * Distance attenuation: if gShuttleSound is set, sets its volume (parameter 8)
 * to 0x7FFF - max(d - 0x3200, 0) / 4, clamped at 0, where d is the distance
 * from (gCamEyeX, gCamEyeY, gCamEyeZ) >> 11 to gShuttlePos/gShuttlePosY/gShuttlePosZ.
 * The asm's dead `sub` (x - gShuttlePos) and its `addi` trap on overflow; C
 * doesn't. Register note: the asm saves and restores every register; asm
 * caller shuttle_update keeps a0, a2, a3, t6, t7, f12 and f14 live across the
 * call (a mixed N64 build would need a thunk; the native port doesn't). */
void shuttle_update_volume(void) {
    s32 dist;
    s32 vol;

    if (gShuttleSound == NULL) {
        return;
    }
    DIST3_802ABCDC(dist, gCamEyeX >> 11, gCamEyeY >> 11, gCamEyeZ >> 11,
                   gShuttlePos, gShuttlePosY, gShuttlePosZ);
    dist -= 0x3200;
    if (dist < 0) {
        dist = 0;
    }
    vol = 0x7FFF - (dist >> 2);
    if (vol < 0) {
        vol = 0;
    }
    snd_modify(gShuttleSound, 8, vol);
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/8DDB0/shuttle_update_volume.s")
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING


extern u8 gShuttleAnim[];
extern u8 gShuttlePoint1Sounded;
extern u8 gShuttlePoint2Sounded;
extern u8 gShuttlePoint3Sounded;
extern u8 gShuttleFlameTimer;
extern u8 gShuttleFlamesOn;
extern u8 gShuttleSmokeOn;
extern u8 gShuttleSmokeTimer;
extern u8 gShuttleShakeOn;

/* Spawns effect def at gVehPoints record (0xFD, n) (fx_spawn type 1 with
 * z = 1, tag 0, b35 0). For that type fx_spawn ignores its other fields;
 * the asm passes whatever its caller left in t6, t7, s0-s4 there, the C 0. */
#define SPAWN_AT_FD(def, data, n)                                         \
    {                                                                     \
        Io802A6274 _io;                                                   \
                                                                          \
        _io.a3 = 0;                                                       \
        _io.t6 = 0;                                                       \
        _io.s1 = 0;                                                       \
        fx_spawn(&_io, (def), (data), 1, 0xFD, (n), 1, 0, 0, 0, 0, 0, 0); \
    }

/* Path events: sounds, shake, flame effects (0x802D2C20).
 * Per-frame event handling for this vehicle (id 0xFD in gVehPoints): event
 * gShuttlePathEvent 2 sets gShuttleAnim entry 1's fields (anim_set_speed/03D4/0290)
 * and plays sound 0x66; 3 plays 0x78; 0xB sets gShuttleFlamesOn/ShakeOn/SmokeOn and plays
 * 0x83; 0xC plays 0x7F into gShuttleSound and clears gShuttleShakeOn; 0xD clears
 * gShuttleSmokeOn. Unless gShuttleAborted, each of the three one-shot flags
 * gShuttlePoint1Sounded..74 still clear plays sound 6, 6, 8 (and gets set) once
 * gShuttleTriggerY (read once) reaches word +4 of gVehPoints record (0xFD, 1..3).
 * gShuttleShakeOn forces gShakeTime = 20 and gShakeSize = 400. gShuttleSmokeOn and
 * gShuttleFlamesOn each run a 5-frame countdown (gShuttleSmokeTimer / gShuttleFlameTimer) that
 * spawns effects at the vehicle's points 4 (gFxDefKind04) or 4, 5, 6
 * (gFxDefJetFlame). Only caller shuttle_update (asm); the survey's t6/t7/s0/s1
 * "outputs" are what anim_update_model sets after it, not read from here. */
void shuttle_update_events(void) {
    u8 *rec;
    s32 level;

    switch (gShuttlePathEvent) {
        case 2:
            anim_set_speed((AnimChannel *) gShuttleAnim, 1, 1);
            anim_set_reverse((AnimChannel *) gShuttleAnim, 1, 0);
            anim_play((AnimChannel *) gShuttleAnim, 1, 1);
            snd_play(gSfxBank, SFX_SHUTTLE_EVENT2, NULL);
            break;
        case 3:
            snd_play(gSfxBank, SFX_SHUTTLE_EVENT3, NULL);
            break;
        case 0xB:
            gShuttleFlamesOn = 1;
            gShuttleShakeOn = 1;
            gShuttleSmokeOn = 1;
            snd_play(gSfxBank, SFX_SHUTTLE_ROAR, NULL);
            break;
        case 0xC:
            snd_play(gSfxBank, SFX_SHUTTLE_LOOP2, &gShuttleSound);
            gShuttleShakeOn = 0;
            break;
        case 0xD:
            gShuttleSmokeOn = 0;
            break;
    }

    if (gShuttleAborted == 0) {
        level = gShuttleTriggerY;
        if (gShuttlePoint1Sounded == 0) {
            veh_find_point(0xFD, 1, &rec);
            if (!(level < ((s32 *) rec)[1])) {
                snd_play(gSfxBank, SFX_SHUTTLE_POINT, NULL);
                gShuttlePoint1Sounded = 1;
            }
        }
        if (gShuttlePoint2Sounded == 0) {
            veh_find_point(0xFD, 2, &rec);
            if (!(level < ((s32 *) rec)[1])) {
                snd_play(gSfxBank, SFX_SHUTTLE_POINT, NULL);
                gShuttlePoint2Sounded = 1;
            }
        }
        if (gShuttlePoint3Sounded == 0) {
            veh_find_point(0xFD, 3, &rec);
            if (!(level < ((s32 *) rec)[1])) {
                snd_play(gSfxBank, SFX_SHUTTLE_POINT3, NULL);
                gShuttlePoint3Sounded = 1;
            }
        }
    }

    if (gShuttleShakeOn != 0) {
        gShakeTime = 20;
        gShakeSize = 400;
    }
    if (gShuttleSmokeOn != 0) {
        if (gShuttleSmokeTimer != 0) {
            gShuttleSmokeTimer -= 1;
        } else {
            gShuttleSmokeTimer = 4;
            SPAWN_AT_FD(gFxDefKind04, 0x107AC0, 4);
        }
    }
    if (gShuttleFlamesOn != 0) {
        if (gShuttleFlameTimer != 0) {
            gShuttleFlameTimer -= 1;
        } else {
            gShuttleFlameTimer = 4;
            SPAWN_AT_FD(gFxDefJetFlame, 0x53020, 4);
            SPAWN_AT_FD(gFxDefJetFlame, 0x53020, 5);
            SPAWN_AT_FD(gFxDefJetFlame, 0x5CC60, 6);
        }
    }
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/8DDB0/shuttle_update_events.s")
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
extern u8 * N64P gShuttleModel;    /* model header: word offsets to the part lists */
extern u8 * N64P gShuttleMtxA;    /* the two save copies */
extern u8 * N64P gShuttleMtxB;
extern u8 gShuttleState[];   /* this vehicle's state block (the asm caller's $gp) */

/* Its model matrix and points (0x802D2FA4).
 * Place this vehicle's model: m = the word at +4 of the header entry at
 * hdr + hdr[6] (hdr = gShuttleModel), plus the current save copy (gShuttleMtxA
 * when gBufferIndex is set, else gShuttleMtxB); rotation gModelRot/gModelRotY/gModelRotZ =
 * gShuttleRotX/Y/Z (6C also to the heading at +0x4C of gShuttleState and to
 * gShuttleHeading); mtx_build_model(position gShuttlePos..50, scale 0x11558, m).
 * Then the points (veh_points_transform, id 0xFD, hdr + hdr[0] .. hdr + hdr[1])
 * relative to the current save copy. The header offsets are added with
 * trapping adds.
 * Register notes: the asm's $gp (= gShuttleState, set by its only caller
 * shuttle_update) is read as the global. It hands veh_points_transform whatever v1,
 * a0, a3, s0, s1 held (they only matter for a point entry with no matrices;
 * 0 here) and s2 = m, and leaves mtx_build_model's f12/f14, which the survey
 * lists as read by shuttle_update's callers (not modelled). Clobbers s1, s2,
 * s4-s7 (conventions.txt). */
void shuttle_build_model(void) {
    MtxChainRegs regs;
    u8 *hdr = gShuttleModel;
    u8 *base;
    s32 *m;

    m = (s32 *) N64_IPTR((*(s32 *) (hdr + *(s32 *) (hdr + 0x18) + 4) + (s32) (gBufferIndex ? gShuttleMtxA : gShuttleMtxB)));
    gModelRot[0] = ((u16) gShuttleRotX);
    *(u16 *) (gShuttleState + 0x4C) = ((u16) gShuttleRotY);
    gShuttleHeading = ((u16) gShuttleRotY);
    gModelRot[1] = ((u16) gShuttleRotY);
    gModelRot[2] = ((u16) gShuttleRotZ);
    mtx_build_model(gShuttlePos, gShuttlePosY, gShuttlePosZ, 0x11558, m);
    base = gBufferIndex ? gShuttleMtxA : gShuttleMtxB;
    hdr = gShuttleModel;
    regs.v1 = 0;
    regs.a0 = 0;
    regs.a3 = 0;
    regs.s1 = 0;
    regs.s2 = (s32) m;
    regs.s0 = 0;
    veh_points_transform(0xFD, (s16 *) (hdr + *(s32 *) hdr), (s16 *) (hdr + *(s32 *) (hdr + 4)), base, &regs);
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/8DDB0/shuttle_build_model.s")
#endif

#ifdef NON_MATCHING
/* The tables of the 8E910 bin (ROM 0x8E910-0x8F860, 0x802D30D0-0x802D4020),
 * right after this file's code: the per-level part lists picked by
 * lvl_pick_path_parts (5CB60.c) for 20 levels (s32 count + LevelPart records; the
 * entry words are filled in at level load by lvl_add_object).
 * The NON_MATCHING build defines it in C and links this file's .data (it has
 * nothing else) back at the table's original address (Makefile NM_PIN_HD_CODE,
 * nm_ldscript.py --pin-object); nm_symaudit.py checks the bytes against the
 * bin. Code reaches it through the absolute D_ symbols. */
typedef struct {
    /* 802D30D0 */ s32 lp802D30D0; /* gPathPartsLvl00: count */
    /* 802D30D4 */ LevelPart lp802D30D0_parts[8];
    /* 802D3194 */ s32 lp802D3194; /* gPathPartsLvl01: count */
    /* 802D3198 */ LevelPart lp802D3194_parts[11];
    /* 802D32A0 */ s32 lp802D32A0; /* gPathPartsLvl02: count */
    /* 802D32A4 */ LevelPart lp802D32A0_parts[5];
    /* 802D331C */ s32 lp802D331C; /* gPathPartsLvl03: count */
    /* 802D3320 */ LevelPart lp802D331C_parts[7];
    /* 802D33C8 */ s32 lp802D33C8; /* gPathPartsLvl04: count */
    /* 802D33CC */ LevelPart lp802D33C8_parts[5];
    /* 802D3444 */ s32 lp802D3444; /* gPathPartsLvl05: count */
    /* 802D3448 */ LevelPart lp802D3444_parts[10];
    /* 802D3538 */ s32 lp802D3538; /* gPathPartsLvl09: count */
    /* 802D353C */ LevelPart lp802D3538_parts[9];
    /* 802D3614 */ s32 lp802D3614; /* gPathPartsLvl0A: count */
    /* 802D3618 */ LevelPart lp802D3614_parts[7];
    /* 802D36C0 */ s32 lp802D36C0; /* gPathPartsLvl0C: count */
    /* 802D36C4 */ LevelPart lp802D36C0_parts[8];
    /* 802D3784 */ s32 lp802D3784; /* gPathPartsLvl0D: count */
    /* 802D3788 */ LevelPart lp802D3784_parts[11];
    /* 802D3890 */ s32 lp802D3890; /* gPathPartsLvl0E: count */
    /* 802D3894 */ LevelPart lp802D3890_parts[7];
    /* 802D393C */ s32 lp802D393C; /* gPathPartsLvl0F: count */
    /* 802D3940 */ LevelPart lp802D393C_parts[8];
    /* 802D3A00 */ s32 lp802D3A00; /* gPathPartsLvl10: count */
    /* 802D3A04 */ LevelPart lp802D3A00_parts[3];
    /* 802D3A4C */ s32 lp802D3A4C; /* gPathPartsLvl11: count */
    /* 802D3A50 */ LevelPart lp802D3A4C_parts[14];
    /* 802D3BA0 */ s32 lp802D3BA0; /* gPathPartsLvl12: count */
    /* 802D3BA4 */ LevelPart lp802D3BA0_parts[2];
    /* 802D3BD4 */ s32 lp802D3BD4; /* gPathPartsLvl1A: count */
    /* 802D3BD8 */ LevelPart lp802D3BD4_parts[11];
    /* 802D3CE0 */ s32 lp802D3CE0; /* gPathPartsLvl1D: count */
    /* 802D3CE4 */ LevelPart lp802D3CE0_parts[10];
    /* 802D3DD4 */ s32 lp802D3DD4; /* gPathPartsLvl21: count */
    /* 802D3DD8 */ LevelPart lp802D3DD4_parts[9];
    /* 802D3EB0 */ s32 lp802D3EB0; /* gPathPartsLvl39: count */
    /* 802D3EB4 */ LevelPart lp802D3EB0_parts[8];
    /* 802D3F74 */ s32 lp802D3F74; /* gPathPartsLvl3A: count */
    /* 802D3F78 */ LevelPart lp802D3F74_parts[7];
} TextData8E910; /* size 0xF50 */
/* the members must sit at their ROM offsets (with 4-byte pointers) */
typedef char TextData8E910_size_check[(sizeof(void *) != 4 || sizeof(TextData8E910) == 0xF50) ? 1 : -1];

TextData8E910 textdata_8E910 = {
    8,
    {
        { NULL, 4, { 3, 2, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 } },
        { NULL, 9, { 3, 7, 2, 6, 0, 4, 8, 1, 5, 0, 0, 0, 0, 0, 0, 0 } },
        { NULL, 11, { 3, 9, 5, 10, 2, 8, 0, 7, 6, 11, 1, 0, 0, 0, 0, 0 } },
        { NULL, 3, { 1, 2, 3, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 } },
        { NULL, 8, { 2, 6, 3, 7, 0, 1, 4, 5, 0, 0, 0, 0, 0, 0, 0, 0 } },
        { NULL, 4, { 0, 6, 3, 7, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 } },
        { NULL, 8, { 0, 2, 4, 6, 1, 5, 3, 7, 0, 0, 0, 0, 0, 0, 0, 0 } },
        { NULL, 1, { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 } },
    },
    11,
    {
        { NULL, 3, { 1, 0, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 } },
        { NULL, 2, { 1, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 } },
        { NULL, 5, { 4, 5, 6, 2, 3, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 } },
        { NULL, 12, { 0, 6, 3, 9, 1, 7, 4, 10, 8, 2, 5, 11, 0, 0, 0, 0 } },
        { NULL, 8, { 0, 4, 2, 6, 1, 5, 3, 7, 0, 0, 0, 0, 0, 0, 0, 0 } },
        { NULL, 5, { 0, 6, 1, 2, 8, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 } },
        { NULL, 1, { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 } },
        { NULL, 4, { 0, 2, 1, 3, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 } },
        { NULL, 6, { 0, 3, 1, 4, 2, 5, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 } },
        { NULL, 6, { 0, 3, 1, 4, 2, 5, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 } },
        { NULL, 4, { 3, 7, 2, 6, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 } },
    },
    5,
    {
        { NULL, 2, { 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 } },
        { NULL, 3, { 0, 3, 6, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 } },
        { NULL, 8, { 3, 9, 5, 10, 2, 8, 6, 11, 0, 0, 0, 0, 0, 0, 0, 0 } },
        { NULL, 10, { 0, 6, 9, 3, 1, 7, 8, 2, 11, 5, 0, 0, 0, 0, 0, 0 } },
        { NULL, 6, { 6, 9, 7, 10, 8, 11, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 } },
    },
    7,
    {
        { NULL, 3, { 2, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 } },
        { NULL, 5, { 3, 7, 4, 2, 8, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 } },
        { NULL, 4, { 1, 2, 4, 5, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 } },
        { NULL, 8, { 0, 2, 4, 6, 1, 3, 5, 7, 0, 0, 0, 0, 0, 0, 0, 0 } },
        { NULL, 5, { 4, 2, 5, 3, 6, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 } },
        { NULL, 6, { 0, 4, 2, 1, 5, 3, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 } },
        { NULL, 12, { 0, 3, 6, 1, 4, 7, 9, 2, 5, 8, 10, 11, 0, 0, 0, 0 } },
    },
    5,
    {
        { NULL, 3, { 1, 2, 3, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 } },
        { NULL, 4, { 2, 6, 3, 7, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 } },
        { NULL, 8, { 0, 4, 2, 6, 1, 5, 3, 7, 0, 0, 0, 0, 0, 0, 0, 0 } },
        { NULL, 3, { 0, 1, 4, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 } },
        { NULL, 9, { 8, 2, 0, 4, 6, 1, 3, 5, 7, 0, 0, 0, 0, 0, 0, 0 } },
    },
    10,
    {
        { NULL, 5, { 4, 2, 0, 1, 3, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 } },
        { NULL, 8, { 2, 6, 0, 4, 3, 7, 1, 5, 0, 0, 0, 0, 0, 0, 0, 0 } },
        { NULL, 9, { 0, 6, 3, 1, 7, 4, 2, 5, 8, 0, 0, 0, 0, 0, 0, 0 } },
        { NULL, 9, { 0, 3, 6, 1, 4, 7, 5, 8, 2, 0, 0, 0, 0, 0, 0, 0 } },
        { NULL, 8, { 0, 4, 1, 5, 3, 2, 7, 6, 0, 0, 0, 0, 0, 0, 0, 0 } },
        { NULL, 6, { 0, 4, 1, 5, 3, 7, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 } },
        { NULL, 1, { 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 } },
        { NULL, 1, { 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 } },
        { NULL, 6, { 0, 4, 1, 5, 3, 7, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 } },
        { NULL, 12, { 6, 9, 0, 3, 7, 10, 1, 4, 8, 11, 2, 5, 0, 0, 0, 0 } },
    },
    9,
    {
        { NULL, 2, { 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 } },
        { NULL, 2, { 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 } },
        { NULL, 6, { 6, 9, 7, 10, 8, 11, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 } },
        { NULL, 5, { 0, 7, 2, 8, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 } },
        { NULL, 2, { 1, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 } },
        { NULL, 2, { 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 } },
        { NULL, 2, { 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 } },
        { NULL, 2, { 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 } },
        { NULL, 6, { 6, 9, 7, 10, 8, 11, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 } },
    },
    7,
    {
        { NULL, 2, { 1, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 } },
        { NULL, 5, { 4, 5, 6, 2, 3, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 } },
        { NULL, 12, { 0, 3, 6, 9, 1, 4, 7, 10, 2, 5, 8, 11, 0, 0, 0, 0 } },
        { NULL, 2, { 1, 5, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 } },
        { NULL, 4, { 0, 1, 2, 3, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 } },
        { NULL, 4, { 0, 1, 2, 3, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 } },
        { NULL, 5, { 0, 6, 1, 2, 8, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 } },
    },
    8,
    {
        { NULL, 10, { 0, 5, 1, 6, 2, 7, 3, 8, 4, 9, 0, 0, 0, 0, 0, 0 } },
        { NULL, 10, { 0, 5, 1, 6, 2, 7, 3, 8, 4, 9, 0, 0, 0, 0, 0, 0 } },
        { NULL, 10, { 0, 5, 1, 6, 2, 7, 3, 8, 4, 9, 0, 0, 0, 0, 0, 0 } },
        { NULL, 10, { 0, 5, 1, 6, 2, 7, 3, 8, 4, 9, 0, 0, 0, 0, 0, 0 } },
        { NULL, 10, { 0, 5, 1, 6, 2, 7, 3, 8, 4, 9, 0, 0, 0, 0, 0, 0 } },
        { NULL, 1, { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 } },
        { NULL, 1, { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 } },
        { NULL, 1, { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 } },
    },
    11,
    {
        { NULL, 1, { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 } },
        { NULL, 1, { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 } },
        { NULL, 1, { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 } },
        { NULL, 1, { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 } },
        { NULL, 1, { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 } },
        { NULL, 1, { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 } },
        { NULL, 1, { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 } },
        { NULL, 1, { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 } },
        { NULL, 1, { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 } },
        { NULL, 1, { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 } },
        { NULL, 1, { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 } },
    },
    7,
    {
        { NULL, 5, { 0, 9, 1, 2, 10, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 } },
        { NULL, 5, { 0, 9, 1, 2, 10, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 } },
        { NULL, 5, { 0, 9, 1, 2, 10, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 } },
        { NULL, 8, { 3, 1, 5, 9, 8, 4, 2, 6, 0, 0, 0, 0, 0, 0, 0, 0 } },
        { NULL, 3, { 2, 5, 8, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 } },
        { NULL, 1, { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 } },
        { NULL, 7, { 6, 5, 4, 3, 2, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 } },
    },
    8,
    {
        { NULL, 4, { 0, 1, 2, 3, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 } },
        { NULL, 4, { 0, 1, 2, 3, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 } },
        { NULL, 4, { 0, 1, 2, 3, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 } },
        { NULL, 2, { 2, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 } },
        { NULL, 2, { 2, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 } },
        { NULL, 1, { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 } },
        { NULL, 6, { 0, 1, 3, 4, 6, 7, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 } },
        { NULL, 4, { 0, 1, 2, 3, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 } },
    },
    3,
    {
        { NULL, 3, { 3, 4, 5, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 } },
        { NULL, 2, { 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 } },
        { NULL, 3, { 0, 1, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 } },
    },
    14,
    {
        { NULL, 2, { 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 } },
        { NULL, 2, { 1, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 } },
        { NULL, 2, { 1, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 } },
        { NULL, 4, { 4, 5, 6, 7, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 } },
        { NULL, 2, { 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 } },
        { NULL, 2, { 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 } },
        { NULL, 2, { 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 } },
        { NULL, 4, { 2, 3, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 } },
        { NULL, 1, { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 } },
        { NULL, 2, { 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 } },
        { NULL, 2, { 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 } },
        { NULL, 1, { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 } },
        { NULL, 1, { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 } },
        { NULL, 2, { 1, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 } },
    },
    2,
    {
        { NULL, 6, { 2, 3, 0, 4, 1, 5, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 } },
        { NULL, 1, { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 } },
    },
    11,
    {
        { NULL, 3, { 1, 0, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 } },
        { NULL, 2, { 1, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 } },
        { NULL, 5, { 4, 5, 6, 2, 3, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 } },
        { NULL, 12, { 0, 6, 3, 9, 7, 1, 4, 10, 2, 8, 5, 11, 0, 0, 0, 0 } },
        { NULL, 8, { 0, 4, 2, 6, 1, 5, 3, 7, 0, 0, 0, 0, 0, 0, 0, 0 } },
        { NULL, 6, { 0, 6, 1, 7, 2, 8, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 } },
        { NULL, 1, { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 } },
        { NULL, 4, { 0, 2, 1, 3, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 } },
        { NULL, 6, { 0, 3, 1, 4, 2, 5, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 } },
        { NULL, 6, { 0, 3, 1, 4, 2, 5, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 } },
        { NULL, 4, { 3, 7, 2, 6, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 } },
    },
    10,
    {
        { NULL, 1, { 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 } },
        { NULL, 1, { 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 } },
        { NULL, 6, { 6, 9, 7, 10, 8, 11, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 } },
        { NULL, 5, { 0, 7, 2, 8, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 } },
        { NULL, 2, { 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 } },
        { NULL, 2, { 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 } },
        { NULL, 2, { 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 } },
        { NULL, 10, { 0, 6, 3, 9, 1, 7, 2, 8, 5, 11, 0, 0, 0, 0, 0, 0 } },
        { NULL, 2, { 8, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 } },
        { NULL, 2, { 6, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 } },
    },
    9,
    {
        { NULL, 3, { 4, 5, 6, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 } },
        { NULL, 6, { 0, 1, 2, 3, 4, 5, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 } },
        { NULL, 1, { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 } },
        { NULL, 6, { 6, 0, 7, 1, 8, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 } },
        { NULL, 4, { 0, 2, 1, 3, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 } },
        { NULL, 4, { 1, 4, 0, 3, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 } },
        { NULL, 8, { 3, 7, 1, 5, 2, 6, 0, 4, 0, 0, 0, 0, 0, 0, 0, 0 } },
        { NULL, 4, { 2, 6, 3, 7, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 } },
        { NULL, 6, { 6, 7, 9, 10, 8, 11, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 } },
    },
    8,
    {
        { NULL, 4, { 3, 2, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 } },
        { NULL, 9, { 3, 7, 2, 6, 0, 4, 8, 1, 5, 0, 0, 0, 0, 0, 0, 0 } },
        { NULL, 11, { 3, 9, 5, 10, 2, 8, 0, 7, 6, 11, 1, 0, 0, 0, 0, 0 } },
        { NULL, 3, { 1, 2, 3, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 } },
        { NULL, 8, { 2, 6, 3, 7, 0, 1, 4, 5, 0, 0, 0, 0, 0, 0, 0, 0 } },
        { NULL, 4, { 0, 6, 3, 7, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 } },
        { NULL, 8, { 0, 2, 4, 6, 1, 5, 3, 7, 0, 0, 0, 0, 0, 0, 0, 0 } },
        { NULL, 1, { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 } },
    },
    7,
    {
        { NULL, 3, { 0, 3, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 } },
        { NULL, 4, { 5, 4, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 } },
        { NULL, 4, { 0, 6, 2, 8, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 } },
        { NULL, 2, { 4, 5, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 } },
        { NULL, 2, { 4, 5, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 } },
        { NULL, 2, { 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 } },
        { NULL, 3, { 6, 7, 8, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 } },
    },
};
#endif
