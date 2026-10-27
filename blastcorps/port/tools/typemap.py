#!/usr/bin/env python3
"""Byte-order schema for the RDRAM image, from the game's own C declarations.

Compiles every hd_code game file with the host gcc (-m32 -malign-double: the
i686 MinGW struct layout) and -g, then walks the DWARF of every variable the
C declares or defines (extern declarations included, struct types expanded)
to get, per N64 data symbol, its scalar leaves: (offset, width) of every 2-,
4- and 8-byte field.  Byte-swapping the big-endian image by these leaves is
the "swap on load by type" option.

Writes OUT (one line per symbol: 'addr name size leaves'), and prints how
much of the hd_code .data/.rodata image the declarations type, and where
files disagree about a symbol's layout (the accesses a whole-image swap would
get wrong for one of the readers).

usage: typemap.py ADDRS OUT    (from the project root; ADDRS = port/build/addrs.txt)
"""
import collections
import glob
import os
import subprocess
import sys
import tempfile

from elftools.elf.elffile import ELFFile

ADDRS, OUT = sys.argv[1], sys.argv[2]
SHIM = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "include", "port_ultratypes.h")
# (the SDK headers: ultralib's; the N64 pointer marks are empty at -m32)
FLAGS = ["-m32", "-malign-double", "-g", "-O0", "-c", "-std=gnu89", "-nostdinc", "-I", ".", "-I", "include",
         "-I", "../lib/ultralib/include", "-I", "../lib/ultralib/include/PR",
         "-I", "../lib/ultralib/include/compiler/modern_gcc", "-include", SHIM, "-D_LANGUAGE_C", "-D_FINALROM",
         "-DNON_MATCHING", "-DPORT_HOST", "-D_MIPS_SZLONG=32", "-D_MIPS_SIM=1",
         "-DBUILD_VERSION=VERSION_I", "-DBUILD_VERSION_STRING=\"2.0I\"", "-w",
         "-fno-eliminate-unused-debug-symbols", "-fno-eliminate-unused-debug-types"]
IMAGE = (0x802E8BD0, 0x8030F660)
# hd .text tables (pinned), hd .data/.rodata, front-end .data/.rodata
INIT_RANGES = [(0x802447C0, 0x802E8BD0), IMAGE, (0x80208040, 0x80210E90)]
CAP = None
if os.environ.get("TYPEMAP_ALL"):
    # every symbol, .bss included: the declared layouts compare.py's strict
    # mode checks RDRAM words against (port/Makefile strict-data); open-ended
    # extents (extern T x[], views up to the next symbol) are capped, or the
    # big buffers (framebuffers, heap) don't fit in memory as leaf lists
    INIT_RANGES = [(0x80000000, 0x80800000)]
    CAP = 0x10000

addr = {}
nmsize = {}
for line in open(ADDRS):
    p = line.split()
    addr[p[0]] = int(p[1], 16)
    nmsize[p[0]] = int(p[2]) if len(p) > 2 else 0
order = sorted(set(addr.values()))


def next_sym(a):
    import bisect
    i = bisect.bisect_right(order, a)
    return order[i] if i < len(order) else a


def attr(die, name):
    a = die.attributes.get(name)
    return a.value if a is not None else None


def tref(die):
    if "DW_AT_type" not in die.attributes:
        return None
    return die.get_DIE_from_attribute("DW_AT_type")


