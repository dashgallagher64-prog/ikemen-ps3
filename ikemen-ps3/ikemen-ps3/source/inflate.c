/* inflate.c - a small, self-contained RFC 1951 (DEFLATE) decompressor.
 *
 * No external dependencies (no zlib). Used by png_dec.c to inflate IDAT chunks.
 * Written from scratch against the spec; checked against real PNG streams in
 * Ikemen_GO-Elecbyte-Screenpack/chars/kfm/kfm.sff.
 *
 * API:  int inflate(const uint8_t *in, size_t in_len, uint8_t *out, size_t out_cap, size_t *out_len);
 *   in, in_len   : the raw DEFLATE stream (after the 2-byte zlib header if any -
 *                  png_dec.c strips the zlib header itself before calling)
 *   out          : caller-allocated output buffer
 *   out_cap      : size of `out` in bytes
 *   out_len      : filled with the number of decompressed bytes on success
 *   returns 0 = ok, -1 = truncated input, -2 = corrupt stream, -3 = output overflow
 *
 * Supports all three block types (stored / fixed Huffman / dynamic Huffman),
 * all 29 length codes, all 30 distance codes, and the 19-symbol code-length
 * alphabet. No dynamic allocation.
 */
#include <stdint.h>
#include <string.h>
#include "ikemen.h"

/* ----------------------------------------------------------- bit reader */
typedef struct {
    const uint8_t *p;
    size_t len, pos;        /* byte position */
    uint32_t acc;           /* bit accumulator */
    int nbits;              /* bits currently in acc (0..7 leftover + new bytes) */
} Bits;

static void bits_init(Bits *b, const uint8_t *p, size_t len)
{
    b->p = p; b->len = len; b->pos = 0; b->acc = 0; b->nbits = 0;
}

/* Return the next `n` bits (LSB-first, as DEFLATE is little-endian bit-wise).
 * Returns 0 and sets *eof=1 if the stream is exhausted; callers must check. */
static uint32_t bits_get(Bits *b, int n, int *eof)
{
    while (b->nbits < n) {
        if (b->pos >= b->len) { *eof = 1; return 0; }
        b->acc |= (uint32_t)b->p[b->pos++] << b->nbits;
        b->nbits += 8;
    }
    uint32_t v = b->acc & ((1u << n) - 1u);
    b->acc >>= n;
    b->nbits -= n;
    *eof = 0;
    return v;
}

/* ----------------------------------------------------------- Huffman */
/* A canonical Huffman code table. Codes are stored by length so we can decode
 * by reading 1 bit at a time and walking the per-length counts. */
typedef struct {
    /* counts[L] = number of codes of length L (L = 1..15); counts[0] unused */
    int counts[16];
    /* symbols sorted by (length, code); symbol_count total */
    uint16_t syms[288];
    int nsym;
} Huff;

/* Build a canonical Huffman table from code lengths. `lens[i]` is the length
 * of the code for symbol i; n is the number of symbols. Returns 0 on success,
 * -1 if the lengths are impossible (over-/under-subscribed). */
static int huff_build(Huff *h, const uint8_t *lens, int n)
{
    memset(h, 0, sizeof *h);
    for (int i = 0; i < n; i++) if (lens[i] > 15) return -1;
    for (int i = 0; i < n; i++) h->counts[lens[i]]++;
    h->counts[0] = 0;                                   /* length 0 = unused symbol */

    /* check: a complete code has exactly 2^max_length leaf slots filled */
    int left = 1;
    for (int L = 1; L <= 15; L++) {
        left <<= 1;
        left -= h->counts[L];
        if (left < 0) return -1;                        /* over-subscribed */
    }
    /* left > 0 means under-subscribed (incomplete code); that's only legal for
     * the special case of a single 1-bit code (RFC 1951 3.2.7). We accept it but
     * mark the table "single-1-bit" by leaving it as-is; decoding still works
     * because the only code that fits is "0" or "1" of length 1. */

    /* offsets into syms[] for each length: starts where length-L symbols go */
    int offs[16] = {0};
    int acc = 0;
    for (int L = 1; L <= 15; L++) {
        offs[L] = acc;
        acc += h->counts[L];
    }
    h->nsym = acc;
    for (int i = 0; i < n; i++) if (lens[i]) h->syms[offs[lens[i]]++] = (uint16_t)i;
    return 0;
}

/* Decode one symbol. Returns the symbol, or -1 on truncation.
 * Walks the per-length ranges: at length L the codes occupy [first, first+count)
 * and syms[idx..idx+count) are their symbols (sorted in canonical order). */
