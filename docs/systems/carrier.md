# The missile carrier

On the main missions a missile carrier (the code's "CMO": `cmo_hit_request`,
"TOO FAR AWAY FROM CMO") drives across the level. The player has to clear
everything in its way. The carrier never steers and never slows down for an
obstacle: it drives in a straight line along its spawn heading, **any**
collision fails the mission, and it succeeds when it has driven its route
length from the start point.

Everything here is **from the code** unless it says **measured** (bc_headless
runs on level 0, Simian Acres; scripts and results in the agent's scratchpad,
summarised at the end).

Related docs: [destruction.md](destruction.md) (what it hits, its blast),
[collision.md](collision.md), [scoring.md](scoring.md) (level end),
[camera.md](camera.md) (the carrier cameras).

## Where the code is

| file | what |
| --- | --- |
| `src.us.v11/hd_code/75490.c` | `carrier_spawn`, `carrier_update`, `carrier_update_sound`, `carrier_scan_speed_zones`, `carrier_load_collide_dists` / `carrier_check_collide_dists`, `carrier_check_crash`, `carrier_check_arrival`, `carrier_update_anims`, `carrier_build_model`, `carrier_set_physics` (all hand-written asm with NON_MATCHING C) |
| `src.us.v11/hd_code/00000.c` | `carrier_update_proximity` (danger zones), the frame order, the hit/done latch, the carrier cameras |
| `src.us.v11/hd_code/5CB60.c` | `lvl_spawn_carrier` (level load) |
| `src.us.v11/hd_code/77E20.c` | `obj_find_carrier_target`, `obj_calc_target_time`, path buildings |
| `src.us.v11/hd_code/56040.c` | `veh_block_toward_part` (sets `gCarrierContact`) |
| `src.us.v11/hd_code/17E10.c` | `hud_mission_failed`, `hud_level_complete` |
| `src.us.v11/hd_code/39050.c`, `679E0.c` | the explosion dome and its damage |
| `src.us.v11/hd_code/30C70.c`, `3E4C0.c` | the HUD target marker, the target arrow and the radar |

The carrier is vehicle type `VEH_CARRIER` (0xFF); its `VehState` is
`gCarrierState`, its position `gCarrierX/Y/Z`.

## Spawn

The level file holds a 9-byte carrier record (`lvl_spawn_carrier`, at level
offset 0x54, just after the vehicle list):

| off | field |
| --- | --- |
| +0 | u8 speed (0 = no carrier on this level) |
| +1 | BE s16 x (map units, `<< 5`) |
| +3 | BE s16 z |
| +5 | BE s16 heading (12-bit) |
| +7 | BE s16 route length (map units, `<< 5`) |

`carrier_spawn(model, x, z, speed, limit, heading, fp)` sets
`gCarrierX = gCarrierStartX`, `gCarrierZ = gCarrierStartZ`,
`gCarrierTargetSpeed = speed`, `gCarrierRouteLen = limit`, the heading (both
`VS_HEADING` and `VS_MOVE_HEADING`), the ground height (`veh_init_ground` from
y 0x7FFF down), the animation channels, the hit spheres
(`veh_register_parts(0xFF, ..., 0x59D8)`) and the shadow, sets
`gHaveCarrier`, and runs one `carrier_update` with `VS_SPAWN_UPDATE` set
(no animation, no crash test). The same record byte also decides whether the
intro chopper is spawned (`lvl_spawn_chopper`, not on timed missions).

`initlevel` spawns the carrier except when the level is opened for its status
page (`MODE_LEVEL_STATUS`); in attract mode only for demo 0.

## The route

There is no path data. `carrier_update` moves it with
`veh_move_along_heading(..., VS_MOVE_HEADING, ...)` and nothing ever changes
that heading after spawn, so the route is a straight line. Distance travelled
is the straight x/z distance from the start (`math_dist3(gCarrierX, 0,
gCarrierZ, gCarrierStartX, 0, gCarrierStartZ)`).

Several things assume the route runs toward +z: `carrier_scan_speed_zones`
keys on `gCarrierZ`, `obj_find_carrier_target` and
`obj_tri_targets_ahead_done` treat "ahead" as `z >= gCarrierZ`, and the map
view scroll is clamped to `gCarrierStartZ - 400 .. gCarrierStartZ +
gCarrierRouteLen`. A carrier spawned with another heading would break those.

