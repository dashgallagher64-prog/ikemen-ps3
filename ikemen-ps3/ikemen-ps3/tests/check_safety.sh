#!/bin/sh
# Static safety gate. Fails if source/ contains anything that could modify the console or the
# user's files beyond the one log file, or if the package settings stop being a plain HDD game.
# Comments are stripped first, so only real code is checked.
cd "$(dirname "$0")/.." || exit 2
bad=0
fail() { echo "SAFETY FAIL: $*"; bad=1; }

strip() { gcc -fpreprocessed -dD -E -P "$1" 2>/dev/null | sed 's/^#.*//'; }

# 1. no calls that delete, rename, create, relabel, run or load anything
FORBID='remove|unlink|unlinkat|rename|renameat|rmdir|mkdir|truncate|ftruncate|chmod|chown|utime|symlink|link|system|popen|execv|execve|execl|execlp|execvp|fork|vfork|ioctl|mount|umount|mmap|dlopen|dlsym|syscall|lv2_syscall|poke|peek|sysFs[A-Za-z]*|sysLv2[A-Za-z]*|sysProcess[A-Za-z]*|sysModule[A-Za-z]*|sys_[a-z_0-9]+|netConnect|socket|connect|sendto|send'
for f in source/*.c; do
    hits=$(strip "$f" | grep -nE "(^|[^A-Za-z0-9_])($FORBID)[[:space:]]*\(")
    [ -n "$hits" ] && fail "$f uses a forbidden call:" && echo "$hits"
done

# 2. every fopen must be read-only, except the single log file opened in main.c
for f in source/*.c; do
    strip "$f" | tr '\n' ' ' | grep -oE 'fopen[[:space:]]*\([^;]*;' | while IFS= read -r stmt; do
        case "$stmt" in
            *'"rb"'*) ;;
            *'log_path'*'"w"'*) [ "$f" = "source/main.c" ] || { echo "SAFETY FAIL: $f opens a file for writing: $stmt"; exit 1; } ;;
            *) echo "SAFETY FAIL: $f has an fopen that is not read-only: $stmt"; exit 1 ;;
        esac
    done || bad=1
done

# 3. nothing writes except to that log (fprintf/fputs/fwrite/fflush must target 'log' in main.c)
for f in source/*.c; do
    strip "$f" | grep -nE '(^|[^A-Za-z0-9_])(fwrite|fputs|fputc|putc|fprintf|vfprintf|write|pwrite|fflush)[[:space:]]*\(' | while IFS= read -r line; do
        case "$f:$line" in
            source/main.c:*'(log,'*|source/main.c:*'(log)'*) ;;
            *) echo "SAFETY FAIL: $f writes somewhere other than the log: $line"; exit 1 ;;
        esac
    done || bad=1
done

# 4. package stays an ordinary HDD game with its own title id
grep -q 'name="CATEGORY" type="string">HG<' sfo.xml        || fail "sfo.xml CATEGORY must stay HG (a normal HDD game)"
grep -q 'name="TITLE_ID" type="string">IKEMEN001<' sfo.xml || fail "sfo.xml TITLE_ID changed"
grep -q 'name="ATTRIBUTE" type="integer">0<' sfo.xml        || fail "sfo.xml ATTRIBUTE must stay 0 (no special system rights)"
libs=$(sed -n 's/^LIBS[[:space:]]*:=[[:space:]]*//p' Makefile)
for l in $libs; do
    case "$l" in -lrsx|-lgcm_sys|-lio|-lsysutil|-lrt|-llv2|-lm) ;; *) fail "Makefile links an unexpected library: $l" ;; esac
done

[ "$bad" = 0 ] && echo "safety check passed: read-only content access, one log file, no system/firmware calls"
exit $bad
