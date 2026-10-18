/* fight.c - the fight engine: character loading, trigger context, state controllers, physics, hits.
 * The order of one tick follows Ikemen GO: inputs -> states -3/-2/-1 -> (buffered ChangeState) -> current
 * state -> physics -> landing -> hits -> state time++ -> animation. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <ctype.h>
#include "fight.h"
#include "text.h"

#define HB_S 1                  /* attacker state-type bits used in a HitDef */
#define HB_C 2
#define HB_A 4
static int stbit(int st) { return st >= 1 && st <= 4 ? 1 << (st - 1) : 0; }
#define MAX_STATE_CHAIN 32      /* ChangeState hops allowed in one tick (guards against A->B->A loops) */

/* ============================================================================================ loading */
static int file_exists(const char *p)
{
    FILE *f = fopen(p, "rb");
    if (!f) return 0;
    fclose(f);
    return 1;
}

static void join_rel(char *out, size_t n, const char *dir, const char *rel_in)
{
    char rel[IK_PATH_LEN];
    out[0] = 0;
    if (content_safe_rel(rel_in, rel, sizeof rel) != 0) return;
    content_find_ci(dir, rel, out, n);
}

static void note(CharData *cd, const char *what, const char *file)
{
    size_t n = strlen(cd->problem);
    if (n + 4 >= sizeof cd->problem) return;
    const char *base = file ? strrchr(file, '/') : NULL;
    base = base ? base + 1 : file;
    snprintf(cd->problem + n, sizeof cd->problem - n, "%s%s%s%s", n ? "; " : "", what, base ? " " : "", base ? base : "");
}

static int has_ext_ci(const char *s, const char *ext)
{
    size_t a = strlen(s), b = strlen(ext);
    return a >= b && strcasecmp(s + a - b, ext) == 0;
}

int chardata_load(CharData *cd, const char *def_path, const char *common_dir)
{
    memset(cd, 0, sizeof *cd);
    cns_init(&cd->cns);
    DefFile df;
    if (def_load(&df, def_path) != 0 || !df.e) { note(cd, "cannot read", def_path); return -1; }
    snprintf(cd->dir, sizeof cd->dir, "%s", def_path);
    char *sl = strrchr(cd->dir, '/');
    if (sl) *sl = 0; else strcpy(cd->dir, ".");
    const char *nm = def_get(&df, "Info", "displayname");
    if (!nm || !*nm) nm = def_get(&df, "Info", "name");
    snprintf(cd->name, sizeof cd->name, "%s", nm && *nm ? nm : "?");

    char path[IK_PATH_LEN];
    const char *v;
    cd->coord_w = 320;
    if ((v = def_get(&df, "Info", "localcoord")) && atoi(v) >= 64 && atoi(v) <= 8192) cd->coord_w = atoi(v);

    /* sprites */
    v = def_get(&df, "Files", "sprite");
    if (v && *v) { join_rel(path, sizeof path, cd->dir, v); if (path[0] && sff_load(path, &cd->sff) == 0) cd->sff_ok = 1; }
    if (!cd->sff_ok) { cd->warnings++; note(cd, "no sprites:", v); }

    /* animations */
    v = def_get(&df, "Files", "anim");
    int air_ok = 0;
    if (v && *v) { join_rel(path, sizeof path, cd->dir, v); if (path[0] && air_load(path, &cd->air) == 0) air_ok = 1; }
    if (!air_ok) { cd->warnings++; note(cd, "no animations:", v); memset(&cd->air, 0, sizeof cd->air); }

    /* commands: the shared common.cmd first, then the character's own */
    snprintf(path, sizeof path, "%s/common.cmd", common_dir);
    memset(&cd->cmd, 0, sizeof cd->cmd);
    {
        size_t len; char *t = text_load(path, CMD_MAX_FILE, &len);
        if (t) { cmd_parse_append(t, len, &cd->cmd); free(t); }
    }
    const char *cmdf = def_get(&df, "Files", "cmd");
    char cmdpath[IK_PATH_LEN] = "";
    if (cmdf && *cmdf) {
        join_rel(cmdpath, sizeof cmdpath, cd->dir, cmdf);
        size_t len; char *t = cmdpath[0] ? text_load(cmdpath, CMD_MAX_FILE, &len) : NULL;
        if (t) { cmd_parse_append(t, len, &cd->cmd); free(t); } else { cd->warnings++; note(cd, "no commands:", cmdf); }
    }

    /* states: common1 -> cmd file's own states -> st files -> cns */
    snprintf(path, sizeof path, "%s/common1.cns", common_dir);
    {
        const char *sc = def_get(&df, "Files", "stcommon");
        char own[IK_PATH_LEN] = "";
        if (sc && *sc) join_rel(own, sizeof own, cd->dir, sc);
        if (own[0] && file_exists(own)) cns_load(own, &cd->cns, 1);       /* character ships its own common states */
        else if (cns_load(path, &cd->cns, 1) != 0) { cd->warnings++; note(cd, "missing common state file", NULL); }
    }
    if (cmdpath[0]) cns_load(cmdpath, &cd->cns, 1);
    char done[12][IK_PATH_LEN]; int ndone = 0;
    const char *keys[12] = { "st", "st0", "st1", "st2", "st3", "st4", "st5", "st6", "st7", "st8", "st9", "cns" };
    for (int i = 0; i < 12; i++) {
        v = def_get(&df, "Files", keys[i]);
        if (!v || !*v) continue;
        join_rel(path, sizeof path, cd->dir, v);
        if (!path[0]) continue;
        int dup = 0;
        for (int k = 0; k < ndone; k++) if (!strcmp(done[k], path)) dup = 1;
        if (dup) continue;
        snprintf(done[ndone++], IK_PATH_LEN, "%s", path);
        if (has_ext_ci(v, ".zss")) { cd->warnings++; note(cd, "Ikemen-only ZSS states not supported:", v); continue; }
        if (cns_load(path, &cd->cns, 1) != 0) { cd->warnings++; note(cd, "cannot read", v); }
    }
    if (cd->cns.truncated) note(cd, "state files too big, partly loaded", NULL);
    def_free(&df);
    cns_finish(&cd->cns);
    cd->ctrl_base = calloc((size_t)cd->cns.ns + 1, sizeof(uint32_t));
    if (!cd->ctrl_base) return -3;
    for (int i = 0; i < cd->cns.ns; i++) cd->ctrl_base[i + 1] = cd->ctrl_base[i] + (uint32_t)cd->cns.s[i].nc;
    cd->nctrl = cd->ctrl_base[cd->cns.ns];
    if (!cns_state(&cd->cns, 0)) { note(cd, "no state 0 anywhere", NULL); return -2; }                              /* nothing to stand in */
    return 0;
}

void chardata_free(CharData *cd)
{
    air_free(&cd->air); cns_free(&cd->cns); cmd_free(&cd->cmd); free(cd->ctrl_base);
    if (cd->sff_ok) sff_free(&cd->sff);
    memset(cd, 0, sizeof *cd);
}

/* ============================================================================================ helpers */
static Player *opp(const Player *p) { return &p->f->p[p->idx ^ 1]; }
static int rnd(Fight *f) { f->rng = (int)((uint32_t)f->rng * 1103515245u + 12345u); return (int)(((uint32_t)f->rng >> 16) & 0x7fff); }
static Val vI(int32_t i) { return val_int(i); }
static Val vF(double x) { return val_flt(x); }
static double cconst(const Player *p, const char *name, double dflt);

static const AirFrame *cur_frame(const Player *p) { return anim_frame(&p->anim); }

/* facing-aware distance in front of p (positive = opponent is in front) */
static float dist_x(const Player *p) { Player *o = opp(p); return (o->x - p->x) * (float)p->facing; }

static float width_front(const Player *p)
{
    return p->statetype == ST_A ? cconst(p, "size.air.front", 12) : cconst(p, "size.ground.front", 16);
}
static float width_back(const Player *p)
{
    return p->statetype == ST_A ? cconst(p, "size.air.back", 12) : cconst(p, "size.ground.back", 15);
}

