#!/usr/bin/env bash
# Install Heimat on a Move that runs Schwung (SSH to move.local).
set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_ROOT="$(dirname "$SCRIPT_DIR")"
cd "$REPO_ROOT"

if [ ! -d "dist/heimat-solo" ]; then
    echo "Error: dist/ not found. Run ./scripts/build.sh first."
    exit 1
fi

DEST=/data/UserData/schwung/modules/overtake/heimat-solo
echo "=== Installing Heimat ==="
ssh ableton@move.local "mkdir -p $DEST"
scp -r dist/heimat-solo/* ableton@move.local:"$DEST"/
ssh ableton@move.local "chmod -R a+rw $DEST"
echo ""
echo "Restart the Move so Schwung loads the module."
echo "(An old track-slot Heimat, if you installed one, stays in"
echo " modules/sound_generators/heimat until you remove it.)"
