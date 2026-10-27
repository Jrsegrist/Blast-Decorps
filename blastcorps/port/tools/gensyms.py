#!/usr/bin/env python3
"""Symbol plumbing for the fixed-RAM Windows build (port/).

  gensyms.py addrs OUT                 'name addr size' for every data symbol at an
                                       N64 address (from the NON_MATCHING ELFs)
  gensyms.py copytab64 OUT.c ADDRS RECORD...   the start-up table that copies the
                                       native initialisers of pinned objects into
                                       RDRAM (from coffpin.py's records)

The clang check build (port64.mk; COFF x86_64 objects pinned by coffpin.py):
  gensyms.py link64 OUTDIR OBJ...      OUTDIR/abs_syms.o (a COFF object of absolute
                                       symbols for the data the objects use but
                                       nothing defines, rdramobj.py), stubs.c (a
                                       trap for every function nobody defines),
                                       link_report.txt; symbols read with nm
  gensyms.py ptrtab OUT.c RECORD...    the N64 value of every pointer slot in the
                                       pinned C objects (NM objects' R_MIPS_32 data
                                       relocations, words from the NM ELFs)

The MSVC build (port/CMakeLists.txt) takes what it needs from the NON_MATCHING
build as a folder of text files, so it needs neither the ELFs nor pyelftools
(only Python's standard library):
  gensyms.py inputs OUTDIR             (WSL, `make -C port inputs`) OUTDIR/addrs.txt,
                                       elfsyms.txt ('name addr func|data size' for
                                       every ELF symbol) and nmptrs.txt ('addr value'
                                       for every pointer slot of the NM objects' data)
  gensyms.py linkcoff OUTDIR ELFSYMS BASE --game LIST.. --host LIST..
                                       like link64, reading the COFF objects itself (no
                                       nm); absolute symbols relative to the image base
                                       (rdramobj.py --base); LIST: a file of object paths
  gensyms.py ptrtab-in OUT.c NMPTRS RECORD...   ptrtab from nmptrs.txt

The NON_MATCHING ELFs (build_nm/) are the source of truth for addresses: data
and .bss are pinned at their original N64 addresses there, and the pinned
.text tables (NM_PIN_HD_CODE) too.  Text moved (0x80800000+), and anything at
0x80800000 or above only exists in the NM build, so it stays native.
"""
import os
import re
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
# renamed symbols (tools_port/names.txt) are classified by the address names they had
sys.path.insert(0, os.path.join(ROOT, "tools_port"))
from names import orig, addr_of  # noqa: E402
VERSION = "us.v11"
ELFS = ["build_nm/hd_code.%s.elf" % VERSION, "build_nm/hd_front_end.%s.elf" % VERSION]
NM = os.environ.get("NM", "x86_64-w64-mingw32-nm")
PIN_LIMIT = 0x80800000
# emitted by gcc itself (64-bit division helpers, block clears it synthesises,
# sqrt for the IDO sqrt.s intrinsic): from libgcc / the C runtime
RUNTIME = {"__divdi3", "__udivdi3", "__moddi3", "__umoddi3", "__muldi3", "__ashldi3", "__ashrdi3",
           "__lshrdi3", "__fixdfdi", "__fixsfdi", "__fixunsdfdi", "__fixunssfdi", "__floatdidf",
           "__floatdisf", "__floatundidf", "__floatundisf", "memset", "memcpy", "memmove",
           "sqrt", "sqrtf", "fabs", "fabsf", "___chkstk_ms", "__alloca", "__divmoddi4",
           "__udivmoddi4", "strcpy", "strlen", "memcmp", "strchr"}


def undefined_funcs():
    out = set()
    for fn in os.listdir(ROOT):
        if fn.startswith("undefined_funcs") and fn.endswith(".txt"):
            for line in open(os.path.join(ROOT, fn)):
                m = re.match(r"\s*(\w+)\s*=", line)
                if m:
                    out.add(m.group(1))
    return out


