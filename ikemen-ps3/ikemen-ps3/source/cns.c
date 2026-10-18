/* cns.c - parser for state files. Controllers attach to the most recent [Statedef], as in MUGEN/Ikemen
 * (the number in "[State N, label]" is only a label). Malformed expressions become NULL, which never fires. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cns.h"
#include "text.h"

static const char *SC_NAMES[SC_COUNT] = {
#define X(id, name) [id] = name,
    CONTROLLER_LIST(X)
#undef X
};

void cns_init(StateDb *db) { memset(db, 0, sizeof *db); }

static void free_ctrl(Ctrl *c)
{
    for (int i = 0; i < c->nall; i++) expr_free(c->all[i]);
    free(c->all);
    for (int g = 0; g < c->ngrp; g++) { for (int i = 0; i < c->grp[g].n; i++) expr_free(c->grp[g].e[i]); free(c->grp[g].e); }
    free(c->grp);
    for (int p = 0; p < c->npar; p++) { free(c->par[p].raw); for (int i = 0; i < c->par[p].ne; i++) expr_free(c->par[p].e[i]); }
    free(c->par);
}

static void free_state(StateDef *s)
{
    expr_free(s->juggle); expr_free(s->ctrl); expr_free(s->anim); expr_free(s->velx); expr_free(s->vely);
    expr_free(s->poweradd); expr_free(s->sprpriority);
    for (int i = 0; i < s->nc; i++) free_ctrl(&s->c[i]);
    free(s->c);
}

void cns_free(StateDb *db)
{
    for (int i = 0; i < db->ns; i++) free_state(&db->s[i]);
    free(db->s); free(db->k);
    memset(db, 0, sizeof *db);
}

static int charge(StateDb *db, size_t n)
{
    db->bytes += n;
    if (db->bytes > CNS_BUDGET) { db->truncated = 1; return -1; }
    return 0;
}

static Expr *compile_charged(StateDb *db, const char *src)
{
    Expr *e = expr_compile(src);
    if (e && charge(db, expr_size(e))) { expr_free(e); return NULL; }
    return e;
}

static int parse_num(const char *s, double *out)
{
    s = text_skip_blank(s);
    const char *p = s;
    int neg = 0, digits = 0;
    double v = 0, f = 0.1;
    if (*p == '-' || *p == '+') { neg = *p == '-'; p++; }
    while (*p >= '0' && *p <= '9') { v = v * 10 + (*p - '0'); if (v > 1e12) v = 1e12; p++; digits++; }
    if (*p == '.') { p++; while (*p >= '0' && *p <= '9') { v += (*p - '0') * f; f *= 0.1; p++; digits++; } }
    if (!digits) return 0;
    *out = neg ? -v : v;
    return 1;
}

static void add_const(StateDb *db, const char *name, double v)
{
    for (int i = 0; i < db->nk; i++) if (!strcmp(db->k[i].name, name)) { db->k[i].v = v; return; }
    if (db->nk >= CNS_MAX_CONSTS) return;
    Const *nk = realloc(db->k, (size_t)(db->nk + 1) * sizeof *nk);
    if (!nk) return;
    db->k = nk;
    snprintf(nk[db->nk].name, sizeof nk[0].name, "%s", name);
    nk[db->nk].v = v;
    db->nk++;
}

static char *trim(char *s)
{
    while (*s == ' ' || *s == '\t') s++;
    size_t n = strlen(s);
    while (n && (s[n - 1] == ' ' || s[n - 1] == '\t')) s[--n] = 0;
    return s;
}

static int sym_code(const char *v, int kind)
{
    char b[8]; snprintf(b, sizeof b, "%s", v); text_lower(b);
    return expr_symbol(kind, trim(b));
}

static int ctrl_type(const char *v)
{
    char b[48]; snprintf(b, sizeof b, "%s", v); text_lower(b);
    char *t = trim(b);
    for (int i = 1; i < SC_COUNT; i++) if (SC_NAMES[i] && !strcmp(SC_NAMES[i], t)) return i;
    return SC_UNKNOWN;
}

static int add_state(StateDb *db, int no, int override, StateDef **out)
{
    *out = NULL;
    for (int i = 0; i < db->ns; i++) {
        StateDef *s = &db->s[i];
        if (s->no != no) continue;
        if (s->gen == db->gen) { *out = s; return 0; }       /* same file defines it again: merge */
        if (!override) return 1;                              /* keep the earlier definition */
        free_state(s);
        memset(s, 0, sizeof *s);
        s->no = no; s->gen = db->gen;
        *out = s;
        return 0;
    }
    if (db->ns >= CNS_MAX_STATEDEFS) { db->truncated = 1; return 1; }
    if (db->ns >= db->cap) {
        int nc = db->cap ? db->cap * 2 : 128;
        if (nc > CNS_MAX_STATEDEFS) nc = CNS_MAX_STATEDEFS;
        StateDef *ns = realloc(db->s, (size_t)nc * sizeof *ns);
        if (!ns) return 1;
        db->s = ns; db->cap = nc;
    }
    StateDef *s = &db->s[db->ns++];
    memset(s, 0, sizeof *s);
    s->no = no; s->gen = db->gen;
    charge(db, sizeof *s);
    *out = s;
    return 0;
}

