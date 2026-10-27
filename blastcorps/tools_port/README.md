# tools_port: NON_MATCHING builds and the equivalence harness

The native port needs real C for the roughly 660 hand-written asm functions in
hd_code (56040 through 8DDB0), and for the front end's hand-written 1B100
(the front end's former near-misses are matched C now). That C can't byte-match, so it is checked for
*functional* equivalence instead. Two pieces support this:

* `make NON_MATCHING=1` builds `#ifdef NON_MATCHING` rewrites into `build_nm/`.
* `eqcheck.py` runs a function from both builds in a MIPS emulator on
  identical random inputs and diffs the results.

## 1. Writing a rewrite

```c
#ifdef NON_MATCHING
void radar_clear_markers(void) {
    s32 i;

    for (i = 0; i < 25; i++) {
        *(s32 *) (gRadarMarkers + i * 0x14) = -1;
    }
}
#else
#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/8A080/radar_clear_markers.s")
#endif
```

Put any externs or prototypes the rewrite needs inside the `#ifdef`, so the
matching build sees exactly the same source as before. asm-processor already
handles a pragma sitting inside `#else`. Worked examples:
`src.us.v11/hd_code/8A080.c` (radar_clear_markers) and `83910.c` (barge_enter).

## 2. Building

```bash
cd ~/blastcorps/blastcorps && source ../.env/bin/activate
make VERSION=us.v11 -j8                  # matching: must still print both ": OK" lines
make VERSION=us.v11 NON_MATCHING=1 -j8   # -> build_nm/{hd_code,init}.us.v11.elf, no checksum
```

What `NON_MATCHING=1` changes (Makefile plus `tools_port/nm_ldscript.py`):

| | matching (`build/`) | NON_MATCHING (`build_nm/`) |
|---|---|---|
| CFLAGS | | `-DNON_MATCHING` |
| hd_code `.text` VRAM | 0x802447C0 | `NM_TEXT_VRAM` (default 0x80800000) |
| `.hd_code_data` | 0x802E8BD0 | same address, and every `*_bin` blob is pinned to its original address |
| data tables inside the hd_code text bins | bin bytes in `.hd_code` | C definitions (69000/69930/7F8B0/7FB50/8DDB0.c `.data`) in `.nm_pin_<ADDR>_<SIZE>` sections at the original addresses (`NM_PIN_HD_CODE`, section 7) |
| sections nothing places (e.g. new `.rodata` in a hand-asm file) | discarded, so the link fails | `.nm_extra` at `NM_EXTRA_VRAM` (0x80A00000) |
| front end `.hd_front_end` (text + RSP ucode bin) VRAM | 0x801E7000 | `NM_FE_TEXT_VRAM` (0x80900000) |
| `.hd_front_end_data` | 0x80208040 | same address, bins pinned |
| front end: sections nothing places | discarded | `.nm_extra_fe` at `NM_FE_EXTRA_VRAM` (0x80A80000) |
| `undefined_*.txt` | `name = addr;` (overrides objects) | functions (`func_*`, aliases `a = b;`, all of `undefined_funcs*`) become `PROVIDE(name = addr);`, so a moved function's own definition wins; data assignments stay hard |
| front end's `undefined_*.hd_front_end.txt` | hd_code functions at their original addresses | hd_code functions at their **NM** addresses (`nm_ldscript.py --resolve-elf build_nm/hd_code.elf`), so front-end C calls hd_code's relocated code and its C rewrites directly (a rewrite with a non-ABI convention is not at, and doesn't take the registers of, the original address) |
| `all` | sha1 verify | link only |

hd_code's calls into the front end still go to the original front-end
addresses (hd_code links first; the front end links against it). In the
harness those land in the original front-end text, which the NM machine is
pre-filled with, and are translated to the NM function by name (the same
mechanism as pointers in the data blobs); in the test ROM they hit trampolines.

Data assignments must stay hard because some data symbols are pinned in
`undefined_syms` *and* defined in C (hd.c's u64 `gPrevGameMode`, `gGameMode`, `gNextGameMode` sit outside
its modelled `.bss`). With a `PROVIDE()` the C definition would win in
`build_nm` and the variable would move (gNextGameMode landed at 0x80312D88, on
top of gSchedStack), while the hand asm still used 0x80364A98: two homes for one
variable. `python3 tools_port/nm_symaudit.py` compares the symbol tables of
`build/` and `build_nm/` (init, hd_code and the front end) and lists every
non-text symbol whose address differs; `runchecks -b`/`-B` runs it after
building and stops if anything moved.

Why the text moves: code ends exactly where the data segment starts, and most
data symbols are absolute `D_xxxxxxxx = 0x...` definitions, so data can't move.
Any rewrite that grows would run into it. Moving the text clear of the data
means data and `.bss` keep their original addresses, and only code addresses
change. The harness maps those by symbol name. The NM ELF is a verification
artifact; for a bootable ROM see 2a.

make doesn't track flag changes. Because `build_nm/` is a separate tree,
switching modes never clobbers the matching objects.

### 2a. Test ROM (`make ... nmrom`)

```bash
make VERSION=us.v11 -j8                                   # build/ is an input
make VERSION=us.v11 NON_MATCHING=1 -j8 nmrom BASEROM=~/blastcorps/baserom.us.v11.z64
# -> build_nm/blastcorps.nm.us.v11.z64 (12 MB) + build_nm/blastcorps.nm.us.v11.txt (report)
```

A bootable ROM that runs the NON_MATCHING hd_code (`tools_port/nm_rom.py`; the
docstring has the details). **Needs the Expansion Pak (8 MB RDRAM).** The same
`build_nm` objects are relinked as `build_nm/hd_code.rom.us.v11.elf` with the
text at `NM_ROM_TEXT_VRAM` (0x80400000, Expansion Pak RAM; Blast Corps itself
only uses 4 MB) and `.nm_extra` right behind it; the harness link (0x80800000)
is untouched. The ROM is the base ROM plus two appended images and a patched
init:

* init still inflates the original hd_code text + data to 0x802447C0, so the
  data tables inside the text bins are where the NM code reads them.
* init's boot tail (a register save/restore around a no-op debug stub,
  init+0x1AE0..0x1BF8) is replaced in place by a loader: write back +
  invalidate the D-cache, PI-DMA image A (NM text) to 0x80400000 and image B
  (original text with trampolines + NM `.hd_code_data`) over 0x802447C0,
  invalidate the I-cache, `a0 = gp`, jump to the NM `hd_boot`.
* Trampolines (`j NM; nop`) on the original function entries send original-
  address callers (the front end, which is loaded once to 0x801E7000 and calls
  hd_code at fixed addresses; pointer tables in the bins) into the NM code.
  Not trampolined: rewritten functions with a `conventions.txt` line (their C
  takes other registers), rewritten functions called from original code that
  can still run, functions whose second word is a branch target. The report
  lists them, the front-end calls that stay on original code, every NM
  jal/j outside the NM text, lui/lo constants and data words that still point
  into the original text, and the loader's disassembly.
* **The NON_MATCHING front end runs too.** The front-end objects are relinked
  as `build_nm/hd_front_end.rom.us.v11.elf` with the text at
  `NM_ROM_FE_TEXT_VRAM` (0x80500000, after the NM hd_code; image A covers
  both, one DMA), data pinned, calling the ROM-linked hd_code's addresses.
  hd_code inflates the front end from ROM to 0x801E7000 when it first needs
  it (`hd_load_front_end`, from the range init stores at 0x803FFFF8/C), so
  `nm_rom.py` builds image D: the original front-end text with a trampoline
  on every function entry (all 155; skipped like hd_code's: second word a
  target, first word a delay slot, a full convention) followed by the NM
  `.hd_front_end_data`, each gzipped again with `tools_port/gzpack.py`; the loader
  overwrites init's two words with image D's ROM range. The report lists the
  front-end entries left as original code and any front-end data word that
  changed other than as a code pointer. Checked in mupen64plus (glide64mk2):
  the NM front-end functions run (breakpoints in 0x805xxxxx hit, the
  original entries hold `j 0x805xxxxx`), and the ROM goes front end -> attract
  demos 0..8 like the base ROM (the NM build runs a little slower).
* The ROM is padded to 12 MB: `hd_boot` reads a debug command line from
  ROM 0xFFB000, past the end of the original ROM, so the ROM must stay below
  that. Header CRCs are recomputed (CIC-6102). Emulators won't find the ROM in
  their database: set the save type to EEPROM 4 Kbit and enable 8 MB RAM.

## 3. Running the harness

Setup is done once and is already installed: `pip install unicorn pyelftools` into `~/blastcorps/.env`.

```bash
python3 tools_port/eqcheck.py FUNC [options]     # from the repo dir, venv active
bash tools_port/eq.sh FUNC [options]             # same, from anywhere: activates ../.env and cds to the repo
```

By default it compares `build/` (the original asm) against `build_nm/` (your
rewrite), runs 200 trials, and stops at the first failing trial. Each build is
init + hd_code + the front end (three ELFs): FUNC is found in whichever
segment defines it, front-end and hd_code functions call each other by name,
and the NM machine is pre-filled with the original text of both segments.

**Start with `--explore`.** It runs the reference once and prints which
globals, heap, or stack bytes the function read and wrote, plus the calls it
made with their arguments. Use that to decide which inputs to randomize:

```
$ python3 tools_port/eqcheck.py hud_remember_marked_target --explore --mem gShownTargets:40=words:0,1,2 --mem gMarkedTargetPts=int
reads:
  gMarkedTargetPts               0x8036C794..0x8036C798 (4 bytes)
  gShownTargets               0x8036C7A0..0x8036C7AC (12 bytes)
writes:
  gShownTargets               0x8036C7A8..0x8036C7AC (4 bytes)
```

Heap bytes are labelled by block (`heap0 (0x80B00000)`, see `ptr` below).
Loads from data tables that live inside the text range (for example
`gTexAnimDefList` in the 7D9D0 blob) are listed with an `(in text)` suffix.
Contiguous bytes with no nearby symbol are merged into one `(unnamed) 0x...`
range, and a run of more than four back-to-back symbol groups (a scan running
across many globals) prints as one `D_A .. D_B` line, so a runaway scan of an
unterminated table is a single line, not thousands.

### Inputs

All registers (GPRs, FPRs, hi/lo) and the caller's 0x100-byte frame at `sp`
get identical pseudo-random poison in both runs. `sp` is 0x80E00000 and `ra`
is a sentinel. On top of that you can set:

* `--arg REG=SPEC` takes any register (`a0`..`a3`, `f12`, `f14`, `t0`, `s1`, and so on), or a stack argument: `--arg stack0=SPEC` (the C slot names, `stackN` = the word at `sp+0x10+4N`) or the same as `--arg sp+0x10=SPEC`. Every stack argument given this way (and every `stackN` the function's own convention uses) is one of the function's **own incoming slots**: under o32 the callee owns them, so writes to them are not compared (a C rewrite that assigns to a stack-passed parameter stores it there). `-v` prints a note naming the slots whose writes were ignored.
* `--mem TARGET[:SIZE]=FILL` sets memory contents. TARGET is `SYM`, `SYM+0x10`, a raw address, or relative to an argument or heap block: `@a0`, `@a0+0x40`, `@heap1+0x10` (see below). Symbols resolve separately in each build. An unknown symbol is an error naming the symbol and the build.
* **Sizing.** Without `:SIZE` a value fill writes a word (4 bytes). For `--mem SYM=...` (no offset) whose ELF symbol has a size of 1 or 2 (C-defined `u8`/`s16` globals) the write is sized from the symbol, with a `note:`. Absolute `D_` symbols from `undefined_syms*.txt` have no size; then, if the next symbol starts less than 4 bytes after the target, you get a `warning:` and should write `SYM:1=` or `SYM:2=` yourself. A word written over a byte global silently clobbers its neighbours.
* `--mem TARGET*STRIDE:COUNT[/W]=FILL` fills the same W-byte field (default 4) in COUNT records STRIDE bytes apart, each record drawn separately: `--mem gRadarMarkers+8*0x14:25/2=choice:0,0x7FFF` sets the halfword at +8 of all 25 entries of a 0x14-byte table.
* `--heap SIZE[=FILL]` allocates a scratch-heap block without putting its address in any register (FILL defaults to `rand`; `zero`, `words:..` etc. work). Use it for memory reached only through a global or a field: `--heap 0x300 --mem gLevelData=rel:heap0`. `--heap` blocks are numbered first (`heap0`, `heap1`, ...), before `--arg ptr` blocks.

| SPEC / FILL | meaning |
|---|---|
| `0x1234` | constant |
| `int` / `int:LO:HI` | random int (biased toward small values) or uniform in a range |
| `choice:0,1,5` | one of these values; `choice:0x50*3,0x5A` weights a value (picked 3x as often) |
| `float[:LO:HI]` | random single in an FPR (`f12=float`) |
| `fbits[:LO:HI]` | float bits in an integer register or stack slot (o32 float args after an int arg) |
| `ptr[:SIZE]` / `ptrz[:SIZE]` | pointer to a fresh scratch-heap block (random or zero bytes, default 0x200) |
| `int64[:LO:HI]` | a full 64-bit value (for a whole 64-bit register, a 64-bit convention input, or 8 bytes of `--mem`) |
| `sym:NAME` / `sym:NAME+0x10` | the **address** of NAME (in a register too, resolved per build) |
| `val:NAME[+0x10][:W]` | the **value** stored at NAME+0x10 in the build's loaded image (W = 1, 2 or 4 bytes, zero-extended; `.bss` reads 0; earlier `--mem` writes are not seen) |
| `rel:BASE+TERM+..` | an **address** relative to another value: see "Pointer-relative values" |
| `rand`, `zero` | (`--mem`) SIZE random or zero bytes |
| `hex:0011223344` | (`--mem`) exactly these bytes (spaces/underscores allowed); SIZE defaults to their length, a larger SIZE repeats them |
| `words:a,b,c` / `halves:a,b` / `bytes:a,b` | (`--mem`) each word, halfword or byte chosen from a pool (weights `v*N` allowed), which is good for flags, indices, and NULL vs non-NULL pointers |
| `floats[:LO:HI]` | (`--mem`) SIZE/4 random floats |
| `onehot:STRIDE[@OFF][/W]:POOL:FILL` | (`--mem`, needs SIZE) fill with FILL, then put one value from POOL into exactly one slot: see "One special slot" |

#### Order of application

Registers and the caller's frame are poisoned first. Then the `--heap` blocks
are allocated and filled. Then every `--arg` is
generated, left to right except that `rel:` args come after all the others; a
`ptr` block's random contents are laid down at this point. Then every `--mem`,
left to right. Later writes win, so a `--mem` into a `ptr` block (or into the
stack-argument area) overrides the block's random bytes. (Before Oct 2026 the
order was the reverse and `ptr` data silently overwrote `--mem` specs.)

#### Pointer-relative values

Scratch-heap blocks are numbered in allocation order: `heap0`, `heap1`, ...
(`--heap` blocks first, then `--arg ... ptr` specs, then `ptr` fills in
`--mem`). The first block is always 0x80B00000. `heap` alone is 0x80B00000.

* `--mem @a0+0x40=0x50` writes into the memory an argument points at. `@NAME`
  is an `--arg` register (`a0`..), `heapN`, or `heap`.
* `--arg a1=rel:a0+0x60*int:0:5` makes a value relative to another one.
  BASE is an `--arg` register, `heap`/`heapN`, a symbol (`NAME` or `sym:NAME`), or a number. Each
  `+TERM` is a constant (`+0x10`, `+-4`), any value spec (`+choice:0,0x60,0x5A0`),
  or `K*SPEC`, K times a random value (`+0x60*int:0:16`, `+0x14*choice:0,24`).
  The same works as a `--mem` fill, which stores a pointer: `--mem gLevelData=rel:heap0+0x20`.

> **`rel:SYM+k` is SYM's ADDRESS plus k, never the value stored at SYM.**
> `--mem D_X=rel:D_Y+1` stores `&D_Y + 1` (a pointer), not `D_Y + 1`. A
> boundary run written that way ("one more than the limit in D_Y") tests
> nothing. For "the value at SYM" use `val:SYM` (the loaded-image value, as a
> constant) or set both globals from the same fixed numbers. eqcheck prints a
> `note:` for every `--mem ...=rel:SYM..` with a symbol base to remind you.

```
# a1 = a0 + 0..16 records of 0x60 bytes, inside a 0x600-byte block
eqcheck.py coll_disable_tris --arg a0=ptr:0x600 --arg a1=rel:a0+0x60*int:0:16 --ret void
# a global pointing at a heap block, and fields inside that block
eqcheck.py lvl_group_get --arg a0=int:1:20 --mem gLevelData=ptr:0x300 \
    --mem @heap0+0x74=choice:0x100*2,0x102,0 --mem @heap0+0x100:0x200=halves:4,6,8,0x10
```

#### One special slot

`onehot:STRIDE[@OFF][/W]:POOL:FILL` fills SIZE bytes with FILL (any fill;
a plain constant repeats as a word), then picks one slot of the
SIZE/STRIDE-slot array and writes a value from POOL (comma list, weights
allowed) at byte OFF of that slot (default 0), W bytes wide (1, 2 or 4,
default 4). The slot is the first one 30% of the time, the last one 30%, and a
uniformly random one otherwise, so one run covers first/last/middle. Examples
for a 25 x 0x14 table whose word 0 is an id (-1 = free):

```
--mem gRadarMarkers:500=onehot:0x14:-1:words:0,1,2          # exactly one free slot
--mem gRadarMarkers:500=onehot:0x14:0x100:words:-1,0,1      # the searched id in exactly one slot
--mem gRadarMarkers:500=onehot:0x14@8/2:0x7FFF:rand         # one slot's halfword at +8 = 0x7FFF
```

Choose pools that reach every branch. In the demo, a broken
`hud_target_was_shown` (with its NULL check dropped) **passes** when
`gNearestTargetPts=choice:1,2` and fails at once with `choice:0,1,2`. The harness
only proves equivalence on the inputs you give it.

### Calls

Every call to another function is **stubbed** by default. The harness records
the callee's name and its argument registers (`a0`..`a3` unless you override
them), puts a canned value in `v0`, `v1`, and `f0`, and returns. The canned
value is a deterministic hash of the seed, the trial, the callee, and the call
number, so both builds get the same values. The ordered list of calls is
compared between the builds.

