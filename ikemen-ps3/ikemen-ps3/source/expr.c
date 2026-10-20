/* expr.c - compiler and evaluator for CNS / CMD expressions.
 * Written from the MUGEN trigger documentation and checked against Ikemen GO's compiler.go for the odd forms
 * (AnimElem = n, op m / TimeMod = a, b / Command = "x" / HitDefAttr = SCA, NA / intervals / redirects). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "expr.h"
#include "text.h"

enum { OP_PUSHI, OP_PUSHF, OP_TRIG, OP_NEG, OP_NOT, OP_BNOT, OP_ADD, OP_SUB, OP_MUL, OP_DIV, OP_MOD, OP_POW,
       OP_LT, OP_GT, OP_LE, OP_GE, OP_EQ, OP_NE, OP_BAND, OP_BXOR, OP_BOR, OP_LXOR, OP_AND_SC, OP_OR_SC, OP_TOBOOL,
       OP_INTERVAL, OP_ASSIGN, OP_REDIR, OP_ENDREDIR, OP_HITATTR };

typedef struct { uint8_t op, nargs; int16_t a, s; double d; } Insn;

struct Expr {
    uint16_t n;
    uint8_t  nstr;
    char     strs[EXPR_MAX_STR][EXPR_STR_LEN];
    Insn     ins[1];
};

typedef struct { const char *name; int arity, kind; } TrigInfo;
static const TrigInfo TRIGS[TG_COUNT] = {
#define X(id, name, ar, kind) [id] = { name, ar, kind },
    TRIGGER_LIST(X)
#undef X
};

/* ---------------------------------------------------------------------------- tokenizer */
enum { T_END, T_NUM, T_STR, T_ID, T_OP };
#define MAX_TOK 400
typedef struct { int t; char s[EXPR_STR_LEN]; double v; int isf; } Tok;

