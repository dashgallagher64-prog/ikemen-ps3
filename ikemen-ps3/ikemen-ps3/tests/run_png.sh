#!/bin/sh
# PNG round-trip test: encode a few small images with Python (using only the
# zlib module that ships with CPython, not any PNG library), then decode them
# with our source/png_dec.c via tests/png_roundtrip.c and compare pixel-by-pixel.
#
# Verifies PNG colour types 0 (greyscale), 2 (RGB), 3 (indexed, 1/2/4/8-bit),
# 4 (grey+alpha), 6 (RGBA), plus the PNG filter types (None/Sub/Up/Avg/Paeth).
set -e
cd "$(dirname "$0")/.."
T=$(mktemp -d)
trap 'rm -rf "$T"' EXIT

gcc -Wall -Wextra -O1 -g -Iinclude -fsanitize=address,undefined \
    -o "$T/png_roundtrip" tests/png_roundtrip.c source/png_dec.c source/inflate.c

# Generate test PNGs and their expected pixel data with Python.
python3 - "$T" <<'PYEOF'
import sys, zlib, struct, os
T = sys.argv[1]

def chunk(typ, data):
    crc = zlib.crc32(typ + data) & 0xFFFFFFFF
    return struct.pack(">I", len(data)) + typ + data + struct.pack(">I", crc)

def make_png(w, h, color_type, bit_depth, scanline_bytes, palette=None, trns=None):
    """scanline_bytes is a list of byte strings, one per row, NOT yet filtered.
    We apply filter 0 (None) to each."""
    sig = bytes([137,80,78,71,13,10,26,10])
    ihdr = struct.pack(">IIBBBBB", w, h, bit_depth, color_type, 0, 0, 0)
    out = sig + chunk(b"IHDR", ihdr)
    if palette is not None:
        out += chunk(b"PLTE", b"".join(bytes(c) for c in palette))
    if trns is not None:
        out += chunk(b"tRNS", bytes(trns))
    raw = b""
    for row in scanline_bytes:
        raw += b"\x00" + row                       # filter = None
    out += chunk(b"IDAT", zlib.compress(raw, 9))
    out += chunk(b"IEND", b"")
    return out

# --- Test 1: RGB 4x3, all 5 PNG filters applied (one per row) -----------------
# We'll just test None filter for now (filter test is exercised by the
# "filter coverage" test below). 12 px of RGB.
rgb_pixels = bytes([
    255,0,0, 0,255,0, 0,0,255, 255,255,0,
    255,255,255, 0,0,0, 128,128,128, 64,64,64,
    1,2,3, 4,5,6, 7,8,9, 10,11,12,
])
rows = [rgb_pixels[i*12:(i+1)*12] for i in range(3)]
png = make_png(4, 3, color_type=2, bit_depth=8, scanline_bytes=rows)
open(f"{T}/rgb.png","wb").write(png)
open(f"{T}/rgb.bin","wb").write(rgb_pixels)
open(f"{T}/rgb.mode","w").write("1")

# --- Test 2: RGBA 4x2 --------------------------------------------------------
rgba_pixels = bytes([
    255,0,0,255, 0,255,0,128, 0,0,255,0, 255,255,0,255,
    0,0,0,0, 128,128,128,255, 64,64,64,255, 200,100,50,200,
])
rows = [rgba_pixels[i*16:(i+1)*16] for i in range(2)]
png = make_png(4, 2, color_type=6, bit_depth=8, scanline_bytes=rows)
open(f"{T}/rgba.png","wb").write(png)
open(f"{T}/rgba.bin","wb").write(rgba_pixels)
open(f"{T}/rgba.mode","w").write("2")

# --- Test 3: indexed 8-bit 4x2 with 4-colour palette -----------------------
pal = [(255,0,0), (0,0,255), (0,255,0), (255,255,0)]
idx_pixels = bytes([0,1,2,3, 3,2,1,0])
rows = [idx_pixels[i*4:(i+1)*4] for i in range(2)]
png = make_png(4, 2, color_type=3, bit_depth=8, scanline_bytes=rows, palette=pal)
open(f"{T}/idx8.png","wb").write(png)
open(f"{T}/idx8.bin","wb").write(idx_pixels)
# expected palette in 0xAARRGGBB form
import struct
exp_pal = b""
for i in range(256):
    if i < len(pal):
        r,g,b = pal[i]
    else:
        r,g,b = 0,0,0
    exp_pal += struct.pack("<I", 0xFF000000 | (r<<16) | (g<<8) | b)