static int has_anim(const Player *p, int no) { return air_find(&p->cd->air, no) != NULL; }

/* ============================================================================================ trigger context */
static int is_cmd(const Player *p, const char *n) { return cmdstate_active(&p->cs, n); }

static Val ghv(const Player *p, const char *s)
{
    if (!strcmp(s, "xvel")) return vF(p->gh_xvel);
    if (!strcmp(s, "yvel")) return vF(p->gh_yvel);
    if (!strcmp(s, "hittime")) return vI(p->hit_time);
    if (!strcmp(s, "slidetime")) return vI(p->gh_slide);
    if (!strcmp(s, "damage")) return vI(p->gh_damage);
    if (!strcmp(s, "animtype") || !strcmp(s, "air.animtype") || !strcmp(s, "fall.animtype")) return vI(p->gh_animtype);
    if (!strcmp(s, "guarded")) return vI(p->gh_guarded);
    if (!strcmp(s, "fall")) return vI(p->fall);
    if (!strcmp(s, "hitshaketime")) return vI(p->shake);
    if (!strcmp(s, "ctrltime")) return vI(p->hit_time);
    if (!strcmp(s, "fall.xvel")) return vF(p->gh_fvx);
    if (!strcmp(s, "fall.yvel")) return vF(p->gh_fvy);
    return vI(0);                           /* isbound, fall.recover, hitcount, ... */
}

static const struct { const char *n; double v; } CONST_DEFAULTS[] = {
    {"data.life", 1000}, {"data.power", 3000}, {"data.attack", 100}, {"data.defence", 100},
    {"size.xscale", 1}, {"size.yscale", 1}, {"size.height", 60},
    {"size.ground.back", 15}, {"size.ground.front", 16}, {"size.air.back", 12}, {"size.air.front", 12},
    {"movement.yaccel", 0.44}, {"movement.stand.friction", 0.85}, {"movement.crouch.friction", 0.82},
    {"movement.stand.friction.threshold", 2}, {"movement.crouch.friction.threshold", 0.05},
    {"movement.jump.changeanim.threshold", -4}, {"movement.air.gethit.groundlevel", 25},
    {"movement.air.gethit.groundrecover.ground.threshold", -20}, {"movement.air.gethit.groundrecover.groundlevel", 10},
    {"movement.air.gethit.airrecover.threshold", -1}, {"movement.air.gethit.airrecover.yaccel", 0.35},
    {"movement.air.gethit.trip.groundlevel", 15}, {"movement.down.bounce.offset.x", 0}, {"movement.down.bounce.offset.y", 32},
    {"movement.down.bounce.yaccel", 0.4}, {"movement.down.bounce.groundlevel", 12}, {"movement.down.friction.threshold", 0.05},
};

static double cconst(const Player *p, const char *name, double dflt)
{
    for (int i = 0; i < p->cd->cns.nk; i++) if (!strcmp(p->cd->cns.k[i].name, name)) return p->cd->cns.k[i].v;
    for (unsigned i = 0; i < sizeof CONST_DEFAULTS / sizeof *CONST_DEFAULTS; i++) if (!strcmp(CONST_DEFAULTS[i].n, name)) return CONST_DEFAULTS[i].v;
    return dflt;
}

static int hd_encode(const Player *p)
{
    const HitDef *h = &p->hd;
    if (!h->on) return 0;
    int at = 0;
    for (int c = 0; c < 3; c++) for (int t = 0; t < 3; t++) if ((h->cls & (1 << c)) && (h->typ & (1 << t))) at |= 1 << (c * 3 + t);
    return h->st | (at << 8);
}

static int ai(const Val *a, int k) { return (int)a[k].d; }

static Val pget(void *ud, int id, const Val *a, int n, const char *s)
{
    Player *p = (Player *)ud;
    Fight *f = p->f;
    Player *q = opp(p);
    switch (id) {
    case TG_ALIVE: return vI(p->alive);
    case TG_ANIM: return vI(p->animno);
    case TG_ANIMTIME: return vI(anim_time(&p->anim));
    case TG_CTRL: return vI(p->ctrl);
    case TG_STATENO: return vI(p->stateno);
    case TG_PREVSTATENO: return vI(p->prev_stateno);
    case TG_TIME: case TG_STATETIME: return vI(p->time);
    case TG_STATETYPE: return vI(p->statetype);
    case TG_MOVETYPE: return vI(p->movetype);
    case TG_PHYSICS: return vI(p->physics);
    case TG_LIFE: return vI(p->life);
    case TG_LIFEMAX: return vI(p->lifemax);
    case TG_POWER: return vI(p->power);
    case TG_POWERMAX: return vI(p->powermax);
    case TG_FACING: return vI(p->facing);
    case TG_RANDOM: return vI(rnd(f) % 1000);
    case TG_MOVECONTACT: return vI(p->movecontact);
    case TG_MOVEHIT: return vI(p->movehit);
    case TG_MOVEGUARDED: return vI(p->moveguarded);
    case TG_HITOVER: return vI(p->hit_time <= 0 && p->shake <= 0);
    case TG_HITSHAKEOVER: return vI(p->shake <= 0);
    case TG_HITPAUSETIME: return vI(p->pause + p->shake);
    case TG_HITFALL: case TG_FALL: return vI(p->fall);
    case TG_ROUNDSTATE: return vI(2);
    case TG_ROUNDNO: case TG_MATCHNO: return vI(1);
    case TG_GAMETIME: return vI(f->tick);
    case TG_NUMTARGET: return vI(p->ntarget);
    case TG_TEAMSIDE: return vI(p->idx + 1);
    case TG_ID: return vI(1000 + p->idx);
    case TG_NUMENEMY: case TG_NUMPLAYERS: return vI(p->f ? (id == TG_NUMENEMY ? 1 : 2) : 0);
    case TG_P2LIFE: return vI(q->life);
    case TG_P2STATENO: return vI(q->stateno);
    case TG_P2MOVETYPE: return vI(q->movetype);
    case TG_P2STATETYPE: return vI(q->statetype);
    case TG_P2ANIM: return vI(q->animno);
    case TG_POSX: return vF(p->x);
    case TG_POSY: return vF(p->y);
    case TG_VELX: return vF(p->vx);
    case TG_VELY: return vF(p->vy);
    case TG_P2DISTX: return vF(dist_x(p));
    case TG_P2DISTY: return vF(q->y - p->y);
    case TG_P2BODYDISTX: return vF(dist_x(p) - width_front(p) - width_front(q));
    case TG_P2BODYDISTY: return vF(q->y - p->y);
    case TG_SCREENPOSX: return vF(p->x - f->camx + (FIGHT_LOCAL_W * f->cs / 2));
    case TG_SCREENPOSY: return vF(p->y);
    case TG_BACKEDGEDIST: return vF(p->facing > 0 ? (p->x - (f->camx - (FIGHT_LOCAL_W * f->cs / 2))) : ((f->camx + (FIGHT_LOCAL_W * f->cs / 2)) - p->x));
    case TG_FRONTEDGEDIST: return vF(p->facing > 0 ? ((f->camx + (FIGHT_LOCAL_W * f->cs / 2)) - p->x) : (p->x - (f->camx - (FIGHT_LOCAL_W * f->cs / 2))));
    case TG_BACKEDGEBODYDIST: {
        double d = p->facing > 0 ? (p->x - (f->camx - (FIGHT_LOCAL_W * f->cs / 2))) : ((f->camx + (FIGHT_LOCAL_W * f->cs / 2)) - p->x);
        return vF(d - width_back(p)); }
    case TG_FRONTEDGEBODYDIST: {
        double d = p->facing > 0 ? ((f->camx + (FIGHT_LOCAL_W * f->cs / 2)) - p->x) : (p->x - (f->camx - (FIGHT_LOCAL_W * f->cs / 2)));
        return vF(d - width_front(p)); }
    case TG_ANIMEXIST: case TG_SELFANIMEXIST: return vI(n > 0 && has_anim(p, ai(a, 0)));
    case TG_ANIMELEMTIME: return vI(anim_elem_time(&p->anim, n > 0 ? ai(a, 0) : 1));
    case TG_ANIMELEMNO: return vI(anim_elem_no(&p->anim, n > 0 ? ai(a, 0) : 0));
    case TG_STATENOEXIST: case TG_SELFSTATENOEXIST: return vI(n > 0 && cns_state(&p->cd->cns, ai(a, 0)) != NULL);
    case TG_VAR: { int i = ai(a, 0); return (i >= 0 && i < FIGHT_NVAR) ? vI(p->var[i]) : val_bad(); }
    case TG_FVAR: { int i = ai(a, 0); return (i >= 0 && i < FIGHT_NFVAR) ? vF(p->fvar[i]) : val_bad(); }
    case TG_SYSVAR: { int i = ai(a, 0); return (i >= 0 && i < 5) ? vI(p->sysvar[i]) : val_bad(); }
    case TG_SYSFVAR: { int i = ai(a, 0); return (i >= 0 && i < 5) ? vF(p->sysfvar[i]) : val_bad(); }
    case TG_CONST: return vF(cconst(p, s, 0));
    case TG_CONST240P: case TG_CONST480P: case TG_CONST720P: return n > 0 ? a[0] : vI(0);
    case TG_GETHITVARFALL: return ghv(p, s ? s : "");
    case TG_COMMAND: return vI(s ? is_cmd(p, s) : 0);
    case TG_HITDEFATTR: return vI(hd_encode(p));
    default: break;
    }
    return vI(0);
}

