#include "common.h"
#include <ultra64.h>
#include "game/game.h"

/* Front-end loader: DMAs a (possibly compressed) block from ROM in 16 KB
 * pieces and decompresses it. */

extern s32 * N64P gFeRomStartPtr; /* ROM start of the front end */
extern s32 * N64P gFeRomEndPtr; /* ROM end of the front end */

#if defined(NON_MATCHING) && defined(PORT_HOST)
/* Windows port: byte order on load (port/src/load/port_load.h) */
void port_on_load(u32 rom, u32 dst, u32 len);
#endif


/* Blanks the screen; if the front end is not loaded yet, loads and inflates it to 0x801E7000, clears the rest
 * of its 0x37D00-byte area and starts the pak thread ("got front end") (0x8028B3E0).
 * Load the front end overlay to 0x801E7000 once */
void hd_load_front_end(void) {
    u32 size = *gFeRomEndPtr - *gFeRomStartPtr;

    osViBlack(1);
    if (frontEndPresent == 0) {
        osInvalDCache((void *) 0x801E7000, 0x37D00);
        osInvalICache((void *) 0x801E7000, 0x37D00);
        hd_rom_load(*gFeRomStartPtr, 0x801E7000, &size, 0xD, 10, 1);
        bzero((void *) (size + 0x801E7000), 0x37D00 - size);
        PORT_FE_LOADED();
        frontEndPresent = 1;
        fe_pak_start_thread();
        debug_printf("got front end\n");
    }
}

/* DMAs *size bytes from ROM devAddr in 16 KB pieces (compressed data to the staging buffer at 0x8021ED00),
 * then decompresses once per non-zero pass (arg3, arg4) by method arg5 (1 kiunzip, 2 lzss_decompress); *size
 * becomes the decompressed length (0x8028B4C4).
 * DMA *size bytes from ROM devAddr to dest (to a staging buffer at 0x8021ED00
 * when compressed), then decompress with method arg5 (1: kiunzip,
 * 2: lzss_decompress); *size becomes the decompressed length. */
void hd_rom_load(u32 devAddr, u32 dest, u32 *size, u8 arg3, u8 arg4, u8 arg5) {
    u32 i;
    u32 rem;
    u32 buf;
    u32 orig;
    u32 p;
#if defined(NON_MATCHING) && defined(PORT_HOST)
    u32 rom = devAddr;
#endif

    orig = dest;
    PORT_ROM_SPAN(&devAddr, size);
    if (arg3 || arg4) {
        buf = 0x8021ED00;
    } else {
        buf = dest;
    }
    osInvalDCache((void *) buf, *size);
    p = buf;
    for (i = 0; i < *size / 0x4000; i++) {
        osPiStartDma(&gDmaIoMesg, 0, 0, devAddr, (void *) p, 0x4000, &gDmaMesgQueue);
        osRecvMesg(&gDmaMesgQueue, NULL, 1);
        devAddr += 0x4000;
        p += 0x4000;
    }
    rem = *size - (*size / 0x4000) * 0x4000;
    if (rem) {
        osPiStartDma(&gDmaIoMesg, 0, 0, devAddr, (void *) p, rem, &gDmaMesgQueue);
        osRecvMesg(&gDmaMesgQueue, NULL, 1);
    }
    switch (arg5) {
        case 1:
            if (arg3) {
                kiunzip((u8 * N64P *) &buf, (u8 * N64P *) &dest, 0x8004B400);
            }
            if (arg4) {
                kiunzip((u8 * N64P *) &buf, (u8 * N64P *) &dest, 0x8004B400);
            }
            break;
        case 2:
            if (arg3) {
                lzss_decompress(&buf, &dest, 0x8004B400, arg3);
            }
            if (arg4) {
                lzss_decompress(&buf, &dest, 0x8004B400, arg4);
            }
            break;
    }
    if (arg3 || arg4) {
        *size = dest - orig;
    }
#if defined(NON_MATCHING) && defined(PORT_HOST)
    /* the block is in place (decompressed): swap it by its asset schema */
    port_on_load(rom, orig, *size);
#endif
}
