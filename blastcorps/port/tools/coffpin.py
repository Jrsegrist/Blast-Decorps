#!/usr/bin/env python3
"""Pin a game object file's data to N64 addresses (fixed-RAM model, any COFF compiler).

It works on the COFF object itself (no assembly text, no linker script), so the
MSVC objects and the clang check build's go through the same step.  Every data
object the NON_MATCHING ELF places in N64
memory (ADDRS, from gensyms.py addrs; function-local statics by their
D_<address> name: clang calls them `fn.D_xxxxxxxx`, gcc `D_xxxxxxxx.N`) is taken
out of the object:

  initialised   its bytes stay, renamed __native_<name> (an external symbol, for
                the start-up copy table); every relocation that pointed at it
                points at a new undefined <name>
  zero (bss)    the symbol becomes an undefined external <name>

<name> is then defined by the RDRAM section object (rdramobj.py) at its N64
address, so code in this file and every other one uses the RDRAM copy.  The
object must be built with one section per variable (-fdata-sections; MSVC /Gw):
relocations against a section symbol are moved to the object in that section.

usage: coffpin.py [--base HEX --elfsyms FILE] ADDRS IN.o OUT.o RECORD
       coffpin.py [--base HEX --elfsyms FILE] --batch ADDRS OUTDIR LIST
  RECORD written: 'cname addr kind size' per pinned object (kind data|bss;
          cname = the C name the copy table uses, without __native_)
  --batch: every object listed in LIST (one path per line) -> OUTDIR/STEM.obj
          and OUTDIR/STEM.rec (STEM: the object's file name up to its first
          dot), for build systems that hand over a target's objects as a list
          (CMake's $<TARGET_OBJECTS>)

MSVC (link.exe) objects, --base IMAGE_BASE --elfsyms ELFSYMS (gensyms.py inputs):
link.exe resolves a REL32 fixup against an absolute symbol as if the symbol's
value were an RVA (image base + value), but an ADDR64/ADDR32 fixup with the
value itself.  The absolute symbols are therefore written relative to the
image base (rdramobj.py --base), and here every ADDR64/ADDR32 fixup against
an N64 data symbol (pinned, or undefined and an N64 data symbol in ELFSYMS)
gets the image base added to its addend.  link.exe refuses ADDR32NB (image-
relative) fixups against absolute symbols, which MSVC emits to index global
arrays off __ImageBase (`mov eax, [rcx + rax*4 + imagerel D_x]`): those are
resolved here (address - image base, into the addend) and become no-ops
(IMAGE_REL_AMD64_ABSOLUTE).  /Gw puts every variable in a COMDAT
section of its own: a pinned .bss variable's section loses its COMDAT flag and
its size (it has no symbol left to select it).  MSVC names a function-local
static `?D_xxxxxxxx@?1??func@@9@9`, and a C tentative definition (`u64 D_x;`
in several files) a COMMON symbol, which the linker would allocate natively:
an N64 one becomes a plain undefined reference.
"""
import os
import re
import struct
import sys

# renamed symbols (tools_port/names.txt) count as the address names they had
sys.path.insert(0, os.path.join(os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))),
                                "tools_port"))
from names import orig  # noqa: E402

SCN_CNT_CODE = 0x20
SCN_CNT_INIT = 0x40
SCN_CNT_UNINIT = 0x80
SCN_LNK_COMDAT = 0x1000
CLASS_EXTERNAL = 2
CLASS_STATIC = 3
STATIC_NAME = re.compile(r"^(?:.*\.)?(D_([0-9A-F]{8}))(?:\.\d+)?$|^\?(D_([0-9A-F]{8}))@")
# x86_64 relocation types
REL_ABSOLUTE = 0
REL_ADDR64 = 1
REL_ADDR32 = 2
REL_ADDR32NB = 3
PIN_LIMIT = 0x80800000


