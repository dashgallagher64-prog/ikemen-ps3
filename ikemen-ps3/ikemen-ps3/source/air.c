/* air.c - AIR animation files: parsing and playback.
 *
 * Parsing and timing rules were checked against Ikemen GO's src/anim.go (ReadAnimFrame, readAnimation,
 * readClsn, compileAction, resolveCopyAction, Action, animSeek, SetAnimElem, AnimElemTime, AnimElemNo).
 * Deliberate differences: interpolate lines are ignored, and an action with no valid frames is dropped
 * (Ikemen instead lets it inherit the next action in the file).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "air.h"
#include "text.h"

/* ---------------------------------------------------------------------------- growth helpers */

static int grow(void **p, uint32_t *cap, uint32_t need, size_t elem, uint32_t limit)
{
    if (need <= *cap) return 0;
    if (need > limit) return -1;
    uint32_t nc = *cap ? *cap * 2 : 256;
    while (nc < need) nc *= 2;
    if (nc > limit) nc = limit;
    void *np = realloc(*p, (size_t)nc * elem);
    if (!np) return -1;
    *p = np;
    *cap = nc;
    return 0;
}

typedef struct { uint32_t start; uint32_t n; } Set;

typedef struct {
    Air      *a;
    uint32_t  cap_frames, cap_boxes, cap_act;
} Builder;

static int add_boxes(Builder *b, uint32_t n, uint32_t *start)
{
    if (grow((void **)&b->a->boxes, &b->cap_boxes, b->a->nboxes + n, sizeof(Box), AIR_MAX_BOXES)) return -1;
    *start = b->a->nboxes;
    memset(&b->a->boxes[*start], 0, (size_t)n * sizeof(Box));
    b->a->nboxes += n;
    return 0;
}

/* ---------------------------------------------------------------------------- small parsers */

static int16_t clamp16(int32_t v) { return (int16_t)(v < -32768 ? -32768 : v > 32767 ? 32767 : v); }

static void norm_box(Box *bx)
{
    if (bx->x1 > bx->x2) { int16_t t = bx->x1; bx->x1 = bx->x2; bx->x2 = t; }
    if (bx->y1 > bx->y2) { int16_t t = bx->y1; bx->y1 = bx->y2; bx->y2 = t; }
}

/* float parse for scale/angle: returns 1 if the whole (trimmed) text is a number */
static int parse_float(const char *s, float *out)
{
    s = text_skip_blank(s);
    const char *p = s;
    int neg = 0, digits = 0;
    double v = 0, frac = 0.1;
    if (*p == '-' || *p == '+') { neg = (*p == '-'); p++; }
    while (*p >= '0' && *p <= '9') { v = v * 10 + (*p - '0'); p++; digits++; if (v > 1e9) v = 1e9; }
    if (*p == '.') {
        p++;
        while (*p >= '0' && *p <= '9') { v += (*p - '0') * frac; frac *= 0.1; p++; digits++; }
    }
    p = text_skip_blank(p);
    if (!digits || *p) return 0;
    *out = (float)(neg ? -v : v);
    return 1;
}

static void alpha_numbers(const char *a, int start, int defsrc, int defdst, uint8_t *src, uint8_t *dst)
{
    int i = start, alp = 0;
    *src = (uint8_t)defsrc; *dst = (uint8_t)defdst;
    for (; a[i] >= '0' && a[i] <= '9'; i++) { alp = alp * 10 + (a[i] - '0'); if (alp > 0x3fff) alp &= 0x3fff; }
    alp &= 0x3fff;
    if (alp > 255) alp = 255;
    *src = (uint8_t)alp;
    if (a[i] == 'd') {
        i++;
        if (a[i] >= '0' && a[i] <= '9') {
            alp = 0;
            for (; a[i] >= '0' && a[i] <= '9'; i++) { alp = alp * 10 + (a[i] - '0'); if (alp > 0x3fff) alp &= 0x3fff; }
            alp &= 0x3fff;
            if (alp > 255) alp = 255;
            *dst = (uint8_t)alp;
        }
    }
}

