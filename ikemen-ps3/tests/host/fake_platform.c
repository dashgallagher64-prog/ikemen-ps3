/* fake_platform.c - host stand-in for gfx.c + pad + sysutil so the real source/main.c menu logic
 * can run on a normal computer.
 *
 *   IK_KEYS      sequence of presses, one char per step: u d x o l r (up down cross circle L1 R1), '.' = wait
 *   IK_SHOT_DIR  if set, writes shot_NN.ppm (1280x720) after every step
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <io/pad.h>
#include <sysutil/sysutil.h>
#include "ikemen.h"

#define FW 1280
#define FH 720
static uint32_t fb[FW * FH];
static int frame;
static sysutilCallback cb;
static void *cb_ud;

static const char *keys(void) { const char *k = getenv("IK_KEYS"); return k ? k : ""; }

int gfx_init(void) { gfx_target(fb, FW, FH); return 0; }
uint32_t *gfx_begin(void) { gfx_target(fb, FW, FH); return fb; }

void gfx_end(void)
{
    const char *dir = getenv("IK_SHOT_DIR");
    if (dir && frame % 4 == 3) {
        char p[512];
        snprintf(p, sizeof p, "%s/shot_%02d.ppm", dir, frame / 4);
        FILE *f = fopen(p, "wb");
        if (f) {
            fprintf(f, "P6\n%d %d\n255\n", FW, FH);
            for (int i = 0; i < FW * FH; i++) {
                uint8_t px[3] = { (fb[i] >> 16) & 255, (fb[i] >> 8) & 255, fb[i] & 255 };
                fwrite(px, 1, 3, f);
            }
            fclose(f);
        }
    }
    frame++;
}

int ioPadInit(int max) { (void)max; return 0; }
int ioPadGetInfo(padInfo *i) { memset(i, 0, sizeof *i); i->status[0] = 1; return 0; }

int ioPadGetData(int port, padData *d)
{
    (void)port;
    memset(d, 0, sizeof *d);
    const char *k = keys();
    size_t step = (size_t)frame / 4, n = strlen(k);
    if (step < n && frame % 4 < 2) {
        switch (k[step]) {
            case 'u': d->BTN_UP = 1; break;
            case 'd': d->BTN_DOWN = 1; break;
            case 'x': d->BTN_CROSS = 1; break;
            case 'o': d->BTN_CIRCLE = 1; break;
            case 'l': d->BTN_L1 = 1; break;
            case 'r': d->BTN_R1 = 1; break;
        }
    }
    return 0;
}

int sysUtilRegisterCallback(int slot, sysutilCallback c, void *ud) { (void)slot; cb = c; cb_ud = ud; return 0; }

int sysUtilCheckCallback(void)
{
    /* quit once the key script has been played (plus one step so the last screenshot happens) */
    if ((size_t)frame / 4 > strlen(keys()) && cb) cb(SYSUTIL_EXIT_GAME, 0, cb_ud);
    return 0;
}
