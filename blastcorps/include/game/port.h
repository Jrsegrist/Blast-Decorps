#ifndef GAME_PORT_H
#define GAME_PORT_H

/*
 * Hooks for the native (Windows) port, which builds the NON_MATCHING C with
 * PORT_HOST defined (port/Makefile). They expand to nothing in every N64
 * build, so the matching build is unaffected.
 *
 *   PORT_SPIN()       inside a busy-wait loop that waits for another thread
 *                     or an interrupt (the VI counter): the port's
 *                     cooperative scheduler lets time pass there. It is a
 *                     call, so the waited-on global is re-read each time.
 *   PORT_FE_LOADED()  the front-end overlay has just been inflated over
 *                     0x801E7000: the port restores the overlay's initial
 *                     (host byte order) .data/.bss.
 *   PORT_SAVE_BEGIN(p, kind, size) / PORT_SAVE_END(p, kind, size)
 *                     around a save routine's CRC and EEPROM/pak transfer:
 *                     p is pointed at a copy of the record in N64 byte order,
 *                     then back at the record, which gets the copy's contents
 *                     in host order (port/src/platform/save.c), so save files
 *                     are byte-identical to the N64's.  Only the bytes the
 *                     routine changed go back: the game's other threads keep
 *                     writing the record during the transfer, as on the N64.
 *   PORT_SAVE_SYNC(p, kind)  a write path's CRC, just added to the copy, goes
 *                     into the record now (the N64 routine adds it to the
 *                     record itself, before the transfer starts).
 *   PORT_SAVE_BE64(x) a u64 (the save semaphore) to or from N64 byte order.
 *   PORT_SAVE_SEMBLOCK(p)  the 0x20 bytes the pak semaphore write sends from
 *                     &sem (a u64 parameter): the semaphore, then the 24 bytes
 *                     of stack that follow it on the N64 (as observed), not
 *                     the host's stack.
 *   PORT_SAVE_SWAP(p, kind, size)  in-place conversion (an involution), for
 *                     the status block, whose only multi-byte value is its
 *                     "no status" marker.
 */
/*   PORT_HALF(i)      index of halfword i of an array of 32-bit words viewed
 *                     as u16s (the N64 Mtx's elements: integer halves, then
 *                     fraction halves, two per word). The port keeps such
 *                     words in host order (as ultralib's gu writes them), so
 *                     on the little-endian host the two halves of each word
 *                     trade places: i ^ 1. */
#if defined(NON_MATCHING) && defined(PORT_HOST)
#define PORT_HALF(i) ((i) ^ 1)
#else
#define PORT_HALF(i) (i)
#endif
/*   PORT_SHAMT(n)     a variable shift amount as MIPS sllv/srlv/srav use it
 *                     (low 5 bits); in C a shift by 32 or more is undefined
 *                     and gcc folds it away. */
#if defined(NON_MATCHING) && defined(PORT_HOST)
#define PORT_SHAMT(n) ((n) & 31)
#else
#define PORT_SHAMT(n) (n)
#endif
/*   PORT_WRAP_MUL(a, b), PORT_WRAP_ADD(a, b)   a signed 32-bit multiply or add
 *                     that overflows at run time (the N64 wraps; in C it is
 *                     undefined, and MSVC has no -fwrapv): done on u32 in the
 *                     port.  The sites are the ones a signed-overflow census of
 *                     the attract mode and all levels finds (`make -C port
 *                     overflow-census`). */
#if defined(NON_MATCHING) && defined(PORT_HOST)
#define PORT_WRAP_MUL(a, b) ((s32) ((u32) (a) * (u32) (b)))
#define PORT_WRAP_ADD(a, b) ((s32) ((u32) (a) + (u32) (b)))
#else
#define PORT_WRAP_MUL(a, b) ((a) * (b))
#define PORT_WRAP_ADD(a, b) ((a) + (b))
#endif
/*   PORT_GVI(fn, v)   v, a read of the game's retrace counter gSchedFrameCount in
 *                     function fn after code the native port runs in no time;
 *                     when following an emulator, the value the emulator
 *                     read there (port/tools/cmp_spec.py GVI_FUNCS). */
/*   PORT_GVI_LAST(v)  v, a second use of the counter that IDO takes from the
 *                     register of the PORT_GVI read just before (no load of
 *                     its own): when following an emulator, that read's value. */
/*   PORT_EMU(site, v) v, a value whose timing against the retrace the native
 *                     port can't reproduce (another thread writes it after CPU
 *                     time the native code doesn't take); when following an
 *                     emulator, the value the emulator read at SITE in the same
 *                     retrace (port/tools/cmp_spec.py EMU_SITES). */