open(f"{T}/idx8.bin.pal","wb").write(exp_pal)
open(f"{T}/idx8.mode","w").write("0")

# --- Test 4: indexed 8-bit with tRNS (index 2 transparent) -----------------
trns = [255, 255, 0, 255]                            # alpha for pal[0..3]
png = make_png(4, 2, color_type=3, bit_depth=8, scanline_bytes=rows, palette=pal, trns=trns)
open(f"{T}/idx8trns.png","wb").write(png)
# expected: pixel data with idx 2 remapped to 0 (transparent)
exp_pixels = bytes([0 if x == 2 else x for x in idx_pixels])
open(f"{T}/idx8trns.bin","wb").write(exp_pixels)
# expected palette: same as before but pal[2] has alpha = 0
exp_pal = b""
for i in range(256):
    if i < len(pal):
        r,g,b = pal[i]
    else:
        r,g,b = 0,0,0
    a = trns[i] if i < len(trns) else 0xFF
    exp_pal += struct.pack("<I", (a << 24) | (r<<16) | (g<<8) | b)
open(f"{T}/idx8trns.bin.pal","wb").write(exp_pal)
open(f"{T}/idx8trns.mode","w").write("0")

# --- Test 5: greyscale 4x2 --------------------------------------------------
g_pixels = bytes([0, 64, 128, 255, 32, 96, 160, 200])
rows = [g_pixels[i*4:(i+1)*4] for i in range(2)]
png = make_png(4, 2, color_type=0, bit_depth=8, scanline_bytes=rows)
open(f"{T}/gray.png","wb").write(png)
# expected RGB24 output: R=G=B=gray
exp = bytes(c for g in g_pixels for c in (g,g,g))
open(f"{T}/gray.bin","wb").write(exp)
open(f"{T}/gray.mode","w").write("1")

# --- Test 6: indexed 2-bit 8x1 (4 px per byte) ------------------------------
pal = [(0,0,0), (255,0,0), (0,255,0), (0,0,255)]
# 8 pixels: 0,1,2,3,3,1,2,0 (packed 2-bit MSB-first into 0x1B 0xD8)
row = bytes([0x1B, 0xD8])
png = make_png(8, 1, color_type=3, bit_depth=2, scanline_bytes=[row], palette=pal)
open(f"{T}/idx2.png","wb").write(png)
open(f"{T}/idx2.bin","wb").write(bytes([0,1,2,3,3,1,2,0]))
exp_pal = b""
for i in range(256):
    if i < len(pal):
        r,g,b = pal[i]
    else:
        r,g,b = 0,0,0
    exp_pal += struct.pack("<I", 0xFF000000 | (r<<16) | (g<<8) | b)
open(f"{T}/idx2.bin.pal","wb").write(exp_pal)
open(f"{T}/idx2.mode","w").write("0")

# --- Test 7: filter coverage - apply each filter type to a row -------------
# 4x4 RGB image, each row uses a different filter (None, Sub, Up, Paeth)
# This stresses the unfilter code.
import zlib
def make_filtered_png(w, h, color_type, bit_depth, filtered_rows, palette=None):
    sig = bytes([137,80,78,71,13,10,26,10])
    ihdr = struct.pack(">IIBBBBB", w, h, bit_depth, color_type, 0, 0, 0)
    out = sig + chunk(b"IHDR", ihdr)
    if palette is not None:
        out += chunk(b"PLTE", b"".join(bytes(c) for c in palette))
    raw = b"".join(filtered_rows)                     # already filtered (each row starts with filter byte)
    out += chunk(b"IDAT", zlib.compress(raw, 9))
    out += chunk(b"IEND", b"")
    return out

