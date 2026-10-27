Blast Decorps - Blast Corps for Windows (build @VERSION@)
========================================================

This is Blast Corps (Rare, 1997) running natively on Windows: the game's code was
decompiled back into C and compiled for the PC, with RT64 drawing the graphics and
the game's own sound microcode reproduced on the CPU. It is NOT an emulator, but it
plays the original game exactly: the game logic runs on the N64's 60 Hz clock,
frame for frame as on the console.

No game data is included. You need your own copy of the game:

    a ROM image of Blast Corps (USA) (Rev 1), also called v1.1,
    as a .z64, .v64 or .n64 file
    (SHA-1 of the .z64: 483f7161aea39de8b45c9fbc70a2c3883c4dea8c)

The first USA release (v1.0), the Japanese Blastdozer and the European version are
not supported (yet); the game tells you if you pick one of those.


Running it
----------
Double-click bc.exe. The first time, a file dialog asks for your ROM; the game
checks it and remembers where it is (in bc.ini). That's all.

Needs: Windows 10 or 11, a graphics card with Direct3D 12 or Vulkan support, and
the files of this folder kept together (bc.exe, SDL2.dll, dxcompiler.dll, dxil.dll;
bc_packtool.exe is only for making texture packs).
The folder must be writable (settings, saves and the log are kept here).