* `--follow NAME[,NAME]` runs that callee for real, using each build's own version of it. `--follow-all` runs every callee; `--no-follow NAME[,NAME]` keeps stubbing those under `--follow-all` (OS calls that block or DMA, a decompressor, a callee whose own checks stub its callees).
* `--stop-at NAME[:K]` ends the run at the K-th (default first) call to the stubbed or modelled NAME, as if the function had returned there: memory and calls are compared, registers are not (they're mid-function). For loops that never return, e.g. the front end's save thread `fe_pak_thread`. A `--model` can end the run itself with `mem.stop()`; `tools_port/models/mesgqueue.py` models `osRecvMesg`/`osSendMesg` on the real OSMesgQueue (and stops when the thread blocks on its empty command queue).
* **Static helpers are followed automatically.** A callee that exists only in the new build (no symbol of that name in the reference build, typically a `static` helper a rewrite introduced) is part of the rewrite, so it always runs for real instead of showing up as an extra call. The run prints `note: following NAME, which exists only in build_nm`.
* `--sig NAME=a0,a1` sets which registers count as NAME's arguments. Use it when a callee takes fewer than 4 arguments and the two versions leave different garbage in the unused ones. **`--sig NAME=` (empty list) compares no arguments at all**, just that the call happened, for a callee that takes none (otherwise leftover a0-a3 garbage is compared). `--sig '*=a0'` changes the default. Stack arguments can be listed as `stack0` (= `sp+0x10`), `stack1`, ... For a callee in `conventions.txt` (section 3a) the arguments are already mapped, and `--sig` then selects C slots (`--sig math_dist3=a0,a1,a2`). **Widths:** an entry `REG:W` (W = 1, 2, 4, 8) compares only the low W bytes: `--sig trail_add=a0:1,a1,a2:2,stack4:2` compares a `u8`/`s16` argument at its declared width, so leftover upper bits (asm passes the whole register, C a truncated value) don't count. This works with and without a convention (no more per-line `--conv` just for widths).
* `--stub-ret NAME=SPEC` sets what a stubbed callee returns (its first convention output, if it has one; `--stub-ret NAME.REG=SPEC` sets the output in asm register REG). SPEC is `rand` (default), a constant, `ptr:SIZE` (deterministic heap blocks, useful for allocators), `seq:1,0,5` (the k-th call in a trial returns the k-th value, cycling: one run drives several branches), or any value spec from the `--arg` table (`choice:0,1*3`, `int:LO:HI`, `int64`, `float`, `fbits`, `sym:NAME`, `val:NAME`), drawn per trial and call, identically in both builds. A constant wider than 32 bits returns `v0:v1` (o32 `u64`); for a convention output declared 64-bit (`ret64`, below) the whole value is delivered.
* `--stub-preserve NAME=v1,a0` keeps a stub from writing those registers. The default stub writes v0, v1 and f0; asm callers that rely on the callee preserving one of them (tex_decode_next keeps &gTexDecodeQueueHead in v1 across tex_decode) fault in the reference otherwise. `NAME=*` applies to every stub. Put permanent cases in `conventions.txt` as `preserve`.
* `--model NAME=FILE.py[:FUNC]` runs a Python function in place of NAME in both builds (like `--follow`, so no call is recorded). It is called as `FUNC(args, mem)`: `args` are NAME's inputs in C argument order (its convention's `in` list, else a0-a3), `mem` reads and writes guest memory (`mem.u32(a)`, `mem.s16(a)`, `mem.w32(a, v)`, `mem.read(a, n)`, `mem.sym("D_8036...")`). It returns the outputs in `out` order (an int for one), delivered on the caller's side like a stub's. FILE is also looked up in `tools_port/models/` (example: `math_dist3.py`). Use it for a callee that can't run in the emulator, or to cross-check a model.

