/* Minimál <sys/mman.h> RexOS-hez.
 * Nincs igazi mmap a kernelben; a glue malloc + read alapú emulációt ad
 * (posix_extra.c), ami fájl READ-only mappelésére elég - a NetSurf pontosan
 * erre használja. */
#ifndef _SYS_MMAN_H
#define _SYS_MMAN_H

#include <sys/types.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PROT_NONE  0x0
#define PROT_READ  0x1
#define PROT_WRITE 0x2
#define PROT_EXEC  0x4

#define MAP_SHARED    0x01
#define MAP_PRIVATE   0x02
#define MAP_FIXED     0x10
#define MAP_ANON      0x20
#define MAP_ANONYMOUS MAP_ANON

#define MAP_FAILED ((void *)-1)

void *mmap(void *addr, size_t len, int prot, int flags, int fd, off_t off);
int   munmap(void *addr, size_t len);

#ifdef __cplusplus
}
#endif

#endif /* _SYS_MMAN_H */
