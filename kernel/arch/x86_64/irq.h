/* Rex OS - Hardware IRQ infrastructure (vectors 32..47) */
#pragma once
#include <rexos/types.h>
#include <arch/x86_64/isr.h>   /* interrupt_frame */

typedef void (*irq_handler_t)(uint8_t irq, struct interrupt_frame *frame);

/* PIC remap + IDT bekötés a 16 stub-ra. */
void irq_init(void);

/* Egy handler regisztrálása. Automatikusan unmask-eli az IRQ-t a PIC-en. */
void irq_register(uint8_t irq, irq_handler_t handler);
void irq_unregister(uint8_t irq);

/* Az asm fájl exportja: stub-tábla 16 IRQ-ra. */
extern void *irq_stub_table[16];

/* A közös C dispatcher, melyet az asm stub hív. */
void irq_common_c_handler(struct interrupt_frame *frame);
