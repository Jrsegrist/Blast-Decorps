/* Save data in N64 byte order (PORT_SAVE_* hooks in hd_front_end/0E7B0.c,
 * include/game/port.h).
 *
 * The save thread (pfsHandler.c) hands the EEPROM and the Controller Pak the
 * game's records as raw bytes and puts a CRC over those bytes into the last
 * four.  On the N64 the bytes are big-endian; natively the multi-byte fields
 * are little-endian.  So that save files interchange with emulators (and the
 * CRC is the one an N64 would compute), each save routine works on a big-
 * endian copy: port_save_begin() returns a buffer holding the record in N64
 * byte order, the routine computes/checks the CRC and does the transfer on
 * it, and port_save_end() puts back, in host order, what the routine changed
 * in the buffer (the CRC bytes, the data read); port_save_sync() does that
 * early, for the CRC a write path has just added (PORT_SAVE_SYNC).  The record
 * itself never holds big-endian fields while other threads can run.
 *
 * Layouts (every other byte is a u8):
 *   PLAYER   players[n], 0x100: u16 0x0A (rank points), u32 0x10 (vehicle
 *            unlocks), u32 0x14 (money), u32 0xF0 (vehicle flags); CRC 0xFC.
 *   TIMES    gBestTimes[n], 0x20: u16[14] best times; CRC 0x1C.
 *   EETIMES  gEepTimes block, 8: u16[4] (time, time ^ 0x55AA pairs).
 *   STATUS   pakBuffer, 0x40: a bit stream (bytes), or the u64 marker
 *            0x1234567887654321 at 0 ("no status"); CRC 0x3C.  Swapped in
 *            place (a macro array; the marker is the only multi-byte value).
 * The semaphore (u64 at EEPROM block 0x3F / pak offset 0xDE0) goes through
 * port_save_be64. */
#include "plat.h"

enum { KIND_PLAYER, KIND_TIMES, KIND_EETIMES, KIND_STATUS };

static u16 bswap16(u16 v) {
    return (u16) ((v >> 8) | (v << 8));
}
static u32 bswap32(u32 v) {
    return (v >> 24) | ((v >> 8) & 0xFF00) | ((v << 8) & 0xFF0000) | (v << 24);
}

static void swap16(u8 *p, int off) {
    u16 v;
    PORT_MEMCPY(&v, p + off, 2);
    v = bswap16(v);
    PORT_MEMCPY(p + off, &v, 2);
}
static void swap32(u8 *p, int off) {
    u32 v;
    PORT_MEMCPY(&v, p + off, 4);
    v = bswap32(v);
    PORT_MEMCPY(p + off, &v, 4);
}

/* host <-> N64 order (an involution) */
static void convert(u8 *p, int kind, int size) {
    int i;
    switch (kind) {
        case KIND_PLAYER:
            swap16(p, 0x0A);
            swap32(p, 0x10);
            swap32(p, 0x14);
            swap32(p, 0xF0);
            break;
        case KIND_TIMES:
            for (i = 0; i < 0x1C; i += 2) swap16(p, i);
            break;
        case KIND_EETIMES:
            for (i = 0; i < size; i += 2) swap16(p, i);
            break;
        case KIND_STATUS: {
            static const u8 le[8] = { 0x21, 0x43, 0x65, 0x87, 0x78, 0x56, 0x34, 0x12 };
            static const u8 be[8] = { 0x12, 0x34, 0x56, 0x78, 0x87, 0x65, 0x43, 0x21 };
            if (PORT_MEMCMP(p, le, 8) == 0) PORT_MEMCPY(p, be, 8);
            else if (PORT_MEMCMP(p, be, 8) == 0) PORT_MEMCPY(p, le, 8);
            break;
        }
    }
}

/* The copy goes back into the record byte by byte, only where the save routine
 * changed it (the CRC bytes it wrote, the data it read): the game's other threads
 * keep running during the transfer (an EEPROM write takes ~15 frames), and what
 * they write into the record meanwhile stays, as on the N64, where the routine
 * works on the record itself.  g_snap is the record as the copy last had it. */
static u8 g_buf[0x100];
static u8 g_snap[0x100];
static u8 *g_orig;
static int g_kind, g_size;

