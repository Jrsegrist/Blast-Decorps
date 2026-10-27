"""Our own deflate encoder (RFC 1951), tuned to reproduce the ROM's members.

    deflate_raw(data, level=6) -> raw deflate stream (no zlib/gzip framing)

Every gzip member in the ROM (738 of them in either US ROM, the four code
images included) comes back byte for byte when its inflated data goes through
deflate_raw at level 6.  Python's zlib does not manage that, so this module
is a complete encoder of its own.

Clean room: written from RFC 1951/1952 and the zlib-licensed zlib sources
(deflate.c / trees.c, including the #ifdef'd TRUNCATE_BLOCK code) as the
reference for the classic algorithm; no GPL source (GNU gzip or any patched
gzip) was read or used.  Where the ROM's encoder differs from current zlib,
the behaviour below was worked out by parsing the ROM's own deflate streams
(block types and boundaries, symbols per block, the LZ77 token sequence) and
comparing them with ours.

The algorithm (zlib's deflate_slow and trees.c):

* LZ77 over a 64 KB window (2 x 32 KB; the upper half slides down when the
  scan position nears its end), 3-byte hash chains (15-bit hash, shift 5),
  lazy matching with the classic per-level table (level 6: good 8, lazy 16,
  nice 128, chain 128); a 3-byte match further back than 4096 is dropped.
* Symbols collect in a buffer; the block is Huffman coded when it ends:
  dynamic trees (heap by frequency, ties to the shallower subtree, lengths
  limited to 15/7 bits), static trees when they are no larger, or a stored
  block when that is smaller still (and the block's bytes are still in the
  window).

What the ROM's streams showed (all three are where zlib 1.x differs):

1. The symbol buffer holds 0x8000 entries: a block ends after 32767
   symbols (zlib's default memLevel 8 has 16384; memLevel 9 would also
   change the hash size).
2. Every 4096 symbols the encoder guesses whether to end the block early
   (zlib's TRUNCATE_BLOCK, but tested at multiples of 0x1000, not 0x2000):
   with out = (symbols * 8 + sum over distance codes of freq * (5 + extra
   bits)) / 8 and in = bytes covered so far, the block ends when fewer than
   half the symbols are matches and out < in / 2.  This is why the code
   images have blocks of 4096/8192/12288/24576 symbols.
3. At the end of the input the match search does not shorten `nice` to the
   bytes that are left: candidates are compared up to 258 bytes, past the
   end of the data into whatever the window holds there, and only the final
   length is cut to the bytes left.  So the last match of a member can come
   from further back than the nearest equally long one; 38 of the 738
   members depend on it.  Past the end the window holds zeros (fresh) or
   older data (once it has slid); zeroing it instead gives the same 738
   members, so the ROM doesn't tell the two apart; this keeps the window as
   it is.

Speed (pure Python, no accelerator needed): about 1.3 s for hd_code_text
(672 KB), 20 s for all 13 MB of the ROM's members.
"""

__all__ = ["deflate_raw", "CONFIG"]

# level: (good, lazy, nice, chain) -- the lazy-matching levels
CONFIG = {4: (4, 4, 16, 16), 5: (8, 16, 32, 32), 6: (8, 16, 128, 128),
          7: (8, 32, 128, 256), 8: (32, 128, 258, 1024), 9: (32, 258, 258, 4096)}

WSIZE = 1 << 15
WMASK = WSIZE - 1
HASH_SIZE = 1 << 15
HASH_MASK = HASH_SIZE - 1
H_SHIFT = 5
MIN_MATCH, MAX_MATCH = 3, 258
MIN_LOOKAHEAD = MAX_MATCH + MIN_MATCH + 1
MAX_DIST = WSIZE - MIN_LOOKAHEAD
TOO_FAR = 4096
LIT_BUFSIZE = 0x8000            # (1) above
TRUNCATE_MASK = 0xFFF           # (2) above

L_CODES, D_CODES, BL_CODES = 286, 30, 19
HEAP_SIZE = 2 * L_CODES + 1
MAX_BITS, MAX_BL_BITS = 15, 7
END_BLOCK, REP_3_6, REPZ_3_10, REPZ_11_138 = 256, 16, 17, 18

EXTRA_LBITS = [0] * 8 + [1] * 4 + [2] * 4 + [3] * 4 + [4] * 4 + [5] * 4 + [0]
EXTRA_DBITS = [0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13]
EXTRA_BLBITS = [0] * 16 + [2, 3, 7]
BL_ORDER = [16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15]


