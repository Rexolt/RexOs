/* =============================================================================
 *  RexOS - DEFLATE / gzip / zlib dekompresszió
 *
 *  A zlib `puff.c` (Mark Adler, public domain) algoritmusát követi, de
 *  setjmp/longjmp helyett explicit hibakezelést használ (a freestanding
 *  user-libc-nek nincs setjmp-je). RFC 1951 (DEFLATE), 1950 (zlib), 1952 (gzip).
 * ========================================================================== */
#include "inflate.h"
#include "libc.h"

#define MAXBITS   15    /* max bits egy kódban */
#define MAXLCODES 286   /* max literal/length kódok száma */
#define MAXDCODES 30    /* max distance kódok száma */
#define MAXCODES  (MAXLCODES + MAXDCODES)
#define FIXLCODES 288   /* fix literal/length kódok száma */

typedef struct {
    /* kimenet */
    uint8_t      *out;
    unsigned long outlen;
    unsigned long outcnt;
    /* bemenet */
    const uint8_t *in;
    unsigned long  inlen;
    unsigned long  incnt;
    /* bit-akkumulátor */
    int  bitbuf;
    int  bitcnt;
    int  err;       /* 1 = bemenet elfogyott bit-olvasás közben */
} inf_state;

typedef struct {
    short *count;   /* count[len] = adott hosszú kódok száma */
    short *symbol;  /* kanonikus sorrendben a szimbólumok */
} huffman;

/* `need` bit kiolvasása LSB-first sorrendben. Hiba esetén err=1, és 0-t ad. */
static int inf_bits(inf_state *s, int need) {
    long val = s->bitbuf;
    while (s->bitcnt < need) {
        if (s->incnt == s->inlen) { s->err = 1; return 0; }
        val |= (long)(s->in[s->incnt++]) << s->bitcnt;
        s->bitcnt += 8;
    }
    s->bitbuf = (int)(val >> need);
    s->bitcnt -= need;
    return (int)(val & ((1L << need) - 1));
}

/* "Stored" (tömörítetlen) blokk. */
static int inf_stored(inf_state *s) {
    unsigned len;
    /* byte-határra igazítás */
    s->bitbuf = 0;
    s->bitcnt = 0;
    if (s->incnt + 4 > s->inlen) return 2;
    len  = s->in[s->incnt++];
    len |= (unsigned)s->in[s->incnt++] << 8;
    /* a következő 2 byte a ~len; nem ellenőrizzük, csak átugorjuk */
    s->incnt += 2;
    if (s->incnt + len > s->inlen) return 2;
    while (len--) {
        if (s->outcnt == s->outlen) return 1;  /* kimenet betelt */
        s->out[s->outcnt++] = s->in[s->incnt++];
    }
    return 0;
}

/* Egy szimbólum dekódolása a megadott Huffman-kód alapján. */
static int inf_decode(inf_state *s, const huffman *h) {
    int code  = 0;
    int first = 0;
    int index = 0;
    for (int len = 1; len <= MAXBITS; len++) {
        code |= inf_bits(s, 1);
        if (s->err) return -99;
        int count = h->count[len];
        if (code - count < first) return h->symbol[index + (code - first)];
        index += count;
        first += count;
        first <<= 1;
        code  <<= 1;
    }
    return -9;   /* hibás kód */
}

/* Kanonikus Huffman-tábla felépítése a kódhosszakból. */
static int inf_construct(huffman *h, const short *length, int n) {
    short offs[MAXBITS + 1];

    for (int len = 0; len <= MAXBITS; len++) h->count[len] = 0;
    for (int symbol = 0; symbol < n; symbol++) h->count[length[symbol]]++;
    if (h->count[0] == n) return 0;   /* nincs kód, de érvényes (üres) */

    int left = 1;
    for (int len = 1; len <= MAXBITS; len++) {
        left <<= 1;
        left -= h->count[len];
        if (left < 0) return left;    /* túl sok kód adott hosszra */
    }

    offs[1] = 0;
    for (int len = 1; len < MAXBITS; len++)
        offs[len + 1] = offs[len] + h->count[len];

    for (int symbol = 0; symbol < n; symbol++)
        if (length[symbol] != 0) h->symbol[offs[length[symbol]]++] = (short)symbol;

    return left;   /* 0 = teljes; >0 = nem teljes (egy kód is lehet) */
}

