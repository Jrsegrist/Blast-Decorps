# Scoring, level completion and medals

How a level ends, what is counted (buildings, people, RDUs, money, comm
points, academy markers), how a result is graded into a medal, the medal
points ("units") and rank titles, best times, and the debug cheats.

Everything here is **from the code** unless it says **measured**. Level
numbers are `levelno` (0-59); this doc names no level contents (ROM data).

Related docs: [destruction.md](destruction.md) (the tally and money come from
the object system), [carrier.md](carrier.md) (mission success and failure).
The save formats are in the engine docs ([frontend.md](frontend.md)).

## Where the code is

| file | Rare's name | what |
| --- | --- | --- |
| `src.us.v11/hd_code/409D0.c` | stats_perm.c | `stats_level_start`, `stats_print_status`, `stats_save_status`, `stats_award_bonuses`, `stats_finish_level`, `stats_finish_bonus_level`, the level flags, the clock conversions |
| `src.us.v11/hd_front_end/07800.c` | stats.c | grading: `fe_stats_grade_level`, `fe_stats_grade_time` |
| `src.us.v11/hd_code/1D990.c` | | objectives: `hud_init_objective`, `hud_update_objective`, `hud_check_*`, `hud_update_race`, the HUD |
| `src.us.v11/hd_code/00000.c` | hd.c | the frame loop, the request latch, the results mode setup, the debug cheats |
| `src.us.v11/hd_code/17E10.c` | | `hud_level_complete`, `hud_mission_failed` |
| `src.us.v11/hd_code/77E20.c` | | `obj_tally`, `obj_check_level_end`, path buildings |
| `src.us.v11/hd_code/26570.c` | | RDUs (`rdu_*`), `parse_cmdline` |
| `src.us.v11/hd_code/8A2E0.c` | | communication points (`commpt_*`) |
| `src.us.v11/hd_code/41930.c`, `52D70.c`, `53220.c` | academy.c | the game state and the academy markers |
| `src.us.v11/hd_code/7FB50.c` | | mission status bitstreams (`record_status`, `apply_status`, `create_status`) |
| `src.us.v11/hd_front_end/01C40.c` | | level-kind retyping, regrading |
| `src.us.v11/hd_front_end/10850.c` | bestTimes.c | the best-times screen |
| `src.us.v11/hd_code/50670.c` | | the race ghost |
| `include/game/types.h` | | `Score`, `PlayerInfo`, `LevelInfo` |
| `include/game/game.h` | | `LEVEL_KIND_*`, `MODE_*`, `YW_*` |

## Data structures

### Score (16 bytes): `nss` (this run), `gScoreOld`, `gScoreRes`, `gScoreRs2`

