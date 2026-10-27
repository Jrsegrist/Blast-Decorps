#!/usr/bin/env python3
"""Build a bootable test ROM that runs the NON_MATCHING hd_code.

    make VERSION=us.v11 NON_MATCHING=1 nmrom [BASEROM=path/to/baserom.us.v11.z64]

Layout (see tools_port/README.md, "Test ROM"):
  * The base ROM (sha1-checked) is kept byte for byte below 0x800000, so init
    still inflates the original hd_code text + data to 0x802447C0 exactly as
    before (that keeps the data tables inside the .text bins, which the NM code
    reads at their original addresses, and the original code for anything
    that still calls original addresses).
  * Image A, appended at ROM 0x800000: the relinked NM text
    (hd_code.rom.*.elf, .hd_code at 0x80400000 = Expansion Pak RAM) followed
    by .nm_extra (+ .nm_extra_bss as zeros).
  * Image B, right after A: the original text with a trampoline
    (`j <NM address>; nop`) on every function entry that is safe to redirect,
    followed by the NM .hd_code_data (same address and size as the original,
    its code pointers relocated to the NM text).  It is DMAed over 0x802447C0.
  * init's boot tail (init_boot, init+0x1AE0..0x1BF8: a register
    save/restore around the no-op debug stub init_debug_printf) is replaced by an
    inline loader: write back + invalidate the D-cache, PI-DMA image A and
    image B, invalidate the I-cache, `a0 = gp`, jump to the NM entry
    (hd_boot) with init's stack, exactly like the original `j`.
  * With --fe-elf: the NM front end (hd_front_end.rom.*.elf, text at
    0x80500000) joins image A, and image D, appended after B, is the
    front end hd_code inflates to 0x801E7000 (hd_load_front_end): the original
    front-end text with a trampoline on every safe function entry plus the NM
    .hd_front_end_data, both gzipped again with tools_port/gzpack.py.  The loader
    stores image D's ROM range at 0x803FFFF8/C, where init has just put the
    original front end's range.
  * The ROM is padded to 12 MB (it must stay below 0xFFB000: hd_boot
    reads a debug command line from ROM 0xFFB000, which is past the end of the
    original 8 MB ROM) and the header CRCs (CIC-6102) are recomputed.

Trampolines are skipped for (a) rewritten functions that have a
conventions.txt line (their C version does not take the asm's registers, so
an original-address caller such as the front end must keep the asm), (b)
functions whose second word is a branch/jump/table target (the trampoline's
nop would replace it), (c) functions shorter than 8 bytes.

The script also checks/reports: the base ROM's gzip assets inflate to
build/'s hd_code text/data, the NM data segment's size/address, every jal/j
in the NM text by target region, words in the original text bins and the NM
data that still point into the original text, the front end's calls into
hd_code, and a disassembly of the loader.  Report: <out>.txt.
"""
import argparse
import hashlib
import os
import re
import struct
import subprocess
import sys
import tempfile
import zlib

from elftools.elf.elffile import ELFFile

CROSS = "mips-linux-gnu-"
ROM_SIZE = 0xC00000
APPEND_AT = 0x800000
CMDLINE_ROM = 0xFFB000          # hd_boot reads 64 bytes there

INIT_ROM = 0x1000
INIT_VRAM = 0x8021ED00
PATCH_START = 0x802207E0        # init+0x1AE0: addiu sp,sp,-0xF8 (register save)
PATCH_END = 0x802208F8          # init+0x1BF8: just past `j 0x802447C0; addi sp,sp,0x40`
HD_TEXT_VRAM = 0x802447C0
HD_TEXT_ROM_GZ = 0x787FD0
HD_DATA_ROM_GZ = 0x7D73B4
HD_GZ_END = 0x7E3AC7
FRONT_END_VRAM = 0x801E7000     # hd_load_front_end (46C20.c)
FRONT_END_TEXT_SIZE = 0x21040   # hd_front_end.us.v11.yaml
FE_GZ_ROM, FE_GZ_END = 0x7E3AD0, 0x7F9BE0   # gzip'd front end (text, data); init stores the range
                                            # at 0x803FFFF8 / 0x803FFFFC (hd_code gFeRomStartPtr/EndPtr)
INIT_RANGE = (0x8021ED00, 0x80224B50)


def be_words(b):
    n = len(b) // 4
    return struct.unpack(">%dI" % n, b[:n * 4])


class Elf:
    def __init__(self, path):
        self.path = path
        f = open(path, "rb")
        e = ELFFile(f)
        self.sections = {}
        idx_name = {}
        for i, s in enumerate(e.iter_sections()):
            idx_name[i] = s.name
            if s["sh_flags"] & 2:   # SHF_ALLOC
                data = None if s["sh_type"] == "SHT_NOBITS" else s.data()
                self.sections[s.name] = (s["sh_addr"], s["sh_size"], data)
        self.syms = {}
        self.funcs = {}             # name -> addr (FUNC symbols)
        for sym in e.get_section_by_name(".symtab").iter_symbols():
            shndx = sym["st_shndx"]
            sec = idx_name.get(shndx) if isinstance(shndx, int) else None
            self.syms[sym.name] = (sym["st_value"], sec)
            if sym["st_info"]["type"] == "STT_FUNC" and sec is not None:
                self.funcs[sym.name] = sym["st_value"]
        f.close()

    def sec(self, name):
        return self.sections[name]


def die(msg):
    sys.exit("nm_rom: " + msg)


def gunzip_member(b):
    d = zlib.decompressobj(16 + zlib.MAX_WBITS)
    return d.decompress(b)


