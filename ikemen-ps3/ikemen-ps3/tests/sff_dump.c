/* sff_dump: load an SFF, print stats, write a contact sheet as PPM.
 * usage: sff_dump file.sff [out.ppm] [maxsprites] */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../include/ikemen.h"

int main(int argc, char **argv)
{
    if (argc < 2) { fprintf(stderr, "usage: %s file.sff [out.ppm] [max]\n", argv[0]); return 2; }
    Sff s;
    int rc = sff_load(argv[1], &s);
    if (rc) { printf("sff_load failed rc=%d\n", rc); return 1; }

    int by_err[8] = {0};
    for (int i = 0; i < s.nspr; i++) by_err[s.spr[i].err]++;
    printf("v%d sprites=%d palettes=%d ok=%d trunc=%d format=%d unsupported=%d badlink=%d nomem=%d\n",
           s.version, s.nspr, s.npal, by_err[0], by_err[1], by_err[2], by_err[3], by_err[4], by_err[5]);

    int max = argc > 3 ? atoi(argv[3]) : 120;
    if (max > s.nspr) max = s.nspr;
    int mw = 1, mh = 1;
    for (int i = 0; i < max; i++) {
        if (s.spr[i].w > mw) mw = s.spr[i].w;
        if (s.spr[i].h > mh) mh = s.spr[i].h;
    }
    int scale = (mw < 24 && mh < 24) ? 4 : 1;
    int cw = mw * scale + 4, ch = mh * scale + 4;
    int cols = 1200 / cw; if (cols < 1) cols = 1;
    int rows = (max + cols - 1) / cols;
    int W = cols * cw, H = rows * ch;
    uint32_t *img = malloc((size_t)W * H * 4);
    for (int i = 0; i < W * H; i++) img[i] = 0xFF303030;

    for (int i = 0; i < max; i++) {
        const Sprite *sp = &s.spr[i];
        if (sp->err) continue;
        int cx = (i % cols) * cw + 2, cy = (i / cols) * ch + 2;
        sprite_blit(sp, sff_palette(&s, sp), img, W, H, cx + sp->ax * scale, cy + sp->ay * scale, scale, 0);
    }
    if (argc > 2) {
        FILE *f = fopen(argv[2], "wb");
        fprintf(f, "P6\n%d %d\n255\n", W, H);
        for (int i = 0; i < W * H; i++) {
            uint8_t px[3] = { (img[i] >> 16) & 255, (img[i] >> 8) & 255, img[i] & 255 };
            fwrite(px, 1, 3, f);
        }
        fclose(f);
        printf("wrote %s (%dx%d)\n", argv[2], W, H);
    }
    /* per-format summary for the first few sprites */
    for (int i = 0; i < s.nspr && i < 5; i++)
        printf("  [%d] g=%u i=%u %ux%u axis=(%d,%d) pal=%d err=%d\n", i, s.spr[i].group, s.spr[i].image,
               s.spr[i].w, s.spr[i].h, s.spr[i].ax, s.spr[i].ay, s.spr[i].pal, s.spr[i].err);
    free(img);
    sff_free(&s);
    return 0;
}
