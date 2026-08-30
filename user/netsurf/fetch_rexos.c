/*
 * NetSurf http/https fetcher a RexOS-hez.
 *
 * A hálózat a RexOS natív TCP syscalljain megy (a kernel oldja a DNS-t is),
 * a TLS BearSSL (a user/tls.c engine-loop mintája alapján). A NetSurf
 * fetcher API pollozó: minden poll() hívásban EGY aktív fetch halad
 * (a teljes kérés szinkron lefut - a UI eközben áll; későbbi finomítás a
 * nem blokkoló fetch állapotgép).
 *
 * Header-eket soronként továbbítjuk (FETCH_HEADER), a body-t FETCH_DATA-val;
 * a chunked kódolást itt fejtjük ki, redirectnél FETCH_REDIRECT megy.
 * Accept-Encoding-ot nem küldünk, így a szerver tömörítetlenül válaszol.
 *
 * A build-netsurf.sh másolja be content/fetchers/rexos.c néven.
 */

#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <libwapcaplet/libwapcaplet.h>

#include "utils/nsurl.h"
#include "utils/corestrings.h"
#include "utils/log.h"
#include "utils/utils.h"
#include "utils/ring.h"

#include "content/fetch.h"
#include "content/fetchers.h"

#include "bearssl.h"
#include <zlib.h>

/* --- RexOS syscallok --------------------------------------------------------- */

#define RSYS_YIELD        2
#define RSYS_TICKS        10
#define RSYS_NET_CONNECT  21
#define RSYS_NET_SEND     22
#define RSYS_NET_RECV     23
#define RSYS_TIME         24
#define RSYS_NET_CLOSE    25

typedef struct {
    uint8_t  second, minute, hour, day, month;
    uint32_t year;
} rexos_rtc_t;

static inline uint64_t rsc3(uint64_t nr, uint64_t a1, uint64_t a2, uint64_t a3)
{
    uint64_t ret;
    __asm__ volatile("syscall"
                     : "=a"(ret)
                     : "a"(nr), "D"(a1), "S"(a2), "d"(a3)
                     : "rcx", "r11", "memory");
    return ret;
}

static inline uint64_t rnet_connect(const char *h, uint16_t p)
{ return rsc3(RSYS_NET_CONNECT, (uint64_t)h, p, 0); }
static inline int rnet_send(uint64_t s, const void *b, uint64_t l)
{ return (int)rsc3(RSYS_NET_SEND, s, (uint64_t)b, l); }
static inline int rnet_recv(uint64_t s, void *b, uint64_t l)
{ return (int)rsc3(RSYS_NET_RECV, s, (uint64_t)b, l); }
static inline void rnet_close(uint64_t s)
{ rsc3(RSYS_NET_CLOSE, s, 0, 0); }

/* soros port debug (SYS_WRITE fd=1 -> kprintf) */
static void rdbg(const char *a, const char *b)
{
    char line[256];
    int n = snprintf(line, sizeof(line), "[fetch] %s%s\n", a, b ? b : "");
    if (n > 0) rsc3(0 /* RSYS_WRITE */, 1, (uint64_t)line, (uint64_t)n);
}

/* --- CA bundle (rexos_ca.c) --------------------------------------------------- */
extern const br_x509_trust_anchor TAs[];
extern const size_t TAs_NUM;

/* --- kapcsolat absztrakció (sima TCP vagy TLS) -------------------------------- */

struct rconn {
    uint64_t sock;
    int      is_tls;
    int      tls_err;
    br_ssl_client_context   cc;
    br_x509_minimal_context xc;
    uint8_t  iobuf[BR_SSL_BUFSIZE_BIDI];
};

static void tls_set_time(struct rconn *c)
{
    rexos_rtc_t t;
    rsc3(RSYS_TIME, (uint64_t)&t, 0, 0);
    int y = (int)t.year, m = (int)t.month, d = (int)t.day;
    if (y < 2024 || y > 2100 || m < 1 || m > 12 || d < 1 || d > 31) {
        y = 2026; m = 1; d = 1; t.hour = 0; t.minute = 0; t.second = 0;
    }
    int adj_y = y - (m <= 2 ? 1 : 0);
    int era   = (adj_y >= 0 ? adj_y : adj_y - 399) / 400;
    int yoe   = adj_y - era * 400;
    int doy   = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    int doe   = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    long unix_days = (long)era * 146097 + doe - 719468;
    br_x509_minimal_set_time(&c->xc, (uint32_t)(unix_days + 719528),
                             (uint32_t)t.hour * 3600u +
                             (uint32_t)t.minute * 60u + t.second);
}