static int pset(void *ud, int kind, int idx, Val v)
{
    Player *p = (Player *)ud;
    if (kind == AS_VAR && idx >= 0 && idx < FIGHT_NVAR) p->var[idx] = (int32_t)v.d;
    else if (kind == AS_FVAR && idx >= 0 && idx < FIGHT_NFVAR) p->fvar[idx] = (float)v.d;
    else if (kind == AS_SYSVAR && idx >= 0 && idx < 5) p->sysvar[idx] = (int32_t)v.d;
    else if (kind == AS_SYSFVAR && idx >= 0 && idx < 5) p->sysfvar[idx] = (float)v.d;
    else return 0;
    return 1;
}

static void *predirect(void *ud, int kind, int arg)
{
    Player *p = (Player *)ud;
    (void)arg;
    switch (kind) {
    case RD_ROOT: case RD_SELF: return p;
    case RD_P2: case RD_ENEMY: case RD_ENEMYNEAR: return opp(p);
    case RD_TARGET: return p->ntarget > 0 ? (void *)opp(p) : NULL;
    default: return NULL;
    }
}

static ExprCtx make_ctx(Player *p) { ExprCtx c; c.ud = p; c.get = pget; c.set = pset; c.redir = predirect; return c; }

/* ============================================================================================ states */
static Val peval(Player *p, const Expr *e)
{
    ExprCtx c = make_ctx(p);
    return expr_eval(e, &c);
}
static int32_t pint(Player *p, const Expr *e, int32_t d) { if (!e) return d; Val v = peval(p, e); return v.bad ? d : (int32_t)v.d; }
static float pflt(Player *p, const Expr *e, float d) { if (!e) return d; Val v = peval(p, e); return v.bad ? d : (float)v.d; }

/* parameter lookup helpers: expression idx of the named parameter, or the default */
static const Param *prm(const Ctrl *k, const char *name) { return ctrl_param(k, name); }
static int32_t P_int(Player *p, const Ctrl *k, const char *name, int idx, int32_t d)
{
    const Param *a = prm(k, name);
    if (!a || idx >= a->ne || !a->e[idx]) return d;
    return pint(p, a->e[idx], d);
}
static float P_flt(Player *p, const Ctrl *k, const char *name, int idx, float d)
{
    const Param *a = prm(k, name);
    if (!a || idx >= a->ne || !a->e[idx]) return d;
    return pflt(p, a->e[idx], d);
}
static int P_has(Player *p, const Ctrl *k, const char *name, int idx)
{
    (void)p;
    const Param *a = prm(k, name);
    return a && idx < a->ne && a->e[idx];
}
static const char *P_raw(Player *p, const Ctrl *k, const char *name)
{
    (void)p;
    const Param *a = prm(k, name);
    return a ? a->raw : NULL;
}

/* controller fires when every triggerall is true and at least one numbered group is entirely true */
static int ctrl_fires(Player *p, const Ctrl *k)
{
    ExprCtx c = make_ctx(p);
    for (int i = 0; i < k->nall; i++) if (!val_true(expr_eval(k->all[i], &c))) return 0;
    for (int g = 0; g < k->ngrp; g++) {
        if (k->grp[g].n == 0) continue;
        int ok = 1;
        for (int i = 0; i < k->grp[g].n && ok; i++) if (!val_true(expr_eval(k->grp[g].e[i], &c))) ok = 0;
        if (ok) return 1;
    }
    return 0;
}

static void set_anim(Player *p, int no, int elem, int elemtime)
{
    p->animno = no;
    anim_start(&p->anim, &p->cd->air, air_find(&p->cd->air, no), elem, elemtime);
}

/* Enters a state (Ikemen: Char.changeState). Returns 0 if the state does not exist (nothing happens). */
static int enter_state(Player *p, int no, int ctrl, int anim)
{
    const StateDef *d = cns_state(&p->cd->cns, no);
    if (!d) { p->f->missing_state++; return 0; }
    p->prev_stateno = p->stateno;
    p->prev_type = p->statetype;
    p->stateno = no;
    p->time = 0;
    p->serial++;
    if (!d->hitdefpersist) p->hd.on = 0;
    if (!d->movehitpersist) p->movecontact = p->movehit = p->moveguarded = 0;
    if (d->type) p->statetype = d->type;
    if (d->movetype) p->movetype = d->movetype;
    if (d->physics) p->physics = d->physics;
    if (d->anim) set_anim(p, pint(p, d->anim, 0), 0, 0);
    if (d->velx) p->vx = pflt(p, d->velx, p->vx);
    if (d->vely) p->vy = pflt(p, d->vely, p->vy);
    if (d->ctrl) p->ctrl = pint(p, d->ctrl, 0) != 0;
    if (d->poweradd) { p->power += pint(p, d->poweradd, 0); if (p->power > p->powermax) p->power = p->powermax; if (p->power < 0) p->power = 0; }
    if (d->sprpriority) p->sprpriority = pint(p, d->sprpriority, 0);
    if (d->facep2) { Player *q = opp(p); if ((q->x - p->x) * p->facing < 0) p->facing = -p->facing; }
    if (ctrl >= 0) p->ctrl = ctrl != 0;
    if (anim >= 0) set_anim(p, anim, 0, 0);
    return 1;
}

/* ============================================================================================ controllers */
enum { RUN_OK = 0, RUN_STATE_CHANGED = 1 };

static int parse_flags(const char *s, const char *letters)    /* "MAF" -> bit set by position in letters */
{
    int r = 0;
    for (; s && *s; s++) {
        char c = (char)tolower((unsigned char)*s);
        if (c == 'm') r |= 3;                 /* M = H + L */
        else { const char *p = strchr(letters, c); if (p && c) r |= 1 << (p - letters); }
    }
    return r;
}

static int animtype_of(const char *s, int d)
{
    if (!s) return d;
    char b[16]; size_t i = 0;
    for (; s[i] && i < 15; i++) b[i] = (char)tolower((unsigned char)s[i]);
    b[i] = 0;
    if (!strncmp(b, "light", 5)) return 0;
    if (!strncmp(b, "medium", 6)) return 1;
    if (!strncmp(b, "hard", 4)) return 2;
    if (!strncmp(b, "back", 4)) return 3;
    if (!strncmp(b, "up", 2)) return 4;
    if (!strncmp(b, "diagup", 6)) return 5;
    return d;
}

