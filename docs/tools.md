# bcfiles: the level and model file library

`blastcorps/tools_port/bcfiles/` is a Python package (standard library only)
that reads every level, vehicle model, front-end scene and building from the
player's own ROM into Python objects, writes them back byte for byte, and
puts edited files back into a ROM. It is the file layer for a level editor;
the formats are described in [files](systems/files.md) and
[levels](systems/levels.md). Nothing ROM-derived is in the repository: the
tools read the ROM you give them and write to the folder you name.

## Command line

Run from `blastcorps/` (or anywhere, with the path to the package):

```bash
python3 tools_port/bcfiles list ROM                  # every file: kind, ROM range, name, sizes
python3 tools_port/bcfiles dump ROM level:2          # a level's header and records as text
python3 tools_port/bcfiles dump ROM model:driver --all   # a model, with its display lists decoded
python3 tools_port/bcfiles dump ROM object:12 --json # a building as JSON
python3 tools_port/bcfiles extract ROM OUTDIR        # every file decompressed, plus JSON
python3 tools_port/bcfiles build ROM OUTDIR NEW.z64  # the extract's (edited) JSON back into a ROM
python3 tools_port/bcfiles gltf ROM level:1 OUTDIR   # glTF 2.0 + PNG textures, for Blender
python3 tools_port/bcfiles roundtrip ROM             # parse + write + JSON every file, compare
python3 tools_port/bcfiles mod-check ROM MOD.bcmod    # a level mod: hashes and changes against the ROM
python3 tools_port/bcfiles mod-apply ROM MOD.bcmod OUTDIR    # the edited level files (level_NN.bin)
python3 tools_port/bcfiles mod-export ROM MOD.bcmod NEW.z64  # a ROM with the mod's levels (and texture slots) in it
python3 tools_port/bcfiles geo-rebuild ROM OUTDIR      # every level's derived sections regenerated (level_NN.bin)
python3 tools_port/bcfiles geo-gltf ROM level:0 OUTDIR # a level's triangles as glTF, for editing in Blender
python3 tools_port/bcfiles geo-import ROM level:0 EDITED.gltf OUT.bcmod   # the edited glTF as a geometry mod
python3 tools_port/bcfiles tex-import ROM 1549 IMAGE.png OUT.bcmod [--into MOD] [--palette ID]   # an image into a texture slot
```

`python3 -m bcfiles ...` works too with `tools_port` on `PYTHONPATH`. FILE is
`level:N` or `level:NAME`, `model:NAME`, `scene:NAME`, `object:N` (an
object-table index) or `object:NAME`, or a hex ROM offset.

**extract** writes `OUTDIR/index.json` (every file, its kind and ROM range),
`levels/NN_name/`, `models/name/`, `scenes/name/`, `objects/NNN_name/`
(each `data.bin`, the inflated file, and `level.json` / `model.json` /
`object.json`), the other gzip files (`images/`, `demos/`, `code/`) and the
LZSS files (`lzss/`) decompressed.

**build** reads the JSON files of an extract, writes each file that changed
and leaves the others' compressed bytes as they are (an unchanged extract
rebuilds the original ROM exactly). A changed file goes back in place when it
fits the room up to the next file (compressed at level 6, else level 9, else
level 9 for both members); the ROM keeps every offset, so the game, the
tables and the port need no change, and the header checksums are fixed. A
file that doesn't fit is written for the shiftable build instead
([shiftable](shiftable.md)): `build` writes the compressed file and a pads
JSON (`"rom": {"replace": ...}`), prints the `make SHIFT=1 SHIFT_PADS=...`
command and exits with status 3. The shifted ROM runs in an emulator and in
the port (bc.exe, bc_headless): the shiftable build leaves a ROM map in it,
through which the port finds the moved files ([shiftable](shiftable.md), "The
port"); bc.exe accepts it without `--no-rom-check`.

**gltf** walks the display lists (F3D) the way the RSP draws them and writes
`NAME.gltf`, `NAME.bin` and `textures/*.png`. A level gets its static
geometry plus its buildings (one node per placement; `--no-objects` leaves
them out); `--scale S` sets the units (default 0.01: 100 map units = 1 m).
Not modelled: the vehicles' part matrices (parts come out where their
vertices are, which for the vehicles is assembled), blend modes and fog, the
sky, texture animations.

