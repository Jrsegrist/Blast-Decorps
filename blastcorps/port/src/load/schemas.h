/* Level / packed-object / model / static-segment / demo schemas (included
 * by port_assets.c).
 *
 * These assets are blocks whose header holds s32 offsets (from the block's
 * start) of their sections: section k = [base + hdr[slot], base + hdr[end]).
 * Sections overlap (several lists share one display-list area), so every
 * swap goes through a per-block byte map and each field is swapped once.
 * Sections not handled stay big-endian: byte streams the code reads bytewise
 * (the BE16U/BE16S/BE32 macros of 5CB60.c), byte tables, texels.
 *
 * Derived from the code that reads each section (5CB60.c, 5FD50.c, 60D50.c,
 * 62740.c, 77E20.c, 4B5E0.c, 479D0.c, 48D00.c ...) and checked against the
 * widths the NON_MATCHING code reads them at in the attract demos
 * (tools/m64widths.py, `make -C port loadcheck LC_TRACE=...`).
 */

typedef struct {
    uint32_t base, len;
    uint8_t *done;   /* per byte: already swapped (or deliberately kept) */
} Block;

static int blk_ok(Block *b, uint32_t off, uint32_t w) {
    uint32_t k;
    if (off + w > b->len) return 0;
    for (k = 0; k < w; k++)
        if (b->done[off + k]) return 0;
    return 1;
}

/* swap one W-byte field at OFF (once) */
static void blk_swap(Block *b, uint32_t off, int w) {
    if (!blk_ok(b, off, (uint32_t) w)) return;
    memset(b->done + off, 1, (size_t) w);
    port_bswap_n(P(b->base + off), 1, w);
}

/* mark bytes as handled without swapping them (byte data inside a range a
 * later, coarser rule would otherwise swap) */
static void blk_keep(Block *b, uint32_t off, uint32_t n) {
    if (off < b->len) memset(b->done + off, 1, off + n <= b->len ? n : b->len - off);
}

static void blk_n(Block *b, uint32_t off, uint32_t n, int w) {
    uint32_t i;
    for (i = 0; i < n; i++) blk_swap(b, off + i * (uint32_t) w, w);
}

static void blk_records(Block *b, uint32_t lo, uint32_t hi, uint32_t stride, const char *leaves) {
    Leaves l;
    uint32_t r;
    int i;
    parse_leaves(leaves, &l);
    for (r = lo; r + stride <= hi; r += stride)
        for (i = 0; i < l.n; i++) blk_swap(b, r + l.f[i].off, l.f[i].w);
}

/* a display-list area: u32 words (once each); the runs swapped here are
 * registered with gfx_fix.c, which converts the vertices, viewports, lights
 * and texels the lists use inside them before they are drawn */
static void blk_words(Block *b, uint32_t lo, uint32_t hi) {
    uint32_t r, start = 0;
    int run = 0;
    for (r = lo; r + 4 <= hi; r += 4) {
        if (blk_ok(b, r, 4)) {
            blk_swap(b, r, 4);
            if (!run) start = r, run = 1;
        } else if (run) {
            port_gfx_word_area(b->base + start, r - start);
            run = 0;
        }
    }
    if (run) port_gfx_word_area(b->base + start, r - start);
}

static uint32_t blk_rd32(Block *b, uint32_t off) { return off + 4 <= b->len ? rd32(b->base + off) : 0; }
static uint32_t blk_be32(Block *b, uint32_t off) {
    uint8_t *p = P(b->base + off);
    return off + 4 <= b->len ? (uint32_t) p[0] << 24 | p[1] << 16 | p[2] << 8 | p[3] : 0;
}

/* section [hdr[s], hdr[e]) if sane, after the header was swapped */
static int blk_sect(Block *b, uint32_t s, uint32_t e, uint32_t *lo, uint32_t *hi) {
    *lo = blk_rd32(b, s);
    *hi = blk_rd32(b, e);
    return *lo <= *hi && *hi <= b->len;
}

/* ------------------------------------------------------------------ levels */

