/* ============================================================================
 *  Rex OS - Preemptive scheduler (Phase 9)
 *
 *  EGYSÉGES context-switch mechanizmus az IRQ exit útvonalon keresztül.
 *  Minden task (újonnan létrehozott VAGY már futott) ugyanazon stack-layoutot
 *  használ: full IRQ frame.
 *
 *  Új task induló stackje (felülről lefelé, csökkenő rsp):
 *
 *      stack_top:                          [unused/padding, 16-byte aligned]
 *      stack_top -   8:    task_exit_ptr   <-- safety net ha entry visszatér
 *                                              ezen a címen lesz a "popped rsp"
 *      stack_top -  16:    SS = 0x10
 *      stack_top -  24:    RSP = stack_top - 8 (iretq ide állítja vissza rsp-t)
 *      stack_top -  32:    RFLAGS = 0x202 (IF=1)
 *      stack_top -  40:    CS = 0x08
 *      stack_top -  48:    RIP = task_entry_trampoline
 *      stack_top -  56:    err_code = 0  (dummy)
 *      stack_top -  64:    vector = 0    (dummy)
 *      stack_top -  72:    r15 = 0
 *      stack_top -  80:    r14 = 0
 *      stack_top -  88:    r13 = 0
 *      stack_top -  96:    r12 = 0
 *      stack_top - 104:    r11 = 0
 *      stack_top - 112:    r10 = 0
 *      stack_top - 120:    r9  = 0
 *      stack_top - 128:    r8  = 0
 *      stack_top - 136:    rdi = 0
 *      stack_top - 144:    rsi = 0
 *      stack_top - 152:    rbp = 0
 *      stack_top - 160:    rbx = 0
 *      stack_top - 168:    rdx = 0
 *      stack_top - 176:    rcx = 0
 *      stack_top - 184:    rax = 0       <-- task->rsp ide mutat (IRQ frame teteje)
 *
 *  Switch-eléskor az asm:
 *    1) pop r15..rax  (15 reg)
 *    2) add rsp, 16   (vector + err_code eldobás)
 *    3) iretq         (rip, cs, rflags, rsp, ss visszaállítás)
 *  -> iretq jelez egy "új task indul" eseményt, és a trampoline-ra ugrik.
 * ========================================================================== */

#include <sched/sched.h>
#include <mm/heap.h>
#include <lib/string.h>
#include <lib/printf.h>
#include <lib/panic.h>
#include <rexos/io.h>

#include <arch/x86_64/idt.h>
#include <arch/x86_64/gdt.h>
#include <mm/vmm.h>
#include <mm/pmm.h>
#include <rexos/elf.h>
#include <rexos/fs.h>

/* asm-ből: a 0xFE software interrupt stub */
extern void yield_int_stub(void);

/* --- Globalis flag, az asm epilógus is olvashatja indirekten ----------- */
volatile uint8_t g_need_resched = 0;
uint64_t g_current_kernel_rsp = 0;

/* --- Belső állapot ----------------------------------------------------- */

static task_t  *s_current        = NULL;
static task_t  *s_task_list_head = NULL;   /* körkörös láncolt lista */
static uint64_t s_next_id        = 0;
static task_t  *s_dead_task_list = NULL;

static void reaper_task_entry(void *arg) {
    (void)arg;
    for (;;) {
        while (s_dead_task_list) {
            __asm__ volatile("cli");
            task_t *dead = s_dead_task_list;
            if (dead) s_dead_task_list = dead->next;
            __asm__ volatile("sti");
            
            if (dead) {
                if (dead->cr3 && dead->cr3 != vmm_kernel_pml4_phys()) {
                    vmm_destroy_user_pml4(dead->cr3);
                }
                if (dead->stack_base) {
                    kfree(dead->stack_base);
                }
                kfree(dead);
                kprintf("[sched] Reaper cleaned up dead task resources.\n");
            }
        }
        task_yield();
    }
}

/* --- Helpers ------------------------------------------------------------ */

static void list_insert(task_t *t)
{
    if (!s_task_list_head) {
        s_task_list_head = t;
        t->next = t;
        return;
    }
    t->next = s_task_list_head->next;
    s_task_list_head->next = t;
}