/* length/distance dekódoló táblák (RFC 1951 3.2.5). */
static const short inf_lens[29] = {
    3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31,
    35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258 };
static const short inf_lext[29] = {
    0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2,
    3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0 };
static const short inf_dists[30] = {
    1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193,
    257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145,
    8193, 12289, 16385, 24577 };
static const short inf_dext[30] = {
    0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6,
    7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13 };

/* Egy blokk literal/length/distance kódjainak feldolgozása. */
static int inf_codes(inf_state *s, const huffman *lencode, const huffman *distcode) {
    int symbol;
    do {
        symbol = inf_decode(s, lencode);
        if (symbol < 0) return symbol;
        if (symbol < 256) {
            if (s->outcnt == s->outlen) return 1;   /* betelt */
            s->out[s->outcnt++] = (uint8_t)symbol;
        } else if (symbol > 256) {
            symbol -= 257;
            if (symbol >= 29) return -10;
            int len = inf_lens[symbol] + inf_bits(s, inf_lext[symbol]);
            if (s->err) return -99;

            symbol = inf_decode(s, distcode);
            if (symbol < 0) return symbol;
            if (symbol >= 30) return -11;
            unsigned long dist = inf_dists[symbol] + (unsigned long)inf_bits(s, inf_dext[symbol]);
            if (s->err) return -99;
            if (dist > s->outcnt) return -12;        /* túl messzire mutat vissza */

            if (s->outcnt + (unsigned long)len > s->outlen) {
                /* nem fér ki: kitöltjük ami belefér, és csonkoltként térünk vissza */
                while (s->outcnt < s->outlen) {
                    s->out[s->outcnt] = s->out[s->outcnt - dist];
                    s->outcnt++;
                }
                return 1;
            }
            while (len--) {
                s->out[s->outcnt] = s->out[s->outcnt - dist];
                s->outcnt++;
            }
        }
    } while (symbol != 256);   /* 256 = end-of-block */
    return 0;
}

/* Fix Huffman-táblás blokk (RFC 1951 3.2.6). */
static int inf_fixed(inf_state *s) {
    static int    virgin = 1;
    static short  lencnt[MAXBITS + 1],  lensym[FIXLCODES];
    static short  distcnt[MAXBITS + 1], distsym[MAXDCODES];
    static huffman lencode, distcode;

    if (virgin) {
        short lengths[FIXLCODES];
        int symbol;
        lencode.count   = lencnt;  lencode.symbol  = lensym;
        distcode.count  = distcnt; distcode.symbol = distsym;
        for (symbol = 0;   symbol < 144; symbol++) lengths[symbol] = 8;
        for (;             symbol < 256; symbol++) lengths[symbol] = 9;
        for (;             symbol < 280; symbol++) lengths[symbol] = 7;
        for (;             symbol < FIXLCODES; symbol++) lengths[symbol] = 8;
        inf_construct(&lencode, lengths, FIXLCODES);
        for (symbol = 0; symbol < MAXDCODES; symbol++) lengths[symbol] = 5;
        inf_construct(&distcode, lengths, MAXDCODES);
        virgin = 0;
    }
    return inf_codes(s, &lencode, &distcode);
}