/* 0x2C: texture records {u32 id; u8 count @4; u8 @5, @6; s16 @8, @A; then
 * count - 1 u32 texture ids from +0xC} (lvl_resolve_tex_ids, lvl_step_tex_anims) */
static void lvl_texrecs(Block *b, uint32_t lo, uint32_t hi) {
    while (lo + 0xC <= hi) {
        uint32_t n = P(b->base + lo)[4];
        if (n == 0) break;
        blk_swap(b, lo, 4);
        blk_n(b, lo + 8, 2, 2);
        blk_n(b, lo + 0xC, n - 1, 4);
        lo += 0xC + (n - 1) * 4;
    }
}

/* 0x30: collision cells: a big-endian u32 (read bytewise, BE32: the offset of
 * the next cell from the section start), then 0x14-byte triangles (nine s16,
 * then two bytes) up to it (lvl_build_ground_grid, ground_scan_grid) */
static void lvl_cells(Block *b, uint32_t lo, uint32_t hi) {
    uint32_t p = lo;
    while (p + 4 <= hi) {
        uint32_t next = lo + blk_be32(b, p);
        blk_keep(b, p, 4);
        if (next <= p || next > hi) break;
        blk_records(b, p + 4, next, 0x14, "2@0 2@2 2@4 2@6 2@8 2@A 2@C 2@E 2@10");
        p = next;
    }
}

/* 0x3C: lifts (block_spawn_all): s16 n1, n1 x {s16 x, y, z, type}; s16 n2,
 * n2 x {s16 x, y, z; u8 type, n; s16 @8; n 0x16-byte triangle items read
 * bytewise (lvl_build_coll_tri, BE16S)} */
static void lvl_lifts(Block *b, uint32_t lo, uint32_t hi) {
    uint32_t p = lo, i, n;
    if (p + 2 > hi) return;
    blk_swap(b, p, 2);
    n = (uint16_t) rd16(b->base + p);
    p += 2;
    for (i = 0; i < n && p + 8 <= hi; i++, p += 8) blk_n(b, p, 4, 2);
    if (p + 2 > hi) return;
    blk_swap(b, p, 2);
    n = (uint16_t) rd16(b->base + p);
    p += 2;
    for (i = 0; i < n && p + 10 <= hi; i++) {
        uint32_t items = P(b->base + p)[7];
        blk_n(b, p, 3, 2);
        blk_swap(b, p + 8, 2);
        blk_keep(b, p + 10, items * 0x16);
        p += 10 + items * 0x16;
    }
}

/* 0x74: group chain (lvl_build_part_tris, lvl_groups_update_tris, lvl_group_move_part): s32 n (0 =
 * empty), then groups: {s16 next (-1 = last); s16 [9]; u16 mtxOffset[10] @0x14;
 * s32 count @0x28} + count x {s16 index[9]; u8 @0x12, @0x13; {s32 unk0;
 * s32 base[3]}[3] @0x14} */
static void lvl_groups(Block *b, uint32_t lo, uint32_t hi) {
    uint32_t p = lo + 4, n, i;
    int16_t next;
    if (lo + 4 > hi) return;
    blk_swap(b, lo, 4);
    if (rd32(b->base + lo) == 0) return;
    do {
        if (p + 0x2C > hi) return;
        blk_n(b, p, 10, 2);
        blk_n(b, p + 0x14, 10, 2);
        blk_swap(b, p + 0x28, 4);
        next = (int16_t) rd16(b->base + p);
        n = rd32(b->base + p + 0x28);
        p += 0x2C;
        for (i = 0; i < n && p + 0x44 <= hi; i++, p += 0x44) {
            blk_n(b, p, 9, 2);
            blk_keep(b, p + 0x12, 2);
            blk_n(b, p + 0x14, 12, 4);
        }
    } while (next != -1);
}