static void list_remove(task_t *t)
{
    if (!s_task_list_head) return;
    task_t *p = s_task_list_head;
    do {
        if (p->next == t) {
            p->next = t->next;
            if (s_task_list_head == t) {
                s_task_list_head = (t->next == t) ? NULL : t->next;
            }
            t->next = NULL;
            return;
        }
        p = p->next;
    } while (p != s_task_list_head);
}

/* Round-robin: a `from`-tól indulva keressük a következő READY task-ot.
 * Visszaadhatja saját magát, ha senki más nem futóképes. */
static task_t *pick_next_ready(task_t *from)
{
    if (!from || !from->next) return from;
    task_t *p = from->next;
    while (p != from) {
        if (p->state == TASK_STATE_READY) return p;
        p = p->next;
    }
    /* Senki más nincs READY-ben; ha mi még futóképesek vagyunk, maradunk. */
    if (from->state == TASK_STATE_READY || from->state == TASK_STATE_RUNNING) {
        return from;
    }
    return NULL;
}

/* --- Entry trampoline --------------------------------------------------- */

static void task_entry_trampoline(void)
{
    /* Iretq utáni állapot: IF=1 (rflags 0x202), Ring 0, kernel CS/SS.
     * sti() nem szükséges - már be van kapcsolva a popped rflags miatt. */
    task_t *self = s_current;
    if (!self) kpanic("trampoline: no current task");
    if (self->entry) self->entry(self->arg);
    task_exit();
}

/* --- Public API --------------------------------------------------------- */

void sched_init(void)
{
    task_t *t0 = (task_t *)kzalloc(sizeof(task_t));
    if (!t0) kpanic("sched_init: kmalloc failed");

    t0->id   = s_next_id++;
    t0->state = TASK_STATE_RUNNING;
    t0->stack_base = (uint8_t *)kmalloc(TASK_STACK_SIZE);
    t0->cr3 = vmm_kernel_pml4_phys();
    t0->rsp = 0;            /* az első IRQ exit fogja kitölteni mentéskor */
    t0->entry = NULL;
    t0->arg = NULL;
    
    /* Beállítjuk a kezdeti RSP0-t, ha kmain átlépne Ring 3-ba context switch előtt! */
    g_current_kernel_rsp = (uint64_t)t0->stack_base + TASK_STACK_SIZE;
    tss_set_rsp0(g_current_kernel_rsp);
    t0->next = NULL;

    const char *nm = "kmain";
    size_t i = 0;
    while (nm[i] && i < TASK_NAME_MAX - 1) { t0->name[i] = nm[i]; i++; }
    t0->name[i] = 0;

    list_insert(t0);
    s_current = t0;

    kprintf("[sched] task[0] '%s' adopted as current\n", t0->name);
    
    /* Reaper taszk indítása a DEAD taszkok takarítására */
    task_create("reaper", reaper_task_entry, NULL);
}

void sched_install_yield_vector(void)
{
    /* 0xFE-ra kötjük a software yield stubot. Interrupt gate -> IF=0 entryskor. */
    idt_set_entry(0xFE, (void *)yield_int_stub, GDT_KERNEL_CS, IDT_INTR_GATE);
    kprintf("[sched] yield vector 0xFE installed\n");
}

