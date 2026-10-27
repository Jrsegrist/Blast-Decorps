# Destruction: buildings, damage, debris and chain reactions

Blast Corps is about knocking things down. Every destructible thing in a level
(buildings, barns, walls, the end object) is a **level object** made of up to 16
**parts**. Each part has a damage percentage. A part at 100 % collapses: it
topples, fades or sinks, drops debris, may release people, and may take the
parts it supported down with it. When every part is down the object is
destroyed and its money value is added to the level total. Explosions (TNT,
explosive debris, the carrier's blast) deal delayed damage to everything in a
radius, which is how chain reactions spread.

Everything here is **from the code** unless it says "measured". Most of the
functions are hand-written asm; the `#ifdef NON_MATCHING` C rewrites next to the
`GLOBAL_ASM` pragmas, and their comments, are the readable version.

Related docs: [collision.md](collision.md) (how a hit is detected),
[vehicles.md](vehicles.md) (who hits), [carrier.md](carrier.md) (the carrier's
blast), [scoring.md](scoring.md) (what the tally feeds).

## Where the code is

| file | what |
| --- | --- |
| `src.us.v11/hd_code/77E20.c` | the object system: hits, damage, collapse, falling/toppling/fading/sinking parts, the effect queue, delayed damage, the tally, targets, the level end |
| `src.us.v11/hd_code/5CB60.c` | level load: `lvl_spawn_objects`, `lvl_add_object`, `lvl_build_coll_tri` |
| `src.us.v11/hd_code/89250.c` | probes used by projectiles and TNT: `coll_probe_objects`, `coll_blast_at_probe`, `coll_spawn_blast_fx` |
| `src.us.v11/hd_code/48D00.c` | TNT crates |
| `src.us.v11/hd_code/4DA80.c` | shells (the Ballista's missiles) |
| `src.us.v11/hd_code/4B5E0.c` | the Ramdozer's push blocks |
| `src.us.v11/hd_code/679E0.c` | `obj_blast_around_carrier` |
| `src.us.v11/hd_code/39050.c` | `fx_draw_carrier_blast` (the carrier's dome) |
| `src.us.v11/hd_code/60F60.c`, `43A60.c` | billboard effects (`fx_spawn`) and particles (`ptcl_*`) |
| `src.us.v11/hd_code/20460.c` | people running out (`people_spawn`) |
| `src.us.v11/hd_code/32E00.c` | the "dig coach" hints that grade your hits |
| `include/game/object.h` | `ObjectModel`, `LevelObject`, `CollisionTri`, `OBJ_KIND_*`, `OBJ_TOUGH_*`, `DMG_RULE_*` |

## Data structures

### LevelObject (`gObjects[]`, 0xFC bytes, `gObjectsEnd` = one past the last)

| off | field | meaning |
| --- | --- | --- |
| 0x00 | `info` | the object's `ObjectModel` (model header) |
| 0x04 / 0x08 | `tris` / `trisEnd` | its collision triangles (`CollisionTri`, on the heap) |
| 0x0C | `radius` | bounding sphere radius (model u16 at +2, `<< 5`) |
| 0x10 | `pos[3]` | sphere centre |
| 0x1C | `spawnPos[3]` | placed position (base of the moving-object poses) |
| 0x28 / 0x2C | `centreX` / `centreZ` | footprint centre; the carrier aims at this |
| 0x30 | `kind` | object type from the placement (`OBJ_KIND_*`) |
| 0x48.. | `partFalling[16]`, `partFallTime[16]`, `partSpinX/Y/Z[16]` | state of parts that lost their support |
| 0xE8 | `cell` | level grid cell (visibility, see below) |
| 0xE9 | `numParts` | up to 16 |
| 0xEA | `destroyed` | set once by `obj_check_destroyed` |
| 0xEB | `pathBuilding` | the placement flag: one of the buildings in the carrier's path |
| 0xEC | `partPct[16]` | damage per part, 0..100; **100 = down** |

### ObjectModel (the `info` header, 0x4C bytes, in the loaded level data)

Most fields are byte offsets from the header to tables. The fields that matter
for destruction:

| off | field | meaning |
| --- | --- | --- |
| 0x04 | `type` | **toughness: every non-full damage is divided by it.** `OBJ_TOUGH_FLIMSY` (1): not counted as a building, no particles, and some vehicles kill it in one hit. `OBJ_TOUGH_UNCOUNTED` (0xFF): left out of the tally. 0 would divide by zero |
| 0x05 | `debrisScale` | nonzero: generic random debris scaled by this; zero: the model's own effect records |
| 0x06 | `numPeople` | people released; overwritten at spawn from the placement record |
| 0x07 | `partsTopple` | parts topple over (`obj_start_topple`); clear: they fade out (`obj_start_part_fade`) |
| 0x08 | `value` | money value of the whole object |
| 0x0D | `immunePart` | 1-based part that takes no damage and doesn't count for "destroyed" |
| 0x24 | `floorTris` | 0x14-byte floor triangles; byte 0x12 = part, 0x13 = "gone" |
| 0x2C | `partPoints` | per-part pivot / debris origin (s16 x, y, z) |
| 0x30 / 0x34 / 0x38 | `breakEffects` / `landEffects` / `partDlRecs` | 0x38-byte break and landing effect records, then the per-part display-list records |
| 0x3C | `toppleKeys` | s16 per part: which part topples (`obj_part_outranked`), also the fall distance |
| 0x40 | `partHeights` | s16 per part (`<< 5`): exposure test and sink depth |
| 0x44 / 0x48 | `supportRecs` / `supportRecsEnd` | `{u8 part; u8 threshold; u8 n; n x {u8 support; u8 weight}}` |

### CollisionTri (0x60 bytes)

Collision is per triangle; each triangle knows its part (`partNo`, 1-based) and
whether it is solid (`isSolid`). `linkedPart` makes a triangle become solid
when that other part falls (the rubble or the inner wall that is now exposed).
Every collapse turns the part's triangles off and its linked triangles on;
when a falling part lands, `obj_disable_landed_part_tris` turns off the
triangles that were linked to it.

### Object kinds and special cases (`object.h`)

- `OBJ_KIND_END` (0x38): the end object. It takes no damage; the driver on foot
  touching it sets `gEndObjectTouched` (see scoring.md). Blasts skip it.
- `OBJ_KIND_SHADOW_A..C` (0xBA..0xBC): get a blob shadow at load. What they are
  is unknown.
- `OBJ_KIND_MARS_FAIL_FIRST..END` (0xE7..0xE9): on level 0x2D, hitting one fails
  the mission (`obj_check_mars_fail` sets `gFailTrigger`).
- `lvl_end_seq_set_toughness` (5CB60.c) sets the toughness of kind 0x24 to 2 on
  level 0x31.

## Level load

`initlevel` (5CB60.c) calls `obj_clear_delayed_damage`, then
`lvl_spawn_objects`, then `obj_clear_effect_queue`, and later `tnt_spawn_all`
and `crate_spawn_all`. `lvl_spawn_objects` walks the level's 14-byte placement
records (`{u16 x, y, z, type; u8 flag, people; u16 poseType; u16 tag}`,
big-endian), loads each model (`lvl_load_object`), and calls `lvl_add_object`,
which fills the `LevelObject`, adds path buildings to the level's `gPathParts`
list (`lvl_pick_path_parts` picks a list for the levels that have one), and
builds the collision triangles. `gPeopleTotal` is the sum of the placements'
people counts. On a level with a saved status (resumed), `apply_status`
(7FB50.c) knocks down the parts saved as destroyed.

## How a vehicle hit becomes damage

```
vehicle update (e.g. ramdozer_update)
  veh_collision_setup(...)      gHitSpeed = speed, gVehDamageMode, gNonBlockingParts, gCollX/Y/Z
  veh_collide_spheres / veh_collide_world
  obj_collide_vehicle(id, veh)                                   77E20.c
    obj_set_full_damage(veh)    gFullDamage: mode 2, or any wheel slot airborne (not in mode 1)
    for each object whose sphere overlaps the vehicle's bounding sphere
        (skipped: the hidden end object, and kinds gObjSolidKinds lists for this
     vehicle: veh_collide_world already treats those as indestructible walls)
      obj_collide_sphere -> obj_hit_tri(id, e, tri) per solid triangle hit
    writes VS_HIT_PART_DOWN / VS_HIT_OBJECT / VS_HIT_IMMUNE_PART into the VehState
```

`obj_hit_tri` is the core:

1. Find the first of the vehicle's **hit spheres** (`gVehPartSpheres`: x, y, z,
   r, u16 `value`, ...) that touches the triangle. Its 1-based index is the
   damage-rule **key**.
2. Mars-fail check; if `gFailTrigger` is set, no damage.
3. Each (object, part) is hit at most once per pass (`obj_hits_has`/`_add`).
4. **Contact debounce:** unless the hit is full-damage or from the carrier,
   a sphere that already touched something last frame does no damage
   (`veh_prev_contacts_has`). So ramming damage lands on the first frame of a
   contact; to hit again you back off and ram again. The rocking-hit test
   (below) runs here and can bypass the debounce.
5. `value = obj_calc_damage(...)`; the part's `partPct += value` (capped at
   100); screen shake `gShakeTime = 10`, `gShakeSize = min(value, 500)`;
   `obj_spawn_break_effects(e, part, pct)`.
6. At 100: the part collapses (`obj_mark_part_gone`, `obj_start_topple`,
   `obj_start_part_fade`, `obj_start_sinking_part`, `obj_spawn_people`, shake
   15 / 400, triangles off, linked triangles on).
7. The triangle blocks the vehicle (`gCollHit`, `obj_tri_clip_free_arc`)
   while the part stands, unless the sphere's key is in `gNonBlockingParts`;
   a part that just went down lets it through (`gObjHitPartDown`).
8. `obj_drop_unsupported_parts`, then `obj_check_destroyed`.

### obj_calc_damage

| case | damage |
| --- | --- |
| the carrier (id 0xFF) | 100 to a flimsy object (toughness 1), 0 to anything else |
| the part is the model's `immunePart` | 0 |
| **full-damage rule** (`obj_full_damage_rule`): a rocking hit; or the frame after a wall block (`gBlockedFrame + 1`) on a level listed in `gObjBlockedFullDamage` for this vehicle; or a flimsy object hit by Sideswipe (1), Ballista (10) or the Cyclone Suit (16) | **100, not divided by toughness** |
| `gFullDamage` (airborne, or J-Bomb damage class 2) | 100 |
| otherwise | the first applicable record of `gObjDamageRules` for (vehicle, key), **divided by toughness** |

The `gObjDamageRules` records are `{u8 vehicle; u8 key; u8 kind; u8 n; ...}`:

| kind | damage |
| --- | --- |
| `DMG_RULE_ATTACK` 0xFC | `n * value` if `gVehAttacking` |
| `DMG_RULE_EXPOSED` 0xFB | `n * value` if `obj_part_exposed` (no standing support, and the part is low enough: `partHeights << 5 <= gCollY + 0x578`) |
| `DMG_RULE_SPEED` 0xFD | `abs(gHitSpeed) * value * n >> 4`; always applies, ends each key's list |
| `DMG_RULE_SPEED_REV` 0xFE / `_FWD` 0xFF | the same, only when moving backward / forward |
| anything else | an animation channel of the vehicle (`anim_get_state`): while it plays, `(round(f * (hi - lo)) + lo) * value` |

So for most vehicles damage is proportional to speed: `|speed| * value * n /
16 / toughness` percent. `gDigHitDamage` / `gDigHitDamagePerPart` keep the last
values for the dig coach.

Who sets the special inputs:

- `gVehAttacking`: only Thunderfist (6C5E0.c) and the Cyclone Suit (8AEE0.c),
  around their roll / special move. Backlash and the J-Bomb never set it.
- `gVehDamageMode`: 0 for everyone except the J-Bomb (`jbomb_update`): 1 while
  its action is 1..3 (no full damage even airborne), 2 when the action is 4 or
  shortly after it (every hit is 100). That action 4 is the stomp landing is an
  inference.
- Airborne full damage: any vehicle with a wheel slot in the air does 100 per
  part hit (`obj_set_full_damage`), which is why jumps into buildings work.

### Other damage sources

- **Probes** (`coll_probe_objects(r, amount)`, 89250.c), used by shells, TNT and
  blocks: the first triangle hit gets `coll_damage_object_part`: `pct += amount
  / toughness`, shake 10 / 200, full collapse at 100.
- **Delayed (blast) damage**: `obj_blast_damage(x, y, z, radius, amount)` gives
  **every standing part of every object in range the same `amount /
  toughness`, with no falloff**, through `obj_add_delayed_damage`: 40 slots of
  `{object, u16 amount, part, timer}`, each with a random delay of 1..20 frames
  (`util_rand2_range(1, 20)`); hits on the same (object, part) add up. When
  all 40 slots are busy, new blast damage is dropped. `obj_update_delayed_damage`
  applies it each frame (shake 10 / min(amount, 500)).

## Collapse

- `obj_check_destroyed`: an object is destroyed when every part except the
  immune part is at 100 or falling. Then `destroyed = 1`, `gLevelMoney +=
  value`, the "$N" popup (`gValuePopupNew`), and the alpha zones tied to the
  object are turned off.
- `obj_drop_unsupported_parts`: for each standing part with a support record,
  the weights of its supports that still stand are summed; below the record's
  threshold the part starts **falling** (`partFalling`, spin from
  `gPartJitterX/Y/Z`, triangles off). It repeats until nothing changes, so a
  collapse cascades within the frame.
- Falling parts are advanced inside `obj_draw_all`: the drop is `t * t *
  -30000` (fixed point) until it reaches the part's `toppleKeys` distance; then
  the part lands (`obj_spawn_landing_effects`, shake 10 / 300, pct = 100).
  Because this runs in the draw loop, only objects in visible cells advance
  (uncertain whether this is ever visible in play).
- **Toppling** (`obj_start_topple`, needs `partsTopple`; only the part with the
  lowest key topples): 2 slots (`gTopplingParts`). The part rotates away from
  the impact point (`gCollX/Z`), speeding up by 15 a frame until about 79
  degrees (0x385), then holds 6 frames.
- **Fading** (`obj_start_part_fade`, when `partsTopple` is clear): 4 slots
  (`gFadingParts`); alpha drops by 20 a frame from 255.
- **Sinking** (`obj_start_sinking_part`, only kinds listed in `gObjSinkingKinds`):
  1 slot (`gSinkingPart`); the part sinks into the ground with a shake, speed
  += 16 a frame, `gPartSinking` while it runs.
- When the slots are full the animation is skipped; the damage and collision
  still change.
- Damage stages: `obj_draw_all` draws each per-part display-list record only
  when its condition on `partPct` holds (pct >= limit or pct <= limit), which is
  how a building shows cracks and missing walls.

## Debris and effects

- `obj_spawn_break_effects(e, part, pct)`: with `debrisScale`, random debris
  (`obj_spawn_random_effects`, kind tables `gObjDebrisKinds` /
  `gObjDebrisKindsFull`, counts `gObjDebrisCounts`; none if the hit did less than
  20). Otherwise the model's 0x38-byte break records for that part whose level
  (byte 0x33) is reached, each once (a done flag in the model data); level
  0xFF records repeat on every hit of 20 or more.
- `obj_queue_effect` puts a record in `gEffectQueue` (30 slots; a full queue
  drops it). `obj_run_effect_queue`, every frame, counts each record's delay
  down, spawns its billboard (`fx_spawn` with a definition from
  `gFxDefsByKind`), plays its sound, and **if the record has a blast radius
  (u16 at +0x2E), calls `obj_blast_damage(pos, radius, byte 0x30 << 4)`.**
- `obj_start_emitter` starts a particle burst (`ptcl_start`) for some effect
  kinds (`gObjEmitterFxKinds`); only one emitter runs at a time.
- `obj_spawn_people`: when exactly one part is at 100, `people_spawn` releases
  the object's people (`gScorePeople += n`, up to 20 people on screen; see
  scoring.md).