static void do_hitdef(Player *p, const Ctrl *k)
{
    HitDef *h = &p->hd;
    HitDef old = *h;
    int cont = p->ctrl_cont && old.on;           /* same HitDef firing every tick = one activation, not many */
    memset(h, 0, sizeof *h);
    const char *at = P_raw(p, k, "attr");
    h->st = HB_S | HB_C | HB_A; h->cls = 1; h->typ = 1;
    if (at) {
        int st = 0; const char *c = at;
        for (; *c && *c != ','; c++) { char ch = (char)tolower((unsigned char)*c); if (ch == 's') st |= HB_S; else if (ch == 'c') st |= HB_C; else if (ch == 'a') st |= HB_A; }
        if (st) h->st = st;
        h->cls = h->typ = 0;
        while (*c) {
            if (*c == ',' || *c == ' ') { c++; continue; }
            char a = (char)tolower((unsigned char)c[0]), b = (char)tolower((unsigned char)c[1]);
            int kb = a == 'n' ? 1 : a == 's' ? 2 : a == 'h' ? 4 : 0;
            int tb = b == 'a' ? 1 : b == 't' ? 2 : b == 'p' ? 4 : 0;
            h->cls |= kb; h->typ |= tb;
            c += (c[1]) ? 2 : 1;
        }
        if (!h->cls) h->cls = 1;
        if (!h->typ) h->typ = 1;
    }
    const char *hf = P_raw(p, k, "hitflag");
    h->hitflag = hf ? parse_flags(hf, "hlafd") : 3 | 4 | 8;                       /* default MAF */
    const char *gf = P_raw(p, k, "guardflag");
    h->guardflag = gf ? parse_flags(gf, "hla") : 0;
    h->damage = P_int(p, k, "damage", 0, 0);
    h->gdamage = P_int(p, k, "damage", 1, 0);
    h->p1pause = P_int(p, k, "pausetime", 0, 0);
    h->p2shake = P_int(p, k, "pausetime", 1, 0);
    h->gp1pause = P_has(p, k, "guard.pausetime", 0) ? P_int(p, k, "guard.pausetime", 0, 0) : h->p1pause;
    h->gp2shake = P_has(p, k, "guard.pausetime", 1) ? P_int(p, k, "guard.pausetime", 1, 0) : h->p2shake;
    h->animtype = animtype_of(P_raw(p, k, "animtype"), 0);
    h->air_animtype = animtype_of(P_raw(p, k, "air.animtype"), h->animtype);
    const char *gt = P_raw(p, k, "ground.type");
    h->ground_type = (gt && (gt[0] == 'l' || gt[0] == 'L')) ? 1 : 0;
    h->ghittime = P_int(p, k, "ground.hittime", 0, 0);
    h->ahittime = P_int(p, k, "air.hittime", 0, 20);
    h->gghittime = P_has(p, k, "guard.hittime", 0) ? P_int(p, k, "guard.hittime", 0, 0) : h->ghittime;
    h->gvx = P_flt(p, k, "ground.velocity", 0, 0); h->gvy = P_flt(p, k, "ground.velocity", 1, 0);
    h->avx = P_flt(p, k, "air.velocity", 0, 0);    h->avy = P_flt(p, k, "air.velocity", 1, 0);
    h->guardvx = P_has(p, k, "guard.velocity", 0) ? P_flt(p, k, "guard.velocity", 0, 0) : h->gvx;
    h->fall = P_int(p, k, "fall", 0, 0) != 0;
    h->fall_vx = P_flt(p, k, "fall.xvelocity", 0, 0); h->fall_vy = P_flt(p, k, "fall.yvelocity", 0, -4.5f);
    h->numhits = P_int(p, k, "numhits", 0, 1); if (h->numhits < 1) h->numhits = 1;
    h->hits_left = h->numhits;
    h->id = P_int(p, k, "id", 0, 0); h->chainid = P_int(p, k, "chainid", 0, -1); h->nochainid = P_int(p, k, "nochainid", 0, -1);
    h->kill = P_int(p, k, "kill", 0, 1); h->guard_kill = P_int(p, k, "guard.kill", 0, 1);
    h->getpower = P_has(p, k, "getpower", 0) ? P_int(p, k, "getpower", 0, 0) : h->damage * 7 / 10;
    h->givepower = P_has(p, k, "givepower", 0) ? P_int(p, k, "givepower", 0, 0) : h->damage * 6 / 10;
    h->p1stateno = P_int(p, k, "p1stateno", 0, -1); h->p2stateno = P_int(p, k, "p2stateno", 0, -1);
    h->on = 1;
    if (cont) h->hits_left = old.hits_left < h->hits_left ? old.hits_left : h->hits_left;   /* keep a spent hit spent */
}

static void clampvar(Player *p) { if (p->life < 0) p->life = 0; if (p->life > p->lifemax) p->life = p->lifemax; if (p->power < 0) p->power = 0; if (p->power > p->powermax) p->power = p->powermax; }

