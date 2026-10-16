/* png_dec.c - a small PNG (RFC 2083) decoder for SFF v2 PNG sprites (formats
 * 10/11/12). Uses our own inflate (source/inflate.c) for IDAT, no libpng.
 *
 * Supports the colour types and bit depths that show up in real MUGEN/Ikemen
 * SFF content:
 *   - colour type 0 (greyscale), bit depth 8 -> RGB24 (R=G=B=gray, fully opaque)
 *   - colour type 2 (RGB), bit depth 8      -> RGB24
 *   - colour type 3 (indexed), bit depth 1/2/4/8 -> PAL8 with PLTE palette
 *   - colour type 4 (grey+alpha), bit depth 8   -> RGBA32 (A from grey+alpha)
 *   - colour type 6 (RGBA), bit depth 8         -> RGBA32
 *   - bit depth 16 for colour types 0/2/4/6 is accepted by taking the high byte
 *     of each sample (the framebuffer is 8 bits/channel anyway).
 *
 * Adam7 interlacing is rejected with SPR_ERR_UNSUPPORTED (rare in SFF). The
 * zlib wrapper around IDAT is required (PNG always uses zlib, not raw deflate).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ikemen.h"

/* ------------------------------------------------------------ inflate glue */
extern int inflate(const uint8_t *in, size_t in_len, uint8_t *out, size_t out_cap, size_t *out_len);

/* ------------------------------------------------------------ chunk reader */
typedef struct {
    const uint8_t *p;
    size_t len, pos;
} R;

static uint32_t rd32be(const uint8_t *p) { return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3]; }

static int read_u32(R *r, uint32_t *out)
{
    if (r->pos + 4 > r->len) return -1;
    *out = rd32be(r->p + r->pos);
    r->pos += 4;
    return 0;
}

/* ------------------------------------------------------------ PNG unfilter */
/* Apply the inverse PNG filter to one scanline. PNG filters reference the
 * pixel `bpp` bytes to the left (a) and `bpp` bytes up (b). bpp is the number
 * of bytes per pixel in the raw (filtered) image, which for colour types
 * 0/2/3/4/6 at bit depth 8 is 1/3/1/2/4; at bit depth 16 it's 2/6/2/4/8.
 *
 * For sub-8-bit indexed images we still use bpp=1 for the filter math (since
 * each byte holds multiple pixels and the spec says the filter operates on
 * bytes). The caller expands to 8-bit per pixel afterwards.
 */
static void unfilter_scanline(uint8_t *line, const uint8_t *prev, int bpl, int bpp)
{
    uint8_t type = line[0];
    uint8_t *px = line + 1;
    const uint8_t *pp = prev ? prev + 1 : NULL;

    switch (type) {
        case 0:                                         /* None */
            break;
        case 1:                                         /* Sub: x += a */
            for (int i = 0; i < bpl; i++) {
                int a = (i >= bpp) ? px[i - bpp] : 0;
                px[i] = (uint8_t)(px[i] + a);
            }
            break;
        case 2:                                         /* Up: x += b */
            for (int i = 0; i < bpl; i++)
                px[i] = (uint8_t)(px[i] + (pp ? pp[i] : 0));
            break;
        case 3:                                         /* Average: x += (a+b)/2 */
            for (int i = 0; i < bpl; i++) {
                int a = (i >= bpp) ? px[i - bpp] : 0;
                int b = pp ? pp[i] : 0;
                px[i] = (uint8_t)(px[i] + (uint8_t)((a + b) >> 1));
            }
            break;
        case 4: {                                       /* Paeth: x += Paeth(a,b,c) */
            for (int i = 0; i < bpl; i++) {
                int a = (i >= bpp) ? px[i - bpp] : 0;
                int b = pp ? pp[i] : 0;
                int c = (i >= bpp && pp) ? pp[i - bpp] : 0;
                int p = a + b - c;
                int pa = p > a ? p - a : a - p;
                int pb = p > b ? p - b : b - p;
                int pc = p > c ? p - c : c - p;
                int pr = (pa <= pb && pa <= pc) ? a : (pb <= pc) ? b : c;
                px[i] = (uint8_t)(px[i] + (uint8_t)pr);
            }
            break;
        }
        default:                                        /* bad filter type - leave as-is, caller may detect */
            break;
    }
    /* overwrite the filter byte so the caller gets a clean scanline */
    line[0] = 0;
}

