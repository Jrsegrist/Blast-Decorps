/* bc_headless.exe: the game logic with no graphics or audio output.
 *
 *   bc_headless.exe ROM [options]
 *     --frames N         stop after N frames (frame = a frame-ending gfx task)
 *     --vis N            stop after N retraces
 *     --dump F1,F2,...   dump RDRAM (8 MB, host byte order) at these frames
 *     --dump-every N     ... and every N frames
 *     --dump-dir DIR     where dumps go (default .)
 *     --trace FILE       one line per frame: retrace, time, game mode, level
 *     --input FILE       controller 1 recording (see input.c)
 *     --no-controller    controller 1 unplugged
 *     --gettime FILE     injected osGetTime values (see os_time.c)
 *     --frame-done FILE  injected frame completion retraces (see os_time.c)
 *     --clock FILE       osGetTime/osGetCount values keyed by caller (os_time.c)
 *     --syms FILE        `nm -n` of this exe (bc_headless.syms, which the build
 *                        writes next to it): caller names for --clock, symbols
 *     --sync FILE        the emulator's time at each thread switch point
 *                        (os_thread.c; port/tools/compare.py writes it)
 *     --calls F,G,...    log every call of these game functions with its first
 *                        argument words, per frame (compare.py calls; plat_host.c)
 *     --load-log         log every DMA/decompressed load and its asset kind
 *     --frame-sp FILE    per frame, when its task leaves the RSP ("@N VI.F",
 *                        like --frame-done; default: with the RDP)
 *     --eeprom FILE      EEPROM image (read at boot, written on save)
 *     --no-eeprom        no EEPROM chip
 *     --gfx-cycles N     RCP time of a frame's gfx task, in 46.875 MHz counts
 *                        (default 781250 = one retrace)
 *     --small-gfx-cycles N, --aud-cycles N   other gfx tasks / audio tasks (0)
 *     --gettime-cost N   CPU counts charged per osGetTime/osGetCount (0)
 *     --boot-count N     virtual time (counts since power-on) when hd_code
 *                        starts (0); retraces stay on multiples of 781250
 *     --cmdline STR      the debug command line at PI 0xFFB000 (default none;
 *                        at most 63 bytes): "-c" turns on the debug cheats
 *     --cheats-on F      turn the debug cheats on when frame F is sent, as
 *                        bc.exe's setting or hotkey does in a running game
 *     --cheats-save M    normal (default) or separate: once the cheats were on,
 *                        the saves go to <eeprom>.cheats.eep / <mpk>.cheats.mpk
 *                        (bc.exe's [game] cheats_save; save.c)
 *     --mpk FILE         Controller Pak 1 image (mupen64plus .mpk format: four
 *                        32 KB paks, controller 1's first; created if missing)
 *     --print            print the game's debug messages (debug_printf)
 *     --wav FILE         write what the game plays (the AI buffers) to FILE
 *                        (and FILE.frames: game frame -> sample offset)
 *     --wav-all          ... including buffers a full AI FIFO drops
 *     --no-audio         don't run audio tasks (the game logic is the same)
 *     --cull-wide K      the visibility test as wide as K times the 4:3 view
 *                        (bc.exe's widescreen option; default 1, the N64's)
 *     --audio-capture FILE[:N]  record (the first N) audio tasks' inputs and
 *                        outputs for port/tools/audio (asp_lle, asp_replay)
 *     --poke F:ADDR:SIZE:VALUE,...  write RAM (hex address and value, host
 *                        order at that width) when frame F is sent, after its
 *                        dump (compare.py's CMP_POKE does the same in the
 *                        emulator): e.g. pick the level a run loads
 *     --watch F:ADDR[:N] debugging: log the first N (20) writes to ADDR's 4 KB
 *                        page from frame F on (eip and stack code pointers)
 *     --no-msgbox        never show a dialog (bc.exe: errors, crashes, RT64 and
 *                        SDL message boxes are only logged); for automated runs
 *     --crash-dir DIR    where a crash's bc-crash-*.dmp/.txt go (default: the
 *                        current folder; bc.exe: bc.log's folder)
 *     --crash-test KIND[:F]  crash on purpose at frame F (testing the crash
 *                        report): av, div, stack, thread, abort, fatal, box,
 *                        heaplock, heapbad, heapover, cxx (bc.exe)
 *     --mod FILE         a level mod (.bcmod, port/src/load/level_mod.h): the
 *                        level files it changes are edited in memory as the
 *                        game reads them from the ROM (repeatable; a mod made
 *                        for another ROM stops the run)
 *     --mod-out DIR      (testing) write the edited level files to DIR
 *                        (level_NN.bin, as the ROM would hold them inflated)
 *     --level-dir DIR    (testing) the game reads DIR\level_NN.bin instead of
 *                        the ROM's level NN wherever there is one (inflated, in
 *                        the ROM's byte order: bcfiles geo-rebuild / mod-apply)
 *     -v / -vv / -q      verbose / more verbose / quiet */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "rdram.h"
