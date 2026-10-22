#!/bin/sh
set -e
ROOT=$(cd "$(dirname "$0")/.." && pwd)
BUILD=${BUILD_DIR:-$ROOT/build-deb}
DIST=${DIST_DIR:-$ROOT/dist}

for tool in cmake cpack dpkg-deb; do
    if ! command -v "$tool" >/dev/null 2>&1; then
        echo "error: '$tool' was not found. Run: sudo apt install build-essential cmake libsdl3-dev dpkg-dev file" >&2
        exit 1
    fi
done

JOBS=$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 4)
cmake -S "$ROOT" -B "$BUILD" -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr \
      -DNR_BUILD_SDL=ON -DNR_REQUIRE_SDL=ON -DNR_SDL_VERSION=${NR_SDL_VERSION:-3} "$@"
cmake --build "$BUILD" --parallel "$JOBS"
if [ ! -e "$BUILD/nrlauncher" ]; then
    echo "error: the launcher was not built (sudo apt install libsdl3-dev)." >&2
    exit 1
fi
echo "== unit tests"
(cd "$BUILD" && env -u NR_DISC ctest --output-on-failure)
mkdir -p "$DIST"
(cd "$BUILD" && cpack -G DEB -B "$DIST")
rm -rf "$DIST/_CPack_Packages"
for deb in "$DIST"/*.deb; do
    dpkg-deb -f "$deb"
done
ls -l "$DIST"/*.deb
