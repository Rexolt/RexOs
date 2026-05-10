/* ============================================================================
 *  Rex OS - Kernel heap allocator
 *
 *  Egyszerű first-fit allokátor implicit lánclistával.
 *  Minden blokk fejléc:
 *    +0  size_t    size       (a felhasználói payload mérete; header NÉLKÜL)
 *    +8  uint32_t  magic      (0xC0FFEE01 = used,  0xC0FFEEFE = free)
 *    +12 uint32_t  pad
 *    +16 block_t  *next       (lineáris lánc a heap végéig)
 *    +24 block_t  *prev       (backward coalescing-hez)
 *    -- payload kezdődik header után, 16-byte aligned --
 *
 *  Lefoglaláskor (kmalloc):
 *    - first-fit: az első szabad, elég nagy blokk
 *    - ha >> mint a kérés, ketté vágjuk
 *  Felszabadításkor (kfree):
 *    - a következő blokkkal coalesce-olunk, ha az is szabad
 *    - az előzővel coalesce-olunk, ha az is szabad
 * ========================================================================== */

#include <mm/heap.h>
#include <mm/pmm.h>
#include <mm/vmm.h>
#include <lib/string.h>
#include <lib/printf.h>
#include <lib/panic.h>

#define BLOCK_MAGIC_USED  0xC0FFEE01u
#define BLOCK_MAGIC_FREE  0xC0FFEEFEu

typedef struct block {
    size_t          size;        /* user payload size (header nélkül) */
    uint32_t        magic;
    uint32_t        _pad;
    struct block   *next;
    struct block   *prev;
} block_t;

_Static_assert(sizeof(block_t) == 32, "block_t must be 32 bytes for 16-byte alignment");

static block_t *s_head = NULL;       /* első blokk a heap-en */
static size_t   s_total_bytes = 0;

/* --- Helpers ------------------------------------------------------------ */

static inline size_t align_up(size_t x, size_t a)
{
    return (x + (a - 1)) & ~(a - 1);
}

static inline void *block_payload(block_t *b)
{
    return (void *)((uint8_t *)b + sizeof(block_t));
}

static inline block_t *payload_block(void *p)
{
    return (block_t *)((uint8_t *)p - sizeof(block_t));
}

static void check_block(block_t *b, const char *ctx)
{
    if (b->magic != BLOCK_MAGIC_USED && b->magic != BLOCK_MAGIC_FREE) {
        kprintf("[heap] CORRUPT block at %p, magic=0x%x (ctx=%s)\n",
                (void *)b, b->magic, ctx);
        kpanic("kernel heap corruption detected");
    }
}

/* --- Init --------------------------------------------------------------- */

void kheap_init(void)
{
    size_t bytes  = KHEAP_INITIAL_SIZE;
    size_t frames = bytes / 4096;

    /* PMM-ből frame-ek, VMM-mel mapping. Nem szükséges, hogy
     * fizikailag összefüggő legyen, csak virtuálisan. */
    for (size_t i = 0; i < frames; i++) {
        uintptr_t phys = pmm_alloc_frame();
        if (!phys) kpanic("kheap_init: out of physical memory");
        uintptr_t virt = KHEAP_VIRT_BASE + i * 4096;
        if (!vmm_map_page(virt, phys, PAGE_PRESENT | PAGE_WRITABLE)) {
            kpanic("kheap_init: vmm_map_page failed");
        }
    }

    s_head = (block_t *)KHEAP_VIRT_BASE;
    s_head->size  = bytes - sizeof(block_t);
    s_head->magic = BLOCK_MAGIC_FREE;
    s_head->next  = NULL;
    s_head->prev  = NULL;
    s_total_bytes = bytes;

    kprintf("[heap] virt 0x%lx .. 0x%lx (%lu KB), single free block of %lu bytes\n",
            (uint64_t)KHEAP_VIRT_BASE,
            (uint64_t)KHEAP_VIRT_BASE + bytes,
            (uint64_t)bytes / 1024,
            (uint64_t)s_head->size);
}

