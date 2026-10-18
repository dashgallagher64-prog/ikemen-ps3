/* A normal attack must hit once, even if the character re-fires its HitDef every tick. */
#include <stdio.h>
#include "fight.h"
static int fails;
#define CHECK(c) do{ if(!(c)){ printf("FAIL line %d: %s\n",__LINE__,#c); fails++; } }while(0)
int main(int argc, char **argv)
{
    CharData cd;
    if (argc < 3 || chardata_load(&cd, argv[1], argv[2]) != 0) { puts("cannot load"); return 2; }
    Fight f;
    CHECK(fight_init(&f, &cd, &cd) == 0);
    for (int t = 0; t < 10; t++) fight_tick(&f, 0, 0);
    int hits = 0, last = f.p[1].life;
    for (int t = 0; t < 120; t++) {
        fight_tick(&f, t == 0 ? FI_X : 0, 0);
        f.p[1].x = f.p[0].x + 40.0f * f.cs;              /* a dummy that cannot be knocked away */
        if (f.p[1].life < last) { hits++; last = f.p[1].life; }
    }
    printf("%s: one punch -> %d hit(s), life %d\n", argv[1], hits, f.p[1].life);
    CHECK(hits == 1);
    fight_free(&f); chardata_free(&cd);
    printf(fails ? "hit_test: %d FAILED\n" : "hit_test: ok\n", fails);
    return fails != 0;
}