Calls are recognized by control reaching a function's entry address, so
`jal`, `jalr`, and tail `j`/`b` all count. A target outside any loaded code
(another overlay, or a NULL function pointer) becomes `sub_XXXXXXXX` and is
stubbed.

### 3a. Non-ABI functions: `conventions.txt`

About 540 hand-asm functions take inputs in t-, s-, v-registers (and gp/fp)
and return results in v1, s-, t-registers or fp. Their C rewrites use plain
o32: ordinary parameters and a return value, with extra outputs through
pointer arguments. `tools_port/conventions.txt` records the mapping, one line
per function, and eqcheck applies it automatically:

```
veh_tris_all_solid_impl: in v0=a0 ; out v1=ret
math_atan: in v1=a0 ; out fp=ret ; preserve a0,a1,v1
veh_has_partial_rider_walk: in s2=a0 ; out t0=ret ; preserve t1,t2,t3,t4,s3 ; clobbers s2
math_dist3: in t3=a0,t4=a1,t5=a2,t6=a3,t7=stack0,s0=stack1 ; out s1=ret ; preserve t6,t7,s0 ; clobbers s1
tex_decode: preserve a0,v1
```

| clause | meaning |
|---|---|
| `in REG=SLOT,..` | the asm takes an input in REG; the C version takes it in SLOT: `a0`-`a3`, `stackN` (the word at sp+0x10+4N), `f12`, `f14`, or `*SLOT[+OFF][:W]` (read through a pointer argument; W = 1, 2, 4 bytes). List **every** input, ABI ones too (`a0=a0`), in C argument order. REG may be `stackN` for a stack argument the asm reads in place. |
| `out REG=DEST,..` | the asm returns a value in REG; the C version delivers it as `ret` (v0), `fret` (f0), another register, or `*SLOT[+OFF][:W]` (written through a pointer argument). Several outputs through one struct pointer: `out s1=*a2,t0=*a2+4`. An output written on some paths only is passed in and out: `in t0=*a2 ; out t0=*a2`. |
| `preserve REGS` | registers the asm leaves unchanged and its asm callers rely on. A default stub (no `in`/`out`) won't write them. |
| `clobbers REGS` | callee-saved registers (s0-s7, fp, f20-f31) the asm changes without documenting them as outputs. Not compared when this function is the one under test (outputs aren't either; they're compared through the mapping). Ranges work: `s0-s7`. |

