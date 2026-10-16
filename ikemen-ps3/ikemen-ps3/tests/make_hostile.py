#!/usr/bin/env python3
"""Writes deliberately hostile SFF files into a directory (for tests/run_hostile.sh).
Each one is something a corrupt download or a malicious 'character' could contain."""
import os, struct, sys

out = sys.argv[1]
os.makedirs(out, exist_ok=True)
SIG = b"ElecbyteSpr\0"


def v2_header(first, nspr, palhdr, npal, lofs, tofs):
    h = bytearray(SIG + bytes([0, 0, 0, 2]) + b"\0" * 48)
    struct.pack_into("<I", h, 0x24, first)
    struct.pack_into("<I", h, 0x28, nspr)
    struct.pack_into("<I", h, 0x2C, palhdr)
    struct.pack_into("<I", h, 0x30, npal)
    struct.pack_into("<I", h, 0x34, lofs)
    struct.pack_into("<I", h, 0x3C, tofs)
    return h


def v2_sprite(group, image, w, h, link, fmt, depth, ofs, size, pal=0, flags=0):
    return struct.pack("<HHHHhhHBBIIHH", group, image, w, h, 0, 0, link, fmt, depth, ofs, size, pal, flags)


def write(name, data):
    with open(os.path.join(out, name), "wb") as f:
        f.write(data)


# 1. v2 header claims ~2.3 million sprites (file is 64 MB of zeros after the header)
n = (64 << 20) // 28
write("v2_many_sprites.sff", bytes(v2_header(0x40, n, 0x40, 0, 0, 0)) + b"\0" * ((64 << 20) - 0x40))

# 2. v2 claims 4 million palettes (each 1 KB once allocated) in a 64 MB file
n = (64 << 20) // 16
write("v2_many_palettes.sff", bytes(v2_header(0x40, 1, 0x40, n, 0, 0)) + b"\0" * ((64 << 20) - 0x40))

# 3. v2: 20000 sprites, each RLE8 and claiming 8192x8192 (64 MB when decoded)
n = 20000
body = bytearray(v2_header(0x40, n, 0x40, 0, 0, 0))
for i in range(n):
    body += v2_sprite(0, i, 8192, 8192, 0, 2, 8, 0x40 + n * 28, 8)
body += struct.pack("<I", 0) + bytes([0x41, 7, 0x41, 7])
write("v2_huge_dims.sff", bytes(body))

# 4. v1: header chain points back at itself (cycle) with a 2000x2000 PCX each time
pcx = bytearray(128)
pcx[2] = 1; pcx[3] = 8
struct.pack_into("<HHHH", pcx, 4, 0, 0, 2999, 2999)
struct.pack_into("<H", pcx, 66, 3000)
data = bytes(pcx) + bytes([0xC0 | 63, 5]) * 20 + b"\x0c" + bytes(768)
sub = struct.pack("<IIhhHHHB", 0x200, len(data), 0, 0, 0, 0, 0, 0) + b"\0" * 13
v1 = bytearray(SIG + bytes([0, 1, 0, 1]) + struct.pack("<IIII", 1, 40, 0x200, 0) + b"\0" * 0x1C0)
v1 = v1.ljust(0x200, b"\0") + sub + data          # next=0x200 -> its own header: a cycle
write("v1_cycle.sff", bytes(v1))

# 5. v2 lz5 bomb: valid tiny stream expanding to the maximum claimed size, many sprites share it
n = 500
body = bytearray(v2_header(0x40, n, 0x40, 0, 0, 0))
for i in range(n):
    body += v2_sprite(0, i, 4096, 4096, 0, 4, 8, 0x40 + n * 28, 16)
body += struct.pack("<I", 0) + bytes([0x00, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20])
write("v2_lz5_bomb.sff", bytes(body))

# 6. only the 64-byte header, then nothing
write("v2_header_only.sff", bytes(v2_header(0x40, 3, 0x40, 3, 0, 0)))

# 7. all 0xFF
write("all_ff.sff", SIG + bytes([0, 0, 0, 2]) + b"\xff" * 4096)

# 8. file larger than the loader's size cap (100 MB of zeros after a valid signature)
write("oversized.sff", bytes(v2_header(0x40, 1, 0x40, 0, 0, 0)) + b"\0" * ((100 << 20) - 0x40))

# 9. v2: 500 sprites that each decode to 2000x2000 (4 MB): a total of ~2 GB if nothing stopped it.
#    Every sprite is individually legal, so only the per-file memory budget can catch this.
n = 500
body = bytearray(v2_header(0x40, n, 0x40, 0, 0, 0))
for i in range(n):
    body += v2_sprite(0, i, 2000, 2000, 0, 2, 8, 0x40 + n * 28, 8)
body += struct.pack("<I", 0) + bytes([0x41, 7, 0x41, 7])
write("v2_budget.sff", bytes(body))

# 10. v1 cycle again, but each sprite is individually legal (2000x2000) so the budget is what stops it
pcx = bytearray(128)
pcx[2] = 1; pcx[3] = 8
struct.pack_into("<HHHH", pcx, 4, 0, 0, 1999, 1999)
struct.pack_into("<H", pcx, 66, 2000)
data = bytes(pcx) + bytes([0xC0 | 63, 5]) * 20 + b"\x0c" + bytes(768)
sub = struct.pack("<IIhhHHHB", 0x200, len(data), 0, 0, 0, 0, 0, 0) + b"\0" * 13
v1 = bytearray(SIG + bytes([0, 1, 0, 1]) + struct.pack("<IIII", 1, 40, 0x200, 0) + b"\0" * 0x1C0)
v1 = v1.ljust(0x200, b"\0") + sub + data
write("v1_budget.sff", bytes(v1))

print("wrote", len(os.listdir(out)), "files to", out)
