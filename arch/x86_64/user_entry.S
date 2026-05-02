.section .text
.global user_enter
.global syscall_entry_asm
.extern syscall_dispatch
.extern g_syscall_user_rip
.extern g_syscall_user_rsp
.extern g_sysret_override
.extern g_sysret_rip
.extern g_sysret_rsp

user_enter:
    mov %rsi, %rax
    pushq $0x1b
    pushq %rax
    pushfq
    popq %rax
    or $0x200, %rax
    pushq %rax
    pushq $0x23
    pushq %rdi
    xor %rax, %rax
    xor %rbx, %rbx
    xor %rcx, %rcx
    xor %rdx, %rdx
    xor %rbp, %rbp
    xor %r8, %r8
    xor %r9, %r9
    xor %r10, %r10
    xor %r11, %r11
    xor %r12, %r12
    xor %r13, %r13
    xor %r14, %r14
    xor %r15, %r15
    iretq

syscall_entry_asm:
    mov %rsp, user_saved_rsp(%rip)
    mov %rcx, g_syscall_user_rip(%rip)
    mov %rsp, g_syscall_user_rsp(%rip)
    mov $syscall_stack_top, %rsp
    push %r11
    push %rcx
    push %r9
    push %r8
    push %r10
    push %rdx
    push %rsi
    push %rdi
    mov %rax, %rdi
    mov 0(%rsp), %rsi
    mov 8(%rsp), %rdx
    mov 16(%rsp), %rcx
    mov 24(%rsp), %r8
    mov 32(%rsp), %r9
    xor %r10, %r10
    call syscall_dispatch
    mov %rax, syscall_ret(%rip)
    pop %rdi
    pop %rsi
    pop %rdx
    pop %r10
    pop %r8
    pop %r9
    pop %rcx
    pop %r11
    mov syscall_ret(%rip), %rax
    cmpq $0, g_sysret_override(%rip)
    je 2f
    mov g_sysret_rip(%rip), %rcx
    mov g_sysret_rsp(%rip), %rsp
    movq $0, g_sysret_override(%rip)
    sysretq
2:
    mov user_saved_rsp(%rip), %rsp
    sysretq

.section .bss
.align 16
syscall_stack:
    .skip 32768
syscall_stack_top:
user_saved_rsp:
    .quad 0
syscall_ret:
    .quad 0
