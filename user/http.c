#include "http.h"
#include "libc.h"
#include "tls.h"

/* Debug: serial-on logol (write(1,...) -> kprintf a kernelben). */
#define HTTP_DBG 1
static void hdbg(const char *s) {
#if HTTP_DBG
    int n = 0; while (s[n]) n++;
    write(1, s, (uint64_t)n);
#else
    (void)s;
#endif
}
static void hdbg_int(int v) {
#if HTTP_DBG
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

#define HTTP_HOST_MAX 128
#define HTTP_PATH_MAX 256
#define HTTP_RAW_INITIAL 4096
#define HTTP_HEADER_SLOP 8192
#define HTTP_RESPONSE_MAX 131072
#define HTTP_RECV_CHUNK 512
#define HTTP_IDLE_TIMEOUT_TICKS 300
#define HTTP_TOTAL_TIMEOUT_TICKS 1000

typedef struct {
    char host[HTTP_HOST_MAX];
    char path[HTTP_PATH_MAX];
    uint16_t port;
    int      is_tls;  /* https:// -> 1 */
} http_url_t;

/* ---- Connection wrapper: plain TCP vagy TLS, közös send/recv interfész ---- */
typedef struct {
    int        is_tls;
    uint64_t   sock;     /* csak ha !is_tls */
    tls_ctx_t *tls;      /* csak ha is_tls */
} http_conn_t;

static int hc_open(http_conn_t *c, const char *host, uint16_t port, int is_tls) {
    c->is_tls = is_tls;
    c->sock   = 0;
    c->tls    = 0;
    if (is_tls) {
        c->tls = tls_connect(host, port);
        return c->tls ? 0 : -1;
    } else {
        c->sock = net_connect(host, port);
        if (c->sock == (uint64_t)-1 || c->sock == 0) return -1;
        return 0;
    }
}

static int hc_send(http_conn_t *c, const void *buf, uint64_t len) {
    if (c->is_tls) return tls_send(c->tls, buf, len);
    return net_send(c->sock, buf, len);
}

static int hc_recv(http_conn_t *c, void *buf, uint64_t max) {
    if (c->is_tls) return tls_recv(c->tls, buf, max);
    return net_recv(c->sock, buf, max);
}

static void hc_close(http_conn_t *c) {
    if (c->is_tls) {
        if (c->tls) tls_close(c->tls);
    } else {
        if (c->sock != 0 && c->sock != (uint64_t)-1) net_close(c->sock);
    }
    c->tls = 0;
    c->sock = 0;
}

static int h_strlen(const char *s) {
    int len = 0;
    while (s && s[len]) len++;
    return len;
}

static void h_memzero(void *p, uint64_t n) {
    uint8_t *b = (uint8_t *)p;
    for (uint64_t i = 0; i < n; i++) b[i] = 0;
}

static void h_memcpy(void *dst, const void *src, uint64_t n) {
    uint8_t *d = (uint8_t *)dst;
    const uint8_t *s = (const uint8_t *)src;
    for (uint64_t i = 0; i < n; i++) d[i] = s[i];
}

static void h_strncpy0(char *dst, const char *src, int max) {
    int i = 0;
    if (max <= 0) return;
    for (; src && src[i] && i < max - 1; i++) dst[i] = src[i];
    dst[i] = 0;
}

static void h_strcat(char *dst, const char *src) {
    int pos = h_strlen(dst);
    int i = 0;
    while (src[i]) dst[pos++] = src[i++];
    dst[pos] = 0;
}

static int h_strcat_limit(char *dst, const char *src, int max) {
    int pos = h_strlen(dst);
    int i = 0;
    if (pos >= max) return 0;
    while (src[i] && pos < max - 1) dst[pos++] = src[i++];
    dst[pos] = 0;
    return src[i] == 0;
}

static int h_strncmp(const char *a, const char *b, int n) {
    for (int i = 0; i < n; i++) {
        if (a[i] != b[i]) return a[i] - b[i];
        if (!a[i]) return 0;
    }
    return 0;
}

static char h_lower(char c) {
    if (c >= 'A' && c <= 'Z') return (char)(c + ('a' - 'A'));
    return c;
}

static int h_strncasecmp(const char *a, const char *b, int n) {
    for (int i = 0; i < n; i++) {
        char ca = h_lower(a[i]);
        char cb = h_lower(b[i]);
        if (ca != cb) return ca - cb;
        if (!ca) return 0;
    }
    return 0;
}

static int h_atoi(const char *s) {
    int v = 0;
    while (*s >= '0' && *s <= '9') v = v * 10 + (*s++ - '0');
    return v;
}

static int h_hex_value(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static void h_append_int(char *dst, int value) {
    char tmp[16];
    int n = 0;
    if (value == 0) {
        h_strcat(dst, "0");
        return;
    }
    while (value > 0 && n < (int)sizeof(tmp)) {
        tmp[n++] = (char)('0' + (value % 10));
        value /= 10;
    }
    while (n > 0) {
        char one[2] = { tmp[--n], 0 };
        h_strcat(dst, one);
    }
}

static int h_append_int_limit(char *dst, int value, int max) {
    char tmp[16];
    int n = 0;
    if (value == 0) return h_strcat_limit(dst, "0", max);
    while (value > 0 && n < (int)sizeof(tmp)) {
        tmp[n++] = (char)('0' + (value % 10));
        value /= 10;
    }
    while (n > 0) {
        char one[2] = { tmp[--n], 0 };
        if (!h_strcat_limit(dst, one, max)) return 0;
    }
    return 1;
}

static void h_set_status(http_response_t *resp, const char *status) {
    if (resp) h_strncpy0(resp->status, status, HTTP_STATUS_MAX);
}

static void h_clear_response(http_response_t *resp) {
    if (!resp) return;
    h_memzero(resp, sizeof(*resp));
}

static int h_parse_url(const char *url, http_url_t *parsed, char *normalized) {
    h_memzero(parsed, sizeof(*parsed));
    parsed->port   = 80;
    parsed->is_tls = 0;

    const char *p = url;
    if (h_strncmp(p, "https://", 8) == 0) {
        p += 8;
        parsed->is_tls = 1;
        parsed->port   = 443;
    } else if (h_strncmp(p, "http://", 7) == 0) {
        p += 7;
    }
    if (!p[0]) return HTTP_ERR_BAD_URL;

    int hi = 0;
    while (*p && *p != '/' && *p != ':' && hi < HTTP_HOST_MAX - 1) {
        parsed->host[hi++] = *p++;
    }
    parsed->host[hi] = 0;
    if (hi == 0 || (*p && *p != '/' && *p != ':')) return HTTP_ERR_BAD_URL;

    if (*p == ':') {
        p++;
        int port = h_atoi(p);
        if (port <= 0 || port > 65535) return HTTP_ERR_BAD_URL;
        parsed->port = (uint16_t)port;
        while (*p >= '0' && *p <= '9') p++;
    }

    int pi = 0;
    if (*p == '/') {
        while (*p && pi < HTTP_PATH_MAX - 1) parsed->path[pi++] = *p++;
        if (*p) return HTTP_ERR_BAD_URL;
    } else {
        parsed->path[pi++] = '/';
    }
    parsed->path[pi] = 0;

    if (normalized) {
        uint16_t default_port = parsed->is_tls ? 443 : 80;
        normalized[0] = 0;
        if (!h_strcat_limit(normalized, parsed->is_tls ? "https://" : "http://", HTTP_URL_MAX))
            return HTTP_ERR_BAD_URL;
        if (!h_strcat_limit(normalized, parsed->host, HTTP_URL_MAX)) return HTTP_ERR_BAD_URL;
        if (parsed->port != default_port) {
            if (!h_strcat_limit(normalized, ":", HTTP_URL_MAX)) return HTTP_ERR_BAD_URL;
            if (!h_append_int_limit(normalized, parsed->port, HTTP_URL_MAX)) return HTTP_ERR_BAD_URL;
        }
        if (!h_strcat_limit(normalized, parsed->path, HTTP_URL_MAX)) return HTTP_ERR_BAD_URL;
    }
    return HTTP_OK;
}

static int h_find_header_end(const char *raw, int len) {
    for (int i = 0; i + 3 < len; i++) {
        if (raw[i] == '\r' && raw[i + 1] == '\n' && raw[i + 2] == '\r' && raw[i + 3] == '\n') return i + 4;
    }
    return -1;
}

static int h_parse_status_code(const char *raw, int len) {
    if (len < 12 || h_strncmp(raw, "HTTP/", 5) != 0) return 0;
    int i = 0;
    while (i < len && raw[i] != ' ') i++;
    while (i < len && raw[i] == ' ') i++;
    if (i + 2 >= len) return 0;
    return h_atoi(raw + i);
}

static const char *h_header_value(const char *headers, int header_len, const char *name, int *value_len) {
    int name_len = h_strlen(name);
    int pos = 0;

    while (pos < header_len && headers[pos] != '\n') pos++;
    if (pos < header_len) pos++;

    while (pos < header_len) {
        int line_start = pos;
        while (pos < header_len && headers[pos] != '\n') pos++;
        int line_end = pos;
        if (pos < header_len) pos++;
        if (line_end > line_start && headers[line_end - 1] == '\r') line_end--;
        if (line_end == line_start) break;

        if (line_end - line_start > name_len &&
            h_strncasecmp(headers + line_start, name, name_len) == 0 &&
            headers[line_start + name_len] == ':') {
            int vstart = line_start + name_len + 1;
            while (vstart < line_end && (headers[vstart] == ' ' || headers[vstart] == '\t')) vstart++;
            int vend = line_end;
            while (vend > vstart && (headers[vend - 1] == ' ' || headers[vend - 1] == '\t')) vend--;
            *value_len = vend - vstart;
            return headers + vstart;
        }
    }
    *value_len = 0;
    return 0;
}

static int h_header_is_chunked(const char *headers, int header_len) {
    int len = 0;
    const char *v = h_header_value(headers, header_len, "Transfer-Encoding", &len);
    if (!v) return 0;
    for (int i = 0; i + 6 < len; i++) {
        if (h_strncasecmp(v + i, "chunked", 7) == 0) return 1;
    }
    return 0;
}

static int h_header_content_length(const char *headers, int header_len) {
    int len = 0;
    const char *v = h_header_value(headers, header_len, "Content-Length", &len);
    if (!v || len <= 0) return -1;
    return h_atoi(v);
}

static int h_header_location(const char *headers, int header_len, char *out, int out_max) {
    int len = 0;
    const char *v = h_header_value(headers, header_len, "Location", &len);
    if (!v || len <= 0) return 0;
    if (len >= out_max) len = out_max - 1;
    for (int i = 0; i < len; i++) out[i] = v[i];
    out[len] = 0;
    return 1;
}

static int h_decode_chunked(const char *body, int body_len, char *out, uint64_t out_max, http_response_t *resp) {
    int pos = 0;
    uint64_t written = 0;
    if (out_max == 0) return 0;

    while (pos < body_len) {
        int chunk_len = 0;
        int saw_digit = 0;
        while (pos < body_len && body[pos] != '\r' && body[pos] != '\n' && body[pos] != ';') {
            int hv = h_hex_value(body[pos]);
            if (hv < 0) break;
            saw_digit = 1;
            chunk_len = chunk_len * 16 + hv;
            pos++;
        }
        while (pos < body_len && body[pos] != '\n') pos++;
        if (pos < body_len) pos++;
        if (!saw_digit) break;
        if (chunk_len == 0) break;
        if (pos + chunk_len > body_len) {
            if (resp) resp->truncated = 1;
            chunk_len = body_len - pos;
        }
        int copied = 0;
        for (int i = 0; i < chunk_len && written + 1 < out_max; i++) {
            out[written++] = body[pos + i];
            copied++;
        }
        if (copied < chunk_len && resp) resp->truncated = 1;
        pos += chunk_len;
        if (pos < body_len && body[pos] == '\r') pos++;
        if (pos < body_len && body[pos] == '\n') pos++;
    }
    out[written] = 0;
    return (int)written;
}

static int h_copy_body(const char *body, int body_len, char *out, uint64_t out_max, http_response_t *resp) {
    uint64_t copy = (uint64_t)body_len;
    if (out_max == 0) return 0;
    if (copy >= out_max) {
        copy = out_max - 1;
        if (resp) resp->truncated = 1;
    }
    for (uint64_t i = 0; i < copy; i++) out[i] = body[i];
    out[copy] = 0;
    return (int)copy;
}

static int h_build_request(const http_url_t *url, char *req, int req_max) {
    (void)req_max;
    req[0] = 0;
    h_strcat(req, "GET ");
    h_strcat(req, url->path);
    h_strcat(req, " HTTP/1.1\r\nHost: ");
    h_strcat(req, url->host);
    h_strcat(req, "\r\nUser-Agent: RexOS/0.1\r\nAccept: text/html,*/*\r\nAccept-Encoding: identity\r\nConnection: close\r\n\r\n");
    return h_strlen(req);
}

static int h_grow_raw(char **raw, int *raw_cap, int needed, int raw_limit) {
    if (needed < *raw_cap) return 1;
    int new_cap = *raw_cap;
    while (needed >= new_cap && new_cap < raw_limit) {
        int next = new_cap * 2;
        if (next <= new_cap || next > raw_limit) next = raw_limit;
        new_cap = next;
    }
    if (needed >= new_cap) return 0;

    char *grown = (char *)malloc((uint64_t)new_cap);
    if (!grown) return 0;
    h_memcpy(grown, *raw, (uint64_t)*raw_cap);
    free(*raw);
    *raw = grown;
    *raw_cap = new_cap;
    return 1;
}

static int h_read_response(http_conn_t *conn, char **raw_io, int *raw_cap_io, int raw_limit,
                           int *out_len, int *header_len, int *content_len, int *hit_limit) {
    int total = 0;
    int hdr_len = -1;
    int clen = -1;
    uint64_t start_tick = get_ticks();
    uint64_t last_data_tick = start_tick;
    char *raw = *raw_io;
    int raw_cap = *raw_cap_io;
    *hit_limit = 0;

    while (1) {
        char chunk[HTTP_RECV_CHUNK];
        int bytes = hc_recv(conn, chunk, HTTP_RECV_CHUNK);
        uint64_t now = get_ticks();
        if (bytes < 0) return HTTP_ERR_RECV;
        if (bytes == 0) {
            if (hdr_len >= 0 && clen >= 0 && total - hdr_len >= clen) break;
            if (now - last_data_tick >= HTTP_IDLE_TIMEOUT_TICKS) break;
            if (now - start_tick >= HTTP_TOTAL_TIMEOUT_TICKS) break;
            yield();
            continue;
        }
        last_data_tick = now;
        if (!h_grow_raw(&raw, &raw_cap, total + bytes + 1, raw_limit)) {
            bytes = raw_cap - total - 1;
            if (bytes < 0) bytes = 0;
            *hit_limit = 1;
        }
        for (int i = 0; i < bytes; i++) raw[total++] = chunk[i];
        raw[total] = 0;
        *raw_io = raw;
        *raw_cap_io = raw_cap;
        if (hdr_len < 0) {
            hdr_len = h_find_header_end(raw, total);
            if (hdr_len >= 0) clen = h_header_content_length(raw, hdr_len);
        }
        if (*hit_limit) break;
        if (hdr_len >= 0 && clen >= 0 && total - hdr_len >= clen) break;
    }

    raw[total] = 0;
    *out_len = total;
    *header_len = hdr_len;
    *content_len = clen;
    return HTTP_OK;
}

static int h_make_absolute_url(const char *base_url, const char *location, char *out) {
    if (h_strncmp(location, "http://", 7) == 0 || h_strncmp(location, "https://", 8) == 0) {
        h_strncpy0(out, location, HTTP_URL_MAX);
        return HTTP_OK;
    }

    http_url_t base;
    int pr = h_parse_url(base_url, &base, 0);
    if (pr != HTTP_OK) return pr;

    uint16_t default_port = base.is_tls ? 443 : 80;
    out[0] = 0;
    if (!h_strcat_limit(out, base.is_tls ? "https://" : "http://", HTTP_URL_MAX))
        return HTTP_ERR_BAD_URL;
    if (!h_strcat_limit(out, base.host, HTTP_URL_MAX)) return HTTP_ERR_BAD_URL;
    if (base.port != default_port) {
        if (!h_strcat_limit(out, ":", HTTP_URL_MAX)) return HTTP_ERR_BAD_URL;
        if (!h_append_int_limit(out, base.port, HTTP_URL_MAX)) return HTTP_ERR_BAD_URL;
    }
    if (location[0] == '/') {
        if (!h_strcat_limit(out, location, HTTP_URL_MAX)) return HTTP_ERR_BAD_URL;
    } else {
        if (!h_strcat_limit(out, "/", HTTP_URL_MAX)) return HTTP_ERR_BAD_URL;
        if (!h_strcat_limit(out, location, HTTP_URL_MAX)) return HTTP_ERR_BAD_URL;
    }
    return HTTP_OK;
}

static int h_body_cap_from(uint64_t desired) {
    if (desired == 0 || desired > HTTP_RESPONSE_MAX) return HTTP_RESPONSE_MAX;
    return (int)desired;
}

static int h_finish_alloc_body(const char *body, int body_len, int is_chunked,
                               uint64_t max_body, char **out_body, http_response_t *resp) {
    uint64_t cap_body = max_body;
    if (cap_body == 0 || cap_body > HTTP_RESPONSE_MAX) cap_body = HTTP_RESPONSE_MAX;
    char *body_buf = (char *)malloc(cap_body + 1);
    if (!body_buf) {
        h_set_status(resp, "Out of memory");
        return HTTP_ERR_NO_MEMORY;
    }
    int copied = is_chunked
        ? h_decode_chunked(body, body_len, body_buf, cap_body + 1, resp)
        : h_copy_body(body, body_len, body_buf, cap_body + 1, resp);
    if (!is_chunked && (uint64_t)body_len > cap_body && resp) resp->truncated = 1;
    *out_body = body_buf;
    return copied;
}

static int h_http_get_impl(const char *url, char *fixed_out, uint64_t fixed_out_max,
                           char **alloc_out, uint64_t alloc_max, http_response_t *resp) {
    h_clear_response(resp);
    if (!url || (!fixed_out && !alloc_out)) return HTTP_ERR_BAD_URL;
    if (fixed_out) {
        if (fixed_out_max == 0) return HTTP_ERR_BAD_URL;
        fixed_out[0] = 0;
    }
    if (alloc_out) *alloc_out = 0;

    char current_url[HTTP_URL_MAX];
    h_strncpy0(current_url, url, HTTP_URL_MAX);

    int desired_body = alloc_out ? h_body_cap_from(alloc_max) : h_body_cap_from(fixed_out_max ? fixed_out_max - 1 : 0);
    int raw_limit = desired_body + HTTP_HEADER_SLOP;
    if (raw_limit > HTTP_RESPONSE_MAX + HTTP_HEADER_SLOP) raw_limit = HTTP_RESPONSE_MAX + HTTP_HEADER_SLOP;
    int raw_cap = HTTP_RAW_INITIAL;
    if (raw_cap > raw_limit) raw_cap = raw_limit;
    char *raw = (char *)malloc((uint64_t)raw_cap);
    if (!raw) {
        h_set_status(resp, "Out of memory");
        return HTTP_ERR_NO_MEMORY;
    }

    for (int redirect = 0; redirect <= HTTP_MAX_REDIRECTS; redirect++) {
        http_url_t parsed;
        char normalized[HTTP_URL_MAX];
        int pr = h_parse_url(current_url, &parsed, normalized);
        if (pr != HTTP_OK) {
            h_set_status(resp, "Bad URL");
            free(raw);
            return pr;
        }
        if (resp) h_strncpy0(resp->final_url, normalized, HTTP_URL_MAX);

        hdbg("[http] GET ");
        hdbg(parsed.is_tls ? "https://" : "http://");
        hdbg(parsed.host);
        hdbg(":"); hdbg_int((int)parsed.port);
        hdbg(parsed.path); hdbg("\n");

        http_conn_t conn;
        if (hc_open(&conn, parsed.host, parsed.port, parsed.is_tls) != 0) {
            hdbg("[http] hc_open FAILED\n");
            h_set_status(resp, parsed.is_tls
                ? "TLS connection failed (handshake or cert)"
                : "Connection failed (DNS/TCP error)");
            free(raw);
            return HTTP_ERR_CONNECT;
        }
        hdbg("[http] hc_open ok\n");

        char req[512];
        int req_len = h_build_request(&parsed, req, sizeof(req));
        if (hc_send(&conn, req, req_len) < 0) {
            hdbg("[http] hc_send FAILED\n");
            h_set_status(resp, "HTTP send failed");
            hc_close(&conn);
            free(raw);
            return HTTP_ERR_SEND;
        }

        int raw_len = 0;
        int header_len = -1;
        int content_len = -1;
        int hit_limit = 0;
        hdbg("[http] sent req, reading response...\n");
        int rr = h_read_response(&conn, &raw, &raw_cap, raw_limit, &raw_len, &header_len, &content_len, &hit_limit);
        hdbg("[http] read_response done: total="); hdbg_int(raw_len);
        hdbg(" hdr="); hdbg_int(header_len);
        hdbg(" clen="); hdbg_int(content_len);
        hdbg(" hit_limit="); hdbg_int(hit_limit); hdbg("\n");
        if (rr != HTTP_OK) {
            hdbg("[http] read_response FAILED rc="); hdbg_int(rr); hdbg("\n");
            h_set_status(resp, "HTTP receive failed");
            hc_close(&conn);
            free(raw);
            return rr;
        }
        if (hit_limit && resp) resp->truncated = 1;
        if (header_len <= 0) {
            hdbg("[http] no header end found\n");
            h_set_status(resp, "Invalid HTTP response");
            hc_close(&conn);
            free(raw);
            return HTTP_ERR_RESPONSE;
        }

        int status_code = h_parse_status_code(raw, header_len);
        hdbg("[http] status="); hdbg_int(status_code); hdbg("\n");
        int is_chunked = h_header_is_chunked(raw, header_len);
        if (resp) {
            resp->status_code = status_code;
            resp->header_len = header_len;
            resp->chunked = is_chunked;
        }

        if ((status_code == 301 || status_code == 302 || status_code == 303 ||
             status_code == 307 || status_code == 308) && redirect < HTTP_MAX_REDIRECTS) {
            char location[HTTP_URL_MAX];
            if (h_header_location(raw, header_len, location, HTTP_URL_MAX)) {
                hdbg("[http] redirect -> "); hdbg(location); hdbg("\n");
                /* https:// és http:// célok egyaránt mehetnek - a TLS layer kezeli. */
                int ar = h_make_absolute_url(normalized, location, current_url);
                if (ar != HTTP_OK) {
                    h_set_status(resp, "Bad redirect URL");
                    hc_close(&conn);
                    free(raw);
                    return ar;
                }
                hc_close(&conn);
                continue;
            }
        }

        const char *body = raw + header_len;
        int body_len = raw_len - header_len;
        if (content_len >= 0 && body_len > content_len) body_len = content_len;

        int copied;
        if (alloc_out) {
            int fr = h_finish_alloc_body(body, body_len, is_chunked, alloc_max, alloc_out, resp);
            if (fr < 0) {
                hc_close(&conn);
                free(raw);
                return fr;
            }
            copied = fr;
        } else {
            copied = is_chunked
                ? h_decode_chunked(body, body_len, fixed_out, fixed_out_max, resp)
                : h_copy_body(body, body_len, fixed_out, fixed_out_max, resp);
        }

        if (resp) {
            resp->body_len = copied;
            resp->status[0] = 0;
            h_strcat(resp->status, "HTTP ");
            h_append_int(resp->status, status_code);
            if (resp->truncated) h_strcat(resp->status, " (truncated)");
        }

        hc_close(&conn);
        free(raw);
        return HTTP_OK;
    }

    h_set_status(resp, "Too many redirects");
    free(raw);
    return HTTP_ERR_TOO_MANY_REDIRECTS;
}

int http_get(const char *url, char *out, uint64_t out_max, http_response_t *resp) {
    return h_http_get_impl(url, out, out_max, 0, 0, resp);
}

int http_get_alloc(const char *url, char **out_body, uint64_t max_body, http_response_t *resp) {
    return h_http_get_impl(url, 0, 0, out_body, max_body, resp);
}
