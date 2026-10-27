# Collision: ground, walls, objects and probes

How a vehicle finds the ground under its wheels, how walls and buildings stop
it, how vehicles touch each other, and how projectiles, TNT and push blocks
probe the world. The core idea: **walls don't push a vehicle back; they shrink
the set of headings it may move in** (the "free arc"), and the vehicle turns
into what is left.

Everything here is **from the code** unless it says **measured**. Almost all of
it is hand-written asm (56040.c, 62740.c, 77E20.c, 89250.c); the
`#ifdef NON_MATCHING` C beside each pragma and its comments are the readable
version.

Related docs: [vehicles.md](vehicles.md), [destruction.md](destruction.md)
(what a hit on a building does), [carrier.md](carrier.md).

## Where the code is

| file | what |
| --- | --- |
| `src.us.v11/hd_code/62740.c` | ground: the wheel slots, the triangle scans, height, surfaces, bounds, the free-arc steering helpers, decks |
| `src.us.v11/hd_code/56040.c` | vehicle vs world: `veh_collision_setup`, `veh_collide_world`, `veh_collide_spheres`, the sphere-triangle tests, the free arc, the vehicle-owned triangles |
| `src.us.v11/hd_code/77E20.c` | vehicle vs objects: `obj_collide_vehicle`, `obj_hit_tri`, `obj_tri_clip_free_arc`, the contact buffers |
| `src.us.v11/hd_code/89250.c` | probes for non-vehicles: `coll_begin_probe`, `coll_probe_tris`, `coll_probe_objects`, `coll_probe_vehicles`, `coll_ground_height` |
| `src.us.v11/hd_code/8A080.c` | dynamic triangles (push blocks) |
| `src.us.v11/hd_code/5CB60.c` | level load: the grids, `lvl_build_coll_tri` |
| `src.us.v11/hd_code/679E0.c` | `ground_in_triangle`, the level 0x17 wrap |
| `include/game/object.h` | `CollisionTri`, `TRI_AXIS_*`, `TRI_SIDE_*` |
| `include/game/types.h` | `VehState` (the slots), `SURFACE_*` |

## Units

| space | scale | used by |
| --- | --- | --- |
| world | s32 | positions (`gPlayerX`, `LevelObject.pos`, hit spheres) |
| level data | world `>> 5` | s16 coordinates in the level file: ground triangles, spawn and respawn records, bounds |
| collision triangles | world `>> 2` | `CollisionTri.v` (`lvl_build_coll_tri` stores level `<< 3`); every sphere test shifts the sphere `>> 2` first |
| matrices, effects | world `<< 11` | `fx_spawn`, the camera eye |

Headings are 12-bit (0..0xFFF, 0x400 a quadrant); the code often wraps with
0xFFF instead of 0x1000. y is up. "No ground" is 99999999 (0x5F5E0FF).

## The ground

### Triangle sets

1. **The ground grid** (`gGroundCells`, `gGroundCols` x `gGroundRows` cells of
   `gGroundCellW` x `gGroundCellD`, level header 0x08..0x0E, data at 0x30):
   0x14-byte triangles `{s16 x0, y0, z0, x1, y1, z1, x2, y2, z2; u8 surface;
   u8 stop}`. `ground_cache_tri` moves a hit triangle to the front of its cell's
   list at run time.
2. **Moving level parts** (`gGroupTris`, 0x28-byte records of world
   coordinates plus a surface byte): rebuilt every frame by
   `lvl_groups_update_tris` (77E20.c) from the level's groups, which
   `lvl_group_move_part` moves (bridges and the like).
3. **Vehicle decks** (`gMoveableTris`, 0x38 bytes, with the owning vehicle id at
   +0x36): the train's, the barges' and the crane's surfaces, transformed each
   frame (`veh_deck_transform`).
4. **Guard triangles** (`gGuardTris`, level header 0x24..0x28): extra
   triangles the J-Bomb's ceiling test and the crane use (`ground_scan_guard_tris`).

Objects also carry floor triangles (`ObjectModel.floorTris`), scanned by
`ground_over_object` only for the J-Bomb's shadow.