static void merge_back(void) {
    u8 host[0x100];
    int i;
    PORT_MEMCPY(host, g_buf, g_size);
    convert(host, g_kind, g_size);
    for (i = 0; i < g_size; i++) {
        if (host[i] != g_snap[i]) {
            g_orig[i] = host[i];
            g_snap[i] = host[i];
        }
    }
}

void *port_save_begin(void *p, int kind, int size) {
    if (kind == KIND_STATUS) {
        convert(p, kind, size);
        return p;
    }
    if (g_orig != NULL) host_fatal("port_save_begin: nested save (kind %d in %d)", kind, g_kind);
    if (size > (int) sizeof g_buf) host_fatal("port_save_begin: size %d", size);
    g_orig = p;
    g_kind = kind;
    g_size = size;
    PORT_MEMCPY(g_buf, p, size);
    PORT_MEMCPY(g_snap, p, size);
    convert(g_buf, kind, size);
    return g_buf;
}

/* osEepromLongWrite (os_si.c), before each 8-byte block: the N64's routine hands the EEPROM the record itself,
 * block by block over ~15 frames (12 ms a block), so each block goes out as the record is at that moment; a
 * game write to the record during the transfer reaches the blocks sent after it.  The port's routine hands
 * over the copy: what the game changed in the record since the copy last had it comes into the copy now,
 * for the bytes of this block (N64 order), as the N64 would send them. */
void port_save_refresh(const void *block, int n) {
    u8 perm[0x100];
    const u8 *b = block;
    int i, off;
    if (g_orig == NULL || b < g_buf || b >= g_buf + g_size) return;
    off = (int) (b - g_buf);
    if (n > g_size - off) n = g_size - off;
    /* which host byte each N64-order byte is (convert permutes bytes within the fields) */
    for (i = 0; i < g_size; i++) perm[i] = (u8) i;
    convert(perm, g_kind, g_size);
    for (i = off; i < off + n; i++) {
        int h = perm[i];
        if (g_orig[h] != g_snap[h]) {
            g_buf[i] = g_orig[h];
            g_snap[h] = g_orig[h];
        }
    }
}

/* after the CRC went into the copy, before the transfer: the record gets it now,
 * as the N64's fe_pak_add_crc writes it into the record before the write starts */
void port_save_sync(void *buf, int kind) {
    if (buf != g_buf || g_orig == NULL || kind != g_kind) host_fatal("port_save_sync: unmatched (kind %d)", kind);
    merge_back();
}

void *port_save_end(void *buf, int kind, int size) {
    u8 *orig;
    if (kind == KIND_STATUS) {
        convert(buf, kind, size);
        return buf;
    }
    if (buf != g_buf || g_orig == NULL || kind != g_kind) host_fatal("port_save_end: unmatched (kind %d)", kind);
    merge_back();
    if (host_verbose)
        host_log("save: kind %d at %p done (frame %u): %02x %02x %02x %02x ... %02x %02x %02x %02x\n", kind,
                 (void *) g_orig, (unsigned) plat_stats.frames, g_orig[0], g_orig[1], g_orig[2], g_orig[3],
                 g_orig[g_size - 4], g_orig[g_size - 3], g_orig[g_size - 2], g_orig[g_size - 1]);
    orig = g_orig;
    g_orig = NULL;
    return orig;
}

/* fe_pak_put_semaphore writes 0x20 bytes to the pak from the address of its u64
 * parameter: the semaphore and then whatever follows it on the stack.  On
 * the N64 (mupen64plus, original ROM, every write seen) that is
 * 00000000 00000000 00000000 802DC2C8 00000000 00000000; the port writes
 * those bytes rather than the host's stack. */
void *port_save_semblock(const void *sem) {
    static u8 block[0x20];
    static const u8 tail[0x18] = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x80, 0x2D, 0xC2, 0xC8, 0, 0, 0, 0, 0, 0, 0, 0 };
    PORT_MEMCPY(block, sem, 8);
    PORT_MEMCPY(block + 8, tail, sizeof tail);
    return block;
}

/* for host-side logging (headless_main.c's --print) */
unsigned plat_frames(void) {
    return plat_stats.frames;
}

u64 port_save_be64(u64 v) {
    return ((u64) bswap32((u32) v) << 32) | bswap32((u32) (v >> 32));
}