static Ctrl *add_ctrl(StateDb *db, StateDef *sd, int *total)
{
    if (*total >= CNS_MAX_CTRLS) { db->truncated = 1; return NULL; }
    if (sd->nc >= sd->cap) {
        int nc = sd->cap ? sd->cap * 2 : 8;
        Ctrl *n = realloc(sd->c, (size_t)nc * sizeof *n);
        if (!n) return NULL;
        sd->c = n; sd->cap = nc;
    }
    if (charge(db, sizeof(Ctrl))) return NULL;
    Ctrl *c = &sd->c[sd->nc++];
    memset(c, 0, sizeof *c);
    c->persistent = 1;
    (*total)++;
    return c;
}

static int ptr_push(void ***arr, int *n, void *v)
{
    void **na = realloc(*arr, (size_t)(*n + 1) * sizeof *na);
    if (!na) return -1;
    *arr = na; na[(*n)++] = v;
    return 0;
}

static void set_statedef_key(StateDb *db, StateDef *s, const char *key, const char *val)
{
    if (!strcmp(key, "type")) s->type = sym_code(val, 1);
    else if (!strcmp(key, "movetype")) s->movetype = sym_code(val, 2);
    else if (!strcmp(key, "physics")) s->physics = sym_code(val, 3);
    else if (!strcmp(key, "juggle")) { expr_free(s->juggle); s->juggle = compile_charged(db, val); }
    else if (!strcmp(key, "ctrl")) { expr_free(s->ctrl); s->ctrl = compile_charged(db, val); }
    else if (!strcmp(key, "anim")) { expr_free(s->anim); s->anim = compile_charged(db, val); }
    else if (!strcmp(key, "poweradd")) { expr_free(s->poweradd); s->poweradd = compile_charged(db, val); }
    else if (!strcmp(key, "sprpriority")) { expr_free(s->sprpriority); s->sprpriority = compile_charged(db, val); }
    else if (!strcmp(key, "velset")) {
        static char pc[2][EXPR_MAX_SRC];
        int n = expr_split(val, pc, 2);
        expr_free(s->velx); expr_free(s->vely);
        s->velx = n > 0 ? compile_charged(db, pc[0]) : NULL;
        s->vely = n > 1 ? compile_charged(db, pc[1]) : NULL;
    } else {
        double d;
        int v = parse_num(val, &d) ? (int)d : 0;
        if (!strcmp(key, "facep2")) s->facep2 = v;
        else if (!strcmp(key, "hitcountpersist")) s->hitcountpersist = v;
        else if (!strcmp(key, "movehitpersist")) s->movehitpersist = v;
        else if (!strcmp(key, "hitdefpersist")) s->hitdefpersist = v;
    }
}

