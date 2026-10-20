#ifndef IK_CNS_H
#define IK_CNS_H
/* CNS / ST / CMD state files: [Data] [Size] [Velocity] [Movement] constants, [Statedef N], [State N, label]. */
#include <stdint.h>
#include <stddef.h>
#include "expr.h"

#define CNS_MAX_FILE      (4u * 1024u * 1024u)
#define CNS_MAX_STATEDEFS 4096
#define CNS_MAX_CTRLS     60000
#define CNS_MAX_PARAMS    48
#define CNS_MAX_GROUPS    24
#define CNS_MAX_LINES     64          /* trigger lines per controller */
#define CNS_MAX_VALUES    8           /* comma separated values in one parameter */
#define CNS_MAX_CONSTS    1024
#define CNS_BUDGET        (48u * 1024u * 1024u)   /* total bytes a character's states may use */

#define CONTROLLER_LIST(X) \
 X(SC_CHANGESTATE,"changestate") X(SC_SELFSTATE,"selfstate") X(SC_CHANGEANIM,"changeanim") X(SC_CHANGEANIM2,"changeanim2") \
 X(SC_CTRLSET,"ctrlset") X(SC_POSSET,"posset") X(SC_POSADD,"posadd") X(SC_VELSET,"velset") X(SC_VELADD,"veladd") X(SC_VELMUL,"velmul") \
 X(SC_STATETYPESET,"statetypeset") X(SC_PHYSICSSET,"physicsset") X(SC_PLAYSND,"playsnd") X(SC_HITDEF,"hitdef") X(SC_NULL,"null") \
 X(SC_TURN,"turn") X(SC_WIDTH,"width") X(SC_VARSET,"varset") X(SC_VARADD,"varadd") X(SC_VARRANDOM,"varrandom") X(SC_VARRANGESET,"varrangeset") \
 X(SC_AFTERIMAGE,"afterimage") X(SC_AFTERIMAGETIME,"afterimagetime") X(SC_PALFX,"palfx") X(SC_ALLPALFX,"allpalfx") X(SC_BGPALFX,"bgpalfx") \
 X(SC_PAUSE,"pause") X(SC_SUPERPAUSE,"superpause") X(SC_ENVSHAKE,"envshake") X(SC_EXPLOD,"explod") X(SC_REMOVEEXPLOD,"removeexplod") \
 X(SC_MODIFYEXPLOD,"modifyexplod") X(SC_SCREENBOUND,"screenbound") X(SC_POSFREEZE,"posfreeze") X(SC_TARGETBIND,"targetbind") \
 X(SC_TARGETSTATE,"targetstate") X(SC_TARGETLIFEADD,"targetlifeadd") X(SC_TARGETFACING,"targetfacing") X(SC_TARGETVELSET,"targetvelset") \
 X(SC_TARGETVELADD,"targetveladd") X(SC_TARGETPOWERADD,"targetpoweradd") X(SC_TARGETDROP,"targetdrop") X(SC_HITOVERRIDE,"hitoverride") \
 X(SC_REVERSALDEF,"reversaldef") X(SC_NOTHITBY,"nothitby") X(SC_HITBY,"hitby") X(SC_SPRPRIORITY,"sprpriority") X(SC_ASSERTSPECIAL,"assertspecial") \
 X(SC_HITVELSET,"hitvelset") X(SC_LIFEADD,"lifeadd") X(SC_LIFESET,"lifeset") X(SC_POWERADD,"poweradd") X(SC_POWERSET,"powerset") \
 X(SC_MAKEDUST,"makedust") X(SC_GRAVITY,"gravity") X(SC_HELPER,"helper") X(SC_DESTROYSELF,"destroyself") X(SC_PROJECTILE,"projectile") \
 X(SC_BINDTOPARENT,"bindtoparent") X(SC_BINDTOROOT,"bindtoroot") X(SC_BINDTOTARGET,"bindtotarget") X(SC_ATTACKDIST,"attackdist") \
 X(SC_ATTACKMULSET,"attackmulset") X(SC_DEFENCEMULSET,"defencemulset") X(SC_FALLENVSHAKE,"fallenvshake") X(SC_HITADD,"hitadd") \
 X(SC_HITFALLDAMAGE,"hitfalldamage") X(SC_HITFALLSET,"hitfallset") X(SC_HITFALLVEL,"hitfallvel") X(SC_OFFSET,"offset") \
 X(SC_PARENTVARSET,"parentvarset") X(SC_PARENTVARADD,"parentvaradd") X(SC_PLAYERPUSH,"playerpush") X(SC_REMAPPAL,"remappal") \
 X(SC_STOPSND,"stopsnd") X(SC_SNDPAN,"sndpan") X(SC_TRANS,"trans") X(SC_ANIMRESET,"animreset") X(SC_DISPLAYTOCLIPBOARD,"displaytoclipboard") \
 X(SC_APPENDTOCLIPBOARD,"appendtoclipboard") X(SC_CLEARCLIPBOARD,"clearclipboard") X(SC_DEFENCEMULSET2,"defencemulset2") \
 X(SC_ENVCOLOR,"envcolor") X(SC_MOVEHITRESET,"movehitreset") X(SC_FORCEFEEDBACK,"forcefeedback") X(SC_GAMEMAKEANIM,"gamemakeanim") X(SC_VICTORYQUOTE,"victoryquote") X(SC_ASSERTINPUT,"assertinput")

