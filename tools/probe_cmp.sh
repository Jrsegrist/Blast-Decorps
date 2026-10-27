#!/bin/bash
# Compile a C snippet with the hd_code game-file flags and compare functions
# against their target asm in asm/nonmatchings/hd_code/DIR/.
#
# usage: tools/probe_cmp.sh FILE.c DIR OPT func [func...]
#   OPT is -O1 or -O2. FULL=a:b prints a side-by-side of instructions a..b.
# Relocated fields are masked, so symbol names and addends don't matter;
# a MATCH here still needs a real build to confirm.
set -e
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../blastcorps" && pwd)
src=$(realpath "$1"); d=$2; o=$3; shift 3
obj=$(mktemp /tmp/probe_cmp.XXXXXX.o); trap "rm -f $obj" EXIT
cd "$root"
ul=../lib/ultralib
"${IDO_DIR:-../tools/ido5.3}/cc" -c -32 -G 0 -Xcpluscomm -signed -nostdinc -non_shared -Wab,-r4300_mul \
    -D_LANGUAGE_C -D_FINALROM -woff 649,838 -I . -I include -I $ul/include -I $ul/include/PR \
    -I $ul/include/compiler/ido -D_MIPS_SZLONG=32 -DBUILD_VERSION=VERSION_I \
    $o -mips2 -o32 -o "$obj" "$src"
python3 "$here/probe_cmp.py" "$obj" "$root/asm/nonmatchings/hd_code/$d" "$@"
