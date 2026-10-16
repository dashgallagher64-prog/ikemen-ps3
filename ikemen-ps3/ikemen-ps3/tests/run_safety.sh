#!/bin/sh
# Everything that backs the "cannot brick the console" claims, in one command:
#   sh tests/run_safety.sh                      static gate + path guard + hostile-file tests
#   sh tests/run_safety.sh <screenpack checkout> ... plus fuzzing mutated real SFFs (ASan/UBSan and a memory cap)
set -e
cd "$(dirname "$0")/.."
T=$(mktemp -d); trap 'rm -rf "$T"' EXIT
CC="gcc -Wall -Wextra -O1 -g -Iinclude"

echo "== 1. static gate: no delete/rename/system/firmware calls, one log file, plain HDD-game package"
sh tests/check_safety.sh

echo; echo "== 2. .def files cannot point outside their own folder"
$CC -Wno-format-truncation -o "$T/safe_rel" tests/safe_rel_test.c source/def.c
"$T/safe_rel" | grep -c ' ok$' | sed 's/^/   cases ok: /'
"$T/safe_rel" >/dev/null

echo; echo "== 3. hostile SFF files stay small and fast (200 MB address-space cap)"
python3 -I tests/make_hostile.py "$T/hostile" >/dev/null
$CC -o "$T/adv_test" tests/adv_test.c source/sprite.c
for f in "$T"/hostile/*.sff; do
    n=$(basename "$f" .sff)
    out=$("$T/adv_test" "$f" 200) || { echo "   FAIL $n: $out"; exit 1; }
    case "$n" in   # these must be refused outright, the rest may load but only within budget
        all_ff|oversized|v2_header_only|v2_many_sprites) case "$out" in rc=-*) ;; *) echo "   FAIL $n was not refused: $out"; exit 1 ;; esac ;;
    esac
    printf '   ok  %-22s %s\n' "$n" "$out"
done

if [ -n "$1" ]; then
    R=$1
    SEEDS="$R/chars/kfm/kfm.sff $R/chars/kfm/intro.sff $R/chars/kfm/ending.sff $R/font/f-6x9.sff $R/font/arcade.sff $R/stages/stageZ.sff"
    echo; echo "== 4. fuzz: corrupt real SFFs thousands of times"
    $CC -fsanitize=address,undefined -fno-sanitize-recover=undefined -o "$T/fuzz_asan" tests/fuzz_sff.c source/sprite.c
    for seed in 1 2; do "$T/fuzz_asan" 1500 $seed "$T/mut.sff" $SEEDS | sed 's/^/   [asan] /'; done
    $CC -o "$T/fuzz_plain" tests/fuzz_sff.c source/sprite.c
    for seed in 3 4; do FUZZ_LIMIT_MB=200 FUZZ_HEAP_MB=64 "$T/fuzz_plain" 3000 $seed "$T/mut.sff" $SEEDS | sed 's/^/   [200MB cap] /'; done
fi
echo; echo "all safety checks passed"
