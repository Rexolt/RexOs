#include <rexos/elf.h>
#include <sched/sched.h>
#include <mm/heap.h>
#include <mm/pmm.h>
#include <mm/vmm.h>
#include <lib/printf.h>
#include <lib/string.h>

/* Külső assembly függvény a Ring 3-ba való ugráshoz */
extern void jmp_user_mode(uint64_t entry_point, uint64_t user_stack);

uint64_t elf_load_ex(vfs_node_t *file, uint64_t *brk_out) {
    if (!file) return 0;
    
    Elf64_Ehdr ehdr;
    if (vfs_read(file, 0, sizeof(Elf64_Ehdr), (uint8_t *)&ehdr) != sizeof(Elf64_Ehdr)) {
        kprintf("[elf] Failed to read ELF header.\n");
        return 0;
    }
    
    if (ehdr.e_ident[0] != 0x7F || ehdr.e_ident[1] != 'E' ||
        ehdr.e_ident[2] != 'L' || ehdr.e_ident[3] != 'F') {
        kprintf("[elf] Not a valid ELF file.\n");
        return 0;
    }
    
    uint64_t ph_size = ehdr.e_phentsize * ehdr.e_phnum;
    uint8_t *ph_buf = kmalloc(ph_size);
    if (!ph_buf) {
        kprintf("[elf] Out of memory for program headers.\n");
        return 0;
    }
    
    vfs_read(file, ehdr.e_phoff, ph_size, ph_buf);
    
    uint64_t highest_addr = 0;
    
    for (int i = 0; i < ehdr.e_phnum; i++) {
        Elf64_Phdr *phdr = (Elf64_Phdr *)(ph_buf + i * ehdr.e_phentsize);
        
        if (phdr->p_type == PT_LOAD) {
            uint64_t vaddr = phdr->p_vaddr;
            uint64_t memsz = phdr->p_memsz;
            uint64_t filesz = phdr->p_filesz;
            uint64_t p_flags = phdr->p_flags;
            
            uint64_t seg_end = vaddr + memsz;
            if (seg_end > highest_addr) highest_addr = seg_end;
            
            /* Alap igazítás 4KB határokra */
            uint64_t start_page = vaddr & ~0xFFF;
            uint64_t end_page = (vaddr + memsz + 0xFFF) & ~0xFFF;
            
            /* Determine page flags from ELF p_flags */
            /* PF_R = 0x4, PF_W = 0x2, PF_X = 0x1 */
            uint64_t flags = PAGE_PRESENT | PAGE_USER | PAGE_WRITABLE;  /* Make all segments writable for now */
            flags &= ~PAGE_NX;  /* Start with NX cleared (executable by default) */
            if (!(p_flags & 0x1)) flags |= PAGE_NX;    /* Set NX if NOT executable */
            
            /* Fizikai lapok foglalása és felmappelése */
            uintptr_t current_pml4 = task_current() ? task_current()->cr3 : vmm_kernel_pml4_phys();
            for (uint64_t page = start_page; page < end_page; page += 0x1000) {
                uintptr_t phys = pmm_alloc_frame();
                vmm_map_page_pml4(current_pml4, page, phys, flags);
                /* Töröljük a lap tartalmát (.bss szekció miatt is fontos) */
                kmemset((void *)phys_to_virt(phys), 0, 0x1000); // HHDM-en keresztül írjuk, biztonságosabb
            }
            
            /* Adat beolvasása HHDM-en keresztül, lap-onként.
             * A közvetlen vaddr írás nem biztonságos: az aktív CR3 nem
             * feltétlenül tartalmazza a user PML4 mappingjait ilyenkor. */
            if (filesz > 0) {
                uint64_t remaining = filesz;
                uint64_t src_off   = phdr->p_offset;
                uint64_t dst_v     = vaddr;

                while (remaining > 0) {
                    uint64_t page_off = dst_v & 0xFFFULL;
                    uint64_t chunk    = 0x1000ULL - page_off;
                    if (chunk > remaining) chunk = remaining;

                    uintptr_t frame = vmm_translate_pml4(current_pml4,
                                                          dst_v & ~0xFFFULL);
                    if (!frame) {
                        kprintf("[elf] translate failed at vaddr 0x%lx\n", dst_v);
                        kfree(ph_buf);
                        return 0;
                    }
                    vfs_read(file, src_off, chunk,
                             (uint8_t *)phys_to_virt(frame) + page_off);

                    dst_v     += chunk;
                    src_off   += chunk;
                    remaining -= chunk;
                }
            }
            
            kprintf("[elf] Loaded segment at 0x%lx (size: %lu bytes)\n", vaddr, memsz);
        }
    }
    kfree(ph_buf);
    
    /* brk: az utolsó szegmens vége, page-re igazítva */
    if (brk_out) {
        *brk_out = (highest_addr + 0xFFF) & ~0xFFF;
    }
    
    return ehdr.e_entry;
}

uint64_t elf_load(vfs_node_t *file) {
    return elf_load_ex(file, NULL);
}

