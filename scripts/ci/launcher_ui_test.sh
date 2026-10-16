#!/bin/sh
# Drives the launcher through every tab and control with scripted clicks
# (NRLAUNCHER_TEST_CLICKS) under a virtual X server. Used by CI with an
# AddressSanitizer/UndefinedBehaviorSanitizer build: any memory error makes
# the launcher abort and this script fail. No game data is needed.
#
# Usage: scripts/ci/launcher_ui_test.sh <build dir>
set -eu
BUILD=${1:?usage: launcher_ui_test.sh <build dir>}
LAUNCHER="$BUILD/nrlauncher"
[ -x "$LAUNCHER" ] || { echo "launcher not found: $LAUNCHER" >&2; exit 1; }

# For every tab: open each control in both columns and pick the item below it.
CLICKS=""
for tab in 118 321 523 725 928; do
    CLICKS="$CLICKS;$tab,145"
    for x in 450 1030; do
        for y in 209 260 311 361 411 462 513; do
            CLICKS="$CLICKS;$x,$y;$x,$((y + 56))"
        done
    done
done
CLICKS=${CLICKS#;}
N=$(printf '%s' "$CLICKS" | tr ';' '\n' | wc -l)

WORK=$(mktemp -d)
cp "$LAUNCHER" "$WORK/"                      # portable mode: settings stay in $WORK
OUT="$WORK/launcher_final.png"
cd "$WORK"
SDL_AUDIODRIVER=dummy NRLAUNCHER_TEST_CLICKS="$CLICKS" NRLAUNCHER_SCREENSHOT="$OUT" \
NRLAUNCHER_SCREENSHOT_FRAME=$((N * 8 + 20)) timeout 600 ./nrlauncher
[ -s "$OUT" ] || { echo "the launcher did not reach the end of the click script" >&2; exit 1; }
echo "launcher UI test: $N clicks, no errors"

# Every launcher language must render without errors as well.
for lang in en-US en-GB en-CA pt-BR pt-PT es-ES es-MX es-419 es-AR es-US \
            zh-Hans-CN zh-Hans-SG zh-Hant-TW zh-Hant-HK zh-Hant-MO ja-JP ko-KR; do
    for tab in 0 1 2 3 4; do
        rm -f "$OUT"
        SDL_AUDIODRIVER=dummy NRLAUNCHER_LANG=$lang NRLAUNCHER_TAB=$tab NRLAUNCHER_SCREENSHOT="$OUT" \
        timeout 120 ./nrlauncher
        [ -s "$OUT" ] || { echo "launcher failed for language $lang, tab $tab" >&2; exit 1; }
    done
done
echo "launcher language test: all languages and tabs rendered"
rm -rf "$WORK"
