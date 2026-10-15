/* sff.c - SFF header probe (v1 and v2).
 *
 * SFF files are little-endian. The PS3's Cell PPU is big-endian, so every
 * multi-byte field must be read byte-by-byte (never cast the buffer to a struct).
 */
#include <stdio.h>
#include <string.h>
#include "ikemen.h"

static uint32_t rd32le(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

int sff_probe(const char *path, SffInfo *out)
{
    uint8_t h[64];
    memset(out, 0, sizeof *out);

    FILE *fp = fopen(path, "rb");
    if (!fp) return -1;
    size_t n = fread(h, 1, sizeof h, fp);
    fclose(fp);
    if (n < sizeof h) return -2;

    if (memcmp(h, "ElecbyteSpr\0", 12) != 0) return -3;

    out->version = h[15];               /* bytes 12..15 = lo3, lo2, lo1, hi */
    if (out->version == 1) {
        out->groups  = rd32le(h + 0x10);
        out->sprites = rd32le(h + 0x14);
    } else if (out->version == 2) {
        out->sprites  = rd32le(h + 0x28);
        out->palettes = rd32le(h + 0x30);
    } else {
        return -4;
    }
    return 0;
}
