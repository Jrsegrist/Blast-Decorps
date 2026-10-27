# The game camera

This is the N64 game's own camera (`cam_update`), not the PC port's orbit
camera (port/src/live/live_orbit.cpp), which only reads this one.

In normal play the camera does **not** chase from behind the vehicle. It
looks at the player from a fixed compass direction (`gCamHeading`, 135
degrees at level start) at a per-vehicle distance and height; C-left and
C-right turn that direction in 45-degree steps, C-up and C-down zoom. The
vehicle's heading only feeds a look-ahead offset that shifts the view in the
direction of travel as you accelerate. Hint windows, the level end, the
carrier's crash, the intro chopper, map view and replays have cameras of their
own.

Everything here is **from the code** unless it says **measured** (bc_headless
on level 0 in the Ramdozer; see the end).

Related docs: [vehicles.md](vehicles.md), [carrier.md](carrier.md).

## Where the code is

| file | function | what |
| --- | --- | --- |
| `src.us.v11/hd_code/00000.c` | `cam_update` | the whole camera, once a frame, called from `hd_build_frame_dl` |
| | `cam_turn_heading` | the C-left / C-right turns |
| | `cam_polar_offset` | the eye's horizontal offset for a distance and heading |
| | `cam_fixed_heading_area` | places where the heading is locked to 135 |
| | `cam_eye_scale` | the culling view's distance factor per vehicle |
| | `hd_setup_level`, `hd_game_frame`, `hd_main_thread` | resets, zoom, snaps |
| `src.us.v11/hd_code/60D50.c` | `cam_apply_height_zone`, `cam_load_level_defaults` | per-position camera height; the start-of-level distance |
| `src.us.v11/hd_code/1D990.c` | `cam_set_level_fovy`, `hud_init_objective` | field of view; fixed-eye levels and the lift settings |
| `src.us.v11/hd_code/23C20.c` | `util_cam_path_init` / `_step` | scripted camera paths (offsets from the carrier or shuttle) |
| each vehicle's `*_enter` | | `gCamDistance`, `gCamHeightBase` for that vehicle |
| `src.us.v11/hd_code/7FB50.c` | `jbomb_update_cam_distance` | the J-Bomb's altitude-dependent camera |

## Outputs and units

- `gCamEyeX/Y/Z` (s32, world units `<< 11`): the eased eye.
- `gCamAtX/Y/Z` (f32, world units / 32): the eased look-at point.
- `gCamYaw` (0..0xFFF) and `gCamPitch`: computed from the final eye and
  target, read by the sky (`backdrop_draw`), billboards and particles.
- Three matrices in the frame's `DynamicBuf` (types.h): `view`
  (`guLookAtReflect`, with the reflection-mapping `lookAt`), `cullView`
  (`guLookAt` for the visibility test) and `persp` (`guPerspective(gCamFovy,
  4:3, near 10, far 10000; 20000 in replays)`).

`cam_update` runs in `hd_build_frame_dl`, after the level and object display
lists are built, so visibility and level-DL selection use the previous
frame's camera.

## Per-frame flow inside cam_update

```
cam_turn_heading                         C-left / C-right: gCamHeadingGoal +-45, 3 degrees a frame
hint-camera flags                        gCamOnEndObject / gCamOnCarrierTarget / gCamOnMarkedTarget / gCamWindowView
gCamSnap |= map view, shuttle, gFramesInMode < 2
pick easing rates                        (table below)
gCamType = gCurVehicle                   (VEH_CARRIER / SHUTTLE / CHOPPER in their modes)
per-type look-ahead settings             (table below)
gCamTilt ramps toward the speed while A or B is held
offX/offZ = look-ahead along gCamTurn (eased toward gPlayerMoveHeading)
eye goal and target goal by mode         (normal follow below)
zoom: eye goal scaled away from target by gCamZoom
ease the eye (or snap); camera lift; ease the target
shake (target only); attract pull-in
build view / cullView / lookAt, gCamYaw, gCamPitch; gCamSnap = 0
```

## Normal follow

