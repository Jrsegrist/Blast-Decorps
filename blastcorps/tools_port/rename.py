#!/usr/bin/env python3
"""Apply the rename map tools_port/names.txt to the whole tree.

    python3 tools_port/rename.py            rename (then: make extract, build, make -C port inputs)
    python3 tools_port/rename.py --dry-run  what would change, per file
    python3 tools_port/rename.py --check    list every old name still in the tree (exit 1 if any)
    python3 tools_port/rename.py --which NAME   old <-> new for one symbol
    python3 tools_port/rename.py FILE...    only these files (e.g. a branch's new files)

names.txt has one `old = new` per line (`#` starts a comment); old is the
address name (func_XXXXXXXX, D_XXXXXXXX).  The map is the alias table: an old
name in a note or an old report resolves through it (--which), and the address
stays in symbol_addrs.  Re-runnable on any branch: a name that is already
renamed is left alone, so another branch applies the same map before a merge
(see the README, "Naming conventions").

What it rewrites, whole-word (so func_80244930_fp or D_80364A94 are other
names), in every tracked text file of the repository except tools/, lib/,
blastcorps/asm/ (generated: `make extract` regenerates it), port/inputs/
(generated: `make -C port inputs`), names.txt and the files that spell out
example names on purpose (this file, names.py, tests/test_rename.py):
  * C sources and headers, symbol_addrs*.txt, undefined_syms*.txt, the yaml
    and linker scripts, the Makefiles;
  * tools_port (checks/*.txt, conventions.txt, hand_asm.txt, models, the
    Python tools) and port/ (tools such as cmp_spec.py, data/*.txt, the host
    code), READMEs and notes;
  * `LEGACY_<old>` macro names become `LEGACY_<new>`;
  * a `#define new new` line left behind (a file's alias macro for the old
    name, e.g. `#define levelno D_802E8BDC` once D_802E8BDC is levelno) is
    removed.
Then each renamed symbol gets a `new = 0xADDR;` line in its segment's
symbol_addrs file (unless it has one), so splat names it in the asm it
generates and in its undefined_syms_auto files.

Struct fields are renamed by hand (the compiler reports a use that was
missed); names.txt lists them as `field Type.old = new` lines for reference.

A name that is replaced later (or a symbol whose name isn't an address name,
like func_8029AA10_fp) gets a `renamed previous = new` line: edit the address
line to the new name and add the `renamed` line; rename.py then rewrites the
previous name everywhere, and --check reports it, like an address name.
"""
import argparse
import os
import re
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
BC = os.path.dirname(HERE)                  # blastcorps/
sys.path.insert(0, HERE)
import names as namesmod                    # noqa: E402

EXCLUDE_PREFIXES = ("tools/", "lib/", "blastcorps/asm/", "blastcorps/port/inputs/", "blastcorps/expected/",
                    "blastcorps/port/rt64/", "blastcorps/nonmatchings/")
# (the map itself, and the tools and tests that spell out example names)
EXCLUDE_FILES = ("blastcorps/tools_port/names.txt", "blastcorps/tools_port/rename.py",
                 "blastcorps/tools_port/names.py", "blastcorps/tools_port/tests/test_rename.py")
BINARY_EXT = (".png", ".bin", ".z64", ".n64", ".v64", ".ico", ".o", ".obj", ".zip", ".rtz", ".jpg", ".gif",
              ".pdf", ".exe", ".dll", ".lib", ".pdb")
# which symbol_addrs file names an address
SEGMENTS = [(0x801E7000, 0x8021ED00, "symbol_addrs.hd_front_end.us.v11.txt"),
            (0x8021ED00, 0x802447C0, "symbol_addrs.init.us.v11.txt")]
DEFAULT_SEGMENT = "symbol_addrs.hd_code.us.v11.txt"
SYMADDR_HEADER = "// tools_port/names.txt (tools_port/rename.py): renamed symbols at their addresses"


def repo_root():
    r = subprocess.run(["git", "rev-parse", "--show-toplevel"], cwd=BC, capture_output=True, text=True)
    if r.returncode:
        sys.exit("rename.py: not in a git checkout")
    return r.stdout.strip()


