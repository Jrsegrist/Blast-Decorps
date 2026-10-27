# Files

Blast Corps has no file system in the usual sense: no directory, no file
names at run time, no file ids except for textures and buildings. The ROM is
a list of separately compressed blobs ("files"), and the game finds each one
in one of three ways:

1. **By a ROM offset built into the code.** Levels, vehicle models, title
   pictures, sound banks, the front end and so on are linker symbols such as
   `romChimpRaw` or `romMusicBankCtl`. The code gets a file's length as "the
   next file's start minus this file's start".
2. **Through the texture table** at ROM 0x4CE0: 4096 eight-byte entries, one
   per texture or palette. Display lists, sprites, fonts and effects name
   textures by their index in this table.
3. **Through the packed-object table** at ROM 0x6EC4C0: 512 offsets to the
   buildings and other placed level objects. A level's placement list names
   objects by their index in this table.

Almost everything is compressed: the big files are gzip (deflate) members,
some are Rare's LZSS, and textures use seven small custom packings of their
own. This document covers the ROM layout, the tables, the loaders and the
formats of the file kinds. The decompressors themselves are described in
[memory](memory.md); the sections of a level file are described in
[levels](levels.md).

See also: [overview](overview.md), [memory](memory.md), [levels](levels.md),
[rendering](rendering.md), [frontend](frontend.md), [audio](audio.md),
[vehicles](vehicles.md), [destruction](destruction.md),
[versions](../versions.md), [the shiftable build](../shiftable.md).

---

## 1. ROM layout (US 1.1)

All offsets below come from the code (symbol files, `undefined_syms.*.txt`,
constants in the C) or the splat yamls. The ROM is 8 MB.

| ROM range | what | how it is read |
| --- | --- | --- |
| 0x000000-0x000040 | cartridge header (entry point 0x8021ED00, CRCs) | by the console |
| 0x000040-0x001000 | IPL3 boot code (CIC-6102) | by the console |
| 0x001000-0x004CE0 | `init`: boot code and its data, stored uncompressed | copied to 0x8021ED00 by IPL3 |
| 0x004CE0-0x00CCE0 | **texture table** (0x8000 bytes, 4096 entries) | `tex_load_table`: one raw DMA |
| 0x00CCE0-0x350950 | textures and palettes, each packed on its own | `tex_get` and friends, through the table |
| 0x350950-0x3539A0 | music sound bank `.ctl` (LZSS) | `music_init_audio` (`romMusicBankCtl`) |
| 0x3539A0-0x3A1920 | music samples `.tbl` (raw ADPCM) | streamed by the audio DMA callback (`romMusicBankTbl`) |
| 0x3A1920-0x3A48C0 | effects sound bank `.ctl` (LZSS) | `romSfxBankCtl` |
| 0x3A48C0-0x44F5C0 | effect samples `.tbl` | `romSfxBankTbl` |
| 0x44F5C0-0x487050 | sequence file (libultra `ALSeqFile`) and the tunes, uncompressed | `romSequenceFile`, `music_play_tune` |
| 0x487050-0x48FE90 | whole images: title logo, title picture, boot banner, boot icon, copyright (gzip) | `romTitleLogo` ... `romCopyright` |
| 0x48FE90-0x4A5660 | vehicle models and two other models, each a pair `X.raw` + `X_dl.raw` (gzip) | `lvl_load_model`, `lvl_load_model_plain` (offsets in the code) |
| 0x4A5660-0x66C900 | the 60 levels, each a pair `X.raw` + `X_dl.raw` (gzip) | `lvl_load_file` (`romLagpRaw` ... `romLevel59Raw`) |
| 0x66C900-0x6A9F10 | whole images: globe textures, attract pictures, traffic lights, controller picture (gzip) | `romWorldTexturesRaw`, `romAttractPictures`, `romTrafficLights`, `romControllerPicture` |
| 0x6A9F10-0x6AD3F0 | attract-mode demo recordings (gzip) | `rec_start_playback` (`romDemoRecordings`) |
| 0x6AD3F0-0x6E8980 | three front-end background pictures (LZSS) | `fe_bg_load` (`romFeBgPicture1/2/3`) |
| 0x6E8980-0x6EC4C0 | three front-end 3D scenes, each a pair (gzip) | `fe_scene_load` (`romUsaStarScene`, `romNinLogoScene`, `romReflectLogoScene`) |
| 0x6EC4C0-0x6ECCC0 | **packed-object table** (0x800 bytes, 512 offsets) | `lvl_load_object_table`: one raw DMA |
| 0x6ECCC0-0x787F35 | buildings and other placed objects, each a pair (gzip) | `lvl_load_object`, through the table |
| 0x787F40-0x788000 | the static segment (segment 1 display lists, LZSS) | `hd_mode_setup` (`romStaticSegment`) |
| 0x787FD0-0x7E3AD0 | `hd_code` text and data: two gzip members back to back | inflated by init at boot |
| 0x7E3AD0-0x7F9BDD | front end (`hd_front_end`) text and data: two gzip members | `hd_load_front_end` |
| 0x7F9BE0-0x800000 | 0xFF fill | - |

Notes on the table:

- The ranges marked "pair" are two gzip members stored back to back that the
  game always inflates together into one contiguous block (section 3).
