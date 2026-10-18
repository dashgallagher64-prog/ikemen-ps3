#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cmd.h"
#include "text.h"
int main(int argc, char **argv)
{
    size_t len; char *t = text_load(argv[1], CMD_MAX_FILE, &len);
    if (!t) return 2;
    srand(99);
    for (int it = 0; it < 2000; it++) {
        char *m = malloc(len + 1); memcpy(m, t, len);
        int n = 1 + rand() % 40;
        for (int k = 0; k < n; k++) {
            size_t p = (size_t)rand() % len;
            if (rand() & 1) m[p] = (char)rand(); else m[p] = "~/$>+,0123456789DBFUabcxyzs=[]\n"[rand() % 30];
        }
        CmdList l; cmd_parse(m, (rand() % 4) ? len : (size_t)rand() % len, &l);
        CmdState s; if (cmdstate_init(&s, &l) == 0) {
            for (int k = 0; k < 400; k++) { cmdstate_tick(&s, (uint16_t)(rand() & 0x7ff)); cmdstate_active(&s, "x"); }
            cmdstate_free(&s);
        }
        cmd_free(&l); free(m);
    }
    free(t); puts("fuzz_cmd ok"); return 0;
}
