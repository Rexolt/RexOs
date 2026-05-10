/* ============================================================================
 *  Rex OS - Virtual Memory Manager (x86_64 4-level paging)
 *
 *  Stratégia init-kor:
 *    1) Új PML4 allokálása a PMM-ből
 *    2) HHDM mapping építése 2 MB huge page-ekkel: 0..4 GB elérhető lesz
 *       a 0xffff8000_00000000 + phys címen
 *    3) Kernel ELF mappingje 4 KB lapokkal: virtual_base -> physical_base
 *    4) CR3 átkapcsolása az új PML4 fizikai címére
 *    5) Innentől minden további map_page hívás a saját tábláinkat módosítja
 * ========================================================================== */

#include <mm/vmm.h>
#include <mm/pmm.h>
#include <lib/string.h>
#include <lib/printf.h>
#include <lib/panic.h>
#include <limine.h>

/* --- Limine: kell a kernel fizikai/virtuális helye --------------------- */

__attribute__((used, section(".limine_requests")))
static volatile struct limine_kernel_address_request kaddr_request = {
    .id       = LIMINE_KERNEL_ADDRESS_REQUEST,
    .revision = 0,
    .response = NULL,
};

/* A linker.ld definiálja, a .bss vége utáni cím (page-aligned). */
extern char __kernel_end[];

/* --- Index makrók ------------------------------------------------------ */

#define PML4_INDEX(v) (((v) >> 39) & 0x1FFULL)
#define PDPT_INDEX(v) (((v) >> 30) & 0x1FFULL)
#define PD_INDEX(v)   (((v) >> 21) & 0x1FFULL)
#define PT_INDEX(v)   (((v) >> 12) & 0x1FFULL)

#define ADDR_MASK_4K  0x000FFFFFFFFFF000ULL  /* maszkolja a phys címet 4K-ra */
#define ADDR_MASK_2M  0x000FFFFFFFE00000ULL  /* maszkolja a phys címet 2M-ra */
#define PAGE_OFF_4K   0xFFFULL
#define PAGE_OFF_2M   0x1FFFFFULL

/* --- Internal state ---------------------------------------------------- */

static uintptr_t s_kernel_pml4_phys = 0;

/* --- Segédek ----------------------------------------------------------- */

static uint64_t *table_phys_to_virt(uint64_t entry)
{
    return (uint64_t *)phys_to_virt(entry & ADDR_MASK_4K);
}

/* Megkeresi vagy létrehozza a következő szintű táblát. */
static uint64_t *get_or_create_table(uint64_t *parent, size_t idx,
                                     uint64_t intermediate_flags)
{
    if (!(parent[idx] & PAGE_PRESENT)) {
        uintptr_t new_phys = pmm_alloc_frame();
        if (new_phys == 0) return NULL;
        kmemset(phys_to_virt(new_phys), 0, PAGE_SIZE);
        parent[idx] = new_phys | intermediate_flags;
    }
    /* Ha valaki korábban 2 MB huge page-et tett ide, az most összevész
     * egy 4K-os mappinggal. Mi most ezt nem támogatjuk; panic. */
    if (parent[idx] & PAGE_HUGE) {
        kpanic("vmm: cannot split a 2MB huge page into 4K mappings (idx=%lu)", idx);
    }
    return table_phys_to_virt(parent[idx]);
}

/* --- Public API -------------------------------------------------------- */

bool vmm_map_page_pml4(uintptr_t pml4_phys, uintptr_t virt, uintptr_t phys, uint64_t flags)
{
    uint64_t *pml4 = (uint64_t *)phys_to_virt(pml4_phys);

    /* Minden belső táblának USER jogot kell adni, hogy a Ring 3
     * elérhesse a végső PT-ben lévő USER lapokat. A Kernel lapokat a PT védi. */
    const uint64_t inter = PAGE_PRESENT | PAGE_WRITABLE | PAGE_USER;

    uint64_t *pdpt = get_or_create_table(pml4, PML4_INDEX(virt), inter);
    if (!pdpt) return false;
    uint64_t *pd   = get_or_create_table(pdpt, PDPT_INDEX(virt), inter);
    if (!pd)   return false;
    uint64_t *pt   = get_or_create_table(pd,   PD_INDEX(virt),   inter);
    if (!pt)   return false;

    pt[PT_INDEX(virt)] = (phys & ADDR_MASK_4K) | flags;
    
    if (pml4_phys == s_kernel_pml4_phys) {
        vmm_flush_tlb(virt);
    }
    return true;
}

