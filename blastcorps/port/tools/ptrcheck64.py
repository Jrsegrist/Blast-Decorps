#!/usr/bin/env python3
"""Pointer-width audit of the game C for the 64-bit build (port/README.md, "64-bit").

The 64-bit exe keeps N64 memory at its N64 addresses with N64 layouts: every
pointer stored in N64 memory (a struct field, a pinned global, an element of
a pinned table) must stay 4 bytes (`T * N64P`, port/include/port_n64ptr.h).  This
reads clang's AST of each translation unit (x86_64 target, the 64-bit build's
flags) and reports what still has a host-width (8-byte) pointer:

  FIELD  record.field   a pointer field of a record (struct/union)
  FNPTR  record.field   a function pointer field (clang can't load __ptr32
                        function pointers correctly: use N64FN(T), a u32)
  VAR    name           a pinned global (in ADDRS, or named D_<addr>) holding a
                        pointer
  CAST   file:line      a cast to a pointer to a host-width pointer
                        (`*(u8 **) p` reads 8 bytes of N64 memory)
  SEXT   file:line      a signed 32-bit int cast to a host-width pointer: an
                        address >= 0x80000000 sign-extends (N64_IPTR)
  SIZEOF file:line      sizeof of a host-width pointer type (a table of N64
                        pointers sized with it is twice as big)
  PADD   file:line      pointer arithmetic with a pointer cast to a signed int
                        (`m += (s32) buf`: N64_A32)
  VARG   file:line      an N64 pointer (`T * N64P`, __ptr32) passed in a variadic
                        position (`sprintf(buf, "%s", rec->name)`): it goes as 4
                        bytes and the callee's va_arg(ap, char *) reads 8 (MSVC:
                        the upper half is stack garbage).  Cast it to a native
                        pointer: `(char *) rec->name`
  KNR    file:line      the same passed to a function without a prototype
                        (`int f();`, or an implicit declaration): no conversion
                        to the parameter's type, the same 4-byte argument

usage: ptrcheck64.py ADDRS ALLOW OUT -- CLANG_CMD... -- FILE.c...
  ALLOW  finding keys known to be host-only, fnmatch patterns ('FIELD
         record.*' for a whole record), a reason after '#'
         (data/ptrcheck64_allow.txt)
  Exit status 1 if anything not allowed is found.
"""
import fnmatch
import os
import re
import subprocess
import sys

# renamed symbols (tools_port/names.txt) count as the address names they had
sys.path.insert(0, os.path.join(os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))),
                                "tools_port"))
from names import orig  # noqa: E402

ANY = re.compile(r"^([ |`-]*)\w+ 0x[0-9a-f]+ ")
# a signed integer of at most 32 bits: sign-extends when cast to a 64-bit pointer
SIGNED32 = {"int", "short", "signed char", "char", "long"}
DECL = re.compile(r"^(?P<indent>[ |`-]*)(?P<kind>RecordDecl|FieldDecl|VarDecl|CStyleCastExpr|TypedefDecl)"
                  r" 0x[0-9a-f]+ (?P<rest>.*)$")
TYPE = re.compile(r"'([^']*)'(?::'([^']*)')?")
LOCTOK = re.compile(r"(?:([^\s<>,()]+\.[ch]):(\d+):\d+|line:(\d+):\d+)")


def first_type(rest):
    ms = TYPE.findall(rest)
    if not ms:
        return None
    a, b = ms[0]
    return b or a


def host_ptr(t):
    """does type string t (canonical) contain a pointer that is not __ptr32?
    (clang prints an address-space pointer's qualifiers in front of the
    pointee, `__uptr __ptr32 unsigned char *`, so count them)"""
    if t is None:
        return False
    return t.count("__ptr32") < t.count("*")


def is_fnptr(t):
    return t is not None and re.search(r"\(\s*\*", t) is not None


def ptr_to_host_ptr(t):
    """T ** or T *const * etc.: a pointer whose pointee is a host-width pointer"""
    if t is None or is_fnptr(t):
        return False
    # every pointer level but the outermost must be __ptr32
    return t.count("*") >= 2 and t.count("__ptr32") < t.count("*") - 1


def outer_n64(t):
    """is type string t (canonical) an N64 pointer (`T * N64P`): is its
    outermost pointer __ptr32?  clang prints the address space on the
    pointee, `__uptr __ptr32 char *` (N64P to char), `char *__uptr __ptr32 *`
    (N64P to a host char *); the attribute sugar of an inner pointer comes out
    the other way round, `char * __ptr32  __uptr *` (a host pointer to an N64P
    one), so only `__uptr __ptr32` between the last two `*` counts.  A
    function pointer is not one (N64 function pointers are N64FN, a u32)."""
    if t is None or is_fnptr(t) or "*" not in t:
        return False
    k = t.rfind("*")
    seg = t[t.rfind("*", 0, k) + 1:k]
    return re.search(r"__[us]ptr __ptr32\b", seg) is not None


