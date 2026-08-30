/* Mini-iconv a RexOS-hez (a GNU libiconv helyett).
 * A NetSurf framebuffer frontend UTF-8 lokális charsettel fut, így a
 * gyakorlatban identitás-konverziók történnek; ezen felül a Latin-1 <-> UTF-8
 * irányokat támogatjuk. Ismeretlen kódolásra az iconv_open hibát ad, amit a
 * hívók (pl. netsurf utils/utf8.c) kezelnek. */
#ifndef REXOS_ICONV_H
#define REXOS_ICONV_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef void *iconv_t;

iconv_t iconv_open(const char *tocode, const char *fromcode);
size_t  iconv(iconv_t cd, char **inbuf, size_t *inbytesleft,
              char **outbuf, size_t *outbytesleft);
int     iconv_close(iconv_t cd);

#ifdef __cplusplus
}
#endif

#endif /* REXOS_ICONV_H */
