/* RexOS nyers syscall interfész a newlib glue és a portolt könyvtárak számára.
 * A syscall számok a kernel/arch/x86_64/syscall.c-vel és a user/libc.h-val
 * vannak szinkronban. */
#pragma once
#include <stdint.h>

#define RSYS_WRITE        0
#define RSYS_EXIT         1
#define RSYS_YIELD        2
#define RSYS_READ         3
#define RSYS_GET_FB       4
#define RSYS_SPAWN        5
#define RSYS_OPEN         6
#define RSYS_GETDENTS     7
#define RSYS_WAITPID      8
#define RSYS_BRK          9
#define RSYS_TICKS        10
#define RSYS_KBD_POLL     11
#define RSYS_MOUSE        12
#define RSYS_MKDIR        17
#define RSYS_UNLINK       18
#define RSYS_CLOSE        19
#define RSYS_SEEK         20
#define RSYS_NET_CONNECT  21
#define RSYS_NET_SEND     22
#define RSYS_NET_RECV     23
#define RSYS_TIME         24
#define RSYS_NET_CLOSE    25
#define RSYS_RANDOM       26
#define RSYS_STAT         27
#define RSYS_FSTAT        28
#define RSYS_SLEEP_MS     29

/* A RSYS_NET_RECV visszatérési értékei: >0 = byte-ok, 0 = nincs adat
 * (a kernel időkorlátja lejárt), RSYS_NET_RECV_EOF = a kapcsolat
 * lezárult és a puffer üres. */
#define RSYS_NET_RECV_EOF (-2)

/* Kernel oldali O_CREAT (a newlib sajátja 0x0200, fordítás kell!) */
#define RSYS_O_CREAT      0x40

/* RSYS_STAT / RSYS_FSTAT kimeneti struktúra */
typedef struct {
    uint64_t length;
    uint32_t flags;   /* 1 = fájl, 2 = könyvtár (kernel FS_FILE/FS_DIRECTORY) */
    uint32_t inode;
} rexos_stat_t;

/* RSYS_TIME kimenete. A layoutnak byte-ra egyeznie kell a kernel
 * rtc_time_t-jével (rexos/rtc.h): NEM packed, a year offset 8-on, sizeof 12. */
typedef struct {
    uint8_t  second, minute, hour, day, month;
    uint32_t year;
} rexos_rtc_time_t;

static inline uint64_t rexos_syscall3(uint64_t nr, uint64_t a1, uint64_t a2, uint64_t a3)
{
    uint64_t ret;
    __asm__ volatile("syscall"
                     : "=a"(ret)
                     : "a"(nr), "D"(a1), "S"(a2), "d"(a3)
                     : "rcx", "r11", "memory");
    return ret;
}

static inline uint64_t rexos_syscall1(uint64_t nr, uint64_t a1)
{
    return rexos_syscall3(nr, a1, 0, 0);
}

static inline uint64_t rexos_syscall2(uint64_t nr, uint64_t a1, uint64_t a2)
{
    return rexos_syscall3(nr, a1, a2, 0);
}

/* --- Framebuffer ------------------------------------------------------------ */

/* 32 bpp XRGB framebuffer usermode mappingje (0xA0000000). */
static inline void *rexos_get_fb(uint64_t *width, uint64_t *height, uint64_t *pitch)
{
    return (void *)rexos_syscall3(RSYS_GET_FB, (uint64_t)width,
                                  (uint64_t)height, (uint64_t)pitch);
}

/* --- Hálózat (nyers TCP a portokhoz; BSD socket shim később épül rá) ------ */

static inline uint64_t rexos_net_connect(const char *host, uint16_t port)
{
    return rexos_syscall2(RSYS_NET_CONNECT, (uint64_t)host, port);
}
static inline int rexos_net_send(uint64_t sock, const void *buf, uint64_t len)
{
    return (int)rexos_syscall3(RSYS_NET_SEND, sock, (uint64_t)buf, len);
}
static inline int rexos_net_recv(uint64_t sock, void *buf, uint64_t max)
{
    return (int)rexos_syscall3(RSYS_NET_RECV, sock, (uint64_t)buf, max);
}
static inline int rexos_net_close(uint64_t sock)
{
    return (int)rexos_syscall1(RSYS_NET_CLOSE, sock);
}
