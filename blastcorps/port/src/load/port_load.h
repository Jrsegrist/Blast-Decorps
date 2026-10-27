/* Byte order on load (stage 3, "load").
 *
 * The RDRAM image is kept in host (little-endian) order: everything the
 * game's C reads natively must be swapped once, when it arrives from the ROM,
 * by the width the code reads it at.  Two entry points:
 *
 *  - the data images (hd_code .data/.rodata at 0x802E8BD0, the front end's at
 *    0x80208040): swapped by a table generated at build time
 *    (port/tools/swaptab.py: C declarations + traced read widths), right after
 *    they are inflated and before the native initialisers are copied in;
 *  - runtime assets: port_on_dma (every PI DMA, from the platform layer) and
 *    port_on_load (after the game decompresses a block: hd_rom_load and
 *    5CB60's packed-object loader), which look the ROM range up in the asset
 *    table (port_assets.c) and apply that asset type's schema.
 *
 * Textures, palettes, compressed streams and the byte streams the code reads
 * with BE16U/BE16S/BE32 or byte loads stay big-endian.
 */
#ifndef PORT_LOAD_H
#define PORT_LOAD_H

#include <stdint.h>

typedef struct {
    uint32_t addr;   /* N64 address of the first element */
    uint32_t count;  /* elements */
    uint32_t width;  /* 2, 4 or 8 */
} PortSwapRun;

/* generated (build/swaptab.c) */
extern const PortSwapRun port_swap_hd[];
extern const PortSwapRun port_swap_fe[];

/* swap N elements of WIDTH (2, 4, 8) bytes at host pointer P */
void port_bswap_n(void *p, uint32_t n, int width);
/* The game reads a few never-initialised heap bytes (5CB60.c triangle
 * records), which on the N64 hold a big-endian byte of what was there
 * before.  Every swap records the unit it swapped (port_unit_mark, width 1 =
 * kept big-endian); port_n64_byte gives the byte the N64 would hold at P. */
void port_unit_mark(void *p, uint32_t n, int width);
void *port_unit_save(uint32_t addr, uint32_t n);
void port_unit_restore(uint32_t addr, const void *saved, uint32_t n);
uint8_t port_n64_byte(const void *p);
void port_n64_store_byte(void *p, uint8_t v);  /* its inverse */
void port_vtx_unread(void *v, uint32_t n);      /* cull-box Vtx: bytes 6-15 unread (garbage) */
/* ... and the units of a graphics task's display lists, matrices and
 * vertices (dl: physical address), from the platform's osSpTaskStartGo */
void port_mark_gfx_task(uint32_t dl);
/* N Vtx built in fresh heap memory with fields left unwritten: the N64's
 * stale bytes in the native Vtx layout (42240.c) */
void port_vtx_stale(void *v, uint32_t n);
/* bytes nothing writes or reads (record padding): with --load-log, logged
 * for the comparison (port/tools/compare.py skips them until reloaded) */
void port_garbage(const void *p, uint32_t len);
/* apply a run table (terminated by count 0); returns the bytes swapped */
uint32_t port_swap_runs(const PortSwapRun *runs);

/* After the hd_code data image is inflated (once, at boot). */
void port_load_image_hd(void);
/* After the front end is inflated to 0x801E7000 (boot and every reload):
 * swaps its data image, then copies the front end's native initialisers. */
void port_load_image_fe(void);

/* After the game decompressed ROM block ROM into DST (LEN bytes out). */
void port_on_load(uint32_t rom, uint32_t dst, uint32_t len);
/* (port/src/platform/port_dma.h) after every ROM -> RDRAM DMA. */
void port_on_dma(uint32_t dst, uint32_t rom, uint32_t len);

/* Graphics data inside display-list areas (gfx_fix.c).  The schemas swap a
 * display-list area of an asset as u32 words (the CPU walks and patches the
 * commands as words), but such an area can also hold vertices, viewports,
 * lights and texels: as words they end up in the emulator's layout, while the
 * renderer (RT64 in RT64_NATIVE_HOST_LAYOUT) reads host-order Vtx fields and
 * big-endian texels like everywhere else.  The schemas register the areas
 * they swapped by words; before the renderer runs a task,
 * port_gfx_fix_task walks its display lists (F3D: segments, sub-lists) and
 * converts whatever those areas hold besides commands to the native layout,
 * once per load.  Only the windowed build calls it. */
void port_gfx_word_area(uint32_t addr, uint32_t len);
int port_gfx_in_raw(uint32_t addr, uint32_t *word_as_loaded);
/* the same for parts of the data images left as ROM bytes (swap.c) */
void port_gfx_raw_area(uint32_t addr, uint32_t len);
/* every load/DMA: areas overlapping the new data are forgotten */
void port_gfx_forget(uint32_t addr, uint32_t len);
/* walk the display list at physical address DL; returns the bytes converted */
uint32_t port_gfx_fix_task(uint32_t dl);
/* 1: log words left alone because the game changed them since the load */
extern int port_gfx_debug;

/* Statistics: per asset kind, how many loads and bytes were handled. */
void port_load_report(void);
/* 1: log every DMA/load and the kind it was classified as (stderr). */
extern int port_load_verbose;

#endif