## Library

```python
import sys; sys.path.insert(0, "tools_port")
from bcfiles import Rom, level, model, rombuild

rom = Rom.load("baserom.us.v11.z64")      # .z64/.v64/.n64; US 1.0 or 1.1
f = rom.level(1)                          # RomFile: gzip members, ROM range, slot
lv = level.Level.parse(f.data, f.split)   # typed sections
lv["objects"][0].x += 256                 # ObjPlacement record
data, split = lv.write()
blob, how = rombuild.pack_to_fit(f, data, split)
```

| module | what |
| --- | --- |
| `romfs` | the ROM walk (gzip members one after another, the LZSS pictures, the scenes, the object table and buildings, the static segment, the code), so a ROM from the shiftable build reads too; the texture and object tables |
| `gz` | gzip members as kiunzip reads them; recompression with `deflate.py` (our deflate: every ROM member byte for byte) |
| `deflate` | our deflate encoder (RFC 1951, lazy matching as at gzip level 6, blocks of up to 32767 symbols that end early where an estimate checked every 4096 symbols says the block compresses well): byte-identical to all 738 members of each US ROM; `tools_port/gzpack.py` wraps it in a gzip member for the builds |
| `lzss` | Rare's LZSS: decoder as `lzss_decode`, and an encoder the decoder accepts |
| `texture` | the seven texture packings, texel formats to RGBA |
| `level` | `Level`: header, segment-8 vertices, every section of levels.md as records, the display-list block |
| `model` | `VehicleModel` (0x4C header: vehicles, markers, front-end scenes), `PackedObject` (0x50 header: buildings) |
| `gbi` | F3D display lists: decoding, a walker |
| `gltf`, `png` | the exporter |
| `rombuild` | in-place replacement, the shiftable-build config, CIC-6102 checksums |
| `mod` | level mods (`.bcmod`, below): parse, check, apply to a `Level` |
| `geometry` | a level's triangles and the generators of everything derived from them (below) |
| `geogltf` | the geometry glTF for Blender and its import as a geometry edit |

Outside the library, `tools_port/romsplit.py` uses the walk for the builds:
the splat yaml of the top-level `make extract` (`romsplit.py splat`), the
shiftable build's ROM units and vermap's code layout.
`tools_port/provenance_check.py` checks that no line of the fork this
repository started from remains (README, "Licence").

Every parser keeps what it can't type: bytes after a section's records are
the section's `tail`, a section that doesn't parse is kept whole (`raw`),
and the padding that only aligns the next section is recomputed. On the US
ROMs nothing needs either: every level, model, scene and building is fully
typed and writes back byte for byte (below).

## Level mods (.bcmod)

bc.exe's level editor (the viewer's Editor panel, `port/README.md`) saves its
work as a mod: a small JSON file with, per level, the SHA-1 of the level file
as the ROM holds it (both gzip members inflated), the changes, and the SHA-1
of the result:

```json
{"format": "bcmod/1", "name": "wide start", "description": "...",
 "levels": [{"level": 1, "original_sha1": "...", "edited_sha1": "...",
             "changes": [{"op": "set", "kind": "object", "index": 0, "fields": {"x": 1880, "z": 1880}},
                         {"op": "add", "kind": "rdu", "index": 100, "copy_of": 99, "fields": {"x": 2000}},
                         {"op": "delete", "kind": "tnt", "index": 3}]}]}
```

Kinds and fields (the record layouts of [levels](systems/levels.md)):
`vehicle` (type, x, y, z, heading), `carrier` (speed, x, z, heading, limit;
one record, no add or delete), `object` (x, y, z, type, path, people,
motion, tag), `rdu` (x, y, z), `tnt` (x, y, z, type, timer, unk8, unkA),
`crate` (x, y, z, type), `commpt` (x, y, z, w), `block` (x, y, z, model),
`slot` (x, y, z, model, target; its triangles move with x, y, z). Changes
apply in order and an index counts the records as they are by then; `add`
inserts a copy of record `copy_of` and sets its fields, so a mod carries no
ROM data beyond the values it sets. Field ranges and the counts the game's
fixed arrays hold (8 vehicle spawns, 55 objects, 400 RDUs, 20 TNT, 15
crates, 2 communication points, 8 blocks, 8 slots with at most 100
triangles), one driver spawn (type 0) and the known vehicle types are
checked; a change that breaks them is refused.

