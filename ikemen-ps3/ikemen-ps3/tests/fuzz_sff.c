/* fuzz_sff: mutate real SFF files and DEF text, feed them to the parsers, and make sure nothing
 * crashes, hangs or balloons in memory. Run under ASan/UBSan (memory safety) and again without
 * sanitizers plus an address-space limit (out-of-memory behaviour).
 *
 *   fuzz_sff <iterations> <seed> <tmpfile> <seed.sff> [more.sff ...]
 *   env FUZZ_LIMIT_MB=200   cap address space (use only in a non-sanitizer build)
 *   env FUZZ_HEAP_MB=64     fail if one loaded Sff keeps more than this much heap alive
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <time.h>
#include <malloc.h>
#include <sys/resource.h>
#include "../include/ikemen.h"

static uint64_t rng = 88172645463325252ull;
static uint32_t rnd(void) { rng ^= rng << 13; rng ^= rng >> 7; rng ^= rng << 17; return (uint32_t)(rng >> 11); }
static uint32_t below(uint32_t n) { return n ? rnd() % n : 0; }

static const char *cur_desc = "";
static void on_alarm(int s) { (void)s; fprintf(stderr, "HANG (>5s) while: %s\n", cur_desc); abort(); }

static uint8_t *slurp(const char *p, size_t *n)
{
    FILE *f = fopen(p, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END); long l = ftell(f); fseek(f, 0, SEEK_SET);
    uint8_t *b = malloc((size_t)l);
    if (fread(b, 1, (size_t)l, f) != (size_t)l) { fclose(f); free(b); return NULL; }
    fclose(f); *n = (size_t)l; return b;
}

static const uint32_t extreme[] = { 0, 1, 2, 0x7F, 0x80, 0xFF, 0xFFFF, 0x10000, 0x7FFFFFFF, 0x80000000u, 0xFFFFFFFFu, 0xFFFFFFF0u, 28, 32, 64 };

/* returns a human description of what it did */
static const char *mutate(uint8_t *b, size_t *n)
{
    switch (below(7)) {
    case 0: { int k = 1 + below(16); while (k--) b[below(*n)] ^= (uint8_t)(1u << below(8)); return "bit flips"; }
    case 1: { int k = 1 + below(32); while (k--) b[below(*n)] = (uint8_t)rnd(); return "random bytes"; }
    case 2: *n = 1 + below(*n); return "truncate";
    case 3: { /* header fields get extreme values */
        int k = 1 + below(4);
        while (k--) { size_t o = below(*n < 0x48 ? *n : 0x48) & ~3u; if (o + 4 <= *n) { uint32_t v = extreme[below(sizeof extreme / sizeof *extreme)]; memcpy(b + o, &v, 4); } }
        return "header extremes"; }
    case 4: { /* sprite-header / palette-header area: first few KB after the file header */
        size_t lim = *n < 0x2000 ? *n : 0x2000; int k = 1 + below(12);
        while (k--) { size_t o = 0x40 + below(lim > 0x44 ? lim - 0x44 : 1); if (o + 4 <= *n) { uint32_t v = extreme[below(sizeof extreme / sizeof *extreme)]; memcpy(b + o, &v, 4); } }
        return "sprite-header extremes"; }
    case 5: { size_t o = below(*n), l = below(512); if (o + l > *n) l = *n - o; memset(b + o, below(2) ? 0x00 : 0xFF, l); return "fill chunk"; }
    default: { size_t o = below(*n), l = below(256); if (o + l > *n) l = *n - o;
               for (size_t i = 0; i < l; i++) b[o + i] = (uint8_t)rnd();
               return "random chunk"; }
    }
}

