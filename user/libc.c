#include "libc.h"

uint64_t syscall1(uint64_t nr, uint64_t arg1) {
    uint64_t ret;
    __asm__ volatile (
        "syscall"
        : "=a"(ret)
        : "a"(nr), "D"(arg1)
        : "rcx", "r11", "memory"
    );
    return ret;
}

uint64_t syscall2(uint64_t nr, uint64_t arg1, uint64_t arg2) {
    uint64_t ret;
    __asm__ volatile (
        "syscall"
        : "=a"(ret)
        : "a"(nr), "D"(arg1), "S"(arg2)
        : "rcx", "r11", "memory"
    );
    return ret;
}

uint64_t syscall3(uint64_t nr, uint64_t arg1, uint64_t arg2, uint64_t arg3) {
    uint64_t ret;
    __asm__ volatile (
        "syscall"
        : "=a"(ret)
        : "a"(nr), "D"(arg1), "S"(arg2), "d"(arg3)
        : "rcx", "r11", "memory"
    );
    return ret;
}

void exit(int code) {
    syscall1(SYS_EXIT, (uint64_t)code);
    while (1);
}

void yield(void) {
    syscall1(SYS_YIELD, 0);
}

int open(const char *path) {
    return (int)syscall1(SYS_OPEN, (uint64_t)path);
}

int read(int fd, void *buf, uint64_t count) {
    return (int)syscall3(SYS_READ, (uint64_t)fd, (uint64_t)buf, count);
}

int write(int fd, const void *buf, uint64_t count) {
    return (int)syscall3(SYS_WRITE, (uint64_t)fd, (uint64_t)buf, count);
}

int getdents(int fd, dirent_t *dir) {
    return (int)syscall2(SYS_GETDENTS, (uint64_t)fd, (uint64_t)dir);
}

int spawn(const char *path) {
    return (int)syscall1(SYS_SPAWN, (uint64_t)path);
}

void waitpid(int pid) {
    syscall1(SYS_WAITPID, (uint64_t)pid);
}

void print(const char *str) {
    uint64_t len = 0;
    while (str[len]) len++;
    write(1, str, len);
}

void print_char(char c) {
    char buf[2] = {c, 0};
    write(1, buf, 1);
}

void *get_fb(uint64_t *width, uint64_t *height, uint64_t *pitch) {
    return (void *)syscall3(SYS_GET_FB, (uint64_t)width, (uint64_t)height, (uint64_t)pitch);
}

uint64_t get_ticks(void) {
    return syscall1(SYS_TICKS, 0);
}

char kbd_poll(void) {
    return (char)syscall1(SYS_KBD_POLL, 0);
}

void get_mouse(uint32_t *x, uint32_t *y, uint32_t *buttons) {
    uint32_t buf[3];
    syscall1(SYS_MOUSE, (uint64_t)buf);
    *x = buf[0]; *y = buf[1]; *buttons = buf[2];
}

/* --- Hardware introspection --- */

int block_dev_count(void) {
    return (int)syscall1(SYS_BLOCK_COUNT, 0);
}

int block_dev_info(int idx, block_info_t *out) {
    return (int)syscall2(SYS_BLOCK_INFO, (uint64_t)idx, (uint64_t)out);
}

int pci_dev_count(void) {
    return (int)syscall1(SYS_PCI_COUNT, 0);
}

int pci_dev_info(int idx, pci_info_t *out) {
    return (int)syscall2(SYS_PCI_INFO, (uint64_t)idx, (uint64_t)out);
}

/* --- Memóriakezelés (malloc / free) --- */

void *sbrk(int64_t increment) {
    uint64_t old_brk = syscall1(SYS_BRK, 0);
    if (increment == 0) return (void *)old_brk;
    uint64_t new_brk = syscall1(SYS_BRK, old_brk + increment);
    if (new_brk == old_brk) return (void *)-1; /* hiba */
    return (void *)old_brk;
}

/* Egyszerű first-fit allocator header-es blokkokkal */
typedef struct block_header {
    uint64_t size;          /* adat méret (header nélkül) */
    uint64_t used;          /* 1 = foglalt, 0 = szabad */
    struct block_header *next;
} block_header_t;

static block_header_t *heap_head = 0;

void *malloc(uint64_t size) {
    if (size == 0) return 0;
    
    /* 8-byte igazítás */
    size = (size + 7) & ~7ULL;
    
    /* Keressünk szabad blokkot (first-fit) */
    block_header_t *b = heap_head;
    while (b) {
        if (!b->used && b->size >= size) {
            b->used = 1;
            return (void *)((uint8_t *)b + sizeof(block_header_t));
        }
        b = b->next;
    }
    
    /* Nincs szabad blokk → brk-vel növeljük a heap-et */
    uint64_t total = sizeof(block_header_t) + size;
    block_header_t *new_block = (block_header_t *)sbrk(total);
    if ((int64_t)new_block == -1) return 0;
    
    new_block->size = size;
    new_block->used = 1;
    new_block->next = 0;
    
    /* Láncba fűzés */
    if (!heap_head) {
        heap_head = new_block;
    } else {
        block_header_t *tail = heap_head;
        while (tail->next) tail = tail->next;
        tail->next = new_block;
    }
    
    return (void *)((uint8_t *)new_block + sizeof(block_header_t));
}

void free(void *ptr) {
    if (!ptr) return;
    block_header_t *b = (block_header_t *)((uint8_t *)ptr - sizeof(block_header_t));
    b->used = 0;
}

