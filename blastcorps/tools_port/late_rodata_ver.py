#!/usr/bin/env python3
"""tools/late_rodata.py for a version other than us.v11.

late_rodata.py reads the typed constants from symbol_addrs.hd_code.us.v11.txt
and their values from hd_code.us.v11.bin. This runs it with VERSION's files:

  late_rodata_ver.py TOOLS_DIR VERSION asm/VERSION/nonmatchings/hd_code/FILE ...

(the hd_code data segment's vram minus its ROM offset is the same in every
version, so late_rodata.ROM_OFF stays as it is).
"""
import importlib.util
import os
import sys


def main():
    tools, ver = sys.argv[1], sys.argv[2]
    spec = importlib.util.spec_from_file_location('late_rodata', os.path.join(tools, 'late_rodata.py'))
    lr = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(lr)
    lr.SYMS = 'symbol_addrs.hd_code.%s.txt' % ver
    lr.ROM = 'hd_code.%s.bin' % ver
    sys.argv = [sys.argv[0]] + sys.argv[3:]
    lr.main()


if __name__ == '__main__':
    main()