#include "plat_host.h"
#include "load/port_load.h"
#include "load/port_mods.h"

void (*host_opts_hook)(HostOpts *o);
void (*host_rom_hook)(HostOpts *o);

static void usage(const char *bad) {
    if (host_gui) {
        if (bad != NULL)
            host_fatal("Unknown command-line option, or one without its value: \"%s\".\n\nREADME.txt lists the options.",
                       bad);
        host_fatal("No ROM was given.");
    }
    fprintf(stderr, "usage: bc_headless.exe ROM [--frames N] [--vis N] [--dump F1,F2..] [--dump-every N]\n"
                    "       [--dump-dir DIR] [--trace FILE] [--input FILE] [--no-controller] [--gettime FILE]\n"
                    "       [--frame-done FILE] [--frame-sp FILE] [--clock FILE] [--syms FILE] [--sync FILE]\n"
                    "       [--calls F,G..] [--load-log]\n"
                    "       [--eeprom FILE] [--no-eeprom] [--gfx-cycles N] [--small-gfx-cycles N]\n"
                    "       [--aud-cycles N] [--gettime-cost N] [--boot-count N] [--cmdline STR]\n"
                    "       [--cheats-on F] [--cheats-save normal|separate]\n"
                    "       [--mpk FILE] [--print] [--wav FILE] [--wav-all] [--no-audio] [--audio-capture FILE[:N]]\n"
                    "       [--cull-wide K]\n"
                    "       [--poke F:ADDR:SIZE:VALUE,..] [--watch F:ADDR[:N]] [--no-msgbox] [--crash-dir DIR]\n"
                    "       [--crash-test KIND[:F]] [--mod FILE] [--mod-out DIR] [--level-dir DIR] [-v] [-q]\n");
    exit(1);
}

static unsigned num(const char *s) {
    return (unsigned) strtoul(s, NULL, 0);
}

static int g_game_print;
unsigned plat_frames(void); /* save.c */

/* the game's debug printf (hd_code 56010.c debug_printf), with --print:
 * to stderr like the platform's own log, prefixed with the frame number */
void port_game_print(const char *fmt, va_list ap) {
    if (g_game_print) {
        fprintf(stderr, "game f=%u: ", plat_frames());
        vfprintf(stderr, fmt, ap);
        if (fmt[0] != 0 && fmt[strlen(fmt) - 1] != '\n') fputc('\n', stderr);
    }
}

/* gensyms' stubs.c: a function nothing defines was called */
void port_stub_hit(const char *name) {
    host_fatal("unimplemented function called: %s", name);
}

static int cmp_u(const void *a, const void *b) {
    unsigned x = *(const unsigned *) a, y = *(const unsigned *) b;
    return x < y ? -1 : x > y;
}

