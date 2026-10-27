#!/usr/bin/env python3
"""Inspect or edit a Blast Corps EEPROM save (4 Kbit, 512 bytes, N64 byte
order: the file mupen64plus writes as <ROM name>.eep and bc_headless.exe
writes with --eeprom).

  eepsave.py FILE                          decode and check the CRCs
  eepsave.py FILE OUT --set OFF:W=VAL ...  set a big-endian field of the player
                                           record (W = 1, 2, 4), fix its CRC,
                                           write OUT

Layout (front end pfsHandler.c, 0E7B0.c):
  0x000-0x0FF  player record (players[n]); 4-byte CRC at 0xFC, one byte per
               0x20-byte lane j: sum of __osContDataCrc(record + i*0x80 + j*0x20)
               over i (computed with the CRC bytes zeroed)
               fields: name 0-7, level 8, rank points u16 0x0A, title 0x0C,
               vehicle unlocks u32 0x10, money u32 0x14, per-level rank 0x18
               (0x3C), per-level bits 0x54, academy 0x90, state 0x91, best-time
               slot per level 0x92, vehicle flags u32 0xF0
  0x100-0x1F7  best times: per level n, u16 time at 0x100 + 4n and time ^ 0x55AA
               at 0x102 + 4n (written in 8-byte blocks, two levels each)
  0x1F8-0x1FF  semaphore u64 (0x87569AB6CD076AEC = valid, 0x2704197125121981
               = being written)
"""
import struct
import sys

GOOD = 0x87569AB6CD076AEC
BUSY = 0x2704197125121981


def data_crc(b):
    """__osContDataCrc (old SDK form) over 32 bytes"""
    t = 0
    for i in range(33):
        for j in range(7, -1, -1):
            t2 = 0x85 if t & 0x80 else 0
            t = (t << 1) & 0xFF
            if i < 32 and b[i] & (1 << j):
                t |= 1
            t ^= t2
    return t


def block_crc(data, size):
    d = bytearray(data[:size])
    d[size - 4:size] = b"\0\0\0\0"
    crc = [0, 0, 0, 0]
    for i in range((size + 0x7F) >> 7):
        for j in range(4):
            off = i * 0x80 + j * 0x20
            if off < size:
                crc[j] = (crc[j] + data_crc(d[off:off + 0x20])) & 0xFF
    return bytes(crc)


def show(e):
    rec = e[:0x100]
    name = rec[:8]
    print("player: name %r level %d points %d title %d unlocks %08X money %d state %d flags %08X" % (
        name, rec[8], struct.unpack(">H", rec[10:12])[0], rec[12], struct.unpack(">I", rec[0x10:0x14])[0],
        struct.unpack(">I", rec[0x14:0x18])[0], rec[0x91], struct.unpack(">I", rec[0xF0:0xF4])[0]))
    print("  ranks", " ".join("%d:%d" % (i, r) for i, r in enumerate(rec[0x18:0x54]) if r))
    ok = block_crc(rec, 0x100) == rec[0xFC:0x100]
    print("  CRC %s %s" % (rec[0xFC:0x100].hex(), "ok" if ok else "BAD (want %s)" % block_crc(rec, 0x100).hex()))
    times = []
    for n in range(0x3E):
        t, x = struct.unpack(">HH", e[0x100 + 4 * n:0x104 + 4 * n])
        if (t, x) != (0xFFFF, 0xFFFF):
            times.append("%d:%d%s" % (n, t, "" if t ^ 0x55AA == x else "(bad)"))
    print("  times", " ".join(times) or "-")
    sem = struct.unpack(">Q", e[0x1F8:0x200])[0]
    print("  semaphore %016X %s" % (sem, {GOOD: "valid", BUSY: "write in progress"}.get(sem, "?")))


def main():
    e = bytearray(open(sys.argv[1], "rb").read())
    if len(e) < 512:
        e += b"\xff" * (512 - len(e))
    if len(sys.argv) > 2:
        out = sys.argv[2]
        args = sys.argv[3:]
        assert args and args[0] == "--set", __doc__
        for a in [x for x in args if x != "--set"]:
            loc, val = a.split("=")
            off, w = loc.split(":")
            off, w, val = int(off, 0), int(w), int(val, 0)
            e[off:off + w] = val.to_bytes(w, "big")
        e[0xFC:0x100] = block_crc(e, 0x100)
        open(out, "wb").write(bytes(e))
    show(e)


if __name__ == "__main__":
    main()
