#!/usr/bin/env python3
"""Self-tests of tools_port/vermap.py on synthetic code (no ROM, no build).

    python3 tools_port/tests/test_vermap.py
"""
import os
import struct
import sys
import tempfile

TP = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, TP)
import vermap   # noqa: E402

FAILS = []


def check(what, got, want):
    if got != want:
        FAILS.append(what)
        print("FAIL %s\n  got:  %r\n  want: %r" % (what, got, want))
    else:
        print("ok   %s" % what)


def lui(rt, imm):
    return (0x0F << 26) | (rt << 16) | (imm & 0xFFFF)


def lw(rt, off, rs):
    return (0x23 << 26) | (rs << 21) | (rt << 16) | (off & 0xFFFF)


def jal(t):
    return (3 << 26) | ((t >> 2) & 0x3FFFFFF)


def hi_lo(addr):
    lo = addr & 0xFFFF
    return ((addr >> 16) + (1 if lo & 0x8000 else 0)) & 0xFFFF, lo


JR_RA, NOP, ADDU = 0x03E00008, 0, 0x01CF7821
VRAM = 0x80400000


def func_load(data_addr, callee):
    """lui/lw of a data word, a call, return: 8 words"""
    h, l = hi_lo(data_addr)
    return [lui(14, h), lw(14, l, 14), jal(callee), NOP, ADDU, NOP, JR_RA, NOP]


def segment(changed_extra):
    """three functions (A, B, C) then data; C has extra words in one version,
    which moves the data"""
    a = VRAM
    b = a + 0x20
    c = b + 0x20
    words_c = [ADDU] * 4 + [NOP] * changed_extra + [JR_RA, NOP]
    text_len = 0x40 + len(words_c) * 4
    text_len = (text_len + 15) & ~15
    data_vram = VRAM + text_len
    d = data_vram + 0x10        # the word A loads
    # A loads a data word and calls C; B only calls A
    text = func_load(d, c) + [ADDU, ADDU, jal(a), NOP, ADDU, NOP, JR_RA, NOP]
    text += words_c
    text += [NOP] * ((text_len - len(text) * 4) // 4)
    data = [0x11111111, 0x22222222, 0x33333333, 0x44444444, 0xAAAA0000, 0xBBBB0000, b, 0x55555555]
    raw = struct.pack('>%dI' % (len(text) + len(data)), *(text + data))
    return vermap.Seg('test', raw, VRAM, text_len), (a, b, c, d)


def main():
    check("mask jal", vermap.mask(jal(0x80401234)), 3 << 26)
    check("mask lui", vermap.mask(lui(14, 0x8030)), lui(14, 0))
    check("mask R-type kept", vermap.mask(ADDU), ADDU)
    check("dmask pointer", vermap.dmask(0x80304A90), 0x80000000)
    check("dmask physical pointer", vermap.dmask(0x002FDD48), 0x80000000)
    check("dmask plain word", vermap.dmask(0x3F800000), 0x3F800000)

    ref, (ra, rb, rc, rd) = segment(changed_extra=4)
    oth, (oa, ob, oc, od) = segment(changed_extra=0)
    funcs = [(ra, 'A'), (rb, 'B'), (rc, 'C')]
    sm = vermap.SegMap('test', ref, oth, funcs)
    kinds = {f['name']: f['kind'] for f in sm.funcs}
    check("A: data address moved -> shifted", kinds['A'], 'shifted')
    check("B: same bytes, same place -> identical", kinds['B'], 'identical')
    check("C: shorter -> changed", kinds['C'], 'changed')
    fc = [f for f in sm.funcs if f['name'] == 'C'][0]
    check("C starts after B in the other version", fc['other'], oc)
    check("map_text inside A", sm.map_text(ra + 8), oa + 8)
    check("map_data", sm.map_data(rd), od)
    pairs = set(sm.pairs())
    check("lui/lw pair", (rd, od) in pairs, True)
    check("jal pair", (ra, oa) in pairs, True)
    check("data pointer pair", (rb, ob) in pairs, True)
    # the text pieces of a subsegment that spans A..C: one piece
    check("runs over the text", vermap.runs(sm, ra, rc + 4), [(ra, oa)])

    # port_syms with a stand-in mapper
    class FakeMapper:
        overrides = {}

        def __call__(self, a):
            return ({0x80400010: (0x80400010, 'text'), 0x80305000: (0x80304F50, 'data'),
                     0x80306000: (None, 'bss?')}.get(a, (a, 'fixed')))

        def seg_of(self, a):
            return None
    d = tempfile.mkdtemp()
    src, dst = os.path.join(d, 'in.txt'), os.path.join(d, 'out.txt')
    open(src, 'w').write('// header us.v11\nfoo = 0x80400010; // type:func\nbar = 0x80305000;\n'
                         'gone = 0x80306000;\nrom = 0x00350950;\n')
    bad = vermap.port_syms(FakeMapper(), src, dst, 'us.v10', 'us.v11')
    out = open(dst).read().split('\n')
    check("port_syms header", out[0], '// header us.v10')
    check("port_syms text", out[1], 'foo = 0x80400010; // type:func')
    check("port_syms data", out[2], 'bar = 0x80304F50;')
    check("port_syms unmapped", out[3].startswith('// gone: not in us.v10'), True)
    check("port_syms rom offset keeps its width", out[4], 'rom = 0x00350950;')
    check("port_syms reports", [b[0] for b in bad], ['gone'])

    print("%d failure(s)" % len(FAILS))
    return 1 if FAILS else 0


if __name__ == '__main__':
    sys.exit(main())
