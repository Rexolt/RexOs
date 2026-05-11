/* ============================================================================
 *  Rex OS - Kernel main entry point
 *  Phase 6: Framebuffer text console + interactive shell
 * ========================================================================== */

#include <rexos/types.h>
#include <rexos/io.h>
#include <limine.h>

#include <arch/x86_64/gdt.h>
#include <arch/x86_64/idt.h>
#include <arch/x86_64/irq.h>

#include <drivers/serial/serial.h>
#include <drivers/framebuffer/fb.h>
#include <drivers/console/console.h>
#include <drivers/timer/pit.h>
#include <drivers/keyboard/keyboard.h>
#include <drivers/mouse/mouse.h>

#include <mm/pmm.h>
#include <mm/vmm.h>
#include <mm/heap.h>

#include <sched/sched.h>
#include <rexos/fs.h>
#include <fs/tarfs.h>
#include <fs/fat32.h>
#include <drivers/ata/ata.h>
#include <drivers/ahci/ahci.h>
#include <drivers/pci/pci.h>
#include <drivers/usb/xhci.h>
#include <drivers/nvme/nvme.h>
#include <rexos/elf.h>
#include <arch/x86_64/syscall.h>

#include <lib/printf.h>
#include <lib/string.h>
#include <lib/panic.h>

/* --- Limine boot protocol requests -------------------------------------- */

__attribute__((used, section(".limine_requests")))
static volatile LIMINE_BASE_REVISION(3)

__attribute__((used, section(".limine_requests")))
static volatile struct limine_module_request module_request = {
    .id = LIMINE_MODULE_REQUEST,
    .revision = 0
};

__attribute__((used, section(".limine_requests_start")))
static volatile LIMINE_REQUESTS_START_MARKER

__attribute__((used, section(".limine_requests_end")))
static volatile LIMINE_REQUESTS_END_MARKER

static __noreturn void hcf(void)
{
    cli();
    for (;;) { hlt(); }
}

/* --- Worker tasks ------------------------------------------------------- */

static void worker_ticker(void *arg)
{
    (void)arg;
    uint64_t last_tick = 0;

    for (;;) {
        uint64_t t = pit_ticks();
        if (t - last_tick >= 500) { /* kb 5 másodperc */
            last_tick = t;
            /* A felhasználó kérésére elnémítjuk a ticker kimenetét, 
               hogy ne zavarja meg a promptot. A háttérben továbbra is fut. */
        }
        task_yield();
    }
}

static void enable_sse(void) {
    uint64_t cr0, cr4;
    __asm__ volatile("mov %%cr0, %0" : "=r"(cr0));
    cr0 &= ~(1 << 2); /* Clear EM (Emulation) */
    cr0 |= (1 << 1);  /* Set MP (Monitor Coprocessor) */
    __asm__ volatile("mov %0, %%cr0" :: "r"(cr0));

    __asm__ volatile("mov %%cr4, %0" : "=r"(cr4));
    cr4 |= (1 << 9);  /* Set OSFXSR (FXSAVE/FXRSTOR support) */
    cr4 |= (1 << 10); /* Set OSXMMEXCPT (SIMD Floating-Point Exception support) */
    __asm__ volatile("mov %0, %%cr4" :: "r"(cr4));
}

/* --- Shell ------------------------------------------------------------- */

#define SHELL_LINE_MAX 128

static int s_next_worker_id = 0;

