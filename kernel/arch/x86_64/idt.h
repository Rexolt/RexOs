/* Rex OS - Interrupt Descriptor Table (x86_64) */
#pragma once
#include <rexos/types.h>

#define IDT_ENTRIES 256

/* Gate type-attribútum byte (P=1, DPL=0):
 *   0x8E = interrupt gate (IF auto-clear)
 *   0x8F = trap gate      (IF nem változik)
 */
#define IDT_INTR_GATE 0x8E
#define IDT_TRAP_GATE 0x8F

void idt_init(void);
void idt_set_entry(int vec, void *handler, uint16_t selector, uint8_t type_attr);
