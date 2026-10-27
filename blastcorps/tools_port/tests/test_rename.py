#!/usr/bin/env python3
"""Self-tests of tools_port/rename.py's rewriting and tools_port/names.py (no ROM, no build).

    python3 tools_port/tests/test_rename.py
"""
import os
import sys
import tempfile

TP = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, TP)
import names    # noqa: E402
import rename   # noqa: E402

FAILS = []


def check(what, got, want):
    if got != want:
        FAILS.append(what)
        print("FAIL %s\n  got:  %r\n  want: %r" % (what, got, want))
    else:
        print("ok   %s" % what)


def main():
    mapping = {"func_80244930": "hd_main_thread", "D_802E8BDC": "levelno", "D_80364A90": "gGameMode"}
    pat = rename.token_re(mapping)

    def ren(s):
        return rename.rename_text(s, pat, mapping)

    check("whole word", ren("func_80244930(); func_80244930_fp; xfunc_80244930; D_80364A94;")[0],
          "hd_main_thread(); func_80244930_fp; xfunc_80244930; D_80364A94;")
    check("asm path in a pragma", ren('#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/00000/func_80244930.s")')[0],
          '#pragma GLOBAL_ASM("asm/nonmatchings/hd_code/00000/hd_main_thread.s")')
    check("check-file keys", ren('("func_80244930", 0x4A58, 0x56C0) PORT_GVI("func_80244930:4A58", x)')[0],
          '("hd_main_thread", 0x4A58, 0x56C0) PORT_GVI("hd_main_thread:4A58", x)')
    check("LEGACY_ macro", ren("#define LEGACY_D_80364A90\n#ifndef LEGACY_D_80364A90\n")[0],
          "#define LEGACY_gGameMode\n#ifndef LEGACY_gGameMode\n")
    check("--mem expressions", ren("--mem D_80364A90+0x5C*0x74:3=rand --mem D_80364A90:8=0")[0],
          "--mem gGameMode+0x5C*0x74:3=rand --mem gGameMode:8=0")
    check("identity define removed",
          ren("#define levelno D_802E8BDC\n#define x D_802E8BDC /* c */\nint a;\n")[0],
          "#define x levelno /* c */\nint a;\n")
    check("identity define with a comment removed", ren("  #define levelno D_802E8BDC /* level */\nint a;\n")[0],
          "int a;\n")
    check("CRLF kept", ren("D_802E8BDC = 1;\r\n")[0], "levelno = 1;\r\n")
    once = ren("func_80244930(D_802E8BDC);")[0]
    check("idempotent", ren(once), (once, 0))
    check("count", ren("D_80364A90 D_80364A90 func_80244930")[1], 3)

    # names.py: orig/new/addr_of with a map file
    with tempfile.TemporaryDirectory() as d:
        p = os.path.join(d, "names.txt")
        with open(p, "w") as f:
            f.write("# comment\nfunc_80244930 = hd_main_thread  # main thread\n\n"
                    "field Unk74.unk5C = id\nD_80364A90 = gGameMode\n")
        rows = names.load(p)
        check("names.load", [(o, n) for o, n, _ in rows],
              [("func_80244930", "hd_main_thread"), ("D_80364A90", "gGameMode")])
        names._OLD2NEW = {o: n for o, n, _ in rows}
        names._NEW2OLD = {n: o for o, n, _ in rows}
        check("orig", (names.orig("hd_main_thread"), names.orig("gGameMode"), names.orig("osSendMesg")),
              ("func_80244930", "D_80364A90", "osSendMesg"))
        check("new", names.new("D_80364A90"), "gGameMode")
        check("addr_of", (names.addr_of("gGameMode"), names.addr_of("D_802E8BDC"), names.addr_of("osSendMesg")),
              (0x80364A90, 0x802E8BDC, None))
        names._OLD2NEW = names._NEW2OLD = None
        with open(p, "a") as f:
            f.write("renamed gOldName = gGameMode  # was the address line's name\n"
                    "renamed func_80244930_fp = hd_main_thread_fp\n")
        check("names.load skips renamed lines", [(o, n) for o, n, _ in names.load(p)],
              [("func_80244930", "hd_main_thread"), ("D_80364A90", "gGameMode")])
        check("names.load_renamed", [(o, n) for o, n, _ in names.load_renamed(p)],
              [("gOldName", "gGameMode"), ("func_80244930_fp", "hd_main_thread_fp")])
    m2 = dict(mapping)
    m2.update({"gOldName": "gGameMode", "func_80244930_fp": "hd_main_thread_fp"})
    pat2 = rename.token_re(m2)
    check("renamed names rewritten", rename.rename_text("gOldName = func_80244930_fp(func_80244930);", pat2, m2)[0],
          "gGameMode = hd_main_thread_fp(hd_main_thread);")

    check("segment files", (rename.segment_file(0x801E7598), rename.segment_file(0x80220000),
                            rename.segment_file(0x80244930), rename.segment_file(0x80364A90)),
          ("symbol_addrs.hd_front_end.us.v11.txt", "symbol_addrs.init.us.v11.txt",
           "symbol_addrs.hd_code.us.v11.txt", "symbol_addrs.hd_code.us.v11.txt"))
    # the real map parses and is consistent
    check("names.txt loads", isinstance(rename.load_map(), list), True)
    check("names.txt renamed lines load", isinstance(rename.load_renamed(rename.load_map()), list), True)
    print("%d failed" % len(FAILS))
    return 1 if FAILS else 0


if __name__ == "__main__":
    sys.exit(main())
