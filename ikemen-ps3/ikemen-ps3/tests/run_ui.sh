#!/bin/sh
# Runs the real source/main.c menu on the host with a fake pad and screen, dumping screenshots.
#   sh tests/run_ui.sh <content-root> <out-dir> [keys]
# <content-root> needs chars/, stages/, data/ like the PS3 USRDIR (e.g. a copy of the Ikemen screenpack repo).
# keys: u d x o l r . (default "ddxx" = down, down, confirm char, confirm stage)
set -e
cd "$(dirname "$0")/.."
ROOT=$(cd "$1" && pwd); OUT=$2; KEYS=${3:-ddxx}
mkdir -p "$OUT"
gcc -Wall -Wextra -O1 -g -fsanitize=address,undefined -Iinclude -Itests/host/stubs \
    -DIK_ROOT="\"$ROOT\"" -o "$OUT/ui_host" \
    tests/host/fake_platform.c source/main.c source/gfx_draw.c source/content.c source/def.c \
    source/sff.c source/sprite.c 2>&1 | grep -v 'format-truncation\|note:\|^ *[0-9]* |\|^ *|\|In function\|inlined from\|In file included\|~~~\|\^' || true
IK_KEYS="$KEYS" IK_SHOT_DIR="$OUT" "$OUT/ui_host"
echo "screenshots in $OUT"; ls "$OUT"/*.ppm | head -20