#if defined(NON_MATCHING) && defined(PORT_HOST)
unsigned int port_gvi_read(const char *fn, unsigned int v);
unsigned int port_emu_read(const char *site, unsigned int v);
unsigned int port_gvi_last(unsigned int v);
#define PORT_GVI(fn, v) port_gvi_read(fn, v)
#define PORT_GVI_LAST(v) port_gvi_last(v)
#define PORT_EMU(site, v) port_emu_read(site, v)
#else
#define PORT_GVI(fn, v) (v)
#define PORT_GVI_LAST(v) (v)
#define PORT_EMU(site, v) (v)
#endif
#define PORT_SAVE_PLAYER 0  /* player record, 0x100 bytes */
#define PORT_SAVE_TIMES 1   /* best times, 0x20 bytes */
#define PORT_SAVE_EETIMES 2 /* EEPROM best-time words, 8 bytes */
#define PORT_SAVE_STATUS 3  /* level status block, 0x40 bytes (in place) */
#if defined(NON_MATCHING) && defined(PORT_HOST)
#include <stdarg.h>
void port_spin(void);
void port_fe_loaded(void);
void port_game_print(const char *fmt, va_list ap); /* the game's debug printf */
void *port_save_begin(void *p, int kind, int size);
void *port_save_end(void *p, int kind, int size);
void port_save_sync(void *p, int kind);
unsigned long long port_save_be64(unsigned long long v);
void *port_save_semblock(const void *sem);
#define PORT_SPIN() port_spin()
#define PORT_FE_LOADED() port_fe_loaded()
#define PORT_SAVE_BEGIN(p, kind, size) ((p) = port_save_begin((p), (kind), (size)))
#define PORT_SAVE_END(p, kind, size) ((p) = port_save_end((p), (kind), (size)))
#define PORT_SAVE_SYNC(p, kind) port_save_sync((p), (kind))
#define PORT_SAVE_BE64(x) ((x) = port_save_be64(x))
#define PORT_SAVE_SWAP(p, kind, size) ((void) port_save_begin((p), (kind), (size)))
#define PORT_SAVE_SEMBLOCK(p) ((u8 *) port_save_semblock(p))
#else
#define PORT_SAVE_SWAP(p, kind, size)
#define PORT_SAVE_SEMBLOCK(p) (p)
#define PORT_SPIN()
#define PORT_FE_LOADED()
#define PORT_SAVE_BEGIN(p, kind, size)
#define PORT_SAVE_END(p, kind, size)
#define PORT_SAVE_SYNC(p, kind)
#define PORT_SAVE_BE64(x)
#endif

/*   Frame interpolation tags (bc.exe's tagged interpolation, port/src/live/
 *   live_interp.cpp).  They only tell the host who draws what; nothing is
 *   written to the display list or anywhere else in RDRAM, and the game's
 *   state is never read back, so the game runs the same with or without them.
 *   PORT_ITAG(gdl, kind, index)  the commands written from gdl on (in that
 *                     display list, and the lists it calls) belong to the
 *                     object (kind, index), until the next PORT_ITAG on that
 *                     list (PORT_ITAG_NONE: nobody's).
 *   PORT_ISPAWN(kind, index)  slot `index` of `kind` starts a new object (not
 *                     blended with what the slot showed before).
 *   PORT_ICUT_IF(c)   the game's camera of this frame is set (the end of
 *                     00000.c cam_update; bc.exe's orbit camera reads it
 *                     there, read-only); if c: the game snapped it (no
 *                     in-between pictures into this frame).
 *   PORT_DRAW_CELLS(list)  the visible-cell list (gVisibleCells) a pure drawing
 *                     path uses: the game's own, unless bc.exe's orbit camera
 *                     asks for more (live_orbit.cpp; the game logic keeps
 *                     reading gVisibleCells itself). */