**64-bit values.** `:8` marks a 64-bit value. On the asm side it is the whole
64-bit register; on the C side an o32 pair: `a0:8` = a0 (high word) : a1
(low), `a2:8` = a2:a3, `stackN:8` (N even) = two stack words, `ret64` (or
`ret:8`, `v0:8`) = v0:v1, `*a2:8` = 8 bytes through a pointer. So
`obj_fade_substitute_cmd: in ... ; out t4=ret64` gives a C caller of the stub v0:v1 and
compares all 64 bits, and `in t4=a0:8` passes a whole 64-bit t4 to the asm
and the a0/a1 pair to the C. Generated inputs are 64-bit random (or
`--arg t4=int64:..`; a 32-bit `--arg` value is sign-extended). `a1:8`,
`a3:8`, odd `stackN:8` and FPU registers are rejected.

**A gotcha with `--conv`:** a line with `in` but no `out` is a full convention
with *no outputs*, so a stub called from asm leaves v0 alone while one called
from C still gets the canned v0: the two builds then see different return
values. If the callee returns something, say so: `--conv 'NAME: in ... ; out v0=ret'`.

`#` starts a comment. A line with `in` or `out` is a **full** convention.
`python3 tools_port/draftconv.py func_X [func_Y ..]` drafts a line from the
asm survey (`funcs.json`: inputs, outputs used, conditional outputs, clobbered
s-registers, what asm callers rely on) with notes; check it against the asm
and edit before adding it. `--conv 'NAME: ...'` adds or replaces a line for
one run, `--conv-file`/`--no-conv` pick or disable the file. `runchecks`
refuses to start if the file doesn't parse.

**The function under test.** Every input gets the same value in its asm
register (reference) and its C slot (rewrite); where a register isn't the
other side's input it gets the value on both sides, so callee-saved
comparisons stay fair. `--arg` may name either (`--arg a0=..` or
`--arg s2=..`; if an asm input register is also a C slot of a different
input, `--arg REG` means the asm register). Pointer slots point at scratch
blocks outside the compared memory (0x80D80000..), pre-filled with poison and
holding the `*SLOT` inputs. After the run each output is compared as asm
register vs C result (`output t0=v0: build=.. build_nm=..`). `--ret`
defaults to `void` for a full convention, since the outputs are compared
instead. The reference/rewrite side is decided per build: a function is "asm"
in a build when its `GLOBAL_ASM` pragma is used there (always in `build/`; in
`build_nm/` unless it sits in the `#else` of `#ifdef NON_MATCHING`), else "C".
So `--new build` compares asm with asm, register by register.

