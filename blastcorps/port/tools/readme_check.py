#!/usr/bin/env python3
"""The docs against the code (python3 port/tools/readme_check.py [PORT_DIR]; exit 1 on a gap):

  - every bc.ini key of the defaults text (src/live/live_setup.cpp kDefaultIni) is named in port/README.md and
    in dist/README.txt (the player's), in full or in a shared form such as sensitivity_x/y or
    camera_left/right/up/down; the per-button [keyboard]/[controller] bindings count as named when the README
    says "one line per N64 button";
  - every option bc.exe's extraArg takes (src/live/live_start.cpp; --set's short forms included) is in
    live_rt64.cpp's header comment, and every option that comment lists is taken;
  - every option bc_headless's host_main takes (src/platform/headless_main.c) is in that file's header comment
    and its usage text, and the other way round.
Standard library only."""
import os
import re
import sys

port = sys.argv[1] if len(sys.argv) > 1 else os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")


def read(rel):
    with open(os.path.join(port, rel), encoding="utf-8") as f:
        return f.read()


problems = []

# ---- bc.ini keys ---------------------------------------------------------------------------------------
src = read("src/live/live_setup.cpp")
m = re.search(r'kDefaultIni\[\] =\n(.*?);\n', src, re.S)
text = "".join(re.findall(r'"((?:[^"\\]|\\.)*)"', m.group(1))).replace("\\r\\n", "\n").replace('\\"', '"')
keys, sec = [], None
for line in text.split("\n"):
    line = line.strip()
    if not line or line.startswith(";"):
        continue
    if line.startswith("["):
        sec = line[1:-1]
        continue
    keys.append((sec, line.split("=")[0].strip()))
BUTTONS = {"a", "b", "z", "start", "l", "r", "c_up", "c_down", "c_left", "c_right", "d_up", "d_down", "d_left",
           "d_right", "stick_up", "stick_down", "stick_left", "stick_right"}


def named(doc, key):
    if re.search(r'(?<![A-Za-z_])%s(?![A-Za-z_])' % re.escape(key), doc):
        return True
    # a shared form: prefix_a/b(/c...) names prefix_a, prefix_b ...
    for mm in re.finditer(r'\b([a-z_]+_)([a-z]+(?:/[a-z]+)+)\b', doc):
        if key in (mm.group(1) + w for w in mm.group(2).split("/")):
            return True
    return False


for doc_name in ("README.md", "dist/README.txt"):
    doc = read(doc_name)
    for sec, key in keys:
        if sec in ("keyboard", "controller") and key in BUTTONS and "one line per N64 button" in doc:
            continue
        if not named(doc, key):
            problems.append("%s doesn't name the bc.ini key [%s] %s" % (doc_name, sec, key))
print("bc.ini: %d keys in the defaults" % len(keys))

# ---- bc.exe's options ------------------------------------------------------------------------------------
start = read("src/live/live_start.cpp")
rt = read("src/live/live_rt64.cpp")
taken = set(re.findall(r'!strcmp\(a, "(--[a-z0-9-]+)"\)', start)) | set(re.findall(r'\{"(--[a-z0-9-]+)", "', start))
taken |= {"--low-test"}   # (main reads it before anything else)
header = rt[:rt.index("*/")]
listed = set(re.findall(r'(--[a-z][a-z0-9-]+)', header))
hl = read("src/platform/headless_main.c")
hl_header = hl[:hl.index("*/")]
hl_taken = set(re.findall(r'!strcmp\(a, "(--[a-z0-9-]+)"\)', hl))
hl_listed = set(re.findall(r'(--[a-z][a-z0-9-]+)', hl_header))
usage = hl[hl.index("static void usage"):hl.index("static unsigned num")]
hl_usage = set(re.findall(r'(--[a-z][a-z0-9-]+)', usage))
for o in sorted(taken - listed - hl_listed):
    problems.append("bc.exe takes %s, but live_rt64.cpp's header comment doesn't list it" % o)
for o in sorted(listed - taken - hl_taken):
    problems.append("live_rt64.cpp's header comment lists %s, which neither exe takes" % o)
for o in sorted(hl_taken - hl_listed):
    problems.append("bc_headless takes %s, but headless_main.c's header comment doesn't list it" % o)
for o in sorted(hl_listed - hl_taken):
    problems.append("headless_main.c's header comment lists %s, which it doesn't take" % o)
for o in sorted(hl_taken - hl_usage):
    problems.append("bc_headless takes %s, but its usage text doesn't show it" % o)
print("options: bc.exe %d, bc_headless %d" % (len(taken), len(hl_taken)))

for p in problems:
    print("  " + p)
print("readme_check: %s" % ("OK" if not problems else "%d problems" % len(problems)))
sys.exit(1 if problems else 0)
