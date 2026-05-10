/* ============================================================================
 *  Rex OS - 8259 PIC driver
 *
 *  Init szekvencia (ICW1..ICW4):
 *    ICW1: command byte, "init in cascade mode"
 *    ICW2: vector offset (master: 0x20, slave: 0x28)
 *    ICW3: cascade pin info (master: slave at IRQ2; slave: cascade id 2)
 *    ICW4: 8086 mode bit
 *  Utána: data port = interrupt mask register (1 = letiltva).
 * ========================================================================== */

#include <arch/x86_64/pic.h>
#include <rexos/io.h>

#define PIC1_CMD   0x20
#define PIC1_DATA  0x21
#define PIC2_CMD   0xA0
#define PIC2_DATA  0xA1

#define ICW1_INIT  0x10
#define ICW1_ICW4  0x01
#define ICW4_8086  0x01
#define PIC_EOI    0x20

void pic_init(void)
{
    /* ICW1 */
    outb(PIC1_CMD, ICW1_INIT | ICW1_ICW4); io_wait();
    outb(PIC2_CMD, ICW1_INIT | ICW1_ICW4); io_wait();

    /* ICW2 - vector offsets */
    outb(PIC1_DATA, PIC1_VECTOR_OFFSET); io_wait();
    outb(PIC2_DATA, PIC2_VECTOR_OFFSET); io_wait();

    /* ICW3 - cascade */
    outb(PIC1_DATA, 0x04); io_wait();  /* slave PIC at IRQ2 (bit 2 = 0x04) */
    outb(PIC2_DATA, 0x02); io_wait();  /* slave cascade identity = 2 */

    /* ICW4 */
    outb(PIC1_DATA, ICW4_8086); io_wait();
    outb(PIC2_DATA, ICW4_8086); io_wait();

    /* Minden IRQ letiltva induláskor; a driverek külön unmask-elik. */
    outb(PIC1_DATA, 0xFF);
    outb(PIC2_DATA, 0xFF);
}

void pic_eoi(uint8_t irq)
{
    if (irq >= 8) outb(PIC2_CMD, PIC_EOI);
    outb(PIC1_CMD, PIC_EOI);
}

void pic_mask(uint8_t irq)
{
    uint16_t port;
    uint8_t  bit = irq;
    if (irq < 8) { port = PIC1_DATA; }
    else         { port = PIC2_DATA; bit -= 8; }
    uint8_t v = inb(port);
    outb(port, (uint8_t)(v | (1u << bit)));
}

void pic_unmask(uint8_t irq)
{
    uint16_t port;
    uint8_t  bit = irq;
    if (irq < 8) { port = PIC1_DATA; }
    else         { port = PIC2_DATA; bit -= 8; }
    uint8_t v = inb(port);
    outb(port, (uint8_t)(v & ~(1u << bit)));
    
    /* Ha slave IRQ-t nyitunk meg, a master IRQ2 (cascade) is kell! */
    if (irq >= 8) {
        uint8_t m = inb(PIC1_DATA);
        outb(PIC1_DATA, (uint8_t)(m & ~(1u << 2)));
    }
}
