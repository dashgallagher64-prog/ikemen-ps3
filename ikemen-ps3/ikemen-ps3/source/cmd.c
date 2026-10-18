/* cmd.c - parsing [Command] definitions and matching them against input history.
 * Rules (press / hold / release, ~N charge, $ four-way, > no-other-input, a+b, same-name commands act as OR,
 * buffer.time, time window) follow the MUGEN CMD documentation; the matcher is written from scratch. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cmd.h"
#include "text.h"

static const struct { const char *w; uint16_t m; } DIRW[8] = {
    {"B", CK_BACK}, {"DB", CK_DOWN | CK_BACK}, {"D", CK_DOWN}, {"DF", CK_DOWN | CK_FWD},
    {"F", CK_FWD},  {"UF", CK_UP | CK_FWD},    {"U", CK_UP},   {"UB", CK_UP | CK_BACK}
};

static uint16_t btn_bit(char c)
{
    switch (c) {
    case 'a': return CK_A; case 'b': return CK_B; case 'c': return CK_C;
    case 'x': return CK_X; case 'y': return CK_Y; case 'z': return CK_Z; case 's': return CK_S;
    }
    return 0;
}
static const char BTNS[] = "abcxyzs";

static char *trim(char *s)
{
    while (*s == ' ' || *s == '\t') s++;
    size_t n = strlen(s);
    while (n && (s[n - 1] == ' ' || s[n - 1] == '\t')) s[--n] = 0;
    return s;
}

static void unquote(char *s)
{
    size_t n = strlen(s);
    if (n >= 2 && s[0] == '"' && s[n - 1] == '"') { memmove(s, s + 1, n - 2); s[n - 2] = 0; }
}

/* parses one symbol token; returns 0 on failure */
static int parse_sym(char *tok, const char remap[7], CmdSym *out)
{
    memset(out, 0, sizeof *out);
    tok = trim(tok);
    for (;;) {
        if (*tok == '>') { out->gt = 1; tok++; }
        else if (*tok == '~') { out->kind = 2; tok++; }
        else if (*tok == '/') { out->kind = 1; tok++; }
        else if (*tok == '$') { out->fourway = 1; tok++; }
        else if (*tok == '+') tok++;
        else if (*tok >= '0' && *tok <= '9') {
            int32_t v; int used = text_parse_int(tok, &v);
            out->hold = (uint16_t)(v > CMD_HIST - 8 ? CMD_HIST - 8 : v);
            tok += used;
        } else break;
    }
    if (!*tok) return 0;
    while (*tok) {
        char *plus = strchr(tok, '+');
        if (plus) *plus = 0;
        char *w = trim(tok);
        int ok = 0;
        for (int i = 0; i < 8; i++) if (!strcmp(w, DIRW[i].w)) { out->dirmask |= DIRW[i].m; ok = 1; }
        if (!ok && w[0] && !w[1]) {
            char *p = strchr(BTNS, w[0]);
            if (p && *p) { out->btnmask |= btn_bit(remap[p - BTNS]); ok = 1; }
        }
        if (!ok) return 0;
        if (!plus) break;
        tok = plus + 1;
    }
    return out->dirmask || out->btnmask;
}

static int parse_command(const char *value, const char remap[7], Command *c)
{
    char buf[TEXT_LINE_MAX];
    snprintf(buf, sizeof buf, "%s", value);
    c->nsym = 0;
    char *p = buf;
    for (;;) {
        char *comma = strchr(p, ',');
        if (comma) *comma = 0;
        CmdSym s;
        if (!parse_sym(p, remap, &s)) return -1;
        /* successive identical plain directions: F, F  ->  F, >~F, >F */
        if (c->nsym > 0 && !s.btnmask && s.kind == 0) {
            CmdSym *pr = &c->sym[c->nsym - 1];
            if (!pr->btnmask && pr->kind == 0 && pr->dirmask == s.dirmask && !pr->fourway && !s.fourway) {
                if (c->nsym + 2 > CMD_MAX_SYMBOLS) return -1;
                CmdSym r = s; r.kind = 2; r.gt = 1; r.hold = 0;
                c->sym[c->nsym++] = r;
                s.gt = 1;
            }
        }
        if (c->nsym >= CMD_MAX_SYMBOLS) return -1;
        c->sym[c->nsym++] = s;
        if (!comma) break;
        p = comma + 1;
    }
    return c->nsym > 0 ? 0 : -1;
}

