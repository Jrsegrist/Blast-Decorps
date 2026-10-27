/* Host (Windows) services for the platform layer.  This header uses no
 * libultra types, so it can be included both by files that see <windows.h>
 * (plat_host.c) and by files that see the game's <ultra64.h>. */
#ifndef PLAT_HOST_H
#define PLAT_HOST_H

#include "port_cc.h"

typedef void *HostFiber;

/* Turn the calling (main) thread into a fiber; returns it. */
HostFiber host_fiber_init(void);
/* A new fiber running fn(arg) on its own host stack (the game's N64 stacks
 * are too small for gcc -O2 code and stay unused). */
HostFiber host_fiber_create(void (*fn)(void *), void *arg);
void host_fiber_switch(HostFiber f);
void host_fiber_delete(HostFiber f);

/* stderr logging */
void host_log(const char *fmt, ...) PORT_PRINTF(1, 2);
PORT_NORETURN void host_fatal(const char *fmt, ...) PORT_PRINTF(1, 2);
int host_env(const char *name);   /* is the environment variable set (debug switches) */
extern int host_verbose;

/* bc.exe is a Windows GUI program: with host_gui set, host_fatal and the
 * crash reporter also show a message box (nobody may be reading stderr),
 * after hiding host_gui_window (an HWND, so a fullscreen window doesn't
 * cover the box); host_log_path names the log file stderr went to, if any. */
extern int host_gui;
extern void *host_gui_window;
extern const char *host_log_path;
/* a message box (UTF-8 text); `error` picks the icon */
void host_message(const char *text, int error);

/* fopen for a UTF-8 path (falls back to the ANSI code page); returns FILE * */
void *host_fopen(const char *path, const char *mode);

/* per-frame trace file (no-op until opened) */
int host_trace_open(const char *path);
void host_trace(const char *fmt, ...) PORT_PRINTF(1, 2);
void host_trace_flush_raw(void);   /* crash.c: flush it without taking the C runtime's lock */

/* whole-file helpers: read returns a malloc'd buffer (NULL if missing) */
void *host_read_file(const char *path, unsigned *size);
int host_write_file(const char *path, const void *data, unsigned size);
void *host_realloc(void *p, unsigned size);

/* MSVC x64 build: make the function at FN call port_entry_hook (os_thread.c)
 * before its first instruction (--sync's entry points, --calls); 0 if done
 * (or already), -1 if its first instruction can't be relocated */
int host_entry_hook(unsigned fn);

/* Host memory RT64 can read through its extended RDRAM addresses: an address
 * with bit 31 set is an offset from RDRAM's base (0x80000000 here), i.e. the
 * host pointer itself, so it must lie between RDRAM's end and 4 GB (bc.exe's
 * widescreen ticker, live_wide.cpp).  host_low_reserve takes it once, first
 * thing in bc.exe's main: before SDL, RT64 and the graphics driver, which on
 * some PCs fill that whole range (0 if done).  host_low_alloc hands out pieces
 * of it (16-byte aligned, zeroed; NULL when it can't: logged once, counted);
 * host_low_summary logs the use at exit.  host_low_test (testing, bc.exe
 * --low-test MASK): 1 crowd the range before the reservation (one 1 MB hole
 * left), 2 crowd it after (as the drivers do), 4 no reservation at start-up
 * (taken at the first use, as before). */
int host_low_reserve(void);
void *host_low_alloc(unsigned size, const char *who);
void host_low_summary(void);
extern unsigned host_low_test;

/* The host's work inside the game's step, for bc.exe's report of long game
 * steps (live_frametimes.cpp): milliseconds and counts, summed since the start
 * (main thread only; the platform layer adds to them).  host_ms: a clock in
 * ms (QueryPerformanceCounter). */
typedef struct {
    double dma_ms, audio_ms, gfx_ms, save_ms;   /* PI DMAs (with the load-time swaps), audio tasks and the
                                                   AI's output (bc.exe's sound device), gfx tasks (bc.exe's
                                                   renderer), save files written */
    unsigned dma_n, audio_n, gfx_n, save_n;
    unsigned long long dma_bytes;
} HostProf;
extern HostProf host_prof;
double host_ms(void);

/* flush-to-zero for the current fiber (the game's osInitialize sets FPCSR FS) */
void host_set_fpu_mode(void);

/* exit the process (flushes stdio); host_exit_hook (if set) runs first */
PORT_NORETURN void host_exit(int code);
extern void (*host_exit_hook)(void);