**Callers.** When a stubbed callee has a full convention, the stub looks at
which code made the call (the function containing the calling block): from
asm it reads the arguments from the asm registers and writes its canned
outputs into the asm output registers *and nothing else*; from C it reads
a0-a3/stack/f12/f14 (through the pointer for `*SLOT` inputs) and writes v0
(`ret`), f0, or through the pointer argument. Arguments are recorded under
their C slot names, so the asm caller and its C rewrite compare.
`--follow NAME` needs nothing special: each build runs its own version (asm
with register inputs, C with o32).

```
# C rewrite of veh_has_partial_rider (asm: or s2,a0,0 ; jal veh_has_partial_rider_walk ; or v0,t0,0)
eqcheck.py veh_has_partial_rider --arg a0=int                         # callee stubbed: s2 vs a0, t0 vs v0
eqcheck.py veh_has_partial_rider --follow veh_has_partial_rider_walk --arg a0=..   # both builds run their own callee
```

### What is compared

1. **Outcome.** Did both runs return? A run that hits the same unmapped address or CPU exception in both builds counts as equivalent and is reported in the summary; the calls both made before the fault must still agree, but only up to the shorter of the two call lists (a load in a `jal` delay slot faults before the call is recorded in the asm and after it in a C version that loads after the call, so a call "in flight" at the fault doesn't count). Registers and memory aren't compared after a fault. A timeout in either build is a failure. An access to an address with no KSEG0/KSEG1/kuseg mapping (KSEG2, a non-sign-extended 64-bit address) counts as an unmapped access.
2. **Return registers.** Set with `--ret int` (default `v0`; but `void` when the function has a full convention, or when its NON_MATCHING C rewrite is defined `void NAME(`, with a `note:` saying so; `--ret int` overrides), `ptr`, `void`, `u64` (`v0`,`v1`), `float` (`f0`), `double` (`f0`,`f1`), or `regs:v0,t0,...` for non-ABI outputs. A function with a full convention has its outputs compared through the mapping (section 3a).
3. **Callee-saved registers** (`s0`-`s7`, `fp`, `sp`, `gp`, `f20`-`f31`). This catches hand asm that returns values in s-registers. Use `--ignore-reg R` or `--no-saved-check` to relax it. Registers that the function's convention lists as outputs or `clobbers` are skipped.
4. **Memory.** Every byte that differs from the start-of-run state in either run: game RAM (data, bss, globals), heap, caller frame, NM-only sections, and the MMIO register blocks. Bytes below the entry `sp` are the callee's private frame and are ignored, as are the 16 arg-home bytes at `0(sp)` unless you pass `--check-home`, and the function's own incoming stack-argument slots (see `--arg stackN`). Addresses that exist only in the new build are translated by symbol.
5. **Call sequence**, plus the MMIO access sequence with `--mmio-log`.

Values that are code addresses (function pointers in arguments or stored to
memory) are matched by symbol name, because NM text sits at a different address.

A failure prints the trial number, the generated inputs, and the first
difference, for example:

```
FAIL trial 11 (seed 0)
  inputs: gShownTargets = words 1,2,2,1,2,1,1,1,1,0; gMarkedTargetPts = 1
  memory: 1 byte(s) differ; first at 0x8036C7C7 gShownTargets+0x27: build=01 build_nm=00 (word @0x8036C7C4: 00000001 vs 00000000)
```

To reproduce a failure, rerun with the same `--seed` (and `-n` covering that
trial). `-v` lists more differing bytes and the event lists. `--trace` prints
every basic block both builds execute.

### Other options

`-n N` sets the number of trials. `--seed S` sets the seed. `--max-fail K` keeps going after the first failure. `--ref DIR` and `--new DIR` pick the builds (use `--new build` to compare a build with itself). `--max-insns` (default 2M) and `--timeout` (default 10 s per run) bound each run. `--mmio 0xA4400010=0x200` presets an MMIO word. `--ignore-new-only` skips writes to NM-only memory.

`tools_port/demo.sh` runs the demo set below and works as a smoke test.

## 4. Regression suite

Every rewrite's eqcheck runs are checked in, so any later change (a struct
refactor, a shared header, an eqcheck change) can be re-verified in one go.

**Spec format.** One file per source file, `tools_port/checks/<FILE>.txt`
(for `src.us.v11/hd_code/<FILE>.c`; `checks/fe_<FILE>.txt` for the front end's
`src.us.v11/hd_front_end/<FILE>.c`, so names can't clash). Each line is one
eqcheck run:

```
# comment
radar_set_marker: -n 300 --ret void --mem gRadarMarkers:500=words:-1,0,1,2 --arg a0=choice:-1,0,0x100
radar_set_marker: -n 200 --ret void --seed 4 --mem gRadarMarkers:500=onehot:0x14:-1:words:0,1,2 \
    --arg a0=choice:0,3,0x100
```

A function can have any number of lines (a random run plus boundary runs);
give each line of the same function its own `--seed`. Arguments are split
shell-style (quotes work) but nothing is expanded, so write values out instead
of using `$VARS`. A trailing `\` continues a line.

**Running.**

```bash
bash tools_port/runchecks.sh            # everything, plus the coverage check
bash tools_port/runchecks.sh -b         # make NON_MATCHING=1 first
bash tools_port/runchecks.sh -B         # matching build (checks both OK lines) + NON_MATCHING first
bash tools_port/runchecks.sh 8A080 cam_apply_height_zone   # filter: check file, function, or substring
bash tools_port/runchecks.sh -q 20      # quick: cap every run at 20 trials
bash tools_port/runchecks.sh --list 60D50          # show the selected command lines
bash tools_port/runchecks.sh --coverage-only
bash tools_port/runchecks.sh -b --changed          # only files whose .c or check file differs from main
bash tools_port/runchecks.sh --changed --since HEAD~3
```

`--changed` selects check files whose `src.us.v11/hd_code/<FILE>.c` (or
`src.us.v11/hd_front_end/<FILE>.c` for `fe_<FILE>`) or
`tools_port/checks/<FILE>.txt` differs from the merge base of `--since`
(default `main`) and HEAD, counting committed, staged, unstaged and untracked
changes, and reports any rewrite in those files that has no check line. It doesn't notice
changes to shared headers or to eqcheck itself; run the whole suite for those.

Runs go in parallel (`-j`, default min(cores, 8)) on a pool of worker
processes. Each worker imports eqcheck once and runs many check lines
in-process (`eqcheck.run_captured`), keeping the parsed ELFs and the two
emulator machines between lines: a reused machine is reset to its load-time
state (every page written since, inside or outside the diffed regions, is put
back, and the CPU context restored), and `tests/test_eqcheck.py --real` checks
that pooled results equal fresh-process results. Lines are scheduled longest
first. `--no-pool` runs each line in its own `eqcheck.py` process as before;
if a worker dies, the lines it had are rerun that way automatically. The output is a table with one row per run (file,
function, line, PASS/FAIL/ERROR, trials, time). A failing run shows the
eqcheck failure lines and a ready-to-paste `bash tools_port/eq.sh ...` rerun
command. A run in which every trial faulted counts as a FAIL. The exit status
is nonzero on any failure.

**Coverage.** Without a filter the runner also lists every function whose
`#pragma GLOBAL_ASM` sits in the `#else` branch of an `#ifdef NON_MATCHING`
block in `src.us.v11/hd_code/*.c` or `src.us.v11/hd_front_end/*.c` (counted
per segment) and reports any with no check line
(`MISSING`, which makes the exit status nonzero). It also notes check lines for
functions that have no rewrite. With `-B` the matching build must print all
three `: OK` lines.

Current suite (Oct 2026): 681 hd_code + 9 front-end rewrites, 1789 runs, all
PASS, about 4 minutes wall at `-j4`. (Earlier: 386 functions, 935 runs, about
2 minutes wall at `-j4`; 6.3 minutes before the worker pool, TLB write
tracking and the emu_start timeout change, see section 6; 148 runs took
637 s at `-j4` before the earlier restore/diff speedup).

**Harness tests.** `python3 tools_port/tests/test_eqcheck.py` runs the
harness's own regression tests on tiny hand-assembled functions (delay-slot
fault state reset, TLB fills in delay slots, faults around a `jal`, own
stack-argument slots, 64-bit conventions, `--stub-ret seq:`, `hex:`, `--sig`
widths); `--real` adds the pool-vs-fresh-process comparison on real check
lines. Run it after changing eqcheck.

