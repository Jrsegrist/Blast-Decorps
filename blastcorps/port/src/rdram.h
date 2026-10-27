/* Fixed-RAM model: the N64's 8 MB of RDRAM mapped at 0x80000000 (KSEG0)
 * in a 32-bit large-address-aware process, so every N64 address the game
 * uses (absolute symbols, literals, pointers inside data) is a valid host
 * pointer with no translation. */
#ifndef PORT_RDRAM_H
#define PORT_RDRAM_H

#include <stddef.h>
#include <stdint.h>

#define RDRAM_BASE 0x80000000u
#define RDRAM_SIZE 0x00800000u

/* hd_code: data+rodata image, then .bss up to the end of the 4 MB bank */
#define HD_DATA_VRAM 0x802E8BD0u
#define HD_DATA_SIZE 0x00026A90u
#define HD_BSS_START 0x8030F660u
#define HD_BSS_END   0x80400000u
/* front end overlay: text (and its RSP ucode) then data+rodata, then .bss */
#define FE_VRAM      0x801E7000u
#define FE_DATA_VRAM 0x80208040u
#define FE_DATA_END  0x80210E90u   /* end of .data+.rodata (start of .bss) */
#define FE_BSS_END   0x8021ED00u

/* compressed segments in the ROM (gzip members; tools_port/romsplit.py) */
#define ROM_HD_TEXT  0x787FD0u
#define ROM_HD_DATA  0x7D73B4u
#define ROM_FE_TEXT  0x7E3AD0u
#define ROM_FE_DATA  0x7F63A1u

typedef struct {
    uint32_t addr;
    const char *start, *end;
    const char *name;
    uint32_t n64_size; /* st_size in the NON_MATCHING ELF (0 = unknown) */
} PortCopy;

extern const PortCopy port_copytab[];

/* the N64 value of each pointer slot of the pinned C objects (their native
 * initialisers hold 0 there: N64_DPTR; gensyms.py ptrtab) */
typedef struct {
    uint32_t addr, value;
} PortPtrSlot;
extern const PortPtrSlot port_ptrtab[];

/* Reserve+commit RDRAM at 0x80000000.  Returns 0 on success. */
int rdram_map(void);
/* Read the ROM file, inflate the data segments into place, zero .bss,
 * copy the native initialisers of pinned objects.  Returns 0 on success. */
int rdram_load(const char *rom_path);

uint8_t *rom_bytes(size_t *size);

/* why rdram_map/rdram_load failed (a sentence for the user) */
const char *rdram_error(void);

/* a ROM image in place into .z64 (big-endian) order: returns the format it
 * was in (.z64; .v64 = bytes swapped in pairs; .n64 = little-endian
 * words), or -1 if the header isn't an N64 ROM's.  rdram_load does this. */
enum { ROM_Z64, ROM_V64, ROM_N64 };
int rom_normalise(uint8_t *p, size_t n);

/* gunzip one member at (original) ROM offset OFF into DST (max DSTMAX); returns size or -1 */
long rom_gunzip(uint32_t off, void *dst, size_t dstmax);
/* ... at this ROM's offset OFF (rom_phys), and *next: the offset after the member (its 8-byte trailer), where a
 * following member starts */
long rom_gunzip_next(uint32_t off, void *dst, size_t dstmax, uint32_t *next);

/* ROMs from the shiftable build with files moved (docs/shiftable.md "The port"; rdram.c): the port's ROM
 * offsets are the original ROM's, the ROM's map turns them into this ROM's.  Without a map (the retail ROM)
 * every function here returns its argument. */
#define ROM_TEX_TABLE   0x004CE0u          /* the texture table (offsets from itself) */
#define ROM_OBJ_TABLE   0x6EC4C0u          /* the packed-object table (offsets from itself) */
#define ROM_PHYS_WINDOW 0x0C000000u        /* offsets from here are this ROM's own (port_rom_span, rom_virt) */
int rom_map_active(void);
uint32_t rom_phys(uint32_t v);             /* where a read from original offset V starts */
uint32_t rom_phys_end(uint32_t v);         /* where a read that ends at original offset V ends */
uint32_t rom_virt(uint32_t p);             /* the original offset of this ROM's P (a file's start) */
/* 1: ROM image P (N bytes, .z64 order) is a shift build the port runs (MSG: what it is); 0: no ROM map;
 * -1: a map, but the port can't run it (MSG: why) */
int rom_shift_check(const uint8_t *p, size_t n, char *msg, size_t msglen);
/* a ROM from the shiftable build made before it wrote its map (files moved, nothing says where: refused, rdram.c):
 * where its game data image is (>= 0), -1 if it isn't one (a ROM with a map, the retail layout, not Blast Corps) */
long rom_unmapped_shift(const uint8_t *rom, size_t size);
#define ROM_UNMAPPED_SHIFT_MSG                                                                                      \
    "it is Blast Corps (USA) (Rev 1) with its files moved (a ROM from the shiftable build), but it has no ROM map: "  \
    "it was built before the shift build wrote one (2026-10-25), and nothing in it says where the files went, so "   \
    "the port can't run it.  Build it again with the current tools (make SHIFT=1, or bcfiles mod-export for a "      \
    "mod): their ROMs carry the map and run here.  Or play the mod itself (--mod / the mods folder)."
/* (platform/os_hw.c, hd_rom_load: plat.h) */
unsigned int rom_map_dma(unsigned int off, unsigned int n, unsigned int *phys);
void rom_map_fixup(void *dst, unsigned int off, unsigned int n);
unsigned int rom_map_virt(unsigned int off);
void port_rom_span(unsigned int *rom, unsigned int *size);

/* in inflate.c: raw deflate */
long inflate_raw_used(const uint8_t *src, size_t srclen, uint8_t *dst, size_t dstmax, size_t *used);

#endif
