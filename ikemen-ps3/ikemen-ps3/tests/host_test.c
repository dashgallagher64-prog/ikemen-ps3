/* Host-side test for the portable parts (def.c, sff.c, content.c).
 * Build/run: see tests/run.sh */
#include <stdio.h>
#include "../include/ikemen.h"

static ContentList chars, stages, packs;

int main(int argc, char **argv)
{
    const char *root = argc > 1 ? argv[1] : ".";
    content_scan_chars(&chars, root);
    content_scan_stages(&stages, root);
    content_scan_screenpacks(&packs, root);

    printf("chars=%d stages=%d packs=%d\n", chars.n, stages.n, packs.n);
    for (int i = 0; i < chars.n; i++) {
        SffInfo si;
        int rc = chars.e[i].sprite[0] ? sff_probe(chars.e[i].sprite, &si) : -9;
        printf("  char  '%s' sprite=%s rc=%d", chars.e[i].name, chars.e[i].sprite, rc);
        if (rc == 0) printf(" v%d groups=%u sprites=%u palettes=%u", si.version, si.groups, si.sprites, si.palettes);
        printf("\n");
    }
    for (int i = 0; i < stages.n; i++) printf("  stage '%s' (%s)\n", stages.e[i].name, stages.e[i].def);
    for (int i = 0; i < packs.n;  i++) printf("  pack  '%s' (%s)\n", packs.e[i].name,  packs.e[i].def);
    return 0;
}
