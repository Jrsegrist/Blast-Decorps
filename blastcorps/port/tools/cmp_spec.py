# m64trace spec (tools_port/m64trace/m64trace.py) behind `make -C port compare`:
# the emulator side of the frame-by-frame comparison (port/tools/compare.py).
#
# Run by compare.py, configured through the environment:
#   CMP_DIR    output directory (emu.txt log written by m64trace, frame_NNNNNNN.bin dumps)
#   CMP_ELFS   comma-separated ELFs giving the ROM's addresses (NM test ROM:
#              build_nm/hd_code.rom.us.v11.elf; base ROM: build/hd_code.us.v11.elf)
#   CMP_DUMP   frames to dump: "every:N", "a-b" (every frame of a range), "f1,f2,..."
#              (combinations with '+'), e.g. "every:50+700-760"
#   CMP_VIS    stop after this many VIs
#   CMP_STOP   stop after this many frames (0: no limit)
#
# Logged (one line per event, parsed by compare.py):
#   B vi=V count=C                 hd_code's entry (hd_boot): boot offset
#   F n vi=V d=D mode=..           frame n: the main thread sends the scheduler the
#                                  frame's gfx task (flag 0x40) -- the point bc_headless dumps
#                                  at (D: counts since VI V); after an M line, " || "
#   G vi=V d=D                     a frame task reaches osSpTaskStartGo
#   R vi=V d=D                     __scHandleRDP (__scHandleRDP): a frame's RDP work done
#   P vi=V d=D y=Y                 __scHandleRSP (__scHandleRSP) for a frame task: its RSP
#                                  part done (y=1: it yielded to an audio task)
#   T ra=RA th=ID v=VALUE          osGetTime returns VALUE to RA (thread ID)
#   C ra=RA th=ID v=VALUE          osGetCount (callers other than osGetTime)
#   A ra=RA th=ID v=VALUE          osAiGetLength (the audio thread sizes its frames by it),
#                                  logged at the return site RA in the caller
#   Z ra=RA th=ID v=N              alAudioFrame asked for N samples (the audio frame size)
#   Q ra=FUNC th=ID v=VALUE        the game retrace counter gSchedFrameCount as a GVI_FUNCS
#                                  function reads it (port.h PORT_GVI)
#   U ra=RA th=ID v=WORD           cull_rsp_test's RSP visibility test: the output word it
#                                  tests (0xE8000000 = not visible)
#   M ra=RA th=ID c=COUNT f=FUNC q=A0 gvi=G   a message call (queue A0) / osStartThread
#                                  (thread A0) from RA (running thread ID) at COUNT, the game's
#                                  retrace counter gSchedFrameCount being G; f=enter: the entry of a
#                                  function that reads that counter
#   V (as "#in" lines)             the count register at some VIs (period, wrap-around)
# Dumps: RDRAM 0x80000000-0x80400000 as N64 big-endian bytes.
import ctypes as C, os, struct, sys

from elftools.elf.elffile import ELFFile

# renamed symbols (tools_port/names.txt) count as the address names they had
sys.path.insert(0, os.path.join(os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))),
                                "tools_port"))
from names import orig  # noqa: E402

OUT = os.environ["CMP_DIR"]
ELFS = os.environ["CMP_ELFS"].split(",")
VIS = int(os.environ.get("CMP_VIS", "36000"))
STOP = int(os.environ.get("CMP_STOP", "0"))


def parse_dumps(s):
    every, frames = 0, set()
    for part in s.split("+"):
        part = part.strip()
        if not part:
            continue
        if part.startswith("every:"):
            every = int(part[6:])
        elif "-" in part:
            a, b = part.split("-")
            frames.update(range(int(a), int(b) + 1))
        else:
            frames.update(int(x) for x in part.split(","))
    return every, frames


DUMP_EVERY, DUMP_FRAMES = parse_dumps(os.environ.get("CMP_DUMP", "every:100"))

syms = {}
code = {}
for path in ELFS:
    with open(path, "rb") as f:
        elf = ELFFile(f)
        for sec in elf.iter_sections():
            if sec.name == ".symtab":
                for s in sec.iter_symbols():
                    if s.name and s["st_info"]["type"] in ("STT_FUNC", "STT_NOTYPE", "STT_OBJECT"):
                        syms.setdefault(s.name, s["st_value"])
        for seg in elf.iter_segments():
            if seg["p_type"] == "PT_LOAD" and seg["p_filesz"]:
                code[seg["p_vaddr"]] = seg.data()


