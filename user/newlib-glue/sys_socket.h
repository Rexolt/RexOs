/* Minimál BSD <sys/socket.h> RexOS-hez.
 * A NetSurf core fordításához kell; a tényleges hálózati forgalom a RexOS
 * natív syscallokon megy (rexos_syscall.h), a BSD API itt csak vékony réteg. */
#ifndef _SYS_SOCKET_H
#define _SYS_SOCKET_H

#include <sys/types.h>
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef uint32_t socklen_t;
typedef uint16_t sa_family_t;

#define AF_UNSPEC  0
#define AF_INET    2
#define AF_INET6   10
#define PF_UNSPEC  AF_UNSPEC
#define PF_INET    AF_INET
#define PF_INET6   AF_INET6

#define SOCK_STREAM 1
#define SOCK_DGRAM  2

#define SOL_SOCKET   1
#define SO_REUSEADDR 2
#define SO_ERROR     4
#define SO_KEEPALIVE 9
#define SO_RCVTIMEO  20
#define SO_SNDTIMEO  21

#define SHUT_RD   0
#define SHUT_WR   1
#define SHUT_RDWR 2

#define MSG_PEEK     0x02
#define MSG_DONTWAIT 0x40
#define MSG_NOSIGNAL 0x4000

struct sockaddr {
    sa_family_t sa_family;
    char        sa_data[14];
};

struct sockaddr_storage {
    sa_family_t ss_family;
    char        __ss_pad[126];
};

int     socket(int domain, int type, int protocol);
int     connect(int fd, const struct sockaddr *addr, socklen_t len);
int     bind(int fd, const struct sockaddr *addr, socklen_t len);
int     listen(int fd, int backlog);
int     accept(int fd, struct sockaddr *addr, socklen_t *len);
int     shutdown(int fd, int how);
int     getsockopt(int fd, int level, int opt, void *val, socklen_t *len);
int     setsockopt(int fd, int level, int opt, const void *val, socklen_t len);
int     getsockname(int fd, struct sockaddr *addr, socklen_t *len);
int     getpeername(int fd, struct sockaddr *addr, socklen_t *len);
ssize_t send(int fd, const void *buf, size_t n, int flags);
ssize_t recv(int fd, void *buf, size_t n, int flags);
ssize_t sendto(int fd, const void *buf, size_t n, int flags,
               const struct sockaddr *addr, socklen_t len);
ssize_t recvfrom(int fd, void *buf, size_t n, int flags,
                 struct sockaddr *addr, socklen_t *len);

#ifdef __cplusplus
}
#endif

#endif /* _SYS_SOCKET_H */