/* --- kmalloc ------------------------------------------------------------ */

void *kmalloc(size_t size)
{
    if (!size) return NULL;
    if (!s_head) return NULL;

    size = align_up(size, KHEAP_ALIGN);

    for (block_t *b = s_head; b; b = b->next) {
        check_block(b, "kmalloc");

        if (b->magic == BLOCK_MAGIC_FREE && b->size >= size) {
            /* Split, ha érdemes (marad legalább header + 16 byte payload). */
            size_t leftover = b->size - size;
            if (leftover >= sizeof(block_t) + KHEAP_ALIGN) {
                block_t *split =
                    (block_t *)((uint8_t *)b + sizeof(block_t) + size);
                split->size  = leftover - sizeof(block_t);
                split->magic = BLOCK_MAGIC_FREE;
                split->next  = b->next;
                split->prev  = b;
                if (b->next) b->next->prev = split;
                b->next = split;
                b->size = size;
            }
            b->magic = BLOCK_MAGIC_USED;
            return block_payload(b);
        }
    }
    return NULL;  /* OOM */
}

void *kzalloc(size_t size)
{
    void *p = kmalloc(size);
    if (p) kmemset(p, 0, size);
    return p;
}

void *kcalloc(size_t n, size_t size)
{
    /* Egyszerű overflow védelem. */
    if (n && size > (size_t)-1 / n) return NULL;
    return kzalloc(n * size);
}

/* --- kfree -------------------------------------------------------------- */

void kfree(void *ptr)
{
    if (!ptr) return;
    block_t *b = payload_block(ptr);
    check_block(b, "kfree");
    if (b->magic == BLOCK_MAGIC_FREE) {
        kpanic("kfree: double free detected");
    }
    b->magic = BLOCK_MAGIC_FREE;

    /* Forward coalesce. */
    if (b->next && b->next->magic == BLOCK_MAGIC_FREE) {
        b->size += sizeof(block_t) + b->next->size;
        b->next = b->next->next;
        if (b->next) b->next->prev = b;
    }
    /* Backward coalesce. */
    if (b->prev && b->prev->magic == BLOCK_MAGIC_FREE) {
        b->prev->size += sizeof(block_t) + b->size;
        b->prev->next = b->next;
        if (b->next) b->next->prev = b->prev;
    }
}

/* --- krealloc ----------------------------------------------------------- */

void *krealloc(void *ptr, size_t new_size)
{
    if (!ptr) return kmalloc(new_size);
    if (!new_size) { kfree(ptr); return NULL; }

    block_t *b = payload_block(ptr);
    check_block(b, "krealloc");

    size_t aligned = align_up(new_size, KHEAP_ALIGN);
    if (b->size >= aligned) return ptr;  /* in-place shrink, nem split-eljük */

    /* Egyszerű: új allokáció + másolás + free. */
    void *np = kmalloc(new_size);
    if (!np) return NULL;
    kmemcpy(np, ptr, b->size);
    kfree(ptr);
    return np;
}

/* --- Diagnosztika ------------------------------------------------------- */

void kheap_get_stats(kheap_stats_t *out)
{
    if (!out) return;
    kmemset(out, 0, sizeof(*out));
    out->total_bytes = s_total_bytes;

    for (block_t *b = s_head; b; b = b->next) {
        out->block_count++;
        if (b->magic == BLOCK_MAGIC_FREE) {
            out->free_block_count++;
            out->free_bytes += b->size;
            if (b->size > out->largest_free) out->largest_free = b->size;
        } else {
            out->used_bytes += b->size;
        }
    }
}

void kheap_dump(void)
{
    kprintf("[heap] dump:\n");
    int idx = 0;
    for (block_t *b = s_head; b; b = b->next, idx++) {
        kprintf("  [%d] %p  size=%lu  %s\n",
                idx, (void *)b, (uint64_t)b->size,
                b->magic == BLOCK_MAGIC_FREE ? "FREE" : "USED");
    }
}
