/* sprite.c - SFF v1/v2 sprite loading and decoding. Portable C (host + PS3).
 *
 * Format details were checked against Ikemen GO's src/image.go (readHeader, readPcxHeader,
 * RlePcxDecode, readHeaderV2, Rle8Decode, Rle5Decode, Lz5Decode, readV2, loadPalettes).
 * All multi-byte values are little-endian and are read byte-wise (PS3 is big-endian).
 *
 * Supported: v1 (PCX, shared palettes, linked sprites), v2 raw 8-bit, RLE8, RLE5, LZ5, v2 palettes.
 * Not yet: v2 PNG (fmt 10/11/12) and raw 24/32-bit sprites -> sprite.err = SPR_ERR_UNSUPPORTED.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ikemen.h"

/* Hard limits. The PS3 has very little RAM for a homebrew app, so hostile or corrupt files must be
 * rejected cheaply instead of being allowed to eat memory. Real content is far below every one. */
#define MAX_SFF_BYTES (64u * 1024u * 1024u)   /* whole file is read into RAM: larger files are refused */
#define MAX_DIM       4096
#define MAX_PIXELS    (4u * 1024u * 1024u)    /* per sprite (2048x2048) */
#define MAX_SPRITES   65536
#define MAX_PALETTES  2048
#define SFF_BUDGET    (40u * 1024u * 1024u)   /* decoded pixels per loaded Sff */

static uint32_t u32(const uint8_t *p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24); }
static uint16_t u16(const uint8_t *p) { return (uint16_t)(p[0] | (p[1] << 8)); }
static int16_t  s16(const uint8_t *p) { return (int16_t)u16(p); }

static uint8_t *slurp(const char *path, size_t *len)
{
    FILE *fp = fopen(path, "rb");
    if (!fp) return NULL;
    fseek(fp, 0, SEEK_END);
    long n = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    if (n < 64 || (unsigned long)n > MAX_SFF_BYTES) { fclose(fp); return NULL; }
    uint8_t *b = malloc((size_t)n);
    if (b && fread(b, 1, (size_t)n, fp) != (size_t)n) { free(b); b = NULL; }
    fclose(fp);
    *len = (size_t)n;
    return b;
}

/* ------------------------------------------------------------ decoders */

/* PCX RLE (v1). Runs never cross a scanline of bpl bytes; only the first w bytes of each row are kept. */
static int dec_pcx(const uint8_t *r, size_t len, int enc, int bpl, int w, int h, uint8_t *p)
{
    size_t i = 0;
    for (int y = 0; y < h; y++) {
        int x = 0;
        while (x < bpl) {
            if (i >= len) return -1;                     /* truncated: rest stays 0 */
            int cnt = 1;
            uint8_t d = r[i++];
            if (enc == 1 && d >= 0xC0) {
                cnt = d & 0x3F;
                if (i >= len) return -1;
                d = r[i++];
            }
            for (; cnt > 0 && x < bpl; cnt--, x++)
                if (x < w) p[(size_t)y * w + x] = d;
        }
    }
    return 0;
}

/* v2 RLE8: 01xxxxxx cc = run of xxxxxx copies of cc; anything else = literal byte. */
static void dec_rle8(const uint8_t *r, size_t len, uint8_t *p, size_t total)
{
    size_t i = 0, j = 0;
    while (j < total && i < len) {
        int cnt = 1;
        uint8_t d = r[i++];
        if ((d & 0xC0) == 0x40) {
            cnt = d & 0x3F;
            if (i >= len) break;
            d = r[i++];
        }
        for (; cnt > 0; cnt--) if (j < total) p[j++] = d;
    }
}

/* v2 RLE5 */
static void dec_rle5(const uint8_t *r, size_t len, uint8_t *p, size_t total)
{
    size_t i = 0, j = 0;
    while (j < total && i < len) {
        int rl = r[i++];
        if (i >= len) break;
        int dl = r[i] & 0x7F;
        uint8_t c = 0;
        if (r[i] >> 7) {
            i++;
            if (i >= len) break;
            c = r[i];
        }
        i++;
        for (;;) {
            if (j < total) p[j++] = c;
            if (--rl < 0) {
                if (--dl < 0) break;
                if (i >= len) return;
                c  = r[i] & 0x1F;
                rl = r[i] >> 5;
                i++;
            }
        }
    }
}

