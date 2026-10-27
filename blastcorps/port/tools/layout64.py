#!/usr/bin/env python3
"""Record layouts of the 64-bit build against the 32-bit one (port/README.md, "64-bit").

Every struct/union a translation unit lays out, compiled by clang for i686
(the 32-bit build's ABI: -malign-double -mno-ms-bitfields, 4-byte pointers =
the N64's layouts) and for x86_64 (N64P pointers): sizes, alignments and
field offsets must be the same, or the record's N64 memory would differ.
Host-only records (the platform's own bookkeeping, register-result structs
passed by value) are listed in ALLOW as 'LAYOUT name' with a reason.

usage: layout64.py ALLOW OUT -- CLANG64_CMD... -- FILE.c...
  CLANG64_CMD is the x86_64 compile command (with --target=x86_64-w64-mingw32);
  the i686 one is derived from it.
"""
import fnmatch
import re
import subprocess
import sys


def layouts(cmd, f):
    r = subprocess.run(cmd + ["-fsyntax-only", "-w", "-Xclang", "-fdump-record-layouts-simple", f],
                       capture_output=True, text=True, errors="replace")
    if r.returncode != 0:
        sys.stderr.write(r.stderr[-2000:])
        raise SystemExit("layout64: clang failed on %s" % f)
    out = []
    name = None
    for line in r.stdout.splitlines():
        m = re.match(r"^Type: (.*)$", line)
        if m:
            name = re.sub(r"\(unnamed at [^)]*/([^/)]+)\)", r"(unnamed at \1)", m.group(1))
            cur = {"name": name}
            out.append(cur)
            continue
        m = re.match(r"^\s+(Size|Alignment):(\d+)", line)
        if m and name:
            cur[m.group(1)] = int(m.group(2))
        m = re.match(r"^\s+FieldOffsets: \[(.*)\]>?", line)
        if m and name:
            cur["offsets"] = m.group(1)
    return out


def main():
    a = sys.argv[1:]
    allow_path, out_path = a[0], a[1]
    i = a.index("--", 2)
    j = a.index("--", i + 1)
    cmd64, files = a[i + 1:j], a[j + 1:]
    cmd32 = [("--target=i686-w64-mingw32" if c.startswith("--target=") else c) for c in cmd64]
    cmd32 += ["-malign-double", "-mno-ms-bitfields"]
    allow = set()
    try:
        for line in open(allow_path):
            line = line.split("#")[0].strip()
            if line:
                allow.add(line)
    except FileNotFoundError:
        pass
    diffs = {}
    nrec = 0
    for f in files:
        l32, l64 = layouts(cmd32, f), layouts(cmd64, f)
        # pair the records by name (and occurrence): a record only one of the
        # two compiles lays out (host code under #ifdef _WIN64) is skipped
        def keyed(ls):
            seen, out = {}, {}
            for r in ls:
                n = seen.get(r["name"], 0)
                seen[r["name"]] = n + 1
                out[(r["name"], n)] = r
            return out
        k32, k64 = keyed(l32), keyed(l64)
        for key in sorted(set(k32) & set(k64)):
            r32, r64 = k32[key], k64[key]
            nrec += 1
            k = (r32.get("Size"), r32.get("offsets"))
            if k != (r64.get("Size"), r64.get("offsets")) or r32.get("Alignment") != r64.get("Alignment"):
                key = "LAYOUT %s" % r32["name"]
                diffs.setdefault(key, "i686 size %s align %s offsets [%s] / x86_64 size %s align %s offsets [%s] (%s)" % (
                    r32.get("Size"), r32.get("Alignment"), r32.get("offsets"), r64.get("Size"), r64.get("Alignment"),
                    r64.get("offsets"), f.split("/")[-1]))
    bad = 0
    with open(out_path, "w") as o:
        for k in sorted(diffs):
            ok = any(fnmatch.fnmatchcase(k, p) for p in allow)
            bad += not ok
            o.write("%s%s\t%s\n" % ("allowed " if ok else "", k, diffs[k]))
    print("layout64: %d record layouts compared in %d files, %d differ (%d not allowed) (%s)" %
          (nrec, len(files), len(diffs), bad, out_path))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
