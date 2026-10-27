#!/usr/bin/env python3
"""Post-process splat's nonmatching .s files for IDO's rodata layout.

IDO emits a file's .rodata as all string literals first (in function
order), then "late rodata" - float/double constants and jump tables - also
in function order. splat writes each GLOBAL_ASM function's rodata as one
`.section .rodata` block in address order, i.e. strings followed by
constants. asm-processor needs the constants in `.section .late_rodata`
(with `.late_rodata_alignment 8` when doubles are present), or it
mis-sizes the section and the layout breaks.

Constants that splat cannot attribute (loaded with a bare `-0xNNNN($at)`
whose `lui` is far away) can be typed in symbol_addrs.hd_code.us.v11.txt
with `// type:f32` or `// type:f64`. Every typed constant is then placed
in the late rodata of the first function that loads it (by %lo(sym) or by
a matching bare $at offset), with its value read from the ROM, in address
order.

usage: late_rodata.py asm/nonmatchings/hd_code/FILE [...]
Idempotent: files already containing .late_rodata are left alone.
"""
import re
import sys
import glob
import os
import struct

# Late = float/double constants and jump tables (.word <label>). A numeric
# .word is a short string (splat writes "in\0\0" as .word 0x696E0000).
# If late data seems to sit between strings, the splat "file" is really two
# original files (26570 was yoshi.c + the scheduler): split it.
LATE = re.compile(r'^\s*\.(float|double)\b|^\s*\.word\s+[A-Za-z_.]')

SYMS = 'symbol_addrs.hd_code.us.v11.txt'
ROM = 'hd_code.us.v11.bin'
ROM_OFF = 0xA4410 - 0x802E8BD0  # hd_code rodata vaddr -> offset in ROM
RAW = re.compile(r'\b(lwc1|ldc1)\s+\$f\d+, (-?0x[0-9a-fA-F]+|\d+)\(\$at\)')


def load_typed():
    """name -> (addr, 'f32'|'f64') for typed data symbols."""
    typed = {}
    if not os.path.exists(SYMS):
        return typed
    for line in open(SYMS):
        m = re.match(r'\s*(\w+)\s*=\s*0x([0-9A-Fa-f]+);.*type:(f32|f64)\b', line)
        if m:
            typed[m.group(1)] = (int(m.group(2), 16), m.group(3))
    return typed


def const_block(name, addr, kind, rom):
    off = addr + ROM_OFF
    if kind == 'f64':
        v = struct.unpack('>d', rom[off:off + 8])[0]
        return 'glabel %s\n.double %r\n\n' % (name, v)
    v = struct.unpack('>f', rom[off:off + 4])[0]
    return 'glabel %s\n.float %r\n\n' % (name, v)


def block_addr(b):
    m = re.match(r'glabel \w*?([0-9A-F]{8})\b', b)
    return int(m.group(1), 16) if m else None


def fix(path, typed, rom, emitted):
    s = open(path).read()
    if '.late_rodata' in s:
        return False
    # typed constants this function loads
    own = set()
    for m in re.finditer(r'%lo\((\w+)\)', s):
        if m.group(1) in typed:
            own.add(m.group(1))
    for m in RAW.finditer(s):
        off = int(m.group(2), 0)
        c = [n for n, (a, k) in typed.items() if ((a & 0xFFFF) ^ 0x8000) - 0x8000 == off]
        if len(c) == 1:
            own.add(c[0])
    own = sorted((n for n in own if n not in emitted), key=lambda n: typed[n][0])
    emitted.update(own)
    if '.section .rodata' not in s and not own:
        return False
    if '.section .rodata' in s:
        head, sep, rest = s.partition('.section .rodata')
    else:
        head, sep, rest = '', '.section .rodata', '\n' + s
    # rodata block ends at the next .section directive (normally .text)
    m = re.search(r'^\.section \.text', rest, re.M)
    ro, tail = (rest[:m.start()], rest[m.start():]) if m else (rest, '')
    if m is None and not head:
        ro, tail = '\n', '\n.section .text\n' + rest
    blocks = re.split(r'(?=^glabel )', ro, flags=re.M)
    early, late = [], []
    for b in blocks:
        name = b.split()[1] if b.startswith('glabel') else None
        if name in typed:
            continue  # regenerated below, in the function that loads it
        body = [l for l in b.split('\n')[1:] if l.strip()]
        is_late = b.startswith('glabel') and body and LATE.match(body[0])
        is_str = b.startswith('glabel') and body and re.match(r'^\s*\.(ascii|asciz)\b', body[0])
        if is_late or (late and not is_str):
            late.append(b)
        else:
            early.append(b)
    if own:
        late += [const_block(n, typed[n][0], typed[n][1], rom) for n in own]
        if all(block_addr(b) is not None for b in late):
            late.sort(key=block_addr)
    if not late:
        if not own and ''.join(early) == ro:
            return False
        out = head + sep + ''.join(early) + tail
        open(path, 'w').write(out)
        return True
    align = ''
    if any('.double' in b for b in late):
        # the block's first constant tells where it really starts: a leading
        # .float at 4 mod 8 means the block is only 4-aligned
        start = block_addr(late[0]) or 0
        align = '.late_rodata_alignment %d\n' % (4 if start % 8 else 8)
    out = head + sep + ''.join(early)
    out += '\n.section .late_rodata\n' + align + '\n' + ''.join(late)
    out += tail
    open(path, 'w').write(out)
    return True


def main():
    typed = load_typed()
    rom = open(ROM, 'rb').read() if typed and os.path.exists(ROM) else b''
    n = 0
    for d in sys.argv[1:]:
        emitted = set()
        for p in sorted(glob.glob(os.path.join(d, '*.s'))):
            n += fix(p, typed, rom, emitted)
    print('late_rodata: rewrote %d file(s)' % n)


if __name__ == '__main__':
    main()
