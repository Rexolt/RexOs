/* =============================================================================
 *  RexOS - User-space TLS (BearSSL minimal client)
 *
 *  Skeleton (Fázis 3 első lépcső). Ami már működik:
 *    - br_ssl_client_init_full + X.509 minimal verifier
 *    - SNI a `br_ssl_client_reset(ctx, host, 0)` hívással
 *    - Aszinkron engine-loop (SENDREC/RECVREC) a kernel net_* szinkron API-jára
 *      illesztve
 *    - Bidi I/O buffer (BR_SSL_BUFSIZE_BIDI)
 *
 *  Ami még NEM működik / TODO:
 *    - Entrópia ideiglenes: rdtsc + tick + cím-jitter. Kriptográfiailag GYENGE.
 *      Valódi rdrand-ot vagy a kerneltől kért HW seed-et kell hozzátenni.
 *    - CA bundle: amíg `ca_bundle.c` placeholder (TAs_NUM=0), minden cert
 *      BR_ERR_X509_NOT_TRUSTED-del el fog bukni. Futtasd: ./tools/gen_ca_bundle.sh
 *    - Idő/RTC: a tanusítvány validity check `br_x509_minimal_set_time`-mal
 *      állítható. Most a default = 0 (UNIX epoch), tehát "notBefore" majdnem
 *      mindig OK lesz, "notAfter" mindig OK -> ezt is fixálni kell, mihelyt
 *      a get_time() pontos UTC napokat ad.
 *    - tls_send/recv hibakezelés: csak alap, retry/timeout nincs.
 * ========================================================================== */
#include "tls.h"
#include "libc.h"
#include "bearssl.h"

/* CA bundle (ca_bundle.c) - üres placeholder, amíg a gen script nem fut. */
extern const br_x509_trust_anchor TAs[];
extern const size_t TAs_NUM;

/* ---- Belső kontextus ---------------------------------------------------- */
struct tls_ctx {
    br_ssl_client_context  cc;
    br_x509_minimal_context xc;
    uint64_t  sock;        /* net_connect handle */
    int       last_err;    /* BearSSL engine state error code, 0 = OK */
    uint8_t   iobuf[BR_SSL_BUFSIZE_BIDI];
};

/* ---- Entrópia (GYENGE - csak fejlesztéshez) ----------------------------- */
static uint64_t tls_rdtsc(void) {
    uint32_t lo, hi;
    __asm__ __volatile__("rdtsc" : "=a"(lo), "=d"(hi));
    return ((uint64_t)hi << 32) | lo;
}

/* ---- X.509 érvényesség-idő beállítása (RTC -> BearSSL napok+másodpercek) -- */
/* BearSSL formátuma: (days, seconds), ahol days = nap a 0000-01-01-tól.
 * Howard Hinnant proleptic Gregorian képlettel unix_days-t számolunk, majd
 * +719528 eltolás. */
static void tls_set_validation_time(tls_ctx_t *ctx) {
    rtc_time_t t;
    get_time(&t);
    int y = (int)t.year, m = (int)t.month, d = (int)t.day;
    /* Sanity: ha az RTC nem inicializált, használjunk biztos defaultot. */
    if (y < 2024 || y > 2100 || m < 1 || m > 12 || d < 1 || d > 31) {
        y = 2025; m = 6; d = 1; t.hour = 0; t.minute = 0; t.second = 0;
    }
    int adj_y = y - (m <= 2 ? 1 : 0);
    int era   = (adj_y >= 0 ? adj_y : adj_y - 399) / 400;
    int yoe   = adj_y - era * 400;
    int doy   = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    int doe   = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    long unix_days = (long)era * 146097 + doe - 719468;
    uint32_t bearssl_days = (uint32_t)(unix_days + 719528);
    uint32_t seconds = (uint32_t)t.hour * 3600u
                     + (uint32_t)t.minute * 60u
                     + (uint32_t)t.second;
    br_x509_minimal_set_time(&ctx->xc, bearssl_days, seconds);
}