static void tls_entropy(uint8_t out[32])
{
    uint64_t pool[4] = { 0, 0, 0, 0 };
    for (int i = 0; i < 64; i++) {
        uint32_t lo, hi;
        __asm__ __volatile__("rdtsc" : "=a"(lo), "=d"(hi));
        pool[0] ^= ((uint64_t)hi << 32) | lo;
        pool[1] = (pool[1] << 7) ^ rsc3(RSYS_TICKS, 0, 0, 0);
        pool[2] += (uint64_t)(uintptr_t)&pool;
        pool[3] = (pool[3] * 1103515245ULL + 12345ULL) ^ pool[0];
        rsc3(RSYS_YIELD, 0, 0, 0);
    }
    memcpy(out, pool, 32);
}

/* BearSSL engine-loop: pörgetés amíg a kívánt állapot elérhető */
static int tls_run_until(struct rconn *c, unsigned wait_for)
{
    br_ssl_engine_context *eng = &c->cc.eng;
    for (;;) {
        unsigned st = br_ssl_engine_current_state(eng);

        if (st & BR_SSL_CLOSED) {
            c->tls_err = br_ssl_engine_last_error(eng);
            return 0;
        }
        if (wait_for && (st & wait_for)) return 1;

        if (st & BR_SSL_SENDREC) {
            size_t slen;
            uint8_t *sbuf = br_ssl_engine_sendrec_buf(eng, &slen);
            int sent = rnet_send(c->sock, sbuf, slen);
            if (sent <= 0) { c->tls_err = BR_ERR_IO; return 0; }
            br_ssl_engine_sendrec_ack(eng, (size_t)sent);
            continue;
        }
        if (st & BR_SSL_RECVREC) {
            size_t rlen;
            uint8_t *rbuf = br_ssl_engine_recvrec_buf(eng, &rlen);
            /* A kernel recv ~5 mp után 0-val tér vissza, ha nincs adat.
             * 3 egymást követő üres kör (~15 mp) után feladjuk, különben egy
             * elhalt kapcsolat örökre felfüggesztené a böngészőt. */
            int empty = 0;
            int got;
            while ((got = rnet_recv(c->sock, rbuf, rlen)) == 0) {
                if (++empty >= 3) { c->tls_err = BR_ERR_IO; return 0; }
                rsc3(RSYS_YIELD, 0, 0, 0);
            }
            if (got < 0) { c->tls_err = BR_ERR_IO; return 0; }
            br_ssl_engine_recvrec_ack(eng, (size_t)got);
            continue;
        }
        if (!wait_for) return 1;
        rsc3(RSYS_YIELD, 0, 0, 0);
    }
}

static struct rconn *rconn_open(const char *host, uint16_t port, int is_tls)
{
    struct rconn *c = calloc(1, sizeof(*c));
    if (!c) return NULL;

    c->sock = rnet_connect(host, port);
    if (c->sock == (uint64_t)-1 || c->sock == 0) { free(c); return NULL; }
    c->is_tls = is_tls;
    if (!is_tls) return c;

    br_ssl_client_init_full(&c->cc, &c->xc, TAs, TAs_NUM);
    tls_set_time(c);
    uint8_t seed[32];
    tls_entropy(seed);
    br_ssl_engine_inject_entropy(&c->cc.eng, seed, sizeof(seed));
    br_ssl_engine_set_buffer(&c->cc.eng, c->iobuf, sizeof(c->iobuf), 1);
    if (!br_ssl_client_reset(&c->cc, host, 0) ||
        !tls_run_until(c, BR_SSL_SENDAPP)) {
        NSLOG(netsurf, WARNING, "TLS handshake failed (%s): err=%d",
              host, c->tls_err);
        rnet_close(c->sock);
        free(c);
        return NULL;
    }
    return c;
}

