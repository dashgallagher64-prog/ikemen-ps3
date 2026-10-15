/* gfx_draw.c - portable software drawing (clear/rect/text) into whatever buffer gfx_target() set.
 * Used by the PS3 build (gfx.c points it at the RSX framebuffer) and by the host test harness.
 */
#include "ikemen.h"

static uint32_t *tgt;
static int tw, th;

void gfx_target(uint32_t *buf, int w, int h) { tgt = buf; tw = w; th = h; }
int  gfx_width(void)  { return tw; }
int  gfx_height(void) { return th; }

void gfx_clear(uint32_t c)
{
    for (int i = 0; i < tw * th; i++) tgt[i] = c;
}

void gfx_rect(int x, int y, int w, int h, uint32_t c)
{
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > tw) w = tw - x;
    if (y + h > th) h = th - y;
    for (int j = 0; j < h; j++) {
        uint32_t *row = tgt + (y + j) * tw + x;
        for (int i = 0; i < w; i++) row[i] = c;
    }
}

/* 3x5 font: 5 rows of 3 bits, row 0 in bits 14..12. Lowercase maps to uppercase. */
static uint16_t glyph(int ch)
{
    static const uint16_t digits[10] = {
        0x7B6F, 0x2C97, 0x73E7, 0x73CF, 0x5BC9, 0x79CF, 0x79EF, 0x7249, 0x7BEF, 0x7BCF };
    static const uint16_t alpha[26] = {
        0x2BED, 0x6BAE, 0x3923, 0x6B6E, 0x79A7, 0x79A4, 0x396B, 0x5BED, 0x7497, 0x126A,
        0x5BAD, 0x4927, 0x5FED, 0x6B6D, 0x2B6A, 0x6BA4, 0x2B7B, 0x6BAD, 0x388E, 0x7492,
        0x5B6F, 0x5B6A, 0x5BFD, 0x5AAD, 0x5A92, 0x72A7 };
    if (ch >= 'a' && ch <= 'z') ch -= 32;
    if (ch >= '0' && ch <= '9') return digits[ch - '0'];
    if (ch >= 'A' && ch <= 'Z') return alpha[ch - 'A'];
    switch (ch) {
        case '.': return 0x0002;
        case '-': return 0x01C0;
        case '_': return 0x0007;
        case '/': return 0x12A4;
        case ':': return 0x0410;
        case '>': return 0x4454;
        default:  return 0;
    }
}

void gfx_text(int x, int y, int scale, uint32_t color, const char *s)
{
    for (; *s; s++, x += 4 * scale) {
        uint16_t g = glyph((unsigned char)*s);
        for (int r = 0; r < 5; r++)
            for (int c = 0; c < 3; c++)
                if (g & (1 << (14 - (r * 3 + c))))
                    gfx_rect(x + c * scale, y + r * scale, scale, scale, color);
    }
}
