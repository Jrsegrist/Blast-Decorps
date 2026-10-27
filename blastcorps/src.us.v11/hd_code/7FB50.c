#include "common.h"
#include <ultra64.h>
#include "game/game.h"
/* Views: this file reads these shared variables (game/variables.h) as other types. */
#define gObjects ((Rec7FB50 *) gObjects)
#ifdef NON_MATCHING
#define gObjectsEnd (*(Rec7FB50 * N64P *) &gObjectsEnd)
#define gEngineSound (*(s32 *) &gEngineSound)
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
/* gObjects: array of 0xFC-byte records (55 slots); gObjectsEnd points one
 * past the last record in use.  This file's view of a LevelObject
 * (game/object.h; the same field names): it reads `info` as bytes
 * (info[4] = ObjectModel.type, 1 = excluded). */
typedef struct {
    /* 0x00 */ u8 * N64P info;
    /* 0x04 */ u8 pad4[0x2C];
    /* 0x30 */ s32 kind;    /* 0x38 (the end object) = excluded */
    /* 0x34 */ u8 pad34[0xB5];
    /* 0xE9 */ u8 numParts; /* number of per-part bytes at +0xEC */
    /* 0xEA */ u8 destroyed; /* LevelObject.destroyed */
    /* 0xEB */ u8 pathBuilding;
    /* 0xEC */ u8 partPct[0x10]; /* 100 = part destroyed */
} Rec7FB50; /* size 0xFC */

extern u8 gStatusLevelPercents[];


/* Bit writer used by the status packers below: MSB-first into whole bytes.
 * (Macros rather than functions: the originals are single asm leaves.) */
typedef struct {
    u8 * N64P out;
    u32 bits;
    s32 count;
} BitWriter7FB50;

#define BITW_PUT(w, bit)                \
    {                                   \
        (w).bits = ((w).bits << 1) | (bit); \
        (w).count++;                    \
        if ((w).count == 8) {           \
            *(w).out++ = (w).bits;      \
            (w).bits = 0;               \
            (w).count = 0;              \
        }                               \
    }

/* Flush a partial byte, left-aligned. */
#define BITW_FLUSH(w)                                  \
    {                                                  \
        if ((w).count != 0) {                          \
            *(w).out++ = (w).bits << (8 - (w).count);  \
        }                                              \
    }

/* The player-vehicle block. The asm reaches it through gp (callers
 * jbomb_ride_follow / jbomb_update set gp = &gJbombState); the C reads it
 * directly. Only the fields below are known. */
extern u8 gJbombState[];
#define VEH_S32(off) (*(s32 *) (gJbombState + (off)))
#define VEH_S16(off) (*(s16 *) (gJbombState + (off)))
#define VEH_U8(off) (gJbombState[off])
/* +0x28/0x2C/0x30, +0x34/0x38/0x3C, +0x40/0x44/0x48: three s32 triples
 * +0x76: s16 (signed, decays towards 0)
 * +0x78..0x95: 15 s16 tuning values
 * +0x96..0x98: u8 flags; +0x9F: u8 (100 = ...); +0xA1: u8 mode (0..5); +0xA2: u8 previous mode */

extern s32 gJbombPosY;
extern void * N64P gJbombThrustSound;
extern void * N64P gJbombAirSound;
extern f32 gJbombPitch;
extern f32 gJbombRoll;
extern u8 gJbombAirSteerRate;
extern u8 gJbombThrusting;
extern u8 gJbombThrottleStep;
extern f32 gJbombRollMax;
extern f32 gJbombRollMaxClamp;
extern f32 gJbombRollMin;
extern f32 gJbombRollMinClamp;


/* This vehicle's animation channel table and the channel-field setters /
 * getter of 56040.c (AnimChannel; asm conventions in
 * tools_port/conventions.txt). anim_get_state's out[0] is (s8) field 0x10. */
extern u8 gJbombAnim[];

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
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
/* record_status: pack into `out`, one bit each, MSB-first, (1) for every part
 * of every record in use whether it is 100, then (2) rdu_is_lit(i) for
 * i = 0 .. gRduCount-1. Each section is padded to a whole byte. Returns the
 * number of bytes written. */