static int is_trigger_key(const char *key, int *n)
{
    if (strncmp(key, "trigger", 7)) return 0;
    const char *p = key + 7;
    if (!strcmp(p, "all")) { *n = 0; return 1; }
    int v = 0, d = 0;
    while (*p >= '0' && *p <= '9') { v = v * 10 + (*p - '0'); if (v > 100000) v = 100000; p++; d++; }
    if (!d || *p) return 0;
    *n = v;
    return 1;
}

static void set_ctrl_key(StateDb *db, Ctrl *c, const char *key, const char *val, const char *rawval, int *lines)
{
    int tn;
    if (!strcmp(key, "type")) { c->type = ctrl_type(val); return; }
    if (!strcmp(key, "persistent")) { double d; c->persistent = parse_num(val, &d) ? (int)d : 1; if (c->persistent < 0) c->persistent = 0; return; }
    if (!strcmp(key, "ignorehitpause")) { double d; c->ignorehitpause = parse_num(val, &d) ? (int)d != 0 : 0; return; }
    if (is_trigger_key(key, &tn)) {
        if (*lines >= CNS_MAX_LINES) return;
        (*lines)++;
        Expr *e = compile_charged(db, rawval);
        if (tn == 0) { ptr_push((void ***)&c->all, &c->nall, e); charge(db, sizeof(void *)); return; }
        if (tn > CNS_MAX_GROUPS) { expr_free(e); return; }
        if (tn > c->ngrp) {
            Group *ng = realloc(c->grp, (size_t)tn * sizeof *ng);
            if (!ng) { expr_free(e); return; }
            memset(ng + c->ngrp, 0, (size_t)(tn - c->ngrp) * sizeof *ng);
            c->grp = ng; c->ngrp = tn;
            charge(db, (size_t)tn * sizeof *ng);
        }
        ptr_push((void ***)&c->grp[tn - 1].e, &c->grp[tn - 1].n, e);
        return;
    }
    if (c->npar >= CNS_MAX_PARAMS) return;
    Param *np = realloc(c->par, (size_t)(c->npar + 1) * sizeof *np);
    if (!np) return;
    c->par = np;
    Param *p = &np[c->npar];
    memset(p, 0, sizeof *p);
    snprintf(p->name, sizeof p->name, "%s", key);
    size_t rl = strlen(rawval);
    p->raw = malloc(rl + 1);
    if (!p->raw) return;
    memcpy(p->raw, rawval, rl + 1);
    if (charge(db, sizeof *p + rl + 1)) { free(p->raw); return; }
    c->npar++;
    static char pc[CNS_MAX_VALUES][EXPR_MAX_SRC];
    int n = expr_split(rawval, pc, CNS_MAX_VALUES);
    for (int i = 0; i < n; i++) p->e[i] = pc[i][0] ? compile_charged(db, pc[i]) : NULL;
    p->ne = n;
}