## 5. Writing checks for a new rewrite

1. **`--explore` first.** Note every global, heap and stack-argument byte read
   and every call. Re-run it with your inputs to confirm they're the ones read.
2. **One random run, 200-400 trials**, randomizing every input the function
   reads, with pools that reach every branch (`choice:`, `words:`, weights).
   **Pre-fill the outputs with `rand` too** (every global, field or buffer the
   function writes): the diff only sees bytes that *change*, so a store of 0
   into memory that is already 0 (the default bss) is invisible, and a rewrite
   that drops it passes.
3. **Size byte and halfword globals.** An unsized `--mem SYM=VAL` writes 4
   bytes. eqcheck sizes it from the ELF when the symbol has a size, and warns
   when the next symbol is closer than 4 bytes; otherwise write `SYM:1=`/`SYM:2=`
   yourself, or the word spills into the neighbouring globals.
4. **Explicit boundary runs** (each with its own `--seed`): 0, -1, NULL;
   sentinel values and their neighbours; table full and table empty; only the
   first or only the last slot special (`onehot:`); 0x7FFF / 0x8000 / 0xFFFF
   for halfwords, 0x7FFFFFFF / 0x80000000 for words; signed vs unsigned shifts
   (negative inputs). Random pools almost never hit "exactly one slot special
   at the boundary" (the pilot's off-by-one loop bound passed 600 random trials).
   For a field in every record of a table use the stride form
   (`--mem TBL+8*0x14:25/2=choice:..`); for memory reached only through a
   global pointer use `--heap SIZE` plus `--mem PTR=rel:heap0`.
5. **Terminate scanned tables.** A table walked until a sentinel must have one
   in `--mem` (with the default zero bss the walk runs off through megabytes).
   Loops with `!=` bounds need inputs that respect the precondition.
6. **`--ret void` for void functions** (eqcheck now assumes it when the C
   rewrite is defined `void NAME(`; otherwise leftover v0 is compared);
   use the real width (`--ret int` when the asm returns the full 32-bit value).
7. **`--sig NAME=a0,a1`** for callees that take fewer than 4 arguments, so
   garbage in unused argument registers isn't compared.
   Use `--sig NAME=` for a callee that takes no arguments, and `REG:W` for
   narrow ones (`--sig NAME=a0:1,a1:2`).
   A callee whose result picks the branch: `--stub-ret NAME=seq:0,1` or
   `choice:0,1,5` so one run covers every path.
8. **Non-ABI interfaces.** If the function (or a callee) takes or returns
   values outside o32, add its line to `conventions.txt` (start from
   `python3 tools_port/draftconv.py NAME`), write the C with ordinary
   parameters/return/pointer outputs, and check it directly (inputs aliased,
   outputs mapped) and from a rewritten caller, both stubbed and with
   `--follow`. Keep the "asm callers rely on preserved" comment in the C.
9. **Static helpers are fine.** A rewrite may split work into `static`
   functions: callees that exist only in `build_nm` are run, not stubbed, so
   they don't show up as extra calls. (Helpers must not share a name with any
   function in the matching build, or they'll be stubbed and compared as calls.)
10. **Mutation spot-check.** Break the C once on purpose (loop bound, a
   dropped check, `<` for `<=`, a dropped store of 0), rebuild NON_MATCHING,
   confirm at least one check line FAILs, then revert and rebuild.
11. Add the lines to `tools_port/checks/<FILE>.txt` and run
   `bash tools_port/runchecks.sh -b <FILE>` (or `-b --changed`), then the whole
   suite before you commit.

## 6. Demo results (Oct 2026)

| function | kind | comparison | result |
|---|---|---|---|
| hud_sprite_setup (GBI setup, writes a display list) | matched C | build vs build / vs build_nm | PASS 50 / 100 |
| hud_target_was_shown (search a 10-entry table) | matched C | vs build_nm | PASS 100 |
| hud_remember_marked_target (insert into a free slot) | matched C | build vs build / vs build_nm | PASS 50 / 100 |
| hud_mtx_xform (matrix × vec4, `--follow guMtxL2F`) | matched C | vs build_nm | PASS 100 |
| hud_remember_marked_target, loop bound changed to 9 | broken | vs build_nm | FAIL trial 11: `gShownTargets+0x27` differs |
| hud_target_was_shown, NULL check dropped | broken | vs build_nm | PASS with pool {1,2}; FAIL trial 1 with {0,1,2}: `$v0` 0 vs 1 |
| hud_sprite_setup, wrong geometry mode | broken | vs build_nm | FAIL trial 0: `heap+0xE` 02 vs 00 |
| radar_clear_markers (fills a 25-entry table) | hand asm, C rewrite | vs build_nm | PASS 50 |
| barge_enter (sets 2 globals, calls veh_start_engine_sound(a0, 0x72)) | hand asm, C rewrite | vs build_nm | PASS 200 |
| veh_tris_all_solid_impl (id in v0, result in v1), math_atan (v1 -> fp), veh_has_partial_rider_walk (s2 -> t0, recursive) | non-ABI asm, C rewrite via conventions.txt | vs build_nm | PASS 300 each |
| veh_tris_all_solid / math_atan_c / veh_has_partial_rider (wrappers of the above) | callee stubbed under the mapping / `--follow` | vs build_nm | PASS 200 / 300 |
| veh_tris_all_solid_impl without the byte-0x51 test; veh_has_partial_rider passing id+1 | broken | vs build_nm | FAIL: `output v1=v0` 1 vs 0; `event #0 differs: veh_has_partial_rider_walk(a0=0x9) vs (a0=0xA)` |
| obj_calc_target_time (calls math_dist3: cvt.d.l/sqrt.d/cvt.l.d) | FPU long ops emulated; also `--model` | vs build_nm | PASS 300; z read from the wrong word: FAIL |

