/* ============================================================================
 *  Rex OS - Physical Memory Manager
 *
 *  Bitmap-alapú frame allocator:
 *    - 1 bit / 4 KB keret (1=foglalt, 0=szabad)
 *    - A bitmap maga az első olyan USABLE memóriaregionban él, amiben elfér
 *    - A Limine memmap-jából tanuljuk meg, mi USABLE és mi nem
 *    - A HHDM (Higher-Half Direct Map) segítségével írjuk a bitmapet
 *     -Slab jobb lenne, de ez bemelegítésnek tökéletes.
 *  Allokáció: lineáris keresés (O(n)). Egyszerű, de bemelegítésnek tökéletes.
 *  Felszabadítás: O(1).
 * ========================================================================== */

#include <mm/pmm.h>
#include <lib/string.h>
#include <lib/printf.h>
#include <lib/panic.h>
#include <limine.h>

/* --- Limine requests --------------------------------------------------- */

__attribute__((used, section(".limine_requests")))
static volatile struct limine_memmap_request memmap_request = {
    .id       = LIMINE_MEMMAP_REQUEST,
    .revision = 0,
    .response = NULL,
};

__attribute__((used, section(".limine_requests")))
static volatile struct limine_hhdm_request hhdm_request = {
    .id       = LIMINE_HHDM_REQUEST,
    .revision = 0,
    .response = NULL,
};

/* --- Internal state ---------------------------------------------------- */

static uint8_t  *s_bitmap        = NULL;
static size_t    s_bitmap_size   = 0;     /* byte-ban */
static size_t    s_total_frames  = 0;
static size_t    s_used_frames   = 0;
static size_t    s_search_hint   = 0;     /* gyorsabb újrakezdés */
static uintptr_t s_hhdm_offset   = 0;

/* --- Bitmap primitívek ------------------------------------------------- */

static inline bool bm_test(size_t i)
{
    return (s_bitmap[i >> 3] >> (i & 7)) & 1u;
}

static inline void bm_set(size_t i)
{
    s_bitmap[i >> 3] |= (uint8_t)(1u << (i & 7));
}

static inline void bm_clear(size_t i)
{
    s_bitmap[i >> 3] &= (uint8_t)~(1u << (i & 7));
}

/* --- Memmap típusnevek (logoláshoz) ------------------------------------ */

static const char *memmap_type_name(uint64_t type)
{
    switch (type) {
    case LIMINE_MEMMAP_USABLE:                 return "Usable";
    case LIMINE_MEMMAP_RESERVED:               return "Reserved";
    case LIMINE_MEMMAP_ACPI_RECLAIMABLE:       return "ACPI Reclaim";
    case LIMINE_MEMMAP_ACPI_NVS:               return "ACPI NVS";
    case LIMINE_MEMMAP_BAD_MEMORY:             return "Bad Memory";
    case LIMINE_MEMMAP_BOOTLOADER_RECLAIMABLE: return "Bootloader Reclaim";
    case LIMINE_MEMMAP_KERNEL_AND_MODULES:     return "Kernel+Modules";
    case LIMINE_MEMMAP_FRAMEBUFFER:            return "Framebuffer";
    default:                                   return "Unknown";
    }
}

/* --- Init -------------------------------------------------------------- */