bool vmm_map_page(uintptr_t virt, uintptr_t phys, uint64_t flags)
{
    return vmm_map_page_pml4(s_kernel_pml4_phys, virt, phys, flags);
}

/* 2 MB huge page mapper — csak az init használja, a HHDM-hez. */
static bool vmm_map_huge_2mb(uintptr_t virt, uintptr_t phys, uint64_t flags)
{
    uint64_t *pml4 = (uint64_t *)phys_to_virt(s_kernel_pml4_phys);
    const uint64_t inter = PAGE_PRESENT | PAGE_WRITABLE;

    uint64_t *pdpt = get_or_create_table(pml4, PML4_INDEX(virt), inter);
    if (!pdpt) return false;
    uint64_t *pd   = get_or_create_table(pdpt, PDPT_INDEX(virt), inter);
    if (!pd)   return false;

    pd[PD_INDEX(virt)] = (phys & ADDR_MASK_2M) | flags | PAGE_HUGE;
    return true;
}

bool vmm_unmap_page(uintptr_t virt)
{
    uint64_t *pml4 = (uint64_t *)phys_to_virt(s_kernel_pml4_phys);
    if (!(pml4[PML4_INDEX(virt)] & PAGE_PRESENT)) return false;

    uint64_t *pdpt = table_phys_to_virt(pml4[PML4_INDEX(virt)]);
    if (!(pdpt[PDPT_INDEX(virt)] & PAGE_PRESENT)) return false;

    uint64_t *pd = table_phys_to_virt(pdpt[PDPT_INDEX(virt)]);
    if (!(pd[PD_INDEX(virt)] & PAGE_PRESENT)) return false;

    if (pd[PD_INDEX(virt)] & PAGE_HUGE) {
        pd[PD_INDEX(virt)] = 0;
    } else {
        uint64_t *pt = table_phys_to_virt(pd[PD_INDEX(virt)]);
        pt[PT_INDEX(virt)] = 0;
    }
    vmm_flush_tlb(virt);
    return true;
}

uintptr_t vmm_translate(uintptr_t virt)
{
    uint64_t *pml4 = (uint64_t *)phys_to_virt(s_kernel_pml4_phys);
    if (!(pml4[PML4_INDEX(virt)] & PAGE_PRESENT)) return 0;

    uint64_t *pdpt = table_phys_to_virt(pml4[PML4_INDEX(virt)]);
    if (!(pdpt[PDPT_INDEX(virt)] & PAGE_PRESENT)) return 0;

    uint64_t *pd = table_phys_to_virt(pdpt[PDPT_INDEX(virt)]);
    if (!(pd[PD_INDEX(virt)] & PAGE_PRESENT)) return 0;

    if (pd[PD_INDEX(virt)] & PAGE_HUGE) {
        return (pd[PD_INDEX(virt)] & ADDR_MASK_2M) | (virt & PAGE_OFF_2M);
    }

    uint64_t *pt = table_phys_to_virt(pd[PD_INDEX(virt)]);
    if (!(pt[PT_INDEX(virt)] & PAGE_PRESENT)) return 0;
    return (pt[PT_INDEX(virt)] & ADDR_MASK_4K) | (virt & PAGE_OFF_4K);
}

void vmm_flush_tlb(uintptr_t virt)
{
    __asm__ volatile ("invlpg (%0)" : : "r"(virt) : "memory");
}

uintptr_t vmm_kernel_pml4_phys(void)
{
    return s_kernel_pml4_phys;
}

uintptr_t vmm_create_user_pml4(void)
{
    uintptr_t phys = pmm_alloc_frame();
    if (!phys) return 0;
    
    uint64_t *new_pml4 = (uint64_t *)phys_to_virt(phys);
    uint64_t *kernel_pml4 = (uint64_t *)phys_to_virt(s_kernel_pml4_phys);
    
    /* Ürítjük az alsó felét (User Space: 0 - 255. bejegyzés) */
    for (int i = 0; i < 256; i++) {
        new_pml4[i] = 0;
    }
    
    /* Bemásoljuk a felső felét (Kernel Space: 256 - 511. bejegyzés) a Kernel PML4-ből */
    for (int i = 256; i < 512; i++) {
        new_pml4[i] = kernel_pml4[i];
    }
    
    return phys;
}