static void cmd_help(void)
{
    kprintf("Available commands:\n");
    kprintf("  help     - show this help\n");
    kprintf("  uptime   - seconds since boot\n");
    kprintf("  meminfo  - free / total physical memory\n");
    kprintf("  heap     - kernel heap stats\n");
    kprintf("  heapdump - list all heap blocks\n");
    kprintf("  heaptest - run a heap stress self-test\n");
    kprintf("  ps       - list scheduler tasks\n");
    kprintf("  spawn    - create a background ticker task\n");
    kprintf("  yield    - explicit yield (sanity check)\n");
    kprintf("  ticks    - raw PIT tick count\n");
    kprintf("  ls       - list files in the root directory\n");
    kprintf("  cat      - print the contents of a file\n");
    kprintf("  run      - execute an ELF binary from the VFS\n");
    kprintf("  clear    - clear the screen\n");
    kprintf("  banner   - draw the Rex OS banner\n");
    kprintf("  echo ... - echo arguments\n");
    kprintf("  panic    - trigger a kernel panic (test)\n");
    kprintf("  reboot   - triple fault -> reboot\n");
}

static void cmd_spawn(void)
{
    int id = s_next_worker_id++;
    char name[16];
    name[0] = 't'; name[1] = 'i'; name[2] = 'c'; name[3] = 'k';
    name[4] = '-';
    name[5] = '0' + ((id / 10) % 10);
    name[6] = '0' + (id % 10);
    name[7] = 0;
    task_t *t = task_create(name, worker_ticker, (void *)(uintptr_t)id);
    if (!t) kprintf("spawn: failed (OOM?)\n");
    else    kprintf("spawned %s (id=%lu)\n", t->name, t->id);
}

static void cmd_heap(void)
{
    kheap_stats_t st;
    kheap_get_stats(&st);
    kprintf("kernel heap:\n");
    kprintf("  total       : %lu bytes (%lu KB)\n", st.total_bytes, st.total_bytes / 1024);
    kprintf("  used        : %lu bytes (%lu blocks)\n",
            st.used_bytes, st.block_count - st.free_block_count);
    kprintf("  free        : %lu bytes (%lu blocks)\n", st.free_bytes, st.free_block_count);
    kprintf("  largest free: %lu bytes\n", st.largest_free);
}

static void cmd_heaptest(void)
{
    kprintf("[heaptest] starting...\n");

    kheap_stats_t before;
    kheap_get_stats(&before);

    /* 1) Sok kicsi allokáció + free */
    void *p[64];
    for (int i = 0; i < 64; i++) {
        p[i] = kmalloc(64 + i * 4);
        if (!p[i]) { kprintf("[heaptest] FAIL: alloc %d\n", i); return; }
        ((char *)p[i])[0] = (char)i;  /* írhatóság */
    }
    /* Felszabadítjuk minden másodikat (lyukas mintázat) */
    for (int i = 0; i < 64; i += 2) kfree(p[i]);

    /* 2) Most allokálunk egy nagyobbat - kell hogy menjen, mert van összefüggő szabad blokk */
    void *big = kmalloc(8192);
    if (!big) { kprintf("[heaptest] FAIL: big alloc\n"); return; }
    kmemset(big, 0xAB, 8192);

    /* Maradékot is felszabadítjuk */
    for (int i = 1; i < 64; i += 2) kfree(p[i]);
    kfree(big);

    /* 3) krealloc teszt */
    char *s = kmalloc(16);
    if (!s) { kprintf("[heaptest] FAIL: krealloc base\n"); return; }
    for (int i = 0; i < 15; i++) s[i] = 'A' + (i % 26);
    s[15] = 0;
    s = krealloc(s, 64);
    if (!s) { kprintf("[heaptest] FAIL: krealloc grow\n"); return; }
    if (s[0] != 'A' || s[14] != 'A' + 14 % 26) {
        kprintf("[heaptest] FAIL: krealloc data not preserved\n");
        return;
    }
    kfree(s);

    kheap_stats_t after;
    kheap_get_stats(&after);

    kprintf("[heaptest] before: used=%lu free=%lu blocks=%lu\n",
            before.used_bytes, before.free_bytes, before.block_count);
    kprintf("[heaptest] after : used=%lu free=%lu blocks=%lu\n",
            after.used_bytes, after.free_bytes, after.block_count);

    if (after.used_bytes != before.used_bytes) {
        kprintf("[heaptest] FAIL: %lu bytes leaked!\n",
                after.used_bytes - before.used_bytes);
    } else {
        kprintf("[heaptest] PASSED. No leaks, coalescing OK.\n");
    }
}

