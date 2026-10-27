"""The rename map (tools_port/names.txt) for the tools that look at names.

The decomp's symbols started as address names: func_80244930 (code), D_80364A90
(data).  tools_port/rename.py gives them real names from names.txt, but several
tools classify symbols by those prefixes (`func_` = a game function, `D_` = N64
data at the address in its name).  They ask orig() first, so a renamed symbol is
treated exactly as its address name was:

    from names import orig
    if orig(name).startswith("func_"): ...

Only the Python standard library: the MSVC build runs some of these tools on
Windows.
"""
import os
import re

HERE = os.path.dirname(os.path.abspath(__file__))
NAMES_TXT = os.path.join(HERE, "names.txt")

# `old = new` (comments after '#'); old is an address name
LINE = re.compile(r"^\s*([A-Za-z_]\w*)\s*=\s*([A-Za-z_]\w*)\s*(?:#.*)?$")
ADDR_NAME = re.compile(r"^(func|D)_([0-9A-F]{8})$")


RENAMED = re.compile(r"^\s*renamed\s+([A-Za-z_]\w*)\s*=\s*([A-Za-z_]\w*)\s*(?:#.*)?$")


def load(path=NAMES_TXT):
    """[(old, new, line number)] in file order; field lines (`field ...`) and
    `renamed` lines (see load_renamed) are skipped"""
    rows = []
    if not os.path.exists(path):
        return rows
    with open(path) as f:
        for n, line in enumerate(f, 1):
            s = line.split("#", 1)[0].strip()
            if not s or s.startswith(("field ", "renamed ")):
                continue
            m = LINE.match(line)
            if not m:
                raise SystemExit("%s:%d: can't parse %r (want `old = new`)" % (path, n, line.rstrip()))
            rows.append((m.group(1), m.group(2), n))
    return rows


def load_renamed(path=NAMES_TXT):
    """[(previous, new, line number)]: `renamed previous = new` lines, a name
    given earlier and replaced since (the address line already says `new`), or
    a non-address name such as func_8029AA10_fp.  rename.py rewrites the
    previous name like an address name; orig() and friends don't use them."""
    rows = []
    if not os.path.exists(path):
        return rows
    with open(path) as f:
        for n, line in enumerate(f, 1):
            if not line.split("#", 1)[0].strip().startswith("renamed "):
                continue
            m = RENAMED.match(line)
            if not m:
                raise SystemExit("%s:%d: can't parse %r (want `renamed previous = new`)" % (path, n, line.rstrip()))
            rows.append((m.group(1), m.group(2), n))
    return rows


_OLD2NEW = None
_NEW2OLD = None


def _maps():
    global _OLD2NEW, _NEW2OLD
    if _OLD2NEW is None:
        rows = load()
        _OLD2NEW = {o: n for o, n, _ in rows}
        _NEW2OLD = {n: o for o, n, _ in rows}
        # a non-address name renamed (func_8029AA10_fp -> veh_collide_world_fp): the new name stands for
        # what the previous one did (the tools classify it as before)
        for p, n, _ in load_renamed():
            _OLD2NEW.setdefault(p, n)
            _NEW2OLD.setdefault(n, p)
    return _OLD2NEW, _NEW2OLD


def orig(name):
    """the address name a symbol had before it was renamed (itself if it wasn't)"""
    return _maps()[1].get(name, name)


def new(name):
    """the current name of a symbol (itself if it isn't in the map)"""
    return _maps()[0].get(name, name)


def game_functions():
    """the new names of renamed game functions (old name func_XXXXXXXX), sorted"""
    return sorted(n for o, n in _maps()[0].items() if o.startswith("func_"))


def addr_of(name):
    """the N64 address an address name (or a renamed one) stands for, or None"""
    m = ADDR_NAME.match(orig(name))
    return int(m.group(2), 16) if m else None
