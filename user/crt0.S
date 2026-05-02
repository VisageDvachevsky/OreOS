.section .text
.global _start
.global syscall6
.extern main
_start:
    xor %rbp, %rbp
    call main
    mov %rax, %rdi
    mov $3, %rax
    syscall
1:
    hlt
    jmp 1b

syscall6:
    mov %rdi, %rax
    mov %rsi, %rdi
    mov %rdx, %rsi
    mov %rcx, %rdx
    mov %r8, %r10
    mov %r9, %r8
    syscall
    ret