The field names are Rare's (they appear in debug prints: `ip=%8d : tc=%5d :
bd=%2d : cr=%2d : rt=%3d : coin=%1d : bdn=%1d`). Several have alias globals
at the same address.

| off | field | alias in `nss` | meaning |
| --- | --- | --- | --- |
| 0x0 | `ip` | | money: the total `value` of destroyed objects |
| 0x4 | `tc` | `gScoreTime` | time in tenths of a second (max 59999; 0xFFFF = failed / none) |
| 0x8 | `bd` | `gScoreBuildings` | buildings destroyed |
| 0x9 | `cr` | `gScorePeople` | people released |
| 0xA | `coin` | `gScoreGrade` | the medal (as `PlayerInfo.medal`) |
| 0xB | `bdn` | `gScoreVehicle` | the vehicle of the result (indexes `gVehicleTimeSlot`) |
| 0xC | `rt` | `gScoreRdus` | RDUs lit |

The totals: `gBuildingCount`, `gPeopleTotal`, `gRduCount`.

- `nss` is preloaded at level start with the saved result (`stats_level_start`).
- At `MODE_LEVEL_START`, `nss` is copied to `gScoreOld` and `gScoreRes` (the
  baseline the results compare against). `gScoreRs2` keeps the graded result.

### PlayerInfo (0x100 bytes per save slot, `players[]`)

The fields scoring uses: `units` (medal points, max 360), `title` (rank title,
`units / 12`, one of 31 `gRankTitles`), `vehiclesUnlocked` (a bit per vehicle
type), `money`, `medal[60]`, `commPts[60]` (bits of comm points taken),
`academy` (6 marker bits), `gameState` (0-13), `bestVehicle[60]`.

`medal[l]`: 0 locked, 1 bronze, 2 silver, 3 gold, 4 platinum, 5 done without
a medal, 8 open (not done). "Done" everywhere is `medal > 0 && medal < 6`.

### LevelInfo (`gLevelInfo[60]`, 0x44 bytes, hd_code data)

`kind` (below), `gameState`, the race geometry (`raceArea`, `raceStartBox`,
`raceQuads`), `target` (laps, targets, $ or RDUs to get), `waterY`,
`vehicles` (allowed types), and `medalTimes[4]` (platinum, gold, silver,
bronze, in tenths of a second; **[3] is also the time limit**).

## Level kinds and objectives

| kind | value | objective (`hud_update_objective`) |
| --- | --- | --- |
| `LEVEL_KIND_MISSION` | 1 | the main missions: no timer; the carrier's arrival or the end object (carrier.md) |
| `LEVEL_KIND_RACE` | 2 | `hud_update_race`: `target` laps |
| `LEVEL_KIND_TARGETS` | 4 | `hud_check_targets`: `gScoreBuildings >= target` |
| `LEVEL_KIND_DAMAGE` | 8 | `hud_check_damage`: `nss.ip >= target` ("CAUSE $%d DAMAGE") |
| `LEVEL_KIND_RDUS` | 0x10 | `hud_check_rdus`: `gScoreRdus >= target` |
| `LEVEL_KIND_BUILDINGS` | 0x20 | `hud_check_path`: every flagged building down |
| `LEVEL_KIND_PILL_MAZE` | 0x40 | RDUs, with a ghost and a power pill |
| `LEVEL_KIND_MISSION_TIMED` | 0x80 | a main mission played against the clock once `gameState >= 11` ("CLEAR CARRIER PATH": path buildings down) |

- `fe_player_update_level_kinds` (01C40.c) retypes every mission level (`kind &
  0x81`) to 0x80 or back to 1 whenever the world screen opens, so `kind` in
  `gLevelInfo` changes at run time.
- Every kind except `MISSION` is timed: `gTimeLeft = medalTimes[3] - elapsed`
  each frame; at 0 the level fails (`cmo_hit_request`). Under 10 seconds a tick
  sound plays each second and the timer flashes. `hud_mission_intro` shows the
  objective and limit, then a 3-2-1-go countdown into `MODE_LEVEL`.
- Falling into water (or below `waterY`) fails target and building levels.
- **Races:** the track area is split into four quadrants; a lap is the four
  `raceQuads` crossings in order, then the start box. Lap 1 is timed from the
  level start, not from the start box. "%d LAPS LEFT!" and the music speeds up
  (tempo x 0.95) after each lap.
- **Pill maze:** eating the power pill gives 400 frames (`gPowerPillTimer`)
  during which the ghost flees; touching the ghost without it fails the level.

### The clock

`gSchedRetraceCount` counts retraces while the game isn't paused. Tenths =
retraces / 6 (`stats_frames_to_tenths`, capped at 59999), so times are real
60 Hz time, independent of the frame rate. `gLevelStartTime` is set when
`MODE_LEVEL` starts. **Measured:** while a hint window paused the game the
counter stood still.

## Level completion

```
objective met / carrier arrived / end object touched   -> gLevelDoneRequest
time out / carrier hit / fail trigger / drowned        -> cmo_hit_request
end of hd_game_frame:  gLevelDoneRequest -> gLevelDone, else cmo_hit_request -> gCarrierHit
```

- `obj_check_level_end` (77E20.c): the driver **on foot** touching the end
  object (`OBJ_KIND_END`) sets `gEndObjectTouched`; on carrier/shuttle levels
  it also needs the path buildings, box targets and triangle targets ahead
  cleared. With no carrier it goes to `MODE_LEVEL_CLEAR` (clear tune, then
  results).
- Timed kinds end in `MODE_TIMED_END`; carrier levels in `MODE_CARRIER_END`.
- `hud_level_complete` (17E10.c): on the first frame of `gLevelDone`,
  `obj_tally`, then `YW_LEVEL_DONE` (or the Moon / shuttle-ending windows on
  levels 0x28 and 0x32) and the done tune, then a fade to `MODE_RESULTS`.
- Failure: `hud_mission_failed`, then `MODE_RESULTS` if the level was already
  done, else `MODE_REPLAY` and the level restarts (carrier.md has the measured
  sequence).

## What is counted

- **Buildings and money:** `obj_tally` (destruction.md). A destroyed object
  adds its `value` to `gLevelMoney` at once (the "$N" popup); the tally puts
  the total in `nss.ip`. Flimsy objects (toughness 1) count for money but not
  as buildings. At level start `gLevelMoney` is the saved amount.
- **People:** `gPeopleTotal` is summed from the object placements;
  `people_spawn` adds to `gScorePeople` **when a building releases them**, not
  when they reach safety.
- **RDUs** (26570.c): `rdu_update` lights an RDU when the player is within
  `gRduPickupRadius[vehicle]` of it (`gScoreRdus++`, the lit order recorded
  for the replay camera). After the 4th, a hint.
- **Bonuses** (`stats_award_bonuses`, missions only): every RDU
  (`YW_BONUS_RDUS`), every person (`YW_BONUS_PEOPLE`), every building
  (`YW_BONUS_BUILDINGS`), all three (`YW_BONUS_ALL`). They are a window and a
  jingle; nothing in the code adds money or points for them. A bonus already
  met on the very first frame is marked silently.
- **Communication points** (8A2E0.c): driving within 0x4C of one takes it
  (a burst of debris, a hint) and sets a bit in `PlayerInfo.commPts[levelno]`
  (`stats_set_level_flag`). Each bit opens the level in
  `GlobeLevel.commRoutes[bit]` on the globe (`fe_open_reachable_levels`). On a
  level already done that isn't a mission, taking a comm point ends the visit
  and returns to the world (`stats_finish_bonus_level`). The bit for entry k is
  `gCommPtCount - 1 - k` (a countdown quirk kept from the asm).
- **Academy markers** (52D70.c): six hidden markers (`gAcademyMarkers`), found
  within 0x50 while `gameState < 5`; all six (`academy == 0x3F`) are needed to
  leave game state 4.
- **Mission targets:** when no target is left on a mission,
  `YW_TARGETS_CLEARED` and `gTargetsCleared`.

## Grading

`MODE_RESULTS` setup (`hd_main_thread`) computes the time (missions:
`gTimeInLevel` added to the stored time; timed kinds: `medalTimes[3] -
gTimeLeft`; failed: 0xFFFF), then `fe_stats_grade_level` (07800.c).

### Missions

`stats_print_status` gives the average completion: buildings %, people % and
RDUs % (a total of 0 counts as 100 %), averaged over all three with a
Controller Pak, but **over buildings and RDUs only with EEPROM saves**
(`gUseEeprom`). The medal:

| average | medal |
| --- | --- |
| 100 % | gold (3) |
| >= 90 % | silver (2) |
| >= 70 % | bronze (1) |
| less | done, no medal (5) |

A first clear also gives 3 units ("Units up 3"). The money is saved
(`PlayerInfo.money`). The mission's destruction is saved as a bitstream
(`record_status`: one bit per object part at 100 %, one per lit RDU), and a
done mission replayed from the globe starts with it restored (`apply_status`).

### Timed levels

`fe_stats_grade_time(time, level, gameState)`:

```
time <= medalTimes[0] and gameState >= 12   platinum (4)
time <= medalTimes[1]                       gold (3)
time <= medalTimes[2]                       silver (2)
time <= medalTimes[3]                       bronze (1)
otherwise                                   5
```

A result only replaces the stored one when it is at least as fast
(`nss.tc <= gScoreOld.tc`); otherwise the old time and medal stay ("YOUR BEST
STAYS"). Per-vehicle bests are kept separately (`gBestTimes[player][slot]`,
`gVehicleTimeSlot[vehicle]`).

### Units and rank titles

On commit: `units += newMedal % 5 - oldMedal % 5` while under 360 (so "done"
counts 0, and an improved medal adds only the difference); 354 jumps straight
to 360; a new title when `units / 12` passes `title` (then `MODE_PROMOTION`).
Platinum adds one unit in the regrade below.

### Game state and regrading (`academy_advance_state`, 41930.c)

Checked each time the world screen opens:

| state | advances when |
| --- | --- |
| 1-3 | every mission level with that `LevelInfo.gameState` is done |
| 4 | all six academy markers found |
| 5, 6, 7 | levels 0x31, 0x32, 0x28 done |
| 8 | units >= 222 |
| 9 | units >= 234: `fe_player_regrade_missions` regrades every done mission by its first-clear time against `medalTimes` (no platinum yet) |
| 10 | the "missions regraded" notice (`MODE_REGRADE_NOTICE`) |
| 11 | units >= 297: `fe_player_regrade_platinum` regrades every done level with platinum allowed (+1 unit each) |
| 12 | units >= 354: units = 360, title 30 ("YOU CAN STOP NOW") |

From state 11 the missions are played as `MISSION_TIMED`.

## Best times and the ghost

- `gBestTimes[player][16]`: the current level's times per vehicle slot, loaded
  per level. `PlayerInfo.bestVehicle[level]` picks the slot of the best.
- EEPROM stores each time with a check word `time ^ 0x55AA`; Pak files and the
  player records carry CRCs ([frontend.md](frontend.md)). These are the only integrity
  checks found.
- The best-times screen (10850.c) lists each vehicle slot the player has
  unlocked, four rows per slot.
- Races record a ghost (`ghost_record`, a 0x20000-byte buffer allocated only
  on race levels); `ghost_keep_if_best` keeps it when the time is at least as
  fast.

## Bonus levels

All kinds except the missions: they are entered through the vehicle select
(`MODE_VEHICLE_SELECT`); a vehicle is offered when it is both unlocked
(`vehiclesUnlocked`, set by entering that vehicle anywhere) and allowed on the
level (`LevelInfo.vehicles`). Races, target, damage, building, RDU and pill-maze
levels all grade by time; the non-mission kinds get a turbo start (release A on
the right light).

## Debug cheats

`parse_cmdline` (26570.c) reads the debug command line (ROM 0xFFB000 on a
development cart; the port's `--cmdline`): `-c` sets `gDebugCheats`, `-C` also
`gDebugStepGameState` (state advances without the unit thresholds); `-d -v -s
-j -m -l` set other debug flags.

With `gDebugCheats` in a level (`hd_game_frame`):

- a new press of C-right (`R_CBUTTONS`) completes the level;
- with C-up (`U_CBUTTONS`) held it fails it instead;
- with Z held it is a perfect result: `gCheatAllDestroyed` (the next
  `obj_tally` counts every building), all RDUs and people, and comm-point bits
  0 and 1.

Also: Z gives the academy marker, A skips the promotion animation.

Nothing caches `gDebugCheats`: parse_cmdline sets it once at boot and the
readers above check it every time, so the port's bc.exe switches it in a
running game (`[game] debug_cheats`, F3; port/README.md "Debug cheats").

## Per-frame order (`hd_game_frame`)

1. The debug cheat check.
2. Vehicle and carrier updates (hits, arrival).
3. Unpaused: path buildings, `obj_check_level_end`, `rdu_update`, the power
   pill, `academy_check_marker`, money popups.
4. Effects and delayed damage (money arrives here when objects finish).
5. Non-mission kinds: `hud_update_objective` (timer and objective).
6. `commpt_update`.
7. The frame display list: `stats_award_bonuses`, `hud_level_complete` /
   `hud_mission_failed`.
8. The request latch.

## Modding notes

- **Time limit and medal thresholds:** `gLevelInfo[level].medalTimes[0..3]`
  (tenths; [3] is the limit).
- **Objective amount:** `gLevelInfo[level].target`.
- **Mission medal thresholds:** 100 / 90 / 70 in `fe_stats_grade_level`; the
  averaging (with or without people) in `stats_print_status`.
- **Platinum gate:** `gameState >= 12` in `fe_stats_grade_time`.
- **Units and states:** the thresholds in `academy_advance_state`; `units <
  360` and `== 354` in `fe_stats_grade_level`; titles are `units / 12`.
- **Money per object:** `ObjectModel.value`.
- **Pickup radii:** `gRduPickupRadius[vehicle]`; comm point 0x4C; academy
  marker 0x50; power pill 40 and 400 frames.
- Don't rely on edits to `gLevelInfo[].kind` for missions: it is rewritten
  when the world opens.

## Versions

US 1.1's best-times menu counts its own rows (`gBestItemCount`); US 1.0 used
the menu's item count, which on a timed mission includes a blank last item, so
the highlight also coloured that item. The vehicle select in 1.1 restores the
level's vehicle on B (`gDiggerPrevVehicle`), which feeds `nss.bdn`. See
[../versions.md](../versions.md).

## Open questions

- What Rare's abbreviations stand for (`ip`, `cr`, `bdn`, `rt`, `coin`); the
  meanings above come from use.
- Whether the mission time is meant to accumulate over replays of a done
  mission (`nss.tc = tenths(gTimeInLevel) + nss.tc`).
- How the first-clear +3 units and the later mission regrade combine (the code
  adds both).
- Why EEPROM saves leave people out of the mission percentage.
- `LevelInfo.unk20`.
- "cmo" (`cmo_hit_request`, "cmo destroy %d stars") is the carrier's name in
  the code; its expansion is unknown.

## Notable findings

- People count when released, not when rescued.
- Bonuses carry no reward in the code.
- `stats_set_level_flag` relies on MIPS shift masking (`1 << (bit + 0x1F)`
  sets bit `bit - 1`); the port spells it `PORT_SHAMT`.
- `fe_player_regrade_missions`'s comment says "race levels" but it regrades
  missions.
- Never-entered modes: `MODE_PRE_PROMOTION`, `MODE_UNUSED_33`,
  `MODE_UNUSED_WINDOW_13`, `MODE_UNUSED_41`, `MODE_UNUSED_52`,
  `MODE_BEST_TIMES_UNUSED`.
- Levels 0x31, 0x2F and 0x26 are treated as non-graded ("dummy") levels in the
  grading asserts, regrades and save slots.
- `gRaceStartTime` is set but never read; `fx_init_power_pill` checks only the
  first spawn entry.