static int run_ctrl(Fight *f, Player *p, const Ctrl *k)
{
    Player *q = opp(p);
    switch (k->type) {
    case SC_CHANGESTATE: case SC_SELFSTATE: {
        int no = P_int(p, k, "value", 0, p->stateno);
        int ctrl = P_has(p, k, "ctrl", 0) ? (P_int(p, k, "ctrl", 0, 0) != 0) : -1;
        int an = P_has(p, k, "anim", 0) ? P_int(p, k, "anim", 0, 0) : -1;
        p->buf_state = no; p->buf_ctrl = ctrl; p->buf_anim = an;
        return RUN_STATE_CHANGED;
    }
    case SC_CHANGEANIM: case SC_CHANGEANIM2:
        set_anim(p, P_int(p, k, "value", 0, 0), P_int(p, k, "elem", 0, 0) > 1 ? P_int(p, k, "elem", 0, 0) : 0, P_int(p, k, "elemtime", 0, 0));
        break;
    case SC_CTRLSET: p->ctrl = P_int(p, k, "value", 0, 0) != 0; break;
    case SC_VELSET:
        if (P_has(p, k, "x", 0)) p->vx = P_flt(p, k, "x", 0, 0);
        if (P_has(p, k, "y", 0)) p->vy = P_flt(p, k, "y", 0, 0);
        break;
    case SC_VELADD:
        if (P_has(p, k, "x", 0)) p->vx += P_flt(p, k, "x", 0, 0);
        if (P_has(p, k, "y", 0)) p->vy += P_flt(p, k, "y", 0, 0);
        break;
    case SC_VELMUL:
        if (P_has(p, k, "x", 0)) p->vx *= P_flt(p, k, "x", 0, 1);
        if (P_has(p, k, "y", 0)) p->vy *= P_flt(p, k, "y", 0, 1);
        break;
    case SC_POSSET:
        if (P_has(p, k, "x", 0)) p->x = P_flt(p, k, "x", 0, 0);
        if (P_has(p, k, "y", 0)) p->y = P_flt(p, k, "y", 0, 0);
        break;
    case SC_POSADD:
        if (P_has(p, k, "x", 0)) p->x += P_flt(p, k, "x", 0, 0) * (float)p->facing;
        if (P_has(p, k, "y", 0)) p->y += P_flt(p, k, "y", 0, 0);
        break;
    case SC_POSFREEZE: p->posfreeze = P_has(p, k, "value", 0) ? P_int(p, k, "value", 0, 1) != 0 : 1; break;
    case SC_TURN: p->facing = -p->facing; break;
    case SC_STATETYPESET: {
        const char *r;
        if (P_has(p, k, "statetype", 0) || (r = P_raw(p, k, "statetype"))) {
            r = P_raw(p, k, "statetype");
            if (r) { char c = (char)tolower((unsigned char)r[0]); p->statetype = c == 's' ? ST_S : c == 'c' ? ST_C : c == 'a' ? ST_A : c == 'l' ? ST_L : p->statetype; }
        }
        if ((r = P_raw(p, k, "movetype"))) { char c = (char)tolower((unsigned char)r[0]); p->movetype = c == 'i' ? MT_I : c == 'a' ? MT_A : c == 'h' ? MT_H : p->movetype; }
        if ((r = P_raw(p, k, "physics"))) { char c = (char)tolower((unsigned char)r[0]); p->physics = c == 's' ? PH_S : c == 'c' ? PH_C : c == 'a' ? PH_A : c == 'n' ? PH_N : p->physics; }
        break;
    }
    case SC_PHYSICSSET: {
        const char *r = P_raw(p, k, "value");
        if (r) { char c = (char)tolower((unsigned char)r[0]); p->physics = c == 's' ? PH_S : c == 'c' ? PH_C : c == 'a' ? PH_A : c == 'n' ? PH_N : p->physics; }
        break;
    }
    case SC_VARSET: case SC_VARADD: {
        int add = k->type == SC_VARADD;
        for (int i = 0; i < k->npar; i++) {                         /* var(3) = 5  style keys */
            const Param *a = &k->par[i];
            int kind = -1; const char *q = a->name;
            if (!strncmp(q, "var(", 4)) { kind = AS_VAR; q += 4; }
            else if (!strncmp(q, "fvar(", 5)) { kind = AS_FVAR; q += 5; }
            else if (!strncmp(q, "sysvar(", 7)) { kind = AS_SYSVAR; q += 7; }
            else if (!strncmp(q, "sysfvar(", 8)) { kind = AS_SYSFVAR; q += 8; }
            if (kind < 0 || a->ne < 1 || !a->e[0]) continue;
            int32_t idx; if (!text_parse_int(q, &idx)) continue;
            Val v = peval(p, a->e[0]);
            if (v.bad) continue;
            if (add) { Val ix = vI(idx); int tg = kind == AS_VAR ? TG_VAR : kind == AS_FVAR ? TG_FVAR : kind == AS_SYSVAR ? TG_SYSVAR : TG_SYSFVAR; Val old = pget(p, tg, &ix, 1, NULL); if (old.bad) continue; v = (old.isf || v.isf) ? vF(old.d + v.d) : vI((int32_t)(old.d + v.d)); }
            pset(p, kind, idx, v);
        }
        {   /* v = 3, value = 5  style */
            int idx_i = P_has(p, k, "v", 0) ? P_int(p, k, "v", 0, -1) : -1;
            int idx_f = P_has(p, k, "fv", 0) ? P_int(p, k, "fv", 0, -1) : -1;
            int sidx = P_has(p, k, "sysvar", 0) ? P_int(p, k, "sysvar", 0, -1) : -1;
            int sfidx = P_has(p, k, "sysfvar", 0) ? P_int(p, k, "sysfvar", 0, -1) : -1;
            const Param *vp = prm(k, "value");
            Val v = (vp && vp->ne > 0 && vp->e[0]) ? peval(p, vp->e[0]) : vI(0);
            if (v.bad) break;
            if (idx_i >= 0 && idx_i < FIGHT_NVAR) { int32_t nv = (int32_t)v.d; p->var[idx_i] = add ? p->var[idx_i] + nv : nv; }
            if (idx_f >= 0 && idx_f < FIGHT_NFVAR) { float nv = (float)v.d; p->fvar[idx_f] = add ? p->fvar[idx_f] + nv : nv; }
            if (sidx >= 0 && sidx < 5) { int32_t nv = (int32_t)v.d; p->sysvar[sidx] = add ? p->sysvar[sidx] + nv : nv; }
            if (sfidx >= 0 && sfidx < 5) { float nv = (float)v.d; p->sysfvar[sfidx] = add ? p->sysfvar[sfidx] + nv : nv; }
        }
        break;
    }
    case SC_VARRANDOM: {
        int idx = P_int(p, k, "v", 0, -1), lo = 0, hi = 1000;
        if (P_has(p, k, "range", 0)) { if (P_has(p, k, "range", 1)) { lo = P_int(p, k, "range", 0, 0); hi = P_int(p, k, "range", 1, 1000); } else hi = P_int(p, k, "range", 0, 1000); }
        if (hi < lo) hi = lo;
        if (idx >= 0 && idx < FIGHT_NVAR) p->var[idx] = lo + rnd(f) % (hi - lo + 1);
        break;
    }
    case SC_VARRANGESET: {
        int first = P_int(p, k, "first", 0, 0), last = P_int(p, k, "last", 0, FIGHT_NVAR - 1), val = P_int(p, k, "value", 0, 0);
        for (int i = first < 0 ? 0 : first; i <= last && i < FIGHT_NVAR; i++) p->var[i] = val;
        break;
    }
    case SC_HITDEF: do_hitdef(p, k); break;
    case SC_LIFEADD: p->life += P_int(p, k, "value", 0, 0); clampvar(p); if (p->life <= 0) p->alive = 0; break;
    case SC_LIFESET: p->life = P_int(p, k, "value", 0, 0); clampvar(p); p->alive = p->life > 0; break;
    case SC_POWERADD: p->power += P_int(p, k, "value", 0, 0); clampvar(p); break;
    case SC_POWERSET: p->power = P_int(p, k, "value", 0, 0); clampvar(p); break;
    case SC_SPRPRIORITY: p->sprpriority = P_int(p, k, "value", 0, 0); break;
    case SC_GRAVITY: p->vy += cconst(p, "movement.yaccel", 0.44f); break;
    case SC_HITVELSET: break;
    case SC_TARGETSTATE: if (p->ntarget > 0) { Player *t = q; int no = P_int(p, k, "value", 0, -1); if (no >= 0) { /* target states belong to the target's own state files unless they are common */ enter_state(t, no, -1, -1); } } break;
    case SC_TARGETLIFEADD: if (p->ntarget > 0) { q->life += P_int(p, k, "value", 0, 0); clampvar(q); if (q->life <= 0) q->alive = 0; } break;
    case SC_TARGETFACING: if (p->ntarget > 0) { int v = P_int(p, k, "value", 0, 0); if (v) q->facing = v > 0 ? p->facing : -p->facing; } break;
    case SC_TARGETVELSET: if (p->ntarget > 0) { if (P_has(p, k, "x", 0)) q->vx = P_flt(p, k, "x", 0, 0); if (P_has(p, k, "y", 0)) q->vy = P_flt(p, k, "y", 0, 0); } break;
    case SC_TARGETVELADD: if (p->ntarget > 0) { if (P_has(p, k, "x", 0)) q->vx += P_flt(p, k, "x", 0, 0); if (P_has(p, k, "y", 0)) q->vy += P_flt(p, k, "y", 0, 0); } break;
    case SC_TARGETPOWERADD: if (p->ntarget > 0) { q->power += P_int(p, k, "value", 0, 0); clampvar(q); } break;
    case SC_TARGETBIND: if (p->ntarget > 0) { float px = P_flt(p, k, "pos", 0, 0), py = P_flt(p, k, "pos", 1, 0); q->x = p->x + px * (float)p->facing; q->y = p->y + py; } break;
    case SC_ASSERTSPECIAL: case SC_NULL:
    /* accepted and deliberately ignored: visual / sound / bookkeeping only */
    case SC_PLAYSND: case SC_STOPSND: case SC_WIDTH: case SC_SCREENBOUND: case SC_AFTERIMAGE: case SC_AFTERIMAGETIME:
    case SC_PALFX: case SC_ALLPALFX: case SC_PAUSE: case SC_SUPERPAUSE: case SC_EXPLOD: case SC_REMOVEEXPLOD:
    case SC_MODIFYEXPLOD: case SC_ENVSHAKE: case SC_ENVCOLOR: case SC_NOTHITBY: case SC_HITBY: case SC_HITOVERRIDE:
    case SC_REVERSALDEF: case SC_MAKEDUST: case SC_OFFSET: case SC_PLAYERPUSH: case SC_TRANS: case SC_REMAPPAL:
    case SC_DEFENCEMULSET: case SC_FALLENVSHAKE: case SC_HITFALLDAMAGE: case SC_HITFALLSET: case SC_HITFALLVEL:
    case SC_MOVEHITRESET: case SC_HITADD: case SC_TARGETDROP:
        break;
    default:
        f->unimpl[k->type >= 0 && k->type < SC_COUNT ? k->type : 0]++;
        break;
    }
    return RUN_OK;
}

