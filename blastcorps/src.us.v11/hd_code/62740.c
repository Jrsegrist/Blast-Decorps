#include "common.h"
#include <ultra64.h>
#include "game/game.h"
/* Views: this file reads these shared variables (game/variables.h) as other types. */
#define gVehicles ((u8 *) gVehicles)
#ifdef NON_MATCHING
#define gModelRot (*(s16 *) gModelRot)
#define gSlotGroundY (*(s32 *) gSlotGroundY)
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
/* Port-phase rewrites (functional, not matching). Register conventions of the
 * hand asm are in tools_port/conventions.txt.
 *
 * Vehicle record: much of this file's asm reaches the current vehicle's state
 * through $gp, which the vehicle code points at that vehicle's record
 * (gSideswipeState, gThunderfistState, gSkyfallState, ... - one per vehicle, so it is not a
 * fixed global). The C versions take it as an explicit `u8 *veh` argument
 * (convention `in gp=a0`). Known fields:
 *   0x00 f32, 0x28..0x48 s32 x9, 0x4C u16 heading (12-bit angle),
 *   0x4E u16 target heading, 0x50 u8 surface class, 0x76 s16 signed speed,
 *   0x78 5 x {s16 lo, s16 hi, s16 value} band table, 0x96..0xA5 u8 flags
 *   (0x9F highest surface class, 0xA5 s8 direction). The record is 0xA6 bytes long as far
 *   as veh_snapshot/veh_rollback (save/restore) are concerned. */
#define VEH_U8(v, off) (*(u8 *) ((u8 *) (v) + (off)))
#define VEH_S8(v, off) (*(s8 *) ((u8 *) (v) + (off)))
#define VEH_U16(v, off) (*(u16 *) ((u8 *) (v) + (off)))
#define VEH_S16(v, off) (*(s16 *) ((u8 *) (v) + (off)))
#define VEH_S32(v, off) (*(s32 *) ((u8 *) (v) + (off)))
#define VEH_F32(v, off) (*(f32 *) ((u8 *) (v) + (off)))

extern s32 gBlockedTurnRates[]; /* {key, value, threshold} triples, value 0 ends */
extern u8 gVehSnapshot[];  /* vehicle save buffer: 0x300 + 0xA6 + 3 words */
extern s32 gGroundTriSwap[]; /* 0x14-byte swap temp */
extern u8 * N64P gGroundCellTris;
extern u8 * N64P gGroundCellTrisEnd;
extern u8 * N64P gGroundHitTri;
extern s32 gSlotY;
extern s32 gSlotY1;
extern s32 gSlotY2;
extern s32 gSlotGroundY1;
extern s32 gSlotGroundY2;
extern u8 gSlotStandOn;
extern u8 gSlotStandOn1;
extern u8 gSlotAirborne;
extern u8 gSlotAirborne1;
extern u8 gSlotSurface;
extern u8 gNoWheels;


/* Prototypes shared by the ground-height functions (veh_update_stand_on ..
 * veh_reground, veh_ground_contact); their definitions are further down. */
s32 veh_slot_launch(s32 index, s32 *a, s32 *b, s32 *c, s32 extra, s32 value);
s32 veh_clamp_climb(s32 x);
s32 veh_slot_land(s32 index, s32 *a, s32 *b, s32 *c, s32 extra, s32 pos, s32 s3);
/* (veh_respawn_if_out's Pos802A8CCC: game/regs.h.) */

#define D_803ED394_ ((&gModelRot)[2])

/* Find the gStandOn record keyed `key` (or claim the end marker for it:
 * byte 0 = key, the -1 word otherwise left) and copy the three slot kinds
 * gSlotStandOn[0..2] into its bytes 1..3. The key compare is against the full
 * register (a key above 0xFF never matches); 0xFF bytes that aren't the end
 * marker are skipped. Shared by veh_update_stand_on and veh_init_ground. */
#define PORT_STORE_KINDS(key)                                                       \
    {                                                                               \
        u8 *p_ = gStandOn;                                                        \
        for (;;) {                                                                  \
            if (p_[0] == (key)) {                                                   \
                break;                                                              \
            }                                                                       \
            if (p_[0] == 0xFF && *(s32 *) p_ == -1) {                               \
                p_[0] = (key);                                                      \
                break;                                                              \
            }                                                                       \
            p_ += 4;                                                                \
        }                                                                           \
        p_[1] = (&gSlotStandOn)[0];                                                   \
        p_[2] = (&gSlotStandOn)[1];                                                   \
        p_[3] = (&gSlotStandOn)[2];                                                   \
    }

/* cvt.w.s under the game's FCSR: float -> s32, round to nearest even (a C
 * cast truncates). NaN and out-of-range inputs (degenerate geometry) give
 * 0x7FFFFFFF, the invalid-operation result the emulator produces (the VR4300
 * itself raises an unimplemented-operation exception there). */
