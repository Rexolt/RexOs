/* Rex OS - Physical Memory Manager (bitmap-based frame allocator) */
#pragma once
#include <rexos/types.h>

#define PAGE_SIZE 4096

void   pmm_init(void);

/* Egy 4KB-os fizikai keret allokálása.
 * @return  fizikai cím (nem nulla), vagy 0 ha nincs szabad memória. */
uintptr_t pmm_alloc_frame(void);

/* Több egymást követő keret (csak egyszerű lineáris keresés). */
uintptr_t pmm_alloc_frames(size_t count);

/* Egy keret felszabadítása (a phys címen). */
void   pmm_free_frame(uintptr_t phys);

/* Több keret felszabadítása. */
void   pmm_free_frames(uintptr_t phys, size_t count);

/* Statisztika */
size_t pmm_total_frames(void);
size_t pmm_used_frames(void);
size_t pmm_free_frames_count(void);

/* HHDM (Higher-Half Direct Map) offset - bármely fizikai cím
 * elérhető a (phys + hhdm_offset()) virtuális címen. */
uintptr_t hhdm_offset(void);

/* Konvenciós helperek */
static inline void *phys_to_virt(uintptr_t phys)
{
    return (void *)(phys + hhdm_offset());
}

static inline uintptr_t virt_to_phys_hhdm(void *virt)
{
    return (uintptr_t)virt - hhdm_offset();
}