static void tls_collect_entropy(uint8_t out[32]) {
    /* Naiv keverés: rdtsc minden iterációban, ticks(), és pár stack-cím.
     * Kifejezetten NEM elég TLS-hez! Csak skeletonnak. */
    uint64_t pool[4] = { 0, 0, 0, 0 };
    for (int i = 0; i < 64; i++) {
        pool[0] ^= tls_rdtsc();
        pool[1] = (pool[1] << 7) ^ get_ticks();
        pool[2] += (uint64_t)(uintptr_t)&pool;
        pool[3] = (pool[3] * 1103515245ULL + 12345ULL) ^ tls_rdtsc();
        yield();
    }
    memcpy(out, pool, 32);
}

/* ---- BearSSL engine loop ----------------------------------------------- */
/* A BearSSL engine állapotgépét hajtjuk a kernel net_* szinkron API-jára.
 *
 *   wait_for: BR_SSL_SENDAPP -> handshake vagy küldés vége
 *             BR_SSL_RECVAPP -> beérkezett app data
 *             0              -> bármi haladás (handshake során)
 *
 * Visszatérés: 1 ha a kívánt állapot elérhető, 0 ha az engine lezárult/hibázott.
 */
static int tls_run_until(tls_ctx_t *ctx, unsigned wait_for) {
    br_ssl_engine_context *eng = &ctx->cc.eng;
    while (1) {
        unsigned st = br_ssl_engine_current_state(eng);

        if (st & BR_SSL_CLOSED) {
            ctx->last_err = br_ssl_engine_last_error(eng);
            return 0;
        }

        if (wait_for && (st & wait_for)) return 1;

        if (st & BR_SSL_SENDREC) {
            size_t slen;
            uint8_t *sbuf = br_ssl_engine_sendrec_buf(eng, &slen);
            int sent = net_send(ctx->sock, sbuf, slen);
            if (sent <= 0) {
                ctx->last_err = BR_ERR_IO;
                return 0;
            }
            br_ssl_engine_sendrec_ack(eng, (size_t)sent);
            continue;
        }

        if (st & BR_SSL_RECVREC) {
            size_t rlen;
            uint8_t *rbuf = br_ssl_engine_recvrec_buf(eng, &rlen);
            int got = net_recv(ctx->sock, rbuf, rlen);
            if (got < 0) {
                ctx->last_err = BR_ERR_IO;
                return 0;
            }
            if (got == 0) {
                yield();
                continue;
            }
            br_ssl_engine_recvrec_ack(eng, (size_t)got);
            continue;
        }

        /* SENDAPP vagy RECVAPP, és nem várjuk (wait_for=0): térjünk vissza. */
        if (!wait_for) return 1;

        yield();
    }
}

/* ---- Public API -------------------------------------------------------- */

/* Egyszerű serial-debug log (write(1,...) -> kprintf a kernelben). */
#define TLS_DBG 1
static void tdbg(const char *s) {
#if TLS_DBG
    int n = 0; while (s[n]) n++;
    write(1, s, (uint64_t)n);
#else
    (void)s;
#endif
}
static void tdbg_int(int v) {
#if TLS_DBG
    char buf[16]; int i = 0;
    if (v < 0) { write(1, "-", 1); v = -v; }
    if (v == 0) { write(1, "0", 1); return; }
    while (v > 0 && i < 15) { buf[i++] = '0' + (v % 10); v /= 10; }
    char rev[16]; for (int j = 0; j < i; j++) rev[j] = buf[i - 1 - j];
    write(1, rev, (uint64_t)i);
#else
    (void)v;
#endif
}

