/* PIF (joybus) emulation for the front end's own Controller Pak code, and
 * the Controller Pak itself, kept in a mupen64plus-format .mpk file.
 *
 * The front end's SDK Pfs objects (hd_front_end/ul_pfs*.c, ul_contpfs.c,
 * ul_contram*.c: ultralib's source, compiled natively) talk to the pak
 * through PIF RAM: they build joybus commands in a 64-byte OSPifRam and call
 * __osSiRawStartDma(OS_WRITE) / (OS_READ).  The write runs the commands (when
 * pifstatus has CONT_CMD_EXE set) and the read hands the results back; both
 * post the SI event PLAT_SI_DMA_COUNTS later, as the hardware's interrupt
 * would.
 *
 * Commands: status / reset (0x00, 0xFF), pak read (0x02), pak write (0x03),
 * on channel 0 (controller 1); the other channels answer "no response".  The
 * PIF RAM bytes are what the N64 would see, except that the SDK writes the
 * pak address (__OSContRamReadFormat.address, a u16) and pifstatus in host
 * order, so those are read as host integers.
 *
 * Byte order: the pak image is big-endian (N64 format, as mupen64plus keeps
 * it), the Pfs code is native.  Pak reads and writes convert the system
 * blocks by their layout, so the C sees host-order fields:
 *   blocks 1, 3, 4, 6    __OSPackId (u32 repaired, random; u64 serial_mid,
 *                        serial_low; u16 deviceid; u8 banks, version, swapped
 *                        as a pair; u16 checksum, inverted_checksum)
 *   blocks 8-15, 16-23   inode table and its backup: 128 __OSInodeUnit (u16)
 *                        per page
 *   blocks 24-39         directory: one __OSDir per block (u32 game_code, u16
 *                        company_code, u16 start_page, u16 data_sum)
 *   everything else      bytes (block 0, the label, file data)
 * This is the layout of a one-bank (32 KB) pak, the only kind mupen64plus
 * emulates.  The data CRC the PIF returns is computed over the host-order
 * bytes the C sends or receives, which is what the SDK checks it against.
 * Where the SDK reads a byte pair both as two u8s and as one u16 (the
 * __OSInodeUnit union; __OSPackId's banks/version, summed as a u16 word by
 * __osIdCheckSum), the port's copy of PRinternal/controller.h (port/Makefile)
 * declares the two bytes in reverse order, so both views agree.
 *
 * The .mpk file holds four paks (32 KB each, controller 1's first); only the
 * first is used, the rest are kept as they are.  A missing file is created
 * with four freshly formatted paks.  Writes go to the file at once. */
#include "plat.h"

#define PAK_SIZE 0x8000
#define MPK_SIZE (4 * PAK_SIZE)
#define BLOCK 32

u8 __osContDataCrc(u8 *data); /* the front end's ul_crc.c */

static u8 *g_mpk;
static int g_pak;          /* controller 1 has a pak */
static u8 g_pif[64];

/* ---- byte order of the system blocks --------------------------------------- */

static void swapn(u8 *p, int n) {
    int i;
    for (i = 0; i < n / 2; i++) {
        u8 t = p[i];
        p[i] = p[n - 1 - i];
        p[n - 1 - i] = t;
    }
}

/* host <-> N64 order of block `addr` (an involution) */
static void convert_block(u32 addr, u8 *b) {
    int i;
    if (addr == 1 || addr == 3 || addr == 4 || addr == 6) {
        swapn(b + 0x00, 4);
        swapn(b + 0x04, 4);
        swapn(b + 0x08, 8);
        swapn(b + 0x10, 8);
        swapn(b + 0x18, 2);
        swapn(b + 0x1A, 2); /* banks, version: one u16 word to the checksum */
        swapn(b + 0x1C, 2);
        swapn(b + 0x1E, 2);
    } else if (addr >= 8 && addr < 24) {
        for (i = 0; i < BLOCK; i += 2) swapn(b + i, 2);
    } else if (addr >= 24 && addr < 40) {
        swapn(b + 0x0, 4);
        swapn(b + 0x4, 2);
        swapn(b + 0x6, 2);
        swapn(b + 0xA, 2);
    }
}