def leaves(t, base, out, depth=0):
    """Append (offset, width) scalar leaves of type DIE t at offset base.
    Returns the type's size (None if unknown, e.g. T[])."""
    while t is not None and t.tag in ("DW_TAG_typedef", "DW_TAG_const_type", "DW_TAG_volatile_type"):
        t = tref(t)
    if t is None:
        return None
    if t.tag in ("DW_TAG_base_type", "DW_TAG_pointer_type", "DW_TAG_enumeration_type"):
        n = attr(t, "DW_AT_byte_size") or 4
        out.append((base, n, t.tag == "DW_TAG_base_type" and attr(t, "DW_AT_encoding") == 4))
        return n
    if t.tag in ("DW_TAG_structure_type", "DW_TAG_union_type"):
        for m in t.iter_children():
            if m.tag == "DW_TAG_member":
                mn = attr(m, "DW_AT_name")
                mn = mn.decode() if isinstance(mn, bytes) else (mn or "")
                off = attr(m, "DW_AT_data_member_location") or 0
                if mn.startswith("pad") and t.tag == "DW_TAG_structure_type":
                    # byte padding (`u8 pad2C[0xA]`) is what a per-file view
                    # doesn't know, not a byte field: it must not block another
                    # file's typed fields there (01C40.c's u16 LevelInfo.times
                    # under 1D990.c's pad2C).  Wider "pads" are real fields.
                    pl = []
                    leaves(tref(m), 0, pl, depth + 1)
                    widths = [w for x in pl for w in ([e[1] for e in x[4]] if x[0] == "ARRAY" else [x[1]])]
                    if widths and all(w == 1 for w in widths):
                        continue
                leaves(tref(m), base + (off if isinstance(off, int) else 0), out, depth + 1)
                if t.tag == "DW_TAG_union_type":
                    # a union's first member is its layout (Gfx: the two u32
                    # words, Mtx: the s32 matrix, Vtx: Vtx_t), not the
                    # alignment members after it
                    break
        return attr(t, "DW_AT_byte_size")
    if t.tag == "DW_TAG_array_type":
        et = tref(t)
        dims = []
        for sub in t.iter_children():
            if sub.tag == "DW_TAG_subrange_type":
                ub = attr(sub, "DW_AT_upper_bound")
                cnt = attr(sub, "DW_AT_count")
                dims.append(cnt if cnt is not None else (ub + 1 if isinstance(ub, int) else None))
        one = []
        esz = leaves(et, 0, one, depth + 1)
        one = flatten(one, esz or 0)
        inner = 1
        for d in dims[1:]:
            inner *= d or 0
        stride = (esz or 0) * inner
        n0 = dims[0] if dims else None
        # expand the element leaves for the inner dims once; outer count may be unknown
        elem = []
        for k in range(inner):
            elem += [(o + k * (esz or 0), w, f) for o, w, f in one]
        out.append(("ARRAY", base, stride, n0, elem))
        return stride * n0 if (n0 is not None and stride) else None
    return None


def flatten(lv, size):
    flat = []
    for x in lv:
        if x[0] == "ARRAY":
            _, base, stride, n, elem = x
            if not stride:
                if n == 1 or n is None:   # zero-size element / flexible: keep the leaves once
                    flat += [(base + o, w, f) for o, w, f in elem]
                continue
            if n is None:
                n = max(0, (size - base) // stride) if size else 0
            if CAP:
                n = min(n, max(1, CAP // stride))
            for k in range(n):
                flat += [(base + k * stride + o, w, f) for o, w, f in elem]
        else:
            flat.append(x)
    return flat


import re
VIEW_ARR = re.compile(r"^\s*#\s*define\s+([A-Za-z_]\w*)\s+\(\(\s*(?:struct\s+)?([A-Za-z_]\w*)\s*(\*?)\s*\*\s*\)\s*&?\s*\1\s*\)")
VIEW_SCAL = re.compile(r"^\s*#\s*define\s+([A-Za-z_]\w*)\s+\(\*\s*\(\s*(?:struct\s+)?([A-Za-z_]\w*)\s*(\*?)\s*\*\s*\)\s*&\s*\1\s*\)")
# views of N64 data symbols: address names D_XXXXXXXX, or names they were given
# (tools_port/names.txt)
sys.path.insert(0, os.path.join(os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))),
                                "tools_port"))