/* Runs every controller of one state. Returns 1 if a ChangeState was requested (stop processing the state). */
static int run_state_block(Fight *f, Player *p, int stateno, int in_hitpause)
{
    const StateDef *d = cns_state(&p->cd->cns, stateno);
    if (!d) return 0;
    uint32_t base = p->cd->ctrl_base[d - p->cd->cns.s];
    for (int i = 0; i < d->nc; i++) {
        const Ctrl *k = &d->c[i];
        uint32_t fi = base + (uint32_t)i;
        if (in_hitpause && !k->ignorehitpause) continue;
        /* persistent: 0 = once per state entry, 1 = every time, N = at most every N ticks */
        if (k->persistent != 1 && fi < p->ncfire && p->fire_serial[fi] == p->serial + 1) {
            if (k->persistent == 0) continue;
            if (p->time - p->fire_time[fi] < k->persistent) continue;
        }
        if (!ctrl_fires(p, k)) continue;
        p->ctrl_cont = fi < p->ncfire && p->fire_serial[fi] == p->serial + 1 && p->time - p->fire_time[fi] <= 1;
        if (fi < p->ncfire) { p->fire_serial[fi] = p->serial + 1; p->fire_time[fi] = p->time; }
        if (run_ctrl(f, p, k) == RUN_STATE_CHANGED) return 1;
    }
    return 0;
}

/* Applies a buffered ChangeState. */
static int apply_buffered(Player *p)
{
    if (p->buf_state == INT32_MIN) return 0;
    int no = p->buf_state, ctrl = p->buf_ctrl, an = p->buf_anim;
    p->buf_state = INT32_MIN;
    return enter_state(p, no, ctrl, an);
}

/* ============================================================================================ physics */
static float clampf(float v, float lo, float hi) { return v < lo ? lo : v > hi ? hi : v; }

static void physics(Fight *f, Player *p)
{
    (void)f;
    if (!p->posfreeze) { p->x += p->vx * (float)p->facing; p->y += p->vy; }
    p->posfreeze = 0;
    switch (p->physics) {
    case PH_S: p->vx *= cconst(p, "movement.stand.friction", 0.85f); break;
    case PH_C: p->vx *= cconst(p, "movement.crouch.friction", 0.82f); break;
    case PH_A: p->vy += cconst(p, "movement.yaccel", 0.44f); break;
    default: break;
    }
    if ((p->physics == PH_S || p->physics == PH_C) && p->y > 0) p->y = 0;
    /* landing: falling, at or below the floor. Hit-reaction states handle their own landing (Ikemen exempts
     * state 105, the back dash, the same way). */
    if (p->physics == PH_A && p->vy > 0 && p->y >= 0 && p->stateno != 105 && p->movetype != MT_H) {
        p->y = 0; p->vy = 0;
        enter_state(p, 52, 0, -1);
    }
}

/* ============================================================================================ one player */
static uint16_t to_rel(const Player *p, uint16_t raw)
{
    uint16_t k = 0;
    int fwd = p->facing > 0 ? FI_RIGHT : FI_LEFT, back = p->facing > 0 ? FI_LEFT : FI_RIGHT;
    if (raw & FI_UP) k |= CK_UP;
    if (raw & FI_DOWN) k |= CK_DOWN;
    if (raw & fwd) k |= CK_FWD;
    if (raw & back) k |= CK_BACK;
    if ((raw & FI_LEFT) && (raw & FI_RIGHT)) k &= (uint16_t)~(CK_FWD | CK_BACK);
    if ((raw & FI_UP) && (raw & FI_DOWN)) k &= (uint16_t)~(CK_UP | CK_DOWN);
    if (raw & FI_A) k |= CK_A;
    if (raw & FI_B) k |= CK_B;
    if (raw & FI_C) k |= CK_C;
    if (raw & FI_X) k |= CK_X;
    if (raw & FI_Y) k |= CK_Y;
    if (raw & FI_Z) k |= CK_Z;
    if (raw & FI_START) k |= CK_S;
    return k;
}

static int is_paused(const Player *p) { return p->pause > 0 || p->shake > 0; }

static void player_action(Fight *f, Player *p)
{
    int paused = is_paused(p);
    p->buf_state = INT32_MIN;
    for (int no = -3; no <= -1; no++) run_state_block(f, p, no, paused);
    apply_buffered(p);
    for (int hops = 0; hops < MAX_STATE_CHAIN; hops++) {
        if (!run_state_block(f, p, p->stateno, paused)) break;
        if (!apply_buffered(p)) break;
    }
    if (!paused) physics(f, p);
}

/* paused0: whether the player was already paused when this tick began. A hit made during this tick must not freeze
 * the tick it happened in: its time and animation still advance, otherwise "Time = 0" / "AnimElem = n" attack
 * triggers would still be true when the pause ends and the HitDef would fire (and hit) again. */
static void player_post(Fight *f, Player *p, int paused0)
{
    (void)f;
    if (!paused0) {
        p->time++;
        anim_tick(&p->anim);
        if (p->movecontact > 0) p->movecontact++;
        if (p->movehit > 0) p->movehit++;
        if (p->moveguarded > 0) p->moveguarded++;
    } else if (p->pause > 0) p->pause--;
    if (p->shake > 0) { if (paused0) p->shake--; }
    else if (p->hit_time > 0) p->hit_time--;
    if (p->life <= 0) p->alive = 0;
}

/* ============================================================================================ hits */
typedef struct { float x1, y1, x2, y2; } Rect;

static Rect world_box(const Player *p, const AirFrame *fr, const Box *b)
{
    float d = (float)(p->facing * fr->hflip);
    Rect r;
    float xa = p->x + d * b->x1 * p->xs, xb = p->x + d * b->x2 * p->xs;
    r.x1 = xa < xb ? xa : xb; r.x2 = xa < xb ? xb : xa;
    r.y1 = p->y + b->y1 * p->ys; r.y2 = p->y + b->y2 * p->ys;
    return r;
}

static int boxes_touch(const Player *a, const Player *d)
{
    const AirFrame *fa = cur_frame(a), *fd = cur_frame(d);
    if (!fa || !fd || !fa->nclsn1 || !fd->nclsn2) return 0;
    for (int i = 0; i < fa->nclsn1; i++) {
        Rect ra = world_box(a, fa, &a->cd->air.boxes[fa->clsn1 + (uint32_t)i]);
        for (int j = 0; j < fd->nclsn2; j++) {
            Rect rd = world_box(d, fd, &d->cd->air.boxes[fd->clsn2 + (uint32_t)j]);
            if (ra.x1 < rd.x2 && rd.x1 < ra.x2 && ra.y1 < rd.y2 && rd.y1 < ra.y2) return 1;
        }
    }
    return 0;
}

static int can_hit(const Player *a, const Player *d)
{
    const HitDef *h = &a->hd;
    if (!h->on || h->hits_left <= 0 || a->movetype != MT_A || a->pause > 0 || a->shake > 0) return 0;
    if (!(h->st & stbit(a->statetype))) return 0;
    int need;                                    /* hitflag bit the defender's situation requires */
    if (d->statetype == ST_L) need = 16;         /* D */
    else if (d->statetype == ST_A) need = d->fall ? 8 : 4;
    else need = 3;
    if (!(h->hitflag & need)) {
        if (!(d->statetype == ST_A && d->fall && (h->hitflag & 4))) return 0;   /* falling players also count as air */
        if (!(h->hitflag & 8)) return 0;
    }
    return boxes_touch(a, d);
}

