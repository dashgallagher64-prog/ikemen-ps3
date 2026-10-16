/* Checks that sff_load_ex(path, g, i) yields the same pixels/palette as a full sff_load.
 * usage: sff_filter_test file.sff [more.sff ...] */
#include <stdio.h>
#include <string.h>
#include "../include/ikemen.h"

int main(int argc, char **argv)
{
    int bad = 0, checked = 0;
    for (int a = 1; a < argc; a++) {
        Sff full;
        if (sff_load(argv[a], &full)) { printf("%s: load failed\n", argv[a]); bad++; continue; }
        int step = full.nspr > 80 ? full.nspr / 80 : 1;       /* sample large files */
        for (int i = 0; i < full.nspr; i += step) {
            const Sprite *f = &full.spr[i];
            if (f->err) continue;
            Sff part;
            if (sff_load_ex(argv[a], &part, f->group, f->image)) { bad++; continue; }
            const Sprite *p = sff_find(&part, f->group, f->image);
            const uint32_t *pf = sff_palette(&full, f), *pp = p ? sff_palette(&part, p) : NULL;
            int same = p && !p->err && p->w == f->w && p->h == f->h && pf && pp &&
                       !memcmp(p->px, f->px, (size_t)f->w * f->h) && !memcmp(pf, pp, 1024);
            checked++;
            if (!same) { bad++; printf("%s: MISMATCH g=%u i=%u (err=%d)\n", argv[a], f->group, f->image, p ? p->err : -1); }
            /* a filtered load must not decode unrelated sprites */
            int decoded = 0;
            for (int k = 0; k < part.nspr; k++) if (part.spr[k].px && part.spr[k].owns) decoded++;
            if (decoded > 2) { bad++; printf("%s: filtered load decoded %d sprites\n", argv[a], decoded); }
            sff_free(&part);
        }
        sff_free(&full);
    }
    printf("filter test: %d sprites checked, %d problems\n", checked, bad);
    return bad ? 1 : 0;
}
