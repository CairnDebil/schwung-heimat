#!/usr/bin/env bash
# Build both Heimat modules for Ableton Move (ARM64):
#   dist/heimat/        chain sound generator      -> dist/heimat-module.tar.gz
#   dist/heimat-solo/   standalone overtake module -> dist/heimat-solo-module.tar.gz
# Both come from src/dsp/heimat.c (HM_SOLO=0 / HM_SOLO=1).
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
mkdir -p build dist/heimat dist/heimat-solo
FLAGS="-O3 -g -shared -fPIC -std=gnu11 -pthread -Wall -Wextra -Isrc/dsp"

${CROSS_PREFIX}gcc $FLAGS -DHM_SOLO=0 src/dsp/heimat.c -o build/heimat.so -lm
${CROSS_PREFIX}gcc $FLAGS -DHM_SOLO=1 src/dsp/heimat.c -o build/heimat-solo.so -lm

# cat instead of cp: avoids ExtFS deallocation issues with Docker volumes
cat src/module.json      > dist/heimat/module.json
cat src/help.json        > dist/heimat/help.json
cat src/ui.js            > dist/heimat/ui.js
cat build/heimat.so      > dist/heimat/dsp.so
cat src/solo/module.json > dist/heimat-solo/module.json
cat src/solo/help.json   > dist/heimat-solo/help.json
cat src/solo/ui.js       > dist/heimat-solo/ui.js
cat build/heimat-solo.so > dist/heimat-solo/dsp.so
for d in heimat heimat-solo; do
    cat src/web/web_ui.html > dist/$d/web_ui.html    # browser editor, served by schwung-manager
    cat LICENSE > dist/$d/LICENSE
    cat NOTICE  > dist/$d/NOTICE
    chmod +x dist/$d/dsp.so
done

( cd dist && tar --owner=0 --group=0 -czf heimat-module.tar.gz heimat/ \
          && tar --owner=0 --group=0 -czf heimat-solo-module.tar.gz heimat-solo/ )

echo ""
echo "=== Build complete ==="
echo "dist/heimat-module.tar.gz       (Signal Chain sound generator)"
echo "dist/heimat-solo-module.tar.gz  (standalone instrument)"
echo "Install: ./scripts/install.sh"