static void swap_level(uint32_t base, uint32_t len) {
    Block b;
    uint32_t lo, hi, s;
    b.base = base;
    b.len = len;
    b.done = calloc(len + 4, 1);
    /* header: u16 x 12 (grid and bounds, lvl_read_header), s32 at 0x18 and
     * 0x1C, then the s32 section offsets 0x20..0xC4 */
    blk_n(&b, 0, 12, 2);
    blk_n(&b, 0x18, (0xC8 - 0x18) / 4, 4);
    /* segment 8 (gLevelSegment8 = level + 0xC8): Vtx up to the first section */
    blk_records(&b, 0xC8, blk_rd32(&b, 0x20), 0x10, "2@0 2@2 2@4 2@6 2@8 2@A");
    if (blk_sect(&b, 0x20, 0x24, &lo, &hi)) blk_records(&b, lo, hi, 2, "2@0");   /* BoxSpawn s16 x4 */
    if (blk_sect(&b, 0x24, 0x28, &lo, &hi))                                       /* triangles */
        blk_records(&b, lo, hi, 0x14, "2@0 2@2 2@4 2@6 2@8 2@A 2@C 2@E 2@10");
    if (blk_sect(&b, 0x28, 0x2C, &lo, &hi)) blk_records(&b, lo, hi, 2, "2@0");
    if (blk_sect(&b, 0x2C, 0x30, &lo, &hi)) lvl_texrecs(&b, lo, hi);
    if (blk_sect(&b, 0x30, 0x34, &lo, &hi)) lvl_cells(&b, lo, hi);
    if (blk_sect(&b, 0x34, 0x38, &lo, &hi)) blk_records(&b, lo, hi, 2, "2@0");   /* path s16 */
    if (blk_sect(&b, 0x38, 0x3C, &lo, &hi)) blk_records(&b, lo, hi, 0xC, "2@0 2@2 2@4 2@8 2@A");
    if (blk_sect(&b, 0x3C, 0x40, &lo, &hi)) lvl_lifts(&b, lo, hi);
    for (s = 0x40; s < 0x50; s += 4)                                              /* height zones etc. */
        if (blk_sect(&b, s, s + 4, &lo, &hi)) blk_records(&b, lo, hi, 2, "2@0");
    /* 0x50, 0x54: byte streams (BE16S); 0x5C: 14-byte placements, BE16U bytes
     * but for the u16 at 0xA and 0xC; 0x60..0x74: byte grids */
    if (blk_sect(&b, 0x50, 0x58, &lo, &hi)) blk_keep(&b, lo, hi - lo);
    /* 0x58: the carrier-progress panel events, 8-byte CarrierPanelEvent records
     * {s16 carrier z; u8 a, b, c, d (hud_panel_start's arguments); u8 done; pad}.
     * 32E00.c dig_coach_update reads the s16 natively (`lh`, not BE16S): the
     * loader once left the section big-endian with the byte streams around
     * it, so levels 1 and 26 (one record each, z 2879 = 0x0B3F) fired their
     * panel at z 16139 (0x3F0B), past the carrier's route: never. */
    if (blk_sect(&b, 0x58, 0x5C, &lo, &hi)) blk_records(&b, lo, hi, 8, "2@0");
    if (blk_sect(&b, 0x5C, 0x60, &lo, &hi)) blk_records(&b, lo, hi, 0xE, "2@A 2@C");
    if (blk_sect(&b, 0x60, 0x74, &lo, &hi)) blk_keep(&b, lo, hi - lo);
    if (blk_sect(&b, 0x74, 0x78, &lo, &hi)) lvl_groups(&b, lo, hi);
    /* display lists and the visibility records/groups (0xA0..0xC0): all u32
     * words (tex_resolve_dl_impl, lvl_relocate_dls, lvl_build_visible_dl, lvl_copy_dl_tex_anim) */
    if (blk_sect(&b, 0xA0, 0xC0, &lo, &hi)) blk_words(&b, lo, hi);
    if (blk_sect(&b, 0x78, 0x84, &lo, &hi)) blk_words(&b, lo, hi);
    if (blk_sect(&b, 0x88, 0x9C, &lo, &hi)) blk_words(&b, lo, hi);
    free(b.done);
}

/* --------------------------------------------------- packed objects (5CB60) */