static int rconn_send(struct rconn *c, const void *buf, size_t len)
{
    if (!c->is_tls) {
        return rnet_send(c->sock, buf, len);
    }
    br_ssl_engine_context *eng = &c->cc.eng;
    const uint8_t *p = buf;
    size_t left = len;
    while (left > 0) {
        if (!tls_run_until(c, BR_SSL_SENDAPP)) return -1;
        size_t alen;
        uint8_t *abuf = br_ssl_engine_sendapp_buf(eng, &alen);
        if (!abuf || alen == 0) return -1;
        size_t copy = left < alen ? left : alen;
        memcpy(abuf, p, copy);
        br_ssl_engine_sendapp_ack(eng, copy);
        br_ssl_engine_flush(eng, 0);
        p += copy;
        left -= copy;
    }
    tls_run_until(c, 0);
    return (int)len;
}

/* vissza: >0 byte, 0 = kapcsolat vége, <0 hiba */
static int rconn_recv(struct rconn *c, void *buf, size_t max)
{
    if (!c->is_tls) {
        /* a kernel recv 0-t ad timeoutnál ÉS closednál is; a hívó a teljes
         * válasz feldolgozásával kezeli (Content-Length / chunked vég) */
        return rnet_recv(c->sock, buf, max);
    }
    br_ssl_engine_context *eng = &c->cc.eng;
    if (!tls_run_until(c, BR_SSL_RECVAPP)) {
        return (c->tls_err == 0) ? 0 : -1;
    }
    size_t alen;
    uint8_t *abuf = br_ssl_engine_recvapp_buf(eng, &alen);
    if (!abuf || alen == 0) return 0;
    size_t copy = max < alen ? max : alen;
    memcpy(buf, abuf, copy);
    br_ssl_engine_recvapp_ack(eng, copy);
    return (int)copy;
}

static void rconn_close(struct rconn *c)
{
    if (!c) return;
    if (c->is_tls) {
        br_ssl_engine_close(&c->cc.eng);
        /* best-effort close_notify; hibára nem várunk */
    }
    rnet_close(c->sock);
    free(c);
}

/* --- keep-alive kapcsolat-cache ----------------------------------------------
 * host:port(+tls) kulcsú, kevés bejegyzéses cache. A TLS handshake (~1-2 mp
 * a gyenge entrópiagyűjtéssel) a legdrágább művelet; egy Wikipédia-oldal
 * 30+ kapcsolata így 2-3-ra esik. */

#define CONN_CACHE_SIZE 4
#define CONN_HOST_MAX   128

struct conn_cache_ent {
    int      used;
    char     host[CONN_HOST_MAX];
    uint16_t port;
    int      is_tls;
    struct rconn *conn;
    uint64_t last_used_tick;
};

static struct conn_cache_ent conn_cache[CONN_CACHE_SIZE];

static struct rconn *cache_take(const char *host, uint16_t port, int is_tls)
{
    for (int i = 0; i < CONN_CACHE_SIZE; i++) {
        struct conn_cache_ent *e = &conn_cache[i];
        if (e->used && e->port == port && e->is_tls == is_tls &&
            strcmp(e->host, host) == 0) {
            e->used = 0;
            return e->conn;
        }
    }
    return NULL;
}

static void cache_put(const char *host, uint16_t port, int is_tls,
                      struct rconn *conn)
{
    if (strlen(host) >= CONN_HOST_MAX) {
        rconn_close(conn);
        return;
    }
    uint64_t now = rsc3(RSYS_TICKS, 0, 0, 0);
    struct conn_cache_ent *slot = NULL;
    for (int i = 0; i < CONN_CACHE_SIZE; i++) {
        if (!conn_cache[i].used) { slot = &conn_cache[i]; break; }
    }
    if (!slot) {
        /* legrégebben használt kidobása */
        slot = &conn_cache[0];
        for (int i = 1; i < CONN_CACHE_SIZE; i++) {
            if (conn_cache[i].last_used_tick < slot->last_used_tick)
                slot = &conn_cache[i];
        }
        rconn_close(slot->conn);
    }
    slot->used = 1;
    strcpy(slot->host, host);
    slot->port = port;
    slot->is_tls = is_tls;
    slot->conn = conn;
    slot->last_used_tick = now;
}

/* --- body sink: FETCH_DATA közvetlenül vagy gzip-en át ----------------------- */

