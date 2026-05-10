/* Rex OS - Virtual Memory Manager (x86_64 4-level paging) */
#pragma once
#include <rexos/types.h>

/* PTE flag bitek (közös PML4/PDPT/PD/PT-re) */
#define PAGE_PRESENT   (1ULL << 0)
#define PAGE_WRITABLE  (1ULL << 1)
#define PAGE_USER      (1ULL << 2)
#define PAGE_PWT       (1ULL << 3)
#define PAGE_PCD       (1ULL << 4)
#define PAGE_ACCESSED  (1ULL << 5)
#define PAGE_DIRTY     (1ULL << 6)
#define PAGE_HUGE      (1ULL << 7)   /* PD szinten: 2 MB; PDPT szinten: 1 GB */
#define PAGE_GLOBAL    (1ULL << 8)
#define PAGE_NX        (1ULL << 63)  /* csak ha EFER.NXE = 1 */

void vmm_init(void);

/* Egy 4 KB-os lap mappelése.
 * @param virt   virtuális cím (4 KB-ra igazítva)
 * @param phys   fizikai cím (4 KB-ra igazítva)
 * @param flags  PAGE_* bitek (PAGE_PRESENT mindig kell)
 * @return       true ha sikerült, false ha kifogytunk a fizikai memóriából. */
bool vmm_map_page(uintptr_t virt, uintptr_t phys, uint64_t flags);

/* Lap mappelése egy specifikus PML4 táblába. */
bool vmm_map_page_pml4(uintptr_t pml4_phys, uintptr_t virt, uintptr_t phys, uint64_t flags);

/* Egy lap felmentése a mappolás alól. */
bool vmm_unmap_page(uintptr_t virt);

/* Virt → phys címfordítás. 0 ha nincs mappolva.
 * (Megjegyzés: ha a fizikai cím történetesen 0, ezt nem különböztetjük meg
 *  unmapped-től; nem szokott gond lenni mert 0 oldalt sosem mappolunk.) */
uintptr_t vmm_translate(uintptr_t virt);

/* Egyetlen TLB bejegyzés érvénytelenítése. */
void vmm_flush_tlb(uintptr_t virt);

/* A kernel PML4-ének fizikai címe (CR3-ba töltöttük). */
uintptr_t vmm_kernel_pml4_phys(void);

/* Létrehoz egy új PML4 táblát (processzek számára), 
 * a kernel magas-memóriás bejegyzéseivel inicializálva. */
uintptr_t vmm_create_user_pml4(void);

/* Töröl egy felhasználói PML4 táblát, és felszabadítja a teljes User Space
 * alatti memóriakereteket és lapkönyvtárakat. */
void vmm_destroy_user_pml4(uintptr_t pml4_phys);
