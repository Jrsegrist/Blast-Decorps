#!/usr/bin/env python3
"""Shows that no code or text of the fork this repository started from remains.

The repository began as a fork of retroplastic/blastcorps (last upstream commit
baa807e, 2021-12-28; mkst and queueRAM wrote parts of it).  Everything from it
was replaced.  This script compares every tracked file of a commit (default
HEAD) with every text file of the upstream tree, line by line:

  exact          the same line after stripping the ends
  near-duplicate the same "skeleton": lower case, only the letters and digits,
                 one space between the words (so spacing, punctuation, quoting
                 and case changes don't hide a copied line)

Blank and trivial lines are not compared: a line whose skeleton has fewer than
3 words or fewer than 12 letters and digits (`endif`, `}`, `#endif`, `else`).
Every other line that matches is listed, and must be explained by a rule of
tools_port/provenance_allow.txt (a facts-only line: a ROM offset in splat's
syntax, a SHA-1, a submodule's URL); the check fails on any line no rule
explains.  The submodules are other projects (pinned commits, their own
licences) and are not part of the comparison.

`--blame` adds git blame: every line of the commit not written by this
project's author (--author, default jrsegrist@gmail.com) is listed with its
file and author, and must be blank, trivial or an explained match too.

usage (from anywhere in the checkout):
  python3 blastcorps/tools_port/provenance_check.py [--rev HEAD] [--upstream baa807e] [--blame] [-v]
The upstream commit has to be in the repository's objects; in a clone that
lacks it: git fetch https://github.com/retroplastic/blastcorps.git master
"""
import argparse
import os
import re
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ALLOW = os.path.join(HERE, "provenance_allow.txt")
WORD = re.compile(r"[a-z0-9]+")


def git(*args, binary=False):
    r = subprocess.run(["git"] + list(args), capture_output=True)
    if r.returncode:
        sys.exit("git %s: %s" % (" ".join(args), r.stderr.decode(errors="replace").strip()))
    return r.stdout if binary else r.stdout.decode("utf-8", errors="replace")


def skeleton(line):
    return " ".join(WORD.findall(line.lower()))


def trivial(skel):
    return len(skel.split()) < 3 or len(skel.replace(" ", "")) < 12


def tree_files(rev):
    """(path, blob) of the regular files of a commit (submodules are 'commit' entries: skipped)."""
    out = []
    for line in git("ls-tree", "-r", "-z", rev).split("\0"):
        if not line:
            continue
        meta, path = line.split("\t", 1)
        mode, kind, sha = meta.split()
        if kind == "blob" and mode != "120000":
            out.append((path, sha))
    return out


def text_lines(sha):
    data = git("cat-file", "blob", sha, binary=True)
    if b"\0" in data[:8192]:
        return None
    return data.decode("utf-8", errors="replace").split("\n")


def load_rules():
    """(line rules, source rules): FILE-GLOB<tab>REGEX<tab>REASON, or
    @source<tab>UPSTREAM-GLOB<tab>REASON (the upstream tree's copies of other
    people's code: a match only with lines of those files is not upstream's)."""
    rules, sources = [], []
    for raw in open(ALLOW, encoding="utf-8"):
        raw = raw.rstrip("\n")
        if not raw.strip() or raw.lstrip().startswith("#"):
            continue
        parts = raw.split("\t")
        if len(parts) != 3:
            sys.exit("%s: want FILE-GLOB<tab>REGEX<tab>REASON: %r" % (ALLOW, raw))
        if parts[0] == "@source":
            sources.append((re.compile(glob_re(parts[1])), parts[2]))
        else:
            rules.append((re.compile(glob_re(parts[0])), re.compile(parts[1]), parts[2]))
    return rules, sources


def glob_re(g):
    out = ""
    i = 0
    while i < len(g):
        c = g[i]
        if g.startswith("**", i):
            out += ".*"
            i += 2
            continue
        out += "[^/]*" if c == "*" else re.escape(c)
        i += 1
    return "^" + out + "$"


def explain(rules, sources, path, line, srcs):
    for fre, lre, why in rules:
        if fre.match(path) and lre.search(line):
            return why
    whys = []
    for s in sorted(srcs):
        w = next((why for sre, why in sources if sre.match(s)), None)
        if w is None:
            return None
        if w not in whys:
            whys.append(w)
    return "; ".join(whys) if whys else None


def names(srcs):
    s = sorted(srcs)
    return ", ".join(s[:2]) + (" +%d" % (len(s) - 2) if len(s) > 2 else "")


