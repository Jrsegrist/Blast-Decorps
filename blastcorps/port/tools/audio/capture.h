/* Audio task capture files (rsp_tap.so and bc_headless --audio-capture
 * write them, asp_replay / asp_lle read them).  A file is a sequence of
 * records:
 *   CapHeader
 *   uint8_t dmem[4096]           DMEM before the task (N64 byte order)
 *   AspVU vu                     the interpreter's vector unit state before the task
 *   n_reads  x (CapRange + data) RDRAM the task read, as it was before the task,
 *                                in N64 byte order (big-endian)
 *   n_writes x (CapRange + data) RDRAM the task wrote, as the reference left it
 *                                (rsp_tap: the LLE RSP; bc_headless: aspmain)
 * Host byte order for the header fields (x86 only). */
#ifndef CAPTURE_H
#define CAPTURE_H

#include <stdint.h>
#include "../../src/audio/aspmain.h"

typedef struct {
    char magic[4];             /* "ASPC" */
    uint32_t index;            /* audio task number */
    AspTask task;
    uint32_t n_reads, n_writes;
} CapHeader;

typedef struct {
    uint32_t addr, len, kind;
} CapRange;

#endif
