# port/: the native Windows port

Blast Corps' NON_MATCHING C, compiled with MSVC for x64 Windows: `bc.exe`
(the game in a window, RT64 renderer), `bc_headless.exe` (the same game
logic, no output: the test vehicle) and the player package.  The N64's 8 MB
of RDRAM sits at 0x80000000 in the process and the game's data at its N64
addresses, in N64 layouts.  Nothing in `src.us.v11/` changes for this beyond
`NON_MATCHING`/`PORT_HOST` code; the matching build is untouched.

There is one build: MSVC (`CMakeLists.txt`), opened in Visual Studio or
driven from WSL.  Around it:

- **Build on Windows with Visual Studio**: what a player or developer needs;
  no WSL.  The facts the build takes from the decomp are committed in
  `port/inputs/`.
- **Verification from WSL** (`Makefile`, `msvc.mk`): `port/inputs`, the
  checks against the emulator and the original code, all run against the
  MSVC exes.
- **The clang check** (`port64.mk`): a clang x86_64 bc_headless of the same
  sources that nobody plays, for the checks MSVC can't do (pointer-width
  audit, record layouts, signed-overflow census) and as a second compiler
  whose per-frame traces must equal MSVC's.
- **The matching ROM build**: unchanged.

```
# Windows (x64 Native Tools Command Prompt, or Visual Studio: Open Folder > port/)
cmake --preset x64-release && cmake --build --preset x64-release   # bc.exe, bc_headless.exe
cmake --build --preset dist                                        # the player package

# WSL (needs the NON_MATCHING build: make VERSION=us.v11 NON_MATCHING=1 at the decomp root)
make -C port msvc                  # the MSVC exes, from WSL (MSVC_DIR on a Windows drive)
make -C port verify                # check-inputs, loadcheck, bc_headless vs the emulator
make -C port verify-levels         # every level vs the emulator
make -C port loadcheck dist compare strict-data typemap ucode
make -C port inputs check-inputs   # port/inputs: rewrite / check against the code
make -C port norom-check           # what needs no ROM (the CI's Linux job)
make -C port asan                  # bc_headless with AddressSanitizer
make -C port clang-check           # the clang check (ptrcheck64, headless64, traces vs MSVC)
make -C port overflow-census       # signed overflows at run time (clang -fsanitize)
```

## Build on Windows with Visual Studio

**Prerequisites**
- Visual Studio 2022 or later with the "Desktop development with C++"
  workload (MSVC x64 tools, a Windows SDK, "C++ CMake tools for Windows",
  which bring CMake and Ninja).  Tested with Visual Studio 2026 18.10
  (MSVC 14.51, CMake 4.3, Windows SDK 10.0.26100).
- Python 3 (python.org; only its standard library is used) on `PATH`, or
  `-DPython3_EXECUTABLE=...`: the build's generated sources come from the
  Python tools in `tools/` (pinning, tables, ultralib copies).
- Git for Windows: the repository and its ultralib submodule
  (`git submodule update --init lib/ultralib`), and the first configure
  fetches RT64.
- The ROM, Blast Corps (USA) (Rev 1), only to run the game (bc.exe asks for
  it with a file dialog, or takes it as its first argument).

Nothing else: the facts the build needs from the decomp's NON_MATCHING
build (data addresses, which symbols are functions, the N64 pointer slots,
the C declarations' types and the two link maps) are in `port/inputs/`,
which is part of the repository (made in WSL by `make -C port inputs`; see
"Verification from WSL").  It holds no ROM data.

**Building**
1. Visual Studio: File > Open > Folder..., the `blastcorps/port` folder.  It
   reads `CMakePresets.json`: choose "x64 Release" (the default, optimised,
   with a PDB), "x64 Debug" (no optimisation, for stepping through the game
   code), "x64 Release, bc_headless.exe only" (nothing downloaded) or
   "x64 Release + AddressSanitizer" (below).
2. The first configure downloads into `port/build/thirdparty` (shared by all
   presets): RT64 at the pinned commit with its submodules (git, ~500 MB) plus
   the port's patches (`cmake/rt64_pin.cmake`, `cmake/fetch_rt64.cmake`), the official
   DXC v1.9.2609 and SDL2 2.26.3 releases (SHA-256 checked).
3. Build > Build All: `out/build/<preset>/bc.exe` (with SDL2.dll,
   dxcompiler.dll, dxil.dll next to it) and `bc_headless.exe`.  The target
   `dist` makes the player package: `dist/BlastCorps-port-<hash>.zip` and
   `dist/BlastCorps-port-<hash>-pdb.zip` (bc.pdb, the map and the .syms
   that match the exe).
4. Run and debug: pick `bc.exe` as the startup item and press F5; arguments
   (the ROM, `--no-config` ...) go into its launch configuration
   (Debug > Debug and Launch Settings: `"args": [ "D:\\roms\\bc.z64" ]`).
   Breakpoints work in the game's C (`src.us.v11/hd_code/*.c`), the platform
   layer and RT64.  N64 memory sits at its N64 addresses, so a game variable
   is watched as `*(u64 *)0x80364A90` (the game mode).

From a command prompt ("x64 Native Tools Command Prompt for VS"):
`cmake --preset x64-release`, `cmake --build --preset x64-release`,
`cmake --build --preset dist`.  The Visual Studio generator works too
(`cmake -G "Visual Studio 18 2026" -A x64 ...`).  Keep the binary folder's
path short: RT64's object paths must stay under 250 characters.

**Heap checking: the x64-asan preset** (`BC_ASAN`; from WSL `make -C port
asan` -> `MSVC_DIR/Asan/bc_headless.exe`).  bc_headless with the port's host
code (load layer, platform, audio, crash reporter: everything that uses the
C heap) built `/fsanitize=address`: a heap overrun or underrun, a use after
free, a double or bad free stops with AddressSanitizer's report (the access,
the block, where it was allocated and freed, with source lines);
`--crash-test heapover` checks that it does.  It runs as bc_headless does
(same options, same per-frame trace), about five times slower.  The game C
is not instrumented: it has no heap, and ASan's redzones around globals
would cover the N64 data that follows them.  Two things make it work with
the fixed-address RDRAM:
- ASan's run time puts its shadow memory in the first large free range of
  the address space while its DLL initialises, before any code of the exe
  runs; that range starts at 0x7FFF0000 and covers 0x80000000.  So N64
  memory comes from `bc_rdram.dll` (`src/rdram_asan_dll.c`), a DLL with no
  code and one 8 MB section, linked at a fixed base of 0x7FFF0000 with 64 KB
  section alignment: its section starts at 0x80000000, the loader maps it
  with the exe's imports before any DLL initialises, and ASan's shadow goes
  elsewhere.  `rdram_map` makes that range writable and clears it instead of
  VirtualAlloc'ing it.
- ASan's heap lives above 4 GB.  Only host code keeps heap blocks (the game
  is never given one), so the ASan build lets `rdram_map`'s below-4-GB check
  pass for the heap; a heap address that did reach N64 memory would be cut
  to 4 bytes and fault at once.
