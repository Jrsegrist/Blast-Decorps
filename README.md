# Blast Decorps

[![CI](https://github.com/Jrsegrist/Blast-Decorps/actions/workflows/ci.yml/badge.svg)](https://github.com/Jrsegrist/Blast-Decorps/actions/workflows/ci.yml)

Blast Decorps: a matching decompilation of Blast Corps (Rare, 1997) for the Nintendo 64, and a native Windows port built from it.

- **The decompilation is complete for `(USA) (Rev 1)`.** The source rebuilds that ROM byte for byte, and `(USA)` (v1.0) from the same source. Every compiler-built function is matched C. Rare's hand-written assembly stays assembly in the matching build, with equivalent `NON_MATCHING` C for the port.
- **Every function is named**, along with nearly all the data and the shared structs' fields. Documentation of the game's systems (levels, physics, collision, scoring, saves) is in [`docs/`](docs).
- **The Windows port** ([`blastcorps/port`](blastcorps/port)) runs the decompiled game natively, with RT64 for graphics. It plays the original game frame for frame, and adds optional widescreen, high frame rates, an orbit camera, texture packs and a level editor. **Download a ready-to-run build from [Releases](https://github.com/Jrsegrist/Blast-Decorps/releases)**; you need your own `Blast Corps (USA) (Rev 1)` ROM.
- `Blastdozer (Japan)` and `Blast Corps (Europe) (En,De)` aren't supported yet.

The repository holds no game data: everything is split from your own ROM. The versions, by their no-intro names:

| file at the top of the repo | ROM | SHA-1 | builds |
| --- | --- | --- | --- |
| `baserom.us.v11.z64` | `Blast Corps (USA) (Rev 1)` | `483f7161aea39de8b45c9fbc70a2c3883c4dea8c` | yes (the reference) |
| `baserom.us.v10.z64` | `Blast Corps (USA)` | `185a6ef7ba1adb243278062c81a7d4e119bda58c` | yes |
| `baserom.jp.z64` | `Blastdozer (Japan)` | `b147fdbeb661c89107c440b00dc4810508f58636` | not yet |
| `baserom.eu.z64` | `Blast Corps (Europe) (En,De)` | `460212600f8b9f0da95219c4c7330f2e626d9a7e` | not yet |

# Getting started

## What you need

Linux or WSL2 (Ubuntu 24.04 is what it is tested on) with `make`, `git`, `curl`, Python 3.12 (with `venv`), the
MIPS binutils (`binutils-mips-linux-gnu`) and a host `gcc`. The Windows port has its own requirements
([`blastcorps/port/README.md`](blastcorps/port/README.md)).

## Clone and set up

```
git clone --recursive https://github.com/Jrsegrist/Blast-Decorps.git
cd Blast-Decorps
make setup            # the submodules, a Python venv in .env (requirements.txt) and IDO 5.3 in tools/ido5.3
. .env/bin/activate
```

`make setup` downloads the compiler, IDO 5.3 statically recompiled for Linux, from
[decompals/ido-static-recomp](https://github.com/decompals/ido-static-recomp)'s releases (v1.2, checked against its
SHA-256); `make setup-ido` does only that. A clone made without `--recursive` gets its submodules from `make setup`
too.

## Build

Put your ROM at the top of the repo as `baserom.us.v11.z64`, then:

```
make VERSION=us.v11 extract                  # check the ROM, split it, inflate the three code segments
make VERSION=us.v11                          # the ROM again from its pieces: build/blastcorps.us.v11.z64: OK
make -C blastcorps VERSION=us.v11 extract    # the code segments' assembly (splat)
make -C blastcorps VERSION=us.v11 -j8        # compile and check them: three OK lines
make VERSION=us.v11 CODE=1                   # the whole ROM with the compiled code: build/blastcorps.us.v11.z64: OK
```

The three OK lines are the code segments built from the source:

```
build/init.us.v11.bin: OK
build/hd_code.us.v11.bin: OK
build/hd_front_end.us.v11.bin: OK
```

`CODE=1` puts the compiled init and the compiled hd_code and front end, compressed again by
`blastcorps/tools_port/gzpack.py` (the ROM's own deflate, byte for byte), into the ROM in place of the split
files. The ROM split comes from the ROM itself (`blastcorps/tools_port/romsplit.py` writes splat's yaml at extract
time), and the expected SHA-1s are in `roms.sha1` and `blastcorps/segments.sha1`.

## Other versions (US 1.0)

US 1.0 builds from the same sources: the same steps with `VERSION=us.v10`
(and `baserom.us.v10.z64`) give `build/blastcorps.us.v10.z64: OK` and the
three OK lines of `build/us.v10/`. [docs/versions.md](docs/versions.md) has
the layout (per-version asm/assets/build directories, generated symbol
files, `#if VERSION_US_V10`) and what differs between US 1.0 and US 1.1.

## Decompiling a function

[`tools/mips_to_c`](tools/mips_to_c) (upstream: [matt-kempster/m2c](https://github.com/matt-kempster/m2c)) generates
a first-pass C guess from a disassembled function, using the project's real headers as context so types/signatures
for known libultra calls resolve correctly instead of showing up as `?`.

```
pip install graphviz
tools/m2c.sh blastcorps/asm/init/1A30.s
tools/m2c.sh -f func_80220E70 blastcorps/asm/init/1F40.s
```

The guess will not match byte-for-byte on its own — use `tools/asm-differ` to iterate against it until it does, then
move the function into `src.<version>/<segment>/<offset>.c` and flip the corresponding entry in
`<segment>.<version>.yaml` from `asm` to `c` (see `init.us.v11.yaml`'s `0x1660`/`0x3990` entries for examples,
including how to mark an irreducibly-asm function like a CP0 register accessor with `#pragma GLOBAL_ASM(...)`
instead of forcing it into C).

Note: many functions in `init` (and likely the low-level parts of `hd_code`) are standard N64/libultra boot and
hardware-init code shared across nearly every N64 game, not Blast Corps-specific logic — e.g. `0xA4600000`-range
addresses are PI (cartridge DMA) registers, and `0x1FC007FC` is the PIF RAM control register. It's worth checking
whether an existing N64 decomp (e.g. the [n64decomp](https://github.com/n64decomp) projects) already has a matching,
documented implementation before decompiling these from scratch.

## Decomp status

`python3 tools_port/decomp_status.py` (from `blastcorps/`, after a matching build) counts every
function of the three code segments by category. Permanent hand-written assembly is listed in
`tools_port/hand_asm.txt`; `-v` lists the functions that are not matched C.

| segment | functions | matched C | hand asm + NM C | hand asm, no C | near-miss | unmatched IDO C | IDO C matched | named |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| init | 43 | 31 | 0 | 12 | 0 | 0 | 100.0% | 43 (100%) |
| hd_code | 1400 | 692 | 681 | 27 | 0 | 0 | 100.0% | 1400 (100%) |
| hd_front_end | 155 | 149 | 6 | 0 | 0 | 0 | 100.0% | 155 (100%) |
| total | 1598 | 872 | 687 | 39 | 0 | 0 | 100.0% | 1598 (100%) |

(Earlier counts included 17 leftover SDK alias symbols, func_802D4020 = osInitialize and
the like, as matched hd_code functions: 1615 / 887.)  `--naming` adds the rename map's size,
the shared structs' named fields and the named constants (game modes, sounds, the vehicle
state offsets, vehicle types, surfaces, object kinds, level kinds, Yoshi windows, effect kinds).
After naming phase 4 (the data): 2075 data symbols named; 41 `D_XXXXXXXX` names are left in the
C and headers (4 statics, kept by rule, and 37 symbols with no evidence: unreferenced, only cleared,
or array elements that only comments mention); 226 of the shared structs' 258 fields are named (the
rest are padding or never read).

- **matched C**: compiled from C in the byte-matching build (game code and the ultralib-based SDK).
- **hand asm**: Rare's hand-written game code (hd_code 56040–8DDB0, the front end's 1B100
  wrappers), the libultra `.s` routines and the boot entry. They stay `GLOBAL_ASM` in the
  matching build; the port uses the `NON_MATCHING` C versions where they exist ("+ NM C"), and its
  platform layer replaces the SDK ones.
- **near-miss**: IDO-compiled functions still `GLOBAL_ASM` in the matching build, with
  `NON_MATCHING` C that the port uses. None are left: the last two (front end fe_pak_thread and
  fe_globe_draw_route) were matched in Oct 2026, so every IDO-compiled function is now matched C.
  The 39 "hand asm, no C" are the boot entry, the libultra `.s` routines (COP0/cache/TLB, exceptasm,
  bcopy/bzero, sqrtf) and hd_set_cop0_compare (an `sd $ra` mtc0 stub in Rare's asm range).

## Naming conventions

Symbols start with splat's address names, `func_XXXXXXXX` (code) and `D_XXXXXXXX` (data), and keep them
until there is evidence for a real one: what the code does, read together with its callers and the data it
touches. A guess is not a name: when unsure, keep the address name and add a one-line comment saying what is
known.

| what | style | examples |
|---|---|---|
| a name the game or the SDK itself gives (assert and debug strings, file names, libultra, the SDK demos' scheduler and audio manager) | exactly that name | `levelno`, `playerNumber`, `yoshiState`, `__scMain`, `osSendMesg` |
| functions | `module_verb_object`, lower snake case; the module is the subsystem or the original file (`hd_` hd.c, `cam_` camera, `veh_` vehicles, `lvl_` levels, `obj_` objects and buildings, `mode_` game modes) | `hd_main_thread`, `cam_build_projection` |
| global variables | `g` + CamelCase | `gGameMode`, `gNextGameMode` |
| tables of ROM data, `.rodata` constants the `NON_MATCHING` C reads through `extern` | `g` + CamelCase, named for the content / the value's role | `gLevelTunes`, `gMathTwoPi` |
| ROM offsets of files and blobs (`D_00xxxxxx`) | `rom` + CamelCase of the file | `romLagpRaw`, `romMusicBankCtl` |
| segment addresses (`D_01xxxxxx`, `D_02xxxxxx`) | `seg1` / `seg2` + CamelCase | `seg2Base`, `seg1RspInitDl` |
| file-scope and function-local `static` data | keep `D_XXXXXXXX`: the port's `coffpin.py` pins statics at the address their name carries | |
| types | CamelCase | `DynamicBuf`, `LevelInfo` |
| struct fields | lowerCamelCase; unidentified ones stay `unkXX` / `padXX` (XX = the offset) | `modelId`, `unk5C` |
| constants and macros | UPPER_SNAKE_CASE | `MODE_LEVEL`, `TOPLEVEL_DL_SIZE` |

Each named function gets a one- or two-line comment on what it does, ending with its address, e.g.
`/* The main game thread: game mode switches, then the frame loop (0x80244930). */`.

**The rename map.** `blastcorps/tools_port/names.txt` holds every rename as `old = new` (fields as
`field Type.old = new`, renamed by hand: the compiler reports a use that was missed). It is also the alias
table for old names in notes and reports: `python3 tools_port/rename.py --which func_XXXXXXXX`. A name that
is replaced later is edited on its address line and keeps a `renamed previous = new` line, and so does a
name that isn't an address name (an address name with a suffix, a type, an alias in undefined_syms);
rename.py rewrites and `--check`s those too.
`tools_port/rename.py` applies the map to the whole tree (C, headers, symbol_addrs and undefined_syms, the
check files, conventions.txt, hand_asm.txt, the port's tools and data files, READMEs), adds each new name to
its segment's `symbol_addrs` file at its address, and is safe to re-run. Tools that classify symbols by their
address-name prefix (`func_` = game function, `D_` = N64 data) ask `tools_port/names.py` for the old name
first, so a rename changes nothing they do. Names never change code: after a rename the matching ROMs,
the NON_MATCHING ROM and every check must come out the same.

**Shared structs and views.** The records several files read (PlayerInfo, LevelInfo, LevelObject,
VehShadow, AnimChannel, VehState) have one definition in `include/game/types.h` / `object.h`. A file
whose code reads the same memory with other offsets or types keeps a view type named for what it does
(e.g. 26570.c `PlayerHintView`, 01C40.c `LevelTimesView`), with the shared field names where they
overlap. Two lessons from naming phase 3: keep a view macro's form (`#define gX (*(T * N64P *) &gX)`
compiles differently in the NON_MATCHING build from a plain declaration of the same type, so the NM ROM
changes), and remove the C objects before a header-only check (the Makefile does not track header
dependencies: `rm build*/src.us.v11/*/*.c.o`).

**Applying the map on another branch** (before merging it with one that renamed):
```
cd blastcorps
python3 tools_port/rename.py                     # rewrite the branch's files
python3 tools_port/rename.py --check             # no old name left (also run by the CI)
make VERSION=us.v11 extract                      # the asm under the new names
make VERSION=us.v11 && make VERSION=us.v11 NON_MATCHING=1 nmrom
make -C port inputs                              # port/inputs: the new names
```
Then merge; a conflict is then a real one, not a rename. A file the branch adds is covered by the first step
(or `rename.py FILE...`).

# Continuous integration

GitHub Actions (`.github/workflows/ci.yml`) runs on every push and pull request to `main`, without the ROM:

- **Windows**: builds the native Windows port (`blastcorps/port`, MSVC x64) from a clean checkout and packages it;
  the player package (`BlastCorps-port-<hash>.zip`) and its symbols (`-pdb.zip`) are attached to the run as
  artifacts. They contain no game data: the game reads it from the player's own ROM.
- **Linux**: the Python tools' self-tests and `make -C blastcorps/port norom-check` (the declared types against
  the committed `port/inputs`, the pointer-width audit, record layouts and the alignment check).

The matching ROM build, the NON_MATCHING build, `runchecks`, `check-inputs`, `verify` and `verify-levels` need the
ROM, so they stay local: see `blastcorps/port/README.md`, "Continuous integration".

# Licence

Blast Decorps is under the MIT licence: see [LICENSE](LICENSE). That covers everything in this repository (the decompiled source, the Windows port and its tools, the build files and the documentation), apart from third-party components, which keep their own licences: the submodules (ultralib, splat, asm-processor, asm-differ, m2c, decomp-permuter, n64-decomp-workbench), the IDO recompilation `make setup` downloads, and what the port is built with (RT64: MIT, SDL2: zlib, the DirectX Shader Compiler). The player zip carries the licence as `LICENSE.txt` and the third-party texts in `THIRD_PARTY_LICENSES.txt`. Nothing from the fork this repository started as remains, which `blastcorps/tools_port/provenance_check.py` checks against that fork's last commit. Blast Corps itself and its data are not included here and are not covered by this licence.

Blast Corps is a trademark of its owners. This project isn't affiliated with or endorsed by Rare, Nintendo or Microsoft, and includes none of the game's data.

# Acknowledgements

This project started as a fork of an earlier Blast Corps decompilation set-up and owes a lot to the people who worked on the game before it. None of their code is in this repository, but their work is where it began:

* **retroplastic** ([retroplastic/blastcorps](https://github.com/retroplastic/blastcorps)), for the original decompilation set-up this repository was forked from.
* **mkst** ([mkst/blastcorps](https://github.com/mkst/blastcorps)), for the first splat set-up of the game and the research into the gzip variant Rare's files use.
* **queueRAM**, for [blast_corps_docs](https://github.com/queueRAM/blast_corps_docs) (the first notes on the ROM's files, levels, textures and vehicles), [sm64tools](https://github.com/queueRAM/sm64tools) and [BlastCorpsEditor](https://github.com/queueRAM/BlastCorpsEditor).
* The authors of the tools the work stands on: [splat](https://github.com/ethteck/splat), [asm-differ](https://github.com/simonlindholm/asm-differ), [asm-processor](https://github.com/simonlindholm/asm-processor), [m2c](https://github.com/matt-kempster/m2c), [decomp-permuter](https://github.com/simonlindholm/decomp-permuter), [ultralib](https://github.com/decompals/ultralib), [ido-static-recomp](https://github.com/decompals/ido-static-recomp), [RT64](https://github.com/rt64/rt64) and [SDL2](https://github.com/libsdl-org/SDL).
