#!/usr/bin/env python3
"""Map one ROM version's code onto another's (e.g. US 1.1 -> US 1.0), and port
symbol files, splat yamls and linker scripts to the other version.

Both ROMs are read directly; init and the gzip'd hd_code / hd_front_end
segments are found by walking each ROM (romsplit.py).

Text: the words are compared with every relocatable field masked (j/jal
targets, lui immediates, the immediates of addiu/ori and loads/stores) and
aligned with difflib. Each function of the reference version (taken from its
matching-build ELF) is

  identical  same bytes at the same address in both versions
  shifted    same instructions, only the relocated fields (addresses) differ
  changed    the instructions differ, or the size does

A function whose masked body turns up elsewhere (the link order changed) is
"shifted" and reported as moved.

Data: the words are aligned the same way, with pointer-looking words masked.
Addresses the code and data refer to (.bss among them) are mapped by votes:
jal targets, lui/lo pairs and pointer words at aligned positions give
(reference address, other address) pairs.

Only names and addresses come out of this tool, never ROM contents.

  vermap.py report [-v]
  vermap.py port-syms IN OUT          symbol file (name = 0xADDR; ...)
  vermap.py port-yaml SEG IN OUT      a segment's splat yaml
  vermap.py port-ld IN OUT            a linker script (.bss placement)
Common options: --ref us.v11 --other us.v10 (defaults), --root DIR (the
directory with the baseroms; default ..).
"""
import argparse, difflib, os, re, struct, subprocess, sys, zlib
from bisect import bisect_right
from collections import Counter

LOAD_STORE = set(range(0x20, 0x30)) | {0x31, 0x33, 0x35, 0x37, 0x39, 0x3D, 0x3F}
LO_OPS = LOAD_STORE | {0x09, 0x0D}  # + addiu, ori

SEGS = ('init', 'hd_code', 'hd_front_end')
VRAM = dict(init=0x8021ED00, hd_code=0x802447C0, hd_front_end=0x801E7000)
INIT_TEXT = 0x3A40          # init: code, then a .data bin (same in all versions so far)
# where each segment's .bss ends (the next thing in RAM)
BSS_END = dict(init=0x80222A20, hd_code=0x80400000, hd_front_end=0x8021ED00)


def words(b):
    n = len(b) // 4
    return list(struct.unpack('>%dI' % n, b[:n * 4]))


def mask(w):
    op = w >> 26
    if op in (2, 3):
        return w & 0xFC000000
    if op == 0x0F or op in LO_OPS:
        return w & 0xFFFF0000
    return w


def dmask(w):
    """a data word that looks like an address (KSEG0, or physical as display
    lists hold them: 0x001E7000-0x00400000) -> a placeholder"""
    if 0x80000000 <= w < 0x80800000 or (0x001E7000 <= w < 0x00400000 and not w & 3):
        return 0x80000000
    return w


def s16(x):
    return x - 0x10000 if x & 0x8000 else x


def rom_layout(rom, ver):
    """ROM offsets of init and the four gzip'd code files ({name: (start, end)}), and the
    top-level split from init on ([(start, name or None)], ending at 0x800000; the header
    and IPL3 before it read as "header"), both from the ROM (romsplit.py)."""
    import romsplit
    segs = romsplit.segments(rom, ver)
    ents = [(s, n) for s, t, n in segs if s >= romsplit.INIT]
    return romsplit.code_layout(rom, ver), ents


class Version:
    def __init__(self, root, ver):
        self.ver = ver
        self.rom = open(os.path.join(root, 'baserom.%s.z64' % ver), 'rb').read()
        self.layout, self.romfiles = rom_layout(self.rom, ver)
        self.seg = {}
        a, b = self.layout['init']
        self.seg['init'] = Seg('init', self.rom[a:b], VRAM['init'], INIT_TEXT)
        for s in ('hd_code', 'hd_front_end'):
            t = self.inflate(s + '_text')
            d = self.inflate(s + '_data')
            self.seg[s] = Seg(s, t + d, VRAM[s], len(t))

    def inflate(self, name):
        a, b = self.layout[name]
        return zlib.decompressobj(31).decompress(self.rom[a:b])


class Seg:
    def __init__(self, name, data, vram, text):
        self.name, self.bytes, self.vram, self.text = name, data, vram, text
        self.w = words(data[:text])
        self.m = [mask(x) for x in self.w]
        self.dw = words(data[text:])
        self.dm = [dmask(x) for x in self.dw]
        self.index = {}
        for j in range(len(self.m) - 3):
            self.index.setdefault(tuple(self.m[j:j + 4]), []).append(j)

    @property
    def data_vram(self):
        return self.vram + self.text

    @property
    def end(self):
        return self.vram + len(self.bytes)