def elf_symbols():
    """name -> (addr, is_func, size).  hd_code wins over the front end (it links first)."""
    from elftools.elf.elffile import ELFFile
    from elftools.elf.sections import SymbolTableSection
    ufuncs = undefined_funcs()
    syms = {}
    for rel in ELFS:
        with open(os.path.join(ROOT, rel), "rb") as f:
            elf = ELFFile(f)
            secs = list(elf.iter_sections())
            for st in secs:
                if not isinstance(st, SymbolTableSection):
                    continue
                for sy in st.iter_symbols():
                    n = sy.name
                    typ = sy["st_info"]["type"]
                    if not n or typ in ("STT_SECTION", "STT_FILE") or n.startswith(("$", ".L", "L8")):
                        continue
                    shndx = sy["st_shndx"]
                    if shndx == "SHN_UNDEF":
                        continue
                    v = sy["st_value"]
                    if shndx == "SHN_ABS":
                        is_func = orig(n).startswith("func_") or n in ufuncs
                    else:
                        is_func = typ == "STT_FUNC" or bool(secs[shndx]["sh_flags"] & 4)
                    if n in syms and syms[n][1] == is_func:
                        continue
                    syms.setdefault(n, (v, is_func, sy["st_size"]))
    return syms


def cmd_addrs(out):
    syms = elf_symbols()
    with open(out, "w") as f:
        for n, (v, is_func, size) in sorted(syms.items(), key=lambda kv: kv[1][0]):
            if not is_func and 0x80000000 <= v < PIN_LIMIT:
                f.write("%s %08X %d\n" % (n, v, size))


def nm_objects(objs):
    defined, undefined = set(), {}
    for o in objs:
        r = subprocess.run([NM, o], capture_output=True, text=True, check=True)
        for line in r.stdout.splitlines():
            p = line.split()
            if len(p) == 3 and p[1] in "TDBRAC" and p[1].isupper():
                defined.add(p[2])
            elif len(p) == 2 and p[0] == "U" and "/game/" in o:   # host-side objects use the C runtime
                undefined.setdefault(p[1], []).append(os.path.basename(o))
    return defined, undefined


# ---- the clang check build (port64.mk): COFF x86_64 objects read with nm ----

def cmd_link64(outdir, objs):
    """the data symbols the objects use but nothing defines go into a COFF
    object of absolute symbols (rdramobj.py), the functions into stubs.c.
    Function-local statics coffpin.py pinned (D_<address>) are defined at the
    address their name gives."""
    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    import rdramobj
    syms = elf_symbols()
    defined, undefined = nm_objects(objs)
    data, stubs, unknown = [], [], []
    for c, users in sorted(undefined.items()):
        # (__ubsan_handle_*: a sanitizer build's runtime, linked separately)
        if c in defined or c in RUNTIME or c.startswith("__imp_") or c.startswith(".") or \
                c.startswith("__ubsan_handle_"):
            continue
        if c in syms:
            v, is_func, _ = syms[c]
            if is_func:
                stubs.append(c)
            else:
                data.append((c, v))      # an N64 address, or a ROM offset (D_00xxxxxx)
        elif orig(c).startswith("D_") and addr_of(c) is not None and 0x80000000 <= addr_of(c) < PIN_LIMIT:
            data.append((c, addr_of(c)))
        elif orig(c).startswith("func_") or c.startswith("os") or c.startswith("__os") or c.startswith("gu") \
                or c.startswith("al") or c in ("bzero", "bcopy", "bcmp", "sprintf", "sqrtf", "sinf", "cosf"):
            stubs.append(c)
        else:
            unknown.append((c, users))
    rdramobj.write(os.path.join(outdir, "abs_syms.o"), data)
    with open(os.path.join(outdir, "abs_syms.txt"), "w") as f:
        f.write("".join("%s %08X\n" % (n, v) for n, v in data))
    with open(os.path.join(outdir, "stubs.c"), "w") as f:
        f.write("/* generated by port/tools/gensyms.py: functions no linked object defines */\n")
        f.write("void port_stub_hit(const char *name);\n")
        for c in stubs:
            f.write("void %s(void) { port_stub_hit(\"%s\"); }\n" % (c, c))
    with open(os.path.join(outdir, "link_report.txt"), "w") as f:
        f.write("absolute data symbols: %d\nstubbed functions: %d\nunknown: %d\n" %
                (len(data), len(stubs), len(unknown)))
        for c, users in unknown:
            f.write("UNKNOWN %s (used by %s)\n" % (c, ", ".join(sorted(set(users)))))
    print("gensyms: %d absolute data symbols, %d stubbed functions, %d unknown" % (len(data), len(stubs), len(unknown)))
    for c, users in unknown:
        print("  unknown symbol %s (used by %s)" % (c, ", ".join(sorted(set(users)))))
    return 1 if unknown else 0


