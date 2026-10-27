#include "common.h"
#include <ultra64.h>
#include "game/game.h"
/* Views: this file reads these shared variables (game/variables.h) as other types. */
#define gObjSolidKinds ((Unk803059F0 *) gObjSolidKinds)
#define gBoxTargets ((BoxTarget *) gBoxTargets)
#define gVehPartSpheres ((Unk803A6B30 *) gVehPartSpheres)
#define gCollTris ((CollisionTri *) gCollTris)
#define gSinkingPart ((SinkingPart *) gSinkingPart)
#define gEffectScratch (*(EffectSpawn *) gEffectScratch)
#define gObjects ((LevelObject *) gObjects)
#ifdef NON_MATCHING
#define gObjectsEnd (*(LevelObject * N64P *) &gObjectsEnd)
#define gGroupTris (*(s32 * N64P *) &gGroupTris)
#endif
/* end of views */


#ifdef NON_MATCHING
/* Types, tables and helpers shared by the port-phase rewrites in this file.
 *
 * Several rewrites below call hand-written helpers that pass arguments and
 * results in t-/s-registers. Every such helper now has a C rewrite (and a
 * tools_port/conventions.txt line), so it is called normally through the
 * prototypes below: math_dist3 (62740), veh_tri_is_solid (56040),
 * mtx_translate and mtx_to_n64 (679E0), and this file's own helpers. */

/* ObjectModel, LevelObject, CollisionTri, LevelObjectHead: game/object.h */


/* (gBoxTargets: BoxTarget, game/types.h.) */


typedef struct {
    /* 0x00 */ s32 pos[3];
    /* 0x0C */ s8 ids[9]; /* -1 terminated (the scan isn't bounded), looked up in gCollTris */
    /* 0x15 */ s8 level;  /* -1 ends the table */
    /* 0x16 */ u8 unk16[0x1A];
} TriTarget; /* size 0x30 */

extern TriTarget gTriTargets[];




/* Callees in other files (C rewrites; see conventions.txt for their asm registers). */

/* Rewritten in this file (see conventions.txt for their asm registers). */
s32 obj_tri_targets_ahead_done(void);
s32 obj_box_targets_done(void);
s32 obj_parts_destroyed(LevelObject *next);
s32 obj_rand_range(s32 lo, s32 hi);
void obj_start_emitter(LevelObject *e, s32 *obj);
void obj_set_sink_quad(u8 *base, LevelObject *e);

/* The 0x38-byte debris record that obj_spawn_random_effects, obj_spawn_random_landing_effect (and 8A2E0's
 * commpt_burst) fill in and hand to obj_queue_effect. */
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
} EffectSpawn;


/* cvt.w.s under the default FCSR: round to nearest, ties to even (a C cast
 * truncates). */
#define CVT_W_S(out, x)                                       \
    {                                                         \
        f32 _x = (x);                                         \
        s32 _r = (s32) _x;                                    \
        f32 _d = _x - _r;                                     \
                                                              \
        if (_d > 0.5f || (_d == 0.5f && (_r & 1))) {          \
            _r++;                                             \
        } else if (_d < -0.5f || (_d == -0.5f && (_r & 1))) { \
            _r--;                                             \
        }                                                     \
        (out) = _r;                                           \
    }

typedef struct {
    /* 0x00 */ s16 next;  /* chain offset of the next group; -1 = last group */
    /* 0x02 */ u8 pad2[0x12];
    /* 0x14 */ u16 mtxOffset[10]; /* 1-based part index -> offset into the matrix buffer */
    /* 0x28 */ s32 count; /* records following this header */
} Unk802C2054Group;       /* size 0x2C */

typedef struct {
    /* 0x00 */ u16 index[9];
    /* 0x12 */ u16 pad12;
    struct {
        /* 0x00 */ s32 mtxOff;  /* matrix offset this part follows */
        /* 0x04 */ s32 base[3];
    } /* 0x14 */ part[3];
} Unk802C2054Record;      /* size 0x44 */
#endif

/* FILE-WIDE FINDING: every one of this file's 68 functions saves $ra (and
 * any other preserved registers) via the 64-bit `sd`/`ld` doubleword
 * instructions, never the normal 32-bit `sw`/`lw` pair IDO emits for every
 * o32 call frame (confirmed mechanically: grep for the first `*ra,` save in
 * each of the 68 .s files here - 68/68 are `sd`, 0/68 are `sw`). Per this
 * project's probe-confirmed research (see the hd_code "phantom dead frame"
 * notes in the project skill file), the `sd $ra`/`ld $ra` doubleword save
 * never comes out of this compiler for ANY real C, including genuine
 * function calls - so this signature, applied file-wide with no exceptions,
 * means the entire file is hand-written MIPS assembly, not reachable from C
 * at all. Spot-checked across four different internal shapes (a trivial
 * single-global-write stub, a ~30-instruction loop, a full all-registers
 * save/restore wrapper preserving even $sp/$fp/$k0/$k1 as data, and a
 * pointer-chain walk with a real call inside that still uses `sd $ra`) -
 * all confirm the same convention regardless of internal complexity. 33 of
 * the 68 additionally follow a "preserve caller registers across scratch
 * reuse" sub-convention (see the block comment above obj_clear_delayed_damage below);
 * the rest don't need that extra layer to already be unreachable from C on
 * frame-shape grounds alone. Every pragma in this file is permanently
 * GLOBAL_ASM; don't attempt a C translation for any of them. */

/* Uses the sd-$ra frame convention - see the file-level note at the top of this file. Permanently GLOBAL_ASM. */
#ifdef NON_MATCHING

/* The end object touched and the objectives met: the level is done (0x802BC5E0).
 * If gEndObjectTouched is set, clears it and, unless gHaveCarrier or gHaveShuttle is set
 * and the level isn't finished yet, marks it finished (gLevelDoneRequest and
 * gPauseRequest = 1). "Finished" means gPathBuildingsDestroyed is set, every gBoxTargets entry
 * has filled set (obj_box_targets_done) and no gTriTargets entry of the current level
 * at or beyond the player's z has an id whose gCollTris isSolid is set
 * (obj_tri_targets_ahead_done). Then, if gHaveCarrier is clear: in game mode 0x40 it calls
 * fade_to_mode(0x40), otherwise it sets gDriverHidden = 1 and the next mode 8.
 * Declared void in hd.c; the asm leaves garbage in v0. */
