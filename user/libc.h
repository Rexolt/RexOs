#pragma once
#include <stdint.h>

#define SYS_WRITE      0
#define SYS_EXIT       1
#define SYS_YIELD      2
#define SYS_READ       3
#define SYS_GET_FB     4
#define SYS_SPAWN      5
#define SYS_OPEN       6
#define SYS_GETDENTS   7
#define SYS_WAITPID    8
#define SYS_BRK        9
#define SYS_TICKS      10
#define SYS_KBD_POLL   11
#define SYS_MOUSE      12
#define SYS_BLOCK_COUNT 13
#define SYS_BLOCK_INFO  14
#define SYS_PCI_COUNT   15
#define SYS_PCI_INFO    16
#define SYS_MKDIR       17
#define SYS_UNLINK      18
#define SYS_CLOSE       19
#define SYS_SEEK        20

#define O_RDONLY  0
#define O_WRONLY  1
#define O_RDWR    2
#define O_CREAT   0x40

typedef struct {
    char name[128];
    uint32_t ino;
} dirent_t;

uint64_t syscall1(uint64_t nr, uint64_t arg1);
uint64_t syscall2(uint64_t nr, uint64_t arg1, uint64_t arg2);
uint64_t syscall3(uint64_t nr, uint64_t arg1, uint64_t arg2, uint64_t arg3);

void exit(int code);
void yield(void);
int open(const char *path);
int read(int fd, void *buf, uint64_t count);
int write(int fd, const void *buf, uint64_t count);
int getdents(int fd, dirent_t *dir);
int spawn(const char *path);
void waitpid(int pid);

void print(const char *str);
void print_char(char c);

void *get_fb(uint64_t *width, uint64_t *height, uint64_t *pitch);

/* Memóriakezelés */
void *sbrk(int64_t increment);
void *malloc(uint64_t size);
void free(void *ptr);

/* Rendszer */
uint64_t get_ticks(void);
char kbd_poll(void);
void get_mouse(uint32_t *x, uint32_t *y, uint32_t *buttons);

/* Hardware introspection */
typedef struct {
    char     name[32];
    uint64_t sector_count;
    uint32_t sector_size;
    uint8_t  writable;
    uint8_t  _pad[3];
} block_info_t;

typedef struct {
    uint8_t  bus, dev, func;
    uint8_t  class_code, subclass, prog_if;
    uint16_t vendor;
    uint16_t device;
} pci_info_t;

int  block_dev_count(void);
int  block_dev_info(int idx, block_info_t *out);
int  pci_dev_count(void);
int  pci_dev_info(int idx, pci_info_t *out);

/* Fájlrendszer write-képes műveletek */
int open_ex(const char *path, int flags);
int close(int fd);
int mkdir(const char *path);
int unlink(const char *path);
int seek(int fd, uint64_t offset);
int write_file(int fd, const void *buf, uint64_t count);
