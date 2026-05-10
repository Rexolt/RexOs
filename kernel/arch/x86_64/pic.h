/* Rex OS - 8259 PIC (Programmable Interrupt Controller) */
#pragma once
#include <rexos/types.h>

#define PIC1_VECTOR_OFFSET 32  /* IRQ 0..7  -> vectors 32..39 */
#define PIC2_VECTOR_OFFSET 40  /* IRQ 8..15 -> vectors 40..47 */

void pic_init(void);                /* remap + mask everything */
void pic_eoi(uint8_t irq);          /* End-of-Interrupt */
void pic_mask(uint8_t irq);
void pic_unmask(uint8_t irq);