/* ---- the .mpk file ----------------------------------------------------------- */

static void put16(u8 *p, u16 v) {
    p[0] = (u8) (v >> 8);
    p[1] = (u8) v;
}

/* a freshly formatted 32 KB pak, in N64 byte order */
static void format_pak(u8 *pak, int n) {
    u8 id[BLOCK];
    u16 sum = 0, isum = 0;
    int i;
    PORT_MEMSET(pak, 0, PAK_SIZE);
    /* ID: repaired, random, serials: an arbitrary fixed pattern (per pak);
     * deviceid 1 (bit 0: a valid pak), one bank, version 0 */
    PORT_MEMSET(id, 0, sizeof id);
    for (i = 0; i < 0x18; i++) id[i] = (u8) (0x5A ^ (i * 37) ^ (n << 4));
    put16(id + 0x18, 0x0001);
    id[0x1A] = 1;
    id[0x1B] = 0;
    for (i = 0; i < 0x1C; i += 2) {
        u16 w = (u16) ((id[i] << 8) | id[i + 1]);
        sum += w;
        isum += (u16) ~w;
    }
    put16(id + 0x1C, sum);
    put16(id + 0x1E, isum);
    PORT_MEMCPY(pak + 1 * BLOCK, id, BLOCK);
    PORT_MEMCPY(pak + 3 * BLOCK, id, BLOCK);
    PORT_MEMCPY(pak + 4 * BLOCK, id, BLOCK);
    PORT_MEMCPY(pak + 6 * BLOCK, id, BLOCK);
    /* inode table and backup: pages 0-4 are the system's, 5-127 free (3);
     * entry 0's low byte is the byte sum of entries 5-127 */
    for (i = 5; i < 128; i++) put16(pak + 0x100 + 2 * i, 0x0003);
    put16(pak + 0x100, (u16) ((123 * 3) & 0xFF));
    PORT_MEMCPY(pak + 0x200, pak + 0x100, 0x100);
}

static void mpk_flush(u32 off, u32 len) {
    const double t0 = host_ms();   /* (host_prof: bc.exe's long game steps) */
    (void) off;
    (void) len;
    if (host_saves_blocked) return;   /* (save.c host_saves_block: the image in memory only) */
    if (host_write_file(plat_cfg.mpk_path, g_mpk, MPK_SIZE) != 0)
        host_log("pif: can't write %s\n", plat_cfg.mpk_path);
    host_prof.save_ms += host_ms() - t0;
    host_prof.save_n++;
}

void plat_pak_init(void) {
    unsigned size = 0;
    u8 *p;
    int i;
    if (plat_cfg.mpk_path == NULL) return;
    g_mpk = host_realloc(NULL, MPK_SIZE);
    p = host_read_file(plat_cfg.mpk_path, &size);
    if (p != NULL) {
        for (i = 0; i < 4; i++) format_pak(g_mpk + i * PAK_SIZE, i);
        PORT_MEMCPY(g_mpk, p, size < MPK_SIZE ? size : MPK_SIZE);
    } else {
        for (i = 0; i < 4; i++) format_pak(g_mpk + i * PAK_SIZE, i);
        mpk_flush(0, MPK_SIZE);
        if (!plat_cfg.quiet) host_log("pif: created %s (four formatted paks)\n", plat_cfg.mpk_path);
    }
    g_pak = 1;
}

/* the whole image to the file now (save.c: the debug cheats' saves start a new file) */
void plat_pak_rewrite(void) {
    if (g_mpk != NULL && plat_cfg.mpk_path != NULL) mpk_flush(0, MPK_SIZE);
}

int plat_pak_present(void) {
    return g_pak;
}

/* ---- joybus -------------------------------------------------------------------- */

static int g_pak_reads, g_pak_writes;

/* one command for `channel`: tx bytes at t (t[0] = command), rx bytes at r;
 * returns 0, or 1 for "no response" */
