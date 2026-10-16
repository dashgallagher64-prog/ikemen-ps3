/* png_roundtrip.c - decode a PNG file and verify the pixels match an expected
 * "ground truth" image baked into the test. Used by tests/run_png.sh.
 *
 *   png_roundtrip file.png W H EXPECTED.bin [mode]
 *
 * Decodes file.png (which must be WxH) using png_decode(), then byte-compares
 * the decoded pixels to EXPECTED.bin (raw pixel data, layout depends on mode:
 *   PAL8   -> W*H bytes of palette indices, plus we check the 256-color
 *             palette separately by reading EXPECTED.bin.pal (1024 bytes,
 *             0xAARRGGBB each) if present
 *   RGB24  -> W*H*3 bytes, R,G,B per pixel
 *   RGBA32 -> W*H*4 bytes, R,G,B,A per pixel
 * Exit 0 on match, 1 on mismatch. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../include/ikemen.h"

extern int png_decode(const uint8_t *data, size_t len, int w, int h,
                       uint8_t **out_px, int *out_mode, uint32_t pal_out[256]);

static unsigned char *slurp(const char *path, size_t *len) {
    FILE *f = fopen(path, "rb"); if (!f) return NULL;
    fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
    unsigned char *b = malloc(n); if (!b) { fclose(f); return NULL; }
    size_t r = fread(b, 1, n, f); fclose(f);
    if (r != (size_t)n) { free(b); return NULL; }
    *len = (size_t)n;
    return b;
}

int main(int argc, char **argv) {
    if (argc < 5) { fprintf(stderr, "usage: %s png W H expected.bin [mode]\n", argv[0]); return 2; }
    int w = atoi(argv[2]), h = atoi(argv[3]);
    int want_mode = argc > 5 ? atoi(argv[5]) : -1;
    if (w <= 0 || h <= 0) { fprintf(stderr, "bad w/h\n"); return 2; }

    size_t plen; unsigned char *png = slurp(argv[1], &plen);
    if (!png) { fprintf(stderr, "can't read %s\n", argv[1]); return 2; }

    uint8_t *px = NULL; int mode = 0; uint32_t pal[256];
    int rc = png_decode(png, plen, w, h, &px, &mode, pal);
    free(png);
    if (rc != SPR_OK) { fprintf(stderr, "png_decode rc=%d\n", rc); return 1; }
    if (want_mode >= 0 && mode != want_mode) {
        fprintf(stderr, "mode mismatch: got %d, want %d\n", mode, want_mode);
        free(px); return 1;
    }

    size_t elen; unsigned char *exp = slurp(argv[4], &elen);
    if (!exp) { fprintf(stderr, "can't read %s\n", argv[4]); free(px); return 2; }

    int bpp = (mode == SPR_MODE_PAL8) ? 1 : (mode == SPR_MODE_RGB24) ? 3 : 4;
    size_t want = (size_t)w * h * bpp;
    if (elen < want) { fprintf(stderr, "expected file too short: %zu < %zu\n", elen, want); free(exp); free(px); return 1; }

    int bad = 0;
    for (size_t i = 0; i < want; i++) {
        if (px[i] != exp[i]) {
            if (bad < 5) fprintf(stderr, "byte %zu: got 0x%02x, want 0x%02x\n", i, px[i], exp[i]);
            bad++;
        }
    }

    /* For PAL8 mode, also verify the palette if EXPECTED.bin.pal exists */
    if (mode == SPR_MODE_PAL8) {
        char pal_path[1024];
        snprintf(pal_path, sizeof pal_path, "%s.pal", argv[4]);
        FILE *pf = fopen(pal_path, "rb");
        if (pf) {
            uint32_t exp_pal[256];
            size_t r = fread(exp_pal, 1, sizeof exp_pal, pf); fclose(pf);
            if (r == sizeof exp_pal) {
                for (int i = 0; i < 256; i++) {
                    if (pal[i] != exp_pal[i]) {
                        if (bad < 5) fprintf(stderr, "pal[%d]: got 0x%08x, want 0x%08x\n", i, pal[i], exp_pal[i]);
                        bad++;
                    }
                }
            }
        }
    }

    free(exp); free(px);
    if (bad) { fprintf(stderr, "FAIL: %d byte/pal mismatches\n", bad); return 1; }
    printf("ok: %dx%d mode=%d, %zu bytes verified\n", w, h, mode, want);
    return 0;
}
