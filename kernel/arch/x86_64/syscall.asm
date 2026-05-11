; =============================================================================
;  Rex OS - Syscall Entry Stub
; =============================================================================

[BITS 64]

extern syscall_handler
extern g_current_kernel_rsp

section .bss
; Ideiglenes változó a User RSP megmentésére (Single-Core rendszereknél biztonságos,
; mert a syscall alatt az interruptok le vannak tiltva az FMASK miatt).
global g_user_rsp
g_user_rsp: resq 1

section .text

global syscall_entry
syscall_entry:
    ; A 'syscall' utasítás ide ugrik. A CPU átvált Ring 0-ba (CS=0x08, SS=0x10),
    ; elmenti a visszatérési címet az RCX-be, és az RFLAGS-t az R11-be.
    ; AZONBAN, az RSP továbbra is a User Stack-re mutat! Gyorsan át kell váltanunk
    ; a Kernel Stack-re, mielőtt bármi mást csinálunk.
    
    mov [rel g_user_rsp], rsp
    mov rsp, [rel g_current_kernel_rsp]
    
    ; Mentsük le a User RSP-t a kernel stack-re, hogy per-task legyen,
    ; mert ha a syscall blokkol (pl. yield), a globális változót felülírná más!
    push qword [rel g_user_rsp]
    
    ; Most már biztonságos (kernel) stack-en vagyunk.
    push rcx    ; User RIP (ide fogunk visszatérni)
    push r11    ; User RFLAGS
    
    push rdi
    push rsi
    push rdx
    push r10
    push r8
    push r9
    
    mov rdi, rax
    mov rsi, [rsp + 40] ; arg1 (saved RDI)
    mov rdx, [rsp + 32] ; arg2 (saved RSI)
    mov rcx, [rsp + 24] ; arg3 (saved RDX)
    mov r8,  [rsp + 16] ; arg4 (saved R10)
    mov r9,  [rsp + 8]  ; arg5 (saved R8)

    ; Syscall entry masked IF while we were still on the user stack.
    ; Now that the user RSP/RIP/RFLAGS and arguments are saved on this
    ; task's kernel stack, interrupts must be enabled again so blocking
    ; network syscalls can receive PIT/e1000 IRQs and actually time out.
    sti
    call syscall_handler
    cli
    
    pop r9
    pop r8
    pop r10
    pop rdx
    pop rsi
    pop rdi
    
    pop r11
    pop rcx
    
    ; Visszatöltjük a User RSP-t a kernel stack-ről!
    pop rsp
    
    ; sysretq: Visszatérés Ring 3-ba. RCX-ből tölti be a RIP-et, R11-ből az RFLAGS-t.
    o64 sysret

; =============================================================================
; void jmp_user_mode(uint64_t entry_point, uint64_t user_stack)
;   rdi = entry_point
;   rsi = user_stack
; =============================================================================
global jmp_user_mode
jmp_user_mode:
    ; A feladatunk, hogy manuálisan felépítsünk egy interrupt frame-et
    ; a kernel stacken, majd iretq-val beugorjunk Ring 3-ba.
    ; GDT szelektorok:
    ;   User DS = 0x1B (Index 3, RPL 3)
    ;   User CS = 0x23 (Index 4, RPL 3)
    
    ; Először frissítsük a DS, ES, FS, GS regisztereket User DS-re!
    mov ax, 0x1B
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    
    ; iretq frame felépítése (fordított sorrendben: SS, RSP, RFLAGS, CS, RIP)
    push 0x1B           ; SS (User DS)
    push rsi            ; RSP (User Stack)
    push 0x202          ; RFLAGS (IF=1)
    push 0x23           ; CS (User CS)
    push rdi            ; RIP (entry_point)
    
    iretq

