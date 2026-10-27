/* SI: controllers from an input source, EEPROM and Controller Pak as files.
 *
 * The controller and EEPROM transfers here complete at once: the call that
 * would start a PIF DMA posts the SI event (OS_EVENT_SI) the hardware would
 * raise, so code that waits for it (osRecvMesg on the SI queue, or
 * cont_query_paks's poll of the queue's validCount, a busy-wait) continues
 * straight away.  (The Controller Pak's transfers, pif.c, take the
 * emulator's SI DMA time.)  The EEPROM's 12 ms waits between blocks are
 * timers, on the SDK's timer list (os_thread.c).
 *
 * EEPROM: 4 Kbit (64 blocks of 8 bytes), kept in memory and written back to
 * the file (--eeprom FILE) after each write, as raw bytes in EEPROM order.
 * A missing file starts as all 0xFF (a blank chip, as mupen64plus formats
 * it).  The save routines hand over big-endian records (save.c), so the
 * file is byte-identical to the one mupen64plus writes for the original ROM
 * (same 512-byte .eep format) and either can be loaded by the other.
 *
 * Controller Pak (--mpk FILE): the front end's own SDK Pfs code
 * (hd_front_end/ul_pfs*.c ...) compiled natively, over the PIF emulation in
 * pif.c; the query reports the pak in controller 1's status. */
#include "plat.h"

#define EEPROM_BLOCKS 64
static u8 g_eeprom[EEPROM_BLOCKS * 8];
static int g_eeprom_dirty;

static u16 g_latch_button;
static s8 g_latch_x, g_latch_y;
static int g_cont_inited;

extern u8 __osMaxControllers;

void plat_si_init(void) {
    unsigned size = 0;
    u8 *p;
    plat_pak_init();
    PORT_MEMSET(g_eeprom, 0xFF, sizeof g_eeprom);
    if (plat_cfg.eeprom_path != NULL && (p = host_read_file(plat_cfg.eeprom_path, &size)) != NULL) {
        PORT_MEMCPY(g_eeprom, p, size < sizeof g_eeprom ? size : sizeof g_eeprom);
    }
}

void plat_si_flush(void) {
    if (g_eeprom_dirty && plat_cfg.eeprom_path != NULL && !host_saves_blocked) {
        const double t0 = host_ms();   /* (host_prof: bc.exe's long game steps) */
        if (host_write_file(plat_cfg.eeprom_path, g_eeprom, sizeof g_eeprom) != 0)
            host_log("si: can't write %s\n", plat_cfg.eeprom_path);
        g_eeprom_dirty = 0;
        host_prof.save_ms += host_ms() - t0;
        host_prof.save_n++;
    }
}

/* the whole image to the file now (save.c: the debug cheats' saves start a new file) */
void plat_si_rewrite(void) {
    g_eeprom_dirty = 1;
    plat_si_flush();
}

static void si_done(void) {
    plat_post_event(OS_EVENT_SI);
}

/* ---- controllers ----------------------------------------------------------- */

/* osContSetCh: how many channels the SI commands cover (libultra's
 * __osMaxControllers); the game reads only controller 1 (the other
 * OSContPad entries are left alone, as on the N64) */
static int g_max_ch = 4;

static void fill_status(OSContStatus *data, u8 *pattern) {
    int i;
    u8 bits = 0;
    for (i = 0; i < g_max_ch; i++, data++) {
        if (i == 0 && plat_cfg.cont_present) {
            data->type = CONT_TYPE_NORMAL;
            /* no pak: 0x02, what the PIF reports (mupen64plus) */
            data->status = plat_pak_present() ? CONT_CARD_ON : CONT_CARD_PULL;
            data->errno = 0;
            bits |= 1 << i;
        } else {
            data->errno = CONT_NO_RESPONSE_ERROR;
        }
    }
    if (pattern) *pattern = bits;
}

s32 osContInit(OSMesgQueue *mq, u8 *bitpattern, OSContStatus *data) {
    /* the SDK waits until 0.5 s after boot before talking to the PIF */
    static OSTimer timer;
    static OSMesgQueue tq;
    static OSMesg tmsg;
    OSTime t;
    const OSTime half_s = (OSTime) 500000 * PLAT_COUNT_HZ / 1000000;
    (void) mq;
    if (g_cont_inited) return 0;
    g_cont_inited = 1;
    __osMaxControllers = MAXCONTROLLERS; /* the SDK's Pfs code loops over it */
    t = osGetTime();
    if (t < half_s) {
        osCreateMesgQueue(&tq, &tmsg, 1);
        osSetTimer(&timer, half_s - t, 0, &tq, (OSMesg) &tmsg);
        osRecvMesg(&tq, NULL, OS_MESG_BLOCK);
    }
    fill_status(data, bitpattern);
    return 0;
}