Controls (default; change them in the settings menu or bc.ini)
------------------------------------------
  N64            Keyboard            Game controller (XInput/Xbox and others)
  Analog stick   Arrow keys          Left stick
  A              X                   A
  B              C                   B or X
  Z              Z or Space          Left or right trigger (with the analog throttle
                                     on: the left trigger; the right one drives)
  Start          Enter               Start
  L / R          A / S               Shoulder buttons
  C buttons      I J K L             Right stick (the orbit camera's, if it is on)
  D-pad          T F G H             D-pad

  F1            settings menu (also Back+Start on a game controller)
  F2            frame-rate counter: on, detailed, off
  F3            the game's debug cheats on/off (see "Debug cheats")
  Alt+Enter, F11  fullscreen on/off
  M             sound off/on;   - and =  volume down/up
  Esc           quit (in the settings menu: back to the game)

The keyboard stick is digital (full tilt); a game controller's stick is analog.


Settings menu
-------------
F1 (or Back+Start on a game controller) opens the settings menu over the game, which
waits while it is open. It works with the mouse, the keyboard (arrows, Enter) or a
game controller (D-pad, A, B). Changes apply at once and are written to bc.ini when
the menu closes (Esc, F1 or "Back to the game"). The graphics API changes on the
next start.


Settings: bc.ini
----------------
bc.ini is created next to bc.exe on the first start, with every setting explained in
it. The settings menu writes it too; you can also edit it with Notepad while the
game is closed. It holds:
  [game]       rom (the ROM's path), saves (the saves folder), controller_pak (0/1),
               debug_cheats (0, the default, or 1) and cheats_save (separate, the
               default, or normal): see "Debug cheats",
               settings_version (leave it: see "Changed defaults" below)
  [video]      api (d3d12 or vulkan), scale (window size 320x240 times this),
               fullscreen, vsync, present_pacing (timer, the default, vsync, vrr
               or auto: see "Frame rate and smoothness"); optional enhancements (off by
               default):
               resolution (internal resolution: window, 1 = the N64's, 2, 3, 4 ...),
               msaa (0, 2, 4, 8), filtering (aa, linear, nearest),
               aspect (original = 4:3, 16:9, 16:10, 21:9, window: widescreen),
               hud_position (centered, the default, or edges: in widescreen the
               HUD's left and right parts at the screen's edges),
               framerate (original = the game's own 20/30 pictures a second,
               display = the display's refresh rate, or a number like 60, 120, 144:
               smoother motion from in-between pictures), interpolation (tagged,
               the default, or auto), fps_counter (off, on, detailed) and
               fps_counter_corner (top-left, top-right, bottom-left, bottom-right),
               texture_pack (an RT64 texture pack folder or .rtz),
               texture_pack_enabled (0/1) and texture_pack_reload (0/1: reload it
               when it changes), texture_dump (a folder) and texture_dump_enabled
               (0/1): see "Texture packs"
  [audio]      volume, mute
  [camera]     mode (original, the default, or orbit: the free camera, below),
               sensitivity_x/y, invert_x/y, follow (1, the default: the view turns
               with your vehicle; 0: fixed in the world), recentre (0, the default,
               or 1) and recentre_delay (seconds), relative_steering (0/1),
               recentre_on_foot, collision (off, the default, smooth or strict),
               zoom_min/max, tilt_min/max, mouse (0/1)
  [mods]       folder (mods, the default) and enabled (the mods to use, by file
               name, separated by commas): see "Editor and mods"
  [keyboard]   one line per N64 button: SDL key names, several separated by commas;
               also quit, menu, fps_counter, debug_cheats and the orbit camera's
               camera_left/right/up/down, camera_zoom_in/out, camera_reset,
               camera_zoom_hold
  [controller] the same for a game controller, plus which stick is the analog stick
               (stick), its dead zone (deadzone) and sensitivity, which controller
               plays (pad = 1, 2 ... or any), the buttons held together that open
               the settings menu (menu), switch the counter (fps_counter) or the
               cheats (debug_cheats), and the analog throttle: analog_throttle (0,
               the default, or 1), throttle (the trigger: righttrigger),
               throttle_deadzone, throttle_curve
Delete bc.ini to get the defaults back. If a line isn't understood, the game says so
when it starts and uses the default for it.

Changed defaults: when a new version changes a default, a bc.ini from an older one
gets the new default for that setting if it still has the old default, once (the
log says so, and the file is rewritten; its comments stay as they were). A value you
chose yourself stays. This version: recentre 1 -> 0 and collision smooth -> off (the
orbit camera stays where you put it and buildings don't move it). Set them back in
the menu or bc.ini if you liked the old behaviour; they stay that way.

Analog throttle (analog_throttle = 1, or Controls > "Drive with the trigger's
pressure" in the settings menu): the right trigger drives your vehicle by how far you
press it. The game's vehicles accelerate the same way whatever the pressure, up to a
top speed, and the pressure sets that top speed: half way holds about half the top
speed, fully pressed is exactly the A button (as the game's own "speed on 3D stick"
control method does with the stick). The trigger then presses no other N64 button: with
the default bindings Z (brake, get out) is the left trigger only. The A button and the
keyboard still drive at full throttle; B brakes and reverses as always. On foot, and
for Thunderfist, the J-Bomb, the Cyclone Suit and the Crane (they don't use a
throttle), the trigger is the A button. The pressure goes to the game with the
controller's state that the game records, so the replay after a level should show
your partial throttle too (turning the option on or off in the middle of a level
can make that level's replay differ).

The window can be resized freely; the picture keeps the N64's 4:3 shape unless
widescreen is on (aspect): then the 3D world fills the wider picture, nothing is
stretched, and the HUD, menus and other 2D pictures stay 4:3 in the middle (with
hud_position = edges the HUD's left and right parts, the radar, panel, money,
timers and icons, move out to the screen's edges; messages and menus stay in the
middle). With
widescreen the game also treats the wider view as visible (which parts of the level
it draws), so it can play out slightly differently from the N64.

A higher frame rate (framerate) only changes what you see: the game still runs at
its own pace, and the renderer adds pictures between the game's own ones (the camera
and moving vehicles and objects glide instead of stepping). It is capped at the
display's refresh rate (on a 60 Hz display, 60). Cuts (a new screen, the mission
intro, a respawn) are shown without blending. It also counts a slightly wider view
as visible, as widescreen does. With interpolation = tagged (the default) each
vehicle, building part, piece of debris, smoke puff and person is matched with
itself from one game frame to the next, new ones appear without smearing, and the
HUD is never blended (but for the radar's sweep and the arrows round the next target,
which glide with the view); the globe's route lines turn with the globe (older
versions let them flicker and jump about while it turned); auto lets the renderer guess
the pairs instead (to compare), but for those route lines and the arrows round the next
target, which glide and turn as with tagged there too (older versions let the lines
jump and the arrows step behind their target in fast camera turns).

The orbit camera (camera mode = orbit, in the settings menu's Camera tab, or
bc.exe --camera orbit) lets you turn the view around your vehicle, or the driver on
foot, at any angle, tilt it and zoom it, while you play: the right stick (or keypad
4 6 8 2, + and -; or hold a mouse button and move the mouse, the wheel zooms); hold Y
and push the right stick up or down to zoom in or out; the right stick's click (keypad
5, the middle mouse button) resets it. (Older versions zoomed out with a click of the
left stick, easy to do by accident while steering hard; a bc.ini with those old
default buttons is changed to the new ones when the game starts.) The view and the
zoom stay where you put them: getting in and out, pausing, the map view and messages
keep them, until you reset them or a new level starts (the zoom is kept even then).
The angle is kept relative to the game's own camera, which follows your vehicle
(follow = 1): set it to the side and it stays at the side as you drive and turn, like
a chase camera; follow = 0 keeps it fixed in the world instead (driving a circle, it
swings round you). recentre = 1 brings back the old behaviour of easing back behind
you a moment after you let go. A click of the right stick while it is pushed far
over (turning or zooming) doesn't reset the view (taken for an accident); click it
with the stick centred. It only changes what you see, with one exception: the driver on foot,
the J-Bomb, Thunderfist and the Cyclone Suit are steered by the game relative to its
own camera, so with the view turned round "up" on the stick used to go towards you.
With relative_steering = 1 (the default) the stick is turned to match the orbit's view
(up always goes away from you) before the game reads it; the vehicles steer as always,
and with the orbit off (or not turned) the game gets exactly what you press. While it
is on, the right stick turns the camera instead of being the C buttons (C left/right,
the game's own 45-degree camera turns, stay on J and L); in menus the right stick is
the C buttons as before. Buildings in the way (collision = off, the default): the
camera never moves for them, so it stays at your angle and zoom; you may see through
a building, or lose sight of yourself behind one, for a moment. It is still kept
above the ground, rising smoothly as the ground rises. collision = smooth: the view
rises over a building that stays between the camera and you, or moves in front of it,
calmly, and goes back a moment after the view is clear; strict moves at once every
time. The sky moves picture by picture with the orbit's view, and at a frame rate
above the original (framerate 60, 120, display ...) the orbit camera turns smoothly
from picture to picture, like the rest of the picture. Smoke and explosions turn to
face the orbit camera, and the four bouncing arrows that mark the next building to
demolish stay round that building in the orbit's view (older versions left them where
the game's own camera saw it, and smoke away from the middle of the picture showed as
thin streaks). People still face the game's own camera, the radar keeps the game's
heading, and the screen doesn't shake in the orbit view.

The frame-rate counter (F2, or fps_counter in the settings menu) shows in a corner
how many pictures a second reach the display and how many frames a second the game
itself makes (30 in play); F2 again adds frame times and a graph, a third time hides
it.

Command-line options (for shortcuts or a .bat file) override bc.ini, for example:
  bc.exe "D:\ROMs\Blast Corps (USA) (Rev 1).z64"   use this ROM (not remembered)
  bc.exe --api vulkan          bc.exe --fullscreen      bc.exe --no-vsync
  bc.exe --mute                bc.exe --volume 50       bc.exe --scale 4
  bc.exe --saves D:\BCsaves    bc.exe --config other.ini   bc.exe --no-pak
  bc.exe --set audio.volume=50   (any bc.ini setting as section.key=value, this run only)
  bc.exe --resolution 4        bc.exe --msaa 4          bc.exe --filtering nearest
  bc.exe --aspect 16:9         bc.exe --texture-pack D:\packs\mypack
  bc.exe --aspect 21:9 --hud-position edges
  bc.exe --framerate 60        bc.exe --framerate display
  bc.exe --interpolation auto  bc.exe --fps-counter on
  bc.exe --camera orbit        bc.exe --present-pacing vsync
  bc.exe --dump-textures texture_dump
  bc.exe --set game.debug_cheats=1   (the debug cheats, this run only)
  bc.exe --cmdline -c          (the game's own debug command line; -c: the cheats)
  bc.exe --viewer 0            (the level viewer on level 0: see Viewer below)
  bc.exe --mod mods\my_mod.bcmod   (a level mod for this run: see Editor and mods)
  bc.exe --no-mods             (start without the mods enabled in bc.ini)


Debug cheats
------------
Rare's development cartridges had a debug command line; its -c switch turns on the
game's own cheats. Turn them on with debug_cheats = 1 in bc.ini, in the settings
menu (F1 > Game > Debug cheats), or with F3 while you play (F3 lasts for this run;
bind another key or controller buttons in Controls). While they are on, a small
"DEBUG CHEATS" marker shows in a corner, and switching them shows a short note.
In a level:
  C-right           completes the level
  C-up + C-right    fails it
  Z + C-right       a perfect finish (every building, RDU and person counted)
Also: A skips the promotion animation, and Z in the academy gives the marker.

Your save is protected: a cheated finish would be saved as a real one, so once the
cheats were on the game saves to saves\blastcorps.cheats.eep (and
blastcorps.cheats.mpk with a Controller Pak) until it closes, and leaves your real
save alone. That file starts as a copy of your save. With the cheats on from the
start, the game continues the cheats save of last time. Turned on during a game,
the switch happens at once in a level, or in the menus when the next level starts
(the menus may be saving just then; the cheats only change anything in a level),
and the cheats save starts again from your save as it is at that moment.
Turning the cheats off doesn't switch back: the game's memory holds the cheated
progress, so it keeps saving to the cheats file until you close it. Next time you
start without the cheats, you play from your real save. To keep cheated progress in
your real save instead, set cheats_save = normal (F1 > Game > "Saves with the
cheats").


Viewer
------
The level viewer holds the game still and lets you fly around a level, look at what
it is made of and see each vehicle or building on its own. Open it with
  bc.exe --viewer 3            (starts on level 3; any level 0-59)
or while you play: F1 > Viewer > "Open the viewer here" (the game stops where it is
and goes on from there when you leave). The Viewer tab can also load any level;
leaving the viewer after loading one restarts the game (that game wasn't reached by
playing, so it can't go on or save). The viewer never touches your saves (its editor
writes mod files only: see Editor and mods).

  WASD                move (Q / E: down / up; Shift: fast, Ctrl: slow)
  right mouse button  hold and move the mouse to look around (the wheel: speed)
  left click          select a marker: its record shows in the Inspector
  Home or P           jump to the player's start      O   an overview from above
  F                   look at the selection           M   model view of it
  Tab or F1           show / hide the panels          Esc leave (model view: back)
  F12                 screenshot without the overlays (Shift+F12: with them),
                      saved in the screenshots folder next to bc.exe
  Controller          left stick moves, right stick looks, triggers down / up,
                      shoulders change the speed, A selects, B goes back,
                      Y the player's start, L3 shows / hides the panels,
                      Back leaves
  Start               the controller works the panels (Start or Back again: back to
                      the camera): D-pad or stick moves, A presses, B backs out of a
                      list, hold X and press LB / RB for another panel (always in the
                      order Level viewer, Inspector, Changes, Terrain). Everything in
                      the editor works this way, with no keyboard: "Keys..." next to
                      the mod's name opens an on-screen keyboard, and pictures in a
                      folder called textures next to bc.exe are listed for the
                      texture slots. "Leave..." asks before it leaves.

The model view shows the level's vehicles and buildings on their own, the driver too
(at a level's start he sits in a vehicle and the game doesn't draw him: the viewer
draws his own model for the model view, standing).

The panels pick the level, turn parts of the picture on and off (terrain, buildings,
vehicles, effects, other items, sky, HUD, the far draw distance, a plain
background), show markers for the level's records (vehicle spawns, the missile
carrier and its route, buildings, RDUs, TNT, crates, communication points, push
blocks, optional collision outlines and height zones) and list the vehicles and
buildings for the model view. Everything is drawn by the game's own code from your
ROM. In the menus or on the globe the viewer shows the picture as it is (the free
camera works in levels).


Editor and mods
---------------
The viewer's editor changes what a level places: vehicle spawns (and so where the
player starts), the missile carrier's start, heading, speed and route length,
buildings and other objects, RDUs, TNT, ammo crates, communication points, push
blocks and their slots; and, in its Terrain mode, the landscape itself and its
textures (see Terrain below).
Turn it on in the viewer's Editor panel (or Ctrl+E), then:

  left click          select a marker; the selected record gets a gizmo
  drag the square     move it on the ground (it sits on the ground: "Moved records sit
                      on the ground"; a grid in the Editor panel snaps x and z)
  drag an arrow       move it along x (red), y (green, up) or z (blue)
  drag the ring       turn it (vehicles and the carrier)
  Inspector           every field as a number (Enter to change it, or its - / +: held
                      or pressed again and again, that's one change to undo)
  Ctrl+Z / Ctrl+Y     undo / redo        Ctrl+D  duplicate (a copy next to it)
  Del                 delete             R / Shift+R  turn by 15 degrees
  G                   put it on the ground

Edited records are drawn in magenta with a *, records a loaded mod changed in
orange. The Changes panel lists every change (click one to select its record), warns
when an edit changed the ground along the missile carrier's route or put a building,
vehicle or push block on it (the carrier may crash as the level starts), and:
  Apply              the game sets the level up again from the edited file, so what
                     you see is the game really running your edit (vehicles that
                     spawn, buildings with their collision, the carrier's route ...)
  Play from here     leaves the viewer into the edited level as a normal game. This is
                     a test run: saving is off until you close bc.exe (a "TEST RUN"
                     marker shows in a corner), so a test can't change your save.
  Save mod           writes mods\<name>.bcmod next to bc.exe; with "Use it from the
                     next start" it is also enabled in bc.ini. Edits you leave the
                     viewer with unsaved are kept in mods\autosave_LNN.bcmod.

A mod file is small JSON: for each level, the changes (record, field, value) and a
fingerprint (SHA-1) of the level as your ROM has it, so a mod made for another ROM
version is refused instead of breaking the level. It holds none of the game's data:
share the .bcmod file, never a ROM. To use mods, put them in the mods folder and
list them in bc.ini:
  [mods]
  enabled = my_mod, other_mod
(or start bc.exe --mod path\to\file.bcmod). The game changes the level files in
memory as it reads them from your ROM; your ROM file is never changed. Two mods
can't change the same level. With no mods enabled the game is exactly as before.

Terrain (the Editor panel's "Terrain" choice, or Ctrl+T while editing) shapes the
ground; the Terrain panel holds the tools and their settings:
  Raise / Lower      hold the left button and move over the ground (a circle shows
                     the brush: radius, strength, falloff; [ and ] change its size)
  Smooth / Flatten   evens the ground out / brings it to a height (Shift+click picks
                     the height under the mouse, or type it)
  Ramp               click its start, then its end (it rises by "Rise" above the
                     ground there); "Make the ramp" (Enter) builds it
  Select             click triangles (or corners); Shift/Ctrl+click adds; "Move by"
                     (PageUp / PageDown: up and down), "Split" (X) cuts each selected
                     triangle in four: big flat triangles need that before the brushes
                     can make a hill on them. The triangles next to them (and the
                     ground and walls along the same edges) are cut to match, so no
                     cracks open when you raise the new corners; "Close seams" fixes
                     cracks left by splits made with older versions
  (where the level's separate pieces meet, their corners are kept together: moving a
  corner moves the neighbouring piece's corner a unit or two away with it, and a
  corner lying along another piece's edge - or a piece's border a few units from
  another's - stays on that edge: it moves with the edge's ends; Split that triangle
  to move it on its own. Where two pieces' edges cross at a corner and a stroke would
  pull them apart, both are cut there so they meet. So no gap opens)
  Paint              gives the selected triangles another texture of the level (those
                     marked "here" are drawn by the lists where they are), its
                     coordinates kept or projected from above; "Switch the run" changes
                     the texture of every triangle of a run
  Texture slot from a PNG   puts your own picture (the size the level draws the slot
                     at: shown) in a texture slot the selection uses (every place that
                     draws that slot shows it), or "as a new slot for the run" (only that
                     run shows it). A busy picture may take more room than the slot
                     had: the slot grows (up to the size the texture unpacks to).
                     Textures the game shows smaller far away get those smaller
                     versions made from your picture too; effect and medal textures
                     keep their colour set (shared with other textures) and use its
                     nearest colours. The keyboard ("Keys...") types the picture's
                     path with a controller
  Put records on the new ground   moves vehicle spawns, buildings, RDUs ... up or down
                     with the ground under them
  Controller: the tool works at the cross in the picture's centre (the left stick
                     moves you along the ground, so the cross moves over it); A use
                     (hold for the brushes; Ramp: start, end, A again builds it), X next tool,
                     shoulders brush size, D-pad left / right strength, up / down the
                     selection up / down (Select) or the ramp's rise, B let go;
                     Start for the panels (Split, Paint, Apply, Undo, Save ...).
If Apply makes the missile carrier crash at once (you changed the ground under it or
put something in its way), the viewer stops there and says so: undo that change (or
make it off the carrier's route) and Apply again.
Bridges, moving parts, switches and gates are drawn in red and can't be moved (a brush
skips them and says so). The panel shows the level's size against the room the game
has for it and the per-frame drawing lists against what the game allocates; a stroke
that would go past either is refused with the reason. While you work the picture shows
the moved ground at once; Apply (a quarter of a second) rebuilds the level - collision,
ground, drawing - and the game sets it up again, so splits, paint and pictures show
then ("Apply after every stroke" does it for you). Every stroke is one change in the
Changes panel: Ctrl+Z / Ctrl+Y undo and redo them with the record changes. Saved mods
keep the terrain and the pictures (format "bcmod/2"); a level opened with a mod loaded
starts from it and saves it again with your new changes.

Geometry mods (format "bcmod/2") can also come from the decomp's bcfiles tool, for
example from a level edited in Blender (python3 tools_port/bcfiles geo-gltf /
geo-import): they move, add or remove the ground's, the scenery's and the walls'
triangles and can give a surface another texture. They go in the mods folder like any
mod; the game works out the level's collision, ground and drawing data from the edited
triangles when the level loads, and puts a mod's texture pictures ("textures") into
their slots as it reads them from your ROM.

For emulators, a mod can be put into a copy of your ROM with the decomp's bcfiles
tool (python3 tools_port/bcfiles mod-export ROM my_mod.bcmod new.z64). When the
edited level still fits its place in the ROM that ROM also runs in bc.exe
(bc.exe --no-rom-check new.z64); one that grew needs the decomp's shiftable build
(make SHIFT=1), whose ROM has the files moved and runs in emulators and in bc.exe:
the shiftable build leaves a map of where the files went inside the ROM, bc.exe
finds them through it and accepts such a ROM as it is (no --no-rom-check; bc.log
says "a ROM from the shiftable build ..."). A shiftable-build ROM made from changed
game code is refused (bc.exe runs its own code), and so is one made before the
shiftable build wrote its map (October 2026): it has files moved and nothing saying
where - build it again with the current tools. The mod file itself works in bc.exe
either way. A texture slot that grew also takes the shiftable build (the textures
after it move); new texture slots go into the free space at the ROM's end.


Texture packs
-------------
A texture pack replaces the game's textures (the pictures on the ground, buildings,
vehicles, menus ...) with your own, at any resolution. The game reads packs in RT64's
format: a folder with an rt64.json file and pictures, or one .rtz file. Put them in
the "packs" folder next to bc.exe and choose one in the settings menu (F1 > Textures
> Texture pack); "None" goes back to the original textures. Packs only change what
you see, never how the game plays.

Making a texture pack (no other tools needed besides a picture editor):

1. Dump the textures. F1 > Textures > tick "Dump textures" (or start bc.exe
   --dump-textures texture_dump). Then play the parts of the game you want to
   change: every texture the game shows is written once into the texture_dump folder
   as <hash>.png, in a folder for where it was first seen (frontend, level_00,
   level_01 ...). The hash in the name is how the renderer recognises the texture.
   texture_dump\index.csv lists them all (format, size, transparency, where and when
   it was first seen); open it in a spreadsheet to find things. Untick the box when
   you have what you need (the dump never changes the game; textures already in the
   folder are not written again).
2. Edit copies. Copy the pictures you want to change into a new folder, for example
   MyPack (keep their names; subfolders are fine), and edit them with any picture
   editor that saves PNG. They can be bigger than the originals: 2x, 4x ... (keep
   the same shape, and keep transparent parts transparent).
3. Build the pack. Drop the MyPack folder onto bc_packtool.exe (or run
   bc_packtool MyPack in a command prompt). It checks the pictures against the dump
   (unknown names, odd sizes, lost transparency: warnings) and writes the pack to
   packs\MyPack. Pictures you didn't change are left out. Options (bc_packtool
   --help): --rtz also makes packs\MyPack.rtz, a single file to share; --mipmaps
   makes big pictures look smoother in the distance; --name, --out, --dump.
4. Use it. F1 > Textures > Texture pack: MyPack. After changing pictures, build again
   and press "Reload the pack". Or run bc_packtool MyPack --watch (it rebuilds
   whenever you save a picture) and tick "Reload the pack by itself when it
   changes": the game shows each edit a moment after you save it.

Good to know:
- Some textures are made by the game while it runs (the globe's zoom, some effects,
  animated screens): each frame of those gets a new name, so the dump writes many
  of them and replacing one changes nothing. Real animations (explosions, the
  mission portraits) are separate textures for each frame; replace every frame.
- A texture used with different palettes (colour sets) has one name per palette.
- A few textures the renderer can't replace (listed as "raw" in index.csv).
- A replacement at the original size can look a little softer than the original in
  menus (the renderer samples replacements more finely); 2x or 4x pictures look crisp.


Saves
-----
The game saves to its EEPROM like the cartridge does: progress, times and medals are
kept in saves\blastcorps.eep next to bc.exe. Change the folder with "saves =" in
bc.ini. "controller_pak = 1" plugs a Controller Pak into the controller
(saves\blastcorps.mpk); as on the N64, the game then keeps its progress on the pak
and leaves the EEPROM alone, so switch it only if you want to play from a pak.

Both files use the same formats as the mupen64plus emulator (a 512-byte .eep; a
128 KB .mpk with four paks), so saves can be copied between the two: rename the
emulator's file to blastcorps.eep / blastcorps.mpk. Back them up like any save.


Frame rate and smoothness
-------------------------
The game runs at its original speed on any monitor: its logic is tied to a virtual
60 Hz video clock, not to your display. It draws a new picture 20-30 times a second
like on the N64 (with framerate = original, the default; see above). Each
1/60 s field is handed to the renderer on a steady 60 Hz timer and, with vsync on
(the default), shown without tearing. On 60 Hz displays that is one refresh per
N64 field.

Frame pacing (present_pacing in bc.ini, "Frame pacing" in the settings menu's Video
tab; it applies at once) decides how the pictures are handed to the display (the
choice matters only with the Direct3D 12 graphics API):
  timer  "Low latency", the default. With vsync on and a frame rate that divides
         the display's (framerate = display, or 60 on a 120 Hz display), one
         picture per refresh (per two refreshes ...), locked to the display;
         otherwise an even timer. The quickest response.
  vsync  "Smooth under load": each picture waits for the display refresh it is
         due at, so motion stays even while other programs keep the PC busy, at
         the cost of about 5-10 ms more delay (half to one refresh at 60 Hz).
         The game falls back to the timer for a few seconds if the PC can't
         keep up at all. Needs vsync on.
  vrr    "G-Sync / FreeSync", experimental: pictures come at an even pace a little
         under the display's highest rate (116 a second on 120 Hz, 139 on 144 Hz).
         Measured on a G-Sync display it was less even than timer (the pacing's
         own timer fell behind and restarted now and then), so use timer on
         G-Sync / FreeSync displays too. On a display without variable refresh it
         gives a small stutter every second or so.
  auto   picks one from the display and the settings (vsync off: timer; vsync on:
         vsync at the game's own frame rate, timer above it). Windows doesn't say
         whether G-Sync is on, so auto never picks vrr.
Try vsync if motion looks uneven while something else runs in the background.

Frame times: bc.log gets a line every 10 seconds with the time between the pictures
shown (mean, spread, slowest) and how many came over 1.5x or 2x late ("stutters"),
and at exit (and whenever a video setting changes) a full report: percentiles, the
time the game, the renderer and the graphics card took per picture, the display's
rate, the pacing in use and whether the display's refreshes followed the pictures
(variable refresh seen). The frame-rate counter (F2, twice for "detailed") shows a
graph of the last 240 pictures and the stutters since it was turned on. Each stutter
is also put down to a cause ("spikes by cause": the game's own work, the graphics
card taking too long for a picture, the renderer, new shaders or textures being
prepared, other work of the program between pictures, or the display side), and with
the orbit camera on a second line shows its own work. A "content" line checks what
the pictures show: each one should be exactly one picture's time further on, and on
the display each refresh should show that much more. A pause of over 50 ms gets a
"long retrace" line saying where the time went (the events split by kind: window,
keys, mouse, controllers). If motion isn't smooth, send the lines starting with "live:
frame times", "live: long retrace" and "live: controllers" from bc.log.

Controllers: the game controllers run on a thread of their own, so Windows' search
for new devices (a Bluetooth controller connecting, any USB or Bluetooth device coming
or going), which can take 50-400 ms, never pauses the game: it carries on with the
controller's last state meanwhile, and bc.log gets a "live: controllers: ... took
over 20 ms" line when it happens.

Settings for integrated graphics (measured on a laptop's Ryzen 7 5700U with its
Radeon graphics, 1920x1080 fullscreen at 60 Hz): framerate 60 with widescreen and
the orbit camera turning kept 59-60 pictures a second at msaa 8, 2 and 0 alike (the
graphics were 20-40 % busy); msaa 8 made each picture take 1.5 to 2 times as long as
msaa 0. If motion isn't smooth on a slower PC, lower msaa to 2 or 0 first, then the
resolution.

On 120/144 Hz and other high-refresh displays the speed is the same; a field then stays up for a whole number of
refreshes (2 or 3 in turn at 144 Hz), a slight unevenness the eye barely sees.
With framerate = display on a 120 Hz display (4 pictures per game picture) the
timer pacing shows one picture per refresh, on G-Sync / FreeSync displays too (keep
present_pacing = timer there).
When a level starts, the first second can stutter briefly while the graphics
driver prepares new shaders; the game then catches up and runs smoothly.


If something goes wrong
-----------------------
Errors are shown in a message box. The log of the last run is in bc.log next to
bc.exe; please include it in a report. If the game crashes, it also writes
bc-crash-<date>-<time>.txt (what happened) and a .dmp file of the same name (part
of the program's memory, for debugging; it can include file paths and your Windows
user name) next to bc.log; please include both. Both can be deleted afterwards.
- "The renderer could not start": try the other graphics API (api = vulkan in
  bc.ini, or d3d12), and update the graphics driver.
- A missing SDL2.dll, dxcompiler.dll or dxil.dll: keep all files of this folder
  together.
- No sound: check the volume keys (- and =) and M; the log says which sound device
  was opened.
- The keyboard stops working: another window took the focus (for example the Xbox
  button's Game Bar, or a notification). Game controllers keep working; in
  fullscreen a controller button brings the game back to the front, otherwise click
  the game's window. bc.log records focus changes and controllers coming and going.


Known issues
------------
- Only Blast Corps (USA) (Rev 1) is supported (and ROMs the decomp's shiftable build
  makes from it with files moved, see "Editor and mods" above).
- 64-bit (x86_64) program; the game's memory sits at the N64's addresses (by design).
- Some rare differences from the N64 remain under investigation (see the project's
  notes): a few vertex values in two attract demos, and when exactly a sound starts
  (by one audio frame) compared with the console.
- The analog stick from the keyboard is all-or-nothing.


Credits and licences
--------------------
Built from Blast Decorps, the Blast Corps decompilation project
(https://github.com/Jrsegrist/Blast-Decorps), under the MIT licence: LICENSE.txt.
Graphics: RT64 (MIT licence).
Windows, input and sound output: SDL2 (zlib licence). Shaders: Microsoft DirectX
Shader Compiler (dxcompiler.dll, dxil.dll). THIRD_PARTY_LICENSES.txt lists every
included component with its licence text.

Blast Corps is a trademark of its owners. This project is not affiliated with or
endorsed by Rare, Nintendo or Microsoft, and includes none of the game's data.
