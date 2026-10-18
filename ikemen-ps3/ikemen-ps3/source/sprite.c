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


/* ------------------------------------------------------------ PNG (8-bit palette) for SFF v2 format 10 */
/* A small inflate in the style of zlib's "puff": canonical Huffman decoding, bounded output. */
typedef struct { const uint8_t *in; size_t n, pos; uint32_t bb; int bc; uint8_t *out; size_t cap, op; int err; } Inf;
typedef struct { uint16_t cnt[16], sym[288]; } Huff;

static int inf_bit(Inf *s)
{
    if (s->bc == 0) {
        if (s->pos >= s->n) { s->err = 1; return 0; }
        s->bb = s->in[s->pos++]; s->bc = 8;
    }
    int b = (int)(s->bb & 1); s->bb >>= 1; s->bc--;
    return b;
}
static int inf_bits(Inf *s, int n) { int v = 0; for (int i = 0; i < n; i++) v |= inf_bit(s) << i; return v; }

static int huff_build(Huff *h, const uint8_t *len, int n)
{
    uint16_t offs[16];
    memset(h->cnt, 0, sizeof h->cnt);
    for (int i = 0; i < n; i++) h->cnt[len[i]]++;
    if (h->cnt[0] == n) return 0;
    int left = 1;
    for (int l = 1; l < 16; l++) { left <<= 1; left -= h->cnt[l]; if (left < 0) return -1; }
    offs[1] = 0;
    for (int l = 1; l < 15; l++) offs[l + 1] = (uint16_t)(offs[l] + h->cnt[l]);
    for (int i = 0; i < n; i++) if (len[i]) h->sym[offs[len[i]]++] = (uint16_t)i;
    return 0;
}

static int huff_dec(Inf *s, const Huff *h)
{
    int code = 0, first = 0, index = 0;
    for (int l = 1; l < 16; l++) {
        code |= inf_bit(s);
        if (s->err) return -1;
        int c = h->cnt[l];
        if (code - c < first) return h->sym[index + (code - first)];
        index += c; first += c; first <<= 1; code <<= 1;
    }
    return -1;
}

static int inf_codes(Inf *s, const Huff *lc, const Huff *dc)
{
    static const uint16_t lb[29] = {3,4,5,6,7,8,9,10,11,13,15,17,19,23,27,31,35,43,51,59,67,83,99,115,131,163,195,227,258};
    static const uint8_t  le[29] = {0,0,0,0,0,0,0,0,1,1,1,1,2,2,2,2,3,3,3,3,4,4,4,4,5,5,5,5,0};
    static const uint16_t db[30] = {1,2,3,4,5,7,9,13,17,25,33,49,65,97,129,193,257,385,513,769,1025,1537,2049,3073,4097,6145,8193,12289,16385,24577};
    static const uint8_t  de[30] = {0,0,0,0,1,1,2,2,3,3,4,4,5,5,6,6,7,7,8,8,9,9,10,10,11,11,12,12,13,13};
    for (;;) {
        int sym = huff_dec(s, lc);
        if (sym < 0 || s->err) return -1;
        if (sym < 256) { if (s->op >= s->cap) return -1; s->out[s->op++] = (uint8_t)sym; }
        else if (sym == 256) return 0;
        else {
            sym -= 257;
            if (sym >= 29) return -1;
            int len = lb[sym] + inf_bits(s, le[sym]);
            int ds = huff_dec(s, dc);
            if (ds < 0 || ds >= 30) return -1;
            size_t dist = (size_t)db[ds] + (size_t)inf_bits(s, de[ds]);
            if (s->err || dist > s->op || s->op + (size_t)len > s->cap) return -1;
            for (int i = 0; i < len; i++) { s->out[s->op] = s->out[s->op - dist]; s->op++; }
        }
    }
}