void vmm_destroy_user_pml4(uintptr_t pml4_phys)
{
    if (pml4_phys == s_kernel_pml4_phys || pml4_phys == 0) return;
    
    uint64_t *pml4 = (uint64_t *)phys_to_virt(pml4_phys);
    
    /* Csak az alsó felét (User Space) szabadítjuk fel */
    for (int i = 0; i < 256; i++) {
        if (pml4[i] & PAGE_PRESENT) {
            uint64_t *pdpt = (uint64_t *)phys_to_virt(pml4[i] & ADDR_MASK_4K);
            for (int j = 0; j < 512; j++) {
                if (pdpt[j] & PAGE_PRESENT) {
                    if (pdpt[j] & PAGE_HUGE) continue; /* Hiba lenne, de átugorjuk */
                    uint64_t *pd = (uint64_t *)phys_to_virt(pdpt[j] & ADDR_MASK_4K);
                    for (int k = 0; k < 512; k++) {
                        if (pd[k] & PAGE_PRESENT) {
                            if (pd[k] & PAGE_HUGE) continue;
                            uint64_t *pt = (uint64_t *)phys_to_virt(pd[k] & ADDR_MASK_4K);
                            for (int l = 0; l < 512; l++) {
                                if (pt[l] & PAGE_PRESENT) {
                                    pmm_free_frame(pt[l] & ADDR_MASK_4K);
                                }
                            }
                            pmm_free_frame(pd[k] & ADDR_MASK_4K);
                        }
                    }
                    pmm_free_frame(pdpt[j] & ADDR_MASK_4K);
                }
            }
            pmm_free_frame(pml4[i] & ADDR_MASK_4K);
        }
    }
    
    pmm_free_frame(pml4_phys);
}

/* --- Init -------------------------------------------------------------- */

void vmm_init(void)
{
    if (!kaddr_request.response) {
        kpanic("vmm: Limine kernel_address response is NULL");
    }

    uint64_t kphys = kaddr_request.response->physical_base;
    uint64_t kvirt = kaddr_request.response->virtual_base;
    uint64_t kend  = (uint64_t)__kernel_end;
    uint64_t ksize = kend - kvirt;

    kprintf("[vmm] kernel virt base : 0x%lx\n", kvirt);
    kprintf("[vmm] kernel virt end  : 0x%lx\n", kend);
    kprintf("[vmm] kernel phys base : 0x%lx\n", kphys);
    kprintf("[vmm] kernel size      : %lu KB\n", ksize / 1024);

    /* 1) PML4 */
    s_kernel_pml4_phys = pmm_alloc_frame();
    if (s_kernel_pml4_phys == 0) kpanic("vmm: cannot allocate PML4");
    kmemset(phys_to_virt(s_kernel_pml4_phys), 0, PAGE_SIZE);
    kprintf("[vmm] new PML4 @ phys 0x%lx\n", s_kernel_pml4_phys);

    /* 2) HHDM: 0..4 GB fizikai mappingje 2 MB lapokkal */
    const uint64_t HHDM_TOP = 4ULL * 1024 * 1024 * 1024;  /* 4 GB */
    const uint64_t hhdm     = hhdm_offset();
    kprintf("[vmm] mapping HHDM: virt 0x%lx -> phys 0x0 .. 0x%lx (2MB pages)\n",
            hhdm, HHDM_TOP);
    for (uint64_t p = 0; p < HHDM_TOP; p += 2 * 1024 * 1024) {
        if (!vmm_map_huge_2mb(hhdm + p, p, PAGE_PRESENT | PAGE_WRITABLE)) {
            kpanic("vmm: HHDM map failed at phys 0x%lx", p);
        }
    }

    /* 3) Kernel ELF: 4 KB lapokkal a teljes range */
    uint64_t kpages = (ksize + PAGE_SIZE - 1) / PAGE_SIZE;
    kprintf("[vmm] mapping kernel : virt 0x%lx -> phys 0x%lx, %lu pages\n",
            kvirt, kphys, kpages);
    for (uint64_t i = 0; i < kpages; i++) {
        if (!vmm_map_page(kvirt + i * PAGE_SIZE,
                          kphys + i * PAGE_SIZE,
                          PAGE_PRESENT | PAGE_WRITABLE)) {
            kpanic("vmm: kernel map failed at virt 0x%lx", kvirt + i * PAGE_SIZE);
        }
    }

    /* 4) CR3 átkapcsolása */
    kprintf("[vmm] loading CR3 = 0x%lx ...\n", s_kernel_pml4_phys);
    __asm__ volatile ("mov %0, %%cr3" : : "r"(s_kernel_pml4_phys) : "memory");
    kprintf("[vmm] CR3 loaded - own page tables ACTIVE.\n");
}