/* v2 LZ5 */
static void dec_lz5(const uint8_t *r, size_t len, uint8_t *p, size_t total)
{
    size_t i = 0, j = 0;
    int eof = 0;
#define RD() (i < len ? r[i++] : (eof = 1, 0))
    if (!len) return;
    uint8_t ct = (uint8_t)RD(), rb = 0;
    unsigned cts = 0, rbc = 0;
    while (j < total && !eof) {
        int d = RD(), cnt;
        if (ct & (1u << cts)) {
            if ((d & 0x3F) == 0) {
                d = ((d << 2) | RD()) + 1;
                cnt = RD() + 2;
            } else {
                rb |= (uint8_t)((d & 0xC0) >> rbc);
                rbc += 2;
                cnt = d & 0x3F;
                if (rbc < 8) d = RD() + 1;
                else { d = rb + 1; rb = 0; rbc = 0; }
            }
            for (;;) {
                if (j < total) { p[j] = ((size_t)d <= j) ? p[j - d] : 0; j++; }
                if (--cnt < 0) break;
            }
        } else {
            if ((d & 0xE0) == 0) cnt = RD() + 8;
            else { cnt = d >> 5; d &= 0x1F; }
            for (; cnt > 0; cnt--) if (j < total) p[j++] = (uint8_t)d;
        }
        if (++cts >= 8) { ct = (uint8_t)RD(); cts = 0; }
    }
#undef RD
}

/* ------------------------------------------------------------ palettes */

static int pal_new(Sff *s)
{
    if (s->npal >= MAX_PALETTES) return -1;
    uint32_t (*np)[256] = realloc(s->pal, (size_t)(s->npal + 1) * sizeof *np);
    if (!np) return -1;
    s->pal = np;
    memset(np[s->npal], 0, sizeof *np);
    return s->npal++;
}

/* ------------------------------------------------------------ v1 */

static void share(Sprite *dst, const Sprite *src)
{
    dst->w = src->w; dst->h = src->h;
    dst->px = src->px; dst->owns = 0;
    if (dst->pal < 0) dst->pal = src->pal;
    dst->err = src->err;
}

static int dec_v1(Sff *s, Sprite *sp, const uint8_t *b, size_t len, size_t pcx,
                  uint32_t size, uint32_t next, int same, int prevpal, int want)
{
    if (pcx + 128 > len) return SPR_ERR_TRUNC;
    const uint8_t *h = b + pcx;
    int enc = h[2], bpp = h[3];
    if (bpp != 8) return SPR_ERR_FORMAT;
    int w  = u16(h + 8)  - u16(h + 4) + 1;
    int ht = u16(h + 10) - u16(h + 6) + 1;
    int bpl = u16(h + 66);
    if (w <= 0 || ht <= 0 || w > MAX_DIM || ht > MAX_DIM || bpl <= 0) return SPR_ERR_FORMAT;
    if ((size_t)w * ht > MAX_PIXELS) return SPR_ERR_FORMAT;

    size_t block_end = (next > pcx) ? next : pcx + size;
    if (block_end > len) block_end = len;
    size_t data_start = pcx + 128, data_end;

    if (same) {
        data_end = block_end;
        sp->pal = prevpal;
    } else {
        if (block_end < data_start + 769) return SPR_ERR_TRUNC;
        long pos;
        for (pos = (long)block_end - 769; pos >= (long)data_start; pos--)
            if (b[pos] == 0x0C) break;
        if (pos < (long)data_start) pos = (long)block_end - 769;   /* same fallback as Ikemen */
        data_end = (size_t)pos;
        int pi = pal_new(s);
        if (pi < 0) return SPR_ERR_NOMEM;
        const uint8_t *pp = b + pos + 1;
        for (int i = 0; i < 256; i++)
            s->pal[pi][i] = 0xFF000000u | ((uint32_t)pp[i * 3] << 16) | ((uint32_t)pp[i * 3 + 1] << 8) | pp[i * 3 + 2];
        sp->pal = pi;
    }

    if (!want) {                       /* palette is registered above so later "same palette" sprites still work */
        sp->w = (uint16_t)w; sp->h = (uint16_t)ht;
        return SPR_NOT_LOADED;
    }
    if (s->bytes + (size_t)w * ht > SFF_BUDGET) return SPR_ERR_NOMEM;
    uint8_t *px = calloc((size_t)w * ht, 1);
    if (!px) return SPR_ERR_NOMEM;
    s->bytes += (size_t)w * ht;
    dec_pcx(b + data_start, data_end > data_start ? data_end - data_start : 0, enc, bpl, w, ht, px);
    sp->w = (uint16_t)w; sp->h = (uint16_t)ht; sp->px = px; sp->owns = 1;
    return SPR_OK;
}

