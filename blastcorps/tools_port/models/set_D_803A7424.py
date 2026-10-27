"""Stand-in for obj_collide_vehicle (vehicle collision pass) that reports a hit.

Sets the hit flag gCollHit = 1 and changes nothing else, so a check can
drive a caller's "collided" branch, which a plain stub never reaches. The
vehicle per-frame updates (6E200 skyfall_update / ramdozer_update, 8AEE0, 6C5E0)
clear gCollHit, call obj_collide_vehicle and test it afterwards. The real pass is
checked on its own in 77E20.

Use: --model obj_collide_vehicle=set_D_803A7424.py        (default entry `model`)
 or: --model obj_collide_vehicle=set_D_803A7424.py:run    (same behaviour)
"""


def model(args, mem):
    mem.w8(mem.sym("gCollHit"), 1)
    return []


def run(args, mem):
    model(args, mem)
    return 0