def fn_params(t):
    """the parameters of a callee's type (`int (*)(char *, const char *, ...)`):
    (number of fixed parameters, variadic?, no prototype?), or None when t is
    not a function (pointer) type"""
    if t is None or not t.rstrip().endswith(")"):
        return None
    # the last top-level parenthesised group is the parameter list
    s = t.rstrip()
    depth, k = 0, len(s) - 1
    while k >= 0:
        if s[k] == ")":
            depth += 1
        elif s[k] == "(":
            depth -= 1
            if depth == 0:
                break
        k -= 1
    if k < 0:
        return None
    inner = s[k + 1:-1].strip()
    if inner == "":
        return (0, False, True)
    if inner == "void":
        return (0, False, False)
    parts, depth, cur = [], 0, ""
    for ch in inner:
        if ch in "([":
            depth += 1
        elif ch in ")]":
            depth -= 1
        if ch == "," and depth == 0:
            parts.append(cur.strip())
            cur = ""
        else:
            cur += ch
    parts.append(cur.strip())
    if parts and parts[-1] == "...":
        return (len(parts) - 1, True, False)
    return (len(parts), False, False)


CALLEE_NAME = re.compile(r"\b(?:Function|Var|ParmVar|Field) 0x[0-9a-f]+ '(\w+)'|->(\w+) 0x|\.(\w+) 0x")