/* Filtered loading: mark which sprites must be decoded to produce (wg,wi), including the sprites
 * it is linked to. Links always point backwards, so one descending pass is enough.
 * Returns NULL for "decode everything" (wg < 0, or out of memory). */
static uint8_t *need_v1(const uint8_t *b, size_t len, uint32_t n, uint32_t shofs, int wg, int wi)
{
    if (wg < 0) return NULL;
    uint8_t *need = calloc(n, 1), *isl = calloc(n, 1);
    uint16_t *link = calloc(n, sizeof *link);
    if (!need || !isl || !link) { free(need); free(isl); free(link); return NULL; }
    for (uint32_t i = 0; i < n; i++) {
        if ((size_t)shofs + 32 > len) break;
        const uint8_t *h = b + shofs;
        isl[i]  = u32(h + 4) == 0;
        link[i] = u16(h + 16);
        if (u16(h + 12) == wg && u16(h + 14) == wi) need[i] = 1;
        uint32_t next = u32(h);
        if (!next) break;
        shofs = next;
    }
    for (uint32_t i = n; i-- > 0;)
        if (need[i] && isl[i] && link[i] < i) need[link[i]] = 1;
    free(isl); free(link);
    return need;
}

static uint8_t *need_v2(const uint8_t *b, size_t len, uint32_t first, uint32_t n, int wg, int wi)
{
    if (wg < 0) return NULL;
    uint8_t *need = calloc(n, 1), *isl = calloc(n, 1);
    uint16_t *link = calloc(n, sizeof *link);
    if (!need || !isl || !link) { free(need); free(isl); free(link); return NULL; }
    for (uint32_t i = 0; i < n; i++) {
        size_t sh = (size_t)first + (size_t)i * 28;
        if (sh + 28 > len) break;
        const uint8_t *h = b + sh;
        isl[i]  = u32(h + 20) == 0;
        link[i] = u16(h + 12);
        if (u16(h) == wg && u16(h + 2) == wi) need[i] = 1;
    }
    for (uint32_t i = n; i-- > 0;)
        if (need[i] && isl[i] && link[i] < i) need[link[i]] = 1;
    free(isl); free(link);
    return need;
}

static int load_v1(Sff *s, const uint8_t *b, size_t len, int wg, int wi)
{
    uint32_t n = u32(b + 0x14), shofs = u32(b + 0x18);
    if (n == 0 || n > len / 32 || n > MAX_SPRITES) return -1;
    s->spr = calloc(n, sizeof(Sprite));
    if (!s->spr) return -1;
    s->nspr = (int)n;
    uint8_t *need = need_v1(b, len, n, shofs, wg, wi);

    int prev = -1;
    for (uint32_t i = 0; i < n; i++) {
        if ((size_t)shofs + 32 > len) { s->nspr = (int)i; break; }
        const uint8_t *h = b + shofs;
        uint32_t next = u32(h), size = u32(h + 4);
        Sprite *sp = &s->spr[i];
        sp->ax = s16(h + 8);  sp->ay = s16(h + 10);
        sp->group = u16(h + 12); sp->image = u16(h + 14);
        uint16_t link = u16(h + 16);
        int same = h[18] != 0 && prev >= 0;
        sp->pal = -1;

        if (size == 0) {
            if (link < i) share(sp, &s->spr[link]);
            else sp->err = SPR_ERR_BADLINK;
        } else {
            sp->err = dec_v1(s, sp, b, len, (size_t)shofs + 32, size, next, same,
                             prev >= 0 ? s->spr[prev].pal : -1, !need || need[i]);
            if (sp->err == SPR_OK || sp->err == SPR_NOT_LOADED) prev = (int)i;
        }
        shofs = next;
        if (next == 0 && i + 1 < n) { s->nspr = (int)i + 1; break; }
    }
    free(need);
    return 0;
}