### The height of one wheel slot (`ground_slot_height`, 62740.c)

The query point is 0x78 above the slot. Three scans each return the triangle
whose height is nearest the query (`ground_scan_part_tris`,
`ground_scan_moveable` skipping the vehicle's own deck, `ground_scan_grid`); the
nearest of the three wins:

- the grid: its surface byte;
- a deck: `SURFACE_NORMAL`, and the slot "stands on" that vehicle
  (`gSlotStandOn`, `gStandOn`: how riders are found);
- a moving part: its surface, and `VS_ON_MOVING_PART` (so the vehicle is
  re-grounded when parked there).

No triangle at all: the slot keeps its height, surface 1. The result is clamped
to 0 or more. The primitive tests are `ground_tri_box_contains` (bounding box),
`ground_tri_contains` (edge sides, single precision) and `ground_tri_height`
(the plane, computed through an `abs()` offset by 1000, so heights below -1000
would come out wrong; whether any level has them is unknown).

`coll_ground_height` (89250.c) does the same three-way choice for TNT, blocks,
shells and object shadows.

### Wheel slots

Each vehicle has three slots (`VehState.wheelOffsets`, rotated by the heading)
handled by `veh_ground_contact` every frame (vehicles.md has the gravity and
bounce side): bounds check (`veh_respawn_if_out`), each slot grounded or
airborne, the vehicle's y = the average of slots 1 and 2, its tilt from the
slot differences (divisors per vehicle), the shadow, the surface, and the
stand-on records. A slot can climb at most 0x240 a frame (`veh_clamp_climb`);
ground more than 0x1E below the expected height launches it.

### Surfaces (`SURFACE_*`)

`veh_ground_surface`: `VS_SURFACE` = the average of the three slots' classes
(classes of 100 and above count as 2), `VS_SURFACE_MAX` / `gSurfaceMax` the
highest.

| class | effect |
| --- | --- |
| 1 (`SURFACE_NORMAL`) | all five speed bands |
| 2..6 | `veh_band_accel` uses `7 - class` bands: rougher ground cuts the top speed; the grip is scaled by the class (`veh_turn_heading`: `grip * surface / speed`) |
| 100 (`SURFACE_WATER`) | fails the level (`gFailTrigger`) unless J-Bomb or a barge |
| 0x65 (`SURFACE_SPIN_SHADOW`) | the player's shadow is drawn as a turning arrow |
| 0x66 (`SURFACE_FATAL`) | fails the level |
| 0x67 (`SURFACE_DIAMOND_SANDS`) | level 12: bounce damping 8 |

`veh_reground` (parked vehicles, the exit probe) stores a leaked register as
the slot surface, which is 0 there; class 0 would make `veh_band_accel` read 7
bands (past the table) and the turn rate 0. Whether that is ever visible is
unknown (the next normal update recomputes it).

Water heights for levels with water come from `water_get_height` (39050.c,
animated waves); target and building levels fail when the player is below the
water, or below `LevelInfo.waterY` on levels without water.

## Walls: the collision triangles

### CollisionTri (0x60 bytes, object.h)

| off | field | meaning |
| --- | --- | --- |
| 0x00 | `normal[3]`, `d` | the plane (s64, unnormalised) |
| 0x20 | `len`, `lenSq` | |
| 0x28 | `v[3][3]` | vertices (world `>> 2`) |
| 0x4C | `heading` | the wall's facing, 0..0xFFF |
| 0x4E | `axis` | the dominant normal axis, dropped for the 2-D test |
| 0x4F | (owner) | the owning vehicle type for vehicle triangles (7 = train) |
| 0x50 | `id` | triangle id (gates) |
| 0x51 | `isSolid` | |
| 0x52 | `partNo` | the object part, 1-based |
| 0x54 | (side) | switch planes: the side last seen (`TRI_SIDE_*`) |
| 0x55 / 0x56 | `wallTwoSided` / `wallSide` | which side blocks |
| 0x57 | `linkedPart` | becomes solid when that part falls |
| 0x58 | `isWall` | objects: only walls clip the free arc |
| 0x59 | (fail) | a nonzero byte makes the wall lethal (`gFailTrigger`) |

`lvl_build_coll_tri` (5CB60.c) builds them from 0x14..0x19-byte packed
level-data records.

### The lists

| list | what | built by |
| --- | --- | --- |
| `gCollCellsA` / `gCollCellsB` | the level's static walls in a grid (`gCollCols` x rows, cells `gCollCellW` x `gCollCellD`, header 0x10..0x16, data 0x6C / 0x70) | `lvl_build_coll_grid_a/b` |
| `gCollTris` | vehicle-owned triangles (decks, the train's sides) and "gates", rebuilt from the vehicle's model each frame (`veh_update_tris`, `veh_build_tris`); `gVehGates` (0xDC bytes) open gates by area or animation state | `lvl_build_object_groups` |
| `gSwitchTris` / `gSwitchGroups` (0xFC bytes) | switch planes: walls for the vehicle kinds listed in the group; for the others, crossing the plane toggles `gDriverCanExit` | `lvl_build_switch_groups` |
| `gDynTris` | the push blocks' triangles | `coll_add_dyn_tris` (8A080.c) |
| each object's `tris` | buildings (destruction.md) | `lvl_add_object` |

`gCollCellsB` is skipped for the J-Bomb (vehicle 9): walls it can fly or stomp
over. The grid lookup (`veh_grid_cells_at`) takes **one** cell, the one under
the sphere's centre, with no bounds check, so triangles near a cell edge must
be listed in both cells by the level data. They are: Rare's grids list a
wall in every cell whose rectangle, grown by 50 map units, it meets
([levels](levels.md), "What the geometry generators show").

### Sphere against triangle

Hit spheres (`gVehBoundSpheres`: 12 per-vehicle bounding spheres;
`gVehPartSpheres`: up to 100 part spheres with a damage `value`) are
registered at spawn (`veh_register_parts`, radius scaled by the vehicle's
scale) and placed each frame (`veh_place_parts`). A sphere hits a triangle
when (in order) its distance to the plane is under r and its projection is
inside the triangle (`math_sphere_tri_plane`, `math_tri_project_2d`,
`math_point_in_tri_2d`), or it touches an edge (`math_sphere_hits_tri_edges`),
or vertex 0 (`math_sphere_hits_tri_vertex0`; only vertex 0 is tested).

## Vehicle against the world (56040.c)

```
veh_collision_setup(z, nonBlocking, b2, b3, x, y, b0, speed, minImpact, damageMode, b8, veh)
    free arc = empty (0 / 0xFFF), gCollX/Y/Z, gHitSpeed = speed,
    gPushDivisor = veh.pushDivisor, gCollHit = gCollBlocked = 0, contacts cleared
veh_collide_spheres(type, veh)       other vehicles, radar markers (stopped TNT / blocks), comm points
veh_collide_world(type)              if the bounding sphere hits anything, each part sphere:
    1. objects of the kinds gObjSolidKinds lists for this vehicle (indestructible for it)
    2. gDynTris (push blocks)
    3. gSwitchTris (veh_hit_switch_triangle)
    4. gCollTris (not the vehicle's own; train triangles set gTrainContact)
    5. the grid cell: gCollCellsB (not for the J-Bomb), gCollCellsA
    -> veh_hit_triangle: gCollHit, clip the free arc +-0x400 around the wall's heading,
       copy the fail byte; sparks if fast enough
    free arc changed -> gCollBlocked, gBlockedFrame
obj_collide_vehicle(type, veh)       buildings (below)
```

Parts listed in the vehicle's `gNonBlockingParts` (an s8 list ended by a
negative number) are recorded as contacts but don't block.

### The free arc (`gFreeArcStart` .. `gFreeArcEnd`)

Each blocking triangle cuts away the half-circle of headings that point into it
(`veh_clip_free_arc(h - 0x400, h + 0x400)`); what is left is the arc the vehicle
may still move in. When blocked (`gCollBlocked`) the vehicle:

1. `veh_prepare_escape`: stores the push divisor, nudges a stopped vehicle's
   speed to 10, flips the arc when reversing;
2. `veh_reverse_if_behind`: if travelling outside +-0x400 of the facing, turns
   the travel round and negates the speed;
3. `veh_turn_into_free_arc`: the travel heading is pulled into the arc (0x78
   inside its ends, or its middle), turning by `scale * |speed|`;
4. `veh_hit_slowdown`: speed eased toward zero by 3.

A hit that doesn't change the arc (gCollHit only) rolls the vehicle back to
its snapshot from before the move and bounces it (per-vehicle speeds,
vehicles.md).

When the arc collapses entirely (walls on every side) while touching another
vehicle, `gPushDivisor` steps up (1, then 8, then +1, capped at 200); other
vehicles' sphere radii are divided by it, so stuck vehicles can squeeze apart.

## Vehicle against buildings (77E20.c)

`obj_collide_vehicle(id, veh)`, after the world pass:

- `obj_set_full_damage`: full damage when the J-Bomb's damage mode is 2 or any
  wheel slot is airborne (not in mode 1).
- For every object whose bounding sphere overlaps the vehicle's (not the hidden
  end object, not kinds `gObjSolidKinds` lists for this vehicle), every solid
  triangle the bounding sphere touches goes to `obj_hit_tri`.
