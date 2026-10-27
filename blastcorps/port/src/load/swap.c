/* Byte-swap primitives and the data-image swap (see port_load.h). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "platform/port_cc.h"
#include "rdram.h"
#include "load/port_load.h"

/* Per RDRAM byte, the unit the host holds it in (see port_load.h
 * port_n64_byte): (offset in the unit << 4) | width, 0 = never recorded. */
static uint8_t unit_map[0x800000];

/* the front end's data image is put back from a snapshot at every reload
 * (plat_core.c): its units with it */
void *port_unit_save(uint32_t addr, uint32_t n) {
    void *s = malloc(n);
    if (s != NULL) memcpy(s, unit_map + (addr - 0x80000000u), n);
    return s;
}

void port_unit_restore(uint32_t addr, const void *s, uint32_t n) {
    if (s != NULL) memcpy(unit_map + (addr - 0x80000000u), s, n);
}

void port_unit_mark(void *p, uint32_t n, int width) {
    uint32_t a = (uint32_t) (uintptr_t) p, i;
    int k;
    if (a < 0x80000000u || a + n * (uint32_t) width > 0x80800000u) return;
    a -= 0x80000000u;
    for (i = 0; i < n; i++, a += (uint32_t) width)
        for (k = 0; k < width; k++) unit_map[a + k] = (uint8_t) (k << 4 | width);
}

/* A graphics task's display lists (dl: physical address): every command
 * they reach is two host-order words, every matrix 16 words and every
 * vertex the native Vtx (six halfwords, four bytes), so record those units
 * for port_n64_byte.  The game reuses heap memory that held display lists
 * (e.g. the front end's globe lists under level 1's collision records, whose
 * never-written byte 0x51 then is a byte of a big-endian Gfx word on the
 * N64).  Follows G_DL calls/branches with the list's own segment table. */
void port_mark_gfx_task(uint32_t dl) {
    uint32_t seg[16], stack[10], pc = dl & 0x7FFFFFu;
    int sp = 0, steps = 0;
    memset(seg, 0, sizeof seg);
#define MSEG(a) ((seg[((a) >> 24) & 15] + ((a) & 0x00FFFFFFu)) & 0x7FFFFFu)
    while (steps++ < 200000 && pc + 8 <= 0x800000u) {
        uint32_t w0, w1;
        uint8_t *p = (uint8_t *) (uintptr_t) (0x80000000u + pc);
        memcpy(&w0, p, 4);
        memcpy(&w1, p + 4, 4);
        /* (lists in the data images' raw areas are ROM bytes: bc_headless
         * never converts them; the commands below are read as bc.exe's
         * renderer would after converting) */
        if (port_gfx_in_raw(pc, &w0)) {
            port_gfx_in_raw(pc + 4, &w1);
        } else {
            port_unit_mark(p, 2, 4);
        }
        pc += 8;
        switch (w0 >> 24) {
            case 0x01:   /* G_MTX */
                if (!port_gfx_in_raw(MSEG(w1), NULL))
                    port_unit_mark((void *) (uintptr_t) (0x80000000u + MSEG(w1)), 16, 4);
                break;
            case 0x04: { /* G_VTX */
                uint32_t a = MSEG(w1), n = (w0 & 0xFFFF) / 16, i;
                for (i = 0; i < n && a + 16 <= 0x800000u && !port_gfx_in_raw(a, NULL); i++, a += 16) {
                    port_unit_mark((void *) (uintptr_t) (0x80000000u + a), 6, 2);
                    port_unit_mark((void *) (uintptr_t) (0x80000000u + a + 12), 4, 1);
                }
                break;
            }
            case 0x06:   /* G_DL */
                if (((w0 >> 16) & 1) == 0) {
                    if (sp == 10) return;
                    stack[sp++] = pc;
                }
                pc = MSEG(w1);
                break;
            case 0xB8:   /* G_ENDDL */
                if (sp == 0) return;
                pc = stack[--sp];
                break;
            case 0xBC:   /* G_MOVEWORD G_MW_SEGMENT */
                if ((w0 & 0xFF) == 6) seg[((w0 >> 8) & 0xFFFF) / 4 & 15] = w1 & 0x7FFFFFu;
                break;
            default:
                break;
        }
    }
#undef MSEG
}

void port_garbage(const void *p, uint32_t len) {
    if (port_load_verbose)
        fprintf(stderr, "load: garbage %08X len %X\n", (unsigned) (uintptr_t) p, (unsigned) len);
}

/* N Vtx whose bytes 6-15 (flag, s, t, colour) nobody reads (cull boxes:
 * gSPCullDisplayList takes the positions only): garbage for the comparator,
 * logged once per record until a load replaces the memory (bit 7 of the
 * unit map at the flag's first byte; port_unit_mark clears it) */
void port_vtx_unread(void *v, uint32_t n) {
    uint32_t a = (uint32_t) (uintptr_t) v, i;
    if (a < 0x80000000u || a + n * 16 > 0x80800000u) return;
    for (i = 0; i < n; i++, a += 16) {
        uint8_t *m = &unit_map[a - 0x80000000u + 6];
        if (*m & 0x80) continue;
        *m |= 0x80;
        port_garbage((void *) (uintptr_t) (a + 6), 10);
    }
}