def word_at(addr):
    for base, data in code.items():
        if base <= addr < base + len(data) - 3:
            return struct.unpack(">I", data[addr - base:addr - base + 4])[0]
    raise KeyError(hex(addr))


def ret_of(name):
    """address of the function's (first) jr ra"""
    a = syms[name]
    for i in range(400):
        if word_at(a + 4 * i) == 0x03E00008:
            return a + 4 * i
    raise KeyError(name)


REGS = ["r0", "at", "v0", "v1", "a0", "a1", "a2", "a3", "t0", "t1", "t2", "t3", "t4", "t5", "t6", "t7",
        "s0", "s1", "s2", "s3", "s4", "s5", "s6", "s7", "t8", "t9", "k0", "k1", "gp", "sp", "fp", "ra"]
A_BOOT = syms["hd_boot"]
A_TASK = syms["osSpTaskStartGo"]
A_RDP = syms["__scHandleRDP"]
A_RSP = syms["__scHandleRSP"]  # __scHandleRSP: an RSP task finished (or yielded)
A_TIME = ret_of("osGetTime")
A_COUNT = ret_of("osGetCount")
A_RUNNING = syms["__osRunningThread"]


def call_site(func, callee):
    """address just after func's (first) jal callee: the call has returned"""
    a, want = syms[func], 0x0C000000 | ((syms[callee] >> 2) & 0x3FFFFFF)
    for i in range(2000):
        if word_at(a + 4 * i) == want:
            return a + 4 * i + 8
    raise KeyError((func, callee))


# cull_rsp_test (5FD50.c): the RSP visibility test; its answer is the first
# word of the output buffer gCullOutput once rsp_wait_task has waited for it
A_CULL = call_site("cull_rsp_test", "rsp_wait_task")
A_CULLBUF = syms.get("gCullOutput", 0x803BEB80)
# thread switch points: the native clock catches up with the emulator's here
SYNC = {syms[n]: n for n in ("osSendMesg", "osRecvMesg", "osJamMesg", "osStartThread")}

# ... and the entries of the game functions that read the scheduler's retrace
# counters gSchedRetraceCount/gSchedFrameCount (the game's clock), found by their
# `lui 0x8031 ... 0x56C0/0x56C4(reg)` accesses
func_sizes = {}
for path in ELFS:
    with open(path, "rb") as f:
        for sec in ELFFile(f).iter_sections():
            if sec.name == ".symtab":
                for s in sec.iter_symbols():
                    if s["st_info"]["type"] == "STT_FUNC" and orig(s.name).startswith("func_") and s["st_size"]:
                        func_sizes[s.name] = (s["st_value"], s["st_size"])
ENTRY = {}
for name, (a, size) in func_sizes.items():
    try:
        words = [word_at(a + i) for i in range(0, size, 4)]
    except KeyError:
        continue
    if any(w >> 26 in (0x23, 0x09) and (w & 0xFFFF) in (0x56C0, 0x56C4) for w in words) and \
            any(w >> 16 in (0x3C01, 0x3C02, 0x3C03, 0x3C04, 0x3C05, 0x3C06, 0x3C07, 0x3C08, 0x3C09, 0x3C0A,
                            0x3C0B, 0x3C0C, 0x3C0D, 0x3C0E, 0x3C0F, 0x3C18, 0x3C19) and (w & 0xFFFF) == 0x8031
                for w in words):
        ENTRY[a] = name
# ... and the sound player's calls that post events to the audio thread
# (1A630.c, sndPlaySfx and friends: alEvtqPostEvent from the game thread):
# which audio frame handles a sound depends on whether the game thread posts it
# before or after the audio thread's frame in that retrace
SOUND_FUNCS = ["snd_play", "snd_post_pitch_step", "snd_modify", "snd_set_slot_volume", "snd_stop_matching",
               "snd_stop"]
for name in SOUND_FUNCS:
    if name in func_sizes:
        ENTRY[func_sizes[name][0]] = name