def _bi_reverse(code, n):
    r = 0
    for _ in range(n):
        r = (r << 1) | (code & 1)
        code >>= 1
    return r


def _tables():
    base_len, len_code = [], [0] * 256
    length = 0
    for code in range(28):
        base_len.append(length)
        for _ in range(1 << EXTRA_LBITS[code]):
            len_code[length] = code
            length += 1
    len_code[255] = 28                  # length 258 has a code of its own
    base_len.append(0)
    base_dist, dist_code = [], [0] * 512
    dist = 0
    for code in range(16):
        base_dist.append(dist)
        for _ in range(1 << EXTRA_DBITS[code]):
            dist_code[dist] = code
            dist += 1
    dist >>= 7
    for code in range(16, 30):
        base_dist.append(dist << 7)
        for _ in range(1 << (EXTRA_DBITS[code] - 7)):
            dist_code[256 + dist] = code
            dist += 1
    return base_len, len_code, base_dist, dist_code


BASE_LENGTH, LENGTH_CODE, BASE_DIST, DIST_CODE = _tables()
# distance - 1 -> distance code, for every distance (saves the two-level lookup)
DCODE_OF = [DIST_CODE[d] if d < 256 else DIST_CODE[256 + (d >> 7)] for d in range(WSIZE)]


def _codes_for(lengths):
    """Canonical codes (bit-reversed, ready to send LSB first) for lengths."""
    count = [0] * (MAX_BITS + 1)
    for n in lengths:
        count[n] += 1
    count[0] = 0
    nxt, code = [0] * (MAX_BITS + 1), 0
    for bits in range(1, MAX_BITS + 1):
        code = (code + count[bits - 1]) << 1
        nxt[bits] = code
    out = [0] * len(lengths)
    for i, n in enumerate(lengths):
        if n:
            out[i] = _bi_reverse(nxt[n], n)
            nxt[n] += 1
    return out


STATIC_LLEN = [8] * 144 + [9] * 112 + [7] * 24 + [8] * 8
STATIC_LCODE = _codes_for(STATIC_LLEN)
STATIC_DLEN = [5] * D_CODES
STATIC_DCODE = [_bi_reverse(n, 5) for n in range(D_CODES)]


# ------------------------------------------------------------------ Huffman

class _BitWriter:
    def __init__(self):
        self.out = bytearray()
        self.buf = 0
        self.n = 0

    def send(self, value, nbits):
        self.buf |= value << self.n
        self.n += nbits
        if self.n >= 32:
            b = self.buf
            self.out += bytes((b & 0xFF, (b >> 8) & 0xFF, (b >> 16) & 0xFF, (b >> 24) & 0xFF))
            self.buf = b >> 32
            self.n -= 32

    def flush_bytes(self):
        while self.n >= 8:
            self.out.append(self.buf & 0xFF)
            self.buf >>= 8
            self.n -= 8

    def windup(self):
        """Pad to a byte boundary."""
        self.flush_bytes()
        if self.n > 0:
            self.out.append(self.buf & 0xFF)
        self.buf = self.n = 0