enum {
    SC_UNKNOWN = 0,
#define X(id, name) id,
    CONTROLLER_LIST(X)
#undef X
    SC_COUNT
};

enum { ST_NONE = 0, ST_S = 1, ST_C = 2, ST_A = 3, ST_L = 4 };
enum { MT_NONE = 0, MT_I = 1, MT_A = 2, MT_H = 3 };
enum { PH_NONE = 0, PH_S = 1, PH_C = 2, PH_A = 3, PH_N = 4 };

typedef struct {
    char     name[32];             /* lowercase */
    char    *raw;                  /* the text after "=" */
    Expr    *e[CNS_MAX_VALUES];    /* compiled comma separated values (NULL where it is not a valid expression) */
    int      ne;
} Param;

typedef struct { Expr **e; int n; } Group;

typedef struct {
    int     type;                  /* SC_* */
    Expr  **all; int nall;         /* triggerall */
    Group  *grp; int ngrp;         /* trigger1..N at index 0..N-1; an empty group (n == 0) can never fire */
    Param  *par; int npar;
    int     persistent;            /* 1 = every tick, N = once every N ticks while true, 0 = once per state entry */
    int     ignorehitpause;
} Ctrl;

typedef struct {
    int    no;
    int    type, movetype, physics;          /* ST_ / MT_ / PH_ (0 = leave unchanged) */
    Expr  *juggle, *ctrl, *anim, *velx, *vely, *poweradd, *sprpriority;
    int    facep2, hitcountpersist, movehitpersist, hitdefpersist;
    Ctrl  *c; int nc, cap;
    int    gen;                              /* which cns_parse call defined it */
} StateDef;

typedef struct { char name[48]; double v; } Const;

typedef struct {
    StateDef *s; int ns, cap;                /* sorted by .no after cns_finish() */
    Const    *k; int nk;
    size_t    bytes;                         /* memory used, checked against CNS_BUDGET */
    int       truncated;                     /* a limit was hit; the rest of the data was skipped */
    int       gen;
} StateDb;

void cns_init(StateDb *db);
/* Parses one state file's text into db. If override is 0, statedefs that already exist are kept (first wins). */
int  cns_parse(const char *text, size_t len, StateDb *db, int override);
int  cns_load(const char *path, StateDb *db, int override);       /* 0 ok, <0 unreadable */
void cns_finish(StateDb *db);                                      /* sorts; call once after all files */
void cns_free(StateDb *db);
const StateDef *cns_state(const StateDb *db, int no);
double cns_const(const StateDb *db, const char *name, double dflt);
const char *cns_ctrl_name(int type);
const Param *ctrl_param(const Ctrl *c, const char *name);

#endif