def read_records(records):
    rows = []
    for r in records:
        for line in open(r):
            p = line.split()
            if len(p) == 4:
                rows.append((p[0], int(p[1], 16), p[2], int(p[3])))
    return rows


def cmd_copytab64(out, addrs_path, records):
    """copytab for coffpin.py's records (each object's size is known)"""
    sizes = {}
    for line in open(addrs_path):
        p = line.split()
        if len(p) >= 3:
            sizes[int(p[1], 16)] = max(sizes.get(int(p[1], 16), 0), int(p[2]))
    rows = [r for r in read_records(records) if r[2] == "data"]
    with open(out, "w") as f:
        f.write("/* generated by port/tools/gensyms.py copytab64: native initialisers of pinned objects */\n")
        f.write("#include \"rdram.h\"\n")
        for n, a, k, s in rows:
            f.write("extern const char __native_%s[];\n" % n)
        f.write("const PortCopy port_copytab[] = {\n")
        for n, a, k, s in rows:
            f.write("    { 0x%08Xu, __native_%s, __native_%s + %d, \"%s\", %du },\n" % (a, n, n, s, n, sizes.get(a, 0)))
        f.write("    { 0, 0, 0, 0, 0 }\n};\n")


def nm_sections(maps):
    """(object path, section) -> address, from the NM link maps"""
    out = {}
    one = re.compile(r"^\s*(\.[\w.]+)\s+0x([0-9a-f]+)\s+0x([0-9a-f]+)\s+(\S+\.o)\s*$")
    name_only = re.compile(r"^\s*(\.[\w.]+)\s*$")
    rest = re.compile(r"^\s+0x([0-9a-f]+)\s+0x([0-9a-f]+)\s+(\S+\.o)\s*$")
    for mp in maps:
        pending = None
        for line in open(os.path.join(ROOT, mp)):
            m = one.match(line)
            if m:
                if int(m.group(3), 16):
                    out[(m.group(4), m.group(1))] = int(m.group(2), 16)
                pending = None
                continue
            m = name_only.match(line)
            if m:
                pending = m.group(1)
                continue
            m = rest.match(line)
            if m and pending:
                if int(m.group(2), 16):
                    out[(m.group(3), pending)] = int(m.group(1), 16)
            pending = None
    return out


def cmd_ptrtab(out, records):
    """every pointer slot (an R_MIPS_32 data relocation of the NON_MATCHING
    objects) inside a pinned C object, with the word the NM ELF holds there:
    start-up writes them over the native initialisers (whose N64_DPTR slots
    are 0 in the 64-bit build)"""
    # every pinned object: one whose initialiser is only N64_DPTR slots is all
    # zero natively, so the compiler put it in .bss
    rows = read_records(records)
    ranges = sorted((a, a + s, n) for n, a, k, s in rows)
    return write_ptrtab(out, ranges, nm_pointer_slots())


