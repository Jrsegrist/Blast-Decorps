#!/bin/bash
# eq.sh: run eqcheck.py from anywhere. Activates the venv (../.env relative to
# the repo dir), cds to the repo dir, and passes all arguments through.
#   bash tools_port/eq.sh radar_clear_markers -n 200 --ret void
cd "$(dirname "$(readlink -f "$0")")/.." || exit 2
source ../.env/bin/activate || exit 2
exec python3 tools_port/eqcheck.py "$@"