static void parse_alpha(const char *field, AirFrame *f)
{
    char a[64];
    const char *p = strpbrk(field, "ASas");
    if (!p) return;                                  /* unknown text: leave the frame opaque */
    snprintf(a, sizeof a, "%s", p);
    text_lower(a);
    size_t n = strlen(a);
    if (!strcmp(a, "a1"))                { f->trans = TR_ADD; f->srcalpha = 255; f->dstalpha = 128; }
    else if (n >= 3 && !strncmp(a, "sas", 3)) { f->trans = TR_SUBADD; alpha_numbers(a, 3, 255, 255, &f->srcalpha, &f->dstalpha); }
    else if (n >= 2 && !strncmp(a, "sa", 2))  { f->trans = TR_SUBADD; f->srcalpha = 255; f->dstalpha = 255; }
    else if (n >= 2 && !strncmp(a, "as", 2))  { f->trans = TR_ADD;    alpha_numbers(a, 2, 255, 0, &f->srcalpha, &f->dstalpha); }
    else if (n >= 2 && !strncmp(a, "ss", 2))  { f->trans = TR_SUB;    alpha_numbers(a, 2, 255, 255, &f->srcalpha, &f->dstalpha); }
    else if (a[0] == 'a')                { f->trans = TR_ADD; f->srcalpha = 255; f->dstalpha = 255; }
    else if (a[0] == 's')                { f->trans = TR_SUB; f->srcalpha = 255; f->dstalpha = 255; }
}

/* "group, image, x, y, time[, flags[, alpha[, xscale[, yscale[, angle]]]]]". Returns 1 if a frame was read. */
static int parse_frame(const char *line, AirFrame *f)
{
    char buf[TEXT_LINE_MAX];
    snprintf(buf, sizeof buf, "%s", line);
    char *fld[10];
    int nf = 0;
    char *p = buf;
    while (nf < 10) {
        fld[nf++] = p;
        if (nf == 10) break;
        char *c = strchr(p, ',');
        if (!c) break;
        *c = 0;
        p = c + 1;
    }
    if (nf < 5) return 0;

    memset(f, 0, sizeof *f);
    f->time = -1; f->group = -1; f->hflip = f->vflip = 1;
    f->srcalpha = 255; f->dstalpha = 0;
    f->xscale = f->yscale = 1.0f;

    int32_t g, i, x, y, t;
    text_parse_int(fld[0], &g); text_parse_int(fld[1], &i);
    text_parse_int(fld[2], &x); text_parse_int(fld[3], &y); text_parse_int(fld[4], &t);
    f->group = g; f->image = i; f->xoff = x; f->yoff = y; f->time = t;

    if (nf >= 6) {
        for (const char *q = fld[5]; *q; q++) {
            if (*q == 'H' || *q == 'h') { f->hflip = -1; f->xoff = -f->xoff; }
            else if (*q == 'V' || *q == 'v') { f->vflip = -1; f->yoff = -f->yoff; }
        }
    }
    if (nf >= 7 && *text_skip_blank(fld[6])) parse_alpha(fld[6], f);
    if (nf >= 8 && *text_skip_blank(fld[7])) { float v; if (parse_float(fld[7], &v)) f->xscale = v; }
    if (nf >= 9 && *text_skip_blank(fld[8])) { float v; if (parse_float(fld[8], &v)) f->yscale = v; }
    if (nf >= 10 && *text_skip_blank(fld[9])) { float v; if (parse_float(fld[9], &v)) f->angle = v; }
    return 1;
}

/* ---------------------------------------------------------------------------- the parser */

typedef struct {
    int       open;                 /* inside a [Begin Action] block */
    int32_t   no;
    uint32_t  frame_mark, box_mark; /* roll back to these if the action is dropped */
    int32_t   loopstart, ols;
    int       copy_target;          /* -1 if none */
    Set       d1, d2, c1, c2;       /* default and current box sets */
    int       use1, use2;           /* next frame takes the default set */
} Cur;

static void cur_begin(Cur *c, Builder *b, int32_t no)
{
    memset(c, 0, sizeof *c);
    c->open = 1;
    c->no = no;
    c->frame_mark = b->a->nframes;
    c->box_mark = b->a->nboxes;
    c->copy_target = -1;
    c->use1 = c->use2 = 1;
}

