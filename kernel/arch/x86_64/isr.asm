; =============================================================================
;  Rex OS - CPU exception stubs (0..31)
;
;  CPU interrupt entry után a stack:
;     [SS] [RSP] [RFLAGS] [CS] [RIP]    <- CPU push (5 qword)
;     [error_code]                       <- ha van (vec 8,10,11,12,13,14,17,21,29,30)
;
;  Mi a stubban egységesítjük: push 0 dummy error code-ot, ha a CPU nem
;  pusholt, majd push a vektor-számot. Innentől minden stub azonos
;  layoutot adat tovább a közös rutinnak.
; =============================================================================

[BITS 64]

extern isr_common_c_handler

section .text

%macro ISR_NOERR 1
global isr%1
isr%1:
    push qword 0           ; dummy error code
    push qword %1          ; vector
    jmp isr_common_stub
%endmacro

%macro ISR_ERR 1
global isr%1
isr%1:
    ; CPU már pusholta az error code-ot
    push qword %1          ; vector
    jmp isr_common_stub
%endmacro

; --- 32 CPU exception ---------------------------------------------------------
ISR_NOERR 0     ; #DE Divide-by-Zero
ISR_NOERR 1     ; #DB Debug
ISR_NOERR 2     ;     NMI
ISR_NOERR 3     ; #BP Breakpoint
ISR_NOERR 4     ; #OF Overflow
ISR_NOERR 5     ; #BR Bound Range Exceeded
ISR_NOERR 6     ; #UD Invalid Opcode
ISR_NOERR 7     ; #NM Device Not Available
ISR_ERR   8     ; #DF Double Fault            (err)
ISR_NOERR 9     ;     Coprocessor Segment Overrun (reserved)
ISR_ERR   10    ; #TS Invalid TSS             (err)
ISR_ERR   11    ; #NP Segment Not Present     (err)
ISR_ERR   12    ; #SS Stack-Segment Fault     (err)
ISR_ERR   13    ; #GP General Protection      (err)
ISR_ERR   14    ; #PF Page Fault              (err)
ISR_NOERR 15    ;     Reserved
ISR_NOERR 16    ; #MF x87 Floating-Point
ISR_ERR   17    ; #AC Alignment Check         (err)
ISR_NOERR 18    ; #MC Machine Check
ISR_NOERR 19    ; #XM SIMD Floating-Point
ISR_NOERR 20    ; #VE Virtualization
ISR_ERR   21    ; #CP Control Protection      (err)
ISR_NOERR 22
ISR_NOERR 23
ISR_NOERR 24
ISR_NOERR 25
ISR_NOERR 26
ISR_NOERR 27
ISR_NOERR 28    ; #HV Hypervisor Injection
ISR_ERR   29    ; #VC VMM Communication       (err)
ISR_ERR   30    ; #SX Security                (err)
ISR_NOERR 31    ;     Reserved

; --- A közös rutin: minden regisztert ment, hívja a C handlert ---------------
isr_common_stub:
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

    ; A C handler első argumentuma (rdi) az interrupt_frame* = jelenlegi RSP
    mov rdi, rsp

    ; Clear direction flag (SysV ABI elvárás)
    cld

    call isr_common_c_handler

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

    ; vector + error_code (vagy a dummy) eldobása a stackről
    add rsp, 16

    iretq

; --- A C kód által hivatkozott stub-tábla ------------------------------------
section .rodata
global isr_stub_table
isr_stub_table:
    dq isr0
    dq isr1
    dq isr2
    dq isr3
    dq isr4
    dq isr5
    dq isr6
    dq isr7
    dq isr8
    dq isr9
    dq isr10
    dq isr11
    dq isr12
    dq isr13
    dq isr14
    dq isr15
    dq isr16
    dq isr17
    dq isr18
    dq isr19
    dq isr20
    dq isr21
    dq isr22
    dq isr23
    dq isr24
    dq isr25
    dq isr26
    dq isr27
    dq isr28
    dq isr29
    dq isr30
    dq isr31