static int huff_decode(Huff *h, Bits *b)
{
    int code = 0, first = 0, idx = 0;
    for (int L = 1; L <= 15; L++) {
        int eof;
        code |= (int)bits_get(b, 1, &eof);
        if (eof) return -1;
        int count = h->counts[L];
        if (code - first < count)                            /* code in [first, first+count) */
            return h->syms[idx + (code - first)];
        idx += count;
        first = (first + count) << 1;
        code <<= 1;
    }
    return -1;                                              /* ran out of lengths */
}

/* ----------------------------------------------------------- fixed tables */
static void fixed_lengths(uint8_t *litlen, uint8_t *dist)
{
    /* literal/length alphabet (0-287): 0-143 = 8, 144-255 = 9, 256-279 = 7, 280-287 = 8 */
    for (int i = 0; i <= 143; i++) litlen[i] = 8;
    for (int i = 144; i <= 255; i++) litlen[i] = 9;
    for (int i = 256; i <= 279; i++) litlen[i] = 7;
    for (int i = 280; i <= 287; i++) litlen[i] = 8;
    /* distance alphabet (0-29): all 5 bits */
    for (int i = 0; i < 30; i++) dist[i] = 5;
}

/* ----------------------------------------------------------- length/dist tables */
/* length base + extra bits for codes 257..285 */
static const int len_base[29] = {
    3,4,5,6,7,8,9,10, 11,13,15,17, 19,23,27,31, 35,43,51,59,
    67,83,99,115, 131,163,195,227, 258
};
static const int len_extra[29] = {
    0,0,0,0,0,0,0,0, 1,1,1,1, 2,2,2,2, 3,3,3,3,
    4,4,4,4, 5,5,5,5, 0
};
/* distance base + extra bits for codes 0..29 */
static const int dist_base[30] = {
    1,2,3,4, 5,7,9,13, 17,25,33,49, 65,97,129,193,
    257,385,513,769, 1025,1537,2049,3073,
    4097,6145,8193,12289, 16385,24577
};
static const int dist_extra[30] = {
    0,0,0,0, 1,1,2,2, 3,3,4,4, 5,5,6,6,
    7,7,8,8, 9,9,10,10, 11,11,12,12, 13,13
};

/* the code-length alphabet (RFC 1951 3.2.7): the order in which the lengths of
 * the code-length codes themselves are listed in a dynamic block. Note this is
 * NOT the natural order 0,1,2,... - the spec interleaves high and low symbols
 * so that the most commonly-used code-length codes (which are typically the
 * lower values plus 16/17/18 for runs) end up early in the stream. */
static const uint8_t cl_order[19] = {
    16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15
};

