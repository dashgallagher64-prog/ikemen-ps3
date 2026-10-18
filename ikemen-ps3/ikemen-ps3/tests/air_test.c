#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "air.h"
static int fails;
#define CHECK(c) do{ if(!(c)){ printf("FAIL line %d: %s\n",__LINE__,#c); fails++; } }while(0)
int main(int argc, char **argv)
{
    Air a;
    if (argc < 2 || air_load(argv[1], &a)) { puts("cannot load air"); return 2; }
    printf("actions=%d frames=%u boxes=%u\n", a.nact, a.nframes, a.nboxes);
    CHECK(a.nact > 100);
    const AirAction *s = air_find(&a, 0);
    CHECK(s && s->nframes == 11);
    CHECK(s && s->loopstart == 0);
    if (s) {
        Anim an; anim_start(&an, &a, s, 1, 0);
        int total = 0; for (int i = 0; i < s->nframes; i++) total += a.frames[s->first+i].time;
        CHECK(s->totaltime == total);
        int seen[64] = {0};
        for (int t = 0; t < total * 2 + 3; t++) { anim_tick(&an); const AirFrame *f = anim_frame(&an); CHECK(f); if (an.drawidx < 64) seen[an.drawidx]++; }
        for (int i = 0; i < s->nframes; i++) CHECK(seen[i] > 0);
        CHECK(anim_length(&an) == total);
        const AirFrame *f0 = &a.frames[s->first];
        CHECK(f0->nclsn2 == 2);
    }
    const AirAction *h = air_find(&a, 5);
    CHECK(h != NULL);
    /* ChangeAnim to elem 3 */
    if (s) { Anim an; anim_start(&an, &a, s, 3, 0); anim_tick(&an); CHECK(an.drawidx == 2); }
    air_free(&a);
    const char *t = "[Begin Action 1]\n1,0,0,0,3\n1,1,0,0,-1\n[Begin Action 2]\nCopy Action 1\n[Begin Action 1]\n9,9,0,0,1\n[Begin Action 3]\nloopstart\n2,0,0,0,2,H\nClsn1: 1\n Clsn1[0] = 5,5,-5,-5\n2,1,0,0,2\n";
    CHECK(air_parse(t, strlen(t), &a) == 0);
    CHECK(a.nact == 3);
    const AirAction *x = air_find(&a, 1); CHECK(x && x->totaltime == -1 && x->nframes == 2 && a.frames[x->first].group == 1);
    CHECK(air_find(&a, 2) && air_find(&a, 2)->nframes == 2);
    x = air_find(&a, 3); CHECK(x && x->totaltime == 4 && a.frames[x->first].hflip == -1);
    CHECK(x && a.frames[x->first+1].nclsn1 == 1 && a.boxes[a.frames[x->first+1].clsn1].x1 == -5);
    CHECK(x && a.frames[x->first].nclsn1 == 0);
    air_free(&a);
    printf(fails ? "air_test: %d FAILED\n" : "air_test: ok\n", fails);
    return fails != 0;
}