s32 osContStartQuery(OSMesgQueue *mq) {
    (void) mq;
    si_done();
    return 0;
}

void osContGetQuery(OSContStatus *data) {
    fill_status(data, NULL);
}

s32 osContStartReadData(OSMesgQueue *mq) {
    (void) mq;
    /* the PIF samples the pad now */
    plat_input_read(plat_stats.cont_reads, &g_latch_button, &g_latch_x, &g_latch_y);
    plat_stats.cont_reads++;
    si_done();
    return 0;
}

void osContGetReadData(OSContPad *data) {
    int i;
    for (i = 0; i < g_max_ch; i++, data++) {
        if (i == 0 && plat_cfg.cont_present) {
            data->button = g_latch_button;
            data->stick_x = g_latch_x;
            data->stick_y = g_latch_y;
            data->errno = 0;
        } else {
            data->errno = CONT_NO_RESPONSE_ERROR;
        }
    }
}

s32 osContSetCh(u8 ch) {
    g_max_ch = ch > 4 ? 4 : ch;
    __osMaxControllers = (u8) g_max_ch;
    return 0;
}

/* ---- EEPROM ------------------------------------------------------------------ */

s32 osEepromProbe(OSMesgQueue *mq) {
    (void) mq;
    return plat_cfg.eeprom_present ? EEPROM_TYPE_4K : 0;
}

s32 osEepromRead(OSMesgQueue *mq, u8 address, u8 *buffer) {
    (void) mq;
    if (!plat_cfg.eeprom_present) return CONT_NO_RESPONSE_ERROR;
    if (address >= EEPROM_BLOCKS) return -1;
    PORT_MEMCPY(buffer, g_eeprom + address * 8, 8);
    plat_stats.eeprom_reads++;
    return 0;
}

s32 osEepromWrite(OSMesgQueue *mq, u8 address, u8 *buffer) {
    (void) mq;
    if (!plat_cfg.eeprom_present) return CONT_NO_RESPONSE_ERROR;
    if (address >= EEPROM_BLOCKS) return -1;
    PORT_MEMCPY(g_eeprom + address * 8, buffer, 8);
    g_eeprom_dirty = 1;
    plat_stats.eeprom_writes++;
    plat_si_flush();
    return 0;
}

s32 osEepromLongRead(OSMesgQueue *mq, u8 address, u8 *buffer, int nbytes) {
    /* this SDK waits 12 ms after each block read too (front end's
     * ul_conteeplongread.c) */
    static OSTimer timer;
    static OSMesgQueue tq;
    static OSMesg tmsg;
    s32 ret = 0;
    osCreateMesgQueue(&tq, &tmsg, 1);
    while (nbytes > 0) {
        ret = osEepromRead(mq, address, buffer);
        if (ret != 0) return ret;
        nbytes -= 8;
        address++;
        buffer += 8;
        osSetTimer(&timer, (OSTime) 12000 * PLAT_COUNT_HZ / 1000000, 0, &tq, (OSMesg) &tmsg);
        osRecvMesg(&tq, NULL, OS_MESG_BLOCK);
    }
    return ret;
}

s32 osEepromLongWrite(OSMesgQueue *mq, u8 address, u8 *buffer, int nbytes) {
    /* the SDK waits 12 ms (one block's write time) after each block */
    static OSTimer timer;
    static OSMesgQueue tq;
    static OSMesg tmsg;
    s32 ret = 0;
    osCreateMesgQueue(&tq, &tmsg, 1);
    while (nbytes > 0) {
        /* the block as the record holds it now (save.c: the routine passes the record's big-endian copy) */
        port_save_refresh(buffer, 8);
        ret = osEepromWrite(mq, address, buffer);
        if (ret != 0) return ret;
        nbytes -= 8;
        address++;
        buffer += 8;
        osSetTimer(&timer, (OSTime) 12000 * PLAT_COUNT_HZ / 1000000, 0, &tq, (OSMesg) &tmsg);
        osRecvMesg(&tq, NULL, OS_MESG_BLOCK);
    }
    return ret;
}

/* the front end calls the EEPROM functions by address (0E7B0.c) */
s32 func_80204C10(OSMesgQueue *mq, u8 a, u8 *b) {
    return osEepromWrite(mq, a, b);
}
s32 func_802050F0(OSMesgQueue *mq, u8 a, u8 *b) {
    return osEepromRead(mq, a, b);
}
s32 func_802042D0(OSMesgQueue *mq, u8 a, u8 *b, int n) {
    return osEepromLongWrite(mq, a, b, n);
}
s32 func_80204410(OSMesgQueue *mq, u8 a, u8 *b, int n) {
    return osEepromLongRead(mq, a, b, n);
}

/* ---- Controller Pak: the front end's own SDK Pfs code over pif.c ------------- */