def nm_pointer_slots():
    """{address: N64 word} for every pointer slot (R_MIPS_32 data relocation) of the NM objects"""
    from elftools.elf.elffile import ELFFile
    from elftools.elf.relocation import RelocationSection
    secaddr = nm_sections(["build_nm/hd_code.%s.map" % VERSION, "build_nm/hd_front_end.%s.map" % VERSION])
    slots = set()
    for (obj, sec), base in secaddr.items():
        # only the C objects' data: their relocations are the decomp's own code
        # (data the build extracts from the ROM is never read here)
        if sec not in (".data", ".rodata", ".sdata") or not obj.endswith(".c.o"):
            continue
        path = os.path.join(ROOT, obj)
        if not os.path.exists(path):
            continue
        with open(path, "rb") as fh:
            elf = ELFFile(fh)
            rs = elf.get_section_by_name(".rel" + sec)
            if not isinstance(rs, RelocationSection):
                continue
            for r in rs.iter_relocations():
                if r["r_info_type"] == 2:      # R_MIPS_32
                    slots.add(base + r["r_offset"])
    # the words, from the linked NM ELFs
    images = []
    for rel in ELFS:
        with open(os.path.join(ROOT, rel), "rb") as fh:
            elf = ELFFile(fh)
            for s in elf.iter_sections():
                if s["sh_type"] == "SHT_PROGBITS" and s["sh_addr"] and s["sh_flags"] & 2:
                    images.append((s["sh_addr"], s.data()))
    out = {}
    for a in sorted(slots):
        v = None
        for base, data in images:
            if base <= a < base + len(data) - 3:
                v = int.from_bytes(data[a - base:a - base + 4], "big")
                break
        out[a] = v
    return out


def write_ptrtab(out, ranges, slots):
    """ranges: sorted (start, end, name) of the pinned objects; slots: {addr: word or None}"""
    import bisect
    starts = [r[0] for r in ranges]
    found, bad = [], []
    for a in sorted(slots):
        i = bisect.bisect_right(starts, a) - 1
        if i < 0 or not (ranges[i][0] <= a < ranges[i][1]):
            continue
        if a % 4:
            bad.append("%08X in %s: unaligned slot" % (a, ranges[i][2]))
            continue
        v = slots[a]
        if v is None:
            bad.append("%08X in %s: not in the NM ELF" % (a, ranges[i][2]))
        elif v != 0 and not (0x80000000 <= v < PIN_LIMIT):
            bad.append("%08X in %s: points at 0x%08X, outside RDRAM (an NM-only object)" % (a, ranges[i][2], v))
        else:
            found.append((a, v, ranges[i][2]))
    with open(out, "w") as f:
        f.write("/* generated by port/tools/gensyms.py ptrtab: the N64 pointer in every pointer slot of the\n"
                " * pinned C objects (from the NON_MATCHING objects' R_MIPS_32 relocations and ELFs) */\n")
        f.write("#include \"rdram.h\"\n")
        f.write("const PortPtrSlot port_ptrtab[] = {\n")
        for a, v, n in found:
            f.write("    { 0x%08Xu, 0x%08Xu }, /* %s */\n" % (a, v, n))
        f.write("    { 0, 0 }\n};\n")
    print("gensyms ptrtab: %d pointer slots in %d pinned objects" % (len(found), len(set(n for _, _, n in found))))
    for b in bad:
        print("  BAD " + b)
    return 1 if bad else 0


# ---- the MSVC build (port/CMakeLists.txt): text inputs, no ELFs, no nm ----

