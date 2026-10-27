#!/usr/bin/env bash
# Generate a first-pass C guess for one or more disassembled functions.
#
#   tools/m2c.sh blastcorps/asm/init/1A30.s
#   tools/m2c.sh -f func_80220E70 blastcorps/asm/init/1F40.s
#
# Builds/reuses blastcorps/ctx.i (preprocessed headers) so m2c knows the
# project's real types and libultra function signatures instead of
# guessing "?" for everything.
set -euo pipefail
cd "$(dirname "$0")/.."

. .env/bin/activate

make -C blastcorps ctx.i

python3 tools/mips_to_c/m2c.py --context blastcorps/ctx.i "$@"