task_t *task_create(const char *name, task_entry_fn entry, void *arg)
{
    task_t *t = (task_t *)kzalloc(sizeof(task_t));
    if (!t) return NULL;

    uint8_t *stack = (uint8_t *)kmalloc(TASK_STACK_SIZE);
    if (!stack) { kfree(t); return NULL; }

    t->id = s_next_id++;
    t->state = TASK_STATE_READY;
    t->stack_base = stack;
    t->cr3 = vmm_kernel_pml4_phys();
    t->entry = entry;
    t->arg = arg;

    size_t i = 0;
    if (name) while (name[i] && i < TASK_NAME_MAX - 1) { t->name[i] = name[i]; i++; }
    t->name[i] = 0;

    /* Stack felső széle - 16-byte aligned, majd -8 a Sys-V ABI rsp-nek. */
    uint64_t stack_top = (uint64_t)(stack + TASK_STACK_SIZE);
    stack_top &= ~(uint64_t)0xF;
    uint64_t rsp_user = stack_top - 8;

    /* Safety net: ha az entry visszatér (ne tegye!), a "ret" ezt fogja
     * popolni rip-nek -> task_exit. */
    *(uint64_t *)rsp_user = (uint64_t)task_exit;

    /* Mesterséges IRQ frame felépítése. Felülről lefelé. */
    uint64_t *sp = (uint64_t *)rsp_user;
    *(--sp) = 0x10;                                 /* SS */
    *(--sp) = rsp_user;                             /* RSP (iretq visszaállítja) */
    *(--sp) = 0x202;                                /* RFLAGS (IF=1) */
    *(--sp) = 0x08;                                 /* CS */
    *(--sp) = (uint64_t)task_entry_trampoline;     /* RIP */
    *(--sp) = 0;                                    /* err_code (dummy) */
    *(--sp) = 0;                                    /* vector (dummy) */
    /* 15 GP regiszter, ugyanabban a sorrendben mint az asm popolja:
     * pop r15 a legalacsonyabb címről, pop rax a legmagasabbról.
     * Tehát push (= előrefelé csökkenő rsp) sorrendben: ss..vector után
     * rax legyen a legmagasabb, r15 legyen a legalacsonyabb cím. */
    *(--sp) = 0; /* rax */
    *(--sp) = 0; /* rcx */
    *(--sp) = 0; /* rdx */
    *(--sp) = 0; /* rbx */
    *(--sp) = 0; /* rbp */
    *(--sp) = 0; /* rsi */
    *(--sp) = 0; /* rdi */
    *(--sp) = 0; /* r8  */
    *(--sp) = 0; /* r9  */
    *(--sp) = 0; /* r10 */
    *(--sp) = 0; /* r11 */
    *(--sp) = 0; /* r12 */
    *(--sp) = 0; /* r13 */
    *(--sp) = 0; /* r14 */
    *(--sp) = 0; /* r15 */

    t->rsp = (uint64_t)sp;

    list_insert(t);

    kprintf("[sched] task[%lu] '%s' created (stack 0x%lx..0x%lx, rsp=0x%lx)\n",
            t->id, t->name,
            (uint64_t)stack, (uint64_t)stack + TASK_STACK_SIZE,
            t->rsp);

    return t;
}

extern void jmp_user_mode(uint64_t entry_point, uint64_t user_stack);

static void user_task_launcher(void *arg) {
    struct vfs_node *file = (struct vfs_node *)arg;
    
    uint64_t brk_end = 0;
    uint64_t entry = elf_load_ex(file, &brk_end);
    if (!entry) {
        kprintf("[sched] user_task_launcher: elf_load failed for task '%s'\n", task_current()->name);
        task_exit();
    }
    
    /* Beállítjuk a user heap kezdetét */
    task_current()->brk_start = brk_end;
    task_current()->brk_current = brk_end;
    
    /* Felhasználói stack lefoglalása és mappelése */
    uint64_t user_stack_top = 0x700000000000;
    uint64_t stack_size = 4 * 4096; // 16 KB stack
    for (uint64_t page = user_stack_top - stack_size; page < user_stack_top; page += 4096) {
        uintptr_t phys = pmm_alloc_frame();
        vmm_map_page_pml4(task_current()->cr3, page, phys, PAGE_PRESENT | PAGE_WRITABLE | PAGE_USER);
        kmemset((void *)phys_to_virt(phys), 0, 4096);
    }
    
    kprintf("[sched] Executing User Task '%s' at 0x%lx. Entering Ring 3...\n", task_current()->name, entry);
    
    jmp_user_mode(entry, user_stack_top);
    task_exit(); // Sosem térhet vissza
}

task_t *task_spawn_user(const char *name, struct vfs_node *elf_file) {
    task_t *t = task_create(name, user_task_launcher, elf_file);
    if (!t) return NULL;
    
    /* Felülírjuk a Kernel PML4-et egy dedikált új PML4-gyel */
    t->cr3 = vmm_create_user_pml4();
    
    return t;
}

task_t *task_current(void) { return s_current; }

/* Ellenőrzi, hogy egy adott PID-ű task még létezik-e a futólistán (nem DEAD). */
int sched_task_alive(uint64_t pid)
{
    if (!s_task_list_head) return 0;
    task_t *p = s_task_list_head;
    do {
        if (p->id == pid && p->state != TASK_STATE_DEAD) return 1;
        p = p->next;
    } while (p != s_task_list_head);
    return 0;
}