# ... and the scheduler's client list (2C560.c): whether a retrace message reaches
# a client depends on whether the game thread adds (removes) it before or after
# that retrace.  hd_load_front_end adds the pak thread's client after inflating
# the front end, CPU time the native code doesn't take (levels 23/45: the retrace
# landed before the add in the emulator, after it natively).
SCHED_FUNCS = ["sched_add_client", "sched_remove_client"]
for name in SCHED_FUNCS:
    if name in func_sizes:
        ENTRY[func_sizes[name][0]] = name
# CMP_CALLS=func_a,func_b: log every call of these (for `compare.py calls`)
CALLS = {}
for n in filter(None, os.environ.get("CMP_CALLS", "").split(",")):
    CALLS[syms[n]] = n
if len(ENTRY) > 80:
    raise SystemExit("cmp_spec: too many entry breakpoints (%d)" % len(ENTRY))

# osAiGetLength's value, logged in the caller just after the call returns
# (the audio manager sizes each frame from it).  Logging it at the callee's
# `jr ra` gave a stale v0 (the AI_LEN load hadn't landed in the debugger's
# view yet: 0 or the previous call's result in 33,977 hits).
_JAL_AILEN = 0x0C000000 | ((syms["osAiGetLength"] >> 2) & 0x3FFFFFF)
AILEN_RET = {}
for name, (a, size) in func_sizes.items():
    for i in range(0, size, 4):
        try:
            if word_at(a + i) == _JAL_AILEN:
                AILEN_RET[a + i + 8] = name
        except KeyError:
            break
# the audio frame size the synthesizer is asked for (alAudioFrame's 4th argument)
A_AFRAME = syms.get("alAudioFrame", syms.get("func_802D9D68"))

# Retrace-counter reads the exe takes from the emulator (port.h PORT_GVI):
# where the game reads gSchedFrameCount mid-function, after CPU time the native
# code doesn't take (00000.c hd_build_frame_dl: the blinking "PRESS START").
# Also gSchedRetraceCount (the scheduler's retrace count): hd_front_end 00000.c
# fe_digger_frame (the vehicle select screen's title pulse).  And 50670.c
# ghost_record (the ghost recorder's sample time, gSchedRetraceCount - start): a
# retrace can land between its entry (a switch point) and the read.  26570.c
# yoshi_run_window (the Yoshi window's open/close fade, gYoshiOpen, read after
# yoshi_select_items) and yoshi_get_item_text (the typewriter's blinking cursor,
# `gSchedFrameCount % 10`, read after copying the text): level 31's closing fade
# and verify's cursor glyph at frame 1360 flipped with the NM ROM's code layout.
# Every gSchedFrameCount load in a GVI_FUNCS function must be a PORT_GVI in the C
# (PORT_GVI_LAST where IDO reuses the register instead of loading again).
GVI_FUNCS = ["hd_build_frame_dl", "fe_digger_frame", "ghost_record", "yoshi_run_window", "yoshi_get_item_text"]
GVI = {}
for name in GVI_FUNCS:
    a, size = func_sizes[name]
    for i in range(0, size, 4):
        w = word_at(a + i)
        if w >> 26 == 0x23 and (w & 0xFFFF) in (0x56C0, 0x56C4):
            GVI[a + i] = (name, 0x80310000 + (w & 0xFFFF))
# Single reads in big functions, keyed by their own name "FUNC:STORE": the
# gSchedRetraceCount (or gSchedFrameCount) load whose value the next few instructions store
# to %lo STORE.  00000.c hd_main_thread:
#   gLevelStartTime = gSchedRetraceCount: the level's start time, read after the level's
#     loading CPU time the native code doesn't take;
#   gReplayStartTime = gSchedFrameCount: when mode 0x40 starts (after a completed level's
#     mode 0x200), read after that mode's setup calls on a loading frame.
GVI_SITES = [("hd_main_thread", 0x4A58, 0x56C0), ("hd_main_thread", 0x4A54, 0x56C4)]
for name, store, src in GVI_SITES:
    a, size = func_sizes[name]
    for i in range(0, size, 4):
        w = word_at(a + i)
        if w >> 26 == 0x23 and (w & 0xFFFF) == src:
            rt = (w >> 16) & 31
            for k in range(1, 6):
                w2 = word_at(a + i + 4 * k)
                if w2 >> 26 == 0x2B and (w2 & 0xFFFF) == store and (w2 >> 16) & 31 == rt:
                    GVI[a + i] = (name, 0x80310000 + src, "%s:%X" % (name, store))
                    break
