# How Blast Corps works: system docs

These docs explain the game's systems from the decomp, for modders and
decomp contributors who know C and roughly what an N64 is but haven't read
this code. Each one gives an overview, the data structures and per-frame
flow, code references (`src.us.v11/...` paths are relative to `blastcorps/`),
modding notes, US 1.0 vs 1.1 differences and open questions.

Claims are from reading the code unless marked "measured" (run in the
native port's `bc_headless`). The docs describe formats, not data: nothing
here is dumped from the ROM.

## Engine

| doc | what it covers |
| --- | --- |
| [overview](overview.md) | boot (init, hd_boot), the threads and message queues, the main loop, the game modes (`MODE_*`) and their flow, one frame's order of work, frame pacing, and a map of the source files |
| [memory](memory.md) | the code segments (init, hd_code, the front-end overlay), the 4 MB RDRAM map, the bump heap and what each mode and level allocates, ROM DMA, the gzip/inflate and LZSS decompressors, caches |
| [files](files.md) | the ROM layout and its tables, how each file kind is found and unpacked, the texture table and the seven texture packings, model, building and scene formats |
| [levels](levels.md) | the level header and every section's record layout, level set-up order, the intro, level kinds, the train line and other paths, the carrier route record, hard-coded per-level checks |
| [rendering](rendering.md) | the frame's display lists and buffers, the RSP tasks and scheduler, the 30 fps swap rule, visibility, models, textures and texture animation, the backdrop, shadows, particles, text, the HUD and fades |
| [frontend](frontend.md) | the front-end segment, the menus and their mode flow, the globe, name entry, the save thread, the EEPROM and Controller Pak formats, the attract demos, text windows |
| [audio](audio.md) | the audio thread and its timing, the audio heap, music (tunes and the tune stack), the sound-effect player, positional and pitch-driven sounds, the sound bank and sequence files |

## Gameplay

| doc | what it covers |
| --- | --- |
| [vehicles](vehicles.md) | the vehicle list and types, per-vehicle data (speed bands, traction, mass), controls and input handling, getting in and out, the driver on foot, the special vehicles (Thunderfist, J-Bomb, Cyclone Suit, Backlash, Ballista, the crane, trucks, train, barges) |
| [collision](collision.md) | world and object collision, ground height, the triangle grids, obstacle tests, how vehicles hit buildings |
| [destruction](destruction.md) | buildings and objects and their damage, debris, chain reactions (TNT, explosive objects), rocking hits |
| [carrier](carrier.md) | the missile carrier's route and speed, how it decides it is blocked, and failure |
| [scoring](scoring.md) | level completion, RDUs, TNT, medals, timers, bonus levels (races, tests), scientists, communication points, best times and secrets |
| [camera](camera.md) | the game's own camera: its modes, how it follows each vehicle, snaps and cuts |

See also [versions](../versions.md) for what US 1.1 changed, and
[tools](../tools.md) for `bcfiles`, the Python library and command line that
read, write, extract, rebuild and export (glTF) the level and model files
from your own ROM; "confirmed by round trip" in files.md and levels.md
means checked with it on every file of the US ROMs.

## Glossary

- **RDU**: the light beacons scattered over a level. Driving over one
  lights it; lighting them all is part of a level's bonus
  (`LEVEL_KIND_RDUS` levels are about finding them).
- **TNT**: explosive crates; destroying one sets off a blast that damages
  what is near it.
- **The carrier**: the missile carrier ("cmo" in the asset names, vehicle
  type 0xFF) that drives a fixed route through a mission level. Anything in
  its path must be destroyed before it arrives, or it crashes and the
  level is failed.
- **BCT chopper**: the Blast Corps helicopter (vehicle type 0xFE) that
  flies the driver in at the start of a level (`MODE_INTRO_*`).
- **Mission / bonus level**: mission levels (`LEVEL_KIND_MISSION`, timed
  as `LEVEL_KIND_MISSION_TIMED` later in the game) have the carrier or the
  shuttle; every other kind (races, targets, damage, RDUs, buildings, the
  pill maze) is a bonus level (see levels.md).
- **Medal**: the gold, silver or bronze grade a level is given (see
  scoring.md for how it is worked out).
- **Scientists, communication points**: things placed in levels for the
  player to find; a communication point (`gCommPts`, model 0x96) is taken
  by driving into it, a scientist uses model 0x98. How each counts toward
  a level's results is in scoring.md.
- **Game mode** (`MODE_*`, `gGameMode`): one bit per screen or state of
  the game, in a 64-bit word; `gNextGameMode` requests a change.
- **Yoshi window** ("YW"): the game's own name for its text windows
  (`yoshi.c`), used for messages, menus and the text pages.
- **Digger**: the developers' name for the vehicle-select screen
  (`digger_loop.c`).
- **hd_code**: the main code segment, loaded once at boot; **hd_front_end**
  is the front-end overlay at 0x801E7000; **init** is the boot segment.
- **Static segment**: the small code/data segment (RSP segment 1) loaded at
  0x803FF600 at each mode set-up.
- **VI / retrace**: one vertical blank, 60 Hz on NTSC. The game logic runs
  once per frame, normally every second retrace (30 fps).
- **RSP / RDP**: the N64's two halves of the graphics/audio coprocessor:
  the RSP runs microcode (geometry, audio), the RDP rasterises.
- **Kiunzip**: Rare's gzip front end, used for almost every compressed
  file; some files use LZSS instead.
- **NON_MATCHING / NM**: C rewrites of hand-written asm, built by
  `make NON_MATCHING=1` and checked for functional equivalence; the native
  port is built from them.