def gunzip_members(b):
    """Every gzip member in b, in order (stops at trailing padding)."""
    out = []
    while b and b[:2] == b"\x1f\x8b":
        d = zlib.decompressobj(16 + zlib.MAX_WBITS)
        out.append(d.decompress(b))
        if not d.eof:
            die("truncated gzip member")
        b = d.unused_data
    return out, len(b)


def cic6102_crc(rom):
    m = 0xFFFFFFFF
    t1 = t2 = t3 = t4 = t5 = t6 = 0xF8CA4DDC
    for d in be_words(rom[0x1000:0x101000]):
        if ((t6 + d) & m) < t6:
            t4 = (t4 + 1) & m
        t6 = (t6 + d) & m
        t3 ^= d
        s = d & 0x1F
        r = ((d << s) | (d >> (32 - s))) & m if s else d
        t5 = (t5 + r) & m
        if t2 > d:
            t2 ^= r
        else:
            t2 ^= t6 ^ d
        t1 = (t1 + (t5 ^ d)) & m
    return t6 ^ t4 ^ t3, t5 ^ t2 ^ t1


def rewritten_functions(src_dir):
    """GLOBAL_ASM functions of the matching build that NON_MATCHING replaces with C."""
    inc = ["-I", ".", "-I", "include", "-I", "../lib/ultralib/include", "-I", "../lib/ultralib/include/PR",
           "-I", "../lib/ultralib/include/compiler/ido", "-D_MIPS_SZLONG=32", "-DBUILD_VERSION=VERSION_I"]
    pat = re.compile(r'GLOBAL_ASM\(\s*"([^"]+)"')
    out = set()
    for fn in sorted(os.listdir(src_dir)):
        if not fn.endswith(".c") or fn.startswith("ul_"):
            continue
        path = os.path.join(src_dir, fn)
        if "GLOBAL_ASM" not in open(path, errors="replace").read():
            continue
        sets = []
        for extra in ([], ["-DNON_MATCHING"]):
            r = subprocess.run(["cpp", "-P", "-D_LANGUAGE_C", "-D_FINALROM"] + inc + extra + [path],
                               capture_output=True, text=True)
            sets.append({os.path.splitext(os.path.basename(p))[0] for p in pat.findall(r.stdout)})
        out |= sets[0] - sets[1]
    return out


def conventions(path, full_only=False):
    """Functions with a conventions.txt line (FULL_ONLY: an `in`/`out` one,
    i.e. a C version that doesn't take the asm's registers)."""
    names = set()
    for line in open(path):
        line = line.split("#", 1)[0].strip()
        m = re.match(r"^([A-Za-z_]\w*)\s*:(.*)$", line)
        if m and (not full_only or re.search(r"(^|;)\s*(in|out)\s", m.group(2))):
            names.add(m.group(1))
    return names


def branch_target(w, pc):
    """Target of a branch/jump instruction word at pc, or None."""
    op = w >> 26
    off = ((w & 0xFFFF) ^ 0x8000) - 0x8000
    if op in (2, 3):
        return (pc & 0xF0000000) | ((w & 0x3FFFFFF) << 2)
    if op in (4, 5, 6, 7, 20, 21, 22, 23):
        return pc + 4 + (off << 2)
    if op == 1 and ((w >> 16) & 0x1F) in (0, 1, 2, 3, 16, 17, 18, 19):
        return pc + 4 + (off << 2)
    if op == 0x11 and ((w >> 21) & 0x1F) == 8:
        return pc + 4 + (off << 2)
    return None


def hi_lo_addrs(words, base):
    """(pc, address) for `lui rX,hi` + `addiu/ori/load/store ..,lo(rX)` pairs (heuristic)."""
    res = []
    for i, w in enumerate(words):
        if w >> 26 != 0xF:
            continue
        rt = (w >> 16) & 0x1F
        hi = (w & 0xFFFF) << 16
        for j in range(i + 1, min(len(words), i + 12)):
            y = words[j]
            op = y >> 26
            rs = (y >> 21) & 0x1F
            if rs == rt and op in (0x09, 0x0D, 0x20, 0x21, 0x23, 0x24, 0x25, 0x28, 0x29, 0x2B, 0x31, 0x35, 0x39, 0x3D):
                lo = y & 0xFFFF
                addr = (hi | lo) if op == 0x0D else (hi + ((lo ^ 0x8000) - 0x8000)) & 0xFFFFFFFF
                res.append((base + 4 * j, addr))
            if ((y >> 16) & 0x1F) == rt and op not in (0x28, 0x29, 0x2B, 0x39, 0x3D) and op != 0:
                break   # rt overwritten
            if op == 0 and ((y >> 11) & 0x1F) == rt:
                break
    return res


LOADER_S = """
    .set noreorder
    .set noat
    .text
    .globl nm_boot
nm_boot:
    # write back + invalidate the whole D-cache (8 KB, 16-byte lines), so no
    # dirty line lands on the DMAed images later and none of them is stale
    lui     $t0, 0x8000
    addiu   $t1, $t0, 0x2000
1:  cache   0x01, 0($t0)
    addiu   $t0, $t0, 0x10
    bne     $t0, $t1, 1b
    nop
    # image A: NM text + .nm_extra (+ the NM front end's) -> Expansion Pak RAM
    move    $a0, $zero
    li      $a1, {rom_a:#x}
    li      $a2, {vram_a:#x}
    li      $a3, {size_a:#x}
    jal     osPiRawStartDma
    nop
2:  jal     osPiGetStatus
    nop
    andi    $v0, $v0, 3
    bnez    $v0, 2b
    nop
    # image B: original text with trampolines + NM .hd_code_data -> 0x802447C0
    move    $a0, $zero
    li      $a1, {rom_b:#x}
    li      $a2, {vram_b:#x}
    li      $a3, {size_b:#x}
    jal     osPiRawStartDma
    nop
3:  jal     osPiGetStatus
    nop
    andi    $v0, $v0, 3
    bnez    $v0, 3b
    nop
    # the front end's ROM range, which init has just stored at 0x803FFFF8/C
    # (hd_code hd_load_front_end inflates it): image D, the patched front end
    lui     $t0, 0x8040
    li      $t1, {fe_start:#x}
    sw      $t1, -8($t0)
    li      $t1, {fe_end:#x}
    sw      $t1, -4($t0)
    # invalidate the whole I-cache (16 KB, 32-byte lines)
    lui     $t0, 0x8000
    addiu   $t1, $t0, 0x4000
4:  cache   0x00, 0($t0)
    addiu   $t0, $t0, 0x20
    bne     $t0, $t1, 4b
    nop
    # as the original tail: a0 = gp, back to init's stack, enter hd_code
    move    $a0, $gp
    li      $t0, {entry:#x}
    jr      $t0
    addi    $sp, $sp, 0x40
"""