```
cam_polar_offset(gCamDistance, gCamHeading, &px, &pz)   135 degrees -> (+d, -d)
eye goal  = (gPlayerX + px + offX, gPlayerY + gCamEyeHeight, gPlayerZ + pz + offZ)
target    = (gPlayerX + offX, gPlayerY + gCamAtHeight, gPlayerZ + offZ)
eye goal  = target + (eye goal - target) * gCamZoom
```

- `gCamDistance` is the offset **per axis** (the horizontal distance is
  `d * sqrt(2)` at the diagonal headings).
- `gCamHeight` = the height zone's height plus `gCamHeightBase` (below);
  `gCamEyeHeight` is `gCamHeight` plus any lift.
- `gCamZoom` (`hd_game_frame`): `gContButtons & 4` (libultra's
  `D_CBUTTONS`, C-down) raises it by 0.05 a frame up to 1.5 (zoom out), `& 8`
  (`U_CBUTTONS`, C-up) lowers it to 1.0; at the top a further zoom out
  switches to map view on carrier and shuttle levels (from the code; not
  measured). C-right (`R_CBUTTONS`, 1) and C-left (2) turn the heading.

**Measured (Ramdozer, level 0):** `gCamHeading` 135.0, `gCamDistance` 3000,
`gCamHeight` 6408, `gCamZoom` 1.3 at the start, eye minus player =
(+3900, +8330, -3900), i.e. 3000 x 1.3 and 6408 x 1.3. Driving and steering
did not change `gCamHeading` or `gCamYaw` (1535 = 135 degrees). One C-right tap
turned the heading from 135 to 180 at 3 degrees a frame. Holding button bit 4
raised `gCamZoom` from 1.3 to 1.5 by 0.05 a frame, bit 8 lowered it to 1.0.

### Per-vehicle distance and height (the `*_enter` functions)

| vehicle | `gCamDistance` | `gCamHeightBase` | set in |
| --- | --- | --- | --- |
| driver on foot | 3400 | 0 | `driver_spawn`, `driver_try_exit` (69BB0.c) |
| Sideswipe | 3400 | 300 | `sideswipe_enter` (6B4A0.c) |
| Thunderfist | 4200 | 3000 | `thunderfist_enter` (6C5E0.c) |
| Skyfall | 3400 | 1000 | `skyfall_enter` (6E200.c) |
| Ramdozer | 3000 | 1000 | `ramdozer_enter` (6E200.c) |
| Backlash | 3400 | 300 | `backlash_enter` (71140.c) |
| crane | 6000 | 9000 | `crane_enter` (75490.c) |
| train | 3000 | 0 | `train_enter` (772A0.c) |
| American Dream | 3000 | 1000 | `amdream_enter` (72B80.c) |
| Ballista | 3400 | 1000 | `ballista_enter` (853D0.c) |
| barges | 3000 | 0 | `barge_enter` (83910.c) |
| police car, A-Team van, Starski | 3000 | 1000 | `police_enter`, `ateam_enter`, `starski_enter` |
| Cyclone Suit | 2000 | -1000 | `cyclone_enter` (8AEE0.c) |
| J-Bomb | 3400 / 600 low, 1000 / 3000 high | | `jbomb_update_cam_distance`: blends between `gJbombLiftFadeY` and `gJbombMaxY` (LevelInfo), so the camera rises and closes in as it flies higher |

Before the player enters a vehicle (`gVehEntered == 0`) the values come from
`cam_load_level_defaults`: `gVehCamDefaults[gStartVehicle]` (distance, base,
target height), indexed by the start vehicle's type.

### Per-vehicle look-ahead (`switch (gCamType)` in cam_update)

`gCamTilt` follows the speed (clamped to `[gCamTiltMin, gCamTiltMax]`) at
`tiltRate` a frame while A or B is held; the look-ahead is `radius * gCamTilt`
along `gCamTurn`, which turns toward `gPlayerMoveHeading` by `turnRate` a frame.
(The "tilt" names are historical: nothing tilts.) `gCamAtHeight` raises the
look-at point above the vehicle.

