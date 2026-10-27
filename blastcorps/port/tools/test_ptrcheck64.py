#!/usr/bin/env python3
"""Self-test of ptrcheck64.py's VARG / KNR findings (an N64 pointer passed
through varargs, or to a function without a prototype): a snippet with the bad
calls must fail with exactly the expected findings, the same snippet with the
calls cast to native pointers must pass.  Synthetic code only (no game source).

usage: test_ptrcheck64.py CLANG_CMD...   (e.g. clang-18 --target=x86_64-w64-mingw32
                                          -fms-extensions -std=gnu89)
"""
import os
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))

# CAST(x): the bad snippet passes x as it is, the good one casts it
SNIPPET = r"""
#define N64P __ptr32 __uptr
typedef unsigned char u8;
typedef u8 * N64P N64U8P;
typedef struct { char * N64P title; u8 * N64P pic; } Rank;
typedef struct { int x; } Rec;
int sprintf(char *, const char *, ...);
void debug_printf(const char *fmt, ...);
int unproto();
extern Rank gRanks[8];
extern Rec * N64P gRecs;
extern char * N64P * N64P gNames;
extern char * N64P * gHostToN64;
extern N64U8P gTd;
extern int (*gLog)(const char *, ...);
extern char buf[64];
#define RANK_NAME(i) gRanks[i].title
void f(int i, int c) {
    sprintf(buf, "%s%s(%s)", "a", "b", CAST(char *, gRanks[i].title));  /* 5th: on the stack */
    sprintf(buf, "%s", CAST(char *, RANK_NAME(i)));                      /* through a macro */
    sprintf(buf, "%s", CAST(char *, c ? gRanks[0].title : gRanks[1].title)); /* a ternary */
    sprintf(buf, "%s", CAST(char *, gNames[i]));                         /* an array element */
    sprintf(buf, "%p", CAST(u8 *, gTd));                                 /* a typedef */
    debug_printf("%p", CAST(void *, gNames));                            /* N64P to N64P */
    gLog("%s", CAST(char *, gRanks[i].title));                           /* a variadic function pointer */
    unproto(CAST(Rec *, &gRecs[i]));                                     /* no prototype */
    /* never findings: a native pointer, a host pointer to an N64P one, an
     * int, a prototyped parameter, a fixed parameter */
    sprintf(buf, "%s %p %d", buf, gHostToN64, c);
    sprintf((char *) gRanks[i].title, "x");
}
"""
EXPECT = {"VARG": [19, 20, 21, 22, 23, 24, 25], "KNR": [26]}


def run(cmd, src, td, name, define):
    path = os.path.join(td, name + ".c")
    with open(path, "w") as o:
        o.write("#define CAST(t, x) %s\n" % define)
        o.write(SNIPPET)
    empty = os.path.join(td, "empty.txt")
    open(empty, "w").close()
    out = os.path.join(td, name + ".txt")
    r = subprocess.run([sys.executable, os.path.join(HERE, "ptrcheck64.py"), empty, empty, out, "--"] + cmd +
                       ["-w", "--", path], capture_output=True, text=True)
    got = {}
    for line in open(out):
        k = line.split("\t")[0].split()
        if k[0] in ("VARG", "KNR"):
            got.setdefault(k[0], []).append(int(k[1].split(":")[1]))
        else:
            got.setdefault(k[0], []).append(k[1])
    for v in got.values():
        v.sort()
    return r.returncode, got, r.stdout + r.stderr


def main():
    cmd = sys.argv[1:]
    if not cmd:
        sys.stderr.write(__doc__)
        return 2
    with tempfile.TemporaryDirectory() as td:
        # (the #define line comes first: the snippet's line N is file line N + 1)
        rc_bad, bad, log_bad = run(cmd, SNIPPET, td, "bad", "(x)")
        rc_good, good, log_good = run(cmd, SNIPPET, td, "good", "((t) (x))")
    want = {k: [n + 1 for n in v] for k, v in EXPECT.items()}
    ok = True
    if rc_bad != 1 or bad != want:
        sys.stderr.write("test_ptrcheck64: the bad snippet: exit %d, findings %s, expected exit 1, %s\n%s"
                         % (rc_bad, bad, want, log_bad))
        ok = False
    if rc_good != 0 or good:
        sys.stderr.write("test_ptrcheck64: the cast snippet: exit %d, findings %s, expected none\n%s"
                         % (rc_good, good, log_good))
        ok = False
    print("test_ptrcheck64: %s (bad snippet: %d VARG + %d KNR found, %d expected; cast snippet: %d findings)"
          % ("OK" if ok else "FAILED", len(bad.get("VARG", [])), len(bad.get("KNR", [])),
             sum(len(v) for v in EXPECT.values()), sum(len(v) for v in good.values())))
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
