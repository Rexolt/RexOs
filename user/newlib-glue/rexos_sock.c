/* BSD socket réteg a RexOS TCP syscallok fölött.
 *
 * A kernel modellje: RSYS_NET_CONNECT(hostname, port) -> socket token
 * (DNS-t is a kernel old). A BSD API-t erre képezzük:
 *   socket()  -> lefoglal egy slotot (kapcsolat még nincs)
 *   connect() -> sockaddr_in IP-t "a.b.c.d" stringgé alakítjuk és a kernel
 *                connectjét hívjuk (a kernel IP literált is elfogad)
 *   send/recv -> natív syscallok
 * A gethostbyname/getaddrinfo a nevet ELTÁROLJA a címben (a kernel úgyis
 * névvel kapcsolódik) - ehhez az in_addr-be egy indexet teszünk, ami a
 * névtáblánkra mutat (REXOS_NAME_MAGIC | index).
 */
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "rexos_syscall.h"

#define MAX_SOCK   32
#define MAX_NAMES  32
#define NAME_MAX_L 255

/* 240.77.x.x - a "rezervált E osztályba" kódolt névtábla index (sosem valódi
 * cél IP a gyakorlatban) */
#define REXOS_NAME_MAGIC 0xF04D0000u

struct rsock {
    int      used;
    uint64_t handle;    /* kernel socket token, 0 = még nincs kapcsolat */
};

static struct rsock s_socks[MAX_SOCK];
static char s_names[MAX_NAMES][NAME_MAX_L + 1];
static int  s_name_next;

/* --- névtábla --------------------------------------------------------------- */

static uint32_t name_store(const char *name)
{
    int idx = s_name_next;
    s_name_next = (s_name_next + 1) % MAX_NAMES;
    strncpy(s_names[idx], name, NAME_MAX_L);
    s_names[idx][NAME_MAX_L] = 0;
    return REXOS_NAME_MAGIC | (uint32_t)idx;
}

static const char *name_lookup(uint32_t host_order_addr)
{
    if ((host_order_addr & 0xFFFF0000u) != REXOS_NAME_MAGIC) return NULL;
    unsigned idx = host_order_addr & 0xFFFFu;
    if (idx >= MAX_NAMES) return NULL;
    return s_names[idx];
}

/* --- inet_* ----------------------------------------------------------------- */

int inet_aton(const char *cp, struct in_addr *inp)
{
    unsigned a, b, c, d;
    if (sscanf(cp, "%u.%u.%u.%u", &a, &b, &c, &d) != 4) return 0;
    if (a > 255 || b > 255 || c > 255 || d > 255) return 0;
    inp->s_addr = htonl((a << 24) | (b << 16) | (c << 8) | d);
    return 1;
}

in_addr_t inet_addr(const char *cp)
{
    struct in_addr in;
    if (!inet_aton(cp, &in)) return INADDR_NONE;
    return in.s_addr;
}

char *inet_ntoa(struct in_addr in)
{
    static char buf[INET_ADDRSTRLEN];
    uint32_t v = ntohl(in.s_addr);
    snprintf(buf, sizeof(buf), "%u.%u.%u.%u",
             (v >> 24) & 0xFF, (v >> 16) & 0xFF, (v >> 8) & 0xFF, v & 0xFF);
    return buf;
}

int inet_pton(int af, const char *src, void *dst)
{
    if (af != AF_INET) { errno = EAFNOSUPPORT; return -1; }
    struct in_addr in;
    if (!inet_aton(src, &in)) return 0;
    memcpy(dst, &in, sizeof(in));
    return 1;
}

const char *inet_ntop(int af, const void *src, char *dst, socklen_t size)
{
    if (af != AF_INET) { errno = EAFNOSUPPORT; return NULL; }
    struct in_addr in;
    memcpy(&in, src, sizeof(in));
    const char *s = inet_ntoa(in);
    if (strlen(s) + 1 > size) { errno = ENOSPC; return NULL; }
    strcpy(dst, s);
    return dst;
}

/* --- névfeloldás ------------------------------------------------------------- */

struct hostent *gethostbyname(const char *name)
{
    static struct hostent he;
    static struct in_addr addr;
    static char *addr_list[2];
    static char *aliases[1];
    static char hname[NAME_MAX_L + 1];