static void cmd_uptime(void)
{
    uint64_t t = pit_ticks();
    uint64_t s = t / pit_frequency();
    kprintf("up %lu seconds (%lu ticks @ %u Hz)\n", s, t, pit_frequency());
}

static void cmd_meminfo(void)
{
    size_t total = pmm_total_frames();
    size_t free_ = pmm_free_frames_count();
    size_t used  = pmm_used_frames();
    kprintf("total: %lu frames (%lu MB)\n", total, total * 4 / 1024);
    kprintf("used : %lu frames (%lu MB)\n", used,  used  * 4 / 1024);
    kprintf("free : %lu frames (%lu MB)\n", free_, free_ * 4 / 1024);
}

static void cmd_banner(void)
{
    uint32_t w = fb_width();
    uint32_t h = fb_height();
    if (!w) return;
    fb_fill_rect(0, 0, w, 64, FB_BANNER);
    fb_fill_rect(40, 16, 32, 32, FB_RED);
    fb_fill_rect(80, 16,  8, 32, FB_WHITE);
    fb_fill_rect(96, 16, 32, 32, FB_ORANGE);
    for (uint32_t x = 0; x < w; x++) {
        uint32_t r = (x * 255) / (w ? w : 1);
        uint32_t g = 100;
        uint32_t b = 255 - r;
        uint32_t c = (r << 16) | (g << 8) | b;
        fb_fill_rect(x, h - 30, 1, 30, c);
    }
}

static void cmd_ls(void)
{
    if (!fs_root) {
        kprintf("No root filesystem mounted.\n");
        return;
    }
    
    kprintf("Directory listing of /:\n");
    uint32_t i = 0;
    dirent_t *dir = NULL;
    while ((dir = vfs_readdir(fs_root, i++)) != NULL) {
        vfs_node_t *node = vfs_finddir(fs_root, dir->name);
        if (node) {
            kprintf("  %s - %lu bytes\n", dir->name, node->length);
        } else {
            kprintf("  %s\n", dir->name);
        }
    }
}

static void cmd_cat(const char *filename)
{
    if (!fs_root) {
        kprintf("No root filesystem mounted.\n");
        return;
    }
    if (!filename || filename[0] == '\0') {
        kprintf("Usage: cat <filename>\n");
        return;
    }
    
    vfs_node_t *node = vfs_finddir(fs_root, filename);
    if (!node) {
        kprintf("cat: '%s': No such file or directory\n", filename);
        return;
    }
    if ((node->flags & 0x07) == FS_DIRECTORY) {
        kprintf("cat: '%s': Is a directory\n", filename);
        return;
    }
    
    uint8_t *buffer = kmalloc(node->length + 1);
    if (!buffer) {
        kprintf("cat: out of memory\n");
        return;
    }
    
    uint64_t bytes = vfs_read(node, 0, node->length, buffer);
    buffer[bytes] = '\0'; // Ensure string is null terminated just in case
    
    kprintf("%s\n", (char *)buffer);
    
    kfree(buffer);
}

static void cmd_run(const char *filename)
{
    if (!fs_root) {
        kprintf("No root filesystem mounted.\n");
        return;
    }
    if (!filename || filename[0] == '\0') {
        kprintf("Usage: run <filename>\n");
        return;
    }
    
    vfs_node_t *node = vfs_finddir(fs_root, filename);
    if (!node) {
        kprintf("run: '%s': No such file or directory\n", filename);
        return;
    }
    
    task_t *t = task_spawn_user(filename, node);
    if (t) {
        kprintf("Spawned user task '%s' with PID %lu.\n", filename, t->id);
    } else {
        kprintf("Failed to spawn user task.\n");
    }
}