/* Dinamikus Huffman-táblás blokk (RFC 1951 3.2.7). */
static int inf_dynamic(inf_state *s) {
    static const short order[19] = {
        16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15 };
    short lengths[MAXCODES];
    short lencnt[MAXBITS + 1],  lensym[MAXLCODES];
    short distcnt[MAXBITS + 1], distsym[MAXDCODES];
    huffman lencode  = { lencnt,  lensym  };
    huffman distcode = { distcnt, distsym };

    int nlen  = inf_bits(s, 5) + 257;
    int ndist = inf_bits(s, 5) + 1;
    int ncode = inf_bits(s, 4) + 4;
    if (s->err) return -99;
    if (nlen > MAXLCODES || ndist > MAXDCODES) return -3;

    int index;
    for (index = 0; index < ncode; index++) {
        lengths[order[index]] = (short)inf_bits(s, 3);
    }
    for (; index < 19; index++) lengths[order[index]] = 0;
    if (s->err) return -99;

    int err = inf_construct(&lencode, lengths, 19);
    if (err != 0) return -4;   /* a code-length kódnak teljesnek kell lennie */

    index = 0;
    while (index < nlen + ndist) {
        int symbol = inf_decode(s, &lencode);
        if (symbol < 0) return symbol;
        if (symbol < 16) {
            lengths[index++] = (short)symbol;
        } else {
            int len = 0;
            if (symbol == 16) {
                if (index == 0) return -5;
                len = lengths[index - 1];
                symbol = 3 + inf_bits(s, 2);
            } else if (symbol == 17) {
                symbol = 3 + inf_bits(s, 3);
            } else {
                symbol = 11 + inf_bits(s, 7);
            }
            if (s->err) return -99;
            if (index + symbol > nlen + ndist) return -6;
            while (symbol--) lengths[index++] = (short)len;
        }
    }

    if (lengths[256] == 0) return -9;   /* nincs end-of-block kód */

    err = inf_construct(&lencode, lengths, nlen);
    if (err < 0 || (err > 0 && nlen - lencode.count[0] != 1)) return -7;
    err = inf_construct(&distcode, lengths + nlen, ndist);
    if (err < 0 || (err > 0 && ndist - distcode.count[0] != 1)) return -8;

    return inf_codes(s, &lencode, &distcode);
}

int rex_inflate(uint8_t *dest, unsigned long *destlen,
                const uint8_t *source, unsigned long srclen) {
    inf_state s;
    s.out = dest;   s.outlen = *destlen; s.outcnt = 0;
    s.in  = source; s.inlen  = srclen;   s.incnt  = 0;
    s.bitbuf = 0;   s.bitcnt = 0;        s.err    = 0;

    int last, type, err = 0;
    do {
        last = inf_bits(&s, 1);
        type = inf_bits(&s, 2);
        if (s.err) { err = 2; break; }
        if      (type == 0) err = inf_stored(&s);
        else if (type == 1) err = inf_fixed(&s);
        else if (type == 2) err = inf_dynamic(&s);
        else                err = -1;     /* type==3 érvénytelen */
        if (err != 0) break;
    } while (!last);

    *destlen = s.outcnt;
    return err;
}

int rex_gunzip(uint8_t *dest, unsigned long *destlen,
               const uint8_t *source, unsigned long srclen) {
    const uint8_t *p = source;
    unsigned long  n = srclen;

    /* gzip: 0x1f 0x8b */
    if (n >= 2 && p[0] == 0x1f && p[1] == 0x8b) {
        if (n < 10) return -20;
        uint8_t flg = p[3];
        unsigned long off = 10;
        if (flg & 0x04) {                 /* FEXTRA */
            if (off + 2 > n) return -20;
            unsigned xlen = p[off] | ((unsigned)p[off + 1] << 8);
            off += 2 + xlen;
        }
        if (flg & 0x08) { while (off < n && p[off] != 0) off++; off++; }  /* FNAME */
        if (flg & 0x10) { while (off < n && p[off] != 0) off++; off++; }  /* FCOMMENT */
        if (flg & 0x02) { off += 2; }     /* FHCRC */
        if (off >= n) return -20;
        return rex_inflate(dest, destlen, p + off, n - off);
    }

    /* zlib: CMF/FLG, CM==8 és (CMF<<8 | FLG) osztható 31-gyel */
    if (n >= 2 && (p[0] & 0x0f) == 8 &&
        (((unsigned)p[0] << 8 | p[1]) % 31) == 0) {
        return rex_inflate(dest, destlen, p + 2, n - 2);
    }

    /* különben: nyers DEFLATE */
    return rex_inflate(dest, destlen, p, n);
}