#define VTX_LEAVES "2@0 2@2 2@4 2@6 2@8 2@A"
#define TRI_LEAVES "2@0 2@2 2@4 2@6 2@8 2@A 2@C 2@E 2@10"

/* 0x28: texture records {u32 id; u8 count @4; u8 @5..7; u32 @8; s16 @C, @E;
 * count - 1 u32 texture ids from +0x10} (lvl_resolve_object_tex) */
static void obj_texrecs(Block *b, uint32_t lo, uint32_t hi) {
    while (lo + 0x10 <= hi) {
        uint32_t n = P(b->base + lo)[4];
        if (n == 0) break;
        blk_swap(b, lo, 4);
        blk_swap(b, lo + 8, 4);
        blk_n(b, lo + 0xC, 2, 2);
        blk_n(b, lo + 0x10, n - 1, 4);
        lo += 0x10 + (n - 1) * 4;
    }
}

/* 0x38 (obj_draw_all): s32 n, n x {s16; u8; u8}; then groups {s32 x 4
 * (offsets); s32 m; m x {s16; u8; u8}} to the end (the last group may stop
 * after its four words) */
static void obj_entries(Block *b, uint32_t *p, uint32_t hi) {
    uint32_t n, i;
    if (*p + 4 > hi) return;
    blk_swap(b, *p, 4);
    n = rd32(b->base + *p);
    *p += 4;
    for (i = 0; i < n && *p + 4 <= hi; i++, *p += 4) {
        blk_swap(b, *p, 2);
        blk_keep(b, *p + 2, 2);
    }
}

static void obj_parts(Block *b, uint32_t lo, uint32_t hi) {
    uint32_t p = lo;
    obj_entries(b, &p, hi);
    while (p + 16 <= hi) {
        blk_n(b, p, 4, 4);
        p += 16;
        obj_entries(b, &p, hi);
    }
}

/* Buildings and other level objects (ROM 0x6ECCC0.., lvl_load_object): u16 at
 * 0 and 2, bytes 4..7, s32 at 8, u16 at 0xE, s32 offsets 0x10..0x4C, Vtx
 * from 0x50 (lvl_translate_object moves them).  Read by 5CB60.c, 77E20.c. */
static void swap_object(uint32_t base, uint32_t len) {
    Block b;
    uint32_t lo, hi;
    if (len < 0x50) return;
    b.base = base;
    b.len = len;
    b.done = calloc(len + 4, 1);
    blk_n(&b, 0, 2, 2);
    blk_swap(&b, 8, 4);
    blk_swap(&b, 0xE, 2);
    blk_n(&b, 0x10, (0x50 - 0x10) / 4, 4);
    blk_records(&b, 0x50, blk_rd32(&b, 0x1C), 0x10, VTX_LEAVES);
    if (blk_sect(&b, 0x10, 0x14, &lo, &hi)) blk_words(&b, lo, hi);      /* display lists */
    if (blk_sect(&b, 0x1C, 0x24, &lo, &hi)) blk_records(&b, lo, hi, 2, "2@0");      /* corner points */
    if (blk_sect(&b, 0x24, 0x28, &lo, &hi)) blk_records(&b, lo, hi, 0x14, TRI_LEAVES);
    if (blk_sect(&b, 0x28, 0x2C, &lo, &hi)) obj_texrecs(&b, lo, hi);
    if (blk_sect(&b, 0x2C, 0x30, &lo, &hi)) blk_records(&b, lo, hi, 2, "2@0");
    /* 0x38-byte debris records (lvl_translate_object moves words 0-2 and 10;
     * obj_run_effect_queue reads the copy: u16 delay 0x2C, u16 radius 0x2E, bytes
     * 0x30.. (kind at 0x31, lvl_remap_break_fx_kinds), flag byte 0x34) */
    if (blk_sect(&b, 0x30, 0x38, &lo, &hi))
        blk_records(&b, lo, hi, 0x38, "4@0 4@4 4@8 4@C 4@10 4@14 4@18 4@1C 4@20 4@24 4@28 2@2C 2@2E");
    if (blk_sect(&b, 0x38, 0x3C, &lo, &hi)) obj_parts(&b, lo, hi);
    /* 0x3C: s16 x hdr[0] (obj_part_outranked; 77E20.c obj_draw_all's falling
     * parts read their landing heights from it as u16) */
    lo = blk_rd32(&b, 0x3C);
    blk_n(&b, lo, (uint16_t) rd16(base), 2);
    if (blk_sect(&b, 0x3C, 0x40, &lo, &hi)) blk_records(&b, lo, hi, 2, "2@0");
    if (blk_sect(&b, 0x40, 0x44, &lo, &hi)) blk_records(&b, lo, hi, 2, "2@0");
    /* 0x44: bytes; 0x48: 0x19-byte triangles read with BE16S */
    free(b.done);
}

