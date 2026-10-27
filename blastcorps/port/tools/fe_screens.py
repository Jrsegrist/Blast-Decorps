#!/usr/bin/env python3
"""fe_screens.py: reach the front end's screens in bc.exe with recorded controller
input (port/data/fe_input/*.txt, bc_headless --input format: "@READ BUTTONS X Y")
and save their pictures (--wshot), e.g. to compare widescreen with 4:3.

    fe_screens.py --exe BC.EXE --rom ROM --out DIR [--aspects 4:3,16:9,21:9]
                  [--only NAME,..] [--list]

Run from WSL (bc.exe and its paths through wslpath).  Each run of the table
below plays one input from power-on (--no-pace, muted, no bc.ini) and saves the
window at the screens' frames as DIR/<aspect>/<screen>.png; the game mode in the
run's trace at each of those frames is checked against the table (a run that
drifted is reported, not silently pictured).  Nothing here is ROM data: the
Controller Pak with a saved player that some screens need is made by the
"pak_player" run, from the same input files, into DIR/pak_player.mpk.

How the screens are reached (frames are game frames, the same in bc_headless):
  attract       no input: boot screens, title, the attract mode's picture with
                the scrolling ticker (MODE_ATTRACT_PICTURE, the bios)
  no_controller --no-controller
  pak_new       a new Controller Pak, START taps: the pak check, the load-game
                menu (player select), an empty slot -> name entry
  pak_player    (setup) levels_input's START / A taps on a new pak: a player
                named "A" saved on it
  load_game     that pak, START to the load-game menu: the player select with
                the ticker (name, rank, medal icons, money)
  erase         ... stick down to ERASE GAME, A, stick up to the player, A:
                the erase menu and its confirmation (not confirmed)
  pak_manager   START held at power-on: the Controller Pak manager's file list,
                A: the delete confirmation
  erase_save    START held at power-on with no Controller Pak in controller 1
                (cont_boot_check_start: START and no pak): "ERASE SAVED GAME!"
                (MODE_ERASE_SAVE).  No A press; the EEPROM is not kept
                (--no-config: no saves folder)
  new_game      levels_input (EEPROM): name entry, the globe's opening sequence
                and the globe
  vehicle_select levels_input, globe poke of level 11 (as compare.py), no A
  credits       levels_input, globe poke of level 49 (the ending: the credits
                scroll over it)
  level_end     level 0 with the debug cheat (--cmdline -c, C-right + Z ends
                it): results, the globe, the level's status
  promotion     ... units poked to 11 first: the promotion screen
  academy       ... game state 8 and 222 units poked: the academy message
  regrade       ... 234 units: the missions regraded notice
"""
import argparse
import os
import shutil
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
DATA = os.path.join(HERE, "..", "data")
INP = os.path.join(DATA, "fe_input")
LEVELS_INPUT = os.path.join(DATA, "levels_input.txt")

PLAYERS = 0x80364AF0                 # players[]: units at +0x0A (u16), gameState at +0x91
POKE_LEVEL = "930:80364AF8:1:%X"     # compare.py LEVEL_POKE_FRAME / LEVEL_SLOT0_LAST
UNITS = "%08X" % (PLAYERS + 0x0A)
STATE = "%08X" % (PLAYERS + 0x91)

M_BOOT1, M_BOOT2, M_ATTRACT, M_LEVEL = 0x10, 0x20, 0x2, 0x4
M_WORLD, M_LOAD_GAME, M_NAME_ENTRY, M_ERASE_MENU = 0x4000, 0x10000, 0x40000, 0x400000
M_ERASE_CONFIRM, M_RESULTS, M_PAK_CHECK, M_VEHICLE = 0x80000, 0x8000000, 0x10000000, 0x20000000
M_PROMOTION, M_PAK_FILES, M_PAK_DELETE, M_LEVEL_STATUS = 0x40000000, 0x100000000, 0x8000000000, 0x80
M_GLOBE_SEQ, M_ACADEMY, M_PICTURE = 0x100000000000, 0x800000000000, 0x1000000000000
M_REGRADE, M_REGRADE_END, M_NO_CONTROLLER = 0x4000000000000, 0x8000000000000, 0x800000000000000
M_ERASE_SAVE = 0x40000000000000

