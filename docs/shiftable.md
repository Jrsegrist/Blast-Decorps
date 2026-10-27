# The shiftable build

The matching build reproduces the original us.v11 code byte for byte, but
everything in it sits at a fixed address: most data symbols are absolute
(`D_80301234 = 0x80301234;`), the binary blobs hold raw pointers, the hand
asm builds addresses with raw `lui`/`addiu` pairs, and the ROM's files are
found through raw offsets.  Change the size of one function and the game
breaks.

`SHIFT=1` is a second build mode (like `NON_MATCHING=1`) that links the same
sources into a whole ROM in which code, data, `.bss` and ROM files can grow,
shrink, move, be added or be removed.  With nothing changed, its ROM is the
original ROM byte for byte (SHA-1 `483f7161aea39de8b45c9fbc70a2c3883c4dea8c`).

```bash
cd blastcorps && source ../.env/bin/activate
make VERSION=us.v11 SHIFT=1 BASEROM=../baserom.us.v11.z64             # -> build_shift/blastcorps.shift.us.v11.z64
make VERSION=us.v11 SHIFT=1 BASEROM=../baserom.us.v11.z64 shift-test  # the test below (about 17 minutes)
make VERSION=us.v11 SHIFT=1 SHIFT_PADS=my_pads.json                   # with pads / added or dropped ROM files
```

`SHIFT=1` compiles into `build_shift/` and first runs the matching build
(`build/`), which is the reference for where everything was.  The tools are
in `tools_port/shift/`; `NON_MATCHING=1 SHIFT=1` is not supported yet.

## How it works

