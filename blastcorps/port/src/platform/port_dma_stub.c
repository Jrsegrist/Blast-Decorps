/* Identity port_on_dma: leaves DMA'd ROM bytes as they are.  The Makefile
 * links this only while port/src/load/ has no sources; the load layer's
 * definition replaces it.  (Not a weak symbol: PE weak externals don't
 * resolve reliably with binutils.) */
#include <stdint.h>
#include "port_dma.h"

void port_on_dma(uint32_t dst, uint32_t rom, uint32_t len) {
    (void) dst;
    (void) rom;
    (void) len;
}

uint32_t port_dma_len(uint32_t rom, uint32_t asked, uint32_t len) {
    (void) rom;
    (void) asked;
    return len;
}
