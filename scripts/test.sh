#!/usr/bin/env bash
# Offline tests, no Move needed:
#   - the DSP engine, as chain module (HM_SOLO=0) and as standalone (HM_SOLO=1)
#   - the Solo user interface against a fake host (needs Node 18+)
#   - the browser editor in Chromium (needs Playwright; skipped without it)
#   - editor and manual in step with the parameter table (needs Python)
# SAN=1 builds the DSP tests with AddressSanitizer + UBSan.
# SCREENS=1 also renders PNG screenshots into docs/screens (needs Python + Pillow).
set -e
cd "$(dirname "$0")/.."
BIN_DIR="$(mktemp -d)"
FLAGS="-O2 -std=gnu11 -Wall -Wextra -pthread"
[ -n "$SAN" ] && FLAGS="-O1 -g -fsanitize=address,undefined -DHM_SANITIZE -fno-omit-frame-pointer -std=gnu11 -pthread"
for solo in 0 1; do
    echo "===== DSP test, HM_SOLO=$solo ====="
    gcc $FLAGS -DHM_SOLO=$solo -Isrc/dsp tests/render_test.c -o "$BIN_DIR/render_test_$solo" -lm
    "$BIN_DIR/render_test_$solo" | tail -n 3
done
if command -v node >/dev/null 2>&1; then
    echo "===== UI test ====="
    node tests/ui_test.mjs | tail -n 1
    if [ -n "$SCREENS" ]; then python3 tests/render_screens.py; fi
    if node -e "require('playwright')" >/dev/null 2>&1; then
        echo "===== Web editor test ====="
        node tests/web_test.mjs | tail -n 1      # uses the state the HM_SOLO=1 DSP test just wrote
    else
        echo "(playwright not found: web editor test skipped)"
    fi
else
    echo "(node not found: UI test skipped)"
fi
if command -v python3 >/dev/null 2>&1; then
    echo "===== Web files ====="
    python3 scripts/gen_web.py --check
fi

