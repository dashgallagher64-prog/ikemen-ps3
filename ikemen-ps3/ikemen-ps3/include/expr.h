#ifndef IK_EXPR_H
#define IK_EXPR_H
/* Trigger / value expressions from CNS and CMD files: compiled to a small forward-only bytecode (no loops, so
 * evaluation always finishes), with hard size limits so hostile files cannot exhaust memory. */
#include <stdint.h>
#include <stddef.h>

#define EXPR_MAX_INSN   512
#define EXPR_MAX_STR    4
#define EXPR_STR_LEN    48
#define EXPR_MAX_DEPTH  48
#define EXPR_STACK      64
#define EXPR_MAX_SRC    2048

typedef struct { double d; uint8_t isf, bad; } Val;

static inline Val val_int(int32_t i)  { Val v = { (double)i, 0, 0 }; return v; }
static inline Val val_flt(double f)   { Val v = { f, 1, 0 }; return v; }
static inline Val val_bad(void)       { Val v = { 0, 0, 1 }; return v; }
static inline int val_true(Val v)     { return !v.bad && v.d != 0; }

/* Every named trigger the expression language knows. The fight engine answers them in its get() callback. */
#define TRIGGER_LIST(X) \
 X(TG_TIME,"time",0,0) X(TG_STATETIME,"statetime",0,0) X(TG_STATENO,"stateno",0,0) X(TG_PREVSTATENO,"prevstateno",0,0) \
 X(TG_ANIM,"anim",0,0) X(TG_ANIMTIME,"animtime",0,0) X(TG_ANIMELEMTIME,"animelemtime",1,0) X(TG_ANIMELEMNO,"animelemno",1,0) \
 X(TG_ANIMEXIST,"animexist",1,0) X(TG_SELFANIMEXIST,"selfanimexist",1,0) X(TG_CTRL,"ctrl",0,0) X(TG_ALIVE,"alive",0,0) \
 X(TG_LIFE,"life",0,0) X(TG_LIFEMAX,"lifemax",0,0) X(TG_POWER,"power",0,0) X(TG_POWERMAX,"powermax",0,0) \
 X(TG_POSX,"pos x",0,0) X(TG_POSY,"pos y",0,0) X(TG_VELX,"vel x",0,0) X(TG_VELY,"vel y",0,0) X(TG_FACING,"facing",0,0) \
 X(TG_COMMAND,"command",0,0) X(TG_VAR,"var",1,0) X(TG_FVAR,"fvar",1,0) X(TG_SYSVAR,"sysvar",1,0) X(TG_SYSFVAR,"sysfvar",1,0) \
 X(TG_RANDOM,"random",0,0) X(TG_STATETYPE,"statetype",0,1) X(TG_MOVETYPE,"movetype",0,2) X(TG_PHYSICS,"physics",0,3) \
 X(TG_MOVECONTACT,"movecontact",0,0) X(TG_MOVEHIT,"movehit",0,0) X(TG_MOVEGUARDED,"moveguarded",0,0) X(TG_MOVEREVERSED,"movereversed",0,0) \
 X(TG_HITFALL,"hitfall",0,0) X(TG_HITCOUNT,"hitcount",0,0) X(TG_UNIQHITCOUNT,"uniqhitcount",0,0) X(TG_HITOVER,"hitover",0,0) \
 X(TG_HITSHAKEOVER,"hitshakeover",0,0) X(TG_HITPAUSETIME,"hitpausetime",0,0) X(TG_CANRECOVER,"canrecover",0,0) \
 X(TG_P2DISTX,"p2dist x",0,0) X(TG_P2DISTY,"p2dist y",0,0) X(TG_P2BODYDISTX,"p2bodydist x",0,0) X(TG_P2BODYDISTY,"p2bodydist y",0,0) \
 X(TG_P2STATETYPE,"p2statetype",0,1) X(TG_P2MOVETYPE,"p2movetype",0,2) X(TG_P2STATENO,"p2stateno",0,0) X(TG_P2LIFE,"p2life",0,0) \
 X(TG_P2ANIM,"p2anim",0,0) X(TG_P2NAME,"p2name",0,0) X(TG_PLAYERIDEXIST,"playeridexist",1,0) \
 X(TG_FRONTEDGEBODYDIST,"frontedgebodydist",0,0) X(TG_BACKEDGEBODYDIST,"backedgebodydist",0,0) \
 X(TG_FRONTEDGEDIST,"frontedgedist",0,0) X(TG_BACKEDGEDIST,"backedgedist",0,0) \
 X(TG_SCREENPOSX,"screenpos x",0,0) X(TG_SCREENPOSY,"screenpos y",0,0) \
 X(TG_ROOTDISTX,"rootdist x",0,0) X(TG_ROOTDISTY,"rootdist y",0,0) X(TG_PARENTDISTX,"parentdist x",0,0) X(TG_PARENTDISTY,"parentdist y",0,0) \
 X(TG_ROUNDNO,"roundno",0,0) X(TG_ROUNDSTATE,"roundstate",0,0) X(TG_GAMETIME,"gametime",0,0) X(TG_MATCHNO,"matchno",0,0) \
 X(TG_TIMEMOD,"timemod",2,0) X(TG_ABS,"abs",1,0) X(TG_FLOOR,"floor",1,0) X(TG_CEIL,"ceil",1,0) \
 X(TG_SIN,"sin",1,0) X(TG_COS,"cos",1,0) X(TG_TAN,"tan",1,0) X(TG_ASIN,"asin",1,0) X(TG_ACOS,"acos",1,0) X(TG_ATAN,"atan",1,0) \
 X(TG_LN,"ln",1,0) X(TG_LOG,"log",2,0) X(TG_EXP,"exp",1,0) X(TG_SQRT,"sqrt",1,0) \
 X(TG_CONST,"const",1,0) X(TG_CONST240P,"const240p",1,0) X(TG_CONST480P,"const480p",1,0) X(TG_CONST720P,"const720p",1,0) \
 X(TG_NUMHELPER,"numhelper",-1,0) X(TG_NUMEXPLOD,"numexplod",-1,0) X(TG_NUMPROJ,"numproj",0,0) X(TG_NUMTARGET,"numtarget",-1,0) \
 X(TG_NUMENEMY,"numenemy",0,0) X(TG_NUMPARTNER,"numpartner",0,0) \
 X(TG_ISHELPER,"ishelper",-1,0) X(TG_TEAMSIDE,"teamside",0,0) X(TG_TEAMMODE,"teammode",0,0) X(TG_AILEVEL,"ailevel",0,0) \
 X(TG_HITDEFATTR,"hitdefattr",0,0) X(TG_INGUARDDIST,"inguarddist",0,0) X(TG_FALL,"fall",0,0) X(TG_GETHITVARFALL,"gethitvar",1,0) \
 X(TG_WIN,"win",0,0) X(TG_LOSE,"lose",0,0) X(TG_DRAW,"draw",0,0) X(TG_MATCHOVER,"matchover",0,0) X(TG_STAGEVAR,"stagevar",1,0) \
 X(TG_PROJCONTACT,"projcontacttime",-1,0) X(TG_PROJHIT,"projhittime",-1,0) X(TG_PROJGUARDED,"projguardedtime",-1,0) \
 X(TG_AUTHORNAME,"authorname",0,0) X(TG_NAME,"name",0,0) X(TG_ID,"id",0,0) X(TG_PARENTID,"parentid",0,0) X(TG_NUMPLAYERS,"numplayers",0,0) \
 X(TG_SELFSTATENOEXIST,"selfstatenoexist",1,0) X(TG_STATENOEXIST,"statenoexist",1,0) X(TG_LASTPLAYERID,"lastplayerid",0,0)

