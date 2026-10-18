#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "air.h"
#include "text.h"
int main(int argc, char **argv)
{
    size_t len; char *t = text_load(argv[1], AIR_MAX_FILE, &len);
    if (!t) return 2;
    srand(1234);
    for (int it = 0; it < 3000; it++) {
        char *m = malloc(len + 1); memcpy(m, t, len);
        int n = 1 + rand() % 40;
        for (int k = 0; k < n; k++) {
            size_t p = (size_t)rand() % len;
            switch (rand() % 3) { case 0: m[p] = (char)rand(); break; case 1: m[p] = "0123456789,-[]:=\n"[rand()%17]; break; default: m[p] = ' '; }
        }
        size_t l = (rand() % 4 == 0) ? (size_t)rand() % len : len;
        Air a; air_parse(m, l, &a);
        for (int i = 0; i < a.nact && i < 20; i++) {
            Anim an; anim_start(&an, &a, &a.act[i], rand() % 20 - 3, rand() % 20 - 3);
            for (int k = 0; k < 300; k++) { anim_tick(&an); anim_frame(&an); anim_elem_no(&an, rand()%100-50); anim_elem_time(&an, rand()%30); }
        }
        air_free(&a); free(m);
    }
    free(t); puts("fuzz_air ok"); return 0;
}
