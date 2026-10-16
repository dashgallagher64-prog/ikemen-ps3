#ifndef IKEMEN_H
#define IKEMEN_H

#include <stdint.h>
#include <stddef.h>

/* Where the installed package keeps its content (same path under CFW and HEN). */
#ifndef IK_ROOT   /* overridable with -DIK_ROOT=... for host tests */
#define IK_ROOT        "/dev_hdd0/game/IKEMEN001/USRDIR"
#endif
#define IK_MAX_ENTRIES 256
#define IK_NAME_LEN    64
#define IK_PATH_LEN    256

/* ---------------------------------------------------------------- DEF/INI */
typedef struct { char section[32]; char key[48]; char value[160]; } DefEntry;
typedef struct { DefEntry *e; int n, cap; } DefFile;

int         def_load(DefFile *f, const char *path);   /* 0 = ok */
void        def_free(DefFile *f);
/* Case-insensitive lookup. Returns NULL if missing. Quotes are stripped. */
const char *def_get(const DefFile *f, const char *section, const char *key);

/* -------------------------------------------------------------------- SFF */
typedef struct {
    int      version;   /* 1 or 2 */
    uint32_t groups;    /* v1 only */
    uint32_t sprites;
    uint32_t palettes;  /* v2 only */
} SffInfo;

int sff_probe(const char *path, SffInfo *out);        /* 0 = ok */

/* Full sprite loading (sprite.c). Pixels are 8-bit palette indices, index 0 = transparent. */
enum {
    SPR_OK = 0,
    SPR_ERR_TRUNC,        /* file/sprite data cut short */
    SPR_ERR_FORMAT,       /* bad header values */
    SPR_ERR_UNSUPPORTED,  /* v2 PNG (fmt 10-12) or raw 24/32-bit: not implemented yet */
    SPR_ERR_BADLINK,      /* linked sprite points at a later/invalid index */
    SPR_ERR_NOMEM,
    SPR_NOT_LOADED        /* skipped by a filtered load: not an error */
};

/* Pixel layout of Sprite.px. Lets the same blitter draw classic 8-bit indexed
 * sprites (PAL8, palette from Sff.pal) and full-color PNG sprites (RGB24/RGBA32,
 * no palette). PAL8 keeps index 0 = transparent; RGBA32 treats alpha < 128 as
 * transparent; RGB24 is fully opaque. */
enum {
    SPR_MODE_PAL8   = 0,   /* px = w*h indices into Sff.pal[sp->pal] (the original SFF path) */
    SPR_MODE_RGB24  = 1,   /* px = w*h*3 bytes, packed R,G,B per pixel; pal = -1 */
    SPR_MODE_RGBA32 = 2,   /* px = w*h*4 bytes, packed R,G,B,A per pixel; pal = -1 */
};

typedef struct {
    uint16_t group, image;
    int16_t  ax, ay;      /* axis (hotspot) */
    uint16_t w, h;
    uint8_t *px;          /* pixel data; size depends on mode (see above). NULL if err/not decoded */
    int      pal;         /* index into Sff.pal, -1 = none (RGB24/RGBA32 always set this to -1) */
    int      owns;        /* 1 if px must be freed (0 = shared with a linked sprite) */
    int      mode;        /* SPR_MODE_* */
    int      err;         /* SPR_* */
} Sprite;

typedef struct {
    int       version;
    int       nspr;
    Sprite   *spr;
    int       npal;
    uint32_t (*pal)[256]; /* 0xFFRRGGBB, matches the XRGB framebuffer */
} Sff;

int            sff_load(const char *path, Sff *out);   /* 0 = ok (individual sprites may still have err != 0) */
void           sff_free(Sff *s);
const Sprite  *sff_find(const Sff *s, int group, int image);
const uint32_t *sff_palette(const Sff *s, const Sprite *sp);
int            sff_count_errors(const Sff *s);
/* Like sff_load but only decodes sprite (group,image) (plus whatever it links to).
 * Pass -1,-1 for everything. Other sprites get err = SPR_NOT_LOADED. Cheap enough for select-screen portraits. */
int            sff_load_ex(const char *path, Sff *out, int group, int image);
/* Copy one decoded sprite + its palette out so the Sff can be freed. 0 = ok. */
int            sff_extract(const Sff *s, int group, int image, Sprite *out, uint32_t pal_out[256]);
void           sprite_free(Sprite *sp);
/* Draw so the sprite's axis lands on (x,y). Integer scale >= 1, optional horizontal flip. Clips.
 * Handles PAL8 (palette lookup, index 0 = transparent), RGB24 (opaque) and RGBA32
 * (alpha < 128 = transparent). For non-PAL8 modes `pal` may be NULL. */
void           sprite_blit(const Sprite *sp, const uint32_t *pal, uint32_t *dst, int dw, int dh,
                           int x, int y, int scale, int flip);

/* ----------------------------------------------------------- png decode */
/* Decode a single PNG stream (signature through IEND) into pixel data.
 * `data`/`len` point at the start of the PNG signature (NOT the SFF 4-byte size
 * prefix - sprite.c skips that). w/h are the dimensions from the SFF sprite
 * header and are checked against the PNG IHDR.
 *
 * On success: *out_px is a malloc'd buffer (caller frees) of
 *   w*h bytes (mode=PAL8), w*h*3 bytes (RGB24) or w*h*4 bytes (RGBA32);
 *   *out_mode is one of SPR_MODE_*; for PAL8, pal_out is filled with up to 256
 *   entries in 0xFFRRGGBB form (missing entries are 0 = transparent).
 *
 * Returns a SPR_* code. Only bit depth 8 is supported for color types 2/4/6,
 * and depths 1/2/4/8 for color type 3 (indexed). Adam7 interlacing is rejected
 * with SPR_ERR_UNSUPPORTED (rare in SFF content). */
int            png_decode(const uint8_t *data, size_t len,
                          int w, int h,
                          uint8_t **out_px, int *out_mode, uint32_t pal_out[256]);

/* ---------------------------------------------------------------- content */
typedef struct {
    char name[IK_NAME_LEN];   /* display name */
    char def[IK_PATH_LEN];    /* full path to the .def */
    char dir[IK_PATH_LEN];    /* folder holding it */
    char sprite[IK_PATH_LEN]; /* full path to the SFF (chars only) */
} ContentEntry;

typedef struct { ContentEntry e[IK_MAX_ENTRIES]; int n; } ContentList;

void content_scan_chars(ContentList *out, const char *root);       /* chars/<dir>/<dir>.def */
void content_scan_stages(ContentList *out, const char *root);      /* stages/NAME.def */
void content_scan_screenpacks(ContentList *out, const char *root); /* data/system.def, data/<pack>/system.def */

/* -------------------------------------------------------------------- gfx */
/* PS3 only (gfx.c). 0xAARRGGBB-style XRGB pixels. */
int       gfx_init(void);
uint32_t *gfx_begin(void);          /* waits for the previous flip, returns back buffer */
void      gfx_end(void);            /* flips */
void      gfx_target(uint32_t *buf, int w, int h);   /* set the buffer the draw calls write to */
int       gfx_width(void);
int       gfx_height(void);
void      gfx_clear(uint32_t color);
void      gfx_rect(int x, int y, int w, int h, uint32_t color);
void      gfx_text(int x, int y, int scale, uint32_t color, const char *s);

#endif
