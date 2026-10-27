"""Rare's LZSS (hd_rom_load method 2; lzss_decode in src.us.v11/hd_code/7F8B0.c).

Bits are read most significant first.  A 1 bit is followed by an 8-bit
literal; a 0 bit by a `bits`-bit window position (0 ends the stream) and a
(16 - bits)-bit length L, which copies L + 3 bytes from the ring window,
one byte at a time (so a copy may read what it just wrote).  The window is
2**bits bytes and writing starts at index 1.  The game passes bits = 10
(static segment) or 13 (sound banks, front-end pictures).  After the end
marker the reader rounds its source pointer up to an even address.

The window starts as whatever was in RAM; the game's data never reads a
window byte before writing it (or the result would depend on RAM), so
zeros are used here.
"""


class BitReader:
    def __init__(self, data, pos=0):
        self.d = data
        self.p = pos
        self.cur = 0
        self.bit = 0x80

    def read(self, nbits):
        v = 0
        for _ in range(nbits):
            if self.bit == 0x80:
                self.cur = self.d[self.p]
                self.p += 1
            v = (v << 1) | (1 if self.cur & self.bit else 0)
            self.bit >>= 1
            if self.bit == 0:
                self.bit = 0x80
        return v


def decompress(data, bits, pos=0):
    """Returns (output bytes, source end offset as the game leaves it (even))."""
    if not 1 <= bits <= 15:
        raise ValueError("bits must be 1..15")
    wmask = (1 << bits) - 1
    win = bytearray(1 << bits)
    wp = 1
    out = bytearray()
    r = BitReader(data, pos)
    while True:
        if r.read(1):
            c = r.read(8)
            out.append(c)
            win[wp] = c
            wp = (wp + 1) & wmask
        else:
            off = r.read(bits)
            if off == 0:
                break
            n = r.read(16 - bits) + 3
            for i in range(n):
                c = win[(off + i) & wmask]
                out.append(c)
                win[wp] = c
                wp = (wp + 1) & wmask
    end = r.p
    if end & 1:
        end += 1
    return bytes(out), end


class BitWriter:
    def __init__(self):
        self.out = bytearray()
        self.cur = 0
        self.n = 0

    def write(self, v, nbits):
        for i in range(nbits - 1, -1, -1):
            self.cur = (self.cur << 1) | ((v >> i) & 1)
            self.n += 1
            if self.n == 8:
                self.out.append(self.cur)
                self.cur = self.n = 0

    def finish(self):
        if self.n:
            self.out.append(self.cur << (8 - self.n))
            self.cur = self.n = 0
        if len(self.out) & 1:
            self.out.append(0)
        return bytes(self.out)


def compress(data, bits, max_chain=256):
    """An LZSS stream lzss_decode turns back into data (greedy matching over
    the ring window; not the original encoder, so not byte-identical)."""
    size = 1 << bits
    wmask = size - 1
    maxlen = (1 << (16 - bits)) - 1 + 3
    w = BitWriter()
    heads = {}                    # 3-byte key -> list of data positions
    i = 0
    n = len(data)
    while i < n:
        best_len, best_off = 0, 0
        if i + 3 <= n:
            key = bytes(data[i:i + 3])
            for j in reversed(heads.get(key, [])[-max_chain:]):
                if i - j >= size - maxlen:      # keep the source inside the window, clear of the write pointer
                    continue
                # window position of data[j] is (1 + j) & wmask; the copy may overlap its own output
                L = 0
                while L < maxlen and i + L < n and data[j + L] == data[i + L]:
                    L += 1
                if L > best_len:
                    best_len, best_off = L, (1 + j) & wmask
                    if L == maxlen:
                        break
        if best_len >= 3 and best_off != 0:
            w.write(0, 1)
            w.write(best_off, bits)
            w.write(best_len - 3, 16 - bits)
            step = best_len
        else:
            w.write(1, 1)
            w.write(data[i], 8)
            step = 1
        for k in range(step):
            if i + k + 3 <= n:
                heads.setdefault(bytes(data[i + k:i + k + 3]), []).append(i + k)
        i += step
    w.write(0, 1)
    w.write(0, bits)
    return w.finish()