**Units.** `ldgen.py` turns each segment's splat linker script into one where
every input section (an object's `.text`, `.data`, `.rodata`, `.bss`, or a
binary blob) is a *unit* between two labels, `__shu_<seg>_<n>` and
`__she_<seg>_<n>`.  The data section follows the text directly (init inflates
hd_code's text and data back to back), `.bss` follows the data, and `.bss` is
modelled: the objects' own `.bss` sections plus the unmodelled stretches
between them (`shiftlib.bss_model`), so the D_ symbols in it have a unit too.
Nothing inside a unit moves relative to the rest of it; units move relative to
each other.  An original address maps to the shifted image through the unit
that contains it.

**Reference link.** `shiftbuild.py` first links the matching objects with the
generated scripts and the original symbol files; the result must be the
matching build's text and data byte for byte, and its units' addresses are the
original addresses.

**Symbols.** In the shifted links every absolute assignment of the
`undefined_*.txt` files becomes relative to its unit
(`D_80301234 = __shu_hd_57 + 0x14;`), a name of another segment takes that
segment's shifted address (hd_code is linked before and after the front end,
so the two see each other's final layout), a ROM offset takes the new place of
its ROM file, and fixed addresses (hardware, low RAM, the pins below) stay.

**Raw references** (`relocscan.py`): everything in the reference image that
holds an address without a relocation — pointer words in the binary blobs and
in C data (`(u16 *) 0x803041DC`), hand-asm `lui`/lo pairs (followed through
the registers to every load, store and `addiu`/`ori` that completes them),
ROM offsets in code (`lvl_load_model`'s vehicle files, init's DMA of
hd_code), and the texture table's base as a 16-bit immediate.  Hand asm also
reuses a relocated `lui` for neighbours with raw offsets
(`veh_copy_slot0`: `lui $at, %hi(gSlotAirborne1)`, then
`sb $v0, -0x2C10($at)` for the byte after it): those low halves are raw
references too (9 of them), and their `lui` is left to the linker.  After the
link each one is patched through the address map; a `lui` shared by several
low halves must still give each of them the right `%hi` (the build stops if a
move puts them in different 64 KB pages).

**The ROM** (`romlayout.py`): `tools_port/romsplit.py` walks the base ROM
(bcfiles' walk: the texture table's slots, every gzip member, the LZSS
pictures, the object table at 0x6EC4C0, the static segment at 0x787F40, the
code members) and every file and gap it finds is a ROM unit.  Units are placed in order, each with
its original alignment (the lowest set bit of its original start, at most 16);
the zero gaps that only align the next unit are fills that grow or shrink.
hd_code's and the front end's text and data are compressed again with
`tools_port/gzpack.py` (the second gzip member of each pair directly after the
first: init and `hd_load_front_end` inflate them back to back); the texture
table (0x4CE0, offsets from itself) and the object table (0x6EC4C0, offsets
from itself) are rewritten; the header CRCs (CIC-6102) are recomputed.

**Overrides** (`tools_port/shift/overrides.txt`):

| kind | what |
|---|---|
| `sym init .L802447C0 = hd_code:hd_boot` | init jumps to hd_boot, not to hd_code's first byte (where it inflates to) |
| `sym hd_code romStaticSegmentEnd = rom:static_segment + 0xC0` | hd_mode_setup DMAs 0xC0 bytes: keep the size |
| `glue hd_code hd_code/69000.c.o`, `.../69930.c.o` | gAtanTable / gAsinSteepTable end in the first 0x14 bytes of these units: nothing may go between |
| `lowbits hd_code gCarrierHit 8` ... | the game turns these addresses into values (below): their units keep their low address byte |

## Memory map and pins

| range | what | in the shiftable build |
|---|---|---|
| 0x80000000..0x80000400 | exception vectors, OS | fixed |
| 0x80000400..0x8004B400 | two frame buffers | fixed (init also DMAs the compressed hd_code here at boot) |
| 0x8004B400.. | heap (`gHeapPtr = 0x8004B400` at every mode change) | fixed |
| 0x801E7000..0x8021ED00 | front end (inflated by `hd_load_front_end`, 0x37D00 bytes), heap in levels | starts at 0x801E7000; must end by 0x8021ED00 (about 0x40D0 bytes free) |
| 0x8021ED00.. | init (the header's entry point); after boot the load buffer for compressed files | init must end (with its `.bss`) below hd_code; the load buffer must hold the largest compressed file (0x1EC50 of 0x25AC0) |
| 0x802447C0..0x803FF6C0 | hd_code text, data, `.bss`, then the 0xC0-byte static segment buffer | starts at 0x802447C0 (`--hd-vram` moves it); must end by 0x803FFFF8 (about 0x930 bytes free) |
| 0x803FFFF8 | the front end's ROM start / end, stored by init | fixed |

The ROM must stay below 0xFFB000 (hd_boot reads a debug command line there);
it is padded to 8 MB, or to the next MB above that.

## What is shiftable now

* Any unit of init, hd_code or the front end can grow, shrink or move: pads
  can go before any unit (`--pads`, below), a C file's changed sections move
  everything after them, and sections the matching layout never placed (a
  new `.rodata`, a new object) go into per-section catch-alls at the ends.
* Every ROM file can move, grow or shrink, new files can be added
  (`rom_<name>` / `rom_<name>_end` symbols in every segment), and files
  nothing refers to can be dropped.
* All 5,951 symbol assignments, 2,249 raw references, 4,096 texture table
  entries and 512 object table entries follow.

## What isn't (yet)

* **Units that hold patched raw references must keep their size.**  The
  patches are found in the matching build and applied by unit and offset;
  if such a unit changes (for instance a front-end C file with raw pointer
  casts, or a hand-asm file), the build stops and says so.  The fix is to give
  that code real symbols (the C ones are listed by `audit.py --details`) or,
  for hand asm, its C rewrite (NON_MATCHING).
* **NON_MATCHING=1 SHIFT=1** (the C rewrites in a shiftable ROM) is the next
  step: most raw references are in the hand asm the rewrites replace.
* **Inserting bytes after a texture.**  A texture's DMA length is the distance
  to the next texture table entry, so bytes put between two textures are read
  and decoded as part of the first one.  Add textures as new table entries;
  the shift test's texture-area pad sits after a texture the attract demos
  never load.  Other files are gzip members or have explicit sizes, where a
  longer read is harmless.
* **Addresses the game uses as numbers.**  Hand asm hands stale registers that
  hold addresses to functions that store them as data (FIDELITY notes in
  75490.c, 71140.c, 8A080.c): `gImpactEffectScale = (u8) &gCarrierHit`, the
  collision triangles' id byte `(u8) &gObjectsEnd`, `(u8) &gBlockSlots[i]`,
  `&gPlayerZ`.  `lowbits` keeps the low byte of those units' addresses (up to
  255 bytes of space before them).  Others may exist where the shift test
  doesn't reach.
* Hand asm that shares one `lui` across several objects can't move them into
  different 64 KB pages without an edit.
* The port runs its own code (the matching / NON_MATCHING C at the original
  addresses), not the ROM's: a ROM whose code or data moved in RAM doesn't run
  in bc.exe (below, "The port").

## The port (bc.exe, bc_headless)

The Windows port runs its own build of the game code, so every ROM offset it
uses (the ROM symbols linked into the game code, the level and model tables,
the port's own asset ranges) is the original ROM's.  A shifted ROM tells it
where its files went: when a file moved or changed size, `shiftbuild.py`
writes a **ROM map** into the trailer after the last unit, with a 16-byte
footer at the ROM's end (`BCSHIFT!`, the map's offset, a CRC-32).  Nothing on
the N64 reads the trailer and the header CRCs don't cover it, so emulators run
the ROM as before.  An unchanged build has no map and stays the original ROM.

The map is a list of runs of original units that moved by the same amount:
original start and end, new start, and the new end of the run's data (its last
unit's bytes, without the alignment zeros after them).  Header: `BCSHMAP1`,
version 1, run count, flags (1: code or data moved in RAM), the runs' CRC-32;
then 16 bytes per run, big-endian.  The `.map.txt` report lists the runs
(`map` lines); `shiftcheck.py` checks every unit against them.

The port (`port/src/rdram.c`) reads the map when it loads the ROM and every
read of the ROM goes through it: a read from an original offset starts at the
offset's place in its run; a read that ends exactly where a run ends (a whole
file: the file grew, or the next one moved) ends at the end of the run's data,
so a grown file is read whole, as the shifted code's own `*_end` symbols read
it; `hd_rom_load` translates the whole file's range before its 16 KB pieces.
The texture and object tables hold offsets from themselves, i.e. this ROM's
places; as they are read, their entries become original offsets, which the
port's code then adds to the original table address.  The port accepts such a
ROM when the map reads, its flags say nothing moved in RAM, and the inflated
data images of hd_code and the front end are the original ROM's (SHA-1): a
build that only moved files leaves them byte for byte (only the code holds ROM
offsets).  bc.exe's ROM check then lets it through without `--no-rom-check`
and logs what it is.  Tested: fmt1's growth test (level 1 + 12 placements +
150 RDUs) and four mods exported past their slots (three geometry mods of
level 0, a level 1 records mod) run in bc_headless and bc.exe with the same
traces as the mods applied in memory, and the records at frames 1250 and 1300
equal mupen64plus's on the same ROMs.

