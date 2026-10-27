#!/usr/bin/env python3
"""Check the player package (make_dist.py's zips) before it is published.

  check_dist.py DIST_DIR

DIST_DIR holds BlastCorps-port-V.zip and BlastCorps-port-V-pdb.zip.  Fails
unless each zip holds exactly the files make_dist.py puts in it (no game data,
no save or config files, nothing left over from a run) and no member starts
like an N64 ROM image in any byte order.  The CI (.github/workflows/ci.yml)
runs it before uploading the zips.
"""
import glob
import os
import sys
import zipfile

DIST = {"bc.exe", "bc_packtool.exe", "SDL2.dll", "dxcompiler.dll", "dxil.dll", "README.txt", "LICENSE.txt",
        "THIRD_PARTY_LICENSES.txt"}
PDB = {"bc.pdb", "bc.map", "bc.syms"}
# the first word of a ROM: .z64 (big-endian), .v64 (byte pairs swapped), .n64 (little-endian words)
ROM_MAGIC = (b"\x80\x37\x12\x40", b"\x37\x80\x40\x12", b"\x40\x12\x37\x80")


def check(path, want, prefix):
    bad = []
    with zipfile.ZipFile(path) as z:
        names = z.namelist()
        got = set()
        for n in names:
            if prefix and not n.startswith(prefix):
                bad.append("%s: outside %s" % (n, prefix))
                continue
            got.add(n[len(prefix):])
            with z.open(n) as f:
                if f.read(4) in ROM_MAGIC:
                    bad.append("%s: starts like an N64 ROM" % n)
        if len(names) != len(got):
            bad.append("duplicate members")
        if got != want:
            bad.append("members %s, expected %s" % (sorted(got), sorted(want)))
        size = sum(i.file_size for i in z.infolist())
    print("%s: %d files, %d bytes unpacked: %s" % (os.path.basename(path), len(names), size,
                                                   "OK" if not bad else "FAILED"))
    for b in bad:
        print("  " + b)
    return not bad


def main():
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    d = sys.argv[1]
    pdb = glob.glob(os.path.join(d, "BlastCorps-port-*-pdb.zip"))
    dist = [z for z in glob.glob(os.path.join(d, "BlastCorps-port-*.zip")) if z not in pdb]
    if len(dist) != 1 or len(pdb) != 1:
        sys.exit("check_dist: expected one package zip and one -pdb zip in %s, found %s" % (d, dist + pdb))
    name = os.path.basename(dist[0])[:-4]
    ok = check(dist[0], DIST, name + "/")
    ok = check(pdb[0], PDB, "") and ok
    print("check_dist: " + ("OK" if ok else "FAILED"))
    sys.exit(0 if ok else 1)


if __name__ == "__main__":
    main()