**Measured (level 0):** x stays at 63360; z grows from 64583 by 27 per game
frame (the game ran at 2 retraces a frame); `gCarrierRouteLen` = 96000,
`gCarrierTargetSpeed` = 28.

The carrier follows the terrain like any vehicle: `carrier_set_physics`
(`gGravity = gLevelGravity * 2`, `gBounceThreshold = 60`, `gBounceDamping =
10`), `veh_slope` / `veh_apply_slope` (slope pull and a drag of 1 a frame,
divisor 2400.0) and `veh_ground_contact` with its three wheel slots.

## Speed

```
speed = VS_SPEED
if (speed < gCarrierTargetSpeed) speed = min(speed + 8, gCarrierTargetSpeed)
then slope and drag (veh_apply_slope)
```

- Acceleration is +8 a frame up to the target; only slope and drag ever reduce
  the speed, so on flat ground it sits just under the target (measured 27 a
  frame against a target of 28).
- **Speed zones** (`carrier_scan_speed_zones`): the table `gCarrierSpeedZones`
  of `{s16 z (map units); u8 level; u8 speed}` records, ended by a negative z.
  The last record for this level whose z the carrier has passed sets a new
  `gCarrierTargetSpeed`. So speed changes are scripted by position per level,
  never by obstacles.
- The step is the speed per game frame (measured: z advanced by the speed each
  frame). Whether anything compensates for a lower frame rate was not checked.

## How it fails: there is no "blocked"

`carrier_check_crash`, every frame while it isn't already hit:

```
veh_collision_setup(... gCarrierNonBlockingParts ..., id 0xFF)
veh_collide_spheres(0xFF)                    vehicles, radar markers, comm points
if (!gCarrierContact) {
    gCollHit = 0
    if (gCarrierWorldHitOn) veh_collide_world(0xFF)
    if (gCarrierObjHitOn)   obj_collide_vehicle(0xFF)
    if (!gCollHit && every wheel slot's height changed by < 0x2711) return   all clear
}
cmo_hit_request = 1; gPauseRequest = 1        mission failed
```

So the mission fails when:

1. **A vehicle touches it** (`gCarrierContact`, set by `veh_block_toward_part`
   when the carrier's spheres overlap a part of any vehicle except the driver on
   foot). Driving into the carrier fails the mission; walking into it does not.
2. **It hits a wall or an object** (`gCollHit` from `veh_collide_world` or
   `obj_collide_vehicle`), except parts listed in `gCarrierNonBlockingParts`.
   Objects it hits are damaged the carrier's way: 100 % to flimsy objects
   (toughness 1), nothing to anything else. Whether flattening a flimsy
   object also counts as a crash depends on whether its triangle still set
   `gCollHit` (a part that goes to 100 on the hit lets the vehicle through in
   `obj_hit_tri`); not measured.
3. **A wheel slot's height jumps by 10001 (0x2711) or more** in one frame
   ("tilted": it fell or climbed something).

Other ways the carrier is destroyed: a TNT crate touching it
(`tnt_update`), a Ballista missile touching it (`shell_update_all`): both set
`cmo_hit_request` directly.

`gCarrierCollideDists` (`{s16 level; u16 worldDist; u16 objDist}`, ended by a
negative level) turns world and object collision on only after the carrier has
driven that far on some levels (`gCarrierWorldHitOn` / `gCarrierObjHitOn`,
latched). Why is unknown (perhaps it starts inside scenery).

`gCollBlocked`, `gBlockedFrame` and the free heading arc are computed for the
carrier like for any vehicle but it never reads them. `gProbeHitCarrier` (89250.c)
is about push blocks: a Ramdozer block the carrier touches is shoved along at
the carrier's speed (`block_update`).

## The target ahead and the danger zones

`obj_find_carrier_target` (77E20.c), every frame: the nearest remaining target
to the carrier (3-D distance, starting from 9999999), among

1. standing path buildings (`LevelObject.pathBuilding`, not
   `obj_parts_destroyed`), skipping those behind the carrier (`pos.z <
   gCarrierZ`) unless the carrier is inside their sphere; the recorded point is
   the footprint centre (`centreX/Z`) and `pos.y`;
2. `gBoxTargets` not yet filled (the Ramdozer block slots);
3. `gTriTargets` of this level ahead of the carrier with a solid triangle.

The result is `gCarrierTargetX/Y/Z` and `gCarrierTargetDist` (9999999 = none).
Only the parts listed for each path building in `gPathParts` have to go, not
the whole building.