#define PORT_ITAG_NONE 0
#define PORT_ITAG_HUD 1       /* 2D and HUD: shown exactly as drawn */
#define PORT_ITAG_OBJ 2       /* gVehicles[] record (vehicles, the carrier): index | lod << 8 */
#define PORT_ITAG_SHADOW 3    /* vehicle shadow quad, gVehicleShadows[] index */
#define PORT_ITAG_SHADOWFE 4  /* the flying vehicle's shadow */
#define PORT_ITAG_BUILDING 5  /* gObjects[] building object (posed or not) */
#define PORT_ITAG_BPART 6     /* a building's moving part: object << 8 | part */
#define PORT_ITAG_FALL 7      /* falling part slot gTopplingParts[] */
#define PORT_ITAG_RISE 8      /* rising piece gSinkingPart */
#define PORT_ITAG_BILLBOARD 9 /* explosion / smoke record gEffects[] */
#define PORT_ITAG_PARTICLE 10 /* particle gParticles[] */
#define PORT_ITAG_PEOPLE 11   /* person sprite gPeople[] (world-space quad) */
#define PORT_ITAG_GSHADOW 12  /* ground shadow of a gObjects[] object (world-space quad) */
#define PORT_ITAG_MARKER 13   /* people's marker */
#define PORT_ITAG_RADAR 14    /* the radar's sweep and heading arrow (blended; then back to HUD) */
#define PORT_ITAG_CUT 30
#define PORT_ITAG_SPAWN 31
/*   Widescreen tags (bc.exe's widescreen, port/src/live/live_wide.cpp), told
 *   the host the same way (nothing written, nothing read back):
 *   PORT_WTAG(gdl, kind, x0, x1)  the 2D commands written from gdl on (in that
 *                     list and the lists it calls) are, until the next
 *                     PORT_WTAG on that list:
 *     PORT_WTAG_NONE   not HUD (or centred: left where it is);
 *     PORT_WTAG_LEFT / PORT_WTAG_RIGHT  a HUD part of the left / right side
 *                      (moved to that edge with video.hud_position = edges);
 *     PORT_WTAG_SPAN   a HUD part spanning x0..x1 (screen pixels): left, right
 *                      or centred by where it lies;
 *     PORT_WTAG_TEXT_SIDES  a text flush of the HUD: each string's quads go
 *                      to the side the string is on (PORT_WTAG_TEXT);
 *     PORT_WTAG_TICKER the ticker's lit band (01C40.c; made as wide as the
 *                      picture);
 *     PORT_WTAG_STARS  the globe's starfield (spread across the picture).
 *   PORT_WTAG(vtx, PORT_WTAG_TEXT, n, fit)  a queued string's n vertices from
 *                     vtx (14B30.c; fit != 0: a centred string).
 *   PORT_WTAG(gdl, PORT_WTAG_TICKER_TEXT, 0 / 1, 0)  the front end's text flush
 *                     starts / has ended at gdl (17990.c): the ticker's
 *                     characters beyond the 4:3 picture are drawn there
 *                     (PORT_TICKER).
 *   PORT_TICKER(what, a, t)  the player select's ticker (01C40.c), told to bc.exe's
 *                     widescreen (live_wide.cpp; nothing written, nothing read back):
 *                     PORT_TICKER_RESET the line was blanked; PORT_TICKER_SHIFT
 *                     character a left the line's left end; PORT_TICKER_FRAME t (a
 *                     PortTicker) is this frame's line, drawn by the game for the 4:3
 *                     picture: the host draws the characters (and the medal icons) on
 *                     either side of it, those that left the line and those it shows
 *                     next. */
#define PORT_WTAG_NONE 0
#define PORT_WTAG_LEFT 1
#define PORT_WTAG_RIGHT 2
#define PORT_WTAG_SPAN 3
#define PORT_WTAG_TEXT_SIDES 4
#define PORT_WTAG_TICKER 5
#define PORT_WTAG_STARS 6
#define PORT_WTAG_TEXT 7
#define PORT_WTAG_TICKER_TEXT 8
#define PORT_TICKER_RESET 0
#define PORT_TICKER_SHIFT 1
#define PORT_TICKER_FRAME 2
#if defined(NON_MATCHING) && defined(PORT_HOST)
/* the ticker's state for PORT_TICKER_FRAME (01C40.c fe_player_draw_panel) */
typedef struct PortTicker {
    const unsigned short *line;   /* gScrollLine: the characters on show, from x = -pixel */
    const unsigned char *text;    /* gScrollText: the narrow text, len characters */
    int len;                      /* sslen */
    int pos;                      /* gScrollPos: the text index last fed into the line */
    int pixel, charWidth;         /* gScrollPixel, gScrollCharWidth */
    float speed, speedGoal;       /* gScrollSpeed, gScrollSpeedGoal */
    int hold;                     /* gScrollHoldTime */
    int alpha;                    /* gPanelAlpha */
    int iconX, iconPos, iconSprite;               /* the rank icon: gScrollIconX ... */
    int medalX[5], medalPos[5], medalSprite;      /* the medals 1..maxRank: gScrollMedalX ... */
    int maxRank;
    unsigned short skip[3];       /* characters drawn as nothing (gTextSpace/gFontAltBlankChar/gFontNoAdvanceChar[0]) */
    unsigned short noAdvance;     /* the one that doesn't advance (gFontNoAdvanceChar[0]) */
    unsigned short terminator;    /* gTextTerminator[0] */
    float advance;                /* gFontAdvance[0] */
    int glyphs;                   /* gScrollUsesGlyphs (the wide font: not extended) */
} PortTicker;
void port_ticker(int what, int a, const PortTicker *t);
#define PORT_TICKER(what, a, t) port_ticker((what), (int) (a), (t))
void port_itag(const void *gdl, int kind, int index);
#define PORT_ITAG(gdl, kind, index) port_itag((const void *) (gdl), (kind), (int) (index))
#define PORT_ISPAWN(kind, index) port_itag(0, PORT_ITAG_SPAWN, ((kind) << 16) | (int) (index))
#define PORT_ICUT_IF(c) port_itag(0, PORT_ITAG_CUT, (c) ? 1 : 0)
short *port_draw_cells(short *game);
#define PORT_DRAW_CELLS(list) port_draw_cells((short *) (list))
void port_wtag(const void *gdl, int kind, int x0, int x1);
#define PORT_WTAG(gdl, kind, x0, x1) port_wtag((const void *) (gdl), (kind), (int) (x0), (int) (x1))
/* bc.exe's analog throttle (45BB0.c cont_update, the digital control method's vehicles): the stick y the
 * game made from the A button (+80: full throttle, the top speed bands[13] * y / 80) may become the raw pad's
 * y (the trigger's pressure, put there by the host when it read the pad; recorded and replayed with it).
 * Nothing changes unless bc.exe's analog_throttle is on. */
