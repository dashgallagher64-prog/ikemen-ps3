#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cns.h"
#include "text.h"
static int dummy;
static Val get(void *u, int id, const Val *a, int n, const char *s) { (void)u;(void)id;(void)a;(void)n;(void)s; return val_int(rand() % 3); }
static int set(void *u, int k, int i, Val v) { (void)u;(void)k;(void)i;(void)v; return 1; }
static void *redir(void *u, int k, int a) { (void)k; (void)a; return rand() & 1 ? u : NULL; }
int main(int argc, char **argv)
{
    size_t len; char *t = text_load(argv[1], CNS_MAX_FILE, &len);
    if (!t) return 2;
    srand(7);
    ExprCtx c = { &dummy, get, set, redir };
    for (int it = 0; it < 400; it++) {
        char *m = malloc(len + 1); memcpy(m, t, len);
        int n = 1 + rand() % 60;
        for (int k = 0; k < n; k++) {
            size_t p = (size_t)rand() % len;
            if (rand() & 1) m[p] = (char)rand(); else m[p] = "()[],:=!&|^~*/%+-<>0123456789\"\n"[rand() % 32];
        }
        StateDb db; cns_init(&db);
        cns_parse(m, (rand() % 4) ? len : (size_t)rand() % len, &db, 0);
        cns_finish(&db);
        for (int i = 0; i < db.ns; i++) for (int j = 0; j < db.s[i].nc; j++) {
            const Ctrl *ct = &db.s[i].c[j];
            for (int k = 0; k < ct->nall; k++) expr_eval(ct->all[k], &c);
            for (int g = 0; g < ct->ngrp; g++) for (int k = 0; k < ct->grp[g].n; k++) expr_eval(ct->grp[g].e[k], &c);
        }
        cns_free(&db); free(m);
    }
    /* deliberately nasty inputs */
    const char *nasty[] = {
        "[Statedef 1]\n[State 1]\ntype=ChangeState\ntrigger1=((((((((((((((((((((((((((((((((((((((((((((((((((((1))))))))))))))))))))))))))))))))))))))))))))))))))))\n",
        "[Statedef 99999999999999999999]\n[State 1]\ntrigger999999999=1\ntrigger1=1/0\n",
        "[Data]\nlife=\n[Size]\nx=1,2,3,4,5\n[State 0]\ntrigger1 = var(1) := var(1) := 3\n",
    };
    for (unsigned i = 0; i < sizeof nasty / sizeof *nasty; i++) { StateDb db; cns_init(&db); cns_parse(nasty[i], strlen(nasty[i]), &db, 0); cns_finish(&db); cns_free(&db); }
    free(t); puts("fuzz_cns ok"); return 0;
}