    /* IP literál? */
    if (!inet_aton(name, &addr)) {
        /* Név: a kernel connectje oldja fel; indexet kódolunk a címbe */
        addr.s_addr = htonl(name_store(name));
    }
    strncpy(hname, name, NAME_MAX_L);
    hname[NAME_MAX_L] = 0;

    he.h_name = hname;
    he.h_aliases = aliases;      aliases[0] = NULL;
    he.h_addrtype = AF_INET;
    he.h_length = 4;
    he.h_addr_list = addr_list;  addr_list[0] = (char *)&addr; addr_list[1] = NULL;
    return &he;
}

int getaddrinfo(const char *node, const char *service,
                const struct addrinfo *hints, struct addrinfo **res)
{
    (void)hints;
    if (!node) return EAI_NONAME;

    struct addrinfo *ai = calloc(1, sizeof(*ai));
    struct sockaddr_in *sa = calloc(1, sizeof(*sa));
    if (!ai || !sa) { free(ai); free(sa); return EAI_MEMORY; }

    sa->sin_family = AF_INET;
    sa->sin_port = htons(service ? (uint16_t)atoi(service) : 0);
    if (!inet_aton(node, &sa->sin_addr)) {
        sa->sin_addr.s_addr = htonl(name_store(node));
    }

    ai->ai_family = AF_INET;
    ai->ai_socktype = SOCK_STREAM;
    ai->ai_protocol = IPPROTO_TCP;
    ai->ai_addrlen = sizeof(*sa);
    ai->ai_addr = (struct sockaddr *)sa;
    *res = ai;
    return 0;
}

void freeaddrinfo(struct addrinfo *res)
{
    while (res) {
        struct addrinfo *n = res->ai_next;
        free(res->ai_addr);
        free(res->ai_canonname);
        free(res);
        res = n;
    }
}

const char *gai_strerror(int errcode)
{
    (void)errcode;
    return "name resolution error";
}

/* --- BSD socket API ----------------------------------------------------------
 * fd-k: a fájl fd-kkel közös térben nem ütközhetnek; 1000+index tartományt
 * használunk, és minden itteni hívás csak ezt a tartományt fogadja el. */

#define SOCK_FD_BASE 1000

static struct rsock *sock_of(int fd)
{
    int idx = fd - SOCK_FD_BASE;
    if (idx < 0 || idx >= MAX_SOCK || !s_socks[idx].used) return NULL;
    return &s_socks[idx];
}

int socket(int domain, int type, int protocol)
{
    (void)protocol;
    if (domain != AF_INET || type != SOCK_STREAM) {
        errno = EAFNOSUPPORT;
        return -1;
    }
    for (int i = 0; i < MAX_SOCK; i++) {
        if (!s_socks[i].used) {
            s_socks[i].used = 1;
            s_socks[i].handle = 0;
            return SOCK_FD_BASE + i;
        }
    }
    errno = EMFILE;
    return -1;
}

int connect(int fd, const struct sockaddr *addr, socklen_t len)
{
    struct rsock *s = sock_of(fd);
    if (!s) { errno = EBADF; return -1; }
    if (!addr || len < sizeof(struct sockaddr_in) ||
        addr->sa_family != AF_INET) {
        errno = EINVAL;
        return -1;
    }
    const struct sockaddr_in *sin = (const struct sockaddr_in *)addr;
    uint16_t port = ntohs(sin->sin_port);

    /* Névtábla-hivatkozás vagy IP literál */
    const char *host = name_lookup(ntohl(sin->sin_addr.s_addr));
    char ipbuf[INET_ADDRSTRLEN];
    if (!host) {
        struct in_addr in = sin->sin_addr;
        strcpy(ipbuf, inet_ntoa(in));
        host = ipbuf;
    }

    uint64_t h = rexos_net_connect(host, port);
    if (h == (uint64_t)-1) {
        errno = ECONNREFUSED;
        return -1;
    }
    s->handle = h;
    return 0;
}

ssize_t send(int fd, const void *buf, size_t n, int flags)
{
    (void)flags;
    struct rsock *s = sock_of(fd);
    if (!s || !s->handle) { errno = EBADF; return -1; }
    int r = rexos_net_send(s->handle, buf, n);
    if (r < 0) { errno = EPIPE; return -1; }
    return r;
}