static int key_eq(const char *line, const char *key)
{
    size_t n = strlen(key);
    if (strncmp(line, key, n)) return 0;
    const char *p = text_skip_blank(line + n);
    return *p == '=';
}
static const char *val_of(const char *line) { return text_skip_blank(strchr(line, '=') + 1); }

int cmd_parse(const char *text, size_t len, CmdList *out)
{
    memset(out, 0, sizeof *out);
    return cmd_parse_append(text, len, out);
}

int cmd_parse_append(const char *text, size_t len, CmdList *out)
{
    char remap[7]; memcpy(remap, BTNS, 7);
    int deftime = 15, defbuf = 1;
    enum { NONE, DEFAULTS, REMAP, COMMAND } sec = NONE;
    Command cur; int have = 0; uint32_t cap = (uint32_t)out->n;
    int has_time = 0, has_buf = 0;
    char line[TEXT_LINE_MAX], low[TEXT_LINE_MAX];
    size_t pos = 0;

#define FLUSH() do { if (have) { \
        if (cur.nsym > 0 && out->n < CMD_MAX_COMMANDS) { \
            if (!has_time) cur.time = deftime; \
            if (!has_buf) cur.buftime = defbuf; \
            if (cur.time < 1) cur.time = 1; \
            if (cur.time > CMD_HIST - 8) cur.time = CMD_HIST - 8; \
            if (cur.buftime < 1) cur.buftime = 1; \
            if (cur.buftime > 120) cur.buftime = 120; \
            if ((uint32_t)out->n >= cap) { cap = cap ? cap * 2 : 64; \
                Command *nc = realloc(out->c, cap * sizeof *nc); if (!nc) { cap = (uint32_t)out->n; } else out->c = nc; } \
            if ((uint32_t)out->n < cap) out->c[out->n++] = cur; \
        } have = 0; } } while (0)

    while (text_next_line(text, len, &pos, line, sizeof line)) {
        if (!line[0]) continue;
        if (line[0] == '[') {
            FLUSH();
            snprintf(low, sizeof low, "%s", line);
            text_lower(low);
            if (!strncmp(low, "[command]", 9)) {
                sec = COMMAND; have = 1; has_time = has_buf = 0;
                memset(&cur, 0, sizeof cur);
                cur.name[0] = 0;
            } else if (!strncmp(low, "[defaults]", 10)) sec = DEFAULTS;
            else if (!strncmp(low, "[remap]", 7)) sec = REMAP;
            else sec = NONE;
            continue;
        }
        if (!strchr(line, '=')) continue;
        snprintf(low, sizeof low, "%s", line);
        text_lower(low);
        if (sec == DEFAULTS) {
            int32_t v;
            if (key_eq(low, "command.time")) { text_parse_int(val_of(low), &v); deftime = v; }
            else if (key_eq(low, "command.buffer.time")) { text_parse_int(val_of(low), &v); defbuf = v; }
        } else if (sec == REMAP) {
            char *eq = strchr(low, '=');
            char *k = trim(low); char *v = eq + 1; *eq = 0; k = trim(low); v = trim(v);
            if (k[0] && !k[1] && v[0] && !v[1]) {
                const char *pk = strchr(BTNS, k[0]), *pv = strchr(BTNS, v[0]);
                if (pk && pv && *pk && *pv) remap[pk - BTNS] = v[0];
            }
        } else if (sec == COMMAND && have) {
            int32_t v;
            if (key_eq(low, "name")) {
                char tmp[TEXT_LINE_MAX]; snprintf(tmp, sizeof tmp, "%s", val_of(line));
                char *t = trim(tmp); unquote(t);
                snprintf(cur.name, sizeof cur.name, "%s", t);
            } else if (key_eq(low, "command")) {
                char tmp[TEXT_LINE_MAX]; snprintf(tmp, sizeof tmp, "%s", val_of(line));
                Command t = cur;
                if (parse_command(trim(tmp), remap, &t) == 0) cur = t; else cur.nsym = 0;
            } else if (key_eq(low, "time")) { text_parse_int(val_of(low), &v); cur.time = v; has_time = 1; }
            else if (key_eq(low, "buffer.time")) { text_parse_int(val_of(low), &v); cur.buftime = v; has_buf = 1; }
        }
    }
    FLUSH();
