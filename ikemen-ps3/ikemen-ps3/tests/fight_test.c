/* Host test: loads real characters and plays scripted inputs through the fight engine. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "fight.h"
static int fails; static int unimpl_total[SC_COUNT];
#define CHECK(c) do{ if(!(c)){ printf("FAIL line %d: %s\n",__LINE__,#c); fails++; } }while(0)

static void run(Fight *f, uint16_t k1, int n) { for (int i = 0; i < n; i++) fight_tick(f, k1, 0); }

int main(int argc, char **argv)
{
    if (argc < 3) { puts("usage: fight_test kfm.def commondir"); return 2; }
    CharData cd;
    int rc = chardata_load(&cd, argv[1], argv[2]);
    printf("load rc=%d warnings=%d states=%d consts=%d ctrls=%u cmds=%d anims=%d\n", rc, cd.warnings, cd.cns.ns, cd.cns.nk, cd.nctrl, cd.cmd.n, cd.air.nact);
    CHECK(rc == 0 && cd.warnings == 0);
    int bad = 0;
    for (int i = 0; i < cd.cns.ns; i++) for (int j = 0; j < cd.cns.s[i].nc; j++) {
        const Ctrl *c = &cd.cns.s[i].c[j];
        if (c->type == SC_UNKNOWN) { printf("unknown controller type in state %d\n", cd.cns.s[i].no); bad++; }
        for (int k = 0; k < c->nall; k++) if (!c->all[k]) { printf("bad triggerall in state %d\n", cd.cns.s[i].no); bad++; }
        for (int g = 0; g < c->ngrp; g++) for (int k = 0; k < c->grp[g].n; k++) if (!c->grp[g].e[k]) { printf("bad trigger in state %d group %d\n", cd.cns.s[i].no, g + 1); bad++; }
    }
    CHECK(bad == 0);

    Fight f;
    CHECK(fight_init(&f, &cd, &cd) == 0);
    Player *p = &f.p[0], *q = &f.p[1];
    run(&f, 0, 5);
    CHECK(p->stateno == 0 && p->ctrl && p->animno == 0);

    /* walk forward */
    float x0 = p->x;
    run(&f, FI_RIGHT, 20);
    printf("walk: state %d anim %d x %.1f -> %.1f\n", p->stateno, p->animno, x0, p->x);
    CHECK(p->stateno == 20 && p->x > x0 + 20);
    run(&f, 0, 20);
    CHECK(p->stateno == 0);

    /* jump and land */
    run(&f, FI_UP, 2);
    int saw_air = 0;
    for (int i = 0; i < 80; i++) { fight_tick(&f, 0, 0); if (p->statetype == ST_A) saw_air = 1; }
    printf("jump: saw_air=%d state %d y %.1f\n", saw_air, p->stateno, p->y);
    CHECK(saw_air && p->stateno == 0 && p->y == 0 && p->ctrl);

    /* crouch */
    run(&f, FI_DOWN, 20);
    CHECK(p->stateno == 11 && p->statetype == ST_C);
    run(&f, 0, 20);
    CHECK(p->stateno == 0);

    /* walk up to the opponent and punch (x = stand light punch in KFM) */
    for (int i = 0; i < 200 && p->x < q->x - 60; i++) fight_tick(&f, FI_RIGHT, 0);
    int life0 = q->life, hit = 0, st0 = 0;
    run(&f, 0, 5);
    fight_tick(&f, FI_X, 0);
    for (int i = 0; i < 40; i++) { fight_tick(&f, 0, 0); if (q->stateno >= 5000 && !hit) { hit = 1; st0 = q->stateno; } }
    printf("punch: p1 state %d, p2 life %d -> %d, p2 state first hit %d, now %d, dist %.1f\n", p->stateno, life0, q->life, st0, q->stateno, q->x - p->x);
    CHECK(p->prev_stateno == 200 || p->stateno == 0);
    CHECK(q->life < life0);
    CHECK(hit && st0 == 5000);
    run(&f, 0, 60);
    CHECK(q->stateno == 0 && p->stateno == 0);

    /* every KFM attack state must at least be enterable and finish without hanging or crashing */
    int tried = 0;
    for (int i = 0; i < cd.cns.ns; i++) {
        int no = cd.cns.s[i].no;
        if (no < 200 || no >= 4000) continue;
        Fight g;
        CHECK(fight_init(&g, &cd, &cd) == 0);
        /* put the state in through the engine's own path by faking a buffered change */
        g.p[0].buf_state = no;
        for (int t = 0; t < 300; t++) fight_tick(&g, 0, 0);
        tried++;
        for (int u = 0; u < SC_COUNT; u++) unimpl_total[u] += g.unimpl[u];
        fight_free(&g);
    }
    printf("ran %d attack/special states for 300 ticks each\n", tried);

    /* special move by motion: quarter circle forward + x (Kung Fu Palm) */
    {
        Fight g; CHECK(fight_init(&g, &cd, &cd) == 0);
        for (int t = 0; t < 5; t++) fight_tick(&g, 0, 0);
        fight_tick(&g, FI_DOWN, 0); fight_tick(&g, FI_DOWN, 0);
        fight_tick(&g, FI_DOWN | FI_RIGHT, 0); fight_tick(&g, FI_DOWN | FI_RIGHT, 0);
        fight_tick(&g, FI_RIGHT, 0); fight_tick(&g, FI_RIGHT | FI_X, 0);
        int s1 = g.p[0].stateno;
        printf("QCF+x -> state %d\n", s1);
        CHECK(s1 >= 1000 && s1 < 1100);
        for (int t = 0; t < 120; t++) fight_tick(&g, 0, 0);
        CHECK(g.p[0].stateno == 0);
        for (int i = 0; i < SC_COUNT; i++) f.unimpl[i] += g.unimpl[i];
        fight_free(&g);
    }
    int unimpl = 0;
    for (int i = 0; i < SC_COUNT; i++) if (f.unimpl[i]) { printf("unimplemented controller %d hit %d times\n", i, f.unimpl[i]); unimpl++; }
    for (int i = 0; i < SC_COUNT; i++) if (unimpl_total[i]) printf("  (attack states) controller %d unimplemented x%d\n", i, unimpl_total[i]);
    fight_free(&f);
    chardata_free(&cd);
    printf(fails ? "fight_test: %d FAILED\n" : "fight_test: ok\n", fails);
    return fails != 0;
}
