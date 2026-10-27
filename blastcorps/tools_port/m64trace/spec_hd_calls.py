# Pass 1: integer registers at the hd.c -> asm calls (veh_update_others / veh_update_player callees), anim_tex_update_all, zone scans
R = ["t6", "t7", "s0", "s1", "s2", "s3", "s4", "fp", "a3", "t8"]
B618 = {0x802B5FAC: "B5FAC", 0x802B02A0: "B02A0", 0x802B46C4: "B46C4", 0x802B77A0: "B77A0", 0x802CBC08: "CBC08",
        0x802CCD80: "CCD80", 0x802CFB00: "CFB00", 0x802C5860: "C5860", 0x802B2FA0: "B2FA0"}
B7AC = {0x802AEEC8: "AEEC8", 0x802B03F4: "B03F4", 0x802B152C: "B152C", 0x802B327C: "B327C", 0x802B49AC: "B49AC",
        0x802B6294: "B6294", 0x802BB274: "BB274", 0x802BBEB8: "BBEB8", 0x802B7A88: "B7A88", 0x802C5AFC: "C5AFC",
        0x802CA4E0: "CA4E0", 0x802C8BB8: "C8BB8", 0x802CBEF0: "CBEF0", 0x802CD068: "CD068", 0x802CFDE8: "CFDE8",
        0x802D0F98: "D0F98"}
BPS = {}
for d in (B618, B7AC):
    for a, l in d.items():
        BPS[a] = (l, R + ["ra"], [])
BPS[0x8029E0AC] = ("E0AC", ["fp", "ra", "a0", "a1"], [])
BPS[0x802ABD54] = ("ABD54", ["a3", "ra"], [])
MEM = [(0x802E8BDC, 4), (0x802E8BEC, 4), (0x80364456, 1)]
REGMEM = {0x802ABD54: [("r0", 0x803BDFD4, 4)], 0x8029E0AC: [("fp", 0, 4), ("fp", 4, 4)]}
VIS = 60 * 60 * 8
DEFMAX = 10 ** 9