#undef FLUSH
    return 0;
}

int cmd_load(const char *path, CmdList *out)
{
    size_t len;
    memset(out, 0, sizeof *out);
    char *t = text_load(path, CMD_MAX_FILE, &len);
    if (!t) return -1;
    int rc = cmd_parse(t, len, out);
    free(t);
    return rc;
}

void cmd_free(CmdList *l) { free(l->c); memset(l, 0, sizeof *l); }

/* ---------------------------------------------------------------------------- matching */

static uint16_t snap(const CmdState *s, int off)
{
    if (off < 0 || off >= CMD_HIST) return 0;
    return s->hist[(s->head - off) & (CMD_HIST - 1)];
}

static int cond(const CmdSym *y, uint16_t k)
{
    if (y->btnmask && (k & y->btnmask) != y->btnmask) return 0;
    if (y->dirmask) {
        if (y->fourway) { if ((k & y->dirmask) != y->dirmask) return 0; }
        else if ((k & CK_DIRS) != y->dirmask) return 0;
    }
    return 1;
}

/* does symbol y happen at tick offset o (o = ticks ago)? */
static int sym_at(const CmdState *s, const CmdSym *y, int o)
{
    uint16_t now = snap(s, o), prv = snap(s, o + 1);
    int cn = cond(y, now), cp = cond(y, prv);
    if (y->kind == 1) return cn;
    if (y->kind == 0) return cn && !cp;
    if (cn || !cp) return 0;
    int held = 0;
    while (held < CMD_HIST && cond(y, snap(s, o + 1 + held))) held++;
    return held >= y->hold;
}

static int match(const CmdState *s, const Command *c)
{
    int next = 0;                      /* offset of the symbol matched after the one being searched */
    for (int i = c->nsym - 1; i >= 0; i--) {
        const CmdSym *y = &c->sym[i];
        int lo, hi = c->time - 1;
        if (i == c->nsym - 1) { lo = 0; hi = 0; }
        else lo = next;                 /* a release and the next press may share a tick (D -> DF) */
        int found = -1;
        for (int o = lo; o <= hi; o++) {
            if (!sym_at(s, y, o)) continue;
            if (i < c->nsym - 1 && c->sym[i + 1].gt) {
                /* nothing may change between this symbol and the next one, other than the next one itself */
                int ok = 1;
                for (int k = next + 1; k < o && ok; k++) if (snap(s, k) != snap(s, o)) ok = 0;
                const CmdSym *z = &c->sym[i + 1];
                uint16_t allowed = (uint16_t)(z->btnmask | (z->dirmask ? CK_DIRS : 0));
                uint16_t diff = (uint16_t)(snap(s, next) ^ snap(s, next + 1));
                if (next + 1 < o) { if (diff & ~allowed) ok = 0; }
                if (!ok) continue;
            }
            found = o; break;
        }
        if (found < 0) return 0;
        next = found;
    }
    return 1;
}

int cmdstate_init(CmdState *s, const CmdList *l)
{
    memset(s, 0, sizeof *s);
    s->l = l;
    s->buf = calloc((size_t)(l->n ? l->n : 1), sizeof(int));
    return s->buf ? 0 : -1;
}

void cmdstate_free(CmdState *s) { free(s->buf); memset(s, 0, sizeof *s); }

void cmdstate_clear(CmdState *s)
{
    memset(s->hist, 0, sizeof s->hist);
    if (s->buf) memset(s->buf, 0, (size_t)s->l->n * sizeof(int));
}

void cmdstate_tick(CmdState *s, uint16_t keys)
{
    s->head = (s->head + 1) & (CMD_HIST - 1);
    s->hist[s->head] = keys;
    for (int i = 0; i < s->l->n; i++) {
        const Command *c = &s->l->c[i];
        if (match(s, c)) s->buf[i] = c->buftime;
        else if (s->buf[i] > 0) s->buf[i]--;
    }
}

int cmdstate_active(const CmdState *s, const char *name)
{
    for (int i = 0; i < s->l->n; i++)
        if (s->buf[i] > 0 && !strcmp(s->l->c[i].name, name)) return 1;
    return 0;
}