`carrier_update_proximity(gCarrierTargetDist)` (00000.c), in `MODE_LEVEL` /
`MODE_MAP_VIEW` when no window is pending and the game isn't paused, sets
`gCarrierDanger`: 2 when the distance is under `near`, 1 under `far`, else 0.

| level | far | near |
| --- | --- | --- |
| default | 12000 | 6000 |
| 9 | 12000 (36000 once `gCarrierZ > 100320`) | 6000 (18000) |
| 12 | 26000 | 20000 |
| 13 | 55000 | 35000 |
| 14 | 14000 / 22000 / 25000 (z > 80000 / > 177600) | 8000 / 17500 / 20000 |
| 15 | 24000 | 18000 |
| 16 | 33000 | 23000 |
| 18 | 36000 | 28000 |
| 29 | 22000 | 14000 |
| 58 | 36000 | 30000 |

**Measured (level 0, default thresholds):** `gCarrierTargetDist` started at
38947 and fell by 27 a frame; `gCarrierDanger` became 1 between 12335 and
11986 and 2 between 6024 and 5495.

What a change does:

- 0 to 1 or 2: the level's danger tune (`music_push_danger_tune`,
  `gLevelDangerTunes[levelno]`); a warning panel if the player is more than
  20000 from the carrier.
- 2: the alarm loop `SFX_CARRIER_ALARM` and window `YW_CARRIER_ALARM`.
- 1: window `YW_CARRIER_DANGER`, or the first time the hint
  `YW_HINT_CARRIER_NEAR`; while that hint shows, the camera looks at the
  target (camera.md). **Measured:** the hint opened at danger 1 and paused the
  game (`gPaused`, the level clock `gSchedRetraceCount` stopped) until A was
  pressed.
- back to 0: alarm off, tune popped (not on a timed mission whose path is
  clear: "WILL THIS FIX IT!!?"), a chime if it was 2.

## Success and failure

`carrier_check_arrival`: when the distance from the start reaches
`gCarrierRouteLen`, `gLevelDoneRequest = gPauseRequest = 1`. The carrier
has no end object; `obj_check_level_end` (the driver touching the end object)
on a carrier level additionally needs every path building, box target and
triangle target ahead cleared.

The requests are latched at the end of `hd_game_frame`: `gLevelDoneRequest`
becomes `gLevelDone`, otherwise `cmo_hit_request` becomes `gCarrierHit` (the
general "mission failed" flag, also used by timeouts and fail triggers).

When either is set on a carrier (or shuttle) level, `hd_build_frame_dl`
switches to `MODE_CARRIER_END` (or `MODE_REPLAY_CARRIER_END` in a replay): the
carrier camera, input zeroed, windows off.

### The crash

- On the hit frame: the HUD panel stops, the wheel animation stops, effects and
  the effect queue are cleared (`fx_finish_all`, `obj_clear_effect_queue`).
- After 5 frames (`gCarrierExplodeDelay`) the explosion billboard spawns;
  18 (0x12) frames later `gCarrierExplodeDone` is set. From then on the carrier
  isn't drawn, and random debris is doubled.
- `fx_draw_carrier_blast` (39050.c) tints the screen red and grows up to two
  red domes (+0.06 scale a frame, the second 10 frames later). Every frame a
  dome is active it calls `obj_blast_around_carrier(gBlastScales[0] * 283.0f)`,
  which gives every standing object within that radius
  `obj_blast_damage(pos, 10, 100000)`: the blast levels everything it reaches.
- `hud_mission_failed` (17E10.c): `SFX_FAIL_CRASH`, music fade, after 180
  frames `SFX_MISSION_FAILED` and a fade, then `MODE_RESULTS` if the level was
  completed before, otherwise `MODE_REPLAY` (the player's run replayed, then
  the level restarts).

**Measured (level 0, the carrier left to crash):** it stopped at z = 99040
with `gCarrierTargetDist` 4603 (so the first path building's front is about
4600 from its centre); `gCarrierHit` = 1 and mode 0x200
(`MODE_CARRIER_END`) for about 180 frames, then mode 0x40 (`MODE_REPLAY`)
for about 120 frames (the attempt), then the level restarted (mode 4, the
carrier back at its start).

## Sound, HUD

- `carrier_update_sound`: the engine loop (`SFX_CARRIER_LOOP`), silent beyond
  16000 from the player, volume `0x7FFF - 2 * max(d - 4000, 0)`, pan by dx.
  `carrier_update_anims` sets its pitch and the wheel animation from the speed.
