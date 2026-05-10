/* Rex OS - Interrupt Service Routine support */
#pragma once
#include <rexos/types.h>

/* Stack frame az isr_common_c_handler-nek átadva.
 * Sorrend: a memóriában a legalacsonyabb címen lévő mező a struct első tagja.
 *
 *   [low addr]  r15 (last pushed by asm)
 *               r14, r13, r12, r11, r10, r9, r8,
 *               rdi, rsi, rbp, rbx, rdx, rcx, rax
 *               vector       <- pushed by ISR stub
 *               error_code   <- pushed by CPU (or dummy 0 by stub)
 *               rip, cs, rflags, rsp, ss  <- pushed by CPU
 *   [high addr]
 */
struct interrupt_frame {
    uint64_t r15, r14, r13, r12, r11, r10, r9, r8;
    uint64_t rdi, rsi, rbp, rbx, rdx, rcx, rax;
    uint64_t vector;
    uint64_t error_code;
    uint64_t rip;
    uint64_t cs;
    uint64_t rflags;
    uint64_t rsp;
    uint64_t ss;
};

/* Az asm fájlban exportált stub-tábla: index = exception vector. */
extern void *isr_stub_table[32];

/* A közös C handler, melyet a stub-ok hívnak. */
void isr_common_c_handler(struct interrupt_frame *frame);
