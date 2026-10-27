# Vehicles

Blast Corps has about twenty drivable things: bulldozers, trucks, robot
suits, cars, a train, barges, a crane, and the driver on foot. The code calls
them "diggers" ("changing to digger %d"). Each type has its own file with a
spawn, enter, leave, exit check and per-frame update function; they share a
state block (`VehState`), a speed-band model for throttle, a grip model for
skidding, three wheel slots for the ground, and the collision code.

Everything here is **from the code** unless it says **measured** (bc_headless
on level 0 in the Ramdozer; see the end).

Related docs: [collision.md](collision.md) (ground, walls, hit spheres),
[destruction.md](destruction.md) (what a hit does), [camera.md](camera.md)
(the per-vehicle camera), [carrier.md](carrier.md).

## The vehicle types (`VEH_*`, include/game/types.h)

| id | vehicle | file | notes |
| --- | --- | --- | --- |
| 0 | driver on foot | `hd_code/69BB0.c` | `driver_*` |
| 1 | Sideswipe | `6B4A0.c` | side rams |
| 2 | Thunderfist | `6C5E0.c` | robot suit, shoulder roll |
| 3 | Skyfall | `6E200.c` | buggy with a boost |
| 4 | Ramdozer | `6E200.c` | bulldozer, pushes blocks |
| 5 | Backlash | `71140.c` | truck with a power slide |
| 6 | crane | `75490.c` | stationary |
| 7 | train | `772A0.c` | rail line, deck |
| 8 | American Dream | `72B80.c` | car |
| 9 | J-Bomb | `7FB50.c` | flying robot, stomp |
| 10 | Ballista | `853D0.c` | motorbike with missiles |
| 11, 17, 18 | barges | `83910.c` | one shared implementation |
| 12 | (none) | | no code; a spawn record of type 12 hits a debug trap |
| 13 | police car | `86F60.c` | car |
| 14 | A-Team van | `88160.c` | car |
| 15 | Starski | `8AEE0.c` | car |
| 16 | Cyclone Suit | `8AEE0.c` | robot suit, charge |
| 0xFD | shuttle | `8DDB0.c` | path-driven |
| 0xFE | BCT chopper | `72B80.c` | the intro chopper |
| 0xFF | missile carrier | `75490.c` | see carrier.md |

The four "cars" (8, 13, 14, 15) share one code shape (`police_spawn` is the
template) and open the car hint (`YW_HINT_CAR`) when entered.

## Data structures

### VehRecord (`gVehicles[]`, 0x74 bytes, types.h; `gVehiclesEnd`)

One per loaded vehicle model, used for drawing: segment bases and matrix
buffers (`seg6`, `seg7Odd` / `seg7Even`), the three draw layers' display lists
per buffer (`dlOdd0..2`, `dlEven0..2`), the shadow and motion-blur lists
(`shadowDl`, `blurDl`; see [rendering](rendering.md)), `type` (0x5C), `alpha` (0x60, set by alpha
zones), `x, y, z` (0x64..0x6C, written by `veh_set_record` at the end of each
update) and `onMovingPart` (0x70). `veh_add_record` (5CB60.c) appends one at
spawn.

### VehState (0xA6 bytes, `gDriverState`, `gSideswipeState`, ...)

The block every update reads (types.h; offsets `VS_*`). The main fields:

| off | field | meaning |
| --- | --- | --- |
| 0x04 | `slotY[3][3]` | the three wheel slots' heights: now, last frame, the frame before |
| 0x28..0x40 | `slotLaunchVel`, `slotLaunchY`, `slotAirTime` | per airborne slot |
| 0x4C | `heading` | facing, 12-bit (0..0xFFF; 0 = +z, 0x400 = +x) |
| 0x4E | `moveHeading` | direction of travel this frame |
| 0x50 | `surface` | average surface class under the slots |
| 0x52 | `wheelOffsets[3][2]` | {x, z} of the slots |
| 0x74 | `momentumHeading` | turns toward the facing; the travel follows it while skidding |
| 0x76 | `speed` | s16: position units moved per game frame |
| 0x78 | `bands[5][3]` | the speed bands `{lo, hi, accel}`; [0] is reverse, [4].hi the forward top speed |
| 0x96 | `slotAirborne[3]` | |
| 0x99 | `skidding` | |
| 0x9A | `spawnUpdate` | set during the update run by `*_spawn` |
| 0x9B..0x9E | `onMovingPart`, `hitPartDown`, `hitObject`, `hitImmunePart` | |
| 0xA0 | `pushDivisor` | see collision.md |
| 0xA1 / 0xA2 | `action` / `actionAux` | per-type state (J-Bomb mode, Thunderfist roll, crane) |

