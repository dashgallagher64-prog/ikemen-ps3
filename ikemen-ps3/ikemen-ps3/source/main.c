/* main.c - Ikemen PS3 proof of concept.
 *
 * Scans USRDIR for characters, stages and screenpacks, lets you pick them with
 * the pad, and probes each character's SFF so you can confirm the big-endian
 * parsing works on real hardware. No fight engine yet.
 *
 *   Menus:  D-pad up/down  move   X  confirm   O  back   L1 / R1  cycle screenpack   PS button  quit to XMB
 *   Fight:  D-pad = directions    Square/Triangle/R1 = x/y/z    Cross/Circle/R2 = a/b/c
 *           START = start   SELECT = show hitboxes   L1 = reset   L2 = back to the menu
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <io/pad.h>
#include <sysutil/sysutil.h>
#include "ikemen.h"
#include "fight.h"

#define COL_BG     0xFF101820
#define COL_TITLE  0xFFFFC040
#define COL_TEXT   0xFFE0E0E0
#define COL_DIM    0xFF808890
#define COL_SEL    0xFF2850A0
#define COL_OK     0xFF60D070
#define COL_ERR    0xFFE05050

static int running = 1;

static void sysutil_cb(uint64_t status, uint64_t param, void *ud)
{
    (void)param; (void)ud;
    if (status == SYSUTIL_EXIT_GAME) running = 0;
}

typedef struct { int up, down, cross, circle, l1, r1; uint16_t fight; int l2, select; } Btn;

static Btn poll_pad(void)
{
    Btn b = {0};
    padInfo pi;
    padData pd;
    ioPadGetInfo(&pi);
    for (int i = 0; i < MAX_PADS; i++) {
        if (!pi.status[i]) continue;
        ioPadGetData(i, &pd);
        b.up     |= pd.BTN_UP;
        b.down   |= pd.BTN_DOWN;
        b.cross  |= pd.BTN_CROSS;
        b.circle |= pd.BTN_CIRCLE;
        b.l1     |= pd.BTN_L1;
        b.r1     |= pd.BTN_R1;
        b.l2     |= pd.BTN_L2;
        b.select |= pd.BTN_SELECT;
        if (pd.BTN_UP)       b.fight |= FI_UP;
        if (pd.BTN_DOWN)     b.fight |= FI_DOWN;
        if (pd.BTN_LEFT)     b.fight |= FI_LEFT;
        if (pd.BTN_RIGHT)    b.fight |= FI_RIGHT;
        if (pd.BTN_CROSS)    b.fight |= FI_A;
        if (pd.BTN_CIRCLE)   b.fight |= FI_B;
        if (pd.BTN_R2)       b.fight |= FI_C;
        if (pd.BTN_SQUARE)   b.fight |= FI_X;
        if (pd.BTN_TRIANGLE) b.fight |= FI_Y;
        if (pd.BTN_R1)       b.fight |= FI_Z;
        if (pd.BTN_START)    b.fight |= FI_START;
    }
    return b;
}

static ContentList chars, stages, packs;

/* Lazily decoded sprites for the select screens: one filtered SFF read per character, then cached. */
typedef struct { int state; Sprite sp; uint32_t pal[256]; } Preview;   /* 0 = not tried, 1 = ok, 2 = missing/failed */
static Preview portrait[IK_MAX_ENTRIES], stand[IK_MAX_ENTRIES];

static const Preview *preview(Preview *cache, const ContentList *l, int i, int group, int image)
{
    Preview *p = &cache[i];
    if (p->state == 0) {
        p->state = 2;
        Sff s;
        if (l->e[i].sprite[0] && sff_load_ex(l->e[i].sprite, &s, group, image) == 0) {
            if (sff_extract(&s, group, image, &p->sp, p->pal) == 0) p->state = 1;
            sff_free(&s);
        }
    }
    return p->state == 1 ? p : NULL;
}