from names import orig  # noqa: E402
VIEW_NAME = re.compile(r"^D_[0-9A-F]{8}$")


def file_views(path):
    """name -> (type name, element is a pointer, scalar view)"""
    out = {}
    for line in open(path, errors="replace"):
        m = VIEW_ARR.match(line)
        if m and not VIEW_NAME.match(orig(m.group(1))):
            m = None
        if m:
            out[m.group(1)] = (m.group(2), m.group(3) == "*" or m.group(2) == "void", False)
            continue
        m = VIEW_SCAL.match(line)
        if m and VIEW_NAME.match(orig(m.group(1))):
            out[m.group(1)] = (m.group(2), m.group(3) == "*", True)
    return out


decls = collections.defaultdict(dict)   # name -> {file: (size, leaves)}
defined = set()                          # names some game file defines (native initialiser)
pending_views = []                       # (name, file key, element size, element leaves, scalar)
tmp = tempfile.mkdtemp()
# game files, then the libultra/libaudio wrappers (ultralib C; their data is
# defined in C, so it gets native initialisers when the port links them)
UL = "../lib/ultralib"
UL_FLAGS = ["-m32", "-malign-double", "-g", "-O0", "-c", "-nostdinc", "-I", UL, "-I", UL + "/include",
            "-I", UL + "/include/compiler/gcc", "-I", UL + "/include/PR", "-I", ".", "-D_MIPS_SZLONG=32",
            "-DBUILD_VERSION=VERSION_I", "-DBUILD_VERSION_STRING=\"2.0I\"", "-DNDEBUG", "-D_FINALROM",
            "-DF3DEX_GBI", "-D_LANGUAGE_C", "-w", "-fno-eliminate-unused-debug-symbols"]
files = [f for seg in ("hd_code", "hd_front_end") for f in sorted(glob.glob("src.us.v11/%s/*.c" % seg))]
ul_failed = []
for src in files:
    fid = os.path.basename(os.path.dirname(src)) + "/" + os.path.basename(src)
    obj = os.path.join(tmp, fid.replace("/", "_") + ".o")
    is_ul = os.path.basename(src).startswith("ul_")
    r = subprocess.run(["gcc"] + (UL_FLAGS if is_ul else FLAGS) + ["-o", obj, src], capture_output=True, text=True)
    if r.returncode and is_ul:
        ul_failed.append(fid)
        continue
    if r.returncode:
        print("compile failed: %s\n%s" % (src, r.stderr[:500]))
        continue
    views = {} if is_ul else file_views(src)
    with open(obj, "rb") as f:
        dw = ELFFile(f).get_dwarf_info()
        for cu in dw.iter_CUs():
            if views:
                # the file's `#define D_x ((T *) D_x)` views: T's layout over the
                # symbol's extent (array view) or once (scalar view)
                types = {}
                for die in cu.iter_DIEs():
                    if die.tag in ("DW_TAG_typedef", "DW_TAG_base_type", "DW_TAG_structure_type"):
                        n = attr(die, "DW_AT_name")
                        n = n.decode() if isinstance(n, bytes) else n
                        if n and (n not in types or die.tag == "DW_TAG_typedef"):
                            types[n] = die
                for nm, (tname, is_ptr, scalar) in views.items():
                    if nm not in addr or not any(r0 <= addr[nm] < r1 for r0, r1 in INIT_RANGES):
                        continue
                    a = addr[nm]
                    ext = nmsize.get(nm) or (next_sym(a) - a)
                    one = []
                    if is_ptr:
                        esz = 4
                        one = [(0, 4, False)]
                    elif tname in types:
                        esz = leaves(types[tname], 0, one)
                        one = flatten(one, esz or 0)
                    else:
                        continue
                    if not esz:
                        continue
                    # the array's extent is settled once every file is read
                    # (u16 *) / (s32 *) views of struct arrays rank like (void **) ones: last
                    kind = "#ptrview" if is_ptr or len(one) <= 1 else "#view"
                    pending_views.append((nm, fid + kind, esz, list(one), scalar))
            for die in cu.get_top_DIE().iter_children():
                if die.tag != "DW_TAG_variable":
                    continue
                nm = attr(die, "DW_AT_name")
                nm = nm.decode() if isinstance(nm, bytes) else nm
                # only initialised memory needs a byte-order schema (.bss starts
                # zeroed and is only ever written by native code)
                if nm not in addr or not any(r0 <= addr[nm] < r1 for r0, r1 in INIT_RANGES):
                    continue
                a = addr[nm]
                if "DW_AT_location" in die.attributes:
                    defined.add(nm)
                lv = []
                sz = leaves(tref(die), 0, lv)
                if (sz is None and len(lv) == 1 and lv[0][0] == "ARRAY" and lv[0][1] == 0 and lv[0][2]
                        and nm not in defined):
                    # `extern T D_x[];`: the extent is settled with the views
                    pending_views.append((nm, fid, lv[0][2], list(lv[0][4]), False))
                    continue
                if sz is None:
                    sz = nmsize.get(nm) or (next_sym(a) - a)
                    if CAP:
                        sz = min(sz, CAP)
                decls[nm][fid] = (sz, list(flatten(lv, sz)))

