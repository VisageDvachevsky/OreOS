.section .ap_trampoline, "a"
.global ap_trampoline_start
.global ap_trampoline_end

.set TRAMP_BASE, 0x8000
.set CFG_CR3, 0x8f00
.set CFG_STACK, 0x8f08
.set CFG_ENTRY, 0x8f10
.set CFG_CPU_ID, 0x8f18
.set CFG_STARTED, 0x8f1c
.set GDT_BASE, 0x8f40
.set GDT_PTR, 0x8f70

.code16
ap_trampoline_start:
    cli
    xor %ax, %ax
    mov %ax, %ds
    mov %ax, %es
    mov %ax, %ss
    mov $0x7000, %sp

    lgdt GDT_PTR
    mov %cr0, %eax
    or $0x1, %eax
    mov %eax, %cr0
    ljmp $0x08, $(TRAMP_BASE + protected_mode - ap_trampoline_start)

.code32
protected_mode:
    mov $0x10, %ax
    mov %ax, %ds
    mov %ax, %es
    mov %ax, %ss

    mov CFG_CR3, %eax
    mov %eax, %cr3

    mov %cr4, %eax
    or $(1 << 5), %eax
    mov %eax, %cr4

    mov $0xc0000080, %ecx
    rdmsr
    or $((1 << 8) | (1 << 11)), %eax
    wrmsr

    mov %cr0, %eax
    or $0x80000000, %eax
    mov %eax, %cr0
    ljmp $0x18, $(TRAMP_BASE + long_mode - ap_trampoline_start)

.code64
long_mode:
    mov $0x20, %ax
    mov %ax, %ds
    mov %ax, %es
    mov %ax, %ss
    mov CFG_STACK, %rsp
    and $-16, %rsp
    mov CFG_CPU_ID, %edi
    mov CFG_ENTRY, %rax
    call *%rax
1:
    hlt
    jmp 1b

.balign 16
ap_trampoline_end:
