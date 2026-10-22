#!/bin/sh
# Run the tests. To include the tests that use the game data, give the path of
# YOUR disc image:  scripts/run_tests.sh "/path/NASCAR Rumble (USA).cue"
# The project is built first (scripts/build.sh) when build/nrtests is missing.
set -e
ROOT=$(cd "$(dirname "$0")/.." && pwd)
[ -x "$ROOT/build/nrtests" ] || "$ROOT/scripts/build.sh"
if [ -n "$1" ]; then NR_DISC="$1"; export NR_DISC; fi
cd "$ROOT/build"
./nrtests