def cmd_inputs(outdir):
    """what the MSVC build needs from the NON_MATCHING build, as text files"""
    os.makedirs(outdir, exist_ok=True)
    cmd_addrs(os.path.join(outdir, "addrs.txt"))
    syms = elf_symbols()
    with open(os.path.join(outdir, "elfsyms.txt"), "w") as f:
        for n, (v, is_func, size) in sorted(syms.items(), key=lambda kv: (kv[1][0], kv[0])):
            f.write("%s %08X %s %d\n" % (n, v, "func" if is_func else "data", size))
    with open(os.path.join(outdir, "nmptrs.txt"), "w") as f:
        for a, v in sorted(nm_pointer_slots().items()):
            f.write("%08X %s\n" % (a, "-" if v is None else "%08X" % v))


def load_elfsyms(path):
    syms = {}
    for line in open(path):
        p = line.split()
        if len(p) >= 4:
            syms[p[0]] = (int(p[1], 16), p[2] == "func", int(p[3]))
    return syms


def read_list(path):
    return [l.strip() for l in open(path) if l.strip()]


# MSVC's run-time and compiler helpers (from the CRT / the import libraries)
MSVC_RUNTIME = {"__chkstk", "_fltused", "__security_cookie", "__security_check_cookie", "__GSHandlerCheck",
                "__C_specific_handler", "_penter", "_pexit", "__ImageBase", "_tls_index", "_tls_array",
                "__guard_dispatch_icall_fptr", "__guard_check_icall_fptr", "__report_rangecheckfailure",
                "_Avx2WmemEnabled", "__isa_available", "__favor"}


def cmd_linkcoff(outdir, elfsyms_path, base, game_lists, host_lists):
    """link64 for MSVC objects: symbols read from the COFF objects themselves"""
    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    import coffpin
    import rdramobj
    syms = load_elfsyms(elfsyms_path)
    n64data = {n: v[0] for n, v in syms.items() if not v[1]}
    game = [o for l in game_lists for o in read_list(l)]
    host = [o for l in host_lists for o in read_list(l)]
    defined, undefined, absfix = set(), {}, []
    for o in game + host:
        c = coffpin.Coff(open(o, "rb").read())
        for name, value, secno, typ, cls, naux, aux, idx in c.syms:
            if cls != coffpin.CLASS_EXTERNAL:
                continue
            if secno != 0 or value != 0:      # defined, absolute or COMMON
                defined.add(name)
            elif o in game:
                undefined.setdefault(name, []).append(os.path.basename(o))
        if o in host:
            # coffpin.py --base fixes the game objects' ADDR64/ADDR32 fixups to N64
            # data; host code must not have any (link.exe would get them wrong)
            idx_sym = {s[7]: s for s in c.syms}
            for si, va, symi, t, _ in c.relocs_full():
                s = idx_sym.get(symi)
                if t in (coffpin.REL_ADDR64, coffpin.REL_ADDR32, coffpin.REL_ADDR32NB) and s and s[2] == 0 and \
                        s[1] == 0 and coffpin.is_n64_name(s[0], n64data):
                    absfix.append("%s: %s" % (os.path.basename(o), s[0]))
    data, stubs, unknown = [], [], []
    for c, users in sorted(undefined.items()):
        if c in defined or c in RUNTIME or c in MSVC_RUNTIME or c.startswith("__imp_") or c.startswith("."):
            continue
        if c in syms:
            v, is_func, _ = syms[c]
            if is_func:
                stubs.append(c)
            else:
                data.append((c, v))      # an N64 address, or a ROM offset (D_00xxxxxx)
        elif orig(c).startswith("D_") and addr_of(c) is not None and 0x80000000 <= addr_of(c) < PIN_LIMIT:
            data.append((c, addr_of(c)))
        elif orig(c).startswith("func_") or c.startswith("os") or c.startswith("__os") or c.startswith("gu") \
                or c.startswith("al") or c in ("bzero", "bcopy", "bcmp", "sprintf", "sqrtf", "sinf", "cosf"):
            stubs.append(c)
        else:
            unknown.append((c, users))
    rdramobj.write(os.path.join(outdir, "abs_syms.obj"), data, base=base)
    with open(os.path.join(outdir, "abs_syms.txt"), "w") as f:
        f.write("".join("%s %08X\n" % (n, v) for n, v in data))
    # (a function MSVC knows as an intrinsic can't be defined in C: its stub
    # gets another name and the linker takes it for the missing one)
    intrinsic = {"sinf", "cosf", "sqrtf", "sin", "cos", "sqrt", "fabs", "fabsf", "memcpy", "memset", "memcmp",
                 "strlen", "strcmp", "strcpy", "ldiv", "lldiv", "abs", "labs"}
    stubs_c = "/* generated by port/tools/gensyms.py: functions no linked object defines */\n" \
              "void port_stub_hit(const char *name);\n" + \
              "".join("void %s(void) { port_stub_hit(\"%s\"); }\n" % (c, c) for c in stubs if c not in intrinsic) + \
              "".join("void port_stub_%s(void) { port_stub_hit(\"%s\"); }\n"
                      "#pragma comment(linker, \"/alternatename:%s=port_stub_%s\")\n" % (c, c, c, c)
                      for c in stubs if c in intrinsic)
    p = os.path.join(outdir, "stubs.c")
    if not os.path.exists(p) or open(p).read() != stubs_c:
        open(p, "w").write(stubs_c)
    with open(os.path.join(outdir, "link_report.txt"), "w") as f:
        f.write("absolute data symbols: %d\nstubbed functions: %d\nunknown: %d\n" %
                (len(data), len(stubs), len(unknown)))
        for c, users in unknown:
            f.write("UNKNOWN %s (used by %s)\n" % (c, ", ".join(sorted(set(users)))))
    print("gensyms: %d absolute data symbols, %d stubbed functions, %d unknown" % (len(data), len(stubs), len(unknown)))
    for c, users in unknown:
        print("  unknown symbol %s (used by %s)" % (c, ", ".join(sorted(set(users)))))
    for a in absfix:
        print("  host object with an absolute fixup to N64 data (not supported by link.exe): " + a)
    return 1 if unknown or absfix else 0