Each type's block sits after its 32 animation channels (`gXxxAnim = state -
0x300`) and before x, y, z (`PortVehPos`).

### Units

Positions are s32 world units, 32 to a level-data unit (`<< 5`). Speed is the
step per game frame along the heading (`veh_move_along_heading`; slopes shorten
it). **Measured:** at a speed of 253 the Ramdozer moved 253 units a frame
(heading 71: dx 27, dz 251). The game ran at 2 retraces a frame on level 0, so
a speed of 250 is about 7500 units a second.

## Spawning

`lvl_spawn_vehicles` (5CB60.c) walks the level's 9-byte records `{u8 type, BE
s16 x, y, z, heading}` (level offset 0x50..0x54) and calls `lvl_load_model`
and the type's `*_spawn`. On non-mission levels a Sideswipe record (type 1)
becomes the player's chosen vehicle (`gDiggerVehicle`). Types 6, 7, 0xB, 0x11
and 0x12 have decks (`gHaveRideables`). In attract mode only the recorded
vehicle is spawned. `lvl_find_start_vehicle` picks the vehicle placed on the
driver's start spot (`gStartVehicle`).

A `*_spawn` follows one template: two matrix buffers from the heap,
`veh_add_record`, `veh_reset_state`, the wheel and stand-on tables, position
and heading, `veh_init_ground`, the animation channels, **the band table**,
`veh_register_parts` (hit spheres) and `veh_add_shadow`, then one update with
`spawnUpdate` set.

The respawn table `gRespawnPoints` is the same list (level offset 0x50), so a
vehicle that leaves the level's bounds is put back at its own spawn record.

## The dispatchers (00000.c)

- `veh_update_player` switches on `gCurVehicle` and calls that type's update.
- `veh_update_others`: parked vehicles standing on moving level parts are
  re-grounded (`*_reground`); other types print "MOVEABLE GEOMETRY MOVE ROUTINE
  NOT WRITTEN YET".
- `veh_enter`, `veh_leave`, `veh_exit_check`: switches over the types' functions.
- `veh_update_riders`: vehicles standing on the player's deck follow it.

## Per-frame update

A wheeled vehicle's update (Sideswipe's is representative):

```
zone alpha, animations
veh_steer_throttle(rate, &heading, veh, &speed, mode, flags, bands, delta)
    veh_steer   heading += stick x * rate / 80
    veh_throttle  speed from the bands
