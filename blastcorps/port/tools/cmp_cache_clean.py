#!/usr/bin/env python3
"""List (and with --delete remove) cache folders no current NM ROM needs.

usage: cmp_cache_clean.py [--cache DIR] [--keep HASH ...] [--old] [--delete]

The comparator's cache (tools/compare.py; default ~/cmp_cache) names each
emulator run by its run and the short hash of its ROM ("level-nm-07-990290a8",
native counterparts "...-nativemsvc-1a2b3c", logs "....log", locks "....lock").
A folder is stale when its hash is not one of the kept ROMs: by default the
NM ROMs found in ~/blastcorps/blastcorps/build_nm and ~/wt/*/blastcorps/build_nm, plus
any --keep HASH (8 or more hex digits).  --old also lists the folders and
logs from before the hash scheme (verify-nm-N, level-nm-LL, level-LL.log ...).
Nothing is removed without --delete, and never a folder whose lock is held.
Old-name folders have no locks (the old compare.py took none): don't --old
--delete while a compare.py from before the hash scheme may still be running.
"""
import fcntl
import glob
import hashlib
import os
import re
import shutil
import sys

NEW = re.compile(r"^(verify-nm-\d+|level-[a-z]+-\d\d|attract-[a-z]+-demo\d+)-([0-9a-f]{8})(-native.*)?$")
OLD = re.compile(r"^(verify-nm-\d+|level-[a-z]+-\d\d|attract-[a-z]+-demo\d+)(-native[a-z0-9]+)?$|^level-\d\d\.log$")


def opt_all(args, name):
    out = []
    while name in args:
        i = args.index(name)
        out.append(args[i + 1])
        del args[i:i + 2]
    return out


def locked(path):
    lock = path if path.endswith(".lock") else re.sub(r"(-native[^/]*)?(\.log)?$", "", path) + ".lock"
    if not os.path.exists(lock):
        return False
    with open(lock) as f:
        try:
            fcntl.flock(f, fcntl.LOCK_EX | fcntl.LOCK_NB)
        except OSError:
            return True
        fcntl.flock(f, fcntl.LOCK_UN)
    return False


def main():
    args = sys.argv[1:]
    cache = os.path.expanduser((opt_all(args, "--cache") or ["~/cmp_cache"])[0])
    keep = set(h[:8] for h in opt_all(args, "--keep"))
    old = "--old" in args
    delete = "--delete" in args
    for rom in glob.glob(os.path.expanduser("~/blastcorps/blastcorps/build_nm/*.z64")) + \
            glob.glob(os.path.expanduser("~/wt/*/blastcorps/build_nm/*.z64")):
        keep.add(hashlib.sha1(open(rom, "rb").read()).hexdigest()[:8])
    print("kept ROM hashes: %s" % " ".join(sorted(keep)))
    total = 0
    for name in sorted(os.listdir(cache)):
        path = os.path.join(cache, name)
        base = name[:-4] if name.endswith(".log") else name[:-5] if name.endswith(".lock") else name
        m = NEW.match(base)
        if m:
            if m.group(2) in keep:
                continue
            why = "ROM %s not kept" % m.group(2)
        elif old and OLD.match(name):
            why = "old name (no ROM hash)"
        else:
            continue
        if locked(path):
            print("in use, skipped: %s" % name)
            continue
        size = 0
        if os.path.isdir(path):
            for d, _, fs in os.walk(path):
                size += sum(os.path.getsize(os.path.join(d, f)) for f in fs)
        else:
            size = os.path.getsize(path)
        total += size
        print("%s %-44s %8.1f MB  (%s)" % ("removing" if delete else "stale", name, size / 1e6, why))
        if delete:
            if os.path.isdir(path):
                shutil.rmtree(path)
            else:
                os.remove(path)
    print("%s %.1f MB" % ("removed" if delete else "stale (run with --delete to remove):", total / 1e6))


if __name__ == "__main__":
    main()