static void draw_list(const char *title, const ContentList *l, int sel, int scale)
{
    int line = 5 * scale + 2 * scale + 4;
    int rows = (gfx_height() - 8 * line) / line;
    int top  = sel - rows / 2;
    if (top > l->n - rows) top = l->n - rows;
    if (top < 0) top = 0;

    gfx_text(40, 30, scale + 1, COL_TITLE, title);
    if (l->n == 0) {
        gfx_text(40, 30 + 3 * line, scale, COL_ERR, "NOTHING FOUND - COPY CONTENT INTO USRDIR");
        return;
    }
    for (int i = 0; i < rows && top + i < l->n; i++) {
        int y = 30 + (3 + i) * line;
        if (top + i == sel) gfx_rect(30, y - 4, gfx_width() - 330, line, COL_SEL);   /* leave room for the preview panel */
        gfx_text(40, y, scale, COL_TEXT, l->e[top + i].name);
    }
}

int main(void)
{
    char log_path[IK_PATH_LEN];
    snprintf(log_path, sizeof log_path, "%s/ikemen.log", IK_ROOT);
    FILE *log = fopen(log_path, "w");

    sysUtilRegisterCallback(0, sysutil_cb, NULL);
    ioPadInit(7);
    if (gfx_init() != 0) { if (log) { fprintf(log, "gfx_init failed\n"); fclose(log); } return 1; }

    content_scan_chars(&chars, IK_ROOT);
    content_scan_stages(&stages, IK_ROOT);
    content_scan_screenpacks(&packs, IK_ROOT);
    if (log) {
        fprintf(log, "chars=%d stages=%d screenpacks=%d\n", chars.n, stages.n, packs.n);
        for (int i = 0; i < chars.n; i++) {
            SffInfo si;
            int rc = chars.e[i].sprite[0] ? sff_probe(chars.e[i].sprite, &si) : -9;
            fprintf(log, "char %s sff=%s rc=%d v%d sprites=%u\n", chars.e[i].name,
                    chars.e[i].sprite, rc, rc == 0 ? si.version : 0, rc == 0 ? si.sprites : 0);
        }
        fflush(log);
    }

    int screen = 0, selc = 0, sels = 0, selp = 0;
    CharData cd; Fight fight; int fight_on = 0, cd_ok = 0, load_rc = 0;
    static const char *const load_err[] = { "", "CHARACTER FILES MISSING", "NO STANDING STATE (STATE 0)", "OUT OF MEMORY" };
    char common_dir[IK_PATH_LEN];
    snprintf(common_dir, sizeof common_dir, "%s/data", IK_ROOT);
    Btn prev = {0};
    int scale = gfx_height() >= 720 ? 3 : 2;

    while (running) {
        sysUtilCheckCallback();
        Btn b = poll_pad();
        int up = b.up && !prev.up, down = b.down && !prev.down;
        int ok = b.cross && !prev.cross, back = b.circle && !prev.circle;
        if (fight_on) {
            if (b.l2 && !prev.l2) { fight_free(&fight); chardata_free(&cd); cd_ok = 0; fight_on = 0; screen = 2; }
            else {
                if (b.l1 && !prev.l1) { fight_free(&fight); fight_init(&fight, &cd, &cd); if (stages.n) fight_set_stage(&fight, stages.e[sels].def); }
                if (b.select && !prev.select) fight.show_boxes = !fight.show_boxes;
                fight_tick(&fight, b.fight, 0);
            }
            prev = b;
            uint32_t *fb = gfx_begin();
            if (fight_on) fight_draw(&fight, fb, gfx_width(), gfx_height());
            else gfx_clear(COL_BG);
            gfx_end();
            continue;
        }
        if (b.r1 && !prev.r1 && packs.n) selp = (selp + 1) % packs.n;
        if (b.l1 && !prev.l1 && packs.n) selp = (selp + packs.n - 1) % packs.n;
        prev = b;

        ContentList *cl = (screen == 0) ? &chars : &stages;
        int *sel = (screen == 0) ? &selc : &sels;
        if (screen < 2) {
            if (up && *sel > 0) (*sel)--;
            if (down && *sel < cl->n - 1) (*sel)++;
            if (ok && (cl->n || screen == 1)) screen++;   /* stages may be empty */
        } else if (ok) {                                  /* ready screen: start the fight */
            load_rc = chardata_load(&cd, chars.e[selc].def, common_dir);
            cd_ok = load_rc == 0 || load_rc == -2 ? 1 : 0;      /* chardata_load fills cd even on -2 */
            if (load_rc == 0 && fight_init(&fight, &cd, &cd) == 0) {
                if (stages.n) fight_set_stage(&fight, stages.e[sels].def);
                fight_on = 1;
            } else {
                if (log) { fprintf(log, "fight start failed for %s rc=%d\n", chars.e[selc].name, load_rc); fflush(log); }
                chardata_free(&cd); cd_ok = 0;
                if (load_rc == 0) load_rc = -3;
            }
        }
        if (back && screen > 0) screen--;

        uint32_t *fb = gfx_begin();
        gfx_clear(COL_BG);

        if (screen < 2) {
            draw_list(screen == 0 ? "SELECT CHARACTER" : "SELECT STAGE", cl, *sel, scale);
            if (screen == 0 && chars.n) {                      /* portrait = sprite 9000,0 */
                const Preview *pv = preview(portrait, &chars, selc, 9000, 0);
                int px = gfx_width() - 260, py = 110;
                gfx_rect(px - 10, py - 10, 220, 220, 0xFF1C2838);
                if (pv) {
                    int big = pv->sp.w > pv->sp.h ? pv->sp.w : pv->sp.h, sc = big ? 200 / big : 1;
                    if (sc < 1) sc = 1;
                    if (sc > 8) sc = 8;
                    sprite_blit(&pv->sp, pv->pal, fb, gfx_width(), gfx_height(), px, py, sc, 0);
                } else {
                    gfx_text(px + 20, py + 90, scale, COL_DIM, "NO PREVIEW");
                }
            }
        } else {
            int line = 5 * scale + 2 * scale + 4, y = 30;
            char t[IK_PATH_LEN + 32];
            gfx_text(40, y, scale + 1, COL_TITLE, "READY - X: FIGHT (TRAINING DUMMY)");
            y += 3 * line;
            snprintf(t, sizeof t, "CHAR:  %s", chars.e[selc].name);   gfx_text(40, y, scale, COL_TEXT, t); y += line;
            snprintf(t, sizeof t, "STAGE: %s", stages.n ? stages.e[sels].name : "-"); gfx_text(40, y, scale, COL_TEXT, t); y += line;
            snprintf(t, sizeof t, "PACK:  %s", packs.n ? packs.e[selp].name : "-");   gfx_text(40, y, scale, COL_TEXT, t); y += 2 * line;

            if (load_rc != 0) {
                int e = load_rc == -2 ? 2 : load_rc == -3 ? 3 : 1;
                gfx_text(40, y, scale, COL_ERR, load_err[e]);
                y += line;
            }
            SffInfo si;
            int rc = chars.e[selc].sprite[0] ? sff_probe(chars.e[selc].sprite, &si) : -9;
            if (rc == 0) {
                snprintf(t, sizeof t, "SFF V%d  SPRITES:%u", si.version, si.sprites);
                gfx_text(40, y, scale, COL_OK, t);
            } else {
                snprintf(t, sizeof t, "SFF PROBE FAILED (%d)", rc);
                gfx_text(40, y, scale, COL_ERR, t);
            }

            /* standing sprite = 0,0, drawn with its axis (feet) on the ground line */
            const Preview *pv = preview(stand, &chars, selc, 0, 0);
            if (pv) {
                int sc = pv->sp.h ? (gfx_height() * 45 / 100) / pv->sp.h : 1;
                if (sc < 1) sc = 1;
                if (sc > 6) sc = 6;
                sprite_blit(&pv->sp, pv->pal, fb, gfx_width(), gfx_height(),
                            gfx_width() - 260, gfx_height() - 120, sc, 0);
            }
        }
        gfx_text(40, gfx_height() - 6 * scale - 20, scale, COL_DIM,
                 "X:OK  O:BACK  L1/R1:SCREENPACK");
        gfx_end();
    }

    if (fight_on) fight_free(&fight);
    if (cd_ok || fight_on) chardata_free(&cd);
    if (log) fclose(log);
    return 0;
}
