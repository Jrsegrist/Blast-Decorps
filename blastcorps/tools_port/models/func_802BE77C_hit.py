"""Stand-in for obj_collide_vehicle (the collision pass) that reports a hit: it only
sets the byte gCollHit to 1, as the real pass does when the vehicle touches
something. Lets a caller's "hit" branch run without building a collision scene
(wt-v5: backlash_update / backlash_spawn bounce path).

    eqcheck.py backlash_update --model obj_collide_vehicle=func_802BE77C_hit.py ...
"""


def model(args, mem):
    a = mem.sym("gCollHit")
    w = a & ~3
    shift = (3 - (a & 3)) * 8
    v = mem.u32(w)
    mem.w32(w, (v & ~(0xFF << shift)) | (1 << shift))
    return []
