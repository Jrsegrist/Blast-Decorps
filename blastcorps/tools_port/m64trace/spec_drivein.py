# Vehicle 0's drive-in (getting out of a vehicle), original ROM (baserom.us.v11) addresses.
# Boot -> new game -> level 0 in vehicle 4 (START / A pulses until game mode 4), wait 2 s,
# save ~/drivein_lvl0.st (used by spec_drivein_trials.py), press Z, run 10 s more.
# Logs the leftover key (t8) and fp that driver_try_exit / driver_check_exit_point / driver_exit_walk_step hand to
# veh_reground, and what that stores (gSlotSurface[0..2] at 0x803ED3F2, vehicle 0's +0x50).
import os
VIS = 60 * 80
SAVE = os.path.expanduser("~/drivein_lvl0.st")
WATCH = [(0x80364A90, 8), (0x80364A98, 8), (0x802E8BDC, 4), (0x80364456, 1), (0x802E8BD0, 1),
         (0x80364AA8, 4), (0x803ED826, 1), (0x8036443C, 2)]
FL = [12, 14, 16, 18, 20, 22, 24, 26]
G = ["a0", "a2", "t2", "t8", "fp", "gp", "sp", "ra", "s0", "s1", "s2", "s3", "s4"]
BPS = {
    0x802AE888: ("ae888_entry", G, FL),
    0x802AED58: ("aec3c_call9a60", G, FL),        # driver_check_exit_point's jal veh_reground
    0x802AED60: ("aec3c_ret9a60", ["fp", "t8"], []),
    0x802AEE38: ("aec3c_exit", ["a3", "fp", "t8"], FL),
    0x802AEEC8: ("aeec8_entry", ["t8", "fp", "ra"], []),
    0x802AF340: ("af340_entry", ["t8", "fp", "ra"], []),
    0x802AF3C4: ("af340_call9a60", G, FL),        # driver_exit_walk_step's jal veh_reground
    0x802AF3CC: ("af340_ret9a60", ["fp", "t8"], []),
}
MEM = [(0x80364456, 1), (0x803ED826, 1), (0x803ED828, 1), (0x803ED808, 4), (0x803ED80C, 4), (0x803ED810, 4),
       (0x803ED814, 4), (0x803ED3F0, 4), (0x803ED3F4, 4), (0x803ED7B0, 4)]
DEFMAX = 2000

A, B, Z, START = 0x8000, 0x4000, 0x2000, 0x1000


def INPUT(vi, rd, ctl):
    v = ctl.vars
    ph = v.setdefault("ph", 0)
    mode = rd(0x80364A90, 8)
    if ph == 0:  # front end: START / A pulses until playing (mode 4, not paused, in a vehicle)
        if mode == 4 and rd(0x802E8BD0, 1) == 0 and rd(0x80364456, 1) != 0:
            v["ph"], v["t"] = 1, vi
            ctl.log("in level %x vehicle %x" % (rd(0x802E8BDC, 4), rd(0x80364456, 1)))
            return 0, 0, 0
        if vi % 90 < 6:
            return (START if (vi // 90) % 2 == 0 else A), 0, 0
        return 0, 0, 0
    if ph == 1:  # stand still for 2 s, then save
        if vi - v["t"] >= 120 and rd(0x8036443C, 2) == 0 and rd(0x802E8BD0, 1) == 0:
            ctl.save(SAVE)
            v["ph"], v["t"] = 2, vi
        return 0, 0, 0
    if ph == 2:  # Z for 7 frames
        if vi - v["t"] < 3:
            return 0, 0, 0
        if vi - v["t"] < 10:
            return Z, 0, 0
        v["ph"], v["t"] = 3, vi
        ctl.log("Z released: vehicle %x drive-in %x" % (rd(0x80364456, 1), rd(0x803ED826, 1)))
        return 0, 0, 0
    if vi - v["t"] > 600:
        ctl.stop()
    return 0, 0, 0
