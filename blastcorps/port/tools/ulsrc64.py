#!/usr/bin/env python3
"""ultralib sources for the 64-bit build: function pointers kept in N64 memory.

libaudio keeps function pointers in records that live in RDRAM (ALPlayer's voice
handler, the filters' pull/param handlers, the synthesizer's and the loader's
DMA callbacks, the sequence players' oscillator hooks).  In the 64-bit build
those fields are 4-byte code addresses, N64FN(T) = a u32 (ulhdr64.py; clang
has no working __ptr32 function pointer), so every use goes through
N64FN_SET(field, value) / N64FN_GET(T, field).  This rewrites the sources
mechanically; the repo's ul_*.c and the ultralib submodule are not edited.

usage: ulsrc64.py UL_DIR OUTINC OUTSRC UL_FILE.c...
  A 2-line wrapper (`#include "src/audio/x.c"`) is copied to OUTSRC as is and the
  ultralib file it includes, if it needs rewriting, goes to OUTINC/src/audio/x.c
  (OUTINC is searched before UL_DIR); a full copy is rewritten into OUTSRC.
"""
import os
import re
import sys

FIELDS = {"handler", "setParam", "paramHdl", "initOsc", "updateOsc", "stopOsc", "dma"}
# base expression of a member access: identifiers, [..], -> and . chains
BASE = r"[A-Za-z_]\w*(?:\[[^\]]*\])?(?:(?:->|\.)[A-Za-z_]\w*(?:\[[^\]]*\])?)*"
ACCESS = re.compile(r"(?<![\w>.])(" + BASE + r")(->|\.)(" + "|".join(sorted(FIELDS)) + r")\b")


def fn_type(base, field):
    if field == "handler":
        return "ALVoiceHandler" if re.search(r"\b(client|node)\b", base) else "ALCmdHandler"
    if field == "dma":
        return "ALDMANew" if re.search(r"\bdrvr\b", base) else "ALDMAproc"
    return {"setParam": "ALSetParam", "paramHdl": "ALSetFXParam", "initOsc": "ALOscInit",
            "updateOsc": "ALOscUpdate", "stopOsc": "ALOscStop"}[field]


def is_config(base, field):
    # ALSeqpConfig / ALSynConfig fields are plain (N64P data) pointers
    return field in ("initOsc", "updateOsc", "stopOsc") and re.fullmatch(r"c", base) is not None


def rewrite_code(code):
    """rewrite one comment-free code segment"""
    out, pos, n = [], 0, 0
    for m in ACCESS.finditer(code):
        base, op, field = m.group(1), m.group(2), m.group(3)
        if is_config(base, field):
            continue
        out.append(code[pos:m.start()])
        expr = base + op + field
        rest = code[m.end():]
        a = re.match(r"\s*=(?!=)", rest)
        if a:
            semi = rest.find(";", a.end())
            rhs = rest[a.end():semi].strip()
            # the right-hand side may itself read an N64FN field
            rhs = rewrite_code(rhs)[0]
            out.append("N64FN_SET(%s, %s)" % (expr, rhs))
            pos = m.end() + semi
        else:
            out.append("N64FN_GET(%s, %s)" % (fn_type(base, field), expr))
            pos = m.end()
        n += 1
    out.append(code[pos:])
    return "".join(out), n


def rewrite(text, name=""):
    """skip comments and strings; rewrite the code between them"""
    out, i, total = [], 0, 0
    for a, b in TEXT_SUBS + FILE_SUBS.get(name, []):
        text, k = re.subn(a, b, text)
        total += k
    tok = re.compile(r'/\*.*?\*/|//[^\n]*|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'', re.S)
    for m in tok.finditer(text):
        seg, k = rewrite_code(text[i:m.start()])
        out.append(seg)
        total += k
        out.append(m.group(0))
        i = m.end()
    seg, k = rewrite_code(text[i:])
    out.append(seg)
    return "".join(out), total + k


# the other N64-pointer spots of these sources (their own declarations and casts)
TEXT_SUBS = [
    (r"\b(ALGlobals\s*)\*(\s*alGlobals\b)", r"\1* N64P \2"),
    (r"\(ALFilter\s*\*\*\)", r"(ALFilter * N64P *)"),
    (r"\b(ALFilter\s*)\*\*(\s*sources\b)", r"\1* N64P *\2"),
    # (the bus source tables in the heap hold 4-byte pointers)
    (r"sizeof\s*\(\s*ALFilter\s*\*\s*\)", r"sizeof(ALFilter * N64P)"),
    # (an event posted from a local: alEvtqPostEvent copies the whole record into the queue in the audio heap,
    # with the bytes its type doesn't use (the union's rest, the padding after `type`); from a host stack those
    # would differ from run to run (the N64's MIPS stack had its own leftovers there); nothing reads them)
    (r"(?m)^(\s+)(ALEvent|ALSndpEvent)(\s+)(evt)\s*;", r"\1\2\3\4 = {0};"),
]
# per file: the bank/sequence file loaders turn file offsets into pointers by
# adding the file's address held in an s32: on x86_64, pointer + s32
# sign-extends an address >= 0x80000000 (u32 adds it as the N64 does)
FILE_SUBS = {
    "bnkf.c": [(r"\bs32(\s+)(offset|woffset|table)\b", r"u32\1\2")],
}


def main():
    ul, outinc, outsrc = sys.argv[1:4]
    os.makedirs(outsrc, exist_ok=True)
    total = 0
    paths = []
    for a in sys.argv[4:]:
        # @FILE: the files listed in FILE, one per line (the CMake build)
        paths += [l.strip() for l in open(a[1:]) if l.strip()] if a.startswith("@") else [a]
    for path in paths:
        text = open(path, newline="").read()
        m = re.search(r'^#include "(src/[\w/]+\.c)"\s*$', text, re.M)
        if m and len(text.strip().split("\n")) <= 3:
            src = open(os.path.join(ul, m.group(1)), newline="").read()
            new, k = rewrite(src, os.path.basename(m.group(1)))
            # copied even when unchanged: its quoted includes of its neighbours
            # ("synthInternals.h") must find the marked copies next to it
            dst = os.path.join(outinc, m.group(1))
            os.makedirs(os.path.dirname(dst), exist_ok=True)
            open(dst, "w", newline="").write(new)
            out = text
        else:
            out, k = rewrite(text, os.path.basename(path).replace("ul_", "", 1))
        total += k
        open(os.path.join(outsrc, os.path.basename(path)), "w", newline="").write(out)
    print("ulsrc64: %d function-pointer uses rewritten in %d files" % (total, len(paths)))


if __name__ == "__main__":
    main()
