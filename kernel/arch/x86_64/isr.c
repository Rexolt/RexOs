/* ============================================================================
 *  Rex OS - C-oldali interrupt dispatcher
 *
 *  Az asm stubokból érkező interrupt_frame-et megkapja, eldönti hogy
 *  recoverable-e (pl. #BP), vagy panic-olni kell.
 * ========================================================================== */

#include <arch/x86_64/isr.h>
#include <lib/printf.h>
#include <lib/panic.h>
#include <sched/sched.h>

static const char *const exception_names[32] = {
    "#DE Divide-by-Zero",
    "#DB Debug",
    "NMI",
    "#BP Breakpoint",
    "#OF Overflow",
    "#BR Bound Range Exceeded",
    "#UD Invalid Opcode",
    "#NM Device Not Available",
    "#DF Double Fault",
    "Coprocessor Segment Overrun",
    "#TS Invalid TSS",
    "#NP Segment Not Present",
    "#SS Stack-Segment Fault",
    "#GP General Protection",
    "#PF Page Fault",
    "Reserved (15)",
    "#MF x87 Floating-Point",
    "#AC Alignment Check",
    "#MC Machine Check",
    "#XM SIMD Floating-Point",
    "#VE Virtualization",
    "#CP Control Protection",
    "Reserved (22)", "Reserved (23)",
    "Reserved (24)", "Reserved (25)",
    "Reserved (26)", "Reserved (27)",
    "#HV Hypervisor Injection",
    "#VC VMM Communication",
    "#SX Security",
    "Reserved (31)",
};

static void dump_frame(const struct interrupt_frame *f)
{
    kprintf("  vector : %lu  (%s)\n",
            f->vector,
            f->vector < 32 ? exception_names[f->vector] : "Unknown");
    kprintf("  err    : 0x%lx\n", f->error_code);
    kprintf("  rip    : 0x%lx    cs : 0x%lx    rflags: 0x%lx\n",
            f->rip, f->cs, f->rflags);
    kprintf("  rsp    : 0x%lx    ss : 0x%lx\n", f->rsp, f->ss);
    kprintf("  rax=0x%lx  rbx=0x%lx  rcx=0x%lx  rdx=0x%lx\n",
            f->rax, f->rbx, f->rcx, f->rdx);
    kprintf("  rsi=0x%lx  rdi=0x%lx  rbp=0x%lx\n",
            f->rsi, f->rdi, f->rbp);
    kprintf("  r8 =0x%lx  r9 =0x%lx  r10=0x%lx  r11=0x%lx\n",
            f->r8, f->r9, f->r10, f->r11);
    kprintf("  r12=0x%lx  r13=0x%lx  r14=0x%lx  r15=0x%lx\n",
            f->r12, f->r13, f->r14, f->r15);

    /* #PF: a hibázó lineáris cím a CR2-ben van. Enélkül a page fault dump
     * gyakorlatilag használhatatlan, mert nem derül ki MIT címzett a kód. */
    if (f->vector == 14) {
        uint64_t cr2;
        __asm__ volatile ("mov %%cr2, %0" : "=r"(cr2));
        kprintf("  cr2    : 0x%lx  (%s %s %s%s)\n",
                cr2,
                (f->error_code & 0x1) ? "protection-violation" : "not-present",
                (f->error_code & 0x2) ? "write" : "read",
                (f->error_code & 0x4) ? "user" : "kernel",
                (f->error_code & 0x10) ? " instruction-fetch" : "");
    }
}

void isr_common_c_handler(struct interrupt_frame *frame)
{
    /* #BP Breakpoint: nem fatális, csak naplózzuk és visszatérünk. */
    if (frame->vector == 3) {
        kprintf("[isr] #BP breakpoint at rip=0x%lx (resuming)\n", frame->rip);
        return;
    }

    kprintf("\n--- CPU EXCEPTION CAUGHT ---\n");
    dump_frame(frame);

    /* Ring 3-ból jött a hiba? Akkor egy user processz hibázott, nem a kernel.
     * Ilyenkor NEM állítjuk meg a gépet: csak az adott taszkot lőjük ki, a
     * desktop és a többi processz fut tovább (a waitpid-ben blokkoló szülő
     * visszakapja a vezérlést, mert a taszk DEAD lesz). */
    if ((frame->cs & 3) == 3) {
        task_t *t = task_current();
        kprintf("[isr] killing faulting user task '%s' (pid %lu)\n",
                t ? t->name : "?", t ? t->id : 0);
        task_exit();   /* nem tér vissza */
    }

    /* Kernel módú hiba: itt tényleg nincs mit tenni. */
    kpanic("unhandled CPU exception #%lu (%s)",
           frame->vector,
           frame->vector < 32 ? exception_names[frame->vector] : "Unknown");
}