void port_throttle(int rawY, unsigned short *button, signed char *y);
#define PORT_THROTTLE(rawY, button, y) port_throttle((int) (rawY), (button), (signed char *) (y))
/* bc.exe's level viewer (port/src/live/live_viewer.cpp): at the end of every frame of hd_main_thread's frame
 * loop, the level the viewer asked for (if any) is requested the game's own way: levelno and gNextGameMode =
 * MODE_LEVEL_START, so the mode switch sets it up (plat_core.c port_viewer_frame).  Nothing changes unless the
 * viewer asked; it asks only while no front end is loaded and no fade or mode switch is under way. */
void port_viewer_frame(void);
#define PORT_VIEWER_FRAME() port_viewer_frame()
/* Edited level files (mods, bc.exe's level editor: port/src/load/port_mods.c).  Around lvl_load_file's
 * hd_rom_load: PORT_LEVEL_PRE(level, dest) before it, PORT_LEVEL_POST(level, rom, dest, size) after it, where
 * the host puts the edited file (big-endian, as the ROM would hold it) over what was inflated, sets *size and
 * swaps it as it swapped the ROM's.  Nothing changes unless that level has an edited file. */
void port_level_pre(int level, unsigned int dest);
void port_level_post(int level, unsigned int rom, unsigned int dest, unsigned int *size);
#define PORT_LEVEL_PRE(level, dest) port_level_pre((int) (level), (unsigned int) (dest))
#define PORT_LEVEL_POST(level, rom, dest, size) \
    port_level_post((int) (level), (unsigned int) (rom), (unsigned int) (dest), (unsigned int *) (size))
/* A ROM from the shiftable build with files moved (port/src/rdram.c): hd_rom_load's ROM range (the original
 * offsets the code holds) becomes the file's range in this ROM, all of it (a file may have grown), before
 * the 16 KB pieces are read.  Nothing changes with the retail ROM. */
void port_rom_span(unsigned int *rom, unsigned int *size);
/* The visibility test's display list (5FD50.c cull_rsp_test): its host address, for the platform's model of the
 * test (port/src/platform/os_hw.c); the task in N64 memory holds the address the N64 gives the list. */
void port_cull_data(const void *dl);
#define PORT_ROM_SPAN(rom, size) port_rom_span((unsigned int *) (rom), (unsigned int *) (size))
#else
#define PORT_ROM_SPAN(rom, size)
#define PORT_LEVEL_PRE(level, dest)
#define PORT_LEVEL_POST(level, rom, dest, size)
#define PORT_VIEWER_FRAME()
#define PORT_THROTTLE(rawY, button, y)
#define PORT_ITAG(gdl, kind, index)
#define PORT_ISPAWN(kind, index)
#define PORT_ICUT_IF(c)
#define PORT_DRAW_CELLS(list) ((short *) (list))
#define PORT_WTAG(gdl, kind, x0, x1)
#define PORT_TICKER(what, a, t)
#endif

#endif
