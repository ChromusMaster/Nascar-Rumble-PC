#!/bin/sh
# Build the Debian/Ubuntu package (.deb) of NR Engine.
#
# Usage: scripts/build_deb.sh [extra CMake arguments]
#                                         -> dist/nrengine_<version>_<arch>.deb
#   scripts/build_deb.sh -DNR_DEB_SHLIBDEPS=OFF   (SDL2 built from source, not
#                                         from libsdl2-dev: fixed Depends list)
#
# Steps performed:
#   1. configure a Release build in build-deb/ with the install prefix /usr
#   2. compile everything (launcher, engine, headless engine, nrtool, tests)
#   3. run the unit tests (no game data is needed for them)
#   4. run CPack with the DEB generator; dpkg-shlibdeps computes the
#      run-time dependencies (libsdl2-2.0-0, libc6, ...) automatically
#
# Build dependencies (Debian/Ubuntu):
#   sudo apt install build-essential cmake libsdl2-dev dpkg-dev file
#
# Install the result with:  sudo apt install ./dist/nrengine_*.deb
# The package never contains game files; the user selects their own disc.
set -e
ROOT=$(cd "$(dirname "$0")/.." && pwd)
BUILD=${BUILD_DIR:-$ROOT/build-deb}
DIST=${DIST_DIR:-$ROOT/dist}

for tool in cmake cpack dpkg-deb; do
    if ! command -v "$tool" >/dev/null 2>&1; then
        echo "error: '$tool' was not found. Run: sudo apt install build-essential cmake libsdl2-dev dpkg-dev" >&2
        exit 1
    fi
done

JOBS=$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 4)
cmake -S "$ROOT" -B "$BUILD" -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr "$@"
cmake --build "$BUILD" --parallel "$JOBS"
if [ ! -e "$BUILD/nrlauncher" ]; then
    echo "error: the launcher was not built because SDL2 is missing (sudo apt install libsdl2-dev)." >&2
    exit 1
fi
if [ -e "$BUILD/nrtests" ]; then
    echo "== unit tests"
    (cd "$BUILD" && env -u NR_DISC ./nrtests)
fi
mkdir -p "$DIST"
(cd "$BUILD" && cpack -G DEB -B "$DIST")
rm -rf "$DIST/_CPack_Packages"
echo
ls -l "$DIST"/*.deb
echo
echo "Inspect:  dpkg-deb --info  $DIST/nrengine_*.deb"
echo "          dpkg-deb --contents $DIST/nrengine_*.deb"
echo "Install:  sudo apt install $DIST/nrengine_*.deb"