/* Crash reporting (crash.c).  host_crash_init runs first in main (both
 * exes): it reads --no-msgbox and --crash-dir DIR from the command line (and
 * BC_NO_MSGBOX from the environment) and installs the handlers.  A crash
 * writes a report (exception, module + RVA, registers, backtrace with names,
 * game state) to stderr and bc-crash-YYYYMMDD-HHMMSS.txt, a minidump
 * bc-crash-YYYYMMDD-HHMMSS.dmp next to it (in the crash folder: --crash-dir,
 * bc.exe: bc.log's folder, bc_headless: the current folder), shows a message
 * box (bc.exe, unless --no-msgbox) and exits with code 4. */
extern int host_no_msgbox;   /* --no-msgbox: no dialog of any kind; errors are only logged */
#define HOST_CRASH_CODE 0xE0424301u   /* exception code of host_crash_now's reports */
void host_crash_init(int argc, char **argv, const char *exe_kind, const char *version);
void host_crash_set_dir(const char *dir);   /* UTF-8; NULL/"" = the current folder */
/* `describe` adds the game state to a report (writes one line into buf; no
 * stdio or heap); `after` (may be NULL) runs once the report and dump are out */
void host_crash_set_describe(void (*describe)(char *buf, unsigned size), void (*after)(void));
void host_crash_rearm(void);                /* re-install the filter (after RT64 starts) */
PORT_NORETURN void host_crash_now(const char *why);   /* report a crash from here */
int host_snprintf(char *buf, unsigned size, const char *fmt, ...) PORT_PRINTF(3, 4);
/* --crash-test KIND[:FRAME] (testing the reporter): av, div, stack, thread,
 * abort, fatal, box, heaplock (a fault with the process heap's lock held),
 * heapbad (a fault after corrupting a heap block header), heapover (a byte
 * written past a malloc block: the AddressSanitizer build stops; else harmless),
 * cxx (bc.exe: an uncaught C++ exception) at game frame
 * FRAME (default 1); host_crash_test_frame is called once per frame */
int host_crash_test_set(const char *spec);
void host_crash_test_frame(unsigned frame);
extern void (*host_crash_test_cxx)(void);
/* debugging: --watch FRAME:ADDR[:N] logs the first N writes to ADDR's 4 KB
 * page from that frame on (host_watch_frame is called once per frame) */
int host_watch_set(const char *spec);
void host_watch_frame(unsigned frame);

/* Run options (headless_main.c parses them; the platform reads them as
 * plat_cfg).  Plain C types only: unsigned/int are 32 bits here. */
typedef struct HostOpts {
    const char *rom_path;
    unsigned char *rom;        /* the ROM image (big-endian .z64) */
    unsigned rom_size;
    unsigned frames;           /* stop after this many frames (0 = never) */
    unsigned max_vis;          /* stop after this many retraces (0 = never) */
    unsigned gfx_cycles;       /* RSP+RDP time of a frame's gfx task (counts) */
    unsigned aud_cycles;       /* RSP time of an audio task */
    unsigned small_gfx_cycles; /* RSP time of other gfx tasks (shadows, cull test) */
    unsigned count_per_gettime;/* CPU counts charged per osGetTime/osGetCount */
    unsigned boot_count;       /* count (time since power-on) when hd_code starts */
    const char *dump_dir;
    unsigned *dump_frames;     /* frame numbers to dump RDRAM at (ascending) */
    unsigned n_dump_frames;
    unsigned dump_every;       /* also dump every N frames (0 = off) */
    const char *input_path;
    const char *gettime_path;
    const char *frame_done_path;
    const char *eeprom_path;
    int eeprom_present;
    int cont_present;          /* controller 1 plugged in */
    const char *cmdline;       /* debug command line the game reads at PI 0xFFB000 */
    const char *trace_path;    /* one summary line per frame */
    const char *syms_path;     /* `nm -n` of the exe (names for --clock, crash reports) */
    const char *clock_path;    /* keyed osGetTime/osGetCount values (os_time.c) */
    const char *sync_path;     /* the emulator's thread timing (os_thread.c) */
    const char *frame_sp_path; /* when each frame task leaves the RSP (os_time.c) */
    const char *calls;         /* function names whose calls are logged (os_thread.c) */
    int quiet;
    int game_print;            /* print the game's debug messages (debug_printf) */
    const char *mpk_path;      /* Controller Pak image (.mpk), or NULL: no pak */
    const char *wav_path;      /* --wav: the AI stream (what the game plays) as a WAV file */
    int wav_all;               /* --wav-all: also the buffers a full AI FIFO drops (every task's output) */
    const char *audio_capture; /* --audio-capture: audio tasks' inputs/outputs (port/tools/audio) */
    unsigned audio_capture_max;
    int audio_off;             /* --no-audio: audio tasks are not run (no output) */
    unsigned (*pokes)[4];      /* --poke: {frame, address, size, value}: RAM writes when a frame is sent */
    unsigned n_pokes;
    int cheats_save;           /* the debug cheats' saves (save.c): 1 = once the cheats were on in this run,
                                  the save files are <name>.cheats.<ext> (bc.exe's [game] cheats_save =
                                  separate; --cheats-save separate), 0 = the normal files (bc_headless's default) */
    unsigned cheats_on_frame;  /* --cheats-on F: turn the debug cheats on when frame F is sent, as bc.exe's
                                  setting does in a running game (0 = never) */
    const struct HostLive *live; /* renderer, window, live input (bc.exe); NULL in bc_headless */
    const char **mod_paths;    /* --mod FILE (repeatable): .bcmod files applied to the ROM's level files in memory
                                  (port/src/load/port_mods.h); a mod that doesn't fit the ROM stops the run */
    unsigned n_mod_paths;
    const char *mod_out;       /* --mod-out DIR (testing): the edited level files written there at start-up */
} HostOpts;

