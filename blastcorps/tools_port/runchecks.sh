#!/bin/bash
# runchecks.sh: run the eqcheck regression suite (tools_port/checks/*.txt).
# Activates the venv, cds to the repo dir, passes all arguments through.
#   bash tools_port/runchecks.sh -b            # build NON_MATCHING, run everything
#   bash tools_port/runchecks.sh 8A080 cam_apply_height_zone
cd "$(dirname "$(readlink -f "$0")")/.." || exit 2
source ../.env/bin/activate || exit 2
exec python3 tools_port/runchecks.py "$@"