int main(int argc, char **argv)
{
    if (argc < 5) { fprintf(stderr, "usage: %s iterations seed tmpfile seed.sff...\n", argv[0]); return 2; }
    long iters = atol(argv[1]);
    rng ^= (uint64_t)atoll(argv[2]) * 0x9E3779B97F4A7C15ull; for (int i = 0; i < 8; i++) rnd();
    const char *tmp = argv[3];
    int nseed = argc - 4;

    if (getenv("FUZZ_LIMIT_MB")) {
        struct rlimit rl; rl.rlim_cur = rl.rlim_max = (rlim_t)atol(getenv("FUZZ_LIMIT_MB")) << 20;
        setrlimit(RLIMIT_AS, &rl);
    }
    long heap_cap = (getenv("FUZZ_HEAP_MB") ? atol(getenv("FUZZ_HEAP_MB")) : 64) << 20;
    signal(SIGALRM, on_alarm);

    uint8_t *seeds[64]; size_t sizes[64];
    for (int i = 0; i < nseed && i < 64; i++) {
        seeds[i] = slurp(argv[4 + i], &sizes[i]);
        if (!seeds[i]) { fprintf(stderr, "cannot read %s\n", argv[4 + i]); return 2; }
    }

    long loaded = 0, rejected = 0, spr_err = 0, blits = 0; size_t worst_heap = 0; double worst_t = 0;
    for (long it = 0; it < iters; it++) {
        int s = below(nseed);
        size_t n = sizes[s];
        uint8_t *b = malloc(n);
        memcpy(b, seeds[s], n);
        char desc[160];
        snprintf(desc, sizeof desc, "iter %ld seed=%s", it, argv[4 + s]);
        const char *what = mutate(b, &n);
        if (below(3) == 0) what = mutate(b, &n);          /* stack two mutations sometimes */
        snprintf(desc + strlen(desc), sizeof desc - strlen(desc), " mutation=%s", what);

        FILE *f = fopen(tmp, "wb"); fwrite(b, 1, n, f); fclose(f);
        free(b);

        cur_desc = desc; alarm(5);
        struct timespec t0, t1; clock_gettime(CLOCK_MONOTONIC, &t0);
        struct mallinfo2 m0 = mallinfo2(); size_t base = m0.uordblks + m0.hblkhd;

        Sff sf; int rc;
        if (below(2)) rc = sff_load(tmp, &sf);
        else          rc = sff_load_ex(tmp, &sf, below(4) ? (int)below(30) : 9000, (int)below(8));
        if (rc != 0) { rejected++; }
        else {
            loaded++;
            struct mallinfo2 m1 = mallinfo2(); size_t live = m1.uordblks + m1.hblkhd - base;
            if (live > worst_heap) worst_heap = live;
            if ((long)live > heap_cap) { fprintf(stderr, "HEAP %zu MB kept alive by one load: %s\n", live >> 20, desc); abort(); }
            spr_err += sff_count_errors(&sf);
            /* exercise the public API on whatever came out */
            static uint32_t fb[320 * 240];
            for (int k = 0; k < sf.nspr && k < 40; k++) {
                const Sprite *sp = &sf.spr[k];
                const Sprite *g = sff_find(&sf, sp->group, sp->image); (void)g;
                sprite_blit(sp, sff_palette(&sf, sp), fb, 320, 240, (int)below(400) - 40, (int)below(300) - 40, 1 + below(3), below(2));
                blits++;
            }
            Sprite ex; uint32_t pal[256];
            if (sf.nspr) sff_extract(&sf, sf.spr[0].group, sf.spr[0].image, &ex, pal), sprite_free(&ex);
            sff_free(&sf);
        }
        clock_gettime(CLOCK_MONOTONIC, &t1); alarm(0);
        double dt = (t1.tv_sec - t0.tv_sec) + (t1.tv_nsec - t0.tv_nsec) / 1e9;
        if (dt > worst_t) worst_t = dt;
        if (dt > 2.0) fprintf(stderr, "slow (%.1fs): %s\n", dt, desc);
    }
    printf("fuzz done: %ld iterations, %ld loaded, %ld rejected, %ld sprite-level errors, %ld blits, "
           "worst load %.2fs, worst live heap %zu KB\n", iters, loaded, rejected, spr_err, blits, worst_t, worst_heap >> 10);
    fflush(stdout);
    for (int i = 0; i < nseed && i < 64; i++) free(seeds[i]);
    return 0;
}
