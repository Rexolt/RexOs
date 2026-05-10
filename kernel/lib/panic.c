/* Rex OS - kernel panic implementation */
#include <lib/panic.h>
#include <lib/printf.h>
#include <rexos/io.h>

__noreturn void kpanic(const char *fmt, ...)
{
    cli();  /* megszakítások le - innen nem akarunk semmilyen mellékhatást */

    kprintf("\n");
    kprintf("###########################################\n");
    kprintf("#           !!! KERNEL PANIC !!!          #\n");
    kprintf("###########################################\n");
    kprintf("  reason: ");

    va_list ap;
    va_start(ap, fmt);
    kvprintf(fmt, ap);
    va_end(ap);

    kprintf("\n");
    kprintf("  system halted.\n");
    kprintf("###########################################\n");

    for (;;) { hlt(); }
}
