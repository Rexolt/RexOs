/* =============================================================================
 *  RexOS <-> newlib syscall glue
 *
 *  A newlib --disable-newlib-supplied-syscalls beállítással épül, így ezeket a
 *  függvényeket tőlünk várja. Mindegyik a RexOS natív syscalljaira képez
 *  (lásd rexos_syscall.h és kernel/arch/x86_64/syscall.c).
 *
 *  Fontos eltérések a kernel és a POSIX szemantika közt, amiket itt hidalunk át:
 *   - a kernel SYS_SEEK csak abszolút offsetet tud -> userspace árnyék-offset
 *     tábla kell a SEEK_CUR/SEEK_END-hez
 *   - a kernel O_CREAT értéke 0x40, a newlibé 0x200 -> fordítás
 *   - stdin: a kernel SYS_READ addig blokkol, amíg 'count' byte össze nem
 *     gyűlik -> fd 0-nál egyszerre csak 1 byte-ot kérünk
 * ============================================================================= */
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/times.h>
#include <sys/time.h>
#include <sys/fcntl.h>
#include <stddef.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include "rexos_syscall.h"

#undef errno
extern int errno;

/* environ-t a newlib adja (libc/stdlib/environ.c) */

#define MAX_FD 64

/* Árnyék offsetek: a kernel is vezet offsetet, de lekérdezni nem tudjuk,
 * ezért minden mozgást itt is követünk, és seekkor szinkronizálunk. */
static uint64_t s_offset[MAX_FD];

/* --- Processz ------------------------------------------------------------- */

void _exit(int code)
{
    rexos_syscall1(RSYS_EXIT, (uint64_t)code);
    for (;;) { }
}

int getpid(void) { return 1; }

int kill(int pid, int sig)
{
    (void)pid; (void)sig;
    errno = ENOSYS;
    return -1;
}

int fork(void)  { errno = ENOSYS; return -1; }

int execve(const char *name, char *const argv[], char *const env[])
{
    (void)name; (void)argv; (void)env;
    errno = ENOSYS;
    return -1;
}

int wait(int *status) { (void)status; errno = ENOSYS; return -1; }

/* --- Fájl I/O -------------------------------------------------------------- */

int open(const char *name, int flags, ...)
{
    uint64_t kflags = 0;
    if (flags & O_CREAT) kflags |= RSYS_O_CREAT;

    int fd = (int)rexos_syscall2(RSYS_OPEN, (uint64_t)name, kflags);
    if (fd < 0) {
        errno = ENOENT;
        return -1;
    }
    if (fd < MAX_FD) {
        s_offset[fd] = 0;
        if (flags & O_APPEND) {
            rexos_stat_t st;
            if ((int)rexos_syscall2(RSYS_FSTAT, (uint64_t)fd, (uint64_t)&st) == 0) {
                s_offset[fd] = st.length;
                rexos_syscall2(RSYS_SEEK, (uint64_t)fd, st.length);
            }
        }
    }
    return fd;
}

/* Socket fd-k (1000+) lezárása a rexos_sock.c-ben; gyenge default, hogy a
 * socket réteg nélkül linkelt programok is működjenek. */
int __attribute__((weak)) __rexos_sock_close(int fd)
{
    (void)fd;
    return -1;
}

int close(int fd)
{
    if (fd >= 1000) return __rexos_sock_close(fd);
    if (fd < 3) return 0;
    return (int)rexos_syscall1(RSYS_CLOSE, (uint64_t)fd);
}

_READ_WRITE_RETURN_TYPE read(int fd, void *buf, size_t count)
{
    if (fd == 0) {
        /* stdin: a kernel count byte-ig blokkolna; 1 byte-onként olvasunk */
        if (count == 0) return 0;
        int r = (int)rexos_syscall3(RSYS_READ, 0, (uint64_t)buf, 1);
        return (r < 0) ? -1 : r;
    }
    int r = (int)rexos_syscall3(RSYS_READ, (uint64_t)fd, (uint64_t)buf, count);
    if (r > 0 && fd < MAX_FD) s_offset[fd] += (uint64_t)r;
    return r;
}

_READ_WRITE_RETURN_TYPE write(int fd, const void *buf, size_t count)
{
    int r = (int)rexos_syscall3(RSYS_WRITE, (uint64_t)fd, (uint64_t)buf, count);
    if (r > 0 && fd >= 3 && fd < MAX_FD) s_offset[fd] += (uint64_t)r;
    return r;
}

off_t lseek(int fd, off_t offset, int whence)
{
    if (fd < 3 || fd >= MAX_FD) { errno = EBADF; return (off_t)-1; }

    uint64_t target;
    switch (whence) {
    case SEEK_SET:
        target = (uint64_t)offset;
        break;
    case SEEK_CUR:
        target = s_offset[fd] + (uint64_t)offset;
        break;
    case SEEK_END: {
        rexos_stat_t st;
        if ((int)rexos_syscall2(RSYS_FSTAT, (uint64_t)fd, (uint64_t)&st) != 0) {
            errno = EBADF;
            return (off_t)-1;
        }
        target = st.length + (uint64_t)offset;
        break;
    }
    default:
        errno = EINVAL;
        return (off_t)-1;
    }

    if ((int)rexos_syscall2(RSYS_SEEK, (uint64_t)fd, target) < 0) {
        errno = EBADF;
        return (off_t)-1;
    }
    s_offset[fd] = target;
    return (off_t)target;
}