# name, pak (None / "new" / "player"), input, extra options, frames, [(screen, frame, mode, level)]
RUNS = [
    ("attract", None, None, [], 3150,
     [("boot", 100, M_BOOT1, None), ("boot_2", 350, M_BOOT2, None), ("title", 600, M_ATTRACT, None),
      ("attract_picture", 2700, M_PICTURE, None), ("attract_picture_2", 3100, M_PICTURE, None)]),
    ("no_controller", None, None, ["--no-controller"], 220, [("no_controller", 200, M_NO_CONTROLLER, None)]),
    ("pak_new", "new", "pak_start_name.txt", [], 1000,
     [("pak_check", 690, M_PAK_CHECK, None), ("load_game_new", 800, M_LOAD_GAME, None),
      ("name_entry_pak", 950, M_NAME_ENTRY, None)]),
    ("pak_player", "new", "pak_new_player.txt", [], 1400, []),
    ("load_game", "player", "pak_start.txt", [], 1100,
     [("load_game", 900, M_LOAD_GAME, None), ("load_game_2", 1000, M_LOAD_GAME, None)]),
    ("erase", "player", "pak_erase.txt", [], 1300,
     [("erase_menu", 1100, M_ERASE_MENU, None), ("erase_confirm", 1280, M_ERASE_CONFIRM, None)]),
    ("pak_manager", "player", "pak_manager.txt", [], 300,
     [("pak_files", 150, M_PAK_FILES, None), ("pak_delete_confirm", 280, M_PAK_DELETE, None)]),
    ("erase_save", None, "erase_save.txt", [], 400,
     [("erase_save", 150, M_ERASE_SAVE, None), ("erase_save_2", 380, M_ERASE_SAVE, None)]),
    ("new_game", None, LEVELS_INPUT, ["--poke", POKE_LEVEL % 0], 1000,
     [("name_entry", 750, M_NAME_ENTRY, None), ("globe_sequence", 940, M_GLOBE_SEQ, None),
      ("globe", 995, M_WORLD, None)]),
    ("vehicle_select", None, "vehicle_select.txt", ["--poke", POKE_LEVEL % 11], 1100,
     [("vehicle_select", 1080, M_VEHICLE, 11)]),
    ("credits", None, LEVELS_INPUT, ["--poke", POKE_LEVEL % 49], 2200,
     [("credits", 1500, M_LEVEL, 49), ("credits_2", 1900, M_LEVEL, 49), ("credits_3", 2150, M_LEVEL, 49)]),
    ("level_end", None, "level_end.txt", ["--cmdline", "-c", "--poke", POKE_LEVEL % 0], 1900,
     [("results", 1700, M_RESULTS, 0), ("globe_after_level", 1780, M_WORLD, 0),
      ("level_status", 1880, M_LEVEL_STATUS, 0)]),
    ("promotion", None, "level_end.txt",
     ["--cmdline", "-c", "--poke", POKE_LEVEL % 0 + ",1500:%s:2:B" % UNITS], 1800,
     [("promotion", 1700, M_PROMOTION, 0), ("results_after_promotion", 1790, M_RESULTS, 0)]),
    ("academy", None, "level_end.txt",
     ["--cmdline", "-c", "--poke", POKE_LEVEL % 0 + ",1700:%s:2:DE,1700:%s:1:8" % (UNITS, STATE)], 2310,
     [("academy_message", 1800, M_ACADEMY, 0), ("academy_message_2", 2300, M_ACADEMY, 0)]),
    ("regrade", None, "level_end.txt",
     ["--cmdline", "-c", "--poke", POKE_LEVEL % 0 + ",1700:%s:2:EA,1700:%s:1:8" % (UNITS, STATE)], 3100,
     [("regrade_notice", 2800, M_REGRADE, 0), ("regrade_notice_end", 3090, M_REGRADE_END, 0)]),
]

WINDOWS = {"4:3": ("original", "1280x960"), "16:9": ("16:9", "1600x900"), "21:9": ("21:9", "1680x720")}


def wpath(p):
    return subprocess.check_output(["wslpath", "-w", os.path.abspath(p)], text=True).strip()