struct fetch_rexos_ctx;
static void send_cb(const fetch_msg *msg, struct fetch_rexos_ctx *c);

struct body_sink {
    struct fetch_rexos_ctx *c;
    int      gz;         /* Content-Encoding: gzip */
    int      zinit;
    int      zerr;
    int      zend;       /* Z_STREAM_END elérve */
    unsigned long in_bytes, out_bytes;
    z_stream zs;
    uint8_t  out[16384];
};

static int sink_init(struct body_sink *s, struct fetch_rexos_ctx *c, int gz)
{
    memset(s, 0, sizeof(*s));
    s->c = c;
    s->gz = gz;
    if (gz) {
        /* 15+32: zlib ÉS gzip fejléc auto-detektálás */
        if (inflateInit2(&s->zs, 15 + 32) != Z_OK) return -1;
        s->zinit = 1;
    }
    return 0;
}

static void sink_fini(struct body_sink *s)
{
    if (s->zinit) inflateEnd(&s->zs);
    s->zinit = 0;
}

/* vissza: 0 OK, -1 dekódolási hiba */
static int sink_emit(struct body_sink *s, const uint8_t *buf, size_t len);
static int sink_emit(struct body_sink *s, const uint8_t *buf, size_t len)
{
    fetch_msg msg;
    if (len == 0) return 0;

    s->in_bytes += len;
    if (!s->gz) {
        s->out_bytes += len;
        msg.type = FETCH_DATA;
        msg.data.header_or_data.buf = buf;
        msg.data.header_or_data.len = len;
        send_cb(&msg, s->c);
        return 0;
    }

    s->zs.next_in = (Bytef *)buf;
    s->zs.avail_in = (uInt)len;
    while (s->zs.avail_in > 0) {
        s->zs.next_out = s->out;
        s->zs.avail_out = sizeof(s->out);
        int zr = inflate(&s->zs, Z_NO_FLUSH);
        if (zr != Z_OK && zr != Z_STREAM_END && zr != Z_BUF_ERROR) {
            s->zerr = zr;
            return -1;
        }
        size_t produced = sizeof(s->out) - s->zs.avail_out;
        if (produced > 0) {
            s->out_bytes += produced;
            msg.type = FETCH_DATA;
            msg.data.header_or_data.buf = s->out;
            msg.data.header_or_data.len = produced;
            send_cb(&msg, s->c);
        }
        if (zr == Z_STREAM_END) { s->zend = 1; break; }
        if (zr == Z_BUF_ERROR && produced == 0) break; /* több input kell */
    }
    return 0;
}

/* --- fetch kontextus ---------------------------------------------------------- */

struct fetch_rexos_ctx {
    struct fetch *parent_fetch;
    nsurl *url;
    char  *post_urlenc;         /* POST body (url-encoded), vagy NULL */

    bool aborted;
    bool locked;

    struct fetch_rexos_ctx *r_next, *r_prev;
};

static struct fetch_rexos_ctx *ring = NULL;

static bool fetch_rexos_initialise(lwc_string *scheme)
{
    NSLOG(netsurf, INFO, "rexos fetcher initialised for %s",
          lwc_string_data(scheme));
    return true;
}

static void fetch_rexos_finalise(lwc_string *scheme)
{
    (void)scheme;
}

static bool fetch_rexos_can_fetch(const nsurl *url)
{
    (void)url;
    return true;
}

static void *fetch_rexos_setup(struct fetch *parent_fetch, nsurl *url,
                               bool only_2xx, bool downgrade_tls,
                               const char *post_urlenc,
                               const struct fetch_multipart_data *post_multipart,
                               const char **headers)
{
    (void)only_2xx; (void)downgrade_tls; (void)post_multipart; (void)headers;

    struct fetch_rexos_ctx *ctx = calloc(1, sizeof(*ctx));
    if (!ctx) return NULL;

    ctx->parent_fetch = parent_fetch;
    ctx->url = nsurl_ref(url);
    if (post_urlenc) ctx->post_urlenc = strdup(post_urlenc);

    RING_INSERT(ring, ctx);
    return ctx;
}

static bool fetch_rexos_start(void *vctx)
{
    (void)vctx;
    return true;
}

static void fetch_rexos_abort(void *vctx)
{
    struct fetch_rexos_ctx *c = vctx;
    c->aborted = true;
}