- The static segment's range overlaps the start of `hd_code` in the list
  above because `romStaticSegmentEnd` is 0x788000 while `hd_code` starts at
  0x787FD0: `hd_mode_setup` DMAs 0xC0 bytes from 0x787F40 (so the last 0x30
  bytes are the start of `hd_code`'s gzip member) and runs LZSS over them.
  The LZSS stream's end marker does come before those bytes: the stream is
  0x82 bytes (0x787F40-0x787FC2) and decodes to the 0xC0-byte segment
  (checked with `tools_port/bcfiles`, [tools](../tools.md)); the yamls don't
  split the static segment out at all (0x787F40 is one of the shiftable
  build's extra unit starts).
- `hd_boot` also reads a 16-word debug command line from PI address
  0xFFB000, past the end of the 8 MB ROM (`parse_cmdline` in
  `src.us.v11/hd_code/00000.c`). On a retail cartridge nothing is there.
- The zeros after reflectlogo's pair (from 0x6EC4B1, just before the object
  table) and after the last building (endsid5, from 0x787F35) are padding:
  the next file starts on a 16-byte boundary. (Older splits of the ROM put
  their boundaries at 0x6EC4B4 and 0x787F37.)
- From the title logo (0x487050) to the end of the code, the only bytes
  between two files are zeros up to the next 16-byte boundary, except in
  the building area, where the pairs are packed back to back at any
  address. `tools_port/bcfiles` finds the files by walking this chain, so it
  also reads ROMs from the shiftable build.
  All 738 gzip members of the ROM have FNAME and nothing else in their
  flags, XFL 0, OS 3 (Unix), no pad byte before them, and a correct CRC32
  and ISIZE.

### 1.1 The split

`tools_port/romsplit.py` derives the layout from the ROM itself (the walk
above, through `tools_port/bcfiles`), for two users:

- **`romsplit.py splat`** writes the splat file the top-level `make extract`
  splits the ROM with (`build/blastcorps.<version>.yaml`): the header, IPL3,
  `init`, the texture table (0x4CE0), one `bin` for the whole texture and
  sound area from 0xCCE0, then one `bin` per gzip member (named
  `<FNAME>.<MTIME>`, e.g. `lagp.raw.855417165` and `lagp_dl.raw.855417176`;
  `.DUPE` on the second copy of cohut1), the four code members, the zero pads
  and the 0xFF trailer. Every asset stays a byte-exact binary.
- **`romsplit.units()`** gives the shiftable build's ROM units
  (`tools_port/shift/romlayout.py`, see [the shiftable build](../shiftable.md)):
  every texture-table slot (`HEX.blastN`, palettes `HEX.lut256` /
  `HEX.lut128`), every gzip file, the LZSS pictures, the object table, the
  static segment, the code members, and the gaps between them.

The member names are the gzip headers' FNAME fields (the original file
names) with the MTIME appended (a Unix time from early 1997; versions.md notes
that US 1.0 and 1.1 differ only in the time stamps of four level files that
were recompressed). `kiunzip_get_method` skips the FNAME field; the code
never reads it.

---

## 2. The two ROM tables

### 2.1 The texture table (ROM 0x4CE0)

```c
/* one entry, 8 bytes, big-endian; 0x1000 entries fill 0x8000 bytes */
typedef struct {
    u32 rom;    /* offset of the packed data from 0x4CE0 (not from ROM 0) */
    u16 size;   /* packed size in bytes: what the decoder consumes (even) */
    u16 type;   /* packing, 0..6 (section 4.3); anything else decodes to nothing */
} TexTableEntry;   /* src.us.v11/hd_code/5BF40.c, NON_MATCHING section */
```

- An entry's ROM data is `[0x4CE0 + e[n].rom, 0x4CE0 + e[n+1].rom)`: the DMA
  length is the distance to the **next** entry, which can be larger than
  `size` (alignment padding). `size` is what is decoded.
- `tex_load_table` takes 0x8000 bytes from the heap for the table
  (`gTexTable`) and DMAs it in raw, once per game mode: `hd_mode_setup`
  clears `gTexTableLoaded` and calls it on every mode change, `initlevel` and
  `spr_load` call it again (it does nothing when already loaded). It also
  empties the texture cache.
- The same table holds palettes (lookup tables for packings 4 and 5). They
  are stored as type 0 entries and fetched with `tex_load_palette` /
  `tex_queue_palette`, which DMA the entry raw into a 0x100-byte buffer and do
  not decode it.
- Ids are table indexes. Display lists carry them in the address word of
  `G_SETTIMG`; game code passes constants (`tex_get(0x546, 0)` for the power
  pill in `src.us.v11/hd_code/39050.c`) or reads them from data tables
  (crate types, TNT boxes, water, HUD panel frames, glyphs).

The comment on `TexTableEntry.size` in `5BF40.c` calls it "the unpacked
size"; the code (`tex_decode` copies and decodes exactly `size` bytes of
input) shows it is the packed size, which is also what the community notes
say. Worth fixing in the comment. The data agrees: decoding exactly `size`
bytes of every entry with packing 1-6 (3,599 entries in US 1.1) gives more
bytes than `size` in every case. 431 entries have a packing above 6 or size
0, and 26 have a `size` larger than the distance to the next entry.

### 2.2 The packed-object table (ROM 0x6EC4C0)

```c
u32 gPackedObjOffsets[0x200];   /* big-endian offsets from 0x6EC4C0 */
```

Object `i` is `[0x6EC4C0 + t[i], 0x6EC4C0 + t[i+1])`, a pair of gzip members
(model data + display lists). `lvl_load_object_table` takes 0x800 heap bytes
and DMAs the table raw each time a level's objects are spawned
(`lvl_spawn_objects`). The level's placements name objects by this index
(section 4.2). The shiftable build notes that entry 241 points at a second,
identical copy of the `cohut1` pair (the yaml marks it `.DUPE`). Entries
0-277 name 278 files, one file each, in ROM order; entries 278-511 (234 of
them) all hold 0x9BA75, the end of the last building (ROM 0x787F35), so
they name no file. The highest type any US level places is 277.

---

## 3. Loaders

There are four ways a file gets from ROM to RAM.

```
  ROM file kind                     loader                         decompression
  -----------------------------     ---------------------------    --------------------------
  texture / palette (table id)  ->  tex_get, tex_load_to_heap,     tex_decode (7 packings)
                                    tex_queue_load, tex_load_
                                    palette, tex_queue_palette
  building (object table id)    ->  lvl_load_object  --+
  vehicle model (type)          ->  lvl_load_model   --+--> port_load_packed: 1 DMA,
  ghost model (type)            ->  lvl_load_model_plain     kiunzip_entry x2 (gzip pair)
  everything else (ROM symbol)  ->  hd_rom_load(rom, dest, &size, a3, a4, method)
```

### 3.1 `hd_rom_load` (src.us.v11/hd_code/46C20.c)

```c
void hd_rom_load(u32 devAddr, u32 dest, u32 *size, u8 arg3, u8 arg4, u8 method);
```

- DMAs `*size` bytes from `devAddr` in 16 KB (0x4000) pieces, waiting on
  `gDmaMesgQueue` after each.