A trial (both builds) takes about 2 ms for a small function (5-10 ms before
Oct 2026), and a fresh process starts in about 0.4-0.6 s (paid once per
runchecks worker, not per line). Guest RAM is mapped from host buffers
(`mem_map_ptr`). **Write tracking via the soft TLB:** the machine runs with
`UC_TLB_VIRTUAL` and its own fill hook (KSEG0/KSEG1 to physical, kuseg 1:1 as
the R4000 does with Status.ERL set); the TLB is flushed before every run and a
page is entered writable only on a store miss, so the set of written pages is
exact, and after the run only those pages are memcmp'd against the
start-of-run snapshot (then bytes inside changed pages). Before that, every
run memcmp'd all ~7 MB of writable regions, about half of each trial's time.
Before the next run only the pages that changed or held inputs are copied
back. `EQCHECK_VERIFY_DIFF=1` cross-checks every run against a full memcmp
(the whole suite passes it); `EQCHECK_FULL_DIFF=1` turns the TLB tracking off.
Registers are written and read with one batch call each (straight through
the C API). `--timeout` is checked in the block hook (every 4096 blocks)
rather than passed to `emu_start`, whose timeout starts a timer thread per
call (about 0.3 ms, more than a small trial's emulation); `--max-insns`
still bounds every run. Big `--mem` fills (`rand`, `words:`/`halves:`/`bytes:`
of more than 64 values) are drawn in one RNG call; small ones, the caller
frame and `ptr` blocks are drawn as before, so existing check lines keep
their inputs. The parsed ELF symbol tables are cached
in `<build dir>/.eqcheck_cache.<version>.pickle`, keyed by the ELFs' size and
mtime. (Before Oct 2026 every trial copied and compared about 6 MB through
`mem_read`/`mem_write`: about 33 ms per trial single-threaded and much more
under `-j4`; the 148-run suite went from 637 s to 69 s at `-j4`.)

## 7. Known limitations

* **Equivalence is only as good as the inputs.** Randomize every global the function reads (`--explore` lists them), and use value pools that reach the edge cases: NULL, 0, negative values, counts at their bounds.
* **No hardware.** MMIO (`0xA4xxxxxx`: SP, DP, MI, VI, AI, PI, RI, SI) is plain memory. A write then a read returns the written value, nothing has side effects, DMA never happens, and status registers read whatever you preset with `--mmio`, otherwise 0. A loop polling a busy bit therefore exits at once or spins until `--timeout`. RSP and RDP code (microcode, `osSpTask*`) and cartridge or PIF space (unmapped, so access faults) can't be tested this way. Final MMIO state is compared; the order of accesses is compared only with `--mmio-log`.
* **Interrupts, threads, and the OS** don't exist. Stub calls into libultra (the default), or follow only pure ones such as `guMtxL2F`.
* **FPU.** Runs as R4000 with Status.FR=0, as the game does: libultra's osCreateThread gives every thread SR = IMASK|IE|EXL, the exception handler only ORs in CU1 on first FPU use, and IDO's o32 code keeps doubles in even/odd pairs. The VR4300 still executes the long-integer ops (`cvt.d.l`, `cvt.s.l`, `cvt.l.d`/`.s`, `round/trunc/ceil/floor.l`) in that mode, on an even/odd pair (used by math_dist3 and libultra's `__ll_to_d` family). QEMU raises a reserved-instruction exception for them unless FR=1, so eqcheck replaces each such word in the loaded code with a nop and emulates it in a per-address code hook (delay slots included). Conversions use the FCSR rounding mode for `cvt.l`; `cvt.d.l`/`cvt.s.l` round to nearest. A NaN, infinity or out-of-range source (where the VR4300 raises an unimplemented-operation exception) ends the run as "can't compare". The FCSR starts at 0 (round to nearest, no traps). The harness compares raw bits, so a different NaN payload or a different but legitimate rounding order (for example `a*b+c` evaluated in another order) shows up as a difference. Inspect those by hand. Results under flush-to-zero or unimplemented-operation exceptions follow QEMU, not the VR4300.
* **64-bit registers.** The CPU runs in 64-bit mode, because the hand asm uses `sd`/`ld`/`dsll`. Inputs are sign-extended 32-bit values unless given as `int64` or declared `:8` in a convention. Register comparisons use the low 32 bits, except convention inputs/outputs declared `:8` (`ret64`, `a0:8`, ...), which compare all 64 bits; functions passing true 64-bit values in a single register without such a declaration aren't fully checked.
* **Non-ABI functions** are mapped through `conventions.txt` (section 3a). What it can't express: gp or fp used as a global base pointer rather than an argument (draftconv flags `gp_offsets`; the C reads the globals directly, so don't map them); outputs whose *register* depends on the path (not just whether it is written); values passed in hi/lo or the FPU condition flag; 64-bit FPU (double) values; and asm callers that rely on registers a C callee doesn't preserve (`asm_callers_rely_on_preserved`: a mixed N64 build would need a thunk; the harness's C side doesn't model it). Pointer-slot outputs/inputs (`*aN`) are new and so far exercised only by asm-vs-asm runs and the parser; the three demo rewrites use register/`ret` mappings.
* **Call detection is entry-address based.** Asm that falls through into the next function, or jumps into the middle of another function, isn't seen as a call. The fall-through target becomes a recorded call only in the rewrite, which looks like a spurious difference. Fix it with `--follow` on that target.
* **Data blobs keep original code addresses.** A word in a raw `.bin` blob that points into the text points at the original text. The NM machine is pre-filled with the original text at its old address, so such paths still execute (the original code). Function entries reached that way are translated by name, and other blocks print a `note:`. (An Oct 2026 scan found no function pointers in the hd_code or front-end data at all, only compiler jump tables and libultra's `__osException` table; the text-range words outside those were pointers to the data tables inside the text bins, now C, see below.)
* **The data tables inside the hd_code text bins** (68810, 690C0, 7D9D0_data, 800E0_data, 8E910: gAtanTable, the arcsine tables, the texture-animation entries and effect definitions with their lists gTexAnimDefList/gFxDefsByKind, the particle definitions and gParticleDefs, the per-level part lists) are defined in C in the NON_MATCHING build (69000.c, 69930.c, 7F8B0.c, 7FB50.c, 8DDB0.c, typed so they read the same on either byte order) and linked back at their original addresses (Makefile `NM_PIN_HD_CODE`, `nm_ldscript.py --pin-object`, sections `.nm_pin_<ADDR>_<SIZE>`), so the absolute `D_` symbols, pointer words elsewhere, the front end and the harness see them where the original has them. `nm_symaudit.py` (run by `runchecks -b/-B`) fails if their bytes differ from the bins; `AddrMap` treats those sections as unmoved; `nm_rom.py` copies them into image B. Only the RSP microcode (A0C30) is still read from the original text.
* **Infinite loops.** These are caught by `--max-insns` and `--timeout`. They are reported as a failure if only one build loops, and as "can't compare" if both do.
* **unicorn delay-slot bugs (worked around).** (1) In unicorn 2.1, any hook that fires for a load or store in a branch delay slot (`UC_HOOK_MEM_READ`/`WRITE`, and `UC_HOOK_TLB_FILL`) corrupts MIPS execution: before calling the hook unicorn rolls the CPU back to the instruction (`cpu_restore_state`), which ORs the branch bits into `env->hflags`; the code after the delay slot never clears them, so the next block runs as if it were in a delay slot (an RI exception or a wild jump). eqcheck's TLB-fill hook repairs this (`Machine._delay_slot_fix`: when the instruction before the faulting pc is a branch, it clears the bits in a saved context and restores it; `calibrate_hflags()` finds hflags' offset in the context once per process by faulting in a beq/bne delay slot; if that fails, write tracking falls back to a full memcmp). `--explore` and `--mmio-log` still decode loads and stores in a per-instruction code hook; don't add `UC_HOOK_MEM_READ`/`UC_HOOK_MEM_WRITE` hooks. (2) A run that *ends* with a fault in a delay slot leaves the same branch state (hflags, btarget) behind, so the next `emu_start` used to execute one instruction at the new entry and then jump to the old branch target (seen as a 4-byte block followed by the previous trial's faulting code). Every run now starts from a clean CPU context saved when the machine was created.
* `init`, `hd_code` and the front end are loaded (the front end at its 0x801E7000 overlay address). Calls into other code (level overlays, NULL pointers) are stubbed as `sub_XXXXXXXX`, and reading their data faults or returns 0. Since Oct 2026 an hd_code check with `--follow-all` that reaches a front-end function runs it for real (before, nothing was loaded there).