# Other words the exe takes from the emulator, once per retrace (port.h
# PORT_EMU, key "FUNC:LO"): the load of ADDR (SIZE bytes) in FUNC.
#   2C560.c __scHandleRetrace (__scHandleRetrace) counts the retrace in
#   gSchedRetraceCount unless gPaused (the main thread's display freeze around
#   loads) is set; the main thread clears it after its frame's CPU time
#   (00000.c hd_game_frame, after tex_reset_decode_queue), so a retrace there sees it
#   still set in the emulator and already clear natively.
EMU_SITES = [("__scHandleRetrace", 0x802E8BD0, 1)]
for name, addr, size in EMU_SITES:
    a, fsize = func_sizes[name]
    lo = addr & 0xFFFF
    ops = {1: (0x20, 0x24), 2: (0x21, 0x25), 4: (0x23,)}[size]
    found = [a + i for i in range(0, fsize, 4) if word_at(a + i) >> 26 in ops and word_at(a + i) & 0xFFFF == lo]
    if len(found) != 1:
        raise SystemExit("cmp_spec: EMU_SITES %s %08X: %d loads" % (name, addr, len(found)))
    GVI[found[0]] = (name, addr, "%s:%X" % (name, lo), size)
BPS = {A_BOOT: "B", A_TASK: "F", A_RDP: "R", A_RSP: "P", A_TIME: "T", A_COUNT: "C", A_CULL: "U"}
BPS.update({a: "A" for a in AILEN_RET})
if A_AFRAME:
    BPS[A_AFRAME] = "Z"
BPS.update({a: "M" for a in SYNC})
BPS.update({a: "E" for a in ENTRY})
BPS.update({a: "K" for a in CALLS})
BPS.update({a: "Q" for a in GVI})
GPRS, FPRS, MEM = [], [], []
DEDUPE = False
DEFMAX = 1 << 40


# CMP_INPUT=file: controller 1, a bc_headless --input file ("[@N] BUTTONS X Y" per
# controller read).  Read n is the game's n-th osContStartReadData (the exe's
# count): the pad is set at that call's entry, before its PIF read.  (The
# input plugin's own poll count also has the PIF's other controller commands:
# 2 more than the game's reads by the first frame.)
def load_input(path):
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
    table, cur = [], (0, 0, 0)
    for i, (idx, b, x, y) in enumerate(rows):
        while len(table) < idx:
            table.append(cur)
        cur = (b, x, y)
    table.append(cur)
    return table


INPUT_TABLE = load_input(os.environ["CMP_INPUT"]) if os.environ.get("CMP_INPUT") else []
A_READ = syms["osContStartReadData"]
BPS[A_READ] = "I"   # (not logged: ONHIT returns None)
# CMP_POKE=F:ADDR:SIZE:VALUE,... (hex ADDR/VALUE): write RAM when frame F is sent,
# after its dump (bc_headless --poke, same syntax)
POKES = [(int(f), int(a, 16), int(s), int(v, 16)) for f, a, s, v in
         (p.split(":") for p in os.environ.get("CMP_POKE", "").split(",") if p)]

# CMP_SHOTS=f1,f2,...: a picture of the emulator's window (Glide64mk2, what was on
# screen when frame f was sent: an earlier frame's picture) as CMP_DIR/emu_shot_F.png
# (ImageMagick `import`; a quick visual reference, not the accurate one)
SHOTS = set(int(x) for x in os.environ.get("CMP_SHOTS", "").split(",") if x)
_shot_win = {}


