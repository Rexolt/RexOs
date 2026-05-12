#include "http.h"
#include "libc.h"

#define HTTP_HOST_MAX 128
#define HTTP_PATH_MAX 256
#define HTTP_RAW_MAX  16384
#define HTTP_RECV_CHUNK 512
#define HTTP_IDLE_TIMEOUT_TICKS 300
#define HTTP_TOTAL_TIMEOUT_TICKS 1000

typedef struct {
    char host[HTTP_HOST_MAX];
    char path[HTTP_PATH_MAX];
    uint16_t port;
} http_url_t;

static int h_strlen(const char *s) {
    int len = 0;
    while (s && s[len]) len++;
    return len;
}

static void h_memzero(void *p, uint64_t n) {
    uint8_t *b = (uint8_t *)p;
    for (uint64_t i = 0; i < n; i++) b[i] = 0;
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
    parsed->port = 80;

    const char *p = url;
    if (h_strncmp(p, "http://", 7) == 0) p += 7;
    if (h_strncmp(p, "https://", 8) == 0) return HTTP_ERR_HTTPS;
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
        normalized[0] = 0;
        if (!h_strcat_limit(normalized, "http://", HTTP_URL_MAX)) return HTTP_ERR_BAD_URL;
        if (!h_strcat_limit(normalized, parsed->host, HTTP_URL_MAX)) return HTTP_ERR_BAD_URL;
        if (parsed->port != 80) {
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
    h_strcat(req, "\r\nUser-Agent: RexOS/0.1\r\nAccept: text/html,*/*\r\nConnection: close\r\n\r\n");
    return h_strlen(req);
}

static int h_read_response(uint64_t sock, char *raw, int raw_max, int *out_len, int *header_len, int *content_len) {
    int total = 0;
    int hdr_len = -1;
    int clen = -1;
    uint64_t start_tick = get_ticks();
    uint64_t last_data_tick = start_tick;

    while (total < raw_max - 1) {
        char chunk[HTTP_RECV_CHUNK];
        int bytes = net_recv(sock, chunk, HTTP_RECV_CHUNK);
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
        for (int i = 0; i < bytes && total < raw_max - 1; i++) raw[total++] = chunk[i];
        raw[total] = 0;
        if (hdr_len < 0) {
            hdr_len = h_find_header_end(raw, total);
            if (hdr_len >= 0) clen = h_header_content_length(raw, hdr_len);
        }
        if (hdr_len >= 0 && clen >= 0 && total - hdr_len >= clen) break;
    }

    raw[total] = 0;
    *out_len = total;
    *header_len = hdr_len;
    *content_len = clen;
    return HTTP_OK;
}

static void h_close_socket(uint64_t sock) {
    if (sock != 0 && sock != (uint64_t)-1) net_close(sock);
}

static int h_make_absolute_url(const char *base_url, const char *location, char *out) {
    if (h_strncmp(location, "http://", 7) == 0 || h_strncmp(location, "https://", 8) == 0) {
        h_strncpy0(out, location, HTTP_URL_MAX);
        return HTTP_OK;
    }

    http_url_t base;
    int pr = h_parse_url(base_url, &base, 0);
    if (pr != HTTP_OK) return pr;

    out[0] = 0;
    if (!h_strcat_limit(out, "http://", HTTP_URL_MAX)) return HTTP_ERR_BAD_URL;
    if (!h_strcat_limit(out, base.host, HTTP_URL_MAX)) return HTTP_ERR_BAD_URL;
    if (base.port != 80) {
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

int http_get(const char *url, char *out, uint64_t out_max, http_response_t *resp) {
    h_clear_response(resp);
    if (!url || !out || out_max == 0) return HTTP_ERR_BAD_URL;
    out[0] = 0;

    char current_url[HTTP_URL_MAX];
    h_strncpy0(current_url, url, HTTP_URL_MAX);

    char *raw = (char *)malloc(HTTP_RAW_MAX);
    if (!raw) {
        h_set_status(resp, "Out of memory");
        return HTTP_ERR_NO_MEMORY;
    }

    for (int redirect = 0; redirect <= HTTP_MAX_REDIRECTS; redirect++) {
        http_url_t parsed;
        char normalized[HTTP_URL_MAX];
        int pr = h_parse_url(current_url, &parsed, normalized);
        if (pr != HTTP_OK) {
            h_set_status(resp, pr == HTTP_ERR_HTTPS ? "HTTPS not supported yet (no TLS)" : "Bad URL");
            free(raw);
            return pr;
        }
        if (resp) h_strncpy0(resp->final_url, normalized, HTTP_URL_MAX);

        uint64_t sock = net_connect(parsed.host, parsed.port);
        if (sock == (uint64_t)-1 || sock == 0) {
            h_set_status(resp, "Connection failed (DNS/TCP error)");
            free(raw);
            return HTTP_ERR_CONNECT;
        }

        char req[512];
        int req_len = h_build_request(&parsed, req, sizeof(req));
        if (net_send(sock, req, req_len) < 0) {
            h_set_status(resp, "HTTP send failed");
            h_close_socket(sock);
            free(raw);
            return HTTP_ERR_SEND;
        }

        int raw_len = 0;
        int header_len = -1;
        int content_len = -1;
        int rr = h_read_response(sock, raw, HTTP_RAW_MAX, &raw_len, &header_len, &content_len);
        if (rr != HTTP_OK) {
            h_set_status(resp, "HTTP receive failed");
            h_close_socket(sock);
            free(raw);
            return rr;
        }
        if (raw_len >= HTTP_RAW_MAX - 1 && resp) resp->truncated = 1;
        if (header_len <= 0) {
            h_set_status(resp, "Invalid HTTP response");
            h_close_socket(sock);
            free(raw);
            return HTTP_ERR_RESPONSE;
        }

        int status_code = h_parse_status_code(raw, header_len);
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
                int ar = h_make_absolute_url(normalized, location, current_url);
                if (ar != HTTP_OK) {
                    h_set_status(resp, "Bad redirect URL");
                    h_close_socket(sock);
                    free(raw);
                    return ar;
                }
                h_close_socket(sock);
                continue;
            }
        }

        const char *body = raw + header_len;
        int body_len = raw_len - header_len;
        if (content_len >= 0 && body_len > content_len) body_len = content_len;

        int copied = is_chunked
            ? h_decode_chunked(body, body_len, out, out_max, resp)
            : h_copy_body(body, body_len, out, out_max, resp);

        if (resp) {
            resp->body_len = copied;
            resp->status[0] = 0;
            h_strcat(resp->status, "HTTP ");
            h_append_int(resp->status, status_code);
            if (resp->truncated) h_strcat(resp->status, " (truncated)");
        }

        h_close_socket(sock);
        free(raw);
        return HTTP_OK;
    }

    h_set_status(resp, "Too many redirects");
    free(raw);
    return HTTP_ERR_TOO_MANY_REDIRECTS;
}