- `hud_draw_target_marker` (30C70.c) colours the target marker by
  `gCarrierEta`: `obj_calc_target_time` = distance from the carrier to the
  marked target divided by `gCarrierTargetSpeed` (frames, roughly). Green at
  1501 or more, red under 500, blended between.
- `hud_draw_target_arrow` (3E4C0.c): a 3-D arrow toward `gCarrierTargetX/Z`,
  green over 1500 map units, red under 500, blinking under 250.
- `hud_draw_radar` (3E4C0.c): the carrier and target blips, zoomed by the
  player's distance from the carrier.

## Per-frame order (`hd_game_frame`)

1. `gCarrierContact = 0`.
2. `carrier_update_sound` (LEVEL / MAP_VIEW).
3. `veh_update_player`: the player's collisions can set `gCarrierContact`.
4. `carrier_update` (not in the intro modes; paused or hit, it only animates),
   which ends with `carrier_check_crash` and `carrier_check_arrival`.
5. `carrier_update_proximity(obj_find_carrier_target())`.
6. Path buildings, `obj_check_level_end`, effects, delayed damage (these also
   run while paused once the carrier is hit, so the blast plays out), TNT,
   blocks (pushed by the carrier), shells.
7. The frame display list: `fx_draw_carrier_blast`, `hud_level_complete` /
   `hud_mission_failed`, the switch to `MODE_CARRIER_END`.
8. The hit/done latch.

## Shuttle levels

Levels with a shuttle (`gHaveShuttle`, 8DDB0.c) are the same idea with a
spline path instead of a straight line (`util_shuttle_path_step`, 23C20.c):
the path's end requests level done (or failure, branch 1). They share
`MODE_CARRIER_END`, the map view ("SHUTTLE VIEW"), the end-object gating and
the camera cases.

## Modding notes

- **Speed:** the level record's speed byte (`gCarrierTargetSpeed`), the
  per-level `gCarrierSpeedZones`, the `+8` acceleration in `carrier_update`.
  `obj_calc_target_time` divides by `gCarrierTargetSpeed`, so keep it nonzero.
- **Route length:** the record's s16 at +7. The heading at +5 (keep it +z, see
  above).
- **Unstoppable carrier:** skip the `cmo_hit_request = 1; gPauseRequest = 1`
  in `carrier_check_crash` (NON_MATCHING C), or return early. Raising
  `gCarrierCollideDists` only disables world and object hits; vehicle contact
  and the height test remain. It will still drive into whatever is there.
- **Danger distances:** the `switch (levelno)` in `carrier_update_proximity`.
- **HUD colours:** 1501 / 500 in `hud_draw_target_marker`, 1500 / 500 / 250
  in `hud_draw_target_arrow`.
- **The blast:** 5 and 0x12 frames in `carrier_check_crash`; 0.06, 10 frames
  and 283.0 in `fx_draw_carrier_blast`; radius 10 and amount 100000 in
  `obj_blast_around_carrier`.
- Game-code changes go in the `NON_MATCHING` C (the port builds it).

## Versions

No carrier code differs between US 1.0 and 1.1; only addresses move (see
[../versions.md](../versions.md)).

## Open questions

- Fields of `gCarrierState` beyond the shared `VehState` ones; what animation
  channel 2 (picked by thirds of the camera yaw) shows; what `gCarrierTexAnim`
  animates.
- Why `gCarrierCollideDists` delays collision on some levels.
- `lvl_object_centre` reads the footprint box from the model; whether it is
  already in world coordinates is unclear.
- Box and triangle targets set only `gCarrierTargetX/Z`; `gCarrierTargetY`
  keeps an older candidate's value (the hint camera uses it).
- Stale comments: in variables.h and 77E20.c several carrier variables are
  described as "the player's" (`gCarrierTargetX /* player x */`), and
  `gCarrierTargetSpeed` as a "divisor": all of them are the carrier's.

## Measurements

bc_headless (this branch's build), level 0 reached through
`port/data/levels_input.txt` with the level poke, then no input (or an A tap
every 100 reads to close hint windows), RAM read from dumps every 20-25
frames: `gCarrierX/Y/Z`, `gCarrierTargetDist`, `gCarrierDanger`,
`gCarrierHit`, `gGameMode`, `gPaused`, `currentYoshiWindow`. The numbers above
are from those runs.
