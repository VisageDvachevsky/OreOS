#include "kernel.h"

CpuInfo g_cpus[ORE_MAX_CPUS];
uint32_t g_cpu_count;

uint64_t read_cr3(void) {
    uint64_t value;
    __asm__ volatile("mov %%cr3, %0" : "=r"(value));
    return value;
}

uint64_t read_cr2(void) {
    uint64_t value;
    __asm__ volatile("mov %%cr2, %0" : "=r"(value));
    return value;
}

void write_cr3(uint64_t value) {
    __asm__ volatile("mov %0, %%cr3" : : "r"(value) : "memory");
}

uint64_t read_msr(uint32_t msr) {
    uint32_t lo, hi;
    __asm__ volatile("rdmsr" : "=a"(lo), "=d"(hi) : "c"(msr));
    return ((uint64_t)hi << 32) | lo;
}

void write_msr(uint32_t msr, uint64_t value) {
    __asm__ volatile("wrmsr" : : "c"(msr), "a"((uint32_t)value), "d"((uint32_t)(value >> 32)));
}

void cpu_enable_nxe(void) {
    uint64_t efer = read_msr(0xc0000080U);
    write_msr(0xc0000080U, efer | (1ULL << 11));
}

uint32_t current_cpu_id(void) {
    uint8_t apic_id = (uint8_t)(mmio_read32(acpi_lapic_base() + 0x20) >> 24);
    for (uint32_t i = 0; i < g_cpu_count; ++i) {
        if (g_cpus[i].apic_id == apic_id) return i;
    }
    return 0;
}