def assemble_loader(params, syms, tmp):
    s = os.path.join(tmp, "nm_boot.s")
    o = os.path.join(tmp, "nm_boot.o")
    elf = os.path.join(tmp, "nm_boot.elf")
    binf = os.path.join(tmp, "nm_boot.bin")
    open(s, "w").write(LOADER_S.format(**params))
    subprocess.run([CROSS + "as", "-EB", "-march=vr4300", "-mabi=32", "-o", o, s], check=True)
    defs = []
    for k, v in syms.items():
        defs += ["--defsym", "%s=%#x" % (k, v)]
    subprocess.run([CROSS + "ld", "-EB", "-Ttext=%#x" % PATCH_START, "-e", "nm_boot"] + defs +
                   ["-o", elf, o], check=True)
    subprocess.run([CROSS + "objcopy", "-O", "binary", "--only-section", ".text", elf, binf], check=True)
    dis = subprocess.run([CROSS + "objdump", "-d", "-z", elf], capture_output=True, text=True).stdout
    return open(binf, "rb").read(), dis


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--baserom", required=True)
    ap.add_argument("--sha1", help="the base ROM's expected SHA-1 (../roms.sha1)")
    ap.add_argument("--elf", required=True, help="NM hd_code linked for the ROM (text at 0x80400000)")
    ap.add_argument("--orig-elf", required=True, help="matching hd_code elf (build/)")
    ap.add_argument("--init-elf", required=True, help="matching init elf (build/)")
    ap.add_argument("--front-end", required=True, help="inflated hd_front_end text+data")
    ap.add_argument("--fe-elf", help="NM front end linked for the ROM (text in Expansion Pak RAM)")
    ap.add_argument("--fe-orig-elf", help="matching front-end elf (build/)")
    ap.add_argument("--fe-src-dir")
    ap.add_argument("--gzpack", help="tools_port/gzpack.py (gzips the patched front end)")
    ap.add_argument("--fe-time", type=int, nargs=2, default=[0, 0], metavar=("TEXT", "DATA"),
                    help="gzip header times of the front-end text and data members")
    ap.add_argument("--conventions", required=True)
    ap.add_argument("--src-dir", required=True)
    ap.add_argument("--no-trampolines", action="store_true")
    ap.add_argument("-o", "--out", required=True)
    args = ap.parse_args()

    rep = []

    def say(s=""):
        rep.append(s)

    # ---- base ROM ----------------------------------------------------------
    base = open(args.baserom, "rb").read()
    if args.sha1:
        want = args.sha1.lower()
        got = hashlib.sha1(base).hexdigest()
        if want != got:
            die("%s: sha1 %s, expected %s" % (args.baserom, got, want))
    if len(base) != APPEND_AT:
        die("base ROM is %#x bytes, expected %#x" % (len(base), APPEND_AT))
    crc = cic6102_crc(base)
    if struct.unpack(">II", base[0x10:0x18]) != crc:
        die("CRC self-test failed on the base ROM (not CIC-6102?)")

    orig = Elf(args.orig_elf)
    nm = Elf(args.elf)
    init = Elf(args.init_elf)

    o_ta, o_ts, o_text = orig.sec(".hd_code")
    o_da, o_ds, o_data = orig.sec(".hd_code_data")
    n_ta, n_ts, n_text = nm.sec(".hd_code")
    n_da, n_ds, n_data = nm.sec(".hd_code_data")
    if o_ta != HD_TEXT_VRAM or o_da != o_ta + o_ts:
        die("unexpected original hd_code layout")
    if (n_da, n_ds) != (o_da, o_ds):
        die("NM .hd_code_data at %#x+%#x, original %#x+%#x" % (n_da, n_ds, o_da, o_ds))

    # the base ROM's gzip assets are exactly build/'s hd_code
    if gunzip_member(base[HD_TEXT_ROM_GZ:HD_DATA_ROM_GZ]) != o_text:
        die("base ROM hd_code_text does not inflate to %s's .hd_code" % args.orig_elf)
    if gunzip_member(base[HD_DATA_ROM_GZ:HD_GZ_END]) != o_data:
        die("base ROM hd_code_data does not inflate to %s's .hd_code_data" % args.orig_elf)

    # ---- image A: NM text + .nm_extra (+ bss) ------------------------------
    img_a = bytearray(n_text)
    end = n_ta + n_ts
    for name in (".nm_extra", ".nm_extra_bss"):
        if name not in nm.sections:
            continue
        a, sz, d = nm.sec(name)
        if sz == 0:
            continue
        if a < end:
            die("%s at %#x overlaps the NM text (ends %#x)" % (name, a, end))
        img_a += bytes(a - end)
        img_a += d if d is not None else bytes(sz)
        end = a + sz
    img_a += bytes((-len(img_a)) % 16)
    hd_img_len = len(img_a)

    # ---- the NM front end: its text (+ .nm_extra_fe) joins image A ---------
    fe = None
    if args.fe_elf:
        fe = Elf(args.fe_elf)
        feo = Elf(args.fe_orig_elf)
        f_ta, f_ts, f_text = feo.sec(".hd_front_end")
        f_da, f_ds, f_data = feo.sec(".hd_front_end_data")
        g_ta, g_ts, g_text = fe.sec(".hd_front_end")
        g_da, g_ds, g_data = fe.sec(".hd_front_end_data")
        if (f_ta, f_ts) != (FRONT_END_VRAM, FRONT_END_TEXT_SIZE) or f_da != f_ta + f_ts:
            die("unexpected original front-end layout")
        if (g_da, g_ds) != (f_da, f_ds):
            die("NM .hd_front_end_data at %#x+%#x, original %#x+%#x" % (g_da, g_ds, f_da, f_ds))
        members, pad = gunzip_members(base[FE_GZ_ROM:FE_GZ_END])
        if members != [f_text, f_data]:
            die("base ROM %#x..%#x does not inflate to %s's front-end text + data" % (FE_GZ_ROM, FE_GZ_END,
                                                                                   args.fe_orig_elf))
        if g_ta < n_ta + len(img_a):
            die("NM front end at %#x overlaps the NM hd_code image (ends %#x)" % (g_ta, n_ta + len(img_a)))
        img_a += bytes(g_ta - (n_ta + len(img_a)))
        end = g_ta
        for name in (".hd_front_end", ".nm_extra_fe", ".nm_extra_fe_bss"):
            if name not in fe.sections:
                continue
            a, sz, d = fe.sec(name)
            if sz == 0:
                continue
            if a < end:
                die("%s at %#x overlaps (ends %#x)" % (name, a, end))
            img_a += bytes(a - end)
            img_a += d if d is not None else bytes(sz)
            end = a + sz
        img_a += bytes((-len(img_a)) % 16)
        for name, (a, sz, d) in fe.sections.items():
            if sz and n_ta <= a < 0x80800000 and name not in (".hd_front_end", ".nm_extra_fe", ".nm_extra_fe_bss"):
                die("unexpected front-end section %s at %#x" % (name, a))

    if n_ta + len(img_a) > 0x80800000:
        die("NM image does not fit the Expansion Pak RAM")
    for name, (a, sz, d) in nm.sections.items():
        if sz and n_ta <= a < 0x80800000 and name not in (".hd_code", ".nm_extra", ".nm_extra_bss"):
            die("unexpected section %s at %#x" % (name, a))

    # ---- classification ----------------------------------------------------
    rewritten = rewritten_functions(args.src_dir)
    conv = conventions(args.conventions)
    o_words = be_words(o_text)
    t_end = o_ta + o_ts

    # bin blobs inside the original text (data tables)
    blobs = []
    for name, (addr, sec) in orig.syms.items():
        m = re.match(r"_binary_(.*)_start$", name)
        if m and sec == ".hd_code":
            e = orig.syms.get("_binary_%s_end" % m.group(1))
            if e:
                blobs.append((addr, e[0], m.group(1)))
    blobs.sort()

    def in_blob(a):
        return any(s <= a < e for s, e, _ in blobs)

    # the same blobs in the NM text (relocated copies; not code)
    n_blobs = []
    for name, (addr, sec) in nm.syms.items():
        m = re.match(r"_binary_(.*)_start$", name)
        if m and sec == ".hd_code":
            e = nm.syms.get("_binary_%s_end" % m.group(1))
            if e:
                n_blobs.append((addr, e[0]))

    def in_n_blob(a):
        return any(s <= a < e for s, e in n_blobs)

    n_starts = sorted((a, n) for n, a in nm.funcs.items() if n_ta <= a < n_ta + n_ts)

    def nm_owner(pc):
        best = None
        for a, n in n_starts:
            if a > pc:
                break
            best = (a, n)
        return "%s+%#x" % (best[1], pc - best[0]) if best else "%08X" % pc

    # original function starts (dedupe aliases) and their sizes
    starts = {}
    for name, a in orig.funcs.items():
        if o_ta <= a < t_end and not in_blob(a):
            starts.setdefault(a, []).append(name)
    bounds = sorted(set(starts) | {s for s, _, _ in blobs} | {e for _, e, _ in blobs} | {t_end})

    def size_of(a):
        i = bounds.index(a)
        return bounds[i + 1] - a

    def owner(a):
        if in_blob(a):
            return None
        best = None
        for s in starts:
            if s <= a and (best is None or s > best):
                best = s
        return best

    # every reference to an address in the original text: branches/jumps in
    # the original text, the front end, words in the original text and data
    refs = {}   # target -> set of descriptions

    def ref(t, what):
        if o_ta <= t < t_end:
            refs.setdefault(t, set()).add(what)

    for i, w in enumerate(o_words):
        pc = o_ta + 4 * i
        if in_blob(pc):
            ref(w, "text-table")
            continue
        t = branch_target(w, pc)
        if t is not None:
            ref(t, "orig-branch@%08X" % pc)
    for i, w in enumerate(be_words(n_data)):     # what the game really loads
        ref(w, "data@%08X" % (n_da + 4 * i))
    fe_img = open(args.front_end, "rb").read()
    fe_words = be_words(fe_img)
    fe_calls = {}
    for i, w in enumerate(fe_words):
        pc = FRONT_END_VRAM + 4 * i
        if 4 * i < FRONT_END_TEXT_SIZE:
            t = branch_target(w, pc)
            if t is not None and (w >> 26) in (2, 3):
                ref(t, "front-end-jal")
                if o_ta <= t < t_end:
                    fe_calls.setdefault(t, 0)
                    fe_calls[t] += 1
        else:
            ref(w, "front-end-data")
            if o_ta <= w < t_end:
                fe_calls.setdefault(w, 0)
    for pc, a in hi_lo_addrs(fe_words[:FRONT_END_TEXT_SIZE // 4], FRONT_END_VRAM):
        if a in starts:
            ref(a, "front-end-hilo")
            fe_calls.setdefault(a, 0)

    # ---- trampolines -------------------------------------------------------
    tramp = {}      # orig addr -> nm addr
    skipped = []    # (addr, names, reason)
    for a in sorted(starts):
        names = starts[a]
        nm_addrs = {nm.funcs.get(n) for n in names} - {None}
        nm_addrs = {x for x in nm_addrs if n_ta <= x < n_ta + n_ts}
        if len(nm_addrs) != 1:
            skipped.append((a, names, "not in the NM text"))
            continue
        if size_of(a) < 8:
            skipped.append((a, names, "shorter than 8 bytes"))
            continue
        prev = o_words[(a - o_ta) // 4 - 1] if a > o_ta else 0
        if branch_target(prev, a - 4) is not None or (prev >> 26 == 0 and (prev & 0x3E) == 8):
            skipped.append((a, names, "first word is a delay slot"))
            continue
        if any(n in rewritten and n in conv for n in names):
            skipped.append((a, names, "rewritten with a non-ABI convention"))
            continue
        r4 = {r for r in refs.get(a + 4, ()) if not r.startswith("orig-branch@") or
              not (a <= int(r[-8:], 16) < a + size_of(a))}
        if r4:
            skipped.append((a, names, "second word is a target: %s" % ", ".join(sorted(r4))))
            continue
        tramp[a] = nm_addrs.pop()

    # Original code that can still run: a function body runs from the original
    # text when something enters it other than through a trampoline (an
    # untrampolined entry, or an address inside it).  Starting from what can
    # reach the original text from outside it (front end, text tables, data,
    # NM code), follow branches/calls/fallthrough of every such body.  A
    # rewritten function called from original asm keeps its original code too
    # (the asm caller may rely on registers the C version does not preserve),
    # and so does any running function whose second word it branches to.
    def body_targets(g):
        out = []
        for x in range(g, g + size_of(g), 4):
            t = branch_target(o_words[(x - o_ta) // 4], x)
            if t is not None and not (g <= t < g + size_of(g)):
                out.append(t)
        last = g + size_of(g) - 8
        if last >= g:
            w = o_words[(last - o_ta) // 4]
            if not ((w >> 26) == 2 or (w >> 16) == 0x1000 or ((w >> 26) == 0 and (w & 0x3F) == 8)):
                out.append(g + size_of(g))      # may fall through into the next one
        return out

    running = {}    # function start -> why its original body can run
    work = []
    for t in sorted(fe_calls):
        work.append((t, "front end", False))
    for t, rs in sorted(refs.items()):
        for r in rs:
            if r == "text-table" or r.startswith("data@") or r == "front-end-data":
                work.append((t, r.split("@")[0], True))
    nm_into_orig = set()
    for i, w in enumerate(be_words(n_text)):
        pc = n_ta + 4 * i
        if not in_n_blob(pc) and (w >> 26) in (2, 3):
            t = branch_target(w, pc)
            if o_ta <= t < t_end:
                nm_into_orig.add(t)
                work.append((t, "NM jal", False))
    while work:
        t, why, asm_caller = work.pop()
        if in_blob(t) or not (o_ta <= t < t_end):
            continue
        g = owner(t)
        if g is None:
            continue
        if t == g and g in tramp:
            if asm_caller and set(starts[g]) & rewritten:
                del tramp[g]
                skipped.append((g, starts[g], "rewritten, called by original code (%s)" % why))
            else:
                continue
        if g in running:
            continue
        running[g] = why
        if g in tramp and (set(starts[g]) & rewritten or
                           any(r.startswith("orig-branch@") for r in refs.get(g + 4, ()))):
            del tramp[g]
            skipped.append((g, starts[g], "original body runs (%s)" % why))
        for t2 in body_targets(g):
            work.append((t2, "/".join(starts[g]), True))
    tramp_rewritten = {a for a in tramp if set(starts[a]) & rewritten}
    img_b = bytearray(o_text) + bytearray(n_data)
    if not args.no_trampolines:
        for a, t in tramp.items():
            off = a - o_ta
            img_b[off:off + 8] = struct.pack(">II", 0x08000000 | ((t >> 2) & 0x3FFFFFF), 0)
    # the NM build's C definitions of the tables inside the original .text bins
    # (.nm_pin_<ADDR>_<SIZE>, linked at their original addresses): image B
    # carries them over the bin bytes (only SIZE bytes: the section's padding
    # may cover code)
    pinned = []
    for name, (a, sz, d) in sorted(nm.sections.items()):
        m = re.match(r"^\.nm_pin_([0-9A-F]{8})_([0-9A-F]+)$", name)
        if not m or not sz:
            continue
        size = int(m.group(2), 16)
        off = a - o_ta
        if a != int(m.group(1), 16) or d is None or len(d) < size or off < 0 or off + size > len(o_text):
            die("pinned table %s at %#x does not fit the original text" % (name, a))
        changed = sum(1 for i in range(size) if img_b[off + i] != d[i])
        img_b[off:off + size] = d[:size]
        pinned.append((name, a, size, changed))

    # ---- the front end: trampolines into the NM front end, gzipped (image D) --
    # hd_code inflates the front end from ROM to 0x801E7000 when it is first
    # needed (hd_load_front_end); everything else enters it at original addresses
    # (hd_code calls, pointers in its data).  So the original front-end text
    # gets a trampoline on every function entry it is safe on, and the data
    # becomes the NM .hd_front_end_data (C .data with pointers into the NM
    # front end); both are gzipped again as image D, whose ROM range the
    # loader stores where init put the original range.
    img_d = b""
    ftramp, fskipped = {}, []
    if fe is not None:
        fe_rewritten = rewritten_functions(args.fe_src_dir) if args.fe_src_dir else set()
        fe_conv = conventions(args.conventions, full_only=True)
        fw = be_words(f_text)
        f_end = f_ta + f_ts
        fblobs = []
        for name, (addr, sec) in feo.syms.items():
            m = re.match(r"_binary_(.*)_start$", name)
            if m and sec == ".hd_front_end":
                e = feo.syms.get("_binary_%s_end" % m.group(1))
                if e:
                    fblobs.append((addr, e[0]))

        def f_in_blob(a):
            return any(s <= a < e for s, e in fblobs)
        fstarts = {}
        for name, a in feo.funcs.items():
            # (jump-table case labels of a GLOBAL_ASM function are glabels too)
            if f_ta <= a < f_end and not f_in_blob(a) and not re.match(r"^L[0-9A-F]{8}", name):
                fstarts.setdefault(a, []).append(name)
        fbounds = sorted(set(fstarts) | {s for s, _ in fblobs} | {e for _, e in fblobs} | {f_end})

        def fsize(a):
            return fbounds[fbounds.index(a) + 1] - a
        # who can enter an address: branches in the original front end, and
        # any word in the front-end data, its text bins, hd_code's text/data
        fbr = {}
        fvals = set()
        for i, w in enumerate(fw):
            pc = f_ta + 4 * i
            if f_in_blob(pc):
                fvals.add(w)
                continue
            t = branch_target(w, pc)
            if t is not None:
                fbr.setdefault(t, []).append(pc)
        for ws in (be_words(f_data), be_words(g_data), o_words, be_words(o_data), be_words(n_data)):
            fvals.update(ws)
        for i, w in enumerate(o_words):
            t = branch_target(w, o_ta + 4 * i)
            if t is not None and (w >> 26) in (2, 3):
                fvals.add(t)
        for a in sorted(fstarts):
            names = fstarts[a]
            nm_addrs = {fe.funcs.get(n) for n in names} - {None}
            nm_addrs = {x for x in nm_addrs if g_ta <= x < g_ta + g_ts}
            if len(nm_addrs) != 1:
                fskipped.append((a, names, "not in the NM front-end text"))
                continue
            if fsize(a) < 8:
                fskipped.append((a, names, "shorter than 8 bytes"))
                continue
            prev = fw[(a - f_ta) // 4 - 1] if a > f_ta else 0
            if branch_target(prev, a - 4) is not None or (prev >> 26 == 0 and (prev & 0x3E) == 8):
                fskipped.append((a, names, "first word is a delay slot"))
                continue
            if any(n in fe_rewritten and n in fe_conv for n in names):
                fskipped.append((a, names, "rewritten with a non-ABI convention"))
                continue
            r4 = [p for p in fbr.get(a + 4, []) if not (a <= p < a + fsize(a))]
            if r4 or (a + 4) in fvals:
                fskipped.append((a, names, "second word is a target"))
                continue
            ftramp[a] = nm_addrs.pop()
        f_text2 = bytearray(f_text)
        if not args.no_trampolines:
            for a, t in ftramp.items():
                off = a - f_ta
                f_text2[off:off + 8] = struct.pack(">II", 0x08000000 | ((t >> 2) & 0x3FFFFFF), 0)
        f_text2 = bytes(f_text2)
        with tempfile.TemporaryDirectory() as tmp:
            for raw, blob, t in (("hd_front_end_text.raw", f_text2, args.fe_time[0]),
                                 ("hd_front_end_data.raw", g_data, args.fe_time[1])):
                p = os.path.join(tmp, raw)
                open(p, "wb").write(blob)
                subprocess.run([sys.executable, args.gzpack, p, p + ".gz", "--name", raw, "--time", str(t),
                                "--level", "6"], check=True)
                img_d += open(p + ".gz", "rb").read()
        if gunzip_members(img_d)[0] != [f_text2, g_data]:
            die("image D does not inflate back to the patched front end")
        if len(img_d) > 0x802447C0 - 0x8021ED00:
            die("image D (%#x bytes) does not fit hd_rom_load's staging buffer" % len(img_d))
        if f_ta + len(f_text2) + len(g_data) > f_ta + 0x37D00:
            die("front end larger than hd_load_front_end's 0x37D00 bytes")

    # ---- ROM ---------------------------------------------------------------
    rom = bytearray(base)
    rom_a = APPEND_AT
    rom_b = rom_a + len(img_a)
    rom += img_a + img_b
    rom += bytes((-len(rom)) % 16)
    rom_d = len(rom)
    rom += img_d
    if len(rom) > ROM_SIZE:
        die("ROM too large (%#x)" % len(rom))
    rom += b"\xff" * (ROM_SIZE - len(rom))
    assert ROM_SIZE <= CMDLINE_ROM

    entry = nm.funcs["hd_boot"]
    if entry != n_ta:
        say("note: NM entry hd_boot at %#x (not the text start)" % entry)
    params = dict(rom_a=rom_a, vram_a=n_ta, size_a=len(img_a), rom_b=rom_b, vram_b=o_ta,
                  size_b=len(img_b), entry=entry,
                  fe_start=rom_d if img_d else FE_GZ_ROM, fe_end=rom_d + len(img_d) if img_d else FE_GZ_END)
    isyms = {k: init.funcs[k] for k in ("osPiRawStartDma", "osPiGetStatus")}
    with tempfile.TemporaryDirectory() as tmp:
        loader, dis = assemble_loader(params, isyms, tmp)
    plen = PATCH_END - PATCH_START
    if len(loader) > plen:
        die("loader is %#x bytes, room for %#x" % (len(loader), plen))
    p0 = INIT_ROM + PATCH_START - INIT_VRAM
    i_ta, i_ts, i_text = init.sec(".text") if ".text" in init.sections else init.sec(".init")
    old = base[p0:p0 + plen]
    if old != i_text[PATCH_START - i_ta:PATCH_END - i_ta]:
        die("base ROM init bytes differ from %s" % args.init_elf)
    ow = be_words(old)
    if ow[0] != 0x27BDFF08 or ow[-2:] != (0x080911F0, 0x23BD0040):
        die("init boot tail is not the expected `addiu sp,-0xF8 ... j 0x802447C0; addi sp,0x40`")
    rom[p0:p0 + plen] = loader + bytes(plen - len(loader))
    c1, c2 = cic6102_crc(rom)
    rom[0x10:0x18] = struct.pack(">II", c1, c2)
    open(args.out, "wb").write(rom)

    # ---- self-checks -------------------------------------------------------
    assert rom[rom_a:rom_a + n_ts] == n_text
    assert rom[rom_b + o_ts:rom_b + o_ts + n_ds] == n_data
    assert struct.unpack(">II", rom[0x10:0x18]) == cic6102_crc(rom)
    for a, t in tramp.items():
        if not args.no_trampolines:
            w = struct.unpack(">I", rom[rom_b + a - o_ta:rom_b + a - o_ta + 4])[0]
            assert (w & 0x3FFFFFF) << 2 == t & 0x0FFFFFFF

    # ---- report ------------------------------------------------------------
    say("NM test ROM: %s (%#x bytes, CRC %08X %08X, CIC-6102)" % (args.out, len(rom), c1, c2))
    say("needs 8 MB RDRAM (Expansion Pak): NM text at %#x" % n_ta)
    say()
    say("ROM layout")
    say("  %#08x  base ROM (unchanged except init+0x1AE0..0x1BF8 and the header CRCs)" % 0)
    say("  %#08x  image A -> %#x, %#x bytes (NM .hd_code %#x + .nm_extra%s)" % (
        rom_a, n_ta, len(img_a), n_ts, "; NM front end .hd_front_end %#x at %#x + .nm_extra_fe" % (g_ts, g_ta)
        if fe is not None else ""))
    say("  %#08x  image B -> %#x, %#x bytes (original text + %d trampolines, NM .hd_code_data)"
        % (rom_b, o_ta, len(img_b), 0 if args.no_trampolines else len(tramp)))
    for name, a, size, changed in pinned:
        say("            NM table %s over the original bin at %#x, %#x bytes (%d differ from the bin)"
            % (name, a, size, changed))
    if img_d:
        say("  %#08x  image D, %#x bytes: gzip'd front end (original text + %d trampolines into the NM front "
            "end, NM .hd_front_end_data), inflated by hd_code to %#x instead of ROM %#x..%#x"
            % (rom_d, len(img_d), 0 if args.no_trampolines else len(ftramp), FRONT_END_VRAM, FE_GZ_ROM, FE_GZ_END))
    say("  %#08x  0xFF padding to %#x" % (rom_d + len(img_d), ROM_SIZE))
    say()
    say("loader (init boot tail at %#x, %#x of %#x bytes):" % (PATCH_START, len(loader), plen))
    say("\n".join(l for l in dis.splitlines() if re.match(r"^\s*8022", l)))
    say()

    # NM text: jal/j targets by region
    n_words = be_words(n_text)
    nm_addr_name = {}
    for n, a in nm.funcs.items():
        nm_addr_name.setdefault(a, n)
    region = {}
    outside = {}
    fe_targets = set()
    for i, w in enumerate(n_words):
        if (w >> 26) not in (2, 3):
            continue
        pc = n_ta + 4 * i
        if in_n_blob(pc):
            continue
        t = branch_target(w, pc)
        if n_ta <= t < n_ta + len(img_a):
            k = "NM text"
        elif o_ta <= t < t_end:
            k = "original text"
            outside.setdefault(t, []).append(pc)
        elif INIT_RANGE[0] <= t < INIT_RANGE[1]:
            k = "init"
            outside.setdefault(t, []).append(pc)
        elif FRONT_END_VRAM <= t < FRONT_END_VRAM + FRONT_END_TEXT_SIZE:
            k = "front end"
            fe_targets.add(t)
        else:
            k = "other"
            outside.setdefault(t, []).append(pc)
        region[k] = region.get(k, 0) + 1
    say("NM text jal/j targets by region (words decoded as j/jal, incl. any data in the text):")
    for k, v in sorted(region.items()):
        say("  %-14s %d" % (k, v))
    say("  front-end functions called: %d" % len(fe_targets))
    if outside:
        say("  targets outside NM text / front end:")
        for t in sorted(outside):
            o = owner(t)
            name = "/".join(starts[o]) + ("+%#x" % (t - o) if o != t else "") if o is not None else "?"
            say("    %08X %-28s from %s%s" % (t, name, " ".join(nm_owner(p) for p in outside[t][:6]),
                                              " (%s)" % ("trampolined" if t in tramp else "ORIGINAL CODE")
                                              if o_ta <= t < t_end else ""))
    say()

    # NM text: hi/lo address constants into the original text
    hl = {}
    for pc, a in hi_lo_addrs(n_words, n_ta):
        if o_ta <= a < t_end and not in_n_blob(pc):
            hl.setdefault(a, []).append(pc)
    say("NM text: lui/lo address constants into the original text (%d targets):" % len(hl))
    for a in sorted(hl):
        blob = [b for b in blobs if b[0] <= a < b[1]]
        what = "table in %s" % blob[0][2] if blob else (
            "/".join(starts[a]) + (" (trampolined)" if a in tramp else " (ORIGINAL CODE)") if a in starts
            else "code interior %s" % ("/".join(starts[owner(a)]) if owner(a) else "?"))
        say("  %08X %-46s from %s%s" % (a, what, " ".join(nm_owner(p) for p in hl[a][:3]),
                                         " (+%d)" % (len(hl[a]) - 3) if len(hl[a]) > 3 else ""))
    say()

    # data pointing into the original text
    def ptr_report(title, words, base_addr, filt=None):
        hits = {}
        for i, w in enumerate(words):
            if o_ta <= w < t_end and (filt is None or filt(base_addr + 4 * i)):
                hits.setdefault(w, []).append(base_addr + 4 * i)
        nb = sum(1 for t in hits if in_blob(t))
        say("%s (%d distinct targets; %d of them data in the text bins, not listed):" % (title, len(hits), nb))
        for t in sorted(hits):
            o = owner(t)
            if in_blob(t):
                continue
            elif o is None:
                desc = "?"
            elif o == t:
                desc = "/".join(starts[o]) + (" (trampolined)" if t in tramp else " (ORIGINAL CODE)")
            else:
                desc = "%s+%#x" % ("/".join(starts[o]), t - o)
            say("  %08X %-48s at %s" % (t, desc, " ".join("%08X" % p for p in hits[t][:4])))
        say()

    ptr_report("NM .hd_code_data words still pointing into the original text", be_words(n_data), n_da)
    ptr_report("original text tables (bin blobs) pointing into the original text", o_words, o_ta, in_blob)

    # NM data vs original data: every changed word must be the same symbol+offset
    bad = []
    o_dw, n_dw = be_words(o_data), be_words(n_data)
    for i, (ow_, nw_) in enumerate(zip(o_dw, n_dw)):
        if ow_ == nw_:
            continue
        o = owner(ow_) if o_ta <= ow_ < t_end else None
        ok = False
        if o is not None:
            for n in starts[o]:
                if n in nm.funcs and nm.funcs[n] + (ow_ - o) == nw_:
                    ok = True
                    if ow_ != o and n in rewritten:
                        bad.append((n_da + 4 * i, ow_, nw_, "interior of rewritten %s" % n))
        if not ok:
            bad.append((n_da + 4 * i, ow_, nw_, "not a relocated original-text pointer"))
    say(".hd_code_data words that differ from the original other than as relocated pointers: %d" % len(bad))
    for a, ow_, nw_, why in bad[:200]:
        say("  %08X: %08X -> %08X  %s" % (a, ow_, nw_, why))
    say()

    # front end
    say("front end -> hd_code: %d distinct targets" % len(fe_calls))
    for t in sorted(fe_calls):
        if t in tramp:
            continue
        o = owner(t)
        name = "/".join(starts[o]) if o is not None else "?"
        say("  %08X %-28s ORIGINAL CODE%s" % (t, name, "" if o == t else " (interior)"))
    say("  (all other front-end targets are trampolined into the NM code)")
    say()

    rw_running = sorted(g for g in running if set(starts[g]) & rewritten)
    say("original function bodies that can still run: %d (%d of them rewritten in the NM build)"
        % (len(running), len(rw_running)))
    for g in sorted(running):
        say("  %08X %-28s %s%s" % (g, "/".join(starts[g]), "REWRITTEN " if g in rw_running else "",
                                   "via " + running[g]))
    say()
    say("trampolines: %d; rewritten functions: %d (%d trampolined)" % (
        len(tramp), len(rewritten), len(tramp_rewritten)))
    say("function entries left as original code: %d" % len(skipped))
    for a, names, why in skipped:
        say("  %08X %-28s %s" % (a, "/".join(names), why))
    if fe is not None:
        say()
        fw2 = be_words(g_data)
        fbad = []
        for i, (ow_, nw_) in enumerate(zip(be_words(f_data), fw2)):
            if ow_ != nw_ and not (f_ta <= ow_ < f_end and g_ta <= nw_ < g_ta + g_ts):
                fbad.append((f_da + 4 * i, ow_, nw_))
        say("front end: %d trampolines into the NM front end (%d functions rewritten in C); entries left as "
            "original code: %d" % (len(ftramp), len(fe_rewritten), len(fskipped)))
        for a, names, why in fskipped:
            say("  %08X %-28s %s" % (a, "/".join(names), why))
        say("NM .hd_front_end_data words that differ from the original other than as front-end code pointers: %d"
            % len(fbad))
        for a, ow_, nw_ in fbad[:100]:
            say("  %08X: %08X -> %08X" % (a, ow_, nw_))

    open(os.path.splitext(args.out)[0] + ".txt", "w").write("\n".join(rep) + "\n")
    print(rep[0])
    print("trampolines %d, original bodies that can run %d (%d rewritten), NM jal/j into original text %d"
          % (len(tramp), len(running), len(rw_running), len(nm_into_orig)))
    if fe is not None:
        print("front end: %d trampolines, %d entries left original, image D %#x bytes at ROM %#x"
              % (len(ftramp), len(fskipped), len(img_d), rom_d))
    print("full report: %s" % (os.path.splitext(args.out)[0] + ".txt"))


if __name__ == "__main__":
    main()
