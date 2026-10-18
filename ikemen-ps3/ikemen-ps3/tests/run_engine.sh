#!/bin/sh
# Fight-engine tests against real Ikemen GO default content (Kung Fu Man), under ASan/UBSan.
#   sh tests/run_engine.sh <screenpack checkout>
set -e
cd "$(dirname "$0")/.."
R=${1:?usage: sh tests/run_engine.sh <screenpack checkout>}
K="$R/chars/kfm"
T=$(mktemp -d)
CC="gcc -w -g -fsanitize=address,undefined -fno-sanitize-recover=undefined -Iinclude"
CORE="source/text.c"
$CC tests/air_test.c source/air.c $CORE -o "$T/air_test";           "$T/air_test" "$K/kfm.air"
$CC tests/cmd_test.c source/cmd.c $CORE -o "$T/cmd_test";           "$T/cmd_test" "$K/kfm.cmd"
$CC tests/expr_test.c source/expr.c -lm -o "$T/expr_test";          "$T/expr_test"
$CC tests/cns_test.c source/cns.c source/expr.c $CORE -lm -o "$T/cns_test"; "$T/cns_test" "$K/kfm.cmd" "$K/kfm.cns"
$CC tests/fight_test.c source/fight.c source/air.c source/cmd.c source/cns.c source/expr.c $CORE \
    source/def.c source/content.c source/sff.c source/sprite.c source/gfx_draw.c -lm -o "$T/fight_test"
"$T/fight_test" "$K/kfm.def" pkgfiles/USRDIR/data
# hostile input: random mutations of the real files
$CC tests/fuzz_air.c source/air.c $CORE -o "$T/fz1";                "$T/fz1" "$K/kfm.air"
$CC tests/fuzz_cmd.c source/cmd.c $CORE -o "$T/fz2";                "$T/fz2" "$K/kfm.cmd"
$CC tests/fuzz_cns.c source/cns.c source/expr.c $CORE -lm -o "$T/fz3"; "$T/fz3" "$K/kfm.cns"
rm -rf "$T"
echo "all engine checks passed"
