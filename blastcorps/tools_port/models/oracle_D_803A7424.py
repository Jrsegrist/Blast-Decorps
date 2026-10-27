"""Stand-in for obj_collide_vehicle (vehicle collision pass) in checks of the
vehicle per-frame functions (853D0 ballista_update, 7FB50 jbomb_update):
the only effect their branches depend on is gCollHit ("hit the ring end"),
which the real pass sets deep in its triangle helpers. This model writes
gCollHit from an oracle byte the check line randomizes at heap0
(--heap 4 --mem @heap0:1=bytes:0,1), identically in both builds.

    eqcheck.py ballista_update --model obj_collide_vehicle=oracle_D_803A7424.py --heap 4 --mem @heap0:1=bytes:0,1 ...
"""


def model(args, mem):
    mem.w8(mem.sym("gCollHit"), mem.u8(0x80B00000))
    return []