static void fetch_rexos_free(void *vctx)
{
    struct fetch_rexos_ctx *c = vctx;
    nsurl_unref(c->url);
    free(c->post_urlenc);
    free(c);
}

static void send_cb(const fetch_msg *msg, struct fetch_rexos_ctx *c)
{
    c->locked = true;
    fetch_send_callback(msg, c->parent_fetch);
    c->locked = false;
}

static void send_error(struct fetch_rexos_ctx *c, const char *text)
{
    fetch_msg msg;
    msg.type = FETCH_ERROR;
    msg.data.error = text;
    send_cb(&msg, c);
}

/* --- HTTP végrehajtás --------------------------------------------------------- */

#define RECV_BUF   8192
#define HDR_MAX    16384

/* Egy teljes HTTP tranzakció végrehajtása és a callbackek kiküldése. */
static void fetch_rexos_process(struct fetch_rexos_ctx *c)
{
    fetch_msg msg;
    lwc_string *scheme_l = nsurl_get_component(c->url, NSURL_SCHEME);
    lwc_string *host_l   = nsurl_get_component(c->url, NSURL_HOST);
    lwc_string *port_l   = nsurl_get_component(c->url, NSURL_PORT);
    lwc_string *path_l   = nsurl_get_component(c->url, NSURL_PATH);
    lwc_string *query_l  = nsurl_get_component(c->url, NSURL_QUERY);

    int is_tls = (scheme_l != NULL &&
                  strcmp(lwc_string_data(scheme_l), "https") == 0);
    uint16_t port = is_tls ? 443 : 80;
    if (port_l != NULL) port = (uint16_t)atoi(lwc_string_data(port_l));

    if (host_l == NULL) {
        send_error(c, "Missing host in URL");
        goto out_strings;
    }
    const char *host  = lwc_string_data(host_l);
    const char *path  = path_l ? lwc_string_data(path_l) : "/";
    const char *query = query_l ? lwc_string_data(query_l) : NULL;

    /* --- kérés összeállítása --- */
    size_t reqcap = strlen(path) + (query ? strlen(query) : 0) +
                    strlen(host) +
                    (c->post_urlenc ? strlen(c->post_urlenc) : 0) + 512;
    char *req = malloc(reqcap);
    if (!req) {
        send_error(c, "Out of memory");
        goto out_strings;
    }
    int rl = snprintf(req, reqcap,
                      "%s %s%s%s HTTP/1.1\r\n"
                      "Host: %s\r\n"
                      "User-Agent: NetSurf/3.11 (RexOS; x86_64)\r\n"
                      "Accept: */*\r\n"
                      "Accept-Encoding: gzip\r\n"
                      "Connection: keep-alive\r\n",
                      c->post_urlenc ? "POST" : "GET",
                      path, query ? "?" : "", query ? query : "",
                      host);
    if (c->post_urlenc) {
        rl += snprintf(req + rl, reqcap - rl,
                       "Content-Type: application/x-www-form-urlencoded\r\n"
                       "Content-Length: %zu\r\n\r\n%s",
                       strlen(c->post_urlenc), c->post_urlenc);
    } else {
        rl += snprintf(req + rl, reqcap - rl, "\r\n");
    }

    /* --- kapcsolat (cache-ből vagy új) + kérés + fejlécek ---
     * Cache-elt kapcsolat lehet közben szerveroldalt lezárt; ilyenkor EGY
     * újrapróbálkozás friss kapcsolattal. */
    struct rconn *conn = NULL;
    char *hdr = NULL;
    size_t hdr_len;
    char *body0;             /* a headerrel együtt érkezett body-kezdet */
    size_t body0_len;

    rdbg("GET ", nsurl_access(c->url));
    for (int attempt = 0; attempt < 2; attempt++) {
        int from_cache = 0;
        conn = cache_take(host, port, is_tls);
        if (conn) {
            from_cache = 1;
            rdbg("  reuse ", host);
        } else {
            conn = rconn_open(host, port, is_tls);
        }
        if (conn == NULL) {
            free(req);
            send_error(c, is_tls ? "TLS connection failed"
                                 : "Connection failed");
            goto out_strings;
        }

        if (rconn_send(conn, req, (size_t)rl) <= 0) {
            rconn_close(conn);
            conn = NULL;
            if (from_cache) continue;   /* elévült kapcsolat, újra frissel */
            free(req);
            send_error(c, "Failed to send request");
            goto out_strings;
        }

        if (!hdr) hdr = malloc(HDR_MAX);
        if (!hdr) {
            free(req);
            rconn_close(conn);
            send_error(c, "Out of memory");
            goto out_strings;
        }
        hdr_len = 0;
        body0 = NULL;
        body0_len = 0;

        while (hdr_len < HDR_MAX - 1) {
            int got = rconn_recv(conn, hdr + hdr_len, HDR_MAX - 1 - hdr_len);
            if (got <= 0) break;
            hdr_len += (size_t)got;
            hdr[hdr_len] = 0;
            char *end = strstr(hdr, "\r\n\r\n");
            if (end) {
                body0 = end + 4;
                body0_len = hdr_len - (size_t)(body0 - hdr);
                *end = 0;
                break;
            }
        }

        if (body0 != NULL) break;       /* megvan a teljes fejléc */

        rconn_close(conn);
        conn = NULL;
        if (from_cache && hdr_len == 0) continue; /* halott cache-elt kapcsolat */

        free(req);
        free(hdr);
        send_error(c, "Malformed HTTP response");
        goto out_strings;
    }
    free(req);

    if (conn == NULL || body0 == NULL) {
        free(hdr);
        send_error(c, "Connection failed");
        goto out_strings;
    }

    /* --- státusz sor --- */
    int http_code = 0;
    {
        char *sp = strchr(hdr, ' ');
        if (sp) http_code = atoi(sp + 1);
    }
    if (http_code == 0) {
        free(hdr);
        rconn_close(conn);
        send_error(c, "Bad HTTP status line");
        goto out_strings;
    }
    fetch_set_http_code(c->parent_fetch, http_code);

    /* --- fejlécek feldolgozása + továbbítása --- */
    bool chunked = false;
    bool gzipped = false;
    bool srv_close = false;
    long content_length = -1;
    char *location = NULL;

    /* első menet: csak felismerés (a gzip döntés kell a header-szűréshez) */
    char *line = strchr(hdr, '\n');
    line = line ? line + 1 : NULL;
    while (line && *line) {
        char *nl = strchr(line, '\n');
        size_t ll = nl ? (size_t)(nl - line) : strlen(line);
        while (ll > 0 && (line[ll-1] == '\r')) ll--;
        if (ll > 0) {
            if (strncasecmp(line, "Transfer-Encoding:", 18) == 0 &&
                strcasestr(line + 18, "chunked") != NULL) {
                chunked = true;
            } else if (strncasecmp(line, "Content-Length:", 15) == 0) {
                content_length = atol(line + 15);
            } else if (strncasecmp(line, "Content-Encoding:", 17) == 0 &&
                       strcasestr(line + 17, "gzip") != NULL) {
                gzipped = true;
            } else if (strncasecmp(line, "Connection:", 11) == 0 &&
                       strcasestr(line + 11, "close") != NULL) {
                srv_close = true;
            } else if (strncasecmp(line, "Location:", 9) == 0) {
                const char *v = line + 9;
                size_t vl = ll - 9;
                while (vl && *v == ' ') { v++; vl--; }
                free(location);
                location = strndup(v, vl);
            }
        }
        line = nl ? nl + 1 : NULL;
    }

    /* második menet: továbbítás a NetSurfnek. Gzipnél a Content-Encoding és
     * Content-Length fejléceket elnyeljük: a kliens már a kitömörített
     * adatot kapja, aminek más a hossza és nincs kódolása. */
    line = strchr(hdr, '\n');
    line = line ? line + 1 : NULL;
    while (line && *line && !c->aborted) {
        char *nl = strchr(line, '\n');
        size_t ll = nl ? (size_t)(nl - line) : strlen(line);
        while (ll > 0 && (line[ll-1] == '\r')) ll--;

        if (ll > 0) {
            bool suppress = gzipped &&
                (strncasecmp(line, "Content-Encoding:", 17) == 0 ||
                 strncasecmp(line, "Content-Length:", 15) == 0);
            if (!suppress) {
                msg.type = FETCH_HEADER;
                msg.data.header_or_data.buf = (const uint8_t *)line;
                msg.data.header_or_data.len = ll;
                send_cb(&msg, c);
            }
        }
        line = nl ? nl + 1 : NULL;
    }

    /* --- redirect? --- */
    if (http_code >= 300 && http_code < 400 && location != NULL &&
        !c->aborted) {
        nsurl *abs = NULL;
        if (nsurl_join(c->url, location, &abs) == NSERROR_OK) {
            msg.type = FETCH_REDIRECT;
            msg.data.redirect = nsurl_access(abs);
            send_cb(&msg, c);
            nsurl_unref(abs);
        } else {
            send_error(c, "Bad redirect location");
        }
        free(location);
        free(hdr);
        rconn_close(conn);
        goto out_strings;
    }
    free(location);

    /* --- body streamelése --- */
    /* chunked dekóder állapot */
    long chunk_remaining = -1;   /* -1: chunk-méret sor jön */
    bool done = false;
    bool trailer_clean = false;  /* chunked: a záró CRLF is el lett fogyasztva */
    long body_received = 0;      /* huzalon fogadott body byte-ok (tömörítve) */

    struct body_sink sink;
    if (sink_init(&sink, c, gzipped) != 0) {
        free(hdr);
        rconn_close(conn);
        send_error(c, "Out of memory (inflate)");
        goto out_strings;
    }

    uint8_t *buf = malloc(RECV_BUF);
    size_t pending_len = body0_len;
    uint8_t *pending = malloc(body0_len ? body0_len : 1);
    if (!buf || !pending) {
        free(buf); free(pending); free(hdr);
        sink_fini(&sink);
        rconn_close(conn);
        send_error(c, "Out of memory");
        goto out_strings;
    }
    memcpy(pending, body0, body0_len);
    free(hdr);

    /* egyszerű line-accumulátor a chunk-size sorokhoz */
    char szline[32];
    size_t szline_len = 0;

    while (!done && !c->aborted) {
        /* dolgozzuk fel a pending buffert */
        size_t off = 0;
        while (off < pending_len && !done && !c->aborted) {
            if (!chunked) {
                size_t n = pending_len - off;
                /* keep-alive: Content-Length-en túl nem olvasunk bele a
                 * következő válaszba */
                if (content_length >= 0 &&
                    (long)n > content_length - body_received) {
                    n = (size_t)(content_length - body_received);
                }
                if (sink_emit(&sink, pending + off, n) != 0) goto sink_err;
                body_received += (long)n;
                off += n;
                if (content_length >= 0 && body_received >= content_length) {
                    done = true;
                }
            } else if (chunk_remaining > 0) {
                size_t n = pending_len - off;
                if ((long)n > chunk_remaining) n = (size_t)chunk_remaining;
                if (sink_emit(&sink, pending + off, n) != 0) goto sink_err;
                chunk_remaining -= (long)n;
                off += n;
                if (chunk_remaining == 0) chunk_remaining = -2; /* CRLF jön */
            } else if (chunk_remaining == -2) {
                /* chunk utáni CRLF átugrása */
                if (pending[off] == '\n') chunk_remaining = -1;
                off++;
            } else {
                /* chunk-size sor gyűjtése */
                char ch = (char)pending[off++];
                if (ch == '\n') {
                    szline[szline_len] = 0;
                    long sz = strtol(szline, NULL, 16);
                    szline_len = 0;
                    if (sz == 0) {
                        done = true;
                        /* záró CRLF elfogyasztása, ha már megérkezett (a
                         * kapcsolat csak így hasznosítható újra) */
                        if (off + 1 < pending_len &&
                            pending[off] == '\r' && pending[off+1] == '\n') {
                            trailer_clean = true;
                        }
                    }
                    else chunk_remaining = sz;
                } else if (ch != '\r' && szline_len < sizeof(szline) - 1) {
                    szline[szline_len++] = ch;
                }
            }
        }

        if (done || c->aborted) break;

        int got = rconn_recv(conn, buf, RECV_BUF);
        if (got < 0) {
            free(buf); free(pending); sink_fini(&sink);
            rconn_close(conn);
            send_error(c, "Connection lost while reading body");
            goto out_strings;
        }
        if (got == 0) {
            /* Sima TCP-nél a 0 lehet az 5 mp-es kernel timeout is; ha még
             * várunk adatot (Content-Length / nyitott chunk), párszor
             * újrapróbáljuk, mielőtt lezártnak tekintenénk. */
            bool expecting_more =
                (!chunked && content_length >= 0 &&
                 body_received < content_length) ||
                (chunked && !done);
            static const int MAX_EMPTY = 3;
            int empty;
            for (empty = 0; expecting_more && empty < MAX_EMPTY; empty++) {
                got = rconn_recv(conn, buf, RECV_BUF);
                if (got != 0) break;
            }
            if (got < 0) {
                free(buf); free(pending); sink_fini(&sink);
                rconn_close(conn);
                send_error(c, "Connection lost while reading body");
                goto out_strings;
            }
            if (got == 0) {
                /* kapcsolat vége (Content-Length nélkül normál lezárás) */
                done = true;
                srv_close = true;   /* a kapcsolat kimerült, nem cache-elhető */
                break;
            }
        }
        if ((size_t)got > pending_len) {
            uint8_t *np = realloc(pending, (size_t)got);
            if (!np) {
                free(buf); free(pending); sink_fini(&sink);
                rconn_close(conn);
                send_error(c, "Out of memory");
                goto out_strings;
            }
            pending = np;
        }
        memcpy(pending, buf, (size_t)got);
        pending_len = (size_t)got;
    }

    free(buf);
    free(pending);
    sink_fini(&sink);

    /* --- kapcsolat sorsa: cache-be vissza vagy zárás ---
     * Csak akkor hasznosítható újra, ha a válasz framingje ismert volt és
     * pontosan a végéig fogyasztottuk el. */
    {
        bool reusable = !srv_close && !c->aborted && done &&
            ((chunked && trailer_clean) ||
             (!chunked && content_length >= 0 &&
              body_received == content_length));
        if (reusable) {
            cache_put(host, port, is_tls, conn);
        } else {
            rconn_close(conn);
        }
    }

    if (!c->aborted) {
        msg.type = FETCH_FINISHED;
        send_cb(&msg, c);
    }
    {
        char stats[128];
        snprintf(stats, sizeof(stats),
                 "in=%lu out=%lu gz=%d zend=%d chunked=%d cl=%ld",
                 sink.in_bytes, sink.out_bytes, sink.gz, sink.zend,
                 (int)chunked, content_length);
        rdbg("  done ", stats);
    }
    goto out_strings;

sink_err:
    free(buf);
    free(pending);
    sink_fini(&sink);
    rconn_close(conn);
    send_error(c, "gzip decode error");

out_strings:
    if (scheme_l) lwc_string_unref(scheme_l);
    if (host_l)   lwc_string_unref(host_l);
    if (port_l)   lwc_string_unref(port_l);
    if (path_l)   lwc_string_unref(path_l);
    if (query_l)  lwc_string_unref(query_l);
}

