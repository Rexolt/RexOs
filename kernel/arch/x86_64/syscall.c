#include <arch/x86_64/syscall.h>
#include <arch/x86_64/gdt.h>
#include <lib/printf.h>
#include <sched/sched.h>
#include <drivers/keyboard/keyboard.h>
#include <drivers/framebuffer/fb.h>
#include <mm/vmm.h>
#include <mm/pmm.h>
#include <rexos/fs.h>
#include <lib/string.h>
#include <drivers/mouse/mouse.h>

#define MSR_EFER    0xC0000080
#define MSR_STAR    0xC0000081
#define MSR_LSTAR   0xC0000082
#define MSR_FMASK   0xC0000084

extern void syscall_entry(void);

static inline void wrmsr(uint32_t msr, uint64_t val) {
    uint32_t low = val & 0xFFFFFFFF;
    uint32_t high = val >> 32;
    __asm__ volatile("wrmsr" : : "c"(msr), "a"(low), "d"(high));
}

static inline uint64_t rdmsr(uint32_t msr) {
    uint32_t low, high;
    __asm__ volatile("rdmsr" : "=a"(low), "=d"(high) : "c"(msr));
    return ((uint64_t)high << 32) | low;
}

void syscall_init(void) {
    /* 1. System Call Enable (SCE) bekapcsolása az EFER MSR-ben */
    uint64_t efer = rdmsr(MSR_EFER);
    wrmsr(MSR_EFER, efer | 1);
    
    /* 2. STAR MSR beállítása:
     * Bit 32-47: Kernel CS (a sys_call ide fog ugrani). Az SS automatikusan Kernel CS + 8 lesz.
     * Bit 48-63: User Base (a sysretq ide fog ugrani).
     *            sysretq a User CS-t a (Base + 16)-ból veszi, az SS-t pedig a (Base + 8)-ból!
     *            GDT-nk: Index 3 = User DS (0x1B), Index 4 = User CS (0x23).
     *            Így ha Base = 0x10, Base + 8 = 0x18 (User DS), Base + 16 = 0x20 (User CS).
     */
    uint64_t star = ((uint64_t)0x08 << 32) | ((uint64_t)0x10 << 48);
    wrmsr(MSR_STAR, star);
    
    /* 3. LSTAR MSR: A syscall belépési pontja */
    wrmsr(MSR_LSTAR, (uint64_t)syscall_entry);
    
    /* 4. FMASK MSR: A syscall alatt törlendő flag-ek.
     * 0x202 = RFLAGS.IF (Interrupts Disable) és TF (Trap Flag) törlése.
     * Ez azért kell, hogy syscall alatt ne kapjunk hardware interruptot, amíg nem váltunk kernel stackre! */
    wrmsr(MSR_FMASK, 0x202);
    
    kprintf("[syscall] System call interface (syscall/sysret) initialized.\n");
}

