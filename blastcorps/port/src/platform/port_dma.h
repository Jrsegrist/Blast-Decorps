/* The one hook shared by the platform layer ("plat") and the load-time byte
 * swapper ("load"): called after every PI DMA from the ROM into RDRAM.
 *   dst  N64 (KSEG0) address the bytes were copied to
 *   rom  ROM offset they came from
 *   len  byte count
 * The bytes at dst are still the ROM's big-endian bytes when it is called.
 * The platform has an identity version (port_dma_stub.c), linked only while
 * port/src/load/ has no sources; the load layer supplies the real one. */
#ifndef PORT_DMA_H
#define PORT_DMA_H

/* port_dma_len: before the copy, the byte count a DMA of `asked` bytes at ROM offset `rom` gives (`len`: what the
 * ROM itself gives, its map's): a mod's texture slot that grew past its room is read whole, as a ROM holding it
 * would (its table entry's next offset moved), port_mods.c */
#ifdef _ULTRATYPES_H_
void port_on_dma(u32 dst, u32 rom, u32 len);
u32 port_dma_len(u32 rom, u32 asked, u32 len);
#else
#include <stdint.h>
void port_on_dma(uint32_t dst, uint32_t rom, uint32_t len);
uint32_t port_dma_len(uint32_t rom, uint32_t asked, uint32_t len);
#endif

#endif
