/* opendir/readdir/closedir a RexOS SYS_GETDENTS syscallra építve. */
#include <sys/types.h>
#include <sys/stat.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include "rexos_syscall.h"

/* A kernel dirent_t-je (kernel/arch/x86_64/syscall.c SYS_GETDENTS) */
typedef struct {
    char     name[128];
    uint32_t ino;
} rexos_dirent_t;

struct _DIR {
    int fd;
    struct dirent ent;
};

DIR *opendir(const char *name)
{
    rexos_stat_t st;
    if ((int)rexos_syscall2(RSYS_STAT, (uint64_t)name, (uint64_t)&st) != 0 ||
        !(st.flags & 0x2)) {  /* FS_DIRECTORY */
        errno = ENOTDIR;
        return NULL;
    }
    int fd = (int)rexos_syscall2(RSYS_OPEN, (uint64_t)name, 0);
    if (fd < 0) {
        errno = ENOENT;
        return NULL;
    }
    DIR *d = malloc(sizeof(*d));
    if (!d) {
        rexos_syscall1(RSYS_CLOSE, (uint64_t)fd);
        errno = ENOMEM;
        return NULL;
    }
    d->fd = fd;
    return d;
}

struct dirent *readdir(DIR *d)
{
    if (!d) { errno = EBADF; return NULL; }
    rexos_dirent_t rd;
    /* a kernel az fd_offset-et használja iterátorként, és lépteti is */
    if ((int)rexos_syscall2(RSYS_GETDENTS, (uint64_t)d->fd, (uint64_t)&rd) != 1) {
        return NULL;
    }
    d->ent.d_ino = rd.ino;
    strncpy(d->ent.d_name, rd.name, MAXNAMLEN);
    d->ent.d_name[MAXNAMLEN] = 0;
    return &d->ent;
}

void rewinddir(DIR *d)
{
    if (d) rexos_syscall2(RSYS_SEEK, (uint64_t)d->fd, 0);
}

int closedir(DIR *d)
{
    if (!d) { errno = EBADF; return -1; }
    rexos_syscall1(RSYS_CLOSE, (uint64_t)d->fd);
    free(d);
    return 0;
}

int dirfd(DIR *d)
{
    if (!d) { errno = EBADF; return -1; }
    return d->fd;
}

int alphasort(const struct dirent **a, const struct dirent **b)
{
    return strcmp((*a)->d_name, (*b)->d_name);
}

int scandir(const char *path, struct dirent ***namelist,
            int (*filter)(const struct dirent *),
            int (*compar)(const struct dirent **, const struct dirent **))
{
    DIR *d = opendir(path);
    if (!d) return -1;

    struct dirent **list = NULL;
    int n = 0, cap = 0;
    struct dirent *e;
    while ((e = readdir(d)) != NULL) {
        if (filter && !filter(e)) continue;
        if (n == cap) {
            cap = cap ? cap * 2 : 16;
            struct dirent **nl = realloc(list, (size_t)cap * sizeof(*nl));
            if (!nl) goto oom;
            list = nl;
        }
        struct dirent *copy = malloc(sizeof(*copy));
        if (!copy) goto oom;
        *copy = *e;
        list[n++] = copy;
    }
    closedir(d);

    if (compar && n > 1) {
        qsort(list, (size_t)n, sizeof(*list),
              (int (*)(const void *, const void *))compar);
    }
    *namelist = list;
    return n;

oom:
    for (int i = 0; i < n; i++) free(list[i]);
    free(list);
    closedir(d);
    errno = ENOMEM;
    return -1;
}
