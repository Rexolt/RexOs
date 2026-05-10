; =============================================================================
;  Rex OS - Cooperative context switch
;
;  void context_switch(uint64_t *save_rsp, uint64_t load_rsp);
;     rdi = pointer where we save the OLD task's rsp
;     rsi = the NEW task's rsp value (loaded into rsp)
;
;  Csak callee-saved regisztereket mentünk (Sys V AMD64 ABI).
;  A C fordító már elmentette a caller-saved-eket a hívás előtt.
; =============================================================================

[BITS 64]
section .text

global context_switch
context_switch:
    push rbp
    push rbx
    push r12
    push r13
    push r14
    push r15

    mov [rdi], rsp       ; save current rsp into *save_rsp
    mov rsp, rsi         ; switch to new stack

    pop r15
    pop r14
    pop r13
    pop r12
    pop rbx
    pop rbp

    ret                  ; "visszatérés" a következő task kódjába
                         ; (új task esetén az általunk preparált entry stub-ba)