- If `arg3` or `arg4` is non-zero the data is compressed: the DMA goes to the
  staging buffer at 0x8021ED00 (the area `init` ran from), and then one
  decompression pass runs for each non-zero argument, `arg3` first, both
  reading on from where the previous pass stopped and writing on from where
  it ended. `*size` becomes the total decompressed length.
- `method` 1 is gzip (`kiunzip`, arena at 0x8004B400). For gzip, `arg3` and
  `arg4` are only flags: one pass per gzip member.
- `method` 2 is Rare's LZSS (`lzss_decompress`, window at 0x8004B400), and
  the argument value is the LZSS offset width in bits (10, 12 or 13 in the
  game's calls).
- `method` 0 with both arguments 0 is a plain DMA straight to `dest`.

Every caller, from the code:

| file kind | caller | `arg3, arg4, method` |
| --- | --- | --- |
| level (data + display lists) | `lvl_load_file` (00000.c) | 12, 10, 1: two gzip members |
| front-end scene | `fe_scene_load` (front end 0DE70.c) | 12, 10, 1: two gzip members |
| front end code + data | `hd_load_front_end` (46C20.c) | 13, 10, 1: two gzip members |
| title logo / picture | `title_load_logo_image` (17E10.c) | 12, 0, 1 |
| copyright logo (`romCopyright`) | `hud_load_demo_banner` (55970.c) | 12, 0, 1 |
| boot banner, boot icon | front end 07800.c | 12, 0, 1 |
| controller picture | `hud_load_pad_image` (51690.c) | 10, 0, 1 |
| traffic lights | 1D990.c | 10, 0, 1 |
| attract pictures | `fe_attract_pic_load` (front end 1A240.c) | 13, 0, 1 |
| globe textures | `fe_lvlsel_build_globe_dl` (front end 09570.c) | 13, 0, 1 |
| demo recordings | `rec_start_playback` (17210.c) | 9, 0, 1 |
| static segment | `hd_mode_setup` (00000.c) | 10, 0, 2: LZSS, 10-bit offsets |
| front-end background pictures | `fe_bg_load` (front end 196F0.c) | 13, 0, 2: LZSS, 13-bit offsets |
| music / sfx `.ctl` banks | `music_init_audio` (1C460.c) | 13, 0, 2: LZSS, 13-bit offsets |
| sequence file header, table | `music_init_audio` | 0, 0, 0: raw |
| one tune | `music_play_tune` (1C460.c) | 0, 0, 0: raw |

The non-zero values passed with gzip (12, 10, 13, 9) have no effect on
gzip. They look like LZSS bit widths; one guess (unconfirmed) is that these
files were LZSS-packed earlier in development and the arguments stayed.

`music_init_audio` decompresses each `.ctl` twice: first to 0x8004B400 only
to learn its size, then into a buffer of that size from the audio heap. The
probe writes its output on top of its own LZSS window (both at 0x8004B400),
so the probe's bytes are garbage, but `lzss_decode`'s control flow depends
only on the input bits, never on window contents, so the length it returns is
right.

### 3.2 `port_load_packed` (vehicle models and buildings)

`lvl_load_object`, `lvl_load_model` and `lvl_load_model_plain` (all in
`src.us.v11/hd_code/5CB60.c`, hand asm with C rewrites) share one sequence,
named `port_load_packed` in the C rewrite: DMA the whole range to 0x8021ED00
in one piece, run `kiunzip_entry` twice (two gzip members) onto the heap at
`gHeapPtr`, round the end up (`lvl_align_end`), advance `gHeapPtr` and return
the start. Then the display list's texture ids are resolved
(`tex_resolve_dl_impl`, section 4.3.2). For vehicle models
`anim_tex_find_patches` first records the `G_SETTIMG` commands that texture
animations will rewrite.

There is no sharing: every placement of a building and every vehicle
inflates its own copy onto the heap, even when the same object appears
several times in a level.

### 3.3 Raw DMAs

The two tables and the palettes are DMAed without decompression
(`tex_load_table`, `lvl_load_object_table`, `tex_load_palette`). Textures are
DMAed raw and then decoded in place (section 4.3). The sound samples (`.tbl`)
are never loaded whole; libaudio streams them (see [audio](audio.md)).

### 3.4 Gzip pairs

A "pair" (`X.raw` then `X_dl.raw`) is two complete gzip members stored with
no gap. The loader reads both with one DMA (the size runs from this file's
start to the next file's start, so it covers both) and inflates them back to
back, so in RAM the display-list member follows the data member directly and
the header offsets in the first member can point into the second. `kiunzip`
steps over one byte when the input does not start with the gzip magic 0x1F,
which tolerates a one-byte pad between members; the shiftable build keeps the
two code members (`hd_code_data`, `hd_front_end_data`) right after their
text members for the same reason. The gzip format details are in
[memory](memory.md).

---

## 4. File kinds and formats

The game's data blocks share one convention: a header of `s32` offsets
measured from the block's start, each pair of consecutive slots bounding a
section. The C rewrites write this as `OBJ_PTR(obj, off) = obj + *(s32 *)(obj
+ off)`; the port's schemas (`port/src/load/schemas.h`) describe each
section's field widths. All multi-byte values in ROM are big-endian. Fields
the code reads byte by byte (`BE16U`, `BE16S`, `BE32` in `5CB60.c`) may be
unaligned.

### 4.1 Levels

Level number to file, from `lvl_load_file` (`src.us.v11/hd_code/00000.c`):

| `levelno` | file pair |
| --- | --- |
| 0 | `chimp.raw` + `chimp_dl.raw` |
| 1 | `lagp.raw` + `lagp_dl.raw` |
| 2 .. 8 | `valley`, `fact`, `dip`, `beetle`, `bonus1`, `bonus2`, `bonus3` |
| 9 .. 59 | `level9` .. `level59` |

Levels 0 and 1 are swapped relative to ROM order (lagp comes first in ROM).
The size passed is the next level's start minus this one's (level 59 runs to
`romWorldTexturesRaw`), so it covers both members; `hd_rom_load(..., 12, 10,
1)` inflates both to `gHeapPtr`. `hd_setup_level` then sets `gLevelData` to
the block, advances the heap by the inflated size and calls
`initlevel(gLevelData, ...)`.

In brief (details in [levels](levels.md)): the block starts with a 0xC8-byte
header: twelve `u16` grid and cell sizes (0x00-0x16), an `s32` gravity at
0x18 and a display-list profile at 0x1C, then `s32` section offsets from 0x20
to 0xC4. The data at 0xC8 is segment 8 (`gLevelSegment8`, vertices the level
display lists address as 0x08xxxxxx). Sections that matter to this document:

| header slots | section | used by |
| --- | --- | --- |
| 0x2C-0x30 | texture-animation records with table ids | `lvl_resolve_tex_ids` |
| 0x5C-0x60 | object placements (14 bytes each, below) | `lvl_spawn_objects` |
| 0x50-0x54, 0x54 | vehicle spawn records (9 bytes), carrier record | `lvl_spawn_vehicles`, `lvl_spawn_carrier` |
| 0x78-0x84 | the display lists whose `G_SETTIMG` ids are resolved | `lvl_resolve_dl_textures` |
| 0x90-0x9C | four display-list entry points (`gLevelFileDl0..3`) | `lvl_relocate_dls` |

```c
/* one object placement, 14 bytes, read bytewise (lvl_spawn_objects) */
struct ObjPlacement {
    u16 x, y, z;    /* 0x0: BE, map units; << 5 for world units */
    u16 type;       /* 0x6: BE, index into the packed-object table */
    u8  flag;       /* 0x8: path building (tinted red in the intro fly-in) */
    u8  count;      /* 0x9: people inside (summed into gPeopleTotal) */
    u16 unkA;       /* 0xA: native read; copied to the object's u16 at 0xE */
    u16 tag;        /* 0xC: native read; the record's tag (lvl_add_object) */
};
```

### 4.2 Buildings and other placed objects (packed objects)

Inflated by `lvl_load_object(type)` per placement, then moved to the
placement's position (`lvl_translate_object`) and registered
(`lvl_add_object`). Header layout as far as the code shows it
(`src.us.v11/hd_code/5CB60.c`, `port/src/load/schemas.h` `swap_object`;
meanings beyond "who reads it" are tentative, see
[destruction](destruction.md)):

| off | type | meaning |
| --- | --- | --- |
| 0x00 | u16 | count of the `s16` table at slot 0x3C (`ObjectModel.numToppleKeys`) |
| 0x02 | u16 | bounding radius: `LevelObject.radius` = this << 5 (object.h) |
| 0x04 | u8 | toughness (`lvl_end_seq_set_toughness` sets 2 for kind 0x24 on level 0x31) |
| 0x05-0x07 | u8 | debris scale, people count (written from the placement), parts topple (object.h) |
| 0x08 | s32 | `ObjectModel.value` (summed) |
| 0x0D | u8 | `immunePart` (object.h) |
| 0x0E | u16 | from the placement's 0xA; when non-zero the vertices are not moved |
| 0x10-0x14 | offsets | display list (its `G_SETTIMG` ids resolved at load) |
| 0x18 | offset | inside the display-list area: always its start + 0x18 |
| 0x1C | offset | end of the vertex array; four `s16` (x, y, z) marker points |
| 0x20 | offset | `s16` x, z, x, z footprint box |
| 0x24-0x28 | offsets | floor/collision triangles, 0x14 bytes (nine `s16` + two bytes) |
| 0x28-0x2C | offsets | texture records: `u32` id, count byte at +4, `count - 1` more ids from +0x10 (`lvl_resolve_object_tex`) |
| 0x2C-0x30 | offsets | 8-byte points (`s16` x, y, z) |
| 0x30-0x38 | offsets | two lists of 0x38-byte debris records |
| 0x38 | offset | part lists (`obj_draw_all`) |
| 0x3C-0x44 | offsets | `s16` tables (topple keys; part heights) |
| 0x44-0x48 | offsets | byte records (`supportRecs`, `obj_part_exposed`) |
| 0x48-0x4C | offsets | 0x19-byte triangles of unaligned big-endian `s16` |
| 0x4C | offset | end of the 0x48 section; zeros follow up to the display-list area |
| 0x50.. | Vtx[] | vertices (16 bytes each) up to slot 0x1C |

Confirmed by round trip on all 278 buildings (`tools_port/bcfiles`, [tools](../tools.md)):
the sections lie in the order 0x50 (vertices), 0x1C, 0x20, 0x24, ... 0x48,
then the display lists at 0x10, each running to the next; every one is
filled exactly by its records (4 marker points, one footprint box, whole
0x14-, 0x38- and 0x19-byte records, the part lists to the byte); the only
other bytes are the zeros after 0x4C that start the display-list member on
a 16-byte boundary, and the second member is zero-padded to 16 bytes after
0x14. The display lists address the vertices as segment 9 (file + 0x50)
and contain no `gSPDisplayList`. Totals: 97,183 vertices, 2,459 floor
triangles, 777 + 164 debris records, 18,486 0x19-byte triangles, 32
texture records. (The names in brackets are `ObjectModel`'s, object.h.)

### 4.3 Textures

#### 4.3.1 Loading paths

All five texture loaders are hand asm in `src.us.v11/hd_code/5BF40.c` (C
rewrites under `NON_MATCHING`):

| function | what it does |
| --- | --- |
| `tex_get(id, param)` | returns the texture's physical address; looks `id` up in the cache `gTexCache` (8-byte `{id, physical address}` pairs up to `gTexCacheEnd`), else loads and decodes it at `gHeapPtr` now and appends a pair |
| `tex_resolve_dl(dl, end)` | walks a display list and replaces every `G_SETTIMG` (0xFD) address word, a table id, with the texture's physical address through the same cache |
| `tex_load_to_heap(id, param)` | loads and decodes at `gHeapPtr`, uncached (sprites) |
| `tex_queue_load(id, dest, param)` | starts the DMA into `dest` without waiting and queues a decode request (glyphs, HUD panel, effect tiles) |
| `tex_load_palette(id, dest)` / `tex_queue_palette` | DMAs a palette entry raw into a 0x100-byte buffer (now / queued) |

The immediate paths ("LOAD_TABLE_ENTRY" in the C): invalidate 0x1000 bytes at
`gHeapPtr`, DMA the entry there on `textureDmaMessageQ` and wait, fill the
16-byte request `gTexDecodeReq`, call `tex_decode`, advance `gHeapPtr` by the
decoded size.

Cached addresses are physical (heap address - 0x80000000); display lists set
segment 0 to base 0, so these work as `G_SETTIMG` addresses. The cache is
emptied whenever the table is reloaded (every mode change), so it lives for
one level or one front-end mode.

```c
/* one decode request (gTexDecodeReq, gTexDecodeQueue[]) */
typedef struct {
    u8 *data;    /* 0x0: packed on entry, decoded in place */
    u32 size;    /* 0x4: packed size (the table's u16) */
    s32 type;    /* 0x8: packing 0..6 */
    u8 *param;   /* 0xC: palette for packings 4 and 5, else unused */
} DecodeReq;     /* src.us.v11/hd_code/60F60.c */
```

Per frame, the queued path works like this (main loop in 00000.c, front end
loops in 00000.c, 07390.c, 11530.c, 17990.c of the front end):

```
frame start:  nextdma = 0; no_palette_dmas = 0; tex_reset_decode_queue()
during frame: tex_queue_load      -> DMA into dest (OSIoMesg slot gTexDmaMesgs[nextdma++]),
                                     request appended at gTexDecodeQueueTail
              tex_queue_palette   -> DMA only (nextdma++, no_palette_dmas++)
after the display list is closed:
              wait for nextdma messages on textureDmaMessageQ   (all DMAs, palettes too)
              assert nextdma - no_palette_dmas < NUM_TEXTURE_DMAS
              repeat (nextdma - no_palette_dmas) times: tex_decode_next()
```

Waiting for every DMA before decoding anything is what lets a packing 4/5
texture use a palette queued in the same frame. `textureDmaMessageQ` holds
0x90 (144) messages (`hd_init`'s `osCreateMesgQueue`).

#### 4.3.2 The decoder (`tex_decode`, src.us.v11/hd_code/60F60.c)

`tex_decode` copies `size` packed bytes from `data` to the scratch buffer
`gTexDecodeScratch` (in 8-byte units, then halfwords), runs the decoder for
`type` from the scratch back into `data`, and returns the number of bytes
written. So the destination buffer must hold the larger of the DMAed length
and the decoded size, and packed data must fit the scratch buffer: from the
symbol addresses it is 0x1000 bytes (0x803C3250-0x803C4250), the same 4 KB
that the loaders invalidate before each DMA. That every decoded texture
also fits in 4 KB (the N64's texture memory) is consistent with the buffers
the callers give (0x1000-byte effect caches, 0x200-byte glyph slots) but not
enforced anywhere.

Packings 1-6 read the input as `s16` tokens. Bit 15 clear: a literal that
expands to one or two output texels. Bit 15 set: a back-reference that copies
`tok & 0x1F` units (halfwords or words, depending on the packing) from
earlier output, one unit at a time, so it may overlap what it is writing.

```
back-reference, 16-bit units (packings 1, 3, 6):  offset bytes = (tok & 0x7FFF) >> 5
back-reference, 32-bit units (packings 2, 4, 5):  offset bytes = (tok & 0x7FE0) >> 4
count = tok & 0x1F (0 copies nothing)
```

| type | literal token | output per literal | texel format (from the bits) |
| --- | --- | --- | --- |
| 0 | none: stored data | copies `size >> 3` doublewords (a trailing `size & 7` is dropped) | any |
| 1 | `0RRRRRGGGGBBBBBA` | one u16: `((tok & 0xFFC0) << 1) \| (tok & 0x3F)` | RGBA16 (5551) with green's low bit 0 |
| 2 | `0RRRRGGGGBBBBAAA` | one u32: each field to the top of its byte | RGBA32, 4 bits of R, G, B, 3 of alpha |
| 3 | `0AAAAAAAIIIIIIII`-like: two 7-bit values | two bytes, each `<< 1` | two 8-bit channels, low bit 0 (IA16 in the community notes; the code doesn't fix the meaning) |
| 4 | two bytes `b` | two u16: `(pal[b >> 1] << 1) \| (b & 1)` | RGBA16 through a palette of up to 128 u16 entries; the token supplies the alpha bit |
| 5 | 11-bit index `i`, 4-bit alpha `a` | one u32: palette entry `pal[i]` (5-5-5 colour) to the top of bytes 0-2, `a << 4` in byte 3 | RGBA32 through a palette |
| 6 | two bytes `xxAAABBB` | two bytes `AAA0BBB0` | two 4-bit texels per byte, 3 bits each (fonts; `font_get_glyph` calls its glyphs "32x32 I4") |

`param` (the palette) is only read by packings 4 and 5. The level's texture
records are resolved with `param` = 0 (`initlevel` hands `lvl_resolve_tex_ids`
whatever `$fp` holds, which the C code above it never sets; the C rewrite
passes `NULL`), and `tex_resolve_dl_impl` doesn't set `param` at all (it
uses whatever the last caller left in `gTexDecodeReq`). So presumably the
level's own textures don't use packings 4/5. The callers that do pass a
palette are `spr_load` (palette from `tex_load_palette`) and the effects
(`fx_get_texture`, palette from `tex_queue_palette` into the record's
0x100-byte `gFxLoadParams` slot).

The texel format and size are not stored with the texture: the display list
or the drawing code (`G_SETTILE`, `gfx_draw_textured_quad`'s format argument)
decides how the decoded bytes are read.

### 4.4 Vehicle models (and two other models)

`lvl_load_model(type)` picks the file pair from a `switch` with the ROM
ranges written into the code (`src.us.v11/hd_code/5CB60.c`):

| `type` | file pair | notes |
| --- | --- | --- |
| `VEH_DRIVER` (0) | `driver` | also the default for unknown types |
| `VEH_SIDESWIPE` (1) | `sideswipe` | |
| `VEH_THUNDERFIST` (2) | `magoo` | |
| `VEH_SKYFALL` (3) | `buggy` | |
| `VEH_RAMDOZER` (4) | `bulldozer` | |
| `VEH_BACKLASH` (5) | `truck` | |
| `VEH_CRANE` (6) | `crane` | |
| `VEH_TRAIN` (7) | `train` | |
| `VEH_AMDREAM` (8) | `hotrod` | |
| `VEH_JBOMB` (9) | `jetpack` | |
| `VEH_BALLISTA` (10) | `bike` | |
| `VEH_BARGE_0B/11/12` (11, 17, 18) | `barge` | one file for all three |
| `VEH_POLICE` (13) | `police` | |
| `VEH_ATEAM` (14) | `ateam` | |
| `VEH_STARSKI` (15) | `starski` | |
| `VEH_CYCLONE` (16) | `minimagoo` | |
| `VEH_SHUTTLE` (0xFD) | `shuttle` | |
| `VEH_CHOPPER` (0xFE) | `chopper` | |
| `VEH_CARRIER` (0xFF) | `cmo` | |
| 0x96 | `commpoint` | the communication point |
| 0x98 | `scientist` | the academy marker (`lvl_load_scientist`) |

Type 12 has no case (no vehicle uses it). `lvl_load_model_plain` (used only
for the race ghost, `lvl_load_ghost_model`) has the same table for nine of
these types and falls back to `buggy`. The internal names (`magoo` for
Thunderfist, `buggy` for Skyfall, `hotrod` for the American Dream, `cmo` for
the carrier) are developer names, worth knowing when reading the yamls.

Model header (0x4C bytes of `s32` offsets; from `veh_add_record`,
`lvl_load_model`, `lvl_load_ghost_model`, `lvl_load_scientist` and the port's
`swap_model`):

| off | section | used by |
| --- | --- | --- |
| 0x00-0x04 | parts: `{s16 x4, last = n; n x s32}` ... | collision/attachment points (`veh_points_transform`) |
| 0x04-0x08 | `s16`, pad, then parts `{s16 x6, last = n; n x s32}` ... | `veh_register_parts` |
| 0x08-0x0C | `{s16 k; s16 n; n x s32; k x ten s16}` | deck / part transforms |
| 0x0C-0x10 | same shape | `veh_build_tris` |
| 0x10-0x14 | animation block (offset table, then keyframe groups) | `anim_init_model`, `anim_update_channel` |
| 0x14-0x18 | vertices (Vtx, 16 bytes); drawn as segment 6 | `veh_add_record` word 0, `gGhostVtx`, `gAcademyMarkerVtx` |
| 0x18 | matrix block `{s32 size; s32 bytes; words[]}` | `lvl_load_ghost_model` |
| 0x1C-0x20 | the display-list area whose `G_SETTIMG` ids are resolved | `lvl_load_model` |
| 0x24-0x44 | nine display-list pointers (0x24 is the main one) | `veh_add_record` words 0xC-0x2C |
| 0x48 | one more pointer | `veh_add_record` word 0x54 (meaning unknown) |

The front-end scenes (`usa_star`, `ninlogo`, `reflectlogo`) use the same
layout for the parts `fe_scene_load` reads: segment 6 data at slot 0x14, the
display list at 0x1C-0x20 (its `FeHdr` in front end `0DE70.c`). In
`ninlogo` and `reflectlogo` the word at 0x20 points 8 bytes past the end of
the file (the display-list member ends 8 bytes earlier), so
`tex_resolve_dl` there also walks 8 bytes of whatever follows on the heap.

Confirmed by round trip on the 21 models and 3 scenes (`tools_port/bcfiles`):
the sections lie in slot order (0x00, 0x04, ... 0x18, then the display
lists at 0x1C), each filled exactly by the shapes in the table: runs of
`{s16 x4 or x6, the last = n; n x s32}` at 0x00 and 0x04 (0x04 after a
leading `s16` pair), one `{k; n; n words; k rows}` list at 0x08 and 0x0C,
the animation block's `s16` offset table and its animations at 0x10 (unused
table entries point at the section's end), vertices at 0x14, the matrix
block's words at 0x18. The first member ends at 0x1C; the ten words
0x24-0x48 all point into the display-list area (0x24 at its start). The
display lists use segment 6 (the vertex section) for vertices and segment
7 for the part matrices, and contain no `gSPDisplayList`.

### 4.5 Whole images

Title logo and picture, boot banner and icon, copyright, globe textures,
attract pictures, traffic lights and the controller picture are plain texel
arrays (RGBA16 or IA8, per the assets yaml's subsegments) inside one gzip
member each, loaded with `hd_rom_load` straight onto the heap. The three
front-end background pictures are the same idea but LZSS-packed. None of
these go through the texture table.

### 4.6 Other files

- **Static segment** (ROM 0x787F40, 0xC0 bytes, LZSS 10): display lists for
  segment 1 (`gStaticSegment`; for example `seg1RspInitDl` at 0x01000010 and
  `seg1RdpInitDl` at 0x01000038), reloaded by `hd_mode_setup` at every mode
  change into `gStaticSegmentBuf`.
- **Demo recordings** (ROM 0x6A9F10, gzip): per demo a header, 0x400
  five-byte input records, an `s16` length at 0x140C, then the saved vehicle
  state (`rec_start_playback`, `src.us.v11/hd_code/17210.c`; layout in the
  port's `swap_demos`).
- **Sound**: the two `.ctl` banks are libultra `ALBankFile`s (LZSS 13),
  turned into pointers by `alBnkfNew` with the matching `.tbl` ROM address;
  the sequence file is a libultra `ALSeqFile` (`s16` revision, `s16` count,
  `{u32 offset, s32 len}` per tune, 0x42 tunes), and each tune is an
  uncompressed compressed-MIDI sequence (`ALCMidiHdr`, read by `alCSeqNew`)
  DMAed on demand by `music_play_tune` into a 0x21AE-byte buffer (tune
  lengths are rounded up to even). Details in [audio](audio.md).
- **Code**: `hd_code` (text + data, two gzip members) is DMAed by init to
  0x80000400 and inflated to 0x802447C0 at boot; the front end is inflated by
  `hd_load_front_end` to 0x801E7000 (0x37D00-byte area, the rest zeroed),
  with its ROM start and end read through the pointers `gFeRomStartPtr` /
  `gFeRomEndPtr` to the two words init stores at 0x803FFFF8 (per
  shiftable.md's memory map). See [memory](memory.md).

### 4.7 Segments

Display-list addresses in the files are segmented. The segment numbers the
code sets with `gSPSegment`:

| segment | base | set in |
| --- | --- | --- |
| 0 | 0 (physical addresses: resolved textures) | every frame's display list |
| 1 | `gStaticSegment` (static segment) | every frame |
| 2 | the frame's dynamic buffer (`gDynamicBufs`) | every frame |
| 6 | a model's vertices (slot 0x14) | per vehicle / object / scene |
| 7 | a model's matrices | per vehicle / object / scene |
| 8 | `gLevelSegment8` (level file + 0xC8) | level drawing (00000.c) |
| 9 | toppling-part matrices | 77E20.c |
| 10 (0xA) | `gObjPartMtx0/1` | level drawing (00000.c) |

`lvl_relocate_dls` adds the level display-list area's address (minus
0x80000000) to every `gSPDisplayList` in the level's lists, so branches
inside the level file become physical addresses at load.

---

## 5. Code references

| file | contents |
| --- | --- |
| `src.us.v11/hd_code/46C20.c` | `hd_rom_load`, `hd_load_front_end` |
| `src.us.v11/hd_code/00000.c` | `lvl_load_file`, `hd_setup_level`, `hd_mode_setup` (static segment, `tex_load_table`), `hd_boot` (0xFFB000 command line), the per-frame decode loop |
| `src.us.v11/hd_code/5BF40.c` | `tex_load_table`, `tex_resolve_dl(_impl)`, `tex_get(_impl)`, `tex_load_to_heap(_impl)`, `tex_load_palette(_impl)`, `tex_queue_load(_impl)`, `tex_queue_palette`; `TexTableEntry` |
| `src.us.v11/hd_code/60F60.c` | `tex_reset_decode_queue`, `tex_queue_decode`, `tex_decode_next`, `tex_decode`, `tex_decode_type0..6`; `DecodeReq`; the effects' texture caches (`fx_alloc_texture_caches`, `fx_get_texture`) |
| `src.us.v11/hd_code/5CB60.c` | `initlevel`, `lvl_read_header`, `lvl_resolve_dl_textures`, `lvl_resolve_tex_ids`, `lvl_relocate_dls`, `lvl_spawn_objects`, `lvl_load_object_table`, `lvl_load_object`, `lvl_resolve_object_tex`, `lvl_translate_object`, `lvl_load_model`, `lvl_load_model_plain`, `lvl_load_ghost_model`, `lvl_load_scientist` |
| `src.us.v11/hd_code/17A70.c`, `7F8B0.c` | `kiunzip` (gzip front end), `kiunzip_entry`, `lzss_decompress`, `lzss_decode` |
| `src.us.v11/hd_code/1C460.c` | `music_init_audio` (banks, sequence file), `music_play_tune` |
| `src.us.v11/hd_code/2E490.c` | `spr_load` (sprites: palette + frames from the table) |
| `src.us.v11/hd_code/168B0.c` | `font_get_glyph` (80-slot glyph cache, `tex_queue_load`) |
| front end `0DE70.c`, `196F0.c`, `1A240.c`, `09570.c`, `07800.c` | scenes, background pictures, attract pictures, globe textures, boot banner/icon |
| `port/src/load/port_assets.c`, `schemas.h` | the port's ROM classifier (a compact ROM map) and per-format field layouts |
| `tools_port/shift/romlayout.py`, `docs/shiftable.md` | moving files: the table rewrites and ROM-offset patches |

Main globals: `gHeapPtr` (bump allocator, `include/game/variables.h`),
`gLevelData`, `gLevelSegment8`, `gTexTable`, `gTexTableLoaded`, `gTexCache` /
`gTexCacheEnd`, `gTexDecodeReq`, `gTexDecodeQueue` / `gTexDecodeQueueHead` /
`gTexDecodeQueueTail`, `gTexDecodeScratch`, `gTexDmaMesgs`, `nextdma`,
`no_palette_dmas`, `textureDmaMessageQ`, `gPackedObjOffsets`,
`gStaticSegment`, `gDmaIoMesg` / `gDmaMesgQueue`; the `rom*` symbols in
`undefined_syms.hd_code.us.v11.txt` and `undefined_syms.hd_front_end.us.v11.txt`.

Structs: `TexTableEntry` and `DecodeReq` exist only in the `NON_MATCHING`
sections of 5BF40.c / 60F60.c; the register-result structs `Out802A32CC`,
`Out802A396C` (5CB60.c) and `Unk802A08E4Regs` carry the hand asm's extra
outputs (all the NON_MATCHING register structs are in `include/game/regs.h`). There is no level-header or model-header struct
in `include/`; the code uses offsets.

---

## 6. Modding notes

- **Replacing a texture with one of the same size and packing:** overwrite
  the bytes at `0x4CE0 + rom` and, if the packed size changes, the entry's
  `u16 size`. The packed data can't be longer than the gap to the next entry
  (the DMA length) unless the files after it move. Keep packed data even in
  length and at most 0x1000 bytes (the decoder's scratch buffer). Use type 0
  for unpacked texels; its length must be a multiple of 8 or the tail is
  dropped.
- **Adding textures:** the table has a fixed 4096 entries. The shiftable
  build (`make SHIFT=1`) rewrites the table when ROM units move, and
  [shiftable.md](../shiftable.md) says to add textures as new table entries,
  not as bytes after an existing texture (those bytes would be DMAed with
  the texture before them, since its DMA length runs to the next entry).
  Whether the table has free entries is not known.
- **Changing a level, building or model file:** `tools_port/bcfiles`
  ([tools](../tools.md)) parses and writes them. An edited file whose
  compressed bytes still fit the room up to the next file can go back in
  place: every loader takes "next file's start minus this one's" and
  inflates exactly two members, so the zeros left after a smaller file are
  DMAed but never inflated. Levels and models have up to 15 bytes of padding, buildings
  none, but compressing an edited file at level 9 (instead of the original
  6) usually wins back far more than a small edit adds (moving an object in
  lagp: 93 bytes to spare). Otherwise use the shiftable build: it re-places
  every ROM file, maps the `rom*` symbols to the files' new places, patches
  the 138 raw ROM offsets in `hd_code`'s code (vehicle model ranges, the
  object table, the texture table base) and rewrites both tables; its pads
  file takes a replaced file (`"rom": {"replace": ...}`, [shiftable](../shiftable.md)).
  Limits: a compressed file must fit the 0x25AC0-byte staging area at
  0x8021ED00 (the largest is 0x1EC50, per shiftable.md); a pair's second
  member must follow the first directly; the ROM must end before 0xFFB000.
  The port loads the files at their original offsets, so a shifted ROM runs
  in an emulator but not in bc.exe / bc_headless (bc.exe also checks the
  ROM's SHA-1 unless given `--no-rom-check`).
- **Recompressing:** gzip files must be standard deflate members (method 8;
  `kiunzip_get_method` rejects anything else with "unknown method"). The
  repo's `tools_port/gzpack.py` (over `tools_port/bcfiles/deflate.py`, our
  deflate written from RFC 1951 and the ROM's own members) is what the builds
  use for the code members. At level 6, with the original FNAME and MTIME in
  the header, it gives back every one of the 738 gzip members of the ROM byte
  for byte (US 1.0 and 1.1); Python's zlib at level 6 matches only some (14
  of the 60 levels, 15 of 21 models, 255 of 278 buildings): its blocks end
  after 16384 symbols, the ROM's after 32767 or earlier, where an estimate
  checked every 4096 symbols says the block compresses well.
- **Which building goes where:** a level's placement list (header slot 0x5C)
  names object-table indexes; change the `type` field to place a different
  building. Each placement inflates its own copy, so placing many large
  buildings costs heap.
- **Which model a vehicle type uses:** the `switch` in `lvl_load_model`
  (and `lvl_load_model_plain` for ghosts).
- **Level number to file:** the `switch` in `lvl_load_file`; the community's
  `blast_corps_rom_extending.txt` (in queueRAM's [blast_corps_docs](https://github.com/queueRAM/blast_corps_docs)) replaced it with a ROM table, which the
  shiftable build makes unnecessary.

---

## 7. Community notes vs the code

`blast_corps_docs.txt`, `blast_corps_textures.txt`,
`blast_corps_levels.txt` and `blast_corps_rom_extending.txt` (queueRAM's [blast_corps_docs](https://github.com/queueRAM/blast_corps_docs),
2016; not part of this repository):

| claim | code |
| --- | --- |
| Most files are gzip; the startup code DMAs hd_code, inflates it and jumps to it | confirmed (init's gzip copy; `hd_boot` is the jump target) |
| Levels are two gzip blocks, data and display lists | confirmed; also: both are inflated with one call, back to back |
| Level header is 0xC8 bytes, data at 0xC8 goes to segment 8 | confirmed (`lvl_read_header`, `gLevelSegment8`) |
| Level IDs 0 = chimp, 1 = lagp, then ROM order | confirmed (`lvl_load_file`) |
| `G_SETTIMG` holds a texture-table index, replaced after loading with a segment-0 address | confirmed (`tex_resolve_dl_impl`; the address is physical, segment 0 has base 0) |
| Texture table at 0x4CE0-0xCCE0, entries `[offset from 0x4CE0][length][type]` | confirmed; the length is the packed length |
| Seven packings and their bit layouts | confirmed for all seven (`tex_decode_type0..6`); type 0 drops a trailing `len & 7`; type 4 outputs 16-bit texels (the notes guessed RGBA32); type 6 outputs 4-bit pairs (the notes were unsure) |
| Types 4/5 take a LUT "assigned from a0->0xC" | confirmed: `DecodeReq.param` at offset 0xC |
| Some large textures are just gzip blocks | confirmed (section 4.5) |
| The boot calls the inflater twice (0xD, 0xA) | consistent with `hd_rom_load`'s two passes; for gzip the numbers are only flags |
| "proc_8028B4C4 some sort of DMA function" | `hd_rom_load` |

The notes don't mention the packed-object table, the LZSS files or the
sound data layout.

---

## 8. Differences between versions

From [versions](../versions.md): outside the code the US 1.0 and 1.1 ROMs
are the same apart from the header, the compressed lengths of the code files
(which init holds) and the gzip time stamps of four files (`chimp.raw` and
`trucker3US.raw` with their display lists), recompressed with identical
contents. None of the loaders in this document have `#if VERSION_US_V10`
branches. US 1.0's segment yamls are generated from US 1.1's, and its ROM
split comes from the same walk (versions.md).

---

## 9. Open questions

- **The last texture-table entries:** the table has a 0x7FFF-byte type-0
  "texture" at 0x350948 right before the music bank, which looks like the
  table's end sentinel (it supplies the DMA end of the entry before it).
  Unconfirmed; how many entries are real is unknown.
- **Palette sizes:** `spr_load` advances the heap by only 0x80 bytes after
  `tex_load_palette`, which DMAs the whole entry (0x100 bytes for the
  `.lut256` entries), so the first sprite frame is loaded over the second half
  of a 256-byte palette. Harmless if sprite palettes are 128-byte entries
  (the yaml has `.lut128` ones); not checked which ids the sprites use.
- **Packing 5's index range:** the token gives an 11-bit palette index, but
  every palette buffer the code fills is 0x100 bytes (128 entries).
  Presumably the data never uses indexes above 127.
- **Texture cache size:** from the symbol addresses `gTexCache` has room for
  250 pairs (0x803B8570-0x803B8D40, then `gTexCacheEnd` itself), and
  `tex_get_impl` / `tex_resolve_dl_impl` append without a bound check.
- **The decompression arena at 0x8004B400** is the heap's base, where
  `hd_mode_setup` also puts `gRdpOutputBuffer`. Presumably safe because loads
  happen while nothing is being drawn; not verified.
- **Model header slot 0x48** (points into the display-list area like
  0x28-0x44) and building header 0x18 (always the display-list area + 0x18)
  and 0x4C (the end of the 0x48 section): read, meaning unknown. Building
  header 0x08 is `ObjectModel.value` (object.h). (The "blobs" at 0x6EC4B4 and
  0x787F37 of earlier versions of this list are zero padding, section 1.)
- (Named now.) `romBootBannerEnd` and `romBootIconEnd` (front end, ROM ends
  of the boot banner and icon); `gModeGlobeSeqU64Spawn` (0x8030D888: the u64
  `MODE_GLOBE_SEQ` that `lvl_spawn_vehicles` compares with, see
  [levels](levels.md)).
