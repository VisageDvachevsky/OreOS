.section .text
.global thread_entry_asm
.global context_switch
.extern thread_bootstrap
thread_entry_asm:
    and $-16, %rsp
    sub $8, %rsp
    jmp thread_bootstrap

context_switch:
    push %rbp
    push %rbx
    push %r12
    push %r13
    push %r14
    push %r15
    mov %rsp, (%rdi)
    mov %rsi, %rsp
    pop %r15
    pop %r14
    pop %r13
    pop %r12
    pop %rbx
    pop %rbp
    ret
1:
    hlt
    jmp 1b