def run(args, name, pak, inp, extra, frames, shots, aspect, outdir):
    work = os.path.join(args.out, "work", aspect.replace(":", ""), name)
    shutil.rmtree(work, ignore_errors=True)
    os.makedirs(work)
    cmd = [args.exe, wpath(args.rom), "--no-config", "--mute", "--no-msgbox", "--no-pace",
           "--frames", str(frames), "--trace", wpath(os.path.join(work, "trace.txt")),
           "--crash-dir", wpath(work), "--shot-dir", wpath(work)]
    asp, win = WINDOWS[aspect]
    cmd += ["--aspect", asp, "--window", win]
    if shots:
        cmd += ["--wshot", ",".join(str(s[1]) for s in shots)]
    if inp is not None:
        cmd += ["--input", wpath(inp if os.path.isabs(inp) else os.path.join(INP, inp))]
    if pak is not None:
        mpk = os.path.join(work, "pak.mpk")
        if pak == "player":
            shutil.copy(os.path.join(args.out, "pak_player.mpk"), mpk)
        cmd += ["--mpk", wpath(mpk)]
    cmd += extra
    with open(os.path.join(work, "err.txt"), "w") as err:
        rc = subprocess.call(cmd, cwd=work, stdout=subprocess.DEVNULL, stderr=err, timeout=args.timeout)
    trace = {}
    if os.path.exists(os.path.join(work, "trace.txt")):
        for line in open(os.path.join(work, "trace.txt")):
            f = line.split()
            if len(f) >= 5:
                trace[int(f[0])] = (int(f[3].split("=")[1], 16), int(f[4].split("=")[1]))
    bad = 0
    if rc != 0:
        print("  %s %s: exit status %d (%s)" % (aspect, name, rc, work))
        bad += 1
    if name == "pak_player":
        shutil.copy(os.path.join(work, "pak.mpk"), os.path.join(args.out, "pak_player.mpk"))
    for screen, frame, mode, level in shots:
        got = trace.get(frame)
        pic = os.path.join(work, "win_%07d.png" % frame)
        ok = got is not None and got[0] == mode and (level is None or got[1] == level) and os.path.exists(pic)
        if os.path.exists(pic):
            shutil.copy(pic, os.path.join(outdir, screen + ".png"))
        if not ok:
            bad += 1
            print("  %s %s: frame %d mode %s level %s, expected %016X%s" %
                  (aspect, screen, frame, "%016X" % got[0] if got else "?", got[1] if got else "?", mode,
                   "" if level is None else " level %d" % level))
    return bad


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--exe", help="bc.exe")
    ap.add_argument("--rom", help="the ROM (.z64)")
    ap.add_argument("--out", help="output folder")
    ap.add_argument("--aspects", default="4:3,16:9,21:9")
    ap.add_argument("--only", default="", help="runs to do (names; pak_player is added when needed)")
    ap.add_argument("--timeout", type=int, default=900)
    ap.add_argument("--list", action="store_true", help="list the runs and screens")
    args = ap.parse_args()
    if args.list:
        for name, pak, inp, extra, frames, shots in RUNS:
            print("%-15s %s" % (name, " ".join("%s@%d" % (s[0], s[1]) for s in shots) or "(setup)"))
        return 0
    if not (args.exe and args.rom and args.out):
        ap.error("--exe, --rom and --out are needed")
    only = set(x for x in args.only.split(",") if x)
    if only and any(r[1] == "player" and r[0] in only for r in RUNS):
        only.add("pak_player")
    os.makedirs(args.out, exist_ok=True)
    bad = 0
    for aspect in args.aspects.split(","):
        outdir = os.path.join(args.out, aspect.replace(":", ""))
        os.makedirs(outdir, exist_ok=True)
        for name, pak, inp, extra, frames, shots in RUNS:
            if only and name not in only:
                continue
            if name == "pak_player" and aspect != args.aspects.split(",")[0]:
                continue   # (made once)
            bad += run(args, name, pak, inp, extra, frames, shots, aspect, outdir)
            print("%s %s done" % (aspect, name), flush=True)
    print("fe_screens: %s" % ("OK" if bad == 0 else "%d problems" % bad))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
