#!/usr/bin/env python3
"""Gzip one file the way Blast Corps' members are made.

    python3 gzpack.py INFILE OUTFILE --name NAME --time MTIME [--level 6]

Writes 1F 8B 08 08 <MTIME, LE32> 00 03 NAME\\0 <raw deflate> <CRC32> <ISIZE>:
method deflate, FNAME set, XFL 0, OS 3 (Unix), as every member in the ROM.
The deflate stream comes from bcfiles/deflate.py, which reproduces the ROM's
members byte for byte at level 6.
"""
import argparse
import os
import struct
import sys
import zlib

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from bcfiles.deflate import deflate_raw  # noqa: E402


def gzip_member(data, name=None, mtime=0, level=6):
    return (b"\x1f\x8b\x08\x08" + struct.pack("<I", (mtime or 0) & 0xFFFFFFFF) + b"\x00\x03"
            + (name.encode("utf-8") if name else b"") + b"\x00"
            + deflate_raw(data, level)
            + struct.pack("<II", zlib.crc32(data) & 0xFFFFFFFF, len(data) & 0xFFFFFFFF))


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("infile")
    ap.add_argument("outfile")
    ap.add_argument("--name", default=None, help="original file name (FNAME)")
    ap.add_argument("--time", type=int, default=0, help="MTIME, seconds since 1970")
    ap.add_argument("--level", type=int, default=6, help="compression level 4..9 (default 6)")
    args = ap.parse_args(argv)
    with open(args.infile, "rb") as f:
        data = f.read()
    blob = gzip_member(data, args.name, args.time, args.level)
    with open(args.outfile, "wb") as f:
        f.write(blob)


if __name__ == "__main__":
    main()