A mod's texture slots loaded in bc.exe / bc_headless on such a ROM
(`port_mods.c`) are read from the table and the slot where this ROM has them,
and their bytes are put over the game's DMA at the slot's original offset
(`rom_virt`), with the original DMA length as the room: tested with texture
1549 moved by 16 bytes (a pad before it) - the mod's texels in RDRAM and the
trace are those of the mod on the original ROM.

What the port doesn't follow: added ROM files (the port's code has no
reference to them; a table entry that points at one reads from where it is:
`mod-export`'s new texture slots, an added file `texnew`, are read so) and
code or data changes (flag 1: refused; rebuild the port from the same
source).

A shifted ROM built before the map (2026-10-25) has files moved and nothing
saying where.  The port doesn't try to rebuild a map for it - finding each of
the ~7,000 units by its bytes fails for exactly the files that were edited,
and the offsets are only in the ROM's own code - but it recognises one (no
map, the original game data image inflating from another place) and says what
it is: build it again with the current tools (their ROMs carry the map), or
play the mod itself (`rom_unmapped_shift`, bc.exe's ROM check and
bc_headless).

## Adding a function or a data object

* **C:** write it in an existing file (its unit grows, everything after it
  moves) or a new `src.us.v11/hd_code/NEW.c` (it lands in the catch-all at the
  end of the text / data / `.bss`).  Reference other code and data by name
  (`extern`), never by address.
* **Room in the middle of a segment:** a pads file
  (`tools_port/shift/pads_test.json` is the example):

```json
{"ram": {"hd_code": {"text": [[0, 64]], "data": [[3, 16]], "bss": [[0, 48]]}},
 "rom": {"pads": [["0x4A5660", 16]],
         "add": [{"before": "0x787F35", "name": "my_file", "path": "my_file.bin"}],
         "drop": ["0x787F35"]}}
```

  `[ordinal, bytes]` puts a pad (a dummy function, a data object, or `.bss`
  space; multiples of 16) before the ordinal-th unit of that section;
  ROM `pads` go before the unit at that original offset, `add` puts a new file
  there, `drop` leaves a unit out (the build stops if anything refers to it).
