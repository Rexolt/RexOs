/* Rex OS - Global Descriptor Table (x86_64) */
#pragma once
#include <rexos/types.h>

/* Selector-ok (GDT index << 3 | RPL).
 * Az 5 bejegyzésünk:
 *   0: null
 *   1: kernel code  -> selector 0x08
 *   2: kernel data  -> selector 0x10
 *   3: user code    -> selector 0x18 | 3 = 0x1B   (későbbi user mode)
 *   4: user data    -> selector 0x20 | 3 = 0x23
 */
#define GDT_KERNEL_CS 0x08
#define GDT_KERNEL_DS 0x10
#define GDT_USER_DS   0x1B
#define GDT_USER_CS   0x23
#define GDT_TSS       0x28

struct tss {
    uint32_t reserved0;
    uint64_t rsp0;
    uint64_t rsp1;
    uint64_t rsp2;
    uint64_t reserved1;
    uint64_t ist1;
    uint64_t ist2;
    uint64_t ist3;
    uint64_t ist4;
    uint64_t ist5;
    uint64_t ist6;
    uint64_t ist7;
    uint64_t reserved2;
    uint16_t reserved3;
    uint16_t iopb_offset;
} __attribute__((packed));

void gdt_init(void);
void tss_set_rsp0(uint64_t rsp0);
