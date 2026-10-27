# ROM versions

The decomp builds two versions of the game from one source tree:

| version  | ROM                       | `baserom.<version>.z64` sha1               |
| ---      | ---                       | ---                                        |
| `us.v11` | Blast Corps (USA) (Rev 1) | `483f7161aea39de8b45c9fbc70a2c3883c4dea8c` |
| `us.v10` | Blast Corps (USA)         | `185a6ef7ba1adb243278062c81a7d4e119bda58c` |

US 1.1 is the reference version: every name, comment, NON_MATCHING rewrite
and port check is written against it. US 1.0 is built from the same C, with
`#if VERSION_US_V10` where the code really differs.

## Building

Each version needs its own ROM at the top of the repo (`baserom.us.v10.z64`,
`baserom.us.v11.z64`). The steps are the same for both; pass the version
explicitly (both Makefiles default to `us.v11`).

```
make VERSION=us.v10 extract                 # split the ROM (assets/us.v10/, asm/us.v10/) and inflate
                                            # blastcorps/{init,hd_code,hd_front_end}.us.v10.bin
make VERSION=us.v10                         # the ROM from its pieces: build/blastcorps.us.v10.z64: OK
make VERSION=us.v10 -C blastcorps extract   # splat: asm/us.v10/
make VERSION=us.v10 -C blastcorps           # three OK lines (build/us.v10/)
```

and the same with `VERSION=us.v11`. The matching build of each version
prints one OK line per segment:

```
build/us.v10/init.us.v10.bin: OK
build/us.v10/hd_code.us.v10.bin: OK
build/us.v10/hd_front_end.us.v10.bin: OK
```

`NON_MATCHING=1`, `SHIFT=1` and the Windows port are us.v11 only so far (see
the end of this file for what US 1.0 would need).

## Layout

- **Sources:** `blastcorps/src.us.v11/` is the one source tree. Every
  version compiles it; the name stays because the port, runchecks, rename.py,
  CI and `port/inputs` all refer to it.
- **us.v11** keeps the original paths: `asm/`, `assets/`, `build/`, and its
  yamls, `symbol_addrs.*.us.v11.txt`, `undefined_syms.*.us.v11.txt` and
  `*_bss.us.v11.ld` by hand, as before.