* **A replaced file** (an edited level, model or building that outgrew its
  place): `"rom": {"replace": [{"at": "0x4A5660", "path": "lagp.gz", "empty":
  ["0x4ABF91"]}]}` gives the unit at `at` the bytes of `path` (the whole
  gzip pair) and empties the units in `empty` (the pair's second member, now
  inside `path`; emptied units are unaligned, so nothing comes between the
  two members). `tools_port/bcfiles build` writes this file when an edited
  file doesn't fit ([tools](tools.md)).
* **In-place bytes** (the files that still fit and a mod's texture slots,
  written by `bcfiles build` / `mod-export` next to the replaced files):
  `"rom": {"patch": [{"at": "0x18AD30", "path": "texture_1549.bin",
  "texture": 1549}, {"at": "0x7D4C", "hex": "0228", "texture": 1549}]}` writes
  those bytes over the units at original offset `at` before the ROM is laid
  out (`romlayout.apply_patches`), exactly as the in-place write puts them in
  the original ROM. No unit changes size, so each patch moves with its unit and
  the tables, the code's offsets and the ROM map follow as for any unit; a
  patch may span units (a level's two members and the zeros to its slot's
  end) but the build stops if it touches a unit made from the code, a
  replaced or emptied unit, or puts non-zero bytes in an alignment gap. A
  texture slot's patch (`"texture": id`) must be exactly the slot's room in
  the base ROM (its DMA length) — bytes after a texture are read as part of
  it (the caveat above); a slot that grows is a `replace` of its texture's own
  file instead (bcfiles writes it: the packed bytes rounded up to 8), so the
  textures after it move and the table follows; the 2-byte one is the table
  entry's size field, which the table rewrite keeps (it only rewrites the
  offsets). Tested: geo2_demo and
  geo1_all_tex (level 0 grown past its slot, checker texture in slot 1549):
  the shifted ROM holds the in-place export's slot bytes and size; also with
  a 16-byte pad before the slot (moved: the table and the map follow). Tested: lagp grown from 0x75B0 to
  0x7788 compressed bytes (150 more RDUs, 12 more objects); every file after
  it moved by 0x1E0, the tables and the code's ROM offsets followed, and the
  ROM ran in mupen64plus into the level with all 44 objects in place.
* **New texture slots** (`bcfiles mod-export` on its shift path): their bytes
  are an added file named `texnew` (`"add"`, before the trailer) and
  `"rom": {"texnew": [{"id": 742, "at": 0, "size": 2720}]}` points entry `id`
  at `at` in it (packing 0, the size) and the next entry's offset at the
  bytes' end (`romlayout.apply_texnew`, after the table rewrite); the entries
  before, at and after must be empty in the base ROM. Tested: a new slot on
  level 0's road with level 1 grown past its slot: the shifted ROM in
  bc_headless == the mod on the original ROM (trace, records, texels).
* Mind the free space in the memory map above: hd_code has about 0x930 bytes
  before 0x803FFFF8.  `--hd-vram` is meant to move hd_code down into the
  load buffer's spare 0x6E70 bytes, but the shift test has not run it: treat
  it as untested.

## Checks

* `shiftcheck.py` (static, part of `shift-test`): every unit that kept its
  size holds the same bytes in both builds except relocated words and patched
  raw references; every `R_MIPS_32` / `R_MIPS_26` target and every patched
  reference points at the shifted address of its old target; all 6,290
  symbols moved with their units; no word or `lui`/lo pair left without a
  relocation still holds an old address of something that moved; every ROM
  unit holds the same bytes (the tables: the same entries, moved); the CRCs.