/* Vehicle models (ROM 0x48FE90.., lvl_load_model): s32 offsets 0x00..0x44;
 * Vtx [hdr[0x14], hdr[0x18]); display lists and their tables from hdr[0x18]
 * to the end (u32 words: tex_resolve_dl_impl, veh_add_record, anim_tex_find_patches) */
/* vehicle animation block [hdr[0x10], hdr[0x14]): s16 offsets (count = the
 * first / 2, anim_init_model), each to {u8 k; k + 1 bytes; pad to a word}
 * then groups {s32; s32; k x ten s16} up to the next animation
 * (anim_update_channel, anim_apply_linear_key) */
static void mdl_anims(Block *b, uint32_t lo, uint32_t hi) {
    uint32_t n, i;
    if (lo + 2 > hi) return;
    blk_swap(b, lo, 2);
    n = (uint16_t) rd16(b->base + lo) / 2;
    if (lo + n * 2 > hi) return;
    blk_n(b, lo, n, 2);
    for (i = 0; i < n; i++) {
        uint32_t a = lo + (uint16_t) rd16(b->base + lo + i * 2), e = hi, j, k, p;
        for (j = i + 1; j < n; j++) {
            uint32_t o = lo + (uint16_t) rd16(b->base + lo + j * 2);
            if (o > a) {
                e = o;
                break;
            }
        }
        if (a + 4 > e || e > hi) continue;
        k = P(b->base + a)[0];
        /* header bytes: k, k bytes, one more, padded to a word */
        blk_keep(b, a, (k + 2 + 3) & ~3u);
        for (p = a + ((k + 2 + 3) & ~3u); p + 8 + k * 0x14 <= e; p += 8 + k * 0x14) {
            blk_n(b, p, 2, 4);
            blk_n(b, p + 8, k * 10, 2);
        }
    }
}

static void mdl_parts(Block *b, uint32_t p, uint32_t hi, uint32_t ns) {
    while (p + ns * 2 <= hi) {
        uint32_t n;
        blk_n(b, p, ns, 2);
        n = (uint16_t) rd16(b->base + p + (ns - 1) * 2);
        p += ns * 2;
        if (p + n * 4 > hi) break;
        blk_n(b, p, n, 4);
        p += n * 4;
    }
}

static void mdl_list(Block *b, uint32_t p, uint32_t hi, uint32_t ns) {
    uint32_t k, n;
    if (p + 4 > hi) return;
    blk_n(b, p, 2, 2);
    k = (uint16_t) rd16(b->base + p);
    n = (uint16_t) rd16(b->base + p + 2);
    p += 4;
    if (p + n * 4 + k * ns * 2 > hi) return;
    blk_n(b, p, n, 4);
    blk_n(b, p + n * 4, k * ns, 2);
}