/* ---- the debug cheats and their saves (plat_host.h host_cheats_*) ------------------
 *
 * gDebugCheats (hd_code data, 0x802FA268) is set once at boot by parse_cmdline (26570.c) when the debug
 * command line has -c or -C, and only read after that: every frame by hd_game_frame (the C-right
 * level-end cheat), by fe_promo_draw (A skips the promotion), academy_save_markers (Z) and the boot
 * screen's chime.  Nothing caches it, so writing it between game steps turns the cheats on or off at
 * once; that is all host_cheats_set does to the game.
 *
 * A cheated finish is saved as a real one.  With plat_cfg.cheats_save (bc.exe's [game] cheats_save =
 * separate, the default there), once the cheats have been on in a run the save files are
 * <name>.cheats.<ext> next to the real ones for the rest of the run, and the real files are not written
 * again:
 *   - on at boot (-c / -C on the debug command line): the cheats files are used from the start; one that
 *     doesn't exist yet is first copied from the real file (a missing real file: both start blank, and
 *     only the cheats file gets created);
 *   - turned on in a running game: the switch waits until the front end's save thread is not loaded
 *     (frontEndPresent 0: in a level, the attract demo, or before the front end first loads), so a save
 *     the thread is part-way through (a sequence of commands, 01C40.c) finishes in the real file.  The
 *     cheats can only change what is saved in a level (the C-right finish, Z's academy marker), and the
 *     level unloads the save thread first (hd_setup_level), so nothing cheated reaches the real file.
 *     The cheats files are then written whole from the images in memory, i.e. the real saves as they are
 *     now (an older cheats file is replaced);
 *   - turned off again: the cheats files stay in use until the game closes (the game's memory holds
 *     cheated progress).
 * The images in memory (os_si.c g_eeprom, pif.c g_mpk) don't change: the game sees exactly the same save
 * data either way; only the files written differ. */
#define G_DEBUG_CHEATS (*(volatile s32 *) 0x802FA268)   /* gDebugCheats */
#define G_FE_PRESENT (*(volatile u8 *) 0x80370C50)      /* frontEndPresent */

static int g_cheats_used;        /* the cheats were on in this run */
static int g_cheats_state;       /* 0 the normal files, 1 the switch waits for the save thread to go, 2 switched */
static char *g_cheats_eep, *g_cheats_mpk;   /* the cheats files' names (NULL: no such save) */

static unsigned cstrlen(const char *s) {
    unsigned n = 0;
    while (s[n]) n++;
    return n;
}

/* "dir\blastcorps.eep" -> "dir\blastcorps.cheats.eep" ("x" -> "x.cheats"); kept for the run */
static char *cheats_name(const char *p) {
    unsigned n = cstrlen(p), dot = n, i, k = 0;
    char *s;
    for (i = n; i > 0; i--) {
        if (p[i - 1] == '/' || p[i - 1] == '\\') break;
        if (p[i - 1] == '.') {
            dot = i - 1;
            break;
        }
    }
    s = host_realloc(NULL, n + 8);
    for (i = 0; i < dot; i++) s[k++] = p[i];
    for (i = 0; ".cheats"[i]; i++) s[k++] = ".cheats"[i];
    for (i = dot; i < n; i++) s[k++] = p[i];
    s[k] = 0;
    return s;
}

/* parse_cmdline's reading of the debug command line (the 63 bytes osPiRawReadIo hands it): the words up
 * to the first that doesn't start with '-'; -c or -C turns the cheats on */
int host_cmdline_cheats(const char *s) {
    unsigned i = 0;
    if (s == NULL) return 0;
    while (i < 63 && s[i]) {
        while (i < 63 && s[i] == ' ') i++;
        if (i >= 63 || s[i] == 0) break;
        if (s[i] != '-') break;
        if (i + 1 < 63 && (s[i + 1] == 'c' || s[i + 1] == 'C')) return 1;
        while (i < 63 && s[i] && s[i] != ' ') i++;
    }
    return 0;
}