static int find_idx(const Builder *b, int32_t no)
{
    for (int i = 0; i < b->a->nact; i++) if (b->a->act[i].no == no) return i;
    return -1;
}

static void cur_finish(Cur *c, Builder *b)
{
    if (!c->open) return;
    c->open = 0;
    Air *a = b->a;
    uint32_t nfr = a->nframes - c->frame_mark;
    int keep = (c->copy_target >= 0 || nfr > 0) && a->nact < AIR_MAX_ACTIONS && find_idx(b, c->no) < 0;
    if (!keep) {                                   /* first definition of a number wins; roll back */
        a->nframes = c->frame_mark;
        a->nboxes = c->box_mark;
        return;
    }
    if (grow((void **)&a->act, &b->cap_act, (uint32_t)a->nact + 1, sizeof(AirAction), AIR_MAX_ACTIONS)) {
        a->nframes = c->frame_mark; a->nboxes = c->box_mark; return;
    }
    AirAction *x = &a->act[a->nact++];
    memset(x, 0, sizeof *x);
    x->no = c->no;
    x->first = c->frame_mark;
    x->nframes = (int32_t)nfr;
    x->copy = c->copy_target;
    if (c->copy_target >= 0) { x->nframes = 0; a->nframes = c->frame_mark; return; }

    int32_t loopstart = c->loopstart;
    if (loopstart >= (int32_t)nfr) loopstart = c->ols;
    x->loopstart = loopstart;

    const AirFrame *fr = &a->frames[x->first];
    if (fr[nfr - 1].time == -1) {
        x->totaltime = -1;
    } else {
        int64_t tot = 0, loop = 0, pre = 0, tmp = 0;
        for (uint32_t i = 0; i < nfr; i++) {
            if (fr[i].time == -1) { tot = 0; loop = -tmp; pre = 0; }
            tot += fr[i].time;
            if ((int32_t)i < loopstart) { pre += fr[i].time; tmp += fr[i].time; }
            else loop += fr[i].time;
        }
        if (tot == -1) pre = 0;
        /* times can be hostile; keep them in a sane range */
        if (tot > 1000000000) tot = 1000000000;
        if (tot < -1000000000) tot = -1000000000;
        if (loop > 1000000000) loop = 1000000000;
        if (loop < -1000000000) loop = -1000000000;
        if (pre > 1000000000) pre = 1000000000;
        if (pre < -1000000000) pre = -1000000000;
        x->totaltime = (int32_t)tot; x->looptime = (int32_t)loop; x->prelooptime = (int32_t)pre;
    }
}

/* reads up to size box lines (after a "ClsnN:" header). Unread lines are given back through *pos. */
static int read_clsn(Builder *b, const char *text, size_t len, size_t *pos, int32_t size, Set *out)
{
    uint32_t n = (uint32_t)(size < 0 ? 0 : size > AIR_MAX_SET ? AIR_MAX_SET : size);
    out->n = n;
    out->start = 0;
    if (n && add_boxes(b, n, &out->start)) { out->n = 0; return -1; }
    char line[TEXT_LINE_MAX];
    int32_t count = 0;
    uint8_t filled[AIR_MAX_SET];
    memset(filled, 0, sizeof filled);
    while (count < (int32_t)n) {
        size_t save = *pos;
        if (!text_next_line(text, len, pos, line, sizeof line)) break;
        if (!line[0]) continue;
        text_lower(line);
        if (strncmp(line, "clsn", 4) != 0) { *pos = save; break; }
        char *eq = strchr(line, '=');
        if (!eq) { *pos = save; break; }
        int idx = -1;
        char *lb = strchr(line, '[');
        if (lb && lb < eq) {
            char *rb = strchr(lb, ']');
            if (rb && rb > lb) {
                int ok = 1; const char *q = text_skip_blank(lb + 1);
                if (q >= rb) ok = 0;
                for (const char *r = q; r < rb; r++) if (*r < '0' || *r > '9') { if (*r != ' ' && *r != '\t') ok = 0; }
                if (ok) { int32_t v; text_parse_int(lb + 1, &v); idx = v; }
            }
        }
        if (idx < 0 || idx >= (int)n || filled[idx]) {
            idx = -1;
            for (uint32_t k = 0; k < n; k++) if (!filled[k]) { idx = (int)k; break; }
            if (idx < 0) continue;
        }
        int32_t v[4] = {0, 0, 0, 0};
        const char *q = eq + 1;
        int got = 0;
        while (got < 4) {
            int used = text_parse_int(q, &v[got]);
            const char *c = strchr(q, ',');
            if (!c) { if (used) got++; break; }
            got++;
            q = c + 1;
        }
        if (got < 4) { *pos = save; break; }
        Box *bx = &b->a->boxes[out->start + (uint32_t)idx];
        bx->x1 = clamp16(v[0]); bx->y1 = clamp16(v[1]); bx->x2 = clamp16(v[2]); bx->y2 = clamp16(v[3]);
        norm_box(bx);
        filled[idx] = 1;
        count++;
    }
    return 0;
}