void obj_check_level_end(void) {
    if (gEndObjectTouched == 0) {
        return;
    }
    gEndObjectTouched = 0;
    if (gHaveCarrier != 0 || gHaveShuttle != 0) {
        if (gPathBuildingsDestroyed == 0) {
            return;
        }
        if (obj_box_targets_done() == 0) {
            return;
        }
        if (obj_tri_targets_ahead_done() == 0) {
            return;
        }
    }
    gLevelDoneRequest = 1;
    gPauseRequest = 1;
    if (gHaveCarrier != 0) {
        return;
    }
    if (gGameMode == MODE_REPLAY) {
        fade_to_mode(MODE_REPLAY);
    } else {
        /* The asm stores 1 with sb into the s32 gDriverHidden, i.e. into its
         * most significant byte (readers only test it against 0). Done on
         * the word so either byte order gets the same value. */
        gDriverHidden = (gDriverHidden & 0x00FFFFFF) | 0x01000000;
        gNextGameMode = MODE_LEVEL_CLEAR;
    }
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/77E20/obj_check_level_end.s")
#endif

/* Uses the sd-$ra frame convention - see the file-level note at the top of this file. Permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
/* 0 if a gTriTargets target at or past the carrier's z still stands (0x802BC714).
 * Returns 0 if some gTriTargets entry of the current level whose z is at or
 * beyond the player's (gCarrierZ, read once) lists an id (s8, sign-extended;
 * the list ends at -1) whose gCollTris isSolid is set (veh_tri_is_solid), else 1.
 * The table ends at a level of -1. The asm returns in t4 and saves t0, t1, t3,
 * t5, s0 and s6; asm caller obj_step_tex_anims keeps a0-a3, t0-t2, f12 and f14
 * live across the call. */
s32 obj_tri_targets_ahead_done(void) {
    s32 z = gCarrierZ;
    TriTarget *e;
    s8 *id;

    for (e = gTriTargets; e->level != -1; e++) {
        if (e->level != levelno || e->pos[2] < z) {
            continue;
        }
        for (id = e->ids; *id != -1; id++) {
            if (veh_tri_is_solid(*id) != 0) {
                return 0;
            }
        }
    }
    return 1;
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/77E20/obj_tri_targets_ahead_done.s")
#endif

/* Uses the sd-$ra frame convention - see the file-level note at the top of this file. Permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
/* 1 if every gBoxTargets box target is done (filled) (0x802BC7DC).
 * Returns 1 if every gBoxTargets entry has filled set, else 0. The asm returns
 * it in t4 and saves t0, t1 and t3. Asm caller obj_step_tex_anims keeps a0-a3,
 * t0-t2, f12 and f14 live across the call. */
s32 obj_box_targets_done(void) {
    s32 i;
    BoxTarget *t;

    for (i = gBoxTargetCount, t = gBoxTargets; i != 0; i--, t++) {
        if (t->filled == 0) {
            return 0;
        }
    }
    return 1;
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/77E20/obj_box_targets_done.s")
#endif

/* Confirmed hand-written assembly: a "preserve caller registers via explicit
 * stack save/restore" convention, distinct from every other hand-written-asm
 * family documented elsewhere in this project. Each function in this family
 * (33 of this file's 68) opens by `sd`-saving some subset of registers
 * (always including $v0/$v1; the largest, obj_add_delayed_damage, saves literally
 * every register in the file including $at/$gp/$sp/$fp/$k0/$k1 as raw data)
 * to its own stack frame, is then free to clobber those same register
 * numbers as ordinary scratch for its real body, and `ld`-restores every
 * saved slot byte-for-byte before returning - the restored value is always
 * exactly what was saved, never read or influenced by the body in between.
 * No compiler emits this: IDO never saves/restores a register it doesn't
 * believe is a live local, and no C semantics could want $sp itself
 * round-tripped through memory as inert data (seen in obj_add_delayed_damage). This
 * is a deliberate low-level convention - likely a shared boilerplate/macro
 * the original engine used for a family of dispatch-table callback/handler
 * functions that must leave the caller's register state untouched regardless
 * of their own internal register needs. Permanently GLOBAL_ASM; don't
 * attempt a C translation for any function flagged with this comment.
 * (Port phase: functions of this family with an `#ifdef NON_MATCHING` C
 * rewrite below are functional, non-matching equivalents for the native port.) */
#ifdef NON_MATCHING
extern u8 gDelayedDamage[40][8];

/* The 40 gDelayedDamage slots' timers to 0 (0x802BC840).
 * Clears byte 7 of each of the 40 8-byte entries of gDelayedDamage.
 * Register note: the asm saves/restores v0 and v1; asm caller initlevel
 * keeps t0, f12 and f14 live across the call (a mixed N64 build would need a
 * thunk; the native port doesn't). */
void obj_clear_delayed_damage(void) {
    s32 i;

    for (i = 0; i < 40; i++) {
        gDelayedDamage[i][7] = 0;
    }
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/77E20/obj_clear_delayed_damage.s")
#endif

/* Same preserve-caller-registers convention as obj_clear_delayed_damage above - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING

/* (part, object, amount) into a slot with a random 1..20 frame delay (0x802BC888).
 * gDelayedDamage holds 40 8-byte counters {s32 id; u16 amount; u8 kind; u8 timer},
 * a nonzero timer marking a slot in use. Adds `amount` to the used slot with
 * this id and kind; if there is none, fills the first free slot with id, kind
 * and amount and a timer of util_rand2_range(1, 20). Does nothing when all 40 are
 * in use. kind is compared with the byte as a full word.
 * The asm takes kind in t3, id in t9 and amount in a2 and preserves every
 * register (it saves all of them, $sp included); asm caller obj_blast_damage
 * keeps a0, a1, t3, t4, t6, t7 and t9 live across the call. */
void obj_add_delayed_damage(s32 kind, s32 id, s32 amount) {
    u8 *e;
    s32 i;

    for (i = 40, e = gDelayedDamage[0]; i != 0; i--, e += 8) {
        if (e[7] != 0 && *(s32 *) e == id && e[6] == kind) {
            *(u16 *) (e + 4) += amount;
            return;
        }
    }
    for (i = 40, e = gDelayedDamage[0]; i != 0; i--, e += 8) {
        if (e[7] == 0) {
            e[6] = kind;
            *(u16 *) (e + 4) = amount;
            *(s32 *) e = id;
            e[7] = util_rand2_range(1, 20);
            return;
        }
    }
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/77E20/obj_add_delayed_damage.s")
#endif

/* Uses the sd-$ra frame convention - see the file-level note at the top of this file. Permanently GLOBAL_ASM. */
#ifdef NON_MATCHING

/* Counts the delayed-damage slots down; applies them, collapses parts (0x802BCA2C).
 * Delayed damage: for each of the 40 gDelayedDamage entries {object, u16 amount,
 * u8 part, u8 timer} with a running timer, counts it down; on reaching 0 the
 * entry is spent (timer 0), the screen shakes (10, min(amount, 500)) and the
 * part's percentage += amount, capped at 100. A part reaching 100 collapses:
 * obj_start_topple, obj_start_part_fade unless gCarrierHit or gLevelDone is set,
 * obj_start_sinking_part, obj_spawn_people unless the game mode gGameMode is 0x200 or
 * 0x400, and the part's triangles off. Then obj_drop_unsupported_parts, obj_check_destroyed and
 * obj_spawn_break_effects(e, part, percentage).
 * ABI (00000.c calls it as void(void)); the asm saves s0-s7, gp, fp; the s1
 * the survey lists as an input is only saved. */
void obj_update_delayed_damage(void) {
    u8 *p = gDelayedDamage[0];
    s32 n;

    for (n = 0x28; n != 0; n--, p += 8) {
        LevelObject *e;
        s32 timer = p[7];
        s32 amount;
        s32 part;
        s32 level;
        u8 *pct;

        if (timer == 0) {
            continue;
        }
        timer--;
        if (timer != 0) {
            p[7] = timer;
            continue;
        }
        amount = *(u16 *) (p + 4);
        p[7] = 0;
        e = *(LevelObject * N64P *) p;
        part = p[6];
        gShakeTime = 10;
        if (amount < 0x1F5) {
            gShakeSize = amount;
        } else {
            gShakeSize = 500;
        }
        pct = (u8 *) e + 0xEC + part - 1;
        level = *pct + amount;
        if (level >= 100) {
            level = 100;
        }
        *pct = level;
        if (level == 100) {
            CollisionTri *t;
            CollisionTri *end;

            obj_start_topple(e, part);
            if (gCarrierHit == 0 && gLevelDone == 0) {
                obj_start_part_fade(part, e);
            }
            obj_start_sinking_part(part, e);
            if (gGameMode != MODE_CARRIER_END && gGameMode != MODE_REPLAY_CARRIER_END) {
                obj_spawn_people(e);
            }
            end = e->trisEnd;
            for (t = e->tris; t != end; t++) {
                if (t->partNo == part) {
                    t->isSolid = 0;
                }
            }
        }
        obj_drop_unsupported_parts(e);
        obj_check_destroyed(e);
        obj_spawn_break_effects(e, part, level);
    }
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/77E20/obj_update_delayed_damage.s")
#endif

/* Same preserve-caller-registers convention as obj_clear_delayed_damage above - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
/* Two small byte buffers, each with a write pointer stored just before it:
 * buffer A is gContacts (write pointer gContactsEnd), buffer B is gPrevContacts
 * (write pointer gPrevContactsEnd). Used by veh_contacts_clear/802BCC10/802BCC48. */
extern u8 * N64P gContactsEnd;
extern u8 gContacts[];
extern u8 * N64P gPrevContactsEnd;
extern u8 gPrevContacts[];

/* Empties gContacts (the vehicle's hit spheres touching this frame) (0x802BCBD8).
 * Resets buffer A: its write pointer goes back to the buffer start.
 * Register note: the asm saves/restores v0 and v1 and touches nothing else. */
void veh_contacts_clear(void) {
    gContactsEnd = gContacts;
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/77E20/veh_contacts_clear.s")
#endif

/* Same preserve-caller-registers convention as obj_clear_delayed_damage above - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
/* Empties gPrevContacts (0x802BCC10).
 * Resets buffer B: its write pointer goes back to the buffer start.
 * Register note: the asm saves/restores v0 and v1; asm callers keep
 * registers live across the call (veh_collision_setup: a2, t8; ramdozer_update: a0,
 * a3, t6, f12, f14), so a mixed N64 build would need a thunk. */
void veh_prev_contacts_clear(void) {
    gPrevContactsEnd = gPrevContacts;
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/77E20/veh_prev_contacts_clear.s")
#endif

/* Same preserve-caller-registers convention as obj_clear_delayed_damage above - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING

/* GContacts into gPrevContacts (empty on a mode's first frame) (0x802BCC48).
 * If gFramesInMode is set, copies buffer A (gContacts up to its write pointer)
 * into buffer B byte by byte, front to back; then sets B's write pointer to
 * just past the copied bytes (to B's start when nothing was copied). The asm
 * loops with `!=`, so A's write pointer must not be below gContacts.
 * Register note: the asm saves/restores v0, v1, a0 and a1; asm caller
 * obj_collide_vehicle keeps a0, a1, t6, f12 and f14 live across the call. */
void veh_contacts_save(void) {
    u8 *dst = gPrevContacts;

    if (((s32) gFramesInMode) != 0) {
        u8 *src = gContacts;
        u8 *end = gContactsEnd;

        while (src != end) {
            *dst++ = *src++;
        }
    }
    gPrevContactsEnd = dst;
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/77E20/veh_contacts_save.s")
#endif

/* Same preserve-caller-registers convention as obj_clear_delayed_damage above - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
/* Appends a hit-sphere number to gContacts unless listed (0x802BCCD4).
 * Appends value (its low byte) to buffer A unless buffer A already contains
 * it (veh_contacts_has). The asm takes value in fp (a value, not a base pointer)
 * and saves v0 and v1, so it changes nothing; asm callers keep many registers
 * live across the call (veh_parts_touch_sphere: a2-t1, t3-t8, f12, f14; veh_scale_other_radius:
 * a0-t1, t8, f12, f14; obj_hit_tri: a0, a1, t3, t8, t9, f12, f14). The
 * asm's `addi` on the write pointer can't trap for a RAM address. */
void veh_contacts_add(s32 value) {
    if (veh_contacts_has(value) == 0) {
        *gContactsEnd++ = value;
    }
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/77E20/veh_contacts_add.s")
#endif

/* Uses the sd-$ra frame convention - see the file-level note at the top of this file. Permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
/* Returns 1 if some entry of gContacts differs from value, else 0 (veh_contacts_has's twin); nothing calls it
 * (0x802BCD20).
 * Returns 1 if some byte of buffer A (gContacts up to its write pointer)
 * differs from value (compared as a full word), else 0. The asm takes value
 * in v0 and returns in v1 (it has no callers); it saves a0, a1 and a2. The
 * scan stops at the write pointer with `!=`, which must not be below the
 * buffer start. */
s32 veh_contacts_has_other(s32 value) {
    u8 *p = gContacts;
    u8 *end = gContactsEnd;

    while (p != end) {
        if (*p++ != value) {
            return 1;
        }
    }
    return 0;
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/77E20/veh_contacts_has_other.s")
#endif

/* Uses the sd-$ra frame convention - see the file-level note at the top of this file. Permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
/* 1 if gContacts lists the hit-sphere number (0x802BCD80).
 * Returns 1 if buffer A (gContacts up to its write pointer) contains value
 * (compared as a full word), else 0. The asm takes value in v0, returns in v1
 * and saves a0, a1 and a2; asm callers keep a3 (sideswipe_check_ram_hit) or a0, a3,
 * f12 and f14 (train_update) live across the call. */
s32 veh_contacts_has(s32 value) {
    u8 *p = gContacts;
    u8 *end = gContactsEnd;

    while (p != end) {
        if (*p++ == value) {
            return 1;
        }
    }
    return 0;
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/77E20/veh_contacts_has.s")
#endif

/* Uses the sd-$ra frame convention - see the file-level note at the top of this file. Permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
/* 1 if gPrevContacts lists the hit-sphere number (0x802BCDE0).
 * veh_contacts_has for buffer B (gPrevContacts up to its write pointer gPrevContactsEnd).
 * The asm takes value in fp (as a value, not a base pointer), returns in v1
 * and saves a0, a1 and a2; asm caller obj_hit_tri keeps a0, a1, t3, t8, t9,
 * f12 and f14 live across the call. */
s32 veh_prev_contacts_has(s32 value) {
    u8 *p = gPrevContacts;
    u8 *end = gPrevContactsEnd;

    while (p != end) {
        if (*p++ == value) {
            return 1;
        }
    }
    return 0;
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/77E20/veh_prev_contacts_has.s")
#endif

/* Uses the sd-$ra frame convention - see the file-level note at the top of this file. Permanently GLOBAL_ASM. */
#ifdef NON_MATCHING

/* Finds the closest target to the point gPlayerX/Y/Z, starting from a
 * distance limit of 9999999, over three tables (a later one wins only when
 * strictly closer):
 *   1: gObjects entries with pathBuilding set, destroyed clear and not yet done
 *      (obj_parts_destroyed);
 *   2: gBoxTargets entries with filled clear;
 *   3: gTriTargets entries of the current level with an id whose gCollTris
 *      isSolid is set.
 * Stores the kind (0 = none) in gNearestTargetKind and a pointer for it in gNearestTargetPts
 * (NULL; info + info->markerPts; &entry + 0xC; &entry + 0x16), and returns the
 * entry (0 if none). The asm loops over gObjects with `!=`, so gObjectsEnd
 * must be gObjects + n entries. */
s32 obj_find_nearest_target(void) {
    LevelObject *end = gObjectsEnd;
    LevelObject *e;
    BoxTarget *t;
    TriTarget *p;
    s8 *id;
    void *best = NULL;
    s32 bestDist = 9999999;
    s32 kind = 0;
    s32 dist;
    s32 i;
    u8 *ptr;

    for (e = gObjects; e != end; e++) {
        if (e->pathBuilding == 0 || e->destroyed != 0) {
            continue;
        }
        dist = math_dist3(e->pos[0], e->pos[1], e->pos[2], gPlayerX, gPlayerY, gPlayerZ);
        if (dist >= bestDist) {
            continue;
        }
        if (obj_parts_destroyed(e + 1) != 0) {
            continue;
        }
        bestDist = dist;
        best = e;
        kind = 1;
    }
    for (i = gBoxTargetCount, t = gBoxTargets; i != 0; i--, t++) {
        if (t->filled != 0) {
            continue;
        }
        dist = math_dist3(t->x, t->y, t->z, gPlayerX, gPlayerY, gPlayerZ);
        if (dist < bestDist) {
            kind = 2;
            best = t;
            bestDist = dist;
        }
    }
    for (p = gTriTargets; p->level != -1; p++) {
        if (p->level != levelno) {
            continue;
        }
        dist = math_dist3(p->pos[0], p->pos[1], p->pos[2], gPlayerX, gPlayerY, gPlayerZ);
        if (dist >= bestDist) {
            continue;
        }
        for (id = p->ids; *id != -1; id++) {
            if (veh_tri_is_solid(*id) != 0) {
                kind = 3;
                best = p;
                bestDist = dist;
                break;
            }
        }
    }
    if (kind == 0) {
        ptr = NULL;
    } else if (kind == 1) {
        ObjectModel *info = ((LevelObject *) best)->info;

        ptr = (u8 *) info + info->markerPts;
    } else if (kind == 3) {
        ptr = (u8 *) best + 0x16;
    } else {
        ptr = (u8 *) best + 0xC;
    }
    gNearestTargetPts = (s16 *) ptr;
    gNearestTargetKind = kind;
    return (s32) best;
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/77E20/obj_find_nearest_target.s")
#endif

/* Uses the sd-$ra frame convention - see the file-level note at the top of this file. Permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
/* Returns 1 if the gObjects entry just before `next` is in gPathParts's
 * table and every partPct byte its record lists is 100, else 0 (also 0 when
 * gPathParts is NULL). The asm callers pass their loop pointer already
 * advanced past the entry (t0 = entry + 0xFC), so this takes that pointer too.
 * Table: s32 count, then count 0x18-byte records {LevelObject *entry;
 * s32 n; u8 idx[16]}; the first record for the entry decides. Both counts are
 * counted down with `!=`. The asm returns in v0 and saves t0, t1, t2 and t4;
 * asm callers keep t0, t1 and t4-t7 live across the call. */
s32 obj_parts_destroyed(LevelObject *next) {
    LevelObject *e = next - 1;
    u8 *t = gPathParts;
    s32 n;
    s32 k;
    u8 *b;

    if (t == NULL) {
        return 0;
    }
    for (n = *(s32 *) t, t += 4; n != 0; n--, t += 0x18) {
        if (*(LevelObject * N64P *) t == e) {
            for (k = *(s32 *) (t + 4), b = t + 8; k != 0; k--, b++) {
                if (((u8 *) e)[0xEC + *b] != 100) {
                    return 0;
                }
            }
            return 1;
        }
    }
    return 0;
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/77E20/obj_parts_destroyed.s")
#endif

/* Uses the sd-$ra frame convention - see the file-level note at the top of this file. Permanently GLOBAL_ASM. */
#ifdef NON_MATCHING

/* GCarrierEta: the carrier's x/z distance to the marked target / its speed (0x802BD10C).
 * gCarrierEta = the x/z distance from the player (gCarrierX, gCarrierZ) to
 * the target, divided (unsigned) by gCarrierTargetSpeed when that is nonzero. The
 * target's x/z words are at +0x0/+0x8 for kinds 2 and 3, else +0x10/+0x18 (a
 * gObjects entry). The C callers pass the target pointer as an s32. */
void obj_calc_target_time(s32 arg0) {
    s32 *target = (s32 *) N64_IPTR(arg0);
    s32 x;
    s32 z;
    s32 dist;

    if (gMarkedTargetKind == 2 || gMarkedTargetKind == 3) {
        x = target[0];
        z = target[2];
    } else {
        x = target[4];
        z = target[6];
    }
    dist = math_dist3(x, 0, z, gCarrierX, 0, gCarrierZ);
    if (((u16) gCarrierTargetSpeed) != 0) {
        dist = (u32) dist / ((u16) gCarrierTargetSpeed);
    }
    gCarrierEta = dist;
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/77E20/obj_calc_target_time.s")
#endif

/* Same preserve-caller-registers convention as obj_clear_delayed_damage above - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
extern Mtx * N64P gToppleMtx; /* 2 matrices (lvl_group_move_part's falling parts) */
extern s32 gPoseMtxCursor;  /* Mtx pool cursor for spline_object_matrix */
extern s32 gPartMtxPool1[]; /* matrix pools of the two frames */
extern s32 gPartMtxPool0[];
extern s32 * N64P gPartMtxCursor;
void obj_disable_landed_part_tris(LevelObjectHead *model, s32 id);
void obj_step_tex_anims(LevelObject *obj, u8 *rec, u8 *end);
u32 *obj_copy_dl_tex_anim(u32 *src, u32 *srcEnd, u32 *dst, u8 *tbl, u8 *tblEnd);
u32 *obj_copy_dl_tex_anim2(u32 *src, u32 *srcEnd, u32 *dst, u8 *tbl, u8 *tblEnd);
s32 obj_falling_part_mtx(LevelObject *e, s32 off, s32 dy);
void obj_spawn_landing_effects(LevelObject *e, s32 index);
u32 *obj_draw_fading_parts(u32 *gfx);
u32 *obj_draw_sinking_part(u32 *gfx);
void obj_draw_toppling_parts(u32 **gfxA, u32 **gfxB);

/* Copies the 8-byte display-list commands [src, end) to dst; returns the new dst. */
#define COPY_DL_802BD1F8(dst, src, end)      \
    {                                        \
        u64 *_s = (u64 *) (src);             \
        u64 *_e = (u64 *) (end);             \
        u64 *_d = (u64 *) (dst);             \
                                             \
        while (_s != _e) {                   \
            *_d++ = *_s++;                   \
        }                                    \
        (dst) = (u32 *) _d;                  \
    }

/* obj_draw_all (0x802BD1F8): the level's objects: buildings and the other
 * destructible objects, whose parts break (percentages at +0xEC) and fall.
 * Draws the gObjects objects (up to gObjectsEnd, read once, walked with
 * `!=`) into four display lists: gA / gB (the two halves of the scene list),
 * gC / gD (per-object lists the scene calls). gToppleMtx/765C = mtxA/mtxB,
 * obj_draw_fading_parts(gE) and obj_draw_sinking_part(gF) (their new ends are dropped), the
 * matrix pool is this frame's (gPartMtxPool1 or gPartMtxPool0 by gBufferIndex).
 * Per object whose cell is in the gVisibleCells list (-1 ends it) and that
 * isn't a 0x38 object while obj_end_object_hidden is set:
 * - with a pose (info u16 +0xE != 0): spline_object_matrix builds its matrix
 *   (words +0x1C..+0x24, the pose count, the next gPoseMtxCursor Mtx (advanced by
 *   0x40), state at +0x38, gA, gB, word +0x34, the position) and both scene
 *   lists step 8; the object moves to gSplinePosX/Y/Z (obj_move_model by the
 *   difference);
 * - animated textures (obj_step_tex_anims with info's table texAnimTbl..partPoints), then
 *   gA/gB get a segment-9 command (0xBC002406: moveword offset 0x24) for info + 0x50 and a G_DL to gC/gD
 *   (physical), and the commands info+unk10..unk18 are copied to gC and gD;
 * - the part records info+partDlRecs..toppleKeys: {word n, n x {u16 part, u8 mode, u8
 *   limit}, 4 words}, the u16 at +4 being the record's (1-based) part p too.
 *   A record whose conditions all hold (part's percentage < limit fails for
 *   mode != 0, > limit for mode 0) is drawn: with a moving part
 *   (e->partFalling[p - 1]), its counter partFallTime[p - 1] advances, dy = c * c * -30000;
 *   if dy <= -(table u16 at toppleKeys + 2(p - 1) << 16) the part drops
 *   (partFalling = 0, obj_spawn_landing_effects, shake 10/300, percentage 100,
 *   obj_disable_landed_part_tris); its matrix (obj_falling_part_mtx) is pushed in gC; the
 *   record's gC and gD command ranges (words 0-1 / 2-3, offsets from info)
 *   are copied (through obj_copy_dl_tex_anim / obj_copy_dl_tex_anim2 when the texture table
 *   is non-empty), with G_POPMTX in gC and the matrix push / G_POPMTX in gD
 *   for a moving part. If the first record holds, it is the only one drawn;
 *   otherwise every holding record is.
 * Every object (listed or not) then ends gC and gD (G_ENDDL) and, with a
 * pose, pops the matrix in gA and gB. Finally obj_draw_toppling_parts(&gA, &gB) and
 * G_ENDDL in both (not advanced).
 * ABI (00000.c passes eight pointers); the asm saves s0-s7, gp, fp and uses
 * gp as scratch. Its trapping add/sub stay in game range. */
#ifdef PORT_HOST
/* bc.exe's orbit camera (PORT_DRAW_CELLS): an object whose cell the game
 * didn't list but the drawing list has is drawn as it is, with none of the
 * per-object work above (no pose step, texture animation or falling-part
 * counter: the game leaves unlisted objects alone, so they stay as they
 * are), and without its moving parts.  Only objects without a pose. */
static void port_obj_draw_only(LevelObject *o, u32 **pA, u32 **pB, u32 **pC, u32 **pD) {
    u8 *ob = (u8 *) o;
    u8 *info = (u8 *) o->info;
    u8 *tbl = info + *(s32 *) (info + 0x28);
    u8 *tblEnd = info + *(s32 *) (info + 0x2C);
    u8 *rec = info + *(s32 *) (info + 0x38);
    u8 *recEnd = info + *(s32 *) (info + 0x3C);
    u32 *gA = *pA, *gB = *pB, *gC = *pC, *gD = *pD;
    s16 *s = PORT_DRAW_CELLS(gVisibleCells);
    s32 first;

    if (s == (s16 *) gVisibleCells || *(u16 *) (info + 0xE) != 0) return;
    for (;; s++) {
        if (*s == -1) return;
        if (*s == o->cell) break;
    }
    if (o->kind == OBJ_KIND_END && obj_end_object_hidden() != 0) return;
    PORT_ITAG(gA, PORT_ITAG_BUILDING, o - gObjects);
    PORT_ITAG(gB, PORT_ITAG_BUILDING, o - gObjects);
    gA[0] = gB[0] = 0xBC002406;
    gA[1] = gB[1] = (u32) (info + 0x50) - 0x80000000;
    gA[2] = gB[2] = 0x06000000;
    gA[3] = (u32) gC - 0x80000000;
    gB[3] = (u32) gD - 0x80000000;
    gA += 4;
    gB += 4;
    {
        u64 *p = (u64 *) (info + *(s32 *) (info + 0x10));
        u64 *pe = (u64 *) (info + *(s32 *) (info + 0x18));

        while (p != pe) {
            u64 w = *p++;

            *(u64 *) gC = w;
            gC += 2;
            *(u64 *) gD = w;
            gD += 2;
        }
    }
    first = 1;
    while (rec != recEnd) {
        s32 n = *(s32 *) rec;
        s32 off = (*(u16 *) (rec + 4) - 1) * 2;
        s32 ok = 1;
        u32 *src;
        u32 *srcEnd;

        rec += 4;
        while (n != 0) {
            s32 part = *(u16 *) rec;
            s32 mode = rec[2];
            s32 limit = rec[3];
            s32 pct = ob[0xEC + part - 1];

            n--;
            rec += 4;
            if (mode != 0 ? (pct < limit) : (limit < pct)) {
                rec += n * 4;
                ok = 0;
                break;
            }
        }
        if (!ok) {
            rec += 0x10;
            first = 0;
            continue;
        }
        if (*(u16 *) (ob + 0x48 + off) == 0) {
            src = (u32 *) (info + ((s32 *) rec)[0]);
            srcEnd = (u32 *) (info + ((s32 *) rec)[1]);
            if (tbl != tblEnd) {
                gC = obj_copy_dl_tex_anim(src, srcEnd, gC, tbl, tblEnd);
            } else {
                COPY_DL_802BD1F8(gC, src, srcEnd);
            }
            src = (u32 *) (info + ((s32 *) rec)[2]);
            srcEnd = (u32 *) (info + ((s32 *) rec)[3]);
            if (tbl != tblEnd) {
                gD = obj_copy_dl_tex_anim2(src, srcEnd, gD, tbl, tblEnd);
            } else {
                COPY_DL_802BD1F8(gD, src, srcEnd);
            }
        }
        rec += 0x10;
        if (first) break;
    }
    *pA = gA;
    *pB = gB;
    *pC = gC;
    *pD = gD;
}
#endif

void obj_draw_all(u32 *gA, u32 *gB, u32 *gC, u32 *gD, s32 mtxA, s32 mtxB, u32 *gE, u32 *gF) {
    LevelObject *end;
    LevelObject *o;

    gToppleMtx = (Mtx *) N64_IPTR(mtxA);
    gPoseMtxCursor = mtxB;
    obj_draw_fading_parts(gE);
    obj_draw_sinking_part(gF);
    if (gBufferIndex != 0) {
        gPartMtxCursor = gPartMtxPool1;
    } else {
        gPartMtxCursor = gPartMtxPool0;
    }
    end = gObjectsEnd;
    for (o = gObjects; o != end; o++) {
        u8 *ob = (u8 *) o;
        u8 *info;
        u8 *tbl;
        u8 *tblEnd;
        u8 *rec;
        u8 *recEnd;
        s16 *s;
        s32 count;
        s32 first;

        for (s = (s16 *) gVisibleCells;; s++) {
            if (*s == -1) {
#ifdef PORT_HOST
                port_obj_draw_only(o, &gA, &gB, &gC, &gD);
#endif
                goto tail;
            }
            if (*s == o->cell) {
                break;
            }
        }
        if (o->kind == OBJ_KIND_END && obj_end_object_hidden() != 0) {
            goto tail;
        }
        PORT_ITAG(gA, PORT_ITAG_BUILDING, o - gObjects);
        PORT_ITAG(gB, PORT_ITAG_BUILDING, o - gObjects);
        info = (u8 *) o->info;
        count = *(u16 *) (info + 0xE);
        if (count != 0) {
            s32 mtx = gPoseMtxCursor;
            s32 ox;
            s32 oy;
            s32 oz;

            gPoseMtxCursor = mtx + 0x40;
            spline_object_matrix(*(s32 *) (ob + 0x1C), *(s32 *) (ob + 0x20), *(s32 *) (ob + 0x24), count, (Mtx *) N64_IPTR(mtx),
                          ob + 0x38, (Gfx *) gA, (Gfx *) gB, *(s32 *) (ob + 0x34), o->pos[0], o->pos[1],
                          o->pos[2]);
            gA += 2;
            gB += 2;
            ox = o->pos[0];
            oy = o->pos[1];
            oz = o->pos[2];
            o->pos[0] = gSplinePosX;
            o->pos[1] = gSplinePosY;
            o->pos[2] = gSplinePosZ;
            obj_move_model((LevelObjectHead *) o, o->pos[0] - ox, o->pos[1] - oy, o->pos[2] - oz);
        }
        info = (u8 *) o->info;
        tbl = info + *(s32 *) (info + 0x28);
        tblEnd = info + *(s32 *) (info + 0x2C);
        rec = info + *(s32 *) (info + 0x38);
        recEnd = info + *(s32 *) (info + 0x3C);
        obj_step_tex_anims(o, tbl, tblEnd);
        gA[0] = gB[0] = 0xBC002406;
        gA[1] = gB[1] = (u32) (info + 0x50) - 0x80000000;
        gA[2] = gB[2] = 0x06000000;
        gA[3] = (u32) gC - 0x80000000;
        gB[3] = (u32) gD - 0x80000000;
        gA += 4;
        gB += 4;
        {
            u64 *p = (u64 *) (info + *(s32 *) (info + 0x10));
            u64 *pe = (u64 *) (info + *(s32 *) (info + 0x18));

            while (p != pe) {
                u64 w = *p++;

                *(u64 *) gC = w;
                gC += 2;
                *(u64 *) gD = w;
                gD += 2;
            }
        }
        first = 1;
        while (rec != recEnd) {
            s32 n = *(s32 *) rec;
            s32 off = (*(u16 *) (rec + 4) - 1) * 2;
            s32 moving;
#ifdef NON_MATCHING
            s32 mtxAddr = 0; /* set and read only when moving != 0 */
#else
            s32 mtxAddr;
#endif
            u32 *src;
            u32 *srcEnd;

            rec += 4;
            while (n != 0) {
                s32 part = *(u16 *) rec;
                s32 mode = rec[2];
                s32 limit = rec[3];
                s32 pct = ob[0xEC + part - 1];

                n--;
                rec += 4;
                if (mode != 0 ? (pct < limit) : (limit < pct)) {
                    rec += n * 4 + 0x10;
                    goto next;
                }
            }
            moving = *(u16 *) (ob + 0x48 + off);
            if (moving != 0) {
                u16 *ctr = (u16 *) (ob + 0x68 + off);
                u32 c = *ctr + 1;
                s32 dy;
                s32 floor;

                *ctr = c;
                dy = c * c * (u32) -30000;
                floor = -(*(u16 *) (info + *(s32 *) (info + 0x3C) + off) << 16);
                if (floor >= dy) {
                    u32 idx = (u32) off >> 1;

                    *(u16 *) (ob + 0x48 + off) = 0;
                    obj_spawn_landing_effects(o, idx);
                    gShakeTime = 10;
                    gShakeSize = 300;
                    ob[idx + 0xEC] = 100;
                    obj_disable_landed_part_tris((LevelObjectHead *) o, idx);
                }
                mtxAddr = obj_falling_part_mtx(o, off, dy);
                PORT_ITAG(gC, PORT_ITAG_BPART, ((o - gObjects) << 8) | ((u32) off >> 1));
                gC[0] = 0x01040040;
                gC[1] = mtxAddr;
                gC += 2;
            }
            src = (u32 *) (info + ((s32 *) rec)[0]);
            srcEnd = (u32 *) (info + ((s32 *) rec)[1]);
            if (tbl != tblEnd) {
                gC = obj_copy_dl_tex_anim(src, srcEnd, gC, tbl, tblEnd);
            } else {
                COPY_DL_802BD1F8(gC, src, srcEnd);
            }
            if (moving != 0) {
                gC[0] = 0xBD000000;
                gC[1] = 0;
                gC += 2;
                PORT_ITAG(gD, PORT_ITAG_BPART, ((o - gObjects) << 8) | ((u32) off >> 1));
                gD[0] = 0x01040040;
                gD[1] = mtxAddr;
                gD += 2;
            }
            src = (u32 *) (info + ((s32 *) rec)[2]);
            srcEnd = (u32 *) (info + ((s32 *) rec)[3]);
            if (tbl != tblEnd) {
                gD = obj_copy_dl_tex_anim2(src, srcEnd, gD, tbl, tblEnd);
            } else {
                COPY_DL_802BD1F8(gD, src, srcEnd);
            }
            if (moving != 0) {
                gD[0] = 0xBD000000;
                gD[1] = 0;
                gD += 2;
            }
            rec += 0x10;
            if (first) {
                break;
            }
        next:
            first = 0;
        }
    tail:
        gC[0] = 0xB8000000;
        gC[1] = 0;
        gD[0] = 0xB8000000;
        gD[1] = 0;
        gC += 2;
        gD += 2;
        if (*(u16 *) ((u8 *) o->info + 0xE) != 0) {
            gA[0] = 0xBD000000;
            gA[1] = 0;
            gB[0] = 0xBD000000;
            gB[1] = 0;
            gA += 2;
            gB += 2;
        }
    }
    obj_draw_toppling_parts(&gA, &gB);
    gA[0] = 0xB8000000;
    gA[1] = 0;
    gB[0] = 0xB8000000;
    gB[1] = 0;
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/77E20/obj_draw_all.s")
#endif

/* Uses the sd-$ra frame convention - see the file-level note at the top of this file. Permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
/* A landed part's triangles with linkedPart set stop being solid (0x802BD85C).
 * Clears isSolid of every triangle in [model->start, model->end) whose partNo
 * equals id + 1 and whose linkedPart is set. The end is read once and the walk
 * stops with `!=`. The asm takes model in v0 and id in a3, and saves v1, a0,
 * a1 and a3 (so it changes nothing); asm caller obj_draw_all keeps a0-a3,
 * t0-t5, f12 and f14 live across the call. The asm's `addi` on id traps on
 * 0x7FFFFFFF; the C wraps instead. */
void obj_disable_landed_part_tris(LevelObjectHead *model, s32 id) {
    CollisionTri *r = model->start;
    CollisionTri *end = model->end;

    id++;
    for (; r != end; r++) {
        if (r->partNo == id && r->linkedPart != 0) {
            r->isSolid = 0;
        }
    }
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/77E20/obj_disable_landed_part_tris.s")
#endif

/* Same preserve-caller-registers convention as obj_clear_delayed_damage above - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
/* 409D0.c defines it as u8; declared s32 here because the asm tests the whole
 * of v0 (the u8 callee leaves the upper bits clear anyway). */

/* Level 0x32 not completed yet: its kind-0x38 end object is ignored (0x802BD8C8).
 * Returns 1 on level 0x32 when stats_level_done(level) returns 0, else 0. The
 * asm returns in t1 and preserves every other register it touches; asm
 * callers keep many registers live across the call (veh_sphere_hits_world and
 * veh_report_world_hits: a0-a3, t3-t6, t8, t9, f12, f14). */
s32 obj_end_object_hidden(void) {
    if (levelno == 0x32 && stats_level_done(levelno) == 0) {
        return 1;
    }
    return 0;
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/77E20/obj_end_object_hidden.s")
#endif

/* Uses the sd-$ra frame convention - see the file-level note at the top of this file. Permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
float sqrtf(float);
#pragma intrinsic(sqrtf)

/* Moves an object's model tables and triangles; new planes (0x802BD99C).
 * Moves a model by (dx, dy, dz) world units and refreshes its triangles. The
 * model header's tables (offsets from the header) hold coordinates at two
 * scales: s16 tables at >> 5 (partHeights..supportRecs: y only; the box at footprintBox: x, z,
 * x, z; 0x14-byte records floorTris..texAnimTbl: three x, y, z points; 4 points at
 * markerPts; 8-byte records partPoints..breakEffects: one point) and s32 words at << 11
 * (0x38-byte records breakEffects..landEffects and landEffects..partDlRecs: x, y, z at +0 and y again
 * at +0x28). Each triangle in [model->start, model->end) moves by
 * (dx, dy, dz) >> 2 and gets its plane recomputed: the s64 cross product of
 * its edges, its squared length and length as floats (cvt.s.l, single
 * precision), d = -(normal . vertex 1) (64-bit, wrapping) and the dominant
 * normal axis. Every range is walked with `!=`. The asm's trapping 32-bit
 * `sub`s wrap here.
 * The asm takes model in v0 and (dx, dy, dz) in a2, a3, t0; it changes s0-s7
 * and fp (its callers save them) and leaves junk in a1-a3; asm callers keep
 * f12 and f14 live across the call. */
void obj_move_model(LevelObjectHead *model, s32 dx, s32 dy, s32 dz) {
    u8 *d = model->data;
    s32 hx = dx >> 5, hy = dy >> 5, hz = dz >> 5;
    s32 wx = dx << 11, wy = dy << 11, wz = dz << 11;
    s32 sx, sy, sz;
    s16 *h;
    s16 *hend;
    s32 *w;
    s32 *wend;
    s32 i;
    CollisionTri *r;
    CollisionTri *rend;

#define MODEL_TABLE(o) (d + *(s32 *) (d + (o)))
    for (h = (s16 *) MODEL_TABLE(0x40), hend = (s16 *) MODEL_TABLE(0x44); h != hend; h++) {
        h[0] += hy;
    }
    h = (s16 *) MODEL_TABLE(0x20);
    h[0] += hx;
    h[1] += hz;
    h[2] += hx;
    h[3] += hz;
    for (h = (s16 *) MODEL_TABLE(0x24), hend = (s16 *) MODEL_TABLE(0x28); h != hend; h += 10) {
        for (i = 0; i < 9; i += 3) {
            h[i + 0] += hx;
            h[i + 1] += hy;
            h[i + 2] += hz;
        }
    }
    for (h = (s16 *) MODEL_TABLE(0x1C), i = 4; i != 0; i--, h += 3) {
        h[0] += hx;
        h[1] += hy;
        h[2] += hz;
    }
    for (w = (s32 *) MODEL_TABLE(0x30), wend = (s32 *) MODEL_TABLE(0x34); w != wend; w += 14) {
        w[0] += wx;
        w[1] += wy;
        w[2] += wz;
        w[10] += wy;
    }
    for (w = (s32 *) MODEL_TABLE(0x34), wend = (s32 *) MODEL_TABLE(0x38); w != wend; w += 14) {
        w[0] += wx;
        w[1] += wy;
        w[2] += wz;
        w[10] += wy;
    }
    for (h = (s16 *) MODEL_TABLE(0x2C), hend = (s16 *) MODEL_TABLE(0x30); h != hend; h += 4) {
        h[0] += hx;
        h[1] += hy;
        h[2] += hz;
    }
#undef MODEL_TABLE

    sx = dx >> 2;
    sy = dy >> 2;
    sz = dz >> 2;
    for (r = model->start, rend = model->end; r != rend; r++) {
        s32 x0 = r->v[0][0] + sx, y0 = r->v[0][1] + sy, z0 = r->v[0][2] + sz;
        s32 x1 = r->v[1][0] + sx, y1 = r->v[1][1] + sy, z1 = r->v[1][2] + sz;
        s32 x2 = r->v[2][0] + sx, y2 = r->v[2][1] + sy, z2 = r->v[2][2] + sz;
        s32 ex1 = x0 - x1, ey1 = y0 - y1, ez1 = z0 - z1;
        s32 ex2 = x0 - x2, ey2 = y0 - y2, ez2 = z0 - z2;
        s64 nx, ny, nz;
        s64 ax, ay, az;
        f32 fx, fy, fz;
        f32 sq;

        r->v[0][0] = x0, r->v[0][1] = y0, r->v[0][2] = z0;
        r->v[1][0] = x1, r->v[1][1] = y1, r->v[1][2] = z1;
        r->v[2][0] = x2, r->v[2][1] = y2, r->v[2][2] = z2;
        nx = (s64) ey1 * ez2 - (s64) ez1 * ey2;
        ny = (s64) ez1 * ex2 - (s64) ex1 * ez2;
        nz = (s64) ex1 * ey2 - (s64) ey1 * ex2;
        r->normal[0] = nx;
        r->normal[1] = ny;
        r->normal[2] = nz;
        fx = nx;
        fy = ny;
        fz = nz;
        sq = fx * fx + fy * fy + fz * fz;
        r->lenSq = sq;
        r->d = -(nx * x1 + ny * y1 + nz * z1);
        r->len = sqrtf(sq);

        ax = (nx < 0) ? -nx : nx;
        ay = (ny < 0) ? -ny : ny;
        az = (nz < 0) ? -nz : nz;
        if (az < ax || az < ay) {
            r->axis = (ay < ax || ay < az) ? TRI_AXIS_X : TRI_AXIS_Y;
        } else {
            r->axis = TRI_AXIS_Z;
        }
    }
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/77E20/obj_move_model.s")
#endif

/* Uses the sd-$ra frame convention - see the file-level note at the top of this file. Permanently GLOBAL_ASM. */
#ifdef NON_MATCHING

/* An animation channel record (0xC bytes, then n words). */
#define ANIM_N(r) ((r)[4])                     /* frame count */
#define ANIM_FRAME(r) ((r)[5])                 /* current frame */
#define ANIM_TYPE(r) ((r)[6])                  /* 0 timed, 1 global clock, else player heading */
#define ANIM_FRAC(r) (*(u32 *) ((r) + 8))       /* 0..0xFE blend */
#define ANIM_LO(r) (*(u16 *) ((r) + 0xC))       /* period / heading range start */
#define ANIM_HI(r) (*(u16 *) ((r) + 0xE))       /* tick counter / heading range end */

/* Quarter-turn heading (0..0x3FF) from the sine num / d: arcsine >> 4. */
static s32 heading_802BDDB4(s32 num, f32 d) {
    s32 sine;

    CVT_W_S(sine, 65536.0f * ((f32) num / d));
    return (u32) math_asin(sine) >> 4;
}

/* Steps the objects' texture animation records (timed, clock, heading) (0x802BDDB4).
 * Steps the animation channel records rec..end (each 0xC + n * 4 bytes) of
 * object `obj`. Objects with kind == 0x38 are skipped while gHaveCarrier or
 * gHaveShuttle is set unless gPathBuildingsDestroyed, obj_box_targets_done() and obj_tri_targets_ahead_done()
 * all say otherwise. Type 0: the u16 tick counter (+0xE) counts up to the
 * period (+0xC), then restarts at 0 and the frame (+5) advances (wrapping at
 * n); the blend word (+8) = tick * 255 / period. Type 1: when (low word of
 * gFrameDrawTime) >> 4 is a multiple of the period, frame = (that >> 8) % n.
 * Other types: the heading from the object (pos[0], pos[2]) to the player
 * (gCamEyeX / gCamEyeZ >> 11, logical), 0..0xFFF by quadrant, selects
 * the frame within the u16 range lo (+0xC) .. hi (+0xE), wrapping through
 * 0xFFF when hi < lo (the wrap uses 0xFFF - lo, one short of a full turn,
 * as the asm): frame = v / 255, blend = v % 255 with v = offset * (n - 1) *
 * 255 / span (u32), frame 0 below the range and n - 1 above it.
 * Register convention: obj v0, rec t1, end t2 (conventions.txt); the asm
 * saves everything it uses. Its asm caller obj_draw_all keeps a0-a2 and
 * t0-t5 live (a mixed N64 build would need a thunk; the native port won't).
 * Divisors (period, n, the spans) of 0 trap (break 7); the object can't sit
 * exactly at the player (0 / 0). */
void obj_step_tex_anims(LevelObject *obj, u8 *rec, u8 *end) {
    if (obj->kind == OBJ_KIND_END && (gHaveCarrier != 0 || gHaveShuttle != 0)) {
        if (gPathBuildingsDestroyed == 0 || obj_box_targets_done() == 0 || obj_tri_targets_ahead_done() == 0) {
            return;
        }
    }
    for (; rec != end; rec += 0xC + ANIM_N(rec) * 4) {
        if (ANIM_TYPE(rec) == 0) {
            u32 period = ANIM_LO(rec);
            u32 tick = ANIM_HI(rec) + 1;

            if (tick == period) {
                u32 frame = ANIM_FRAME(rec) + 1;

                if (frame == ANIM_N(rec)) {
                    frame = 0;
                }
                ANIM_FRAME(rec) = frame;
                tick = 0;
            }
            ANIM_HI(rec) = tick;
            ANIM_FRAC(rec) = tick * 0xFF / period;
        } else if (ANIM_TYPE(rec) == 1) {
            u32 t = (u32) gFrameDrawTime >> 4;

            if (t % ANIM_LO(rec) == 0) {
                ANIM_FRAME(rec) = (t >> 8) % ANIM_N(rec);
            }
        } else {
            s32 px = (u32) gCamEyeX >> 11;
            s32 pz = (u32) gCamEyeZ >> 11;
            s32 ox = obj->pos[0];
            s32 oz = obj->pos[2];
            f32 dx = (f32) (s32) (px - ox);
            f32 dz = (f32) (s32) (pz - oz);
            f32 d = sqrtf(dx * dx + dz * dz);
            s32 ang;
            s32 lo = ANIM_LO(rec);
            s32 hi = ANIM_HI(rec);
            u32 v;

            if (px < ox) {
                if (pz < oz) {
                    ang = heading_802BDDB4(ox - px, d) + 0x800;
                } else {
                    ang = heading_802BDDB4(pz - oz, d) + 0xC00;
                }
            } else if (pz < oz) {
                ang = heading_802BDDB4(oz - pz, d) + 0x400;
            } else {
                ang = heading_802BDDB4(px - ox, d);
            }
            if (hi < lo) {
                if (ang < lo && hi < ang) {
                    ANIM_FRAME(rec) = 0;
                    continue;
                }
                v = (ang < lo) ? (u32) (0xFFF - lo) + ang : (u32) (ang - lo);
                v = v * (u32) (ANIM_N(rec) - 1) * 0xFF / ((u32) (0xFFF - lo) + hi);
            } else if (ang < lo) {
                ANIM_FRAME(rec) = 0;
                continue;
            } else if (hi < ang) {
                ANIM_FRAME(rec) = ANIM_N(rec) - 1;
                continue;
            } else {
                v = (u32) (ang - lo) * (u32) (ANIM_N(rec) - 1) * 0xFF / (u32) (hi - lo);
            }
            ANIM_FRAME(rec) = v / 0xFF;
            ANIM_FRAC(rec) = v % 0xFF;
        }
    }
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/77E20/obj_step_tex_anims.s")
#endif

/* Same preserve-caller-registers convention as obj_clear_delayed_damage above - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
/* Copies the display list [src, srcEnd) (8-byte commands) to dst and returns
 * the new end of dst, animating textures on the way: a G_SETTIMG (0xFD) whose
 * image address is a key in the table [tbl, tblEnd) gets one of that record's
 * images. Records are {u32 key; u8 n; u8 cur; u8 pad; u8 animate; u32 primOr;
 * u32 image[n]} (0xC + 4n bytes). With animate set, the first SETTIMG gets
 * image[cur] (its own address when cur is 0), the commands up to and
 * including the next SETTIMG are copied with that one getting image[cur + 1]
 * (cur + 1 wrapping to 0 at n; 0 again means "unchanged"), and the commands up
 * to the next G_SETPRIMCOLOR (0xFA) are copied with primOr ORed into its first
 * word. Without animate, the SETTIMG gets image[cur]. Those two inner scans
 * aren't bounded by srcEnd: the list must contain the markers. The record's
 * cur is never written here. Shared by obj_copy_dl_tex_anim and obj_copy_dl_tex_anim2, which
 * differ only in the register that holds dst. */
static u32 *dl_anim_copy_802BE228(u32 *src, u32 *srcEnd, u32 *dst, u8 *tbl, u8 *tblEnd) {
    u32 w0;
    u32 w1;
    u8 *rec;
    s32 next;

    while (src != srcEnd) {
        w0 = src[0];
        dst[0] = w0;
        w1 = src[1];
        if ((w0 & 0xFF000000) >> 24 == 0xFD) {
            for (rec = tbl; rec != tblEnd; rec += rec[4] * 4 + 0xC) {
                if (*(u32 *) rec == w1) {
                    break;
                }
            }
            if (rec != tblEnd) {
                if (rec[7] != 0) {
                    next = rec[5];
                    if (next != 0) {
                        w1 = *(u32 *) (rec + 0xC + next * 4);
                    }
                    dst[1] = w1;
                    next++;
                    if (rec[4] == next) {
                        next = 0;
                    }
                    src += 2;
                    dst += 2;
                    for (;;) {
                        w0 = src[0];
                        dst[0] = w0;
                        w1 = src[1];
                        if ((w0 & 0xFF000000) >> 24 == 0xFD) {
                            break;
                        }
                        dst[1] = w1;
                        src += 2;
                        dst += 2;
                    }
                    if (next != 0) {
                        w1 = *(u32 *) (rec + 0xC + next * 4);
                    }
                    dst[1] = w1;
                    src += 2;
                    dst += 2;
                    for (;;) {
                        w0 = src[0];
                        if ((w0 & 0xFF000000) >> 24 == 0xFA) {
                            break;
                        }
                        dst[0] = w0;
                        dst[1] = src[1];
                        src += 2;
                        dst += 2;
                    }
                    dst[0] = w0 | *(u32 *) (rec + 8);
                    dst[1] = src[1];
                    src += 2;
                    dst += 2;
                    continue;
                }
                next = rec[5];
                if (next != 0) {
                    w1 = *(u32 *) (rec + 0xC + next * 4);
                }
            }
        }
        dst[1] = w1;
        src += 2;
        dst += 2;
    }
    return dst;
}

/* Copies a display list, swapping the animated SETTIMG images (0x802BE228).
 * See dl_anim_copy_802BE228. The asm takes src in t7, srcEnd in s0, dst in a2
 * and the table in t1..t2, returns the new dst in a2 (and leaves t7 at
 * srcEnd); it saves v0, v1, a0, a3, t3 and t4. Asm caller obj_draw_all keeps
 * a0, a1, t0-t6, f12 and f14 live across the call. */
u32 *obj_copy_dl_tex_anim(u32 *src, u32 *srcEnd, u32 *dst, u8 *tbl, u8 *tblEnd) {
    return dl_anim_copy_802BE228(src, srcEnd, dst, tbl, tblEnd);
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/77E20/obj_copy_dl_tex_anim.s")
#endif

/* Same preserve-caller-registers convention as obj_clear_delayed_damage above - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
/* The same for obj_draw_all's second list (destination in t0) (0x802BE3C8).
 * obj_copy_dl_tex_anim with dst in t0 (in and out) instead of a2. The asm saves v0,
 * v1, a0, a2, a3, t3 and t4; asm caller obj_draw_all keeps a0-a2, t1-t6, f12
 * and f14 live across the call. */
u32 *obj_copy_dl_tex_anim2(u32 *src, u32 *srcEnd, u32 *dst, u8 *tbl, u8 *tblEnd) {
    return dl_anim_copy_802BE228(src, srcEnd, dst, tbl, tblEnd);
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/77E20/obj_copy_dl_tex_anim2.s")
#endif

/* Uses the sd-$ra frame convention - see the file-level note at the top of this file. Permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
extern s32 * N64P gPartMtxCursor; /* matrix pool cursor (0x40-byte 16.16 matrices) */
extern s32 gObjScratchMtx[]; /* scratch 16.16 matrix */

/* A falling part's matrix: spin about its pivot, drop dy (0x802BE574).
 * Builds the matrix of one moving part of object e in the next pool slot
 * (gPartMtxCursor, advanced by 0x40) and returns its address - 0x80000000. off
 * is twice the part index: the halfwords at e + 0x68 + off (scale, u16) and
 * +0x88 (s16) / +0xA8 / +0xC8 (u16) give the angles scale * value (32-bit
 * product, + 0xFFF when negative) about x, y and z; the pivot is the u16
 * x, y, z (<< 16) at info + info->partPoints + off * 4. m = translate(-pivot),
 * then m *= rotation x, y, z, translate(pivot), translate(0, dy, 0) (each
 * built in gObjScratchMtx, mtx_mul), converted to the Mtx layout
 * (mtx_to_n64).
 * Register convention: e in v0, off in a3, dy in s6, result in s5. The asm
 * saves a0-t5, t7, t8, s0-s2, s4, s7 (t6 is untouched); asm caller
 * obj_draw_all keeps a0-a2 and t0-t6 live across the call. */
s32 obj_falling_part_mtx(LevelObject *e, s32 off, s32 dy) {
    s32 *m = gPartMtxCursor;
    u8 *b = (u8 *) e + off;
    u32 scale;
    s32 ax;
    s32 ay;
    s32 az;
    u16 *pivot;
    s32 px;
    s32 py;
    s32 pz;

    gPartMtxCursor = m + 0x10;
    scale = *(u16 *) (b + 0x68);
    ax = scale * *(s16 *) (b + 0x88);
    if (ax < 0) {
        ax += 0xFFF;
    }
    ay = scale * *(u16 *) (b + 0xA8);
    if (ay < 0) {
        ay += 0xFFF;
    }
    az = scale * *(u16 *) (b + 0xC8);
    if (az < 0) {
        az += 0xFFF;
    }
    pivot = (u16 *) ((u8 *) e->info + e->info->partPoints + off * 4);
    px = pivot[0] << 16;
    py = pivot[1] << 16;
    pz = pivot[2] << 16;
    mtx_translate(-px, -py, -pz, m);
    mtx_rotate_x(ax, gObjScratchMtx);
    mtx_mul(gObjScratchMtx, m);
    mtx_rotate_y(ay, gObjScratchMtx);
    mtx_mul(gObjScratchMtx, m);
    mtx_rotate_z(az, gObjScratchMtx);
    mtx_mul(gObjScratchMtx, m);
    mtx_translate(px, py, pz, gObjScratchMtx);
    mtx_mul(gObjScratchMtx, m);
    mtx_translate(0, dy, 0, gObjScratchMtx);
    mtx_mul(gObjScratchMtx, m);
    mtx_to_n64((u16 *) m);
    return (u32) m - 0x80000000;
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/77E20/obj_falling_part_mtx.s")
#endif

#ifdef NON_MATCHING
/* Callees of obj_collide_vehicle / obj_collide_sphere / obj_hit_tri (C rewrites; see
 * conventions.txt for their asm registers). */
void veh_contacts_save(void);
s32 veh_prev_contacts_has(s32 value);
s32 obj_is_solid_for_vehicle(LevelObject *e, s32 bit);
void obj_hits_clear(void);
void obj_hits_add(s32 a, s32 b);
s32 obj_hits_has(s32 a, s32 b);
void obj_collide_sphere(s32 x, s32 y, s32 z, s32 r, LevelObject *e, s32 id);
void obj_hit_tri(s32 id, LevelObject *e, CollisionTri *tri);
void obj_check_mars_fail(LevelObject *e);
void obj_detect_rocking_hits(void);
void obj_set_full_damage(u8 *vehicle);
s32 obj_calc_damage(s32 id, s32 value, s32 bit, LevelObject *e, s32 flag, s32 key);
extern u8 gObjHitPartDown;
extern u8 gObjHit;

/* The sphere-vs-triangle test the asm inlines as a call chain (see 56040.c,
 * math_sphere_hits_tri_edges): plane distance (math_sphere_tri_plane), the projected
 * point-in-triangle test, the edge test, the vertex test; 1 on the first hit. */
static s32 sphere_hits_tri_77E20(s32 x, s32 y, s32 z, s32 r, CollisionTri *tri) {
    s32 hit[3];
    s32 uv[8];

    if (math_sphere_tri_plane(x, y, z, r, (u8 *) tri, hit) == 0) {
        return 0;
    }
    math_tri_project_2d((u8 *) tri, hit[0], hit[1], hit[2], uv);
    if (math_point_in_tri_2d(uv[2], uv[3], uv[4], uv[5], uv[0], uv[1], uv[6], uv[7]) != 0) {
        return 1;
    }
    if (math_sphere_hits_tri_edges(x, y, z, r, (u8 *) tri) != 0) {
        return 1;
    }
    return math_sphere_hits_tri_vertex0(x, y, z, r, (u8 *) tri) != 0;
}
#endif

/* Uses the sd-$ra frame convention - see the file-level note at the top of this file. Permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
extern s32 gObjPrevHitFrame;
extern u8 gObjHitImmunePart;
extern u8 gObjRockLatch;

/* One vehicle's (type id) collision and damage pass against the objects (0x802BE77C).
 * Collision pass of vehicle id (record `vehicle`): clears gObjHitPartDown/7803/7807
 * and the once-per-frame latch gObjRockLatch, copies gObjHitFrame to gObjPrevHitFrame,
 * obj_set_full_damage(vehicle), resets the hit list (obj_hits_clear), then finds the
 * vehicle's gVehBoundSpheres sphere (first entry whose byte 0x10 is id; the scan
 * isn't bounded). If its flag byte 0x11 is set, every object (gObjects up
 * to gObjectsEnd, read once, walked with `!=`) within range of the sphere
 * (math_spheres_overlap), not a 0x38 object while obj_end_object_hidden is set, and not
 * excluded for this vehicle (obj_is_solid_for_vehicle), gets the triangle pass
 * obj_collide_sphere. Then buffer A is copied to B (veh_contacts_save) unless id is
 * 0xFF, gCollBlocked is set unless the ring span is 0/0xFFF, and vehicle[0x9C],
 * [0x9D], [0x9E] = gObjHitPartDown, gObjHit, gObjHitImmunePart.
 * Register convention: id in t8, vehicle in gp (an argument here: callers pass
 * different records). The asm saves a2-t5, t7, s0, s2 and t8 and leaves v0-a1
 * changed, s1/s3/s4 and f20-f28 changed by obj_collide_sphere's helpers. Asm callers
 * keep a3 (skyfall_update, ramdozer_update, backlash_update, amdream_update,
 * police_update, ateam_update, starski_update), t7 (carrier_check_crash) or a3, t0,
 * t1 and t8 (crane_update) live across the call. The other leftovers the
 * survey lists as read by callers (v1, a0, t6, s1, s3, s4, f12-f26) are dead
 * there (overwritten, or saved and restored around the call). */
void obj_collide_vehicle(s32 id, u8 *vehicle) {
    u8 *s;

    gObjHitPartDown = 0;
    gObjHit = 0;
    gObjHitImmunePart = 0;
    gObjPrevHitFrame = gObjHitFrame;
    gObjRockLatch = 0;
    obj_set_full_damage(vehicle);
    obj_hits_clear();
    for (s = gVehBoundSpheres; s[0x10] != id; s += 0x14) {
    }
    if ((s8) s[0x11] != 0) {
        s32 x = ((s32 *) s)[0];
        s32 y = ((s32 *) s)[1];
        s32 z = ((s32 *) s)[2];
        s32 r = ((s32 *) s)[3];
        LevelObject *end = gObjectsEnd;
        LevelObject *o;

        for (o = gObjects; o != end; o++) {
            if (math_spheres_overlap(z, r, o->pos[0], o->pos[1], x, y, o->pos[2], o->radius) == 0) {
                continue;
            }
            if (o->kind == OBJ_KIND_END && obj_end_object_hidden() != 0) {
                continue;
            }
            if (obj_is_solid_for_vehicle(o, id) != 0) {
                continue;
            }
            obj_collide_sphere(x, y, z, r, o, id);
        }
    }
    if (id != 0xFF) {
        veh_contacts_save();
    }
    if (((u16) gFreeArcStart) != 0 || ((u16) gFreeArcEnd) != 0xFFF) {
        gCollBlocked = 1;
    }
    vehicle[0x9C] = gObjHitPartDown;
    vehicle[0x9D] = gObjHit;
    vehicle[0x9E] = gObjHitImmunePart;
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/77E20/obj_collide_vehicle.s")
#endif

/* Same preserve-caller-registers convention as obj_clear_delayed_damage above - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
typedef struct {
    /* 0x00 */ s32 level; /* 0xFFFF = any level */
    /* 0x04 */ s32 key;
    /* 0x08 */ u32 mask;  /* 0 ends the table */
} Unk803059F0;            /* size 0xC */


/* gObjSolidKinds: this object kind is solid (no damage) for the vehicle (0x802BE944).
 * Returns 1 if gObjSolidKinds has an entry for the current level (or any level)
 * whose key is e->kind and whose mask has bit `bit` (taken mod 32, as sllv
 * does), else 0; always 0 when kind is 0x38. The asm takes e in t3 and bit
 * in t8, returns in t9 and saves v0, v1 and a0-a2; asm caller obj_collide_vehicle
 * keeps a0, a1, t3, t4, t6, t8, f12 and f14 live across the call. */
s32 obj_is_solid_for_vehicle(LevelObject *e, s32 bit) {
    s32 key = e->kind;
    s32 level = levelno;
    Unk803059F0 *p;

    if (key == 0x38) {
        return 0;
    }
    for (p = gObjSolidKinds; p->mask != 0; p++) {
        if ((p->level == 0xFFFF || p->level == level) && p->key == key && (p->mask & (1 << (bit & 0x1F)))) {
            return 1;
        }
    }
    return 0;
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/77E20/obj_is_solid_for_vehicle.s")
#endif

/* Same preserve-caller-registers convention as obj_clear_delayed_damage above - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING

/* Empties the (object, part) list of this collision pass (0x802BE9F8).
 * Resets the pointer gObjHitsEnd to the start of gObjHits.
 * Register note: the asm saves/restores v0 and v1; asm caller obj_collide_vehicle
 * keeps a0, a1, t6, t8, f12 and f14 live across the call. */
void obj_hits_clear(void) {
    gObjHitsEnd = gObjHits;
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/77E20/obj_hits_clear.s")
#endif

/* Same preserve-caller-registers convention as obj_clear_delayed_damage above - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
/* Appends (object, part) to this pass's list (0x802BEA30).
 * Appends the pair (a, b) to the list at gObjHits (write pointer
 * gObjHitsEnd). The asm takes a in t9 and b in t3 and saves v0 and v1; asm
 * caller obj_hit_tri keeps a0, a1, t3, t8, t9, f12 and f14 live. */
void obj_hits_add(s32 a, s32 b) {
    s32 *p = (s32 *) gObjHitsEnd;

    p[0] = a;
    p[1] = b;
    gObjHitsEnd = (u8 *) (p + 2);
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/77E20/obj_hits_add.s")
#endif

/* Same preserve-caller-registers convention as obj_clear_delayed_damage above - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
/* 1 if (object, part) is in this pass's list (0x802BEA70).
 * Returns 1 if the pair (a, b) is in the list gObjHits..gObjHitsEnd (see
 * obj_hits_add), else 0. The asm takes a in t9 and b in t3, returns in t7 and
 * saves v0, v1 and a0; asm caller obj_hit_tri keeps a0, a1, t3, t8, t9, f12
 * and f14 live. The walk stops at the write pointer with `!=`. */
s32 obj_hits_has(s32 a, s32 b) {
    s32 *p = (s32 *) gObjHits;
    s32 *end = (s32 *) gObjHitsEnd;

    while (p != end) {
        p += 2;
        if (p[-2] == a && p[-1] == b) {
            return 1;
        }
    }
    return 0;
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/77E20/obj_hits_has.s")
#endif

/* Same preserve-caller-registers convention as obj_clear_delayed_damage above - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
/* A vehicle sphere against the object's live triangles (0x802BEADC).
 * For every collision triangle of e (e->tris up to e->trisEnd, read once; walked
 * with `!=`) whose isSolid is set, runs the sphere-vs-triangle test with the
 * sphere (x, y, z, r) >> 2 (arithmetic shifts) and, on a hit, obj_hit_tri
 * (id, e, triangle).
 * Register convention: x, y, z, r in v0, v1, a0, a1, e in t3, id in t8. The
 * asm saves v0-a2, t3 and t4 and leaves t5/t6 = z >> 2 / r >> 2, t9 = e, s0/s1
 * = the triangle range (and s2-s4 and f20-f28 changed by the test helpers);
 * asm caller obj_collide_vehicle keeps a0, a1, t3, t4 and t8 live across the call.
 * The leftover registers the survey lists as read by obj_collide_vehicle's callers
 * (t6, s1, s3, s4, f12-f26) are dead there: overwritten or only saved and
 * restored. */
void obj_collide_sphere(s32 x, s32 y, s32 z, s32 r, LevelObject *e, s32 id) {
    CollisionTri *t = e->tris;
    CollisionTri *end = e->trisEnd;

    x >>= 2;
    y >>= 2;
    z >>= 2;
    r >>= 2;
    for (; t != end; t++) {
        if (t->isSolid == 0) {
            continue;
        }
        if (sphere_hits_tri_77E20(x, y, z, r, t)) {
            obj_hit_tri(id, e, t);
        }
    }
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/77E20/obj_collide_sphere.s")
#endif

/* Uses the sd-$ra frame convention - see the file-level note at the top of this file. Permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
typedef struct {
    /* 0x00 */ s32 pos[3];
    /* 0x0C */ s32 r;
    /* 0x10 */ u16 value;
    /* 0x12 */ u8 id;
    /* 0x13 */ s8 flag; /* -1 ends the table, 0 = unused */
} Unk803A6B30;           /* size 0x14 */

extern u8 gFullDamage;
extern u16 gLastHitDamage;

/* Damage to a triangle's part from the vehicle's hit spheres (0x802BEBB0).
 * Damage from the vehicle's hit spheres to triangle tri of object e. Walks the
 * gVehPartSpheres spheres (until flag -1) that are in use and belong to vehicle id,
 * counting them; the first whose sphere ((x, y, z, r) >> 2) hits tri wins
 * (none: returns). Then: stamps gObjHitFrame and records e in gHintLastHitObject /
 * gHintBuildingHit (id != 0xFF; e countable, pathBuilding clear, kind != 0x38, new),
 * obj_check_mars_fail, and unless gFailTrigger is set: adds the count to buffer A
 * (veh_contacts_add); if (e, part = tri->partNo) isn't in the hit list yet (and,
 * for a vehicle with gFullDamage clear, buffer B has the count or gCollExitProbe is
 * clear and obj_detect_rocking_hits didn't set gObjRockHit), it is added, the damage
 * (0 for kind 0x38, which also sets gEndObjectTouched for vehicle 0; else
 * obj_calc_damage) goes into the screen shake (10, min(damage, 500)), the part's
 * percentage (capped at 100) and gLastHitDamage, obj_spawn_break_effects updates the part,
 * and a part reaching 100 collapses (obj_mark_part_gone, obj_start_topple,
 * obj_start_part_fade, obj_start_sinking_part, obj_spawn_people, shake 15/400, the part's
 * triangles off and the triangles whose linkedPart is the part on unless their own
 * part is done). Finally gObjHit = 1, and unless the count is in the
 * gNonBlockingParts list: gCollHit = 1 and obj_tri_clip_free_arc(tri) while the part isn't
 * done, else gObjHitPartDown = 1; then obj_drop_unsupported_parts and obj_check_destroyed.
 * Register convention: id in t8, e in t9, tri in s0. The asm saves t3-t7,
 * s0-s4, gp and fp and leaves a1 = 0 when no sphere hits (dead: its only
 * caller obj_collide_sphere restores a1); f20-f28 are changed by the test helpers.
 * Asm caller obj_collide_sphere keeps t3-t6, t8 and t9 live across the call. */
void obj_hit_tri(s32 id, LevelObject *e, CollisionTri *tri) {
    Unk803A6B30 *p;
    u8 *pcts = (u8 *) e + 0xEC;
    s32 count = 0;
    s32 part;
    s32 value;
    s32 level;
    s8 *s;

    for (p = gVehPartSpheres;; p++) {
        if (p->flag == -1) {
            return;
        }
        if (p->flag == 0 || p->id != id) {
            continue;
        }
        count++;
        if (sphere_hits_tri_77E20(p->pos[0] >> 2, p->pos[1] >> 2, p->pos[2] >> 2, p->r >> 2, tri)) {
            break;
        }
    }

    if (id != VEH_CARRIER) {
        gObjHitFrame = gUnpausedFrames;
    }
    if (id != VEH_CARRIER && e->info->type != OBJ_TOUGH_FLIMSY && e->pathBuilding == 0 && e->kind != OBJ_KIND_END && gHintLastHitObject != e) {
        gHintBuildingHit = 1;
        gHintLastHitObject = e;
    }
    obj_check_mars_fail(e);
    if (gFailTrigger != 0) {
        return;
    }
    part = tri->partNo;
    veh_contacts_add(count);
    if (obj_hits_has((s32) e, part) != 0) {
        goto done;
    }
    if (gFullDamage == 0 && id != VEH_CARRIER) {
        if (gCollExitProbe == 0) {
            obj_detect_rocking_hits();
            if (gObjRockHit != 0) {
                goto add;
            }
        }
        if (veh_prev_contacts_has(count) != 0) {
            goto done;
        }
    }
add:
    obj_hits_add((s32) e, part);
    if (e->kind == OBJ_KIND_END) {
        if (id == VEH_DRIVER) {
            gEndObjectTouched = 1;
        }
        value = 0;
    } else {
        value = obj_calc_damage(part, p->value, id, e, e->info->type, count);
    }
    gShakeTime = 10;
    if (value < 0x1F5) {
        gShakeSize = value;
    } else {
        gShakeSize = 500;
    }
    level = pcts[part - 1] + value;
    if (level >= 100) {
        level = 100;
    }
    pcts[part - 1] = level;
    gLastHitDamage = value;
    obj_spawn_break_effects(e, part, level);
    if (level == 100) {
        CollisionTri *t;
        CollisionTri *end;

        obj_mark_part_gone(e, part);
        obj_start_topple(e, part);
        obj_start_part_fade(part, e);
        obj_start_sinking_part(part, e);
        obj_spawn_people(e);
        gShakeTime = 15;
        gShakeSize = 400;
        end = e->trisEnd;
        for (t = e->tris; t != end; t++) {
            if (t->partNo == part) {
                t->isSolid = 0;
            }
            if (t->linkedPart == part && pcts[t->partNo - 1] != 100) {
                t->isSolid = 1;
            }
        }
    }
done:
    gObjHit = 1;
    for (s = gNonBlockingParts;; s++) {
        if (*s == count) {
            goto tail;
        }
        if (*s < 0) {
            break;
        }
    }
    if (pcts[part - 1] != 100) {
        gCollHit = 1;
        obj_tri_clip_free_arc(tri);
    } else {
        gObjHitPartDown = 1;
    }
tail:
    obj_drop_unsupported_parts(e);
    obj_check_destroyed(e);
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/77E20/obj_hit_tri.s")
#endif

/* Uses the sd-$ra frame convention - see the file-level note at the top of this file. Permanently GLOBAL_ASM. */
#ifdef NON_MATCHING

/* On Mars (level 0x2D), touching an object of kind 0xE7..0xE9 fails the mission (0x802BEF9C).
 * On level 0x2D, sets gFailTrigger when e->kind is 0xE7..0xE9 (signed). The asm
 * takes e in t9 and saves v0; asm caller obj_hit_tri keeps a0, a1, t8, t9,
 * f12 and f14 live. */
void obj_check_mars_fail(LevelObject *e) {
    if (levelno == 0x2D && e->kind >= OBJ_KIND_MARS_FAIL_FIRST && e->kind < OBJ_KIND_MARS_FAIL_END) {
        gFailTrigger = 1;
    }
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/77E20/obj_check_mars_fail.s")
#endif

/* Same preserve-caller-registers convention as obj_clear_delayed_damage above - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
extern s32 gObjPrevHitFrame;  /* frame stamp: run counting only if it is gUnpausedFrames - 1 */
extern u8 gObjRockChanged;   /* "the flag changed during this run" */
extern u8 gObjRockStartFlag;   /* flag value at the start of the run */
extern u8 gObjRockLatch;   /* once-per-frame latch, set here */

/* Once per frame for the ramming vehicles: counts consecutive hit frames in which the speed's sign flips or
 * is 0; after 20 with a stick change sets gObjRockHit so the same part can be damaged again (0x802BEFF4).
 * Once-per-frame run detector. Unless the mode excludes it or it already ran
 * (latch gObjRockLatch), it counts consecutive frames (gObjPrevHitFrame + 1 ==
 * gUnpausedFrames) in which gHitSpeed's sign doesn't repeat a nonzero previous
 * sign, recording whether gPadStickX's truth value changed since the run
 * started. After 0x14 such frames the count restarts and gObjRockHit is set if
 * a change was seen. Any other frame resets the run. The asm's `addi` on
 * gObjPrevHitFrame traps on 0x7FFFFFFF; the C wraps instead.
 * Register note: the asm saves/restores v0, v1, a0 and a1 (and clobbers at);
 * asm caller obj_hit_tri keeps a0, a1, t3, t8, t9, f12 and f14 live. */
void obj_detect_rocking_hits(void) {
    u8 mode = gCurVehicle;
    s32 sign;
    s32 flag;
    s32 count;

    if (mode == 0 || mode == 1 || mode == 2 || mode == 6 || mode == 7 || mode == 9 || mode == 0xB ||
        mode == 0x10 || mode == 0x11 || mode == 0x12) {
        return;
    }
    if (gHitSpeed < 0) {
        sign = -1;
    } else if (gHitSpeed != 0) {
        sign = 1;
    } else {
        sign = 0;
    }
    if (gObjRockLatch != 0) {
        return;
    }
    gObjRockLatch = 1;

    if (gObjPrevHitFrame + 1 == gUnpausedFrames && (sign == 0 || ((s8) gObjRockSign) != sign)) {
        count = gObjRockRun;
        flag = (((u8) gPadStickX) != 0) ? 1 : 0;
        if (count == 0) {
            gObjRockStartFlag = flag;
            gObjRockChanged = 0;
        } else if (flag == 0 || flag != gObjRockStartFlag) {
            gObjRockChanged = 1;
        }
        count++; /* full width: 0xFF + 1 stores 0 but is compared as 0x100 */
        gObjRockRun = count;
        gObjRockSign = sign;
        gObjRockHit = 0;
        if (count == 0x14) {
            gObjRockRun = 0;
            if (gObjRockChanged != 0) {
                gObjRockHit = 1;
            }
        }
    } else {
        gObjRockRun = 0;
        gObjRockSign = sign;
        gObjRockHit = 0;
    }
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/77E20/obj_detect_rocking_hits.s")
#endif

/* Same preserve-caller-registers convention as obj_clear_delayed_damage above - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
/* Marks the part's triangle records gone (byte 0x13) (0x802BF1F0).
 * Sets byte 0x13 of every 0x14-byte record in e->info's floorTris..texAnimTbl table
 * whose byte 0x12 equals id (compared as a full word). The walk stops with
 * `!=`. The asm takes e in t9 and id in t3 and saves v0, v1 and a0-a2; asm
 * callers keep t3 and t9 (obj_hit_tri, coll_damage_object_part: also f12, f14),
 * t5-t7 and t9 (obj_drop_unsupported_parts) or a1, t0-t2, t4, t5 and t7 (apply_status)
 * live across the call. */
void obj_mark_part_gone(LevelObject *e, s32 id) {
    ObjectModel *info = e->info;
    u8 *p = (u8 *) info + info->floorTris;
    u8 *end = (u8 *) info + info->texAnimTbl;

    for (; p != end; p += 0x14) {
        if (p[0x12] == id) {
            p[0x13] = 1;
        }
    }
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/77E20/obj_mark_part_gone.s")
#endif

/* Same preserve-caller-registers convention as obj_clear_delayed_damage above - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING

/* For a wall triangle (isWall set), clips the free heading arc to +-0x400 around its heading, on the
 * side of the plane where gCollX/Y/Z lies (0x802BF264).
 * For a collision triangle t with isWall set: s = t's plane function at the
 * point gCollX/Y/Z >> 2 (normal . p + d, 64-bit, wrapping where the
 * asm's dadd traps). If s has the same sign as d (s > 0 with d > 0, or s < 0
 * with d <= 0), veh_clip_free_arc(h - 0x400, h + 0x400) when t->wallTwoSided is 1 or
 * t->wallSide is set; otherwise veh_clip_free_arc(h + 0x400, h - 0x400) when
 * t->wallTwoSided is 1 or t->wallSide is clear (h = t->heading). The asm takes t in s0
 * and saves v0-a3 and t0; asm callers keep a1, t9, f12, f14 (obj_hit_tri)
 * or a1, t3-t6, t9 (coll_probe_object_tris) live across the call. */
void obj_tri_clip_free_arc(CollisionTri *t) {
    s64 s;
    s32 h;

    if (t->isWall == 0) {
        return;
    }
    s = t->normal[0] * (gCollX >> 2) + t->normal[1] * (gCollY >> 2) + t->normal[2] * (gCollZ >> 2) +
        t->d;
    h = t->heading;
    if ((t->d > 0) ? (s > 0) : (s < 0)) {
        /* same side as d */
        if (t->wallTwoSided == 1 || t->wallSide != 0) {
            veh_clip_free_arc(h - 0x400, h + 0x400);
        }
    } else {
        if (t->wallTwoSided == 1 || t->wallSide == 0) {
            veh_clip_free_arc(h + 0x400, h - 0x400);
        }
    }
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/77E20/obj_tri_clip_free_arc.s")
#endif

/* Same preserve-caller-registers convention as obj_clear_delayed_damage above - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
/* 20460.c defines it with s16 coordinates and a u8; the asm passes the
 * unsliced shifted words, so it is declared with full words here. */

/* First part destroyed: the people inside run out (people_spawn) (0x802BF384).
 * If exactly one of e's numParts parts is done (partPct byte == 100), calls
 * people_spawn(x >> 5, y >> 5, z >> 5, unkC >> 5, cell, info->numPeople), the
 * shifts logical. The asm takes e in t9 and preserves every register (all of
 * them saved around the call); asm callers keep a0, t3, t5, t9, f12 and f14
 * (obj_update_delayed_damage), t3, t9, f12 and f14 (obj_hit_tri, coll_damage_object_part) live. */
void obj_spawn_people(LevelObject *e) {
    s32 n;
    s32 count = 0;
    u8 *p = e->partPct;

    for (n = e->numParts; n != 0; n--) {
        if (*p++ == 100) {
            count++;
        }
    }
    if (count == 1) {
        people_spawn((u32) e->pos[0] >> 5, (u32) e->pos[1] >> 5, (u32) e->pos[2] >> 5, (u32) e->radius >> 5,
                      e->cell, e->info->numPeople);
    }
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/77E20/obj_spawn_people.s")
#endif

/* Uses the sd-$ra frame convention - see the file-level note at the top of this file. Permanently GLOBAL_ASM. */
#ifdef NON_MATCHING

/* All parts down: destroyed, value message and score, zones off (0x802BF534).
 * Unless e is already finished (destroyed), finishes it when every part other
 * than the 1-based part info->immunePart is either done (partPct byte == 100) or has
 * a nonzero partFalling halfword: sets destroyed, posts info->value as message
 * gValuePopupNew and adds it to gLevelMoney, and clears byte 0x12 of every 0x24-byte
 * record at gAlphaZones (up to gAlphaZonesEnd, walked with `!=`) whose byte 0x11 is
 * e's 1-based index in gObjects. The asm takes e in t9 and saves t4-t7, s0
 * and s1; asm callers keep a0, t3, t5, t9, f12 and f14 (obj_update_delayed_damage) or a1,
 * f12 and f14 (obj_hit_tri) live. */
void obj_check_destroyed(LevelObject *e) {
    s32 skip;
    s32 part;
    s32 n;
    u8 *p;
    u16 *q;
    s32 value;
    u32 index;
    u8 *r;
    u8 *end;

    if (e->destroyed != 0) {
        return;
    }
    skip = e->info->immunePart;
    p = e->partPct;
    q = e->partFalling;
    for (part = 0, n = e->numParts; n != 0; n--, p++, q++) {
        part++;
        if (part != skip && *p != 100 && *q == 0) {
            return;
        }
    }
    e->destroyed = 1;
    value = e->info->value;
    gValuePopupNew = value;
    gLevelMoney += value;
    index = (u32) ((u8 *) e - (u8 *) gObjects) / 0xFC + 1;
    end = gAlphaZonesEnd;
    for (r = gAlphaZones; r != end; r += 0x24) {
        if (r[0x11] == index) {
            r[0x12] = 0;
        }
    }
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/77E20/obj_check_destroyed.s")
#endif

/* Uses the sd-$ra frame convention - see the file-level note at the top of this file. Permanently GLOBAL_ASM. */
#ifdef NON_MATCHING

/* The u16 at byte offset `off` of entry e, for part index i (0-based; a part
 * byte of 0 gives -1, as in the asm). Four tables of 16: 0x48 = "fallen"
 * flag, 0x88/0xA8/0xC8 = x, y, z. */
#define ENTRY_U16(e, off, i) (*(u16 *) ((u8 *) (e) + (off) + (i) * 2))

/* Parts whose supports' weights fall below the threshold start falling (0x802BF668).
 * Makes parts of e fall. Each record of info's supportRecs..supportRecsEnd list is
 * {u8 part; u8 threshold; u8 n; n x {u8 support; u8 weight}} (parts 1-based).
 * A part that hasn't fallen (u16 flag at 0x48 clear) and isn't done (partPct
 * byte != 100) falls when the weights of its supports that are neither done
 * nor fallen sum to less than the threshold: obj_mark_part_gone(e, part), its flag
 * is set to 1 and gPartJitterX/Y/Z are stored as its x, y, z; then each of
 * e's triangles (tris..trisEnd) with partNo == part has isSolid cleared, and one
 * with linkedPart == part gets isSolid = 1 unless the part its partNo names is done.
 * Repeats the whole pass until nothing falls. All walks stop with `!=`. The
 * asm takes e in t9 and saves t3-t7 and s0-s7; asm callers keep a0, t3, t5,
 * t9, f12, f14 (obj_update_delayed_damage), a1, t9, f12, f14 (obj_hit_tri) or t9
 * (coll_damage_object_part) live across the call. */
void obj_drop_unsupported_parts(LevelObject *e) {
    u8 *b = (u8 *) e;
    ObjectModel *info;
    u8 *rec;
    u8 *end;
    u8 *p;
    CollisionTri *r;
    CollisionTri *rend;
    s32 changed;
    s32 part;
    s32 n;
    s32 sum;

    do {
        info = e->info;
        changed = 0;
        rec = (u8 *) info + info->supportRecs;
        end = (u8 *) info + info->supportRecsEnd;
        while (rec != end) {
            part = rec[0] - 1;
            if (ENTRY_U16(e, 0x48, part) != 0 || b[0xEC + part] == 100) {
                rec += rec[2] * 2 + 3;
                continue;
            }
            sum = 0;
            for (n = rec[2], p = rec + 3; n != 0; n--, p += 2) {
                if (b[0xEB + p[0]] != 100 && ENTRY_U16(e, 0x48, p[0] - 1) == 0) {
                    sum += p[1];
                }
            }
            if (sum < rec[1]) {
                changed = 1;
                obj_mark_part_gone(e, rec[0]);
                part = rec[0] - 1;
                ENTRY_U16(e, 0x48, part) = 1;
                ENTRY_U16(e, 0x88, part) = ((u16) gPartJitterX);
                ENTRY_U16(e, 0xA8, part) = ((u16) gPartJitterY);
                ENTRY_U16(e, 0xC8, part) = ((u16) gPartJitterZ);
                part++;
                for (r = e->tris, rend = e->trisEnd; r != rend; r++) {
                    if (r->partNo == part) {
                        r->isSolid = 0;
                    }
                    if (r->linkedPart == part && b[0xEB + r->partNo] != 100) {
                        r->isSolid = 1;
                    }
                }
            }
            rec = p;
        }
    } while (changed);
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/77E20/obj_drop_unsupported_parts.s")
#endif

/* Same preserve-caller-registers convention as obj_clear_delayed_damage above - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
extern u16 gLastHitDamage;
void obj_spawn_random_effects(LevelObject *e, s32 index, s32 level, ObjectModel *info, s32 *obj);

/* The part's effect records reached by its damage level (0x802BF898).
 * Breaks off part `part` (1-based) of e at damage level `level`. If e->info's
 * debrisScale is nonzero, obj_spawn_random_effects makes generic debris; the asm
 * passes it info and, as its obj (v1), the debrisScale byte itself. Otherwise, for
 * each of info's 0x38-byte records breakEffects..landEffects (walked with `!=`) that isn't
 * done yet (byte 0x34 == 0) and belongs to the part (byte 0x32): with byte
 * 0x33 == 0xFF it is spawned (obj_queue_effect) unless gLastHitDamage < 20, and
 * stays not done; otherwise, if byte 0x33 (the level needed) <= level
 * (signed), obj_start_emitter + obj_queue_effect and it is marked done.
 * The asm takes e in t9, part in t3, level in t5 and preserves every
 * register; asm callers keep a1, t3, t5, t9, f12, f14 (obj_hit_tri) and
 * t3, t5, t9 (coll_damage_object_part) live (a mixed N64 build would need a thunk). */
void obj_spawn_break_effects(LevelObject *e, s32 part, s32 level) {
    ObjectModel *info = e->info;
    u8 *r;
    u8 *end;

    if (info->debrisScale != 0) {
        obj_spawn_random_effects(e, part, level, info, (s32 *) (u32) info->debrisScale);
        return;
    }
    r = (u8 *) info + info->breakEffects;
    end = (u8 *) info + info->landEffects;
    for (; r != end; r += 0x38) {
        if (r[0x34] != 0 || r[0x32] != part) {
            continue;
        }
        if (r[0x33] == 0xFF) {
            if (gLastHitDamage >= 20) {
                obj_queue_effect((u32 *) r);
            }
        } else if (!(level < r[0x33])) {
            obj_start_emitter(e, (s32 *) r);
            obj_queue_effect((u32 *) r);
            r[0x34] = 1;
        }
    }
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/77E20/obj_spawn_break_effects.s")
#endif

/* Uses the sd-$ra frame convention - see the file-level note at the top of this file. Permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
/* Debris kinds by a 0..100 roll: the first record whose threshold is above
 * the roll wins (scans aren't bounded; the ROM tables end at 100). */
typedef struct {
    /* 0x00 */ u16 threshold;
    /* 0x02 */ u16 kind;  /* stored as the record's byte 0x31 */
    /* 0x04 */ s32 lo;    /* range of the second roll */
    /* 0x08 */ s32 hi;
} Unk80306344;            /* size 0xC */

extern Unk80306344 gObjDebrisKinds[];
extern Unk80306344 gObjDebrisKindsFull[];
extern Unk80306344 gObjLandingFxKinds[];
extern u8 gObjDebrisCounts[]; /* {u8 threshold; u8 count} pairs */
extern u16 gLastHitDamage;

#ifdef PORT_HOST
/* The roll is 0..100 inclusive, and every kind table ends at threshold 100:
 * a roll of 100 walks on (gObjDebrisKinds -> gObjDebrisKindsFull -> gObjLandingFxKinds) into
 * gObjDebrisCounts's {u8, u8} pairs, which the N64 reads as an Unk80306344
 * (threshold 0x3201, kind 0x5502 -> byte 2, lo 0x64030000, hi 0). The port
 * keeps those bytes as bytes, so it reads the fields there big-endian. */
static s32 port_kind_field(Unk80306344 *t, s32 off, s32 size) {
    u8 *p = (u8 *) t + off;

    if (p >= gObjDebrisCounts && p + size <= gObjDebrisCounts + 0x10) {
        return size == 2 ? (p[0] << 8) | p[1] : (p[0] << 24) | (p[1] << 16) | (p[2] << 8) | p[3];
    }
    return size == 2 ? *(u16 *) p : *(s32 *) p;
}
#define KT_THRESHOLD(t) ((u16) port_kind_field((t), 0, 2))
#define KT_KIND(t) ((u16) port_kind_field((t), 2, 2))
#define KT_LO(t) port_kind_field((t), 4, 4)
#define KT_HI(t) port_kind_field((t), 8, 4)
#else
#define KT_THRESHOLD(t) ((t)->threshold)
#define KT_KIND(t) ((t)->kind)
#define KT_LO(t) ((t)->lo)
#define KT_HI(t) ((t)->hi)
#endif

/* Random effects (model debrisScale) at the part's point (0x802BF978).
 * Spawns debris for part `index` (1-based) of e: the part's s16 point in
 * e->info's partPoints table (8-byte records) << 16 is the position. A 0..100 roll
 * (obj_rand_range) picks a count from gObjDebrisCounts (first pair whose threshold
 * isn't below the roll); for each of them, with `level` >= 100 the kind table
 * is gObjDebrisKindsFull, else gObjDebrisKinds, but then nothing more happens (not even the
 * final call) if gLastHitDamage < 20. Another roll picks the kind record, a third
 * roll in [lo, hi] times info->debrisScale (doubled if gCarrierExplodeDone) becomes the
 * record's unkC, and the record (gEffectScratch; u16 at 0x2C = 10 for all but the
 * first) goes to obj_queue_effect. Finally obj_start_emitter(e, obj), where obj is
 * gEffectScratch once a record was made, else the caller's obj (the asm's v1).
 * The asm takes e in t9, index in t3, level in t5, info in v0 (the caller
 * passes e->info) and obj in v1; it preserves every register. */
void obj_spawn_random_effects(LevelObject *e, s32 index, s32 level, ObjectModel *info, s32 *obj) {
    ObjectModel *ei = e->info;
    s16 *pt = (s16 *) ((u8 *) ei + ei->partPoints + (index - 1) * 8);
    s32 x = pt[0] << 16;
    s32 y = pt[1] << 16;
    s32 z = pt[2] << 16;
    EffectSpawn *d = &gEffectScratch;
    Unk80306344 *t;
    u8 *c = gObjDebrisCounts;
    s32 *last = obj; /* the asm's v1 (a local: don't write the stack argument) */
    s32 roll;
    s32 count;
    s32 i;
    u32 v;

    roll = obj_rand_range(0, 100);
    while (c[0] < roll) {
        c += 2;
    }
    count = c[1];
    for (i = 0; i != count; i++) {
        if (level >= 100) {
            t = gObjDebrisKindsFull;
        } else {
            t = gObjDebrisKinds;
            if (gLastHitDamage < 20) {
                return;
            }
        }
        roll = obj_rand_range(0, 100);
        while (!(roll < KT_THRESHOLD(t))) {
            t++;
        }
        d->pos[0] = x;
        d->pos[1] = y;
        d->pos[2] = z;
        roll = obj_rand_range(KT_LO(t), KT_HI(t));
        v = PORT_WRAP_MUL(info->debrisScale, roll);
        if (gCarrierExplodeDone != 0) {
            v <<= 1;
        }
        d->unkC = v;
        d->unk10[0] = 0;
        d->unk10[1] = 0;
        d->unk10[2] = 0;
        d->unk10[3] = 0;
        d->unk10[4] = 0;
        d->unk10[5] = 0;
        d->unk28 = 0xFC180000;
        d->unk2C = (i != 0) ? 10 : 0;
        d->unk2E = 0;
        d->unk30 = 0;
        d->unk34 = 0;
        d->unk35 = 0;
        d->kind = KT_KIND(t);
        obj_queue_effect((u32 *) d);
        last = (s32 *) d;
    }
    obj_start_emitter(e, last);
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/77E20/obj_spawn_random_effects.s")
#endif

/* Same preserve-caller-registers convention as obj_clear_delayed_damage above - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING

/* Util_rand_range(lo, hi) for the asm callers (0x802BFB50).
 * util_rand_range(lo, hi) (a random number in a range) for asm callers: the asm
 * passes a0/a1 through, returns the result in s4 and preserves every other
 * register; asm callers keep t5-t7 and t9 (obj_spawn_random_effects), t7
 * (obj_spawn_random_landing_effect) or t0, t1 and t4 (obj_draw_sinking_part) live. */
s32 obj_rand_range(s32 lo, s32 hi) {
    return util_rand_range(lo, hi);
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/77E20/obj_rand_range.s")
#endif

/* Same preserve-caller-registers convention as obj_clear_delayed_damage above - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
extern s8 gObjEmitterFxKinds[]; /* -1 terminated */

/* Effect ids listed in gObjEmitterFxKinds start the particle emitter (0x802BFBF4).
 * Unless e's info type is 1, and if obj's byte 0x31 is in the list
 * gObjEmitterFxKinds (s8 entries compared with the u8, so 0x80..0xFE never match),
 * calls ptcl_start((low word of gFrameDrawTime >> 8) % 4, obj x, y, z >> 11
 * (logical), e->pos[1]). The asm takes e in t9 and obj in v1 and preserves
 * every register; asm caller obj_spawn_break_effects keeps a0, t3, t5 and t9 live. */
void obj_start_emitter(LevelObject *e, s32 *obj) {
    s8 *p;
    s32 key;

    if (e->info->type == OBJ_TOUGH_FLIMSY) {
        return;
    }
    key = ((u8 *) obj)[0x31];
    for (p = gObjEmitterFxKinds; *p != -1; p++) {
        if (*p == key) {
            ptcl_start(((u32) gFrameDrawTime >> 8) % 4, (u32) obj[0] >> 11, (u32) obj[1] >> 11,
                          (u32) obj[2] >> 11, e->pos[1]);
            return;
        }
    }
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/77E20/obj_start_emitter.s")
#endif

/* Same preserve-caller-registers convention as obj_clear_delayed_damage above - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
void obj_spawn_random_landing_effect(LevelObject *e, s32 index);

/* A fallen part hit the ground: its landing effects (0x802BFD1C).
 * Spawns the debris of part index + 1 of e: with e->info's debrisScale nonzero, one
 * generic record (obj_spawn_random_landing_effect); else every one of info's 0x38-byte records
 * landEffects..partDlRecs (walked with `!=`) whose byte 0x32 is the part goes to
 * obj_queue_effect. The asm takes e in v0 and index in a3 (addi: index + 1 must
 * not overflow) and saves v0, v1, a0, a1, a3; asm caller obj_draw_all keeps
 * a0-a3 and t0-t6 live (a mixed N64 build would need a thunk). */
void obj_spawn_landing_effects(LevelObject *e, s32 index) {
    ObjectModel *info = e->info;
    s32 part = index + 1;
    u8 *r;
    u8 *end;

    if (info->debrisScale != 0) {
        obj_spawn_random_landing_effect(e, part);
        return;
    }
    r = (u8 *) info + info->landEffects;
    end = (u8 *) info + info->partDlRecs;
    for (; r != end; r += 0x38) {
        if (r[0x32] == part) {
            obj_queue_effect((u32 *) r);
        }
    }
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/77E20/obj_spawn_landing_effects.s")
#endif

/* Uses the sd-$ra frame convention - see the file-level note at the top of this file. Permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
/* One random landing effect (gObjLandingFxKinds) at the part's point (0x802BFDAC).
 * One debris record for part `index` (1-based) of e (see obj_spawn_random_effects): x, z
 * from the part's s16 point in e->info's partPoints table (<< 16), y = e->pos[1]
 * << 11; the kind record is picked from gObjLandingFxKinds by a 0..100 roll
 * (obj_rand_range), and a roll in its [lo, hi] times info->debrisScale is the unkC.
 * The record (gEffectScratch) goes to obj_queue_effect. The asm takes e in v0 and
 * index in a3 (its caller passes the 0-based index + 1) and preserves every
 * register. */
void obj_spawn_random_landing_effect(LevelObject *e, s32 index) {
    ObjectModel *info = e->info;
    s16 *pt = (s16 *) ((u8 *) info + info->partPoints + (index - 1) * 8);
    s32 x = pt[0];
    s32 z = pt[2];
    s32 y = e->pos[1];
    EffectSpawn *d = &gEffectScratch;
    Unk80306344 *t = gObjLandingFxKinds;
    s32 roll;

    roll = obj_rand_range(0, 100);
    while (!(roll < KT_THRESHOLD(t))) {
        t++;
    }
    d->pos[0] = x << 16;
    d->pos[1] = y << 11;
    d->pos[2] = z << 16;
    roll = obj_rand_range(KT_LO(t), KT_HI(t));
    d->unk10[0] = 0;
    d->unk10[1] = 0;
    d->unk10[2] = 0;
    d->unk10[3] = 0;
    d->unk10[4] = 0;
    d->unk10[5] = 0;
    d->unk2C = 0;
    d->unk2E = 0;
    d->unk30 = 0;
    d->unkC = info->debrisScale * roll;
    d->unk28 = 0xFC180000;
    d->unk34 = 0;
    d->unk35 = 0;
    d->kind = KT_KIND(t);
    obj_queue_effect((u32 *) d);
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/77E20/obj_spawn_random_landing_effect.s")
#endif

/* Same preserve-caller-registers convention as obj_clear_delayed_damage above - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
extern u8 gFullDamage;

/* GFullDamage from the vehicle's damage mode and wheel-slot flags (0x802BFEE4).
 * gFullDamage = 0 if gVehDamageMode is 1; else 1 if gVehDamageMode is 2 or any of the
 * vehicle's bytes 0x96..0x98 is 1; else 0. The asm takes the vehicle in gp
 * (the vehicle update functions point gp at their own gRamdozerState /
 * gBacklashState block, so it is an argument here, not a fixed base) and saves v0
 * and v1; asm caller obj_collide_vehicle keeps a0, a1, t6, t8, f12 and f14 live. */
void obj_set_full_damage(u8 *vehicle) {
    if (gVehDamageMode == 1) {
        gFullDamage = 0;
    } else if (gVehDamageMode == 2 || vehicle[0x96] == 1 || vehicle[0x97] == 1 || vehicle[0x98] == 1) {
        gFullDamage = 1;
    } else {
        gFullDamage = 0;
    }
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/77E20/obj_set_full_damage.s")
#endif

/* Same preserve-caller-registers convention as obj_clear_delayed_damage above - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
extern u8 gObjHitImmunePart;
extern u8 gObjDamageRules[];   /* records {u8 bit; u8 key; u8 kind; u8 n; ...}, see below */
s32 obj_full_damage_rule(s32 bit, s32 flag, s32 *out);
s32 obj_part_exposed(s32 id, LevelObject *e);

/* A part's damage from the vehicle, its value and the toughness rules (0x802BFF6C).
 * Damage amount for part `id` of e hit with `value` under rule (bit, key):
 *   bit 0xFF: 100 if flag is 1, else 0 (nothing stored);
 *   otherwise gDigHit = 1, gDigHitVehicle = bit, then: the part info->immunePart is
 *   immune (gObjHitImmunePart = 1, gDigHit = 0, 0); obj_full_damage_rule(bit, flag)
 *   hit: 100; gFullDamage set: 100 (gDigHitDamage = gDigHitDamagePerPart = 100);
 *   else the first gObjDamageRules record for (bit, key) that applies gives r:
 *     0xFC (if gVehAttacking) / 0xFB (if obj_part_exposed(id, e)): n * value;
 *     0xFF (if gHitSpeed >= 0) / 0xFE (if <= 0) / 0xFD (always):
 *       |gHitSpeed| * value * n >> 4 (logical);
 *     any other kind k: anim_get_state(k, gCollVehicleAnim) record; if its first
 *       field is nonzero, with o = its 7th field (s8) and lo, hi the bytes at
 *       rec + o + 4 and + 5: (round(f * (hi - lo)) + lo) * value, f its float;
 *       else the record (n extra bytes) is skipped.
 *   The table walk isn't bounded: (bit, key) must have a record that applies
 *   (the ROM table ends each key's list with an 0xFD record). Then
 *   gDigHitDamage = r, gDigHitDamagePerPart = r / flag (unsigned; the asm traps on 0) and
 *   r / flag is returned. Products are 32-bit (multu low word).
 * The asm takes id in t3, value in t7, bit in t8, e in t9, flag in gp and key
 * in fp (all values), returns in t7 and saves v0-t4; asm caller obj_hit_tri
 * keeps a0, a1, t3, t9, f12 and f14 live across the call.
 * Register carry-over kept: a regular-kind record passes its kind to
 * anim_get_state in v0, which the asm doesn't restore, so a later 0xFB record
 * of the same key gets that kind as its id. Not reproduced: obj_part_exposed
 * leaves v0 doubled when it reaches its height test, which only a second 0xFB
 * record of the same key would see (the ROM table has none). */
s32 obj_calc_damage(s32 id, s32 value, s32 bit, LevelObject *e, s32 flag, s32 key) {
    s32 out = value;
    s32 v0 = id;
    u8 *rec;
    s32 kind;
    s32 o[8];
    s32 m;
    u32 r;

    if (bit == VEH_CARRIER) {
        return (flag == 1) ? 100 : 0;
    }
    gDigHit = 1;
    gDigHitVehicle = bit;
    if (e->info->immunePart == id) {
        gObjHitImmunePart = 1;
        gDigHit = 0;
        return 0;
    }
    if (obj_full_damage_rule(bit, flag, &out) != 0) {
        return out;
    }
    if (gFullDamage != 0) {
        gDigHitDamage = 100;
        gDigHitDamagePerPart = 100;
        return 100;
    }
    rec = gObjDamageRules;
    for (;;) {
        kind = rec[2];
        if (rec[0] == bit && rec[1] == key) {
            if (kind == DMG_RULE_ATTACK) {
                if (gVehAttacking != 0) {
                    r = (u32) rec[3] * out;
                    break;
                }
            } else if (kind == DMG_RULE_EXPOSED) {
                if (obj_part_exposed(v0, e) != 0) {
                    r = (u32) rec[3] * out;
                    break;
                }
            } else if (kind == DMG_RULE_SPEED_FWD || kind == DMG_RULE_SPEED_REV || kind == DMG_RULE_SPEED) {
                if ((kind == DMG_RULE_SPEED_FWD && gHitSpeed < 0) || (kind == DMG_RULE_SPEED_REV && gHitSpeed > 0)) {
                    rec += 4;
                    continue;
                }
                m = gHitSpeed;
                if (m < 0) {
                    m = -m;
                }
                r = ((u32) m * out * rec[3]) >> 4;
                break;
            } else {
                v0 = kind;
                anim_get_state(kind, (AnimChannel *) gCollVehicleAnim, o);
                if (o[0] != 0) {
                    u8 *q = rec + o[6];
                    s32 lo = q[4];

                    CVT_W_S(m, ((f32 *) o)[7] * (f32) (q[5] - lo));
                    r = (u32) (m + lo) * out;
                    break;
                }
                rec += rec[3] + 4;
                continue;
            }
        }
        if (kind == DMG_RULE_ATTACK || kind == DMG_RULE_EXPOSED || kind == DMG_RULE_SPEED || kind == DMG_RULE_SPEED_FWD || kind == DMG_RULE_SPEED_REV) {
            rec += 4;
        } else {
            rec += rec[3] + 4;
        }
    }
    gDigHitDamage = r;
    r = r / (u32) flag;
    gDigHitDamagePerPart = r;
    return r;
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/77E20/obj_calc_damage.s")
#endif

/* Same preserve-caller-registers convention as obj_clear_delayed_damage above - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
typedef struct {
    /* 0x00 */ s32 level;
    /* 0x04 */ u32 mask; /* 0 ends the table */
} Unk80305E10;

extern Unk80305E10 gObjBlockedFullDamage[];

/* The cases worth 100: push run, level/vehicle mask, flimsy objects (0x802C0284).
 * Returns 1, and sets *out = 100, when any of these holds (else returns 0 and
 * leaves *out alone): gObjRockHit is set (it is cleared, and nothing else is
 * checked); in the frame after gBlockedFrame (gBlockedFrame + 1 == gUnpausedFrames), a
 * gObjBlockedFullDamage entry of the current level has bit `bit` (mod 32) in its mask;
 * or flag is 1 and bit is 1, 10 or 16. The asm takes bit in t8 and flag in gp
 * (a value here), returns in t2 and t7 (100, written only with a 1) and saves
 * v0, v1 and a0; asm caller obj_calc_damage keeps t8 and t9 live. The asm's
 * `addi` on gBlockedFrame traps on 0x7FFFFFFF; the C wraps instead. */
s32 obj_full_damage_rule(s32 bit, s32 flag, s32 *out) {
    Unk80305E10 *p;
    s32 ret = 0;

    if (gObjRockHit != 0) {
        *out = 100;
        gObjRockHit = 0;
        return 1;
    }
    if (gBlockedFrame + 1 == gUnpausedFrames) {
        for (p = gObjBlockedFullDamage; p->mask != 0; p++) {
            if ((p->mask & (1 << (bit & 0x1F))) && p->level == levelno) {
                ret = 1;
                *out = 100;
                break;
            }
        }
    }
    if (bit == 1 && flag == 1) {
        ret = 1;
        *out = 100;
    }
    if (bit == 10 && flag == 1) {
        ret = 1;
        *out = 100;
    }
    if (bit == 16 && flag == 1) {
        ret = 1;
        *out = 100;
    }
    return ret;
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/77E20/obj_full_damage_rule.s")
#endif

/* Uses the sd-$ra frame convention - see the file-level note at the top of this file. Permanently GLOBAL_ASM. */
#ifdef NON_MATCHING

/* 0 if an intact support covers the part or it is too high (0x802C038C).
 * Returns 0 if part `id` of e is blocked, else 1. Blocked: a record in
 * e->info's supportRecs..supportRecsEnd list ({u8 part; u8 pad; u8 n; n x {u8 id; u8 pad}})
 * lists id (compared as a full word) while e's partPct byte for its 1-based
 * part isn't 100 (part 0 reads pathBuilding); or the s16 height
 * info->partHeights[id - 1] << 5 is above gCollY + 0x578. A record whose part is
 * done skips the rest of its ids. The record walk stops with `!=`. The asm
 * takes id in v0 and e in t9, returns in t3 (and leaves v0 doubled when it
 * gets to the height test); it saves v1, a0-a3, t0-t2 and t4. Asm caller
 * obj_calc_damage keeps t2, t4, t7, t8 and t9 live. The asm's `addi` on
 * gCollY traps on overflow; the C wraps. */
s32 obj_part_exposed(s32 id, LevelObject *e) {
    ObjectModel *info = e->info;
    u8 *rec = (u8 *) info + info->supportRecs;
    u8 *end = (u8 *) info + info->supportRecsEnd;
    s32 n;
    s32 k;
    u8 *p;

    while (rec != end) {
        n = rec[2];
        for (k = n, p = rec + 3; k != 0; k--, p += 2) {
            if (*p == id) {
                if (((u8 *) e)[0xEB + rec[0]] == 100) {
                    break;
                }
                return 0;
            }
        }
        rec += n * 2 + 3;
    }
    if (gCollY + 0x578 < *(s16 *) ((u8 *) info + info->partHeights + id * 2 - 2) << 5) {
        return 0;
    }
    return 1;
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/77E20/obj_part_exposed.s")
#endif

/* Same preserve-caller-registers convention as obj_clear_delayed_damage above - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
extern u8 gEffectQueue[30][0x38];

/* Frees the 30 gEffectQueue slots (0x802C049C).
 * Sets byte 0x34 of each of the 30 0x38-byte entries of gEffectQueue to 1.
 * Register note: the asm saves/restores v0, v1 and a0; asm callers keep
 * registers live across the call (initlevel: a2, a3, t0, t1, t2, f12,
 * f14; carrier_check_crash: a0, a2, a3, t6, t7, f12, f14; commpt_burst: t1). */
void obj_clear_effect_queue(void) {
    s32 i;

    for (i = 0; i < 30; i++) {
        gEffectQueue[i][0x34] = 1;
    }
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/77E20/obj_clear_effect_queue.s")
#endif

/* Uses the sd-$ra frame convention - see the file-level note at the top of this file. Permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
/* Copies a 0x38-byte effect record into a free gEffectQueue slot (0x802C04F0).
 * Copies the 0x38-byte record src (word by word) into the first of the 30
 * gEffectQueue entries whose byte 0x34 is set (see obj_clear_effect_queue); does nothing
 * if there is none. The asm takes src in v1 and saves v1 and a0-a2; asm
 * callers keep registers live across the call (obj_spawn_break_effects: a0, t3, t5,
 * t9; obj_spawn_random_effects: t5-t7, t9; obj_spawn_landing_effects: a0, a3; commpt_burst:
 * a1-a3). */
void obj_queue_effect(u32 *src) {
    u8 *e = gEffectQueue[0];
    u32 *dst;
    s32 i;
    s32 k;

    for (i = 30; i != 0; i--, e += 0x38) {
        if (e[0x34] != 0) {
            for (dst = (u32 *) e, k = 0x38; k != 0; k -= 4) {
                *dst++ = *src++;
            }
            return;
        }
    }
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/77E20/obj_queue_effect.s")
#endif

/* Same preserve-caller-registers convention as obj_clear_delayed_damage above - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING


extern u8 * N64P gObjEffectDefLists[];  /* per kind: {u8 n; u8 defIndex[n]} */

/* Counts the queued effects down; spawns them, sound, blast damage (0x802C0574).
 * Runs the 30 pending debris records of gEffectQueue (0x38 bytes each): a
 * record with byte 0x34 set is free; one whose u16 delay at 0x2C is nonzero
 * just counts it down. Otherwise the effect definition is picked from kind
 * r[0x31]'s list in gObjEffectDefLists by (low word of gFrameDrawTime >> 4) % n (n == 0
 * would trap) and spawned with fx_spawn (type 0, the record's words as
 * position/data/fields, tag 1 for kind 0x15 or when gPartSinking is set or
 * gWaterOn is set with gWaterLayer == 1, else 0). If that succeeded and
 * neither the u64 gNextGameMode nor gCarrierHit is set, snd_play_effect(kind)
 * (the asm saves every register around that call). The record is then
 * freed, and a nonzero u16 radius at 0x2E applies obj_blast_damage blast
 * damage at its position with amount r[0x30] << 4. Saves s0-s7. */
void obj_run_effect_queue(void) {
    u8 *r = gEffectQueue[0];
    s32 n;

    for (n = 30; n != 0; n--, r += 0x38) {
        Io802A6274 io;
        u8 *list;
        u8 *def;
        s32 tag;

        if (r[0x34] != 0) {
            continue;
        }
        if (*(u16 *) (r + 0x2C) != 0) {
            *(u16 *) (r + 0x2C) -= 1;
            continue;
        }
        list = gObjEffectDefLists[r[0x31]];
        def = gFxDefsByKind[list[1 + ((u32) gFrameDrawTime >> 4) % list[0]]];
        if (r[0x31] == EFFECT_KIND_COMMPT_BURST || gPartSinking != 0 || (gWaterOn != 0 && gWaterLayer == 1)) {
            tag = 1;
        } else {
            tag = 0;
        }
        io.a3 = tag;
        io.t6 = *(s32 *) (r + 0x10);
        io.s1 = *(s32 *) (r + 0x1C);
        if (fx_spawn(&io, def, *(s32 *) (r + 0x0C), 0, *(s32 *) (r + 0x00), *(s32 *) (r + 0x04),
                          *(s32 *) (r + 0x08), *(s32 *) (r + 0x14), *(s32 *) (r + 0x18), *(s32 *) (r + 0x20),
                          *(s32 *) (r + 0x24), *(s32 *) (r + 0x28), r[0x35]) != 0
            && gNextGameMode == 0 && gCarrierHit == 0) {
            snd_play_effect(r[0x31]);
        }
        r[0x34] = 1;
        if (*(u16 *) (r + 0x2E) != 0) {
            obj_blast_damage(*(s32 *) (r + 0x00), *(s32 *) (r + 0x04), *(s32 *) (r + 0x08), *(u16 *) (r + 0x2E),
                          r[0x30] << 4);
        }
    }
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/77E20/obj_run_effect_queue.s")
#endif

/* Uses the sd-$ra frame convention - see the file-level note at the top of this file. Permanently GLOBAL_ASM. */
#ifdef NON_MATCHING

/* GFadingParts: alpha -20 a frame, drawn while above 0 (0x802C08C4).
 * For each of the 4 gFadingParts entries: with its s16 at 0x4B0 zero, counts
 * its byte at 0x4B2 down to 0; otherwise lowers the s16 by 20 and, if that
 * leaves it <= 0, clears it and sets the byte to 3, else draws the entry
 * (G_RDPPIPESYNC, G_SETENVCOLOR with the new value as its colour word, and a
 * G_DL call to the entry itself as a physical address). Ends the list with 0xB8 (end display list) and returns the new
 * end. The asm takes and returns gfx in v0 and saves v1 and a0-a3; asm caller
 * obj_draw_all keeps a0-a3, f12 and f14 live. */
u32 *obj_draw_fading_parts(u32 *gfx) {
    u8 *e = gFadingParts;
    s32 i;
    s32 h;

    for (i = 4; i != 0; i--, e += 0x4B8) {
        h = *(s16 *) (e + 0x4B0);
        if (h == 0) {
            if (e[0x4B2] != 0) {
                e[0x4B2]--;
            }
            continue;
        }
        h -= 20;
        if (h <= 0) {
            *(s16 *) (e + 0x4B0) = 0;
            e[0x4B2] = 3;
            continue;
        }
        *(s16 *) (e + 0x4B0) = h;
        gfx[0] = 0xE7000000;
        gfx[1] = 0;
        gfx[2] = 0xFB000000;
        gfx[3] = h;
        gfx[4] = 0x06000000;
        gfx[5] = (u32) e - 0x80000000;
        gfx += 6;
    }
    PORT_ITAG(gfx, PORT_ITAG_NONE, 0);
    gfx[0] = 0xB8000000;
    gfx[1] = 0;
    return gfx + 2;
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/77E20/obj_draw_fading_parts.s")
#endif

/* Same preserve-caller-registers convention as obj_clear_delayed_damage above - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
u64 obj_fade_substitute_cmd(u64 cmd);

/* Shared by obj_start_part_fade and obj_start_sinking_part: appends to *gfxp the display
 * lists of e->info's conditional records (partDlRecs..toppleKeys, walked with `!=`):
 * {s32 n; n x {u16 part; u8 atLeast; u8 limit}; s32 a0, a1, b0, b1}. A record
 * counts only if its first condition's part (the u16 at +4, read even when
 * n is 0) is `id`; then every condition must hold, with v = 90 for part id
 * itself, else e's partPct byte for that (1-based) part: v >= limit when
 * atLeast is set, else v <= limit. A record that holds has its two ranges of
 * 8-byte commands [info + a0, info + a1) and [info + b0, info + b1) copied,
 * through obj_fade_substitute_cmd when `substitute` is set. Each command uses up one of
 * *budget; when it reaches 0 the copy stops and 0 is returned (the callers
 * then return at once). Returns 1 otherwise. */
static s32 dl_cond_copy_802C09B8(u32 **gfxp, LevelObject *e, s32 id, s32 *budget, s32 substitute) {
    ObjectModel *info = e->info;
    u8 *rec = (u8 *) info + info->partDlRecs;
    u8 *end = (u8 *) info + info->toppleKeys;
    u64 *gfx = (u64 *) *gfxp;
    u64 *src;
    u64 *srcEnd;
    s32 n;
    s32 part;
    s32 atLeast;
    s32 limit;
    s32 v;
    s32 ok;
    s32 k;

    while (rec != end) {
        n = *(s32 *) rec;
#ifdef PORT_HOST
        /* With n == 0 the u16 at +4 is the top half of the s32 offset a0
         * (on the N64: 0 for any offset below 64K, so the record never
         * counts); the port keeps that s32 in host order, so read its top
         * half explicitly (found by port/tools/compare.py: the low half
         * matched part ids, the copy ran out of budget and a collapsing
         * part's rising piece never started) */
        part = n != 0 ? *(u16 *) (rec + 4) : (u16) ((u32) *(s32 *) (rec + 4) >> 16);
#else
        part = *(u16 *) (rec + 4);
#endif
        rec += 4;
        if (part == id) {
            ok = 1;
            while (n != 0) {
                part = *(u16 *) rec;
                atLeast = rec[2];
                limit = rec[3];
                n--;
                rec += 4;
                v = (part == id) ? 90 : ((u8 *) e)[0xEC + part - 1];
                if ((atLeast != 0) ? (v < limit) : (limit < v)) {
                    ok = 0;
                    break;
                }
            }
            if (ok) {
                for (k = 0; k < 2; k++) {
                    src = (u64 *) ((u8 *) info + ((s32 *) rec)[k * 2]);
                    srcEnd = (u64 *) ((u8 *) info + ((s32 *) rec)[k * 2 + 1]);
                    for (; src != srcEnd; src++) {
                        if (--*budget == 0) {
                            return 0;
                        }
                        *gfx++ = substitute ? obj_fade_substitute_cmd(*src) : *src;
                    }
                }
            }
        }
        rec += n * 4 + 0x10; /* remaining conditions (0 once all held) and the 4 offsets */
    }
    *gfxp = (u32 *) gfx;
    return 1;
}

/* Copies a collapsing part's display list into a fading slot (0x802C09B8).
 * Builds entry `id`'s debris display list in the first free gFadingParts slot
 * (u16 at 0x4B0 and byte at 0x4B2 both zero; none free: nothing happens),
 * unless e->info->partsTopple is set: a segment-9 command (0xBC002406) pointing at
 * info + 0x50 (physical), the conditional records' commands (see
 * dl_cond_copy_802C09B8, through obj_fade_substitute_cmd, at most 0x93), then
 * 0xB8000000 0 (end), and marks the slot used (u16 at 0x4B0 = 0xFF). If the
 * command budget runs out it returns right there, without the end command.
 * The asm takes id in t3 and e in t9 and preserves every register (all of
 * them saved); asm callers keep a0, t3, t5, t9, f12, f14 (obj_update_delayed_damage) or
 * t3, t9, f12, f14 (obj_hit_tri, coll_damage_object_part) live across the call. */
void obj_start_part_fade(s32 id, LevelObject *e) {
    ObjectModel *info = e->info;
    u8 *slot = gFadingParts;
    u32 *gfx;
    s32 budget = 0x94;
    s32 i;

    if (info->partsTopple != 0) {
        return;
    }
    for (i = 4;; i--, slot += 0x4B8) {
        if (i == 0) {
            return;
        }
        if (*(u16 *) (slot + 0x4B0) == 0 && slot[0x4B2] == 0) {
            break;
        }
    }
    gfx = (u32 *) slot;
    gfx[0] = 0xBC002406;
    gfx[1] = (u32) info + 0x50 - 0x80000000;
    gfx += 2;
    if (dl_cond_copy_802C09B8(&gfx, e, id, &budget, 1) == 0) {
        return;
    }
    *(u16 *) (slot + 0x4B0) = 0xFF;
    gfx[0] = 0xB8000000;
    gfx[1] = 0;
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/77E20/obj_start_part_fade.s")
#endif

/* Same preserve-caller-registers convention as obj_clear_delayed_damage above - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
typedef struct {
    /* 0x00 */ u64 from;
    /* 0x08 */ u64 to;
} Unk802F46C0;

extern Unk802F46C0 gObjFadeCmdSubs[12]; /* display-list command substitutions */

/* The gObjFadeCmdSubs command substitutions for a fading copy (0x802C0C64).
 * Returns the replacement for a 64-bit display-list command: the `to` of the
 * first of the 12 gObjFadeCmdSubs entries whose `from` equals cmd, else cmd
 * itself. The asm takes and returns cmd as one 64-bit value in t4 (ld/sd by
 * its caller obj_start_part_fade) and saves v0, v1 and a0; obj_start_part_fade keeps
 * a0-a3, t1-t3 and t9 live. The harness only maps the low word (t4 <-> a1 /
 * v1), see conventions.txt. */
u64 obj_fade_substitute_cmd(u64 cmd) {
    s32 i;

    for (i = 0; i < 12; i++) {
        if (gObjFadeCmdSubs[i].from == cmd) {
            return gObjFadeCmdSubs[i].to;
        }
    }
    return cmd;
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/77E20/obj_fade_substitute_cmd.s")
#endif

/* Uses the sd-$ra frame convention - see the file-level note at the top of this file. Permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
/* One rising piece of debris (obj_start_sinking_part starts it, obj_draw_sinking_part moves
 * and draws it). The display list obj_start_sinking_part builds sits at the start. */
typedef struct {
    /* 0x000 */ u8 dl[0x960];
    /* 0x960 */ s32 mtx[2][16]; /* 16.16 translation, then Mtx layout; one per gBufferIndex buffer */
    /* 0x9E0 */ s16 vtx[4][8];  /* 4 Vtx (obj_set_sink_quad) */
    /* 0xA20 */ s32 depth;      /* how far it sinks (the part's height, << 5, - the object's y) */
    /* 0xA24 */ s32 height;     /* how far it has sunk */
    /* 0xA28 */ u16 speed;      /* +16 a frame */
    /* 0xA2A */ u8 active;
    /* 0xA2B */ u8 cooldown;    /* 3 frames after it ends before the next can start */
    /* 0xA2C */ u8 padA2C[4];
} SinkingPart;                  /* size 0xA30 */


/* GSinkingPart: accelerates down with an x/z shake (0x802C0CBC).
 * Moves the gSinkingPart entry (one) and draws it into gfx: an inactive entry
 * counts cooldown down to 0. An active one speeds up by 16 and sinks by its
 * speed (the translation is -(h << 11): down); once that reaches depth it deactivates (active = 0, cooldown = 3).
 * Otherwise gPartSinking = 1, its matrix (mtx[gBufferIndex != 0]) becomes a
 * translation by (r1 << 12, -(height << 11), r2 << 12) with r1, r2 rolls in
 * -32..32 (obj_rand_range), converted by mtx_to_n64, and gfx gets
 * G_RDPPIPESYNC, a segment-0xC command (0xBC003006) to that matrix and a
 * G_DL call to the entry (physical addresses). gPartSinking is cleared first.
 * Ends the list with 0xB8000000 0 and returns the new end. The asm takes and
 * returns gfx in v0 (its caller ignores it) and saves v1, a0-t4, s2 and s4;
 * asm caller obj_draw_all keeps a0-a3, f12 and f14 live across the call. */
u32 *obj_draw_sinking_part(u32 *gfx) {
    SinkingPart *p = gSinkingPart;
    s32 i;
    s32 speed;
    s32 h;
    s32 x;
    s32 z;

    gPartSinking = 0;
    for (i = 1; i != 0; i--, p++) {
        if (p->active == 0) {
            if (p->cooldown != 0) {
                p->cooldown--;
            }
            continue;
        }
        speed = p->speed + 16;
        h = p->height + speed;
        p->speed = speed;
        if (!(h < p->depth)) {
            p->active = 0;
            p->cooldown = 3;
            continue;
        }
        p->height = h;
        gPartSinking = 1;
        x = obj_rand_range(-32, 32) << 12;
        z = obj_rand_range(-32, 32) << 12;
        mtx_translate(x, -(h << 11), z, p->mtx[(gBufferIndex != 0) ? 1 : 0]);
        mtx_to_n64((u16 *) p->mtx[(gBufferIndex != 0) ? 1 : 0]);
        PORT_ITAG(gfx, PORT_ITAG_RISE, 1 - i);
        gfx[0] = 0xE7000000;
        gfx[1] = 0;
        gfx[2] = 0xBC003006;
        gfx[3] = (u32) p->mtx[(gBufferIndex != 0) ? 1 : 0] - 0x80000000;
        gfx[4] = 0x06000000;
        gfx[5] = (u32) p - 0x80000000;
        gfx += 6;
    }
    gfx[0] = 0xB8000000;
    gfx[1] = 0;
    return gfx + 2;
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/77E20/obj_draw_sinking_part.s")
#endif

/* Same preserve-caller-registers convention as obj_clear_delayed_damage above - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
extern s16 gObjSinkingKinds[]; /* kind values that get a rising piece; ends at the first negative one */
extern u64 gObjSinkRenderCmds[6]; /* fixed render-state commands */

/* Kinds in gObjSinkingKinds: the collapsing part sinks its own height (0x802C0E8C).
 * Starts the rising piece of debris for part `id` of e: unless the game mode
 * gGameMode has 0x440 set, e->kind is in the list gObjSinkingKinds (an equal
 * value is found before the negative terminator) and the gSinkingPart entry is
 * idle (active and cooldown zero). Then obj_set_sink_quad sets its 4 vertices, and
 * its display list is built in place: segment 9 (0xBC002406) = info + 0x50,
 * segment 0xB (0xBC002C06) = its vertices, the 6 commands of gObjSinkRenderCmds,
 * 0x01040040 0x0C000000, the conditional records' commands (see
 * dl_cond_copy_802C09B8, copied as they are, at most 0x120; if that runs out
 * it returns right there), 0xBD000000 0 and 0xB8000000 0. Finally
 * height = 0, speed = 0, active = 1 and depth = (info->partHeights[id - 1] << 5)
 * - e->pos[1]. The asm takes id in t3 and e in t9 and preserves every
 * register (all of them saved); asm callers keep a0, t3, t5, t9, f12, f14
 * (obj_update_delayed_damage) or t3, t9, f12, f14 (obj_hit_tri, coll_damage_object_part) live
 * across the call. */
void obj_start_sinking_part(s32 id, LevelObject *e) {
    s16 *k;
    SinkingPart *p;
    ObjectModel *info;
    u32 *gfx;
    s32 budget = 0x121;
    s32 i;

    if ((u32) gGameMode & (MODE_REPLAY | MODE_REPLAY_CARRIER_END)) {
        return;
    }
    for (k = gObjSinkingKinds; *k != e->kind; k++) {
        if (*k < 0) {
            return;
        }
    }
    for (i = 1, p = gSinkingPart;; i--, p++) {
        if (i == 0) {
            return;
        }
        if (p->active == 0 && p->cooldown == 0) {
            break;
        }
    }
    info = e->info;
    obj_set_sink_quad((u8 *) p, e);
    gfx = (u32 *) p->dl;
    gfx[0] = 0xBC002406;
    gfx[1] = (u32) info + 0x50 - 0x80000000;
    gfx[2] = 0xBC002C06;
    gfx[3] = (u32) p->vtx - 0x80000000;
    gfx += 4;
    for (i = 0; i < 6; i++) {
        ((u64 *) gfx)[i] = gObjSinkRenderCmds[i];
    }
    gfx += 12;
    gfx[0] = 0x01040040;
    gfx[1] = 0x0C000000;
    gfx += 2;
    if (dl_cond_copy_802C09B8(&gfx, e, id, &budget, 0) == 0) {
        return;
    }
    info = e->info;
    p->height = 0;
    p->depth = (*(s16 *) ((u8 *) info + info->partHeights + (id - 1) * 2) << 5) - e->pos[1];
    p->speed = 0;
    p->active = 1;
    gfx[0] = 0xBD000000;
    gfx[1] = 0;
    gfx[2] = 0xB8000000;
    gfx[3] = 0;
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/77E20/obj_start_sinking_part.s")
#endif

/* Uses the sd-$ra frame convention - see the file-level note at the top of this file. Permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
/* The sinking part's 4-vertex footprint box at the object's base (0x802C1214).
 * Writes the positions of 4 Vtx at base + 0x9E0: the corners of e's info box
 * (s16 x0, z0, x1, z1 at info->footprintBox) grown by 100 on every side, at height
 * e->pos[1] >> 5 (arithmetic), in the order (x0, z0), (x1, z0), (x0, z1),
 * (x1, z1); each value is stored as its low 16 bits. The asm takes base in v1
 * and e in t9 and saves a0-a3 and t0-t3; asm caller obj_start_sinking_part keeps t3
 * and t9 live. */
void obj_set_sink_quad(u8 *base, LevelObject *e) {
    ObjectModel *info = e->info;
    s16 *box = (s16 *) ((u8 *) info + info->footprintBox);
    s32 y = e->pos[1] >> 5;
    s32 x0 = box[0] - 100;
    s32 z0 = box[1] - 100;
    s32 x1 = box[2] + 100;
    s32 z1 = box[3] + 100;
    s16 *v = (s16 *) (base + 0x9E0);

    v[0] = x0, v[1] = y, v[2] = z0;
    v[8] = x1, v[9] = y, v[10] = z0;
    v[16] = x0, v[17] = y, v[18] = z1;
    v[24] = x1, v[25] = y, v[26] = z1;
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/77E20/obj_set_sink_quad.s")
#endif

/* Uses the sd-$ra frame convention - see the file-level note at the top of this file. Permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
extern Mtx * N64P gToppleMtx; /* 2 matrices for them */
/* 23C20.c defines it with s16 parameters; the asm passes rx as a full
 * u16 + u16 sum and ry zero-extended, so it is declared with words here. */

/* GTopplingParts: the tilt grows to about 79 degrees, then gone (0x802C12E0).
 * Updates and draws the 2 gTopplingParts entries into two display lists. An
 * inactive entry (byte 0x470 clear) counts its byte 0x472 down to 0. An
 * active one advances its u16 angle 0x46C by its u16 speed 0x46E while the
 * sum stays below 0x385 (the speed then grows by 15); once it doesn't, it
 * counts byte 0x471 up and, at 6, deactivates (0x470 = 0, 0x472 = 3) without
 * drawing. A drawn entry gets util_rotate_about_point(x, y, z at 0x464..0x468, the sum,
 * u16 0x46A, its matrix gToppleMtx[i]), then both lists get gSPSegment(9,
 * physical address in word 0x460), and a G_DL call to the entry (list A) or
 * to entry + 0x230 (list B), as physical addresses. The asm takes and returns
 * the two list pointers in a0/a1 (here through gfxA/gfxB) and changes s0-s5
 * (its caller obj_draw_all saves them). */
void obj_draw_toppling_parts(u32 **gfxA, u32 **gfxB) {
    u32 *a = *gfxA;
    u32 *b = *gfxB;
    u8 *e = gTopplingParts;
    Mtx *m = gToppleMtx;
    s32 i;
    s32 sum;
    s32 c;
    u32 seg;

    for (i = 2; i != 0; i--, e += 0x478, m++) {
        if (e[0x470] == 0) {
            if (e[0x472] != 0) {
                e[0x472]--;
            }
            continue;
        }
        sum = *(u16 *) (e + 0x46C) + *(u16 *) (e + 0x46E);
        if (sum < 0x385) {
            *(u16 *) (e + 0x46E) += 15;
            *(u16 *) (e + 0x46C) = sum;
        } else {
            c = e[0x471] + 1;
            e[0x471] = c;
            if (c >= 6) {
                e[0x470] = 0;
                e[0x472] = 3;
                continue;
            }
        }
        util_rotate_about_point(*(s16 *) (e + 0x464), *(s16 *) (e + 0x466), *(s16 *) (e + 0x468), sum,
                      *(u16 *) (e + 0x46A), m);
        seg = *(u32 *) (e + 0x460) - 0x80000000;
        PORT_ITAG(a, PORT_ITAG_FALL, 2 - i);
        PORT_ITAG(b, PORT_ITAG_FALL, 2 - i);
        a[0] = 0xBC002406;
        a[1] = seg;
        b[0] = 0xBC002406;
        b[1] = seg;
        a[2] = 0x06000000;
        a[3] = (u32) e - 0x80000000;
        b[2] = 0x06000000;
        b[3] = (u32) (e + 0x230) - 0x80000000;
        a += 4;
        b += 4;
    }
    PORT_ITAG(a, PORT_ITAG_NONE, 0);
    PORT_ITAG(b, PORT_ITAG_NONE, 0);
    *gfxA = a;
    *gfxB = b;
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/77E20/obj_draw_toppling_parts.s")
#endif

/* Same preserve-caller-registers convention as obj_clear_delayed_damage above - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
s32 obj_part_outranked(LevelObject *e, s32 index);

/* Quarter-turn heading (0..0x3FF) from the sine num / d: arcsine >> 4. */
static s32 heading_802C1438(s32 num, f32 d) {
    s32 sine;

    CVT_W_S(sine, 65536.0f * ((f32) num / d));
    return (u32) math_asin(sine) >> 4;
}

/* Model partsTopple set: the part topples away from the vehicle (0x802C1438).
 * Starts the toppling of part `index` (1-based) of e: only when
 * e's info byte 7 is set and obj_part_outranked(e, index) is 0. The first
 * gTopplingParts slot (2 of 0x478 bytes) whose bytes 0x470 and 0x472 are both
 * clear gets: word 0x460 = info + 0x50; two display lists, A at the slot and
 * B at slot + 0x230, each starting with {0x01040040, 0x02000000 + slot *
 * 0x40}. Then for every conditional record of info (partDlRecs .. toppleKeys: u32
 * condition count, then count x {u16 part, u8 mode, u8 limit}, then 4
 * offsets; the record belongs to the part named by its first condition) of
 * this part whose conditions all hold (the state of the
 * named part, e->partPct[part - 1], or 0x5A for this part itself, >= limit
 * when mode != 0, <= limit when mode == 0), the 8-byte commands
 * info + off0 .. off1 go to list A and off2 .. off3 to list B, at most 0x42
 * per list (running out returns at once, the slot left inactive). Both
 * lists end with {0xBD000000, 0}, {0xB8000000, 0}; the slot becomes active
 * (0x470 = 1, u16 0x46C = 0, u16 0x46E = 1, 0x471 = 0) at x, y, z
 * (s16 0x464..0x468: the part's point from info->partPoints, and e->pos[1] >> 5)
 * with s16 0x46A = (heading from the part (<< 5) to gCollX/Z) - 0x400,
 * plus 0xFFF when negative (heading as obj_step_tex_anims's).
 * Register convention: e in t9, index in t3 (conventions.txt); the asm
 * saves and restores every register except f0/f2. Asm callers keep a0, t3,
 * t5, t9, f12 and f14 live (a mixed N64 build would need a thunk; the native
 * port won't). The part can't sit exactly at the point (0 / 0). */
void obj_start_topple(LevelObject *e, s32 index) {
    ObjectModel *info = e->info;
    u8 *base = (u8 *) info;
    u8 *slot = gTopplingParts;
    u32 seg = 0x02000000;
    s32 i;
    u32 *a;
    u32 *b;
    u8 *rec;
    u8 *recEnd;
    s32 budgetA;
    s32 budgetB;
    s16 *pt;
    s32 x;
    s32 z;
    s32 px;
    s32 pz;
    f32 d;
    s32 ang;

    if (((u8 *) info)[7] == 0) {
        return;
    }
    if (obj_part_outranked(e, index) != 0) {
        return;
    }
    for (i = 2;; i--, seg += 0x40, slot += 0x478) {
        if (i == 0) {
            return;
        }
        if (slot[0x470] == 0 && slot[0x472] == 0) {
            break;
        }
    }
    PORT_ISPAWN(PORT_ITAG_FALL, 2 - i);
    *(u8 * N64P *) (slot + 0x460) = base + 0x50;
    a = (u32 *) slot;
    b = (u32 *) (slot + 0x230);
    a[0] = 0x01040040;
    a[1] = seg;
    b[1] = seg;
    b[0] = 0x01040040;
    a += 2;
    b += 2;
    budgetA = 0x43;
    budgetB = 0x43;
    rec = base + info->partDlRecs;
    recEnd = base + info->toppleKeys;
    while (rec != recEnd) {
        u32 n = *(u32 *) rec;
        s32 ok = *(u16 *) (rec + 4) == index;

        rec += 4;
        while (ok && n != 0) {
            s32 part = *(u16 *) rec;
            s32 mode = rec[2];
            s32 limit = rec[3];
            s32 state;

            n--;
            rec += 4;
            state = (part == index) ? 0x5A : e->partPct[part - 1];
            ok = (mode != 0) ? (state >= limit) : (state <= limit);
        }
        if (!ok) {
            rec += n * 4 + 0x10;
            continue;
        }
        {
            u32 *src = (u32 *) (base + ((s32 *) rec)[0]);
            u32 *srcEnd = (u32 *) (base + ((s32 *) rec)[1]);

            for (; src != srcEnd; src += 2, a += 2) {
                if (--budgetA == 0) {
                    return;
                }
                a[0] = src[0];
                a[1] = src[1];
            }
            src = (u32 *) (base + ((s32 *) rec)[2]);
            srcEnd = (u32 *) (base + ((s32 *) rec)[3]);
            for (; src != srcEnd; src += 2, b += 2) {
                if (--budgetB == 0) {
                    return;
                }
                b[0] = src[0];
                b[1] = src[1];
            }
        }
        rec += 0x10;
    }
    a[0] = 0xBD000000;
    a[1] = 0;
    a[2] = 0xB8000000;
    a[3] = 0;
    b[0] = 0xBD000000;
    b[1] = 0;
    b[2] = 0xB8000000;
    b[3] = 0;
    slot[0x470] = 1;
    *(u16 *) (slot + 0x46C) = 0;
    *(u16 *) (slot + 0x46E) = 1;
    slot[0x471] = 0;
    *(s16 *) (slot + 0x466) = e->pos[1] >> 5;
    pt = (s16 *) (base + info->partPoints + (index - 1) * 8);
    *(s16 *) (slot + 0x464) = pt[0];
    *(s16 *) (slot + 0x468) = pt[2];
    x = pt[0] << 5;
    z = pt[2] << 5;
    px = gCollX;
    pz = gCollZ;
    {
        f32 dx = (f32) (s32) (px - x);
        f32 dz = (f32) (s32) (pz - z);

        d = sqrtf(dx * dx + dz * dz);
    }
    if (px < x) {
        if (pz < z) {
            ang = heading_802C1438(x - px, d) + 0x800;
        } else {
            ang = heading_802C1438(pz - z, d) + 0xC00;
        }
    } else if (pz < z) {
        ang = heading_802C1438(z - pz, d) + 0x400;
    } else {
        ang = heading_802C1438(px - x, d);
    }
    ang -= 0x400;
    if (ang < 0) {
        ang += 0xFFF;
    }
    *(s16 *) (slot + 0x46A) = ang;
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/77E20/obj_start_topple.s")
#endif

/* Same preserve-caller-registers convention as obj_clear_delayed_damage above - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
void obj_add_delayed_damage(s32 kind, s32 id, s32 amount);

/* Radius damage: every live part in range, delayed (0x802C18D4).
 * Blast damage: for every gObjects entry (up to gObjectsEnd, walked with
 * `!=`) whose sphere (pos, radius) overlaps the sphere at (x, y, z) >> 11
 * (logical shifts) with radius radius << 5 (math_spheres_overlap), unless its kind
 * is 0x38, every part that isn't done (partPct byte != 100) gets
 * obj_add_delayed_damage(part (1-based), e, amount / info->type (unsigned; the asm
 * traps on a zero type)). The asm takes x, y, z in t3, t4, t5, radius in a1
 * and amount in t6, and preserves every register it touches; asm callers
 * keep a0 (obj_blast_around_carrier) or f12, f14 (obj_run_effect_queue) live across the call. */
void obj_blast_damage(s32 x, s32 y, s32 z, s32 radius, s32 amount) {
    LevelObject *end = gObjectsEnd;
    LevelObject *e;
    s32 px = (u32) x >> 11;
    s32 py = (u32) y >> 11;
    s32 pz = (u32) z >> 11;
    s32 r = radius << 5;
    s32 n;
    s32 part;
    u8 *p;

    for (e = gObjects; e != end; e++) {
        if (math_spheres_overlap(pz, r, e->pos[0], e->pos[1], px, py, e->pos[2], e->radius) == 0) {
            continue;
        }
        if (e->kind == OBJ_KIND_END) {
            continue;
        }
        for (n = e->numParts, p = e->partPct, part = 1; n != 0; n--, p++, part++) {
            if (*p != 100) {
                obj_add_delayed_damage(part, (s32) e, (u32) amount / e->info->type);
            }
        }
    }
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/77E20/obj_blast_damage.s")
#endif

/* Uses the sd-$ra frame convention - see the file-level note at the top of this file. Permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
/* With tbl the s16 table at e->info->toppleKeys and n = info->numToppleKeys entries,
 * returns n minus the number of leading entries >= tbl[index - 1] (so 0 if
 * all n are). The asm takes e in t9 and index in t3, returns in a2 and saves
 * v0, a0, a1 and a3; asm caller obj_start_topple keeps t3, t9, f12 and f14
 * live. */
s32 obj_part_outranked(LevelObject *e, s32 index) {
    ObjectModel *info = e->info;
    s16 *tbl = (s16 *) ((u8 *) info + info->toppleKeys);
    s32 key = tbl[index - 1];
    s32 n = info->numToppleKeys;

    while (n != 0 && *tbl >= key) {
        tbl++;
        n--;
    }
    return n;
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/77E20/obj_part_outranked.s")
#endif

/* Uses the sd-$ra frame convention - see the file-level note at the top of this file. Permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
/* 1 if every path building (pathBuilding) is destroyed (0x802C1AA0).
 * Returns 1 if every gObjects entry with pathBuilding set is done (obj_parts_destroyed),
 * else 0. The asm loops with `!=`, so gObjectsEnd must be gObjects + n
 * entries. The asm saves and restores v1, t0, t1 and t3. */
s32 obj_path_buildings_destroyed(void) {
    LevelObject *end = gObjectsEnd;
    LevelObject *e;

    for (e = gObjects; e != end; e++) {
        if (e->pathBuilding == 0) {
            continue;
        }
        if (obj_parts_destroyed(e + 1) == 0) {
            return 0;
        }
    }
    return 1;
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/77E20/obj_path_buildings_destroyed.s")
#endif

/* Uses the sd-$ra frame convention - see the file-level note at the top of this file. Permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
/* Destroyed path buildings: the HUD's "%d/%d" (1D990.c) (0x802C1B1C).
 * Counts the gObjects entries with pathBuilding set that are done (obj_parts_destroyed).
 * 1D990.c declares it u8, but the asm returns the full 32-bit count. The asm
 * loops with `!=`, so gObjectsEnd must be gObjects + n entries. The asm saves
 * and restores v1, t0, t1 and t3. */
s32 obj_count_destroyed_path_buildings(void) {
    LevelObject *end = gObjectsEnd;
    LevelObject *e;
    s32 count = 0;

    for (e = gObjects; e != end; e++) {
        if (e->pathBuilding == 0) {
            continue;
        }
        if (obj_parts_destroyed(e + 1) != 0) {
            count++;
        }
    }
    return count;
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/77E20/obj_count_destroyed_path_buildings.s")
#endif

/* Uses the sd-$ra frame convention - see the file-level note at the top of this file. Permanently GLOBAL_ASM. */
#ifdef NON_MATCHING

/* The nearest target ahead of the carrier: distance and x, y, z (0x802C1B9C).
 * Finds the distance from the player (gCarrierX/Y/Z) to the closest
 * target, starting from a limit of 9999999, over the same three tables as
 * obj_find_nearest_target (a later candidate wins only when strictly closer):
 *   1: gObjects entries with pathBuilding set, destroyed clear and not yet done
 *      (obj_parts_destroyed), skipping one whose z is below the player's when its
 *      radius is below the distance; records centreX, y, centreZ;
 *   2: gBoxTargets entries with filled clear; records x, z;
 *   3: gTriTargets entries of the current level whose z isn't below the
 *      player's, with an id whose gCollTris isSolid is set; records x, z.
 * The recorded values go to gCarrierTargetX/Y/Z (Y only for kind 1), the
 * distance to gCarrierTargetDist and the return value. The asm loops over gObjects
 * with `!=`, so gObjectsEnd must be gObjects + n entries. */
u32 obj_find_carrier_target(void) {
    LevelObject *end = gObjectsEnd;
    LevelObject *e;
    BoxTarget *t;
    TriTarget *p;
    s8 *id;
    s32 bestDist = 9999999;
    s32 dist;
    s32 i;

    for (e = gObjects; e != end; e++) {
        if (e->pathBuilding == 0 || e->destroyed != 0) {
            continue;
        }
        dist = math_dist3(e->pos[0], e->pos[1], e->pos[2], gCarrierX, gCarrierY, gCarrierZ);
        if (dist >= bestDist) {
            continue;
        }
        if (obj_parts_destroyed(e + 1) != 0) {
            continue;
        }
        if (e->pos[2] < gCarrierZ && e->radius < dist) {
            continue;
        }
        gCarrierTargetX = e->centreX;
        bestDist = dist;
        gCarrierTargetY = e->pos[1];
        gCarrierTargetZ = e->centreZ;
    }
    for (i = gBoxTargetCount, t = gBoxTargets; i != 0; i--, t++) {
        if (t->filled != 0) {
            continue;
        }
        dist = math_dist3(t->x, t->y, t->z, gCarrierX, gCarrierY, gCarrierZ);
        if (dist < bestDist) {
            gCarrierTargetX = t->x;
            gCarrierTargetZ = t->z;
            bestDist = dist;
        }
    }
    for (p = gTriTargets; p->level != -1; p++) {
        if (p->level != levelno) {
            continue;
        }
        dist = math_dist3(p->pos[0], p->pos[1], p->pos[2], gCarrierX, gCarrierY, gCarrierZ);
        if (dist >= bestDist || p->pos[2] < gCarrierZ) {
            continue;
        }
        for (id = p->ids; *id != -1; id++) {
            if (veh_tri_is_solid(*id) != 0) {
                gCarrierTargetX = p->pos[0];
                gCarrierTargetZ = p->pos[2];
                bestDist = dist;
                break;
            }
        }
    }
    gCarrierTargetDist = bestDist;
    return bestDist;
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/77E20/obj_find_carrier_target.s")
#endif

/* Uses the sd-$ra frame convention - see the file-level note at the top of this file. Permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
/* ObjectModel/Entry and gObjects/gObjectsEnd are declared at the top of the file. */

/* The level's totals: objects, nss.bd destroyed, nss.ip value (0x802C1DD0).
 * Tallies the entries gObjects..gObjectsEnd (0xFC bytes each). With
 * onlyFlagged set, only entries whose pathBuilding is nonzero count, and each of
 * those stores its kind into gPathBuilding (so the last one wins). Of the
 * counted entries whose info->type isn't 0xFF: those with type != 1 are
 * counted into gBuildingCount; if the entry's destroyed or gCheatAllDestroyed is nonzero,
 * type != 1 entries are also counted into nss.bd (Score) and every such
 * entry's info->value is summed into nss.ip. Clears gCheatAllDestroyed.
 * The asm loops with `!=`, so gObjectsEnd must be gObjects + n entries.
 * The asm keeps its counters in s0-s2 but saves and restores s0-s7. */
void obj_tally(s32 onlyFlagged) {
    LevelObject *e;
    LevelObject *end = gObjectsEnd;
    s32 counted = 0;
    s32 active = 0;
    s32 total = 0;

    for (e = gObjects; e != end; e++) {
        ObjectModel *info = e->info;

        if (onlyFlagged != 0) {
            if (e->pathBuilding == 0) {
                continue;
            }
            gPathBuilding = e->kind;
        }
        if (info->type == OBJ_TOUGH_UNCOUNTED) {
            continue;
        }
        if (info->type != OBJ_TOUGH_FLIMSY) {
            counted++;
        }
        if ((e->destroyed | gCheatAllDestroyed) == 0) {
            continue;
        }
        if (info->type != OBJ_TOUGH_FLIMSY) {
            active++;
        }
        total += info->value;
    }
    gBuildingCount = counted;
    nss.bd = active; /* the active count */
    nss.ip = total; /* the total */
    gCheatAllDestroyed = 0;
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/77E20/obj_tally.s")
#endif

/* TODO: lvl_group_get - walk a u16-offset chain rooted at
 * gLevelData + gLevelData->unk74: `u8 *t1 = (u8*)gLevelData + *(s32*)((u8*)gLevelData + 0x74);
 * u16 *t2 = (u16*)(t1 + 4); for (arg0--; arg0 != 0; arg0--) t2 = (u16*)(t1 + *t2);
 * return (u8*)t2 + 2;` (target: 11 instructions, a pure leaf with zero calls).
 * The target saves/restores $ra via an 8-byte `sd $ra`/`ld $ra` frame despite
 * never calling anything - this is the already-documented dead-frame shape
 * that no C phrasing can produce even for a genuine call, so definitely not
 * for a callless leaf (see the hd_code "phantom dead frame" notes in the
 * project skill file). Every phrasing tried (plain locals, register-qualified
 * locals) either adds extra spills on top or drops the frame to nothing -
 * never this exact shape. Logic confirmed correct via direct diff read. */
#ifdef NON_MATCHING
/* Record n of the level's group chain (gLevelData + 0x74) (0x802C1EE0).
 * Returns record `index` (1-based) of a chain of variable-length records.
 * The chain starts at level + *(s32 *)(level + 0x74), where level is
 * gLevelData. Each record is {u16 offsetOfNextRecord, s16 data[]}, with
 * offsets relative to the chain start, and the first record sits at chain + 4.
 * The result points at the record's data, just past its link. index 0 makes
 * the asm loop ~2^32 times, so callers must pass index >= 1. */
s16 *lvl_group_get(s32 index) {
    u8 *chain = (u8 *) gLevelData + *(s32 *) ((u8 *) gLevelData + 0x74);
    u16 *record = (u16 *) (chain + 4);

    for (index--; index != 0; index--) {
        record = (u16 *) (chain + *record);
    }
    return (s16 *) (record + 1);
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/77E20/lvl_group_get.s")
#endif

/* Uses the sd-$ra frame convention - see the file-level note at the top of this file. Permanently GLOBAL_ASM. */
#ifdef NON_MATCHING

/* A group part's translation matrix and its triangles' base (0x802C1F30).
 * Moves part `part` (1-based) of group `group` (1-based) of the level's group
 * chain (see lvl_groups_update_tris) to (x, y, z). The group's mtxOffset[part - 1]
 * locates a matrix in gObjPartMtx1 (if gBufferIndex) or gObjPartMtx0; it becomes a
 * translation by (x, y, z) in s15.16 (as mtx_translate builds it), stored in
 * the N64 Mtx layout (mtx_to_n64 splits the words into integer and
 * fraction halves). Every part of the group's records that follows the same
 * offset gets base = (x, y, z) >> 11. Groups are found through their `next`
 * offsets; group 0 makes the asm loop ~2^32 times, so callers pass >= 1. */
void lvl_group_move_part(s32 group, s32 part, s32 x, s32 y, s32 z) {
    u8 *level = gLevelData;
    u8 *chain = level + *(s32 *) (level + 0x74);
    Unk802C2054Group *g = (Unk802C2054Group *) (chain + 4);
    Unk802C2054Record *r;
    u16 off;
    u16 *mtx;
    s32 n;
    s32 k;

    for (group--; group != 0; group--) {
        g = (Unk802C2054Group *) (chain + (u16) g->next);
    }
    off = *(u16 *) ((u8 *) g + 0x14 + (part - 1) * 2); /* g->mtxOffset[part - 1] */
    mtx = (u16 *) (((gBufferIndex != 0) ? gObjPartMtx1 : gObjPartMtx0) + off);
    mtx_translate(x, y, z, (s32 *) mtx);
    mtx_to_n64(mtx);

    x >>= 11;
    y >>= 11;
    z >>= 11;
    r = (Unk802C2054Record *) (g + 1);
    for (n = g->count; n != 0; n--, r++) {
        for (k = 0; k < 3; k++) {
            if (r->part[k].mtxOff == off) {
                r->part[k].base[0] = x;
                r->part[k].base[1] = y;
                r->part[k].base[2] = z;
            }
        }
    }
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/77E20/lvl_group_move_part.s")
#endif

/* Uses the sd-$ra frame convention - see the file-level note at the top of this file. Permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
/* Unk802C2054Group/Record are declared at the top of the file. */

/* The group records into the triangle list gGroupTris (0x802C2054).
 * Walks the level's group list (at gLevelData + *(s32 *)(gLevelData + 0x74);
 * an initial zero word means "empty") and, for each record, writes nine words
 * (index[k] << 5) + part[k / 3].base[k % 3] to the output array gGroupTris,
 * 10 words (0x28 bytes) per record; word 9 of each output slot is left
 * untouched. Groups are stored back to back: a 0x2C-byte header, then
 * `count` 0x44-byte records, then the next header. The walk stops after a
 * group whose header's `next` is -1. The asm also computes chain + next but
 * never uses it (dead), so `next` acts only as an end marker here.
 * `count` is counted down with `!=` and must not be negative. */
void lvl_groups_update_tris(void) {
    u8 *level = gLevelData;
    u8 *chain = level + *(s32 *) (level + 0x74);
    s32 *out = gGroupTris;
    u8 *p;
    s16 next;
    s32 n;
    s32 k;
    Unk802C2054Record *r;

    if (*(s32 *) chain == 0) {
        return;
    }
    p = chain + 4;
    do {
        next = ((Unk802C2054Group *) p)->next;
        n = ((Unk802C2054Group *) p)->count;
        r = (Unk802C2054Record *) (p + sizeof(Unk802C2054Group));
        while (n != 0) {
            n--;
            for (k = 0; k < 9; k++) {
                out[k] = (r->index[k] << 5) + r->part[k / 3].base[k % 3];
            }
            out += 10;
            r++;
        }
        p = (u8 *) r;
    } while (next != -1);
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/77E20/lvl_groups_update_tris.s")
#endif