veh_turn_heading(veh, &momentumHeading, grip, ...)   skid model -> moveHeading
veh_slope / veh_apply_slope(..., divisor)           slope pull, drag
veh_move_along_heading(slope, moveHeading, &speed, &x, &z)
veh_ground_contact(...)                             bounds, wheel slots, surface, stand-on
anim_update_model, build the model (hit spheres placed)
veh_collision_setup / veh_collide_spheres / veh_collide_world / obj_collide_vehicle
blocked -> veh_prepare_escape, turn into the free arc; hit -> roll back and bounce
publish gPlayerX/Y/Z, gPlayerSpeed, gPlayerHeading, gPlayerMoveHeading; veh_set_record
```

The order of the whole frame (`hd_game_frame`): `veh_apply_switch` (on foot),
`cont_update`, the exit check (Z), `veh_update_others`, the chopper,
`veh_update_player`, the carrier, then riders, TNT and blocks.

## Throttle: the speed bands (`veh_throttle`, 62740.c)

Each vehicle has five bands `{lo, hi, accel}`: [0] is reverse, [1]..[4]
forward. While accelerating, the band containing the current speed gives the
acceleration: `speed += accel * traction`, where `veh_drive_traction` is 4
with any wheel on the ground and 0 fully airborne. Above the last band the
speed falls by 6 a frame, so `bands[4].hi` is the top speed. Braking or
reversing direction moves the speed by the call's `delta`. Z brakes.

- **Surface:** `veh_band_accel` searches `5` bands on surface 1, `7 - surface`
  on classes 3..6: rough ground drops the top bands and so the top speed (see
  collision.md).
- `gVehSlowed` (touching a push block or TNT): top speed halved and two bands
  fewer (not on level 0x22, not barges).
- With method 1 (analog throttle), the stick's y scales the top speed
  (`bands[13] * stickY / 80`).
- `veh_widen_bands` doubles every band on wide-FOV levels (`gWideFovLevel`),
  only for Skyfall, Ramdozer and Backlash.
- `veh_apply_slope` adds the slope pull (`slope drop / divisor * -4`) and a
  drag of 1 a frame (3 for the walkers 0, 2, 9, 16), and can cap the speed at
  1.5 x the top.

### Band tables (from each `*_spawn`)

| vehicle | reverse | forward bands (lo..hi: accel) | top |
| --- | --- | --- | --- |
| driver | -40..0: 2 | 0..40: 2, 40..60: 2, 60..80: 2, 80..100: 2 | 100 |
| Sideswipe | -180..0: 5 | 0..60: 5, 60..100: 4, 100..180: 3, 180..220: 2 | 220 |
| Thunderfist, Cyclone | -100..0: 4 | four times 0..160: 4 | 160 |
| Skyfall | -180..5: 2 | 0..120: 4, 120..160: 5, 160..220: 2, 220..250: 2 | 250 |
| Ramdozer | -180..0: 6 | 0..80: 6, 80..140: 4, 140..190: 2, 190..250: 2 | 250 |
| Backlash | -120..0: 2 | 0..80: 6, 80..100: 4, 100..140: 2, 140..220: 2 | 220 |
| train, barges | -180..0: 1 | 0..80, 80..140, 140..190, 190..250: all 1 | 250 |
| American Dream | -180..0: 2 | 0..80: 4, 80..160: 5, 160..250: 3, 250..350: 2 | 350 |
| police car | -180..0: 2 | 0..80: 1, 80..160: 2, 160..180: 3, 180..340: 2 | 340 |
| A-Team van | -180..0: 8 | 0..80: 8, 80..160: 8, 160..250: 8, 250..330: 4 | 330 |
| Starski | -180..0: 2 | 0..280: 4, 280..300: 5, 300..310: 3, 310..320: 2 | 320 |
| Ballista | -150..0: 2 | 0..180: 8, 180..200: 4, 200..250: 2, 250..260: 2 | 260 |
| J-Bomb walking / flying | -100..0: 4 / -200..0: 2 | 0..150: 4 / 0..250: 2 | 150 / 250 (`jbomb_set_tuning`, every frame) |

**Measured (Ramdozer, flat ground):** holding A the speed rose 23 a frame
from 0 (accel 6 x 4, minus 1 drag), passed 184 within 12 frames, and settled
at 253..257 (the top band's 250 overshot, then the 6-a-frame decay); holding
B it went to about -180..-203; releasing everything it fell by about 2 a frame.

## Steering and skidding

- `veh_steer(rate, &heading)`: `heading += |stickX| * rate / 80` (left adds),
  a third less on wide-FOV levels. Each type has a `*_steer_rate`: the driver
  0x8C; Ramdozer 75 (22 airborne); Skyfall speed / 2.5 (speed / 11 airborne);
  Thunderfist 120 (50 rolling, 0 stopped); Cyclone 110 (5 charging); Sideswipe,
  Backlash, Ballista, Starski and the cars speed / a per-type float; J-Bomb
  speed / 1.5 walking, a ramp of 20..100 in the air. The train and barges
  don't steer.
  **Measured (Ramdozer):** full stick turned the heading 75 units a frame on
  the ground and 22 while a wheel was airborne.
- `veh_turn_heading(veh, &momentumHeading, grip, &speed, &heading,
  &moveHeading, &skidding, sound)`: the momentum heading turns toward the
  facing by `grip * surface / speed`; while it lags the facing by 0x156 or more
  the vehicle skids (moves along the momentum heading, skid sound). So grip
  falls with speed. Grip constants: driver and train/barges 9000, Sideswipe
  18000, Thunderfist/Cyclone 23000, American Dream 6000, police 12000, A-Team
  20000, Starski 8000, Ballista 16000; Skyfall 15000 (2000 while braking),
  Ramdozer 9000 (2000 with B or Z), Backlash 9000 (3200 with L or R: the power
  slide). The J-Bomb has no skid.
- For 10 frames after a wall block, `veh_blocked_turn_rate` may use a
  per-level rate (`gBlockedTurnRates`).

## Wheel slots, gravity and bounce

`veh_ground_contact` (62740.c) runs each slot as grounded or airborne (see
collision.md for the ground search):

- Grounded: if the ground has fallen away by more than 0x1E below where gravity
  would put the slot, it launches.
- Airborne: `drop = vel * n + gravity * n * n`; it lands when the ground comes
  up to it; a landing harder than `gBounceThreshold` bounces with `|d| /
  gBounceDamping` (`SFX_THUD` for wheeled vehicles).
- The height is the average of slots 1 and 2; the tilt comes from the slot
  height differences.

Each `*_set_physics` sets, every frame, `gGravity` = the level's gravity
(level header word 0x18) times 4 (driver, Sideswipe, Thunderfist, Cyclone,
Skyfall, the cars, Ballista) or 2 (Ramdozer, Backlash, train, barges, carrier),
and the bounce threshold / damping (driver and walkers 40 / 3, Skyfall 60 / 3,
Ramdozer 110 / 4, Backlash 60 / 2, police 150 / 6, train and barges 60 / 4).
On level 12's sand (`SURFACE_DIAMOND_SANDS`) the damping is 8. Riders on a
deck get 3x gravity.

## Control input (`cont_update`, 45BB0.c)

- Reads controller 1; in attract and replay modes the pad comes from the
  recording (`rec_next`); in a level it is recorded (`rec_record`).
- **Control method** (`gControlMethod`): per vehicle type, bit `gCurVehicle` of
  `PlayerInfo.controlModes`, chosen in the pause menu's "CONTROL METHOD"
  window (`cont_show_method_option`): "SPEED ON 3D STICK?" for the wheeled
  vehicles, "360 DEGREE MODE?" for the driver, Thunderfist and Cyclone,
  "AIRBORNE 360' MODE?" for the J-Bomb.
- Method 0, wheeled vehicles: the stick's y is ignored; **A = +80 (accelerate),
  B = -80 (brake, then reverse)**, D-pad or stick x steers. Method 1: stick y
  is an analog throttle as well.
- Method 0 for the driver, Thunderfist, Cyclone and J-Bomb: **360-degree
  steering** (`cont_stick_to_steer`, 46F60.c): the stick points the direction
  to go, relative to the camera heading; the code turns toward it, and pulling
  back at low speed turns around.
- Dead zone 10, range +-80 (`cont_dead_zone`).
- The globals the vehicle code reads: `gPadStickX/Y`, `gHeldA`, `gHeldB`,
  `gHeldZ`, `gHeldL`, `gHeldR`, `gHeldLeft/Right`, `gHeldCLeft/CRight`, ...
- The port's analog-trigger throttle (`PORT_THROTTLE`) is a port addition.

What the buttons do (from the code): A / B throttle; Z brakes, and gets out
(below); L / R are the special action (Sideswipe rams, Skyfall boost, Backlash
slide, Ballista missiles, Thunderfist / Cyclone roll, J-Bomb thrust and stomp
taps, crane action) or the horn (Ramdozer, train, barges, cars); C buttons turn
and zoom the camera.

### Turbo start

On the timed levels the start lights run `hud_draw_turbo_start` (1D990.c):
pressing A at the right moment (not held through the countdown) sets
`gTurboPending`; the vehicle's update then calls `veh_start_turbo` (86ED0.c):
`speed = 1.5 * gCamTiltMax` (the camera's per-vehicle look-ahead cap, so the
turbo speed comes from a camera table), `SFX_TURBO`, motion blur
(`mb_start`), `gTurboActive` for 90 frames. Skyfall, Ramdozer, Backlash,
Ballista and the cars have it; Sideswipe, the walkers, the J-Bomb, the train
and barges don't.

## Getting in and out

**In:** walking into a vehicle. The driver's hit spheres touching another
vehicle's call `veh_request_switch` (`gNextVehicle`; not into a closed train
car's riders). Next frame `veh_apply_switch` (00000.c): refused with
`SFX_ERROR` if something stands half on the vehicle's deck
(`veh_has_partial_rider`); otherwise riders, TNT and blocks are attached,
`veh_enter` runs the type's `*_enter` (camera distance, animations, engine
sound), `gCurVehicle` changes, the vehicle is unlocked for the player
(`vehiclesUnlocked |= 1 << type`), and the driver is hidden. The camera glides
to the new vehicle; there is no cut.

**Out:** Z, only when stopped (`gPlayerSpeed == 0`), not paused, and only on
mission levels (`LEVEL_KIND_MISSION` / `_TIMED`). Then `veh_leave`:

1. `veh_exit_check` (the type's `*_exit_check`): 0 = no (error sound), 1 = try,
   2 = silently no. Most vehicles refuse while airborne; Thunderfist and Cyclone
   while rolling; Sideswipe while its rams are out (it returns 5, treated as a
   refusal); Ballista during a wheelie; the J-Bomb unless walking and idle; the
   crane unless its arm is idle. The train and barges always allow it.
2. `driver_try_exit(dist)` (69BB0.c): refused when `gDriverCanExit` is clear
   (switch planes toggle it, collision.md). The exit direction is a **world
   axis** (+z by default; `gDriverExitWalks` per level and vehicle can pick
   another and a distance multiplier). Every 100 units out to `dist` (a
   per-type table) the driver is test-placed and collided with the world,
   objects and TNT; any hit refuses. US 1.1 sets `gCollExitProbe` during this
   probe (destruction.md: rocking hits).
3. On success the type's `*_leave` (speed 0, engine sounds off), and the driver
   walks out 100 units a frame to the probe's end (`gDriverExitWalk`).

## The driver on foot (69BB0.c)

Walks with the bands above (top 100), 360-degree steering by default, grip
9000, gravity x4; idle animations and footsteps. He enters vehicles by touching
them, touches the end object (scoring.md), and takes comm points and RDUs like
any vehicle. He can't push blocks (`block_update(0)` zeroes the block speed).
`driver_update_anims` tests the high byte of `levelno` (always 0), so a level
check there is always true.

## Special vehicles

### Thunderfist (6C5E0.c) and the Cyclone Suit (8AEE0.c)

Walkers: no wheel table (slot 0 is copied to the others), drag 3, gears by
speed. **The roll / charge:** with speed at least 0x96 (150) and A or B
(method 0) or L / R held, `action` 1: the speed is forced to 0x1BE (446) for
Thunderfist, 0x118 (280) for the Cyclone, while the roll animation plays. A
roll that knocks a part down (`hitPartDown`) halves the speed and sets
`gVehAttacking` (the damage rules' "attack" bonus, destruction.md) for the
recovery; hitting something solid drops to 0x3C. Their top band is the same
four times, so terrain never limits their top speed.

### J-Bomb (7FB50.c)

`action` modes: 0 walk, 1 flying, 2 falling (gravity x4), 3 knocked back, 4
stomp, 5 idle animation. A, L or R thrusts while below `gJbombMaxY`; the lift
bonus (30) fades out between `gJbombLiftFadeY` and `gJbombMaxY` (both from
`LevelInfo`). **The stomp:** B (or tapping L and R) in the air: mode 4, after
the wind-up the speed becomes -0x4B0 (a fast drop), and the landing shakes the
screen (20, 800). In mode 4 and just after it every part it hits takes full
damage (`gVehDamageMode` 2); while flying it takes none (mode 1).
`jbomb_lift_off_water` lifts it off water (it doesn't drown);
`gJbombCeilingAbove` stops it flying into ceilings. The camera rises with it
(camera.md). It ignores the second wall grid (`gCollCellsB`).

### Backlash (71140.c)

L or R lowers the grip from 9000 to 3200, so the momentum heading lags and the
truck slides; while skidding it leaves tyre trails and smoke and blurs. Level
0x17 wraps its position around (`veh_check_wrap`). On pill-maze levels its
object hits go through `veh_maze_object_hits` (a hit fails unless the power
pill is on).

### Ballista (853D0.c)

L or R fires a missile when `gBallistaMissiles` isn't 0 (5-frame cooldown,
alternating sides): `shell_fire(|speed| * 0x140 / 0x104 + 0x280, ..., 0x1A4)`
(destruction.md). It starts with 0 missiles; a missile crate gives 10. A
missile is used up even when all 4 shell slots are busy (the shot is lost).
Full throttle below top speed does a wheelie / jump.

### Sideswipe (6B4A0.c)

L or R fires the side rams (animation channel 5, 10-frame cooldown) when
`gSideswipeRamCharges` isn't 0; a ram crate gives 10. A ram that hits retracts
and shakes the screen (10, 600). It one-hits flimsy objects (destruction.md).

### Skyfall (6E200.c)

`gSkyfallBoostMeter` (0..100) starts full. L or R with at least 4 left: -4 a
frame, speed +40 up to 400, the boost sound and flame; otherwise speed over 250
falls by 25 a frame and the meter refills by 1 a frame. The HUD dial shows the
meter.

### Ramdozer (6E200.c) and the push blocks (4B5E0.c)

Turning in place animates the tracks. A block it touches is pushed at the
player's movement + 10 (capped per vehicle, `gBlockPushSpeedMax`); blocks
fill the level's block slots (the carrier's "box targets", carrier.md).
Touching a block or TNT sets `gVehSlowed`. **Measured:** the first push opened
the hint `YW_HINT_RAMDOZER_PUSH`, which paused the game until A.

### Crane (75490.c)

Doesn't drive (`gPlayerSpeed` is 0): A / B move the arm, D-pad left / right
swing it, L / R start the action animation; its test part checks hits on
blocks and TNT (`crane_check_part_hits`). Leaving the crane also runs
`ramdozer_leave` / `police_leave` when those are loaded (reason unknown;
perhaps vehicles it carries).

### Train (772A0.c) and barges (83910.c)

Throttle only (all band accels 1, `delta` 6, no steering), horn on L / R.
`lvl_init_train_line` (1D990.c) can pin the train's x to a line between two
points as z changes (`gTrainOnLine`). Bumper spheres stop it or bounce it back
by 40 when it runs into something. Both have decks other vehicles ride on
(`veh_attach_riders`, `veh_move_riders`, the `*_ride_attach` /
`*_ride_follow` functions); `gTrainClosed` says the train's sides are up.
Barges float (no water failure) and aren't slowed by blocks. US 1.1 added
train-only out-of-bounds tests on three levels (below).

### Chopper and shuttle

The BCT chopper (0xFE, `chopper_update`, `gChopperMode` 0..5: cruise, approach,
slow, straight, descend to land, climb) flies the intro and drops the driver.
The shuttle (0xFD) follows a spline path (carrier.md).

## Out of bounds and failure

- `veh_respawn_if_out` (62740.c, first thing in `veh_ground_contact`): outside
  the level box (`gBoundsMinX..MaxZ`, level offset 0x4C) the vehicle goes back
  to its spawn record, with a camera snap, an effect and a warning panel.
- Water (surface 100) fails every vehicle except the J-Bomb and the barges;
  surface 0x66 fails any (`gFailTrigger`).
- On target and building levels, falling below the water height or
  `LevelInfo.waterY` fails the level.

## Engine sounds (7F8B0.c)

`veh_start_engine_sound(_, id)` in each `*_enter` (Sideswipe 0x36, Skyfall
0x8C, Ramdozer 0x0B, Backlash 5, train 0x20, barges 0x72, Ballista 0x94,
Starski 0xCE); `veh_set_engine_level` sets the pitch from the speed;
`veh_update_horn(sfx)` plays the horn while L or R is held;
`veh_stop_engine_sounds` on leaving.

## Modding notes

| change | where |
| --- | --- |
| top speed, acceleration | the band table in that vehicle's `*_spawn` (`sVeh1Bands` 6B4A0.c, `sVeh3Bands` / `sVeh4Bands` 6E200.c, inline stores elsewhere; the J-Bomb's in `jbomb_set_tuning`): `bands[4].hi` (VehState +0x92, "band 13" as an s16 index) is the top speed, each band's third value its acceleration (x4 on the ground) |
| braking step | the `delta` argument of the vehicle's `veh_steer_throttle` / `veh_throttle` call |
| turn rate | the `*_steer_rate` functions |
| grip, sliding | the grip argument of `veh_turn_heading`, or `gSkyfallGrip` / `gRamdozerGrip` / `gBacklashGrip` in the `*_set_physics`; the skid threshold 0x156 in `veh_turn_heading` |
| gravity, bounce | the `*_set_physics` functions; the level's gravity at header word 0x18 |
| ram charges, missiles | the start value in `sideswipe_spawn` / `ballista_spawn`; `+= 10` in `crate_update` (479D0.c) |
| Skyfall boost | cost 4, +40, cap 400, decay to 250, refill 1 in `skyfall_update_anims` |
| roll / charge | trigger speed 0x96 and roll speed 0x1BE / 0x118 in the Thunderfist and Cyclone action functions |
| J-Bomb | the lift bonus 30, stomp speed -0x4B0, timers in `jbomb_update_modes`; altitudes in `LevelInfo` |
| turbo | `1.5 * gCamTiltMax` in `veh_start_turbo`; the caps in `cam_update`'s type switch |
| exit | the Z condition in `hd_game_frame`; `gDriverExitWalks`; the 100-unit steps in `driver_try_exit` |

Most vehicle functions are hand-written asm; changes go in their
`NON_MATCHING` C, which the port builds. The matching build uses the asm.

## Versions

- `veh_respawn_if_out`: US 1.1 also respawns the train on level 0 when x < 1000
  (level units), level 18 when z < 900, level 13 when z < 1100.
- `driver_try_exit`: US 1.1 sets `gCollExitProbe` during the probe.
- The front end's vehicle select remembers the level's vehicle in US 1.1.

See [../versions.md](../versions.md).

## Open questions

- The per-type exit distance table is read as `((s32 *) &gUseEeprom)[type]`
  (0x802E8BF8): its real name and extent. Suggested name: `gVehExitDist`.
- (`gModeGlobeSeqU64Spawn`, the attract spawn filter's mode, is answered in
  [levels](levels.md): it is `MODE_GLOBE_SEQ` and the filter never runs.)
  `D_803F8B77` (set to 0x32 by
  `ballista_spawn`), `D_803FC999` (cleared by `cyclone_spawn`), and the
  position halves `D_803F874C`, `D_803EFACC`, `D_803EEB3C`, `D_803F8F2C`,
  `D_803F8B4C`, `D_803F7C00` (the y / z words of vehicle position blocks).
- `VehState.unkA4`; `VehRecord`'s 0x18..0x2F and 0x3C..0x53 (not read by the matched or NON_MATCHING C).
- The values of the data floats (steer divisors, skid pitch, engine pitch
  steps).
- `gLevelGravity` is an f32 loaded from an s32 header word.
- Where the front end sets `gDiggerVehicle` for normal play.
- Why leaving the crane also leaves the Ramdozer and the police car.

## Notable findings

- Type 12 has no code; a level record of type 12 would stop the spawn loop.
- `veh_exit_check` has no `return` in the matching build (it returns the
  handler's leftover v0).
- The Skyfall's reverse band is `{-180, 5, 2}` (hi 5, not 0).
- The Ballista loses a missile when no shell slot is free.
- The turbo speed comes from a camera table.
- Never called: `veh_throttle_lr` (L / R accelerate), `barge_steer_rate`,
  `thunderfist_reground`, `cyclone_reground`, the Ballista's reground, the
  `*_save_state` functions, two stick helpers in 46F60.c.
- Debug traps (`syscall`) for impossible modes in several vehicles.

## Measurements

bc_headless, level 0 (the Ramdozer), reached through
`port/data/levels_input.txt` and the level poke; inputs: A held 300 reads; B
held, then steering with B and with A; RAM read every 2 or 4 frames
(`gPlayerSpeed`, `gPlayerX/Y/Z`, `gPlayerHeading`, `gPlayerMoveHeading`,
`gContButtons`, `gPadStickX`, `gPaused`, `currentYoshiWindow`).