- `obj_hit_tri` finds the first of the vehicle's part spheres that touches the
  triangle, damages the part (destruction.md), and then: a part still standing
  sets `gCollHit` and clips the free arc (only triangles with `isWall`); a part
  that just went down sets `gObjHitPartDown` and lets the vehicle through.
- The results go into the VehState (`hitPartDown`, `hitObject`,
  `hitImmunePart`); the Thunderfist and Cyclone rolls read them.
- Contact buffers (`veh_contacts_*`, `veh_prev_contacts_*`): last frame's
  touching spheres, so damage lands once per contact.

`veh_maze_object_hits` (679E0.c) replaces this on pill-maze levels (a hit fails
unless the power pill is on).

## Vehicle against vehicle

`veh_collide_spheres` tests the vehicle's bounding sphere against every other
vehicle's (radius divided by `pushDivisor`), then their part spheres
(`veh_collide_vehicle_parts`), skipping vehicles riding on each other:

- each touching pair clips the free arc toward the other part
  (`veh_block_toward_part`);
- touching the train counts `gTrainContactFrames` (the hint `YW_HINT_TRAIN` at
  15);
- **touching the carrier** with anything but the driver on foot sets
  `gCarrierContact` (the mission fails, carrier.md);
- the driver touching a vehicle requests entering it (`veh_request_switch`,
  vehicles.md).

