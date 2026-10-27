#!/usr/bin/env python3
"""No 16-byte-aligned memory access to N64 data in the 64-bit build's game code.

The x86-64 ABI gives a global array of 16 bytes or more 16-byte alignment, and
clang assumes it for extern arrays too; N64 data sits at N64 addresses with
N64 alignment (gCamPathBasis is only 8-aligned), so an aligned SSE access to it
faults.  The game code is built without the vectorizers (port64.mk); this
disassembles the game objects and reports every legacy-SSE instruction that
needs a 16-byte-aligned memory operand, unless the operand is on the stack
(%rsp: aligned by the ABI).

usage: align64.py OBJDUMP OBJ...      exit 1 if anything is found
"""
import re
import subprocess
import sys

# legacy SSE forms whose memory operand need not be aligned
UNALIGNED_OK = re.compile(r"^(movups|movupd|movdqu|lddqu|movss|movsd|movd|movq|movlps|movhps|movlpd|movhpd|"
                          r"cvt\w*|ucomis[sd]|comis[sd]|pinsr\w|pextr\w|insertps|extractps|"
                          r"\w+ss|\w+sd|v\w+|movbe|crc32\w*|prefetch\w*)$")
XMM_MEM = re.compile(r"^\s*[0-9a-f]+:\s+(?:[0-9a-f]{2} )+\s*(\w+)\s+(.*)$")


def main():
    objdump, objs = sys.argv[1], sys.argv[2:]
    bad = []
    nm = objdump.replace("objdump", "nm")
    for o in objs:
        r = subprocess.run([objdump, "-dr", "--no-show-raw-insn", o], capture_output=True, text=True)
        # the object's own data (host statics of the platform files: aligned as
        # declared) vs N64 data (undefined here: pinned at its N64 address)
        own = set()
        for line in subprocess.run([nm, o], capture_output=True, text=True).stdout.splitlines():
            p = line.split()
            if len(p) == 3 and p[1] in "bBdDrR":
                own.add(p[2])
        platform = "/plat/" in o or "/bc_plat/" in o
        fn = "?"
        # registers that hold a stack address: MSVC saves xmm6-15 through a copy
        # of rsp (`mov %rsp,%rax` / `lea N(%rsp),%r11`, then movaps to -N(%rax))
        stackreg = set()
        lines = r.stdout.splitlines()
        for i, line in enumerate(lines):
            m = re.match(r"^[0-9a-f]+ <(.*)>:", line)
            if m:
                if not m.group(1).startswith("$"):   # (MSVC's local labels $LNn: not a new function)
                    fn = m.group(1)
                    stackreg = set()
                continue
            m = re.match(r"^\s*[0-9a-f]+:\s+(\w+)\s+(.*)$", line)
            if not m:
                continue
            mn, ops = m.group(1), m.group(2)
            dst = re.search(r",(%r\w+)\s*$", ops.split("#")[0].strip())
            if dst:
                if (mn == "mov" and ops.startswith("%rsp,")) or (mn == "lea" and "(%rsp" in ops):
                    stackreg.add(dst.group(1))
                else:
                    stackreg.discard(dst.group(1))
            if "%xmm" not in ops or "(" not in ops:
                continue
            if UNALIGNED_OK.match(mn):
                continue
            if re.search(r"\(%rsp[,)]", ops) or any(re.search(r"\(%s[,)]" % re.escape(s), ops) for s in stackreg):
                continue
            if "(%rip)" in ops and i + 1 < len(lines):
                # the object's own constant pool or data (aligned by the compiler)
                rel = re.search(r"(?:R_X86_64_|IMAGE_REL_AMD64_)\w+\s+(\S+)", lines[i + 1])
                if rel and (rel.group(1).startswith((".rdata", "__xmm@", ".LCPI", "__real@", ".data", ".bss"))
                            or rel.group(1) in own):
                    continue
            elif platform:
                # the platform files' register-based accesses are to their own
                # host tables (the game files have none: llalign64.py)
                continue
            bad.append("%s: %s: %s %s %s" % (o.split("/")[-1], fn, mn, ops.split("#")[0].strip(),
                                             lines[i + 1].strip() if i + 1 < len(lines) else ""))
    for b in bad:
        print("align64: " + b)
    print("align64: %d objects, %d aligned SSE accesses off the stack" % (len(objs), len(bad)))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