def shot(n):
    import subprocess
    if "wid" not in _shot_win:
        t = subprocess.run(["xwininfo", "-root", "-tree"], capture_output=True, text=True).stdout
        _shot_win["wid"] = None
        for ln in t.splitlines():
            if '"glide64mk2' in ln.lower() and "python3" in ln:
                wid = ln.split()[0]
                # (this process's window: other emulators may be running)
                p = subprocess.run(["xprop", "-id", wid, "_NET_WM_PID"], capture_output=True, text=True).stdout
                if p.split("=")[-1].strip() == str(os.getpid()):
                    _shot_win["wid"] = wid
                    break
    if _shot_win["wid"]:
        subprocess.run(["import", "-window", _shot_win["wid"], os.path.join(OUT, "emu_shot_%d.png" % n)],
                       capture_output=True)


core = C.CDLL("/usr/lib/x86_64-linux-gnu/libmupen64plus.so.2")
core.DebugGetCPUDataPtr.restype = C.c_void_p
core.DebugGetCPUDataPtr.argtypes = [C.c_int]
core.DebugMemGetPointer.restype = C.c_void_p
core.DebugMemGetPointer.argtypes = [C.c_int]
core.DebugMemWrite32.argtypes = [C.c_uint32, C.c_uint32]
core.DebugMemWrite16.argtypes = [C.c_uint32, C.c_uint16]
core.DebugMemWrite8.argtypes = [C.c_uint32, C.c_uint8]

st = {"vi": 0, "vi_count": 0, "frame": 0, "reads": 0, "pad": (0, 0, 0)}
os.makedirs(OUT, exist_ok=True)


def count():
    p = core.DebugGetCPUDataPtr(5)  # M64P_CPU_REG_COP0: u32 regs
    return C.cast(p, C.POINTER(C.c_uint32))[9]


def frac():
    """counts since the last VI (compare.py divides by the measured VI period)"""
    return "%d" % ((count() - st["vi_count"]) & 0xFFFFFFFF)


def dump(n):
    p = core.DebugMemGetPointer(1)  # M64P_DBG_PTR_RDRAM: host-order u32 words
    raw = bytearray(C.string_at(p, 0x400000))
    import array
    a = array.array("I", bytes(raw))
    a.byteswap()  # -> big-endian byte image (on a little-endian host)
    with open(os.path.join(OUT, "frame_%07d.bin" % n), "wb") as f:
        f.write(a.tobytes())


def thread_id(rd):
    t = rd(A_RUNNING, 4)
    return rd(t + 0x14, 4) if 0x80000000 <= t < 0x80800000 else -1


def frame_event(rd):
    st["frame"] += 1
    n = st["frame"]
    if (DUMP_EVERY and n % DUMP_EVERY == 0) or n in DUMP_FRAMES:
        dump(n)
    for f, a, sz, val in POKES:
        if f == n:  # after the dump, like bc_headless --poke
            {1: core.DebugMemWrite8, 2: core.DebugMemWrite16, 4: core.DebugMemWrite32}[sz](a, val)
    if n in SHOTS:
        shot(n)
    if STOP and n >= STOP:
        core.CoreDoCommand(6, 0, None)
    return "F %d vi=%d d=%s mode=%08x%08x lvl=%x mf=%x gvi=%x demo=%x rd=%d" % (
        n, st["vi"], frac(), rd(0x80364A90, 4), rd(0x80364A94, 4), rd(0x802E8BDC, 4), rd(0x80358060, 4),
        rd(0x803156C4, 4), rd(0x802E8BEC, 4), st["reads"])


_inlib = []


def set_pad(b, x, y):
    """controller 1 now (the scripted input plugin m64trace loaded, M64INPUT_SO;
    dlopen gives the same handle)"""
    if not _inlib:
        so = os.environ.get("M64INPUT_SO")
        _inlib.append(C.CDLL(so) if so else None)
    if _inlib[0]:
        b &= 0xFFFF
        C.c_uint32.in_dll(_inlib[0], "m64input_keys").value = (
            ((b >> 8) | ((b & 0xFF) << 8)) | ((x & 0xFF) << 16) | ((y & 0xFF) << 24))
    st["pad"] = (b, x, y)


