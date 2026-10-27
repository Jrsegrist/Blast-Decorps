/* Small helpers the host-side C files of src/load share (level_mod.c, level_geo.c, tex_codec.c, port_mods.c):
 * an error message into the caller's buffer, a growing byte buffer, and big-endian values in byte buffers (the
 * level files, PNG and the ROM's tables as the ROM holds them).  N64 memory's own byte order is the load
 * layer's business (port_load.h, swap.c). */
#ifndef PORT_HOST_UTIL_H
#define PORT_HOST_UTIL_H

#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* printf into err[n] (nothing if err is NULL or n 0) */
static inline void seterr(char *err, size_t n, const char *fmt, ...) {
    va_list ap;
    if (err == NULL || n == 0) return;
    va_start(ap, fmt);
    vsnprintf(err, n, fmt, ap);
    va_end(ap);
}

static inline uint32_t get_be32(const uint8_t *p) {
    return (uint32_t) p[0] << 24 | (uint32_t) p[1] << 16 | (uint32_t) p[2] << 8 | p[3];
}
static inline unsigned get_be16(const uint8_t *p) {
    return (unsigned) (p[0] << 8 | p[1]);
}
static inline void put_be32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t) (v >> 24), p[1] = (uint8_t) (v >> 16), p[2] = (uint8_t) (v >> 8), p[3] = (uint8_t) v;
}
static inline void put_be16(uint8_t *p, unsigned v) {
    p[0] = (uint8_t) (v >> 8), p[1] = (uint8_t) v;
}

/* a byte buffer that grows (doubling, from 256); free(p) when done */
typedef struct {
    uint8_t *p;
    size_t n, cap;
} Buf;

/* n bytes from data (NULL: zeros) at the end; -1 if out of memory (the buffer as it was) */
static inline int buf_add(Buf *b, const void *data, size_t n) {
    if (b->n + n > b->cap) {
        size_t cap = b->cap ? b->cap : 256;
        uint8_t *q;
        while (cap < b->n + n) cap *= 2;
        q = (uint8_t *) realloc(b->p, cap);
        if (q == NULL) return -1;
        b->p = q, b->cap = cap;
    }
    if (n) {
        if (data) memcpy(b->p + b->n, data, n);
        else memset(b->p + b->n, 0, n);
    }
    b->n += n;
    return 0;
}

#endif
