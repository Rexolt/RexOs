/* Rex OS - freestanding libc-replacement (k* = kernel) */
#pragma once
#include <rexos/types.h>

void  *kmemset (void *dest, int c, size_t n);
void  *kmemcpy (void *dest, const void *src, size_t n);
void  *kmemmove(void *dest, const void *src, size_t n);
int    kmemcmp (const void *a, const void *b, size_t n);
size_t kstrlen (const char *s);
int    kstrcmp (const char *a, const char *b);
char  *kstrcpy (char *dest, const char *src);
char  *kstrncpy(char *dest, const char *src, size_t n);