/* C szintű megszakításkezelő a syscallhoz. Az assemblyből hívjuk. */
uint64_t syscall_handler(uint64_t nr, uint64_t arg1, uint64_t arg2, uint64_t arg3, uint64_t arg4, uint64_t arg5) {
    (void)arg4; (void)arg5;
    
    /* Syscall tábla szimulálása if-else szerkezettel */
    if (nr == 0) { // sys_write (stdout)
        // RDI = arg1 (fd), RSI = arg2 (str), RDX = arg3 (len)
        if (arg1 == 1 || arg1 == 2) {
            kprintf("%s", (const char *)arg2);
            return arg3;
        }
    } else if (nr == 1) { // sys_exit
        kprintf("\n[syscall] User task '%s' exited with code %lu.\n", task_current()->name, arg1);
        task_exit(); // Ez kilép és meghívja a schedulert
        return 0;
    } else if (nr == 2) { // sys_yield
        task_yield();
        return 0;
    } else if (nr == 3) { // sys_read
        if (arg1 == 0) { // stdin
            char *buf = (char *)arg2;
            uint64_t count = arg3;
            uint64_t read = 0;
            while (read < count) {
                if (keyboard_has_data()) {
                    buf[read++] = keyboard_getc();
                } else {
                    task_yield();
                }
            }
            return read;
        } else if (arg1 >= 3 && arg1 < 16) {
            task_t *t = task_current();
            if (t->fd_table[arg1]) {
                uint64_t bytes = vfs_read(t->fd_table[arg1], t->fd_offset[arg1], arg3, (uint8_t *)arg2);
                t->fd_offset[arg1] += bytes;
                return bytes;
            }
        }
        return 0;
    } else if (nr == 4) { // sys_get_fb
        uint64_t *width = (uint64_t *)arg1;
        uint64_t *height = (uint64_t *)arg2;
        uint64_t *pitch = (uint64_t *)arg3;
        
        *width = fb_width();
        *height = fb_height();
        *pitch = fb_pitch();
        
        uintptr_t fb_phys = vmm_translate((uintptr_t)fb_address());
        uint64_t fb_size = fb_pitch() * fb_height();
        uint64_t fb_vaddr = 0xA0000000;
        
        for (uint64_t offset = 0; offset < fb_size; offset += 0x1000) {
            vmm_map_page_pml4(task_current()->cr3, fb_vaddr + offset, fb_phys + offset, PAGE_PRESENT | PAGE_WRITABLE | PAGE_USER);
        }
        
        return fb_vaddr;
    } else if (nr == 5) { // sys_spawn
        const char *path = (const char *)arg1;
        vfs_node_t *node = vfs_finddir(fs_root, path);
        if (node) {
            task_t *t = task_spawn_user(path, node);
            if (t) return t->id;
        }
        return (uint64_t)-1;
    } else if (nr == 6) { // sys_open
        const char *path = (const char *)arg1;
        vfs_node_t *node;
        if (path[0] == '/' && path[1] == '\0') {
            node = fs_root;  // "/" → gyökér könyvtár
        } else {
            node = vfs_finddir(fs_root, path);
        }
        if (!node) return (uint64_t)-1;
        
        task_t *t = task_current();
        for (int i = 3; i < 16; i++) {
            if (!t->fd_table[i]) {
                t->fd_table[i] = node;
                t->fd_offset[i] = 0;
                return i;
            }
        }
        return (uint64_t)-1;
    } else if (nr == 7) { // sys_getdents
        task_t *t = task_current();
        if (arg1 >= 3 && arg1 < 16 && t->fd_table[arg1]) {
            dirent_t *user_dir = (dirent_t *)arg2;
            dirent_t *d = vfs_readdir(t->fd_table[arg1], t->fd_offset[arg1]);
            if (d) {
                *user_dir = *d;
                t->fd_offset[arg1]++;
                return 1;
            }
        }
        return 0;
    } else if (nr == 8) { // sys_waitpid
        uint64_t pid = arg1;
        while (sched_task_alive(pid)) {
            task_yield();
        }
        return 0;
    } else if (nr == 9) { // sys_brk
        task_t *t = task_current();
        uint64_t new_brk = arg1;
        
        if (new_brk == 0) {
            /* Lekérdezés: visszaadjuk az aktuális brk-t */
            return t->brk_current;
        }
        
        /* Igazítsuk page-re */
        new_brk = (new_brk + 0xFFF) & ~0xFFF;
        
        if (new_brk < t->brk_start) {
            return t->brk_current; /* nem engedjük az ELF alá menni */
        }
        
        /* Növelés: új lapok mappelése */
        uint64_t old_page = (t->brk_current + 0xFFF) & ~0xFFF;
        for (uint64_t page = old_page; page < new_brk; page += 0x1000) {
            uintptr_t phys = pmm_alloc_frame();
            if (!phys) return t->brk_current; /* nincs elég memória */
            vmm_map_page_pml4(t->cr3, page, phys, PAGE_PRESENT | PAGE_WRITABLE | PAGE_USER);
            kmemset((void *)phys_to_virt(phys), 0, 0x1000);
        }
        
        t->brk_current = new_brk;
        return new_brk;
    } else if (nr == 10) { // sys_ticks
        extern uint64_t pit_ticks(void);
        return pit_ticks();
    } else if (nr == 11) { // sys_kbd_poll (non-blocking keyboard read)
        if (keyboard_has_data()) {
            return (uint64_t)keyboard_getc();
        }
        return 0; /* nincs adat */
    } else if (nr == 12) { // sys_mouse
        uint32_t *out = (uint32_t *)arg1;
        mouse_state_t ms;
        mouse_get_state(&ms);
        out[0] = (uint32_t)ms.x;
        out[1] = (uint32_t)ms.y;
        out[2] = (uint32_t)ms.buttons;
        return 0;
    }
    
    kprintf("[syscall] Unknown syscall %lu from task '%s'\n", nr, task_current()->name);
    return (uint64_t)-1;
}