static __noreturn void cmd_reboot(void)
{
    /* Triple fault trükk: betöltünk egy 0 limit-es IDT-t és int $0 -t hívunk.
     * A CPU nem talál érvényes handlert -> #DF -> nem találja megint -> reset. */
    struct { uint16_t limit; uint64_t base; } __packed null_idt = { 0, 0 };
    __asm__ volatile ("lidt %0; int $0" : : "m"(null_idt));
    for (;;) { hlt(); }  /* nem ér ide */
}

static void shell_execute(char *line)
{
    /* Trim leading spaces. */
    while (*line == ' ') line++;
    if (*line == 0) return;

    /* Split command/args egy szóköznél. */
    char *args = line;
    while (*args && *args != ' ') args++;
    if (*args == ' ') { *args = 0; args++; while (*args == ' ') args++; }

    if      (kstrcmp(line, "help")     == 0) cmd_help();
    else if (kstrcmp(line, "uptime")   == 0) cmd_uptime();
    else if (kstrcmp(line, "meminfo")  == 0) cmd_meminfo();
    else if (kstrcmp(line, "heap")     == 0) cmd_heap();
    else if (kstrcmp(line, "heapdump") == 0) kheap_dump();
    else if (kstrcmp(line, "heaptest") == 0) cmd_heaptest();
    else if (kstrcmp(line, "ps")       == 0) sched_dump();
    else if (kstrcmp(line, "spawn")    == 0) cmd_spawn();
    else if (kstrcmp(line, "yield")    == 0) { task_yield(); kprintf("yielded.\n"); }
    else if (kstrcmp(line, "ticks")    == 0) kprintf("%lu\n", pit_ticks());
    else if (kstrcmp(line, "ls")       == 0) cmd_ls();
    else if (kstrcmp(line, "cat")      == 0) cmd_cat(args);
    else if (kstrcmp(line, "run")      == 0) cmd_run(args);
    else if (kstrcmp(line, "clear")    == 0) console_clear();
    else if (kstrcmp(line, "banner")   == 0) cmd_banner();
    else if (kstrcmp(line, "echo")     == 0) kprintf("%s\n", args);
    else if (kstrcmp(line, "panic")    == 0) kpanic("user-requested panic from shell");
    else if (kstrcmp(line, "reboot")   == 0) cmd_reboot();
    else kprintf("unknown command: '%s' (try 'help')\n", line);
}

__attribute__((unused))
static __noreturn void shell_run(void)
{
    char line[SHELL_LINE_MAX];
    size_t len = 0;

    kprintf("\nrex> ");
    for (;;) {
        while (keyboard_has_data()) {
            char c = keyboard_getc();

            if (c == '\n') {
                kprintf("\n");
                line[len] = 0;
                shell_execute(line);
                len = 0;
                kprintf("rex> ");
            } else if (c == '\b') {
                if (len > 0) {
                    len--;
                    kprintf("\b");
                }
            } else if (c >= 32 && c < 127 && len < SHELL_LINE_MAX - 1) {
                line[len++] = c;
                char buf[2] = { c, 0 };
                kprintf("%s", buf);
            }
        }
        /* Cooperative: amíg nincs input, átadjuk a CPU-t a többi task-nak.
         * Phase 11: A preempció miatt ezt is kivehetjük, a CPU magától fog váltani! */
        // task_yield();
    }
}

