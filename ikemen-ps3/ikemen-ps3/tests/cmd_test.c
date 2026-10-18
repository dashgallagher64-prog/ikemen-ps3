#include <stdio.h>
#include <string.h>
#include "cmd.h"
static int fails;
#define CHECK(c) do{ if(!(c)){ printf("FAIL line %d: %s\n",__LINE__,#c); fails++; } }while(0)
/* feed a script: each entry is held for n ticks; returns 1 if name was active at any tick */
typedef struct { uint16_t k; int n; } Step;
static int run(const CmdList *l, const char *name, const Step *st, int ns)
{
    CmdState s; cmdstate_init(&s, l);
    int hit = 0;
    for (int i = 0; i < ns; i++) for (int t = 0; t < st[i].n; t++) { cmdstate_tick(&s, st[i].k); if (cmdstate_active(&s, name)) hit = 1; }
    cmdstate_free(&s);
    return hit;
}
int main(int argc, char **argv)
{
    CmdList l;
    if (argc < 2 || cmd_load(argv[1], &l)) { puts("cannot load cmd"); return 2; }
    printf("commands=%d\n", l.n);
    CHECK(l.n == 37);
    enum { D = CK_DOWN, DF = CK_DOWN | CK_FWD, F = CK_FWD, B = CK_BACK, DB = CK_DOWN | CK_BACK };
    Step qcf[] = { {0,2}, {D,2}, {DF,2}, {F,2}, {F|CK_X,1}, {F,3} };
    CHECK(run(&l, "QCF_x", qcf, 6));
    CHECK(!run(&l, "QCF_y", qcf, 6));
    CHECK(!run(&l, "QCB_x", qcf, 6));
    CHECK(run(&l, "x", qcf, 6));
    Step qcb[] = { {0,2}, {D,2}, {DB,2}, {B,2}, {B|CK_Y,1} };
    CHECK(run(&l, "QCB_y", qcb, 5));
    CHECK(!run(&l, "QCF_y", qcb, 5));
    /* too slow */
    Step slow[] = { {D,10}, {DF,10}, {F,10}, {F|CK_X,1} };
    CHECK(!run(&l, "QCF_x", slow, 4));
    /* double tap */
    Step ff[] = { {0,1}, {F,2}, {0,2}, {F,2} };
    CHECK(run(&l, "FF", ff, 4));
    Step f1[] = { {0,1}, {F,8} };
    CHECK(!run(&l, "FF", f1, 2));
    /* simultaneous x+y */
    Step xy[] = { {0,2}, {CK_X|CK_Y,1} };
    CHECK(run(&l, "recovery", xy, 2));
    /* hold down + a */
    Step da[] = { {D,3}, {D|CK_A,1} };
    CHECK(run(&l, "down_a", da, 2));
    Step na[] = { {0,3}, {CK_A,1} };
    CHECK(!run(&l, "down_a", na, 2));
    /* same-name OR and blocking, in both orders */
    Step bl[] = { {0,2}, {F,1}, {F|CK_X,1} };
    CHECK(run(&l, "blocking", bl, 3));
    Step bl2[] = { {0,2}, {CK_X,1}, {F|CK_X,1} };
    CHECK(run(&l, "blocking", bl2, 3));
    /* buffer: "a" has time 1 and buffer 1, active only on the press tick */
    { CmdState s; cmdstate_init(&s, &l);
      cmdstate_tick(&s, 0); cmdstate_tick(&s, CK_A); CHECK(cmdstate_active(&s, "a"));
      cmdstate_tick(&s, CK_A); CHECK(!cmdstate_active(&s, "a"));
      cmdstate_free(&s); }
    cmd_free(&l);
    /* custom: charge ~30a, buffer.time, remap */
    const char *t = "[Defaults]\ncommand.time=10\ncommand.buffer.time=3\n[Command]\nname=\"chg\"\ncommand=~30a\n[Command]\nname=\"nope\"\ncommand=zzz\n[Command]\nname=\"hb\"\ncommand=/$D\n";
    CHECK(cmd_parse(t, strlen(t), &l) == 0);
    CHECK(l.n == 2);
    Step c1[] = { {CK_A,30}, {0,1} }; CHECK(run(&l, "chg", c1, 2));
    Step c2[] = { {CK_A,10}, {0,1} }; CHECK(!run(&l, "chg", c2, 2));
    { CmdState s; cmdstate_init(&s, &l);
      cmdstate_tick(&s, DF); CHECK(cmdstate_active(&s, "hb")); cmdstate_tick(&s, 0); CHECK(cmdstate_active(&s, "hb"));
      cmdstate_tick(&s, 0); cmdstate_tick(&s, 0); CHECK(!cmdstate_active(&s, "hb"));
      cmdstate_free(&s); }
    cmd_free(&l);
    printf(fails ? "cmd_test: %d FAILED\n" : "cmd_test: ok\n", fails);
    return fails != 0;
}