/* boot (plat_start, before the save images are read): the cheats on at boot use the cheats files */
void plat_cheats_boot(void) {
    const char **paths[2];
    char **names[2];
    int k;
    if (plat_cfg.eeprom_path != NULL) g_cheats_eep = cheats_name(plat_cfg.eeprom_path);
    if (plat_cfg.mpk_path != NULL) g_cheats_mpk = cheats_name(plat_cfg.mpk_path);
    if (!host_cmdline_cheats(plat_cfg.cmdline)) return;
    g_cheats_used = 1;
    if (!plat_cfg.cheats_save) {
        host_log("cheats: debug cheats on at boot (debug command line \"%s\"); saves: the normal files\n",
                 plat_cfg.cmdline);
        return;
    }
    paths[0] = &plat_cfg.eeprom_path, paths[1] = &plat_cfg.mpk_path;
    names[0] = &g_cheats_eep, names[1] = &g_cheats_mpk;
    for (k = 0; k < 2; k++) {
        unsigned size = 0;
        void *p;
        if (*paths[k] == NULL) continue;
        p = host_read_file(*names[k], &size);   /* (the buffers here are kept: once a run) */
        if (p != NULL) {
            host_log("cheats: %s: continued\n", *names[k]);
        } else if ((p = host_read_file(*paths[k], &size)) != NULL) {
            if (host_write_file(*names[k], p, size) != 0) host_log("cheats: can't write %s\n", *names[k]);
            else host_log("cheats: %s: copied from %s\n", *names[k], *paths[k]);
        } else {
            host_log("cheats: %s: starts blank (no %s)\n", *names[k], *paths[k]);
        }
        *paths[k] = *names[k];
    }
    g_cheats_state = 2;
    host_log("cheats: debug cheats on at boot (debug command line \"%s\"); saves: the cheats files (EEPROM %s, "
             "Controller Pak %s)\n", plat_cfg.cmdline, g_cheats_eep ? g_cheats_eep : "(none)",
             plat_cfg.mpk_path ? g_cheats_mpk : "(none)");
}

/* the switch to the cheats files, if it is due and safe now */
static void cheats_switch(void) {
    if (!plat_cfg.cheats_save || !g_cheats_used || g_cheats_state == 2) return;
    if (G_FE_PRESENT != 0) {
        if (g_cheats_state == 0)
            host_log("cheats: saves go to the cheats files from the next level (the front end's save thread is "
                     "loaded now; frame %u)\n", (unsigned) plat_stats.frames);
        g_cheats_state = 1;
        return;
    }
    if (plat_cfg.eeprom_path != NULL) plat_cfg.eeprom_path = g_cheats_eep;
    if (plat_cfg.mpk_path != NULL) plat_cfg.mpk_path = g_cheats_mpk;
    plat_si_rewrite();   /* the cheats files: the images in memory (the real saves as they are now) */
    plat_pak_rewrite();
    g_cheats_state = 2;
    host_log("cheats: saves now go to the cheats files (EEPROM %s, Controller Pak %s), written from the saves in "
             "memory; the real files are not written again in this run (frame %u)\n",
             plat_cfg.eeprom_path ? plat_cfg.eeprom_path : "(none)", plat_cfg.mpk_path ? plat_cfg.mpk_path : "(none)",
             (unsigned) plat_stats.frames);
}

void host_cheats_set(int on) {
    G_DEBUG_CHEATS = on ? 1 : 0;
    host_log("cheats: debug cheats %s (frame %u)\n", on ? "on" : "off", (unsigned) plat_stats.frames);
    if (on) g_cheats_used = 1;
    cheats_switch();
}

int host_cheats_on(void) {
    return G_DEBUG_CHEATS != 0;
}

void host_cheats_set_save(int separate) {
    plat_cfg.cheats_save = separate;
    cheats_switch();   /* (cheats used with the normal files, now separate: from now on) */
}

int host_cheats_saves(void) {
    return g_cheats_state;
}

const char *host_cheats_save_name(void) {
    return g_cheats_eep != NULL && plat_cfg.eeprom_path != NULL ? g_cheats_eep
           : g_cheats_mpk != NULL && plat_cfg.mpk_path != NULL  ? g_cheats_mpk
                                                                : NULL;
}

/* ---- saving off (plat_host.h host_saves_block) ---- */
int host_saves_blocked;

void host_saves_block(const char *why) {
    if (!host_saves_blocked)
        host_log("saves: off for the rest of this run (%s): the game saves into memory only, no file is written "
                 "(frame %u)\n", why, (unsigned) plat_stats.frames);
    host_saves_blocked = 1;
}

/* each frame (plat_on_frame): bc_headless --cheats-on F, and a switch that waits */
void plat_cheats_frame(u32 f) {
    if (plat_cfg.cheats_on_frame != 0 && f == plat_cfg.cheats_on_frame) host_cheats_set(1);
    if (g_cheats_state == 1) cheats_switch();
}
