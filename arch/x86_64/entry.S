.section .text
.global kernel_entry_asm
.extern kernel_entry
kernel_entry_asm:
    cli
    mov $stack_top, %rsp
    and $-16, %rsp
    call kernel_entry
1:
    hlt
    jmp 1b

.section .bss
.align 16
stack_bottom:
    .skip 65536
stack_top:
