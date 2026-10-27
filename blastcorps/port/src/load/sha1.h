/* SHA-1 (FIPS 180-4) of a byte buffer (sha1.c): the mod files' level and texture-slot hashes (level_mod.h,
 * port_mods.c), bc.exe's ROM check (live_setup.cpp). */
#ifndef PORT_SHA1_H
#define PORT_SHA1_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void lm_sha1(const uint8_t *data, size_t len, uint8_t out[20]);
void lm_sha1_hex(const uint8_t *data, size_t len, char out[41]);   /* lower-case hex, NUL-terminated */

#ifdef __cplusplus
}
#endif

#endif
