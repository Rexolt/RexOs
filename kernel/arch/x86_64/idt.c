/* ============================================================================
 *  Rex OS - IDT init
 *
 *  Egy 64-bit IDT entry 16 byte:
 *    offset_low [15:0]
 *    selector [15:0]              (mindig kernel CS = 0x08)
 *    ist [2:0]   | reserved [7:3]
 *    type_attr [7:0]              (0x8E = interrupt gate, P=1, DPL=0)
 *    offset_mid [31:16]
 *    offset_high [63:32]
 *    reserved [31:0]
 * ========================================================================== */

#include <arch/x86_64/idt.h>
#include <arch/x86_64/gdt.h>
#include <arch/x86_64/isr.h>
#include <lib/string.h>

struct idt_entry {
    uint16_t offset_low;
    uint16_t selector;
    uint8_t  ist;
    uint8_t  type_attr;
    uint16_t offset_mid;
    uint32_t offset_high;
    uint32_t reserved;
} __packed;

struct idt_ptr {
    uint16_t limit;
    uint64_t base;
} __packed;

static struct idt_entry s_idt[IDT_ENTRIES];
static struct idt_ptr   s_idt_ptr;

void idt_set_entry(int vec, void *handler, uint16_t selector, uint8_t type_attr)
{
    uint64_t addr = (uint64_t)handler;
    s_idt[vec].offset_low  = (uint16_t)(addr & 0xFFFF);
    s_idt[vec].selector    = selector;
    s_idt[vec].ist         = 0;
    s_idt[vec].type_attr   = type_attr;
    s_idt[vec].offset_mid  = (uint16_t)((addr >> 16) & 0xFFFF);
    s_idt[vec].offset_high = (uint32_t)((addr >> 32) & 0xFFFFFFFFu);
    s_idt[vec].reserved    = 0;
}

void idt_init(void)
{
    /* Üres IDT - bármilyen ismeretlen interrupt #GP-t okoz, amit
     * tovább kezelünk az isr_common_c_handler-ben (vector 13). */
    kmemset(s_idt, 0, sizeof(s_idt));

    /* CPU exceptionök 0..31 bekötése a stub-okhoz. */
    for (int i = 0; i < 32; i++) {
        idt_set_entry(i, isr_stub_table[i], GDT_KERNEL_CS, IDT_INTR_GATE);
    }

    s_idt_ptr.limit = (uint16_t)(sizeof(s_idt) - 1);
    s_idt_ptr.base  = (uint64_t)&s_idt;

    __asm__ volatile ("lidt %0" : : "m"(s_idt_ptr));
}
