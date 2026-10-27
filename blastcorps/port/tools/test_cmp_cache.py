#!/usr/bin/env python3
"""Tests for the comparator cache's folder names and locks (tools/compare.py,
tools/cmp_cache_clean.py): no emulator, no exe; temporary folders only.

usage: python3 tools/test_cmp_cache.py
"""
import os
import shutil
import subprocess
import sys
import tempfile
import threading
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import compare  # noqa: E402

fails = []


def check(cond, what):
    print("%s %s" % ("ok  " if cond else "FAIL", what))
    if not cond:
        fails.append(what)


def main():
    tmp = tempfile.mkdtemp(prefix="cmpcache_")
    cache = os.path.join(tmp, "cache")
    os.makedirs(cache)
    rom_a, rom_b = os.path.join(tmp, "a.z64"), os.path.join(tmp, "b.z64")
    open(rom_a, "wb").write(b"\x80\x37\x12\x40" + b"A" * 4096)
    open(rom_b, "wb").write(b"\x80\x37\x12\x40" + b"B" * 4096)
    ha, hb = compare.rom_sha1(rom_a), compare.rom_sha1(rom_b)

    # names: the ROM's hash in every emulator folder, the exe in the native ones
    os.environ["CMP_TAG"] = "msvc"
    os.environ["CMP_EXE"] = "/x/wt1/Release/bc_headless.exe"
    ea, na = compare.level_dirs(cache, "nm", 7, rom_a)
    eb, nb = compare.level_dirs(cache, "nm", 7, rom_b)
    check(os.path.basename(ea) == "level-nm-07-" + ha[:8], "level folder named by ROM hash (%s)" % os.path.basename(ea))
    check(ea != eb and na != nb, "different ROMs -> different emulator and native folders")
    check(na.startswith(ea + "-nativemsvc-"), "native folder = emulator folder + -nativemsvc-EXEHASH (%s)"
          % os.path.basename(na))
    os.environ["CMP_EXE"] = "/x/wt2/Release/bc_headless.exe"
    ea2, na2 = compare.level_dirs(cache, "nm", 7, rom_a)
    check(ea2 == ea and na2 != na, "same ROM, other exe -> shared emulator folder, own native folder")
    old = os.path.join(cache, "level-nm-07")
    check(ea != old and not na.startswith(old + "-native"), "never the old (hashless) names")

    # staleness: rom.sha1 by content, spec.sha1 by content
    os.makedirs(ea)
    open(os.path.join(ea, "emu.txt"), "w").write("x\n")
    open(os.path.join(ea, "boot.txt"), "w").write("1 2 3\n")
    open(os.path.join(ea, "rom.sha1"), "w").write(ha + "\n")
    open(os.path.join(ea, "spec.sha1"), "w").write(compare.spec_sha1() + "\n")
    check(not compare.emu_stale(ea, rom_a), "folder with matching rom.sha1 + spec.sha1 is reused")
    check(compare.emu_stale(ea, rom_b), "folder whose rom.sha1 differs is not reused")
    open(os.path.join(ea, "spec.sha1"), "w").write("0" * 40 + "\n")
    check(compare.emu_stale(ea, rom_a), "folder whose spec.sha1 differs is redone")

    # elfs.txt: a run recorded by another (since removed) worktree is used with
    # THIS checkout's ELFs; new runs store paths relative to ROOT
    root2 = os.path.join(tmp, "wt_other", "blastcorps")
    os.makedirs(os.path.join(root2, "build_nm"))
    nm_elfs = ["build_nm/hd_code.rom.us.v11.elf", "build_nm/hd_front_end.rom.us.v11.elf"]
    for p in nm_elfs:
        open(os.path.join(root2, p), "wb").write(b"\x7fELF")
    nm_rom = os.path.join(root2, "build_nm/blastcorps.nm.us.v11.z64")
    shutil.copy(rom_a, nm_rom)
    saved = compare.ROOT, compare.default_rom
    compare.ROOT = root2
    compare.default_rom = lambda kind: nm_rom
    open(os.path.join(ea, "elfs.txt"), "w").write(
        "".join("/home/x/wt/gone/blastcorps/%s\n" % p for p in nm_elfs))
    want = [os.path.join(root2, p) for p in nm_elfs]
    check(compare.cached_elfs(ea) == want, "old absolute elfs.txt of a removed worktree -> this ROOT's ELFs")
    check("build_nm" in compare.cached_elfs(ea)[0], "NM run still recognised (build_nm in the path)")
    open(os.path.join(ea, "elfs.txt"), "w").write("\n".join(compare.rom_elfs("nm")) + "\n")
    check(not any(os.path.isabs(p) for p in open(os.path.join(ea, "elfs.txt")).read().split()),
          "new elfs.txt entries are relative")
    check(compare.cached_elfs(ea) == want, "relative elfs.txt -> this ROOT's ELFs")
    # this checkout's NM ROM is not the folder's: a clear error, not wrong symbols
    shutil.copy(rom_b, nm_rom)
    os.utime(nm_rom, (1, 1))
    err = subprocess.run([sys.executable, "-c",
                          "import sys; sys.path.insert(0,%r); import compare\n"
                          "compare.ROOT=%r; compare.default_rom=lambda k: %r\n"
                          "compare.cached_elfs(%r)" % (HERE, root2, nm_rom, ea)],
                         capture_output=True, text=True)
    check(err.returncode == 1 and "rebuild build_nm" in err.stderr and "nmrom" in err.stderr,
          "ROM hash mismatch -> 'rebuild build_nm' error (%s)" % err.stderr.strip()[:90])
    compare.ROOT, compare.default_rom = saved
    os.remove(os.path.join(ea, "elfs.txt"))

    # the lock: a second user of the same folder waits for the first
    t0 = time.time()
    holder = subprocess.Popen([sys.executable, "-c",
                               "import sys,time; sys.path.insert(0,%r); import compare\n"
                               "with compare.CacheLock(%r):\n print('held', flush=True); time.sleep(2)" % (HERE, ea)],
                              stdout=subprocess.PIPE, text=True)
    check(holder.stdout.readline().strip() == "held", "lock taken by another process")
    with compare.CacheLock(ea):
        waited = time.time() - t0
    holder.wait()
    check(waited >= 1.5, "second run waited for the lock (%.1f s)" % waited)
    t0 = time.time()
    with compare.CacheLock(eb):
        pass
    check(time.time() - t0 < 0.5, "other ROM's folder: no wait")

    # run_level: two ROMs at once write only their own folders (runs faked)
    written = {}

    def fake_locked(level, frames, every, input_path, kind, shots, force, keep, strict, rom, emudir, natdir):
        written.setdefault(rom, []).append((emudir, natdir))
        os.makedirs(natdir, exist_ok=True)
        time.sleep(0.2)
        return {"level": level}

    compare.run_level_locked = fake_locked
    os.makedirs(old)
    open(os.path.join(old, "rom.sha1"), "w").write("990290a8" + "0" * 32 + "\n")
    # (run_level takes the ROM from default_rom: each thread its own)
    tl = threading.local()
    compare.default_rom = lambda kind: tl.rom

    def job(r):
        tl.rom = r
        compare.run_level(3, cache, 10, 10, r, "nm")

    th = [threading.Thread(target=job, args=(r,)) for r in (rom_a, rom_b)]
    for t in th:
        t.start()
    for t in th:
        t.join()
    ok = all(written.get(r) and written[r][0][0].endswith(compare.rom_sha1(r)[:8]) for r in (rom_a, rom_b))
    check(ok, "concurrent run_level with two ROMs: folders %s" %
          sorted(os.path.basename(d) for v in written.values() for d, _ in v))
    check(open(os.path.join(old, "rom.sha1")).read().startswith("990290a8") and len(os.listdir(old)) == 1,
          "old folder untouched")

    # cmp_cache_clean: dry run lists, deletes nothing; --keep keeps
    open(os.path.join(cache, "level-07.log"), "w").write("x")
    out = subprocess.run([sys.executable, os.path.join(HERE, "cmp_cache_clean.py"), "--cache", cache,
                          "--keep", ha, "--old"], capture_output=True, text=True).stdout
    listed = [line.split()[1] for line in out.splitlines() if line.startswith("stale ")]
    check(os.path.basename(eb) + ".lock" in listed and os.path.basename(ea) not in listed,
          "clean: other ROM's files listed, kept ROM's not (%s)" % listed)
    check("level-nm-07" in listed and "level-07.log" in listed, "clean --old: old names listed")
    check(os.path.isdir(old) and os.path.isdir(written[rom_b][0][1]), "clean dry run removed nothing")

    shutil.rmtree(tmp)
    print("%d failed" % len(fails) if fails else "all passed")
    sys.exit(1 if fails else 0)


if __name__ == "__main__":
    main()
