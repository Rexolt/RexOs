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
#include <drivers/block/block.h>
#include <drivers/pci/pci.h>
#include <rexos/net.h>
#include <rexos/rtc.h>
#include <rexos/io.h>

extern uint64_t pit_ticks(void);

#define SYS_NET_RECV    23
#define SYS_TIME        24
#define SYS_NET_CLOSE   25

#define PIT_HZ 100
#define NET_CONNECT_TIMEOUT_TICKS (3 * PIT_HZ)

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

/* Felhasználói tér-beli pointer ellenőrzés (alapvető range-validáció).
 * Megakadályozza, hogy user-space kód kernel memóriacímet adjon át syscall-ban.
 * Az x86_64 canonical user space felső határa: 0x00007fffffffffff.
 * FONTOS: lap-szintű validáció (vmm_translate) nincs; ez csak address range ellenőrzés. */
static inline bool uptr_ok(uint64_t ptr, uint64_t len)
{
    if (!ptr) return false;
    if (ptr > 0x00007fffffffffffULL) return false;
    if (len && (ptr + len < ptr || ptr + len > 0x00007fffffffffffULL + 1ULL)) return false;
    return true;
}

void syscall_init(void) {
    /* 1. System Call Enable (SCE) bekapcsolása az EFER MSR-ben */
    uint64_t efer = rdmsr(MSR_EFER);
    wrmsr(MSR_EFER, efer | 1);
    
    /* 2. STAR MSR beállítása:
     * Bit 32-47: Kernel CS (syscall → Ring 0, SS automatikusan CS+8 = 0x10).
     * Bit 48-63: sysretq User-szelektor alap.
     *   A sysretq 64-bit módban: User CS = (Alap+16) | 3, User SS = (Alap+8) | 3.
     *   GDT layout: 0x08=Kernel CS, 0x10=Kernel DS,
     *               0x18=User DS (alap), 0x20=User CS (alap).
     *   Alap=0x10 → Alap+8=0x18 | 3 = 0x1B (User DS), Alap+16=0x20 | 3 = 0x23 (User CS).
     *   A CPU automatikusan beállítja az RPL=3 bitet a sysretq során. */
    uint64_t star = ((uint64_t)0x08 << 32) | ((uint64_t)0x10 << 48);
    wrmsr(MSR_STAR, star);
    
    /* 3. LSTAR MSR: A syscall belépési pontja */
    wrmsr(MSR_LSTAR, (uint64_t)syscall_entry);
    
    /* 4. FMASK MSR: A syscall belépésekor törlendő RFLAGS bitek.
     * Bit 9 (IF)  – interruptok letiltása, amíg kernel stackre nem váltunk.
     * Bit 8 (TF)  – single-step debug letiltása kernel kódban (biztonság).
     * 0x300 = IF (bit 9) + TF (bit 8).
     * (Az eredeti 0x202 csak IF-et tiltotta; bit 1 reserved, maszkolása hatástalan.) */
    wrmsr(MSR_FMASK, 0x300);
    
    kprintf("[syscall] System call interface (syscall/sysret) initialized.\n");
}

