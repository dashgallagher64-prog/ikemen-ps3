#!/bin/sh
# Builds the portable code with the host compiler and runs it against a fake content tree.
set -e
cd "$(dirname "$0")/.."
T=$(mktemp -d)
mkdir -p "$T/chars/kfm" "$T/chars/ryu" "$T/stages" "$T/data/neo"

cat > "$T/chars/kfm/kfm.def" <<'EOF'
; test character
[Info]
name = "Kung Fu Man"   ; trailing comment
displayname = "Kung Fu Man"
[Files]
sprite = kfm.sff
EOF
cat > "$T/chars/ryu/ryu.def" <<'EOF'
[Info]
name = "Ryu"
[Files]
sprite = ryu.sff
EOF
cat > "$T/stages/stage0.def" <<'EOF'
[Info]
name = "Training Room"
EOF
printf '[Info]\nname = x\n' > "$T/data/system.def"
printf '[Info]\nname = neo\n' > "$T/data/neo/system.def"

python3 - "$T" <<'EOF'
import struct, sys
t = sys.argv[1]
# SFF v1: sig(12) + ver(lo3,lo2,lo1,hi) + groups(u32) + images(u32) + ...
h1 = b"ElecbyteSpr\0" + bytes([0, 1, 0, 1]) + struct.pack("<II", 3, 42) + b"\0" * 40
# SFF v2: images at 0x28, palettes at 0x30
h2 = bytearray(b"ElecbyteSpr\0" + bytes([0, 0, 0, 2]) + b"\0" * 48)
struct.pack_into("<I", h2, 0x28, 1234)
struct.pack_into("<I", h2, 0x30, 7)
open(t + "/chars/kfm/kfm.sff", "wb").write(h1)
open(t + "/chars/ryu/ryu.sff", "wb").write(bytes(h2))
EOF

gcc -Wall -Wextra -O1 -Iinclude -fsanitize=address,undefined -o "$T/host_test" \
    tests/host_test.c source/def.c source/sff.c source/content.c
"$T/host_test" "$T"
rm -rf "$T"