void pmm_init(void)
{
    if (!memmap_request.response) kpanic("pmm: Limine memmap response is NULL");
    if (!hhdm_request.response)   kpanic("pmm: Limine HHDM response is NULL");

    s_hhdm_offset = hhdm_request.response->offset;

    struct limine_memmap_response *mm = memmap_request.response;

    kprintf("[pmm] HHDM offset       : 0x%lx\n", s_hhdm_offset);
    kprintf("[pmm] memory map entries: %lu\n",   mm->entry_count);

    /* 1) Legmagasabb USABLE fizikai cím megtalálása */
    uintptr_t top = 0;
    uint64_t  usable_total = 0;
    for (uint64_t i = 0; i < mm->entry_count; i++) {
        struct limine_memmap_entry *e = mm->entries[i];
        uintptr_t end = (uintptr_t)(e->base + e->length);

        kprintf("  [%2lu] 0x%016lx - 0x%016lx (%6lu KB) %s\n",
                i, e->base, end, e->length / 1024,
                memmap_type_name(e->type));

        if (e->type == LIMINE_MEMMAP_USABLE) {
            usable_total += e->length;
            if (end > top) top = end;
        }
    }

    s_total_frames = top / PAGE_SIZE;
    s_bitmap_size  = (s_total_frames + 7) / 8;

    kprintf("[pmm] usable RAM total  : %lu MB\n", usable_total / (1024 * 1024));
    kprintf("[pmm] top of usable RAM : 0x%lx\n",  top);
    kprintf("[pmm] total frames      : %lu\n",   s_total_frames);
    kprintf("[pmm] bitmap size       : %lu bytes\n", s_bitmap_size);

    /* 2) Helyet keresünk a bitmap-nek: az első USABLE region, ami elég nagy */
    uintptr_t bitmap_phys = 0;
    for (uint64_t i = 0; i < mm->entry_count; i++) {
        struct limine_memmap_entry *e = mm->entries[i];
        if (e->type != LIMINE_MEMMAP_USABLE) continue;
        if (e->length >= s_bitmap_size) {
            bitmap_phys = (uintptr_t)e->base;
            break;
        }
    }
    if (bitmap_phys == 0) kpanic("pmm: no usable region large enough for bitmap");

    s_bitmap = (uint8_t *)phys_to_virt(bitmap_phys);

    /* 3) Mindent FOGLALT-ra állítunk (biztonságos default) */
    kmemset(s_bitmap, 0xFF, s_bitmap_size);
    s_used_frames = s_total_frames;

    /* 4) USABLE régiókat felszabadítjuk */
    for (uint64_t i = 0; i < mm->entry_count; i++) {
        struct limine_memmap_entry *e = mm->entries[i];
        if (e->type != LIMINE_MEMMAP_USABLE) continue;

        size_t first  = (size_t)(e->base / PAGE_SIZE);
        size_t nframe = (size_t)(e->length / PAGE_SIZE);
        for (size_t f = 0; f < nframe; f++) {
            if (bm_test(first + f)) {
                bm_clear(first + f);
                s_used_frames--;
            }
        }
    }

    /* 5) A bitmap-et magát visszafoglaljuk */
    size_t bm_first = bitmap_phys / PAGE_SIZE;
    size_t bm_count = (s_bitmap_size + PAGE_SIZE - 1) / PAGE_SIZE;
    for (size_t f = 0; f < bm_count; f++) {
        if (!bm_test(bm_first + f)) {
            bm_set(bm_first + f);
            s_used_frames++;
        }
    }

    kprintf("[pmm] bitmap @ phys 0x%lx (virt %p), occupies %lu frames\n",
            bitmap_phys, (void *)s_bitmap, bm_count);
    kprintf("[pmm] free frames       : %lu (%lu MB available)\n",
            s_total_frames - s_used_frames,
            ((s_total_frames - s_used_frames) * PAGE_SIZE) / (1024 * 1024));
}

/* --- Allokáció / felszabadítás ---------------------------------------- */

uintptr_t pmm_alloc_frame(void)
{
    /* Két részben keresünk: a hinttől a végéig, majd 0-tól a hintig. */
    for (size_t i = s_search_hint; i < s_total_frames; i++) {
        if (!bm_test(i)) {
            bm_set(i);
            s_used_frames++;
            s_search_hint = i + 1;
            return (uintptr_t)i * PAGE_SIZE;
        }
    }
    for (size_t i = 0; i < s_search_hint; i++) {
        if (!bm_test(i)) {
            bm_set(i);
            s_used_frames++;
            s_search_hint = i + 1;
            return (uintptr_t)i * PAGE_SIZE;
        }
    }
    return 0;  /* out of memory */
}

uintptr_t pmm_alloc_frames(size_t count)
{
    if (count == 0) return 0;
    if (count == 1) return pmm_alloc_frame();

    /* Egyszerű lineáris run-keresés */
    size_t run_start = 0;
    size_t run_len   = 0;
    for (size_t i = 0; i < s_total_frames; i++) {
        if (!bm_test(i)) {
            if (run_len == 0) run_start = i;
            run_len++;
            if (run_len == count) {
                for (size_t k = 0; k < count; k++) {
                    bm_set(run_start + k);
                }
                s_used_frames += count;
                s_search_hint = run_start + count;
                return (uintptr_t)run_start * PAGE_SIZE;
            }
        } else {
            run_len = 0;
        }
    }
    return 0;
}

void pmm_free_frame(uintptr_t phys)
{
    size_t i = phys / PAGE_SIZE;
    if (i >= s_total_frames) return;
    if (!bm_test(i)) return;  /* double-free védelem */
    bm_clear(i);
    s_used_frames--;
    if (i < s_search_hint) s_search_hint = i;
}

void pmm_free_frames(uintptr_t phys, size_t count)
{
    for (size_t k = 0; k < count; k++) {
        pmm_free_frame(phys + k * PAGE_SIZE);
    }
}

/* --- Statisztika ------------------------------------------------------- */

size_t    pmm_total_frames(void)      { return s_total_frames; }
size_t    pmm_used_frames(void)       { return s_used_frames; }
size_t    pmm_free_frames_count(void) { return s_total_frames - s_used_frames; }
uintptr_t hhdm_offset(void)           { return s_hhdm_offset; }
