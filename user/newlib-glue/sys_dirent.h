/* RexOS <sys/dirent.h> - a newlib <dirent.h> wrappere ezt húzza be.
 * A tools/build-ports.sh telepíti a toolchain x86_64-elf/include/sys/ alá. */
#ifndef _SYS_DIRENT_H
#define _SYS_DIRENT_H

#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MAXNAMLEN 127

struct dirent {
    ino_t d_ino;
    char  d_name[MAXNAMLEN + 1];
};

typedef struct _DIR DIR;

DIR           *opendir(const char *name);
struct dirent *readdir(DIR *dir);
int            closedir(DIR *dir);
void           rewinddir(DIR *dir);
int            dirfd(DIR *dir);
int            alphasort(const struct dirent **a, const struct dirent **b);
int            scandir(const char *path, struct dirent ***namelist,
                       int (*filter)(const struct dirent *),
                       int (*compar)(const struct dirent **,
                                     const struct dirent **));

#ifdef __cplusplus
}
#endif

#endif /* _SYS_DIRENT_H */