/* ----------------------------------------------------------- the decoder */
static int decode_block(Bits *b, uint8_t *out, size_t out_cap, size_t *out_len)
{
    size_t op = 0;                                          /* output position */
    Huff lit, dst;
    int eof;

    for (;;) {
        int final = (int)bits_get(b, 1, &eof);  if (eof) goto trunc;
        int type  = (int)bits_get(b, 2, &eof);  if (eof) goto trunc;

        Huff *L = NULL, *D = NULL;
        if (type == 0) {
            /* stored: skip to a byte boundary (drop any leftover bits in the
             * accumulator - bits_get always leaves < 8 bits, so just clearing
             * acc/nbits moves the read cursor to the next whole input byte at
             * b->p[b->pos]), then read LEN/NLEN, copy LEN bytes verbatim. */
            b->acc = 0;
            b->nbits = 0;
            if (b->pos + 4 > b->len) goto trunc;
            int len  = b->p[b->pos] | (b->p[b->pos + 1] << 8);
            int nlen = b->p[b->pos + 2] | (b->p[b->pos + 3] << 8);
            b->pos += 4;
            if ((len ^ 0xFFFF) != nlen) goto corrupt;       /* LEN/NLEN check */
            if (b->pos + (size_t)len > b->len) goto trunc;
            if (op + (size_t)len > out_cap) goto overflow;
            memcpy(out + op, b->p + b->pos, (size_t)len);
            b->pos += len;
            op += len;
        } else if (type == 1) {
            uint8_t ll[288], dd[30];
            fixed_lengths(ll, dd);
            if (huff_build(&lit, ll, 288) != 0) goto corrupt;
            if (huff_build(&dst, dd, 30) != 0) goto corrupt;
            L = &lit; D = &dst;
        } else if (type == 2) {
            int hlit  = 257 + (int)bits_get(b, 5, &eof);  if (eof) goto trunc;
            int hdist =   1 + (int)bits_get(b, 5, &eof);  if (eof) goto trunc;
            int hclen =   4 + (int)bits_get(b, 4, &eof);  if (eof) goto trunc;
            uint8_t cl[19] = {0};
            for (int i = 0; i < hclen; i++)
                cl[cl_order[i]] = (uint8_t)bits_get(b, 3, &eof);
            if (eof) goto trunc;
            Huff cl_h;
            if (huff_build(&cl_h, cl, 19) != 0) goto corrupt;

            /* Read all hlit+hdist code lengths into one contiguous buffer.
             * ll[0..hlit) holds literal/length lengths, dd[0..hdist) holds
             * distance lengths. RFC 1951's "16" repeat copies the previous
             * length, so we treat the buffer as flat and let it cross the
             * ll/dd boundary (in valid streams this never happens because
             * there's no previous distance length to repeat at the start). */
            uint8_t lens[288 + 30] = {0};
            int total = hlit + hdist, got = 0;
            while (got < total) {
                int sym = huff_decode(&cl_h, b);
                if (sym < 0) goto trunc;
                if (sym < 16) {
                    lens[got++] = (uint8_t)sym;
                } else if (sym == 16) {
                    if (!got) goto corrupt;                  /* no previous length to copy */
                    int rep = 3 + (int)bits_get(b, 2, &eof);  if (eof) goto trunc;
                    uint8_t prev = lens[got - 1];
                    while (rep-- && got < total) lens[got++] = prev;
                } else if (sym == 17) {
                    int rep = 3 + (int)bits_get(b, 3, &eof);  if (eof) goto trunc;
                    while (rep-- && got < total) lens[got++] = 0;
                } else { /* 18 */
                    int rep = 11 + (int)bits_get(b, 7, &eof); if (eof) goto trunc;
                    while (rep-- && got < total) lens[got++] = 0;
                }
            }
            if (huff_build(&lit, lens, hlit) != 0) goto corrupt;
            if (huff_build(&dst, lens + hlit, hdist) != 0) goto corrupt;
            L = &lit; D = &dst;
        } else {
            goto corrupt;                                    /* reserved block type */
        }

        /* If the block was stored (type 0), there are no tokens to decode. */
        if (!L) { if (final) break; continue; }

        /* Decode tokens until end-of-block (256). */
        for (;;) {
            int sym = huff_decode(L, b);
            if (sym < 0) goto trunc;
            if (sym == 256) break;                           /* end of block */
            if (sym < 256) {                                 /* literal */
                if (op + 1 > out_cap) goto overflow;
                out[op++] = (uint8_t)sym;
            } else {                                        /* length/distance pair */
                int li = sym - 257; if (li < 0 || li > 28) goto corrupt;
                int length = len_base[li] + (int)bits_get(b, len_extra[li], &eof);
                if (eof) goto trunc;
                int dsym = huff_decode(D, b);
                if (dsym < 0 || dsym > 29) goto corrupt;
                int distance = dist_base[dsym] + (int)bits_get(b, dist_extra[dsym], &eof);
                if (eof) goto trunc;
                if ((size_t)distance > op) goto corrupt;     /* would copy before start of output */
                if (op + (size_t)length > out_cap) goto overflow;
                /* byte-by-byte copy: handles overlap (length > distance) */
                for (int i = 0; i < length; i++)
                    out[op] = out[op - distance], op++;
            }
        }
        if (final) break;
    }
    *out_len = op;
    return 0;

trunc:    return -1;
corrupt:  return -2;
overflow: return -3;
}

/* Public entry point: skip the zlib header (CMF/FLG + optional FDICT) and decode.
 * PNG IDAT streams always use zlib wrapping (RFC 1950), so we always expect the
 * 2-byte header here. If the caller passes a raw deflate stream, it must call
 * decode_block directly via a separate entry point. */
int inflate(const uint8_t *in, size_t in_len, uint8_t *out, size_t out_cap, size_t *out_len)
{
    if (in_len < 2) return -1;
    /* zlib header: CMF + FLG. CM = lower 4 bits of CMF (must be 8 = deflate);
     * CINFO = upper 4 bits (window size = 2^(CINFO+8), 7..15). FLG bit check:
     * (CMF*256 + FLG) must be divisible by 31. */
    uint8_t cmf = in[0], flg = in[1];
    if ((cmf & 0x0F) != 8) return -2;
    if (((cmf << 8) | flg) % 31 != 0) return -2;
    size_t off = 2;
    if (flg & 0x20) {                                       /* FDICT: 4-byte dict id follows */
        if (in_len < 6) return -1;
        off += 4;
    }
    Bits b;
    bits_init(&b, in + off, in_len - off);
    size_t got = 0;
    int rc = decode_block(&b, out, out_cap, &got);
    if (rc == 0) *out_len = got;
    return rc;
}
