/* adv_test: load one (hostile) SFF the way the PS3 build would and report time + memory.
 *   adv_test <file.sff> [limit_mb=200]
 * Prints rc, seconds, bytes of heap still alive after the load, and sprite-level error counts.
 * Exit 1 if it took > 3 s or kept more than 64 MB alive.
 */
#include <stdio.h>
#include <stdlib.h>
#include <malloc.h>
#include <time.h>
#include <signal.h>
#include <unistd.h>
#include <sys/resource.h>
#include "../include/ikemen.h"

static void on_alarm(int s) { (void)s; puts("HANG (>10s)"); fflush(stdout); _exit(3); }

int main(int argc, char **argv)
{
    if (argc < 2) return 2;
    struct rlimit rl; rl.rlim_cur = rl.rlim_max = (rlim_t)(argc > 2 ? atol(argv[2]) : 200) << 20;
    setrlimit(RLIMIT_AS, &rl);
    signal(SIGALRM, on_alarm); alarm(10);

    struct timespec a, b; clock_gettime(CLOCK_MONOTONIC, &a);
    struct mallinfo2 m0 = mallinfo2(); size_t base = m0.uordblks + m0.hblkhd;
    Sff s; int rc = sff_load(argv[1], &s);
    struct mallinfo2 m1 = mallinfo2(); size_t live = m1.uordblks + m1.hblkhd - base;
    clock_gettime(CLOCK_MONOTONIC, &b);
    double dt = (b.tv_sec - a.tv_sec) + (b.tv_nsec - a.tv_nsec) / 1e9;
    printf("rc=%d  %.2fs  live=%zu MB  sprites=%d palettes=%d errors=%d\n", rc, dt, live >> 20,
           rc ? 0 : s.nspr, rc ? 0 : s.npal, rc ? 0 : sff_count_errors(&s));
    if (!rc) sff_free(&s);
    return (dt > 3.0 || live > (64u << 20)) ? 1 : 0;
}
