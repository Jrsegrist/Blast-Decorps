"""Python model of math_dist3 for eqcheck --model (example; the harness can
also run the asm itself since FPU long ops are emulated).

math_dist3(ax, ay, az, bx, by, bz) (convention: in t3,t4,t5,t6,t7,s0;
out s1): the rounded 3-D distance.  Each difference is a 32-bit subtraction
(subu, sign-extended), the squares and their sum are 64-bit (dmult/daddu,
wrapping), then cvt.d.l, sqrt.d and cvt.l.d (round to nearest even, the FCSR
default); s1 gets the low 32 bits.

    eqcheck.py obj_calc_target_time --model math_dist3=math_dist3.py ...
"""
import math


def s32(v):
    v &= 0xFFFFFFFF
    return v - (1 << 32) if v & 0x80000000 else v


def model(args, mem):
    ax, ay, az, bx, by, bz = args
    s = sum(s32(b - a) ** 2 for a, b in ((ax, bx), (ay, by), (az, bz))) & ((1 << 64) - 1)
    if s >> 63:
        s -= 1 << 64
    d = float(s)
    if d < 0:
        raise ValueError("sqrt of a negative (wrapped) sum: the VR4300 traps in cvt.l.d")
    return [round(math.sqrt(d)) & 0xFFFFFFFF]
