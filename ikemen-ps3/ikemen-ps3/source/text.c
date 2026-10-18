/* text.c - text helpers for the AIR / CMD / CNS parsers. Portable C, read-only file access. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "text.h"

char *text_load(const char *path, size_t max, size_t *len)
{
    FILE *fp = fopen(path, "rb");
    if (!fp) return NULL;
    fseek(fp, 0, SEEK_END);
    long n = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    if (n <= 0 || (unsigned long)n > max) { fclose(fp); return NULL; }
    char *b = malloc((size_t)n + 1);
    if (!b) { fclose(fp); return NULL; }
    if (fread(b, 1, (size_t)n, fp) != (size_t)n) { free(b); fclose(fp); return NULL; }
    fclose(fp);
    b[n] = 0;
    size_t skip = 0;
    if (n >= 3 && (unsigned char)b[0] == 0xEF && (unsigned char)b[1] == 0xBB && (unsigned char)b[2] == 0xBF) skip = 3;
    if (skip) memmove(b, b + skip, (size_t)n - skip + 1);
    *len = (size_t)n - skip;
    return b;
}

int text_next_line(const char *text, size_t len, size_t *pos, char *out, size_t cap)
{
    size_t p = *pos;
    if (p >= len) return 0;
    size_t n = 0;
    int quote = 0, comment = 0;
    while (p < len && text[p] != '\n' && text[p] != '\r') {
        char c = text[p++];
        if (c == '"') quote = !quote;
        else if (c == ';' && !quote) comment = 1;
        if (!comment && n + 1 < cap) out[n++] = c;
    }
    if (p < len && text[p] == '\r') { p++; if (p < len && text[p] == '\n') p++; }
    else if (p < len && text[p] == '\n') p++;
    *pos = p;
    while (n > 0 && isspace((unsigned char)out[n - 1])) n--;
    out[n] = 0;
    size_t lead = 0;
    while (out[lead] && isspace((unsigned char)out[lead])) lead++;
    if (lead) memmove(out, out + lead, n - lead + 1);
    return 1;
}

void text_lower(char *s)
{
    for (; *s; s++) *s = (char)tolower((unsigned char)*s);
}

const char *text_skip_blank(const char *s)
{
    while (*s == ' ' || *s == '\t') s++;
    return s;
}

int text_parse_int(const char *s, int32_t *out)
{
    const char *p = text_skip_blank(s);
    int neg = 0;
    *out = 0;
    if (*p == '-' || *p == '+') { neg = (*p == '-'); p++; }
    if (*p < '0' || *p > '9') return 0;
    int64_t n = 0;
    int64_t limit = neg ? 2147483648LL : 2147483647LL;
    while (*p >= '0' && *p <= '9') {
        n = n * 10 + (*p - '0');
        if (n > limit) n = limit;
        p++;
    }
    *out = (int32_t)(neg ? -n : n);
    return (int)(p - s);
}

int text_parse_float(const char *s, float *out)
{
    const char *p = text_skip_blank(s);
    int neg = 0, digits = 0;
    double v = 0, sc = 0.1;
    *out = 0;
    if (*p == '-' || *p == '+') { neg = (*p == '-'); p++; }
    while (*p >= '0' && *p <= '9') { v = v * 10 + (*p - '0'); if (v > 1e9) v = 1e9; p++; digits++; }
    if (*p == '.') {
        p++;
        while (*p >= '0' && *p <= '9') { v += (*p - '0') * sc; sc *= 0.1; p++; digits++; }
    }
    if (!digits) return 0;
    *out = (float)(neg ? -v : v);
    return (int)(p - s);
}
