/* POSIX kiegészítések a NetSurf-hez: mmap emuláció, uname, realpath,
 * unlinkat, pipe-stub. */
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <sys/utsname.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <limits.h>
#include "rexos_syscall.h"

#ifndef PATH_MAX
#define PATH_MAX 1024
#endif

/* --- mmap emuláció: csak fájl READ mapping (malloc + read) ------------------ */

void *mmap(void *addr, size_t len, int prot, int flags, int fd, off_t off)
{
    (void)addr; (void)prot;

    if (len == 0) { errno = EINVAL; return MAP_FAILED; }

    void *buf = malloc(len);
    if (!buf) { errno = ENOMEM; return MAP_FAILED; }

    if (flags & MAP_ANON) {
        memset(buf, 0, len);
        return buf;
    }

    /* Fájl mapping: beolvassuk az egészet. Írás nem propagálódik vissza -
     * a NetSurf csak olvasásra használja. */
    uint64_t saved = (uint64_t)lseek(fd, 0, SEEK_CUR);
    if (lseek(fd, off, SEEK_SET) == (off_t)-1) {
        free(buf);
        errno = EBADF;
        return MAP_FAILED;
    }
    size_t got = 0;
    while (got < len) {
        int r = read(fd, (char *)buf + got, len - got);
        if (r <= 0) break;
        got += (size_t)r;
    }
    lseek(fd, (off_t)saved, SEEK_SET);
    if (got < len) memset((char *)buf + got, 0, len - got);
    return buf;
}

int munmap(void *addr, size_t len)
{
    (void)len;
    free(addr);
    return 0;
}

/* --- uname ------------------------------------------------------------------- */

int uname(struct utsname *buf)
{
    if (!buf) { errno = EFAULT; return -1; }
    strcpy(buf->sysname,  "RexOS");
    strcpy(buf->nodename, "rexos");
    strcpy(buf->release,  "0.1");
    strcpy(buf->version,  "0.1");
    strcpy(buf->machine,  "x86_64");
    return 0;
}

/* --- realpath ------------------------------------------------------------------
 * A RexOS VFS-ben nincs szimlink és nincs cwd; a path-t normalizáljuk
 * (., .. feloldás), relatív útvonalat "/" gyökérre értelmezünk. */

char *realpath(const char *path, char *resolved)
{
    if (!path) { errno = EINVAL; return NULL; }

    char tmp[PATH_MAX];
    size_t ti = 0;
    tmp[ti++] = '/';

    const char *p = path;
    while (*p) {
        while (*p == '/') p++;
        if (!*p) break;
        const char *seg = p;
        while (*p && *p != '/') p++;
        size_t sl = (size_t)(p - seg);
        if (sl == 1 && seg[0] == '.') continue;
        if (sl == 2 && seg[0] == '.' && seg[1] == '.') {
            while (ti > 1 && tmp[ti - 1] == '/') ti--;
            while (ti > 1 && tmp[ti - 1] != '/') ti--;
            continue;
        }
        if (ti > 1) tmp[ti++] = '/';
        if (ti + sl >= PATH_MAX - 1) { errno = ENAMETOOLONG; return NULL; }
        memcpy(tmp + ti, seg, sl);
        ti += sl;
    }
    tmp[ti] = 0;

    if (!resolved) resolved = malloc(ti + 1);
    if (!resolved) { errno = ENOMEM; return NULL; }
    memcpy(resolved, tmp, ti + 1);
    return resolved;
}

/* --- unlinkat ------------------------------------------------------------------ */

#ifndef AT_FDCWD
#define AT_FDCWD -100
#endif

int unlinkat(int dirfd, const char *path, int flags)
{
    (void)flags;
    if (dirfd != AT_FDCWD && path[0] != '/') {
        errno = ENOSYS;
        return -1;
    }
    return unlink(path);
}

/* --- pipe (stub) ----------------------------------------------------------------- */

int pipe(int fds[2])
{
    (void)fds;
    errno = ENOSYS;
    return -1;
}

/* --- további fd/fájl műveletek ------------------------------------------------- */

int dup(int fd)
{
    /* A kernel nem tud fd-duplikálást; stdio fd-re az azonos fd visszaadása
     * elegendő (közös offsettel amúgy is ugyanoda írnak). */
    if (fd <= 2) return fd;
    errno = ENOSYS;
    return -1;
}

int rmdir(const char *path)
{
    return unlink(path);
}

int fstatat(int dirfd, const char *path, struct stat *st, int flags)
{
    (void)flags;
    if (dirfd != -100 /* AT_FDCWD */ && path[0] != '/') {
        errno = ENOSYS;
        return -1;
    }
    return stat(path, st);
}

/* --- POSIX regex (stub) ----------------------------------------------------------
 * A newlib ad <regex.h> headert, de implementációt nem. A NetSurf csak a
 * "teljes oldal mentése" funkcióban használ regexet; a stub fordítási hibát
 * ad vissza, amit a hívó kezel. */
#include <regex.h>

int regcomp(regex_t *preg, const char *pattern, int cflags)
{
    (void)preg; (void)pattern; (void)cflags;
    return REG_ESPACE;
}

int regexec(const regex_t *preg, const char *string, size_t nmatch,
            regmatch_t pmatch[], int eflags)
{
    (void)preg; (void)string; (void)nmatch; (void)pmatch; (void)eflags;
    return REG_NOMATCH;
}

size_t regerror(int errcode, const regex_t *preg, char *errbuf, size_t size)
{
    (void)errcode; (void)preg;
    const char *msg = "regex not supported on RexOS";
    if (errbuf && size) {
        strncpy(errbuf, msg, size - 1);
        errbuf[size - 1] = 0;
    }
    return strlen(msg) + 1;
}

void regfree(regex_t *preg)
{
    (void)preg;
}