`gTrainContact` (touching a train-owned triangle) knocks a flying J-Bomb down.

## Probes (89250.c)

Things that aren't vehicles use the same machinery through probes:

```
coll_begin_probe(x, y, z)              empty free arc, gCollX/Y/Z, gProbeNonBlockingParts
coll_probe_vehicles(r)                 vehicles' spheres: gProbeHitPlayer, gProbeHitCarrier, block the arc
coll_probe_tris(x, y, z, r, kind, tag) the world walls (veh_report_world_hits); a collapsed arc -> fully blocked
coll_probe_objects(r, amount)          buildings: the first triangle hit takes amount / toughness
coll_step_xz / coll_heading_in_ring    move along the arc
```

| user | kind | |
| --- | --- | --- |
| the camera lift (00000.c) | 0xBE | raises the eye out of walls (camera.md) |
| push blocks (`block_update`, 4B5E0.c) | 0xC8 | pushed by vehicles (and by the carrier: `gProbeHitCarrier`), stop on walls; no damage |
| TNT (`tnt_update`, 48D00.c) | 0xC9 | explodes on a building or the carrier |
| shells (`shell_update_all`, 4DA80.c) | 0xCA | 420 damage to the first building triangle; the carrier is destroyed |

The probe kinds are compared with `gObjSolidKinds` masks by `kind & 31`, so
they alias vehicle bits (whether that matters is unknown).

