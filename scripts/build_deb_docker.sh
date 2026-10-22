#!/bin/sh
set -e
ROOT=$(cd "$(dirname "$0")/.." && pwd)
IMAGE=${NR_DEB_IMAGE:-debian:13}
docker run --rm -v "$ROOT:/src:ro" -v "$ROOT/dist:/dist" "$IMAGE" sh -c '
set -e
export DEBIAN_FRONTEND=noninteractive
apt-get update -qq
apt-get install -y -qq --no-install-recommends build-essential cmake libsdl3-dev dpkg-dev file ca-certificates >/dev/null
cp -r /src /tmp/nrengine
rm -rf /tmp/nrengine/build-deb /tmp/nrengine/dist
BUILD_DIR=/tmp/build-deb DIST_DIR=/dist sh /tmp/nrengine/scripts/build_deb.sh
chown -R '"$(id -u):$(id -g)"' /dist
'