## 8. Tracing the real game (`m64trace/`)

For values the harness can't know (registers a C caller leaves behind, what the
game really passes), `m64trace/m64trace.py ROM SPEC.py OUT.txt` runs a ROM in
mupen64plus (ctypes over `libmupen64plus.so.2`, glide64mk2 so the frame loop
advances; with no input the attract demos 0-8 reach nine levels in about 8
minutes of game time, and section 8.1 adds scripted input) and logs registers, FPRs and memory at exec breakpoints without
stopping. The spec format is in the file's docstring; `spec_hd_calls.py` and
`spec_spawn.py` are the runs behind the Oct 2026 leaked-register audit.
`summarise.py OUT.txt` prints the distinct values per breakpoint and register,
`dumpcmp.py` diffs RAM dumps (base ROM vs the NM test ROM). For the NM test ROM
use addresses from `build_nm/hd_code.rom.us.v11.elf`.

### 8.1 Scripted input and save states

Without input the game only reaches the attract demos. A spec that defines
`INPUT` gets a real controller: `m64trace.py` then attaches `m64input.so`
instead of input-sdl. That is a small mupen64plus input plugin
(`m64input.c`, written from the public plugin API, built with gcc into
`~/.cache/m64input-<md5 of the source>.so` on first use) whose controller-1 state is a global the
front end sets through ctypes. The game reads it through its normal path
(PIF -> `osContGetReadData` -> `cont_update`), so the front end, gameplay and
the recording code all see it.

```python
A, B, Z, START = 0x8000, 0x4000, 0x2000, 0x1000   # N64 button word
def INPUT(vi, read, ctl):          # once per VI; returns (buttons, stick_x, stick_y)
    if read(0x80364A90, 8) == 4:   # game mode 4 = playing (read sizes 1/2/4/8)
        ...
    return START if vi % 90 < 6 else 0, 0, 0
```

`ctl` has `log(text)` (a `#in vi=N` line in the output), `save(path)` /
`load(path)` (mupen64plus save states, taken at the next VI; they work with the
debugger and breakpoints), `stop()`, `write(addr, size, value)`, `polls` (the
game's controller reads so far) and `vars` (script state). `LOADSTATE = path`
starts a run from a save state; `WATCH = [(addr, size)]` logs a `#w` line
whenever one of them changes (game mode, level, vehicle ...), which is how
scripts find out where they are (the screenshots are black). `ONHIT` may
return None to drop a hit (e.g. log a shared function only inside one caller).

Reaching gameplay: from boot, pulse START and A alternately (6 frames out of
every 90) until `gGameMode == 4` (mode 4 = playing; front end/menus are other
modes, 0x100 is the pause menu, 0x800 the level intro) with `gPaused == 0`
and a vehicle in `gCurVehicle`: that is a new game on level 0 (Simian Acres,
vehicle 4), about 1500 VIs (~25 s of game time, under a minute of wall time).
Save a state there and later runs start from it in a second. Controls in game:
A accelerate, B brake/reverse, stick steers, Z gets out of the vehicle (when
it is stopped), START pauses.

Examples: `spec_drivein.py` boots, enters level 0, saves
`~/drivein_lvl0.st` and gets out of the vehicle (Z), logging vehicle 0's
drive-in calls; `spec_drivein_trials.py` starts from that state and runs 40
trials (load the state, drive with some steering for a while, stop, Z), which
puts the drive-in checks beside buildings. Input is a pure function of the VI
count and RAM, so a run replays exactly (same ROM, same emulator settings).

For stage 3 (headless game logic vs the emulator) the same plugin gives
recorded input: log the per-VI `(buttons, x, y)` an INPUT script returns (or a
human's input-sdl session, by reading `gContPad`, the game's copy of the
pad, at each frame) and play it back as a table; feed the headless build the
same pad words per game frame. Note the game itself reads the pad once per
game frame (`cont_update`), not per VI, so compare by game frame
(`gFramesInMode` counts them per mode) rather than by VI when timing differs
(the NM build runs slower and drops VIs differently).

`spec_input.py` does that: `M64INPUT=file` replays a bc_headless `--input`
file keyed by the game's controller reads; `M64DUMP=addr:len,...` logs
memory at every game-mode change (and `M64DUMPMODE`/`M64DUMPEVERY` every n
reads in one mode); `M64PRINT=1` logs the game's debug messages
(debug_printf, like bc_headless `--print`); `M64WRITE=vi:addr:size:val`
pokes RAM (`100:802FA268:4:1` = the `-c` debug cheats). Saves:
`M64SAVEDIR=dir` keeps the .eep/.mpk files there instead of the user's
mupen64plus save dir, `M64PAK=1` plugs a Controller Pak into controller 1.