static int cmp_act(const void *x, const void *y)
{
    int32_t a = ((const AirAction *)x)->no, b = ((const AirAction *)y)->no;
    return a < b ? -1 : a > b;
}

static void resolve_copies(Air *a)
{
    if (a->nact > 1) qsort(a->act, (size_t)a->nact, sizeof(AirAction), cmp_act);
    for (int i = 0; i < a->nact; i++) {
        AirAction *x = &a->act[i];
        for (int loops = 0; x->copy >= 0 && loops < 8; loops++) {
            const AirAction *t = air_find(a, x->copy);
            if (!t || t == x) break;
            if (t->copy >= 0) { x->copy = t->copy; continue; }
            int32_t no = x->no;
            *x = *t;
            x->no = no;
            x->copy = -1;
            break;
        }
    }
    int w = 0;                                       /* drop actions whose copy never resolved */
    for (int i = 0; i < a->nact; i++) if (a->act[i].copy < 0) a->act[w++] = a->act[i];
    a->nact = w;
}

int air_parse(const char *text, size_t len, Air *out)
{
    memset(out, 0, sizeof *out);
    Builder b;
    memset(&b, 0, sizeof b);
    b.a = out;
    Cur c;
    memset(&c, 0, sizeof c);

    char line[TEXT_LINE_MAX], low[TEXT_LINE_MAX];
    size_t pos = 0;
    while (text_next_line(text, len, &pos, line, sizeof line)) {
        if (!line[0]) continue;
        if (line[0] == '[') {
            cur_finish(&c, &b);
            snprintf(low, sizeof low, "%s", line);
            text_lower(low);
            char *rb = strchr(low, ']');
            if (!rb) continue;
            *rb = 0;
            const char *s = text_skip_blank(low + 1);
            if (strncmp(s, "begin action ", 13) == 0 || strncmp(s, "begin  action ", 14) == 0) {
                const char *q = strstr(s, "action") + 6;
                int32_t no;
                text_parse_int(q, &no);
                cur_begin(&c, &b, no);
            }
            continue;
        }
        if (!c.open) continue;

        snprintf(low, sizeof low, "%s", line);
        text_lower(low);

        if (strncmp(low, "copy action ", 12) == 0) {
            int32_t id = -1;
            const char *q = text_skip_blank(low + 12);
            if (!text_parse_int(q, &id) || id < 0) id = -1;
            c.copy_target = id;
            if (id < 0) { c.open = 0; b.a->nframes = c.frame_mark; b.a->nboxes = c.box_mark; continue; }
            cur_finish(&c, &b);
            continue;
        }
        if ((low[0] >= '0' && low[0] <= '9') || low[0] == '-') {
            AirFrame f;
            if (!parse_frame(low, &f)) continue;
            c.ols = c.loopstart;
            if (c.use1) c.c1 = c.d1;
            if (c.use2) c.c2 = c.d2;
            f.clsn1 = c.c1.start; f.nclsn1 = (uint16_t)c.c1.n;
            f.clsn2 = c.c2.start; f.nclsn2 = (uint16_t)c.c2.n;
            if (grow((void **)&out->frames, &b.cap_frames, out->nframes + 1, sizeof(AirFrame), AIR_MAX_FRAMES)) continue;
            out->frames[out->nframes++] = f;
            c.use1 = c.use2 = 1;
            continue;
        }
        if (strncmp(low, "loopstart", 9) == 0) {
            c.loopstart = (int32_t)(out->nframes - c.frame_mark);
            continue;
        }
        if (strlen(low) >= 5 && strncmp(low, "clsn", 4) == 0) {
            char *colon = strchr(low, ':');
            if (!colon) continue;
            int32_t size;
            text_parse_int(colon + 1, &size);
            if (size < 0) continue;
            char type = low[4];
            int isdef = strlen(low) >= 12 && strncmp(low + 5, "default", 7) == 0;
            if (type != '1' && type != '2') continue;
            Set s;
            if (read_clsn(&b, text, len, &pos, size, &s)) continue;
            if (type == '1') { c.c1 = s; if (isdef) c.d1 = s; c.use1 = 0; }
            else             { c.c2 = s; if (isdef) c.d2 = s; c.use2 = 0; }
            continue;
        }
    }
    cur_finish(&c, &b);
    resolve_copies(out);
    return 0;
}