def get_funcs(elf, lo, hi):
    out = subprocess.run(['mips-linux-gnu-nm', '-n', elf], capture_output=True, text=True, check=True).stdout
    syms = {}
    for line in out.splitlines():
        p = line.split()
        if len(p) != 3 or p[1] not in 'Tt':
            continue
        a = int(p[0], 16)
        if not lo <= a < hi or re.match(r'^\.?L[0-9A-F]{8}', p[2]):
            continue
        syms.setdefault(a, p[2])
    return sorted(syms.items())


def seq_map(a, b):
    sm = difflib.SequenceMatcher(None, a, b, autojunk=False)
    m = {}
    ops = sm.get_opcodes()
    for tag, i1, i2, j1, j2 in ops:
        if tag == 'equal':
            for k in range(i2 - i1):
                m[i1 + k] = j1 + k
    return m, ops


class SegMap:
    """ref segment -> other segment: functions, text words, data words."""

    def __init__(self, name, ref, oth, elf):
        self.name, self.ref, self.oth = name, ref, oth
        self.tmap, self.tops = seq_map(ref.m, oth.m)
        self.dmap, self.dops = seq_map(ref.dm, oth.dm)
        self.moved_data = self.find_moved_data()
        # (elf: the ref matching build's ELF, or a list of (address, name))
        funcs = elf if isinstance(elf, list) else get_funcs(elf, ref.vram, ref.vram + ref.text)
        self.funcs = self.classify(funcs)
        self.fstarts = [f['ref'] for f in self.funcs]

    def find_moved_data(self):
        """Blocks of data that difflib saw as a delete plus an insert: moved."""
        dels = [(i1, i2) for t, i1, i2, j1, j2 in self.dops if t in ('delete', 'replace') and i2 - i1 >= 4]
        ins = [(j1, j2) for t, i1, i2, j1, j2 in self.dops if t in ('insert', 'replace') and j2 - j1 >= 4]
        moved = []
        for i1, i2 in dels:
            blk = self.ref.dm[i1:i2]
            n = i2 - i1
            for j1, j2 in ins:
                if j2 - j1 < n:
                    continue
                hit = None
                for j in range(j1, j2 - n + 1):
                    if self.oth.dm[j:j + n] == blk:
                        hit = j
                        break
                if hit is None and abs(j2 - j1 - n) <= 4:
                    # about the same size, mostly the same words: moved (and
                    # perhaps edited); difflib's ends can be a word off
                    best = max((sum(a == b for a, b in zip(blk, self.oth.dm[j:j + n])), -abs(j - j1), j)
                               for j in range(max(0, j1 - 4), j1 + 5))
                    if best[0] * 2 >= n:
                        hit = best[2]
                if hit is not None:
                    # difflib's ends can pair a moved block's first/last word (often a
                    # zero) with a neighbour: map the words that land inside the
                    # inserted block, past i2 too while they still match
                    for k in range(min(i2 - i1 + 8, len(self.ref.dm) - i1)):
                        j = hit + k
                        if not j1 <= j < j2:
                            if j >= j2:
                                break
                            self.dmap.pop(i1 + k, None)
                            continue
                        if k >= n and self.ref.dm[i1 + k] != self.oth.dm[j]:
                            break
                        self.dmap[i1 + k] = j
                    moved.append((i1, i2, hit))
                    break
        return moved

    def classify(self, funcs):
        ref, oth, amap = self.ref, self.oth, self.tmap
        res = []
        for k, (addr, name) in enumerate(funcs):
            end = funcs[k + 1][0] if k + 1 < len(funcs) else ref.vram + ref.text
            i1, i2 = (addr - ref.vram) // 4, (end - ref.vram) // 4
            js = [amap.get(i) for i in range(i1, i2)]
            r = dict(name=name, ref=addr, size=(i2 - i1) * 4, other=None, osize=None, moved=False)
            j1 = None
            if all(j is not None for j in js) and all(js[n] == js[0] + n for n in range(len(js))):
                j1 = js[0]
            else:
                body = ref.m[i1:i2]
                hits = [j for j in oth.index.get(tuple(body[:4]), []) if oth.m[j:j + len(body)] == body]
                if len(hits) == 1:
                    j1 = hits[0]
                    r['moved'] = True
                    for n in range(i2 - i1):
                        amap[i1 + n] = j1 + n
            if j1 is not None:
                same = ref.w[i1:i2] == oth.w[j1:j1 + i2 - i1]
                r['kind'] = 'identical' if same and j1 == i1 else 'shifted'
                r['other'] = oth.vram + j1 * 4
                r['osize'] = r['size']
            else:
                r['kind'] = 'changed'
            res.append(r)
        # a changed function starts where the previous one ends, and ends where
        # the next one (in the other version's order) starts
        for k, r in enumerate(res):
            if r['kind'] != 'changed':
                continue
            prev = res[k - 1] if k else None
            j0 = self.tmap.get((r['ref'] - ref.vram) // 4)
            if j0 is not None and (r['ref'] - ref.vram) // 4 - 1 in self.tmap:
                # its first word (and the previous function's last) are aligned
                r['other'] = oth.vram + j0 * 4
            elif prev is not None and prev['other'] is not None and prev['osize'] is not None:
                r['other'] = prev['other'] + prev['osize']
            elif k == 0:
                r['other'] = oth.vram
        starts = sorted(r['other'] for r in res if r['other'] is not None)
        for r in res:
            if r['kind'] == 'changed' and r['other'] is not None:
                nxt = [s for s in starts if s > r['other']]
                r['osize'] = (nxt[0] if nxt else oth.vram + oth.text) - r['other']
        return res

    def func_at(self, addr):
        k = bisect_right(self.fstarts, addr) - 1
        return self.funcs[k] if k >= 0 else None

    def map_text(self, addr):
        f = self.func_at(addr)
        if f is None or f['other'] is None:
            return None
        if f['kind'] != 'changed':
            return addr - f['ref'] + f['other']
        if addr == f['ref']:
            return f['other']
        j = self.tmap.get((addr - self.ref.vram) // 4)
        return None if j is None else self.oth.vram + j * 4 + (addr & 3)

    def map_data(self, addr):
        i = (addr - self.ref.data_vram) // 4
        j = self.dmap.get(i)
        if j is None:
            # an unaligned word: interpolate from the nearest aligned word before it
            for back in range(1, 64):
                if i - back in self.dmap:
                    j = self.dmap[i - back] + back
                    break
            else:
                return None
        return self.oth.data_vram + j * 4 + (addr & 3)

    @staticmethod
    def resolve(ws, vram):
        """index -> address for the jal and lui/lo instructions of a run of words."""
        out, hi = {}, {}
        for k, w in enumerate(ws):
            op = w >> 26
            if op in (2, 3):
                out[k] = (vram & 0xF0000000) | ((w & 0x3FFFFFF) << 2)
            elif op == 0x0F:
                hi[(w >> 16) & 31] = (w & 0xFFFF) << 16
            elif op in LO_OPS and op != 0x0D:
                rs = (w >> 21) & 31
                if rs in hi:
                    out[k] = (hi[rs] + s16(w & 0xFFFF)) & 0xFFFFFFFF
            if op == 0 and (w & 0x3F) in (8, 9):
                hi.clear()
        return out

    def changed_pairs(self):
        """Address pairs from the functions that changed: their instructions
        aligned by opcode alone (the registers often differ), each side's
        addresses resolved on its own."""
        ref, oth = self.ref, self.oth

        def opm(w):
            op = w >> 26
            return (op, w & 0x3F) if op == 0 else (op, (w >> 16) & 31) if op == 1 else (op,)
        out = []
        for f in self.funcs:
            if f['kind'] != 'changed' or f['other'] is None or not f['osize']:
                continue
            i1, n1 = (f['ref'] - ref.vram) // 4, f['size'] // 4
            j1, n2 = (f['other'] - oth.vram) // 4, f['osize'] // 4
            wa, wb = ref.w[i1:i1 + n1], oth.w[j1:j1 + n2]
            va, vb = self.resolve(wa, ref.vram), self.resolve(wb, oth.vram)
            sm = difflib.SequenceMatcher(None, [opm(w) for w in wa], [opm(w) for w in wb], autojunk=False)
            for tag, a1, a2, b1, b2 in sm.get_opcodes():
                if tag == 'equal':
                    for k in range(a2 - a1):
                        if a1 + k in va and b1 + k in vb:
                            out.append((va[a1 + k], vb[b1 + k]))
        return [(a, b) for a, b in out if 0x80000000 <= a < 0x80800000]

    def pairs(self):
        """(ref, other) address pairs from aligned code and data."""
        ref, oth, amap = self.ref, self.oth, self.tmap
        out = []
        hi_a, hi_b = {}, {}
        for i in range(len(ref.w)):
            j = amap.get(i)
            if j is None:
                hi_a.clear(); hi_b.clear()
                continue
            wa, wb = ref.w[i], oth.w[j]
            op = wa >> 26
            if op == 3 or op == 2:
                out.append(((ref.vram & 0xF0000000) | ((wa & 0x3FFFFFF) << 2),
                            (oth.vram & 0xF0000000) | ((wb & 0x3FFFFFF) << 2)))
            elif op == 0x0F:
                rt = (wa >> 16) & 31
                hi_a[rt] = (wa & 0xFFFF) << 16
                hi_b[rt] = (wb & 0xFFFF) << 16
            elif op in LO_OPS and op != 0x0D:
                rs = (wa >> 21) & 31
                if rs in hi_a:
                    out.append(((hi_a[rs] + s16(wa & 0xFFFF)) & 0xFFFFFFFF,
                                (hi_b[rs] + s16(wb & 0xFFFF)) & 0xFFFFFFFF))
            if op == 0 and (wa & 0x3F) in (8, 9):
                hi_a.clear(); hi_b.clear()
        for i, j in self.dmap.items():
            a, b = ref.dw[i], oth.dw[j]
            if a != b and dmask(a) == 0x80000000 and dmask(b) == 0x80000000:
                out.append((a, b))
            elif a == b and dmask(a) == 0x80000000:
                out.append((a, b))
        return [(a, b) for a, b in out if 0x80000000 <= a < 0x80800000]


class Mapper:
    def __init__(self, root, ref, other, elfdir, verbose=False):
        self.R, self.O = Version(root, ref), Version(root, other)
        self.maps = {}
        for s in SEGS:
            self.maps[s] = SegMap(s, self.R.seg[s], self.O.seg[s],
                                  os.path.join(elfdir, '%s.%s.elf' % (s, ref)))
        votes = {}
        for s in SEGS:
            for a, b in self.maps[s].pairs():
                votes.setdefault(a, Counter())[b - a] += 1
        # a changed function starts where the jals of the unchanged code say
        for s in SEGS:
            sm = self.maps[s]
            for f in sm.funcs:
                if f['kind'] == 'changed' and f['ref'] in votes:
                    f['other'] = f['ref'] + votes[f['ref']].most_common(1)[0][0]
            starts = sorted(f['other'] for f in sm.funcs if f['other'] is not None)
            for f in sm.funcs:
                if f['kind'] == 'changed' and f['other'] is not None:
                    nxt = [x for x in starts if x > f['other']]
                    f['osize'] = (nxt[0] if nxt else sm.oth.vram + sm.oth.text) - f['other']
                    # only the nops that pad it to the next file's 16-byte start differ
                    i1, j1 = (f['ref'] - sm.ref.vram) // 4, (f['other'] - sm.oth.vram) // 4
                    a = sm.ref.m[i1:i1 + f['size'] // 4]
                    b = sm.oth.m[j1:j1 + f['osize'] // 4]
                    while a and a[-1] == 0:
                        a = a[:-1]
                    while b and b[-1] == 0:
                        b = b[:-1]
                    if a == b:
                        f['kind'], f['padding'] = 'shifted', True
        # the changed functions' own references: only where the unchanged code has
        # none, and only small moves (their alignment is a guess)
        cvotes = {}
        for s in SEGS:
            for a, b in self.maps[s].changed_pairs():
                if a not in votes and abs(b - a) < 0x8000:
                    cvotes.setdefault(a, Counter())[b - a] += 1
        self.votes, self.cvotes = votes, cvotes
        self.vaddrs = sorted(votes)
        # hand-checked addresses the votes can't give (code that changed):
        # tools_port/vermap_overrides.REF-OTHER.txt, `0xREF = 0xOTHER  # note`
        self.overrides = {}
        p = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'vermap_overrides.%s-%s.txt' % (ref, other))
        if os.path.exists(p):
            for line in open(p):
                m = re.match(r'\s*(0x[0-9A-Fa-f]+)\s*=\s*(0x[0-9A-Fa-f]+|none)', line)
                if m:
                    self.overrides[int(m.group(1), 16)] = None if m.group(2) == 'none' else int(m.group(2), 16)

    def seg_of(self, addr):
        for s in SEGS:
            sg = self.R.seg[s]
            if sg.vram <= addr < BSS_END[s]:
                return s
        return None

    def map_rom(self, off):
        """A ROM offset (assets, the code files)."""
        ents = self.R.romfiles
        oents = dict((n, o) for o, n in self.O.romfiles if n)
        first_code = min(self.R.layout[n][0] for n in self.R.layout if n != 'init')
        if off < first_code and oents and first_code == min(
                self.O.layout[n][0] for n in self.O.layout if n != 'init'):
            # the assets before the code files: same offsets in both (checked by `report`)
            return off
        for k in range(len(ents) - 1):
            o, n = ents[k]
            if o <= off < ents[k + 1][0] or (off == ents[k + 1][0] and k + 2 == len(ents)):
                if n is None:
                    # an unnamed piece (padding): keep its distance from the previous named one
                    return None
                nn = n.replace(self.R.ver, self.O.ver)
                if nn in oents:
                    return oents[nn] + off - o
                return None
        return off if off == len(self.R.rom) else None

    def __call__(self, addr, exact=False):
        """Map one address; returns (address or None, how)."""
        if addr in self.overrides:
            return self.overrides[addr], 'override'
        if addr < 0x80000000:
            if addr < 0x01000000:
                r = self.map_rom(addr)
                return r, 'rom'
            return addr, 'const'
        s = self.seg_of(addr)
        if s is None:
            return addr, 'fixed'
        sm, sg = self.maps[s], self.R.seg[s]
        if addr < sg.data_vram:
            return sm.map_text(addr), 'text'
        if addr < sg.end:
            return sm.map_data(addr), 'data'
        if addr == sg.end:
            return sm.oth.end, 'data'
        # .bss: votes for this address, else the nearest voted address below it
        v = self.votes.get(addr)
        if v:
            d, n = v.most_common(1)[0]
            return addr + d, 'bss'
        v = self.cvotes.get(addr)
        if v:
            d, n = v.most_common(1)[0]
            return addr + d, 'bss(changed code)'
        k = bisect_right(self.vaddrs, addr) - 1
        while k >= 0 and self.vaddrs[k] >= sg.end:
            a = self.vaddrs[k]
            d, n = self.votes[a].most_common(1)[0]
            if n >= 1 and addr - a < 0x1000:
                return addr + d, 'bss~'
            k -= 1
        if addr - sg.end < 0x1000:
            # near the start of .bss: it moves with the end of the data
            return addr + sm.oth.end - sg.end, 'bss~'
        return None, 'bss?'


SYM_RE = re.compile(r'^(\s*)([\w.$]+)(\s*=\s*)(0x[0-9A-Fa-f]+)(\s*;)(.*)$')


def port_syms(mp, src, dst, other_ver, ref_ver, ld=False):
    out, bad = [], []
    cmt = (lambda s: '/* %s */' % s.replace('*/', '* /')) if ld else (lambda s: '// ' + s)
    for line in open(src):
        line = line.rstrip('\n')
        m = SYM_RE.match(line)
        if not m:
            out.append(line.replace(ref_ver, other_ver))
            continue
        a = int(m.group(4), 16)
        b, how = mp(a)
        tail = m.group(6)
        if b is None:
            bad.append((m.group(2), a, how))
            out.append(cmt('%s: not in %s (%s): %s' % (m.group(2), other_ver, how, line.strip())))
            continue
        sz = re.search(r'size:(0x[0-9A-Fa-f]+)', tail)
        if sz and how == 'text':
            f = mp.maps[mp.seg_of(a)].func_at(a)
            if f and f['ref'] == a and f['osize'] is not None and f['osize'] != f['size']:
                tail = tail.replace(sz.group(0), 'size:0x%X' % f['osize'])
        out.append('%s%s%s0x%08X%s%s' % (m.group(1), m.group(2), m.group(3), b, m.group(5), tail)
                   if a >= 0x80000000 else
                   '%s%s%s0x%0*X%s%s' % (m.group(1), m.group(2), m.group(3), len(m.group(4)) - 2, b, m.group(5), tail))
    open(dst, 'w').write('\n'.join(out) + '\n')
    for n, a, how in bad:
        print('  %s: %s 0x%08X (%s) not mapped' % (dst, n, a, how), file=sys.stderr)
    return bad


SUB_RE = re.compile(r'^(\s*- \[)(0x[0-9A-Fa-f]+)(,.*)$')


def runs(sm, s, e):
    """[(ref address, other address)] where the ref range [s, e) lands in
    contiguous pieces of the other version."""
    rsg = sm.ref
    out, prev = [], None
    if s < rsg.data_vram:
        # text: by whole functions (inside a changed one the words don't line up)
        fs = [f for f in sm.funcs if s <= f['ref'] < e and f['other'] is not None]
        if not fs or fs[0]['ref'] != s:
            # starts with words that are no function's own (a bin table)
            b = sm.map_text(s)
            out.append((s, b))
            prev = None if b is None or not fs else b + fs[0]['ref'] - s
        for f in fs:
            if f['other'] != prev:
                out.append((f['ref'], f['other']))
            prev = f['other'] + (f['osize'] if f['osize'] is not None else f['size'])
        return out
    for a in range(s, e, 4):
        b = sm.map_text(a) if a < rsg.data_vram else sm.map_data(a)
        if b is None:
            if prev is not None:
                prev += 4
            continue
        # a few words more or fewer (data edited in place) keep it one piece;
        # only a jump backwards or far ahead (a block that moved) starts another
        if prev is None or not prev + 4 - 0x100 <= b <= prev + 4 + 0x100 or (b < prev and out and b < out[-1][1]):
            out.append((a, b))
        prev = b
    return out


def port_yaml(mp, seg, src, dst, ref_ver, other_ver, build_dir, asm_dir, asset_dir):
    """Subsegment offsets mapped, pieces that moved split (bins) and the list
    sorted in the other version's order."""
    lines = open(src).read().split('\n')
    sm = mp.maps[seg]
    rsg, osg = mp.R.seg[seg], mp.O.seg[seg]
    sub_idx = [k for k, l in enumerate(lines) if SUB_RE.match(l) and l.startswith('    - [')]
    offs = [int(SUB_RE.match(lines[k]).group(2), 16) for k in sub_idx]
    # comment-only lines right after a subsegment stay with it
    cont = {}
    skip = set()
    for k in sub_idx:
        j = k + 1
        while j < len(lines) and lines[j].strip().startswith('#') and j not in sub_idx:
            cont.setdefault(k, []).append(lines[j])
            skip.add(j)
            j += 1
    new_subs, bad = [], []
    for n, k in enumerate(sub_idx):
        m = SUB_RE.match(lines[k])
        off, rest = offs[n], m.group(3)
        s = rsg.vram + off
        e = rsg.vram + (offs[n + 1] if n + 1 < len(offs) else len(rsg.bytes))
        if 'linker' in rest:
            pieces = [(s, osg.data_vram)]
        elif e <= s:
            b = sm.map_text(s) if s < rsg.data_vram else (osg.end if s == rsg.end else sm.map_data(s))
            pieces = [(s, b)]
        else:
            pieces = runs(sm, s, e)
        if not pieces or pieces[0][1] is None:
            bad.append(lines[k])
            continue
        for p, (ra, ob) in enumerate(pieces):
            r = rest
            if p:
                nm = re.match(r'(,\s*bin,\s*)([\w/.]+?)(?:_([0-9A-F]+))?(\].*)$', rest)
                if not nm:
                    bad.append('%s (piece at 0x%X)' % (lines[k].strip(), ra - rsg.vram))
                    continue
                r = '%s%s_%X]  # (split off: moved in %s)' % (nm.group(1), nm.group(2), ra - rsg.vram, other_ver)
            text = '%s0x%0*X%s' % (m.group(1), len(m.group(2)) - 2, ob - osg.vram, r)
            if p == 0 and k in cont:
                text = '\n'.join([text] + cont[k])
            new_subs.append((ob - osg.vram, n, p, text))
    new_subs.sort()
    out, done, hdr_done = [], False, False
    for k, line in enumerate(lines):
        if k in skip:
            continue
        if k in sub_idx:
            if not done:
                out.extend(x[3] for x in new_subs)
                done = True
            continue
        m2 = re.match(r'^(\s*- \[)(0x[0-9A-Fa-f]+)(\]\s*)$', line)
        if m2:
            line = '%s0x%X%s' % (m2.group(1), len(osg.bytes), m2.group(3))
        line = line.replace(ref_ver, other_ver) if 'src_path' not in line else line
        out.append(line)
        if line.startswith('options:') and not hdr_done:
            out.append('  # generated from %s by tools_port/vermap.py: do not edit (the comments,' %
                       os.path.basename(src))
            out.append("  # and the offsets in the file names, are %s's)" % ref_ver)
            out.append('  build_path: %s' % build_dir)
            out.append('  asm_path: %s' % asm_dir)
            out.append('  asset_path: %s' % asset_dir)
            hdr_done = True
    open(dst, 'w').write('\n'.join(out))
    for l in bad:
        print('  %s: not mapped: %s' % (dst, l.strip()), file=sys.stderr)
    return bad


def port_ld(mp, src, dst, ref_ver, other_ver, build_dir):
    txt = open(src).read()
    bad = []

    def rep(m):
        a = int(m.group(0), 16)
        b, how = mp(a)
        if b is None:
            bad.append(m.group(0))
            return m.group(0)
        return '0x%08X' % b
    txt = re.sub(r'0x8[0-9A-Fa-f]{7}', rep, txt)
    txt = txt.replace('build/src.%s/' % ref_ver, '%s/src.%s/' % (build_dir, ref_ver))
    txt = txt.replace('%s.ld' % ref_ver, '%s.ld' % other_ver)
    open(dst, 'w').write(txt)
    for b in bad:
        print('  %s: not mapped: %s' % (dst, b), file=sys.stderr)
    return bad


GEN_NOTE = ('// generated by tools_port/vermap.py (make VERSION=%s port-version) from the %s files:\n'
            '// %s\'s names at %s\'s addresses. Do not edit: change the %s files and regenerate.\n')


def port_symbol_set(mp, seg, ref, other):
    """symbol_addrs.SEG.OTHER.txt: SEG's ref symbol_addrs, plus every name the
    ref build got from splat's auto files, so the shared C links."""
    src = 'symbol_addrs.%s.%s.txt' % (seg, ref)
    dst = 'symbol_addrs.%s.%s.txt' % (seg, other)
    tmp = dst + '.tmp'
    bad = port_syms(mp, src, tmp, other, ref)
    body = open(tmp).read()
    os.remove(tmp)
    have = set(m.group(2) for m in (SYM_RE.match(l) for l in body.split('\n')) if m)
    extra, nbad = [], 0
    for auto in ('undefined_syms_auto', 'undefined_funcs_auto'):
        p = '%s.%s.%s.txt' % (auto, seg, ref)
        if not os.path.exists(p):
            continue
        for line in open(p):
            m = SYM_RE.match(line.rstrip('\n'))
            if not m or m.group(2) in have or m.group(2).startswith('.'):
                continue
            a = int(m.group(4), 16)
            b, how = mp(a)
            have.add(m.group(2))
            if b is None:
                extra.append('// %s = 0x%08X: not in %s (%s)' % (m.group(2), a, other, how))
                nbad += 1
                continue
            extra.append('%s = 0x%08X;%s' % (m.group(2), b, ' // type:func' if 'funcs' in auto else ''))
    # two names at different ref addresses landing on one address: a guess went wrong
    seen, refaddr = {}, {}
    for p in (src, 'undefined_syms_auto.%s.%s.txt' % (seg, ref), 'undefined_funcs_auto.%s.%s.txt' % (seg, ref)):
        if os.path.exists(p):
            for l in open(p):
                m = SYM_RE.match(l.rstrip('\n'))
                if m:
                    refaddr.setdefault(m.group(2), int(m.group(4), 16))
    for l in body.split('\n') + extra:
        m = SYM_RE.match(l)
        if m and m.group(2) in refaddr:
            seen.setdefault(int(m.group(4), 16), {}).setdefault(refaddr[m.group(2)], []).append(m.group(2))
    for b, d in sorted(seen.items()):
        if len(d) > 1:
            print('  %s: 0x%08X <- %s' % (dst, b, '; '.join('%s 0x%08X (%s)' % (','.join(v), a, mp(a)[1])
                                                          for a, v in sorted(d.items()))), file=sys.stderr)
    out = GEN_NOTE % (other, ref, ref, other, ref) + body.rstrip('\n') + '\n\n'
    out += '// names from %s\'s undefined_syms_auto / undefined_funcs_auto (splat-detected)\n' % ref
    out += '\n'.join(extra) + '\n'
    open(dst, 'w').write(out)
    print('%s: %d names (%d not mapped)' % (dst, len(have), len(bad) + nbad))


def port_all(mp, ref, other):
    bdir, adir, sdir = 'build/' + other, 'asm/' + other, 'assets/' + other
    for seg in SEGS:
        port_yaml(mp, seg, '%s.%s.yaml' % (seg, ref), '%s.%s.yaml' % (seg, other), ref, other, bdir, adir, sdir)
        port_symbol_set(mp, seg, ref, other)
        src = 'undefined_syms.%s.%s.txt' % (seg, ref)
        dst = 'undefined_syms.%s.%s.txt' % (seg, other)
        port_syms(mp, src, dst + '.tmp', other, ref, ld=True)
        note = GEN_NOTE % (other, ref, ref, other, ref)
        note = '/* ' + note.replace('// ', '').rstrip('\n').replace('\n', '\n * ') + ' */\n'
        open(dst, 'w').write(note + open(dst + '.tmp').read())
        os.remove(dst + '.tmp')
        bss = '%s_bss.%s.ld' % (seg, ref)
        if os.path.exists(bss):
            port_ld(mp, bss, '%s_bss.%s.ld' % (seg, other), ref, other, bdir)


def report(mp, verbose):
    total = Counter()
    for s in SEGS:
        sm = mp.maps[s]
        c = Counter(f['kind'] for f in sm.funcs)
        total.update(c)
        mv = sum(f['moved'] for f in sm.funcs)
        pad = sum(1 for f in sm.funcs if f.get('padding'))
        print('%-13s text %X -> %X, data %X -> %X: %s%s%s' % (
            s, sm.ref.text, sm.oth.text, len(sm.ref.dw) * 4, len(sm.oth.dw) * 4,
            ', '.join('%s %d' % kv for kv in sorted(c.items())), ' (%d moved)' % mv if mv else '',
            ' (%d of the shifted differ in padding only)' % pad if pad else ''))
        for f in sm.funcs:
            if f['kind'] == 'changed':
                print('  changed %-32s %08X size %4X -> %s size %s' % (
                    f['name'], f['ref'], f['size'], '%08X' % f['other'] if f['other'] else '?',
                    '%X' % f['osize'] if f['osize'] is not None else '?'))
            elif f.get('padding'):
                print('  padding %-32s %08X size %4X -> %08X size %X (only the trailing nops differ)' % (
                    f['name'], f['ref'], f['size'], f['other'], f['osize']))
            elif f['moved'] and verbose:
                print('  moved   %-32s %08X -> %08X' % (f['name'], f['ref'], f['other']))
        mvd = {i1: (i2, j) for i1, i2, j in sm.moved_data}
        for i1, (i2, j) in sorted(mvd.items()):
            print('  data moved   %08X-%08X -> %08X' % (sm.ref.data_vram + i1 * 4, sm.ref.data_vram + i2 * 4,
                                                       sm.oth.data_vram + j * 4))
        for tag, i1, i2, j1, j2 in sm.dops:
            if tag != 'equal' and i1 not in mvd:
                print('  data %-7s %08X-%08X -> %08X-%08X' % (
                    tag, sm.ref.data_vram + i1 * 4, sm.ref.data_vram + i2 * 4,
                    sm.oth.data_vram + j1 * 4, sm.oth.data_vram + j2 * 4))
        if verbose:
            for tag, i1, i2, j1, j2 in sm.tops:
                if tag != 'equal':
                    print('  text %-7s %08X-%08X -> %08X-%08X' % (
                        tag, sm.ref.vram + i1 * 4, sm.ref.vram + i2 * 4,
                        sm.oth.vram + j1 * 4, sm.oth.vram + j2 * 4))
    print('total: %s' % ', '.join('%s %d' % kv for kv in sorted(total.items())))
    # the ROM files before the code: map_rom takes them to be at the same offsets
    first = min(mp.R.layout[n][0] for n in mp.R.layout if n != 'init')
    a, b = mp.R.rom[:first], mp.O.rom[:first]
    rng, i = [], 0
    while i < first:
        if a[i] != b[i]:
            j = i
            while j < first and a[j] != b[j]:
                j += 1
            if rng and i - rng[-1][1] <= 16:
                rng[-1][1] = j
            else:
                rng.append([i, j])
            i = j
        else:
            i += 1
    print('ROM before the code files (0-0x%X): %d differing ranges' % (first, len(rng)))
    offs = [o for o, n in mp.R.romfiles]
    for s, e in rng:
        k = bisect_right(offs, s) - 1
        where = 'header' if k < 0 or not mp.R.romfiles[k][1] else '%s+0x%X' % (mp.R.romfiles[k][1], s - offs[k])
        print('  0x%06X-0x%06X  %s' % (s, e, where))


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('cmd', choices=['report', 'port-syms', 'port-yaml', 'port-ld', 'port-all'])
    ap.add_argument('args', nargs='*')
    ap.add_argument('--ref', default='us.v11')
    ap.add_argument('--other', default='us.v10')
    ap.add_argument('--root', default='..')
    ap.add_argument('--elf-dir', default='build')
    ap.add_argument('-v', action='store_true')
    a = ap.parse_args()
    mp = Mapper(a.root, a.ref, a.other, a.elf_dir, a.v)
    bdir, adir, sdir = 'build/' + a.other, 'asm/' + a.other, 'assets/' + a.other
    bad = []
    if a.cmd == 'report':
        report(mp, a.v)
    elif a.cmd == 'port-syms':
        for i in range(0, len(a.args), 2):
            bad += port_syms(mp, a.args[i], a.args[i + 1], a.other, a.ref)
    elif a.cmd == 'port-yaml':
        bad += port_yaml(mp, a.args[0], a.args[1], a.args[2], a.ref, a.other, bdir, adir, sdir)
    elif a.cmd == 'port-ld':
        bad += port_ld(mp, a.args[0], a.args[1], a.ref, a.other, bdir)
    elif a.cmd == 'port-all':
        port_all(mp, a.ref, a.other)
    sys.exit(1 if bad else 0)


if __name__ == '__main__':
    main()
