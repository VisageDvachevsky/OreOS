ENTRY(kernel_entry_asm)

SECTIONS {
    . = 0x100000;
    __kernel_start = .;
    .text : { *(.text .text.*) }
    .rodata : { *(.rodata .rodata.*) }
    .data : { *(.data .data.*) }
    .bss : {
        *(COMMON)
        *(.bss .bss.*)
    }
    __kernel_end = .;
}