# array views run to the next symbol some file declares or defines: splat
# also names addresses inside arrays (mid-array references), which no file
# declares, and those must not cut the array short
declared_addrs = sorted(set(addr[n] for n in decls) | set(addr[n] for n, *_ in pending_views))
import bisect as _bisect
by_addr = collections.defaultdict(list)
for _n in decls:
    by_addr[addr[_n]].append(_n)


def fits(b, a, esz, one):
    """is the symbol at b just a field (or element) inside an array of `one`
    elements starting at a?  (05C450's `extern f32 gSolarEarthSize; /* gSolarNodes[3].unk0 */`)"""
    rel = (b - a) % esz
    for n in by_addr.get(b, []):
        for sz, lv in decls[n].values():
            if rel == 0 and sz == esz:
                return True
            first = [x for x in lv if x[0] == 0]
            if first and any(o == rel and w == first[0][1] for o, w, _ in one):
                return True
    return False


for nm, key, esz, one, scalar in pending_views:
    a = addr[nm]
    i = _bisect.bisect_right(declared_addrs, a)
    while i < len(declared_addrs) and not scalar and fits(declared_addrs[i], a, esz, one):
        i += 1
    nxt = declared_addrs[i] if i < len(declared_addrs) else a + esz
    ext = max(nmsize.get(nm) or 0, nxt - a)
    if CAP:
        ext = min(ext, CAP)
    n = 1 if scalar else max(1, ext // esz)
    # byte leaves too: an explicit u8 field blocks a coarser view's word there
    decls[nm][key] = (esz * n, [(k * esz + o, w, fl) for k in range(n) for o, w, fl in one])

# per symbol: pick the layout most files agree on; record disagreements
conflicts = []
chosen = {}
for nm, per in decls.items():
    # a typed layout (one with multi-byte leaves) beats `extern u8 D_x[]`
    # declarations, which only say "some bytes" (files then view them with
    # their own #define casts)
    typed = {f: v for f, v in per.items() if any(x[1] > 1 for x in v[1])}
    pool = typed or per
    lay = collections.Counter((sz, tuple(lv)) for sz, lv in pool.values())
    (sz, lv), _ = lay.most_common(1)[0]
    if typed:
        sz = max(sz, max(s for s, _ in per.values()))
    if len(lay) > 1:
        # several typed views of the same bytes (per-file struct views): the
        # union of their leaves, pointer-array views (`(void **) D_x`, a
        # decompiler shortcut) last; a field an earlier view types, even as a
        # byte, isn't retyped by a later one
        isptr = {}
        for f, (s, l) in pool.items():
            isptr[(s, tuple(l))] = isptr.get((s, tuple(l)), True) and f.endswith("#ptrview")
        taken = {}
        merged = []
        clash = 0
        # the most detailed view (most multi-byte fields) first: partial views
        # declare the fields they don't know as u8 arrays
        for (vsz, vlv), _ in sorted(lay.most_common(), key=lambda kv: (
                isptr[kv[0]], -sum(1 for x in kv[0][1] if x[1] > 1), -kv[1])):
            for o, w, fl in vlv:
                span = range(o, o + w)
                if all(k not in taken for k in span):
                    for k in span:
                        taken[k] = (o, w)
                    merged.append((o, w, fl))
                elif any(taken.get(k) != (o, w) for k in span):
                    clash += 1
        lv = sorted(merged)
        if clash:
            conflicts.append((addr[nm], nm, {f: (s, len([x for x in l if x[1] > 1])) for f, (s, l) in pool.items()}))
    chosen[nm] = (sz, [x for x in lv if x[1] > 1])

# OUT columns: addr name size D|- leaves   (D = some game file defines it: the
# native initialiser overwrites the image there, so it needs no swap)
with open(OUT, "w") as f:
    for nm, (sz, lv) in sorted(chosen.items(), key=lambda kv: addr[kv[0]]):
        f.write("%08X %s %d %s %s\n" % (addr[nm], nm, sz, "D" if nm in defined else "-",
                                        " ".join("%X:%d%s" % (o, w, "f" if fl else "") for o, w, fl in lv)))


def coverage(label, lo, hi):
    state = bytearray(hi - lo)   # 0 untyped, 1 declared byte-only/opaque, 2 multi-byte leaf, 3 native
    for nm, (sz, lv) in chosen.items():
        a = addr[nm]
        if not (lo <= a < hi):
            continue
        for k in range(a, min(a + sz, hi)):
            if state[k - lo] == 0:
                state[k - lo] = 1
        for o, w, _ in lv:
            for k in range(a + o, min(a + o + w, hi)):
                state[k - lo] = 2
        if nm in defined:
            for k in range(a, min(a + sz, hi)):
                state[k - lo] = 3
    tot = hi - lo
    c = collections.Counter(state)
    nsyms_img = sum(1 for nm in chosen if lo <= addr[nm] < hi)
    nsyms_all = sum(1 for nm, a in addr.items() if lo <= a < hi)
    print("%s image 0x%X bytes; %d of its %d symbols are declared in C" % (label, tot, nsyms_img, nsyms_all))
    print("  bytes defined in C (native initialiser): 0x%X (%.1f%%)" % (c[3], 100.0 * c[3] / tot))
    print("  bytes inside multi-byte typed leaves: 0x%X (%.1f%%)" % (c[2], 100.0 * c[2] / tot))
    print("  bytes declared but byte-typed/opaque (u8[], char): 0x%X (%.1f%%)" % (c[1], 100.0 * c[1] / tot))
    print("  bytes no declaration covers: 0x%X (%.1f%%)" % (c[0], 100.0 * c[0] / tot))


if ul_failed:
    print("libultra wrappers that don't compile with host gcc (their data not typed): %d: %s" %
          (len(ul_failed), " ".join(ul_failed)))
coverage("hd_code data", *IMAGE)
coverage("front-end data", 0x80208040, 0x80210E90)
print("symbols whose layout differs between files: %d" % len(conflicts))
for a, nm, per in sorted(conflicts)[:40]:
    distinct = collections.OrderedDict()
    for f, (s, n) in sorted(per.items()):
        distinct.setdefault((s, n), []).append(f)
    print("  %08X %s: %s" % (a, nm, "; ".join("size %s/%d leaves: %s%s" % (s, n, ", ".join(fs[:3]),
                                                                          " +%d" % (len(fs) - 3) if len(fs) > 3 else "")
                                             for (s, n), fs in distinct.items())))