s32 record_status(u8 *out) {
    BitWriter7FB50 w;
    Rec7FB50 *r;
    u8 *part;
    s32 count;
    s32 i;
    s32 n;

    w.out = out;
    w.bits = 0;
    w.count = 0;
    for (r = gObjects; r != gObjectsEnd; r++) {
        part = r->partPct;
        for (n = r->numParts; n != 0; n--) {
            BITW_PUT(w,*part++ == 100);
        }
    }
    BITW_FLUSH(w);

    w.bits = 0;
    w.count = 0;
    count = gRduCount; /* read once, before any call */
    for (i = 0; i < count; i++) {
        BITW_PUT(w,rdu_is_lit(i));
    }
    BITW_FLUSH(w);

    return w.out - out;
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/7FB50/record_status.s")
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING

/* Applies a record_status / create_status bit stream (0x802C4BF0).
 * apply_status: reads a bitstream in record_status's format (MSB-first; the
 * bit reader runs on across the records and restarts on a fresh byte for
 * section 2). Section 1, per record in use and per part i (1-based): a set
 * bit destroys the part (parts[i-1] = 100, obj_mark_part_gone(record, i)), a
 * clear bit restores it (0) and clears the record's "all intact" flag
 * (+0xEA) unless i equals byte +0xD of the record's +0 object; +0xEA is
 * written for every record. gPeopleRestored = the sum of byte +6 of the +0 object
 * of every record that had a part destroyed. Then, per destroyed part i of
 * each record, every 0x60-byte entry of [+4, +8) whose u16 at +0x52 is i
 * gets byte +0x51 = 0, and one whose byte +0x57 is i gets byte +0x51 = 1
 * when the part named by its +0x52 (1-based; 0 reads the byte before the
 * parts) isn't destroyed. Section 2: gRduCount bits, rdu_set_lit(i) for
 * each set bit i. The asm saves every register it uses; its loops use `!=`
 * (the entry walk must reach +8 exactly). */
void apply_status(void *in) {
    u8 *p = in;
    Rec7FB50 *r;
    Rec7FB50 *end;
    u32 mask = 1;
    u32 byte = 0;
    s32 sum = 0;
    s32 n;
    s32 i;

    end = gObjectsEnd;
    for (r = gObjects; r != end; r++) {
        u8 *part = r->partPct;
        s32 allIntact = 1;
        s32 anyDestroyed = 0;

        n = r->numParts;
        i = 0;
        while (n != 0) {
            u8 v;

            n--;
            i++;
            if (mask == 1) {
                byte = *p++;
                mask = 0x100;
            }
            mask >>= 1;
            if (!(byte & mask)) {
                if (r->info[0xD] != i) {
                    allIntact = 0;
                }
                v = 0;
            } else {
                v = 100;
                obj_mark_part_gone((struct LevelObject *) r, i);
                anyDestroyed = 1;
            }
            *part++ = v;
        }
        r->destroyed = allIntact;
        if (anyDestroyed) {
            sum += r->info[6];
        }
    }
    gPeopleRestored = sum;

    end = gObjectsEnd;
    for (r = gObjects; r != end; r++) {
        n = r->numParts;
        for (i = 0; n != 0; i++) {
            n--;
            if (r->partPct[i] == 100) {
                u8 *e = *(u8 * N64P *) ((u8 *) r + 4);
                u8 *eEnd = *(u8 * N64P *) ((u8 *) r + 8);

                for (; e != eEnd; e += 0x60) {
                    s32 id = *(u16 *) (e + 0x52);

                    if (id == i + 1) {
                        e[0x51] = 0;
                    }
                    if (e[0x57] == i + 1 && r->partPct[id - 1] != 100) {
                        e[0x51] = 1;
                    }
                }
            }
        }
    }

    n = gRduCount;
    mask = 1;
    for (i = 0; n != 0; i++) {
        n--;
        if (mask == 1) {
            byte = *p++;
            mask = 0x100;
        }
        mask >>= 1;
        if (byte & mask) {
            rdu_set_lit(i);
        }
    }
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/7FB50/apply_status.s")
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
/* A record counts toward a random status unless it is excluded. */
#define REC7FB50_ELIGIBLE(r) ((r)->kind != OBJ_KIND_END && (r)->info[4] != OBJ_TOUGH_FLIMSY)

/* create_status: build a status bitstream in the format record_status writes,
 * for difficulty/percentage level `level` (1-based index into gStatusLevelPercents, a
 * table of percentages).
 * Section 1: for every record in use, numParts copies of one bit. The bit is
 * 1 for eligible records that are already done (unkEB), plus enough further
 * eligible records, in order, to make gStatusLevelPercents[level-1] percent of the
 * eligible ones, plus one (none at all when level == 5).
 * Section 2: gRduCount bits, the first gStatusLevelPercents[level-1] percent of them 1.
 * Each section is padded to a whole byte. Returns the number of bytes written. */
u32 create_status(void *outp, u8 level) {
    BitWriter7FB50 w;
    Rec7FB50 *r;
    u32 pct;
    u32 eligible;
    s32 done;
    s32 extra;
    u32 ones;
    s32 count;
    u32 bit;
    s32 n;

    pct = gStatusLevelPercents[level - 1];

    eligible = 0;
    for (r = gObjects; r != gObjectsEnd; r++) {
        if (REC7FB50_ELIGIBLE(r)) {
            eligible++;
        }
    }
    /* Note: unlike `eligible`, this counts done records whether eligible or not. */
    done = 0;
    for (r = gObjects; r != gObjectsEnd; r++) {
        if (r->pathBuilding != 0) {
            done++;
        }
    }
    extra = (pct * eligible) / 100 + 1;
    if (level - 1 == 4) {
        extra = 0;
    } else {
        extra -= done;
        if (extra < 0) {
            extra = 0;
        }
    }

    w.out = outp;
    w.bits = 0;
    w.count = 0;
    for (r = gObjects; r != gObjectsEnd; r++) {
        bit = 0;
        if (REC7FB50_ELIGIBLE(r)) {
            if (r->pathBuilding != 0) {
                bit = 1;
            } else if (extra != 0) {
                extra--;
                bit = 1;
            }
        }
        for (n = r->numParts; n != 0; n--) {
            BITW_PUT(w,bit);
        }
    }
    BITW_FLUSH(w);

    count = gRduCount;
    ones = (pct * gRduCount) / 100;
    w.bits = 0;
    w.count = 0;
    for (; count != 0; count--) {
        bit = 0;
        if (ones != 0) {
            bit = 1;
            ones--;
        }
        BITW_PUT(w,bit);
    }
    BITW_FLUSH(w);

    return w.out - (u8 *) outp;
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/7FB50/create_status.s")
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
extern u32 gJbombPos[]; /* x, y, z */
extern u8 * N64P gJbombModel;   /* its model */
extern u64 * N64P gJbombMtxA;  /* save copy pair */
extern u64 * N64P gJbombMtxB;
extern s32 gJbombSinceStomp2;
extern s32 gJbombSinceStomp4;
extern u8 gJbombEscaping;
extern u8 gJbombBounced;
extern u8 gJbombThrottleHold;
extern u8 gJbombKnocked;
extern u8 gJbombFlameTimer;
extern s8 gJbombJetRamp;
extern u8 gJbombThrustHold;
extern u8 gJbombStompTimer;
extern u8 gJbombStompDelay;
extern u8 gJbombPrevHeldB;
extern u8 gJbombPrevAirborne;
extern u8 gJbombTapTimerL;
extern u8 gJbombTapTimerR;
extern u8 gJbombStompDropPending;
extern u8 gJbombIdleTimer;

/* Sets up J-Bomb (type 9) (0x802C5120).
 * Load vehicle 9 (the 5CB60 dispatcher lvl_spawn_vehicles, record type 9): model
 * `model` (gJbombModel), save copies gJbombMtxA / gJbombMtxB taken 0x1000
 * bytes each from gHeapPtr (veh_add_record(9, 0, copies, model)), record
 * reset (veh_reset_state), wheel table +0x52..0x68, position (x, y, z),
 * heading `heading` at +0x4C/+0x4E/+0x74, ground heights (veh_init_ground,
 * key 9, fp), the model's animation channels (anim_init_model, channel 0 set
 * to 100 / 0 / 0 / 0.0 / restart, run on both copies with anim_update_model and
 * anim_rewind between), its flags, counters and mode (+0xA1/+0xA2 = 0),
 * gJbombPitch/Roll = 0.5, gJbombSinceStomp2/4 = 999999, parts (veh_register_parts, scale
 * 0x4268), veh_add_shadow(9, 100, 45, 45), channel 6 (0.0 with 2, restart
 * with -1), one update with +0x9A set (jbomb_update), the save copy pair
 * (veh_sync_buffers(gJbombMtxB, gJbombMtxA, 0x1000)), copy the model matrix
 * from the second copy to the first (mtx_copy_at) and gEngineSound = 0.
 * Register convention (conventions.txt): x t7, y s3, z s0, heading s1,
 * model s2, fp (handed to veh_init_ground, which stores it into gSlotSurface:
 * the dispatcher's leftover $fp in the game). The FP block veh_init_ground
 * passes through starts at 0 here (not read first). The asm saves t0-t5
 * (asm caller lvl_spawn_vehicles keeps t1, t2 live: a mixed N64 build would need
 * a thunk) and leaves s2-s4, fp and f12-f26 as its callees leave them
 * (survey: read by lvl_spawn_vehicles's next records; not modelled) and $gp =
 * the block. Adds are trapping in the asm. */
void jbomb_spawn(s32 x, s32 y, s32 z, s32 heading, u8 *model, s32 fp) {
    TriSideOut f;
    u8 *m;

    gJbombModel = model;
    gJbombMtxA = (u64 *) gHeapPtr;
    gJbombMtxB = (u64 *) (gHeapPtr + 0x1000);
    gHeapPtr += 0x2000;
    veh_add_record(VEH_JBOMB, 0, (s32) gJbombMtxA, (s32) gJbombMtxB, model);
    veh_reset_state(gJbombState);
    VEH_S16(VS_WHEEL_OFFSETS) = 0;
    VEH_S16(0x54) = 0;
    VEH_S16(0x56) = 0;
    VEH_S16(0x58) = 0;
    VEH_S16(0x5A) = 0;
    VEH_S16(0x5C) = 0;
    VEH_S16(VS_STAND_OFFSETS) = 0x190;
    VEH_S16(0x60) = 0x190;
    VEH_S16(0x62) = -0x190;
    VEH_S16(0x64) = 0x190;
    VEH_S16(0x66) = 0x190;
    VEH_S16(0x68) = -0x190;
    gJbombPos[0] = x;
    gJbombPos[1] = y;
    gJbombPos[2] = z;
    VEH_S16(VS_HEADING) = heading;
    VEH_S16(VS_MOVE_HEADING) = heading;
    VEH_S16(VS_MOMENTUM_HEADING) = heading;
    f.pz = f.cross = f.cz = f.side = f.sideZ = f.dz = 0.0f;
    veh_init_ground(&VEH_S16(VS_WHEEL_OFFSETS), gJbombPos[1], x, z, &VEH_S32(4), (s32 *) &gJbombPos[1], &VEH_S16(VS_HEADING), 9, fp,
                  gJbombState, &f);
    anim_init_model((u32 *) gJbombMtxB, (u32 *) gJbombMtxA, (AnimChannel *) gJbombAnim, gJbombModel);
    anim_set_speed((AnimChannel *) gJbombAnim, 0, 100);
    anim_set_reverse((AnimChannel *) gJbombAnim, 0, 0);
    anim_set_loop_mode((AnimChannel *) gJbombAnim, 0, 0);
    anim_set_interp(0.0f, (AnimChannel *) gJbombAnim, 0, 0);
    anim_play((AnimChannel *) gJbombAnim, 0, 1);
    anim_update_model((u8 *) gJbombMtxA, (u8 *) gJbombMtxB, (AnimChannel *) gJbombAnim);
    anim_rewind(0, (AnimChannel *) gJbombAnim);
    anim_play((AnimChannel *) gJbombAnim, 0, 1);
    anim_update_model((u8 *) gJbombMtxB, (u8 *) gJbombMtxA, (AnimChannel *) gJbombAnim);
    gJbombEscaping = 0;
    gJbombBounced = 0;
    gJbombKnocked = 0;
    gJbombThrustHold = 0;
    gJbombThrottleHold = 0;
    gJbombTapTimerL = 0;
    gJbombTapTimerR = 0;
    gJbombStompDropPending = 0;
    gJbombFlameTimer = 0;
    gJbombJetRamp = 0;
    gJbombPitch = 0.5f;
    gJbombRoll = 0.5f;
    VEH_U8(VS_ACTION) = 0;
    VEH_U8(VS_ACTION_AUX) = 0;
    gJbombAirSteerRate = 0x14;
    gJbombPrevAirborne = 0;
    gJbombIdleTimer = 0x1E;
    gJbombSinceStomp2 = 999999;
    gJbombSinceStomp4 = 999999;
    gJbombStompTimer = 0;
    gJbombStompDelay = 0;
    gContSteerError = 0;
    gJbombPrevHeldB = 0;
    m = gJbombModel;
    veh_register_parts(9, m + *(s32 *) (m + 4), m + *(s32 *) (m + 8), 0x4268);
    veh_add_shadow(VEH_JBOMB, 0x64, 0x2D, 0x2D);
    anim_set_frame(0.0f, (AnimChannel *) gJbombAnim, 6, 2);
    anim_play((AnimChannel *) gJbombAnim, 6, -1);
    VEH_U8(VS_SPAWN_UPDATE) = 1;
    jbomb_update();
    VEH_U8(VS_SPAWN_UPDATE) = 0;
    veh_sync_buffers(gJbombMtxB, gJbombMtxA, 0x1000);
    m = gJbombModel;
    mtx_copy_at((u8 *) gJbombMtxB, (u8 *) gJbombMtxA, *(s32 *) (m + *(s32 *) (m + 0x18) + 4));
    gEngineSound = 0;
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/7FB50/jbomb_spawn.s")
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
/* J-Bomb's exit check (0x802C5508).
 * Exit check for this vehicle (called from veh_exit_check in hd.c, which
 * declares it void and returns the leftover v0). 0 when a byte +0x96/7/8 of
 * the block is 1. Mode (+0xA1) 5: resets the mode to 0, stops channels 7
 * and 8, sets channel 6 (0.0, 2) and re-arms channel 9 (0.0 / 3 / 0 / 0.0 /
 * 1, restart with 1), returns 2. Other nonzero modes return 2. Mode 0:
 * 2 when channel 31's field 0x10 is 1, else 1 when channel 9's field 0x10 is
 * 0, else 2. Returns s32 (the asm's v0); the asm saves and restores $gp and
 * leaves v1 changed, which the C caller doesn't use. */
s32 jbomb_exit_check(void) {
    s32 ch[8];

    if (VEH_U8(VS_SLOT_AIRBORNE) == 1 || VEH_U8(0x97) == 1 || VEH_U8(0x98) == 1) {
        return 0;
    }
    if (VEH_U8(VS_ACTION) == 5) {
        VEH_U8(VS_ACTION) = 0;
        anim_stop(7, (AnimChannel *) gJbombAnim);
        anim_stop(8, (AnimChannel *) gJbombAnim);
        anim_set_frame(0.0f, (AnimChannel *) gJbombAnim, 6, 2);
        anim_set_frame(0.0f, (AnimChannel *) gJbombAnim, 9, 0);
        anim_set_speed((AnimChannel *) gJbombAnim, 9, 3);
        anim_set_reverse((AnimChannel *) gJbombAnim, 9, 0);
        anim_set_interp(0.0f, (AnimChannel *) gJbombAnim, 9, 0);
        anim_set_loop_mode((AnimChannel *) gJbombAnim, 9, 1);
        anim_play((AnimChannel *) gJbombAnim, 9, 1);
        return 2;
    }
    if (VEH_U8(VS_ACTION) != 0) {
        return 2;
    }
    anim_get_state(0x1F, (AnimChannel *) gJbombAnim, ch);
    if (ch[0] == 1) {
        return 2;
    }
    anim_get_state(9, (AnimChannel *) gJbombAnim, ch);
    if (ch[0] == 0) {
        return 1;
    }
    return 2;
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/7FB50/jbomb_exit_check.s")
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
extern u64 * N64P gJbombMtxA; /* save copy pair (veh_sync_buffers) */
extern u64 * N64P gJbombMtxB;

/* Getting out of J-Bomb (0x802C5688).
 * Teardown for this vehicle (called from veh_leave in hd.c): clears the
 * s16 at +0x76, veh_sync_buffers(gJbombMtxA, gJbombMtxB, 0x1000), stops
 * channel 31 and the sounds gJbombThrustSound / gJbombAirSound that are playing (the
 * handles are left as they are). The asm saves and restores $gp and leaves
 * v1 = &gJbombAnim or snd_stop's leftover; the C caller uses neither. */
void jbomb_leave(void) {
    VEH_S16(VS_SPEED) = 0;
    veh_sync_buffers(gJbombMtxA, gJbombMtxB, 0x1000);
    anim_stop(0x1F, (AnimChannel *) gJbombAnim);
    if (gJbombThrustSound != NULL) {
        snd_stop(gJbombThrustSound);
    }
    if (gJbombAirSound != NULL) {
        snd_stop(gJbombAirSound);
    }
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/7FB50/jbomb_leave.s")
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
/* Getting into J-Bomb (0x802C5714).
 * Enter this vehicle (called from hd.c and 17210.c): clears the byte at
 * +0x99 and starts channels 3 and 4 (fields 0 / 0 / 1, 1.0 with 1, restart
 * with -1) and channel 1 (0 / 0 / 0, restart with -1). The asm points $gp at
 * gJbombState and leaves it there (conventions.txt: clobbers gp) and leaves
 * v1 = -1; C callers use neither. */
void jbomb_enter(void) {
    VEH_U8(VS_SKIDDING) = 0;
    anim_set_speed((AnimChannel *) gJbombAnim, 3, 0);
    anim_set_reverse((AnimChannel *) gJbombAnim, 3, 0);
    anim_set_loop_mode((AnimChannel *) gJbombAnim, 3, 1);
    anim_set_interp(1.0f, (AnimChannel *) gJbombAnim, 3, 1);
    anim_play((AnimChannel *) gJbombAnim, 3, -1);
    anim_set_speed((AnimChannel *) gJbombAnim, 4, 0);
    anim_set_reverse((AnimChannel *) gJbombAnim, 4, 0);
    anim_set_interp(1.0f, (AnimChannel *) gJbombAnim, 4, 1);
    anim_set_loop_mode((AnimChannel *) gJbombAnim, 4, 1);
    anim_play((AnimChannel *) gJbombAnim, 4, -1);
    anim_set_speed((AnimChannel *) gJbombAnim, 1, 0);
    anim_set_reverse((AnimChannel *) gJbombAnim, 1, 0);
    anim_set_loop_mode((AnimChannel *) gJbombAnim, 1, 0);
    anim_play((AnimChannel *) gJbombAnim, 1, -1);
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/7FB50/jbomb_enter.s")
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
extern u32 gJbombPos[]; /* x, y, z */
void jbomb_build_model(MtxChainRegs *regs);

/* Placed J-Bomb on a moving level part: re-ground it (0x802C5860).
 * Move this vehicle while it isn't the player's (veh_update_others, case 9):
 * ground heights for its three slots (veh_reground with table +0x52, angle
 * +0x4C, heights to +4.., the middle y to gJbombPosY, key 9), place the
 * model (jbomb_build_model), then veh_set_record(z, 9, x, y, block).
 * Register convention (conventions.txt): `fp` is the asm's $fp, which goes
 * on to veh_reground (which stores it into gSlotSurface[0..2], so the
 * block's +0x50 becomes (u8) fp). The only caller, C veh_update_others, passes
 * nothing: in the game this is whatever $fp its callers left (FIDELITY:
 * see tools_port port_followups), so the native port must pass it
 * explicitly. f12-f26 only pass through veh_reground (not read first; the
 * asm restores f20-f30), so the C starts them at 0. jbomb_build_model gets s0 =
 * z and s1 = veh_reground's s1. The asm saves every s-register, gp, fp and
 * f20-f30; it leaves v1 = y (veh_set_record preserves it), which the survey
 * lists as read by veh_update_others (C: not a real use). */
void jbomb_reground(s32 fp) {
    TriSideOut f;
    Out802A9A60 out;
    MtxChainRegs regs;
    s32 z = gJbombPos[2];

    f.pz = f.cross = f.cz = f.side = f.sideZ = f.dz = 0.0f;
    veh_reground((s16 *) (gJbombState + 0x52), gJbombPos[1], gJbombPos[0], z, (s32 *) (gJbombState + 4),
                  (s32 *) &gJbombPos[1], (s16 *) (gJbombState + 0x4C), 9, fp, gJbombState, &f, &out);
    regs.s0 = z;
    regs.s1 = (s32) out.s1;
    jbomb_build_model(&regs);
    veh_set_record(gJbombPos[2], 9, gJbombPos[0], gJbombPos[1], gJbombState);
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/7FB50/jbomb_reground.s")
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
extern u32 gJbombPos[]; /* x, y, z */
/* Veh_zone_alpha at J-Bomb's position (0x802C5970).
 * Zone level lookup (veh_zone_alpha) for vehicle id 9 at its position
 * gJbombPos..+8; returns veh_zone_alpha's v1 (the zone list end).
 * Register convention: the asm passes veh_zone_alpha's scan registers t6, t7,
 * s0-s4 through (in and out; here through r), leaves a3 = id and clobbers
 * s5, s6 (conventions.txt). Asm caller jbomb_update keeps f12 and f14 live
 * (a mixed N64 build would need a thunk; the native port won't). */
s32 jbomb_zone_alpha(ZoneScanRegs *r) {
    return veh_zone_alpha(9, gJbombPos[0], gJbombPos[1], gJbombPos[2], r);
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/7FB50/jbomb_zone_alpha.s")
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING

/* Records J-Bomb's position on the deck it stands on (0x802C59B4).
 * Value pair at this vehicle's x/z (gJbombPos[0], [2]) on the triangle
 * `id` (veh_deck_to_local), stored as s16s at +0x6A/+0x6C of the vehicle block.
 * Register convention: id in a3; veh_deck_to_local's FP results (f12-f26, f24
 * also in) pass through to the caller, here through r (conventions.txt). The
 * asm saves and restores t0, t1, t3, t4 (asm caller veh_attach_riders_walk keeps t0
 * and t1 live: a mixed N64 build would need a thunk) and points $gp at
 * gJbombState without restoring it. */
void jbomb_ride_attach(s32 id, InterpRegs *r) {
    veh_deck_to_local(id, ((s32 *) gJbombPos)[0], ((s32 *) gJbombPos)[2], r);
    VEH_S16(VS_DECK_POS) = r->t3;
    VEH_S16(0x6C) = r->t4;
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/7FB50/jbomb_ride_attach.s")
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
void jbomb_set_gravity(void);
void jbomb_set_tuning(void);

/* Moves J-Bomb with the deck under it (0x802C5A14).
 * Set this vehicle down at the point on triangle `id` given by the value
 * pair at +0x6A/+0x6C (veh_deck_to_world): gGravity (jbomb_set_gravity) and the
 * tuning values (jbomb_set_tuning) for its mode, gBounceSound = 0, ground
 * contact (veh_ground_contact: id 9, position gJbombPos..+8, divisors 0x78 /
 * 0x78, angle +0x4C, flags +0x96, table +0x52, records +0x28/+0x40/+0x34,
 * heights +4), place the model (jbomb_build_model) and veh_set_record(z, 9, x,
 * y, block).
 * Register convention (conventions.txt): id in a3; s3 in/out (veh_ground_contact's
 * slot result, through *s3); the FP state chains through veh_deck_to_world and
 * veh_ground_contact (r: f24 in; f20-f26 out, f12/f14 are left as mtx_build_model's
 * cos/sin temporaries, not modelled). jbomb_build_model gets s0 = &+0x96 and
 * s1 = &D_803F7C00, the flags/pz pointers veh_ground_contact leaves. The asm
 * saves and restores a3, t0, t1, t2 (asm caller veh_move_riders_walk keeps a3, t0,
 * t1 live: a mixed N64 build would need a thunk) and points $gp at
 * gJbombState without restoring it. */
void jbomb_ride_follow(s32 id, s32 *s3, InterpRegs *r) {
    Regs802A8768 r8768;
    MtxChainRegs regs;

    veh_deck_to_world(id, VEH_S16(VS_DECK_POS), VEH_S16(0x6C), r);
    jbomb_set_gravity();
    jbomb_set_tuning();
    gBounceSound = 0;
    r8768.s3 = *s3;
    veh_ground_contact(gJbombState, 9, (s32 *) &gJbombPos[0], (s32 *) &gJbombPos[1], (s32 *) &gJbombPos[2], r->t3,
                  r->t4, 0x78, 0x78, &VEH_S16(VS_HEADING), &VEH_U8(VS_SLOT_AIRBORNE), &VEH_S16(VS_WHEEL_OFFSETS), &VEH_S32(VS_SLOT_LAUNCH_VEL),
                  &VEH_S32(VS_SLOT_AIR_TIME), &VEH_S32(VS_SLOT_LAUNCH_Y), &VEH_S32(4), &r8768, (TriSideOut *) &r->f12);
    *s3 = r8768.s3;
    regs.s0 = (s32) &VEH_U8(VS_SLOT_AIRBORNE);
    regs.s1 = (s32) &gJbombPos[2];
    jbomb_build_model(&regs);
    veh_set_record(gJbombPos[2], 9, gJbombPos[0], gJbombPos[1], gJbombState);
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/7FB50/jbomb_ride_follow.s")
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
extern u8 * N64P gJbombModel; /* this vehicle's model */
extern u8 gJbombEscaping;  /* "turning back to the ring" flag */
extern u8 gJbombBounced;  /* bounced off the ring end (the bounce code is unreachable here) */
extern u8 gJbombThrottleHold;  /* throttle hold-off countdown */
extern s16 gJbombEscapeHeading; /* ring target heading */
extern u8 gJbombKnocked;
extern s32 gJbombSinceStomp2;
extern s32 gJbombSinceStomp4;
extern u8 gJbombNonBlockingParts[];
void jbomb_update_modes(s32 s0, TriSideOut *f, TriScanRegs *r);
void jbomb_slow_down(void);
void jbomb_check_overhead(s32 x, s32 z, TriSideOut *f);
void jbomb_update_cam_distance(void);
s32 jbomb_steer_rate(void);

/* J-Bomb driven by the player, per frame (0x802C5AFC).
 * Per-frame update of vehicle 9 when it is the player's (veh_update_player and
 * its init jbomb_spawn): zone lookup (jbomb_zone_alpha), gGravity
 * (jbomb_set_gravity), unless +0x9A is set the mode machine (jbomb_update_modes) and
 * the gCamDistance pair (jbomb_update_cam_distance), the tuning values (jbomb_set_tuning),
 * steering (veh_steer at rate jbomb_steer_rate()), throttle (veh_throttle
 * with delta gJbombThrottleStep, unless gJbombBounced; held off while gJbombThrottleHold counts
 * down), braking (veh_coast in mode 0, else jbomb_slow_down), heading
 * target = heading, slope (veh_slope unless +0x96 is 1) and drag
 * (veh_apply_slope in mode 0; other modes move the speed 1 toward 0) unless
 * gJbombBounced, ring turn-back (veh_set_target_heading when gJbombEscaping), the forward
 * step (veh_move_along_heading), the ceiling check jbomb_check_overhead and ground contact
 * (veh_ground_contact), the model (anim_update_model on the current copy pair,
 * jbomb_build_model). Outside mode 5: collision (veh_collision_setup with the mode
 * class 1 (modes 1-3) / 2 (mode 4, or modes 0/6+ while gJbombSinceStomp4 < 3) / 0,
 * veh_collide_spheres, veh_collide_world; a hit, gTrainContact, in modes 1-4 -> mode 2,
 * gJbombKnocked = 1), then either the collision pass obj_collide_vehicle (no ring
 * hit) whose ring-end hit (gCollHit, or +0x9C) sends modes 1-3 (and modes
 * 0/5+ while gJbombSinceStomp2 < 3 or with +0x96 == 1) to mode 2 and otherwise
 * falls into the turn back, or (ring hit) the turn back toward the ring
 * midpoint (veh_prepare_escape, gJbombEscaping = 1, veh_reverse_if_behind, veh_turn_into_free_arc,
 * veh_step_heading, veh_hit_slowdown(0)). Mode 4 and the quiet paths clear
 * gJbombEscaping/Bounced. Finally the position, speed and angles go to gPlayerX.. /
 * gPlayerSpeed.., veh_set_record(z, 9, x, y, block), and gJbombAirborne = mode in
 * 1..4. (The asm also holds a bounce-off-the-ring-end block like
 * ballista_update's, but nothing branches to it.)
 * Register notes (FIDELITY, see tools_port/port_followups.md): jbomb_zone_alpha's
 * scan registers start as the caller's t6, t7, s0-s4 (passed on to
 * jbomb_update_modes -> jbomb_update_jet_effects -> fx_spawn where veh_zone_alpha leaves
 * them) and the FP block f12-f26 as the caller's (only passed through):
 * the C starts them at 0. veh_collision_setup gets b0 = t0 = 9 and h2 = t2 = the
 * model's point list end, which jbomb_build_model (veh_points_transform) leaves:
 * modelled. jbomb_build_model gets s0 = &+0x96, s1 = &D_803F7C00 (left after
 * veh_ground_contact; anim_update_model's own leftovers in the game; only used for a
 * count-0 point record). veh_prepare_escape / veh_collide_spheres take the block in
 * $gp: passed explicitly (gJbombState). The asm leaves
 * v1, f12, f14 as its callees leave them (survey: read by veh_update_player /
 * jbomb_spawn; not real uses). Integer sums trap in the asm. */
void jbomb_update(void) {
    ZoneScanRegs zr;
    TriScanRegs tr;
    TriSideOut f;
    Out802A860C o860c;
    Regs802A8768 r8768;
    MtxChainRegs regs;
    f32 slope;
    s32 x;
    s32 z;
    s32 v;
    s32 mid;
    s32 cur;
    s32 turn;
    s32 tgt;
    s32 cls;
    s32 mode;
    u8 *mdl;

    zr.t6 = zr.t7 = zr.s0 = zr.s1 = zr.s2 = zr.s3 = zr.s4 = 0;
    jbomb_zone_alpha(&zr);
    jbomb_set_gravity();
    if ((s8) VEH_U8(VS_SPAWN_UPDATE) == 0) {
        f.pz = f.cross = f.cz = f.side = f.sideZ = f.dz = 0.0f;
        tr.a1 = 0;
        tr.a3 = 9;
        tr.t6 = zr.t6;
        tr.t7 = 0;
        tr.fp = 0;
        tr.s1 = zr.s1;
        tr.s2 = zr.s2;
        tr.s3 = zr.s3;
        tr.s4 = zr.s4;
        jbomb_update_modes(zr.s0, &f, &tr);
        jbomb_update_cam_distance();
    }
    jbomb_set_tuning();
    veh_steer(jbomb_steer_rate(), (u16 *) &VEH_S16(VS_HEADING));
    if (gJbombBounced == 0) {
        if (gJbombThrottleHold == 0) {
            veh_throttle(gJbombState, &VEH_S16(VS_SPEED), 4, &VEH_U8(VS_SLOT_AIRBORNE), &VEH_S16(VS_BANDS), gJbombThrottleStep);
        } else {
            gJbombThrottleHold--;
        }
    }
    if (VEH_U8(VS_ACTION) == 0) {
        veh_coast(gJbombState);
    } else {
        jbomb_slow_down();
    }
    VEH_S16(VS_MOVE_HEADING) = VEH_S16(VS_HEADING);
    if (VEH_U8(VS_SLOT_AIRBORNE) == 1) {
        slope = 0.0f;
    } else {
        slope = veh_slope(&VEH_S16(VS_SPEED), &VEH_U8(VS_SLOT_AIRBORNE), &VEH_S32(4), (f32 *) &VEH_S32(0));
    }
    if ((s8) gJbombBounced == 0) {
        if (VEH_U8(VS_ACTION) == 0) {
            veh_apply_slope(gJbombState, &VEH_S16(VS_SPEED), 9, (s8 *) &VEH_U8(VS_SLOT_AIRBORNE), &VEH_S32(4), 1, 120.0f);
        } else {
            v = VEH_S16(VS_SPEED);
            if (v > 0) {
                v--;
            } else if (v < 0) {
                v++;
            }
            VEH_S16(VS_SPEED) = v;
        }
    }
    if ((s8) gJbombEscaping != 0) {
        veh_set_target_heading(gJbombState, &gJbombEscapeHeading);
    }
    x = veh_move_along_heading(slope, *(u16 *) &VEH_S16(VS_MOVE_HEADING), &VEH_S16(VS_SPEED), (s32 *) &gJbombPos[0],
                      (s32 *) &gJbombPos[2], &o860c);
    z = o860c.t1;
    f.pz = f.cross = f.cz = f.side = f.sideZ = f.dz = 0.0f;
    jbomb_check_overhead(x, z, &f);
    gBounceSound = 0;
    r8768.s3 = o860c.s3;
    veh_ground_contact(gJbombState, 9, (s32 *) &gJbombPos[0], (s32 *) &gJbombPos[1], (s32 *) &gJbombPos[2], x, z,
                  0x78, 0x78, &VEH_S16(VS_HEADING), &VEH_U8(VS_SLOT_AIRBORNE), &VEH_S16(VS_WHEEL_OFFSETS), &VEH_S32(VS_SLOT_LAUNCH_VEL), &VEH_S32(VS_SLOT_AIR_TIME),
                  &VEH_S32(VS_SLOT_LAUNCH_Y), &VEH_S32(4), &r8768, &f);
    if (gBufferIndex != 0) {
        anim_update_model((u8 *) gJbombMtxA, (u8 *) gJbombMtxB, (AnimChannel *) gJbombAnim);
    } else {
        anim_update_model((u8 *) gJbombMtxB, (u8 *) gJbombMtxA, (AnimChannel *) gJbombAnim);
    }
    regs.s0 = (s32) &VEH_U8(VS_SLOT_AIRBORNE);
    regs.s1 = (s32) &gJbombPos[2];
    jbomb_build_model(&regs);
    gJbombKnocked = 0;
    mode = VEH_U8(VS_ACTION);
    if (mode == 5) {
        goto quiet;
    }
    if (mode == 1 || mode == 2 || mode == 3) {
        cls = 1;
    } else if (mode == 4 || gJbombSinceStomp4 < 3) {
        cls = 2;
    } else {
        cls = 0;
    }
    mdl = gJbombModel; /* t2 as veh_points_transform (in jbomb_build_model) leaves it */
    veh_collision_setup(gJbombPos[2], (s32) gJbombNonBlockingParts, 1, 0, gJbombPos[0], gJbombPos[1], 9, VEH_S16(VS_SPEED),
                  (s32) (mdl + *(s32 *) (mdl + 4)), cls, 9, gJbombState);
    veh_collide_spheres(9, gJbombState);
    veh_collide_world(9);
    if (gTrainContact != 0 && VEH_U8(VS_ACTION) != 0 && VEH_U8(VS_ACTION) != 5) {
        VEH_U8(VS_ACTION) = 2;
        gJbombKnocked = 1;
    }
    if (gCollBlocked == 0) {
        gCollHit = 0;
        gCollVehicleAnim = gJbombAnim;
        obj_collide_vehicle(9, gJbombState);
        if (gCollHit == 0 && VEH_U8(VS_HIT_PART_DOWN) == 0) {
            goto quiet;
        }
        mode = VEH_U8(VS_ACTION);
        if (mode == 1 || mode == 2 || mode == 3) {
            goto toMode2;
        }
        if (mode == 4) {
            goto quiet;
        }
        if (gJbombSinceStomp2 < 3 || VEH_U8(VS_SLOT_AIRBORNE) == 1) {
            goto toMode2;
        }
    }
    /* turn back toward the ring */
    veh_prepare_escape(gJbombState);
    gJbombEscaping = 1;
    mid = veh_free_arc_mid();
    v = *(u16 *) &VEH_S16(VS_MOVE_HEADING) - 0x800;
    if (v < 0) {
        v += 0xFFF;
    }
    v -= mid;
    if (v < 0) {
        v = -v;
    }
    if (!(v < 0x801)) {
        v = 0xFFF - v;
    }
    veh_reverse_if_behind(gJbombState);
    turn = veh_turn_into_free_arc(gJbombState, *(u16 *) &VEH_S16(VS_MOVE_HEADING), *(u16 *) &VEH_S16(VS_HEADING), &cur, 0.25f);
    gJbombEscapeHeading = cur;
    VEH_S16(VS_MOVE_HEADING) = cur;
    VEH_S16(VS_MOMENTUM_HEADING) = cur;
    veh_step_heading(gJbombState, turn, v, &tgt);
    veh_hit_slowdown(gJbombState, 0);
    goto done;
toMode2:
    VEH_U8(VS_ACTION) = 2;
    gJbombKnocked = 1;
    goto done;
quiet:
    gJbombEscaping = 0;
    gJbombBounced = 0;
done:
    gPlayerX = gJbombPos[0];
    gPlayerY = gJbombPos[1];
    gPlayerZ = gJbombPos[2];
    gPlayerSpeed = VEH_S16(VS_SPEED);
    gPlayerMoveHeading = *(u16 *) &VEH_S16(VS_MOVE_HEADING);
    gPlayerHeading = *(u16 *) &VEH_S16(VS_HEADING);
    veh_set_record(gJbombPos[2], 9, gJbombPos[0], gJbombPos[1], gJbombState);
    mode = VEH_U8(VS_ACTION);
    gJbombAirborne = (mode != 0 && mode != 5) ? 1 : 0;
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/7FB50/jbomb_update.s")
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
/* No stick input: speed 8 toward 0 (when not walking) (0x802C617C).
 * Unless gControlMethod or gPadStickY is set, moves the s16 at +0x76 of the
 * vehicle block 8 towards 0 (stopping at 0). The asm saves s7, the only
 * register it uses; asm caller jbomb_update keeps a0-a3 and t6 live (a
 * mixed N64 build would need a thunk, the native port doesn't). */
void jbomb_slow_down(void) {
    s32 v;

    if (gControlMethod != 0 || gPadStickY != 0) {
        return;
    }
    v = VEH_S16(VS_SPEED);
    if (v >= 0) {
        v -= 8;
        if (v < 0) {
            v = 0;
        }
    } else {
        v += 8;
        if (v > 0) {
            v = 0;
        }
    }
    VEH_S16(VS_SPEED) = v;
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/7FB50/jbomb_slow_down.s")
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
extern s32 gJbombSinceStomp2; /* frames since the countdown gJbombStompTimer was at 2/3 */
extern s32 gJbombSinceStomp4; /* frames since it was at 4 */
extern s16 gJbombStepFrame; /* channel 6's last field 0x18 */
extern u8 gJbombKnocked;
extern u8 gJbombThrustHold; /* countdown */
extern u8 gJbombStompTimer; /* countdown */
extern u8 gJbombStompDelay; /* countdown (reloaded with 9 outside modes 1-3) */
extern u8 gJbombPrevHeldB; /* previous frame's gHeldB */
extern u8 gJbombPrevAirborne; /* previous frame's +0x96 */
extern u8 gJbombTapPhaseL;
extern u8 gJbombTapTimerL;
extern u8 gJbombTapPhaseR;
extern u8 gJbombTapTimerR;
extern u8 gJbombStompDropPending;
extern f32 gJbombIdleTakeoffPitch;
s32 jbomb_thrust_speed(void);
s32 jbomb_check_surface_above(TriSideOut *f, TriScanRegs *r);
void jbomb_lift_off_water(void);
void jbomb_update_jet_effects(s32 t6, s32 t7, s32 s0, s32 s1, s32 s2, s32 s3, s32 s4);
void jbomb_check_idle(void);
void jbomb_update_jet_sounds(void);
void jbomb_track_lr_taps(void);
void jbomb_update_tilt(s32 *up);

/* Three s32 copies of one value at +off, +off+4, +off+8 of the vehicle block. */
#define VEH_TRIPLE(off, v) (VEH_S32(off) = VEH_S32((off) + 4) = VEH_S32((off) + 8) = (v))

/* Walk / fly / fall / dive / idle state machine (+0xA1) (0x802C61F0).
 * Per-frame update of this vehicle's mode machine (+0xA1: 0 idle/driving,
 * 1 airborne?, 2/3, 4, 5; +0xA2 keeps the mode the frame started in), its
 * countdowns and animation channels (gJbombAnim), then the ground check
 * jbomb_check_surface_above, jbomb_lift_off_water, the steering channels (jbomb_update_tilt, modes
 * 1-3), the ramp/records jbomb_update_jet_effects, the idle check jbomb_check_idle and
 * the looping sounds jbomb_update_jet_sounds. Mode values above 5 hit a `syscall` in
 * the asm (not reached in the game; the C does nothing there).
 * Register convention (conventions.txt): s0 comes in and goes on to
 * jbomb_update_jet_effects; the FP registers f12-f26 and a3, t6, s1-s4 come in for
 * jbomb_check_surface_above (here through f and r), and its f20-f26 are what the caller
 * jbomb_update reads back. t6, t7, s1-s4 after jbomb_check_surface_above (or the
 * zeros/fx_spawn results on the mode-4 exit path, s4 = |speed| / 16 in
 * mode 0, s2 from jbomb_update_tilt) are what jbomb_update_jet_effects gets. Where an asm
 * callee leaves an undocumented register behind (jbomb_update_tilt leaves s3 =
 * 270 on its steering paths, snd_play is C and may change t6/t7) the
 * C passes the documented value instead. $gp (= gJbombState) is read as the
 * global. Integer counters use trapping `addi` in the asm. */
void jbomb_update_modes(s32 s0, TriSideOut *f, TriScanRegs *r) {
    s32 mode = VEH_U8(VS_ACTION);
    s32 v;
    s32 t6;
    s32 t7;
    s32 s1;
    s32 s2;
    s32 s3;
    s32 s4;
    s16 old;
    s32 ch[8];
    Io802A6274 io;

    VEH_U8(VS_ACTION_AUX) = mode;
    if (mode == 1 || mode == 2 || mode == 3) {
        if (gJbombStompDelay != 0) {
            gJbombStompDelay--;
        }
    } else {
        gJbombStompDelay = 9;
    }
    v = gJbombStompTimer;
    if (v != 0) {
        v--;
        gJbombStompTimer = v;
    }
    if (v == 2 || v == 3) {
        gJbombSinceStomp2 = 0;
    } else {
        gJbombSinceStomp2++;
    }
    if (v == 4) {
        gJbombSinceStomp4 = 0;
    } else {
        gJbombSinceStomp4++;
    }

    gJbombThrusting = 0;
    if (!(gJbombMaxY < gJbombPosY) && VEH_U8(VS_ACTION) != 4 && VEH_U8(VS_ACTION) != 2) {
        if (gJbombThrustHold != 0) {
            gJbombThrustHold--;
        } else if (gHeldA != 0 || gHeldL != 0 || gHeldR != 0) {
            if (VEH_U8(VS_SLOT_AIRBORNE) != 1) {
                VEH_TRIPLE(0x34, gJbombPosY);
                VEH_TRIPLE(0x40, 1);
                VEH_TRIPLE(0x28, 30);
                VEH_U8(VS_SLOT_AIRBORNE) = 1;
                VEH_U8(0x97) = 1;
                VEH_U8(0x98) = 1;
            } else {
                v = jbomb_thrust_speed();
                VEH_TRIPLE(0x40, 1);
                VEH_TRIPLE(0x28, v);
                VEH_TRIPLE(0x34, gJbombPosY);
            }
            gJbombThrusting = 1;
        }
    }

    if (VEH_U8(VS_ACTION) == 5 && (VEH_S16(VS_SPEED) != 0 || VEH_U8(VS_SLOT_AIRBORNE) != 0)) {
        VEH_U8(VS_ACTION) = 0;
        anim_stop(7, (AnimChannel *) gJbombAnim);
        anim_stop(8, (AnimChannel *) gJbombAnim);
        anim_set_frame(0.0f, (AnimChannel *) gJbombAnim, 6, 2);
        anim_set_frame(0.0f, (AnimChannel *) gJbombAnim, 9, 0);
        anim_set_speed((AnimChannel *) gJbombAnim, 9, 3);
        anim_set_reverse((AnimChannel *) gJbombAnim, 9, 0);
        anim_set_interp(0.0f, (AnimChannel *) gJbombAnim, 9, 0);
        anim_set_loop_mode((AnimChannel *) gJbombAnim, 9, 1);
        anim_play((AnimChannel *) gJbombAnim, 9, 1);
    }

    if (VEH_U8(VS_HIT_IMMUNE_PART) != 0) {
        VEH_U8(VS_ACTION) = 3;
        gJbombKnocked = 1;
    } else if (gJbombPrevAirborne != 1) {
        if (VEH_U8(VS_SLOT_AIRBORNE) == 1) {
            anim_stop(6, (AnimChannel *) gJbombAnim);
            anim_stop(9, (AnimChannel *) gJbombAnim);
            gJbombPitch = (VEH_U8(VS_ACTION_AUX) == 5) ? gJbombIdleTakeoffPitch : 0.5f;
            gJbombRoll = 0.5f;
            VEH_U8(VS_ACTION) = 1;
        }
    } else if (VEH_U8(VS_SLOT_AIRBORNE) == 0 && gJbombKnocked == 0) {
        /* landed: reset the position triples */
        v = gJbombPosY;
        VEH_S32(VS_SLOT_Y) = v;
        VEH_S32(0x08) = v;
        VEH_S32(0x0C) = v;
        VEH_S32(0x10) = v;
        VEH_S32(0x14) = v;
        VEH_S32(0x18) = v;
        VEH_S32(0x1C) = v;
        VEH_S32(0x20) = v;
        VEH_S32(0x24) = v;
        anim_set_frame(0.0f, (AnimChannel *) gJbombAnim, 6, 2);
        anim_stop(5, (AnimChannel *) gJbombAnim);
        anim_stop(4, (AnimChannel *) gJbombAnim);
        anim_stop(3, (AnimChannel *) gJbombAnim);
        if (VEH_U8(VS_ACTION_AUX) != 4) {
            anim_merge_poses(3, 4, (AnimChannel *) gJbombAnim);
            anim_blend_channels(0x1E, 6, (AnimChannel *) gJbombAnim);
            anim_set_speed((AnimChannel *) gJbombAnim, 0x1F, 0x32);
            anim_set_reverse((AnimChannel *) gJbombAnim, 0x1F, 0);
            anim_set_loop_mode((AnimChannel *) gJbombAnim, 0x1F, 0);
            anim_play((AnimChannel *) gJbombAnim, 0x1F, 1);
        }
        s4 = VEH_S16(VS_SPEED);
        if (s4 >= 0x97) {
            s4 = 0x96;
            VEH_S16(VS_SPEED) = s4;
        } else if (s4 < -0x64) {
            s4 = -0x64;
            VEH_S16(VS_SPEED) = s4;
        }
        r->s4 = s4; /* the asm's s4, on into jbomb_check_surface_above */
        VEH_U8(VS_ACTION) = 0;
    }

    jbomb_check_surface_above(f, r);
    jbomb_lift_off_water();
    t6 = r->t6;
    t7 = r->t7;
    s1 = r->s1;
    s2 = r->s2;
    s3 = r->s3;
    s4 = r->s4;

    if (VEH_U8(VS_ACTION_AUX) == 4 && VEH_U8(VS_ACTION) == 0) {
        snd_play(gSfxBank, SFX_JBOMB_STOMP_LAND, NULL);
        gShakeTime = 0x14;
        gShakeSize = 0x320;
        io.a3 = 0;
        io.t6 = 0;
        io.s1 = 0;
        fx_spawn(&io, gFxDefKind04, 0x222E0, 0, ((s32 *) gJbombPos)[0] << 11, gJbombPosY << 11,
                      ((s32 *) gJbombPos)[2] << 11, 0, 0, 0, 0, 0, 0);
        t6 = io.t6;
        s1 = io.s1;
        t7 = 0;
        s0 = 0;
        s2 = 0;
        s3 = 0;
        s4 = 0;
    }

    switch (VEH_U8(VS_ACTION)) {
        case 5:
            anim_get_state(7, (AnimChannel *) gJbombAnim, ch);
            if (ch[0] == 1) {
                break;
            }
            anim_get_state(8, (AnimChannel *) gJbombAnim, ch);
            if (ch[0] == 1) {
                break;
            }
            anim_set_frame(0.0f, (AnimChannel *) gJbombAnim, 8, 0);
            anim_set_speed((AnimChannel *) gJbombAnim, 8, 1);
            anim_set_reverse((AnimChannel *) gJbombAnim, 8, 0);
            anim_set_interp(0.5f, (AnimChannel *) gJbombAnim, 8, 1);
            anim_set_loop_mode((AnimChannel *) gJbombAnim, 8, 1);
            anim_play((AnimChannel *) gJbombAnim, 8, -1);
            break;

        case 4:
            if (gJbombStompDropPending != 0) {
                anim_get_state(5, (AnimChannel *) gJbombAnim, ch);
                if (ch[0] != 1) {
                    VEH_TRIPLE(0x34, gJbombPosY);
                    VEH_TRIPLE(0x40, 1);
                    VEH_TRIPLE(0x28, -0x4B0);
                    gJbombStompDropPending = 0;
                }
            }
            gJbombThrustHold = 10;
            break;

        case 0:
            anim_get_state(0x1F, (AnimChannel *) gJbombAnim, ch);
            if (ch[0] == 1) {
                break;
            }
            anim_get_state(9, (AnimChannel *) gJbombAnim, ch);
            if (ch[0] == 1) {
                break;
            }
            s4 = VEH_S16(VS_SPEED);
            anim_set_reverse((AnimChannel *) gJbombAnim, 6, (s4 < 0) ? 1 : 0);
            if (s4 < 0) {
                s4 = -s4;
            }
            s4 = (u32) s4 / 16;
            anim_set_speed((AnimChannel *) gJbombAnim, 6, s4);
            anim_set_interp(0.5f, (AnimChannel *) gJbombAnim, 6, 1);
            anim_set_loop_mode((AnimChannel *) gJbombAnim, 6, 0);
            anim_play((AnimChannel *) gJbombAnim, 6, -1);
            anim_get_state(6, (AnimChannel *) gJbombAnim, ch);
            /* ch[6] is the asm's t1 (the whole word; the store keeps 16 bits) */
            old = gJbombStepFrame;
            gJbombStepFrame = ch[6];
            if (ch[6] != old) {
                if (ch[6] == 0) {
                    snd_play(gSfxBank, SFX_JBOMB_STEP_A, NULL);
                } else if (ch[6] == 3) {
                    snd_play(gSfxBank, SFX_JBOMB_STEP_B, NULL);
                }
            }
            break;

        case 2:
            gJbombThrustHold = 10;
            if (gHeldA != 0 || gHeldL != 0 || gHeldR != 0) {
                v = jbomb_thrust_speed();
                VEH_TRIPLE(0x40, 1);
                VEH_TRIPLE(0x28, v);
                VEH_TRIPLE(0x34, gJbombPosY);
                gJbombThrusting = 1;
                VEH_U8(VS_ACTION) = 1;
            }
            /* fall through */
        case 1:
        case 3:
            anim_stop(0x1F, (AnimChannel *) gJbombAnim);
            anim_stop(6, (AnimChannel *) gJbombAnim);
            anim_play((AnimChannel *) gJbombAnim, 4, -1);
            anim_play((AnimChannel *) gJbombAnim, 3, -1);
            jbomb_update_tilt(&s2);
            if (gJbombKnocked != 0) {
                if (VEH_U8(VS_HIT_PART_DOWN) == 0) {
                    v = VEH_S16(VS_SPEED);
                    if (v >= 0x15) {
                        v = 0x14;
                    }
                    if (v < -0x14) {
                        v = -0x14;
                    }
                    VEH_S16(VS_SPEED) = v;
                }
                VEH_TRIPLE(0x34, gJbombPosY);
                VEH_TRIPLE(0x40, 1);
                VEH_TRIPLE(0x28, 0x14A);
                VEH_U8(VS_SLOT_AIRBORNE) = 1;
                VEH_U8(0x97) = 1;
                VEH_U8(0x98) = 1;
                break;
            }
            if (gJbombStompTimer != 0 || gJbombStompDelay != 0 || VEH_U8(VS_SURFACE_MAX) == SURFACE_WATER) {
                break;
            }
            jbomb_track_lr_taps();
            if (gJbombTapPhaseL != 3 && (gJbombPrevHeldB != 0 || gHeldB == 0)) {
                break;
            }
            /* both press/release trackers done, or a new gHeldB press: mode 4 */
            gJbombTapTimerL = 0;
            gJbombTapPhaseL = 0;
            gJbombTapTimerR = 0;
            gJbombTapPhaseR = 0;
            gJbombStompTimer = 20;
            gJbombStompDropPending = 1;
            VEH_U8(VS_ACTION) = 4;
            VEH_TRIPLE(0x28, 0);
            VEH_TRIPLE(0x34, gJbombPosY);
            VEH_TRIPLE(0x40, 1);
            anim_stop(4, (AnimChannel *) gJbombAnim);
            anim_stop(3, (AnimChannel *) gJbombAnim);
            anim_set_speed((AnimChannel *) gJbombAnim, 5, 10);
            anim_set_reverse((AnimChannel *) gJbombAnim, 5, 0);
            anim_set_loop_mode((AnimChannel *) gJbombAnim, 5, 1);
            anim_set_frame(0.0f, (AnimChannel *) gJbombAnim, 5, 0);
            anim_play((AnimChannel *) gJbombAnim, 5, 1);
            snd_play(gSfxBank, SFX_JBOMB_STOMP, NULL);
            break;

        default:
            /* the asm executes `syscall` here (no mode above 5 exists) */
            break;
    }

    jbomb_update_jet_effects(t6, t7, s0, s1, s2, s3, s4);
    jbomb_check_idle();
    jbomb_update_jet_sounds();
    gJbombPrevAirborne = VEH_U8(VS_SLOT_AIRBORNE);
    gJbombPrevHeldB = gHeldB;
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/7FB50/jbomb_update_modes.s")
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
extern u8 gJbombIdleTimer; /* idle countdown */

/* 30 still frames walking: the idle animation (mode 5) (0x802C6DAC).
 * Idle check: while the vehicle moves (+0x76 != 0) or is in a mode
 * (+0xA1 != 0) the countdown gJbombIdleTimer is reloaded with 30, else it counts
 * down to 0. In mode 0, stopped, with the countdown at 0: mode 5, channel 6
 * stopped and channel 7 armed (0.0 / 0, 3, 0, 0.0 / 0, 1, restart with 1).
 * The asm reads the block through $gp (= gJbombState, set by its caller);
 * asm caller jbomb_update_modes keeps a1-a3 live (a mixed N64 build would need a
 * thunk; the native port won't). */
void jbomb_check_idle(void) {
    if (VEH_S16(VS_SPEED) != 0 || VEH_U8(VS_ACTION) != 0) {
        gJbombIdleTimer = 30;
    } else if (gJbombIdleTimer != 0) {
        gJbombIdleTimer--;
    }
    if (VEH_U8(VS_ACTION) != 0 || VEH_S16(VS_SPEED) != 0 || gJbombIdleTimer != 0) {
        return;
    }
    VEH_U8(VS_ACTION) = 5;
    anim_stop(6, (AnimChannel *) gJbombAnim);
    anim_set_frame(0.0f, (AnimChannel *) gJbombAnim, 7, 0);
    anim_set_speed((AnimChannel *) gJbombAnim, 7, 3);
    anim_set_reverse((AnimChannel *) gJbombAnim, 7, 0);
    anim_set_interp(0.0f, (AnimChannel *) gJbombAnim, 7, 0);
    anim_set_loop_mode((AnimChannel *) gJbombAnim, 7, 1);
    anim_play((AnimChannel *) gJbombAnim, 7, 1);
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/7FB50/jbomb_check_idle.s")
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
/* Thrust sound 2 and the air loop 0xF (0x802C6ECC).
 * Starts/stops the two looping vehicle sounds (handles gJbombThrustSound and
 * gJbombAirSound) for the current mode (+0xA1) and gJbombThrusting. The survey lists
 * a1-a3 as outputs read by jbomb_update_modes's callers: they are only what the
 * sound calls leave behind, nothing this function sets. */
void jbomb_update_jet_sounds(void) {
    s32 mode = VEH_U8(VS_ACTION);

    if (mode == 0 || mode == 4) {
        if (gJbombAirSound != NULL) {
            snd_stop(gJbombAirSound);
        }
    }
    if (gJbombThrusting == 0) {
        if (gJbombThrustSound != NULL) {
            snd_stop(gJbombThrustSound);
        }
        mode = VEH_U8(VS_ACTION);
        if (mode == 1 || mode == 2 || mode == 3) {
            if (gJbombAirSound == NULL) {
                snd_play(gSfxBank, SFX_JBOMB_AIR, &gJbombAirSound);
            }
        }
    } else {
        if (gJbombThrustSound == NULL) {
            snd_play(gSfxBank, SFX_JBOMB_THRUST, &gJbombThrustSound);
        }
        if (gJbombAirSound != NULL) {
            snd_stop(gJbombAirSound);
        }
    }
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/7FB50/jbomb_update_jet_sounds.s")
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
/* The next upward speed, fading with height (0x802C6FD8).
 * With a = +0x28, n = +0x40 of the vehicle block and k = gGravity:
 * (a*n + round(k*n*n)) - (a*(n-1) + round(k*(n-1)^2)) + bonus, where bonus
 * is 30 when gJbombPosY < gJbombLiftFadeY, else
 * 30 - (u32)((gJbombPosY - gJbombLiftFadeY) * 30) / (u32)(gJbombMaxY - gJbombLiftFadeY).
 * Products are 32-bit; round() is cvt.w.s (nearest, ties to even).
 * The asm returns the value in s6 and also changes s7 and a1 (scratch);
 * asm caller jbomb_update_modes keeps a2, a3, t6, t7, f12 and f14 live (a mixed
 * N64 build would need a thunk, the native port doesn't). */
s32 jbomb_thrust_speed(void) {
    s32 a = VEH_S32(VS_SLOT_LAUNCH_VEL);
    s32 n = VEH_S32(VS_SLOT_AIR_TIME);
    s32 r;
    s32 sum;
    s32 bonus;

    CVT_W_S(r, gGravity * (f32) (n * n));
    sum = a * n + r;
    n = VEH_S32(VS_SLOT_AIR_TIME) - 1;
    a = VEH_S32(VS_SLOT_LAUNCH_VEL);
    CVT_W_S(r, gGravity * (f32) (n * n));
    sum -= a * n + r;
    if (gJbombPosY < gJbombLiftFadeY) {
        bonus = 30;
    } else {
        bonus = 30 - (u32) ((gJbombPosY - gJbombLiftFadeY) * 30) / (u32) (gJbombMaxY - gJbombLiftFadeY);
    }
    return sum + bonus;
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/7FB50/jbomb_thrust_speed.s")
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
extern u32 gJbombPos[]; /* x, y, z */

/* A surface 0..3000 above: pushed up (mode 1) (0x802C70E8).
 * Ground check below the vehicle (gJbombPos x/z, gJbombPosY y) with
 * ground_scan_guard_tris: when a triangle was found whose height h >= y, and h - y is
 * below 200, the vehicle snaps to mode 1 (+0xA1 = 1, triples +0x34.. = y,
 * +0x40.. = 1, +0x28.. = 30, flags +0x96..0x98 = 1, gJbombThrusting = 1); below
 * 3000 the same with +0x28.. = 0 and gJbombThrusting left alone. Returns the asm's
 * v1: 1 when it switched, else ground_scan_guard_tris's result.
 * Register convention: ground_scan_guard_tris's FP results (f12-f26) and scan
 * registers (a3, t6, t7, fp, s1-s4) pass through to the caller, here through
 * f and r (conventions.txt); $gp (= gJbombState) is read as the global. */
s32 jbomb_check_surface_above(TriSideOut *f, TriScanRegs *r) {
    s32 best = ground_scan_guard_tris(gJbombPos[0], gJbombPos[2], gJbombPosY, f, r);
    s32 d;
    s32 w;

    if (r->a1 == 0 || gGuardTrisBestY < gJbombPosY) {
        return best;
    }
    d = gGuardTrisBestY - gJbombPosY;
    if (d < 200) {
        w = 30;
    } else if (d < 3000) {
        w = 0;
    } else {
        return best;
    }
    VEH_U8(VS_ACTION) = 1;
    VEH_S32(VS_SLOT_LAUNCH_Y) = gJbombPosY;
    VEH_S32(0x38) = gJbombPosY;
    VEH_S32(0x3C) = gJbombPosY;
    VEH_S32(VS_SLOT_AIR_TIME) = 1;
    VEH_S32(0x44) = 1;
    VEH_S32(0x48) = 1;
    VEH_S32(VS_SLOT_LAUNCH_VEL) = w;
    VEH_S32(0x2C) = w;
    VEH_S32(0x30) = w;
    VEH_U8(VS_SLOT_AIRBORNE) = 1;
    VEH_U8(0x97) = 1;
    VEH_U8(0x98) = 1;
    if (d < 200) {
        gJbombThrusting = 1;
    }
    return 1;
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/7FB50/jbomb_check_surface_above.s")
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING

/* A surface >= 200 above the new x/z (0x802C71FC).
 * gJbombCeilingAbove = 1 when ground_scan_guard_tris at (x, z, gJbombPosY) finds ground at
 * least 200 above gJbombPosY, else 0.
 * Register convention: x t0, z t1; ground_scan_guard_tris's FP results (f12-f26) are
 * left for the caller, here through f (in/out; conventions.txt). The asm
 * saves and restores every integer register; asm caller jbomb_update keeps
 * t0, t1 and t7 live (a mixed N64 build would need a thunk; the native port
 * won't). ground_scan_guard_tris's scan-register inputs are whatever the caller left
 * in a3/t6/s1-s4; they only pass through, so the C hands it zeros. */
void jbomb_check_overhead(s32 x, s32 z, TriSideOut *f) {
    TriScanRegs r;

    r.a1 = 0;
    r.a3 = 0;
    r.t6 = 0;
    r.t7 = 0;
    r.fp = 0;
    r.s1 = 0;
    r.s2 = 0;
    r.s3 = 0;
    r.s4 = 0;
    gJbombCeilingAbove = 0;
    ground_scan_guard_tris(x, z, gJbombPosY, f, &r);
    if (r.a1 != 0 && gGuardTrisBestY >= gJbombPosY && gGuardTrisBestY - gJbombPosY >= 200) {
        gJbombCeilingAbove = 1;
    }
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/7FB50/jbomb_check_overhead.s")
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
/* J-Bomb on surface 100 (water): outside mode 1 resets gJbombPitch/Roll to 0.5; below the shadow height + 2500
 * it is lifted into mode 1 with thrust on (0x802C7354).
 * When +0x9F of the vehicle block is 100: outside mode 1, resets
 * gJbombPitch/gJbombRoll to 0.5; then, if gJbombPosY < veh_shadow_src_y(9) +
 * 2500, switches to mode 1 (triples +0x34.. = gJbombPosY, +0x40.. = 1,
 * +0x28.. = 30, flags +0x96..0x98 = 1, gJbombThrusting = 1).
 * The survey lists v1, t6, t7 as outputs read by jbomb_update_modes: v1 is only
 * scratch on the taken path and t6/t7 are untouched. Asm caller
 * jbomb_update_modes keeps a1-a3, f12 and f14 live (a mixed N64 build would need
 * a thunk, the native port doesn't). */
void jbomb_lift_off_water(void) {
    s32 limit;
    s32 cur;

    if (VEH_U8(VS_SURFACE_MAX) != SURFACE_WATER) {
        return;
    }
    if (VEH_U8(VS_ACTION) != 1) {
        gJbombPitch = 0.5f;
        gJbombRoll = 0.5f;
    }
    limit = veh_shadow_src_y(9) + 2500;
    cur = gJbombPosY;
    if (cur < limit) {
        VEH_S32(VS_SLOT_LAUNCH_Y) = cur;
        VEH_S32(0x38) = cur;
        VEH_S32(0x3C) = cur;
        VEH_S32(VS_SLOT_AIR_TIME) = 1;
        VEH_S32(0x44) = 1;
        VEH_S32(0x48) = 1;
        VEH_S32(VS_SLOT_LAUNCH_VEL) = 30;
        VEH_S32(0x2C) = 30;
        VEH_S32(0x30) = 30;
        VEH_U8(VS_SLOT_AIRBORNE) = 1;
        VEH_U8(0x97) = 1;
        VEH_U8(0x98) = 1;
        VEH_U8(VS_ACTION) = 1;
        gJbombThrusting = 1;
    }
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/7FB50/jbomb_lift_off_water.s")
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
/* GCamDistance and gCamHeightBase by altitude (0x802C7410).
 * Sets the pair gCamDistance / gCamHeightBase from where gJbombPosY lies in
 * [gJbombLiftFadeY, gJbombMaxY): (0xD48, 0x258) below it, (1000, 3000) at or
 * past the end, else interpolated (0xD48 - 2400*t, 0x258 + 2400*t with
 * t = (cur - lo) / (hi - lo), signed division). The survey lists a0-a3 as
 * outputs read by jbomb_update: they're scratch (a0 = gJbombMaxY). */
void jbomb_update_cam_distance(void) {
    s32 cur = gJbombPosY;
    s32 lo = gJbombLiftFadeY;
    s32 hi = gJbombMaxY;

    if (cur < lo) {
        gCamDistance = 0xD48;
        gCamHeightBase = 0x258;
    } else if (cur >= hi) {
        gCamDistance = 1000;
        gCamHeightBase = 3000;
    } else {
        s32 num = cur - lo;
        s32 den = hi - lo;

        gCamDistance = num * -0x960 / den + 0xD48;
        gCamHeightBase = num * 0x960 / den + 0x258;
    }
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/7FB50/jbomb_update_cam_distance.s")
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING

extern u8 gJbombPrevAirborne; /* previous frame's +0x96 */
extern s8 gJbombJetRamp; /* 0..50 ramp */
extern u8 gJbombFlameTimer; /* countdown */

/* Touchdown dust, the jet ramp (channel 1), jet flames (0x802C7544).
 * When +0x96 just became 1 (gJbombPrevAirborne == 0) with gJbombThrusting set:
 * ptcl_start(4, record (9, 1) of veh_find_point, veh_shadow_floor_y(9)). Then
 * the ramp gJbombJetRamp steps up (to 50) in mode 5 or with gJbombThrusting set,
 * else down (to 0), and channel 1 of gJbombAnim gets ramp / 4 (divu, as the
 * asm). The countdown gJbombFlameTimer, when nonzero, just counts down; at zero it
 * restarts at 4 in mode 5 (1 with gJbombThrusting set, else nothing more happens)
 * and two fx_spawn records are set up (def gFxDefJetFlame, data 0x15F90,
 * type 1 at (9, 1, 1) and (9, 2, 1)).
 * Register convention: the asm passes t6, t7, s0-s4 through to
 * fx_spawn (t6 and s1 in its in/out block; s3 is jbomb_roll_limit's 270);
 * $gp (= gJbombState) is read as the global. It leaves fx_spawn's a3,
 * t6 and s1 and changes s5-s7 (conventions.txt: clobbers). */
void jbomb_update_jet_effects(s32 t6, s32 t7, s32 s0, s32 s1, s32 s2, s32 s3, s32 s4) {
    Io802A6274 io;
    u8 *rec;
    s32 floor;
    s32 v;

    if (gJbombPrevAirborne == 0 && VEH_U8(VS_SLOT_AIRBORNE) == 1 && gJbombThrusting != 0) {
        floor = veh_shadow_floor_y(9);
        veh_find_point(9, 1, &rec);
        ptcl_start(4, ((s32 *) rec)[0], ((s32 *) rec)[1], ((s32 *) rec)[2], floor);
    }
    if (VEH_U8(VS_ACTION) == 5 || gJbombThrusting != 0) {
        v = gJbombJetRamp + 1;
        if (v >= 0x33) {
            v = 0x32;
        }
    } else {
        v = gJbombJetRamp - 1;
        if (v < 0) {
            v = 0;
        }
    }
    gJbombJetRamp = v;
    anim_set_speed((AnimChannel *) gJbombAnim, 1, (u32) (s32) gJbombJetRamp / 4);
    if (gJbombFlameTimer != 0) {
        gJbombFlameTimer--;
        return;
    }
    if (VEH_U8(VS_ACTION) == 5) {
        gJbombFlameTimer = 4;
    } else if (gJbombThrusting != 0) {
        gJbombFlameTimer = 1;
    } else {
        return;
    }
    io.a3 = 1;
    io.t6 = t6;
    io.s1 = s1;
    fx_spawn(&io, gFxDefJetFlame, 0x15F90, 1, 9, 1, 1, t7, s0, s2, s3, s4, 1);
    io.a3 = 1;
    fx_spawn(&io, gFxDefJetFlame, 0x15F90, 1, 9, 2, 1, t7, s0, s2, s3, s4, 1);
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/7FB50/jbomb_update_jet_effects.s")
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
extern u8 gJbombTapPhaseL; /* phase 0 (odd = waiting for input 0 to be released) */
extern u8 gJbombTapTimerL; /* timer 0 */
extern u8 gJbombTapPhaseR; /* phase 1 */
extern u8 gJbombTapTimerR; /* timer 1 */

/* One step of a press/release tracker: when the timer has run out the phase
 * is reset; otherwise the phase advances (and the timer restarts at 4) once
 * the input reaches the state the phase is waiting for (pressed on even
 * phases, released on odd ones), and the timer counts down while it hasn't.
 * (A macro, not a function: the original is a single asm leaf.) */
#define PRESS_TRACKER_STEP(phase, timer, input)                     \
    {                                                               \
        if ((timer) == 0) {                                         \
            (phase) = 0;                                            \
            (timer) = 4;                                            \
        } else if (((phase) & 1) ? ((input) == 0) : ((input) != 0)) { \
            (timer) = 4;                                            \
            (phase)++;                                              \
        } else {                                                    \
            (timer)--;                                              \
        }                                                           \
    }

/* Press/release trackers on gHeldL and gHeldR (0x802C770C).
 * Update the two press/release trackers (gJbombTapPhaseL/TimerL on gHeldL and
 * gJbombTapPhaseR/TimerR on gHeldR).
 * Register note: the asm touches only at/v0/v1/a0; its caller jbomb_update_modes
 * keeps a1-a3, t6, t7, f12 and f14 live across the call (a mixed N64 build
 * would need a thunk, the native port does not). */
void jbomb_track_lr_taps(void) {
    PRESS_TRACKER_STEP(gJbombTapPhaseL, gJbombTapTimerL, gHeldL);
    PRESS_TRACKER_STEP(gJbombTapPhaseR, gJbombTapTimerR, gHeldR);
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/7FB50/jbomb_track_lr_taps.s")
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
extern f32 gJbombRollStepLeft;
extern f32 gJbombRollStepRight;
extern f32 gJbombRollRecentreStep;
extern f32 gJbombPitchStep;
extern f32 gJbombPitchRecentreStep;
f32 jbomb_pitch_limit(void);
f32 jbomb_roll_limit(s32 up);

/* Moves f towards 0.5 by step without passing it (NaN goes the "above"
 * way, as the asm's compares do). */
#define CENTER_STEP(f, step)          \
    {                                 \
        if ((f) < 0.5f) {             \
            (f) += (step);            \
            if (!((f) <= 0.5f)) {     \
                (f) = 0.5f;           \
            }                         \
        } else {                      \
            (f) -= (step);            \
            if ((f) < 0.5f) {         \
                (f) = 0.5f;           \
            }                         \
        }                             \
    }

/* Roll (channel 4, stick x) and pitch (channel 3, stick y) (0x802C7864).
 * Steering / tilt animation (0.5 = centred). gJbombRoll: while steering
 * (gControlMethod set or |gContSteerError| large: >= 4000 or < -3999) with
 * gPadStickX >= 31 it rises by gJbombRollStepRight up to jbomb_roll_limit(1), with
 * gPadStickX < -30 it falls by gJbombRollStepLeft down to jbomb_roll_limit(0) (the up
 * flag is left in *up, the asm's s2); otherwise it steps back to 0.5 by
 * gJbombRollRecentreStep. Channel 4 of gJbombAnim then gets 1 - it as (2x, 1) below
 * 0.5 or (2(x - 0.5), 2) above. gJbombPitch likewise with step
 * gJbombPitchStep * (1 - 2|x - 0.5|): gPadStickY > 0 raises it up to
 * 1 - jbomb_pitch_limit() (if not already above), < 0 lowers it down to
 * jbomb_pitch_limit() (if not already below), else (or when outside) it centres
 * by gJbombPitchRecentreStep; channel 3 gets it in the same two-range form.
 * The asm also computes round(100 * (1 - gJbombRoll)) into t2 and drops it,
 * clobbers s5, and keeps jbomb_roll_limit's s3 = 270 (conventions.txt:
 * clobbers s3, s5). Asm caller jbomb_update_modes keeps a1-a3, t6, t7, f12, f14
 * live (a mixed N64 build would need a thunk; the native port won't). */
void jbomb_update_tilt(s32 *up) {
    f32 f = gJbombRoll;
    f32 lim;
    f32 step;
    f32 w;
    s32 s;

    if (gControlMethod != 0 || gContSteerError >= 0xFA0 || gContSteerError < -0xF9F) {
        s = gPadStickX;
        if (s >= 0x1F) {
            *up = 1;
            f += gJbombRollStepRight;
            lim = jbomb_roll_limit(1);
            if (!(f <= lim)) {
                f = lim;
            }
            goto store1;
        }
        if (s < -0x1E) {
            *up = 0;
            f -= gJbombRollStepLeft;
            lim = jbomb_roll_limit(0);
            if (f < lim) {
                f = lim;
            }
            goto store1;
        }
    }
    step = gJbombRollRecentreStep;
    CENTER_STEP(f, step);
store1:
    gJbombRoll = f;
    f = 1.0f - f;
    if (f <= 0.5f) {
        anim_set_frame(f * 2.0f, (AnimChannel *) gJbombAnim, 4, 1);
    } else {
        anim_set_frame((f - 0.5f) * 2.0f, (AnimChannel *) gJbombAnim, 4, 2);
    }

    f = gJbombPitch;
    if (f <= 0.5f) {
        w = 0.5f - (0.5f - f);
    } else {
        w = 0.5f - (f - 0.5f);
    }
    step = gJbombPitchStep * (w * 2.0f);
    s = gPadStickY;
    if (s > 0) {
        lim = 1.0f - jbomb_pitch_limit();
        if (f <= lim) {
            f += step;
            if (!(f <= lim)) {
                f = lim;
            }
            goto store2;
        }
    } else if (s < 0) {
        lim = jbomb_pitch_limit();
        if (!(f < lim)) {
            f -= step;
            if (f < lim) {
                f = lim;
            }
            goto store2;
        }
    }
    step = gJbombPitchRecentreStep;
    CENTER_STEP(f, step);
store2:
    gJbombPitch = f;
    if (f <= 0.5f) {
        anim_set_frame(f * 2.0f, (AnimChannel *) gJbombAnim, 3, 1);
    } else {
        anim_set_frame((f - 0.5f) * 2.0f, (AnimChannel *) gJbombAnim, 3, 2);
    }
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/7FB50/jbomb_update_tilt.s")
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
/* (1 - |stick y|/80) / 2 (0x802C7BC0).
 * (1 - |gPadStickY| / 80) / 2. The asm returns it in f4 (and changes f6);
 * asm caller jbomb_update_tilt keeps f2 live (a mixed N64 build would need a
 * thunk, the native port doesn't). */
f32 jbomb_pitch_limit(void) {
    s32 v = gPadStickY;

    if (v < 0) {
        v = -v;
    }
    return (1.0f - (f32) v / 80.0f) / 2.0f;
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/7FB50/jbomb_pitch_limit.s")
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
/* 0.5 +- |speed|/270/2, clamped (0x802C7C1C).
 * f = |+0x76 of the vehicle block| / 270 * 0.5, then 0.5 + f if `up`
 * else 0.5 - f, capped at gJbombRollMax (-> gJbombRollMaxClamp) and floored at
 * gJbombRollMin (-> gJbombRollMinClamp). The asm takes `up` in s2 and returns the value
 * in f2; it also leaves s3 = 270 (scratch, not read by its caller
 * jbomb_update_tilt). */
f32 jbomb_roll_limit(s32 up) {
    s32 v = VEH_S16(VS_SPEED);
    f32 f;

    if (v < 0) {
        v = -v;
    }
    f = (f32) v / 270.0f * 0.5f;
    if (up != 0) {
        f = f + 0.5f;
    } else {
        f = 0.5f - f;
    }
    if (!(f <= gJbombRollMax)) {
        f = gJbombRollMaxClamp;
    }
    if (f < gJbombRollMin) {
        f = gJbombRollMinClamp;
    }
    return f;
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/7FB50/jbomb_roll_limit.s")
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
extern u8 * N64P gJbombModel; /* this vehicle's model: +0 / +4 / +8 offsets of the point lists, +0x18 the matrix */

/* J-Bomb's model matrix, parts and points (0x802C7CB0).
 * Place this vehicle's model (vehicle 9): builds its matrix (scale 0x4268,
 * angles (0, +0x4C of the block, 0)) at the model's +0x18 entry inside the
 * current copy (gBufferIndex ? gJbombMtxA : gJbombMtxB) with mtx_build_model,
 * then, except in mode 5 (+0xA1), places the parts (veh_place_parts(x, y, z,
 * 9, model + [4], model + [8], copy)), and always the points
 * (veh_points_transform(9, model + [0], model + [4], copy)).
 * Register convention (conventions.txt): s0 and s1 come in for the
 * mtx_chain_transform chain, whose registers (v1, a0, a3, s1, s2) go out; regs
 * carries them. mtx_build_model leaves a3 = 0 (mtx_mul's loop counter)
 * and s2 = the matrix, which the asm then hands on: modelled explicitly.
 * v1/a0 going into veh_points_transform are y and z (veh_place_parts restores them).
 * The asm reads the block through $gp (= gJbombState, set by every caller)
 * and leaves t0 = 9 and t2 = the point list end. Its cos/sin f12/f14
 * temporaries (survey: read by jbomb_update) are not modelled (see
 * mtx_build_model). Asm caller jbomb_update keeps t6, t7 live (a mixed N64
 * build would need a thunk; the native port won't). */
void jbomb_build_model(MtxChainRegs *regs) {
    u8 *mdl = gJbombModel;
    u8 *base;
    s32 *m;
    s32 x;
    s32 y;
    s32 z;

    m = (s32 *) (*(u32 *) (mdl + *(s32 *) (mdl + 0x18) + 4) +
                 (u32) (gBufferIndex != 0 ? (u8 *) gJbombMtxA : (u8 *) gJbombMtxB));
    gModelRot[0] = 0;
    gModelRot[2] = 0;
    gModelRot[1] = *(u16 *) (gJbombState + 0x4C);
    mtx_build_model(gJbombPos[0], gJbombPos[1], gJbombPos[2], 0x4268, m);
    base = gBufferIndex != 0 ? (u8 *) gJbombMtxA : (u8 *) gJbombMtxB;
    x = gJbombPos[0];
    y = gJbombPos[1];
    z = gJbombPos[2];
    regs->a3 = 0;
    regs->s2 = (s32) m;
    if (VEH_U8(VS_ACTION) != 5) {
        mdl = gJbombModel;
        veh_place_parts(x, y, z, 9, mdl + *(s32 *) (mdl + 4), mdl + *(s32 *) (mdl + 8), base, regs);
    }
    regs->v1 = y;
    regs->a0 = z;
    mdl = gJbombModel;
    veh_points_transform(9, (s16 *) (mdl + *(s32 *) (mdl + 0)), (s16 *) (mdl + *(s32 *) (mdl + 4)), base, regs);
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/7FB50/jbomb_build_model.s")
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
/* J-Bomb's steering rate (0x802C7DFC).
 * Modes 0 and 5 (+0xA1): returns round(+0x76 / 1.5) (cvt.w.s, nearest).
 * Modes 1-4: if |gPadStickX| is large (>= 51 or < -50) gJbombAirSteerRate climbs by
 * 2 up to 100, else it is reset to 20; returns the new gJbombAirSteerRate. Any other
 * mode hits a `syscall` in the asm (a deliberate crash) and, if that
 * returned, would continue as modes 1-4, which is what this does.
 * The asm returns the value in s3; asm caller jbomb_update keeps a0-a3
 * live (a mixed N64 build would need a thunk, the native port doesn't). */
s32 jbomb_steer_rate(void) {
    s32 mode = VEH_U8(VS_ACTION);
    s32 v;
    s32 r;

    if (mode == 0 || mode == 5) {
        CVT_W_S(r, (f32) VEH_S16(VS_SPEED) / 1.5f);
        return r;
    }
    v = gPadStickX;
    if (v >= 0x33 || v < -0x32) {
        r = gJbombAirSteerRate + 2;
        if (r >= 0x65) {
            r = 100;
        }
        gJbombAirSteerRate = r;
        return r;
    }
    gJbombAirSteerRate = 20;
    return 20;
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/7FB50/jbomb_steer_rate.s")
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
/* GGravity = gLevelGravity (x4 while falling) (0x802C7ECC).
 * gGravity = gLevelGravity, times 4 in mode 2. Asm callers keep registers
 * live across the call (jbomb_ride_follow t3, t4, f12, f14; jbomb_update
 * a0-a3, t6, f12, f14): a mixed N64 build would need a thunk. */
void jbomb_set_gravity(void) {
    f32 k;

    if (VEH_U8(VS_ACTION) == 2) {
        k = 4.0f;
    } else {
        k = 1.0f;
    }
    gGravity = gLevelGravity * k;
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/7FB50/jbomb_set_gravity.s")
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
/* Speed bands and throttle step for the mode (0x802C7F28).
 * Sets gBounceThreshold / gBounceDamping to 0xFF and loads the 15 s16 tuning values at
 * +0x78 of the vehicle block plus gJbombThrottleStep for the mode (+0xA1 nonzero or
 * zero). Asm callers keep registers live across the call (jbomb_ride_follow t3,
 * t4, f12, f14; jbomb_update a0-a3): a mixed N64 build would need a thunk. */
void jbomb_set_tuning(void) {
    static const s16 sActive[15] = { -200, 0, 2, 0, 250, 2, 0, 250, 2, 0, 250, 2, 0, 250, 2 };
    static const s16 sIdle[15] = { -100, 0, 4, 0, 150, 4, 0, 150, 4, 0, 150, 4, 0, 150, 4 };
    const s16 *src;
    s32 i;

    gBounceThreshold = 0xFF;
    gBounceDamping = 0xFF;
    if (VEH_U8(VS_ACTION) != 0) {
        src = sActive;
    } else {
        src = sIdle;
    }
    for (i = 0; i < 15; i++) {
        VEH_S16(0x78 + i * 2) = src[i];
    }
    gJbombThrottleStep = (VEH_U8(VS_ACTION) != 0) ? 4 : 12;
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/7FB50/jbomb_set_tuning.s")
#endif

/* jbomb_save_state: two-address trampoline into veh_serialize_state, same
 * confirmed-unreachable-from-C 8-byte sd-$ra frame as veh_check_wrap
 * (hd_code/679E0.c). Permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
extern u8 gJbombState[];  /* this vehicle's state block: 0xA6 bytes are (de)serialized */
extern u32 gJbombPos[]; /* plus these three words */

/* J-Bomb's state into a buffer (0x802C8074).
 * Serialize this vehicle's state (gJbombState[0..0xA5] plus the three words
 * gJbombPos[0..2]) into `dst` via veh_serialize_state. Returns the callee's result
 * (0xB2, the byte count): the asm leaves the callee's v0 untouched. */
s32 jbomb_save_state(u8 *dst) {
    return veh_serialize_state(dst, gJbombState, gJbombPos);
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/7FB50/jbomb_save_state.s")
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
/* J-Bomb's state from a buffer (recording start) (0x802C80A0).
 * Inverse of jbomb_save_state: restore this vehicle's state from `src` via
 * veh_deserialize_state. Called from 17210.c as `void jbomb_load_state(void *)`. */
void jbomb_load_state(void *src) {
    veh_deserialize_state(src, gJbombState, gJbombPos);
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/7FB50/jbomb_load_state.s")
#endif

#ifdef NON_MATCHING
/* The tables of the 800E0 bin (ROM 0x800E0-0x80280, 0x802C48A0-0x802C4A40),
 * just before this file's code: the five particle emitter definitions
 * (ParticleDef + their texture sequences, list gParticleDefs; 43A60.c).
 * The NON_MATCHING build defines it in C and links this file's .data (it has
 * nothing else) back at the table's original address (Makefile NM_PIN_HD_CODE,
 * nm_ldscript.py --pin-object); nm_symaudit.py checks the bytes against the
 * bin. Code reaches it through the absolute D_ symbols. */
typedef struct {
    /* 802C48A0 */ ParticleDef pd802C48A0; /* gPtclDefBreak0 */
    /* 802C48C4 */ s16 sq802C48C4[5];
    /* 802C48CE */ u16 sq802C48C4_pad;
    /* 802C48D0 */ s16 sq802C48D0[3];
    /* 802C48D6 */ u16 sq802C48D0_pad;
    /* 802C48D8 */ s16 sq802C48D8[19];
    /* 802C48FE */ u16 sq802C48D8_pad;
    /* 802C4900 */ ParticleDef pd802C4900; /* gPtclDefBreak1 */
    /* 802C4924 */ s16 sq802C4924[7];
    /* 802C4932 */ u16 sq802C4924_pad;
    /* 802C4934 */ s16 sq802C4934[2];
    /* 802C4938 */ s16 sq802C4938[8];
    /* 802C4948 */ ParticleDef pd802C4948; /* gPtclDefBreak2 */
    /* 802C496C */ s16 sq802C496C[7];
    /* 802C497A */ u16 sq802C496C_pad;
    /* 802C497C */ s16 sq802C497C[2];
    /* 802C4980 */ s16 sq802C4980[8];
    /* 802C4990 */ ParticleDef pd802C4990; /* gPtclDefBreak3 */
    /* 802C49B4 */ s16 sq802C49B4[7];
    /* 802C49C2 */ u16 sq802C49B4_pad;
    /* 802C49C4 */ s16 sq802C49C4[2];
    /* 802C49C8 */ s16 sq802C49C8[8];
    /* 802C49D8 */ ParticleDef pd802C49D8; /* gPtclDefJbombDust */
    /* 802C49FC */ s16 sq802C49FC[6];
    /* 802C4A08 */ s16 sq802C4A08[2];
    /* 802C4A0C */ s16 sq802C4A0C[9];
    /* 802C4A1E */ u16 sq802C4A0C_pad;
    /* 802C4A20 */ ParticleDef * N64P particleDefs[5]; /* gParticleDefs */
    /* 802C4A34 */ s32 particleDefsEnd;
    /* 802C4A38 */ u32 pad802C4A38[2];
} TextData800E0; /* size 0x1A0 */
/* the members must sit at their ROM offsets (with 4-byte pointers) */
typedef char TextData800E0_size_check[(sizeof(void *) != 4 || sizeof(TextData800E0) == 0x1A0) ? 1 : -1];

TextData800E0 textdata_800E0 = {
    { N64_DPTR(textdata_800E0.sq802C48C4), N64_DPTR(textdata_800E0.sq802C48D0), N64_DPTR(textdata_800E0.sq802C48D8), 5, 500, 60, 50, 20, 8, 8, -25, 50, 1, 160, 19, 0xFF, 0xFF, 0xFF, 0xFF, 0, 3 },
    {
        0x0004, 0x07B9, 0x07BA, 0x07BB, 0x07BC,
    },
    0x0000,
    {
        0x0002, 0x07BD, 0x07BC,
    },
    0x0000,
    {
        0x0012, 0x07BD, 0x07BD, 0x07BD, 0x07BE, 0x07BE, 0x07BE, 0x07BF,
        0x07BF, 0x07BF, 0x07C0, 0x07C0, 0x07C0, 0x07C1, 0x07C1, 0x07C1,
        0x07C2, 0x07C2, 0x07C2,
    },
    0x0000,
    { N64_DPTR(textdata_800E0.sq802C4924), N64_DPTR(textdata_800E0.sq802C4934), N64_DPTR(textdata_800E0.sq802C4938), 5, 550, 60, 30, 20, 16, 16, -25, 50, 1, 180, 19, 0xFF, 0xFF, 0xFF, 0xFF, 0, 3 },
    {
        0x0006, 0x07D0, 0x07D0, 0x07D1, 0x07D1, 0x07D2, 0x07D2,
    },
    0x0000,
    {
        0x0001, 0x07D3,
    },
    {
        0x0007, 0x07D4, 0x07D4, 0x07D4, 0x07D4, 0x07D4, 0x07D4, 0x07D4,
    },
    { N64_DPTR(textdata_800E0.sq802C496C), N64_DPTR(textdata_800E0.sq802C497C), N64_DPTR(textdata_800E0.sq802C4980), 5, 400, 60, 30, 20, 16, 16, -25, 60, 1, 160, 19, 0xFF, 0xFF, 0xFF, 0xFF, 0, 3 },
    {
        0x0006, 0x07D0, 0x07D0, 0x07D1, 0x07D1, 0x07D2, 0x07D2,
    },
    0x0000,
    {
        0x0001, 0x07D3,
    },
    {
        0x0007, 0x07D4, 0x07D4, 0x07D4, 0x07D4, 0x07D4, 0x07D4, 0x07D4,
    },
    { N64_DPTR(textdata_800E0.sq802C49B4), N64_DPTR(textdata_800E0.sq802C49C4), N64_DPTR(textdata_800E0.sq802C49C8), 5, 500, 60, 30, 20, 16, 16, -25, 60, 1, 160, 19, 0xFF, 0xFF, 0xFF, 0xFF, 0, 3 },
    {
        0x0006, 0x07D0, 0x07D0, 0x07D1, 0x07D1, 0x07D2, 0x07D2,
    },
    0x0000,
    {
        0x0001, 0x07D3,
    },
    {
        0x0007, 0x07D4, 0x07D4, 0x07D4, 0x07D4, 0x07D4, 0x07D4, 0x07D4,
    },
    { N64_DPTR(textdata_800E0.sq802C49FC), N64_DPTR(textdata_800E0.sq802C4A08), N64_DPTR(textdata_800E0.sq802C4A0C), 5, 500, 60, 30, 20, 16, 16, -25, 50, 1, 160, 19, 0xFF, 0xFF, 0xFF, 0xFF, 0, 3 },
    {
        0x0005, 0x0848, 0x0848, 0x0848, 0x0848, 0x0848,
    },
    {
        0x0001, 0x0849,
    },
    {
        0x0008, 0x084A, 0x084A, 0x084A, 0x084A, 0x084B, 0x084B, 0x084B,
        0x084B,
    },
    0x0000,
    {
        N64_DPTR(&textdata_800E0.pd802C48A0), N64_DPTR(&textdata_800E0.pd802C4900), N64_DPTR(&textdata_800E0.pd802C4948),
        N64_DPTR(&textdata_800E0.pd802C4990), N64_DPTR(&textdata_800E0.pd802C49D8),
    },
    -1,
    { 0x00000000, 0x00000000 },
};
#endif
