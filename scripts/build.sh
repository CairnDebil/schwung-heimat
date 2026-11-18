#!/usr/bin/env bash
# Build Heimat for Ableton Move (ARM64):
#   dist/heimat-solo/  the overtake instrument -> dist/heimat-solo-module.tar.gz
# (The module keeps the id heimat-solo, which it has had since the start, so an
# installed copy updates in place. The track-slot module is suspended: see
# attic/track-module.)
#
# Uses Docker for cross-compilation unless CROSS_PREFIX is set
# (e.g. CROSS_PREFIX=aarch64-linux-gnu- for a local toolchain).
set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_ROOT="$(dirname "$SCRIPT_DIR")"
IMAGE_NAME="schwung-heimat-builder"

if [ -z "$CROSS_PREFIX" ] && [ ! -f "/.dockerenv" ]; then
    echo "=== Heimat build (via Docker) ==="
    if ! docker image inspect "$IMAGE_NAME" &>/dev/null; then
        echo "Building Docker image (first time only)..."
        docker build -t "$IMAGE_NAME" -f "$SCRIPT_DIR/Dockerfile" "$REPO_ROOT"
    fi
    docker run --rm \
        -v "$REPO_ROOT:/build" \
        -u "$(id -u):$(id -g)" \
        -w /build \
        "$IMAGE_NAME" \
        bash ./scripts/build.sh
    echo "=== Done ==="
    exit 0
fi

CROSS_PREFIX="${CROSS_PREFIX:-aarch64-linux-gnu-}"
cd "$REPO_ROOT"
echo "=== Building Heimat (cross prefix: $CROSS_PREFIX) ==="
if command -v python3 >/dev/null 2>&1; then python3 scripts/gen_web.py; fi   # editor and manual follow the parameter table

rm -rf build dist
mkdir -p build dist/heimat-solo
FLAGS="-O3 -g -shared -fPIC -std=gnu11 -pthread -Wall -Wextra -Isrc/dsp"

${CROSS_PREFIX}gcc $FLAGS src/dsp/heimat.c -o build/heimat-solo.so -lm

# cat instead of cp: avoids ExtFS deallocation issues with Docker volumes
D=dist/heimat-solo
cat src/module.json        > $D/module.json
cat src/help.json          > $D/help.json
cat src/ui.js              > $D/ui.js
cat build/heimat-solo.so   > $D/dsp.so
cat src/web/web_ui.html    > $D/web_ui.html    # browser editor, served by schwung-manager
cat LICENSE                > $D/LICENSE
cat NOTICE                 > $D/NOTICE
chmod +x $D/dsp.so

( cd dist && tar --owner=0 --group=0 -czf heimat-solo-module.tar.gz heimat-solo/ )

echo ""
echo "=== Build complete ==="
echo "dist/heimat-solo-module.tar.gz  (Heimat)"
echo "Install: ./scripts/install.sh"