Two implementations build the same bytes: `port/src/load/level_mod.c` (bc.exe,
bc_headless, the editor) and `mod.py` here (both lay the file out as
`Level.write` does: every section at its alignment, the padding recomputed,
the display-list block on 16 bytes with the visibility records' offsets
moved). A mod with `edited_sha1` is refused by either if its result differs.

In the port a mod is applied in memory: `bc.exe --mod FILE` (or bc.ini
`[mods] enabled = name, ...`, files in `mods\`), `bc_headless --mod FILE`
(`--mod-out DIR` writes the edited files for checking). The ROM is never
changed; the game reads the edited file through one hook around
`lvl_load_file`'s `hd_rom_load` (`game/port.h` `PORT_LEVEL_PRE` /
`PORT_LEVEL_POST`). **mod-export** puts a mod's levels into a ROM the way
**build** does: in place when they fit (that ROM runs in bc.exe with
`--no-rom-check` too), else the shiftable build's config (exit status 3; the
shifted ROM runs in emulators and in bc.exe / bc_headless through its ROM map).
That config carries everything the in-place write would have put in the ROM:
the levels that outgrew their slots as `replace` entries, and the levels that
fit and the mod's texture slots as `patch` entries, the same bytes at the same
original offsets (a level and the zeros to its slot's end; a slot's room, the
packed texels with their mips, and its table entry's size), which the shifted
build writes over those units before it lays the ROM out, so they move with
them ([shiftable](shiftable.md)). `build` does the same for files that fit
next to ones that don't.

## Geometry

A level file holds three sets of triangles ([levels](systems/levels.md)):
the drawn ones (the display-list block's lists and the segment-8
vertices), the floor (the ground grid, 0x30: every floor triangle is also a
drawn one, corner for corner, on all 60 US levels) and the walls (collision
grids A and B, 0x6C / 0x70: mostly an invisible mesh of their own).
`geometry.py` takes them out of a level (`Geometry.from_level`) and writes
them back with everything the game derives from them regenerated
(`write_into`):

| derived | rule (what reproduces Rare's data) |
| --- | --- |
| ground grid (0x30) | a floor triangle is in every cell its x-z footprint meets (closed rectangles), except cells lying strictly inside it; every cell lists its triangles in one global order, a topological order of Rare's per-cell lists |
| wall grids (0x6C, 0x70) | the same with every cell grown by 50 map units, half-open (touching the grown rectangle's high edge doesn't count); at exactly that edge an unchanged triangle keeps the cells Rare gave it (`wall_ties_of`) |
| visibility heights (0x48) | per cell, min(0, lowest y) and max(0, highest y) of the drawn vertices in its closed rectangle |
| bounds (0x4C) | the floor triangles' x-z extent, 50 map units more on every side |
| display lists (0x78..0x84) | each list written again: its state commands in order, its triangles in G_VTX loads of at most 16 vertices (F3D's buffer) between the same vertex-state commands (matrix, texture scale, lighting and texture-generation modes, G_DL), the sub-lists' G_CULLDL box the list's bounding box (Rare's corner order), every offset into the block moved (header 0x7C..0x9C, visibility records and groups, G_DL targets) |

The bridges' packed lists (G_MTX, part matrices, vertices used across a
matrix change) are kept as they are ("frozen"); so are the switch and gate
triangles (0x64, 0x68), the moving-part groups (0x74), the guard triangles
(0x24), the height zones (0x40, 0x44) and every per-level table in the code.

With `base=` (the original level; what mods use) everything an edit
doesn't reach keeps Rare's data: a grid cell whose regenerated list is the
same for both geometries keeps the file's list, likewise visibility cells and
the bounds; a list the edit doesn't touch keeps its commands and vertices; a
list whose triangles only moved keeps its commands and gets its vertices
written over the old ones; vertices no list loads any more are dropped. An
unchanged level comes back byte for byte (all 60).

How close the rules come, rebuilding all 60 US levels from their own
triangles (`geo-rebuild`, no `base`): the draw streams (state commands and
triangles with their vertex values) are equal at all 4,332 entry points;
1,565 of 1,608 ground cells, all 2,508 wall-grid cells, 543 of 545
visibility cells and all 60 bounds equal Rare's. The rest are triangles that
only touch a cell (no area) and duplicates. (Walls exactly 50 units from a
cell: of the 48 (triangle, cell) pairs where a wall only touches a cell's
grown rectangle at its high edge, Rare listed 11, on levels 35, 41 and 42,
and left 37 out, with nothing in the stored s16 corners to tell them apart:
the same corner is in for one triangle and out for its neighbour, and two
triangles identical up to a shift along x get different answers at the same
z edge. Rare's tool most likely decided on its unrounded coordinates. So the
rule is half-open, and a triangle the edit leaves unchanged keeps the tie
cells Rare gave it.) The game answers the same: loadcheck TB (the game's
own `coll_ground_height` and wall probes, native and the original MIPS in
unicorn, 1,200 random points per level) finds no difference on any level,
TC (native, ~30 million lattice points) none either, and bc_headless
running verify-levels' input on the 60 rebuilt levels (`--level-dir`) gives
the ROM's traces and game state.

### Editing (`Geometry.apply_edit`, bcmod/2 "geometry")

```json
{"move": [[2243, 123, 2454, 2243, 140, 2454]],
 "remove": {"render": [113], "ground": [683], "walls_a": [], "walls_b": []},
 "add": {"render": [{"after": 114, "v": [[x, y, z, s, t, r, g, b, a], [...], [...]], "flag": 0}],
         "ground": [{"v": [x0, y0, z0, x1, y1, z1, x2, y2, z2], "surface": 1, "flag": 0}],
         "walls_a": [{"v": [nine coordinates], "heading": 0, "side": 0, "fail": 0}]},
 "settimg": [[4, 0, 1434]]}
```

- **move**: every corner at (x, y, z) - drawn, floor or wall - goes to the
  new place (corners are welded by position: exactly; the editor also moves
  the corners near it and on its edges, below). A bridge's, moving part's,
  switch's or gate's corner can't be moved.
- **remove**: triangle ids of the unedited level: drawn triangles count in
  the block's order (`Geometry.render_tris()`), floor and wall triangles in
  their global order.
- **add**: drawn triangles go right after triangle `after`, in its list and
  state (its texture, combiner, render mode); not into a frozen list. Floor
  triangles are scanned first in their cells, and Rare's triangles under
  them lose their stop flag: the game's scan stops at the first triangle with
  that flag it hits, and it moves the last triangle a wheel stood on to the
  front of its cell (`ground_cache_tri`). Walls: `side` picks the side that
  blocks (0: the side away from the level's origin), `heading` the facing
  the free arc is cut around (default: the normal's).
- **settimg**: the k-th G_SETTIMG of a list loads another texture id (the
  same texel format and size as the old one, or the picture is garbage).
- Applied in that order: moves, removes, adds (in the moved coordinates),
  settimg. Checked afterwards: the per-frame visible lists against
  `lvl_alloc_dl_buffers`' sizes, counted as `lvl_build_visible_dl` copies
  them with every cell visible (`Geometry.list_commands`, C twin
  `lg_budgets`: a run of records with one id copies its first record's
  commands from the start and the rest from their alt offset, groups come
  from the alt offset; the worst case, not a sum of every command in the
  block). Counted that way no level's own data overfills a list (levels 4
  and 43: 4295 and 4315 of 6000 in list 0, where the old plain sum said 7746
  and 6513), so splits work there too. The port takes an edited file at most
  64 KB larger than the ROM's.

Helpers for scripts: `subdivide_edit(geo, ids, levels, height, lv)` (the
editor's Split, `levels` times, then every corner of the pieces raised by a
height function: hills and dents), `flatten_edit`, `quad_edit` (a ramp or a
platform), `merge_edits`. bc.exe's level editor makes the same kind of edit
with its Terrain tools (brushes, ramps, splits, moving corners, painting,
texture slots: port/README.md) and saves it as a bcmod/2 mod. Its splits
leave no T-junctions: a neighbour sharing an edge with a split triangle is
split along it too (one, two or three edges: 2, 3 or 4 pieces, the shorter
diagonal), and so are the floor triangles under them and the wall triangles
standing on the edge (`remove` + `add` walls with the old one's heading, side
and fail), so the ground, the picture and the walls stay closed when the new
corners move. `subdivide_edit` runs the same splits (`bcfiles/terrain.py`, the
twin of the editor's `port/src/live/terrain.cpp`): `port/tools/subdiv_cross.py
bc_cameratest.exe ROM` splits patches of several levels with both and
compares the edits (all 60 levels: 120 patches split once and twice, every
edit the same).

Rare's meshes meet without sharing corners, so the editor's model welds them
by position (`terrain.Model`, `WELD_TOL` = 2 map units, the tolerance the
data was authored with: two thirds of the levels' open edges have another
mesh's edge within one unit, then the count drops to the background):

- the level's corners within 2 of each other on every axis (chained) are one
  group: a move moves the whole group by the move of its lowest-numbered
  moved member (`Model.move` / `resolve`), each member keeping its own
  offset;
- a level corner within 2 of another triangle's edge (drawn, floor or wall;
  more than 2 from its ends) follows that edge while the edge is there: it
  takes the edge's ends' displacement at its place along it and can't be
  moved by itself (15-35% of a level's corners are such T-junctions);
- a corner of a mesh's border (an open drawn or floor edge) 2 to 3 units
  from another mesh's open edge - a seam Rare left a little wider
  (`SEAM_TOL` = 3: the survey's counts of open edges with another one within
  0, 1, 2, 3 units are 3778, 2255, 553, 902, then the background) - follows
  that edge too, keeping the gap as it was (an edge it lies on comes first);
- a corner lying on two other meshes' edges (meshes that overlap: their edges
  cross there) can follow only one; when a move would take those edges more
  than a unit apart at it (`SPLIT_GAP`), every edge it lies on is split at it
  - the T-junction fixed: it becomes a corner of those meshes - and each half
  again at the other corners that followed that edge; the corner goes to the
  mean of the places its edges gave it (`last_weld` "tjunctions"). An edge a
  triangle that can't split has (a bridge's, a protected corner's) stays;
- a split edge's midpoint is such a corner when one lies within 2 of the
  middle (the corner becomes a corner of both meshes, and free);
- protected corners never move: a weld skips them (`last_weld`
  "prot_skipped").

`Model(geo, protected, weld=False)` is the model without the welds (the
editor before them). `subdiv_cross.py` also moves the split patches, runs
raise strokes (`Model.raise_step`, the editor's brush) and L0's triangle 113
moved up 8 through both twins, compares the edits and the welds' counts
(T-junctions split too), and checks the result: no corner lying on another
mesh's edge may end up more than a unit off it (before the crossing splits:
17 corners in these 49 strokes, 29 units at L10's crossing (1633, 165, 4415);
the wider seams, 680 in these levels: 25 pulled apart, up to 40 units; now
none of either); cracks, open edges near the edit that weren't open before
(no other edge within 2, not within 3 units of an edge's ends, an edge from
the same corner counting only when it runs the same way): none for the L0
case, 24 units over 49 soft strokes on 12 levels (main before: 8; 112 without
the welds). The first measure (every sample, a same-corner edge never
counting: it reads two meshes welded at a corner as open) and, with
`SUBDIV_FACES=1`, a surface one are reported too: the surface measure counts
a mesh that lies on another's face (a decal, a building's foot) lifted off it
by a brush - 11988 units against 13444 before - which the welds don't hold
(a corner on a face isn't welded to it: future work).

`port/src/load/level_geo.c` is the C twin (bc.exe, bc_headless and the
editor apply geometry mods with it when a level loads): loadcheck TD checks
that it rebuilds all 60 levels into the same files as `geo-rebuild` and
gives the ROM's files back keeping Rare's data, and its mod results equal
`mod.py`'s byte for byte.

### glTF for Blender (`geo-gltf`, `geo-import`)

`geo-gltf` writes meshes `render` (every drawn triangle, three vertices of
its own, one primitive per run of triangles sharing a state, material
`r<piece>.<run>`, textured), `ground` (materials `g.s<surface>.f<flag>`) and
`walls_a` / `walls_b`. Every vertex carries where it came from:

| attribute | holds |
| --- | --- |
| TEXCOORD_0 | the texture coordinates (render) |
| TEXCOORD_1 | x, z of the corner in the unedited level (map units) |
| TEXCOORD_2 | y of that corner, the triangle's id |
| TEXCOORD_3 | walls: heading, side * 256 + fail |
| COLOR_0 | the vertex colour (render) |

Material `extras` hold what an added triangle needs (`after`: the run's last
triangle; the texture's size and tile values, to turn UVs back into s, t).
Blender keeps UV maps on face corners, so edit the meshes freely: on import
(`geo-import`, .gltf or .glb) a triangle whose corners carry one id and that
id's original corners is that triangle (corners that moved become moves),
any other triangle is added (into its material's run; floor triangles with
their material's surface and flag; walls with TEXCOORD_3 or the heading of
their normal), and ids that are gone are removed. Positions are rounded to
whole map units (100 map units = 1 m at the default scale 0.01). Moving the
drawn mesh moves the floor too (welded corners); to change where the ground
is without a visible change, edit the `ground` mesh.

### Texture slots (`tex-import`, bcmod/2 "textures")

`tex-import ROM ID IMAGE.png` makes a mod entry `{"id", "fmt", "siz", "png",
"slot_sha1"}`: the slot's texel format is taken from the level lists that use
it (the image must have their size), the image is converted (RGBA16/32,
IA4/8/16, I4/8) and packed with the slot's own packing (literals and
back-references for packings 1, 2, 3, 6; stored for 0). A slot that holds
more than the image keeps the rest. When the picture packs to more than the
slot's room (up to the next table entry), the slot grows: up to what the
texture unpacks to (the most any buffer it is loaded into holds), its packed
bytes rounded up to 8 (see "Growing" below).

- **Mipmapped slots** (`"mips": n`): a slot the level lists load whole
  (LOADBLOCK) and sample with tiles 1..n at smaller sizes holds its smaller
  levels after the picture (each half the size, rows padded to 8 bytes, odd
  rows' 32-bit words swapped as TMEM interleaves them). `tex-import` finds
  them from the lists and makes every level from the picture (2x2 averages),
  so the slot looks like the picture far away too, not like Rare's old
  texture; without "mips" (older mods) only the picture's own bytes change.
- **Palette packings 4 and 5** (`"palette": id`, `--palette ID`): these slots
  (28 effect sprites and the medals) hold colour indices into a palette slot
  of their own. Every one of those palettes is shared with other textures,
  so the palette is kept and each pixel takes the nearest of its colours;
  `tex-import` says how many textures use the palette and warns when the
  slot is used with more than one palette (the medals). CI4/CI8 texels are
  then packed as the game unpacks them (token stream of packing 4 / 5).
  C twin: `tc_encode_slot_pal`, `tc_encode_slot_mips` (the same bytes;
  `port/tools/texcodec_cross.py` checks 1800 synthetic cases). `mod-export` writes the packed bytes and the table entry's size in
place (or, on its exit-3 path, puts them in the shiftable build's config as
`patch` entries: the shifted ROM holds the same slot bytes and size, wherever
the slot ends up; tested with the slot moved 16 bytes).

- **Growing.** The texture data is packed back to back (an entry's DMA
  length is the next entry's offset minus its own; at most 6 bytes of slack
  per entry), so a slot can't grow in place: `mod-export` takes its exit-3
  path, the slot's texture (its own ROM file) is `replace`d by the packed
  bytes rounded up to 8 and the shiftable build moves everything after it
  (the table's offsets follow; its size goes in as a `patch`). bc.exe and
  bc_headless keep the ROM as it is: the game's table there still ends the
  slot at the next entry, so the DMA that asks for the slot's old room is
  handed the whole grown slot (`port_rom_grown`, through `port_dma_len`), the
  same bytes a shifted ROM gives (its ROM map reads a file that grew to its
  end). Tested: L0's road slot 1549 (room 1584) with a 32 x 32 noise picture
  (2720 bytes): bc_headless and bc.exe on the shifted ROM == `--mod` on retail
  (trace, records, the 2720 texel bytes in RDRAM, bc.exe's pictures),
  mupen64plus on the shifted ROM the same records and texels.
- **New slots** (`"new": true`, `"from"`: the slot whose texels after the
  picture's it keeps; a G_SETTIMG retarget (settimg) draws it): one of the
  table's empty entries, with empty ones before and after it (the entry
  before a texture's ends its DMA: an entry right after a real texture can't
  be taken, the first free one is 742). bc.exe puts the picture, stored,
  past the ROM's end at run time; `mod-export` puts the new slots one after
  the other (in id order, as bc.exe does) in the ROM's free space at its end
  (the 0xFF trailer, 25 KB in the retail ROM; the last 16 bytes kept) and
  points the entry there and the next entry's offset at their end, in
  place; when anything needs the shift path (or they don't fit), they are an
  added file `texnew` before the trailer and the shift build points the
  table into it (`"texnew"` entries, `romlayout.apply_texnew`). No table move
  is needed (the 4096 entries hold them). Tested both ways: the exported ROM
  vs `--mod` on retail, trace, records and texels the same.

bc.exe and bc_headless apply a mod's slots when it loads
(`port/src/load/tex_codec.c`, the C twin of `texture.py` / `png.py`: the same
packed bytes; the game's DMAs of the slot and of its table entry read them,
as from an exported ROM). On a ROM from the shiftable build, bc.exe reads the
table and the slot where that ROM has them and puts a mod's slot bytes at the
slot's original offset (the offset the game's DMA asks for, through the ROM
map), so a mod's slots also work on a ROM whose textures moved.

### Geometry mods (bcmod/2)

```json
{"format": "bcmod/2", "name": "...", "description": "...",
 "levels": [{"level": 0, "original_sha1": "...", "edited_sha1": "...",
             "changes": [...], "geometry": {...}}],
 "textures": [{"id": 1549, "fmt": 0, "siz": 16, "png": "<base64>", "slot_sha1": "..."}]}
```

A mod holds the edit and the author's own values (and images), never ROM
data or anything derived: the record changes apply first, then the geometry,
then every derived section is regenerated (keeping Rare's data where the
edit doesn't reach), then `edited_sha1` is checked. bc.exe's editor writes
these files (its Terrain mode) and opens a level with a loaded mod's
geometry and texture slots as its starting point, so saving again keeps
them. bcmod/1 files load as
before; a bcmod/1 file can't carry geometry. A delta keyed by positions and
ids works for every edit made so far (moves, splits, additions, removals,
retextures); what it can't express is changing a list's state commands
beyond a texture id (render modes, combiners): that would need its own op.

`--level-dir DIR` (bc_headless, bc.exe; testing) makes the game read
`DIR\level_NN.bin` (inflated, the ROM's byte order: `geo-rebuild`,
`mod-apply`) instead of the ROM's level through the same hook.

## Tests

```bash
python3 tools_port/bcfiles/test_bcfiles.py              # synthetic data only (CI)
python3 tools_port/bcfiles/test_bcfiles.py --rom ROM    # also every file of your ROM
```

The `--rom` run checks, for US 1.1 and 1.0: the 60 levels, 21 models, 3
scenes and 278 buildings parse with nothing left untyped and write back
byte for byte, through JSON as well; recompression with `deflate.py`
reproduces every member; the six LZSS files decode and survive the encoder;
the texture table decodes; the walk finds every file where the code's
constants put it; an unchanged rebuild is the ROM; an edited level goes back
in place with valid checksums.