/* Saving switched off (save.c): the game still saves into the EEPROM and pak images in memory (it sees no
 * difference), but nothing is written to the files.  bc.exe's level editor sets it for a test run of an
 * edited level ("Play from here"). */
extern int host_saves_blocked;
void host_saves_block(const char *why);

/* The VI registers as libultra's VI manager would program them for the
 * frame on screen (os_hw.c fills them at every retrace). */
typedef struct {
    unsigned status, origin, width, intr, burst, v_sync, h_sync, leap, h_start, v_start, v_burst,
        x_scale, y_scale;
} HostViRegs;

/* What the windowed build (bc.exe, port/src/live/) adds to the platform.
 * The game logic steps exactly as in bc_headless: these hooks observe the
 * game (they render its graphics tasks and show its frames) and pace the
 * virtual clock to real time, but never change what the game sees, except
 * through `input` (the player's controller) and, with the widescreen
 * option on, host_cull_wide. */
typedef struct HostLive {
    /* after the ROM images are in RDRAM, before the game starts */
    void (*boot)(void);
    /* a graphics task the RSP would run: physical addresses of the ucode
     * text and data and of the display list */
    void (*gfx_task)(unsigned ucode, unsigned ucode_data, unsigned data_ptr, unsigned data_size);
    /* a retrace (virtual time `when`, in counts): VI registers of the frame
     * on screen; paces to real time and presents */
    void (*vi)(const HostViRegs *regs, unsigned vi_count, unsigned long long when, unsigned frames);
    /* controller 1 now: returns 1 and fills the pad if the live input is used */
    int (*input)(unsigned short *button, signed char *x, signed char *y);
    /* a buffer the AI starts playing: `frames` host-order L/R sample pairs
     * at `rate` Hz (virtual time; the output paces it to real time) */
    void (*audio)(const short *lr, unsigned frames, unsigned rate);
} HostLive;

/* Widescreen (bc.exe's optional enhancement; os_hw.c): the width of
 * cull_rsp_test's visibility test as a multiple of the game's 4:3 view, so
 * level cells (and the objects on them) at the edges of a wider picture are
 * drawn.  1.0 (the default) is the exact model of the N64's ucode; another
 * value changes what the game sees (which cells are visible), so the game
 * can then differ from the N64's.  bc_headless: --cull-wide K (testing). */
extern double host_cull_wide;

/* Frame interpolation tags from the game (port.h PORT_ITAG; plat_core.c):
 * bc.exe's tagged interpolation sets this (NULL: a tag costs one call).
 * `addr` is the display-list address the tag applies from (0 for
 * PORT_ISPAWN and PORT_ICUT).  It must not change anything the game sees. */
extern void (*host_itag_hook)(unsigned addr, int kind, int index);
/* ... and bc.exe's level viewer's, while it captures a frame (live_viewer.cpp; the same tags, kinds < 30) */
extern void (*host_viewer_itag_hook)(unsigned addr, int kind, int index);

/* Widescreen tags from the game (port.h PORT_WTAG; plat_core.c): bc.exe's
 * widescreen sets this while the picture is wider than 4:3 (live_wide.cpp).
 * It must not change anything the game sees. */
extern void (*host_wtag_hook)(unsigned addr, int kind, int x0, int x1);

/* The player select's ticker (port.h PORT_TICKER; plat_core.c): bc.exe's
 * widescreen sets this (live_wide.cpp); `t` is a PortTicker (game/port.h).
 * It must not change anything the game sees.  port_ticker_gfx builds the
 * commands for the characters and icons beyond the 4:3 picture in host
 * memory, port_glyph_decode a font glyph from the ROM (plat_core.c). */