- **Another version** extracts into `asm/<version>/` and `assets/<version>/`
  and builds into `build/<version>/` (all git-ignored, like us.v11's). The
  `GLOBAL_ASM` pragmas name `asm/nonmatchings/...`; the Makefile compiles a
  copy of each such file with the paths pointed at `asm/<version>/`
  (`ASM_SRC`, under `build/<version>/`).
- **Its yamls, symbol files and `.bss` linker scripts are generated** from
  us.v11's by `tools_port/vermap.py` and committed:
  `make VERSION=us.v10 -C blastcorps port-version` (needs both ROMs and the
  us.v11 matching build's ELFs in `build/`). Names are us.v11's everywhere
  (a function still called `func_<address>` keeps its us.v11 address in
  the name in both versions); only the addresses differ. Run it after changing a us.v11 yaml
  or symbol file, then `extract` the other version again. `rename.py`
  renames inside the generated files too, so a rename needs no
  regeneration.
- `tools_port/vermap_overrides.us.v11-us.v10.txt` holds the few addresses
  the code comparison can't settle (variables only the changed code uses);
  `none` means the symbol doesn't exist in that version.
- **Conditionals:** `include/version.h` (from `common.h`) defines
  `VERSION_US_V10` and `VERSION_US_V11` to 1 or 0 (the Makefile passes
  `-DVERSION_US_V1x=1`; a build that passes neither, the port, is US 1.1).
  Write `#if VERSION_US_V10 ... #else ... #endif` with US 1.1 in the `#else`.
  Raw numeric addresses in the C are US 1.1's; where they move, spell them
  through a macro (`HD_JTEXT_ADDR`, `TNT_RADIUS_ADDR`).
- A PAL or JP build would be one more version the same way: its ROM walk in
  `tools_port/romsplit.py` (bcfiles' offsets), `port-version`
  for its blastcorps/ files, and conditionals. Those versions differ far
  more (other text, other levels), so expect many more changed functions and
  overrides.

`make VERSION=us.v10 -C blastcorps version-report` prints the comparison.

## US 1.0 against US 1.1

Code (functions of the us.v11 matching build, by tools_port/vermap.py;
"shifted" = the same instructions, only relocated addresses differ):

| segment      | text size (1.1 / 1.0) | identical | shifted | changed |
| ---          | ---                   | ---       | ---     | ---     |
| init         | 0x3A40 / 0x3A40       | 65        | 1       | 0       |
| hd_code      | 0xA4410 / 0xA4360     | 108       | 1291    | 4       |
| hd_front_end | 0x21040 / 0x21010     | 0         | 151     | 5       |
| total        |                       | 173       | 1443    | 9       |

Four of the shifted functions differ only in the nops that pad them to the
next object's 16-byte start (lvl_align_end, ground_scan_guard_tris,
driver_load_state, fe_best_draw_icon), and init's one shifted function (the
boot code) holds the code files' ROM offsets. In the front end, the
level-select globe and solar-system object (09570.c and 0C450.c, 14
functions) is linked later in US 1.0, after 17990.c, with its .data and
.rodata. All other data is the same apart from addresses, except one
Japanese text string in hd_code's .data (US 1.1 0x803035D0) that was
edited: it is 4 bytes longer in US 1.1, which moves the text tables after
it by 4 bytes, up to the next 16-byte-aligned block at 0x80304A90.

Outside the code the ROMs are the same apart from the header, the
compressed lengths of the code files (init holds them) and the gzip time
stamps of four level files (chimp.raw and trucker3US.raw with their display
lists), which were recompressed with identical contents.

### What US 1.1 changed

| function (file) | change in US 1.1 |
| --- | --- |
| veh_respawn_if_out (hd_code 62740, hand asm) | Extra out-of-bounds tests for the train (vehicle 7): on Simian Acres (level 0) it is respawned when x < 1000, on Beeton Tracks (18) when z < 900 and on Ebony Coast (13) when z < 1100, besides the level's normal bounds. |
| driver_try_exit (hd_code 69BB0, hand asm) | Sets a new flag, gCollExitProbe, while it probes for a place to get out of the vehicle, and clears it after. |
| obj_hit_tri (hd_code 77E20, hand asm) | Skips the rocking-hit test (obj_detect_rocking_hits, which can add the hit) while gCollExitProbe is set, i.e. during the exit probe's collision tests. |
| lvl_read_header (hd_code 5CB60, hand asm) | Clears gCollExitProbe when a level loads. |
| fe_digger_init, fe_digger_frame (front end 00000, digger_loop.c) | The vehicle select remembers the level's vehicle (gDiggerPrevVehicle) and B (back) restores it; US 1.0 kept whichever vehicle was browsed to. Assert line numbers move. |
| fe_best_build_menu, fe_best_player_listed, fe_best_color_items (front end 10850, bestTimes.c) | The best-times menu keeps its own count of the time rows (gBestItemCount = rows x 4). US 1.0 used the menu header's item count, which on a timed mission includes the blank last item, so the player highlight loops also coloured that item. |

The four hand-asm functions need no source change: their GLOBAL_ASM comes
from each version's own ROM. The three variables new in US 1.1
(gCollExitProbe, gDiggerPrevVehicle, gBestItemCount) are `none` in the
override file.

## What US 1.0 would need for the port and SHIFT=1

- **Port (bc.exe on a US 1.0 ROM):** a `NON_MATCHING=1` build for us.v10
  (nm_ldscript.py and the pin lists use us.v11 addresses); `#if
  VERSION_US_V10` versions of the four changed hand-asm functions' NON_MATCHING
  C (lvl_read_header, veh_respawn_if_out, driver_try_exit, obj_hit_tri);
  `port/inputs` per version (the addresses, typemap and maps are us.v11's)
  and the host code's and port/data's us.v11 addresses through them; the
  loader's ROM checks (SHA-1, the code files' ROM offsets and lengths); and
  the eqcheck suite and the verify traces run against US 1.0.
- **SHIFT=1:** romsplit.py's units for US 1.0, shiftbuild.py's reference
  build in `build/us.v10/` instead of `build/`, the raw-reference overrides
  (tools_port/shift/overrides.txt) at US 1.0's addresses (vermap.py can map
  them), the Makefile's us.v11-only guard lifted, and a shift test with US
  1.0's attract-demo RAM.
