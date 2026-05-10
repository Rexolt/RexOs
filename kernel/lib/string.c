/* Rex OS - freestanding string/memory helpers
 *
 * GCC néha implicit hívásokat generál a memset/memcpy-ra
 * (pl. struct inicializáláshoz). Ezért adunk PLAIN nevű
 * aliasokat is, hogy a linker megtalálja őket.
 */
#include <lib/string.h>

void *kmemset(void *dest, int c, size_t n)
{
    uint8_t *p = (uint8_t *)dest;
    while (n--) *p++ = (uint8_t)c;
    return dest;
}

void *kmemcpy(void *dest, const void *src, size_t n)
{
    uint8_t       *d = (uint8_t *)dest;
    const uint8_t *s = (const uint8_t *)src;
    while (n--) *d++ = *s++;
    return dest;
}

void *kmemmove(void *dest, const void *src, size_t n)
{
    uint8_t       *d = (uint8_t *)dest;
    const uint8_t *s = (const uint8_t *)src;
    if (d < s) {
        while (n--) *d++ = *s++;
    } else {
        d += n; s += n;
        while (n--) *--d = *--s;
    }
    return dest;
}

int kmemcmp(const void *a, const void *b, size_t n)
{
    const uint8_t *pa = (const uint8_t *)a;
    const uint8_t *pb = (const uint8_t *)b;
    while (n--) {
        if (*pa != *pb) return (int)*pa - (int)*pb;
        pa++; pb++;
    }
    return 0;
}

char *kstrcpy(char *dest, const char *src)
{
    char *d = dest;
    while ((*d++ = *src++) != '\0');
    return dest;
}

char *kstrncpy(char *dest, const char *src, size_t n)
{
    size_t i;
    for (i = 0; i < n && src[i] != '\0'; i++)
        dest[i] = src[i];
    for ( ; i < n; i++)
        dest[i] = '\0';
    return dest;
}

size_t kstrlen(const char *s)
{
    size_t n = 0;
    while (s[n]) n++;
    return n;
}

int kstrcmp(const char *a, const char *b)
{
    while (*a && (*a == *b)) { a++; b++; }
    return (int)(uint8_t)*a - (int)(uint8_t)*b;
}

/* --- Aliasok GCC implicit hívásokhoz --- */
void *memset (void *d, int c, size_t n)              { return kmemset(d, c, n); }
void *memcpy (void *d, const void *s, size_t n)      { return kmemcpy(d, s, n); }
void *memmove(void *d, const void *s, size_t n)      { return kmemmove(d, s, n); }
int   memcmp (const void *a, const void *b, size_t n){ return kmemcmp(a, b, n); }