/* Cooperative yield: software interrupt 0xFE.
 * IF úgyis bekapcsolt, az int utasítás software szinten triggerel. */
void task_yield(void)
{
    g_need_resched = 1;
    __asm__ volatile ("int $0xFE");
}

__noreturn void task_exit(void)
{
    if (!s_current) kpanic("task_exit: no current task");

    task_t *me = s_current;
    me->state = TASK_STATE_DEAD;
    kprintf("[sched] task[%lu] '%s' exited\n", me->id, me->name);

    /* Trigger resched: az IRQ epilógus átnéz egy DEAD task állapotot is és
     * majd a sched_maybe_switch nem mentí el a rsp-jét (lásd lent). */
    g_need_resched = 1;
    __asm__ volatile ("int $0xFE");

    /* Ide elvileg sosem jövünk vissza, de safety net: */
    for (;;) { hlt(); }
}

/* --- A KÖZPONTI Phase 9 ROUTINE -----------------------------------------
 *
 * Az irq_common_stub asm hívja, miután az adott vektor C handler-e lefutott.
 *
 * Visszaadott érték:
 *   0           = nincs switch, az asm folytassa az aktuális task pop+iretq-jával
 *   != 0        = új rsp érték, az asm rakja rsp-be -> egy másik task fog
 *                 visszatérni az iretq-vel
 *
 * A current_rsp paraméter az aktuális task INTERRUPT FRAME tetejére mutat.
 * Ez a frame az asm push-jai után épült fel, és pontosan ezt mentjük el a
 * task struktban switch előtt, hogy később ugyanide visszaállhassunk.
 * --------------------------------------------------------------------- */
uint64_t sched_maybe_switch(uint64_t current_rsp)
{
    if (!g_need_resched) return 0;
    g_need_resched = 0;

    if (!s_current) return 0;

    task_t *prev = s_current;
    task_t *next = pick_next_ready(prev);

    if (!next || next == prev) {
        /* Nincs jobb választás. Ha mi DEAD vagyunk, az nagy baj: */
        if (prev->state == TASK_STATE_DEAD) {
            kpanic("scheduler: current task is DEAD and no other runnable");
        }
        return 0;
    }

    /* Csak akkor mentjük az aktuális rsp-t, ha még él a task (nem DEAD).
     * DEAD tasknál az állapotunk amúgy is eldobható (a stackjén állunk
     * éppen, de azt csak a következő reaper takaríthatja ki később). */
    if (prev->state != TASK_STATE_DEAD) {
        prev->rsp = current_rsp;
        if (prev->state == TASK_STATE_RUNNING) prev->state = TASK_STATE_READY;
    } else {
        /* Kihúzzuk a listából, hogy a következő pick már ne válassza. */
        list_remove(prev);
        prev->next = s_dead_task_list;
        s_dead_task_list = prev;
    }

    next->state = TASK_STATE_RUNNING;
    s_current = next;

    g_current_kernel_rsp = (uint64_t)next->stack_base + TASK_STACK_SIZE;
    tss_set_rsp0(g_current_kernel_rsp);

    /* Ha a következő taszknak más a PML4 táblája, átváltunk rá! */
    if (prev->cr3 != next->cr3) {
        __asm__ volatile("mov %0, %%cr3" : : "r"(next->cr3) : "memory");
    }

    return next->rsp;
}

/* --- Diagnosztika ------------------------------------------------------- */

size_t sched_task_count(void)
{
    if (!s_task_list_head) return 0;
    size_t n = 0;
    task_t *p = s_task_list_head;
    do { n++; p = p->next; } while (p != s_task_list_head);
    return n;
}

void sched_dump(void)
{
    kprintf("[sched] tasks (%lu):\n", (uint64_t)sched_task_count());
    if (!s_task_list_head) return;
    task_t *p = s_task_list_head;
    do {
        const char *st =
            p->state == TASK_STATE_RUNNING ? "RUNNING" :
            p->state == TASK_STATE_READY   ? "READY"   :
            p->state == TASK_STATE_DEAD    ? "DEAD"    : "?";
        kprintf("  [%lu] %-16s %-8s rsp=0x%lx %s\n",
                p->id, p->name, st, p->rsp,
                p == s_current ? "<- current" : "");
        p = p->next;
    } while (p != s_task_list_head);
}
