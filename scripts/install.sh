#!/usr/bin/env bash
# Install both Heimat modules on a Move that runs Schwung (SSH to move.local).
set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_ROOT="$(dirname "$SCRIPT_DIR")"
cd "$REPO_ROOT"

if [ ! -d "dist/heimat" ] || [ ! -d "dist/heimat-solo" ]; then
    echo "Error: dist/ not found. Run ./scripts/build.sh first."
    exit 1
fi

BASE=/data/UserData/schwung/modules
install_one() {
    local src="$1" dest="$2"
    echo "-> $dest"
    ssh ableton@move.local "mkdir -p $dest"
    scp -r "$src"/* ableton@move.local:"$dest"/
    ssh ableton@move.local "chmod -R a+rw $dest"
}

echo "=== Installing Heimat ==="
install_one dist/heimat      "$BASE/sound_generators/heimat"
install_one dist/heimat-solo "$BASE/overtake/heimat-solo"
echo ""
echo "Restart the Move so Schwung loads the modules."
