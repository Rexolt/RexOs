/* ============================================================================
 *  Rex OS - IRQ dispatcher
 *
 *  PIC remap a Phase 3-ban már kész IDT 32..47 vektorjaira, majd
 *  handler regisztrációs API a driverek számára.
 * ========================================================================== */

#include <arch/x86_64/irq.h>
#include <arch/x86_64/idt.h>
#include <arch/x86_64/gdt.h>
#include <arch/x86_64/pic.h>
#include <lib/printf.h>

static irq_handler_t s_handlers[16] = { NULL };

void irq_init(void)
{
    pic_init();

    /* IDT 32..47 bekötése az asm stubokra. */
    for (int i = 0; i < 16; i++) {
        idt_set_entry(32 + i, irq_stub_table[i], GDT_KERNEL_CS, IDT_INTR_GATE);
    }
}

void irq_register(uint8_t irq, irq_handler_t handler)
{
    if (irq >= 16) return;
    s_handlers[irq] = handler;
    pic_unmask(irq);
}

void irq_unregister(uint8_t irq)
{
    if (irq >= 16) return;
    pic_mask(irq);
    s_handlers[irq] = NULL;
}

void irq_common_c_handler(struct interrupt_frame *frame)
{
    uint64_t vec = frame->vector;

    /* Csak a hardware IRQ vektorokat (32..47) routoljuk a handlerekhez +
     * küldünk EOI-t. A 0xFE software yield vector itt NO-OP - csak az
     * irq_common_stub epilógusa fogja végrehajtani a switch-et. */
    if (vec >= 32 && vec <= 47) {
        uint8_t irq = (uint8_t)(vec - 32);

        if (s_handlers[irq]) {
            s_handlers[irq](irq, frame);
        }
        /* Spurious IRQ-knál (pl. IRQ7/IRQ15 handler nélkül) is küldjünk EOI-t,
         * különben blokkolódna a következő. */
        pic_eoi(irq);
    }
    /* Egyéb vektorok (pl. 0xFE yield): nincs teendő, az epilógus oldja meg. */
}