Screen shake (`gShakeTime` frames, `gShakeSize`):

| event | time | size |
| --- | --- | --- |
| vehicle hit | 10 | min(damage, 500) |
| part collapse (vehicle hit) | 15 | 400 |
| delayed (blast) hit | 10 | min(amount, 500) |
| falling part lands | 10 | 300 |
| probe hit | 10 | 200 |
| TNT or shell explodes | 10 | 400 |

## Chain reactions and explosives

```
part reaches 100
  -> obj_spawn_break_effects -> obj_queue_effect
       -> obj_run_effect_queue (after the record's delay)
            -> record has a radius? obj_blast_damage(pos, radius, amount)
                 -> obj_add_delayed_damage on every standing part in range (1..20 frames)
                      -> obj_update_delayed_damage -> more parts at 100 -> ...
```

Only the model's own break and landing records can carry a radius, so which
objects explode is part of each model's data. The random debris never does.

### TNT (48D00.c)

- Level records of 12 bytes: `{s16 x, y, z; u8 type; u8 fuse; s16 radius; s16
  amount}`. The crate (0x48 bytes in `gTntCrates`) keeps the fuse in frames
  (`fuse * 60`), the blast radius and the amount.
- Touching a crate in a vehicle lights its fuse (a ticking loop). The crate can
  be pushed (push speed = the player's speed + 8, capped by `gTntPushSpeedMax`).
  A pushed or ridden crate that hits an object, the fuse reaching 0, or the
  carrier touching it (which also destroys the carrier: `cmo_hit_request`) all
  call `tnt_explode`: blast billboard, shake 10 / 400, `SFX_EXPLOSION`.
- 5 frames later `coll_blast_at_probe(amount, radius)` calls
  `obj_blast_damage` at the crate; each part then waits its own 1..20 frames.
- `TNT_RADIUS_ADDR` (the first `gTntTypes` record's probe radius) is used for
  ridden crates; it moves between versions (see [../versions.md](../versions.md)).
- No code was found where a blast lights another TNT crate: chains run
  through the buildings' break effects.

### Other weapons

- **Ballista missiles** (4DA80.c): `ballista_update` fires `shell_fire(speed *
  0x140 / 0x104 + 0x280, ..., type 0, 0x1A4)`: a shell does **420 (0x1A4)
  damage divided by toughness** to the first triangle it touches, then
  explodes. It has no blast radius. A shell that touches the carrier destroys it.
- **Push blocks** (4B5E0.c, Ramdozer): `coll_probe_objects(radius, 0)`; they
  collide but do no damage.
- **Crates** (479D0.c) are ammo pickups (10 missiles or 10 side-rams), not
  destruction.
- **The carrier's blast**: when the carrier crashes, `fx_draw_carrier_blast`
  (39050.c, a draw function) grows up to two domes and every frame calls
  `obj_blast_around_carrier(gBlastScales[0] * 283.0f)`, which gives each object
  still standing within that radius of the carrier `obj_blast_damage(pos, 10,
  100000)`: everything near the carrier is levelled.

## Rocking hits (obj_detect_rocking_hits)

Rocking a vehicle back and forth against a building gives a full-damage hit.
The test runs from `obj_hit_tri`, at most once a frame:

- It only counts while the **player's** vehicle (`gCurVehicle`) is Skyfall (3),
  Ramdozer (4), Backlash (5), the American Dream (8), Ballista (10), the police
  car (13), the A-Team van (14) or Starski (15). On foot, Sideswipe,
  Thunderfist, the crane, the train, the J-Bomb, the barges and the Cyclone
  Suit never rock.
- A frame continues the run when an object was also hit the previous frame and
  the sign of `gHitSpeed` is 0 or differs from last frame's (`gObjRockSign`).
  The first frame of a run notes whether the stick x is nonzero; a later frame
  with the stick at 0 or different marks a change.
- After 0x14 (20) such frames in a row, `gObjRockHit` is set if a change was
  seen, and the run restarts. Any other frame resets the run.
- With `gObjRockHit`, the debounce is skipped and `obj_full_damage_rule`
  returns 100 (and clears the flag).

US 1.1 skips this test while the driver's exit probe runs (`gCollExitProbe`,
set by `driver_try_exit`); in US 1.0 the exit probe's own collision passes
could advance the run.

## The tally, money and path buildings

- `obj_tally(onlyFlagged)` (at level setup and at the results) counts buildings
  into `gBuildingCount` (toughness not 1 or 0xFF) and adds destroyed ones to
  `nss.bd` (`gScoreBuildings`) and their values to `nss.ip`. On
  `LEVEL_KIND_BUILDINGS` / `LEVEL_KIND_MISSION_TIMED` levels only path
  buildings count.
- Money: `obj_check_destroyed` adds `value` to `gLevelMoney`; the HUD counter
  (`hud_tick_money_counter`, 00000.c) eases toward it.
- Path buildings: `obj_path_buildings_destroyed` is true when every listed part
  of every `gPathParts` entry is 100; it feeds `gPathBuildingsDestroyed` each
  unpaused frame. `obj_count_destroyed_path_buildings` feeds the HUD. A building
  flagged as a path building on a level without a `gPathParts` list could never
  count as done (inference).

## Per-frame order (`hd_game_frame`, 00000.c)

1. `veh_update_player` (unless paused): the vehicle's own collision pass, so
   direct hits, collapses and rocking happen here.
2. `carrier_update` (its own `obj_collide_vehicle(0xFF)` pass).
3. If not paused: `obj_path_buildings_destroyed`, `obj_check_level_end`, the
   riders (`tnt_ride_vehicle`, `block_ride_vehicle`), the money popups.
4. `obj_draw_all`: falling, landing, toppling, fading, sinking, moving objects.
5. If not paused (or the carrier has crashed, or a bonus level is ending):
   `obj_run_effect_queue`, `obj_update_delayed_damage`.
6. If not paused: `tnt_update`, `block_update`, `shell_update_all`,
   `crate_update`.
7. `fx_draw_all`, `ptcl_update`, `commpt_update`, `dig_coach_update`.
8. The frame's display list, including `fx_draw_carrier_blast` (the carrier's
   blast damage).

