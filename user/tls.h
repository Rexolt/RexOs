/* =============================================================================
 *  RexOS - User-space TLS layer (BearSSL alapú)
 *
 *  Cél: a HTTP kliens (user/http.c) transzparensen tudjon HTTPS-t használni.
 *
 *  API: kapcsolatonként egy tls_ctx_t. Életciklus:
 *      tls_ctx_t *ctx = tls_connect(host, port);
 *      tls_send(ctx, data, len);
 *      tls_recv(ctx, buf, max);
 *      tls_close(ctx);
 *
 *  Belül a BearSSL "client" engine fut, X.509 minimal verifier-rel a
 *  ca_bundle.c-ből származó trust anchor-okkal és SNI-vel.
 *
 *  Entrópia: jelenleg gyenge (rdtsc + tick + cím-jitter); TLS kulcscseréhez
 *  ez NEM elég, valódi rdrand/HW RNG kell - lásd TODO a tls.c-ben.
 * ========================================================================== */
#pragma once
#include <stdint.h>

#define TLS_OK                 0
#define TLS_ERR_CONNECT       -1
#define TLS_ERR_HANDSHAKE     -2
#define TLS_ERR_IO            -3
#define TLS_ERR_CLOSED        -4
#define TLS_ERR_NOMEM         -5

typedef struct tls_ctx tls_ctx_t;

/* Megnyit egy TCP+TLS kapcsolatot a host:port-ra. NULL hiba esetén. */
tls_ctx_t *tls_connect(const char *host, uint16_t port);

/* Küld pontosan `len` byte-ot, vagy < 0 hibakód. */
int  tls_send(tls_ctx_t *ctx, const void *buf, uint64_t len);

/* Beolvas legfeljebb `max` byte-ot. 0 = peer lezárta, < 0 = hiba. */
int  tls_recv(tls_ctx_t *ctx, void *buf, uint64_t max);

/* Lezárja a kapcsolatot, felszabadítja a kontextust. */
void tls_close(tls_ctx_t *ctx);

/* Hibakód utolsó hívástól (BearSSL engine state). 0 = OK. */
int  tls_last_error(const tls_ctx_t *ctx);