def cmd_ptrtab_in(out, nmptrs_path, records):
    rows = read_records(records)
    ranges = sorted((a, a + s, n) for n, a, k, s in rows)
    slots = {}
    for line in open(nmptrs_path):
        p = line.split()
        if len(p) == 2:
            slots[int(p[0], 16)] = None if p[1] == "-" else int(p[1], 16)
    return write_ptrtab(out, ranges, slots)


def main():
    # @FILE: the arguments listed in FILE, one per line (long object lists)
    argv = []
    for a in sys.argv:
        argv += read_list(a[1:]) if a.startswith("@") else [a]
    sys.argv = argv
    cmd = sys.argv[1]
    if cmd == "inputs":
        cmd_inputs(sys.argv[2])
    elif cmd == "linkcoff":
        args = sys.argv[5:]
        lists = {"--game": [], "--host": []}
        cur = None
        for a in args:
            if a in lists:
                cur = lists[a]
            else:
                cur.append(a)
        sys.exit(cmd_linkcoff(sys.argv[2], sys.argv[3], int(sys.argv[4], 16), lists["--game"], lists["--host"]))
    elif cmd == "ptrtab-in":
        sys.exit(cmd_ptrtab_in(sys.argv[2], sys.argv[3], sys.argv[4:]))
    elif cmd == "addrs":
        cmd_addrs(sys.argv[2])
    elif cmd == "link64":
        sys.exit(cmd_link64(sys.argv[2], sys.argv[3:]))
    elif cmd == "copytab64":
        cmd_copytab64(sys.argv[2], sys.argv[3], sys.argv[4:])
    elif cmd == "ptrtab":
        sys.exit(cmd_ptrtab(sys.argv[2], sys.argv[3:]))
    else:
        sys.exit("usage: see docstring")


if __name__ == "__main__":
    main()