int air_load(const char *path, Air *out)
{
    size_t len;
    memset(out, 0, sizeof *out);
    char *t = text_load(path, AIR_MAX_FILE, &len);
    if (!t) return -1;
    int rc = air_parse(t, len, out);
    free(t);
    return rc;
}

void air_free(Air *a)
{
    free(a->act); free(a->frames); free(a->boxes);
    memset(a, 0, sizeof *a);
}

const AirAction *air_find(const Air *a, int no)
{
    int lo = 0, hi = a->nact - 1;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        if (a->act[mid].no == no) return &a->act[mid];
        if (a->act[mid].no < no) lo = mid + 1; else hi = mid - 1;
    }
    return NULL;
}

/* ---------------------------------------------------------------------------- playback */

static const AirFrame *frames_of(const Anim *a) { return a->air->frames + a->act->first; }

static void seek(Anim *a, int32_t elem)
{
    const AirFrame *fr = frames_of(a);
    int32_t nf = a->act->nframes;
    if (elem < 0) elem = 0;
    int foo = 1;
    for (;;) {
        a->curelem = elem;
        while (a->curelem < nf && fr[a->curelem].time <= 0) {
            if (a->curelem == nf - 1 && fr[a->curelem].time == -1) break;
            a->curelem++;
        }
        if (a->curelem < nf) break;
        foo = !foo;
        if (foo) { a->curelem = nf - 1; break; }
    }
    if (a->curelem < 0) a->curelem = 0;
    else if (a->curelem >= nf) a->curelem = nf - 1;
}

static void update_sprite(Anim *a)
{
    const AirAction *ac = a->act;
    if (ac->nframes == 0) return;
    if (ac->totaltime > 0) {
        if (a->curtime >= ac->totaltime) { a->curelemtime = 0; a->newframe = 1; a->curelem = ac->loopstart; }
        seek(a, a->curelem);
        if (ac->prelooptime < 0 && a->curtime >= ac->totaltime + ac->prelooptime &&
            a->curtime >= ac->totaltime - ac->looptime &&
            (a->curtime == ac->totaltime + ac->prelooptime || a->curtime == ac->totaltime - ac->looptime)) {
            a->curelemtime = 0; a->newframe = 1; a->curelem = 0;
        }
    }
    a->newframe = 0;
    a->drawidx = a->curelem;
}

static int32_t max0(int32_t v) { return v < 0 ? 0 : v; }

int32_t anim_time(const Anim *a)
{
    return (a->act ? a->curtime - a->act->totaltime : 0);
}

int32_t anim_elem_time(const Anim *a, int32_t elem)
{
    if (!a->act || a->act->nframes == 0) return 0;
    if (elem > a->act->nframes) {
        int32_t t = anim_time(a);
        return t > 0 ? 0 : t;
    }
    const AirFrame *fr = frames_of(a);
    int32_t e = (elem < 0 ? 0 : elem) - 1, t = a->curtime;
    for (int32_t i = 0; i < e; i++) t -= max0(fr[i].time);
    return t;
}

