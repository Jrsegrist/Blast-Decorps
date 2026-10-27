/* SHA-1 (FIPS 180-4; see sha1.h). */
#include "sha1.h"

#include <stdio.h>
#include <string.h>

#include "host_util.h"

static uint32_t rol(uint32_t x, int n) {
    return (x << n) | (x >> (32 - n));
}

static void sha1_block(uint32_t h[5], const uint8_t *p) {
    uint32_t w[80], a = h[0], b = h[1], c = h[2], d = h[3], e = h[4];
    int t;
    for (t = 0; t < 16; t++) w[t] = get_be32(p + 4 * t);
    for (t = 16; t < 80; t++) w[t] = rol(w[t - 3] ^ w[t - 8] ^ w[t - 14] ^ w[t - 16], 1);
    for (t = 0; t < 80; t++) {
        uint32_t f, k, tmp;
        if (t < 20) f = (b & c) | (~b & d), k = 0x5A827999u;
        else if (t < 40) f = b ^ c ^ d, k = 0x6ED9EBA1u;
        else if (t < 60) f = (b & c) | (b & d) | (c & d), k = 0x8F1BBCDCu;
        else f = b ^ c ^ d, k = 0xCA62C1D6u;
        tmp = rol(a, 5) + f + e + k + w[t];
        e = d, d = c, c = rol(b, 30), b = a, a = tmp;
    }
    h[0] += a, h[1] += b, h[2] += c, h[3] += d, h[4] += e;
}

void lm_sha1(const uint8_t *data, size_t len, uint8_t out[20]) {
    uint32_t h[5] = {0x67452301u, 0xEFCDAB89u, 0x98BADCFEu, 0x10325476u, 0xC3D2E1F0u};
    uint8_t last[128];
    size_t i, full = len / 64 * 64, rest = len - full, n;
    const uint64_t bits = (uint64_t) len * 8;
    for (i = 0; i < full; i += 64) sha1_block(h, data + i);
    memset(last, 0, sizeof last);
    if (rest) memcpy(last, data + full, rest);
    last[rest] = 0x80;
    n = rest + 1 + 8 <= 64 ? 64 : 128;
    for (i = 0; i < 8; i++) last[n - 1 - i] = (uint8_t) (bits >> (8 * i));
    sha1_block(h, last);
    if (n == 128) sha1_block(h, last + 64);
    for (i = 0; i < 5; i++) put_be32(out + 4 * i, h[i]);
}

void lm_sha1_hex(const uint8_t *data, size_t len, char out[41]) {
    uint8_t d[20];
    int i;
    lm_sha1(data, len, d);
    for (i = 0; i < 20; i++) snprintf(out + 2 * i, 3, "%02x", d[i]);
}