int host_main(int argc, char **argv, int (*extra)(int argc, char **argv, int *i, HostOpts *o),
              void (*prestart)(HostOpts *o)) {
    HostOpts o;
    int i;
    size_t rom_size;
    const char *level_dir = NULL;
    memset(&o, 0, sizeof o);
    o.gfx_cycles = 781250;
    o.cont_present = 1;
    o.eeprom_present = 1;
    if (host_opts_hook != NULL) host_opts_hook(&o);
    for (i = 1; i < argc; i++) {
        const char *a = argv[i];
#define ARG() (i + 1 < argc ? argv[++i] : (usage(a), (char *) NULL))
        if (extra != NULL && extra(argc, argv, &i, &o)) continue;
        if (!strcmp(a, "--frames")) o.frames = num(ARG());
        else if (!strcmp(a, "--vis")) o.max_vis = num(ARG());
        else if (!strcmp(a, "--dump")) {
            char *s = ARG(), *tok;
            for (tok = strtok(s, ","); tok; tok = strtok(NULL, ",")) {
                o.dump_frames = realloc(o.dump_frames, (o.n_dump_frames + 1) * sizeof(unsigned));
                o.dump_frames[o.n_dump_frames++] = num(tok);
            }
            qsort(o.dump_frames, o.n_dump_frames, sizeof(unsigned), cmp_u);
        } else if (!strcmp(a, "--dump-every")) o.dump_every = num(ARG());
        else if (!strcmp(a, "--dump-dir")) o.dump_dir = ARG();
        else if (!strcmp(a, "--trace")) o.trace_path = ARG();
        else if (!strcmp(a, "--input")) o.input_path = ARG();
        else if (!strcmp(a, "--no-controller")) o.cont_present = 0;
        else if (!strcmp(a, "--gettime")) o.gettime_path = ARG();
        else if (!strcmp(a, "--frame-done")) o.frame_done_path = ARG();
        else if (!strcmp(a, "--clock")) o.clock_path = ARG();
        else if (!strcmp(a, "--syms")) o.syms_path = ARG();
        else if (!strcmp(a, "--sync")) o.sync_path = ARG();
        else if (!strcmp(a, "--frame-sp")) o.frame_sp_path = ARG();
        else if (!strcmp(a, "--calls")) o.calls = ARG();
        else if (!strcmp(a, "--load-log")) port_load_verbose = 1;
        else if (!strcmp(a, "--eeprom")) o.eeprom_path = ARG();
        else if (!strcmp(a, "--no-eeprom")) o.eeprom_present = 0;
        else if (!strcmp(a, "--gfx-cycles")) o.gfx_cycles = num(ARG());
        else if (!strcmp(a, "--small-gfx-cycles")) o.small_gfx_cycles = num(ARG());
        else if (!strcmp(a, "--aud-cycles")) o.aud_cycles = num(ARG());
        else if (!strcmp(a, "--gettime-cost")) o.count_per_gettime = num(ARG());
        else if (!strcmp(a, "--boot-count")) o.boot_count = num(ARG());
        else if (!strcmp(a, "--cmdline")) o.cmdline = ARG();
        else if (!strcmp(a, "--cheats-on")) o.cheats_on_frame = num(ARG());
        else if (!strcmp(a, "--cheats-save")) {
            const char *s = ARG();
            if (!strcmp(s, "separate")) o.cheats_save = 1;
            else if (!strcmp(s, "normal")) o.cheats_save = 0;
            else usage(s);
        }
        else if (!strcmp(a, "--print")) o.game_print = g_game_print = 1;
        else if (!strcmp(a, "--mpk")) o.mpk_path = ARG();
        else if (!strcmp(a, "--wav")) o.wav_path = ARG();
        else if (!strcmp(a, "--wav-all")) o.wav_all = 1;
        else if (!strcmp(a, "--no-audio")) o.audio_off = 1;
        else if (!strcmp(a, "--cull-wide")) {
            host_cull_wide = atof(ARG());
            if (!(host_cull_wide >= 1.0 && host_cull_wide <= 4.0)) host_fatal("--cull-wide: from 1 to 4");
        }
        else if (!strcmp(a, "--no-msgbox")) host_no_msgbox = 1;   /* (read first by host_crash_init) */
        else if (!strcmp(a, "--crash-dir")) host_crash_set_dir(ARG());
        else if (!strcmp(a, "--crash-test")) {
            const char *s = ARG();
            if (host_crash_test_set(s)) usage(s);
        } else if (!strcmp(a, "--watch")) {
            const char *s = ARG();
            if (host_watch_set(s)) usage(s);
        } else if (!strcmp(a, "--poke")) {
            /* F:ADDR:SIZE:VALUE,... (ADDR and VALUE hex): after frame F's dump */
            char *s = ARG(), *tok;
            for (tok = strtok(s, ","); tok; tok = strtok(NULL, ",")) {
                unsigned f, ad, sz, v;
                if (sscanf(tok, "%u:%x:%u:%x", &f, &ad, &sz, &v) != 4 || (sz != 1 && sz != 2 && sz != 4) ||
                    ad < 0x80000000u || ad >= 0x80800000u || ad % sz)
                    usage(tok);
                o.pokes = realloc(o.pokes, (o.n_pokes + 1) * sizeof *o.pokes);
                o.pokes[o.n_pokes][0] = f, o.pokes[o.n_pokes][1] = ad;
                o.pokes[o.n_pokes][2] = sz, o.pokes[o.n_pokes][3] = v;
                o.n_pokes++;
            }
        }
        else if (!strcmp(a, "--audio-capture")) {
            char *s = ARG(), *colon = strrchr(s, ':');
            /* FILE:N (a drive letter's colon is followed by a path, not digits) */
            if (colon != NULL && colon[1] >= '0' && colon[1] <= '9') {
                o.audio_capture_max = num(colon + 1);
                *colon = 0;
            }
            o.audio_capture = s;
        }
        else if (!strcmp(a, "--mod")) {
            o.mod_paths = realloc((void *) o.mod_paths, (o.n_mod_paths + 1) * sizeof *o.mod_paths);
            o.mod_paths[o.n_mod_paths++] = ARG();
        }
        else if (!strcmp(a, "--mod-out")) o.mod_out = ARG();
        else if (!strcmp(a, "--level-dir")) level_dir = ARG();
        else if (!strcmp(a, "-v")) host_verbose = 1;
        else if (!strcmp(a, "-vv")) host_verbose = 2;
        else if (!strcmp(a, "-q")) o.quiet = 1;
        else if (a[0] == '-') usage(a);
        else if (o.rom_path == NULL) o.rom_path = a;
        else usage(a);
#undef ARG
    }
    if (host_rom_hook != NULL) host_rom_hook(&o);
    if (o.rom_path == NULL) usage(NULL);
    setvbuf(stdout, NULL, _IOLBF, 4096);   /* (the UCRT wants a size; Windows has no line buffering) */
    if (rdram_map())
        host_fatal("Can't reserve the N64's memory at 0x80000000: %s", rdram_error());
    if (rdram_load(o.rom_path)) host_fatal("%s", rdram_error());
    o.rom = rom_bytes(&rom_size);
    o.rom_size = (unsigned) rom_size;
    host_watch_frame(0);   /* (--watch 0:ADDR: from boot) */
    /* level mods: applied in memory to the level files the game reads from the ROM (none: nothing changes) */
    port_mods_log_hook = host_log;
    for (i = 0; i < (int) o.n_mod_paths; i++) {
        char err[1024];
        if (port_mods_add(o.mod_paths[i], err, sizeof err)) host_fatal("The mod can't be used: %s", err);
    }
    if (level_dir != NULL) {
        char err[1200];
        const int n = port_mods_read_levels(level_dir, err, sizeof err);
        if (n < 0) host_fatal("--level-dir: %s", err);
        host_log("--level-dir: %d level file%s from %s\n", n, n == 1 ? "" : "s", level_dir);
    }
    if (o.mod_out != NULL) {
        const int n = port_mods_write_levels(o.mod_out);
        if (n < 0) host_fatal("--mod-out %s: can't write the edited level files", o.mod_out);
        host_log("mods: %d edited level file%s written to %s\n", n, n == 1 ? "" : "s", o.mod_out);
    }
    if (prestart != NULL) prestart(&o);
    plat_start(&o);
}

#ifndef PORT_LIVE
int main(int argc, char **argv) {
    host_crash_init(argc, argv, "bc_headless", NULL);
    return host_main(argc, argv, NULL, NULL);
}
#endif