/* ------------------------------------------------------------ v2 */

static int load_v2(Sff *s, const uint8_t *b, size_t len, int wg, int wi)
{
    uint32_t first = u32(b + 0x24), n = u32(b + 0x28);
    uint32_t palhdr = u32(b + 0x2C), npal = u32(b + 0x30);
    uint32_t lofs = u32(b + 0x34), tofs = u32(b + 0x3C);
    if (n == 0 || n > len / 28 || n > MAX_SPRITES || npal > len / 16) return -1;
    if (npal > MAX_PALETTES) npal = MAX_PALETTES;       /* extra palettes are ignored */

    /* palettes: 16-byte headers, RGBA entries in the literal data block */
    for (uint32_t i = 0; i < npal; i++) {
        if ((size_t)palhdr + (size_t)(i + 1) * 16 > len) break;
        const uint8_t *h = b + palhdr + (size_t)i * 16;
        uint16_t link = u16(h + 6);
        uint32_t ofs = u32(h + 8), size = u32(h + 12);
        int pi = pal_new(s);
        if (pi < 0) return -1;
        if (size == 0) {
            if (link < (uint32_t)pi) memcpy(s->pal[pi], s->pal[link], sizeof s->pal[pi]);
            continue;
        }
        size_t at = (size_t)lofs + ofs;
        uint32_t cnt = size / 4;
        if (cnt > 256) cnt = 256;
        if (at + (size_t)cnt * 4 > len) continue;
        for (uint32_t c = 0; c < cnt; c++) {
            const uint8_t *q = b + at + c * 4;
            s->pal[pi][c] = 0xFF000000u | ((uint32_t)q[0] << 16) | ((uint32_t)q[1] << 8) | q[2];
        }
    }

    s->spr = calloc(n, sizeof(Sprite));
    if (!s->spr) return -1;
    s->nspr = (int)n;
    uint8_t *need = need_v2(b, len, first, n, wg, wi);

    for (uint32_t i = 0; i < n; i++) {
        size_t sh = (size_t)first + (size_t)i * 28;
        if (sh + 28 > len) { s->nspr = (int)i; break; }
        const uint8_t *h = b + sh;
        Sprite *sp = &s->spr[i];
        sp->group = u16(h);  sp->image = u16(h + 2);
        int w = u16(h + 4), ht = u16(h + 6);
        sp->ax = s16(h + 8); sp->ay = s16(h + 10);
        uint16_t link = u16(h + 12);
        int fmt = h[14], depth = h[15];
        uint32_t ofs = u32(h + 16), size = u32(h + 20);
        uint16_t pidx = u16(h + 24), flags = u16(h + 26);
        sp->pal = (pidx < s->npal) ? pidx : -1;
        ofs += (flags & 1) ? tofs : lofs;

        if (size == 0) {
            if (link < i) share(sp, &s->spr[link]);
            else sp->err = SPR_ERR_BADLINK;
            continue;
        }
        if (need && !need[i]) { sp->err = SPR_NOT_LOADED; continue; }
        if (w <= 0 || ht <= 0 || w > MAX_DIM || ht > MAX_DIM || (size_t)w * ht > MAX_PIXELS) { sp->err = SPR_ERR_FORMAT; continue; }
        if ((size_t)ofs + size > len) { sp->err = SPR_ERR_TRUNC; continue; }

        size_t total = (size_t)w * ht;
        if (s->bytes + total > SFF_BUDGET) { sp->err = SPR_ERR_NOMEM; continue; }
        const uint8_t *src = b + ofs;
        uint8_t *px = NULL;

        if (fmt == 0) {
            if (depth != 8) { sp->err = SPR_ERR_UNSUPPORTED; continue; }
            px = calloc(total, 1);
            if (px) memcpy(px, src, size < total ? size : total);
        } else if (fmt >= 2 && fmt <= 4) {
            if (size < 4) { sp->err = SPR_ERR_TRUNC; continue; }
            px = calloc(total, 1);
            if (px) {
                if (fmt == 2) dec_rle8(src + 4, size - 4, px, total);
                else if (fmt == 3) dec_rle5(src + 4, size - 4, px, total);
                else dec_lz5(src + 4, size - 4, px, total);
            }
        } else {
            sp->err = SPR_ERR_UNSUPPORTED;     /* 10/11/12 = PNG: needs zlib/libpng */
            continue;
        }
        if (!px) { sp->err = SPR_ERR_NOMEM; continue; }
        s->bytes += total;
        sp->w = (uint16_t)w; sp->h = (uint16_t)ht; sp->px = px; sp->owns = 1;
    }
    free(need);
    return 0;
}

