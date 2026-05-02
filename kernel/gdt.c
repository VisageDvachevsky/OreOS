#include "kernel.h"

typedef struct {
    uint16_t limit;
    uint64_t base;
} __attribute__((packed)) GdtPtr;

typedef struct {
    uint32_t reserved0;
    uint64_t rsp0;
    uint64_t rsp1;
    uint64_t rsp2;
    uint64_t reserved1;
    uint64_t ist[7];
    uint64_t reserved2;
    uint16_t reserved3;
    uint16_t iomap_base;
} __attribute__((packed)) Tss64;

static uint64_t gdt[7];
static Tss64 tss;

static void set_tss_descriptor(uint32_t index, uint64_t base, uint32_t limit) {
    gdt[index] = ((uint64_t)(limit & 0xffff)) |
                 ((base & 0xffffffULL) << 16) |
                 (0x89ULL << 40) |
                 (((uint64_t)(limit >> 16) & 0xf) << 48) |
                 (((base >> 24) & 0xffULL) << 56);
    gdt[index + 1] = base >> 32;
}

void tss_set_rsp0(uint64_t rsp0) {
    tss.rsp0 = rsp0;
}

void gdt_init(void) {
    gdt[0] = 0;
    gdt[1] = 0x00af9a000000ffffULL;
    gdt[2] = 0x00af92000000ffffULL;
    gdt[3] = 0x00aff2000000ffffULL;
    gdt[4] = 0x00affa000000ffffULL;
    tss.iomap_base = sizeof(tss);
    set_tss_descriptor(5, (uint64_t)(uintptr_t)&tss, sizeof(tss) - 1);
    GdtPtr ptr = { .limit = sizeof(gdt) - 1, .base = (uint64_t)(uintptr_t)gdt };
    __asm__ volatile(
        "lgdt %0\n"
        "mov $0x10, %%ax\n"
        "mov %%ax, %%ds\n"
        "mov %%ax, %%es\n"
        "mov %%ax, %%ss\n"
        "pushq $0x08\n"
        "leaq 1f(%%rip), %%rax\n"
        "pushq %%rax\n"
        "lretq\n"
        "1:\n"
        "mov $0x28, %%ax\n"
        "ltr %%ax\n"
        :
        : "m"(ptr)
        : "rax", "memory");
}