/* ------------------------------------------------------------ bit unpacker */
/* (sub-8-bit indexed expansion is inlined into png_decode so we don't need a
 * separate function and a temporary buffer) */

/* ------------------------------------------------------------ public API */
int png_decode(const uint8_t *data, size_t len,
               int w, int h,
               uint8_t **out_px, int *out_mode, uint32_t pal_out[256])
{
    *out_px = NULL; *out_mode = SPR_MODE_PAL8;
    if (!pal_out) return SPR_ERR_FORMAT;

    /* PNG signature: 89 50 4e 47 0d 0a 1a 0a */
    static const uint8_t PNG_SIG[8] = {0x89, 'P', 'N', 'G', 0x0d, 0x0a, 0x1a, 0x0a};
    if (len < 8 || memcmp(data, PNG_SIG, 8) != 0) return SPR_ERR_FORMAT;

    R r = { data + 8, len - 8, 0 };

    /* IHDR must be first */
    uint32_t chunk_len; char chunk_type[5] = {0};
    if (read_u32(&r, &chunk_len)) return SPR_ERR_TRUNC;
    if (r.pos + 8 + chunk_len + 4 > r.len) return SPR_ERR_TRUNC;
    memcpy(chunk_type, r.p + r.pos, 4); r.pos += 4;
    if (memcmp(chunk_type, "IHDR", 4) != 0 || chunk_len != 13) return SPR_ERR_FORMAT;

    uint32_t pw = rd32be(r.p + r.pos);
    uint32_t ph = rd32be(r.p + r.pos + 4);
    uint8_t bd = r.p[r.pos + 8];
    uint8_t ct = r.p[r.pos + 9];
    uint8_t comp = r.p[r.pos + 10];
    uint8_t filt = r.p[r.pos + 11];
    uint8_t interlace = r.p[r.pos + 12];
    r.pos += 13 + 4;                                    /* data + CRC */

    if (comp != 0 || filt != 0) return SPR_ERR_FORMAT;
    if (interlace != 0) return SPR_ERR_UNSUPPORTED;
    if (pw != (uint32_t)w || ph != (uint32_t)h) return SPR_ERR_FORMAT;

    /* validate colour type / bit depth combinations */
    int bpp_raw;                                        /* bytes per pixel in the raw scanline */
    switch (ct) {
        case 0:                                         /* greyscale */
            if (bd != 8 && bd != 16) return SPR_ERR_UNSUPPORTED;
            bpp_raw = (bd == 16) ? 2 : 1;
            break;
        case 2:                                         /* RGB */
            if (bd != 8 && bd != 16) return SPR_ERR_UNSUPPORTED;
            bpp_raw = (bd == 16) ? 6 : 3;
            break;
        case 3:                                         /* indexed */
            if (bd != 1 && bd != 2 && bd != 4 && bd != 8) return SPR_ERR_UNSUPPORTED;
            bpp_raw = 1;                                /* filter operates per byte */
            break;
        case 4:                                         /* grey+alpha */
            if (bd != 8 && bd != 16) return SPR_ERR_UNSUPPORTED;
            bpp_raw = (bd == 16) ? 4 : 2;
            break;
        case 6:                                         /* RGBA */
            if (bd != 8 && bd != 16) return SPR_ERR_UNSUPPORTED;
            bpp_raw = (bd == 16) ? 8 : 4;
            break;
        default: return SPR_ERR_UNSUPPORTED;
    }

    /* Walk chunks, collecting PLTE / tRNS / IDAT data. */
    uint8_t pal[256][3] = {0};
    int pal_n = 0;
    uint8_t trns[256] = {0xFF};                         /* default alpha = opaque */
    int trns_n = 0;

    /* Gather IDAT bytes into a growable buffer. */
    uint8_t *idat = NULL; size_t idat_cap = 0, idat_len = 0;
    int rc = SPR_OK;

    while (rc == SPR_OK) {
        if (read_u32(&r, &chunk_len)) { rc = SPR_ERR_TRUNC; break; }
        if (r.pos + 4 + chunk_len + 4 > r.len) { rc = SPR_ERR_TRUNC; break; }
        memcpy(chunk_type, r.p + r.pos, 4); r.pos += 4;

        if (memcmp(chunk_type, "IEND", 4) == 0) break;
        else if (memcmp(chunk_type, "PLTE", 4) == 0) {
            if (chunk_len % 3 != 0 || chunk_len / 3 > 256) { rc = SPR_ERR_FORMAT; break; }
            pal_n = (int)(chunk_len / 3);
            for (int i = 0; i < pal_n; i++) {
                pal[i][0] = r.p[r.pos + i*3];
                pal[i][1] = r.p[r.pos + i*3 + 1];
                pal[i][2] = r.p[r.pos + i*3 + 2];
            }
            /* default opaque for all palette entries */
            for (int i = 0; i < 256; i++) trns[i] = 0xFF;
            trns_n = 256;
        }
        else if (memcmp(chunk_type, "tRNS", 4) == 0) {
            if (ct == 3) {
                trns_n = (int)chunk_len;
                for (int i = 0; i < trns_n && i < 256; i++) trns[i] = r.p[r.pos + i];
                for (int i = trns_n; i < 256; i++) trns[i] = 0xFF;
            }
            /* tRNS for colour type 0/2 is a single colour, not used here since
             * we always expand to RGB24/RGBA32 (alpha=255 if no tRNS). */
        }
        else if (memcmp(chunk_type, "IDAT", 4) == 0) {
            if (idat_len + chunk_len > idat_cap) {
                size_t nc = idat_cap ? idat_cap * 2 : 8192;
                while (nc < idat_len + chunk_len) nc *= 2;
                uint8_t *np = realloc(idat, nc);
                if (!np) { rc = SPR_ERR_NOMEM; break; }
                idat = np; idat_cap = nc;
            }
            memcpy(idat + idat_len, r.p + r.pos, chunk_len);
            idat_len += chunk_len;
        }
        /* else: skip ancillary chunk */
        r.pos += chunk_len + 4;                         /* data + CRC */
    }

    if (rc != SPR_OK) { free(idat); return rc; }

    /* inflate the IDAT data */
    int bpp_out;                                       /* bytes per pixel in the OUTPUT image */
    int mode;
    if (ct == 3) { mode = SPR_MODE_PAL8; bpp_out = 1; }
    else if (ct == 2) { mode = SPR_MODE_RGB24; bpp_out = 3; }
    else if (ct == 6) { mode = SPR_MODE_RGBA32; bpp_out = 4; }
    else if (ct == 0) { mode = SPR_MODE_RGB24; bpp_out = 3; }
    else /* ct == 4 */ { mode = SPR_MODE_RGBA32; bpp_out = 4; }

    int bpl_raw = (ct == 3) ?
        (w * bd + 7) / 8 :                              /* packed for indexed sub-8 */
        w * bpp_raw;                                    /* one byte per sample, expanded for bd=16 */
    size_t inflated_cap = (size_t)(bpl_raw + 1) * h;
    uint8_t *raw = malloc(inflated_cap ? inflated_cap : 1);
    if (!raw) { free(idat); return SPR_ERR_NOMEM; }

    size_t got = 0;
    int irc = inflate(idat, idat_len, raw, inflated_cap, &got);
    free(idat);
    if (irc != 0) { free(raw); return SPR_ERR_FORMAT; }
    if (got != inflated_cap) { free(raw); return SPR_ERR_TRUNC; }

    /* Unfilter scanline by scanline. */
    uint8_t *prev = NULL;
    for (int y = 0; y < h; y++) {
        uint8_t *line = raw + (size_t)y * (bpl_raw + 1);
        unfilter_scanline(line, prev, bpl_raw, bpp_raw);
        prev = line;
    }

    /* For 16-bit colour types, take the high byte of each sample (we already
     * have the bytes laid out as sample_hi, sample_lo, sample_hi, ...; collapse
     * them to one byte per channel by moving the high bytes to the front of
     * each pixel). */
    if (bd == 16 && ct != 3) {
        int channels = (ct == 2) ? 3 : (ct == 6) ? 4 : (ct == 0) ? 1 : 2;
        for (int y = 0; y < h; y++) {
            uint8_t *line = raw + (size_t)y * (bpl_raw + 1) + 1;
            for (int x = 0; x < w; x++)
                for (int c = 0; c < channels; c++)
                    line[x * channels + c] = line[x * channels * 2 + c * 2];   /* keep high byte */
        }
    }

    /* Convert raw pixels into the output format. */
    uint8_t *out = malloc((size_t)w * h * bpp_out);
    if (!out) { free(raw); return SPR_ERR_NOMEM; }

    if (ct == 3) {
        /* PAL8: build palette from PLTE + tRNS, then write pixel indices to
         * `out`. For 8-bit indexed we just copy bytes from the raw scanline;
         * for sub-8-bit (1/2/4) we expand each packed byte to one byte per
         * pixel. Either way, indices whose tRNS alpha is 0 are remapped to
         * 0 so the blit's "idx == 0 => transparent" rule skips them. */
        for (int i = 0; i < 256; i++)
            pal_out[i] = 0xFF000000u | ((uint32_t)pal[i][0] << 16) | ((uint32_t)pal[i][1] << 8) | pal[i][2];
        for (int i = 0; i < 256; i++)
            pal_out[i] = (pal_out[i] & 0x00FFFFFF) | ((uint32_t)trns[i] << 24);

        if (bd == 8) {
            for (int y = 0; y < h; y++) {
                const uint8_t *line = raw + (size_t)y * (bpl_raw + 1) + 1;
                for (int x = 0; x < w; x++) {
                    uint8_t idx = line[x];
                    if (trns[idx] == 0) idx = 0;
                    out[y * w + x] = idx;
                }
            }
        } else {
            int mask = (1 << bd) - 1;
            int per = 8 / bd;
            for (int y = 0; y < h; y++) {
                const uint8_t *line = raw + (size_t)y * (bpl_raw + 1) + 1;
                for (int x = 0; x < w; x++) {
                    int bi = x / per;
                    int sh = bd * (per - 1 - (x % per));
                    uint8_t idx = (uint8_t)((line[bi] >> sh) & mask);
                    if (trns[idx] == 0) idx = 0;
                    out[y * w + x] = idx;
                }
            }
        }
    } else if (ct == 0) {
        /* Greyscale -> RGB24 (R=G=B=gray, A=255) */
        for (int y = 0; y < h; y++) {
            uint8_t *line = raw + (size_t)y * (bpl_raw + 1) + 1;
            for (int x = 0; x < w; x++) {
                uint8_t g = line[x];
                out[y * w * 3 + x * 3] = g;
                out[y * w * 3 + x * 3 + 1] = g;
                out[y * w * 3 + x * 3 + 2] = g;
            }
        }
    } else if (ct == 2) {
        /* RGB -> RGB24 */
        int src_bpp = (bd == 16) ? 3 : 3;              /* after 16-bit collapse */
        for (int y = 0; y < h; y++) {
            uint8_t *line = raw + (size_t)y * (bpl_raw + 1) + 1;
            for (int x = 0; x < w; x++) {
                out[y * w * 3 + x * 3]     = line[x * src_bpp];
                out[y * w * 3 + x * 3 + 1] = line[x * src_bpp + 1];
                out[y * w * 3 + x * 3 + 2] = line[x * src_bpp + 2];
            }
        }
    } else if (ct == 4) {
        /* Grey+alpha -> RGBA32 */
        for (int y = 0; y < h; y++) {
            uint8_t *line = raw + (size_t)y * (bpl_raw + 1) + 1;
            for (int x = 0; x < w; x++) {
                uint8_t g = line[x * 2];
                uint8_t a = line[x * 2 + 1];
                out[y * w * 4 + x * 4]     = g;
                out[y * w * 4 + x * 4 + 1] = g;
                out[y * w * 4 + x * 4 + 2] = g;
                out[y * w * 4 + x * 4 + 3] = a;
            }
        }
    } else { /* ct == 6, RGBA -> RGBA32 */
        for (int y = 0; y < h; y++) {
            uint8_t *line = raw + (size_t)y * (bpl_raw + 1) + 1;
            for (int x = 0; x < w; x++) {
                out[y * w * 4 + x * 4]     = line[x * 4];
                out[y * w * 4 + x * 4 + 1] = line[x * 4 + 1];
                out[y * w * 4 + x * 4 + 2] = line[x * 4 + 2];
                out[y * w * 4 + x * 4 + 3] = line[x * 4 + 3];
            }
        }
    }

    free(raw);
    *out_px = out;
    *out_mode = mode;
    return SPR_OK;
}
