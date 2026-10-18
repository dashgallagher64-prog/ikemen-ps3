#ifndef IK_FIGHT_H
#define IK_FIGHT_H
/* The fight: characters, per-tick state machine, physics, hits and drawing. Portable C (no PS3 headers). */
#include <stdint.h>
#include "ikemen.h"
#include "air.h"
#include "cmd.h"
#include "cns.h"

/* raw controller bits (what the pad gives); facing is applied inside the engine */
enum { FI_UP = 1, FI_DOWN = 2, FI_LEFT = 4, FI_RIGHT = 8, FI_A = 16, FI_B = 32, FI_C = 64, FI_X = 128,
       FI_Y = 256, FI_Z = 512, FI_START = 1024 };

#define FIGHT_NVAR   60
#define FIGHT_NFVAR  40
#define FIGHT_LOCAL_W 320           /* MUGEN local coordinate space */
#define FIGHT_LOCAL_H 240
#define FIGHT_ZOFFSET 200           /* ground line, in local y */

typedef struct {
    Air air; StateDb cns; CmdList cmd; Sff sff; int sff_ok;
    uint32_t *ctrl_base; uint32_t nctrl;   /* per-state offset into the flat controller numbering */
    char name[IK_NAME_LEN], dir[IK_PATH_LEN];
    int  coord_w;                    /* [Info] localcoord width (320 for classic MUGEN characters) */
    char problem[160];               /* what went wrong while loading (shown on screen and logged) */
    int  warnings;                   /* files that could not be read, etc. */
} CharData;

/* common_dir holds common1.cns and common.cmd (USRDIR/data). Returns 0 if the character can fight. */
int  chardata_load(CharData *cd, const char *def_path, const char *common_dir);
void chardata_free(CharData *cd);

typedef struct {
    int on;
    int st, cls, typ;                /* attr: ST_* bits, class N/S/H = 1/2/4, type A/T/P = 1/2/4 */
    int hitflag, guardflag;          /* bit sets: 1 H, 2 L, 4 A, 8 F, 16 D (M = H+L) */
    int damage, gdamage, p1pause, p2shake, gp1pause, gp2shake;
    int animtype, air_animtype, ground_type;
    int ghittime, ahittime, gghittime;
    float gvx, gvy, avx, avy, guardvx;
    int fall; float fall_vx, fall_vy;
    int numhits, hits_left, id, chainid, nochainid, kill, guard_kill;
    int getpower, givepower, p1stateno, p2stateno;
} HitDef;

typedef struct Fight Fight;
typedef struct {
    Fight *f; const CharData *cd; int idx;
    int stateno, prev_stateno, statetype, prev_type, movetype, physics, ctrl, alive, facing;
    int32_t time; uint32_t serial;
    float x, y, vx, vy;
    Anim anim; int animno;
    int autofit;                     /* 1 if the character was shrunk to fit the screen */
    int ctrl_cont;                   /* the controller being run also fired on the previous tick */
    float xs, ys;                    /* [Size] xscale / yscale: graphics and hitboxes are scaled by these */
    int life, lifemax, power, powermax;
    int32_t var[FIGHT_NVAR], sysvar[5]; float fvar[FIGHT_NFVAR], sysfvar[5];
    uint16_t raw; CmdState cs;
    HitDef hd;
    int movecontact, movehit, moveguarded;   /* ticks since contact (0 = none) */
    int pause, shake, hit_time, fall, hit_xvel_dummy;
    int gh_damage, gh_animtype, gh_guarded, gh_fall, gh_hittime, gh_slide;
    float gh_xvel, gh_yvel, gh_fvx, gh_fvy;
    int buf_state; int buf_ctrl; int buf_anim;             /* ChangeState requested from -3/-2/-1 */
    int sprpriority; int posfreeze;
    uint32_t *fire_serial; int32_t *fire_time; uint32_t ncfire;   /* persistent= bookkeeping per controller */
    int ntarget; int target_idx;
    int hitcount;
} Player;

struct Fight {
    Player p[2];
    int tick, rng;
    float camx, cs;                  /* cs: character units per stage unit (localcoord / 320) */
    int left_bound, right_bound, cam_left, cam_right, zoffset;
    int show_boxes;
    int unimpl[SC_COUNT];            /* how often a controller we do not implement yet was triggered */
    int missing_state;               /* ChangeState to a state that does not exist */
};

int  fight_init(Fight *f, const CharData *c1, const CharData *c2);
void fight_free(Fight *f);
void fight_set_stage(Fight *f, const char *def_path);       /* optional: bounds from a stage .def */
void fight_tick(Fight *f, uint16_t raw1, uint16_t raw2);    /* one game tick (60 per second) */
void fight_draw(const Fight *f, uint32_t *fb, int w, int h);

#endif