uint8_t port_n64_byte(const void *p) {
    uint32_t a = (uint32_t) (uintptr_t) p;
    const uint8_t *b = p;
    int m, w, k;
    if (a < 0x80000000u || a >= 0x80800000u) return *b;
    m = unit_map[a - 0x80000000u];
    w = m & 15;
    k = (m >> 4) & 7;
    if (w < 2) return *b;
    return b[w - 1 - 2 * k];
}

/* Store the byte the N64 would hold at P (the inverse of port_n64_byte):
 * where P lies in a recorded host-order unit, the byte goes to its host
 * position in that unit. */
void port_n64_store_byte(void *p, uint8_t v) {
    uint32_t a = (uint32_t) (uintptr_t) p;
    uint8_t *b = p;
    int m, w, k;
    if (a < 0x80000000u || a >= 0x80800000u) {
        *b = v;
        return;
    }
    m = unit_map[a - 0x80000000u];
    w = m & 15;
    k = (m >> 4) & 7;
    if (w < 2) *b = v;
    else b[w - 1 - 2 * k] = v;
}

/* N Vtx records the game builds in fresh heap memory without writing every
 * field (42240.c's HUD quads write ob and tc only): the N64 keeps whatever
 * the heap held in the rest, and the RSP reads the colour bytes.  Give the
 * native records those stale bytes in the native Vtx layout (halfwords in
 * host order, colour bytes as on the N64) before the game writes its fields. */
void port_vtx_stale(void *v, uint32_t n) {
    uint8_t *p = v, b[16];
    uint32_t i;
    int k;
    for (i = 0; i < n; i++, p += 16) {
        for (k = 0; k < 16; k++) b[k] = port_n64_byte(p + k);
        for (k = 0; k < 6; k++) {
            uint16_t h = (uint16_t) (b[2 * k] << 8 | b[2 * k + 1]);
            memcpy(p + 2 * k, &h, 2);
        }
        memcpy(p + 12, b + 12, 4);
        port_unit_mark(p, 6, 2);
        port_unit_mark(p + 12, 4, 1);
        /* the flag halfword: nobody reads it (F3D ignores it), and what the
         * heap held there is only as good as the units recorded for it (game
         * C's own stores aren't): tell the comparator */
        port_garbage(p + 6, 2);
    }
}

void port_bswap_n(void *p, uint32_t n, int width) {
    uint8_t *b = p;
    uint32_t i;
    port_unit_mark(p, n, width);
    switch (width) {
        case 2:
            for (i = 0; i < n; i++, b += 2) {
                uint16_t v;
                memcpy(&v, b, 2);
                v = PORT_BSWAP16(v);
                memcpy(b, &v, 2);
            }
            break;
        case 4:
            for (i = 0; i < n; i++, b += 4) {
                uint32_t v;
                memcpy(&v, b, 4);
                v = PORT_BSWAP32(v);
                memcpy(b, &v, 4);
            }
            break;
        case 8:
            for (i = 0; i < n; i++, b += 8) {
                uint64_t v;
                memcpy(&v, b, 8);
                v = PORT_BSWAP64(v);
                memcpy(b, &v, 8);
            }
            break;
    }
}

uint32_t port_swap_runs(const PortSwapRun *r) {
    uint32_t bytes = 0;
    for (; r->count; r++) {
        port_bswap_n((void *) (uintptr_t) r->addr, r->count, (int) r->width);
        bytes += r->count * r->width;
    }
    return bytes;
}

/* The parts of image [lo, hi) left as ROM bytes: neither swapped by the
 * table nor replaced by a native initialiser.  gfx_fix.c converts the
 * display lists, vertices and matrices the RSP finds there (port_load.h). */
static void register_raw(uint32_t lo, uint32_t hi, const PortSwapRun *runs) {
    uint8_t *typed = calloc(hi - lo, 1);
    const PortCopy *c;
    uint32_t a, start = 0;
    int raw = 0;
    if (typed == NULL) return;
    for (; runs->count; runs++) {
        uint32_t s = runs->addr, e = runs->addr + runs->count * runs->width;
        for (a = s < lo ? lo : s; a < e && a < hi; a++) typed[a - lo] = 1;
    }
    for (c = port_copytab; c->name; c++) {
        uint32_t e = c->addr + (uint32_t) (c->end - c->start);
        for (a = c->addr < lo ? lo : c->addr; a < e && a < hi; a++) typed[a - lo] = 1;
    }
    for (a = lo; a <= hi; a++) {
        int t = a == hi || typed[a - lo];
        if (!t && !raw) start = a, raw = 1;
        else if (t && raw) {
            if (a - start >= 8) port_gfx_raw_area(start, a - start);
            raw = 0;
        }
    }
    free(typed);
}

void port_load_image_hd(void) {
    port_swap_runs(port_swap_hd);
    register_raw(HD_DATA_VRAM, HD_DATA_VRAM + HD_DATA_SIZE, port_swap_hd);
}

void port_load_image_fe(void) {
    const PortCopy *c;
    port_swap_runs(port_swap_fe);
    /* the front end's own C data: native initialisers (the N64 reload resets
     * them too: the heap overwrites the overlay while a level runs) */
    for (c = port_copytab; c->name; c++)
        if (c->addr >= FE_DATA_VRAM && c->addr < FE_BSS_END)
            memcpy((void *) (uintptr_t) c->addr, c->start, c->end - c->start);
    register_raw(FE_DATA_VRAM, FE_DATA_END, port_swap_fe);
}