The DLLs (`bc_rdram.dll`, `clang_rt.asan_dynamic-x86_64.dll` from the
compiler's folder) are copied next to the exe.  This replaces the retired
32-bit build's page-heap wrapper (`checked_heap.c`, mingw `--wrap`).

## How the port works

### Memory model

N64 memory is at 0x80000000-0x80800000 in the 64-bit process (`src/rdram.c`
reserves and commits it with `VirtualAlloc`), so every N64 address is a
valid host pointer and N64-layout data keeps 4-byte pointers: RAM stays
byte-identical to the N64's apart from byte order.  Everything the game can
be given a host address of must be below 4 GB: the exe (linked at a fixed
base 0x40000000, `/FIXED /DYNAMICBASE:NO /HIGHENTROPYVA:NO`, no relocations;
code reaches RDRAM RIP-relative, < 1.1 GB away), the heap and the fiber
stacks (bottom-up allocations without high-entropy VA; `rdram_map` and the
fiber trampoline check it).

The game and SDK headers and the game C mark what is an N64 pointer
(`port/include/port_n64ptr.h`; the marks expand to nothing in the IDO builds,
`include/n64ptr.h`; the SDK's are put into the port's copies of ultralib's
headers by `port/tools/ulhdr64.py`, from `port/data/ultralib_n64ptr.txt`):

| mark | meaning (MSVC and clang) | where |
|---|---|---|
| `T * N64P x` | `__ptr32 __uptr`: a 4-byte pointer, zero-extended when loaded | record fields, pinned globals, casts that read an N64 pointer (`*(u8 * N64P *) (p + 8)`), functions taking pointers to such pointers |
| `N64FN(T) x` | a u32 (clang can't use `__ptr32` function pointers): `N64FN_SET`, `N64FN_GET(T, x)` | libaudio's handlers in N64 memory |
| `N64_IPTR(x)` | `(unsigned) x`: zero-extend | a signed 32-bit address cast to a pointer |
| `N64_A32` | `unsigned` | an int holding an address added to a pointer |
| `N64_DPTR(x)` | 0, the value comes from the pointer table | pointers in static initialisers of N64 data |
| `N64_KEEP` | `used` | a pinned static clang would fold away |

ultralib's ul_* objects see ultralib's own headers: `tools/ulhdr64.py`
copies them with the marks listed in `data/ultralib_n64ptr.txt`, and
`tools/ulsrc64.py` rewrites the libaudio sources' uses of N64FN fields, the
bank loader's s32 address offsets (bnkf.c) and `sizeof(ALFilter *)` (the bus
source tables live in the audio heap, which the game sizes exactly: 0 bytes
left over).  `tools/lesrc.py` makes little-endian copies of five SDK
sources (`le/`): two libc files read a double's sign/exponent half-word at
index 0, gu's sinf/cosf initialise their double constants as big-endian word
pairs, and the Controller Pak's inode/ID byte pairs and a directory entry's
never-written bytes (see the tool).

### Pinning data, tables, the link

- **Pinning (`tools/coffpin.py`).** Every data object the C defines that the
  NON_MATCHING ELF places at an N64 address is taken out of its COFF object
  (built with one section per variable, `/Gw`): an initialised one keeps its
  bytes as `__native_<name>` (the copy table), a bss one becomes undefined,
  and every relocation to it goes to an undefined `<name>`.  Function-local
  statics (`D_xxxxxxxx`) are pinned by name.
- **Absolute symbols (`gensyms.py linkcoff`, `tools/rdramobj.py`).** The N64
  data symbols the objects use become absolute COFF symbols in one object;
  functions nothing defines become traps (`stubs.c`).  link.exe resolves a
  REL32 fixup against an absolute symbol as image base + value and an ADDR64
  one as the value, and refuses ADDR32NB ones (MSVC indexes global arrays
  off `__ImageBase`): the symbols are written as address - image base, and
  coffpin adds the base to ADDR64/ADDR32 addends and resolves ADDR32NB
  itself.  It also handles `/Gw`'s per-variable COMDAT sections, MSVC's names
  for local statics and its COMMON symbols (C tentative definitions).
  (RDRAM as a section of the image at 0x80000000 would avoid absolute
  symbols, but the image would then cover 0x7FFE0000, where Windows maps
  KUSER_SHARED_DATA: such an exe doesn't start.)
- **Copy table (`gensyms.py copytab64`).** Start-up copies the native
  initialisers over the loaded image, already in host byte order and layout;
  each copy is checked against the N64 `st_size`.
- **Pointer table (`gensyms.py ptrtab-in`, `inputs/nmptrs.txt`).** Every
  pointer slot of the pinned C objects (the NON_MATCHING objects' R_MIPS_32
  data relocations) with the word the NM ELF holds there, written after the
  copy: the `N64_DPTR` initialisers (strings, tables of N64 addresses) get
  the N64's own values.
- **Image load (`rdram_load`).** The hd_code `.data`/`.rodata` gzip member
  (ROM 0x7D73B4) is inflated to 0x802E8BD0, and the front end (text+ucode,
  data) to 0x801E7000/0x80208040.  Both `.bss` ranges are cleared, the load
  layer swaps the image bytes by type (below), then the native initialisers
  and the pointer table go in.  The hd_code text is not loaded; its tables
  are C (NM_PIN_HD_CODE).

### MSVC specifics

- Game code: `/O2 /Ob1` (only `inline` functions are inlined; `/Ob0` in the
  files that read the clock), `/GH` (an empty `_pexit` call before each return
  keeps tail calls as calls: return addresses name their callers, which
  `--clock`/`--sync` key on and crash reports use).  `--sync`'s
  function-entry points and `--calls` patch only the functions they name, at
  start-up, into their entry (`plat_host.c host_entry_hook`, `entry_x64.asm`,
  the linker's `/FUNCTIONPADMIN`): the hook runs before any instruction of
  the function.  (MSVC's own `/Gh` hook comes after the prologue, and the
  compiler schedules body code, N64 memory accesses among it, before it.)
- MSVC has no `-fwrapv`: the signed adds and multiplies that really overflow
  are wrapped explicitly (`PORT_WRAP_MUL`/`PORT_WRAP_ADD`, `game/port.h`:
  the two random-number generators in 23C20.c and the debris size in
  77E20.c), found by the clang check's overflow census.  No
  `-fno-strict-aliasing` equivalent is needed (MSVC does no type-based alias
  analysis), nor `-malign-double` (8-byte members are naturally aligned).
  The GBI's bit-fields are written for Microsoft's layout rules where they
  differ (`gbi.h`, `rdb.h`; `layout_check.c` asserts every GBI and ABI
  record's size and offsets and checks the bit positions at start-up).
  `sinf`/`cosf` stay the SDK's (`#pragma function`).  MSVC doesn't assume
  16-byte alignment of N64 data (`tools/align64.py` over its objects finds
  only stack saves), so the clang build's IR alignment pass has no MSVC
  counterpart.
- Warnings: the port's own code (the host libraries, src/live, the test exes,
  bc_packtool) builds at `/W4` with no warning (`BC_WARN`; the conversion
  warnings 4244/4267 and the N64-address casts 4311/4312 off), RT64's, SDL's
  and the other third-party headers as SYSTEM includes (`/external:W0`); the
  game C and the platform files that see `<ultra64.h>` stay at `/W1` (the
  decomp's code).  The clang check compiles the host C with `-Wall -Wextra`.
- Crash reports name functions and source lines from the exe's PDB
  (dbghelp, with a time limit); the minidump opens in Visual Studio with the
  PDB.  The static C runtime (`/MT`): the package needs no Visual C++
  redistributable.
- `-DPORT_HOST` enables the game's port hooks (`include/game/port.h`):
  `PORT_SPIN()` in the two busy-waits on the VI counter (00000.c),
  `PORT_FE_LOADED()` after the front end is inflated (46C20.c) and the rest
  below.  Both expand to nothing in the N64 builds.  hd_code files that are
  only SDK asm or RCP-register C (8FD90 ... A0A30) are left out; the ultralib
  os/io objects are replaced by the platform layer.

### Byte order on load (`src/load/`)

Memory is host-endian (little-endian): everything the game's C reads
natively is swapped once, by the width the code reads it at, when it arrives
from the ROM. Textures, palettes, pictures, compressed streams and the byte
streams the code reads bytewise (5CB60.c's BE16U/BE16S/BE32) stay big-endian.

- **Data images** (hd_code .data/.rodata at 0x802E8BD0, front end at
  0x80208040): `rdram_load` calls `port_load_image_hd/fe` (swap.c) with the
  table `swaptab.c` that `tools/swaptab.py` generates from
  1. `tools/typemap.py`: the DWARF of every game file's declarations (host
     gcc in WSL; `inputs/typemap.txt`) and its per-file
     `#define D_x ((T *) D_x)` struct views (merged: the most detailed view
     first; arrays run past splat's mid-array symbols);
  2. `data/image_widths.txt`: the widths the NON_MATCHING code reads each
     address at (traced, below); the trace wins where it reads one wider field
     over narrower declared ones;
  3. `data/image_static.txt`: fixed-address accesses of the native code, for
     bytes 1-2 leave untyped (made once by `tools/mixscan.py --widths` over
     the retired i686 build's gcc assembly);
  4. `data/image_overrides.txt`: hand decisions (gObjFadeCmdSubs DL commands as
     words, the gUseEeprom flag/word alias kept big-endian, struct copies).
  Bytes from C objects need nothing: their native initialisers are copied
  over the image. `make -C port typemap` prints the coverage.
- **Assets**: `port_on_dma` (every PI copy, from the platform) and
  `port_on_load` (after the game decompressed a block: hooks in 46C20.c
  hd_rom_load and 5CB60.c's packed loader) classify the ROM range
  (port_assets.c) and apply the schema (schemas.h): the texture table, sound
  banks (walked like alBnkfNew), the sequence file and tunes, levels (header
  offsets, triangles, collision cells, lifts, group chain, display lists,
  segment-8 vertices), packed objects (buildings), vehicle models
  (parts, animations, vertices, display lists), the demo recordings, the
  front-end scenes and the static segment. Texture entries are swapped
  around 60F60.c's decoder (`port_texture_input/output`, NON_MATCHING hooks):
  s16 tokens in, big-endian texels out.
- **Graphics data the renderer reads** (`src/load/gfx_fix.c`): the
  schemas swap an asset's display-list area as u32 words (the CPU walks and
  patches commands as words), but such an area can also hold vertices,
  viewports, lights and texels, which then end up in the emulator's layout
  (two halfword pairs and a byte-reversed colour word per Vtx).  Likewise the
  parts of the data images no declaration or traced read typed stay ROM
  bytes, and some are display lists, vertices and matrices only the RSP reads
  (e.g. the front-end list at 0x80208FA8 that level 0's lists call).  The
  schemas and `port_load_image_*` register both kinds of area; before bc.exe
  draws a task, `port_gfx_fix_task` follows its display lists with the real
  segment table and converts what they use there to the native layout, each
  word once per load and only while it still holds what was loaded (data the
  game wrote since is left alone).  bc_headless doesn't call it.  Vertices
  in the data images that other typing rules made words get the `vtx`
  override (`data/image_overrides.txt`; the CPU never reads them).
- **Exceptions fixed in NON_MATCHING C** (`PORT_HOST`): 00000.c
  `port_802E8BF8_word` (the u8 EEPROM flag is word 0's MSB of a word table),
  196F0.c (the front-end background pictures are tinted on big-endian pixels).

Tools:
- `tools/m64widths.py ROM OUT VIS` (MODE=nm for the NON_MATCHING test ROM,
  the default; MODE=base for the original): mupen64plus access-width tracer.
  Memory breakpoints (physical ranges; the core reports the aligned word, the
  byte address is recomputed from the instruction) over the data images and
  every loaded asset; per (region, offset, width, R/W, function). Run from the
  project root; about 25 minutes for the 30000 VIs of the nine attract demos.
  `EXTRA=name:lo:hi,...` adds regions (.bss state blocks).
- `tools/widths.py image|facts|assets`: reduce traces.
- `make -C port loadcheck [LC_TRACE=trace.txt]`: real assets through the
  game's own loaders and consumers (`bc_loadcheck.exe`,
  `src/loadcheck_main.c`), native vs the original code in unicorn
  (`tools/loadref.py`): T5 the gUseEeprom alias, T6 520 texture decodes, T7
  every level's object stream, T8 every level's height zones, TA every
  level's carrier panel events (section 0x58) through dig_coach_update as
  the carrier's z sweeps the s16 range; with a trace, T9 checks every traced
  read of every traced asset right after loading.

### bc_headless and the platform layer

`bc_headless.exe ROM [options]` runs the whole game logic (every hd_code and
front-end game file, plus the ultralib gu, libm, printf and libaudio objects
compiled natively) on the platform layer in `src/platform/`, with no
graphics or audio output. Options are listed in
`src/platform/headless_main.c` (`--frames N`, `--dump F1,F2`, `--dump-every N`,
`--trace FILE`, `--input FILE`, `--gettime FILE`, `--frame-done FILE`,
`--eeprom FILE`, `--mpk FILE`, `--print`, `--cmdline STR` (at most 63 bytes),
`--cheats-on F`, `--cheats-save normal|separate`, `--gfx-cycles N`,
`--boot-count N`, `--syms FILE`, `-v`...). A frame is one frame-ending gfx
task (the scheduler task with flag 0x40); dumps are the 8 MB of RDRAM in host
byte order, taken when the main thread sends that task to the scheduler.
The build writes `bc_headless.syms` (`tools/mapsyms.py`: link.exe's map as
`nm -n` output) for `--syms`.

Two runs of the same exe with the same options give the same traces and the
same RAM dumps, byte for byte (checked: levels 0, 1, 3, 13, 29, 45 and the
attract demos, 4-5 dumps each).  Host memory never reaches N64 memory: game
locals live on host fiber stacks, whose addresses Windows picks anew every
run, so the two places where the game copied host stack bytes into RDRAM are
closed (PORT_HOST): hd_huft_build (53220.c) clears its local `Huft r`, whose
padding was copied into every Huffman table entry (the inflate's work area
at 0x8004C900..), and cull_rsp_test (5FD50.c) puts the address the N64 gives
cull_start's display list (0x80312B08, gMainThreadStack + 0x1D88, in every
emulator dump) into the task's data pointer (0x803BE780), handing the host
address to the platform separately (`port_cull_data`, os_hw.c).

Platform model (`src/platform/`):

| piece | file | behaviour |
|---|---|---|
| threads, messages, events | os_thread.c | libultra's priority scheduler run cooperatively on host fibers: switches only where libultra would (block, ready a higher priority, interrupt); FIFO among equal priorities. The idle thread is parked when it drops to priority 0. |
| virtual clock | os_thread.c, os_time.c | 46.875 MHz counts; game code takes no time; the clock jumps to the next event (retrace every 781250 counts, timers, task completions) when every thread waits, or in `port_spin`. osGetTime/osGetCount read it; injection by file (`--gettime`), C hooks, `--boot-count`. Timers keep libultra's list as this game's SDK (older than 2.0K) does, quirk included: osSetTimer inserts without charging the time since the Compare interrupt was last set to the first timer, so overlapping timers run late or early as on the N64 (the EEPROM's 12 ms block timers against the scheduler's 6 ms audio timer: one EEPROM block per retrace, as in mupen64plus). |
| frame pacing | os_hw.c | a frame's gfx task completes `--gfx-cycles` after it starts (default one retrace), or at the retrace a `--frame-done` file gives (e.g. from an emulator trace). |
| VI | os_hw.c | the VI manager's retrace work: swap the framebuffer, send the osViSetEvent message every retraceCount retraces. |
| PI | os_hw.c | osPiStartDma = memcpy from the ROM + `port_on_dma`, completion message at once; osPiRawReadIo reads ROM words (the debug command line at 0xFFB000 is `--cmdline`, default empty). A ROM from the shiftable build: both read through its ROM map (rdram.c; "Moved files" below). |
| SI | os_si.c, input.c | controller 1 from `--input` (or idle), osContInit's 0.5 s wait; EEPROM 4 Kbit in a file (`--eeprom`, mupen64plus's 512-byte .eep format). |
| Controller Pak | pif.c | `--mpk FILE` (mupen64plus .mpk: four 32 KB paks, controller 1's first; created formatted if missing): the front end's own SDK Pfs objects run natively over a PIF emulation (status, pak read/write); the system blocks (ID, inode, directory) are converted by layout at the pak boundary. Each PIF RAM DMA's interrupt comes 0x900 counts after it starts (mupen64plus's SI DMA duration), so a pak transfer takes virtual time and the main thread runs while the pak thread waits, as in the emulator. |
| saves | save.c | the save thread's routines (0E7B0.c, `PORT_SAVE_*` hooks) work on a big-endian copy of each record, so CRCs and files are the N64's: EEPROM files are byte-identical to mupen64plus's for the same play (checked: new game, level completion with best time), and load either way. Only what the routine changed goes back into the record (its CRC as soon as it is added, `PORT_SAVE_SYNC`; the data read, at the end), so the game's writes to the record during a transfer (an EEPROM write takes ~15 frames) stay, as on the N64. The EEPROM write sends each 8-byte block as the record is when that block goes out (`port_save_refresh` before each block of `osEepromLongWrite`, one block per retrace through the SDK's timer list): a game write during the transfer reaches the blocks sent after it, as on the N64 (checked against mupen64plus with bc_headless following it: a byte poked into each of the 32 blocks 7 frames into the write; both EEPROM files have the first 8 blocks old and the rest new, byte-identical). The Controller Pak's writes go through the Pfs code on the record's copy, a 32-byte block per PIF transfer. |
| RSP/RDP | os_hw.c | gfx tasks dropped and counted (SP done, plus DP done for frame tasks; bc.exe draws them with RT64, same completion times); cull_rsp_test's cull test answered by a CPU model of its ucode (below); audio tasks run at once on the microcode interpreter (src/audio/, "Audio" below), SP done. |
| AI | os_hw.c | a two-buffer DMA FIFO playing at the programmed rate in virtual time, so osAiGetLength (which sizes each audio frame) behaves; each accepted buffer goes to the output (`--wav FILE`; bc.exe's speakers), which never feeds back into the timing. |
| front end | plat_core.c | data+bss snapshot at boot, restored after every reload. |
| debug output | headless_main.c | `--print`: the game's debug printf (debug_printf, empty on the N64) to stderr with the frame number; `--cmdline "-c"` turns on the game's debug cheats (C-right completes the level, C-up + C-right fails it, Z + C-right is a perfect finish). |
| debug cheats | save.c | `host_cheats_set` writes gDebugCheats (0x802FA268) between game steps (bc.exe's setting and key; `--cheats-on F` at frame F); the game reads it every time, parse_cmdline sets it only at boot. `--cheats-save separate` (bc.exe's default, `[game] cheats_save`): once the cheats were on, the save files are `<name>.cheats.<ext>` for the rest of the run (below). |

Host-order rules the game C follows under `PORT_HOST` (include/game/port.h):
`PORT_HALF(i)` for u16 views of 32-bit words (the N64 Mtx's elements; the
port keeps Mtx words in host order), `PORT_SHAMT(n)` for variable shift
amounts that can reach 32 (MIPS uses the low 5 bits), `PORT_SAVE_*` for save
records, `PORT_WRAP_*` for signed overflow. Tools: `tools/eepsave.py FILE
[OUT --set OFF:W=VAL]` decodes or edits an .eep (fixing its CRC).

### bc.exe: the game in a window

`bc.exe ROM [bc_headless options] [--api d3d12|vulkan]
[--shot F1,F2..] [--shot-dir DIR] [--shot-every N] [--no-pace] [--scale N]
[--mute] [--volume N]`
plays the game in a window: bc_headless's game and platform objects plus
`src/live/` (SDL2 window and input, the RT64 renderer). The exe needs
`dxcompiler.dll`, `dxil.dll` and `SDL2.dll` next to it (the build copies
them). Options are listed at the top of live_rt64.cpp (parsed in
live_start.cpp); `--frames N` and the other bc_headless options work as there.
bc.exe's own files: `live_rt64.cpp` (the window, RT64, input, sound,
interpolation cuts, events, the platform's HostLive hooks, the settings
menu's changes, start-up), `live_frametimes.cpp` (the pacing, flip, perf and
spike logs, the F2 counter, the frame-time report), `live_shots.cpp`
(screenshots), `live_start.cpp` (the command line, bc.ini, bc.log, the ROM,
main), their shared state in `live_rt64.h`; `live_setup.cpp` (bc.ini, the
ROM check), `live_menu.cpp` (the settings menu and the overlay),
`live_input.cpp`, `live_orbit.cpp` / `orbit_math.cpp`, `live_interp.cpp`,
`live_wide.cpp`, `live_throttle.cpp`, `live_texdump.cpp` / `tmem_decode.cpp`,
and the viewer and editor (below).

Keys: arrows = stick, X = A, C = B, Z or Space = Z, Enter = START, A/S =
L/R, I J K L = C buttons, T F G H = D-pad, M = sound on/off, - and = =
volume, F1 = settings menu, F2 = frame-rate counter, F3 = the debug cheats,
Alt+Enter / F11 = fullscreen, Esc quits; an SDL game controller
(XInput pads etc.) maps the same way (left stick, A, B/X, triggers = Z,
Start, shoulders, right stick = C buttons, D-pad). `--input FILE` replays a
recording instead.

**RT64** (github.com/rt64/rt64, MIT) at the pinned commit 43373749, with
`rt64/patches/0003` (`RT64_NATIVE_HOST_LAYOUT`), `0004` (the L3D
handler), `0005` (vsync pacing, under "Pacing" below), `0006` (the view
hook), `0007` (present scheduling and transform pivots) and `0008` (each
present's picture content), is fetched by `cmake/fetch_rt64.cmake` and built
with MSVC as a static subproject.  The commit and the patches, in the order they apply, are
listed once, in `cmake/rt64_pin.cmake`: a new patch is a file in
`rt64/patches` plus a line there.  To change RT64, edit the fetched tree
(`port/build/thirdparty/rt64`, each patch a local commit there), rebuild,
and regenerate the patch with `git diff`.

**RT64_NATIVE_HOST_LAYOUT** (`src/common/rt64_rdram_layout.h` in the
patched tree): RT64 normally reads RDRAM in the emulator's layout (each
32-bit word a host u32 of the big-endian word: the byte at A is at A^3).
The port's RDRAM is the game's own memory: structures in host order with
their fields where the C declares them, byte streams (textures, palettes,
colour and depth images, ucode) as big-endian bytes. The switch reorders
RT64's Vtx/Light structs, the viewport indices and the G_MW_LIGHTCOL word,
makes the TMEM loads read bytes without the XOR, drops the word swaps when
images go between RDRAM and the GPU (the shaders already produce and take
big-endian pixels), and hashes the ucode from a word-swapped copy (the
database holds emulator-layout hashes). Gfx commands and Mtx are host u32
words in both layouts.

**L3D.** The front end's ucode (gL3dText, slot 0) is Fast3D's line build;
RT64 knew it by hash ("L3D Blast Corps") but had no handler. Patch 0004
adds `GBIUCode::L3D`: F3D plus G_LINE3D (0xB5, `gSPLineW3D`), drawn by
`RSP::drawLine` as a screen-space quad 1.5 + wd/2 pixels wide (the vertices
are moved perpendicular to the line in clip space and mapped back through
the inverse MVP). The globe's route lines use it.

How the platform drives it (HostLive in `plat_host.h`; NULL in
bc_headless):
- boot: the ucode text and data RT64 hashes (gF3dText, gF3dData,
  gL3dData) are put back at their addresses as ROM bytes (hd_code's text
  isn't loaded natively; nothing native reads them).
- `osSpTaskStartGo`: a gfx task goes to RT64 (`loadUCodeGBI` +
  `processDisplayLists`, after `port_gfx_fix_task`). cull_rsp_test's
  visibility test (ucode gCullUcodeText, whose RDP output the CPU reads) stays
  with the platform: os_hw.c `cull_visible` evaluates its list (projection
  and modelview, 8 box corners, 12 triangles): visible iff a triangle is not
  entirely outside one of x = +-w, y = +-w, z = -w in clip space and
  clipping it to those planes leaves a polygon. This model matched the real
  ucode, run on an LLE RSP (mupen64plus + cxd4 + angrylion-rdp-plus), in all
  3921 tests of the attract demos; `BC_CULL_LOG=1` logs every test. (With
  `--clock` the emulator's word is used instead, and the model is checked
  against it: the stats line `cull: N tests, H not visible; ... the model
  disagrees with D`. Comparison runs take the word from an LLE RSP,
  `compare.py emu --lle`; mupen64plus's HLE RSP never runs this ucode, so
  its word would always say "visible".)
  RT64 raises the SP/DP interrupts while it processes the task;
  they are counted, and the game hears about completion from the same
  virtual-time model as bc_headless (`--gfx-cycles`, `--frame-done`).
- every retrace: the VI registers the VI manager would program for the
  frame on screen (os_hw.c, from the game's OSViMode, osViBlack and
  osViSetSpecialFeatures, which edits the control word cumulatively like
  the ROM's: the game turns VI gamma off, so RT64 applies none; bc.exe logs
  every VI status change), `updateScreen`, SDL events, and pacing: virtual
  retrace N is shown at start + N/60 s (`--no-pace` runs flat out; more than
  a quarter second behind restarts the reference).
- input: controller 1 from the keyboard/pad when no `--input` file is given.

Debugging what is drawn: `--dl-dump F[:N]` / `--dl-dump-every N` print
display lists (with each G_VTX's first vertex in both layouts);
`--dl-skip LO:HI` turns the triangles whose commands lie in that physical
range into no-ops (bisect which list draws a shape); `--gfx-fix-log` with
`BC_GFX_WATCH=addr` reports what gfx_fix.c does to one word. Data-image
vertices typed as words by some declaration need a `vtx` line in
`data/image_overrides.txt` (four so far, all K0-addressed G_VTX targets in
the hd data image now match the ROM layout).

Same game logic: with the same input, bc.exe and bc_headless produce
identical per-frame traces (retrace, time, mode, level, frames in mode, game
retrace counter). Their RDRAM differs only in what the renderer writes
(colour and depth images: framebuffers, Z, the shadow and other
render-to-texture images), the staged ucode, the graphics data gfx_fix.c
converted, and pointers to the exes' own code/data.

### Player setup and package

bc.exe is a Windows GUI program (`/SUBSYSTEM:WINDOWS`): double-clicking it
opens no console. Its log goes to stderr when the parent gave it one (WSL, a
redirect, a script: everything below works unchanged), otherwise to
`bc.log` next to the exe (or `%TEMP%`). With `host_gui` set, `host_fatal`,
the crash reporter, C++ `terminate` and bad options also show a message box.

**`--no-msgbox`** (both exes; every script that runs one passes it:
compare.py's `native`, so `verify`, `verify-levels`, `level`, `run`): no
dialog of any kind, for automated runs. Errors and crash reports go only to
the log (stderr) and the crash files; `host_message` just logs; bc.exe shows
no ROM file dialog (no ROM -> exit 2); Windows' fault, critical-error and
WER dialogs are off (`SetErrorMode`, `WerSetFlags`); the C runtime's assert
and abort messages go to stderr; `MessageBox*` calls from the exe's own
imports (RT64) and SDL2.dll only log their text; SDL assertions become crash
reports. It is read by `host_crash_init` before anything else (so even a bad
option or a missing ROM stays quiet) and accepted by the shared option
parser (`host_main`, headless_main.c). `BC_NO_MSGBOX=1` in the environment
does the same, but WSL doesn't pass the environment to Windows exes.
Exit codes: 0 done, 1 usage (bc_headless), 2 fatal error (`host_fatal`;
bad option, missing ROM ...), 4 crash.

**Crash reports** (`src/platform/crash.c`, both exes). `host_crash_init`,
first in `main`, installs an unhandled-exception filter (re-armed after RT64
starts), `std::terminate` (bc.exe), `SIGABRT` and SDL assertion handlers,
and starts a reporter thread that waits. A crash on any thread or fiber hands
the exception to that thread (its own stack: stack overflows work; no C
runtime locks or heap), which writes:
- to stderr (bc.exe: its log) and `bc-crash-YYYYMMDD-HHMMSS.txt`: the
  exception (code, fault address as module + RVA, read/write address),
  registers, the host thread (main = the game's fibers; the fiber), the game
  state (game thread, frame, retrace, mode, next mode, level, frames in mode,
  game VI counter), and a backtrace: return addresses found on the stack,
  call-site checked and unwound with the x64 unwind tables, named with
  source lines from the exe's PDB (dbghelp, with a time limit) or the
  nearest DLL export; a frame whose direct call targets the function below
  it is confirmed, others are marked `?` (indirect calls, tail calls or
  stale);
- `bc-crash-YYYYMMDD-HHMMSS.dmp` (`MiniDumpWriteDump` from System32's
  dbghelp.dll, loaded at start-up): threads, indirectly referenced memory, the
  8 MB RDRAM and the exe's .data/.bss (~17 MB); with `--dump-dir`, also
  `frame_9999999.bin` as before. The files are written by a second waiting
  thread with a 20-second limit: MiniDumpWriteDump (and creating a file)
  allocates from the process heap, whose lock a crash inside the heap leaves
  held for good (it happened: see "Rare boot crash" below); the report on
  stderr is complete by then, and an unfinished dump deletes itself. It also
  suspends every other thread of the dump, so the reporter is left out of
  the dump (`IncludeThreadCallback`) and keeps its time limit; the dumper
  first runs `HeapValidate` and writes no dump when that finds the heap
  corrupt; the PDB lookup first checks that the heap's lock is free.
They go to `--crash-dir DIR` (both exes; compare.py passes the native run's
folder), else bc.exe: bc.log's folder (the exe's), bc_headless: the current
folder, falling back to `%TEMP%`. Then bc.exe shows a message box (unless
`--no-msgbox`) and the process ends with exit code 4 through
`TerminateProcess` (no DLL detach or atexit code runs in a crashed process;
`host_fatal` ends the same way, code 2). `--crash-test KIND[:FRAME]` crashes
on purpose (av, div, stack, thread, abort, fatal, box, heaplock, heapbad,
heapover; bc.exe also cxx).
Example (bc_headless --crash-test av:5):

    CRASH: access violation (exception 0xC0000005) at 0x00004000A350 = bc_headless.exe+0xA350
      read of address 0x000000000010
      rax=0000000000000010 rbx=0000000040939410 rcx=0000000040105E10 rdx=0000000000002002
      ...
      rip=000000004000A350 eflags=00010246
      host thread 24892 (main: the game's fibers), fiber 0000000002AC4A00; Blast Corps port dev, exe bc_headless
      game: thread 3@80310BD0, frame 5, retrace 49, mode 0x0000000000000010 (next 0x0000000000000000), level 0, frames in mode 4, game VI counter 49
    backtrace (return addresses found on the stack; ? = not confirmed by the call chain, may be stale):
      #0   00004000A350  bc_headless.exe+00A350  host_crash_test_frame+0x100 (crash.c:1120)
      #1   0000400AECB8  bc_headless.exe+0AECB8  plat_on_frame+0x128 (plat_core.c:112)
      #2   0000400A9376  bc_headless.exe+0A9376  osSendMesg+0x46 (os_thread.c:295)
      #3   00004004B40F  bc_headless.exe+04B40F  rsp_send_task+0x17F (405F0.c:97)
      #4   00004008ECE9  bc_headless.exe+08ECE9  fe_boot_screen_frame+0x79 (07800.c:380)
      #5   000040024270  bc_headless.exe+024270  hd_main_thread+0x1930 (00000.c:977)
      #6 ? 0000400A8DEE  bc_headless.exe+0A8DEE  fiber_main+0xE (os_thread.c:138)
      ...
    minidump: ...\bc-crash-20261007-212029.dmp

**Rare boot crash** (heap corruption seen 3 times in ~250 runs before
784dbcf: `free()` of the sound bank map after the swap, once in `sscanf`, at
retrace 30-107). Cause: `swap_bank` (load/port_assets.c) walks the first
copy of each sound bank (at 0x8004B400, where it is decompressed) and meets
offsets 0xFFFFFFFB, 0xFFFFFFFC and 0xFFFFFFFF; the old `bank_once` check
`off + size > len` wrapped and passed, so `done[off] = 1` wrote 1-5 bytes
*before* the map, into the heap block header, whenever the byte there read 0
(an encoded header byte: rarely, and differently every run). Fixed by
784dbcf (bounds without wrap-around), found and confirmed with the (since
retired) 32-bit build's page-heap wrapper: the old walk stopped at every
boot ("block of 19209 bytes was written before its start (-4)"), the
current code ran 2000 boots, 60 bc.exe level runs and all 60 levels under
it without a fault.  The x64-asan preset is the tool for this now.

`src/live/live_setup.cpp`:
- **ROM.** A ROM argument is used as given; otherwise `rom =` from bc.ini;
  otherwise (or if that one fails the check) a Windows open-file dialog
  (GetOpenFileNameW), and the choice is written back to bc.ini.  Every ROM is
  read as .z64, .v64 (byte pairs swapped) or .n64 (little-endian words;
  `rom_normalise` in rdram.c, which `rdram_load` also uses) and must have the
  SHA-1 of Blast Corps (USA) (Rev 1); US v1.0, Japan, Europe, other games and
  modified dumps get their own message.  A ROM from the shiftable build with
  files moved (its ROM map, rdram.c `rom_shift_check`) is accepted when its
  game data is the supported ROM's (below, "Moved files").  `--no-rom-check`
  skips the check (the NON_MATCHING test ROM, ROMs edited in place).
- **bc.ini** next to the exe (`--config FILE` for another), created on the
  first start from the commented defaults (the text in live_setup.cpp is
  the defaults): `[game]` rom, saves, controller_pak, debug_cheats,
  cheats_save, settings_version; `[video]` api, scale, fullscreen, vsync,
  present_pacing and the enhancements below (resolution, msaa, filtering,
  aspect, hud_position, framerate, interpolation, fps_counter,
  fps_counter_corner, texture_pack, texture_pack_enabled,
  texture_pack_reload, texture_dump, texture_dump_enabled); `[audio]`
  volume, mute; `[camera]` the orbit camera's (below); `[mods]` folder,
  enabled; `[keyboard]`/`[controller]` one line per N64 button and per
  action (quit, menu, fps_counter, debug_cheats, the orbit camera's
  camera_*: SDL key names / SDL game-controller names, axes as `rightx+`),
  the analog stick's source (stick), deadzone and sensitivity, pad, the
  analog throttle's keys, and the controller's held-together combinations
  (menu, fps_counter, debug_cheats).  dist/README.txt lists them for players;
  `tools/readme_check.py` checks that both READMEs name every key of the
  defaults text.  Lines it doesn't understand are listed in one warning box;
  their defaults stay (an alias accepted for a key stays accepted).
  Command-line options override it.
- **Saves**: `saves/blastcorps.eep` (and with `controller_pak = 1`
  `saves/blastcorps.mpk`) next to the exe; `--saves DIR`, `--no-saves`,
  `--no-pak`, `--eeprom`/`--mpk` override.  With a pak the game keeps its
  progress on the pak and never touches the EEPROM (checked: level completion
  with a pak = 0 EEPROM accesses), so the default is no pak.
- **Debug cheats** (`[game] debug_cheats = 0|1`, the menu's Game tab, the
  `debug_cheats` key F3 / `[controller] debug_cheats` buttons for the run):
  the game's own cheats (gDebugCheats, the dev cart's `-c`): in a level
  C-right completes it, C-up + C-right fails it, Z + C-right is a perfect
  finish; A skips the promotion animation, Z gives the academy marker.  On at
  boot, bc.exe puts `-c` on the debug command line (exactly `--cmdline -c`;
  bc.exe takes bc_headless's `--cmdline STR`, and one with `-c`/`-C` counts as
  the setting on); switched in a running game, `host_cheats_set` (save.c)
  writes gDebugCheats, which every reader reads each time (hd_game_frame each
  frame, fe_promo_draw, academy_save_markers, the boot screen's chime), so
  that is safe at any retrace.  A "DEBUG CHEATS" marker (bottom right; top
  right when the counter is there) and a 4 s note when switched are drawn by
  the menu's overlay (never into the game's frame); bc.log logs the state at
  boot and every switch.  The other switches of parse_cmdline aren't exposed:
  `-C` also sets gDebugStepGameState (academy_advance_state then steps the
  rank one state per save, regardless of points: it changes saved progress);
  `-l` (gDebugRamEeprom) saves to a RAM copy of the EEPROM instead of the
  chip (lost at exit) and makes the D-pad, L and R tune the globe's light; `-d` (gDebugFlagD)
  starts the main thread once instead of twice; `-v -s -j -m` set flags
  nothing reads.  **Saves** (`[game] cheats_save = separate`, the default, or
  `normal`; save.c): once the cheats were on in a run, the EEPROM / pak files
  are `blastcorps.cheats.eep` / `.mpk` until bc.exe closes, and the real ones
  aren't written again.  On at boot: the cheats files from the start (a
  missing one is first copied from the real file).  Switched on in a running
  game: the switch waits until frontEndPresent is 0 (a level, the attract
  demo, before the front end loads; the level unloads the save thread first),
  so a save sequence the thread is part-way through ends in the real file;
  the cheats files are then written whole from the images in memory (the
  real saves as they are then).  Off again: the cheats files stay (the game's
  memory holds cheated progress).  The images the game reads never change:
  only the files written differ.  No settings_version bump: both keys are
  new, so no old default needs migrating; a file without them gets the
  defaults (off, separate) and is not rewritten.  An older bc.ini whose own
  bindings use F3 keeps it there (the cheats key is then unbound).
- **`--no-config`**: no bc.ini, the built-in defaults, no save files unless
  `--eeprom`/`--mpk`/`--saves`: bc.exe then runs exactly like bc_headless
  with the same options.  Tests and comparisons should pass it (with a ROM
  argument), so a developer's bc.ini can't change what they measure.
- Keys: Alt+Enter / F11 switch fullscreen (desktop resolution; RT64 keeps
  4:3), the window is resizable; F1 (`menu` in `[keyboard]`) or Back+Start
  (`menu` in `[controller]`) opens the settings menu.
- **`--set section.key=value`** (repeatable): a bc.ini setting for this run
  only, over the file or `--no-config`'s defaults (e.g. `--set
  audio.volume=50`); a bad one is listed with the file's warnings (fatal
  with `--no-msgbox`).
- **The settings menu** (`src/live/live_menu.cpp`): an overlay drawn with
  Dear ImGui, which RT64 bundles (MIT): RT64's `Inspector` class sets ImGui
  up for the device and swap chain RT64 made (D3D12 or Vulkan backend, SDL2
  input), the menu builds a frame on the main thread and RT64's present
  thread draws it over the picture from the render hook (`RT64::
  SetRenderHooks`, the draw hook).  Nothing of it exists until the menu is
  first opened, and the hook draws nothing while it is closed.  While it is
  open the game waits inside the retrace hook (`liveVi`): RT64's own pause
  (`state->debuggerInspector.paused`, its debugger's) re-renders the last
  frame's work and presents it at each `updateScreen`, ~60 times a second,
  so video settings show at once; the game's virtual clock doesn't move
  (same per-frame trace), the pacing starts over afterwards, and the keys
  and buttons held when it closed don't reach the game until each is let go
  (`live::InputGate`: per input, the stick until it is back in its dead
  zone, at most one second, so nothing stuck can keep input blocked; new
  inputs pass at once).  It works with the
  mouse, keyboard (arrows, Enter, Esc) and a controller (ImGui's gamepad
  navigation), and runs in the swap chain's pixels (bc.exe is DPI aware;
  ImGui's D3D12 backend ignores the framebuffer scale).  Each change is
  applied at once (`settingChanged`: fullscreen, window size, vsync, volume,
  mute; the graphics API on the next start) and the settings changed are
  written to bc.ini when it closes (`configSave`: only their lines are
  rewritten, or added to their section; comments and everything else stay;
  nothing with `--no-config`).  Testing: `--menu-shot F` opens it at frame
  F, saves a picture of the window and closes it, `--menu-set S.K=V`
  changes settings in it as a player would; `--wshot F,..` saves the
  window's picture (PrintWindow with PW_RENDERFULLCONTENT, after RT64
  presented: RT64's output at the window's size, unlike `--shot`'s 320x240
  RDRAM picture) and `--window WxH` sets the window's size.
- **The level and model viewer** (`bc.exe --viewer [LEVEL]`, or the settings
  menu's Viewer tab).  Roadmap item 6, part 1: looking at levels and models
  with the game's own drawing (the editor, part 2, works in it).  Its files:
  `src/live/live_viewer.cpp` (the core: the game's state, the records, the
  capture, the replay hooks, the frozen loop), `viewer_edit.cpp` (the
  editor's record edits, the gizmo, mod files), `viewer_terrain.cpp` (the
  Terrain mode), `viewer_panels.cpp` (overlays, the Inspector, the panels),
  `viewer_input.cpp` (keyboard, mouse, controllers, the camera's step),
  `viewer_script.cpp` (`--viewer-test` and its virtual controller), their
  shared state in `viewer_internal.h`; the records and camera math in
  `viewer_records.cpp`.
  - **Holding the game still.** As the settings menu does: the game waits
    inside the retrace hook (`liveVi` calls `viewerVi`), so none of its code
    runs and its virtual clock doesn't move.  The picture is the game's last
    captured frame, drawn again every tick: its graphics tasks go to RT64 once
    more (`processDisplayLists` on the same RDRAM, read only; the VI line
    counter alternates so RT64 presents each one).  Chosen over the game's own
    pause (it changes modes, windows and the clock) and over stepping frames
    (the world would move): nothing of the game changes while the viewer is
    up.
  - **The capture.** Before it freezes, the game runs two more frames (no
    controller input, unpaced, silent) with every level cell in the drawing
    paths (`PORT_DRAW_CELLS`, as the orbit camera's; as many cells as the
    game's level display-list buffers hold, nearest first: an upper bound of
    `lvl_build_visible_dl`'s copy per list against `lvl_alloc_dl_buffers`'
    sizes, so nothing overflows; "draw every cell" off: the game's own cells)
    and its `PORT_ITAG` tags recorded (`host_viewer_itag_hook`, plat_core.c).
    Opened over play and left without loading a level, the game goes on from
    there: the trace of such a run equals a run without the viewer (tested on
    the attract demo and on level 3 driven by the verify-levels input).
  - **Loading a level** (the menu's level picker, `--viewer LEVEL`): the game's
    own set-up.  `host_viewer_request` (plat_core.c); at the end of a frame of
    `hd_main_thread`'s loop (`PORT_VIEWER_FRAME`, game/port.h, the one game-code
    hook) in a level or the attract demo (no front end loaded: the set-up
    destroys the save thread), not fading and with no mode switch pending, the
    game gets `levelno`, `gDiggerVehicle` (bonus levels: the lowest vehicle of
    the level's `LevelInfo.vehicles` stands for the player's pick) and
    `gNextGameMode = MODE_LEVEL_START`; a finished mission's medal reads "not
    done" for that one switch (else the set-up would apply the status block
    the front end loaded for another level).  From boot the request waits for
    the attract demo (frame ~503).  All 60 level ids load (missions start in
    play, other kinds in their countdown).  Since such a game wasn't brought
    there by play (no player loaded from boot, a stale status block), leaving
    the viewer after a load restarts bc.exe (the same command line without
    `--viewer`, then exit); nothing is saved meanwhile (saving happens only in
    the front end's results modes, never reached).
  - **What is drawn** (the command hook, `interpSetCommandHook`, and an owner
    hook before every command, `interpSetOwnerHook`, live_interp.cpp): who
    draws what comes from the tags (`HUD`, `RADAR`: HUD; `OBJ`, `SHADOW*`:
    vehicles; `BUILDING`, `BPART`, `FALL`, `RISE`, `GSHADOW`: buildings;
    `BILLBOARD`, `PARTICLE`, `PEOPLE`, `MARKER`: effects), the level's lists
    (`gLevelDl0..3[2]`, `gLevelFileDl0..3`, the packed calls at SEG2(0xA4E0):
    terrain) and the frame's sky (the backdrop and ground fill after the
    second `G_SETCIMG`); a category turned off has its draw commands
    (triangles, quads, texture and fill rectangles) left out, the state
    commands kept.  Plain background (the default): the ground fill drawn
    full screen in grey and the backdrop left out (the game's sky is a flat
    picture for its own camera).  The free camera's view replaces the frame's
    game view in every perspective projection that has it (RT64 patch 0006's
    `ViewHook`), and the far plane moves from 10000 to 120000.  Front-end
    frames (the globe, menus) are drawn as they were, with the game's camera.
  - **Overlays and the inspector** (ImGui over the picture, `uiFrame` in
    live_menu.cpp): markers and labels for the level file's records - vehicle
    spawns (type names, model files), the carrier record and its route (a
    straight line along its heading, on the ground), object placements, RDUs,
    TNT, ammo crates, communication points, push blocks and slots, height
    zones (optional), panel events (inspector only) - and the runtime
    `gObjects` / `gVehicles` records; collision outlines near the camera
    (grids A and B from the file, the buildings' `CollisionTri`s).  A click
    selects the nearest marker; the inspector lists its fields (levels.md's
    layouts), its runtime record and its raw bytes.
  - **Model view**: a vehicle (`gVehicles` record: its `OBJ` tag) or a
    building (`gObjects` record: its `BUILDING` tag) of the current level
    alone, on the plain background, with an orbit camera centred on what it
    drew (its vertices through RT64's model matrices).  Vehicles not in the
    level would need their files loaded onto the game's heap (it can't grow
    without changing the game): not done.  The driver: while he sits in a
    vehicle (a level's start) `veh_draw_layer` leaves his record out, so the
    captured frame has nothing of his; his model view then runs, host side
    and once per replay, the frame's first layer-0 vehicle commands (the
    game's own: segments, env colour, geometry mode, list call, matrix load)
    with his segments, alpha and list - his own display list over his own part
    matrices (the standing pose his last update left) - and branches back
    (`driverInject`: RT64 reads the commands from a host array; RDRAM and the
    game's state are untouched).
  - **Controls**: WASD move, Q/E (C/Space) down/up, Shift x4, Ctrl x0.25, the
    right mouse button held: look, the wheel: speed; a controller's left stick
    moves, the right stick looks, the triggers go down/up, the shoulders
    change the speed, A selects at the centre, B backs out, Y: the player's
    start, Back: leave, L3: show / hide the panels, Start: the controller on
    the panels - ImGui's gamepad navigation (RT64's ImGui backend never sees
    a controller, so the viewer feeds it the controllers' state itself,
    `uiPadState` in live_menu.cpp): D-pad / left stick move, A presses, B
    backs out of a list, hold X + LB/RB picks another panel in a fixed order
    (Level viewer, Inspector, Changes, Terrain - those shown; ImGui's own
    goes by the last used: `panelCycle` sets its windowing target, the
    shoulders aren't ImGui's while X is held), Start or Back hands the
    controller back to the camera.  The panel for what is being done takes it
    (Terrain, the Inspector with a record selected, else the Level viewer);
    after an Apply or a screenshot it goes back to the panel it was on.  The
    Level viewer panel's first item is "Player start" (A right after Start
    lands on the first item), and "Leave..." asks first (a popup whose first
    button, Stay, is what A presses).  The mod's name has an on-screen
    keyboard ("Keys..." in the Changes panel: a key grid the navigation
    walks, B or Done closes it), and the PNGs of a `textures\` folder next
    to the exe are listed for the texture slots, so a whole session needs no
    keyboard.  Home/P: the player's start, O: an
    overview, F: look at the selection, M: model view, Tab/F1: the panels,
    F12: a clean screenshot, Shift+F12: one with the overlays (in
    `screenshots\` next to the exe, or `--shot-dir`), F11 fullscreen, Esc:
    leave (model view: back).
  - **Testing**: `--viewer-test "OP;OP;..."` runs a script in the frozen
    viewer (`wait:N`, `cam:start|game|top|X,Y,Z,YAW,PITCH`, `show:CATEGORY|far|
    plain|markers|labels|collision|zones|cells|labeldist|KIND=0|1`,
    `select:KIND:I`, `dump:KIND:I` (the record's fields to the log),
    `model:KIND:I|off`, `orbit:YAW,PITCH[,DIST]`, `shot:NAME` (clean),
    `oshot:NAME` (overlays), `pshot:NAME` (with the panels), `level:N`,
    `recapture`, `leave`, `quit`); `--viewer-open F` opens it over the game at
    frame F.  `bc_cameratest` tests the record parsing and camera math on a
    synthetic level image.
- **The level editor and mods** (roadmap item 6, part 2: records; part 3:
  terrain and textures, below).  The model: `src/load/level_mod.c` (plain C, shared by
  bc_headless, bc.exe and the editor: a level file as the ROM holds it, its
  editable records, add / delete, the layout, the change list, `.bcmod`
  files, SHA-1), `src/live/level_edit.cpp` (the editor: undo / redo, which
  records were edited, the gizmo's ray math), `src/load/port_mods.c` (the
  edited files the game reads).  docs/tools.md has the mod format.
  - **Editing** (the viewer's Editor panel, Ctrl+E): the editor works on a
    host-side copy of the level file - the ROM's, inflated by the host
    (`port_level_original`: both gzip members at lvl_load_file's ROM offset),
    plus the changes a loaded mod makes - and the markers and the inspector
    show that copy (`viewer::parseLevel(..., fileOrder)`).  Records: vehicle
    spawns, the carrier record, object placements, RDUs, TNT, ammo crates,
    communication points, push blocks and slots.  A selected record gets a
    gizmo (ImGui overlay): the square drags it on the horizontal plane at its
    height (`edit::pickRay` through view x projection, `rayPlaneY`), y then
    taken from the level's ground grid on the host side (the viewer's
    `groundAt` over `gGroundCells`: the game's `coll_ground_height` writes game
    state, so it isn't called while frozen), optionally on a grid; the arrows
    move along an axis (`rayAxis`), the ring turns the heading.  The inspector
    edits every field as a number (typed: when the field is left; its - / +
    buttons: at once, and a burst of them - held, repeating, or pressed again
    within 0.7 s on the same field - is one undo step, `pushEditBurst`; a
    burst back where it began leaves none); Ctrl+Z / Ctrl+Y, Ctrl+D (a copy 48 units
    along x), Del, R, G.  Each edit is a change (`LmChange`) pushed on the
    list; the copy is rebuilt from the original and the list (so undo is
    exact), checked against the game's rules (`lm_check`: one driver spawn,
    known vehicle types, the fixed arrays' sizes: 8 spawns, 55 objects, 400
    RDUs, 20 TNT, 15 crates, 2 communication points, 8 blocks and 8 slots)
    and refused if it breaks one.  Edited records are drawn magenta with a
    `*`, a mod's orange; the Changes panel lists the changes, and warns before
    Apply when edits moved ground under or beside the carrier's route (160 map
    units either side, from 200 before its start to its end) or moved / added
    buildings, vehicles or push blocks onto it (`carrierWarning`; the game
    crashes the carrier when it hits something or its wheels' heights part).
  - **Apply**: the edited file goes to `port_level_set_edit` and the level is
    loaded again by the game's own set-up (the viewer's level request), so
    the picture afterwards is the game running the edit.  **Play from here**:
    the same, then the viewer is left into play once the level is up (no
    restart): a test run, with saving switched off for the rest of the run
    (`host_saves_block`, save.c: the game saves into its EEPROM / pak images
    in memory as always, os_si.c and pif.c don't write the files) and a
    "TEST RUN" marker.  **Save mod**: `mods\<name>.bcmod` next to the exe
    (the mods' changes, then the editor's, with both SHA-1s), optionally
    added to bc.ini's `[mods] enabled`; unsaved edits are kept in
    `mods\autosave_LNN.bcmod` when the viewer is left.
  - **The game-code hook** (the only one): `lvl_load_file` (00000.c) calls
    `PORT_LEVEL_PRE(level, dest)` before its `hd_rom_load` and
    `PORT_LEVEL_POST(level, rom, dest, size)` after it (game/port.h, empty in
    the N64 builds).  With no edited file for the level both return at once.
    With one, PRE keeps the bytes (and byte-order units) under the ROM file's
    extent; `hd_rom_load` runs as always (the DMA, the inflate, the swap);
    POST puts back what lay beyond an edited file that is shorter, writes the
    edited file (big-endian, as the ROM would hold it) over the inflated one,
    sets `*size` and swaps it through `port_on_load`, as the ROM's was.  The
    heap then grows by the edited file's size, exactly as for a ROM that
    holds the edited file (the compressed staging buffer still holds the
    ROM's bytes: the only difference).  An edited file may be at most 64 KB
    larger than the ROM's.
  - **Mods at load time**: `bc_headless --mod FILE` and `bc.exe --mod FILE`
    (repeatable), bc.exe's `[mods] enabled = a, b` (files in `[mods] folder`,
    default `mods`; `--no-mods` skips them).  `port_mods_add` checks each
    level's `original_sha1` against the ROM's file, applies the changes,
    checks the rules and the object types against the ROM's object table and
    the `edited_sha1`; a mod that doesn't fit is refused whole (bc_headless:
    the run stops; bc.exe: a message, the game starts without it).  Two mods
    can't change one level.  `bc_headless --mod-out DIR` writes the edited
    files (testing).  No mod and no edit: nothing runs, the traces equal
    main's.
  - **Moved files** (a ROM from the shiftable build, docs/shiftable.md "The
    port"): the port runs its own code, so every ROM offset it uses is the
    original ROM's (`kLevelRom`, port_assets.c's ranges, `rdram.h`'s code
    offsets, every ROM symbol linked into the game code, the level and model
    tables in the code).  When files moved, `make SHIFT=1` writes a ROM map
    into the ROM's trailer (runs of the original ROM that moved by one amount:
    original start/end, new start, new end of the data; a footer `BCSHIFT!`
    in the last 16 bytes; nothing reads the trailer, the CRCs don't cover it;
    an unchanged build has no map and stays the original ROM byte for byte).
    `rdram.c` reads it at load and every read of the ROM goes through it:
    `osPiStartDma` (os_hw.c: `rom_map_dma` gives the place, and a read that
    ends where a run ends, i.e. a whole file that grew or whose neighbour
    moved, gets the file's new length), `hd_rom_load` (`PORT_ROM_SPAN`: the
    whole file's range before its 16 KB pieces), `rom_gunzip` (the code and
    data images), port_mods.c's level files, the font glyphs.  The texture
    and object tables hold offsets from themselves; `rom_map_fixup` turns
    their entries into original offsets as they are read, so everything the
    code adds to them is an original offset too.  port_assets.c classifies by
    original offsets, unchanged.  No map (the retail ROM): every one of these
    returns its argument, the traces equal main's.  bc.exe's ROM check accepts
    a ROM with a valid map whose data images (hd_code's and the front end's,
    inflated) have the supported ROM's SHA-1 and whose map doesn't say code
    or data moved in RAM (the port's code would not be the ROM's then), with
    a log line; `rdram_load` logs the same line (bc_headless too).  An
    exported ROM whose levels fit in place (no map) still needs
    `--no-rom-check`.
  - **Geometry mods** (roadmap item 6, part 3): bcmod/2 files may carry a
    level's "geometry" (moved corners, added and removed drawn / floor / wall
    triangles, G_SETTIMG retargets; docs/tools.md "Geometry").
    `lm_mod_apply` applies the record changes, then `lg_apply`
    (`src/load/level_geo.c`, the C twin of tools_port/bcfiles geometry.py)
    regenerates the ground grid, the collision grids, the visibility heights,
    the bounds and the display-list block from the edited triangles, keeping
    the file's own data wherever the edit doesn't reach (an empty edit gives
    the ROM's file back).  The result goes through the same hook as any mod.
    loadcheck TD checks that `lg_rebuild` makes the same files as `bcfiles
    geo-rebuild` on all 60 levels; mods give the same bytes as `bcfiles
    mod-apply`.  `--level-dir DIR` (bc_headless, bc.exe; testing): the game
    reads `DIR\level_NN.bin` instead of the ROM's level (`port_mods_read_levels`).
    The visible-list budget: the four per-frame lists with every cell visible
    against `lvl_alloc_dl_buffers`' sizes, counted as `lvl_build_visible_dl`
    copies them (a run of records with one id: the first from its start, the
    others from their alt offset; groups from the alt offset;
    `visible_count`, bcfiles `list_commands` likewise, 60/60 levels equal).
    Counted so, no level's own data overfills a list - the old plain sum of
    every command said levels 4 and 43 did (7746 and 6513 of 6000), the copy
    gives 4295 and 4315 - so the budget is the game's own size everywhere and
    splits work on those levels too.
  - **Splits in scripts**: bcfiles `terrain.py` is the twin of the editor's
    `src/live/terrain.cpp` (SUBDIVIDE / SEAL: neighbours, floor twins and walls
    split along the edges, kept edges, the seams; the welds and MOVE's
    resolution), and `geometry.subdivide_edit` uses it;
    `port/tools/subdiv_cross.py BC_CAMERATEST.EXE ROM` splits patches of
    several levels with both (`bc_cameratest --subdiv DIR`), moves them, runs
    raise strokes, and compares the edits entry for entry and the welds'
    counts; it also checks the result for cracks (open edges of the drawn and
    floor triangles near the edit that weren't open before).
  - **Texture slots** (bcmod/2 "textures": the author's PNG for a table entry):
    `src/load/tex_codec.c` is the C twin of bcfiles texture.py / png.py (PNG
    decode over `inflate.c`, PNG encode with stored deflate blocks, base64, the
    table's seven packings, RGBA8 <-> texels, `tc_encode_slot`: the same
    greedy back-references, so the same packed bytes; bc_cameratest checks 697
    vectors from texture.py, `port/tools/texcodec_cross.py BC_CAMERATEST.EXE`
    1800 PNG + slot cases byte for byte, mipmapped and palette slots
    included).  A mipmapped slot (`"mips": n`: lists that LOADBLOCK it and
    sample tiles 1..n smaller) gets every level made from the picture
    (`tc_encode_slot_mips`: halved by 2x2 averages, rows padded to 8 bytes,
    odd rows' words swapped as TMEM interleaves them), so it shows the
    picture far away as well; the palette packings 4 and 5 (`"palette": id`:
    effect sprites, medals) keep the shared palette and take each pixel's
    nearest colour (`tc_encode_slot_pal`).  `port_mods_add` checks each slot
    (the SHA-1 of its packed bytes, the PNG, the room up to the next entry) and
    packs it the slot's way; from then on `port_on_dma` (every ROM -> RDRAM
    DMA) lays the patched bytes over the DMA'd ones: the slot's room (packed
    bytes, then zeros) and the table entry's size - what a ROM made by `bcfiles
    mod-export` holds, so the game's own loader and decoder run it.  No slots:
    `port_rom_patches` is 0 and nothing runs.  **A slot that grows** (its
    picture packs to more than its room, up to what the texture unpacks to):
    its patch is its packed bytes rounded up to 8; the game, whose table here
    keeps every offset, asks for the old room, and `port_dma_len` (called by
    osPiStartDma before the copy) hands that DMA - and only a DMA of the slot
    itself - the whole grown slot (`port_rom_grown`), as the shifted ROM
    `mod-export` builds for it gives it (its ROM map reads a grown file to its
    end).  **New slots** ("new"): one of the table's empty entries (no bytes in
    the ROM, empty entries before and after it: the entry before a texture's
    ends its DMA, so an entry right after a texture is never taken) gets the
    picture stored (packing 0) at a ROM address past the 8 MB ROM (0x02000000
    on, inside the cartridge's address range: as a larger cartridge holding it
    there would give it), the entry's offset, size and packing and the next
    entry's offset (the DMA's end) patched in the table the game DMAs; "from"
    names the slot whose texels after the picture's (smaller levels of detail)
    it keeps; a run's G_SETTIMG (settimg) names the new id.  bcfiles
    mod-export puts new slots in the ROM's free space at its end (or an added
    file on the shift path) and points the table there: the same bytes, the
    same DMA lengths.  Caveat: the game's texture cache holds 250 ids
    (`gTexCache`); a level that draws many more textures than it did could
    fill it.
  - **The Terrain mode** (roadmap item 6, part 3, phase 2; the editor's
    "Terrain" choice, Ctrl+T): `src/live/terrain.cpp` is the model (plain C++,
    bc_cameratest tests it): the level's triangles from `lg_mesh_open` (the ids
    an edit counts, each drawn triangle's list and texture state, the protected
    corners: bridges' lists, moving parts, switches, gates), their corners
    welded by position as an edit moves them; steps MOVE (the brushes raise,
    lower, smooth, flatten with radius / strength / falloff; a ramp between two
    points; a selection moved by hand), SUBDIVIDE (drawn triangles and the
    floor triangles with the same corners split four ways at their edges'
    midpoints - geometry.py `_split`'s values; every neighbour sharing an
    edge with a split triangle is split along it too - 2, 3 or 4 pieces, the
    shorter diagonal - and so are the floor triangles under them and the wall
    triangles standing on those edges, so no T-junction is left and no crack
    opens when the new corners move; corners of older splits lying on
    another triangle's edge (a mod made before) are counted as seams and
    "Close seams" splits the triangles there, step SEAL; a split edge's
    midpoint is another mesh's corner when one lies on the edge within 2 of
    its middle), PAINT (drawn
    triangles join a run of another texture in their list, or in a list drawn
    where they are; coordinates kept, or projected from above at the run's own
    density) and RETEX (a run's G_SETTIMG); `exportEdit` makes the LgEdit
    (moves of the level's corners, removes, adds with the corners' current
    values, settimg), `import` takes a mod's in (adds bound to the corners they
    share).  **Welds across meshes**: Rare's meshes (the display-list pieces,
    the floor, the walls) meet without sharing corners - corners a unit or
    two apart stand for one, and a mesh's corner often lies on another mesh's
    edge (L0: triangle 113's edge at z 2020 carries list 6's corner (2210,
    140, 2020); moving 113 up 8 opened a sliver there).  The level's corners
    within `kWeldTol` (2) on every axis are a group (chained) that a MOVE
    moves as one (by the move of its lowest-numbered moved member), and a
    level corner within 2 of another triangle's edge (more than 2 from its
    ends: two thirds of the 60 levels' open edges have another mesh's edge
    within one unit, then the count falls to the background) follows that
    edge while it is there: it keeps its place along it as the edge's ends
    move and can't move by itself (15-35% of a level's corners; a Split of
    that edge takes it in and frees it).  A border corner 2-3 units from
    another mesh's border edge (a seam Rare left wider, `kSeamTol`) follows it
    too.  A corner on two meshes' crossing edges (overlapping meshes) follows
    one; a MOVE that would take them more than a unit apart there splits both
    edges at it (and each half at the other corners that followed it), the
    corner going to the mean place (`splitCrossings`, WeldInfo.tjunctions):
    no corner on another mesh's edge is pulled off it.  `Model::resolve` adds the welds to
    a MOVE's corners (apply, and the stroke's preview); a protected corner a
    weld would move stays and the step's message says so.  Every step is one entry of the editor's change list with the
    record changes (`edit::EditStep`; undo / redo rebuild the model and the
    file from the original); the image is `lg_apply`'d at each step (8-21 ms on
    the largest level, 328 KB) and a step that breaks a budget (the visible
    lists, the port's 64 KB room for a grown file) is refused with the reason.
    "Put records on the new ground" moves records by the ground's change under
    them since they were last put on it (one step).  The mods' geometry and
    texture slots are the editor's base: a saved mod keeps them (with the
    editor's own), so a level opened from a geometry mod saves its geometry
    again.  **Preview**: while a stroke is made and until Apply, `replay()`
    writes the moved corners into the vertices the frozen frame's lists point
    to (the level file's segment 8, matched by position) before RT64 reads the
    frame and puts the game's values back right after (RDRAM holds what the
    game left whenever the game could run); G_CULLDL is skipped meanwhile (the
    boxes are the old ones).  Splits, paint and slots change the lists: Apply
    shows them (the game rebuilds and reloads: 0.19-0.30 s; "Apply after every
    stroke" does it per step; the camera stays).  The panel draws the brush
    circle on the ground, the protected parts in red, the selection; texture
    slots from a PNG (the selection's slots with their size, format, packing
    and room, the picture converted, packed and previewed texel by texel, or
    put in a new slot for the run); the level's size and lists against their
    budgets.  Controller: the tool at the picture's centre (a cross marks
    it; the left stick forward moves along the ground at the camera's height,
    so the point moves - a ramp's end follows it, joined to its start); A
    held: the brush (a click for the others; Ramp: start, end, A again
    makes it), X: the next tool, LB / RB: the brush's size, D-pad left /
    right: strength or amount, up / down: the selection up / down 8 (Select)
    or the ramp's rise, B: let go of the selection or the ramp; Start: the
    panels (split, paint from the texture list, Apply, Undo, Save mod: every
    terrain and record tool works from the controller alone).  An Apply whose
    carrier crashes at once (an edit under or in front of it: the game's own
    `carrier_check_crash`) is held there with a message instead of the game
    going back to the front end.
    Test ops (`--viewer-test`): `tmode`, `ttool`, `tbrush`, `tflat`,
    `tstroke:X1,Z1,X2,Z2,N`, `tstrokec:N`, `tramp:X1,Z1,X2,Z2,RISE`,
    `tselect:X,Z[,add]`, `tclick`, `tclear`, `tsplit`, `tmove`, `tpaint`,
    `tretex`, `timport:PNG`, `timportnew:PNG`, `tfollow`, `tapplyeach`, `tpal`,
    `tseal`, `tinfo` (through the picking where a point is given; it counts
    the corners following another mesh's edge).  A virtual
    SDL controller drives the same path a real one takes (SDL's controller
    API, the controllers' thread, the camera, the tools, ImGui's navigation):
    `vpad:attach`, `vpad:A+X[,N]` (held N frames), `vpaddown:B` / `vpadup:B`,
    `vpadaxis:lx|ly|rx|ry|lt|rt,V`; `padstate[:log]` logs where the
    navigation is (and the aim point, the ramp, the mod's name).
  - **Testing**: `--viewer-test` ops `edit:on|off`, `eset:KIND:I:F=V,..`,
    `edup:KIND:I`, `edel:KIND:I`, `eundo`, `eredo`, `edrag:KIND:I:X,Z`,
    `erot:KIND:I:DEG`, `eaxis:KIND:I:x|y|z:D` (through the gizmo's picking),
    `egrid:N`, `esnap:0|1`, `eapply`, `eplay`, `esave:NAME[:1]`,
    `estep:KIND:I:FIELD:D[:N]` (the Inspector's - / + pressed N times),
    `ewarn` (the carrier-route warning to the log).
    `bc_cameratest` tests level_mod.c and level_edit.cpp on a synthetic level
    file (the layout of an odd-sized add, undo to the original, the rules,
    the mod format, SHA-1, the ray math); `tools_port/bcfiles/test_bcfiles.py`
    tests `mod.py`.
- **Optional enhancements** (all off by default: with the defaults bc.exe
  renders exactly as before, and none of them changes the game logic or its
  per-frame trace, except the visibility test of widescreen and of a higher
  frame rate, below).  `[video]`:
  - `resolution` (`--resolution`): `window` (the default: RT64's
    WindowIntegerScale, the whole multiple of 240 lines that covers the
    swap chain, what bc.exe always used), `1` (the N64's 320x240,
    `Resolution::Original`) or `N` (`Manual`, N times 320x240; higher than
    the window = supersampled).  `msaa` (`--msaa`): 0 (default), 2, 4, 8
    (RT64 needs programmable sample positions; the menu offers what the
    device supports).  `filtering` (`--filtering`): `aa` (RT64's
    anti-aliased pixel scaling, the default), `linear`, `nearest`: how the
    finished picture is scaled to the window.  Applied before RT64 starts
    (`configToRT64`) and live from the menu (`updateUserConfig`, and
    `updateMultisampling` for a new sample count, as RT64's own inspector
    does).  At high resolutions some seams between the level's ground
    polygons show (the N64's rasteriser hides them at 240 lines).
  - `aspect` (`--aspect`): `original` (4:3, the default), `16:9`, `16:10`,
    `21:9`, any `W:H` up to 4:1, or `window` (as wide as the window).
    RT64's own widescreen (`userConfig.aspectRatio` Manual + `aspectTarget`,
    or Expand): every projection whose viewport covers the whole width gets
    a wider viewport and its x scale narrowed to match
    (`rt64_projection_processor.cpp`, perspective and orthographic), so the
    3D view widens without stretching; 2D rectangles and partial-width views
    keep their 4:3 shape in the middle (the HUD, text, the radar, the
    front end's pictures: pillarboxed); full-width fill rectangles stretch.
    No game code changes: the game still builds its 4:3 perspective
    (00000.c hd_build_frame_dl `guPerspective(..., gCamFovy, 4/3, ...)`, the
    front-end screens likewise).  The game's CPU-side visibility test
    (cull_rsp_test via 00000.c cull_start, which uses that projection)
    decides which level cells, and the buildings and objects on them, are
    drawn, so it must widen with the view: `host_cull_wide` (os_hw.c) makes
    `cull_visible` accept |x| <= k w with k = the aspect / (4/3) (from the
    window's size with `window`, updated on resize), else cells at the
    edges would pop in.  1.0 (4:3) runs the exact model; with widescreen on
    the game sees more cells visible, so it can in principle play out
    differently from the N64 (it's an option); measured: at 21:9 the
    per-frame trace (retrace, time, mode, level, frames in mode, game
    retrace counter) is the same as with 4:3 in all 60 levels (1900 frames).  bc_headless `--cull-wide K` runs the same;
    checked against an independent model of the K-times-wider frustum over
    every test of levels 0, 13, 30 (1187 tests, K = 1, 4/3, 1.75: all
    agree; at 1.75, 184 tests turn visible only through the wider view).
    The game's full-screen 2D would stop at the 4:3 edges too, so it is
    widened on RT64's side while RT64 reads the display list
    (`src/live/live_wide.cpp`, through live_interp.cpp's GBI wrapping; RDRAM
    and the display lists untouched; nothing at all with `original`): the
    sky (2D810.c backdrop_draw, a textured quad under the orthographic
    projection: RT64's copies of its 4 vertices are widened about the
    screen's centre, its texture coordinates with them, so the sky goes on
    to the edges instead of showing the far ground or older pictures above
    the horizon), the full-screen fade quad of a failed mission and of the
    attract demo's end (17E10.c, gFadeVtx), the dark panels of the
    full-width message windows (26570.c gYoshiWindowDl, widened from 80 %
    of the width up), and the front end's background picture (196F0.c
    fe_bg_draw's 64x16 rectangles, drawn with RT64's extended
    `G_EX_ASPECT_STRETCH`: stretched to the width).  The fades to black
    (30430.c), the carrier blast's red tint and the clears are full-width
    fill rectangles (stretched by RT64 already).  Two front-end pictures
    the game names with `PORT_WTAG` (port.h; host-side tags like
    `PORT_ITAG`, nothing written to RDRAM) are widened the same way: the
    ticker's lit band (01C40.c gPanelDl, a 3D strip; the player select and
    the results screen) and the globe's starfield (11530.c; each star's quad
    moved out from the view's axis by k, its size kept, so the stars fill the
    wider picture as densely as the 4:3 one).  The ticker's text and icons
    scroll across the whole width too: the game keeps only the characters
    that fit 320 pixels, so the host draws the rest (`PORT_TICKER`, 01C40.c;
    live_wide.cpp, plat_core.c port_ticker_gfx): left of the picture the
    characters that left the line and the icons the game stopped drawing,
    right of it the text the line shows next and where its icons will be
    (the game's scroll simulated from the frame's state), with the game's
    own font glyphs (decoded from the ROM by its decoders) and commands run
    by RT64 inside the front end's text flush; the vertices and textures
    are host memory RT64 reads through its extended RDRAM addresses, so
    RDRAM is never written.  If the text changes mid-scroll (Z/R on the
    load-game menu) the characters right of the picture show the new text
    at once.
    The front end's screens can be reached and pictured with
    `tools/fe_screens.py` (recorded input in `data/fe_input/`, pokes for the
    promotion / academy screens; it checks the game mode at each picture).
    Limits (widescreen): markers that point at off-screen targets (30C70.c) still use
    the 4:3 edges.
  - `hud_position` (`--hud-position`): `centered` (the default: the HUD in
    the 4:3 middle, as before) or `edges` (widescreen only): the HUD's left
    parts (the vehicle panel or portrait when it is top-left, the Skyfall's
    dial, the missile / ram icons and counts, the radar and the carrier's
    target arrow, the objective icon, laps, timers and progress text) move to
    the left edge and its right parts (money, the panel when bottom-right,
    the fixed-camera icon) to the right edge, keeping their distance from
    the 4:3 picture's edge.  The game tags its HUD drawing with the side
    (`PORT_WTAG` in 00000.c hd_build_frame_dl, 1D990.c, 32E00.c, 39050.c;
    text strings by where they lie, recorded at 14B30.c text_queue_string
    because the flush sorts the glyphs of all strings), and live_wide.cpp
    sets RT64's extended-GBI origins while RT64 reads those commands (the
    viewport's origin for vertices, the rectangle alignment for texture
    rectangles, a scissor spanning the screen), with RT64's
    `extAspectRatio` Expand.  Centred messages, the message windows and
    menus (pause, level complete), the markers that follow the 3D view and
    the front end stay centred.
  - `framerate` (`--framerate`): `original` (the default: RT64's
    `refreshRate` Original, every picture as the game made it, 30 a second
    in play), `display` (`RefreshRate::Display`: the display's refresh rate)
    or `N` (30-1000, `Manual` + `refreshRateTarget`; RT64 caps it at the
    display's rate). Above the game's own rate RT64's workload queue renders
    extra pictures between the game's last two frames
    (`rt64_workload_queue.cpp`, `curFrameWeight`) and its present thread
    shows them 1/N s apart (`rt64_present_queue.cpp`). The game logic is
    untouched: it keeps its own cadence and the per-frame trace is the same
    as with the option off (below). Nothing is added to the game's display
    lists either (no extended GBI, so N64 RAM is exactly as with the option
    off): RT64 matches the two frames' draw calls itself (every matrix is
    `G_EX_ID_AUTO`: calls with the same combiner, other modes, geometry mode
    and triangle count, paired by the nearest transform,
    `rt64_game_frame.cpp`), so the camera (projection and view) and every
    model matrix it finds again are interpolated: vehicles, the carrier,
    the train, debris, the globe; a matrix that moved more than ten times as
    far as the frame before is not blended (`RigidBody::updateLinear`,
    AUTO). `live_rt64.cpp` adds, only with the option on:
    the game's cadence (the median of the last three intervals between
    frame-buffer swaps, `State::setRefreshRate`: RT64's own guess from the VI
    history turns interpolation off whenever the cadence wavers); cuts
    (`interpTask`: a frame of a new game mode or level, or whose camera,
    read from the frame's own `DynamicBuf.view` view matrix, jumped more
    than 400 units and 4 times as far as the frame before, or turned more
    than 40 degrees, gets rate 0: RT64 shows it as it is, with no in-between
    pictures into it; in play the camera moves at most ~7 units and turns
    under 1 degree a frame, the mission intro's cuts move 2500-7500); and a
    visibility test 1.1 times as wide (`host_cull_wide`, times the widescreen
    factor), so cells at the sides of an in-between picture whose camera is
    still turning don't pop in. `--interp-log` logs every frame's camera
    motion; `--display-hz N` tells RT64 the display runs at N Hz (testing a
    rate above this PC's 60 Hz display, with `--no-vsync`); `--wseq F:MS`
    saves every new picture the window shows for MS ms from frame F (screen
    copies: the window must be uncovered).
  - `interpolation` (`--interpolation`, only with `framerate` above
    original): `tagged` (the default) or `auto` (RT64's own matching, as
    above). Tagged: the game names what it draws and RT64 pairs every object
    with itself (`src/live/live_interp.cpp`):
    - in the game, `PORT_ITAG(gdl, kind, index)` where a draw loop starts an
      object, `PORT_ISPAWN(kind, slot)` where a slot pool starts a new
      object, `PORT_ICUT_IF(snap)` in the camera (include/game/port.h). They
      expand to nothing outside `NON_MATCHING && PORT_HOST`; in the port they
      call `port_itag` (plat_core.c), which hands them to `host_itag_hook`
      (NULL unless tagged interpolation is on). Nothing is written to the
      display lists or to RDRAM and nothing goes back to the game, so its
      per-frame trace is the same with the option on (checked: all 60
      levels). Tagged: the `gVehicles` records (vehicles, the carrier; per
      LOD), vehicle shadows, building objects and their moving parts, falling
      parts, the rising piece, explosion/smoke billboards, particles, people,
      ground shadows and, from the dial on, the HUD;
    - while RT64 reads a task, the GBI's function table (public plain
      function pointers) is wrapped, so every command passes `dispatch`
      first (RT64 itself is unchanged; the original frame rate unwraps it,
      unless another host feature's hook needs it). A
      tagged address makes that object the owner of what follows at that
      display-list level and in the lists it calls (RT64's return-address
      stack is followed); each model matrix it loads gets
      `RSP::matrixId(hash(kind, slot, generation, the matrix's
      segment-relative address or its order), G_EX_ORDER_LINEAR, position /
      rotation / scale interpolated)`, the call behind RT64's extended
      `gEXMatrixGroup`. RT64 pairs equal IDs of the two frames
      (`GameFrame::match`) instead of guessing; an ID that is new (a spawn, a
      new generation) is shown as it is;
    - a tagged matrix that moved over 400 units and 8 times its last move (a
      respawn, a teleport) starts a new generation (no blend across the
      jump); people and the ground shadows (world-space quads rebuilt every
      frame) get a transform each with vertex interpolation; the HUD and 2D
      get `G_EX_ID_IGNORE` (never blended, exactly as drawn: text, the dial,
      the radar's blips), except the radar's sweep and heading arrow
      (`PORT_ITAG_RADAR`, blended), the target marker's four arrows (their
      G_VTX at DynamicBuf + 0x1F00: an ID of their own, vertices blended, a
      new generation when the marker starts or moves to another target) and
      the attract mode's bobbing logo (left to RT64's matching); the globe's
      route lines (G_LINE3D of the L3D microcode, untagged) get one ID per
      task, matrices blended, a new generation when the globe's matrix
      jumps: RT64's own matching compares draw calls by their triangle
      count, which changes as line segments fade in and out, so it left the
      lines unpaired and in the in-between pictures they showed the next
      frame's position over a globe still half way there (they flickered and
      jumped about while the globe turned); untagged
      drawing (terrain, water, the backdrop, tyre trails, front end) keeps
      `G_EX_ID_AUTO`;
    - with `interpolation = auto` (RT64's own matching, no tags recorded) the
      GBI is wrapped all the same and just those two get their IDs
      (`interpTagsAutoIds`): the route lines (the jump ahead of the turning
      globe was there too) and the marker's arrows (left to RT64 they were
      never blended and stepped at the game's rate over a smoothly turning
      view: in the orbit camera's fast turns they trailed and caught up with
      their target every game frame); everything else stays `G_EX_ID_AUTO`.
      `--orbit-log` checks the blended arrows against where each picture's
      own orbit view puts the target's sides (`markerCheck`; level 1, 150
      degrees a second: at most 1.1 N64 pixels at 60 Hz and 1.4 at 144 Hz
      on the picture; 300 degrees a second: up to 3-7 at the picture's
      edges, where the projection bends most);
    - wheels (`wheelPivot`): a vehicle part multiplied onto its parent
      (`G_MTX_MUL`) can turn about an axle away from its model origin (the
      Backlash's wheels: about 30 world units off), so its origin swings round
      the axle between frames; RT64 moved the origin along a straight line
      while turning the part, and the wheels left their hubs in the
      in-between pictures (their dark tread showed beside the striped drums
      at 60 and 120 fps).  The point that stays still relative to the parent
      between two frames (the least-norm solution of c (Lprev - Lcur) = tcur -
      tprev, the axle's own direction held out, accepted when exact and the
      same as the frame before) becomes the transform's pivot (patch 0007
      `TransformGroup::pivot`: RT64 moves the pivot's world position along the
      line and turns about it; the game's frames are the matrices
      themselves).  RT64 turns a part along the shortest arc, so one turning
      over half a turn in a game frame would turn backwards in between: in
      the 60 levels (1900 frames, the summary's turn histogram) vehicle parts
      turn under 90 degrees a frame but for a few frames of 125-176 degrees
      (levels 1, 19, 29, 36, 37, 46: still under half a turn).  2043 parts
      got a pivot.  `--tag-test 32`: no pivots;
    - the game's camera snap counts as a cut when the camera really jumped
      (over 60 units or 15 degrees: the snap is also set every frame of the
      shuttle and map views, which move smoothly).
    - cost: the same time in `processDisplayLists` as `auto` (level 0: 2.36
      vs 2.38 ms per task paced at 60, 2.33 vs 2.31 at 144) and the same
      presents (60.00/s at 60 with vsync; 143.7 vs 143.9/s at 144 without,
      interval sd 0.34 vs 0.37 ms). (Those intervals were RT64's draw times;
      the uneven pacing at 60 with vsync they showed is fixed by the vsync
      pacing, under "Pacing" below.)
    - checked: per-frame traces at 60 (tagged) and 144 (tagged, counter
      detailed) identical to options off and to the branch's base bc.exe in
      all 60 levels (1900 frames), no crash.
  - `fps_counter` (`--fps-counter`): `off` (default), `on` (pictures
    presented a second, counted on RT64's present thread, and the game's own
    frames a second), `detailed` (also the mean and longest present interval
    and a graph of the last ones); `fps_counter_corner`: `top-left` (default:
    clear of the money, top right, and the radar, bottom left), `top-right`,
    `bottom-left`, `bottom-right`. The `fps_counter` key (F2) switches off ->
    on -> detailed for the run, `[controller] fps_counter` a held button
    combination (default none). Drawn with the menu's ImGui overlay (a frame
    built once a retrace, drawn at every present); off, none of it runs, and
    it only reads clocks.
  - `present_pacing` (`--present-pacing`; menu "Frame pacing"): `timer`
    (default, "Low latency"), `vsync` ("Smooth under load"), `vrr` ("G-Sync /
    FreeSync") or `auto`; see "Present pacing" and "Present scheduling" under
    Pacing below.  Applies at once.
  - `texture_pack` (`--texture-pack`, which also enables it) and
    `texture_pack_enabled` (default 0): an RT64 texture pack (a folder with
    `rt64.json`, or an `.rtz`; RT64's TEXTURE-PACKS.md) through RT64's
    `TextureCache::loadReplacementDirectory`; off = `clearReplacementDirectories`.
    Live from the menu (the main thread, which submits textures, as RT64
    requires).  `--texture-hashes F` logs the RT64 hashes (version 5) of the
    textures loaded at frame F, for pack makers.  Checked with a synthetic
    pack (every texture of level 0 replaced by a checkerboard, by
    `rt64.json` entries with auto-path `rt64`): replaced from the command line
    and when switched on in the menu, back to the originals when switched
    off.  No Blast Corps pack exists to try; the port's RDRAM keeps textures
    as big-endian bytes and RT64_NATIVE_HOST_LAYOUT loads TMEM without the
    XOR, so TMEM (and the hashes) should be the emulator's, but that is not
    checked against a pack made under an emulator.  The menu's Textures tab
    lists the packs in `packs\` next to bc.exe (folders with an rt64.json,
    `.rtz` files), reloads one ("Reload the pack": RT64's
    `loadReplacementDirectory` again) and, with `texture_pack_reload = 1`,
    reloads it by itself when its rt64.json (or .rtz) changes (checked once a
    second, between frames).
  - **Making texture packs** (dist/README.txt "Texture packs" has the
    player's how-to).  The texture dump (`texture_dump_enabled`, a folder in
    `texture_dump`; `--dump-textures DIR`; the Textures tab;
    `src/live/live_texdump.cpp`): every texture RT64 decodes is written once
    as `<where>/<hash>.png`, where = `frontend` or `level_NN` (the game mode
    when the dump first saw it), hash = RT64's own (TMEMHasher version 5,
    what rt64.json lists), plus `index.csv` (hash, file, format, TLUT, size,
    alpha class, where, first frame, how many times RT64 loaded it).  RT64's
    own dump facility (`State::dumpingTexturesDirectory`) writes raw TMEM and
    RDRAM files for its texture_hasher tool, on the render path; instead the
    main thread looks at RT64's `TextureCache::textureMap` each retrace
    (under its mutex, only when `globalVersion` moved): a slot holding a
    texture not seen there before is an upload, and a hash new to the session
    has its 4 KB TMEM copy (`Texture::bytesTMEM`), tile and TLUT mode queued.
    A worker thread decodes it to RGBA with `src/live/tmem_decode.cpp`, a
    byte-for-byte port of RT64's TextureDecoder.hlsli/Formats.hlsli (every
    value the shader writes is n/255 into R8G8B8A8_UNORM, so the bytes are
    RT64's), encodes PNG with RT64's miniz and writes it (folder already
    holding `<hash>.png` anywhere below: skipped).  Raw-TMEM textures
    (`decodeTMEM` off) are only listed.  No RT64 patch; nothing is written to
    N64 memory: traces equal with the dump on.  Checked: an identity pack made
    from the dump (same pictures, shift none) draws level 0 within 0.01 % of
    the pixels (mean difference 0.02-0.04 of 255: RT64 samples replaced
    textures without the N64's 1/128 coordinate rounding).
    `bc_packtool.exe` (`src/packtool/`, shipped in the package; RT64's
    stb_image and miniz): a folder of `<hash>.png` (any subfolders, any size)
    -> `packs\NAME\` with rt64.json (configuration version 3, hash version 5,
    autoPath rt64, every texture listed by hash and path, defaultOperation
    preload, defaultShift half; pictures at their original size get shift
    none: half would move them half a texel) and the pictures, and/or
    `packs\NAME.rtz` (stored zip).  Checks against the dump's index.csv:
    unknown hashes, sizes that aren't a whole multiple or change the shape,
    power-of-two originals made non-power-of-two, lost transparency (warnings);
    pictures identical to the dump's are left out.  `--mipmaps` writes RGBA8
    DDS with a full mipmap chain (2x2 box filter weighted by alpha);
    `--watch` rebuilds when a picture changes (rt64.json last, for
    `texture_pack_reload`).  `bc_texturetest.exe`: the decoder against
    hand-worked TMEM (each format, TLUTs, odd-row word swap, RGBA32 halves)
    and the packer on synthetic pictures (61 checks).
  - **The orbit camera** (`[camera] mode`, `--camera orbit`, the menu's
    Camera tab; `original` by default): a free camera that turns around the
    player's vehicle (or the driver on foot) at any angle, tilts and zooms,
    `src/live/live_orbit.cpp` with its math in `orbit_math.cpp` (tested by
    `bc_cameratest.exe`). **Render-only**: the game computes its own camera
    as always and uses it for everything it does; only what RT64 draws uses
    the orbit view, so the game's per-frame trace is the same as with the
    option off (checked: all 60 levels with the camera turning by itself,
    `--orbit-test`).
    - The game's camera: at the end of 00000.c cam_update (`PORT_ICUT_IF`,
      now every frame, `host_camera_hook`) the eye (from the view matrix the
      game just built, `DynamicBuf.unk140`) and the point it looks at
      (gCamAtX/Y/Z) are copied, read-only, for that DynamicBuf.
    - The view: RT64 patch `0006-view-hook.patch` adds `RT64::ViewHook`,
      called for every perspective projection of every rendered picture
      (in-between pictures of a higher frame rate too) after RT64's own
      interpolation; a projection whose view is the frame's game view gets the
      orbit view: the target and the game's eye interpolated with the
      picture's weight (so it moves smoothly at any frame rate), the orbit's
      direction and distance and the input since the last step (stepped every
      retrace). The projection (field of view, widescreen) is the game's.
      Cuts (the game's camera snap, a new mode or level) reset the orbit.
    - Only in normal play (game mode 4); the front end, the globe, the map
      and missile views, mission intros and the ending keep the game's camera.
    - Culling: the game's visibility walk (5FD50.c, one box test every other
      frame) only lists cells its own camera sees, and the list gates game
      logic too: 77E20.c obj_draw_all runs buildings' moving parts, falling
      parts (debris, random numbers, screen shake) and chasing objects only
      for listed objects. So the list is left alone: the pure drawing paths
      read `PORT_DRAW_CELLS(gVisibleCells)` instead (5FD50.c's level geometry
      and group lists; 77E20.c draws an unlisted building as it is, without
      any per-object work and without its moving parts), which with the orbit
      camera on in play is the game's list plus the cells the orbit camera
      can see: each cell's box (the game's own, from its grid of ground
      height ranges, grown by the collision triangles of the buildings in
      it) is tested against the orbit view (the game's projection, widescreen
      and interpolation width, its far plane), from the newest game camera
      (the level's lists are built before the frame's cam_update), with
      margins for what the view does until the next list: 20 degrees plus
      0.25 s of its recent turning speed, the apex moved back by 4 units
      plus 10 steps of the eye's recent motion; turning faster than 70
      degrees of margin covers (200 degrees a second), every cell (as before, and as the game
      itself does while its camera turns, cull_restart). The game logic
      reads the real list. The display lists then hold more (RDRAM differs
      only there). People and markers on unlisted cells stay hidden.
      `--orbit-log` checks every rendered picture's own view against its
      frame's list (cells it sees but the list left out: none in the 60
      levels, see below); `--orbit-cull all` draws every cell (testing).
    - The backdrop (2D810.c: a 2D quad scrolled by the camera's heading and
      pitch, the ground colour filled below its horizon) follows the orbit's
      heading and pitch: while RT64 reads the frame (`interpSetCommandHook`,
      live_interp.cpp's GBI wrapping), its 4 vertices get the orbit's values
      just for RT64's read and are put back at once, and the fill rectangle's
      top is changed in a copy of the command.
    - Collision: read-only copies of the game's height tests keep the eye 1
      unit above the level's ground triangles (62740.c ground_scan_grid's
      collision grid, gGroundCells) and the buildings' collision triangles
      (gCollTris, the 0x60-byte records); a blocked eye is pulled in along
      the line to the target at once and let out slowly. No game function is
      called. A step asks up to ~800 heights (the ray to the eye, and steeper
      rays to rise over an obstacle) while the camera is turned; testing all
      1700-2200 building triangles for each took 650-800 ms of every second
      on a Ryzen 7 5700U (the game slowed to 20 frames a second while
      turning), so the triangles are binned by x/z (`orbit::TriGrid`; the
      buildings' each step when they changed, each ground cell's list when
      its bytes changed): 15-30 ms a second, the same answers
      (`--orbit-log` compares every query with a test of every triangle).
      Blocked close to the target, the view first rises over the obstacle
      (a steeper view, up to 75 degrees more, at most `tilt_max`: the first
      that sees past it, else the one that sees farthest if that is at least
      15 % of the distance more) and, where easing up would meanwhile pull
      the eye in under 30 % of its distance, rises at once.  Before, it
      pulled in to 15 % while the rise eased in (and when no rise up to 50
      degrees saw past the obstacle): the eye ended up inside the player's
      vehicle or a bridge, and those pictures of big close-up polygons took
      the GPU up to 3 times as long (level 0, the camera low and turning:
      GPU per picture up to 11-12 ms against 4 on average at MSAA 8x on the
      integrated GPU here; 297-412 of 3100 steps pulled in under 30 %, now
      0-27).  The stress test (`--orbit-test sweep`, 50 s, MSAA 8x, intervals
      over 1.5x the target) at 120 frames a second: level 0 36 -> 3, level
      15 178 -> 102, level 29 245 -> 100 (the game's camera: 1, 39, 228); at
      60: 44 -> 8, 3 -> 2, 17 -> 13 (3, 3, 18); the slowest picture on the GPU
      11.5 -> 8.6, 10.4 -> 8.9, 31.2 -> 18.4 ms.
    - The kept view (settings version 2): `[camera] recentre` defaults to 0
      and nothing but `camera_reset` (and a new level: MODE_LEVEL_START or
      another level number, the zoom kept; switching the orbit off) changes
      the player's yaw, tilt or zoom: not the game's camera cuts (gCamSnap:
      respawns, the map view; mode changes), not getting in or out, not the
      pause, map view, message windows and other modes outside play (the
      orbit is inactive there and its state is kept), not the collision (it
      changes only its own pull-in / rise / lift).  Before (76dd727), the
      recentre ease (default on, 1.5 s) brought the view back behind the
      player after every turn, each cut and every return to play (game mode
      != 4) reset the yaw and tilt, and the smooth collision's pull-in
      changed the distance near buildings (what looked like a forced zoom);
      the zoom value itself was only reset by `camera_reset`, whose default
      (the right stick's click) a hard push on the camera stick can press:
      a click while the camera stick is pushed over half way is now ignored
      (counted in the summary).  The frame (`[camera] follow`,
      `orbit::Limits::follow`, default 1): the kept yaw and tilt are offsets
      from the game camera's (which chases the vehicle's heading with its
      own lag; on foot it doesn't turn with the driver), so driving a full
      circle the view stays at the player's angle to the vehicle, like a
      chase camera with an offset; follow = 0 keeps world angles (the old
      behaviour without recentring: driving a circle, the view swings round
      to the front).  The tilt offset is limited by `tilt_min/max` where the
      view is made, the offset kept.  `--orbit-test set:YAW[:PITCH[:ZOOM]]`
      puts the view there once (then no camera input): level 0 through the
      intro walk, getting in, getting out, the map view (Start) and back, the
      offset stayed 90.0, the zoom 1.60 (5 cuts, no reset).
    - Collision comfort (`[camera] collision`, `orbit::Collision`, default
      `off` since settings version 2 (before: `smooth`); `strict` is the
      behaviour above, `off` never moves for obstacles, only keeps the eye
      above the terrain grid: `clearance` over the highest terrain of a ring
      around the eye (`offRing`, 10 % of the distance, 2-12 units) and of
      where it is going (its last step's move 3-18 steps on), reached on the
      `springIn` spring, and never under the terrain right under it
      (`floorClear` 1 unit, at once).  Measured (3600 frames, off / smooth;
      "snaps": a step where the lift moved the picture's eye up over 3 units
      out of a much slower step): sweep L0 3/0, L3 0/2, L19 0/1, L29 4/3;
      `hold:120:15` 0/1, 0/1, 0/0, 0/0.  The eye inside a building (off;
      the height query's buildings, roofs up to 40 units above the eye):
      sweep L0 219 of 5106 steps (4 %), L3 131/2364 (6 %), L19 4/4862, L29
      76/2284 (3 %); hold:120 L0 24/5106, L3 180/2364 (8 %), L19 0, L29 0.
      A near-plane nudge or fading the building in the way would need the
      renderer (an RT64 patch) and wasn't done.  The view along
      the line of sight and a ray each side (`rayOffset` of the distance
      beside the eye; the larger of the line of sight's and the three rays'
      median: a pole on the line of sight is ignored, a clear line of sight
      between two buildings is clear) must be blocked for `holdIn` (0.15 s)
      before the camera rises (lowest 5-degree step that sees past) or pulls
      in (to 97 % of the free part along the view the rise is going to), and
      clear by `hysteresis` (8 %) for `holdOut` (0.8 s) before it goes back
      (the rise straight to the lowest step that sees past); positions follow
      critically damped springs (`orbit::Spring`, 0.15 s in/up, 0.9 s
      out/down).  Only the eye itself inside an obstacle (a point test) moves
      it at once, on a 0.05 s spring, to the obstacle's near side along the
      line (or hurries a rise); the lift above the ground is immediate.  When
      the target itself is blocked (the player pushing into a building) it
      doesn't pull in at all: that would only put the eye into the vehicle.
      `--orbit-steplog` logs every step.  The summary line `orbit collision` counts turning
      points of the pull-in and the rise (moves of 3 % / 1.5 degrees one way
      after the other), the fastest moves and the steps with the eye inside an
      obstacle (`orbit::CollisionStats`).  Measured (3600 frames, the
      levels' driving input; strict = before -> smooth), with the stress
      sweep: pull-in / rise turning points level 0 25/39 -> 11/23, level 3
      33/33 -> 2/14, level 19 9/7 -> 7/7, level 29 9/11 -> 5/7; fastest
      pull-in 30-42 -> 1.5-7 shares of the distance a second, fastest rise
      276-1472 -> 60-181 degrees a second; with the view held 120 degrees
      round (`--orbit-test hold:120:15`, as when driving with a turned
      view): level 0 6/15 -> 2/13, level 3 19/31 -> 3/9, level 29 0/3 ->
      0/1, fastest pull-in 43-51 -> 3-6, rise 600-2334 -> 49-361.
    - Camera-relative steering (`[camera] relative_steering`, default 1): the
      game steers the driver on foot (vehicle 0), Thunderfist (2), the J-Bomb
      (9) and the Cyclone Suit (16) relative to its camera (45BB0.c
      cont_update, control method 0, calls 46F60.c cont_stick_to_steer, which
      adds gCamHeading to the stick's angle) unless the player picked the
      other control method for it (gPlayerControlModes[playerNumber], a bit
      per vehicle type); every other vehicle reads the stick as steering and
      throttle (cont_process_stick).  For those characters, in play
      (gGameMode 4, not paused), while the orbit shows a turned view, the
      stick of each controller read (`host_input_hook` in plat_input_read:
      the player's input and an `--input` file alike) is turned by the
      difference between the game's view and the orbit's
      (`orbit::steerTurn`, `orbit::rotateStick`, length kept).  This is the
      one place the orbit camera changes the game's input, by design; with the
      orbit off, not turned, or no stick held, the game gets the input as
      before (traces equal).  `recentre_on_foot` now only applies with
      relative steering off (then for all four characters).  Checked with
      `--orbit-test hold:DEG[:PITCH]` (the orbit kept DEG from the game's
      camera) and scripted input: level 0 on foot, held 90 degrees round,
      stick up: the driver walks 2 degrees off "away from the orbit's eye"
      (94 with relative steering off).
    - Analog throttle (`[controller] analog_throttle`, default 0; `throttle`
      = righttrigger, `throttle_deadzone`, `throttle_curve`; live_input.h,
      live_throttle.cpp).  How the game drives (45BB0.c cont_update, 62740.c
      veh_throttle): no vehicle reads A itself; in the digital control method
      (mode 0) `cont_process_stick(0, 0)` zeroes the stick's y and makes A
      +80 / B -80 (`cont_buttons_to_stick`), and the "SPEED ON 3D STICK"
      method (mode 1, `gPlayerControlModes` bit) keeps the stick's y (A / B
      still +-80).  `veh_throttle` (every throttle vehicle: Sideswipe 1,
      Skyfall 3, Ramdozer 4, Backlash 5, train 7, American Dream 8, Ballista
      10, barges 11/17/18, police 13, A-Team 14, Starski 15) adds the
      vehicle's band acceleration times traction each frame while the speed
      is under bands[13] * gPadStickY / 80, B brakes by `delta` then reverses
      up to bands[0] * y / -80, Z (`gHeldZ`) brakes to 0 (and at 0 gets out).
      So the stick's y is already an analog top speed; the acceleration
      below it doesn't change.  The driver on foot, Thunderfist, the J-Bomb
      and the Cyclone Suit steer and move by the stick (360 mode: the
      stick's length is their speed), the Crane's A/B move its arm: the
      trigger is the A button for them (from 96/255), and everywhere outside
      play.  The hook: each read (plat_input_read: the player's pad, or an
      `--input` line's optional 4th value 0..255) the trigger's pressure goes
      into the raw pad (`throttleEncode`): digital method, A and the raw y =
      80 t (the game ignores the raw y there); analog method, y = max(stick,
      80 t).  `PORT_THROTTLE` (45BB0.c, after `cont_process_stick(0, 0)`,
      NON_MATCHING/PORT_HOST) puts the raw y in place of A's 80 for a read
      this code encoded (in replays after a level, if the level's play had
      encoded reads; never in the attract demos).  The raw pad is what
      `rec_record` keeps, so the level's replay gets the pressure back.  The
      trigger bound to the throttle presses no other N64 button (Z stays on
      the left trigger).  Fidelity (`--throttle-log`: every read's speed,
      gLastSpeed, and position, gCamAtX/Y/Z): with the trigger fully pressed
      the run equals the A button's, released equals nothing pressed (levels
      0, 1, 5: identical speed and position at every read); with the option
      off nothing is encoded (traces equal main).  Measured (top speed in
      the game's units, `orbit5_thr` runs, held 700 reads from the start):
      Backlash (level 1) 79 / 118 / 173 / 228 at 25 / 50 / 75 / 100 %,
      Skyfall (level 5) 78 / 145 / 195 / 258, Ramdozer (level 0) - / 141 /
      195 / 258 (at 25 % it met a building first); the gain in the first
      frames is the same at every pressure until the lower top speed is
      reached.  Side effects of a partial y the game has in its own analog
      method too: the Ballista's hop needs y >= 60, a camera height uses y.
    - The sky: the backdrop's texture follows the heading in whole 1/4096
      turns and only once a game frame; in the orbit view its vertices get
      the view's heading in fractions, unwrapped, and its G_VTX an
      interpolation ID of its own (texture coordinates and vertices
      interpolated; `rsp.matrixId` set just for that G_VTX and put back), so
      RT64 moves it picture by picture.  A cut, engaging the orbit or taking a
      texture repeat off the s16 values gets a new ID (no blend).
    - Controls: `[controller] camera_left/right/up/down` (default the right
      stick), `camera_zoom_hold` (Y: held, look up/down zoom in/out instead),
      `camera_zoom_in` / `camera_zoom_out` (buttons that zoom while held;
      default none: the old default, Y and the left stick's click, which a
      hard push on the stick could click while steering, is changed to the
      new one when a bc.ini still has exactly it), `camera_reset` (right stick
      click); `[keyboard]` keypad 4/6/8/2, + and -,
      5; the mouse (`[camera] mouse`): hold the left or right button and move
      to turn, the wheel zooms, the middle button resets. In play the camera's
      inputs don't reach the N64 pad (by default the right stick was the C
      buttons: C-left/C-right are the game's own 45-degree camera turns,
      00000.c cam_turn_heading, and C-up/C-down do nothing in play; they stay on
      the keyboard's J/L/I/K, and the right stick is the C buttons again in
      menus).
    - Settings: `sensitivity_x/y` (percent), `invert_x/y`, `follow` (the
      kept view relative to the game's camera, default 1, or the world),
      `recentre` (ease back behind the player after `recentre_delay` seconds
      without camera input; default 0: the view stays until reset),
      `relative_steering`, `recentre_on_foot` (with relative steering off:
      ease back for the camera-relative characters anyway), `collision`,
      `zoom_min/max` (times the game's distance), `tilt_min/max` (degrees),
      `mouse`.  The camera stick uses `[controller] deadzone` (per axis, as
      the N64 stick) and a curve (0.6 v|v| + 0.4 v); its input is one amount
      per step (1/60 s of the game's virtual time, `orbitVi`): a stall of
      the main thread doesn't turn or zoom it further.
    - Billboards: smoke, explosions and particles (60F60.c, 43A60.c) are
      quads the game turns toward its camera: a uniform scale, a turn about
      x by the pitch from the billboard to the game's eye, a turn about y by
      the camera's heading.  After each G_MTX RT64 runs (the matrix hook of
      `interpSetCommandHook`), a matrix built that way (rows of equal length
      at right angles, x level and along the game camera's right, the normal
      pitched to the game's eye) is rebuilt the same way for the orbit's eye
      in RT64's copy (`billboardFace`); any other matrix whose rotation is
      about the inverse of the game view's (up to its scale) is turned by
      Rg Ro^T.  (Before, only the second test was made: billboards away from
      the middle of the game's picture, tilted by up to half its field of
      view, failed it and were seen edge-on from the orbit camera's side, as
      thin streaks: "stretched" smoke and explosions.)
    - The target marker (30C70.c hud_draw_target_marker: four bouncing
      arrows round the next target, 2D, placed with the game's view and
      projection): after RT64 read their vertices (16 at DynamicBuf +
      0x1F00), its copies are moved by where the orbit's view shows the
      middle of each side of the target's box minus where the game's showed
      it (the target's points: `gMarkedTargetPts`, else `gNearestTargetPts`,
      whichever matches the drawn arrows); a target behind the orbit's eye
      hides them.  (Before, they stayed where the game's camera saw the
      target and floated over whatever the orbit showed there.)  Between
      game frames RT64 blends the two frames' arrows (tagged, and with
      `interpolation = auto` too: live_interp.cpp); `--orbit-log` measures
      them against each picture's own view (`markerCheck`, the summary's
      "target marker check" line).
    - Known limits: the screen shake isn't applied to the orbit view;
      people sprites (world-space quads built on the CPU) face the game's
      camera; the radar keeps the game's heading; with the game's own camera
      the backdrop still moves once a game frame (its sky is rarely in view);
      environment-mapped reflections use the game's view.
    - Testing: `--orbit-test YAWSPEED[:PITCH[:ZOOM]]` turns the camera by
      itself (degrees a second, an absolute pitch, a zoom); `--orbit-test
      sweep` is the stress test (`orbit::sweepAt`: turning both ways up to
      about 210 degrees a second with a 320 flick every 13 s, tilting over
      the tilt range, zooming over the zoom range, the same every run);
      `--orbit-log` logs
      both cameras every 30 frames and runs the checks above, `--orbit-cull
      all` draws every cell, `--perf-log` has the orbit's host work,
      `--view-log FILE` every picture's view (when it was drawn, its frame and
      weight, the heading and pitch; with the game's own camera, every
      projection's view as RT64 interpolated it); `--orbit-repeatable` is
      kept for old scripts (pictures are always repeatable now, below).
    - The view at each picture's own time: the orbit after every step (once a
      retrace, the game's virtual time) is kept for 64 steps; a picture with
      interpolation weight w between frames made after steps a and b shows
      the orbit at step a + w (b - a), between the two steps around it
      (`Orbit::between`: the yaw along the shorter arc, the zoom
      geometrically, the pitch, blend and the collision's pull-in, lift and
      rise linearly), as the game's camera and the world are interpolated by
      the same weight.  Before, a picture took the newest step plus the
      stick's rate times the time since it, read when RT64's render thread
      drew the picture; RT64 draws a frame's in-between pictures together,
      ahead of their presents, so the orbit view turned in 30 Hz steps while
      the world was smooth (level 0, a constant 120 degrees a second, heading
      step per picture: at 120 frames a second 2.73 / 0.41 / 0.47 / 0.44
      degrees by the picture's place in the frame instead of 1.00, at 60
      3.61 / 0.39 instead of 2.00; now 1.00 and 2.000, sd/mean 0.018 from
      0.815 at 60).  The input, collision and rise are still worked out once
      a step; mouse moves are spread over the pictures between steps.  The
      view is as far behind the input as the game's own interpolated camera
      (a picture shows the time between the frame before and its own).
    - Performance (the frame-time report below has the orbit's own lines):
      its host work is small: the step with the collision 0.25-0.35 ms a
      retrace, the drawing list 0.02 ms (one a game frame), the frame setup
      (backdrop, billboards) 0.003 ms, RT64's render thread 0.002 ms a
      picture in the view hook; in the stress test on levels 0, 15 and 29 at
      60, 120 and 240 frames a second it never made a retrace late.  What
      the orbit view costs is GPU time: the same scene seen low and across
      the level takes more per picture than the game's steep view (level 0
      at MSAA 8x: 4.2 against 3.7 ms on average), and close-ups took up to
      3 times as long (the collision above).  Drawing fewer cells doesn't
      help: every cell (`--orbit-cull all`) costs 2 % more, cells farther
      than 1500-3000 units left out (tried) nothing measurable; MSAA 8x
      costs 1.8 times MSAA off.  The orbit view brings no materials of its
      own in play (the same new shaders as the game's camera).
  - Controls (`[keyboard]`, `[controller]`): the menu's Controls tab remaps
    every binding (click it, press the key, controller button, stick or
    trigger; Backspace clears; the menu's own controller buttons are a
    combination held together), sets the analog stick (left/right/none),
    its dead zone and `sensitivity` (percent of the N64's full range at full
    tilt; 100 = before), picks the controller (`pad = 1` the first one found,
    the default, `N`, or `any` = all at once) and resets to the defaults.
    Every SDL game controller is opened (`src/live/live_input.cpp`,
    `live::PadSet`): about every 16 ms detached ones are closed and new ones
    opened, so a controller that drops out and reconnects (a new SDL instance
    id) plays again as `pad = 1`.  SDL's controllers run on a thread of their
    own (`live::InputThread`): SDL 2.26's device detection - DirectInput's
    device enumeration and XInput's after any HID device arrives or leaves
    (and again 300 ms and 2 s later), HIDAPI's and Raw Input's opening of new
    devices - ran inside the main thread's `SDL_PollEvent` (SDL_JoystickUpdate
    with SDL's controller lock held, and its device window's messages), and
    a user's log showed 50-400 ms pauses there (an Xbox Elite 2, maybe over
    Bluetooth, connecting; more in play and on the level-complete screen).
    The thread starts SDL's game controller subsystem itself (its device
    windows are its own), waits for messages or 1 ms, dispatches them, calls
    `SDL_JoystickUpdate`, rescans and samples every controller into plain
    data (`PadState`); the main thread (`SDL_HINT_AUTO_UPDATE_JOYSTICKS` 0)
    copies the newest sample once a retrace and never calls SDL's controller
    functions.  SDL's controller events still reach its queue (the menu, the
    menu buttons; a button event newer than the sample is applied to it).
    bc_inputtest: a 300 ms update on the thread with SDL's controller lock
    held, the main thread's reads all under 0.02 ms.
    Controllers keep working when the window loses the focus
    (`SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS`: SDL's default drops their
    input then, and with the keyboard also gone a player in fullscreen had
    no input at all after an unseen focus steal, e.g. the Xbox button's Game
    Bar); in fullscreen a controller button pressed while unfocused also
    raises the window to get the keyboard back.  bc.log records controllers
    added/removed (name, SDL id, frame), focus lost/gained,
    minimise/restore, the menu opening/closing and the input gate
    closing/opening (with what held it).  `bc_inputtest.exe` (built with
    bc.exe; SDL virtual controllers, no window or ROM) tests the pad
    mapping, the menu combination, the gate (held, released, stuck) and
    unplug/replug with `pad = 1`, `2` and `any`.  The game is
    one-player: the platform reports only controller 1 (with the Controller
    Pak); the ROM has no Rumble Pak code (no osMotor calls), so there is
    nothing to map for rumble or other pads.
- Pacing: each retrace's picture is handed to RT64 when its virtual time is
  due (60 per second of real time, QueryPerformanceCounter, 1 ms timer
  resolution), *before* the game works on the next retrace, so the hand-overs
  are regular; vsync (`vsync = 1`, plume's swap chain) then shows it at the
  next refresh.  `--pace-log FILE` writes the hand-over and present times
  and logs a summary at exit.  Measured on this PC (60 Hz display, AMD
  integrated GPU, 1500 retraces of attract mode): 60.000 retraces/s; hand-over
  interval sd 0.6-2 ms (was 7-8 ms when the wait came after the hand-over),
  97-99.7 % within 2 ms of 16.67 ms; the misses are the first second of demo
  0 (level start: up to 50-77 ms behind with D3D12 while shaders compile,
  ~14 ms with Vulkan), then a catch-up.  Same per-frame trace with vsync
  on/off, windowed/fullscreen, D3D12/Vulkan.
- **Present pacing** (`[video] present_pacing = timer | vsync | vrr | auto` in
  bc.ini, the settings menu's Video tab "Frame pacing", `--present-pacing P`
  for one run; `live_rt64.cpp` `applyPresentPacing`; vrr and auto: "Present
  scheduling" below): how pictures are handed
  to the display.  **`timer` is the default**: RT64's own present timer, the
  lowest latency, and on an idle PC as even as the other (numbers below).
  `vsync` ("Smooth under load") is the vsync pacing below: even while the PC
  is busy, for about 5-10 ms more latency.  The menu switches it at once
  (the present thread reads `PresentQueue::vsyncPacing`, an atomic, at every
  picture; switching is the same as the patch's own fallback to RT64's timer
  and back).  It needs vsync on and D3D12 (the menu greys it out otherwise);
  `--no-pace` always uses RT64's own pacing.  A bc.ini from before the key
  existed reads as `timer` (`bc_inputtest` checks that, and the menu's change
  saved and read back).
- **Vsync pacing** (`present_pacing = vsync`; RT64 patch
  `rt64/patches/0005-vsync-pacing.patch`,
  `PresentQueue::holdFor` / `waitForHold` / `dropToCatchUp` / `noteStruggle`):
  only how pictures reach the display changes; the game, its virtual clock
  and the hand-overs above are untouched.
  - The problem: RT64's present thread handed the swap chain a picture every
    1/rate s on its own timer, and plume's D3D12 swap chain presents with a
    sync interval of 0 even with vsync on (it uses present wait), so the
    display showed whichever picture was newest at each refresh.  At a
    target rate equal to the display's the timer beat against the refreshes,
    and RT64's present thread fell behind (its lag crept from 10 to 45 ms),
    after which its workload queue rendered one picture instead of two for
    some game frames: measured with DXGI frame statistics (`--flip-log`), at
    `framerate = 60` on a 60 Hz display 10-18 % of the shown intervals were
    33 ms instead of 16.7.
  - With vsync pacing (and vsync on): every picture is presented with a sync interval of 1
    (never dropped by the display) and held for the refreshes the game's
    timeline gives it: an in-between picture one refresh at the display's
    rate (N at 1/N of it; other target rates keep RT64's timer); a game
    picture the refreshes between its retrace's due time (`liveVi` hands it
    over, `State::presentDueTime`) and the next one's, less its in-between
    pictures' (2 in play, 1 or 2 in the front end; `original` included).  A
    hold over one refresh waits for the middle of the refresh before the due
    one, counted from when the picture before was really shown (DXGI frame
    statistics), so the phase of the game's retraces against the display's
    doesn't matter.  Drift between the two clocks changes a hold only when the
    timeline is 3/4 of a refresh off.  Latency is kept low: an in-between
    picture that would queue behind another and show a refresh later (after
    it was queued) than the lowest such latency of the last seconds (what
    windowed or fullscreen gives) is dropped once; a wait that stayed over a refresh
    too long for 30 pictures is shortened once.  When the PC can't keep up
    (six pictures dropped or presented over five refreshes after they were
    queued, within 2 s: a busy GPU or CPU; a game thread that was late
    itself, e.g. loading, doesn't count) RT64's own pacing takes over for
    5 s.  RT64's workload queue renders fewer in-between pictures
    (`frameReduction`) only after waiting over half a display frame for the
    present thread to let go of a target: with vsync pacing that happens on
    a refresh, and any wait used to make every third game frame one picture
    short.  D3D12 only: Vulkan's FIFO swap chain never drops a picture, and
    RT64's timer feeding it measured regular (present intervals sd 0.03-0.09
    ms at 60 on the 60 Hz display; the vsync pacing there made them bursty,
    with no frame statistics to check the display), so it keeps RT64's
    pacing.  Vsync off,
    `--no-pace` or `present_pacing = timer` (the default): RT64's own pacing.
  - Measured (this PC: 60 Hz display, AMD integrated GPU, D3D12, level 0
    frames 1150-1450, runs whose game hand-overs were regular): windowed
    `original` 0-0.3 % of the shown intervals off their due length (was
    0-40 %, run to run), `60` 594 of 594 pictures one refresh each in all 5
    runs, `display` 593/594 (was 1-8 % two-refresh holds);
    fullscreen `original` 0-3 % (was 15-29 %).  Those runs had other work
    (builds, emulator runs) on the PC; with the PC idle both pacings are
    near perfect (windowed `60` 595/595 and 591-596/598 one-refresh holds,
    `original` 297/297 and 298/298; fullscreen `60` 573-595/595 and 592-598/598,
    the same at internal resolution 1, so not GPU-bound): the vsync pacing
    keeps it so when the PC is busy.  Its cost is latency from a retrace's
    due time to the display: about half to one refresh more (idle: `original`
    17-18 ms vs 7, `60` 34-44 vs 30-37; fullscreen `60` 40-46 vs 32-43).
    That trade-off is why `timer` became the default: on an idle PC timer
    pacing is as even (`original` 298/298, `60` 596/596, fullscreen `60`
    592-598/598) with 5-10 ms less latency; vsync pacing (`original`
    297/297, `60` 595/595, fullscreen `60` 573-595/595) is the choice for a
    PC that is busy with other work while the game runs.
  - `--flip-log FILE` (testing): every present RT64 made (submission, call,
    hold) and, with D3D12, every refresh and the present on screen (DXGI
    frame statistics, polled every millisecond), as CSV.
  - `--perf-log FILE` (testing): twice a second, as CSV, the game's frames,
    RT64's rendered pictures and their GPU time (RT64's own timestamp
    queries), its render thread's time per workload, the main thread's time
    in processDisplayLists and waiting for the paced retrace, the orbit
    camera's host work, and the frame workloads' draw calls and triangles.
    `--pace-from F` (testing) paces only from game frame F, so a measured
    run gets to play sooner.
- **Present scheduling** (RT64 patch `rt64/patches/0007-pacing-and-pivots.patch`,
  `PresentQueue::scheduling`; `live_rt64.cpp` `applyPresentPacing`): when the
  vsync pacing above is off, how the pictures between the game's are spaced.
  RT64's own timer slept 1/target s from the last present call (every late
  wake-up and every wait in the swap chain delayed all the pictures after it)
  and, at a target equal to the display's rate with vsync on, beat against
  the refreshes (a picture replaced before it was shown, the one before held
  two refreshes).  Now, with `present_pacing = timer` (the default):
  - vsync on, D3D12, a target that divides the display's rate (`framerate =
    display`, 60 on 120 Hz): no timer; each picture is presented with a sync
    interval of display / target and the latency-1 frame-latency wait lets one
    picture queue: one picture per refresh;
  - otherwise an even timer: each due time one period after the last due
    time; a picture not ready by then is presented when it is and the timer
    goes on from there (no burst to catch up).
  `present_pacing = vrr` (G-Sync / FreeSync): the even timer at most 97 % of
  the display's rate (`kVrrMargin`: 116 on 120 Hz, 139 on 144, 232 on 240;
  `configToRT64` caps the target), presented with a sync interval of 1 when
  vsync is on (a variable-refresh display refreshes when the picture comes)
  or 0 with tearing allowed when it is off.  `present_pacing = auto`: vsync off
  -> timer; vsync on -> vsync pacing at the game's own rate, timer (locked)
  above it; Windows says nothing about G-Sync, so auto never picks vrr.  With
  any scheduling but RT64's own, the workload queue's `frameReduction` (one
  in-between picture fewer when it waited for the present thread) counts only
  waits over half a picture, as the vsync pacing does.  `--present-legacy`
  (testing): RT64's own timer.  Measured on this PC (60 Hz, level 0, frames
  1100-1700 paced, windowed, idle): `framerate = 60` with vsync, RT64's timer
  1195-1198 of ~1199 pictures one refresh and 1-2 held two (3 runs; in earlier
  runs up to 9 reduced pictures and 5 intervals over 1.5x), locked 1199/1199
  in all 3 (no reduced pictures); the vsync pacing and auto the same.  Without
  vsync (`--display-hz`) at 120 / 144 / 240 both timers kept the target
  (sd 0.05-0.3 ms; the outliers were the integrated GPU rendering late), and
  vrr at 116 a second sd 0.05-0.6 ms against RT64's timer's 0.1-1.3.
- **Frame times** (always measured; `live_frametimes.cpp` `framePresented` /
  `frameLog`): every swap-chain present (the patch's `presentedHook`) goes
  into a histogram of present-to-present intervals (0.05 ms bins) against the
  target (RT64's target rate with in-between pictures, else the game's rate);
  the main thread's time per retrace is split into the game (and the host's
  per-frame work), reading the display lists, RT64's `updateScreen` and the
  pacing wait; the GPU time per rendered picture comes from RT64's timestamp
  queries (`rendererGPUProfiler`); with D3D12 the DXGI frame statistics after
  each present show whether the refreshes followed the presents (refreshes
  over 1.2 times the nominal period: variable refresh seen) and how many
  refreshes each picture stayed on screen and how many presents were replaced
  before they were shown (sampled once a present).  bc.log gets
  `live: frame times (10 s)` lines and, per settings segment (a video setting
  changed, F11, `--pace-from`) and at exit, a block: presents, mean, sd,
  p50/p95/p99/max, over 1.5x and 2x the target; CPU and GPU ms per picture;
  display Hz, RT64's target, the pacing, vsync, fullscreen, tearing support,
  window and internal size, MSAA (the segment's own settings, kept while it
  ran: the change that ends a segment is applied before its block is
  written), refresh statistics, sync intervals used; RT64's in-between
  pictures not drawn (reduced, rendering late, cut), presents skipped, timer
  restarts.  Intervals over 250 ms (loads) and the menu's pictures are left
  out.  The F2 counter's detailed mode shows the last 240 intervals, the
  target and the stutters (over 1.5x) since it was turned on.
  - **Content times** (RT64 patch 0008): every present says which workload
    the picture was rendered from and its interpolation weight; a picture
    shows the virtual time of the frame before (the retrace its display list
    came after) plus its weight of the way to its own frame's.  The `content`
    line (10 s and per segment): the content steps per picture (mean, sd,
    even = within 10 % of one period, repeated, skipped), the present calls'
    intervals less the content steps (sd), and on the display (DXGI frame
    statistics, consecutive pictures each seen at the refresh it came on) the
    refresh intervals less the content steps: what the display really showed
    against what it should have.  `--picture-log FILE` (testing) writes every
    present with its interval, content time, weight and workload.
  - **Long retraces** (over 50 ms): a line each (the first 40, then every
    20th) with where the time went: the game's step split into PI DMAs (with
    the load-time byte swaps), audio (tasks and the output), gfx tasks (RT64
    reading the display lists), save files written and the rest, the game's
    own code with its inflate (`plat_host.h` `host_prof`); RT64's submit; the
    host's work after the retrace (the events: SDL's pump - the window's
    messages - and the handlers of window, keyboard, mouse, controller and
    other events, reading the pad state; the orbit camera's step; and the
    controllers' own thread's longest update meanwhile, which no longer holds
    the game up).
  - **Textures**: every 10 s, the textures RT64 made, by size (its texture
    map's creation frames, sampled every other retrace: RT64 evicts a texture
    unused for a few frames, and the hash map's growth in `spikes by cause`
    counts one each time it comes back).  Mid-level, about 20-50 a second,
    mostly 32x32, evicted again within frames: small uploads, never a burst
    of 8 in a retrace (no spike put down to them in the runs here).
  - **Spikes by cause**: each retrace keeps a record (the main thread's loop
    since the retrace before, less its pacing wait, split into the game, the
    display lists, the orbit camera and RT64's `updateScreen`; whether its
    frame task found RT64's workload queue full; the longest picture on the
    GPU and the longest RT64 workload finished since; the shaders RT64 was
    given to compile and the textures it added).  Every interval over 1.5x
    the target is put down, once the retraces around it are in, to the
    first cause that applies over its retraces (`orbit::spikeCause`, tested
    by `bc_cameratest`): a retrace over 1.5 periods late (by its largest
    part, "other" being the rest of the loop: the host's work after the
    retrace or time the thread didn't run; it used to fall to "game";
    display-list time with the queue full is the render thread's, or
    the GPU's when a picture took over 3/4 of the target), a picture over
    3/4 of the target on the GPU (the present's own GPU work comes on top),
    an RT64 workload longer than the game frame, new shaders (RT64 draws a
    new material with its ubershader until the shader is compiled on its
    idle-priority threads: 1.8 times the GPU time, `--ubershaders-only`),
    a burst of 8 or more new textures, else the present side.  The 10 s
    lines and each segment's block get `spikes by cause` (with the
    retraces missed, the longest RT64 workload, new shaders and textures)
    and, with the orbit camera on, its work per retrace (step, collision
    queries, drawing lists made and with every cell, frame setup, the view
    hook per picture, grids made again, steps pulled in close to the
    player).  `--spike-log FILE` (testing) writes every spike with its
    window as CSV.
- **Host memory below 4 GB** (`plat_host.c` `host_low_reserve` /
  `host_low_alloc`): the widescreen ticker's vertices and glyphs live in host
  memory RT64 reads through its extended RDRAM addresses (an address with bit
  31 set is an offset from RDRAM's base, i.e. the host pointer: between RDRAM's
  end and 4 GB).  1 MB there is reserved first thing in bc.exe's main, before
  SDL, RT64 and the graphics driver, which on a PC with a big graphics card
  (RTX 5080, 4K, MSAA 8x) had filled that range by the first ticker screen
  (the ticker was then drawn 4:3 wide and the failure logged every frame).
  At 0x81000000 (or 0x90000000, 0xA0000000, 0xC0000000), else the first free
  range a VirtualQuery search finds.  A failure is logged once, and the frames
  drawn 4:3 for want of it counted at exit.  `--low-test MASK` (testing): 1
  crowds [0x80800000, 4 GB) before the reservation (a 1 MB hole left at
  0xE7300000: the search finds it), 2 after it (as the driver does: the ticker
  still works), 4 skips the reservation (with 2: the user's failure, once).
- **The package** (CMake target `dist`, `dist/make_dist.py`; from WSL
  `make -C port dist` -> `port/build/dist/`): `BlastCorps-port-<hash>/` and
  its zip: bc.exe, SDL2.dll, dxcompiler.dll, dxil.dll, `README.txt` (from
  `dist/README.txt`: running, controls, bc.ini, saves, known issues),
  `LICENSE.txt` (the repository's MIT `LICENSE`) and `THIRD_PARTY_LICENSES.txt` (every licence from the trees the build used:
  RT64 and its contrib code, DXC, SDL2; it fails if one is missing), plus
  `BlastCorps-port-<hash>-pdb.zip` (bc.pdb, map, syms).  No ROM data.  The
  exe's icon is drawn by `tools/make_icon.py`; `res/bc.rc` adds the version
  resource (git hash) and a manifest.

### Audio (`src/audio/`)

The game's audio thread (22EE0.c, Rare's audiomgr) builds a command list
each audio frame with libaudio (the old-SDK synthesizer, compiled natively
from the ul_* objects) and starts an M_AUDTASK with the ROM's audio
microcode (gAspMainText, the "Blast Corps / Diddy Kong Racing" variant of
aspMain).  `src/audio/aspmain.c` executes that command list as the RSP
would: a model of DMEM (the microcode's map: segment table 0x320,
parameters 0x360, command buffer 0x380, ADPCM table 0x4C0, sample buffers
from 0x5C0, state scratch 0xF90), SP DMA with its 8-byte alignment, and
each command's vector-unit arithmetic (accumulator width, saturation,
rounding, vector flags) as the microcode does it.  The constant tables
(resampler filter, masks, ramps) come from the task's ucode data at run
time, read from the ROM's data image; nothing ROM-derived is in the source.
`port_audio.c` connects it to the platform: osSpTaskStartGo runs the task,
osAiSetNextBuffer hands the finished buffer to the output.

RDRAM layout at the interpreter's boundary (the bus in port_audio.c): the
command list is host-order u32 words (like display lists); A_LOADADPCM's
codebooks / pole-filter coefficients and A_SETLOOP's loop states are
host-order s16 (the load layer swaps them in the sound banks; libaudio
computes the coefficients and copies the loop state natively); everything
else the microcode moves stays in N64 byte order: ROM sample data, the
microcode's own state blocks (ADPCM, resampler, envelope mixer, pole
filter), reverb delay lines and the output buffers, which no C code reads.
The AI plays big-endian L/R pairs; the output converts them.

Outputs: `bc_headless --wav FILE` (the stream the game hands the AI, plus
`FILE.frames`: game frame -> sample offset; `--wav-all` also keeps buffers
a full AI FIFO drops, i.e. every task's output), deterministic; bc.exe queues
the same buffers to SDL audio at the AI rate (osViClock / dacrate, 22047 Hz)
with a little resampling (at most 0.5%) holding the device queue near
60 ms against drift between the paced virtual clock and the sound card; a
queue past 0.3 s (after a stall) is dropped.  `--mute`, `--volume N`, keys
M (mute), - and = (volume).  `--no-audio` skips the audio tasks (the game
logic is the same either way: bc.exe and bc_headless give identical traces
and identical WAVs).

Verification (`tools/audio/`, Linux tools; the LLE RSP is cxd4,
CC0-licensed, from `~/thirdparty/ref`, loaded at run time as a test oracle,
not shipped or copied):
- `rsp_tap.so`, an RSP plugin for mupen64plus: every audio task runs on the
  LLE RSP (the ROM's real microcode) and on aspmain from the same RDRAM and
  DMEM copy; all 8 MB of RDRAM are compared afterwards.  Other tasks go to
  `TAP_HLE` (rsp-hle).  `TAP_CAPTURE=file` saves each task's inputs and
  results.  `ai_dump.so`, an audio plugin, writes the AI stream to a WAV.
- `bc_headless --audio-capture FILE[:N]` records the native tasks;
  `asp_lle FILE build/hd_code.us.v11.bin` hosts the LLE plugin itself and
  runs them with the ROM's rspboot and microcode (from the build's
  decompressed segment); `asp_replay` reruns captures on aspmain.
- `wavcmp cmp REF.wav TEST.wav [SECTIONS]` aligns two recordings window by
  window and reports correlation, SNR and bit-exact samples.
- `compare.py emu` takes `CMP_TRACER` (a tracer with these plugins), so one
  emulator run gives both the clock logs for bc_headless and the sound.
  `asp_dump` prints captured command lists (`-sizes`: samples per task),
  `asp_listdiff A B` compares two captures' lists command by command.
  `BC_AILEN_SEQ=1` (WSLENV) makes `--clock`'s osAiGetLength values go to
  the audio thread in call order, for audio comparisons.

Results (Oct 2026): every audio task of the emulator's attract cycle
(17,236 and 17,243 tasks in two runs, base and NM ROM) leaves all 8 MB of
RDRAM and the sample-buffer DMEM identical to the LLE microcode's; so do
3,958 of bc_headless's own tasks under asp_lle.  With the emulator's clock
and its audio frame sizes, bc_headless's command lists equal the ROM's
(addresses aside) until the first sound the game starts in a different
audio frame (task 328, ~11 s), and the WAVs are bit-identical up to there;
the rest of the 132 s compared differs only by that event timing (band
spectrum within 0.2 dB, loudness within 0.1 dB; see the agent notes).  The
interpreter skips empty command lists (the microcode would run one
command out of a 256-row DMA of junk).  Not exercised by this game's lists
(so checked only against the microcode's code, not run): RESAMPLE flag 2
(the restored 16-byte input tail), ENVMIXER without A_AUX, commands 16+.

## Verification from WSL

The decomp side (WSL Ubuntu: IDO, splat, the NON_MATCHING build) makes the
MSVC build's inputs and runs every check against the MSVC exes.  Needs the
project venv (pyelftools, unicorn), host `gcc` (typemap.py), Visual Studio
on the Windows side, the NON_MATCHING ELFs (`make VERSION=us.v11
NON_MATCHING=1` at the decomp root), the matching ELFs (`build/`, for
loadcheck's reference) and the ROM (`ROM=`, default `../baserom.us.v11.z64`
or `~/blastcorps/baserom.us.v11.z64`).  The exes get Windows paths through
`wslpath -w`.

**`port/inputs/`** (`make -C port inputs`, `gensyms.py inputs`): what the
MSVC build takes from the NON_MATCHING build, as text files, so the Windows
side needs neither the ELFs nor pyelftools:
- `addrs.txt`: every data symbol at an N64 address, with its size;
- `elfsyms.txt`: every ELF symbol's name, address, kind (func/data) and size;
- `nmptrs.txt`: every pointer slot (R_MIPS_32 data relocation) of the NM *C*
  objects' data with the word the NM ELF holds there (a symbol's address
  plus the C source's addend; data the build extracts from the ROM is never
  read);
- `typemap.txt`: the declared scalar leaves of every data-image symbol
  (`tools/typemap.py`: host gcc's DWARF of the game C);
- `hd_code.us.v11.map`, `hd_front_end.us.v11.map`: the NM link maps
  (section addresses and sizes per object, symbols).
Facts of the decomp's own code and its NM link only: no ROM bytes (asset
contents, images, values read from the ROM).  The folder is committed.
After a change to the game code (anything that moves NM data, changes a
declaration or a pointer initialiser), run `make -C port inputs` and commit
the result; **`make -C port check-inputs`** (part of `verify`) brings the
NON_MATCHING build up to date, makes the inputs again in
`build/inputs-check` and fails if they differ from `port/inputs`.

**`make -C port msvc`** builds the MSVC exes from WSL (`tools/msvc_build.sh`:
the sources, `port/inputs` included, mirrored to `MSVC_DIR` on the Windows
side, default `%LOCALAPPDATA%\blastcorps-msvc`, because MSVC compiling over
`\\wsl.localhost` is slow; Visual Studio found with vswhere; CMake + Ninja
in its x64 environment; `MSVC_CONFIG` Release (default), Debug or Asan;
`MSVC_PYTHON`, `MSVC_THIRDPARTY` if needed) -> `MSVC_DIR/<config>/`.  Every
target below builds what it needs this way.  (The old `-msvc` names,
`verify-msvc` etc., still work.)  It needs no git for Windows: before CMake
runs, `msvc_build.sh` prepares RT64 in the Windows-side thirdparty folder
with WSL's git, by running the same `cmake/fetch_rt64.cmake` with WSL's
cmake (commit and patch list from `cmake/rt64_pin.cmake`); CMake then finds
the tree's stamp up to date.  Either git gives the same tree byte for byte
(the fetch pins `core.autocrlf=false` and `core.symlinks=false`).

Other generated files live in `port/build/gen/`: `addrs.txt`,
`typemap.txt`, `typemap_all.txt` (every symbol, for strict mode),
`swaptab.c` and `le/` (for the clang check), `facts.txt`.

### Comparing with the emulator: `make -C port compare DEMO=n`

`tools/compare.py` runs the attract mode in mupen64plus (tools_port/m64trace,
spec `tools/cmp_spec.py`), feeds bc_headless the emulator's clock, and
compares RDRAM frame by frame.  The exe is `CMP_EXE` (the make targets pass
the MSVC build's; without it, the Release build in `MSVC_DIR`); its native
runs go to `...-nativemsvc` folders (`CMP_TAG`).  It uses the NON_MATCHING
test ROM (`build_nm/blastcorps.nm.us.v11.z64`, `make ... nmrom`), which runs
the same C as the exe, so a difference is a port problem (byte order,
platform, host compiler), not a rewrite bug; `CMP_KIND=base` uses the
original ROM. The emulator run (log + RDRAM dumps every 10 frames, ~1 GB per
demo) is cached in `CMP_CACHE` (default `~/cmp_cache/attract-nm-demoN-ROMHASH`) and
redone when cmp_spec.py or the ROM changes. Output: the timeline (submission
retrace, game mode, level, frames in mode, game retrace counter per frame),
then per compared frame "match" or the differing runs with symbols and the
asset an address was loaded from.

A frame is the main thread handing the scheduler its frame task (osSendMesg
to gScheduler with flag 0x40): the same program point on both machines; the
exe dumps there (`--dump`, `--dump-every`). Subcommands (`compare.py -h`):
`emu`, `inject`, `native`, `diff`, `frames`, `hex` (one region side by side),
`calls` (with `emu --calls F,G` and the exe's `--calls F,G`: the calls of
chosen functions per frame, to find where execution forks), `demos`, `run`.

What the emulator log gives the exe:
- `--clock`: every osGetTime / osGetCount / osAiGetLength result, keyed by the
  calling function and the retrace it was read in, and the visibility test's
  answer (cull_rsp_test's RDP output word) keyed by frame; other osGetTime
  calls get the emulator's clock (mupen64plus counts 811092 per VI; the
  mapping is in the file). Timers run in emulator counts.
- `--sync`: the CPU time model. Game code takes no time natively; every
  message call (osSendMesg/RecvMesg/JamMesg/StartThread), main-thread clock
  read, and entry of a function that reads the game's retrace counter (found
  by cmp_spec.py; patched in at start-up, see "MSVC specifics") is a switch
  point: the native thread spins (interrupts and higher-priority threads
  run) until the emulator's time for it, and until the game's retrace
  counter gSchedFrameCount reads what it read there. Not for the scheduler and
  audio threads (interrupt driven; their call order follows the RSP).
- `--frame-done` / `--frame-sp`: when each frame task's RDP and RSP parts
  finished; `--boot-count`: the power-on time when hd_code starts.

Word comparison: each 32-bit word matches if some layout agrees (u32, two
u16, u16 + two bytes, four bytes, an u64 pair, a u32 at 2 mod 4), if both are
pointers to the same function / the exe's own constant data, or an empty
thread queue (__osThreadTail vs NULL). This is lenient: a field swapped as
two u16 where the code reads a u32 still "matches" (see strict mode below).
`tools/compare_ignore.txt` lists what legitimately differs, with
reasons: framebuffers, Z-buffer, RSP buffers, per-frame display lists, code,
thread stacks and OSThreads, libultra internals the platform keeps
elsewhere, scheduler timing/history, and the audio subsystem (its section
of the file; `diff --audio` compares it, see below).

Strict mode (`diff --strict`, after `make -C port strict-data
LC_TRACE=trace.txt`): each byte also gets the width its readers use, from
the declarations of every symbol (`TYPEMAP_ALL=1 typemap.py`), the traced
reads of the data images and of every traced asset (placed where the exe's
load log put it), and `data/image_overrides.txt`; a word that matches only
under some other layout is reported. C objects' .data/.rodata, textures
and a few buffers whose declared type isn't their readers' (`Layout.UNTYPED`)
stay lenient. More emulator inputs: `Q` lines (the game retrace counter
where `PORT_GVI` marks a mid-frame read, `cmp_spec.py GVI_FUNCS`/`GVI_SITES`;
and, once per retrace, the display-freeze flag gPaused as the scheduler's
retrace handler read it, `PORT_EMU`, `EMU_SITES`), and
`calls --args` compares the first argument words (`emu --calls` logs a0-a3,
the exe its argument registers' home area, which may be stale). Never-written
record padding is logged by the exe (`port_garbage`, "load: garbage") and
skipped; the game's reads of such stale bytes get the N64's big-endian view
(`port_n64_byte`, from the unit every swap recorded; `port_vtx_stale` does
the same for the HUD quads 42240.c builds without writing their colour
bytes, which the RSP reads).

**LLE RSP (`emu --lle`, the default for `run` and `verify`).** The
emulator runs the RSP tasks whose results the game reads on cxd4 (an LLE
RSP, CC0, from `~/thirdparty/ref`, loaded at run time as an oracle) through
`tools/audio/rsp_tap.so`: every audio task (each also checked against
the port's interpreter) and cull_rsp_test's visibility test
(`TAP_LLE_UCODE`); graphics stay on the HLE plugin; `ai_dump.so` writes the
emulator's sound to `EMUDIR/emu.wav`.  The emulator log also has
osAiGetLength's real values (logged in the caller after the call returns:
at the callee's `jr ra` the debugger saw a stale v0, which once made the
native audio frames absurd, the audio thread stop after ~3,990 tasks and
the timeline slip at frame ~3505) and each audio frame's sample count (Z
lines).  The sound player's event-posting calls (1A630.c sndPlaySfx and
friends) are entry switch points, so the game thread posts each sound
before or after the audio thread's frame as in the emulator.  An RDP-done
handler that ran before its own retrace's handler in the emulator (the
DP interrupt's message overtook the VI manager's) gets its completion just
before that retrace natively (`inject` counts them).

Results (Oct 2026, all nine attract demos, 14,498 frames, LLE run): the
timeline (retrace, mode, level, frames in mode, game VI) is identical on
every frame; RAM dumps (every 10th frame) 1445 of 1449 match, and strict
mode finds no word that matches only leniently in any of them (the 4 left:
the scheduler's cmdQ.validCount at the dump instant, when the audio
thread's timer fires a little earlier natively than the game thread's
frame send); 4,177 visibility tests, the native model agrees with the LLE
RSP's answer on all of them; 16,986 emulator audio tasks identical to the
interpreter's.  `diff --audio` (the audio heap, synthesizer, sequence and
sound players, audio manager): the audio state matches through frame 2370
except the padding and unused union bytes of ALEvent copies in the event
queues (stack garbage on both machines); from frame 2380 (demo 1) some
voice/DMA state differs.  Sound (`--wav` vs the emulator's LLE WAV, 564 s,
1 s windows): waveform correlation median 1.000 in every section, the
title and logos bit-exact, demo 0 98% of samples bit-exact, the rest
44-88% bit-exact with a median SNR of ~75 dB.

### Quick regression check: `make -C port verify`

Rebuilds the NM test ROM, runs `check-inputs`, builds the MSVC bc_headless
and bc_loadcheck, runs `loadcheck`, then `tools/compare.py verify --frames
VERIFY_FRAMES` (default 1500: boot, logos, front end and attract demo 0): the
emulator side (NM ROM, `--lle`, dumps every 10 frames) is cached in
`$(CMP_CACHE)/verify-nm-N-ROMHASH` and redone only when the ROM (by content: `make
nmrom` rewrites it every time), `cmp_spec.py` or the tracer change (the whole
check takes about 5 minutes then, 1.5 minutes with the cache).  Reported:
frames whose timeline matches, RAM dumps that match (lenient, and strict with
the layouts of `make -C port strict-data`), the visibility tests (emulator
LLE vs the exe's model), the emulator's audio tasks vs the interpreter, the
main thread's switch points used.  It fails when a number is worse than
in `data/verify_expect.txt` (`key MIN`, `key =N`, `key <=N`).
`VERIFY_FRAMES=14498 make -C port verify` covers all nine demos (about 25
minutes the first time, ~12 GB of dumps).

**The cache is shared by worktrees.** Every emulator-run folder carries the
first 8 hex digits of its ROM's SHA-1 (`verify-nm-1500-990290a8`,
`level-nm-07-990290a8`, `attract-nm-demo0-990290a8`), its native counterparts
add the exe (`...-nativemsvc-1a2b3c`: `CMP_TAG` and a hash of the exe's path)
and verify-levels' per-level logs sit next to them (`....log`). So checks run
from worktrees with different NM ROMs never write into each other's folders;
worktrees with the same ROM share the emulator run, and each run holds an
flock on `FOLDER.lock` from the staleness check to its last comparison (a
second run of the same folder prints "in use ... waiting"). A folder is
reused only when its `rom.sha1` (the full hash) and `spec.sha1` (cmp_spec.py +
the tracer, by content) match. Nothing in a folder points into the worktree
that recorded it: `elfs.txt` names the ELFs relative to the checkout
(`build_nm/hd_code.rom.us.v11.elf`; older absolute entries are mapped by that
tail), read from the checkout using the run, whose `build_nm` ROM must be the
folder's (`rom.sha1`; else "rebuild build_nm"). `tools/cmp_cache_clean.py` lists folders of
ROMs no worktree has any more (`--old`: also the folders from before the
hash names) and removes them with `--delete`.

### Every level: `make -C port verify-levels`

The attract demos load 9 of the game's 60 levels (`levelno` 0-59; the
type byte of each `gLevelInfo` record is in `data/levels.txt`).  To reach any
level with the same input on both sides, `tools/compare.py level --level L`
plays `data/levels_input.txt` (made by `tools/levels_input.py`): START / A
from controller read 400 (title, new game, player A, the opening sequence),
A on the globe at read 985, A taps for the intro pages, then from read 1220 a
driving pattern (accelerate, turn both ways, brake and reverse, stop, Z out,
walk, A, Z in, ...).  At frame 930, before the globe first opens, both sides
write L to player 0's "last level" byte (`players[0].pad0[8]`, 0x80364AF8:
00000.c reads it once, on the first entry to the globe, 0x4000), so the globe
opens centred on L and A selects it; the game loads it its own way (mission
intro, bonus-level page, sequence).  The poke is `CMP_POKE` / `compare.py emu
--poke` in the emulator and `--poke F:ADDR:SIZE:VALUE` in the exes (applied
when frame F is sent, after its dump); the input is `CMP_INPUT` / `--input`.
In the emulator the pad is set at the entry of each `osContStartReadData`,
so read n gets line n exactly (the input plugin's own count includes 2 more
PIF commands by the first frame).  `--shots F,..` also saves the emulator's
window (Glide64mk2) at those frames.

`make -C port verify-levels [LEVELS=a,b-c] [LEVEL_JOBS=2]` runs every level
for 1900 frames (the level from frame ~1046-1172, ~730-855 frames in it),
emulator side cached in `$(CMP_CACHE)/level-nm-LL-ROMHASH` (about 3.5 minutes per
level the first time; 14 dumps: every 20 frames from frame 1000 to 1100,
where the level loads and the comparator's stale-trailer rule learns the old
level's heap, then every 100), and compares
the timeline and the RAM dumps (lenient: in a level, heap data lands on
declared arrays such as the frame buffers, so the strict typing reports
noise; `compare.py level --strict` has it).  Expectations per level are in
`data/verify_levels_expect.txt` (`L key VALUE`, `* key VALUE` for all).
All 60 levels take about 85 minutes the first time (two emulators at a
time; ~5.6 GB of cache) and 11 minutes cached.

Results (Oct 2026): every level is reached, no crash, the timeline matches
on all 1900 frames in all 60 levels (49, the ending, used to submit two
mission-intro frames a retrace early natively), and all 14 RAM dumps match in 58;
the rest differ in what the comparison can't follow: 47 a streaming
decompressor's progress between frames, 58 the music's sequence position
(audio thread timing).
bc.exe, following the emulator's clock, shows the same pictures as the
emulator's Glide64mk2 window (radar colour aside, a Glide64 error) and gives
bc_headless's per-frame trace in all 60.  The fixes this found (levels agent):
a crash in level 13 (an untyped trigger table), the trail zones' table, the
area test's halves, a front-end static in host .bss, two mid-function
retrace reads (PORT_GVI), and the stale-byte emulation: display lists,
matrices, vertices and text quads built in heap memory record their units,
and the never-written vertex fields of several builders (sprites, boxes,
globe icons, water) get the N64's old bytes.  When the matched globe display
list (hd_front_end fe_lvlsel_build_globe_dl) changed the NM ROM's CPU timing, three
more such races showed up (lvfix agent): the mode-0x40 start time
(hd_main_thread:4A54), the ghost recorder's sample time (ghost_record) and
the retrace handler's freeze test (__scHandleRetrace:8BD0, PORT_EMU).
Matching the last two front-end functions moved the NM ROM's timing again
(match100 agent) and three more showed up: the Yoshi window's fade and the
typewriter's blinking cursor read the retrace counter mid-function
(GVI_FUNCS yoshi_run_window, yoshi_get_item_text; `PORT_GVI_LAST` where IDO
reuses the loaded register), and the pak thread's scheduler client, added by
hd_load_front_end after the inflate's CPU time, got a retrace message natively
that the emulator's retrace had already passed (sched_add_client and
sched_remove_client are entry switch points now; levels 23 and 45 match 14/14).
A byte-order bug these runs can't see (endian agent): the loader left level
section 0x58, the carrier-progress panel events, big-endian, but
dig_coach_update reads its s16 carrier z natively, so on levels 1 and 26 the
panel (`hud_panel_start(1, 1, 5, 0x81)` at z 2879) waited for z 16139 and
never came.  verify-levels never gets there: with its input the carrier hits a
building at z ~1670 (and ~2100 on the retry), and the lenient dump comparison
accepts a halfword in either byte order, so the unswapped section looks fine
until the event fires.  With the carrier moved on both sides
(`--poke 1150:803EF6E4:4:16580`, z 2860, plus the level poke; `compare.py emu
--calls hud_panel_start` and the exe's `--calls hud_panel_start`) the emulator
shows the panel at frame 1179 on both levels, the old exe never (every dump
differs from 1180 on), the fixed one at 1179 with the later dumps matching
(but for a few gTuneStack words after the danger tune the moved carrier
starts at 1150, in both exes).  loadcheck's TA now checks the section
against the original code on every level.

Debugging what native code wrote: `--watch F:ADDR[:N]` logs the first N
writes to ADDR's 4 KB page from frame F on (rip and the stack's code
pointers; WSL doesn't hand environment variables to the exe, hence an
option); the emulator side is `tools_port/m64trace` with `WRITES`/`ONWRITE`
over `cmp_spec.py` (scratchpad tools of the levels work).  `m64widths.py`
takes `W_INPUT` / `W_POKE` / `W_SAVEDIR` to trace a level's access widths.

## The clang check (`port64.mk`)

`make -C port clang-check`: the same game, ultralib and platform sources
built by clang 18 as an x86_64 `build/headless64/bc_headless.exe`
(`--target=x86_64-w64-mingw32 -fms-extensions`, mingw-w64's x86_64 binutils
and CRT, all from apt), checked, then run against the MSVC bc_headless.
Nobody plays it and nothing ships from it; it exists for what MSVC can't
check and as a second compiler for the same sources:

1. **`ptrcheck64`**: `tools/ptrcheck64.py` over clang's AST of every game,
   platform and ultralib file: host-width pointer fields and pinned globals,
   casts to host-pointer pointers, signed int -> pointer casts (they
   sign-extend: `N64_IPTR`), sizeof of host pointers, pointer + (s32)
   pointer; `tools/layout64.py`: every record laid out i686 vs x86_64 (sizes
   and offsets must be equal); `data/ptrcheck64_allow.txt` lists the
   host-only records.  clang also errors on a pointer that changes address
   space under another pointer (`u8 **p = D_x` with D_x an N64P array).
2. **`headless64`** with **`tools/align64.py`**: the x86-64 ABI gives global
   arrays of 16 bytes or more 16-byte alignment, and clang assumes it for
   every array, extern ones too: it used `movaps` on an 8-aligned N64 array.
   The game code is compiled to LLVM IR, `tools/llalign64.py` lowers every
   alignment above 4 on memory accesses and N64 globals, then the code is
   generated; align64.py checks the objects (no 16-byte-aligned access
   except stack and constants).  `-fno-inline-functions` and
   `-fno-optimize-sibling-calls` for the game, as MSVC's `/Ob1 /GH`;
   `-finstrument-functions` gives `--sync` its entry points.  Pinning is the
   same `coffpin.py`; the absolute symbols come from `gensyms.py link64`
   (nm over the objects; GNU ld and lld link them alike), the pointer table
   from the NM ELFs (`gensyms.py ptrtab`) rather than `inputs/nmptrs.txt`.
3. **Traces** (`tools/tracecmp.py`): the clang exe and the MSVC exe side by
   side over the attract demos (4000 frames) and every level (1900 frames,
   `data/levels_input.txt` and the globe poke, as verify-levels), on the
   free-running virtual clock (no emulator); every per-frame trace must be
   identical and every run must exit 0.  `CLANG_CHECK_ARGS` narrows it
   (`--levels 0-9 --attract 2000`); `LEVEL_JOBS` runs at a time.

**`make -C port overflow-census`**: MSVC has no `-fwrapv` and no
signed-overflow sanitizer, so the census is a clang build
(`build/hl64ubsan`, `-fno-wrapv -fsanitize=signed-integer-overflow` with a
minimal runtime of our own, `tools/ubsan_minimal.c`) run over the attract
demos and every level (`tools/overflow_census.py`); it fails if any signed
add or multiply overflows that the source doesn't wrap explicitly
(`PORT_WRAP_*`).  `OC_ARGS`: e.g. `--levels 0-9`.  (Its build crashes in
level 47: 01C40.c fe_player_ticker_set_text reads `bios[slot]` with slot 4, past its
4-entry array, in the player select's biography mode; a latent
out-of-bounds read of the original code that the normal builds survive.)

## Continuous integration

`.github/workflows/ci.yml` (GitHub Actions) runs on every push and pull
request to `main` and by hand (Actions > CI > Run workflow).  It has no ROM and
uses nothing derived from one, and no secrets.

- **Windows** (`windows-latest`, Visual Studio 2022 there; developed with
  2026): a clean checkout plus `lib/ultralib`, then in Visual Studio's x64
  environment `cmake --preset x64-release` (with the committed `port/inputs`,
  binary folder `RUNNER_TEMP\b`), the default build (bc.exe, bc_headless.exe),
  `bc_loadcheck` and `dist`.  Smoke tests without a ROM: the exes start and
  fail as they should (bc_headless: usage, exit 1; a missing ROM, exit 2;
  bc.exe `--no-config --no-msgbox` with no ROM or a missing one, exit 2, no
  dialog).  `dist/check_dist.py` checks that each zip holds exactly the files
  `make_dist.py` puts in it and nothing that starts like a ROM image; the two
  zips are the run's artifacts.  RT64 (with its submodules), DXC and SDL2 are
  cached (`BC_THIRDPARTY` in `RUNNER_TEMP`), keyed by the download URLs and
  SHA-256s in CMakeLists.txt, `rt64_pin.cmake` (the RT64 commit and patch
  list), `fetch_rt64.cmake` and the patches; a miss fetches them again (a few minutes).
- **Linux** (`ubuntu-24.04`, the WSL distribution's release: the same gcc 13
  and clang 18): `compileall` over the Python tools, the synthetic
  `tools_port/tests/test_eqcheck.py`, and **`make -C port norom-check`**:
  without the NON_MATCHING build (its asm and assets are extracted from the
  ROM) the data addresses come from the committed `inputs/addrs.txt`, in
  `build/norom`: `typemap.txt` made again from the game C must equal
  `inputs/typemap.txt` (the part of `check-inputs` that needs no NM build: a
  declaration changed without `make -C port inputs` fails here), `ptrcheck64`
  and `layout64`, the clang game objects through `llalign64.py` and
  `align64.py`, and `tools/readme_check.py` (both READMEs name every bc.ini
  key; bc.exe's and bc_headless's option comments and usage text list every
  option they take).

Stays local, because it needs the ROM: the matching build and its three OK
lines, the NON_MATCHING build and `nmrom`, `tools_port/runchecks.sh`,
`check-inputs` (it rebuilds the NM build: the other five inputs files),
`loadcheck`, `verify`, `verify-levels`, `compare`, `clang-check`'s traces and
link (its pointer table comes from the NM ELFs), `overflow-census`, the asan
runs.  The exes' start-up `layout_check.c` runs only after a ROM is read.
Run those before merging into `main`, as before.

## The matching ROM build

Unchanged: `make VERSION=us.v11 -j` at the decomp root builds the matching
ROM and checks it (the three OK lines), `make VERSION=us.v11 NON_MATCHING=1`
the NON_MATCHING ELFs and `nmrom` the NM test ROM; `bash
tools_port/runchecks.sh` the rewrite checks.  Port changes to game code are
`NON_MATCHING`/`PORT_HOST` only (`include/game/port.h`, the N64P marks), so
the matching build stays byte-identical.