void kmain(void)
{
    if (!LIMINE_BASE_REVISION_SUPPORTED) hcf();

    serial_init();

    kprintf("\n");
    kprintf("===========================================\n");
    kprintf("  %s v%s\n", KERNEL_NAME, KERNEL_VERSION);
    kprintf("  Phase 18-20: User Mode OS with libc + VFS syscalls.\n");
    kprintf("===========================================\n");

    gdt_init();
    idt_init();
    syscall_init();

    fb_init();
    pmm_init();
    vmm_init();
    kheap_init();

    irq_init();
    pit_init(100);
    keyboard_init();
    mouse_init();
    mouse_set_bounds(fb_width(), fb_height());
    sti();
    
    enable_sse();

    /* Initrd (TARFS) keresése a Limine modulok között */
    if (module_request.response != NULL) {
        kprintf("[vfs] Found %lu modules from bootloader\n", module_request.response->module_count);
        for (uint64_t i = 0; i < module_request.response->module_count; i++) {
            struct limine_file *mod = module_request.response->modules[i];
            kprintf("  Module %lu: path='%s', addr=0x%lx, size=%lu bytes\n",
                    i, mod->path, (uint64_t)mod->address, mod->size);
            
            /* TODO: Később a Lépés 10.3-ban itt fogjuk átadni a TARFS-nek! */
            tarfs_init((uint64_t)mod->address, mod->size);
            break; // Csak egy initrd-t várunk
        }
    } else {
        kprintf("[vfs] No modules found from bootloader!\n");
    }

    /* --- Storage stack: PCI -> AHCI/ATA -> Block -> FAT32 ----------------
     * Először a PCI buszt enumeráljuk. Ezután priorizáljuk:
     *   1. AHCI SATA (modern q35, valódi gépek)
     *   2. Legacy IDE ATA PIO (régi gépek / qemu -M pc)
     * Az első sikeresen regisztrált block device-ot használja a FAT32.
     */
    pci_init();

    /* NVMe prioritás: ha van NVMe kontroller, azt preferáljuk az AHCI/ATA előtt. */
    bool has_nvme = nvme_init();

    if (!has_nvme) {
        if (!ahci_init()) {
            kprintf("[storage] no AHCI; falling back to legacy ATA PIO\n");
            ata_init();
        }
    }

    /* USB stack: xHCI controller + HID devices */
    if (xhci_init()) {
        xhci_enumerate_ports();
    }

    vfs_node_t *fat_root = fat32_init();
    if (fat_root) {
        vfs_mount("/mnt", fat_root);

        /* Önteszt: olvassunk egy fájlt a FAT32-ről, hogy lássuk működik */
        vfs_node_t *test = vfs_lookup("/mnt/README.TXT");
        if (!test) test = vfs_lookup("/mnt/readme.txt");
        if (test && test->length > 0 && test->length < 256) {
            uint8_t tbuf[256];
            uint64_t n = vfs_read(test, 0, test->length, tbuf);
            tbuf[n < 255 ? n : 255] = 0;
            kprintf("[fat32] read self-test (/mnt/README.TXT, %lu bytes): %s\n",
                    n, (const char *)tbuf);
        }
    }

    console_init();

    sched_init();
    sched_install_yield_vector();

    kprintf("============================================================\n");
    kprintf("   Welcome to %s v%s\n", KERNEL_NAME, KERNEL_VERSION);
    kprintf("   Console: %ux%u px, 8x16 font\n", fb_width(), fb_height());
    kprintf("   Preemptive Scheduler + User Mode initialized.\n");
    kprintf("============================================================\n");

    /* Grafikus asztal indítása Ring 3-as processzként.
     * Ha a desktop.elf nem található, fallback shell.elf-re. */
    kprintf("[kmain] Spawning RexOS Desktop...\n");
    vfs_node_t *desktop_node = vfs_finddir(fs_root, "desktop.elf");
    if (desktop_node) {
        task_spawn_user("desktop.elf", desktop_node);
    } else {
        vfs_node_t *shell_node = vfs_finddir(fs_root, "shell.elf");
        if (shell_node) {
            kprintf("[kmain] desktop.elf not found, falling back to shell.elf\n");
            task_spawn_user("shell.elf", shell_node);
        } else {
            kprintf("[kmain] ERROR: no shell or desktop found in initrd!\n");
        }
    }

    /* A kernel fő taszkja (kmain) befejezte a dolgát, nyugovóra térhet.
       Ezzel hivatalosan is átadjuk az irányítást a preemptív schedulernek
       és a User Mode folyamatoknak! */
    kprintf("[kmain] Boot sequence complete. Kernel idle.\n");
    task_exit();
}
