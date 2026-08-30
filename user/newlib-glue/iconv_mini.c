/* Mini-iconv implementáció (lásd iconv.h). */
#include "iconv.h"
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

enum enc { ENC_UTF8, ENC_LATIN1 };

struct cd {
    enum enc from, to;
};

static int parse_enc(const char *name, enum enc *out)
{
    /* "//TRANSLIT" / "//IGNORE" suffix levágása */
    char buf[32];
    size_t n = 0;
    for (; name[n] && name[n] != '/' && n < sizeof(buf) - 1; n++)
        buf[n] = name[n];
    buf[n] = 0;

    if (!strcasecmp(buf, "UTF-8") || !strcasecmp(buf, "UTF8")) {
        *out = ENC_UTF8;
        return 0;
    }
    if (!strcasecmp(buf, "ISO-8859-1") || !strcasecmp(buf, "ISO8859-1") ||
        !strcasecmp(buf, "LATIN1")     || !strcasecmp(buf, "US-ASCII") ||
        !strcasecmp(buf, "ASCII")      || !strcasecmp(buf, "ANSI_X3.4-1968")) {
        *out = ENC_LATIN1;
        return 0;
    }
    return -1;
}

iconv_t iconv_open(const char *tocode, const char *fromcode)
{
    enum enc from, to;
    if (parse_enc(fromcode, &from) != 0 || parse_enc(tocode, &to) != 0) {
        errno = EINVAL;
        return (iconv_t)-1;
    }
    struct cd *cd = malloc(sizeof(*cd));
    if (!cd) {
        errno = ENOMEM;
        return (iconv_t)-1;
    }
    cd->from = from;
    cd->to = to;
    return cd;
}

int iconv_close(iconv_t cdp)
{
    if (cdp == (iconv_t)-1) {
        errno = EBADF;
        return -1;
    }
    free(cdp);
    return 0;
}

size_t iconv(iconv_t cdp, char **inbuf, size_t *inleft,
             char **outbuf, size_t *outleft)
{
    if (cdp == (iconv_t)-1) {
        errno = EBADF;
        return (size_t)-1;
    }
    struct cd *cd = cdp;

    /* Állapot-reset hívás */
    if (!inbuf || !*inbuf) return 0;

    const unsigned char *in = (const unsigned char *)*inbuf;
    unsigned char *out = (unsigned char *)*outbuf;
    size_t il = *inleft, ol = *outleft;

    while (il > 0) {
        unsigned int cp;
        size_t consumed;

        /* Dekódolás */
        if (cd->from == ENC_LATIN1) {
            cp = in[0];
            consumed = 1;
        } else { /* UTF-8 */
            unsigned char c = in[0];
            if (c < 0x80) { cp = c; consumed = 1; }
            else if ((c & 0xE0) == 0xC0) { consumed = 2; cp = c & 0x1F; }
            else if ((c & 0xF0) == 0xE0) { consumed = 3; cp = c & 0x0F; }
            else if ((c & 0xF8) == 0xF0) { consumed = 4; cp = c & 0x07; }
            else { errno = EILSEQ; goto out_update; }
            if (consumed > il) { errno = EINVAL; goto out_update; }
            for (size_t k = 1; k < consumed; k++) {
                if ((in[k] & 0xC0) != 0x80) { errno = EILSEQ; goto out_update; }
                cp = (cp << 6) | (in[k] & 0x3F);
            }
        }

        /* Kódolás */
        if (cd->to == ENC_LATIN1) {
            if (ol < 1) { errno = E2BIG; goto out_update; }
            *out++ = (cp <= 0xFF) ? (unsigned char)cp : '?';
            ol--;
        } else { /* UTF-8 */
            size_t need = (cp < 0x80) ? 1 : (cp < 0x800) ? 2 :
                          (cp < 0x10000) ? 3 : 4;
            if (ol < need) { errno = E2BIG; goto out_update; }
            switch (need) {
            case 1: *out++ = (unsigned char)cp; break;
            case 2:
                *out++ = 0xC0 | (cp >> 6);
                *out++ = 0x80 | (cp & 0x3F);
                break;
            case 3:
                *out++ = 0xE0 | (cp >> 12);
                *out++ = 0x80 | ((cp >> 6) & 0x3F);
                *out++ = 0x80 | (cp & 0x3F);
                break;
            default:
                *out++ = 0xF0 | (cp >> 18);
                *out++ = 0x80 | ((cp >> 12) & 0x3F);
                *out++ = 0x80 | ((cp >> 6) & 0x3F);
                *out++ = 0x80 | (cp & 0x3F);
                break;
            }
            ol -= need;
        }

        in += consumed;
        il -= consumed;
    }

    *inbuf = (char *)in;
    *inleft = il;
    *outbuf = (char *)out;
    *outleft = ol;
    return 0;

out_update:
    *inbuf = (char *)in;
    *inleft = il;
    *outbuf = (char *)out;
    *outleft = ol;
    return (size_t)-1;
}
