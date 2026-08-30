/* RexOS user-mode C runtime belépési pont (newlib-es appokhoz).
 *
 * A kernel iretq-val adja át a vezérlést; a stack tetején egy hamis
 * visszatérési cím (0) van, így RSP%16 == 8 - pontosan az az állapot,
 * amit a SysV ABI egy függvény belépésekor elvár. A .bss-t az ELF
 * loader nullázza, ezért itt csak main-t hívunk. */
    .text
    .global _start
    .type _start, @function
_start:
    xor  %rbp, %rbp          /* stack frame lánc vége (backtrace-hez) */
    and  $-16, %rsp          /* 16-ra igazítás: a kernel %16==8-cal ad át
                                (C-függvényként kezeli a _start-ot), de a
                                call előtt %16==0 kell a SysV ABI-hoz */
    call __libc_init_array   /* .init_array konstruktorok (newlib helper) -
                                enélkül az __attribute__((constructor))
                                regisztrációk (pl. libnsfb surface-ek) nem
                                futnának le */
    mov  $1, %edi            /* argc = 1 */
    lea  __rexos_argv(%rip), %rsi  /* argv = {"app", NULL} */
    lea  __rexos_envp(%rip), %rdx  /* envp = {NULL} */
    call main
    mov  %eax, %edi          /* main visszatérési értéke -> exit kód */
    call exit                /* newlib exit: atexit + stdio flush + _exit */
1:  jmp  1b
    .size _start, . - _start

/* A newlib __libc_init_array/__libc_fini_array _init/_fini-t hív (crti.o
 * szerepe lenne); nálunk üres stub - a konstruktorok az .init_array-ből
 * futnak. */
    .global _init
    .type _init, @function
_init:
    ret
    .global _fini
    .type _fini, @function
_fini:
    ret

    .section .rodata
__rexos_arg0:
    .asciz "app"

    .section .data
    .align 8
__rexos_argv:
    .quad __rexos_arg0
__rexos_envp:               /* argv NULL lezárója egyben az üres envp is */
    .quad 0

    .section .note.GNU-stack,"",@progbits