## Modding notes

- **Make a building tougher or weaker:** its model's toughness byte
  (`ObjectModel.type`, +4) in the level data. Damage is divided by it (except
  full-damage hits). Never 0; 1 also makes it flimsy (not counted, one-hit for
  Sideswipe, Ballista and the Cyclone Suit).
- **Money value:** `ObjectModel.value` (+8). **People:** byte 9 of the
  placement record. **Immune part:** +0xD.
- **A vehicle's ramming damage:** its `gObjDamageRules` records (for each hit
  sphere: kind and multiplier `n`) and the hit spheres' `value` (set up by the
  vehicle). The speed rule is `|speed| * value * n >> 4`.
- **One-hit vehicles for flimsy objects:** the bits 1, 10, 16 hard-coded in
  `obj_full_damage_rule`.
- **TNT:** the level's TNT record (fuse byte x 60 frames, radius, amount); the
  5-frame post-explosion delay in `tnt_explode`.
- **Missile damage:** the `0x1A4` in `ballista_update`'s `shell_fire` call.
- **Carrier blast:** `10, 100000` in `obj_blast_around_carrier` (679E0.c) and
  `283.0f` in `fx_draw_carrier_blast`.
- **Blast delay:** `util_rand2_range(1, 20)` in `obj_add_delayed_damage`.
- **Rocking:** the run length 0x14 and the vehicle list in
  `obj_detect_rocking_hits`.