int cns_parse(const char *text, size_t len, StateDb *db, int override)
{
    db->gen++;
    enum { NONE, CONSTS, STATEDEF, STATE } sec = NONE;
    const char *prefix = "";
    StateDef *sd = NULL;
    Ctrl *cc = NULL;
    int skip_state = 0, lines = 0;
    int total = 0;
    for (int i = 0; i < db->ns; i++) total += db->s[i].nc;
    char line[TEXT_LINE_MAX], low[TEXT_LINE_MAX];
    size_t pos = 0;
    while (!db->truncated && text_next_line(text, len, &pos, line, sizeof line)) {
        if (!line[0]) continue;
        snprintf(low, sizeof low, "%s", line);
        text_lower(low);
        if (line[0] == '[') {
            cc = NULL;
            const char *s = text_skip_blank(low + 1);
            if (!strncmp(s, "statedef", 8)) {
                sec = STATEDEF; sd = NULL; skip_state = 0;
                int32_t no;
                if (text_parse_int(s + 8, &no)) {
                    if (add_state(db, no, override, &sd)) skip_state = 1;
                } else skip_state = 1;
                if (skip_state) sec = NONE;
            } else if (!strncmp(s, "state", 5) && sd && !skip_state) {
                sec = STATE; lines = 0;
                cc = add_ctrl(db, sd, &total);
                if (!cc) sec = NONE;
            } else if (!strncmp(s, "state", 5)) {
                sec = NONE;
            } else if (!strncmp(s, "data", 4)) { sec = CONSTS; prefix = "data."; }
            else if (!strncmp(s, "size", 4)) { sec = CONSTS; prefix = "size."; }
            else if (!strncmp(s, "velocity", 8)) { sec = CONSTS; prefix = "velocity."; }
            else if (!strncmp(s, "movement", 8)) { sec = CONSTS; prefix = "movement."; }
            else sec = NONE;
            continue;
        }
        char *eq = strchr(line, '=');
        if (!eq || sec == NONE) continue;
        char keybuf[96];
        size_t kl = (size_t)(eq - line);
        if (kl >= sizeof keybuf) continue;
        memcpy(keybuf, low, kl); keybuf[kl] = 0;
        char *key = trim(keybuf);
        char valbuf[TEXT_LINE_MAX]; snprintf(valbuf, sizeof valbuf, "%s", eq + 1);
        char *raw = trim(valbuf);
        char lowval[TEXT_LINE_MAX]; snprintf(lowval, sizeof lowval, "%s", raw); text_lower(lowval);
        if (sec == CONSTS) {
            char name[48]; double v[2]; int nv = 0;
            const char *q = raw;
            while (nv < 2) {
                if (!parse_num(q, &v[nv])) break;
                nv++;
                const char *c = strchr(q, ',');
                if (!c) break;
                q = c + 1;
            }
            if (!nv) continue;
            snprintf(name, sizeof name, "%s%s", prefix, key);
            add_const(db, name, v[0]);
            if (nv == 2) {
                snprintf(name, sizeof name, "%s%s.x", prefix, key); add_const(db, name, v[0]);
                snprintf(name, sizeof name, "%s%s.y", prefix, key); add_const(db, name, v[1]);
            } else if (!strcmp(prefix, "velocity.")) {
                snprintf(name, sizeof name, "%s%s.x", prefix, key); add_const(db, name, v[0]);
            }
        } else if (sec == STATEDEF && sd) {
            set_statedef_key(db, sd, key, raw);
        } else if (sec == STATE && cc) {
            set_ctrl_key(db, cc, key, lowval, raw, &lines);
        }
    }
    return 0;
}

int cns_load(const char *path, StateDb *db, int override)
{
    size_t len;
    char *t = text_load(path, CNS_MAX_FILE, &len);
    if (!t) return -1;
    int rc = cns_parse(t, len, db, override);
    free(t);
    return rc;
}

static int cmp_state(const void *a, const void *b)
{
    int x = ((const StateDef *)a)->no, y = ((const StateDef *)b)->no;
    return x < y ? -1 : x > y;
}

void cns_finish(StateDb *db) { if (db->ns > 1) qsort(db->s, (size_t)db->ns, sizeof(StateDef), cmp_state); }

const StateDef *cns_state(const StateDb *db, int no)
{
    int lo = 0, hi = db->ns - 1;
    while (lo <= hi) {
        int m = (lo + hi) / 2;
        if (db->s[m].no == no) return &db->s[m];
        if (db->s[m].no < no) lo = m + 1; else hi = m - 1;
    }
    return NULL;
}

double cns_const(const StateDb *db, const char *name, double dflt)
{
    for (int i = 0; i < db->nk; i++) if (!strcmp(db->k[i].name, name)) return db->k[i].v;
    return dflt;
}

const Param *ctrl_param(const Ctrl *c, const char *name)
{
    for (int i = 0; i < c->npar; i++) if (!strcmp(c->par[i].name, name)) return &c->par[i];
    return NULL;
}