def ONHIT(pc, g, rd):
    if pc == A_TASK:
        t = g["a0"]
        if not (rd(t - 0x10 + 0x08, 4) & 0x40):
            return None
        return "G vi=%d d=%s" % (st["vi"], frac())
    if pc == A_RSP:
        t = rd(g["a0"] + 0x274, 4)  # sc->curRSPTask
        if not (0x80000000 <= t < 0x80800000 and rd(t + 8, 4) & 0x40):
            return None
        return "P vi=%d d=%s y=%d" % (st["vi"], frac(), 1 if rd(t + 4, 4) == 3 else 0)
    if pc == A_RDP:
        return "R vi=%d d=%s" % (st["vi"], frac())
    if pc == A_TIME:
        # the jr ra's delay slot finishes the high word (IDO: addu v0,v0,t4):
        # the breakpoint sees v0 before it
        v0 = g["v0"]
        ds = word_at(A_TIME + 4)
        if ds >> 26 == 0 and ds & 0x7FF == 0x21 and (ds >> 11) & 31 == 2:
            v0 = (g[REGS[(ds >> 21) & 31]] + g[REGS[(ds >> 16) & 31]]) & 0xFFFFFFFF
        return "T ra=%x th=%d v=%x" % (g["ra"], thread_id(rd), (v0 << 32) | (g["v1"] & 0xFFFFFFFF))
    if pc == A_COUNT:
        if syms["osGetTime"] <= g["ra"] < A_TIME:
            return None
        return "C ra=%x th=%d v=%x" % (g["ra"], thread_id(rd), g["v0"])
    if pc == A_CULL:
        return "U ra=%x th=%d v=%x frame=%d" % (syms["cull_rsp_test"], thread_id(rd), rd(A_CULLBUF, 4), st["frame"])
    if pc in AILEN_RET:
        # (ra: this return site, in the caller)
        return "A ra=%x th=%d v=%x" % (pc, thread_id(rd), g["v0"])
    if pc == A_AFRAME:
        return "Z ra=%x th=%d v=%x" % (g["ra"], thread_id(rd), g["a3"])
    if pc in GVI:
        # (the lw hasn't run: memory holds what it loads)
        q = "Q ra=%x th=%d v=%x" % (syms[GVI[pc][0]], thread_id(rd), rd(GVI[pc][1], GVI[pc][3] if len(GVI[pc]) > 3 else 4))
        return q + (" name=%s" % GVI[pc][2] if len(GVI[pc]) > 2 else "")
    if pc in CALLS:
        # (a0-a3: the arguments; compare.py calls shows them next to the exe's)
        k = "K f=%s ra=%x frame=%d a=%s" % (CALLS[pc], g["ra"], st["frame"],
                                            ",".join("%x" % (g[r] & 0xFFFFFFFF) for r in ("a0", "a1", "a2", "a3")))
        if pc not in ENTRY:
            return k
        return k + " || M ra=%x th=%d c=%d f=enter q=0 gvi=%x" % (pc, thread_id(rd), count(), rd(0x803156C4, 4))
    if pc in ENTRY:
        return "M ra=%x th=%d c=%d f=enter q=0 gvi=%x" % (pc, thread_id(rd), count(), rd(0x803156C4, 4))
    if pc in SYNC:
        m = "M ra=%x th=%d c=%d f=%s q=%x gvi=%x" % (g["ra"], thread_id(rd), count(), SYNC[pc], g["a0"],
                                                    rd(0x803156C4, 4))
        # a frame: the main thread sends the scheduler (gScheduler) a task with
        # the frame flag 0x40 (405F0.c rsp_send_task) -- the exe dumps here too
        a1 = g["a1"]
        if (SYNC[pc] == "osSendMesg" and g["a0"] == 0x80315440 and 0x80000000 <= a1 < 0x80800000
                and rd(a1 + 8, 4) & 0x40):
            m += " || " + frame_event(rd)
        return m
    if pc == A_BOOT:
        return "B vi=%d count=%d vicount=%d" % (st["vi"], count(), st["vi_count"])
    if pc == A_READ:
        n = st["reads"]
        st["reads"] += 1
        if INPUT_TABLE:
            set_pad(*(INPUT_TABLE[n] if n < len(INPUT_TABLE) else INPUT_TABLE[-1]))
        return None
    return None


def INPUT(vi, rd, ctl):
    st["vi"] = vi
    st["vi_count"] = count()
    if vi <= 3 or vi % 600 == 0:
        ctl.log("V %d count=%d" % (vi, st["vi_count"]))
    return st["pad"]   # (m64trace sets the plugin's pad every VI: keep the last read's)
