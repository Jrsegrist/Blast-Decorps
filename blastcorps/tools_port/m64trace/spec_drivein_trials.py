# Drive-in trials from spec_drivein.py's save state (level 0, vehicle 4, standing), original ROM:
# each trial loads the state, drives (A + steering STEER[k % 5]) for 1..8 s, brakes until stopped
# and presses Z. Some trials stop beside buildings, where veh_collide_world's world hit leaves fp = 2
# (vehicle 0's part count) for the next drive-in check. D3N trials (default 40), ~12 min game time.
import os
SAVE = os.path.expanduser("~/drivein_lvl0.st")
LOADSTATE = SAVE
VIS = int(os.environ.get("D3VIS", 60 * 60 * 12))
NTRIALS = int(os.environ.get("D3N", 40))
G = ["t7", "s0", "t8", "fp", "ra"]
BPS = {
    0x802AE888: ("ae888_entry", ["a0", "t8", "fp", "ra"], []),
    0x802AED58: ("aec3c_call9a60", G, []),
    0x802AEE38: ("aec3c_exit", ["a3", "fp"], []),
    0x8029AAA8: ("aa10_hit", ["t8", "ra"], []),          # veh_collide_world: sphere hit, fp = 0
    0x8029AB04: ("aa10_done", ["t8", "fp", "ra"], []),   # after its part loop
    0x802AF3C4: ("af340_call9a60", G, []),
}
MEM = [(0x80364456, 1), (0x803ED826, 1), (0x803ED828, 1), (0x803643E0, 4), (0x803643E8, 4)]
DEFMAX = 10 ** 9
DEDUPE = False


def ONHIT(pc, g, rd):
    if pc in (0x8029AAA8, 0x8029AB04) and rd(0x803ED827, 1) == 0:
        return None  # veh_collide_world only inside driver_try_exit (gDriverExitChecking set)
    return ""


A, B, Z = 0x8000, 0x4000, 0x2000
STEER = [0, -70, 70, -35, 35]


def INPUT(vi, rd, ctl):
    v = ctl.vars
    if "k" not in v:
        v.update(k=0, ph="drive", t=vi)
    k = v["k"]
    el = vi - v["t"]
    if v["ph"] == "drive":
        if el < 60 * (1 + (k // 5) % 8):
            return A, STEER[k % 5], 0
        v.update(ph="stop", t=vi)
        return 0, 0, 0
    if v["ph"] == "stop":
        if (el > 30 and rd(0x8036443C, 2) == 0) or el > 900:
            v.update(ph="z", t=vi)
            ctl.log("trial %d stopped: speed %x pos %x,%x vehicle %x" % (
                k, rd(0x8036443C, 2), rd(0x803643E0, 4), rd(0x803643E8, 4), rd(0x80364456, 1)))
        return (B if el > 120 else 0), 0, 0
    if v["ph"] == "z":
        if el < 7:
            return Z, 0, 0
        if el < 150:
            return 0, 0, 0
        ctl.log("trial %d result: vehicle %x drive-in %x" % (k, rd(0x80364456, 1), rd(0x803ED826, 1)))
        v["k"] = k + 1
        if v["k"] >= NTRIALS:
            ctl.stop()
        ctl.load(SAVE)
        v.update(ph="wait", t=vi)
        return 0, 0, 0
    if el >= 3:
        v.update(ph="drive", t=vi)
    return 0, 0, 0