class Coff:
    def __init__(self, data):
        self.d = bytearray(data)
        (self.machine, self.nsec, _, self.symptr, self.nsyms, self.opthdr, _) = struct.unpack_from("<HHIIIHH", data, 0)
        if self.machine == 0 and self.nsec == 0xFFFF:
            raise SystemExit("coffpin: bigobj objects are not supported")
        strtab = self.symptr + self.nsyms * 18
        (strsize,) = struct.unpack_from("<I", data, strtab)
        self.strtab = bytearray(data[strtab:strtab + strsize])
        if strtab + strsize != len(data):
            raise SystemExit("coffpin: the symbol and string tables are not at the end of the object")
        self.secs = []
        off = 20 + self.opthdr
        for i in range(self.nsec):
            name, vsz, va, rawsz, rawptr, relptr, lnptr, nrel, nln, ch = struct.unpack_from("<8sIIIIIIHHI", data, off)
            if ch & 0x01000000:     # IMAGE_SCN_LNK_NRELOC_OVFL
                (nrel,) = struct.unpack_from("<I", data, relptr)
                relptr += 10
                nrel -= 1
            self.secs.append({"name": self.name_of(name), "size": rawsz, "ptr": rawptr, "relptr": relptr,
                              "nrel": nrel, "ch": ch, "hdr": off})
            off += 40
        self.syms = []   # [name, value, secno, type, cls, naux, aux bytes]
        i = 0
        while i < self.nsyms:
            raw = data[self.symptr + i * 18:self.symptr + i * 18 + 18]
            name8, value, secno, typ, cls, naux = struct.unpack("<8sIhHBB", raw)
            aux = bytes(data[self.symptr + (i + 1) * 18:self.symptr + (i + 1 + naux) * 18])
            self.syms.append([self.name_of(name8), value, secno, typ, cls, naux, aux, i])
            i += 1 + naux

    def name_of(self, name8):
        if name8[:4] == b"\0\0\0\0":
            (off,) = struct.unpack_from("<I", name8, 4)
            end = self.strtab.index(b"\0", off)
            return self.strtab[off:end].decode()
        if name8[:1] == b"/" and name8[1:2].isdigit():
            off = int(name8[1:].rstrip(b"\0"))
            end = self.strtab.index(b"\0", off)
            return self.strtab[off:end].decode()
        return name8.rstrip(b"\0").decode()

    def relocs(self):
        """(section index, file offset of the reloc's symbol index field, symbol index)"""
        for si, s in enumerate(self.secs):
            for k in range(s["nrel"]):
                p = s["relptr"] + k * 10
                (_, symi, _) = struct.unpack_from("<IIH", self.d, p)
                yield si, p + 4, symi

    def relocs_full(self):
        """(section index, offset in the section, symbol index, type, file offset of the entry)"""
        for si, s in enumerate(self.secs):
            for k in range(s["nrel"]):
                p = s["relptr"] + k * 10
                va, symi, typ = struct.unpack_from("<IIH", self.d, p)
                yield si, va, symi, typ, p

    def encode_name(self, name):
        b = name.encode()
        if len(b) <= 8:
            return b.ljust(8, b"\0")
        off = len(self.strtab)
        self.strtab += b + b"\0"
        return b"\0\0\0\0" + struct.pack("<I", off)

    def write(self):
        out = bytearray(self.d[:self.symptr])
        n = 0
        for name, value, secno, typ, cls, naux, aux, _ in self.syms:
            out += self.encode_name(name) + struct.pack("<IhHBB", value, secno, typ, cls, naux) + aux
            n += 1 + naux
        struct.pack_into("<I", self.strtab, 0, len(self.strtab))
        out += self.strtab
        struct.pack_into("<I", out, 12, n)
        return bytes(out)


def load_addrs(addrs_path):
    addrs = {}
    for line in open(addrs_path):
        p = line.split()
        if len(p) >= 2:
            addrs[p[0]] = int(p[1], 16)
    return addrs


def load_n64data(elfsyms_path):
    """name -> address of the N64 data symbols (gensyms.py inputs: 'name addr func|data size')"""
    out = {}
    for line in open(elfsyms_path):
        p = line.split()
        if len(p) >= 3 and p[2] == "data":
            out.setdefault(p[0], int(p[1], 16))
    return out


def n64_addr(name, n64data):
    """the N64 address of data symbol NAME (None if it isn't one)"""
    if name in n64data:
        return n64data[name]
    m = re.match(r"D_([0-9A-F]{8})$", orig(name))
    if m and 0x80000000 <= int(m.group(1), 16) < PIN_LIMIT:
        return int(m.group(1), 16)
    return None


def is_n64_name(name, n64data):
    return n64_addr(name, n64data) is not None