tls_ctx_t *tls_connect(const char *host, uint16_t port) {
    if (!host) return 0;

    tdbg("[tls] connect "); tdbg(host); tdbg(":"); tdbg_int((int)port); tdbg("\n");
    tls_ctx_t *ctx = (tls_ctx_t *)malloc(sizeof(tls_ctx_t));
    if (!ctx) { tdbg("[tls] malloc(tls_ctx_t) FAILED\n"); return 0; }
    memset(ctx, 0, sizeof(*ctx));

    /* 1) TCP kapcsolat */
    ctx->sock = net_connect(host, port);
    if (ctx->sock == (uint64_t)-1 || ctx->sock == 0) {
        tdbg("[tls] net_connect FAILED\n");
        free(ctx);
        return 0;
    }
    tdbg("[tls] tcp ok, init bearssl\n");

    /* 2) BearSSL client init (teljes profil: minden főbb cipher + X.509 minimal) */
    br_ssl_client_init_full(&ctx->cc, &ctx->xc, TAs, TAs_NUM);

    /* 2b) Tanúsítvány érvényességi idő az RTC-ből, különben BR_ERR_X509_TIME_UNKNOWN */
    tls_set_validation_time(ctx);

    /* 3) Entrópia injektálás (GYENGE - lásd TODO) */
    uint8_t seed[32];
    tls_collect_entropy(seed);
    br_ssl_engine_inject_entropy(&ctx->cc.eng, seed, sizeof(seed));

    /* 4) Bidi I/O buffer beállítása */
    br_ssl_engine_set_buffer(&ctx->cc.eng, ctx->iobuf, sizeof(ctx->iobuf), 1);

    /* 5) Handshake indítás SNI-vel */
    if (!br_ssl_client_reset(&ctx->cc, host, 0)) {
        ctx->last_err = br_ssl_engine_last_error(&ctx->cc.eng);
        tdbg("[tls] client_reset FAILED err="); tdbg_int((int)ctx->last_err); tdbg("\n");
        net_close(ctx->sock);
        free(ctx);
        return 0;
    }
    tdbg("[tls] handshake start\n");

    /* 6) Engine pörgetése amíg SENDAPP nem jön (handshake kész) */
    if (!tls_run_until(ctx, BR_SSL_SENDAPP)) {
        tdbg("[tls] handshake FAILED err="); tdbg_int((int)ctx->last_err); tdbg("\n");
        net_close(ctx->sock);
        free(ctx);
        return 0;
    }
    tdbg("[tls] handshake OK\n");

    return ctx;
}

int tls_send(tls_ctx_t *ctx, const void *buf, uint64_t len) {
    if (!ctx || !buf) return TLS_ERR_IO;
    br_ssl_engine_context *eng = &ctx->cc.eng;
    const uint8_t *p = (const uint8_t *)buf;
    uint64_t left = len;

    while (left > 0) {
        if (!tls_run_until(ctx, BR_SSL_SENDAPP)) return TLS_ERR_IO;
        size_t alen;
        uint8_t *abuf = br_ssl_engine_sendapp_buf(eng, &alen);
        if (!abuf || alen == 0) return TLS_ERR_IO;
        size_t copy = (left < alen) ? (size_t)left : alen;
        memcpy(abuf, p, copy);
        br_ssl_engine_sendapp_ack(eng, copy);
        br_ssl_engine_flush(eng, 0); /* azonnali rekord-kibocsátás */
        p    += copy;
        left -= copy;
    }
    /* Pörgetjük az engine-t, hogy a kimenő rekordok valóban kimenjenek */
    tls_run_until(ctx, 0);
    return (int)len;
}

int tls_recv(tls_ctx_t *ctx, void *buf, uint64_t max) {
    if (!ctx || !buf || max == 0) return TLS_ERR_IO;
    br_ssl_engine_context *eng = &ctx->cc.eng;

    if (!tls_run_until(ctx, BR_SSL_RECVAPP)) {
        /* Lehet, hogy a peer szabályosan lezárta */
        if (ctx->last_err == 0) return 0;
        return TLS_ERR_IO;
    }

    size_t alen;
    uint8_t *abuf = br_ssl_engine_recvapp_buf(eng, &alen);
    if (!abuf || alen == 0) return 0;

    size_t copy = (max < alen) ? (size_t)max : alen;
    memcpy(buf, abuf, copy);
    br_ssl_engine_recvapp_ack(eng, copy);
    return (int)copy;
}

void tls_close(tls_ctx_t *ctx) {
    if (!ctx) return;
    br_ssl_engine_close(&ctx->cc.eng);
    tls_run_until(ctx, 0); /* close_notify-t kiküldjük */
    if (ctx->sock != 0 && ctx->sock != (uint64_t)-1) net_close(ctx->sock);
    free(ctx);
}

int tls_last_error(const tls_ctx_t *ctx) {
    return ctx ? ctx->last_err : -1;
}
