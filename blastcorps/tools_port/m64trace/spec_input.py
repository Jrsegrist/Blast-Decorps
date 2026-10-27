# Replay a bc_headless.exe --input file (port/src/platform/input.c format:
# "[@N] BUTTONS X Y" per controller read) in mupen64plus, keyed by the
# game's controller reads (the plugin's poll count), so the emulator and the
# headless exe see the same pad on the same read.
#   M64INPUT=file   the input file (required)
#   M64VIS=n        VIs to run (default 3000)
#   M64DUMP=a:len,...  regions logged ("#in ... dump ADDR HEX") at each game
#                   mode change and at the end
# With M64SAVEDIR (m64trace.py) the EEPROM / pak files live in that directory.
import os

VIS = int(os.environ.get("M64VIS", "3000"))
BPS = {}
# M64PRINT=1: log the game's debug messages (debug_printf, an empty function
# in the ROM: format string and a1-a3), like bc_headless.exe --print
if os.environ.get("M64PRINT"):
    BPS = {0x8029A7E4: ("print", ["a1", "a2", "a3"], [])}
    MEM = []
    DEDUPE = False
    MAXHITS = {0x8029A7E4: 1 << 30}

    def ONHIT(pc, g, rd):
        s, a = [], g["a0"]
        while len(s) < 120 and 0x80000000 <= a < 0x80800000:
            c = rd(a, 1)
            if c == 0:
                break
            s.append(chr(c) if 32 <= c < 127 else "\\x%02x" % c)
            a += 1
        return repr("".join(s))
WATCH = [(0x80364A90, 8), (0x802E8BDC, 4), (0x80364456, 1)]
DUMP = [tuple(int(x, 16) for x in d.split(":")) for d in os.environ.get("M64DUMP", "").split(",") if d]


def _load(path):
    rows, idx = [], 0
    for line in open(path):
        q = line.split("#")[0].strip()
        if not q:
            continue
        parts = q.replace(",", " ").split()
        if parts[0].startswith("@"):
            idx = int(parts[0][1:], 0)
            parts = parts[1:]
        if not parts:
            continue
        v = [int(p, 0) for p in parts] + [0, 0]
        rows.append((idx, v[0] & 0xFFFF, v[1], v[2]))
        idx += 1
    return rows


ROWS = _load(os.environ["M64INPUT"])


def _at(n):
    cur = (0, 0, 0)
    for idx, b, x, y in ROWS:
        if idx > n:
            break
        cur = (b, x, y)
    return cur


def _dump(rd, ctl, why):
    for a, ln in DUMP:
        ctl.log("dump %s %08x %s" % (why, a, "".join("%02x" % rd(a + i, 1) for i in range(ln))))


DUMP_MODE = int(os.environ.get("M64DUMPMODE", "0"), 16)   # with M64DUMPEVERY=n: also dump
DUMP_EVERY = int(os.environ.get("M64DUMPEVERY", "0"))      # every n controller reads in that mode


# M64WRITE=vi:addr:size:value,...  poke RAM at a VI, e.g. 100:802FA268:4:1 sets
# the debug-cheat switch the "-c" command line sets (bc_headless --cmdline -c)
WRITES = [tuple(int(x, 16) if i else int(x) for i, x in enumerate(w.split(":")))
          for w in os.environ.get("M64WRITE", "").split(",") if w]


def INPUT(vi, rd, ctl):
    v = ctl.vars
    for wvi, a, sz, val in WRITES:
        if vi == wvi:
            ctl.write(a, sz, val)
            ctl.log("write %08x = %x" % (a, val))
    mode = rd(0x80364A90, 8)
    if mode != v.get("mode"):
        v["mode"] = mode
        _dump(rd, ctl, "mode=%x reads=%d" % (mode, ctl.polls))
    if DUMP_EVERY and mode == DUMP_MODE and ctl.polls != v.get("lastpoll") and ctl.polls % DUMP_EVERY == 0:
        v["lastpoll"] = ctl.polls
        _dump(rd, ctl, "every mode=%x reads=%d" % (mode, ctl.polls))
    if vi == VIS - 1:
        _dump(rd, ctl, "end reads=%d" % ctl.polls)
    b, x, y = _at(ctl.polls)
    return b, x, y