/* returns number of bytes written, or -1 */
static long inflate_raw(const uint8_t *in, size_t n, uint8_t *out, size_t cap)
{
    Inf s; memset(&s, 0, sizeof s);
    s.in = in; s.n = n; s.out = out; s.cap = cap;
    int last;
    do {
        last = inf_bit(&s);
        int type = inf_bits(&s, 2);
        if (s.err) return -1;
        if (type == 0) {
            s.bc = 0;
            if (s.pos + 4 > s.n) return -1;
            unsigned len = in[s.pos] | (in[s.pos + 1] << 8);
            unsigned nlen = in[s.pos + 2] | (in[s.pos + 3] << 8);
            s.pos += 4;
            if (len != (~nlen & 0xFFFF) || s.pos + len > s.n || s.op + len > s.cap) return -1;
            memcpy(s.out + s.op, in + s.pos, len); s.op += len; s.pos += len;
        } else if (type == 1) {
            uint8_t l[288]; Huff lc, dc;
            for (int i = 0; i < 144; i++) l[i] = 8;
            for (int i = 144; i < 256; i++) l[i] = 9;
            for (int i = 256; i < 280; i++) l[i] = 7;
            for (int i = 280; i < 288; i++) l[i] = 8;
            huff_build(&lc, l, 288);
            for (int i = 0; i < 30; i++) l[i] = 5;
            huff_build(&dc, l, 30);
            if (inf_codes(&s, &lc, &dc)) return -1;
        } else if (type == 2) {
            static const uint8_t ord[19] = {16,17,18,0,8,7,9,6,10,5,11,4,12,3,13,2,14,1,15};
            uint8_t l[320]; Huff lc, dc, cc;
            int nlen = inf_bits(&s, 5) + 257, ndist = inf_bits(&s, 5) + 1, ncode = inf_bits(&s, 4) + 4;
            if (s.err || nlen > 286 || ndist > 30) return -1;
            memset(l, 0, sizeof l);
            for (int i = 0; i < ncode; i++) l[ord[i]] = (uint8_t)inf_bits(&s, 3);
            if (huff_build(&cc, l, 19) < 0) return -1;
            int idx = 0;
            uint8_t ll[320]; memset(ll, 0, sizeof ll);
            while (idx < nlen + ndist) {
                int sym = huff_dec(&s, &cc);
                if (sym < 0) return -1;
                if (sym < 16) ll[idx++] = (uint8_t)sym;
                else {
                    int prev = 0, rep;
                    if (sym == 16) { if (idx == 0) return -1; prev = ll[idx - 1]; rep = 3 + inf_bits(&s, 2); }
                    else if (sym == 17) rep = 3 + inf_bits(&s, 3);
                    else rep = 11 + inf_bits(&s, 7);
                    if (idx + rep > nlen + ndist) return -1;
                    while (rep--) ll[idx++] = (uint8_t)prev;
                }
                if (s.err) return -1;
            }
            if (ll[256] == 0) return -1;
            if (huff_build(&lc, ll, nlen) < 0 || huff_build(&dc, ll + nlen, ndist) < 0) return -1;
            if (inf_codes(&s, &lc, &dc)) return -1;
        } else return -1;
    } while (!last);
    return (long)s.op;
}

