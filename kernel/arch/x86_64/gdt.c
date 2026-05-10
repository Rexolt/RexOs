/* ============================================================================
 *  Rex OS - GDT init (x86_64, long mode)
 *
 *  Egy GDT entry 8 byte:
 *    limit_low[15:0]
 *    base_low[15:0]
 *    base_mid[7:0]
 *    access:    P | DPL[1:0] | S | type[3:0]
 *               P=1 present, DPL=ring, S=1 code/data (0=system),
 *               type code: bit3=1, bit2=conforming, bit1=read, bit0=accessed
 *               type data: bit3=0, bit2=expand-down, bit1=write, bit0=accessed
 *    granularity: G | D/B | L | AVL | limit_high[3:0]
 *                 G=1 4KB blocks, L=1 long-mode code, D/B=0 in 64-bit code
 *    base_high[7:0]
 *
 *  Long mode-ban a base/limit többnyire ignorálva van CS/DS-re, de a
 *  flag bitek (különösen az L bit) kritikusak.
 * ========================================================================== */

#include <arch/x86_64/gdt.h>
#include <lib/string.h>

struct gdt_entry {
    uint16_t limit_low;
    uint16_t base_low;
    uint8_t  base_mid;
    uint8_t  access;
    uint8_t  granularity;
    uint8_t  base_high;
} __packed;

struct gdt_ptr {
    uint16_t limit;
    uint64_t base;
} __packed;

#define GDT_ENTRIES 7

static struct gdt_entry s_gdt[GDT_ENTRIES];
static struct gdt_ptr   s_gdt_ptr;
static struct tss       s_tss;

void tss_set_rsp0(uint64_t rsp0)
{
    s_tss.rsp0 = rsp0;
}

static void gdt_set_tss(int i, uint64_t base, uint32_t limit)
{
    s_gdt[i].limit_low   = (uint16_t)(limit & 0xFFFF);
    s_gdt[i].base_low    = (uint16_t)(base  & 0xFFFF);
    s_gdt[i].base_mid    = (uint8_t)((base  >> 16) & 0xFF);
    s_gdt[i].access      = 0x89; /* Present, DPL=0, Type=9 (Available 64-bit TSS) */
    s_gdt[i].granularity = (uint8_t)(((limit >> 16) & 0x0F) | 0x00);
    s_gdt[i].base_high   = (uint8_t)((base  >> 24) & 0xFF);
    
    s_gdt[i+1].limit_low   = (uint16_t)(base >> 32);
    s_gdt[i+1].base_low    = (uint16_t)(base >> 48);
    s_gdt[i+1].base_mid    = 0;
    s_gdt[i+1].access      = 0;
    s_gdt[i+1].granularity = 0;
    s_gdt[i+1].base_high   = 0;
}

static void gdt_set(int i, uint32_t base, uint32_t limit, uint8_t access, uint8_t flags)
{
    s_gdt[i].limit_low   = (uint16_t)(limit & 0xFFFF);
    s_gdt[i].base_low    = (uint16_t)(base  & 0xFFFF);
    s_gdt[i].base_mid    = (uint8_t)((base  >> 16) & 0xFF);
    s_gdt[i].access      = access;
    s_gdt[i].granularity = (uint8_t)(((limit >> 16) & 0x0F) | (flags & 0xF0));
    s_gdt[i].base_high   = (uint8_t)((base  >> 24) & 0xFF);
}

void gdt_init(void)
{
    s_gdt_ptr.limit = (uint16_t)(sizeof(s_gdt) - 1);
    s_gdt_ptr.base  = (uint64_t)&s_gdt;

    /* Null descriptor */
    gdt_set(0, 0, 0, 0, 0);

    /* Kernel code: P=1 DPL=0 S=1 type=0xA (exec/read) -> access=0x9A
     * Flags: G=1, L=1 -> upper nibble 0xA0 */
    gdt_set(1, 0, 0xFFFFF, 0x9A, 0xA0);

    /* Kernel data: P=1 DPL=0 S=1 type=0x2 (read/write) -> access=0x92
     * Flags: G=1, D/B=1 -> upper nibble 0xC0 */
    gdt_set(2, 0, 0xFFFFF, 0x92, 0xC0);

    /* User data: DPL=3 -> access=0xF2 */
    gdt_set(3, 0, 0xFFFFF, 0xF2, 0xC0);

    /* User code: DPL=3 -> access=0xFA, long mode flags */
    gdt_set(4, 0, 0xFFFFF, 0xFA, 0xA0);

    /* TSS descriptor occupies 2 slots (5 and 6) */
    kmemset(&s_tss, 0, sizeof(s_tss));
    s_tss.iopb_offset = sizeof(s_tss);
    gdt_set_tss(5, (uint64_t)&s_tss, sizeof(s_tss) - 1);

    /* Betöltés és a szegmens-regiszterek újratöltése.
     * CS reload: long mode-ban nincs direkt 'mov cs, ax', ezért
     * "fake far ret" trükköt használunk (push selector, push label, lretq). */
    __asm__ volatile (
        "lgdt %0\n\t"
        "pushq $0x08\n\t"           /* GDT_KERNEL_CS */
        "leaq 1f(%%rip), %%rax\n\t"
        "pushq %%rax\n\t"
        "lretq\n"
        "1:\n\t"
        "movw $0x10, %%ax\n\t"      /* GDT_KERNEL_DS */
        "movw %%ax, %%ds\n\t"
        "movw %%ax, %%es\n\t"
        "movw %%ax, %%fs\n\t"
        "movw %%ax, %%gs\n\t"
        "movw %%ax, %%ss\n\t"
        
        /* Load TSS */
        "movw $0x28, %%ax\n\t"
        "ltr %%ax\n\t"
        : : "m"(s_gdt_ptr) : "rax", "memory"
    );
}
