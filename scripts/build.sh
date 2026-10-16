#!/bin/sh
# Build NR Engine on Linux, BSD, macOS or MSYS2 (Windows).
#
# Usage: scripts/build.sh [Release|Debug|RelWithDebInfo] [extra CMake arguments...]
#
#   scripts/build.sh                      optimized build in ./build
#   scripts/build.sh Debug                debug build (assertions, no optimization)
#   BUILD_DIR=out scripts/build.sh        choose another build folder
#   scripts/build.sh Release -DNR_BUILD_TESTS=OFF
#
# Needs: CMake >= 3.13, a C99 compiler and (for the window and the launcher)
# the SDL2 development package. See docs/BUILD.md for every platform.
set -e
TYPE=${1:-Release}
[ $# -gt 0 ] && shift
ROOT=$(cd "$(dirname "$0")/.." && pwd)
BUILD=${BUILD_DIR:-$ROOT/build}

if ! command -v cmake >/dev/null 2>&1; then
    echo "error: cmake was not found. Install it first (Debian/Ubuntu: sudo apt install cmake)." >&2
    exit 1
fi

JOBS=$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 4)
echo "== configuring ($TYPE) in $BUILD"
cmake -S "$ROOT" -B "$BUILD" -DCMAKE_BUILD_TYPE="$TYPE" "$@"
echo "== compiling with $JOBS parallel jobs"
cmake --build "$BUILD" --parallel "$JOBS"
echo
echo "Executables in $BUILD:"
for f in nrlauncher nrengine nrengine_headless nrtool nrtests; do
    for e in "" .exe; do [ -e "$BUILD/$f$e" ] && echo "  $f$e"; done
done
if [ ! -e "$BUILD/nrlauncher" ] && [ ! -e "$BUILD/nrlauncher.exe" ]; then
    echo
    echo "note: SDL2 was not found, so the launcher and the engine window were not built."
    echo "      Debian/Ubuntu: sudo apt install libsdl2-dev   Fedora: sudo dnf install SDL2-devel"
fi
echo
echo "Start the game with: $BUILD/nrlauncher"
