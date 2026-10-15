/* def.c - tiny INI/DEF parser (MUGEN/Ikemen style). Portable C, no PS3 deps. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>
#include "ikemen.h"

static char *trim(char *s)
{
    while (*s && isspace((unsigned char)*s)) s++;
    char *e = s + strlen(s);
    while (e > s && isspace((unsigned char)e[-1])) *--e = 0;
    return s;
}

/* Cut a ';' comment, ignoring ';' inside double quotes. */
static void strip_comment(char *s)
{
    int q = 0;
    for (; *s; s++) {
        if (*s == '"') q = !q;
        else if (*s == ';' && !q) { *s = 0; return; }
    }
}

static void unquote(char *s)
{
    size_t n = strlen(s);
    if (n >= 2 && s[0] == '"' && s[n - 1] == '"') {
        memmove(s, s + 1, n - 2);
        s[n - 2] = 0;
    }
}

static int add(DefFile *f, const char *sec, const char *key, const char *val)
{
    if (f->n == f->cap) {
        int nc = f->cap ? f->cap * 2 : 64;
        DefEntry *ne = realloc(f->e, nc * sizeof(DefEntry));
        if (!ne) return -1;
        f->e = ne; f->cap = nc;
    }
    DefEntry *d = &f->e[f->n++];
    snprintf(d->section, sizeof d->section, "%s", sec);
    snprintf(d->key,     sizeof d->key,     "%s", key);
    snprintf(d->value,   sizeof d->value,   "%s", val);
    return 0;
}

int def_load(DefFile *f, const char *path)
{
    memset(f, 0, sizeof *f);
    FILE *fp = fopen(path, "rb");
    if (!fp) return -1;

    char line[512], sec[32] = "";
    int first = 1;
    while (fgets(line, sizeof line, fp)) {
        char *p = line;
        /* skip UTF-8 BOM */
        if (first && (unsigned char)p[0] == 0xEF && (unsigned char)p[1] == 0xBB &&
            (unsigned char)p[2] == 0xBF) p += 3;
        first = 0;

        strip_comment(p);
        p = trim(p);
        if (!*p) continue;

        if (*p == '[') {
            char *e = strchr(p, ']');
            if (!e) continue;
            *e = 0;
            snprintf(sec, sizeof sec, "%s", trim(p + 1));
            continue;
        }
        char *eq = strchr(p, '=');
        if (!eq) continue;
        *eq = 0;
        char *k = trim(p), *v = trim(eq + 1);
        unquote(v);
        if (add(f, sec, k, v) < 0) { fclose(fp); return -1; }
    }
    fclose(fp);
    return 0;
}

void def_free(DefFile *f)
{
    free(f->e);
    memset(f, 0, sizeof *f);
}

const char *def_get(const DefFile *f, const char *section, const char *key)
{
    for (int i = 0; i < f->n; i++)
        if (!strcasecmp(f->e[i].section, section) && !strcasecmp(f->e[i].key, key))
            return f->e[i].value;
    return NULL;
}
