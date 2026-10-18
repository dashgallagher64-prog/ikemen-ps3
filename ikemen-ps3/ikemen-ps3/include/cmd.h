#ifndef IK_CMD_H
#define IK_CMD_H
/* CMD files: [Command] definitions and matching against a history of controller input. */
#include <stdint.h>
#include <stddef.h>

#define CMD_MAX_FILE      (2u * 1024u * 1024u)
#define CMD_MAX_COMMANDS  512
#define CMD_MAX_SYMBOLS   32
#define CMD_HIST          128      /* ticks of input history kept */
#define CMD_NAME_MAX      64

/* input bits, already relative to the way the character faces (BACK/FWD, not LEFT/RIGHT) */
enum {
    CK_UP = 1 << 0, CK_DOWN = 1 << 1, CK_BACK = 1 << 2, CK_FWD = 1 << 3,
    CK_A = 1 << 4, CK_B = 1 << 5, CK_C = 1 << 6, CK_X = 1 << 7, CK_Y = 1 << 8, CK_Z = 1 << 9, CK_S = 1 << 10,
    CK_DIRS = CK_UP | CK_DOWN | CK_BACK | CK_FWD
};

typedef struct {
    uint16_t dirmask, btnmask;     /* keys named by the symbol */
    uint16_t hold;                 /* minimum held ticks before a release (~30a) */
    uint8_t  kind;                 /* 0 press, 1 hold (/), 2 release (~) */
    uint8_t  fourway, gt;          /* $ and > modifiers */
} CmdSym;

typedef struct {
    char     name[CMD_NAME_MAX];
    CmdSym   sym[CMD_MAX_SYMBOLS];
    int      nsym;
    int      time, buftime;
} Command;

typedef struct { Command *c; int n; } CmdList;

int  cmd_parse(const char *text, size_t len, CmdList *out);    /* 0 = ok */
/* Like cmd_parse but adds to a list that already exists (zero-initialize it first). */
int  cmd_parse_append(const char *text, size_t len, CmdList *out);
int  cmd_load(const char *path, CmdList *out);                 /* 0 = ok, <0 = unreadable */
void cmd_free(CmdList *l);

typedef struct {
    const CmdList *l;
    uint16_t hist[CMD_HIST];
    int      head;
    int     *buf;                  /* remaining buffered ticks per command */
} CmdState;

int  cmdstate_init(CmdState *s, const CmdList *l);
void cmdstate_free(CmdState *s);
void cmdstate_clear(CmdState *s);
/* Records this tick's input (CK_* bits) and re-evaluates every command. */
void cmdstate_tick(CmdState *s, uint16_t keys);
/* Trigger Command = "name": true while any command with that name is active. */
int  cmdstate_active(const CmdState *s, const char *name);

#endif
