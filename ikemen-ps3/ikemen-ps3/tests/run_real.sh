#!/bin/sh
# Regression test against real SFF files (Ikemen GO's default content).
#   git clone --depth 1 https://github.com/ikemen-engine/Ikemen_GO-Elecbyte-Screenpack
#   sh tests/run_real.sh Ikemen_GO-Elecbyte-Screenpack
set -e
cd "$(dirname "$0")/.."
R=${1:?usage: sh tests/run_real.sh <screenpack checkout>}
T=$(mktemp -d)
gcc -Wall -Wextra -O1 -g -Iinclude -fsanitize=address,undefined -o "$T/sff_dump" tests/sff_dump.c source/sprite.c
gcc -Wall -Wextra -O1 -g -Iinclude -fsanitize=address,undefined -o "$T/sff_filter_test" tests/sff_filter_test.c source/sprite.c

expect() {
    out=$("$T/sff_dump" "$R/$1" | head -1)
    case "$out" in
        *"$2"*) echo "ok    $1" ;;
        *) echo "FAIL  $1"; echo "  want: $2"; echo "  got:  $out"; rm -rf "$T"; exit 1 ;;
    esac
}
# v2: LZ5 + one PNG sprite (PNG is not implemented yet, so exactly 1 unsupported)
expect chars/kfm/kfm.sff     "v2 sprites=281 palettes=16 ok=280 trunc=0 format=0 unsupported=1"
# v1: PCX with shared palettes
expect chars/kfm/intro.sff   "v1 sprites=12 palettes=2 ok=12 trunc=0 format=0 unsupported=0 badlink=0"
expect chars/kfm/ending.sff  "v1 sprites=7 palettes=1 ok=7"
# v2: RLE8
expect font/f-6x9.sff        "ok=94"
expect font/arcade.sff       "ok=67"

"$T/sff_filter_test" "$R/chars/kfm/kfm.sff" "$R/chars/kfm/intro.sff" "$R/chars/kfm/ending.sff" \
                     "$R/font/f-6x9.sff" "$R/stages/stageZ.sff" "$R/chars/kfm_zss/kfm.sff"
rm -rf "$T"
echo "all real-asset checks passed"