/* 8-bit palette PNG -> w*h indices. Returns 0 or an SPR_ERR_*. */
static int dec_png8(const uint8_t *src, size_t n, uint8_t *px, int w, int h)
{
    if (n < 33 || memcmp(src, "\x89PNG\r\n\x1a\n", 8) != 0) return SPR_ERR_FORMAT;
    size_t pos = 8, idat_len = 0;
    int pw = 0, ph = 0, depth = 0, ctype = -1, interlace = 0;
    uint8_t *idat = malloc(n);
    if (!idat) return SPR_ERR_NOMEM;
    while (pos + 12 <= n) {
        uint32_t cl = ((uint32_t)src[pos] << 24) | ((uint32_t)src[pos + 1] << 16) | ((uint32_t)src[pos + 2] << 8) | src[pos + 3];
        const uint8_t *tp = src + pos + 4, *d = src + pos + 8;
        if (cl > n || pos + 12 + cl > n) break;
        if (!memcmp(tp, "IHDR", 4) && cl >= 13) {
            pw = (int)(((uint32_t)d[0] << 24) | ((uint32_t)d[1] << 16) | ((uint32_t)d[2] << 8) | d[3]);
            ph = (int)(((uint32_t)d[4] << 24) | ((uint32_t)d[5] << 16) | ((uint32_t)d[6] << 8) | d[7]);
            depth = d[8]; ctype = d[9]; interlace = d[12];
        } else if (!memcmp(tp, "IDAT", 4)) { memcpy(idat + idat_len, d, cl); idat_len += cl; }
        else if (!memcmp(tp, "IEND", 4)) break;
        pos += 12 + cl;
    }
    int rc = 0;
    uint8_t *raw = NULL;
    if (pw != w || ph != h || w <= 0 || h <= 0) rc = SPR_ERR_FORMAT;
    else if (depth != 8 || ctype != 3 || interlace) rc = SPR_ERR_UNSUPPORTED;
    else if (idat_len < 6) rc = SPR_ERR_TRUNC;
    if (!rc) {
        size_t rawn = ((size_t)w + 1) * (size_t)h;
        raw = malloc(rawn);
        if (!raw) rc = SPR_ERR_NOMEM;
        else if (inflate_raw(idat + 2, idat_len - 2, raw, rawn) != (long)rawn) rc = SPR_ERR_TRUNC;
        else {
            for (int y = 0; y < h; y++) {
                const uint8_t *r = raw + (size_t)y * ((size_t)w + 1);
                uint8_t *o = px + (size_t)y * w;
                const uint8_t *up = y ? px + (size_t)(y - 1) * w : NULL;
                int ft = r[0]; r++;
                for (int x = 0; x < w; x++) {
                    int a = x ? o[x - 1] : 0, b = up ? up[x] : 0, c = (x && up) ? up[x - 1] : 0, v = r[x];
                    switch (ft) {
                    case 0: break;
                    case 1: v += a; break;
                    case 2: v += b; break;
                    case 3: v += (a + b) / 2; break;
                    case 4: { int p = a + b - c, pa = abs(p - a), pb = abs(p - b), pc = abs(p - c);
                              v += (pa <= pb && pa <= pc) ? a : (pb <= pc ? b : c); break; }
                    default: rc = SPR_ERR_FORMAT; break;
                    }
                    o[x] = (uint8_t)v;
                }
                if (rc) break;
            }
        }
    }
    free(raw); free(idat);
    return rc;
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
        } else if (fmt == 10) {
            if (size < 4) { sp->err = SPR_ERR_TRUNC; continue; }
            px = calloc(total, 1);
            if (px) {
                int e = dec_png8(src + 4, size - 4, px, w, ht);
                if (e) { free(px); sp->err = e; continue; }
            }
        } else {
            sp->err = SPR_ERR_UNSUPPORTED;     /* 11/12 = 24/32-bit PNG: needs a true-colour sprite path */
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

void sprite_blit_scaled(const Sprite *sp, const uint32_t *pal, uint32_t *dst, int dw, int dh,
                        int x, int y, float scale, int flip)
{
    if (!sp || !sp->px || !pal || !(scale > 0.0f) || scale > 64.0f) return;
    float fl = flip ? (float)x - (float)(sp->w - sp->ax) * scale : (float)x - (float)sp->ax * scale;
    float ft = (float)y - (float)sp->ay * scale;
    int ow = (int)((float)sp->w * scale + 1.0f), oh = (int)((float)sp->h * scale + 1.0f);
    int left = (int)fl, top = (int)ft;
    for (int dy = 0; dy < oh; dy++) {
        int yy = top + dy;
        if (yy < 0 || yy >= dh) continue;
        int sy = (int)((float)dy / scale);
        if (sy >= sp->h) continue;
        for (int dx = 0; dx < ow; dx++) {
            int xx = left + dx;
            if (xx < 0 || xx >= dw) continue;
            int sx = (int)((float)dx / scale);
            if (sx >= sp->w) continue;
            if (flip) sx = sp->w - 1 - sx;
            uint8_t idx = sp->px[(size_t)sy * sp->w + sx];
            if (idx) dst[(size_t)yy * dw + xx] = pal[idx];
        }
    }
}
