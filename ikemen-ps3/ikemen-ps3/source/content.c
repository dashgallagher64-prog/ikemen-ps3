/* content.c - discovers characters, stages and screenpacks in USRDIR.
 * Adding content = copy a folder/file over FTP, no rebuild or edit needed.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <dirent.h>
#include <sys/stat.h>
#include "ikemen.h"

static int is_dir(const char *p)
{
    struct stat st;
    return stat(p, &st) == 0 && S_ISDIR(st.st_mode);
}

static int has_ext(const char *name, const char *ext)
{
    size_t a = strlen(name), b = strlen(ext);
    return a > b && !strcasecmp(name + a - b, ext);
}

static int cmp_entry(const void *a, const void *b)
{
    return strcasecmp(((const ContentEntry *)a)->name, ((const ContentEntry *)b)->name);
}

static void sort_list(ContentList *l)
{
    qsort(l->e, l->n, sizeof(ContentEntry), cmp_entry);
}

/* Copy a path from a .def into out, turning MUGEN-style backslashes into '/'.
 * Returns -1 (and out is unusable) if it is empty, absolute, has a drive/device colon, or has a ".." part.
 * Content can therefore never make the game open anything outside the folder its .def lives in. */
static int safe_rel(const char *in, char *out, size_t n)
{
    size_t len = strlen(in);
    if (len == 0 || len >= n) return -1;
    for (size_t i = 0; i <= len; i++) out[i] = (in[i] == '\\') ? '/' : in[i];
    if (out[0] == '/' || strchr(out, ':')) return -1;
    for (const char *p = out; *p; ) {
        const char *q = strchr(p, '/');
        size_t part = q ? (size_t)(q - p) : strlen(p);
        if (part == 2 && p[0] == '.' && p[1] == '.') return -1;
        if (!q) break;
        p = q + 1;
    }
    return 0;
}

/* Fill display name from [Info] name, falling back to the given default. */
static void read_name(ContentEntry *e, const char *fallback)
{
    DefFile f;
    const char *n = NULL;
    if (def_load(&f, e->def) == 0) {
        n = def_get(&f, "Info", "displayname");
        if (!n || !*n) n = def_get(&f, "Info", "name");
    }
    snprintf(e->name, sizeof e->name, "%s", (n && *n) ? n : fallback);

    e->sprite[0] = 0;
    if (f.e) {
        const char *s = def_get(&f, "Files", "sprite");
        char rel[IK_PATH_LEN];
        /* a .def may only point at files inside its own folder tree (no "..", no absolute paths) */
        if (s && *s && safe_rel(s, rel, sizeof rel) == 0)
            snprintf(e->sprite, sizeof e->sprite, "%s/%s", e->dir, rel);
        def_free(&f);
    }
}

void content_scan_chars(ContentList *out, const char *root)
{
    char base[IK_PATH_LEN];
    out->n = 0;
    snprintf(base, sizeof base, "%s/chars", root);
    DIR *d = opendir(base);
    if (!d) return;

    struct dirent *de;
    while ((de = readdir(d)) && out->n < IK_MAX_ENTRIES) {
        if (de->d_name[0] == '.') continue;
        char dir[IK_PATH_LEN];
        snprintf(dir, sizeof dir, "%s/%s", base, de->d_name);
        if (!is_dir(dir)) continue;

        /* prefer <dir>/<dir>.def, else the first .def in the folder */
        char want[IK_NAME_LEN + 8], found[IK_NAME_LEN + 8] = "";
        snprintf(want, sizeof want, "%s.def", de->d_name);
        DIR *sd = opendir(dir);
        if (!sd) continue;
        struct dirent *fe;
        while ((fe = readdir(sd))) {
            if (!has_ext(fe->d_name, ".def")) continue;
            if (!strcasecmp(fe->d_name, want)) { snprintf(found, sizeof found, "%s", fe->d_name); break; }
            if (!found[0]) snprintf(found, sizeof found, "%s", fe->d_name);
        }
        closedir(sd);
        if (!found[0]) continue;

        ContentEntry *e = &out->e[out->n++];
        snprintf(e->dir, sizeof e->dir, "%s", dir);
        snprintf(e->def, sizeof e->def, "%s/%s", dir, found);
        read_name(e, de->d_name);
    }
    closedir(d);
    sort_list(out);
}

void content_scan_stages(ContentList *out, const char *root)
{
    char base[IK_PATH_LEN];
    out->n = 0;
    snprintf(base, sizeof base, "%s/stages", root);
    DIR *d = opendir(base);
    if (!d) return;

    struct dirent *de;
    while ((de = readdir(d)) && out->n < IK_MAX_ENTRIES) {
        if (!has_ext(de->d_name, ".def")) continue;
        ContentEntry *e = &out->e[out->n++];
        snprintf(e->dir, sizeof e->dir, "%s", base);
        snprintf(e->def, sizeof e->def, "%s/%s", base, de->d_name);
        char stem[IK_NAME_LEN];
        snprintf(stem, sizeof stem, "%s", de->d_name);
        stem[strlen(stem) - 4] = 0;
        read_name(e, stem);
    }
    closedir(d);
    sort_list(out);
}

void content_scan_screenpacks(ContentList *out, const char *root)
{
    char base[IK_PATH_LEN], p[IK_PATH_LEN];
    out->n = 0;
    snprintf(base, sizeof base, "%s/data", root);

    /* the stock motif: data/system.def */
    snprintf(p, sizeof p, "%s/system.def", base);
    FILE *fp = fopen(p, "rb");
    if (fp) {
        fclose(fp);
        ContentEntry *e = &out->e[out->n++];
        snprintf(e->dir, sizeof e->dir, "%s", base);
        snprintf(e->def, sizeof e->def, "%s", p);
        e->sprite[0] = 0;
        snprintf(e->name, sizeof e->name, "default");
    }

    /* custom packs: data/<pack>/system.def */
    DIR *d = opendir(base);
    if (!d) return;
    struct dirent *de;
    while ((de = readdir(d)) && out->n < IK_MAX_ENTRIES) {
        if (de->d_name[0] == '.') continue;
        char dir[IK_PATH_LEN];
        snprintf(dir, sizeof dir, "%s/%s", base, de->d_name);
        if (!is_dir(dir)) continue;
        snprintf(p, sizeof p, "%s/system.def", dir);
        fp = fopen(p, "rb");
        if (!fp) continue;
        fclose(fp);
        ContentEntry *e = &out->e[out->n++];
        snprintf(e->dir, sizeof e->dir, "%s", dir);
        snprintf(e->def, sizeof e->def, "%s", p);
        e->sprite[0] = 0;
        snprintf(e->name, sizeof e->name, "%s", de->d_name);
    }
    closedir(d);
    /* keep "default" first, sort the rest */
    int start = (out->n && !strcmp(out->e[0].name, "default")) ? 1 : 0;
    qsort(out->e + start, out->n - start, sizeof(ContentEntry), cmp_entry);
}