def pin(addrs, inp, outp, rec_path, base=None, n64data=None):
    addrs = dict(addrs)
    c = Coff(open(inp, "rb").read())
    # symbol table index -> position in c.syms
    by_index = {s[7]: k for k, s in enumerate(c.syms)}
    # objects per section (non-section symbols), to size them and to resolve
    # relocations against section symbols
    per_sec = {}
    for k, s in enumerate(c.syms):
        name, value, secno, typ, cls, naux = s[:6]
        if secno > 0 and not (cls == CLASS_STATIC and naux > 0 and value == 0 and name == c.secs[secno - 1]["name"]):
            if cls in (CLASS_EXTERNAL, CLASS_STATIC) and not name.startswith((".L", "$")):
                per_sec.setdefault(secno, []).append((value, k))
    for v in per_sec.values():
        v.sort()
    pinned = {}     # c.syms position -> (key, kind, size)
    for k, s in enumerate(c.syms):
        name, value, secno, typ, cls, naux = s[:6]
        if secno <= 0 or cls not in (CLASS_EXTERNAL, CLASS_STATIC):
            continue
        sec = c.secs[secno - 1]
        if sec["ch"] & SCN_CNT_CODE:
            continue
        if name == sec["name"] and naux:
            continue     # the section symbol
        if name in addrs and cls == CLASS_EXTERNAL:
            key = name
        else:
            m = STATIC_NAME.match(name)
            if not m:
                continue
            key, hexa = (m.group(1), m.group(2)) if m.group(1) else (m.group(3), m.group(4))
            if not (0x80000000 <= int(hexa, 16) < PIN_LIMIT):
                continue
            if key not in addrs:
                addrs[key] = int(hexa, 16)
        objs = per_sec[secno]
        pos = [o[0] for o in objs].index(value)
        end = objs[pos + 1][0] if pos + 1 < len(objs) else sec["size"]
        kind = "bss" if sec["ch"] & SCN_CNT_UNINIT else "data"
        pinned[k] = (key, kind, end - value, secno, value)
    # relocations: to a pinned symbol, or to the section symbol of a pinned
    # object's section
    sec_sym = {}
    for k, s in enumerate(c.syms):
        name, value, secno, typ, cls, naux = s[:6]
        if secno > 0 and cls == CLASS_STATIC and naux and value == 0 and name == c.secs[secno - 1]["name"]:
            sec_sym[s[7]] = secno
    sec_pinned = {}
    for k, (key, kind, size, secno, value) in pinned.items():
        sec_pinned.setdefault(secno, []).append(k)
    new_syms = {}    # key -> new symbol table index (appended undefined externals)
    next_index = c.nsyms

    def undef_index(key):
        nonlocal next_index
        if key not in new_syms:
            new_syms[key] = next_index
            c.syms.append([key, 0, 0, 0, CLASS_EXTERNAL, 0, b"", next_index])
            next_index += 1
        return new_syms[key]

    recs = []
    for k, (key, kind, size, secno, value) in sorted(pinned.items()):
        s = c.syms[k]
        if s[5]:
            raise SystemExit("coffpin: %s: data symbol %s has auxiliary records" % (inp, s[0]))
        if kind == "data":
            s[0] = "__native_" + key
            s[4] = CLASS_EXTERNAL
            undef_index(key)
        else:
            s[0], s[1], s[2], s[4] = key, 0, 0, CLASS_EXTERNAL
        recs.append("%s %08X %s %d" % (key, addrs[key], kind, size))
    moved = 0
    for si, field, symi in c.relocs():
        k = by_index.get(symi)
        if k is None:
            continue
        if k in pinned:
            key, kind = pinned[k][0], pinned[k][1]
            if kind == "data":
                struct.pack_into("<I", c.d, field, undef_index(key))
                moved += 1
            continue
        if symi in sec_sym and sec_sym[symi] in sec_pinned:
            secno = sec_sym[symi]
            ks = sec_pinned[secno]
            others = [o for o in per_sec.get(secno, []) if o[1] not in ks]
            if len(ks) != 1 or pinned[ks[0]][4] != 0 or others:
                raise SystemExit("coffpin: %s: relocation against section %s, which holds more than one "
                                 "object (build with -fdata-sections)" % (inp, c.secs[secno - 1]["name"]))
            key, kind = pinned[ks[0]][0], pinned[ks[0]][1]
            struct.pack_into("<I", c.d, field, undef_index(key) if kind == "data" else c.syms[ks[0]][7])
            moved += 1
    # duplicate keys (two statics with the same address name) would clash
    keys = [p[0] for p in pinned.values()]
    if len(keys) != len(set(keys)):
        raise SystemExit("coffpin: %s: an N64 address is defined twice: %s" %
                         (inp, sorted(k for k in keys if keys.count(k) > 1)))
    if base is not None:
        # MSVC makes a C tentative definition (`u64 D_x;` in several files) a
        # COMMON symbol (undefined, value = size): the linker would allocate it
        # natively.  An N64 one becomes a plain reference to the address.
        for s in c.syms:
            if s[2] == 0 and s[1] != 0 and s[4] == CLASS_EXTERNAL and \
                    (s[0] in addrs or is_n64_name(s[0], n64data or {})):
                s[1] = 0
        # a pinned .bss variable's COMDAT section has no symbol left: an
        # ordinary empty section
        for k, (key, kind, size, secno, value) in pinned.items():
            sec = c.secs[secno - 1]
            if kind != "bss" or not sec["ch"] & SCN_LNK_COMDAT:
                continue
            if any(p[3] == secno and p[1] == "data" for p in pinned.values()) or \
                    [o for o in per_sec.get(secno, []) if o[1] not in sec_pinned.get(secno, [])]:
                raise SystemExit("coffpin: %s: COMDAT section %d holds more than the pinned %s" % (inp, secno, key))
            sec["ch"] &= ~SCN_LNK_COMDAT
            struct.pack_into("<I", c.d, sec["hdr"] + 16, 0)        # SizeOfRawData
            struct.pack_into("<I", c.d, sec["hdr"] + 36, sec["ch"])
            for s in c.syms:
                if s[2] == secno and s[4] == CLASS_STATIC and s[5] and s[0] == sec["name"]:
                    aux = bytearray(s[6])
                    struct.pack_into("<I", aux, 0, 0)    # Length
                    aux[14] = 0                          # Selection
                    s[6] = bytes(aux)
        # ADDR64/ADDR32 fixups against N64 data symbols: + image base;
        # ADDR32NB: resolved here (see above)
        idx_sym = {s[7]: s for s in c.syms}
        pkeys = set(keys)
        for si, va, symi, typ, rel in c.relocs_full():
            if typ not in (REL_ADDR64, REL_ADDR32, REL_ADDR32NB):
                continue
            s = idx_sym.get(symi)
            if s is None or s[2] != 0:
                continue
            addr = addrs[s[0]] if s[0] in pkeys else n64_addr(s[0], n64data or {})
            if addr is None:
                continue
            sec = c.secs[si]
            if sec["ch"] & SCN_CNT_UNINIT:
                continue
            p = sec["ptr"] + va
            if typ == REL_ADDR64:
                (v,) = struct.unpack_from("<Q", c.d, p)
                struct.pack_into("<Q", c.d, p, (v + base) & 0xFFFFFFFFFFFFFFFF)
            elif typ == REL_ADDR32:
                (v,) = struct.unpack_from("<I", c.d, p)
                struct.pack_into("<I", c.d, p, (v + base) & 0xFFFFFFFF)
            else:
                (v,) = struct.unpack_from("<I", c.d, p)
                struct.pack_into("<I", c.d, p, (v + addr - base) & 0xFFFFFFFF)
                struct.pack_into("<H", c.d, rel + 8, REL_ABSOLUTE)
    open(outp, "wb").write(c.write())
    with open(rec_path, "w") as f:
        f.write("".join(r + "\n" for r in recs))


def main():
    args = sys.argv[1:]
    base = elfsyms = None
    while args and args[0].startswith("--") and args[0] != "--batch":
        if args[0] == "--base":
            base = int(args[1], 16)
        elif args[0] == "--elfsyms":
            elfsyms = args[1]
        else:
            raise SystemExit("coffpin: unknown option " + args[0])
        args = args[2:]
    n64data = load_n64data(elfsyms) if elfsyms else set()
    if args and args[0] == "--batch":
        addrs_path, outdir, listfile = args[1:4]
        addrs = load_addrs(addrs_path)
        os.makedirs(outdir, exist_ok=True)
        seen = set()
        for line in open(listfile):
            obj = line.strip()
            if not obj:
                continue
            stem = os.path.basename(obj).split(".")[0]
            if stem in seen:
                raise SystemExit("coffpin: two objects named %s in %s" % (stem, listfile))
            seen.add(stem)
            pin(addrs, obj, os.path.join(outdir, stem + ".obj"), os.path.join(outdir, stem + ".rec"), base, n64data)
        return
    addrs_path, inp, outp, rec_path = args[0:4]
    pin(load_addrs(addrs_path), inp, outp, rec_path, base, n64data)


if __name__ == "__main__":
    main()