extern void (*host_ticker_hook)(int what, int a, const void *t);
int port_ticker_gfx(const void *t, int pass, double ext, int shift, const unsigned short *hist, int nhist,
                    void *gfx, int max, void *vtx, int maxVtx, unsigned (*glyph)(unsigned id), int *nvtx);
int port_glyph_decode(unsigned id, unsigned char *dst, int max);

/* bc.exe's orbit camera (live_orbit.cpp; NULL otherwise).  host_camera_hook:
 * once a frame, at the end of the game's camera function (00000.c
 * cam_update, PORT_ICUT_IF), `snap` = its snap flag; it may only read.
 * host_draw_cells_hook: the visible-cell list for the pure drawing paths
 * (PORT_DRAW_CELLS: the level's geometry, unlisted buildings drawn as they
 * are); `game` is the game's own gVisibleCells, which its logic keeps using.
 * Returns `game` or a sorted, -1-terminated list containing it. */
extern void (*host_camera_hook)(int snap);
extern short *(*host_draw_cells_hook)(short *game);
/* bc.exe's orbit camera's camera-relative steering ([camera] relative_steering;
 * NULL otherwise): each controller read (plat_input_read: the --input file's
 * or the player's), it may turn the stick before the game gets it.  It is
 * the one hook that changes what the game sees, by design and only in orbit
 * mode with the stick held (see live_orbit.cpp steerHook). */
extern void (*host_input_hook)(unsigned short *button, signed char *x, signed char *y);
/* bc.exe's analog throttle ([controller] analog_throttle; NULL otherwise).  host_throttle_input_hook: each
 * controller read (plat_input_read, before host_input_hook), the throttle trigger's pressure `trigger`
 * (0..255; -1: none: the --input line's 4th value, or host_live_trigger, the player's, set by bc.exe's
 * input) goes into the pad (the A button and the raw stick y: live_throttle.cpp).  host_throttle_hook:
 * PORT_THROTTLE in the game's pad processing. */
extern void (*host_throttle_input_hook)(unsigned short *button, signed char *x, signed char *y, int trigger);
extern void (*host_throttle_hook)(int rawY, unsigned short *button, signed char *y);
extern int host_live_trigger;

/* The game's debug cheats (save.c): gDebugCheats, which the debug command line's -c / -C sets at boot
 * (C-right completes a level, C-up + C-right fails it, Z + C-right is a perfect finish).
 * host_cheats_set: on or off in a running game (bc.exe's setting and hotkey; main thread, between game
 * steps: the game reads it each time).  host_cheats_on: gDebugCheats now.  host_cmdline_cheats: does
 * this debug command line turn them on (parse_cmdline's reading).  With HostOpts.cheats_save
 * (host_cheats_set_save), once they were on the saves go to the cheats files (save.c explains when);
 * host_cheats_saves: 0 the normal files, 1 waiting for the front end's save thread to go (the next
 * level), 2 the cheats files; host_cheats_save_name: the EEPROM's cheats file (else the pak's), or NULL
 * when the run keeps no saves. */
void host_cheats_set(int on);
int host_cheats_on(void);
int host_cmdline_cheats(const char *cmdline);
void host_cheats_set_save(int separate);
int host_cheats_saves(void);
const char *host_cheats_save_name(void);

/* bc.exe's level viewer (plat_core.c; live_viewer.cpp): host_viewer_request asks for level `level` (bonus
 * levels: `vehicle` stands for the player's pick); the game takes it at the end of a frame in a level or the
 * attract demo (port.h PORT_VIEWER_FRAME).  host_viewer_state: 0 nothing asked, 1 waiting for such a frame,
 * 2 asked (gNextGameMode = MODE_LEVEL_START); host_viewer_done puts it back to 0. */
void host_viewer_request(int level, int vehicle);
int host_viewer_state(void);
void host_viewer_done(void);

/* platform entry (plat_core.c): boots the game and never returns */
PORT_NORETURN void plat_start(const HostOpts *o);

/* option parsing and start-up shared by bc_headless.exe and bc.exe
 * (headless_main.c).  `extra` gets the options host_main doesn't know
 * (return 1 if consumed; *i may advance), `prestart` runs just before the
 * game starts (after the ROM is loaded). */
int host_main(int argc, char **argv, int (*extra)(int argc, char **argv, int *i, HostOpts *o),
              void (*prestart)(HostOpts *o));

/* bc.exe's hooks into host_main (NULL in bc_headless): `host_opts_hook`
 * sets defaults (from the config file) before the command line is parsed;
 * `host_rom_hook` runs after it and finds and checks the ROM (the command
 * line's, the configured one, or a file dialog), setting o->rom_path. */
extern void (*host_opts_hook)(HostOpts *o);
extern void (*host_rom_hook)(HostOpts *o);

#endif