static void fetch_rexos_poll(lwc_string *scheme)
{
    (void)scheme;
    struct fetch_rexos_ctx *c, *save_ring = NULL;

    while (ring != NULL) {
        c = ring;
        RING_REMOVE(ring, c);

        if (c->locked) {
            RING_INSERT(save_ring, c);
            continue;
        }

        if (!c->aborted) {
            fetch_rexos_process(c);
        }

        fetch_remove_from_queues(c->parent_fetch);
        fetch_free(c->parent_fetch);
    }

    ring = save_ring;
}

/* --- regisztráció -------------------------------------------------------------- */

nserror fetch_rexos_register(void);
nserror fetch_rexos_register(void)
{
    nserror ret;
    const struct fetcher_operation_table fetcher_ops = {
        .initialise = fetch_rexos_initialise,
        .acceptable = fetch_rexos_can_fetch,
        .setup = fetch_rexos_setup,
        .start = fetch_rexos_start,
        .abort = fetch_rexos_abort,
        .free = fetch_rexos_free,
        .poll = fetch_rexos_poll,
        .finalise = fetch_rexos_finalise
    };

    ret = fetcher_add(lwc_string_ref(corestring_lwc_http), &fetcher_ops);
    if (ret != NSERROR_OK) return ret;
    return fetcher_add(lwc_string_ref(corestring_lwc_https), &fetcher_ops);
}