def main():
    a = sys.argv[1:]
    addrs_path, allow_path, out_path = a[0], a[1], a[2]
    i = a.index("--", 3)
    j = a.index("--", i + 1)
    cmd, files = a[i + 1:j], a[j + 1:]
    pinned = set()
    for line in open(addrs_path):
        p = line.split()
        if p:
            pinned.add(p[0])
    allow = set()
    try:
        for line in open(allow_path):
            line = line.split("#")[0].strip()
            if line:
                allow.add(line)
    except FileNotFoundError:
        pass

    def allowed(k):
        return any(fnmatch.fnmatchcase(k, a) for a in allow)

    found = {}
    for f in files:
        r = subprocess.run(cmd + ["-fsyntax-only", "-Xclang", "-ast-dump", "-fno-color-diagnostics", f],
                           capture_output=True, text=True, errors="replace")
        if r.returncode != 0:
            sys.stderr.write(r.stderr[-3000:])
            sys.stderr.write("ptrcheck64: clang failed on %s (its AST is still read)\n" % f)
            found.setdefault("ERROR %s" % f.split("/")[-1], ("clang errors", f))
        stack = []   # (depth, record name)
        curfile, curline = None, 0
        sext = None  # (depth, key) of an int -> pointer cast whose operand is next
        estack = []  # every node's (depth, kind, type): the ancestors of the current one
        calls = []   # open CallExprs: [depth, where, children seen, params, callee name]
        for line in r.stdout.splitlines():
            m = DECL.match(line)
            g0 = ANY.match(line)
            if g0:
                d0 = len(g0.group(1))
                while estack and estack[-1][0] >= d0:
                    estack.pop()
                kind0 = re.match(r"^[ |`-]*(\w+)", line).group(1)
                t0 = first_type(line[g0.end():])
                # pointer arithmetic with a pointer cast to a signed int: an
                # N64 address >= 0x80000000 is negative and sign-extends
                # (`p += (s32) q`: N64_A32 instead of s32)
                if kind0 == "CStyleCastExpr" and "<PointerToIntegral>" in line and t0 in SIGNED32:
                    for d, kd, td in reversed(estack):
                        if kd in ("ParenExpr", "ImplicitCastExpr", "CStyleCastExpr"):
                            continue
                        if kd in ("BinaryOperator", "CompoundAssignOperator") and td and "*" in td:
                            lm2 = LOCTOK.findall(line.split(">")[0] if "<" in line else "")
                            ln = curline
                            for fl, a1, a2 in lm2:
                                ln = int(a1) if fl else int(a2) if a2 else ln
                                break
                            found.setdefault("PADD %s:%d" % ((curfile or "?").split("/")[-1], ln),
                                             (t0, "%s:%d" % (curfile, ln)))
                        break
                estack.append((d0, kind0, t0))
            if sext is not None:
                g = ANY.match(line)
                if g and len(g.group(1)) > sext[0]:
                    ot = first_type(line[g.end():])
                    # (a literal can't be negative here: no sign to extend)
                    if ot in SIGNED32 and not re.search(r"\bIntegerLiteral 0x", line):
                        found.setdefault(sext[1], (ot, sext[2]))
                    sext = None
            # clang prints a location relative to the previous one: follow it
            lpart = line.split(">")[0] if "<" in line else ""
            locs = LOCTOK.findall(lpart)
            first = None
            for fl, ln, ln2 in locs:
                if fl:
                    curfile, curline = fl, int(ln)
                elif ln2:
                    curline = int(ln2)
                if first is None:
                    first = (curfile, curline)
            # N64 pointers as variadic / unprototyped arguments: the children of
            # a CallExpr are the callee, then the arguments as passed (after
            # the implicit conversions: a variadic pointer argument keeps its
            # own type)
            if g0:
                while calls and calls[-1][0] >= d0:
                    calls.pop()
                if calls and calls[-1][0] == d0 - 2:
                    c = calls[-1]
                    idx = c[2]
                    c[2] += 1
                    if idx == 0:
                        c[3] = fn_params(t0)
                    elif c[3] is not None:
                        nfix, var, noproto = c[3]
                        if (noproto or (var and idx - 1 >= nfix)) and outer_n64(t0):
                            k = "KNR" if noproto else "VARG"
                            here = "%s:%d" % ((c[1][0] or "?").split("/")[-1], c[1][1])
                            found.setdefault("%s %s" % (k, here),
                                             ("%s arg %d of %s()" % (t0, idx, c[4] or "?"),
                                              "%s:%d" % c[1]))
                elif calls and calls[-1][2] == 1 and calls[-1][4] is None:
                    # inside the callee: its name
                    nm = CALLEE_NAME.search(line)
                    if nm:
                        calls[-1][4] = nm.group(1) or nm.group(2) or nm.group(3)
                if kind0 == "CallExpr":
                    calls.append([d0, first or (curfile, curline), 0, None, None])
            # sizeof(T *) of a host-width pointer: 8 natively, 4 on the N64
            # (a table of N64 pointers sized with it comes out twice as big)
            sz = re.search(r"UnaryExprOrTypeTraitExpr 0x[0-9a-f]+ <[^>]*> '[^']*' sizeof '([^']*)'(?::'([^']*)')?", line)
            if sz:
                st = sz.group(2) or sz.group(1)
                if host_ptr(st) and not is_fnptr(st):
                    here = "%s:%d" % ((curfile or "?").split("/")[-1], curline)
                    found.setdefault("SIZEOF %s" % here, (st, "%s:%d" % (curfile, curline)))
            if not m:
                continue
            depth = len(m.group("indent"))
            kind, rest = m.group("kind"), m.group("rest")
            while stack and stack[-1][0] >= depth:
                stack.pop()
            where = first or (curfile, curline)
            if kind == "RecordDecl":
                nm = re.search(r"\b(struct|union) (\w+)", rest)
                if nm and nm.group(2) == "definition":
                    nm = None
                name = nm.group(2) if nm else "anon@%s:%d" % ((where[0] or "?").split("/")[-1], where[1])
                stack.append((depth, name))
                continue
            if kind == "TypedefDecl":
                continue
            t = first_type(rest)
            src = "%s:%d" % (where[0] or "?", where[1])
            if kind == "FieldDecl":
                fm = re.search(r"\b(\w+) '", rest)
                fname = fm.group(1) if fm else "?"
                rec = stack[-1][1] if stack else "?"
                if is_fnptr(t):
                    key = "FNPTR %s.%s" % (rec, fname)
                elif host_ptr(t):
                    key = "FIELD %s.%s" % (rec, fname)
                else:
                    continue
                found.setdefault(key, (t, src))
            elif kind == "VarDecl":
                vm = re.search(r"\b(\w+) '", rest)
                vname = vm.group(1) if vm else "?"
                if vname not in pinned and not re.match(r"D_[0-9A-F]{8}$", orig(vname)):
                    continue
                if host_ptr(t):
                    found.setdefault("VAR %s" % vname, (t, src))
            elif kind == "CStyleCastExpr":
                here = "%s:%d" % ((where[0] or "?").split("/")[-1], where[1])
                if ptr_to_host_ptr(t):
                    found.setdefault("CAST %s" % here, (t, src))
                # an int -> host-width pointer cast (to a __ptr32 one truncates:
                # nothing to extend)
                if "<IntegralToPointer>" in rest and t and t.count("__ptr32") < t.count("*"):
                    sext = (depth, "SEXT %s" % here, src)
    bad = 0
    kinds = {}
    with open(out_path, "w") as o:
        for k in sorted(found):
            t, fl = found[k]
            ok = allowed(k)
            if not ok:
                bad += 1
                kinds[k.split()[0]] = kinds.get(k.split()[0], 0) + 1
            o.write("%s%s\t%s\t%s\n" % ("allowed " if ok else "", k, t, fl))
    print("ptrcheck64: %d findings not allowed %s (%s)" % (bad, kinds, out_path))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
