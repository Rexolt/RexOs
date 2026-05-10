; =============================================================================
;  Rex OS - Hardware IRQ stubs (vectors 32..47) + software yield (0xFE)
;
;  Az IRQ-knál a CPU NEM nyomja a stack-re az error code-ot. Mi push 0 dummy-t
;  + a vektor-számot, így a frame layout megegyezik az exception (ISR)
;  stub-okéval -> ugyanazt az `interrupt_frame` struct-ot használhatjuk.
;
;  PHASE 9: A C handler után meghívjuk a sched_maybe_switch-et. Ha az
;  != 0-t ad vissza, az új rsp-vel felülírjuk a sajátunkat -> a pop+iretq
;  szekvencia egy MÁSIK task állapotát fogja visszaállítani. Ez a teljes
;  preempció + cooperative yield egységes mechanizmusa.
; =============================================================================

[BITS 64]

extern irq_common_c_handler
extern sched_maybe_switch

section .text

%macro IRQ_STUB 2  ; %1 = IRQ index (0..15), %2 = vector (32..47)
global irq%1
irq%1:
    push qword 0
    push qword %2
    jmp irq_common_stub
%endmacro

IRQ_STUB 0,  32
IRQ_STUB 1,  33
IRQ_STUB 2,  34
IRQ_STUB 3,  35
IRQ_STUB 4,  36
IRQ_STUB 5,  37
IRQ_STUB 6,  38
IRQ_STUB 7,  39
IRQ_STUB 8,  40
IRQ_STUB 9,  41
IRQ_STUB 10, 42
IRQ_STUB 11, 43
IRQ_STUB 12, 44
IRQ_STUB 13, 45
IRQ_STUB 14, 46
IRQ_STUB 15, 47

; --- Software yield interrupt (vector 0xFE) -----------------------------
; A task_yield() ezt triggereli (int $0xFE). Ugyanúgy push 0 + vector,
; majd ugorjunk az irq_common_stub-ra. A C handler mit sem fog csinálni
; (hardware vector tartomány alatt/felett van, EOI sem kell), de az
; epilógus a need_resched flaget látva át fog váltani.
global yield_int_stub
yield_int_stub:
    push qword 0
    push qword 0xFE
    jmp irq_common_stub

irq_common_stub:
    push rax
    push rcx
    push rdx
    push rbx
    push rbp
    push rsi
    push rdi
    push r8
    push r9
    push r10
    push r11
    push r12
    push r13
    push r14
    push r15

    mov rdi, rsp
    cld

    call irq_common_c_handler

    ; --- Phase 9: resched epilógus -----------------------------------
    ; Az aktuális rsp az IRQ frame tetején (r15 alján). Ezt átadjuk
    ; a sched_maybe_switch-nek; ha 0-t ad vissza, marad minden, ha
    ; nem 0-t, az visszakapott érték LESZ az új rsp.
    mov rdi, rsp
    call sched_maybe_switch
    test rax, rax
    jz .no_switch
    mov rsp, rax
.no_switch:

    pop r15
    pop r14
    pop r13
    pop r12
    pop r11
    pop r10
    pop r9
    pop r8
    pop rdi
    pop rsi
    pop rbp
    pop rbx
    pop rdx
    pop rcx
    pop rax

    add rsp, 16   ; eldobjuk a vektor + dummy error code-ot
    iretq

section .rodata
global irq_stub_table
irq_stub_table:
    dq irq0
    dq irq1
    dq irq2
    dq irq3
    dq irq4
    dq irq5
    dq irq6
    dq irq7
    dq irq8
    dq irq9
    dq irq10
    dq irq11
    dq irq12
    dq irq13
    dq irq14
    dq irq15
