/* fuzz_fight: mutate a real character's .cns/.cmd/.air, load it and play random inputs. Must never crash or hang.
 *   fuzz_fight <iterations> <seed> <workdir> <common_dir> <char_dir> <name>   (char_dir/name.{def,cns,cmd,air,sff}) */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include "fight.h"
static uint64_t rng = 88172645463325252ull;
static uint32_t rnd(void) { rng ^= rng << 13; rng ^= rng >> 7; rng ^= rng << 17; return (uint32_t)(rng >> 11); }
static void on_alarm(int s) { (void)s; fprintf(stderr, "HANG\n"); abort(); }
static char *slurp(const char *p, size_t *n) { FILE *f = fopen(p, "rb"); if (!f) return NULL; fseek(f, 0, SEEK_END); *n = (size_t)ftell(f); fseek(f, 0, SEEK_SET); char *b = malloc(*n + 1); if (fread(b, 1, *n, f) != *n) { free(b); fclose(f); return NULL; } fclose(f); return b; }
int main(int argc, char **argv)
{
    int iters = atoi(argv[1]); rng ^= (uint64_t)atoi(argv[2]) * 2654435761u;
    const char *work = argv[3], *common = argv[4], *dir = argv[5], *name = argv[6];
    const char *exts[] = { "cns", "cmd", "air", "def" };
    char path[1024]; size_t len[4]; char *orig[4];
    for (int i = 0; i < 4; i++) { snprintf(path, sizeof path, "%s/%s.%s", dir, name, exts[i]); orig[i] = slurp(path, &len[i]); if (!orig[i]) { puts("seed missing"); return 2; } }
    char sffsrc[1024]; snprintf(sffsrc, sizeof sffsrc, "%s/%s.sff", dir, name);
    snprintf(path, sizeof path, "cp '%s' '%s/%s.sff'", sffsrc, work, name); if (system(path)) return 2;
    signal(SIGALRM, on_alarm);
    int loaded = 0, fought = 0;
    for (int it = 0; it < iters; it++) {
        alarm(20);
        for (int i = 0; i < 4; i++) {
            char *m = malloc(len[i] + 1); memcpy(m, orig[i], len[i]);
            int nm = (int)(rnd() % 30);
            for (int k = 0; k < nm; k++) {
                size_t p = rnd() % len[i];
                switch (rnd() % 4) { case 0: m[p] = (char)rnd(); break; case 1: m[p] = "0123456789,-=\n"[rnd() % 14]; break;
                                     case 2: { size_t q = rnd() % len[i]; m[p] = m[q]; break; } default: m[p] = ' '; }
            }
            snprintf(path, sizeof path, "%s/%s.%s", work, name, exts[i]);
            FILE *f = fopen(path, "wb"); fwrite(m, 1, len[i], f); fclose(f); free(m);
        }
        snprintf(path, sizeof path, "%s/%s.def", work, name);
        CharData cd; int rc = chardata_load(&cd, path, common);
        if (rc == 0) {
            loaded++;
            Fight f;
            if (fight_init(&f, &cd, &cd) == 0) {
                fought++;
                for (int t = 0; t < 500; t++) {
                    uint16_t a = (t % 7 == 0) ? (uint16_t)(rnd() & 0x7ff) : 0, b = (t % 5 == 0) ? (uint16_t)(rnd() & 0x7ff) : 0;
                    static uint16_t ha, hb; if (a) ha = a; if (b) hb = b;
                    fight_tick(&f, ha, hb);
                }
                fight_free(&f);
            }
        }
        chardata_free(&cd);
    }
    printf("fuzz_fight ok: %d iterations, %d loaded, %d fought\n", iters, loaded, fought);
    return 0;
}