static int guarding(const Player *d, const HitDef *h)
{
    if (!h->guardflag) return 0;
    if (d->statetype == ST_L) return 0;
    int in_guard = d->stateno >= 150 && d->stateno <= 155;
    if (!in_guard && (d->movetype == MT_H || !d->ctrl)) return 0;
    int back = d->facing > 0 ? FI_LEFT : FI_RIGHT;
    if (!(d->raw & back) && !in_guard) return 0;
    if (!(d->raw & back)) return 0;
    if (d->statetype == ST_A) return (h->guardflag & 4) != 0;
    return (h->guardflag & 3) != 0;
}

static void apply_hit(Fight *f, Player *a, Player *d)
{
    HitDef *h = &a->hd;
    int g = guarding(d, h);
    int dmg = g ? h->gdamage : h->damage;
    d->life -= dmg;
    clampvar(d);
    a->power += h->getpower; d->power += h->givepower; clampvar(a); clampvar(d);
    int killed = d->life <= 0;
    if (killed && !(g ? h->guard_kill : h->kill)) { d->life = 1; killed = 0; }
    if (killed) d->alive = 0;

    d->facing = -a->facing;                       /* the victim turns to face the attacker */
    int airborne = d->statetype == ST_A;
    int to;
    d->gh_damage = dmg; d->gh_guarded = g; d->gh_slide = 0;
    d->gh_animtype = airborne ? h->air_animtype : h->animtype;
    float vx, vy;
    d->fall = h->fall;
    if (g) {
        to = airborne ? 152 : (d->statetype == ST_C ? 151 : 150);
        vx = h->guardvx; vy = 0; d->hit_time = h->gghittime;
        d->shake = h->gp2shake; a->pause = h->gp1pause;
        if (airborne) vy = d->vy;
        d->fall = 0;
    } else {
        d->shake = h->p2shake; a->pause = h->p1pause;
        if (airborne) { to = 5020; vx = h->avx; vy = h->avy; d->hit_time = h->ahittime; }
        else if (h->gvy < 0 || h->fall) { to = 5020; vx = h->gvx; vy = h->gvy < 0 ? h->gvy : -3.0f; d->hit_time = h->ghittime; }
        else { to = d->statetype == ST_C ? 5010 : 5000; vx = h->gvx; vy = 0; d->hit_time = h->ghittime; }
        if (killed) { to = 5020; d->fall = 1; if (vy > -5) vy = -5; d->hit_time = 0; }
    }
    d->gh_xvel = vx; d->gh_yvel = vy; d->gh_fvx = h->fall_vx; d->gh_fvy = h->fall_vy;
    if (enter_state(d, to, 0, -1)) { d->vx = vx; d->vy = vy; }
    d->hd.on = 0;                                  /* being hit cancels the victim's own attack */
    d->ntarget = 0;

    a->movecontact = 1;
    if (g) a->moveguarded = 1; else a->movehit = 1;
    a->hitcount++;
    a->ntarget = 1;
    h->hits_left--;
    (void)f;
}

static void resolve_hits(Fight *f)
{
    int hit[2];
    for (int i = 0; i < 2; i++) hit[i] = can_hit(&f->p[i], &f->p[i ^ 1]);
    for (int i = 0; i < 2; i++) if (hit[i]) apply_hit(f, &f->p[i], &f->p[i ^ 1]);
}

/* ============================================================================================ world */
static void world(Fight *f)
{
    Player *a = &f->p[0], *b = &f->p[1];
    /* keep bodies apart */
    float al = a->facing > 0 ? a->x - width_back(a) : a->x - width_front(a);
    float ar = a->facing > 0 ? a->x + width_front(a) : a->x + width_back(a);
    float bl = b->facing > 0 ? b->x - width_back(b) : b->x - width_front(b);
    float br = b->facing > 0 ? b->x + width_front(b) : b->x + width_back(b);
    float hgt = cconst(a, "size.height", 60);
    int vert = (a->y - hgt < b->y) && (b->y - hgt < a->y);
    if (vert && ar > bl && br > al && a->alive && b->alive) {
        float ov = (al < bl ? ar - bl : br - al);
        if (ov > 0) {
            float sgn = a->x <= b->x ? -1.0f : 1.0f;
            a->x += sgn * ov * 0.5f; b->x -= sgn * ov * 0.5f;
        }
    }
    /* camera between the fighters, then keep both on screen and inside the stage */
    float mid = (a->x + b->x) * 0.5f;
    f->camx = clampf(mid, (float)f->cam_left, (float)f->cam_right);
    for (int i = 0; i < 2; i++) {
        Player *p = &f->p[i];
        float lo = f->camx - (FIGHT_LOCAL_W * f->cs / 2) + 4 * f->cs, hi = f->camx + (FIGHT_LOCAL_W * f->cs / 2) - 4 * f->cs;
        p->x = clampf(p->x, lo > f->left_bound ? lo : (float)f->left_bound, hi < f->right_bound ? hi : (float)f->right_bound);
    }
}

/* ============================================================================================ public */
/* Safety net: a character whose standing sprite would fill most of the screen (missing localcoord, wrong [Size]
 * scale, ...) is shrunk so it fits. Normal characters are never touched. */
static void autofit(Player *p, float cs)
{
    const AirAction *a = air_find(&p->cd->air, 0);
    if (!a || a->nframes < 1 || !p->cd->sff_ok) return;
    const AirFrame *fr = &p->cd->air.frames[a->first];
    const Sprite *sp = sff_find(&p->cd->sff, fr->group, fr->image);
    if (!sp || sp->h == 0) return;
    float ysc = fr->yscale > 0.0f ? fr->yscale : 1.0f;
    float hgt = (float)sp->h * p->ys * ysc / cs;           /* height on the 240-line screen */
    if (hgt > 170.0f) {
        float k = 110.0f / hgt;
        p->xs *= k; p->ys *= k; p->autofit = 1;
    }
}

int fight_init(Fight *f, const CharData *c1, const CharData *c2)
{
    memset(f, 0, sizeof *f);
    f->rng = 12345;
    f->cs = c1->coord_w > 0 ? (float)c1->coord_w / 320.0f : 1.0f;      /* character units per stage unit */
    if (f->cs < 0.25f) f->cs = 0.25f;
    if (f->cs > 8.0f) f->cs = 8.0f;
    f->left_bound = (int)(-320 * f->cs); f->right_bound = (int)(320 * f->cs);
    f->cam_left = (int)(-150 * f->cs); f->cam_right = (int)(150 * f->cs); f->zoffset = FIGHT_ZOFFSET;
    const CharData *cds[2] = { c1, c2 };
    for (int i = 0; i < 2; i++) {
        Player *p = &f->p[i];
        p->f = f; p->cd = cds[i]; p->idx = i;
        p->facing = i == 0 ? 1 : -1;
        p->x = (i == 0 ? -70.0f : 70.0f) * f->cs;
        p->alive = 1; p->ctrl = 1;
        p->lifemax = p->life = (int)cconst(p, "data.life", 1000);
        p->powermax = 3000;
        p->xs = (float)cconst(p, "size.xscale", 1); p->ys = (float)cconst(p, "size.yscale", 1);
        if (!(p->xs >= 0.02f && p->xs <= 20.0f)) p->xs = 1.0f;
        if (!(p->ys >= 0.02f && p->ys <= 20.0f)) p->ys = 1.0f;
        autofit(p, f->cs);
        p->buf_state = INT32_MIN;
        p->statetype = ST_S; p->movetype = MT_I; p->physics = PH_S;
        p->ncfire = p->cd->nctrl;
        p->fire_serial = calloc(p->ncfire ? p->ncfire : 1, sizeof(uint32_t));
        p->fire_time = calloc(p->ncfire ? p->ncfire : 1, sizeof(int32_t));
        if (!p->fire_serial || !p->fire_time || cmdstate_init(&p->cs, &p->cd->cmd) != 0) { fight_free(f); return -1; }
        if (!enter_state(p, 0, 1, -1)) { fight_free(f); return -2; }
        set_anim(p, 0, 0, 0);
    }
    return 0;
}