static int tokenize(const char *p, Tok *tk, int *nt)
{
    int n = 0;
    for (;;) {
        while (*p == ' ' || *p == '\t') p++;
        if (!*p) break;
        if (n >= MAX_TOK - 1) return -1;
        Tok *t = &tk[n];
        memset(t, 0, sizeof *t);
        if ((*p >= '0' && *p <= '9') || (*p == '.' && p[1] >= '0' && p[1] <= '9')) {
            double v = 0; int isf = 0;
            while (*p >= '0' && *p <= '9') { v = v * 10 + (*p - '0'); if (v > 1e15) v = 1e15; p++; }
            if (*p == '.') {
                isf = 1; p++;
                double f = 0.1;
                while (*p >= '0' && *p <= '9') { v += (*p - '0') * f; f *= 0.1; p++; }
            }
            if (!isf && v > 2147483647.0) v = 2147483647.0;
            t->t = T_NUM; t->v = v; t->isf = isf;
        } else if ((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') || *p == '_') {
            int k = 0;
            while ((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') || *p == '_' || (*p >= '0' && *p <= '9') || *p == '.') {
                if (k >= EXPR_STR_LEN - 1) return -1;
                char c = *p++;
                t->s[k++] = (char)((c >= 'A' && c <= 'Z') ? c + 32 : c);
            }
            t->t = T_ID;
        } else if (*p == '"') {
            int k = 0; p++;
            while (*p && *p != '"') { if (k >= EXPR_STR_LEN - 1) return -1; t->s[k++] = *p++; }
            if (*p != '"') return -1;
            p++;
            t->t = T_STR;
        } else {
            static const char *two[] = { "&&", "||", "^^", "!=", "<=", ">=", "**", ":=" };
            t->t = T_OP;
            int found = 0;
            for (unsigned i = 0; i < sizeof two / sizeof *two; i++)
                if (p[0] == two[i][0] && p[1] == two[i][1]) { t->s[0] = p[0]; t->s[1] = p[1]; p += 2; found = 1; break; }
            if (!found) {
                if (!strchr("+-*/%=<>!&|^~()[],", *p)) return -1;
                t->s[0] = *p++;
            }
        }
        n++;
    }
    tk[n].t = T_END; tk[n].s[0] = 0;
    *nt = n;
    return 0;
}

/* ---------------------------------------------------------------------------- compiler */
typedef struct {
    Tok  tk[MAX_TOK];
    int  nt, pos;
    Insn ins[EXPR_MAX_INSN];
    int  ni;
    char strs[EXPR_MAX_STR][EXPR_STR_LEN];
    int  ns;
    int  depth, err;
    int  lastkind;
} P;

static const Tok *cur(P *p) { return &p->tk[p->pos]; }
static int is_op(P *p, const char *s) { const Tok *t = cur(p); return t->t == T_OP && !strcmp(t->s, s); }
static void adv(P *p) { if (p->tk[p->pos].t != T_END) p->pos++; }
static int accept(P *p, const char *s) { if (is_op(p, s)) { adv(p); return 1; } return 0; }
static void expect(P *p, const char *s) { if (!accept(p, s)) p->err = 1; }

static int emit(P *p, int op, int nargs, int a, int s, double d)
{
    if (p->ni >= EXPR_MAX_INSN) { p->err = 1; return p->ni - 1; }
    Insn *i = &p->ins[p->ni];
    i->op = (uint8_t)op; i->nargs = (uint8_t)nargs; i->a = (int16_t)a; i->s = (int16_t)s; i->d = d;
    return p->ni++;
}

static int add_str(P *p, const char *s)
{
    for (int i = 0; i < p->ns; i++) if (!strcmp(p->strs[i], s)) return i;
    if (p->ns >= EXPR_MAX_STR) { p->err = 1; return 0; }
    snprintf(p->strs[p->ns], EXPR_STR_LEN, "%s", s);
    return p->ns++;
}

static int find_trig(const char *name)
{
    for (int i = 0; i < TG_COUNT; i++) if (!strcmp(TRIGS[i].name, name)) return i;
    return -1;
}

static int redirect_kind(const char *s)
{
    static const struct { const char *n; int k; } R[] = {
        {"p2", RD_P2}, {"parent", RD_PARENT}, {"root", RD_ROOT}, {"helper", RD_HELPER}, {"enemy", RD_ENEMY},
        {"target", RD_TARGET}, {"partner", RD_PARTNER}, {"enemynear", RD_ENEMYNEAR}, {"playerid", RD_PLAYERID}, {"self", RD_SELF} };
    for (unsigned i = 0; i < sizeof R / sizeof *R; i++) if (!strcmp(s, R[i].n)) return R[i].k;
    return 0;
}

int expr_symbol(int kind, const char *n)
{
    if (kind == 1) { if (!strcmp(n, "s")) return 1; if (!strcmp(n, "c")) return 2; if (!strcmp(n, "a")) return 3; if (!strcmp(n, "l")) return 4; }
    if (kind == 2) { if (!strcmp(n, "i")) return 1; if (!strcmp(n, "a")) return 2; if (!strcmp(n, "h")) return 3; }
    if (kind == 3) { if (!strcmp(n, "s")) return 1; if (!strcmp(n, "c")) return 2; if (!strcmp(n, "a")) return 3; if (!strcmp(n, "n")) return 4; }
    return 0;
}

static void p_bin(P *p, int level);
static void p_unary(P *p);

static void push_num(P *p, double v, int isf) { emit(p, isf ? OP_PUSHF : OP_PUSHI, 0, 0, -1, v); }

static int is_interval(P *p)
{
    if (is_op(p, "[")) return 1;
    if (!is_op(p, "(")) return 0;
    int depth = 0;
    for (int i = p->pos; p->tk[i].t != T_END; i++) {
        const Tok *t = &p->tk[i];
        if (t->t != T_OP) continue;
        if (t->s[0] == '(' || t->s[0] == '[') depth++;
        else if (t->s[0] == ')' || t->s[0] == ']') { depth--; if (depth == 0) return 0; }
        else if (t->s[0] == ',' && depth == 1) return 1;
    }
    return 0;
}

static void compare_tail(P *p, int dflt_op)
{
    /* optional ", op value" used by AnimElem; emits the compare against a value */
    static const struct { const char *s; int op; } C[] = { {"=", OP_EQ}, {"!=", OP_NE}, {"<=", OP_LE}, {">=", OP_GE}, {"<", OP_LT}, {">", OP_GT} };
    int op = dflt_op;
    if (accept(p, ",")) {
        int ok = 0;
        op = OP_EQ;
        for (unsigned i = 0; i < sizeof C / sizeof *C; i++) if (is_op(p, C[i].s)) { op = C[i].op; adv(p); ok = 1; break; }
        (void)ok;               /* no operator written means "=" */
        p_bin(p, 8);
    } else push_num(p, 0, 0);
    emit(p, op, 0, 0, -1, 0);
}

static void p_primary(P *p)
{
    if (p->err) return;
    if (++p->depth > EXPR_MAX_DEPTH) { p->err = 1; p->depth--; return; }
    const Tok *t = cur(p);
    if (t->t == T_NUM) { push_num(p, t->v, t->isf); adv(p); }
    else if (t->t == T_OP && !strcmp(t->s, "(")) { adv(p); p_bin(p, 0); expect(p, ")"); }
    else if (t->t == T_ID) {
        char name[EXPR_STR_LEN]; snprintf(name, sizeof name, "%s", t->s);
        adv(p);
        int rk = redirect_kind(name);
        if (rk) {
            if (accept(p, "(")) { p_bin(p, 0); expect(p, ")"); } else push_num(p, -1, 0);
            expect(p, ",");
            emit(p, OP_REDIR, 0, rk, -1, 0);
            p_unary(p);
            emit(p, OP_ENDREDIR, 0, 0, -1, 0);
        } else if (!strcmp(name, "animelem")) {
            if (!is_op(p, "=")) { p->err = 1; }
            else {
                adv(p); p_bin(p, 8);
                emit(p, OP_TRIG, 1, TG_ANIMELEMTIME, -1, 0);
                compare_tail(p, OP_EQ);
            }
        } else if (!strcmp(name, "timemod")) {
            int neg = 0;
            if (accept(p, "!=")) neg = 1; else expect(p, "=");
            p_bin(p, 8); expect(p, ","); p_bin(p, 8);
            emit(p, OP_TRIG, 2, TG_TIMEMOD, -1, 0);
            if (neg) emit(p, OP_NOT, 0, 0, -1, 0);
        } else if (!strcmp(name, "command")) {
            int neg = 0;
            if (accept(p, "!=")) neg = 1; else expect(p, "=");
            if (cur(p)->t != T_STR) p->err = 1;
            else { int si = add_str(p, cur(p)->s); adv(p); emit(p, OP_TRIG, 0, TG_COMMAND, si, 0); if (neg) emit(p, OP_NOT, 0, 0, -1, 0); }
        } else if (!strcmp(name, "hitdefattr")) {
            int neg = 0;
            if (accept(p, "!=")) neg = 1; else expect(p, "=");
            int st = 0, at = 0, first = 1;
            for (;;) {
                const Tok *a = cur(p);
                if (a->t != T_ID) { p->err = 1; break; }
                if (first) {
                    for (const char *c = a->s; *c; c++) { if (*c == 's') st |= 1; else if (*c == 'c') st |= 2; else if (*c == 'a') st |= 4; else p->err = 1; }
                } else {
                    static const char *AT[] = { "na", "nt", "np", "sa", "st", "sp", "ha", "ht", "hp" };
                    int ok = 0;
                    for (int i = 0; i < 9; i++) if (!strcmp(a->s, AT[i])) { at |= 1 << i; ok = 1; }
                    if (!ok) p->err = 1;
                }
                adv(p); first = 0;
                if (!accept(p, ",")) break;
            }
            emit(p, OP_TRIG, 0, TG_HITDEFATTR, -1, 0);
            emit(p, OP_HITATTR, 0, st, at, 0);
            if (neg) emit(p, OP_NOT, 0, 0, -1, 0);
        } else if (!strcmp(name, "gethitvar")) {
            expect(p, "(");
            if (cur(p)->t != T_ID) p->err = 1;
            else { int si = add_str(p, cur(p)->s); adv(p); emit(p, OP_TRIG, 0, TG_GETHITVARFALL, si, 0); }
            expect(p, ")");
        } else if (!strcmp(name, "const")) {
            expect(p, "(");
            if (cur(p)->t != T_ID) p->err = 1;
            else { int si = add_str(p, cur(p)->s); adv(p); emit(p, OP_TRIG, 0, TG_CONST, si, 0); }
            expect(p, ")");
        } else {
            /* axis forms: "pos x" */
            if (cur(p)->t == T_ID && (!strcmp(cur(p)->s, "x") || !strcmp(cur(p)->s, "y"))) {
                char both[EXPR_STR_LEN * 2];
                snprintf(both, sizeof both, "%s %s", name, cur(p)->s);
                if (find_trig(both) >= 0) { snprintf(name, sizeof name, "%.*s", EXPR_STR_LEN - 1, both); adv(p); }
            }
            int id = find_trig(name);
            if (id < 0) { p->err = 1; }
            else {
                int ar = TRIGS[id].arity, n = 0;
                p->lastkind = TRIGS[id].kind;
                if (ar > 0 || ar == -1) {
                    if (accept(p, "(")) {
                        for (;;) {
                            p_bin(p, 0); n++;
                            if (n > 4) { p->err = 1; break; }
                            if (!accept(p, ",")) break;
                        }
                        expect(p, ")");
                    } else if (ar > 0) p->err = 1;
                }
                if (ar > 0 && n != ar) p->err = 1;
                if (id == TG_VAR || id == TG_FVAR || id == TG_SYSVAR || id == TG_SYSFVAR) {
                    if (accept(p, ":=")) {
                        p_bin(p, 0);
                        emit(p, OP_ASSIGN, 0, id == TG_VAR ? AS_VAR : id == TG_FVAR ? AS_FVAR : id == TG_SYSVAR ? AS_SYSVAR : AS_SYSFVAR, -1, 0);
                        p->depth--; return;
                    }
                }
                emit(p, OP_TRIG, n, id, -1, 0);
            }
        }
    } else p->err = 1;
    p->depth--;
}

static void p_unary(P *p)
{
    if (p->err) return;
    if (++p->depth > EXPR_MAX_DEPTH) { p->err = 1; p->depth--; return; }
    if (accept(p, "-")) { p_unary(p); emit(p, OP_NEG, 0, 0, -1, 0); }
    else if (accept(p, "!")) { p_unary(p); emit(p, OP_NOT, 0, 0, -1, 0); }
    else if (accept(p, "~")) { p_unary(p); emit(p, OP_BNOT, 0, 0, -1, 0); }
    else if (accept(p, "+")) p_unary(p);
    else p_primary(p);
    p->depth--;
}

static void p_pow(P *p)
{
    p_unary(p);
    if (accept(p, "**")) {
        if (++p->depth > EXPR_MAX_DEPTH) { p->err = 1; p->depth--; return; }
        p_pow(p);
        p->depth--;
        emit(p, OP_POW, 0, 0, -1, 0);
    }
}

static void p_interval(P *p, int neg)
{
    int lo = is_op(p, "[");
    adv(p);
    p_bin(p, 8); expect(p, ","); p_bin(p, 8);
    int hi = is_op(p, "]");
    if (!hi && !is_op(p, ")")) { p->err = 1; return; }
    adv(p);
    emit(p, OP_INTERVAL, 0, (lo ? 1 : 0) | (hi ? 2 : 0) | (neg ? 4 : 0), -1, 0);
}

static void p_bin(P *p, int level)
{
    if (p->err) return;
    if (++p->depth > EXPR_MAX_DEPTH) { p->err = 1; p->depth--; return; }
    static const struct { const char *s; int op; } L[][4] = {
        /*0*/ {{"||", OP_OR_SC}}, /*1*/ {{"^^", OP_LXOR}}, /*2*/ {{"&&", OP_AND_SC}}, /*3*/ {{"|", OP_BOR}}, /*4*/ {{"^", OP_BXOR}},
        /*5*/ {{"&", OP_BAND}}, /*6*/ {{0}}, /*7*/ {{"<=", OP_LE}, {">=", OP_GE}, {"<", OP_LT}, {">", OP_GT}},
        /*8*/ {{"+", OP_ADD}, {"-", OP_SUB}}, /*9*/ {{"*", OP_MUL}, {"/", OP_DIV}, {"%", OP_MOD}} };
    if (level >= 10) p_pow(p);
    else if (level == 6) {
        p->lastkind = 0;
        p_bin(p, 7);
        int kind = p->lastkind;
        int neg = is_op(p, "!=");
        if (!p->err && (is_op(p, "=") || neg)) {
            adv(p);
            if (is_interval(p)) p_interval(p, neg);
            else if (cur(p)->t == T_ID && kind && expr_symbol(kind, cur(p)->s) && find_trig(cur(p)->s) < 0 && !redirect_kind(cur(p)->s)) {
                push_num(p, expr_symbol(kind, cur(p)->s), 0); adv(p);
                emit(p, neg ? OP_NE : OP_EQ, 0, 0, -1, 0);
            } else if (cur(p)->t == T_ID && kind && expr_symbol(kind, cur(p)->s) && !strcmp(cur(p)->s, "a")) {
                push_num(p, expr_symbol(kind, "a"), 0); adv(p);
                emit(p, neg ? OP_NE : OP_EQ, 0, 0, -1, 0);
            } else { p_bin(p, 7); emit(p, neg ? OP_NE : OP_EQ, 0, 0, -1, 0); }
        }
    } else {
        p_bin(p, level + 1);
        for (;;) {
            int op = -1;
            for (int i = 0; i < 4 && L[level][i].s; i++) if (is_op(p, L[level][i].s)) { op = L[level][i].op; break; }
            if (op < 0 || p->err) break;
            adv(p);
            if (op == OP_AND_SC || op == OP_OR_SC) {
                int j = emit(p, op, 0, 0, -1, 0);
                p_bin(p, level + 1);
                emit(p, OP_TOBOOL, 0, 0, -1, 0);
                p->ins[j].a = (int16_t)p->ni;
            } else { p_bin(p, level + 1); emit(p, op, 0, 0, -1, 0); }
        }
    }
    p->depth--;
}

Expr *expr_compile(const char *src)
{
    if (!src || strlen(src) >= EXPR_MAX_SRC) return NULL;
    P *p = calloc(1, sizeof *p);
    if (!p) return NULL;
    Expr *e = NULL;
    if (tokenize(src, p->tk, &p->nt) == 0 && p->nt > 0) {
        p_bin(p, 0);
        if (!p->err && cur(p)->t == T_END) {
            e = malloc(sizeof(Expr) + (size_t)p->ni * sizeof(Insn));
            if (e) {
                memset(e->strs, 0, sizeof e->strs);
                e->n = (uint16_t)p->ni; e->nstr = (uint8_t)p->ns;
                memcpy(e->strs, p->strs, sizeof e->strs);
                memcpy(e->ins, p->ins, (size_t)p->ni * sizeof(Insn));
            }
        }
    }
    free(p);
    return e;
}

void expr_free(Expr *e) { free(e); }
size_t expr_size(const Expr *e) { return e ? sizeof(Expr) + (size_t)e->n * sizeof(Insn) : 0; }

/* ---------------------------------------------------------------------------- evaluator */
static double clampi(double v) { return v > 2147483647.0 ? 2147483647.0 : v < -2147483648.0 ? -2147483648.0 : v; }
static int32_t toi(Val v) { double d = clampi(v.d); return (int32_t)d; }

static Val arith(int op, Val a, Val b)
{
    if (a.bad || b.bad) return val_bad();
    int isf = a.isf || b.isf;
    switch (op) {
    case OP_ADD: return isf ? val_flt(a.d + b.d) : val_int((int32_t)clampi(a.d + b.d));
    case OP_SUB: return isf ? val_flt(a.d - b.d) : val_int((int32_t)clampi(a.d - b.d));
    case OP_MUL: return isf ? val_flt(a.d * b.d) : val_int((int32_t)clampi(a.d * b.d));
    case OP_DIV:
        if (b.d == 0) return val_bad();
        if (isf) return val_flt(a.d / b.d);
        return val_int((int32_t)((int64_t)toi(a) / (int64_t)toi(b)));
    case OP_MOD:
        if (isf || toi(b) == 0) return val_bad();
        return val_int((int32_t)((int64_t)toi(a) % (int64_t)toi(b)));
    case OP_POW: {
        double r = pow(a.d, b.d);
        if (r != r) return val_bad();
        if (!isf && b.d >= 0) return val_int((int32_t)clampi(r));
        return val_flt(r);
    }
    case OP_LT: return val_int(a.d < b.d);
    case OP_GT: return val_int(a.d > b.d);
    case OP_LE: return val_int(a.d <= b.d);
    case OP_GE: return val_int(a.d >= b.d);
    case OP_EQ: return val_int(a.d == b.d);
    case OP_NE: return val_int(a.d != b.d);
    case OP_BAND: return val_int((int32_t)((uint32_t)toi(a) & (uint32_t)toi(b)));
    case OP_BOR:  return val_int((int32_t)((uint32_t)toi(a) | (uint32_t)toi(b)));
    case OP_BXOR: return val_int((int32_t)((uint32_t)toi(a) ^ (uint32_t)toi(b)));
    case OP_LXOR: return val_int((a.d != 0) != (b.d != 0));
    }
    return val_bad();
}

static Val builtin(const ExprCtx *c, void *ud, int id, const Val *a, int n, const char *str)
{
    for (int i = 0; i < n; i++) if (a[i].bad) return val_bad();
    switch (id) {
    case TG_ABS:   return a[0].isf ? val_flt(fabs(a[0].d)) : val_int((int32_t)clampi(fabs(a[0].d)));
    case TG_FLOOR: return val_int((int32_t)clampi(floor(a[0].d)));
    case TG_CEIL:  return val_int((int32_t)clampi(ceil(a[0].d)));
    case TG_SIN:   return val_flt(sin(a[0].d));
    case TG_COS:   return val_flt(cos(a[0].d));
    case TG_TAN:   return val_flt(tan(a[0].d));
    case TG_ASIN:  return (a[0].d < -1 || a[0].d > 1) ? val_bad() : val_flt(asin(a[0].d));
    case TG_ACOS:  return (a[0].d < -1 || a[0].d > 1) ? val_bad() : val_flt(acos(a[0].d));
    case TG_ATAN:  return val_flt(atan(a[0].d));
    case TG_LN:    return a[0].d <= 0 ? val_bad() : val_flt(log(a[0].d));
    case TG_LOG:   return (a[0].d <= 0 || a[1].d <= 0 || a[0].d == 1) ? val_bad() : val_flt(log(a[1].d) / log(a[0].d));
    case TG_EXP:   return val_flt(exp(a[0].d));
    case TG_SQRT:  return a[0].d < 0 ? val_bad() : val_flt(sqrt(a[0].d));
    case TG_TIMEMOD: {
        int32_t m = toi(a[0]);
        if (m <= 0) return val_bad();
        Val t = c->get(ud, TG_TIME, NULL, 0, "");
        if (t.bad) return t;
        return val_int(((int64_t)toi(t) % m) == toi(a[1]));
    }
    }
    return c->get(ud, id, a, n, str);
}

Val expr_eval(const Expr *e, const ExprCtx *c)
{
    if (!e) return val_bad();
    Val st[EXPR_STACK];
    void *uds[8]; int nuds = 0;
    void *ud = c->ud;
    int sp = 0;
#define PUSH(v) do { if (sp >= EXPR_STACK) return val_bad(); st[sp++] = (v); } while (0)
#define NEED(k) do { if (sp < (k)) return val_bad(); } while (0)
    for (int pc = 0; pc < e->n; pc++) {
        const Insn *i = &e->ins[pc];
        switch (i->op) {
        case OP_PUSHI: PUSH(val_int((int32_t)i->d)); break;
        case OP_PUSHF: PUSH(val_flt(i->d)); break;
        case OP_TRIG: {
            NEED(i->nargs);
            Val args[4];
            for (int k = 0; k < i->nargs; k++) args[k] = st[sp - i->nargs + k];
            sp -= i->nargs;
            const char *s = (i->s >= 0 && i->s < e->nstr) ? e->strs[i->s] : "";
            PUSH(ud ? builtin(c, ud, i->a, args, i->nargs, s) : val_bad());
            break;
        }
        case OP_NEG: NEED(1); if (!st[sp-1].bad) st[sp-1].d = -st[sp-1].d; break;
        case OP_NOT: NEED(1); st[sp-1] = st[sp-1].bad ? val_bad() : val_int(st[sp-1].d == 0); break;
        case OP_BNOT: NEED(1); st[sp-1] = st[sp-1].bad ? val_bad() : val_int((int32_t)~(uint32_t)toi(st[sp-1])); break;
        case OP_TOBOOL: NEED(1); st[sp-1] = val_int(val_true(st[sp-1])); break;
        case OP_AND_SC: NEED(1); if (!val_true(st[sp-1])) { st[sp-1] = val_int(0); pc = i->a - 1; } else sp--; break;
        case OP_OR_SC:  NEED(1); if (val_true(st[sp-1])) { st[sp-1] = val_int(1); pc = i->a - 1; } else sp--; break;
        case OP_INTERVAL: {
            NEED(3);
            Val hi = st[sp-1], lo = st[sp-2], x = st[sp-3]; sp -= 3;
            if (x.bad || lo.bad || hi.bad) { PUSH(val_bad()); break; }
            int in = ((i->a & 1) ? x.d >= lo.d : x.d > lo.d) && ((i->a & 2) ? x.d <= hi.d : x.d < hi.d);
            PUSH(val_int((i->a & 4) ? !in : in));
            break;
        }
        case OP_ASSIGN: {
            NEED(2);
            Val v = st[sp-1], idx = st[sp-2]; sp -= 2;
            if (v.bad || idx.bad || !ud || !c->set || !c->set(ud, i->a, toi(idx), v)) PUSH(val_bad()); else PUSH(v);
            break;
        }
        case OP_REDIR: {
            NEED(1);
            Val a = st[--sp];
            if (nuds >= 8) return val_bad();
            uds[nuds++] = ud;
            ud = (ud && c->redir && !a.bad) ? c->redir(ud, i->a, toi(a)) : NULL;
            break;
        }
        case OP_ENDREDIR: if (nuds > 0) ud = uds[--nuds]; break;
        case OP_HITATTR: {
            NEED(1);
            Val v = st[sp-1];
            if (v.bad) break;
            int32_t m = toi(v);
            st[sp-1] = val_int(((m & 0xff) & i->a) && (((m >> 8) & 0xffff) & i->s));
            break;
        }
        default: {
            NEED(2);
            Val b = st[sp-1], a = st[sp-2]; sp -= 2;
            PUSH(arith(i->op, a, b));
        }
        }
    }
    return sp == 1 ? st[0] : val_bad();
#undef PUSH
#undef NEED
}

/* ---------------------------------------------------------------------------- list splitting */
/* "Parent," or "Helper(3)," so far: the comma belongs to the redirect ("value = Parent, Var(3)"), not to the list */
static int only_redirect(const char *s)
{
    while (*s == ' ' || *s == '\t') s++;
    char w[16]; int n = 0;
    while (((*s >= 'a' && *s <= 'z') || (*s >= 'A' && *s <= 'Z')) && n < 15) { w[n++] = (char)((*s >= 'A' && *s <= 'Z') ? *s + 32 : *s); s++; }
    w[n] = 0;
    if (!n || !redirect_kind(w)) return 0;
    while (*s == ' ' || *s == '\t') s++;
    if (*s == '(') { int d = 0; for (; *s; s++) { if (*s == '(') d++; else if (*s == ')' && --d == 0) { s++; break; } } if (d) return 0; }
    while (*s == ' ' || *s == '\t') s++;
    return *s == 0;
}

int expr_split(const char *src, char out[][EXPR_MAX_SRC], int max)
{
    int n = 0, depth = 0, quote = 0;
    size_t k = 0;
    if (max <= 0) return 0;
    out[0][0] = 0;
    for (const char *p = src; ; p++) {
        char ch = *p;
        int split = (ch == ',' && !depth && !quote) || ch == 0;
        if (split && ch == ',') { out[n][k] = 0; if (only_redirect(out[n])) split = 0; }
        if (split) {
            out[n][k] = 0;
            char *s = out[n];
            while (*s == ' ' || *s == '\t') s++;
            if (s != out[n]) memmove(out[n], s, strlen(s) + 1);
            size_t l = strlen(out[n]);
            while (l && (out[n][l-1] == ' ' || out[n][l-1] == '\t')) out[n][--l] = 0;
            n++;
            if (ch == 0 || n >= max) break;
            k = 0; out[n][0] = 0;
            continue;
        }
        if (ch == '"') quote = !quote;
        else if (!quote) { if (ch == '(' || ch == '[') depth++; else if (ch == ')' || ch == ']') { if (depth) depth--; } }
        if (k + 1 < EXPR_MAX_SRC) out[n][k++] = ch;
    }
    return n;
}

const char *expr_trigger_name(int id) { return id >= 0 && id < TG_COUNT ? TRIGS[id].name : "?"; }
