#include <stdio.h>
#include <string.h>
#include "cns.h"
static int fails;
#define CHECK(c) do{ if(!(c)){ printf("FAIL line %d: %s\n",__LINE__,#c); fails++; } }while(0)
int main(int argc, char **argv)
{
    StateDb db; cns_init(&db);
    if (argc < 3 || cns_load(argv[1], &db, 0) || cns_load(argv[2], &db, 0)) { puts("cannot load"); return 2; }
    cns_finish(&db);
    int nc = 0, bad = 0, unk = 0, nexpr = 0, nnull = 0;
    for (int i = 0; i < db.ns; i++) for (int j = 0; j < db.s[i].nc; j++) {
        const Ctrl *c = &db.s[i].c[j]; nc++;
        if (c->type == SC_UNKNOWN) unk++;
        for (int k = 0; k < c->nall; k++) { nexpr++; if (!c->all[k]) nnull++; }
        for (int g = 0; g < c->ngrp; g++) for (int k = 0; k < c->grp[g].n; k++) { nexpr++; if (!c->grp[g].e[k]) { nnull++; if (bad++ < 5) printf("bad trigger in state %d ctrl %d group %d\n", db.s[i].no, j, g+1); } }
    }
    printf("statedefs=%d controllers=%d unknown-type=%d trigger-exprs=%d uncompiled=%d bytes=%zu consts=%d\n", db.ns, nc, unk, nexpr, nnull, db.bytes, db.nk);
    CHECK(db.ns > 50);
    CHECK(cns_state(&db, -1) && cns_state(&db, -3) && cns_state(&db, 200));
    CHECK(cns_const(&db, "data.life", 0) == 1000);
    CHECK(cns_const(&db, "velocity.walk.fwd.x", 0) == 2.4);
    CHECK(cns_const(&db, "movement.yaccel", 0) > 0);
    CHECK(cns_const(&db, "size.ground.back", 0) > 0);
    const StateDef *s = cns_state(&db, 200);
    CHECK(s && s->type == ST_S && s->movetype == MT_A && s->physics == PH_S && s->anim && s->nc > 0);
    const Ctrl *c = s ? &s->c[0] : NULL;
    CHECK(c && c->type != SC_UNKNOWN);
    CHECK(nnull == 0);
    cns_free(&db);
    printf(fails ? "cns_test: %d FAILED\n" : "cns_test: ok\n", fails);
    return fails != 0;
}