def tracked_files(root):
    out = subprocess.run(["git", "ls-files", "-z"], cwd=root, capture_output=True, check=True).stdout
    files = []
    for rel in out.decode("utf-8", "surrogateescape").split("\0"):
        if not rel or rel.startswith(EXCLUDE_PREFIXES) or rel in EXCLUDE_FILES:
            continue
        if not (rel.startswith("blastcorps/") or rel.startswith("docs/") or "/" not in rel):
            continue
        if rel.lower().endswith(BINARY_EXT):
            continue
        p = os.path.join(root, rel)
        if os.path.isfile(p) and not os.path.islink(p):
            files.append(rel)
    return files


def read_text(path):
    data = open(path, "rb").read()
    if b"\0" in data:
        return None
    return data.decode("utf-8", "surrogateescape")


def load_map():
    rows = namesmod.load()
    seen_old, seen_new, errors = {}, {}, []
    for old, new, n in rows:
        if not namesmod.ADDR_NAME.match(old):
            errors.append("names.txt:%d: %s is not an address name (func_/D_ + 8 hex digits)" % (n, old))
        if old in seen_old:
            errors.append("names.txt:%d: %s is renamed twice (line %d)" % (n, old, seen_old[old]))
        if new in seen_new:
            errors.append("names.txt:%d: %s is the new name of two symbols (line %d)" % (n, new, seen_new[new]))
        if namesmod.ADDR_NAME.match(new):
            errors.append("names.txt:%d: the new name %s is an address name" % (n, new))
        seen_old[old], seen_new[new] = n, n
    if errors:
        sys.exit("\n".join(errors))
    return [(o, n) for o, n, _ in rows]


def load_renamed(pairs):
    """`renamed previous = new` lines: previous names rewritten like old ones (no symbol_addrs line:
    the address line of names.txt already names the symbol)"""
    rows = namesmod.load_renamed()
    olds = {o for o, _ in pairs}
    news = {n for _, n in pairs}
    seen, errors = {}, []
    for prev, new, n in rows:
        if prev in olds or prev in news:
            errors.append("names.txt:%d: %s is still an address line's name; rename it there" % (n, prev))
        if prev in seen:
            errors.append("names.txt:%d: %s is renamed twice (line %d)" % (n, prev, seen[prev]))
        if namesmod.ADDR_NAME.match(new):
            errors.append("names.txt:%d: the new name %s is an address name" % (n, new))
        seen[prev] = n
    if errors:
        sys.exit("\n".join(errors))
    return [(p, n) for p, n, _ in rows]


def token_re(words):
    words = sorted(set(words), key=lambda w: (-len(w), w))
    return re.compile(r"(?<![\w$])(LEGACY_)?(" + "|".join(map(re.escape, words)) + r")(?![\w$])")


IDENTITY_DEFINE = re.compile(r"^[ \t]*#[ \t]*define[ \t]+(\w+)[ \t]+\1[ \t]*(?:/\*[^\n]*?\*/[ \t]*|//[^\n]*)?\r?\n",
                             re.M)


def rename_text(text, pat, mapping):
    count = [0]

    def sub(m):
        count[0] += 1
        return (m.group(1) or "") + mapping[m.group(2)]
    text = pat.sub(sub, text)
    if count[0]:
        news = set(mapping.values())
        text = IDENTITY_DEFINE.sub(lambda m: "" if m.group(1) in news else m.group(0), text)
    return text, count[0]


def segment_file(addr):
    for lo, hi, f in SEGMENTS:
        if lo <= addr < hi:
            return f
    return DEFAULT_SEGMENT