enum {
#define X(id, name, ar, kind) id,
    TRIGGER_LIST(X)
#undef X
    TG_COUNT
};

/* redirect kinds for "P2, Life", "Target, Life", "Helper(3), Pos X" ... */
const char *expr_trigger_name(int id);

enum { RD_P2 = 1, RD_PARENT, RD_ROOT, RD_HELPER, RD_ENEMY, RD_TARGET, RD_PARTNER, RD_ENEMYNEAR, RD_PLAYERID, RD_SELF };
/* var assignment kinds */
enum { AS_VAR = 0, AS_FVAR, AS_SYSVAR, AS_SYSFVAR };

typedef struct {
    void *ud;
    /* answers a trigger. args holds nargs numeric arguments; str is the string argument (Command, Const) or "". */
    Val  (*get)(void *ud, int id, const Val *args, int nargs, const char *str);
    /* Var(idx) := v. Returns 0 if not allowed. May be NULL. */
    int  (*set)(void *ud, int kind, int idx, Val v);
    /* switches the context for a redirect; returns the new ud or NULL if the target does not exist. May be NULL. */
    void *(*redir)(void *ud, int kind, int arg);
} ExprCtx;

typedef struct Expr Expr;

/* Compiles one expression. Returns NULL if it is malformed, too long, or uses an unknown trigger. */
Expr *expr_compile(const char *src);
void  expr_free(Expr *e);
Val   expr_eval(const Expr *e, const ExprCtx *ctx);
size_t expr_size(const Expr *e);       /* bytes used, for memory budgeting */

/* Splits "a, b(1,2), [3,4]" at top-level commas. Copies at most max pieces into out (each EXPR_MAX_SRC). */
int   expr_split(const char *src, char out[][EXPR_MAX_SRC], int max);

/* Looks up the numeric code of a bare symbol (S, C, A, L / I, A, H / ...). kind is the trigger's kind field. */
int   expr_symbol(int kind, const char *lowercase_name);

#endif