def blame_others(rev, path, author):
    """line numbers (1-based) of path at rev written by someone other than author -> that author."""
    r = subprocess.run(["git", "blame", "--line-porcelain", rev, "--", path], capture_output=True)
    if r.returncode:
        return {}
    out, cur, num = {}, None, None
    for line in r.stdout.decode("utf-8", errors="replace").split("\n"):
        m = re.match(r"^[0-9a-f]{40} \d+ (\d+)", line)
        if m:
            num = int(m.group(1))
            continue
        if line.startswith("author-mail "):
            mail = line[len("author-mail "):].strip("<>")
            if mail != author:
                cur = mail
                out[num] = mail
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--rev", default="HEAD")
    ap.add_argument("--upstream", default="baa807e", help="the upstream commit (default baa807e)")
    ap.add_argument("--blame", action="store_true", help="also list the lines git blame gives to others")
    ap.add_argument("--author", default="jrsegrist@gmail.com")
    ap.add_argument("-v", "--verbose", action="store_true", help="list the explained matches too")
    a = ap.parse_args()
    os.chdir(git("rev-parse", "--show-toplevel").strip())
    if subprocess.run(["git", "cat-file", "-e", a.upstream + "^{commit}"], capture_output=True).returncode:
        sys.exit("upstream commit %s is not in this repository: git fetch "
                 "https://github.com/retroplastic/blastcorps.git master" % a.upstream)
    rules, sources = load_rules()

    # the upstream lines (-> the upstream files that have them)
    up_exact, up_skel, up_files, up_lines = {}, {}, 0, 0
    for path, sha in tree_files(a.upstream):
        lines = text_lines(sha)
        if lines is None:
            continue
        up_files += 1
        for ln in lines:
            s = skeleton(ln)
            if not s or trivial(s):
                continue
            up_lines += 1
            up_exact.setdefault(ln.strip(), set()).add(path)
            up_skel.setdefault(s, set()).add(path)

    # the commit's lines
    files = tree_files(a.rev)
    matches, scanned, nfiles = [], 0, 0
    text = {}
    for path, sha in files:
        lines = text_lines(sha)
        if lines is None:
            continue
        nfiles += 1
        text[path] = lines
        for i, ln in enumerate(lines, 1):
            s = skeleton(ln)
            if not s or trivial(s):
                continue
            scanned += 1
            if ln.strip() in up_exact:
                matches.append((path, i, ln, "exact", up_exact[ln.strip()] | up_skel[s]))
            elif s in up_skel:
                matches.append((path, i, ln, "near-duplicate", up_skel[s]))

    unexplained, by_reason = [], {}
    for path, i, ln, kind, src in matches:
        why = explain(rules, sources, path, ln, src)
        if why is None:
            unexplained.append((path, i, ln, kind, src))
        else:
            by_reason.setdefault(why, []).append((path, i, ln, kind, src))

    print("upstream %s: %d text files, %d non-trivial lines" % (a.upstream, up_files, up_lines))
    print("%s: %d text files, %d non-trivial lines compared" % (a.rev, nfiles, scanned))
    print("matching lines: %d (exact %d, near-duplicate %d); explained %d, unexplained %d" % (
        len(matches), sum(1 for m in matches if m[3] == "exact"),
        sum(1 for m in matches if m[3] != "exact"), len(matches) - len(unexplained), len(unexplained)))
    for why, ms in sorted(by_reason.items()):
        print("  %4d  %s" % (len(ms), why))
        if a.verbose:
            for path, i, ln, kind, src in ms:
                print("          %s:%d (%s of %s): %s" % (path, i, kind, names(src), ln.strip()))
    for path, i, ln, kind, src in unexplained:
        print("UNEXPLAINED %s:%d (%s of %s): %s" % (path, i, kind, names(src), ln.strip()))

    bad_blame = 0
    if a.blame:
        explained = {(p, i) for ms in by_reason.values() for p, i, _, _, _ in ms}
        kinds, others = {}, []
        for path in sorted(text):
            for num, mail in sorted(blame_others(a.rev, path, a.author).items()):
                ln = text[path][num - 1] if num - 1 < len(text[path]) else ""
                s = skeleton(ln)
                if not s:
                    k = "blank"
                elif trivial(s):
                    k = "trivial"
                elif (path, num) in explained:
                    k = "explained match"
                elif ln.strip() not in up_exact and s not in up_skel:
                    k = "not in the upstream tree"
                else:
                    k = "UNEXPLAINED"
                    bad_blame += 1
                kinds[k] = kinds.get(k, 0) + 1
                others.append((path, num, mail, k, ln))
        print("git blame: %d lines by others: %s" % (len(others), ", ".join(
            "%s %d" % (k, n) for k, n in sorted(kinds.items())) or "none"))
        for path, num, mail, k, ln in others:
            if a.verbose or k not in ("blank", "trivial"):
                print("  %s:%d  %s  [%s]  %s" % (path, num, mail, k, ln.strip()))
    if unexplained or bad_blame:
        print("provenance_check: FAILED")
        return 1
    print("provenance_check: OK (no upstream line remains that a rule doesn't explain)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
