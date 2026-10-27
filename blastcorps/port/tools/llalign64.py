#!/usr/bin/env python3
"""Clamp the alignment of every memory access in an LLVM IR module to 4 bytes.

The x86-64 ABI gives a global array of 16 bytes or more 16-byte alignment and
clang assumes it for every array it sees, extern ones included; it then uses
aligned SSE accesses (movaps) on them.  N64 data sits at N64 addresses with
N64 alignment (gCamPathBasis is 8-aligned): such an access faults.  No clang
option turns the assumption off, and the attribute that would (aligned on
each declaration) can't be applied by #pragma clang attribute, so the 64-bit
build compiles the game code to optimised IR, lowers every `align N` above 4
on globals and memory operations (not on stack slots: allocas stay aligned),
and only then generates code (port64.mk).  Lowering an alignment is always
correct; it only costs aligned-load forms the game code hardly uses.

usage: llalign64.py IN.ll OUT.ll [ADDRS]
  ADDRS: the pinned symbols (gensyms.py addrs), whose definitions are lowered too
"""
import os
import re
import sys

# renamed symbols (tools_port/names.txt) count as the address names they had
sys.path.insert(0, os.path.join(os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))),
                                "tools_port"))
from names import orig  # noqa: E402

ALIGN = re.compile(r"\balign (\d+)\b")


def clamp(m):
    n = int(m.group(1))
    return "align %d" % min(n, 4)


def main():
    src, dst = sys.argv[1:3]
    global pinned
    pinned = set()
    if len(sys.argv) > 3:
        for line in open(sys.argv[3]):
            p = line.split()
            if p:
                pinned.add(p[0])
    n = 0
    out = []
    for line in open(src):
        s = line.lstrip()
        # stack slots keep their alignment; metadata and attribute groups
        # (alignstack, !...) are left alone
        if " = alloca " in line or s.startswith(("!", "attributes ", ";", "declare ")) or "alignstack" in line:
            out.append(line)
            continue
        # a global defined here keeps its alignment (host code may rely on it),
        # unless it is N64 data (pinned: coffpin moves it to its N64 address;
        # codegen infers an access's alignment from its global too)
        g = re.match(r"^@([\w.$]+) = ", line)
        if g and " external " not in line.split("=", 1)[-1][:40] and not (
                g.group(1) in pinned or re.search(r"(^|\.)D_[0-9A-F]{8}($|\.)", orig(g.group(1)))):
            out.append(line)
            continue
        new, k = ALIGN.subn(lambda m: clamp(m), line)
        if k and new != line:
            n += 1
        out.append(new)
    open(dst, "w").write("".join(out))


if __name__ == "__main__":
    main()