/* ------------------------------------------------------------ public API */

int sff_load_ex(const char *path, Sff *out, int wg, int wi)
{
    memset(out, 0, sizeof *out);
    size_t len;
    uint8_t *b = slurp(path, &len);
    if (!b) return -1;
    if (memcmp(b, "ElecbyteSpr\0", 12) != 0) { free(b); return -3; }
    out->version = b[15];
    int rc = (out->version == 1) ? load_v1(out, b, len, wg, wi)
           : (out->version == 2) ? load_v2(out, b, len, wg, wi) : -4;
    free(b);
    if (rc != 0) { sff_free(out); return rc; }
    return 0;
}

void sff_free(Sff *s)
{
    for (int i = 0; i < s->nspr; i++)
        if (s->spr[i].owns) free(s->spr[i].px);
    free(s->spr);
    free(s->pal);
    memset(s, 0, sizeof *s);
}

const Sprite *sff_find(const Sff *s, int group, int image)
{
    for (int i = 0; i < s->nspr; i++)
        if (s->spr[i].group == group && s->spr[i].image == image) return &s->spr[i];
    return NULL;
}

const uint32_t *sff_palette(const Sff *s, const Sprite *sp)
{
    return (sp->pal >= 0 && sp->pal < s->npal) ? s->pal[sp->pal] : NULL;
}

int sff_load(const char *path, Sff *out) { return sff_load_ex(path, out, -1, -1); }

int sff_count_errors(const Sff *s)
{
    int e = 0;
    for (int i = 0; i < s->nspr; i++)
        if (s->spr[i].err != SPR_OK && s->spr[i].err != SPR_NOT_LOADED) e++;
    return e;
}

int sff_extract(const Sff *s, int group, int image, Sprite *out, uint32_t pal_out[256])
{
    memset(out, 0, sizeof *out);
    const Sprite *sp = sff_find(s, group, image);
    if (!sp || sp->err || !sp->px) return -1;
    const uint32_t *pal = sff_palette(s, sp);
    if (!pal) return -2;
    size_t n = (size_t)sp->w * sp->h;
    uint8_t *px = malloc(n);
    if (!px) return -3;
    memcpy(px, sp->px, n);
    memcpy(pal_out, pal, 256 * sizeof(uint32_t));
    *out = *sp;
    out->px = px; out->owns = 1; out->pal = 0;
    return 0;
}

void sprite_free(Sprite *sp)
{
    if (sp->owns) free(sp->px);
    memset(sp, 0, sizeof *sp);
}

void sprite_blit(const Sprite *sp, const uint32_t *pal, uint32_t *dst, int dw, int dh,
                 int x, int y, int scale, int flip)
{
    if (!sp || !sp->px || !pal || scale < 1) return;
    int left = flip ? x - (sp->w - sp->ax) * scale : x - sp->ax * scale;
    int top  = y - sp->ay * scale;
    for (int sy = 0; sy < sp->h; sy++) {
        for (int sx = 0; sx < sp->w; sx++) {
            uint8_t idx = sp->px[(size_t)sy * sp->w + sx];
            if (!idx) continue;                               /* index 0 = transparent */
            int px0 = left + (flip ? sp->w - 1 - sx : sx) * scale;
            int py0 = top + sy * scale;
            for (int j = 0; j < scale; j++) {
                int yy = py0 + j;
                if (yy < 0 || yy >= dh) continue;
                for (int i = 0; i < scale; i++) {
                    int xx = px0 + i;
                    if (xx >= 0 && xx < dw) dst[(size_t)yy * dw + xx] = pal[idx];
                }
            }
        }
    }
}
