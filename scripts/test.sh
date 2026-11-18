#!/usr/bin/env bash
# Offline tests, no Move needed:
#   - the DSP engine
#   - the on-screen user interface against a fake host (needs Node 18+)
#   - the browser editor in Chromium (needs Playwright; skipped without it)
#   - editor and manual in step with the parameter table (needs Python)
# SAN=1 builds the DSP tests with AddressSanitizer + UBSan.
# SCREENS=1 also renders PNG screenshots into docs/screens (needs Python + Pillow).
set -e
cd "$(dirname "$0")/.."
BIN_DIR="$(mktemp -d)"
FLAGS="-O2 -std=gnu11 -Wall -Wextra -pthread"
[ -n "$SAN" ] && FLAGS="-O1 -g -fsanitize=address,undefined -DHM_SANITIZE -fno-omit-frame-pointer -std=gnu11 -pthread"
echo "===== DSP test ====="
gcc $FLAGS -Isrc/dsp tests/render_test.c -o "$BIN_DIR/render_test" -lm
"$BIN_DIR/render_test" | tail -n 3
if command -v node >/dev/null 2>&1; then
    echo "===== UI test ====="
    node tests/ui_test.mjs | tail -n 1
    if [ -n "$SCREENS" ]; then python3 tests/render_screens.py; fi
    if node -e "require('playwright')" >/dev/null 2>&1; then
        echo "===== Web editor test ====="
        node tests/web_test.mjs | tail -n 1      # uses the state the DSP test just wrote
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

