#include <stdio.h>
#include <string.h>
#include <math.h>
#include "expr.h"
static int fails; static int vars[10]; static double fvars[10];
#define CHECK(c) do{ if(!(c)){ printf("FAIL line %d: %s\n",__LINE__,#c); fails++; } }while(0)
static Val get(void *ud, int id, const Val *a, int n, const char *s)
{
    (void)ud; (void)n;
    switch (id) {
    case TG_TIME: return val_int(7);
    case TG_ANIM: return val_int(200);
    case TG_ANIMELEMTIME: return val_int(a[0].d == 3 ? 0 : a[0].d == 4 ? -2 : 5);
    case TG_STATETYPE: return val_int(3);          /* A */
    case TG_P2STATETYPE: return val_int(1);
    case TG_MOVETYPE: return val_int(2);           /* A */
    case TG_POSX: return val_flt(-12.5);
    case TG_VAR: return val_int(vars[(int)a[0].d]);
    case TG_FVAR: return val_flt(fvars[(int)a[0].d]);
    case TG_COMMAND: return val_int(!strcmp(s, "QCF_x"));
    case TG_CONST: return val_flt(!strcmp(s, "velocity.walk.fwd.x") ? 2.4 : 0);
    case TG_HITDEFATTR: return val_int(1 | (1 << 8));  /* S, NA */
    case TG_LIFE: return val_int(1000);
    }
    return val_int(0);
}
static int set(void *ud, int k, int idx, Val v) { (void)ud; if (idx < 0 || idx > 9) return 0; if (k == AS_VAR) vars[idx] = (int)v.d; else fvars[idx] = v.d; return 1; }
static void *redir(void *ud, int kind, int arg) { (void)arg; return kind == RD_P2 ? ud : NULL; }
static int dummy; static ExprCtx C = { &dummy, get, set, redir };
static double ev(const char *s) { Expr *e = expr_compile(s); if (!e) return -999999; Val v = expr_eval(e, &C); expr_free(e); return v.bad ? -888888 : v.d; }
static int ok(const char *s) { Expr *e = expr_compile(s); if (!e) return 0; expr_free(e); return 1; }
int main(void)
{
    CHECK(ev("1+2*3") == 7);
    CHECK(ev("(1+2)*3") == 9);
    CHECK(ev("7/2") == 3);
    CHECK(ev("7.0/2") == 3.5);
    CHECK(ev("-7 % 3") == -1);
    CHECK(ev("2**3**2") == 512);
    CHECK(ev("1/0") == -888888);
    CHECK(ev("!0") == 1 && ev("!5") == 0 && ev("~0") == -1);
    CHECK(ev("5 & 3") == 1 && ev("5 | 3") == 7 && ev("5 ^ 3") == 6);
    CHECK(ev("1 && 0 || 1") == 1);
    CHECK(ev("Time = 7") == 1 && ev("Time != 7") == 0 && ev("time >= 8") == 0);
    CHECK(ev("Time = [5,9]") == 1 && ev("Time = (7,9]") == 0 && ev("Time != [0,3]") == 1);
    CHECK(ev("anim = [200, 299]") == 1);
    CHECK(ev("StateType = A") == 1 && ev("StateType = S") == 0 && ev("StateType != C") == 1);
    CHECK(ev("MoveType = A") == 1 && ev("MoveType = H") == 0);
    CHECK(ev("P2StateType = S") == 1);
    CHECK(ev("Pos X < 0") == 1 && ev("abs(Pos X)") == 12.5);
    CHECK(ev("AnimElem = 3") == 1 && ev("AnimElem = 4, >= -2") == 1 && ev("AnimElem = 4, < -2") == 0 && ev("AnimElem = 5") == 0);
    CHECK(ev("AnimElemTime(3) = 0") == 1);
    CHECK(ev("TimeMod = 7, 0") == 1 && ev("TimeMod = 4, 3") == 1 && ev("TimeMod = 4, 2") == 0);
    CHECK(ev("Command = \"QCF_x\"") == 1 && ev("Command = \"x\"") == 0 && ev("Command != \"x\"") == 1);
    CHECK(ev("Const(velocity.walk.fwd.x)") == 2.4);
    CHECK(ev("HitDefAttr = SCA, NA, SA") == 1 && ev("HitDefAttr = SCA, HA") == 0 && ev("HitDefAttr = C, NA") == 0);
    CHECK(ev("var(3) := 5") == 5 && vars[3] == 5 && ev("var(3) = 5") == 1);
    CHECK(ev("fvar(2) := 1.5 + 1") == 2.5 && fvars[2] == 2.5);
    CHECK(ev("var(99) := 1") == -888888);
    CHECK(ev("P2, Life") == 1000 && ev("Target, Life") == -888888 && ev("Target, Life > 0 || 1") == 1);
    CHECK(ev("0 && (1/0)") == 0 && ev("1 || (1/0)") == 1);
    CHECK(ev("floor(2.7)") == 2 && ev("ceil(2.1)") == 3 && fabs(ev("sin(0)")) < 1e-9);
    CHECK(!ok("Time ="));
    CHECK(!ok("nosuchtrigger = 1"));
    CHECK(!ok("1 +"));
    CHECK(!ok("((((((((((((((((((((((((((((((((((((((((((((((((((((((((1))))))))))))))))))))))))))))))))))))))))))))))))))))))))"));
    CHECK(!ok("\"abc"));
    CHECK(!ok("Time @ 4"));
    char sp[3][EXPR_MAX_SRC];
    CHECK(expr_split("100, abs(1,2), [3,4]", sp, 3) == 3 && !strcmp(sp[1], "abs(1,2)") && !strcmp(sp[2], "[3,4]"));
    printf(fails ? "expr_test: %d FAILED\n" : "expr_test: ok\n", fails);
    return fails != 0;
}