/* C szintű megszakításkezelő a syscallhoz. Az assemblyből hívjuk. */
uint64_t syscall_handler(uint64_t nr, uint64_t arg1, uint64_t arg2, uint64_t arg3, uint64_t arg4, uint64_t arg5) {
    (void)arg4; (void)arg5;
    
    /* Syscall tábla szimulálása if-else szerkezettel */
    if (nr == 0) { // sys_write
        // RDI = arg1 (fd), RSI = arg2 (buf), RDX = arg3 (len)
        if (arg1 == 1 || arg1 == 2) {  /* stdout / stderr */
            if (!uptr_ok(arg2, arg3)) return (uint64_t)-1;
            kprintf("%s", (const char *)arg2);
            return arg3;
        } else if (arg1 >= 3 && arg1 < 16) {  /* fájl fd */
            if (!uptr_ok(arg2, arg3)) return (uint64_t)-1;
            task_t *t = task_current();
            if (t->fd_table[arg1]) {
                uint64_t written = vfs_write(t->fd_table[arg1],
                                             t->fd_offset[arg1],
                                             arg3,
                                             (uint8_t *)arg2);
                t->fd_offset[arg1] += written;
                return written;
            }
        }
        return (uint64_t)-1;
    } else if (nr == 1) { // sys_exit
        kprintf("\n[syscall] User task '%s' exited with code %lu.\n", task_current()->name, arg1);
        task_exit(); // Ez kilép és meghívja a schedulert
        return 0;
    } else if (nr == 2) { // sys_yield
        task_yield();
        return 0;
    } else if (nr == 3) { // sys_read
        if (arg1 == 0) { // stdin
            if (!uptr_ok(arg2, arg3)) return (uint64_t)-1;
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
            if (!uptr_ok(arg2, arg3)) return (uint64_t)-1;
            task_t *t = task_current();
            if (t->fd_table[arg1]) {
                uint64_t bytes = vfs_read(t->fd_table[arg1], t->fd_offset[arg1], arg3, (uint8_t *)arg2);
                t->fd_offset[arg1] += bytes;
                return bytes;
            }
        }
        return 0;
    } else if (nr == 4) { // sys_get_fb
        if (!uptr_ok(arg1, sizeof(uint64_t)) ||
            !uptr_ok(arg2, sizeof(uint64_t)) ||
            !uptr_ok(arg3, sizeof(uint64_t))) return (uint64_t)-1;
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
        if (!uptr_ok(arg1, 1)) return (uint64_t)-1;
        const char *path = (const char *)arg1;
        vfs_node_t *node = vfs_lookup(path);
        if (!node) node = vfs_finddir(fs_root, path);
        if (node) {
            task_t *t = task_spawn_user(path, node);
            if (t) return t->id;
        }
        return (uint64_t)-1;
    } else if (nr == 6) { // sys_open(path, flags)
        if (!uptr_ok(arg1, 1)) return (uint64_t)-1;
        const char *path = (const char *)arg1;
        uint64_t oflags = arg2;   /* O_CREAT = 0x40 */
        vfs_node_t *node;
        if (path[0] == '/') {
            node = vfs_lookup(path);
        } else {
            node = vfs_finddir(fs_root, path);
        }
        /* O_CREAT: ha nem létezik, létrehozzuk */
        if (!node && (oflags & 0x40)) {
            vfs_node_t *parent = fs_root;
            const char *fname = path;
            /* Utolsó '/' előtti rész = szülő könyvtár */
            const char *last_slash = NULL;
            for (const char *q = path; *q; q++) if (*q == '/') last_slash = q;
            if (last_slash && last_slash != path) {
                char parent_path[128];
                size_t plen = (size_t)(last_slash - path);
                if (plen < 127) {
                    for (size_t qi = 0; qi < plen; qi++) parent_path[qi] = path[qi];
                    parent_path[plen] = 0;
                    vfs_node_t *pp = vfs_lookup(parent_path);
                    if (pp) parent = pp;
                }
                fname = last_slash + 1;
            } else if (path[0] == '/') {
                fname = path + 1;
            }
            if (*fname) node = vfs_create(parent, fname, 0);
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
        if (!uptr_ok(arg2, sizeof(dirent_t))) return (uint64_t)-1;
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
        return pit_ticks();
    } else if (nr == 11) { // sys_kbd_poll (non-blocking keyboard read)
        if (keyboard_has_data()) {
            return (uint64_t)keyboard_getc();
        }
        return 0; /* nincs adat */
    } else if (nr == 12) { // sys_mouse
        if (!uptr_ok(arg1, 3 * sizeof(uint32_t))) return (uint64_t)-1;
        uint32_t *out = (uint32_t *)arg1;
        mouse_state_t ms;
        mouse_get_state(&ms);
        out[0] = (uint32_t)ms.x;
        out[1] = (uint32_t)ms.y;
        out[2] = (uint32_t)ms.buttons;
        return 0;
    } else if (nr == 13) { // sys_block_count
        return (uint64_t)block_count();
    } else if (nr == 14) {
        /* sys_block_info(idx, out):
         *   out[0..31]    = name (32 byte)
         *   out[32..39]   = sector_count (u64)
         *   out[40..43]   = sector_size  (u32)
         *   out[44]       = writable (u8: 0/1)
         */
        if (!uptr_ok(arg2, 45)) return (uint64_t)-1;
        size_t idx = (size_t)arg1;
        block_device_t *bd = block_at(idx);
        if (!bd) return (uint64_t)-1;
        uint8_t *out = (uint8_t *)arg2;
        for (int i = 0; i < 32; i++) out[i] = (uint8_t)bd->name[i];
        *(uint64_t *)(out + 32) = bd->sector_count;
        *(uint32_t *)(out + 40) = bd->sector_size;
        out[44] = bd->write ? 1 : 0;
        return 0;
    } else if (nr == 15) { // sys_pci_count
        return (uint64_t)pci_device_count();
    } else if (nr == 16) {
        /* sys_pci_info(idx, out):
         *   out[0]  = bus
         *   out[1]  = dev
         *   out[2]  = func
         *   out[3]  = class
         *   out[4]  = subclass
         *   out[5]  = prog_if
         *   out[6..7]   = vendor (u16)
         *   out[8..9]   = device (u16)
         */
        if (!uptr_ok(arg2, 10)) return (uint64_t)-1;
        size_t idx = (size_t)arg1;
        const pci_device_t *p = pci_device_at(idx);
        if (!p) return (uint64_t)-1;
        uint8_t *out = (uint8_t *)arg2;
        out[0] = p->bus; out[1] = p->dev; out[2] = p->func;
        out[3] = p->class_code; out[4] = p->subclass; out[5] = p->prog_if;
        *(uint16_t *)(out + 6) = p->vendor;
        *(uint16_t *)(out + 8) = p->device;
        return 0;
    } else if (nr == 17) { // sys_mkdir(path)
        if (!uptr_ok(arg1, 1)) return (uint64_t)-1;
        const char *path = (const char *)arg1;
        vfs_node_t *parent = fs_root;
        const char *dname = path;
        const char *last_slash = NULL;
        for (const char *q = path; *q; q++) if (*q == '/') last_slash = q;
        if (last_slash && last_slash != path) {
            char parent_path[128];
            size_t plen = (size_t)(last_slash - path);
            if (plen < 127) {
                for (size_t qi = 0; qi < plen; qi++) parent_path[qi] = path[qi];
                parent_path[plen] = 0;
                vfs_node_t *pp = vfs_lookup(parent_path);
                if (pp) parent = pp;
            }
            dname = last_slash + 1;
        } else if (path[0] == '/') {
            dname = path + 1;
        }
        return (*dname && vfs_mkdir_node(parent, dname) == 0) ? 0 : (uint64_t)-1;

    } else if (nr == 18) { // sys_unlink(path)
        if (!uptr_ok(arg1, 1)) return (uint64_t)-1;
        const char *path = (const char *)arg1;
        vfs_node_t *parent = fs_root;
        const char *fname = path;
        const char *last_slash = NULL;
        for (const char *q = path; *q; q++) if (*q == '/') last_slash = q;
        if (last_slash && last_slash != path) {
            char parent_path[128];
            size_t plen = (size_t)(last_slash - path);
            if (plen < 127) {
                for (size_t qi = 0; qi < plen; qi++) parent_path[qi] = path[qi];
                parent_path[plen] = 0;
                vfs_node_t *pp = vfs_lookup(parent_path);
                if (pp) parent = pp;
            }
            fname = last_slash + 1;
        } else if (path[0] == '/') {
            fname = path + 1;
        }
        return (*fname && vfs_unlink(parent, fname) == 0) ? 0 : (uint64_t)-1;

    } else if (nr == 19) { // sys_close(fd)
        int fd = (int)arg1;
        if (fd >= 3 && fd < 16) {
            task_t *t = task_current();
            if (t->fd_table[fd]) {
                vfs_close(t->fd_table[fd]);
                t->fd_table[fd] = NULL;
                t->fd_offset[fd] = 0;
                return 0;
            }
        }
        return (uint64_t)-1;

    } else if (nr == 20) { // sys_seek(fd, offset)
        int fd = (int)arg1;
        if (fd >= 3 && fd < 16) {
            task_t *t = task_current();
            if (t->fd_table[fd]) {
                t->fd_offset[fd] = arg2;
                return arg2;
            }
        }
        return (uint64_t)-1;

    } else if (nr == 21) { /* SYS_NET_CONNECT(hostname_ptr, port) -> socket_id */
        if (!uptr_ok(arg1, 1)) return (uint64_t)-1;
        const char *hostname = (const char *)arg1;
        uint16_t port = (uint16_t)arg2;

        net_device_t *dev = net_get_default_dev();
        if (!dev) return (uint64_t)-1;

        /* Várakozás a DHCP-re (IP != 0.0.0.0) max 5 másodpercig */
        uint64_t dhcp_start = pit_ticks();
        while (dev->ip.ip[0] == 0 && pit_ticks() - dhcp_start < 5000) {
            __asm__ volatile("pause");
        }
        if (dev->ip.ip[0] == 0) {
            kprintf("[net] DHCP timeout, cannot connect.\n");
            return (uint64_t)-1;
        }

        ip4_addr_t dest_ip;
        /* Ha IP literál (kezdő szám), átalakítjuk, egyébként DNS */
        if (hostname[0] >= '0' && hostname[0] <= '9') {
            /* Gyors IPv4 parse: "a.b.c.d" */
            uint8_t a = 0, b = 0, c = 0, d = 0;
            const char *p = hostname;
            while (*p >= '0' && *p <= '9') a = a * 10 + (*p++ - '0');
            if (*p == '.') p++;
            while (*p >= '0' && *p <= '9') b = b * 10 + (*p++ - '0');
            if (*p == '.') p++;
            while (*p >= '0' && *p <= '9') c = c * 10 + (*p++ - '0');
            if (*p == '.') p++;
            while (*p >= '0' && *p <= '9') d = d * 10 + (*p++ - '0');
            dest_ip.ip[0] = a; dest_ip.ip[1] = b;
            dest_ip.ip[2] = c; dest_ip.ip[3] = d;
        } else {
            if (!dns_resolve(dev, hostname, &dest_ip)) return (uint64_t)-1;
        }

        tcp_socket_t *sock = tcp_connect(dev, &dest_ip, port);
        if (!sock) return (uint64_t)-1;

        /* Várakozás valódi óra alapján: max 3 másodperc a 100 Hz-es PIT-tel.
         * Ne hozzunk létre új socketet retry-ként: az eredeti SYN ARP miss esetén
         * pending queue-ba kerül és az ARP válasz után automatikusan kiküldődik. */
        uint64_t start_tick = pit_ticks();
        while (pit_ticks() - start_tick < NET_CONNECT_TIMEOUT_TICKS &&
               sock->state == TCP_SYN_SENT) {
            __asm__ volatile("pause");
        }

        if (sock->state != TCP_ESTABLISHED) {
            tcp_release(sock);
            return (uint64_t)-1;
        }

        /* socket pointer-t uint64-ként adjuk vissza; user csak token-ként kezeli */
        return (uint64_t)(uintptr_t)sock;

    } else if (nr == 22) { /* SYS_NET_SEND(socket_id, buf, len) */
        tcp_socket_t *sock = (tcp_socket_t *)(uintptr_t)arg1;
        if (!tcp_socket_is_valid(sock)) return (uint64_t)-1;
        if (!uptr_ok(arg2, arg3)) return (uint64_t)-1;
        tcp_send_data(sock, (const void *)arg2, (uint32_t)arg3);
        return arg3;

    } else if (nr == 23) { /* SYS_NET_RECV(socket_id, buf, max_len) -> bytes */
        tcp_socket_t *sock = (tcp_socket_t *)(uintptr_t)arg1;
        if (!tcp_socket_is_valid(sock)) return (uint64_t)-1;
        if (!uptr_ok(arg2, arg3)) return (uint64_t)-1;

        /* Várakozás adatra vagy a kapcsolat lezárására. PIT-alapú timeout,
         * így a TLS handshake apró recv-jeinél is determinisztikus a viselkedés. */
        uint64_t recv_start = pit_ticks();
        const uint64_t RECV_TIMEOUT_TICKS = 5 * PIT_HZ; /* ~5 másodperc */
        while (sock->rx_len == 0 &&
               (sock->state == TCP_ESTABLISHED || sock->state == TCP_SYN_SENT) &&
               pit_ticks() - recv_start < RECV_TIMEOUT_TICKS) {
            __asm__ volatile("pause");
        }

        /* Kritikus szakasz: az e1000 IRQ a tcp_receive-en keresztül ugyanezt
         * a rx_buf-ot/rx_len-t írja. Rövid CLI/STI a konzisztens szelet
         * kimásolásához. */
        cli();
        uint32_t avail = sock->rx_len;
        if (avail == 0) {
            sti();
            return 0;
        }
        uint32_t copy = avail;
        if (copy > (uint32_t)arg3) copy = (uint32_t)arg3;
        kmemcpy((void *)arg2, sock->rx_buf, copy);
        uint32_t remaining = avail - copy;
        if (remaining > 0) {
            /* Memmove-szerű shift balra; a forrás és cél átfedhet. */
            for (uint32_t i = 0; i < remaining; i++) {
                sock->rx_buf[i] = sock->rx_buf[i + copy];
            }
        }
        sock->rx_len = remaining;
        sti();
        return copy;

    } else if (nr == 24) { /* SYS_TIME(out_ptr) */
        if (!uptr_ok(arg1, sizeof(rtc_time_t))) return (uint64_t)-1;
        rtc_get_time((rtc_time_t *)arg1);
        return 0;

    } else if (nr == SYS_NET_CLOSE) { /* SYS_NET_CLOSE(socket_id) */
        tcp_socket_t *sock = (tcp_socket_t *)(uintptr_t)arg1;
        if (!tcp_socket_is_valid(sock)) return (uint64_t)-1;
        tcp_release(sock);
        return 0;
    }

    kprintf("[syscall] Unknown syscall %lu from task '%s'\n", nr, task_current()->name);
    return (uint64_t)-1;
}
