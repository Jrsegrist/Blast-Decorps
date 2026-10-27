/* Headless platform layer: internal interfaces.  Included by the os*
 * replacement files, which are compiled with the game's headers (<ultra64.h>,
 * ultralib's, marked: tools/ulhdr64.py) so that OSThread, OSMesgQueue, OSIoMesg ... have exactly the
 * layout the game C uses. */
#ifndef PLAT_H
#define PLAT_H

#include <ultra64.h>
#include "port_cc.h"
#include "plat_host.h"
#include "port_dma.h"

/* ---- virtual clock ----------------------------------------------------- */
/* CPU count rate (osGetCount / osGetTime units): 46.875 MHz.  One NTSC field
 * (VI retrace) every 781250 counts = 60 Hz exactly. */
#define PLAT_COUNT_HZ 46875000u
#define PLAT_VI_PERIOD 781250u

extern u64 plat_now;     /* virtual time in count units */
extern u32 plat_vi_count; /* retraces so far */

/* Event kinds for the virtual-time event queue */
enum {
    PEV_VI = 1,    /* retrace (re-armed every PLAT_VI_PERIOD) */
    PEV_TIMER,     /* the timers' Compare interrupt (os_thread.c's timer list), p = NULL */
    PEV_SP_DONE,   /* RSP task finished: post OS_EVENT_SP */
    PEV_DP_DONE,   /* RDP full sync: post OS_EVENT_DP */
    PEV_SI_DONE    /* a PIF RAM DMA finished (pif.c): post OS_EVENT_SI */
};
void plat_event_add(u64 when, int kind, void *p);
void plat_event_remove(int kind, void *p);
/* advance the clock to the next pending event and deliver it (and every
 * other event due at that time).  Returns 0 if there is none. */
int plat_advance(void);
/* post an interrupt-style event message (OS_EVENT_*) now */
void plat_post_event(int event);
/* the idle loop: runs game threads; returns never */
PORT_NORETURN void plat_run(void);
/* thread currently running (NULL = host idle loop) */
OSThread *plat_running(void);
const char *plat_thread_name(OSThread *t);

/* ---- per-subsystem hooks --------------------------------------------- */
void plat_vi_retrace(void);          /* os_vi.c */
void plat_timer_fire(OSTimer *t);    /* os_thread.c: the Compare interrupt (t unused) */

/* ---- configuration (HostOpts in plat_host.h, filled by headless_main.c) -- */
typedef HostOpts PlatConfig;
extern PlatConfig plat_cfg;
extern u8 *plat_rom;
extern u32 plat_rom_size;
/* a ROM from the shiftable build with files moved (rdram.c, rdram.h): a read of N bytes at the original
 * offset OFF reads the returned length at *PHYS; then the two self-relative tables get original offsets.
 * The retail ROM: *PHYS = OFF, N, nothing changed. */
unsigned int rom_map_dma(unsigned int off, unsigned int n, unsigned int *phys);
void rom_map_fixup(void *dst, unsigned int off, unsigned int n);
unsigned int rom_map_virt(unsigned int off);   /* the original offset (of a hd_rom_load piece: port_rom_span) */

/* ---- statistics --------------------------------------------------------- */
typedef struct {
    u32 frames;            /* frame (swap-buffer) gfx tasks */
    u32 gfx_tasks;         /* all gfx tasks dropped */
    u32 cull_tasks;        /* cull_rsp_test visibility tests */
    u32 cull_hidden;       /* ... answered "not visible" */
    u32 cull_keyed, cull_disagree;   /* --clock: the emulator's answers; how many the model differs from */
    u32 aud_tasks;
    u32 pi_dmas;
    u64 pi_bytes;
    u32 cont_reads;
    u32 eeprom_reads, eeprom_writes;
    u32 fe_reloads;
    u32 gettime_calls;
    u32 thread_switches;
} PlatStats;
extern PlatStats plat_stats;

/* frame boundary hook (os_sp.c calls it when a frame's gfx task starts) */
void plat_on_frame(void);
/* input source (input.c) */
void plat_input_init(const char *path);
void plat_input_read(u32 index, u16 *button, s8 *x, s8 *y);
/* clock injection (os_time.c) */
void plat_clock_init(const char *gettime_path, const char *frame_done_path);
/* --syms / --clock (os_time.c) */
void plat_syms_load(const char *path);
const char *plat_sym_name(u32 addr, u32 *off);
u32 plat_sym_addr(const char *name);
void plat_clock_keyed_load(const char *path);
/* kind 0 osGetTime, 1 osGetCount, 2 osAiGetLength: the injected value for
 * the caller at RA in this retrace (or the emulator's clock for 0/1) */
int plat_clock_key_take(int kind, void *ra, u64 *v);
/* the same by caller name; kind 3: cull_rsp_test's visibility test word */
int plat_clock_key_take_name(int kind, const char *name, u64 *v);
/* N emulator counts as native counts (the emulator's VI period differs) */
u64 plat_emu_counts(u64 n);
void plat_clock_report(void);
/* --sync: the emulator's thread timing (os_thread.c) */
void plat_sync_load(const char *path);
void plat_sync_report(void);
void plat_spin_until(u64 t);
/* a switch point of the running thread (kind: s/r/j/t messages, g/c clock) */
void plat_sync_point(void *ra, char kind, void *arg);
void plat_calls_init(const char *names);
/* injected completion time of frame N's gfx task, or ~0 (use gfx_cycles) */
u64 plat_frame_done_time(u32 frame);
u64 plat_frame_sp_time(u32 frame);
/* optional C hook for osGetTime (part 2's trace matching): returns 1 and
 * sets *t to override the value of call number `call` */
extern int (*plat_gettime_hook)(u32 call, u64 now, u64 *t);
/* the same for osGetCount (1C460.c and 20460.c use it as a random source) */
extern int (*plat_getcount_hook)(u32 call, u64 now, u32 *c);
/* front-end overlay: snapshot after boot load, restore on reload */
void plat_fe_snapshot(void);
/* SI devices */
void plat_si_init(void);
void plat_si_flush(void);
void plat_si_rewrite(void);          /* the whole EEPROM image to the file now */
/* Controller Pak (pif.c) */
void plat_pak_init(void);
int plat_pak_present(void);
void plat_pak_rewrite(void);         /* the whole .mpk image to the file now */
/* osEepromLongWrite: the save copy's block (8 bytes at BLOCK) as the record holds it now (save.c) */
void port_save_refresh(const void *block, int n);
/* the debug cheats' saves (save.c): at boot (before plat_si_init), each frame */
void plat_cheats_boot(void);
void plat_cheats_frame(u32 f);

/* game symbols the platform reads (absolute, at their N64 addresses) */
extern u32 osViClock;

#endif
