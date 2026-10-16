/* includes content.c directly to reach the static helper */
#include "../source/content.c"
#include <assert.h>
static int t(const char *in, const char *want) {
    char o[IK_PATH_LEN]; int rc = safe_rel(in, o, sizeof o);
    int ok = want ? (rc == 0 && !strcmp(o, want)) : (rc != 0);
    printf("%-26s -> %-14s %s\n", in, rc ? "REJECT" : o, ok ? "ok" : "WRONG");
    return ok;
}
int main(void) {
    int ok = 1;
    ok &= t("kfm.sff", "kfm.sff");
    ok &= t("gfx\\kfm.sff", "gfx/kfm.sff");
    ok &= t("sub/dir/a.sff", "sub/dir/a.sff");
    ok &= t("..\\x.sff", NULL);
    ok &= t("a/../../x.sff", NULL);
    ok &= t("a/..", NULL);
    ok &= t("/dev_flash/x", NULL);
    ok &= t("\\dev_flash\\x", NULL);
    ok &= t("dev_hdd0:/x", NULL);
    ok &= t("", NULL);
    ok &= t("..hidden/ok.sff", "..hidden/ok.sff");
    ok &= t("a..b.sff", "a..b.sff");
    return ok ? 0 : 1;
}