static void swap_model(uint32_t base, uint32_t len) {
    Block b;
    uint32_t lo, hi;
    if (len < 0x4C) return;
    b.base = base;
    b.len = len;
    b.done = calloc(len + 4, 1);
    blk_n(&b, 0, 0x4C / 4, 4);
    /* collision/attachment parts (veh_points_transform, veh_register_parts/C454,
     * veh_deck_transform, veh_build_tris; mtx_chain_transform reads their words):
     * [hdr0, hdr4): {s16 x4, the last = n; n x s32} ...
     * [hdr4, hdr8): s16, pad, then {s16 x6, the last = n; n x s32} ...
     * [hdr8, hdrC): {s16 k; s16 n; n x s32; k x ten s16}
     * [hdrC, hdr10): the same (veh_build_tris) */
    if (blk_sect(&b, 0x00, 0x04, &lo, &hi)) mdl_parts(&b, lo, hi, 4);
    if (blk_sect(&b, 0x04, 0x08, &lo, &hi) && lo + 4 <= hi) {
        blk_n(&b, lo, 2, 2);
        mdl_parts(&b, lo + 4, hi, 6);
    }
    if (blk_sect(&b, 0x08, 0x0C, &lo, &hi)) mdl_list(&b, lo, hi, 10);
    if (blk_sect(&b, 0x0C, 0x10, &lo, &hi)) mdl_list(&b, lo, hi, 10);
    if (blk_sect(&b, 0x10, 0x14, &lo, &hi)) mdl_anims(&b, lo, hi);
    if (blk_sect(&b, 0x14, 0x18, &lo, &hi)) blk_records(&b, lo, hi, 0x10, VTX_LEAVES);
    lo = blk_rd32(&b, 0x18);
    if (lo < len) blk_words(&b, lo, len);
    free(b.done);
}

/* The attract-mode recordings (ROM 0x6A9F10, 17210.c): per demo a header
 * (u16 at 0, 2, 8; bytes 0xA, 0xB), 0x400 five-byte RecEntry (bytes), s16
 * length at 0x140C, then the vehicle's saved state (0xB2 bytes,
 * veh_serialize_state): the first 0xA6 bytes of its state block, which
 * veh_deserialize_state copies bytewise into the live block, then three words it
 * reassembles from big-endian bytes itself.  The block's fields, the same
 * for every vehicle (traced on the blocks of vehicles 0-5, 9 and 10 over
 * the attract demos; tools/m64widths.py EXTRA=...):
 * s32 x 19, s16 at 0x4C/0x4E, byte 0x50/0x51, s16 x 12 from 0x52, bytes
 * 0x6A..0x73, s16 x 17 from 0x74, bytes 0x96..0xA5. */
#define VEH_STATE_LEAVES                                                                          \
    "4@0 4@4 4@8 4@C 4@10 4@14 4@18 4@1C 4@20 4@24 4@28 4@2C 4@30 4@34 4@38 4@3C 4@40 4@44 4@48 " \
    "2@4C 2@4E 2@52 2@54 2@56 2@58 2@5A 2@5C 2@5E 2@60 2@62 2@64 2@66 2@68 2@74 2@76 2@78 2@7A "    \
    "2@7C 2@7E 2@80 2@82 2@84 2@86 2@88 2@8A 2@8C 2@8E 2@90 2@92 2@94"

static void swap_demos(uint32_t base, uint32_t len) {
    uint32_t p = 0;
    while (p + 0x140E <= len) {
        uint32_t n;
        port_bswap_n(P(base + p), 2, 2);
        port_bswap_n(P(base + p + 8), 1, 2);
        port_bswap_n(P(base + p + 0x140C), 1, 2);
        n = rd16(base + p + 0x140C);
        if (n >= 0xA6 && p + 0x140E + n <= len) swap_leaves(base + p + 0x140E, VEH_STATE_LEAVES);
        p += 0x140E + n;
    }
}

static int schema_apply(int kind, uint32_t dst, uint32_t rom, uint32_t len) {
    (void) rom;
    switch (kind) {
        case K_LEVEL:
            swap_level(dst, len);
            return 1;
        case K_PACKED:
            swap_object(dst, len);
            return 1;
        case K_MODEL:
            swap_model(dst, len);
            return 1;
        case K_DEMOS:
            swap_demos(dst, len);
            return 1;
        case K_SCENE:            /* front-end scenes: offsets, then display lists */
        case K_STATIC:           /* segment 1: display lists (RSP only) */
            port_bswap_n(P(dst), len / 4, 4);
            port_gfx_word_area(dst, len & ~3u);
            return 1;
        default:
            return 0;
    }
}
