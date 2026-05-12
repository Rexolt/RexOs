#pragma once
#include <stdint.h>

#define HTTP_URL_MAX       256
#define HTTP_STATUS_MAX     64
#define HTTP_MAX_REDIRECTS   3

#define HTTP_OK                 0
#define HTTP_ERR_BAD_URL       -1
#define HTTP_ERR_HTTPS         -2
#define HTTP_ERR_CONNECT       -3
#define HTTP_ERR_SEND          -4
#define HTTP_ERR_RECV          -5
#define HTTP_ERR_NO_MEMORY     -6
#define HTTP_ERR_RESPONSE      -7
#define HTTP_ERR_TOO_MANY_REDIRECTS -8

typedef struct {
    int status_code;
    int body_len;
    int header_len;
    int truncated;
    int chunked;
    char final_url[HTTP_URL_MAX];
    char status[HTTP_STATUS_MAX];
} http_response_t;

int http_get(const char *url, char *out, uint64_t out_max, http_response_t *resp);
int http_get_alloc(const char *url, char **out_body, uint64_t max_body, http_response_t *resp);
