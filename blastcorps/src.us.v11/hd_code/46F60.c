#include "common.h"
#include <ultra64.h>
#include "game/game.h"

/* Analog-stick steering helpers: stick angle/magnitude to steering and
 * direction-button bits. */

#define ABS(x) ((x) > 0 ? (x) : -(x))
#define SIGN(x) ((x) >= 0 ? 1 : -1)

f32 sqrtf(f32);

extern s8 gSteerReverse;
extern s16 gSteerTurnTarget;

u16 cont_steer_toward(s16 heading, s16 target, s8 *reverse, u8 unused);
f32 math_asin_ratio_deg(f32 a, f32 b);

/* Resets the analog steering state: the turn-around latch (gSteerTurnAround) and the heading snap (gSteerHeadingSnap)
 * (0x8028B720). */
void cont_steer_reset(void) {
    gSteerTurnAround = 0;
    gSteerHeadingSnap = 0;
}

/* Analog control method: the stick points where to go, relative to the camera; returns a left/right steer of
 * -80/0/+80 in *x and the stick's magnitude in *y, snapping the heading once within one frame's turn
 * (0x8028B734). */
void cont_stick_to_steer(s8 *x, s8 *y, u8 method) {
    s8 stickX;
    s8 stickY;
    s16 stickAngle; /* the stick's direction, camera-relative, 0x10000 = one turn */
    s16 stickMag;
    s16 heading;    /* gSteerHeading in the same units */
    u16 press;      /* R_JPAD / L_JPAD from cont_steer_toward */
    s16 remaining;  /* of the turn around */

    heading = -gSteerHeading * 16 + 0x7FF0;
    stickX = *x;
    stickY = *y;
    stickAngle = math_angle_2d_deg(0, 0, stickX, -stickY) * 65536.0 / 360.0 + 32768.0 +
                 (s16) ((360.0 - (gCamHeading + 180.0)) / 360.0 * 65536.0);
    stickMag = sqrtf(stickX * stickX + stickY * stickY);
    *y = stickMag;
    gSteerReverse = 0;
    press = 0;
    if (gSteerTurnAround == 1) {
        remaining = gSteerTurnTarget - heading;
        if (ABS(remaining) <= ABS(gSteerRate * 16)) {
            gSteerTurnAround = 0;
        }
        press = L_JPAD;
    } else {
        if (*y >= 0xB) {
            press = cont_steer_toward(heading, stickAngle, &gSteerReverse, method);
        }
        if (gSteerReverse != 0 && gPlayerSpeed < 0x28) {
            gSteerTurnTarget = stickAngle;
            gSteerTurnAround = 1;
        } else if (gSteerHeadingSnap != 0) {
            gSteerSnapAngle = 0x7FF - stickAngle / 16;
            *x = 0;
            return;
        }
    }
    *x = 0;
    if (press == L_JPAD) {
        *x = -0x50;
    }
    if (press == R_JPAD) {
        *x = 0x50;
    }
}

/* Returns R_JPAD or L_JPAD to turn the heading toward the target (sets the heading snap when close); if the
 * target is far behind and the speed is below 40, flips the reverse flag and returns 0. arg 4 is unused
 * (0x8028BA1C). */
u16 cont_steer_toward(s16 heading, s16 target, s8 *reverse, u8 unused) {
    s16 diff;
    u16 behind;   /* past this |diff| the target counts as behind: half a turn less half the cone */
    s16 cone;     /* the forward cone, degrees: 120 standing, 90 from |speed| 200 */
    u16 minCone;

    minCone = 0x5A;
    cone = 0x78 - ABS(gLastSpeed) * (0x78 - minCone) / 200;
    behind = 0x8000 - ((cone < minCone ? minCone : cone) << 16) / 720;
    if (*reverse != 0) {
        heading += 0x8000;
    }
    diff = target - heading;
    gContSteerError = diff;
    if (ABS(diff) <= ABS(gSteerRate * 16)) {
        gSteerHeadingSnap = 1;
    }
    if (behind < ABS(diff) && gPlayerSpeed < 0x28) {
        *reverse ^= -1;
        return 0;
    }
    if (SIGN(gSteerRate) * diff > 0) {
        return R_JPAD;
    }
    return L_JPAD;
}

