"""Test stand-in for an object-hit pass (obj_collide_vehicle and friends) for eqcheck
--model: it only sets the hit flag gCollHit to the byte at 0x80B00000 (the
first --heap block), so one check line can drive both the "hit" and "no hit"
paths of a vehicle update that clears the flag, runs the pass and tests it
(police_update / ateam_update and other vehicle updates).

    eqcheck.py police_update --heap 0x10 --mem @heap0:1=choice:0,1 \
        --model obj_collide_vehicle=hit_flag_803A7424.py ...
"""


def model(args, mem):
    mem.w8(mem.sym("gCollHit"), mem.u8(0x80B00000))
    return []