The driver's exit probe (`driver_try_exit`) doesn't use these; it runs the
driver's own collision at each test point (vehicles.md).

## Bounds and wrap

- `veh_respawn_if_out` (62740.c): outside the level box (`gBoundsMinX`,
  `gBoundsMinZ`, `gBoundsMaxX`, `gBoundsMaxZ`, level units, header 0x4C), the
  vehicle goes back to its record in `gRespawnPoints` (header 0x50: the spawn
  list) with `gCamSnap`, an effect and a warning panel. US 1.1 adds three
  train-only limits (vehicles.md).
- `veh_check_wrap` / `veh_wrap_position` (679E0.c): level 0x17 only (from
  `backlash_update`), the position wraps around at fixed x / z limits.

## Per-frame order

1. `gCarrierContact = 0`.
2. `veh_update_others`: parked vehicles on moving parts re-grounded.
3. `veh_update_player`: move, `veh_ground_contact`, model and spheres,
   `veh_collision_setup`, `veh_collide_spheres`, `veh_collide_world`,
   `obj_collide_vehicle`, the blocked / hit response.
4. `carrier_update` (its own pass).
5. Riders follow their decks (`veh_update_riders`), TNT and blocks ride.
6. Probes: `tnt_update`, `block_update`, `shell_update_all`.
7. `lvl_groups_update_tris`: moving parts' triangles for the next frame.
8. The camera's lift probe (in `cam_update`).

## Modding notes

- **Slow or rough ground:** the surface byte (+0x12) of the level's ground
  triangles: 1 is full speed, 2..6 progressively slower, 100 water, 0x66
  lethal. There is no separate friction value; sliding comes from grip
  (vehicles.md).
- **A lethal wall:** the grid triangle's fail byte (CollisionTri +0x59).
- **A building one vehicle can't hurt:** an entry `{level (0xFFFF any), kind,
  vehicle mask}` in `gObjSolidKinds`; the object is then a plain wall for that
  vehicle.
- **Parts a vehicle drives through:** its `gNonBlockingParts` list.
- **Hit sphere sizes:** the radii in the vehicle model's part records, scaled by
  the scale passed to `veh_register_parts` in its `*_spawn`; 12 bounding and
  100 part spheres in all.
- **Step height:** 0x78 (query), 0x1E (launch), 0x240 (climb) in 62740.c.
- **Bounds:** the level header's box at 0x4C; respawn points at 0x50.
- Code changes go in the `NON_MATCHING` C (the port builds it).

## Versions

US 1.1 adds the train-only out-of-bounds tests and `gCollExitProbe` (rocking
hits are skipped during the exit probe). See [../versions.md](../versions.md).

## Open questions

- (Done.) CollisionTri bytes 0x4F, 0x54 and 0x59 are named in object.h:
  `owner`, `switchSide`, `failTrigger` (the code still reads them by offset).
- The meaning of the A / B wall-grid split beyond "B is ignored by the J-Bomb".
- For grid triangles the builder fills `partNo`, `linkedPart` and `isWall` from
  leftover registers; nothing seems to read them for grid triangles.
- `veh_collision_setup`'s `b2` and `b8` arguments, `gVehContact`,
  `gPushDivisorStepped`.
- (Done.) The record types: `gBoxTargets` is `BoxTarget` (`include/game/types.h`),
  `gTriTargets` (0x80306480) `TriTarget` (77E20.c; its bytes 0x16..0x2F are unknown).
- The leaked-register surface 0 from `veh_reground` (above).
- (Done.) `veh_collide_world_fp` (56040.c) is `veh_collide_world` with the
  asm's fp output.

## Notable findings

- Walls never push: every block is a heading restriction.
- One grid cell per lookup, no bounds checks; only vertex 0 of a triangle gets
  a point test.
- The ground cells' triangle lists are reordered at run time
  (`ground_cache_tri`).
- Unbounded scans: `gRespawnPoints`, `gSwitchGroups` and the bounding-sphere
  owner searches have no end test; a missing key reads past the table.
- `veh_contacts_has_other` (77E20.c) has no callers.