def _build_tree(freq_in, elems, slen, extra, base, max_length, cost):
    """trees.c build_tree + gen_bitlen + gen_codes.  cost = [opt_len,
    static_len] (updated).  Returns (lengths, codes, max_code); lengths has
    elems + 1 entries (room for scan_tree's guard)."""
    freq = list(freq_in[:elems]) + [0] * (HEAP_SIZE - elems)
    length = [0] * HEAP_SIZE
    dad = [0] * HEAP_SIZE
    depth = [0] * HEAP_SIZE
    heap = [0] * (HEAP_SIZE + 1)
    heap_len, heap_max, max_code = 0, HEAP_SIZE, -1
    for n in range(elems):
        if freq[n]:
            heap_len += 1
            heap[heap_len] = max_code = n
    # at least two codes, so a code always has one bit at least
    while heap_len < 2:
        if max_code < 2:
            max_code += 1
            node = max_code
        else:
            node = 0
        heap_len += 1
        heap[heap_len] = node
        freq[node] = 1
        depth[node] = 0
        cost[0] -= 1
        if slen:
            cost[1] -= slen[node]

    def smaller(a, b):
        return freq[a] < freq[b] or (freq[a] == freq[b] and depth[a] <= depth[b])

    def downheap(k):
        v = heap[k]
        j = k << 1
        while j <= heap_len:
            if j < heap_len and smaller(heap[j + 1], heap[j]):
                j += 1
            if smaller(v, heap[j]):
                break
            heap[k] = heap[j]
            k = j
            j <<= 1
        heap[k] = v

    for n in range(heap_len // 2, 0, -1):
        downheap(n)
    node = elems
    while True:
        n = heap[1]
        heap[1] = heap[heap_len]
        heap_len -= 1
        downheap(1)
        m = heap[1]
        heap_max -= 1
        heap[heap_max] = n
        heap_max -= 1
        heap[heap_max] = m
        freq[node] = freq[n] + freq[m]
        depth[node] = max(depth[n], depth[m]) + 1
        dad[n] = dad[m] = node
        heap[1] = node
        node += 1
        downheap(1)
        if heap_len < 2:
            break
    heap_max -= 1
    heap[heap_max] = heap[1]

    # gen_bitlen: lengths from the tree, limited to max_length
    bl_count = [0] * (MAX_BITS + 1)
    length[heap[heap_max]] = 0
    overflow = 0
    h = heap_max + 1
    while h < HEAP_SIZE:
        n = heap[h]
        bits = length[dad[n]] + 1
        if bits > max_length:
            bits = max_length
            overflow += 1
        length[n] = bits
        h += 1
        if n > max_code:
            continue
        bl_count[bits] += 1
        xbits = extra[n - base] if n >= base else 0
        f = freq[n]
        cost[0] += f * (bits + xbits)
        if slen:
            cost[1] += f * (slen[n] + xbits)
    if overflow:
        while True:
            bits = max_length - 1
            while bl_count[bits] == 0:
                bits -= 1
            bl_count[bits] -= 1
            bl_count[bits + 1] += 2
            bl_count[max_length] -= 1
            overflow -= 2
            if overflow <= 0:
                break
        h = HEAP_SIZE
        for bits in range(max_length, 0, -1):
            n = bl_count[bits]
            while n:
                h -= 1
                m = heap[h]
                if m > max_code:
                    continue
                if length[m] != bits:
                    cost[0] += (bits - length[m]) * freq[m]
                    length[m] = bits
                n -= 1

    # gen_codes
    nxt, code = [0] * (MAX_BITS + 1), 0
    for bits in range(1, MAX_BITS + 1):
        code = (code + bl_count[bits - 1]) << 1
        nxt[bits] = code
    codes = [0] * (elems + 1)
    for n in range(max_code + 1):
        ln = length[n]
        if ln:
            codes[n] = _bi_reverse(nxt[ln], ln)
            nxt[ln] += 1
    return length[:elems + 1], codes, max_code


def _runs(lengths, max_code):
    """scan_tree/send_tree: the code-length sequence as (symbol, extra
    value, extra bits) items."""
    lens = lengths[:max_code + 1] + [0xFFFF]          # guard
    out = []
    prevlen, nextlen, count = -1, lens[0], 0
    max_count, min_count = (138, 3) if nextlen == 0 else (7, 4)
    for n in range(max_code + 1):
        curlen, nextlen = nextlen, lens[n + 1]
        count += 1
        if count < max_count and curlen == nextlen:
            continue
        if count < min_count:
            out += [(curlen, 0, 0)] * count
        elif curlen != 0:
            if curlen != prevlen:
                out.append((curlen, 0, 0))
                count -= 1
            out.append((REP_3_6, count - 3, 2))
        elif count <= 10:
            out.append((REPZ_3_10, count - 3, 3))
        else:
            out.append((REPZ_11_138, count - 11, 7))
        count, prevlen = 0, curlen
        if nextlen == 0:
            max_count, min_count = 138, 3
        elif curlen == nextlen:
            max_count, min_count = 6, 3
        else:
            max_count, min_count = 7, 4
    return out


def _flush_block(w, syms, lfreq, dfreq, stored, last):
    """Codes one block.  syms: list of (length - 3, distance) / (literal, 0);
    stored: the block's bytes, or None when they have left the window."""
    cost = [0, 0]
    llen, lcode, lmax = _build_tree(lfreq, L_CODES, STATIC_LLEN, EXTRA_LBITS, 257, MAX_BITS, cost)
    dlen, dcode, dmax = _build_tree(dfreq, D_CODES, STATIC_DLEN, EXTRA_DBITS, 0, MAX_BITS, cost)
    lruns, druns = _runs(llen, lmax), _runs(dlen, dmax)
    blfreq = [0] * BL_CODES
    for sym, _, _ in lruns + druns:
        blfreq[sym] += 1
    bcost = [cost[0], 0]
    bllen, blcode, _ = _build_tree(blfreq, BL_CODES, None, EXTRA_BLBITS, 0, MAX_BL_BITS, bcost)
    max_blindex = BL_CODES - 1
    while max_blindex >= 3 and bllen[BL_ORDER[max_blindex]] == 0:
        max_blindex -= 1
    opt_len = bcost[0] + 3 * (max_blindex + 1) + 5 + 5 + 4
    opt_lenb = (opt_len + 3 + 7) >> 3
    static_lenb = (cost[1] + 3 + 7) >> 3
    if static_lenb <= opt_lenb:
        opt_lenb = static_lenb
    send = w.send
    if stored is not None and len(stored) + 4 <= opt_lenb:
        send(last, 3)
        w.windup()
        n = len(stored)
        w.out += bytes((n & 0xFF, n >> 8, ~n & 0xFF, (~n >> 8) & 0xFF)) + stored
        if last:
            w.windup()
        return
    if static_lenb == opt_lenb:
        send(2 + last, 3)
        lc, ll, dc, dl = STATIC_LCODE, STATIC_LLEN, STATIC_DCODE, STATIC_DLEN
    else:
        send(4 + last, 3)
        send(lmax + 1 - 257, 5)
        send(dmax + 1 - 1, 5)
        send(max_blindex + 1 - 4, 4)
        for rank in range(max_blindex + 1):
            send(bllen[BL_ORDER[rank]], 3)
        for sym, val, nb in lruns + druns:
            send(blcode[sym], bllen[sym])
            if nb:
                send(val, nb)
        lc, ll, dc, dl = lcode, llen, dcode, dlen
    len_code, base_len, base_dist, dcode_of = LENGTH_CODE, BASE_LENGTH, BASE_DIST, DCODE_OF
    for a, dist in syms:
        if not dist:
            send(lc[a], ll[a])
        else:
            code = len_code[a]
            send(lc[code + 257], ll[code + 257])
            eb = EXTRA_LBITS[code]
            if eb:
                send(a - base_len[code], eb)
            dist -= 1
            code = dcode_of[dist]
            send(dc[code], dl[code])
            eb = EXTRA_DBITS[code]
            if eb:
                send(dist - base_dist[code], eb)
    send(lc[END_BLOCK], ll[END_BLOCK])
    if last:
        w.windup()


# --------------------------------------------------------------------- LZ77

def _longest_match(window, prev, strstart, cur_match, best_len, match_start, chain, good, nice, lookahead):
    """zlib's longest_match, except that nice is not cut to the lookahead
    (see (3) above).  Returns (length, match start)."""
    if best_len >= good:
        chain >>= 2
    limit = strstart - MAX_DIST if strstart > MAX_DIST else 0
    scan_end1 = window[strstart + best_len - 1]
    scan_end = window[strstart + best_len]
    s0, s1 = window[strstart], window[strstart + 1]
    while True:
        if (window[cur_match + best_len] == scan_end and window[cur_match + best_len - 1] == scan_end1
                and window[cur_match] == s0 and window[cur_match + 1] == s1):
            ln = 2
            while ln <= MAX_MATCH - 16 and window[strstart + ln:strstart + ln + 16] == window[cur_match + ln:cur_match + ln + 16]:
                ln += 16
            while ln < MAX_MATCH and window[strstart + ln] == window[cur_match + ln]:
                ln += 1
            if ln > best_len:
                match_start = cur_match
                best_len = ln
                if ln >= nice:
                    break
                scan_end1 = window[strstart + best_len - 1]
                scan_end = window[strstart + best_len]
        cur_match = prev[cur_match & WMASK]
        if cur_match <= limit:
            break
        chain -= 1
        if chain == 0:
            break
    return (best_len if best_len <= lookahead else lookahead), match_start


def deflate_raw(data, level=6):
    """A raw deflate stream (RFC 1951, no zlib/gzip framing) for data;
    level 4..9 (the lazy-matching levels; the ROM's members are level 6)."""
    if level not in CONFIG:
        raise ValueError("deflate level must be 4..9 (got %r)" % (level,))
    good, max_lazy, nice, max_chain = CONFIG[level]
    data = bytes(data)
    n = len(data)
    window = bytearray(2 * WSIZE)
    prev = [0] * WSIZE
    head = [0] * HASH_SIZE
    rd = 0
    strstart = block_start = lookahead = ins_h = 0
    match_start = prev_match = 0
    prev_length = match_length = MIN_MATCH - 1
    match_available = False
    w = _BitWriter()
    syms = []
    lfreq = [0] * L_CODES
    lfreq[END_BLOCK] = 1
    dfreq = [0] * D_CODES
    matches = 0
    len_code, dcode_of = LENGTH_CODE, DCODE_OF
    dcost = [5 + e for e in EXTRA_DBITS]

    def flush(last):
        nonlocal syms, lfreq, dfreq, matches
        stored = bytes(window[block_start:strstart]) if block_start >= 0 else None
        _flush_block(w, syms, lfreq, dfreq, stored, last)
        syms = []
        lfreq = [0] * L_CODES
        lfreq[END_BLOCK] = 1
        dfreq = [0] * D_CODES
        matches = 0

    def end_early():
        """(2): the every-4096-symbols guess."""
        nl = len(syms)
        out_length = (nl * 8 + sum(f * c for f, c in zip(dfreq, dcost))) >> 3
        return matches < nl // 2 and out_length < (strstart - block_start) // 2

    while True:
        if lookahead < MIN_LOOKAHEAD:
            # fill_window
            while True:
                more = 2 * WSIZE - lookahead - strstart
                if strstart >= WSIZE + MAX_DIST:
                    window[:WSIZE] = window[WSIZE:]
                    match_start -= WSIZE
                    strstart -= WSIZE
                    block_start -= WSIZE
                    head = [m - WSIZE if m >= WSIZE else 0 for m in head]
                    prev = [m - WSIZE if m >= WSIZE else 0 for m in prev]
                    more += WSIZE
                if rd >= n:
                    break
                k = min(more, n - rd)
                e = strstart + lookahead
                window[e:e + k] = data[rd:rd + k]
                rd += k
                lookahead += k
                if lookahead >= MIN_MATCH:
                    ins_h = ((window[strstart] << H_SHIFT) ^ window[strstart + 1]) & HASH_MASK
                if lookahead >= MIN_LOOKAHEAD or rd >= n:
                    break
            if lookahead == 0:
                break
        hash_head = 0
        if lookahead >= MIN_MATCH:
            ins_h = ((ins_h << H_SHIFT) ^ window[strstart + 2]) & HASH_MASK
            hash_head = head[ins_h]
            prev[strstart & WMASK] = hash_head
            head[ins_h] = strstart
        prev_length, prev_match = match_length, match_start
        match_length = MIN_MATCH - 1
        if hash_head and prev_length < max_lazy and strstart - hash_head <= MAX_DIST:
            match_length, match_start = _longest_match(window, prev, strstart, hash_head, prev_length, match_start,
                                                       max_chain, good, nice, lookahead)
            if match_length == MIN_MATCH and strstart - match_start > TOO_FAR:
                match_length = MIN_MATCH - 1
        if prev_length >= MIN_MATCH and match_length <= prev_length:
            max_insert = strstart + lookahead - MIN_MATCH
            dist = strstart - 1 - prev_match
            lc = prev_length - MIN_MATCH
            syms.append((lc, dist))
            matches += 1
            lfreq[len_code[lc] + 257] += 1
            dfreq[dcode_of[dist - 1]] += 1
            nl = len(syms)
            bflush = ((nl & TRUNCATE_MASK) == 0 and end_early()) or nl == LIT_BUFSIZE - 1
            lookahead -= prev_length - 1
            for _ in range(prev_length - 2):
                strstart += 1
                if strstart <= max_insert:
                    ins_h = ((ins_h << H_SHIFT) ^ window[strstart + 2]) & HASH_MASK
                    prev[strstart & WMASK] = head[ins_h]
                    head[ins_h] = strstart
            match_available = False
            match_length = MIN_MATCH - 1
            strstart += 1
            if bflush:
                flush(0)
                block_start = strstart
        elif match_available:
            c = window[strstart - 1]
            syms.append((c, 0))
            lfreq[c] += 1
            nl = len(syms)
            if ((nl & TRUNCATE_MASK) == 0 and end_early()) or nl == LIT_BUFSIZE - 1:
                flush(0)
                block_start = strstart
            strstart += 1
            lookahead -= 1
        else:
            match_available = True
            strstart += 1
            lookahead -= 1
    if match_available:
        c = window[strstart - 1]
        syms.append((c, 0))
        lfreq[c] += 1
    flush(1)
    return bytes(w.out)