static void fill_stat(struct stat *st, const rexos_stat_t *rst)
{
    memset(st, 0, sizeof(*st));
    st->st_size  = (off_t)rst->length;
    st->st_ino   = rst->inode;
    st->st_mode  = (rst->flags & 0x2) ? (S_IFDIR | 0755) : (S_IFREG | 0644);
    st->st_nlink = 1;
    st->st_blksize = 512;
    st->st_blocks  = (rst->length + 511) / 512;
}

int fstat(int fd, struct stat *st)
{
    if (fd <= 2) {
        /* stdio: karakteres eszköz -> a newlib sor-bufferelést használ */
        memset(st, 0, sizeof(*st));
        st->st_mode = S_IFCHR | 0666;
        return 0;
    }
    rexos_stat_t rst;
    if ((int)rexos_syscall2(RSYS_FSTAT, (uint64_t)fd, (uint64_t)&rst) != 0) {
        errno = EBADF;
        return -1;
    }
    fill_stat(st, &rst);
    return 0;
}

int stat(const char *path, struct stat *st)
{
    rexos_stat_t rst;
    if ((int)rexos_syscall2(RSYS_STAT, (uint64_t)path, (uint64_t)&rst) != 0) {
        errno = ENOENT;
        return -1;
    }
    fill_stat(st, &rst);
    return 0;
}

int isatty(int fd) { return fd <= 2 ? 1 : 0; }

int access(const char *path, int mode)
{
    (void)mode; /* a RexOS VFS-ben nincs jogosultságkezelés: létezés = elérhető */
    rexos_stat_t st;
    if ((int)rexos_syscall2(RSYS_STAT, (uint64_t)path, (uint64_t)&st) != 0) {
        errno = ENOENT;
        return -1;
    }
    return 0;
}

int unlink(const char *name)
{
    if ((int)rexos_syscall1(RSYS_UNLINK, (uint64_t)name) != 0) {
        errno = ENOENT;
        return -1;
    }
    return 0;
}

int link(const char *old, const char *new)
{
    (void)old; (void)new;
    errno = ENOSYS;
    return -1;
}

int mkdir(const char *path, mode_t mode)
{
    (void)mode;
    if ((int)rexos_syscall1(RSYS_MKDIR, (uint64_t)path) != 0) {
        errno = EEXIST;
        return -1;
    }
    return 0;
}

/* --- Memória ---------------------------------------------------------------- */

void *sbrk(ptrdiff_t incr)
{
    uint64_t old_brk = rexos_syscall1(RSYS_BRK, 0);
    if (incr == 0) return (void *)old_brk;
    uint64_t new_brk = rexos_syscall1(RSYS_BRK, old_brk + (uint64_t)incr);
    if (new_brk == old_brk && incr > 0) {
        errno = ENOMEM;
        return (void *)-1;
    }
    return (void *)old_brk;
}

/* --- Idő -------------------------------------------------------------------- */

/* Napok száma 1970-01-01 óta (civil naptár -> epoch nap, Howard Hinnant-féle
 * algoritmus egyszerűsítve). */
static int64_t days_from_civil(int64_t y, unsigned m, unsigned d)
{
    y -= m <= 2;
    int64_t era = (y >= 0 ? y : y - 399) / 400;
    unsigned yoe = (unsigned)(y - era * 400);
    unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + (int64_t)doe - 719468;
}

int gettimeofday(struct timeval *tv, void *tz)
{
    (void)tz;
    if (!tv) return 0;

    /* Ezredmásodperc-felbontás kell (a NetSurf ütemezője ms-alapú timerekre
     * épül; másodperc-granularitással minden 10 ms-os callback ~1 s-t
     * csúszna). Bázis: RTC az első híváskor, onnantól PIT tick (10 ms). */
    static int64_t  base_sec = -1;
    static uint64_t base_ticks;

    if (base_sec < 0) {
        rexos_rtc_time_t t;
        rexos_syscall1(RSYS_TIME, (uint64_t)&t);
        int64_t days = days_from_civil((int64_t)t.year, t.month, t.day);
        base_sec = days * 86400 + t.hour * 3600 + t.minute * 60 + t.second;
        base_ticks = rexos_syscall1(RSYS_TICKS, 0);
    }

    uint64_t dticks = rexos_syscall1(RSYS_TICKS, 0) - base_ticks; /* 100 Hz */
    tv->tv_sec  = base_sec + (int64_t)(dticks / 100);
    tv->tv_usec = (suseconds_t)((dticks % 100) * 10000);
    return 0;
}

clock_t times(struct tms *buf)
{
    uint64_t ticks = rexos_syscall1(RSYS_TICKS, 0); /* 100 Hz PIT */
    if (buf) {
        buf->tms_utime  = (clock_t)ticks;
        buf->tms_stime  = 0;
        buf->tms_cutime = 0;
        buf->tms_cstime = 0;
    }
    return (clock_t)ticks;
}

/* newlib clock()/CLOCKS_PER_SEC alapértelmezése 100 - pont a PIT frekvenciánk. */

unsigned int sleep(unsigned int seconds)
{
    rexos_syscall1(RSYS_SLEEP_MS, (uint64_t)seconds * 1000);
    return 0;
}

int usleep(useconds_t usec)
{
    rexos_syscall1(RSYS_SLEEP_MS, (uint64_t)usec / 1000);
    return 0;
}