- **Pool sizes** (hard-coded): 40 delayed-damage slots, 30 effect-queue slots, 2
  toppling, 4 fading and 1 sinking part, 1 particle emitter, 20 people.
- Code changes go in the `NON_MATCHING` C (the port builds it); the matching
  build uses the original asm.

## Versions

US 1.0 and 1.1 differ here only in `obj_hit_tri`'s `gCollExitProbe` test
(rocking hits during the exit probe, above) and the address of the TNT type
table (`TNT_RADIUS_ADDR`). See [../versions.md](../versions.md).

## Open questions

- What `toppleKeys` really encodes (it is both the topple priority and the fall
  distance).
- What object kinds 0xBA..0xBC are.
- `ObjectModel` bytes 0x02 (the radius source), 0x0C, 0x0E (the pose type is the
  u16 at 0x0E) and 0x10..0x18 (an always-drawn display-list range) are unnamed.
- (Done.) The TNT crate record and type table are `TntCrate` / `TntType`
  in 48D00.c (`fuse`, `litFrame`, `blastDelay`, `blastRadius`, `blastAmount`,
  `pushSpeed`, `riding`, ...; the type's `radius` and `objectRadius`).
- The toppling, fading and sinking slot records are raw offsets.
- Which vehicles' damage rules use 0xFC (attack) and 0xFB (exposed) is in ROM
  data and not documented here.
- `obj_update_delayed_damage` doesn't call `obj_mark_part_gone` or turn on the
  linked triangles when it brings a part to 100, unlike the other two collapse
  paths. A bug or deliberate is unknown.
- The J-Bomb action numbers (which one is the stomp).

## Notable findings

- The carrier's blast damage runs from a draw function, every frame its dome
  is active.
- Blast damage has no falloff: every part in the radius takes the same amount.
- The damage-rule walk is unbounded (it relies on each list ending with 0xFD),
  and a random roll of exactly 100 reads past the debris tables' ends (the
  port keeps this; `port_kind_field`).
- `veh_contacts_has_other` (77E20.c) has no callers.
- Debug cheat (`gDebugCheats`, command line `-c`): Z with the level-complete
  key sets `gCheatAllDestroyed`, which makes `obj_tally` count every building
  as destroyed. See scoring.md.