static int joybus(int channel, u8 *t, int tx, u8 *r, int rx) {
    u16 field;
    u32 addr;
    u8 data[BLOCK];
    if (channel != 0 || !plat_cfg.cont_present) return 1;
    switch (t[0]) {
        case 0x00: /* status */
        case 0xFF: /* reset */
            if (rx >= 3) {
                r[0] = 0x05; /* type low byte first: CONT_TYPE_NORMAL */
                r[1] = 0x00;
                r[2] = g_pak ? CONT_CARD_ON : 0;
            }
            return 0;
        case 0x02: /* pak read: address (host u16: addr << 5 | crc) */
            if (tx < 3 || rx < 33) return 1;
            PORT_MEMCPY(&field, t + 1, 2);
            addr = field >> 5;
            if (g_pak && addr < PAK_SIZE / BLOCK) {
                PORT_MEMCPY(data, g_mpk + addr * BLOCK, BLOCK);
                convert_block(addr, data);
            } else {
                PORT_MEMSET(data, 0, BLOCK);
            }
            PORT_MEMCPY(r, data, BLOCK);
            r[BLOCK] = __osContDataCrc(data);
            if (!g_pak) r[BLOCK] = (u8) ~r[BLOCK];
            g_pak_reads++;
            return 0;
        case 0x03: /* pak write */
            if (tx < 35 || rx < 1) return 1;
            PORT_MEMCPY(&field, t + 1, 2);
            addr = field >> 5;
            PORT_MEMCPY(data, t + 3, BLOCK);
            r[0] = __osContDataCrc(data);
            if (g_pak && addr < PAK_SIZE / BLOCK) {
                convert_block(addr, data);
                PORT_MEMCPY(g_mpk + addr * BLOCK, data, BLOCK);
                mpk_flush(addr * BLOCK, BLOCK);
            } else if (!g_pak) {
                r[0] = (u8) ~r[0];
            }
            g_pak_writes++;
            return 0;
        default:
            return 1;
    }
}

/* run the PIF RAM's command list (the N64's byte stream) */
static void pif_execute(void) {
    int i = 0, channel = 0;
    while (i < 0x3C && channel < 5) {
        u8 b = g_pif[i];
        int tx, rx;
        if (b == 0xFE) break;        /* end */
        if (b == 0xFF || b == 0xFD) { /* padding (and channel reset marker) */
            i++;
            continue;
        }
        if (b == 0) { /* skip this channel */
            channel++;
            i++;
            continue;
        }
        tx = b & 0x3F;
        if (i + 1 >= 0x3C) break;
        rx = g_pif[i + 1] & 0x3F;
        if (i + 2 + tx + rx > 0x3C) break;
        if (joybus(channel, g_pif + i + 2, tx, g_pif + i + 2 + tx, rx)) g_pif[i + 1] |= 0x80; /* no response */
        i += 2 + tx + rx;
        channel++;
    }
}

/* An SI DMA to or from PIF RAM takes time: its interrupt comes PLAT_SI_DMA_COUNTS counts after the start,
 * mupen64plus's SI DMA duration (0x900, its default; no ROM-database entry for this game), so the Pfs code's
 * transfers (a Controller Pak block read or write: a DMA each way) take virtual time, during which the thread
 * waiting for them (the front end's pak thread) blocks and lower-priority threads run, as in the emulator.
 * (Following an emulator, plat_emu_counts converts to the port's counts.)  The controller and EEPROM
 * transfers (os_si.c) still complete at once. */
#define PLAT_SI_DMA_COUNTS 0x900u

s32 __osSiRawStartDma(s32 direction, void *dramAddr) {
    if (direction == OS_READ) {
        PORT_MEMCPY(dramAddr, g_pif, sizeof g_pif);
    } else {
        u32 status;
        PORT_MEMCPY(g_pif, dramAddr, sizeof g_pif);
        PORT_MEMCPY(&status, g_pif + 0x3C, 4);
        if (status & 1) {
            pif_execute();
            status = 0;
            PORT_MEMCPY(g_pif + 0x3C, &status, 4);
        }
    }
    plat_event_add(plat_now + plat_emu_counts(PLAT_SI_DMA_COUNTS), PEV_SI_DONE, NULL);
    return 0;
}

/* SI access: the platform's other SI users complete at once and never wait,
 * so the Pfs code only ever finds the SI free */
void __osSiGetAccess(void) {
}
void __osSiRelAccess(void) {
}