static void set_elem(Anim *a, int32_t elem, int32_t elemtime)
{
    const AirFrame *fr = frames_of(a);
    int32_t nf = a->act->nframes;
    a->curelem = elem - 1 < 0 ? 0 : elem - 1;
    if (a->curelem >= nf) a->curelem = 0;
    a->drawidx = a->curelem;
    if (elemtime != 0) {
        int32_t ft = fr[a->curelem].time;
        if (elemtime < 0 || (ft != -1 && elemtime >= ft)) elemtime = 0;
    }
    a->curelemtime = elemtime;
    a->newframe = 1;
    a->loopend = 0;
    update_sprite(a);
    a->curtime = 0;
    a->curtime = -anim_elem_time(a, a->curelem + 1) + a->curelemtime;
}

void anim_start(Anim *a, const Air *air, const AirAction *act, int32_t elem, int32_t elemtime)
{
    memset(a, 0, sizeof *a);
    a->air = air;
    a->act = act;
    a->newframe = 1;
    if (!act || act->nframes == 0) return;
    if (elem > 1 || elemtime != 0) set_elem(a, elem, elemtime);
}

void anim_tick(Anim *a)
{
    if (!a->act) return;
    if (a->act->nframes == 0) { a->loopend = 1; return; }
    const AirAction *ac = a->act;
    const AirFrame *fr = frames_of(a);
    int32_t nf = ac->nframes;

    update_sprite(a);

#define NEXT() do { \
        if (ac->totaltime != -1 || a->curelem < nf - 1) { \
            a->curelemtime = 0; a->newframe = 1; \
            for (;;) { \
                a->curelem++; \
                if ((ac->totaltime == -1 && a->curelem == nf - 1) || a->curelem >= nf || fr[a->curelem].time > 0) break; \
            } \
        } } while (0)

    if (fr[a->curelem].time <= 0) NEXT();
    if (a->curelem < nf) {
        a->curelemtime++;
        if (a->curelemtime >= fr[a->curelem].time) {
            NEXT();
            if (a->curelem >= nf) a->curelem = ac->loopstart;
        }
    } else {
        a->curelem = ac->loopstart;
    }
#undef NEXT
    if (ac->totaltime != -1 && a->curtime >= ac->totaltime) a->curtime = ac->totaltime - ac->looptime;
    a->curtime++;
    if (ac->totaltime != -1 && a->curtime >= ac->totaltime) a->loopend = 1;
    if (a->curelem < 0 || a->curelem >= nf) a->curelem = ac->loopstart < nf && ac->loopstart >= 0 ? ac->loopstart : 0;
}

const AirFrame *anim_frame(const Anim *a)
{
    if (!a->act || a->act->nframes == 0) return NULL;
    int32_t i = a->drawidx;
    if (i < 0 || i >= a->act->nframes) i = 0;
    return &frames_of(a)[i];
}

int32_t anim_elem_no(const Anim *a, int32_t time)
{
    if (!a->act || a->act->nframes == 0) return 0;
    const AirFrame *fr = frames_of(a);
    int32_t nf = a->act->nframes, ls = a->act->loopstart;
    int32_t i = a->curelem, oldt = 0;
    long guard = 0;
    if (time <= 0) {
        int loop = 0;
        time += a->curelemtime;
        for (;;) {
            if (time >= 0) return i + 1;
            i--;
            if (i < 0 || (a->curelem >= ls && i < ls)) {
                if (time == oldt) break;
                oldt = time;
                loop = 1;
                i = nf - 1;
            }
            time += max0(fr[i].time);
            if (loop && i == nf - 1 && fr[i].time == -1) return i + 1;
            if (++guard > 1000000) break;
        }
    } else {
        time += a->curelemtime;
        for (;;) {
            time -= max0(fr[i].time);
            if (time < 0 || (i == nf - 1 && fr[i].time == -1)) return i + 1;
            i++;
            if (i >= nf) {
                if (time == oldt) break;
                oldt = time;
                i = ls;
            }
            if (++guard > 1000000) break;
        }
    }
    return nf;
}

int32_t anim_length(const Anim *a)
{
    if (!a->act || a->act->nframes == 0) return 0;
    const AirFrame *fr = frames_of(a);
    int32_t len = 0;
    for (int32_t i = 0; i < a->act->nframes; i++) {
        if (fr[i].time == -1) len += 1;
        else if (fr[i].time > 0) len += fr[i].time;
    }
    return len;
}