/* Angle in degrees of the vector from (x0, y0) to (x1, y1): -y is 0, +x 90, +y 180, -x 270 (0x8028BBF4),
 * per quadrant. */
f32 math_angle_2d_deg(s16 x0, s16 y0, s16 x1, s16 y1) {
#ifdef NON_MATCHING
    f32 angle = 0.0f; /* every quadrant test sets it; never read unset */
#else
    f32 angle;
#endif

    if (x1 >= x0 && y1 >= y0) {
        angle = math_asin_ratio_deg(y1 - y0, x1 - x0) + 90.0f;
    } else if (x1 >= x0 && y1 < y0) {
        angle = 90.0f - math_asin_ratio_deg(y0 - y1, x1 - x0);
    } else if (x1 < x0 && y1 < y0) {
        angle = math_asin_ratio_deg(y0 - y1, x0 - x1) + 270.0f;
    } else if (x1 < x0 && y1 >= y0) {
        angle = 270.0f - math_asin_ratio_deg(y1 - y0, x0 - x1);
    }
    return angle;
}

/* asin(a / sqrt(a*a + b*b)) in degrees through math_asin_c, the fixed-point inverse-trig lookup; 0 when both
 * are 0 (0x8028BD88). */
f32 math_asin_ratio_deg(f32 a, f32 b) {
    s32 asinFixed;
    f32 ratio;

    if (a == 0.0 && b == 0.0) {
        return 0.0f;
    }
    ratio = a / sqrtf(a * a + b * b);
    asinFixed = math_asin_c(ratio * 65536.0);
    return ((f32) asinFixed / 65536.0) * 360.0;
}

/* Unused: stick past +-50 presses D-pad left/right and A (y < -50) or B (y > 50); Z also presses L and R
 * (0x8028BE70). */
void cont_stick_to_buttons_inv_y(u16 *buttons, s8 x, s8 y) {
    if (!(*buttons & L_JPAD) && (x < -0x32)) {
        *buttons |= L_JPAD;
    }
    if (!(*buttons & R_JPAD) && (x >= 0x33)) {
        *buttons |= R_JPAD;
    }
    if (!(*buttons & A_BUTTON) && (y < -0x32)) {
        *buttons |= A_BUTTON;
    }
    if (!(*buttons & B_BUTTON) && (y >= 0x33)) {
        *buttons |= B_BUTTON;
    }
    if (*buttons & Z_TRIG) {
        *buttons |= L_TRIG | R_TRIG;
    }
}

/* Unused analog-to-digital control: the stick's magnitude and its direction against the heading press D-pad
 * left/right (cont_steer_toward) and A, or B when reversing; Z also presses L and R (0x8028BF34). */
void cont_stick_dir_to_buttons(u16 *buttons, s8 x, s8 y, u8 method) {
    s16 stickAngle;
    s16 stickMag;
    s16 heading;
    u16 want; /* 1: steer, 2: drive (A / B), 4: (set past 0x4C, not read) */

    stickAngle = math_angle_2d_deg(0, 0, x, -y) * 65536.0 / 360.0 + 32768.0;
    stickMag = sqrtf(x * x + y * y);
    want = 0;
    heading = -gSteerHeading * 16 + 0x7FF0;
    heading -= 0x2000;
    if (stickMag >= 0x10) {
        want = 1;
        if (method == 0 || method == 2 || method == 0x10) {
            want |= 2;
        }
    }
    if (stickMag >= 0x2E) {
        want |= 2;
    }
    if (stickMag >= 0x4C && (gSteerReverse == 0 || method != 3)) {
        want |= 4;
    }
    if (want == 0) {
        gSteerReverse = 0;
    }
    if (!(*buttons & (L_JPAD | R_JPAD)) && (want & 1)) {
        *buttons |= cont_steer_toward(heading, stickAngle, &gSteerReverse, method);
    }
    if (!(*buttons & (A_BUTTON | B_BUTTON)) && (want & 2)) {
        if (gSteerReverse != 0) {
            *buttons |= A_BUTTON;
        } else {
            *buttons |= B_BUTTON;
        }
    }
    if (*buttons & Z_TRIG) {
        *buttons |= L_TRIG | R_TRIG;
    }
}
