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
            
            uint64_t seg_end = vaddr + memsz;
            if (seg_end > highest_addr) highest_addr = seg_end;
            
            /* Alap igazítás 4KB határokra */
            uint64_t start_page = vaddr & ~0xFFF;
            uint64_t end_page = (vaddr + memsz + 0xFFF) & ~0xFFF;
            
            /* Fizikai lapok foglalása és felmappelése (PAGE_USER | PAGE_WRITABLE) */
            uintptr_t current_pml4 = task_current() ? task_current()->cr3 : vmm_kernel_pml4_phys();
            for (uint64_t page = start_page; page < end_page; page += 0x1000) {
                uintptr_t phys = pmm_alloc_frame();
                vmm_map_page_pml4(current_pml4, page, phys, PAGE_PRESENT | PAGE_WRITABLE | PAGE_USER);
                /* Töröljük a lap tartalmát (.bss szekció miatt is fontos) */
                kmemset((void *)phys_to_virt(phys), 0, 0x1000); // HHDM-en keresztül írjuk, biztonságosabb
            }
            
            /* Adat beolvasása a memóriába (már fel van mappelva a virtuális cím!) */
            if (filesz > 0) {
                vfs_read(file, phdr->p_offset, filesz, (uint8_t *)vaddr);
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