| type | radius | tiltRate | turnRate | `gCamAtHeight` | tilt max / min |
| --- | --- | --- | --- | --- | --- |
| driver | 20 | 0.25 | 15 | 0 | 0x55 / -0x19 |
| Sideswipe | 11 | 0.75 | 30 | 0 | 0xCD / -0xA5 |
| Thunderfist | 11 | 0.75 | 30 | 2000 | 0x91 / -0x55 |
| Skyfall | 7 | 3.0 | 30 | 0 | 0xEB / -0xA5 |
| Ramdozer | 9 | 0.75 | 30 | 0 | 0xEB / -0xA5 |
| Backlash | 9 | 0.75 | 30 | 0 | 0xCD / -0x69 |
| crane | 11 | 0.75 | 30 | 5000 | 0 / 0 (no look-ahead) |
| train, barges | 11 | 0.75 | 30 | 0 | 0xEB / -0xA5 |
| American Dream | 7 | 3.0 | 30 | 0 | 0x14F / -0xA5 |
| J-Bomb | 11 | 0.75 | 30 | 1000 | 0x87 / -0x55 |
| Ballista | 9 | 0.75 | 30 | 0 | 0xF5 / -0x87 |
| police / A-Team / Starski | 7 | 3.0 | 30 | 0 | 0x145 / 0x13B / 0x131, -0xA5 |
| Cyclone Suit | 2 | 0.75 | 30 | 600 | 0x91 / -0x55 |
| carrier, chopper, shuttle | 11 | 0.75 | 30 | | 0 / 0 |

**Measured (Ramdozer):** `gCamTilt` rose by 0.75 a frame while A was held
(0 to 60 in 80 frames) and fell with the speed after release.

### Easing

| situation | eye rate x / y / z (fraction of the gap a frame) |
| --- | --- |
| default | 0.5 / 0.03 / 0.5 |
| J-Bomb | 0.5 / 0.06 / 0.5 |
| intro chopper modes | 0.06 / 0.03 / 0.03 |
| hint windows, level done, `MODE_LEVEL_CLEAR` | 0.08 / 0.03 / 0.08 |
| carrier hit (carrier-end modes) | 0.2 / 0.1 / 0.2 |
| RDU / pill-maze replay | 0.1 / 0.03 / 0.1 |

So the camera keeps up sideways at once but follows height changes slowly. When
`gCamType` changes (a vehicle switch) or a hint camera toggles, the target
glides from the old target: the difference decays by 0.95 (x, z) and 0.97 (y)
a frame until under 0.1. `dampX/Y/Z` in cam_update are assigned and never read.

## Heading locks

`cam_fixed_heading_area` holds the heading at 135 degrees (C-turns refused with
`SFX_DENIED`) in the globe sequences, attract mode, all of level 0x3B, and
parts of levels 4, 13 and 16 (coordinate tests in the function).

## Height zones and lift

- `cam_apply_height_zone(gLevelData)` (60D50.c, every frame): the highest of
  the level's 10-byte height zones `{s16 minX, minZ, maxX, maxZ, height}`
  (level offsets 0x40..0x44) containing the player sets `gCamHeight = (height
  << 5) + gCamHeightBase`. A zone height of 3000 means "none" (`gCamNoHeightZone`;
  at heading ~135 the eye then sits 14600 above the player).
- Lift (`LevelInfo.camLift`, `camLiftMax`, `camLiftStep`, `camLiftRadius`, set by
  `hud_init_objective`): when the heading is not ~135, a collision probe at the
  eye (`coll_probe_tris(..., kind 0xBE)`) raises `gCamEyeHeight` by the step
  while it hits geometry (radius +1600 hysteresis) up to the maximum, and
  lowers it back otherwise.
- `chopper_find_cruise_height` uses the same zone format for the intro
  chopper's flying height, not the camera.

## Field of view

`cam_set_level_fovy(levelno)` (1D990.c, at level setup): 45 degrees, or 80
with `gWideFovLevel = 1` on the levels in `gWideFovLevels` (5 entries).
`gWideFovLevel` also changes vehicle handling in 62740.c (wider speed bands,
turn rate). Aspect 4:3, near 10, far 10000 (20000 in replays).