void fight_free(Fight *f)
{
    for (int i = 0; i < 2; i++) {
        Player *p = &f->p[i];
        free(p->fire_serial); free(p->fire_time);
        cmdstate_free(&p->cs);
        p->fire_serial = NULL; p->fire_time = NULL;
    }
}

void fight_set_stage(Fight *f, const char *def_path)
{
    DefFile d;
    if (def_load(&d, def_path) != 0 || !d.e) return;
    const char *v;
    if ((v = def_get(&d, "PlayerInfo", "leftbound"))) f->left_bound = (int)(atoi(v) * f->cs);
    if ((v = def_get(&d, "PlayerInfo", "rightbound"))) f->right_bound = (int)(atoi(v) * f->cs);
    if ((v = def_get(&d, "Camera", "boundleft"))) f->cam_left = (int)(atoi(v) * f->cs);
    if ((v = def_get(&d, "Camera", "boundright"))) f->cam_right = (int)(atoi(v) * f->cs);
    if ((v = def_get(&d, "StageInfo", "zoffset"))) f->zoffset = atoi(v);
    if (f->left_bound > f->right_bound) { f->left_bound = (int)(-320 * f->cs); f->right_bound = (int)(320 * f->cs); }
    if (f->cam_left > f->cam_right) { f->cam_left = (int)(-150 * f->cs); f->cam_right = (int)(150 * f->cs); }
    if (f->zoffset < 100 || f->zoffset > 300) f->zoffset = FIGHT_ZOFFSET;
    def_free(&d);
}

void fight_tick(Fight *f, uint16_t raw1, uint16_t raw2)
{
    f->tick++;
    f->p[0].raw = raw1; f->p[1].raw = raw2;
    for (int i = 0; i < 2; i++) cmdstate_tick(&f->p[i].cs, to_rel(&f->p[i], f->p[i].raw));
    for (int i = 0; i < 2; i++) player_action(f, &f->p[i]);
    int paused0[2] = { is_paused(&f->p[0]), is_paused(&f->p[1]) };
    resolve_hits(f);
    world(f);
    for (int i = 0; i < 2; i++) player_post(f, &f->p[i], paused0[i]);
}

/* ============================================================================================ drawing */
static void outline(int x, int y, int w, int h, uint32_t c)
{
    if (w < 1) w = 1;
    if (h < 1) h = 1;
    gfx_rect(x, y, w, 1, c); gfx_rect(x, y + h - 1, w, 1, c);
    gfx_rect(x, y, 1, h, c); gfx_rect(x + w - 1, y, 1, h, c);
}

void fight_draw(const Fight *f, uint32_t *fb, int w, int h)
{
    gfx_target(fb, w, h);
    int S = h / FIGHT_LOCAL_H; if (S < 1) S = 1;
    int ox = (w - FIGHT_LOCAL_W * S) / 2, oy = (h - FIGHT_LOCAL_H * S) / 2;
#define SX(x) (ox + (int)((((x) - f->camx) / f->cs + FIGHT_LOCAL_W / 2) * (float)S))
#define SY(y) (oy + (int)(((float)f->zoffset + (y) / f->cs) * (float)S))

    /* plain stage: sky and floor */
    gfx_clear(0xFF182030);
    gfx_rect(0, SY(0), w, h - SY(0), 0xFF3A3028);
    gfx_rect(0, SY(0), w, S, 0xFF605040);

    /* draw lower sprite priority first */
    int order[2] = { 0, 1 };
    if (f->p[1].sprpriority < f->p[0].sprpriority) { order[0] = 1; order[1] = 0; }
    for (int n = 0; n < 2; n++) {
        const Player *p = &f->p[order[n]];
        gfx_rect(SX(p->x) - 14 * S, SY(0) - S, 28 * S, 2 * S, 0xFF201810);          /* shadow */
        const AirFrame *fr = cur_frame(p);
        if (!fr || fr->group < 0 || !p->cd->sff_ok) continue;
        const Sprite *sp = sff_find(&p->cd->sff, fr->group, fr->image);
        if (!sp || !sp->px) continue;
        const uint32_t *pal = sff_palette(&p->cd->sff, sp);
        if (!pal) continue;
        int ax = SX(p->x + (float)(p->facing * fr->xoff) * p->xs), ay = SY(p->y + (float)fr->yoff * p->ys);
        float fxs = fr->xscale > 0.0f ? fr->xscale : 1.0f, fys = fr->yscale > 0.0f ? fr->yscale : 1.0f;
        sprite_blit_scaled2(sp, pal, fb, w, h, ax, ay, (float)S / f->cs * p->xs * fxs, (float)S / f->cs * p->ys * fys,
                            (p->facing * fr->hflip) < 0);
    }
    if (f->show_boxes) {
        for (int i = 0; i < 2; i++) {
            const Player *p = &f->p[i];
            const AirFrame *fr = cur_frame(p);
            if (!fr) continue;
            for (int k = 0; k < fr->nclsn2; k++) {
                Rect r = world_box(p, fr, &p->cd->air.boxes[fr->clsn2 + (uint32_t)k]);
                outline(SX(r.x1), SY(r.y1), SX(r.x2) - SX(r.x1), SY(r.y2) - SY(r.y1), 0xFF4080FF);
            }
            for (int k = 0; k < fr->nclsn1; k++) {
                Rect r = world_box(p, fr, &p->cd->air.boxes[fr->clsn1 + (uint32_t)k]);
                outline(SX(r.x1), SY(r.y1), SX(r.x2) - SX(r.x1), SY(r.y2) - SY(r.y1), 0xFFFF4040);
            }
        }
    }

    /* HUD: life and power bars, and for player 1 what the engine currently sees */
    int bw = (w / 2) - 60, bh = 14 * S / 2;
    for (int i = 0; i < 2; i++) {
        const Player *p = &f->p[i];
        int x = i == 0 ? 30 : w - 30 - bw;
        gfx_rect(x - 2, 18, bw + 4, bh + 4, 0xFF000000);
        int fill = p->lifemax ? (int)((long long)bw * p->life / p->lifemax) : 0;
        int fx = i == 0 ? x + (bw - fill) : x;
        gfx_rect(fx, 20, fill, bh, 0xFF40C060);
        int pf = p->powermax ? (int)((long long)bw * p->power / p->powermax) : 0;
        gfx_rect(i == 0 ? x : x + bw - pf, 20 + bh + 4, pf, 4, 0xFF40A0E0);
        gfx_text(x, 20 + bh + 14, 2, 0xFFE0E0E0, p->autofit ? "AUTO-SIZED" : p->cd->name);
    }
    char t[128];
    const Player *p = &f->p[0];
    snprintf(t, sizeof t, "STATE %d  ANIM %d  TIME %d  CTRL %d", p->stateno, p->animno, p->time, p->ctrl);
    gfx_text(30, h - 70, 2, 0xFFE0E0E0, t);
    char act[128]; act[0] = 0; size_t n = 0;
    for (int i = 0; i < p->cd->cmd.n && n < 100; i++) {
        const char *nm = p->cd->cmd.c[i].name;
        if (p->cs.buf && p->cs.buf[i] > 0 && strncmp(nm, "hold", 4) != 0 && strlen(nm) < 20) n += (size_t)snprintf(act + n, sizeof act - n, "%s ", nm);
    }
    gfx_text(30, h - 50, 2, 0xFFFFC040, act);
    gfx_text(30, h - 30, 2, 0xFF808890, "SELECT:HITBOXES  L1:RESET  L2:BACK");
#undef SX
#undef SY
}