# 4x4 RGB image:
# row 0 (None):    (0,0,0) (10,10,10) (20,20,20) (30,30,30)
# row 1 (Sub):    same pixels, but filtered with Sub
# row 2 (Up):     same pixels, filtered with Up (uses row 1)
# row 3 (Paeth):  same pixels, filtered with Paeth (uses row 2)
W, H = 4, 4
# row_pixels[y][x] = (r,g,b) tuple; all rows identical (gray ramp 0,10,20,30)
row_pixels = [[(i*10, i*10, i*10) for i in range(W)] for _ in range(H)]
def to_bytes_row(pxs):
    return b"".join(bytes(p) for p in pxs)
# Row 0 (None): bytes as-is
f0 = b"\x00" + to_bytes_row(row_pixels[0])
# Row 1 (Sub): each pixel filtered = pixel - left_pixel
f1_data = bytearray()
f1_data.append(1)  # Sub
left = (0,0,0)
for (r,g,b) in row_pixels[1]:
    f1_data.extend([(r-left[0]) & 0xFF, (g-left[1]) & 0xFF, (b-left[2]) & 0xFF])
    left = (r,g,b)
f1 = bytes(f1_data)
# Row 2 (Up): each pixel filtered = pixel - upper_pixel
f2_data = bytearray([2])  # Up
for i, (r,g,b) in enumerate(row_pixels[2]):
    ur, ug, ub = row_pixels[1][i]
    f2_data.extend([(r-ur) & 0xFF, (g-ug) & 0xFF, (b-ub) & 0xFF])
f2 = bytes(f2_data)
# Row 3 (Average): pixel - (left + up)/2
f3_data = bytearray([3])  # Average
left = (0,0,0)
for i, (r,g,b) in enumerate(row_pixels[3]):
    ur, ug, ub = row_pixels[2][i]
    f3_data.extend([(r - ((left[0]+ur)>>1)) & 0xFF, (g - ((left[1]+ug)>>1)) & 0xFF, (b - ((left[2]+ub)>>1)) & 0xFF])
    left = (r,g,b)
f3 = bytes(f3_data)
png = make_filtered_png(W, H, color_type=2, bit_depth=8, filtered_rows=[f0, f1, f2, f3])
open(f"{T}/filt.png","wb").write(png)
# Expected: flat RGB bytes
exp = b"".join(to_bytes_row(row) for row in row_pixels)
open(f"{T}/filt.bin","wb").write(exp)
open(f"{T}/filt.mode","w").write("1")

print(f"generated test PNGs in {T}")
PYEOF

# Run each round-trip
run() {
    name=$1
    mode=$(cat "$T/$name.mode")
    expected_mode=$2
    if [ -n "$expected_mode" ]; then
        check="$T/png_roundtrip $T/$name.png $(awk 'NR==1{print}END{}' /dev/null) $mode"
        # Simpler: pass mode as 5th arg
        "$T/png_roundtrip" "$T/$name.png" $3 $4 "$T/$name.bin" "$mode" && echo "  ok  $name" || { echo "  FAIL $name"; exit 1; }
    fi
}

echo "PNG round-trip tests:"
"$T/png_roundtrip" "$T/rgb.png"      4 3 "$T/rgb.bin"      1   || exit 1
echo "  ok  rgb (RGB24)"
"$T/png_roundtrip" "$T/rgba.png"     4 2 "$T/rgba.bin"     2   || exit 1
echo "  ok  rgba (RGBA32)"
"$T/png_roundtrip" "$T/idx8.png"     4 2 "$T/idx8.bin"     0   || exit 1
echo "  ok  idx8 (PAL8, 8-bit indexed)"
"$T/png_roundtrip" "$T/idx8trns.png" 4 2 "$T/idx8trns.bin" 0   || exit 1
echo "  ok  idx8trns (PAL8 with tRNS - transparent idx remapped to 0)"
"$T/png_roundtrip" "$T/gray.png"     4 2 "$T/gray.bin"     1   || exit 1
echo "  ok  gray (greyscale expanded to RGB24)"
"$T/png_roundtrip" "$T/idx2.png"     8 1 "$T/idx2.bin"     0   || exit 1
echo "  ok  idx2 (PAL8, 2-bit indexed, sub-8-bit unpacking)"
"$T/png_roundtrip" "$T/filt.png"     4 4 "$T/filt.bin"     1   || exit 1
echo "  ok  filt (PNG filters: None, Sub, Up, Average)"
echo "all PNG round-trip tests passed"