## Other cameras (by mode)

- **Fixed eye** (`gCamFixedEye`, `gFixedEyeLevels`, set by
  `hud_init_objective`): the eye stays at a fixed point and turns to follow the
  player; a blinking icon shows it.
- **Hint cameras:** while certain windows show, the camera looks at the end
  object (`gCamOnEndObject`, also `MODE_LEVEL_CLEAR`), the carrier's next
  target (`gCamOnCarrierTarget`, `YW_HINT_CARRIER_NEAR`) or the marked target
  (`gCamOnMarkedTarget`): eye 2500 out along the heading and 5000 up.
- **Control-method window** (`gCamWindowView`, `YW_CONTROL_METHOD`): the eye
  swings in front of the vehicle to show it.
- **Map view** (`MODE_MAP_VIEW`, "MISSILE VIEW" / "SHUTTLE VIEW"): high above
  the carrier (+4000, +25000, +7000 + scroll; Z/R scroll it while paused), or
  chasing the shuttle; always snapped.
- **Carrier end** (`MODE_CARRIER_END`, carrier.md): a wide shot of the carrier
  (+16000, +15000, -3000 after a crash; +4000, +2400, -4000 when done), mirrored
  in the replay version.
- **Replays** (`MODE_REPLAY`): missions watch the carrier; target and damage
  levels chase from behind (`gPlayerMoveHeading`, twice the distance); races
  and building levels look from in front; RDU levels look from the next RDU to
  be lit.
- **Intro** (`MODE_INTRO_APPROACH` / `FLY` / `LANDING`): the eye near the
  player, looking at the BCT chopper (`gChopperX/Z`, `gChopperDropY`).
- **Camera paths** (`util_cam_path_*`, `gCamPaths`, `gCamPathPoints`): cardinal
  splines evaluated each unpaused frame into `gCamPathX/Y/Z`, used as an offset
  from the carrier or shuttle.
- **Shuttle follow** (`gShuttleCamFollow`): set by a shuttle path event.
- **Attract demos:** heading locked; with `gRecDemoNoLogo` the eye is pulled in
  (`gAttractCamZoom` down to 0.4).

## Snaps and cuts (`gCamSnap`)