def update_symbol_addrs(pairs, dry):
    """every renamed symbol named at its address in hd_code's symbol_addrs file (hd_code's asm
    refers to the front end's and init's addresses too) and in its own segment's, if another
    (tools_port/segsyms.py hands hd_code's names on to the front end, except the front end's own)"""
    added = {}
    for old, new in pairs:
        addr = int(old.split("_")[1], 16)
        kind = "func" if old.startswith("func_") else "data"
        for fn in sorted({DEFAULT_SEGMENT, segment_file(addr)}):
            path = os.path.join(BC, fn)
            text = open(path).read()
            if re.search(r"^\s*%s\s*=" % re.escape(new), text, re.M):
                continue
            added.setdefault(fn, []).append("%s = 0x%08X; // type:%s" % (new, addr, kind))
    for fn, lines in added.items():
        path = os.path.join(BC, fn)
        text = open(path).read()
        if SYMADDR_HEADER not in text:
            text = text.rstrip("\n") + "\n\n" + SYMADDR_HEADER + "\n"
        text = text.rstrip("\n") + "\n" + "\n".join(lines) + "\n"
        if not dry:
            open(path, "w").write(text)
        print("  %s: %d symbol%s added" % (fn, len(lines), "" if len(lines) == 1 else "s"))
    return sum(len(v) for v in added.values())


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("files", nargs="*", help="only these files (paths relative to the current directory)")
    ap.add_argument("--dry-run", action="store_true")
    ap.add_argument("--check", action="store_true", help="report old names still in the tree; exit 1 if any")
    ap.add_argument("--which", metavar="NAME")
    args = ap.parse_args()

    pairs = load_map()
    renamed = load_renamed(pairs)
    mapping = dict(pairs)
    mapping.update(renamed)
    if args.which:
        n = args.which
        if n in mapping:
            print("%s = %s" % (n, mapping[n]))
        elif namesmod.orig(n) != n:
            print("%s = %s" % (namesmod.orig(n), n))
        else:
            print("%s: not in names.txt" % n)
            return 1
        return 0
    if not pairs:
        print("rename.py: names.txt is empty")
        return 0

    root = repo_root()
    if args.files:
        files = [os.path.relpath(os.path.abspath(f), root) for f in args.files]
    else:
        files = tracked_files(root)
    pat = token_re(mapping)

    if args.check:
        stale = 0
        for rel in files:
            text = read_text(os.path.join(root, rel))
            if text is None:
                continue
            for i, line in enumerate(text.splitlines(), 1):
                for m in pat.finditer(line):
                    print("%s:%d: %s (now %s)" % (rel, i, m.group(0), mapping[m.group(2)]))
                    stale += 1
            stem = os.path.basename(rel).partition(".")[0]
            if stem in mapping:
                print("%s: file named after %s (now %s)" % (rel, stem, mapping[stem]))
                stale += 1
        print("rename.py --check: %d old name%s in %d files checked" % (stale, "" if stale == 1 else "s", len(files)))
        return 1 if stale else 0

    # a new name already in use by something else would merge two symbols
    news = token_re(mapping.values())
    olds_left = set()
    clash = {}
    for rel in files:
        text = read_text(os.path.join(root, rel))
        if text is None:
            continue
        for m in pat.finditer(text):
            olds_left.add(m.group(2))
        for m in news.finditer(text):
            clash.setdefault(m.group(2), set()).add(rel)
    inv = {v: k for k, v in mapping.items()}
    bad = [(n, sorted(clash[n])[:3]) for n in clash if inv[n] in olds_left and not args.files]
    # (a new name and its old name both present: a half-applied map is fine to finish, but a
    # new name used for something else is not: list them and let the user decide)
    for n, where in bad:
        print("note: %s (new name of %s) is already used in %s; finishing a partial rename?" %
              (n, inv[n], ", ".join(where)))

    total, changed = 0, 0
    for rel in files:
        path = os.path.join(root, rel)
        text = read_text(path)
        if text is None:
            continue
        new_text, n = rename_text(text, pat, mapping)
        if n:
            total += n
            changed += 1
            print("  %-60s %d" % (rel, n))
            if not args.dry_run:
                with open(path, "wb") as f:
                    f.write(new_text.encode("utf-8", "surrogateescape"))
    # files named after a symbol (tools_port/models/func_XXXXXXXX.py): their
    # references were just rewritten, so the file follows
    moved = 0
    for rel in files:
        d, base = os.path.split(rel)
        stem, dot, ext = base.partition(".")
        if stem in mapping:
            dst = os.path.join(d, mapping[stem] + dot + ext)
            print("  %s -> %s" % (rel, dst))
            moved += 1
            if not args.dry_run:
                os.rename(os.path.join(root, rel), os.path.join(root, dst))
    if moved:
        print("rename.py: %d file%s renamed (stage the old and the new path)" % (moved, "" if moved == 1 else "s"))
    added = 0 if args.files else update_symbol_addrs(pairs, args.dry_run)
    print("rename.py: %d replacement%s in %d file%s%s; %d symbol_addrs line%s added" %
          (total, "" if total == 1 else "s", changed, "" if changed == 1 else "s",
           " (dry run)" if args.dry_run else "", added, "" if added == 1 else "s"))
    if not args.dry_run and (changed or added):
        print("next: make VERSION=us.v11 extract (the asm under the new names), the builds, "
              "and make -C port inputs")
    return 0


if __name__ == "__main__":
    sys.exit(main())
