/* Raw DEFLATE decoder (RFC 1951), written for the port; used at start-up to
 * unpack the game's gzip'd code/data segments from the user's ROM. */
#include <stdint.h>
#include <stddef.h>
#include <string.h>

typedef struct {
    const uint8_t *src;
    size_t srclen, pos;
    uint32_t bitbuf;
    int bitcnt;
    uint8_t *dst;
    size_t dstmax, out;
    int err;
} State;

typedef struct {
    uint16_t count[16];   /* codes per length */
    uint16_t symbol[320]; /* symbols ordered by code */
} Huff;

static int bits(State *s, int need) {
    uint32_t v = s->bitbuf;
    while (s->bitcnt < need) {
        if (s->pos >= s->srclen) {
            s->err = 1;
            return 0;
        }
        v |= (uint32_t) s->src[s->pos++] << s->bitcnt;
        s->bitcnt += 8;
    }
    s->bitbuf = v >> need;
    s->bitcnt -= need;
    return (int) (v & ((1u << need) - 1));
}

static int decode(State *s, const Huff *h) {
    int code = 0, first = 0, index = 0, len;
    for (len = 1; len < 16; len++) {
        code |= bits(s, 1);
        int count = h->count[len];
        if (code - count < first) {
            return h->symbol[index + (code - first)];
        }
        index += count;
        first += count;
        first <<= 1;
        code <<= 1;
    }
    s->err = 2;
    return 0;
}

static void build(Huff *h, const uint8_t *lengths, int n) {
    uint16_t offs[16];
    int i;
    memset(h->count, 0, sizeof h->count);
    for (i = 0; i < n; i++) h->count[lengths[i]]++;
    h->count[0] = 0;
    offs[1] = 0;
    for (i = 1; i < 15; i++) offs[i + 1] = offs[i] + h->count[i];
    for (i = 0; i < n; i++)
        if (lengths[i]) h->symbol[offs[lengths[i]]++] = (uint16_t) i;
}

static const uint16_t LBASE[29] = {3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31,
                                   35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};
static const uint8_t LEXT[29] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2,
                                 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
static const uint16_t DBASE[30] = {1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193,
                                   257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145,
                                   8193, 12289, 16385, 24577};
static const uint8_t DEXT[30] = {0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6,
                                 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};

static void codes(State *s, const Huff *lit, const Huff *dist) {
    for (;;) {
        int sym = decode(s, lit);
        if (s->err) return;
        if (sym < 256) {
            if (s->out >= s->dstmax) { s->err = 3; return; }
            s->dst[s->out++] = (uint8_t) sym;
        } else if (sym == 256) {
            return;
        } else {
            sym -= 257;
            if (sym >= 29) { s->err = 4; return; }
            int len = LBASE[sym] + bits(s, LEXT[sym]);
            int ds = decode(s, dist);
            if (ds >= 30) { s->err = 4; return; }
            size_t d = DBASE[ds] + bits(s, DEXT[ds]);
            if (d > s->out || s->out + len > s->dstmax) { s->err = 5; return; }
            while (len--) { s->dst[s->out] = s->dst[s->out - d]; s->out++; }
        }
        if (s->err) return;
    }
}

/* ... and *used: the input bytes the stream took (it ends on a byte boundary: the bits left over in its last
 * byte are padding) */
long inflate_raw_used(const uint8_t *src, size_t srclen, uint8_t *dst, size_t dstmax, size_t *used) {
    State s;
    Huff lit, dist;
    uint8_t lengths[320];
    int last, i;
    memset(&s, 0, sizeof s);
    s.src = src; s.srclen = srclen; s.dst = dst; s.dstmax = dstmax;
    do {
        last = bits(&s, 1);
        int type = bits(&s, 2);
        if (type == 0) {
            s.bitbuf = 0; s.bitcnt = 0;
            if (s.pos + 4 > srclen) return -1;
            unsigned len = src[s.pos] | (src[s.pos + 1] << 8);
            s.pos += 4;
            if (s.pos + len > srclen || s.out + len > dstmax) return -1;
            memcpy(dst + s.out, src + s.pos, len);
            s.pos += len; s.out += len;
        } else if (type == 1) {
            for (i = 0; i < 144; i++) lengths[i] = 8;
            for (; i < 256; i++) lengths[i] = 9;
            for (; i < 280; i++) lengths[i] = 7;
            for (; i < 288; i++) lengths[i] = 8;
            build(&lit, lengths, 288);
            for (i = 0; i < 30; i++) lengths[i] = 5;
            build(&dist, lengths, 30);
            codes(&s, &lit, &dist);
        } else if (type == 2) {
            static const uint8_t ORD[19] = {16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15};
            int nlen = bits(&s, 5) + 257, ndist = bits(&s, 5) + 1, ncode = bits(&s, 4) + 4;
            Huff lencode;
            memset(lengths, 0, sizeof lengths);
            for (i = 0; i < ncode; i++) lengths[ORD[i]] = (uint8_t) bits(&s, 3);
            build(&lencode, lengths, 19);
            i = 0;
            while (i < nlen + ndist && !s.err) {
                int sym = decode(&s, &lencode), rep = 0, val = 0;
                if (sym < 16) { lengths[i++] = (uint8_t) sym; continue; }
                if (sym == 16) { if (!i) return -1; val = lengths[i - 1]; rep = 3 + bits(&s, 2); }
                else if (sym == 17) rep = 3 + bits(&s, 3);
                else rep = 11 + bits(&s, 7);
                if (i + rep > nlen + ndist) return -1;
                while (rep--) lengths[i++] = (uint8_t) val;
            }
            build(&lit, lengths, nlen);
            build(&dist, lengths + nlen, ndist);
            codes(&s, &lit, &dist);
        } else {
            return -1;
        }
        if (s.err) return -1;
    } while (!last);
    if (used) *used = s.pos;
    return (long) s.out;
}