A snap puts the eye and target straight on their goals for one frame. Set by:
entering or leaving map view (`hd_main_thread`), the yoshi pages, any mode
change (`gFramesInMode < 2`, which includes level start), the shuttle and map
cameras (every frame), the control-method window toggling,
`veh_respawn_if_out` (out of bounds, respawned) and `veh_wrap_position`
(level 0x17's wrap-around). **A vehicle switch does not snap**: the camera
glides to the new vehicle with the easing above. The port turns a snap into a
cut for its frame interpolation (`PORT_ICUT_IF(gCamSnap)` at the end of
`cam_update`).

## Shake

`gShakeTime` frames, `gShakeSize` units: cam_update adds a pseudo-random offset
(from `gFrameDrawTime`, within +-size/2) to the **look-at point only** (not the
eye or the culling view), in LEVEL, ATTRACT, CARRIER_END, the globe sequences
and the yoshi pages. Each frame `gShakeTime--`, `gShakeSize -= gShakeSize / 6 -
1`. Setters: see [destruction.md](destruction.md) (hits, collapses, TNT,
shells); also the J-Bomb's stomp landing (20, 800), Sideswipe's ram hit
(10, 600) and shuttle path events (20, 400).

## Culling

`cullView` is a look-at from `at + (eye - at) * cam_eye_scale(...)`: the
factor per vehicle (3.5 on foot, 1.4 Sideswipe, 1.6 Thunderfist/crane/train/
J-Bomb/barges/Cyclone, 4.8 Skyfall/American Dream/cars, 5.0 Ramdozer/
Backlash/Ballista), reduced so the distance stays under 10000. A factor above 1
pushes the culling eye back (a wider volume); the comments that say "pulled
in" don't match the arithmetic. `cull_start` (00000.c) uses it with the
previous frame's projection; `cull_restart(n)` (5FD50.c) marks every cell
visible for n frames when the camera jumps (C-turns, map view, window view,
intro).

## Alpha zones

`veh_zone_alpha` (62740.c) fades **vehicles** by their own position in
`gAlphaZones` (0x24-byte records: x, y, z, radius, mode, owning object,
enabled, ...); the camera eye isn't involved. A destroyed object turns its
zones off.

## Sound listener

`gCamEyeX/Y/Z` is commented as the listener, but only `shuttle_update_volume`
(8DDB0.c) uses it for sound in the C; most positional sounds (e.g. the
carrier's) use the player's position.

## Per-frame order (`hd_game_frame`)

1. Zoom buttons and map-view toggles.
2. Vehicle updates (set `gPlayerX/Y/Z`, speed and headings; the J-Bomb sets its
   camera distance).
3. `util_cam_path_step` (unpaused), `cam_apply_height_zone`.
4. Level and object display lists (with last frame's visibility).
5. `hd_build_frame_dl` -> `cam_update` -> sky -> `guPerspective` -> the scene
   and HUD.

## Modding notes

- **Follow distance / height per vehicle:** `gCamDistance` / `gCamHeightBase`
  in that vehicle's `*_enter` (table above); the J-Bomb's in
  `jbomb_update_cam_distance`.
- **Aim height and look-ahead:** the `switch (gCamType)` in `cam_update`.
- **Lag:** the easing rates at the top of `cam_update` (0.5 / 0.03 / 0.5) and
  the target decay (0.95 / 0.97).
- **Default angle and turning:** `gCamHeading = 135.0f` in `hd_setup_level`;
  the 45-degree step and 3-degree speed in `cam_turn_heading`; locked areas in
  `cam_fixed_heading_area`.
- **Zoom range:** 1.0..1.5, 0.05 a frame, in `hd_game_frame`.
- **FOV:** `cam_set_level_fovy` (45 / 80) and `gWideFovLevels`; near / far in
  `hd_build_frame_dl`.
- **Move the eye far out** and geometry may pop: the visibility test uses the
  `cam_eye_scale` distance capped at 10000.
- Code changes go in the `NON_MATCHING` C (the port builds it).

## Versions

No camera code differs between US 1.0 and 1.1.

## Open questions

- `gVehCamDefaults` is indexed by `gStartVehicle` (a vehicle type, set by
  `lvl_find_start_vehicle`); its name (it was named per level before) and
  the comments in 60D50.c and variables.h now say so. Open: it has 21
  entries for 19 vehicle types.
- Whether `gVehEntered` is 0 during the intro (which would make the chopper's
  look-ahead case dead).
- The yoshi item numbers (0x14D, 0x130, 0x164) that select the hint cameras.
- `gCamZoom` starts at 1.3 (measured); where that value is set wasn't traced.
- `mb.c` (34430.c: `mb_render_view`, `mb_push_sample`) renders the player's
  vehicle from the camera eye into a small texture; what it is for is unknown.
- In `MODE_REPLAY_CARRIER_END` the crash eye uses `-offZ` where every other case
  uses `+offZ` (a slip, perhaps).

## Notable findings

- The camera never rotates with the vehicle; only the C buttons turn it.
- Shake moves only the target.
- Camera types without a case (type 12) leave locals uninitialised in the
  original; the NON_MATCHING build zeroes them.
- `gCamTurn` wraps with 4095.0 instead of 4096; `gCamYaw = 0x2000` at setup is
  out of range until the first frame.

## Measurements

bc_headless, level 0 (Ramdozer), reached with `port/data/levels_input.txt`
and the level poke; input: hold A for 80 reads, a C-right tap, then button
bits 4 and 8 held for 40 and 20 reads; RAM read every 2 frames
(`gCamHeading`, `gCamZoom`, `gCamTilt`, `gCamDistance`, `gCamHeight`,
`gCamEyeHeight`, `gCamEyeX/Y/Z`, `gCamAtX/Y/Z`, `gCamYaw`, `gPlayerX/Y/Z`).