* `shifttest.py` / `make SHIFT=1 shift-test`:
  1. the unshifted ROM must be the original ROM (SHA-1);
  2. a ROM built with `pads_test.json`: a dummy function at the start of each
     code segment (after init's entry stub, whose address the header fixes)
     and one in the middle of hd_code, data objects at the start of hd_code's
     and the front end's data and in the middle, `.bss` pads, 64 bytes put in
     the ROM's file area at three places (between textures, before the first
     level file, after the object table), a new 256-byte ROM file and a
     dropped one;
  3. `shiftcheck.py` on it;
  4. both ROMs in mupen64plus (`tools_port/m64trace`, `spec_shiftrun.py`, SDL's
     offscreen video driver), no input: the title, the front end, then the
     attract demos, nine levels.  RDRAM is dumped at the same game frames
     (counted from each level's start) and `shiftcmp.py` compares it object
     by object.

To make two runs comparable, the game's own uses of the CPU count
(`util_seed_rand`, `people_init`, `snd_random_id`: the random seeds, and
`gFrameDrawTime`) get synthetic values; the boot inflate and the DMAs take
different times in a shifted ROM, so the real count differs, and it even
varies a little between two runs of the same ROM.  `shiftcmp.py` then counts
a differing word as equal when it is a translated pointer (the shifted
address of the old one, its physical address, a ROM offset, a table entry),
derived from a moved address (an address packed with other bits, an offset
from a moved object), a leftover (each image still holds its own build's copy
of something loaded earlier: the compressed hd_code from the boot DMA, the
front end's code, the vector code — also byte by byte), stale (fixed-address
bytes neither run wrote since the level started: a level can write three
bytes of a word over the front end's leftover data), timing-dependent (it
differs between the unshifted ROM and three runs of it with other CPU timing,
`CountPerOp` 1, 3 and 4), or other threads' memory (thread stacks and records,
message queues — the front end's pak queues stay in use during the levels,
over the level's heap — the scheduler, the audio heap, libultra's managers).
Everything else is a difference, and the test fails on any.

Result (`pads_test.json`): the unshifted ROM is the original; shiftcheck OK
(2,249 patched raw references, 6,290 symbols, 0 stale references, 7,049 ROM
units, CRC); in the emulator, RAM at frames 200 and 800 of the first level
and frame 400 of the other eight: 0 words differ (each comparison:
786,000–794,000 words equal, 5,000–5,600 translated pointers, 82,000–89,000
leftovers, 5,800–6,300 in other threads' memory, 280–570 timing-dependent,
180–360 stale).  The whole test takes about 17 minutes.

**The front end's dead code.**  The levels put some buffers on the heap where
the front end's code was, and read them before they write them:
`veh_sync_buffers` copies a vehicle's save buffer A to buffer B on the first
frame (for the J-bomb, `gJbombMtxA` at 0x801E83E0, which the game has only
partly filled by then), so buffer B then holds words of the front end's code.  A shifted front end
calls hd_code at other addresses, so those words differ (`jal`s, `%lo`s).
This is the original game's behaviour (it reads uninitialised memory), not a
broken reference, and nothing else follows from it: without the zeroing below,
the only difference in the whole test is those 48 words of buffer B in the
third level (a copy of code, so not a leftover at its own address).  So that
the test compares the layout and not this, `spec_shiftrun.py` zeroes the front end's
code (0x801E7000 to the end of its `.text`) at the entry of `hd_setup_level`,
in every run, the unshifted and noise runs too (`SHIFT_FILL`).

## Audit (`python3 tools_port/shift/audit.py --baserom ROM [--details]`)

What pins addresses in the matching build (counts; `--details` lists every
item with file:line or blob+offset):

| | matching build | shiftable build |
|---|---|---|
| absolute symbol assignments | init 68, hd_code 2,096, front end 3,787 | unit-relative (2,261) or mapped (3,451 other-segment, 176 ROM); 62 fixed, 1 alias |
| pointer words in binary blobs | hd_code 1,180, front end 318, init 11 | patched |
| pointer words in C data | front end 95 (01C40.c) | patched |
| hand-asm `lui`/lo addresses | hd_code 491 instructions, init 8 (9 of them low halves of a relocated `lui`) | patched |
| ROM offsets in code | hd_code 138 (vehicle models, object table, texture table base ×7), init 8 | patched |
| raw addresses in the matching C | 46C20.c, 48D00.c (`0x802FDBAC + i * 0x18`), 01C40.c, 00000.c debug prints | pins kept; the others patched as raw references |
| linker-script pins | 5 section VMAs, 20 `.bss` sections at fixed addresses | text VMAs only |
| ROM tables | texture table (4,096 entries), object table (512) | rewritten |
| ROM sizes | "next file - this file" everywhere | follow (see the texture caveat) |
| boot | CRCs; init's DMA of hd_code and the front end's ROM range | recomputed / patched |

Remaining raw references after the shift: 0 (shiftcheck "stale references").
