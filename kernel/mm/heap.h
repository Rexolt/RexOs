/* Rex OS - Kernel heap (kmalloc/kfree)
 *
 * Implicit lánclista, first-fit, forward coalescing.
 * A heap a VMM tetejére épül: PMM-ből allokált lapok, virtuálisan
 * összefüggő tartományba mappelve.
 */
#pragma once
#include <rexos/types.h>

#define KHEAP_VIRT_BASE      0xffff900000000000ULL
#define KHEAP_INITIAL_SIZE   (1 * 1024 * 1024)   /* 1 MB */
#define KHEAP_ALIGN          16

void  kheap_init(void);

void *kmalloc(size_t size);
void *kzalloc(size_t size);                /* kmalloc + nullázás */
void *kcalloc(size_t n, size_t size);
void *krealloc(void *ptr, size_t new_size);
void  kfree(void *ptr);

/* Diagnosztika */
typedef struct {
    size_t total_bytes;     /* heap teljes mérete */
    size_t used_bytes;      /* foglalt bájtok (header nélkül) */
    size_t free_bytes;      /* szabad bájtok */
    size_t block_count;     /* összes blokk */
    size_t free_block_count;
    size_t largest_free;    /* legnagyobb összefüggő szabad blokk */
} kheap_stats_t;

void kheap_get_stats(kheap_stats_t *out);
void kheap_dump(void);                     /* részletes lista a serial/console-ra */