static s32 port_cvt_w_s(f32 x) {
    s32 t;
    f32 frac;

    if (!(x >= -2147483648.0f && x < 2147483648.0f)) {
        return 0x7FFFFFFF;
    }
    t = (s32) x;
    frac = x - (f32) t;

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
/* Wide-FOV levels: doubles the speed-band bounds (0x802A6F00).
 * If gWideFovLevel is set, double both bounds of each of the 5 band-table
 * entries at veh+0x78. (The asm saves v0/v1/a0; asm callers keep f12/f14.) */
void veh_widen_bands(u8 *veh) {
    s16 *band;
    s32 i;

    if (gWideFovLevel != 0) {
        band = &VEH_S16(veh, VS_BANDS);
        for (i = 5; i != 0; i--) {
            band[0] = band[0] << 1;
            band[1] = band[1] << 1;
            band += 3;
        }
    }
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/62740/veh_widen_bands.s")
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
/* (gFreeArcStart/gFreeArcEnd are declared at the top of the file.) */

/* The midpoint of the free heading arc (0x802A6F6C).
 * Midpoint between ring indices A and B, going forward from A (wrapping
 * through 0xFFF when B < A). Returns s32 because the asm leaves the full
 * value in v0 (C callers declare s16; values are < 0x1000 for 12-bit input).
 * Register note: the asm saves/restores v1 and a0 and touches nothing but
 * v0/at. Its asm callers rely on that: veh_turn_into_free_arc keeps a1 live, and the
 * vehicle update functions (driver_update, skyfall_update, backlash_update,
 * starski_update, ...) keep a3, t6, f12 and f14 live across the call. This C
 * version is plain o32, so a mixed N64 build would need a thunk preserving
 * those; the native port does not. */
s32 veh_free_arc_mid(void) {
    s32 a = ((u16) gFreeArcStart);
    s32 b = ((u16) gFreeArcEnd);
    s32 mid;

    if (b < a) {
        mid = (((0xFFF - a) + b) >> 1) + a;
        if (mid >= 0x1000) {
            mid -= 0xFFF;
        }
        return mid;
    }
    return (u32) (a + b) >> 1;
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/62740/veh_free_arc_mid.s")
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
/* After a hit, the speed toward +-limit (0x802A6FE4).
 * When gPlayFrames is set: record the speed's direction (veh+0x76 > 0 ? 1 : -1)
 * in veh+0xA5, then move the speed 3 toward zero, but forward speed not below
 * `limit` and reverse speed not above -limit. (The asm computes -limit with a
 * trapping `neg`, so limit = 0x80000000 raises an overflow exception there.)
 * Asm callers keep a0, a3, t6, f12 and f14 live across the call (the asm
 * saves a0/a2/v1). */
void veh_hit_slowdown(u8 *veh, s32 limit) {
    s32 speed;

    if (gPlayFrames != 0) {
        speed = VEH_S16(veh, VS_SPEED);
        VEH_S8(veh, VS_LAST_DIR) = (speed > 0) ? 1 : -1;
        if (speed >= 0) {
            speed -= 3;
            if (!(limit < speed)) {
                speed = limit;
            }
        } else {
            speed += 3;
            if (!(speed < -limit)) {
                speed = -limit;
            }
        }
        VEH_S16(veh, VS_SPEED) = speed;
    }
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/62740/veh_hit_slowdown.s")
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
/* The target heading (half a turn more when reversing) (0x802A7070).
 * Target heading (veh+0x4E) = *angle when the stored direction (veh+0xA5)
 * still matches the speed's sign, else *angle turned half a circle (-0x800,
 * wrapping by +0xFFF). Asm callers keep a0-a3, t6 and f12 live (the asm saves
 * v0/v1). */
void veh_set_target_heading(u8 *veh, s16 *angle) {
    s32 dir = (VEH_S16(veh, VS_SPEED) > 0) ? 1 : -1;
    s32 target;

    if (VEH_S8(veh, VS_LAST_DIR) == dir) {
        target = *angle;
    } else {
        target = *angle - 0x800;
        if (target < 0) {
            target += 0xFFF;
        }
    }
    VEH_U16(veh, VS_MOVE_HEADING) = target;
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/62740/veh_set_target_heading.s")
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
/* Target outside +-90 degrees: flip it, negate the speed (0x802A70D8).
 * If the target heading (veh+0x4E) is outside the +-0x400 window around the
 * heading (veh+0x4C) - tested as written for the wrapped window (lo > hi) -
 * turn the target half a circle, negate the speed and move ring index A back
 * and B forward by 0x800 (mod 0xFFF). Asm callers keep a3, t6, f12 and f14
 * live (the asm saves v0, v1, a0, a1). */
void veh_reverse_if_behind(u8 *veh) {
    s32 heading = VEH_U16(veh, VS_HEADING);
    s32 lo = heading - 0x400;
    s32 hi = heading + 0x400;
    s32 t;

    if (lo < 0) {
        lo += 0xFFF;
    }
    if (hi >= 0x1000) {
        hi -= 0xFFF;
    }
    t = VEH_U16(veh, VS_MOVE_HEADING);
    if (hi < lo) {
        if (!(t < lo && hi < t)) {
            return;
        }
    } else if (!(t < lo || hi < t)) {
        return;
    }
    t += 0x800;
    if (t >= 0x1000) {
        t -= 0xFFF;
    }
    VEH_U16(veh, VS_MOVE_HEADING) = t;
    VEH_S16(veh, VS_SPEED) = -VEH_S16(veh, VS_SPEED);
    t = ((u16) gFreeArcStart) - 0x800;
    if (t < 0) {
        t += 0xFFF;
    }
    gFreeArcStart = t;
    t = ((u16) gFreeArcEnd) + 0x800;
    if (0xFFF < t) {
        t -= 0xFFF;
    }
    gFreeArcEnd = t;
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/62740/veh_reverse_if_behind.s")
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
/* The turn back into the free heading arc (0x802A71DC).
 * Steering toward the ring: lo = A + 0x78 and hi = B - 0x78 (ring indices
 * gFreeArcStart/gFreeArcEnd, wrapped by 0x1000) bound a window. If `target` is in
 * the "stop" region of that window (tested as the asm does for the wrapped
 * and unwrapped cases), the new index is `target` and the turn is 0.
 * Otherwise, if `cur` is outside the window, pull it to the nearer end (A +
 * 0x78 or B - 0x78, wrapped by 0xFFF) and, if that lands outside A..B, use
 * the ring midpoint (veh_free_arc_mid). The turn is round(scale * |speed|)
 * (veh+0x76), signed toward `target` the short way round (|cur - target| <=
 * 0x800 decides).
 * Register convention: gp, cur in a0, target in a1, scale in f0; outputs the
 * new index in a0 (*curOut) and the turn in a1 (return value). The asm saves
 * v0, v1, a2, a3 and t0-t2 and keeps f0 live across veh_free_arc_mid; asm
 * callers keep a3, t6, f12 and f14 live (a mixed N64 build would need a
 * thunk). */
s32 veh_turn_into_free_arc(u8 *veh, s32 cur, s32 target, s32 *curOut, f32 scale) {
    s32 a = ((u16) gFreeArcStart);
    s32 b = ((u16) gFreeArcEnd);
    s32 lo;
    s32 hi;
    s32 da;
    s32 db;
    s32 speed;
    s32 turn;
    s32 d;
    s32 pull;

    if (b < a) {
        lo = a + 0x78;
        if (lo >= 0x1000) {
            lo -= 0x1000;
        }
        hi = b - 0x78;
        if (hi < 0) {
            hi += 0x1000;
        }
        if (hi < lo && !(target < lo && hi < target)) {
            *curOut = target;
            return 0;
        }
        pull = !(hi < lo) || (cur < lo && hi < cur);
    } else {
        lo = a + 0x78;
        hi = b - 0x78;
        if (lo < hi && !(target < lo) && !(hi < target)) {
            *curOut = target;
            return 0;
        }
        pull = !(lo < hi) || cur < lo || hi < cur;
    }
    if (pull) {
        da = a - cur;
        if (da < 0) {
            da = -da;
        }
        if (da > 0x800) {
            da = 0xFFF - da;
        }
        db = b - cur;
        if (db < 0) {
            db = -db;
        }
        if (db > 0x800) {
            db = 0xFFF - db;
        }
        if (db < da) {
            cur = b - 0x78;
            if (cur < 0) {
                cur += 0xFFF;
            }
        } else {
            cur = a + 0x78;
            if (cur >= 0x1000) {
                cur -= 0xFFF;
            }
        }
        if (b < a) {
            if (cur < a && b < cur) {
                cur = veh_free_arc_mid();
            }
        } else if (cur < a || b < cur) {
            cur = veh_free_arc_mid();
        }
    }
    speed = VEH_S16(veh, VS_SPEED);
    if (speed < 0) {
        speed = -speed;
    }
    turn = port_cvt_w_s(scale * (f32) speed);
    *curOut = cur;
    d = cur - target;
    if (cur < target) {
        if (d < 0) {
            d = -d;
        }
        return (d <= 0x800) ? -turn : turn;
    }
    return (d <= 0x800) ? turn : -turn;
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/62740/veh_turn_into_free_arc.s")
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
/* The heading toward the target by delta (0x802A746C).
 * Turn the heading (veh+0x4C) by `delta` toward the target (veh+0x4E),
 * wrapping mod 0xFFF, and snap to the target if the step passes it.
 * Register convention: the asm returns the stepped heading in v1 (left as the
 * caller's v1 when heading == target) and the target in a0; the C takes the
 * caller's v1 as `v1`, returns v1 and stores the target through `targetOut`.
 * (The asm also leaves the old heading in v0.) Asm callers keep a3, t6, f12
 * and f14 live. */
s32 veh_step_heading(u8 *veh, s32 delta, s32 v1, s32 *targetOut) {
    s32 heading = VEH_U16(veh, VS_HEADING);
    s32 target = VEH_U16(veh, VS_MOVE_HEADING);
    s32 next;
    s32 snap;

    *targetOut = target;
    if (heading == target) {
        return v1;
    }
    next = heading + delta;
    if (next < 0) {
        next += 0xFFF;
    }
    if (next >= 0x1000) {
        next -= 0xFFF;
    }
    if (delta >= 0) {
        if (heading < target) {
            snap = (target < next) || (next < heading);
        } else {
            snap = (target < heading) && (next < heading) && (target < next);
        }
    } else {
        if (target < heading) {
            snap = !(target < next) || !(next < heading);
        } else {
            snap = (heading < target) && (next < target) && (heading < next);
        }
    }
    VEH_U16(veh, VS_HEADING) = snap ? target : next;
    return next;
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/62740/veh_step_heading.s")
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
/* Clears a vehicle state block's motion (0x802A754C).
 * Reset a vehicle record: clear the f32 at +0, the words at +0x28..+0x48, the
 * speed and most flag bytes; set +0x50 and +0xA0 to 1. Asm callers keep a1,
 * a3, t0, t1, t6, t7, f12 and f14 live (the asm saves t1; it zeroes f0). */
void veh_reset_state(u8 *veh) {
    VEH_U8(veh, VS_SLOT_AIRBORNE) = 0;
    VEH_U8(veh, 0x97) = 0;
    VEH_U8(veh, 0x98) = 0;
    VEH_U8(veh, VS_SURFACE) = SURFACE_NORMAL;
    VEH_U8(veh, VS_PUSH_DIVISOR) = 1;
    VEH_S32(veh, VS_SLOT_LAUNCH_VEL) = 0;
    VEH_S32(veh, 0x2C) = 0;
    VEH_S32(veh, 0x30) = 0;
    VEH_S32(veh, VS_SLOT_LAUNCH_Y) = 0;
    VEH_S32(veh, 0x38) = 0;
    VEH_S32(veh, 0x3C) = 0;
    VEH_S32(veh, VS_SLOT_AIR_TIME) = 0;
    VEH_S32(veh, 0x44) = 0;
    VEH_S32(veh, 0x48) = 0;
    VEH_S16(veh, VS_SPEED) = 0;
    VEH_U8(veh, VS_SKIDDING) = 0;
    VEH_U8(veh, VS_ON_MOVING_PART) = 0;
    VEH_U8(veh, VS_HIT_PART_DOWN) = 0;
    VEH_U8(veh, VS_HIT_OBJECT) = 0;
    VEH_U8(veh, VS_HIT_IMMUNE_PART) = 0;
    VEH_U8(veh, VS_ACTION) = 0;
    VEH_U8(veh, VS_ACTION_AUX) = 0;
    VEH_U8(veh, VS_MOVING) = 0;
    VEH_U8(veh, 0xA4) = 0;
    VEH_U8(veh, VS_LAST_DIR) = 0;
    VEH_F32(veh, 0x00) = 0.0f;
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/62740/veh_reset_state.s")
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
/* The state, channels and position before the collisions (0x802A75DC).
 * Save a vehicle: copy 0x300 bytes from `src` and the 0xA6-byte record into
 * gVehSnapshot, followed by the words *w0, *w1, *w2 (unaligned, at +0x3A6).
 * Register convention: asm inputs gp, v0, v1, a0, a1 (see conventions.txt);
 * the asm restores every register it touches. Asm callers keep a0-a3, t6,
 * t7, f12 and f14 live. */
void veh_snapshot(u8 *veh, u8 *src, s32 *w0, s32 *w1, s32 *w2) {
    u8 *dst = gVehSnapshot;
    s32 i;
    s32 w;

    for (i = 0x300; i != 0; i--) {
        *dst++ = *src++;
    }
    for (i = 0xA6; i != 0; i--) {
        *dst++ = *veh++;
    }
    w = *w0;
    dst[0] = w >> 24;
    dst[1] = w >> 16;
    dst[2] = w >> 8;
    dst[3] = w;
    w = *w1;
    dst[4] = w >> 24;
    dst[5] = w >> 16;
    dst[6] = w >> 8;
    dst[7] = w;
    w = *w2;
    dst[8] = w >> 24;
    dst[9] = w >> 16;
    dst[10] = w >> 8;
    dst[11] = w;
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/62740/veh_snapshot.s")
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
/* Restores veh_snapshot after a collision (the crash bounce) (0x802A768C).
 * Restore what veh_snapshot saved: 0x300 bytes from gVehSnapshot to `dst`, the
 * 0xA6-byte record to `veh`, then copy `size` bytes (8-byte units, size a
 * multiple of 8: the asm loops on size != 0) from src to dst2, then the three
 * saved words to *w0, *w1, *w2.
 * Register convention: asm inputs gp, v0, v1, a0, a1, a2 (src), a3 (dst2),
 * t0 (size); the asm returns the advanced src/dst2 in a2/a3 - here the return
 * value and *dst2End. Asm callers keep a0, a1, f12 and f14 live. */
u64 *veh_rollback(u8 *veh, u8 *dst, s32 *w0, s32 *w1, s32 *w2, u64 *src, u64 *dst2, s32 size,
                   u64 **dst2End) {
    u8 *buf = gVehSnapshot;
    u64 *s = src; /* locals, so the stack-passed parameters stay untouched */
    u64 *d = dst2;
    s32 n = size;
    s32 i;

    for (i = 0x300; i != 0; i--) {
        *dst++ = *buf++;
    }
    for (i = 0xA6; i != 0; i--) {
        *veh++ = *buf++;
    }
    while (n != 0) {
        n -= 8;
        *d++ = *s++;
    }
    *w0 = (buf[0] << 24) | (buf[1] << 16) | (buf[2] << 8) | buf[3];
    *w1 = (buf[4] << 24) | (buf[5] << 16) | (buf[6] << 8) | buf[7];
    *w2 = (buf[8] << 24) | (buf[9] << 16) | (buf[10] << 8) | buf[11];
    *dst2End = d;
    return s;
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/62740/veh_rollback.s")
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING

/* Copies the drawn matrix buffer to the other one (0x802A7764).
 * Copy `size` bytes in 8-byte units from a to b, or from b to a when
 * gBufferIndex (the frame buffer index) is nonzero. Both pointers must be
 * 8-aligned and size a multiple of 8 (the asm loops on `size != 0`).
 * Register note: the asm saves/restores a0-a3, and its asm callers
 * (sideswipe_leave, jbomb_spawn, cyclone_spawn, ...) keep a0-a3, f12 and f14
 * live across the call; a mixed N64 build would need a thunk, the native
 * port does not. */
void veh_sync_buffers(u64 *a, u64 *b, s32 size) {
    u64 *src = a;
    u64 *dst = b;

    if (gBufferIndex != 0) {
        src = b;
        dst = a;
    }
    while (size != 0) {
        *dst++ = *src++;
        size -= 8;
    }
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/62740/veh_sync_buffers.s")
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
/* No throttle: speed toward 0 by 8 (0x802A77D0).
 * With no steering input (gPadStickY == 0), brake the speed (veh+0x76) by 8
 * toward zero, stopping at zero. Asm callers keep a0-a3 and t6 live (the asm
 * saves s7). */
void veh_coast(u8 *veh) {
    s32 speed;

    if (gPadStickY == 0) {
        speed = VEH_S16(veh, VS_SPEED);
        if (speed >= 0) {
            speed -= 8;
            if (speed < 0) {
                speed = 0;
            }
        } else {
            speed += 8;
            if (speed > 0) {
                speed = 0;
            }
        }
        VEH_S16(veh, VS_SPEED) = speed;
    }
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/62740/veh_coast.s")
#endif

/* veh_steer_throttle: two sequential no-arg calls (`veh_steer();
 * veh_throttle();`) using the 8-byte addiu-sp/sd-ra/ld-ra frame instead
 * of the 24-byte o32 shadow frame plain C produces for any real call -
 * same confirmed-unreachable-from-C frame family as veh_check_wrap in
 * hd_code/679E0.c (see that file's comment for the probe evidence).
 * Permanently GLOBAL_ASM. */
#ifdef NON_MATCHING

/* Veh_steer, then veh_throttle (0x802A7834).
 * Steering then throttle for one vehicle: veh_steer(rate, angle), then
 * veh_throttle(veh, speed, mode, flags, bands, delta).
 * Register convention: rate s3, angle s4 (veh_steer's), veh gp, speed t6,
 * mode t7, flags s0, bands s1, delta s2 (veh_throttle's); s1 and s3 are
 * clobbered by the callees. Asm callers (driver_update, sideswipe_update,
 * backlash_update) keep t6 live across the call (a mixed N64 build would need
 * a thunk; the native port doesn't). */
void veh_steer_throttle(s32 rate, u16 *angle, u8 *veh, s16 *speed, s32 mode, u8 *flags, s16 *bands, s32 delta) {
    veh_steer(rate, angle);
    veh_throttle(veh, speed, mode, flags, bands, delta);
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/62740/veh_steer_throttle.s")
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
s32 veh_past_reverse_limit(s32 x, s16 *p);
s32 veh_past_forward_limit(s32 x, s16 *p);
s32 veh_band_accel(u8 *veh, s32 x, s16 *band);
s32 veh_drive_traction(s32 mode, u8 *f);

/* The throttle, brake and reverse step of the speed (0x802A785C).
 * Throttle/brake step for the speed *speed (s16). With gHeldZ set: if
 * gLastSpeed is nonzero, brake by `delta` toward 0 (by the speed's sign).
 * Otherwise: with gVehSlowed set (and levelno != 0x22, gCurVehicle not
 * 0xB/0x11/0x12) clamp the speed to half the top band bound (bands[13]) or,
 * in reverse, half bands[0]; then with steer input gPadStickY > 0 accelerate
 * (or brake by `delta` when rolling backward) and with < 0 the mirror image:
 * the step is the band value veh_band_accel finds times
 * veh_drive_traction(mode, flags), or -6 going forward with no band; nothing when
 * the speed is already past the limit (veh_past_forward_limit / veh_past_reverse_limit). The
 * result is copied to gLastSpeed.
 * Register convention: gp, speed in t6, mode in t7, flags in s0, bands in s1
 * (clobbered by veh_band_accel), delta in s2. Asm callers keep a0-a3 and t6
 * live (a mixed N64 build would need a thunk). */
void veh_throttle(u8 *veh, s16 *speed, s32 mode, u8 *flags, s16 *bands, s32 delta) {
    s32 s;
    s32 lim;
    s32 mult;
    s32 r;

    if (gHeldZ != 0) {
        if (gLastSpeed != 0) {
            s = *speed;
            if (s > 0) {
                s -= delta;
                if (s <= 0) {
                    s = 0;
                }
            } else {
                s += delta;
                if (s >= 0) {
                    s = 0;
                }
            }
            *speed = s;
        }
    } else {
        if (((s8) gVehSlowed) != 0 && levelno != 0x22 && gCurVehicle != VEH_BARGE_0B && gCurVehicle != VEH_BARGE_11 &&
            gCurVehicle != VEH_BARGE_12) {
            s = *speed;
            if (s >= 0) {
                lim = bands[13] >> 1;
                if (lim < s) {
                    *speed = lim;
                }
            } else {
                lim = bands[0] >> 1;
                if (s < lim) {
                    *speed = lim;
                }
            }
        }
        mult = veh_drive_traction(mode, flags);
        if (gPadStickY != 0) {
            s = *speed;
            if (gPadStickY > 0) {
                if (s < 0) {
                    *speed = s + delta;
                } else if (veh_past_forward_limit(s, bands) == 0) {
                    r = veh_band_accel(veh, s, bands);
                    if (r != 0) {
                        *speed = s + r * mult;
                    } else {
                        *speed = s - 6;
                    }
                }
            } else {
                if (s > 0) {
                    *speed = s - delta;
                } else if (veh_past_reverse_limit(s, bands) == 0) {
                    r = veh_band_accel(veh, s, bands);
                    *speed = s - r * mult;
                }
            }
        }
    }
    gLastSpeed = *speed;
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/62740/veh_throttle.s")
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
/* Speed beyond the stick-scaled reverse limit (0x802A7A1C).
 * 1 if x < (p[0] * steer) / -80 (steer = gPadStickY), else 0.
 * Register convention: x in t2, p in s1, result in t4 (asm saves v0, v1, a0).
 * Asm callers keep t2, t3 and t6 live. */
s32 veh_past_reverse_limit(s32 x, s16 *p) {
    return x < (p[0] * gPadStickY) / -80;
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/62740/veh_past_reverse_limit.s")
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
/* Speed beyond the stick-scaled forward limit (0x802A7AAC).
 * 1 if (p[13] * steer) / 80 < x (p[13] is the halfword at +0x1A), else 0.
 * Register convention: x in t2, p in s1, result in t4 (asm saves v0, v1, a0).
 * Asm callers keep t2, t3 and t6 live. */
s32 veh_past_forward_limit(s32 x, s16 *p) {
    return (p[13] * gPadStickY) / 80 < x;
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/62740/veh_past_forward_limit.s")
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
/* An unused variant: L/R accelerate (0x802A7B3C).
 * Variant of veh_throttle's speed step (no callers found): with gHeldZ
 * set and gLastSpeed nonzero, brake *speed by `delta` toward 0 by the sign of
 * gLastSpeed; otherwise, if gHeldL or gHeldR is set, accelerate: a
 * negative speed by `delta`, else by 4 * the band value veh_band_accel finds
 * (or -6 with none). The result is copied to gLastSpeed.
 * Register convention: gp, speed in t6, bands in s1 (clobbered by
 * veh_band_accel), delta in s2. */
void veh_throttle_lr(u8 *veh, s16 *speed, s16 *bands, s32 delta) {
    s32 s;
    s32 d;
    s32 r;

    if (gHeldZ != 0) {
        d = gLastSpeed;
        if (d != 0) {
            s = *speed;
            if (d > 0) {
                s -= delta;
                if (s <= 0) {
                    s = 0;
                }
            } else {
                s += delta;
                if (s >= 0) {
                    s = 0;
                }
            }
            *speed = s;
        }
    } else if (gHeldL != 0 || gHeldR != 0) {
        s = *speed;
        if (s < 0) {
            *speed = s + delta;
        } else {
            r = veh_band_accel(veh, s, bands);
            if (r != 0) {
                *speed = s + r * 4;
            } else {
                *speed = s - 6;
            }
        }
    }
    gLastSpeed = *speed;
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/62740/veh_throttle_lr.s")
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
/* The acceleration of the speed band holding x (0x802A7C28).
 * Search n {s16 lo, s16 hi, s16 value} bands (n = 5 when veh+0x50 == 1, else
 * 7 - veh+0x50; 2 fewer when gVehSlowed is set) for lo <= x <= hi and return
 * that band's value, or 0. n must come out >= 0 (the asm loops on n != 0).
 * Register convention: gp, x in t2, bands in s1 (advanced: s1 is clobbered),
 * result in t4. Asm callers keep t2, t3 and t6 live (the asm saves t1). */
s32 veh_band_accel(u8 *veh, s32 x, s16 *band) {
    s32 n = VEH_U8(veh, VS_SURFACE);

    if (n == SURFACE_NORMAL) {
        n = 5;
    } else {
        n = 7 - n;
    }
    if (((s8) gVehSlowed) != 0) {
        n -= 2;
    }
    while (n != 0) {
        n--;
        band += 3;
        if (x < band[-3]) {
            continue;
        }
        if (band[-2] < x) {
            continue;
        }
        return band[-1];
    }
    return 0;
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/62740/veh_band_accel.s")
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
/* Speed within range of the top band bound (0x802A7CB0).
 * 1 if the speed (veh+0x76) is within `range` of a band bound: going forward,
 * the hi bound of band (veh+0x50 == 1 ? 4 : 6 - veh+0x50); in reverse, the lo
 * bound of band 0. Register convention: gp, range in v1, result in v0 (the asm
 * saves t1, t2, t4). Asm callers keep a1-a3, f12 and f14 live. */
s32 veh_near_top_speed(u8 *veh, s32 range) {
    s32 speed = VEH_S16(veh, VS_SPEED);
    s32 i;
    s32 d;

    if (speed >= 0) {
        i = VEH_U8(veh, VS_SURFACE);
        if (i == SURFACE_NORMAL) {
            i = 4;
        } else {
            i = 6 - i;
        }
        d = speed - *(s16 *) (veh + 0x78 + i * 6 + 2);
    } else {
        d = speed - VEH_S16(veh, VS_BANDS);
    }
    if (d < 0) {
        d = -d;
    }
    return d < range;
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/62740/veh_near_top_speed.s")
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
/* The drive factor from the wheels' airborne flags (0x802A7D68).
 * Classify the three flag bytes f[0..2] by `mode`:
 *   0: f[0], f[1] both set -> 0, one set -> 2, neither -> 4
 *   1: f[2] set -> 0, else 4
 *   2: (f[0] == 0) + (f[1] == 0) + 2 * (f[2] == 0)
 *   4: 4
 *   other: 4 if any of f[0..2] is 0, else 0
 * Register convention: mode in t7, f in s0, result in t3 (the asm saves t1 and
 * s0; it clobbers t2 in the default case). Asm callers keep t6 live. */
s32 veh_drive_traction(s32 mode, u8 *f) {
    s32 n;
    s32 i;

    switch (mode) {
        case 0:
            if (f[0] != 0) {
                return (f[1] != 0) ? 0 : 2;
            }
            return (f[1] != 0) ? 2 : 4;
        case 1:
            return (f[2] != 0) ? 0 : 4;
        case 2:
            n = 0;
            if (f[0] == 0) {
                n++;
            }
            if (f[1] == 0) {
                n++;
            }
            if (f[2] == 0) {
                n += 2;
            }
            return n;
        case 4:
            return 4;
        default:
            for (i = 0; i < 3; i++) {
                if (f[i] == 0) {
                    return 4;
                }
            }
            return 0;
    }
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/62740/veh_drive_traction.s")
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
/* Stick-x steering of the heading (0x802A7E70).
 * Steering: turn rate = s3 (minus a third when gWideFovLevel; forced to 100/200
 * when gSteerTurnAround, by gSteerAirborne), saved to gSteerRate; then turn *angle by
 * |steer| * rate / 80 against the sign of steer (gPadStickX), wrapped to
 * 0..0xFFF. When gSteerHeadingSnap is set and gVehJustSwitched isn't, *angle is overridden
 * by gSteerSnapAngle. The result also goes to gSteerHeading.
 * Register convention: rate in s3 (clobbered), angle in s4 (the asm saves s5;
 * it clobbers t0, t2, t4). Asm callers keep a0-a3, t6 and t7 live. */
void veh_steer(s32 rate, u16 *angle) {
    s32 steer;
    s32 a;
    s32 mag;

    if (gWideFovLevel != 0) {
        rate -= rate / 3;
    }
    if (gSteerTurnAround != 0) {
        rate = (gSteerAirborne != 0) ? 100 : 200;
    }
    gSteerRate = rate;
    steer = gPadStickX;
    a = *angle;
    mag = (steer < 0) ? -steer : steer;
    rate = (mag * rate) / 80;
    if (steer != 0) {
        if (steer < 0) {
            a += rate;
        } else {
            a -= rate;
        }
        if (a < 0) {
            a += 0x1000;
        }
        if (a >= 0x1000) {
            a -= 0x1000;
        }
        *angle = a;
    }
    if (gSteerHeadingSnap != 0 && gVehJustSwitched == 0) {
        a = gSteerSnapAngle;
    }
    *angle = a;
    gSteerHeading = a;
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/62740/veh_steer.s")
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
s32 veh_blocked_turn_rate(s32 cur);
extern u8 gSkidSoundTimer;  /* sound retrigger counter */
extern s32 gSkidPitch; /* f32 bits: pitch */
extern f32 gSkidPitchPerSpeed;

/* Travel heading toward the facing; skid flag and sound (0x802A7FD8).
 * Turn the heading *heading toward *target (12-bit angles, wrapping through
 * 0x1000, snapping to the target when the step passes it). The step is
 * |veh_blocked_turn_rate(rate) * veh+0x50 / *speed| (no division when the speed is
 * 0). Unless *flag is set, a heading within 0x156 of the target just copies
 * the target to *out and stops; otherwise *flag = 1. With `sound` set, every
 * 7th call sets pitch gSkidPitch = 0.5 + |speed| * gSkidPitchPerSpeed and plays it
 * (snd_play / snd_modify). Finally *out = heading, and *flag = 0 once
 * the target is reached.
 * Register convention: gp, heading t1, rate t5, speed t6, target s4, out s5,
 * flag s6, sound s7 (the asm saves t6 and, around the sound calls, every
 * register). Asm callers keep a0-a3 and t6 live. (0x80000000 / -1 traps in
 * the asm's divide.) */
void veh_turn_heading(u8 *veh, u16 *heading, s32 rate, s16 *speedp, u16 *target, u16 *out, s8 *flag,
                   s32 sound) {
    s32 speed;
    s32 h;
    s32 t;
    s32 d;
    s32 n;
    f32 pitch;

    rate = veh_blocked_turn_rate(rate);
    speed = *speedp;
    h = *heading;
    t = *target;
    if (speed != 0) {
        rate = (s32) (rate * VEH_U8(veh, VS_SURFACE)) / speed;
    }
    d = h - t;
    if (rate < 0) {
        rate = -rate;
    }
    if (d < 0) {
        d = -d;
    }
    if (d > 0x800) {
        if (t < h) {
            h += rate;
            if (h >= 0x1000) {
                h -= 0x1000;
                if (t < h) {
                    h = t;
                }
            }
        } else {
            h -= rate;
            if (h < 0) {
                h += 0x1000;
                if (h < t) {
                    h = t;
                }
            }
        }
    } else if (h < t) {
        h += rate;
        if (h >= 0x1000 || t < h) {
            h = t;
        }
    } else {
        h -= rate;
        if (h < 0 || h < t) {
            h = t;
        }
    }
    *heading = h;
    if (*flag == 0) {
        d = h - t;
        if (d < 0) {
            d = -d;
        }
        if (d > 0x800) {
            d = 0x1000 - d;
        }
        if (d < 0x156) {
            *out = t;
            return;
        }
        *flag = 1;
    }
    if (sound != 0) {
        n = gSkidSoundTimer + 1;
        if (n >= 7) {
            if (speed < 0) {
                speed = -speed;
            }
            gSkidSoundTimer = 0;
            pitch = 0.5f + (f32) speed * gSkidPitchPerSpeed;
            gSkidPitch = *(s32 *) &pitch;
            snd_modify(snd_play(gSfxBank, SFX_SKID, NULL), 0x10, gSkidPitch);
            n = 0;
        }
        gSkidSoundTimer = n;
    }
    *out = h;
    if (h == t) {
        *flag = 0;
    }
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/62740/veh_turn_heading.s")
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
/* For 10 frames after an arc clip (gBlockedFrame), returns the first gBlockedTurnRates entry for this level whose
 * window is still open; otherwise the caller's rate (0x802A8314).
 * Unless gBlockedFrame + 10 < gUnpausedFrames, walk the {key, value, threshold}
 * table gBlockedTurnRates (ended by value 0) for the first entry with key ==
 * levelno and threshold + gBlockedFrame >= gUnpausedFrames, and return its value;
 * otherwise return `cur` unchanged.
 * Register convention: the asm returns the value in t5 and leaves t5 alone
 * otherwise, so the C takes the caller's t5 as `cur` (the asm saves every
 * other register it uses). Asm callers keep t1 and t6 live. */
s32 veh_blocked_turn_rate(s32 cur) {
    s32 base = gBlockedFrame;
    s32 limit = gUnpausedFrames;
    s32 *e;

    if (base + 10 < limit) {
        return cur;
    }
    for (e = gBlockedTurnRates; e[1] != 0; e += 3) {
        if (e[0] != levelno) {
            continue;
        }
        if (e[2] + base < limit) {
            continue;
        }
        return e[1];
    }
    return cur;
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/62740/veh_blocked_turn_rate.s")
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
/* (front - back height) / wheelbase (0x802A83B8).
 * Unless one of the flag bytes f[1], f[2], f[0] is 1 or *div is 0, set
 * *out = (f32) (p[0] - p[1]) / *div; return *out either way.
 * Register convention: div in t6, f in s0, p in s7, out in t8, result in f12
 * (the asm saves s7 and clobbers t0, t1, t3). Asm callers keep a0-a3 and t6
 * live. Note the asm computes p[0] - p[1] with a trapping `sub` even when
 * *div is 0. */
f32 veh_slope(s16 *div, u8 *f, s32 *p, f32 *out) {
    s32 d;
    f32 r;

    if (f[1] == 1 || f[2] == 1 || f[0] == 1) {
        return *out;
    }
    d = *div;
    if (d == 0) {
        return *out;
    }
    r = (f32) (p[0] - p[1]) / (f32) d;
    *out = r;
    return r;
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/62740/veh_slope.s")
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
s32 veh_slope_drop(s32 *p);

/* Slope pull and rolling drag on the speed (0x802A843C).
 * Slope/drag step for *speed. Nothing if gHeldZ is set and the speed is
 * 0. With `clamp`, cap the speed at 1.5 * veh+0x92 (forward) or 1.5 *
 * veh+0x78 (reverse). Then, unless a flag byte f[0..2] is 1, add
 * round(veh_slope_drop(p) / div * -4) and drag the result toward 0 by 3 (kind
 * 0, 2, 9, 0x10) or 1, stopping at 0.
 * Register convention: gp, speed t6, kind t7, f s0, p s7, clamp t8 (clobbered),
 * div f2 (clobbered). Asm callers keep a0-a3, t6 and f12 live (a mixed N64
 * build would need a thunk). div must be nonzero (else the cvt.w.s faults). */
void veh_apply_slope(u8 *veh, s16 *speed, s32 kind, s8 *f, s32 *p, s32 clamp, f32 div) {
    s32 s;
    s32 lim;
    s32 step;

    if (gHeldZ != 0 && *speed == 0) {
        return;
    }
    if (clamp != 0) {
        s = *speed;
        if (s >= 0) {
            lim = VEH_S16(veh, 0x92);
            lim += lim >> 1;
            if (lim < s) {
                *speed = lim;
            }
        } else {
            lim = VEH_S16(veh, VS_BANDS);
            lim += lim >> 1;
            if (s < lim) {
                *speed = lim;
            }
        }
    }
    if (f[0] == 1 || f[1] == 1 || f[2] == 1) {
        return;
    }
    s = veh_slope_drop(p);
    s = *speed + port_cvt_w_s((f32) s / div * -4.0f);
    if (kind == VEH_DRIVER || kind == VEH_THUNDERFIST || kind == VEH_CYCLONE || kind == VEH_JBOMB) {
        step = 3;
    } else {
        step = 1;
    }
    if (s != 0) {
        if (s > 0) {
            s -= step;
            if (s < 0) {
                s = 0;
            }
        } else {
            s += step;
            if (s > 0) {
                s = 0;
            }
        }
    }
    *speed = s;
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/62740/veh_apply_slope.s")
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
/* The smaller of two wheel-height differences (0x802A8590).
 * Of p[0] - p[6] and p[3] - p[6], return the one with the smaller magnitude
 * (the first on a tie). Register convention: p in s7, result in t1 (the asm
 * saves t3-t6 and clobbers t2). Asm callers keep t6, t7 and f2 live. */
s32 veh_slope_drop(s32 *p) {
    s32 a = p[0] - p[6];
    s32 b = p[3] - p[6];
    s32 absA = (a < 0) ? -a : a;
    s32 absB = (b < 0) ? -b : b;

    return (absB < absA) ? b : a;
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/62740/veh_slope_drop.s")
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING


/* New x/z: a speed-scaled step along the heading (0x802A860C).
 * Offset a point by a scaled length along a heading: n = *len (s16) scaled
 * by |f| (n * (1 - |f| / 2) when |f| < 1 (or NaN), else n / (2|f|), rounded
 * to nearest; n = 0 stays 0). With a = angle % 0x400 (C remainder),
 * s = n * sin(a) >> 16 and c = n * cos(a) >> 16 (32-bit products), the
 * quadrant of `angle` (< 0x400, < 0x800, < 0xC00, else) gives (x, z) =
 * (*px + s, *pz + c), (*px + c, *pz - s), (*px - s, *pz - c) or
 * (*px - c, *pz + s). Returns x (t0); z, *px and the cosine go to out.
 * Register convention: f in f12, angle t4, len t6, px t7, pz s1; outputs t0,
 * t1, s3, fp (conventions.txt). Asm callers keep t7 (and some t6) live; the
 * survey also lists f12/f14 as read afterwards (math_cos's leftovers),
 * which a C version can't hand back (a mixed N64 build would need a thunk;
 * the native port won't). The add/sub trap on overflow (game range). */
s32 veh_move_along_heading(f32 f, s32 angle, s16 *len, s32 *px, s32 *pz, Out802A860C *out) {
    union {
        f32 f;
        u32 u;
    } a;
    s32 n = *len;
    s32 r;
    s32 s;
    s32 c;
    s32 x;
    s32 z;

    a.f = f;
    a.u &= 0x7FFFFFFF;
    if (n != 0) {
        if (!(a.f >= 1.0f)) {
            n = port_cvt_w_s((1.0f - a.f / 2.0f) * (f32) n);
        } else {
            n = port_cvt_w_s((f32) n / (a.f * 2.0f));
        }
    }
    r = angle % 0x400;
    s = (s32) ((u32) n * (u32) math_sin(r)) >> 16;
    out->fp = math_cos(r);
    c = (s32) ((u32) n * (u32) out->fp) >> 16;
    x = *px;
    z = *pz;
    out->s3 = x;
    if (angle < 0x400) {
        x += s;
        z += c;
    } else if (angle < 0x800) {
        x += c;
        z -= s;
    } else if (angle < 0xC00) {
        x -= s;
        z -= c;
    } else {
        x -= c;
        z += s;
    }
    out->t1 = z;
    return x;
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/62740/veh_move_along_heading.s")
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
extern s16 gVehTiltDivB;
extern s16 gVehTiltDivA;
extern s16 gVehContactAngle;
extern u8 gVehSlot0Flags;
void veh_respawn_if_out(u8 *veh, s32 id, s32 *px, s32 *py, s32 *pz, Pos802A8CCC *pos);
void veh_check_no_wheels(u16 *p);
void veh_diamond_sands_damping(u8 *veh);
void veh_rider_gravity(void);
void veh_copy_slot0(u8 *veh);
void veh_ground_surface(u8 *veh, u8 *f, s32 kind);
s32 veh_ground_tilt(s32 *a3Out);
s32 veh_slot_grounded(s32 index, s32 *a, s32 *b, s32 *c, s16 *tbl, s32 x, s32 z, s16 *angle, s32 *ys, s32 key,
                  u8 *veh, s32 fpIn, TriSideOut *f);
void veh_slot_airborne(s32 index, s32 *a, s32 *b, s32 *c, s16 *tbl, s32 x, s32 z, s16 *angle, s32 *ys, s32 key,
                   u8 *veh, s32 fpIn, s32 *s3, TriSideOut *f);


/* (s32) (|d| << 16) / div (signed 32-bit divide: traps on div 0 and on
 * 0x80000000 / -1), through math_atan, >> 4 (logical); negated as asked. */
#define TILT_8768(d, div, negIfPos)                                                    \
    ((d) >= 0 ? ((negIfPos) ? -((u32) math_atan((s32) ((u32) (d) << 16) / (div)) >> 4) \
                            : (u32) math_atan((s32) ((u32) (d) << 16) / (div)) >> 4)   \
              : ((negIfPos) ? (u32) math_atan((s32) ((u32) -(d) << 16) / (div)) >> 4   \
                            : -((u32) math_atan((s32) ((u32) -(d) << 16) / (div)) >> 4)))

/* Per-frame wheel-slot ground contact and tilt (0x802A8768).
 * Per-frame ground contact of vehicle `id` (record veh): out-of-bounds reset
 * (veh_respawn_if_out, may move (x, z) and write *px/*py/*pz); gVehTiltDivB = divB,
 * gVehTiltDivA = divA, gVehContactAngle = *angle, gVehSlot0Flags = flags[0]; veh+0x9B =
 * 0; veh_check_no_wheels(tbl), veh_diamond_sands_damping, veh_rider_gravity. Then the three wheel
 * slots: for i = 0..2, if gNoWheels is set and i > 0, veh_copy_slot0(veh) and
 * stop; else (s8) flags[i] == 1 -> veh_slot_airborne (airborne), 0 ->
 * veh_slot_grounded (grounded; other values hit the asm's `syscall` debug trap
 * first). After: flags[0..2] = gSlotAirborne[0..2]; with h0..h2 =
 * gSlotY[0..2] the height records ys (three of {now, prev, prev2})
 * shift in h0/h1/h2; *px = x, *pz = z, *py = (h1 + h2) >> 1 (logical);
 * gModelRotZ = tilt (h1 - h0, divA, negated when >= 0) and gModelRot = fp =
 * tilt (h2 - h0, divB, negated when < 0) (TILT_8768). Then
 * veh_update_shadow(id, *px, (gSlotGroundY[1] + [2]) >> 1, *pz, (h1 + h2) >> 1,
 * veh_ground_tilt's two results, gVehContactAngle), veh_ground_surface(veh, flags, id)
 * and, when gRiderPass is 0, id != 0xFF and gHaveRideables is set,
 * veh_update_stand_on(*px, *pz, veh + 0x5E, veh + 0x4C, veh + 4, id, veh, fp, f).
 * The asm's add/sub/neg trap on overflow.
 * Register convention: veh gp, id t8, px t7, py s2, pz s1, x t0, z t1, divB t9,
 * divA fp, angle s4, flags s0, tbl v1, a/b/c a1-a3, ys s7; s3 in/out, s4 and
 * fp out (r), f12-f26 in/out (f). The asm restores t4 and clobbers s5-s7 (s7
 * = veh + 4 on the veh_update_stand_on path); t6 is left as the callees leave it
 * (listed as read by the vehicle callers; not modelled). Asm callers keep t7
 * live. */
void veh_ground_contact(u8 *veh, s32 id, s32 *px, s32 *py, s32 *pz, s32 x, s32 z, s32 divB, s32 divA, s16 *angle,
                   u8 *flags, s16 *tbl, s32 *a, s32 *b, s32 *c, s32 *ys, Regs802A8768 *r, TriSideOut *f) {
    Pos802A8CCC pos;
    s32 s3 = r->s3;
    s32 i;
    s32 h0;
    s32 h1;
    s32 h2;
    s32 t;
    s32 fp;
    s32 r1;
    s32 r2;

    pos.t0 = x;
    pos.t1 = z;
    veh_respawn_if_out(veh, id, px, py, pz, &pos);
    x = pos.t0;
    z = pos.t1;
    gVehTiltDivB = divB;
    gVehTiltDivA = divA;
    gVehContactAngle = *angle;
    gVehSlot0Flags = flags[0];
    VEH_U8(veh, VS_ON_MOVING_PART) = 0;
    veh_check_no_wheels((u16 *) tbl);
    veh_diamond_sands_damping(veh);
    veh_rider_gravity();
    for (i = 0; i < 3; i++) {
        if (gNoWheels != 0 && i != 0) {
            veh_copy_slot0(veh);
            break;
        }
        if (((s8 *) flags)[i] == 1) {
            veh_slot_airborne(i, a, b, c, tbl, x, z, angle, ys, id, veh, divA, &s3, f);
        } else { /* 0 (other values: the asm's syscall, then this) */
            s3 = veh_slot_grounded(i, a, b, c, tbl, x, z, angle, ys, id, veh, divA, f);
        }
    }
    flags[0] = (&gSlotAirborne)[0];
    flags[1] = (&gSlotAirborne)[1];
    flags[2] = (&gSlotAirborne)[2];
    h0 = (&gSlotY)[0];
    h1 = (&gSlotY)[1];
    h2 = (&gSlotY)[2];
    t = ys[1];
    ys[2] = t;
    ys[1] = ys[0];
    ys[0] = h0;
    ys[5] = ys[4];
    ys[4] = ys[3];
    ys[3] = h1;
    ys[8] = ys[7];
    ys[7] = ys[6];
    ys[6] = h2;
    *px = x;
    *pz = z;
    *py = (u32) (h1 + h2) >> 1;
    t = h1 - h0;
    D_803ED394_ = TILT_8768(t, divA, 1);
    t = h2 - h0;
    fp = TILT_8768(t, divB, 0);
    gModelRot = fp;
    r1 = veh_ground_tilt(&r2);
    veh_update_shadow(id, *px, (u32) ((&gSlotGroundY)[1] + (&gSlotGroundY)[2]) >> 1, *pz,
                  (u32) ((&gSlotY)[1] + (&gSlotY)[2]) >> 1, r1, r2, gVehContactAngle);
    veh_ground_surface(veh, flags, id);
    r->s4 = angle;
    if (gRiderPass == 0 && id != 0xFF && gHaveRideables != 0) {
        r->s4 = &VEH_S16(veh, VS_HEADING);
        veh_update_stand_on(*px, *pz, &VEH_S16(veh, VS_STAND_OFFSETS), &VEH_S16(veh, VS_HEADING), &VEH_S32(veh, 4), id, veh, fp, f);
    }
    r->s3 = s3;
    r->fp = fp;
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/62740/veh_ground_contact.s")
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
extern s16 gVehTiltDivB;
extern s16 gVehTiltDivA;

/* Two tilt angles from the slot ground heights (0x802A8B10).
 * Two tilt angles from the word triple gSlotGroundY/gSlotGroundY1/gSlotGroundY2: a = math_atan(
 * (|AC - A8| << 16) / gVehTiltDivA) >> 4 (logical shift), negated when
 * AC >= A8, goes to *a3Out; b likewise from B0 - A8 and gVehTiltDivB, negated
 * when B0 < A8, is returned. The differences use trapping `sub`/`neg` and the
 * divides trap on a zero divisor.
 * Register convention: outputs a1 (return value) and a3 (*a3Out). The asm saves
 * v0, v1, a2, t2, t3, s5, t9 and fp; its caller veh_ground_contact keeps a2, t7,
 * t8, f12 and f14 live. */
s32 veh_ground_tilt(s32 *a3Out) {
    s32 base = gSlotGroundY;
    s32 d = gSlotGroundY1 - base;
    s32 r;

    if (d >= 0) {
        r = math_atan((s32) ((u32) d << 16) / gVehTiltDivA);
        *a3Out = -((u32) r >> 4);
    } else {
        r = math_atan((s32) ((u32) -d << 16) / gVehTiltDivA);
        *a3Out = (u32) r >> 4;
    }
    d = gSlotGroundY2 - base;
    if (d >= 0) {
        r = math_atan((s32) ((u32) d << 16) / gVehTiltDivB);
        return (u32) r >> 4;
    }
    r = math_atan((s32) ((u32) -d << 16) / gVehTiltDivB);
    return -((u32) r >> 4);
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/62740/veh_ground_tilt.s")
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING

/* (Pos802A8CCC is declared at the top of the file.) */

/* Out of the level box: back to the respawn point (0x802A8CCC).
 * Out-of-bounds reset for vehicle `id` (record veh): with gx = x >> 5 and
 * gz = z >> 5, vehicle 7 counts as out on level 0 when gx < 1000, on level
 * 0x12 when gz < 900 and on level 0xD when gz < 0x44C; any vehicle is out
 * when (gx, gz) leaves the box gBoundsMinX..36. When out: the respawn record
 * with key `id` (key 1 unless gLevelKind is 1 or 0x80; the scan has no end
 * check, the key must exist) gives x, y, z (each s16 << 5), stored to *px,
 * *py, *pz and handed back in pos; gCamSnap = 1; veh_reset_state(veh);
 * words 1..9 of veh = y; effect kind 0x13 (data 1000000) at the new position
 * (fx_spawn_at_late) and hud_panel_start(1, 1, 4, 0x6C).
 * Register convention: veh in gp, id t8, px t7, py s2, pz s1, x/z in t0/t1
 * (in/out through pos; conventions.txt). The asm saves every other register
 * (around the two calls all of them); its asm caller veh_ground_contact keeps
 * a1-a3, t7-t9 live. f12/f14 are left as the callees leave them, which the
 * survey lists as read afterwards; not modelled. */
void veh_respawn_if_out(u8 *veh, s32 id, s32 *px, s32 *py, s32 *pz, Pos802A8CCC *pos) {
    s32 gx = pos->t0 >> 5;
    s32 gz = pos->t1 >> 5;
    s32 out = 0;
    s32 key;
    u8 *rec;
    s32 x;
    s32 y;
    s32 z;
    s32 i;

    if (id == 7) {
        if (levelno == 0) {
            out = gx < 1000;
        } else if (levelno == 0x12) {
            out = gz < 900;
        } else if (levelno == 0xD) {
            out = gz < 0x44C;
        }
    }
    if (!out && gx >= gBoundsMinX && gx <= gBoundsMaxX && gz >= gBoundsMinZ && gz <= gBoundsMaxZ) {
        return;
    }
    key = (((s32) gLevelKind) == LEVEL_KIND_MISSION || ((s32) gLevelKind) == LEVEL_KIND_MISSION_TIMED) ? id : 1;
    rec = gRespawnPoints;
    while (rec[0] != key) {
        rec += 9;
    }
    gCamSnap = 1;
    x = (s16) ((rec[1] << 8) | rec[2]) << 5;
    *px = x;
    y = (s16) ((rec[3] << 8) | rec[4]) << 5;
    *py = y;
    z = (s16) ((rec[5] << 8) | rec[6]) << 5;
    *pz = z;
    veh_reset_state(veh);
    for (i = 1; i <= 9; i++) {
        VEH_S32(veh, i * 4) = y;
    }
    fx_spawn_at_late(x, y, z, 0x13, 1000000);
    hud_panel_start(1, 1, 4, SFX_PANEL_WARNING);
    pos->t0 = x;
    pos->t1 = z;
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/62740/veh_respawn_if_out.s")
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING

/* The rider pass: gravity x3 (0x802A8FB4).
 * If gRiderPass is set, triple gGravity.
 * Register note: the asm touches only v0/at/f0/f2; its caller veh_ground_contact
 * keeps a1-a3, t0, t1, t7-t9, f12 and f14 live across the call (a mixed N64
 * build would need a thunk, the native port does not). */
void veh_rider_gravity(void) {
    if (gRiderPass != 0) {
        gGravity = gGravity * 3.0f;
    }
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/62740/veh_rider_gravity.s")
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
/* If the highest surface class under the vehicle (veh+0x9F, VehState.surfaceMax) is 0x67 and levelno == 12, set
 * gBounceDamping = 8. (The asm clobbers v0.) Asm callers keep a1-a3, t0, t1,
 * t7-t9, f12 and f14 live. */
void veh_diamond_sands_damping(u8 *veh) {
    if (VEH_U8(veh, VS_SURFACE_MAX) == SURFACE_DIAMOND_SANDS && levelno == 12) {
        gBounceDamping = 8;
    }
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/62740/veh_diamond_sands_damping.s")
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
/* No wheel table: slot 0's results to slots 1-2 (0x802A9038).
 * Copy each "current" value into its two shadow copies: the bytes gSlotAirborne,
 * gSlotSurface, gSlotStandOn, the words gSlotY, gSlotGroundY and the vehicle
 * words +0x34, +0x28, +0x40. (The asm saves v0.) Asm callers keep t0, t1,
 * t7-t9, f12 and f14 live. */
void veh_copy_slot0(u8 *veh) {
    s32 w;

    (&gSlotAirborne1)[0] = gSlotAirborne;
    (&gSlotAirborne1)[1] = gSlotAirborne;
    gSlotY1 = gSlotY;
    gSlotY2 = gSlotY;
    gSlotGroundY1 = gSlotGroundY;
    gSlotGroundY2 = gSlotGroundY;
    w = VEH_S32(veh, VS_SLOT_LAUNCH_Y);
    VEH_S32(veh, 0x38) = w;
    VEH_S32(veh, 0x3C) = w;
    w = VEH_S32(veh, VS_SLOT_LAUNCH_VEL);
    VEH_S32(veh, 0x2C) = w;
    VEH_S32(veh, 0x30) = w;
    w = VEH_S32(veh, VS_SLOT_AIR_TIME);
    VEH_S32(veh, 0x44) = w;
    VEH_S32(veh, 0x48) = w;
    (&gSlotSurface)[1] = gSlotSurface;
    (&gSlotSurface)[2] = gSlotSurface;
    gSlotStandOn1 = gSlotStandOn;
    (&gSlotStandOn1)[1] = gSlotStandOn;
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/62740/veh_copy_slot0.s")
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
/* GNoWheels: the wheel offset table is all zero (0x802A90E4).
 * gNoWheels = 1 if all six halfwords p[0..5] are 0, else 0.
 * Register convention: p in v1 (the asm saves v0). Asm callers keep a1-a3,
 * t0, t1, t7-t9, f12 and f14 live. */
void veh_check_no_wheels(u16 *p) {
    if (p[0] == 0 && p[1] == 0 && p[2] == 0 && p[3] == 0 && p[4] == 0 && p[5] == 0) {
        gNoWheels = 1;
    } else {
        gNoWheels = 0;
    }
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/62740/veh_check_no_wheels.s")
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
/* Surface grip and the water / fatal surfaces (0x802A9164).
 * From the three bytes at gSlotSurface (each >= 100 counted as 2): veh+0x50 =
 * their average; veh+0x9F = gSurfaceMax = their maximum. Then set gFailTrigger
 * when the max is 0x66, or when it is 100 with gPlayFrames set and kind not
 * 9/11/0x11/0x12 - both only if *f != 1.
 * Register convention: gp, f in s0, kind in t8 (the asm clobbers t0-t2).
 * Asm callers keep a0, a2, a3, t7, t8, f12 and f14 live. */
void veh_ground_surface(u8 *veh, u8 *f, s32 kind) {
    u8 *v = &gSlotSurface;
    s32 sum;
    s32 x;
    s32 max;

    x = v[0];
    if (x >= 100) {
        x = 2;
    }
    sum = x;
    x = v[1];
    if (x >= 100) {
        x = 2;
    }
    sum += x;
    x = v[2];
    if (x >= 100) {
        x = 2;
    }
    sum += x;
    VEH_U8(veh, VS_SURFACE) = (u32) sum / 3;
    max = 0;
    if (max < v[0]) {
        max = v[0];
    }
    if (max < v[1]) {
        max = v[1];
    }
    if (max < v[2]) {
        max = v[2];
    }
    VEH_U8(veh, VS_SURFACE_MAX) = max;
    gSurfaceMax = max;
    if (gSurfaceMax == SURFACE_FATAL && f[0] != 1) {
        gFailTrigger = 1;
    }
    if (kind != VEH_JBOMB && kind != VEH_BARGE_0B && kind != VEH_BARGE_11 && kind != VEH_BARGE_12 && gPlayFrames != 0 && gSurfaceMax == SURFACE_WATER &&
        f[0] != 1) {
        gFailTrigger = 1;
    }
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/62740/veh_ground_surface.s")
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
/* GStandOn: what each wheel slot rests on (0x802A92C8).
 * Ground heights for the three wheel slots: for i = 0..2, (dx, dz) =
 * veh_wheel_offset(i, tbl, angle) and ground_slot_height(i, x + dx, z + dz,
 * ys[3 * i], key, veh, fpIn, f) (12-byte records, y first; the FP state f
 * chains through). Then the slot kinds go to key's gStandOn record
 * (PORT_STORE_KINDS). Returns gSlotStandOn[1] (the asm's t6 at the end). The
 * asm's adds trap on overflow.
 * Register convention: x t0, z t1, tbl t3, angle s4, ys s7, key t8, veh gp,
 * fp; f12-f26 in/out (f); result t6. The asm restores t0, t1, t7, s0, s2, s7,
 * t9, clobbers s5/s6 and leaves v0 = 3, t2 = -1. Asm caller crane_update
 * keeps t7 live. */
s32 veh_update_stand_on(s32 x, s32 z, s16 *tbl, s16 *angle, s32 *ys, s32 key, u8 *veh, s32 fpIn, TriSideOut *f) {
    s32 i;
    s32 dx;
    s32 dz;

    for (i = 0; i < 3; i++) {
        dx = veh_wheel_offset(i, tbl, angle, &dz);
        ground_slot_height(i, x + dx, z + dz, ys[i * 3], key, veh, fpIn, f);
    }
    PORT_STORE_KINDS(key);
    return (&gSlotStandOn)[1];
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/62740/veh_update_stand_on.s")
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
/* A grounded wheel slot follows or leaves the ground (0x802A93B0).
 * Ground slot `index` while on the ground: (dx, dz) = veh_wheel_offset(index,
 * tbl, angle); y = ys[3 * index], prev = ys[3 * index + 1]; v =
 * veh_clamp_climb(y - prev) (clamped step); expected = v +
 * round(gGravity) + y; h = ground_slot_height(index, x + dx, z + dz, y, key,
 * veh, fpIn, f). If h <= expected - 0x1E (the ground fell away):
 * v = veh_slot_launch(index, a, b, c, y, v) and the height is `expected`;
 * else gSlotAirborne[index] = 0 and the height is h. gSlotY[index] = the
 * height. Returns v. The asm's add/sub trap on overflow.
 * Register convention: index v0, a/b/c a1-a3, tbl v1, x t0, z t1, angle s4,
 * ys s7, key t8, veh gp, fp; result s3, f12-f26 in/out (f). The asm restores
 * t0, t1, t3, t6, t7, s0-s2, s4; asm caller veh_ground_contact keeps v0, a1-a3,
 * t0, t1, t7-t9 live. */
s32 veh_slot_grounded(s32 index, s32 *a, s32 *b, s32 *c, s16 *tbl, s32 x, s32 z, s16 *angle, s32 *ys, s32 key,
                  u8 *veh, s32 fpIn, TriSideOut *f) {
    s32 dx;
    s32 dz;
    s32 y;
    s32 v;
    s32 expected;
    s32 h;

    dx = veh_wheel_offset(index, tbl, angle, &dz);
    y = ys[index * 3];
    v = veh_clamp_climb(y - ys[index * 3 + 1]);
    expected = v + port_cvt_w_s(gGravity) + y;
    h = ground_slot_height(index, x + dx, z + dz, y, key, veh, fpIn, f);
    if (!(expected - 0x1E < h)) {
        v = veh_slot_launch(index, a, b, c, y, v);
        h = expected;
    } else {
        (&gSlotAirborne)[index] = 0;
    }
    (&gSlotY)[index] = h;
    return v;
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/62740/veh_slot_grounded.s")
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
/* A wheel table entry turned by the heading (0x802A94A4).
 * Rotate table entry `index` of tbl ({s16 x, s16 z} pairs) by the angle *angle
 * (s16) with math_rotate_xz_inv: returns its a3 result (the x offset) and stores its
 * t1 result (the z offset) in *dz. (The asm's add of index * 4 traps.)
 * Register convention: index v0, tbl v1, angle s4; results t5 (return value)
 * and t6 (*dz). The asm saves a0-a3, t0, t1 and fp around the call; asm
 * callers keep v0, v1, a0-a3, t0-t4, t7 and t8 live. It also leaves the sine
 * routine's f12/f14 temporaries, which the survey lists as read by
 * veh_update_stand_on/93B0/95A4/992C/9A60: they only flow into the triangle scans'
 * FP pass-through state (TriSideOut), not modelled here (a mixed N64 build
 * would need a thunk; the native port won't). */
s32 veh_wheel_offset(s32 index, s16 *tbl, s16 *angle, s32 *dz) {
    return math_rotate_xz_inv(tbl[index * 2], tbl[index * 2 + 1], *angle, dz);
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/62740/veh_wheel_offset.s")
#endif

/* veh_clamp_climb: clamps $s3 to a max of 0x240 (`if (s3 >= 0x241) s3 =
 * 0x240;`), reading AND writing $s3 directly with no parameter or
 * return value involved at all - same non-ABI register-threading
 * character as the anim_tex_find family in hd_code/56040.c, here used
 * as a persistent "hot" value shared across functions via a dedicated
 * callee-saved register instead of a global variable. Not expressible
 * as a normal C function. */
#ifdef NON_MATCHING
/* Clamps a slot's vertical speed (0x802A9514).
 * Port rewrite: clamp to at most 0x240 (negative values pass through).
 * Register convention: value in s3, result in s3 (conventions.txt maps it to
 * a0/ret). Asm callers keep a1-a3, t0-t2, t8, t9, f12 and f14 live. */
s32 veh_clamp_climb(s32 x) {
    if (x >= 0 && x >= 0x241) {
        x = 0x240;
    }
    return x;
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/62740/veh_clamp_climb.s")
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
/* A wheel slot leaves the ground (0x802A9540).
 * Record slot `index`: a[index] = value clamped by veh_clamp_climb, b[index] = 2,
 * c[index] = extra, gSlotAirborne[index] = 1; returns the clamped value.
 * Register convention: index v0, a/b/c in a1-a3, extra t2, value s3; result in
 * s3 (the asm saves a1-a3; it sets s0 = 1 and clobbers t3, t7). Asm callers
 * keep t8, t9, f12 and f14 live. */
s32 veh_slot_launch(s32 index, s32 *a, s32 *b, s32 *c, s32 extra, s32 value) {
    value = veh_clamp_climb(value);
    a[index] = value;
    b[index] = 2;
    c[index] = extra;
    (&gSlotAirborne)[index] = 1;
    return value;
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/62740/veh_slot_launch.s")
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING

/* An airborne wheel slot falls or touches down (0x802A95A4).
 * Ground slot `index` while airborne: with n = b[index] (then b[index] =
 * n + 1), drop = a[index] * n + round(gGravity * (f32) (n * n)) (32-bit
 * products) and expected = c[index] + drop. (dx, dz) = veh_wheel_offset(index,
 * tbl, angle), h = ground_slot_height(index, x + dx, z + dz, ys[3 * index], key,
 * veh, fpIn, f). Landing test: with key 9 and gJbombCeilingAbove set, it lands when
 * h - expected >= 0x6A5 or h < expected; otherwise when h < expected. Landed:
 * gSlotY[index] = max(expected, 0), gSlotAirborne[index] = 1, s3 unchanged.
 * Not landed: gSlotY[index] = h and *s3 = veh_slot_land(index, a, b, c, h,
 * drop, *s3). The asm's add/sub trap on overflow.
 * Register convention: index v0, a/b/c a1-a3, tbl v1, x t0, z t1, angle s4,
 * ys s7, key t8, veh gp, fp; s3 in/out (*s3), f12-f26 in/out (f); the asm
 * restores t0-t7, s0-s2 and leaves s6 = drop or what veh_slot_land leaves
 * (clobbers s6). Asm caller veh_ground_contact keeps v0, a1-a3, t0, t1, t7-t9
 * live. */
void veh_slot_airborne(s32 index, s32 *a, s32 *b, s32 *c, s16 *tbl, s32 x, s32 z, s16 *angle, s32 *ys, s32 key,
                   u8 *veh, s32 fpIn, s32 *s3, TriSideOut *f) {
    s32 n = b[index];
    s32 drop;
    s32 expected;
    s32 dx;
    s32 dz;
    s32 h;

    b[index] = n + 1;
    drop = a[index] * n + port_cvt_w_s(gGravity * (f32) (n * n));
    expected = c[index] + drop;
    dx = veh_wheel_offset(index, tbl, angle, &dz);
    h = ground_slot_height(index, x + dx, z + dz, ys[index * 3], key, veh, fpIn, f);
    if ((key == 9 && gJbombCeilingAbove != 0 && h - expected >= 0x6A5) || h < expected) {
        if (expected < 0) {
            expected = 0;
        }
        (&gSlotY)[index] = expected;
        (&gSlotAirborne)[index] = 1;
    } else {
        (&gSlotY)[index] = h;
        *s3 = veh_slot_land(index, a, b, c, h, drop, *s3);
    }
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/62740/veh_slot_airborne.s")
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING

/* Touch-down: bounce (sound 0xC) or settle (0x802A9710).
 * Slot `index` check: predicted = a[index] * (b[index] - 2) + round(gGravity
 * * (b[index] - 2)^2) (32-bit products, cvt.w.s rounding); if |pos -
 * predicted| exceeds gBounceThreshold (signed compare), play sound 0xC when
 * gBounceSound is set (the asm saves every register around that call) and
 * restart the slot with veh_slot_launch(index, a, b, c, extra, |pos -
 * predicted| / gBounceDamping) (unsigned divide; gBounceDamping == 0 traps),
 * returning its result. Otherwise gSlotAirborne[index] = 0 and the result is
 * the caller's s3, unchanged. The asm's add/sub/neg trap on overflow.
 * Register convention: index v0, a/b/c a1-a3, extra t3, pos s6, s3 in/out
 * (result); it clobbers s0 and s6. Asm caller veh_slot_airborne keeps f12 and
 * f14 live. */
s32 veh_slot_land(s32 index, s32 *a, s32 *b, s32 *c, s32 extra, s32 pos, s32 s3) {
    s32 n = b[index] - 2;
    s32 d = pos - (a[index] * n + port_cvt_w_s(gGravity * (f32) (n * n)));

    if (d < 0) {
        d = -d;
    }
    if (!(gBounceThreshold < d)) {
        (&gSlotAirborne)[index] = 0;
        return s3;
    }
    if (gBounceSound != 0) {
        snd_play(gSfxBank, SFX_THUD, NULL);
    }
    return veh_slot_launch(index, a, b, c, extra, (u32) d / gBounceDamping);
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/62740/veh_slot_land.s")
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
/* At spawn: the wheel slots from the ground scans (0x802A992C).
 * Reset the three ground slots from the triangle scans: for i = 0..2, (dx, dz)
 * = veh_wheel_offset(i, tbl, angle); h = ground_scan_moveable(x + dx, z + dz, y, key, f,
 * &kind, ..); when kind is 0, ground_scan_grid(x + dx, z + dz, y, f, &h, &fp)
 * may replace h (and fp). gSlotStandOn[i] = kind, dst[3i..3i+2] = h,
 * gSlotSurface[i] = fp (low byte; fp carries over between slots). Then *mid =
 * (dst[3] + dst[6]) >> 1 (logical), veh+0x50 = (gSlotSurface[0] + [1] + [2]) / 3
 * and the kinds go to key's gStandOn record (PORT_STORE_KINDS). Returns dst
 * (the asm's s3). The asm's adds trap on overflow.
 * Register convention: tbl v1, y t2, x t7, z s0, dst s1, mid s2, angle s4, key
 * t8, fp, veh gp; f12-f26 in/out (f); result s3. The asm restores s1 and fp
 * and clobbers s5/s6 (and a1/a2 through the scans). */
s32 *veh_init_ground(s16 *tbl, s32 y, s32 x, s32 z, s32 *dst, s32 *mid, s16 *angle, s32 key, s32 fpIn, u8 *veh,
                   TriSideOut *f) {
    s32 fp = fpIn;
    s32 *p = dst;
    s32 i;
    s32 dx;
    s32 dz;
    s32 h;
    s32 kind;
    s32 a2;

    for (i = 0; i < 3; i++) {
        dx = veh_wheel_offset(i, tbl, angle, &dz);
        h = ground_scan_moveable(x + dx, z + dz, y, key, f, &kind, &a2);
        if (kind == 0) {
            ground_scan_grid(x + dx, z + dz, y, f, &h, &fp);
            kind = 0;
        }
        (&gSlotStandOn)[i] = kind;
        p[0] = h;
        p[1] = h;
        p[2] = h;
        p += 3;
        (&gSlotSurface)[i] = fp;
    }
    *mid = (u32) (dst[3] + dst[6]) >> 1;
    VEH_U8(veh, VS_SURFACE) = (u32) ((&gSlotSurface)[0] + (&gSlotSurface)[1] + (&gSlotSurface)[2]) / 3;
    PORT_STORE_KINDS(key);
    return dst;
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/62740/veh_init_ground.s")
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING

/* A placed vehicle put back on the ground (0x802A9A60).
 * As veh_init_ground but through ground_slot_height: for i = 0..2, veh+0x9B = 0,
 * h = ground_slot_height(i, x + dx, z + dz, y, key, veh, fp, f), dst[3i..3i+2] =
 * h, gSlotSurface[i] = fp (overwriting ground_slot_height's flag). Then *mid =
 * (dst[3] + dst[6]) >> 1 (logical), veh+0x50 = (gSlotSurface[0] + [1] + [2]) /
 * 3, gModelRot = gModelRotZ = 0. Returns the last dz (the asm's t6); out
 * gets dst + 9 and dst (s1, s3). The asm's adds trap on overflow.
 * Register convention: tbl v1, y t2, x t7, z s0, dst s1, mid s2, angle s4, key
 * t8, fp, veh gp; f12-f26 in/out (f); results t6, s1, s3 (out); clobbers
 * s5/s6. Asm callers keep t7 live. */
s32 veh_reground(s16 *tbl, s32 y, s32 x, s32 z, s32 *dst, s32 *mid, s16 *angle, s32 key, s32 fp, u8 *veh,
                  TriSideOut *f, Out802A9A60 *out) {
    s32 *p = dst;
    s32 i;
    s32 dx;
    s32 dz;
    s32 h;

    for (i = 0; i < 3; i++) {
        dx = veh_wheel_offset(i, tbl, angle, &dz);
        VEH_U8(veh, VS_ON_MOVING_PART) = 0;
        h = ground_slot_height(i, x + dx, z + dz, y, key, veh, fp, f);
        p[0] = h;
        p[1] = h;
        p[2] = h;
        p += 3;
        (&gSlotSurface)[i] = fp;
    }
    *mid = (u32) (dst[3] + dst[6]) >> 1;
    VEH_U8(veh, VS_SURFACE) = (u32) ((&gSlotSurface)[0] + (&gSlotSurface)[1] + (&gSlotSurface)[2]) / 3;
    gModelRot = 0;
    D_803ED394_ = 0;
    out->s1 = p;
    out->s3 = dst;
    return dz;
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/62740/veh_reground.s")
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
void ground_cache_tri(s32 index, s32 kind);

#define ABS_9B1C(v) ((v) < 0 ? -(v) : (v))

/* The nearest ground height of the three sources, slot i (0x802A9B1C).
 * Ground height for slot `index` at (x, y + 0x78, z): h1 = ground_scan_part_tris
 * (its fp result kept as fp1), h2 = ground_scan_moveable (its id result kept),
 * then ground_scan_grid (found flag, h3 and fp in/out, seeded with h2 and fp1).
 * Not found and h1, h2 both 0x5F5E0FF: result y, gSlotSurface = 1. Found and
 * h3 at least as close (|h - (y + 0x78)|) as h2 and h1: ground_cache_tri(index,
 * skip), result h3, gSlotSurface = its fp. Otherwise h2 if strictly closer than
 * h1 (gSlotSurface = 1, gSlotStandOn = the id), else h1 (veh byte 0x9B = 1,
 * gSlotSurface = fp1). gSlotStandOn[index] is 0 except on the h2 path; the result
 * is clamped at 0 and stored to gSlotGroundY[index]. The TriSideOut FP results
 * chain through the three scans.
 * Register convention: index v0, x t0, z t1, y t2, skip t8, veh gp, fp in;
 * result t3, f12-f26 in/out (f); f28 clobbered by the scans. The asm's
 * add/sub/neg trap on overflow. Asm callers keep a1-a3, t2, t6-t8 live (a
 * mixed N64 build would need a thunk). */
s32 ground_slot_height(s32 index, s32 x, s32 z, s32 y, s32 skip, u8 *veh, s32 fpIn, TriSideOut *f) {
    s32 yy = y + 0x78;
    s32 fp = fpIn;
    s32 fp1;
    s32 h1;
    s32 h2;
    s32 h3;
    s32 id;
    s32 a2;
    s32 d1;
    s32 d2;
    s32 d3;
    s32 res;
    s32 flag;
    s32 kind = 0;

    h1 = ground_scan_part_tris(x, z, yy, f, &fp);
    fp1 = fp;
    h2 = ground_scan_moveable(x, z, yy, skip, f, &id, &a2);
    h3 = h2;
    if (ground_scan_grid(x, z, yy, f, &h3, &fp) == 0) {
        if (h1 == 0x5F5E0FF && h2 == 0x5F5E0FF) {
            res = yy - 0x78;
            flag = SURFACE_NORMAL;
            goto store;
        }
        d2 = ABS_9B1C(h2 - yy);
        d1 = ABS_9B1C(h1 - yy);
    } else {
        d2 = ABS_9B1C(h2 - yy);
        d3 = ABS_9B1C(h3 - yy);
        d1 = ABS_9B1C(h1 - yy);
        if (!(d2 < d3) && !(d1 < d3)) {
            ground_cache_tri(index, skip);
            res = h3;
            flag = fp;
            goto store;
        }
    }
    if (d2 < d1) {
        res = h2;
        flag = SURFACE_NORMAL;
        kind = id;
    } else {
        VEH_U8(veh, VS_ON_MOVING_PART) = 1;
        res = h1;
        flag = fp1;
    }
store:
    (&gSlotStandOn)[index] = kind;
    (&gSlotSurface)[index] = flag;
    if (res < 0) {
        res = 0;
    }
    (&gSlotGroundY)[index] = res;
    return res;
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/62740/ground_slot_height.s")
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
/* Moves the hit triangle to the front of its grid cell (0x802A9CAC).
 * If the 0x14-byte record list gGroundCellTris..gGroundCellTrisEnd spans at least 0x8C
 * bytes, swap record (index + (kind == 0xFF ? 3 : 0)) with the record
 * gGroundHitTri points at (through the temp gGroundTriSwap), unless they're the same.
 * Register convention: index in v0, kind in t8 (the asm saves v1, a0-a3).
 * Asm callers keep t3, f12 and f14 live. */
void ground_cache_tri(s32 index, s32 kind) {
    u8 *base = gGroundCellTris;
    s32 *rec;
    s32 *cur;
    s32 i;

    if ((s32) (gGroundCellTrisEnd - base) < 0x8C) {
        return;
    }
    if (kind == 0xFF) {
        index += 3;
    }
    rec = (s32 *) (base + index * 0x14);
    cur = (s32 *) gGroundHitTri;
    if (cur == rec) {
        return;
    }
    for (i = 0; i < 5; i++) {
        gGroundTriSwap[i] = cur[i];
    }
    cur = (s32 *) gGroundHitTri;
    for (i = 0; i < 5; i++) {
        cur[i] = rec[i];
    }
    for (i = 0; i < 5; i++) {
        rec[i] = gGroundTriSwap[i];
    }
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/62740/ground_cache_tri.s")
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
s32 ground_tri_height(s32 x, s32 z, s32 x0, s32 y0, s32 z0, s32 x1, s32 y1, s32 z1, s32 x2, s32 y2, s32 z2,
                  s32 *a3Out, s32 *t6Out);

/* The per-triangle test every scan below makes: (x, z) inside the bounding
 * box (ground_tri_box_contains) and the triangle (ground_tri_contains, which also fills *f),
 * then *h = the plane height there (ground_tri_height, which also sets the asm's
 * a3 and t6). v = x0, y0, z0, x1, y1, z1, x2, y2, z2. */
static s32 port_tri_height(s32 x, s32 z, s32 *v, TriSideOut *f, s32 *h, s32 *a3, s32 *t6) {
    if (ground_tri_box_contains(x, z, v[0], v[2], v[3], v[5], v[6], v[8]) == 0) {
        return 0;
    }
    if (ground_tri_contains(x, z, v[0], v[2], v[3], v[5], v[6], v[8], f) == 0) {
        return 0;
    }
    *h = ground_tri_height(x, z, v[0], v[1], v[2], v[3], v[4], v[5], v[6], v[7], v[8], a3, t6);
    return 1;
}

/* Loads a triangle stored as nine s16 coordinates in 1/32 units. */
#define PORT_TRI_S16(v, rec)                       \
    {                                              \
        s32 i_;                                    \
        for (i_ = 0; i_ < 9; i_++) {               \
            (v)[i_] = ((s16 *) (rec))[i_] << 5;    \
        }                                          \
    }

/* The nearest height over the moving level parts (0x802A9DC0).
 * Of the triangles in gGroupTris .. gGroupTrisEnd containing (x, z), the one
 * whose plane height is nearest y (the later one on a tie, |y - h| compared
 * unsigned): returns its height (0x5F5E0FF if none) and puts its byte +0x24
 * in *fpOut (unchanged if none).
 * Register convention: x t0, z t1, y t2; result t3, fp (*fpOut) and the
 * ground_tri_contains FP results (f12, f14, f20, f22, f24, f26 through f, in/out).
 * The asm saves every other register it touches and clobbers f28; `y - h` is a
 * trapping `sub`. Asm callers keep t0, t1, t2 and t8 live. */
s32 ground_scan_part_tris(s32 x, s32 z, s32 y, TriSideOut *f, s32 *fpOut) {
    u8 *rec = gGroupTris;
    u8 *end = gGroupTrisEnd;
    s32 best = 0x5F5E0FF;
    u32 bestD = 0x5F5E0FF;
    s32 h;
    s32 d;
    s32 a3;
    s32 t6;

    while (rec != end) {
        u8 *tri = rec;

        rec += 0x28;
        if (!port_tri_height(x, z, (s32 *) tri, f, &h, &a3, &t6)) {
            continue;
        }
        d = y - h;
        if (d < 0) {
            d = -d;
        }
        if (bestD < (u32) d) {
            continue;
        }
        best = h;
        bestD = d;
        *fpOut = tri[0x24];
    }
    return best;
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/62740/ground_scan_part_tris.s")
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
/* The nearest height over the vehicle decks (0x802A9F24).
 * ground_scan_part_tris over the transformed triangles gMoveableTris .. *gMoveableTrisEnd
 * (0x38 bytes, 9 s32 coords, u16 id at +0x36), skipping those with id ==
 * `skip`: returns the nearest height (0x5F5E0FF if none); *idOut = its id,
 * else 0 - but ground_tri_height's t6 (y0 - y2) when the last triangle it
 * measured was not the nearest, as in the asm; *a2Out = skip.
 * Register convention: x t0, z t1, y t2, skip t8; results t3, t6 (*idOut),
 * a2 (*a2Out) and the FP results through f (in/out). The asm saves every
 * other register it touches and clobbers f28; asm callers keep a1, t0-t2 and
 * t8 live. */
s32 ground_scan_moveable(s32 x, s32 z, s32 y, s32 skip, TriSideOut *f, s32 *idOut, s32 *a2Out) {
    u8 *rec = gMoveableTris;
    u8 *end = gMoveableTrisEnd;
    s32 best = 0x5F5E0FF;
    u32 bestD = 0x5F5E0FF;
    s32 t6 = 0;
    s32 id;
    s32 h;
    s32 d;
    s32 a3;

    *a2Out = skip;
    while (rec != end) {
        u8 *tri = rec;

        id = *(u16 *) (tri + 0x36);
        rec += 0x38;
        if (id == skip) {
            continue;
        }
        if (!port_tri_height(x, z, (s32 *) tri, f, &h, &a3, &t6)) {
            continue;
        }
        d = y - h;
        if (d < 0) {
            d = -d;
        }
        if (bestD < (u32) d) {
            continue;
        }
        best = h;
        bestD = d;
        t6 = id;
    }
    *idOut = t6;
    return best;
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/62740/ground_scan_moveable.s")
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING

/* The nearest height in the ground grid cell of (x, z) (0x802AA094).
 * Ground lookup: the grid cell of (x, z) (signed divides by gGroundCellW /
 * gGroundCellD, row stride gGroundCols) gives a list of 0x14-byte triangles (9
 * s16 coords in 1/32 units, u8 at +0x12, stop flag at +0x13); its bounds go
 * to gGroundCellTris/gGroundCellTrisEnd. Scanning like ground_scan_part_tris, each nearer-or-equal
 * hit records the triangle in gGroundHitTri, its height in *t3io and byte +0x12
 * in *fpio, and ends the scan if byte +0x13 is set. Returns 1 if any hit.
 * Register convention: x t0, z t1, y t2; results a1 (return value), t3 and fp
 * (in/out: unchanged without a hit) and the FP results through f. The asm
 * saves every other register it touches and clobbers f28; the divides trap on
 * a zero cell size, `y - h` and the index arithmetic are trapping. Asm callers
 * keep a2, t2, t4, t6 and t8 live. */
s32 ground_scan_grid(s32 x, s32 z, s32 y, TriSideOut *f, s32 *t3io, s32 *fpio) {
    s32 cx = x / ((s32) gGroundCellW);
    s32 cz = z / ((s32) gGroundCellD);
    u8 * N64P *cell = &gGroundCells[gGroundCols * cz + cx];
    u8 *rec = cell[0];
    u8 *end = cell[1] - 4;
    u32 bestD = 0x5F5E0FF;
    s32 found = 0;
    s32 v[9];
    s32 h;
    s32 d;
    s32 a3;
    s32 t6;

    gGroundCellTris = rec;
    gGroundCellTrisEnd = end;
    while (rec != end) {
        u8 *tri = rec;

        PORT_TRI_S16(v, tri);
        rec += 0x14;
        if (!port_tri_height(x, z, v, f, &h, &a3, &t6)) {
            continue;
        }
        d = y - h;
        if (d < 0) {
            d = -d;
        }
        if (bestD < (u32) d) {
            continue;
        }
        gGroundHitTri = tri;
        found = 1;
        *t3io = h;
        bestD = d;
        *fpio = tri[0x12];
        if (tri[0x13] != 0) {
            break;
        }
    }
    return found;
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/62740/ground_scan_grid.s")
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
f64 fabs(f64);
#pragma intrinsic(fabs)
extern f64 gGroundPlaneScale; /* 2000.0 */

/* A triangle's plane height at (x, z) (0x802AA2E4).
 * Height of the plane through the triangle p0 = (x0, y0, z0), p1, p2 above
 * the point (x, z), as round(|h / (ny * 2000)| * gGroundPlaneScale) - 1000, where
 * n = (nx, ny, nz) is the triangle normal (64-bit integer cross product),
 * d = -(n . p1) and h = nx * x - 1000 * ny + nz * z + d. Returns 0xFF676981
 * when ny == 0 (vertical triangle).
 * Register convention: x t0, z t1, p0 s1/s2/s3, p1 s4/s5/s6, p2 s7/t8/t9;
 * result in v0. The asm also leaves nz * z1 (low 32 bits) in a3 and y0 - y2
 * in t6, which some callers read: here *a3Out and *t6Out. The asm's 32-bit
 * subtractions, 64-bit dsubs and final add trap on overflow; the C wraps.
 * Rounding is to nearest even (cvt.l.d under the default FCSR).
 * Asm callers keep a1, a2, t0-t3, t7, f12 and f14 live. */
s32 ground_tri_height(s32 x, s32 z, s32 x0, s32 y0, s32 z0, s32 x1, s32 y1, s32 z1, s32 x2, s32 y2, s32 z2,
                  s32 *a3Out, s32 *t6Out) {
    s64 dy01 = y0 - y1;
    s64 dz02 = z0 - z2;
    s64 dz01 = z0 - z1;
    s64 dy02 = y0 - y2;
    s64 dx02 = x0 - x2;
    s64 dx01 = x0 - x1;
    s64 nx = dy01 * dz02 - dz01 * dy02;
    s64 ny = dz01 * dx02 - dx01 * dz02;
    s64 nz = dx01 * dy02 - dy01 * dx02;
    s64 nzz1 = nz * z1;
    s64 d = -(nx * x1 + ny * y1 + nzz1);
    s64 h = nx * x + ny * -1000 + nz * z + d;
    s64 t;
    f64 r;
    f64 frac;

    *t6Out = y0 - y2;
    *a3Out = (s32) nzz1;
    if (ny == 0) {
        return 0xFF676981;
    }
    r = gGroundPlaneScale * fabs((f64) h / (f64) (ny * 2000));
    t = (s64) r; /* truncates; round to nearest even below */
    frac = r - (f64) t;
    if (frac > 0.5 || (frac == 0.5 && (t & 1))) {
        t++;
    } else if (frac < -0.5 || (frac == -0.5 && (t & 1))) {
        t--;
    }
    return (s32) t - 1000;
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/62740/ground_tri_height.s")
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
/* (TriSideOut is declared at the top of the file.) */

/* (x, z) inside the triangle (0x802AA460).
 * 1 if the point (x, z) is on the same side of each edge of the triangle
 * (x0, z0), (x1, z1), (x2, z2) as the point c = ((x0 + (x1 + x2) / 2) / 2,
 * (z0 + (z1 + z2) / 2) / 2); edges whose line passes through (x, z) are
 * skipped. 0 otherwise. Float math in single precision, as the asm.
 * Register convention: x t0, z t1, x0 s1, z0 s3, x1 s4, z1 s6, x2 s7, z2 t9;
 * result in v0, plus f12/f14/f20/f22/f24/f26 (see TriSideOut) through `out`
 * (f24 is passed in too). The asm clobbers f28 (callee-saved) and f2-f18;
 * its x1 + x2, z1 + z2 and edge differences use trapping add/sub. Asm callers
 * keep a1-a3 and t0-t9 live. */
s32 ground_tri_contains(s32 x, s32 z, s32 x0, s32 z0, s32 x1, s32 z1, s32 x2, s32 z2, TriSideOut *out) {
    f32 px = x;
    f32 pz = z;
    f32 cx = ((f32) x0 + (f32) (x1 + x2) / 2.0f) / 2.0f;
    f32 cz = ((f32) z0 + (f32) (z1 + z2) / 2.0f) / 2.0f;
    f32 cross;
    f32 side = 2.0f;
    f32 sideZ = out->sideZ;
    f32 ax;
    f32 az;
    f32 dx;
    f32 dz;
    s32 edge;
    s32 result = 1;

    for (edge = 3; edge != 0; edge--) {
        if (edge == 3) {
            ax = x0;
            az = z0;
            dz = z1 - z0;
            dx = x1 - x0;
        } else if (edge == 2) {
            ax = x0;
            az = z0;
            dz = z2 - z0;
            dx = x2 - x0;
        } else {
            ax = x1;
            az = z1;
            dz = z2 - z1;
            dx = x2 - x1;
        }
        cross = (px - ax) * dz - (pz - az) * dx;
        if (cross == 0.0f) {
            continue;
        }
        sideZ = (cz - az) * dx;
        side = (cx - ax) * dz - sideZ;
        if (cross > 0.0f) {
            if (side > 0.0f) {
                continue;
            }
        } else if (side < 0.0f) {
            continue;
        }
        result = 0;
        break;
    }
    out->pz = pz;
    out->cross = cross;
    out->cz = cz;
    out->side = side;
    out->sideZ = sideZ;
    out->dz = dz;
    return result;
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/62740/ground_tri_contains.s")
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
/* (x, z) inside the triangle's bounding box (0x802AA5E0).
 * 1 if the point (x, z) lies inside the bounding box of the triangle
 * (x0, z0), (x1, z1), (x2, z2), bounds inclusive; else 0.
 * Register convention: x t0, z t1, x0 s1, z0 s3, x1 s4, z1 s6, x2 s7, z2 t9;
 * result in v0 (the asm saves t2-t5). Asm callers keep a1-a3, t0-t9, f12 and
 * f14 live. */
s32 ground_tri_box_contains(s32 x, s32 z, s32 x0, s32 z0, s32 x1, s32 z1, s32 x2, s32 z2) {
    s32 minX = x0;
    s32 minZ = z0;
    s32 maxX = x0;
    s32 maxZ = z0;

    if (x1 < minX) {
        minX = x1;
    }
    if (x2 < minX) {
        minX = x2;
    }
    if (maxX < x1) {
        maxX = x1;
    }
    if (maxX < x2) {
        maxX = x2;
    }
    if (z1 < minZ) {
        minZ = z1;
    }
    if (z2 < minZ) {
        minZ = z2;
    }
    if (maxZ < z1) {
        maxZ = z1;
    }
    if (maxZ < z2) {
        maxZ = z2;
    }
    if (x < minX || maxX < x || z < minZ || maxZ < z) {
        return 0;
    }
    return 1;
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/62740/ground_tri_box_contains.s")
#endif

#ifdef NON_MATCHING
extern u16 gMtxWork[]; /* accumulated Mtx (see mtx_mul) */
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
/* C entry: rotations, then mtx_build_model (0x802AA6D0).
 * o32 entry: gModelRot = rx, gModelRotY = ry, gModelRotZ = rz (halfwords),
 * then mtx_build_model(x, y, z, scale, m). The asm saves s0-s7, gp and fp. */
void mtx_build_model_rot(s32 x, s32 y, s32 z, s16 rx, s16 ry, s16 rz, s32 scale, Mtx *m) {
    gModelRotY = ry;
    gModelRot = rx;
    D_803ED394_ = rz;
    mtx_build_model(x, y, z, scale, (s32 *) m);
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/62740/mtx_build_model_rot.s")
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING

/* Scale, rotations x, z, y (gModelRot), translation (0x802AA764).
 * Build the model matrix m (16.16 words, converted in place to the Mtx
 * layout at the end by mtx_to_n64): m = scale matrix (scale on all three
 * axes, mtx_scale), then for each of rotation by gModelRot
 * (mtx_rotate_x), gModelRotZ (mtx_rotate_z), gModelRotY (mtx_rotate_y)
 * (angles read as u16) and the translation (x, y, z) << 11 (mtx_translate)
 * build that matrix in gMtxWork and combine it into m with
 * mtx_mul(gMtxWork, m).
 * Register convention: x s4, y s5, z s6, scale s7, m t8. The asm leaves a0 =
 * &gMtxWork and s2 = m (clobbers s2), and the cosine/sine routines' f12/f14
 * temporaries, which the survey lists as read by asm callers (not modelled,
 * see math_cos). */
void mtx_build_model(s32 x, s32 y, s32 z, s32 scale, s32 *m) {
    s32 *tmp = (s32 *) gMtxWork;

    mtx_scale(scale, scale, scale, m);
    mtx_rotate_x((u16) gModelRot, tmp);
    mtx_mul(tmp, m);
    mtx_rotate_z((u16) D_803ED394_, tmp);
    mtx_mul(tmp, m);
    mtx_rotate_y((u16) gModelRotY, tmp);
    mtx_mul(tmp, m);
    mtx_translate(x << 11, y << 11, z << 11, tmp);
    mtx_mul(tmp, m);
    mtx_to_n64((u16 *) m);
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/62740/mtx_build_model.s")
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
/* Copies one 0x40-byte Mtx at an offset (0x802AA838).
 * Copy 64 bytes (8 doublewords) from src + off to dst + off (8-aligned).
 * Register convention: src t0, dst t1, off t2 (the asm saves t0, t1, t3, t4).
 * Asm callers keep a1-a3, f12 and f14 live. */
void mtx_copy_at(u8 *src, u8 *dst, s32 off) {
    u64 *s = (u64 *) (src + off);
    u64 *d = (u64 *) (dst + off);
    s32 i;

    for (i = 8; i != 0; i--) {
        *d++ = *s++;
    }
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/62740/mtx_copy_at.s")
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
extern u16 gMtxWork[]; /* accumulated Mtx (s15.16: int halves, then frac halves at +0x20) */
extern u16 gMtxChainTemp[]; /* product temp, same layout */

/* Element (row, col) of an N64 fixed-point Mtx as s15.16. */
#define MTX_FIX(m, row, col) \
    ((s32) (((m)[PORT_HALF((row) * 4 + (col))] << 16) | (m)[PORT_HALF(16 + (row) * 4 + (col))]))


/* Concatenates a matrix chain, transforms a point (0x802AA890).
 * Concatenate `count` Mtx (each at base + offsets[i]) as M = M0 * M1 * ...
 * (64-bit products, >> 16) into gMtxWork, then transform (x, y, z) by M in
 * 32-bit integer math: x' = M00 x + M10 y + M20 z + (M30 int part only - the
 * asm drops its fraction), y' and z' likewise with full M31/M32. Returns
 * x' >> 11; y' >> 11 and z' >> 11 go to regs->v1/a0, y'/z' themselves to
 * regs->s1/s2, and the last matrix address to regs->a3 (with count == 1:
 * the first matrix + 0x40, where its copy loop leaves it). With count == 0
 * nothing is computed and the results are the caller's s0/s1/s2 >> 11.
 * Register convention: count a1, offsets a2, base s4, x v0, y v1, z a0;
 * outputs v0, v1, a0, a3, s1, s2 (asm clobbers s1/s2; saves the rest).
 * Asm callers keep a1, a2, t0-t7, f12 and f14 live. */
s32 mtx_chain_transform(s32 count, s32 *offsets, u8 *base, s32 x, s32 y, s32 z, MtxChainRegs *regs) {
    s32 *src;
    s32 *dst;
    u16 *b;
    s64 sum;
    s32 i;
    s32 j;
    s32 k;
    s32 rx;
    s32 ry;
    s32 rz;

    if (count == 0) {
        regs->v1 = regs->s1 >> 11;
        regs->a0 = regs->s2 >> 11;
        return regs->s0 >> 11;
    }
    src = (s32 *) (base + *offsets);
    dst = (s32 *) gMtxWork;
    for (i = 0; i < 16; i++) {
        dst[i] = src[i];
    }
    b = (u16 *) (src + 16); /* the asm's a3 ends past the first matrix */
    offsets++;
    count--;
    while (count != 0) {
        b = (u16 *) (base + *offsets);
        for (i = 0; i < 4; i++) {
            for (j = 0; j < 4; j++) {
                sum = 0;
                for (k = 0; k < 4; k++) {
                    sum += (s64) MTX_FIX(gMtxWork, i, k) * (s64) MTX_FIX(b, k, j);
                }
                sum >>= 16;
                gMtxChainTemp[PORT_HALF(16 + i * 4 + j)] = sum;
                gMtxChainTemp[PORT_HALF(i * 4 + j)] = (u32) sum >> 16;
            }
        }
        src = (s32 *) gMtxChainTemp;
        dst = (s32 *) gMtxWork;
        for (i = 0; i < 16; i++) {
            dst[i] = src[i];
        }
        offsets++;
        count--;
    }
    rx = MTX_FIX(gMtxWork, 0, 0) * x + MTX_FIX(gMtxWork, 1, 0) * y + MTX_FIX(gMtxWork, 2, 0) * z +
         (gMtxWork[PORT_HALF(12)] << 16);
    ry = MTX_FIX(gMtxWork, 0, 1) * x + MTX_FIX(gMtxWork, 1, 1) * y + MTX_FIX(gMtxWork, 2, 1) * z +
         MTX_FIX(gMtxWork, 3, 1);
    rz = MTX_FIX(gMtxWork, 0, 2) * x + MTX_FIX(gMtxWork, 1, 2) * y + MTX_FIX(gMtxWork, 2, 2) * z +
         MTX_FIX(gMtxWork, 3, 2);
    regs->a3 = (s32) b;
    regs->s1 = ry;
    regs->s2 = rz;
    regs->v1 = ry >> 11;
    regs->a0 = rz >> 11;
    return rx >> 11;
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/62740/mtx_chain_transform.s")
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
/* (gMoveableTrisEnd and gMoveableTris are declared at the top of the file.) */

/* A vehicle's deck triangles into gMoveableTris (0x802AABE4).
 * Transform desc[0] triangles (3 x {s16 x, y, z}, 0x14 bytes each, after the
 * header {u16 n, u16 count, s32 offsets[count]}) through the matrix chain
 * (mtx_chain_transform with count/offsets/base) into gMoveableTris records tagged
 * `id`: each triangle reuses the next record with that id at or after the
 * current one, else takes a new one at the end (*gMoveableTrisEnd grows). A record
 * holds the three transformed points as words at +0, the source points as
 * halves at +0x24. Returns the remaining count (always 0, the asm's t7) and
 * leaves the source pointer past the last triangle in *vertsOut.
 * Register convention: id t3, desc t4, base s4; mtx_chain_transform's v1/a3/s1/s2
 * pass through `regs`; outputs t7 (return value), s3 (*vertsOut). The asm
 * also clobbers s0 (= the record, which mtx_chain_transform reads only for a count
 * of 0). Asm callers keep f12 and f14 live. */
s32 veh_deck_transform(s32 id, u16 *desc, u8 *base, MtxChainRegs *regs, s16 **vertsOut) {
    s32 count = desc[1];
    s32 *offsets = (s32 *) (desc + 2);
    s16 *v = (s16 *) ((u8 *) desc + count * 4 + 4);
    u8 *end = gMoveableTrisEnd;
    u8 *slot = gMoveableTris;
    s32 n = desc[0];
    s32 k;

    while (n != 0) {
        n--;
        while (slot != end && *(u16 *) (slot + 0x36) != id) {
            slot += 0x38;
        }
        if (slot == end) {
            end += 0x38;
        }
        *(u16 *) (slot + 0x36) = id;
        for (k = 0; k < 3; k++) {
            *(s16 *) (slot + 0x24 + k * 6) = v[k * 3 + 0];
            *(s16 *) (slot + 0x26 + k * 6) = v[k * 3 + 1];
            *(s16 *) (slot + 0x28 + k * 6) = v[k * 3 + 2];
            regs->s0 = (s32) slot;
            *(s32 *) (slot + k * 12) =
                mtx_chain_transform(count, offsets, base, v[k * 3 + 0], v[k * 3 + 1], v[k * 3 + 2], regs);
            *(s32 *) (slot + k * 12 + 4) = regs->v1;
            *(s32 *) (slot + k * 12 + 8) = regs->a0;
        }
        v += 10;
        slot += 0x38;
    }
    gMoveableTrisEnd = end;
    *vertsOut = v;
    return n;
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/62740/veh_deck_transform.s")
#endif


/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
/* C entry of veh_deck_to_local (0x802AACD4).
 * o32 entry for C callers (48D00, 4B5E0): the value pair at (x, z) on the
 * gMoveableTris triangle `id` (veh_deck_to_local) is stored as halfwords to *uOut
 * and *wOut. The asm passes the caller's f24 through to veh_deck_to_local, where
 * it only feeds the FP side results, and doesn't save the f20-f28 that
 * veh_deck_to_local changes (conventions.txt: clobbers); those results are
 * dropped here, so f24 starts as 0. id is used as a full word (the C
 * callers declare it u8). */
void veh_deck_to_local_c(s32 id, s32 x, s32 z, s16 *uOut, s16 *wOut) {
    InterpRegs r;

    r.f24 = 0.0f;
    veh_deck_to_local(id, x, z, &r);
    *uOut = r.t3;
    *wOut = r.t4;
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/62740/veh_deck_to_local_c.s")
#endif

#ifdef NON_MATCHING
void ground_tri_interp(s32 x, s32 z, s32 x0, s32 z0, s32 x1, s32 z1, s32 x2, s32 z2, s32 u1, s32 w1, s32 u2,
                   s32 w2, InterpRegs *r);

#define REC_S32(rec, off) (*(s32 *) ((rec) + (off)))
#define REC_S16(rec, off) (*(s16 *) ((rec) + (off)))

/* Shared body of veh_deck_to_local / veh_deck_to_world: find, from the start of the
 * 0x38-byte records gMoveableTris, the first record with u16 id (+0x36) == id
 * whose triangle contains (x, z) by ground_tri_box_contains (bounding box) and
 * ground_tri_contains (edge sides); the search has no end check (such a record
 * must exist). Each record carries two coordinate sets: s32 words at
 * +0/+8, +0xC/+0x14, +0x18/+0x20 and s16 halves at +0x24/+0x28,
 * +0x2A/+0x2E, +0x30/+0x34. `byHalves` picks the s16 set as the triangle
 * (and the s32 set as the interpolated values), else the reverse. Then
 * ground_tri_interp interpolates the values at (x, z) into r->t3/t4, starting
 * from corner 0's value pair; r's FP fields start as ground_tri_contains's FP
 * results (f12 = pz, f14 = cross bits, f20 = cz, f22 = side, f24 = sideZ,
 * f26 = dz; f24 is also an input, r->f24), which ground_tri_interp overwrites
 * unless the point is a corner. */
static void port_tri_interp(s32 id, s32 x, s32 z, InterpRegs *r, s32 byHalves) {
    u8 *rec = gMoveableTris - 0x38;
    TriSideOut f;
    s32 c[6];
    s32 u[6];
    s32 i;

    f.sideZ = r->f24;
    for (;;) {
        do {
            rec += 0x38;
        } while (*(u16 *) (rec + 0x36) != id);
        if (byHalves) {
            c[0] = REC_S16(rec, 0x24);
            c[1] = REC_S16(rec, 0x28);
            c[2] = REC_S16(rec, 0x2A);
            c[3] = REC_S16(rec, 0x2E);
            c[4] = REC_S16(rec, 0x30);
            c[5] = REC_S16(rec, 0x34);
        } else {
            c[0] = REC_S32(rec, 0x00);
            c[1] = REC_S32(rec, 0x08);
            c[2] = REC_S32(rec, 0x0C);
            c[3] = REC_S32(rec, 0x14);
            c[4] = REC_S32(rec, 0x18);
            c[5] = REC_S32(rec, 0x20);
        }
        if (ground_tri_box_contains(x, z, c[0], c[1], c[2], c[3], c[4], c[5]) == 0) {
            continue;
        }
        if (ground_tri_contains(x, z, c[0], c[1], c[2], c[3], c[4], c[5], &f) != 0) {
            break;
        }
    }
    if (byHalves) {
        u[0] = REC_S32(rec, 0x00);
        u[1] = REC_S32(rec, 0x08);
        u[2] = REC_S32(rec, 0x0C);
        u[3] = REC_S32(rec, 0x14);
        u[4] = REC_S32(rec, 0x18);
        u[5] = REC_S32(rec, 0x20);
    } else {
        u[0] = REC_S16(rec, 0x24);
        u[1] = REC_S16(rec, 0x28);
        u[2] = REC_S16(rec, 0x2A);
        u[3] = REC_S16(rec, 0x2E);
        u[4] = REC_S16(rec, 0x30);
        u[5] = REC_S16(rec, 0x34);
    }
    r->t3 = u[0];
    r->t4 = u[1];
    r->f12 = f.pz;
    {
        /* f14 holds the float's raw bits (a union, not *(s32 *) &f.cross:
         * that read is type punning gcc treats as uninitialised) */
        union {
            f32 f;
            s32 i;
        } bits;
        bits.f = f.cross;
        r->f14 = bits.i;
    }
    r->f20 = f.cz;
    r->f22 = f.side;
    r->f24 = f.sideZ;
    r->f26 = f.dz;
    ground_tri_interp(x, z, c[0], c[1], c[2], c[3], c[4], c[5], u[2], u[3], u[4], u[5], r);
}
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
/* World (x, z) into the deck's model coordinates (0x802AAD0C).
 * Value pair at (x, z) on the gMoveableTris triangle with id `id`, by its s32
 * coordinates; values from its s16 fields (see port_tri_interp).
 * Register convention: id a3, x t0, z t1, f24 in; results t3, t4 and
 * f12-f26 through r. The asm saves everything else it touches (f28 is
 * clobbered by ground_tri_contains). Asm callers keep a3, t0, t1 (and t2) live (a
 * mixed N64 build would need a thunk). */
void veh_deck_to_local(s32 id, s32 x, s32 z, InterpRegs *r) {
    port_tri_interp(id, x, z, r, 0);
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/62740/veh_deck_to_local.s")
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
/* C entry of veh_deck_to_world (0x802AAE1C).
 * As veh_deck_to_local_c with veh_deck_to_world (triangle from the record's s16
 * coordinates, values from its s32 words), stored as words. The C callers
 * declare it s32 and pass s16 coordinates, but ignore the result; the asm
 * doesn't set v0. */
void veh_deck_to_world_c(s32 id, s32 x, s32 z, s32 *uOut, s32 *wOut) {
    InterpRegs r;

    r.f24 = 0.0f;
    veh_deck_to_world(id, x, z, &r);
    *uOut = r.t3;
    *wOut = r.t4;
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/62740/veh_deck_to_world_c.s")
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
/* Deck model coordinates into world (x, z) (0x802AAE54).
 * As veh_deck_to_local with the roles swapped: the triangle is the record's s16
 * coordinates, the values its s32 words. Same register convention; asm
 * callers keep t2 (veh_deck_to_world_c) or a2, a3, t7 (veh_follow_heading) live. */
void veh_deck_to_world(s32 id, s32 x, s32 z, InterpRegs *r) {
    port_tri_interp(id, x, z, r, 1);
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/62740/veh_deck_to_world.s")
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
f32 ground_interp_dx0_zero(s32 t0, s32 s7, s32 v1, s32 a1, s32 t9, s32 t1, s32 a0, f32 *f20Out);
f32 ground_interp_dx12_zero(s32 s7, s32 t0, s32 v0, s32 a0, s32 t1, s32 t9, s32 a1, f32 *f20Out);
f32 ground_interp_dz0_zero(s32 t1, s32 t9, s32 a1, s32 v1, s32 s7, s32 t0, s32 v0, f32 *f20Out);
f32 ground_interp_dz12_zero(s32 t9, s32 t1, s32 a0, s32 v0, s32 t0, s32 s7, s32 v1, f32 *f20Out);

/* (InterpRegs is declared above veh_deck_to_local.) */

/* Interpolates a corner value pair at (x, z) (0x802AAF64).
 * Interpolate the value pair (u, w) at the point (x, z) of the triangle with
 * corners (x0, z0), (x1, z1), (x2, z2) carrying (r->t3, r->t4), (u1, w1),
 * (u2, w2): if the point is corner 0 nothing changes, at corner 1 or 2 its
 * pair is copied. Otherwise the line from corner 0 through the point meets
 * edge 1-2 at parameter t along it (ground_interp_dx0_zero, _dx12_zero,
 * _dz0_zero and _dz12_zero handle the axis-aligned cases, inline 64-bit products and single-precision math the
 * general one) with k the point's position on that line, and
 * u = round((edge value - u0 * k) / (1 - k)), likewise w. Every result
 * (and the FP intermediates the asm callers read) goes to `r`; on the corner
 * paths the FP fields keep their old values.
 * Register convention: x t0, z t1, corners s1/s3, s4/s6, s7/t9, (u1, w1) =
 * t5/t6, (u2, w2) = t7/s0, (u0, w0) = t3/t4 in/out, plus f12, f14, f20, f22,
 * f24, f26 out (in/out). The asm clobbers s2 and uses trapping `sub` for all
 * the differences. A degenerate triangle divides by 0 and the cvt.w.s faults
 * (NaN/inf) in the asm. */
void ground_tri_interp(s32 x, s32 z, s32 x0, s32 z0, s32 x1, s32 z1, s32 x2, s32 z2, s32 u1, s32 w1, s32 u2,
                   s32 w2, InterpRegs *r) {
    s32 dx0;
    s32 dx12;
    s32 dz0;
    s32 dz12;
    f32 k;
    f32 t;
    f32 fdx0;
    f32 den;
    f32 b;
    f32 eu;
    f32 ew;
    f32 omk;

    if (x == x0 && z == z0) {
        return;
    }
    if (x == x1 && z == z1) {
        r->t3 = u1;
        r->t4 = w1;
        return;
    }
    if (x == x2 && z == z2) {
        r->t3 = u2;
        r->t4 = w2;
        return;
    }
    dx0 = x0 - x;
    dx12 = x1 - x2;
    dz0 = z0 - z;
    dz12 = z1 - z2;
    if (dx0 == 0) {
        k = ground_interp_dx0_zero(x, x2, dx12, dz12, z2, z, dz0, &t);
    } else if (dx12 == 0) {
        k = ground_interp_dx12_zero(x2, x, dx0, dz0, z, z2, dz12, &t);
    } else if (dz0 == 0) {
        k = ground_interp_dz0_zero(z, z2, dz12, dx12, x2, x, dx0, &t);
    } else if (dz12 == 0) {
        k = ground_interp_dz12_zero(z2, z, dz0, dx0, x, x2, dx12, &t);
    } else {
        fdx0 = (f32) dx0;
        b = (f32) ((s64) dz12 * dx0);
        den = 1.0f - (f32) ((s64) dx12 * dz0) / b;
        k = (f32) ((s64) dx12 * z) / b - (f32) ((s64) dx12 * z2) / b;
        k = k + (f32) x2 / fdx0;
        k = k - (f32) x / fdx0;
        k = k / den;
        t = (fdx0 * k + (f32) x - (f32) x2) / (f32) dx12;
    }
    omk = 1.0f - k;
    eu = (f32) (u1 - u2) * t + (f32) u2;
    ew = (f32) (w1 - w2) * t + (f32) w2;
    r->t3 = port_cvt_w_s((eu - (f32) r->t3 * k) / omk);
    r->t4 = port_cvt_w_s((ew - (f32) r->t4 * k) / omk);
    r->f12 = omk;
    r->f14 = r->t4;
    r->f20 = t;
    r->f22 = (f32) dx0 * k + (f32) x;
    r->f24 = (f32) dz0 * k + (f32) z;
    r->f26 = (f32) z;
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/62740/ground_tri_interp.s")
#endif

/* ground_interp_dx0_zero / ground_interp_dx12_zero / ground_interp_dz0_zero / ground_interp_dz12_zero: four
 * variants of one line-intersection step used by ground_tri_interp. Each computes
 * a slope k = (f32) (p - q) / (f32) d and then
 * t = ((f32) m * k + (f32) c - (f32) e) / (f32) n, returning k and t in f10/f20
 * (which one is which differs per variant). The C versions return f10 and
 * store f20 through `f20Out`; register mappings are in conventions.txt. The
 * asm saves every integer register it reads and clobbers f0 and f2; asm
 * callers keep a0, t0, t1 and t3-t7 live. `p - q` is a trapping `sub`. */

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
/* f20 = (t0 - s7) / v1; f10 = (a1 * f20 + t9 - t1) / a0. */
f32 ground_interp_dx0_zero(s32 t0, s32 s7, s32 v1, s32 a1, s32 t9, s32 t1, s32 a0, f32 *f20Out) {
    f32 k = (f32) (t0 - s7) / (f32) v1;

    *f20Out = k;
    return ((f32) a1 * k + (f32) t9 - (f32) t1) / (f32) a0;
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/62740/ground_interp_dx0_zero.s")
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
/* f10 = (s7 - t0) / v0; f20 = (a0 * f10 + t1 - t9) / a1. */
f32 ground_interp_dx12_zero(s32 s7, s32 t0, s32 v0, s32 a0, s32 t1, s32 t9, s32 a1, f32 *f20Out) {
    f32 k = (f32) (s7 - t0) / (f32) v0;

    *f20Out = ((f32) a0 * k + (f32) t1 - (f32) t9) / (f32) a1;
    return k;
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/62740/ground_interp_dx12_zero.s")
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
/* f20 = (t1 - t9) / a1; f10 = (v1 * f20 + s7 - t0) / v0. */
f32 ground_interp_dz0_zero(s32 t1, s32 t9, s32 a1, s32 v1, s32 s7, s32 t0, s32 v0, f32 *f20Out) {
    f32 k = (f32) (t1 - t9) / (f32) a1;

    *f20Out = k;
    return ((f32) v1 * k + (f32) s7 - (f32) t0) / (f32) v0;
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/62740/ground_interp_dz0_zero.s")
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
/* f10 = (t9 - t1) / a0; f20 = (v0 * f10 + t0 - s7) / v1. */
f32 ground_interp_dz12_zero(s32 t9, s32 t1, s32 a0, s32 v0, s32 t0, s32 s7, s32 v1, f32 *f20Out) {
    f32 k = (f32) (t9 - t1) / (f32) a0;

    *f20Out = ((f32) v0 * k + (f32) t0 - (f32) s7) / (f32) v1;
    return k;
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/62740/ground_interp_dz12_zero.s")
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
/* 4-byte entries {u8 id, u8 flag, u8 pad[2]}, terminated by a 0xFFFFFFFF word. */

/* Vehicle id stands on another (0x802AB3C0).
 * Looks up `id` in the gStandOn list. Returns 1 if the first entry with
 * that id has a nonzero flag byte, 0 if the flag is 0 or the id is absent.
 * (The asm also saves/restores t0/t1; its only caller is C, 23C20.) */
s32 veh_is_riding(s32 id) {
    u8 *entry;

    for (entry = gStandOn; *(s32 *) entry != -1; entry += 4) {
        if (entry[0] == id) {
            return entry[1] != 0;
        }
    }
    return 0;
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/62740/veh_is_riding.s")
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
/* Vehicle a stands on vehicle b (0x802AB41C).
 * 1 if some gStandOn entry (scanning to the -1 terminator) has byte 1 ==
 * key1 and byte 0 == key0, else 0.
 * Register convention: key0 in t4, key1 in t8, result in a2 (the asm saves
 * t0, t1). Asm callers keep a0, a1, a3, t2-t8, f12 and f14 live. */
s32 veh_rides_on(s32 key0, s32 key1) {
    u8 *entry;

    for (entry = gStandOn; *(s32 *) entry != -1; entry += 4) {
        if (entry[1] == key1 && entry[0] == key0) {
            return 1;
        }
    }
    return 0;
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/62740/veh_rides_on.s")
#endif

#ifdef NON_MATCHING
/* Per-type hooks of the gStandOn tree walks below (vehicle files). */
void veh_attach_riders_walk(s32 id, InterpRegs *r);
void veh_move_riders_walk(s32 id, s32 *s3, InterpRegs *r);
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
/* The riders of id record their deck positions (0x802AB478).
 * Entry from C (00000.c veh_apply_switch, which declares a u8 parameter; the asm
 * moves all of a0 to s2): veh_attach_riders_walk(id). The asm saves every callee-saved
 * register around the call, so the walk's register results are dropped; its
 * FP state starts as whatever the C caller left in f20-f26 (f24 is an input
 * of the per-type hooks, where it only feeds FP side results that nothing
 * stores): 0 here. */
void veh_attach_riders(s32 id) {
    InterpRegs r;

    r.f20 = 0.0f;
    r.f22 = 0.0f;
    r.f24 = 0.0f;
    r.f26 = 0.0f;
    veh_attach_riders_walk(id, &r);
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/62740/veh_attach_riders.s")
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
/* The recursive walk of veh_attach_riders (0x802AB50C).
 * Walk the gStandOn tree (4-byte entries {u8 type, u8 parent, ..}, scanned
 * from the start to the -1 word) below node `id`: for every entry whose
 * parent byte is `id`, run its type's hook with that parent id (3:
 * skyfall_ride_attach, 4: ramdozer_ride_attach, 5: backlash_ride_attach, 8: amdream_ride_attach, 9:
 * jbomb_ride_attach, 0xA: ballista_ride_attach, 0xD: police_ride_attach, 0xE: ateam_ride_attach,
 * 0xF: starski_ride_attach; their results are ignored) and then recurse on the type
 * byte as the new id. Type 0 and other types are skipped without recursing.
 * The type is the byte read before the hook (the asm keeps it in t1, which
 * the hooks preserve; they may rewrite gStandOn). An entry whose type
 * equals its parent recurses forever in the asm too.
 * Register convention: id in s2; the hooks' FP state chains through r (f24
 * in, f20-f26 out; conventions.txt). The asm saves a2, a3, t0, t1 and s2 and
 * leaves the hooks' gp, s4 and f28 changed. */
void veh_attach_riders_walk(s32 id, InterpRegs *r) {
    u8 *e;
    s32 type;

    for (e = gStandOn; *(s32 *) e != -1; e += 4) {
        if (e[1] != id) {
            continue;
        }
        type = e[0];
        switch (type) {
            case VEH_SKYFALL:
                skyfall_ride_attach(e[1], r);
                break;
            case VEH_BACKLASH:
                backlash_ride_attach(e[1], r);
                break;
            case VEH_RAMDOZER:
                ramdozer_ride_attach(e[1], r);
                break;
            case VEH_AMDREAM:
                amdream_ride_attach(e[1], r);
                break;
            case VEH_POLICE:
                police_ride_attach(e[1], r);
                break;
            case VEH_ATEAM:
                ateam_ride_attach(e[1], r);
                break;
            case VEH_STARSKI:
                starski_ride_attach(e[1], r);
                break;
            case VEH_JBOMB:
                jbomb_ride_attach(e[1], r);
                break;
            case VEH_BALLISTA:
                ballista_ride_attach(e[1], r);
                break;
            default: /* 0 and unknown types */
                continue;
        }
        veh_attach_riders_walk(type, r);
    }
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/62740/veh_attach_riders_walk.s")
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
/* The riders of id follow it (0x802AB670).
 * Entry from C (00000.c veh_update_riders): veh_move_riders_walk(id). The asm saves
 * every callee-saved register (gp and fp too) around the call. The walk's s3
 * and FP state start as whatever the C caller left in s3 and f20-f26; the
 * hooks only pass s3 through (jbomb_ride_follow / ballista_ride_follow hand it to
 * veh_ground_contact's in/out slot result) and f24 only feeds FP side results,
 * none of which is stored: 0 here. */
void veh_move_riders(s32 id) {
    InterpRegs r;
    s32 s3 = 0;

    r.f20 = 0.0f;
    r.f22 = 0.0f;
    r.f24 = 0.0f;
    r.f26 = 0.0f;
    veh_move_riders_walk(id, &s3, &r);
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/62740/veh_move_riders.s")
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
/* The recursive walk of veh_move_riders (0x802AB714).
 * As veh_attach_riders_walk with the second set of hooks (3: skyfall_ride_follow, 4:
 * ramdozer_ride_follow, 5: backlash_ride_follow, 8: amdream_ride_follow, 9: jbomb_ride_follow, 0xA:
 * ballista_ride_follow, 0xD: police_ride_follow, 0xE: ateam_ride_follow, 0xF: starski_ride_follow),
 * and entries whose parent byte is 0 are skipped (so id 0 matches nothing).
 * Register convention: id in a3; s3 in/out (*s3: the hooks' s3 result, or
 * their in/out s3) and the FP state through r (f24 in; f20-f26 out). The
 * asm saves a3, t0, t1, t2; the hooks leave s0-s2, s4-s7, fp, gp and f28
 * changed, and amdream_ride_follow also s3 (not modelled: s3 only passes through
 * to veh_ground_contact's result, which nothing stores). */
void veh_move_riders_walk(s32 id, s32 *s3, InterpRegs *r) {
    u8 *e;
    s32 type;

    for (e = gStandOn; *(s32 *) e != -1; e += 4) {
        if (e[1] != id || e[1] == 0) {
            continue;
        }
        type = e[0];
        switch (type) {
            case VEH_SKYFALL:
                *s3 = skyfall_ride_follow(id, r);
                break;
            case VEH_BACKLASH:
                *s3 = backlash_ride_follow(id, (TriSideOut *) &r->f12);
                break;
            case VEH_RAMDOZER:
                *s3 = ramdozer_ride_follow(id, r);
                break;
            case VEH_AMDREAM:
                amdream_ride_follow(id, &r->f24);
                break;
            case VEH_POLICE:
                *s3 = police_ride_follow(id, (TriSideOut *) &r->f12);
                break;
            case VEH_ATEAM:
                *s3 = ateam_ride_follow(id, (TriSideOut *) &r->f12);
                break;
            case VEH_STARSKI:
                *s3 = starski_ride_follow(id, r);
                break;
            case VEH_JBOMB:
                jbomb_ride_follow(id, s3, r);
                break;
            case VEH_BALLISTA:
                ballista_ride_follow(id, s3, r);
                break;
            default: /* 0 and unknown types */
                continue;
        }
        veh_move_riders_walk(type, s3, r);
    }
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/62740/veh_move_riders_walk.s")
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
s32 veh_has_partial_rider_walk(s32 id);

/* Something straddles vehicle id (0x802AB878).
 * Wrapper: returns veh_has_partial_rider_walk(id). 00000.c declares it taking a u8; the
 * asm passes all of a0 through, so this takes s32. */
s32 veh_has_partial_rider(s32 id) {
    return veh_has_partial_rider_walk(id);
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/62740/veh_has_partial_rider.s")
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
/* (gStandOn is declared above, with veh_is_riding.) */

/* The recursive walk of veh_has_partial_rider (0x802AB8D8).
 * Returns 1 if `id` is in bytes 1-3 of a gStandOn entry, scanning up to the
 * -1 terminator; an entry whose bytes 1-3 all equal `id` instead recurses on
 * its byte 0 (when nonzero), and counts only if that finds a match.
 * Register convention: the asm takes id in s2 and returns the result in t0,
 * keeping t1-t4 and s3 but leaving s2 changed on a recursive hit
 * (tools_port/conventions.txt); this C is plain o32. The asm recursion
 * relies on t1 surviving its own call, which C handles itself. */
s32 veh_has_partial_rider_walk(s32 id) {
    u8 *entry;

    for (entry = gStandOn; *(s32 *) entry != -1; entry += 4) {
        if (entry[1] == id) {
            if (entry[2] != id || entry[3] != id) {
                return 1;
            }
            if (entry[0] != 0 && veh_has_partial_rider_walk(entry[0]) == 1) {
                return 1;
            }
        } else if (entry[2] == id || entry[3] == id) {
            return 1;
        }
    }
    return 0;
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/62740/veh_has_partial_rider_walk.s")
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
extern s16 gVehWheelHeading; /* scratch heading for veh_wheel_offset */

#define WRAP_12(v) ((v) >= 0x1000 ? (v) - 0xFFF : (v))
#define ABS_B9A4(v) ((v) < 0 ? -(v) : (v))

/* A rider's heading from its two tracked deck points (0x802AB9A4).
 * Steer toward a point on triangle `id`: B = veh_deck_to_world(id, x1, z1) and
 * A = veh_deck_to_world(id, x2, z2) (interpolated value pairs; r's f24 chains
 * through both). d = veh_follow_angle(A, offset of tbl[0] rotated by *angle, B);
 * when d > 0x400 it is retried with the heading turned by 0x400 (d += 0x400)
 * and, if then > 0x800, by 0x800 (d += 0x800), the turned heading going
 * through gVehWheelHeading (wrapped as h >= 0x1000 ? h - 0xFFF : h). Then the two
 * headings *angle + d (wrapped the same way) and *angle - d (+ 0xFFF when
 * negative) are tried: each rotated offset is added to B and its distance
 * |B + off - A| (|dx| + |dz|) taken; returns the + heading when its distance
 * is strictly smaller, else the - heading, and the - heading's distance in
 * *s3Out. gVehWheelHeading is left = the - heading. The asm's add/sub/neg trap.
 * Register convention: tbl v1, x1 t0, angle a2, id a3, z1 t1, x2 s1, z2 s2;
 * results t0 (return value) and s3 (*s3Out); r: f24 in, f24/f26 out (r's
 * other fields are scratch). The asm clobbers s0-s2, s4-s6, fp, f20, f22;
 * asm callers keep a3 live. */
s32 veh_follow_heading(s16 *tbl, s32 x1, u16 *angle, s32 id, s32 z1, s32 x2, s32 z2, s32 *s3Out, InterpRegs *r) {
    s32 ax;
    s32 az;
    s32 bx;
    s32 bz;
    s32 dx;
    s32 dz;
    s32 d;
    s32 h;
    s32 a0;
    s32 plus;
    s32 minus;
    s32 d1;
    s32 d2;
    s32 e1;
    s32 e2;

    veh_deck_to_world(id, x1, z1, r);
    bx = r->t3;
    bz = r->t4;
    veh_deck_to_world(id, x2, z2, r);
    ax = r->t3;
    az = r->t4;
    dx = veh_wheel_offset(0, tbl, (s16 *) angle, &dz);
    d = veh_follow_angle(ax, az, dx, dz, bx, bz);
    if (d >= 0x401) {
        h = *angle + 0x400;
        gVehWheelHeading = WRAP_12(h);
        dx = veh_wheel_offset(0, tbl, &gVehWheelHeading, &dz);
        d = veh_follow_angle(ax, az, dx, dz, bx, bz) + 0x400;
        if (d >= 0x801) {
            h = *angle + 0x800;
            gVehWheelHeading = WRAP_12(h);
            dx = veh_wheel_offset(0, tbl, &gVehWheelHeading, &dz);
            d = veh_follow_angle(ax, az, dx, dz, bx, bz) + 0x800;
        }
    }
    a0 = *angle;
    plus = a0 + d;
    plus = WRAP_12(plus);
    gVehWheelHeading = plus;
    dx = veh_wheel_offset(0, tbl, &gVehWheelHeading, &dz);
    minus = a0 - d;
    d1 = dx + bx;
    d2 = dz + bz;
    if (minus < 0) {
        minus += 0xFFF;
    }
    gVehWheelHeading = minus;
    dx = veh_wheel_offset(0, tbl, &gVehWheelHeading, &dz);
    e1 = dx + bx - ax;
    e2 = dz + bz - az;
    d1 -= ax;
    d2 -= az;
    d1 = ABS_B9A4(d1) + ABS_B9A4(d2);
    e1 = ABS_B9A4(e1) + ABS_B9A4(e2);
    *s3Out = e1;
    if (!(d1 < e1)) {
        plus = minus;
    }
    return plus;
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/62740/veh_follow_heading.s")
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
float sqrtf(float);
#pragma intrinsic(sqrtf)

/* The angle helper of veh_follow_heading (arcsine) (0x802ABB1C).
 * Angle at point A = (ax, az) between A->B (B = (bx, bz)) and the offset
 * (dx, dz) from B: d0 = |A - B|, d1 = |B + (dx, dz) - A| (64-bit squares,
 * cvt.s.l, sqrt.s), sine = round(d1 / 2 / d0 * 65536) clamped to 0xFFFF,
 * then math_asin (arcsine) >> 3 (logical). The differences and sums
 * wrap at 32 bits. d0 = 0 makes the rounding invalid (keep A != B).
 * Register convention: ax t3, az t4, dx t5, dz t6, bx t7, bz s0; result in
 * s6; the asm restores v1 and clobbers s1-s4 and fp (conventions.txt). Asm
 * callers keep a2, t3-t7 live (a mixed N64 build would need a thunk; the
 * native port won't). */
s32 veh_follow_angle(s32 ax, s32 az, s32 dx, s32 dz, s32 bx, s32 bz) {
    s64 u = (s32) ((u32) ax - (u32) bx);
    s64 w = (s32) ((u32) az - (u32) bz);
    s64 p = (s32) ((u32) bx + (u32) dx - (u32) ax);
    s64 q = (s32) ((u32) bz + (u32) dz - (u32) az);
    f32 d0 = sqrtf((f32) (u * u + w * w));
    f32 d1 = sqrtf((f32) (p * p + q * q));
    s32 sine = port_cvt_w_s(d1 / 2.0f / d0 * 65536.0f);

    if (sine >= 0x10000) {
        sine = 0xFFFF;
    }
    return (u32) math_asin(sine) >> 3;
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/62740/veh_follow_angle.s")
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
/* A model's points into gVehPoints (0x802ABBEC).
 * Transform a list of points into gVehPoints records tagged `id`: start at the
 * first record with id byte +0xC == id (or the -1 end marker), then for each
 * entry {s16 x, y, z; u16 count; s32 offsets[count]} between p and end
 * (exclusive) store mtx_chain_transform(count, offsets, base, x, y, z) and its y/z
 * results as the record's three words, tag it, and move to the next record.
 * Register convention: id t0, p t1, end t2, base s4; the mtx_chain_transform
 * registers v1, a0, a3, s1, s2 (and s0 for a count of 0) pass through
 * `regs` (asm callers read them after the call). The asm saves a1, a2, t3,
 * t4; asm callers keep t0, t2, t4, t6, t7, f12 and f14 live. */
void veh_points_transform(s32 id, s16 *p, s16 *end, u8 *base, MtxChainRegs *regs) {
    u8 *rec = gVehPoints;
    s32 count;

    while (*(s32 *) rec != -1 && rec[0xC] != id) {
        rec += 0x10;
    }
    while (p != end) {
        count = (u16) p[3];
        *(s32 *) (rec + 0) = mtx_chain_transform(count, (s32 *) (p + 4), base, p[0], p[1], p[2], regs);
        *(s32 *) (rec + 4) = regs->v1;
        *(s32 *) (rec + 8) = regs->a0;
        rec[0xC] = id;
        p = (s16 *) ((u8 *) p + count * 4 + 8);
        rec += 0x10;
    }
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/62740/veh_points_transform.s")
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
/* Point record n of vehicle id (0x802ABC88).
 * Find the first 0x10-byte gVehPoints record whose byte +0xC is `id` (no
 * terminator: one must exist), step (n - 1) records further, and return the
 * record through `recOut` and (n - 1) * 0x10 as the result.
 * Register convention: id in v0, n in v1; outputs v1 (here the return value)
 * and a0 (*recOut). The asm saves a1. Asm callers keep a1-a3, t0, t3, t6,
 * t7, f12 and f14 live. */
s32 veh_find_point(s32 id, s32 n, u8 **recOut) {
    u8 *rec = gVehPoints;

    while (rec[0xC] != id) {
        rec += 0x10;
    }
    n--;
    if (n != 0) {
        n *= 0x10;
        rec += n;
    }
    *recOut = rec;
    return n;
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/62740/veh_find_point.s")
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
f64 sqrt(f64);
#pragma intrinsic(sqrt)

/* Rounded 3-D distance (0x802ABCDC).
 * Rounded 3-D distance between (ax, ay, az) and (bx, by, bz): each difference
 * is a wrapping 32-bit subtraction, the squares and their sum are 64-bit
 * (wrapping), then sqrt in double and round to nearest even (the asm's
 * cvt.d.l / sqrt.d / cvt.l.d under the default FCSR); the low 32 bits are
 * returned. A sum that wraps negative gives NaN, where the VR4300 traps in
 * cvt.l.d. The Python model tools_port/models/math_dist3.py does the same.
 * Register convention: inputs t3, t4, t5, t6, t7, s0; result in s1 (the asm
 * restores t6, t7, s0). Asm callers keep a0-a3, t0, t1, t3-t7, f12 and f14
 * live. */
s32 math_dist3(s32 ax, s32 ay, s32 az, s32 bx, s32 by, s32 bz) {
    s64 dx = (s32) ((u32) bx - (u32) ax);
    s64 dy = (s32) ((u32) by - (u32) ay);
    s64 dz = (s32) ((u32) bz - (u32) az);
    f64 d = sqrt((f64) (dx * dx + dy * dy + dz * dz));
    u32 r = d; /* d < 2^32: truncation fits */
    f64 frac = d - r;

    if (frac > 0.5 || (frac == 0.5 && (r & 1))) {
        r++;
    }
    return r;
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/62740/math_dist3.s")
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING


/* A vehicle's env alpha from the alpha zones (0x802ABD54).
 * Find the first zone (gAlphaZones .. *gAlphaZonesEnd, 0x24 bytes) within whose
 * radius the point (x, y, z) lies (distance by math_dist3), whose byte
 * +0x12 is set and whose id list (byte +0x13 = length, ids from +0x15)
 * contains `id`. Its level: with byte +0x14 == 0, 0xFF if byte +0x10 == 1, else
 * fading from 0xFF at the centre to gDefaultVehAlpha at the radius; with byte +0x14
 * = L != 0, L if byte +0x10 == 1, else fading from L to gDefaultVehAlpha. No zone:
 * gDefaultVehAlpha. The level goes to +0x60 of the gVehicles record whose word
 * +0x5C is `id` (one must exist). Returns *gAlphaZonesEnd.
 * Register convention: id a3, point t3/t4/t5; outputs v1 (return value) and
 * t6, t7, s0-s4 (through `r`, unchanged if no zone is looked at). The asm
 * clobbers s5, s6; asm callers keep a3 live. The fades divide by the radius
 * (unsigned; a zero radius traps). */
s32 veh_zone_alpha(s32 id, s32 x, s32 y, s32 z, ZoneScanRegs *r) {
    u8 *end = gAlphaZonesEnd;
    u8 *zone = gAlphaZones;
    s32 t6 = r->t6;
    s32 t7 = r->t7;
    s32 s0 = r->s0;
    s32 s1 = r->s1;
    s32 s2 = r->s2;
    s32 s3 = r->s3;
    s32 s4 = r->s4;
    s32 level;
    s32 k;
    u8 *rec;

    for (; zone != end; zone += 0x24) {
        t6 = ((s32 *) zone)[0];
        t7 = ((s32 *) zone)[1];
        s0 = ((s32 *) zone)[2];
        s1 = math_dist3(x, y, z, t6, t7, s0);
        s2 = ((s32 *) zone)[3];
        if (s2 < s1) {
            continue;
        }
        s3 = zone[0x12];
        if (s3 == 0) {
            continue;
        }
        s3 = zone[0x13];
        s4 = (s32) (zone + 0x15);
        while (s3 != 0) {
            if (id == *(u8 *) N64_IPTR(s4)) {
                break;
            }
            s4++;
            s3--;
        }
        if (s3 == 0) {
            continue;
        }
        s3 = zone[0x14];
        if (s3 == 0) {
            s3 = zone[0x10];
            if (s3 == 1) {
                level = 0xFF;
            } else {
                k = 0xFF - gDefaultVehAlpha;
                s1 = (u32) (k * s1) / (u32) s2;
                level = k - s1 + gDefaultVehAlpha;
            }
        } else {
            s4 = zone[0x10];
            if (s4 == 1) {
                level = s3;
            } else {
                k = gDefaultVehAlpha - s3;
                s1 = (u32) (k * s1) / (u32) s2;
                level = s3 + s1;
            }
        }
        goto found;
    }
    level = gDefaultVehAlpha;
found:
    rec = gVehicles;
    while (*(s32 *) (rec + 0x5C) != id) {
        rec += 0x74;
    }
    *(s32 *) (rec + 0x60) = level;
    r->t6 = t6;
    r->t7 = t7;
    r->s0 = s0;
    r->s1 = s1;
    r->s2 = s2;
    r->s3 = s3;
    r->s4 = s4;
    return (s32) end;
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/62740/veh_zone_alpha.s")
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING
s32 ground_scan_object(s32 x, s32 z, s32 y, u8 *obj, s32 *found, TriSideOut *f);

/* (x, z, y) above an object's own floor (0x802ABEDC).
 * 1 if (x, z) at height y is over some object's own ground: for each object
 * record gObjects .. gObjectsEnd (read once, walked with `!=`), its info's
 * s16 box at info + info->unk20 (x0, z0, x1, z1, << 5) is tested with
 * ground_tri_box_contains as the triangle (x0, z0), (x1, z1), (x1, z0) and, failing
 * that, (x0, z0), (x1, z1), (x0, z1); on a hit ground_scan_object(x, z, y, info)
 * scans its triangles and sets the found flag (and gObjectFloorY). Returns the
 * flag (13A70.c declares the function u8; the asm returns the word).
 * The asm leaves f20-f28 as ground_scan_object leaves them (FP results of the
 * scans, passing the caller's f24 in) although its only caller is C; the C
 * keeps them. Its v1 is ground_scan_object's last result (unused by the caller). */
s32 ground_over_object(s32 x, s32 y, s32 z) {
    u8 *end = gObjectsEnd;
    u8 *e;
    s32 found = 0;
    TriSideOut f;

    f.sideZ = 0.0f;
    for (e = gObjects; e != end; e += 0xFC) {
        u8 *info = *(u8 * N64P *) e;
        s16 *box = (s16 *) (info + *(s32 *) (info + 0x20));
        s32 x0 = box[0] << 5;
        s32 z0 = box[1] << 5;
        s32 x1 = box[2] << 5;
        s32 z1 = box[3] << 5;

        if (ground_tri_box_contains(x, z, x0, z0, x1, z1, x1, z0) != 0
            || ground_tri_box_contains(x, z, x0, z0, x1, z1, x0, z1) != 0) {
            ground_scan_object(x, z, y, info, &found, &f);
        }
    }
    return found;
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/62740/ground_over_object.s")
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING

/* The highest floor below in one object's triangles (0x802ABFC8).
 * Highest-below ground in an object's own triangle list (obj + its words
 * +0x24 .. +0x28; 0x14-byte triangles as in ground_scan_grid, skipped when byte
 * +0x13 is set): among triangles containing (x, z) whose height h <= y, the
 * one with the smallest y - h (the later one on a tie: the asm skips only when
 * best < d). Each such hit sets *found = 1 and gObjectFloorY = h. Returns the
 * best y - h (0x5F5E0FF if none).
 * Register convention: x t0, z t1, y t2, obj s0; results v1 (return value),
 * a1 (*found, in/out) and f24/f26 through f (in/out). The asm clobbers s1-s7,
 * t8, t9, fp, f20, f22 and f28; asm callers keep t0-t2, t6 and t7 live. */
s32 ground_scan_object(s32 x, s32 z, s32 y, u8 *obj, s32 *found, TriSideOut *f) {
    u8 *rec = obj + *(s32 *) (obj + 0x24);
    u8 *end = obj + *(s32 *) (obj + 0x28);
    u32 bestD = 0x5F5E0FF;
    s32 v[9];
    s32 h;
    s32 d;
    s32 a3;
    s32 t6;

    for (; rec != end; rec += 0x14) {
        if (rec[0x13] != 0) {
            continue;
        }
        PORT_TRI_S16(v, rec);
        if (!port_tri_height(x, z, v, f, &h, &a3, &t6)) {
            continue;
        }
        d = y - h;
        if (d < 0) {
            continue;
        }
        if (bestD < (u32) d) {
            continue;
        }
        *found = 1;
        gObjectFloorY = h;
        bestD = d;
    }
    return bestD;
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/62740/ground_scan_object.s")
#endif

/* Uses the sd-$ra frame convention (see hd_code/77E20.c's file-level note and the project skill file) - permanently GLOBAL_ASM. */
#ifdef NON_MATCHING


/* Scans the level header's guard triangles (+0x24..+0x28: J-Bomb anti-clip, the Ironstone Mine crane area)
 * for the one nearest y at (x, z); returns |y - h| and sets the found flag and gGuardTrisBestY (0x802AC0BC).
 * ground_scan_part_tris's nearest-height scan over gGuardTris .. gGuardTrisEnd: each
 * nearer-or-equal hit sets the found flag and gGuardTrisBestY = h. Returns the
 * best |y - h| (0x5F5E0FF if none).
 * Register convention: x t0, z t1, y t2; results v1 (return value), a1, a3,
 * t6, t7, fp, s1-s4 through r (a3, t6, s1-s4 in/out) and the FP results
 * through f (in/out). The asm clobbers s5-s7, t8, t9 and f28; `neg` traps on
 * 0x80000000. */
s32 ground_scan_guard_tris(s32 x, s32 z, s32 y, TriSideOut *f, TriScanRegs *r) {
    u8 *rec = gGuardTris;
    u8 *end = gGuardTrisEnd;
    u32 bestD = 0x5F5E0FF;
    s32 v[9];
    s32 h;
    s32 d;

    r->a1 = 0;
    for (; rec != end; rec += 0x14) {
        PORT_TRI_S16(v, rec);
        r->s1 = v[0];
        r->s2 = v[1];
        r->s3 = v[2];
        r->s4 = v[3];
        if (!port_tri_height(x, z, v, f, &h, &r->a3, &r->t6)) {
            continue;
        }
        d = y - h;
        if (d < 0) {
            d = -d;
        }
        if (bestD < (u32) d) {
            continue;
        }
        r->a1 = 1;
        gGuardTrisBestY = h;
        bestD = d;
    }
    r->t7 = (s32) end;
    r->fp = (s32) end;
    return bestD;
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/62740/ground_scan_guard_tris.s")
#endif