ssize_t recv(int fd, void *buf, size_t n, int flags)
{
    struct rsock *s = sock_of(fd);
    if (!s || !s->handle) { errno = EBADF; return -1; }

    /* A kernelben nincs külön nem blokkoló recv: a RSYS_NET_RECV úgyis
     * visszatér a saját időkorlátjával, és megkülönbözteti a "nincs adat"
     * (0) és a "lezárt kapcsolat" (NET_RECV_EOF) esetet. A MSG_DONTWAIT így
     * a hívó felé helyes választ ad, csak nem azonnal. Jelenleg semmi nem
     * használja ezt az ágat, a NetSurf fetcher a syscallt közvetlenül hívja. */
    int r = rexos_net_recv(s->handle, buf, n);
    if (r == RSYS_NET_RECV_EOF) return 0;          /* lezárt kapcsolat = EOF */
    if (r < 0) { errno = ECONNRESET; return -1; }
    if (r == 0 && (flags & MSG_DONTWAIT)) { errno = EAGAIN; return -1; }
    return r;
}

static int sock_close(int fd)
{
    struct rsock *s = sock_of(fd);
    if (!s) return -1;
    if (s->handle) rexos_net_close(s->handle);
    s->used = 0;
    s->handle = 0;
    return 0;
}

/* A close()-t a syscalls.c-ből hívjuk vissza socket fd-kre. */
int __rexos_sock_close(int fd);
int __rexos_sock_close(int fd)
{
    return sock_close(fd);
}

int shutdown(int fd, int how)
{
    (void)how;
    struct rsock *s = sock_of(fd);
    if (!s) { errno = EBADF; return -1; }
    return 0;
}

int bind(int fd, const struct sockaddr *addr, socklen_t len)
{
    (void)fd; (void)addr; (void)len;
    errno = ENOSYS;
    return -1;
}

int listen(int fd, int backlog)
{
    (void)fd; (void)backlog;
    errno = ENOSYS;
    return -1;
}

int accept(int fd, struct sockaddr *addr, socklen_t *len)
{
    (void)fd; (void)addr; (void)len;
    errno = ENOSYS;
    return -1;
}

int getsockopt(int fd, int level, int opt, void *val, socklen_t *len)
{
    (void)level;
    struct rsock *s = sock_of(fd);
    if (!s) { errno = EBADF; return -1; }
    if (opt == SO_ERROR && val && len && *len >= sizeof(int)) {
        *(int *)val = 0;
        *len = sizeof(int);
        return 0;
    }
    errno = ENOPROTOOPT;
    return -1;
}

int setsockopt(int fd, int level, int opt, const void *val, socklen_t len)
{
    (void)level; (void)opt; (void)val; (void)len;
    return sock_of(fd) ? 0 : (errno = EBADF, -1);
}

int getsockname(int fd, struct sockaddr *addr, socklen_t *len)
{
    (void)fd; (void)addr; (void)len;
    errno = ENOSYS;
    return -1;
}

int getpeername(int fd, struct sockaddr *addr, socklen_t *len)
{
    (void)fd; (void)addr; (void)len;
    errno = ENOSYS;
    return -1;
}

ssize_t sendto(int fd, const void *buf, size_t n, int flags,
               const struct sockaddr *addr, socklen_t len)
{
    (void)addr; (void)len;
    return send(fd, buf, n, flags);
}

ssize_t recvfrom(int fd, void *buf, size_t n, int flags,
                 struct sockaddr *addr, socklen_t *len)
{
    (void)addr; (void)len;
    return recv(fd, buf, n, flags);
}

/* --- select ------------------------------------------------------------------
 * Egyszerűsített: nem blokkol, csak jelzi hogy "minden kész" (a NetSurf
 * fetch pollozó modellje úgyis rendszeresen újrapróbál). */
#include <sys/select.h>

int select(int nfds, fd_set *readfds, fd_set *writefds,
           fd_set *exceptfds, struct timeval *timeout)
{
    (void)nfds; (void)readfds; (void)writefds; (void)exceptfds;
    if (timeout) {
        uint64_t ms = (uint64_t)timeout->tv_sec * 1000 +
                      (uint64_t)timeout->tv_usec / 1000;
        if (ms) rexos_syscall1(RSYS_SLEEP_MS, ms);
    }
    return 0;
}
