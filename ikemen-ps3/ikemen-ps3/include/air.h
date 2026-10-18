#ifndef IK_AIR_H
#define IK_AIR_H
/* AIR animation files: parsing and playback. Rules follow Ikemen GO's src/anim.go. */
#include <stdint.h>
#include <stddef.h>

/* hard limits so a hostile or corrupt file cannot use unbounded memory */
#define AIR_MAX_FILE     (8u * 1024u * 1024u)
#define AIR_MAX_ACTIONS  8192
#define AIR_MAX_FRAMES   131072
#define AIR_MAX_BOXES    524288
#define AIR_MAX_SET      128      /* boxes in one Clsn list */

typedef struct { int16_t x1, y1, x2, y2; } Box;   /* normalized: x1 <= x2, y1 <= y2 */

enum { TR_NONE = 0, TR_ADD, TR_SUB, TR_SUBADD };

typedef struct {
    int32_t  time;              /* ticks, -1 = forever */
    int16_t  group, image;      /* sprite, -1 = none */
    int16_t  xoff, yoff;        /* already negated for H / V flips, like Ikemen */
    int8_t   hflip, vflip;      /* +1 or -1 */
    uint8_t  trans, srcalpha, dstalpha, pad;
    uint32_t clsn1, clsn2;      /* index of the first box in Air.boxes */
    uint16_t nclsn1, nclsn2;
    float    xscale, yscale, angle;
} AirFrame;

typedef struct {
    int32_t  no;
    uint32_t first;             /* index of the first frame in Air.frames */
    int32_t  nframes;
    int32_t  loopstart;
    int32_t  totaltime;         /* -1 if the last frame lasts forever */
    int32_t  looptime, prelooptime;
    int32_t  copy;              /* "Copy Action N" target, -1 if none (only used while parsing) */
} AirAction;

typedef struct {
    AirAction *act;             /* sorted by .no */
    int        nact;
    AirFrame  *frames;
    uint32_t   nframes;
    Box       *boxes;
    uint32_t   nboxes;
} Air;

int  air_load(const char *path, Air *out);                      /* 0 = ok, <0 = could not read */
int  air_parse(const char *text, size_t len, Air *out);         /* 0 = ok */
void air_free(Air *a);
const AirAction *air_find(const Air *a, int no);

/* ----------------------------------------------------------------------------------- playback */
typedef struct {
    const Air       *air;
    const AirAction *act;
    int32_t curelem;        /* 0-based current element */
    int32_t curelemtime;    /* ticks spent in this element */
    int32_t curtime;        /* ticks since the start of the animation (can be negative after a seek) */
    int32_t drawidx;        /* element being drawn */
    int     newframe;
    int     loopend;
} Anim;

/* Starts an animation at element elem (1-based) and elemtime, like ChangeAnim. act may be NULL (no animation). */
void anim_start(Anim *a, const Air *air, const AirAction *act, int32_t elem, int32_t elemtime);
void anim_tick(Anim *a);                         /* one game tick (Ikemen's Animation.Action) */
const AirFrame *anim_frame(const Anim *a);       /* frame to draw, NULL if none */
int32_t anim_time(const Anim *a);                /* trigger AnimTime */
int32_t anim_elem_time(const Anim *a, int32_t elem);   /* trigger AnimElemTime(elem) */
int32_t anim_elem_no(const Anim *a, int32_t time);     /* trigger AnimElemNo(time) */
int32_t anim_length(const Anim *a);              /* total ticks of one pass (infinite frame counts 1) */

#endif
