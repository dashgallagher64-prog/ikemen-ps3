/* Helper / Explod / Projectile: spawn, redirects, parent variables, one hit each, cleanup. */
#include <stdio.h>
#include "fight.h"
static int fails;
#define CHECK(c) do{ if(!(c)){ printf("FAIL line %d: %s\n",__LINE__,#c); fails++; } }while(0)
static int nlive(Fight *f) { int n = 0; for (int i = 0; i < FIGHT_MAXHELPER; i++) n += f->h[i].live; return n; }
static int nex(Fight *f) { int n = 0; for (int i = 0; i < FIGHT_MAXEXPLOD; i++) n += f->ex[i].on; return n; }
static int npr(Fight *f) { int n = 0; for (int i = 0; i < FIGHT_MAXPROJ; i++) n += f->pr[i].on; return n; }
int main(int argc, char **argv)
{
    CharData cd;
    if (argc < 3 || chardata_load(&cd, argv[1], argv[2]) != 0) { puts("cannot load"); return 2; }
    Fight f;
    CHECK(fight_init(&f, &cd, &cd) == 0);
    int base = f.p[1].lifemax, maxh = 0, maxe = 0, maxp = 0, sawpid = 0, v8 = -1;
    for (int t = 1; t <= 120; t++) {
        fight_tick(&f, 0, 0);
        f.p[1].x = f.p[0].x + (t < 50 ? 45.0f : 120.0f) * f.cs;                 /* a dummy that cannot be knocked away */
        if (nlive(&f) > maxh) maxh = nlive(&f);
        if (nex(&f) > maxe) maxe = nex(&f);
        if (npr(&f) > maxp) maxp = npr(&f);
        if (t == 20) {
            CHECK(nlive(&f) == 1);
            for (int i = 0; i < FIGHT_MAXHELPER; i++) if (f.h[i].live) { sawpid = f.h[i].pid >= 2000 && f.h[i].hid == 77 && f.h[i].parent == &f.p[0]; CHECK(f.h[i].x > f.p[0].x + 20 && f.h[i].x < f.p[0].x + 50); }
        }
        if (t == 25) v8 = f.p[0].var[8];
        if (t == 40) { CHECK(f.p[0].var[5] == 99); CHECK(f.p[0].var[6] == 1); CHECK(f.p[0].var[7] > 0); }
        if (t == 35) CHECK(nex(&f) == 1);
        if (t == 62) CHECK(npr(&f) == 1);
        if (t == 100) CHECK(nlive(&f) == 0 && nex(&f) == 0 && npr(&f) == 0);
    }
    printf("helpers<=%d explods<=%d projectiles<=%d  parent var5=%d  var8=%d  dummy life %d/%d\n", maxh, maxe, maxp, f.p[0].var[5], v8, f.p[1].life, base);
    CHECK(sawpid); CHECK(maxh == 1); CHECK(maxe == 1); CHECK(maxp == 1);
    CHECK(v8 == 10);                                          /* tick 25: 0 explods + 0 projectiles + 10 * 1 helper */
    CHECK(f.p[1].life == base - 17 - 33);                     /* helper hit once, projectile hit once */
    CHECK(f.p[0].pj_contact > 0 || f.p[0].pj_hit > 0);
    CHECK(f.unimpl[SC_HELPER] == 0 && f.unimpl[SC_EXPLOD] == 0 && f.unimpl[SC_PROJECTILE] == 0 && f.unimpl[SC_DESTROYSELF] == 0);
    CHECK(f.overflow == 0);
    fight_free(&f); chardata_free(&cd);
    printf(fails ? "helper_test: %d FAILED\n" : "helper_test: ok\n", fails);
    return fails != 0;
}
